#ifndef UVSR_DENOISING_CB_H
#define UVSR_DENOISING_CB_H

#include "renderer_gpu_contract.h"

// Shared by built-in spatial denoising, application-side NRD guide
// preparation, and the guided full-resolution resolve. NRD receives its
// camera matrices through native CommonSettings.
struct DenoisingConstants
{
    PlanarViewConstants view;

    UVSR_GPU_FLOAT2 fullResolution;
    UVSR_GPU_FLOAT2 denoiserResolution;
    UVSR_GPU_FLOAT2 sourceResolution;
    UVSR_GPU_FLOAT2 fullResolutionInv;
    UVSR_GPU_FLOAT2 denoiserResolutionInv;
    UVSR_GPU_FLOAT2 sourceResolutionInv;

    float hitDistanceNormalization;
    float motionScaleX;
    float motionScaleY;
    float denoisingRange;

    UVSR_GPU_FLOAT3 localLightPosition;
    float localLightRadius;

    float directionalTanAngularRadius;
    UVSR_GPU_UINT reverseDepth;
    UVSR_GPU_UINT method;
    UVSR_GPU_UINT signalType;

    float spatialRadius;
    UVSR_GPU_UINT spatialMethod;
    UVSR_GPU_UINT2 spatialPadding;
};

#endif // UVSR_DENOISING_CB_H
