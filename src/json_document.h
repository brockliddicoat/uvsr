#pragma once

#include <stddef.h>
#include <stdint.h>

namespace uvsr::json
{
    struct TextView
    {
        const char* data = nullptr;
        size_t size = 0;
        constexpr TextView() = default;
        constexpr TextView(const char* bytes, size_t count) : data(bytes), size(count) {}
        TextView(const char* terminated) noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
    };

    [[nodiscard]] bool SameText(TextView left, TextView right) noexcept;

    enum class Kind : uint8_t { Null, Boolean, Number, String, Array, Object };
    enum class ErrorCode : uint8_t
    {
        None, InvalidInput, OutOfMemory, Capacity, InvalidView, InvalidOperation
    };
    struct Error
    {
        ErrorCode code = ErrorCode::None;
        size_t byte = 0;
        const char* message = "";
    };

    class Document;
    struct Value
    {
        const Document* owner = nullptr;
        uint32_t index = UINT32_MAX;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] Kind Type() const noexcept;
        [[nodiscard]] bool Boolean() const noexcept;
        [[nodiscard]] double Number() const noexcept;
        // strings are decoded; numbers retain their original token.
        [[nodiscard]] TextView Text() const noexcept;
        [[nodiscard]] TextView Name() const noexcept;
        [[nodiscard]] size_t Count() const noexcept;
        [[nodiscard]] Value First() const noexcept;
        [[nodiscard]] Value Next() const noexcept;
        [[nodiscard]] Value Parent() const noexcept;
        [[nodiscard]] Value Find(TextView name) const noexcept;
        [[nodiscard]] Value At(size_t position) const noexcept;
    };

    enum class SeedKind : uint8_t { Null, Boolean, Integer, String, Copy, Array, Object };
    // seeds borrow strings/documents only until the synchronous builder returns.
    struct Seed
    {
        SeedKind kind = SeedKind::Null;
        bool boolean = false;
        int64_t integer = 0;
        TextView text;
        Value copy;

        Seed() = default;
        Seed(Value value) noexcept : kind(SeedKind::Copy), copy(value) {}
        Seed(const Document& value) noexcept;
        [[nodiscard]] static Seed String(TextView value) noexcept;
        [[nodiscard]] static Seed Integer(int64_t value) noexcept;
        [[nodiscard]] static Seed Boolean(bool value) noexcept;
        [[nodiscard]] static Seed Array() noexcept;
        [[nodiscard]] static Seed Object() noexcept;
    };
    struct Member
    {
        TextView name;
        Seed value;
    };

    // document/text views borrow this owner. mutation, movement and destruction
    // invalidate them. failed operations preserve the document and its views.
    class Document
    {
    public:
        Document() noexcept = default;
        ~Document() noexcept;
        Document(const Document&) = delete;
        Document& operator=(const Document&) = delete;
        Document(Document&& other) noexcept;
        Document& operator=(Document&& other) noexcept;

        [[nodiscard]] Value Root() const noexcept { return {this, 0}; }
        [[nodiscard]] bool Parse(TextView input, Error& error, unsigned maximumDepth = 64) noexcept;
        [[nodiscard]] bool Assign(Seed value, Error& error) noexcept;
        [[nodiscard]] bool MakeObject(const Member* members, size_t count, Error& error) noexcept;
        [[nodiscard]] bool Replace(TextView name, Seed replacement, Error& error) noexcept;
        [[nodiscard]] bool Append(Seed value, Error& error) noexcept;
        void Clear() noexcept;
        [[nodiscard]] size_t NodeCount() const noexcept { return m_Count ? m_Count : 1; }
        [[nodiscard]] size_t StorageBytes() const noexcept;

    private:
        struct Node;
        Node* m_Nodes = nullptr;
        char* m_Text = nullptr;
        uint32_t m_Count = 0;
        uint32_t m_Capacity = 0;
        size_t m_TextSize = 0;
        size_t m_TextCapacity = 0;

        [[nodiscard]] bool Prepare(size_t nodes, size_t textBytes, Error& error) noexcept;
        [[nodiscard]] bool AddText(TextView value, size_t& offset, Error& error) noexcept;
        [[nodiscard]] bool AddNode(Kind kind, uint32_t parent, uint32_t& index, Error& error) noexcept;
        void Adopt(Document& candidate) noexcept;
        [[nodiscard]] const Node* Get(uint32_t index) const noexcept;
        friend struct Value;
        friend struct ParserState;
        friend struct BuilderState;
        friend bool WriteSerialized(Value input, char* output, size_t capacity,
            size_t& size, Error& error) noexcept;
    };

    [[nodiscard]] bool Integer(Value value, int64_t& result) noexcept;
    enum class NumberComparison : uint8_t { Token, Integer };
    [[nodiscard]] bool Equal(Value left, Value right,
        NumberComparison numbers = NumberComparison::Token) noexcept;
    [[nodiscard]] bool ValidateSigned(Value value, Error& error) noexcept;

    [[nodiscard]] bool MeasureEscaped(TextView input, size_t& size, Error& error) noexcept;
    [[nodiscard]] bool WriteEscaped(TextView input, char* output, size_t capacity,
        size_t& size, Error& error) noexcept;
    [[nodiscard]] bool MeasureSerialized(Value input, size_t& size, Error& error) noexcept;
    [[nodiscard]] bool WriteSerialized(Value input, char* output, size_t capacity,
        size_t& size, Error& error) noexcept;

#if defined(UVSR_JSON_TEST_HOOKS)
    // failure injection affects only allocations made by this owner on this thread.
    void FailAllocationAfter(size_t successfulAllocations) noexcept;
    void ClearAllocationFailure() noexcept;
#endif
}
