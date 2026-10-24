#pragma once
#include "renderer_gpu_contract.h"
struct DenoisingReplaySamplingConstants
{
    UVSR_GPU_UINT2 sourceSize;
    UVSR_GPU_UINT2 gradientSize;
    UVSR_GPU_UINT frame;
    UVSR_GPU_UINT phase;
    UVSR_GPU_UINT valid;
    UVSR_GPU_UINT padding;
};
