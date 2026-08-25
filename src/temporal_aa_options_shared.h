#ifndef UVSR_TEMPORAL_AA_OPTIONS_SHARED_H
#define UVSR_TEMPORAL_AA_OPTIONS_SHARED_H

// This macro-only file is the numeric ABI shared by the four visible quality
// recipes and HLSL. Advanced behavior remains runtime-uniform, avoiding a
// compile-time cross-product for controls that do not change resource layout.

#define UVSR_TAA_MOTION_CENTER 0
#define UVSR_TAA_MOTION_CLOSEST_CROSS 1
#define UVSR_TAA_MOTION_CENTER_FIRST_EDGE_DILATION 2
#define UVSR_TAA_MOTION_SOURCE_COUNT 3

#define UVSR_TAA_CURRENT_DIRECT 0
#define UVSR_TAA_CURRENT_DEJITTERED 1
#define UVSR_TAA_CURRENT_RECONSTRUCTION_COUNT 2

#define UVSR_TAA_HISTORY_BILINEAR 0
#define UVSR_TAA_HISTORY_ONE_SAMPLE_BICUBIC 1
#define UVSR_TAA_HISTORY_FIVE_TAP_CATMULL_ROM 2
#define UVSR_TAA_HISTORY_NINE_TAP_CATMULL_ROM 3
#define UVSR_TAA_HISTORY_FILTER_COUNT 4

#define UVSR_TAA_RECTIFICATION_PAIR_RGB 0
#define UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG 1
#define UVSR_TAA_RECTIFICATION_COUNT 2

#define UVSR_TAA_BLEND_PERMUTATION_COUNT 4

#define UVSR_TAA_LDS_LEGACY 0
#define UVSR_TAA_LDS_PACKED 1

// Stationary center-owned samples bypass raw previous depth; moving samples
// validate one nearest texel. The historical name preserves the CPU/HLSL ABI.
#define UVSR_TAA_BEHAVIOR_NEAREST_TEXEL_DEPTH (1u << 0u)
#define UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT (1u << 1u)
#define UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST (1u << 2u)
#define UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION (1u << 3u)
#define UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN (1u << 4u)

#endif
