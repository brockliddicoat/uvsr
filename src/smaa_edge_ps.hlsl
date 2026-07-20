//
// UVSR wrapper around the original SMAA 1x edge pass.
// Pattern detection, local-contrast adaptation, weight lookup, diagonal
// handling, corner handling, and neighborhood blending remain in the pinned
// upstream module under third_party/smaa.
//

#pragma pack_matrix(row_major)

cbuffer SmaaConstants : register(b0)
{
    float4 SmaaRtMetrics;
    float4 SmaaSubsampleIndices;
}

#include "smaa_edge_config.hlsli"

#define SMAA_HLSL_4 1
#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1
#include "third_party/smaa/SMAA.hlsl"

Texture2D<float4> ColorTexture : register(t0);

#include "smaa_edge_common.hlsli"

float2 main(
    in float4 position : SV_Position,
    in float2 uv : UV) : SV_Target
{
    float4 offset[3];
    SMAAEdgeDetectionVS(uv, offset);

    const float2 edges = SmaaColorEdgeDetection(
        ColorTexture,
        PointSampler,
        uv,
        offset);
    if (dot(edges, float2(1.0, 1.0)) == 0.0)
        discard;
    return edges;
}
