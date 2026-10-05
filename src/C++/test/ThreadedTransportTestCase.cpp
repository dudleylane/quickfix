#include "config.h"

#include <Application.h>
#include <DataDictionaryProvider.h>
#include <MessageStore.h>
#include <Parser.h>
#include <SSLSocketAcceptor.h>
#include <Session.h>
#include <SessionSettings.h>
#include <SocketAcceptor.h>
#include <ThreadedSocketAcceptor.h>
#include <ThreadedSocketConnection.h>
#include <TimeRange.h>
#if (HAVE_SSL > 0)
#include <ThreadedSSLSocketAcceptor.h>
#include <ThreadedSSLSocketConnection.h>
#include <UtilitySSL.h>
#endif

#include "TestHelper.h"
#include "catch_amalgamated.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sstream>
#include <sys/resource.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace FIX;

// A threaded connection is read by its own thread, but disconnect() is reached
// from any thread -- Session::disconnect() through the responder, or a
// transport's onStop. It may shut the socket down, which wakes the reader, but
// it must not close it: the number would be free for reuse while the reader
// still polled it, and the reader's own failure path would close it a second
// time (#28). The transport closes the socket, once, when it retires the
// connection's thread. The TLS twin must also leave its SSL object alone while
// the reader is inside SSL_read, since an SSL object is not safe for concurrent
// use (#27).

namespace
{
bool isOpen(int fd) { return ::fcntl(fd, F_GETFD) != -1; }

std::size_t openDescriptors()
{
    std::size_t count = 0;
    if (DIR *dir = ::opendir("/proc/self/fd"))
    {
        while (::readdir(dir))
        {
            ++count;
        }
        ::closedir(dir);
    }
    return count;
}

// A loopback port nothing is listening on, found by binding port 0.
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

#if (HAVE_SSL > 0)
std::string certPath(const std::string &leaf) { return TestSettings::specPath + "/../bin/cfg/certs/" + leaf; }

// Holds every read on a BIO for a while and records whether anything writes to
// the same BIO meanwhile -- which is what SSL_shutdown does.
struct ReadWindow
{
    std::atomic<bool> inRead{false};
    std::atomic<bool> overlapped{false};
};

long holdReads(BIO *bio, int oper, const char *, size_t, int, long, int ret, size_t *)
{
    ReadWindow *window = reinterpret_cast<ReadWindow *>(BIO_get_callback_arg(bio));
    if (oper == BIO_CB_READ)
    {
        window->inRead = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    else if (oper == (BIO_CB_READ | BIO_CB_RETURN))
    {
        window->inRead = false;
    }
    else if (oper == BIO_CB_WRITE && window->inRead)
    {
        window->overlapped = true;
    }
    return ret;
}
#endif
} // namespace

TEST_CASE("ThreadedTransportTests")
{
    SECTION("disconnectFromAnotherThreadLeavesTheSocketToItsConnection")
    {
        int pair[2];
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        const int fd = pair[0];
        ThreadedSocketConnection *connection =
            new ThreadedSocketConnection(fd, ThreadedSocketConnection::Sessions(), nullptr);

        std::thread reader(
            [connection]
            {
                while (connection->read())
                {
                }
            });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        connection->disconnect();
        reader.join();
        CHECK(isOpen(fd));

        delete connection;
        CHECK(isOpen(fd));
        socket_close(fd); // what removeThread and onStop do when they retire the thread
        CHECK_FALSE(isOpen(fd));
        ::close(pair[1]);
    }

#if (HAVE_SSL > 0)
    SECTION("tlsDisconnectFromAnotherThreadWaitsForTheReader")
    {
        SSL_CTX *serverCtx = SSL_CTX_new(TLS_server_method());
        REQUIRE(SSL_CTX_use_certificate_file(serverCtx, certPath("127_0_0_1_server.crt").c_str(), SSL_FILETYPE_PEM) ==
                1);
        REQUIRE(SSL_CTX_use_PrivateKey_file(serverCtx, certPath("127_0_0_1_server.key").c_str(), SSL_FILETYPE_PEM) ==
                1);
        SSL_CTX *clientCtx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(clientCtx, SSL_VERIFY_NONE, nullptr);

        int pair[2];
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);

        // Bound the way the threaded transport binds it: the BIO owns the fd.
        SSL *server = SSL_new(serverCtx);
        BIO *bio = BIO_new_socket(pair[0], BIO_CLOSE);
        SSL_set_bio(server, bio, bio);
        SSL *client = SSL_new(clientCtx);
        SSL_set_fd(client, pair[1]);

        int accepted = 0;
        std::thread handshake([server, &accepted] { accepted = SSL_accept(server); });
        REQUIRE(SSL_connect(client) == 1);
        handshake.join();
        REQUIRE(accepted == 1);

        ThreadedSSLSocketConnection *connection =
            new ThreadedSSLSocketConnection(pair[0], server, ThreadedSSLSocketConnection::Sessions(), nullptr);

        ReadWindow window;
        BIO_set_callback_arg(bio, reinterpret_cast<char *>(&window));
        BIO_set_callback_ex(bio, holdReads);

        REQUIRE(SSL_write(client, "x", 1) == 1);
        std::thread reader([connection] { connection->read(); });
        while (!window.inRead)
        {
            std::this_thread::yield();
        }

        // The reader is inside SSL_read now, and will be for a while.
        connection->disconnect();
        reader.join();
        CHECK_FALSE(window.overlapped);

        BIO_set_callback_ex(bio, nullptr);
        delete connection;
        SSL_free(server);
        SSL_free(client);
        ::close(pair[1]);
        SSL_CTX_free(clientCtx);
        SSL_CTX_free(serverCtx);
    }

    SECTION("readWorksOnADescriptorAtOrAboveFdSetsize")
    {
        // The threaded TLS connection waits on its socket with poll(), like the other
        // transports, so a descriptor of FD_SETSIZE (1024) or more is served normally. Put the
        // connection's socket at a descriptor >= 1024 and drive one read through it.
        //
        // This checks the poll() path works at a high descriptor; it does not detect a return
        // to the fd_set path. Verified 2026-10-04: with the fd_set code restored, this section
        // still passes, AddressSanitizer included (likely because the overrun lands inside the
        // connection object, which ASan does not instrument).
        rlimit limit{};
        REQUIRE(::getrlimit(RLIMIT_NOFILE, &limit) == 0);
        if (limit.rlim_cur <= FD_SETSIZE)
        {
            rlimit raised = limit;
            raised.rlim_cur = std::min<rlim_t>(limit.rlim_max, FD_SETSIZE + 16);
            if (::setrlimit(RLIMIT_NOFILE, &raised) != 0 || raised.rlim_cur <= FD_SETSIZE)
            {
                SKIP("cannot raise RLIMIT_NOFILE above FD_SETSIZE");
            }
        }

        SSL_CTX *serverCtx = SSL_CTX_new(TLS_server_method());
        REQUIRE(SSL_CTX_use_certificate_file(serverCtx, certPath("127_0_0_1_server.crt").c_str(), SSL_FILETYPE_PEM) ==
                1);
        REQUIRE(SSL_CTX_use_PrivateKey_file(serverCtx, certPath("127_0_0_1_server.key").c_str(), SSL_FILETYPE_PEM) ==
                1);
        SSL_CTX *clientCtx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(clientCtx, SSL_VERIFY_NONE, nullptr);

        int pair[2];
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);

        // Move the server side to the lowest free descriptor >= FD_SETSIZE.
        const int high = ::fcntl(pair[0], F_DUPFD, FD_SETSIZE);
        REQUIRE(high >= FD_SETSIZE);
        ::close(pair[0]);

        SSL *server = SSL_new(serverCtx);
        BIO *bio = BIO_new_socket(high, BIO_CLOSE);
        SSL_set_bio(server, bio, bio);
        SSL *client = SSL_new(clientCtx);
        SSL_set_fd(client, pair[1]);

        int accepted = 0;
        std::thread handshake([server, &accepted] { accepted = SSL_accept(server); });
        REQUIRE(SSL_connect(client) == 1);
        handshake.join();
        REQUIRE(accepted == 1);

        ThreadedSSLSocketConnection *connection =
            new ThreadedSSLSocketConnection(high, server, ThreadedSSLSocketConnection::Sessions(), nullptr);

        REQUIRE(SSL_write(client, "x", 1) == 1);
        std::atomic<bool> done{false};
        std::thread reader(
            [connection, &done]
            {
                connection->read();
                done = true;
            });
        reader.join();
        CHECK(done.load());

        delete connection;
        SSL_free(server);
        SSL_free(client);
        ::close(pair[1]);
        SSL_CTX_free(clientCtx);
        SSL_CTX_free(serverCtx);
        ::setrlimit(RLIMIT_NOFILE, &limit);
    }
#endif
}

TEST_CASE("ThreadedTransportStopTests")
{
    SECTION("stopWithALiveConnectionIsPromptAndLeavesNoDescriptorOpen")
    {
        // onStop shuts every socket down to wake its thread, joins the threads,
        // and only then closes the sockets. A socket it forgot would show up here
        // as a descriptor left open; the double close this replaced cannot be
        // seen from inside the process -- strace showed it.
        const int port = freePort();
        std::stringstream config;
        config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
               << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
               << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=THREADEDSTOP\nTargetCompID=TW\n";
        SessionSettings settings(config);
        NullApplication application;
        MemoryStoreFactory factory;

        const std::size_t before = openDescriptors();
        {
            ThreadedSocketAcceptor acceptor(application, factory, settings);
            acceptor.start();
            const int client = connectTo(port);
            REQUIRE(client >= 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const auto start = std::chrono::steady_clock::now();
            acceptor.stop();
            CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));
            ::close(client);
        }
        CHECK(openDescriptors() == before);
    }
}

namespace
{
// Send every byte of a buffer, looping past short writes.
bool sendAll(int fd, const std::string &bytes)
{
    size_t sent = 0;
    while (sent < bytes.size())
    {
        ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n <= 0)
        {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}
} // namespace

TEST_CASE("ThreadedTransportParserCapTests")
{
    SECTION("anOversizedStreamDropsOnlyItsConnection")
    {
        // A peer that sends more than Parser::MAX_MESSAGE_SIZE without completing a
        // message makes addToStream throw MessageParseError. The connection's read
        // loop must catch it and drop just this connection; left uncaught it reaches
        // the thread entry and terminates the whole process, every session with it.
        // Unfixed, this section aborts the ut binary (exit 134) rather than failing a
        // CHECK; fixed, the acceptor keeps serving and stop() stays prompt.
        const int port = freePort();
        std::stringstream config;
        config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
               << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
               << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=THREADEDCAP\nTargetCompID=TW\n";
        SessionSettings settings(config);
        NullApplication application;
        MemoryStoreFactory factory;

        const std::size_t before = openDescriptors();
        {
            ThreadedSocketAcceptor acceptor(application, factory, settings);
            acceptor.start();

            const int flooder = connectTo(port);
            REQUIRE(flooder >= 0);
            // A header declaring a body far larger than the stream will ever hold, so
            // the parser keeps buffering and never frames a message, then filler to
            // carry the buffer past the 8 MB cap.
            std::string flood = "8=FIX.4.2\0019=99999999\001";
            flood.append(9 * 1024 * 1024, 'A');
            sendAll(flooder, flood);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            ::close(flooder);

            // The process is still alive: a second peer can connect and the acceptor
            // stops promptly.
            const int survivor = connectTo(port);
            CHECK(survivor >= 0);
            if (survivor >= 0)
            {
                ::close(survivor);
            }

            const auto start = std::chrono::steady_clock::now();
            acceptor.stop();
            CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));
        }
        CHECK(openDescriptors() == before);
    }
}

namespace
{
SessionSettings deadlineSettings(int port, const std::string &sender)
{
    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
           << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=Y\nDataDictionary="
           << TestSettings::pathForSpec("FIX42") << "\n"
#if (HAVE_SSL > 0)
           << "ServerCertificateFile=" << certPath("127_0_0_1_server.crt") << "\n"
           << "ServerCertificateKeyFile=" << certPath("127_0_0_1_server.key") << "\n"
#endif
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=" << sender << "\nTargetCompID=TW\n";
    return SessionSettings(config);
}

// A well-framed Logon that binds the session but fails its dictionary check --
// it carries a field a Logon does not define -- so it draws a session-level
// reject instead of a disconnect, and the session is not logged on.
std::string rejectedFirstMessage(const std::string &target)
{
    Message message;
    message.getHeader().setField(BeginString("FIX.4.2"));
    message.getHeader().setField(MsgType("A"));
    message.setField(EncryptMethod(0));
    message.setField(HeartBtInt(30));
    message.setField(Symbol("IBM"));
    message.getHeader().setField(SenderCompID("TW"));
    message.getHeader().setField(TargetCompID(target));
    message.getHeader().setField(MsgSeqNum(1));
    message.getHeader().setField(SendingTime(UtcTimeStamp::now()));
    return message.toString();
}

// True once the peer has closed the connection.
bool closedByPeer(int fd)
{
    struct pollfd pfd = {fd, POLLIN, 0};
    if (::poll(&pfd, 1, 0) <= 0)
    {
        return false;
    }
    char byte;
    return ::recv(fd, &byte, 1, MSG_DONTWAIT) == 0 || (pfd.revents & (POLLHUP | POLLERR));
}
} // namespace

TEST_CASE("SetupDeadlineTests")
{
    SECTION("aConnectionThatDoesNotLogOnIsDroppedOnEveryAcceptor")
    {
        // An accepted connection must log on within ten seconds, whether it sends
        // nothing at all or only messages that bind its session and draw rejects
        // (#71). One wait covers every acceptor, so this costs ~12 s once.
        NullApplication application;
        MemoryStoreFactory factory;

        const int threadedPort = freePort();
        ThreadedSocketAcceptor threaded(application, factory, deadlineSettings(threadedPort, "DEADLINE1"));
        threaded.start();
        const int reactorPort = freePort();
        SocketAcceptor reactor(application, factory, deadlineSettings(reactorPort, "DEADLINE2"));
        reactor.start();

        std::vector<int> clients;
        const int threadedBound = connectTo(threadedPort);
        REQUIRE(threadedBound >= 0);
        REQUIRE(sendAll(threadedBound, rejectedFirstMessage("DEADLINE1")));
        clients.push_back(threadedBound);
        const int threadedSilent = connectTo(threadedPort);
        REQUIRE(threadedSilent >= 0);
        clients.push_back(threadedSilent);
        const int reactorBound = connectTo(reactorPort);
        REQUIRE(reactorBound >= 0);
        REQUIRE(sendAll(reactorBound, rejectedFirstMessage("DEADLINE2")));
        clients.push_back(reactorBound);

#if (HAVE_SSL > 0)
        const int tlsPort = freePort();
        ThreadedSSLSocketAcceptor tls(application, factory, deadlineSettings(tlsPort, "DEADLINE3"));
        tls.start();
        const int tlsSilent = connectTo(tlsPort); // never starts its handshake
        REQUIRE(tlsSilent >= 0);
        clients.push_back(tlsSilent);
#endif

        std::this_thread::sleep_for(std::chrono::seconds(3));
        for (std::size_t i = 0; i < clients.size(); ++i)
        {
            INFO("client " << i);
            CHECK_FALSE(closedByPeer(clients[i])); // well inside the deadline
        }

        const auto start = std::chrono::steady_clock::now();
        std::vector<bool> closed(clients.size(), false);
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(12) &&
               std::count(closed.begin(), closed.end(), false) > 0)
        {
            for (std::size_t i = 0; i < clients.size(); ++i)
            {
                closed[i] = closed[i] || closedByPeer(clients[i]);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        for (std::size_t i = 0; i < clients.size(); ++i)
        {
            INFO("client " << i);
            CHECK(closed[i]);
            ::close(clients[i]);
        }

#if (HAVE_SSL > 0)
        tls.stop();
#endif
        reactor.stop();
        threaded.stop();
    }
}

namespace
{
SessionSettings limitSettings(int port, const std::string &sender, const std::string &maxPending)
{
    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
           << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nMaxPendingConnections=" << maxPending
           << "\n"
#if (HAVE_SSL > 0)
           << "ServerCertificateFile=" << certPath("127_0_0_1_server.crt") << "\n"
           << "ServerCertificateKeyFile=" << certPath("127_0_0_1_server.key") << "\n"
#endif
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=" << sender << "\nTargetCompID=TW\n";
    return SessionSettings(config);
}

std::string logonTo(const std::string &target)
{
    Message message;
    message.getHeader().setField(BeginString("FIX.4.2"));
    message.getHeader().setField(MsgType("A"));
    message.getHeader().setField(SenderCompID("TW"));
    message.getHeader().setField(TargetCompID(target));
    message.getHeader().setField(MsgSeqNum(1));
    message.getHeader().setField(SendingTime(UtcTimeStamp::now()));
    message.setField(EncryptMethod(0));
    message.setField(HeartBtInt(30));
    message.setField(ResetSeqNumFlag(true));
    return message.toString();
}

// Polls a condition until it holds or the deadline passes. The tests below wait on state another
// thread sets -- a session's logon, a released slot -- rather than assume it is set at once.
template <typename Predicate> bool waitUntil(Predicate predicate, std::chrono::milliseconds limit)
{
    const auto start = std::chrono::steady_clock::now();
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() - start > limit)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

// Stops an acceptor when the scope ends, so a failed REQUIRE cannot leave its threads running
// against a destroyed acceptor. stop() on a stopped acceptor does nothing.
struct StopOnExit
{
    Acceptor &acceptor;
    ~StopOnExit() { acceptor.stop(true); }
};

// True if the peer closes the connection within the given time.
bool closesWithin(int fd, std::chrono::milliseconds limit)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < limit)
    {
        if (closedByPeer(fd))
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

// Exercises one acceptor started with MaxPendingConnections=2. A Logon is sent
// only when the transport can carry one without TLS.
void checkPendingLimit(Acceptor &acceptor, int port, const std::string &sender, bool canLogOn)
{
    INFO("acceptor for " << sender);
    std::vector<int> open;
    if (canLogOn)
    {
        // A logged-on session does not count against the limit.
        const int loggedOn = connectTo(port);
        REQUIRE(loggedOn >= 0);
        REQUIRE(sendAll(loggedOn, logonTo(sender)));
        char reply[512];
        CHECK(::recv(loggedOn, reply, sizeof(reply), 0) > 0); // the Logon reply
        open.push_back(loggedOn);
        // The reply goes out before the session records it sent, and a threaded acceptor gives
        // the connection's slot back on that connection's thread just after: wait for both, or
        // under load the logged-on connection still counts and the second below is refused.
        const SessionID id(BeginString("FIX.4.2"), SenderCompID(sender), TargetCompID("TW"));
        CHECK(waitUntil(
            [&]
            {
                Session *session = acceptor.getSession(id);
                return session && session->isLoggedOn();
            },
            std::chrono::milliseconds(5000)));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    const int first = connectTo(port);
    const int second = connectTo(port);
    REQUIRE(first >= 0);
    REQUIRE(second >= 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const int third = connectTo(port);
    REQUIRE(third >= 0);
    CHECK(closesWithin(third, std::chrono::milliseconds(2000))); // refused: two already waiting
    ::close(third);
    CHECK_FALSE(closedByPeer(first));
    CHECK_FALSE(closedByPeer(second));
    for (const int fd : open)
    {
        CHECK_FALSE(closedByPeer(fd));
    }

    // One waiting connection leaves, and its place is free again.
    ::close(first);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const int fourth = connectTo(port);
    REQUIRE(fourth >= 0);
    CHECK_FALSE(closesWithin(fourth, std::chrono::milliseconds(1000)));

    ::close(second);
    ::close(fourth);
    for (const int fd : open)
    {
        ::close(fd);
    }
}
} // namespace

TEST_CASE("PendingConnectionLimitTests")
{
    SECTION("eachAcceptorRefusesConnectionsBeyondTheLimitOfThoseWaitingToLogOn")
    {
        NullApplication application;
        MemoryStoreFactory factory;

        const int threadedPort = freePort();
        ThreadedSocketAcceptor threaded(application, factory, limitSettings(threadedPort, "LIMIT1", "2"));
        threaded.start();
        {
            StopOnExit stop{threaded};
            checkPendingLimit(threaded, threadedPort, "LIMIT1", true);
        }

        const int reactorPort = freePort();
        SocketAcceptor reactor(application, factory, limitSettings(reactorPort, "LIMIT2", "2"));
        reactor.start();
        {
            StopOnExit stop{reactor};
            checkPendingLimit(reactor, reactorPort, "LIMIT2", true);
        }

#if (HAVE_SSL > 0)
        // No TLS client here, so these hold connections in their handshake.
        const int tlsThreadedPort = freePort();
        ThreadedSSLSocketAcceptor tlsThreaded(application, factory, limitSettings(tlsThreadedPort, "LIMIT3", "2"));
        tlsThreaded.start();
        {
            StopOnExit stop{tlsThreaded};
            checkPendingLimit(tlsThreaded, tlsThreadedPort, "LIMIT3", false);
        }

        const int tlsReactorPort = freePort();
        SSLSocketAcceptor tlsReactor(application, factory, limitSettings(tlsReactorPort, "LIMIT4", "2"));
        tlsReactor.start();
        {
            StopOnExit stop{tlsReactor};
            checkPendingLimit(tlsReactor, tlsReactorPort, "LIMIT4", false);
        }
#endif
    }

    SECTION("aNegativeLimitIsAConfigurationError")
    {
        NullApplication application;
        MemoryStoreFactory factory;
        CHECK_THROWS_AS(ThreadedSocketAcceptor(application, factory, limitSettings(freePort(), "LIMIT5", "-1")),
                        ConfigError);
    }
}

namespace
{
void ignoreSignal(int) {}
} // namespace

TEST_CASE("ThreadedSendTests")
{
    SECTION("aShortSendResumesWithTheRemainingBytes")
    {
        // send() on a blocking socket returns a short count when a signal interrupts it after part of
        // the message has gone. The rest must follow -- exactly the rest, not the whole length again
        // from the advanced position, which reads past the message and puts those bytes on the wire.
        int pair[2];
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        const int small = 4096;
        ::setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
        ::setsockopt(pair[1], SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));

        struct sigaction action{};
        struct sigaction previous{};
        action.sa_handler = ignoreSignal; // no SA_RESTART: an interrupted send() returns
        ::sigemptyset(&action.sa_mask);
        REQUIRE(::sigaction(SIGUSR1, &action, &previous) == 0);

        NullApplication application;
        MemoryStoreFactory factory;
        const SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SHORTSEND"), TargetCompID("TW"));
        DataDictionaryProvider provider;
        TimeRange always(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0));
        {
            Session session([] { return UtcTimeStamp::now(); }, application, factory, sessionID, provider, always, 30,
                            nullptr);
            ThreadedSocketConnection connection(sessionID, pair[0], "", 0, nullptr);

            Message logout;
            logout.getHeader().setField(MsgType(MsgType_Logout));
            logout.setField(Text(std::string(1024 * 1024, 'T'))); // far larger than the socket buffers

            std::atomic<bool> sent{false};
            std::thread sender(
                [&]
                {
                    session.send(logout);
                    sent = true;
                });

            // Drain on its own thread, slowly at first so the sender blocks with the buffers full,
            // until the sender has finished and nothing more arrives.
            std::string received;
            std::thread reader(
                [&]
                {
                    char buffer[65536];
                    for (;;)
                    {
                        struct pollfd pfd = {pair[1], POLLIN, 0};
                        if (::poll(&pfd, 1, 200) <= 0)
                        {
                            if (sent)
                            {
                                return;
                            }
                            continue;
                        }
                        const ssize_t n =
                            ::recv(pair[1], buffer, received.size() < 64 * 1024 ? 512 : sizeof(buffer), 0);
                        if (n <= 0)
                        {
                            return;
                        }
                        received.append(buffer, static_cast<size_t>(n));
                        if (received.size() < 64 * 1024)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                    }
                });

            // Interrupt the blocked sender mid-message, a few times.
            for (int round = 0; round < 5 && !sent; ++round)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                ::pthread_kill(sender.native_handle(), SIGUSR1);
            }
            sender.join();
            reader.join();

            // Exactly one well-framed message, and nothing after it.
            Parser parser;
            parser.addToStream(received);
            std::string message;
            REQUIRE(parser.readFixMessage(message));
            CHECK(message.size() == received.size());
            CHECK_NOTHROW(Message(message, true)); // BodyLength and CheckSum match the bytes
        }
        ::sigaction(SIGUSR1, &previous, nullptr);
        ::close(pair[0]);
        ::close(pair[1]);
    }
}

TEST_CASE("ReactorTeardownTests")
{
    SECTION("stopDisconnectsSessionsWhosePeerIgnoresTheLogout")
    {
        // A peer that stays logged on through stop()'s logout wait still has its connection open
        // when the reactor deletes its server and monitor. stop() must disconnect that session
        // through the normal path: left holding the connection as its responder, the session still
        // reports logged on, and a send from another thread reaches the deleted monitor (#49).
        NullApplication application;
        MemoryStoreFactory factory;
        const int port = freePort();
        // LogoutTimeout longer than the reactor's five-second wait after stop(): with the default
        // of two seconds the session gives up on the Logout and disconnects itself in time.
        std::stringstream config;
        config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
               << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nLogoutTimeout=10\n"
               << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=TEARDOWN\nTargetCompID=TW\n";
        SessionSettings settings(config);
        SocketAcceptor acceptor(application, factory, settings);
        acceptor.start();

        const int peer = connectTo(port);
        REQUIRE(peer >= 0);
        REQUIRE(sendAll(peer, logonTo("TEARDOWN")));
        char reply[512];
        const ssize_t got = ::recv(peer, reply, sizeof(reply), 0);
        REQUIRE(got > 0); // the Logon reply
        INFO("reply: " << std::string(reply, static_cast<size_t>(got)));
        const SessionID id(BeginString("FIX.4.2"), SenderCompID("TEARDOWN"), TargetCompID("TW"));
        StopOnExit stop{acceptor};
        Session *session = acceptor.getSession(id);
        REQUIRE(session != nullptr);
        // The reply goes out before the session records its Logon as sent.
        REQUIRE(waitUntil([&] { return session->isLoggedOn(); }, std::chrono::milliseconds(5000)));

        acceptor.stop(true); // the peer never answers the Logout

        CHECK_FALSE(session->isLoggedOn());
        // A Logout is sent whatever the session's state, so this reaches the responder if the
        // session still has one: the connection, whose monitor stop() deleted. A plain build only
        // reads the freed monitor; under AddressSanitizer it is a reported use-after-free.
        Message logout;
        logout.getHeader().setField(MsgType(MsgType_Logout));
        session->send(logout);

        ::close(peer);
    }
}
