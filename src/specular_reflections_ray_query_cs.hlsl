#pragma pack_matrix(row_major)

// Low-confidence RayQuery routing is adapted from AMD FidelityFX SDK 1.1.4
// Hybrid Reflections.
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

#include "specular_reflections_cb.h"

cbuffer c_Specular : register(b0)
{
    SpecularReflectionConstants g_Specular;
};

#include "specular_reflections_shared.hlsli"

RaytracingAccelerationStructure t_WorldBvh : register(t0);
StructuredBuffer<uint> t_LowConfidenceRays : register(t1);
Texture2D<float> t_Depth : register(t2);
Texture2D<float4> t_Material : register(t3);
Texture2D<float4> t_Normals : register(t4);
Texture2D<float4> t_SceneColorPyramid : register(t5);
TextureCubeArray<float4> t_SpecularEnvironment : register(t6);
ByteAddressBuffer t_RayCounter : register(t7);
SamplerState s_LinearClamp : register(s0);

RWTexture2D<float4> u_HitUvPdfDistance : register(u0);
RWTexture2D<float4> u_HitDirectionFootprint : register(u1);
RWTexture2D<uint4> u_HitMetadata : register(u2);
RWTexture2D<float> u_HitConfidence : register(u3);
RWTexture2D<float4> u_SourceRadiance : register(u4);

float3 SpecularRayQueryEnvironment(float3 direction, float roughness)
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

void SpecularTraceRayQuery(uint2 pixel)
{
    if (!SpecularInViewport(pixel))
        return;
    uint4 metadata = u_HitMetadata[pixel];
    if (!SpecularRecordIsValid(metadata) ||
        u_HitConfidence[pixel] >= g_Specular.screenSpaceConfidence)
    {
        return;
    }

    const float4 normalChannels = t_Normals[pixel];
    if (!(dot(normalChannels.xyz, normalChannels.xyz) > 1e-12f))
        return;
    const PbrGBufferSurfaceNormals normals =
        DecodePbrGBufferSurfaceNormals(
            normalChannels,
            t_Material[pixel]);
    const float depth = t_Depth[pixel];
    const float2 pixelCenter = float2(pixel) + 0.5f;
    const float3 worldPosition = ReconstructWorldPosition(
        g_Specular.view,
        pixelCenter,
        depth);
    const float3 viewDirection = -GetIncidentVector(
        g_Specular.view.cameraDirectionOrPosition,
        worldPosition);
    float3 geometricNormal = PbrSafeNormalize(
        normals.geometricNormal,
        viewDirection);
    if (dot(geometricNormal, viewDirection) < 0.0f)
        geometricNormal = -geometricNormal;
    const float3 rayDirection = PbrSafeNormalize(
        u_HitDirectionFootprint[pixel].xyz,
        geometricNormal);

    RayDesc ray;
    ray.Origin = SpecularOffsetRayOrigin(
        worldPosition,
        geometricNormal,
        g_Specular.rayBias);
    ray.Direction = rayDirection;
    ray.TMin = 0.0f;
    ray.TMax = g_Specular.rayMaxDistance;
    RayQuery<
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
        RAY_FLAG_FORCE_OPAQUE |
        RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(t_WorldBvh, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed())
    {
    }

    const bool committed =
        query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    const float hitDistance = committed
        ? query.CommittedRayT()
        : g_Specular.rayMaxDistance;
    const float3 worldHit = ray.Origin + rayDirection * hitDistance;
    bool projectionValid = false;
    const float3 projectedHit = SpecularProjectWorldPosition(
        worldHit,
        projectionValid);
    uint2 hitPixel = 0u;
    bool projectable = committed && projectionValid &&
        all(projectedHit.xy >= 0.0f) && all(projectedHit.xy <= 1.0f) &&
        projectedHit.z >= 0.0f && projectedHit.z <= 1.0f;
    if (projectable)
    {
        hitPixel = min(
            uint2(projectedHit.xy * float2(
                g_Specular.renderWidth,
                g_Specular.renderHeight)),
            uint2(g_Specular.renderWidth - 1.0f,
                g_Specular.renderHeight - 1.0f));
        const float visibleDepth = t_Depth[hitPixel];
        const float3 visibleWorld = ReconstructWorldPosition(
            g_Specular.view,
            float2(hitPixel) + 0.5f,
            visibleDepth);
        const float4 visibleNormalChannels = t_Normals[hitPixel];
        const bool shadeable =
            dot(visibleNormalChannels.xyz, visibleNormalChannels.xyz) >
                1e-12f;
        const float tolerance = max(
            g_Specular.rayBias +
                hitDistance * g_Specular.depthRejection,
            1e-4f);
        projectable = shadeable && all(isfinite(visibleWorld)) &&
            length(visibleWorld - worldHit) <= tolerance;
    }

    const float roughness = saturate(normalChannels.w);
    const uint path = projectable
        ? SpecularHitPathRayTracedScreen
        : SpecularHitPathEnvironment;
    const float2 hitUv = projectable ? projectedHit.xy : -1.0f;
    float4 hitRecord = u_HitUvPdfDistance[pixel];
    hitRecord.xy = hitUv;
    hitRecord.w = hitDistance;
    u_HitUvPdfDistance[pixel] = hitRecord;

    const float normalDotView = saturate(dot(
        PbrSafeNormalize(normals.shadingNormal, geometricNormal),
        viewDirection));
    float4 directionRecord = u_HitDirectionFootprint[pixel];
    directionRecord.w = SpecularTraceFootprintPixels(
        roughness,
        hitDistance,
        normalDotView,
        SpecularProjectionPixelsPerMeter(worldPosition));
    u_HitDirectionFootprint[pixel] = directionRecord;
    const uint instanceIdentity = committed
        ? query.CommittedInstanceID()
        : 0xffffffffu;
    const uint primitiveIdentity = committed
        ? ((query.CommittedGeometryIndex() & 0xffu) << 24u) |
            (query.CommittedPrimitiveIndex() & 0x00ffffffu)
        : 0xffffffffu;
    u_HitMetadata[pixel] = uint4(
        SpecularMakeHitFlags(path),
        projectable ? SpecularPackPixel(hitPixel) : 0u,
        instanceIdentity,
        primitiveIdentity);
    u_HitConfidence[pixel] = projectable ? 1.0f : 0.0f;

    if (g_Specular.deferredColorLookup == 0u)
    {
        float3 radiance = SpecularRayQueryEnvironment(
            rayDirection,
            roughness);
        if (projectable)
        {
            radiance = max(
                t_SceneColorPyramid.SampleLevel(
                    s_LinearClamp,
                    projectedHit.xy,
                    SpecularMipFromTraceFootprint(directionRecord.w)).rgb,
                0.0f);
        }
        u_SourceRadiance[pixel] = float4(
            SpecularClampHalfRadiance(radiance),
            roughness);
    }
}

[numthreads(64, 1, 1)]
void TraceCompact(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint dispatchIndex = dispatchThreadId.x;
    const uint rayCount = t_RayCounter.Load(0u);
    if (dispatchIndex >= rayCount)
        return;
    const uint linearPixel = t_LowConfidenceRays[dispatchIndex];
    const uint width = uint(g_Specular.renderWidth);
    SpecularTraceRayQuery(uint2(
        linearPixel % width,
        linearPixel / width));
}

[numthreads(8, 8, 1)]
void TraceDense(uint2 pixel : SV_DispatchThreadID)
{
    SpecularTraceRayQuery(pixel);
}
