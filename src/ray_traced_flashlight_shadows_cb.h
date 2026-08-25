#ifndef UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_CB_H
#define UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_CB_H

#include "renderer_gpu_contract.h"

#include "flashlight_shared.h"

struct RayTracedFlashlightShadowConstants
{
    PlanarViewConstants view;

    UVSR_GPU_FLOAT4 lightPositionAndRange;
    UVSR_GPU_FLOAT4 lightDirectionAndEmitterRadius;
    FlashlightBeamProfile beamProfile;

    float depthQuantizationStep;
    float rayBias;
    UVSR_GPU_UINT reverseDepth;
    UVSR_GPU_UINT floatDepth;

    UVSR_GPU_UINT sampleSequencePhase;
    UVSR_GPU_UINT sampleCount;
    UVSR_GPU_UINT noisePattern;
    UVSR_GPU_UINT sampleSequenceMode;
};

#endif // UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_CB_H
