//
// Shared SMAA neighborhood pass. The spatially filtered result is a
// presentation texture selected by the CPU; long-term TAA never reads it back
// as temporal history.
//

#pragma pack_matrix(row_major)

cbuffer SmaaConstants : register(b0)
{
    float4 SmaaRtMetrics;
    float4 SmaaSubsampleIndices;
}

#define SMAA_HLSL_4 1
#define SMAA_PRESET_HIGH 1
#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1
#include "third_party/smaa/SMAA.hlsl"

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float4> BlendTexture : register(t1);

float4 main(
    in float4 position : SV_Position,
    in float2 uv : UV) : SV_Target
{
    float4 offset;
    SMAANeighborhoodBlendingVS(uv, offset);
    return SMAANeighborhoodBlendingPS(
        uv,
        offset,
        ColorTexture,
        BlendTexture);
}
