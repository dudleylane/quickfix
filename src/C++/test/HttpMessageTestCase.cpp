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

#include <HttpConnection.h>
#include <HttpMessage.h>
#include <HttpServer.h>
#include <SessionSettings.h>
#include <Utility.h>

#include "catch_amalgamated.hpp"

#include <arpa/inet.h>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

using namespace FIX;

TEST_CASE("HttpMessageTests")
{
    SECTION("setString")
    {
        HttpMessage object;

        static const char *strGood1 = "GET / HTTP/1.0\r\nContent Type: text/html\r\n\r\n";
        static const char *strGood2 = "GET /another/url HTTP/1.0\r\nContent Type: text/html\r\n\r\n";
        static const char *strGoodWithParameters =
            "GET /some/url?p1=one&p2=two&p3=three HTTP/1.0\r\nContent Type: text/html\r\n\r\n";
        static const char *strGoodWithEmptyParameters =
            "GET /some/url?p1=&p2=&p3= HTTP/1.0\r\nContent Type: text/html\r\n\r\n";
        static const char *strBad1 = "GEB /some/url HTTP/1.0\r\nContent Type: text/html\r\n\r\n";
        static const char *strBad2 = "GET /some/url HTTB/1.0\r\nContent Type: text/html\r\n\r\n";
        static const char *strBad3 = "GEB /some/url HTTB/1.0\r\nContent Type: text/html\r\n\r\n";

        object.setString(strGood1);
        CHECK("/" == object.getRootString());
        CHECK(0U == object.getParameters().size());

        object.setString(strGood2);
        CHECK("/another/url" == object.getRootString());
        CHECK(0U == object.getParameters().size());

        object.setString(strGoodWithParameters);
        CHECK("/some/url" == object.getRootString());
        CHECK(3U == object.getParameters().size());
        CHECK("one" == object.getParameters().find("p1")->second);
        CHECK("two" == object.getParameters().find("p2")->second);
        CHECK("three" == object.getParameters().find("p3")->second);

        object.setString(strGoodWithEmptyParameters);
        CHECK("/some/url" == object.getRootString());
        CHECK(3U == object.getParameters().size());
        CHECK("" == object.getParameters().find("p1")->second);
        CHECK("" == object.getParameters().find("p2")->second);
        CHECK("" == object.getParameters().find("p3")->second);

        CHECK_THROWS_AS(object.setString(strBad1), InvalidMessage);
        CHECK_THROWS_AS(object.setString(strBad2), InvalidMessage);
        CHECK_THROWS_AS(object.setString(strBad3), InvalidMessage);
    }
}

namespace
{
HttpMessage request(const std::string &method, const std::string &target, const std::string &headers)
{
    return HttpMessage(method + " " + target + " HTTP/1.1\r\n" + headers + "\r\n");
}

int freePort()
{
    const socket_handle probe = socket_createAcceptor(0, true);
    const int port = socket_hostport(probe);
    socket_close(probe);
    return port;
}

std::string exchange(int port, const std::string &text)
{
    const socket_handle s = socket_createConnector();
    if (socket_connect(s, "127.0.0.1", port) < 0)
    {
        socket_close(s);
        return "";
    }
    socket_send(s, text.c_str(), static_cast<int>(text.size()));
    std::string response;
    char buffer[4096];
    ssize_t size;
    while ((size = socket_recv(s, buffer, sizeof(buffer))) > 0)
    {
        response.append(buffer, static_cast<size_t>(size));
    }
    socket_close(s);
    return response;
}

SessionSettings settingsFrom(const std::string &defaults)
{
    std::istringstream stream("[DEFAULT]\n" + defaults);
    return SessionSettings(stream);
}
} // namespace

TEST_CASE("HttpRequestCheckTests")
{
    SECTION("methodAndCheckedHeadersAreParsed")
    {
        const HttpMessage post =
            request("POST", "/resetSessions?confirm=1", "host: 127.0.0.1:9911\r\nOrigin:  http://127.0.0.1:9911\r\n");
        CHECK("POST" == post.getMethod());
        CHECK("/resetSessions" == post.getRootString());
        CHECK("127.0.0.1:9911" == post.getHost());
        CHECK("http://127.0.0.1:9911" == post.getOrigin());
        CHECK_THROWS_AS(HttpMessage("PUT / HTTP/1.1\r\n\r\n"), InvalidMessage);
    }

    SECTION("pagesThatOnlyReadAreServedOverGet")
    {
        CHECK(0 == HttpConnection::checkRequest(request("GET", "/", "Host: 127.0.0.1:9911\r\n")));
        CHECK(0 == HttpConnection::checkRequest(request("GET", "/resetSessions", "Host: localhost:9911\r\n")));
        CHECK(0 == HttpConnection::checkRequest(request(
                       "GET", "/session?BeginString=FIX.4.2&SenderCompID=A&TargetCompID=B", "Host: [::1]:9911\r\n")));
        CHECK(0 == HttpConnection::checkRequest(request("GET", "/resetSessions?confirm=0", "Host: 127.0.0.1\r\n")));
    }

    SECTION("stateChangesNeedAPost")
    {
        const std::string host = "Host: 127.0.0.1:9911\r\n";
        CHECK(405 == HttpConnection::checkRequest(request("GET", "/resetSessions?confirm=1", host)));
        CHECK(405 == HttpConnection::checkRequest(request("GET", "/disableSessions?confirm=1", host)));
        CHECK(405 == HttpConnection::checkRequest(
                         request("GET", "/session?BeginString=FIX.4.2&SenderCompID=A&TargetCompID=B&Enabled=0", host)));
        CHECK(405 == HttpConnection::checkRequest(request(
                         "GET", "/session?BeginString=FIX.4.2&SenderCompID=A&TargetCompID=B&Next%20Incoming=1", host)));
        CHECK(0 == HttpConnection::checkRequest(request("POST", "/resetSessions?confirm=1", host)));
        CHECK(0 == HttpConnection::checkRequest(
                       request("POST", "/resetSessions?confirm=1", host + "Origin: http://127.0.0.1:9911\r\n")));
    }

    SECTION("otherOriginsAndHostNamesAreRefused")
    {
        CHECK(403 == HttpConnection::checkRequest(request("POST", "/resetSessions?confirm=1",
                                                          "Host: 127.0.0.1:9911\r\nOrigin: http://example.com\r\n")));
        CHECK(403 == HttpConnection::checkRequest(request("GET", "/", "Host: example.com:9911\r\n")));
        CHECK(403 == HttpConnection::checkRequest(request("GET", "/", "")));
    }
}

TEST_CASE("HttpServerTests")
{
    SECTION("aConnectionLeavesItsSocketToTheServer")
    {
        int pair[2];
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        const std::string text = "GET /nothing HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        REQUIRE(::write(pair[1], text.c_str(), text.size()) == static_cast<ssize_t>(text.size()));

        HttpConnection connection(pair[0]);
        int reads = 0;
        while (connection.read())
        {
            ++reads;
        }
        CHECK(reads == 0);                      // one request, answered in the first read
        CHECK(::fcntl(pair[0], F_GETFD) != -1); // still open: the server's monitor closes it
        char buffer[64] = {};
        CHECK(::read(pair[1], buffer, sizeof(buffer) - 1) > 0);
        CHECK(std::string(buffer).find(" 404 ") != std::string::npos);
        ::close(pair[0]);
        ::close(pair[1]);
    }

    SECTION("listensOnLoopbackByDefault")
    {
        const int port = freePort();
        HttpServer::startGlobal(settingsFrom("HttpAcceptPort=" + std::to_string(port) + "\n"));
        const std::string response = exchange(port, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        CHECK(response.find(" 200 ") != std::string::npos);

        // The listening socket's local address is 127.0.0.1, not the wildcard.
        // /proc/net/tcp lists it as local_address with state 0A (LISTEN).
        std::string listener;
        std::ifstream tcp("/proc/net/tcp");
        std::string line;
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), ":%04X", port);
        while (std::getline(tcp, line))
        {
            std::istringstream fields(line);
            std::string slot, local, remote, state;
            fields >> slot >> local >> remote >> state;
            if (state == "0A" && local.size() > 5 && local.compare(local.size() - 5, 5, suffix) == 0)
            {
                listener = local;
            }
        }
        CHECK(listener == std::string("0100007F") + suffix);

        CHECK(exchange(port, "GET /resetSessions?confirm=1 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").find(" 405 ") !=
              std::string::npos);
        HttpServer::stopGlobal(settingsFrom("HttpAcceptPort=" + std::to_string(port) + "\n"));
    }

    SECTION("stoppingWithoutAPortLeavesAnotherServerRunning")
    {
        const int port = freePort();
        const SessionSettings withPort = settingsFrom("HttpAcceptPort=" + std::to_string(port) + "\n");
        const SessionSettings withoutPort = settingsFrom("");
        HttpServer::startGlobal(withPort);
        HttpServer::startGlobal(withoutPort);
        HttpServer::stopGlobal(withoutPort);
        CHECK(exchange(port, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").find(" 200 ") != std::string::npos);
        HttpServer::stopGlobal(withPort);
        CHECK(exchange(port, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").empty());
    }
}
