#ifndef UVSR_AUTO_EXPOSURE_CB_H
#define UVSR_AUTO_EXPOSURE_CB_H

#include <donut/shaders/view_cb.h>

struct AutoExposureConstants
{
    uint2 viewOrigin;
    uint2 viewSize;

    float frameDeltaSeconds;
    float exposureCompensationEV;
    float adjustmentPeriodSeconds;
    uint resetExposure;

    float maximumBrighteningEV;
    float maximumDarkeningEV;
    float2 padding;
};

#endif
