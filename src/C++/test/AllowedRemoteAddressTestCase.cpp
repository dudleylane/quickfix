#include "config.h"

#include <Application.h>
#include <FixFields.h>
#include <FixValues.h>
#include <Message.h>
#include <MessageStore.h>
#include <Session.h>
#include <SessionSettings.h>
#include <SocketAcceptor.h>
#include <ThreadedSocketAcceptor.h>
#if (HAVE_SSL > 0)
#include <SSLSocketAcceptor.h>
#include <ThreadedSSLSocketAcceptor.h>
#include <UtilitySSL.h>
#endif

#include "TestHelper.h"
#include "catch_amalgamated.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

using namespace FIX;

// An acceptor refuses a peer outside AllowedRemoteAddresses before the peer's
// first message reaches the session, so the application never sees it and the
// session's state does not move (GHSA-2ppv, 6347ee9d). Nothing drove that
// through a connection on any of the four acceptors (#128).

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

// Connects from 127.0.0.1, with reads timing out after five seconds.
int connectTo(int port)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        int client = ::socket(AF_INET, SOCK_STREAM, 0);
        if (::connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0)
        {
            timeval timeout{5, 0};
            ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            return client;
        }
        ::close(client);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return -1;
}

class CountingApplication : public NullApplication
{
public:
    void onLogon(const SessionID &) override { ++callbacks; }
    void fromAdmin(const Message &, const SessionID &)
        EXCEPT(FieldNotFound, IncorrectDataFormat, IncorrectTagValue, RejectLogon) override
    {
        ++callbacks;
    }
    std::atomic<int> callbacks{0};
};

SessionID sessionID(const std::string &acceptor) { return SessionID("FIX.4.2", acceptor, "CLIENT"); }

SessionSettings settings(int port, const std::string &acceptor, const std::string &allowed)
{
    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
           << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nAllowedRemoteAddresses=" << allowed << "\n"
#if (HAVE_SSL > 0)
           << "ServerCertificateFile=" << certPath("127_0_0_1_server.crt") << "\n"
           << "ServerCertificateKeyFile=" << certPath("127_0_0_1_server.key") << "\n"
#endif
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=" << acceptor << "\nTargetCompID=CLIENT\n";
    return SessionSettings(config);
}

std::string logon(const std::string &acceptor)
{
    Message message;
    message.getHeader().setField(BeginString(BeginString_FIX42));
    message.getHeader().setField(MsgType(MsgType_Logon));
    message.getHeader().setField(SenderCompID("CLIENT"));
    message.getHeader().setField(TargetCompID(acceptor));
    message.getHeader().setField(MsgSeqNum(1));
    message.getHeader().setField(SendingTime(UtcTimeStamp::now()));
    message.setField(EncryptMethod(0));
    message.setField(HeartBtInt(30));
    return message.toString();
}

// What the acceptor did with the Logon: answered it, or closed the connection.
struct Outcome
{
    bool loggedOn = false;
    bool closed = false;
};

Outcome plainLogon(int port, const std::string &acceptor)
{
    Outcome outcome;
    const int client = connectTo(port);
    REQUIRE(client >= 0);
    const std::string request = logon(acceptor);
    REQUIRE(::send(client, request.data(), request.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(request.size()));
    std::string reply;
    char buffer[512];
    while (true)
    {
        const ssize_t count = ::recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0)
        {
            outcome.closed = count == 0 || errno == ECONNRESET;
            break;
        }
        reply.append(buffer, count);
        if (reply.find("\00135=A\001") != std::string::npos)
        {
            outcome.loggedOn = true;
            break;
        }
    }
    ::close(client);
    return outcome;
}

#if (HAVE_SSL > 0)
Outcome tlsLogon(int port, const std::string &acceptor)
{
    Outcome outcome;
    const int client = connectTo(port);
    REQUIRE(client >= 0);
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, client);
    REQUIRE(SSL_connect(ssl) == 1);
    const std::string request = logon(acceptor);
    REQUIRE(SSL_write(ssl, request.data(), static_cast<int>(request.size())) == static_cast<int>(request.size()));
    std::string reply;
    char buffer[512];
    while (true)
    {
        const int count = SSL_read(ssl, buffer, sizeof(buffer));
        if (count <= 0)
        {
            const int error = SSL_get_error(ssl, count);
            outcome.closed = error == SSL_ERROR_ZERO_RETURN || error == SSL_ERROR_SYSCALL || error == SSL_ERROR_SSL;
            break;
        }
        reply.append(buffer, count);
        if (reply.find("\00135=A\001") != std::string::npos)
        {
            outcome.loggedOn = true;
            break;
        }
    }
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    ::close(client);
    return outcome;
}
#endif

template <typename AcceptorType> void check(const std::string &name, Outcome (*client)(int, const std::string &))
{
    MemoryStoreFactory factory;

    SECTION("refused")
    {
        CountingApplication application;
        const int port = freePort();
        AcceptorType acceptor(application, factory, settings(port, name + "DENY", "127.0.0.2"));
        acceptor.start();
        const Outcome outcome = client(port, name + "DENY");
        Session *session = Session::lookupSession(sessionID(name + "DENY"));
        REQUIRE(session != nullptr);
        CHECK_FALSE(outcome.loggedOn);
        CHECK(outcome.closed);
        CHECK(application.callbacks == 0);
        CHECK(session->getExpectedTargetNum() == 1);
        acceptor.stop(true);
    }

    SECTION("allowed")
    {
        CountingApplication application;
        const int port = freePort();
        AcceptorType acceptor(application, factory, settings(port, name + "ALLOW", "127.0.0.2,127.0.0.1"));
        acceptor.start();
        const Outcome outcome = client(port, name + "ALLOW");
        CHECK(outcome.loggedOn);
        CHECK(application.callbacks > 0);
        acceptor.stop(true);
    }
}
} // namespace

TEST_CASE("AllowedRemoteAddressTests")
{
    SECTION("SocketAcceptor") { check<SocketAcceptor>("REACTOR", plainLogon); }
    SECTION("ThreadedSocketAcceptor") { check<ThreadedSocketAcceptor>("THREADED", plainLogon); }
#if (HAVE_SSL > 0)
    SECTION("SSLSocketAcceptor") { check<SSLSocketAcceptor>("TLSREACTOR", tlsLogon); }
    SECTION("ThreadedSSLSocketAcceptor") { check<ThreadedSSLSocketAcceptor>("TLSTHREADED", tlsLogon); }
#endif
}
