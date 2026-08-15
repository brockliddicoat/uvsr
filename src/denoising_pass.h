#pragma once

#include "denoiser_backend.h"
#include "denoising_settings.h"

#include <donut/core/math/math.h>
#include <donut/engine/BindingCache.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class IView;
    class ShaderFactory;
}

namespace uvsr
{
    struct DenoisingInputs
    {
        nvrhi::ITexture* rawSignal = nullptr;
        nvrhi::ITexture* hitDistance = nullptr;
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* normalRoughness = nullptr;
        nvrhi::ITexture* motionVectors = nullptr;
        const donut::engine::IView* currentView = nullptr;
        const donut::engine::IView* previousView = nullptr;
        // Output and guide resolution. A zero source size means the raw signal
        // and hit texture already use this resolution.
        dm::uint2 signalSize = dm::uint2::zero();
        dm::uint2 sourceSize = dm::uint2::zero();

        // ReBLUR uses this as hit distance parameter A. AO should provide its
        // trace radius, GI its trace reach, and sky visibility its ray reach.
        float hitDistanceNormalization = 1.f;
        float frameDeltaSeconds = 0.f;
        uint64_t frameIndex = 0;
        // Set false when an aggregate ratio signal is paired with an unrelated
        // nearest hit. The pass then preserves the raw signal.
        bool hitDistanceMatchesSignal = true;

        // Sun SIGMA inputs.
        dm::float3 lightDirectionWorld = dm::float3::zero();
        float directionalTanAngularRadius = 0.f;

        // Flashlight SIGMA inputs.
        dm::float3 localLightPosition = dm::float3::zero();
        float localLightRadius = 0.f;
    };

    struct DenoisingResult
    {
        nvrhi::ITexture* texture = nullptr;
        bool denoised = false;
    };

    // Owns one independent spatial output or backend signal and history for
    // AO, GI, sky, sun, and flashlight. Any disabled, unavailable, invalid,
    // or failed path returns the caller's raw signal for that frame.
    class DenoisingPass final
    {
    public:
        DenoisingPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory,
            uint32_t framesInFlight = 3);
        ~DenoisingPass();

        DenoisingResult ProcessAmbientOcclusion(
            nvrhi::ICommandList* commandList,
            const DenoisingSignalSettings& settings,
            const DenoisingInputs& inputs);
        DenoisingResult ProcessDiffuseGi(
            nvrhi::ICommandList* commandList,
            const DenoisingSignalSettings& settings,
            const DenoisingInputs& inputs);
        DenoisingResult ProcessSkyVisibility(
            nvrhi::ICommandList* commandList,
            const DenoisingSignalSettings& settings,
            const DenoisingInputs& inputs);
        DenoisingResult ProcessSunShadow(
            nvrhi::ICommandList* commandList,
            const DenoisingSignalSettings& settings,
            const DenoisingInputs& inputs);
        DenoisingResult ProcessFlashlightShadow(
            nvrhi::ICommandList* commandList,
            const DenoisingSignalSettings& settings,
            const DenoisingInputs& inputs);

        void DisableSignal(DenoiserSignalType type) noexcept;
        void RequestHistoryReset(
            DenoiserHistoryReset reset =
                DenoiserHistoryReset::ClearAndRestart) noexcept;
        void ReleaseResources() noexcept;

        [[nodiscard]] const DenoiserBackendCapabilities& GetCapabilities()
            const noexcept;
        [[nodiscard]] const DenoiserStatus& GetLastStatus(
            DenoiserSignalType type) const noexcept;
        [[nodiscard]] DenoiserMemoryStats GetBackendMemoryStats() const noexcept;
        [[nodiscard]] uint64_t GetCallerOwnedBytes() const noexcept;
        [[nodiscard]] bool IsSpatialAvailable() const noexcept;
        [[nodiscard]] bool IsOperational() const noexcept;

    private:
        enum class SignalClass : uint8_t
        {
            Radiance = 0,
            Shadow = 1,
            ScalarRadiance = 2,
            Count
        };

        struct Pipeline
        {
            nvrhi::ShaderHandle shader;
            nvrhi::BindingLayoutHandle bindingLayout;
            nvrhi::ComputePipelineHandle pipeline;
        };

        struct SignalState
        {
            DenoiserSignalHandle handle;
            DenoiserSignalDescription description;
            DenoiserSettings backendSettings;
            DenoiserStatus lastStatus;
            DenoiserMethod method = DenoiserMethod::ReblurDiffuse;
            DenoisingMethodChoice methodChoice = DenoisingMethodChoice::None;
            nvrhi::Format spatialFormat = nvrhi::Format::UNKNOWN;
            DenoiserExtent extent;
            dm::uint2 fullSize = dm::uint2::zero();
            dm::uint2 sourceSize = dm::uint2::zero();
            std::array<nvrhi::ITexture*, 5> inputTextures{};
            bool configured = false;
            std::unique_ptr<donut::engine::BindingCache> bindingCache;

            nvrhi::TextureHandle motionVectors;
            nvrhi::TextureHandle normalRoughness;
            nvrhi::TextureHandle viewZ;
            nvrhi::TextureHandle noisyRadianceHitDistance;
            nvrhi::TextureHandle denoisedRadianceHitDistance;
            nvrhi::TextureHandle penumbra;
            nvrhi::TextureHandle shadow;
            nvrhi::TextureHandle resolved;
            nvrhi::TextureHandle spatialOutput;
            DenoiserSignalResources resources;
        };

        static constexpr size_t c_SignalCount = 5;
        static constexpr size_t c_SpatialFormatCount = 5;

        nvrhi::DeviceHandle m_Device;
        std::unique_ptr<IDenoiserBackend> m_Backend;
        nvrhi::BufferHandle m_ConstantBuffer;
        std::array<Pipeline, c_SpatialFormatCount> m_SpatialPipelines;
        std::array<Pipeline, size_t(SignalClass::Count)> m_PreparePipelines;
        std::array<Pipeline, size_t(SignalClass::Count)> m_ResolvePipelines;
        std::array<SignalState, c_SignalCount> m_Signals;
        uint32_t m_FramesInFlight = 3;
        bool m_SpatialAvailable = false;
        bool m_FrontEndAvailable = false;
        bool m_BackendInitialized = false;

        DenoisingResult Process(
            DenoiserSignalType type,
            nvrhi::ICommandList* commandList,
            const DenoisingSignalSettings& settings,
            const DenoisingInputs& inputs);
        DenoisingResult ProcessSpatial(
            DenoiserSignalType type,
            nvrhi::ICommandList* commandList,
            const DenoisingSignalSettings& settings,
            const DenoisingInputs& inputs);
        bool EnsureSpatialResources(
            SignalState& state,
            DenoisingMethodChoice method,
            nvrhi::ITexture* rawSignal,
            dm::uint2 fullSize,
            dm::uint2 sourceSize);
        bool EnsureResources(
            SignalState& state,
            DenoiserSignalType type,
            DenoiserMethod method,
            dm::uint2 fullSize,
            DenoiserExtent extent);
        void ReleaseSignal(SignalState& state) noexcept;
        [[nodiscard]] DenoiserSettings BuildBackendSettings(
            DenoiserSignalType type,
            DenoiserMethod method,
            const DenoisingSignalSettings& settings,
            const DenoisingInputs& inputs) const noexcept;
        [[nodiscard]] DenoiserFrameSettings BuildFrameSettings(
            DenoiserExtent extent,
            const DenoisingInputs& inputs) const noexcept;
        [[nodiscard]] static size_t SignalIndex(
            DenoiserSignalType type) noexcept;
        [[nodiscard]] static SignalClass GetSignalClass(
            DenoiserSignalType type,
            DenoiserMethod method) noexcept;
    };
}
