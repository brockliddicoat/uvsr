#pragma once

#include "json_document.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace uvsr::contract
{
    using JsonValue = json::Value;

    [[nodiscard]] inline std::int64_t Integer(
        const JsonValue& value,
        std::string_view description)
    {
        std::int64_t result = 0;
        const char* first = value.string.data();
        const char* last = first + value.string.size();
        const auto parsed = std::from_chars(first, last, result);
        if (value.kind != JsonValue::Kind::Number ||
            parsed.ec != std::errc{} || parsed.ptr != last)
        {
            throw std::runtime_error(std::string(description) + " is not an int64 integer");
        }
        return result;
    }

    inline void ValidateContractValues(const JsonValue& value)
    {
        if (value.kind == JsonValue::Kind::Null)
            throw std::runtime_error("null is forbidden in a signed contract");
        if (value.kind == JsonValue::Kind::Number)
            (void)Integer(value, "contract value");
        for (const auto& member : value.object)
            ValidateContractValues(member.second);
        for (const auto& item : value.array)
            ValidateContractValues(item);
    }

    [[nodiscard]] inline JsonValue ParseJson(std::string_view text)
    {
        JsonValue result = json::Parser(text, 16u).Parse();
        ValidateContractValues(result);
        return result;
    }

    inline void RequireExactObject(
        const JsonValue& value,
        std::initializer_list<std::string_view> expected,
        std::string_view description)
    {
        if (value.kind != JsonValue::Kind::Object ||
            value.object.size() != expected.size())
        {
            throw std::runtime_error(
                std::string(description) + " has unexpected properties");
        }
        for (const std::string_view name : expected)
        {
            bool found = false;
            for (const auto& member : value.object)
                found = found || member.first == name;
            if (!found)
            {
                throw std::runtime_error(
                    std::string(description) + " is missing " +
                    std::string(name));
            }
        }
    }

    [[nodiscard]] inline const JsonValue& Member(
        const JsonValue& value,
        std::string_view name)
    {
        if (value.kind != JsonValue::Kind::Object)
            throw std::runtime_error("JSON value is not an object");
        for (const auto& member : value.object)
        {
            if (member.first == name)
                return member.second;
        }
        throw std::runtime_error("missing JSON property " + std::string(name));
    }

    [[nodiscard]] inline const std::string& String(
        const JsonValue& value,
        std::string_view description)
    {
        if (value.kind != JsonValue::Kind::String)
            throw std::runtime_error(std::string(description) + " is not a string");
        return value.string;
    }

    [[nodiscard]] inline bool Boolean(
        const JsonValue& value,
        std::string_view description)
    {
        if (value.kind != JsonValue::Kind::Boolean)
            throw std::runtime_error(std::string(description) + " is not a boolean");
        return value.boolean;
    }

    [[nodiscard]] inline std::string ReadFile(
        const std::filesystem::path& path,
        std::uintmax_t maximumBytes)
    {
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("required file is missing: " + path.string());
        const std::uintmax_t size = std::filesystem::file_size(path);
        if (size == 0u || size > maximumBytes)
            throw std::runtime_error("file size is outside its limit: " + path.string());
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot read " + path.string());
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
    }

    [[nodiscard]] inline bool IsLowerHex(std::string_view value, std::size_t size)
    {
        if (value.size() != size)
            return false;
        for (const char character : value)
        {
            if (!((character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f')))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] inline bool IsCanonicalDottedVersion(
        std::string_view value,
        unsigned requiredParts,
        std::int64_t maximumPart)
    {
        unsigned parts = 0u;
        std::size_t position = 0u;
        for (;;)
        {
            const std::size_t begin = position;
            while (position < value.size() && value[position] >= '0' &&
                value[position] <= '9')
            {
                ++position;
            }
            if (begin == position ||
                (value[begin] == '0' && position - begin != 1u))
            {
                return false;
            }
            std::int64_t part = 0;
            const auto parsed = std::from_chars(
                value.data() + begin, value.data() + position, part);
            if (parsed.ec != std::errc{} || part > maximumPart)
                return false;
            ++parts;
            if (position == value.size())
                return parts == requiredParts;
            if (value[position++] != '.' || position == value.size())
                return false;
        }
    }

    [[nodiscard]] inline std::vector<unsigned char> DecodeBase64(
        std::string_view encoded)
    {
        if (encoded.empty() || encoded.size() % 4u != 0u)
            throw std::runtime_error("Base64 length is not canonical");
        const auto decode = [](char value) -> int
        {
            if (value >= 'A' && value <= 'Z') return value - 'A';
            if (value >= 'a' && value <= 'z') return value - 'a' + 26;
            if (value >= '0' && value <= '9') return value - '0' + 52;
            if (value == '+') return 62;
            if (value == '/') return 63;
            return -1;
        };
        std::vector<unsigned char> result;
        result.reserve(encoded.size() / 4u * 3u);
        for (std::size_t offset = 0u; offset < encoded.size(); offset += 4u)
        {
            const bool last = offset + 4u == encoded.size();
            const bool pad2 = encoded[offset + 2u] == '=';
            const bool pad3 = encoded[offset + 3u] == '=';
            if ((!last && (pad2 || pad3)) || (pad2 && !pad3))
                throw std::runtime_error("Base64 padding is not canonical");
            const int a = decode(encoded[offset]);
            const int b = decode(encoded[offset + 1u]);
            const int c = pad2 ? 0 : decode(encoded[offset + 2u]);
            const int d = pad3 ? 0 : decode(encoded[offset + 3u]);
            if (a < 0 || b < 0 || c < 0 || d < 0)
                throw std::runtime_error("Base64 contains an invalid character");
            if ((pad2 && (b & 0x0f) != 0) ||
                (pad3 && !pad2 && (c & 0x03) != 0))
            {
                throw std::runtime_error("Base64 has nonzero padding bits");
            }
            const std::uint32_t value =
                (static_cast<std::uint32_t>(a) << 18u) |
                (static_cast<std::uint32_t>(b) << 12u) |
                (static_cast<std::uint32_t>(c) << 6u) |
                static_cast<std::uint32_t>(d);
            result.push_back(static_cast<unsigned char>(value >> 16u));
            if (!pad2)
                result.push_back(static_cast<unsigned char>(value >> 8u));
            if (!pad3)
                result.push_back(static_cast<unsigned char>(value));
        }
        return result;
    }

    [[nodiscard]] inline std::string EncodeBase64(
        const std::vector<unsigned char>& bytes)
    {
        constexpr char Alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        result.reserve((bytes.size() + 2u) / 3u * 4u);
        for (std::size_t offset = 0u; offset < bytes.size(); offset += 3u)
        {
            const std::size_t remaining = bytes.size() - offset;
            const std::uint32_t value =
                (static_cast<std::uint32_t>(bytes[offset]) << 16u) |
                (remaining > 1u ?
                    static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u : 0u) |
                (remaining > 2u ? bytes[offset + 2u] : 0u);
            result.push_back(Alphabet[(value >> 18u) & 0x3fu]);
            result.push_back(Alphabet[(value >> 12u) & 0x3fu]);
            result.push_back(remaining > 1u ? Alphabet[(value >> 6u) & 0x3fu] : '=');
            result.push_back(remaining > 2u ? Alphabet[value & 0x3fu] : '=');
        }
        return result;
    }

    [[nodiscard]] inline std::string EncodeBase64(std::string_view text)
    {
        return EncodeBase64(std::vector<unsigned char>(text.begin(), text.end()));
    }

}
