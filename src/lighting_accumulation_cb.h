#ifndef UVSR_LIGHTING_ACCUMULATION_CB_H
#define UVSR_LIGHTING_ACCUMULATION_CB_H

#include "renderer_gpu_scalar.h"

struct LightingAccumulationConstants
{
    UVSR_GPU_UINT2 extent;
    UVSR_GPU_UINT resetHistory;
    UVSR_GPU_UINT padding;
};

#ifdef __cplusplus
static_assert(sizeof(LightingAccumulationConstants) == 16);
static_assert(offsetof(LightingAccumulationConstants, extent) == 0);
static_assert(offsetof(LightingAccumulationConstants, resetHistory) == 8);
static_assert(offsetof(LightingAccumulationConstants, padding) == 12);
#endif

#endif // UVSR_LIGHTING_ACCUMULATION_CB_H
