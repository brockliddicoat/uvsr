#ifndef UVSR_SCREEN_SPACE_VISIBILITY_COMMON_HLSLI
#define UVSR_SCREEN_SPACE_VISIBILITY_COMMON_HLSLI

// Shared helpers for the screen-space visibility shader family: the sampling
// pass, the depth-hierarchy prefilter, temporal reconstruction, and the
// bilateral filter. Each of these helpers reads the c_Visibility constant
// buffer's g_Visibility global, so this header must be included after the
// cbuffer declaration in every consuming shader. Consolidating the definitions
// keeps the depth-validity convention, the sampling->full-resolution remap, and
// the guarded normalize identical across passes instead of relying on hand-kept
// copies staying in sync.

// Device-depth validity respecting the active depth convention. Forward depth
// is [0, 1) with the far plane at 1; reverse depth is (0, 1] with the near
// plane at 1. Non-finite depths are always rejected so downstream
// reconstruction never consumes NaN/Inf from cleared or unwritten texels.
bool IsValidDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Visibility.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

// Map a sampling-resolution pixel to the full-resolution guide texel at the
// center of its scale footprint, clamped to the last valid full-resolution
// texel so gather reads never leave the image when the sampling grid does not
// divide the full resolution evenly.
uint2 SamplingToFullPixel(uint2 samplingPixel)
{
    uint scale = max(g_Visibility.resolutionScale, 1u);
    uint2 fullSize = uint2(g_Visibility.fullResolution);
    return min(samplingPixel * scale + scale / 2u, fullSize - 1u);
}

// Normalize with a guard against zero-length and non-finite inputs, returning
// the caller's fallback direction when the vector cannot be normalized. The
// 1e-12 length-squared floor rejects vectors below roughly 1e-6 in magnitude.
float3 SafeNormal(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12f && isfinite(lengthSquared)
        ? value * rsqrt(lengthSquared)
        : fallback;
}

#endif // UVSR_SCREEN_SPACE_VISIBILITY_COMMON_HLSLI
