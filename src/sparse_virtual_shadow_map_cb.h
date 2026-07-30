#ifndef UVSR_SPARSE_VIRTUAL_SHADOW_MAP_CB_H
#define UVSR_SPARSE_VIRTUAL_SHADOW_MAP_CB_H

#include <donut/shaders/view_cb.h>

#define SVSM_CLIPMAP_COUNT 6

struct SparseVirtualShadowMapResolveConstants
{
    PlanarViewConstants cameraView;
    float4x4 worldToClip[SVSM_CLIPMAP_COUNT];

    float4 clipmapExtentAndTexelSize[SVSM_CLIPMAP_COUNT];

    uint2 outputSize;
    uint tapCount;
    uint resolutionBias;

    float depthBias;
    uint debugView;
    uint filterMode;
    uint adaptiveFiltering;
};

#endif // UVSR_SPARSE_VIRTUAL_SHADOW_MAP_CB_H
