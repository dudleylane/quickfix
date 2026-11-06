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

#ifdef HAVE_MYSQL

#include "DatabaseLogTestCase.h"
#include <MySQLLog.h>

using namespace FIX;

namespace
{
struct mySQLLogFixture : DatabaseLogFixture
{
    mySQLLogFixture()
        : database(defaults.getString(MYSQL_LOG_DATABASE)), user(defaults.getString(MYSQL_LOG_USER)),
          password(defaults.getString(MYSQL_LOG_PASSWORD)), host(defaults.getString(MYSQL_LOG_HOST)),
          port((short)defaults.getInt(MYSQL_LOG_PORT)), connection(database, user, password, host, port)
    {
        emptyDatabaseLogTables(*this);
    }

    void execute(const std::string &sql)
    {
        MySQLQuery query(sql);
        REQUIRE(connection.execute(query));
    }

    std::vector<std::string> select(const std::string &sql)
    {
        MySQLQuery query(sql);
        REQUIRE(connection.execute(query));
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
    short port;
    MySQLConnection connection;
};

using Rows = std::vector<std::string>;
} // namespace

TEST_CASE_METHOD(mySQLLogFixture, "MySQLLogTests")
{
    SECTION("whatEveryDatabaseLogDoes")
    {
        checkDatabaseLog<MySQLLogFactory>(*this, MYSQL_LOG_INCOMING_TABLE, MYSQL_LOG_OUTGOING_TABLE,
                                          MYSQL_LOG_EVENT_TABLE);
    }

    SECTION("theTimeWrittenIsUtc")
    {
        MySQLLogFactory factory(settingsFor(defaults));
        Log *log = factory.create(plain);
        log->onEvent("event");
        factory.destroy(log);

        MySQLQuery query("SELECT ABS(TIMESTAMPDIFF(SECOND, time, UTC_TIMESTAMP())) < 300, "
                         "time_milliseconds BETWEEN 0 AND 999 FROM event_log");
        REQUIRE(connection.execute(query));
        REQUIRE(query.rows() == 1);
        CHECK(std::string(query.getValue(0, 0)) == "1");
        CHECK(std::string(query.getValue(0, 1)) == "1");
    }

    SECTION("aLogConstructedDirectlyWrites")
    {
        {
            MySQLLog log(plain, database, user, password, host, port);
            log.onIncoming("session");
        }
        {
            MySQLLog log(database, user, password, host, port);
            log.onIncoming("global");
        }

        CHECK(databaseLogRows(*this, "messages_log") ==
              Rows{"FIX.4.2|LOG|TEST|NULL|session", "NULL|NULL|NULL|NULL|global"});
    }

    SECTION("aPooledConnectionOutlivesAnyOneLog")
    {
        Dictionary pooled = defaults;
        pooled.setBool(MYSQL_LOG_USECONNECTIONPOOL, true);
        MySQLLogFactory factory(settingsFor(pooled));
        Log *first = factory.create(plain);
        Log *second = factory.create(other);
        first->onIncoming("first");
        factory.destroy(first);
        second->onIncoming("second");
        factory.destroy(second);

        CHECK(databaseLogRows(*this, "messages_log") ==
              Rows{"FIX.4.2|LOG|TEST|NULL|first", "FIX.4.2|OTHER|TEST|NULL|second"});
    }
}

TEST_CASE_METHOD(mySQLLogFixture, "MySQLConnectionTests")
{
    SECTION("aKilledConnectionReconnects")
    {
        MySQLConnection victim(database, user, password, host, port);
        MySQLQuery id("SELECT CONNECTION_ID()");
        REQUIRE(victim.execute(id));
        MySQLQuery kill(std::string("KILL ") + id.getValue(0, 0));
        REQUIRE(connection.execute(kill));

        MySQLQuery next("SELECT 41 + 1");
        REQUIRE(victim.execute(next));
        CHECK(std::string(next.getValue(0, 0)) == "42");
    }

    SECTION("aFailedConnectionSaysWhy")
    {
        // Port 1 is not MySQL. The reason comes from the failed connect, not from a ping of the handle it left.
        try
        {
            MySQLConnection refused(database, user, password, host, 1);
            FAIL("connected to port 1");
        }
        catch (const ConfigError &e)
        {
            const std::string what = e.what();
            CHECK(what.find("quickfix") != std::string::npos);
            CHECK(what.find("gone away") == std::string::npos);
        }
    }
}

#endif
