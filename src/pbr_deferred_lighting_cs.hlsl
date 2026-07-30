#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/utils.hlsli>
#include <donut/shaders/shadows.hlsli>
#include <donut/shaders/binding_helpers.hlsli>
#include "pbr_deferred_lighting_cb.h"
#include "pbr_environment.hlsli"
#include "pbr_gbuffer.hlsli"
#include "pbr_lighting.hlsli"

#ifndef WRITE_SOURCE_RADIANCE
#define WRITE_SOURCE_RADIANCE 0
#endif
#ifndef WRITE_BOUNCE_METADATA
#define WRITE_BOUNCE_METADATA 0
#endif

cbuffer c_Deferred : register(b0)
{
    PbrDeferredLightingConstants g_PbrDeferred;
};

#define g_Deferred g_PbrDeferred.deferred

Texture2DArray t_ShadowMapArray : register(t0);
TextureCubeArray t_DiffuseEnvironment : register(t1);
TextureCubeArray t_SpecularEnvironment : register(t2);
Texture2D t_EnvironmentBrdf : register(t3);

SamplerComparisonState s_ShadowSamplerComparison : register(s1);
SamplerState s_DiffuseEnvironmentSampler : register(s2);
SamplerState s_EnvironmentBrdfSampler : register(s3);

Texture2D t_GBufferDepth : register(t8);
Texture2D t_GBuffer0 : register(t9);
Texture2D t_GBuffer1 : register(t10);
Texture2D t_GBuffer2 : register(t11);
Texture2D t_GBuffer3 : register(t12);
Texture2D t_MaterialAmbientOcclusion : register(t14);
Texture2D t_IndirectSpecular : register(t15);
Texture2D t_ShadowBuffer : register(t16);
Texture2D<float> t_DirectionalVisibility0 : register(t20);
Texture2D<float> t_DirectionalVisibility1 : register(t21);
Texture2D<float> t_DirectionalVisibility2 : register(t22);

VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Output : register(u0);
#if WRITE_SOURCE_RADIANCE
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_SourceRadiance : register(u1);
#endif

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

float GetDirectionalLightVisibility(
    uint lightIndex,
    int2 pixelPosition)
{
    float visibility = 1.0f;
    if (int(lightIndex) ==
        g_PbrDeferred.directionalVisibilityLightIndices.x)
    {
        visibility *= saturate(
            t_DirectionalVisibility0[pixelPosition]);
    }
    if (int(lightIndex) ==
        g_PbrDeferred.directionalVisibilityLightIndices.y)
    {
        visibility *= saturate(
            t_DirectionalVisibility1[pixelPosition]);
    }
    if (int(lightIndex) ==
        g_PbrDeferred.directionalVisibilityLightIndices.z)
    {
        visibility *= saturate(
            t_DirectionalVisibility2[pixelPosition]);
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
            combinedCascadeVisibility + cascadeVisibility * (1.0001f - combinedCascadeVisibility.y));
        if (combinedCascadeVisibility.y == 1.0f)
            break;
    }

    combinedCascadeVisibility.x +=
        (1.0f - combinedCascadeVisibility.y) * light.outOfBoundsShadow;
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
        visibility *= saturate(objectVisibility.x + (1.0f - objectVisibility.y));
        if (!(visibility > 0.0f))
            break;
    }

    return saturate(visibility);
}

[numthreads(16, 16, 1)]
void main(int2 i_globalIdx : SV_DispatchThreadID)
{
    if (any(i_globalIdx.xy >= int2(g_Deferred.view.viewportSize)))
        return;

    int2 pixelPosition = i_globalIdx.xy + int2(g_Deferred.view.viewportOrigin);
    float4 normalChannels = t_GBuffer2[pixelPosition];
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
    {
        u_Output[pixelPosition] = 0.0f;
#if WRITE_SOURCE_RADIANCE
        u_SourceRadiance[pixelPosition] = 0.0f;
#endif
        return;
    }

    float4 gbufferChannels[4];
    gbufferChannels[0] = t_GBuffer0[pixelPosition];
    gbufferChannels[1] = t_GBuffer1[pixelPosition];
    gbufferChannels[2] = normalChannels;
    gbufferChannels[3] = t_GBuffer3[pixelPosition];
    PbrGBufferData gbuffer = DecodePbrGBuffer(
        gbufferChannels, t_MaterialAmbientOcclusion[pixelPosition].x);

    float3 surfaceWorldPosition = ReconstructWorldPosition(
        g_Deferred.view,
        float2(pixelPosition) + 0.5f,
        t_GBufferDepth[pixelPosition].x);
    float3 viewIncident = GetIncidentVector(
        g_Deferred.view.cameraDirectionOrPosition,
        surfaceWorldPosition);
    PbrSurfaceInteraction surface;
    surface.position = surfaceWorldPosition;
    surface.shadingNormal = gbuffer.shadingNormal;
    surface.geometricNormal = gbuffer.geometricNormal;
    surface.viewDirection = -viewIncident;
    PbrPreparedSurface preparedSurface = PreparePbrSurface(surface);
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
    if (g_Deferred.numLightProbes > 0u)
    {
        const bool needDiffuseEnvironment =
#if WRITE_SOURCE_RADIANCE
            true;
#else
            g_PbrDeferred.separateIndirect == 0 ||
            g_PbrDeferred.lightingDebugView == 4u ||
            g_PbrDeferred.lightingDebugView == 9u;
#endif
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
        }

        const bool needSpecularEnvironment =
            g_PbrDeferred.separateIndirect == 0 ||
            (g_PbrDeferred.lightingDebugView >= 6u &&
                g_PbrDeferred.lightingDebugView <= 9u);
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
        }
    }

    if (g_PbrDeferred.lightingDebugView != 0u)
    {
        float3 debugColor = 0.0f;
        if (g_PbrDeferred.lightingDebugView == 1u)
        {
            debugColor = gbuffer.shadingNormal * 0.5f + 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView == 2u)
        {
            debugColor = gbuffer.geometricNormal * 0.5f + 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView == 3u)
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
        else if (g_PbrDeferred.lightingDebugView == 4u)
        {
            debugColor = environmentDiffuseResponse;
        }
        else if (g_PbrDeferred.lightingDebugView == 5u)
        {
            // Exact unit-color diffuse response for the diagnostic radiance
            // field L(w) = 1 + 0.75 * w.x:
            // E(n) / pi = 1 + (2 * 0.75 / 3) * n.x.
            float cardinalResponse =
                1.0f + 0.5f * gbuffer.shadingNormal.x;
            debugColor = cardinalResponse * 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView == 6u)
        {
            debugColor = prefilteredEnvironment;
        }
        else if (g_PbrDeferred.lightingDebugView == 7u)
        {
            debugColor = float3(
                environmentBrdf.x,
                environmentBrdf.y,
                0.0f);
        }
        else if (g_PbrDeferred.lightingDebugView == 8u)
        {
            debugColor = environmentSpecular;
        }
        else if (g_PbrDeferred.lightingDebugView == 9u)
        {
            debugColor =
                environmentDiffuse + environmentSpecular;
        }
        else if (g_PbrDeferred.lightingDebugView == 10u)
        {
            const float specularOcclusion =
                PbrEnvironmentSpecularOcclusion(
                    preparedEnvironment.noV,
                    gbuffer.ambientOcclusion,
                    preparedEnvironment.perceptualRoughness);
            debugColor = specularOcclusion.xxx;
        }
        else if (g_PbrDeferred.lightingDebugView == 11u)
        {
            const float normalizedMip =
                environmentProbe.mipLevels > 1.0f
                    ? preparedEnvironment.specularMip /
                        (environmentProbe.mipLevels - 1.0f)
                    : 0.0f;
            debugColor = normalizedMip.xxx;
        }

        u_Output[pixelPosition] = float4(
            min(max(debugColor, 0.0f), 65504.0f),
            0.0f);
#if WRITE_SOURCE_RADIANCE
        u_SourceRadiance[pixelPosition] = 0.0f;
#endif
        return;
    }

    float3 directDiffuse = 0.0f;
    float3 directSpecular = 0.0f;
    if (g_Deferred.numLights > 0u)
    {
        float angle = GetRandom(i_globalIdx.xy + float2(g_Deferred.randomOffset.x, 0.0f));
        float2 sinCosRotation = float2(sin(angle), cos(angle));
        LightingContributionGate exactDirectGate = MakeLightingContributionGate(
            0u,
            0.0f,
            1.0f);

        [loop]
        for (uint lightIndex = 0; lightIndex < g_Deferred.numLights; ++lightIndex)
        {
            LightConstants light = g_Deferred.lights[lightIndex];
            float visibility = GetScreenShadowVisibility(
                light,
                pixelPosition) *
                GetDirectionalLightVisibility(
                    lightIndex,
                    pixelPosition);
            if (!(visibility > 0.0f))
                continue;

            PbrLightSample lightSample = SamplePbrLight(
                light, surfaceWorldPosition, 1.0f);
            uint lightRejection = LightingClassifyContribution(
                exactDirectGate,
                LightingSource_Direct,
                lightSample.incidentRadiance,
                1.0f);
            if (!LightingShouldEvaluate(lightRejection))
                continue;
            lightRejection = ClassifyPbrDirectSurfacePrepared(
                preparedSurface, lightSample.directionToLight);
            if (!LightingShouldEvaluate(lightRejection))
                continue;

            visibility = EvaluateLightVisibility(
                light, surfaceWorldPosition, sinCosRotation, visibility);
            if (!(visibility > 0.0f))
                continue;
            lightSample.visibility = visibility;
            PbrDirectLighting direct = EvaluateDirectLightPrevalidated(
                preparedMaterial, preparedSurface, lightSample);
            directDiffuse += direct.diffuse;
            directSpecular += direct.specular;
        }
    }

    float3 indirectSpecular = 0.0f;
    if (g_Deferred.indirectSpecularScale > 0.0f)
        indirectSpecular = t_IndirectSpecular[pixelPosition].rgb * g_Deferred.indirectSpecularScale;

#if WRITE_SOURCE_RADIANCE
    // Build the configured GI source only in the specialization that has an
    // actual downstream source consumer.
    float3 sourceEmissive = g_PbrDeferred.includeEmissiveSource != 0
        ? max(gbuffer.material.emissive, 0.0f) * g_PbrDeferred.emissiveSourceGain
        : 0.0f;
    // Environment diffuse is direct lighting from the global radiance field
    // at the source surface. Carry it into the diffuse GI source so the next
    // bounce sees the same sky illumination as the visible surface. Specular
    // IBL remains excluded from this diffuse transport path.
    float3 sourceRadiance =
        max(directDiffuse + environmentDiffuse, 0.0f) +
        sourceEmissive;
    if (any(isnan(sourceRadiance)) || any(isinf(sourceRadiance)))
        sourceRadiance = 0.0f;
    bool doubleSidedEmissive = any(sourceEmissive > 0.0f) &&
        (gbuffer.material.featureMask & PbrFeature_DoubleSided) != 0u;
#if WRITE_BOUNCE_METADATA
    float3 diffuseTransport = max(gbuffer.material.baseColor, 0.0f) *
        (1.0f - saturate(gbuffer.material.metalness)) *
        saturate(gbuffer.ambientOcclusion);
    bool diffuseActive = all(isfinite(diffuseTransport)) &&
        any(diffuseTransport > 0.0f);
    uint sourceMetadata = 0u;
    if (doubleSidedEmissive)
        sourceMetadata |= PbrGiMetadata_OutgoingDoubleSided;
    if ((gbuffer.material.featureMask & PbrFeature_DoubleSided) != 0u)
        sourceMetadata |= PbrGiMetadata_SurfaceDoubleSided;
    if (diffuseActive)
        sourceMetadata |= PbrGiMetadata_DiffuseActive;
#else
    uint sourceMetadata = doubleSidedEmissive
        ? PbrGiMetadata_OutgoingDoubleSided
        : 0u;
#endif
#endif

    float3 diffuse = directDiffuse +
        (g_PbrDeferred.separateIndirect != 0
            ? 0.0f
            : environmentDiffuse);
    float3 specular = directSpecular +
        (g_PbrDeferred.separateIndirect != 0
            ? 0.0f
            : environmentSpecular) +
        indirectSpecular;
    float3 finalLinearHdr = max(diffuse + specular + gbuffer.material.emissive, 0.0f);
    if (any(isnan(finalLinearHdr)) || any(isinf(finalLinearHdr)))
        finalLinearHdr = 0.0f;

    u_Output[pixelPosition] = float4(
        min(max(finalLinearHdr, 0.0f), 65504.0f), 0.0f);
#if WRITE_SOURCE_RADIANCE
    u_SourceRadiance[pixelPosition] = float4(
        min(sourceRadiance, 65504.0f), float(sourceMetadata));
#endif
}
