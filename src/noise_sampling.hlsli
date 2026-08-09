#ifndef UVSR_NOISE_SAMPLING_HLSLI
#define UVSR_NOISE_SAMPLING_HLSLI

static const uint UVSR_NOISE_PATTERN_SPATIAL_WHITE = 0u;
static const uint UVSR_NOISE_PATTERN_SPATIAL_BLUE = 1u;
static const uint UVSR_NOISE_PATTERN_SPATIOTEMPORAL_BLUE = 2u;

uint UVSRNoiseHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

uint2 UVSRNoiseStreamOffset(uint semanticStream)
{
    return uint2(
        UVSRNoiseHash(semanticStream ^ 0x68bc21ebu),
        UVSRNoiseHash(semanticStream ^ 0x02e5be93u));
}

uint2 UVSRNoiseSpatialPhaseOffset(uint phase)
{
    // Integer Weyl steps corresponding to the two-dimensional R2 additive
    // recurrence. The texture mask below makes the translation toroidal.
    return phase * uint2(0xc13fa9a9u, 0x91e10da5u);
}

uint2 UVSRNoiseCenteredCoordinate(
    uint2 localDispatchPixel,
    uint2 localDispatchExtent,
    uint resolution,
    uint2 translation)
{
    const int2 centered = int2(localDispatchPixel) -
        int2(localDispatchExtent / 2u) +
        int2(resolution / 2u, resolution / 2u);
    const uint2 mask = uint2(resolution - 1u, resolution - 1u);
    return (uint2(centered) + translation) & mask;
}

float UVSRDecodeR8Noise(float value)
{
    // R8_UNORM Load returns k / 255. Decode to the center of the source bin,
    // keeping every result strictly inside the open unit interval.
    return (value * 255.0f + 0.5f) * (1.0f / 256.0f);
}

float UVSRSamplePrecomputedNoise(
    Texture2DArray<float> noiseTexture,
    uint noisePattern,
    uint2 localDispatchPixel,
    uint2 localDispatchExtent,
    uint phase,
    uint semanticStream)
{
    uint width;
    uint height;
    uint layerCount;
    noiseTexture.GetDimensions(width, height, layerCount);

    const uint resolution = max(min(width, height), 1u);
    const uint safeLayerCount = max(layerCount, 1u);
    uint2 translation = UVSRNoiseStreamOffset(semanticStream);
    uint layer = 0u;

    if (noisePattern == UVSR_NOISE_PATTERN_SPATIOTEMPORAL_BLUE)
    {
        // STBN advances only through Z. Keeping XY fixed preserves the
        // intended spatiotemporal sequence for each semantic stream.
        layer = phase & (safeLayerCount - 1u);
    }
    else
    {
        // Spatial textures have one slice. Animation translates the complete
        // tile without changing its center anchor or per-stream separation.
        translation += UVSRNoiseSpatialPhaseOffset(phase);
    }

    const uint2 coordinate = UVSRNoiseCenteredCoordinate(
        localDispatchPixel,
        localDispatchExtent,
        resolution,
        translation);
    const float encoded = noiseTexture.Load(int4(
        int2(coordinate),
        int(layer),
        0));
    return UVSRDecodeR8Noise(encoded);
}

#endif
