%define QUICKFIX_RUBY_EXCEPTION
%exception
{
  if(tryRubyException([&]() mutable
  {
    $action
    return self;
  fail:
    return Qnil;
  }) == Qnil)
  {
    SWIG_fail;
  }
  redeliverKill();
}
%enddef

// Ruby has no counterpart to the -threads option the Python binding is built
// with, so the calls that can block on the engine or call back into Ruby release
// the GVL by hand: every method of Session, Initiator and Acceptor (../quickfix.i
// applies this around their headers), and the callbacks take it back through the
// adapters below. Without it the thread Initiator#start and Acceptor#start create
// held the GVL for as long as the transport ran, and no other Ruby thread ran
// again (#102).
%define QUICKFIX_RUBY_EXCEPTION_WITHOUT_GVL
%exception
{
  if(tryRubyException([&]() mutable
  {
    return withoutGvl([&]() mutable -> VALUE
    {
      $action
      return self;
    });
  }) == Qnil)
  {
    SWIG_fail;
  }
  redeliverKill();
}
%enddef

QUICKFIX_RUBY_EXCEPTION

// block() runs the transport until it is stopped, so Ruby gets a way to end it:
// Thread#kill, or the threads being terminated at exit, stops the transport.
%exception FIX::Initiator::block
{
  if(tryRubyException([&]() mutable
  {
    return blockWithoutGvl([&]() mutable -> VALUE
    {
      $action
      return self;
    }, *arg1);
  }) == Qnil)
  {
    SWIG_fail;
  }
  redeliverKill();
}

%exception FIX::Acceptor::block
{
  if(tryRubyException([&]() mutable
  {
    return blockWithoutGvl([&]() mutable -> VALUE
    {
      $action
      return self;
    }, *arg1);
  }) == Qnil)
  {
    SWIG_fail;
  }
  redeliverKill();
}

// An Application, LogFactory or Log implemented in Ruby is handed to the engine
// behind an adapter that takes the GVL before calling it.
%typemap(in) FIX::Application & (void *argp = 0, int res = 0) {
  res = SWIG_ConvertPtr($input, &argp, $descriptor(FIX::Application *), 0);
  if (!SWIG_IsOK(res)) {
    SWIG_exception_fail(SWIG_ArgError(res), Ruby_Format_TypeError("", "FIX::Application &", "$symname", $argnum, $input));
  }
  if (!argp) {
    SWIG_exception_fail(SWIG_ValueError, Ruby_Format_TypeError("invalid null reference ", "FIX::Application &", "$symname", $argnum, $input));
  }
  $1 = &RubyApplication::adapt(*reinterpret_cast<FIX::Application *>(argp));
}

%typemap(out) FIX::Application & {
  $result = SWIG_NewPointerObj(SWIG_as_voidptr(&RubyApplication::unadapt(*$1)), $descriptor(FIX::Application *), 0);
  if (Swig::Director *director = dynamic_cast<Swig::Director *>(&RubyApplication::unadapt(*$1))) {
    $result = director->swig_get_self();
  }
}

%typemap(in) FIX::LogFactory & (void *argp = 0, int res = 0) {
  res = SWIG_ConvertPtr($input, &argp, $descriptor(FIX::LogFactory *), 0);
  if (!SWIG_IsOK(res)) {
    SWIG_exception_fail(SWIG_ArgError(res), Ruby_Format_TypeError("", "FIX::LogFactory &", "$symname", $argnum, $input));
  }
  if (!argp) {
    SWIG_exception_fail(SWIG_ValueError, Ruby_Format_TypeError("invalid null reference ", "FIX::LogFactory &", "$symname", $argnum, $input));
  }
  $1 = &RubyLogFactory::adapt(*reinterpret_cast<FIX::LogFactory *>(argp));
}

%{
#include <Acceptor.h>
#include <Application.h>
#include <Initiator.h>
#include <Log.h>
#include <ruby/thread.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <cstdio>
#include <unistd.h>

// Exported by libruby, but declared only in its internal headers.
extern "C" int ruby_thread_has_gvl_p(void);

namespace
{
struct WithoutGvl
{
  std::function<VALUE()> *call;
  std::function<void()> *unblock;
  VALUE result = Qnil;
  std::exception_ptr error;
  bool ran = false;
  std::atomic<bool> unblocked{false};
};

// The call this thread made without the GVL, if any.
thread_local WithoutGvl *t_withoutGvl = nullptr;

void *runWithoutGvl(void *p)
{
  WithoutGvl *w = static_cast<WithoutGvl *>(p);
  WithoutGvl *outer = t_withoutGvl;
  t_withoutGvl = w;
  w->ran = true;
  try
  {
    w->result = (*w->call)();
  }
  catch (...)
  {
    w->error = std::current_exception();
  }
  t_withoutGvl = outer;
  return nullptr;
}

// Ruby calls this from another thread, holding its own locks, so it must not
// block.
void unblockWithoutGvl(void *p)
{
  WithoutGvl *w = static_cast<WithoutGvl *>(p);
  w->unblocked = true;
  (*w->unblock)();
}

}

// Makes call with the GVL released. rb_thread_call_without_gvl2 rather than
// rb_thread_call_without_gvl: it never raises, so no Ruby exception unwinds
// through the C++ frames here. It skips the call if an interrupt is already
// pending; a call that cannot be unblocked then runs with the GVL held, and
// block() returns at once, to be interrupted.
VALUE withoutGvl(std::function<VALUE()> call, std::function<void()> unblock = {})
{
  WithoutGvl w{&call, &unblock};
  rb_thread_call_without_gvl2(runWithoutGvl, &w, unblock ? unblockWithoutGvl : nullptr, &w);
  if (!w.ran)
  {
    return unblock ? Qnil : call();
  }
  if (w.error)
  {
    std::rethrow_exception(w.error);
  }
  return w.result;
}

// Thread#kill (and Thread#exit) reaches a callback's rb_protect not as an
// exception but as Ruby's fatal tag, and director:except used to take it for an
// error and end the process with status 1 (#147). The kill may not unwind the
// engine's C++ frames either, so the callback returns as if it had finished,
// with the kill still in $!: the transport this thread runs is stopped, as
// Ruby's unblock function would have stopped it, no further callback runs on the
// thread, and the kill goes on once the thread is out of the engine -- where the
// wrapper of the method Ruby called would raise (QUICKFIX_RUBY_EXCEPTION), or
// after the dispatcher's callback. Ruby marks a thread when a kill first lands
// and ignores a second Thread#kill, and unwinding a killed thread needs $! to
// hold the tag, which rb_set_errinfo refuses to set; so the kill is never
// cleared, and goes on with rb_jump_tag.
namespace
{
// vm_core.h's TAG_FATAL, the tag Ruby ends a killed thread with.
const int killTag = 8;
thread_local bool t_deferredKill = false;
}

bool deferKill(VALUE error)
{
  if (!FIXNUM_P(error) || FIX2INT(error) != killTag)
  {
    return false;
  }
  t_deferredKill = true;
  if (t_withoutGvl && !t_withoutGvl->unblocked && *t_withoutGvl->unblock)
  {
    t_withoutGvl->unblocked = true;
    (*t_withoutGvl->unblock)();
  }
  return true;
}

// With the GVL, where a longjmp leaves no C++ frame that needs unwinding.
void redeliverKill()
{
  if (!t_deferredKill)
  {
    return;
  }
  t_deferredKill = false;
  if (rb_errinfo() != INT2FIX(killTag))
  {
    rb_raise(rb_eThreadError, "quickfix: a Thread#kill that landed in a callback was lost before it could be delivered");
  }
  rb_jump_tag(killTag);
}

// block() without the GVL. When Ruby asks the thread to stop, a thread of its
// own calls stop(true) -- the unblock function may not block, and stop() takes
// engine locks -- and block() returns once the transport has stopped. That
// thread never enters Ruby, and is joined before this returns.
// The transports whose block() is running, which may not be deleted yet.
std::mutex g_blockingMutex;
std::set<const void *> g_blocking;

bool isBlocking(const void *transport)
{
  std::lock_guard<std::mutex> lock(g_blockingMutex);
  return g_blocking.count(transport) != 0;
}

template <typename Transport>
VALUE blockWithoutGvl(std::function<VALUE()> call, Transport &transport)
{
  {
    std::lock_guard<std::mutex> lock(g_blockingMutex);
    g_blocking.insert(&transport);
  }
  struct Unregister
  {
    const void *transport;
    ~Unregister()
    {
      std::lock_guard<std::mutex> lock(g_blockingMutex);
      g_blocking.erase(transport);
    }
  } unregister{&transport};
  std::thread stopper;
  struct Join
  {
    std::thread &thread;
    ~Join()
    {
      if (thread.joinable())
      {
        thread.join();
      }
    }
  } join{stopper};
  return withoutGvl(std::move(call),
                    [&]()
                    {
                      if (!stopper.joinable())
                      {
                        stopper = std::thread([&transport]() { transport.stop(true); });
                      }
                    });
}

// At exit Ruby frees every object in no particular order, so an initiator or
// acceptor freed after its store factory, log factory or application used them
// from its destructor (#105). quickfix_ruby.rb's at_exit hook, which runs while
// they are all alive, deletes each one through these instead: the Ruby object
// gives up the pointer, so its finalizer does nothing, and any later call on it
// raises ObjectPreviouslyDeleted. One whose block() is still running is not
// touched.
template <typename Transport>
VALUE destroyTransport(VALUE object, swig_type_info *type)
{
  void *pointer = 0;
  if (!SWIG_IsOK(SWIG_ConvertPtr(object, &pointer, type, 0)) || !pointer || isBlocking(pointer))
  {
    return Qfalse;
  }
  if (!SWIG_IsOK(SWIG_ConvertPtr(object, &pointer, type, SWIG_POINTER_RELEASE)) || !pointer)
  {
    return Qfalse;
  }
  delete static_cast<Transport *>(pointer);
  return Qtrue;
}

VALUE destroyInitiator(VALUE, VALUE object) { return destroyTransport<FIX::Initiator>(object, SWIGTYPE_p_FIX__Initiator); }
VALUE destroyAcceptor(VALUE, VALUE object) { return destroyTransport<FIX::Acceptor>(object, SWIGTYPE_p_FIX__Acceptor); }

VALUE isInitiatorBlocking(VALUE, VALUE object)
{
  void *pointer = 0;
  return SWIG_IsOK(SWIG_ConvertPtr(object, &pointer, SWIGTYPE_p_FIX__Initiator, 0)) && pointer && isBlocking(pointer)
             ? Qtrue
             : Qfalse;
}
VALUE isAcceptorBlocking(VALUE, VALUE object)
{
  void *pointer = 0;
  return SWIG_IsOK(SWIG_ConvertPtr(object, &pointer, SWIGTYPE_p_FIX__Acceptor, 0)) && pointer && isBlocking(pointer)
             ? Qtrue
             : Qfalse;
}

// A thread Ruby did not create -- a connection thread of a threaded transport --
// cannot enter Ruby at all, so its callbacks run on a Ruby thread made for them,
// the dispatcher, while it waits (#104). The dispatcher starts with the first
// adapter and waits for callbacks without the GVL; once it has ended, which
// happens when Ruby ends its threads at exit, callbacks from such threads are
// skipped, as a stopped transport thread's are.
namespace
{
struct Dispatch
{
  const std::function<void()> *callback;
  std::exception_ptr error;
  bool done = false;
};

std::mutex g_dispatchMutex;
std::condition_variable g_dispatchQueued;
std::condition_variable g_dispatchDone;
std::deque<Dispatch *> g_dispatchQueue;
Dispatch *g_dispatching = nullptr;
bool g_dispatcherRunning = false;
bool g_dispatcherWoken = false;

// Without the GVL: takes the next callback, or returns none when Ruby wakes the
// dispatcher to deliver an interrupt.
void *waitForDispatch(void *)
{
  std::unique_lock<std::mutex> lock(g_dispatchMutex);
  g_dispatchQueued.wait(lock, []() { return !g_dispatchQueue.empty() || g_dispatcherWoken; });
  g_dispatcherWoken = false;
  if (g_dispatchQueue.empty())
  {
    return nullptr;
  }
  g_dispatching = g_dispatchQueue.front();
  g_dispatchQueue.pop_front();
  return g_dispatching;
}

void wakeDispatcher(void *)
{
  std::lock_guard<std::mutex> lock(g_dispatchMutex);
  g_dispatcherWoken = true;
  g_dispatchQueued.notify_all();
}

void finishDispatch(Dispatch *dispatch)
{
  std::lock_guard<std::mutex> lock(g_dispatchMutex);
  dispatch->done = true;
  g_dispatching = nullptr;
  g_dispatchDone.notify_all();
}

// No C++ object with a destructor lives in this frame: rb_thread_check_ints
// leaves it by longjmp when the thread is killed, and dispatcherEnded then
// releases whatever was waiting.
VALUE dispatcherLoop(VALUE)
{
  for (;;)
  {
    Dispatch *dispatch = static_cast<Dispatch *>(rb_thread_call_without_gvl2(waitForDispatch, 0, wakeDispatcher, 0));
    rb_thread_check_ints();
    if (dispatch)
    {
      try
      {
        (*dispatch->callback)();
      }
      catch (...)
      {
        dispatch->error = std::current_exception();
      }
      finishDispatch(dispatch);
      redeliverKill();
    }
  }
  return Qnil;
}

VALUE dispatcherEnded(VALUE)
{
  std::lock_guard<std::mutex> lock(g_dispatchMutex);
  g_dispatcherRunning = false;
  if (g_dispatching)
  {
    g_dispatching->done = true;
    g_dispatching = nullptr;
  }
  for (Dispatch *dispatch : g_dispatchQueue)
  {
    dispatch->done = true;
  }
  g_dispatchQueue.clear();
  g_dispatchDone.notify_all();
  return Qnil;
}

VALUE dispatcherMain(void *) { return rb_ensure(dispatcherLoop, Qnil, dispatcherEnded, Qnil); }

VALUE g_dispatcherThread = Qnil;

// Called with the GVL, which serialises it.
void startDispatcher()
{
  if (g_dispatcherThread != Qnil)
  {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_dispatchMutex);
    g_dispatcherRunning = true;
  }
  g_dispatcherThread = rb_thread_create(dispatcherMain, 0);
  rb_gc_register_address(&g_dispatcherThread);
  rb_funcall(g_dispatcherThread, rb_intern("name="), 1, rb_str_new_cstr("quickfix callbacks"));
}

// quickfix_ruby.rb refuses to stop a threaded transport on this thread (#111).
VALUE callbackThread(VALUE) { return g_dispatcherThread; }

void dispatch(const std::function<void()> &callback)
{
  Dispatch dispatch{&callback};
  {
    std::unique_lock<std::mutex> lock(g_dispatchMutex);
    if (!g_dispatcherRunning)
    {
      return;
    }
    g_dispatchQueue.push_back(&dispatch);
    g_dispatchQueued.notify_all();
    g_dispatchDone.wait(lock, [&]() { return dispatch.done; });
  }
  if (dispatch.error)
  {
    std::rethrow_exception(dispatch.error);
  }
}
}

// An exception that escapes a Ruby callback, other than the FIX exceptions the
// engine expects, ends the process with status 1, as upstream's binding meant to.
// It called exit(1), which runs the process's static destructors -- the adapter
// tables below among them -- while the engine's threads still run, and they then
// called through freed adapters; Log and LogFactory let the exception unwind the
// engine instead (#112). This prints it as Ruby prints an uncaught exception,
// flushes Ruby's own buffers, and leaves with _exit, which runs no destructors.
// Called from director:except, with the GVL held.
[[noreturn]] void exitOnCallbackError(VALUE error)
{
  int state = 0;
  VALUE text = rb_protect([](VALUE e) { return rb_funcall(e, rb_intern("full_message"), 0); }, error, &state);
  if (state || !RB_TYPE_P(text, T_STRING))
  {
    rb_set_errinfo(Qnil);
    text = rb_protect([](VALUE e) { return rb_obj_as_string(e); }, error, &state);
  }
  if (!state && RB_TYPE_P(text, T_STRING))
  {
    fwrite(RSTRING_PTR(text), 1, RSTRING_LEN(text), stderr);
    if (RSTRING_LEN(text) == 0 || RSTRING_PTR(text)[RSTRING_LEN(text) - 1] != '\n')
    {
      fputc('\n', stderr);
    }
  }
  rb_protect([](VALUE) { rb_io_flush(rb_stdout); return Qnil; }, Qnil, &state);
  rb_protect([](VALUE) { rb_io_flush(rb_stderr); return Qnil; }, Qnil, &state);
  fflush(nullptr);
  _exit(1);
}

// Runs a callback into Ruby. A thread that holds the GVL calls straight through;
// one that released it in withoutGvl takes it back for the call, and skips the
// call once Ruby has asked that thread to stop, since the Ruby code would raise
// into director:except, which exits the process. A thread Ruby did not create
// hands the call to the dispatcher.
void withGvl(const std::function<void()> &callback)
{
  if (t_deferredKill)
  {
    return;
  }
  if (ruby_thread_has_gvl_p())
  {
    callback();
    return;
  }
  if (!ruby_native_thread_p())
  {
    dispatch(callback);
    return;
  }
  if (t_withoutGvl && t_withoutGvl->unblocked)
  {
    return;
  }

  struct Call
  {
    const std::function<void()> &callback;
    std::exception_ptr error;
  } call{callback};
  rb_thread_call_with_gvl(
      [](void *p) -> void *
      {
        Call *c = static_cast<Call *>(p);
        try
        {
          c->callback();
        }
        catch (...)
        {
          c->error = std::current_exception();
        }
        return nullptr;
      },
      &call);
  if (call.error)
  {
    std::rethrow_exception(call.error);
  }
}

// One adapter per Ruby object, kept for the life of the process: an acceptor or
// initiator holds a reference to it, and Ruby gives no hook for when that ends.
// Created while the caller holds the GVL, which serialises the maps.
class RubyApplication : public FIX::Application
{
public:
  static FIX::Application &adapt(FIX::Application &application)
  {
    if (!dynamic_cast<Swig::Director *>(&application))
    {
      return application;
    }
    startDispatcher();
    static std::map<FIX::Application *, std::unique_ptr<RubyApplication>> adapters;
    std::unique_ptr<RubyApplication> &adapter = adapters[&application];
    if (!adapter)
    {
      adapter.reset(new RubyApplication(application));
    }
    return *adapter;
  }

  static FIX::Application &unadapt(FIX::Application &application)
  {
    RubyApplication *adapter = dynamic_cast<RubyApplication *>(&application);
    return adapter ? adapter->m_application : application;
  }

  void onCreate(const FIX::SessionID &sessionID) override
  {
    withGvl([&]() { m_application.onCreate(sessionID); });
  }
  void onLogon(const FIX::SessionID &sessionID) override
  {
    withGvl([&]() { m_application.onLogon(sessionID); });
  }
  void onLogout(const FIX::SessionID &sessionID) override
  {
    withGvl([&]() { m_application.onLogout(sessionID); });
  }
  void toAdmin(FIX::Message &message, const FIX::SessionID &sessionID) override
  {
    withGvl([&]() { m_application.toAdmin(message, sessionID); });
  }
  void toApp(FIX::Message &message, const FIX::SessionID &sessionID) EXCEPT(FIX::DoNotSend) override
  {
    withGvl([&]() { m_application.toApp(message, sessionID); });
  }
  void fromAdmin(const FIX::Message &message, const FIX::SessionID &sessionID)
      EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) override
  {
    withGvl([&]() { m_application.fromAdmin(message, sessionID); });
  }
  void fromApp(const FIX::Message &message, const FIX::SessionID &sessionID)
      EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) override
  {
    withGvl([&]() { m_application.fromApp(message, sessionID); });
  }

private:
  explicit RubyApplication(FIX::Application &application) : m_application(application) {}

  FIX::Application &m_application;
};

class RubyLog : public FIX::Log
{
public:
  explicit RubyLog(FIX::Log &log) : m_log(log) {}

  FIX::Log &log() { return m_log; }

  void clear() override
  {
    withGvl([&]() { m_log.clear(); });
  }
  void backup() override
  {
    withGvl([&]() { m_log.backup(); });
  }
  void onIncoming(const std::string &value) override
  {
    withGvl([&]() { m_log.onIncoming(value); });
  }
  void onOutgoing(const std::string &value) override
  {
    withGvl([&]() { m_log.onOutgoing(value); });
  }
  void onEvent(const std::string &value) override
  {
    withGvl([&]() { m_log.onEvent(value); });
  }

private:
  FIX::Log &m_log;
};

// The engine creates and destroys logs through the factory, so the factory
// adapter wraps each log it creates and unwraps it on the way back.
class RubyLogFactory : public FIX::LogFactory
{
public:
  static FIX::LogFactory &adapt(FIX::LogFactory &factory)
  {
    if (!dynamic_cast<Swig::Director *>(&factory))
    {
      return factory;
    }
    startDispatcher();
    static std::map<FIX::LogFactory *, std::unique_ptr<RubyLogFactory>> adapters;
    std::unique_ptr<RubyLogFactory> &adapter = adapters[&factory];
    if (!adapter)
    {
      adapter.reset(new RubyLogFactory(factory));
    }
    return *adapter;
  }

  FIX::Log *create() override
  {
    FIX::Log *log = nullptr;
    withGvl([&]() { log = m_factory.create(); });
    return wrap(log);
  }
  FIX::Log *create(const FIX::SessionID &sessionID) override
  {
    FIX::Log *log = nullptr;
    withGvl([&]() { log = m_factory.create(sessionID); });
    return wrap(log);
  }
  void destroy(FIX::Log *log) override
  {
    RubyLog *adapter = dynamic_cast<RubyLog *>(log);
    FIX::Log *inner = adapter ? &adapter->log() : log;
    withGvl([&]() { m_factory.destroy(inner); });
    delete adapter;
  }

private:
  explicit RubyLogFactory(FIX::LogFactory &factory) : m_factory(factory) {}

  static FIX::Log *wrap(FIX::Log *log)
  {
    return log && dynamic_cast<Swig::Director *>(log) ? new RubyLog(*log) : log;
  }

  FIX::LogFactory &m_factory;
};
%}

%rename(_getFieldName) FIX::DataDictionary::getFieldName;
%rename(_getValueName) FIX::DataDictionary::getValueName;
%rename(_getFieldTag) FIX::DataDictionary::getFieldTag;
%rename(_getGroup) FIX::DataDictionary::getGroup;

%typemap(in) std::string& (std::string temp) {
  temp = std::string((char*)StringValuePtr($input));
  $1 = &temp;
} 	 
	  	 
%typemap(argout) std::string& { 	 
  if( std::string("$1_type") == "std::string &" ) 	 
  { 	 
    rb_str_resize( $input, 0 );
    rb_str_append( $input, rb_str_new2($1->c_str()) );
  }
} 	 
	  	 
%typemap(in) int& (int temp) {
  temp = NUM2INT($input);
  $1 = &temp;
} 	 
	  	 
%typemap(argout) int& {
  if( std::string("$1_type") == "int &" )
  {
    vresult = result ? SWIG_From_int(static_cast< int >(*$1)) : Qnil;
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

// A view holds its owner in an instance variable, which the GC marks, and a call whose result is its argument
// returns the argument's own object, so neither outlives what it points into (#194; ../quickfix.i). A wrapper's
// self is the receiver and argv the arguments after it. Message's getGroup returns a Group SWIG has not read yet.
%typemap(out, noblock=1) FIX::Header &getHeader, const FIX::Header &getHeader, FIX::Trailer &getTrailer,
                         const FIX::Trailer &getTrailer, const FIX::Dictionary &get {
  $result = SWIG_NewPointerObj(SWIG_as_voidptr($1), $descriptor, 0);
  rb_iv_set($result, "@quickfixOwner", self);
}
// A group's view is also registered, so the calls that destroy its group can end it (#196, QuickfixViews below).
// This runs after the method's own redeliverKill, so it raises what lands while it registers the view.
%typemap(out, noblock=1) FIX::FieldMap &getGroupRef, FIX::FieldMap *getGroupPtr {
  $result = SWIG_NewPointerObj(SWIG_as_voidptr($1), $descriptor, 0);
  rb_iv_set($result, "@quickfixOwner", self);
  if ($1)
  {
    bool registered = QuickfixViews::registerView($result, $1);
    redeliverKill();
    QuickfixViews::redeliverError();
    if (!registered)
      rb_memerror();
  }
}
%{
// Ends a view: a proxy that owns nothing, of an object that is destroyed or about to be. Its pointer becomes null,
// and SWIG raises ObjectPreviouslyDeleted for it wherever it is passed afterwards, as self or as an argument of any
// kind. An owner is never ended: its pointer is what frees its object. SWIG gives a proxy that owns its object a
// free function, and every proxy it makes the @__swigtype__ variable.
bool quickfixEndView(VALUE view)
{
  if (TYPE(view) != T_DATA || RTYPEDDATA_P(view) || !RTEST(rb_ivar_defined(view, rb_intern("@__swigtype__"))) ||
      RDATA(view)->dfree)
    return false;
  DATA_PTR(view) = 0;
  return true;
}

// Quickfix._endView(view) ends view, and says whether it could: for the tests, and for quickfix_ruby.rb's
// TransportViews (#197).
VALUE quickfixTestEndView(VALUE, VALUE view) { return quickfixEndView(view) ? Qtrue : Qfalse; }
%}
// A method with overloads first asks each overload's typecheck whether it takes the arguments, and SWIG's took a
// proxy with a null pointer for an argument of no type, so the call raised ArgumentError ("Wrong arguments for
// overloaded method") where a method without overloads raises ObjectPreviouslyDeleted. The proxy is still of its
// class: these count it as one, and the overload chosen raises ObjectPreviouslyDeleted from its own conversion.
// SWIG's typemaps (typemaps/swigtype.swg), with that one test added; rvalue references take no proxy here.
%typemap(typecheck, precedence=SWIG_TYPECHECK_POINTER, noblock=1) SWIGTYPE * {
  void *vptr = 0;
  int res = SWIG_ConvertPtr($input, &vptr, $descriptor, 0);
  $1 = SWIG_CheckState(res) || res == SWIG_ObjectPreviouslyDeletedError;
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_POINTER, noblock=1) SWIGTYPE *const& {
  void *vptr = 0;
  int res = SWIG_ConvertPtr($input, &vptr, $*descriptor, 0);
  $1 = SWIG_CheckState(res) || res == SWIG_ObjectPreviouslyDeletedError;
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_POINTER, noblock=1) SWIGTYPE &, const SWIGTYPE & {
  void *vptr = 0;
  int res = SWIG_ConvertPtr($input, &vptr, $descriptor, SWIG_POINTER_NO_NULL);
  $1 = SWIG_CheckState(res) || res == SWIG_ObjectPreviouslyDeletedError;
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_POINTER, noblock=1) SWIGTYPE {
  void *vptr = 0;
  int res = SWIG_ConvertPtr($input, &vptr, $&descriptor, SWIG_POINTER_NO_NULL);
  $1 = SWIG_CheckState(res) || res == SWIG_ObjectPreviouslyDeletedError;
}

%typemap(out, noblock=1) FIX::FieldBase &getField { $result = argv[0]; }
%typemap(out, noblock=1) FIX::FieldMap &getGroup { $result = argv[2]; }
%typemap(out, noblock=1) FIX::Group &getGroup, Group &getGroup { $result = argv[1]; }

// A call that destroys a group a view may point at (#196; ../quickfix.i): QUICKFIX_RUBY_EXCEPTION, with BEFORE ahead
// of $action, which runs with the GVL. Session's send and sendToTarget release the GVL around $action alone, as
// QUICKFIX_RUBY_EXCEPTION_WITHOUT_GVL does, so BEFORE, and the destructor of anything it declares, run with it, and
// before tryRubyException translates a C++ exception.
%define QUICKFIX_VIEWS_EXCEPTION(decl, BEFORE)
%exception decl
{
  if(tryRubyException([&]() mutable
  {
    BEFORE
    $action
    return self;
  fail:
    return Qnil;
  }) == Qnil)
  {
    SWIG_fail;
  }
  redeliverKill();
  QuickfixViews::redeliverError();
}
%enddef
%define QUICKFIX_VIEWS_SESSION_EXCEPTION(decl, BEFORE)
%exception decl
{
  if(tryRubyException([&]() mutable
  {
    BEFORE
    return withoutGvl([&]() mutable -> VALUE
    {
      $action
      return self;
    });
  }) == Qnil)
  {
    SWIG_fail;
  }
  redeliverKill();
  QuickfixViews::redeliverError();
}
%enddef

%include ../quickfix.i

%{
#include <unordered_map>
#include <unordered_set>
namespace QuickfixViews
{
// The group views handed out (#196; ../quickfix.i). An ObjectSpace::WeakMap holds each view, mapped to the address
// of its group, so a view still goes when Ruby collects it: Ruby's C API has no weak reference of its own. The
// counts, by address, serve the fast check and say when the map needs walking: they never miss a view that has not
// been ended, and may count views collected since the last walk recounted them. A walk ends the views of the
// groups a call destroys by nulling their pointers (quickfixEndView), and SWIG raises ObjectPreviouslyDeleted for
// them from then on.
//
// An ended view stays in the map until Ruby collects it, so a loop that ends a view on every pass would walk every
// view it had made since the last GC. Instead, once the map holds more than twice the views not ended, and 16 more,
// a walk keeps the views it finds not ended, and a new map of those replaces it (WeakMap#delete is Ruby 3.3's). So a
// walk costs O(views not ended), amortized.
//
// Each call into Ruby here may switch threads, or run a trap handler or a finalizer that registers views itself,
// and may deliver a Thread#raise, a Thread#kill or a throw. Each lands after the call has done its work: each_pair's
// block calls nothing that checks for one. So nothing here holds an iterator or a count across such a call, a walk
// is repeated when a view was registered while it ran, a map is replaced only if none was, and what lands is held
// for the method's wrapper to deliver once it is out of C++ frames: a kill by deferKill, anything else by protect
// (redeliverError). While one is held, nothing here calls into Ruby again, where a second could land over it.
static VALUE s_map = Qnil;
static std::unordered_map<const void *, size_t> s_counts;
static size_t s_total = 0;
// The map's entries as the last walk found them, and the views registered since: more than it holds once Ruby has
// collected some.
static size_t s_entries = 0;
static size_t s_registrations = 0;
static size_t s_idle = 0;
static size_t s_lastGc = 0;
static size_t s_walkAt = 64;
// How many times something landed in protect: for the tests, which show they reach it.
static size_t s_landed = 0;
// What protect caught, for redeliverError: its tag, and $! as protect found it, compared, not kept, since Ruby
// keeps it there.
thread_local int t_heldTag = 0;
thread_local VALUE t_heldError = Qnil;

static bool holding() { return t_heldTag || t_deferredKill; }

// Calls into Ruby, unless something is held already, so that nothing it raises or throws unwinds through C++
// frames. A kill is held by deferKill; anything else by its tag, to go on with rb_jump_tag as it would have gone on.
// False if it did not call, or something landed. call creates no C++ object that needs destroying.
template <typename Call> static bool protect(Call &call)
{
  if (holding())
    return false;
  int state = 0;
  rb_protect(
      [](VALUE call) -> VALUE
      {
        (*reinterpret_cast<Call *>(call))();
        return Qnil;
      },
      reinterpret_cast<VALUE>(&call), &state);
  if (!state)
    return true;
  ++s_landed;
  if (!deferKill(rb_errinfo()))
  {
    t_heldTag = state;
    t_heldError = rb_errinfo();
  }
  return false;
}

// With the GVL, where a longjmp leaves no C++ frame that needs unwinding: after redeliverKill, which outranks it.
// What protect held goes on as it would have; if $! no longer holds it, it was lost, and that is raised instead.
void redeliverError()
{
  if (!t_heldTag)
    return;
  int tag = t_heldTag;
  VALUE error = t_heldError;
  t_heldTag = 0;
  t_heldError = Qnil;
  if (rb_errinfo() != error)
    rb_raise(rb_eThreadError, "quickfix: what landed while views were registered or ended was lost before it could "
                              "be delivered");
  rb_jump_tag(tag);
}

static const void *addressOf(VALUE value) { return reinterpret_cast<const void *>(NUM2ULL(value)); }

struct Walk
{
  const std::unordered_set<const void *> *doomed;
  // The views not ended, each followed by its address, when the map is to be replaced: an array made with room for
  // as many as the counts allow, so that pushing never allocates; Qnil if not, or if they did not fit.
  VALUE kept;
  long room;
  std::unordered_map<const void *, size_t> counts;
  size_t total = 0;
  size_t entries = 0;
  bool failed = false;
  bool refused = false;
};

// Calls nothing that can raise, allocate or let other Ruby code run, so a walk is never cut short once it has begun.
static VALUE walkView(RB_BLOCK_CALL_FUNC_ARGLIST(view, data))
{
  Walk *walk = reinterpret_cast<Walk *>(data);
  ++walk->entries;
  if (!DATA_PTR(argv[0]))
    return Qnil;
  const void *address = addressOf(argv[1]);
  if (walk->doomed && walk->doomed->count(address))
  {
    if (quickfixEndView(argv[0]))
      return Qnil;
    walk->refused = true;
  }
  try
  {
    ++walk->counts[address];
    ++walk->total;
  }
  catch (...)
  {
    walk->failed = true;
  }
  if (!NIL_P(walk->kept))
  {
    if (walk->room >= 2)
    {
      rb_ary_push(walk->kept, argv[0]);
      rb_ary_push(walk->kept, argv[1]);
      walk->room -= 2;
    }
    else
    {
      walk->kept = Qnil;
    }
  }
  return Qnil;
}

// A new map of the views kept replaces the old one, if every one went in and no view was registered meanwhile: one
// registered meanwhile is in the old map alone.
static void replace(VALUE kept)
{
  size_t registrations = s_registrations;
  VALUE map = Qnil;
  auto make = [&]() { map = rb_class_new_instance(0, nullptr, rb_path2class("ObjectSpace::WeakMap")); };
  if (!protect(make))
    return;
  for (long i = 0; i + 1 < RARRAY_LEN(kept); i += 2)
  {
    VALUE view = RARRAY_AREF(kept, i);
    VALUE address = RARRAY_AREF(kept, i + 1);
    auto store = [&]() { rb_funcall(map, rb_intern("[]="), 2, view, address); };
    if (!protect(store))
      return;
  }
  if (registrations != s_registrations)
    return;
  s_map = map;
  s_entries = static_cast<size_t>(RARRAY_LEN(kept) / 2);
  RB_GC_GUARD(map);
}

// Walks the map: ends the views at doomed, if given, recounts the rest, and replaces the map if ended views fill
// it. Repeated when a view was registered while it ran, which the recount may miss; ending a view twice is
// harmless. False if it never ran undisturbed, if something landed, or if a view at doomed could not be ended:
// quickfixEndView refuses one that owns its object, which no view does.
static bool walk(const std::unordered_set<const void *> *doomed)
{
  VALUE kept = Qnil;
  long room = 0;
  if (s_entries > 2 * s_total + 16)
  {
    room = static_cast<long>(2 * s_total);
    auto make = [&]() { kept = rb_ary_new_capa(room); };
    if (!protect(make))
      return false;
  }
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    size_t registrations = s_registrations;
    Walk pass{doomed, attempt ? Qnil : kept, room};
    auto each = [&]()
    { rb_block_call(s_map, rb_intern("each_pair"), 0, nullptr, walkView, reinterpret_cast<VALUE>(&pass)); };
    if (!protect(each))
      return false;
    if (registrations != s_registrations)
      continue;
    if (!pass.failed)
    {
      s_counts.swap(pass.counts);
      s_total = pass.total;
    }
    s_entries = pass.entries;
    s_lastGc = rb_gc_count();
    s_idle = 0;
    if (!NIL_P(pass.kept))
      replace(pass.kept);
    RB_GC_GUARD(kept);
    return !pass.refused;
  }
  return false;
}

// From a wrapper's out typemap, which delivers what landed here, so a view that may be missing from the map is
// never handed out. It is counted first, so a count never misses it. False if it is not in the map.
bool registerView(VALUE view, const void *address)
{
  try
  {
    ++s_counts[address];
  }
  catch (...)
  {
    return false;
  }
  ++s_total;
  ++s_entries;
  ++s_registrations;
  auto store = [&]() { rb_funcall(s_map, rb_intern("[]="), 2, view, ULL2NUM(reinterpret_cast<uintptr_t>(address))); };
  if (!protect(store))
    return false;
  if (s_total >= s_walkAt)
  {
    if (rb_gc_count() != s_lastGc)
      walk(nullptr);
    s_walkAt = std::max<size_t>(64, 2 * s_total);
  }
  return true;
}

bool anyGroupViews() { return s_total != 0; }

// A call that destroys groups with views runs only once those views are ended. Calls that end nothing recount the
// map once they outnumber its views, if a GC may have collected some.
void endViews(const Addresses &addresses)
{
  if (!s_total)
    return;
  std::unordered_set<const void *> doomed;
  for (const void *address : addresses)
    if (s_counts.count(address))
      doomed.insert(address);
  if (doomed.empty())
  {
    if (++s_idle >= s_total && rb_gc_count() != s_lastGc)
      walk(nullptr);
    return;
  }
  if (!walk(&doomed))
    throw FIX::RuntimeError("the views of the groups this call destroys could not be ended");
}
}

// For the tests: Quickfix._viewCount is how many group views are registered and not ended, after a recount;
// Quickfix._viewEntries how many entries the registry's map holds, ended views among them; and Quickfix._viewsLanded
// how many times something landed in the registry's own calls into Ruby.
VALUE quickfixTestViewCount(VALUE)
{
  QuickfixViews::walk(nullptr);
  redeliverKill();
  QuickfixViews::redeliverError();
  return SIZET2NUM(QuickfixViews::s_total);
}

VALUE quickfixTestViewEntries(VALUE) { return rb_funcall(QuickfixViews::s_map, rb_intern("size"), 0); }

VALUE quickfixTestViewsLanded(VALUE) { return SIZET2NUM(QuickfixViews::s_landed); }

// For quickfix_ruby.rb, which keeps the views of transports (#197; ../quickfix.i): addresses, as Integers. Nothing
// here calls into Ruby while a C++ object that needs destroying is alive, except on running out of memory.
namespace QuickfixTransportViews
{
static VALUE toRuby(const Addresses &addresses)
{
  VALUE result = rb_ary_new_capa(static_cast<long>(addresses.size()));
  for (const void *address : addresses)
    rb_ary_push(result, ULL2NUM(reinterpret_cast<uintptr_t>(address)));
  return result;
}

// What get finds in transport, an Initiator or an Acceptor: nothing in anything else, or in one never made or
// already destroyed.
template <typename Get> static VALUE ofTransport(VALUE transport, Get get)
{
  Addresses addresses;
  bool failed = false;
  try
  {
    void *pointer = 0;
    if (SWIG_IsOK(SWIG_ConvertPtr(transport, &pointer, SWIGTYPE_p_FIX__Initiator, 0)) && pointer)
      addresses = get(*static_cast<FIX::Initiator *>(pointer));
    else if (SWIG_IsOK(SWIG_ConvertPtr(transport, &pointer, SWIGTYPE_p_FIX__Acceptor, 0)) && pointer)
      addresses = get(*static_cast<FIX::Acceptor *>(pointer));
  }
  catch (...)
  {
    failed = true;
  }
  if (failed)
    rb_memerror();
  return toRuby(addresses);
}
}

// Quickfix._transportAddresses(transport): what its destructor frees that a view can point at.
VALUE quickfixTransportAddresses(VALUE, VALUE transport)
{
  return QuickfixTransportViews::ofTransport(transport, [](auto &t) { return QuickfixTransportViews::addressesOf(t); });
}

// Quickfix._sessionAddresses(transport): the sessions it made.
VALUE quickfixSessionAddresses(VALUE, VALUE transport)
{
  return QuickfixTransportViews::ofTransport(transport, [](auto &t) { return QuickfixTransportViews::sessionsOf(t); });
}

// Quickfix._liveSessions: the sessions that exist.
VALUE quickfixLiveSessions(VALUE)
{
  QuickfixTransportViews::Addresses live;
  bool failed = false;
  try
  {
    live = QuickfixTransportViews::liveSessions();
  }
  catch (...)
  {
    failed = true;
  }
  if (failed)
    rb_memerror();
  return QuickfixTransportViews::toRuby(live);
}

// Quickfix._viewAddress(view): what view points at, if it is a view -- a proxy that owns nothing and is not ended,
// as quickfixEndView would take -- and nil if not.
VALUE quickfixViewAddress(VALUE, VALUE view)
{
  if (TYPE(view) != T_DATA || RTYPEDDATA_P(view) || !RTEST(rb_ivar_defined(view, rb_intern("@__swigtype__"))) ||
      RDATA(view)->dfree || !DATA_PTR(view))
    return Qnil;
  return ULL2NUM(reinterpret_cast<uintptr_t>(DATA_PTR(view)));
}
%}
	  	 
// Without director:except a Log or LogFactory method calls Ruby with a bare
// rb_funcall, and an exception longjmps out through the engine's C++ frames (#112).
%define QUICKFIX_RUBY_EXIT_ON_ERROR(method)
%feature("director:except") method {
  if( $error != 0 ) {
    if( deferKill( $error ) ) return Qnil;
    exitOnCallbackError( $error );
  }
}
%enddef

// create has a Log to return and none to make, so a kill there still ends the
// process.
%feature("director:except") FIX::LogFactory::create {
  if( $error != 0 ) {
    exitOnCallbackError( $error );
  }
}

QUICKFIX_RUBY_EXIT_ON_ERROR(FIX::Log::clear)
QUICKFIX_RUBY_EXIT_ON_ERROR(FIX::Log::backup)
QUICKFIX_RUBY_EXIT_ON_ERROR(FIX::Log::onIncoming)
QUICKFIX_RUBY_EXIT_ON_ERROR(FIX::Log::onOutgoing)
QUICKFIX_RUBY_EXIT_ON_ERROR(FIX::Log::onEvent)
QUICKFIX_RUBY_EXIT_ON_ERROR(FIX::LogFactory::destroy)

%feature("director:except") FIX::Application::onCreate {
  if( $error != 0 ) {
    if( deferKill( $error ) ) return Qnil;
    exitOnCallbackError( $error );
  }
}

%feature("director:except") FIX::Application::onLogon {
  if( $error != 0 ) {
    if( deferKill( $error ) ) return Qnil;
    exitOnCallbackError( $error );
  }
}

%feature("director:except") FIX::Application::onLogout {
  if( $error != 0 ) {
    if( deferKill( $error ) ) return Qnil;
    exitOnCallbackError( $error );
  }
}

%feature("director:except") FIX::Application::toAdmin {
  if( $error != 0 ) {
    if( deferKill( $error ) ) return Qnil;
    exitOnCallbackError( $error );
  }
}

%feature("director:except") FIX::Application::toApp {
  if( $error != 0 ) {
    if( deferKill( $error ) ) return Qnil;
    void* result;

    if( SWIG_ConvertPtr($error, &result, SWIGTYPE_p_FIX__DoNotSend, 0 ) != -1 ) {
      rb_set_errinfo(Qnil);
      throw *((FIX::DoNotSend*)result);
    } else {
      exitOnCallbackError( $error );
    }
  }
}

%feature("director:except") FIX::Application::fromAdmin {
  if( $error != 0 ) {
    if( deferKill( $error ) ) return Qnil;
    void* result;

    if( SWIG_ConvertPtr($error, &result, SWIGTYPE_p_FIX__FieldNotFound, 0 ) != -1 ) {
      rb_set_errinfo(Qnil);
      throw *((FIX::FieldNotFound*)result);
    } else if( SWIG_ConvertPtr($error, &result, SWIGTYPE_p_FIX__IncorrectDataFormat, 0 ) != -1 ) {
      rb_set_errinfo(Qnil);
      throw *((FIX::IncorrectDataFormat*)result);
    } else if( SWIG_ConvertPtr($error, &result, SWIGTYPE_p_FIX__IncorrectTagValue, 0 ) != -1 ) {
      rb_set_errinfo(Qnil);
      throw *((FIX::IncorrectTagValue*)result);
    } else if( SWIG_ConvertPtr($error, &result, SWIGTYPE_p_FIX__RejectLogon, 0 ) != -1 ) {
      rb_set_errinfo(Qnil);
      throw *((FIX::RejectLogon*)result);
    } else {
      exitOnCallbackError( $error );
    }
  }
}

%feature("director:except") FIX::Application::fromApp {
  if( $error != 0 ) {
    if( deferKill( $error ) ) return Qnil;
    void* result;

    if( SWIG_ConvertPtr($error, &result, SWIGTYPE_p_FIX__FieldNotFound, 0 ) != -1 ) {
      rb_set_errinfo(Qnil);
      throw *((FIX::FieldNotFound*)result);
    } else if( SWIG_ConvertPtr($error, &result, SWIGTYPE_p_FIX__IncorrectDataFormat, 0 ) != -1 ) {
      rb_set_errinfo(Qnil);
      throw *((FIX::IncorrectDataFormat*)result);
    } else if( SWIG_ConvertPtr($error, &result, SWIGTYPE_p_FIX__IncorrectTagValue, 0 ) != -1 ) {
      rb_set_errinfo(Qnil);
      throw *((FIX::IncorrectTagValue*)result);
    } else if( SWIG_ConvertPtr($error, &result, SWIGTYPE_p_FIX__UnsupportedMessageType, 0 ) != -1 ) {
      rb_set_errinfo(Qnil);
      throw *((FIX::UnsupportedMessageType*)result);
    } else {
      exitOnCallbackError( $error );
    }
  }
}

%init %{
#ifndef _MSC_VER
      struct sigaction new_action, old_action;
      new_action.sa_handler = SIG_DFL;
      sigemptyset( &new_action.sa_mask );
      new_action.sa_flags = 0;
      sigaction( SIGINT, &new_action, &old_action );
#endif
%}

%init %{
  rb_define_module_function(mQuickfix, "_destroyInitiator", RUBY_METHOD_FUNC(destroyInitiator), 1);
  rb_define_module_function(mQuickfix, "_destroyAcceptor", RUBY_METHOD_FUNC(destroyAcceptor), 1);
  rb_define_module_function(mQuickfix, "_initiatorBlocking", RUBY_METHOD_FUNC(isInitiatorBlocking), 1);
  rb_define_module_function(mQuickfix, "_acceptorBlocking", RUBY_METHOD_FUNC(isAcceptorBlocking), 1);
  rb_define_module_function(mQuickfix, "_callbackThread", RUBY_METHOD_FUNC(callbackThread), 0);
  rb_define_module_function(mQuickfix, "_endView", RUBY_METHOD_FUNC(quickfixTestEndView), 1);
  rb_define_module_function(mQuickfix, "_viewCount", RUBY_METHOD_FUNC(quickfixTestViewCount), 0);
  rb_define_module_function(mQuickfix, "_viewEntries", RUBY_METHOD_FUNC(quickfixTestViewEntries), 0);
  rb_define_module_function(mQuickfix, "_viewsLanded", RUBY_METHOD_FUNC(quickfixTestViewsLanded), 0);
  rb_define_module_function(mQuickfix, "_transportAddresses", RUBY_METHOD_FUNC(quickfixTransportAddresses), 1);
  rb_define_module_function(mQuickfix, "_sessionAddresses", RUBY_METHOD_FUNC(quickfixSessionAddresses), 1);
  rb_define_module_function(mQuickfix, "_liveSessions", RUBY_METHOD_FUNC(quickfixLiveSessions), 0);
  rb_define_module_function(mQuickfix, "_viewAddress", RUBY_METHOD_FUNC(quickfixViewAddress), 1);
  // The group views' map (QuickfixViews): a root before it exists, so no GC can take it in between.
  rb_gc_register_address(&QuickfixViews::s_map);
  QuickfixViews::s_map = rb_class_new_instance(0, nullptr, rb_path2class("ObjectSpace::WeakMap"));
%}
