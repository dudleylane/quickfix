// The Java binding (#148): src/quickfix.i, plus what a JVM needs. swig.sh generates QuickfixJava.cpp,
// QuickfixJava.h and the classes under swig/; the hand-written classes are under main/.

%begin %{
// An engine thread -- a threaded transport's connection thread, a reactor's thread -- attaches to the
// JVM on its first callback and stays attached until it ends: attaching and detaching around every
// callback, SWIG's default, costs microseconds each time. Attached as a daemon, so a running engine does
// not keep the JVM alive; Transports' shutdown hook stops the transports before it goes.
#define SWIG_JAVA_DETACH_ON_THREAD_END
#define SWIG_JAVA_ATTACH_CURRENT_THREAD_AS_DAEMON
#define SWIG_JAVA_USE_THREAD_NAME
%}

%{
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include <Exceptions.h>

// The Java classes the engine's exceptions become, and their callbacks' exceptions are matched against,
// as global references: found once in JNI_OnLoad, through the class loader that loaded this library,
// where FindClass on an engine thread would ask the system class loader.
namespace QuickfixJava
{
struct ExceptionClass
{
  const char *name;
  jclass cls;
  jmethodID make;
};
// Most derived first: the engine's exception is matched against these in order (throwToJava).
ExceptionClass g_exceptions[] = {
  {"SocketSendFailed"}, {"SocketRecvFailed"}, {"SocketCloseFailed"}, {"SocketException"},
  {"DataDictionaryNotFound"}, {"FieldNotFound"}, {"FieldConvertError"}, {"MessageParseError"},
  {"InvalidMessage"}, {"ConfigError"}, {"RuntimeError"}, {"InvalidTagNumber"}, {"RequiredTagMissing"},
  {"TagNotDefinedForMessage"}, {"NoTagValue"}, {"IncorrectTagValue"}, {"IncorrectDataFormat"},
  {"IncorrectMessageStructure"}, {"DuplicateFieldNumber"}, {"InvalidMessageType"},
  {"UnsupportedMessageType"}, {"UnsupportedVersion"}, {"TagOutOfOrder"}, {"EmbeddedSOH"},
  {"OutOfOrderGroupMembers"}, {"RepeatedTag"}, {"RepeatingGroupCountMismatch"}, {"DoNotSend"},
  {"RejectLogon"}, {"SessionNotFound"}, {"IOException"}, {"FIXException"},
};
jfieldID g_detail;
jfieldID g_field;

ExceptionClass &exceptionClass(const char *name)
{
  for (ExceptionClass &e : g_exceptions)
  {
    if (std::strcmp(e.name, name) == 0)
    {
      return e;
    }
  }
  std::fprintf(stderr, "quickfix: no Java class for %s\n", name);
  std::abort();
}

// Throws e into Java as the class of the same name, keeping what it carries.
void throwToJava(JNIEnv *jenv, const FIX::Exception &e)
{
  const char *name = "FIXException";
  int field = 0;
  std::string version;
#define QUICKFIX_JAVA_MATCH(Type) else if (dynamic_cast<const FIX::Type *>(&e)) { name = #Type; }
#define QUICKFIX_JAVA_MATCH_FIELD(Type) \
  else if (const FIX::Type *t = dynamic_cast<const FIX::Type *>(&e)) { name = #Type; field = t->field; }
  if (false) {}
  QUICKFIX_JAVA_MATCH(SocketSendFailed)
  QUICKFIX_JAVA_MATCH(SocketRecvFailed)
  QUICKFIX_JAVA_MATCH(SocketCloseFailed)
  QUICKFIX_JAVA_MATCH(SocketException)
  else if (const FIX::DataDictionaryNotFound *t = dynamic_cast<const FIX::DataDictionaryNotFound *>(&e))
  {
    name = "DataDictionaryNotFound";
    version = t->version;
  }
  QUICKFIX_JAVA_MATCH_FIELD(FieldNotFound)
  QUICKFIX_JAVA_MATCH(FieldConvertError)
  QUICKFIX_JAVA_MATCH(MessageParseError)
  QUICKFIX_JAVA_MATCH(InvalidMessage)
  QUICKFIX_JAVA_MATCH(ConfigError)
  QUICKFIX_JAVA_MATCH(RuntimeError)
  QUICKFIX_JAVA_MATCH_FIELD(InvalidTagNumber)
  QUICKFIX_JAVA_MATCH_FIELD(RequiredTagMissing)
  QUICKFIX_JAVA_MATCH_FIELD(TagNotDefinedForMessage)
  QUICKFIX_JAVA_MATCH_FIELD(NoTagValue)
  QUICKFIX_JAVA_MATCH_FIELD(IncorrectTagValue)
  QUICKFIX_JAVA_MATCH_FIELD(IncorrectDataFormat)
  QUICKFIX_JAVA_MATCH(IncorrectMessageStructure)
  QUICKFIX_JAVA_MATCH(DuplicateFieldNumber)
  QUICKFIX_JAVA_MATCH(InvalidMessageType)
  QUICKFIX_JAVA_MATCH(UnsupportedMessageType)
  QUICKFIX_JAVA_MATCH(UnsupportedVersion)
  QUICKFIX_JAVA_MATCH_FIELD(TagOutOfOrder)
  QUICKFIX_JAVA_MATCH_FIELD(EmbeddedSOH)
  QUICKFIX_JAVA_MATCH_FIELD(OutOfOrderGroupMembers)
  QUICKFIX_JAVA_MATCH_FIELD(RepeatedTag)
  QUICKFIX_JAVA_MATCH_FIELD(RepeatingGroupCountMismatch)
  QUICKFIX_JAVA_MATCH(DoNotSend)
  QUICKFIX_JAVA_MATCH(RejectLogon)
  QUICKFIX_JAVA_MATCH(SessionNotFound)
  QUICKFIX_JAVA_MATCH(IOException)
#undef QUICKFIX_JAVA_MATCH
#undef QUICKFIX_JAVA_MATCH_FIELD
  ExceptionClass &c = exceptionClass(name);
  jstring type = jenv->NewStringUTF(e.type.c_str());
  jstring detail = jenv->NewStringUTF(e.detail.c_str());
  jstring jversion = jenv->NewStringUTF(version.c_str());
  jobject thrown = jenv->CallStaticObjectMethod(c.cls, c.make, type, detail, field, jversion);
  if (thrown && !jenv->ExceptionCheck())
  {
    jenv->Throw(static_cast<jthrowable>(thrown));
  }
}

// An exception a Java callback lets out, other than one the engine expects of that callback, ends the
// process (#112, #122): the engine has no way to continue the callback, and unwinding a Java exception
// through its frames would leave a session half-updated. Print it as Java prints an uncaught one,
// flush, and _exit: exit() would run static destructors under engine threads still running, and a
// Java System.exit would run the shutdown hook, which waits for this very thread.
[[noreturn]] void exitOnCallbackError(JNIEnv *jenv, jthrowable error, const char *callback)
{
  std::fprintf(stderr, "quickfix: an exception escaped %s; exiting\n", callback);
  std::fflush(stderr);
  jenv->Throw(error);
  jenv->ExceptionDescribe();
  std::fflush(nullptr);
  _exit(1);
}

bool isA(JNIEnv *jenv, jthrowable error, const char *name)
{
  return jenv->IsInstanceOf(error, exceptionClass(name).cls);
}

std::string detailOf(JNIEnv *jenv, jthrowable error)
{
  jstring detail = static_cast<jstring>(jenv->GetObjectField(error, g_detail));
  std::string result;
  if (detail)
  {
    const char *chars = jenv->GetStringUTFChars(detail, nullptr);
    result = chars ? chars : "";
    jenv->ReleaseStringUTFChars(detail, chars);
    jenv->DeleteLocalRef(detail);
  }
  return result;
}

int fieldOf(JNIEnv *jenv, jthrowable error) { return jenv->GetIntField(error, g_field); }
} // namespace QuickfixJava

namespace QuickfixViews
{
// The view registry's (after %include ../quickfix.i, below): keeps the JVM, and finds Views.end.
bool load(JavaVM *vm, JNIEnv *jenv);
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *)
{
  JNIEnv *jenv = nullptr;
  if (vm->GetEnv(reinterpret_cast<void **>(&jenv), JNI_VERSION_1_8) != JNI_OK)
  {
    return JNI_ERR;
  }
  for (QuickfixJava::ExceptionClass &e : QuickfixJava::g_exceptions)
  {
    const std::string path = std::string("com/dudleylane/quickfix/") + e.name;
    jclass local = jenv->FindClass(path.c_str());
    if (!local)
    {
      return JNI_ERR;
    }
    e.cls = static_cast<jclass>(jenv->NewGlobalRef(local));
    jenv->DeleteLocalRef(local);
    const std::string signature = "(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)Lcom/dudleylane/quickfix/" +
                                  std::string(e.name) + ";";
    e.make = jenv->GetStaticMethodID(e.cls, "fromEngine", signature.c_str());
    if (!e.make)
    {
      return JNI_ERR;
    }
  }
  jclass base = QuickfixJava::exceptionClass("FIXException").cls;
  QuickfixJava::g_detail = jenv->GetFieldID(base, "detail", "Ljava/lang/String;");
  QuickfixJava::g_field = jenv->GetFieldID(base, "field", "I");
  if (!QuickfixJava::g_detail || !QuickfixJava::g_field || !QuickfixViews::load(vm, jenv))
  {
    return JNI_ERR;
  }
  return JNI_VERSION_1_8;
}
%}

// The engine's exceptions reach Java as the hand-written classes in main/, which mirror Exceptions.h; the
// C++ types are not wrapped, so the names stay free for them.
%ignore FIX::Exception;
%ignore FIX::DataDictionaryNotFound;
%ignore FIX::FieldNotFound;
%ignore FIX::FieldConvertError;
%ignore FIX::MessageParseError;
%ignore FIX::InvalidMessage;
%ignore FIX::ConfigError;
%ignore FIX::RuntimeError;
%ignore FIX::InvalidTagNumber;
%ignore FIX::RequiredTagMissing;
%ignore FIX::TagNotDefinedForMessage;
%ignore FIX::NoTagValue;
%ignore FIX::IncorrectTagValue;
%ignore FIX::IncorrectDataFormat;
%ignore FIX::IncorrectMessageStructure;
%ignore FIX::DuplicateFieldNumber;
%ignore FIX::InvalidMessageType;
%ignore FIX::UnsupportedMessageType;
%ignore FIX::UnsupportedVersion;
%ignore FIX::TagOutOfOrder;
%ignore FIX::EmbeddedSOH;
%ignore FIX::OutOfOrderGroupMembers;
%ignore FIX::RepeatedTag;
%ignore FIX::RepeatingGroupCountMismatch;
%ignore FIX::DoNotSend;
%ignore FIX::RejectLogon;
%ignore FIX::SessionNotFound;
%ignore FIX::IOException;
%ignore FIX::SocketException;
%ignore FIX::SocketSendFailed;
%ignore FIX::SocketRecvFailed;
%ignore FIX::SocketCloseFailed;

// Every call into the engine: a C++ exception becomes the Java one, where it would otherwise reach the
// JNI boundary and terminate the JVM. With decl, that call's, with BEFORE ahead of $action.
%define QUICKFIX_JAVA_EXCEPTION(decl, BEFORE)
%exception decl {
  try
  {
    BEFORE
    $action
  }
  catch (const FIX::Exception &e)
  {
    QuickfixJava::throwToJava(jenv, e);
    return $null;
  }
  catch (const std::exception &e)
  {
    SWIG_JavaThrowException(jenv, SWIG_JavaRuntimeException, e.what());
    return $null;
  }
  catch (...)
  {
    SWIG_JavaThrowException(jenv, SWIG_JavaRuntimeException, "unknown C++ exception");
    return $null;
  }
}
%enddef
QUICKFIX_JAVA_EXCEPTION(, )

// A call that destroys what a view may point at (#196; ../quickfix.i): BEFORE ends those views first.
%define QUICKFIX_VIEWS_EXCEPTION(decl, BEFORE)
QUICKFIX_JAVA_EXCEPTION(decl, BEFORE)
%enddef
%define QUICKFIX_VIEWS_SESSION_EXCEPTION(decl, BEFORE)
QUICKFIX_JAVA_EXCEPTION(decl, BEFORE)
%enddef
// And in Java, delete() on a proxy that owns its object, which frees it and everything in it.
QUICKFIX_VIEWS_EXCEPTION(FIX::FieldMap::~FieldMap, QuickfixViews::beforeDeleting(*arg1);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Message::~Message, QuickfixViews::beforeDeletingMessage(*arg1);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Header::~Header, QuickfixViews::beforeDeleting(*arg1);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Trailer::~Trailer, QuickfixViews::beforeDeleting(*arg1);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Group::~Group, QuickfixViews::beforeDeleting(*arg1);)
QUICKFIX_VIEWS_EXCEPTION(FIX::SessionSettings::~SessionSettings, QuickfixViews::beforeDeletingSettings(*arg1);)

// A callback's Java exception: the FIX exceptions the engine expects of that callback reach it as the C++
// type, which the engine answers as it would a C++ application's (a reject, a refused logon, a message not
// sent); anything else ends the process (exitOnCallbackError).
%define QUICKFIX_JAVA_CALLBACK(method, EXPECTED)
%feature("director:except") method {
  jthrowable $error = jenv->ExceptionOccurred();
  if ($error)
  {
    jenv->ExceptionClear();
    EXPECTED
    QuickfixJava::exitOnCallbackError(jenv, $error, #method);
  }
}
%enddef
%define QUICKFIX_JAVA_RETHROW(Type)
    if (QuickfixJava::isA(jenv, $error, #Type)) { throw FIX::Type(QuickfixJava::detailOf(jenv, $error)); }
%enddef
%define QUICKFIX_JAVA_RETHROW_FIELD(Type)
    if (QuickfixJava::isA(jenv, $error, #Type))
    {
      throw FIX::Type(QuickfixJava::fieldOf(jenv, $error), QuickfixJava::detailOf(jenv, $error));
    }
%enddef

QUICKFIX_JAVA_CALLBACK(FIX::Application::onCreate, )
QUICKFIX_JAVA_CALLBACK(FIX::Application::onLogon, )
QUICKFIX_JAVA_CALLBACK(FIX::Application::onLogout, )
QUICKFIX_JAVA_CALLBACK(FIX::Application::toAdmin, )
QUICKFIX_JAVA_CALLBACK(FIX::Application::toApp, QUICKFIX_JAVA_RETHROW(DoNotSend))
QUICKFIX_JAVA_CALLBACK(FIX::Application::fromAdmin,
                       QUICKFIX_JAVA_RETHROW_FIELD(FieldNotFound) QUICKFIX_JAVA_RETHROW_FIELD(IncorrectDataFormat)
                       QUICKFIX_JAVA_RETHROW_FIELD(IncorrectTagValue) QUICKFIX_JAVA_RETHROW(RejectLogon))
QUICKFIX_JAVA_CALLBACK(FIX::Application::fromApp,
                       QUICKFIX_JAVA_RETHROW_FIELD(FieldNotFound) QUICKFIX_JAVA_RETHROW_FIELD(IncorrectDataFormat)
                       QUICKFIX_JAVA_RETHROW_FIELD(IncorrectTagValue) QUICKFIX_JAVA_RETHROW(UnsupportedMessageType))
QUICKFIX_JAVA_CALLBACK(FIX::Log::clear, )
QUICKFIX_JAVA_CALLBACK(FIX::Log::backup, )
QUICKFIX_JAVA_CALLBACK(FIX::Log::onIncoming, )
QUICKFIX_JAVA_CALLBACK(FIX::Log::onOutgoing, )
QUICKFIX_JAVA_CALLBACK(FIX::Log::onEvent, )
QUICKFIX_JAVA_CALLBACK(FIX::LogFactory::create, )
QUICKFIX_JAVA_CALLBACK(FIX::LogFactory::destroy, )

// A Log a Java LogFactory creates is the session's until the factory's destroy is called with it, and the
// GC must not free it meanwhile: the factory holds it, and hands destroy the same object.
// FIX::Log * appears in director methods only as LogFactory::create's result and destroy's argument.
%typemap(javadirectorout) FIX::Log * "Log.getCPtr(LogFactory.swigKeep(jself, $javacall))"
%typemap(javadirectorin) FIX::Log * "LogFactory.swigRelease(jself, $jniinput)"
%typemap(javacode) FIX::LogFactory %{
  private final java.util.Map<Long, Log> swigLogs = new java.util.concurrent.ConcurrentHashMap<>();

  static Log swigKeep(LogFactory factory, Log log) {
    if (log != null) {
      factory.swigLogs.put(Log.getCPtr(log), log);
    }
    return log;
  }

  static Log swigRelease(LogFactory factory, long cPtr) {
    if (cPtr == 0) {
      return null;
    }
    Log log = factory.swigLogs.remove(cPtr);
    return log != null ? log : new Log(cPtr, false);
  }
%}

// A SessionID handed to Java -- a callback's argument, LogFactory.create's, or Session.getSessionID()'s result --
// is never a view of the engine's, which its transport frees with its sessions (#190). Nor is it a copy per call,
// as Python and Ruby are handed: Java frees a native object only in its proxy's finalize(), after a collection
// that native memory does not prompt, and a copy's 280 native bytes outweigh the 64 its proxy takes in the heap,
// so an application allocating little else would hold several times its young generation in copies. It is one
// copy per session ID, made when first handed out and never freed, which every SessionID handed out for that ID
// shares. Being shared, it cannot change: fromString throws for it, and every SessionID's getters return copies,
// which also keeps a field's lazily computed length off the shared one.
%{
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include <SessionID.h>

namespace QuickfixJava
{
inline FIX::SessionID *share(const FIX::SessionID &sessionID)
{
  struct Copies
  {
    std::shared_mutex mutex;
    std::unordered_map<std::string, FIX::SessionID> byString; // a node keeps its address through a rehash
  };
  static Copies *const copies = new Copies; // never freed: a proxy can outlive every static destructor
  const std::string &key = sessionID.toStringFrozen();
  {
    std::shared_lock lock(copies->mutex);
    auto found = copies->byString.find(key);
    if (found != copies->byString.end())
    {
      return &found->second;
    }
  }
  std::unique_lock lock(copies->mutex);
  return &copies->byString.try_emplace(key, sessionID).first->second;
}
}
%}
%typemap(directorin, descriptor="L$packagepath/$javaclassname;") const FIX::SessionID &
%{ *(FIX::SessionID **)&$input = QuickfixJava::share($1); %}
%typemap(javadirectorin) const FIX::SessionID & "$javaclassname.swigShare($jniinput)"
%typemap(out) const FIX::SessionID & %{ *(FIX::SessionID **)&$result = QuickfixJava::share(*$1); %}
%typemap(javaout) const FIX::SessionID & {
    return $javaclassname.swigShare($jnicall);
  }
%rename(fromStringUnchecked) FIX::SessionID::fromString;
%javamethodmodifiers FIX::SessionID::fromString "private";
%typemap(out) const FIX::BeginString &getBeginString %{ *(FIX::BeginString **)&$result = new FIX::BeginString(*$1); %}
%typemap(out) const FIX::SenderCompID &getSenderCompID
%{ *(FIX::SenderCompID **)&$result = new FIX::SenderCompID(*$1); %}
%typemap(out) const FIX::TargetCompID &getTargetCompID
%{ *(FIX::TargetCompID **)&$result = new FIX::TargetCompID(*$1); %}
%typemap(javaout) const FIX::BeginString &getBeginString, const FIX::SenderCompID &getSenderCompID,
                  const FIX::TargetCompID &getTargetCompID {
    return new $javaclassname($jnicall, true);
  }
%typemap(javacode) FIX::SessionID %{
  // Whether the engine handed this out: then it is the one copy of its ID, which all of those share.
  private transient boolean shared;

  static SessionID swigShare(long cPtr) {
    SessionID sessionID = new SessionID(cPtr, false);
    sessionID.shared = true;
    return sessionID;
  }

  /**
   * Sets this SessionID from its string, "BeginString:SenderCompID->TargetCompID", or with ":qualifier" after
   * that. A SessionID the engine handed out -- a callback's argument, LogFactory.create's, or
   * Session.getSessionID()'s result -- is shared by every one handed out for its session, so it cannot change:
   * for one of those this throws UnsupportedOperationException. Change a new SessionID instead.
   */
  public void fromString(String str) {
    if (shared) {
      throw new UnsupportedOperationException("a SessionID the engine handed out is shared and cannot change: "
                                              + "change a new SessionID instead");
    }
    fromStringUnchecked(str);
  }

  // Equal by value, as the engine's operator== compares: by the session's string, which toString() returns (#190).
  @Override
  public boolean equals(Object obj) {
    return obj instanceof SessionID other && toString().equals(other.toString());
  }

  @Override
  public int hashCode() {
    return toString().hashCode();
  }
%}

%typemap(javaclassmodifiers) FIX::Application %{
/**
 * FIX::Application: the engine calls these on its own threads -- a reactor's, or a threaded transport's
 * connection thread -- as it would a C++ application's.
 * <ul>
 * <li>A {@link Message} passed in is the engine's, valid only until the callback returns: the engine reuses it
 * for the next message. Read it there (a generated class's {@code view} does so without copying), or keep a
 * copy, {@code new Message(message)}; never the argument itself.</li>
 * <li>The FIX exceptions the engine expects of a callback -- {@link FieldNotFound}, {@link IncorrectTagValue}
 * and {@link IncorrectDataFormat} from {@code fromApp} or {@code fromAdmin}, {@link UnsupportedMessageType} from
 * {@code fromApp}, {@link RejectLogon} from {@code fromAdmin}, {@link DoNotSend} from {@code toApp} -- are
 * answered as for a C++ application. Any other exception ends the process with status 1, its stack trace
 * printed: the engine cannot continue a callback half done.</li>
 * <li>A callback may {@code stop()} the transport calling it: if another thread is stopping it already, the
 * callback's stop returns at once; if not, a threaded transport's returns once its wait for this callback
 * times out, about 15 s. It must not {@code close()} it, which would free the transport under the callback's
 * own thread, nor call {@code System.exit}: the shutdown hook that stops the running transports would wait for
 * this very callback.</li>
 * </ul>
 */
public class%}

// The transports are the hand-written classes in main/, which keep their arguments and register while
// started (Transports); the SWIG classes they extend are not for direct use.
%javamethodmodifiers FIX::SocketInitiator::SocketInitiator "protected";
%javamethodmodifiers FIX::SocketAcceptor::SocketAcceptor "protected";
%javamethodmodifiers FIX::SSLSocketInitiator::SSLSocketInitiator "protected";
%javamethodmodifiers FIX::SSLSocketAcceptor::SSLSocketAcceptor "protected";
%javamethodmodifiers FIX::ThreadedSocketInitiator::ThreadedSocketInitiator "protected";
%javamethodmodifiers FIX::ThreadedSocketAcceptor::ThreadedSocketAcceptor "protected";
%javamethodmodifiers FIX::ThreadedSSLSocketInitiator::ThreadedSSLSocketInitiator "protected";
%javamethodmodifiers FIX::ThreadedSSLSocketAcceptor::ThreadedSSLSocketAcceptor "protected";

// FixCommonFields.h's field classes are SessionID's getters' types. Under their own names they would clash
// with the generated com.dudleylane.quickfix.field classes in any code that imports both packages.
%rename(BeginStringField) FIX::BeginString;
%rename(SenderCompIDField) FIX::SenderCompID;
%rename(TargetCompIDField) FIX::TargetCompID;

// Sequence numbers are a Java long: SWIG's BigInteger for a uint64_t is awkward to use and calls into the JVM
// without checking for an exception (-Xcheck:jni), and a sequence number never nears 2^63. UInt64Field keeps
// BigInteger, whose values can exceed a long.
%apply long long { FIX::SEQNUM, SEQNUM };

// Loads the library before anything calls into it. com.dudleylane.quickfix.library names a file to load
// instead of libquickfixjava.so from java.library.path, as the tests do.
%pragma(java) jniclasscode=%{
  static {
    String library = System.getProperty("com.dudleylane.quickfix.library");
    if (library != null) {
      System.load(library);
    } else {
      System.loadLibrary("quickfixjava");
    }
  }
%}

// Classes the headers use before the header declaring them is included: without these, Session::getLog(),
// getStore(), Message::getGroup() and getSessionID() came out as opaque SWIGTYPE_p_* handles.
namespace FIX
{
class Log;
class MessageStore;
class Group;
class SessionID;
class DataDictionary;
}

%include std_string.i
%include std_set.i
namespace std
{
%template(StringSet) set<std::string>;
};

// The DataDictionary lookups return through reference parameters, which Java has no way to pass: as SWIG
// wraps them they took opaque handles nobody can make. In their place, Java-shaped ones: a name or value, or
// null when the dictionary has none; a tag, or 0; and a group's delimiter and dictionary separately.
%ignore FIX::DataDictionary::getFieldName(int, std::string &) const;
%ignore FIX::DataDictionary::getFieldTag(const std::string &, int &) const;
%ignore FIX::DataDictionary::getValueName(int, const std::string &, std::string &) const;
%ignore FIX::DataDictionary::getNameValue(int, const std::string &, std::string &) const;
%ignore FIX::DataDictionary::getGroup(const std::string &, int, int &, const DataDictionary *&) const;
%newobject FIX::DataDictionary::getGroupDictionary;
%extend FIX::DataDictionary {
  /** The field's name, or null if the dictionary does not name it. */
  const char *getFieldName(int field) const
  {
    thread_local std::string result;
    return $self->getFieldName(field, result) ? result.c_str() : nullptr;
  }
  /** The tag of the field so named, or 0 if the dictionary has none. */
  int getFieldTag(const std::string &name) const
  {
    int field = 0;
    return $self->getFieldTag(name, field) ? field : 0;
  }
  /** The name of the field's value, or null. */
  const char *getValueName(int field, const std::string &value) const
  {
    thread_local std::string result;
    return $self->getValueName(field, value, result) ? result.c_str() : nullptr;
  }
  /** The value of the field's value so named, or null. */
  const char *getNameValue(int field, const std::string &name) const
  {
    thread_local std::string result;
    return $self->getNameValue(field, name, result) ? result.c_str() : nullptr;
  }
  /** The delimiter of the group field opens in message type msg, or 0 if it opens none. */
  int getGroupDelimiter(const std::string &msg, int field) const
  {
    int delim = 0;
    const FIX::DataDictionary *group = nullptr;
    return $self->getGroup(msg, field, delim, group) ? delim : 0;
  }
  /**
   * A copy of that group's dictionary, or null. A copy, not a view (#196): this dictionary deletes its own when one
   * is read into it, or addGroup replaces that group, so a view could not outlive them. Copying FIX 5.0 SP2's
   * largest groups takes a millisecond or so: keep the result, and ask isGroup or getGroupDelimiter what they say.
   */
  FIX::DataDictionary *getGroupDictionary(const std::string &msg, int field) const
  {
    int delim = 0;
    const FIX::DataDictionary *group = nullptr;
    return $self->getGroup(msg, field, delim, group) && group ? new FIX::DataDictionary(*group) : nullptr;
  }
}

// Bytes that need not be text -- a data field's (RawData, SecureData, XmlData), or a whole message holding one
// -- cross as a byte[]: a String crosses JNI as modified UTF-8, which does not survive arbitrary bytes.
%{
typedef std::string QuickfixBytes;
%}
typedef std::string QuickfixBytes;
%typemap(jni) QuickfixBytes, const QuickfixBytes & "jbyteArray"
%typemap(jtype) QuickfixBytes, const QuickfixBytes & "byte[]"
%typemap(jstype) QuickfixBytes, const QuickfixBytes & "byte[]"
%typemap(javain) const QuickfixBytes & "$javainput"
%typemap(javaout) QuickfixBytes {
    return $jnicall;
  }
%typemap(in) const QuickfixBytes & (std::string temp) {
  if (!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null byte[]");
    return $null;
  }
  const jsize length = jenv->GetArrayLength($input);
  temp.resize(static_cast<size_t>(length));
  jenv->GetByteArrayRegion($input, 0, length, reinterpret_cast<jbyte *>(temp.data()));
  $1 = &temp;
}
%typemap(out) QuickfixBytes {
  $result = jenv->NewByteArray(static_cast<jsize>($1.size()));
  if ($result) {
    jenv->SetByteArrayRegion($result, 0, static_cast<jsize>($1.size()), reinterpret_cast<const jbyte *>($1.data()));
  }
}
%extend FIX::FieldMap {
  /** The field's bytes as they are, for a data field; FieldNotFound if it is not set. */
  QuickfixBytes getFieldBytes(int tag) const { return $self->getField(tag); }
  /** Sets the field to these bytes as they are; a data field's length field is the caller's to set. */
  void setFieldBytes(int tag, const QuickfixBytes &value) { $self->setField(tag, value); }
}
// setBytes is setString, and ends the views of the message's groups as it does (#196; ../quickfix.i). Ahead of
// the %extend, which takes the features declared before it.
QUICKFIX_VIEWS_EXCEPTION(FIX::Message::setBytes, QuickfixViews::beforeClearingMessage(*arg1);)
%extend FIX::Message {
  /** The message as the bytes the engine would send. */
  QuickfixBytes toBytes() const { return $self->toString(); }
  /** Parses these bytes into the message, as setString does. */
  void setBytes(const QuickfixBytes &message, bool validate = true) { $self->setString(message, validate); }
  /** With a dictionary, which a data field holding an SOH needs: it says where the field's bytes end. */
  void setBytes(const QuickfixBytes &message, bool validate, const FIX::DataDictionary *dictionary)
  {
    $self->setString(message, validate, dictionary);
  }
}

// For the generated classes (spec/GeneratorJava.rb), whose constructors set BeginString, ApplVerID and MsgType and
// whose view() reads MsgType: a header field, without the header proxy an owned message would register (Views).
%javamethodmodifiers FIX::Message::getHeaderField "protected";
%javamethodmodifiers FIX::Message::setHeaderField "protected";
%extend FIX::Message {
  std::string getHeaderField(int tag) const { return $self->getHeader().getField(tag); }
  void setHeaderField(int tag, const std::string &value) { $self->getHeader().setField(tag, value); }
}

// What a call returns from inside another object (#194; ../quickfix.i). A view holds its owner in swigOwner, so the
// owner is not finalized while the view is reachable; a const view is a copy its proxy owns; a call whose result
// is its argument returns the argument. The proxies' parameters keep their C++ names, field and group.
//
// A view of an object that a call from Java can destroy is also registered, and that call ends it first (#196;
// Views, and QuickfixViews below): a group's view always, and a header's, a trailer's, a SessionSettings
// dictionary's or a generated class's view() of a message when Java can destroy what it views.
%typemap(javacode) FIX::FieldMap %{
  // What this proxy is a view into, if anything: kept reachable while this is (#194).
  protected Object swigOwner;
  // Whether this is a view the binding registered (#196; Views).
  transient boolean swigRegistered;
%}
%typemap(javacode) FIX::Dictionary %{
  // What this proxy is a view into, if anything: kept reachable while this is (#194).
  protected Object swigOwner;
%}
%typemap(javacode) FIX::Message %{
  /**
   * For a generated class's view(): message as that class, which make constructs from message's pointer. The view
   * holds message, and is registered as a header view would be (Views.into). IllegalArgumentException if message's
   * MsgType is not msgType, FieldNotFound if it has none, and NullPointerException if message is null, deleted or
   * an ended view.
   */
  protected static <T extends Message> T typedView(Message message, String msgType, String name,
                                                   java.util.function.LongFunction<T> make) {
    String actual = message.getHeaderField(35);
    if (!msgType.equals(actual)) {
      throw new IllegalArgumentException("not a " + name + " (35=" + msgType + "): 35=" + actual);
    }
    long cPtr = getCPtr(message);
    T view = make.apply(cPtr);
    view.swigOwner = message;
    return Views.into(message, view, cPtr);
  }
%}
%typemap(javaout) FIX::FieldMap &getGroupRef, FIX::FieldMap *getGroupPtr {
    long cPtr = $jnicall;
    if (cPtr == 0) {
      return null;
    }
    $javaclassname view = new $javaclassname(cPtr, false);
    view.swigOwner = this;
    return Views.group(view, cPtr);
  }
%typemap(javaout) FIX::Header &getHeader, const FIX::Header &getHeader, FIX::Trailer &getTrailer,
                  const FIX::Trailer &getTrailer, const FIX::Dictionary &get {
    long cPtr = $jnicall;
    if (cPtr == 0) {
      return null;
    }
    $javaclassname view = new $javaclassname(cPtr, false);
    view.swigOwner = this;
    return Views.into(this, view, cPtr);
  }
%typemap(out) const FIX::FieldBase &getFieldRef, const FIX::FieldBase *const getFieldPtr
%{ *(FIX::FieldBase **)&$result = new FIX::FieldBase(*$1); %}
%typemap(out) const std::set<FIX::SessionID> &getSessions
%{ *(std::set<FIX::SessionID> **)&$result = new std::set<FIX::SessionID>(*$1); %}
%typemap(javaout) const FIX::FieldBase &getFieldRef, const FIX::FieldBase *const getFieldPtr,
                  const std::set<FIX::SessionID> &getSessions {
    return new $javaclassname($jnicall, true);
  }
%typemap(javaout) FIX::FieldBase &getField {
    $jnicall;
    return field;
  }
%typemap(javaout) FIX::FieldMap &getGroup, FIX::Group &getGroup, Group &getGroup {
    $jnicall;
    return group;
  }

// What a transport hands out of itself (#197; ../quickfix.i, Views): a Session, a session's store and log, and the
// transport's log. Each holds its transport in swigOwner -- a session's store and log through the session's view --
// so the GC never finalizes a transport a view can reach; and each is registered by the address of what it views,
// so that the transport's delete(), which close() and its finalizer reach, ends it, as a constructor that throws
// ends those of the sessions it had made. A static lookup's view is given the transport that made its session; one
// taken in onCreate, before that transport exists, once its constructor returns (Views.constructed). The javaconstruct
// and javadestruct_derived below are each transport class's, the *Base classes a script can also make directly.
%typemap(javacode) FIX::Session, FIX::Log, FIX::MessageStore %{
  // What this proxy is a view into, if anything: kept reachable while this is (#197).
  protected Object swigOwner;
%}
%typemap(javaout) FIX::Session *lookupSession, FIX::Session *registerSession {
    long cPtr = $jnicall;
    return cPtr == 0 ? null : Views.session(new Session(cPtr, false), cPtr, null);
  }
%typemap(javaout) FIX::Session *getSession {
    long cPtr = $jnicall;
    return cPtr == 0 ? null : Views.session(new Session(cPtr, false), cPtr, this);
  }
%typemap(javaout) FIX::Log *getLog {
    long cPtr = $jnicall;
    return cPtr == 0 ? null : Views.log(new Log(cPtr, false), cPtr, this);
  }
%typemap(javaout) const FIX::MessageStore *getStore {
    long cPtr = $jnicall;
    return cPtr == 0 ? null : Views.store(new MessageStore(cPtr, false), cPtr, this);
  }
%define QUICKFIX_JAVA_TRANSPORT(Class)
%typemap(javaconstruct) Class {
    this(Views.constructing(() -> $imcall), true);
    Views.constructed(this);
  }
%typemap(javadestruct_derived, methodname="delete", methodmodifiers="public synchronized", parameters="") Class {
    if (swigCPtr != 0) {
      if (swigCMemOwn) {
        long[] freed = Views.teardown(this);
        swigCMemOwn = false;
        try {
          $jnicall;
        } finally {
          Views.endTransportViews(freed);
        }
      }
      swigCPtr = 0;
    }
    super.delete();
  }
%enddef
QUICKFIX_JAVA_TRANSPORT(FIX::SocketInitiator)
QUICKFIX_JAVA_TRANSPORT(FIX::SocketAcceptor)
QUICKFIX_JAVA_TRANSPORT(FIX::ThreadedSocketInitiator)
QUICKFIX_JAVA_TRANSPORT(FIX::ThreadedSocketAcceptor)
QUICKFIX_JAVA_TRANSPORT(FIX::SSLSocketInitiator)
QUICKFIX_JAVA_TRANSPORT(FIX::SSLSocketAcceptor)
QUICKFIX_JAVA_TRANSPORT(FIX::ThreadedSSLSocketInitiator)
QUICKFIX_JAVA_TRANSPORT(FIX::ThreadedSSLSocketAcceptor)
// What a transport was made with, getApplication() and getMessageStoreFactory(), each class in main/ returns as the
// object it keeps. A session's getAllowedRemoteAddresses() and a transport's getSessionSettings() return a copy, and
// getDataDictionaryProvider() and getLogonTime(), which returned opaque handles that died with the transport, are
// not wrapped (#197).
%ignore FIX::Session::getDataDictionaryProvider;
%ignore FIX::Session::getLogonTime;
%typemap(out) const std::set<std::string> &getAllowedRemoteAddresses
%{ *(std::set<std::string> **)&$result = new std::set<std::string>(*$1); %}
%typemap(javaout) const std::set<std::string> &getAllowedRemoteAddresses {
    return new $javaclassname($jnicall, true);
  }
%typemap(out) const FIX::Dictionary *const getSessionSettings
%{ *(FIX::Dictionary **)&$result = $1 ? new FIX::Dictionary(*$1) : nullptr; %}
%typemap(javaout) const FIX::Dictionary *const getSessionSettings {
    long cPtr = $jnicall;
    return cPtr == 0 ? null : new $javaclassname(cPtr, true);
  }

// A proxy whose object was deleted, or a view whose object the binding has destroyed, has swigCPtr 0, and a call
// took it for a null pointer: a method ran on nullptr, which crashed the JVM, and a pointer argument read it as
// none, as setString does a dictionary. As self it now throws NullPointerException, and as a pointer argument too
// (Proxies.cPtr). null still passes as a null pointer, and a reference argument throws already (SWIG's null check).
%typemap(check, noblock=1) SWIGTYPE *self {
  if (!$1) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "$symname: its object was deleted, or the view ended");
    return $null;
  }
}
%typemap(javain) SWIGTYPE * "Proxies.cPtr($javaclassname.getCPtr($javainput), $javainput)"

%include ../quickfix.i

// The views of objects that a call from Java can destroy (#196; ../quickfix.i, Views): each by the address of its
// object, as a weak global reference to its proxy, so a view still goes when the GC collects it. Views.group
// registers a group's view, and Views.into a header's, a trailer's, a SessionSettings dictionary's or a generated
// class's view() of a message when Java can destroy what it views. A call that destroys objects first ends the
// views registered at them through Views.end, which calls a view's delete(): a view owns nothing, so that only sets
// its pointer to 0, and the null-proxy check above throws for it from then on.
//
// One mutex guards the map. The counts are written under it and read without it, for the calls' fast check: one
// load while no group view is registered, and for delete() while no view at all is. It is never held across a call
// into Java: endViews takes the entries at its addresses out of the map under it, and ends their views after. An
// entry leaves the map once: taken by endViews, or pruned once the GC has cleared its reference -- at a
// registration once the map has doubled since the last prune, and once calls that end nothing outnumber its
// entries. A view its caller deleted stays until one of those, and ending it again does nothing.
%{
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace QuickfixViews
{
struct Entry
{
  jweak view;
  bool group;
};
static JavaVM *s_vm = nullptr;
static jclass s_class = nullptr;  // Views
static jmethodID s_end = nullptr; // Views.end(Object)
static std::mutex s_mutex;
static std::unordered_multimap<const void *, Entry> s_entries;
static size_t s_groupEntries = 0;
static size_t s_pruneAt = 64;
static size_t s_idle = 0;
static std::atomic<size_t> s_groups{0};
static std::atomic<size_t> s_all{0};

bool load(JavaVM *vm, JNIEnv *jenv)
{
  s_vm = vm;
  jclass local = jenv->FindClass("com/dudleylane/quickfix/Views");
  if (!local)
    return false;
  s_class = static_cast<jclass>(jenv->NewGlobalRef(local));
  jenv->DeleteLocalRef(local);
  s_end = s_class ? jenv->GetStaticMethodID(s_class, "end", "(Ljava/lang/Object;)Z") : nullptr;
  return s_end != nullptr;
}

// Under s_mutex, after a change to the map.
static void publish()
{
  s_groups.store(s_groupEntries, std::memory_order_release);
  s_all.store(s_entries.size(), std::memory_order_release);
}

// Under s_mutex: drops the entries whose views the GC has collected.
static void prune(JNIEnv *jenv)
{
  for (auto entry = s_entries.begin(); entry != s_entries.end();)
  {
    if (jenv->IsSameObject(entry->second.view, nullptr))
    {
      jenv->DeleteWeakGlobalRef(entry->second.view);
      s_groupEntries -= entry->second.group;
      entry = s_entries.erase(entry);
    }
    else
    {
      ++entry;
    }
  }
  s_idle = 0;
  publish();
}

bool anyGroupViews() { return s_groups.load(std::memory_order_acquire) != 0; }
static bool anyViews() { return s_all.load(std::memory_order_acquire) != 0; }

// A Java exception pending on entry is set aside, since most JNI calls may not be made under one, and is pending
// again on every exit.
class Pending
{
public:
  explicit Pending(JNIEnv *jenv) : m_jenv(jenv), m_error(jenv->ExceptionOccurred())
  {
    if (m_error)
      m_jenv->ExceptionClear();
  }
  ~Pending()
  {
    if (m_error)
    {
      m_jenv->Throw(m_error);
      m_jenv->DeleteLocalRef(m_error);
    }
  }
  bool any() const { return m_error != nullptr; }
  Pending(const Pending &) = delete;
  Pending &operator=(const Pending &) = delete;

private:
  JNIEnv *m_jenv;
  jthrowable m_error;
};

// Throws, once it has ended the rest, if a view could not be ended (Views.end said so, or threw), so that the call
// which would destroy its object does not run; but not over an exception that was pending on entry, which only the
// comparison after send() and reverseRoute() can meet, and that comparison runs after the call.
void endViews(const Addresses &addresses)
{
  if (addresses.empty())
    return;
  JNIEnv *jenv = nullptr;
  if (s_vm->GetEnv(reinterpret_cast<void **>(&jenv), JNI_VERSION_1_8) != JNI_OK)
    throw FIX::RuntimeError("the views of the objects this call destroys could not be ended");
  Pending pending(jenv);
  std::vector<jweak> doomed;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    size_t count = 0;
    for (const void *address : addresses)
      count += s_entries.count(address);
    if (!count)
    {
      if (++s_idle >= s_entries.size())
        prune(jenv);
      return;
    }
    // Reserved first, so that nothing leaves the map unless all of it can. An address can be given twice.
    doomed.reserve(count);
    for (const void *address : addresses)
    {
      auto range = s_entries.equal_range(address);
      for (auto entry = range.first; entry != range.second; ++entry)
      {
        doomed.push_back(entry->second.view);
        s_groupEntries -= entry->second.group;
      }
      s_entries.erase(range.first, range.second);
    }
    s_idle = 0;
    publish();
  }
  bool failed = false;
  for (jweak weak : doomed)
  {
    jobject view = jenv->NewLocalRef(weak);
    jenv->DeleteWeakGlobalRef(weak);
    if (!view)
      continue;
    jboolean ended = jenv->CallStaticBooleanMethod(s_class, s_end, view);
    if (jenv->ExceptionCheck())
    {
      jenv->ExceptionClear();
      ended = JNI_FALSE;
    }
    jenv->DeleteLocalRef(view);
    failed = failed || !ended;
  }
  if (failed && !pending.any())
    throw FIX::RuntimeError("the views of the objects this call destroys could not be ended");
}

// Java's delete() on a proxy that owns its object (QUICKFIX_VIEWS_EXCEPTION above): the object, and every group in
// it. A message's header and trailer are its members.
inline void beforeDeleting(const FIX::FieldMap &map)
{
  if (!anyViews())
    return;
  Addresses addresses(1, &map);
  addGroupsUnder(map, addresses);
  endViews(addresses);
}

inline void beforeDeletingMessage(const FIX::Message &message)
{
  if (!anyViews())
    return;
  Addresses addresses{&message, &message.getHeader(), &message.getTrailer()};
  addGroupsUnder(message.getHeader(), addresses);
  addGroupsUnder(message, addresses);
  addGroupsUnder(message.getTrailer(), addresses);
  endViews(addresses);
}

// Every dictionary get() can return: SessionSettings never erases one before its destructor.
inline void beforeDeletingSettings(const FIX::SessionSettings &settings)
{
  if (!anyViews())
    return;
  Addresses addresses(1, &settings.get());
  for (const FIX::SessionID &sessionID : settings.getSessions())
    addresses.push_back(&settings.get(sessionID));
  endViews(addresses);
}
} // namespace QuickfixViews

// Views.register: view, a proxy of the object at cPtr, is registered there. OutOfMemoryError if it cannot be, and
// the view is then not handed out.
extern "C" JNIEXPORT void JNICALL Java_com_dudleylane_quickfix_Views_register(JNIEnv *jenv, jclass, jobject view,
                                                                              jlong cPtr, jboolean group)
{
  using namespace QuickfixViews;
  jweak weak = jenv->NewWeakGlobalRef(view);
  if (!weak)
  {
    if (!jenv->ExceptionCheck())
      SWIG_JavaThrowException(jenv, SWIG_JavaOutOfMemoryError, "a view could not be registered");
    return;
  }
  try
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_entries.emplace(reinterpret_cast<const void *>(cPtr), Entry{weak, group == JNI_TRUE});
    s_groupEntries += group == JNI_TRUE;
    if (s_entries.size() >= s_pruneAt)
    {
      prune(jenv);
      s_pruneAt = std::max<size_t>(64, 2 * s_entries.size());
    }
    publish();
  }
  catch (...)
  {
    jenv->DeleteWeakGlobalRef(weak);
    SWIG_JavaThrowException(jenv, SWIG_JavaOutOfMemoryError, "a view could not be registered");
  }
}

// For the tests, by reflection, once the views the GC has collected are dropped: whether view is registered
// (Views.registered), and how many views are registered at cPtr (Views.countAt).
extern "C" JNIEXPORT jboolean JNICALL Java_com_dudleylane_quickfix_Views_registered(JNIEnv *jenv, jclass, jobject view)
{
  using namespace QuickfixViews;
  std::lock_guard<std::mutex> lock(s_mutex);
  prune(jenv);
  for (const auto &entry : s_entries)
    if (jenv->IsSameObject(entry.second.view, view))
      return JNI_TRUE;
  return JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL Java_com_dudleylane_quickfix_Views_countAt(JNIEnv *jenv, jclass, jlong cPtr)
{
  using namespace QuickfixViews;
  std::lock_guard<std::mutex> lock(s_mutex);
  prune(jenv);
  return static_cast<jlong>(s_entries.count(reinterpret_cast<const void *>(cPtr)));
}

// For Views' registry of what transports hand out (#197; ../quickfix.i): addresses, as Java longs.
namespace QuickfixTransportViews
{
template <typename Get> static jlongArray toJava(JNIEnv *jenv, Get get)
{
  std::vector<jlong> values;
  try
  {
    for (const void *address : get())
      values.push_back(static_cast<jlong>(reinterpret_cast<std::uintptr_t>(address)));
  }
  catch (...)
  {
    SWIG_JavaThrowException(jenv, SWIG_JavaOutOfMemoryError, "a transport's views could not be listed");
    return nullptr;
  }
  jlongArray result = jenv->NewLongArray(static_cast<jsize>(values.size()));
  if (result)
    jenv->SetLongArrayRegion(result, 0, static_cast<jsize>(values.size()), values.data());
  return result;
}

template <typename Transport> static Transport &transport(jlong cPtr)
{
  return *reinterpret_cast<Transport *>(static_cast<std::uintptr_t>(cPtr));
}
}

extern "C" JNIEXPORT jlongArray JNICALL Java_com_dudleylane_quickfix_Views_addressesOf(JNIEnv *jenv, jclass, jlong cPtr,
                                                                                     jboolean initiator)
{
  using namespace QuickfixTransportViews;
  return toJava(jenv, [&] {
    return initiator ? addressesOf(transport<FIX::Initiator>(cPtr)) : addressesOf(transport<FIX::Acceptor>(cPtr));
  });
}

extern "C" JNIEXPORT jlongArray JNICALL Java_com_dudleylane_quickfix_Views_sessionsOf(JNIEnv *jenv, jclass, jlong cPtr,
                                                                                    jboolean initiator)
{
  using namespace QuickfixTransportViews;
  return toJava(jenv, [&] {
    return initiator ? sessionsOf(transport<FIX::Initiator>(cPtr)) : sessionsOf(transport<FIX::Acceptor>(cPtr));
  });
}

extern "C" JNIEXPORT jlongArray JNICALL Java_com_dudleylane_quickfix_Views_liveSessions(JNIEnv *jenv, jclass)
{
  return QuickfixTransportViews::toJava(jenv, [] { return QuickfixTransportViews::liveSessions(); });
}
%}
