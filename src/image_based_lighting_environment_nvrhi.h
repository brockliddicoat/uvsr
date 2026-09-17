#pragma once

#include "image_based_lighting_environment.h"
#include "image_based_lighting_sources.h"
#include "image_based_lighting_radiance.h"
#include "windows_executable_path.h"
#include <nvrhi/nvrhi.h>
#include <memory>

namespace uvsr
{
    class RendererCommonPasses;
    class RendererLightProbeProcessing;
    class RendererShaderFactory;

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

    class ImageBasedLightingEnvironment
    {
    public:
        static constexpr uint32_t DiffuseCubeDimension = ImageBasedLightingDiffuseDimension;
        static constexpr uint32_t RadianceCubeDimension = ImageBasedLightingRadianceDimension;
        static constexpr uint32_t RadianceCubeMipCount = 10u;
        static constexpr uint32_t SpecularCubeDimension = 256u;
        static constexpr uint32_t SpecularCubeMipCount = 9u;

        enum class PrepareResult { Prepared, Unavailable, PathFailure, AllocationFailure };
        [[nodiscard]] static const char* PreparationFailureText(PrepareResult result) noexcept
        {
            return result == PrepareResult::AllocationFailure ? "IBL preparation could not allocate CPU storage." :
                result == PrepareResult::PathFailure ? "IBL preparation could not encode its asset path." : "";
        }

        ImageBasedLightingEnvironment(
            nvrhi::IDevice* device,
            RendererShaderFactory*
                shaderFactory,
            RendererCommonPasses*
                commonPasses,
            WindowsPath environmentAssetDirectory);

        ~ImageBasedLightingEnvironment();

        // unavailable content leaves output unchanged and may accompany a
        // successful scene import. checked path/storage failure rejects it.
        [[nodiscard]] PrepareResult PrepareRadiance(ImageBasedLightingSource source,
            bool neutralize, PreparedImageBasedLightingRadiance& output) const;

        // a nonempty prepared owner contains the complete fixed radiance table;
        // staging that state allocates nothing and cannot reject it.
        void StagePreparedRadiance(PreparedImageBasedLightingRadiance prepared);

        // True only after the selected radiance and requested maps exist.
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

        // Returns true after preparing radiance or a newly requested lobe.
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

        [[nodiscard]] const ImageBasedLightingProbe* GetLightProbe() const
        {
            return m_PreparationState.IsReady() && m_LightProbe.IsActive()
                ? &m_LightProbe
                : nullptr;
        }

        [[nodiscard]] float GetRadianceScale() const
        {
            return m_RadianceScale;
        }

    private:
        bool AdvancePreparedRadiance(
            nvrhi::ICommandList* commandList);
        bool FailPreparation();

        nvrhi::DeviceHandle m_Device;
        std::unique_ptr<RendererLightProbeProcessing> m_ProbeProcessing;
        nvrhi::TextureHandle m_RadianceTexture;
        ImageBasedLightingProbe m_LightProbe;
        RendererDiffuseEnvironmentSh m_DiffuseSh;
        WindowsPath m_EnvironmentAssetDirectory;
        ImageBasedLightingSource m_LastRequestedSource =
            ImageBasedLightingSource::Count;
        float m_RadianceScale = 1.f;
        bool m_DiffuseReady = false;
        bool m_Uploaded = false;
        bool m_LastNeutralize = false;
        ImageBasedLightingPreparationState m_PreparationState;
        PreparedImageBasedLightingRadiance m_PreparedRadiance;
        uint32_t m_PreparedRadianceStep = 0u;
        uint32_t m_SpecularMip = 0u;
    };
}
