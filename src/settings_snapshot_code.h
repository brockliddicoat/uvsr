#pragma once

#include <stddef.h>

namespace uvsr
{
    // diagnostics contain fixed text and, at most, two four-digit versions.
    struct SettingsSnapshotCodeError
    {
        char text[160]{};
    };

    [[nodiscard]] bool ValidateSettingsSnapshotLoadCode(const char* code,
        size_t length, SettingsSnapshotCodeError& error) noexcept;
}
