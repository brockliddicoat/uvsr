#ifndef UVSR_SAMPLE_ACCUMULATION_HLSLI
#define UVSR_SAMPLE_ACCUMULATION_HLSLI

static const uint UVSR_SAMPLE_SEQUENCE_FRAME_PHASE = 0u;
static const uint UVSR_SAMPLE_SEQUENCE_SUCCESSFUL_COUNT = 1u;
static const uint UVSR_SAMPLE_SEQUENCE_ANIMATED_RESET = 2u;

bool UvsrSampleScheduleEnabled(uint sequenceMode)
{
    return sequenceMode != UVSR_SAMPLE_SEQUENCE_FRAME_PHASE;
}

uint UvsrResolveSampleSequencePhase(
    uint sequenceMode,
    uint attemptToken,
    uint framePhase)
{
    if (sequenceMode == UVSR_SAMPLE_SEQUENCE_ANIMATED_RESET)
        return framePhase;
    return sequenceMode == UVSR_SAMPLE_SEQUENCE_SUCCESSFUL_COUNT &&
            attemptToken > 0u
        ? attemptToken - 1u
        : framePhase;
}

static const uint UVSR_SAMPLE_AVERAGING_CUMULATIVE = 0u;
static const uint UVSR_SAMPLE_AVERAGING_EXPONENTIAL = 1u;
static const uint UVSR_SAMPLE_SCHEDULING_EVERY_PIXEL = 0u;
static const uint UVSR_SAMPLE_SCHEDULING_VARIANCE_GUIDED = 1u;
static const float UVSR_SAMPLE_RELATIVE_ERROR_COLOR_FLOOR = 0.001f;

float UvsrSampleMeanWeight(
    uint averaging,
    uint effectiveHistory,
    uint previousCount,
    uint newCount)
{
    if (averaging == UVSR_SAMPLE_AVERAGING_EXPONENTIAL)
    {
        return rcp(float(min(
            max(newCount, 1u),
            max(effectiveHistory, 2u))));
    }
    return newCount > previousCount
        ? rcp(float(newCount))
        : 0.0f;
}

float3 UvsrSampleVarianceUpdate(
    uint averaging,
    uint previousCount,
    uint newCount,
    float3 previousVariance,
    float3 previousMean,
    float3 sample,
    float3 newMean,
    float meanWeight)
{
    if (previousCount == 0u)
        return 0.0f;

    const float3 delta = sample - previousMean;
    if (averaging == UVSR_SAMPLE_AVERAGING_EXPONENTIAL)
    {
        return max(
            (1.0f - meanWeight) *
                (max(previousVariance, 0.0f) +
                    meanWeight * delta * delta),
            0.0f);
    }
    if (newCount <= previousCount)
        return max(previousVariance, 0.0f);
    return max(
        (float(previousCount) * max(previousVariance, 0.0f) +
            delta * (sample - newMean)) /
            float(newCount),
        0.0f);
}

float UvsrSampleUpdateRate(
    uint scheduling,
    uint averaging,
    uint effectiveHistory,
    uint minimumSamples,
    float targetRelativeError,
    float minimumUpdateRate,
    uint previousCount,
    float3 previousMean,
    float3 previousVariance)
{
    if (scheduling == UVSR_SAMPLE_SCHEDULING_EVERY_PIXEL ||
        previousCount < max(minimumSamples, 2u) ||
        !all(isfinite(previousMean)) ||
        !all(isfinite(previousVariance)))
    {
        return 1.0f;
    }

    uint effectiveSamples = max(previousCount, 1u);
    if (averaging == UVSR_SAMPLE_AVERAGING_EXPONENTIAL)
    {
        const uint safeHistory = clamp(effectiveHistory, 2u, 4096u);
        effectiveSamples = min(
            effectiveSamples,
            safeHistory * 2u - 1u);
    }
    const float3 standardError = sqrt(
        max(previousVariance, 0.0f) / float(effectiveSamples));
    const float3 relativeError = standardError / max(
        abs(previousMean),
        UVSR_SAMPLE_RELATIVE_ERROR_COLOR_FLOOR);
    const float maximumRelativeError = max(
        relativeError.x,
        max(relativeError.y, relativeError.z));
    return clamp(
        maximumRelativeError / max(targetRelativeError, 0.001f),
        clamp(minimumUpdateRate, 1.0f / 256.0f, 1.0f),
        1.0f);
}

uint UvsrSampleUpdateInterval(
    uint scheduling,
    uint averaging,
    uint effectiveHistory,
    uint minimumSamples,
    float targetRelativeError,
    float minimumUpdateRate,
    uint previousCount,
    float3 previousMean,
    float3 previousVariance)
{
    const float updateRate = UvsrSampleUpdateRate(
        scheduling,
        averaging,
        effectiveHistory,
        minimumSamples,
        targetRelativeError,
        minimumUpdateRate,
        previousCount,
        previousMean,
        previousVariance);
    return clamp(uint(floor(rcp(max(updateRate, 1.0f / 256.0f)))), 1u, 256u);
}

#endif // UVSR_SAMPLE_ACCUMULATION_HLSLI
