/* -*- C++ -*- */

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

#ifndef FIX_MESSAGESORTERS_H
#define FIX_MESSAGESORTERS_H

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#endif

#include "FieldNumbers.h"
#include "SharedArray.h"

#include <functional>
#include <map>
#include <stdarg.h>
#include <vector>

namespace FIX
{
/// Sorts fields in correct header order.
struct header_order
{
    static bool compare(const int x, const int y)
    {
        int orderedX = getOrderedPosition(x);
        int orderedY = getOrderedPosition(y);

        if (orderedX && orderedY)
        {
            return orderedX < orderedY;
        }
        else if (orderedX)
        {
            return true;
        }
        else if (orderedY)
        {
            return false;
        }
        else
        {
            return x < y;
        }
    }

    static int getOrderedPosition(const int field)
    {
        switch (field)
        {
        case FIELD::BeginString:
            return 1;
        case FIELD::BodyLength:
            return 2;
        case FIELD::MsgType:
            return 3;
        default:
            return 0;
        };
    }
};

/// Sorts fields in correct trailer order.
struct trailer_order
{
    static bool compare(const int x, const int y)
    {
        if (x == FIELD::CheckSum)
        {
            return false;
        }
        else if (y == FIELD::CheckSum)
        {
            return true;
        }

        int orderedX = getOrderedPosition(x);
        int orderedY = getOrderedPosition(y);

        if (orderedX && orderedY)
        {
            return orderedX < orderedY;
        }
        else if (orderedX)
        {
            return true;
        }
        else if (orderedY)
        {
            return false;
        }
        else
        {
            return x < y;
        }
    }

    static int getOrderedPosition(const int field)
    {
        switch (field)
        {
        case FIELD::SignatureLength:
            return 1;
        case FIELD::Signature:
            return 2;
        default:
            return 0;
        };
    }
};

/// Sorts fields in correct group order
struct group_order
{
    static bool compare(const int x, const int y, int *order, int largest)
    {
        if (x <= largest && y <= largest)
        {
            int iX = order[x];
            int iY = order[y];
            if (iX == 0 && iY == 0)
            {
                return x < y;
            }
            else if (iX == 0)
            {
                return false;
            }
            else if (iY == 0)
            {
                return true;
            }
            else
            {
                return iX < iY;
            }
        }
        else if (x <= largest)
        {
            return true;
        }
        else if (y <= largest)
        {
            return false;
        }
        else
        {
            return x < y;
        }
    }
};

typedef std::less<int> normal_order;

/**
 * Sorts fields in header, normal, or trailer order.
 *
 * Used as a dynamic sorter to create Header, Trailer, and Message
 * FieldMaps while maintaining the same base type.
 */
struct message_order
{
public:
    enum cmp_mode
    {
        header,
        trailer,
        normal,
        group
    };

    // A group order with no fields is the normal one, so the group comparator can assume a table.
    message_order(cmp_mode mode = normal) : m_mode(mode == group ? normal : mode), m_delim(0), m_mask(0) {}
    message_order(int first, ...);
    message_order(const int order[]);
    message_order(const int order[], size_t size);
    message_order(const message_order &) = default;
    message_order(message_order &&) = default;

    bool operator()(const int x, const int y) const
    {
        switch (m_mode)
        {
        case header:
            return header_order::compare(x, y);
        case trailer:
            return trailer_order::compare(x, y);
        case group:
            return groupCompare(x, y);
        case normal:
        default:
            return x < y;
        }
    }

    message_order &operator=(const message_order &) = default;
    message_order &operator=(message_order &&) = default;

    operator bool() const { return !m_groupOrder.empty(); }

private:
    void setOrder(const int order[], size_t size);

    // Fields the order names come first, by position; the rest follow by number.
    bool groupCompare(const int x, const int y) const
    {
        const int positionX = position(x);
        const int positionY = position(y);
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

    // Returns field's position, or 0 if the order does not name it. The array lookup is inline; the
    // table's probe is not, so that the comparator stays small enough to inline everywhere it did.
    int position(const int field) const
    {
        if (m_mask < 0)
        {
            return static_cast<unsigned>(field) <= static_cast<unsigned>(~m_mask) ? m_groupOrder[field] : 0;
        }
        return probe(field);
    }

    int probe(int field) const;

    // m_groupOrder holds each field's position, counting from 1, in one of two forms. When the
    // field numbers are compact it is upstream's array indexed by field number, and m_mask is the
    // bitwise complement of the largest field number, so negative. Otherwise it is an open-addressed
    // table of (field, position) pairs, a zero field marking an empty slot, with m_mask + 1 slots, a
    // power of two at least twice the number of fields. The array alone cost largest field number + 1
    // ints per order: 200 KB for a FIX 5.0 SP2 group naming field 50000, and 3.8 GB for an SP2
    // DataDictionary, which primes every group's order when it loads (#86).
    cmp_mode m_mode;
    int m_delim;
    shared_array<int> m_groupOrder;
    int m_mask;
};
} // namespace FIX

#endif // FIX_MESSAGESORTERS_H
