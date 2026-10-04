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

#ifndef FIX_HTTPCONNECTION_H
#define FIX_HTTPCONNECTION_H

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#endif

#include "HttpParser.h"
#include <stdio.h>

namespace FIX
{
class HttpMessage;

/// Encapsulates a HTTP socket file descriptor
class HttpConnection
{
public:
    HttpConnection(socket_handle s);

    socket_handle getSocket() const { return m_socket; }
    /// Serves one request; false once it has been answered or the peer has gone.
    bool read();

    /// 0 if the request may be processed, else the HTTP status refusing it:
    /// 403 for a Host that is a DNS name other than localhost, 405 for a state
    /// change requested other than by POST, 403 for a POST from another origin.
    static int checkRequest(const HttpMessage &);

private:
    bool readMessage(std::string &msg) EXCEPT(SocketRecvFailed);
    void processStream();
    void processRequest(const HttpMessage &);
    void processRoot(const HttpMessage &, std::stringstream &h, std::stringstream &b);
    void processResetSessions(const HttpMessage &, std::stringstream &h, std::stringstream &b);
    void processRefreshSessions(const HttpMessage &, std::stringstream &h, std::stringstream &b);
    void processEnableSessions(const HttpMessage &, std::stringstream &h, std::stringstream &b);
    void processDisableSessions(const HttpMessage &, std::stringstream &h, std::stringstream &b);
    void processSession(const HttpMessage &, std::stringstream &h, std::stringstream &b);
    void processResetSession(const HttpMessage &, std::stringstream &h, std::stringstream &b);
    void processRefreshSession(const HttpMessage &, std::stringstream &h, std::stringstream &b);

    void showToggle(std::stringstream &s, const std::string &name, bool value, const std::string &url);
    void showRow(std::stringstream &s, const std::string &name, bool value, const std::string &url = "");
    void showRow(std::stringstream &s, const std::string &name, const std::string &value, const std::string &url = "");
    void showRow(std::stringstream &s, const std::string &name, int value, const std::string &url = "");

    bool send(const std::string &);
    void disconnect(int error = 0);

    socket_handle m_socket;
    bool m_done = false;
    char m_buffer[BUFSIZ];

    HttpParser m_parser;
#if _MSC_VER
    fd_set m_fds;
#endif
};
} // namespace FIX

#endif
