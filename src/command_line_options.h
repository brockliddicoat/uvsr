#pragma once

#include <charconv>
#include <string_view>
#include <system_error>

namespace uvsr
{
    inline bool ParseCommandLineInt(
        std::string_view text,
        int minimum,
        int maximum,
        int& value)
    {
        if (text.empty() || minimum > maximum)
            return false;

        int parsed = 0;
        const char* const begin = text.data();
        const char* const end = begin + text.size();
        const std::from_chars_result result =
            std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} ||
            result.ptr != end ||
            parsed < minimum ||
            parsed > maximum)
        {
            return false;
        }
        value = parsed;
        return true;
    }
}
