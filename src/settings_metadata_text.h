#pragma once

#include <stddef.h>
#include <string_view>

namespace uvsr
{
    // catalog metadata is fixed at build time. the catalog proves that every
    // current value fits; synthetic or invalid definitions report failure.
    struct SettingsMetadataText
    {
        static constexpr size_t Capacity = 256;
        char bytes[Capacity]{};
        size_t length = 0;
        bool valid = true;
        [[nodiscard]] bool IsValid() const noexcept { return valid; }
        [[nodiscard]] const char* Data() const noexcept { return bytes; }
        [[nodiscard]] size_t Size() const noexcept { return length; }
        [[nodiscard]] std::string_view View() const noexcept { return {bytes, length}; }
    };
}
