#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene_nvrhi.h"
#include "uvsr_renderer_lighting_nvrhi.h"
#include "uvsr_renderer_frame_nvrhi.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include "renderer_scene_encoding.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <type_traits>
#include <cstring>

using namespace donut;
using namespace donut::app;
using namespace uvsr;

namespace
{
inline void HashLightingHistoryBytes(
    uint64_t& signature,
    const void* data,
    size_t size)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t byteIndex = 0; byteIndex < size; ++byteIndex)
    {
        signature ^= uint64_t(bytes[byteIndex]);
        signature *= 1099511628211ull;
    }
}

template<typename T>
inline void HashLightingHistoryValue(
    uint64_t& signature,
    const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    HashLightingHistoryBytes(signature, &value, sizeof(value));
}

}

auto UvsrSceneViewer::UpdateImageBasedLighting(nvrhi::ICommandList* commandList) -> void {
        if (!m_lighting->imageBasedLightingEnvironment)
            return;

        constexpr float WhiteWorldIndirectReferenceScale = 4.0f;
        const bool whiteWorldEnabled =
            m_ui.WhiteWorld != WhiteWorldMode::Off;
        const bool rasterAmbientEnabled =
            m_ui.Lighting == LightingSolution::RayMarching && m_ui.EnableAmbientFill;
        m_lighting->imageBasedLightingEnvironment->Update(
            commandList,
            whiteWorldEnabled,
            whiteWorldEnabled
                ? WhiteWorldIndirectReferenceScale
                : 1.f,
            m_ui.EnvironmentExposureStops,
            rasterAmbientEnabled &&
                m_ui.EnableDiffuseIbl,
            m_ui.DiffuseIblStrength,
            rasterAmbientEnabled &&
                m_ui.EnableSpecularIbl,
            m_ui.SpecularIblStrength,
            m_ui.EnvironmentSource);
    }

auto UvsrSceneViewer::InvalidateLightingAccumulationHistory() -> void {
        ++m_lighting->lightingHistoryEpoch;
        if (m_lighting->lightingHistoryEpoch == 0u)
            m_lighting->lightingHistoryEpoch = 1u;
    }

auto UvsrSceneViewer::SynchronizeLightingAccumulationHistory(
        uint32_t width,
        uint32_t height,
        const RendererSceneLightRange& submittedLights,
        const RaySceneView& rayScene,
        bool sceneContentChanged,
        const NoiseSettings& skyNoiseSettings,
        const NoiseSettings& flashlightNoiseSettings,
        bool directionalRayVisibilitySelected,
        bool directionalRayVisibilityReady,
        bool rayTracedFlashlightShadowSelected,
        bool flashlightStochasticRequested,
        bool rayTracedFlashlightShadowReady,
        bool rayTracedSkyVisibilitySelected,
        bool skyVisibilityStochasticRequested,
        bool rayTracedSkyVisibilityReady) -> void {
        uint64_t viewSignature = 1469598103934665603ull;
        uint64_t signature = 1469598103934665603ull;

        if (m_frame->view.valid)
        {
            // exclude temporal jitter and preserve the former 9+3 affine hash layout.
            const auto& view = m_frame->view.constants;
            float worldToView[12];
            for (unsigned row = 0; row < 3; ++row)
                for (unsigned column = 0; column < 3; ++column)
                    worldToView[row * 3 + column] = view.matWorldToView.values[row * 4 + column];
            for (unsigned column = 0; column < 3; ++column)
                worldToView[9 + column] = view.matWorldToView.values[12 + column];
            const auto& viewToClip = view.matViewToClipNoOffset;
            HashLightingHistoryValue(viewSignature, worldToView);
            HashLightingHistoryValue(viewSignature, viewToClip);
        }

        HashLightingHistoryValue(signature, width);
        HashLightingHistoryValue(signature, height);
        HashLightingHistoryValue(signature, m_ui.DirectionalShadows.hardShadows);
        const uint64_t sceneIdentity = submittedLights.scene.generation;
        HashLightingHistoryValue(signature, sceneIdentity);
        HashLightingHistoryValue(signature, sceneContentChanged);
        if (sceneContentChanged)
        {
            const uint64_t contentFrame = uint64_t(GetFrameIndex());
            HashLightingHistoryValue(signature, contentFrame);
        }

        const RendererSceneHandle submittedFlashlight =
            ShouldSubmitFlashlight(m_lighting->flashlightTransition)
                ? m_lighting->flashlight
                : RendererSceneHandle{};
        const FlashlightBeamProfile flashlightProfile =
            submittedFlashlight
                ? ResolveFlashlightBeamProfile(
                    m_ui.Flashlight,
                    m_lighting->flashlightResolvedRight.x,
                    m_lighting->flashlightResolvedRight.y,
                    m_lighting->flashlightResolvedRight.z)
                : FlashlightBeamProfile{};
        HashLightingHistoryValue(signature, flashlightProfile);

        const bool rayMarchingRayTraversalSelected =
            m_ui.Lighting == LightingSolution::RayMarching &&
            (directionalRayVisibilitySelected ||
                rayTracedFlashlightShadowSelected ||
                rayTracedSkyVisibilitySelected);
        if (m_ui.Lighting == LightingSolution::PathTracing ||
            rayMarchingRayTraversalSelected)
        {
            HashLightingHistoryValue(signature, rayScene.contentRevision);
            if (!rayScene)
            {
                // Without an authoritative geometry revision, conservatively
                // invalidate every frame rather than retain stale transport.
                const uint64_t frameIdentity = uint64_t(GetFrameIndex());
                HashLightingHistoryValue(signature, frameIdentity);
            }
        }

        const uint64_t lightCount = submittedLights.Count();
        HashLightingHistoryValue(signature, lightCount);
        for (uint32_t ordinal = 0; ordinal < lightCount; ++ordinal)
        {
            const auto light = submittedLights.At(ordinal);
            LightConstants constants{};
            const bool validLight = EncodeRendererSceneLight(submittedLights.scene, light, constants).Succeeded();
            HashLightingHistoryValue(signature, validLight);
            if (!validLight)
            {
                const uint64_t failedFrame = uint64_t(GetFrameIndex());
                HashLightingHistoryValue(signature, failedFrame);
                continue;
            }
            HashLightingHistoryValue(signature, constants);
        }

        const uintptr_t environmentIdentity =
            reinterpret_cast<uintptr_t>(
                m_lighting->imageBasedLightingEnvironment
                    ? m_lighting->imageBasedLightingEnvironment->GetRadianceTexture()
                    : nullptr);
        const float environmentScale =
            m_lighting->imageBasedLightingEnvironment
                ? m_lighting->imageBasedLightingEnvironment->GetRadianceScale()
                : 0.f;
        HashLightingHistoryValue(signature, environmentIdentity);
        HashLightingHistoryValue(signature, environmentScale);
        HashLightingHistoryValue(signature, m_ui.EnvironmentSource);
        HashLightingHistoryValue(signature, m_ui.EnvironmentExposureStops);
        HashLightingHistoryValue(signature, m_ui.ShowEnvironmentBackground);
        HashLightingHistoryValue(signature, m_ui.Lighting);
        if (m_ui.Lighting == LightingSolution::RayMarching)
            HashLightingHistoryValue(signature, m_ui.AccumulateSamples);

        if ((m_ui.Lighting == LightingSolution::RayMarching &&
                m_ui.AccumulateSamples) ||
            m_ui.Lighting == LightingSolution::PathTracing)
        {
            HashLightingHistoryValue(signature, m_ui.Noise.pattern);
            HashLightingHistoryValue(signature, m_ui.Noise.resolution);
            HashLightingHistoryValue(signature, m_ui.Noise.animate);
        }

        if (m_ui.Lighting == LightingSolution::RayMarching &&
            m_ui.AccumulateSamples)
        {
            // accumulation consumes raw scene-linear frames before display processing.
            // Upstream scheduling may retain each stochastic producer's raw
            // texture. Hash every behavior-affecting field individually so
            // structure padding can never manufacture compatibility.
            const auto hashNoiseSettings =
                [&](const NoiseSettings& settings)
                {
                    HashLightingHistoryValue(signature, settings.pattern);
                    HashLightingHistoryValue(signature, settings.resolution);
                    HashLightingHistoryValue(signature, settings.animate);
                };
            hashNoiseSettings(skyNoiseSettings);
            hashNoiseSettings(flashlightNoiseSettings);

            const DirectionalShadowSettings& directional =
                m_ui.DirectionalShadows;
            HashLightingHistoryValue(signature, directional.enabled);
            HashLightingHistoryValue(signature, directional.samplesPerPixel);
            HashLightingHistoryValue(signature, directional.rayBias);
            HashLightingHistoryValue(signature, directional.maxDistance);

            const RayTracedSkyVisibilitySettings& skyVisibility =
                m_ui.RayTracedSkyVisibility;
            HashLightingHistoryValue(signature, skyVisibility.enabled);
            HashLightingHistoryValue(
                signature, skyVisibility.applyToDiffuseIbl);
            HashLightingHistoryValue(
                signature, skyVisibility.applyToSpecularIbl);
            HashLightingHistoryValue(
                signature, skyVisibility.sampleRateLog2);
            HashLightingHistoryValue(signature, skyVisibility.rayBias);
            HashLightingHistoryValue(signature, skyVisibility.maxDistance);

            const FlashlightSettings& flashlight = m_ui.Flashlight;
            HashLightingHistoryValue(signature, m_ui.FlashlightEnabled);
            HashLightingHistoryValue(signature, flashlight.realisticLens);
            HashLightingHistoryValue(
                signature, flashlight.stationaryWhenIdle);
            HashLightingHistoryValue(signature, flashlight.castShadows);
            HashLightingHistoryValue(
                signature, flashlight.peakIntensityCandela);
            HashLightingHistoryValue(signature, flashlight.rangeMeters);
            HashLightingHistoryValue(
                signature, flashlight.cameraHorizontalOffsetMeters);
            HashLightingHistoryValue(
                signature, flashlight.cameraVerticalOffsetMeters);
            HashLightingHistoryValue(
                signature, flashlight.beamSizeDegrees);
            HashLightingHistoryValue(
                signature, flashlight.angularSizeDegrees);
            HashLightingHistoryValue(
                signature, flashlight.beamRoundness);
            HashLightingHistoryValue(signature, flashlight.edgeSoftness);
            HashLightingHistoryValue(
                signature, flashlight.colorLinearRed);
            HashLightingHistoryValue(
                signature, flashlight.colorLinearGreen);
            HashLightingHistoryValue(
                signature, flashlight.colorLinearBlue);
            HashLightingHistoryValue(signature, flashlight.hotspotSize);
            HashLightingHistoryValue(
                signature, flashlight.hotspotStrength);
            HashLightingHistoryValue(signature, flashlight.swayDegrees);
            HashLightingHistoryValue(
                signature, flashlight.aimCorrectionSeconds);

            HashLightingHistoryValue(signature, m_ui.EnableAmbientFill);
            HashLightingHistoryValue(signature, m_ui.EnableDiffuseIbl);
            HashLightingHistoryValue(signature, m_ui.DiffuseIblStrength);
            HashLightingHistoryValue(signature, m_ui.EnableSpecularIbl);
            HashLightingHistoryValue(signature, m_ui.SpecularIblStrength);
            HashLightingHistoryValue(signature, m_ui.WhiteWorld);
            HashLightingHistoryValue(signature, m_ui.LightingDebugView);

            HashLightingHistoryValue(
                signature, directionalRayVisibilitySelected);
            HashLightingHistoryValue(
                signature, directionalRayVisibilityReady);
            HashLightingHistoryValue(
                signature, rayTracedFlashlightShadowSelected);
            HashLightingHistoryValue(
                signature, flashlightStochasticRequested);
            HashLightingHistoryValue(
                signature, rayTracedFlashlightShadowReady);
            HashLightingHistoryValue(
                signature, rayTracedSkyVisibilitySelected);
            HashLightingHistoryValue(
                signature, skyVisibilityStochasticRequested);
            HashLightingHistoryValue(
                signature, rayTracedSkyVisibilityReady);
            HashLightingHistoryValue(
                signature, bool(rayScene));
        }

        const bool viewChanged =
            !m_lighting->hasLightingHistorySignatures ||
            viewSignature != m_lighting->lastLightingViewSignature;
        const bool domainChanged =
            !m_lighting->hasLightingHistorySignatures ||
            signature != m_lighting->lastLightingDomainSignature;
        if (viewChanged || domainChanged)
        {
            InvalidateLightingAccumulationHistory();
            m_lighting->lastLightingViewSignature = viewSignature;
            m_lighting->lastLightingDomainSignature = signature;
            m_lighting->hasLightingHistorySignatures = true;
        }
    }

auto UvsrSceneViewer::ResetImageBasedLightingHistory() -> void {
        InvalidateLightingAccumulationHistory();
        m_lighting->hasLightingHistorySignatures = false;
        InvalidateRendererStageTiming(
            RendererTimingStage::ShadowRayDispatch);
        InvalidateRendererStageTiming(
            RendererTimingStage::SkyVisibilityRayDispatch);
        InvalidateRendererStageTiming(
            RendererTimingStage::CompleteFrame);
    }

auto UvsrSceneViewer::ResetNoiseSamplingHistory(
        bool shadows,
        bool skyVisibility,
        bool flashlight) -> void {
        InvalidateLightingAccumulationHistory();
        if (shadows)
        {
            InvalidateRendererStageTiming(
                RendererTimingStage::ShadowRayDispatch);
        }
        if (skyVisibility)
        {
            m_lighting->rayTracedSkyVisibilityPhase = 0u;
            InvalidateRendererStageTiming(
                RendererTimingStage::SkyVisibilityRayDispatch);
        }
        if (flashlight)
        {
            m_lighting->rayTracedFlashlightShadowPhase = 0u;
            InvalidateRendererStageTiming(
                RendererTimingStage::ShadowRayDispatch);
        }
        InvalidateRendererStageTiming(RendererTimingStage::CompleteFrame);
    }

auto UvsrSceneViewer::GetNoiseTextureResidentBytes() const -> uint64_t {
        return m_lighting->noiseTextureLibrary
            ? m_lighting->noiseTextureLibrary->GetResidentBytes()
            : 0u;
    }


#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::DidCommitLightingAccumulationThisFrame() const -> bool {
        return m_lighting->lightingAccumulationCommittedThisFrame;
    }
#endif
