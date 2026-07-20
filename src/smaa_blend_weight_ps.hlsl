//
// Source-faithful SMAA blending-weight pass. SMAA_QUALITY_PRESET is a static
// PSO dimension mapped exactly to the pinned Low/Medium/High/Ultra presets.
//

#pragma pack_matrix(row_major)

cbuffer SmaaConstants : register(b0)
{
    float4 SmaaRtMetrics;
    float4 SmaaSubsampleIndices;
}

#include "smaa_quality_config.hlsli"

#define SMAA_HLSL_4 1
#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1
#include "third_party/smaa/SMAA.hlsl"

// Keep the resource types identical to upstream's SMAATexture2D macro. The
// physical lookup/edge formats may expose fewer channels, but D3D supplies the
// missing components through the normal texture-format expansion rules.
Texture2D<float4> EdgesTexture : register(t0);
Texture2D<float4> AreaTexture : register(t1);
Texture2D<float4> SearchTexture : register(t2);

float4 main(
    in float4 position : SV_Position,
    in float2 uv : UV) : SV_Target
{
    float2 pixelCoordinate;
    float4 offset[3];
    SMAABlendingWeightCalculationVS(
        uv,
        pixelCoordinate,
        offset);
    return SMAABlendingWeightCalculationPS(
        uv,
        pixelCoordinate,
        offset,
        EdgesTexture,
        AreaTexture,
        SearchTexture,
        SmaaSubsampleIndices);
}
