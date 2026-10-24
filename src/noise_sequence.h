#ifndef UVSR_NOISE_SEQUENCE_H
#define UVSR_NOISE_SEQUENCE_H

#include "shader_math.h"

UVSR_SHADER_INLINE ShaderUint UVSRNoiseHash(ShaderUint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

UVSR_SHADER_INLINE ShaderUint UVSRNoisePhaseCoordinate(
    ShaderUint phase, ShaderUint resolution, ShaderUint recurrence)
{
    // the high bits quantize the fractional Weyl recurrence. masking its low
    // bits instead visits only one diagonal orbit and repeats after resolution
    // frames. retained resolutions are powers of two no larger than 512.
    return (((phase * recurrence) >> 16u) * resolution) >> 16u;
}

#endif
