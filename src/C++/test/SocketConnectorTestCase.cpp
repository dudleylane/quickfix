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

#include <fcntl.h>
#include <unistd.h>
#include <vector>

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

    SECTION("dropped_socket_number_is_not_reused_before_the_drop_is_reported")
    {
        // #26. A dropped socket is reported on the next block(), and
        // SocketInitiator::onTimeout connects new sockets in between. A new
        // socket takes the lowest free number -- typically the one just dropped
        // -- so if that number were free already, the queued report would reach
        // the new socket: drop() would close it, and the initiator's
        // onDisconnect would delete the new connection and strand the old one's
        // session.
        SocketServer server(0);
        socket_handle serverSocket = server.add(0, true, true);
        const uint16_t port = socket_hostport(serverSocket);

        SocketConnector connector(1);
        SocketConnectorTestStrategy strategy;
        const socket_handle old = connector.connect("127.0.0.1", port, false, 1024, 1024);
        REQUIRE(old >= 0);
        REQUIRE(server.accept(serverSocket) >= 0);
        for (int i = 0; i < 20 && strategy.connect == 0; ++i)
        {
            connector.block(strategy, true);
        }
        REQUIRE(1 == strategy.connect);

        // Take every free number below old's, so that old's is the lowest free
        // one the moment it is released.
        std::vector<int> fillers;
        for (;;)
        {
            const int fd = ::open("/dev/null", O_RDONLY);
            REQUIRE(fd >= 0);
            if (fd > old)
            {
                ::close(fd);
                break;
            }
            fillers.push_back(fd);
        }

        REQUIRE(connector.getMonitor().drop(old));
        const socket_handle fresh = connector.connect("127.0.0.1", port, false, 1024, 1024);
        REQUIRE(fresh >= 0);
        REQUIRE(server.accept(serverSocket) >= 0);
        for (int i = 0; i < 20 && strategy.connect < 2; ++i)
        {
            connector.block(strategy, true);
        }

        CHECK(fresh != old);
        CHECK(2 == strategy.connect);
        CHECK(1 == strategy.disconnect);
        CHECK(connector.getMonitor().drop(fresh));

        for (int fd : fillers)
        {
            ::close(fd);
        }
        server.close();
    }
}
