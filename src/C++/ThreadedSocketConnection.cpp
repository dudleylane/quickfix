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

#include <cerrno>
#include <poll.h>

#include "Session.h"
#include "ThreadedSocketAcceptor.h"
#include "ThreadedSocketConnection.h"
#include "ThreadedSocketInitiator.h"
#include "Utility.h"

namespace FIX
{
ThreadedSocketConnection::ThreadedSocketConnection(socket_handle s, Sessions sessions, Log *pLog)
    : m_socket(s), m_pLog(pLog), m_sessions(sessions), m_pSession(0), m_disconnect(false),
      m_setupDeadline(::time(0) + SETUP_SECONDS)
{
#if _MSC_VER
    FD_ZERO(&m_fds);
    FD_SET(m_socket, &m_fds);
#endif
}

ThreadedSocketConnection::ThreadedSocketConnection(const SessionID &sessionID, socket_handle s,
                                                   const std::string &address, short port, Log *pLog,
                                                   const std::string &sourceAddress, short sourcePort)
    : m_socket(s), m_address(address), m_port(port), m_sourceAddress(sourceAddress), m_sourcePort(sourcePort),
      m_pLog(pLog), m_pSession(Session::lookupSession(sessionID)), m_disconnect(false)
{
#if _MSC_VER
    FD_ZERO(&m_fds);
    FD_SET(m_socket, &m_fds);
#endif
    if (m_pSession)
    {
        m_pSession->setResponder(this);
    }
}

ThreadedSocketConnection::~ThreadedSocketConnection()
{
    if (m_pSession)
    {
        m_pSession->setResponder(0);
        Session::unregisterSession(m_pSession->getSessionID());
    }
}

bool ThreadedSocketConnection::send(const std::string &msg)
{
    size_t totalSent = 0;
    while (totalSent < msg.length())
    {
        // A signal can interrupt a blocking send() after part of the message has gone, and
        // send() then returns a short count: continue with what remains, not the whole length
        // again from the advanced position, which read past the message onto the wire. One
        // interrupted before anything went returns EINTR and is simply retried.
        const ssize_t sent = socket_send(m_socket, msg.c_str() + totalSent, msg.length() - totalSent);
        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        totalSent += static_cast<size_t>(sent);
    }

    return true;
}

bool ThreadedSocketConnection::connect()
{
    // do the bind in the thread as name resolution may block
    if (!m_sourceAddress.empty() || m_sourcePort)
    {
        socket_bind(m_socket, m_sourceAddress.c_str(), m_sourcePort);
    }

    return socket_connect(getSocket(), m_address.c_str(), m_port) >= 0;
}

void ThreadedSocketConnection::disconnect()
{
    // Any thread can get here -- Session::disconnect() through the responder, or
    // a transport's onStop -- while this connection's own thread polls the
    // socket. Shutting it down wakes that thread. Closing it would free the
    // number for reuse while the reader still used it, and the reader's failure
    // path would close it again (#28): the transport closes it, once, when it
    // retires the connection's thread.
    m_disconnect = true;
    ::shutdown(m_socket, SHUT_RDWR);
}

bool ThreadedSocketConnection::setupExpired()
{
    if (m_setupDeadline == 0)
    {
        return false;
    }
    if (m_pSession && m_pSession->receivedLogon())
    {
        m_setupDeadline = 0; // set up; from here on the session times itself
        return false;
    }
    if (::time(0) <= m_setupDeadline)
    {
        return false;
    }

    // Accepted, but not logged on in time: whether it sent nothing, an incomplete
    // message, or only messages that drew a reject, the connection and the
    // session it bound are freed for the counterparty.
    if (m_pSession)
    {
        m_pSession->getLog()->onEvent("Timed out a connection that did not log on");
        m_pSession->disconnect();
    }
    else
    {
        if (m_pLog)
        {
            m_pLog->onEvent("Timed out a connection that did not log on");
        }
        disconnect();
    }
    return true;
}

bool ThreadedSocketConnection::read()
{
    if (setupExpired())
    {
        return false;
    }

#if _MSC_VER
    struct timeval timeout = {1, 0};
    fd_set readset = m_fds;
#else
    int timeout = 1000; // 1000ms = 1 second
    struct pollfd pfd = {m_socket, POLLIN | POLLPRI, 0};
#endif

    try
    {
        // Wait for input (1 second timeout)
#if _MSC_VER
        int result = select(0, &readset, 0, 0, &timeout);
#else
        int result = poll(&pfd, 1, timeout);
#endif

        if (result > 0) // Something to read
        {
            // We can read without blocking
            ssize_t size = socket_recv(m_socket, m_buffer, sizeof(m_buffer));
            if (size <= 0)
            {
                throw SocketRecvFailed(size);
            }
            m_parser.addToStream(m_buffer, size);
        }
        else if (result == 0 && m_pSession) // Timeout
        {
            m_pSession->next(UtcTimeStamp::now());
        }
        else if (result < 0) // Error
        {
            throw SocketRecvFailed(result);
        }

        processStream();
        return true;
    }
    catch (SocketRecvFailed &e)
    {
        if (m_disconnect)
        {
            return false;
        }

        if (m_pSession)
        {
            m_pSession->getLog()->onEvent(e.what());
            m_pSession->disconnect();
        }
        else
        {
            disconnect();
        }

        return false;
    }
    catch (MessageParseError &e)
    {
        if (m_disconnect)
        {
            return false;
        }

        if (m_pSession)
        {
            m_pSession->getLog()->onEvent(e.what());
            m_pSession->disconnect();
        }
        else
        {
            disconnect();
        }

        return false;
    }
}

bool ThreadedSocketConnection::readMessage(std::string &msg) EXCEPT(SocketRecvFailed)
{
    try
    {
        return m_parser.readFixMessage(msg);
    }
    catch (MessageParseError &)
    {
    }
    return true;
}

void ThreadedSocketConnection::processStream()
{
    std::string msg;
    while (readMessage(msg))
    {
        if (!m_pSession)
        {
            if (!setSession(msg))
            {
                disconnect();
                continue;
            }
        }
        try
        {
            m_pSession->next(msg, UtcTimeStamp::now());
        }
        catch (InvalidMessage &)
        {
            if (!m_pSession->isLoggedOn())
            {
                disconnect();
                return;
            }
        }
    }
}

bool ThreadedSocketConnection::setSession(const std::string &msg)
{
    m_pSession = Session::lookupSession(msg, true);
    if (!m_pSession)
    {
        if (m_pLog)
        {
            m_pLog->onEvent("Session not found for incoming message: " + msg);
            m_pLog->onIncoming(msg);
        }
        return false;
    }

    SessionID sessionID = m_pSession->getSessionID();
    m_pSession = 0;

    // see if the session frees up within 5 seconds
    for (int i = 1; i <= 5; i++)
    {
        if (!Session::isSessionRegistered(sessionID))
        {
            m_pSession = Session::registerSession(sessionID);
        }
        if (m_pSession)
        {
            break;
        }
        process_sleep(1);
    }

    if (!m_pSession)
    {
        return false;
    }
    if (m_sessions.find(m_pSession->getSessionID()) == m_sessions.end())
    {
        return false;
    }

    if (m_pSession->isAcceptor())
    {
        std::string remote_address = socket_peername(m_socket);
        if (!m_pSession->getAllowedRemoteAddresses().empty() && !m_pSession->inAllowedRemoteAddresses(remote_address))
        {
            m_pSession->getLog()->onEvent("Deny connections to the acceptor from " + remote_address);
            return false;
        }
        m_pSession->getLog()->onEvent("Allows connections to the acceptor from " + remote_address);
    }

    m_pSession->setResponder(this);
    return true;
}

} // namespace FIX
