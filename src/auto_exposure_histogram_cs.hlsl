#pragma pack_matrix(row_major)

#include "auto_exposure_cb.h"
#include "auto_exposure_shared.h"

cbuffer c_AutoExposure : register(b0)
{
    AutoExposureConstants g_AutoExposure;
};

Texture2D<float4> t_SceneColor : register(t0);
RWBuffer<uint> u_Histogram : register(u0);

static const float3 AutoExposureLuminanceWeights = float3(
    0.2126f,
    0.7152f,
    0.0722f);

[numthreads(16, 16, 1)]
void main(uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (any(dispatchPosition >= g_AutoExposure.viewSize))
        return;

    const uint2 pixelPosition =
        dispatchPosition + g_AutoExposure.viewOrigin;
    const float3 color = t_SceneColor.Load(
        int3(pixelPosition, 0)).rgb;
    if (!all(isfinite(color)))
        return;

    const float luminance = dot(max(color, 0.0f),
        AutoExposureLuminanceWeights);
    if (!isfinite(luminance) || !(luminance > 1e-8f))
        return;

    const float normalized = saturate(
        (log2(luminance) -
            UVSR_AUTO_EXPOSURE_MINIMUM_LOG_LUMINANCE) /
        (UVSR_AUTO_EXPOSURE_MAXIMUM_LOG_LUMINANCE -
            UVSR_AUTO_EXPOSURE_MINIMUM_LOG_LUMINANCE));
    const uint bin = min(
        uint(normalized *
            float(UVSR_AUTO_EXPOSURE_HISTOGRAM_BIN_COUNT)),
        UVSR_AUTO_EXPOSURE_HISTOGRAM_BIN_COUNT - 1u);
    InterlockedAdd(u_Histogram[bin], 1u);
}
