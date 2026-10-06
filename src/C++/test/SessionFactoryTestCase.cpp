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

#include <Application.h>
#include <MessageStore.h>
#include <Session.h>
#include <SessionFactory.h>

#include "TestHelper.h"

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("SessionFactoryTests")
{
    SECTION("validConfiguration")
    {
        NullApplication application;
        MemoryStoreFactory messageStoreFactory;
        SessionFactory object(application, messageStoreFactory, 0);

        SessionID sessionID("FIX.4.2", "SENDER", "TARGET");
        Dictionary settings;
        settings.setString(CONNECTION_TYPE, "initiator");
        settings.setString(USE_DATA_DICTIONARY, "N");
        settings.setString(START_TIME, "12:00:00");
        settings.setString(END_TIME, "12:00:00");
        settings.setString(HEARTBTINT, "30");
        object.destroy(object.create(sessionID, settings));
    }

    SECTION("startDayAndEndDayAreDifferent")
    {
        NullApplication application;
        MemoryStoreFactory messageStoreFactory;
        SessionFactory object(application, messageStoreFactory, 0);

        SessionID sessionID("FIX.4.2", "SENDER", "TARGET");
        Dictionary settings;
        settings.setString(CONNECTION_TYPE, "initiator");
        settings.setString(USE_DATA_DICTIONARY, "N");
        settings.setString(START_TIME, "12:00:00");
        settings.setString(END_TIME, "12:00:00");
        settings.setString(START_DAY, "Sun");
        settings.setString(END_DAY, "Mon");
        settings.setString(HEARTBTINT, "30");
        object.destroy(object.create(sessionID, settings));
    }

    SECTION("nonStopSession")
    {
        NullApplication application;
        MemoryStoreFactory messageStoreFactory;
        SessionFactory object(application, messageStoreFactory, 0);

        SessionID sessionID("FIX.4.2", "SENDER", "TARGET");
        Dictionary settings;
        settings.setString(CONNECTION_TYPE, "initiator");
        settings.setString(USE_DATA_DICTIONARY, "N");
        settings.setString(NON_STOP_SESSION, "Y");
        settings.setString(HEARTBTINT, "30");

        Session *session = nullptr;
        CHECK_NOTHROW(session = object.create(sessionID, settings));
        CHECK(session->getIsNonStopSession());
        object.destroy(session);
    }

    SECTION("wrongNonStopAndTime")
    {
        NullApplication application;
        MemoryStoreFactory messageStoreFactory;
        SessionFactory object(application, messageStoreFactory, 0);

        SessionID sessionID("FIX.4.2", "SENDER", "TARGET");
        Dictionary settings;
        settings.setString(CONNECTION_TYPE, "initiator");
        settings.setString(USE_DATA_DICTIONARY, "N");
        settings.setString(NON_STOP_SESSION, "Y");
        settings.setString(START_TIME, "12:00:00");
        settings.setString(END_TIME, "12:00:00");
        settings.setString(HEARTBTINT, "30");

        CHECK_THROWS(object.create(sessionID, settings));
    }
}

TEST_CASE("SessionFactoryAllowedRemoteAddressesTests")
{
    SECTION("aListWrittenWithSpacesAllowsEveryAddress")
    {
        // "a, b" used to store " b", which no peer address matches, so the second address was
        // silently denied (#59).
        NullApplication application;
        MemoryStoreFactory messageStoreFactory;
        SessionFactory object(application, messageStoreFactory, 0);

        SessionID sessionID("FIX.4.2", "ALLOWLIST", "TARGET");
        Dictionary settings;
        settings.setString(CONNECTION_TYPE, "acceptor");
        settings.setString(USE_DATA_DICTIONARY, "N");
        settings.setString(START_TIME, "12:00:00");
        settings.setString(END_TIME, "12:00:00");
        settings.setString(ALLOWED_REMOTE_ADDRESSES, "127.0.0.1, 127.0.0.2");
        Session *session = object.create(sessionID, settings);
        CHECK(session->inAllowedRemoteAddresses("127.0.0.1"));
        CHECK(session->inAllowedRemoteAddresses("127.0.0.2"));
        CHECK_FALSE(session->inAllowedRemoteAddresses("127.0.0.3"));
        object.destroy(session);
    }
}

TEST_CASE("SessionFactoryDataDictionaryTests")
{
    NullApplication application;
    MemoryStoreFactory messageStoreFactory;
    SessionFactory object(application, messageStoreFactory, 0);

    auto settingsFor = [](const std::string &extraKey, const std::string &extraValue)
    {
        Dictionary settings;
        settings.setString(CONNECTION_TYPE, "acceptor");
        settings.setString(START_TIME, "00:00:00");
        settings.setString(END_TIME, "00:00:00");
        settings.setString(DATA_DICTIONARY, FIX::TestSettings::pathForSpec("FIX44"));
        if (!extraKey.empty())
        {
            settings.setString(extraKey, extraValue);
        }
        return settings;
    };
    auto dictionaryOf = [](Session *session)
    { return &session->getDataDictionaryProvider().getSessionDataDictionary(BeginString("FIX.4.4")); };

    SECTION("sessionsWithTheSameFileAndFlagsShareOneDictionary")
    {
        // Each session used to get its own deep copy of the dictionary -- some 55 MB for FIX 5.0
        // SP2 -- just to apply at most four validation flags (#68).
        Session *a = object.create(SessionID("FIX.4.4", "SHARE1", "T"), settingsFor(VALIDATE_USER_DEFINED_FIELDS, "N"));
        Session *b = object.create(SessionID("FIX.4.4", "SHARE2", "T"), settingsFor(VALIDATE_USER_DEFINED_FIELDS, "N"));
        Session *c = object.create(SessionID("FIX.4.4", "SHARE3", "T"), settingsFor(VALIDATE_USER_DEFINED_FIELDS, "Y"));
        Session *d = object.create(SessionID("FIX.4.4", "SHARE4", "T"), settingsFor("", ""));
        CHECK(dictionaryOf(a) == dictionaryOf(b));
        CHECK(dictionaryOf(a) != dictionaryOf(c)); // a different flag gets its own dictionary
        CHECK(dictionaryOf(a) != dictionaryOf(d));
        CHECK(dictionaryOf(c) != dictionaryOf(d));
        for (Session *session : {a, b, c, d})
        {
            object.destroy(session);
        }
    }

    SECTION("eachSessionGetsTheFieldOrderSettingItAskedFor")
    {
        // The parsed dictionary used to be keyed by path alone, so PreserveMessageFieldsOrder counted
        // only for the first session to load a file.
        Session *plain = object.create(SessionID("FIX.4.4", "ORDER1", "T"), settingsFor("", ""));
        Session *ordered =
            object.create(SessionID("FIX.4.4", "ORDER2", "T"), settingsFor(PRESERVE_MESSAGE_FIELDS_ORDER, "Y"));
        CHECK_FALSE(dictionaryOf(plain)->isMessageFieldsOrderPreserved());
        CHECK(dictionaryOf(ordered)->isMessageFieldsOrderPreserved());
        object.destroy(plain);
        object.destroy(ordered);
    }
}
