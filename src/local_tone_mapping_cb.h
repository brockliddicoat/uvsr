#ifndef UVSR_LOCAL_TONE_MAPPING_CB_H
#define UVSR_LOCAL_TONE_MAPPING_CB_H

#include <donut/shaders/view_cb.h>

// This range belongs to bilateral analysis only. It deliberately remains
// independent of AgX's display encoding range.
#define LOCAL_TONE_MAPPING_MIN_EV (-16.0f)
#define LOCAL_TONE_MAPPING_MAX_EV (16.0f)

// Log luminance is translated by -MIN_EV, then stored with twelve fractional
// bits. The CPU reference tests prove that the largest possible per-voxel sum
// remains below uint32 for every quality and supported viewport dimension.
#define LOCAL_TONE_MAPPING_FIXED_POINT_SCALE (4096.0f)

#define LOCAL_TONE_MAPPING_DEBUG_OFF                 0u
#define LOCAL_TONE_MAPPING_DEBUG_INPUT_LOG_LUMINANCE 1u
#define LOCAL_TONE_MAPPING_DEBUG_BASE_LOCAL_EV       2u
#define LOCAL_TONE_MAPPING_DEBUG_FINAL_LOCAL_EV      3u
#define LOCAL_TONE_MAPPING_DEBUG_GRID_CONFIDENCE     4u

#ifdef __cplusplus
#define UVSR_LOCAL_ALIGN_16 alignas(16)
#else
#define UVSR_LOCAL_ALIGN_16
#endif

// Shared by all bilateral-grid compute stages. Every row is exactly sixteen
// bytes so the CPU and HLSL layouts are reviewable without implicit packing.
struct UVSR_LOCAL_ALIGN_16 LocalToneMappingConstants
{
    // xy: input dimensions, z: sampling stride, w: spatial cell size.
    uint4 inputDimensionsStrideCell;

    // xyz: grid dimensions, w: linear voxel count.
    uint4 gridDimensionsVoxelCount;

    // x/y: analysis EV range, z: inverse range, w: fixed-point scale.
    float4 rangeAndFixedPoint;

    // x: spatial sigma in cells, y: range sigma in bins,
    // z: current-reference blend weight, w: confidence normalization.
    float4 filterAndReference;

    // x: blur axis, y: kernel radius, z: reset scalar history, w: padding.
    uint4 blurAxisRadiusAndFlags;
};

// Appended to UVSR's existing AgX constant buffer. The finalized grid is
// sampled before sharpening and white balance, while the resulting scalar EV
// is applied with the existing global exposure after white balance.
struct UVSR_LOCAL_ALIGN_16 LocalToneMappingDisplayConstants
{
    // xyz: grid dimensions, w: spatial cell size in native pixels.
    float4 gridDimensionsAndCellSize;

    // x/y: analysis EV range, z: strength, w: maximum absolute correction.
    float4 rangeStrengthAndMaximumCorrection;

    // x/y: shadow/highlight directional scales, z: debug mode, w: active.
    float4 directionDebugAndActive;
};

#undef UVSR_LOCAL_ALIGN_16

#endif // UVSR_LOCAL_TONE_MAPPING_CB_H
