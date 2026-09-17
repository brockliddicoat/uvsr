#pragma once

#include "ray_visibility_pass_nvrhi.h"
#include "flashlight.h"
#include "image_based_lighting_background_pass_nvrhi.h"
#include "image_based_lighting_environment_nvrhi.h"
#include "lighting_accumulation_pass_nvrhi.h"
#include "noise_texture_library_nvrhi.h"
#include "path_tracing_pass_nvrhi.h"
#include "pbr_deferred_lighting_pass_nvrhi.h"
#include "renderer_vector_math.h"
#include <memory>

namespace uvsr
{
    struct RendererLightingState
    {
        RendererSceneHandle sunLight;
        RendererSceneHandle flashlight;
        float flashlightTransition = 0.f;
        float flashlightSwayTime = 0.f;
        uvsr::gpu_contract::Float3 flashlightAimDirection = uvsr::gpu_contract::Float3{0.f, 0.f, -1.f};
        uvsr::gpu_contract::Float3 flashlightResolvedPosition{};
        uvsr::gpu_contract::Float3 flashlightResolvedDirection = uvsr::gpu_contract::Float3{0.f, 0.f, -1.f};
        uvsr::gpu_contract::Float3 flashlightResolvedRight = uvsr::gpu_contract::Float3{1.f, 0.f, 0.f};
        uvsr::gpu_contract::Float3 flashlightDesiredPosition{};
        float flashlightCollisionRadius = 0.f;
        bool flashlightPoseValid = false;
        FlashlightSettings flashlightMotionSettings;
        uvsr::gpu_contract::Float3 flashlightCameraPosition{};
        uvsr::gpu_contract::Float3 flashlightCameraDirection = uvsr::gpu_contract::Float3{0.f, 0.f, -1.f};
        uvsr::gpu_contract::Float3 flashlightCameraUp = uvsr::gpu_contract::Float3{0.f, 1.f, 0.f};
        uvsr::gpu_contract::Float3 flashlightSubmittedPosition{};
        uvsr::gpu_contract::Float3 flashlightSubmittedDirection = uvsr::gpu_contract::Float3{0.f, 0.f, -1.f};
        uvsr::gpu_contract::Float3 flashlightSubmittedRight = uvsr::gpu_contract::Float3{1.f, 0.f, 0.f};
        bool flashlightSubmittedPoseValid = false;
        std::unique_ptr<PbrDeferredLightingPass> pbrDeferredLightingPass;
        std::unique_ptr<ImageBasedLightingEnvironment> imageBasedLightingEnvironment;
        std::unique_ptr<ImageBasedLightingBackgroundPass> imageBasedLightingBackgroundPass;
        std::unique_ptr<RayVisibilityPass> directionalRayVisibilityPass;
        std::unique_ptr<RayVisibilityPass> rayTracedFlashlightShadowPass;
        std::unique_ptr<RayVisibilityPass> rayTracedSkyVisibilityPass;
        std::unique_ptr<PathTracingPass> pathTracingPass;
        std::unique_ptr<LightingAccumulationPass> lightingAccumulationPass;
        std::unique_ptr<NoiseTextureLibrary> noiseTextureLibrary;
        uint64_t lightingHistoryEpoch = 1u;
        uint64_t lastLightingViewSignature = 0u;
        uint64_t lastLightingDomainSignature = 0u;
        bool hasLightingHistorySignatures = false;
        bool directionalRayVisibilityDispatchedThisFrame = false;
        bool pathTransportDispatchedThisFrame = false;
        bool reportedPathTransportFailure = false;
        SelectedLightingTransportState selectedLightingTransportState = SelectedLightingTransportState::RayMarching;
        bool rayTracedFlashlightShadowDispatchedThisFrame = false;
#if defined(UVSR_BUILD_TESTING)
        bool flashlightLightingSubmittedThisFrame = false;
#endif
        bool rayTracedFlashlightShadowContributedLastFrame = false;
        uint64_t rayTracedFlashlightShadowPhase = 0u;
        uint64_t rayTracedSkyVisibilityPhase = 0u;
        bool rayTracedSkyVisibilityContributedLastFrame = false;
        bool rayTracedSkyVisibilityDispatchedThisFrame = false;
#if defined(UVSR_BUILD_TESTING)
        bool lightingAccumulationCommittedThisFrame = false;
#endif
    };
}
