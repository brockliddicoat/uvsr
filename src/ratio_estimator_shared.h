#ifndef UVSR_RATIO_ESTIMATOR_SHARED_H
#define UVSR_RATIO_ESTIMATOR_SHARED_H

// Shared correlated-ratio composition for stochastic transport estimators.
// Shadows are the first consumer. Future consumers must preserve the same
// sample, proposal density, normalization, validity decision, and positive
// filter weight in numerator and denominator. Shadow callers form their
// numerator by applying binary visibility to the shared contribution.
#ifdef __cplusplus

#include <cmath>

struct RatioEstimatorFloat3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

inline bool RatioEstimatorIsFinite(float value)
{
    return std::isfinite(value);
}

inline RatioEstimatorFloat3 RatioEstimatorMakeFloat3(
    float x,
    float y,
    float z)
{
    return { x, y, z };
}

#define UVSR_RATIO_FLOAT3 RatioEstimatorFloat3
#define UVSR_RATIO_INLINE inline

#else

bool RatioEstimatorIsFinite(float value)
{
    return isfinite(value);
}

float3 RatioEstimatorMakeFloat3(float x, float y, float z)
{
    return float3(x, y, z);
}

#define UVSR_RATIO_FLOAT3 float3
#define UVSR_RATIO_INLINE

#endif

static const float RatioEstimatorDefaultDenominatorEpsilon = 1e-4f;

UVSR_RATIO_INLINE float ResolveCorrelatedRatioChannel(
    float filteredNumerator,
    float filteredDenominator,
    float denominatorEpsilon)
{
    const float safeEpsilon = denominatorEpsilon > 0.0f
        ? denominatorEpsilon
        : RatioEstimatorDefaultDenominatorEpsilon;
    if (!(filteredDenominator >= safeEpsilon) ||
        !RatioEstimatorIsFinite(filteredDenominator) ||
        !RatioEstimatorIsFinite(filteredNumerator))
    {
        return 1.0f;
    }

    const float ratio = filteredNumerator / filteredDenominator;
    return RatioEstimatorIsFinite(ratio) && ratio >= 0.0f
        ? ratio
        : 1.0f;
}

UVSR_RATIO_INLINE UVSR_RATIO_FLOAT3 ResolveCorrelatedRatio(
    UVSR_RATIO_FLOAT3 filteredNumerator,
    UVSR_RATIO_FLOAT3 filteredDenominator,
    float denominatorEpsilon)
{
    return RatioEstimatorMakeFloat3(
        ResolveCorrelatedRatioChannel(
            filteredNumerator.x,
            filteredDenominator.x,
            denominatorEpsilon),
        ResolveCorrelatedRatioChannel(
            filteredNumerator.y,
            filteredDenominator.y,
            denominatorEpsilon),
        ResolveCorrelatedRatioChannel(
            filteredNumerator.z,
            filteredDenominator.z,
            denominatorEpsilon));
}

#undef UVSR_RATIO_FLOAT3
#undef UVSR_RATIO_INLINE

#endif // UVSR_RATIO_ESTIMATOR_SHARED_H
