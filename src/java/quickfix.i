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
  if (!QuickfixJava::g_detail || !QuickfixJava::g_field)
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
// JNI boundary and terminate the JVM.
%exception {
  try
  {
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

%include ../quickfix.i
