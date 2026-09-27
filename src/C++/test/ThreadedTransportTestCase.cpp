#include "config.h"

#include <Application.h>
#include <MessageStore.h>
#include <SessionSettings.h>
#include <ThreadedSocketAcceptor.h>
#include <ThreadedSocketConnection.h>
#if (HAVE_SSL > 0)
#include <ThreadedSSLSocketConnection.h>
#include <UtilitySSL.h>
#endif

#include "TestHelper.h"
#include "catch_amalgamated.hpp"

#include <atomic>
#include <chrono>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sstream>
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
