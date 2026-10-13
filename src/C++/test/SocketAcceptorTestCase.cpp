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
#include <SocketAcceptor.h>
#include <Utility.h>
#include <chrono>
#include <fix42/Logon.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("SocketAcceptorTests")
{
    TestApplication application;
    MemoryStoreFactory factory;
    socket_handle socket;

    SessionSettings settings;
    std::string input = "[DEFAULT]\n"
                        "ConnectionType=acceptor\n"
                        "SocketAcceptPort=0\n"
                        "SocketReuseAddress=Y\n"
                        "SendBufferSize=1024\n"
                        "ReceiveBufferSize=1024\n"
                        "StartTime=00:00:00\n"
                        "EndTime=00:00:00\n"
                        "UseDataDictionary=N\n"
                        "CheckLatency=N\n"
                        "[SESSION]\n"
                        "BeginString=FIX.4.2\n"
                        "SenderCompID=ISLD\n"
                        "TargetCompID=TW\n"
                        "[SESSION]\n"
                        "BeginString=FIX.4.1\n"
                        "SenderCompID=ISLD\n"
                        "TargetCompID=WT\n";
    std::stringstream stream(input);
    stream >> settings;

    SocketAcceptor object(application, factory, settings);
    object.poll();
    socket = createSocket(object.sessionToPort().find(SessionID("FIX.4.2", "ISLD", "TW"))->second, "127.0.0.1");
    object.poll();

    SECTION("receivePartialMessage")
    {
        std::string firstPart = "8=FIX.4.29=28235=834=2369=31450"
                                "52=20041209-15:35:32.68749=TW50=G56=ISLD"
                                "60=20041209-15:35:3259=055=GE54=148=BLA000060467"
                                "107=BLF5167=FUT44=9740.0041=040=239=038=10"
                                "37=20041209004077151=10150=020=09717=67960"
                                "17=0068712004120909353214=011=000679606=0432=20041209"
                                "1=1234567810=1458=F";

        std::string secondPart = "IX.4.29=34035=834=3369=31450"
                                 "52=20041209-15:35:32.69249=TW50=G56=ISLD"
                                 "60=20041209-15:35:3259=055=GE54=148=CME000060467"
                                 "107=BLF5167=FUT44=9740.0041=040=239=238=10"
                                 "37=20041209004077337=0C032=1031=9740.00151=0150=2"
                                 "20=09717=6796017=00687220041209093532TN0002843"
                                 "75=2004120914=011=00067960375=BLA030A16=0"
                                 "432=200412091=1234567810=217";

        FIX42::Logon logon;
        logon.getHeader().set(SenderCompID("TW"));
        logon.getHeader().set(TargetCompID("ISLD"));
        logon.getHeader().set(MsgSeqNum(1));
        logon.getHeader().set(SendingTime::now());
        logon.set(HeartBtInt(30));

        CHECK(socket_send(socket, logon.toString().c_str(), (int)strlen(logon.toString().c_str())));
        object.poll();
        CHECK(socket_send(socket, firstPart.c_str(), (int)strlen(firstPart.c_str())));
        object.poll();
        CHECK(socket_send(socket, secondPart.c_str(), (int)strlen(secondPart.c_str())));
        object.poll();
    }

    object.stop(true);
    destroySocket(socket);
}

// A logged-on session that is sent nothing still gets its heartbeat on schedule, whatever the
// reactor's other connections are doing.
TEST_CASE("SocketAcceptorHeartbeatTests")
{
    int probe = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(probe, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    socklen_t length = sizeof(address);
    ::getsockname(probe, reinterpret_cast<sockaddr *>(&address), &length);
    ::close(probe);
    const uint16_t port = ntohs(address.sin_port);

    std::stringstream input;
    input << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
          << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nCheckLatency=N\n"
          << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ISLD\nTargetCompID=TW\n";
    SessionSettings settings(input);
    TestApplication application;
    MemoryStoreFactory factory;
    SocketAcceptor object(application, factory, settings);
    object.poll();

    socket_handle socket = createSocket(port, "127.0.0.1");
    socket_handle other = createSocket(port, "127.0.0.1");
    REQUIRE(socket != INVALID_SOCKET_HANDLE);
    REQUIRE(other != INVALID_SOCKET_HANDLE);

    FIX42::Logon logon;
    logon.getHeader().set(SenderCompID("TW"));
    logon.getHeader().set(TargetCompID("ISLD"));
    logon.getHeader().set(MsgSeqNum(1));
    logon.getHeader().set(SendingTime::now());
    logon.set(HeartBtInt(1));
    const std::string logonString = logon.toString();
    CHECK(socket_send(socket, logonString.c_str(), (int)logonString.size()));

    std::string received;
    char buffer[4096];
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4) &&
           received.find("\00135=0\001") == std::string::npos)
    {
        socket_send(other, "x", 1);
        object.poll();
        ssize_t size;
        while ((size = ::recv(socket, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0)
        {
            received.append(buffer, size);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    INFO(received);
    CHECK(received.find("\00135=A\001") != std::string::npos);
    CHECK(received.find("\00135=0\001") != std::string::npos);

    object.stop(true);
    destroySocket(other);
    destroySocket(socket);
}
