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

#include "config.h"

#include "Exceptions.h"
#include "PortSetting.h"
#include "Session.h"
#include "Settings.h"
#include "SocketAcceptor.h"
#include "Utility.h"

namespace FIX
{
SocketAcceptor::SocketAcceptor(Application &application, MessageStoreFactory &factory, const SessionSettings &settings)
    EXCEPT(ConfigError)
    : Acceptor(application, factory, settings), m_pServer(0), m_reactorActive(false), m_lastPreSessionSweep(0)
{
}

SocketAcceptor::SocketAcceptor(Application &application, MessageStoreFactory &factory, const SessionSettings &settings,
                               LogFactory &logFactory) EXCEPT(ConfigError)
    : Acceptor(application, factory, settings, logFactory), m_pServer(0), m_reactorActive(false),
      m_lastPreSessionSweep(0)
{
}

SocketAcceptor::~SocketAcceptor()
{
    disconnectRemaining();

    // onStart() owns m_pServer on the blocking path and nulls it there, but the
    // poll() path never runs onStart(), so without this the server leaks.  Doing
    // it here rather than in onStop() avoids racing a blocking onStart() that may
    // still be using it when stop() is called from another thread.
    delete m_pServer;
    m_pServer = 0;
}

void SocketAcceptor::onConfigure(const SessionSettings &sessionSettings) EXCEPT(ConfigError)
{
    for (const SessionID &sessionID : sessionSettings.getSessions())
    {
        const Dictionary &settings = sessionSettings.get(sessionID);
        getPortSetting(settings, SOCKET_ACCEPT_PORT, true);
        if (settings.has(SOCKET_REUSE_ADDRESS))
        {
            settings.getBool(SOCKET_REUSE_ADDRESS);
        }
        if (settings.has(SOCKET_NODELAY))
        {
            settings.getBool(SOCKET_NODELAY);
        }
    }
}

void SocketAcceptor::onInitialize(const SessionSettings &sessionSettings) EXCEPT(RuntimeError)
{
    uint16_t port = 0;

    try
    {
        m_pServer = new SocketServer(1);

        for (const SessionID &sessionID : sessionSettings.getSessions())
        {
            const Dictionary &settings = sessionSettings.get(sessionID);
            port = getPortSetting(settings, SOCKET_ACCEPT_PORT, true);

            const bool reuseAddress =
                settings.has(SOCKET_REUSE_ADDRESS) ? settings.getBool(SOCKET_REUSE_ADDRESS) : true;

            const bool noDelay = settings.has(SOCKET_NODELAY) ? settings.getBool(SOCKET_NODELAY) : false;

            const int sendBufSize =
                settings.has(SOCKET_SEND_BUFFER_SIZE) ? settings.getInt(SOCKET_SEND_BUFFER_SIZE) : 0;

            const int rcvBufSize =
                settings.has(SOCKET_RECEIVE_BUFFER_SIZE) ? settings.getInt(SOCKET_RECEIVE_BUFFER_SIZE) : 0;

            socket_handle acceptSocket = m_pServer->add(port, reuseAddress, noDelay, sendBufSize, rcvBufSize);
            m_portToSessions[socket_hostport(acceptSocket)].insert(sessionID);
            m_sessionToPort[sessionID] = socket_hostport(acceptSocket);
        }
    }
    catch (SocketException &e)
    {
        delete m_pServer;
        m_pServer = 0;
        throw RuntimeError("Unable to create, bind, or listen to port " + IntConvertor::convert(port) + " (" +
                           e.what() + ")");
    }
}

void SocketAcceptor::onStart()
{
    {
        Locker l(m_serverMutex);
        m_reactorActive = true;
    }

    while (!isStopped() && m_pServer && m_pServer->block(*this))
    {
        expirePendingReads();
    }

    if (!m_pServer)
    {
        Locker l(m_serverMutex);
        m_reactorActive = false;
        return;
    }

    time_t start = 0;
    time_t now = 0;

    ::time(&start);
    while (isLoggedOn())
    {
        m_pServer->block(*this);
        if (::time(&now) - 5 >= start)
        {
            break;
        }
    }

    disconnectRemaining();

    Locker l(m_serverMutex);
    m_pServer->close();
    delete m_pServer;
    m_pServer = 0;
    m_reactorActive = false;
}

void SocketAcceptor::disconnectRemaining()
{
    // A session whose peer did not answer its Logout within the wait above is still logged on,
    // with this connection as its responder. Deleting the server deletes the monitor the
    // connection signals on send(), so a later send -- from any thread -- would use freed memory
    // (#49). Disconnecting first drops the responder, and the connection is then deleted here.
    for (const SocketConnections::value_type &entry : m_connections)
    {
        if (Session *pSession = entry.second->getSession())
        {
            pSession->disconnect();
        }
        delete entry.second;
    }
    m_connections.clear();
}

bool SocketAcceptor::onPoll()
{
    if (!m_pServer)
    {
        return false;
    }

    time_t start = 0;
    time_t now = 0;

    if (isStopped())
    {
        if (start == 0)
        {
            ::time(&start);
        }
        if (!isLoggedOn())
        {
            start = 0;
            return false;
        }
        if (::time(&now) - 5 >= start)
        {
            start = 0;
            return false;
        }
    }

    m_pServer->block(*this, true);
    expirePendingReads();
    return true;
}

size_t SocketAcceptor::pendingConnections() const
{
    size_t pending = 0;
    for (const SocketConnections::value_type &entry : m_connections)
    {
        Session *pSession = entry.second->getSession();
        if (pSession == 0 || !pSession->receivedLogon())
        {
            ++pending;
        }
    }
    return pending;
}

void SocketAcceptor::expirePendingReads()
{
    if (!m_pServer || m_connections.empty())
    {
        return;
    }

    const time_t now = ::time(0);
    if (now == m_lastPreSessionSweep)
    {
        return;
    }
    m_lastPreSessionSweep = now;

    for (const SocketConnections::value_type &entry : m_connections)
    {
        // A connection is past setup once its session has received a logon. One
        // that has bound a session without logging on -- its first messages drew
        // only rejects -- would otherwise hold that session until the peer left,
        // since a session not logged on has no timer of its own on an acceptor.
        // Dropping is deferred to the monitor, which reports it on the next
        // block() and closes the socket once (#26); m_connections is not mutated
        // here, so iterating it is safe.
        Session *pSession = entry.second->getSession();
        if ((pSession == 0 || !pSession->receivedLogon()) && entry.second->getSecondsFromSetupStart(now) > 10)
        {
            if (getLog())
            {
                getLog()->onEvent(pSession ? "Timed out a connection that did not log on"
                                           : "Timed out a connection that sent no complete message");
            }
            m_pServer->getMonitor().drop(entry.first);
        }
    }
}

void SocketAcceptor::onStop()
{
    Locker l(m_serverMutex);
    if (!m_pServer)
    {
        return;
    }

    if (m_reactorActive)
    {
        // The reactor loop owns the server: wake it, and it sees isStopped() and
        // closes the server itself. Closing it from this thread as well closed
        // each listener up to three times, and could close whatever another
        // thread had been given the number in between (#29). signal() only
        // writes to the monitor's own wakeup socket, and for a socket the
        // monitor does not hold it does nothing else.
        m_pServer->getMonitor().signal(INVALID_SOCKET_HANDLE);
    }
    else
    {
        // No reactor loop -- the poll() path -- so the server is closed here.
        m_pServer->close();
    }
}

void SocketAcceptor::onConnect(SocketServer &server, socket_handle a, socket_handle s)
{
    if (!socket_isValid(s))
    {
        return;
    }
    SocketConnections::iterator i = m_connections.find(s);
    if (i != m_connections.end())
    {
        return;
    }
    // MaxPendingConnections: past the limit a new connection is refused at once,
    // so connections that never log on cannot exhaust the process's descriptors.
    // Logged-on sessions do not count against it.
    if (getMaxPendingConnections() > 0 && pendingConnections() >= static_cast<size_t>(getMaxPendingConnections()))
    {
        if (getLog())
        {
            getLog()->onEvent("Refused a connection from " + std::string(socket_peername(s)) + ": " +
                              std::to_string(getMaxPendingConnections()) + " connections are waiting to log on");
        }
        server.getMonitor().drop(s);
        return;
    }
    uint16_t port = server.socketToPort(a);
    Sessions sessions = m_portToSessions[port];
    m_connections[s] = new SocketConnection(s, sessions, &server.getMonitor());

    std::stringstream stream;
    stream << "Accepted connection from " << socket_peername(s) << " on port " << port;

    if (getLog())
    {
        getLog()->onEvent(stream.str());
    }
}

void SocketAcceptor::onWrite(SocketServer &server, socket_handle s)
{
    SocketConnections::iterator i = m_connections.find(s);
    if (i == m_connections.end())
    {
        return;
    }
    SocketConnection *pSocketConnection = i->second;
    if (pSocketConnection->processQueue())
    {
        pSocketConnection->unsignal();
    }
}

bool SocketAcceptor::onData(SocketServer &server, socket_handle s)
{
    SocketConnections::iterator i = m_connections.find(s);
    if (i == m_connections.end())
    {
        return false;
    }
    SocketConnection *pSocketConnection = i->second;
    return pSocketConnection->read(*this, server);
}

void SocketAcceptor::onDisconnect(SocketServer &, socket_handle s)
{
    SocketConnections::iterator i = m_connections.find(s);
    if (i == m_connections.end())
    {
        return;
    }
    SocketConnection *pSocketConnection = i->second;

    Session *pSession = pSocketConnection->getSession();
    if (pSession)
    {
        pSession->disconnect();
    }

    delete pSocketConnection;
    m_connections.erase(s);
}

void SocketAcceptor::onError(SocketServer &) {}

void SocketAcceptor::onTimeout(SocketServer &)
{
    SocketConnections::iterator i;
    for (i = m_connections.begin(); i != m_connections.end(); ++i)
    {
        i->second->onTimeout();
    }
}
} // namespace FIX
