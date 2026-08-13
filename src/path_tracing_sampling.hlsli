#ifndef UVSR_PATH_TRACING_SAMPLING_HLSLI
#define UVSR_PATH_TRACING_SAMPLING_HLSLI

#include "pbr_lighting.hlsli"

static const float UVSR_PATH_PI = 3.14159265358979323846f;
static const float UVSR_PATH_TWO_PI = 6.28318530717958647692f;
static const float UVSR_PATH_TARGET_EPSILON = 1.0e-6f;
static const float UVSR_PATH_MAX_REUSED_RESERVOIR_CANDIDATES = 32.0f;

uint PathTracingHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

struct PathTracingRandomStream
{
    uint2 seed;
    uint dimension;
};

PathTracingRandomStream PathTracingCreateRandomStream(
    uint2 seed,
    uint domain)
{
    PathTracingRandomStream stream;
    stream.seed = uint2(
        PathTracingHash(seed.x ^ PathTracingHash(seed.y + domain)),
        PathTracingHash(seed.y ^ PathTracingHash(seed.x + domain +
            0x9e3779b9u)));
    stream.dimension = 0u;
    return stream;
}

uint PathTracingRandomUint(inout PathTracingRandomStream stream)
{
    // Counter-based sampling makes every dimension a pure function of the
    // persisted 64-bit seed. Replaying a seed never depends on mutable frame,
    // pixel-history, noise-texture, or prior-call state.
    const uint counter = stream.dimension++;
    return PathTracingHash(
        stream.seed.x ^ PathTracingHash(
            stream.seed.y + counter * 0x9e3779b9u));
}

float PathTracingUintToUnitFloat(uint bits)
{
    return (float(bits >> 8u) + 0.5f) * (1.0f / 16777216.0f);
}

float PathTracingRandom(inout PathTracingRandomStream stream)
{
    return PathTracingUintToUnitFloat(PathTracingRandomUint(stream));
}

float2 PathTracingFiniteLightRandom(uint sampleSeed)
{
    // The complete 32-bit seed is persisted by direct reservoirs. Hashing it
    // into two dimensions avoids the irreversible 24-bit truncation caused by
    // round-tripping a random float through asuint.
    return float2(
        PathTracingUintToUnitFloat(PathTracingHash(
            sampleSeed ^ 0xa511e9b3u)),
        PathTracingUintToUnitFloat(PathTracingHash(
            sampleSeed ^ 0x63d83595u)));
}

uint2 PathTracingMakeSampleSeed(
    uint2 pixel,
    uint samplePhase,
    uint successfulSampleCount,
    uint serialLow,
    uint serialHigh,
    uint failedAttemptSalt,
    float precomputedNoise)
{
    uint low = PathTracingHash(pixel.x ^ (pixel.y * 0x632be5abu));
    low = PathTracingHash(low ^ samplePhase);
    low = PathTracingHash(low ^ successfulSampleCount * 0x85157af5u);
    low = PathTracingHash(low ^ serialLow);
    low = PathTracingHash(low ^ failedAttemptSalt * 0x27d4eb2du);
    low ^= uint(saturate(precomputedNoise) * 4294967295.0f);

    uint high = PathTracingHash(pixel.y ^ (pixel.x * 0x68bc21ebu));
    high = PathTracingHash(high ^ PathTracingHash(samplePhase +
        0x9e3779b9u));
    high = PathTracingHash(high ^ successfulSampleCount * 0x02e5be93u);
    high = PathTracingHash(high ^ serialHigh);
    high = PathTracingHash(high ^ failedAttemptSalt * 0x165667b1u);
    high ^= PathTracingHash(asuint(precomputedNoise));
    return uint2(PathTracingHash(low), PathTracingHash(high));
}

float PathTracingLuminance(float3 value)
{
    return dot(max(value, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
}

void PathTracingBuildBasis(
    float3 normal,
    out float3 tangent,
    out float3 bitangent)
{
    const float signValue = normal.z >= 0.0f ? 1.0f : -1.0f;
    const float denominator = -1.0f / (signValue + normal.z);
    const float product = normal.x * normal.y * denominator;
    tangent = float3(
        1.0f + signValue * normal.x * normal.x * denominator,
        signValue * product,
        -signValue * normal.x);
    bitangent = float3(
        product,
        signValue + normal.y * normal.y * denominator,
        -normal.y);
}

float3 PathTracingLocalToWorld(float3 local, float3 normal)
{
    float3 tangent;
    float3 bitangent;
    PathTracingBuildBasis(normal, tangent, bitangent);
    return PbrSafeNormalize(
        tangent * local.x + bitangent * local.y + normal * local.z,
        normal);
}

float3 PathTracingSampleUniformCone(
    float2 random,
    float oneMinusCosineMaximum,
    float3 axis)
{
    const float cosine = 1.0f - random.x * oneMinusCosineMaximum;
    const float sine = sqrt(saturate(1.0f - cosine * cosine));
    const float phi = UVSR_PATH_TWO_PI * random.y;
    return PathTracingLocalToWorld(
        float3(sine * cos(phi), sine * sin(phi), cosine),
        axis);
}

float3 PathTracingSampleCosineHemisphere(float2 random, float3 normal)
{
    const float radius = sqrt(saturate(random.x));
    const float phi = UVSR_PATH_TWO_PI * random.y;
    const float2 disk = radius * float2(cos(phi), sin(phi));
    return PathTracingLocalToWorld(
        float3(disk, sqrt(saturate(1.0f - random.x))),
        normal);
}

float3 PathTracingSampleGgxHalfVector(
    float2 random,
    float alpha,
    float3 normal)
{
    const float alphaSquared = alpha * alpha;
    const float cosineSquared = (1.0f - random.x) /
        max(1.0f + (alphaSquared - 1.0f) * random.x, 1.0e-6f);
    const float cosine = sqrt(saturate(cosineSquared));
    const float sine = sqrt(saturate(1.0f - cosineSquared));
    const float phi = UVSR_PATH_TWO_PI * random.y;
    return PathTracingLocalToWorld(
        float3(sine * cos(phi), sine * sin(phi), cosine),
        normal);
}

PbrPreparedMaterial PathTracingPrepareMaterial(
    PathTracingSurface surface)
{
    // Reconstruct the exact UVSR metallic-roughness inputs used by the raster
    // G-buffer. Donut's diffuseAlbedo is already Fresnel-attenuated and cannot
    // be fed back into EvaluateBsdfPrepared without attenuating dielectrics a
    // second time.
    PbrMaterialParameters material = (PbrMaterialParameters)0;
    material.baseColor = max(surface.material.baseColor, 0.0f);
    material.metalness = saturate(surface.material.metalness);
    material.perceptualRoughness = saturate(surface.material.roughness);
    material.dielectricF0 =
        (surface.materialConstants.flags &
            MaterialFlags_UseSpecularGlossModel) == 0 &&
        surface.materialConstants.specularColor.r > 0.0f
            ? saturate(surface.materialConstants.specularColor.r)
            : IorToF0(1.5f);
    return PreparePbrMaterial(material);
}

PbrPreparedSurface PathTracingPrepareSurface(
    PathTracingSurface surface,
    float3 viewDirection)
{
    PbrSurfaceInteraction interaction;
    interaction.position = surface.position;
    interaction.shadingNormal = surface.shadingNormal;
    interaction.geometricNormal = surface.geometricNormal;
    interaction.viewDirection = viewDirection;
    return PreparePbrSurface(interaction);
}

// The shared raster BRDF intentionally floors a few denominators for robust
// real-time shading. A path estimator cannot use those floors because its GGX
// sampler draws from the unfloored distribution: changing only the evaluated
// density breaks f/pdf and darkens very smooth highlights. The transport-local
// form instead accepts every positive finite analytic denominator.
float PathTracingD_GGXExact(float NoH, float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float denominator =
        NoH * NoH * (alphaSquared - 1.0f) + 1.0f;
    const float normalization =
        UVSR_PI * denominator * denominator;
    return normalization > 0.0f && isfinite(normalization)
        ? alphaSquared / normalization
        : 0.0f;
}

float PathTracingV_SmithGGXCorrelatedExact(
    float NoV,
    float NoL,
    float alpha)
{
    const float alphaSquared = alpha * alpha;
    const float lambdaV = NoL * sqrt(max(
        NoV * NoV * (1.0f - alphaSquared) + alphaSquared,
        0.0f));
    const float lambdaL = NoV * sqrt(max(
        NoL * NoL * (1.0f - alphaSquared) + alphaSquared,
        0.0f));
    const float denominator = lambdaV + lambdaL;
    return denominator > 0.0f && isfinite(denominator)
        ? 0.5f / denominator
        : 0.0f;
}

float PathTracingPdfGGXExact(float NoH, float VoH, float alpha)
{
    const float denominator = 4.0f * abs(VoH);
    return denominator > 0.0f && isfinite(denominator)
        ? PathTracingD_GGXExact(NoH, alpha) * saturate(NoH) /
            denominator
        : 0.0f;
}

PbrBsdfEvaluation PathTracingEvaluateBsdfPreparedExact(
    PbrPreparedMaterial material,
    PbrPreparedSurface surface,
    float3 directionToLight)
{
    PbrBsdfEvaluation result = (PbrBsdfEvaluation)0;
    const float geometricNoL = dot(
        surface.geometricNormal,
        directionToLight);
    const float NoL = dot(surface.shadingNormal, directionToLight);
    if (surface.geometricNoV <= UVSR_MIN_COSINE ||
        geometricNoL <= UVSR_MIN_COSINE ||
        surface.shadingNoV <= UVSR_MIN_COSINE || NoL <= UVSR_MIN_COSINE)
    {
        return result;
    }

    const float3 halfVector = PbrSafeNormalize(
        surface.viewDirection + directionToLight,
        surface.shadingNormal);
    const float NoH = saturate(dot(surface.shadingNormal, halfVector));
    const float VoH = saturate(dot(surface.viewDirection, halfVector));
    const float3 fresnel = FresnelSchlick(VoH, material.specularF0);
    const float distribution =
        PathTracingD_GGXExact(NoH, material.alpha);
    const float visibility = PathTracingV_SmithGGXCorrelatedExact(
        surface.shadingNoV,
        NoL,
        material.alpha);
    result.diffuse =
        material.diffuseColor * (1.0f - fresnel) * UVSR_INV_PI;
    result.specular = EvaluateGGX(distribution, visibility, fresnel);
    result.total = result.diffuse + result.specular;
    result.diffusePdf = PdfLambert(NoL);
    result.specularPdf = PathTracingPdfGGXExact(NoH, VoH, material.alpha);
    return result;
}

float PathTracingDiffuseSelectionProbability(
    PbrPreparedMaterial material)
{
    const float diffuseWeight = PathTracingLuminance(material.diffuseColor);
    const float specularWeight = PathTracingLuminance(material.specularF0);
    const float total = diffuseWeight + specularWeight;
    if (!(total > 0.0f) || !isfinite(total))
        return 1.0f;
    if (!(diffuseWeight > 0.0f))
        return 0.0f;
    if (!(specularWeight > 0.0f))
        return 1.0f;
    return clamp(diffuseWeight / total, 0.05f, 0.95f);
}

struct PathTracingBsdfSample
{
    float3 direction;
    float3 weight;
    float pdf;
    uint diffuseBranch;
    uint valid;
};

PathTracingBsdfSample PathTracingSampleBsdf(
    PathTracingSurface surface,
    float3 viewDirection,
    float3 random,
    bool forceDiffuse)
{
    PathTracingBsdfSample result = (PathTracingBsdfSample)0;
    const PbrPreparedMaterial material =
        PathTracingPrepareMaterial(surface);
    const PbrPreparedSurface preparedSurface =
        PathTracingPrepareSurface(surface, viewDirection);
    const float diffuseProbability =
        PathTracingDiffuseSelectionProbability(material);
    const bool sampleDiffuse = forceDiffuse ||
        random.x < diffuseProbability;

    if (sampleDiffuse)
    {
        result.direction = PathTracingSampleCosineHemisphere(
            random.yz,
            preparedSurface.shadingNormal);
        result.diffuseBranch = 1u;
    }
    else
    {
        const float3 halfVector = PathTracingSampleGgxHalfVector(
            random.yz,
            material.alpha,
            preparedSurface.shadingNormal);
        result.direction = reflect(-preparedSurface.viewDirection, halfVector);
    }
    result.direction = PbrSafeNormalize(
        result.direction,
        preparedSurface.shadingNormal);

    const PbrBsdfEvaluation evaluation = PathTracingEvaluateBsdfPreparedExact(
        material,
        preparedSurface,
        result.direction);
    result.pdf = diffuseProbability * evaluation.diffusePdf +
        (1.0f - diffuseProbability) * evaluation.specularPdf;
    const float cosine = saturate(dot(
        preparedSurface.shadingNormal,
        result.direction));
    if (result.pdf > 0.0f && isfinite(result.pdf) && cosine > 0.0f &&
        dot(preparedSurface.geometricNormal, result.direction) > 0.0f)
    {
        result.weight = evaluation.total *
            (cosine / result.pdf);
        result.valid = all(isfinite(result.weight)) ? 1u : 0u;
    }
    return result;
}

struct PathTracingReservoir
{
    float selected;
    float weightSum;
    float selectedTarget;
    float candidateCount;
    uint selectedSampleSeed;
};

PathTracingReservoir PathTracingLoadReservoir(
    float4 packed,
    uint selectedSampleSeed)
{
    PathTracingReservoir reservoir;
    reservoir.selected = packed.x;
    reservoir.weightSum = max(packed.y, 0.0f);
    reservoir.selectedTarget = max(packed.z, 0.0f);
    reservoir.candidateCount = max(packed.w, 0.0f);
    reservoir.selectedSampleSeed = selectedSampleSeed;
    return reservoir;
}

float4 PathTracingStoreReservoir(PathTracingReservoir reservoir)
{
    return float4(
        reservoir.selected,
        reservoir.weightSum,
        reservoir.selectedTarget,
        reservoir.candidateCount);
}

bool PathTracingReservoirIsValid(PathTracingReservoir reservoir)
{
    return reservoir.candidateCount > 0.0f &&
        reservoir.weightSum > 0.0f &&
        reservoir.selectedTarget > 0.0f &&
        all(isfinite(float4(
            reservoir.selected,
            reservoir.weightSum,
            reservoir.selectedTarget,
            reservoir.candidateCount)));
}

void PathTracingReservoirUpdate(
    inout PathTracingReservoir reservoir,
    float selected,
    uint selectedSampleSeed,
    float target,
    float groupWeight,
    float groupCandidateCount,
    float random)
{
    if (!(groupCandidateCount > 0.0f) || !isfinite(groupCandidateCount))
        return;
    const float newCandidateCount =
        reservoir.candidateCount + groupCandidateCount;
    if (!isfinite(newCandidateCount))
        return;
    // M counts every submitted proposal. Zero-contribution candidates do not
    // participate in selection, but omitting them from M brightens the RIS
    // estimator whenever a sampled light is occluded or back-facing.
    reservoir.candidateCount = newCandidateCount;
    if (!(target > 0.0f) ||
        !(groupWeight > 0.0f) ||
        !all(isfinite(float3(target, groupWeight, selected))))
    {
        return;
    }
    const float newWeightSum = reservoir.weightSum + groupWeight;
    if (!isfinite(newWeightSum))
        return;
    if (random * newWeightSum < groupWeight)
    {
        reservoir.selected = selected;
        reservoir.selectedTarget = target;
        reservoir.selectedSampleSeed = selectedSampleSeed;
    }
    reservoir.weightSum = newWeightSum;
}

void PathTracingReservoirCombine(
    inout PathTracingReservoir reservoir,
    PathTracingReservoir reused,
    float targetAtCurrentSurface,
    float random)
{
    if (!(reused.candidateCount > 0.0f) ||
        !isfinite(reused.candidateCount))
        return;
    // Preserve the reused reservoir's distribution while bounding its
    // effective history. Without this cap, recursively reused candidate counts
    // and weights grow every frame until they overflow and invalidate DI.
    const float reusedCandidateScale = min(
        1.0f,
        UVSR_PATH_MAX_REUSED_RESERVOIR_CANDIDATES /
            reused.candidateCount);
    const bool reusableSelection = PathTracingReservoirIsValid(reused) &&
        targetAtCurrentSurface > 0.0f &&
        isfinite(targetAtCurrentSurface);
    const float groupWeight = reusableSelection
        ? reused.weightSum * reusedCandidateScale *
            targetAtCurrentSurface / reused.selectedTarget
        : 0.0f;
    PathTracingReservoirUpdate(
        reservoir,
        reused.selected,
        reused.selectedSampleSeed,
        reusableSelection ? targetAtCurrentSurface : 0.0f,
        groupWeight,
        reused.candidateCount * reusedCandidateScale,
        random);
}

float PathTracingReservoirNormalization(
    PathTracingReservoir reservoir)
{
    return PathTracingReservoirIsValid(reservoir)
        ? reservoir.weightSum /
            (reservoir.candidateCount * reservoir.selectedTarget)
        : 0.0f;
}

struct PathTracingContributionReservoir
{
    float3 contributionMean;
    float targetSum;
    uint candidateCount;
};

void PathTracingContributionReservoirUpdate(
    inout PathTracingContributionReservoir reservoir,
    float3 contribution,
    float unusedRandom)
{
    if (!all(isfinite(contribution)))
        return;

    // Conditional on the already-evaluated candidates, the previous
    // luminance-proportional RIS estimator selected candidate i with t_i/S
    // and returned C_i*S/(M*t_i). Its expectation is exactly sum(C_i)/M over
    // eligible candidates. Accumulate that conditional expectation directly:
    // this is the Rao-Blackwellized estimator and removes selection variance
    // without changing the estimator's expectation. Every finite proposal,
    // including a successful black path, still contributes one to M.
    const uint newCandidateCount = reservoir.candidateCount + 1u;
    const float target = max(PathTracingLuminance(contribution), 0.0f);
    const float newTargetSum = reservoir.targetSum + target;
    const bool eligible = target > UVSR_PATH_TARGET_EPSILON &&
        isfinite(target) && isfinite(newTargetSum);
    const float3 eligibleContribution = eligible
        ? contribution
        : 0.0f;
    if (eligible)
        reservoir.targetSum = newTargetSum;
    const float inverseCount = rcp(float(newCandidateCount));
    // Form the online mean as two bounded weighted terms. This avoids an
    // otherwise unnecessary overflow when two individually finite HDR
    // candidates approach the float maximum.
    reservoir.contributionMean =
        reservoir.contributionMean *
            (float(reservoir.candidateCount) * inverseCount) +
        eligibleContribution * inverseCount;
    reservoir.candidateCount = newCandidateCount;
}

float3 PathTracingContributionReservoirEstimate(
    PathTracingContributionReservoir reservoir)
{
    if (reservoir.candidateCount == 0u ||
        !all(isfinite(reservoir.contributionMean)))
    {
        return 0.0f;
    }
    return reservoir.contributionMean;
}

float PathTracingLightPowerWeight(LightConstants light)
{
    const float colorPower = PathTracingLuminance(light.color) *
        max(light.intensity, 0.0f);
    if (light.lightType == LightType_Directional)
        return colorPower;
    const float radiusWeight = light.radius > 0.0f
        ? max(light.radius * light.radius, 1.0e-4f)
        : 1.0f;
    return colorPower * radiusWeight;
}

struct PathTracingAnalyticLightSample
{
    PbrLightSample pbr;
    float3 sampledEndpoint;
    uint hasFiniteEndpoint;
};

float PathTracingFinitePositionalProfile(
    LightConstants light,
    float centerDistanceSquared,
    float3 directionToSample,
    bool useFlashlightProfile,
    FlashlightBeamProfile flashlightProfile)
{
    if (!isfinite(light.angularSizeOrInvRange) ||
        !isfinite(centerDistanceSquared) ||
        !all(isfinite(directionToSample)))
    {
        return 0.0f;
    }

    float rangeWeight = 1.0f;
    if (light.angularSizeOrInvRange > 0.0f)
    {
        const float inverseRangeSquared = light.angularSizeOrInvRange *
            light.angularSizeOrInvRange;
        rangeWeight = saturate(
            1.0f - centerDistanceSquared * inverseRangeSquared);
        rangeWeight *= rangeWeight;
        if (!(rangeWeight > 0.0f) || !isfinite(rangeWeight))
            return 0.0f;
    }

    float spotWeight = 1.0f;
    if (light.lightType == LightType_Spot)
    {
        if (!all(isfinite(light.direction)) ||
            !isfinite(light.innerAngle) || !isfinite(light.outerAngle))
        {
            return 0.0f;
        }
        const float3 lightDirection = PbrSafeNormalize(
            light.direction,
            float3(0.0f, -1.0f, 0.0f));
        const bool validFlashlightProfile =
            useFlashlightProfile &&
            flashlightProfile.active > 0.5f &&
            isfinite(flashlightProfile.shapeExponent) &&
            flashlightProfile.shapeExponent >=
                UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT &&
            flashlightProfile.shapeExponent <=
                UVSR_FLASHLIGHT_MAX_SHAPE_EXPONENT &&
            isfinite(flashlightProfile.spillInnerCosine) &&
            isfinite(flashlightProfile.spillOuterCosine) &&
            isfinite(flashlightProfile.spillWeight) &&
            isfinite(flashlightProfile.hotspotWeight);
        if (validFlashlightProfile)
        {
            spotWeight = EvaluateFlashlightBeamProfile(
                flashlightProfile,
                lightDirection,
                -directionToSample);
        }
        else
        {
            const float cosTheta = dot(-directionToSample, lightDirection);
            const float cosInner = cos(light.innerAngle * 0.5f);
            const float cosOuter = cos(light.outerAngle * 0.5f);
            if (!isfinite(cosTheta) || !isfinite(cosInner) ||
                !isfinite(cosOuter))
            {
                return 0.0f;
            }
            spotWeight = saturate(
                (cosTheta - cosOuter) /
                max(cosInner - cosOuter, UVSR_MIN_PDF));
            spotWeight *= spotWeight * (3.0f - 2.0f * spotWeight);
        }
        if (!(spotWeight > 0.0f) || !isfinite(spotWeight))
            return 0.0f;
    }

    const float profile = rangeWeight * spotWeight;
    return isfinite(profile) ? profile : 0.0f;
}

bool PathTracingAnalyticLightInputsAreFinite(LightConstants light)
{
    return all(isfinite(light.position)) &&
        all(isfinite(light.direction)) &&
        all(isfinite(light.color)) &&
        isfinite(light.radius) && isfinite(light.intensity) &&
        isfinite(light.angularSizeOrInvRange) &&
        isfinite(light.innerAngle) && isfinite(light.outerAngle);
}

PathTracingAnalyticLightSample PathTracingSampleAnalyticLight(
    LightConstants light,
    float3 surfacePosition,
    float visibility,
    bool useFlashlightProfile,
    FlashlightBeamProfile flashlightProfile,
    uint sampleSeed)
{
    PathTracingAnalyticLightSample result =
        (PathTracingAnalyticLightSample)0;
    if (!PathTracingAnalyticLightInputsAreFinite(light) ||
        !all(isfinite(surfacePosition)))
    {
        return result;
    }
    const bool directional = light.lightType == LightType_Directional;
    const bool positional = light.lightType == LightType_Point ||
        light.lightType == LightType_Spot;
    const bool finiteDirectional = directional &&
        isfinite(light.angularSizeOrInvRange) &&
        light.angularSizeOrInvRange > 0.0f;
    const bool finitePositional = positional && isfinite(light.radius) &&
        light.radius > 0.0f;

    if (!finiteDirectional && !finitePositional)
    {
        // This exact shared call is the transport's delta-light contract.
        // In particular, angular size == 0 and radius <= 0 retain the same
        // direction, radiance, visibility, and unit directional PDF as raster.
        result.pbr = SamplePbrLight(
            light,
            surfacePosition,
            visibility,
            useFlashlightProfile,
            flashlightProfile);
        if (positional && all(isfinite(light.position)))
        {
            result.sampledEndpoint = light.position;
            result.hasFiniteEndpoint = 1u;
        }
        return result;
    }

    result.pbr.visibility = saturate(visibility);
    result.pbr.lightSelectionPdf = 1.0f;
    const float2 random = PathTracingFiniteLightRandom(sampleSeed);
    if (finiteDirectional)
    {
        const float alpha = 0.5f * light.angularSizeOrInvRange;
        if (!(alpha > 0.0f) || !(alpha < UVSR_PATH_PI))
        {
            return result;
        }
        const float sineAlpha = sin(alpha);
        const float sineHalfAlpha = sin(0.5f * alpha);
        const float sineAlphaSquared = sineAlpha * sineAlpha;
        const float oneMinusCosineMaximum =
            2.0f * sineHalfAlpha * sineHalfAlpha;
        const float solidAngle =
            UVSR_PATH_TWO_PI * oneMinusCosineMaximum;
        if (!(sineAlphaSquared > 0.0f) ||
            !(solidAngle > 0.0f) ||
            !isfinite(sineAlphaSquared) || !isfinite(solidAngle))
        {
            return result;
        }

        const float3 centerDirection = -PbrSafeNormalize(
            light.direction,
            float3(0.0f, -1.0f, 0.0f));
        result.pbr.directionToLight = PathTracingSampleUniformCone(
            random,
            oneMinusCosineMaximum,
            centerDirection);
        result.pbr.directionalPdf = 1.0f / solidAngle;
        const float radianceScale = light.intensity /
            (UVSR_PATH_PI * sineAlphaSquared);
        const float3 incidentRadiance = light.color * radianceScale;
        if (all(isfinite(result.pbr.directionToLight)) &&
            isfinite(result.pbr.directionalPdf) &&
            result.pbr.directionalPdf > 0.0f &&
            all(isfinite(incidentRadiance)))
        {
            result.pbr.incidentRadiance = max(incidentRadiance, 0.0f);
        }
        return result;
    }

    const float radius = light.radius;
    const float radiusSquared = radius * radius;
    const float3 surfaceToCenter = light.position - surfacePosition;
    const float actualDistanceSquared = dot(
        surfaceToCenter,
        surfaceToCenter);
    if (!(radiusSquared > 0.0f) || !isfinite(radiusSquared) ||
        !(actualDistanceSquared >= 0.0f) ||
        !isfinite(actualDistanceSquared))
    {
        return result;
    }
    const float centerDistance = sqrt(actualDistanceSquared);
    const float3 centerDirection = centerDistance > 0.0f
        ? surfaceToCenter / centerDistance
        : float3(0.0f, 1.0f, 0.0f);

    float endpointDistance = 0.0f;
    if (centerDistance > radius)
    {
        const float sineAlpha = radius / centerDistance;
        const float sineAlphaSquared = sineAlpha * sineAlpha;
        const float cosineAlpha = sqrt(saturate(
            1.0f - sineAlphaSquared));
        // This quotient is algebraically 1-cos(alpha), but remains positive
        // for small apparent emitters where 1-cos(alpha) rounds to zero.
        const float oneMinusCosineMaximum = sineAlphaSquared /
            (1.0f + cosineAlpha);
        const float solidAngle =
            UVSR_PATH_TWO_PI * oneMinusCosineMaximum;
        if (!(solidAngle > 0.0f) || !isfinite(solidAngle))
            return result;
        result.pbr.directionToLight = PathTracingSampleUniformCone(
            random,
            oneMinusCosineMaximum,
            centerDirection);
        result.pbr.directionalPdf = 1.0f / solidAngle;

        const float centerProjection = dot(
            surfaceToCenter,
            result.pbr.directionToLight);
        const float discriminant = radiusSquared -
            (actualDistanceSquared - centerProjection * centerProjection);
        if (!isfinite(discriminant))
            return (PathTracingAnalyticLightSample)0;
        endpointDistance = centerProjection - sqrt(max(discriminant, 0.0f));
    }
    else
    {
        // A receiver inside or on a two-sided spherical shell sees the whole
        // 4pi domain. Intersect the sampled ray with the exit root.
        result.pbr.directionToLight = PathTracingSampleUniformCone(
            random,
            2.0f,
            centerDirection);
        result.pbr.directionalPdf = 1.0f / (4.0f * UVSR_PATH_PI);
        const float3 centerToSurface = surfacePosition - light.position;
        const float projected = dot(
            centerToSurface,
            result.pbr.directionToLight);
        const float discriminant = projected * projected -
            (actualDistanceSquared - radiusSquared);
        if (!isfinite(discriminant))
            return (PathTracingAnalyticLightSample)0;
        endpointDistance = -projected + sqrt(max(discriminant, 0.0f));
    }

    if (!(endpointDistance >= 0.0f) || !isfinite(endpointDistance) ||
        !all(isfinite(result.pbr.directionToLight)) ||
        !(result.pbr.directionalPdf > 0.0f) ||
        !isfinite(result.pbr.directionalPdf))
    {
        return (PathTracingAnalyticLightSample)0;
    }
    result.sampledEndpoint = surfacePosition +
        result.pbr.directionToLight * endpointDistance;
    result.hasFiniteEndpoint = 1u;

    const float centerDistanceSquared = max(
        actualDistanceSquared,
        UVSR_MIN_LIGHT_DISTANCE_SQUARED);
    const float profile = PathTracingFinitePositionalProfile(
        light,
        centerDistanceSquared,
        result.pbr.directionToLight,
        useFlashlightProfile,
        flashlightProfile);
    const float radianceScale = light.intensity /
        (UVSR_PATH_PI * radiusSquared);
    const float3 incidentRadiance = light.color *
        (radianceScale * profile);
    if (all(isfinite(result.sampledEndpoint)) &&
        all(isfinite(incidentRadiance)))
    {
        result.pbr.incidentRadiance = max(incidentRadiance, 0.0f);
    }
    return result;
}

float3 PathTracingEvaluateUnshadowedAnalyticSample(
    PathTracingSurface surface,
    float3 viewDirection,
    PathTracingAnalyticLightSample analyticSample,
    float lightSelectionPdf)
{
    const PbrLightSample lightSample = analyticSample.pbr;
    const float samplingPdf =
        lightSelectionPdf * lightSample.directionalPdf;
    if (!(samplingPdf > 0.0f) || !isfinite(samplingPdf))
        return 0.0f;
    const PbrPreparedSurface preparedSurface =
        PathTracingPrepareSurface(surface, viewDirection);
    if (!CanEvaluatePbrDirectSurfacePrepared(
            preparedSurface,
            lightSample.directionToLight))
    {
        return 0.0f;
    }
    const PbrBsdfEvaluation bsdf = PathTracingEvaluateBsdfPreparedExact(
        PathTracingPrepareMaterial(surface),
        preparedSurface,
        lightSample.directionToLight);
    const float cosineTerm = saturate(dot(
        preparedSurface.shadingNormal,
        lightSample.directionToLight));
    const float3 evaluated = max(lightSample.incidentRadiance, 0.0f) *
        (bsdf.diffuse + bsdf.specular) *
        (cosineTerm * saturate(lightSample.visibility) / samplingPdf);
    return all(isfinite(evaluated)) ? evaluated : 0.0f;
}

float3 PathTracingEvaluateUnshadowedLightProxy(
    PathTracingSurface surface,
    float3 viewDirection,
    uint lightIndex)
{
    if (lightIndex >= g_PathTracing.lightCount)
        return 0.0f;
    PbrLightSample lightSample = SamplePbrLight(
        t_PathTracingLights[lightIndex],
        surface.position,
        1.0f,
        int(lightIndex) == g_PathTracing.flashlight.lightIndex,
        g_PathTracing.flashlight.profile);
    lightSample.lightSelectionPdf = 1.0f;
    const PbrPreparedSurface preparedSurface =
        PathTracingPrepareSurface(surface, viewDirection);
    if (!CanEvaluatePbrDirectSurfacePrepared(
            preparedSurface,
            lightSample.directionToLight))
    {
        return 0.0f;
    }
    const PbrBsdfEvaluation bsdf = PathTracingEvaluateBsdfPreparedExact(
        PathTracingPrepareMaterial(surface),
        preparedSurface,
        lightSample.directionToLight);
    const float samplingPdf =
        lightSample.lightSelectionPdf * lightSample.directionalPdf;
    if (!(samplingPdf > 0.0f) || !isfinite(samplingPdf))
        return 0.0f;
    const float cosineTerm = saturate(dot(
        preparedSurface.shadingNormal,
        lightSample.directionToLight));
    const float3 evaluated = max(lightSample.incidentRadiance, 0.0f) *
        (bsdf.diffuse + bsdf.specular) *
        (cosineTerm * saturate(lightSample.visibility) / samplingPdf);
    return all(isfinite(evaluated)) ? evaluated : 0.0f;
}

float3 PathTracingEvaluateUnshadowedLight(
    PathTracingSurface surface,
    float3 viewDirection,
    uint lightIndex,
    uint sampleSeed)
{
    if (lightIndex >= g_PathTracing.lightCount)
        return 0.0f;
    const PathTracingAnalyticLightSample analyticSample =
        PathTracingSampleAnalyticLight(
            t_PathTracingLights[lightIndex],
            surface.position,
            1.0f,
            int(lightIndex) == g_PathTracing.flashlight.lightIndex,
            g_PathTracing.flashlight.profile,
            sampleSeed);
    return PathTracingEvaluateUnshadowedAnalyticSample(
        surface,
        viewDirection,
        analyticSample,
        1.0f);
}

#if UVSR_PT_NEE_MODE == 2
float PathTracingAdaptiveLightWeight(
    PathTracingSurface surface,
    float3 viewDirection,
    uint lightIndex)
{
    // This is only a positive selector proxy. The normalized value returned by
    // PathTracingSelectLight is kept exactly in the estimator; this proxy floor
    // is never substituted for a selected light or directional PDF.
    return max(
        PathTracingLuminance(PathTracingEvaluateUnshadowedLightProxy(
            surface, viewDirection, lightIndex)),
        UVSR_PATH_TARGET_EPSILON);
}
#endif

float PathTracingLightSelectionWeight(
    PathTracingSurface surface,
    float3 viewDirection,
    uint lightIndex)
{
#if UVSR_PT_NEE_MODE == 0
    return 1.0f;
#elif UVSR_PT_NEE_MODE == 1
    // As above, this floor defines the authored Power proposal distribution;
    // the resulting exact normalized discrete PDF is not clamped downstream.
    return max(
        PathTracingLightPowerWeight(t_PathTracingLights[lightIndex]),
        UVSR_PATH_TARGET_EPSILON);
#elif UVSR_PT_NEE_MODE == 2
    return PathTracingAdaptiveLightWeight(
        surface, viewDirection, lightIndex);
#else
#error Unsupported UVSR_PT_NEE_MODE
#endif
}

#if UVSR_PT_NEE_MODE != 0
float PathTracingLightWeightRange(
    PathTracingSurface surface,
    float3 viewDirection,
    uint beginIndex,
    uint endIndex)
{
    float total = 0.0f;
    [loop]
    for (uint index = beginIndex;
        index < min(endIndex, g_PathTracing.lightCount);
        ++index)
    {
        total += PathTracingLightSelectionWeight(
            surface, viewDirection, index);
    }
    return total;
}
#endif

uint PathTracingSelectLight(
    PathTracingSurface surface,
    float3 viewDirection,
    float random,
    out float selectionPdf)
{
    selectionPdf = 0.0f;
    if (g_PathTracing.lightCount == 0u)
        return 0u;
#if UVSR_PT_NEE_MODE == 0
    const uint selected = min(
        uint(random * float(g_PathTracing.lightCount)),
        g_PathTracing.lightCount - 1u);
    selectionPdf = 1.0f / float(g_PathTracing.lightCount);
    return selected;
#else
    const float totalWeight = PathTracingLightWeightRange(
        surface,
        viewDirection,
        0u,
        g_PathTracing.lightCount);
    if (!(totalWeight > 0.0f))
        return 0u;

    uint selectedIndex = 0u;
#if UVSR_PT_NEE_MODE == 2
    // A binary hierarchy over the complete submitted-light buffer.
    // Weights are rebuilt at the current path vertex, making this the
    // adaptive-tree option without persistent scene-specific tables.
    uint beginIndex = 0u;
    uint span = 1u;
    [loop]
    while (span < g_PathTracing.lightCount)
        span <<= 1u;
    float target = random * totalWeight;
    [loop]
    while (span > 1u)
    {
        const uint halfSpan = span >> 1u;
        const float leftWeight = PathTracingLightWeightRange(
            surface,
            viewDirection,
            beginIndex,
            beginIndex + halfSpan);
        if (target >= leftWeight)
        {
            target -= leftWeight;
            beginIndex += halfSpan;
        }
        span = halfSpan;
    }
    selectedIndex = min(beginIndex, g_PathTracing.lightCount - 1u);
#elif UVSR_PT_NEE_MODE == 1
    float target = random * totalWeight;
    [loop]
    for (uint index = 0u; index < g_PathTracing.lightCount; ++index)
    {
        const float weight = PathTracingLightSelectionWeight(
            surface, viewDirection, index);
        if (target < weight || index + 1u == g_PathTracing.lightCount)
        {
            selectedIndex = index;
            break;
        }
        target -= weight;
    }
#endif
    selectionPdf = PathTracingLightSelectionWeight(
        surface, viewDirection, selectedIndex) / totalWeight;
    return selectedIndex;
#endif
}

void PathTracingPrepareAnalyticShadowRay(
    PathTracingSurface surface,
    PathTracingAnalyticLightSample analyticSample,
    out float3 shadowOrigin,
    out float3 shadowDirection,
    out float shadowRayMaximum)
{
    shadowDirection = analyticSample.pbr.directionToLight;
    shadowOrigin = PathTracingPrepareRayOrigin(
        surface.position,
        surface.geometricNormal,
        shadowDirection,
        g_PathTracing.rayBias);
    shadowRayMaximum = g_PathTracing.maximumRayDistance;
    if (analyticSample.hasFiniteEndpoint == 0u)
        return;

    // Visibility must terminate at the sampled emitter point after the robust
    // origin offset. Center distance and radius shortcuts do not identify the
    // sampled proposal and can incorrectly shadow another point on the shell.
    const float3 shadowVector =
        analyticSample.sampledEndpoint - shadowOrigin;
    const float endpointDistance = length(shadowVector);
    if (!(endpointDistance > 0.0f) || !isfinite(endpointDistance))
    {
        shadowRayMaximum = 0.0f;
        return;
    }
    shadowDirection = shadowVector / endpointDistance;
    shadowRayMaximum = endpointDistance - g_PathTracing.rayBias;
}

float3 PathTracingEvaluateSelectedLightPrepared(
    PathTracingSurface surface,
    float3 viewDirection,
    uint lightIndex,
    uint sampleSeed,
    float lightSelectionPdf)
{
    if (lightIndex >= g_PathTracing.lightCount)
        return 0.0f;
    const LightConstants light = t_PathTracingLights[lightIndex];
    const PathTracingAnalyticLightSample analyticSample =
        PathTracingSampleAnalyticLight(
            light,
            surface.position,
            1.0f,
            int(lightIndex) == g_PathTracing.flashlight.lightIndex,
            g_PathTracing.flashlight.profile,
            sampleSeed);
    const PbrLightSample lightSample = analyticSample.pbr;
    // The discrete light selector already returns its exact proposal PDF.
    // Clamping that value here darkens every selected light whose probability
    // is below the clamp, which is reachable in ordinary high-dynamic-range
    // Power and NEE-AT distributions. Reject an invalid proposal, but retain
    // every positive finite probability exactly in the Monte Carlo weight.
    const float samplingPdf =
        lightSelectionPdf * lightSample.directionalPdf;
    if (!(samplingPdf > 0.0f) || !isfinite(samplingPdf))
        return 0.0f;
    const PbrPreparedSurface preparedSurface =
        PathTracingPrepareSurface(surface, viewDirection);
    if (!CanEvaluatePbrDirectSurfacePrepared(
            preparedSurface,
            lightSample.directionToLight))
    {
        return 0.0f;
    }
    float3 shadowOrigin;
    float3 shadowDirection;
    float shadowRayMaximum;
    PathTracingPrepareAnalyticShadowRay(
        surface,
        analyticSample,
        shadowOrigin,
        shadowDirection,
        shadowRayMaximum);
    if (PathTracingTraceOcclusion(
            t_WorldBvh,
            shadowOrigin,
            shadowDirection,
            g_PathTracing.rayBias,
            shadowRayMaximum))
    {
        return 0.0f;
    }
    const PbrBsdfEvaluation bsdf = PathTracingEvaluateBsdfPreparedExact(
        PathTracingPrepareMaterial(surface),
        preparedSurface,
        lightSample.directionToLight);
    const float cosineTerm = saturate(dot(
        preparedSurface.shadingNormal,
        lightSample.directionToLight));
    const float sampleWeight = cosineTerm *
        saturate(lightSample.visibility) / samplingPdf;
    const float3 evaluated = max(lightSample.incidentRadiance, 0.0f) *
        (bsdf.diffuse + bsdf.specular) * sampleWeight;
    return all(isfinite(evaluated)) ? evaluated : 0.0f;
}

float3 PathTracingSampleEnvironment(float3 direction)
{
    return max(
        t_Environment.SampleLevel(
            s_RayMaterialSampler,
            direction,
            0.0f).rgb * g_PathTracing.environmentScale,
        0.0f);
}

float3 PathTracingProbeIncomingRadiance(
    float3 origin,
    float3 direction)
{
    PathTracingSurface probe;
    if (!PathTracingTraceSurface(
            t_WorldBvh,
            origin,
            direction,
            g_PathTracing.rayBias,
            g_PathTracing.maximumRayDistance,
            probe))
    {
        return PathTracingSampleEnvironment(direction);
    }
    return max(probe.material.emissiveColor, 0.0f);
}

#endif // UVSR_PATH_TRACING_SAMPLING_HLSLI
