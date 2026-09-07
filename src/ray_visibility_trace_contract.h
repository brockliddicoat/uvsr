#ifndef UVSR_RAY_VISIBILITY_TRACE_CONTRACT_H
#define UVSR_RAY_VISIBILITY_TRACE_CONTRACT_H

// Executable CPU/HLSL reduction contract for one binary inline-ray-query
// result per visibility sample.

#ifdef __cplusplus

#include <cstdint>

using RayVisibilityContractUint = std::uint32_t;

#define UVSR_RAY_VISIBILITY_INLINE inline

#else

#define RayVisibilityContractUint uint

#define UVSR_RAY_VISIBILITY_INLINE

#endif

struct RayVisibilityTraceSample
{
    float visibility;
    RayVisibilityContractUint queryCount;
    RayVisibilityContractUint occluded;
};

struct RayVisibilityTraceAggregate
{
    RayVisibilityContractUint queryCount;
    RayVisibilityContractUint sampleCount;
    RayVisibilityContractUint visibleSampleCount;
};

UVSR_RAY_VISIBILITY_INLINE RayVisibilityTraceSample
    ResolveRayVisibilityTraceSample(bool committedTriangleHit)
{
    RayVisibilityTraceSample result;
    result.visibility = committedTriangleHit ? 0.0f : 1.0f;
    result.queryCount = 1u;
    result.occluded = committedTriangleHit ? 1u : 0u;
    return result;
}

UVSR_RAY_VISIBILITY_INLINE RayVisibilityTraceAggregate
    BeginRayVisibilityTraceAggregate()
{
    RayVisibilityTraceAggregate result;
    result.queryCount = 0u;
    result.sampleCount = 0u;
    result.visibleSampleCount = 0u;
    return result;
}

UVSR_RAY_VISIBILITY_INLINE RayVisibilityTraceAggregate
    AccumulateRayVisibilityTraceSample(
        RayVisibilityTraceAggregate aggregate,
        RayVisibilityTraceSample sample)
{
    aggregate.queryCount += sample.queryCount;
    aggregate.sampleCount += 1u;
    aggregate.visibleSampleCount += sample.occluded == 0u ? 1u : 0u;
    return aggregate;
}

UVSR_RAY_VISIBILITY_INLINE bool RayVisibilityTraceAggregateIsComplete(
    RayVisibilityTraceAggregate aggregate,
    RayVisibilityContractUint expectedSampleCount)
{
    return aggregate.queryCount == expectedSampleCount &&
        aggregate.sampleCount == expectedSampleCount;
}

UVSR_RAY_VISIBILITY_INLINE float ResolveRayVisibilityTraceAverage(
    RayVisibilityTraceAggregate aggregate)
{
    return aggregate.sampleCount > 0u
        ? float(aggregate.visibleSampleCount) /
            float(aggregate.sampleCount)
        : 0.0f;
}

#ifndef __cplusplus
#undef RayVisibilityContractUint
#endif
#undef UVSR_RAY_VISIBILITY_INLINE

#endif // UVSR_RAY_VISIBILITY_TRACE_CONTRACT_H
