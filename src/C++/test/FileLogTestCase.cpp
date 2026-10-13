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

#include <FileLog.h>
#include <Utility.h>
#include <fstream>
#include <iterator>
#include <sstream>

#include "catch_amalgamated.hpp"

using namespace FIX;

namespace
{
inline void deleteLogSession(std::string sender, std::string target)
{
    file_unlink(("log/FIX.4.2-" + sender + "-" + target + ".event.current.log").c_str());
    file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".event.backup.1.log").c_str());
    file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".event.backup.2.log").c_str());
    file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".event.backup.3.log").c_str());
    file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".event.backup.4.log").c_str());
    file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".event.backup.5.log").c_str());

    file_unlink(("log/FIX.4.2-" + sender + "-" + target + ".messages.current.log").c_str());
    file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".messages.backup.1.log").c_str());
    file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".messages.backup.2.log").c_str());
    file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".messages.backup.3.log").c_str());
    file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".messages.backup.4.log").c_str());

    file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".messages.backup.5.log").c_str());
}
} // namespace

struct generateFileNameFixture
{
    generateFileNameFixture() : fileLogFactory("log", "log" + file_separator() + "backup")
    {
        deleteLogSession("GENERATEFILENAME", "TEST");
        SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("GENERATEFILENAME"), TargetCompID("TEST"));

        object = (FileLog *)fileLogFactory.create(sessionID);
    }

    ~generateFileNameFixture()
    {
        fileLogFactory.destroy(object);
        deleteLogSession("GENERATEFILENAME", "TEST");
    }

    FileLogFactory fileLogFactory;
    FileLog *object;
};

TEST_CASE_METHOD(generateFileNameFixture, "FileLogTests")
{
    SECTION("generateFileName")
    {
        object->onEvent("EVENT1");
        object->onIncoming("INCOMING1");
        object->onOutgoing("OUTGOING1");

        CHECK(file_exists("log/FIX.4.2-GENERATEFILENAME-TEST.event.current.log"));
        CHECK(file_exists("log/FIX.4.2-GENERATEFILENAME-TEST.messages.current.log"));

        object->backup();
        object->onEvent("EVENT2");
        object->onIncoming("INCOMING2");
        object->onOutgoing("OUTGOING2");

        CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.event.backup.1.log"));
        CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.messages.backup.1.log"));

        object->backup();
        object->onEvent("EVENT3");
        object->onIncoming("INCOMING3");
        object->onOutgoing("OUTGOING3");

        CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.event.backup.2.log"));
        CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.messages.backup.2.log"));

        object->backup();
        object->onEvent("EVENT4");
        object->onIncoming("INCOMING4");
        object->onOutgoing("OUTGOING4");

        CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.event.backup.3.log"));
        CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.messages.backup.3.log"));

        object->backup();
        object->onEvent("EVENT5");
        object->onIncoming("INCOMING5");
        object->onOutgoing("OUTGOING5");

        CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.event.backup.4.log"));
        CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.messages.backup.4.log"));
    }
}

// FileLogFlush=N makes FileLogFactory create a log that buffers its message lines (#153); they are all
// in the file once the log is destroyed. Without the setting, each line is in the file at once.
TEST_CASE("FileLogFlushTests")
{
    SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("FLUSH"), TargetCompID("TEST"));
    const std::string fileName = "log/FIX.4.2-FLUSH-TEST.messages.current.log";
    auto factoryFor = [](const std::string &flush)
    {
        std::stringstream config("[DEFAULT]\nConnectionType=acceptor\nFileLogPath=log\n" + flush +
                                 "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=FLUSH\nTargetCompID=TEST\n");
        return FileLogFactory(SessionSettings(config));
    };
    auto contents = [&fileName]
    {
        std::ifstream file(fileName);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    };

    FileLogFactory flushing = factoryFor("");
    Log *log = flushing.create(sessionID);
    CHECK(dynamic_cast<BufferedFileLog *>(log) == nullptr);
    log->clear();
    log->onOutgoing("flushed");
    CHECK(contents().find(" : flushed\n") != std::string::npos);
    flushing.destroy(log);

    FileLogFactory buffering = factoryFor("FileLogFlush=N\n");
    log = buffering.create(sessionID);
    REQUIRE(dynamic_cast<BufferedFileLog *>(log) != nullptr);
    log->clear();
    log->onIncoming("in");
    log->onOutgoing("out");
    buffering.destroy(log);
    const std::string written = contents();
    CHECK(written.find(" : in\n") != std::string::npos);
    CHECK(written.find(" : out\n") != std::string::npos);
}
