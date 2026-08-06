#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/gbuffer.hlsli>
#include "pbr_gbuffer.hlsli"
#include "ray_traced_sky_visibility_cb.h"

cbuffer c_RayTracedSkyVisibility : register(b0)
{
    RayTracedSkyVisibilityConstants g_SkyVisibility;
};

RaytracingAccelerationStructure t_WorldBvh : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_GBufferMaterial : register(t2);
Texture2D<float4> t_GBufferNormals : register(t3);
Texture2DArray<float> t_BlueNoise : register(t4);

RWTexture2D<float> u_Visibility : register(u0);

static const float SkyVisibilityTwoPi = 6.28318530717958647692f;
static const float SkyVisibilityUint24Scale = 1.0f / 16777216.0f;

bool SkyVisibilityInViewport(uint2 dispatchPosition)
{
    return all(dispatchPosition <
        uint2(g_SkyVisibility.view.viewportSize));
}

int2 SkyVisibilityPixelPosition(uint2 dispatchPosition)
{
    return int2(dispatchPosition) +
        int2(g_SkyVisibility.view.viewportOrigin);
}

float SkyVisibilityRadicalInverse(uint index, uint base)
{
    float inverseBase = rcp(float(base));
    float inversePower = inverseBase;
    float result = 0.0f;
    [loop]
    while (index > 0u)
    {
        uint digit = index % base;
        result += float(digit) * inversePower;
        index /= base;
        inversePower *= inverseBase;
    }
    return result;
}

float SkyVisibilityBlueNoise(int2 pixelPosition, uint layer)
{
    int2 coordinate = pixelPosition & 63;
    return t_BlueNoise.Load(int4(
        coordinate,
        int(layer % 5u),
        0));
}

float SkyVisibilityPermutatedWhiteNoise(
    int2 pixelPosition,
    uint dimension,
    uint phase)
{
    uint state = uint(pixelPosition.x) +
        uint(pixelPosition.y) * 65537u +
        dimension * 747796405u + phase * 2891336453u + 1u;
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) *
        277803737u;
    word = (word >> 22u) ^ word;
    return float((word >> 8u) & 0x00ffffffu) *
        SkyVisibilityUint24Scale;
}

float SkyVisibilityGoldenWeylPhase(uint frameIndex)
{
    const uint phase = frameIndex * 0x9e3779b9u;
    return float(phase >> 8u) * SkyVisibilityUint24Scale;
}

float2 SkyVisibilitySample2D(
    int2 pixelPosition,
    uint sampleIndex)
{
    const uint phase = g_SkyVisibility.sampleSequencePhase;
    const uint firstDimension = sampleIndex * 2u;
    if (g_SkyVisibility.noisePattern == 0u)
    {
        return float2(
            SkyVisibilityPermutatedWhiteNoise(
                pixelPosition, firstDimension, phase),
            SkyVisibilityPermutatedWhiteNoise(
                pixelPosition, firstDimension + 1u, phase));
    }

    const uint sequenceIndex = sampleIndex + 1u;
    return frac(float2(
        SkyVisibilityRadicalInverse(sequenceIndex, 2u) +
            SkyVisibilityBlueNoise(pixelPosition, 0u) +
            SkyVisibilityRadicalInverse(phase + 1u, 5u),
        SkyVisibilityRadicalInverse(sequenceIndex, 3u) +
            SkyVisibilityBlueNoise(pixelPosition, 1u) +
            SkyVisibilityGoldenWeylPhase(phase)));
}

float3 SkyVisibilitySafeGeometricNormal(
    float3 geometricNormal,
    float3 viewDirection)
{
    float3 safeNormal = PbrSafeNormalize(
        geometricNormal,
        viewDirection);
    if (dot(safeNormal, viewDirection) < 0.0f)
        safeNormal = -safeNormal;
    return safeNormal;
}

float3 SkyVisibilitySampleCosineHemisphere(
    float3 geometricNormal,
    float2 sample)
{
    const float radialSquared = saturate(sample.x);
    const float radial = sqrt(radialSquared);
    const float phi = SkyVisibilityTwoPi * sample.y;
    const float normalDistance = sqrt(max(1.0f - radialSquared, 0.0f));
    const float3 helper = abs(geometricNormal.z) < 0.999f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent = PbrSafeNormalize(
        cross(helper, geometricNormal),
        float3(1.0f, 0.0f, 0.0f));
    const float3 bitangent = cross(geometricNormal, tangent);
    return PbrSafeNormalize(
        tangent * (cos(phi) * radial) +
            bitangent * (sin(phi) * radial) +
            geometricNormal * normalDistance,
        geometricNormal);
}

float SkyVisibilityStepDepthTowardCamera(float depth)
{
    if (g_SkyVisibility.floatDepth != 0u)
    {
        uint bits = asuint(saturate(depth));
        if (g_SkyVisibility.reverseDepth != 0u)
            bits = min(bits + 1u, asuint(1.0f));
        else
            bits = bits > 0u ? bits - 1u : 0u;
        float stepped = asfloat(bits);
        return isfinite(stepped) ? saturate(stepped) : depth;
    }

    float direction = g_SkyVisibility.reverseDepth != 0u
        ? 1.0f
        : -1.0f;
    return saturate(
        depth + direction * g_SkyVisibility.depthQuantizationStep);
}

float SkyVisibilityOffsetFloatComponent(
    float position,
    float direction)
{
    static const float Origin = 1.0f / 32.0f;
    static const float FloatScale = 1.0f / 65536.0f;
    static const float IntegerScale = 256.0f;
    int integerOffset = int(IntegerScale * direction);
    float shifted = asfloat(
        asint(position) + (position < 0.0f
            ? -integerOffset
            : integerOffset));
    return abs(position) < Origin
        ? position + FloatScale * direction
        : shifted;
}

float3 SkyVisibilityOffsetFloatPosition(
    float3 position,
    float3 direction)
{
    float3 safeDirection = PbrSafeNormalize(
        direction,
        float3(0.0f, 0.0f, 1.0f));
    return float3(
        SkyVisibilityOffsetFloatComponent(
            position.x, safeDirection.x),
        SkyVisibilityOffsetFloatComponent(
            position.y, safeDirection.y),
        SkyVisibilityOffsetFloatComponent(
            position.z, safeDirection.z));
}

float3 SkyVisibilityPrepareRayOrigin(
    float3 surfacePosition,
    float3 geometricNormal,
    float3 viewDirection,
    float2 pixelCenter,
    float depth)
{
    const float3 safeNormal = SkyVisibilitySafeGeometricNormal(
        geometricNormal,
        viewDirection);
    const float safeDepth = SkyVisibilityStepDepthTowardCamera(depth);
    float3 depthStepPosition = ReconstructWorldPosition(
        g_SkyVisibility.view,
        pixelCenter,
        safeDepth);
    const float depthStepDistance = all(isfinite(depthStepPosition))
        ? length(depthStepPosition - surfacePosition)
        : 0.0f;
    const float clearance = max(
        max(g_SkyVisibility.rayBias, 0.0f),
        depthStepDistance);
    return SkyVisibilityOffsetFloatPosition(
        surfacePosition + safeNormal * clearance,
        safeNormal);
}

bool SkyVisibilityTrace(
    float3 rayOrigin,
    float3 direction)
{
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = g_SkyVisibility.rayDistance;

    RayQuery<
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_FORCE_OPAQUE |
        RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(t_WorldBvh, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed())
    {
    }
    return query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

[numthreads(8, 8, 1)]
void Generate(uint2 dispatchPosition : SV_DispatchThreadID)
{
    if (!SkyVisibilityInViewport(dispatchPosition))
        return;
    const int2 pixelPosition =
        SkyVisibilityPixelPosition(dispatchPosition);
    const float4 normalChannels = t_GBufferNormals[pixelPosition];
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
    {
        u_Visibility[pixelPosition] = 1.0f;
        return;
    }

    const float4 packedMaterial = t_GBufferMaterial[pixelPosition];
    const PbrGBufferSurfaceNormals surfaceNormals =
        DecodePbrGBufferSurfaceNormals(
            normalChannels,
            packedMaterial);
    const float depth = t_Depth[pixelPosition];
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const float3 surfacePosition = ReconstructWorldPosition(
        g_SkyVisibility.view,
        pixelCenter,
        depth);
    const float3 viewIncident = GetIncidentVector(
        g_SkyVisibility.view.cameraDirectionOrPosition,
        surfacePosition);
    const float3 viewDirection = -viewIncident;
    const float3 geometricNormal = SkyVisibilitySafeGeometricNormal(
        surfaceNormals.geometricNormal,
        viewDirection);
    const float3 rayOrigin = SkyVisibilityPrepareRayOrigin(
        surfacePosition,
        surfaceNormals.geometricNormal,
        viewDirection,
        pixelCenter,
        depth);

    const uint sampleCount = max(g_SkyVisibility.sampleCount, 1u);
    uint visibleSampleCount = 0u;
    [loop]
    for (uint sampleIndex = 0u;
        sampleIndex < sampleCount;
        ++sampleIndex)
    {
        const float3 direction = SkyVisibilitySampleCosineHemisphere(
            geometricNormal,
            SkyVisibilitySample2D(pixelPosition, sampleIndex));
        if (SkyVisibilityTrace(rayOrigin, direction))
            ++visibleSampleCount;
    }

    u_Visibility[pixelPosition] =
        float(visibleSampleCount) / float(sampleCount);
}
