#ifndef UVSR_SPECULAR_REFLECTIONS_CB_H
#define UVSR_SPECULAR_REFLECTIONS_CB_H

#include <donut/shaders/view_cb.h>

struct SpecularReflectionConstants
{
    PlanarViewConstants view;

    float renderWidth;
    float renderHeight;
    float inverseRenderWidth;
    float inverseRenderHeight;

    float specularEnvironmentScale;
    float specularEnvironmentMipLevels;
    float specularEnvironmentArrayIndex;
    uint skyEstimateKind;

    float maxRoughness;
    float depthThickness;
    float traceAndFilterBias;
    float screenSpaceConfidence;

    float rayMaxDistance;
    float rayBias;
    float backfaceRejection;
    float depthRejection;

    uint frameIndex;
    uint reflectionTechnique;
    uint samplesPerQuad;
    uint ratioEstimator;

    uint deferredColorLookup;
    uint reverseDepth;
    uint maxTraversalIterations;
    uint minTraversalOccupancy;

    uint mostDetailedMip;
    uint diagnosticView;
    uint sceneColorMipLevels;
    uint depthMipLevels;

    uint useRayTracing;
    uint compactRayTracing;
    uint padding0;
    uint padding1;
};

#endif // UVSR_SPECULAR_REFLECTIONS_CB_H
