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
 * you must synchronize those resources; SynchronizedApplication does it for all of them.
 * A Session calls none of these methods while it holds a lock of its own, so a lock the
 * application takes in them may also be held while it sends (#73).
 *
 * toApp and toAdmin run before the Session sequences the message, so it carries no
 * MsgSeqNum yet -- except a gap-fill SequenceReset, whose number is fixed in advance, and
 * a resent message, which keeps its original number.
 *
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
 * Takes another Application and serialises all of its callbacks under one recursive mutex, so
 * only one thread runs the application's code at a time. A callback may send, and so may a
 * thread holding m_mutex: the Session calls back without holding its own lock (#73), which 20.x
 * did not, and this class was deprecated there for the deadlock that caused (#45).
 */
class SynchronizedApplication : public Application
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
