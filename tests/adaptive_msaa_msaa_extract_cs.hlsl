// UVSR-native test-only MSAA extraction shader. No external source text is
// copied into this file. It is compiled and staged outside the production
// runtime shader tree and exists only to validate exact per-sample raster
// ownership, classification, and depth parity.

#ifndef UVSR_TEST_MSAA_SAMPLES
#define UVSR_TEST_MSAA_SAMPLES 2
#endif

static const uint g_Width = 8u;
static const uint g_Height = 8u;
static const uint g_SampleCount = UVSR_TEST_MSAA_SAMPLES;

Texture2DMS<uint2, UVSR_TEST_MSAA_SAMPLES> t_SurfaceKeys : register(t0);
Texture2DMS<uint, UVSR_TEST_MSAA_SAMPLES> t_VisibilityClass : register(t1);
Texture2DMS<float, UVSR_TEST_MSAA_SAMPLES> t_Depth : register(t2);

struct ExtractedSample
{
    uint2 surfaceKey;
    uint visibilityClass;
    uint depthBits;
};

RWStructuredBuffer<ExtractedSample> u_Samples : register(u0);
RWByteAddressBuffer u_Counters : register(u1);

static const uint InvalidWord = 0xffffffffu;
static const uint ClearedVisibilityWord = 0u;
static const uint GeometryInstanceGenerationShift = 20u;
static const uint GeometryInstanceGenerationMask = 0xfffu;
static const uint MinimumGeometryInstanceGeneration = 1u;
static const uint MaximumGeometryInstanceGeneration = 0xffeu;
static const uint BackgroundClass = 0u;
static const uint ReconstructClass = 1u;
static const uint LocalFallbackBit = 0x40u;
static const uint FallbackBit = 0x80u;
static const uint FallbackReasonMask = 0x3fu;
static const uint MaximumFallbackReason = 21u;
static const uint UnsupportedDeformationReason = 15u;
static const uint UnsupportedMaterialReason = 16u;
static const uint ReconstructionFailureReason = 18u;
static const uint ExtractedSampleStride = 16u;
static const uint CounterByteCount = 36u;
static const uint FatalConfigCounterOffset = 24u;

bool IsValidCoveredClass(uint visibilityClass)
{
    if (visibilityClass == ReconstructClass)
        return true;
    const bool fallback =
        (visibilityClass & FallbackBit) != 0u;
    const bool localFallback = fallback &&
        (visibilityClass & LocalFallbackBit) != 0u;
    const uint reason = visibilityClass & FallbackReasonMask;
    const bool validLocalReason =
        reason == UnsupportedDeformationReason ||
        reason == UnsupportedMaterialReason ||
        reason == ReconstructionFailureReason;
    return visibilityClass <= 0xffu && fallback &&
        reason > 0u && reason <= MaximumFallbackReason &&
        (!localFallback || validLocalReason);
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchThreadId : SV_DispatchThreadID)
{
    uint outputRecordCount;
    uint outputRecordStride;
    u_Samples.GetDimensions(outputRecordCount, outputRecordStride);
    uint counterByteCount;
    u_Counters.GetDimensions(counterByteCount);
    uint keyWidth;
    uint keyHeight;
    uint keySampleCount;
    t_SurfaceKeys.GetDimensions(keyWidth, keyHeight, keySampleCount);
    uint classWidth;
    uint classHeight;
    uint classSampleCount;
    t_VisibilityClass.GetDimensions(
        classWidth, classHeight, classSampleCount);
    uint depthWidth;
    uint depthHeight;
    uint depthSampleCount;
    t_Depth.GetDimensions(depthWidth, depthHeight, depthSampleCount);

    const bool pixelProductValid = g_Width != 0u && g_Height != 0u &&
        g_Width <= 0xffffffffu / g_Height;
    const uint pixelCount = pixelProductValid
        ? g_Width * g_Height
        : 0u;
    const bool recordProductValid = pixelProductValid &&
        g_SampleCount != 0u &&
        pixelCount <= 0xffffffffu / g_SampleCount;
    const uint requiredRecordCount = recordProductValid
        ? pixelCount * g_SampleCount
        : 0u;
    const bool configurationValid = recordProductValid &&
        g_SampleCount == UVSR_TEST_MSAA_SAMPLES &&
        outputRecordCount >= requiredRecordCount &&
        outputRecordStride == ExtractedSampleStride &&
        counterByteCount >= CounterByteCount &&
        all(uint3(keyWidth, keyHeight, keySampleCount) ==
            uint3(g_Width, g_Height, g_SampleCount)) &&
        all(uint3(classWidth, classHeight, classSampleCount) ==
            uint3(g_Width, g_Height, g_SampleCount)) &&
        all(uint3(depthWidth, depthHeight, depthSampleCount) ==
            uint3(g_Width, g_Height, g_SampleCount));
    if (!configurationValid)
    {
        if (all(dispatchThreadId == 0u))
        {
            uint ignored;
            u_Counters.InterlockedAdd(
                FatalConfigCounterOffset, 1u, ignored);
        }
        return;
    }
    if (any(dispatchThreadId >= uint2(g_Width, g_Height)))
        return;

    const uint pixelIndex = dispatchThreadId.y * g_Width +
        dispatchThreadId.x;
    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < UVSR_TEST_MSAA_SAMPLES;
        ++sampleIndex)
    {
        ExtractedSample sample;
        sample.surfaceKey = t_SurfaceKeys.Load(
            int2(dispatchThreadId), sampleIndex);
        sample.visibilityClass = t_VisibilityClass.Load(
            int2(dispatchThreadId), sampleIndex);
        const float depth = t_Depth.Load(
            int2(dispatchThreadId), sampleIndex);
        sample.depthBits = asuint(depth);
        u_Samples[pixelIndex * UVSR_TEST_MSAA_SAMPLES +
            sampleIndex] = sample;

        const bool finiteDepth = isfinite(depth);
        const bool positiveDepth = finiteDepth && depth > 0.0f;
        const bool firstKeyIsInvalidSentinel =
            sample.surfaceKey.x == InvalidWord;
        const bool secondKeyIsInvalidSentinel =
            sample.surfaceKey.y == InvalidWord;
        const uint generation =
            (sample.surfaceKey.x >> GeometryInstanceGenerationShift) &
            GeometryInstanceGenerationMask;
        const bool validKey = !firstKeyIsInvalidSentinel &&
            !secondKeyIsInvalidSentinel &&
            generation >= MinimumGeometryInstanceGeneration &&
            generation <= MaximumGeometryInstanceGeneration;
        const bool partialKey = firstKeyIsInvalidSentinel !=
            secondKeyIsInvalidSentinel;
        const bool validCovered = positiveDepth && validKey &&
            IsValidCoveredClass(sample.visibilityClass);
        const bool validBackground = sample.depthBits == 0u &&
            all(sample.surfaceKey == ClearedVisibilityWord) &&
            sample.visibilityClass == BackgroundClass;
        uint ignored;
        if (validCovered)
            u_Counters.InterlockedAdd(0u, 1u, ignored);
        else if (validBackground)
            u_Counters.InterlockedAdd(4u, 1u, ignored);
        if (!finiteDepth)
            u_Counters.InterlockedAdd(8u, 1u, ignored);
        if (partialKey)
            u_Counters.InterlockedAdd(12u, 1u, ignored);
        if (positiveDepth && !validCovered)
            u_Counters.InterlockedAdd(16u, 1u, ignored);
        if (!positiveDepth && !validBackground)
            u_Counters.InterlockedAdd(20u, 1u, ignored);
        u_Counters.InterlockedAdd(28u, 1u, ignored);
        if (finiteDepth && depth < 0.0f)
            u_Counters.InterlockedAdd(32u, 1u, ignored);
    }
}
