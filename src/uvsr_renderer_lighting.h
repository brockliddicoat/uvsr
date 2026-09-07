#pragma once

#include "ray_visibility_pass.h"
#include "flashlight.h"
#include "image_based_lighting_background_pass.h"
#include "image_based_lighting_environment.h"
#include "lighting_accumulation_pass.h"
#include "noise_texture_library.h"
#include "path_tracing_pass.h"
#include "pbr_deferred_lighting_pass.h"
#include <donut/engine/Scene.h>
#include <memory>
#include <vector>

namespace uvsr
{
    struct RendererLightingState
    {
        std::shared_ptr<donut::engine::DirectionalLight> sunLight;
        std::shared_ptr<donut::engine::SpotLight> flashlight;
        std::shared_ptr<donut::engine::SceneGraphNode> flashlightNode;
        float flashlightTransition = 0.f;
        float flashlightSwayTime = 0.f;
        donut::math::float3 flashlightAimDirection = donut::math::float3(0.f, 0.f, -1.f);
        donut::math::float3 flashlightResolvedPosition = 0.f;
        donut::math::float3 flashlightResolvedDirection = donut::math::float3(0.f, 0.f, -1.f);
        donut::math::float3 flashlightResolvedRight = donut::math::float3(1.f, 0.f, 0.f);
        donut::math::float3 flashlightDesiredPosition = 0.f;
        float flashlightCollisionRadius = 0.f;
        bool flashlightPoseValid = false;
        FlashlightSettings flashlightMotionSettings;
        donut::math::float3 flashlightCameraPosition = 0.f;
        donut::math::float3 flashlightCameraDirection = donut::math::float3(0.f, 0.f, -1.f);
        donut::math::float3 flashlightCameraUp = donut::math::float3(0.f, 1.f, 0.f);
        donut::math::float3 flashlightSubmittedPosition = 0.f;
        donut::math::float3 flashlightSubmittedDirection = donut::math::float3(0.f, 0.f, -1.f);
        donut::math::float3 flashlightSubmittedRight = donut::math::float3(1.f, 0.f, 0.f);
        bool flashlightSubmittedPoseValid = false;
        std::vector<std::shared_ptr<donut::engine::Light>> sceneLightsWithoutFlashlight;
        std::vector<std::shared_ptr<donut::engine::Light>> editableLights;
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
