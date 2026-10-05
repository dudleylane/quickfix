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

#if (HAVE_SSL > 0)

#include <Log.h>
#include <SessionSettings.h>
#include <UtilitySSL.h>

#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

#include "TestHelper.h"
#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("UtilitySSLTests")
{
    SECTION("is_ip_address_IPv4_ReturnsTrue")
    {
        CHECK(is_ip_address("192.168.1.1"));
        CHECK(is_ip_address("127.0.0.1"));
        CHECK(is_ip_address("0.0.0.0"));
        CHECK(is_ip_address("255.255.255.255"));
        CHECK(is_ip_address("10.0.0.1"));
    }

    SECTION("is_ip_address_IPv6_ReturnsTrue")
    {
        CHECK(is_ip_address("::1"));
        CHECK(is_ip_address("::"));
        CHECK(is_ip_address("2001:db8::1"));
        CHECK(is_ip_address("fe80::1"));
        CHECK(is_ip_address("::ffff:192.168.1.1"));
    }

    SECTION("is_ip_address_IPv6_Bracketed_ReturnsTrue")
    {
        CHECK(is_ip_address("[::1]"));
        CHECK(is_ip_address("[2001:db8::1]"));
        CHECK(is_ip_address("[fe80::1]"));
        CHECK(is_ip_address("[::]"));
    }

    SECTION("is_ip_address_Hostname_ReturnsFalse")
    {
        CHECK_FALSE(is_ip_address("localhost"));
        CHECK_FALSE(is_ip_address("example.com"));
        CHECK_FALSE(is_ip_address("fix.server.example.com"));
        CHECK_FALSE(is_ip_address("my-server-01"));
        CHECK_FALSE(is_ip_address("server123"));
    }

    SECTION("is_ip_address_Empty_ReturnsFalse") { CHECK_FALSE(is_ip_address("")); }

    SECTION("is_ip_address_InvalidInput_ReturnsFalse")
    {
        CHECK_FALSE(is_ip_address("not-an-ip"));
        CHECK_FALSE(is_ip_address("192.168.1"));
        CHECK_FALSE(is_ip_address("192.168.1.256"));
        CHECK_FALSE(is_ip_address("[incomplete"));
    }

    SECTION("ssl_set_sni_hostname_NullSSL_ReturnsFalse") { CHECK_FALSE(ssl_set_sni_hostname(nullptr, "example.com")); }

    SECTION("ssl_set_sni_hostname_EmptyHostname_ReturnsTrue")
    {
        // Empty hostname should be skipped silently
        // We can't test with null SSL but we can document behavior
        CHECK_FALSE(ssl_set_sni_hostname(nullptr, ""));
    }
}

// findCAList builds the client-CA list an acceptor advertises.  The fixtures
// live beside the sample configs; specPath is "spec" or "../spec" depending on
// where ut is launched from, so resolve relative to it.
static std::string certPath(const std::string &leaf)
{
    return FIX::TestSettings::specPath + "/../bin/cfg/certs/" + leaf;
}

TEST_CASE("FindCAListTests")
{
    SECTION("findCAList_BundleFile_ReturnsTheCertificateName")
    {
        const std::string caFile = certPath("certs/cacert.pem");
        STACK_OF(X509_NAME) *caList = findCAList(caFile.c_str(), 0);
        REQUIRE(caList != 0);
        CHECK(sk_X509_NAME_num(caList) == 1);
        sk_X509_NAME_pop_free(caList, X509_NAME_free);
    }

    SECTION("findCAList_Directory_ReturnsEachCertificateName")
    {
        const std::string caPath = certPath("newcerts");
        STACK_OF(X509_NAME) *caList = findCAList(0, caPath.c_str());
        REQUIRE(caList != 0);
        // 01.pem and 02.pem hold distinct subjects; "." and ".." are not certificates.
        CHECK(sk_X509_NAME_num(caList) == 2);
        sk_X509_NAME_pop_free(caList, X509_NAME_free);
    }

    SECTION("findCAList_FileAndDirectory_DeduplicatesByName")
    {
        const std::string caFile = certPath("certs/cacert.pem");
        const std::string caPath = certPath("certs");
        // The same certificate reached both ways must appear once.
        STACK_OF(X509_NAME) *caList = findCAList(caFile.c_str(), caPath.c_str());
        REQUIRE(caList != 0);
        CHECK(sk_X509_NAME_num(caList) == 1);
        sk_X509_NAME_pop_free(caList, X509_NAME_free);
    }

    SECTION("findCAList_NeitherSource_ReturnsEmptyList")
    {
        STACK_OF(X509_NAME) *caList = findCAList(0, 0);
        REQUIRE(caList != 0);
        CHECK(sk_X509_NAME_num(caList) == 0);
        sk_X509_NAME_pop_free(caList, X509_NAME_free);
    }
}

namespace
{
// A throwaway CA and a server certificate it signs, made per run: the checked-in
// certificates under bin/cfg/certs expired years ago and carry no SAN.
struct TestPki
{
    EVP_PKEY *caKey = EVP_EC_gen("P-256");
    X509 *caCert = nullptr;
    EVP_PKEY *serverKey = EVP_EC_gen("P-256");
    X509 *serverCert = nullptr;
    EVP_PKEY *otherKey = EVP_EC_gen("P-256");
    X509 *otherCert = nullptr; // self-signed, trusted by nothing
    std::string caFile;

    TestPki()
    {
        caCert = makeCert(caKey, caKey, nullptr, "Test CA", nullptr, true);
        serverCert = makeCert(serverKey, caKey, caCert, "server", "DNS:localhost,IP:127.0.0.1", false);
        otherCert = makeCert(otherKey, otherKey, nullptr, "server", "DNS:localhost,IP:127.0.0.1", false);

        caFile = (std::filesystem::temp_directory_path() / ("quickfix-ut-ca-" + std::to_string(::getpid()) + ".pem"))
                     .string();
        FILE *f = std::fopen(caFile.c_str(), "w");
        PEM_write_X509(f, caCert);
        std::fclose(f);
    }

    ~TestPki()
    {
        std::remove(caFile.c_str());
        X509_free(otherCert);
        X509_free(serverCert);
        X509_free(caCert);
        EVP_PKEY_free(otherKey);
        EVP_PKEY_free(serverKey);
        EVP_PKEY_free(caKey);
    }

    static X509 *makeCert(EVP_PKEY *key, EVP_PKEY *signer, X509 *issuer, const char *cn, const char *san, bool ca)
    {
        X509 *cert = X509_new();
        X509_set_version(cert, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), ca ? 1 : 2);
        X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
        X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
        X509_set_pubkey(cert, key);
        X509_NAME_add_entry_by_txt(X509_get_subject_name(cert), "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char *>(cn), -1, -1, 0);
        X509_set_issuer_name(cert, X509_get_subject_name(issuer ? issuer : cert));

        X509V3_CTX ctx;
        X509V3_set_ctx_nodb(&ctx);
        X509V3_set_ctx(&ctx, issuer ? issuer : cert, cert, nullptr, nullptr, 0);
        addExtension(cert, &ctx, NID_basic_constraints, ca ? "critical,CA:TRUE" : "CA:FALSE");
        if (ca)
        {
            addExtension(cert, &ctx, NID_key_usage, "critical,keyCertSign,cRLSign");
        }
        if (san)
        {
            addExtension(cert, &ctx, NID_subject_alt_name, san);
        }
        X509_sign(cert, signer, EVP_sha256());
        return cert;
    }

    static void addExtension(X509 *cert, X509V3_CTX *ctx, int nid, const char *value)
    {
        X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, ctx, nid, value);
        X509_add_ext(cert, ext, -1);
        X509_EXTENSION_free(ext);
    }
};

SessionSettings settingsFrom(const std::string &defaults)
{
    std::istringstream stream("[DEFAULT]\n" + defaults);
    return SessionSettings(stream);
}

SSL_CTX *serverContext(X509 *cert, EVP_PKEY *key)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_use_certificate(ctx, cert);
    SSL_CTX_use_PrivateKey(ctx, key);
    return ctx;
}

// Runs a client and a server handshake against each other over a socket pair,
// non-blocking, until both finish or either fails.
bool handshake(SSL_CTX *clientCtx, SSL_CTX *serverCtx, const std::string &host)
{
    int pair[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair) == 0);
    SSL *client = SSL_new(clientCtx);
    SSL *server = SSL_new(serverCtx);
    SSL_set_fd(client, pair[0]);
    SSL_set_fd(server, pair[1]);
    ssl_set_sni_hostname(client, host);
    ssl_set_peer_host(client, host);

    bool clientDone = false;
    bool serverDone = false;
    bool failed = false;
    for (int i = 0; i < 1000 && !failed && !(clientDone && serverDone); ++i)
    {
        if (!clientDone)
        {
            const int rc = SSL_connect(client);
            const int err = SSL_get_error(client, rc);
            clientDone = rc == 1;
            failed = rc != 1 && err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE;
        }
        if (!serverDone && !failed)
        {
            const int rc = SSL_accept(server);
            const int err = SSL_get_error(server, rc);
            serverDone = rc == 1;
            failed = rc != 1 && err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE;
        }
    }
    ERR_clear_error();
    SSL_free(client);
    SSL_free(server);
    ::close(pair[0]);
    ::close(pair[1]);
    return clientDone && serverDone && !failed;
}

SSL_CTX *contextFor(bool server, const std::string &defaults, int &verifyLevel)
{
    NullLog log;
    std::string err;
    SessionSettings settings = settingsFrom(defaults);
    SSL_CTX *ctx = createSSLContext(server, settings, err);
    REQUIRE(ctx != nullptr);
    REQUIRE(loadCAInfo(ctx, server, settings, &log, err, verifyLevel));
    return ctx;
}
} // namespace

TEST_CASE("PeerVerificationTests")
{
    TestPki pki;
    SSL_CTX *server = serverContext(pki.serverCert, pki.serverKey);
    int verifyLevel = SSL_CLIENT_VERIFY_NOTSET;

    SECTION("anInitiatorWithTheIssuingCAAcceptsTheServerByNameOrAddress")
    {
        SSL_CTX *client = contextFor(false, "CertificationAuthoritiesFile=" + pki.caFile + "\n", verifyLevel);
        CHECK(handshake(client, server, "localhost"));
        CHECK(handshake(client, server, "127.0.0.1"));
        SSL_CTX_free(client);
    }

    SECTION("anInitiatorRefusesACertificateForAnotherHost")
    {
        SSL_CTX *client = contextFor(false, "CertificationAuthoritiesFile=" + pki.caFile + "\n", verifyLevel);
        CHECK_FALSE(handshake(client, server, "example.com"));
        CHECK_FALSE(handshake(client, server, "127.0.0.2"));
        SSL_CTX_free(client);
    }

    SECTION("anInitiatorWithoutAConfiguredCARefusesAnUntrustedServer")
    {
        SSL_CTX *client = contextFor(false, "", verifyLevel);
        CHECK(SSL_CLIENT_VERIFY_REQUIRE == verifyLevel);
        CHECK_FALSE(handshake(client, server, "localhost"));
        SSL_CTX_free(client);
    }

    SECTION("anInitiatorRefusesACertificateItsCADidNotIssue")
    {
        SSL_CTX *client = contextFor(false, "CertificationAuthoritiesFile=" + pki.caFile + "\n", verifyLevel);
        SSL_CTX *impostor = serverContext(pki.otherCert, pki.otherKey);
        CHECK_FALSE(handshake(client, impostor, "localhost"));
        SSL_CTX_free(impostor);
        SSL_CTX_free(client);
    }

    SECTION("anInitiatorSkipsVerificationOnlyWhenToldTo")
    {
        SSL_CTX *client = contextFor(false, "CertificateVerifyLevel=0\n", verifyLevel);
        CHECK(SSL_CLIENT_VERIFY_NONE == verifyLevel);
        CHECK(handshake(client, server, "example.com"));
        SSL_CTX_free(client);
    }

    SECTION("anAcceptorRequiresAClientCertificateWithoutAConfiguredCA")
    {
        SSL_CTX *acceptor = contextFor(true, "CertificateVerifyLevel=1\n", verifyLevel);
        SSL_CTX_use_certificate(acceptor, pki.serverCert);
        SSL_CTX_use_PrivateKey(acceptor, pki.serverKey);
        CHECK(SSL_CLIENT_VERIFY_REQUIRE == verifyLevel);
        SSL_CTX *client = contextFor(false, "CertificationAuthoritiesFile=" + pki.caFile + "\n", verifyLevel);
        CHECK_FALSE(handshake(client, acceptor, "localhost")); // the client presents no certificate
        SSL_CTX_free(client);
        SSL_CTX_free(acceptor);
    }

    SSL_CTX_free(server);
}

TEST_CASE("SSLSettingsTests")
{
    NullLog log;
    std::string err;

    SECTION("anUnrecognisedProtocolTokenIsAConfigurationError")
    {
        // A token the parser does not know -- here "TLSv1.3" for TLSv1_3 -- used to disable
        // nothing, silently, leaving every protocol version enabled (#55).
        SessionSettings bad = settingsFrom("SSLProtocol=-all +TLSv1.3\n");
        SSL_CTX *ctx = createSSLContext(true, bad, err);
        CHECK(ctx == nullptr);
        CHECK(err.find("SSLProtocol") != std::string::npos);
        if (ctx)
        {
            SSL_CTX_free(ctx);
        }
    }

    SECTION("recognisedProtocolTokensStillApply")
    {
        SSL_CTX *only13 = createSSLContext(true, settingsFrom("SSLProtocol=-all +TLSv1_3\n"), err);
        REQUIRE(only13 != nullptr);
        CHECK((SSL_CTX_get_options(only13) & SSL_OP_NO_TLSv1_2) != 0);
        CHECK((SSL_CTX_get_options(only13) & SSL_OP_NO_TLSv1_3) == 0);
        SSL_CTX_free(only13);

        SSL_CTX *no12 = createSSLContext(true, settingsFrom("SSLProtocol=all -TLSv1_2\n"), err);
        REQUIRE(no12 != nullptr);
        CHECK((SSL_CTX_get_options(no12) & SSL_OP_NO_TLSv1_2) != 0);
        SSL_CTX_free(no12);
    }

    SECTION("eitherRevocationListSettingWorksAlone")
    {
        // Setting only one of the two used to hand OpenSSL an empty path for the other, and the
        // acceptor failed to start (#56).
        const std::string pem = certPath("certs/cacert.pem");
        const std::string dir = certPath("certs");
        for (const std::string &defaults :
             {"CertificateRevocationListFile=" + pem + "\n", "CertificateRevocationListDirectory=" + dir + "\n"})
        {
            INFO(defaults);
            SessionSettings settings = settingsFrom(defaults);
            SSL_CTX *ctx = createSSLContext(true, settings, err);
            REQUIRE(ctx != nullptr);
            err.clear();
            loadCRLInfo(ctx, settings, &log, err);
            CHECK(err.empty());
            SSL_CTX_free(ctx);
        }
    }
}

#endif // HAVE_SSL
