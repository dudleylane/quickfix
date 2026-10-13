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

#ifndef FIX_SOCKETINITIATOR_H
#define FIX_SOCKETINITIATOR_H

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#endif

#include "HostDetailsProvider.h"
#include "Initiator.h"
#include "SocketConnection.h"
#include "SocketConnector.h"

namespace FIX
{
/// Socket implementation of Initiator.
class SocketInitiator : public Initiator, SocketConnector::Strategy
{
public:
    SocketInitiator(Application &, MessageStoreFactory &, const SessionSettings &) EXCEPT(ConfigError);
    SocketInitiator(Application &, MessageStoreFactory &, const SessionSettings &, LogFactory &) EXCEPT(ConfigError);

    virtual ~SocketInitiator();

private:
    typedef std::map<socket_handle, SocketConnection *> SocketConnections;

    void onConfigure(const SessionSettings &) EXCEPT(ConfigError);
    void onInitialize(const SessionSettings &) EXCEPT(RuntimeError);

    void onStart();
    bool onPoll();
    void onStop();

    void doConnect(const SessionID &, const Dictionary &d);
    void onConnect(SocketConnector &, socket_handle);
    void onWrite(SocketConnector &, socket_handle);
    bool onData(SocketConnector &, socket_handle);
    void onDisconnect(SocketConnector &, socket_handle);
    void onError(SocketConnector &);
    void onTimeout(SocketConnector &);

    HostDetailsProvider m_hostDetailsProvider;
    SocketConnector m_connector;
    SocketConnections m_pendingConnections;
    SocketConnections m_connections;
    // Unused since #125: each session's reconnect timing is in Initiator. Kept
    // so the layout does not change before the next soname bump.
    time_t m_lastConnect;
    int m_reconnectInterval;
    bool m_noDelay;    // unused since #150, which reads SocketNodelay per session; kept for the class layout
    int m_sendBufSize; // unused since #170, which reads it per session; kept for the class layout
    int m_rcvBufSize;  // unused since #170, which reads it per session; kept for the class layout
};
/*! @} */
} // namespace FIX

#endif // FIX_SOCKETINITIATOR_H
