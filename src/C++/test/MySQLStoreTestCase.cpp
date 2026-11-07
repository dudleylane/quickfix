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

#include "MessageStoreTestCase.h"
#include "TestHelper.h"
#include <MySQLStore.h>

#include "catch_amalgamated.hpp"

using namespace FIX;

struct mySQLStoreFixture
{
    mySQLStoreFixture(bool reset) : factory(TestSettings::sessionSettings.get())
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

    ~mySQLStoreFixture() { factory.destroy(object); }

    MySQLStoreFactory factory;
    MessageStore *object;
    bool resetAfter;
};

struct noResetMySQLStoreFixture : mySQLStoreFixture
{
    noResetMySQLStoreFixture() : mySQLStoreFixture(false) {}
};

struct resetMySQLStoreFixture : mySQLStoreFixture
{
    resetMySQLStoreFixture() : mySQLStoreFixture(true) {}
};

TEST_CASE_METHOD(resetMySQLStoreFixture,
                 "resetMySQLStoreTests"){SECTION("setGet"){CHECK_MESSAGE_STORE_SET_GET}

                                         SECTION("setGetWithQuote"){CHECK_MESSAGE_STORE_SET_GET_WITH_QUOTE}

                                         SECTION("setTwiceWithQuote"){CHECK_MESSAGE_STORE_SET_TWICE_WITH_QUOTE}

                                         SECTION("setGetUint64"){CHECK_MESSAGE_STORE_SET_GET_UINT64}

                                         SECTION("other"){CHECK_MESSAGE_STORE_OTHER}

                                         SECTION("otherUint64"){CHECK_MESSAGE_STORE_OTHER_UINT64}

                                         SET_SEQUENCE_NUMBERS}

TEST_CASE_METHOD(noResetMySQLStoreFixture, "noResetMySQLStoreTests")
{
    SECTION("reload"){CHECK_MESSAGE_STORE_RELOAD}

    SECTION("refresh")
    {
        CHECK_MESSAGE_STORE_REFRESH
    }
}

TEST_CASE_METHOD(resetMySQLStoreFixture, "MySQLStoreMessageTests")
{
    std::vector<std::string> messages;

    SECTION("aMessageOverSixtyFourKilobytesComesBackWhole")
    {
        const std::string message(70000, 'x');
        CHECK(object->set(1, message));
        object->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK(message == messages[0]);
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

    SECTION("aMessageTheServerRefusesThrows")
    {
        // Past what the column can hold. Reporting success here turns a later resend into a gap fill.
        CHECK_THROWS_AS(object->set(1, std::string(17 * 1024 * 1024, 'x')), IOException);
        object->get(1, 1, messages);
        CHECK(messages.empty());
    }

    SECTION("aSettingThatCannotBeAPortIsAConfigError")
    {
        Dictionary refused = TestSettings::sessionSettings.get();
        refused.setString(MYSQL_STORE_PORT, GENERATE("-1", "65536", "not a number"));
        MySQLStoreFactory refusedFactory(refused);
        CHECK_THROWS_AS(
            refusedFactory.create(UtcTimeStamp::now(),
                                  SessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"))),
            ConfigError);
    }

    SECTION("aSessionTheDatabaseCannotHoldIsAConfigError")
    {
        // A CompID longer than its column, so the session's row cannot be made. A sanitizer build is what sees a
        // connection this leaves behind: no destructor runs after a constructor throws.
        CHECK_THROWS_AS(
            factory.create(UtcTimeStamp::now(),
                           SessionID(BeginString("FIX.4.2"), SenderCompID(std::string(65, 'x')), TargetCompID("TEST"))),
            ConfigError);
    }

    SECTION("aStoreThatFailsToStartLeavesAPooledConnectionToTheStoresUsingIt")
    {
        const Dictionary settings = TestSettings::sessionSettings.get();
        const DatabaseConnectionID id(settings.getString(MYSQL_STORE_DATABASE), settings.getString(MYSQL_STORE_USER),
                                      settings.getString(MYSQL_STORE_PASSWORD), settings.getString(MYSQL_STORE_HOST),
                                      settings.has(MYSQL_STORE_PORT) ? settings.getInt(MYSQL_STORE_PORT) : 0);
        MySQLConnectionPool pool(true);
        MySQLStore store(UtcTimeStamp::now(),
                         SessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST")), id, &pool);
        CHECK_THROWS_AS(
            MySQLStore(UtcTimeStamp::now(),
                       SessionID(BeginString("FIX.4.2"), SenderCompID(std::string(65, 'x')), TargetCompID("TEST")), id,
                       &pool),
            ConfigError);
        CHECK(store.set(1, "still connected"));
        store.get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK("still connected" == messages[0]);
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

#endif