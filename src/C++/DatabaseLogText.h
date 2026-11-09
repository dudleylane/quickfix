/* -*- C++ -*- */

#ifndef FIX_DATABASELOGTEXT_H
#define FIX_DATABASELOGTEXT_H

#include <cstddef>
#include <string>

namespace FIX
{
namespace detail
{
/// The length of the well-formed UTF-8 sequence starting at value[i], by the Unicode standard's table, or 0 if none
/// does.
inline std::size_t utf8SequenceLength(const std::string &value, std::size_t i)
{
    const auto byte = [&value](std::size_t j) { return static_cast<unsigned char>(value[j]); };
    const unsigned char lead = byte(i);
    std::size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF)
    {
        length = 2;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
        length = 3;
        low = lead == 0xE0 ? 0xA0 : 0x80;
        high = lead == 0xED ? 0x9F : 0xBF;
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
        length = 4;
        low = lead == 0xF0 ? 0x90 : 0x80;
        high = lead == 0xF4 ? 0x8F : 0xBF;
    }
    else
    {
        return 0;
    }

    if (value.size() - i < length || byte(i + 1) < low || byte(i + 1) > high)
    {
        return 0;
    }
    for (std::size_t j = i + 2; j < i + length; ++j)
    {
        if (byte(j) < 0x80 || byte(j) > 0xBF)
        {
            return 0;
        }
    }
    return length;
}

inline bool isHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
} // namespace detail

/// The text a database log writes for value. Each NUL and each byte over 0x7F is written as \x and two hex digits,
/// except the bytes of well-formed UTF-8 when keepUtf8 is set, and so is a backslash that would otherwise read as the
/// start of one. Every other byte is written as it is, so reading each \x and two hex digits back as that byte gives
/// value again.
inline std::string databaseLogText(const std::string &value, bool keepUtf8)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string text;
    text.reserve(value.size());
    for (std::size_t i = 0; i < value.size();)
    {
        const unsigned char byte = static_cast<unsigned char>(value[i]);
        const std::size_t sequence = keepUtf8 && byte > 0x7F ? detail::utf8SequenceLength(value, i) : 0;
        if (sequence)
        {
            text.append(value, i, sequence);
            i += sequence;
            continue;
        }

        const bool readsAsHex = byte == '\\' && value.size() - i > 3 && value[i + 1] == 'x' &&
                                detail::isHexDigit(value[i + 2]) && detail::isHexDigit(value[i + 3]);
        if (byte == 0 || byte > 0x7F || readsAsHex)
        {
            text += "\\x";
            text += digits[byte >> 4];
            text += digits[byte & 0x0F];
        }
        else
        {
            text += static_cast<char>(byte);
        }
        ++i;
    }
    return text;
}
} // namespace FIX

#endif // FIX_DATABASELOGTEXT_H
