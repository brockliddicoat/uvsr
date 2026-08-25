#include "renderer_gpu_contract.h"
#include "renderer_pixel_readback_cb.h"
#include "denoising_cb.h"
#include "directional_ray_visibility_cb.h"
#include "path_tracing_cb.h"
#include "pbr_deferred_lighting_cb.h"
#include "ray_traced_flashlight_shadows_cb.h"
#include "ray_traced_sky_visibility_cb.h"
#include "screen_space_visibility_cb.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

#define UVSR_ASSERT_GPU_TYPE(type, expectedSize) \
    static_assert(sizeof(type) == expectedSize); \
    static_assert(alignof(type) == 4u); \
    static_assert(std::is_trivial_v<type>); \
    static_assert(std::is_standard_layout_v<type>); \
    static_assert(std::is_trivially_copyable_v<type>)
#define UVSR_ASSERT_GPU_MEMBER(type, member, expectedOffset) \
    static_assert(offsetof(type, member) == expectedOffset)

UVSR_ASSERT_GPU_TYPE(uvsr::gpu_contract::Float2, 8u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float2, x, 0u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float2, y, 4u);
UVSR_ASSERT_GPU_TYPE(uvsr::gpu_contract::Float3, 12u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float3, x, 0u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float3, y, 4u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float3, z, 8u);
UVSR_ASSERT_GPU_TYPE(uvsr::gpu_contract::Float4, 16u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float4, x, 0u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float4, y, 4u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float4, z, 8u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float4, w, 12u);
UVSR_ASSERT_GPU_TYPE(uvsr::gpu_contract::Int2, 8u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Int2, x, 0u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Int2, y, 4u);
UVSR_ASSERT_GPU_TYPE(uvsr::gpu_contract::Int4, 16u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Int4, values, 0u);
UVSR_ASSERT_GPU_TYPE(uvsr::gpu_contract::Uint2, 8u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Uint2, x, 0u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Uint2, y, 4u);
UVSR_ASSERT_GPU_TYPE(uvsr::gpu_contract::Uint3, 12u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Uint3, x, 0u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Uint3, y, 4u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Uint3, z, 8u);
UVSR_ASSERT_GPU_TYPE(uvsr::gpu_contract::Uint4, 16u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Uint4, x, 0u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Uint4, y, 4u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Uint4, z, 8u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Uint4, w, 12u);
UVSR_ASSERT_GPU_TYPE(uvsr::gpu_contract::Float3x4, 48u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float3x4, values, 0u);
UVSR_ASSERT_GPU_TYPE(uvsr::gpu_contract::Float4x4, 64u);
UVSR_ASSERT_GPU_MEMBER(uvsr::gpu_contract::Float4x4, values, 0u);

UVSR_ASSERT_GPU_TYPE(PlanarViewConstants, 720u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, matWorldToView, 0u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, matViewToClip, 64u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, matWorldToClip, 128u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, matClipToView, 192u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, matViewToWorld, 256u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, matClipToWorld, 320u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, matViewToClipNoOffset, 384u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, matWorldToClipNoOffset, 448u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, matClipToViewNoOffset, 512u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, matClipToWorldNoOffset, 576u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, viewportOrigin, 640u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, viewportSize, 648u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, viewportSizeInv, 656u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, pixelOffset, 664u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, clipToWindowScale, 672u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, clipToWindowBias, 680u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, windowToClipScale, 688u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, windowToClipBias, 696u);
UVSR_ASSERT_GPU_MEMBER(PlanarViewConstants, cameraDirectionOrPosition, 704u);

UVSR_ASSERT_GPU_TYPE(GeometryData, 64u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, numIndices, 0u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, numVertices, 4u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, indexBufferIndex, 8u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, indexOffset, 12u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, vertexBufferIndex, 16u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, positionOffset, 20u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, prevPositionOffset, 24u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, texCoord1Offset, 28u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, texCoord2Offset, 32u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, normalOffset, 36u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, tangentOffset, 40u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, curveRadiusOffset, 44u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, materialIndex, 48u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, pad0, 52u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, pad1, 56u);
UVSR_ASSERT_GPU_MEMBER(GeometryData, pad2, 60u);

UVSR_ASSERT_GPU_TYPE(InstanceData, 112u);
UVSR_ASSERT_GPU_MEMBER(InstanceData, flags, 0u);
UVSR_ASSERT_GPU_MEMBER(InstanceData, firstGeometryInstanceIndex, 4u);
UVSR_ASSERT_GPU_MEMBER(InstanceData, firstGeometryIndex, 8u);
UVSR_ASSERT_GPU_MEMBER(InstanceData, numGeometries, 12u);
UVSR_ASSERT_GPU_MEMBER(InstanceData, transform, 16u);
UVSR_ASSERT_GPU_MEMBER(InstanceData, prevTransform, 64u);

UVSR_ASSERT_GPU_TYPE(MaterialConstants, 208u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, baseOrDiffuseColor, 0u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, flags, 12u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, specularColor, 16u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, materialID, 28u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, emissiveColor, 32u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, domain, 44u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, opacity, 48u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, roughness, 52u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, metalness, 56u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, normalTextureScale, 60u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, occlusionStrength, 64u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, alphaCutoff, 68u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, transmissionFactor, 72u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, baseOrDiffuseTextureIndex, 76u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, metalRoughOrSpecularTextureIndex, 80u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, emissiveTextureIndex, 84u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, normalTextureIndex, 88u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, occlusionTextureIndex, 92u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, transmissionTextureIndex, 96u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, opacityTextureIndex, 100u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, normalTextureTransformScale, 104u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, padding1, 112u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, sssScale, 124u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, sssTransmissionColor, 128u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, sssAnisotropy, 140u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, sssScatteringColor, 144u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, hairMelanin, 156u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, hairBaseColor, 160u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, hairMelaninRedness, 172u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, hairLongitudinalRoughness, 176u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, hairAzimuthalRoughness, 180u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, hairIor, 184u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, hairCuticleAngle, 188u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, hairDiffuseReflectionTint, 192u);
UVSR_ASSERT_GPU_MEMBER(MaterialConstants, hairDiffuseReflectionWeight, 204u);

UVSR_ASSERT_GPU_TYPE(ShadowConstants, 112u);
UVSR_ASSERT_GPU_MEMBER(ShadowConstants, matWorldToUvzwShadow, 0u);
UVSR_ASSERT_GPU_MEMBER(ShadowConstants, shadowFadeScale, 64u);
UVSR_ASSERT_GPU_MEMBER(ShadowConstants, shadowFadeBias, 72u);
UVSR_ASSERT_GPU_MEMBER(ShadowConstants, shadowMapCenterUV, 80u);
UVSR_ASSERT_GPU_MEMBER(ShadowConstants, shadowFalloffDistance, 88u);
UVSR_ASSERT_GPU_MEMBER(ShadowConstants, shadowMapArrayIndex, 92u);
UVSR_ASSERT_GPU_MEMBER(ShadowConstants, shadowMapSizeTexels, 96u);
UVSR_ASSERT_GPU_MEMBER(ShadowConstants, shadowMapSizeTexelsInv, 104u);

UVSR_ASSERT_GPU_TYPE(LightConstants, 112u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, direction, 0u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, lightType, 12u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, position, 16u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, radius, 28u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, color, 32u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, intensity, 44u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, angularSizeOrInvRange, 48u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, innerAngle, 52u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, outerAngle, 56u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, outOfBoundsShadow, 60u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, shadowCascades, 64u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, perObjectShadows, 80u);
UVSR_ASSERT_GPU_MEMBER(LightConstants, shadowChannel, 96u);

UVSR_ASSERT_GPU_TYPE(LightProbeConstants, 128u);
UVSR_ASSERT_GPU_MEMBER(LightProbeConstants, diffuseScale, 0u);
UVSR_ASSERT_GPU_MEMBER(LightProbeConstants, specularScale, 4u);
UVSR_ASSERT_GPU_MEMBER(LightProbeConstants, mipLevels, 8u);
UVSR_ASSERT_GPU_MEMBER(LightProbeConstants, padding1, 12u);
UVSR_ASSERT_GPU_MEMBER(LightProbeConstants, diffuseArrayIndex, 16u);
UVSR_ASSERT_GPU_MEMBER(LightProbeConstants, specularArrayIndex, 20u);
UVSR_ASSERT_GPU_MEMBER(LightProbeConstants, padding2, 24u);
UVSR_ASSERT_GPU_MEMBER(LightProbeConstants, frustumPlanes, 32u);

UVSR_ASSERT_GPU_TYPE(DeferredLightingConstants, 6496u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, view, 0u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, shadowMapTextureSize, 720u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, enableAmbientOcclusion, 728u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, padding, 732u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, ambientColorTop, 736u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, ambientColorBottom, 752u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, numLights, 768u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, numLightProbes, 772u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, indirectDiffuseScale, 776u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, indirectSpecularScale, 780u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, randomOffset, 784u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, padding2, 792u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, noisePattern, 800u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, lights, 864u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, shadows, 2656u);
UVSR_ASSERT_GPU_MEMBER(DeferredLightingConstants, lightProbes, 4448u);

UVSR_ASSERT_GPU_TYPE(GBufferFillConstants, 1440u);
UVSR_ASSERT_GPU_MEMBER(GBufferFillConstants, view, 0u);
UVSR_ASSERT_GPU_MEMBER(GBufferFillConstants, viewPrev, 720u);

UVSR_ASSERT_GPU_TYPE(GBufferPushConstants, 28u);
UVSR_ASSERT_GPU_MEMBER(GBufferPushConstants, startInstanceLocation, 0u);
UVSR_ASSERT_GPU_MEMBER(GBufferPushConstants, startVertexLocation, 4u);
UVSR_ASSERT_GPU_MEMBER(GBufferPushConstants, positionOffset, 8u);
UVSR_ASSERT_GPU_MEMBER(GBufferPushConstants, prevPositionOffset, 12u);
UVSR_ASSERT_GPU_MEMBER(GBufferPushConstants, texCoordOffset, 16u);
UVSR_ASSERT_GPU_MEMBER(GBufferPushConstants, normalOffset, 20u);
UVSR_ASSERT_GPU_MEMBER(GBufferPushConstants, tangentOffset, 24u);

UVSR_ASSERT_GPU_TYPE(SceneVertex, 60u);
UVSR_ASSERT_GPU_MEMBER(SceneVertex, pos, 0u);
UVSR_ASSERT_GPU_MEMBER(SceneVertex, prevPos, 12u);
UVSR_ASSERT_GPU_MEMBER(SceneVertex, texCoord, 24u);
UVSR_ASSERT_GPU_MEMBER(SceneVertex, normal, 32u);
UVSR_ASSERT_GPU_MEMBER(SceneVertex, tangent, 44u);

UVSR_ASSERT_GPU_TYPE(FlashlightBeamProfile, 48u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, beamRightX, 0u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, beamRightY, 4u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, beamRightZ, 8u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, shapeExponent, 12u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, spillInnerCosine, 16u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, spillOuterCosine, 20u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, spillWeight, 24u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, hotspotWeight, 28u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, hotspotInnerCosine, 32u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, hotspotOuterCosine, 36u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, emitterRadiusMeters, 40u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfile, active, 44u);

UVSR_ASSERT_GPU_TYPE(FlashlightBeamProfileBinding, 64u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfileBinding, profile, 0u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfileBinding, lightIndex, 48u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfileBinding, padding0, 52u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfileBinding, padding1, 56u);
UVSR_ASSERT_GPU_MEMBER(FlashlightBeamProfileBinding, padding2, 60u);

UVSR_ASSERT_GPU_TYPE(DenoisingConstants, 832u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, view, 0u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, fullResolution, 720u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, denoiserResolution, 728u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, sourceResolution, 736u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, fullResolutionInv, 744u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, denoiserResolutionInv, 752u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, sourceResolutionInv, 760u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, hitDistanceNormalization, 768u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, motionScaleX, 772u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, motionScaleY, 776u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, denoisingRange, 780u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, localLightPosition, 784u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, localLightRadius, 796u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, directionalTanAngularRadius, 800u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, reverseDepth, 804u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, method, 808u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, signalType, 812u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, spatialRadius, 816u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, spatialMethod, 820u);
UVSR_ASSERT_GPU_MEMBER(DenoisingConstants, spatialPadding, 824u);

UVSR_ASSERT_GPU_TYPE(DirectionalRayVisibilityConstants, 752u);
UVSR_ASSERT_GPU_MEMBER(DirectionalRayVisibilityConstants, view, 0u);
UVSR_ASSERT_GPU_MEMBER(
    DirectionalRayVisibilityConstants, directionToLightAndDistance, 720u);
UVSR_ASSERT_GPU_MEMBER(DirectionalRayVisibilityConstants, rayBias, 736u);
UVSR_ASSERT_GPU_MEMBER(
    DirectionalRayVisibilityConstants, depthQuantizationStep, 740u);
UVSR_ASSERT_GPU_MEMBER(DirectionalRayVisibilityConstants, reverseDepth, 744u);
UVSR_ASSERT_GPU_MEMBER(DirectionalRayVisibilityConstants, floatDepth, 748u);

UVSR_ASSERT_GPU_TYPE(PathTracingConstants, 1568u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, view, 0u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, previousView, 720u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, flashlight, 1440u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, environmentScale, 1504u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, rayBias, 1508u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, maximumRayDistance, 1512u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, noisePattern, 1516u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, dispatchExtent, 1520u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, lightCount, 1528u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, flags, 1532u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, previousViewValid, 1536u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, instanceCount, 1540u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, padding0, 1544u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, padding1, 1548u);
UVSR_ASSERT_GPU_MEMBER(PathTracingConstants, rayMaterialLimits, 1552u);

UVSR_ASSERT_GPU_TYPE(PbrDeferredLightingConstants, 6576u);
UVSR_ASSERT_GPU_MEMBER(PbrDeferredLightingConstants, deferred, 0u);
UVSR_ASSERT_GPU_MEMBER(PbrDeferredLightingConstants, separateIndirect, 6496u);
UVSR_ASSERT_GPU_MEMBER(PbrDeferredLightingConstants, lightingDebugView, 6500u);
UVSR_ASSERT_GPU_MEMBER(PbrDeferredLightingConstants, visibilityDebugView, 6504u);
UVSR_ASSERT_GPU_MEMBER(
    PbrDeferredLightingConstants, skyVisibilityApplication, 6508u);
UVSR_ASSERT_GPU_MEMBER(
    PbrDeferredLightingConstants, directVisibilityLightIndices, 6512u);
UVSR_ASSERT_GPU_MEMBER(
    PbrDeferredLightingConstants, flashlightLightIndex, 6520u);
UVSR_ASSERT_GPU_MEMBER(
    PbrDeferredLightingConstants, flashlightPadding, 6524u);
UVSR_ASSERT_GPU_MEMBER(
    PbrDeferredLightingConstants, flashlightBeamProfile, 6528u);

UVSR_ASSERT_GPU_TYPE(RayTracedFlashlightShadowConstants, 832u);
UVSR_ASSERT_GPU_MEMBER(RayTracedFlashlightShadowConstants, view, 0u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedFlashlightShadowConstants, lightPositionAndRange, 720u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedFlashlightShadowConstants,
    lightDirectionAndEmitterRadius,
    736u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedFlashlightShadowConstants, beamProfile, 752u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedFlashlightShadowConstants, depthQuantizationStep, 800u);
UVSR_ASSERT_GPU_MEMBER(RayTracedFlashlightShadowConstants, rayBias, 804u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedFlashlightShadowConstants, reverseDepth, 808u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedFlashlightShadowConstants, floatDepth, 812u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedFlashlightShadowConstants, sampleSequencePhase, 816u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedFlashlightShadowConstants, sampleCount, 820u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedFlashlightShadowConstants, noisePattern, 824u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedFlashlightShadowConstants, sampleSequenceMode, 828u);

UVSR_ASSERT_GPU_TYPE(RayTracedSkyVisibilityConstants, 768u);
UVSR_ASSERT_GPU_MEMBER(RayTracedSkyVisibilityConstants, view, 0u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedSkyVisibilityConstants, sampleSequencePhase, 720u);
UVSR_ASSERT_GPU_MEMBER(RayTracedSkyVisibilityConstants, sampleCount, 724u);
UVSR_ASSERT_GPU_MEMBER(RayTracedSkyVisibilityConstants, noisePattern, 728u);
UVSR_ASSERT_GPU_MEMBER(RayTracedSkyVisibilityConstants, rayDistance, 732u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedSkyVisibilityConstants, depthQuantizationStep, 736u);
UVSR_ASSERT_GPU_MEMBER(RayTracedSkyVisibilityConstants, rayBias, 740u);
UVSR_ASSERT_GPU_MEMBER(RayTracedSkyVisibilityConstants, reverseDepth, 744u);
UVSR_ASSERT_GPU_MEMBER(RayTracedSkyVisibilityConstants, floatDepth, 748u);
UVSR_ASSERT_GPU_MEMBER(
    RayTracedSkyVisibilityConstants, sampleSequenceMode, 752u);
UVSR_ASSERT_GPU_MEMBER(RayTracedSkyVisibilityConstants, padding0, 756u);

UVSR_ASSERT_GPU_TYPE(ScreenSpaceVisibilityConstants, 848u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, view, 0u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, fullResolution, 720u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, samplingResolution, 728u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, radiusWorld, 736u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, thicknessWorld, 740u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, stepDistributionExponent, 744u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, sampleSequenceMode, 748u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, ambientStrength, 752u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, indirectDiffuseIntensity, 756u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, sampleSequencePhase, 760u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, maximumSampleCount, 764u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, sourceRadianceAvailable, 768u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, enableAmbientOcclusion, 772u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, enableIndirectDiffuse, 776u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, reverseDepth, 780u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, orthographicProjection, 784u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, resolutionScale, 788u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, noisePattern, 792u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, visibilityDebugView, 796u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, diffuseEnvironmentEnabled, 800u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, diffuseEnvironmentScale, 804u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, diffuseEnvironmentArrayIndex, 808u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, specularEnvironmentEnabled, 812u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, specularEnvironmentScale, 816u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, specularEnvironmentMipLevels, 820u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, specularEnvironmentArrayIndex, 824u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, lightingDebugView, 828u);
UVSR_ASSERT_GPU_MEMBER(
    ScreenSpaceVisibilityConstants, skyVisibilityApplication, 832u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, padding0, 836u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, padding1, 840u);
UVSR_ASSERT_GPU_MEMBER(ScreenSpaceVisibilityConstants, padding2, 844u);

static_assert(MaterialDomain_Opaque == 0);
static_assert(MaterialDomain_AlphaTested == 1);
static_assert(MaterialDomain_AlphaBlended == 2);
static_assert(MaterialDomain_Transmissive == 3);
static_assert(MaterialDomain_TransmissiveAlphaTested == 4);
static_assert(MaterialDomain_TransmissiveAlphaBlended == 5);
static_assert(MaterialFlags_UseSpecularGlossModel == 0x001);
static_assert(MaterialFlags_DoubleSided == 0x002);
static_assert(MaterialFlags_UseMetalRoughOrSpecularTexture == 0x004);
static_assert(MaterialFlags_UseBaseOrDiffuseTexture == 0x008);
static_assert(MaterialFlags_UseEmissiveTexture == 0x010);
static_assert(MaterialFlags_UseNormalTexture == 0x020);
static_assert(MaterialFlags_UseOcclusionTexture == 0x040);
static_assert(MaterialFlags_UseTransmissionTexture == 0x080);
static_assert(MaterialFlags_MetalnessInRedChannel == 0x100);
static_assert(MaterialFlags_UseOpacityTexture == 0x200);
static_assert(MaterialFlags_SubsurfaceScattering == 0x400);
static_assert(MaterialFlags_Hair == 0x800);
static_assert(InstanceFlags_CurveDisjointOrthogonalTriangleStrips == 1u);
static_assert(InstanceFlags_CurveLinearSweptSpheres == 2u);
static_assert(UVSR_LIGHT_TYPE_NONE == 0);
static_assert(UVSR_LIGHT_TYPE_DIRECTIONAL == 1);
static_assert(UVSR_LIGHT_TYPE_SPOT == 2);
static_assert(UVSR_LIGHT_TYPE_POINT == 3);
static_assert(UVSR_DEFERRED_MAX_LIGHTS == 16);
static_assert(UVSR_DEFERRED_MAX_SHADOWS == 16);
static_assert(UVSR_DEFERRED_MAX_LIGHT_PROBES == 16);
static_assert(c_SizeOfTriangleIndices == 12u);
static_assert(c_SizeOfPosition == 12u);
static_assert(c_SizeOfTexcoord == 8u);
static_assert(c_SizeOfNormal == 4u);
static_assert(UVSR_GBUFFER_SPACE_MATERIAL == 0);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_CONSTANTS == 0);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_DIFFUSE_TEXTURE == 0);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_SPECULAR_TEXTURE == 1);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_NORMAL_TEXTURE == 2);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_EMISSIVE_TEXTURE == 3);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_OCCLUSION_TEXTURE == 4);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_TRANSMISSION_TEXTURE == 5);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_OPACITY_TEXTURE == 6);
static_assert(UVSR_GBUFFER_SPACE_INPUT == 1);
static_assert(UVSR_GBUFFER_BINDING_PUSH_CONSTANTS == 1);
static_assert(UVSR_GBUFFER_BINDING_INSTANCE_BUFFER == 10);
static_assert(UVSR_GBUFFER_BINDING_VERTEX_BUFFER == 11);
static_assert(UVSR_GBUFFER_SPACE_VIEW == 2);
static_assert(UVSR_GBUFFER_BINDING_VIEW_CONSTANTS == 2);
static_assert(UVSR_GBUFFER_BINDING_MATERIAL_SAMPLER == 0);
static_assert(UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH == 1u);
static_assert(UVSR_PATH_TRACING_FLAG_SHOW_ENVIRONMENT_BACKGROUND == 2u);
static_assert(UVSR_SKY_VISIBILITY_APPLY_NEITHER == 0u);
static_assert(UVSR_SKY_VISIBILITY_APPLY_DIFFUSE_IBL == 1u);
static_assert(UVSR_SKY_VISIBILITY_APPLY_SPECULAR_IBL == 2u);
static_assert(UVSR_SKY_VISIBILITY_APPLY_BOTH_IBL == 3u);

#undef UVSR_ASSERT_GPU_MEMBER
#undef UVSR_ASSERT_GPU_TYPE

namespace
{
    bool Require(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }

    bool GeometrySerializationKnownAnswer()
    {
        const GeometryData value = {
            1u, 2u, 3, 4u,
            5, 6u, 7u, 8u,
            9u, 10u, 11u, 12u,
            13u, 14u, 15u, 16u
        };
        std::array<std::uint32_t, 16> words{};
        std::memcpy(words.data(), &value, sizeof(value));
        for (std::uint32_t index = 0; index < words.size(); ++index)
        {
            if (words[index] != index + 1u)
                return false;
        }
        return true;
    }

    bool PackedPodSerializationKnownAnswer()
    {
        const uvsr::gpu_contract::Float4 floats = {
            1.f, -2.f, 3.5f, -4.25f };
        const std::array<float, 4> expectedFloats = {
            1.f, -2.f, 3.5f, -4.25f };
        std::array<float, 4> actualFloats{};
        std::memcpy(actualFloats.data(), &floats, sizeof(floats));

        uvsr::gpu_contract::Int4 integers = { { 1, -2, 3, -4 } };
        integers[2] = 5;
        const std::array<std::int32_t, 4> expectedIntegers = {
            1, -2, 5, -4 };
        std::array<std::int32_t, 4> actualIntegers{};
        std::memcpy(actualIntegers.data(), &integers, sizeof(integers));
        return actualFloats == expectedFloats &&
            actualIntegers == expectedIntegers;
    }

    bool PushConstantSerializationKnownAnswer()
    {
        const GBufferPushConstants value = {
            0x01020304u,
            0x11121314u,
            0x21222324u,
            0x31323334u,
            0x41424344u,
            0x51525354u,
            0x61626364u
        };
        const std::array<std::uint32_t, 7> expected = {
            0x01020304u,
            0x11121314u,
            0x21222324u,
            0x31323334u,
            0x41424344u,
            0x51525354u,
            0x61626364u
        };
        std::array<std::uint32_t, 7> actual{};
        std::memcpy(actual.data(), &value, sizeof(value));
        return actual == expected;
    }

    bool NestedArrayBoundariesKnownAnswer()
    {
        DeferredLightingConstants value{};
        const auto* base = reinterpret_cast<const std::byte*>(&value);
        return reinterpret_cast<const std::byte*>(&value.lights[0]) - base ==
                864 &&
            reinterpret_cast<const std::byte*>(&value.lights[15]) - base ==
                2544 &&
            reinterpret_cast<const std::byte*>(&value.shadows[0]) - base ==
                2656 &&
            reinterpret_cast<const std::byte*>(&value.shadows[15]) - base ==
                4336 &&
            reinterpret_cast<const std::byte*>(&value.lightProbes[0]) - base ==
                4448 &&
            reinterpret_cast<const std::byte*>(&value.lightProbes[15]) - base ==
                6368;
    }
}

int main()
{
    bool ok = true;
    ok &= Require(
        PackedPodSerializationKnownAnswer(),
        "Packed GPU POD serialization changed.");
    ok &= Require(
        GeometrySerializationKnownAnswer(),
        "GeometryData serialization changed.");
    ok &= Require(
        PushConstantSerializationKnownAnswer(),
        "GBuffer push-constant serialization changed.");
    ok &= Require(
        NestedArrayBoundariesKnownAnswer(),
        "Deferred-light array boundaries changed.");
    return ok ? 0 : 1;
}
