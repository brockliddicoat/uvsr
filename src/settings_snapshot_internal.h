#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string_view>

namespace uvsr::settings_snapshot_detail
{
    inline bool ValidText(std::string_view text) noexcept
    {
        return text.size() <= size_t(PTRDIFF_MAX) &&
            (!text.size() || (text.data() &&
                reinterpret_cast<uintptr_t>(text.data()) <= UINTPTR_MAX - text.size()));
    }
}
