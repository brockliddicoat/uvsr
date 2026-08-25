#ifndef UVSR_RAY_VISIBILITY_TRACE_CONTRACT_H
#define UVSR_RAY_VISIBILITY_TRACE_CONTRACT_H

// Executable CPU/HLSL reduction contract for one binary inline-ray-query
// result per visibility sample and closest committed-blocker retention.

#ifdef __cplusplus

#include <algorithm>
#include <cmath>
#include <cstdint>

using RayVisibilityContractUint = std::uint32_t;

inline bool RayVisibilityContractIsFinite(float value) noexcept
{
    return std::isfinite(value);
}

inline float RayVisibilityContractMin(float left, float right) noexcept
{
    return std::min(left, right);
}

#define UVSR_RAY_VISIBILITY_INLINE inline

#else

#define RayVisibilityContractUint uint

bool RayVisibilityContractIsFinite(float value)
{
    return isfinite(value);
}

float RayVisibilityContractMin(float left, float right)
{
    return min(left, right);
}

#define UVSR_RAY_VISIBILITY_INLINE

#endif

struct RayVisibilityTraceSample
{
    float visibility;
    float hitDistance;
    RayVisibilityContractUint queryCount;
    RayVisibilityContractUint occluded;
};

struct RayVisibilityTraceAggregate
{
    float closestHitDistance;
    RayVisibilityContractUint queryCount;
    RayVisibilityContractUint sampleCount;
    RayVisibilityContractUint visibleSampleCount;
};

UVSR_RAY_VISIBILITY_INLINE RayVisibilityTraceSample
    ResolveRayVisibilityTraceSample(
        bool committedTriangleHit,
        float committedRayT,
        bool outputHitDistance,
        float maximumHitDistance,
        float missHitDistance)
{
    RayVisibilityTraceSample result;
    result.visibility = committedTriangleHit ? 0.0f : 1.0f;
    result.hitDistance = missHitDistance;
    result.queryCount = 1u;
    result.occluded = committedTriangleHit ? 1u : 0u;
    if (!committedTriangleHit)
        return result;
    if (!outputHitDistance)
    {
        result.hitDistance = 0.0f;
        return result;
    }
    // A committed blocker remains occluding even if a malformed distance is
    // observed. Publish the conservative maximum rather than turning it into
    // a miss or a nearer false blocker.
    result.hitDistance =
        RayVisibilityContractIsFinite(committedRayT) &&
            committedRayT >= 0.0f
        ? RayVisibilityContractMin(
            committedRayT,
            maximumHitDistance)
        : maximumHitDistance;
    return result;
}

UVSR_RAY_VISIBILITY_INLINE RayVisibilityTraceAggregate
    BeginRayVisibilityTraceAggregate(float missHitDistance)
{
    RayVisibilityTraceAggregate result;
    result.closestHitDistance = missHitDistance;
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
    if (sample.occluded != 0u)
    {
        aggregate.closestHitDistance = RayVisibilityContractMin(
            aggregate.closestHitDistance,
            sample.hitDistance);
    }
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
