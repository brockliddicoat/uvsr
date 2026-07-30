#ifndef UVSR_SCREEN_SPACE_VISIBILITY_CB_H
#define UVSR_SCREEN_SPACE_VISIBILITY_CB_H

#include <donut/shaders/view_cb.h>

// Shared by sampling, temporal reconstruction, bilateral filtering, and
// composition. Directional masks remain register-local and are never written
// to a persistent texture by the default path.
struct ScreenSpaceVisibilityConstants
{
    PlanarViewConstants view;

    float2 fullResolution;
    float2 samplingResolution;

    float radiusWorld;
    float thicknessWorld;
    float stepDistributionExponent;
    float ambientPower;

    float ambientStrength;
    float indirectDiffuseIntensity;
    float minimumBounceContribution;
    float lightingExposureScale;

    float temporalResponse;
    float spatialRadius;
    uint frameIndex;
    uint maximumSampleCount;

    uint knownInactiveLightingSources;
    uint enableAmbientOcclusion;
    uint enableIndirectDiffuse;
    uint reverseDepth;

    uint orthographicProjection;
    uint useDepthHierarchy;
    uint resolutionScale;
    uint sampleScheduler;

    uint historyValid;
    uint showIndirectDiffuseOnly;
    uint packedEdgeMode;
    uint diffuseEnvironmentEnabled;

    float diffuseEnvironmentScale;
    uint diffuseEnvironmentArrayIndex;
    uint specularEnvironmentEnabled;
    float specularEnvironmentScale;

    float specularEnvironmentMipLevels;
    uint specularEnvironmentArrayIndex;
    uint padding0;
    uint padding1;
};

#endif // UVSR_SCREEN_SPACE_VISIBILITY_CB_H
