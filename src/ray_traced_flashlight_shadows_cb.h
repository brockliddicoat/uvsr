#ifndef UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_CB_H
#define UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_CB_H

#include <donut/shaders/view_cb.h>

#include "flashlight_shared.h"

struct RayTracedFlashlightShadowConstants
{
    PlanarViewConstants view;

    float4 lightPositionAndRange;
    float4 lightDirectionAndEmitterRadius;
    FlashlightBeamProfile beamProfile;

    float depthQuantizationStep;
    float rayBias;
    uint reverseDepth;
    uint floatDepth;

    uint sampleSequencePhase;
    uint sampleCount;
    uint noisePattern;
    uint padding0;
};

#endif // UVSR_RAY_TRACED_FLASHLIGHT_SHADOWS_CB_H
