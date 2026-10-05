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

#ifndef FIX_SOCKETACCEPTOR_H
#define FIX_SOCKETACCEPTOR_H

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#endif

#include "Acceptor.h"
#include "Mutex.h"
#include "SocketConnection.h"
#include "SocketServer.h"

namespace FIX
{
/// Socket implementation of Acceptor.
class SocketAcceptor : public Acceptor, SocketServer::Strategy
{
    friend class SocketConnection;

public:
    typedef std::map<SessionID, uint16_t> SessionToPort;

    SocketAcceptor(Application &, MessageStoreFactory &, const SessionSettings &) EXCEPT(ConfigError);
    SocketAcceptor(Application &, MessageStoreFactory &, const SessionSettings &, LogFactory &) EXCEPT(ConfigError);

    virtual ~SocketAcceptor();

    const SessionToPort &sessionToPort() { return m_sessionToPort; }

private:
    bool readSettings(const SessionSettings &);

    typedef std::set<SessionID> Sessions;
    typedef std::map<uint16_t, Sessions> PortToSessions;
    typedef std::map<socket_handle, SocketConnection *> SocketConnections;

    void onConfigure(const SessionSettings &) EXCEPT(ConfigError);
    void onInitialize(const SessionSettings &) EXCEPT(RuntimeError);

    void onStart();
    bool onPoll();
    void onStop();

    void onConnect(SocketServer &, socket_handle, socket_handle);
    void onWrite(SocketServer &, socket_handle);
    bool onData(SocketServer &, socket_handle);
    void onDisconnect(SocketServer &, socket_handle);
    void onError(SocketServer &);
    void onTimeout(SocketServer &);

    // Drops a connection that has not produced its first message within the
    // setup deadline, so a peer that stalls its first message does not hold a
    // slot open indefinitely.
    void expirePendingReads();
    /// Disconnect every remaining connection's session through the normal path, so it drops the
    /// connection as its responder, then delete the connections. Must run while the server and
    /// its monitor still exist.
    void disconnectRemaining();
    /// Accepted connections whose session has not received a logon.
    size_t pendingConnections() const;

    SocketServer *m_pServer;
    // Guards the hand-over of m_pServer between a reactor loop (onStart) and
    // stop() on another thread: while the loop runs it owns the server, and
    // onStop only wakes it (#29).
    Mutex m_serverMutex;
    bool m_reactorActive;
    time_t m_lastPreSessionSweep;
    PortToSessions m_portToSessions;
    SessionToPort m_sessionToPort;
    SocketConnections m_connections;
};
/*! @} */
} // namespace FIX

#endif // FIX_SOCKETACCEPTOR_H
