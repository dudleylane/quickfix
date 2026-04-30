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

#ifndef FIX_PARSER_H
#define FIX_PARSER_H

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#endif

#include "Exceptions.h"
#include <iostream>
#include <string>

namespace FIX
{
/// Parses %FIX messages off an input stream.
class Parser
{
public:
    static constexpr size_t MAX_MESSAGE_SIZE = 8 * 1024 * 1024;

    Parser() {}
    ~Parser() {}

    bool extractLength(int &length, std::string::size_type &pos, const std::string &buffer) EXCEPT(MessageParseError);
    [[nodiscard]] bool readFixMessage(std::string &str) EXCEPT(MessageParseError);

    void addToStream(const char *str, size_t len)
    {
        if (m_buffer.size() + len > MAX_MESSAGE_SIZE) [[unlikely]]
            throw MessageParseError("Message size exceeds maximum allowed");
        m_buffer.append(str, len);
    }
    void addToStream(const std::string &str)
    {
        if (m_buffer.size() + str.size() > MAX_MESSAGE_SIZE) [[unlikely]]
            throw MessageParseError("Message size exceeds maximum allowed");
        m_buffer.append(str);
    }

private:
    std::string m_buffer;
};
} // namespace FIX
#endif // FIX_PARSER_H
