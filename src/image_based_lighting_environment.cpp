#include "image_based_lighting_environment.h"
#include "renderer_light_probe_processing.h"
#include "renderer_log.h"

#include <DirectXPackedVector.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>

#include "image_based_lighting_shared.h"

namespace
{
    const DirectX::XMFLOAT3 LuminanceWeights(
        0.2126f, 0.7152f, 0.0722f);

    DirectX::XMFLOAT3 SanitizeRadiance(
        DirectX::XMFLOAT3 value,
        bool neutralize)
    {
        value = uvsr::RendererClampFloat3(
            value, 0.f, uvsr::RendererEnvironmentHalfMaximum);
        if (neutralize)
        {
            const float luminance = DirectX::XMVectorGetX(
                DirectX::XMVector3Dot(
                    DirectX::XMLoadFloat3(&value),
                    DirectX::XMLoadFloat3(&LuminanceWeights)));
            value = { luminance, luminance, luminance };
        }
        return value;
    }

    uvsr::ImageBasedLightingHalf4 ToHalf4(DirectX::XMFLOAT3 value)
    {
        using DirectX::PackedVector::XMConvertFloatToHalf;
        return {
            XMConvertFloatToHalf(value.x),
            XMConvertFloatToHalf(value.y),
            XMConvertFloatToHalf(value.z),
            XMConvertFloatToHalf(0.f)
        };
    }

    DirectX::XMFLOAT3 SampleLatLongBilinear(
        const float* pixels,
        uint32_t width,
        uint32_t height,
        DirectX::XMFLOAT3 direction)
    {
        direction = uvsr::RendererNormalizeEnvironmentDirection(
            direction, { 0.f, 1.f, 0.f });
        float u = std::atan2(direction.z, direction.x) /
            (2.f * DirectX::XM_PI) + 0.5f;
        u -= std::floor(u);
        const float v = std::acos(std::clamp(
            direction.y, -1.f, 1.f)) / DirectX::XM_PI;
        const float sourceX = u * float(width) - 0.5f;
        const float sourceY = v * float(height) - 0.5f;
        const int32_t x0 = int32_t(std::floor(sourceX));
        const int32_t y0 = int32_t(std::floor(sourceY));
        const float tx = sourceX - std::floor(sourceX);
        const float ty = sourceY - std::floor(sourceY);
        const auto load = [=](int32_t x, int32_t y)
        {
            x %= int32_t(width);
            if (x < 0)
                x += int32_t(width);
            y = std::clamp(y, 0, int32_t(height) - 1);
            const size_t offset = (size_t(y) * width + uint32_t(x)) * 3u;
            return DirectX::XMVectorSet(
                pixels[offset], pixels[offset + 1u], pixels[offset + 2u], 0.f);
        };
        return uvsr::RendererStoreFloat3(DirectX::XMVectorLerp(
            DirectX::XMVectorLerp(load(x0, y0), load(x0 + 1, y0), tx),
            DirectX::XMVectorLerp(load(x0, y0 + 1), load(x0 + 1, y0 + 1), tx),
            ty));
    }

    template <typename Sample>
    std::vector<uvsr::ImageBasedLightingHalf4> MakeCubemapFaces(
        uint32_t size, const Sample& sample)
    {
        std::vector<uvsr::ImageBasedLightingHalf4> faces(size_t(6u) * size * size);
        for (uint32_t face = 0u; face < 6u; ++face)
            for (uint32_t y = 0u; y < size; ++y)
                for (uint32_t x = 0u; x < size; ++x)
                    faces[(size_t(face) * size + y) * size + x] = ToHalf4(
                        SanitizeRadiance(sample(uvsr::RendererEnvironmentCubeDirection(
                            face, x, y, size)), false));
        return faces;
    }

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
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
        const std::shared_ptr<RendererCommonPasses>& commonPasses,
        std::filesystem::path environmentAssetDirectory)
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
        m_ProbeProcessing = std::make_unique<RendererLightProbeProcessing>(
            device, shaderFactory, commonPasses,
            RadianceCubeDimension, nvrhi::Format::RGBA16_FLOAT);
        if (!m_RadianceTexture || !m_ProbeProcessing->IsValid())
        {
            log::error("Failed to create UVSR IBL radiance resources.");
            m_PreparationState.Fail();
        }
    }

    ImageBasedLightingEnvironment::~ImageBasedLightingEnvironment() = default;

    std::optional<ImageBasedLightingEnvironment::PreparedRadiance>
        ImageBasedLightingEnvironment::PrepareRadiance(
            ImageBasedLightingSource source,
            bool neutralize) const
    {
        const ImageBasedLightingSourceInfo& info = GetImageBasedLightingSourceInfo(source);
        if (!info.relativePath[0])
            return std::nullopt;

        const std::filesystem::path path = m_EnvironmentAssetDirectory / info.relativePath;
        int width = 0;
        int height = 0;
        int sourceChannels = 0;
        std::unique_ptr<float, decltype(&stbi_image_free)> decoded(
            stbi_loadf(path.string().c_str(), &width, &height, &sourceChannels, 3),
            &stbi_image_free);
        if (!decoded)
        {
            log::warning("Could not load IBL environment '%s': %s",
                path.generic_string().c_str(),
                stbi_failure_reason() ? stbi_failure_reason() : "unknown image error");
            return std::nullopt;
        }
        if (width < 4 || height < 2 ||
            std::abs(int64_t(width) - int64_t(height) * 2) > 2)
        {
            log::warning("IBL environment '%s' is not a valid 2:1 lat-long image "
                "(%d x %d, source channels %d).",
                path.generic_string().c_str(), width, height, sourceChannels);
            return std::nullopt;
        }

        float* pixels = decoded.get();
        for (size_t pixel = 0u; pixel < size_t(width) * size_t(height); ++pixel)
        {
            const DirectX::XMFLOAT3 radiance = SanitizeRadiance(
                { pixels[pixel * 3u], pixels[pixel * 3u + 1u], pixels[pixel * 3u + 2u] },
                neutralize);
            pixels[pixel * 3u] = radiance.x;
            pixels[pixel * 3u + 1u] = radiance.y;
            pixels[pixel * 3u + 2u] = radiance.z;
        }
        const auto projection = ProjectRendererDiffuseEnvironmentLatLongRgb(
            pixels, uint32_t(width), uint32_t(height));
        if (!projection)
        {
            log::warning("IBL environment '%s' did not project to finite positive diffuse SH9.",
                path.generic_string().c_str());
            return std::nullopt;
        }

        PreparedRadiance prepared;
        prepared.source = source;
        prepared.neutralize = neutralize;
        prepared.diffuseSh = projection->sh;
        prepared.radianceFaces = MakeCubemapFaces(RadianceCubeDimension,
            [&](DirectX::XMFLOAT3 direction)
            {
                return SampleLatLongBilinear(pixels, uint32_t(width), uint32_t(height), direction);
            });
        log::info("Prepared %s IBL source (%d x %d, average luminance %.4f).",
            info.displayName, width, height, projection->averageLuminance);
        return prepared;
    }

    void ImageBasedLightingEnvironment::StagePreparedRadiance(PreparedRadiance prepared)
    {
        m_LastRequestedSource = prepared.source;
        m_LastNeutralize = prepared.neutralize;
        m_DiffuseSh = prepared.diffuseSh;
        m_PreparedRadiance = std::move(prepared);
        m_PreparedRadianceStep = m_SpecularMip = 0u;
        m_Uploaded = m_DiffuseReady = false;
        m_PreparationState.Begin();
    }

    bool ImageBasedLightingEnvironment::FailPreparation()
    {
        m_PreparedRadiance.reset();
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
            if (m_PreparedRadiance->radianceFaces.size() !=
                size_t(6u) * RadianceCubeDimension * RadianceCubeDimension)
                return FailPreparation();

            if (m_PreparedRadianceStep < 6u)
            {
                commandList->writeTexture(m_RadianceTexture, m_PreparedRadianceStep, 0u,
                    m_PreparedRadiance->radianceFaces.data() +
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
                m_PreparedRadiance.reset();
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
            const auto faces = MakeCubemapFaces(DiffuseCubeDimension,
                [this](DirectX::XMFLOAT3 direction)
                {
                    return EvaluateRendererEnvironmentSh(m_DiffuseSh, direction);
                });
            for (uint32_t face = 0u; face < 6u; ++face)
                commandList->writeTexture(m_LightProbe.diffuseMap, face, 0u,
                    faces.data() + size_t(face) * DiffuseCubeDimension * DiffuseCubeDimension,
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
            auto prepared = PrepareRadiance(requestedSource, neutralize);
            if (!prepared)
                return FailPreparation();
            StagePreparedRadiance(std::move(*prepared));
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
