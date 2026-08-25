#ifndef UVSR_PATH_TRACING_SAMPLING_HLSLI
#define UVSR_PATH_TRACING_SAMPLING_HLSLI

#include "pbr_lighting.hlsli"
#include "path_tracing_transport_contract.h"

static const float UVSR_PATH_TWO_PI = 6.28318530717958647692f;

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
    return PathTracingLocalToWorld(
        PathTracingSampleCosineHemisphereLocal(random),
        normal);
}

float3 PathTracingSampleGgxHalfVector(
    float2 random,
    float alpha,
    float3 normal)
{
    return PathTracingLocalToWorld(
        PathTracingSampleGgxHalfVectorLocal(random, alpha),
        normal);
}

PbrPreparedMaterial PathTracingPrepareMaterial(
    PathTracingSurface surface)
{
    if (surface.preparedMaterialValid != 0u)
        return surface.preparedMaterial;

    // Reconstruct the exact UVSR metallic-roughness inputs used by the raster
    // G-buffer. Donut's diffuseAlbedo is already Fresnel-attenuated and cannot
    // be fed back into EvaluateBsdfPrepared without attenuating dielectrics a
    // second time.
    const PathTracingPreparedMaterialContract resolved =
        ResolvePathTracingPreparedMaterial(
            surface.material.baseColor,
            surface.material.metalness,
            surface.material.roughness,
            surface.materialConstants.specularColor.r,
            (surface.materialConstants.flags &
                MaterialFlags_UseSpecularGlossModel) != 0);
    PbrPreparedMaterial material;
    material.diffuseColor = resolved.diffuseColor;
    material.specularF0 = resolved.specularF0;
    material.alpha = resolved.alpha;
    return material;
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

PbrBsdfEvaluation PathTracingEvaluateBsdfPreparedExact(
    PbrPreparedMaterial material,
    PbrPreparedSurface surface,
    float3 directionToLight)
{
    const float geometricNoL = dot(
        surface.geometricNormal,
        directionToLight);
    const float NoL = dot(surface.shadingNormal, directionToLight);
    const float3 halfVector = PbrSafeNormalize(
        surface.viewDirection + directionToLight,
        surface.shadingNormal);
    const float NoH = saturate(dot(surface.shadingNormal, halfVector));
    const float VoH = saturate(dot(surface.viewDirection, halfVector));
    PathTracingPreparedMaterialContract contractMaterial;
    contractMaterial.diffuseColor = material.diffuseColor;
    contractMaterial.specularF0 = material.specularF0;
    contractMaterial.alpha = material.alpha;
    const PathTracingBsdfContractEvaluation evaluated =
        ResolvePathTracingBsdfEvaluation(
            contractMaterial,
            surface.geometricNoV,
            geometricNoL,
            surface.shadingNoV,
            NoL,
            NoH,
            VoH);
    PbrBsdfEvaluation result;
    result.diffuse = evaluated.diffuse;
    result.specular = evaluated.specular;
    result.total = evaluated.total;
    result.diffusePdf = evaluated.diffusePdf;
    result.specularPdf = evaluated.specularPdf;
    return result;
}

float PathTracingDiffuseSelectionProbability(
    PbrPreparedMaterial material)
{
    PathTracingPreparedMaterialContract contractMaterial;
    contractMaterial.diffuseColor = material.diffuseColor;
    contractMaterial.specularF0 = material.specularF0;
    contractMaterial.alpha = material.alpha;
    return ResolvePathTracingDiffuseSelectionProbability(contractMaterial);
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
    PathTracingBsdfContractEvaluation contractEvaluation;
    contractEvaluation.diffuse = evaluation.diffuse;
    contractEvaluation.specular = evaluation.specular;
    contractEvaluation.total = evaluation.total;
    contractEvaluation.diffusePdf = evaluation.diffusePdf;
    contractEvaluation.specularPdf = evaluation.specularPdf;
    const PathTracingBsdfWeightContract weighted =
        ResolvePathTracingBsdfWeight(
            contractEvaluation,
            diffuseProbability,
            dot(preparedSurface.shadingNormal, result.direction),
            dot(preparedSurface.geometricNormal, result.direction));
    result.pdf = weighted.pdf;
    result.weight = weighted.weight;
    result.valid = weighted.valid;
    return result;
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

    const float rangeWeight = ResolvePbrAnalyticalRangeWeight(
        centerDistanceSquared,
        light.angularSizeOrInvRange);
    if (!(rangeWeight > 0.0f) || !isfinite(rangeWeight))
        return 0.0f;

    float spotWeight = 1.0f;
    if (light.lightType == UVSR_LIGHT_TYPE_SPOT)
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
            if (!isfinite(cosTheta))
                return 0.0f;
            spotWeight = ResolvePbrOrdinarySpotWeight(
                cosTheta,
                light.innerAngle,
                light.outerAngle);
        }
        if (!(spotWeight > 0.0f) || !isfinite(spotWeight))
            return 0.0f;
    }

    const float profile = ApplyPbrAnalyticalLightProfile(
        1.0f,
        rangeWeight,
        spotWeight);
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
    const bool directional =
        light.lightType == UVSR_LIGHT_TYPE_DIRECTIONAL;
    const bool positional = light.lightType == UVSR_LIGHT_TYPE_POINT ||
        light.lightType == UVSR_LIGHT_TYPE_SPOT;
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
        const PbrFiniteDirectionalEmitterContract emitter =
            ResolvePbrFiniteDirectionalEmitter(
                light.intensity,
                light.angularSizeOrInvRange);
        if (emitter.valid == 0)
            return result;

        const float3 centerDirection = -PbrSafeNormalize(
            light.direction,
            float3(0.0f, -1.0f, 0.0f));
        result.pbr.directionToLight = PathTracingSampleUniformCone(
            random,
            emitter.oneMinusCosineMaximum,
            centerDirection);
        result.pbr.directionalPdf = emitter.directionalPdf;
        const float3 incidentRadiance =
            light.color * emitter.radianceScale;
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

    const PbrFiniteSphereEmitterContract emitter =
        ResolvePbrFiniteSphereEmitter(
            light.intensity,
            radius,
            centerDistance);
    if (emitter.valid == 0)
        return result;

    if (emitter.receiverOutside != 0)
    {
        result.pbr.directionToLight = PathTracingSampleUniformCone(
            random,
            emitter.oneMinusCosineMaximum,
            centerDirection);
    }
    else
    {
        // A receiver inside or on a two-sided spherical shell sees the whole
        // 4pi domain. Intersect the sampled ray with the exit root.
        result.pbr.directionToLight = PathTracingSampleUniformCone(
            random,
            emitter.oneMinusCosineMaximum,
            centerDirection);
    }
    result.pbr.directionalPdf = emitter.directionalPdf;

    const float centerProjection = dot(
        surfaceToCenter,
        result.pbr.directionToLight);
    const PbrFiniteSphereEndpointContract endpoint =
        ResolvePbrFiniteSphereEndpoint(
            centerProjection,
            actualDistanceSquared,
            radiusSquared,
            emitter.receiverOutside != 0);
    if (endpoint.valid == 0 ||
        !all(isfinite(result.pbr.directionToLight)) ||
        !(result.pbr.directionalPdf > 0.0f) ||
        !isfinite(result.pbr.directionalPdf))
    {
        return (PathTracingAnalyticLightSample)0;
    }
    result.sampledEndpoint = surfacePosition +
        result.pbr.directionToLight * endpoint.distance;
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
    const float3 incidentRadiance = light.color *
        (emitter.radianceScale * profile);
    if (all(isfinite(result.sampledEndpoint)) &&
        all(isfinite(incidentRadiance)))
    {
        result.pbr.incidentRadiance = max(incidentRadiance, 0.0f);
    }
    return result;
}

uint PathTracingSelectLight(
    float random,
    out float selectionPdf)
{
    selectionPdf = 0.0f;
    if (g_PathTracing.lightCount == 0u)
        return 0u;
    const uint selected = min(
        uint(random * float(g_PathTracing.lightCount)),
        g_PathTracing.lightCount - 1u);
    selectionPdf = 1.0f / float(g_PathTracing.lightCount);
    return selected;
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

#endif // UVSR_PATH_TRACING_SAMPLING_HLSLI
