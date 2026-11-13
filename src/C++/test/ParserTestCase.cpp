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

#include <Parser.h>
#include <SocketConnector.h>
#include <SocketServer.h>
#include <Utility.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <vector>

#include "catch_amalgamated.hpp"

using namespace FIX;

namespace
{
// The CPU time this thread has used, in milliseconds. Other threads' work is not counted.
double threadCpuMillis()
{
    rusage usage{};
    getrusage(RUSAGE_THREAD, &usage);
    return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0 +
           (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
}

// The CPU this thread spends on `stream` arriving at a new Parser in `reads` reads, reading after each as a transport
// does.
double parseCpuMillis(const std::string &stream, std::size_t reads)
{
    Parser parser;
    std::string message;
    const std::size_t size = (stream.size() + reads - 1) / reads;
    const double start = threadCpuMillis();
    for (std::size_t offset = 0; offset < stream.size(); offset += size)
    {
        parser.addToStream(stream.data() + offset, std::min(size, stream.size() - offset));
        while (parser.readFixMessage(message))
        {
        }
    }
    return threadCpuMillis() - start;
}

// Checks that `stream`, which frames no message, costs about as much to parse in 256 reads as in one, as linear
// parsing does. Rescanning what is buffered on every read costs over a hundred times as much, so a bound of eight
// times leaves room either way. A ratio and not a time: the same parse costs about ten times as much under TSan as in
// a plain build, and several times as much again on a loaded machine, which slows both parses alike. The floor stops
// a plain build's few milliseconds, where a page fault or a busy sibling core is a large share, from deciding it.
void checkParsingIsLinear(const std::string &stream)
{
    const double oneRead = parseCpuMillis(stream, 1);
    const double everyChunk = parseCpuMillis(stream, 256);
    INFO("one read: " << oneRead << " ms of CPU");
    CHECK(everyChunk < std::max(8 * oneRead, 50.0));
}
} // namespace

TEST_CASE("ParserTests")
{
    Parser object;

    SECTION("extractLength")
    {
        std::string normalLength = "8=FIX.4.2\0019=12\00135=A\001108=30\00110=31\001";
        std::string badLength = "8=FIX.4.2\0019=A\00135=A\001108=30\00110=31\001";
        std::string negativeLength = "8=FIX.4.2\0019=-1\00135=A\001108=30\00110=31\001";
        std::string incomplete_1 = "8=FIX.4.2";
        std::string incomplete_2 = "8=FIX.4.2\0019=12";

        int length = 0;
        std::string::size_type pos = 0;

        CHECK(object.extractLength(length, pos, normalLength));
        CHECK(12 == length);
        CHECK(15 == (int)pos);

        pos = 0;
        length = 0;
        CHECK_THROWS_AS(object.extractLength(length, pos, badLength), MessageParseError);
        CHECK(length <= 0);

        length = 0;
        CHECK(0U == pos);
        CHECK_THROWS_AS(object.extractLength(length, pos, negativeLength), MessageParseError);
        CHECK(length <= 0);

        CHECK(0U == pos);
        object.extractLength(length, pos, incomplete_1);

        object.extractLength(length, pos, incomplete_2);
        CHECK(0U == pos);

        CHECK(!object.extractLength(length, pos, ""));
    }

    SECTION("readFixMessage")
    {
        std::string fixMsg1 = "8=FIX.4.2\0019=12\00135=A\001108=30\00110=31\001";
        std::string fixMsg2 = "8=FIX.4.2\0019=17\00135=4\00136=88\001123=Y\00110=34\001";
        std::string fixMsg3 = "8=FIX.4.2\0019=19\00135=A\001108=30\0019710=8\00110=31\001";
        std::string badLength = "8=FIX.4.2\0019=200A\00135=A\001108=30\00110=31\001";

        object.addToStream(fixMsg1 + fixMsg2 + fixMsg3 + badLength);

        std::string readFixMsg;
        CHECK(object.readFixMessage(readFixMsg));
        CHECK(fixMsg1 == readFixMsg);

        CHECK(object.readFixMessage(readFixMsg));
        CHECK(fixMsg2 == readFixMsg);

        CHECK(object.readFixMessage(readFixMsg));
        CHECK(fixMsg3 == readFixMsg);

        CHECK_THROWS_AS(object.readFixMessage(readFixMsg), MessageParseError);
    }

    SECTION("readPartialFixMessage")
    {
        std::string partFixMsg1 = "8=FIX.4.2\0019=17\00135=4\00136=";
        std::string partFixMsg2 = "88\001123=Y\00110=34\001";

        object.addToStream(partFixMsg1);

        std::string readPartFixMsg;
        CHECK(!object.readFixMessage(readPartFixMsg));
        object.addToStream(partFixMsg2);
        CHECK(object.readFixMessage(readPartFixMsg));
        CHECK(partFixMsg1 + partFixMsg2 == readPartFixMsg);
    }

    SECTION("readMessagesByteByByte")
    {
        std::string fixMsg;
        std::string fixMsg1 = "8=FIX.4.2\0019=54\00135=i\001117=1\001296=1\001302=A\001"
                              "311=DELL\001364=10\001365=DELL\001COMP\001\00110=152\001";
        std::string fixMsg2 = "8=FIX.4.2\0019=17\00135=4\00136=88\001123=Y\00110=34\001";
        std::string fixMsg3 = "8=FIX.4.2\0019=19\00135=A\001108=30\0019710=8\00110=31\001";

        for (unsigned int i = 0; i < fixMsg1.length(); ++i)
        {
            object.addToStream(fixMsg1.c_str() + i, 1);
        }
    }

    SECTION("readMessageWithBadLength")
    {
        std::string fixMsg =
            "8=TEST\0019=TEST\00135=TEST\00149=SS1\00156=RORE\00134=3\00152=20050222-16:45:53\00110=TEST\001";

        object.addToStream(fixMsg);

        std::string readFixMsg;
        CHECK_THROWS_AS(object.readFixMessage(readFixMsg), MessageParseError);
        object.readFixMessage(readFixMsg);
    }

    SECTION("addToStreamExceedsMaxMessageSize")
    {
        std::string chunk(1024 * 1024, 'X');
        for (int i = 0; i < 8; i++)
        {
            object.addToStream(chunk);
        }
        CHECK_THROWS_AS(object.addToStream(chunk), MessageParseError);
    }

    SECTION("readFixMessageDoesNotRescanTheWholeBuffer")
    {
        // A stream that never yields "8=" must not be rescanned from the front on every
        // read: that is quadratic in the buffered size and the caller lets the buffer grow
        // to MAX_MESSAGE_SIZE. '8' bytes are the worst input -- each one is a candidate
        // start of "8=". Feed 1 MB, under the 8 MB cap, in recv-sized chunks.
        checkParsingIsLinear(std::string(1024 * 1024, '8'));
    }

    SECTION("readFixMessageDoesNotRescanForTheLengthHeader")
    {
        // The companion case: "8=" is present but the "\0019=" length header never arrives, so
        // readFixMessage cannot frame a message. It must resume the header search where it left
        // off rather than rescan from the front each read. SOH bytes after "8=" are the worst
        // input -- every byte is a candidate start of "\0019=".
        checkParsingIsLinear("8=" + std::string(1024 * 1024, '\001'));
    }

    SECTION("readFixMessageFramesEveryReadBoundary")
    {
        // However the stream is cut into reads, each message comes out whole and in order.
        // A cut inside "9=<length>" leaves the header present but unfinished; the next read
        // must resume at that header, not past it, or it frames the message on the next
        // message's header instead.
        const std::string first = "8=FIX.4.2\0019=12\00135=A\001108=30\00110=031\001";
        const std::string second = "8=FIX.4.2\0019=5\00135=0\00110=161\001";
        const std::string stream = first + second + first;
        for (std::string::size_type cut1 = 1; cut1 < stream.size(); ++cut1)
        {
            for (std::string::size_type cut2 = cut1; cut2 < stream.size(); ++cut2)
            {
                Parser parser;
                std::vector<std::string> framed;
                std::string message;
                for (const std::string &read :
                     {stream.substr(0, cut1), stream.substr(cut1, cut2 - cut1), stream.substr(cut2)})
                {
                    parser.addToStream(read);
                    while (parser.readFixMessage(message))
                    {
                        framed.push_back(message);
                    }
                }
                INFO("cuts at " << cut1 << " and " << cut2);
                REQUIRE(framed.size() == 3);
                CHECK(framed[0] == first);
                CHECK(framed[1] == second);
                CHECK(framed[2] == first);
            }
        }
    }
}
