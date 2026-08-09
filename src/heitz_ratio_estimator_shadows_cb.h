#ifndef UVSR_HEITZ_RATIO_ESTIMATOR_SHADOWS_CB_H
#define UVSR_HEITZ_RATIO_ESTIMATOR_SHADOWS_CB_H

#include <donut/shaders/view_cb.h>

struct HeitzRatioEstimatorShadowConstants
{
    PlanarViewConstants view;

    // Receiver-to-light center direction and angular half-radius in radians.
    float4 directionToLightAndAngularRadius;

    uint sampleSequencePhase;
    uint sampleCount;
    uint hardShadows;
    uint noisePattern;

    float rayDistance;
    float denominatorEpsilon;
    float depthQuantizationStep;
    float rayBias;

    uint reverseDepth;
    uint floatDepth;
    uint useRatioEstimator;
    uint padding1;
};

#endif // UVSR_HEITZ_RATIO_ESTIMATOR_SHADOWS_CB_H
