#ifndef UVSR_DIRECTIONAL_RAY_VISIBILITY_CB_H
#define UVSR_DIRECTIONAL_RAY_VISIBILITY_CB_H

#include "renderer_gpu_contract.h"

struct DirectionalRayVisibilityConstants
{
    PlanarViewConstants view;
    UVSR_GPU_FLOAT4 directionToLightAndDistance;
    float rayBias;
    float depthQuantizationStep;
    UVSR_GPU_UINT reverseDepth;
    UVSR_GPU_UINT floatDepth;
    float angularDiameter;
    UVSR_GPU_UINT sampleCount;
    UVSR_GPU_UINT sampleSequencePhase;
    UVSR_GPU_UINT sampleSequenceMode;
    UVSR_GPU_UINT noisePattern;
    UVSR_GPU_UINT padding1;
    UVSR_GPU_UINT padding2;
    UVSR_GPU_UINT padding3;
};

#endif
