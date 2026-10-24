#pragma once

#include "json_document.h"
#include "file_bytes.h"

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

// temporary callers retain their existing exception/string boundary. stage 09.05
// removes this adapter; document ownership and parsing remain in json_document.
namespace uvsr::json
{
    class LegacyError : public std::exception
    {
        std::string m_Message;
    public:
        explicit LegacyError(std::string message) : m_Message(std::move(message)) {}
        const char* what() const noexcept override { return m_Message.c_str(); }
    };

    inline TextView View(std::string_view value) noexcept { return {value.data(), value.size()}; }
    inline std::string_view Borrow(TextView value) noexcept
    { return {value.data ? value.data : "", value.size}; }
    inline std::string PathText(const std::filesystem::path& path)
    {
        const auto bytes = path.u8string();
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    [[noreturn]] inline void Throw(Error error)
    {
        throw LegacyError("JSON error at byte " + std::to_string(error.byte) + ": " + error.message);
    }

    [[nodiscard]] inline Document Parse(std::string_view input, unsigned depth = 64)
    {
        Document result;
        Error error;
        if (!result.Parse(View(input), error, depth)) Throw(error);
        return result;
    }

    [[nodiscard]] inline Document Clone(Value value)
    {
        Document result;
        Error error;
        if (!result.Assign(value, error)) Throw(error);
        return result;
    }

    [[nodiscard]] inline std::string Escape(std::string_view input)
    {
        size_t size = 0;
        Error error;
        if (!MeasureEscaped(View(input), size, error)) Throw(error);
        std::string result(size + 1, '\0');
        if (!WriteEscaped(View(input), result.data(), result.size(), size, error)) Throw(error);
        result.resize(size);
        return result;
    }

    [[nodiscard]] inline std::string Serialize(Value value)
    {
        size_t size = 0;
        Error error;
        if (!MeasureSerialized(value, size, error)) Throw(error);
        std::string result(size + 1, '\0');
        if (!WriteSerialized(value, result.data(), result.size(), size, error)) Throw(error);
        result.resize(size);
        return result;
    }

    [[nodiscard]] inline Document Read(const std::filesystem::path& path,
        uint64_t maximumBytes = 16u * 1024u * 1024u)
    {
        FileBytes bytes;
        FileReadResult result;
        if (!ReadFileBytes(path.c_str(), maximumBytes, bytes, result))
        {
            const char* message = result.error == FileReadError::Missing || result.error == FileReadError::NotRegular ?
                "JSON file is missing: " : result.error == FileReadError::TooLarge ?
                "JSON file size is invalid: " : result.error == FileReadError::Read || result.error == FileReadError::Close ?
                "cannot finish JSON file: " : "cannot read JSON file: ";
            throw LegacyError(message + PathText(path));
        }
        if (!bytes.Size()) throw LegacyError("JSON file size is invalid: " + PathText(path));
        return Parse({bytes.Data(), bytes.Size()});
    }
}
