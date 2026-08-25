#ifndef UVSR_RENDERER_LIGHT_PROBE_CONTRACT_H
#define UVSR_RENDERER_LIGHT_PROBE_CONTRACT_H

#ifdef __cplusplus

#include <cstddef>
#include <cstdint>
#include <type_traits>

#define UVSR_LIGHT_PROBE_UINT std::uint32_t

#else

#define UVSR_LIGHT_PROBE_UINT uint

#endif

struct LightProbeProcessingConstants
{
    UVSR_LIGHT_PROBE_UINT sampleCount;
    float lodBias;
    float roughness;
    float inputCubeSize;
};

#ifdef __cplusplus
static_assert(sizeof(LightProbeProcessingConstants) == 16u);
static_assert(alignof(LightProbeProcessingConstants) == alignof(float));
static_assert(std::is_standard_layout_v<LightProbeProcessingConstants>);
static_assert(std::is_trivially_copyable_v<LightProbeProcessingConstants>);
static_assert(offsetof(LightProbeProcessingConstants, sampleCount) == 0u);
static_assert(offsetof(LightProbeProcessingConstants, lodBias) == 4u);
static_assert(offsetof(LightProbeProcessingConstants, roughness) == 8u);
static_assert(offsetof(LightProbeProcessingConstants, inputCubeSize) == 12u);
#endif

#undef UVSR_LIGHT_PROBE_UINT

#endif // UVSR_RENDERER_LIGHT_PROBE_CONTRACT_H
