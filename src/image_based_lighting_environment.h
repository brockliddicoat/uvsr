#pragma once

#include "diffuse_environment_math.h"
#include "image_based_lighting_sources.h"

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

        ImageBasedLightingEnvironment(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses,
            std::filesystem::path environmentAssetDirectory);

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

        [[nodiscard]] nvrhi::ITexture* GetDiffuseTexture() const
        {
            return m_Uploaded ? m_DiffuseTexture.Get() : nullptr;
        }

        [[nodiscard]] nvrhi::ITexture* GetSpecularTexture() const
        {
            return m_Uploaded ? m_SpecularTexture.Get() : nullptr;
        }

        [[nodiscard]] nvrhi::ITexture* GetEnvironmentBrdfTexture() const;

        [[nodiscard]] const donut::engine::LightProbe* GetLightProbe() const
        {
            return m_Uploaded ? m_LightProbe.get() : nullptr;
        }

        [[nodiscard]] const std::vector<
            std::shared_ptr<donut::engine::LightProbe>>& GetLightProbes() const
        {
            return m_ActiveLightProbes;
        }

        [[nodiscard]] float GetRadianceScale() const
        {
            return m_RadianceScale;
        }

        [[nodiscard]] float GetSourceAverageLuminance() const
        {
            return m_SourceAverageLuminance;
        }

        [[nodiscard]] ImageBasedLightingSource GetActiveSource() const
        {
            return m_LastSource;
        }

        [[nodiscard]] const DiffuseEnvironmentSh& GetLastSh() const
        {
            return m_LastSh;
        }

    private:
        struct ImportedRadiance
        {
            std::vector<float> pixels;
            uint32_t width = 0u;
            uint32_t height = 0u;
            DiffuseEnvironmentSh diffuseSh;
            float averageLuminance = 0.f;
        };

        [[nodiscard]] std::optional<ImportedRadiance>
            LoadImportedRadiance(
                ImageBasedLightingSource source,
                bool neutralize) const;

        bool RebuildRadiance(
            nvrhi::ICommandList* commandList,
            const ImportedRadiance& imported);

        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<donut::render::LightProbeProcessingPass>
            m_ProbeProcessing;
        nvrhi::TextureHandle m_RadianceTexture;
        nvrhi::TextureHandle m_DiffuseTexture;
        nvrhi::TextureHandle m_SpecularTexture;
        std::shared_ptr<donut::engine::LightProbe> m_LightProbe;
        std::vector<std::shared_ptr<donut::engine::LightProbe>>
            m_ActiveLightProbes;
        std::filesystem::path m_EnvironmentAssetDirectory;
        ImageBasedLightingSource m_LastRequestedSource =
            ImageBasedLightingSource::Count;
        ImageBasedLightingSource m_LastSource =
            ImageBasedLightingSource::Count;
        DiffuseEnvironmentSh m_LastSh;
        float m_RadianceScale = 1.f;
        float m_SourceAverageLuminance = 0.f;
        bool m_BrdfReady = false;
        bool m_Uploaded = false;
        bool m_LastNeutralize = false;
        bool m_LastLoadFailed = false;
    };
}
