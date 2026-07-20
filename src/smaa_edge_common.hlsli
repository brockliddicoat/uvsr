//
// Shared color-edge calculation for fullscreen and compacted SMAA paths.
// The edge decisions must remain identical; only their dispatch topology
// differs.
//

float3 SmaaEdgeColor(float3 sceneLinearHdr)
{
    // Upstream thresholds are calibrated for finite presentation color. With
    // no tonemapper, clamp scene-linear color before applying the exact IEC
    // 61966-2-1 transfer. Neighborhood blending still filters untouched HDR.
    sceneLinearHdr.r = isfinite(sceneLinearHdr.r)
        ? sceneLinearHdr.r
        : 0.0;
    sceneLinearHdr.g = isfinite(sceneLinearHdr.g)
        ? sceneLinearHdr.g
        : 0.0;
    sceneLinearHdr.b = isfinite(sceneLinearHdr.b)
        ? sceneLinearHdr.b
        : 0.0;
    sceneLinearHdr = saturate(sceneLinearHdr);
    const float3 low = 12.92 * sceneLinearHdr;
    const float3 high =
        1.055 * pow(sceneLinearHdr, 1.0 / 2.4) - 0.055;
    return float3(
        sceneLinearHdr.r <= 0.0031308 ? low.r : high.r,
        sceneLinearHdr.g <= 0.0031308 ? low.g : high.g,
        sceneLinearHdr.b <= 0.0031308 ? low.b : high.b);
}

float2 SmaaColorEdgeDetection(
    Texture2D<float4> colorTexture,
    SamplerState pointSampler,
    float2 uv,
    float4 offset[3])
{
    // This is upstream SMAAColorEdgeDetectionPS with only its texture fetches
    // adapted through SmaaEdgeColor for UVSR's scene-linear HDR contract.
    const float2 threshold =
        float2(SMAA_THRESHOLD, SMAA_THRESHOLD);
    float4 delta;
    const float3 center = SmaaEdgeColor(
        colorTexture.SampleLevel(pointSampler, uv, 0).rgb);
    const float3 left = SmaaEdgeColor(
        colorTexture.SampleLevel(pointSampler, offset[0].xy, 0).rgb);
    float3 difference = abs(center - left);
    delta.x = max(max(difference.r, difference.g), difference.b);

    const float3 top = SmaaEdgeColor(
        colorTexture.SampleLevel(pointSampler, offset[0].zw, 0).rgb);
    difference = abs(center - top);
    delta.y = max(max(difference.r, difference.g), difference.b);

    float2 edges = step(threshold, delta.xy);
    if (dot(edges, float2(1.0, 1.0)) == 0.0)
        return 0.0;

    const float3 right = SmaaEdgeColor(
        colorTexture.SampleLevel(pointSampler, offset[1].xy, 0).rgb);
    difference = abs(center - right);
    delta.z = max(max(difference.r, difference.g), difference.b);

    const float3 bottom = SmaaEdgeColor(
        colorTexture.SampleLevel(pointSampler, offset[1].zw, 0).rgb);
    difference = abs(center - bottom);
    delta.w = max(max(difference.r, difference.g), difference.b);

    float2 maximumDelta = max(delta.xy, delta.zw);
    const float3 leftLeft = SmaaEdgeColor(
        colorTexture.SampleLevel(pointSampler, offset[2].xy, 0).rgb);
    difference = abs(center - leftLeft);
    delta.z = max(max(difference.r, difference.g), difference.b);

    const float3 topTop = SmaaEdgeColor(
        colorTexture.SampleLevel(pointSampler, offset[2].zw, 0).rgb);
    difference = abs(center - topTop);
    delta.w = max(max(difference.r, difference.g), difference.b);

    maximumDelta = max(maximumDelta, delta.zw);
    const float finalDelta = max(maximumDelta.x, maximumDelta.y);
    edges *= step(
        finalDelta,
        SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);
    return edges;
}
