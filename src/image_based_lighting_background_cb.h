#ifndef UVSR_IMAGE_BASED_LIGHTING_BACKGROUND_CB_H
#define UVSR_IMAGE_BASED_LIGHTING_BACKGROUND_CB_H

struct ImageBasedLightingBackgroundConstants
{
    float4x4 matClipToTranslatedWorld;

    float radianceScale;
    float3 padding;
};

#endif // UVSR_IMAGE_BASED_LIGHTING_BACKGROUND_CB_H
