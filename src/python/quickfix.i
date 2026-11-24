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

// A view holds its owner in an attribute, and a call whose result is its argument returns the argument, so
// neither outlives what it points into (#194; ../quickfix.i). SWIG calls the result val. Features go before the
// %include: one declared after the class has been read does not apply.
%define QUICKFIX_PYTHON_KEEPS_OWNER(method)
%feature("pythonappend") method %{val._quickfixOwner = self%}
%enddef
QUICKFIX_PYTHON_KEEPS_OWNER(FIX::Message::getHeader)
QUICKFIX_PYTHON_KEEPS_OWNER(FIX::Message::getTrailer)
QUICKFIX_PYTHON_KEEPS_OWNER(FIX::FieldMap::getGroupRef)
QUICKFIX_PYTHON_KEEPS_OWNER(FIX::FieldMap::getGroupPtr)
QUICKFIX_PYTHON_KEEPS_OWNER(FIX::SessionSettings::get)
%feature("pythonappend") FIX::FieldMap::getField %{if len(args) == 1 and isinstance(args[0], FieldBase): val = args[0]%}
%feature("pythonappend") FIX::FieldMap::getGroup %{val = group%}
%feature("pythonappend") FIX::Message::getGroup %{val = group%}
%feature("pythonappend") FIX::Header::getGroup %{val = group%}
%feature("pythonappend") FIX::Trailer::getGroup %{val = group%}
%feature("pythonappend") FIX::Group::getGroup %{val = group%}

// A proxy whose C++ pointer is null -- a view the binding has ended, because what it pointed at was destroyed
// (quickfixEndView) -- raises ReferenceError wherever it is passed, as self or as a pointer argument. SWIG's own
// typemap (typemaps/swigtype.swg, copied here with the one test added) converted it to a null pointer: a method ran
// on nullptr, and a pointer argument read it as none, as setString does a dictionary. None still passes as a null
// pointer, and a reference argument raises already (SWIG's null-reference check).
%typemap(in, noblock=1) SWIGTYPE * (void *argp = 0, int res = 0) {
  res = SWIG_ConvertPtr($input, &argp, $descriptor, $disown | %convertptr_flags);
  if (!SWIG_IsOK(res)) {
    %argument_fail(res, "$type", $symname, $argnum);
  }
  if (!argp && $input != Py_None) {
    PyErr_SetString(PyExc_ReferenceError, "in method '$symname', argument $argnum: what it refers to was destroyed");
    SWIG_fail;
  }
  $1 = %reinterpret_cast(argp, $ltype);
}

%{
// Ends a view: a proxy that owns nothing, of an object that is destroyed or about to be. Its pointer becomes null,
// so any later use raises (the typemap above). An owner is never ended: its pointer is what frees its object.
bool quickfixEndView(PyObject *view)
{
  SwigPyObject *proxy = SWIG_Python_GetSwigThis(view);
  if (!proxy || proxy->own)
    return false;
  proxy->ptr = 0;
  return true;
}
%}
// For the tests: fix._endView(view) ends view, and says whether it could. With the GIL: finding a proxy's object
// reads its attributes.
%rename(_endView) quickfixTestEndView;
%nothread quickfixTestEndView;
%inline %{
bool quickfixTestEndView(PyObject *view) { return quickfixEndView(view); }
%}

// A call that destroys a group a view may point at (#196; ../quickfix.i): the exception translation above, with
// BEFORE ahead of $action. $action releases the GIL only inside itself (-threads), so BEFORE, and the destructor of
// anything it declares, run with it.
%define QUICKFIX_VIEWS_EXCEPTION(decl, BEFORE)
%exception decl
{
  if(!tryPythonException([&]() mutable
  {
    BEFORE
    $action
    return true;
  fail:
    return false;
  }))
  {
    SWIG_fail;
  }
}
%enddef
%define QUICKFIX_VIEWS_SESSION_EXCEPTION(decl, BEFORE)
QUICKFIX_VIEWS_EXCEPTION(decl, BEFORE)
%enddef

// A group's view is registered, so the calls that destroy its group can end it (QuickfixViews, below). One that
// cannot be is not handed out: the call raises instead.
%typemap(ret, noblock=1) FIX::FieldMap &getGroupRef, FIX::FieldMap *getGroupPtr {
  if ($1 && $result && !QuickfixViews::registerView($result, $1)) {
    Py_DECREF($result);
    $result = NULL;
  }
}

// So is what a transport hands out of itself, apart from the group views (#197; QuickfixTransportViews below), so
// that its destructor, or its constructor when it throws, can end them. getLog's result is not a view when it is a
// director: that is the script's own Log, which its LogFactory made.
%typemap(ret, noblock=1) FIX::Session *lookupSession, FIX::Session *registerSession, FIX::Session *getSession {
  if ($1 && $result && !QuickfixTransportViews::registerView($result, $1, true)) {
    Py_DECREF($result);
    $result = NULL;
  }
}
%typemap(ret, noblock=1) FIX::Log *getLog {
  if ($1 && $result && !dynamic_cast<Swig::Director *>($1) && !QuickfixTransportViews::registerView($result, $1, false)) {
    Py_DECREF($result);
    $result = NULL;
  }
}
// The transports' constructors and destructors. BEFORE runs with the GIL, and what it declares is destroyed with
// it: after $action, or as $action's exception leaves, once $action has taken the GIL back.
%define QUICKFIX_TRANSPORT_VIEWS(Class, Name)
QUICKFIX_VIEWS_EXCEPTION(Class::Name, QuickfixTransportViews::Construction construction;)
QUICKFIX_VIEWS_EXCEPTION(Class::~Name, QuickfixTransportViews::Teardown<Class> teardown(*arg1);)
%enddef
QUICKFIX_TRANSPORT_VIEWS(FIX::SocketInitiator, SocketInitiator)
QUICKFIX_TRANSPORT_VIEWS(FIX::SocketAcceptor, SocketAcceptor)
QUICKFIX_TRANSPORT_VIEWS(FIX::ThreadedSocketInitiator, ThreadedSocketInitiator)
QUICKFIX_TRANSPORT_VIEWS(FIX::ThreadedSocketAcceptor, ThreadedSocketAcceptor)
QUICKFIX_TRANSPORT_VIEWS(FIX::SSLSocketInitiator, SSLSocketInitiator)
QUICKFIX_TRANSPORT_VIEWS(FIX::SSLSocketAcceptor, SSLSocketAcceptor)
QUICKFIX_TRANSPORT_VIEWS(FIX::ThreadedSSLSocketInitiator, ThreadedSSLSocketInitiator)
QUICKFIX_TRANSPORT_VIEWS(FIX::ThreadedSSLSocketAcceptor, ThreadedSSLSocketAcceptor)

%include ../quickfix.i

// The group views handed out (#196; ../quickfix.i): each view's `this` object, by the address of its group. The
// registry holds a reference to it, as the view's proxy does, so the object of a view nothing else holds has a
// count of 1, and a prune drops it: at a registration once the registry has doubled since the last prune, and once
// calls that end nothing outnumber its objects. A view that is ended loses its pointer, and the null-proxy check
// above raises for it from then on. Nothing here runs Python code or releases the GIL, which guards it all: the
// calls' hooks and the registration run with it. A view never owns its group, and is made not to before its object
// is dropped, so dropping one runs no destructor.
%{
#include <unordered_map>
namespace QuickfixViews
{
static std::unordered_map<const void *, std::vector<PyObject *>> s_views;
static size_t s_entries = 0;
static size_t s_idle = 0;
static size_t s_pruneAt = 64;

static void drop(PyObject *object)
{
  reinterpret_cast<SwigPyObject *>(object)->own = 0;
  Py_DECREF(object);
  --s_entries;
}

static void prune()
{
  for (auto views = s_views.begin(); views != s_views.end();)
  {
    std::vector<PyObject *> &objects = views->second;
    for (size_t i = 0; i < objects.size();)
    {
      if (Py_REFCNT(objects[i]) == 1)
      {
        drop(objects[i]);
        objects[i] = objects.back();
        objects.pop_back();
      }
      else
      {
        ++i;
      }
    }
    views = objects.empty() ? s_views.erase(views) : std::next(views);
  }
  s_idle = 0;
}

// From a wrapper's ret typemap, outside the exception translation, so nothing may escape. False, with a Python
// exception set, if view could not be registered.
bool registerView(PyObject *view, const void *address)
{
  PyObject *object = reinterpret_cast<PyObject *>(SWIG_Python_GetSwigThis(view));
  if (!object)
  {
    PyErr_SetString(PyExc_RuntimeError, "a group view could not be registered");
    return false;
  }
  try
  {
    s_views[address].push_back(object);
  }
  catch (...)
  {
    PyErr_NoMemory();
    return false;
  }
  Py_INCREF(object);
  if (++s_entries >= s_pruneAt)
  {
    prune();
    s_pruneAt = std::max<size_t>(64, 2 * s_entries);
  }
  return true;
}

bool anyGroupViews() { return !s_views.empty(); }

void endViews(const Addresses &addresses)
{
  bool ended = false;
  for (const void *address : addresses)
  {
    auto views = s_views.find(address);
    if (views == s_views.end())
      continue;
    for (PyObject *object : views->second)
    {
      reinterpret_cast<SwigPyObject *>(object)->ptr = 0;
      drop(object);
    }
    s_views.erase(views);
    ended = true;
  }
  if (ended)
    s_idle = 0;
  else if (++s_idle >= s_entries)
    prune();
}
}
%}
// For the tests: fix._viewCount() is how many group views are registered and still held. With the GIL, like the
// rest of the registry: a prune can free objects.
%rename(_viewCount) quickfixTestViewCount;
%nothread quickfixTestViewCount;
%inline %{
size_t quickfixTestViewCount()
{
  QuickfixViews::prune();
  return QuickfixViews::s_entries;
}
%}

// The views of a transport's sessions and log handed out (#197; ../quickfix.i): each view's `this` object, by the
// address of what it views, held and pruned as the group views' are above, and whether that is a session. A map of
// their own, walked only when a transport is destroyed or its constructor throws. With the GIL throughout.
%{
namespace QuickfixTransportViews
{
struct Views
{
  std::vector<PyObject *> objects;
  bool session = false;
};
typedef std::unordered_map<const void *, Views> Map;
static Map s_views;
static size_t s_entries = 0;
static size_t s_pruneAt = 64;

static void drop(PyObject *object)
{
  reinterpret_cast<SwigPyObject *>(object)->own = 0;
  Py_DECREF(object);
  --s_entries;
}

static void prune()
{
  for (auto views = s_views.begin(); views != s_views.end();)
  {
    std::vector<PyObject *> &objects = views->second.objects;
    for (size_t i = 0; i < objects.size();)
    {
      if (Py_REFCNT(objects[i]) == 1)
      {
        drop(objects[i]);
        objects[i] = objects.back();
        objects.pop_back();
      }
      else
      {
        ++i;
      }
    }
    views = objects.empty() ? s_views.erase(views) : std::next(views);
  }
}

// From a wrapper's ret typemap, outside the exception translation, so nothing may escape. False, with a Python
// exception set, if view could not be registered. A proxy that owns its object is not a view.
bool registerView(PyObject *view, const void *address, bool session)
{
  SwigPyObject *proxy = SWIG_Python_GetSwigThis(view);
  if (!proxy)
  {
    PyErr_SetString(PyExc_RuntimeError, "a transport's view could not be registered");
    return false;
  }
  if (proxy->own)
    return true;
  try
  {
    Views &views = s_views[address];
    views.objects.push_back(reinterpret_cast<PyObject *>(proxy));
    views.session = views.session || session;
  }
  catch (...)
  {
    PyErr_NoMemory();
    return false;
  }
  Py_INCREF(reinterpret_cast<PyObject *>(proxy));
  if (++s_entries >= s_pruneAt)
  {
    prune();
    s_pruneAt = std::max<size_t>(64, 2 * s_entries);
  }
  return true;
}

// Ends the views in views, and erases them: the entry after.
static Map::iterator endAt(Map::iterator views)
{
  for (PyObject *object : views->second.objects)
  {
    reinterpret_cast<SwigPyObject *>(object)->ptr = 0;
    drop(object);
  }
  return s_views.erase(views);
}

static void endViews(const Addresses &addresses)
{
  for (const void *address : addresses)
  {
    auto views = s_views.find(address);
    if (views != s_views.end())
      endAt(views);
  }
}

// A transport's destructor: ends the views of what it frees before it runs, and again after, since a script's store
// or log factory runs inside it and can hand out a view of a session it has yet to free.
template <typename Transport> class Teardown
{
public:
  explicit Teardown(Transport &transport) : m_addresses(addressesOf(transport)) { endViews(m_addresses); }
  ~Teardown() { endViews(m_addresses); }
  Teardown(const Teardown &) = delete;
  Teardown &operator=(const Teardown &) = delete;

private:
  Addresses m_addresses;
};

// A transport's constructor: one that throws has freed the sessions it made, so as its exception leaves, the views
// of sessions that no longer exist are ended.
class Construction
{
public:
  Construction() : m_exceptions(std::uncaught_exceptions()) {}
  ~Construction()
  {
    if (std::uncaught_exceptions() <= m_exceptions || s_views.empty())
      return;
    try
    {
      Addresses live = liveSessions();
      for (auto views = s_views.begin(); views != s_views.end();)
        views = views->second.session && !std::binary_search(live.begin(), live.end(), views->first) ? endAt(views)
                                                                                                    : std::next(views);
    }
    catch (...)
    {
    }
  }
  Construction(const Construction &) = delete;
  Construction &operator=(const Construction &) = delete;

private:
  int m_exceptions;
};
}
%}
// For the tests: fix._transportViewCount() is how many views of transports are registered and still held.
%rename(_transportViewCount) quickfixTestTransportViewCount;
%nothread quickfixTestTransportViewCount;
%inline %{
size_t quickfixTestTransportViewCount()
{
  QuickfixTransportViews::prune();
  return QuickfixTransportViews::s_entries;
}
%}

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

# What a transport was made with is the script's own object, kept above. SWIG returned a second proxy of it that
# owned nothing, and kept nothing alive once the transport had gone (#197).
def _keptArgument(transport, index, name):
  args, kwargs = transport._quickfixArguments
  return args[index] if len(args) > index else kwargs[name]

for _cls in (Initiator, Acceptor):
  _cls.getApplication = lambda self: _keptArgument(self, 0, 'application')
  _cls.getMessageStoreFactory = lambda self: _keptArgument(self, 1, 'storeFactory')
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

