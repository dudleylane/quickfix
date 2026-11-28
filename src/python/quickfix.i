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

// Throws into the engine, as the C++ exception it holds, a FIX exception a
// Python callback raised. PyErr_Fetch gave the caller the type, the value and
// the traceback as new references, and the throw leaves the caller, so they
// are released here, by a destructor: after the throw has copied the C++
// exception out of the value's proxy, which owns it, and whether or not that
// copy succeeds.
template <class E>
[[noreturn]] void throwToEngine( void *exception, PyObject *ptype, PyObject *pvalue, PyObject *ptraceback )
{
  struct Fetched
  {
    PyObject *type, *value, *traceback;
    ~Fetched()
    {
      Py_XDECREF( type );
      Py_XDECREF( value );
      Py_XDECREF( traceback );
    }
  } fetched{ ptype, pvalue, ptraceback };
  throw *static_cast<E *>( exception );
}

// The engine keeps each Log a factory creates until it destroys it, but nothing kept the script's Log, and the GC
// freed it under the engine (#205). Keep it on the factory's own Python object, which #201 pins for the transport's
// life, in a _quickfixLogs dict keyed by the C++ Log the engine holds and counted per address (a factory may return
// one Log for several sessions). The C++ Log is used only as a key, so it is a void *. Under the GIL.
static void keepLog( PyObject *factory, void *log, PyObject *pyLog )
{
  if( !factory || !pyLog ) return;
  PyObject *logs = PyObject_GetAttrString( factory, "_quickfixLogs" );
  if( !logs )
  {
    PyErr_Clear();
    logs = PyDict_New();
    if( !logs ) { PyErr_Clear(); return; }
    PyObject_SetAttrString( factory, "_quickfixLogs", logs );
  }
  PyObject *key = PyLong_FromVoidPtr( log );
  if( key )
  {
    PyObject *entry = PyDict_GetItem( logs, key );
    if( entry )
    {
      PyObject *count = PyLong_FromLong( PyLong_AsLong( PyList_GET_ITEM( entry, 1 ) ) + 1 );
      if( count ) PyList_SetItem( entry, 1, count );
    }
    else
    {
      PyObject *pair = PyList_New( 2 ), *one = PyLong_FromLong( 1 );
      if( pair && one )
      {
        Py_INCREF( pyLog );
        PyList_SET_ITEM( pair, 0, pyLog );
        PyList_SET_ITEM( pair, 1, one );
        PyDict_SetItem( logs, key, pair );
      }
      else Py_XDECREF( one );
      Py_XDECREF( pair );
    }
    Py_DECREF( key );
  }
  PyErr_Clear();
  Py_DECREF( logs );
}

// Returns an owned reference to the Log create returned, if it is still kept, and drops the keep once destroy has
// been called for it as many times as create made it; otherwise NULL, for the caller to make a fresh proxy. Leaves
// no Python error set: destroy runs in the GC's teardown and at the interpreter's exit.
static PyObject *releaseLog( PyObject *factory, void *log )
{
  PyObject *result = NULL;
  if( factory )
  {
    PyObject *logs = PyObject_GetAttrString( factory, "_quickfixLogs" );
    if( logs )
    {
      PyObject *key = PyLong_FromVoidPtr( log );
      PyObject *entry = key ? PyDict_GetItem( logs, key ) : NULL;
      if( entry )
      {
        result = PyList_GET_ITEM( entry, 0 );
        Py_XINCREF( result );
        long count = PyLong_AsLong( PyList_GET_ITEM( entry, 1 ) ) - 1;
        if( count <= 0 ) PyDict_DelItem( logs, key );
        else { PyObject *c = PyLong_FromLong( count ); if( c ) PyList_SetItem( entry, 1, c ); }
      }
      Py_XDECREF( key );
      Py_DECREF( logs );
    }
  }
  PyErr_Clear();
  return result;
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

// #207: a public close() deletes a transport now, but it must not delete one while another thread is still inside
// its block() -- that would free the transport under block()'s own stack. block()'s with-GIL hook (the
// QUICKFIX_VIEWS_EXCEPTION mechanism above, run before $action takes the GIL away) registers the transport here for
// the duration of the call, keyed by its FIX::Acceptor/FIX::Initiator subobject -- the same pointer the destroy
// helper computes -- and the helper reports it still running, touching nothing, while it is registered. Both the
// hook's insert and the helper's check hold the GIL, so the GIL orders them: no thread can enter block() between the
// helper reading the registry and its clearing of the proxy. A leaked set, never destroyed, so no static destructor
// races a transport still blocking at exit.
%{
#include <set>
namespace QuickfixBlocking
{
inline std::set<const void *> &registry()
{
  static std::set<const void *> *blocking = new std::set<const void *>;
  return *blocking;
}
inline bool isBlocking(const void *transport) { return transport && registry().count(transport) != 0; }
class Guard
{
public:
  explicit Guard(const void *transport) : m_transport(transport)
  {
    if (m_transport)
      registry().insert(m_transport);
  }
  ~Guard()
  {
    if (m_transport)
      registry().erase(m_transport);
  }
  Guard(const Guard &) = delete;
  Guard &operator=(const Guard &) = delete;

private:
  const void *m_transport;
};
}
%}
QUICKFIX_VIEWS_EXCEPTION(FIX::Acceptor::block, QuickfixBlocking::Guard _qfBlockGuard(static_cast<FIX::Acceptor *>(arg1));)
QUICKFIX_VIEWS_EXCEPTION(FIX::Initiator::block, QuickfixBlocking::Guard _qfBlockGuard(static_cast<FIX::Initiator *>(arg1));)

// A script LogFactory's Log (#205): create keeps it on the factory (so the GC does not free one the engine still
// uses), and destroy is handed the object create returned, not a fresh proxy, so a factory can match it to what it
// kept. Only LogFactory::create returns a FIX::Log * from a director, and only LogFactory::destroy takes one in; the
// directorout is SWIG's own SWIGTYPE * directorout (typemaps/swigtype.swg) with the keep appended.
%typemap(directorout,noblock=1,warning=SWIGWARN_TYPEMAP_DIRECTOROUT_PTR_MSG)
  FIX::Log *(void *swig_argp, int swig_res, swig_owntype own) {
  swig_res = SWIG_ConvertPtrAndOwn($input, &swig_argp, $descriptor, %convertptr_flags | SWIG_POINTER_DISOWN, &own);
  if (!SWIG_IsOK(swig_res)) {
    %dirout_fail(swig_res,"$type");
  }
  $result = %reinterpret_cast(swig_argp, $ltype);
  swig_acquire_ownership_obj(%as_voidptr($result), own /* & TODO: SWIG_POINTER_OWN */);
  keepLog(swig_get_self(), %as_voidptr($result), $input);
}
%typemap(directorin) FIX::Log * {
  $input = releaseLog(swig_get_self(), %as_voidptr($1));
  if (!$input) {
    $input = SWIG_NewPointerObj(%as_voidptr($1), $descriptor, 0);
  }
}

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

// #207: close() refuses from inside any callback, on any thread. A callback that closes its own transport would
// delete it under its own stack -- a toApp reached through Session.sendToTarget runs on the script's own calling
// thread, which no thread-identity check catches, as the Ruby half proved under ASan. A per-thread depth counter,
// bumped by an RAII guard that src/python/swig.sh injects at the top of every SwigDirector upcall: the director
// dispatch is Python's single callback chokepoint, as withGvl is Ruby's. The guard's destructor runs on every path
// out of an upcall -- a normal return, the C++ throw throwToEngine makes for a FIX exception the engine expects
// (DoNotSend and the rest), and the stack unwinding -- so the count cannot stick and leave close() refusing for
// ever on that thread. exitOnCallbackError() _exit()s for any other exception, ending the process, so a decrement
// missed there is moot. The rule is stop()'s rule: no close() of any transport from inside any callback.
%{
namespace QuickfixInCallback
{
thread_local int t_depth = 0;
class Guard
{
public:
  Guard() { ++t_depth; }
  ~Guard() { --t_depth; }
  Guard(const Guard &) = delete;
  Guard &operator=(const Guard &) = delete;
};
}
%}
%nothread quickfixInCallback;
%rename(_inCallback) quickfixInCallback;
%inline %{
bool quickfixInCallback() { return QuickfixInCallback::t_depth > 0; }
%}

// close()'s delete reproduces the generated transport delete wrapper (_wrap_delete_SocketAcceptorBase and its kin):
// it runs the destructor under QuickfixTransportViews::Teardown -- which ends the #197 session and log views, so a
// view taken earlier raises ReferenceError rather than reaching freed memory -- and releases the GIL around the
// delete itself (QuickfixPins::destroy: the C++ delete, then the factory references let go). It differs in two ways.
// It refuses, reporting the transport still running and leaving it owned by the GC, while block() holds it (above).
// And it clears the proxy's pointer, not only its ownership (DISOWN clears only ownership), BEFORE the delete, so a
// method reaching the transport from another thread during the delete raises through the null-proxy typemap rather
// than racing it. The template is C++ only; the two wrapped functions pass the base's type. nothread: the proxy and
// views are read and written under the GIL, which the helper releases only around the delete.
%{
template <class Transport>
static int quickfixDestroyTransport(PyObject *self, swig_type_info *type)
{
  SwigPyObject *proxy = self ? SWIG_Python_GetSwigThis(self) : nullptr;
  if (!proxy || !proxy->ptr)
    return 0; // already closed, or never a transport proxy
  void *argp = nullptr;
  if (!SWIG_IsOK(SWIG_ConvertPtr(self, &argp, type, 0)) || !argp)
    return 0;
  Transport *transport = static_cast<Transport *>(argp);
  if (QuickfixBlocking::isBlocking(transport))
    return 1; // another thread is inside block() -- leave the proxy owning it, for the GC
  proxy->ptr = 0;
  proxy->own = 0;
  QuickfixTransportViews::Teardown<Transport> teardown(*transport);
  {
    SWIG_PYTHON_THREAD_BEGIN_ALLOW;
    QuickfixPins::destroy(transport);
    SWIG_PYTHON_THREAD_END_ALLOW;
  }
  return 0;
}
%}
%nothread _quickfixDestroyAcceptor;
%nothread _quickfixDestroyInitiator;
%nothread _quickfixBlocking;
%inline %{
int _quickfixDestroyAcceptor(PyObject *self)
{
  return quickfixDestroyTransport<FIX::Acceptor>(self, SWIGTYPE_p_FIX__Acceptor);
}
int _quickfixDestroyInitiator(PyObject *self)
{
  return quickfixDestroyTransport<FIX::Initiator>(self, SWIGTYPE_p_FIX__Initiator);
}
// Whether another thread is inside this transport's block() now, by the same key the destroy helper uses. A Python
// program may call block() on a thread of its own -- start() uses the engine's own threads instead, which stop()
// joins -- and this reports while it runs, so close()'s wait loop and a test can see the destroy helper refuse.
// Tries the Acceptor base, then the Initiator base, so one helper serves both.
bool _quickfixBlocking(PyObject *self)
{
  void *argp = nullptr;
  if (SWIG_IsOK(SWIG_ConvertPtr(self, &argp, SWIGTYPE_p_FIX__Acceptor, 0)) && argp)
    return QuickfixBlocking::isBlocking(argp);
  argp = nullptr;
  if (SWIG_IsOK(SWIG_ConvertPtr(self, &argp, SWIGTYPE_p_FIX__Initiator, 0)) && argp)
    return QuickfixBlocking::isBlocking(argp);
  return false;
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
      throwToEngine<FIX::DoNotSend>( result, ptype, pvalue, ptraceback );
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
      throwToEngine<FIX::FieldNotFound>( result, ptype, pvalue, ptraceback );
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__IncorrectDataFormat, 0 ) != -1 ) {
      throwToEngine<FIX::IncorrectDataFormat>( result, ptype, pvalue, ptraceback );
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__IncorrectTagValue, 0 ) != -1 ) {
      throwToEngine<FIX::IncorrectTagValue>( result, ptype, pvalue, ptraceback );
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__RejectLogon, 0 ) != -1 ) {
      throwToEngine<FIX::RejectLogon>( result, ptype, pvalue, ptraceback );
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
      throwToEngine<FIX::FieldNotFound>( result, ptype, pvalue, ptraceback );
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__IncorrectDataFormat, 0 ) != -1 ) {
      throwToEngine<FIX::IncorrectDataFormat>( result, ptype, pvalue, ptraceback );
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__IncorrectTagValue, 0 ) != -1 ) {
      throwToEngine<FIX::IncorrectTagValue>( result, ptype, pvalue, ptraceback );
    } else if( SWIG_ConvertPtr(pvalue, &result, SWIGTYPE_p_FIX__UnsupportedMessageType, 0 ) != -1 ) {
      throwToEngine<FIX::UnsupportedMessageType>( result, ptype, pvalue, ptraceback );
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
# keeps its arguments, as quickfix_ruby.rb does for Ruby (#107). Kept on the transport, they still
# went in the same collection as it, in any order, and its destructor used its store and log
# factories (#201): those are pinned from C++ too, until the transport's C++ object is deleted.
def _keepArguments(cls):
  construct = cls.__init__
  def __init__(self, *args, **kwargs):
    construct(self, *args, **kwargs)
    self._quickfixArguments = (args, kwargs)
    self._quickfixCloseLock = _threading.RLock()
    self._quickfixClosed = False
    pin = _quickfixPinAcceptor if isinstance(self, Acceptor) else _quickfixPinInitiator
    pin(self, args[1] if len(args) > 1 else kwargs.get('storeFactory'),
        args[3] if len(args) > 3 else kwargs.get('logFactory'))
  cls.__init__ = __init__

def _transportClasses(base):
  for cls in base.__subclasses__():
    yield cls
    yield from _transportClasses(cls)

for _cls in set(_transportClasses(Initiator)) | set(_transportClasses(Acceptor)):
  _keepArguments(_cls)
del _cls

# SynchronizedApplication holds a C++ reference to the application it wraps but kept nothing referring to the
# script's object, so once the script dropped its own reference the application was freed and the next callback
# through the wrapper used freed memory (#208). Keep it on the wrapper's own object, as the transports keep theirs.
def _keepApplication(cls):
  construct = cls.__init__
  def __init__(self, app, *args, **kwargs):
    construct(self, app, *args, **kwargs)
    self._quickfixApplication = app
  cls.__init__ = __init__
_keepApplication(SynchronizedApplication)

# What a transport was made with is the script's own object, kept above. SWIG returned a second proxy of it that
# owned nothing, and kept nothing alive once the transport had gone (#197).
def _keptArgument(transport, index, name):
  args, kwargs = transport._quickfixArguments
  return args[index] if len(args) > index else kwargs[name]

for _cls in (Initiator, Acceptor):
  _cls.getApplication = lambda self: _keptArgument(self, 0, 'application')
  _cls.getMessageStoreFactory = lambda self: _keptArgument(self, 1, 'storeFactory')
del _cls

# close() ends a transport deterministically (#207). A script whose factory or application refers to its transport
# pins it (#201), so it and its sessions outlive every Python reference, freed only when the garbage collector
# collects the proxy. close() stops the transport and deletes it now: its sessions are unregistered (a later
# transport for the same SessionID then constructs), its factories are let go, and a method on it afterward raises
# ReferenceError rather than reaching freed memory. It refuses from inside a callback, on any thread -- a callback
# that closed its own transport would delete it under its own stack -- as stop() does; the refusal is before the
# lock, so a callback's close() raises at once rather than deadlocking a teardown that holds the lock (#111). A
# second close() is a no-op. A transport another thread is still inside block() on is not deleted: close() stops it
# and waits, up to timeout seconds, for block() to return, and raises RuntimeError if it does not.
import threading as _threading
import time as _time

def _quickfixClose(self, timeout=15.0):
  if _inCallback():
    raise RuntimeError('cannot close a transport from inside a callback')
  with self._quickfixCloseLock:
    if self._quickfixClosed:
      return
    _destroy = _quickfixDestroyInitiator if isinstance(self, Initiator) else _quickfixDestroyAcceptor
    deadline = _time.monotonic() + timeout
    while True:
      try:
        self.stop()
      except ReferenceError:
        pass  # a reentrant call cleared this proxy during release; the destroy below then reports it already closed
      if _destroy(self) == 0:
        self._quickfixClosed = True
        return
      if _time.monotonic() >= deadline:
        raise RuntimeError('transport still inside block() after %g s; not closed' % timeout)
      _time.sleep(0.02)

Initiator.close = _quickfixClose
Acceptor.close = _quickfixClose
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

