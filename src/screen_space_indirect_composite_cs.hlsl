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
Texture2D<float4> t_GBufferMaterial : register(t12);

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

    uint2 samplingPixel = pixel;
    if (g_Visibility.debugMode != 0u)
    {
        float3 debugColor = 0.0f;
        if (g_Visibility.debugMode == 1u)
            debugColor = (g_Visibility.enableAmbientOcclusion != 0u
                ? saturate(t_RawAmbientVisibility[samplingPixel]) : 1.0f).xxx;
        else if (g_Visibility.debugMode == 2u)
            debugColor = (g_Visibility.enableAmbientOcclusion != 0u
                ? saturate(t_FilteredAmbientVisibility[pixel]) : 1.0f).xxx;
        else if (g_Visibility.debugMode == 3u)
            debugColor = g_Visibility.enableIndirectDiffuse != 0u
                ? max(t_RawIndirectDiffuse[samplingPixel].rgb, 0.0f)
                : 0.0f;
        else if (g_Visibility.debugMode == 4u)
            debugColor = g_Visibility.enableIndirectDiffuse != 0u
                ? max(t_FilteredIndirectDiffuse[pixel].rgb, 0.0f) : 0.0f;
        else if (g_Visibility.debugMode == 5u)
            debugColor = g_Visibility.enableIndirectDiffuse != 0u
                ? max(t_FilteredIndirectDiffuse[pixel].rgb, 0.0f) *
                    g_Visibility.indirectDiffuseIntensity
                : 0.0f;
        else if (g_Visibility.debugMode == 6u)
            debugColor = max(t_DirectRadianceSource[pixel].rgb, 0.0f);
        else if (g_Visibility.debugMode == 17u ||
            g_Visibility.debugMode == 18u)
        {
            float historyWeight = g_Visibility.temporalEnabled != 0u
                ? t_HistoryValidity[samplingPixel]
                : 0.0f;
            debugColor = g_Visibility.debugMode == 17u
                ? historyWeight.xxx
                : lerp(
                    float3(0.8f, 0.05f, 0.05f),
                    float3(0.05f, 0.8f, 0.1f),
                    saturate(historyWeight));
        }
        else if (g_Visibility.debugMode >= 7u)
            debugColor = t_FilteredDebug[pixel].rgb;

        if (any(!isfinite(debugColor)))
            debugColor = 0.0f;
        u_Output[pixel] = float4(
            min(max(debugColor, 0.0f), 65504.0f), 0.0f);
        return;
    }

    float filteredAmbientVisibility = g_Visibility.enableAmbientOcclusion != 0u
        ? saturate(t_FilteredAmbientVisibility[pixel]) : 1.0f;
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
    float4 packedMaterial = t_GBufferMaterial[pixel];
    float perceptualRoughness = saturate(t_Normal[pixel].w);
    float ior = lerp(1.0f, 3.0f, saturate(packedMaterial.b));
    float dielectricF0 = (ior - 1.0f) / max(ior + 1.0f, 1e-4f);
    dielectricF0 *= dielectricF0;
    float3 specularF0 = lerp(dielectricF0.xxx, baseColor, metalness);
    float materialAmbientOcclusion = saturate(t_MaterialAmbientOcclusion[pixel]);
    float3 normalWS = SafeNormal(t_Normal[pixel].xyz);
    float hemisphere = normalWS.y * 0.5f + 0.5f;

    // Preserve UVSR's existing sky-gradient fallback convention. The screen
    // visibility term affects only these approximate indirect components.
    float3 approximateAmbientIrradiance = lerp(
        g_Visibility.ambientColorBottom,
        g_Visibility.ambientColorTop,
        hemisphere);
    float3 approximateFallbackIndirect = approximateAmbientIrradiance *
        diffuseReflectance * materialAmbientOcclusion;
    float roughSpecularEnergy = lerp(1.0f, 0.55f, perceptualRoughness);
    float3 approximateFallbackSpecular = approximateAmbientIrradiance *
        specularF0 * roughSpecularEnergy * materialAmbientOcclusion;
    approximateFallbackIndirect *= adjustedAmbientVisibility;
    approximateFallbackSpecular *= adjustedAmbientVisibility;

    // The traversal outputs irradiance. Apply the receiving diffuse BRDF once;
    // metals therefore receive no ordinary diffuse screen-space GI.
    float3 screenSpaceIndirect = filteredIndirectDiffuse * diffuseReflectance *
        (InversePi * g_Visibility.indirectDiffuseIntensity) *
        materialAmbientOcclusion;
    if (g_Visibility.enableIndirectDiffuse == 0u)
        screenSpaceIndirect = 0.0f;

    float3 finalComposite = t_BaseLighting[pixel].rgb +
        approximateFallbackIndirect + approximateFallbackSpecular +
        screenSpaceIndirect;
    if (any(!isfinite(finalComposite)))
        finalComposite = 0.0f;
    finalComposite = max(finalComposite, 0.0f);

    u_Output[pixel] = float4(min(finalComposite, 65504.0f), 0.0f);
}
