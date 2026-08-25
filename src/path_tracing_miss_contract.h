#ifndef UVSR_PATH_TRACING_MISS_CONTRACT_H
#define UVSR_PATH_TRACING_MISS_CONTRACT_H

#ifdef __cplusplus

#include <cstdint>
using PathTracingMissUint = std::uint32_t;
#define UVSR_PATH_MISS_INLINE inline

#else

#define PathTracingMissUint uint
#define UVSR_PATH_MISS_INLINE

#endif

UVSR_PATH_MISS_INLINE bool PathTracingMissUsesEnvironment(
    PathTracingMissUint bounce,
    bool showPrimaryEnvironmentBackground)
{
    return bounce > 0u || showPrimaryEnvironmentBackground;
}

#ifndef __cplusplus
#undef PathTracingMissUint
#endif
#undef UVSR_PATH_MISS_INLINE

#endif // UVSR_PATH_TRACING_MISS_CONTRACT_H
