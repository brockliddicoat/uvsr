#include "svgf_common.hlsli"

// this pass alone translates UVSR's device depth, world normals, and pixel
// displacement. the subsequent passes consume a private Falcor-style contract.
float viewDepth(float deviceDepth, float2 pixelCenter, PlanarViewConstants view)
{
    float2 uv = (pixelCenter - view.viewportOrigin) * view.viewportSizeInv;
    float4 position = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, deviceDepth, 1), view.matClipToView);
    return abs(position.z / position.w);
}
float depthAt(int2 p)
{
    uint2 full = surfacePixel(clamp(p, 0, int2(g.extent) - 1));
    float d = t_Input[full].x;
    bool valid = isfinite(d) && (g.reverseDepth != 0 ? d > 0 && d <= 1 : d >= 0 && d < 1);
    return valid ? viewDepth(d, float2(full) + 0.5, g.currentView) : -1;
}
float3 normalAt(int2 p)
{
    float3 n = t_Surface[surfacePixel(clamp(p, 0, int2(g.extent) - 1))].xyz;
    return all(isfinite(n)) && dot(n, n) > 1e-10 ? normalize(n) : float3(0, 0, 1);
}
float gridCoordinate(float p, uint stride, uint fullExtent, uint extent)
{
    if (extent <= 1) return 0;
    float last = min((extent - 1u) * stride + stride / 2u, fullExtent - 1u);
    float beforeLast = (extent - 2u) * stride + stride / 2u;
    return p > beforeLast ? float(extent - 2u) + (p - beforeLast) / (last - beforeLast)
        : (p - float(stride / 2u)) / float(stride);
}

[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID)
{
    if (!inside(p)) return;
    float z = depthAt(p);
    float3 normal = normalAt(p);
    float2 derivatives = t_Reprojection[surfacePixel(p)].xy;
    float fwidthZ = derivatives.x;
    float normalFwidth = derivatives.y;
    u_Output[p] = float4(z, fwidthZ, packNormal(normal));
    uint2 full = surfacePixel(p);
    float4 motion = t_PreviousSurface[full];
    float4 correspondence = float4(0, 0, -1, normalFwidth);
    if (g.reset == 0 && z >= 0 && motion.w > 0 && all(isfinite(motion)))
    {
        float2 previousCenter = float2(full) + 0.5 + motion.xy;
        float previousDeviceDepth = t_Input[full].x + motion.z;
        float previousZ = viewDepth(previousDeviceDepth, previousCenter, g.previousView);
        if (previousDeviceDepth >= 0 && previousDeviceDepth <= 1 && isfinite(previousZ))
            correspondence = float4(
                gridCoordinate(previousCenter.x - 0.5, g.sampleStride.x, g.surfaceExtent.x, g.extent.x),
                gridCoordinate(previousCenter.y - 0.5, g.sampleStride.y, g.surfaceExtent.y, g.extent.y),
                previousZ, normalFwidth);
    }
    u_Second[p] = correspondence;
}
