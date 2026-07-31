#ifndef UVSR_TAA_TEMPORAL_AA_DEBUG_HLSLI
#define UVSR_TAA_TEMPORAL_AA_DEBUG_HLSLI

float3 TemporalAaDebugHeatmap(float value)
{
    value = saturate(value);
    return saturate(float3(
        1.5 - abs(4.0 * value - 3.0),
        1.5 - abs(4.0 * value - 2.0),
        1.5 - abs(4.0 * value - 1.0)));
}

#endif
