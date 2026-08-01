//
// Thin UVSR compile wrapper around Intel CMAA2 2.3.
//
// The algorithm body is pinned to GameTechDev/CMAA2 commit
// 071c6b0857559f4e36f614362e6d2aab1b61938a with one documented boundary-load
// patch. This wrapper selects the color-range and quality contracts used by
// UVSR's RGBA16F display-linear and scene-linear targets.
//

#ifndef CMAA2_STATIC_QUALITY_PRESET
#error CMAA2_STATIC_QUALITY_PRESET must be a compile-time shader define
#endif

#ifndef CMAA2_SUPPORT_HDR_COLOR_RANGE
#error CMAA2_SUPPORT_HDR_COLOR_RANGE must be a compile-time shader define
#endif

// Intel documents the full-color detector as its highest-quality path. Keep
// the faster luma detector for the cost-oriented tiers and make Ultra the
// uncompromised color-edge permutation, including isoluminant chromatic edges.
#if CMAA2_STATIC_QUALITY_PRESET == 3
#define CMAA2_EDGE_DETECTION_LUMA_PATH 0
#else
#define CMAA2_EDGE_DETECTION_LUMA_PATH 1
#endif
#define CMAA2_EXTRA_SHARPNESS 0
#define CMAA2_USE_HALF_FLOAT_PRECISION 0
#define CMAA2_UAV_STORE_TYPED 1
#define CMAA2_UAV_STORE_TYPED_UNORM_FLOAT 0
#define CMAA2_UAV_STORE_CONVERT_TO_SRGB 0
#define CMAA_MSAA_SAMPLE_COUNT 1
// Intel's sample dispatches partial 28x28 edge tiles but its raw Texture.Load
// helper does not clamp those lanes. UVSR supports arbitrary viewport sizes;
// clamp only the source coordinate so a partial tile cannot manufacture a
// black edge at the right/bottom boundary.
#define CMAA2_CLAMP_SOURCE_LOADS_TO_VIEWPORT 1

#include "third_party/intel_cmaa2/CMAA2.hlsl"
