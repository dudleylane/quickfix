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

#include "MessageSorters.h"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace FIX;

namespace
{
// The group comparator as it was when message_order kept a dense array indexed by field number:
// what its open-addressed table must reproduce (#86).
struct DenseGroupOrder
{
    explicit DenseGroupOrder(const std::vector<int> &order)
    {
        largest = *std::max_element(order.begin(), order.end());
        positions.assign(largest + 1, 0);
        for (size_t i = 0; i < order.size(); ++i)
        {
            positions[order[i]] = static_cast<int>(i + 1);
        }
    }

    bool operator()(int x, int y) const
    {
        const int positionX = x >= 0 && x <= largest ? positions[x] : 0;
        const int positionY = y >= 0 && y <= largest ? positions[y] : 0;
        if (positionX && positionY)
        {
            return positionX < positionY;
        }
        else if (positionX)
        {
            return true;
        }
        else if (positionY)
        {
            return false;
        }
        return x < y;
    }

    int largest;
    std::vector<int> positions;
};

long residentKB()
{
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
    {
        if (line.rfind("VmRSS:", 0) == 0)
        {
            return std::stol(line.substr(6));
        }
    }
    return -1;
}
} // namespace

TEST_CASE("MessageSortersTestCase")
{
    SECTION("headerOrder")
    {
        CHECK(!header_order::compare(FIELD::BeginString, FIELD::BeginString));
        CHECK(header_order::compare(FIELD::BeginString, FIELD::BodyLength));
        CHECK(header_order::compare(FIELD::BeginString, FIELD::MsgType));
        CHECK(header_order::compare(FIELD::BeginString, 1));
        CHECK(header_order::compare(FIELD::BeginString, 100));
        CHECK(header_order::compare(FIELD::BeginString, 0));

        CHECK(!header_order::compare(FIELD::BodyLength, FIELD::BeginString));
        CHECK(!header_order::compare(FIELD::BodyLength, FIELD::BodyLength));
        CHECK(header_order::compare(FIELD::BodyLength, FIELD::MsgType));
        CHECK(header_order::compare(FIELD::BodyLength, 1));
        CHECK(header_order::compare(FIELD::BeginString, 100));
        CHECK(header_order::compare(FIELD::BodyLength, 0));

        CHECK(!header_order::compare(FIELD::MsgType, FIELD::BeginString));
        CHECK(!header_order::compare(FIELD::MsgType, FIELD::BodyLength));
        CHECK(!header_order::compare(FIELD::MsgType, FIELD::MsgType));
        CHECK(header_order::compare(FIELD::MsgType, 1));
        CHECK(header_order::compare(FIELD::BeginString, 100));
        CHECK(header_order::compare(FIELD::MsgType, 0));

        CHECK(!header_order::compare(1, FIELD::BeginString));
        CHECK(!header_order::compare(1, FIELD::BodyLength));
        CHECK(!header_order::compare(1, FIELD::MsgType));
        CHECK(!header_order::compare(1, 1));
        CHECK(header_order::compare(1, 100));
        CHECK(!header_order::compare(1, 0));

        CHECK(!header_order::compare(100, FIELD::BeginString));
        CHECK(!header_order::compare(100, FIELD::BodyLength));
        CHECK(!header_order::compare(100, FIELD::MsgType));
        CHECK(!header_order::compare(100, 1));
        CHECK(!header_order::compare(100, 100));
        CHECK(!header_order::compare(100, 0));

        CHECK(!header_order::compare(0, FIELD::BeginString));
        CHECK(!header_order::compare(0, FIELD::BodyLength));
        CHECK(!header_order::compare(0, FIELD::MsgType));
        CHECK(header_order::compare(0, 1));
        CHECK(header_order::compare(0, 100));
        CHECK(!header_order::compare(0, 0));
    }

    SECTION("trailerOrder")
    {
        CHECK(!trailer_order::compare(FIELD::CheckSum, 0));
        CHECK(!trailer_order::compare(FIELD::CheckSum, 1));
        CHECK(!trailer_order::compare(FIELD::CheckSum, 100));

        CHECK(trailer_order::compare(0, FIELD::CheckSum));
        CHECK(trailer_order::compare(1, FIELD::CheckSum));
        CHECK(trailer_order::compare(100, FIELD::CheckSum));

        CHECK(trailer_order::compare(FIELD::SignatureLength, 0));
        CHECK(trailer_order::compare(FIELD::SignatureLength, 1));
        CHECK(trailer_order::compare(FIELD::SignatureLength, 100));

        CHECK(!trailer_order::compare(0, FIELD::SignatureLength));
        CHECK(!trailer_order::compare(1, FIELD::SignatureLength));
        CHECK(!trailer_order::compare(100, FIELD::SignatureLength));

        CHECK(!trailer_order::compare(FIELD::Signature, FIELD::SignatureLength));
        CHECK(trailer_order::compare(FIELD::SignatureLength, FIELD::Signature));
        CHECK(!trailer_order::compare(FIELD::Signature - 1, FIELD::Signature));
        CHECK(trailer_order::compare(FIELD::Signature, FIELD::Signature + 1));
        CHECK(!trailer_order::compare(FIELD::Signature + 1, FIELD::Signature));
        CHECK(trailer_order::compare(FIELD::Signature, FIELD::Signature - 1));
        CHECK(!trailer_order::compare(FIELD::Signature, FIELD::Signature));
        CHECK(!trailer_order::compare(FIELD::SignatureLength - 1, FIELD::SignatureLength));
        CHECK(trailer_order::compare(FIELD::SignatureLength, FIELD::SignatureLength + 1));
        CHECK(!trailer_order::compare(FIELD::SignatureLength + 1, FIELD::SignatureLength));
        CHECK(trailer_order::compare(FIELD::SignatureLength, FIELD::SignatureLength - 1));
        CHECK(!trailer_order::compare(FIELD::SignatureLength, FIELD::SignatureLength));
    }

    SECTION("normalOrder")
    {
        CHECK(!trailer_order::compare(1, 1));
        CHECK(trailer_order::compare(1, 2));
        CHECK(trailer_order::compare(1, 3));

        CHECK(!trailer_order::compare(2, 1));
        CHECK(!trailer_order::compare(2, 2));
        CHECK(trailer_order::compare(2, 3));

        CHECK(!trailer_order::compare(3, 1));
        CHECK(!trailer_order::compare(3, 2));
        CHECK(!trailer_order::compare(3, 3));
    }

    SECTION("groupOrder")
    {
        int order[6] = {50, 12, 100, 11, 49, 0};
        message_order sorter(order);

        CHECK(!sorter(50, 50));
        CHECK(sorter(50, 12));
        CHECK(sorter(50, 100));
        CHECK(sorter(50, 11));
        CHECK(sorter(50, 49));

        CHECK(!sorter(12, 50));
        CHECK(!sorter(12, 12));
        CHECK(sorter(12, 100));
        CHECK(sorter(12, 11));
        CHECK(sorter(12, 49));

        CHECK(!sorter(100, 50));
        CHECK(!sorter(100, 12));
        CHECK(!sorter(100, 100));
        CHECK(sorter(100, 11));
        CHECK(sorter(100, 49));

        CHECK(!sorter(11, 50));
        CHECK(!sorter(11, 12));
        CHECK(!sorter(11, 100));
        CHECK(!sorter(11, 11));
        CHECK(sorter(11, 49));

        CHECK(!sorter(49, 50));
        CHECK(!sorter(49, 12));
        CHECK(!sorter(49, 100));
        CHECK(!sorter(49, 11));
        CHECK(!sorter(49, 49));

        CHECK(sorter(50, 49));
        CHECK(sorter(50, 51));

        CHECK(sorter(12, 11));
        CHECK(sorter(12, 13));

        CHECK(sorter(100, 99));
        CHECK(sorter(100, 101));

        CHECK(sorter(11, 10));
        CHECK(sorter(11, 13));

        CHECK(sorter(49, 48));
        CHECK(sorter(49, 51));
    }

    SECTION("groupOrderMatchesDenseOrder")
    {
        std::mt19937 random(86);
        for (int round = 0; round < 60; ++round)
        {
            // Small orders, and ones large enough that probe sequences collide; field numbers up to
            // FIX 5.0 SP2's, with repeats.
            const int size = round % 3 == 0 ? 600 + round : 1 + round % 40;
            const int range = round % 2 ? 60000 : 1500;
            std::uniform_int_distribution<int> fieldOf(1, range);
            std::vector<int> order;
            for (int i = 0; i < size; ++i)
            {
                order.push_back(i > 0 && i % 7 == 0 ? order[i / 2] : fieldOf(random));
            }

            const message_order sorter(order.data(), order.size());
            const DenseGroupOrder dense(order);

            std::vector<int> queries(order.begin(), order.begin() + std::min<size_t>(order.size(), 60));
            for (int i = 0; i < 60; ++i)
            {
                queries.push_back(fieldOf(random));
            }
            queries.push_back(0);
            queries.push_back(dense.largest + 1);
            queries.push_back(range + 1000);

            int mismatches = 0;
            std::string first;
            for (int x : queries)
            {
                for (int y : queries)
                {
                    if (sorter(x, y) != dense(x, y) && mismatches++ == 0)
                    {
                        first = std::to_string(x) + " vs " + std::to_string(y);
                    }
                }
            }
            INFO("round " << round << ", first mismatch " << first);
            CHECK(mismatches == 0);
        }
    }

    SECTION("groupOrderRepeatedFieldTakesLastPosition")
    {
        int order[] = {10, 20, 10, 0};
        message_order sorter(order);

        CHECK(sorter(20, 10));
        CHECK(!sorter(10, 20));
    }

    SECTION("groupOrderWithNoFieldsIsNumeric")
    {
        int none[] = {0};
        message_order sorter(none);

        CHECK(sorter(1, 2));
        CHECK(!sorter(2, 1));
        CHECK(!sorter(2, 2));
    }

    SECTION("groupOrderMemoryFollowsFieldCount")
    {
        // A dense array indexed by field number cost 200 KB for an order naming field 50000, so these
        // 10,000 orders held 2 GB; the table costs a few dozen bytes each (#86).
        const long before = residentKB();
        std::vector<message_order> orders;
        orders.reserve(10000);
        for (int i = 0; i < 10000; ++i)
        {
            orders.push_back(message_order(453, 448, 447, 50000 + i % 3, 0));
        }
        const long after = residentKB();

        REQUIRE(before > 0);
        CHECK(after - before < 100 * 1024);
        CHECK(orders.back()(448, 50002));
    }
}
