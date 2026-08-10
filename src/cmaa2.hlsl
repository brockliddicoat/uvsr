//
// Thin UVSR compile wrapper around Intel CMAA2 2.3.
//
// The algorithm body is pinned to GameTechDev/CMAA2 commit
// 071c6b0857559f4e36f614362e6d2aab1b61938a with two documented integration
// hooks: a runtime edge threshold and clamped partial-tile source loads. UVSR
// runs CMAA2 only on its RGBA16F display-linear target.
//

#ifndef CMAA2_EDGE_DETECTION_LUMA_PATH
#error CMAA2_EDGE_DETECTION_LUMA_PATH must be a compile-time shader define
#endif

cbuffer UvsrCmaa2Constants : register(b0)
{
    float g_UvsrCmaa2EdgeThreshold;
    float3 g_UvsrCmaa2Padding;
};

// Intel's source normally chooses this value from a static quality preset.
// The pinned hook lets UVSR expose the same range continuously without
// multiplying detector permutations by four redundant threshold variants.
#define g_CMAA2_EdgeThreshold lpfloat(g_UvsrCmaa2EdgeThreshold)
#define CMAA2_EXTRA_SHARPNESS 0
#define CMAA2_USE_HALF_FLOAT_PRECISION 0
#define CMAA2_UAV_STORE_TYPED 1
#define CMAA2_UAV_STORE_TYPED_UNORM_FLOAT 0
#define CMAA2_UAV_STORE_CONVERT_TO_SRGB 0
#define CMAA2_SUPPORT_HDR_COLOR_RANGE 0
#define CMAA_MSAA_SAMPLE_COUNT 1
// Intel's sample dispatches partial 28x28 edge tiles but its raw Texture.Load
// helper does not clamp those lanes. UVSR supports arbitrary viewport sizes;
// clamp only the source coordinate so a partial tile cannot manufacture a
// black edge at the right/bottom boundary.
#define CMAA2_CLAMP_SOURCE_LOADS_TO_VIEWPORT 1

#include <legal/code-samples/intel-cmaa2/CMAA2.hlsl>
