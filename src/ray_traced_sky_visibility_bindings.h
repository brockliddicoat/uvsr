#ifndef UVSR_RAY_TRACED_SKY_VISIBILITY_BINDINGS_H
#define UVSR_RAY_TRACED_SKY_VISIBILITY_BINDINGS_H

#include "ray_traced_material_bindings.h"

#ifdef __cplusplus

#include <array>
#include <cstdint>

namespace uvsr
{
    inline constexpr std::uint32_t SkyVisibilityConstantBufferSlot = 0u;
    inline constexpr std::uint32_t SkyVisibilityWorldTlasSlot = 0u;
    inline constexpr std::uint32_t SkyVisibilityDepthSlot = 1u;
    inline constexpr std::uint32_t SkyVisibilityMaterialSlot = 2u;
    inline constexpr std::uint32_t SkyVisibilityNormalsSlot = 3u;
    inline constexpr std::uint32_t SkyVisibilityNoiseSlot = 4u;
    inline constexpr std::uint32_t SkyVisibilityAttemptMaskSlot = 5u;
    inline constexpr std::uint32_t SkyVisibilityOutputSlot = 0u;
    inline constexpr std::uint32_t SkyVisibilityClosestOutputSlot = 1u;
    inline constexpr std::uint32_t SkyVisibilityHitDistanceOutputSlot = 2u;

    inline constexpr std::array<std::uint32_t, 5>
        SkyVisibilityGBufferAndNoiseSlots = {
            SkyVisibilityDepthSlot,
            SkyVisibilityMaterialSlot,
            SkyVisibilityNormalsSlot,
            SkyVisibilityNoiseSlot,
            SkyVisibilityAttemptMaskSlot
        };

    struct RayTracedSkyVisibilityBindingIdentity
    {
        const void* worldTlas = nullptr;
        const void* depth = nullptr;
        const void* material = nullptr;
        const void* normals = nullptr;
        const void* geometryBuffer = nullptr;
        const void* materialBuffer = nullptr;
        const void* geometryIndexMap = nullptr;
        const void* descriptorTable = nullptr;
        const void* noiseTexture = nullptr;
        const void* attemptMask = nullptr;

        [[nodiscard]] constexpr bool operator==(
            const RayTracedSkyVisibilityBindingIdentity& other) const noexcept
        {
            return worldTlas == other.worldTlas && depth == other.depth &&
                material == other.material && normals == other.normals &&
                geometryBuffer == other.geometryBuffer &&
                materialBuffer == other.materialBuffer &&
                geometryIndexMap == other.geometryIndexMap &&
                descriptorTable == other.descriptorTable &&
                noiseTexture == other.noiseTexture &&
                attemptMask == other.attemptMask;
        }

        [[nodiscard]] constexpr bool operator!=(
            const RayTracedSkyVisibilityBindingIdentity& other) const noexcept
        {
            return !(*this == other);
        }
    };
}

#else

#define UVSR_SKY_VISIBILITY_CONSTANT_BUFFER_REGISTER b0
#define UVSR_SKY_VISIBILITY_WORLD_TLAS_REGISTER t0
#define UVSR_SKY_VISIBILITY_DEPTH_REGISTER t1
#define UVSR_SKY_VISIBILITY_MATERIAL_REGISTER t2
#define UVSR_SKY_VISIBILITY_NORMALS_REGISTER t3
#define UVSR_SKY_VISIBILITY_NOISE_REGISTER t4
#define UVSR_SKY_VISIBILITY_ATTEMPT_MASK_REGISTER t5
#define UVSR_SKY_VISIBILITY_OUTPUT_REGISTER u0
#define UVSR_SKY_VISIBILITY_CLOSEST_OUTPUT_REGISTER u1
#define UVSR_SKY_VISIBILITY_HIT_DISTANCE_OUTPUT_REGISTER u2

#endif

#endif
