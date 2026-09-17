#pragma once

#include <stddef.h>
#include <stdint.h>

namespace uvsr::shader_blob
{
    struct Constant
    {
        const char* name;
        const char* value;
    };

    // names and values borrow NUL-terminated strings for this call. success
    // borrows bytecode from blob; failure preserves both output arguments.
    [[nodiscard]] bool find_permutation(const void* blob, size_t blobSize,
        const Constant* constants, uint32_t constantCount,
        const void** binary, size_t* binarySize) noexcept;
}
