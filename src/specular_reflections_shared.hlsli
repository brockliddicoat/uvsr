#ifndef UVSR_SPECULAR_REFLECTIONS_SHARED_HLSLI
#define UVSR_SPECULAR_REFLECTIONS_SHARED_HLSLI

// The GGX VNDF sampler and hierarchical traversal used by this implementation
// are adapted from AMD FidelityFX SDK 1.1.4 SSSR and Hybrid Reflections.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <donut/shaders/gbuffer.hlsli>
#include "pbr_gbuffer.hlsli"
#include "pbr_environment.hlsli"
#include "specular_reflections_cb.h"
#include "visibility_projection_shared.h"

static const uint SpecularHitValid = 1u << 0u;
static const uint SpecularHitPathShift = 1u;
static const uint SpecularHitPathMask = 3u << SpecularHitPathShift;
static const uint SpecularHitPathScreen = 0u;
static const uint SpecularHitPathEnvironment = 1u;
static const uint SpecularHitPathRayTracedScreen = 2u;
static const float SpecularMinimumPdf = 1e-6f;
static const float SpecularFloatMaximum = 3.402823466e+38f;
static const float SpecularHalfMaximum = 65504.0f;

float3 SpecularClampHalfRadiance(float3 radiance)
{
    return all(isfinite(radiance))
        ? min(max(radiance, 0.0f), SpecularHalfMaximum)
        : 0.0f;
}

bool SpecularInViewport(uint2 pixel)
{
    return all(pixel < uint2(
        g_Specular.renderWidth,
        g_Specular.renderHeight));
}

uint SpecularPackPixel(uint2 pixel)
{
    return (pixel.x & 0xffffu) | ((pixel.y & 0xffffu) << 16u);
}

uint SpecularMakeHitFlags(uint path)
{
    return SpecularHitValid |
        ((path << SpecularHitPathShift) & SpecularHitPathMask);
}

bool SpecularRecordIsValid(uint4 metadata)
{
    return (metadata.x & SpecularHitValid) != 0u;
}

uint SpecularRecordPath(uint4 metadata)
{
    return (metadata.x & SpecularHitPathMask) >> SpecularHitPathShift;
}

float SpecularHash(uint2 pixel, uint dimension)
{
    uint state = pixel.x + pixel.y * 65537u +
        dimension * 747796405u + g_Specular.frameIndex * 2891336453u + 1u;
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) *
        277803737u;
    word = (word >> 22u) ^ word;
    return float((word >> 8u) & 0x00ffffffu) *
        (1.0f / 16777216.0f);
}

float2 SpecularRandom2D(uint2 pixel)
{
    return float2(
        SpecularHash(pixel, 0u),
        SpecularHash(pixel, 1u));
}

bool SpecularSelectRay(uint2 pixel)
{
    if (g_Specular.samplesPerQuad >= 4u)
        return true;
    const uint phase = g_Specular.frameIndex & 3u;
    const uint localIndex = (pixel.x & 1u) | ((pixel.y & 1u) << 1u);
    if (g_Specular.samplesPerQuad == 1u)
        return localIndex == phase;
    return (localIndex & 1u) == (phase & 1u);
}

float SpecularRaySelectionProbability()
{
    return float(max(g_Specular.samplesPerQuad, 1u)) * 0.25f;
}

float3 SpecularSampleGgxVndf(
    float3 viewLocal,
    float alphaX,
    float alphaY,
    float2 randomSample)
{
    const float3 stretchedView = normalize(float3(
        alphaX * viewLocal.x,
        alphaY * viewLocal.y,
        viewLocal.z));
    const float lengthSquared = dot(stretchedView.xy, stretchedView.xy);
    const float3 tangent = lengthSquared > 0.0f
        ? float3(-stretchedView.y, stretchedView.x, 0.0f) *
            rsqrt(lengthSquared)
        : float3(1.0f, 0.0f, 0.0f);
    const float3 bitangent = cross(stretchedView, tangent);
    const float radius = sqrt(randomSample.x);
    const float phi = 2.0f * UVSR_PI * randomSample.y;
    const float diskX = radius * cos(phi);
    float diskY = radius * sin(phi);
    const float blend = 0.5f * (1.0f + stretchedView.z);
    diskY = (1.0f - blend) * sqrt(max(1.0f - diskX * diskX, 0.0f)) +
        blend * diskY;
    const float3 hemisphereNormal = diskX * tangent + diskY * bitangent +
        sqrt(max(1.0f - diskX * diskX - diskY * diskY, 0.0f)) *
            stretchedView;
    return normalize(float3(
        alphaX * hemisphereNormal.x,
        alphaY * hemisphereNormal.y,
        max(hemisphereNormal.z, 0.0f)));
}

void SpecularBuildBasis(
    float3 normal,
    out float3 tangent,
    out float3 bitangent)
{
    const float3 helper = abs(normal.z) < 0.999f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(1.0f, 0.0f, 0.0f);
    tangent = PbrSafeNormalize(
        cross(helper, normal),
        float3(1.0f, 0.0f, 0.0f));
    bitangent = cross(normal, tangent);
}

float SpecularSmithG1(float normalDotView, float alpha)
{
    const float cosine = saturate(normalDotView);
    const float alphaSquared = alpha * alpha;
    const float root = sqrt(max(
        alphaSquared + (1.0f - alphaSquared) * cosine * cosine,
        0.0f));
    return 2.0f * cosine / max(cosine + root, SpecularMinimumPdf);
}

float SpecularVisibleGgxDirectionPdf(
    float3 normal,
    float3 viewDirection,
    float3 halfVector,
    float alpha)
{
    const float normalDotView = saturate(dot(normal, viewDirection));
    const float normalDotHalf = saturate(dot(normal, halfVector));
    const float distribution = D_GGX(normalDotHalf, alpha);
    // The visible-normal half-vector PDF divided by the reflection Jacobian.
    return max(
        distribution * SpecularSmithG1(normalDotView, alpha) /
            max(4.0f * normalDotView, SpecularMinimumPdf),
        SpecularMinimumPdf);
}

bool SpecularSampleReflectionDirection(
    PbrPreparedSurface surface,
    float perceptualRoughness,
    uint2 pixel,
    out float3 reflectedDirection,
    out float sourcePdf)
{
    float3 tangent;
    float3 bitangent;
    SpecularBuildBasis(surface.shadingNormal, tangent, bitangent);
    const float3 viewLocal = float3(
        dot(surface.viewDirection, tangent),
        dot(surface.viewDirection, bitangent),
        dot(surface.viewDirection, surface.shadingNormal));
    const float unbiasedAlpha = PerceptualRoughnessToAlpha(
        perceptualRoughness);
    const float proposalAlpha = max(
        unbiasedAlpha * exp2(-g_Specular.traceAndFilterBias),
        UVSR_MIN_ALPHA);
    const float3 halfLocal = SpecularSampleGgxVndf(
        viewLocal,
        proposalAlpha,
        proposalAlpha,
        SpecularRandom2D(pixel));
    const float3 halfVector = PbrSafeNormalize(
        tangent * halfLocal.x + bitangent * halfLocal.y +
            surface.shadingNormal * halfLocal.z,
        surface.shadingNormal);
    reflectedDirection = PbrSafeNormalize(
        reflect(-surface.viewDirection, halfVector),
        surface.shadingNormal);
    sourcePdf = SpecularVisibleGgxDirectionPdf(
        surface.shadingNormal,
        surface.viewDirection,
        halfVector,
        proposalAlpha) * SpecularRaySelectionProbability();
    return dot(surface.shadingNormal, reflectedDirection) > UVSR_MIN_COSINE &&
        dot(surface.geometricNormal, reflectedDirection) > UVSR_MIN_COSINE &&
        isfinite(sourcePdf) && sourcePdf > 0.0f;
}

float SpecularDestinationGgxWeight(
    PbrPreparedMaterial material,
    PbrPreparedSurface surface,
    float3 direction)
{
    const float normalDotLight = dot(surface.shadingNormal, direction);
    const float geometricDotLight = dot(surface.geometricNormal, direction);
    if (surface.shadingNoV <= UVSR_MIN_COSINE ||
        surface.geometricNoV <= UVSR_MIN_COSINE ||
        normalDotLight <= UVSR_MIN_COSINE ||
        geometricDotLight <= UVSR_MIN_COSINE)
    {
        return 0.0f;
    }
    const float3 halfVector = PbrSafeNormalize(
        surface.viewDirection + direction,
        surface.shadingNormal);
    const float distribution = D_GGX(
        saturate(dot(surface.shadingNormal, halfVector)),
        material.alpha);
    const float visibility = V_SmithGGXCorrelated(
        surface.shadingNoV,
        normalDotLight,
        material.alpha);
    // Fresnel is excluded: the preintegrated DFG factor is applied once after
    // normalized spatial reuse.
    return max(distribution * visibility * normalDotLight, 0.0f);
}

float SpecularElongation(float roughness, float normalDotView)
{
    return max(
        lerp(
            saturate(normalDotView * 2.0f),
            1.0f,
            sqrt(saturate(roughness))),
        1e-3f);
}

float SpecularProjectionPixelsPerMeter(float3 worldPosition)
{
    const float3 viewPosition = mul(
        float4(worldPosition, 1.0f),
        g_Specular.view.matWorldToView).xyz;
    return 0.5f * g_Specular.renderHeight *
        abs(g_Specular.view.matViewToClip[1][1]) /
        max(abs(viewPosition.z), 1e-3f);
}

float SpecularTraceFootprintPixels(
    float roughness,
    float hitDistance,
    float normalDotView,
    float projectionPixelsPerMeter)
{
    const float unbiasedConeTangent = max(
        saturate(roughness) * saturate(roughness),
        1e-4f) / SpecularElongation(roughness, normalDotView);
    const float traceConeTangent = unbiasedConeTangent *
        exp2(-g_Specular.traceAndFilterBias);
    const float footprint = max(hitDistance, 0.0f) *
        max(projectionPixelsPerMeter, 0.0f) * traceConeTangent;
    return isfinite(footprint)
        ? min(footprint, SpecularHalfMaximum)
        : SpecularHalfMaximum;
}

float SpecularMipFromTraceFootprint(float traceFootprintPixels)
{
    return clamp(
        log2(max(traceFootprintPixels, 1.0f)) +
            g_Specular.traceAndFilterBias,
        0.0f,
        float(max(g_Specular.sceneColorMipLevels, 1u) - 1u));
}

float SpecularEnvironmentMip(float roughness)
{
    return ImageBasedLightingReceiverMip(
        saturate(roughness),
        g_Specular.specularEnvironmentMipLevels);
}

float4 SpecularWorldToClip(float3 worldPosition)
{
    return mul(
        float4(worldPosition, 1.0f),
        g_Specular.view.matWorldToClip);
}

float3 SpecularProjectClipPosition(float4 clip, out bool valid)
{
    valid = isfinite(clip.w) && clip.w > 1e-6f;
    const float inverseW = valid ? rcp(clip.w) : 0.0f;
    const float3 ndc = clip.xyz * inverseW;
    return float3(
        ndc.x * 0.5f + 0.5f,
        0.5f - ndc.y * 0.5f,
        ndc.z);
}

float3 SpecularProjectWorldPosition(float3 worldPosition, out bool valid)
{
    return SpecularProjectClipPosition(
        SpecularWorldToClip(worldPosition),
        valid);
}

float3 SpecularProjectClippedRayEndpoint(
    float3 worldOrigin,
    float3 worldEndpoint,
    out bool valid)
{
    const float4 originClip = SpecularWorldToClip(worldOrigin);
    float4 endpointClip = SpecularWorldToClip(worldEndpoint);
    valid = all(isfinite(originClip)) && all(isfinite(endpointClip)) &&
        originClip.w > 1e-6f;
    if (!valid)
        return 0.0f;

    // Clip once in homogeneous space before dividing by W. The shared helper
    // selects z >= 0 for forward depth or z <= w for reversed depth and also
    // preserves positive W. A far endpoint may cross behind the eye while a
    // nearer screen-space segment still contains a valid hit.
    const VisibilityProjectionClipResult clip =
        ComputeVisibilityProjectionEndpointClip(
            originClip.z,
            originClip.w,
            endpointClip.z,
            endpointClip.w,
            g_Specular.reverseDepth != 0u);
    if (clip.valid == 0u)
    {
        valid = false;
        return 0.0f;
    }
    endpointClip = lerp(originClip, endpointClip, clip.endpointScale);
    return SpecularProjectClipPosition(endpointClip, valid);
}

float SpecularOffsetFloatComponent(float position, float direction)
{
    static const float Origin = 1.0f / 32.0f;
    static const float FloatScale = 1.0f / 65536.0f;
    static const float IntegerScale = 256.0f;
    const int integerOffset = int(IntegerScale * direction);
    const float shifted = asfloat(
        asint(position) + (position < 0.0f
            ? -integerOffset
            : integerOffset));
    return abs(position) < Origin
        ? position + FloatScale * direction
        : shifted;
}

float3 SpecularOffsetRayOrigin(float3 position, float3 normal, float bias)
{
    const float3 safeNormal = PbrSafeNormalize(
        normal,
        float3(0.0f, 1.0f, 0.0f));
    const float3 biased = position + safeNormal * max(bias, 0.0f);
    return float3(
        SpecularOffsetFloatComponent(biased.x, safeNormal.x),
        SpecularOffsetFloatComponent(biased.y, safeNormal.y),
        SpecularOffsetFloatComponent(biased.z, safeNormal.z));
}

#endif // UVSR_SPECULAR_REFLECTIONS_SHARED_HLSLI
