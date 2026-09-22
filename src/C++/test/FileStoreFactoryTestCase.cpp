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

#include "TestHelper.h"
#include <FileStore.h>
#include <Session.h>
#include <fstream>
#include <typeinfo>

#include "catch_amalgamated.hpp"

using namespace FIX;

struct callCreateFixture
{
    callCreateFixture() : object("store") { deleteSession("FS", "FACT"); }

    ~callCreateFixture() { deleteSession("FS", "FACT"); }

    FileStoreFactory object;
};

TEST_CASE_METHOD(callCreateFixture, "FileStoreFactoryTests")
{
    SECTION("callCreate")
    {
        SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("FS"), TargetCompID("FACT"));

        MessageStore *messageStore = object.create(UtcTimeStamp::now(), sessionID);
        CHECK(typeid(FileStore) == typeid(*messageStore));
        object.destroy(messageStore);

        std::ifstream messageFile("store/FIX.4.2-FS-FACT.body");
        CHECK(!messageFile.fail());
        messageFile.close();

        std::ifstream seqnumsFile("store/FIX.4.2-FS-FACT.seqnums");
        CHECK(!seqnumsFile.fail());
        seqnumsFile.close();

        std::ifstream sessionFile("store/FIX.4.2-FS-FACT.session");
        CHECK(!sessionFile.fail());
        sessionFile.close();
    }
} // namespace FIX
