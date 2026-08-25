#ifndef UVSR_PATH_TRACING_BINDINGS_H
#define UVSR_PATH_TRACING_BINDINGS_H

#ifdef __cplusplus

#include <array>
#include <cstdint>

namespace uvsr
{
    inline constexpr std::uint32_t PathTracingConstantBufferSlot = 0u;
    inline constexpr std::uint32_t PathTracingWorldTlasSlot = 0u;
    inline constexpr std::uint32_t PathTracingEnvironmentSlot = 1u;
    inline constexpr std::uint32_t PathTracingNoiseSlot = 2u;
    inline constexpr std::uint32_t PathTracingLightsSlot = 13u;
    inline constexpr std::uint32_t PathTracingInstancesSlot = 14u;
    inline constexpr std::uint32_t PathTracingRawMeanUavSlot = 0u;
    inline constexpr std::uint32_t PathTracingAcceptedCountUavSlot = 1u;
    inline constexpr std::uint32_t PathTracingMotionUavSlot = 2u;
    inline constexpr std::uint32_t PathTracingDepthUavSlot = 3u;
    inline constexpr std::uint32_t PathTracingRetryGenerationUavSlot = 4u;
    inline constexpr std::array<std::uint32_t, 5> PathTracingUavSlots = {
        PathTracingRawMeanUavSlot,
        PathTracingAcceptedCountUavSlot,
        PathTracingMotionUavSlot,
        PathTracingDepthUavSlot,
        PathTracingRetryGenerationUavSlot
    };
}

#else

#define UVSR_PATH_TRACING_CONSTANT_BUFFER_REGISTER b0
#define UVSR_PATH_TRACING_WORLD_TLAS_REGISTER t0
#define UVSR_PATH_TRACING_ENVIRONMENT_REGISTER t1
#define UVSR_PATH_TRACING_NOISE_REGISTER t2
#define UVSR_PATH_TRACING_LIGHTS_REGISTER t13
#define UVSR_PATH_TRACING_INSTANCES_REGISTER t14
#define UVSR_PATH_TRACING_RAW_MEAN_UAV_REGISTER u0
#define UVSR_PATH_TRACING_ACCEPTED_COUNT_UAV_REGISTER u1
#define UVSR_PATH_TRACING_MOTION_UAV_REGISTER u2
#define UVSR_PATH_TRACING_DEPTH_UAV_REGISTER u3
#define UVSR_PATH_TRACING_RETRY_GENERATION_UAV_REGISTER u4

#endif

#endif
