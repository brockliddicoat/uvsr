#ifndef UVSR_RAY_TRACED_SKY_VISIBILITY_CB_H
#define UVSR_RAY_TRACED_SKY_VISIBILITY_CB_H

#include <donut/shaders/view_cb.h>

struct RayTracedSkyVisibilityConstants
{
    PlanarViewConstants view;

    uint sampleSequencePhase;
    uint sampleCount;
    uint noisePattern;
    float rayDistance;

    float depthQuantizationStep;
    float rayBias;
    uint reverseDepth;
    uint floatDepth;
};

#endif // UVSR_RAY_TRACED_SKY_VISIBILITY_CB_H
