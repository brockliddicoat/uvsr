#include "denoising_common.hlsli"

#ifndef DENOISING_STAGE
#define DENOISING_STAGE 0
#endif

RWTexture2D<float4> u_Color : register(u0);
RWTexture2D<float4> u_Moments : register(u1);

void DenoisingWrite(uint2 p, float4 value)
{
    u_Color[p] = float4(DenoisingFinite(value.xyz), isfinite(value.w) ? max(value.w, 0.0f) : 0.0f);
}

void DenoisingPrepare(uint2 p)
{
    uint2 fullPixel = DenoisingFullPixel(p);
    float depth = t_Guide[fullPixel].x;
    float4 guide = DenoisingMakeGuide(fullPixel, depth, t_PreviousGuide[fullPixel].xyz);
    float4 motion = t_Reprojection[fullPixel];
    float4 reprojection = 0.0f;
    if (guide.w > 0.0f && motion.w > 0.0f && all(isfinite(motion)))
    {
        float2 previousFull = float2(fullPixel) + 0.5f + motion.xy;
        float3 previousPosition = ReconstructViewPosition(
            g_Denoising.previousView, previousFull, depth + motion.z);
        if (all(isfinite(previousPosition)))
            reprojection = float4(DenoisingGridPosition(previousFull - 0.5f), abs(previousPosition.z), 1.0f);
    }
    u_Color[p] = guide;
    u_Moments[p] = reprojection;
}

void DenoisingHistoryTap(int2 p, float weight, float4 guide, float expectedZ,
    inout float total, inout float3 color, inout float3 moments)
{
    if (!DenoisingInside(p) || weight <= 0.0f) return;
    float4 previousGuide = t_PreviousGuide[p];
    if (!(previousGuide.w > 0.0f) || dot(guide.xyz, previousGuide.xyz) < g_Denoising.settings.normalRejection ||
        abs(previousGuide.w - expectedZ) > max(expectedZ * g_Denoising.settings.disocclusionThreshold, 1e-5f)) return;
    float3 history = t_History[p].xyz;
    float3 historyMoments = t_Moments[p].xyz;
    if (any(!isfinite(history)) || any(!isfinite(historyMoments)) || historyMoments.z < 1.0f) return;
    total += weight;
    color += history * weight;
    moments += historyMoments * weight;
}

void DenoisingTemporal(uint2 p)
{
    float4 sample = DenoisingSample(DenoisingFullPixel(p));
    float3 raw = sample.xyz;
    float luminance = min(DenoisingLuminance(raw), 1e16f);
    float4 guide = DenoisingGuide(p);
    float4 reprojection = t_Reprojection[p];
    float total = 0.0f;
    float3 history = 0.0f, moments = 0.0f;
    if (g_Denoising.resetHistory == 0u && g_Denoising.settings.temporal != 0u &&
        guide.w > 0.0f && reprojection.w > 0.0f)
    {
        int2 origin = int2(floor(reprojection.xy));
        float2 fraction = frac(reprojection.xy);
        [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x)
            DenoisingHistoryTap(origin + int2(x, y), (x == 0 ? 1.0f - fraction.x : fraction.x) *
                (y == 0 ? 1.0f - fraction.y : fraction.y), guide, reprojection.z, total, history, moments);
    }
    bool validHistory = total >= 0.01f;
    if (validHistory) { history /= total; moments /= total; }
    else { history = 0.0f; moments = 0.0f; }
    float length = min(moments.z + 1.0f, float(g_Denoising.settings.historyLength));
    float alpha = validHistory ? max(g_Denoising.settings.alpha, rcp(length)) : 1.0f;
    float momentsAlpha = validHistory ? max(g_Denoising.settings.momentsAlpha, rcp(length)) : 1.0f;
    if (g_Denoising.adaptiveAvailable != 0u && g_Denoising.settings.adaptive != 0u)
    {
        float2 position = (float2(DenoisingFullPixel(p)) + 0.5f) / 3.0f - 0.5f;
        int2 origin = int2(floor(position));
        float2 fraction = frac(position);
        float3 gradient = 0.0f;
        [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x)
        {
            int2 tap = clamp(origin + int2(x, y), 0, int2(g_Denoising.gradientSize) - 1);
            float weight = (x == 0 ? 1.0f - fraction.x : fraction.x) * (y == 0 ? 1.0f - fraction.y : fraction.y);
            gradient += t_Gradient[tap].xyz * weight;
        }
        if (gradient.z > 0.0f)
        {
            // normalize the common-random-number difference after reconstruction.
            float adaptiveAlpha = saturate(g_Denoising.settings.adaptiveScale * gradient.x / max(gradient.y, 1e-5f));
            alpha = max(alpha, adaptiveAlpha);
            momentsAlpha = max(momentsAlpha, adaptiveAlpha);
            length = min(length, rcp(max(alpha, 1e-5f)));
        }
    }
    if (sample.w <= 0.0f)
    {
        alpha = momentsAlpha = validHistory ? 0.0f : 1.0f;
        length = moments.z;
    }
    float2 currentMoments = float2(luminance, luminance * luminance);
    float2 filteredMoments = lerp(moments.xy, currentMoments, momentsAlpha);
    float variance = g_Denoising.settings.variance != 0u
        ? max(filteredMoments.y - filteredMoments.x * filteredMoments.x, 0.0f) : 1.0f;
    DenoisingWrite(p, float4(lerp(history, raw, alpha), variance));
    u_Moments[p] = float4(filteredMoments, length, 0.0f);
}

void DenoisingVariance(uint2 p)
{
    float4 center = DenoisingColor(p);
    float4 guide = DenoisingGuide(p);
    float history = t_Moments[p].z;
    if (!(guide.w > 0.0f) || history >= float(g_Denoising.settings.varianceHistory))
    { DenoisingWrite(p, center); return; }
    DenoisingSurface surface = DenoisingMakeSurface(DenoisingFullPixel(p), guide);
    float3 color = 0.0f;
    float2 moments = 0.0f;
    float total = 0.0f;
    int radius = int(g_Denoising.settings.varianceRadius);
    [loop] for (int y = -radius; y <= radius; ++y)
    [loop] for (int x = -radius; x <= radius; ++x)
    {
        int2 tap = int2(p) + int2(x, y);
        if (!DenoisingInside(tap)) continue;
        if (t_Moments[tap].z <= 0.0f) continue;
        float3 tapColor = DenoisingColor(tap).xyz;
        float weight = x == 0 && y == 0 ? 1.0f : DenoisingSurfaceWeight(surface, DenoisingFullPixel(tap), DenoisingGuide(tap));
        weight *= exp(-abs(DenoisingLuminance(center.xyz) - DenoisingLuminance(tapColor)) /
            max(g_Denoising.settings.colorPhi, 1e-5f));
        color += tapColor * weight;
        moments += t_Moments[tap].xy * weight;
        total += weight;
    }
    color /= max(total, 1e-6f);
    moments /= max(total, 1e-6f);
    float variance = max(moments.y - moments.x * moments.x, 0.0f) *
        float(g_Denoising.settings.varianceHistory) / max(history, 1.0f);
    DenoisingWrite(p, float4(color, variance));
}

void DenoisingAtrous(uint2 p)
{
    float4 center = DenoisingColor(p);
    float4 guide = DenoisingGuide(p);
    if (!(guide.w > 0.0f)) { DenoisingWrite(p, center); return; }
    float variance = center.w;
    if (g_Denoising.settings.varianceBlur != 0u)
    {
        variance = 0.0f;
        [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            variance += DenoisingColor(int2(p) + int2(x, y)).w *
                (x == 0 ? 0.5f : 0.25f) * (y == 0 ? 0.5f : 0.25f);
    }
    float luminance = DenoisingLuminance(center.xyz);
    float luminanceScale = max(g_Denoising.settings.colorPhi * sqrt(max(variance, 0.0f)), 1e-5f);
    DenoisingSurface surface = DenoisingMakeSurface(DenoisingFullPixel(p), guide);
    float3 color = center.xyz;
    float resultVariance = center.w;
    float total = 1.0f;
    static const float kernel[3] = { 1.0f, 2.0f / 3.0f, 1.0f / 6.0f };
    [unroll] for (int y = -2; y <= 2; ++y)
    [unroll] for (int x = -2; x <= 2; ++x)
    {
        if (x == 0 && y == 0) continue;
        int2 tap = int2(p) + int2(x, y) * int(g_Denoising.dilation);
        if (!DenoisingInside(tap)) continue;
        float4 tapColor = DenoisingColor(tap);
        float weight = kernel[abs(x)] * kernel[abs(y)] * DenoisingSurfaceWeight(surface, DenoisingFullPixel(tap), DenoisingGuide(tap)) *
            exp(-abs(luminance - DenoisingLuminance(tapColor.xyz)) / luminanceScale);
        color += tapColor.xyz * weight;
        resultVariance += tapColor.w * weight * weight;
        total += weight;
    }
    DenoisingWrite(p, float4(color / total, resultVariance / (total * total)));
}

void DenoisingBilateral(uint2 p)
{
    float4 sample = DenoisingSample(DenoisingFullPixel(p));
    float3 center = sample.xyz;
    float4 guide = DenoisingGuide(p);
    if (!(guide.w > 0.0f)) { DenoisingWrite(p, float4(center, 0.0f)); return; }
    DenoisingSurface surface = DenoisingMakeSurface(DenoisingFullPixel(p), guide);
    float3 color = center * sample.w;
    float total = sample.w;
    int radius = int(g_Denoising.settings.radius);
    float sigma = max(float(radius) * 0.5f, 0.5f);
    [loop] for (int y = -radius; y <= radius; ++y)
    [loop] for (int x = -radius; x <= radius; ++x)
    {
        int2 tap = int2(p) + int2(x, y);
        if ((x == 0 && y == 0) || !DenoisingInside(tap)) continue;
        float4 tapSample = DenoisingSample(DenoisingFullPixel(tap));
        if (tapSample.w <= 0.0f) continue;
        float3 difference = tapSample.xyz - center;
        float signalDistance = g_Denoising.scalarSignal != 0u ? difference.x * difference.x : dot(difference, difference) / 3.0f;
        float weight = g_Denoising.settings.guidance == 0u
            ? exp(-signalDistance / (2.0f * g_Denoising.settings.rangeSigma * g_Denoising.settings.rangeSigma))
            : DenoisingSurfaceWeight(surface, DenoisingFullPixel(tap), DenoisingGuide(tap));
        if (g_Denoising.settings.gaussian != 0u) weight *= exp(-float(x * x + y * y) / (2.0f * sigma * sigma));
        color += tapSample.xyz * weight;
        total += weight;
    }
    DenoisingWrite(p, float4(total > 0.0f ? color / total : center, 0.0f));
}

void DenoisingGradient(uint2 p)
{
    uint2 fullPixel = min(p * 3u + 1u, g_Denoising.fullSize - 1u);
    uint2 guidePixel = min(uint2(max(round(DenoisingGridPosition(float2(fullPixel))), 0.0f)), g_Denoising.size - 1u);
    DenoisingSurface surface = DenoisingMakeSurface(DenoisingFullPixel(guidePixel), DenoisingGuide(guidePixel));
    float3 gradient = 0.0f;
    float total = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        int2 tap = int2(p) + int2(x, y) * int(g_Denoising.dilation);
        if (any(tap < 0) || any(tap >= int2(g_Denoising.gradientSize))) continue;
        float3 value = t_Color[tap].xyz;
        if (!(value.z > 0.0f) || any(!isfinite(value))) continue;
        uint2 tapFull = min(uint2(tap) * 3u + 1u, g_Denoising.fullSize - 1u);
        uint2 tapGuidePixel = min(uint2(max(round(DenoisingGridPosition(float2(tapFull))), 0.0f)), g_Denoising.size - 1u);
        float weight = x == 0 && y == 0 ? 1.0f : DenoisingSurfaceWeight(surface, DenoisingFullPixel(tapGuidePixel), DenoisingGuide(tapGuidePixel));
        weight *= (x == 0 ? 1.0f : 0.5f) * (y == 0 ? 1.0f : 0.5f);
        gradient += value * weight;
        total += weight;
    }
    u_Color[p] = total > 0.0f ? float4(gradient / total, 0.0f) : 0.0f;
}

[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID)
{
#if DENOISING_STAGE == 5
    if (all(p < g_Denoising.gradientSize)) DenoisingGradient(p);
#else
    if (!DenoisingInside(p)) return;
#if DENOISING_STAGE == 0
    DenoisingPrepare(p);
#elif DENOISING_STAGE == 1
    DenoisingTemporal(p);
#elif DENOISING_STAGE == 2
    DenoisingVariance(p);
#elif DENOISING_STAGE == 3
    DenoisingAtrous(p);
#elif DENOISING_STAGE == 4
    DenoisingBilateral(p);
#endif
#endif
}
