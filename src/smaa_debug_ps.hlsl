//
// Developer-only static SMAA diagnostics. This shader is intentionally absent
// from shaders_production.cfg, and every view is a separate PSO permutation.
//

#ifndef SMAA_DEBUG_VIEW
#error SMAA_DEBUG_VIEW must be a compile-time shader define
#endif

Texture2D<float4> SourceColor : register(t0);
Texture2D<float2> EdgeMask : register(t1);
Texture2D<float4> BlendWeights : register(t2);
Texture2D<float4> FilteredColor : register(t3);

float4 main(in float4 position : SV_Position) : SV_Target
{
    const uint2 pixel = uint2(position.xy);

#if SMAA_DEBUG_VIEW == 0
    const float2 edges = saturate(EdgeMask.Load(int3(pixel, 0)));
    // Red and green preserve the two stored edge orientations; yellow marks
    // pixels where both orientations are active.
    return float4(edges.x, edges.y, 0.0, 1.0);
#elif SMAA_DEBUG_VIEW == 1
    const float4 weights =
        saturate(BlendWeights.Load(int3(pixel, 0)));
    // The upstream weight texture stores the vertical-neighbor pair in RG and
    // the horizontal-neighbor pair in BA. Blue makes even one weak direction
    // visible.
    const float horizontal = saturate(weights.b + weights.a);
    const float vertical = saturate(weights.r + weights.g);
    const float strongest = max(
        max(weights.r, weights.g),
        max(weights.b, weights.a));
    return float4(horizontal, vertical, strongest, 1.0);
#elif SMAA_DEBUG_VIEW == 2
    const float3 source = SourceColor.Load(int3(pixel, 0)).rgb;
    const float3 filtered = FilteredColor.Load(int3(pixel, 0)).rgb;
    const float3 difference =
        all(isfinite(source)) && all(isfinite(filtered))
            ? abs(filtered - source)
            : 0.0;
    // SMAA corrections are deliberately subpixel-small. A fixed 16x gain
    // reveals their RGB footprint without changing the production result.
    return float4(saturate(difference * 16.0), 1.0);
#else
#error Unsupported SMAA_DEBUG_VIEW
#endif
}
