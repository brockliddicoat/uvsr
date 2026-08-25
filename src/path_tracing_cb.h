#ifndef UVSR_PATH_TRACING_CB_H
#define UVSR_PATH_TRACING_CB_H

#include "renderer_gpu_contract.h"
#include "flashlight_shared.h"

#define UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH (1u << 0u)
#define UVSR_PATH_TRACING_FLAG_SHOW_ENVIRONMENT_BACKGROUND (1u << 1u)

struct PathTracingConstants
{
    PlanarViewConstants view;
    PlanarViewConstants previousView;
    FlashlightBeamProfileBinding flashlight;

    float environmentScale;
    float rayBias;
    float maximumRayDistance;
    UVSR_GPU_UINT noisePattern;

    UVSR_GPU_UINT2 dispatchExtent;
    UVSR_GPU_UINT lightCount;
    UVSR_GPU_UINT flags;

    UVSR_GPU_UINT previousViewValid;
    UVSR_GPU_UINT instanceCount;
    UVSR_GPU_UINT padding0;
    UVSR_GPU_UINT padding1;

    UVSR_GPU_UINT4 rayMaterialLimits;
};

#endif
