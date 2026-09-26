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

#include "TestHelper.h"
#include <SocketConnector.h>
#include <SocketServer.h>
#ifdef _MSC_VER
#include <stdlib.h>
#endif

#include "catch_amalgamated.hpp"

using namespace FIX;

// Named for this file: SocketServerTestCase.cpp already defines a global
// TestStrategy, and two global classes of one name in different translation
// units are an ODR violation the linker resolves silently.
struct SocketConnectorTestStrategy : public SocketConnector::Strategy
{
    void onConnect(SocketConnector &, socket_handle) override { connect++; }
    void onWrite(SocketConnector &, socket_handle) override {}
    bool onData(SocketConnector &, socket_handle) override { return true; }
    void onDisconnect(SocketConnector &, socket_handle) override { disconnect++; }
    void onError(SocketConnector &) override {}

    int connect = 0;
    int disconnect = 0;
};

TEST_CASE("SocketConnectorTests")
{
    SECTION("accept")
    {
        SocketConnector object;
        SocketServer server(0);
        // Port 0 lets the OS pick a free one; binding a fixed port collides
        // with anything else on the machine using it, including the
        // acceptance suite and a concurrent CI run.
        socket_handle socket = server.add(0, true, true);
        const uint16_t port = socket_hostport(socket);
        CHECK(object.connect("127.0.0.1", port, false, 1024, 1024));
        CHECK(server.accept(socket));
        server.close();
    }

    SECTION("connect_to_dead_port_fires_disconnect_once")
    {
        // Adapted from upstream 30622fa6. Bind to get a free port, then close
        // it so nothing is listening and the connect is refused.
        SocketServer server(0);
        socket_handle serverSocket = server.add(0, true, true);
        const uint16_t port = socket_hostport(serverSocket);
        server.close();

        SocketConnector connector(1);
        SocketConnectorTestStrategy strategy;
        connector.connect("127.0.0.1", port, false, 1024, 1024);

        // Upstream asserts the notification after the first block(); here it
        // arrives from the dropped-socket queue on the one after, so the
        // assertion is exactly once across several. If the failed socket were
        // never dropped, poll() would report it on every block() -- a CPU spin
        // that re-fires onDisconnect and never closes the fd.
        process_sleep(0.1);
        for (int i = 0; i < 4; ++i)
        {
            connector.block(strategy, true);
        }
        CHECK(1 == strategy.disconnect);
    }

    SECTION("self_dropped_connection_is_reported_once")
    {
        // A connection drops its own socket when its session ends -- logout, a
        // heartbeat timeout -- via Session::disconnect -> the responder's
        // disconnect() -> SocketMonitor::drop. The dropped-socket queue is the
        // only way the connector's strategy then hears of it, so that report
        // must not be suppressed: the initiator's onDisconnect is what deletes
        // the connection and marks the session disconnected so it reconnects.
        SocketServer server(0);
        socket_handle serverSocket = server.add(0, true, true);
        const uint16_t port = socket_hostport(serverSocket);

        SocketConnector connector(1);
        SocketConnectorTestStrategy strategy;
        const socket_handle socket = connector.connect("127.0.0.1", port, false, 1024, 1024);
        REQUIRE(socket >= 0);
        CHECK(server.accept(serverSocket));

        for (int i = 0; i < 20 && strategy.connect == 0; ++i)
        {
            connector.block(strategy, true);
        }
        REQUIRE(1 == strategy.connect);

        REQUIRE(connector.getMonitor().drop(socket)); // what the connection's disconnect() does
        for (int i = 0; i < 4; ++i)
        {
            connector.block(strategy, true);
        }
        CHECK(1 == strategy.disconnect);

        server.close();
    }
}
