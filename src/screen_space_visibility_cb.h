#ifndef UVSR_SCREEN_SPACE_VISIBILITY_CB_H
#define UVSR_SCREEN_SPACE_VISIBILITY_CB_H

#include <donut/shaders/view_cb.h>
#include "sky_visibility_application.h"

// Shared by visibility sampling, required upsampling, and composition.
struct ScreenSpaceVisibilityConstants
{
    PlanarViewConstants view;

    float2 fullResolution;
    float2 samplingResolution;

    float radiusWorld;
    float thicknessWorld;
    float stepDistributionExponent;
    uint sampleSequenceMode;

    float ambientStrength;
    float indirectDiffuseIntensity;
    uint sampleSequencePhase;
    uint maximumSampleCount;

    uint sourceRadianceAvailable;
    uint enableAmbientOcclusion;
    uint enableIndirectDiffuse;
    uint reverseDepth;

    uint orthographicProjection;
    uint resolutionScale;
    uint noisePattern;
    uint visibilityDebugView;

    uint diffuseEnvironmentEnabled;
    float diffuseEnvironmentScale;
    uint diffuseEnvironmentArrayIndex;
    uint specularEnvironmentEnabled;

    float specularEnvironmentScale;
    float specularEnvironmentMipLevels;
    uint specularEnvironmentArrayIndex;
    uint lightingDebugView;

    uint skyVisibilityApplication;
    uint padding0;
    uint padding1;
    uint padding2;
};

#endif // UVSR_SCREEN_SPACE_VISIBILITY_CB_H
