#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_cb.h"

cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

Texture2D<float4> t_BaseLighting : register(t0);
Texture2D<float> t_FilteredAmbientVisibility : register(t1);
Texture2D<float4> t_FilteredIndirectDiffuse : register(t2);
Texture2D<float4> t_GBufferDiffuse : register(t3);
Texture2D<float4> t_GBufferEmissive : register(t4);
Texture2D<float> t_MaterialAmbientOcclusion : register(t5);
Texture2D<float4> t_Normal : register(t6);
Texture2D<float> t_RawAmbientVisibility : register(t7);
Texture2D<float4> t_RawIndirectDiffuse : register(t8);
Texture2D<float4> t_FilteredDebug : register(t9);
Texture2D<float4> t_DirectRadianceSource : register(t10);
Texture2D<float> t_HistoryValidity : register(t11);

VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Output : register(u0);

static const float InversePi = 0.31830988618379067154f;

float3 SafeNormal(float3 value)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12f && isfinite(lengthSquared)
        ? value * rsqrt(lengthSquared)
        : float3(0.0f, 1.0f, 0.0f);
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(g_Visibility.fullResolution)))
        return;

    uint2 samplingPixel = min(pixel / g_Visibility.resolutionScale,
        uint2(g_Visibility.samplingResolution) - 1u);
    float rawAmbientVisibility = g_Visibility.debugMode == 1u
        ? saturate(t_RawAmbientVisibility[samplingPixel]) : 1.0f;
    float filteredAmbientVisibility = g_Visibility.enableAmbientOcclusion != 0u
        ? saturate(t_FilteredAmbientVisibility[pixel]) : 1.0f;
    float3 rawIndirectDiffuse = g_Visibility.debugMode == 3u
        ? max(t_RawIndirectDiffuse[samplingPixel].rgb, 0.0f) : 0.0f;
    float3 filteredIndirectDiffuse = g_Visibility.enableIndirectDiffuse != 0u
        ? max(t_FilteredIndirectDiffuse[pixel].rgb, 0.0f) : 0.0f;

    float adjustedAmbientVisibility = 1.0f;
    if (g_Visibility.enableAmbientOcclusion != 0u)
    {
        float poweredVisibility = abs(g_Visibility.ambientPower - 1.0f) < 1e-4f
            ? filteredAmbientVisibility
            : pow(max(filteredAmbientVisibility, 1e-6f), g_Visibility.ambientPower);
        adjustedAmbientVisibility = saturate(
            1.0f - g_Visibility.ambientStrength * (1.0f - poweredVisibility));
    }

    float3 baseColor = max(t_GBufferDiffuse[pixel].rgb, 0.0f);
    float metalness = saturate(t_GBufferEmissive[pixel].a);
    float3 diffuseReflectance = baseColor * (1.0f - metalness);
    float materialAmbientOcclusion = saturate(t_MaterialAmbientOcclusion[pixel]);
    float3 normalWS = SafeNormal(t_Normal[pixel].xyz);
    float hemisphere = normalWS.y * 0.5f + 0.5f;

    // Preserve UVSR's existing sky-gradient fallback convention. The screen
    // visibility term affects only this approximate indirect component.
    float3 approximateFallbackIndirect = lerp(
        g_Visibility.ambientColorBottom,
        g_Visibility.ambientColorTop,
        hemisphere) * diffuseReflectance * materialAmbientOcclusion;
    approximateFallbackIndirect *= adjustedAmbientVisibility;

    // The traversal outputs irradiance. Apply the receiving diffuse BRDF once;
    // metals therefore receive no ordinary diffuse screen-space GI.
    float3 screenSpaceIndirect = filteredIndirectDiffuse * diffuseReflectance *
        (InversePi * g_Visibility.indirectDiffuseIntensity) *
        materialAmbientOcclusion;
    if (g_Visibility.enableIndirectDiffuse == 0u)
        screenSpaceIndirect = 0.0f;

    float3 finalComposite = t_BaseLighting[pixel].rgb +
        approximateFallbackIndirect + screenSpaceIndirect;
    if (any(!isfinite(finalComposite)))
        finalComposite = 0.0f;
    finalComposite = max(finalComposite, 0.0f);

    float3 outputColor = finalComposite;
    if (g_Visibility.debugMode == 1u)
        outputColor = rawAmbientVisibility.xxx;
    else if (g_Visibility.debugMode == 2u)
        outputColor = filteredAmbientVisibility.xxx;
    else if (g_Visibility.debugMode == 3u)
        outputColor = rawIndirectDiffuse;
    else if (g_Visibility.debugMode == 4u)
        outputColor = filteredIndirectDiffuse;
    else if (g_Visibility.debugMode == 5u)
        outputColor = filteredIndirectDiffuse * g_Visibility.indirectDiffuseIntensity;
    else if (g_Visibility.debugMode == 6u)
        outputColor = max(t_DirectRadianceSource[pixel].rgb, 0.0f);
    else if (g_Visibility.debugMode == 17u)
    {
        float historyWeight = g_Visibility.temporalEnabled != 0u
            ? t_HistoryValidity[samplingPixel]
            : 0.0f;
        outputColor = historyWeight.xxx;
    }
    else if (g_Visibility.debugMode == 18u)
    {
        float historyWeight = g_Visibility.temporalEnabled != 0u
            ? t_HistoryValidity[samplingPixel]
            : 0.0f;
        outputColor = lerp(
            float3(0.8f, 0.05f, 0.05f),
            float3(0.05f, 0.8f, 0.1f),
            saturate(historyWeight));
    }
    else if (g_Visibility.debugMode >= 7u)
        outputColor = t_FilteredDebug[pixel].rgb;

    if (any(!isfinite(outputColor)))
        outputColor = 0.0f;
    u_Output[pixel] = float4(min(max(outputColor, 0.0f), 65504.0f), 0.0f);
}
