#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>
#include <system_error>

namespace donut::app
{
    inline bool IsDonutGraphicsApiOption(std::string_view token)
    {
        constexpr std::array<std::string_view, 12> Options = {
            "-d3d11",
            "-dx11",
            "--d3d11",
            "--dx11",
            "-d3d12",
            "-dx12",
            "--d3d12",
            "--dx12",
            "-vk",
            "-vulkan",
            "--vk",
            "--vulkan"
        };
        for (const std::string_view option : Options)
        {
            if (token == option)
                return true;
        }
        return false;
    }

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

    inline bool ParseCommandLineUint32(
        std::string_view text,
        uint32_t& value)
    {
        if (text.empty())
            return false;

        uint32_t parsed = 0u;
        const char* const begin = text.data();
        const char* const end = begin + text.size();
        const std::from_chars_result result =
            std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end)
            return false;
        value = parsed;
        return true;
    }
}
