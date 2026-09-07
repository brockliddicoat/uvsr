#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene.h"
#include "uvsr_renderer_lighting.h"
#include "uvsr_renderer_frame.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include "gpu_capabilities.h"

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;

auto UvsrSceneViewer::EnsureDirectionalRayVisibilityPass() -> void {
        if (!m_ui.Representation.allowRayTraversal ||
            !m_ui.DirectionalShadows.enabled ||
            m_lighting->directionalRayVisibilityPass ||
            !SupportsDirectionalRayVisibility())
        {
            return;
        }
        m_lighting->directionalRayVisibilityPass =
            std::make_unique<RayVisibilityPass>(
                GetDevice(),
                m_frame->rendererShaderFactory,
                m_scene->bindlessLayout, RayVisibilityPass::Kind::Sun);
        uvsr::log::info(
            "Directional ray visibility first-use pipeline %s",
            m_lighting->directionalRayVisibilityPass->IsSupported()
                ? "available"
                : "unavailable");
    }

auto UvsrSceneViewer::EnsureRayTracedFlashlightShadowPass() -> void {
        if (!m_ui.Representation.allowRayTraversal ||
            !m_ui.Flashlight.castShadows ||
            !m_lighting->flashlight ||
            !ShouldSubmitFlashlight(m_lighting->flashlightTransition) ||
            m_lighting->rayTracedFlashlightShadowPass ||
            !HasRayTracedFlashlightShadowHardwareSupport())
        {
            return;
        }

        m_lighting->rayTracedFlashlightShadowPass =
            std::make_unique<RayVisibilityPass>(
                GetDevice(),
                m_frame->rendererShaderFactory,
                m_scene->bindlessLayout, RayVisibilityPass::Kind::Flashlight);
        uvsr::log::info(
            "Ray-traced flashlight shadow first-use pipeline %s",
            m_lighting->rayTracedFlashlightShadowPass->IsSupported()
                ? "available"
                : "unavailable");
    }

auto UvsrSceneViewer::EnsureRayTracedSkyVisibilityPass() -> void {
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
            return;
        }
        m_lighting->rayTracedSkyVisibilityPass =
            std::make_unique<RayVisibilityPass>(
                GetDevice(),
                m_frame->rendererShaderFactory,
                m_scene->bindlessLayout, RayVisibilityPass::Kind::Sky);
        uvsr::log::info(
            "Ray-traced sky visibility first-use pipeline %s",
            m_lighting->rayTracedSkyVisibilityPass->IsSupported()
                ? "available"
                : "unavailable");
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
