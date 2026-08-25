#ifndef UVSR_PATH_TRACING_TRANSPORT_CONTRACT_H
#define UVSR_PATH_TRACING_TRANSPORT_CONTRACT_H

#include "pbr_surface_light_contract.h"

#define UVSR_PATH_TRACING_BOUNCE_COUNT 4u
#define UVSR_PATH_TRACING_SAMPLES_PER_FRAME 1u
#define UVSR_PATH_TRACING_RUSSIAN_ROULETTE_START 3u
#define UVSR_PATH_TRACING_RETRY_GENERATION_CLEARED 0u
#define UVSR_PATH_TRACING_RETRY_GENERATION_FIRST 1u

#ifdef __cplusplus

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

using PathTracingTransportUint = std::uint32_t;
using PathTracingTransportFloat3 = PbrContractFloat3;

struct PathTracingTransportUint2
{
    PathTracingTransportUint x;
    PathTracingTransportUint y;
};

struct PathTracingTransportFloat2
{
    float x;
    float y;
};

inline PathTracingTransportUint PathTracingTransportAsUint(
    float value) noexcept
{
    PathTracingTransportUint bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline PathTracingTransportFloat2 PathTracingTransportMakeFloat2(
    float x,
    float y) noexcept
{
    return { x, y };
}

inline PathTracingTransportFloat3 PathTracingTransportMakeFloat3(
    float x,
    float y,
    float z) noexcept
{
    return { x, y, z };
}

inline PathTracingTransportUint2 PathTracingTransportMakeUint2(
    PathTracingTransportUint x,
    PathTracingTransportUint y) noexcept
{
    return { x, y };
}

inline PathTracingTransportFloat3 PathTracingTransportAdd3(
    PathTracingTransportFloat3 left,
    PathTracingTransportFloat3 right) noexcept
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

inline PathTracingTransportFloat3 PathTracingTransportMultiply3(
    PathTracingTransportFloat3 left,
    PathTracingTransportFloat3 right) noexcept
{
    return { left.x * right.x, left.y * right.y, left.z * right.z };
}

inline PathTracingTransportFloat3 PathTracingTransportScale3(
    PathTracingTransportFloat3 value,
    float scale) noexcept
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

inline PathTracingTransportFloat3 PathTracingTransportMaxZero3(
    PathTracingTransportFloat3 value) noexcept
{
    return {
        std::max(value.x, 0.f),
        std::max(value.y, 0.f),
        std::max(value.z, 0.f)
    };
}

inline bool PathTracingTransportIsFinite3(
    PathTracingTransportFloat3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

inline bool PathTracingTransportIsNonnegative3(
    PathTracingTransportFloat3 value) noexcept
{
    return value.x >= 0.f && value.y >= 0.f && value.z >= 0.f;
}

#define UVSR_PATH_TRANSPORT_INLINE inline
#define UVSR_PATH_TRANSPORT_INOUT(type) type&

#else

#define PathTracingTransportUint uint
#define PathTracingTransportUint2 uint2
#define PathTracingTransportFloat2 float2
#define PathTracingTransportFloat3 float3

uint PathTracingTransportAsUint(float value)
{
    return asuint(value);
}

float2 PathTracingTransportMakeFloat2(float x, float y)
{
    return float2(x, y);
}

float3 PathTracingTransportMakeFloat3(float x, float y, float z)
{
    return float3(x, y, z);
}

uint2 PathTracingTransportMakeUint2(uint x, uint y)
{
    return uint2(x, y);
}

float3 PathTracingTransportAdd3(float3 left, float3 right)
{
    return left + right;
}

float3 PathTracingTransportMultiply3(float3 left, float3 right)
{
    return left * right;
}

float3 PathTracingTransportScale3(float3 value, float scale)
{
    return value * scale;
}

float3 PathTracingTransportMaxZero3(float3 value)
{
    return max(value, 0.0f);
}

bool PathTracingTransportIsFinite3(float3 value)
{
    return all(isfinite(value));
}

bool PathTracingTransportIsNonnegative3(float3 value)
{
    return all(value >= 0.0f);
}

#define UVSR_PATH_TRANSPORT_INLINE
#define UVSR_PATH_TRANSPORT_INOUT(type) inout type

#endif

struct PathTracingRandomStream
{
    PathTracingTransportUint2 seed;
    PathTracingTransportUint dimension;
};

struct PathTracingCameraRandomDraws
{
    float jitterX;
    float jitterY;
};

struct PathTracingDirectLightRandomDraws
{
    float selection;
    PathTracingTransportUint sampleSeed;
};

struct PathTracingBsdfRandomDraws
{
    float branch;
    float sampleX;
    float sampleY;
};

struct PathTracingRetryGenerationTransition
{
    PathTracingTransportUint generation;
    PathTracingTransportUint changed;
};

struct PathTracingPreparedMaterialContract
{
    PathTracingTransportFloat3 diffuseColor;
    PathTracingTransportFloat3 specularF0;
    float alpha;
};

struct PathTracingBsdfContractEvaluation
{
    PathTracingTransportFloat3 diffuse;
    PathTracingTransportFloat3 specular;
    PathTracingTransportFloat3 total;
    float diffusePdf;
    float specularPdf;
};

struct PathTracingBsdfWeightContract
{
    PathTracingTransportFloat3 weight;
    float pdf;
    PathTracingTransportUint valid;
};

struct PathTracingRouletteContract
{
    PathTracingTransportFloat3 throughput;
    float survival;
    PathTracingTransportUint transportValid;
    PathTracingTransportUint continuePath;
};

UVSR_PATH_TRANSPORT_INLINE PathTracingTransportUint PathTracingHash(
    PathTracingTransportUint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

UVSR_PATH_TRANSPORT_INLINE float PathTracingUintToUnitFloat(
    PathTracingTransportUint bits)
{
    return (float(bits >> 8u) + 0.5f) * (1.0f / 16777216.0f);
}

UVSR_PATH_TRANSPORT_INLINE PathTracingRandomStream
    PathTracingCreateRandomStream(
        PathTracingTransportUint2 seed,
        PathTracingTransportUint domain)
{
    PathTracingRandomStream stream;
    stream.seed = PathTracingTransportMakeUint2(
        PathTracingHash(seed.x ^ PathTracingHash(seed.y + domain)),
        PathTracingHash(seed.y ^ PathTracingHash(
            seed.x + domain + 0x9e3779b9u)));
    stream.dimension = 0u;
    return stream;
}

UVSR_PATH_TRANSPORT_INLINE PathTracingTransportUint PathTracingRandomUint(
    UVSR_PATH_TRANSPORT_INOUT(PathTracingRandomStream) stream)
{
    const PathTracingTransportUint counter = stream.dimension++;
    return PathTracingHash(
        stream.seed.x ^ PathTracingHash(
            stream.seed.y + counter * 0x9e3779b9u));
}

UVSR_PATH_TRANSPORT_INLINE float PathTracingRandom(
    UVSR_PATH_TRANSPORT_INOUT(PathTracingRandomStream) stream)
{
    const PathTracingTransportUint bits = PathTracingRandomUint(stream);
    return PathTracingUintToUnitFloat(bits);
}

UVSR_PATH_TRANSPORT_INLINE PathTracingCameraRandomDraws
    PathTracingDrawCameraRandoms(
        UVSR_PATH_TRANSPORT_INOUT(PathTracingRandomStream) stream)
{
    PathTracingCameraRandomDraws result;
    result.jitterX = PathTracingRandom(stream);
    result.jitterY = PathTracingRandom(stream);
    return result;
}

UVSR_PATH_TRANSPORT_INLINE PathTracingDirectLightRandomDraws
    PathTracingDrawDirectLightRandoms(
        UVSR_PATH_TRANSPORT_INOUT(PathTracingRandomStream) stream)
{
    PathTracingDirectLightRandomDraws result;
    result.selection = PathTracingRandom(stream);
    result.sampleSeed = PathTracingRandomUint(stream);
    return result;
}

UVSR_PATH_TRANSPORT_INLINE PathTracingBsdfRandomDraws
    PathTracingDrawBsdfRandoms(
        UVSR_PATH_TRANSPORT_INOUT(PathTracingRandomStream) stream)
{
    PathTracingBsdfRandomDraws result;
    result.branch = PathTracingRandom(stream);
    result.sampleX = PathTracingRandom(stream);
    result.sampleY = PathTracingRandom(stream);
    return result;
}

UVSR_PATH_TRANSPORT_INLINE float PathTracingDrawRouletteRandom(
    UVSR_PATH_TRANSPORT_INOUT(PathTracingRandomStream) stream)
{
    const float roulette = PathTracingRandom(stream);
    return roulette;
}

UVSR_PATH_TRANSPORT_INLINE PathTracingTransportFloat2
    PathTracingFiniteLightRandom(PathTracingTransportUint sampleSeed)
{
    return PathTracingTransportMakeFloat2(
        PathTracingUintToUnitFloat(PathTracingHash(
            sampleSeed ^ 0xa511e9b3u)),
        PathTracingUintToUnitFloat(PathTracingHash(
            sampleSeed ^ 0x63d83595u)));
}

UVSR_PATH_TRANSPORT_INLINE PathTracingTransportUint
    PathTracingNoiseToUint(float noise)
{
    const float safeNoise = PbrContractSaturate(noise);
    return safeNoise >= 1.0f
        ? 0xffffffffu
        : PathTracingTransportUint(safeNoise * 4294967295.0f);
}

UVSR_PATH_TRANSPORT_INLINE PathTracingTransportUint2
    PathTracingMakeSampleSeed(
        PathTracingTransportUint2 pixel,
        PathTracingTransportUint samplePhase,
        PathTracingTransportUint successfulSampleCount,
        PathTracingTransportUint retryGeneration,
        float precomputedNoise)
{
    PathTracingTransportUint low = PathTracingHash(
        pixel.x ^ (pixel.y * 0x632be5abu));
    low = PathTracingHash(low ^ samplePhase);
    low = PathTracingHash(
        low ^ successfulSampleCount * 0x85157af5u);
    low ^= PathTracingHash(retryGeneration * 0x27d4eb2du);
    low ^= PathTracingNoiseToUint(precomputedNoise);

    PathTracingTransportUint high = PathTracingHash(
        pixel.y ^ (pixel.x * 0x68bc21ebu));
    high = PathTracingHash(high ^ PathTracingHash(
        samplePhase + 0x9e3779b9u));
    high = PathTracingHash(
        high ^ successfulSampleCount * 0x02e5be93u);
    high ^= PathTracingHash(retryGeneration * 0x165667b1u);
    high ^= PathTracingHash(PathTracingTransportAsUint(precomputedNoise));
    return PathTracingTransportMakeUint2(
        PathTracingHash(low),
        PathTracingHash(high));
}

UVSR_PATH_TRANSPORT_INLINE PathTracingTransportUint
    PathTracingMakeAttemptPhase(
        PathTracingTransportUint successfulSampleCount,
        PathTracingTransportUint retryGeneration)
{
    // The odd multiplier is invertible modulo 2^32, so every nonzero retry
    // changes the phase for a fixed accepted-sample count. Retry zero retains
    // the established accepted-history sequence exactly.
    return successfulSampleCount + retryGeneration * 0x9e3779b9u;
}

UVSR_PATH_TRANSPORT_INLINE PathTracingTransportUint
    PathTracingAdvanceRetryGeneration(
        PathTracingTransportUint previousGeneration)
{
    const PathTracingTransportUint next = previousGeneration + 1u;
    return next != UVSR_PATH_TRACING_RETRY_GENERATION_CLEARED
        ? next
        : UVSR_PATH_TRACING_RETRY_GENERATION_FIRST;
}

UVSR_PATH_TRANSPORT_INLINE PathTracingRetryGenerationTransition
    ResolvePathTracingRetryGeneration(
        PathTracingTransportUint previousGeneration,
        PathTracingTransportUint sampleAccepted)
{
    PathTracingRetryGenerationTransition result;
    result.generation = sampleAccepted != 0u
        ? UVSR_PATH_TRACING_RETRY_GENERATION_CLEARED
        : PathTracingAdvanceRetryGeneration(previousGeneration);
    result.changed = result.generation != previousGeneration ? 1u : 0u;
    return result;
}

UVSR_PATH_TRANSPORT_INLINE float PathTracingLuminance(
    PathTracingTransportFloat3 value)
{
    const PathTracingTransportFloat3 positive =
        PathTracingTransportMaxZero3(value);
    return positive.x * 0.2126f + positive.y * 0.7152f +
        positive.z * 0.0722f;
}

UVSR_PATH_TRANSPORT_INLINE PathTracingTransportFloat3
    PathTracingSampleCosineHemisphereLocal(
        PathTracingTransportFloat2 random)
{
    const float radius = PbrContractSqrt(PbrContractSaturate(random.x));
    const float phi = 6.28318530717958647692f * random.y;
    return PathTracingTransportMakeFloat3(
        radius * PbrContractCos(phi),
        radius * PbrContractSin(phi),
        PbrContractSqrt(PbrContractSaturate(1.0f - random.x)));
}

UVSR_PATH_TRANSPORT_INLINE PathTracingTransportFloat3
    PathTracingSampleGgxHalfVectorLocal(
        PathTracingTransportFloat2 random,
        float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float cosineSquared = (1.0f - random.x) /
        PbrContractMax(
            1.0f + (alphaSquared - 1.0f) * random.x,
            1.0e-6f);
    const float cosine = PbrContractSqrt(
        PbrContractSaturate(cosineSquared));
    const float sine = PbrContractSqrt(
        PbrContractSaturate(1.0f - cosineSquared));
    const float phi = 6.28318530717958647692f * random.y;
    return PathTracingTransportMakeFloat3(
        sine * PbrContractCos(phi),
        sine * PbrContractSin(phi),
        cosine);
}

UVSR_PATH_TRANSPORT_INLINE PathTracingPreparedMaterialContract
    ResolvePathTracingPreparedMaterial(
        PathTracingTransportFloat3 baseColor,
        float metalness,
        float perceptualRoughness,
        float authoredSpecularF0,
        bool usesSpecularGlossModel)
{
    PathTracingPreparedMaterialContract result;
    baseColor = PathTracingTransportMaxZero3(baseColor);
    metalness = PbrContractSaturate(metalness);
    perceptualRoughness = PbrContractSaturate(perceptualRoughness);
    const float dielectricF0 = !usesSpecularGlossModel &&
        authoredSpecularF0 > 0.0f
        ? PbrContractSaturate(authoredSpecularF0)
        : 0.04f;
    result.diffuseColor = PathTracingTransportScale3(
        baseColor,
        1.0f - metalness);
    result.specularF0 = PathTracingTransportAdd3(
        PathTracingTransportScale3(
            PathTracingTransportMakeFloat3(
                dielectricF0,
                dielectricF0,
                dielectricF0),
            1.0f - metalness),
        PathTracingTransportScale3(baseColor, metalness));
    result.alpha = PbrContractMax(
        perceptualRoughness * perceptualRoughness,
        0.002f);
    return result;
}

UVSR_PATH_TRANSPORT_INLINE float PathTracingD_GGXExact(
    float NoH,
    float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float denominator =
        NoH * NoH * (alphaSquared - 1.0f) + 1.0f;
    const float normalization =
        3.14159265358979323846f * denominator * denominator;
    return normalization > 0.0f && PbrContractIsFinite(normalization)
        ? alphaSquared / normalization
        : 0.0f;
}

UVSR_PATH_TRANSPORT_INLINE float PathTracingV_SmithGGXCorrelatedExact(
    float NoV,
    float NoL,
    float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float lambdaV = NoL * PbrContractSqrt(PbrContractMax(
        NoV * NoV * (1.0f - alphaSquared) + alphaSquared,
        0.0f));
    const float lambdaL = NoV * PbrContractSqrt(PbrContractMax(
        NoL * NoL * (1.0f - alphaSquared) + alphaSquared,
        0.0f));
    const float denominator = lambdaV + lambdaL;
    return denominator > 0.0f && PbrContractIsFinite(denominator)
        ? 0.5f / denominator
        : 0.0f;
}

UVSR_PATH_TRANSPORT_INLINE float PathTracingPdfLambert(float NoL)
{
    return PbrContractSaturate(NoL) * 0.31830988618379067154f;
}

UVSR_PATH_TRANSPORT_INLINE float PathTracingPdfGGXExact(
    float NoH,
    float VoH,
    float alpha)
{
    const float denominator = 4.0f * PbrContractMax(VoH, -VoH);
    return denominator > 0.0f && PbrContractIsFinite(denominator)
        ? PathTracingD_GGXExact(NoH, alpha) *
            PbrContractSaturate(NoH) / denominator
        : 0.0f;
}

UVSR_PATH_TRANSPORT_INLINE PathTracingTransportFloat3
    PathTracingFresnelSchlick(
        float cosine,
        PathTracingTransportFloat3 f0)
{
    const float oneMinusCosine = 1.0f - PbrContractSaturate(cosine);
    float factor = oneMinusCosine * oneMinusCosine;
    factor *= factor * oneMinusCosine;
    return PathTracingTransportMakeFloat3(
        f0.x + (1.0f - f0.x) * factor,
        f0.y + (1.0f - f0.y) * factor,
        f0.z + (1.0f - f0.z) * factor);
}

UVSR_PATH_TRANSPORT_INLINE PathTracingBsdfContractEvaluation
    ResolvePathTracingBsdfEvaluation(
        PathTracingPreparedMaterialContract material,
        float geometricNoV,
        float geometricNoL,
        float shadingNoV,
        float shadingNoL,
        float NoH,
        float VoH)
{
    PathTracingBsdfContractEvaluation result;
    result.diffuse = PathTracingTransportMakeFloat3(0.0f, 0.0f, 0.0f);
    result.specular = result.diffuse;
    result.total = result.diffuse;
    result.diffusePdf = 0.0f;
    result.specularPdf = 0.0f;
    if (geometricNoV <= 1.0e-5f || geometricNoL <= 1.0e-5f ||
        shadingNoV <= 1.0e-5f || shadingNoL <= 1.0e-5f)
    {
        return result;
    }

    const PathTracingTransportFloat3 fresnel =
        PathTracingFresnelSchlick(VoH, material.specularF0);
    const float distribution = PathTracingD_GGXExact(NoH, material.alpha);
    const float visibility = PathTracingV_SmithGGXCorrelatedExact(
        shadingNoV,
        shadingNoL,
        material.alpha);
    result.diffuse = PathTracingTransportMakeFloat3(
        material.diffuseColor.x * (1.0f - fresnel.x) *
            0.31830988618379067154f,
        material.diffuseColor.y * (1.0f - fresnel.y) *
            0.31830988618379067154f,
        material.diffuseColor.z * (1.0f - fresnel.z) *
            0.31830988618379067154f);
    result.specular = PathTracingTransportScale3(
        fresnel,
        distribution * visibility);
    result.total = PathTracingTransportAdd3(
        result.diffuse,
        result.specular);
    result.diffusePdf = PathTracingPdfLambert(shadingNoL);
    result.specularPdf = PathTracingPdfGGXExact(
        NoH,
        VoH,
        material.alpha);
    return result;
}

UVSR_PATH_TRANSPORT_INLINE float
    ResolvePathTracingDiffuseSelectionProbability(
        PathTracingPreparedMaterialContract material)
{
    const float diffuseWeight = PathTracingLuminance(material.diffuseColor);
    const float specularWeight = PathTracingLuminance(material.specularF0);
    const float total = diffuseWeight + specularWeight;
    if (!(total > 0.0f) || !PbrContractIsFinite(total))
        return 1.0f;
    if (!(diffuseWeight > 0.0f))
        return 0.0f;
    if (!(specularWeight > 0.0f))
        return 1.0f;
    return PbrContractMin(
        PbrContractMax(diffuseWeight / total, 0.05f),
        0.95f);
}

UVSR_PATH_TRANSPORT_INLINE PathTracingBsdfWeightContract
    ResolvePathTracingBsdfWeight(
        PathTracingBsdfContractEvaluation evaluation,
        float diffuseProbability,
        float shadingCosine,
        float geometricCosine)
{
    PathTracingBsdfWeightContract result;
    result.weight = PathTracingTransportMakeFloat3(0.0f, 0.0f, 0.0f);
    result.pdf = diffuseProbability * evaluation.diffusePdf +
        (1.0f - diffuseProbability) * evaluation.specularPdf;
    result.valid = 0u;
    const float cosine = PbrContractSaturate(shadingCosine);
    if (result.pdf > 0.0f && PbrContractIsFinite(result.pdf) &&
        cosine > 0.0f && geometricCosine > 0.0f)
    {
        result.weight = PathTracingTransportScale3(
            evaluation.total,
            cosine / result.pdf);
        result.valid = PathTracingTransportIsFinite3(result.weight) ? 1u : 0u;
    }
    return result;
}

UVSR_PATH_TRANSPORT_INLINE bool PathTracingBounceSamplesBsdf(
    PathTracingTransportUint nextBounce)
{
    return nextBounce < UVSR_PATH_TRACING_BOUNCE_COUNT;
}

UVSR_PATH_TRANSPORT_INLINE bool PathTracingRouletteRequiresRandom(
    PathTracingTransportUint nextBounce)
{
    return nextBounce >= UVSR_PATH_TRACING_RUSSIAN_ROULETTE_START;
}

UVSR_PATH_TRANSPORT_INLINE bool PathTracingThroughputIsValid(
    PathTracingTransportFloat3 throughput)
{
    return PathTracingTransportIsFinite3(throughput) &&
        PathTracingTransportIsNonnegative3(throughput);
}

UVSR_PATH_TRANSPORT_INLINE PathTracingRouletteContract
    ResolvePathTracingRoulette(
        PathTracingTransportUint nextBounce,
        PathTracingTransportFloat3 throughput,
        float random)
{
    PathTracingRouletteContract result;
    result.throughput = throughput;
    result.survival = 1.0f;
    result.transportValid = 0u;
    result.continuePath = 0u;
    if (!PathTracingThroughputIsValid(throughput))
    {
        return result;
    }

    result.transportValid = 1u;
    if (nextBounce < UVSR_PATH_TRACING_RUSSIAN_ROULETTE_START)
    {
        result.continuePath = 1u;
        return result;
    }
    result.survival = PbrContractMin(PbrContractMax(
        PbrContractMax(
            throughput.x,
            PbrContractMax(throughput.y, throughput.z)),
        0.05f), 0.95f);
    if (!PbrContractIsFinite(random) || random >= result.survival)
        return result;
    result.throughput = PathTracingTransportScale3(
        throughput,
        1.0f / result.survival);
    result.continuePath = 1u;
    return result;
}

#ifndef __cplusplus
#undef PathTracingTransportUint
#undef PathTracingTransportUint2
#undef PathTracingTransportFloat2
#undef PathTracingTransportFloat3
#endif
#undef UVSR_PATH_TRANSPORT_INOUT
#undef UVSR_PATH_TRANSPORT_INLINE

#endif // UVSR_PATH_TRACING_TRANSPORT_CONTRACT_H
