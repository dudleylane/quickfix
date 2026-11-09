#include "DatabaseLogTestCase.h"
#include <DatabaseLogText.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <utility>

using namespace FIX;

namespace
{
/// Whether value is well-formed UTF-8, found by decoding each code point rather than by DatabaseLogText.h's table.
bool isWellFormedUtf8(const std::string &value)
{
    static const std::uint32_t smallest[] = {0, 0, 0x80, 0x800, 0x10000};
    for (std::size_t i = 0; i < value.size();)
    {
        const unsigned char lead = static_cast<unsigned char>(value[i]);
        std::size_t length = 1;
        std::uint32_t point = lead;
        if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
            point = lead & 0x1F;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
            point = lead & 0x0F;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
            point = lead & 0x07;
        }
        else if (lead > 0x7F)
        {
            return false;
        }

        if (value.size() - i < length)
        {
            return false;
        }
        for (std::size_t j = 1; j < length; ++j)
        {
            const unsigned char next = static_cast<unsigned char>(value[i + j]);
            if ((next & 0xC0) != 0x80)
            {
                return false;
            }
            point = (point << 6) | (next & 0x3F);
        }
        if (length > 1 && (point < smallest[length] || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)))
        {
            return false;
        }
        i += length;
    }
    return true;
}

std::string hex(const std::string &value)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string result;
    for (char c : value)
    {
        const unsigned char byte = static_cast<unsigned char>(c);
        result += digits[byte >> 4];
        result += digits[byte & 0x0F];
        result += ' ';
    }
    return result;
}

/// What is wrong with the text written for value, or nothing: it must read back as value, hold no NUL, and be ASCII,
/// or well-formed UTF-8 where that is kept.
std::string fault(const std::string &value, bool keepUtf8)
{
    const std::string text = databaseLogText(value, keepUtf8);
    if (readDatabaseLogText(text) != value)
    {
        return "reads back otherwise";
    }
    if (text.find('\0') != std::string::npos)
    {
        return "holds a NUL";
    }
    if (keepUtf8 ? !isWellFormedUtf8(text)
                 : !std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; }))
    {
        return keepUtf8 ? "is not UTF-8" : "is not ASCII";
    }
    return "";
}
} // namespace

TEST_CASE("DatabaseLogTextTests")
{
    SECTION("textIsWrittenAsItIs")
    {
        const std::string text = "8=FIX.4.2\x01"
                                 "58=' \" \\ \\' \\x \\xG1 \\x4 ;\x01";
        CHECK(databaseLogText(text, false) == text);
        CHECK(databaseLogText(text, true) == text);
    }

    SECTION("aNulIsWrittenInHex")
    {
        const std::string value("58=a\0b\x01", 7);
        CHECK(databaseLogText(value, false) == "58=a\\x00b\x01");
        CHECK(databaseLogText(value, true) == "58=a\\x00b\x01");
    }

    SECTION("aBackslashThatWouldReadAsHexIsWrittenInHex")
    {
        CHECK(databaseLogText("\\x41", false) == "\\x5Cx41");
        CHECK(databaseLogText("\\xfc", true) == "\\x5Cxfc");
        CHECK(databaseLogText("\\\\x41", false) == "\\\\x5Cx41");
        CHECK(databaseLogText("\\\xFC", false) == "\\\\xFC");
    }

    SECTION("everyByteOverSevenBitsIsWrittenInHex")
    {
        CHECK(databaseLogText("58=caf\xC3\xA9\x01", false) == "58=caf\\xC3\\xA9\x01");
        CHECK(databaseLogText("\xFF", false) == "\\xFF");
    }

    SECTION("wellFormedUtf8IsWrittenAsItIsWhenKept")
    {
        for (const std::string &sequence : wellFormedUtf8Bounds())
        {
            INFO(hex(sequence));
            CHECK(databaseLogText(sequence, true) == sequence);
            CHECK(databaseLogText("a" + sequence + "b", true) == "a" + sequence + "b");
        }
    }

    SECTION("anyOtherByteOverSevenBitsIsWrittenInHexWhenUtf8IsKept")
    {
        const std::vector<std::pair<std::string, std::string>> cases{
            {"\x80", "\\x80"},
            {"\xBF", "\\xBF"},
            {"\xC0\x80", "\\xC0\\x80"},
            {"\xC1\xBF", "\\xC1\\xBF"},
            {"\xE0\x9F\xBF", "\\xE0\\x9F\\xBF"},
            {"\xED\xA0\x80", "\\xED\\xA0\\x80"},
            {"\xED\xBF\xBF", "\\xED\\xBF\\xBF"},
            {"\xF0\x8F\xBF\xBF", "\\xF0\\x8F\\xBF\\xBF"},
            {"\xF4\x90\x80\x80", "\\xF4\\x90\\x80\\x80"},
            {"\xF5\x80\x80\x80", "\\xF5\\x80\\x80\\x80"},
            {"\xFE", "\\xFE"},
            {"\xFF", "\\xFF"},
            {"\xC3", "\\xC3"},
            {"\xE2\x82", "\\xE2\\x82"},
            {"\xF0\x9D\x84", "\\xF0\\x9D\\x84"},
            {"\xC3"
             "A",
             "\\xC3A"},
            {"\xF0\x9D\x84\xC3\xA9", "\\xF0\\x9D\\x84\xC3\xA9"},
        };
        for (const auto &[value, text] : cases)
        {
            INFO(hex(value));
            CHECK(databaseLogText(value, true) == text);
        }
    }

    SECTION("everyValueReadsBack")
    {
        std::vector<std::string> faults;
        const auto check = [&faults](const std::string &value)
        {
            for (bool keepUtf8 : {false, true})
            {
                const std::string why = fault(value, keepUtf8);
                if (!why.empty() && faults.size() < 10)
                {
                    faults.push_back(hex(value) + (keepUtf8 ? "kept " : "") + why);
                }
            }
        };

        for (int first = 0; first < 256; ++first)
        {
            for (int second = 0; second < 256; ++second)
            {
                check({static_cast<char>(first), static_cast<char>(second)});
            }
        }

        // Values built from the pieces that interact: backslashes, x, hex digits and not, a NUL, and UTF-8 whole, cut
        // short and out of range.
        std::vector<std::string> pieces{"\\", "x", "A", "f", "0", "G", std::string(1, '\0'), "\x01"};
        for (const std::string &sequence : wellFormedUtf8Bounds())
        {
            pieces.push_back(sequence);
        }
        for (const std::string &sequence : illFormedUtf8())
        {
            pieces.push_back(sequence);
        }
        std::mt19937 random(20261109);
        std::uniform_int_distribution<std::size_t> piece(0, pieces.size() - 1);
        std::uniform_int_distribution<int> count(1, 8);
        for (int n = 0; n < 100000; ++n)
        {
            std::string value;
            for (int k = count(random); k > 0; --k)
            {
                value += pieces[piece(random)];
            }
            check(value);
        }

        CHECK(faults == std::vector<std::string>{});
    }
}
