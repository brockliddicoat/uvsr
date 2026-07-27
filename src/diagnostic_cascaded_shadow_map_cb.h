#ifndef UVSR_DIAGNOSTIC_CASCADED_SHADOW_MAP_CB_H
#define UVSR_DIAGNOSTIC_CASCADED_SHADOW_MAP_CB_H

#include <donut/shaders/view_cb.h>

#define DIAGNOSTIC_CSM_MAX_CASCADES 4

struct DiagnosticCsmResolveConstants
{
    PlanarViewConstants cameraView;
    // World-to-UVZW on the reference permutation; camera-clip-to-UVZW on the
    // independently selectable precomposed receiver-transform permutation.
    float4x4 worldToUvzw[DIAGNOSTIC_CSM_MAX_CASCADES];
    // nominalNear, nominalFar, projectedFar, cascadeFadeOffset
    float4 cascadeDepthRanges[DIAGNOSTIC_CSM_MAX_CASCADES];
    // cascadeFadeLength, updateAction, UE soft-transition scale, unused
    float4 cascadeParameters[DIAGNOSTIC_CSM_MAX_CASCADES];

    uint2 outputSize;
    uint cascadeCount;
    uint filterMode;

    uint tapCount;
    uint debugView;
    float filterRadiusTexels;
    float receiverDepthBias;

    float maximumShadowDistance;
    float distanceFadeoutFraction;
    float shadowMapResolutionInv;
    uint shadowMapResolution;

    float3 directionToLight;
    float receiverPadding;
};

struct DiagnosticCsmScrollConstants
{
    int2 sourceOffset;
    uint sourceArraySlice;
    uint padding;
};

#endif // UVSR_DIAGNOSTIC_CASCADED_SHADOW_MAP_CB_H
