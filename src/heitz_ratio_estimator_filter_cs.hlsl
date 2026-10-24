#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/binding_helpers.hlsli>
#include "heitz_ratio_estimator_shadows_cb.h"
#include "ratio_estimator_shared.h"

#ifndef HEITZ_ADAPTIVE_RADIUS
#define HEITZ_ADAPTIVE_RADIUS 0
#endif

cbuffer c_HeitzShadows : register(b0)
{
    HeitzRatioEstimatorShadowConstants g_Heitz;
};

Texture2D<float4> t_Shadowed : register(t0);
Texture2D<float4> t_Unshadowed : register(t1);
Texture2D<float> t_Depth : register(t2);
Texture2D<float4> t_NormalRoughness : register(t3);
Texture2D<float> t_AdaptiveRadius : register(t4);

VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_ShadowedOrRatio : register(u0);
VK_IMAGE_FORMAT("rgba16f")
RWTexture2D<float4> u_Unshadowed : register(u1);
VK_IMAGE_FORMAT("r8")
RWTexture2D<float> u_AdaptiveRadius : register(u2);

bool HeitzFilterDispatchInViewport(uint2 dispatchPosition)
{
    return all(dispatchPosition < uint2(g_Heitz.view.viewportSize));
}

int2 HeitzFilterPixelPosition(uint2 dispatchPosition)
{
    return int2(dispatchPosition) + int2(g_Heitz.view.viewportOrigin);
}

bool HeitzFilterPixelInViewport(int2 pixelPosition)
{
    const int2 viewportMinimum = int2(g_Heitz.view.viewportOrigin);
    const int2 viewportMaximum = viewportMinimum +
        int2(g_Heitz.view.viewportSize);
    return all(pixelPosition >= viewportMinimum) &&
        all(pixelPosition < viewportMaximum);
}

int2 HeitzFilterClampPixelToViewport(int2 pixelPosition)
{
    const int2 viewportMinimum = int2(g_Heitz.view.viewportOrigin);
    const int2 viewportMaximum = viewportMinimum +
        max(int2(g_Heitz.view.viewportSize), int2(1, 1)) - 1;
    return clamp(pixelPosition, viewportMinimum, viewportMaximum);
}

bool HeitzFilterValidDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Heitz.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

bool HeitzFilterTryNormalize(float3 value, out float3 normalized)
{
    const float lengthSquared = dot(value, value);
    if (!(lengthSquared > 1e-12f) || !isfinite(lengthSquared))
    {
        normalized = 0.0f;
        return false;
    }
    normalized = value * rsqrt(lengthSquared);
    return all(isfinite(normalized));
}

bool HeitzFilterModeIsValid(uint filterMode)
{
    return filterMode == HEITZ_FILTER_MODE_JOINT_BILATERAL ||
        filterMode == HEITZ_FILTER_MODE_GAUSSIAN_BILATERAL ||
        filterMode == HEITZ_FILTER_MODE_HEITZ_STYLE_PAIRED_BILATERAL;
}

bool HeitzFilterModeUsesGaussianSpatialWeights(uint filterMode)
{
    return filterMode == HEITZ_FILTER_MODE_GAUSSIAN_BILATERAL ||
        filterMode == HEITZ_FILTER_MODE_HEITZ_STYLE_PAIRED_BILATERAL;
}

void HeitzFilterLoadPair(
    int2 pixelPosition,
    out float3 shadowed,
    out float3 unshadowed)
{
    const float4 packedShadowed = t_Shadowed[pixelPosition];
    const float4 packedUnshadowed = t_Unshadowed[pixelPosition];
    if (packedShadowed.a != packedUnshadowed.a ||
        !RatioEstimatorPackedEvidenceExponentIsValid(packedUnshadowed.a))
    {
        shadowed = 0.0f;
        unshadowed = 0.0f;
        return;
    }
    unshadowed = UnpackRatioEstimatorEvidence(
        packedUnshadowed.rgb,
        packedUnshadowed.a);
    shadowed = min(
        UnpackRatioEstimatorEvidence(
            packedShadowed.rgb,
            packedUnshadowed.a),
        unshadowed);
}

void HeitzFilterPackPair(
    float3 shadowed,
    float3 unshadowed,
    out float4 packedShadowed,
    out float4 packedUnshadowed)
{
    const float sharedExponent =
        ResolveRatioEstimatorPackedEvidenceExponent(
            unshadowed,
            g_Heitz.denominatorEpsilon);
    const float3 unshadowedMantissa = PackRatioEstimatorEvidence(
        unshadowed,
        sharedExponent);
    const float3 shadowedMantissa = min(
        PackRatioEstimatorEvidence(shadowed, sharedExponent),
        unshadowedMantissa);
    packedShadowed = float4(shadowedMantissa, sharedExponent);
    packedUnshadowed = float4(unshadowedMantissa, sharedExponent);
}

float3 HeitzAdaptiveConditionalShadow(int2 pixelPosition)
{
    float3 shadowed;
    float3 unshadowed;
    HeitzFilterLoadPair(
        HeitzFilterClampPixelToViewport(pixelPosition),
        shadowed,
        unshadowed);
    return ResolveCorrelatedShadowRatio(
        shadowed,
        unshadowed,
        g_Heitz.denominatorEpsilon);
}

uint HeitzAdaptiveHash(uint2 value)
{
    uint hash = value.x * 0x9e3779b9u ^ value.y * 0x85ebca6bu;
    hash ^= hash >> 16u;
    hash *= 0x7feb352du;
    hash ^= hash >> 15u;
    hash *= 0x846ca68bu;
    return hash ^ (hash >> 16u);
}

float HeitzEstimateNoiseAlongAxis(
    int2 centerPixel,
    float2 axis,
    int scanRadius,
    float maximumRadius)
{
    float3 valueMinusTwo = HeitzAdaptiveConditionalShadow(
        centerPixel + int2(axis * float(-scanRadius)));
    float3 valueMinusOne = HeitzAdaptiveConditionalShadow(
        centerPixel + int2(axis * float(1 - scanRadius)));
    float secondDerivativeMagnitude = 0.0f;
    [loop]
    for (int tap = 2 - scanRadius; tap <= scanRadius; ++tap)
    {
        const float3 value = HeitzAdaptiveConditionalShadow(
            centerPixel + int2(axis * float(tap)));
        secondDerivativeMagnitude +=
            HeitzAdaptiveSecondDerivativeMagnitude(
                valueMinusTwo,
                valueMinusOne,
                value);
        valueMinusTwo = valueMinusOne;
        valueMinusOne = value;
    }
    return ResolveHeitzAdaptiveRawNoise(
        secondDerivativeMagnitude,
        maximumRadius);
}

[numthreads(8, 8, 1)]
void EstimateAdaptiveRadius(uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (!HeitzFilterDispatchInViewport(dispatchPosition))
        return;
    const int2 pixelPosition = HeitzFilterPixelPosition(dispatchPosition);
    const float maximumRadius = clamp(
        g_Heitz.filterRadius,
        1.0f,
        8.0f);
    const int scanRadius = min(10, int(ceil(maximumRadius)));
    const float initialAngle =
        (float(HeitzAdaptiveHash(uint2(pixelPosition)) & 0x00ffffffu) /
            16777216.0f) * 0.7853981633974483f;
    float noiseEstimate = 0.0f;
    [unroll]
    for (uint axisIndex = 0u; axisIndex < 4u; ++axisIndex)
    {
        const float angle = initialAngle +
            float(axisIndex) * 0.7853981633974483f;
        float sine;
        float cosine;
        sincos(angle, sine, cosine);
        noiseEstimate = max(
            noiseEstimate,
            HeitzEstimateNoiseAlongAxis(
                pixelPosition,
                float2(cosine, sine),
                scanRadius,
                maximumRadius));
    }
    u_AdaptiveRadius[pixelPosition] =
        isfinite(noiseEstimate) ? saturate(noiseEstimate) : 0.0f;
}

[numthreads(8, 8, 1)]
void DenoiseAdaptiveRadius(uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (!HeitzFilterDispatchInViewport(dispatchPosition))
        return;
    const int2 pixelPosition = HeitzFilterPixelPosition(dispatchPosition);
    float noiseSum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float noise = t_AdaptiveRadius[
                HeitzFilterClampPixelToViewport(
                    pixelPosition + int2(x, y))];
            noiseSum += isfinite(noise) ? saturate(noise) : 0.0f;
        }
    }
    u_AdaptiveRadius[pixelPosition] = noiseSum / 9.0f;
}

float HeitzReferenceGeometryGuideWeight(
    float3 centerPositionVS,
    float3 samplePositionVS,
    float3 centerNormalVS,
    float3 sampleNormalVS)
{
    // Geometry-only portion of the demo guide. UVSR has no equivalent of the
    // demo's matching area-integrated analytic-U guide texture.
    const float depthWeight = ResolveHeitzReferenceDepthGuide(
        centerPositionVS.z,
        samplePositionVS.z);
    // The demo raises the dot product to the fourth power. The shared helper
    // saturates first so opposite faces do not become identical after the even
    // exponent.
    const float normalWeight = ResolveHeitzReferenceNormalGuide(
        dot(sampleNormalVS, centerNormalVS));

    const float3 positionDelta = centerPositionVS - samplePositionVS;
    const float distanceSquared = dot(positionDelta, positionDelta);
    if (!isfinite(distanceSquared) || distanceSquared < 0.0f)
        return 0.0f;
    const float planeError = max(
        abs(dot(positionDelta, sampleNormalVS)),
        abs(dot(positionDelta, centerNormalVS)));
    const float planeWeight = ResolveHeitzReferencePlaneGuide(
        distanceSquared,
        planeError);

    const float weight = depthWeight * normalWeight * planeWeight;
    return isfinite(weight) ? saturate(weight) : 0.0f;
}

float HeitzFilterGuideWeight(
    int2 samplePixel,
    float centerViewZ,
    float3 centerPositionVS,
    float3 centerNormalWS,
    float3 centerNormalVS,
    uint filterMode)
{
    const float sampleDepth = t_Depth[samplePixel];
    if (!HeitzFilterValidDepth(sampleDepth))
        return 0.0f;

    const float3 samplePositionVS = ReconstructViewPosition(
        g_Heitz.view,
        float2(samplePixel) + 0.5f,
        sampleDepth);
    float3 sampleNormalWS;
    if (any(!isfinite(samplePositionVS)) ||
        !HeitzFilterTryNormalize(
            t_NormalRoughness[samplePixel].xyz,
            sampleNormalWS))
    {
        return 0.0f;
    }

    if (filterMode == HEITZ_FILTER_MODE_HEITZ_STYLE_PAIRED_BILATERAL)
    {
        float3 sampleNormalVS;
        if (!HeitzFilterTryNormalize(
                mul(float4(sampleNormalWS, 0.0f),
                    g_Heitz.view.matWorldToView).xyz,
                sampleNormalVS))
        {
            return 0.0f;
        }
        return HeitzReferenceGeometryGuideWeight(
            centerPositionVS,
            samplePositionVS,
            centerNormalVS,
            sampleNormalVS);
    }

    const float depthScale = max(centerViewZ * 0.02f, 0.01f);
    const float planeDistance = abs(dot(
        samplePositionVS - centerPositionVS,
        centerNormalVS));
    const float depthDistance = abs(
        abs(samplePositionVS.z) - centerViewZ);
    const float depthWeight = exp(-planeDistance / depthScale) *
        exp(-depthDistance / (depthScale * 2.0f));
    const float normalWeight = pow(
        saturate(dot(centerNormalWS, sampleNormalWS)),
        16.0f);
    const float weight = depthWeight * normalWeight;
    return isfinite(weight) ? max(weight, 0.0f) : 0.0f;
}

void HeitzFilterPair(
    int2 centerPixel,
    int2 axis,
    out float3 filteredShadowed,
    out float3 filteredUnshadowed)
{
    HeitzFilterLoadPair(
        centerPixel,
        filteredShadowed,
        filteredUnshadowed);
    const uint filterMode = g_Heitz.filterMode;
    if (!HeitzFilterModeIsValid(filterMode))
        return;
    const bool usesGaussianSpatialWeights =
        HeitzFilterModeUsesGaussianSpatialWeights(filterMode);
    const bool usesHeitzReferenceGeometryGuide =
        filterMode == HEITZ_FILTER_MODE_HEITZ_STYLE_PAIRED_BILATERAL;
    const float radius = clamp(g_Heitz.filterRadius, 0.0f, 8.0f);
    if (!(radius >= 1.0f) || !isfinite(radius))
        return;

    float gaussianRadius = radius;
    bool adaptiveRadius = false;
#if HEITZ_ADAPTIVE_RADIUS
    adaptiveRadius = g_Heitz.adaptiveFilterRadius != 0u &&
        usesGaussianSpatialWeights;
    if (adaptiveRadius)
    {
        const float filteredNoise = t_AdaptiveRadius[centerPixel];
        gaussianRadius = ResolveHeitzAdaptiveGaussianRadius(
            filteredNoise,
            radius);
        if (!(gaussianRadius > 0.5f) || !isfinite(gaussianRadius))
            return;
    }
#endif

    const float centerDepth = t_Depth[centerPixel];
    if (!HeitzFilterValidDepth(centerDepth))
        return;

    const float3 centerPositionVS = ReconstructViewPosition(
        g_Heitz.view,
        float2(centerPixel) + 0.5f,
        centerDepth);
    const float centerViewZ = abs(centerPositionVS.z);
    float3 centerNormalWS;
    if (any(!isfinite(centerPositionVS)) ||
        !(centerViewZ > 0.0f) ||
        !HeitzFilterTryNormalize(
            t_NormalRoughness[centerPixel].xyz,
            centerNormalWS))
    {
        return;
    }

    float3 centerNormalVS;
    if (!HeitzFilterTryNormalize(
            mul(float4(centerNormalWS, 0.0f),
                g_Heitz.view.matWorldToView).xyz,
            centerNormalVS))
    {
        return;
    }

    const int integerRadius = int(ceil(radius));
    float3 shadowedMean = 0.0f;
    float3 unshadowedMean = 0.0f;
    float totalWeight = 0.0f;
    [loop]
    for (int tap = -integerRadius; tap <= integerRadius; ++tap)
    {
        const int2 samplePixel = centerPixel + axis * tap;
        if (!HeitzFilterPixelInViewport(samplePixel))
            continue;

        float spatialWeight = 1.0f;
        if (usesGaussianSpatialWeights)
        {
            spatialWeight = ResolveHeitzAdaptiveGaussianWeight(
                float(tap),
                gaussianRadius);
        }
        const float guideWeight =
            usesHeitzReferenceGeometryGuide && tap == 0
                ? 1.0f
                : HeitzFilterGuideWeight(
                    samplePixel,
                    centerViewZ,
                    centerPositionVS,
                    centerNormalWS,
                    centerNormalVS,
                    filterMode);
        const float weight = spatialWeight * guideWeight;
        if (!(weight > 0.0f) || !isfinite(weight))
            continue;

        float3 tapShadowed;
        float3 tapUnshadowed;
        HeitzFilterLoadPair(
            samplePixel,
            tapShadowed,
            tapUnshadowed);
        const float nextTotalWeight = totalWeight + weight;
        const float retainedWeight = totalWeight / nextTotalWeight;
        const float addedWeight = weight / nextTotalWeight;
        shadowedMean = shadowedMean * retainedWeight +
            tapShadowed * addedWeight;
        unshadowedMean = unshadowedMean * retainedWeight +
            tapUnshadowed * addedWeight;
        totalWeight = nextTotalWeight;
    }

    if (!(totalWeight > 1e-6f) || !isfinite(totalWeight))
        return;
    filteredShadowed = float3(
        isfinite(shadowedMean.x) ? max(shadowedMean.x, 0.0f) : 0.0f,
        isfinite(shadowedMean.y) ? max(shadowedMean.y, 0.0f) : 0.0f,
        isfinite(shadowedMean.z) ? max(shadowedMean.z, 0.0f) : 0.0f);
    filteredUnshadowed = float3(
        isfinite(unshadowedMean.x) ? max(unshadowedMean.x, 0.0f) : 0.0f,
        isfinite(unshadowedMean.y) ? max(unshadowedMean.y, 0.0f) : 0.0f,
        isfinite(unshadowedMean.z) ? max(unshadowedMean.z, 0.0f) : 0.0f);
    filteredShadowed = min(filteredShadowed, filteredUnshadowed);
}

[numthreads(8, 8, 1)]
void Horizontal(uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (!HeitzFilterDispatchInViewport(dispatchPosition))
        return;
    const int2 pixelPosition = HeitzFilterPixelPosition(dispatchPosition);
    float3 filteredShadowed;
    float3 filteredUnshadowed;
    HeitzFilterPair(
        pixelPosition,
        int2(1, 0),
        filteredShadowed,
        filteredUnshadowed);
    float4 packedShadowed;
    float4 packedUnshadowed;
    HeitzFilterPackPair(
        filteredShadowed,
        filteredUnshadowed,
        packedShadowed,
        packedUnshadowed);
    u_ShadowedOrRatio[pixelPosition] = packedShadowed;
    u_Unshadowed[pixelPosition] = packedUnshadowed;
}

[numthreads(8, 8, 1)]
void VerticalResolve(uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (!HeitzFilterDispatchInViewport(dispatchPosition))
        return;
    const int2 pixelPosition = HeitzFilterPixelPosition(dispatchPosition);
    float3 filteredShadowed;
    float3 filteredUnshadowed;
    HeitzFilterPair(
        pixelPosition,
        int2(0, 1),
        filteredShadowed,
        filteredUnshadowed);
    const float3 ratio = ResolveCorrelatedShadowRatio(
        filteredShadowed,
        filteredUnshadowed,
        g_Heitz.denominatorEpsilon);
    u_ShadowedOrRatio[pixelPosition] = float4(ratio, 1.0f);
}
