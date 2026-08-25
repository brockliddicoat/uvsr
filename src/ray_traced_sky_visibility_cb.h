#ifndef UVSR_RAY_TRACED_SKY_VISIBILITY_CB_H
#define UVSR_RAY_TRACED_SKY_VISIBILITY_CB_H

#include "renderer_gpu_contract.h"

struct RayTracedSkyVisibilityConstants
{
    PlanarViewConstants view;

    UVSR_GPU_UINT sampleSequencePhase;
    UVSR_GPU_UINT sampleCount;
    UVSR_GPU_UINT noisePattern;
    float rayDistance;

    float depthQuantizationStep;
    float rayBias;
    UVSR_GPU_UINT reverseDepth;
    UVSR_GPU_UINT floatDepth;

    UVSR_GPU_UINT sampleSequenceMode;
    UVSR_GPU_UINT3 padding0;
};

#endif // UVSR_RAY_TRACED_SKY_VISIBILITY_CB_H
