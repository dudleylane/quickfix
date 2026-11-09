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

#ifdef HAVE_POSTGRESQL

#include "DatabaseLogTestCase.h"
#include <PortSetting.h>
#include <PostgreSQLLog.h>

#include <chrono>
#include <stdlib.h>
#include <thread>

using namespace FIX;

namespace
{
/// PGCLIENTENCODING for the connections made while this lives, which libpq reads as it connects.
struct ClientEncoding
{
    explicit ClientEncoding(const char *encoding) { setenv("PGCLIENTENCODING", encoding, 1); }
    ~ClientEncoding() { unsetenv("PGCLIENTENCODING"); }
};

struct postgreSQLLogFixture : DatabaseLogFixture
{
    postgreSQLLogFixture()
        : database(defaults.getString(POSTGRESQL_LOG_DATABASE)), user(defaults.getString(POSTGRESQL_LOG_USER)),
          password(defaults.getString(POSTGRESQL_LOG_PASSWORD)), host(defaults.getString(POSTGRESQL_LOG_HOST)),
          port(defaults.has(POSTGRESQL_LOG_PORT) ? getPortSetting(defaults, POSTGRESQL_LOG_PORT, true) : 0),
          connection(database, user, password, host, port)
    {
        emptyDatabaseLogTables(*this);
    }

    void execute(const std::string &sql)
    {
        PostgreSQLQuery query(sql);
        const bool succeeded = connection.execute(query);
        INFO(query.reason());
        REQUIRE(succeeded);
    }

    std::vector<std::string> select(const std::string &sql)
    {
        PostgreSQLQuery query(sql);
        const bool succeeded = connection.execute(query);
        INFO(query.reason());
        REQUIRE(succeeded);
        std::vector<std::string> result;
        const int rows = query.rows();
        for (int row = 0; row < rows; ++row)
        {
            result.push_back(query.getValue(row, 0));
        }
        return result;
    }

    std::string database;
    std::string user;
    std::string password;
    std::string host;
    unsigned short port;
    PostgreSQLConnection connection;
};

using Rows = std::vector<std::string>;
} // namespace

TEST_CASE_METHOD(postgreSQLLogFixture, "PostgreSQLLogTests")
{
    SECTION("whatEveryDatabaseLogDoes")
    {
        checkDatabaseLog<PostgreSQLLogFactory>(*this, POSTGRESQL_LOG_INCOMING_TABLE, POSTGRESQL_LOG_OUTGOING_TABLE,
                                               POSTGRESQL_LOG_EVENT_TABLE);
    }

    SECTION("whatEveryLogWritingTextDoes") { checkDatabaseLogText<PostgreSQLLogFactory>(*this); }

    SECTION("theTimeWrittenIsUtc")
    {
        PostgreSQLLogFactory factory(settingsFor(defaults));
        Log *log = factory.create(plain);
        log->onEvent("event");
        factory.destroy(log);

        CHECK(select("SELECT ABS(EXTRACT(EPOCH FROM (time - (NOW() AT TIME ZONE 'UTC')))) < 300 FROM event_log") ==
              Rows{"t"});
    }

    SECTION("aLogConstructedDirectlyWrites")
    {
        {
            PostgreSQLLog log(plain, database, user, password, host, port);
            log.onIncoming("session");
        }
        {
            PostgreSQLLog log(database, user, password, host, port);
            log.onIncoming("global");
        }

        CHECK(databaseLogRows(*this, "messages_log") ==
              Rows{"FIX.4.2|LOG|TEST|NULL|session", "NULL|NULL|NULL|NULL|global"});
    }

    SECTION("aSettingThatCannotBeAPortIsAConfigError")
    {
        Dictionary refused = defaults;
        refused.setString(POSTGRESQL_LOG_PORT, GENERATE("-1", "65536", "not a number"));
        PostgreSQLLogFactory factory(settingsFor(refused));
        CHECK_THROWS_AS(factory.create(plain), ConfigError);
    }

    SECTION("aPooledConnectionOutlivesAnyOneLog")
    {
        Dictionary pooled = defaults;
        pooled.setBool(POSTGRESQL_LOG_USECONNECTIONPOOL, true);
        PostgreSQLLogFactory factory(settingsFor(pooled));
        Log *first = factory.create(plain);
        Log *second = factory.create(other);
        first->onIncoming("first");
        factory.destroy(first);
        second->onIncoming("second");
        factory.destroy(second);

        CHECK(databaseLogRows(*this, "messages_log") ==
              Rows{"FIX.4.2|LOG|TEST|NULL|first", "FIX.4.2|OTHER|TEST|NULL|second"});
    }

    SECTION("wellFormedUtf8IsWrittenAsItIsOnAUtf8Connection")
    {
        REQUIRE(select("SHOW server_encoding") == Rows{"UTF8"});
        const std::string text = "58=caf\xC3\xA9 \xE2\x82\xAC \xF0\x9D\x84\x9E\x01";
        PostgreSQLLogFactory factory(settingsFor(defaults));
        Log *log = factory.create(plain);
        log->onIncoming(text);
        factory.destroy(log);

        CHECK(select("SELECT text FROM messages_log") == Rows{text});
    }

    SECTION("everyByteOverSevenBitsIsWrittenInHexOnAnyOtherConnection")
    {
        PostgreSQLLogFactory factory(settingsFor(defaults));
        Log *log = nullptr;
        {
            ClientEncoding latin1("LATIN1");
            log = factory.create(plain);
        }
        log->onIncoming("58=caf\xC3\xA9\x01");
        factory.destroy(log);

        CHECK(select("SELECT text FROM messages_log") == Rows{"58=caf\\xC3\\xA9\x01"});
    }
}

TEST_CASE_METHOD(postgreSQLLogFixture, "PostgreSQLConnectionTests")
{
    SECTION("aKilledConnectionReconnects")
    {
        PostgreSQLConnection victim(database, user, password, host, port);
        PostgreSQLQuery pid("SELECT pg_backend_pid()");
        REQUIRE(victim.execute(pid));
        const std::string id = pid.getValue(0, 0);
        execute("SELECT pg_terminate_backend(" + id + ")");
        // pg_terminate_backend only signals. A query that beat the signal would not have needed to reconnect.
        while (select("SELECT COUNT(*) FROM pg_stat_activity WHERE pid = " + id) != Rows{"0"})
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        PostgreSQLQuery next("SELECT 41 + 1");
        REQUIRE(victim.execute(next));
        CHECK(std::string(next.getValue(0, 0)) == "42");
    }

    SECTION("aFailedConnectionSaysWhy")
    {
        // Port 1 is not PostgreSQL. No destructor runs after this throw, so anything it holds is LeakSanitizer's to
        // find.
        try
        {
            PostgreSQLConnection refused(database, user, password, host, 1);
            FAIL("connected to port 1");
        }
        catch (const ConfigError &e)
        {
            const std::string what = e.what();
            CHECK(what.find("quickfix") != std::string::npos);
            CHECK(what.find("refused") != std::string::npos);
            CHECK(what.back() == ']');
        }
    }
}

#endif
