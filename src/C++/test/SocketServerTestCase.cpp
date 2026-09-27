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
#include <SocketServer.h>
#ifdef _MSC_VER
#include <stdlib.h>
#endif

#include "catch_amalgamated.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace FIX;

struct TestStrategy : public SocketServer::Strategy
{
    void onConnect(SocketServer &, socket_handle accept, socket_handle socket)
    {
        connect++;
        connectSocket = socket;
    }
    void onWrite(SocketServer &, socket_handle socket)
    {
        write++;
        writeSocket = socket;
    }
    bool onData(SocketServer &server, socket_handle socket)
    {
        data++;
        dataSocket = socket;
        bufLen = recv(socket, buf, 1, 0);
        return bufLen > 0;
    }
    void onDisconnect(SocketServer &, socket_handle socket)
    {
        disconnect++;
        disconnectSocket = socket;
    }
    void onError(SocketServer &) {}

    int connect = 0;
    int write = 0;
    int data = 0;
    int disconnect = 0;
    socket_handle connectSocket = 0;
    socket_handle writeSocket = 0;
    socket_handle dataSocket = 0;
    socket_handle disconnectSocket = 0;
    char buf[1];
    size_t bufLen = 0;
};

TEST_CASE("SocketServerTests")
{
    TestStrategy strategy;

    SECTION("accept")
    {
        SocketServer object(0);
        socket_handle serverS1 = object.add(0, true, true);
        socket_handle clientS1 = createSocket(socket_hostport(serverS1), "127.0.0.1");
        CHECK(clientS1 > 0);
        process_sleep(0.1);
        socket_handle s1 = object.accept(serverS1);
        CHECK(s1 >= 0);
        object.block(strategy);
        CHECK(object.numConnections() == 1);

        socket_handle clientS2 = createSocket(socket_hostport(serverS1), "127.0.0.1");
        CHECK(clientS2 > 0);
        process_sleep(0.1);
        socket_handle s2 = object.accept(serverS1);
        CHECK(s2 >= 0);
        object.block(strategy);
        CHECK(object.numConnections() == 2);

        socket_handle clientS3 = createSocket(socket_hostport(serverS1), "127.0.0.1");
        CHECK(clientS3 > 0);
        process_sleep(0.1);
        socket_handle s3 = object.accept(serverS1);
        CHECK(s3 >= 0);
        object.block(strategy);
        CHECK(object.numConnections() == 3);

        SocketMonitor &monitor = object.getMonitor();
        CHECK(!monitor.drop(-1));
        CHECK(object.numConnections() == 3);
        CHECK(monitor.drop(s1));
        CHECK(object.numConnections() == 2);
        CHECK(monitor.drop(s2));
        CHECK(object.numConnections() == 1);
        CHECK(monitor.drop(s3));
        CHECK(object.numConnections() == 0);

        destroySocket(clientS1);
        destroySocket(clientS2);
        destroySocket(clientS3);
    }

    SECTION("block")
    {
        SocketServer object(0);
        socket_handle serverS = object.add(0, true, true);
        socket_handle clientS = createSocket(socket_hostport(serverS), "127.0.0.1");
        CHECK(clientS >= 0);

        process_sleep(0.1);
        object.block(strategy);
        CHECK(1 == strategy.connect);
        CHECK(strategy.connectSocket > 0);

        send(clientS, "1", 1, 0);
        process_sleep(0.1);
        object.block(strategy);
        object.block(strategy);
        CHECK(1 == strategy.data);
        CHECK(1U == strategy.bufLen);
        CHECK('1' == *strategy.buf);
        CHECK(strategy.dataSocket > 0);

        destroySocket(clientS);
        process_sleep(0.1);
        object.block(strategy);
        CHECK(1 == strategy.disconnect);
        CHECK(strategy.disconnectSocket > 0);
    }

    SECTION("close")
    {
        SocketServer object(0);
        object.add(0, true, true);
        object.close();
        object.block(strategy);
    }

    SECTION("droppedSocketNumberIsNotReusedBeforeTheDropIsReported")
    {
        // #26. A connection that fails in a poll pass is dropped there, and the
        // drop is reported on the next block(). An accept later in the same pass
        // takes the lowest free number; if the dropped one were free already,
        // the report would tear down the connection just accepted. The failing
        // socket is arranged to sit below the listener, so the pass reaches it
        // first.
        const int placeholder = ::open("/dev/null", O_RDONLY);
        REQUIRE(placeholder >= 0);
        SocketServer object(0);
        socket_handle serverS = object.add(0, true, true);
        REQUIRE(serverS > placeholder);
        const int port = socket_hostport(serverS);

        socket_handle first = createSocket(port, "127.0.0.1");
        REQUIRE(first >= 0);
        ::close(placeholder);
        process_sleep(0.1);
        object.block(strategy); // accepts first, below the listener
        const socket_handle failing = strategy.connectSocket;
        REQUIRE(failing < serverS);
        object.block(strategy); // and moves it to the read set

        destroySocket(first);
        socket_handle second = createSocket(port, "127.0.0.1");
        REQUIRE(second >= 0);
        process_sleep(0.1);
        object.block(strategy, true); // one pass: EOF on the failing socket, then the accept
        CHECK(strategy.connectSocket != failing);

        object.block(strategy, true); // the drop is reported
        char byte = 0;
        errno = 0;
        CHECK(::recv(second, &byte, 1, MSG_DONTWAIT) == -1); // no EOF: the new connection survived
        CHECK(errno == EAGAIN);

        destroySocket(second);
        object.close();
    }
}