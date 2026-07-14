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
    float adaptiveStrength;

    float ambientStrength;
    float indirectDiffuseIntensity;
    float emissiveGain;
    float minimumBounceContribution;

    float lightingExposureScale;
    float temporalResponse;
    float spatialRadius;
    float padding0;

    float3 ambientColorTop;
    float padding1;
    float3 ambientColorBottom;
    float padding2;

    uint frameIndex;
    uint minimumSampleCount;
    uint maximumSampleCount;
    uint maximumRefinementSlices;

    uint knownInactiveLightingSources;
    uint enableAmbientOcclusion;
    uint enableIndirectDiffuse;
    uint includeEmissive;

    uint reverseDepth;
    uint orthographicProjection;
    uint useDepthHierarchy;
    uint resolutionScale;

    uint sampleScheduler;
    uint adaptiveSamplingEnabled;
    uint feedbackValid;
    uint historyValid;

    uint collectSamplingStatistics;
    uint showIndirectDiffuseOnly;
    uint padding3;
    uint padding4;
};

#endif // UVSR_SCREEN_SPACE_VISIBILITY_CB_H
