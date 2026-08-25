#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace uvsr::json
{
    struct Value
    {
        enum class Kind { Null, Boolean, Number, String, Array, Object };

        Kind kind = Kind::Null;
        bool boolean = false;
        double number = 0.0;
        std::string string;
        std::vector<Value> array;
        std::vector<std::pair<std::string, Value>> object;

        [[nodiscard]] const Value* Find(std::string_view name) const noexcept
        {
            if (kind != Kind::Object)
                return nullptr;
            for (const auto& member : object)
            {
                if (member.first == name)
                    return &member.second;
            }
            return nullptr;
        }
    };

    class Parser
    {
    public:
        explicit Parser(std::string_view input) : m_Input(input)
        {
            if (input.size() >= 3u &&
                static_cast<unsigned char>(input[0]) == 0xefu &&
                static_cast<unsigned char>(input[1]) == 0xbbu &&
                static_cast<unsigned char>(input[2]) == 0xbfu)
            {
                Fail("byte-order marks are not accepted");
            }
        }

        [[nodiscard]] Value Parse()
        {
            SkipWhitespace();
            Value result = ParseValue(0u);
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

        void SkipWhitespace() noexcept
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

        [[nodiscard]] bool Consume(char expected) noexcept
        {
            if (m_Position == m_Input.size() ||
                m_Input[m_Position] != expected)
            {
                return false;
            }
            ++m_Position;
            return true;
        }

        [[nodiscard]] Value ParseValue(unsigned depth)
        {
            if (depth > 64u)
                Fail("nesting limit exceeded");
            if (m_Position == m_Input.size())
                Fail("unexpected end of input");
            switch (m_Input[m_Position])
            {
            case '{': return ParseObject(depth);
            case '[': return ParseArray(depth);
            case '"':
            {
                Value result;
                result.kind = Value::Kind::String;
                result.string = ParseString();
                return result;
            }
            case 't': return ParseLiteral("true", Value::Kind::Boolean, true);
            case 'f': return ParseLiteral("false", Value::Kind::Boolean, false);
            case 'n': return ParseLiteral("null", Value::Kind::Null, false);
            default:
                if (m_Input[m_Position] == '-' ||
                    (m_Input[m_Position] >= '0' &&
                        m_Input[m_Position] <= '9'))
                {
                    return ParseNumber();
                }
                Fail("value expected");
            }
        }

        [[nodiscard]] Value ParseLiteral(
            std::string_view text,
            Value::Kind kind,
            bool boolean)
        {
            if (m_Input.substr(m_Position, text.size()) != text)
                Fail("invalid literal");
            m_Position += text.size();
            Value result;
            result.kind = kind;
            result.boolean = boolean;
            return result;
        }

        [[nodiscard]] Value ParseObject(unsigned depth)
        {
            Value result;
            result.kind = Value::Kind::Object;
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

        [[nodiscard]] Value ParseArray(unsigned depth)
        {
            Value result;
            result.kind = Value::Kind::Array;
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

        [[nodiscard]] Value ParseNumber()
        {
            const std::size_t begin = m_Position;
            (void)Consume('-');
            if (m_Position == m_Input.size())
                Fail("number digits expected");
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
                    Fail("number digits expected");
                while (m_Position < m_Input.size() &&
                    m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
                {
                    ++m_Position;
                }
            }
            if (Consume('.'))
            {
                const std::size_t fraction = m_Position;
                while (m_Position < m_Input.size() &&
                    m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
                {
                    ++m_Position;
                }
                if (fraction == m_Position)
                    Fail("fraction digits expected");
            }
            if (m_Position < m_Input.size() &&
                (m_Input[m_Position] == 'e' || m_Input[m_Position] == 'E'))
            {
                ++m_Position;
                if (m_Position < m_Input.size() &&
                    (m_Input[m_Position] == '+' || m_Input[m_Position] == '-'))
                {
                    ++m_Position;
                }
                const std::size_t exponent = m_Position;
                while (m_Position < m_Input.size() &&
                    m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
                {
                    ++m_Position;
                }
                if (exponent == m_Position)
                    Fail("exponent digits expected");
            }
            Value result;
            result.kind = Value::Kind::Number;
            const char* first = m_Input.data() + begin;
            const char* last = m_Input.data() + m_Position;
            const auto parsed = std::from_chars(
                first, last, result.number, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != last ||
                !std::isfinite(result.number))
            {
                Fail("number is outside the finite double range");
            }
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
                    output.push_back(static_cast<char>(value));
                else
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
                Fail("unexpected low surrogate");
            AppendCodePoint(output, codePoint);
        }

        [[nodiscard]] std::uint32_t ParseHexWord()
        {
            if (m_Position + 4u > m_Input.size())
                Fail("truncated Unicode escape");
            std::uint32_t result = 0u;
            for (unsigned index = 0u; index < 4u; ++index)
            {
                const char value = m_Input[m_Position++];
                result <<= 4u;
                if (value >= '0' && value <= '9') result += value - '0';
                else if (value >= 'a' && value <= 'f') result += value - 'a' + 10;
                else if (value >= 'A' && value <= 'F') result += value - 'A' + 10;
                else Fail("invalid Unicode escape");
            }
            return result;
        }

        void AppendValidatedUtf8(std::string& output, unsigned char first)
        {
            unsigned count = 0u;
            std::uint32_t codePoint = 0u;
            if (first >= 0xc2u && first <= 0xdfu)
            {
                count = 1u;
                codePoint = first & 0x1fu;
            }
            else if (first >= 0xe0u && first <= 0xefu)
            {
                count = 2u;
                codePoint = first & 0x0fu;
            }
            else if (first >= 0xf0u && first <= 0xf4u)
            {
                count = 3u;
                codePoint = first & 0x07u;
            }
            else
                Fail("invalid UTF-8 lead byte");
            output.push_back(static_cast<char>(first));
            for (unsigned index = 0u; index < count; ++index)
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
            const std::uint32_t minimum = count == 1u ? 0x80u :
                count == 2u ? 0x800u : 0x10000u;
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
                output.push_back(static_cast<char>(
                    0x80u | ((value >> 6u) & 0x3fu)));
                output.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
            }
            else
            {
                output.push_back(static_cast<char>(0xf0u | (value >> 18u)));
                output.push_back(static_cast<char>(
                    0x80u | ((value >> 12u) & 0x3fu)));
                output.push_back(static_cast<char>(
                    0x80u | ((value >> 6u) & 0x3fu)));
                output.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
            }
        }

        std::string_view m_Input;
        std::size_t m_Position = 0u;
    };

    [[nodiscard]] inline Value Parse(std::string_view text)
    {
        return Parser(text).Parse();
    }

    [[nodiscard]] inline Value Read(
        const std::filesystem::path& path,
        std::uintmax_t maximumBytes = 16u * 1024u * 1024u)
    {
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("JSON file is missing: " + path.string());
        const std::uintmax_t size = std::filesystem::file_size(path);
        if (size == 0u || size > maximumBytes)
            throw std::runtime_error("JSON file size is invalid: " + path.string());
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot read JSON file: " + path.string());
        const std::string text{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
        if (stream.bad())
            throw std::runtime_error("cannot finish JSON file: " + path.string());
        return Parse(text);
    }
}
