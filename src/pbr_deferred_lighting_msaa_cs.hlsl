#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "pbr_deferred_lighting_bindings.h"
#include "pbr_deferred_lighting_cb.h"
#include "pbr_environment.hlsli"
#include "pbr_gbuffer.hlsli"
#include "pbr_lighting.hlsli"
#include "renderer_environment_bindings.h"

#ifndef PBR_DEFERRED_MSAA_SAMPLES
#error PBR_DEFERRED_MSAA_SAMPLES must be a static 2, 4, 8, or 16.
#endif

#ifndef PBR_DEFERRED_MSAA_VISIBILITY
#define PBR_DEFERRED_MSAA_VISIBILITY 0
#endif

#if PBR_DEFERRED_MSAA_SAMPLES != 2 && \
    PBR_DEFERRED_MSAA_SAMPLES != 4 && \
    PBR_DEFERRED_MSAA_SAMPLES != 8 && \
    PBR_DEFERRED_MSAA_SAMPLES != 16
#error Unsupported PBR deferred MSAA sample count.
#endif

cbuffer c_Deferred : register(b0)
{
    PbrDeferredLightingConstants g_PbrDeferred;
};

#define g_Deferred g_PbrDeferred.deferred

Texture2DArray t_ShadowMapArray : register(t0);
TextureCubeArray t_DiffuseEnvironment :
    register(UVSR_PBR_DEFERRED_DIFFUSE_ENVIRONMENT_REGISTER);
TextureCubeArray t_SpecularEnvironment :
    register(UVSR_PBR_DEFERRED_SPECULAR_ENVIRONMENT_REGISTER);
Texture2D t_EnvironmentBrdf :
    register(UVSR_PBR_DEFERRED_ENVIRONMENT_BRDF_REGISTER);

SamplerComparisonState s_ShadowSamplerComparison : register(s1);
SamplerState s_DiffuseEnvironmentSampler : register(s2);
SamplerState s_EnvironmentBrdfSampler : register(s3);

Texture2DMS<float, PBR_DEFERRED_MSAA_SAMPLES>
    t_GBufferDepth : register(t8);
Texture2DMS<float4, PBR_DEFERRED_MSAA_SAMPLES>
    t_GBuffer0 : register(t9);
Texture2DMS<float4, PBR_DEFERRED_MSAA_SAMPLES>
    t_GBuffer1 : register(t10);
Texture2DMS<float4, PBR_DEFERRED_MSAA_SAMPLES>
    t_GBuffer2 : register(t11);
Texture2DMS<float4, PBR_DEFERRED_MSAA_SAMPLES>
    t_GBuffer3 : register(t12);
Texture2DMS<float, PBR_DEFERRED_MSAA_SAMPLES>
    t_MaterialAmbientOcclusion : register(t14);

// HdrColor contains the environment/IBL background and was resolved before
// this dispatch. Covered samples are still zero because the background depth
// test failed; the resolved value therefore already represents the exact
// uncovered-sample environment contribution divided by the sample count.
Texture2D t_ResolvedBackground : register(t17);
#if PBR_DEFERRED_MSAA_VISIBILITY
Texture2D t_VisibilityBaseLighting : register(t18);
Texture2D t_VisibilityComposite : register(t19);
#endif
Texture2DArray<float> t_FlashlightVisibility :
    register(UVSR_PBR_FLASHLIGHT_VISIBILITY_REGISTER);
Texture2DArray<float> t_SunVisibility :
    register(UVSR_PBR_SUN_VISIBILITY_REGISTER);
Texture2DArray<float> t_SkyVisibility :
    register(UVSR_PBR_SKY_VISIBILITY_REGISTER);
Texture2D<float> t_FlashlightRawClosestVisibility :
    register(UVSR_PBR_FLASHLIGHT_RAW_CLOSEST_REGISTER);
Texture2D<float> t_FlashlightDenoisedClosestVisibility :
    register(UVSR_PBR_FLASHLIGHT_DENOISED_CLOSEST_REGISTER);
Texture2D<float> t_SkyRawClosestVisibility :
    register(UVSR_PBR_SKY_RAW_CLOSEST_REGISTER);
Texture2D<float> t_SkyDenoisedClosestVisibility :
    register(UVSR_PBR_SKY_DENOISED_CLOSEST_REGISTER);
Texture2D<float> t_SunRawClosestVisibility :
    register(UVSR_PBR_SUN_RAW_CLOSEST_REGISTER);
Texture2D<float> t_SunDenoisedClosestVisibility :
    register(UVSR_PBR_SUN_DENOISED_CLOSEST_REGISTER);

RWTexture2D<float4> u_Output : register(u0);

float GetRandom(float2 position)
{
    int x = int(position.x) & 3;
    int y = int(position.y) & 3;
    return g_Deferred.noisePattern[y][x];
}

float3 DecodeDirectLightVisibility(float encoded)
{
    if (!isfinite(encoded))
        return 1.0f;
    return saturate(encoded).xxx;
}

float ApplyClosestSurfaceDenoisingCorrection(
    float receiverSample,
    float rawClosest,
    float denoisedClosest)
{
    receiverSample = isfinite(receiverSample)
        ? saturate(receiverSample)
        : 1.0f;
    rawClosest = isfinite(rawClosest) ? saturate(rawClosest) : 1.0f;
    denoisedClosest = isfinite(denoisedClosest)
        ? saturate(denoisedClosest)
        : rawClosest;
    if (abs(rawClosest - denoisedClosest) <= 1e-6f)
        return receiverSample;

    if (receiverSample >= rawClosest)
    {
        const float upperRange = max(1.0f - rawClosest, 1e-6f);
        return saturate(denoisedClosest +
            (receiverSample - rawClosest) *
            (1.0f - denoisedClosest) / upperRange);
    }
    const float lowerRange = max(rawClosest, 1e-6f);
    return saturate(denoisedClosest -
        (rawClosest - receiverSample) *
        denoisedClosest / lowerRange);
}

float3 GetDirectLightVisibility(
    uint lightIndex,
    int2 pixelPosition,
    uint sampleIndex)
{
    float3 visibility = 1.0f;
    if (int(lightIndex) ==
        g_PbrDeferred.directVisibilityLightIndices.x)
    {
        const uint2 pixel = uint2(pixelPosition);
        const float corrected = ApplyClosestSurfaceDenoisingCorrection(
            t_FlashlightVisibility[uint3(pixel, sampleIndex)],
            t_FlashlightRawClosestVisibility[pixel],
            t_FlashlightDenoisedClosestVisibility[pixel]);
        visibility = min(
            visibility,
            DecodeDirectLightVisibility(corrected));
    }
    if (int(lightIndex) ==
        g_PbrDeferred.directVisibilityLightIndices.y)
    {
        const uint2 pixel = uint2(pixelPosition);
        const float corrected = ApplyClosestSurfaceDenoisingCorrection(
            t_SunVisibility[uint3(pixel, sampleIndex)],
            t_SunRawClosestVisibility[pixel],
            t_SunDenoisedClosestVisibility[pixel]);
        visibility = min(
            visibility,
            DecodeDirectLightVisibility(corrected));
    }
    return visibility;
}

float EvaluateLightVisibility(
    LightConstants light,
    float3 surfaceWorldPosition,
    float2 sinCosRotation,
    float initialVisibility)
{
    float visibility = initialVisibility;

    float2 combinedCascadeVisibility = 0.0f;
    [loop]
    for (int cascade = 0; cascade < 4; ++cascade)
    {
        if (light.shadowCascades[cascade] < 0)
            break;

        float2 cascadeVisibility = EvaluateShadowPoisson(
            t_ShadowMapArray,
            s_ShadowSamplerComparison,
            g_Deferred.shadows[light.shadowCascades[cascade]],
            surfaceWorldPosition,
            sinCosRotation,
            3.0f);
        combinedCascadeVisibility = saturate(
            combinedCascadeVisibility +
            cascadeVisibility *
                (1.0001f - combinedCascadeVisibility.y));
        if (combinedCascadeVisibility.y == 1.0f)
            break;
    }

    combinedCascadeVisibility.x +=
        (1.0f - combinedCascadeVisibility.y) *
        light.outOfBoundsShadow;
    visibility *= combinedCascadeVisibility.x;
    if (!(visibility > 0.0f))
        return 0.0f;

    [loop]
    for (int object = 0; object < 4; ++object)
    {
        if (light.perObjectShadows[object] < 0)
            continue;

        float2 objectVisibility = EvaluateShadowPoisson(
            t_ShadowMapArray,
            s_ShadowSamplerComparison,
            g_Deferred.shadows[light.perObjectShadows[object]],
            surfaceWorldPosition,
            sinCosRotation,
            3.0f);
        visibility *= saturate(
            objectVisibility.x +
            (1.0f - objectVisibility.y));
        if (!(visibility > 0.0f))
            break;
    }

    return saturate(visibility);
}

bool ShadeDeferredSample(
    int2 pixelPosition,
    int2 dispatchPosition,
    uint sampleIndex,
    out float3 finalLinearHdr)
{
    finalLinearHdr = 0.0f;
    float4 normalChannels =
        t_GBuffer2.Load(pixelPosition, sampleIndex);
    const float sampleDepth =
        t_GBufferDepth.Load(pixelPosition, sampleIndex);
    if (!isfinite(sampleDepth) || sampleDepth <= 0.0f ||
        !(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
        return false;

    float4 gbufferChannels[4];
    gbufferChannels[0] =
        t_GBuffer0.Load(pixelPosition, sampleIndex);
    gbufferChannels[1] =
        t_GBuffer1.Load(pixelPosition, sampleIndex);
    gbufferChannels[2] = normalChannels;
    gbufferChannels[3] =
        t_GBuffer3.Load(pixelPosition, sampleIndex);
    PbrGBufferData gbuffer = DecodePbrGBuffer(
        gbufferChannels,
        t_MaterialAmbientOcclusion.Load(
            pixelPosition,
            sampleIndex));

    // G-buffer attributes use ordinary pixel-frequency interpolation, while
    // coverage and depth remain sample-frequency. Preserve the established
    // center-interpolated attribute convention here; exact sample-position
    // reconstruction requires a coordinated sample-frequency G-buffer path.
    float3 surfaceWorldPosition = ReconstructWorldPosition(
        g_Deferred.view,
        float2(pixelPosition) + 0.5f,
        sampleDepth);
    float3 viewIncident = GetIncidentVector(
        g_Deferred.view.cameraDirectionOrPosition,
        surfaceWorldPosition);
    PbrSurfaceInteraction surface;
    surface.position = surfaceWorldPosition;
    surface.shadingNormal = gbuffer.shadingNormal;
    surface.geometricNormal = gbuffer.geometricNormal;
    surface.viewDirection = -viewIncident;
    PbrPreparedSurface preparedSurface =
        PreparePbrSurface(surface);
    PbrPreparedMaterial preparedMaterial =
        PreparePbrMaterial(gbuffer.material);

    LightProbeConstants environmentProbe =
        (LightProbeConstants)0;
    if (g_Deferred.numLightProbes > 0u)
        environmentProbe = g_Deferred.lightProbes[0];
    PbrPreparedEnvironment preparedEnvironment =
        PreparePbrEnvironment(
            gbuffer.material,
            surface,
            environmentProbe.mipLevels);

    float3 environmentDiffuseResponse = 0.0f;
    float3 prefilteredEnvironment = 0.0f;
    float2 environmentBrdf = 0.0f;
    float3 environmentDiffuse = 0.0f;
    float3 environmentSpecular = 0.0f;
    float skyVisibility = 1.0f;
    const bool showSkyVisibility =
        PbrLightingDebugShowsSkyVisibility(
            g_PbrDeferred.lightingDebugView);
    const bool applySkyVisibilityToDiffuseIbl =
        SkyVisibilityAppliesToDiffuseIbl(
            g_PbrDeferred.skyVisibilityApplication);
    const bool applySkyVisibilityToSpecularIbl =
        SkyVisibilityAppliesToSpecularIbl(
            g_PbrDeferred.skyVisibilityApplication);
    if (PbrNeedsSkyVisibilitySample(
        g_PbrDeferred.lightingDebugView,
        applySkyVisibilityToDiffuseIbl,
        applySkyVisibilityToSpecularIbl))
    {
        const uint2 pixel = uint2(pixelPosition);
        const float sampledSkyVisibility =
            ApplyClosestSurfaceDenoisingCorrection(
                t_SkyVisibility[uint3(pixel, sampleIndex)],
                t_SkyRawClosestVisibility[pixel],
                t_SkyDenoisedClosestVisibility[pixel]);
        skyVisibility = isfinite(sampledSkyVisibility)
            ? saturate(sampledSkyVisibility)
            : 1.0f;
    }
    if (g_Deferred.numLightProbes > 0u)
    {
        const bool needDiffuseEnvironment =
            g_PbrDeferred.separateIndirect == 0 ||
            g_PbrDeferred.lightingDebugView ==
                UVSR_PBR_LIGHTING_DEBUG_DIFFUSE_ENVIRONMENT ||
            g_PbrDeferred.lightingDebugView ==
                UVSR_PBR_LIGHTING_DEBUG_COMBINED_ENVIRONMENT;
        const bool needSpecularEnvironment =
            g_PbrDeferred.separateIndirect == 0 ||
            (g_PbrDeferred.lightingDebugView >=
                    UVSR_PBR_LIGHTING_DEBUG_PREFILTERED_SPECULAR &&
                g_PbrDeferred.lightingDebugView <=
                    UVSR_PBR_LIGHTING_DEBUG_COMBINED_ENVIRONMENT);
        if (needDiffuseEnvironment &&
            environmentProbe.diffuseScale > 0.0f)
        {
            environmentDiffuseResponse =
                t_DiffuseEnvironment.SampleLevel(
                    s_DiffuseEnvironmentSampler,
                    float4(
                        preparedSurface.shadingNormal,
                        environmentProbe.diffuseArrayIndex),
                    0.0f).rgb *
                environmentProbe.diffuseScale;
            if (any(!isfinite(environmentDiffuseResponse)))
                environmentDiffuseResponse = 0.0f;
            environmentDiffuse = EvaluatePbrEnvironmentDiffuse(
                preparedEnvironment,
                environmentDiffuseResponse,
                gbuffer.ambientOcclusion);
            if (applySkyVisibilityToDiffuseIbl)
                environmentDiffuse *= skyVisibility;
        }

        if (needSpecularEnvironment &&
            environmentProbe.specularScale > 0.0f &&
            preparedEnvironment.valid > 0.0f)
        {
            prefilteredEnvironment =
                t_SpecularEnvironment.SampleLevel(
                    s_DiffuseEnvironmentSampler,
                    float4(
                        preparedEnvironment.reflectionDirection,
                        environmentProbe.specularArrayIndex),
                    preparedEnvironment.specularMip).rgb *
                environmentProbe.specularScale;
            environmentBrdf = t_EnvironmentBrdf.SampleLevel(
                s_EnvironmentBrdfSampler,
                float2(
                    preparedEnvironment.noV,
                    preparedEnvironment.perceptualRoughness),
                0.0f).xy;
            if (any(!isfinite(prefilteredEnvironment)))
                prefilteredEnvironment = 0.0f;
            if (any(!isfinite(environmentBrdf)))
                environmentBrdf = 0.0f;
            environmentSpecular = EvaluatePbrEnvironmentSpecular(
                preparedEnvironment,
                prefilteredEnvironment,
                environmentBrdf,
                gbuffer.ambientOcclusion);
            if (applySkyVisibilityToSpecularIbl)
                environmentSpecular *= skyVisibility;
        }
    }

    if (PbrLightingDebugIsActive(
        g_PbrDeferred.lightingDebugView))
    {
        float3 debugColor = 0.0f;
        if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_SHADING_NORMAL)
        {
            debugColor = gbuffer.shadingNormal * 0.5f + 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_GEOMETRIC_NORMAL)
        {
            debugColor = gbuffer.geometricNormal * 0.5f + 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_NORMAL_DIFFERENCE)
        {
            float angularDifference = saturate(
                1.0f - dot(
                    gbuffer.shadingNormal,
                    gbuffer.geometricNormal));
            debugColor = float3(
                angularDifference,
                0.0f,
                1.0f - angularDifference);
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_DIFFUSE_ENVIRONMENT)
        {
            debugColor = environmentDiffuseResponse;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_ENVIRONMENT_DIRECTION)
        {
            float cardinalResponse =
                1.0f + 0.5f * gbuffer.shadingNormal.x;
            debugColor = cardinalResponse * 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_PREFILTERED_SPECULAR)
        {
            debugColor = prefilteredEnvironment;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_ENVIRONMENT_BRDF)
        {
            debugColor = float3(
                environmentBrdf.x,
                environmentBrdf.y,
                0.0f);
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_FINAL_SPECULAR)
        {
            debugColor = environmentSpecular;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_COMBINED_ENVIRONMENT)
        {
            debugColor =
                environmentDiffuse + environmentSpecular;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_SPECULAR_OCCLUSION)
        {
            const float specularOcclusion =
                PbrEnvironmentSpecularOcclusion(
                    preparedEnvironment.noV,
                    gbuffer.ambientOcclusion,
                    preparedEnvironment.perceptualRoughness);
            debugColor = specularOcclusion.xxx;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_ENVIRONMENT_MIP)
        {
            const float normalizedMip =
                environmentProbe.mipLevels > 1.0f
                    ? preparedEnvironment.specularMip /
                        (environmentProbe.mipLevels - 1.0f)
                    : 0.0f;
            debugColor = normalizedMip.xxx;
        }
        else if (showSkyVisibility)
        {
            debugColor =
                ResolvePbrSkyVisibilityDebugColor(skyVisibility);
        }

        finalLinearHdr = min(max(debugColor, 0.0f), 65504.0f);
        return true;
    }

    float3 directDiffuse = 0.0f;
    float3 directSpecular = 0.0f;
    if (g_Deferred.numLights > 0u)
    {
        float angle = GetRandom(
            dispatchPosition +
            float2(g_Deferred.randomOffset.x, 0.0f));
        float2 sinCosRotation =
            float2(sin(angle), cos(angle));
        [loop]
        for (uint lightIndex = 0;
            lightIndex < g_Deferred.numLights;
            ++lightIndex)
        {
            LightConstants light =
                g_Deferred.lights[lightIndex];
            float3 directModulation =
                GetDirectLightVisibility(
                lightIndex,
                pixelPosition,
                sampleIndex);
            if (!any(directModulation > 0.0f))
                continue;

            PbrLightSample lightSample = SamplePbrLight(
                light,
                surfaceWorldPosition,
                1.0f,
                int(lightIndex) == g_PbrDeferred.flashlightLightIndex,
                g_PbrDeferred.flashlightBeamProfile);
            if (!HasPositiveFinitePbrSignal(
                    lightSample.incidentRadiance,
                    1.0f))
                continue;
            if (!CanEvaluatePbrDirectSurfacePrepared(
                    preparedSurface,
                    lightSample.directionToLight))
                continue;

            float visibility = EvaluateLightVisibility(
                light,
                surfaceWorldPosition,
                sinCosRotation,
                1.0f);
            if (!(visibility > 0.0f))
                continue;
            lightSample.visibility = visibility;
            PbrDirectLighting direct =
                EvaluateDirectLightPrevalidated(
                    preparedMaterial,
                    preparedSurface,
                    lightSample);
            directDiffuse += direct.diffuse * directModulation;
            directSpecular += direct.specular * directModulation;
        }
    }

    float3 diffuse = directDiffuse +
        (g_PbrDeferred.separateIndirect != 0
            ? 0.0f
            : environmentDiffuse);
    float3 specular = directSpecular +
        (g_PbrDeferred.separateIndirect != 0
            ? 0.0f
            : environmentSpecular);
    finalLinearHdr = max(
        diffuse +
            specular +
            gbuffer.material.emissive,
        0.0f);
    if (any(isnan(finalLinearHdr)) ||
        any(isinf(finalLinearHdr)))
    {
        finalLinearHdr = 0.0f;
    }
    return true;
}

[numthreads(16, 16, 1)]
void main(int2 i_globalIdx : SV_DispatchThreadID)
{
    if (any(i_globalIdx.xy >=
        int2(g_Deferred.view.viewportSize)))
    {
        return;
    }

    int2 pixelPosition =
        i_globalIdx.xy +
        int2(g_Deferred.view.viewportOrigin);
    float3 finalLinearHdr =
        PbrDebugUsesBlackBackground(
            g_PbrDeferred.lightingDebugView,
            g_PbrDeferred.visibilityDebugView)
        ? 0.0f
        : max(t_ResolvedBackground[pixelPosition].rgb, 0.0f);
    const float inverseSampleCount =
        1.0f / float(PBR_DEFERRED_MSAA_SAMPLES);
    uint coveredSampleCount = 0u;

    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < PBR_DEFERRED_MSAA_SAMPLES;
        ++sampleIndex)
    {
        float3 sampleRadiance;
        if (ShadeDeferredSample(
                pixelPosition,
                i_globalIdx,
                sampleIndex,
                sampleRadiance))
        {
            ++coveredSampleCount;
            finalLinearHdr +=
                sampleRadiance * inverseSampleCount;
        }
    }

#if PBR_DEFERRED_MSAA_VISIBILITY
    const uint debugPresentation = ResolvePbrDebugPresentation(
        g_PbrDeferred.lightingDebugView,
        g_PbrDeferred.visibilityDebugView);
    if (debugPresentation == UVSR_PBR_DEBUG_PRESENT_VISIBILITY)
    {
        finalLinearHdr = t_VisibilityComposite[pixelPosition].rgb;
    }
    else if (debugPresentation == UVSR_PBR_DEBUG_PRESENT_FINAL)
    {
        // Visibility evaluates the closest coherent covered surface once.
        // Apply only its signed lighting correction and scale it by raster
        // coverage; uncovered samples retain their resolved sky contribution.
        const float coverage =
            float(coveredSampleCount) * inverseSampleCount;
        const float3 visibilityCorrection =
            t_VisibilityComposite[pixelPosition].rgb -
            t_VisibilityBaseLighting[pixelPosition].rgb;
        if (all(isfinite(visibilityCorrection)))
        {
            finalLinearHdr +=
                visibilityCorrection * coverage;
        }
    }
#endif

    if (any(isnan(finalLinearHdr)) ||
        any(isinf(finalLinearHdr)))
    {
        finalLinearHdr = 0.0f;
    }
    u_Output[pixelPosition] = float4(
        min(max(finalLinearHdr, 0.0f), 65504.0f),
        0.0f);
}
