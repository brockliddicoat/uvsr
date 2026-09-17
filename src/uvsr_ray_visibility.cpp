#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene_nvrhi.h"
#include "uvsr_renderer_lighting_nvrhi.h"
#include "uvsr_renderer_frame_nvrhi.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>
#include "gpu_capabilities.h"

using namespace donut;
using namespace donut::app;
using namespace uvsr;

auto UvsrSceneViewer::EnsureDirectionalRayVisibilityPass() -> bool {
        if (!m_ui.Representation.allowRayTraversal ||
            !m_ui.DirectionalShadows.enabled ||
            m_lighting->directionalRayVisibilityPass ||
            !SupportsDirectionalRayVisibility())
        {
            return true;
        }
        std::unique_ptr<RayVisibilityPass> candidate(new (std::nothrow) RayVisibilityPass(
                GetDevice(),
                m_frame->rendererShaderFactory.get(),
                m_scene->bindlessLayout, RayVisibilityPass::Kind::Sun));
        if (!candidate)
            return FailRender("Directional ray visibility allocation failed");
        if (!candidate->IsSupported())
            return FailRender("Required renderer pass failed: directional ray visibility");
        m_lighting->directionalRayVisibilityPass = std::move(candidate);
        uvsr::log::info(
            "Directional ray visibility first-use pipeline %s",
            m_lighting->directionalRayVisibilityPass->IsSupported()
                ? "available"
                : "unavailable");
        return true;
    }

auto UvsrSceneViewer::EnsureRayTracedFlashlightShadowPass() -> bool {
        if (!m_ui.Representation.allowRayTraversal ||
            !m_ui.Flashlight.castShadows ||
            !m_lighting->flashlight ||
            !ShouldSubmitFlashlight(m_lighting->flashlightTransition) ||
            m_lighting->rayTracedFlashlightShadowPass ||
            !HasRayTracedFlashlightShadowHardwareSupport())
        {
            return true;
        }

        std::unique_ptr<RayVisibilityPass> candidate(new (std::nothrow) RayVisibilityPass(
                GetDevice(),
                m_frame->rendererShaderFactory.get(),
                m_scene->bindlessLayout, RayVisibilityPass::Kind::Flashlight));
        if (!candidate)
            return FailRender("Ray-traced flashlight shadow allocation failed");
        if (!candidate->IsSupported())
            return FailRender("Required renderer pass failed: ray-traced flashlight visibility");
        m_lighting->rayTracedFlashlightShadowPass = std::move(candidate);
        uvsr::log::info(
            "Ray-traced flashlight shadow first-use pipeline %s",
            m_lighting->rayTracedFlashlightShadowPass->IsSupported()
                ? "available"
                : "unavailable");
        return true;
    }

auto UvsrSceneViewer::EnsureRayTracedSkyVisibilityPass() -> bool {
        const bool debugSelected =
            m_ui.Lighting == LightingSolution::RayMarching &&
            m_ui.LightingDebugView ==
                PbrLightingDebugView::SkyVisibility;
        if (!m_ui.Representation.allowRayTraversal ||
            !m_ui.RayTracedSkyVisibility.enabled ||
            (!HasRayTracedSkyVisibilityConsumer(
                    m_ui.RayTracedSkyVisibility) &&
                !debugSelected) ||
            m_lighting->rayTracedSkyVisibilityPass ||
            !SupportsRayTracedSkyVisibility())
        {
            return true;
        }
        std::unique_ptr<RayVisibilityPass> candidate(new (std::nothrow) RayVisibilityPass(
                GetDevice(),
                m_frame->rendererShaderFactory.get(),
                m_scene->bindlessLayout, RayVisibilityPass::Kind::Sky));
        if (!candidate)
            return FailRender("Ray-traced sky visibility allocation failed");
        if (!candidate->IsSupported())
            return FailRender("Required renderer pass failed: ray-traced sky visibility");
        m_lighting->rayTracedSkyVisibilityPass = std::move(candidate);
        uvsr::log::info(
            "Ray-traced sky visibility first-use pipeline %s",
            m_lighting->rayTracedSkyVisibilityPass->IsSupported()
                ? "available"
                : "unavailable");
        return true;
    }

auto UvsrSceneViewer::HasDirectionalRayVisibilityHardwareSupport() const -> bool {
        return m_scene->bindlessLayout &&
            RayVisibilityPass::IsDeviceSupported(GetDevice(), RayVisibilityPass::Kind::Sun);
    }

auto UvsrSceneViewer::SupportsDirectionalRayVisibility() const -> bool {
        return HasDirectionalRayVisibilityHardwareSupport();
    }


auto UvsrSceneViewer::HasRayTracedFlashlightShadowHardwareSupport() const -> bool {
        return m_scene->bindlessLayout &&
            RayVisibilityPass::IsDeviceSupported(GetDevice(), RayVisibilityPass::Kind::Flashlight);
    }

auto UvsrSceneViewer::HasRayTracedSkyVisibilityHardwareSupport() const -> bool {
        return m_scene->bindlessLayout &&
            RayVisibilityPass::IsDeviceSupported(GetDevice(), RayVisibilityPass::Kind::Sky);
    }

auto UvsrSceneViewer::SupportsRayTracedSkyVisibility() const -> bool {
        return HasRayTracedSkyVisibilityHardwareSupport();
    }

auto UvsrSceneViewer::GetWorldSpaceRepresentationStatus() const -> const WorldSpaceRepresentationStatus& {
        static const WorldSpaceRepresentationStatus unsupported = []
        {
            WorldSpaceRepresentationStatus status;
            status.state = WorldSpaceRepresentationState::Unsupported;
            return status;
        }();
        return m_scene->worldSpaceRepresentation
            ? m_scene->worldSpaceRepresentation->GetStatus()
            : unsupported;
    }

auto UvsrSceneViewer::DidDispatchDirectionalRayVisibilityThisFrame() const -> bool {
        return m_lighting->directionalRayVisibilityDispatchedThisFrame;
    }

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::DidDispatchRayTracedFlashlightShadowThisFrame() const -> bool {
        return m_lighting->rayTracedFlashlightShadowDispatchedThisFrame;
    }
#endif

#if defined(UVSR_BUILD_TESTING)

#endif

#if defined(UVSR_BUILD_TESTING)

#endif

auto UvsrSceneViewer::DidDispatchRayTracedSkyVisibilityThisFrame() const -> bool {
        return m_lighting->rayTracedSkyVisibilityDispatchedThisFrame;
    }
