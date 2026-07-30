#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/utils.hlsli>
#include "pbr_environment.hlsli"
#include "pbr_gbuffer.hlsli"
#include "screen_space_indirect_composite_shared.h"
#include "screen_space_visibility_cb.h"

#ifndef ENABLE_AO_POWER
#define ENABLE_AO_POWER 0
#endif

cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

Texture2D<float4> t_BaseLighting : register(t0);
Texture2D<float> t_AmbientVisibility : register(t1);
Texture2D<float4> t_IndirectDiffuse : register(t2);
Texture2D<float4> t_GBufferDiffuse : register(t3);
Texture2D<float4> t_GBufferEmissive : register(t4);
Texture2D<float> t_MaterialAmbientOcclusion : register(t5);
Texture2D<float4> t_Normal : register(t6);
TextureCubeArray t_DiffuseEnvironment : register(t7);
Texture2D<float4> t_GBufferSpecular : register(t8);
Texture2D<float> t_Depth : register(t9);
TextureCubeArray t_SpecularEnvironment : register(t10);
Texture2D t_EnvironmentBrdf : register(t11);

SamplerState s_DiffuseEnvironmentSampler : register(s0);
SamplerState s_EnvironmentBrdfSampler : register(s1);

VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Output : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(g_Visibility.fullResolution)))
        return;

    float4 normalChannels = t_Normal[pixel];
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
    {
        u_Output[pixel] = t_BaseLighting[pixel];
        return;
    }

    float ambientVisibility = g_Visibility.enableAmbientOcclusion != 0u
        ? saturate(t_AmbientVisibility[pixel]) : 1.0f;
    float3 indirectDiffuse = g_Visibility.enableIndirectDiffuse != 0u
        ? max(t_IndirectDiffuse[pixel].rgb, 0.0f) : 0.0f;

    float adjustedAmbientVisibility = 1.0f;
    if (g_Visibility.enableAmbientOcclusion != 0u)
    {
#if ENABLE_AO_POWER
        float poweredAmbientVisibility = pow(
            ambientVisibility, max(g_Visibility.ambientPower, 0.01f));
#else
        float poweredAmbientVisibility = ambientVisibility;
#endif
        adjustedAmbientVisibility = saturate(
            1.0f - g_Visibility.ambientStrength *
                (1.0f - poweredAmbientVisibility));
    }

    float3 baseColor = max(t_GBufferDiffuse[pixel].rgb, 0.0f);
    float metalness = saturate(t_GBufferEmissive[pixel].a);
    float materialAmbientOcclusion = saturate(t_MaterialAmbientOcclusion[pixel]);
    float4 specularChannels = t_GBufferSpecular[pixel];
    float3 geometricNormal = DecodeOctahedralNormal(
        specularChannels.rg);
    float3 normalWS = PbrSafeNormalize(
        normalChannels.xyz, geometricNormal);
    if (dot(normalWS, geometricNormal) < 0.0f)
        normalWS = -normalWS;
    float ior = lerp(
        1.0f, 3.0f, saturate(specularChannels.b));

    PbrMaterialParameters material = (PbrMaterialParameters)0;
    material.baseColor = baseColor;
    material.metalness = metalness;
    material.perceptualRoughness =
        saturate(normalChannels.w);
    material.dielectricF0 = IorToF0(ior);
    float3 worldPosition = ReconstructWorldPosition(
        g_Visibility.view,
        float2(pixel) + 0.5f,
        t_Depth[pixel]);
    float3 viewIncident = GetIncidentVector(
        g_Visibility.view.cameraDirectionOrPosition,
        worldPosition);
    PbrSurfaceInteraction surface;
    surface.position = worldPosition;
    surface.shadingNormal = normalWS;
    surface.geometricNormal = geometricNormal;
    surface.viewDirection = -viewIncident;
    PbrPreparedEnvironment preparedEnvironment =
        PreparePbrEnvironment(
            material,
            surface,
            g_Visibility.specularEnvironmentMipLevels);

    float3 environmentDiffuseResponse;
    if (g_Visibility.diffuseEnvironmentEnabled != 0u)
    {
        environmentDiffuseResponse =
            t_DiffuseEnvironment.SampleLevel(
                s_DiffuseEnvironmentSampler,
                float4(
                    normalWS,
                    g_Visibility.diffuseEnvironmentArrayIndex),
                0.0f).rgb *
            g_Visibility.diffuseEnvironmentScale;
        if (any(!isfinite(environmentDiffuseResponse)))
            environmentDiffuseResponse = 0.0f;
        environmentDiffuseResponse =
            max(environmentDiffuseResponse, 0.0f);
    }
    else
        environmentDiffuseResponse = 0.0f;
    float3 environmentDiffuse =
        EvaluatePbrEnvironmentDiffuse(
            preparedEnvironment,
            environmentDiffuseResponse,
            materialAmbientOcclusion);

    float3 environmentSpecular = 0.0f;
    if (g_Visibility.specularEnvironmentEnabled != 0u &&
        preparedEnvironment.valid > 0.0f)
    {
        float3 prefilteredRadiance =
            t_SpecularEnvironment.SampleLevel(
                s_DiffuseEnvironmentSampler,
                float4(
                    preparedEnvironment.reflectionDirection,
                    g_Visibility.specularEnvironmentArrayIndex),
                preparedEnvironment.specularMip).rgb *
            g_Visibility.specularEnvironmentScale;
        float2 environmentBrdf = t_EnvironmentBrdf.SampleLevel(
            s_EnvironmentBrdfSampler,
            float2(
                preparedEnvironment.noV,
                preparedEnvironment.perceptualRoughness),
            0.0f).xy;
        if (any(!isfinite(prefilteredRadiance)))
            prefilteredRadiance = 0.0f;
        if (any(!isfinite(environmentBrdf)))
            environmentBrdf = 0.0f;

        // Apply the same visibility field that attenuates diffuse fallback,
        // but use the roughness- and view-aware specular-occlusion policy.
        // This keeps the global probe from illuminating covered interiors as
        // if they had an unobstructed view of the full sky.
        float combinedAmbientOcclusion = saturate(
            materialAmbientOcclusion * adjustedAmbientVisibility);
        environmentSpecular = EvaluatePbrEnvironmentSpecular(
            preparedEnvironment,
            prefilteredRadiance,
            environmentBrdf,
            combinedAmbientOcclusion);
    }

    // The traversal outputs irradiance. Apply the receiving diffuse BRDF once;
    // metals therefore receive no ordinary diffuse screen-space GI.
    float3 screenSpaceIndirect =
        indirectDiffuse *
        preparedEnvironment.diffuseWeight *
        (UVSR_INV_PI * g_Visibility.indirectDiffuseIntensity) *
        materialAmbientOcclusion;
    if (g_Visibility.enableIndirectDiffuse == 0u)
        screenSpaceIndirect = 0.0f;

    float3 finalComposite = ComposeScreenSpaceIndirectLighting(
        t_BaseLighting[pixel].rgb + environmentSpecular,
        environmentDiffuse,
        adjustedAmbientVisibility,
        screenSpaceIndirect);
    if (g_Visibility.showIndirectDiffuseOnly != 0u &&
        g_Visibility.enableIndirectDiffuse != 0u)
    {
        finalComposite = screenSpaceIndirect;
    }
    if (any(!isfinite(finalComposite)))
        finalComposite = 0.0f;
    finalComposite = max(finalComposite, 0.0f);

    u_Output[pixel] = float4(min(finalComposite, 65504.0f), 0.0f);
}
