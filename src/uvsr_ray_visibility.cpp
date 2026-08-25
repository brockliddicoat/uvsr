#include "uvsr_internal.h"

auto UvsrSceneViewer::EnsureDirectionalRayVisibilityPass() -> void {
        if (!m_ui.Representation.allowRayTraversal ||
            !m_ui.DirectionalShadows.enabled ||
            m_DirectionalRayVisibilityPass ||
            !SupportsDirectionalRayVisibility())
        {
            return;
        }
        m_DirectionalRayVisibilityPass =
            std::make_unique<DirectionalRayVisibilityPass>(
                GetDevice(),
                m_RendererShaderFactory,
                m_BindlessLayout);
        uvsr::log::info(
            "Directional ray visibility first-use pipeline %s",
            m_DirectionalRayVisibilityPass->IsSupported()
                ? "available"
                : "unavailable");
    }

auto UvsrSceneViewer::EnsureRayTracedFlashlightShadowPass() -> void {
        if (!m_ui.Representation.allowRayTraversal ||
            !m_ui.Flashlight.castShadows ||
            !m_Flashlight ||
            !ShouldSubmitFlashlight(m_FlashlightTransition) ||
            m_RayTracedFlashlightShadowPass ||
            !HasRayTracedFlashlightShadowHardwareSupport())
        {
            return;
        }

        m_RayTracedFlashlightShadowPass =
            std::make_unique<RayTracedFlashlightShadowPass>(
                GetDevice(),
                m_RendererShaderFactory,
                m_BindlessLayout);
        uvsr::log::info(
            "Ray-traced flashlight shadow first-use pipeline %s",
            m_RayTracedFlashlightShadowPass->IsSupported()
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
            m_RayTracedSkyVisibilityPass ||
            !SupportsRayTracedSkyVisibility())
        {
            return;
        }
        m_RayTracedSkyVisibilityPass =
            std::make_unique<RayTracedSkyVisibilityPass>(
                GetDevice(),
                m_RendererShaderFactory,
                m_BindlessLayout);
        uvsr::log::info(
            "Ray-traced sky visibility first-use pipeline %s",
            m_RayTracedSkyVisibilityPass->IsSupported()
                ? "available"
                : "unavailable");
    }

auto UvsrSceneViewer::HasDirectionalRayVisibilityHardwareSupport() const -> bool {
        return m_BindlessLayout &&
            DirectionalRayVisibilityPass::IsDeviceSupported(GetDevice());
    }

auto UvsrSceneViewer::SupportsDirectionalRayVisibility() const -> bool {
        return HasDirectionalRayVisibilityHardwareSupport();
    }


auto UvsrSceneViewer::HasRayTracedFlashlightShadowHardwareSupport() const -> bool {
        return m_BindlessLayout &&
            RayTracedFlashlightShadowPass::IsDeviceSupported(GetDevice());
    }

auto UvsrSceneViewer::HasRayTracedSkyVisibilityHardwareSupport() const -> bool {
        return m_BindlessLayout &&
            RayTracedSkyVisibilityPass::IsDeviceSupported(GetDevice());
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
        return m_WorldSpaceRepresentation
            ? m_WorldSpaceRepresentation->GetStatus()
            : unsupported;
    }

auto UvsrSceneViewer::InvalidateWorldSpaceRepresentation(
        WorldSpaceRepresentationInvalidation invalidation) -> void {
        if (invalidation != WorldSpaceRepresentationInvalidation::None &&
            (m_DirectionalRayVisibilityPass ||
                m_RayTracedFlashlightShadowPass ||
                m_RayTracedSkyVisibilityPass))
        {
            if (m_DirectionalRayVisibilityPass)
                m_DirectionalRayVisibilityPass->ResetBindingCache();
            if (m_RayTracedFlashlightShadowPass)
                m_RayTracedFlashlightShadowPass->ResetBindingCache();
            if (m_RayTracedSkyVisibilityPass)
                m_RayTracedSkyVisibilityPass->ResetBindingCache();
            ResetImageBasedLightingHistory();
        }
        if (m_WorldSpaceRepresentation)
            m_WorldSpaceRepresentation->Invalidate(invalidation);
    }

auto UvsrSceneViewer::DidDispatchDirectionalRayVisibilityThisFrame() const -> bool {
        return m_DirectionalRayVisibilityDispatchedThisFrame;
    }

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::DidDispatchRayTracedFlashlightShadowThisFrame() const -> bool {
        return m_RayTracedFlashlightShadowDispatchedThisFrame;
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::DidDispatchShadowDenoisingThisFrame() const -> bool {
        return m_ShadowDenoisingDispatchedThisFrame;
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::DidDispatchSkyDenoisingThisFrame() const -> bool {
        return m_RayTracedSkyVisibilityDenoisedThisFrame;
    }
#endif

auto UvsrSceneViewer::DidDispatchRayTracedSkyVisibilityThisFrame() const -> bool {
        return m_RayTracedSkyVisibilityDispatchedThisFrame;
    }
