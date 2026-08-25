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
}

namespace uvsr
{
    class RendererShaderFactory;

    struct PathTracingInputs
    {
        const donut::engine::IView* view = nullptr;
        const donut::engine::IView* previousView = nullptr;
        uint32_t width = 0u;
        uint32_t height = 0u;
        RayTracedMaterialVisibilityInputs materialVisibility;
        nvrhi::rt::IAccelStruct* worldTlas = nullptr;
        nvrhi::ITexture* environment = nullptr;
        float environmentScale = 1.f;
        bool showEnvironmentBackground = true;
        nvrhi::ITexture* noiseTexture = nullptr;
        NoiseSettings noiseSettings;
        std::vector<std::shared_ptr<donut::engine::Light>> lights;
        const donut::engine::Light* flashlight = nullptr;
        FlashlightBeamProfile flashlightProfile = {};
        uint64_t historyEpoch = 0u;
        float rayBias = 0.001f;
        float maximumRayDistance = 1.e8f;
    };

    struct PathTracingCapabilities
    {
        bool rayQuerySupported = false;
    };

    struct PathTracingResult
    {
        nvrhi::ITexture* sceneLinearDisplay = nullptr;
        nvrhi::ITexture* rawMean = nullptr;
        nvrhi::ITexture* temporalDepth = nullptr;
        nvrhi::ITexture* motionVectors = nullptr;
        uint64_t currentCenterPixelAcceptedSampleCount = 0u;
        PathTracingCapabilities capabilities;
        bool dispatched = false;
        bool historyReset = false;

        [[nodiscard]] explicit operator bool() const noexcept
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
            const std::shared_ptr<RendererShaderFactory>&
                shaderFactory,
            nvrhi::IBindingLayout* bindlessLayout);

        [[nodiscard]] PathTracingAvailability GetAvailability()
            const noexcept;

        [[nodiscard]] bool IsSupported() const noexcept
        {
            return GetAvailability().executablePipelineAvailable;
        }

        [[nodiscard]] const PathTracingCapabilities& GetCapabilities()
            const noexcept
        {
            return m_Capabilities;
        }

        [[nodiscard]] uint64_t GetCurrentCenterPixelAcceptedSampleCount()
            const noexcept
        {
            return m_CurrentCenterPixelAcceptedSampleCount;
        }

        void PollAcceptedSampleReadback();
        void SubmitAcceptedSampleReadback();

        [[nodiscard]] PathTracingResult Render(
            nvrhi::ICommandList* commandList,
            const PathTracingInputs& inputs);

        void ResetHistory();
        void ResetBindingCache();

    private:
        [[nodiscard]] bool EnsureResources(uint32_t width, uint32_t height);
        [[nodiscard]] bool EnsureLightBuffer(uint32_t lightCount);
        [[nodiscard]] bool EnsureBindingSet(const PathTracingInputs& inputs);
        void ClearHistory(nvrhi::ICommandList* commandList);

        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindlessLayout;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::SamplerHandle m_Sampler;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BufferHandle m_LightBuffer;
        nvrhi::ShaderHandle m_Shader;
        nvrhi::ComputePipelineHandle m_Pipeline;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::TextureHandle m_RawMean;
        nvrhi::TextureHandle m_SuccessfulSampleCount;
        nvrhi::TextureHandle m_Motion;
        nvrhi::TextureHandle m_Depth;
        nvrhi::TextureHandle m_RetryGeneration;

        struct AcceptedSampleReadbackSlot
        {
            nvrhi::StagingTextureHandle texture;
            nvrhi::EventQueryHandle query;
            uint64_t generation = 0u;
            bool submitted = false;
        };
        std::array<AcceptedSampleReadbackSlot, 3>
            m_AcceptedSampleReadbacks;
        int m_PendingAcceptedSampleReadback = -1;
        uint64_t m_AcceptedSampleGeneration = 1u;
        uint64_t m_CurrentCenterPixelAcceptedSampleCount = 0u;

        nvrhi::rt::IAccelStruct* m_BoundTlas = nullptr;
        RayTracedMaterialVisibilityInputs m_BoundMaterialVisibility;
        nvrhi::ITexture* m_BoundEnvironment = nullptr;
        nvrhi::ITexture* m_BoundNoiseTexture = nullptr;
        uint32_t m_Width = 0u;
        uint32_t m_Height = 0u;
        uint32_t m_LightCapacity = 0u;
        uint64_t m_LastHistoryEpoch = ~uint64_t(0u);
        uint64_t m_LastInputSignature = 0u;
        bool m_HistoryValid = false;
        bool m_ResetRequested = true;
        bool m_ReportedInvalidInput = false;
        PathTracingCapabilities m_Capabilities;
    };
}
