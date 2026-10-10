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
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

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

// Runs a callback into Ruby. A thread that holds the GVL calls straight through;
// one that released it in withoutGvl takes it back for the call, and skips the
// call once Ruby has asked that thread to stop, since the Ruby code would raise
// into director:except, which exits the process. A thread Ruby did not create --
// a connection thread of a threaded transport -- cannot enter Ruby at all.
void withGvl(const std::function<void()> &callback)
{
  if (ruby_thread_has_gvl_p())
  {
    callback();
    return;
  }
  if (!ruby_native_thread_p())
  {
    fprintf(stderr, "quickfix: a callback into Ruby arrived on a thread Ruby did not create; "
                    "the threaded transports cannot be used from Ruby\n");
    abort();
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

%typemap(in) FIX::DataDictionary const *& (FIX::DataDictionary* temp) {
  $1 = new FIX::DataDictionary*[1];
  *$1 = temp;
} 	 

%typemap(free) FIX::DataDictionary const *& {
  delete[] temp; 	 
} 	 
	  	 
%typemap(argout) FIX::DataDictionary const *& {
  void* argp;
  FIX::DataDictionary* pDD = 0;
  int res = SWIG_ConvertPtr($input, &argp, SWIGTYPE_p_FIX__DataDictionary, 0 );
  pDD = reinterpret_cast< FIX::DataDictionary * >(argp);
  *pDD = *(*$1);
} 	 

%include ../quickfix.i
	  	 
%feature("director:except") FIX::Application::onCreate {
  if( $error != 0 ) {
    VALUE message = rb_obj_as_string( $error );
    printf( "%s\n", RSTRING_PTR(message) );
    exit(1);
  }
}

%feature("director:except") FIX::Application::onLogon {
  if( $error != 0 ) {
    VALUE message = rb_obj_as_string( $error );
    printf( "%s\n", RSTRING_PTR(message) );
    exit(1);
  }
}

%feature("director:except") FIX::Application::onLogout {
  if( $error != 0 ) {
    VALUE message = rb_obj_as_string( $error );
    printf( "%s\n", RSTRING_PTR(message) );
    exit(1);
  }
}

%feature("director:except") FIX::Application::toAdmin {
  if( $error != 0 ) {
    VALUE message = rb_obj_as_string( $error );
    printf( "%s\n", RSTRING_PTR(message) );
    exit(1);
  }
}

%feature("director:except") FIX::Application::toApp {
  if( $error != 0 ) {
    void* result;

    Application_toApp_call_depth--;

    if( SWIG_ConvertPtr($error, &result, SWIGTYPE_p_FIX__DoNotSend, 0 ) != -1 ) {
      rb_set_errinfo(Qnil);
      throw *((FIX::DoNotSend*)result);
    } else {
      VALUE message = rb_obj_as_string( $error );
      printf( "%s\n", RSTRING_PTR(message) );
      exit(1);
    }
  }
}

%feature("director:except") FIX::Application::fromAdmin {
  if( $error != 0 ) {
    void* result;

    Application_fromAdmin_call_depth--;

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
      VALUE message = rb_obj_as_string( $error );
      printf( "%s\n", RSTRING_PTR(message) );
      exit(1);
    }
  }
}

%feature("director:except") FIX::Application::fromApp {
  if( $error != 0 ) {
    void* result;

    Application_fromApp_call_depth--;

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
      VALUE message = rb_obj_as_string( $error );
      printf( "%s\n", RSTRING_PTR(message) );
      exit(1);
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
%}
