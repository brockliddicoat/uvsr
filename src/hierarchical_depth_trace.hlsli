#ifndef UVSR_HIERARCHICAL_DEPTH_TRACE_HLSLI
#define UVSR_HIERARCHICAL_DEPTH_TRACE_HLSLI

// independently implemented interval traversal. see the research design notes
// for the AMD SSSR cell-boundary algorithm and perspective-correct DDA reference.
struct HzbScreenRay
{
    float2 beginPixel;
    float2 endPixel;
    float beginDepth;
    float endDepth;
    uint orthographic;
};

float HzbRayDepth(HzbScreenRay ray, float t)
{
    return ray.orthographic != 0u
        ? lerp(ray.beginDepth, ray.endDepth, t)
        : rcp(lerp(rcp(ray.beginDepth), rcp(ray.endDepth), t));
}

uint2 HzbCell(float2 pixel, uint mip, uint2 fullSize)
{
    return min(uint2(max(pixel, 0.0f)) >> mip,
        max(fullSize >> mip, 1u) - 1u);
}

void HzbCellBounds(uint2 cell, uint mip, uint2 fullSize,
    out float2 low, out float2 high)
{
    uint2 size = max(fullSize >> mip, 1u);
    low = float2(cell << mip);
    high = float2((cell + 1u) << mip);
    if (cell.x + 1u == size.x) high.x = float(fullSize.x);
    if (cell.y + 1u == size.y) high.y = float(fullSize.y);
}

float HzbCellExit(HzbScreenRay ray, float2 low, float2 high)
{
    float2 delta = ray.endPixel - ray.beginPixel;
    float tx = abs(delta.x) > 1e-8f
        ? ((delta.x > 0.0f ? high.x : low.x) - ray.beginPixel.x) / delta.x
        : 1.0f;
    float ty = abs(delta.y) > 1e-8f
        ? ((delta.y > 0.0f ? high.y : low.y) - ray.beginPixel.y) / delta.y
        : 1.0f;
    return min(1.0f, min(tx, ty));
}

bool TraceHierarchicalDepth(Texture2D<float2> bounds, HzbScreenRay ray,
    uint2 fullSize, uint mipCount, float thickness, out float2 hitPixel)
{
    hitPixel = ray.endPixel;
    float2 delta = ray.endPixel - ray.beginPixel;
    float pixelLength = max(abs(delta.x), abs(delta.y));
    if (!(pixelLength > 1e-5f) || ray.beginDepth <= 0.0f || ray.endDepth <= 0.0f)
        return false;
    // skip exactly the receiver cell, including a shifted projected origin.
    float2 receiverLow, receiverHigh;
    HzbCellBounds(HzbCell(ray.beginPixel, 0u, fullSize), 0u, fullSize,
        receiverLow, receiverHigh);
    float t = max(0.0f, HzbCellExit(ray, receiverLow, receiverHigh));
    float2 directionNudge = sign(delta) * 0.0001f;
    uint mip = 0u;
    [loop]
    while (t < 1.0f)
    {
        float2 pixel = lerp(ray.beginPixel, ray.endPixel, t);
        if (any(pixel < 0.0f) || any(pixel >= float2(fullSize)))
            return false;
        uint2 cell = HzbCell(pixel + directionNudge, mip, fullSize);
        float2 low, high;
        HzbCellBounds(cell, mip, fullSize, low, high);
        float exitT = max(t, HzbCellExit(ray, low, high));
        float z0 = HzbRayDepth(ray, t);
        float z1 = HzbRayDepth(ray, exitT);
        float2 node = bounds.Load(int3(cell, mip));
        bool possible = node.x <= node.y &&
            max(z0, z1) >= node.x && min(z0, z1) <= node.y + thickness;
        if (possible && mip > 0u)
        {
            --mip;
            continue;
        }
        if (possible)
        {
            hitPixel = float2(cell) + 0.5f;
            return true;
        }
        if (exitT >= 1.0f)
            return false;
        t = max(exitT, asfloat(asuint(t) + 1u));
        mip = min(mip + 1u, mipCount - 1u);
    }
    // leaving the known screen segment supplies no additional occlusion.
    return false;
}

#endif
