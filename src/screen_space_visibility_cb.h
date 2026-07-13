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

    float aoTemporalResponse;
    float giTemporalResponse;
    float depthRejection;
    float normalRejection;

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

    uint temporalEnabled;
    uint spatialEnabled;
    uint spatialRadius;
    uint historyValid;

    uint freezeSamplingPhase;
    uint sectorHitCriterion;
    uint debugMode;
    uint reverseDepth;

    uint orthographicProjection;
    uint useDepthHierarchy;
    // Motion XY is de-jittered, while AO/GI and their validation surfaces are
    // written on the raw jittered grid. The current jitter is already carried
    // by view.pixelOffset; retain the producer frame's jitter here so temporal
    // reprojection can address raw history without applying the delta twice.
    float2 previousJitter;
};

#endif // UVSR_SCREEN_SPACE_VISIBILITY_CB_H
