#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/utils.hlsli>
#include <donut/shaders/shadows.hlsli>
#include <donut/shaders/binding_helpers.hlsli>
#include "pbr_deferred_lighting_cb.h"
#include "pbr_gbuffer.hlsli"
#include "pbr_lighting.hlsli"

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
SamplerComparisonState s_ShadowSamplerComparison : register(s1);

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

// These inputs remain single-sample. Screen-space visibility is deliberately
// mutexed for diagnostic MSAA, so the fallbacks are spatially constant unless
// a future single-sample producer is explicitly connected.
Texture2D t_IndirectSpecular : register(t15);
Texture2D t_ShadowBuffer : register(t16);

// HdrColor contains only the procedural sky and was resolved before this
// dispatch. Covered samples are still zero because the sky depth test failed;
// the resolved value therefore already represents the exact uncovered-sample
// sky contribution divided by the sample count.
Texture2D t_ResolvedBackground : register(t17);
#if PBR_DEFERRED_MSAA_VISIBILITY
Texture2D t_VisibilityBaseLighting : register(t18);
Texture2D t_VisibilityComposite : register(t19);
#endif

VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_Output : register(u0);

float GetRandom(float2 position)
{
    int x = int(position.x) & 3;
    int y = int(position.y) & 3;
    return g_Deferred.noisePattern[y][x];
}

float GetScreenShadowVisibility(
    LightConstants light,
    int2 pixelPosition)
{
    if ((light.shadowChannel.x & 0xfffffffc) == 0)
    {
        float4 channels = t_ShadowBuffer[pixelPosition];
        return saturate(channels[light.shadowChannel.x]);
    }

    return 1.0f;
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
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
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

    float3 directDiffuse = 0.0f;
    float3 directSpecular = 0.0f;
    if (g_Deferred.numLights > 0u)
    {
        // G-buffer rasterization owns coverage and per-sample depth. UVSR uses
        // the fixed D3D12 sample pattern and no programmable sample positions;
        // reconstructing XY at the pixel center matches the established
        // deferred convention while the stored sample depth preserves the
        // silhouette owner.
        float3 surfaceWorldPosition = ReconstructWorldPosition(
            g_Deferred.view,
            float2(pixelPosition) + 0.5f,
            t_GBufferDepth.Load(pixelPosition, sampleIndex));
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

        float angle = GetRandom(
            dispatchPosition +
            float2(g_Deferred.randomOffset.x, 0.0f));
        float2 sinCosRotation =
            float2(sin(angle), cos(angle));
        LightingContributionGate exactDirectGate =
            MakeLightingContributionGate(0u, 0.0f, 1.0f);

        [loop]
        for (uint lightIndex = 0;
            lightIndex < g_Deferred.numLights;
            ++lightIndex)
        {
            LightConstants light =
                g_Deferred.lights[lightIndex];
            float visibility = GetScreenShadowVisibility(
                light,
                pixelPosition);
            if (!(visibility > 0.0f))
                continue;

            PbrLightSample lightSample = SamplePbrLight(
                light,
                surfaceWorldPosition,
                1.0f);
            uint lightRejection =
                LightingClassifyContribution(
                    exactDirectGate,
                    LightingSource_Direct,
                    lightSample.incidentRadiance,
                    1.0f);
            if (!LightingShouldEvaluate(lightRejection))
                continue;
            lightRejection =
                ClassifyPbrDirectSurfacePrepared(
                    preparedSurface,
                    lightSample.directionToLight);
            if (!LightingShouldEvaluate(lightRejection))
                continue;

            visibility = EvaluateLightVisibility(
                light,
                surfaceWorldPosition,
                sinCosRotation,
                visibility);
            if (!(visibility > 0.0f))
                continue;
            lightSample.visibility = visibility;
            PbrDirectLighting direct =
                EvaluateDirectLightPrevalidated(
                    preparedMaterial,
                    preparedSurface,
                    lightSample);
            directDiffuse += direct.diffuse;
            directSpecular += direct.specular;
        }
    }

    float3 indirectDiffuse = 0.0f;
    if (g_PbrDeferred.separateIndirect == 0)
    {
        float hemisphere =
            gbuffer.shadingNormal.y * 0.5f + 0.5f;
        float3 approximateIndirectIrradiance = lerp(
            g_Deferred.ambientColorBottom.rgb,
            g_Deferred.ambientColorTop.rgb,
            hemisphere);
        float3 diffuseReflectance =
            gbuffer.material.baseColor *
            (1.0f - gbuffer.material.metalness);
        indirectDiffuse =
            approximateIndirectIrradiance *
            diffuseReflectance *
            gbuffer.ambientOcclusion;
    }

    float3 indirectSpecular = 0.0f;
    if (g_Deferred.indirectSpecularScale > 0.0f)
    {
        indirectSpecular =
            t_IndirectSpecular[pixelPosition].rgb *
            g_Deferred.indirectSpecularScale;
    }

    float3 diffuse = directDiffuse +
        (g_PbrDeferred.separateIndirect != 0
            ? 0.0f
            : indirectDiffuse);
    float3 specular = directSpecular + indirectSpecular;
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
        max(t_ResolvedBackground[pixelPosition].rgb, 0.0f);
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
    // Visibility evaluates the closest coherent covered surface once. Apply
    // only its signed lighting correction and scale it by raster coverage;
    // uncovered MSAA samples retain their already-resolved sky contribution.
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
