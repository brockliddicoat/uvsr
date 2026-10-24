#pragma once

#include "json_document.h"

namespace uvsr::json
{
    class EncodedText;

    // an emitter borrows stable input for two synchronous passes. raw pieces
    // supply schema punctuation; String owns JSON escaping of data values.
    class OutputWriter
    {
    public:
        OutputWriter() noexcept = default;
        [[nodiscard]] bool Raw(TextView value) noexcept;
        [[nodiscard]] bool String(TextView value) noexcept;
        [[nodiscard]] bool StringParts(const TextView* parts, size_t count) noexcept;
        [[nodiscard]] bool Integer(int64_t value) noexcept;
        [[nodiscard]] bool Unsigned(uint64_t value) noexcept;
        [[nodiscard]] bool Boolean(bool value) noexcept { return Raw(value ? "true" : "false"); }
        [[nodiscard]] bool Reject(ErrorCode code, const char* message) noexcept;
        [[nodiscard]] size_t Size() const noexcept { return m_Size; }
        [[nodiscard]] const Error& Failure() const noexcept { return m_Failure; }

    private:
        OutputWriter(char* destination, size_t capacity) noexcept : m_Data(destination), m_Capacity(capacity) {}
        char* m_Data = nullptr;
        size_t m_Capacity = size_t(PTRDIFF_MAX) - 1;
        size_t m_Size = 0;
        Error m_Failure{};
        friend class EncodedText;
    };

    using OutputEmitter = bool (*)(OutputWriter& output, const void* context) noexcept;

    // one measured allocation. failed emission publishes no text. movement and
    // destruction invalidate views; emitters never retain the writer or context.
    class EncodedText
    {
    public:
        EncodedText() noexcept = default;
        explicit EncodedText(OutputEmitter emit, const void* context = nullptr) noexcept;
        ~EncodedText() noexcept;
        EncodedText(const EncodedText&) = delete;
        EncodedText& operator=(const EncodedText&) = delete;
        EncodedText(EncodedText&& source) noexcept;
        EncodedText& operator=(EncodedText&& source) noexcept;
        [[nodiscard]] bool IsValid() const noexcept { return m_Data != nullptr; }
        [[nodiscard]] const char* Data() const noexcept { return m_Data ? m_Data : ""; }
        [[nodiscard]] size_t Size() const noexcept { return m_Size; }
        [[nodiscard]] TextView View() const noexcept { return {Data(), m_Size}; }
        [[nodiscard]] const Error& Failure() const noexcept { return m_Failure; }

    private:
        char* m_Data = nullptr;
        size_t m_Size = 0;
        Error m_Failure{ErrorCode::InvalidOperation, 0, "JSON output is not initialized"};
    };
}
