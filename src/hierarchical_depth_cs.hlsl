#include "hierarchical_depth_cb.h"

cbuffer Constants : register(b0) { HierarchicalDepthConstants g_Depth; };
Texture2D<float> t_Depth : register(t0);
Texture2D<float2> t_Previous : register(t1);
RWTexture2D<float2> u_Bounds : register(u0);

// an empty interval stays empty under min/max reduction and never occludes.
static const float2 EmptyDepthBounds = float2(3.402823466e+38f, -3.402823466e+38f);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Depth.outputSize))
        return;
    float2 bounds = EmptyDepthBounds;
    if (g_Depth.initialize != 0u)
    {
        float depth = t_Depth[pixel];
        bool valid = isfinite(depth) && (g_Depth.reverseDepth != 0u
            ? depth > 0.0f && depth <= 1.0f
            : depth >= 0.0f && depth < 1.0f);
        float4 p = g_Depth.projectionDepth;
        float denominator = depth * p.y - p.x;
        if (valid && abs(denominator) > 1e-20f)
        {
            float z = abs((p.z - depth * p.w) / denominator);
            if (isfinite(z) && z > 0.0f)
                bounds = z.xx;
        }
    }
    else
    {
        uint2 begin = pixel * 2u;
        // hardware mip sizes round down. the last cell absorbs the odd tail;
        // simply reading a 2x2 footprint would silently discard edge occluders.
        uint2 end = min(begin + 2u, g_Depth.sourceSize);
        if (pixel.x + 1u == g_Depth.outputSize.x) end.x = g_Depth.sourceSize.x;
        if (pixel.y + 1u == g_Depth.outputSize.y) end.y = g_Depth.sourceSize.y;
        for (uint y = begin.y; y < end.y; ++y)
            for (uint x = begin.x; x < end.x; ++x)
            {
                float2 child = t_Previous.Load(int3(x, y, 0));
                bounds = float2(min(bounds.x, child.x), max(bounds.y, child.y));
            }
    }
    u_Bounds[pixel] = bounds;
}
