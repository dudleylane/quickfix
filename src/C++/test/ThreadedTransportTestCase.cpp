#include "config.h"

#include <Application.h>
#include <DataDictionaryProvider.h>
#include <MessageStore.h>
#include <Parser.h>
#include <PortSetting.h>
#include <Session.h>
#include <SessionSettings.h>
#include <SocketAcceptor.h>
#include <SocketInitiator.h>
#include <ThreadedSocketAcceptor.h>
#include <ThreadedSocketConnection.h>
#include <ThreadedSocketInitiator.h>
#include <TimeRange.h>
#include <fix42/Logon.h>
#include <fix42/TestRequest.h>
#if (HAVE_SSL > 0)
#include <SSLSocketAcceptor.h>
#include <ThreadedSSLSocketAcceptor.h>
#include <ThreadedSSLSocketConnection.h>
#include <UtilitySSL.h>
#endif

#include "TestHelper.h"
#include "catch_amalgamated.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sstream>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace FIX;

// Every select() made in this process, the library's included: an executable's
// definition of a symbol the library references takes the library's calls. It
// forwards to libc's. No Linux transport waits with select() -- an fd_set
// overflows at FD_SETSIZE (GHSA-ppch) -- so a test can require none (#130).
namespace
{
std::atomic<int> selectCalls{0};
}

extern "C" int select(int count, fd_set *read, fd_set *write, fd_set *except, timeval *timeout)
{
    ++selectCalls;
    using Select = int (*)(int, fd_set *, fd_set *, fd_set *, timeval *);
    static const Select libcSelect = reinterpret_cast<Select>(::dlsym(RTLD_NEXT, "select"));
    return libcSelect(count, read, write, except, timeout);
}

// accept() for the process, failing with EMFILE while acceptFailures is above
// zero -- the descriptor limit, say -- and forwarding to libc's otherwise (#131).
namespace
{
std::atomic<int> acceptFailures{0};
// Holds each socket accept() accepts this long before returning it.
std::atomic<int> acceptDelayMilliseconds{0};
} // namespace

extern "C" int accept(int socket, sockaddr *address, socklen_t *length)
{
    int failures = acceptFailures.load();
    while (failures > 0)
    {
        if (acceptFailures.compare_exchange_weak(failures, failures - 1))
        {
            errno = EMFILE;
            return -1;
        }
    }
    using Accept = int (*)(int, sockaddr *, socklen_t *);
    static const Accept libcAccept = reinterpret_cast<Accept>(::dlsym(RTLD_NEXT, "accept"));
    const int accepted = libcAccept(socket, address, length);
    if (accepted >= 0 && acceptDelayMilliseconds > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(acceptDelayMilliseconds.load()));
    }
    return accepted;
}

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
        // The overrun itself goes unseen: with the fd_set code restored this read still
        // works, AddressSanitizer included (verified 2026-10-04; the overrun lands inside
        // the connection object, which ASan does not instrument). So the section also
        // counts select() calls, through the select() defined at the top of this file,
        // and requires none (#130).
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
        const int selectsBefore = selectCalls;
        std::atomic<bool> done{false};
        std::thread reader(
            [connection, &done]
            {
                connection->read();
                done = true;
            });
        reader.join();
        CHECK(done.load());
        // The read waited without select(), whose fd_set this descriptor would
        // overrun. This is what fails if the fd_set path comes back (#130).
        CHECK(selectCalls == selectsBefore);

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

    // start() makes the accept threads on a thread of its own, so a stop() right after
    // it can reach onStop before they exist. One made after onStop took the threads to
    // join was never joined -- it outlived the acceptor, a use after free under ASan --
    // and its listener was never closed, so the port stayed bound (#113).
    SECTION("stopRightAfterStartJoinsEveryThreadAndClosesEveryListener")
    {
        NullApplication application;
        MemoryStoreFactory factory;
        auto run = [&](bool ssl)
        {
            const std::size_t before = openDescriptors();
            for (int round = 0; round < 20; ++round)
            {
                std::vector<int> ports;
                std::stringstream config;
                config << "[DEFAULT]\nConnectionType=acceptor\nStartTime=00:00:00\nEndTime=00:00:00\n"
                       << "UseDataDictionary=N\n";
#if (HAVE_SSL > 0)
                config << "ServerCertificateFile=" << certPath("127_0_0_1_server.crt") << "\n"
                       << "ServerCertificateKeyFile=" << certPath("127_0_0_1_server.key") << "\n";
#endif
                for (int session = 0; session < 4; ++session)
                {
                    ports.push_back(freePort());
                    config << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=RESTOP" << session
                           << "\nTargetCompID=TW\nSocketAcceptPort=" << ports.back() << "\n";
                }
                SessionSettings settings(config);
                {
                    std::unique_ptr<Acceptor> acceptor;
#if (HAVE_SSL > 0)
                    if (ssl)
                    {
                        acceptor = std::make_unique<ThreadedSSLSocketAcceptor>(application, factory, settings);
                    }
#endif
                    if (!acceptor)
                    {
                        acceptor = std::make_unique<ThreadedSocketAcceptor>(application, factory, settings);
                    }
                    acceptor->start();
                    acceptor->stop(true);
                }
                for (int port : ports)
                {
                    int probe = ::socket(AF_INET, SOCK_STREAM, 0);
                    sockaddr_in address{};
                    address.sin_family = AF_INET;
                    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    address.sin_port = htons(port);
                    INFO("round " << round << ", port " << port);
                    CHECK(::bind(probe, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
                    ::close(probe);
                }
            }
            CHECK(openDescriptors() == before);
        };
        run(false);
#if (HAVE_SSL > 0)
        run(true);
#endif
    }

    // onStop left every listener in m_sockets, so a restarted acceptor made an accept
    // thread on each old number too -- whatever had reused it -- and its next stop()
    // closed that descriptor (#113).
    SECTION("restartLeavesDescriptorsOpenedSinceTheLastStopAlone")
    {
        const int port = freePort();
        std::stringstream config;
        config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
               << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
               << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=RESTART\nTargetCompID=TW\n";
        SessionSettings settings(config);
        NullApplication application;
        MemoryStoreFactory factory;
        ThreadedSocketAcceptor acceptor(application, factory, settings);
        auto listening = [port]
        {
            const int client = connectTo(port);
            if (client < 0)
            {
                return false;
            }
            ::close(client);
            return true;
        };

        acceptor.start();
        REQUIRE(listening());
        // The kernel queues a connection before the accept thread exists; give it time to
        // start, so this stop() is not the early one the section above covers.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        acceptor.stop(true);

        // Take the lowest free numbers, the stopped listener's among them.
        std::vector<int> held;
        for (int i = 0; i < 16; ++i)
        {
            held.push_back(::open("/dev/null", O_RDONLY));
        }
        acceptor.start();
        REQUIRE(listening());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        acceptor.stop(true);

        for (int fd : held)
        {
            INFO("descriptor " << fd);
            CHECK(isOpen(fd));
            ::close(fd);
        }
    }

    // ThreadedSocketInitiator::onStart checked for a stop before taking m_mutex to
    // connect, so a stop() in between let it make a connection thread onStop never
    // joined, which outlived the initiator (#113). That window is too narrow for this
    // loop to hit -- pausing the start thread in it under gdb showed the use after free
    // -- so this only checks start and stop leave nothing behind. Nothing listens here,
    // so no session logs on and no message memory is recycled across threads, which
    // TSan with the TBB allocator would misreport.
    SECTION("initiatorStopRightAfterStartLeavesNoDescriptorOpen")
    {
        const int port = freePort();
        NullApplication application;
        MemoryStoreFactory factory;
        const std::size_t before = openDescriptors();
        for (int round = 0; round < 20; ++round)
        {
            std::stringstream config;
            config << "[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" << port
                   << "\nReconnectInterval=1\nHeartBtInt=30\nStartTime=00:00:00\nEndTime=00:00:00\n"
                   << "UseDataDictionary=N\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=TW\nTargetCompID=ISTOP\n";
            SessionSettings settings(config);
            ThreadedSocketInitiator initiator(application, factory, settings);
            initiator.start();
            initiator.stop(true);
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

// True once the peer has closed the connection. Drains whatever is readable
// first -- rejects, or a TLS peer's records -- so they do not hide the close.
bool closedByPeer(int fd)
{
    while (true)
    {
        struct pollfd pfd = {fd, POLLIN, 0};
        if (::poll(&pfd, 1, 0) <= 0)
        {
            return false;
        }
        char bytes[4096];
        const ssize_t n = ::recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
        {
            return true;
        }
        if (n < 0)
        {
            return (pfd.revents & (POLLHUP | POLLERR)) != 0;
        }
    }
}

#if (HAVE_SSL > 0)
// A client that completes its TLS handshake and then sends `bytes`; the
// connection is the caller's to close, the SSL object the caller's to free.
SSL *tlsClientSending(int fd, SSL_CTX *ctx, const std::string &bytes)
{
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    if (SSL_connect(ssl) != 1 || SSL_write(ssl, bytes.data(), static_cast<int>(bytes.size())) <= 0)
    {
        SSL_free(ssl);
        return nullptr;
    }
    return ssl;
}
#endif
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
        SSL_CTX *clientCtx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(clientCtx, SSL_VERIFY_NONE, nullptr);
        std::vector<SSL *> sessions;

        const int tlsPort = freePort();
        ThreadedSSLSocketAcceptor tls(application, factory, deadlineSettings(tlsPort, "DEADLINE3"));
        tls.start();
        const int tlsSilent = connectTo(tlsPort); // never starts its handshake
        REQUIRE(tlsSilent >= 0);
        clients.push_back(tlsSilent);
        // #71 over TLS: the handshake completes and the first message only draws a reject.
        const int tlsBound = connectTo(tlsPort);
        REQUIRE(tlsBound >= 0);
        sessions.push_back(tlsClientSending(tlsBound, clientCtx, rejectedFirstMessage("DEADLINE3")));
        REQUIRE(sessions.back() != nullptr);
        clients.push_back(tlsBound);

        // The reactor's TLS acceptor, which no deadline test covered (#131).
        const int tlsReactorPort = freePort();
        SSLSocketAcceptor tlsReactor(application, factory, deadlineSettings(tlsReactorPort, "DEADLINE4"));
        tlsReactor.start();
        const int tlsReactorSilent = connectTo(tlsReactorPort);
        REQUIRE(tlsReactorSilent >= 0);
        clients.push_back(tlsReactorSilent);
        const int tlsReactorBound = connectTo(tlsReactorPort);
        REQUIRE(tlsReactorBound >= 0);
        sessions.push_back(tlsClientSending(tlsReactorBound, clientCtx, rejectedFirstMessage("DEADLINE4")));
        REQUIRE(sessions.back() != nullptr);
        clients.push_back(tlsReactorBound);
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
        for (SSL *session : sessions)
        {
            SSL_free(session);
        }
        SSL_CTX_free(clientCtx);
        tlsReactor.stop();
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

    SECTION("aShortQueuedWriteResumesWithTheRemainingBytes")
    {
        // The same with SocketQueuedWrites=Y, where the connection's thread writes without blocking: a
        // write a signal interrupts, or the socket fills, must resume where it stopped.
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
            ThreadedSocketConnection connection(sessionID, pair[0], "", 0, nullptr, "", 0,
                                                true); // SocketQueuedWrites=Y

            Message logout;
            logout.getHeader().setField(MsgType(MsgType_Logout));
            logout.setField(Text(std::string(1024 * 1024, 'T'))); // far larger than the socket buffers

            // With SocketQueuedWrites=Y the connection's own thread writes what send() queues (#166), so run
            // it as the transports do, and interrupt it mid-write; the write must resume with exactly the
            // remaining bytes.
            std::atomic<bool> done{false};
            connection.startQueuedWrites();
            std::thread writer(
                [&]
                {
                    while (!done && connection.read())
                    {
                    }
                });
            session.send(logout);

            // Drain slowly at first so the writer finds the buffers full, until one whole message is in.
            std::string received;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            char buffer[65536];
            int round = 0;
            for (;;)
            {
                Parser probe;
                probe.addToStream(received);
                std::string whole;
                if (probe.readFixMessage(whole) || std::chrono::steady_clock::now() > deadline)
                {
                    break;
                }
                struct pollfd pfd = {pair[1], POLLIN, 0};
                if (::poll(&pfd, 1, 50) <= 0)
                {
                    continue;
                }
                const ssize_t n = ::recv(pair[1], buffer, received.size() < 64 * 1024 ? 512 : sizeof(buffer), 0);
                if (n <= 0)
                {
                    break;
                }
                received.append(buffer, static_cast<size_t>(n));
                if (received.size() < 64 * 1024)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    if (round++ < 5)
                    {
                        ::pthread_kill(writer.native_handle(), SIGUSR1);
                    }
                }
            }
            // Nothing more may follow the one message.
            struct pollfd pfd = {pair[1], POLLIN, 0};
            if (::poll(&pfd, 1, 200) > 0)
            {
                const ssize_t n = ::recv(pair[1], buffer, sizeof(buffer), MSG_DONTWAIT);
                if (n > 0)
                {
                    received.append(buffer, static_cast<size_t>(n));
                }
            }
            done = true;
            writer.join();

            // The Logout arrives whole and well framed, and every byte after it belongs to another whole
            // message (the session's own timer may send a Logon meanwhile): a resumed write that repeated or
            // skipped bytes would leave a malformed tail.
            Parser parser;
            parser.addToStream(received);
            std::string message;
            REQUIRE(parser.readFixMessage(message));
            CHECK(message.find("\00135=5\001") != std::string::npos);
            CHECK(message.size() > 1024 * 1024);
            CHECK_NOTHROW(Message(message, true)); // BodyLength and CheckSum match the bytes
            std::size_t framed = message.size();
            while (parser.readFixMessage(message))
            {
                CHECK_NOTHROW(Message(message, true));
                framed += message.size();
            }
            CHECK(framed == received.size());
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

// SocketAcceptPort was truncated to short, so 70000 listened on 4464 without a word (#99); 0 still means any. The SSL
// acceptors share the check but also validate certificates in onConfigure, so a ConfigError from
// them here would not show which check fired.
TEST_CASE("AcceptPortRangeTests")
{
    auto settingsFor = [](const std::string &port)
    {
        std::stringstream config;
        config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
               << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
               << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=PORTRANGE\nTargetCompID=TW\n";
        return SessionSettings(config);
    };
    NullApplication application;
    MemoryStoreFactory factory;

    for (const std::string port : {"70000", "65536", "-1"})
    {
        INFO("SocketAcceptPort=" << port);
        SessionSettings settings = settingsFor(port);
        {
            SocketAcceptor acceptor(application, factory, settings);
            CHECK_THROWS_AS(acceptor.start(), ConfigError);
        }
        {
            ThreadedSocketAcceptor acceptor(application, factory, settings);
            CHECK_THROWS_AS(acceptor.start(), ConfigError);
        }
    }

    // 0 lets the kernel choose a port, which the tests rely on.
    SessionSettings any = settingsFor("0");
    ThreadedSocketAcceptor acceptor(application, factory, any);
    CHECK_NOTHROW(acceptor.start());
    acceptor.stop();
}

namespace
{
// The next acceptFailures calls to accept() in this process fail with EMFILE,
// through the accept() defined at the top of this file. Starving the process
// of descriptors instead also starves the sanitizers' runtimes of them.
template <typename AcceptorType> void checkAcceptFailureIsSurvived(const std::string &sender, bool tls)
{
    NullApplication application;
    MemoryStoreFactory factory;
    const int port = freePort();
    AcceptorType acceptor(application, factory, deadlineSettings(port, sender));
    StopOnExit stopOnExit{acceptor};
    acceptFailures = 2; // its accept thread's first two calls; it backs off a second after each
    acceptor.start();

    const int client = connectTo(port);
    REQUIRE(client >= 0);

#if (HAVE_SSL > 0)
    SSL_CTX *ctx = nullptr;
    SSL *ssl = nullptr;
    if (tls)
    {
        ctx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
        ssl = tlsClientSending(client, ctx, logonTo(sender));
        CHECK(ssl != nullptr);
    }
    else
#endif
    {
        (void)tls;
        CHECK(sendAll(client, logonTo(sender)));
    }

    // The acceptor kept listening through both failures, and serves the client.
    CHECK(waitUntil([&]() { return acceptor.isLoggedOn(); }, std::chrono::milliseconds(6000)));
    CHECK(acceptFailures == 0);
    acceptFailures = 0;

    acceptor.stop(true);
#if (HAVE_SSL > 0)
    if (ssl)
    {
        SSL_free(ssl);
    }
    if (ctx)
    {
        SSL_CTX_free(ctx);
    }
#endif
    ::close(client);
}
} // namespace

// A threaded acceptor keeps listening after a failed accept() (063a9e5b, #131);
// it used to end its accept thread at the first one.
TEST_CASE("AcceptFailureTests")
{
    SECTION("ThreadedSocketAcceptor") { checkAcceptFailureIsSurvived<ThreadedSocketAcceptor>("ACCEPTFAIL1", false); }
#if (HAVE_SSL > 0)
    SECTION("ThreadedSSLSocketAcceptor")
    {
        checkAcceptFailureIsSurvived<ThreadedSSLSocketAcceptor>("ACCEPTFAIL2", true);
    }
#endif
}

namespace
{
SessionSettings twoSessionSettings(int port, const std::string &first, const std::string &second)
{
    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
           << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
#if (HAVE_SSL > 0)
           << "ServerCertificateFile=" << certPath("127_0_0_1_server.crt") << "\n"
           << "ServerCertificateKeyFile=" << certPath("127_0_0_1_server.key") << "\n"
#endif
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=" << first << "\nTargetCompID=TW\n"
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=" << second << "\nTargetCompID=TW\n";
    return SessionSettings(config);
}

bool loggedOn(const std::string &acceptorCompID)
{
    Session *session = Session::lookupSession(SessionID("FIX.4.2", acceptorCompID, "TW"));
    return session && session->isLoggedOn();
}

// One client sends half its Logon and stops. The reactor must go on serving
// the others meanwhile; it used to read a first message in a loop of its own
// (98e23483, GHSA-4jw9).
template <typename AcceptorType>
void checkSlowFirstMessage(const std::string &slow, const std::string &prompt, bool tls)
{
    NullApplication application;
    MemoryStoreFactory factory;
    const int port = freePort();
    AcceptorType acceptor(application, factory, twoSessionSettings(port, slow, prompt));
    StopOnExit stopOnExit{acceptor};
    acceptor.start();

    const std::string slowLogon = logonTo(slow);
    const std::string firstHalf = slowLogon.substr(0, slowLogon.size() / 2);
    const std::string secondHalf = slowLogon.substr(slowLogon.size() / 2);
    const int slowClient = connectTo(port);
    const int promptClient = connectTo(port);
    REQUIRE(slowClient >= 0);
    REQUIRE(promptClient >= 0);

#if (HAVE_SSL > 0)
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    SSL *slowSsl = nullptr;
    SSL *promptSsl = nullptr;
#endif
    // Timed from before the prompt client's handshake: a stalled reactor would
    // hold up the handshake too, not only the Logon.
    std::chrono::steady_clock::time_point start;
    if (tls)
    {
#if (HAVE_SSL > 0)
        slowSsl = tlsClientSending(slowClient, ctx, firstHalf);
        REQUIRE(slowSsl != nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // the reactor has the half
        start = std::chrono::steady_clock::now();
        promptSsl = tlsClientSending(promptClient, ctx, logonTo(prompt));
        REQUIRE(promptSsl != nullptr);
#endif
    }
    else
    {
        REQUIRE(sendAll(slowClient, firstHalf));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        start = std::chrono::steady_clock::now();
        REQUIRE(sendAll(promptClient, logonTo(prompt)));
    }

    waitUntil([&]() { return loggedOn(prompt); }, std::chrono::milliseconds(5000));
    CHECK(loggedOn(prompt));
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(2000));
    CHECK_FALSE(loggedOn(slow));

    if (tls)
    {
#if (HAVE_SSL > 0)
        CHECK(SSL_write(slowSsl, secondHalf.data(), static_cast<int>(secondHalf.size())) > 0);
#endif
    }
    else
    {
        CHECK(sendAll(slowClient, secondHalf));
    }
    CHECK(waitUntil([&]() { return loggedOn(slow); }, std::chrono::milliseconds(3000)));

    acceptor.stop(true);
#if (HAVE_SSL > 0)
    if (slowSsl)
    {
        SSL_free(slowSsl);
    }
    if (promptSsl)
    {
        SSL_free(promptSsl);
    }
    SSL_CTX_free(ctx);
#endif
    ::close(slowClient);
    ::close(promptClient);
}
} // namespace

TEST_CASE("SlowFirstMessageTests")
{
    SECTION("SocketAcceptor") { checkSlowFirstMessage<SocketAcceptor>("SLOW1", "PROMPT1", false); }
#if (HAVE_SSL > 0)
    SECTION("SSLSocketAcceptor") { checkSlowFirstMessage<SSLSocketAcceptor>("SLOW2", "PROMPT2", true); }
#endif
}

namespace
{
// Stops a threaded acceptor while its accept thread holds a connection it has
// just accepted. The thread spawned the connection after onStop() had taken its
// snapshot of the threads to join, so the connection's thread and socket
// outlived stop() (edacfa1e, #131).
template <typename AcceptorType> void checkConnectionAcceptedWhileStopping(const std::string &sender)
{
    NullApplication application;
    MemoryStoreFactory factory;
    const int port = freePort();
    const std::size_t before = openDescriptors();
    {
        AcceptorType acceptor(application, factory, deadlineSettings(port, sender));
        StopOnExit stopOnExit{acceptor};
        acceptor.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        acceptDelayMilliseconds = 300;
        const int client = connectTo(port);
        REQUIRE(client >= 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // the accept thread holds it
        acceptor.stop(true);
        acceptDelayMilliseconds = 0;
        ::close(client);

        // Nothing the acceptor made is left open: not its listener, not the
        // connection it accepted while stopping.
        CHECK(openDescriptors() == before);
    }
}
} // namespace

TEST_CASE("AcceptedWhileStoppingTests")
{
    SECTION("ThreadedSocketAcceptor") { checkConnectionAcceptedWhileStopping<ThreadedSocketAcceptor>("STOPPING1"); }
#if (HAVE_SSL > 0)
    SECTION("ThreadedSSLSocketAcceptor")
    {
        checkConnectionAcceptedWhileStopping<ThreadedSSLSocketAcceptor>("STOPPING2");
    }
#endif
}

namespace
{
int portOf(int fd, bool peer)
{
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    const int result = peer ? ::getpeername(fd, reinterpret_cast<sockaddr *>(&address), &length)
                            : ::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length);
    return (result == 0 && address.sin_family == AF_INET) ? ntohs(address.sin_port) : -1;
}

// This process's connected socket whose local port is localPort (or any, for -1) and whose peer port
// is peerPort, other than exclude: the engine's end of a connection the test made or accepted.
// The engine creates it on its own thread, so wait for it.
int findConnectedSocket(int localPort, int peerPort, int exclude)
{
    for (int attempt = 0; attempt < 150; ++attempt)
    {
        DIR *dir = ::opendir("/proc/self/fd");
        int found = -1;
        while (dirent *entry = ::readdir(dir))
        {
            const int fd = std::atoi(entry->d_name);
            if (entry->d_name[0] == '.' || fd == exclude || fd == ::dirfd(dir))
            {
                continue;
            }
            if (portOf(fd, true) == peerPort && (localPort < 0 || portOf(fd, false) == localPort))
            {
                found = fd;
            }
        }
        ::closedir(dir);
        if (found >= 0)
        {
            return found;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return -1;
}

struct SocketOptions
{
    bool noDelay;
    int sendBuffer;
    int receiveBuffer;
};

SocketOptions optionsOf(int fd)
{
    auto get = [fd](int level, int option)
    {
        int value = 0;
        socklen_t length = sizeof(value);
        ::getsockopt(fd, level, option, &value, &length);
        return value;
    };
    return {get(IPPROTO_TCP, TCP_NODELAY) != 0, get(SOL_SOCKET, SO_SNDBUF), get(SOL_SOCKET, SO_RCVBUF)};
}

std::string acceptorConfig(int port, const std::string &first, const std::string &second, int secondPort = 0)
{
    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port
           << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=NODELAY1\nTargetCompID=TW\n"
           << first << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=NODELAY2\nTargetCompID=TW\n"
           << second;
    if (secondPort)
    {
        config << "SocketAcceptPort=" << secondPort << "\n";
    }
    return config.str();
}

template <typename Acceptor> SocketOptions acceptedSocketOptions(const std::string &sessionSetting)
{
    NullApplication application;
    MemoryStoreFactory factory;
    const int port = freePort();
    std::stringstream config(acceptorConfig(port, sessionSetting, sessionSetting));
    SessionSettings settings(config);
    Acceptor acceptor(application, factory, settings);
    acceptor.start();
    const int client = connectTo(port);
    REQUIRE(client >= 0);
    const int accepted = findConnectedSocket(port, portOf(client, false), client);
    REQUIRE(accepted >= 0);
    const SocketOptions options = optionsOf(accepted);
    ::close(client);
    acceptor.stop();
    return options;
}

template <typename Initiator>
SocketOptions connectedSocketOptions(const std::string &defaultSetting, const std::string &sessionSetting)
{
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    REQUIRE(::listen(listener, 4) == 0);
    const int port = portOf(listener, false);

    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" << port
           << "\nReconnectInterval=1\nHeartBtInt=30\nStartTime=00:00:00\nEndTime=00:00:00\n"
           << "UseDataDictionary=N\n"
           << defaultSetting << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=TW\nTargetCompID=NODELAY\n"
           << sessionSetting;
    SessionSettings settings(config);
    NullApplication application;
    MemoryStoreFactory factory;
    Initiator initiator(application, factory, settings);
    initiator.start();
    const int connected = findConnectedSocket(-1, port, listener);
    REQUIRE(connected >= 0);
    const SocketOptions options = optionsOf(connected);
    initiator.stop(true);
    ::close(listener);
    return options;
}
} // namespace

// SocketNodelay defaulted to N, so Nagle's algorithm could hold a small message for the peer's delayed
// ACK; initiators read it from [DEFAULT] only, and an acceptor's first session on a port decided for
// the rest (#150). The SSL transports share the helpers but are not driven here: their onConfigure
// validates certificates too, so a ConfigError would not show which check fired.
TEST_CASE("SocketNoDelayTests")
{
    SECTION("defaultsToOn")
    {
        Dictionary settings;
        CHECK(getSocketNoDelay(settings));
        settings.setString(SOCKET_NODELAY, "N");
        CHECK_FALSE(getSocketNoDelay(settings));
        settings.setString(SOCKET_NODELAY, "Y");
        CHECK(getSocketNoDelay(settings));
    }

    SECTION("acceptorsRejectDisagreementOnAPort")
    {
        NullApplication application;
        MemoryStoreFactory factory;
        const int port = freePort();
        std::stringstream disagree(acceptorConfig(port, "SocketNodelay=N\n", ""));
        SessionSettings conflicting(disagree);
        {
            SocketAcceptor acceptor(application, factory, conflicting);
            CHECK_THROWS_AS(acceptor.start(), ConfigError);
        }
        {
            ThreadedSocketAcceptor acceptor(application, factory, conflicting);
            CHECK_THROWS_AS(acceptor.start(), ConfigError);
        }

        // The same disagreement on separate ports is fine: each port has its own listener.
        std::stringstream separate(acceptorConfig(port, "SocketNodelay=N\n", "", freePort()));
        SessionSettings apart(separate);
        ThreadedSocketAcceptor acceptor(application, factory, apart);
        CHECK_NOTHROW(acceptor.start());
        acceptor.stop();
    }

    SECTION("acceptedSockets")
    {
        CHECK(acceptedSocketOptions<SocketAcceptor>("").noDelay);
        CHECK(acceptedSocketOptions<ThreadedSocketAcceptor>("").noDelay);
        CHECK_FALSE(acceptedSocketOptions<SocketAcceptor>("SocketNodelay=N\n").noDelay);
        CHECK_FALSE(acceptedSocketOptions<ThreadedSocketAcceptor>("SocketNodelay=N\n").noDelay);
    }

    SECTION("initiatorsReadTheSessionsSetting")
    {
        CHECK(connectedSocketOptions<SocketInitiator>("", "").noDelay);
        CHECK(connectedSocketOptions<ThreadedSocketInitiator>("", "").noDelay);
        // Set in the session only: initiators used to read [DEFAULT] alone and ignore this.
        CHECK_FALSE(connectedSocketOptions<SocketInitiator>("", "SocketNodelay=N\n").noDelay);
        CHECK_FALSE(connectedSocketOptions<ThreadedSocketInitiator>("", "SocketNodelay=N\n").noDelay);
        // And a session overrides [DEFAULT].
        CHECK(connectedSocketOptions<ThreadedSocketInitiator>("SocketNodelay=N\n", "SocketNodelay=Y\n").noDelay);
    }
}

// SocketSendBufferSize and SocketReceiveBufferSize had SocketNodelay's scope defect: initiators read
// them from [DEFAULT] only, and an acceptor's first session on a port decided for the rest (#170).
// Linux reports twice the size set, for its bookkeeping.
TEST_CASE("SocketBufferSizeTests")
{
    SECTION("acceptorsRejectDisagreementOnAPort")
    {
        NullApplication application;
        MemoryStoreFactory factory;
        const int port = freePort();
        for (const std::string &key : {"SocketSendBufferSize", "SocketReceiveBufferSize"})
        {
            INFO(key);
            std::stringstream disagree(acceptorConfig(port, key + "=32768\n", ""));
            SessionSettings conflicting(disagree);
            {
                SocketAcceptor acceptor(application, factory, conflicting);
                CHECK_THROWS_AS(acceptor.start(), ConfigError);
            }
            {
                ThreadedSocketAcceptor acceptor(application, factory, conflicting);
                CHECK_THROWS_AS(acceptor.start(), ConfigError);
            }
        }

        // Agreement on one port is fine.
        std::stringstream agree(acceptorConfig(port, "SocketSendBufferSize=32768\n", "SocketSendBufferSize=32768\n"));
        SessionSettings same(agree);
        ThreadedSocketAcceptor acceptor(application, factory, same);
        CHECK_NOTHROW(acceptor.start());
        acceptor.stop();
    }

    SECTION("initiatorsReadTheSessionsSetting")
    {
        const std::string sizes = "SocketSendBufferSize=32768\nSocketReceiveBufferSize=49152\n";
        for (const SocketOptions &options : {connectedSocketOptions<SocketInitiator>("", sizes),
                                             connectedSocketOptions<ThreadedSocketInitiator>("", sizes)})
        {
            CHECK(options.sendBuffer == 2 * 32768);
            CHECK(options.receiveBuffer == 2 * 49152);
        }
        // A session overrides [DEFAULT].
        const SocketOptions overridden = connectedSocketOptions<ThreadedSocketInitiator>(
            "SocketSendBufferSize=16384\n", "SocketSendBufferSize=32768\n");
        CHECK(overridden.sendBuffer == 2 * 32768);
    }

    SECTION("acceptedSockets")
    {
        CHECK(acceptedSocketOptions<SocketAcceptor>("SocketSendBufferSize=32768\n").sendBuffer == 2 * 32768);
        CHECK(acceptedSocketOptions<ThreadedSocketAcceptor>("SocketSendBufferSize=32768\n").sendBuffer == 2 * 32768);

        // With no size set, an accepted socket is left to the kernel, which autotunes it. The threaded
        // acceptors copied the listener's reported size on, which fixed the buffers at that size; the
        // reactor never did, so the two now match.
        const SocketOptions reactor = acceptedSocketOptions<SocketAcceptor>("");
        const SocketOptions threaded = acceptedSocketOptions<ThreadedSocketAcceptor>("");
        CHECK(threaded.sendBuffer == reactor.sendBuffer);
        CHECK(threaded.receiveBuffer == reactor.receiveBuffer);
    }
}

// SocketInitiator sweeps its sessions' timers once a second rather than after every wakeup (#157). A
// logged-on session still gets its heartbeat on schedule.
TEST_CASE("SocketInitiatorTimerTests")
{
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    REQUIRE(::listen(listener, 1) == 0);
    const int port = portOf(listener, false);

    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" << port
           << "\nReconnectInterval=30\nHeartBtInt=1\nStartTime=00:00:00\nEndTime=00:00:00\n"
           << "UseDataDictionary=N\nCheckLatency=N\n"
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=TW\nTargetCompID=ISLD\n";
    SessionSettings settings(config);
    NullApplication application;
    MemoryStoreFactory factory;
    SocketInitiator initiator(application, factory, settings);
    initiator.start();

    const int peer = ::accept(listener, nullptr, nullptr);
    REQUIRE(peer >= 0);
    std::string received;
    char buffer[4096];
    auto readAvailable = [&]
    {
        ssize_t size;
        while ((size = ::recv(peer, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0)
        {
            received.append(buffer, size);
        }
    };
    for (int attempt = 0; attempt < 100 && received.find("\00135=A\001") == std::string::npos; ++attempt)
    {
        readAvailable();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(received.find("\00135=A\001") != std::string::npos);

    FIX42::Logon logon;
    logon.getHeader().set(SenderCompID("ISLD"));
    logon.getHeader().set(TargetCompID("TW"));
    logon.getHeader().set(MsgSeqNum(1));
    logon.getHeader().set(SendingTime::now());
    logon.set(EncryptMethod(0));
    logon.set(HeartBtInt(1));
    const std::string reply = logon.toString();
    REQUIRE(::send(peer, reply.c_str(), reply.size(), 0) == static_cast<ssize_t>(reply.size()));
    received.clear();

    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4) &&
           received.find("\00135=0\001") == std::string::npos)
    {
        ::send(peer, "x", 1, MSG_NOSIGNAL);
        readAvailable();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO(received);
    CHECK(received.find("\00135=0\001") != std::string::npos);

    initiator.stop(true);
    ::close(peer);
    ::close(listener);
}

// An initiator driven by poll() runs its sessions' timers too.
TEST_CASE("SocketInitiatorPollTests")
{
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    REQUIRE(::listen(listener, 1) == 0);
    const int port = portOf(listener, false);

    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" << port
           << "\nReconnectInterval=30\nHeartBtInt=1\nStartTime=00:00:00\nEndTime=00:00:00\n"
           << "UseDataDictionary=N\nCheckLatency=N\n"
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=TW\nTargetCompID=ISLD\n";
    SessionSettings settings(config);
    NullApplication application;
    MemoryStoreFactory factory;
    SocketInitiator initiator(application, factory, settings);
    initiator.poll();

    int peer = -1;
    for (int attempt = 0; attempt < 100 && peer < 0; ++attempt)
    {
        initiator.poll();
        peer = ::accept4(listener, nullptr, nullptr, SOCK_NONBLOCK);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(peer >= 0);
    std::string received;
    char buffer[4096];
    auto pump = [&]
    {
        initiator.poll();
        ssize_t size;
        while ((size = ::recv(peer, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0)
        {
            received.append(buffer, size);
        }
    };
    for (int attempt = 0; attempt < 200 && received.find("\00135=A\001") == std::string::npos; ++attempt)
    {
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(received.find("\00135=A\001") != std::string::npos);

    FIX42::Logon logon;
    logon.getHeader().set(SenderCompID("ISLD"));
    logon.getHeader().set(TargetCompID("TW"));
    logon.getHeader().set(MsgSeqNum(1));
    logon.getHeader().set(SendingTime::now());
    logon.set(EncryptMethod(0));
    logon.set(HeartBtInt(1));
    const std::string reply = logon.toString();
    REQUIRE(::send(peer, reply.c_str(), reply.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(reply.size()));
    received.clear();

    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4) &&
           received.find("\00135=0\001") == std::string::npos)
    {
        ::send(peer, "x", 1, MSG_NOSIGNAL);
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO(received);
    CHECK(received.find("\00135=0\001") != std::string::npos);

    initiator.stop(true);
    ::close(peer);
    ::close(listener);
}

// SocketThreadAffinity pins a threaded transport's connection thread (#165): an initiator's once it
// connects, an acceptor's once its first message binds a session.
TEST_CASE("SocketThreadAffinityTests")
{
    SECTION("parsesCpuLists")
    {
        Dictionary settings;
        CHECK(getSocketThreadAffinity(settings).empty());
        settings.setString(SOCKET_THREAD_AFFINITY, "2");
        CHECK(getSocketThreadAffinity(settings) == std::vector<int>{2});
        settings.setString(SOCKET_THREAD_AFFINITY, "1,4-6");
        CHECK(getSocketThreadAffinity(settings) == std::vector<int>{1, 4, 5, 6});
        for (const char *bad : {"x", "3-1", "", "1,,2", "-2", "1-", "99999", "2 3"})
        {
            INFO(bad);
            settings.setString(SOCKET_THREAD_AFFINITY, bad);
            CHECK_THROWS_AS(getSocketThreadAffinity(settings), ConfigError);
        }
    }

    SECTION("pinsConnectionThreads")
    {
        cpu_set_t allowed;
        REQUIRE(sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
        std::vector<int> cpus;
        for (int cpu = 0; cpu < CPU_SETSIZE && cpus.size() < 2; ++cpu)
        {
            if (CPU_ISSET(cpu, &allowed))
            {
                cpus.push_back(cpu);
            }
        }
        if (cpus.size() < 2)
        {
            SKIP("needs two CPUs");
        }

        // Records the CPU set of the thread each admin message arrives on.
        struct AffinityApplication : NullApplication
        {
            void fromAdmin(const Message &, const SessionID &sessionID)
                EXCEPT(FieldNotFound, IncorrectDataFormat, IncorrectTagValue, RejectLogon) override
            {
                cpu_set_t set;
                sched_getaffinity(0, sizeof(set), &set);
                std::lock_guard<std::mutex> lock(mutex);
                seen[sessionID.getSenderCompID()] = set;
            }
            std::mutex mutex;
            std::map<std::string, cpu_set_t> seen;
        };

        const int port = freePort();
        auto settingsFor = [&](const std::string &type, const std::string &sender, const std::string &target, int cpu)
        {
            std::stringstream config;
            config << "[DEFAULT]\nConnectionType=" << type << "\nSocketAcceptPort=" << port
                   << "\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" << port
                   << "\nReconnectInterval=1\nHeartBtInt=30\nStartTime=00:00:00\nEndTime=00:00:00\n"
                   << "UseDataDictionary=N\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=" << sender
                   << "\nTargetCompID=" << target << "\nSocketThreadAffinity=" << cpu << "\n";
            return SessionSettings(config);
        };
        AffinityApplication application;
        MemoryStoreFactory factory;
        SessionSettings acceptorSettings = settingsFor("acceptor", "PINA", "PINI", cpus[0]);
        SessionSettings initiatorSettings = settingsFor("initiator", "PINI", "PINA", cpus[1]);
        ThreadedSocketAcceptor acceptor(application, factory, acceptorSettings);
        acceptor.start();
        ThreadedSocketInitiator initiator(application, factory, initiatorSettings);
        initiator.start();

        const SessionID initiatorID("FIX.4.2", "PINI", "PINA");
        Session *session = nullptr;
        for (int i = 0; i < 300 && !(session && session->isLoggedOn()); ++i)
        {
            session = Session::lookupSession(initiatorID);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        REQUIRE(session);
        REQUIRE(session->isLoggedOn());
        // The acceptor's thread pins after its first read; this arrives on it once pinned.
        FIX42::TestRequest testRequest(TestReqID("PIN"));
        Session::sendToTarget(testRequest, initiatorID);
        bool both = false;
        for (int i = 0; i < 300 && !both; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::lock_guard<std::mutex> lock(application.mutex);
            both = application.seen.count("PINA") && application.seen.count("PINI") &&
                   CPU_COUNT(&application.seen["PINA"]) == 1;
        }
        initiator.stop(true);
        acceptor.stop(true);

        std::lock_guard<std::mutex> lock(application.mutex);
        REQUIRE(application.seen.count("PINA"));
        REQUIRE(application.seen.count("PINI"));
        CHECK(CPU_COUNT(&application.seen["PINA"]) == 1);
        CHECK(CPU_ISSET(cpus[0], &application.seen["PINA"]));
        CHECK(CPU_COUNT(&application.seen["PINI"]) == 1);
        CHECK(CPU_ISSET(cpus[1], &application.seen["PINI"]));
    }
}

// ThreadedSocketConnection's queued writer, SocketQueuedWrites=Y (#166): send() queues and wakes the
// connection's thread, which writes the queue while it reads. Driven here as the transports drive it, over a socket
// pair.
TEST_CASE("ThreadedQueuedWriteTests")
{
    int pair[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    NullApplication application;
    MemoryStoreFactory factory;
    const SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("QUEUED"), TargetCompID("TW"));
    DataDictionaryProvider provider;
    TimeRange always(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0));
    Session session([] { return UtcTimeStamp::now(); }, application, factory, sessionID, provider, always, 30, nullptr);
    std::unique_ptr<ThreadedSocketConnection> connection(
        new ThreadedSocketConnection(sessionID, pair[0], "", 0, nullptr, "", 0, true));
    connection->startQueuedWrites();

    std::atomic<bool> stop{false};
    std::thread writer; // the connection's thread, started by each section when it wants writing
    auto startWriter = [&]
    {
        writer = std::thread(
            [&]
            {
                while (!stop && connection->read())
                {
                }
            });
    };
    // Logouts: a session not logged on still sends them, and each is numbered under the session's lock.
    auto logout = [](std::size_t text)
    {
        Message message;
        message.getHeader().setField(MsgType(MsgType_Logout));
        if (text)
        {
            message.setField(Text(std::string(text, 'L')));
        }
        return message;
    };
    // Everything that reaches the other end, framed into messages.
    std::mutex readMutex;
    std::string received;
    std::atomic<bool> reading{true};
    std::atomic<bool> paused{false}; // a peer that stops reading
    std::thread reader(
        [&]
        {
            char buffer[65536];
            while (reading)
            {
                if (paused)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                struct pollfd pfd = {pair[1], POLLIN, 0};
                if (::poll(&pfd, 1, 20) > 0)
                {
                    const ssize_t n = ::recv(pair[1], buffer, sizeof(buffer), 0);
                    if (n <= 0)
                    {
                        return;
                    }
                    std::lock_guard<std::mutex> lock(readMutex);
                    received.append(buffer, static_cast<size_t>(n));
                }
            }
        });
    auto logouts = [&]
    {
        std::lock_guard<std::mutex> lock(readMutex);
        Parser parser;
        parser.addToStream(received);
        std::vector<int> seqNums;
        std::string message;
        while (parser.readFixMessage(message))
        {
            if (message.find("\00135=5\001") != std::string::npos)
            {
                const std::string::size_type start = message.find("\00134=") + 4;
                seqNums.push_back(std::stoi(message.substr(start, message.find('\001', start) - start)));
            }
        }
        return seqNums;
    };
    auto waitFor = [&](std::size_t count)
    {
        for (int i = 0; i < 500 && logouts().size() < count; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return logouts().size();
    };
    auto finish = [&]
    {
        stop = true;
        connection->disconnect();
        if (writer.joinable())
        {
            writer.join();
        }
        reading = false;
        reader.join();
    };

    SECTION("wireOrderIsSequenceOrderAcrossThreads")
    {
        startWriter();
        std::vector<std::thread> senders;
        for (int t = 0; t < 4; ++t)
        {
            senders.emplace_back(
                [&]
                {
                    for (int i = 0; i < 500; ++i)
                    {
                        Message message = logout(0);
                        session.send(message);
                    }
                });
        }
        for (std::thread &sender : senders)
        {
            sender.join();
        }
        CHECK(waitFor(2000) == 2000);
        const std::vector<int> seqNums = logouts();
        finish();
        for (std::size_t i = 1; i < seqNums.size(); ++i)
        {
            INFO("message " << i);
            CHECK(seqNums[i] > seqNums[i - 1]);
        }
    }

    SECTION("noMessageIsLeftQueuedWhenTheWriterGoesIdle")
    {
        startWriter();
        for (int round = 1; round <= 200; ++round)
        {
            auto sendOne = [&]
            {
                Message message = logout(0);
                session.send(message);
            };
            std::thread a(sendOne), b(sendOne);
            a.join();
            b.join();
            INFO("round " << round);
            REQUIRE(waitFor(static_cast<std::size_t>(2 * round)) == static_cast<std::size_t>(2 * round));
        }
        finish();
    }

    SECTION("aFullQueueMakesItsSendersWait")
    {
        // Past the bound a sender writes the queue itself, so a peer that stops reading holds it there:
        // the backpressure a blocking write gave.
        paused = true;
        startWriter();
        std::atomic<bool> finished{false};
        std::thread bulk(
            [&]
            {
                for (int i = 0; i < 6; ++i) // 6 MiB against a 4 MiB bound
                {
                    Message message = logout(1024 * 1024);
                    session.send(message);
                }
                finished = true;
            });
        // Long enough for the connection's thread to run the session's timer, which needs the session lock
        // the waiting sender holds: that must not deadlock.
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        CHECK_FALSE(finished.load());
        paused = false;
        bulk.join();
        CHECK(finished.load());
        CHECK(waitFor(6) == 6);
        finish();
    }

    SECTION("aDisconnectWritesWhatWasSentBeforeIt")
    {
        // The session sends a Logout's reply and disconnects at once; the reply must still go out, as it
        // did when every send wrote straight away. No connection thread runs here, so only the flush in
        // disconnect() can write it.
        Message message = logout(0);
        session.send(message);
        connection->disconnect();
        CHECK(waitFor(1) == 1);
        finish();
    }

    SECTION("aDisconnectWaitsForAWriterMidWrite")
    {
        // A sender past the bound holds the writer's role, blocked on a peer that has stopped reading, when
        // another thread disconnects. The disconnect must wait for the role and then flush, not give the
        // queue up: dropping it would lose the messages queued behind the one being written, and cut that
        // one short on the wire.
        paused = true;
        std::thread bulk(
            [&]
            {
                for (int i = 0; i < 4; ++i) // the fourth goes past the 4 MiB bound and blocks writing
                {
                    Message message = logout(1024 * 1024);
                    session.send(message);
                }
            });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::atomic<bool> disconnected{false};
        std::thread disconnecter(
            [&]
            {
                connection->disconnect();
                disconnected = true;
            });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK_FALSE(disconnected.load()); // waiting for the writer
        paused = false;
        bulk.join();
        disconnecter.join();
        CHECK(waitFor(4) == 4);
        {
            std::lock_guard<std::mutex> lock(readMutex);
            Parser parser;
            parser.addToStream(received);
            std::string message;
            std::size_t framed = 0;
            while (parser.readFixMessage(message))
            {
                framed += message.size();
            }
            CHECK(framed == received.size()); // nothing cut short
        }
        finish();
    }

    SECTION("aDisconnectReleasesWaitingSenders")
    {
        paused = true; // the peer never reads again
        startWriter();
        std::atomic<bool> finished{false};
        std::thread bulk(
            [&]
            {
                for (int i = 0; i < 6; ++i)
                {
                    Message message = logout(1024 * 1024);
                    session.send(message);
                }
                finished = true;
            });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK_FALSE(finished.load()); // held at the bound
        connection->disconnect();     // the sender must give up, not hang
        bulk.join();
        CHECK(finished.load());
        finish();
    }
}

// SocketQueuedWrites (#166): opt-in, threaded transports only.
TEST_CASE("SocketQueuedWritesTests")
{
    auto settingsFor = [](const std::string &type, int port, const std::string &first, const std::string &second)
    {
        std::stringstream config;
        config << "[DEFAULT]\nConnectionType=" << type << "\nSocketAcceptPort=" << port
               << "\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" << port
               << "\nReconnectInterval=1\nHeartBtInt=30\nStartTime=00:00:00\nEndTime=00:00:00\n"
               << "UseDataDictionary=N\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=QWA\nTargetCompID=QWI\n"
               << "SocketQueuedWrites=" << first << "\n[SESSION]\nBeginString=FIX.4.4\nSenderCompID=QWA\n"
               << "TargetCompID=QWI\nSocketQueuedWrites=" << second << "\n";
        return SessionSettings(config);
    };
    NullApplication application;
    MemoryStoreFactory factory;

    SECTION("isOffByDefault")
    {
        Dictionary settings;
        CHECK_FALSE(getSocketQueuedWrites(settings));
        settings.setString(SOCKET_QUEUED_WRITES, "Y");
        CHECK(getSocketQueuedWrites(settings));
        settings.setString(SOCKET_QUEUED_WRITES, "maybe");
        CHECK_THROWS_AS(getSocketQueuedWrites(settings), ConfigError);
    }

    SECTION("isRefusedByTransportsWithoutIt")
    {
        const int port = freePort();
        {
            SocketAcceptor acceptor(application, factory, settingsFor("acceptor", port, "Y", "Y"));
            CHECK_THROWS_AS(acceptor.start(), ConfigError);
        }
        {
            SocketInitiator initiator(application, factory, settingsFor("initiator", port, "Y", "Y"));
            CHECK_THROWS_AS(initiator.start(), ConfigError);
        }
#if HAVE_SSL
        {
            ThreadedSSLSocketAcceptor acceptor(application, factory, settingsFor("acceptor", port, "Y", "Y"));
            CHECK_THROWS_AS(acceptor.start(), ConfigError);
        }
#endif
    }

    SECTION("mustAgreeAcrossAPort")
    {
        ThreadedSocketAcceptor acceptor(application, factory, settingsFor("acceptor", freePort(), "Y", "N"));
        CHECK_THROWS_AS(acceptor.start(), ConfigError);
    }

    SECTION("logsOnAndExchangesMessages")
    {
        const int port = freePort();
        ThreadedSocketAcceptor acceptor(application, factory, settingsFor("acceptor", port, "Y", "Y"));
        acceptor.start();
        std::stringstream config;
        config << "[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" << port
               << "\nReconnectInterval=1\nHeartBtInt=30\nStartTime=00:00:00\nEndTime=00:00:00\n"
               << "UseDataDictionary=N\nSocketQueuedWrites=Y\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=QWI\n"
               << "TargetCompID=QWA\n";
        SessionSettings initiatorSettings(config);
        ThreadedSocketInitiator initiator(application, factory, initiatorSettings);
        initiator.start();

        const SessionID initiatorID("FIX.4.2", "QWI", "QWA");
        Session *session = nullptr;
        for (int i = 0; i < 300 && !(session && session->isLoggedOn()); ++i)
        {
            session = Session::lookupSession(initiatorID);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        REQUIRE(session);
        REQUIRE(session->isLoggedOn());
        // A TestRequest is answered by a Heartbeat: both directions written by the connections' threads.
        const SEQNUM expected = session->getExpectedTargetNum() + 1;
        FIX42::TestRequest testRequest(TestReqID("QUEUED"));
        Session::sendToTarget(testRequest, initiatorID);
        for (int i = 0; i < 300 && session->getExpectedTargetNum() < expected; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(session->getExpectedTargetNum() >= expected);
        initiator.stop();
        acceptor.stop();
    }
}

namespace
{
SessionSettings stopSettings(const std::string &type, int port, const std::string &sender, const std::string &target)
{
    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=" << type << "\nSocketAcceptPort=" << port
           << "\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" << port
           << "\nReconnectInterval=1\nHeartBtInt=30\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=" << sender << "\nTargetCompID=" << target << "\n";
    return SessionSettings(config);
}

bool loggedOn(const SessionID &id)
{
    for (int i = 0; i < 500; ++i)
    {
        Session *session = Session::lookupSession(id);
        if (session && session->isLoggedOn())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// Whether something still listens on port: a bind to it with SO_REUSEADDR fails only then.
bool listening(int port)
{
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    const int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    const bool bound = ::bind(s, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
    ::close(s);
    return !bound;
}
} // namespace

// Acceptor::stop and Initiator::stop with more than one caller (#174): a second caller waits for the stop
// running, so that when it returns the transport's threads are joined, except on the transport's own threads,
// which that stop joins.
TEST_CASE("ConcurrentStopTests")
{
    NullApplication application;
    MemoryStoreFactory factory;

    SECTION("aSecondStopReturnsOnlyWhenTheFirstHasFinished")
    {
        // The window: the first stop has set m_stop and is joining the reactor's thread, which closes the
        // listener as it ends. A second stop() used to return there, with the listener still open and the
        // thread still running in the acceptor its caller could now destroy.
        for (int round = 0; round < 10; ++round)
        {
            INFO("round " << round);
            const int port = freePort();
            SocketAcceptor acceptor(application, factory, stopSettings("acceptor", port, "STOPA", "STOPI"));
            acceptor.start();
            SocketInitiator initiator(application, factory, stopSettings("initiator", port, "STOPI", "STOPA"));
            initiator.start();
            REQUIRE(loggedOn(SessionID("FIX.4.2", "STOPI", "STOPA")));
            std::thread first([&]() { acceptor.stop(); });
            while (!acceptor.isStopped())
            {
                std::this_thread::yield();
            }
            acceptor.stop();
            CHECK_FALSE(listening(port));
            first.join();
            initiator.stop();
        }
    }

    SECTION("stopsArrivingTogetherStopOnce")
    {
        for (int round = 0; round < 10; ++round)
        {
            INFO("round " << round);
            const int port = freePort();
            SocketAcceptor acceptor(application, factory, stopSettings("acceptor", port, "STOPA", "STOPI"));
            acceptor.start();
            SocketInitiator initiator(application, factory, stopSettings("initiator", port, "STOPI", "STOPA"));
            initiator.start();
            REQUIRE(loggedOn(SessionID("FIX.4.2", "STOPI", "STOPA")));
            std::atomic<bool> go{false};
            auto stopAcceptor = [&]()
            {
                while (!go)
                {
                }
                acceptor.stop();
            };
            auto stopInitiator = [&]()
            {
                while (!go)
                {
                }
                initiator.stop();
            };
            std::thread a1(stopAcceptor), a2(stopAcceptor), i1(stopInitiator), i2(stopInitiator);
            go = true;
            a1.join();
            a2.join();
            i1.join();
            i2.join();
            CHECK(acceptor.isStopped());
            CHECK(initiator.isStopped());
            CHECK_FALSE(listening(port));
        }
    }

    SECTION("aCallbackStoppingItsOwnTransportDoesNotDeadlock")
    {
        // A threaded transport's stop joins its connection threads, so one of them calling stop() while
        // another stop runs must not wait for it: it would wait for the stop that waits for it.
        struct StoppingApplication : NullApplication
        {
            ThreadedSocketAcceptor *acceptor = nullptr;
            std::atomic<bool> stopping{false};
            std::atomic<bool> returned{false};
            void fromApp(const Message &, const SessionID &)
                EXCEPT(FieldNotFound, IncorrectDataFormat, IncorrectTagValue, UnsupportedMessageType) override
            {
                stopping = true; // the test's thread starts its stop now
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                acceptor->stop();
                returned = true;
            }
        };
        StoppingApplication stopper;
        const int port = freePort();
        ThreadedSocketAcceptor acceptor(stopper, factory, stopSettings("acceptor", port, "STOPA", "STOPI"));
        stopper.acceptor = &acceptor;
        acceptor.start();
        SocketInitiator initiator(application, factory, stopSettings("initiator", port, "STOPI", "STOPA"));
        initiator.start();
        const SessionID id("FIX.4.2", "STOPI", "STOPA");
        REQUIRE(loggedOn(id));
        Message order;
        order.getHeader().setField(MsgType("D"));
        order.setField(ClOrdID("ORDER"));
        REQUIRE(Session::sendToTarget(order, id));
        for (int i = 0; i < 500 && !stopper.stopping; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        REQUIRE(stopper.stopping);
        std::atomic<bool> stopped{false};
        std::thread test(
            [&]()
            {
                acceptor.stop();
                stopped = true;
            });
        // The two waits in stop (logout, then onStop's isLoggedOn) are 10 s and 5 s at most.
        for (int i = 0; i < 400 && !(stopped && stopper.returned); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        CHECK(stopper.returned.load());
        CHECK(stopped.load());
        if (stopped)
        {
            test.join();
        }
        else
        {
            test.detach(); // deadlocked: never joined, so detaching it is safe for TSan (#113)
            FAIL("the stops deadlocked");
        }
        initiator.stop();
    }
}
