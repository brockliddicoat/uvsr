#pragma once

#include <stddef.h>

namespace uvsr
{
    // first-party import storage only. vendor allocation has a separate contract.
    void* ImportAllocate(size_t bytes) noexcept;
}
