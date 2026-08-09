#ifndef UVSR_PBR_LIGHTING_HLSLI
#define UVSR_PBR_LIGHTING_HLSLI

#include <donut/shaders/light_cb.h>
#include "flashlight_shared.h"
#include "pbr.hlsli"

static const float UVSR_MIN_LIGHT_DISTANCE_SQUARED = 1e-4f;

float ResolveAnalyticalPositionalLightIntensity(
    LightConstants light,
    float inverseDistance,
    float distanceSquared)
{
    // Radius zero is the exact point-light branch. Positive radii use the
    // projected solid angle of a spherical emitter, which bounds near-field
    // energy and converges to the same luminous-intensity inverse square law
    // as distance becomes large relative to the emitter.
    if (!(isfinite(light.radius) && light.radius > 0.0f))
        return light.intensity / distanceSquared;

    const float halfAngularSize = atan(min(
        light.radius * inverseDistance,
        1.0f));
    const float solidAngleOverPi = halfAngularSize * halfAngularSize;
    const float radianceTimesPi =
        light.intensity / (light.radius * light.radius);
    return radianceTimesPi * solidAngleOverPi;
}

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

    if (light.lightType == LightType_Directional)
    {
        sample.directionToLight = -PbrSafeNormalize(light.direction, float3(0.0f, -1.0f, 0.0f));
        sample.incidentRadiance = max(light.color * light.intensity, 0.0f);
        return sample;
    }

    if (light.lightType != LightType_Point && light.lightType != LightType_Spot)
        return sample;

    float3 surfaceToLight = light.position - surfacePosition;
    float distanceSquared = max(dot(surfaceToLight, surfaceToLight), UVSR_MIN_LIGHT_DISTANCE_SQUARED);
    float inverseDistance = rsqrt(distanceSquared);
    sample.directionToLight = surfaceToLight * inverseDistance;

    // Donut defines intensity for positional lights as luminous intensity.
    // Resolve the selected analytical emitter before range and beam weights.
    const float incidentLightIntensity =
        ResolveAnalyticalPositionalLightIntensity(
            light,
            inverseDistance,
            distanceSquared);
    float rangeWeight = 1.0f;
    if (light.angularSizeOrInvRange > 0.0f)
    {
        float inverseRangeSquared = light.angularSizeOrInvRange *
            light.angularSizeOrInvRange;
        rangeWeight = saturate(1.0f - distanceSquared * inverseRangeSquared);
        rangeWeight *= rangeWeight;
        if (!(rangeWeight > 0.0f))
            return sample;
    }

    float spotWeight = 1.0f;
    if (light.lightType == LightType_Spot)
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
            const float cosInner = cos(light.innerAngle * 0.5f);
            const float cosOuter = cos(light.outerAngle * 0.5f);
            spotWeight = saturate(
                (cosTheta - cosOuter) /
                max(cosInner - cosOuter, UVSR_MIN_PDF));
            spotWeight *= spotWeight * (3.0f - 2.0f * spotWeight);
        }
        if (!(spotWeight > 0.0f))
            return sample;
    }

    sample.incidentRadiance = max(light.color * incidentLightIntensity *
        (rangeWeight * spotWeight), 0.0f);
    return sample;
}

#endif // UVSR_PBR_LIGHTING_HLSLI
