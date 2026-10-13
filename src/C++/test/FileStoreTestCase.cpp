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

#include "MessageStoreTestCase.h"
#include "TestHelper.h"
#include <FileStore.h>
#include <vector>

#include "catch_amalgamated.hpp"

using namespace FIX;

struct fileStoreFixture
{
    fileStoreFixture(bool resetBefore, bool reset) : factory("store")
    {
        if (resetBefore)
        {
            deleteSession("SETGET", "TEST");
        }

        SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"));

        object = factory.create(UtcTimeStamp::now(), sessionID);

        this->resetAfter = reset;
    }

    ~fileStoreFixture()
    {
        factory.destroy(object);

        if (resetAfter)
        {
            deleteSession("SETGET", "TEST");
        }
    }

    FileStoreFactory factory;
    MessageStore *object;
    bool resetAfter;
};

struct resetBeforeFileStoreFixture : fileStoreFixture
{
    resetBeforeFileStoreFixture() : fileStoreFixture(true, false) {}
};

struct resetAfterFileStoreFixture : fileStoreFixture
{
    resetAfterFileStoreFixture() : fileStoreFixture(false, true) {}
};

struct resetBeforeAndAfterFileStoreFixture : fileStoreFixture
{
    resetBeforeAndAfterFileStoreFixture() : fileStoreFixture(true, true) {}
};

struct noResetFileStoreFixture : fileStoreFixture
{
    noResetFileStoreFixture() : fileStoreFixture(false, false) {}
};

struct resetBeforeAndAfterWithTestFileManager : resetBeforeAndAfterFileStoreFixture
{
    resetBeforeAndAfterWithTestFileManager() : resetBeforeAndAfterFileStoreFixture()
    {
        factory.destroy(object);

        SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"), "Test");

        object = new FileStore(UtcTimeStamp::now(), "store", sessionID);
    }
};

TEST_CASE_METHOD(resetBeforeFileStoreFixture,
                 "resetFileStoreTests"){SECTION("setGet"){CHECK_MESSAGE_STORE_SET_GET}

                                        SECTION("setGetUint64"){CHECK_MESSAGE_STORE_SET_GET_UINT64}

                                        SECTION("setGetWithQuote"){CHECK_MESSAGE_STORE_SET_GET_WITH_QUOTE}

                                        SECTION("other"){CHECK_MESSAGE_STORE_OTHER}

                                        SECTION("otherUint64"){CHECK_MESSAGE_STORE_OTHER_UINT64}

                                        SET_SEQUENCE_NUMBERS}

TEST_CASE_METHOD(noResetFileStoreFixture, "noResetFileStoreTests")
{
    SECTION("reload"){CHECK_MESSAGE_STORE_RELOAD}

    SECTION("refresh")
    {
        CHECK_MESSAGE_STORE_RELOAD
    }
}

TEST_CASE_METHOD(noResetFileStoreFixture, "FileStoreTests_3")
{
    SECTION("refresh") { CHECK_MESSAGE_STORE_REFRESH }
}

TEST_CASE_METHOD(resetAfterFileStoreFixture, "FileStoreTests_4")
{
    SECTION("reload") { CHECK_MESSAGE_STORE_RELOAD }
}

TEST_CASE_METHOD(resetBeforeAndAfterFileStoreFixture, "FileStoreTests_5")
{
    SECTION("FileStore_refresh_reset")
    {
        // Init store with 3 messages
        CHECK_MESSAGE_STORE_SET_GET
        object->get(1, 10, messages);

        // Still 3 messages after refresh
        object->refresh();
        object->get(1, 10, messages);

        // Should be 0 messages after reset
        object->reset(UtcTimeStamp::now());
        object->get(1, 10, messages);
    }
}

TEST_CASE_METHOD(resetBeforeAndAfterWithTestFileManager, "FileStoreTests_6")
{
    SECTION("Refresh_DeleteFileStartup_NoException")
    {
        try
        {
            object->refresh();
        }
        catch (Exception &e)
        {
            CHECK(false);
            throw e;
        }
    }

    SECTION("Reset_DeleteFileStartup_NoException")
    {
        try
        {
            object->reset(UtcTimeStamp::now());
        }
        catch (Exception &e)
        {
            CHECK(false);
            throw e;
        }
    }

    SECTION("FileStoreCreationTime")
    {
        UtcTimeStamp timeStamp = object->getCreationTime();
        UtcTimeStamp currentTimeStamp = UtcTimeStamp::now();
        CHECK(currentTimeStamp.getYear() == timeStamp.getYear());
    }

    SECTION("FileStoreFactory_FileStoreFromDictionary")
    {
        SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"));
        Dictionary dictionary;
        dictionary.setString("ConnectionType", "acceptor");
        dictionary.setString("FileStorePath", "store");

        SessionSettings settings;
        settings.set(sessionID, dictionary);
        FileStoreFactory fileStoreFactory(settings);

        MessageStore *fileStore = fileStoreFactory.create(UtcTimeStamp::now(), sessionID);
        CHECK(fileStore != nullptr);
        fileStoreFactory.destroy(fileStore);
    }
}
// A store opened over damaged files (#126). The session file holds the creation
// time; it was read without a bound into 22 bytes on the stack.
TEST_CASE("FileStoreCorruptionTests")
{
    SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("CORRUPT"), TargetCompID("TEST"));
    const std::string prefix = "store/FIX.4.2-CORRUPT-TEST.";
    deleteSession("CORRUPT", "TEST");
    delete new FileStore(UtcTimeStamp::now(), "store", sessionID); // creates the files

    auto overwrite = [](const std::string &name, const std::string &content)
    {
        FILE *file = fopen(name.c_str(), "w");
        REQUIRE(file != nullptr);
        fputs(content.c_str(), file);
        fclose(file);
    };

    SECTION("anOverlongCreationTimeIsAConfigErrorNotAnOverflow")
    {
        overwrite(prefix + "session", std::string(200, '9'));
        CHECK_THROWS_AS(FileStore(UtcTimeStamp::now(), "store", sessionID), ConfigError);
    }

    SECTION("anUnreadableCreationTimeIsAConfigErrorNamingTheFile")
    {
        overwrite(prefix + "session", "not-a-time");
        try
        {
            FileStore store(UtcTimeStamp::now(), "store", sessionID);
            FAIL("opened a store with an unreadable creation time");
        }
        catch (ConfigError &e)
        {
            CHECK(std::string(e.what()).find(prefix + "session") != std::string::npos);
        }
    }

    SECTION("aTornHeaderEntryKeepsTheMessagesBeforeIt")
    {
        {
            FileStore store(UtcTimeStamp::now(), "store", sessionID);
            store.set(1, "first");
            store.set(2, "second");
        }
        // A crash between writing a message and finishing its header entry.
        FILE *header = fopen((prefix + "header").c_str(), "a");
        REQUIRE(header != nullptr);
        fputs("3,11,", header);
        fclose(header);

        FileStore store(UtcTimeStamp::now(), "store", sessionID);
        std::vector<std::string> messages;
        store.get(1, 3, messages);
        REQUIRE(messages.size() == 2);
        CHECK(messages[0] == "first");
        CHECK(messages[1] == "second");
    }

    deleteSession("CORRUPT", "TEST");
}

// set() writes through the files' descriptors and get() reads through their streams (#151): writes
// after reads must still append, reads must see them, and a reopened store must find everything.
TEST_CASE("FileStoreInterleavedTests")
{
    SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"));
    {
        FileStore store(UtcTimeStamp::now(), "store", sessionID);
        store.reset(UtcTimeStamp::now());
        auto one = [&store](SEQNUM seqNum)
        {
            std::vector<std::string> messages;
            store.get(seqNum, seqNum, messages);
            return messages.size() == 1 ? messages.front() : std::string("<missing>");
        };
        CHECK(store.set(1, "first"));
        CHECK(store.set(2, "second message"));
        CHECK(one(1) == "first");
        CHECK(store.set(3, "third"));
        CHECK(one(3) == "third");
        CHECK(one(2) == "second message");
        store.setNextSenderMsgSeqNum(12345678901ULL);
        store.incrNextTargetMsgSeqNum();
        store.incrNextTargetMsgSeqNum();
    }
    FileStore reopened(UtcTimeStamp::now(), "store", sessionID);
    std::vector<std::string> messages;
    reopened.get(1, 3, messages);
    CHECK(messages == std::vector<std::string>{"first", "second message", "third"});
    CHECK(reopened.getNextSenderMsgSeqNum() == 12345678901ULL);
    CHECK(reopened.getNextTargetMsgSeqNum() == 3);
    reopened.reset(UtcTimeStamp::now());
}
