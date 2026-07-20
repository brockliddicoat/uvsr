//
// Thin UVSR compile wrapper around Intel CMAA2 2.3.
//
// The algorithm body is vendored verbatim from GameTechDev/CMAA2 commit
// 071c6b0857559f4e36f614362e6d2aab1b61938a. UVSR changes only the static
// resource-format contract needed for its scene-linear RGBA16F target.
//

#ifndef CMAA2_STATIC_QUALITY_PRESET
#error CMAA2_STATIC_QUALITY_PRESET must be a compile-time shader define
#endif

#define CMAA2_SUPPORT_HDR_COLOR_RANGE 1
#define CMAA2_EDGE_DETECTION_LUMA_PATH 1
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
