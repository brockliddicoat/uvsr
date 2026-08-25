#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "denoising_cb.h"

#ifndef DENOISING_SIGNAL_CLASS
#define DENOISING_SIGNAL_CLASS 0
#endif

#define DENOISING_RADIANCE 0
#define DENOISING_SHADOW 1
#define DENOISING_SCALAR_RADIANCE 2

#define DENOISER_METHOD_REBLUR_RADIANCE 0
#define DENOISER_METHOD_RELAX_RADIANCE 1

#define DENOISER_SIGNAL_AMBIENT_OCCLUSION 0
#define DENOISER_SIGNAL_SKY_VISIBILITY 2
#define DENOISER_SIGNAL_SUN_SHADOW 3

cbuffer c_Denoising : register(b0)
{
    DenoisingConstants g_Denoising;
};

Texture2D<float4> t_RawSignal : register(t0);
Texture2D<float> t_HitDistance : register(t1);
Texture2D<float> t_Depth : register(t2);
Texture2D<float4> t_NormalRoughness : register(t3);
Texture2D<float4> t_MotionVectors : register(t4);

RWTexture2D<float4> u_MotionVectors : register(u0);
RWTexture2D<float4> u_NormalRoughness : register(u1);
RWTexture2D<float> u_ViewZ : register(u2);

#if DENOISING_SIGNAL_CLASS == DENOISING_RADIANCE || DENOISING_SIGNAL_CLASS == DENOISING_SCALAR_RADIANCE
RWTexture2D<float4> u_RadianceHitDistance : register(u3);
#else
RWTexture2D<float> u_Penumbra : register(u3);
#endif

static const float kFp16Maximum = 65504.0f;
static const float kEpsilon = 1e-6f;

bool IsValidDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Denoising.reverseDepth != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

float3 LinearToYCoCg(float3 color)
{
    return float3(
        dot(color, float3(0.25f, 0.5f, 0.25f)),
        dot(color, float3(0.5f, 0.0f, -0.5f)),
        dot(color, float3(-0.25f, 0.5f, -0.25f)));
}

uint2 MapSourcePixel(uint2 fullPixel)
{
    uint2 sourceSize = uint2(g_Denoising.sourceResolution);
    uint2 fullSize = uint2(g_Denoising.fullResolution);
    return min(uint2((float2(fullPixel) + 0.5f) *
        float2(sourceSize) / float2(fullSize)), sourceSize - 1u);
}

float GetReblurNormalizedHitDistance(float hitDistance, float viewZ)
{
    if (!(hitDistance > 0.0f) || !isfinite(hitDistance))
        return 0.0f;
    float denominator = g_Denoising.hitDistanceNormalization +
        abs(viewZ) * kEpsilon;
    return max(saturate(hitDistance / max(denominator, kEpsilon)), kEpsilon);
}

float PackDirectionalPenumbra(float hitDistance)
{
    if (!(hitDistance > 0.0f) || !isfinite(hitDistance))
        return 0.0f;
    if (hitDistance >= kFp16Maximum)
        return kFp16Maximum;
    return min(hitDistance * g_Denoising.directionalTanAngularRadius * 0.5f,
        32768.0f);
}

float PackLocalPenumbra(
    float hitDistance,
    float distanceToLight)
{
    if (!(hitDistance > 0.0f) || !isfinite(hitDistance))
        return 0.0f;
    if (hitDistance >= kFp16Maximum)
        return kFp16Maximum;
    float size = g_Denoising.localLightRadius * 2.0f;
    float penumbraSize = size * hitDistance /
        max(distanceToLight - hitDistance, kEpsilon);
    return min(penumbraSize * 0.5f, 32768.0f);
}

void StoreInvalid(uint2 pixel)
{
    u_MotionVectors[pixel] = 0.0f;
    u_NormalRoughness[pixel] = 0.0f;
    u_ViewZ[pixel] = 1000000.0f;
#if DENOISING_SIGNAL_CLASS == DENOISING_RADIANCE || DENOISING_SIGNAL_CLASS == DENOISING_SCALAR_RADIANCE
    u_RadianceHitDistance[pixel] = 0.0f;
#else
    u_Penumbra[pixel] = 0.0f;
#endif
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    uint2 denoiserSize = uint2(g_Denoising.denoiserResolution);
    if (any(pixel >= denoiserSize))
        return;

    uint2 fullSize = uint2(g_Denoising.fullResolution);
    uint2 footprintBegin = pixel * fullSize / denoiserSize;
    uint2 footprintEnd = (pixel + 1u) * fullSize / denoiserSize;
    footprintEnd = min(max(footprintEnd, footprintBegin + 1u), fullSize);

    uint2 guidePixel = footprintBegin;
    float guideViewZ = g_Denoising.denoisingRange;
    float3 guideNormal = 0.0f;
    float guideRoughness = 1.0f;
    bool guideValid = false;
    [loop]
    for (uint y = footprintBegin.y; y < footprintEnd.y; ++y)
    {
        [loop]
        for (uint x = footprintBegin.x; x < footprintEnd.x; ++x)
        {
            uint2 candidate = uint2(x, y);
            float depth = t_Depth[candidate];
            float4 guide = t_NormalRoughness[candidate];
            float normalLengthSquared = dot(guide.xyz, guide.xyz);
            if (!IsValidDepth(depth) ||
                !(normalLengthSquared > 1e-12f) ||
                !isfinite(normalLengthSquared))
            {
                continue;
            }
            float3 positionVS = ReconstructViewPosition(
                g_Denoising.view, float2(candidate) + 0.5f, depth);
            float viewZ = abs(positionVS.z);
            if (!isfinite(viewZ) || !(viewZ > 0.0f) ||
                !(viewZ < guideViewZ))
            {
                continue;
            }
            guidePixel = candidate;
            guideViewZ = viewZ;
            guideNormal = guide.xyz * rsqrt(normalLengthSquared);
            guideRoughness = saturate(guide.w);
            guideValid = true;
        }
    }

    if (!guideValid)
    {
        StoreInvalid(pixel);
        return;
    }

    float3 signalSum = 0.0f;
    float hitDistanceSum = 0.0f;
    float3 normalSum = 0.0f;
    float roughnessSum = 0.0f;
    float viewZSum = 0.0f;
    float4 motionSum = 0.0f;
    float sampleCount = 0.0f;
    float depthTolerance = max(guideViewZ * 0.02f, 0.01f);
    [loop]
    for (uint y = footprintBegin.y; y < footprintEnd.y; ++y)
    {
        [loop]
        for (uint x = footprintBegin.x; x < footprintEnd.x; ++x)
        {
            uint2 candidate = uint2(x, y);
            float depth = t_Depth[candidate];
            float4 guide = t_NormalRoughness[candidate];
            float normalLengthSquared = dot(guide.xyz, guide.xyz);
            if (!IsValidDepth(depth) ||
                !(normalLengthSquared > 1e-12f) ||
                !isfinite(normalLengthSquared))
            {
                continue;
            }
            float3 positionVS = ReconstructViewPosition(
                g_Denoising.view, float2(candidate) + 0.5f, depth);
            float viewZ = abs(positionVS.z);
            float3 normal = guide.xyz * rsqrt(normalLengthSquared);
            if (!isfinite(viewZ) || !(viewZ > 0.0f) ||
                abs(viewZ - guideViewZ) > depthTolerance ||
                dot(normal, guideNormal) < 0.9f)
            {
                continue;
            }

            uint2 sourcePixel = MapSourcePixel(candidate);
            float3 signal = t_RawSignal[sourcePixel].rgb;
            if (g_Denoising.signalType == DENOISER_SIGNAL_AMBIENT_OCCLUSION ||
                g_Denoising.signalType == DENOISER_SIGNAL_SKY_VISIBILITY)
                signal = signal.xxx;
            if (any(!isfinite(signal)))
                signal = 0.0f;
            float hitDistance = t_HitDistance[sourcePixel];
            if (!isfinite(hitDistance) || hitDistance < 0.0f)
                hitDistance = 0.0f;
            float4 motion = t_MotionVectors[candidate];
            if (any(!isfinite(motion)))
                motion = 0.0f;

            signalSum += clamp(signal, 0.0f, kFp16Maximum);
            hitDistanceSum += min(hitDistance, kFp16Maximum);
            normalSum += normal;
            float linearRoughness = saturate(guide.w);
            roughnessSum += linearRoughness;
            viewZSum += viewZ;
            motionSum += motion;
            sampleCount += 1.0f;
        }
    }

    if (!(sampleCount > 0.0f))
    {
        uint2 sourcePixel = MapSourcePixel(guidePixel);
        signalSum = max(t_RawSignal[sourcePixel].rgb, 0.0f);
        if (g_Denoising.signalType == DENOISER_SIGNAL_AMBIENT_OCCLUSION ||
            g_Denoising.signalType == DENOISER_SIGNAL_SKY_VISIBILITY)
            signalSum = signalSum.xxx;
        hitDistanceSum = max(t_HitDistance[sourcePixel], 0.0f);
        normalSum = guideNormal;
        roughnessSum = guideRoughness;
        viewZSum = guideViewZ;
        motionSum = t_MotionVectors[guidePixel];
        sampleCount = 1.0f;
    }

    float inverseCount = rcp(sampleCount);
    float3 signal = signalSum * inverseCount;
    float hitDistance = hitDistanceSum * inverseCount;
    float normalLengthSquared = dot(normalSum, normalSum);
    float3 normal = normalLengthSquared > 1e-12f
        ? normalSum * rsqrt(normalLengthSquared)
        : guideNormal;
    float roughness = saturate(roughnessSum * inverseCount);
    float viewZ = viewZSum * inverseCount;
    float4 motion = motionSum * inverseCount;
    motion.xy *= float2(
        g_Denoising.motionScaleX,
        g_Denoising.motionScaleY);
    motion.zw = 0.0f;
    if (any(!isfinite(motion)))
        motion = 0.0f;

    u_MotionVectors[pixel] = motion;
    u_NormalRoughness[pixel] = float4(normal, roughness);
    u_ViewZ[pixel] = viewZ;

#if DENOISING_SIGNAL_CLASS == DENOISING_RADIANCE || DENOISING_SIGNAL_CLASS == DENOISING_SCALAR_RADIANCE
    if (g_Denoising.method == DENOISER_METHOD_RELAX_RADIANCE)
    {
        u_RadianceHitDistance[pixel] = float4(
            min(max(signal, 0.0f), kFp16Maximum),
            min(max(hitDistance, 0.0f), kFp16Maximum));
    }
    else
    {
        u_RadianceHitDistance[pixel] = float4(
            LinearToYCoCg(min(max(signal, 0.0f), kFp16Maximum)),
            GetReblurNormalizedHitDistance(hitDistance, viewZ));
    }
#else
    // Use the selected source pixel's nearest physical blocker instead of
    // averaging blocker and miss sentinels. Sun visibility is one stochastic
    // observation; a finite flashlight emitter can aggregate four rays while
    // retaining the nearest blocker distance for SIGMA.
    float shadowHitDistance = t_HitDistance[MapSourcePixel(guidePixel)];
    if (g_Denoising.signalType == DENOISER_SIGNAL_SUN_SHADOW)
    {
        u_Penumbra[pixel] = PackDirectionalPenumbra(shadowHitDistance);
    }
    else
    {
        float depth = t_Depth[guidePixel];
        float3 worldPosition = ReconstructWorldPosition(
            g_Denoising.view, float2(guidePixel) + 0.5f, depth);
        float distanceToLight = distance(
            worldPosition, g_Denoising.localLightPosition);
        u_Penumbra[pixel] = all(isfinite(worldPosition)) &&
            isfinite(distanceToLight) && distanceToLight > 0.0f
            ? PackLocalPenumbra(shadowHitDistance, distanceToLight)
            : 0.0f;
    }
#endif
}
