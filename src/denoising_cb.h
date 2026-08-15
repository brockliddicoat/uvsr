#ifndef UVSR_DENOISING_CB_H
#define UVSR_DENOISING_CB_H

#include <donut/shaders/view_cb.h>

// Shared by built-in spatial denoising, application-side NRD guide
// preparation, and the guided full-resolution resolve. NRD receives its
// camera matrices through native CommonSettings.
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

    float spatialRadius;
    uint spatialMethod;
    uint2 spatialPadding;
};

#endif // UVSR_DENOISING_CB_H
