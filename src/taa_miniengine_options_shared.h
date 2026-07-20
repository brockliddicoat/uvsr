#ifndef UVSR_TAA_MINIENGINE_OPTIONS_SHARED_H
#define UVSR_TAA_MINIENGINE_OPTIONS_SHARED_H

// This macro-only file is the single numeric ABI for C++ PSO indexing and HLSL
// compile-time specialization. Shipping option values must never be placed in
// a shader constant buffer: every Cartesian-product combination is compiled as
// a distinct shader and receives a distinct compute PSO.

#define UVSR_TAA_MOTION_CENTER 0
#define UVSR_TAA_MOTION_CLOSEST_CROSS 1
#define UVSR_TAA_MOTION_CENTER_FIRST_EDGE_DILATION 2
#define UVSR_TAA_MOTION_SOURCE_COUNT 3

#define UVSR_TAA_CURRENT_DIRECT 0
#define UVSR_TAA_CURRENT_DEJITTERED 1
#define UVSR_TAA_CURRENT_RECONSTRUCTION_COUNT 2

#define UVSR_TAA_INTERIOR_OFF 0
#define UVSR_TAA_INTERIOR_STABLE 1
#define UVSR_TAA_INTERIOR_WEIGHTING_COUNT 2

#define UVSR_TAA_HISTORY_BILINEAR 0
#define UVSR_TAA_HISTORY_ONE_SAMPLE_BICUBIC 1
#define UVSR_TAA_HISTORY_FIVE_TAP_CATMULL_ROM 2
#define UVSR_TAA_HISTORY_FILTER_COUNT 3

#define UVSR_TAA_RECTIFICATION_PAIR_RGB 0
#define UVSR_TAA_RECTIFICATION_PER_PIXEL_RGB 1
#define UVSR_TAA_RECTIFICATION_PER_PIXEL_YCOCG 2
#define UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG 3
#define UVSR_TAA_RECTIFICATION_COUNT 4

#define UVSR_TAA_SAMPLE_RESURRECTION_OFF 0
#define UVSR_TAA_SAMPLE_RESURRECTION_ONE_OLDER_FRAME 1
#define UVSR_TAA_SAMPLE_RESURRECTION_TWO_OLDER_FRAMES 2
#define UVSR_TAA_SAMPLE_RESURRECTION_MODE_COUNT 3

#define UVSR_TAA_DEBUG_OFF 0
#define UVSR_TAA_DEBUG_STABLE_INTERIOR 1
#define UVSR_TAA_DEBUG_FINAL_HISTORY_WEIGHT 2
#define UVSR_TAA_DEBUG_SAMPLE_RESURRECTION 3
#define UVSR_TAA_DEBUG_VIEW_COUNT 4

// Developer-only presentation diagnostics share the UI's debug-view enum but
// never enter the MiniEngine resolve shader. Production shader bundles omit
// their static SMAA permutations entirely.
#define UVSR_SMAA_DEBUG_EDGE_MASK 4
#define UVSR_SMAA_DEBUG_BLEND_WEIGHTS 5
#define UVSR_SMAA_DEBUG_OUTPUT_DELTA 6
#define UVSR_AA_DEBUG_VIEW_COUNT 7

#define UVSR_TAA_STABLE_INTERIOR_FLOOR 0.875
#define UVSR_TAA_SELECTIVE_HISTORY_MINIMUM 0.5
#define UVSR_TAA_SELECTIVE_HISTORY_TRUSTED 0.8
// Rejection below one 8-bit presentation step is smoothly remapped to exact
// zero before R16F export. Sparse classification can then use > 0 with the
// same continuous mask consumed by neighborhood blending, avoiding a tile
// on/off discontinuity in extreme HDR.
#define UVSR_TAA_SELECTIVE_REJECTION_FLOOR (1.0 / 255.0)

#define UVSR_TAA_BLEND_PERMUTATION_COUNT 144

#define UVSR_TAA_KERNEL_8X8_TWO_PIXELS 0
#define UVSR_TAA_KERNEL_16X8_ONE_PIXEL 1
#define UVSR_TAA_KERNEL_COUNT 2

#define UVSR_TAA_LDS_LEGACY 0
#define UVSR_TAA_LDS_SPLIT 1
#define UVSR_TAA_LDS_SPLIT_PACKED 2
#define UVSR_TAA_LDS_LAYOUT_COUNT 3

#endif
