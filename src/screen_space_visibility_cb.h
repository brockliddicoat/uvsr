#ifndef UVSR_SCREEN_SPACE_VISIBILITY_CB_H
#define UVSR_SCREEN_SPACE_VISIBILITY_CB_H

#include <donut/shaders/view_cb.h>
#include "sky_visibility_application.h"

// Shared by visibility sampling, reconstruction, and composition.
struct ScreenSpaceVisibilityConstants
{
    PlanarViewConstants view;

    float2 fullResolution;
    float2 samplingResolution;

    float radiusWorld;
    float thicknessWorld;
    float stepDistributionExponent;
    float padding0;

    float ambientStrength;
    float indirectDiffuseIntensity;
    float spatialRadius;
    uint frameIndex;

    uint maximumSampleCount;
    uint sourceRadianceAvailable;
    uint enableAmbientOcclusion;
    uint enableIndirectDiffuse;

    uint reverseDepth;
    uint orthographicProjection;
    uint resolutionScale;
    uint sampleScheduler;

    uint visibilityDebugView;
    uint packedEdgeMode;
    uint diffuseEnvironmentEnabled;
    float diffuseEnvironmentScale;

    uint diffuseEnvironmentArrayIndex;
    uint specularEnvironmentEnabled;
    float specularEnvironmentScale;
    float specularEnvironmentMipLevels;

    uint specularEnvironmentArrayIndex;
    uint spatialFilter;
    uint lightingDebugView;
    uint skyVisibilityApplication;
};

#endif // UVSR_SCREEN_SPACE_VISIBILITY_CB_H
