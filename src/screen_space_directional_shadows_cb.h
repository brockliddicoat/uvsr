#ifndef UVSR_SCREEN_SPACE_DIRECTIONAL_SHADOWS_CB_H
#define UVSR_SCREEN_SPACE_DIRECTIONAL_SHADOWS_CB_H

struct ScreenSpaceDirectionalShadowConstants
{
    // Receiver-to-light direction transformed as a homogeneous vector by the
    // jittered view-projection matrix. Translation therefore has no effect.
    float4 projectedLight;

    float2 clipToWindowScale;
    float2 clipToWindowBias;

    uint2 textureSize;
    uint traceSampleCount;
    uint tracePadding;

    float surfaceThickness;
    float depthDiscontinuityThreshold;
    float shadowContrast;
    float tracePadding0;

    uint hardShadowSamples;
    uint fadeOutSamples;
    uint ignoreEdgePixels;
    uint usePrecisionOffset;

    uint bilinearSamplingOffsetMode;
    uint useEarlyOut;
    uint debugView;
    uint reverseDepth;
};

#endif // UVSR_SCREEN_SPACE_DIRECTIONAL_SHADOWS_CB_H
