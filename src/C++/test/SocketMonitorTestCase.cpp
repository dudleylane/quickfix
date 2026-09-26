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

#include <SocketMonitor.h>
#include <Utility.h>

#include "catch_amalgamated.hpp"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

using namespace FIX;

namespace
{
// Counts the callbacks one socket receives.
class RecordingStrategy : public SocketMonitor::Strategy
{
public:
    explicit RecordingStrategy(socket_handle watched) : m_watched(watched) {}

    void onConnect(SocketMonitor &, socket_handle) override {}
    void onEvent(SocketMonitor &, socket_handle s) override { events += (s == m_watched); }
    void onWrite(SocketMonitor &, socket_handle) override {}
    void onError(SocketMonitor &, socket_handle s) override { errors += (s == m_watched); }
    void onError(SocketMonitor &) override {}

    int events = 0;
    int errors = 0;

private:
    socket_handle m_watched;
};

// A connected loopback TCP pair: the accepted end first, then the client.
std::pair<int, int> connectedTcpPair()
{
    int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    REQUIRE(::listen(listener, 1) == 0);
    socklen_t length = sizeof(address);
    REQUIRE(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) == 0);

    int client = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(::connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    int accepted = ::accept(listener, nullptr, nullptr);
    REQUIRE(accepted >= 0);
    ::close(listener);
    return {accepted, client};
}
} // namespace

TEST_CASE("SocketMonitorTests")
{
    SocketMonitor monitor;
    int socket = 101;

    SECTION("addWrite_ReadSocketDoesNotExist_False")
    {
        CHECK(!monitor.addWrite(socket));

        socket_close(socket);
    }

    SECTION("addWrite_WriteSocketAlreadyExists_False")
    {
        CHECK(monitor.addRead(socket));
        CHECK(monitor.addWrite(socket));
        CHECK(!monitor.addWrite(socket));

        socket_close(socket);
    }

    SECTION("Unsignal_SocketExists_WriteSocketErased")
    {
        CHECK(monitor.addRead(socket));
        CHECK(monitor.addWrite(socket));
        CHECK(monitor.addConnect(socket));

        monitor.signal(socket);
        monitor.unsignal(socket);

        socket_close(socket);
    }

    SECTION("Unsignal_SocketDoesNotExist_WriteSocketErased")
    {
        CHECK(monitor.addRead(socket));
        CHECK(monitor.addConnect(socket));

        monitor.signal(socket);
        monitor.unsignal(socket);

        socket_close(socket);
    }
}

TEST_CASE("SocketMonitorPeerDisconnectTests")
{
    // Upstream 3a763177 made the monitor report POLLHUP as an error, for peer
    // disconnects it said were being missed. On Linux a socket whose peer has
    // gone is also readable -- tcp_poll and unix_poll set POLLIN whenever the
    // read side is shut down -- so the monitor reports it through onEvent, and
    // the read that follows sees the EOF or the reset. These pin that for each
    // kind of socket the monitor is given: TCP connections, and the AF_UNIX
    // socketpair it wakes itself with (#10).
    SocketMonitor monitor(1);

    SECTION("tcpPeerCloseIsReported")
    {
        auto [accepted, client] = connectedTcpPair();
        REQUIRE(monitor.addRead(accepted));
        ::close(client);

        RecordingStrategy strategy(accepted);
        monitor.block(strategy);
        CHECK(strategy.events + strategy.errors > 0);
    }

    SECTION("tcpPeerResetIsReported")
    {
        auto [accepted, client] = connectedTcpPair();
        REQUIRE(monitor.addRead(accepted));
        linger abortive{1, 0};
        REQUIRE(::setsockopt(client, SOL_SOCKET, SO_LINGER, &abortive, sizeof(abortive)) == 0);
        ::close(client);

        RecordingStrategy strategy(accepted);
        monitor.block(strategy);
        CHECK(strategy.events + strategy.errors > 0);
    }

    SECTION("unixPeerCloseIsReported")
    {
        int pair[2];
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        REQUIRE(monitor.addRead(pair[0]));
        ::close(pair[1]);

        RecordingStrategy strategy(pair[0]);
        monitor.block(strategy);
        CHECK(strategy.events + strategy.errors > 0);
    }
}
