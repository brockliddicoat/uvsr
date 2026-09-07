#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "pbr_deferred_lighting_bindings.h"
#include "pbr_deferred_lighting_cb.h"
#include "pbr_environment.hlsli"
#include "pbr_gbuffer.hlsli"
#include "pbr_lighting.hlsli"
#include "renderer_environment_bindings.h"

cbuffer c_Deferred : register(b0)
{
    PbrDeferredLightingConstants g_PbrDeferred;
};

#define g_Deferred g_PbrDeferred.deferred

TextureCubeArray t_DiffuseEnvironment :
    register(UVSR_PBR_DEFERRED_DIFFUSE_ENVIRONMENT_REGISTER);
TextureCubeArray t_SpecularEnvironment :
    register(UVSR_PBR_DEFERRED_SPECULAR_ENVIRONMENT_REGISTER);
Texture2D t_EnvironmentBrdf :
    register(UVSR_PBR_DEFERRED_ENVIRONMENT_BRDF_REGISTER);

SamplerState s_DiffuseEnvironmentSampler : register(s2);
SamplerState s_EnvironmentBrdfSampler : register(s3);

Texture2D t_GBufferDepth : register(t8);
Texture2D t_GBuffer0 : register(t9);
Texture2D t_GBuffer1 : register(t10);
Texture2D t_GBuffer2 : register(t11);
Texture2D t_GBuffer3 : register(t12);
Texture2D t_MaterialAmbientOcclusion : register(t14);
Texture2D<float4> t_FlashlightVisibility :
    register(UVSR_PBR_FLASHLIGHT_VISIBILITY_REGISTER);
Texture2D<float> t_SunVisibility :
    register(UVSR_PBR_SUN_VISIBILITY_REGISTER);
Texture2D<float> t_SkyVisibility :
    register(UVSR_PBR_SKY_VISIBILITY_REGISTER);

RWTexture2D<float4> u_Output : register(u0);

float3 DecodeDirectLightVisibility(float encoded)
{
    if (!isfinite(encoded))
        return 1.0f;
    return saturate(encoded).xxx;
}

float3 GetDirectLightVisibility(
    uint lightIndex,
    int2 pixelPosition)
{
    float3 visibility = 1.0f;
    if (int(lightIndex) ==
        g_PbrDeferred.directVisibilityLightIndices.x)
    {
        visibility = min(
            visibility,
            DecodeDirectLightVisibility(
                t_FlashlightVisibility[pixelPosition].r));
    }
    if (int(lightIndex) ==
        g_PbrDeferred.directVisibilityLightIndices.y)
    {
        visibility = min(
            visibility,
            DecodeDirectLightVisibility(
                t_SunVisibility[pixelPosition]));
    }
    return visibility;
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
        const float sampledSkyVisibility =
            t_SkyVisibility[pixelPosition];
        skyVisibility = isfinite(sampledSkyVisibility)
            ? saturate(sampledSkyVisibility)
            : 1.0f;
    }
    if (g_Deferred.numLightProbes > 0u)
    {
        if (environmentProbe.diffuseScale > 0.0f)
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

        if (environmentProbe.specularScale > 0.0f &&
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

    float3 lightingDebugColor = 0.0f;
    if (PbrLightingDebugIsActive(
        g_PbrDeferred.lightingDebugView))
    {
        if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_SHADING_NORMAL)
        {
            lightingDebugColor =
                gbuffer.shadingNormal * 0.5f + 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_GEOMETRIC_NORMAL)
        {
            lightingDebugColor =
                gbuffer.geometricNormal * 0.5f + 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_NORMAL_DIFFERENCE)
        {
            float angularDifference = saturate(
                1.0f - dot(
                    gbuffer.shadingNormal,
                    gbuffer.geometricNormal));
            lightingDebugColor = float3(
                angularDifference,
                0.0f,
                1.0f - angularDifference);
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_DIFFUSE_ENVIRONMENT)
        {
            lightingDebugColor = environmentDiffuseResponse;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_ENVIRONMENT_DIRECTION)
        {
            // Exact unit-color diffuse response for the diagnostic radiance
            // field L(w) = 1 + 0.75 * w.x:
            // E(n) / pi = 1 + (2 * 0.75 / 3) * n.x.
            float cardinalResponse =
                1.0f + 0.5f * gbuffer.shadingNormal.x;
            lightingDebugColor = cardinalResponse * 0.5f;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_PREFILTERED_SPECULAR)
        {
            lightingDebugColor = prefilteredEnvironment;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_ENVIRONMENT_BRDF)
        {
            lightingDebugColor = float3(
                environmentBrdf.x,
                environmentBrdf.y,
                0.0f);
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_FINAL_SPECULAR)
        {
            lightingDebugColor = environmentSpecular;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_COMBINED_ENVIRONMENT)
        {
            lightingDebugColor =
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
            lightingDebugColor = specularOcclusion.xxx;
        }
        else if (g_PbrDeferred.lightingDebugView ==
            UVSR_PBR_LIGHTING_DEBUG_ENVIRONMENT_MIP)
        {
            const float normalizedMip =
                environmentProbe.mipLevels > 1.0f
                    ? preparedEnvironment.specularMip /
                        (environmentProbe.mipLevels - 1.0f)
                    : 0.0f;
            lightingDebugColor = normalizedMip.xxx;
        }
        else if (showSkyVisibility)
        {
            lightingDebugColor =
                ResolvePbrSkyVisibilityDebugColor(skyVisibility);
        }
    }

    float3 directDiffuse = 0.0f;
    float3 directSpecular = 0.0f;
    if (g_Deferred.numLights > 0u)
    {
        [loop]
        for (uint lightIndex = 0; lightIndex < g_Deferred.numLights; ++lightIndex)
        {
            LightConstants light = g_Deferred.lights[lightIndex];
            float3 directModulation =
                GetDirectLightVisibility(
                lightIndex,
                pixelPosition);
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

            PbrDirectLighting direct = EvaluateDirectLightPrevalidated(
                preparedMaterial, preparedSurface, lightSample);
            directDiffuse += direct.diffuse * directModulation;
            directSpecular += direct.specular * directModulation;
        }
    }


    float3 diffuse = directDiffuse + environmentDiffuse;
    float3 specular = directSpecular + environmentSpecular;
    float3 finalLinearHdr = max(diffuse + specular + gbuffer.material.emissive, 0.0f);
    if (any(isnan(finalLinearHdr)) || any(isinf(finalLinearHdr)))
        finalLinearHdr = 0.0f;

    const float3 presentedColor =
        PbrLightingDebugIsActive(
            g_PbrDeferred.lightingDebugView)
            ? lightingDebugColor
            : finalLinearHdr;
    u_Output[pixelPosition] = float4(
        min(max(presentedColor, 0.0f), 65504.0f), 0.0f);
}
