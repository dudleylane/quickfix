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

#include "PortSetting.h"
#include "Session.h"
#include "Settings.h"
#include "ThreadedSocketInitiator.h"

namespace FIX
{
ThreadedSocketInitiator::ThreadedSocketInitiator(Application &application, MessageStoreFactory &factory,
                                                 const SessionSettings &settings) EXCEPT(ConfigError)
    : Initiator(application, factory, settings), m_lastConnect(0), m_reconnectInterval(30), m_noDelay(false),
      m_sendBufSize(0), m_rcvBufSize(0)
{
    socket_init();
}

ThreadedSocketInitiator::ThreadedSocketInitiator(Application &application, MessageStoreFactory &factory,
                                                 const SessionSettings &settings, LogFactory &logFactory)
    EXCEPT(ConfigError)
    : Initiator(application, factory, settings, logFactory), m_lastConnect(0), m_reconnectInterval(30),
      m_noDelay(false), m_sendBufSize(0), m_rcvBufSize(0)
{
    socket_init();
}

ThreadedSocketInitiator::~ThreadedSocketInitiator() { socket_term(); }

void ThreadedSocketInitiator::onConfigure(const SessionSettings &s) EXCEPT(ConfigError)
{
    // The socket options are read from each session's settings when it connects (#150, #170); check
    // every value now.
    for (const SessionID &sessionID : s.getSessions())
    {
        const Dictionary &settings = s.get(sessionID);
        getSocketNoDelay(settings);
        getSocketBufferSize(settings, SOCKET_SEND_BUFFER_SIZE);
        getSocketBufferSize(settings, SOCKET_RECEIVE_BUFFER_SIZE);
        getSocketQueuedWrites(settings);
        getSocketThreadAffinity(settings);
    }
}

void ThreadedSocketInitiator::onInitialize(const SessionSettings &s) EXCEPT(RuntimeError) {}

void ThreadedSocketInitiator::onStart()
{
    while (!isStopped())
    {
        // connect() starts only the sessions whose own ReconnectInterval has passed (#125).
        {
            Locker l(m_mutex);
            // stop() can come between the loop's check and this lock. It sets m_stop
            // before onStop() takes m_threads under this lock, and a connection thread
            // made after that would never be joined (#113).
            if (isStopped())
            {
                break;
            }
            connect();
        }

        process_sleep(1);
    }
}

bool ThreadedSocketInitiator::onPoll() { return false; }

void ThreadedSocketInitiator::onStop()
{
    SocketToThread threads;
    SocketToThread::iterator i;

    {
        Locker l(m_mutex);

        time_t start = 0;
        time_t now = 0;

        ::time(&start);
        while (isLoggedOn())
        {
            if (::time(&now) - 5 >= start)
            {
                break;
            }
        }

        threads = m_threads;
        m_threads.clear();
    }

    // Each thread's socket is closed here only after the thread has been joined.
    // Shutting it down is what wakes the thread; closing it first would free the
    // number while the thread still used it, and the connection's failure path
    // would close it again (#28).
    for (i = threads.begin(); i != threads.end(); ++i)
    {
        ::shutdown(i->first, SHUT_RDWR);
    }

    for (i = threads.begin(); i != threads.end(); ++i)
    {
        thread_join(i->second);
        socket_close(i->first);
    }
    threads.clear();
}

void ThreadedSocketInitiator::doConnect(const SessionID &s, const Dictionary &d)
{
    try
    {
        Session *session = Session::lookupSession(s);
        if (!session->isSessionTime(UtcTimeStamp::now()))
        {
            return;
        }

        Log *log = session->getLog();

        HostDetails host = m_hostDetailsProvider.getHost(s, d);

        socket_handle socket = socket_createConnector();
        if (getSocketNoDelay(d))
        {
            socket_setsockopt(socket, TCP_NODELAY);
        }
        if (const int size = getSocketBufferSize(d, SOCKET_SEND_BUFFER_SIZE))
        {
            socket_setsockopt(socket, SO_SNDBUF, size);
        }
        if (const int size = getSocketBufferSize(d, SOCKET_RECEIVE_BUFFER_SIZE))
        {
            socket_setsockopt(socket, SO_RCVBUF, size);
        }

        setPending(s);
        log->onEvent("Connecting to " + host.address + " on port " + IntConvertor::convert(host.port) + " (Source " +
                     host.sourceAddress + ":" + IntConvertor::convert(host.sourcePort) +
                     ") ReconnectInterval=" + IntConvertor::convert(reconnectInterval(d)));

        ThreadedSocketConnection *pConnection =
            new ThreadedSocketConnection(s, socket, host.address, host.port, getLog(), host.sourceAddress,
                                         host.sourcePort, getSocketQueuedWrites(d));

        ThreadPair *pair = new ThreadPair(this, pConnection);

        {
            Locker l(m_mutex);
            thread_id thread;
            if (thread_spawn(&socketThread, pair, thread))
            {
                addThread(socket, thread);
            }
            else
            {
                delete pair;
                pConnection->disconnect();
                delete pConnection;
                socket_close(socket);
                setDisconnected(s);
            }
        }
    }
    catch (std::exception &)
    {
    }
}

void ThreadedSocketInitiator::addThread(socket_handle s, thread_id t)
{
    Locker l(m_mutex);

    m_threads[s] = t;
}

void ThreadedSocketInitiator::removeThread(socket_handle s)
{
    Locker l(m_mutex);
    SocketToThread::iterator i = m_threads.find(s);

    if (i != m_threads.end())
    {
        thread_detach(i->second);
        m_threads.erase(i);

        // The socket is retired with its thread, under the same lock: m_threads
        // is keyed by the fd number, which is free for reuse once it is closed,
        // so erasing the entry first means no new socket can collide with it.
        socket_close(s);
    }
}

THREAD_PROC ThreadedSocketInitiator::socketThread(void *p)
{
    ThreadPair *pair = reinterpret_cast<ThreadPair *>(p);

    ThreadedSocketInitiator *pInitiator = pair->first;
    TransportThread own(static_cast<const Initiator *>(pInitiator)); // stop() must not wait on this thread (#174)
    ThreadedSocketConnection *pConnection = pair->second;
    FIX::SessionID sessionID = pConnection->getSession()->getSessionID();
    FIX::Session *pSession = FIX::Session::lookupSession(sessionID);
    socket_handle socket = pConnection->getSocket();
    delete pair;

    pInitiator->lock();

    if (!pConnection->connect())
    {
        pInitiator->getLog()->onEvent("Connection failed");
        pConnection->disconnect();
        delete pConnection;
        // While registered: once removeThread() has detached this thread, onStop()
        // no longer waits for it, and the initiator may be destroyed under it.
        pInitiator->setDisconnected(sessionID);
        pInitiator->removeThread(socket);
        return 0;
    }

    pInitiator->setConnected(sessionID);
    pInitiator->getLog()->onEvent("Connection succeeded");
    pConnection->startQueuedWrites();

    if (const Dictionary *settings = pInitiator->getSessionSettings(sessionID))
    {
        pinThisThread(*settings, pSession->getLog());
    }

    pSession->next(UtcTimeStamp::now());

    while (pConnection->read())
    {
    }

    delete pConnection;
    // setDisconnected first, while onStop() would still join this thread; after
    // removeThread() the initiator may be gone.
    pInitiator->setDisconnected(sessionID);
    if (!pInitiator->isStopped())
    {
        pInitiator->removeThread(socket);
    }
    return 0;
}
} // namespace FIX
