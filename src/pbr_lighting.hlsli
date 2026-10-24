#ifndef UVSR_PBR_LIGHTING_HLSLI
#define UVSR_PBR_LIGHTING_HLSLI

#include <donut/shaders/light_cb.h>
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
