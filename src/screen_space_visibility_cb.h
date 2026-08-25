#ifndef UVSR_SCREEN_SPACE_VISIBILITY_CB_H
#define UVSR_SCREEN_SPACE_VISIBILITY_CB_H

#include "renderer_gpu_contract.h"
#include "sky_visibility_application.h"

// Shared by visibility sampling, required upsampling, and composition.
struct ScreenSpaceVisibilityConstants
{
    PlanarViewConstants view;

    UVSR_GPU_FLOAT2 fullResolution;
    UVSR_GPU_FLOAT2 samplingResolution;

    float radiusWorld;
    float thicknessWorld;
    float stepDistributionExponent;
    UVSR_GPU_UINT sampleSequenceMode;

    float ambientStrength;
    float indirectDiffuseIntensity;
    UVSR_GPU_UINT sampleSequencePhase;
    UVSR_GPU_UINT maximumSampleCount;

    UVSR_GPU_UINT sourceRadianceAvailable;
    UVSR_GPU_UINT enableAmbientOcclusion;
    UVSR_GPU_UINT enableIndirectDiffuse;
    UVSR_GPU_UINT reverseDepth;

    UVSR_GPU_UINT orthographicProjection;
    UVSR_GPU_UINT resolutionScale;
    UVSR_GPU_UINT noisePattern;
    UVSR_GPU_UINT visibilityDebugView;

    UVSR_GPU_UINT diffuseEnvironmentEnabled;
    float diffuseEnvironmentScale;
    UVSR_GPU_UINT diffuseEnvironmentArrayIndex;
    UVSR_GPU_UINT specularEnvironmentEnabled;

    float specularEnvironmentScale;
    float specularEnvironmentMipLevels;
    UVSR_GPU_UINT specularEnvironmentArrayIndex;
    UVSR_GPU_UINT lightingDebugView;

    UVSR_GPU_UINT skyVisibilityApplication;
    UVSR_GPU_UINT padding0;
    UVSR_GPU_UINT padding1;
    UVSR_GPU_UINT padding2;
};

#endif // UVSR_SCREEN_SPACE_VISIBILITY_CB_H
