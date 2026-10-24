#ifndef UVSR_DENOISING_COMMON_HLSLI
#define UVSR_DENOISING_COMMON_HLSLI

#pragma pack_matrix(row_major)
#include "renderer_gpu_helpers.hlsli"
#include "denoising_cb.h"

cbuffer c_Denoising : register(b0) { DenoisingConstants g_Denoising; };
Texture2D<float4> t_Color : register(t0);
Texture2D<float4> t_Guide : register(t1);
Texture2D<float4> t_PreviousGuide : register(t2);
Texture2D<float4> t_Reprojection : register(t3);
Texture2D<float4> t_History : register(t4);
Texture2D<float4> t_Moments : register(t5);
Texture2D<float4> t_Gradient : register(t6);
Texture2D<float4> t_FullDepth : register(t7);
Texture2D<float4> t_FullNormal : register(t8);
Texture2D<float4> t_Geometry : register(t9);
Texture2D<float4> t_Modulation : register(t10);
Texture2D<float4> t_Emission : register(t11);
Texture2D<float4> t_RawSource : register(t12);

bool DenoisingInside(int2 p) { return all(p >= 0) && all(p < int2(g_Denoising.size)); }
int2 DenoisingClamp(int2 p) { return clamp(p, 0, int2(g_Denoising.size) - 1); }
float4 DenoisingColor(int2 p) { return t_Color[DenoisingClamp(p)]; }
float4 DenoisingGuide(int2 p) { return t_Guide[DenoisingClamp(p)]; }
float DenoisingLuminance(float3 color) { return dot(color, float3(0.2126f, 0.7152f, 0.0722f)); }
float3 DenoisingFinite(float3 color) { return all(isfinite(color)) ? max(color, 0.0f) : 0.0f; }

uint2 DenoisingFullPixel(uint2 p)
{
    uint2 scale = (g_Denoising.fullSize + g_Denoising.size - 1u) / g_Denoising.size;
    return min(p * scale + scale / 2u, g_Denoising.fullSize - 1u);
}

float2 DenoisingGridPosition(float2 fullPixel)
{
    uint2 scale = (g_Denoising.fullSize + g_Denoising.size - 1u) / g_Denoising.size;
    float2 position = (fullPixel - float2(scale / 2u)) / float2(scale);
    // the final sampling cell can be shorter at an odd image boundary.
    uint2 previous = max(g_Denoising.size, 2u) - 2u;
    float2 beforeLast = float2(previous * scale + scale / 2u);
    float2 last = float2(DenoisingFullPixel(g_Denoising.size - 1u));
    float2 tail = float2(previous) + (fullPixel - beforeLast) / max(last - beforeLast, 1.0f);
    position = select(fullPixel > beforeLast, tail, position);
    return select(g_Denoising.size > 1u, position, 0.0f);
}

uint2 DenoisingSourcePixel(uint2 fullPixel)
{
    uint2 scale = (g_Denoising.fullSize + g_Denoising.sourceSize - 1u) / g_Denoising.sourceSize;
    return min(fullPixel / scale, g_Denoising.sourceSize - 1u);
}

float4 DenoisingDecodeSample(uint2 fullPixel, float4 raw)
{
    bool valid = all(isfinite(raw.xyz)) && (g_Denoising.sampleValidity == 0u || raw.w > 0.0f);
    float3 color = g_Denoising.scalarSignal != 0u ? raw.xxx : raw.xyz;
    if (g_Denoising.materialModulation != 0u)
        color = max(color - t_Emission[fullPixel].xyz, 0.0f) / max(t_Modulation[fullPixel].xyz, 0.001f);
    return float4(DenoisingFinite(color), valid ? 1.0f : 0.0f);
}

float4 DenoisingSamplePoint(uint2 fullPixel)
{
    return DenoisingDecodeSample(fullPixel, t_Color[DenoisingSourcePixel(fullPixel)]);
}

bool DenoisingDepthValid(float depth)
{
    return isfinite(depth) && (g_Denoising.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f : depth >= 0.0f && depth < 1.0f);
}

float3 DenoisingGeometricNormal(uint2 fullPixel, float3 fallback)
{
    if (g_Denoising.geometryGuide == 0u) return fallback;
    float3 normal = t_Geometry[fullPixel].xyz;
    if (g_Denoising.geometryGuide != 0u)
    {
        float2 encoded = normal.xy * 2.0f - 1.0f;
        normal = float3(encoded, 1.0f - abs(encoded.x) - abs(encoded.y));
        normal.xy -= select(normal.xy >= 0.0f, 1.0f, -1.0f) * saturate(-normal.z);
    }
    float lengthSquared = dot(normal, normal);
    return isfinite(lengthSquared) && lengthSquared > 1e-12f ? normal * rsqrt(lengthSquared) : fallback;
}

float4 DenoisingMakeGuide(uint2 p, float depth, float3 normal)
{
    float lengthSquared = dot(normal, normal);
    if (!DenoisingDepthValid(depth) || !isfinite(lengthSquared) || lengthSquared < 1e-12f) return 0.0f;
    normal *= rsqrt(lengthSquared);
    float3 geometry = DenoisingGeometricNormal(p, normal);
    if (dot(normal, geometry) < 0.0f) normal = -normal;
    float3 position = ReconstructViewPosition(g_Denoising.view, float2(p) + 0.5f, depth);
    return all(isfinite(position)) ? float4(normal, abs(position.z)) : 0.0f;
}

float3 DenoisingViewRay(float2 fullPixel)
{
    float3 position = ReconstructViewPosition(g_Denoising.view, fullPixel + 0.5f, 0.5f);
    return position / max(abs(position.z), 1e-8f);
}

float3 DenoisingSampleRay(uint2 fullPixel)
{
    float2 offset = g_Denoising.geometryGuide == 2u ? t_Geometry[fullPixel].zw - 0.5f : 0.0f;
    return DenoisingViewRay(float2(fullPixel) + offset);
}

struct DenoisingSurface
{
    float4 guide;
    float4 plane;
    float tolerance;
};

DenoisingSurface DenoisingMakeSurface(uint2 fullPixel, float4 guide)
{
    DenoisingSurface surface;
    surface.guide = guide;
    float3 normal = DenoisingGeometricNormal(fullPixel, guide.xyz);
    float3 normalVS = mul(float4(normal, 0.0f), g_Denoising.view.matWorldToView).xyz;
    float3 ray = DenoisingSampleRay(fullPixel);
    surface.plane = float4(normalVS, -dot(normalVS, ray * guide.w));
    float2 scale = float2((g_Denoising.fullSize + g_Denoising.size - 1u) / g_Denoising.size);
    float3 centerRay = DenoisingViewRay(float2(fullPixel));
    float footprint = guide.w * max(length(DenoisingViewRay(float2(fullPixel) + float2(scale.x, 0)) - centerRay),
        length(DenoisingViewRay(float2(fullPixel) + float2(0, scale.y)) - centerRay));
    surface.tolerance = max(footprint * g_Denoising.settings.depthPhi, max(guide.w * 1e-6f, 1e-6f));
    return surface;
}

float DenoisingSurfaceWeight(DenoisingSurface center, uint2 fullPixel, float4 tap)
{
    if (!(center.guide.w > 0.0f) || !(tap.w > 0.0f)) return 0.0f;
    // compare distance from the geometric plane, not camera-axis depth.
    // points on a sloped plane then remain connected in every direction.
    float3 position = DenoisingSampleRay(fullPixel) * tap.w;
    float distance = abs(dot(center.plane, float4(position, 1.0f)));
    return exp(-distance / center.tolerance) *
        pow(saturate(dot(center.guide.xyz, tap.xyz)), g_Denoising.settings.normalPhi);
}

float4 DenoisingSample(uint2 fullPixel)
{
    if (all(g_Denoising.size == g_Denoising.sourceSize)) return DenoisingSamplePoint(fullPixel);
    uint2 filterScale = (g_Denoising.fullSize + g_Denoising.size - 1u) / g_Denoising.size;
    uint2 sourceScale = (g_Denoising.fullSize + g_Denoising.sourceSize - 1u) / g_Denoising.sourceSize;
    uint2 origin = fullPixel / filterScale * filterScale;
    float4 guide = DenoisingMakeGuide(fullPixel, t_FullDepth[fullPixel].x, t_FullNormal[fullPixel].xyz);
    DenoisingSurface surface = DenoisingMakeSurface(fullPixel, guide);
    float3 color = 0.0f;
    float total = 0.0f;
    // every source observation in the cell contributes before reducing size.
    [loop] for (uint y = 0; y < filterScale.y; y += sourceScale.y)
    [loop] for (uint x = 0; x < filterScale.x; x += sourceScale.x)
    {
        uint2 tap = origin + uint2(x, y) + sourceScale / 2u;
        if (any(tap >= g_Denoising.fullSize)) continue;
        float4 sample = DenoisingSamplePoint(tap);
        if (sample.w <= 0.0f) continue;
        float4 tapGuide = DenoisingMakeGuide(tap, t_FullDepth[tap].x, t_FullNormal[tap].xyz);
        float weight = guide.w > 0.0f ? DenoisingSurfaceWeight(surface, tap, tapGuide) : (tapGuide.w <= 0.0f ? 1.0f : 0.0f);
        color += sample.xyz * weight;
        total += weight;
    }
    return total > 1e-6f ? float4(color / total, 1.0f) : DenoisingSamplePoint(fullPixel);
}

#endif
