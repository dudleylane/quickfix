#include "config.h"

#if (HAVE_SSL > 0)

#include <Application.h>
#include <MessageStore.h>
#include <SSLSocketAcceptor.h>
#include <SSLSocketInitiator.h>
#include <SessionSettings.h>
#include <ThreadedSSLSocketInitiator.h>

#include "TestHelper.h"
#include "catch_amalgamated.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace FIX;

// An SSL initiator logs on only to a server whose certificate names the host it
// connected to (GHSA-rh2f, 36c2baaf): its connection calls ssl_set_peer_host with
// the configured host. The verification tests called that function themselves,
// so removing the initiators' calls passed every test (#129). The server's
// certificate here names only the IP address 127.0.0.1.

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

bool localhostIsLoopback()
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo *result = nullptr;
    if (::getaddrinfo("localhost", nullptr, &hints, &result) != 0)
    {
        return false;
    }
    const bool loopback = reinterpret_cast<sockaddr_in *>(result->ai_addr)->sin_addr.s_addr == htonl(INADDR_LOOPBACK);
    ::freeaddrinfo(result);
    return loopback;
}

SessionSettings acceptorSettings(int port, const std::string &name)
{
    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
           << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
           << "ServerCertificateFile=" << certPath("127_0_0_1_server.crt") << "\n"
           << "ServerCertificateKeyFile=" << certPath("127_0_0_1_server.key") << "\n"
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=" << name << "\nTargetCompID=CLIENT\n";
    return SessionSettings(config);
}

SessionSettings initiatorSettings(int port, const std::string &name, const std::string &host, bool verify)
{
    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=" << host << "\nSocketConnectPort=" << port
           << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nHeartBtInt=30\nReconnectInterval=30\n"
           << "CertificationAuthoritiesFile=" << certPath("certs/cacert.pem") << "\n"
           << "ClientCertificateFile=" << certPath("127_0_0_1_client.crt") << "\n"
           << "ClientCertificateKeyFile=" << certPath("127_0_0_1_client.key") << "\n"
           << (verify ? "" : "CertificateVerifyLevel=0\n") << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=CLIENT\n"
           << "TargetCompID=" << name << "\n";
    return SessionSettings(config);
}

template <typename InitiatorType> bool logsOn(const std::string &name, const std::string &host, bool verify)
{
    NullApplication application;
    MemoryStoreFactory factory;
    const int port = freePort();
    SSLSocketAcceptor acceptor(application, factory, acceptorSettings(port, name));
    acceptor.start();
    InitiatorType initiator(application, factory, initiatorSettings(port, name, host, verify));
    initiator.start();

    // A refused handshake leaves the session disconnected until its next attempt,
    // 30 s away, so four seconds is a fair wait either way.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!initiator.isLoggedOn() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const bool loggedOn = initiator.isLoggedOn();
    initiator.stop(true);
    acceptor.stop(true);
    return loggedOn;
}

template <typename InitiatorType> void check(const std::string &name)
{
    REQUIRE(localhostIsLoopback()); // else "localhost" would test nothing

    SECTION("aCertificateForAnotherNameIsRefused")
    {
        CHECK_FALSE(logsOn<InitiatorType>(name + "1", "localhost", true));
    }

    SECTION("aCertificateForTheHostIsAccepted") { CHECK(logsOn<InitiatorType>(name + "2", "127.0.0.1", true)); }

    SECTION("verifyLevelZeroSkipsTheName") { CHECK(logsOn<InitiatorType>(name + "3", "localhost", false)); }
}
} // namespace

TEST_CASE("SSLInitiatorPeerNameTests")
{
    SECTION("SSLSocketInitiator") { check<SSLSocketInitiator>("PEERREACTOR"); }
    SECTION("ThreadedSSLSocketInitiator") { check<ThreadedSSLSocketInitiator>("PEERTHREADED"); }
}

#endif
