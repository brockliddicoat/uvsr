#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/utils.hlsli>
#include <donut/shaders/shadows.hlsli>
#include <donut/shaders/binding_helpers.hlsli>
#include "pbr_deferred_lighting_cb.h"
#include "pbr_gbuffer.hlsli"
#include "pbr_lighting.hlsli"

cbuffer c_Deferred : register(b0)
{
    PbrDeferredLightingConstants g_PbrDeferred;
};

#define g_Deferred g_PbrDeferred.deferred

Texture2DArray t_ShadowMapArray : register(t0);
TextureCubeArray t_DiffuseLightProbe : register(t1);
TextureCubeArray t_SpecularLightProbe : register(t2);
Texture2D t_EnvironmentBrdf : register(t3);

SamplerState s_ShadowSampler : register(s0);
SamplerComparisonState s_ShadowSamplerComparison : register(s1);
SamplerState s_LightProbeSampler : register(s2);
SamplerState s_BrdfSampler : register(s3);

Texture2D t_GBufferDepth : register(t8);
Texture2D t_GBuffer0 : register(t9);
Texture2D t_GBuffer1 : register(t10);
Texture2D t_GBuffer2 : register(t11);
Texture2D t_GBuffer3 : register(t12);
Texture2D t_MaterialAmbientOcclusion : register(t14);
Texture2D t_IndirectSpecular : register(t15);
Texture2D t_ShadowBuffer : register(t16);

VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Output : register(u0);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_SourceRadiance : register(u1);

float GetRandom(float2 position)
{
    int x = int(position.x) & 3;
    int y = int(position.y) & 3;
    return g_Deferred.noisePattern[y][x];
}

float EvaluateLightVisibility(
    LightConstants light,
    int2 pixelPosition,
    float3 surfaceWorldPosition,
    float2 sinCosRotation)
{
    float visibility = 1.0f;

    if ((light.shadowChannel.x & 0xfffffffc) == 0)
    {
        float4 channels = t_ShadowBuffer[pixelPosition];
        visibility = saturate(channels[light.shadowChannel.x]);
    }

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
    }

    return saturate(visibility);
}

float3 SelectPbrDebugView(
    uint debugView,
    PbrGBufferData gbuffer,
    float3 diffuse,
    float3 specular,
    float directVisibility,
    float3 finalLinearHdr)
{
    if (debugView == 1u) return gbuffer.material.baseColor;
    if (debugView == 2u) return gbuffer.material.metalness.xxx;
    if (debugView == 3u) return gbuffer.material.perceptualRoughness.xxx;
    if (debugView == 4u) return gbuffer.shadingNormal * 0.5f + 0.5f;
    if (debugView == 5u) return diffuse;
    if (debugView == 6u) return specular;
    if (debugView == 7u) return directVisibility.xxx;
    return finalLinearHdr;
}

[numthreads(16, 16, 1)]
void main(int2 i_globalIdx : SV_DispatchThreadID)
{
    if (any(i_globalIdx.xy >= int2(g_Deferred.view.viewportSize)))
        return;

    int2 pixelPosition = i_globalIdx.xy + int2(g_Deferred.view.viewportOrigin);
    float4 gbufferChannels[4];
    gbufferChannels[0] = t_GBuffer0[pixelPosition];
    gbufferChannels[1] = t_GBuffer1[pixelPosition];
    gbufferChannels[2] = t_GBuffer2[pixelPosition];
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

    float3 directDiffuse = 0.0f;
    float3 directSpecular = 0.0f;
    float visibilitySum = 0.0f;
    float angle = GetRandom(i_globalIdx.xy + float2(g_Deferred.randomOffset.x, 0.0f));
    float2 sinCosRotation = float2(sin(angle), cos(angle));

    [loop]
    for (uint lightIndex = 0; lightIndex < g_Deferred.numLights; ++lightIndex)
    {
        LightConstants light = g_Deferred.lights[lightIndex];
        float visibility = EvaluateLightVisibility(
            light, pixelPosition, surfaceWorldPosition, sinCosRotation);
        PbrLightSample lightSample = SamplePbrLight(light, surfaceWorldPosition, visibility);
        PbrDirectLighting direct = EvaluateDirectLight(gbuffer.material, surface, lightSample);
        directDiffuse += direct.diffuse;
        directSpecular += direct.specular;
        visibilitySum += direct.visibility;
    }

    // This sky-color term is an explicit approximation of missing indirect
    // diffuse irradiance. Ambient occlusion affects only this approximation.
    float hemisphere = gbuffer.shadingNormal.y * 0.5f + 0.5f;
    float3 approximateIndirectIrradiance = lerp(
        g_Deferred.ambientColorBottom.rgb,
        g_Deferred.ambientColorTop.rgb,
        hemisphere);
    float3 diffuseReflectance = gbuffer.material.baseColor * (1.0f - gbuffer.material.metalness);
    float3 indirectDiffuse = approximateIndirectIrradiance * diffuseReflectance *
        gbuffer.ambientOcclusion;

    float3 indirectSpecular = 0.0f;
    if (g_Deferred.indirectSpecularScale > 0.0f)
        indirectSpecular = t_IndirectSpecular[pixelPosition].rgb * g_Deferred.indirectSpecularScale;

    // Build the complete configured GI source once in the coherent deferred
    // pass. The stochastic visibility traversal can then fetch one HDR texel
    // instead of separate direct and emissive targets at every claimed sector.
    float3 sourceEmissive = g_PbrDeferred.includeEmissiveSource != 0
        ? max(gbuffer.material.emissive, 0.0f) * g_PbrDeferred.emissiveSourceGain
        : 0.0f;
    float3 sourceRadiance = max(directDiffuse, 0.0f) + sourceEmissive;
    if (any(isnan(sourceRadiance)) || any(isinf(sourceRadiance)))
        sourceRadiance = 0.0f;
    bool doubleSidedEmissive = any(sourceEmissive > 0.0f) &&
        (gbuffer.material.featureMask & PbrFeature_DoubleSided) != 0u;

    float3 diffuse = directDiffuse +
        (g_PbrDeferred.separateIndirect != 0 ? 0.0f : indirectDiffuse);
    float3 specular = directSpecular + indirectSpecular;
    float3 finalLinearHdr = max(diffuse + specular + gbuffer.material.emissive, 0.0f);
    if (any(isnan(finalLinearHdr)) || any(isinf(finalLinearHdr)))
        finalLinearHdr = 0.0f;

    uint debugView = (uint)round(max(g_Deferred.randomOffset.y, 0.0f));
    float averageVisibility = g_Deferred.numLights > 0u
        ? visibilitySum / float(g_Deferred.numLights)
        : 0.0f;
    float3 outputColor = SelectPbrDebugView(
        debugView,
        gbuffer,
        diffuse,
        specular,
        averageVisibility,
        finalLinearHdr);
    u_Output[pixelPosition] = float4(min(max(outputColor, 0.0f), 65504.0f), 0.0f);
    if (g_PbrDeferred.writeSourceRadiance != 0)
        u_SourceRadiance[pixelPosition] = float4(
            min(sourceRadiance, 65504.0f), doubleSidedEmissive ? 1.0f : 0.0f);
}
