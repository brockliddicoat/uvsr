#pragma pack_matrix(row_major)

#include "sky_visibility_application.h"
#include "specular_reflections_cb.h"

cbuffer c_Specular : register(b0)
{
    SpecularReflectionConstants g_Specular;
};

#include "specular_reflections_shared.hlsli"

Texture2D<float4> t_Diffuse : register(t0);
Texture2D<float4> t_Material : register(t1);
Texture2D<float4> t_Normals : register(t2);
Texture2D<float4> t_Emissive : register(t3);
Texture2D<float> t_MaterialAmbientOcclusion : register(t4);
Texture2D<float4> t_HitUvPdfDistance : register(t5);
Texture2D<float4> t_HitDirectionFootprint : register(t6);
Texture2D<uint4> t_HitMetadata : register(t7);
Texture2D<float> t_HitConfidence : register(t8);
Texture2D<float4> t_SourceRadiance : register(t9);
Texture2D<float4> t_SceneColorPyramid : register(t10);
TextureCubeArray<float4> t_SpecularEnvironment : register(t11);
Texture2D<float2> t_EnvironmentBrdf : register(t12);
Texture2D<float4> t_SkySpecularEstimate : register(t13);
Texture2D<float> t_Depth : register(t14);
SamplerState s_LinearClamp : register(s0);

RWTexture2D<float4> u_ResolvedSpecular : register(u0);

float3 SpecularResolveEnvironment(float3 direction, float roughness)
{
    const float3 radiance = t_SpecularEnvironment.SampleLevel(
        s_LinearClamp,
        float4(
            direction,
            g_Specular.specularEnvironmentArrayIndex),
        SpecularEnvironmentMip(roughness)).rgb *
        g_Specular.specularEnvironmentScale;
    return SpecularClampHalfRadiance(radiance);
}

float3 SpecularLoadDeferredRadiance(
    uint path,
    float2 hitUv,
    float hitDistance,
    float3 direction,
    float storedFootprint,
    float confidence,
    float destinationRoughness,
    float destinationNormalDotView,
    float projectionPixelsPerMeter,
    out float3 environmentContribution,
    out float selectedMip)
{
    const float3 environment = SpecularResolveEnvironment(
        direction,
        destinationRoughness);
    environmentContribution = environment;
    selectedMip = SpecularEnvironmentMip(destinationRoughness);
    if (path == SpecularHitPathEnvironment ||
        any(hitUv < 0.0f) || any(hitUv > 1.0f))
    {
        return environment;
    }

    const float destinationFootprint = SpecularTraceFootprintPixels(
        destinationRoughness,
        hitDistance,
        destinationNormalDotView,
        projectionPixelsPerMeter);
    selectedMip = SpecularMipFromTraceFootprint(max(
        storedFootprint,
        destinationFootprint));
    const float3 screenRadiance = max(
        t_SceneColorPyramid.SampleLevel(
            s_LinearClamp,
            hitUv,
            selectedMip).rgb,
        0.0f);
    if (path == SpecularHitPathRayTracedScreen)
    {
        environmentContribution = 0.0f;
        return screenRadiance;
    }
    environmentContribution = environment *
        (1.0f - saturate(confidence));
    return lerp(environment, screenRadiance, saturate(confidence));
}

float3 SpecularRouteDiagnostic(uint path)
{
    if (path == SpecularHitPathScreen)
        return float3(0.1f, 0.85f, 0.2f);
    if (path == SpecularHitPathRayTracedScreen)
        return float3(0.15f, 0.35f, 1.0f);
    return float3(0.9f, 0.25f, 0.1f);
}

[numthreads(8, 8, 1)]
void Resolve(uint2 pixel : SV_DispatchThreadID)
{
    if (!SpecularInViewport(pixel))
        return;

    const float4 normalChannels = t_Normals[pixel];
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
    {
        u_ResolvedSpecular[pixel] = 0.0f;
        return;
    }
    float4 channels[4];
    channels[0] = t_Diffuse[pixel];
    channels[1] = t_Material[pixel];
    channels[2] = normalChannels;
    channels[3] = t_Emissive[pixel];
    const float materialAmbientOcclusion =
        t_MaterialAmbientOcclusion[pixel];
    const PbrGBufferData gbuffer = DecodePbrGBuffer(
        channels,
        materialAmbientOcclusion);
    const float roughness = gbuffer.material.perceptualRoughness;

    const float2 pixelCenter = float2(pixel) + 0.5f;
    const float3 worldPosition = ReconstructWorldPosition(
        g_Specular.view,
        pixelCenter,
        t_Depth[pixel]);
    const float3 receiverWorld = all(isfinite(worldPosition))
        ? worldPosition
        : float3(0.0f, 0.0f, 0.0f);
    const float3 viewDirection = -GetIncidentVector(
        g_Specular.view.cameraDirectionOrPosition,
        receiverWorld);
    PbrSurfaceInteraction interaction;
    interaction.position = receiverWorld;
    interaction.shadingNormal = gbuffer.shadingNormal;
    interaction.geometricNormal = gbuffer.geometricNormal;
    interaction.viewDirection = viewDirection;
    const PbrPreparedSurface surface = PreparePbrSurface(interaction);
    const PbrPreparedMaterial material = PreparePbrMaterial(
        gbuffer.material);
    const PbrPreparedEnvironment environment = PreparePbrEnvironment(
        gbuffer.material,
        interaction,
        g_Specular.specularEnvironmentMipLevels);
    if (environment.valid <= 0.0f)
    {
        u_ResolvedSpecular[pixel] = 0.0f;
        return;
    }

    const float3 fallbackRadiance = SpecularResolveEnvironment(
        environment.reflectionDirection,
        roughness);
    float3 numerator = 0.0f;
    float3 environmentNumerator = 0.0f;
    float denominator = 0.0f;
    float environmentWeight = 0.0f;
    uint validCount = 0u;
    float confidenceSum = 0.0f;
    float hitDistanceSum = 0.0f;
    float sourcePdfSum = 0.0f;
    float mipSum = 0.0f;
    uint diagnosticPath = SpecularHitPathEnvironment;
    const float projectionScale = SpecularProjectionPixelsPerMeter(
        receiverWorld);

    [unroll]
    for (int offsetY = -1; offsetY <= 1; ++offsetY)
    {
        [unroll]
        for (int offsetX = -1; offsetX <= 1; ++offsetX)
        {
            // The renderer defers its native specular IBL whenever this pass
            // is operational. Surfaces outside the stochastic roughness range
            // must therefore fall through to the native environment response
            // below instead of losing their specular energy.
            if (roughness > g_Specular.maxRoughness)
                continue;
            const int2 samplePixel = clamp(
                int2(pixel) + int2(offsetX, offsetY),
                int2(0, 0),
                int2(g_Specular.renderWidth - 1.0f,
                    g_Specular.renderHeight - 1.0f));
            const uint4 metadata = t_HitMetadata[samplePixel];
            if (!SpecularRecordIsValid(metadata))
                continue;
            const float4 hitRecord = t_HitUvPdfDistance[samplePixel];
            const float4 directionRecord =
                t_HitDirectionFootprint[samplePixel];
            const float3 direction = PbrSafeNormalize(
                directionRecord.xyz,
                environment.reflectionDirection);
            const float sourcePdf = hitRecord.z;
            const float confidence = t_HitConfidence[samplePixel];
            const uint path = SpecularRecordPath(metadata);
            const float environmentFraction =
                path == SpecularHitPathEnvironment
                    ? 1.0f
                    : (path == SpecularHitPathRayTracedScreen
                        ? 0.0f
                        : 1.0f - saturate(confidence));
            float selectedMip = 0.0f;
            float3 environmentContribution = 0.0f;
            float3 radiance;
            if (g_Specular.deferredColorLookup != 0u)
            {
                radiance = SpecularLoadDeferredRadiance(
                    path,
                    hitRecord.xy,
                    hitRecord.w,
                    direction,
                    directionRecord.w,
                    confidence,
                    roughness,
                    surface.shadingNoV,
                    projectionScale,
                    environmentContribution,
                    selectedMip);
            }
            else
            {
                const float4 storedRadiance =
                    t_SourceRadiance[samplePixel];
                radiance = max(storedRadiance.rgb, 0.0f);
                environmentContribution = SpecularResolveEnvironment(
                    direction,
                    saturate(storedRadiance.a)) * environmentFraction;
                selectedMip = path == SpecularHitPathEnvironment
                    ? SpecularEnvironmentMip(
                        saturate(storedRadiance.a))
                    : SpecularMipFromTraceFootprint(directionRecord.w);
            }
            if (!all(isfinite(radiance)))
                continue;

            float weight = 1.0f;
            if (g_Specular.ratioEstimator != 0u)
            {
                const float destinationGgx =
                    SpecularDestinationGgxWeight(
                        material,
                        surface,
                        direction);
                if (!(sourcePdf > 0.0f) || !isfinite(sourcePdf) ||
                    !(destinationGgx > 0.0f) ||
                    !isfinite(destinationGgx))
                {
                    continue;
                }
                weight = destinationGgx /
                    max(sourcePdf, SpecularMinimumPdf);
                if (!(weight > 0.0f) || !isfinite(weight))
                    continue;
            }
            numerator += radiance * weight;
            environmentNumerator += environmentContribution * weight;
            denominator += weight;
            environmentWeight += weight * environmentFraction;
            ++validCount;
            confidenceSum += confidence;
            hitDistanceSum += hitRecord.w;
            sourcePdfSum += max(sourcePdf, 0.0f);
            mipSum += selectedMip;
            if (validCount == 1u || path == SpecularHitPathRayTracedScreen)
                diagnosticPath = path;
        }
    }

    float3 normalizedRadiance = fallbackRadiance;
    float3 normalizedEnvironmentRadiance = fallbackRadiance;
    const bool hasNormalizedSamples = validCount > 0u &&
        all(isfinite(numerator)) &&
        all(isfinite(environmentNumerator)) &&
        isfinite(denominator) && denominator > SpecularMinimumPdf;
    if (hasNormalizedSamples)
    {
        normalizedRadiance = numerator / denominator;
        normalizedEnvironmentRadiance =
            environmentNumerator / denominator;
    }
    const float environmentShare = hasNormalizedSamples &&
            isfinite(environmentWeight)
        ? saturate(environmentWeight / denominator)
        : 1.0f;
    const float2 environmentBrdf = t_EnvironmentBrdf.SampleLevel(
        s_LinearClamp,
        float2(environment.noV, environment.perceptualRoughness),
        0.0f);
    const float3 specularResponseFactor = EvaluatePbrEnvironmentSpecular(
        environment,
        float3(1.0f, 1.0f, 1.0f),
        all(isfinite(environmentBrdf)) ? environmentBrdf : 0.0f,
        gbuffer.ambientOcclusion);
    const float3 analyticEnvironmentLobe =
        max(normalizedEnvironmentRadiance, 0.0f) *
        specularResponseFactor;
    float3 adjustedEnvironmentLobe = analyticEnvironmentLobe;
    if (g_Specular.skyEstimateKind ==
        UVSR_SKY_ESTIMATE_SCALAR_VISIBILITY)
    {
        const float scalarVisibility =
            isfinite(t_SkySpecularEstimate[pixel].r)
                ? saturate(t_SkySpecularEstimate[pixel].r)
                : 1.0f;
        adjustedEnvironmentLobe *= scalarVisibility.xxx;
    }
    else if (g_Specular.skyEstimateKind ==
        UVSR_SKY_ESTIMATE_RATIO_MODULATION)
    {
        const float3 ratioModulation = t_SkySpecularEstimate[pixel].rgb;
        adjustedEnvironmentLobe *= all(isfinite(ratioModulation))
            ? saturate(ratioModulation)
            : 1.0f;
    }
    else if (g_Specular.skyEstimateKind ==
        UVSR_SKY_ESTIMATE_FULL_STOCHASTIC_RADIANCE)
    {
        const float3 fullStochastic = t_SkySpecularEstimate[pixel].rgb;
        adjustedEnvironmentLobe = all(isfinite(fullStochastic))
            ? max(fullStochastic, 0.0f) *
                g_Specular.specularEnvironmentScale *
                PbrEnvironmentSpecularOcclusion(
                    environment.noV,
                    gbuffer.ambientOcclusion,
                    environment.perceptualRoughness)
            : 0.0f;
    }
    float3 resolved = max(normalizedRadiance, 0.0f) *
        specularResponseFactor;
    if (g_Specular.skyEstimateKind ==
            UVSR_SKY_ESTIMATE_SCALAR_VISIBILITY ||
        g_Specular.skyEstimateKind ==
            UVSR_SKY_ESTIMATE_RATIO_MODULATION)
    {
        resolved += adjustedEnvironmentLobe - analyticEnvironmentLobe;
    }
    else if (g_Specular.skyEstimateKind ==
        UVSR_SKY_ESTIMATE_FULL_STOCHASTIC_RADIANCE)
    {
        resolved += adjustedEnvironmentLobe * environmentShare -
            analyticEnvironmentLobe;
    }

    if (g_Specular.diagnosticView == 1u)
    {
        resolved = validCount > 0u
            ? (confidenceSum / float(validCount)).xxx
            : 0.0f;
    }
    else if (g_Specular.diagnosticView == 2u)
    {
        resolved = validCount > 0u
            ? saturate(
                hitDistanceSum /
                (float(validCount) * g_Specular.rayMaxDistance)).xxx
            : 0.0f;
    }
    else if (g_Specular.diagnosticView == 3u)
    {
        resolved = validCount > 0u
            ? saturate(sourcePdfSum / float(validCount)).xxx
            : 0.0f;
    }
    else if (g_Specular.diagnosticView == 4u)
    {
        resolved = validCount > 0u
            ? saturate(
                mipSum /
                (float(validCount) *
                    max(float(g_Specular.sceneColorMipLevels - 1u), 1.0f))).xxx
            : 0.0f;
    }
    else if (g_Specular.diagnosticView == 5u)
    {
        resolved = SpecularRouteDiagnostic(diagnosticPath);
    }
    u_ResolvedSpecular[pixel] = float4(
        SpecularClampHalfRadiance(resolved),
        0.0f);
}
