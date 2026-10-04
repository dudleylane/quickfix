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

#include "FieldConvertors.h"
#include "Parser.h"
#include "Utility.h"
#include <algorithm>

namespace FIX
{
bool Parser::extractLength(int &length, std::string::size_type &pos, const std::string &buffer,
                           std::string::size_type searchFrom) EXCEPT(MessageParseError)
{
    if (!buffer.size())
    {
        return false;
    }

    std::string::size_type startPos = buffer.find("\0019=", searchFrom);
    if (startPos == std::string::npos)
    {
        return false;
    }
    startPos += 3;
    std::string::size_type endPos = buffer.find("\001", startPos);
    if (endPos == std::string::npos)
    {
        return false;
    }

    std::string strLength(buffer, startPos, endPos - startPos);

    try
    {
        length = IntConvertor::convert(strLength);
        if (length < 0)
        {
            throw MessageParseError();
        }
    }
    catch (FieldConvertError &)
    {
        throw MessageParseError();
    }

    pos = endPos + 1;
    return true;
}

bool Parser::readFixMessage(std::string &str) EXCEPT(MessageParseError)
{
    std::string::size_type pos = 0;

    if (m_buffer.length() < 2)
    {
        return false;
    }
    pos = m_buffer.find("8=");
    if (pos == std::string::npos)
    {
        // No message start anywhere in the buffer. Drop the scanned bytes, keeping only a
        // trailing byte that could be the '8' of a "8=" split across reads, so the next read
        // does not rescan them. Without this a stream that never yields "8=" is rescanned from
        // the front on every read -- quadratic in the buffered size, which the caller lets grow
        // to MAX_MESSAGE_SIZE.
        if (m_buffer.size() > 1)
        {
            m_buffer.erase(0, m_buffer.size() - 1);
        }
        m_lengthSearchFrom = 0;
        return false;
    }
    if (pos > 0)
    {
        // Discarding leading garbage moves the front, so the resumed length search no longer applies.
        m_buffer.erase(0, pos);
        m_lengthSearchFrom = 0;
    }

    int length = 0;

    try
    {
        // Resume the "\0019=" search where the last read left off, overlapping by the needle length
        // less one so a header split across reads is still found.
        std::string::size_type searchFrom = m_lengthSearchFrom > 2 ? m_lengthSearchFrom - 2 : 0;
        if (extractLength(length, pos, m_buffer, searchFrom))
        {
            m_lengthSearchFrom = 0;
            pos += length;
            if (m_buffer.size() < pos)
            {
                return false;
            }

            pos = m_buffer.find("\00110=", pos - 1);
            if (pos == std::string::npos)
            {
                return false;
            }
            pos += 4;
            pos = m_buffer.find("\001", pos);
            if (pos == std::string::npos)
            {
                return false;
            }
            pos += 1;

            str.assign(m_buffer, 0, pos);
            m_buffer.erase(0, pos);
            return true;
        }
        else
        {
            // "8=" is at the front but the buffer holds no "\0019=" yet. Remember how far we
            // scanned so the next read resumes here instead of from the front.
            m_lengthSearchFrom = m_buffer.size();
        }
    }
    catch (MessageParseError &e)
    {
        if (length > 0)
        {
            m_buffer.erase(0, pos + length);
        }
        else
        {
            m_buffer.erase();
        }
        m_lengthSearchFrom = 0;

        throw e;
    }

    return false;
}
} // namespace FIX
