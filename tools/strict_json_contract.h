#pragma once

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
    struct JsonValue
    {
        enum class Kind { String, Integer, Boolean, Object, Array };

        Kind kind = Kind::String;
        std::string string;
        std::int64_t integer = 0;
        bool boolean = false;
        std::vector<std::pair<std::string, JsonValue>> object;
        std::vector<JsonValue> array;
    };

    class JsonParser
    {
    public:
        explicit JsonParser(std::string_view input) : m_Input(input)
        {
            if (input.size() >= 3u &&
                static_cast<unsigned char>(input[0]) == 0xefu &&
                static_cast<unsigned char>(input[1]) == 0xbbu &&
                static_cast<unsigned char>(input[2]) == 0xbfu)
            {
                Fail("byte-order marks are forbidden");
            }
        }

        [[nodiscard]] JsonValue Parse()
        {
            SkipWhitespace();
            JsonValue result = ParseValue(0u);
            SkipWhitespace();
            if (m_Position != m_Input.size())
                Fail("trailing data");
            return result;
        }

    private:
        [[noreturn]] void Fail(std::string_view message) const
        {
            throw std::runtime_error(
                "JSON error at byte " + std::to_string(m_Position) +
                ": " + std::string(message));
        }

        void SkipWhitespace()
        {
            while (m_Position < m_Input.size())
            {
                const char value = m_Input[m_Position];
                if (value != ' ' && value != '\t' && value != '\r' &&
                    value != '\n')
                {
                    break;
                }
                ++m_Position;
            }
        }

        [[nodiscard]] JsonValue ParseValue(unsigned depth)
        {
            if (depth > 16u)
                Fail("nesting limit exceeded");
            if (m_Position == m_Input.size())
                Fail("unexpected end of input");
            if (m_Input[m_Position] == '{')
                return ParseObject(depth);
            if (m_Input[m_Position] == '[')
                return ParseArray(depth);
            if (m_Input[m_Position] == '"')
            {
                JsonValue result;
                result.kind = JsonValue::Kind::String;
                result.string = ParseString();
                return result;
            }
            if (m_Input[m_Position] == '-' ||
                (m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9'))
            {
                return ParseInteger();
            }
            if (m_Input.substr(m_Position, 4u) == "true")
            {
                m_Position += 4u;
                JsonValue result;
                result.kind = JsonValue::Kind::Boolean;
                result.boolean = true;
                return result;
            }
            if (m_Input.substr(m_Position, 5u) == "false")
            {
                m_Position += 5u;
                JsonValue result;
                result.kind = JsonValue::Kind::Boolean;
                return result;
            }
            Fail("unsupported value type");
        }

        [[nodiscard]] JsonValue ParseObject(unsigned depth)
        {
            JsonValue result;
            result.kind = JsonValue::Kind::Object;
            ++m_Position;
            SkipWhitespace();
            if (Consume('}'))
                return result;
            for (;;)
            {
                if (m_Position == m_Input.size() ||
                    m_Input[m_Position] != '"')
                {
                    Fail("object property name expected");
                }
                std::string name = ParseString();
                for (const auto& member : result.object)
                {
                    if (member.first == name)
                        Fail("duplicate object property");
                }
                SkipWhitespace();
                if (!Consume(':'))
                    Fail("colon expected");
                SkipWhitespace();
                result.object.emplace_back(
                    std::move(name), ParseValue(depth + 1u));
                SkipWhitespace();
                if (Consume('}'))
                    return result;
                if (!Consume(','))
                    Fail("comma expected");
                SkipWhitespace();
            }
        }

        [[nodiscard]] JsonValue ParseArray(unsigned depth)
        {
            JsonValue result;
            result.kind = JsonValue::Kind::Array;
            ++m_Position;
            SkipWhitespace();
            if (Consume(']'))
                return result;
            for (;;)
            {
                result.array.push_back(ParseValue(depth + 1u));
                SkipWhitespace();
                if (Consume(']'))
                    return result;
                if (!Consume(','))
                    Fail("comma expected");
                SkipWhitespace();
            }
        }

        [[nodiscard]] JsonValue ParseInteger()
        {
            const std::size_t begin = m_Position;
            if (Consume('-') && m_Position == m_Input.size())
                Fail("integer digits expected");
            if (m_Input[m_Position] == '0')
            {
                ++m_Position;
                if (m_Position < m_Input.size() &&
                    m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
                {
                    Fail("leading zero");
                }
            }
            else
            {
                if (m_Input[m_Position] < '1' || m_Input[m_Position] > '9')
                    Fail("integer digits expected");
                while (m_Position < m_Input.size() &&
                    m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
                {
                    ++m_Position;
                }
            }
            if (m_Position < m_Input.size() &&
                (m_Input[m_Position] == '.' || m_Input[m_Position] == 'e' ||
                    m_Input[m_Position] == 'E'))
            {
                Fail("only integers are accepted");
            }
            JsonValue result;
            result.kind = JsonValue::Kind::Integer;
            const char* first = m_Input.data() + begin;
            const char* last = m_Input.data() + m_Position;
            const auto parsed = std::from_chars(first, last, result.integer);
            if (parsed.ec != std::errc{} || parsed.ptr != last)
                Fail("integer is outside int64 range");
            return result;
        }

        [[nodiscard]] std::string ParseString()
        {
            ++m_Position;
            std::string output;
            while (m_Position < m_Input.size())
            {
                const unsigned char value =
                    static_cast<unsigned char>(m_Input[m_Position++]);
                if (value == '"')
                    return output;
                if (value < 0x20u)
                    Fail("unescaped control character");
                if (value == '\\')
                {
                    ParseEscape(output);
                    continue;
                }
                if (value < 0x80u)
                {
                    output.push_back(static_cast<char>(value));
                    continue;
                }
                AppendValidatedUtf8(output, value);
            }
            Fail("unterminated string");
        }

        void ParseEscape(std::string& output)
        {
            if (m_Position == m_Input.size())
                Fail("unterminated escape");
            const char escaped = m_Input[m_Position++];
            switch (escaped)
            {
            case '"': output.push_back('"'); return;
            case '\\': output.push_back('\\'); return;
            case '/': output.push_back('/'); return;
            case 'b': output.push_back('\b'); return;
            case 'f': output.push_back('\f'); return;
            case 'n': output.push_back('\n'); return;
            case 'r': output.push_back('\r'); return;
            case 't': output.push_back('\t'); return;
            case 'u': break;
            default: Fail("invalid escape");
            }
            std::uint32_t codePoint = ParseHexWord();
            if (codePoint >= 0xd800u && codePoint <= 0xdbffu)
            {
                if (m_Position + 2u > m_Input.size() ||
                    m_Input[m_Position] != '\\' ||
                    m_Input[m_Position + 1u] != 'u')
                {
                    Fail("high surrogate lacks low surrogate");
                }
                m_Position += 2u;
                const std::uint32_t low = ParseHexWord();
                if (low < 0xdc00u || low > 0xdfffu)
                    Fail("invalid low surrogate");
                codePoint = 0x10000u +
                    ((codePoint - 0xd800u) << 10u) + (low - 0xdc00u);
            }
            else if (codePoint >= 0xdc00u && codePoint <= 0xdfffu)
            {
                Fail("unexpected low surrogate");
            }
            AppendCodePoint(output, codePoint);
        }

        [[nodiscard]] std::uint32_t ParseHexWord()
        {
            if (m_Position + 4u > m_Input.size())
                Fail("truncated unicode escape");
            std::uint32_t result = 0u;
            for (unsigned index = 0u; index < 4u; ++index)
            {
                const char value = m_Input[m_Position++];
                result <<= 4u;
                if (value >= '0' && value <= '9')
                    result += static_cast<unsigned>(value - '0');
                else if (value >= 'a' && value <= 'f')
                    result += static_cast<unsigned>(value - 'a' + 10);
                else if (value >= 'A' && value <= 'F')
                    result += static_cast<unsigned>(value - 'A' + 10);
                else
                    Fail("invalid unicode escape");
            }
            return result;
        }

        void AppendValidatedUtf8(std::string& output, unsigned char first)
        {
            unsigned continuationCount = 0u;
            std::uint32_t codePoint = 0u;
            if (first >= 0xc2u && first <= 0xdfu)
            {
                continuationCount = 1u;
                codePoint = first & 0x1fu;
            }
            else if (first >= 0xe0u && first <= 0xefu)
            {
                continuationCount = 2u;
                codePoint = first & 0x0fu;
            }
            else if (first >= 0xf0u && first <= 0xf4u)
            {
                continuationCount = 3u;
                codePoint = first & 0x07u;
            }
            else
                Fail("invalid UTF-8 lead byte");
            output.push_back(static_cast<char>(first));
            for (unsigned index = 0u; index < continuationCount; ++index)
            {
                if (m_Position == m_Input.size())
                    Fail("truncated UTF-8 sequence");
                const unsigned char next =
                    static_cast<unsigned char>(m_Input[m_Position++]);
                if ((next & 0xc0u) != 0x80u)
                    Fail("invalid UTF-8 continuation byte");
                output.push_back(static_cast<char>(next));
                codePoint = (codePoint << 6u) | (next & 0x3fu);
            }
            const std::uint32_t minimum = continuationCount == 1u ? 0x80u :
                continuationCount == 2u ? 0x800u : 0x10000u;
            if (codePoint < minimum || codePoint > 0x10ffffu ||
                (codePoint >= 0xd800u && codePoint <= 0xdfffu))
            {
                Fail("invalid UTF-8 code point");
            }
        }

        static void AppendCodePoint(std::string& output, std::uint32_t value)
        {
            if (value <= 0x7fu)
                output.push_back(static_cast<char>(value));
            else if (value <= 0x7ffu)
            {
                output.push_back(static_cast<char>(0xc0u | (value >> 6u)));
                output.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
            }
            else if (value <= 0xffffu)
            {
                output.push_back(static_cast<char>(0xe0u | (value >> 12u)));
                output.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3fu)));
                output.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
            }
            else
            {
                output.push_back(static_cast<char>(0xf0u | (value >> 18u)));
                output.push_back(static_cast<char>(0x80u | ((value >> 12u) & 0x3fu)));
                output.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3fu)));
                output.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
            }
        }

        bool Consume(char expected)
        {
            if (m_Position == m_Input.size() ||
                m_Input[m_Position] != expected)
            {
                return false;
            }
            ++m_Position;
            return true;
        }

        std::string_view m_Input;
        std::size_t m_Position = 0u;
    };

    [[nodiscard]] inline JsonValue ParseJson(std::string_view text)
    {
        return JsonParser(text).Parse();
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

    [[nodiscard]] inline std::int64_t Integer(
        const JsonValue& value,
        std::string_view description)
    {
        if (value.kind != JsonValue::Kind::Integer)
            throw std::runtime_error(std::string(description) + " is not an integer");
        return value.integer;
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
