#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "screen_space_visibility_cb.h"

cbuffer c_Visibility : register(b0)
{
    ScreenSpaceVisibilityConstants g_Visibility;
};

Texture2D<float> t_CurrentAmbient : register(t0);
Texture2D<uint> t_CurrentPackedLinearDepth : register(t1);
Texture2D<float4> t_Motion : register(t2);
Texture2D<float> t_PreviousAmbient : register(t3);
Texture2D<float> t_PreviousLinearDepth : register(t4);

VK_IMAGE_FORMAT("r16f") RWTexture2D<float> u_AmbientHistory : register(u0);
VK_IMAGE_FORMAT("r32f") RWTexture2D<float> u_LinearDepthHistory : register(u1);

static const uint ActivisionGuideOffsetMask = 3u;

float ActivisionLinearDepth(uint packedDepthGuide)
{
    return asfloat(packedDepthGuide & ~ActivisionGuideOffsetMask);
}

uint2 ActivisionGuidePixel(
    uint2 samplingPixel,
    uint packedDepthGuide,
    uint2 fullSize)
{
    uint guideOffset = packedDepthGuide & ActivisionGuideOffsetMask;
    uint2 offset = uint2(guideOffset & 1u, guideOffset >> 1u);
    return min(samplingPixel * 2u + offset, fullSize - 1u);
}

float SafeAmbient(uint2 pixel)
{
    float value = t_CurrentAmbient[pixel];
    return isfinite(value) ? max(value, 0.0f) : 1.0f;
}

bool ValidLinearDepth(float depth)
{
    return isfinite(depth) && depth > 0.0f && depth < 65503.0f;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    uint2 size = uint2(g_Visibility.samplingResolution);
    if (any(pixel >= size))
        return;

    uint2 maximumPixel = size - 1u;
    uint scale = max(g_Visibility.resolutionScale, 1u);
    uint2 fullSize = uint2(g_Visibility.fullResolution);
    uint currentPackedDepthGuide =
        t_CurrentPackedLinearDepth[pixel];
    uint2 fullPixel = ActivisionGuidePixel(
        pixel, currentPackedDepthGuide, fullSize);
    float currentAmbient = SafeAmbient(pixel);
    float currentDepth = ActivisionLinearDepth(
        currentPackedDepthGuide);
    float4 motion = t_Motion[fullPixel];

    static const int2 diagonalOffsets[4] = {
        int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1)
    };
    float lowPassMinimum = currentAmbient;
    float lowPassMaximum = currentAmbient;
    [unroll]
    for (uint diagonal = 0u; diagonal < 4u; ++diagonal)
    {
        uint2 samplePixel = uint2(clamp(
            int2(pixel) + diagonalOffsets[diagonal],
            int2(0, 0), int2(maximumPixel)));
        float sampleValue = SafeAmbient(samplePixel);
        lowPassMinimum = min(lowPassMinimum, sampleValue);
        lowPassMaximum = max(lowPassMaximum, sampleValue);
    }

    bool validReprojection = g_Visibility.historyValid != 0u &&
        all(isfinite(motion)) && motion.a > 0.5f &&
        ValidLinearDepth(currentDepth);
    float2 previousFullPosition = float2(fullPixel) + 0.5f + motion.xy;
    float2 previousSamplingTexel =
        previousFullPosition / float(scale) - 0.5f;
    // The guide can select the left or top pixel of its 2x2 source block.
    // At half resolution that maps a valid zero-motion border receiver to
    // -0.25 in sampling-texel space. Keep the complete clamp-sampler
    // footprint while still rejecting reprojections outside the true
    // full-resolution viewport (including odd-size padding).
    validReprojection = validReprojection &&
        all(isfinite(previousSamplingTexel)) &&
        all(previousFullPosition >= 0.0f) &&
        all(previousFullPosition < float2(fullSize)) &&
        all(previousSamplingTexel >= -0.5f) &&
        all(previousSamplingTexel < float2(size) - 0.5f);

    float historyAmbient = currentAmbient;
    float acceptedWeight = 0.0f;
    if (validReprojection)
    {
        int2 basePixel = int2(floor(previousSamplingTexel));
        float2 fraction = frac(previousSamplingTexel);
        float4 bilinearWeights = float4(
            (1.0f - fraction.x) * (1.0f - fraction.y),
            fraction.x * (1.0f - fraction.y),
            (1.0f - fraction.x) * fraction.y,
            fraction.x * fraction.y);
        static const int2 bilinearOffsets[4] = {
            int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1)
        };
        float accumulatedHistory = 0.0f;
        [unroll]
        for (uint tap = 0u; tap < 4u; ++tap)
        {
            uint2 historyPixel = uint2(clamp(
                basePixel + bilinearOffsets[tap],
                int2(0, 0), int2(maximumPixel)));
            float previousDepth = t_PreviousLinearDepth[historyPixel];
            float previousAmbient = t_PreviousAmbient[historyPixel];
            // The history has no normal surface identifier, so apply the
            // disclosed ten-percent linear-depth threshold symmetrically.
            // A one-sided check retains stale background when new foreground
            // arrives and produces dark disocclusion trails.
            float relativeDepthError = abs(previousDepth - currentDepth) /
                max(max(abs(previousDepth), abs(currentDepth)), 1e-4f);
            bool accepted = ValidLinearDepth(previousDepth) &&
                isfinite(relativeDepthError) &&
                relativeDepthError <= 0.10f &&
                isfinite(previousAmbient);
            if (accepted)
            {
                float weight = bilinearWeights[tap];
                accumulatedHistory += max(previousAmbient, 0.0f) * weight;
                acceptedWeight += weight;
            }
        }
        if (acceptedWeight > 1e-5f)
            historyAmbient = accumulatedHistory / acceptedWeight;
    }

    historyAmbient = lerp(
        historyAmbient,
        clamp(historyAmbient, lowPassMinimum, lowPassMaximum),
        0.25f);
    float motionResponse = saturate(length(motion.xy) * 0.125f);
    float currentWeight = acceptedWeight > 1e-5f
        ? lerp(saturate(g_Visibility.temporalResponse), 1.0f, motionResponse)
        : 1.0f;
    float result = lerp(historyAmbient, currentAmbient, currentWeight);
    u_AmbientHistory[pixel] = isfinite(result)
        ? min(max(result, 0.0f), 65504.0f) : currentAmbient;
    u_LinearDepthHistory[pixel] = currentDepth;
}
