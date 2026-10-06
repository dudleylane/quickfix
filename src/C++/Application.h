/* -*- C++ -*- */

/****************************************************************************
** Copyright (c) 2001-2014
**
** This file is part of the QuickFIX FIX Engine
**
** This file may be distributed under the terms of the quickfixengine.org
** license as defined by quickfixengine.org and appearing in the file
** LICENSE included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.quickfixengine.org/LICENSE for licensing information.
**
** Contact ask@quickfixengine.org if any conditions of this licensing are
** not clear to you.
**
****************************************************************************/

#ifndef FIX_APPLICATION_H
#define FIX_APPLICATION_H

#include "Message.h"
#include "Mutex.h"
#include "SessionID.h"

namespace FIX
{
/**
 * This interface must be implemented to define what your %FIX application
 * does.
 *
 * These methods notify your application about events that happen on
 * active %FIX sessions. There is no guarantee how many threads will be calling
 * these functions. If the application is sharing resources among multiple sessions,
 * you must synchronize those resources -- with locks of your own that you never hold
 * while calling into a Session (sending, logging on or off, resetting): a Session holds
 * its own lock while it calls toApp, toAdmin and onLogout, so taking yours in the other
 * order deadlocks. SynchronizedApplication does exactly that and is deprecated (#45).
 * The various MessageCracker classes can be used to parse the generic message
 * structure into specific %FIX messages.
 */
class Application
{
public:
    virtual ~Application() {};
    /// Notification of a session begin created
    virtual void onCreate(const SessionID &) = 0;
    /// Notification of a session successfully logging on
    virtual void onLogon(const SessionID &) = 0;
    /// Notification of a session logging off or disconnecting
    virtual void onLogout(const SessionID &) = 0;
    /// Notification of admin message being sent to target
    virtual void toAdmin(Message &, const SessionID &) = 0;
    /// Notification of app message being sent to target
    virtual void toApp(Message &, const SessionID &) EXCEPT(DoNotSend) = 0;
    /// Notification of admin message being received from target
    virtual void fromAdmin(const Message &, const SessionID &)
        EXCEPT(FieldNotFound, IncorrectDataFormat, IncorrectTagValue, RejectLogon) = 0;
    /// Notification of app message being received from target
    virtual void fromApp(const Message &, const SessionID &)
        EXCEPT(FieldNotFound, IncorrectDataFormat, IncorrectTagValue, UnsupportedMessageType) = 0;
};

/**
 * Takes another Application and serialises all of its callbacks under one mutex, so only
 * one thread runs the application's code at a time.
 *
 * Deprecated: it deadlocks an application that sends from fromApp or fromAdmin, or from a
 * thread of its own. A Session holds its own lock while it calls toApp, toAdmin and onLogout,
 * which this class then locks; a send made while this class's lock is held takes the two in
 * the opposite order (#45). Synchronise the application's own state instead, without holding
 * that lock across calls into a Session.
 */
class [[deprecated("SynchronizedApplication deadlocks an application that sends from a callback or its own "
                   "thread; synchronise the application's own state instead (see Application.h)")]]
SynchronizedApplication : public Application
{
public:
    SynchronizedApplication(Application &app) : m_app(app) {}

    void onCreate(const SessionID &sessionID)
    {
        Locker l(m_mutex);
        app().onCreate(sessionID);
    }
    void onLogon(const SessionID &sessionID)
    {
        Locker l(m_mutex);
        app().onLogon(sessionID);
    }
    void onLogout(const SessionID &sessionID)
    {
        Locker l(m_mutex);
        app().onLogout(sessionID);
    }
    void toAdmin(Message &message, const SessionID &sessionID)
    {
        Locker l(m_mutex);
        app().toAdmin(message, sessionID);
    }
    void toApp(Message &message, const SessionID &sessionID) EXCEPT(DoNotSend)
    {
        Locker l(m_mutex);
        app().toApp(message, sessionID);
    }
    void fromAdmin(const Message &message, const SessionID &sessionID)
        EXCEPT(FieldNotFound, IncorrectDataFormat, IncorrectTagValue, RejectLogon)
    {
        Locker l(m_mutex);
        app().fromAdmin(message, sessionID);
    }
    void fromApp(const Message &message, const SessionID &sessionID)
        EXCEPT(FieldNotFound, IncorrectDataFormat, IncorrectTagValue, UnsupportedMessageType)
    {
        Locker l(m_mutex);
        app().fromApp(message, sessionID);
    }

    Mutex m_mutex;

    Application &app() { return m_app; }
    Application &m_app;
};

/**
 * An empty implementation of an Application. This can be used if you
 * do not want to provide an implementation for all the callback methods.
 * It is also useful for unit tests where the callback
 * values of some or all methods are not of interest.
 */
class NullApplication : public Application
{
    void onCreate(const SessionID &) {}
    void onLogon(const SessionID &) {}
    void onLogout(const SessionID &) {}
    void toAdmin(Message &, const SessionID &) {}
    void toApp(Message &, const SessionID &) EXCEPT(DoNotSend) {}
    void fromAdmin(const Message &, const SessionID &)
        EXCEPT(FieldNotFound, IncorrectDataFormat, IncorrectTagValue, RejectLogon)
    {
    }
    void fromApp(const Message &, const SessionID &)
        EXCEPT(FieldNotFound, IncorrectDataFormat, IncorrectTagValue, UnsupportedMessageType)
    {
    }
};
/*! @} */
} // namespace FIX

#endif // FIX_APPLICATION_H
