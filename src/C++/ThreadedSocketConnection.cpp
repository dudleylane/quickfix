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
#include <chrono>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "Session.h"
#include "ThreadedSocketAcceptor.h"
#include "ThreadedSocketConnection.h"
#include "ThreadedSocketInitiator.h"
#include "Utility.h"

namespace FIX
{
ThreadedSocketConnection::ThreadedSocketConnection(socket_handle s, Sessions sessions, Log *pLog, bool queuedWrites)
    : m_socket(s), m_pLog(pLog), m_sessions(sessions), m_pSession(0), m_disconnect(false),
      m_setupDeadline(::time(0) + SETUP_SECONDS),
      m_wakeFd(queuedWrites ? ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC) : -1), m_queued(m_wakeFd >= 0)
{
#if _MSC_VER
    FD_ZERO(&m_fds);
    FD_SET(m_socket, &m_fds);
#endif
    if (queuedWrites && !m_queued && m_pLog)
    {
        m_pLog->onEvent("SocketQueuedWrites=Y, but no eventfd could be made: this connection writes directly");
    }
}

ThreadedSocketConnection::ThreadedSocketConnection(const SessionID &sessionID, socket_handle s,
                                                   const std::string &address, short port, Log *pLog,
                                                   const std::string &sourceAddress, short sourcePort,
                                                   bool queuedWrites)
    : m_socket(s), m_address(address), m_port(port), m_sourceAddress(sourceAddress), m_sourcePort(sourcePort),
      m_pLog(pLog), m_pSession(Session::lookupSession(sessionID)), m_disconnect(false),
      m_wakeFd(queuedWrites ? ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC) : -1), m_queued(m_wakeFd >= 0)
{
#if _MSC_VER
    FD_ZERO(&m_fds);
    FD_SET(m_socket, &m_fds);
#endif
    if (queuedWrites && !m_queued)
    {
        Log *log = m_pSession ? m_pSession->getLog() : m_pLog;
        if (log)
        {
            log->onEvent("SocketQueuedWrites=Y, but no eventfd could be made: this connection writes directly");
        }
    }
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
    if (m_wakeFd >= 0)
    {
        ::close(m_wakeFd);
    }
}

bool ThreadedSocketConnection::send(const std::string &msg) { return m_queued ? sendQueued(msg) : sendDirect(msg); }

bool ThreadedSocketConnection::sendDirect(const std::string &msg)
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

bool ThreadedSocketConnection::sendQueued(const std::string &msg)
{
    bool wake;
    {
        std::unique_lock<std::mutex> lock(m_sendMutex);
        if (m_disconnect)
        {
            return false;
        }
        wake = m_sendQueue.empty();
        m_sendQueue.push_back(msg);
        m_sendQueueBytes += msg.size();
        // Past the bound, write the queue down here, as a blocking send does, once no other writer is busy;
        // the connection's thread writes only without blocking, so it gives the role up promptly.
        while (m_sendQueueBytes > SEND_QUEUE_LIMIT && !m_disconnect)
        {
            if (m_writing)
            {
                m_sendSpace.wait(lock,
                                 [this] { return !m_writing || m_sendQueueBytes <= SEND_QUEUE_LIMIT || m_disconnect; });
                continue;
            }
            m_writing = true;
            lock.unlock();
            bool ok = true;
            while (ok)
            {
                {
                    std::lock_guard<std::mutex> check(m_sendMutex);
                    if (m_sendQueueBytes <= SEND_QUEUE_LIMIT || m_disconnect)
                    {
                        break;
                    }
                }
                bool progress = false;
                ok = writeFront(true, progress);
            }
            lock.lock();
            m_writing = false;
            m_sendSpace.notify_all();
            if (!ok)
            {
                return false;
            }
        }
    }
    if (wake)
    {
        // Only an empty queue needs a wake: otherwise the thread is writing it and drains it to empty.
        const eventfd_t one = 1;
        ::eventfd_write(m_wakeFd, one);
    }
    return true;
}

void ThreadedSocketConnection::startQueuedWrites()
{
    if (m_queued)
    {
        socket_setnonblock(m_socket);
    }
}

// On this connection's thread: writes queued messages until the queue is empty or the socket would block,
// unless a sender holds the writer's role. Returns false if the socket failed.
bool ThreadedSocketConnection::writeQueued()
{
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        if (m_writing)
        {
            return true;
        }
        m_writing = true;
    }
    bool ok = true;
    bool more = true;
    while (ok && more)
    {
        {
            std::lock_guard<std::mutex> lock(m_sendMutex);
            more = !m_sendQueue.empty() && !m_disconnect;
        }
        if (more)
        {
            bool progress = false;
            ok = writeFront(false, progress);
            more = ok && progress; // no progress: the socket is full, poll for POLLOUT and come back
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        m_writing = false;
    }
    m_sendSpace.notify_all();
    return ok;
}

// The writer's role is held. Writes what remains of the front message, or as much as the socket takes;
// with wait, polls for room until it is written. Returns false if the socket failed.
bool ThreadedSocketConnection::writeFront(bool wait, bool &progress, time_t deadline)
{
    progress = false;
    const std::string *front;
    std::size_t offset;
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        if (m_sendQueue.empty() || m_disconnect)
        {
            return !m_disconnect;
        }
        front = &m_sendQueue.front();
        offset = m_sendOffset;
    }
    while (offset < front->length())
    {
        const ssize_t sent = socket_send(m_socket, front->c_str() + offset, front->length() - offset);
        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                return false;
            }
            if (!wait)
            {
                break;
            }
            if (deadline && ::time(0) > deadline)
            {
                break;
            }
            struct pollfd pfd = {m_socket, POLLOUT, 0};
            if (::poll(&pfd, 1, 1000) < 0 && errno != EINTR)
            {
                return false;
            }
            if (m_disconnect)
            {
                return false;
            }
            continue;
        }
        offset += static_cast<std::size_t>(sent);
        progress = true;
        std::lock_guard<std::mutex> lock(m_sendMutex);
        m_sendOffset = offset;
    }
    if (offset == front->length())
    {
        {
            std::lock_guard<std::mutex> lock(m_sendMutex);
            m_sendQueueBytes -= front->length();
            m_sendQueue.pop_front();
            m_sendOffset = 0;
        }
        m_sendSpace.notify_all();
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
    if (!m_queued)
    {
        m_disconnect = true;
        ::shutdown(m_socket, SHUT_RDWR);
        return;
    }

    // What was sent before the disconnect goes first -- a Logout's reply, say, which the session sends and
    // then disconnects -- as it does when every send writes at once. Take the writer's role and flush, for at
    // most FLUSH_SECONDS: a peer that has stopped reading cannot hold this. If another writer has the role --
    // this connection's thread between two polls, or a sender past the bound -- wait for it within the same
    // deadline, then flush what it left. Never giving up the queue to that window is the point: a sender
    // past the bound gives the role up after each message, and the connection's thread never blocks in a
    // write, but one past the bound on a peer that has stopped reading holds it until the deadline. Waiting
    // cannot deadlock: no writer takes the session's lock, and nothing that holds the role calls here.
    const time_t deadline = ::time(0) + FLUSH_SECONDS;
    bool flush = false;
    {
        std::unique_lock<std::mutex> lock(m_sendMutex);
        while (!m_disconnect && m_writing && ::time(0) <= deadline)
        {
            m_sendSpace.wait_for(lock, std::chrono::milliseconds(100));
        }
        if (!m_disconnect && !m_writing && !m_sendQueue.empty())
        {
            m_writing = true;
            flush = true;
        }
    }
    if (flush)
    {
        bool ok = true;
        bool progress = true;
        for (;;)
        {
            {
                std::lock_guard<std::mutex> lock(m_sendMutex);
                if (m_sendQueue.empty() || m_disconnect)
                {
                    break;
                }
            }
            if (!ok || !progress || ::time(0) > deadline)
            {
                break;
            }
            ok = writeFront(true, progress, deadline);
        }
    }
    // Senders waiting for queue space give up; what is still queued is freed with the connection, since
    // this connection's thread may be writing the front of it.
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        m_disconnect = true;
        if (flush)
        {
            m_writing = false;
        }
    }
    m_sendSpace.notify_all();
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

void ThreadedSocketConnection::pollDirect()
{
#if _MSC_VER
    struct timeval timeout = {1, 0};
    fd_set readset = m_fds;
#else
    int timeout = 1000; // 1000ms = 1 second
    struct pollfd pfd = {m_socket, POLLIN | POLLPRI, 0};
#endif

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
}

void ThreadedSocketConnection::pollQueued()
{
    bool queued;
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        queued = !m_sendQueue.empty();
    }
    struct pollfd pfds[2] = {{m_socket, static_cast<short>(POLLIN | POLLPRI | (queued ? POLLOUT : 0)), 0},
                             {m_wakeFd, POLLIN, 0}};

    // Wait for input, for room to write what is queued, or for a wake from send() (1 second timeout)
    const int result = poll(pfds, 2, 1000);

    if (result > 0)
    {
        if (pfds[1].revents & POLLIN)
        {
            eventfd_t count;
            ::eventfd_read(m_wakeFd, &count);
        }
        if (pfds[0].revents & (POLLIN | POLLPRI | POLLHUP | POLLERR))
        {
            const ssize_t size = socket_recv(m_socket, m_buffer, sizeof(m_buffer));
            if (size > 0)
            {
                m_parser.addToStream(m_buffer, size);
            }
            else if (!(size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)))
            {
                throw SocketRecvFailed(size);
            }
        }
    }
    else if (result < 0 && errno != EINTR) // Error
    {
        throw SocketRecvFailed(result);
    }

    if (!writeQueued())
    {
        throw SocketRecvFailed(-1);
    }

    // The session's timer runs once a wall-clock second, whether or not the poll timed out: wakes from
    // send() could otherwise keep it from ever timing out.
    const time_t second = ::time(0);
    if (m_pSession && second != m_lastTimerSweep)
    {
        m_lastTimerSweep = second;
        m_pSession->next(UtcTimeStamp::now());
    }
}

bool ThreadedSocketConnection::read()
{
    if (setupExpired())
    {
        return false;
    }

    try
    {
        if (m_queued)
        {
            pollQueued();
        }
        else
        {
            pollDirect();
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
