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

#ifdef HAVE_ODBC

#include "DatabaseLogTestCase.h"
#include <OdbcLog.h>

#include <chrono>
#include <stdlib.h>
#include <thread>

using namespace FIX;

namespace
{
std::vector<std::string> selectFrom(OdbcConnection &connection, const std::string &sql)
{
    OdbcQuery query(sql);
    const bool succeeded = connection.execute(query);
    INFO(query.reason());
    REQUIRE(succeeded);
    std::vector<std::string> result;
    while (query.fetch())
    {
        char value[4096];
        SQLLEN length = 0;
        REQUIRE(odbcSuccess(SQLGetData(query.statement(), 1, SQL_C_CHAR, value, sizeof(value), &length)));
        REQUIRE(length < (SQLLEN)sizeof(value));
        result.emplace_back(value, length);
    }
    return result;
}

struct odbcLogFixture : DatabaseLogFixture
{
    odbcLogFixture()
        : user(defaults.getString(ODBC_LOG_USER)), password(defaults.getString(ODBC_LOG_PASSWORD)),
          connectionString(defaults.getString(ODBC_LOG_CONNECTION_STRING)), connection(user, password, connectionString)
    {
        emptyDatabaseLogTables(*this);
    }

    void execute(const std::string &sql)
    {
        OdbcQuery query(sql);
        const bool succeeded = connection.execute(query);
        INFO(query.reason());
        REQUIRE(succeeded);
    }

    std::vector<std::string> select(const std::string &sql) { return selectFrom(connection, sql); }

    std::string user;
    std::string password;
    std::string connectionString;
    OdbcConnection connection;
};

using Rows = std::vector<std::string>;
} // namespace

TEST_CASE_METHOD(odbcLogFixture, "OdbcLogTests")
{
    SECTION("whatEveryDatabaseLogDoes")
    {
        checkDatabaseLog<OdbcLogFactory>(*this, ODBC_LOG_INCOMING_TABLE, ODBC_LOG_OUTGOING_TABLE, ODBC_LOG_EVENT_TABLE);
    }

    SECTION("whatEveryLogWritingTextDoes") { checkDatabaseLogText<OdbcLogFactory>(*this); }

    SECTION("aLogConstructedDirectlyWrites")
    {
        {
            OdbcLog log(plain, user, password, connectionString);
            log.onIncoming("session");
        }
        {
            OdbcLog log(user, password, connectionString);
            log.onIncoming("global");
        }

        CHECK(databaseLogRows(*this, "messages_log") ==
              Rows{"FIX.4.2|LOG|TEST|NULL|session", "NULL|NULL|NULL|NULL|global"});
    }

    SECTION("everyByteOverSevenBitsIsWrittenInHex")
    {
        OdbcLogFactory factory(settingsFor(defaults));
        Log *log = factory.create(plain);
        log->onIncoming("58=caf\xC3\xA9\x01");
        factory.destroy(log);

        CHECK(select("SELECT text FROM messages_log") == Rows{"58=caf\\xC3\\xA9\x01"});
    }
}

TEST_CASE_METHOD(odbcLogFixture, "OdbcConnectionTests")
{
    SECTION("aKilledConnectionReconnects")
    {
        // Ending another connection takes the server's own SQL. This is PostgreSQL's, which ut.cfg's driver reaches.
        OdbcConnection victim(user, password, connectionString);
        const std::string id = selectFrom(victim, "SELECT pg_backend_pid()").at(0);
        execute("SELECT pg_terminate_backend(" + id + ")");
        // pg_terminate_backend only signals. A query that beat the signal would not have needed to reconnect.
        while (select("SELECT COUNT(*) FROM pg_stat_activity WHERE pid = " + id) != Rows{"0"})
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        CHECK(selectFrom(victim, "SELECT 41 + 1") == Rows{"42"});
    }

    SECTION("aConnectionThatCannotBeRemadeFailsTheQuery")
    {
        OdbcConnection victim(user, password, connectionString);
        const std::string id = selectFrom(victim, "SELECT pg_backend_pid()").at(0);
        execute("SELECT pg_terminate_backend(" + id + ")");
        while (select("SELECT COUNT(*) FROM pg_stat_activity WHERE pid = " + id) != Rows{"0"})
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // libpq, which psqlODBC connects through, reads this on every connect, and the server refuses what it names.
        setenv("PGOPTIONS", "-c no_such_setting=1", 1);
        OdbcQuery query("SELECT 41 + 1");
        const bool succeeded = victim.execute(query);
        const bool succeededAgain = victim.execute(query);
        unsetenv("PGOPTIONS");

        CHECK_FALSE(succeeded);
        CHECK_FALSE(succeededAgain);
        CHECK_THROWS_AS(query.throwException(), IOException);
        CHECK(query.reason().find("no_such_setting") != std::string::npos);
        CHECK(selectFrom(victim, "SELECT 41 + 1") == Rows{"42"});
    }

    SECTION("aFailedConnectionSaysWhy")
    {
        try
        {
            OdbcConnection refused(user, password, "DRIVER={no such driver};");
            FAIL("connected through a driver that does not exist");
        }
        catch (const ConfigError &e)
        {
            CHECK(std::string(e.what()).find("no such driver") != std::string::npos);
        }
    }
}

#endif
