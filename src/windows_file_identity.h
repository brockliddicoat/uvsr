#pragma once

#include <stdint.h>

namespace uvsr
{
    struct WindowsSameFileResult
    {
        bool equivalent = false;
        uint32_t nativeCode = 0;
        uint32_t cleanupCode = 0;
    };

    // follows reparse targets and keeps both identities live until comparison.
    // false reports a query or cleanup failure; successful mismatch is true.
    [[nodiscard]] bool QueryWindowsSameFile(const wchar_t* left, const wchar_t* right,
        WindowsSameFileResult& result) noexcept;
}
