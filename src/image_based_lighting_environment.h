#pragma once

#include "diffuse_environment_math.h"
#include "image_based_lighting_sources.h"

#include <donut/core/math/float.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace donut::engine
{
    class CommonRenderPasses;
    class LightProbe;
    class ShaderFactory;
}

namespace donut::render
{
    class LightProbeProcessingPass;
}

namespace uvsr
{
    class ImageBasedLightingEnvironment
    {
    public:
        static constexpr uint32_t DiffuseCubeDimension = 16u;
        static constexpr uint32_t RadianceCubeDimension = 512u;
        static constexpr uint32_t RadianceCubeMipCount = 10u;
        static constexpr uint32_t SpecularCubeDimension = 256u;
        static constexpr uint32_t SpecularCubeMipCount = 9u;

        struct PreparedRadiance
        {
            ImageBasedLightingSource source =
                ImageBasedLightingSource::Count;
            bool neutralize = false;
            std::vector<float> pixels;
            uint32_t width = 0u;
            uint32_t height = 0u;
            std::vector<donut::math::float16_t4> radianceFaces;
            std::vector<donut::math::float16_t4> diffuseFaces;
            DiffuseEnvironmentSh diffuseSh;
            float averageLuminance = 0.f;
        };

        ImageBasedLightingEnvironment(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses,
            std::filesystem::path environmentAssetDirectory);

        // CPU-only decode, validation, SH projection, and cubemap resampling.
        // UVSR runs this on its scene worker before the first rendered frame.
        [[nodiscard]] std::optional<PreparedRadiance> PrepareRadiance(
            ImageBasedLightingSource source,
            bool neutralize) const;

        void StagePreparedRadiance(PreparedRadiance prepared);

        // True when no worker-prepared radiance field is waiting for, or
        // progressing through, its bounded GPU preparation sequence.
        [[nodiscard]] bool IsPreparedRadianceReady() const
        {
            return m_PreparedRadianceStage ==
                PreparedRadianceGpuStage::None;
        }

        // Returns true only when the selected radiance field was uploaded and
        // its derived maps were rebuilt. Exposure and lobe toggles update probe
        // scalars without touching texture contents.
        bool Update(
            nvrhi::ICommandList* commandList,
            bool neutralize,
            float outputScale,
            float exposureStops,
            bool diffuseEnabled,
            float diffuseStrength,
            bool specularEnabled,
            float specularStrength,
            ImageBasedLightingSource source);

        [[nodiscard]] nvrhi::ITexture* GetRadianceTexture() const
        {
            return m_Uploaded ? m_RadianceTexture.Get() : nullptr;
        }

        [[nodiscard]] nvrhi::ITexture* GetRadianceTextureResource() const
        {
            return m_RadianceTexture.Get();
        }

        [[nodiscard]] nvrhi::ITexture* GetEnvironmentBrdfTexture() const;

        [[nodiscard]] const donut::engine::LightProbe* GetLightProbe() const
        {
            return m_Uploaded ? m_LightProbe.get() : nullptr;
        }

        [[nodiscard]] float GetRadianceScale() const
        {
            return m_RadianceScale;
        }

    private:
        enum class PreparedRadianceGpuStage
        {
            None,
            EnvironmentBrdf,
            RadianceFaceUpload,
            DiffuseUpload,
            RadianceMipGeneration,
            SpecularBaseBlit,
            SpecularMipGeneration
        };

        bool AdvancePreparedRadiance(
            nvrhi::ICommandList* commandList);

        bool RebuildRadiance(
            nvrhi::ICommandList* commandList,
            const PreparedRadiance& prepared);

        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<donut::render::LightProbeProcessingPass>
            m_ProbeProcessing;
        nvrhi::TextureHandle m_RadianceTexture;
        nvrhi::TextureHandle m_DiffuseTexture;
        nvrhi::TextureHandle m_SpecularTexture;
        std::shared_ptr<donut::engine::LightProbe> m_LightProbe;
        std::filesystem::path m_EnvironmentAssetDirectory;
        ImageBasedLightingSource m_LastRequestedSource =
            ImageBasedLightingSource::Count;
        float m_RadianceScale = 1.f;
        bool m_BrdfReady = false;
        bool m_Uploaded = false;
        bool m_LastNeutralize = false;
        bool m_LastLoadFailed = false;
        std::optional<PreparedRadiance> m_PreparedRadiance;
        PreparedRadianceGpuStage m_PreparedRadianceStage =
            PreparedRadianceGpuStage::None;
        uint32_t m_PreparedRadianceStep = 0u;
    };
}
