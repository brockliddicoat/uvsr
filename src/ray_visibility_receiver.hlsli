RaytracingAccelerationStructure t_WorldBvh : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_GBufferMaterial : register(t2);
Texture2D<float4> t_GBufferNormals : register(t3);
RWTexture2D<float> u_Visibility : register(u0);
#if RAY_VISIBILITY_HIT_DISTANCE
RWTexture2D<float> u_HitDistance : register(u1);
#endif



#if RAY_VISIBILITY_STOCHASTIC
Texture2DArray<float> t_Noise : register(t4);
Texture2D<uint> t_AttemptMask : register(t5);

float3 RayVisibilityPrepareRayOrigin(float3 surfacePosition,
    float3 geometricNormal, float3 viewDirection, float2 pixelCenter, float depth)
{
    const float3 safeNormal = RayOriginOrientGeometricNormal(geometricNormal, viewDirection);
    const float safeDepth = RayOriginStepDepthTowardCamera(depth,
        RAY_VISIBILITY_CONSTANTS.floatDepth != 0u,
        RAY_VISIBILITY_CONSTANTS.reverseDepth != 0u,
        RAY_VISIBILITY_CONSTANTS.depthQuantizationStep);
    const float3 depthStepPosition = ReconstructWorldPosition(
        RAY_VISIBILITY_CONSTANTS.view, pixelCenter, safeDepth);
    const float depthStepDistance = all(isfinite(depthStepPosition))
        ? length(depthStepPosition - surfacePosition) : 0.0f;
    const float clearance = ResolveRayOriginClearance(
        RAY_VISIBILITY_CONSTANTS.rayBias, depthStepDistance);
    return ResolveRayOriginPosition(surfacePosition, safeNormal, clearance);
}
#endif

float2 RayVisibilityEvaluate(int2 pixelPosition, uint2 dispatchPosition,
    uint sampleSequencePhase, float depth, float4 normalChannels);

#include "denoising_replay_sampling.hlsli"
[numthreads(8, 8, 1)]
void GenerateReplay(uint2 tile : SV_DispatchThreadID)
{
    uint2 source;
    if (!ReplaySelect(tile, source)) return;
    float depth = t_Depth[source];
    float4 normal = t_GBufferNormals[source];
    float2 result = RayVisibilityEvaluate(int2(source), source, g_ReplaySampling.phase, depth, normal);
    // visibility is stored as R8_UNORM. match that quantization before taking
    // the difference so an unchanged scene yields zero, including sample means.
    float value = round(saturate(result.x) * 255.0f) / 255.0f;
    u_ReplayGradient[tile] = ReplayDifference(t_ReplayPreviousSignal[source].xxx, value.xxx);
}

void RayVisibilityGenerate(uint2 dispatchPosition, bool outputHitDistance)
{
    if (any(dispatchPosition >= uint2(RAY_VISIBILITY_CONSTANTS.view.viewportSize)))
        return;
    const int2 pixelPosition = int2(dispatchPosition) + int2(RAY_VISIBILITY_CONSTANTS.view.viewportOrigin);
    uint sampleSequencePhase = 0u;
#if RAY_VISIBILITY_STOCHASTIC
    const bool sampleScheduleEnabled = UvsrSampleScheduleEnabled(RAY_VISIBILITY_CONSTANTS.sampleSequenceMode);
    const uint attemptToken = sampleScheduleEnabled ? t_AttemptMask[pixelPosition] : 0u;
    if (sampleScheduleEnabled && attemptToken == 0u)
        return;
    sampleSequencePhase = UvsrResolveSampleSequencePhase(
        RAY_VISIBILITY_CONSTANTS.sampleSequenceMode, attemptToken, RAY_VISIBILITY_CONSTANTS.sampleSequencePhase);
#endif

    const float depth = t_Depth[pixelPosition];
    const float4 normals = t_GBufferNormals[pixelPosition];
    const bool covered = isfinite(depth) && depth > 0.0f && dot(normals.xyz, normals.xyz) > 1e-12f;
    float2 result = float2(1.0f, RAY_VISIBILITY_EMPTY_HIT_DISTANCE);
    if (covered)
        result = RayVisibilityEvaluate(pixelPosition, dispatchPosition, sampleSequencePhase, depth, normals);
    u_Visibility[pixelPosition] = result.x;
#if RAY_VISIBILITY_HIT_DISTANCE
    if (outputHitDistance)
        u_HitDistance[pixelPosition] = result.y;
#endif

}
