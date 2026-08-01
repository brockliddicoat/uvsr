#ifndef UVSR_SCREEN_SPACE_DIRECTIONAL_SHADOWS_CB_H
#define UVSR_SCREEN_SPACE_DIRECTIONAL_SHADOWS_CB_H

struct ScreenSpaceDirectionalShadowConstants
{
    float4 lightCoordinate;

    int2 waveOffset;
    float surfaceThickness;
    float bilinearThreshold;

    float shadowContrast;
    uint ignoreEdgePixels;
    uint usePrecisionOffset;
    uint bilinearSamplingOffsetMode;

    uint debugOutputEdgeMask;
    uint debugOutputThreadIndex;
    uint debugOutputWaveIndex;
    uint useEarlyOut;

    float2 depthBounds;
    float farDepthValue;
    float nearDepthValue;

    float2 invDepthTextureSize;
    float2 padding;
};

#endif // UVSR_SCREEN_SPACE_DIRECTIONAL_SHADOWS_CB_H
