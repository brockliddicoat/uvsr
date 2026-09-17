#pragma once

#include <stdint.h>

namespace uvsr
{
    [[nodiscard]] bool ParseCommandLineInt(const char* text,
        int32_t minimum, int32_t maximum, int32_t& value) noexcept;
}
