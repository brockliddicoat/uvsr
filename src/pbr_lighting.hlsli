#ifndef UVSR_PBR_LIGHTING_HLSLI
#define UVSR_PBR_LIGHTING_HLSLI

#include <donut/shaders/light_cb.h>
#include "flashlight_shared.h"
#include "pbr.hlsli"

static const float UVSR_MIN_LIGHT_DISTANCE_SQUARED = 1e-4f;

PbrLightSample SamplePbrLight(LightConstants light, float3 surfacePosition, float visibility)
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
    // The point-source incident radiance therefore follows inverse-square falloff.
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
        float cosTheta = dot(-sample.directionToLight, lightDirection);
        float shapeExponent =
            -light.radius - UVSR_FLASHLIGHT_SHAPE_RADIUS_TAG;
        bool hasFlashlightShape =
            light.radius <= -(
                UVSR_FLASHLIGHT_SHAPE_RADIUS_TAG +
                UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT) &&
            isfinite(shapeExponent) &&
            shapeExponent >
                UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT + 1e-4f &&
            shapeExponent <= UVSR_FLASHLIGHT_MAX_SHAPE_EXPONENT;
        if (hasFlashlightShape)
        {
            float3 beamRight =
                float3(light.shadowChannel.yzw) /
                UVSR_FLASHLIGHT_AXIS_QUANTIZATION;
            beamRight -=
                lightDirection * dot(beamRight, lightDirection);
            float beamRightLengthSquared =
                dot(beamRight, beamRight);
            hasFlashlightShape =
                all(isfinite(beamRight)) &&
                isfinite(beamRightLengthSquared) &&
                beamRightLengthSquared > 1e-6f;
            if (hasFlashlightShape)
            {
                beamRight *= rsqrt(beamRightLengthSquared);
                float3 beamUp =
                    PbrSafeNormalize(
                        cross(beamRight, lightDirection),
                        float3(0.0f, 1.0f, 0.0f));
                float3 rayFromLight = -sample.directionToLight;
                float axialDistance =
                    dot(rayFromLight, lightDirection);
                float2 beamSlope = float2(
                    dot(rayFromLight, beamRight),
                    dot(rayFromLight, beamUp)) /
                    max(axialDistance, UVSR_MIN_PDF);
                float outerTangent =
                    tan(light.outerAngle * 0.5f);
                if (axialDistance > UVSR_MIN_PDF &&
                    all(isfinite(beamSlope)) &&
                    isfinite(outerTangent))
                {
                    if (any(abs(beamSlope) > outerTangent))
                        return sample;
                    float2 poweredSlope = pow(
                        abs(beamSlope),
                        float2(shapeExponent, shapeExponent));
                    float shapedSlope = pow(
                        poweredSlope.x + poweredSlope.y,
                        rcp(shapeExponent));
                    if (isfinite(shapedSlope))
                        cosTheta = rsqrt(1.0f + shapedSlope * shapedSlope);
                }
            }
        }
        float cosInner = cos(light.innerAngle * 0.5f);
        float cosOuter = cos(light.outerAngle * 0.5f);
        spotWeight = saturate((cosTheta - cosOuter) / max(cosInner - cosOuter, UVSR_MIN_PDF));
        if (!(spotWeight > 0.0f))
            return sample;
        spotWeight *= spotWeight * (3.0f - 2.0f * spotWeight);
    }

    sample.incidentRadiance = max(light.color * light.intensity *
        (rangeWeight * spotWeight / distanceSquared), 0.0f);
    return sample;
}

#endif // UVSR_PBR_LIGHTING_HLSLI
