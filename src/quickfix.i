#include "C++/DataDictionary.h"
#include "C++/Exceptions.h"
#include <type_traits>

%module(directors="1") quickfix

%exceptionclass FIX::Exception;

%include typemaps.i
%include std_string.i
%include exception.i
%include carrays.i
%include std_unique_ptr.i
%include std_set.i
%include std_vector.i
%include stdint.i

namespace std
{
  %template(VectorString) vector<std::string>;
};

%feature("director") FIX::Application;
%feature("classic") FIX::Exception;
%feature("director") FIX::LogFactory;
%feature("director") FIX::Log;

%array_class(int, IntArray);

%ignore _REENTRANT;
%rename(SocketInitiatorBase) FIX::SocketInitiator;
%rename(SocketAcceptorBase) FIX::SocketAcceptor;
%rename(SSLSocketInitiatorBase) FIX::SSLSocketInitiator;
%rename(SSLSocketAcceptorBase) FIX::SSLSocketAcceptor;
%rename(ThreadedSocketInitiatorBase) FIX::ThreadedSocketInitiator;
%rename(ThreadedSocketAcceptorBase) FIX::ThreadedSocketAcceptor;
%rename(ThreadedSSLSocketInitiatorBase) FIX::ThreadedSSLSocketInitiator;
%rename(ThreadedSSLSocketAcceptorBase) FIX::ThreadedSSLSocketAcceptor;

// Rename enum's as they're clashing with some classes
%rename("%(regex:/^FIX::TYPE::(.*)/Enum\\1/)s", regextarget=1, fullname=1) "^FIX::TYPE::";

// Misunderstanding something here but these constants, declared in the header
// are causing undefined symbol errors when loading the library in python so
// ignoring
%ignore FIX::DateTime::SECONDS_PER_DAY;
%ignore FIX::DateTime::SECONDS_PER_HOUR;
%ignore FIX::DateTime::SECONDS_PER_MIN;
%ignore FIX::DateTime::MINUTES_PER_HOUR;
%ignore FIX::DateTime::NANOS_PER_DAY;
%ignore FIX::DateTime::NANOS_PER_HOUR;
%ignore FIX::DateTime::NANOS_PER_MIN;
%ignore FIX::DateTime::NANOS_PER_SEC;
%ignore FIX::DateTime::JULIAN_19700101;
%ignore FIX::DateTime::getYMD;
%ignore FIX::DateTime::getHMS;

%{
#include <config.h>
#include <Except.h>
#include <Exceptions.h>
#include <FieldTypes.h>
#include <Fields.h>
#include <FixFields.h>
#include <Message.h>
#include <Group.h>
#include <Field.h>
#include <FixCommonFields.h>
#include <Values.h>
#include <SessionID.h>
#include <Dictionary.h>
#include <SessionSettings.h>
#include <Session.h>
#include <Log.h>
#include <FileLog.h>
#include <MessageStore.h>
#include <FileStore.h>
#include <Application.h>
#include <Initiator.h>
#include <SocketInitiator.h>
#include <Acceptor.h>
#include <SocketAcceptor.h>
#include <DataDictionary.h>
#include <SocketMonitor.h>
#include <DatabaseConnectionID.h>
#include <DatabaseConnectionPool.h>
#include <ThreadedSocketAcceptor.h>
#include <ThreadedSocketInitiator.h>
#include <ThreadedSocketConnection.h>
#include <NullStore.h>

#ifdef HAVE_SSL
#include <SSLSocketAcceptor.h>
#include <SSLSocketInitiator.h>
#include <SSLSocketConnection.h>
#include <ThreadedSSLSocketAcceptor.h>
#include <ThreadedSSLSocketInitiator.h>
#else
// The wrappers are generated with HAVE_SSL (swig.sh) and use the SSL transports throughout. This
// included an SSLStubs.h that never existed (#139).
#error "the Python and Ruby wrappers are generated with SSL; build them with HAVE_SSL"
#endif

#ifdef HAVE_MYSQL
#include <MySQLConnection.h>
#include <MySQLStore.h>
#include <MySQLLog.h>
#else
#include <MySQLStubs.h>
#endif

#ifdef HAVE_POSTGRESQL
#include <PostgreSQLConnection.h>
#include <PostgreSQLStore.h>
#include <PostgreSQLLog.h>
#else
#include <PostgreSQLStubs.h>
#endif

#include <functional>
#ifdef SWIGPYTHON
#include "datetime.h"
#endif

#ifdef SWIGPYTHON
template<typename Exception>
void raisePythonException(Exception const& e, swig_type_info* swigType)
{
  SWIG_Python_Raise(SWIG_NewPointerObj((new Exception(static_cast<const Exception&>(e))),swigType,SWIG_POINTER_OWN), typeid(Exception).name(), swigType);
}

bool tryPythonException(std::function<bool()> const& function)
{
  try
  {
    return function();
  }
  catch(FIX::DataDictionaryNotFound const& e)
  {
    raisePythonException<FIX::DataDictionaryNotFound>(e, SWIGTYPE_p_FIX__DataDictionaryNotFound); return false;
  }
  catch(FIX::FieldNotFound const& e)
  {
    raisePythonException<FIX::FieldNotFound>(e, SWIGTYPE_p_FIX__FieldNotFound); return false;
  }
  catch(FIX::FieldConvertError const& e)
  {
    raisePythonException<FIX::FieldConvertError>(e, SWIGTYPE_p_FIX__FieldConvertError); return false;
  }
  catch(FIX::MessageParseError const& e)
  {
    raisePythonException<FIX::MessageParseError>(e, SWIGTYPE_p_FIX__MessageParseError); return false;
  }
  catch(FIX::InvalidMessage const& e)
  {
    raisePythonException<FIX::InvalidMessage>(e, SWIGTYPE_p_FIX__InvalidMessage); return false;
  }
  catch(FIX::ConfigError const& e)
  {
    raisePythonException<FIX::ConfigError>(e, SWIGTYPE_p_FIX__ConfigError); return false;
  }
  catch(FIX::RuntimeError const& e)
  {
    raisePythonException<FIX::RuntimeError>(e, SWIGTYPE_p_FIX__RuntimeError); return false;
  }
  catch(FIX::InvalidTagNumber const& e)
  {
    raisePythonException<FIX::InvalidTagNumber>(e, SWIGTYPE_p_FIX__InvalidTagNumber); return false;
  }
  catch(FIX::RequiredTagMissing const& e)
  {
    raisePythonException<FIX::RequiredTagMissing>(e, SWIGTYPE_p_FIX__RequiredTagMissing); return false;
  }
  catch(FIX::TagNotDefinedForMessage const& e)
  {
    raisePythonException<FIX::TagNotDefinedForMessage>(e, SWIGTYPE_p_FIX__TagNotDefinedForMessage); return false;
  }
  catch(FIX::NoTagValue const& e)
  {
    raisePythonException<FIX::NoTagValue>(e, SWIGTYPE_p_FIX__NoTagValue); return false;
  }
  catch(FIX::IncorrectTagValue const& e)
  {
    raisePythonException<FIX::IncorrectTagValue>(e, SWIGTYPE_p_FIX__IncorrectTagValue); return false;
  }
  catch(FIX::IncorrectDataFormat const& e)
  {
    raisePythonException<FIX::IncorrectDataFormat>(e, SWIGTYPE_p_FIX__IncorrectDataFormat); return false;
  }
  catch(FIX::IncorrectMessageStructure const& e)
  {
    raisePythonException<FIX::IncorrectMessageStructure>(e, SWIGTYPE_p_FIX__IncorrectMessageStructure); return false;
  }
  catch(FIX::DuplicateFieldNumber const& e)
  {
    raisePythonException<FIX::DuplicateFieldNumber>(e, SWIGTYPE_p_FIX__DuplicateFieldNumber); return false;
  }
  catch(FIX::InvalidMessageType const& e)
  {
    raisePythonException<FIX::InvalidMessageType>(e, SWIGTYPE_p_FIX__InvalidMessageType); return false;
  }
  catch(FIX::UnsupportedMessageType const& e)
  {
    raisePythonException<FIX::UnsupportedMessageType>(e, SWIGTYPE_p_FIX__UnsupportedMessageType); return false;
  }
  catch(FIX::UnsupportedVersion const& e)
  {
    raisePythonException<FIX::UnsupportedVersion>(e, SWIGTYPE_p_FIX__UnsupportedVersion); return false;
  }
  catch(FIX::TagOutOfOrder const& e)
  {
    raisePythonException<FIX::TagOutOfOrder>(e, SWIGTYPE_p_FIX__TagOutOfOrder); return false;
  }
  catch(FIX::RepeatedTag const& e)
  {
    raisePythonException<FIX::RepeatedTag>(e, SWIGTYPE_p_FIX__RepeatedTag); return false;
  }
  catch(FIX::RepeatingGroupCountMismatch const& e)
  {
    raisePythonException<FIX::RepeatingGroupCountMismatch>(e, SWIGTYPE_p_FIX__RepeatingGroupCountMismatch); return false;
  }
  catch(FIX::DoNotSend const& e)
  {
    raisePythonException<FIX::DoNotSend>(e, SWIGTYPE_p_FIX__DoNotSend); return false;
  }
  catch(FIX::RejectLogon const& e)
  {
    raisePythonException<FIX::RejectLogon>(e, SWIGTYPE_p_FIX__RejectLogon); return false;
  }
  catch(FIX::SessionNotFound const& e)
  {
    raisePythonException<FIX::SessionNotFound>(e, SWIGTYPE_p_FIX__SessionNotFound); return false;
  }
  catch(FIX::IOException const& e)
  {
    raisePythonException<FIX::IOException>(e, SWIGTYPE_p_FIX__IOException); return false;
  }
  catch(FIX::SocketSendFailed const& e)
  {
    raisePythonException<FIX::SocketSendFailed>(e, SWIGTYPE_p_FIX__SocketSendFailed); return false;
  }
  catch(FIX::SocketRecvFailed const& e)
  {
    raisePythonException<FIX::SocketRecvFailed>(e, SWIGTYPE_p_FIX__SocketRecvFailed); return false;
  }
  catch(FIX::SocketCloseFailed const& e)
  {
    raisePythonException<FIX::SocketCloseFailed>(e, SWIGTYPE_p_FIX__SocketCloseFailed); return false;
  }
  catch(FIX::SocketException const& e)
  {
    raisePythonException<FIX::SocketException>(e, SWIGTYPE_p_FIX__SocketException); return false;
  }
  catch(FIX::Exception const& e)
  {
    raisePythonException<FIX::Exception>(e, SWIGTYPE_p_FIX__Exception); return false;
  }
  catch(std::exception const& e) 
  {
    SWIG_Error(SWIG_RuntimeError, e.what()); return false;
  }
  catch(...)
  {
    SWIG_Error(SWIG_RuntimeError, "unknown exception"); return false;
  }
}
#endif

#ifdef SWIGRUBY
namespace QuickfixViews
{
void redeliverError();
}

// Ruby raises by longjmp, which runs no destructor and completes no handler, so a caught C++ exception is raised into
// Ruby from the method's wrapper, outside every handler: tryRubyException's handlers park what they caught in a
// QuickfixCaught of the wrapper, and once tryRubyException has returned, and the call's temporaries with it, the
// wrapper frees what SWIG made for its arguments and raises with raiseCaught (ruby/quickfix.i). QuickfixCaught holds a
// copy of a FIX exception, as its own class, or the text of anything else, cut to fit; it is plain storage, which a
// longjmp leaves nothing to destroy in.
struct QuickfixCaught
{
  bool caught = false;
  void *copy = nullptr;
  swig_type_info *type = nullptr;
  char text[512];
};

void parkText(QuickfixCaught& caught, const char* text)
{
  size_t length = strnlen(text, sizeof(caught.text) - 1);
  memcpy(caught.text, text, length);
  caught.text[length] = '\0';
  caught.copy = nullptr;
  caught.caught = true;
}

template<typename Exception>
void park(QuickfixCaught& caught, Exception const& e, swig_type_info* swigType)
{
  try
  {
    caught.copy = new Exception(e);
    caught.type = swigType;
    caught.caught = true;
  }
  catch(...)
  {
    parkText(caught, e.what());
  }
}

// Calls function, and returns what it returns, or Qnil with what it threw parked in caught.
VALUE tryRubyException(std::function<VALUE()> const& function, QuickfixCaught& caught)
{
  try
  {
    return function();
  }
  catch(FIX::DataDictionaryNotFound const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__DataDictionaryNotFound);
  }
  catch(FIX::FieldNotFound const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__FieldNotFound);
  }
  catch(FIX::FieldConvertError const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__FieldConvertError);
  }
  catch(FIX::MessageParseError const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__MessageParseError);
  }
  catch(FIX::InvalidMessage const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__InvalidMessage);
  }
  catch(FIX::ConfigError const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__ConfigError);
  }
  catch(FIX::RuntimeError const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__RuntimeError);
  }
  catch(FIX::InvalidTagNumber const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__InvalidTagNumber);
  }
  catch(FIX::RequiredTagMissing const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__RequiredTagMissing);
  }
  catch(FIX::TagNotDefinedForMessage const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__TagNotDefinedForMessage);
  }
  catch(FIX::NoTagValue const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__NoTagValue);
  }
  catch(FIX::IncorrectTagValue const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__IncorrectTagValue);
  }
  catch(FIX::IncorrectDataFormat const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__IncorrectDataFormat);
  }
  catch(FIX::IncorrectMessageStructure const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__IncorrectMessageStructure);
  }
  catch(FIX::DuplicateFieldNumber const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__DuplicateFieldNumber);
  }
  catch(FIX::InvalidMessageType const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__InvalidMessageType);
  }
  catch(FIX::UnsupportedMessageType const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__UnsupportedMessageType);
  }
  catch(FIX::UnsupportedVersion const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__UnsupportedVersion);
  }
  catch(FIX::TagOutOfOrder const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__TagOutOfOrder);
  }
  catch(FIX::RepeatedTag const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__RepeatedTag);
  }
  catch(FIX::RepeatingGroupCountMismatch const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__RepeatingGroupCountMismatch);
  }
  catch(FIX::DoNotSend const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__DoNotSend);
  }
  catch(FIX::RejectLogon const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__RejectLogon);
  }
  catch(FIX::SessionNotFound const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__SessionNotFound);
  }
  catch(FIX::IOException const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__IOException);
  }
  catch(FIX::SocketSendFailed const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__SocketSendFailed);
  }
  catch(FIX::SocketRecvFailed const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__SocketRecvFailed);
  }
  catch(FIX::SocketCloseFailed const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__SocketCloseFailed);
  }
  catch(FIX::SocketException const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__SocketException);
  }
  catch(FIX::Exception const& e)
  {
    park(caught, e, SWIGTYPE_p_FIX__Exception);
  }
  catch(std::exception const& e) 
  {
    parkText(caught, e.what());
  }
  catch(...)
  {
    parkText(caught, "unknown exception");
  }
  return Qnil;
}

// Raises what tryRubyException parked in caught, if anything, from the method's wrapper. A kill that landed in a
// callback outranks it (#147), and so does an exception that landed while a call ended views (#196;
// ruby/quickfix.i): the copy is Ruby's before either goes on, so the GC frees it then.
void raiseCaught(QuickfixCaught& caught)
{
  if(!caught.caught)
    return;
  caught.caught = false;
  void* copy = caught.copy;
  caught.copy = nullptr;
  VALUE error =
    copy ? SWIG_Ruby_ExceptionType(caught.type, SWIG_NewPointerObj(copy, caught.type, SWIG_POINTER_OWN)) : Qnil;
  redeliverKill();
  QuickfixViews::redeliverError();
  if(copy)
    rb_exc_raise(error);
  SWIG_Error(SWIG_RuntimeError, caught.text);
}
#endif
         
// Every SessionID the Python and Ruby bindings hand their language -- a callback's argument, or
// Session::getSessionID()'s result -- is the language's own copy, never a view of the engine's: a session's ID is
// freed with its transport, and a view kept past that reads freed memory (#190, #193). The typemaps that call this
// are below. Java shares one copy per ID instead (QuickfixJava::share in java/quickfix.i).
namespace QuickfixBinding
{
inline FIX::SessionID *handOver(const FIX::SessionID &sessionID) { return new FIX::SessionID(sessionID); }
}

typedef FIX::UtcTimeStamp UtcTimeStamp;
typedef FIX::UtcDate UtcDate;
typedef FIX::UtcTimeOnly UtcTimeOnly;
typedef FIX::message_order message_order;
typedef FIX::Group Group;
typedef FIX::DataDictionary DataDictionary;
typedef FIX::DataDictionaryProvider DataDictionaryProvider;
typedef FIX::MsgType MsgType;
typedef FIX::BeginString BeginString;
typedef FIX::ApplVerID ApplVerID;
typedef FIX::SessionID SessionID;
typedef FIX::Session Session;
typedef FIX::Application Application;
typedef FIX::DateTime DateTime;
typedef FIX::TimeRange TimeRange;
typedef FIX::Responder Responder;
typedef FIX::Log Log;
typedef FIX::LogFactory LogFactory;
typedef FIX::MessageStore MessageStore;
typedef FIX::MessageStoreFactory MessageStoreFactory;
typedef FIX::Mutex Mutex;
typedef FIX::DOMDocumentPtr DOMDocumentPtr;
typedef FIX::SessionSettings SessionSettings;
%}

%typedef DoubleField PriceField;
%typedef DoubleField AmtField;
%typedef DoubleField QtyField;
%typedef StringField CurrencyField;
%typedef StringField MultipleValueStringField;
%typedef StringField MultipleStringValueField;
%typedef StringField MultipleCharValueField;
%typedef StringField ExchangeField;
%typedef StringField LocalMktTimeField;
%typedef StringField LocalMktDateField;
%typedef StringField DataField;
%typedef DoubleField FloatField;
%typedef DoubleField PriceOffsetField;
%typedef StringField MonthField;
%typedef StringField MonthYearField;
%typedef StringField DayOfMonthField;
%typedef UtcDateField UtcDateOnlyField;
%typedef IntField LengthField;
%typedef IntField NumInGroupField;
%typedef IntField SeqNumField;
%typedef IntField TagNumField;
%typedef DoubleField PercentageField;
%typedef StringField CountryField;
%typedef StringField TzTimeOnlyField;
%typedef StringField TzTimeStampField;
%typedef StringField LanguageField;
%typedef StringField QidField;
%typedef StringField QidRefField;

%typedef std::string FIX::STRING;
%typedef char FIX::CHAR;
%typedef double FIX::PRICE;
%typedef int FIX::INT;
%typedef double FIX::AMT;
%typedef double FIX::QTY;
%typedef std::string FIX::CURRENCY;
%typedef std::string FIX::MULTIPLEVALUESTRING;
%typedef std::string FIX::MULTIPLESTRINGVALUE;
%typedef std::string FIX::MULTIPLECHARVALUE;
%typedef std::string FIX::EXCHANGE;
%typedef UtcTimeStamp FIX::UTCTIMESTAMP;
%typedef bool FIX::BOOLEAN;
%typedef std::string FIX::LOCALMKTTIME;
%typedef std::string FIX::LOCALMKTDATE;
%typedef std::string FIX::DATA;
%typedef double FIX::FLOAT;
%typedef double FIX::PRICEOFFSET;
%typedef std::string FIX::MONTHYEAR;
%typedef std::string FIX::DAYOFMONTH;
%typedef UtcDate FIX::UTCDATE;
%typedef UtcDateOnly FIX::UTCDATEONLY;
%typedef UtcTimeOnly FIX::UTCTIMEONLY;
%typedef int FIX::NUMINGROUP;
%typedef double FIX::PERCENTAGE;
%typedef int FIX::SEQNUM;
%typedef int FIX::TAGNUM;
%typedef int FIX::LENGTH;
%typedef std::string FIX::COUNTRY;
%typedef std::string FIX::TZTIMEONLY;
%typedef std::string FIX::TZTIMESTAMP;
%typedef std::string FIX::LANGUAGE;
%typedef std::string FIX::QID;
%typedef std::string FIX::QIDREF;
%template(SessionIDSet) std::set<FIX::SessionID>;

%extend FIX::Exception {
  std::string __str__() {
    return self->what();
  }
}

%extend FIX::Message {
  std::string __str__() {
    return self->toString();
  }
}

%extend FIX::SessionID {
  std::string __str__() {
    return self->toString();
  }
#ifdef SWIGPYTHON
  // Equal by value, as the engine compares (#193): == and hash by the session's string. SWIG names this __eq__
  // and returns NotImplemented for an object of another type, so it compares unequal to one rather than raising.
  bool operator==(const FIX::SessionID &other) const {
    return *self == other;
  }
  size_t __hash__() const {
    return std::hash<std::string>()(self->toStringFrozen());
  }
#endif
}

// A SessionID handed to Python or Ruby is its own copy, freed with its proxy (QuickfixBinding::handOver).
#if defined(SWIGPYTHON) || defined(SWIGRUBY)
%typemap(directorin, noblock=1) const FIX::SessionID & {
  $input = SWIG_NewPointerObj(SWIG_as_voidptr(QuickfixBinding::handOver($1)), $descriptor(FIX::SessionID *), SWIG_POINTER_OWN);
}
%typemap(out, noblock=1) const FIX::SessionID & {
  $result = SWIG_NewPointerObj(SWIG_as_voidptr(QuickfixBinding::handOver(*$1)), $descriptor(FIX::SessionID *), SWIG_POINTER_OWN);
}
#endif

// What a call returns from inside another object (#194). SWIG returns a reference or pointer as a proxy that owns
// nothing and keeps nothing alive, so one kept past its owner -- which Python frees when its last reference goes,
// Ruby at a GC sweep and Java in a finalizer -- read and wrote freed memory. Such a call now returns one of:
// - a copy, where C++ returns const: getFieldRef, getFieldPtr and SessionID's getters (below; java/quickfix.i).
//   A field's view was also moved by any setField that grew its message.
// - the view, holding its owner, where an application writes through it: getHeader, getTrailer, getGroupRef,
//   getGroupPtr and SessionSettings.get (python/, ruby/ and java/quickfix.i). A group's view ends when a call
//   through the binding destroys its group, and raises if it is used after (#196, below).
// - the argument itself, where the result is the argument: getField(field) and getGroup(..., group), likewise.
// A Session, and what a transport or a session returns of itself, ends with its transport (#197, below).
// These return an engine internal, or an opaque type nothing in a binding can use, so they are not wrapped:
%ignore FIX::FieldMap::groups;
%ignore FIX::FieldMap::cloneInto;
%ignore FIX::Group::cloneInto;
%ignore FIX::GroupArena;
%ignore FIX::DataDictionary::getOrderedFields;
%ignore FIX::DataDictionary::getHeaderOrderedFields;
%ignore FIX::DataDictionary::getTrailerOrderedFields;
%ignore FIX::DataDictionary::getMessageOrderedFields;
%ignore FIX::SocketAcceptor::sessionToPort;
// addGroupPtr hands its receiver the group to own and free (#198). No binding can honour that: a group the script
// made is freed by its proxy too, and a group view by the message it is in. addGroup copies, and is what a binding
// needs.
%ignore FIX::FieldMap::addGroupPtr;
// A move constructor would take the groups from under its argument's views (#196). SWIG already shadows both with
// the copy constructor (warnings 509 and 516), so neither was reachable; now neither is wrapped.
%ignore FIX::FieldMap::FieldMap(FieldMap &&);
%ignore FIX::Message::Message(Message &&);

// A group's view (getGroupRef, getGroupPtr) points into its message, and the binding's own calls destroy what it
// points at: removeGroup, clear, setString and the rest below (#196). Each binding keeps the group views it hands
// out, by the address of their group (QuickfixViews in python/, ruby/ and java/quickfix.i), and each of these calls
// first ends the views of what it is about to destroy: their pointers become null, so a later use raises instead of
// reaching freed memory (the null-proxy check above each binding's %include). In Java, whose delete() frees an
// object, it also ends the views of what that frees. These walks say what each call destroys. While no group view
// is registered, a call pays one check.
%{
#include <algorithm>
#include <vector>
namespace QuickfixViews
{
typedef std::vector<const void *> Addresses;

// Each binding's own: whether any group view is registered, and ending the views registered at these addresses.
// endViews() leaves the language's pending error, if there is one, as it found it. (Not end(): an unqualified call
// with a std::vector finds std::end too, and std::end(Container &) binds a non-const vector more tightly.)
bool anyGroupViews();
void endViews(const Addresses &addresses);

// Every group under map, at any depth; not map itself.
inline void addGroupsUnder(const FIX::FieldMap &map, Addresses &addresses)
{
  for (FIX::FieldMap::g_const_iterator tag = map.g_begin(); tag != map.g_end(); ++tag)
  {
    for (const FIX::FieldMap *group : tag->second)
    {
      addresses.push_back(group);
      addGroupsUnder(*group, addresses);
    }
  }
}

// Group num at tag, or null.
inline const FIX::FieldMap *groupAt(const FIX::FieldMap &map, int num, int tag)
{
  if (num <= 0 || map.groupCount(tag) < static_cast<size_t>(num))
    return nullptr;
  return &map.getGroupRef(num, tag);
}

// clear(), and Message's, Header's and Trailer's getGroup(num, group), which clear their argument before they look
// (Message.h): every group under map.
inline void beforeClearing(const FIX::FieldMap &map)
{
  if (!anyGroupViews())
    return;
  Addresses addresses;
  addGroupsUnder(map, addresses);
  endViews(addresses);
}

// Message's clear(), setString() and setStringHeader(), which clear first: the header's, body's and trailer's groups.
inline void beforeClearingMessage(const FIX::Message &message)
{
  if (!anyGroupViews())
    return;
  Addresses addresses;
  addGroupsUnder(message.getHeader(), addresses);
  addGroupsUnder(message, addresses);
  addGroupsUnder(message.getTrailer(), addresses);
  endViews(addresses);
}

// removeGroup(num, tag): group num, and every group under it.
inline void beforeRemovingGroup(const FIX::FieldMap &map, int num, int tag)
{
  if (!anyGroupViews())
    return;
  if (const FIX::FieldMap *group = groupAt(map, num, tag))
  {
    Addresses addresses(1, group);
    addGroupsUnder(*group, addresses);
    endViews(addresses);
  }
}

// removeGroup(tag): every group at tag, and every group under them.
inline void beforeRemovingGroups(const FIX::FieldMap &map, int tag)
{
  if (!anyGroupViews())
    return;
  Addresses addresses;
  for (size_t num = 1, count = map.groupCount(tag); num <= count; ++num)
  {
    const FIX::FieldMap &group = map.getGroupRef(static_cast<int>(num), tag);
    addresses.push_back(&group);
    addGroupsUnder(group, addresses);
  }
  endViews(addresses);
}

// removeField(tag) removes tag's groups with the field, and only when the field is set.
inline void beforeRemovingField(const FIX::FieldMap &map, int tag)
{
  if (map.isSetField(tag))
    beforeRemovingGroups(map, tag);
}

// replaceGroup(num, ...) assigns over group num: the group stays, and the groups under it go.
inline void beforeReplacingGroup(const FIX::FieldMap &map, int num, int tag)
{
  if (!anyGroupViews())
    return;
  if (const FIX::FieldMap *group = groupAt(map, num, tag))
  {
    Addresses addresses;
    addGroupsUnder(*group, addresses);
    endViews(addresses);
  }
}

// FieldMap's and Group's getGroup(num, ..., group) assign group num over group, and throw FieldNotFound first,
// leaving group as it was, when there is none.
inline void beforeGettingGroup(const FIX::FieldMap &map, int num, int tag, const FIX::FieldMap &group)
{
  if (groupAt(map, num, tag))
    beforeClearing(group);
}

// swap(rhs): each map's groups pass to the other, with its arena, so a view would keep the wrong message alive.
inline void beforeSwapping(const FIX::FieldMap &map, const FIX::FieldMap &rhs)
{
  if (!anyGroupViews())
    return;
  Addresses addresses;
  addGroupsUnder(map, addresses);
  addGroupsUnder(rhs, addresses);
  endViews(addresses);
}

// Message's reverseRoute(), and Session's send() and sendToTarget() on their message, remove routing and resend
// fields from its header, and the groups at those tags with them (Message.cpp, Session.cpp). Rather than mirror
// their lists, which would go stale without a word, this compares the header's groups before and after the call,
// also when the call throws. Nothing in the send path removes from the message after toApp or toAdmin returns, so
// deciding before the call that no view needs the comparison holds for a view a callback takes.
class HeaderGroups
{
public:
  explicit HeaderGroups(const FIX::Message &message) : m_message(anyGroupViews() ? &message : nullptr)
  {
    if (m_message)
      addGroupsUnder(message.getHeader(), m_before);
  }
  ~HeaderGroups()
  {
    if (!m_message || m_before.empty())
      return;
    try
    {
      Addresses after;
      addGroupsUnder(m_message->getHeader(), after);
      std::sort(after.begin(), after.end());
      Addresses gone;
      for (const void *address : m_before)
        if (!std::binary_search(after.begin(), after.end(), address))
          gone.push_back(address);
      if (!gone.empty())
        endViews(gone);
    }
    catch (...)
    {
    }
  }
  HeaderGroups(const HeaderGroups &) = delete;
  HeaderGroups &operator=(const HeaderGroups &) = delete;

private:
  const FIX::Message *m_message;
  Addresses m_before;
};
}
%}

// The calls that destroy a group a view may point at, each ending those views first: QUICKFIX_VIEWS_EXCEPTION is
// each binding's exception translation with the code given before $action, after the arguments are converted, so
// an argument that is refused ends nothing. SWIG names the receiver arg1 and the arguments after it arg2 on, and a
// reference argument is a pointer. Every public method of FieldMap, Message, Header, Trailer and Group is classified
// as destroying or not in each binding's view tests, which fail on one that is neither.
QUICKFIX_VIEWS_EXCEPTION(FIX::FieldMap::clear, QuickfixViews::beforeClearing(*arg1);)
QUICKFIX_VIEWS_EXCEPTION(FIX::FieldMap::removeField, QuickfixViews::beforeRemovingField(*arg1, arg2);)
QUICKFIX_VIEWS_EXCEPTION(FIX::FieldMap::removeGroup(int, int), QuickfixViews::beforeRemovingGroup(*arg1, arg2, arg3);)
QUICKFIX_VIEWS_EXCEPTION(FIX::FieldMap::removeGroup(int), QuickfixViews::beforeRemovingGroups(*arg1, arg2);)
QUICKFIX_VIEWS_EXCEPTION(FIX::FieldMap::replaceGroup, QuickfixViews::beforeReplacingGroup(*arg1, arg2, arg3);)
QUICKFIX_VIEWS_EXCEPTION(FIX::FieldMap::getGroup, QuickfixViews::beforeGettingGroup(*arg1, arg2, arg3, *arg4);)
QUICKFIX_VIEWS_EXCEPTION(FIX::FieldMap::swap, QuickfixViews::beforeSwapping(*arg1, *arg2);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Message::clear, QuickfixViews::beforeClearingMessage(*arg1);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Message::setString, QuickfixViews::beforeClearingMessage(*arg1);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Message::setStringHeader, QuickfixViews::beforeClearingMessage(*arg1);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Message::reverseRoute, QuickfixViews::HeaderGroups views(*arg1);)
// A parameter's type is matched as its header spells it: Message.h's FIX::Group, Group.h's Group.
%define QUICKFIX_VIEWS_GROUP_METHODS(Class, GroupType)
QUICKFIX_VIEWS_EXCEPTION(Class::removeGroup(unsigned int, GroupType const &),
                         QuickfixViews::beforeRemovingGroup(*arg1, arg2, arg3->field());)
QUICKFIX_VIEWS_EXCEPTION(Class::removeGroup(GroupType const &), QuickfixViews::beforeRemovingGroups(*arg1, arg2->field());)
QUICKFIX_VIEWS_EXCEPTION(Class::replaceGroup, QuickfixViews::beforeReplacingGroup(*arg1, arg2, arg3->field());)
%enddef
QUICKFIX_VIEWS_GROUP_METHODS(FIX::Message, FIX::Group)
QUICKFIX_VIEWS_GROUP_METHODS(FIX::Header, FIX::Group)
QUICKFIX_VIEWS_GROUP_METHODS(FIX::Trailer, FIX::Group)
QUICKFIX_VIEWS_GROUP_METHODS(FIX::Group, Group)
QUICKFIX_VIEWS_EXCEPTION(FIX::Message::getGroup, QuickfixViews::beforeClearing(*arg3);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Header::getGroup, QuickfixViews::beforeClearing(*arg3);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Trailer::getGroup, QuickfixViews::beforeClearing(*arg3);)
QUICKFIX_VIEWS_EXCEPTION(FIX::Group::getGroup, QuickfixViews::beforeGettingGroup(*arg1, arg2, arg3->field(), *arg3);)
QUICKFIX_VIEWS_SESSION_EXCEPTION(FIX::Session::send, QuickfixViews::HeaderGroups views(*arg2);)
QUICKFIX_VIEWS_SESSION_EXCEPTION(FIX::Session::sendToTarget, QuickfixViews::HeaderGroups views(*arg1);)

// What a transport hands out of itself (#197): a Session (lookupSession, registerSession, getSession), the
// transport's log, and in Java a session's store and log. The transport frees them in its destructor; and when its
// constructor throws, it frees the sessions it had made, of which onCreate can have handed out a view. Each binding
// keeps these views by the address of what they view, apart from the group views above, which a program makes far
// more often and whose checks must stay cheap. It ends those of what a destructor frees, before the destructor and
// again after it, since a script's store or log factory runs inside it; and after a constructor throws, those of
// sessions that no longer exist. A call through an ended view raises (the null-proxy check above each binding's
// %include). In Ruby and Java a view also keeps its transport, so that the collector never frees a transport a
// script is still using through a view -- Ruby's would free it where no Ruby code can run to end the view. Python's
// does not: its cycle collector would free a transport on whatever thread it ran (#111), so a transport there lives
// until its last reference goes. The hooks are in python/ and java/quickfix.i, and Ruby's in quickfix_ruby.rb. A
// transport destroyed while another thread is inside a call through one of its views is still a use after free, as
// in C++.
//
// What a transport was made with -- getApplication, getMessageStoreFactory -- is returned as the script's own
// object, which each binding keeps for the transport's life (#107, #110), and getSessionSettings returns a copy.
%{
namespace QuickfixTransportViews
{
typedef QuickfixViews::Addresses Addresses;

// What a transport's destructor frees that a view can point at: each session, the store and log it hands out, and
// the transport's own log.
template <typename Transport> Addresses addressesOf(Transport &transport)
{
  Addresses addresses;
  for (const FIX::SessionID &sessionID : transport.getSessions())
  {
    FIX::Session *session = transport.getSession(sessionID);
    if (!session)
      continue;
    addresses.push_back(session);
    addresses.push_back(session->getStore());
    addresses.push_back(session->getLog());
  }
  addresses.push_back(transport.getLog());
  return addresses;
}

// The sessions a transport made, whose views keep it in Ruby and Java.
template <typename Transport> Addresses sessionsOf(const Transport &transport)
{
  Addresses sessions;
  for (const FIX::SessionID &sessionID : transport.getSessions())
    if (FIX::Session *session = transport.getSession(sessionID))
      sessions.push_back(session);
  return sessions;
}

// The sessions that exist now, sorted: after a constructor throws, a session view at any other address is a view of
// one it freed.
inline Addresses liveSessions()
{
  Addresses live;
  for (const FIX::SessionID &sessionID : FIX::Session::getSessions())
    if (FIX::Session *session = FIX::Session::lookupSession(sessionID))
      live.push_back(session);
  std::sort(live.begin(), live.end());
  return live;
}
}
%}

#if defined(SWIGPYTHON) || defined(SWIGRUBY)
// A session's store, log, dictionaries, logon times and allowed addresses come back as types these two never wrap,
// opaque objects with no methods that also died with the transport (#197): they are not wrapped. Java's are in
// java/quickfix.i.
%ignore FIX::Session::getStore;
%ignore FIX::Session::getLog;
%ignore FIX::Session::getDataDictionaryProvider;
%ignore FIX::Session::getLogonTime;
%ignore FIX::Session::getAllowedRemoteAddresses;
%typemap(out, noblock=1) const FIX::Dictionary *const getSessionSettings {
  $result = SWIG_NewPointerObj(SWIG_as_voidptr($1 ? new FIX::Dictionary(*$1) : nullptr), $descriptor(FIX::Dictionary *), SWIG_POINTER_OWN);
}
#endif

#if defined(SWIGPYTHON) || defined(SWIGRUBY)
%typemap(out, noblock=1) const FIX::FieldBase &getFieldRef, const FIX::FieldBase *const getFieldPtr {
  $result = SWIG_NewPointerObj(SWIG_as_voidptr(new FIX::FieldBase(*$1)), $descriptor(FIX::FieldBase *), SWIG_POINTER_OWN);
}
%typemap(out, noblock=1) const FIX::BeginString &getBeginString {
  $result = SWIG_NewPointerObj(SWIG_as_voidptr(new FIX::BeginString(*$1)), $descriptor(FIX::BeginString *), SWIG_POINTER_OWN);
}
%typemap(out, noblock=1) const FIX::SenderCompID &getSenderCompID {
  $result = SWIG_NewPointerObj(SWIG_as_voidptr(new FIX::SenderCompID(*$1)), $descriptor(FIX::SenderCompID *), SWIG_POINTER_OWN);
}
%typemap(out, noblock=1) const FIX::TargetCompID &getTargetCompID {
  $result = SWIG_NewPointerObj(SWIG_as_voidptr(new FIX::TargetCompID(*$1)), $descriptor(FIX::TargetCompID *), SWIG_POINTER_OWN);
}
#endif

%extend FIX::FieldBase {
  std::string __str__() {
    return self->getFixString();
  }
}

%init %{
#ifdef SWIGPYTHON
    PyDateTime_IMPORT;
#endif
%}

%extend FIX::SessionSettings {
    void setFromString(const std::string& str) {
        std::stringstream(str) >> (*$self);
    }
}

%include "../C++/Except.h"
%include "../C++/Exceptions.h"
%include "../C++/FieldTypes.h"
%include "../C++/Field.h"
%include "../C++/FieldMap.h"
%include "../C++/Message.h"
%include "../C++/Group.h"
%include "../C++/Field.h"
%include "../C++/FixCommonFields.h"
%include "../C++/Values.h"
%include "../C++/FixValues.h"
%include "../C++/SessionID.h"
%include "../C++/Dictionary.h"
%include "../C++/SessionSettings.h"
// Session's constructor takes a std::move_only_function<UtcTimeStamp()>, which
// SWIG wraps in a SwigValueWrapper that copies -- and a move-only type cannot
// be copied. The constructor is unusable from a scripting language regardless,
// since there is no way to build a move_only_function from Python or Ruby, and
// sessions are created by SessionFactory through the initiator or acceptor
// rather than directly. Everything else on Session stays exposed.
%ignore FIX::Session::Session;
#ifdef SWIGRUBY
QUICKFIX_RUBY_EXCEPTION_WITHOUT_GVL
#endif
%include "../C++/Session.h"
#ifdef SWIGRUBY
QUICKFIX_RUBY_EXCEPTION
#endif
%include "../C++/Log.h"
%include "../C++/FileLog.h"
%include "../C++/MessageStore.h"
%include "../C++/FileStore.h"
%include "../C++/Application.h"
#ifdef SWIGRUBY
QUICKFIX_RUBY_EXCEPTION_WITHOUT_GVL
#endif
%include "../C++/Initiator.h"
#ifdef SWIGRUBY
QUICKFIX_RUBY_EXCEPTION
#endif
%include "../C++/SocketInitiator.h"
#ifdef SWIGRUBY
QUICKFIX_RUBY_EXCEPTION_WITHOUT_GVL
#endif
%include "../C++/Acceptor.h"
#ifdef SWIGRUBY
QUICKFIX_RUBY_EXCEPTION
#endif
%include "../C++/SocketAcceptor.h"
%include "../C++/DataDictionary.h"
%include "../C++/SocketMonitor.h"
%include "../C++/SSLSocketAcceptor.h"
%include "../C++/SSLSocketInitiator.h"
%include "../C++/DatabaseConnectionID.h"
%include "../C++/DatabaseConnectionPool.h"
%include "../C++/MySQLConnection.h"
%include "../C++/MySQLStore.h"
%include "../C++/MySQLLog.h"
%include "../C++/PostgreSQLConnection.h"
%include "../C++/PostgreSQLStore.h"
%include "../C++/PostgreSQLLog.h"
%include "../C++/ThreadedSocketAcceptor.h"
%include "../C++/ThreadedSocketInitiator.h"
%include "../C++/ThreadedSSLSocketAcceptor.h"
%include "../C++/ThreadedSSLSocketInitiator.h"
%include "../C++/NullStore.h"