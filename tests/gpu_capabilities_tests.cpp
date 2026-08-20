#include <cassert>

#include "gpu_capabilities.h"

int main()
{
    static_assert(uvsr::MinimumShaderModel == 0x65u);
    static_assert(!uvsr::SupportsRequiredShaderModel(0x60u));
    static_assert(!uvsr::SupportsRequiredShaderModel(0x64u));
    static_assert(uvsr::SupportsRequiredShaderModel(0x65u));
    static_assert(uvsr::SupportsRequiredShaderModel(0x66u));

    assert(uvsr::SupportsRequiredShaderModel(uvsr::MinimumShaderModel));
    return 0;
}
