/*
* Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// UVSR modification and exact provenance:
// Adapted from donut/include/donut/shaders/gbuffer_cb.h at pinned Donut commit
// bc1ea24b0486f1c00d89327fe16c0b4dd11c5937 for UVSR source base
// 578496047c4753077e36f39a68df8c2c323cee58. The original seven-word
// buffer-load contract is unchanged and geometryLocalIndex is appended as the
// eighth word. UVSR appends a ninth word for the non-identity compact
// visibility class; SurfaceKey packing remains exactly two uints.

#ifndef UVSR_ADAPTIVE_MSAA_RASTER_CB_H
#define UVSR_ADAPTIVE_MSAA_RASTER_CB_H

#define ADAPTIVE_MSAA_BINDING_GEOMETRY_INSTANCE_HANDLE_MAP 12

#define ADAPTIVE_MSAA_INVALID_GEOMETRY_LOCAL_INDEX 0xffffffffu
#define ADAPTIVE_MSAA_INVALID_GEOMETRY_INSTANCE_HANDLE 0xffffffffu
// Exact RTV-clear encoding. Generation zero is reserved, so this cannot be a
// valid SurfaceKey handle. Keep logical invalid/stale sentinels at all-ones.
#define ADAPTIVE_MSAA_CLEARED_VISIBILITY_WORD 0u
#define ADAPTIVE_MSAA_GEOMETRY_INSTANCE_SLOT_BITS 20u
#define ADAPTIVE_MSAA_GEOMETRY_INSTANCE_SLOT_MASK 0x000fffffu
#define ADAPTIVE_MSAA_GEOMETRY_INSTANCE_GENERATION_SHIFT 20u
#define ADAPTIVE_MSAA_GEOMETRY_INSTANCE_GENERATION_MASK 0x00000fffu
#define ADAPTIVE_MSAA_GEOMETRY_INSTANCE_MIN_GENERATION 1u
#define ADAPTIVE_MSAA_GEOMETRY_INSTANCE_MAX_GENERATION 0x00000ffeu

// This marker is deliberately separate from SurfaceKey. It selects the
// reconstruction path after final depth testing and may carry a stable
// FallbackReason for diagnostics. The low six bits fit the frozen reason ABI
// (currently 0..23), bit 6 proves a CPU-local/per-view fallback from an
// otherwise reconstructable snapshot record, and bit 7 distinguishes every
// fallback from the two non-fallback values. Neither provenance bit belongs to
// SurfaceKey. R8_UINT is therefore lossless; R32_UINT is the validated format
// fallback when a device cannot render multisampled R8_UINT.
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND 0u
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT 1u
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_FALLBACK_BIT 0x40u
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_FALLBACK_BIT 0x80u
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_REASON_MASK 0x3fu
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_MAX_REASON 23u
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_UNSUPPORTED_DEFORMATION 15u
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_UNSUPPORTED_MATERIAL 16u
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_RECONSTRUCTION_FAILURE 18u
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_TEXTURE_QUALIFICATION_MISMATCH 23u
#define ADAPTIVE_MSAA_VISIBILITY_CLASS_INVALID 0xffu

#ifdef __cplusplus
#define UVSR_ADAPTIVE_MSAA_RASTER_UINT unsigned int

struct AdaptiveMsaaVisibilityClassDecodeResult
{
    bool valid = false;
    bool reconstruct = false;
    bool fallback = false;
    bool localFallback = false;
    unsigned int fallbackReason = 0u;
};

[[nodiscard]] constexpr bool AdaptiveMsaaIsLocalFallbackReason(
    unsigned int fallbackReason)
{
    return fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_UNSUPPORTED_DEFORMATION ||
        fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_UNSUPPORTED_MATERIAL ||
        fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_RECONSTRUCTION_FAILURE ||
        fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_TEXTURE_QUALIFICATION_MISMATCH;
}

[[nodiscard]] constexpr unsigned int
    AdaptiveMsaaEncodeFallbackVisibilityClass(
        unsigned int fallbackReason,
        bool localFallback)
{
    return fallbackReason > 0u &&
            fallbackReason <= ADAPTIVE_MSAA_VISIBILITY_CLASS_MAX_REASON
        && (!localFallback ||
            AdaptiveMsaaIsLocalFallbackReason(fallbackReason))
        ? ADAPTIVE_MSAA_VISIBILITY_CLASS_FALLBACK_BIT |
            (localFallback
                ? ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_FALLBACK_BIT
                : 0u) |
            fallbackReason
        : ADAPTIVE_MSAA_VISIBILITY_CLASS_INVALID;
}

[[nodiscard]] constexpr AdaptiveMsaaVisibilityClassDecodeResult
    AdaptiveMsaaDecodeVisibilityClass(unsigned int visibilityClass)
{
    AdaptiveMsaaVisibilityClassDecodeResult result;
    result.reconstruct =
        visibilityClass == ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT;
    result.fallback =
        (visibilityClass & ADAPTIVE_MSAA_VISIBILITY_CLASS_FALLBACK_BIT) != 0u;
    result.localFallback = result.fallback &&
        (visibilityClass &
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_FALLBACK_BIT) != 0u;
    result.fallbackReason =
        visibilityClass & ADAPTIVE_MSAA_VISIBILITY_CLASS_REASON_MASK;
    result.valid = result.reconstruct ||
        (visibilityClass <= 0xffu && result.fallback &&
            result.fallbackReason > 0u &&
            result.fallbackReason <=
                ADAPTIVE_MSAA_VISIBILITY_CLASS_MAX_REASON &&
            (!result.localFallback ||
                AdaptiveMsaaIsLocalFallbackReason(
                    result.fallbackReason)));
    return result;
}

static constexpr float AdaptiveMsaaCompactMinimumClipW =
    1.17549435082228750797e-38f;
static constexpr float AdaptiveMsaaCompactMaximumClipW =
    4.25352958651173103504e37f;
static constexpr float AdaptiveMsaaCompactMaximumAbsoluteNdcZ = 16384.0f;
static constexpr float AdaptiveMsaaCompactMinimumWindowCoordinate = -32768.0f;
static constexpr float AdaptiveMsaaCompactMaximumWindowCoordinate = 32767.0f;
static constexpr float AdaptiveMsaaCompactMinimumProjectedTriangleArea =
    1.0e-6f;
static constexpr unsigned int AdaptiveMsaaCompactMinimumSnappedTriangleArea =
    16u;
static constexpr float AdaptiveMsaaCompactMaximumAbsoluteAffineBarycentric =
    1048576.0f;
static constexpr float AdaptiveMsaaCompactMinimumPerspectiveDenominator =
    0.00000095367431640625f;
static constexpr float
    AdaptiveMsaaCompactMaximumAbsolutePerspectiveBarycentric = 1048576.0f;
static constexpr float
    AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient = 1048576.0f;
static constexpr float AdaptiveMsaaCompactMaximumAbsoluteNormalTextureUvScale =
    256.0f;
static constexpr float AdaptiveMsaaCompactMaximumAbsoluteNormalMapScale =
    256.0f;
static constexpr float AdaptiveMsaaCompactMaximumAbsoluteTextureSampleOperand =
    268435456.0f;
static_assert(
    AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient *
        AdaptiveMsaaCompactMaximumAbsoluteNormalTextureUvScale <=
    AdaptiveMsaaCompactMaximumAbsoluteTextureSampleOperand);
#else
#define UVSR_ADAPTIVE_MSAA_RASTER_UINT uint

static const float AdaptiveMsaaCompactMinimumClipW =
    1.17549435082228750797e-38f;
static const float AdaptiveMsaaCompactMaximumClipW =
    4.25352958651173103504e37f;
static const float AdaptiveMsaaCompactMaximumAbsoluteNdcZ = 16384.0f;
static const float AdaptiveMsaaCompactMinimumWindowCoordinate = -32768.0f;
static const float AdaptiveMsaaCompactMaximumWindowCoordinate = 32767.0f;
static const float AdaptiveMsaaCompactMinimumProjectedTriangleArea =
    1.0e-6f;
static const uint AdaptiveMsaaCompactMinimumSnappedTriangleArea = 16u;
static const float AdaptiveMsaaCompactMaximumAbsoluteAffineBarycentric =
    1048576.0f;
static const float AdaptiveMsaaCompactMinimumPerspectiveDenominator =
    0.00000095367431640625f;
static const float AdaptiveMsaaCompactMaximumAbsolutePerspectiveBarycentric =
    1048576.0f;
static const float AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient =
    1048576.0f;
static const float AdaptiveMsaaCompactMaximumAbsoluteNormalTextureUvScale =
    256.0f;
static const float AdaptiveMsaaCompactMaximumAbsoluteNormalMapScale =
    256.0f;
static const float AdaptiveMsaaCompactMaximumAbsoluteTextureSampleOperand =
    268435456.0f;
#endif

struct AdaptiveMsaaGBufferPushConstants
{
    UVSR_ADAPTIVE_MSAA_RASTER_UINT startInstanceLocation;
    UVSR_ADAPTIVE_MSAA_RASTER_UINT startVertexLocation;
    UVSR_ADAPTIVE_MSAA_RASTER_UINT positionOffset;
    UVSR_ADAPTIVE_MSAA_RASTER_UINT prevPositionOffset;
    UVSR_ADAPTIVE_MSAA_RASTER_UINT texCoordOffset;
    UVSR_ADAPTIVE_MSAA_RASTER_UINT normalOffset;
    UVSR_ADAPTIVE_MSAA_RASTER_UINT tangentOffset;
    UVSR_ADAPTIVE_MSAA_RASTER_UINT geometryLocalIndex;
    UVSR_ADAPTIVE_MSAA_RASTER_UINT visibilityClass;
};

#ifndef __cplusplus
// Both the visibility raster and compute reconstruction call this exact
// expression. Precise intermediates keep matrix reassociation or contraction
// from moving a half-subpixel result across Direct3D's 16.8 snapping tie.
void AdaptiveMsaaTransformPosition(
    float3 objectPosition,
    float3x4 objectToWorld,
    float4x4 worldToClip,
    out float3 worldPosition,
    out float4 clipPosition)
{
    precise float3 invariantWorldPosition = mul(
        objectToWorld,
        float4(objectPosition, 1.0f));
    precise float4 invariantClipPosition = mul(
        float4(invariantWorldPosition, 1.0f),
        worldToClip);
    worldPosition = invariantWorldPosition;
    clipPosition = invariantClipPosition;
}
#endif

#undef UVSR_ADAPTIVE_MSAA_RASTER_UINT

#endif // UVSR_ADAPTIVE_MSAA_RASTER_CB_H
