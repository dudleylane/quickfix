%{
#include <cstdio>
#include <unistd.h>

// An exception a Python callback lets out, other than the FIX exceptions the
// engine expects, ends the process (#122). Py_Exit would finalize the
// interpreter, deleting the module's transports under the engine threads still
// running in them; it crashed on the reactor transports. So print the exception
// as Python prints an uncaught one, flush Python's and C's buffered output, and
// _exit, which runs no finalizer. PyErr_Print would itself call Py_Exit for a
// SystemExit, so that one is handled here: its code is the exit status.
[[noreturn]] void exitOnCallbackError()
{
  int status = 1;
  if( PyErr_ExceptionMatches(PyExc_SystemExit) )
  {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_NormalizeException( &ptype, &pvalue, &ptraceback );
    PyObject *code = pvalue ? PyObject_GetAttrString( pvalue, "code" ) : nullptr;
    if( code == Py_None )
      status = 0;
    else if( code && PyLong_Check(code) )
      status = static_cast<int>( PyLong_AsLong(code) );
    else if( code )
    {
      PyObject *err = PySys_GetObject( "stderr" );
      if( err && err != Py_None && PyFile_WriteObject( code, err, Py_PRINT_RAW ) == 0 )
        PyFile_WriteString( "\n", err );
    }
    Py_XDECREF( code );
    Py_XDECREF( ptype );
    Py_XDECREF( pvalue );
    Py_XDECREF( ptraceback );
    PyErr_Clear();
  }
  else
  {
    PyErr_Print();
  }
  for( const char *name : { "stdout", "stderr" } )
  {
    PyObject *stream = PySys_GetObject( name );
    if( stream && stream != Py_None )
      Py_XDECREF( PyObject_CallMethod( stream, "flush", nullptr ) );
    PyErr_Clear();
  }
  fflush( nullptr );
  _exit( status );
}
%}

%exception
{
  if(!tryPythonException([&]() mutable 
  { 
    $action
    return true;
  fail:
    return false;
  })) 
  {
    SWIG_fail;
  }
}

%typemap(in) std::string& (std::string temp) {
  // PyUnicode_AsUTF8 fails, with a TypeError set, for anything but a str;
  // constructing a std::string from its null aborted the process (#124).
  const char *utf8 = PyUnicode_AsUTF8($input);
  if( !utf8 )
    SWIG_fail;
  temp = std::string(utf8);
  $1 = &temp;
}

%typemap(argout) std::string& {
  if( std::string("$1_type") == "std::string &" )
  {
    if( !PyDict_Check(resultobj) )
    {
      Py_XDECREF(resultobj);
      resultobj = PyDict_New();
    }
    // PyDict_SetItem takes its own references to both.
    PyObject *key = PyLong_FromSsize_t(PyDict_Size(resultobj));
    PyObject *value = PyUnicode_FromString($1->c_str());
    PyDict_SetItem( resultobj, key, value );
    Py_XDECREF(key);
    Py_XDECREF(value);
  }
}

%typemap(in) int& (int temp) {
  if( !SWIG_IsOK(SWIG_AsVal_int($input, &temp)) )
    SWIG_exception_fail(SWIG_TypeError, "in method '$symname', argument $argnum must be an int");
  $1 = &temp;
}

%typemap(argout) int& {
  if( std::string("$1_type") == "int &" )
  {
    if( !PyDict_Check(resultobj) )
    {
      Py_XDECREF(resultobj);
      resultobj = PyDict_New();
    }
    PyObject *key = PyLong_FromSsize_t(PyDict_Size(resultobj));
    PyObject *value = PyLong_FromLong(*$1);
    PyDict_SetItem( resultobj, key, value );
    Py_XDECREF(key);
    Py_XDECREF(value);
  }
}

// A DataDictionary lookup that finds nothing returns None. The out-parameter
// typemaps above replace the bool with a dict of the outputs, so a miss looked
// like a hit: getFieldName(999, "") was {0: ''} (#124). A hit keeps the dict.
%typemap(ret) bool FIX::DataDictionary::getFieldName, bool FIX::DataDictionary::getFieldTag,
              bool FIX::DataDictionary::getValueName, bool FIX::DataDictionary::getNameValue,
              bool FIX::DataDictionary::getGroup {
  if( !$1 )
  {
    Py_XDECREF($result);
    $result = Py_None;
    Py_INCREF(Py_None);
  }
}

%typemap(in) FIX::DataDictionary const *& {
  // getGroup sets the pointer only when it finds the group (#121).
  $1 = new FIX::DataDictionary *[1];
  *$1 = 0;
}

%typemap(freearg) FIX::DataDictionary const *& {
  delete[] $1;
}

%typemap(argout) FIX::DataDictionary const *& {
  if( *$1 )
  {
    void* argp = 0;
    int res = SWIG_ConvertPtr($input, &argp, SWIGTYPE_p_FIX__DataDictionary, 0 );
    if( !SWIG_IsOK(res) || !argp )
      SWIG_exception_fail(SWIG_TypeError, "getGroup needs a DataDictionary to copy the group's dictionary into");
    *reinterpret_cast<FIX::DataDictionary *>(argp) = *(*$1);
  }
}

%extend FIX::UtcTimeStamp {
  PyObject *getDateTime() {
      int y, m, d, h, mi, s, fs;
      $self->getYMD(y, m, d);
      $self->getHMS(h, mi, s, fs, 6);
      return PyDateTime_FromDateAndTime(y, m, d, h, mi, s, fs);
  }
}

%rename(FIXException) FIX::Exception;

%include ../quickfix.i

%pythoncode %{
try:
  import thread
except ImportError:
  import _thread as thread

def _quickfix_start_thread(i_or_a):
  i_or_a.block()
%}

%feature("shadow") FIX::Initiator::start() %{
def start(self):
  thread.start_new_thread(_quickfix_start_thread, (self,))
%}

%feature("shadow") FIX::Acceptor::start() %{
def start(self):
  thread.start_new_thread(_quickfix_start_thread, (self,))
%}

%feature("director:except") FIX::Log::clear {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}
%feature("director:except") FIX::Log::backup {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}
%feature("director:except") FIX::Log::onIncoming {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}
%feature("director:except") FIX::Log::onOutgoing {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}
%feature("director:except") FIX::Log::onEvent {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}

%feature("director:except") FIX::LogFactory::create {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}
%feature("director:except") FIX::LogFactory::destroy {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}



%feature("director:except") FIX::Application::onCreate {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}

%feature("director:except") FIX::Application::onLogon {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}

%feature("director:except") FIX::Application::onLogout {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}

%feature("director:except") FIX::Application::toAdmin {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    PyErr_Restore( ptype, pvalue, ptraceback );
    exitOnCallbackError();
  }
}

%feature("director:except") FIX::Application::toApp {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    void *result;

    if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__DoNotSend, 0 ) != -1 ) {
      throw *((FIX::DoNotSend*)result);
    } else {
      PyErr_Restore( ptype, pvalue, ptraceback );
      exitOnCallbackError();
    }
  }
}

%feature("director:except") FIX::Application::fromAdmin {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    void *result;

    if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__FieldNotFound, 0 ) != -1 ) {
      throw *((FIX::FieldNotFound*)result);
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__IncorrectDataFormat, 0 ) != -1 ) {
      throw *((FIX::IncorrectDataFormat*)result);
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__IncorrectTagValue, 0 ) != -1 ) {
      throw *((FIX::IncorrectTagValue*)result);
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__RejectLogon, 0 ) != -1 ) {
      throw *((FIX::RejectLogon*)result);
    } else {
      PyErr_Restore( ptype, pvalue, ptraceback );
      exitOnCallbackError();
    }
  }
}

%feature("director:except") FIX::Application::fromApp {
  if( $error != NULL ) {
    PyObject *ptype, *pvalue, *ptraceback;
    PyErr_Fetch( &ptype, &pvalue, &ptraceback );
    void *result;

    if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__FieldNotFound, 0 ) != -1 ) {
      throw *((FIX::FieldNotFound*)result);
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__IncorrectDataFormat, 0 ) != -1 ) {
      throw *((FIX::IncorrectDataFormat*)result);
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__IncorrectTagValue, 0 ) != -1 ) {
      throw *((FIX::IncorrectTagValue*)result);
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__UnsupportedMessageType, 0 ) != -1 ) {
      throw *((FIX::UnsupportedMessageType*)result);
    } else {
      PyErr_Restore( ptype, pvalue, ptraceback );
      exitOnCallbackError();
    }
  }
}

%pythoncode %{
class SocketInitiator(SocketInitiatorBase):
  application = 0
  storeFactory = 0
  setting = 0
  logFactory = 0

  def __init__(self, application, storeFactory, settings, logFactory=None):
    if logFactory == None:
      SocketInitiatorBase.__init__(self, application, storeFactory, settings)
    else:
      SocketInitiatorBase.__init__(self, application, storeFactory, settings, logFactory)

    self.application = application
    self.storeFactory = storeFactory
    self.settings = settings
    self.logFactory = logFactory

class SocketAcceptor(SocketAcceptorBase):
  application = 0
  storeFactory = 0
  setting = 0
  logFactory = 0

  def __init__(self, application, storeFactory, settings, logFactory=None):
    if logFactory == None:
      SocketAcceptorBase.__init__(self, application, storeFactory, settings)
    else:
      SocketAcceptorBase.__init__(self, application, storeFactory, settings, logFactory)

    self.application = application
    self.storeFactory = storeFactory
    self.settings = settings
    self.logFactory = logFactory

class ThreadedSocketInitiator(ThreadedSocketInitiatorBase):
  application = 0
  storeFactory = 0
  setting = 0
  logFactory = 0

  def __init__(self, application, storeFactory, settings, logFactory=None):
    if logFactory == None:
      ThreadedSocketInitiatorBase.__init__(self, application, storeFactory, settings)
    else:
      ThreadedSocketInitiatorBase.__init__(self, application, storeFactory, settings, logFactory)

    self.application = application
    self.storeFactory = storeFactory
    self.settings = settings
    self.logFactory = logFactory

class ThreadedSocketAcceptor(ThreadedSocketAcceptorBase):
  application = 0
  storeFactory = 0
  setting = 0
  logFactory = 0

  def __init__(self, application, storeFactory, settings, logFactory=None):
    if logFactory == None:
      ThreadedSocketAcceptorBase.__init__(self, application, storeFactory, settings)
    else:
      ThreadedSocketAcceptorBase.__init__(self, application, storeFactory, settings, logFactory)

    self.application = application
    self.storeFactory = storeFactory
    self.settings = settings
    self.logFactory = logFactory

#if (HAVE_SSL > 0)
class SSLSocketInitiator(SSLSocketInitiatorBase):
  application = 0
  storeFactory = 0
  setting = 0
  logFactory = 0

  def __init__(self, application, storeFactory, settings, logFactory=None):
    if logFactory == None:
      SSLSocketInitiatorBase.__init__(self, application, storeFactory, settings)
    else:
      SSLSocketInitiatorBase.__init__(self, application, storeFactory, settings, logFactory)

    self.application = application
    self.storeFactory = storeFactory
    self.settings = settings
    self.logFactory = logFactory

class SSLSocketAcceptor(SSLSocketAcceptorBase):
  application = 0
  storeFactory = 0
  setting = 0
  logFactory = 0

  def __init__(self, application, storeFactory, settings, logFactory=None):
    if logFactory == None:
      SSLSocketAcceptorBase.__init__(self, application, storeFactory, settings)
    else:
      SSLSocketAcceptorBase.__init__(self, application, storeFactory, settings, logFactory)

    self.application = application
    self.storeFactory = storeFactory
    self.settings = settings
    self.logFactory = logFactory

class ThreadedSSLSocketInitiator(ThreadedSSLSocketInitiatorBase):
  def __init__(self, application, storeFactory, settings, logFactory=None):
    if logFactory == None:
      ThreadedSSLSocketInitiatorBase.__init__(self, application, storeFactory, settings)
    else:
      ThreadedSSLSocketInitiatorBase.__init__(self, application, storeFactory, settings, logFactory)

class ThreadedSSLSocketAcceptor(ThreadedSSLSocketAcceptorBase):
  def __init__(self, application, storeFactory, settings, logFactory=None):
    if logFactory == None:
      ThreadedSSLSocketAcceptorBase.__init__(self, application, storeFactory, settings)
    else:
      ThreadedSSLSocketAcceptorBase.__init__(self, application, storeFactory, settings, logFactory)
#endif

# An initiator or acceptor holds C++ references to what it was made with. The classes above keep
# theirs, but the *Base classes SWIG generates kept nothing, so an argument written inline was freed
# when the constructor returned and the transport used freed memory (#110). Every transport class
# keeps its arguments, as quickfix_ruby.rb does for Ruby (#107).
def _keepArguments(cls):
  construct = cls.__init__
  def __init__(self, *args, **kwargs):
    construct(self, *args, **kwargs)
    self._quickfixArguments = (args, kwargs)
  cls.__init__ = __init__

def _transportClasses(base):
  for cls in base.__subclasses__():
    yield cls
    yield from _transportClasses(cls)

for _cls in set(_transportClasses(Initiator)) | set(_transportClasses(Acceptor)):
  _keepArguments(_cls)
del _cls
%}

%init %{
#ifndef _MSC_VER
      struct sigaction new_action, old_action;
      new_action.sa_handler = SIG_DFL;
      sigemptyset( &new_action.sa_mask );
      new_action.sa_flags = 0;
      sigaction( SIGINT, &new_action, &old_action );
#endif
%}

