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

#include "TestHelper.h"
#include <Log.h>
#include <SessionID.h>
#include <SessionSettings.h>

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

namespace FIX
{
/// The sessions and settings every database log test starts from. A fixture derives from this and adds execute(sql),
/// which requires success, and select(sql), the first column of each row, and empties the tables once it can.
struct DatabaseLogFixture
{
    DatabaseLogFixture()
        : defaults(TestSettings::sessionSettings.get()),
          plain(BeginString("FIX.4.2"), SenderCompID("LOG"), TargetCompID("TEST")),
          qualified(BeginString("FIX.4.2"), SenderCompID("LOG"), TargetCompID("TEST"), "QUALIFIED"),
          other(BeginString("FIX.4.2"), SenderCompID("OTHER"), TargetCompID("TEST"))
    {
        defaults.setString(CONNECTION_TYPE, "acceptor");
    }

    SessionSettings settingsFor(const Dictionary &dictionary) const
    {
        SessionSettings settings;
        settings.set(dictionary);
        settings.set(plain, Dictionary());
        settings.set(qualified, Dictionary());
        settings.set(other, Dictionary());
        return settings;
    }

    Dictionary defaults;
    SessionID plain;
    SessionID qualified;
    SessionID other;
};

/// Every schema has these three, and they have one shape.
template <typename Fixture> void emptyDatabaseLogTables(Fixture &fixture)
{
    for (const char *table : {"messages_log", "event_log", "event_backup_log"})
    {
        fixture.execute(std::string("DELETE FROM ") + table);
    }
}

/// One "beginstring|sendercompid|targetcompid|session_qualifier|text" per row, oldest first.
template <typename Fixture> std::vector<std::string> databaseLogRows(Fixture &fixture, const std::string &table)
{
    return fixture.select("SELECT CONCAT_WS('|', COALESCE(RTRIM(beginstring), 'NULL'), "
                          "COALESCE(sendercompid, 'NULL'), COALESCE(targetcompid, 'NULL'), "
                          "COALESCE(session_qualifier, 'NULL'), text) FROM " +
                          table + " ORDER BY id");
}

/// What every database log must do, however it reaches its server.
template <typename Factory, typename Fixture>
void checkDatabaseLog(Fixture &fixture, const char *incomingTableKey, const char *outgoingTableKey,
                      const char *eventTableKey)
{
    using Rows = std::vector<std::string>;
    auto rows = [&fixture](const std::string &table) { return databaseLogRows(fixture, table); };

    SECTION("aSessionLogWritesItsSessionToItsTables")
    {
        Factory factory(fixture.settingsFor(fixture.defaults));
        Log *log = factory.create(fixture.plain);
        log->onIncoming("in");
        log->onOutgoing("out");
        log->onEvent("event");
        factory.destroy(log);

        CHECK(rows("messages_log") == Rows{"FIX.4.2|LOG|TEST|NULL|in", "FIX.4.2|LOG|TEST|NULL|out"});
        CHECK(rows("event_log") == Rows{"FIX.4.2|LOG|TEST|NULL|event"});
    }

    SECTION("aQualifiedSessionLogWritesItsQualifier")
    {
        Factory factory(fixture.settingsFor(fixture.defaults));
        Log *log = factory.create(fixture.qualified);
        log->onIncoming("in");
        log->onEvent("event");
        factory.destroy(log);

        CHECK(rows("messages_log") == Rows{"FIX.4.2|LOG|TEST|QUALIFIED|in"});
        CHECK(rows("event_log") == Rows{"FIX.4.2|LOG|TEST|QUALIFIED|event"});
    }

    SECTION("aGlobalLogWritesNoSession")
    {
        Factory factory(fixture.settingsFor(fixture.defaults));
        Log *log = factory.create();
        log->onIncoming("in");
        log->onEvent("event");
        factory.destroy(log);

        CHECK(rows("messages_log") == Rows{"NULL|NULL|NULL|NULL|in"});
        CHECK(rows("event_log") == Rows{"NULL|NULL|NULL|NULL|event"});
    }

    SECTION("textComesBackAsWritten")
    {
        const std::string text = "8=FIX.4.2\x01"
                                 "58=' \" \\ \\' \\\" ; -- %_ {fn now()} ?\x01"
                                 "10=000\x01";
        Factory factory(fixture.settingsFor(fixture.defaults));
        Log *log = factory.create(fixture.plain);
        log->onIncoming(text);
        factory.destroy(log);

        CHECK(fixture.select("SELECT text FROM messages_log") == Rows{text});
    }

    SECTION("aMessageOverSixtyFourKilobytesIsLogged")
    {
        const std::string text(70000, 'x');
        Factory factory(fixture.settingsFor(fixture.defaults));
        Log *log = factory.create(fixture.plain);
        log->onIncoming(text);
        factory.destroy(log);

        CHECK(fixture.select("SELECT LENGTH(text) FROM messages_log") == Rows{"70000"});
    }

    SECTION("theTablesCanBeNamed")
    {
        Dictionary named = fixture.defaults;
        named.setString(incomingTableKey, "event_log");
        named.setString(outgoingTableKey, "event_backup_log");
        named.setString(eventTableKey, "messages_log");
        Factory factory(fixture.settingsFor(named));
        Log *log = factory.create(fixture.plain);
        log->onIncoming("in");
        log->onOutgoing("out");
        log->onEvent("event");
        factory.destroy(log);

        CHECK(rows("event_log") == Rows{"FIX.4.2|LOG|TEST|NULL|in"});
        CHECK(rows("event_backup_log") == Rows{"FIX.4.2|LOG|TEST|NULL|out"});
        CHECK(rows("messages_log") == Rows{"FIX.4.2|LOG|TEST|NULL|event"});
    }

    SECTION("clearRemovesOnlyItsOwnRows")
    {
        Factory factory(fixture.settingsFor(fixture.defaults));
        Log *plainLog = factory.create(fixture.plain);
        Log *qualifiedLog = factory.create(fixture.qualified);
        Log *otherLog = factory.create(fixture.other);
        Log *globalLog = factory.create();
        for (Log *log : {plainLog, qualifiedLog, otherLog, globalLog})
        {
            log->onIncoming("in");
            log->onEvent("event");
        }

        const Rows all{"FIX.4.2|LOG|TEST|NULL|in", "FIX.4.2|LOG|TEST|QUALIFIED|in", "FIX.4.2|OTHER|TEST|NULL|in",
                       "NULL|NULL|NULL|NULL|in"};
        REQUIRE(rows("messages_log") == all);

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
}
} // namespace FIX
