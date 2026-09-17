#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string_view>

namespace uvsr::catalog_path
{
    enum class Encoding : uint8_t { Filesystem, Utf8 };
    enum class ErrorCode : uint8_t { None, InvalidInput, Capacity, Conversion };
    struct Error
    {
        ErrorCode code = ErrorCode::None;
        uint32_t nativeCode = 0;
    };

    // these lexical helpers borrow counted text, including interior NULs. they
    // do not validate an OS filename or access files. buffers include a terminator.
    struct ConversionPlan { size_t size = 0; uint32_t codePage = 0; };
    // measurement freezes the process code page for the following write. input
    // stays unchanged between the two calls; a plan belongs to that exact input.
    [[nodiscard]] bool MeasureDecode(std::string_view input, Encoding encoding,
        ConversionPlan& plan, Error& error) noexcept;
    [[nodiscard]] bool Decode(std::string_view input, const ConversionPlan& plan,
        wchar_t* output, size_t capacity, size_t& size, Error& error) noexcept;
    [[nodiscard]] bool MeasureEncode(std::wstring_view input, ConversionPlan& plan, Error& error,
        Encoding encoding = Encoding::Filesystem) noexcept;
    [[nodiscard]] bool Encode(std::wstring_view input, const ConversionPlan& plan, char* output,
        size_t capacity, size_t& size, Error& error) noexcept;

    [[nodiscard]] size_t RootName(std::wstring_view text) noexcept;
    [[nodiscard]] size_t RelativeStart(std::wstring_view text) noexcept;
    [[nodiscard]] size_t FilenameStart(std::wstring_view text) noexcept;
    [[nodiscard]] size_t ParentLength(std::wstring_view text) noexcept;
    // normalization is in place and never grows a nonempty path. input storage
    // contains size live units plus its terminator. generic conversion is separate.
    [[nodiscard]] size_t Normalize(wchar_t* text, size_t size) noexcept;
    void MakeGeneric(wchar_t* text, size_t size) noexcept;

    struct JoinPlan
    {
        size_t base = 0;
        size_t first = 0;
        size_t size = 0;
        bool separator = false;
    };
    [[nodiscard]] bool PlanJoin(std::wstring_view left, std::wstring_view right,
        JoinPlan& plan, Error& error) noexcept;
    // output may be left's storage. right must not overlap output.
    [[nodiscard]] bool Join(std::wstring_view left, std::wstring_view right,
        wchar_t* output, size_t capacity, size_t& size, Error& error) noexcept;

    // one component at a time, including root-name, root-directory and the final
    // empty component after a nonroot trailing separator. cursor starts at zero.
    [[nodiscard]] bool Next(std::wstring_view path, size_t& cursor,
        std::wstring_view& component) noexcept;
}
