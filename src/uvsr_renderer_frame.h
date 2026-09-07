#pragma once

#include "agx_tone_mapping_pass.h"
#include "auto_exposure.h"
#include "fast_approximate_aa.h"
#include "renderer_common_passes.h"
#include "renderer_geometry_passes.h"
#include "renderer_pixel_readback.h"
#include "renderer_shader_factory.h"
#include "renderer_targets.h"
#include "uvsr_application.h"
#if defined(UVSR_BUILD_TESTING)
#include "retained_runtime_diagnostic.h"
#endif
#include <donut/engine/BindingCache.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/render/GeometryPasses.h>
#include <donut/render/DrawStrategy.h>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>

namespace uvsr
{
    enum class MaterialPickPurpose
    {
        None,
        FocusCameraAtCursor,
        RefreshMaterialDrawerSelection
    };

    enum class RenderPassPreparationStage
    {
        Idle,
        GBuffer,
        DeferredLighting,
        DeferredLightingPipelines,
        FastApproximateAA,
        EnvironmentBackground,
        ToneMapping,
        Complete
    };

    struct RendererFrameState
    {
        static constexpr uint32_t TimerLatency = 4u;
        explicit RendererFrameState(nvrhi::IDevice* device) : bindingCache(device) {}
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory;
        std::shared_ptr<uvsr::RendererShaderFactory> rendererShaderFactory;
        std::shared_ptr<uvsr::RendererCommonPasses> rendererCommonPasses;
        std::shared_ptr<donut::render::InstancedOpaqueDrawStrategy> opaqueDrawStrategy;
        std::unique_ptr<RenderTargets> renderTargets;
        std::unique_ptr<RendererGeometryPass> gBufferGeometryPass;
        std::unique_ptr<AutoExposurePass> autoExposurePass;
        std::unique_ptr<AgxToneMappingPass> agxToneMappingPass;
        ColorLutResource toneMappingLut;
        std::unique_ptr<FastApproximateAAPass> fastApproximateAAPass;
        std::unique_ptr<RendererGeometryPass> materialIdGeometryPass;
        std::unique_ptr<uvsr::RendererPixelReadback> pixelReadback;
        std::shared_ptr<donut::engine::IView> view;
            nvrhi::CommandListHandle commandList;
        std::array<std::array<nvrhi::TimerQueryHandle, TimerLatency>, static_cast<size_t>(RendererTimingStage::Count)> rendererTimerQueries;
        std::array<std::array<bool, TimerLatency>, static_cast<size_t>(RendererTimingStage::Count)> rendererTimerPending{};
        std::array<std::array<uint64_t, TimerLatency>, static_cast<size_t>(RendererTimingStage::Count)> rendererTimerPendingEpoch{};
        std::array<uint64_t, static_cast<size_t>(RendererTimingStage::Count)> rendererTimerStageEpoch{};
        std::array<bool, static_cast<size_t>(RendererTimingStage::Count)> rendererTimerActive{};
        uint32_t rendererTimerFrame = 0u;
        bool rendererTimerFrameWritable = true;
        RendererTimings rendererTimings;
        donut::engine::BindingCache bindingCache;
        uint64_t submittedMainViewTriangles = 0u;
        float frameDeltaSeconds = 0.f;
        donut::math::uint2 pickPosition = 0u;
        MaterialPickPurpose materialPickPurpose = MaterialPickPurpose::None;
        const donut::engine::Scene* materialPickScene = nullptr;
        bool autoExposureDispatchedThisFrame = false;
#if defined(UVSR_BUILD_TESTING)
        bool runtimeOutputCaptureRequested = false;
        std::optional<RuntimeOutputEvidence> runtimeOutputEvidence;
        std::filesystem::path runtimeOutputCapturePath;
        nvrhi::StagingTextureHandle runtimeLinearReadback;
        bool runtimeLinearReadbackQueued = false;
#endif
        RenderPassPreparationStage renderPassPreparationStage = RenderPassPreparationStage::Idle;
        bool renderPassPreparationWaitForIbl = false;
    };
}
