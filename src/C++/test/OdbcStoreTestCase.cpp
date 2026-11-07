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

#include "MessageStoreTestCase.h"
#include "TestHelper.h"
#include <OdbcStore.h>

#include "catch_amalgamated.hpp"

using namespace FIX;

struct odbcStoreFixture
{
    odbcStoreFixture(bool reset) : factory(TestSettings::sessionSettings.get())
    {
        SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"));

        try
        {
            object = factory.create(UtcTimeStamp::now(), sessionID);
        }
        catch (std::exception &e)
        {
            std::cerr << e.what() << std::endl;
            throw;
        }

        if (reset)
        {
            object->reset(UtcTimeStamp::now());
        }

        this->resetAfter = reset;
    }

    ~odbcStoreFixture() { factory.destroy(object); }

    OdbcStoreFactory factory;
    MessageStore *object;
    bool resetAfter;
};

struct noResetOdbcStoreFixture : odbcStoreFixture
{
    noResetOdbcStoreFixture() : odbcStoreFixture(false) {}
};

struct resetOdbcStoreFixture : odbcStoreFixture
{
    resetOdbcStoreFixture() : odbcStoreFixture(true) {}
};

TEST_CASE_METHOD(resetOdbcStoreFixture,
                 "resetOdbcStoreTests"){SECTION("setGet"){CHECK_MESSAGE_STORE_SET_GET}

                                        SECTION("setGetWithQuote"){CHECK_MESSAGE_STORE_SET_GET_WITH_QUOTE}

                                        SECTION("setTwiceWithQuote"){CHECK_MESSAGE_STORE_SET_TWICE_WITH_QUOTE}

                                        SECTION("setGetUint64"){CHECK_MESSAGE_STORE_SET_GET_UINT64}

                                        SECTION("other"){CHECK_MESSAGE_STORE_OTHER}

                                        SECTION("otherUint64"){CHECK_MESSAGE_STORE_OTHER_UINT64}

                                        SET_SEQUENCE_NUMBERS}

TEST_CASE_METHOD(resetOdbcStoreFixture, "OdbcStoreMessageTests")
{
    std::vector<std::string> messages;

    // A store on a text column, which a PostgreSQL database not altered yet has, and SQL Server's schema. A temporary
    // table hides messages from this one connection. ConnSettings is psqlODBC's, which is the driver ut.cfg names.
    Dictionary textColumn = TestSettings::sessionSettings.get();
    textColumn.setString(ODBC_STORE_CONNECTION_STRING,
                         textColumn.getString(ODBC_STORE_CONNECTION_STRING) +
                             "ConnSettings={CREATE TEMP TABLE messages (LIKE public.messages INCLUDING ALL);"
                             "ALTER TABLE pg_temp.messages ALTER COLUMN message TYPE TEXT};");
    const SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"));

    SECTION("aMessageOfAnyLengthComesBackWhole")
    {
        // Either side of each multiple of the 4096 bytes get() reads at a time, all of which a binary column's read
        // holds.
        const auto length = GENERATE(as<size_t>{}, 1, 4095, 4096, 4097, 8191, 8192, 8193, 70000);
        std::string message(length, 'x');
        message.back() = 'y';
        CHECK(object->set(1, message));
        object->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK(message.size() == messages[0].size());
        CHECK(message == messages[0]);
    }

    SECTION("aMessageOfAnyLengthComesBackWholeFromATextColumn")
    {
        // A text column's read holds 4095 of those bytes and a terminator.
        const auto length = GENERATE(as<size_t>{}, 1, 4094, 4095, 4096, 4097, 8189, 8190, 8191, 70000);
        std::string message(length, 'x');
        message.back() = 'y';
        OdbcStoreFactory textFactory(textColumn);
        MessageStore *store = textFactory.create(UtcTimeStamp::now(), sessionID);
        CHECK(store->set(1, message));
        store->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK(message.size() == messages[0].size());
        CHECK(message == messages[0]);
        textFactory.destroy(store);
    }

    SECTION("aMessageHoldingANulComesBackWhole")
    {
        std::string message = "8=FIX.4.2\x01"
                              "95=3\x01"
                              "96=a";
        message.push_back('\0');
        message += "b\x01"
                   "10=000\x01";
        CHECK(object->set(1, message));
        object->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK(message == messages[0]);
    }

    SECTION("aMessageHoldingAByteInvalidInUtf8ComesBackWhole")
    {
        const std::string message = "8=FIX.4.2\x01"
                                    "58=caf\xFC\x01"
                                    "10=000\x01";
        CHECK(object->set(1, message));
        object->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK(message == messages[0]);
    }

    SECTION("aMessageHoldingEveryByteComesBackWhole")
    {
        // Across the reads' edges too.
        const auto length = GENERATE(as<size_t>{}, 1, 256, 4096, 4097, 8192, 8193, 70000);
        std::string message(length, '\0');
        for (size_t i = 0; i < length; ++i)
        {
            message[i] = static_cast<char>(i);
        }
        CHECK(object->set(1, message));
        object->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK(message.size() == messages[0].size());
        CHECK(message == messages[0]);
    }

    SECTION("aMessageHoldingANulIsNotStoredCut")
    {
        std::string message = "8=FIX.4.2\x01"
                              "95=3\x01"
                              "96=a";
        message.push_back('\0');
        message += "b\x01"
                   "10=000\x01";
        OdbcStoreFactory textFactory(textColumn);
        MessageStore *store = textFactory.create(UtcTimeStamp::now(), sessionID);
        CHECK_THROWS_AS(store->set(1, message), IOException);
        store->get(1, 1, messages);
        CHECK(messages.empty());
        textFactory.destroy(store);
    }

    SECTION("aMessageATextColumnCannotHoldThrows")
    {
        OdbcStoreFactory textFactory(textColumn);
        MessageStore *store = textFactory.create(UtcTimeStamp::now(), sessionID);
        CHECK_THROWS_AS(store->set(1, "58=caf\xFC\x01"), IOException);
        textFactory.destroy(store);
    }

    SECTION("aMessagesTableThatCannotBeReadIsAConfigError")
    {
        // A store guessing the column's type could store what it could not read back. A search path of nothing leaves
        // only the temporary table.
        Dictionary noMessages = TestSettings::sessionSettings.get();
        noMessages.setString(ODBC_STORE_CONNECTION_STRING,
                             noMessages.getString(ODBC_STORE_CONNECTION_STRING) +
                                 "ConnSettings={CREATE TEMP TABLE sessions (LIKE public.sessions INCLUDING ALL);"
                                 "SET search_path=nowhere};");
        OdbcStoreFactory noMessagesFactory(noMessages);
        CHECK_THROWS_AS(noMessagesFactory.create(UtcTimeStamp::now(), sessionID), ConfigError);
    }

    SECTION("aMessageTheServerRefusesThrows")
    {
        // A temporary table hides messages from this one connection and refuses one sequence number, so the INSERT
        // fails and an UPDATE finds no row. ConnSettings is psqlODBC's, which is the driver ut.cfg names.
        Dictionary refusing = TestSettings::sessionSettings.get();
        refusing.setString(ODBC_STORE_CONNECTION_STRING,
                           refusing.getString(ODBC_STORE_CONNECTION_STRING) +
                               "ConnSettings={CREATE TEMP TABLE messages "
                               "(LIKE public.messages INCLUDING ALL, CHECK (msgseqnum <> 99))};");
        OdbcStoreFactory refusingFactory(refusing);
        MessageStore *store = refusingFactory.create(
            UtcTimeStamp::now(), SessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST")));
        CHECK_THROWS_AS(store->set(99, "refused"), IOException);
        CHECK(store->set(1, "stored"));
        store->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK("stored" == messages[0]);
        refusingFactory.destroy(store);
    }

    SECTION("settingAgainReplacesAndAddsNoRow")
    {
        CHECK(object->set(1, "first"));
        CHECK(object->set(1, "second"));
        object->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK("second" == messages[0]);
    }

    SECTION("aSessionIDCanHoldAnApostrophe")
    {
        const SessionID apostrophe(BeginString("FIX.4.2"), SenderCompID("O'NEIL"), TargetCompID("T'EST"), "Q'UAL");
        MessageStore *store = factory.create(UtcTimeStamp::now(), apostrophe);
        store->reset(UtcTimeStamp::now());
        CHECK(store->set(1, "first"));
        CHECK(store->set(1, "stored"));
        store->setNextSenderMsgSeqNum(5);
        store->setNextTargetMsgSeqNum(7);
        store->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK("stored" == messages[0]);
        factory.destroy(store);

        store = factory.create(UtcTimeStamp::now(), apostrophe);
        CHECK(5 == store->getNextSenderMsgSeqNum());
        CHECK(7 == store->getNextTargetMsgSeqNum());
        store->reset(UtcTimeStamp::now());
        factory.destroy(store);
    }

    SECTION("aQualifiedSessionKeepsItsOwnMessages")
    {
        SessionID qualifiedID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"), "QUALIFIED");
        MessageStore *qualified = factory.create(UtcTimeStamp::now(), qualifiedID);
        qualified->reset(UtcTimeStamp::now());
        CHECK(object->set(1, "plain"));
        CHECK(qualified->set(1, "qualified"));
        qualified->setNextSenderMsgSeqNum(5);

        object->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK("plain" == messages[0]);
        qualified->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK("qualified" == messages[0]);
        CHECK(1 == object->getNextSenderMsgSeqNum());

        factory.destroy(qualified);
    }

    SET_SEQUENCE_NUMBERS
}

TEST_CASE_METHOD(noResetOdbcStoreFixture, "noResetOdbcStoreTests")
{
    SECTION("reload"){CHECK_MESSAGE_STORE_RELOAD}

    SECTION("refresh")
    {
        CHECK_MESSAGE_STORE_REFRESH
    }
}

#endif