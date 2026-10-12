#include "config.h"

#if (HAVE_SSL > 0)

#include <Application.h>
#include <FixFields.h>
#include <FixValues.h>
#include <Message.h>
#include <MessageStore.h>
#include <SSLSocketAcceptor.h>
#include <SessionSettings.h>
#include <UtilitySSL.h>

#include "TestHelper.h"
#include "catch_amalgamated.hpp"

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace FIX;

// The reactor's TLS acceptor steps each handshake as its socket becomes ready,
// alongside every other connection, and bounds how long a handshake may take.

namespace
{
std::string certPath(const std::string &leaf) { return TestSettings::specPath + "/../bin/cfg/certs/" + leaf; }

int freePort()
{
    int probe = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(probe, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    socklen_t length = sizeof(address);
    ::getsockname(probe, reinterpret_cast<sockaddr *>(&address), &length);
    ::close(probe);
    return ntohs(address.sin_port);
}

int connectTo(int port)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        int client = ::socket(AF_INET, SOCK_STREAM, 0);
        if (::connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0)
        {
            return client;
        }
        ::close(client);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return -1;
}

SessionSettings acceptorSettings(int port)
{
    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
           << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
           << "ServerCertificateFile=" << certPath("127_0_0_1_server.crt") << "\n"
           << "ServerCertificateKeyFile=" << certPath("127_0_0_1_server.key") << "\n"
           << "SSLProtocol=all\n"
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=SSLACCEPTOR\nTargetCompID=TW\n";
    return SessionSettings(config);
}

std::chrono::milliseconds since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
}
} // namespace

TEST_CASE("SSLSocketAcceptorTests")
{
    NullApplication application;
    MemoryStoreFactory factory;

    SECTION("handshakesAreSteppedAlongsideOtherConnections")
    {
        const int port = freePort();
        SSLSocketAcceptor acceptor(application, factory, acceptorSettings(port));
        REQUIRE(acceptor.poll());

        // A connection whose handshake has not begun.
        const int first = connectTo(port);
        REQUIRE(first >= 0);
        for (int pass = 0; pass < 5; ++pass)
        {
            const auto start = std::chrono::steady_clock::now();
            acceptor.poll();
            CHECK(since(start) < std::chrono::milliseconds(1000));
        }

        // Another client completes its handshake meanwhile.
        const int second = connectTo(port);
        REQUIRE(second >= 0);
        SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
        SSL *ssl = SSL_new(ctx);
        REQUIRE(::fcntl(second, F_SETFL, ::fcntl(second, F_GETFL) | O_NONBLOCK) == 0);
        SSL_set_fd(ssl, second);
        bool connected = false;
        const auto start = std::chrono::steady_clock::now();
        for (int pass = 0; pass < 200 && !connected && since(start) < std::chrono::milliseconds(5000); ++pass)
        {
            connected = SSL_connect(ssl) == 1;
            acceptor.poll();
        }
        CHECK(connected);

        // And the first is still being served, not dropped.
        char byte = 0;
        errno = 0;
        CHECK(::recv(first, &byte, 1, MSG_DONTWAIT) == -1);
        CHECK(errno == EAGAIN);

        SSL_free(ssl);
        SSL_CTX_free(ctx);
        ::close(second);
        ::close(first);
        acceptor.stop(true);
    }

    SECTION("stopIsPromptWithAHandshakeOutstanding")
    {
        const int port = freePort();
        SSLSocketAcceptor acceptor(application, factory, acceptorSettings(port));
        acceptor.start();
        const int client = connectTo(port);
        REQUIRE(client >= 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        const auto start = std::chrono::steady_clock::now();
        acceptor.stop(true);
        CHECK(since(start) < std::chrono::milliseconds(3000));
        ::close(client);
    }

    SECTION("aSessionLogsOnThroughAPortAbove32767")
    {
        // #119. SocketServer reported a port above 32767 negative, so the acceptor
        // found no sessions for the connection and refused the Logon.
        int port = freePort();
        for (int attempt = 0; port <= 32767 && attempt < 100; ++attempt)
        {
            port = freePort();
        }
        REQUIRE(port > 32767);
        SSLSocketAcceptor acceptor(application, factory, acceptorSettings(port));
        REQUIRE(acceptor.poll());

        const int client = connectTo(port);
        REQUIRE(client >= 0);
        SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
        SSL *ssl = SSL_new(ctx);
        REQUIRE(::fcntl(client, F_SETFL, ::fcntl(client, F_GETFL) | O_NONBLOCK) == 0);
        SSL_set_fd(ssl, client);
        const auto start = std::chrono::steady_clock::now();
        bool connected = false;
        while (!connected && since(start) < std::chrono::milliseconds(5000))
        {
            connected = SSL_connect(ssl) == 1;
            acceptor.poll();
        }
        REQUIRE(connected);

        Message logon;
        logon.getHeader().setField(BeginString(BeginString_FIX42));
        logon.getHeader().setField(MsgType(MsgType_Logon));
        logon.getHeader().setField(SenderCompID("TW"));
        logon.getHeader().setField(TargetCompID("SSLACCEPTOR"));
        logon.getHeader().setField(MsgSeqNum(1));
        logon.getHeader().setField(SendingTime(UtcTimeStamp::now()));
        logon.setField(EncryptMethod(0));
        logon.setField(HeartBtInt(30));
        const std::string request = logon.toString();
        bool sent = false;
        while (!sent && since(start) < std::chrono::milliseconds(5000))
        {
            sent = SSL_write(ssl, request.data(), static_cast<int>(request.size())) == static_cast<int>(request.size());
            acceptor.poll();
        }
        REQUIRE(sent);

        std::string reply;
        char buffer[512];
        while (reply.find("\00135=A\001") == std::string::npos && since(start) < std::chrono::milliseconds(5000))
        {
            acceptor.poll();
            const int count = SSL_read(ssl, buffer, sizeof(buffer));
            if (count > 0)
            {
                reply.append(buffer, count);
            }
            else if (SSL_get_error(ssl, count) != SSL_ERROR_WANT_READ)
            {
                break;
            }
        }
        CHECK(reply.find("\00135=A\001") != std::string::npos);

        SSL_free(ssl);
        SSL_CTX_free(ctx);
        ::close(client);
        acceptor.stop(true);
    }
}

TEST_CASE("CheckSSLClientTests")
{
    // checkSSLClient() is the acceptors' verdict on a client whose handshake has
    // completed; these pin the values its callers compare with != 0.
    SSL_CTX *ctx = SSL_CTX_new(TLS_method());
    SSL *ssl = SSL_new(ctx);

    SECTION("aFailedVerificationIsReturned")
    {
        SSL_set_verify_result(ssl, X509_V_ERR_CERT_HAS_EXPIRED);
        CHECK(X509_V_ERR_CERT_HAS_EXPIRED == checkSSLClient(ssl, nullptr, SSL_CLIENT_VERIFY_OPTIONAL));
    }

    SECTION("aMissingCertificateFailsOnlyWhenRequired")
    {
        SSL_set_verify_result(ssl, X509_V_OK);
        CHECK(2 == checkSSLClient(ssl, nullptr, SSL_CLIENT_VERIFY_REQUIRE));
        CHECK(0 == checkSSLClient(ssl, nullptr, SSL_CLIENT_VERIFY_OPTIONAL));
        CHECK(0 == checkSSLClient(ssl, nullptr, SSL_CLIENT_VERIFY_NOTSET));
    }

    SSL_free(ssl);
    SSL_CTX_free(ctx);
}

TEST_CASE("SSLSocketAcceptorRestartTests")
{
    SECTION("aStartThatFailsToBindCanBeRetried")
    {
        // Two sessions on two ports; the second port is taken, so start() binds the first and fails
        // on the second. That must release the first, so a start() retried once the port is free
        // binds both instead of failing on a listener left over from the first attempt (#60).
        const int first = freePort();
        int second = freePort();
        while (second == first)
        {
            second = freePort();
        }
        std::stringstream config;
        config << "[DEFAULT]\nConnectionType=acceptor\nStartTime=00:00:00\nEndTime=00:00:00\n"
               << "UseDataDictionary=N\nSocketReuseAddress=N\n"
               << "ServerCertificateFile=" << certPath("127_0_0_1_server.crt") << "\n"
               << "ServerCertificateKeyFile=" << certPath("127_0_0_1_server.key") << "\n"
               << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=RESTART1\nTargetCompID=TW\nSocketAcceptPort=" << first
               << "\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=RESTART2\nTargetCompID=TW\nSocketAcceptPort="
               << second << "\n";
        SessionSettings settings(config);
        NullApplication application;
        MemoryStoreFactory factory;
        SSLSocketAcceptor acceptor(application, factory, settings);

        const socket_handle occupier = socket_createAcceptor(second, false);
        REQUIRE(occupier != INVALID_SOCKET_HANDLE);
        CHECK_THROWS_AS(acceptor.start(), RuntimeError);
        socket_close(occupier);

        CHECK_NOTHROW(acceptor.start());
        CHECK(connectTo(first) >= 0);
        CHECK(connectTo(second) >= 0);
        acceptor.stop(true);
    }
}

#endif
