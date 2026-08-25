#include "uvsr_internal.h"

auto UvsrSceneViewer::UpdateImageBasedLighting(nvrhi::ICommandList* commandList) -> void {
        if (!m_ImageBasedLightingEnvironment)
            return;

        constexpr float WhiteWorldIndirectReferenceScale = 4.0f;
        const bool whiteWorldEnabled =
            m_ui.WhiteWorld != WhiteWorldMode::Off;
        m_ImageBasedLightingEnvironment->Update(
            commandList,
            whiteWorldEnabled,
            whiteWorldEnabled
                ? WhiteWorldIndirectReferenceScale
                : 1.f,
            m_ui.EnvironmentExposureStops,
            m_ui.EnableAmbientFill &&
                m_ui.EnableDiffuseIbl,
            m_ui.DiffuseIblStrength,
            m_ui.EnableAmbientFill &&
                m_ui.EnableSpecularIbl,
            m_ui.SpecularIblStrength,
            m_ui.EnvironmentSource);
    }

auto UvsrSceneViewer::InvalidateLightingAccumulationHistory() -> void {
        ++m_LightingHistoryEpoch;
        if (m_LightingHistoryEpoch == 0u)
            m_LightingHistoryEpoch = 1u;
    }

auto UvsrSceneViewer::SynchronizeLightingAccumulationHistory(
        uint32_t width,
        uint32_t height,
        const std::vector<std::shared_ptr<Light>>& submittedLights,
        bool worldRepresentationReady,
        bool sceneContentChanged,
        const NoiseSettings& visibilityNoiseSettings,
        const NoiseSettings& skyNoiseSettings,
        const NoiseSettings& flashlightNoiseSettings,
        bool screenSpaceVisibilityRequested,
        bool screenSpaceVisibilityReady,
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

        if (m_View)
        {
            // Exclude temporal jitter. Path accumulation is invalidated by a
            // physical camera change, not by presentation-only sample offsets.
            const dm::affine3 worldToView = m_View->GetViewMatrix();
            const dm::float4x4 viewToClip =
                m_View->GetProjectionMatrix(false);
            HashLightingHistoryValue(viewSignature, worldToView);
            HashLightingHistoryValue(viewSignature, viewToClip);
        }

        HashLightingHistoryValue(signature, width);
        HashLightingHistoryValue(signature, height);
        const uintptr_t sceneIdentity =
            reinterpret_cast<uintptr_t>(m_Scene.get());
        HashLightingHistoryValue(signature, sceneIdentity);
        HashLightingHistoryValue(signature, sceneContentChanged);
        if (sceneContentChanged)
        {
            const uint64_t contentFrame = uint64_t(GetFrameIndex());
            HashLightingHistoryValue(signature, contentFrame);
        }

        const SpotLight* submittedFlashlight =
            ShouldSubmitFlashlight(m_FlashlightTransition)
                ? m_Flashlight.get()
                : nullptr;
        const FlashlightBeamProfile flashlightProfile =
            submittedFlashlight
                ? ResolveFlashlightBeamProfile(
                    m_ui.Flashlight,
                    m_FlashlightResolvedRight.x,
                    m_FlashlightResolvedRight.y,
                    m_FlashlightResolvedRight.z)
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
            const WorldSpaceRepresentationStatus* worldStatus =
                m_WorldSpaceRepresentation
                    ? &m_WorldSpaceRepresentation->GetStatus()
                    : nullptr;
            const uint64_t worldContentRevision = worldStatus
                ? worldStatus->contentRevision
                : 0u;
            HashLightingHistoryValue(signature, worldContentRevision);
            if (!worldRepresentationReady)
            {
                // Without an authoritative geometry revision, conservatively
                // invalidate every frame rather than retain stale transport.
                const uint64_t frameIdentity = uint64_t(GetFrameIndex());
                HashLightingHistoryValue(signature, frameIdentity);
            }
        }

        const uint64_t lightCount = uint64_t(submittedLights.size());
        HashLightingHistoryValue(signature, lightCount);
        for (const std::shared_ptr<Light>& light : submittedLights)
        {
            const bool validLight = bool(light);
            HashLightingHistoryValue(signature, validLight);
            if (!light)
                continue;

            LightConstants constants;
            // FillLightConstants writes only fields relevant to the concrete
            // light type. Clear every lane before hashing so static lights
            // produce a stable renderer-wide history signature.
            std::memset(&constants, 0, sizeof(constants));
            light->FillLightConstants(constants);
            HashLightingHistoryValue(signature, constants);
        }

        const uintptr_t environmentIdentity =
            reinterpret_cast<uintptr_t>(
                m_ImageBasedLightingEnvironment
                    ? m_ImageBasedLightingEnvironment->GetRadianceTexture()
                    : nullptr);
        const float environmentScale =
            m_ImageBasedLightingEnvironment
                ? m_ImageBasedLightingEnvironment->GetRadianceScale()
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
            // Accumulation owns long-term temporal history and consumes raw
            // scene-linear frames. Raster TAA and its camera jitter are both
            // inactive; MSAA sample topology still affects input.
            const ResolvedAntiAliasingSettings antiAliasing =
                m_ui.GetResolvedAntiAliasingSettings();
            HashLightingHistoryValue(
                signature, antiAliasing.rasterSampleCount);
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
            hashNoiseSettings(visibilityNoiseSettings);
            hashNoiseSettings(skyNoiseSettings);
            hashNoiseSettings(flashlightNoiseSettings);

            const ScreenSpaceVisibilitySettings& visibility =
                m_ui.ScreenSpaceVisibility;
            HashLightingHistoryValue(signature, visibility.enabled);
            HashLightingHistoryValue(signature, visibility.quality);
            HashLightingHistoryValue(
                signature, visibility.qualityPresetOrigin);
            HashLightingHistoryValue(signature, visibility.estimator);
            HashLightingHistoryValue(signature, visibility.resolution);
            HashLightingHistoryValue(
                signature, visibility.sampling.maximumSampleCount);
            HashLightingHistoryValue(
                signature, visibility.sampling.radius);
            HashLightingHistoryValue(
                signature, visibility.sampling.thickness);
            HashLightingHistoryValue(
                signature,
                visibility.sampling.stepDistributionExponent);
            HashLightingHistoryValue(
                signature, visibility.ambientOcclusion.enabled);
            HashLightingHistoryValue(
                signature,
                visibility.ambientOcclusion.outputHitDistance);
            HashLightingHistoryValue(
                signature, visibility.ambientOcclusion.strength);
            HashLightingHistoryValue(
                signature, visibility.indirectDiffuse.enabled);
            HashLightingHistoryValue(
                signature,
                visibility.indirectDiffuse.outputHitDistance);
            HashLightingHistoryValue(
                signature, visibility.indirectDiffuse.intensity);
            HashLightingHistoryValue(
                signature, visibility.bufferPrecision.ambient);
            HashLightingHistoryValue(
                signature, visibility.bufferPrecision.indirect);
            HashLightingHistoryValue(signature, visibility.debugView);

            const DirectionalShadowSettings& directional =
                m_ui.DirectionalShadows;
            HashLightingHistoryValue(signature, directional.enabled);
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
                signature, skyVisibility.outputHitDistance);
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
                signature, flashlight.outputHitDistance);
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

            const auto hashDenoisingSignal =
                [&](const DenoisingSignalSettings& settings)
                {
                    HashLightingHistoryValue(signature, settings.method);
                    HashLightingHistoryValue(signature, settings.quality);
                    HashLightingHistoryValue(signature, settings.resolution);
                    HashLightingHistoryValue(
                        signature, settings.historyLength);
                    HashLightingHistoryValue(
                        signature, settings.disocclusionThreshold);
                    HashLightingHistoryValue(
                        signature, settings.antiLagStrength);
                    HashLightingHistoryValue(
                        signature, settings.spatialRadius);
                };
            hashDenoisingSignal(m_ui.Denoising.ambientOcclusion);
            hashDenoisingSignal(m_ui.Denoising.diffuseGi);
            hashDenoisingSignal(m_ui.Denoising.shadows);
            hashDenoisingSignal(m_ui.Denoising.skyVisibility);
            HashLightingHistoryValue(
                signature,
                m_DenoisingPass && m_DenoisingPass->IsOperational());
            HashLightingHistoryValue(
                signature,
                m_DenoisingPass &&
                    m_DenoisingPass->IsSpatialAvailable());

            HashLightingHistoryValue(signature, m_ui.EnableAmbientFill);
            HashLightingHistoryValue(signature, m_ui.EnableDiffuseIbl);
            HashLightingHistoryValue(signature, m_ui.DiffuseIblStrength);
            HashLightingHistoryValue(signature, m_ui.EnableSpecularIbl);
            HashLightingHistoryValue(signature, m_ui.SpecularIblStrength);
            HashLightingHistoryValue(signature, m_ui.WhiteWorld);
            HashLightingHistoryValue(signature, m_ui.LightingDebugView);

            HashLightingHistoryValue(
                signature, screenSpaceVisibilityRequested);
            HashLightingHistoryValue(
                signature, screenSpaceVisibilityReady);
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
                signature, worldRepresentationReady);
        }

        const bool viewChanged =
            !m_HasLightingHistorySignatures ||
            viewSignature != m_LastLightingViewSignature;
        const bool domainChanged =
            !m_HasLightingHistorySignatures ||
            signature != m_LastLightingDomainSignature;
        m_LightingHistoryChangedByViewOnly =
            m_HasLightingHistorySignatures &&
            viewChanged && !domainChanged;
        if (viewChanged || domainChanged)
        {
            InvalidateLightingAccumulationHistory();
            m_LastLightingViewSignature = viewSignature;
            m_LastLightingDomainSignature = signature;
            m_HasLightingHistorySignatures = true;
        }
    }

auto UvsrSceneViewer::ResetImageBasedLightingHistory() -> void {
        InvalidateLightingAccumulationHistory();
        m_HasLightingHistorySignatures = false;
        m_LightingHistoryChangedByViewOnly = false;
        ResetAntiAliasingState();
        InvalidateRendererStageTiming(
            RendererTimingStage::ShadowRayDispatch);
        InvalidateRendererStageTiming(
            RendererTimingStage::SkyVisibilityRayDispatch);
        InvalidateRendererStageTiming(
            RendererTimingStage::CompleteFrame);
    }

auto UvsrSceneViewer::ResetNoiseSamplingHistory(
        bool visibility,
        bool shadows,
        bool skyVisibility,
        bool flashlight) -> void {
        InvalidateLightingAccumulationHistory();
        ResetAntiAliasingState();
        if (visibility)
        {
            m_ScreenSpaceVisibilityPhase = 0u;
            InvalidateRendererStageTiming(
                RendererTimingStage::ScreenSpaceVisibility);
        }
        if (shadows)
        {
            InvalidateRendererStageTiming(
                RendererTimingStage::ShadowRayDispatch);
        }
        if (skyVisibility)
        {
            m_RayTracedSkyVisibilityPhase = 0u;
            InvalidateRendererStageTiming(
                RendererTimingStage::SkyVisibilityRayDispatch);
        }
        if (flashlight)
        {
            m_RayTracedFlashlightShadowPhase = 0u;
            InvalidateRendererStageTiming(
                RendererTimingStage::ShadowRayDispatch);
        }
        InvalidateRendererStageTiming(RendererTimingStage::CompleteFrame);
    }

auto UvsrSceneViewer::GetNoiseTextureResidentBytes() const -> uint64_t {
        return m_NoiseTextureLibrary
            ? m_NoiseTextureLibrary->GetResidentBytes()
            : 0u;
    }

auto UvsrSceneViewer::GetScreenSpaceVisibilityTimings() const -> const ScreenSpaceVisibilityTimings* {
        return m_ScreenSpaceVisibilityPass
            ? &m_ScreenSpaceVisibilityPass->GetTimings()
            : nullptr;
    }

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::DidDispatchScreenSpaceVisibilityThisFrame() const -> bool {
        return m_ScreenSpaceVisibilityDispatchedThisFrame;
    }
#endif


#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::DidCommitLightingAccumulationThisFrame() const -> bool {
        return m_LightingAccumulationCommittedThisFrame;
    }
#endif
