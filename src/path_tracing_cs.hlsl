#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"
#include "path_tracing_accumulation_contract.h"
#include "path_tracing_bindings.h"
#include "path_tracing_cb.h"
#include "path_tracing_miss_contract.h"

cbuffer c_PathTracing : register(UVSR_PATH_TRACING_CONSTANT_BUFFER_REGISTER)
{
    PathTracingConstants g_PathTracing;
};

RaytracingAccelerationStructure t_WorldBvh :
    register(UVSR_PATH_TRACING_WORLD_TLAS_REGISTER);
TextureCube<float4> t_Environment :
    register(UVSR_PATH_TRACING_ENVIRONMENT_REGISTER);
Texture2DArray<float> t_Noise :
    register(UVSR_PATH_TRACING_NOISE_REGISTER);
StructuredBuffer<LightConstants> t_PathTracingLights :
    register(UVSR_PATH_TRACING_LIGHTS_REGISTER);
StructuredBuffer<InstanceData> t_PathTracingInstances :
    register(UVSR_PATH_TRACING_INSTANCES_REGISTER);

RWTexture2D<float4> u_RawMean :
    register(UVSR_PATH_TRACING_RAW_MEAN_UAV_REGISTER);
RWTexture2D<uint> u_SuccessfulSampleCount :
    register(UVSR_PATH_TRACING_ACCEPTED_COUNT_UAV_REGISTER);
RWTexture2D<float4> u_Motion :
    register(UVSR_PATH_TRACING_MOTION_UAV_REGISTER);
RWTexture2D<float> u_Depth :
    register(UVSR_PATH_TRACING_DEPTH_UAV_REGISTER);
RWTexture2D<uint> u_RetryGeneration :
    register(UVSR_PATH_TRACING_RETRY_GENERATION_UAV_REGISTER);

#include "noise_sampling.hlsli"
#include "path_tracing_material.hlsli"
#include "path_tracing_sampling.hlsli"

bool PathTracingFlagIsSet(uint flag)
{
    return (g_PathTracing.flags & flag) != 0u;
}

void PathTracingGenerateCameraRay(
    uint2 pixel,
    float2 jitter,
    out float3 origin,
    out float3 direction)
{
    const float2 windowPixel = g_PathTracing.view.viewportOrigin +
        float2(pixel) + jitter;
    const float2 uv = (windowPixel - g_PathTracing.view.viewportOrigin) *
        g_PathTracing.view.viewportSizeInv;
    const float depth = PathTracingFlagIsSet(
        UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH) ? 1.0f : 0.0f;
    const float4 clipPosition = float4(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f,
        depth,
        1.0f);
    const float4 worldPositionH = mul(
        clipPosition,
        g_PathTracing.view.matClipToWorldNoOffset);
    const float3 worldPosition = worldPositionH.xyz /
        max(abs(worldPositionH.w), 1.0e-8f);
    if (g_PathTracing.view.cameraDirectionOrPosition.w > 0.0f)
    {
        origin = g_PathTracing.view.cameraDirectionOrPosition.xyz;
        direction = PbrSafeNormalize(
            worldPosition - origin,
            float3(0.0f, 0.0f, 1.0f));
    }
    else
    {
        origin = worldPosition;
        direction = PbrSafeNormalize(
            g_PathTracing.view.cameraDirectionOrPosition.xyz,
            float3(0.0f, 0.0f, 1.0f));
    }
}

float3 PathTracingFixedMisDirect(
    PathTracingSurface surface,
    float3 viewDirection,
    inout PathTracingRandomStream randomStream)
{
    if (g_PathTracing.lightCount == 0u)
        return 0.0f;
    const PathTracingDirectLightRandomDraws randoms =
        PathTracingDrawDirectLightRandoms(randomStream);
    float selectionPdf;
    const uint lightIndex = PathTracingSelectLight(
        randoms.selection,
        selectionPdf);
    return PathTracingEvaluateSelectedLightPrepared(
        surface,
        viewDirection,
        lightIndex,
        randoms.sampleSeed,
        selectionPdf);
}

struct PathTracingSample
{
    float3 radiance;
    float3 primaryPosition;
    float3 primaryPreviousPosition;
    float3 primaryGeometricNormal;
    uint primaryHit;
    uint valid;
};

PathTracingSample PathTracingIntegrate(uint2 pixel, uint2 seed)
{
    PathTracingSample result = (PathTracingSample)0;
    result.valid = 1u;
    PathTracingRandomStream cameraStream =
        PathTracingCreateRandomStream(seed, 0x43414d45u);
    PathTracingRandomStream pathStream =
        PathTracingCreateRandomStream(seed, 0x50415448u);
    float3 rayOrigin;
    float3 rayDirection;
    const PathTracingCameraRandomDraws cameraRandoms =
        PathTracingDrawCameraRandoms(cameraStream);
    const float2 cameraJitter = float2(
        cameraRandoms.jitterX,
        cameraRandoms.jitterY);
    PathTracingGenerateCameraRay(
        pixel,
        cameraJitter,
        rayOrigin,
        rayDirection);

    float3 throughput = 1.0f;
    [loop]
    for (uint bounce = 0u;
        bounce < UVSR_PATH_TRACING_BOUNCE_COUNT;
        ++bounce)
    {
        PathTracingSurface surface;
        const bool hit = PathTracingTraceSurface(
            t_WorldBvh,
            rayOrigin,
            rayDirection,
            g_PathTracing.rayBias,
            g_PathTracing.maximumRayDistance,
            bounce == 0u && g_PathTracing.previousViewValid != 0u,
            surface);
        if (!hit)
        {
            if (PathTracingMissUsesEnvironment(
                bounce,
                PathTracingFlagIsSet(
                    UVSR_PATH_TRACING_FLAG_SHOW_ENVIRONMENT_BACKGROUND)))
            {
                result.radiance += throughput *
                    PathTracingSampleEnvironment(rayDirection);
            }
            break;
        }

        if (bounce == 0u)
        {
            result.primaryPosition = surface.position;
            result.primaryPreviousPosition = surface.previousPosition;
            result.primaryGeometricNormal = surface.geometricNormal;
            result.primaryHit = 1u;
        }

        result.radiance += throughput *
            max(surface.material.emissiveColor, 0.0f);
        result.radiance += throughput * PathTracingFixedMisDirect(
            surface,
            -rayDirection,
            pathStream);

        const uint nextBounce = bounce + 1u;
        if (!PathTracingBounceSamplesBsdf(nextBounce))
            break;

        // PathTracingSampleBsdf uses one fixed balance-heuristic mixture of
        // diffuse and GGX proposals; no policy selector or alternate NEE path
        // changes the estimator.
        const PathTracingBsdfRandomDraws bsdfRandoms =
            PathTracingDrawBsdfRandoms(pathStream);
        const float3 bsdfRandom = float3(
            bsdfRandoms.branch,
            bsdfRandoms.sampleX,
            bsdfRandoms.sampleY);
        const PathTracingBsdfSample bsdf = PathTracingSampleBsdf(
            surface,
            -rayDirection,
            bsdfRandom,
            false);
        if (bsdf.valid == 0u)
            break;
        throughput *= bsdf.weight;
        if (!PathTracingThroughputIsValid(throughput))
        {
            result.valid = 0u;
            break;
        }
        float rouletteRandom = 0.0f;
        if (PathTracingRouletteRequiresRandom(nextBounce))
            rouletteRandom = PathTracingDrawRouletteRandom(pathStream);
        const PathTracingRouletteContract roulette =
            ResolvePathTracingRoulette(
                nextBounce,
                throughput,
                rouletteRandom);
        if (roulette.transportValid == 0u)
        {
            result.valid = 0u;
            break;
        }
        if (roulette.continuePath == 0u)
            break;
        throughput = roulette.throughput;
        rayOrigin = PathTracingPrepareRayOrigin(
            surface.position,
            surface.geometricNormal,
            bsdf.direction,
            g_PathTracing.rayBias);
        rayDirection = bsdf.direction;
    }
    result.valid = result.valid != 0u &&
        all(isfinite(result.radiance)) &&
        all(result.radiance >= 0.0f) ? 1u : 0u;
    return result;
}

float PathTracingDeviceDepth(float3 worldPosition)
{
    const float4 clip = mul(
        float4(worldPosition, 1.0f),
        g_PathTracing.view.matWorldToClip);
    return clip.w > 1.0e-6f && all(isfinite(clip))
        ? saturate(clip.z / clip.w)
        : 0.0f;
}

float4 PathTracingMotion(
    uint2 pixel,
    float deviceDepth,
    float3 previousWorldPosition)
{
    if (g_PathTracing.previousViewValid == 0u ||
        !all(isfinite(previousWorldPosition)))
    {
        return 0.0f;
    }
    const float4 previousClip = mul(
        float4(previousWorldPosition, 1.0f),
        g_PathTracing.previousView.matWorldToClip);
    if (!all(isfinite(previousClip)) || !(previousClip.w > 1.0e-6f))
        return 0.0f;
    const float3 previousNdc = previousClip.xyz / previousClip.w;
    if (previousNdc.z < 0.0f || previousNdc.z > 1.0f)
        return 0.0f;
    const float3 svPosition = float3(
        g_PathTracing.view.viewportOrigin + float2(pixel) + 0.5f,
        deviceDepth);
    const float3 motion = GetMotionVector(
        svPosition,
        previousWorldPosition,
        g_PathTracing.view,
        g_PathTracing.previousView);
    return all(isfinite(motion)) ? float4(motion, 1.0f) : 0.0f;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_PathTracing.dispatchExtent))
        return;

    const PathTracingAccumulationState previous =
        RepairPathTracingAccumulation(
            u_RawMean[pixel].rgb,
            u_SuccessfulSampleCount[pixel]);
    if (previous.count == UVSR_PATH_TRACING_SATURATED_SAMPLE_COUNT)
        return;

    const uint retryGeneration = u_RetryGeneration[pixel];
    const uint attemptPhase = PathTracingMakeAttemptPhase(
        previous.count,
        retryGeneration);
    const float noise = UVSRSamplePrecomputedNoise(
        t_Noise,
        g_PathTracing.noisePattern,
        pixel,
        g_PathTracing.dispatchExtent,
        attemptPhase,
        0x50545243u);
    const uint2 seed = PathTracingMakeSampleSeed(
        pixel,
        attemptPhase,
        previous.count,
        retryGeneration,
        noise);
    const PathTracingSample sample = PathTracingIntegrate(pixel, seed);
    const PathTracingAccumulationState accumulated =
        ResolvePathTracingAccumulation(
            previous,
            sample.radiance,
            sample.valid != 0u);
    const PathTracingRetryGenerationTransition retryTransition =
        ResolvePathTracingRetryGeneration(
            retryGeneration,
            accumulated.accepted);
    u_RetryGeneration[pixel] = retryTransition.generation;
    if (accumulated.publish == 0u)
        return;

    u_RawMean[pixel] = float4(
        accumulated.mean,
        1.0f);
    u_SuccessfulSampleCount[pixel] = accumulated.count;
    if (accumulated.accepted == 0u)
        return;

    if (sample.primaryHit != 0u)
    {
        const float depth = PathTracingDeviceDepth(sample.primaryPosition);
        u_Depth[pixel] = depth;
        u_Motion[pixel] = PathTracingMotion(
            pixel,
            depth,
            sample.primaryPreviousPosition);
    }
    else
    {
        u_Depth[pixel] = 0.0f;
        u_Motion[pixel] = 0.0f;
    }
}
