#ifndef UVSR_PATH_TRACING_STABLE_PLANE_RESOLVE_CB_H
#define UVSR_PATH_TRACING_STABLE_PLANE_RESOLVE_CB_H

struct PathTracingStablePlaneResolveConstants
{
    uint2 extent;
    uint stablePlaneCount;
    float resolveStrength;
    uint accumulationAveraging;
    uint accumulationEffectiveHistory;
    uint2 accumulationPadding;
};

#endif // UVSR_PATH_TRACING_STABLE_PLANE_RESOLVE_CB_H
