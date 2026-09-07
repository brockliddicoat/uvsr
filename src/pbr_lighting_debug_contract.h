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



#include "shader_math.h"

UVSR_SHADER_INLINE bool PbrLightingDebugIsActive(
    ShaderUint lightingDebugView)
{
    return lightingDebugView != UVSR_PBR_LIGHTING_DEBUG_NONE;
}

UVSR_SHADER_INLINE bool PbrLightingDebugShowsSkyVisibility(
    ShaderUint lightingDebugView)
{
    return lightingDebugView == UVSR_PBR_LIGHTING_DEBUG_SKY_VISIBILITY;
}

UVSR_SHADER_INLINE bool PbrNeedsSkyVisibilitySample(
    ShaderUint lightingDebugView,
    bool applyToDiffuseIbl,
    bool applyToSpecularIbl)
{
    return PbrLightingDebugShowsSkyVisibility(lightingDebugView) ||
        applyToDiffuseIbl || applyToSpecularIbl;
}

UVSR_SHADER_INLINE ShaderFloat3 ResolvePbrSkyVisibilityDebugColor(
    float sampledSkyVisibility)
{
    const float visibility = ShaderIsFinite(sampledSkyVisibility)
        ? ShaderSaturate(sampledSkyVisibility)
        : 1.0f;
    return ShaderMakeFloat3(visibility, visibility, visibility);
}

#endif // UVSR_PBR_LIGHTING_DEBUG_CONTRACT_H
