#ifndef UVSR_SCREEN_SPACE_VISIBILITY_CB_H
#define UVSR_SCREEN_SPACE_VISIBILITY_CB_H

#include <donut/shaders/view_cb.h>

// Shared by every stage of the current-frame screen-space visibility pipeline.
// Persistent radial masks intentionally are not represented here: the current
// implementation keeps one 32-bit mask per slice in registers.
struct ScreenSpaceVisibilityConstants
{
    PlanarViewConstants view;

    float2 fullResolution;
    float2 samplingResolution;

    float radiusWorld;
    float thicknessWorld;
    float thicknessDistanceScale;
    float stepDistributionExponent;

    float radialJitter;
    float ambientStrength;
    float ambientPower;
    float indirectDiffuseIntensity;

    float emissiveGain;
    float minimumBounceContribution;
    float lightingExposureScale;
    float paddingSampling2;

    float3 ambientColorTop;
    float padding0;
    float3 ambientColorBottom;
    float padding1;

    uint frameIndex;
    uint sliceCount;
    uint sampleCount;
    uint knownInactiveLightingSources;

    uint enableAmbientOcclusion;
    uint enableIndirectDiffuse;
    uint includeEmissive;
    uint distanceScaledThickness;

    uint freezeSamplingPhase;
    uint sectorHitCriterion;
    uint debugMode;
    uint reverseDepth;

    uint orthographicProjection;
    uint useDepthHierarchy;
    uint padding2;
    uint padding3;
};

#endif // UVSR_SCREEN_SPACE_VISIBILITY_CB_H
