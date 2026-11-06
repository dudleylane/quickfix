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

#include "TestHelper.h"
#include <MySQLLog.h>
#include <SessionSettings.h>

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

using namespace FIX;

namespace
{
struct mySQLLogFixture
{
    mySQLLogFixture()
        : defaults(TestSettings::sessionSettings.get()), database(defaults.getString(MYSQL_LOG_DATABASE)),
          user(defaults.getString(MYSQL_LOG_USER)), password(defaults.getString(MYSQL_LOG_PASSWORD)),
          host(defaults.getString(MYSQL_LOG_HOST)), port((short)defaults.getInt(MYSQL_LOG_PORT)),
          connection(database, user, password, host, port),
          plain(BeginString("FIX.4.2"), SenderCompID("LOG"), TargetCompID("TEST")),
          qualified(BeginString("FIX.4.2"), SenderCompID("LOG"), TargetCompID("TEST"), "QUALIFIED"),
          other(BeginString("FIX.4.2"), SenderCompID("OTHER"), TargetCompID("TEST"))
    {
        defaults.setString(CONNECTION_TYPE, "acceptor");
        for (const char *table : {"messages_log", "event_log", "messages_backup_log", "event_backup_log"})
        {
            MySQLQuery query(std::string("DELETE FROM ") + table);
            REQUIRE(connection.execute(query));
        }
    }

    SessionSettings settingsFor(const Dictionary &dictionary)
    {
        SessionSettings settings;
        settings.set(dictionary);
        settings.set(plain, Dictionary());
        settings.set(qualified, Dictionary());
        settings.set(other, Dictionary());
        return settings;
    }

    // One "beginstring|sendercompid|targetcompid|session_qualifier|text" per row, oldest first.
    std::vector<std::string> rows(const std::string &table)
    {
        MySQLQuery query("SELECT CONCAT_WS('|', IFNULL(beginstring, 'NULL'), IFNULL(sendercompid, 'NULL'), "
                         "IFNULL(targetcompid, 'NULL'), IFNULL(session_qualifier, 'NULL'), text) FROM " +
                         table + " ORDER BY id");
        REQUIRE(connection.execute(query));
        std::vector<std::string> result;
        const int count = query.rows();
        for (int row = 0; row < count; ++row)
        {
            result.push_back(query.getValue(row, 0));
        }
        return result;
    }

    Dictionary defaults;
    std::string database;
    std::string user;
    std::string password;
    std::string host;
    short port;
    MySQLConnection connection;
    SessionID plain;
    SessionID qualified;
    SessionID other;
};

using Rows = std::vector<std::string>;
} // namespace

TEST_CASE_METHOD(mySQLLogFixture, "MySQLLogTests")
{
    SECTION("aSessionLogWritesItsSessionToItsTables")
    {
        MySQLLogFactory factory(settingsFor(defaults));
        Log *log = factory.create(plain);
        log->onIncoming("in");
        log->onOutgoing("out");
        log->onEvent("event");
        factory.destroy(log);

        CHECK(rows("messages_log") == Rows{"FIX.4.2|LOG|TEST|NULL|in", "FIX.4.2|LOG|TEST|NULL|out"});
        CHECK(rows("event_log") == Rows{"FIX.4.2|LOG|TEST|NULL|event"});
    }

    SECTION("aQualifiedSessionLogWritesItsQualifier")
    {
        MySQLLogFactory factory(settingsFor(defaults));
        Log *log = factory.create(qualified);
        log->onIncoming("in");
        log->onEvent("event");
        factory.destroy(log);

        CHECK(rows("messages_log") == Rows{"FIX.4.2|LOG|TEST|QUALIFIED|in"});
        CHECK(rows("event_log") == Rows{"FIX.4.2|LOG|TEST|QUALIFIED|event"});
    }

    SECTION("aGlobalLogWritesNoSession")
    {
        MySQLLogFactory factory(settingsFor(defaults));
        Log *log = factory.create();
        log->onIncoming("in");
        log->onEvent("event");
        factory.destroy(log);

        CHECK(rows("messages_log") == Rows{"NULL|NULL|NULL|NULL|in"});
        CHECK(rows("event_log") == Rows{"NULL|NULL|NULL|NULL|event"});
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

    SECTION("textComesBackAsWritten")
    {
        const std::string text = "8=FIX.4.2\x01"
                                 "58=' \" \\ \\' \\\" ; -- %_\x01"
                                 "10=000\x01";
        MySQLLogFactory factory(settingsFor(defaults));
        Log *log = factory.create(plain);
        log->onIncoming(text);
        factory.destroy(log);

        MySQLQuery query("SELECT text FROM messages_log");
        REQUIRE(connection.execute(query));
        REQUIRE(query.rows() == 1);
        CHECK(std::string(query.getValue(0, 0)) == text);
    }

    SECTION("aMessageOverSixtyFourKilobytesIsLogged")
    {
        const std::string text(70000, 'x');
        MySQLLogFactory factory(settingsFor(defaults));
        Log *log = factory.create(plain);
        log->onIncoming(text);
        factory.destroy(log);

        MySQLQuery query("SELECT LENGTH(text) FROM messages_log");
        REQUIRE(connection.execute(query));
        REQUIRE(query.rows() == 1);
        CHECK(std::string(query.getValue(0, 0)) == "70000");
    }

    SECTION("theTablesCanBeNamed")
    {
        Dictionary named = defaults;
        named.setString(MYSQL_LOG_INCOMING_TABLE, "messages_backup_log");
        named.setString(MYSQL_LOG_OUTGOING_TABLE, "messages_log");
        named.setString(MYSQL_LOG_EVENT_TABLE, "event_backup_log");
        MySQLLogFactory factory(settingsFor(named));
        Log *log = factory.create(plain);
        log->onIncoming("in");
        log->onOutgoing("out");
        log->onEvent("event");
        factory.destroy(log);

        CHECK(rows("messages_backup_log") == Rows{"FIX.4.2|LOG|TEST|NULL|in"});
        CHECK(rows("messages_log") == Rows{"FIX.4.2|LOG|TEST|NULL|out"});
        CHECK(rows("event_backup_log") == Rows{"FIX.4.2|LOG|TEST|NULL|event"});
        CHECK(rows("event_log").empty());
    }

    SECTION("clearRemovesOnlyItsOwnRows")
    {
        MySQLLogFactory factory(settingsFor(defaults));
        Log *plainLog = factory.create(plain);
        Log *qualifiedLog = factory.create(qualified);
        Log *otherLog = factory.create(other);
        Log *globalLog = factory.create();
        for (Log *log : {plainLog, qualifiedLog, otherLog, globalLog})
        {
            log->onIncoming("in");
            log->onEvent("event");
        }

        const Rows expectedAll{"FIX.4.2|LOG|TEST|NULL|in", "FIX.4.2|LOG|TEST|QUALIFIED|in",
                               "FIX.4.2|OTHER|TEST|NULL|in", "NULL|NULL|NULL|NULL|in"};
        REQUIRE(rows("messages_log") == expectedAll);

        SECTION("anUnqualifiedSession")
        {
            plainLog->clear();
            CHECK(rows("messages_log") ==
                  Rows{"FIX.4.2|LOG|TEST|QUALIFIED|in", "FIX.4.2|OTHER|TEST|NULL|in", "NULL|NULL|NULL|NULL|in"});
            CHECK(rows("event_log") == Rows{"FIX.4.2|LOG|TEST|QUALIFIED|event", "FIX.4.2|OTHER|TEST|NULL|event",
                                            "NULL|NULL|NULL|NULL|event"});
        }

        SECTION("aQualifiedSession")
        {
            qualifiedLog->clear();
            CHECK(rows("messages_log") ==
                  Rows{"FIX.4.2|LOG|TEST|NULL|in", "FIX.4.2|OTHER|TEST|NULL|in", "NULL|NULL|NULL|NULL|in"});
            CHECK(rows("event_log") ==
                  Rows{"FIX.4.2|LOG|TEST|NULL|event", "FIX.4.2|OTHER|TEST|NULL|event", "NULL|NULL|NULL|NULL|event"});
        }

        SECTION("theGlobalLog")
        {
            globalLog->clear();
            CHECK(rows("messages_log") ==
                  Rows{"FIX.4.2|LOG|TEST|NULL|in", "FIX.4.2|LOG|TEST|QUALIFIED|in", "FIX.4.2|OTHER|TEST|NULL|in"});
            CHECK(rows("event_log") == Rows{"FIX.4.2|LOG|TEST|NULL|event", "FIX.4.2|LOG|TEST|QUALIFIED|event",
                                            "FIX.4.2|OTHER|TEST|NULL|event"});
        }

        for (Log *log : {plainLog, qualifiedLog, otherLog, globalLog})
        {
            factory.destroy(log);
        }
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

        CHECK(rows("messages_log") == Rows{"FIX.4.2|LOG|TEST|NULL|session", "NULL|NULL|NULL|NULL|global"});
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

        CHECK(rows("messages_log") == Rows{"FIX.4.2|LOG|TEST|NULL|first", "FIX.4.2|OTHER|TEST|NULL|second"});
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
