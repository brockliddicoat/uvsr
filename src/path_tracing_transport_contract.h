#ifndef UVSR_PATH_TRACING_TRANSPORT_CONTRACT_H
#define UVSR_PATH_TRACING_TRANSPORT_CONTRACT_H

#include "pbr_surface_light_contract.h"

#define UVSR_PATH_TRACING_SAMPLES_PER_FRAME 1u
#define UVSR_PATH_TRACING_RETRY_GENERATION_CLEARED 0u
#define UVSR_PATH_TRACING_RETRY_GENERATION_FIRST 1u

#include "shader_math.h"

struct PathTracingRandomStream
{
    ShaderUint2 seed;
    ShaderUint dimension;
};

struct PathTracingCameraRandomDraws
{
    float jitterX;
    float jitterY;
};

struct PathTracingDirectLightRandomDraws
{
    float selection;
    ShaderUint sampleSeed;
};

struct PathTracingBsdfRandomDraws
{
    float branch;
    float sampleX;
    float sampleY;
};

struct PathTracingRetryGenerationTransition
{
    ShaderUint generation;
    ShaderUint changed;
};

struct PathTracingPreparedMaterialContract
{
    ShaderFloat3 diffuseColor;
    ShaderFloat3 specularF0;
    float alpha;
};

struct PathTracingBsdfContractEvaluation
{
    ShaderFloat3 diffuse;
    ShaderFloat3 specular;
    ShaderFloat3 total;
    float diffusePdf;
    float specularPdf;
};

struct PathTracingBsdfWeightContract
{
    ShaderFloat3 weight;
    float pdf;
    ShaderUint valid;
};

struct PathTracingRouletteContract
{
    ShaderFloat3 throughput;
    float survival;
    ShaderUint transportValid;
    ShaderUint continuePath;
};

UVSR_SHADER_INLINE ShaderUint PathTracingHash(
    ShaderUint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

UVSR_SHADER_INLINE float PathTracingUintToUnitFloat(
    ShaderUint bits)
{
    return (float(bits >> 8u) + 0.5f) * (1.0f / 16777216.0f);
}

UVSR_SHADER_INLINE PathTracingRandomStream
    PathTracingCreateRandomStream(
        ShaderUint2 seed,
        ShaderUint domain)
{
    PathTracingRandomStream stream;
    stream.seed = ShaderMakeUint2(
        PathTracingHash(seed.x ^ PathTracingHash(seed.y + domain)),
        PathTracingHash(seed.y ^ PathTracingHash(
            seed.x + domain + 0x9e3779b9u)));
    stream.dimension = 0u;
    return stream;
}

UVSR_SHADER_INLINE ShaderUint PathTracingRandomUint(
    UVSR_SHADER_INOUT(PathTracingRandomStream) stream)
{
    const ShaderUint counter = stream.dimension++;
    return PathTracingHash(
        stream.seed.x ^ PathTracingHash(
            stream.seed.y + counter * 0x9e3779b9u));
}

UVSR_SHADER_INLINE float PathTracingRandom(
    UVSR_SHADER_INOUT(PathTracingRandomStream) stream)
{
    const ShaderUint bits = PathTracingRandomUint(stream);
    return PathTracingUintToUnitFloat(bits);
}

UVSR_SHADER_INLINE PathTracingCameraRandomDraws
    PathTracingDrawCameraRandoms(
        UVSR_SHADER_INOUT(PathTracingRandomStream) stream)
{
    PathTracingCameraRandomDraws result;
    result.jitterX = PathTracingRandom(stream);
    result.jitterY = PathTracingRandom(stream);
    return result;
}

UVSR_SHADER_INLINE PathTracingDirectLightRandomDraws
    PathTracingDrawDirectLightRandoms(
        UVSR_SHADER_INOUT(PathTracingRandomStream) stream)
{
    PathTracingDirectLightRandomDraws result;
    result.selection = PathTracingRandom(stream);
    result.sampleSeed = PathTracingRandomUint(stream);
    return result;
}

UVSR_SHADER_INLINE PathTracingBsdfRandomDraws
    PathTracingDrawBsdfRandoms(
        UVSR_SHADER_INOUT(PathTracingRandomStream) stream)
{
    PathTracingBsdfRandomDraws result;
    result.branch = PathTracingRandom(stream);
    result.sampleX = PathTracingRandom(stream);
    result.sampleY = PathTracingRandom(stream);
    return result;
}

UVSR_SHADER_INLINE float PathTracingDrawRouletteRandom(
    UVSR_SHADER_INOUT(PathTracingRandomStream) stream)
{
    const float roulette = PathTracingRandom(stream);
    return roulette;
}

UVSR_SHADER_INLINE ShaderFloat2
    PathTracingFiniteLightRandom(ShaderUint sampleSeed)
{
    return ShaderMakeFloat2(
        PathTracingUintToUnitFloat(PathTracingHash(
            sampleSeed ^ 0xa511e9b3u)),
        PathTracingUintToUnitFloat(PathTracingHash(
            sampleSeed ^ 0x63d83595u)));
}

UVSR_SHADER_INLINE ShaderUint
    PathTracingNoiseToUint(float noise)
{
    const float safeNoise = ShaderSaturate(noise);
    return safeNoise >= 1.0f
        ? 0xffffffffu
        : ShaderUint(safeNoise * 4294967295.0f);
}

UVSR_SHADER_INLINE ShaderUint2
    PathTracingMakeSampleSeed(
        ShaderUint2 pixel,
        ShaderUint samplePhase,
        ShaderUint successfulSampleCount,
        ShaderUint retryGeneration,
        float precomputedNoise)
{
    ShaderUint low = PathTracingHash(
        pixel.x ^ (pixel.y * 0x632be5abu));
    low = PathTracingHash(low ^ samplePhase);
    low = PathTracingHash(
        low ^ successfulSampleCount * 0x85157af5u);
    low ^= PathTracingHash(retryGeneration * 0x27d4eb2du);
    low ^= PathTracingNoiseToUint(precomputedNoise);

    ShaderUint high = PathTracingHash(
        pixel.y ^ (pixel.x * 0x68bc21ebu));
    high = PathTracingHash(high ^ PathTracingHash(
        samplePhase + 0x9e3779b9u));
    high = PathTracingHash(
        high ^ successfulSampleCount * 0x02e5be93u);
    high ^= PathTracingHash(retryGeneration * 0x165667b1u);
    high ^= PathTracingHash(ShaderAsUint(precomputedNoise));
    return ShaderMakeUint2(
        PathTracingHash(low),
        PathTracingHash(high));
}

UVSR_SHADER_INLINE ShaderUint
    PathTracingMakeAttemptPhase(
        ShaderUint successfulSampleCount,
        ShaderUint retryGeneration)
{
    // The odd multiplier is invertible modulo 2^32, so every nonzero retry
    // changes the phase for a fixed accepted-sample count. Retry zero retains
    // the established accepted-history sequence exactly.
    return successfulSampleCount + retryGeneration * 0x9e3779b9u;
}

UVSR_SHADER_INLINE ShaderUint
    PathTracingAdvanceRetryGeneration(
        ShaderUint previousGeneration)
{
    const ShaderUint next = previousGeneration + 1u;
    return next != UVSR_PATH_TRACING_RETRY_GENERATION_CLEARED
        ? next
        : UVSR_PATH_TRACING_RETRY_GENERATION_FIRST;
}

UVSR_SHADER_INLINE PathTracingRetryGenerationTransition
    ResolvePathTracingRetryGeneration(
        ShaderUint previousGeneration,
        ShaderUint sampleAccepted)
{
    PathTracingRetryGenerationTransition result;
    result.generation = sampleAccepted != 0u
        ? UVSR_PATH_TRACING_RETRY_GENERATION_CLEARED
        : PathTracingAdvanceRetryGeneration(previousGeneration);
    result.changed = result.generation != previousGeneration ? 1u : 0u;
    return result;
}

UVSR_SHADER_INLINE float PathTracingLuminance(
    ShaderFloat3 value)
{
    const ShaderFloat3 positive =
        ShaderMaxZero3(value);
    return positive.x * 0.2126f + positive.y * 0.7152f +
        positive.z * 0.0722f;
}

UVSR_SHADER_INLINE ShaderFloat3
    PathTracingSampleCosineHemisphereLocal(
        ShaderFloat2 random)
{
    const float radius = ShaderSqrt(ShaderSaturate(random.x));
    const float phi = 6.28318530717958647692f * random.y;
    return ShaderMakeFloat3(
        radius * ShaderCos(phi),
        radius * ShaderSin(phi),
        ShaderSqrt(ShaderSaturate(1.0f - random.x)));
}

UVSR_SHADER_INLINE ShaderFloat3
    PathTracingSampleGgxHalfVectorLocal(
        ShaderFloat2 random,
        float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float cosineSquared = (1.0f - random.x) /
        ShaderMax(
            1.0f + (alphaSquared - 1.0f) * random.x,
            1.0e-6f);
    const float cosine = ShaderSqrt(
        ShaderSaturate(cosineSquared));
    const float sine = ShaderSqrt(
        ShaderSaturate(1.0f - cosineSquared));
    const float phi = 6.28318530717958647692f * random.y;
    return ShaderMakeFloat3(
        sine * ShaderCos(phi),
        sine * ShaderSin(phi),
        cosine);
}

UVSR_SHADER_INLINE PathTracingPreparedMaterialContract
    ResolvePathTracingPreparedMaterial(
        ShaderFloat3 baseColor,
        float metalness,
        float perceptualRoughness,
        float authoredSpecularF0,
        bool usesSpecularGlossModel)
{
    PathTracingPreparedMaterialContract result;
    baseColor = ShaderMaxZero3(baseColor);
    metalness = ShaderSaturate(metalness);
    perceptualRoughness = ShaderSaturate(perceptualRoughness);
    const float dielectricF0 = !usesSpecularGlossModel &&
        authoredSpecularF0 > 0.0f
        ? ShaderSaturate(authoredSpecularF0)
        : 0.04f;
    result.diffuseColor = ShaderScale3(
        baseColor,
        1.0f - metalness);
    result.specularF0 = ShaderAdd3(
        ShaderScale3(
            ShaderMakeFloat3(
                dielectricF0,
                dielectricF0,
                dielectricF0),
            1.0f - metalness),
        ShaderScale3(baseColor, metalness));
    result.alpha = ShaderMax(
        perceptualRoughness * perceptualRoughness,
        0.002f);
    return result;
}

UVSR_SHADER_INLINE float PathTracingD_GGXExact(
    float NoH,
    float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float denominator =
        NoH * NoH * (alphaSquared - 1.0f) + 1.0f;
    const float normalization =
        3.14159265358979323846f * denominator * denominator;
    return normalization > 0.0f && ShaderIsFinite(normalization)
        ? alphaSquared / normalization
        : 0.0f;
}

UVSR_SHADER_INLINE float PathTracingV_SmithGGXCorrelatedExact(
    float NoV,
    float NoL,
    float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float lambdaV = NoL * ShaderSqrt(ShaderMax(
        NoV * NoV * (1.0f - alphaSquared) + alphaSquared,
        0.0f));
    const float lambdaL = NoV * ShaderSqrt(ShaderMax(
        NoL * NoL * (1.0f - alphaSquared) + alphaSquared,
        0.0f));
    const float denominator = lambdaV + lambdaL;
    return denominator > 0.0f && ShaderIsFinite(denominator)
        ? 0.5f / denominator
        : 0.0f;
}

UVSR_SHADER_INLINE float PathTracingPdfLambert(float NoL)
{
    return ShaderSaturate(NoL) * 0.31830988618379067154f;
}

UVSR_SHADER_INLINE float PathTracingPdfGGXExact(
    float NoH,
    float VoH,
    float alpha)
{
    const float denominator = 4.0f * ShaderMax(VoH, -VoH);
    return denominator > 0.0f && ShaderIsFinite(denominator)
        ? PathTracingD_GGXExact(NoH, alpha) *
            ShaderSaturate(NoH) / denominator
        : 0.0f;
}

UVSR_SHADER_INLINE ShaderFloat3
    PathTracingFresnelSchlick(
        float cosine,
        ShaderFloat3 f0)
{
    const float oneMinusCosine = 1.0f - ShaderSaturate(cosine);
    float factor = oneMinusCosine * oneMinusCosine;
    factor *= factor * oneMinusCosine;
    return ShaderMakeFloat3(
        f0.x + (1.0f - f0.x) * factor,
        f0.y + (1.0f - f0.y) * factor,
        f0.z + (1.0f - f0.z) * factor);
}

UVSR_SHADER_INLINE PathTracingBsdfContractEvaluation
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
    result.diffuse = ShaderMakeFloat3(0.0f, 0.0f, 0.0f);
    result.specular = result.diffuse;
    result.total = result.diffuse;
    result.diffusePdf = 0.0f;
    result.specularPdf = 0.0f;
    if (geometricNoV <= 1.0e-5f || geometricNoL <= 1.0e-5f ||
        shadingNoV <= 1.0e-5f || shadingNoL <= 1.0e-5f)
    {
        return result;
    }

    const ShaderFloat3 fresnel =
        PathTracingFresnelSchlick(VoH, material.specularF0);
    const float distribution = PathTracingD_GGXExact(NoH, material.alpha);
    const float visibility = PathTracingV_SmithGGXCorrelatedExact(
        shadingNoV,
        shadingNoL,
        material.alpha);
    result.diffuse = ShaderMakeFloat3(
        material.diffuseColor.x * (1.0f - fresnel.x) *
            0.31830988618379067154f,
        material.diffuseColor.y * (1.0f - fresnel.y) *
            0.31830988618379067154f,
        material.diffuseColor.z * (1.0f - fresnel.z) *
            0.31830988618379067154f);
    result.specular = ShaderScale3(
        fresnel,
        distribution * visibility);
    result.total = ShaderAdd3(
        result.diffuse,
        result.specular);
    result.diffusePdf = PathTracingPdfLambert(shadingNoL);
    result.specularPdf = PathTracingPdfGGXExact(
        NoH,
        VoH,
        material.alpha);
    return result;
}

UVSR_SHADER_INLINE float
    ResolvePathTracingDiffuseSelectionProbability(
        PathTracingPreparedMaterialContract material)
{
    const float diffuseWeight = PathTracingLuminance(material.diffuseColor);
    const float specularWeight = PathTracingLuminance(material.specularF0);
    const float total = diffuseWeight + specularWeight;
    if (!(total > 0.0f) || !ShaderIsFinite(total))
        return 1.0f;
    if (!(diffuseWeight > 0.0f))
        return 0.0f;
    if (!(specularWeight > 0.0f))
        return 1.0f;
    return ShaderMin(
        ShaderMax(diffuseWeight / total, 0.05f),
        0.95f);
}

UVSR_SHADER_INLINE PathTracingBsdfWeightContract
    ResolvePathTracingBsdfWeight(
        PathTracingBsdfContractEvaluation evaluation,
        float diffuseProbability,
        float shadingCosine,
        float geometricCosine)
{
    PathTracingBsdfWeightContract result;
    result.weight = ShaderMakeFloat3(0.0f, 0.0f, 0.0f);
    result.pdf = diffuseProbability * evaluation.diffusePdf +
        (1.0f - diffuseProbability) * evaluation.specularPdf;
    result.valid = 0u;
    const float cosine = ShaderSaturate(shadingCosine);
    if (result.pdf > 0.0f && ShaderIsFinite(result.pdf) &&
        cosine > 0.0f && geometricCosine > 0.0f)
    {
        result.weight = ShaderScale3(
            evaluation.total,
            cosine / result.pdf);
        result.valid = ShaderIsFinite3(result.weight) ? 1u : 0u;
    }
    return result;
}

UVSR_SHADER_INLINE bool PathTracingBounceSamplesBsdf(
    ShaderUint nextBounce, ShaderUint maximumBounces)
{
    return nextBounce <= maximumBounces;
}

UVSR_SHADER_INLINE bool PathTracingRouletteRequiresRandom(
    ShaderUint nextBounce, ShaderUint minimumBounces)
{
    // Capsaicin counts the camera hit as zero and tests currentBounce > min.
    return nextBounce > minimumBounces + 1u;
}

UVSR_SHADER_INLINE bool PathTracingThroughputIsValid(
    ShaderFloat3 throughput)
{
    return ShaderIsFinite3(throughput) &&
        ShaderIsNonnegative3(throughput);
}

UVSR_SHADER_INLINE PathTracingRouletteContract
    ResolvePathTracingRoulette(
        ShaderUint nextBounce,
        ShaderUint minimumBounces,
        ShaderFloat3 throughput,
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
    if (!PathTracingRouletteRequiresRandom(nextBounce, minimumBounces))
    {
        result.continuePath = 1u;
        return result;
    }
    result.survival = ShaderMin(ShaderMax(
        ShaderMax(
            throughput.x,
            ShaderMax(throughput.y, throughput.z)),
        0.05f), 0.95f);
    if (!ShaderIsFinite(random) || random >= result.survival)
        return result;
    result.throughput = ShaderScale3(
        throughput,
        1.0f / result.survival);
    result.continuePath = 1u;
    return result;
}


#endif // UVSR_PATH_TRACING_TRANSPORT_CONTRACT_H
