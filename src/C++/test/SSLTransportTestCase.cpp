#include "config.h"

#if (HAVE_SSL > 0)

#include <SSLSocketConnection.h>
#include <SocketMonitor.h>
#include <ThreadedSSLSocketConnection.h>
#include <UtilitySSL.h>

#include "catch_amalgamated.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace FIX;

// Each SSL transport must close a connection's fd exactly once, by that
// transport's single owner: the SocketMonitor in the reactor (sockets are
// bound with BIO_NOCLOSE), the BIO in the threaded transport (BIO_CLOSE). A
// second close() is not harmless -- if anything received the same descriptor
// number in between, it closes that instead. Neither test needs a TLS
// handshake; the fd lifecycle is independent of it.

namespace
{
bool isOpen(int fd) { return ::fcntl(fd, F_GETFD) != -1; }

SSL *newSsl(SSL_CTX *ctx, int fd, int closeFlag)
{
    SSL *ssl = SSL_new(ctx);
    BIO *bio = BIO_new_socket(fd, closeFlag);
    SSL_set_bio(ssl, bio, bio);
    return ssl;
}
} // namespace

TEST_CASE("SSLTransportTests")
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_method());
    REQUIRE(ctx != nullptr);

    // In the reactor the monitor closes the fd when it drops the socket, and
    // that can happen either side of the connection being deleted -- both
    // orders occur, so both are tested. SSLSocketConnection's own disconnect()
    // is private (it implements Responder), and its body is exactly
    // m_pMonitor->drop(m_socket), so the tests call drop() directly.

    SECTION("reactorDeleteAfterDropDoesNotCloseTheFdAgain")
    {
        // The session-initiated order: the monitor drops first, and the
        // connection is deleted when the drop is reported on the next block().
        int pair[2];
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        const int fd = pair[0];

        SocketMonitor monitor;
        REQUIRE(monitor.addRead(fd));
        SSLSocketConnection *connection =
            new SSLSocketConnection(fd, newSsl(ctx, fd, BIO_NOCLOSE), SSLSocketConnection::Sessions(), &monitor);

        REQUIRE(monitor.drop(fd));
        CHECK_FALSE(isOpen(fd));

        // Something else now receives that descriptor number -- another
        // connection's socket, a store file reopened during the disconnect. The
        // kernel hands out the lowest free number, which is usually the one the
        // drop just released, so open() normally lands on fd by itself; that is
        // exactly how the reuse happens in production. Force it only if not.
        const int bystander = ::open("/dev/null", O_RDONLY);
        REQUIRE(bystander >= 0);
        if (bystander != fd)
        {
            REQUIRE(::dup2(bystander, fd) == fd);
            ::close(bystander);
        }

        delete connection; // must not close the fd a second time
        CHECK(isOpen(fd));

        ::close(fd);
        ::close(pair[1]);
    }

    SECTION("reactorDeleteBeforeDropLeavesTheFdToTheMonitor")
    {
        // The order ServerWrapper::onError uses, and the one strace showed:
        // onDisconnect deletes the connection, then the monitor drops.
        int pair[2];
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        const int fd = pair[0];

        SocketMonitor monitor;
        REQUIRE(monitor.addRead(fd));
        SSLSocketConnection *connection =
            new SSLSocketConnection(fd, newSsl(ctx, fd, BIO_NOCLOSE), SSLSocketConnection::Sessions(), &monitor);

        delete connection; // the monitor still owns the fd
        CHECK(isOpen(fd));

        REQUIRE(monitor.drop(fd)); // and closes it, once
        CHECK_FALSE(isOpen(fd));

        ::close(pair[1]);
    }

    SECTION("threadedDisconnectKeepsTheFdUntilTheBioReleasesIt")
    {
        int pair[2];
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        const int fd = pair[0];

        SSL *ssl = newSsl(ctx, fd, BIO_CLOSE);
        ThreadedSSLSocketConnection *connection =
            new ThreadedSSLSocketConnection(fd, ssl, ThreadedSSLSocketConnection::Sessions(), nullptr);

        // disconnect() can be called from another thread while the reader sits
        // in select() on this fd. It must wake the reader without giving the
        // number up: if it closed the fd here, the number would be free for
        // reuse while the reader still used it, and SSL_free would close it
        // again below.
        connection->disconnect();
        CHECK(isOpen(fd));

        delete connection;
        SSL_free(ssl); // what removeThread and onStop do: the BIO closes the fd
        CHECK_FALSE(isOpen(fd));

        ::close(pair[1]);
    }

    SSL_CTX_free(ctx);
}

#endif
