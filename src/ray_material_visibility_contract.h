#ifndef UVSR_RAY_MATERIAL_VISIBILITY_CONTRACT_H
#define UVSR_RAY_MATERIAL_VISIBILITY_CONTRACT_H

// Executable CPU/HLSL contract for deciding whether a triangle candidate
// blocks a visibility or transport ray. Resource addressing and texture loads
// stay in the shader; this contract owns face, domain, transport, alpha-source,
// and cutoff semantics.

#define UVSR_RAY_MATERIAL_COVERAGE_REJECT 0u
#define UVSR_RAY_MATERIAL_COVERAGE_OPAQUE 1u
#define UVSR_RAY_MATERIAL_COVERAGE_ALPHA_TESTED 2u

#define UVSR_RAY_MATERIAL_ALPHA_NONE 0u
#define UVSR_RAY_MATERIAL_ALPHA_OPACITY_TEXTURE 1u
#define UVSR_RAY_MATERIAL_ALPHA_BASE_TEXTURE 2u

#ifdef __cplusplus

#include <algorithm>
#include <cmath>
#include <cstdint>

using RayMaterialContractUint = std::uint32_t;

inline float RayMaterialContractSaturate(float value) noexcept
{
    return std::clamp(value, 0.f, 1.f);
}

inline bool RayMaterialContractIsFinite(float value) noexcept
{
    return std::isfinite(value);
}

#define UVSR_RAY_MATERIAL_INLINE inline

#else

#define RayMaterialContractUint uint

float RayMaterialContractSaturate(float value)
{
    return saturate(value);
}

bool RayMaterialContractIsFinite(float value)
{
    return isfinite(value);
}

#define UVSR_RAY_MATERIAL_INLINE

#endif

struct RayMaterialCoveragePlan
{
    RayMaterialContractUint mode;
    RayMaterialContractUint alphaSource;
};

UVSR_RAY_MATERIAL_INLINE RayMaterialCoveragePlan
    ResolveRayMaterialCoveragePlan(
        bool candidateFrontFace,
        bool doubleSided,
        bool requirePathTransportMaterial,
        bool pathTransportUnsupported,
        bool opaqueDomain,
        bool alphaTestedDomain,
        bool opacityTextureAvailable,
        bool baseAlphaTextureAvailable)
{
    RayMaterialCoveragePlan plan;
    plan.mode = UVSR_RAY_MATERIAL_COVERAGE_REJECT;
    plan.alphaSource = UVSR_RAY_MATERIAL_ALPHA_NONE;

    if ((!candidateFrontFace && !doubleSided) ||
        (requirePathTransportMaterial && pathTransportUnsupported))
    {
        return plan;
    }
    if (opaqueDomain)
    {
        plan.mode = UVSR_RAY_MATERIAL_COVERAGE_OPAQUE;
        return plan;
    }
    if (!alphaTestedDomain)
        return plan;

    plan.mode = UVSR_RAY_MATERIAL_COVERAGE_ALPHA_TESTED;
    if (opacityTextureAvailable)
    {
        plan.alphaSource =
            UVSR_RAY_MATERIAL_ALPHA_OPACITY_TEXTURE;
    }
    else if (baseAlphaTextureAvailable)
    {
        plan.alphaSource = UVSR_RAY_MATERIAL_ALPHA_BASE_TEXTURE;
    }
    return plan;
}

UVSR_RAY_MATERIAL_INLINE bool ResolveRayMaterialCandidateCoverage(
    RayMaterialCoveragePlan plan,
    float materialOpacity,
    float selectedTextureOpacity,
    float alphaCutoff,
    bool requestedTextureAvailable)
{
    if (plan.mode == UVSR_RAY_MATERIAL_COVERAGE_OPAQUE)
        return true;
    if (plan.mode != UVSR_RAY_MATERIAL_COVERAGE_ALPHA_TESTED)
        return false;
    if (plan.alphaSource != UVSR_RAY_MATERIAL_ALPHA_NONE &&
        !requestedTextureAvailable)
    {
        return false;
    }
    const float textureOpacity =
        plan.alphaSource == UVSR_RAY_MATERIAL_ALPHA_NONE
        ? 1.0f
        : selectedTextureOpacity;
    if (!RayMaterialContractIsFinite(materialOpacity) ||
        !RayMaterialContractIsFinite(textureOpacity) ||
        !RayMaterialContractIsFinite(alphaCutoff))
    {
        return false;
    }
    return RayMaterialContractSaturate(
        materialOpacity * textureOpacity) >= alphaCutoff;
}

#ifndef __cplusplus
#undef RayMaterialContractUint
#endif
#undef UVSR_RAY_MATERIAL_INLINE

#endif // UVSR_RAY_MATERIAL_VISIBILITY_CONTRACT_H
