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

#include <algorithm>
#include <cstdint>

namespace FIX
{
namespace
{
// Fibonacci hashing, in unsigned arithmetic: field numbers are often consecutive.
unsigned slotOf(const int field)
{
    const std::uint64_t product = static_cast<std::uint32_t>(field) * UINT64_C(0x9E3779B97F4A7C15);
    return static_cast<unsigned>(product >> 32);
}
} // namespace

message_order::message_order(int first, ...) : m_mode(group), m_delim(0), m_mask(0)
{
    std::vector<int> order;

    va_list arguments;
    va_start(arguments, first);
    for (int field = first; field != 0; field = va_arg(arguments, int))
    {
        order.push_back(field);
    }
    va_end(arguments);

    setOrder(order.data(), order.size());
}

message_order::message_order(const int order[]) : m_mode(group), m_delim(0), m_mask(0)
{
    int size = 0;
    while (order[size] != 0)
    {
        ++size;
    }
    setOrder(order, size);
}

message_order::message_order(const int order[], size_t size) : m_mode(group), m_delim(0), m_mask(0)
{
    setOrder(order, size);
}

void message_order::setOrder(const int order[], size_t size)
{
    if (size < 1)
    {
        // Nothing to look up: compare as the normal order does.
        m_mode = normal;
        return;
    }
    m_delim = order[0];

    // Half full at most, so every probe sequence reaches an empty slot.
    size_t slots = 4;
    int largest = 0;
    for (size_t i = 0; i < size; ++i)
    {
        largest = std::max(largest, order[i]);
    }
    while (slots < 2 * size)
    {
        slots *= 2;
    }

    // An array indexed by field number is the faster lookup, so keep it while it costs no more than
    // eight times the table; with FIX 5.0 SP2's field numbers it would cost far more.
    if (static_cast<size_t>(largest) + 1 <= 16 * slots)
    {
        // create() zeroes the array: a field the order does not name has position 0.
        m_groupOrder = shared_array<int>::create(largest + 1);
        m_mask = ~largest;
        int *positions = m_groupOrder;
        for (size_t i = 0; i < size; ++i)
        {
            if (order[i] > 0)
            {
                positions[order[i]] = static_cast<int>(i + 1);
            }
        }
        return;
    }

    // create() zeroes the table: every slot starts empty.
    m_groupOrder = shared_array<int>::create(2 * slots);
    m_mask = static_cast<int>(slots - 1);

    int *table = m_groupOrder;
    for (size_t i = 0; i < size; ++i)
    {
        const int field = order[i];
        if (field <= 0)
        {
            // Zero marks an empty slot, and no field number is negative.
            continue;
        }
        unsigned slot = slotOf(field) & static_cast<unsigned>(m_mask);
        while (table[2 * slot] != 0 && table[2 * slot] != field)
        {
            slot = (slot + 1) & static_cast<unsigned>(m_mask);
        }
        // A field named twice takes its last position, as in the array.
        table[2 * slot] = field;
        table[2 * slot + 1] = static_cast<int>(i + 1);
    }
}
int message_order::probe(int field) const
{
    const int *table = m_groupOrder;
    const unsigned mask = static_cast<unsigned>(m_mask);
    for (unsigned slot = slotOf(field) & mask;; slot = (slot + 1) & mask)
    {
        const int key = table[2 * slot];
        if (key == field)
        {
            return table[2 * slot + 1];
        }
        if (key == 0)
        {
            return 0;
        }
    }
}
} // namespace FIX
