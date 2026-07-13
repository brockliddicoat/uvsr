#ifndef UVSR_PBR_GBUFFER_HLSLI
#define UVSR_PBR_GBUFFER_HLSLI

#include "pbr.hlsli"

struct PbrGBufferData
{
    PbrMaterialParameters material;
    float3 shadingNormal;
    float3 geometricNormal;
    float ambientOcclusion;
};

float2 PbrSignNotZero(float2 value)
{
    return float2(value.x >= 0.0f ? 1.0f : -1.0f,
        value.y >= 0.0f ? 1.0f : -1.0f);
}

float2 EncodeOctahedralNormal(float3 normal)
{
    normal = PbrSafeNormalize(normal, float3(0.0f, 1.0f, 0.0f));
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    if (normal.z < 0.0f)
        normal.xy = (1.0f - abs(normal.yx)) * PbrSignNotZero(normal.xy);
    return normal.xy * 0.5f + 0.5f;
}

float3 DecodeOctahedralNormal(float2 encoded)
{
    float2 f = encoded * 2.0f - 1.0f;
    float3 normal = float3(f, 1.0f - abs(f.x) - abs(f.y));
    float correction = saturate(-normal.z);
    normal.xy -= PbrSignNotZero(normal.xy) * correction;
    return PbrSafeNormalize(normal, float3(0.0f, 1.0f, 0.0f));
}

void EncodePbrGBuffer(
    PbrGBufferData data,
    out float4 channel0,
    out float4 channel1,
    out float4 channel2,
    out float4 channel3,
    out float materialAmbientOcclusion)
{
    float encodedIor = saturate(
        (F0ToIor(data.material.dielectricF0) - 1.0f) * 0.5f);
    channel0 = float4(max(data.material.baseColor, 0.0f), saturate(data.material.opacity));
    channel1 = float4(
        EncodeOctahedralNormal(data.geometricNormal),
        encodedIor,
        float(data.material.featureMask) / 255.0f);
    channel2 = float4(
        PbrSafeNormalize(data.shadingNormal, data.geometricNormal),
        saturate(data.material.perceptualRoughness));
    channel3 = float4(max(data.material.emissive, 0.0f), saturate(data.material.metalness));
    materialAmbientOcclusion = saturate(data.ambientOcclusion);
}

PbrGBufferData DecodePbrGBuffer(float4 channels[4], float materialAmbientOcclusion)
{
    PbrGBufferData result = (PbrGBufferData)0;
    result.material.baseColor = max(channels[0].rgb, 0.0f);
    result.material.opacity = saturate(channels[0].a);
    result.geometricNormal = DecodeOctahedralNormal(channels[1].rg);
    float ior = lerp(1.0f, 3.0f, saturate(channels[1].b));
    result.material.dielectricF0 = IorToF0(ior);
    result.material.featureMask = (uint)round(saturate(channels[1].a) * 255.0f);
    result.shadingNormal = PbrSafeNormalize(channels[2].xyz, result.geometricNormal);
    if (dot(result.shadingNormal, result.geometricNormal) < 0.0f)
        result.shadingNormal = -result.shadingNormal;
    result.material.perceptualRoughness = saturate(channels[2].w);
    result.material.emissive = max(channels[3].rgb, 0.0f);
    result.material.metalness = saturate(channels[3].a);
    result.ambientOcclusion = saturate(materialAmbientOcclusion);
    return result;
}

#endif // UVSR_PBR_GBUFFER_HLSLI
