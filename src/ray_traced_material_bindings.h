#ifndef UVSR_RAY_TRACED_MATERIAL_BINDINGS_H
#define UVSR_RAY_TRACED_MATERIAL_BINDINGS_H

#ifdef __cplusplus

#include <cstdint>

namespace uvsr
{
    inline constexpr std::uint32_t RayMaterialGeometrySlot = 10u;
    inline constexpr std::uint32_t RayMaterialConstantsSlot = 11u;
    inline constexpr std::uint32_t RayMaterialGeometryIndexSlot = 12u;
    inline constexpr std::uint32_t RayMaterialSamplerSlot = 0u;
    inline constexpr std::uint32_t RayMaterialBufferRegisterSpace = 1u;
    inline constexpr std::uint32_t RayMaterialTextureRegisterSpace = 2u;
}

#else

#define UVSR_RAY_MATERIAL_GEOMETRY_REGISTER t10
#define UVSR_RAY_MATERIAL_CONSTANTS_REGISTER t11
#define UVSR_RAY_MATERIAL_GEOMETRY_INDEX_REGISTER t12
#define UVSR_RAY_MATERIAL_SAMPLER_REGISTER s0
#define UVSR_RAY_MATERIAL_BUFFER_REGISTER t0
#define UVSR_RAY_MATERIAL_BUFFER_SPACE space1
#define UVSR_RAY_MATERIAL_TEXTURE_REGISTER t0
#define UVSR_RAY_MATERIAL_TEXTURE_SPACE space2

#endif

#endif
