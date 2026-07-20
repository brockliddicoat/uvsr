#include <donut/shaders/binding_helpers.hlsli>

#ifndef MSAA_VISIBILITY_SAMPLES
#error MSAA_VISIBILITY_SAMPLES must be a static 2, 4, 8, or 16.
#endif

#if MSAA_VISIBILITY_SAMPLES != 2 && \
    MSAA_VISIBILITY_SAMPLES != 4 && \
    MSAA_VISIBILITY_SAMPLES != 8 && \
    MSAA_VISIBILITY_SAMPLES != 16
#error Unsupported MSAA visibility sample count.
#endif

Texture2DMS<float, MSAA_VISIBILITY_SAMPLES> t_Depth : register(t0);
Texture2DMS<float4, MSAA_VISIBILITY_SAMPLES> t_Diffuse : register(t1);
Texture2DMS<float4, MSAA_VISIBILITY_SAMPLES> t_Material : register(t2);
Texture2DMS<float4, MSAA_VISIBILITY_SAMPLES> t_Normals : register(t3);
Texture2DMS<float4, MSAA_VISIBILITY_SAMPLES> t_Emissive : register(t4);
Texture2DMS<float, MSAA_VISIBILITY_SAMPLES>
    t_MaterialAmbientOcclusion : register(t5);
Texture2DMS<float4, MSAA_VISIBILITY_SAMPLES>
    t_MotionVectors : register(t6);

VK_IMAGE_FORMAT("r32f")
RWTexture2D<float> u_Depth : register(u0);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_Diffuse : register(u1);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_Material : register(u2);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_Normals : register(u3);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_Emissive : register(u4);
VK_IMAGE_FORMAT("r16f")
RWTexture2D<float> u_MaterialAmbientOcclusion : register(u5);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_MotionVectors : register(u6);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    uint width;
    uint height;
    uint samples;
    t_Depth.GetDimensions(width, height, samples);
    if (pixel.x >= width || pixel.y >= height)
        return;

    // UVSR's renderer always constructs the PlanarView with reverse-Z. Zero
    // is cleared background, and the greatest finite positive raw depth is
    // therefore the closest covered surface. A nonzero normal is the same
    // ownership test used by the per-sample deferred-lighting kernel.
    bool foundSurface = false;
    uint owner = 0u;
    float closestDepth = 0.0f;
    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < MSAA_VISIBILITY_SAMPLES;
        ++sampleIndex)
    {
        const float depth =
            t_Depth.Load(pixel, sampleIndex);
        const float3 normal =
            t_Normals.Load(pixel, sampleIndex).xyz;
        const bool valid =
            isfinite(depth) &&
            depth > 0.0f &&
            dot(normal, normal) > 1e-12f;
        if (valid &&
            (!foundSurface || depth > closestDepth))
        {
            foundSurface = true;
            closestDepth = depth;
            owner = sampleIndex;
        }
    }

    if (!foundSurface)
    {
        u_Depth[pixel] = 0.0f;
        u_Diffuse[pixel] = 0.0f;
        u_Material[pixel] = 0.0f;
        u_Normals[pixel] = 0.0f;
        u_Emissive[pixel] = 0.0f;
        u_MaterialAmbientOcclusion[pixel] = 1.0f;
        u_MotionVectors[pixel] = 0.0f;
        return;
    }

    // Copy every guide from one owner. Mixing an averaged normal with the
    // closest depth or interpolating material IDs is precisely what makes
    // conventional attribute resolves invalid at silhouettes.
    u_Depth[pixel] = closestDepth;
    u_Diffuse[pixel] = t_Diffuse.Load(pixel, owner);
    u_Material[pixel] = t_Material.Load(pixel, owner);
    u_Normals[pixel] = t_Normals.Load(pixel, owner);
    u_Emissive[pixel] = t_Emissive.Load(pixel, owner);
    u_MaterialAmbientOcclusion[pixel] =
        t_MaterialAmbientOcclusion.Load(pixel, owner);
    const float4 motion =
        t_MotionVectors.Load(pixel, owner);
    u_MotionVectors[pixel] =
        all(isfinite(motion)) ? motion : 0.0f;
}
