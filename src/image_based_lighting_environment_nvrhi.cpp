#include "image_based_lighting_environment_nvrhi.h"
#include "renderer_light_probe_processing_nvrhi.h"
#include "renderer_log.h"

#include "image_based_lighting_asset_path.h"
#include "image_based_lighting_shared.h"
#include <new>

namespace
{
    nvrhi::TextureHandle CreateEnvironmentCube(
        nvrhi::IDevice* device,
        uint32_t size,
        uint32_t mipLevels,
        nvrhi::TextureDimension dimension,
        const char* name)
    {
        nvrhi::TextureDesc description;
        description.width = description.height = size;
        description.arraySize = 6u;
        description.mipLevels = mipLevels;
        description.format = nvrhi::Format::RGBA16_FLOAT;
        description.dimension = dimension;
        description.isRenderTarget = mipLevels > 1u;
        description.initialState = nvrhi::ResourceStates::ShaderResource;
        description.keepInitialState = true;
        description.useClearValue = description.isRenderTarget;
        description.clearValue = nvrhi::Color(0.f);
        description.debugName = name;
        return device->createTexture(description);
    }

}

namespace uvsr
{
    bool ImageBasedLightingProbe::IsActive() const noexcept
    {
        return IsImageBasedLightingProbeActive(
            bool(diffuseMap),
            bool(specularMap),
            bool(environmentBrdf),
            diffuseScale,
            specularScale);
    }

    void ImageBasedLightingProbe::FillLightProbeConstants(
        LightProbeConstants& constants) const noexcept
    {
        constants = MakeImageBasedLightingProbeConstants(
            diffuseArrayIndex,
            specularArrayIndex,
            diffuseScale,
            specularScale,
            specularMap ? float(specularMap->getDesc().mipLevels) : 0.f);
    }

    ImageBasedLightingEnvironment::ImageBasedLightingEnvironment(
        nvrhi::IDevice* device,
        RendererShaderFactory* shaderFactory,
        RendererCommonPasses* commonPasses,
        WindowsPath environmentAssetDirectory)
        : m_Device(device)
        , m_EnvironmentAssetDirectory(std::move(environmentAssetDirectory))
    {
        if (!device || !shaderFactory || !commonPasses)
        {
            m_PreparationState.Fail();
            return;
        }
        m_RadianceTexture = CreateEnvironmentCube(
            device, RadianceCubeDimension, RadianceCubeMipCount,
            nvrhi::TextureDimension::TextureCube, "UVSR IBL Source Radiance");
        m_ProbeProcessing.reset(new (std::nothrow) RendererLightProbeProcessing(
            device, shaderFactory, commonPasses,
            RadianceCubeDimension, nvrhi::Format::RGBA16_FLOAT));
        if (!m_RadianceTexture || !m_ProbeProcessing || !m_ProbeProcessing->IsValid())
        {
            log::error("Failed to create UVSR IBL radiance resources.");
            m_PreparationState.Fail();
        }
    }

    ImageBasedLightingEnvironment::~ImageBasedLightingEnvironment() = default;

    ImageBasedLightingEnvironment::PrepareResult ImageBasedLightingEnvironment::PrepareRadiance(
        ImageBasedLightingSource source, bool neutralize, PreparedImageBasedLightingRadiance& output) const
    {
        const ImageBasedLightingSourceInfo& info = GetImageBasedLightingSourceInfo(source);
        if (!info.relativePath[0]) return PrepareResult::Unavailable;
        ImageBasedLightingAssetPath path;
        ImageBasedLightingPathResult pathResult;
        const auto pathFailure = [&]()
        {
            const PrepareResult result = pathResult.error == ImageBasedLightingPathError::Allocation ?
                PrepareResult::AllocationFailure : PrepareResult::PathFailure;
            log::error("%s System code %u.", PreparationFailureText(result), pathResult.nativeCode);
            return result;
        };
        if (!path.Prepare(m_EnvironmentAssetDirectory.Data(), source, pathResult)) return pathFailure();
        const auto result = PrepareImageBasedLightingRadiance(path.Text(), source, neutralize, output);
        if (result.error == ImageBasedLightingRadianceError::Allocation)
        {
            log::error("%s", PreparationFailureText(PrepareResult::AllocationFailure));
            return PrepareResult::AllocationFailure;
        }
        if (result.Unavailable())
        {
            if (!path.MakeGeneric(pathResult)) return pathFailure();
            switch (result.error)
            {
            case ImageBasedLightingRadianceError::Decode:
                log::warning("Could not load IBL environment '%s': %s", path.Text(),
                    result.decoderReason ? result.decoderReason : "unknown image error");
                break;
            case ImageBasedLightingRadianceError::Dimensions:
                log::warning("IBL environment '%s' is not a valid 2:1 lat-long image "
                    "(%d x %d, source channels %d).", path.Text(), result.width, result.height, result.sourceChannels);
                break;
            case ImageBasedLightingRadianceError::Projection:
                log::warning("IBL environment '%s' did not project to finite positive diffuse SH9.", path.Text());
                break;
            default: break;
            }
            return PrepareResult::Unavailable;
        }
        log::info("Prepared %s IBL source (%d x %d, average luminance %.4f).",
            info.displayName, result.width, result.height, result.averageLuminance);
        return PrepareResult::Prepared;
    }

    void ImageBasedLightingEnvironment::StagePreparedRadiance(PreparedImageBasedLightingRadiance prepared)
    {
        if (!prepared)
        {
            (void)FailPreparation();
            return;
        }
        m_LastRequestedSource = prepared.Source();
        m_LastNeutralize = prepared.Neutralize();
        m_DiffuseSh = prepared.DiffuseSh();
        m_PreparedRadiance = std::move(prepared);
        m_PreparedRadianceStep = m_SpecularMip = 0u;
        m_Uploaded = m_DiffuseReady = false;
        m_PreparationState.Begin();
    }

    bool ImageBasedLightingEnvironment::FailPreparation()
    {
        m_PreparedRadiance.Clear();
        m_Uploaded = m_DiffuseReady = false;
        m_SpecularMip = 0u;
        m_LightProbe.environmentBrdf = nullptr;
        m_PreparationState.Fail();
        return false;
    }

    bool ImageBasedLightingEnvironment::AdvancePreparedRadiance(nvrhi::ICommandList* commandList)
    {
        if (!commandList || !m_ProbeProcessing || !m_ProbeProcessing->IsValid() || !m_RadianceTexture)
            return FailPreparation();

        if (m_PreparedRadiance)
        {
            if (m_PreparedRadiance.Faces().Size() !=
                size_t(6u) * RadianceCubeDimension * RadianceCubeDimension)
                return FailPreparation();

            if (m_PreparedRadianceStep < 6u)
            {
                commandList->writeTexture(m_RadianceTexture, m_PreparedRadianceStep, 0u,
                    m_PreparedRadiance.Faces().Data() +
                        size_t(m_PreparedRadianceStep) * RadianceCubeDimension * RadianceCubeDimension,
                    size_t(RadianceCubeDimension) * sizeof(ImageBasedLightingHalf4));
            }
            else if (!m_ProbeProcessing->GenerateCubemapMips(commandList,
                m_RadianceTexture, 0u, m_PreparedRadianceStep - 6u, 1u))
            {
                return FailPreparation();
            }
            if (++m_PreparedRadianceStep == 6u + RadianceCubeMipCount - 1u)
            {
                m_PreparedRadiance.Clear();
                m_Uploaded = true;
            }
            return false;
        }
        if (!m_Uploaded)
            return FailPreparation();

        if (m_LightProbe.diffuseScale > 0.f && !m_DiffuseReady)
        {
            if (!m_LightProbe.diffuseMap)
                m_LightProbe.diffuseMap = CreateEnvironmentCube(m_Device,
                    DiffuseCubeDimension, 1u, nvrhi::TextureDimension::TextureCubeArray,
                    "UVSR IBL Diffuse Response");
            if (!m_LightProbe.diffuseMap)
                return FailPreparation();
            ImageBasedLightingFaces faces;
            if (!PrepareImageBasedLightingDiffuseFaces(m_DiffuseSh, faces))
            {
                log::error("%s", PreparationFailureText(PrepareResult::AllocationFailure));
                return FailPreparation();
            }
            for (uint32_t face = 0u; face < 6u; ++face)
                commandList->writeTexture(m_LightProbe.diffuseMap, face, 0u,
                    faces.Data() + size_t(face) * DiffuseCubeDimension * DiffuseCubeDimension,
                    size_t(DiffuseCubeDimension) * sizeof(ImageBasedLightingHalf4));
            m_DiffuseReady = true;
            return false;
        }
        if (m_LightProbe.specularScale > 0.f)
        {
            if (!m_LightProbe.environmentBrdf)
            {
                if (!m_ProbeProcessing->RenderEnvironmentBrdfTexture(commandList))
                    return FailPreparation();
                m_LightProbe.environmentBrdf = m_ProbeProcessing->GetEnvironmentBrdfTexture();
                return false;
            }
            if (m_SpecularMip < SpecularCubeMipCount)
            {
                if (!m_LightProbe.specularMap)
                    m_LightProbe.specularMap = CreateEnvironmentCube(m_Device,
                        SpecularCubeDimension, SpecularCubeMipCount,
                        nvrhi::TextureDimension::TextureCubeArray, "UVSR IBL Prefiltered Specular");
                if (!m_LightProbe.specularMap)
                    return FailPreparation();
                const bool succeeded = m_SpecularMip == 0u
                    ? m_ProbeProcessing->BlitCubemap(commandList,
                        m_RadianceTexture, 0u, 0u, m_LightProbe.specularMap, 0u, 0u)
                    : m_ProbeProcessing->RenderSpecularMap(commandList,
                        ImageBasedLightingGenerationRoughness(float(m_SpecularMip) /
                            float(SpecularCubeMipCount - 1u)),
                        m_RadianceTexture, nvrhi::TextureSubresourceSet(0u, RadianceCubeMipCount, 0u, 6u),
                        m_LightProbe.specularMap, 0u, m_SpecularMip);
                if (!succeeded)
                    return FailPreparation();
                ++m_SpecularMip;
                return false;
            }
        }
        m_PreparationState.Complete();
        return true;
    }

    bool ImageBasedLightingEnvironment::Update(
        nvrhi::ICommandList* commandList,
        bool neutralize,
        float outputScale,
        float exposureStops,
        bool diffuseEnabled,
        float diffuseStrength,
        bool specularEnabled,
        float specularStrength,
        ImageBasedLightingSource source)
    {
        if (!commandList || !m_ProbeProcessing || !m_ProbeProcessing->IsValid() || !m_RadianceTexture)
            return FailPreparation();

        const ImageBasedLightingScales scales = ResolveImageBasedLightingScales(
            outputScale, exposureStops, diffuseEnabled, diffuseStrength, specularEnabled, specularStrength);
        m_RadianceScale = scales.radiance;
        m_LightProbe.diffuseScale = scales.diffuse;
        m_LightProbe.specularScale = scales.specular;
        const ImageBasedLightingSource requestedSource =
            uint32_t(source) < uint32_t(ImageBasedLightingSource::Count)
                ? source : ImageBasedLightingSource::Kloppenheim03Day;
        const bool requestChanged = m_LastRequestedSource != requestedSource || m_LastNeutralize != neutralize;

        // Worker preparation advances one GPU step per loading frame.
        if (!requestChanged && m_PreparationState.Get() == ImageBasedLightingPreparationStatus::Preparing)
            return AdvancePreparedRadiance(commandList);

        if (requestChanged || (!m_Uploaded && !m_PreparationState.HasFailed()))
        {
            m_LastRequestedSource = requestedSource;
            m_LastNeutralize = neutralize;
            PreparedImageBasedLightingRadiance prepared;
            if (PrepareRadiance(requestedSource, neutralize, prepared) != PrepareResult::Prepared)
                return FailPreparation();
            StagePreparedRadiance(std::move(prepared));
        }
        else if (m_PreparationState.HasFailed())
        {
            return false;
        }
        else if ((scales.diffuse > 0.f && !m_DiffuseReady) ||
            (scales.specular > 0.f && m_SpecularMip < SpecularCubeMipCount))
        {
            m_PreparationState.Begin();
        }
        else
        {
            return false;
        }

        // Interactive changes use the same steps and complete in this command list.
        while (m_PreparationState.Get() == ImageBasedLightingPreparationStatus::Preparing)
            if (AdvancePreparedRadiance(commandList))
                return true;
        return false;
    }
}
