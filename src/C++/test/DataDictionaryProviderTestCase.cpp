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

#include <DataDictionaryProvider.h>

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("DataDictionaryProviderTests")
{
    SECTION("getApplicationDataDictionary_DataDictionaryNotSet")
    {
        DataDictionaryProvider object;
        DataDictionary expected;

        ApplVerID id;
        DataDictionary actual = object.getApplicationDataDictionary(id);

        CHECK(expected.getVersion() == actual.getVersion());
        CHECK(expected.getOrderedFields() == actual.getOrderedFields());
    }
}
