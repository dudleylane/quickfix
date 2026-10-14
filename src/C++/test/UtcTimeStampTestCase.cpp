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

#include <FieldTypes.h>
#include <ctime>
#include <vector>

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("UtcTimeStampTests")
{
    SECTION("compare")
    {
        UtcTimeStamp object = UtcTimeStamp::now();
        object.setHour(12);
        object.setMinute(20);
        object.setSecond(10);
        object.setMillisecond(10);

        UtcTimeStamp lesserObject = UtcTimeStamp::now();
        lesserObject.setHour(12);
        lesserObject.setMinute(10);
        lesserObject.setSecond(5);
        lesserObject.setMillisecond(0);
        CHECK(lesserObject < object);
        CHECK(lesserObject <= object);
        CHECK(object > lesserObject);
        CHECK(object >= lesserObject);

        UtcTimeStamp greaterObject = UtcTimeStamp::now();
        greaterObject.setHour(13);
        greaterObject.setMinute(10);
        greaterObject.setSecond(5);
        greaterObject.setMillisecond(0);
        CHECK(greaterObject > object);
        CHECK(greaterObject >= object);
        CHECK(object < greaterObject);
        CHECK(object <= greaterObject);

        CHECK(object <= object);
        CHECK(object >= object);
        CHECK(object == object);
    }

    SECTION("subtract")
    {
        UtcTimeStamp sm(10, 10, 10, 10, 10, 2000);
        UtcTimeStamp lg(10, 10, 20, 10, 10, 2000);
        UtcTimeStamp mid(10, 10, 15, 10, 10, 2000);

        CHECK(0 == (sm - sm));
        CHECK(-10 == (sm - lg));
        CHECK(-5 == (sm - mid));

        CHECK(5 == (mid - sm));
        CHECK(-5 == (mid - lg));
        CHECK(0 == (mid - mid));

        CHECK(10 == (lg - sm));
        CHECK(0 == (lg - lg));
        CHECK(5 == (lg - mid));
    }

    SECTION("addSeconds")
    {
        UtcTimeStamp time(10, 10, 10, 10, 10, 2000);
        time += DateTime::SECONDS_PER_DAY;
        CHECK(10 == time.getHour());
        CHECK(10 == time.getMinute());
        CHECK(10 == time.getSecond());
        CHECK(11 == time.getDate());
        CHECK(10 == time.getMonth());
        CHECK(2000 == time.getYear());
    }
}

// SUITE(UtcTimeStampTests)
// {

// }

// fromUtcTimeT works out the Julian day and time of day by arithmetic rather than gmtime_r (#158). It
// must agree with gmtime_r everywhere: before and at the epoch, at day boundaries, leap days, and
// beyond 2038.
TEST_CASE("UtcTimeStampArithmeticTests")
{
    auto viaGmtime = [](time_t t, int fraction, int precision)
    {
        tm parts{};
        gmtime_r(&t, &parts);
        return DateTime::fromTm(parts, fraction, precision);
    };
    std::vector<time_t> times = {0,
                                 -1,
                                 1,
                                 86399,
                                 86400,
                                 -86400,
                                 -86401,
                                 951782400 /* 2000-02-29 */,
                                 951868799,
                                 4107542400 /* 2100-03-01 */,
                                 2147483647,
                                 2147483648LL,
                                 -2208988800LL /* 1900-01-01 */,
                                 253402300799LL /* 9999-12-31 23:59:59 */};
    for (time_t t = -5000000000LL; t < 10000000000LL; t += 7777777)
    {
        times.push_back(t);
    }
    for (time_t t : times)
    {
        INFO("t=" << t);
        const DateTime arithmetic = DateTime::fromUtcTimeT(t, 123456789, 9);
        const DateTime expected = viaGmtime(t, 123456789, 9);
        CHECK(arithmetic.getJulianDate() == expected.getJulianDate());
        CHECK(arithmetic.getNanosecond() == expected.getNanosecond());
        CHECK(arithmetic.getTimeT() == t);
        const DateTime millis = DateTime::fromUtcTimeT(t, 42);
        CHECK(millis.getJulianDate() == expected.getJulianDate());
        CHECK(millis.getMillisecond() == 42);
    }
}
