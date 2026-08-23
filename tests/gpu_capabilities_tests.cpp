#include <cassert>

#include "gpu_capabilities.h"

int main()
{
    static_assert(uvsr::MinimumShaderModel == 0x65u);
    static_assert(!uvsr::SupportsRequiredShaderModel(0x60u));
    static_assert(!uvsr::SupportsRequiredShaderModel(0x64u));
    static_assert(uvsr::SupportsRequiredShaderModel(0x65u));
    static_assert(uvsr::SupportsRequiredShaderModel(0x66u));
    static_assert(uvsr::MinimumD3DFeatureLevel == 0xb000u);
    static_assert(!uvsr::SupportsRequiredFeatureLevel(0xa100u));
    static_assert(uvsr::SupportsRequiredFeatureLevel(0xb000u));
    static_assert(uvsr::SupportsRequiredFeatureLevel(0xc200u));
    static_assert(!uvsr::SupportsBindlessResourceTables(1u));
    static_assert(uvsr::SupportsBindlessResourceTables(2u));
    static_assert(uvsr::SupportsBindlessResourceTables(3u));
    static_assert(!uvsr::SupportsOptionalRayQueryRendering(1u, 11u));
    static_assert(!uvsr::SupportsOptionalRayQueryRendering(2u, 10u));
    static_assert(uvsr::SupportsOptionalRayQueryRendering(2u, 11u));

    assert(uvsr::SupportsRequiredShaderModel(uvsr::MinimumShaderModel));
    assert(uvsr::SupportsRequiredFeatureLevel(
        uvsr::MinimumD3DFeatureLevel));
    assert(uvsr::SupportsOptionalRayQueryRendering(
        uvsr::MinimumBindlessResourceBindingTier,
        uvsr::MinimumInlineRayTracingTier));
    return 0;
}
