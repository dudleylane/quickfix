/* ====================================================================
 * Copyright (c) 1998-2006 Ralf S. Engelschall. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by
 *     Ralf S. Engelschall <rse@engelschall.com> for use in the
 *     mod_ssl project (http://www.modssl.org/)."
 *
 * 4. The names "mod_ssl" must not be used to endorse or promote
 *    products derived from this software without prior written
 *    permission. For written permission, please contact
 *    rse@engelschall.com.
 *
 * 5. Products derived from this software may not be called "mod_ssl"
 *    nor may "mod_ssl" appear in their names without prior
 *    written permission of Ralf S. Engelschall.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by
 *     Ralf S. Engelschall <rse@engelschall.com> for use in the
 *     mod_ssl project (http://www.modssl.org/)."
 *
 * THIS SOFTWARE IS PROVIDED BY RALF S. ENGELSCHALL ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL RALF S. ENGELSCHALL OR
 * HIS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 */

/* ====================================================================
 * Copyright (c) 1995-1999 Ben Laurie. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by Ben Laurie
 *    for use in the Apache-SSL HTTP server project."
 *
 * 4. The name "Apache-SSL Server" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission.
 *
 * 5. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Ben Laurie
 *    for use in the Apache-SSL HTTP server project."
 *
 * THIS SOFTWARE IS PROVIDED BY BEN LAURIE ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL BEN LAURIE OR
 * HIS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 */

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

#if (HAVE_SSL > 0)

#include "Exceptions.h"
#include "PortSetting.h"
#include "SSLSocketAcceptor.h"
#include "Session.h"
#include "Settings.h"
#include "Utility.h"

namespace FIX
{

int SSLSocketAcceptor::passPhraseHandleCB(char *buf, int bufsize, int verify, void *instance)
{
    return reinterpret_cast<SSLSocketAcceptor *>(instance)->passwordHandleCallback(buf, bufsize, verify);
}

SSLSocketAcceptor::SSLSocketAcceptor(Application &application, MessageStoreFactory &factory,
                                     const SessionSettings &settings) EXCEPT(ConfigError)
    : Acceptor(application, factory, settings), m_pServer(0), m_lastSetupSweep(0), m_sslInit(false),
      m_verify(SSL_CLIENT_VERIFY_NOTSET), m_ctx(0), m_revocationStore(0)
{
}

SSLSocketAcceptor::SSLSocketAcceptor(Application &application, MessageStoreFactory &factory,
                                     const SessionSettings &settings, LogFactory &logFactory) EXCEPT(ConfigError)
    : Acceptor(application, factory, settings, logFactory), m_pServer(0), m_lastSetupSweep(0), m_sslInit(false),
      m_verify(SSL_CLIENT_VERIFY_NOTSET), m_ctx(0), m_revocationStore(0)
{
}

SSLSocketAcceptor::~SSLSocketAcceptor()
{
    disconnectRemaining();
    for (const PendingHandshakes::value_type &pending : m_pendingHandshakes)
    {
        delete pending.second.connection;
    }

    // onStart() owns m_pServer on the blocking path and nulls it there, but the
    // poll() path never runs onStart(), so without this the server leaks.  Doing
    // it here rather than in onStop() avoids racing a blocking onStart() that may
    // still be using it when stop() is called from another thread.
    delete m_pServer;
    m_pServer = 0;

    if (m_sslInit)
    {
        SSL_CTX_free(m_ctx);
        m_ctx = 0;
        ssl_term();
    }
}

void SSLSocketAcceptor::onConfigure(const SessionSettings &sessionSettings) EXCEPT(ConfigError)
{
    std::set<SessionID> sessions = sessionSettings.getSessions();
    for (const SessionID &sessionID : sessions)
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

void SSLSocketAcceptor::onInitialize(const SessionSettings &sessionSettings) EXCEPT(RuntimeError)
{
    if (!m_sslInit)
    {
        ssl_init();

        std::string errStr;

        /* set up the application context */
        if ((m_ctx = createSSLContext(true, m_settings, errStr)) == 0)
        {
            ssl_term();
            throw RuntimeError(errStr);
        }

        if (!loadSSLCert(m_ctx, true, m_settings, getLog(), SSLSocketAcceptor::passPhraseHandleCB, this, errStr))
        {
            ssl_term();
            throw RuntimeError(errStr);
        }

        if (!loadCAInfo(m_ctx, true, m_settings, getLog(), errStr, m_verify))
        {
            ssl_term();
            throw RuntimeError(errStr);
        }

        m_revocationStore = loadCRLInfo(m_ctx, m_settings, getLog(), errStr);
        if (!m_revocationStore && !errStr.empty())
        {
            ssl_term();
            throw RuntimeError(errStr);
        }

        m_sslInit = true;
    }

    uint16_t port = 0;

    try
    {
        m_pServer = new SocketServer(1);

        std::set<SessionID> sessions = sessionSettings.getSessions();
        for (const SessionID &sessionID : sessions)
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

            m_portToSessions[port].insert(sessionID);
            m_pServer->add(port, reuseAddress, noDelay, sendBufSize, rcvBufSize);
        }
    }
    catch (SocketException &e)
    {
        // Release the listeners bound before the failing port, as SocketAcceptor does: kept, they
        // would leak with the server and make a retried start() fail to bind them again (#60).
        delete m_pServer;
        m_pServer = 0;
        throw RuntimeError("Unable to create, bind, or listen to port " + IntConvertor::convert(port) + " (" +
                           e.what() + ")");
    }
}

void SSLSocketAcceptor::onStart()
{
    while (!isStopped() && m_pServer && m_pServer->block(*this))
    {
        expireSetup();
    }

    if (!m_pServer)
    {
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

    m_pServer->close();
    delete m_pServer;
    m_pServer = 0;
}

void SSLSocketAcceptor::disconnectRemaining()
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

bool SSLSocketAcceptor::onPoll()
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
    expireSetup();
    return true;
}

void SSLSocketAcceptor::onStop() {}

void SSLSocketAcceptor::onConnect(SocketServer &server, socket_handle a, socket_handle s)
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
    int port = server.socketToPort(a);
    Sessions sessions = m_portToSessions[port];

    SSL *ssl = SSL_new(m_ctx);
    SSL_clear(ssl);
    BIO *sBio = BIO_new_socket(s, BIO_NOCLOSE); // Unfortunately OpenSSL assumes socket is int
    SSL_set_bio(ssl, sBio, sBio);
    // TODO - check this
    SSL_set_app_data(ssl, m_revocationStore);
    SSL_set_verify_result(ssl, X509_V_OK);

    SSLSocketConnection *sconn = new SSLSocketConnection(s, ssl, sessions, &server.getMonitor());

    // The handshake is stepped by onData and onWrite as the socket becomes ready,
    // like every other exchange on this thread, and expireHandshakes() bounds how
    // long it may take.
    sconn->setHandshakeStartTime(time(0));
    m_pendingHandshakes[s] = PendingHandshake{sconn, port};
}

size_t SSLSocketAcceptor::pendingConnections() const
{
    size_t pending = m_pendingHandshakes.size();
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

SSLSocketAcceptor::HandshakeStep SSLSocketAcceptor::stepHandshake(SSLSocketConnection *connection)
{
    SSL *ssl = connection->sslObject();
    ERR_clear_error();
    const int rc = SSL_accept(ssl);
    if (rc > 0)
    {
        return checkSSLClient(ssl, getLog(), m_verify) == 0 ? HANDSHAKE_COMPLETE : HANDSHAKE_FAILED;
    }

    const int err = SSL_get_error(ssl, rc);
    if (err == SSL_ERROR_WANT_READ || (err == SSL_ERROR_SYSCALL && errno == EINTR))
    {
        return HANDSHAKE_IN_PROGRESS;
    }
    if (err == SSL_ERROR_WANT_WRITE)
    {
        connection->subscribeToSocketWriteAvailableEvents();
        return HANDSHAKE_IN_PROGRESS;
    }

    if (getLog())
    {
        if (err == SSL_ERROR_ZERO_RETURN)
        {
            getLog()->onEvent("SSL handshake stopped: connection was closed");
        }
        else if (ERR_GET_REASON(ERR_peek_error()) == SSL_R_HTTP_REQUEST)
        {
            getLog()->onEvent("SSL handshake failed: HTTP spoken on HTTPS port");
        }
        else
        {
            getLog()->onEvent("SSL handshake failed");
            for (unsigned long e = ERR_get_error(); e != 0; e = ERR_get_error())
            {
                const char *reason = ERR_reason_error_string(e);
                getLog()->onEvent(std::string("SSL failure reason: ") + (reason ? reason : "unknown"));
            }
        }
    }
    return HANDSHAKE_FAILED;
}

bool SSLSocketAcceptor::advanceHandshake(SocketServer &server, PendingHandshakes::iterator pending)
{
    SSLSocketConnection *connection = pending->second.connection;
    if (connection->getSecondsFromHandshakeStart(time(0)) > 10)
    {
        abandonHandshake(server, pending, "SSL handshake timed out");
        return true;
    }

    switch (stepHandshake(connection))
    {
    case HANDSHAKE_IN_PROGRESS:
        return true;
    case HANDSHAKE_FAILED:
        abandonHandshake(server, pending, "Failed to accept SSL connection");
        return true;
    case HANDSHAKE_COMPLETE:
        break;
    }

    const socket_handle s = pending->first;
    const int port = pending->second.port;
    m_pendingHandshakes.erase(pending);
    // Reuse the setup clock for the first-message phase that follows.
    connection->setHandshakeStartTime(time(0));
    m_connections[s] = connection;

    std::stringstream stream;
    stream << "Accepted SSL connection from " << socket_peername(s) << " on port " << port;
    if (getLog())
    {
        getLog()->onEvent(stream.str());
    }

    // Anything the client sent behind its handshake may already sit in the SSL
    // object's buffer, where poll() cannot see it.
    if (SSL_pending(connection->sslObject()) > 0)
    {
        return connection->read(*this, server);
    }
    return true;
}

void SSLSocketAcceptor::abandonHandshake(SocketServer &server, PendingHandshakes::iterator pending,
                                         const std::string &reason)
{
    const socket_handle s = pending->first;
    SSLSocketConnection *connection = pending->second.connection;

    std::stringstream stream;
    stream << reason << " from " << socket_peername(s) << " on port " << pending->second.port;
    if (getLog())
    {
        getLog()->onEvent(stream.str());
    }

    // A client refused after a completed handshake is told so.
    SSL *ssl = connection->sslObject();
    if (SSL_is_init_finished(ssl))
    {
        SSL_shutdown(ssl);
    }

    m_pendingHandshakes.erase(pending);
    server.getMonitor().drop(s); // the monitor closes the socket, once
    delete connection;           // BIO_NOCLOSE: frees the SSL object and nothing else
}

void SSLSocketAcceptor::expireSetup()
{
    if (!m_pServer || (m_pendingHandshakes.empty() && m_connections.empty()))
    {
        return;
    }

    const time_t now = time(0);
    if (now == m_lastSetupSweep)
    {
        return;
    }
    m_lastSetupSweep = now;

    for (PendingHandshakes::iterator i = m_pendingHandshakes.begin(); i != m_pendingHandshakes.end();)
    {
        PendingHandshakes::iterator pending = i++;
        if (pending->second.connection->getSecondsFromHandshakeStart(now) > 10)
        {
            abandonHandshake(*m_pServer, pending, "SSL handshake timed out");
        }
    }

    // A connection past its handshake is set up once its session has received a
    // logon; bound the rest too, including one that bound a session with
    // messages that drew only rejects. Dropping is deferred to the monitor, which
    // closes the socket once on the next block() (#26), so m_connections is not
    // mutated here.
    for (const SocketConnections::value_type &entry : m_connections)
    {
        Session *pSession = entry.second->getSession();
        if ((pSession == 0 || !pSession->receivedLogon()) && entry.second->getSecondsFromHandshakeStart(now) > 10)
        {
            if (getLog())
            {
                getLog()->onEvent(pSession ? "Timed out an SSL connection that did not log on"
                                           : "Timed out an SSL connection that sent no complete message");
            }
            m_pServer->getMonitor().drop(entry.first);
        }
    }
}

void SSLSocketAcceptor::onWrite(SocketServer &server, socket_handle s)
{
    PendingHandshakes::iterator pending = m_pendingHandshakes.find(s);
    if (pending != m_pendingHandshakes.end())
    {
        server.getMonitor().unsignal(s);
        advanceHandshake(server, pending);
        return;
    }

    SocketConnections::iterator i = m_connections.find(s);
    if (i == m_connections.end())
    {
        return;
    }
    SSLSocketConnection *pSocketConnection = i->second;

    if (pSocketConnection->didReadFromSocketRequestToWrite())
    {
        pSocketConnection->read(*this, server);
    }

    if (pSocketConnection->processQueue())
    {
        pSocketConnection->unsignal();
    }
}

bool SSLSocketAcceptor::onData(SocketServer &server, socket_handle s)
{
    PendingHandshakes::iterator pending = m_pendingHandshakes.find(s);
    if (pending != m_pendingHandshakes.end())
    {
        return advanceHandshake(server, pending);
    }

    SocketConnections::iterator i = m_connections.find(s);
    if (i == m_connections.end())
    {
        return false;
    }
    SSLSocketConnection *pSocketConnection = i->second;

    if (pSocketConnection->didProcessQueueRequestToRead())
    {
        pSocketConnection->processQueue();
        pSocketConnection->signal();
    }

    return pSocketConnection->read(*this, server);
}

void SSLSocketAcceptor::onDisconnect(SocketServer &, socket_handle s)
{
    PendingHandshakes::iterator pending = m_pendingHandshakes.find(s);
    if (pending != m_pendingHandshakes.end())
    {
        delete pending->second.connection;
        m_pendingHandshakes.erase(pending);
        return;
    }

    SocketConnections::iterator i = m_connections.find(s);
    if (i == m_connections.end())
    {
        return;
    }
    SSLSocketConnection *pSocketConnection = i->second;

    Session *pSession = pSocketConnection->getSession();
    if (pSession)
    {
        pSession->disconnect();
    }

    delete pSocketConnection;
    m_connections.erase(s);
}

void SSLSocketAcceptor::onError(SocketServer &)
{
    if (getLog())
    {
        std::stringstream stream;
        stream << "acceptor onError " << socket_get_last_error();
        getLog()->onEvent(stream.str());
    }
}

void SSLSocketAcceptor::onTimeout(SocketServer &)
{
    SocketConnections::iterator i;
    for (i = m_connections.begin(); i != m_connections.end(); ++i)
    {
        i->second->onTimeout();
    }
}

int SSLSocketAcceptor::passwordHandleCallback(char *buf, size_t bufsize, int verify)
{
    if (m_password.length() >= bufsize)
    {
        return -1;
    }

    memcpy(buf, m_password.c_str(), m_password.length());
    buf[m_password.length()] = '\0';
    return m_password.length();
}
} // namespace FIX

#endif
