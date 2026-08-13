#pragma once

#include "flashlight_shared.h"
#include "noise_settings.h"
#include "path_tracing_settings.h"
#include "ray_traced_material_visibility.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace donut::engine
{
    class IView;
    class Light;
    class ShaderFactory;
}

namespace uvsr
{
    struct PathTracingInputs
    {
        const donut::engine::IView* view = nullptr;
        uint32_t width = 0u;
        uint32_t height = 0u;

        RayTracedMaterialVisibilityInputs materialVisibility;
        nvrhi::rt::IAccelStruct* worldTlas = nullptr;
        nvrhi::ITexture* environment = nullptr;
        float environmentScale = 1.f;
        // This affects primary-ray misses only. Secondary misses always keep
        // environment radiance in the transport solution.
        bool showEnvironmentBackground = true;
        nvrhi::ITexture* noiseTexture = nullptr;
        NoiseSettings noiseSettings;
        std::vector<std::shared_ptr<donut::engine::Light>> lights;

        const donut::engine::Light* flashlight = nullptr;
        FlashlightBeamProfile flashlightProfile = {};

        uint32_t samplingPhase = 0u;
        uint64_t historyEpoch = 0u;
        // This serial advances independently of authored noise animation so
        // adaptive retry cannot leave a skipped pixel permanently starved.
        // UINT64_MAX selects the pass-owned monotonic serial.
        uint64_t schedulingSerial = ~uint64_t(0u);
        bool accumulateSamples = false;
        float rayBias = 0.001f;
        float maximumRayDistance = 1.e8f;
        PathTracingSettings settings;
    };

    struct PathTracingCapabilities
    {
        bool rayQuerySupported = false;
        // One bit per {solver} x {RTXDI off/on} x
        // {Uniform/Power/NEE-AT} pipeline.
        // Hardware support and executable shader availability are reported
        // separately so one unavailable optional preset cannot disable the
        // conservative RTX PT baseline.
        uint32_t pipelineAvailabilityMask = 0u;
        bool serSupported = false;
        bool directReservoirSupported = false;
        bool temporalReservoirReuseSupported = false;
        bool previousFrameSpatialReuseSupported = false;
        bool continuationSeedReservoirSupported = false;
        bool replayablePathSeedSupported = false;
        bool temporalGiCheckpointReuseSupported = false;
        bool spatialGiCheckpointReuseSupported = false;
        bool fullSampleReconnectionSupported = false;
        // Transport signal formats and the executable reconstruction PSO are
        // reported separately. The app composes the latter after constructing
        // both independent passes.
        bool stablePlaneSignalSupported = false;
        bool stablePlaneResolveSupported = false;
        bool psrSupported = false;
        bool nrdSupported = false;

        [[nodiscard]] static constexpr uint32_t GetPipelineVariant(
            const PathTracingSettings& settings) noexcept
        {
            return GetPathTracingPipelineVariant(settings);
        }

        [[nodiscard]] constexpr bool IsPipelineAvailable(
            const PathTracingSettings& settings) const noexcept
        {
            return IsPathTracingPipelineAvailable(
                settings,
                pipelineAvailabilityMask);
        }

        [[nodiscard]] constexpr PathTracingPipelineResolution ResolvePipeline(
            const PathTracingSettings& requestedSettings) const noexcept
        {
            return ResolvePathTracingPipeline(
                requestedSettings,
                pipelineAvailabilityMask);
        }

        [[nodiscard]] constexpr bool CanUseStablePlaneResolve(
            const PathTracingSettings& settings) const noexcept
        {
            return uvsr::CanUseStablePlaneResolve(
                settings,
                stablePlaneSignalSupported &&
                    stablePlaneResolveSupported);
        }
    };

    struct PathTracingResult
    {
        nvrhi::ITexture* sceneLinearDisplay = nullptr;
        nvrhi::ITexture* rawMean = nullptr;
        nvrhi::ITexture* successfulSampleCount = nullptr;
        nvrhi::ITexture* directReservoir = nullptr;
        nvrhi::ITexture* giCheckpointReservoir = nullptr;
        nvrhi::ITexture* pathSeedReservoir = nullptr;
        // These are exposed only after a complete signal cycle in the current
        // history epoch. They are persistent online means, never transient
        // fragments from the current dispatch.
        nvrhi::ITexture* residualMean = nullptr;
        nvrhi::ITexture* diffuseSuffixMean = nullptr;
        nvrhi::ITexture* primaryNormalRoughness = nullptr;
        nvrhi::ITexture* primaryViewZ = nullptr;

        PathTracingCapabilities capabilities;
        uint64_t submittedSamplePassCount = 0u;
        uint64_t signalEpoch = 0u;
        bool dispatched = false;
        bool historyReset = false;
        bool completedSignalCycle = false;
        bool stablePlaneResolveActive = false;
        bool directReservoirActive = false;
        bool temporalReuseActive = false;
        bool spatialReuseActive = false;
        bool giCheckpointReuseActive = false;
        bool pathSeedReplayActive = false;
        bool serRequestedButUnavailable = false;
        bool stablePlaneResolveRequestedButUnavailable = false;
        bool psrRequestedButUnavailable = false;
        bool nrdRequestedButUnavailable = false;
        bool pathReuseRequestedButUnavailable = false;
        bool giReuseRequestedButUnavailable = false;
        // ReSTIR PT and ReSTIR GI are executable clean-room subsets. They
        // deliberately do not claim NVIDIA namesake parity or geometric
        // reconnection.
        bool cleanRoomSolverSubsetActive = false;
        bool namesakeParityUnavailable = false;
        bool solverPresetRequestedButUnavailable = false;
        bool geometricReconnectionUnavailable = false;
        bool pipelineFallbackActive = false;
        bool rawMeanBiasedByFireflyFilter = false;

        [[nodiscard]] explicit operator bool() const
        {
            return sceneLinearDisplay != nullptr && dispatched;
        }
    };

    class PathTracingPass final
    {
    public:
        [[nodiscard]] static PathTracingCapabilities QueryCapabilities(
            nvrhi::IDevice* device);

        PathTracingPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory,
            nvrhi::IBindingLayout* bindlessLayout);

        [[nodiscard]] bool IsSupported() const
        {
            return m_Capabilities.rayQuerySupported &&
                (m_Capabilities.pipelineAvailabilityMask & 1u) != 0u;
        }

        [[nodiscard]] const PathTracingCapabilities& GetCapabilities() const
        {
            return m_Capabilities;
        }

        void SetStablePlaneResolveSupported(bool supported)
        {
            m_Capabilities.stablePlaneResolveSupported = supported &&
                m_Capabilities.stablePlaneSignalSupported && IsSupported();
        }

        [[nodiscard]] PathTracingResult Render(
            nvrhi::ICommandList* commandList,
            const PathTracingInputs& inputs);

        void ResetHistory();
        void ResetBindingCache();

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindlessLayout;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::SamplerHandle m_Sampler;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BufferHandle m_LightBuffer;
        std::array<nvrhi::ShaderHandle,
            PathTracingPipelineVariantCount> m_Shaders;
        std::array<nvrhi::ComputePipelineHandle,
            PathTracingPipelineVariantCount> m_Pipelines;
        std::array<nvrhi::BindingSetHandle, 2> m_BindingSets;

        nvrhi::TextureHandle m_RawMean;
        nvrhi::TextureHandle m_SuccessfulSampleCount;
        nvrhi::TextureHandle m_Display;
        nvrhi::TextureHandle m_ResidualMean;
        nvrhi::TextureHandle m_DiffuseSuffixMean;
        nvrhi::TextureHandle m_PrimaryNormalRoughness;
        nvrhi::TextureHandle m_PrimaryViewZ;
        std::array<nvrhi::TextureHandle, 2> m_DirectReservoirs;
        std::array<nvrhi::TextureHandle, 2> m_SurfaceHistory;
        std::array<nvrhi::TextureHandle, 2> m_DirectSampleSeeds;
        std::array<nvrhi::TextureHandle, 2> m_GiCheckpointReservoirs;
        std::array<nvrhi::TextureHandle, 2> m_GiCheckpointCounts;
        std::array<nvrhi::TextureHandle, 2> m_PathSeedReservoirs;
        std::array<nvrhi::TextureHandle, 2> m_PathSeedStatistics;

        nvrhi::rt::IAccelStruct* m_BoundTlas = nullptr;
        RayTracedMaterialVisibilityInputs m_BoundMaterialVisibility;
        nvrhi::ITexture* m_BoundEnvironment = nullptr;
        nvrhi::ITexture* m_BoundNoiseTexture = nullptr;
        uint32_t m_Width = 0u;
        uint32_t m_Height = 0u;
        uint32_t m_LightCapacity = 0u;
        uint32_t m_HistoryIndex = 0u;
        uint32_t m_LastDebugView = ~0u;
        uint64_t m_LastHistoryEpoch = ~uint64_t(0u);
        uint64_t m_LastTransportSignature = 0u;
        uint64_t m_SubmittedSamplePassCount = 0u;
        uint64_t m_SchedulingSerial = 0u;
        uint64_t m_SignalEpoch = 0u;
        uint32_t m_ProgressivePhase = 0u;
        bool m_HistoryValid = false;
        bool m_ReservoirHistoryValid = false;
        bool m_DebugRefreshActive = false;
        bool m_ResetRequested = true;
        bool m_DirectReuseResourcesFullResolution = false;
        bool m_GiReuseResourcesFullResolution = false;
        bool m_PathReuseResourcesFullResolution = false;
        bool m_StableSignalResourcesFullResolution = false;
        bool m_CompletedSignalCycle = false;
        bool m_ReportedInvalidInput = false;
        bool m_ReportedUnsafeSchedule = false;
        bool m_ReportedUnavailablePipeline = false;
        PathTracingCapabilities m_Capabilities;

        [[nodiscard]] bool EnsureResources(
            uint32_t width,
            uint32_t height,
            bool directReuseRequired,
            bool giReuseRequired,
            bool pathReuseRequired,
            bool stableSignalsRequired);
        [[nodiscard]] bool EnsureLightBuffer(uint32_t lightCount);
        [[nodiscard]] bool EnsureBindingSet(
            const PathTracingInputs& inputs,
            uint32_t historyIndex);
        void ClearHistory(nvrhi::ICommandList* commandList);
        void ClearBindingSets();
    };
}
