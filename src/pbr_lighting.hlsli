#ifndef UVSR_PBR_LIGHTING_HLSLI
#define UVSR_PBR_LIGHTING_HLSLI

#include "renderer_gpu_contract.h"
#include "flashlight_shared.h"
#include "pbr.hlsli"

static const float UVSR_MIN_LIGHT_DISTANCE_SQUARED = 1e-4f;

PbrLightSample SamplePbrLight(
    LightConstants light,
    float3 surfacePosition,
    float visibility,
    bool useFlashlightProfile,
    FlashlightBeamProfile flashlightProfile)
{
    PbrLightSample sample = (PbrLightSample)0;
    sample.visibility = saturate(visibility);
    sample.lightSelectionPdf = 1.0f;
    sample.directionalPdf = 1.0f;

    if (light.lightType == UVSR_LIGHT_TYPE_DIRECTIONAL)
    {
        sample.directionToLight = -PbrSafeNormalize(light.direction, float3(0.0f, -1.0f, 0.0f));
        sample.incidentRadiance = max(light.color * light.intensity, 0.0f);
        return sample;
    }

    if (light.lightType != UVSR_LIGHT_TYPE_POINT &&
        light.lightType != UVSR_LIGHT_TYPE_SPOT)
        return sample;

    float3 surfaceToLight = light.position - surfacePosition;
    float distanceSquared = max(dot(surfaceToLight, surfaceToLight), UVSR_MIN_LIGHT_DISTANCE_SQUARED);
    float inverseDistance = rsqrt(distanceSquared);
    sample.directionToLight = surfaceToLight * inverseDistance;

    // Donut defines intensity for positional lights as luminous intensity.
    // Resolve the selected analytical emitter before range and beam weights.
    const float incidentLightIntensity =
        ResolveAnalyticalPositionalLightIntensity(
            light.intensity,
            light.radius,
            inverseDistance,
            distanceSquared);
    const float rangeWeight = ResolvePbrAnalyticalRangeWeight(
        distanceSquared,
        light.angularSizeOrInvRange);
    if (!(rangeWeight > 0.0f))
        return sample;

    float spotWeight = 1.0f;
    if (light.lightType == UVSR_LIGHT_TYPE_SPOT)
    {
        float3 lightDirection = PbrSafeNormalize(light.direction, float3(0.0f, -1.0f, 0.0f));
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
                -sample.directionToLight);
        }
        else
        {
            const float cosTheta = dot(
                -sample.directionToLight,
                lightDirection);
            spotWeight = ResolvePbrOrdinarySpotWeight(
                cosTheta,
                light.innerAngle,
                light.outerAngle);
        }
        if (!(spotWeight > 0.0f))
            return sample;
    }

    sample.incidentRadiance = max(light.color *
        ApplyPbrAnalyticalLightProfile(
            incidentLightIntensity,
            rangeWeight,
            spotWeight), 0.0f);
    return sample;
}

#endif // UVSR_PBR_LIGHTING_HLSLI
