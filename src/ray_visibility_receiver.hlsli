RaytracingAccelerationStructure t_WorldBvh : register(t0);
Texture2D<float> t_Depth : register(t1);
Texture2D<float4> t_GBufferMaterial : register(t2);
Texture2D<float4> t_GBufferNormals : register(t3);
RWTexture2D<float> u_Visibility : register(u0);



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

float RayVisibilityEvaluate(int2 pixelPosition, uint2 dispatchPosition,
    uint sampleSequencePhase, float depth, float4 normalChannels);

void RayVisibilityGenerate(uint2 dispatchPosition)
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
    float result = 1.0f;
    if (covered)
        result = RayVisibilityEvaluate(pixelPosition, dispatchPosition, sampleSequencePhase, depth, normals);
    u_Visibility[pixelPosition] = result;

}
