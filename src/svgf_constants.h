#ifndef UVSR_SVGF_CONSTANTS_H
#define UVSR_SVGF_CONSTANTS_H

#include "renderer_gpu_contract.h"

struct SvgfConstants
{
    PlanarViewConstants currentView;
    PlanarViewConstants previousView;
    UVSR_GPU_UINT2 extent;
    UVSR_GPU_UINT2 surfaceExtent;
    UVSR_GPU_UINT2 sampleStride;
    UVSR_GPU_UINT reset;
    UVSR_GPU_UINT stepSize;
    float alpha;
    float momentsAlpha;
    float phiColor;
    float phiNormal;
    UVSR_GPU_UINT scalar;
    UVSR_GPU_UINT material;
    UVSR_GPU_UINT reverseDepth;
    UVSR_GPU_UINT padding;
};

#endif
