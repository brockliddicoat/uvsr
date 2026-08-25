#ifndef UVSR_PBR_LIGHTING_DEBUG_CONTRACT_H
#define UVSR_PBR_LIGHTING_DEBUG_CONTRACT_H

#define UVSR_PBR_LIGHTING_DEBUG_NONE 0u
#define UVSR_PBR_LIGHTING_DEBUG_SHADING_NORMAL 1u
#define UVSR_PBR_LIGHTING_DEBUG_GEOMETRIC_NORMAL 2u
#define UVSR_PBR_LIGHTING_DEBUG_NORMAL_DIFFERENCE 3u
#define UVSR_PBR_LIGHTING_DEBUG_DIFFUSE_ENVIRONMENT 4u
#define UVSR_PBR_LIGHTING_DEBUG_ENVIRONMENT_DIRECTION 5u
#define UVSR_PBR_LIGHTING_DEBUG_PREFILTERED_SPECULAR 6u
#define UVSR_PBR_LIGHTING_DEBUG_ENVIRONMENT_BRDF 7u
#define UVSR_PBR_LIGHTING_DEBUG_FINAL_SPECULAR 8u
#define UVSR_PBR_LIGHTING_DEBUG_COMBINED_ENVIRONMENT 9u
#define UVSR_PBR_LIGHTING_DEBUG_SPECULAR_OCCLUSION 10u
#define UVSR_PBR_LIGHTING_DEBUG_ENVIRONMENT_MIP 11u
#define UVSR_PBR_LIGHTING_DEBUG_SKY_VISIBILITY 12u

#define UVSR_VISIBILITY_DEBUG_FINAL_IMAGE 0u
#define UVSR_VISIBILITY_DEBUG_AMBIENT_VISIBILITY 1u
#define UVSR_VISIBILITY_DEBUG_TRACED_INDIRECT 2u
#define UVSR_VISIBILITY_DEBUG_APPLIED_INDIRECT 3u

#define UVSR_PBR_DEBUG_PRESENT_FINAL 0u
#define UVSR_PBR_DEBUG_PRESENT_LIGHTING 1u
#define UVSR_PBR_DEBUG_PRESENT_VISIBILITY 2u

#ifdef __cplusplus

#include <algorithm>
#include <cmath>
#include <cstdint>

using PbrDebugUint = std::uint32_t;

struct PbrDebugFloat3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

inline bool PbrDebugIsFinite(float value) noexcept
{
    return std::isfinite(value);
}

inline float PbrDebugSaturate(float value) noexcept
{
    return std::clamp(value, 0.f, 1.f);
}

inline PbrDebugFloat3 PbrDebugMakeFloat3(
    float x,
    float y,
    float z) noexcept
{
    return { x, y, z };
}

#define UVSR_PBR_DEBUG_INLINE inline

#else

#define PbrDebugUint uint
#define PbrDebugFloat3 float3

bool PbrDebugIsFinite(float value)
{
    return isfinite(value);
}

float PbrDebugSaturate(float value)
{
    return saturate(value);
}

float3 PbrDebugMakeFloat3(float x, float y, float z)
{
    return float3(x, y, z);
}

#define UVSR_PBR_DEBUG_INLINE

#endif

UVSR_PBR_DEBUG_INLINE bool PbrLightingDebugIsActive(
    PbrDebugUint lightingDebugView)
{
    return lightingDebugView != UVSR_PBR_LIGHTING_DEBUG_NONE;
}

UVSR_PBR_DEBUG_INLINE bool PbrVisibilityDebugIsActive(
    PbrDebugUint visibilityDebugView)
{
    return visibilityDebugView != UVSR_VISIBILITY_DEBUG_FINAL_IMAGE;
}

UVSR_PBR_DEBUG_INLINE bool PbrLightingDebugShowsSkyVisibility(
    PbrDebugUint lightingDebugView)
{
    return lightingDebugView == UVSR_PBR_LIGHTING_DEBUG_SKY_VISIBILITY;
}

UVSR_PBR_DEBUG_INLINE bool PbrNeedsSkyVisibilitySample(
    PbrDebugUint lightingDebugView,
    bool applyToDiffuseIbl,
    bool applyToSpecularIbl)
{
    return PbrLightingDebugShowsSkyVisibility(lightingDebugView) ||
        applyToDiffuseIbl || applyToSpecularIbl;
}

UVSR_PBR_DEBUG_INLINE PbrDebugFloat3 ResolvePbrSkyVisibilityDebugColor(
    float sampledSkyVisibility)
{
    const float visibility = PbrDebugIsFinite(sampledSkyVisibility)
        ? PbrDebugSaturate(sampledSkyVisibility)
        : 1.0f;
    return PbrDebugMakeFloat3(visibility, visibility, visibility);
}

UVSR_PBR_DEBUG_INLINE PbrDebugUint ResolvePbrDebugPresentation(
    PbrDebugUint lightingDebugView,
    PbrDebugUint visibilityDebugView)
{
    if (PbrVisibilityDebugIsActive(visibilityDebugView))
        return UVSR_PBR_DEBUG_PRESENT_VISIBILITY;
    if (PbrLightingDebugIsActive(lightingDebugView))
        return UVSR_PBR_DEBUG_PRESENT_LIGHTING;
    return UVSR_PBR_DEBUG_PRESENT_FINAL;
}

UVSR_PBR_DEBUG_INLINE bool PbrDebugUsesBlackBackground(
    PbrDebugUint lightingDebugView,
    PbrDebugUint visibilityDebugView)
{
    return ResolvePbrDebugPresentation(
        lightingDebugView,
        visibilityDebugView) != UVSR_PBR_DEBUG_PRESENT_FINAL;
}

#ifndef __cplusplus
#undef PbrDebugUint
#undef PbrDebugFloat3
#endif
#undef UVSR_PBR_DEBUG_INLINE

#endif // UVSR_PBR_LIGHTING_DEBUG_CONTRACT_H
