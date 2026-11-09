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

#include <cstddef>
#include <random>
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
          other(BeginString("FIX.4.2"), SenderCompID("OTHER"), TargetCompID("TEST")),
          apostrophe(BeginString("FIX.4.2"), SenderCompID("O'NEIL"), TargetCompID("T'EST"), "Q'UAL")
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
        settings.set(apostrophe, Dictionary());
        return settings;
    }

    Dictionary defaults;
    SessionID plain;
    SessionID qualified;
    SessionID other;
    SessionID apostrophe;
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

/// The message a database log's text was written for: each \x and two hex digits read back as that byte.
inline std::string readDatabaseLogText(const std::string &text)
{
    const auto digit = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    };
    std::string message;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\\' && text.size() - i > 3 && text[i + 1] == 'x' && digit(text[i + 2]) >= 0 &&
            digit(text[i + 3]) >= 0)
        {
            message += static_cast<char>(digit(text[i + 2]) * 16 + digit(text[i + 3]));
            i += 3;
        }
        else
        {
            message += text[i];
        }
    }
    return message;
}

/// The first and last code point of each row of the Unicode standard's table of well-formed UTF-8.
inline std::vector<std::string> wellFormedUtf8Bounds()
{
    return {"\xC2\x80",         "\xDF\xBF",         "\xE0\xA0\x80",     "\xE0\xBF\xBF",
            "\xE1\x80\x80",     "\xEC\xBF\xBF",     "\xED\x80\x80",     "\xED\x9F\xBF",
            "\xEE\x80\x80",     "\xEF\xBF\xBF",     "\xF0\x90\x80\x80", "\xF0\xBF\xBF\xBF",
            "\xF1\x80\x80\x80", "\xF3\xBF\xBF\xBF", "\xF4\x80\x80\x80", "\xF4\x8F\xBF\xBF"};
}

/// Bytes over 0x7F that are not well-formed UTF-8: just outside those bounds, a lead byte no sequence has, and cut
/// short.
inline std::vector<std::string> illFormedUtf8()
{
    return {"\x80",
            "\xBF",
            "\xC0\x80",
            "\xC1\xBF",
            "\xE0\x9F\xBF",
            "\xED\xA0\x80",
            "\xED\xBF\xBF",
            "\xF0\x8F\xBF\xBF",
            "\xF4\x90\x80\x80",
            "\xF5\x80\x80\x80",
            "\xFE",
            "\xFF",
            "\xC3",
            "\xE2\x82",
            "\xF0\x9D\x84"};
}

/// Messages a database log must keep: every byte in one, a NUL inside a message, a backslash before what reads as hex,
/// each sequence above, and a seeded sample of short random ones.
inline std::vector<std::string> databaseLogTextSamples()
{
    std::string everyByte;
    for (int byte = 0; byte < 256; ++byte)
    {
        everyByte += static_cast<char>(byte);
    }
    std::string nul = "8=FIX.4.2\x01"
                      "58=a";
    nul += '\0';
    nul += "b\x01"
           "10=000\x01";

    std::vector<std::string> samples{everyByte, nul, "58=\\x41 \\xfc\x01", "\\\xFC"};
    for (const std::string &sequence : wellFormedUtf8Bounds())
    {
        samples.push_back("a" + sequence + "b");
    }
    for (const std::string &sequence : illFormedUtf8())
    {
        samples.push_back("a" + sequence + "b");
    }

    std::mt19937 random(20261109);
    std::uniform_int_distribution<int> length(1, 48);
    std::uniform_int_distribution<int> byte(0, 255);
    for (int n = 0; n < 200; ++n)
    {
        std::string sample(length(random), '\0');
        for (char &c : sample)
        {
            c = static_cast<char>(byte(random));
        }
        samples.push_back(sample);
    }
    return samples;
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

    SECTION("aSessionIDCanHoldAnApostrophe")
    {
        Factory factory(fixture.settingsFor(fixture.defaults));
        Log *log = factory.create(fixture.apostrophe);
        Log *plainLog = factory.create(fixture.plain);
        log->onIncoming("in");
        log->onEvent("event");
        plainLog->onIncoming("plain");

        CHECK(rows("messages_log") == Rows{"FIX.4.2|O'NEIL|T'EST|Q'UAL|in", "FIX.4.2|LOG|TEST|NULL|plain"});
        CHECK(rows("event_log") == Rows{"FIX.4.2|O'NEIL|T'EST|Q'UAL|event"});

        log->clear();
        CHECK(rows("messages_log") == Rows{"FIX.4.2|LOG|TEST|NULL|plain"});
        CHECK(rows("event_log").empty());

        factory.destroy(log);
        factory.destroy(plainLog);
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

/// What a database log that writes its text through databaseLogText must do. MySQLLog keeps bytes as they are instead.
template <typename Factory, typename Fixture> void checkDatabaseLogText(Fixture &fixture)
{
    using Rows = std::vector<std::string>;

    SECTION("everyRowReadsBackAsItsMessage")
    {
        const Rows messages = databaseLogTextSamples();
        Factory factory(fixture.settingsFor(fixture.defaults));
        Log *log = factory.create(fixture.plain);
        for (const std::string &message : messages)
        {
            log->onIncoming(message);
        }
        factory.destroy(log);

        const Rows texts = fixture.select("SELECT text FROM messages_log ORDER BY id");
        REQUIRE(texts.size() == messages.size());
        std::vector<std::size_t> differing;
        for (std::size_t i = 0; i < texts.size(); ++i)
        {
            if (readDatabaseLogText(texts[i]) != messages[i])
            {
                differing.push_back(i);
            }
        }
        CHECK(differing == std::vector<std::size_t>{});
    }

    SECTION("aBackslashThatWouldReadAsHexIsWrittenInHex")
    {
        Factory factory(fixture.settingsFor(fixture.defaults));
        Log *log = factory.create(fixture.plain);
        log->onIncoming("58=\\x41 \\xG1 \\x4\x01");
        factory.destroy(log);

        CHECK(fixture.select("SELECT text FROM messages_log") == Rows{"58=\\x5Cx41 \\xG1 \\x4\x01"});
    }
}
} // namespace FIX
