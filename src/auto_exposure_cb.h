#ifndef UVSR_AUTO_EXPOSURE_CB_H
#define UVSR_AUTO_EXPOSURE_CB_H

#include "renderer_gpu_scalar.h"

struct AutoExposureConstants
{
    UVSR_GPU_UINT2 viewOrigin;
    UVSR_GPU_UINT2 viewSize;

    float frameDeltaSeconds;
    float exposureCompensationEV;
    float adjustmentPeriodSeconds;
    UVSR_GPU_UINT resetExposure;

    float maximumBrighteningEV;
    float maximumDarkeningEV;
    UVSR_GPU_FLOAT2 padding;
};

#ifdef __cplusplus
static_assert(sizeof(AutoExposureConstants) == 48);
static_assert(offsetof(AutoExposureConstants, viewOrigin) == 0);
static_assert(offsetof(AutoExposureConstants, viewSize) == 8);
static_assert(offsetof(AutoExposureConstants, frameDeltaSeconds) == 16);
static_assert(offsetof(AutoExposureConstants, exposureCompensationEV) == 20);
static_assert(offsetof(AutoExposureConstants, adjustmentPeriodSeconds) == 24);
static_assert(offsetof(AutoExposureConstants, resetExposure) == 28);
static_assert(offsetof(AutoExposureConstants, maximumBrighteningEV) == 32);
static_assert(offsetof(AutoExposureConstants, maximumDarkeningEV) == 36);
static_assert(offsetof(AutoExposureConstants, padding) == 40);
#endif

#endif
