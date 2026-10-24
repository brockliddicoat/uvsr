#ifndef UVSR_DENOISING_CB_H
#define UVSR_DENOISING_CB_H

#include <donut/shaders/view_cb.h>

// Shared by the application side guide preparation and guided full resolution
// resolve. NRD receives its camera matrices through native CommonSettings.
struct DenoisingConstants
{
    PlanarViewConstants view;

    float2 fullResolution;
    float2 denoiserResolution;
    float2 sourceResolution;
    float2 fullResolutionInv;
    float2 denoiserResolutionInv;
    float2 sourceResolutionInv;

    float hitDistanceNormalization;
    float motionScaleX;
    float motionScaleY;
    float denoisingRange;

    float3 localLightPosition;
    float localLightRadius;

    float directionalTanAngularRadius;
    uint reverseDepth;
    uint method;
    uint signalType;
};

#endif // UVSR_DENOISING_CB_H
