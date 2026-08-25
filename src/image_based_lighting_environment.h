#pragma once

#include "image_based_lighting_sources.h"
#include "renderer_environment_math.h"
#include "renderer_gpu_contract.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace uvsr
{
    class RendererCommonPasses;
    class RendererLightProbeProcessing;
    class RendererShaderFactory;

    struct ImageBasedLightingHalf4
    {
        std::uint16_t x = 0u;
        std::uint16_t y = 0u;
        std::uint16_t z = 0u;
        std::uint16_t w = 0u;
    };

    static_assert(sizeof(ImageBasedLightingHalf4) == 8u);

    [[nodiscard]] inline bool IsImageBasedLightingProbeActive(
        bool hasDiffuseMap,
        bool hasSpecularMap,
        bool hasEnvironmentBrdf,
        float diffuseScale,
        float specularScale) noexcept
    {
        return (hasDiffuseMap && std::isfinite(diffuseScale) &&
                diffuseScale > 0.f) ||
            (hasSpecularMap && hasEnvironmentBrdf &&
                std::isfinite(specularScale) && specularScale > 0.f);
    }

    [[nodiscard]] inline LightProbeConstants
        MakeImageBasedLightingProbeConstants(
            std::uint32_t diffuseArrayIndex,
            std::uint32_t specularArrayIndex,
            float diffuseScale,
            float specularScale,
            float specularMipLevels) noexcept
    {
        LightProbeConstants constants{};
        constants.diffuseScale = diffuseScale;
        constants.specularScale = specularScale;
        constants.mipLevels = specularMipLevels;
        constants.diffuseArrayIndex = diffuseArrayIndex;
        constants.specularArrayIndex = specularArrayIndex;
        for (auto& plane : constants.frustumPlanes)
            plane = { 0.f, 0.f, 0.f, 1.f };
        return constants;
    }

    struct ImageBasedLightingProbe
    {
        nvrhi::TextureHandle diffuseMap;
        nvrhi::TextureHandle specularMap;
        nvrhi::TextureHandle environmentBrdf;
        std::uint32_t diffuseArrayIndex = 0u;
        std::uint32_t specularArrayIndex = 0u;
        float diffuseScale = 1.f;
        float specularScale = 1.f;

        [[nodiscard]] bool IsActive() const noexcept;
        void FillLightProbeConstants(LightProbeConstants& constants) const
            noexcept;
    };

    enum class ImageBasedLightingPreparationStatus : std::uint8_t
    {
        Idle,
        Preparing,
        Ready,
        Failed
    };

    // One explicit state prevents "no active stage" from disguising a failed
    // upload as ready. Production owns this state and tests drive the same
    // transitions without requiring a graphics device.
    class ImageBasedLightingPreparationState
    {
    public:
        void Begin() noexcept
        {
            m_Status = ImageBasedLightingPreparationStatus::Preparing;
        }

        void Complete() noexcept
        {
            m_Status = m_Status ==
                    ImageBasedLightingPreparationStatus::Preparing
                ? ImageBasedLightingPreparationStatus::Ready
                : ImageBasedLightingPreparationStatus::Failed;
        }

        void Fail() noexcept
        {
            m_Status = ImageBasedLightingPreparationStatus::Failed;
        }

        [[nodiscard]] ImageBasedLightingPreparationStatus Get() const noexcept
        {
            return m_Status;
        }

        [[nodiscard]] bool IsReady() const noexcept
        {
            return m_Status == ImageBasedLightingPreparationStatus::Ready;
        }

        [[nodiscard]] bool HasFailed() const noexcept
        {
            return m_Status == ImageBasedLightingPreparationStatus::Failed;
        }

    private:
        ImageBasedLightingPreparationStatus m_Status =
            ImageBasedLightingPreparationStatus::Idle;
    };

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
            std::vector<ImageBasedLightingHalf4> radianceFaces;
            std::vector<ImageBasedLightingHalf4> diffuseFaces;
            RendererDiffuseEnvironmentSh diffuseSh;
            float averageLuminance = 0.f;
        };

        ImageBasedLightingEnvironment(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>&
                shaderFactory,
            const std::shared_ptr<RendererCommonPasses>&
                commonPasses,
            std::filesystem::path environmentAssetDirectory);

        ~ImageBasedLightingEnvironment();

        // CPU-only decode, validation, SH projection, and cubemap resampling.
        // UVSR runs this on its scene worker before the first rendered frame.
        [[nodiscard]] std::optional<PreparedRadiance> PrepareRadiance(
            ImageBasedLightingSource source,
            bool neutralize) const;

        void StagePreparedRadiance(PreparedRadiance prepared);

        // True only after the selected radiance and all derived maps exist.
        // Idle, in-progress, and failed preparation are deliberately distinct.
        [[nodiscard]] bool IsPreparedRadianceReady() const
        {
            return m_PreparationState.IsReady();
        }

        [[nodiscard]] bool HasPreparedRadianceFailed() const noexcept
        {
            return m_PreparationState.HasFailed();
        }

        [[nodiscard]] ImageBasedLightingPreparationStatus
            GetPreparedRadianceStatus() const noexcept
        {
            return m_PreparationState.Get();
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

        [[nodiscard]] const ImageBasedLightingProbe* GetLightProbe() const
        {
            return m_Uploaded && m_LightProbe.IsActive()
                ? &m_LightProbe
                : nullptr;
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
        std::unique_ptr<RendererLightProbeProcessing> m_ProbeProcessing;
        nvrhi::TextureHandle m_RadianceTexture;
        nvrhi::TextureHandle m_DiffuseTexture;
        nvrhi::TextureHandle m_SpecularTexture;
        ImageBasedLightingProbe m_LightProbe;
        std::filesystem::path m_EnvironmentAssetDirectory;
        ImageBasedLightingSource m_LastRequestedSource =
            ImageBasedLightingSource::Count;
        float m_RadianceScale = 1.f;
        bool m_BrdfReady = false;
        bool m_Uploaded = false;
        bool m_LastNeutralize = false;
        ImageBasedLightingPreparationState m_PreparationState;
        std::optional<PreparedRadiance> m_PreparedRadiance;
        PreparedRadianceGpuStage m_PreparedRadianceStage =
            PreparedRadianceGpuStage::None;
        uint32_t m_PreparedRadianceStep = 0u;
    };
}
