#ifndef UVSR_IMAGE_BASED_LIGHTING_BACKGROUND_CB_H
#define UVSR_IMAGE_BASED_LIGHTING_BACKGROUND_CB_H

#include "renderer_gpu_scalar.h"

struct ImageBasedLightingBackgroundConstants
{
    UVSR_GPU_FLOAT4X4 matClipToTranslatedWorld;

    float radianceScale;
    UVSR_GPU_FLOAT3 padding;
};

#ifdef __cplusplus
static_assert(sizeof(ImageBasedLightingBackgroundConstants) == 80);
static_assert(offsetof(ImageBasedLightingBackgroundConstants, radianceScale) == 64);
#endif

#endif
