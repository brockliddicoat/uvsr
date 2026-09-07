#pragma pack_matrix(row_major)

#include "auto_exposure_cb.h"
#include "auto_exposure_shared.h"

cbuffer c_AutoExposure : register(b0)
{
    AutoExposureConstants g_AutoExposure;
};

Buffer<uint> t_Histogram : register(t0);
RWBuffer<float> u_Exposure : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 dispatchPosition : SV_DispatchThreadID)
{
    uint validPixelCount = 0u;
    [unroll]
    for (uint bin = 0u;
        bin < UVSR_AUTO_EXPOSURE_HISTOGRAM_BIN_COUNT;
        ++bin)
        validPixelCount += t_Histogram[bin];

    float previousExposure = u_Exposure[0];
    if (!isfinite(previousExposure) || !(previousExposure > 0.0f))
        previousExposure = 1.0f;

    const float minimumAutomaticEV =
        -max(g_AutoExposure.maximumDarkeningEV, 0.0f);
    const float maximumAutomaticEV =
        max(g_AutoExposure.maximumBrighteningEV, 0.0f);
    const float boundedPreviousEV = clamp(
        log2(previousExposure),
        minimumAutomaticEV,
        maximumAutomaticEV);
    float adaptedExposure = exp2(boundedPreviousEV);
    if (validPixelCount > 0u)
    {
        const uint medianRank = (validPixelCount + 1u) / 2u;
        uint cumulative = 0u;
        uint medianBin = 0u;
        [loop]
        for (uint bin = 0u;
            bin < UVSR_AUTO_EXPOSURE_HISTOGRAM_BIN_COUNT;
            ++bin)
        {
            cumulative += t_Histogram[bin];
            if (cumulative >= medianRank)
            {
                medianBin = bin;
                break;
            }
        }

        const float normalizedBinCenter =
            (float(medianBin) + 0.5f) /
                float(UVSR_AUTO_EXPOSURE_HISTOGRAM_BIN_COUNT);
        const float meteredLuminance = exp2(lerp(
            UVSR_AUTO_EXPOSURE_MINIMUM_LOG_LUMINANCE,
            UVSR_AUTO_EXPOSURE_MAXIMUM_LOG_LUMINANCE,
            normalizedBinCenter));
        const float targetEV = clamp(
            log2(UVSR_AUTO_EXPOSURE_MIDDLE_GRAY /
                max(meteredLuminance, 1e-8f)),
            minimumAutomaticEV,
            maximumAutomaticEV);
        const float targetExposure = exp2(targetEV);
        if (g_AutoExposure.resetExposure != 0u)
        {
            adaptedExposure = targetExposure;
        }
        else
        {
            const float blend = 1.0f - exp2(
                -max(g_AutoExposure.frameDeltaSeconds, 0.0f) /
                    max(g_AutoExposure.adjustmentPeriodSeconds, 1e-4f));
            adaptedExposure = exp2(clamp(lerp(
                boundedPreviousEV,
                targetEV,
                saturate(blend)),
                minimumAutomaticEV,
                maximumAutomaticEV));
        }
    }
    else if (g_AutoExposure.resetExposure != 0u)
    {
        adaptedExposure = 1.0f;
    }

    u_Exposure[0] = adaptedExposure;
    u_Exposure[1] = adaptedExposure * exp2(
        g_AutoExposure.exposureCompensationEV);
}
