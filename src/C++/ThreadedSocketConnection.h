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

#ifndef FIX_THREADEDSOCKETCONNECTION_H
#define FIX_THREADEDSOCKETCONNECTION_H

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#endif

#include "Parser.h"
#include "Responder.h"
#include "SessionID.h"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace FIX
{
class ThreadedSocketAcceptor;
class ThreadedSocketInitiator;
class Session;
class Application;
class Log;

/// Encapsulates a socket file descriptor (multi-threaded).
class ThreadedSocketConnection : Responder
{
public:
    typedef std::set<SessionID> Sessions;

    /// queuedWrites is SocketQueuedWrites: fixed here, before any sender can reach the connection.
    ThreadedSocketConnection(socket_handle s, Sessions sessions, Log *pLog, bool queuedWrites = false);
    ThreadedSocketConnection(const SessionID &, socket_handle s, const std::string &address, short port, Log *pLog,
                             const std::string &sourceAddress = "", short sourcePort = 0, bool queuedWrites = false);
    virtual ~ThreadedSocketConnection();

    Session *getSession() const { return m_pSession; }
    socket_handle getSocket() const { return m_socket; }
    bool connect();
    void disconnect();
    bool read();
    /// With SocketQueuedWrites=Y, makes the socket non-blocking: from here on this connection's own thread
    /// writes what send() queues, while it goes on reading. Called by the transport's thread once the socket
    /// is connected, since a non-blocking connect() would not wait. With N, does nothing.
    void startQueuedWrites();

private:
    static constexpr int SETUP_SECONDS = 10;
    bool setupExpired();
    bool readMessage(std::string &msg) EXCEPT(SocketRecvFailed);
    void processStream();
    bool send(const std::string &);
    bool sendDirect(const std::string &);
    bool sendQueued(const std::string &);
    void pollDirect();
    void pollQueued();
    bool writeQueued();
    bool writeFront(bool wait, bool &progress, time_t deadline = 0);
    bool setSession(const std::string &msg);

    socket_handle m_socket;
    char m_buffer[BUFSIZ];

    std::string m_address;
    int m_port;
    std::string m_sourceAddress;
    int m_sourcePort;

    Log *m_pLog;
    Parser m_parser;
    Sessions m_sessions;
    Session *m_pSession;
    std::atomic<bool> m_disconnect;
    /// When an accepted connection must have logged on by; 0 once it has, and
    /// for an initiator's connection, which its session times instead.
    time_t m_setupDeadline = 0;

    // Outgoing messages with SocketQueuedWrites=Y (#166); with N none of this is used, and send() writes the
    // message itself, blocking. send() is called under the session's lock, so in sequence order. It queues the
    // message and wakes this connection's thread through m_wakeFd, which writes the queue, so no sender waits
    // for the peer to read. Past SEND_QUEUE_LIMIT queued bytes the sender writes the queue itself, as every
    // sender does with N, which keeps a slow peer's backpressure. It cannot wait for the connection's thread
    // instead: that thread calls into the session, whose lock the sender holds. One writer at a time holds
    // m_writing. Lock order: Session::m_mutex, then m_sendMutex; m_sendMutex is held only to
    // look at or pop the front of the queue, never across a write or a call into the session. Only the writer pops, and
    // appending to a deque leaves its elements in place, so the front stays valid while it is written. While a
    // sender past the bound holds the role, this connection's thread can find POLLOUT ready and the role taken,
    // and poll again at once; that lasts only until the sender's write ends.
    static constexpr std::size_t SEND_QUEUE_LIMIT = 4 * 1024 * 1024;
    static constexpr int FLUSH_SECONDS = 5; // how long disconnect() writes what is still queued
    std::mutex m_sendMutex;
    std::condition_variable m_sendSpace;
    std::deque<std::string> m_sendQueue;
    std::size_t m_sendQueueBytes = 0;
    std::size_t m_sendOffset = 0;
    bool m_writing = false; // whoever writes holds this, under m_sendMutex: one writer at a time
    int m_wakeFd;           // -1 with N
    const bool m_queued;    // Y, and m_wakeFd was made; without it, writes stay direct
    time_t m_lastTimerSweep = 0;
#if _MSC_VER
    fd_set m_fds;
#endif
};
} // namespace FIX

#endif // FIX_THREADEDSOCKETCONNECTION_H
