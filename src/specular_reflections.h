#pragma once

#include "sky_visibility_application.h"
#include "specular_settings.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>

namespace donut::engine
{
    class IView;
    class ShaderFactory;
}

namespace donut::render
{
    class MipMapGenPass;
}

namespace uvsr
{
    enum class SpecularReflectionStatus : uint32_t
    {
        Disabled,
        Preparing,
        UnsupportedDevice,
        MsaaUnavailable,
        MissingResolvedGBuffer,
        MissingEnvironment,
        ExternalDebugViewActive,
        ReadyScreenSpace,
        ReadyHybrid
    };

    struct SpecularReflectionInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* diffuse = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* emissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
        nvrhi::ITexture* sceneColor = nullptr;

        nvrhi::ITexture* specularEnvironment = nullptr;
        nvrhi::ITexture* environmentBrdf = nullptr;
        nvrhi::ITexture* skySpecularEstimate = nullptr;
        float specularEnvironmentScale = 1.f;
        float specularEnvironmentMipLevels = 1.f;
        uint32_t specularEnvironmentArrayIndex = 0u;
        uint32_t skyEstimateKind = UVSR_SKY_ESTIMATE_NONE;

        const donut::engine::IView* view = nullptr;
        nvrhi::rt::IAccelStruct* worldTlas = nullptr;
        bool globalRayTraversalAllowed = false;
        bool coherentTlasReady = false;
        float sceneDiagonal = SpecularDefaultRayMaxDistance;
    };

    struct SpecularReflectionReadiness
    {
        SpecularReflectionStatus status =
            SpecularReflectionStatus::Disabled;
        SpecularTechnique technique = SpecularTechnique::None;
        bool operational = false;
        bool rayTracingRequested = false;
        bool rayTracingAvailable = false;
    };

    struct SpecularReflectionResult
    {
        nvrhi::ITexture* indirectSpecular = nullptr;
        SpecularReflectionStatus status =
            SpecularReflectionStatus::Disabled;
        SpecularTechnique technique = SpecularTechnique::None;
        bool dispatchedScreenSpace = false;
        bool dispatchedRayTracing = false;
        bool ratioEstimator = false;
        bool deferredColorLookup = false;
        bool environmentFallback = false;
        uint32_t skyEstimateKind = UVSR_SKY_ESTIMATE_NONE;

        [[nodiscard]] explicit operator bool() const
        {
            return indirectSpecular != nullptr && dispatchedScreenSpace;
        }
    };

    class SpecularReflectionPass final
    {
    public:
        [[nodiscard]] static bool IsDeviceSupported(
            nvrhi::IDevice* device);

        SpecularReflectionPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory,
            bool deferPipelineCreation = false);
        ~SpecularReflectionPass();

        [[nodiscard]] bool PreparePipelinesStep();
        [[nodiscard]] bool ArePipelinesReady() const
        {
            return m_PipelinesReady;
        }

        [[nodiscard]] SpecularReflectionReadiness PrepareForRender(
            const SpecularSettings& settings,
            const SpecularReflectionInputs& inputs);

        [[nodiscard]] SpecularReflectionResult Render(
            nvrhi::ICommandList* commandList,
            const SpecularSettings& settings,
            const SpecularReflectionInputs& inputs,
            uint32_t frameIndex);

        void DeactivateForExternalDebugView(
            const SpecularSettings& settings);
        void Deactivate();
        void ReleaseResources();
        void ResetBindingCache();

        [[nodiscard]] SpecularReflectionReadiness GetReadiness() const
        {
            return m_Readiness;
        }

    private:
        struct Pipeline
        {
            nvrhi::ShaderHandle shader;
            nvrhi::BindingLayoutHandle bindingLayout;
            nvrhi::ComputePipelineHandle pipeline;
        };

        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
        nvrhi::SamplerHandle m_LinearClampSampler;
        nvrhi::BufferHandle m_ConstantBuffer;

        Pipeline m_PreparePipeline;
        Pipeline m_TracePipeline;
        Pipeline m_ArgumentPipeline;
        Pipeline m_RayQueryCompactPipeline;
        Pipeline m_RayQueryDensePipeline;
        Pipeline m_ResolvePipeline;

        nvrhi::BindingSetHandle m_PrepareBindingSet;
        nvrhi::BindingSetHandle m_TraceBindingSet;
        nvrhi::BindingSetHandle m_ArgumentBindingSet;
        nvrhi::BindingSetHandle m_RayQueryBindingSet;
        nvrhi::BindingSetHandle m_ResolveBindingSet;

        nvrhi::TextureHandle m_SceneColorPyramid;
        nvrhi::TextureHandle m_DepthPyramid;
        nvrhi::TextureHandle m_HitUvPdfDistance;
        nvrhi::TextureHandle m_HitDirectionFootprint;
        nvrhi::TextureHandle m_HitMetadata;
        nvrhi::TextureHandle m_HitConfidence;
        nvrhi::TextureHandle m_SourceRadiance;
        nvrhi::TextureHandle m_DummyRadiance;
        nvrhi::TextureHandle m_ResolvedSpecular;
        nvrhi::BufferHandle m_LowConfidenceRays;
        nvrhi::BufferHandle m_RayCounter;
        nvrhi::BufferHandle m_IndirectArguments;

        std::unique_ptr<donut::render::MipMapGenPass>
            m_SceneColorMipGenerator;
        std::unique_ptr<donut::render::MipMapGenPass>
            m_DepthMipGenerator;

        SpecularReflectionInputs m_BoundInputs;
        donut::math::uint2 m_Size = donut::math::uint2::zero();
        uint32_t m_MipLevels = 0u;
        bool m_SourceRadianceEnabled = false;
        bool m_ReverseDepth = true;
        bool m_Supported = false;
        bool m_RayQuerySupported = false;
        bool m_RayQueryPipelinesReady = false;
        bool m_PipelinesReady = false;
        uint32_t m_PipelinePreparationStep = 0u;
        SpecularReflectionReadiness m_Readiness;

        [[nodiscard]] bool EnsureResources(
            donut::math::uint2 size,
            bool reverseDepth,
            bool sourceRadianceEnabled);
        [[nodiscard]] bool EnsureBindingSets(
            const SpecularReflectionInputs& inputs,
            bool rayTracingAvailable);
        [[nodiscard]] bool ValidateInputs(
            const SpecularReflectionInputs& inputs,
            SpecularReflectionStatus& failure) const;
        [[nodiscard]] bool CreatePipelineStep(uint32_t step);
        void PublishRayRecordWrites(
            nvrhi::ICommandList* commandList) const;
    };
}
