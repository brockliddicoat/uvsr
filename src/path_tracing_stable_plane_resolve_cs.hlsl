#include "path_tracing_stable_plane_resolve_cb.h"
#include "sample_accumulation.hlsli"

cbuffer c_PathTracingStablePlaneResolve : register(b0)
{
    PathTracingStablePlaneResolveConstants g_Resolve;
};

Texture2D<float4> t_RawMean : register(t0);
Texture2D<float4> t_ResidualMean : register(t1);
Texture2D<float4> t_DiffuseSuffixMean : register(t2);
Texture2D<float4> t_PrimaryNormalRoughness : register(t3);
Texture2D<float> t_PrimaryViewZ : register(t4);
Texture2D<float4> t_ColorVariance : register(t5);
Texture2D<uint> t_SuccessfulSampleCount : register(t6);
RWTexture2D<float4> u_Display : register(u0);

static const uint UVSR_STABLE_SIGNAL_MERGED = 0u;
static const uint UVSR_STABLE_SIGNAL_RESIDUAL = 1u;
static const uint UVSR_STABLE_SIGNAL_INDIRECT = 2u;
static const uint UVSR_STABLE_SIGNAL_DIFFUSE = 3u;
static const uint UVSR_STABLE_SIGNAL_SPECULAR = 4u;
static const float3 UVSR_RESOLVE_LUMINANCE =
    float3(0.2126f, 0.7152f, 0.0722f);

struct ResolveConfidence
{
    float luminance;
    float luminanceStandardError;
    float confidence;
};

bool LoadFiniteRaw(int2 pixel, out float3 raw)
{
    raw = t_RawMean[pixel].rgb;
    return all(isfinite(raw));
}

bool LoadGuide(
    int2 pixel,
    out float3 normal,
    out float roughness,
    out float viewZ)
{
    const float4 normalRoughness =
        t_PrimaryNormalRoughness[pixel];
    normal = normalRoughness.xyz;
    roughness = normalRoughness.w;
    viewZ = t_PrimaryViewZ[pixel];
    const float normalLengthSquared = dot(normal, normal);
    if (!all(isfinite(normalRoughness)) || !isfinite(viewZ) ||
        !(viewZ > 0.0f) || !(normalLengthSquared > 0.25f))
    {
        return false;
    }
    normal *= rsqrt(normalLengthSquared);
    roughness = saturate(roughness);
    return true;
}

bool LoadConfidence(
    int2 pixel,
    float3 raw,
    out ResolveConfidence result)
{
    const uint successfulSampleCount =
        t_SuccessfulSampleCount[pixel];
    const float3 variance = t_ColorVariance[pixel].rgb;
    if (successfulSampleCount == 0u ||
        !all(isfinite(variance)) || any(variance < 0.0f))
    {
        result = (ResolveConfidence)0;
        return false;
    }

    result.luminance = dot(raw, UVSR_RESOLVE_LUMINANCE);
    const float luminanceVariance = dot(
        variance,
        UVSR_RESOLVE_LUMINANCE * UVSR_RESOLVE_LUMINANCE);
    uint effectiveSampleCount = max(successfulSampleCount, 1u);
    if (g_Resolve.accumulationAveraging ==
        UVSR_SAMPLE_AVERAGING_EXPONENTIAL)
    {
        const uint safeHistory = clamp(
            g_Resolve.accumulationEffectiveHistory,
            2u,
            4096u);
        effectiveSampleCount = min(
            effectiveSampleCount,
            safeHistory * 2u - 1u);
    }
    result.luminanceStandardError = sqrt(
        max(luminanceVariance, 0.0f) /
        float(effectiveSampleCount));

    // A sample-count ramp prevents a coincidental zero variance from making
    // the first few estimates look converged. Relative noise then keeps
    // filtering useful for genuinely difficult pixels even at a high count.
    const float countConfidence = float(successfulSampleCount) /
        (float(successfulSampleCount) + 16.0f);
    const float relativeNoise = result.luminanceStandardError /
        max(abs(result.luminance), 1.0e-3f);
    result.confidence = saturate(
        countConfidence * rcp(1.0f + relativeNoise));
    return all(isfinite(float3(
        result.luminance,
        result.luminanceStandardError,
        result.confidence)));
}

bool LoadSignal(int2 pixel, uint signal, out float3 value)
{
    float3 raw;
    if (!LoadFiniteRaw(pixel, raw))
    {
        value = 0.0f;
        return false;
    }
    const float3 residual = t_ResidualMean[pixel].rgb;
    const float3 diffuse = t_DiffuseSuffixMean[pixel].rgb;
    if (signal == UVSR_STABLE_SIGNAL_MERGED)
        value = raw;
    else if (signal == UVSR_STABLE_SIGNAL_RESIDUAL)
        value = residual;
    else if (signal == UVSR_STABLE_SIGNAL_INDIRECT)
        value = raw - residual;
    else if (signal == UVSR_STABLE_SIGNAL_DIFFUSE)
        value = diffuse;
    else
    {
        // Derive rather than persist specular. This makes the three accumulated
        // channels recompose to raw despite FP16 residual/diffuse quantization.
        value = raw - residual - diffuse;
    }
    return all(isfinite(value));
}

float StableSignalWeight(
    uint signal,
    int2 offset,
    float centerViewZ,
    float sampleViewZ,
    float normalAgreement,
    float roughnessDifference)
{
    float depthScale;
    float normalScale;
    float roughnessScale;
    if (signal == UVSR_STABLE_SIGNAL_DIFFUSE)
    {
        depthScale = 12.0f;
        normalScale = 6.0f;
        roughnessScale = 1.0f;
    }
    else if (signal == UVSR_STABLE_SIGNAL_RESIDUAL)
    {
        depthScale = 40.0f;
        normalScale = 32.0f;
        roughnessScale = 8.0f;
    }
    else if (signal == UVSR_STABLE_SIGNAL_SPECULAR)
    {
        depthScale = 30.0f;
        normalScale = 48.0f;
        roughnessScale = 12.0f;
    }
    else
    {
        depthScale = 20.0f;
        normalScale = 16.0f;
        roughnessScale = 4.0f;
    }

    const float relativeDepth = abs(sampleViewZ - centerViewZ) /
        max(min(sampleViewZ, centerViewZ), 1.0e-3f);
    const float spatialDistanceSquared = float(dot(offset, offset));
    const float exponent =
        -0.35f * spatialDistanceSquared -
        depthScale * relativeDepth -
        normalScale * (1.0f - normalAgreement) -
        roughnessScale * roughnessDifference;
    return exp(max(exponent, -80.0f));
}

float RadianceEdgeWeight(
    ResolveConfidence center,
    ResolveConfidence sample)
{
    const float radianceDifference = abs(
        sample.luminance - center.luminance);
    const float combinedStandardError = sqrt(
        center.luminanceStandardError *
            center.luminanceStandardError +
        sample.luminanceStandardError *
            sample.luminanceStandardError);
    const float signalScale = max(
        max(abs(center.luminance), abs(sample.luminance)),
        1.0e-3f);
    const float rejectionScale = max(
        3.0f * combinedStandardError,
        0.02f * signalScale + 1.0e-4f);
    const float normalizedDifference = min(
        radianceDifference / rejectionScale,
        16.0f);
    const float confidentEdgeWeight = exp(
        -0.5f * normalizedDifference * normalizedDifference);

    // Variance defines an edge tolerance; it never weights either radiance
    // estimate by reciprocal variance. Low-confidence pairs defer to the
    // geometric guides, while converged pairs preserve radiance boundaries.
    const float edgeConfidence = min(
        center.confidence,
        sample.confidence);
    return lerp(1.0f, confidentEdgeWeight, edgeConfidence);
}

bool FilterStableSignal(
    int2 centerPixel,
    uint signal,
    float3 centerNormal,
    float centerRoughness,
    float centerViewZ,
    ResolveConfidence centerConfidence,
    out float3 filtered)
{
    float3 centerSignal;
    if (!LoadSignal(centerPixel, signal, centerSignal))
    {
        filtered = 0.0f;
        return false;
    }

    float3 weightedSignal = centerSignal;
    float weightSum = 1.0f;
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            const int2 offset = int2(x, y);
            if (all(offset == 0))
                continue;
            const int2 samplePixel = centerPixel + offset;
            if (any(samplePixel < 0) ||
                any(samplePixel >= int2(g_Resolve.extent)))
            {
                continue;
            }

            float3 sampleNormal;
            float sampleRoughness;
            float sampleViewZ;
            if (!LoadGuide(
                    samplePixel,
                    sampleNormal,
                    sampleRoughness,
                    sampleViewZ))
            {
                continue;
            }
            float3 sampleSignal;
            if (!LoadSignal(samplePixel, signal, sampleSignal))
                continue;
            float3 sampleRaw;
            ResolveConfidence sampleConfidence;
            if (!LoadFiniteRaw(samplePixel, sampleRaw) ||
                !LoadConfidence(
                    samplePixel,
                    sampleRaw,
                    sampleConfidence))
            {
                continue;
            }
            const float normalAgreement = saturate(
                dot(centerNormal, sampleNormal));
            const float weight =
                StableSignalWeight(
                    signal,
                    offset,
                    centerViewZ,
                    sampleViewZ,
                    normalAgreement,
                    abs(sampleRoughness - centerRoughness)) *
                RadianceEdgeWeight(
                    centerConfidence,
                    sampleConfidence);
            if (!(weight > 0.0f) || !isfinite(weight))
                continue;
            weightedSignal += sampleSignal * weight;
            weightSum += weight;
        }
    }
    filtered = weightedSignal / weightSum;
    return all(isfinite(filtered));
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Resolve.extent))
        return;

    float3 raw;
    if (!LoadFiniteRaw(int2(pixel), raw))
        raw = 0.0f;
    ResolveConfidence centerConfidence;
    if (!LoadConfidence(int2(pixel), raw, centerConfidence))
    {
        u_Display[pixel] = float4(
            min(max(raw, 0.0f), 65504.0f),
            1.0f);
        return;
    }
    float3 centerNormal;
    float centerRoughness;
    float centerViewZ;
    if (!LoadGuide(
            int2(pixel),
            centerNormal,
            centerRoughness,
            centerViewZ))
    {
        // Sky, primary misses, and invalid guides are never cross-filtered
        // with neighboring geometry. Preserve the center's raw estimate.
        u_Display[pixel] = float4(
            min(max(raw, 0.0f), 65504.0f),
            1.0f);
        return;
    }

    float3 resolved = raw;
    bool valid = false;
    if (g_Resolve.stablePlaneCount == 1u)
    {
        valid = FilterStableSignal(
            int2(pixel),
            UVSR_STABLE_SIGNAL_MERGED,
            centerNormal,
            centerRoughness,
            centerViewZ,
            centerConfidence,
            resolved);
    }
    else if (g_Resolve.stablePlaneCount == 2u)
    {
        float3 residual;
        float3 indirect;
        valid = FilterStableSignal(
                int2(pixel),
                UVSR_STABLE_SIGNAL_RESIDUAL,
                centerNormal,
                centerRoughness,
                centerViewZ,
                centerConfidence,
                residual) &&
            FilterStableSignal(
                int2(pixel),
                UVSR_STABLE_SIGNAL_INDIRECT,
                centerNormal,
                centerRoughness,
                centerViewZ,
                centerConfidence,
                indirect);
        if (valid)
            resolved = residual + indirect;
    }
    else if (g_Resolve.stablePlaneCount == 3u)
    {
        float3 residual;
        float3 diffuse;
        float3 specular;
        valid = FilterStableSignal(
                int2(pixel),
                UVSR_STABLE_SIGNAL_RESIDUAL,
                centerNormal,
                centerRoughness,
                centerViewZ,
                centerConfidence,
                residual) &&
            FilterStableSignal(
                int2(pixel),
                UVSR_STABLE_SIGNAL_DIFFUSE,
                centerNormal,
                centerRoughness,
                centerViewZ,
                centerConfidence,
                diffuse) &&
            FilterStableSignal(
                int2(pixel),
                UVSR_STABLE_SIGNAL_SPECULAR,
                centerNormal,
                centerRoughness,
                centerViewZ,
                centerConfidence,
                specular);
        if (valid)
            resolved = residual + diffuse + specular;
    }

    if (!valid || !all(isfinite(resolved)))
        resolved = raw;
    else
    {
        const float spatialCorrection = saturate(
            g_Resolve.resolveStrength) *
            (1.0f - centerConfidence.confidence);
        resolved = spatialCorrection > 0.0f
            ? raw + (resolved - raw) * spatialCorrection
            : raw;
    }
    u_Display[pixel] = float4(
        min(max(resolved, 0.0f), 65504.0f),
        1.0f);
}
