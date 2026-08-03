#ifndef UVSR_TEMPORAL_AA_OPTIONS_SHARED_H
#define UVSR_TEMPORAL_AA_OPTIONS_SHARED_H

// This macro-only file is the single numeric ABI for C++ PSO indexing and HLSL
// specialization. The algorithm and execution options below remain static PSO
// dimensions. The behavior bits at the end are deliberately runtime-uniform
// advanced image controls on the robust path: keeping them in one
// constant-buffer word avoids a 32-way multiplication of every shipping
// algorithm permutation. Minimum additionally packages one statically folded
// default shader so normal low-cost frames do not pay for that flexibility.

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

#define UVSR_TAA_BLEND_PERMUTATION_COUNT 48

#define UVSR_TAA_LDS_LEGACY 0
#define UVSR_TAA_LDS_PACKED 1

#define UVSR_TAA_BEHAVIOR_MOVING_POINT_DEPTH (1u << 0u)
#define UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT (1u << 1u)
#define UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST (1u << 2u)
#define UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION (1u << 3u)
#define UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN (1u << 4u)

#endif
