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

#include "MessageStoreTestCase.h"
#include "TestHelper.h"
#include <PostgreSQLStore.h>

#include "catch_amalgamated.hpp"

using namespace FIX;

struct postgreSQLStoreFixture
{
    postgreSQLStoreFixture(bool reset) : factory(TestSettings::sessionSettings.get())
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

    ~postgreSQLStoreFixture() { factory.destroy(object); }

    PostgreSQLStoreFactory factory;
    MessageStore *object;
    bool resetAfter;
};

struct noResetPostgreSQLStoreFixture : postgreSQLStoreFixture
{
    noResetPostgreSQLStoreFixture() : postgreSQLStoreFixture(false) {}
};

struct resetPostgreSQLStoreFixture : postgreSQLStoreFixture
{
    resetPostgreSQLStoreFixture() : postgreSQLStoreFixture(true) {}
};

TEST_CASE_METHOD(resetPostgreSQLStoreFixture,
                 "resetPostgreSQLStoreTests"){SECTION("setGet"){CHECK_MESSAGE_STORE_SET_GET}

                                              SECTION("setGetUint64"){CHECK_MESSAGE_STORE_SET_GET_UINT64}

                                              SECTION("setGetWithQuote"){CHECK_MESSAGE_STORE_SET_GET_WITH_QUOTE}

                                              SECTION("setTwiceWithQuote"){CHECK_MESSAGE_STORE_SET_TWICE_WITH_QUOTE}

                                              SECTION("other"){CHECK_MESSAGE_STORE_OTHER}

                                              SECTION("otherUint64"){CHECK_MESSAGE_STORE_OTHER_UINT64}

                                              SET_SEQUENCE_NUMBERS}

TEST_CASE_METHOD(noResetPostgreSQLStoreFixture, "noResetPostgreSQLStoreTests")
{
    SECTION("reload"){CHECK_MESSAGE_STORE_RELOAD}

    SECTION("refresh")
    {
        CHECK_MESSAGE_STORE_REFRESH
    }
}

TEST_CASE_METHOD(resetPostgreSQLStoreFixture, "PostgreSQLStoreMessageTests")
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

    SECTION("aMessageTheServerRefusesThrows")
    {
        // libpq takes a whole connection string as the database name. Reporting success turns a resend into a gap fill.
        Dictionary refusing = TestSettings::sessionSettings.get();
        refusing.setString(POSTGRESQL_STORE_DATABASE, "dbname=" + refusing.getString(POSTGRESQL_STORE_DATABASE) +
                                                          " options='-c default_transaction_read_only=on'");
        PostgreSQLStoreFactory refusingFactory(refusing);
        MessageStore *store = refusingFactory.create(
            UtcTimeStamp::now(), SessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST")));
        CHECK_THROWS_AS(store->set(1, "refused"), IOException);
        refusingFactory.destroy(store);

        object->get(1, 1, messages);
        CHECK(messages.empty());
    }

    SECTION("settingAgainReplacesAndAddsNoRow")
    {
        CHECK(object->set(1, "first"));
        CHECK(object->set(1, "second"));
        object->get(1, 1, messages);
        REQUIRE(1U == messages.size());
        CHECK("second" == messages[0]);
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
