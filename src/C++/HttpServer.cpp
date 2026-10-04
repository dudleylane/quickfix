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

#include "HttpConnection.h"
#include "HttpServer.h"
#include "Settings.h"
#include "Utility.h"

namespace FIX
{
Mutex HttpServer::s_mutex;
int HttpServer::s_count = 0;
HttpServer *HttpServer::s_pServer = 0;

void HttpServer::startGlobal(const SessionSettings &s) EXCEPT(ConfigError, RuntimeError)
{
    Locker l(s_mutex);

    if (!s.get().has(HTTP_ACCEPT_PORT))
    {
        return;
    }

    if (!s_pServer)
    {
        HttpServer *pServer = new HttpServer(s);
        try
        {
            pServer->start();
        }
        catch (...)
        {
            delete pServer;
            throw;
        }
        s_pServer = pServer;
    }
    s_count += 1;
}

void HttpServer::stopGlobal(const SessionSettings &s)
{
    Locker l(s_mutex);

    // Only settings that counted in startGlobal -- those with an HttpAcceptPort
    // -- are uncounted here. Others must not stop a server that another
    // acceptor or initiator started.
    if (!s.get().has(HTTP_ACCEPT_PORT) || s_count == 0)
    {
        return;
    }
    s_count -= 1;
    if (!s_count && s_pServer)
    {
        s_pServer->stop();
        delete s_pServer;
        s_pServer = 0;
    }
}

HttpServer::HttpServer(const SessionSettings &settings) EXCEPT(ConfigError)
    : m_pServer(0), m_settings(settings), m_threadid(0), m_port(0), m_stop(false)
{
}

void HttpServer::onConfigure(const SessionSettings &s) EXCEPT(ConfigError)
{
    m_port = s.get().getInt(HTTP_ACCEPT_PORT);
    // The server has no authentication, so by default only this host reaches it.
    m_address = s.get().has(HTTP_ACCEPT_ADDRESS) ? s.get().getString(HTTP_ACCEPT_ADDRESS) : "127.0.0.1";
}

void HttpServer::onInitialize(const SessionSettings &s) EXCEPT(RuntimeError)
{
    try
    {
        m_pServer = new SocketServer(1);
        m_pServer->add(m_address, m_port, true);
    }
    catch (std::exception &)
    {
        delete m_pServer;
        m_pServer = 0;
        throw RuntimeError("Unable to create, bind, or listen to " + m_address + " port " +
                           IntConvertor::convert((unsigned short)m_port));
    }
}

void HttpServer::start() EXCEPT(ConfigError, RuntimeError)
{
    m_stop = false;
    onConfigure(m_settings);
    onInitialize(m_settings);

    if (!thread_spawn(&startThread, this, m_threadid))
    {
        throw RuntimeError("Unable to spawn thread");
    }
}

void HttpServer::stop()
{
    if (m_stop)
    {
        return;
    }
    m_stop = true;
    onStop();

    if (m_threadid)
    {
        thread_join(m_threadid);
    }
    m_threadid = 0;
}

void HttpServer::onStart()
{
    while (!m_stop && m_pServer && m_pServer->block(*this))
    {
    }

    if (!m_pServer)
    {
        return;
    }

    m_pServer->close();
    delete m_pServer;
    m_pServer = 0;
}

bool HttpServer::onPoll()
{
    if (!m_pServer || m_stop)
    {
        return false;
    }

    m_pServer->block(*this, true);
    return true;
}

void HttpServer::onStop() {}

void HttpServer::onConnect(SocketServer &server, socket_handle a, socket_handle s)
{
    if (!socket_isValid(s))
    {
        return;
    }
    HttpConnection connection(s);
    while (connection.read())
    {
    }
    m_pServer->getMonitor().drop(s);
}

void HttpServer::onWrite(SocketServer &server, socket_handle s) {}

bool HttpServer::onData(SocketServer &server, socket_handle s) { return true; }

void HttpServer::onDisconnect(SocketServer &, socket_handle s) {}

void HttpServer::onError(SocketServer &) {}

void HttpServer::onTimeout(SocketServer &) {}

THREAD_PROC HttpServer::startThread(void *p)
{
    HttpServer *pServer = static_cast<HttpServer *>(p);
    pServer->onStart();
    return 0;
}

} // namespace FIX
