#ifndef UVSR_BEND_SCREEN_SPACE_SHADOWS_CB_H
#define UVSR_BEND_SCREEN_SPACE_SHADOWS_CB_H

struct BendScreenSpaceShadowConstants
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

#endif // UVSR_BEND_SCREEN_SPACE_SHADOWS_CB_H
