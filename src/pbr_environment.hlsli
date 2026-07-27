#ifndef UVSR_PBR_ENVIRONMENT_HLSLI
#define UVSR_PBR_ENVIRONMENT_HLSLI

#include "pbr.hlsli"
#include "image_based_lighting_shared.h"

struct PbrPreparedEnvironment
{
    float3 reflectionDirection;
    float3 diffuseWeight;
    float3 specularF0;
    float perceptualRoughness;
    float noV;
    float specularMip;
    float geometricHorizon;
    float valid;
};

PbrPreparedEnvironment PreparePbrEnvironment(
    PbrMaterialParameters material,
    PbrSurfaceInteraction surface,
    float specularMipLevels)
{
    PbrPreparedEnvironment result = (PbrPreparedEnvironment)0;
    PbrPreparedSurface preparedSurface = PreparePbrSurface(surface);
    if (preparedSurface.geometricNoV <= UVSR_MIN_COSINE ||
        preparedSurface.shadingNoV <= UVSR_MIN_COSINE)
    {
        return result;
    }

    result.valid = 1.0f;
    result.noV = saturate(preparedSurface.shadingNoV);
    result.perceptualRoughness = sqrt(
        PerceptualRoughnessToAlpha(
            material.perceptualRoughness));
    result.specularF0 = lerp(
        material.dielectricF0.xxx,
        material.baseColor,
        material.metalness);

    // The roughness-aware Schlick term is the conventional split-sum diffuse
    // energy-sharing approximation. It keeps diffuse and specular responses
    // derived from one environment from double-counting grazing energy.
    float3 grazingFresnel = max(
        (1.0f - result.perceptualRoughness).xxx,
        result.specularF0);
    float oneMinusNoV = 1.0f - result.noV;
    float fresnelFactor = oneMinusNoV * oneMinusNoV;
    fresnelFactor *= fresnelFactor * oneMinusNoV;
    float3 fresnel = result.specularF0 +
        (grazingFresnel - result.specularF0) *
        fresnelFactor;
    result.diffuseWeight =
        material.baseColor *
        (1.0f - material.metalness) *
        (1.0f - fresnel);

    result.reflectionDirection = reflect(
        -preparedSurface.viewDirection,
        preparedSurface.shadingNormal);
    result.specularMip = ImageBasedLightingReceiverMip(
        result.perceptualRoughness,
        specularMipLevels);

    // A perturbed shading normal can point its reflected ray below the true
    // geometric surface. Fade only that invalid lobe instead of allowing
    // unrelated back-side environment energy to leak through the material.
    float horizon = saturate(
        1.0f + dot(
            result.reflectionDirection,
            preparedSurface.geometricNormal));
    result.geometricHorizon = horizon * horizon;
    return result;
}

float PbrEnvironmentSpecularOcclusion(
    float noV,
    float ambientOcclusion,
    float perceptualRoughness)
{
    return ImageBasedLightingSpecularOcclusion(
        noV,
        ambientOcclusion,
        perceptualRoughness);
}

float3 EvaluatePbrEnvironmentDiffuse(
    PbrPreparedEnvironment environment,
    float3 diffuseResponse,
    float ambientOcclusion)
{
    return environment.valid *
        max(diffuseResponse, 0.0f) *
        environment.diffuseWeight *
        saturate(ambientOcclusion);
}

float3 EvaluatePbrEnvironmentSpecular(
    PbrPreparedEnvironment environment,
    float3 prefilteredRadiance,
    float2 environmentBrdf,
    float ambientOcclusion)
{
    const float3 splitSum = environment.specularF0 *
        environmentBrdf.x + environmentBrdf.y;
    const float specularOcclusion =
        PbrEnvironmentSpecularOcclusion(
            environment.noV,
            ambientOcclusion,
            environment.perceptualRoughness);
    return environment.valid *
        max(prefilteredRadiance, 0.0f) *
        max(splitSum, 0.0f) *
        (environment.geometricHorizon * specularOcclusion);
}

#endif // UVSR_PBR_ENVIRONMENT_HLSLI
