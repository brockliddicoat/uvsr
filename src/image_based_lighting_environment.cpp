#include "image_based_lighting_environment.h"
#include "renderer_common_passes.h"
#include "renderer_light_probe_processing.h"
#include "renderer_log.h"
#include "renderer_shader_factory.h"

#include <DirectXPackedVector.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
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

    template <typename Source>
    DirectX::XMFLOAT3 SampleLatLongBilinear(
        const Source& source,
        DirectX::XMFLOAT3 direction)
    {
        direction = uvsr::RendererNormalizeEnvironmentDirection(
            direction, { 0.f, 1.f, 0.f });
        float u = std::atan2(direction.z, direction.x) /
            (2.f * DirectX::XM_PI) + 0.5f;
        u -= std::floor(u);
        const float v = std::acos(std::clamp(
            direction.y, -1.f, 1.f)) / DirectX::XM_PI;

        const float sourceX = u * float(source.width) - 0.5f;
        const float sourceY = v * float(source.height) - 0.5f;
        const int32_t x0Unwrapped = int32_t(std::floor(sourceX));
        const int32_t y0Unclamped = int32_t(std::floor(sourceY));
        const float tx = sourceX - std::floor(sourceX);
        const float ty = sourceY - std::floor(sourceY);
        const auto wrapX = [&source](int32_t x)
        {
            const int32_t width = int32_t(source.width);
            x %= width;
            return uint32_t(x < 0 ? x + width : x);
        };
        const auto clampY = [&source](int32_t y)
        {
            return uint32_t(std::clamp(
                y, 0, int32_t(source.height) - 1));
        };
        const uint32_t x0 = wrapX(x0Unwrapped);
        const uint32_t x1 = wrapX(x0Unwrapped + 1);
        const uint32_t y0 = clampY(y0Unclamped);
        const uint32_t y1 = clampY(y0Unclamped + 1);
        const auto load = [&source](uint32_t x, uint32_t y)
        {
            const size_t offset =
                (size_t(y) * size_t(source.width) + size_t(x)) * 3u;
            return DirectX::XMFLOAT3(
                source.pixels[offset + 0u],
                source.pixels[offset + 1u],
                source.pixels[offset + 2u]);
        };
        const DirectX::XMFLOAT3 topLeft = load(x0, y0);
        const DirectX::XMFLOAT3 topRight = load(x1, y0);
        const DirectX::XMFLOAT3 bottomLeft = load(x0, y1);
        const DirectX::XMFLOAT3 bottomRight = load(x1, y1);
        const DirectX::XMVECTOR top = DirectX::XMVectorLerp(
            DirectX::XMLoadFloat3(&topLeft),
            DirectX::XMLoadFloat3(&topRight),
            tx);
        const DirectX::XMVECTOR bottom = DirectX::XMVectorLerp(
            DirectX::XMLoadFloat3(&bottomLeft),
            DirectX::XMLoadFloat3(&bottomRight),
            tx);
        return uvsr::RendererStoreFloat3(
            DirectX::XMVectorLerp(top, bottom, ty));
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
        , m_EnvironmentAssetDirectory(
            std::move(environmentAssetDirectory))
    {
        if (!device || !shaderFactory || !commonPasses)
        {
            m_PreparationState.Fail();
            return;
        }

        nvrhi::TextureDesc radianceDesc;
        radianceDesc.width = RadianceCubeDimension;
        radianceDesc.height = RadianceCubeDimension;
        radianceDesc.arraySize = 6u;
        radianceDesc.mipLevels = RadianceCubeMipCount;
        radianceDesc.format = nvrhi::Format::RGBA16_FLOAT;
        radianceDesc.dimension = nvrhi::TextureDimension::TextureCube;
        radianceDesc.isRenderTarget = true;
        radianceDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        radianceDesc.keepInitialState = true;
        radianceDesc.useClearValue = true;
        radianceDesc.clearValue = nvrhi::Color(0.f);
        radianceDesc.debugName = "UVSR IBL Source Radiance";
        m_RadianceTexture = device->createTexture(radianceDesc);

        nvrhi::TextureDesc diffuseDesc;
        diffuseDesc.width = DiffuseCubeDimension;
        diffuseDesc.height = DiffuseCubeDimension;
        diffuseDesc.arraySize = 6u;
        diffuseDesc.mipLevels = 1u;
        diffuseDesc.format = nvrhi::Format::RGBA16_FLOAT;
        diffuseDesc.dimension =
            nvrhi::TextureDimension::TextureCubeArray;
        diffuseDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        diffuseDesc.keepInitialState = true;
        diffuseDesc.debugName = "UVSR IBL Diffuse Response";
        m_DiffuseTexture = device->createTexture(diffuseDesc);

        nvrhi::TextureDesc specularDesc;
        specularDesc.width = SpecularCubeDimension;
        specularDesc.height = SpecularCubeDimension;
        specularDesc.arraySize = 6u;
        specularDesc.mipLevels = SpecularCubeMipCount;
        specularDesc.format = nvrhi::Format::RGBA16_FLOAT;
        specularDesc.dimension =
            nvrhi::TextureDimension::TextureCubeArray;
        specularDesc.isRenderTarget = true;
        specularDesc.initialState =
            nvrhi::ResourceStates::ShaderResource;
        specularDesc.keepInitialState = true;
        specularDesc.useClearValue = true;
        specularDesc.clearValue = nvrhi::Color(0.f);
        specularDesc.debugName = "UVSR IBL Prefiltered Specular";
        m_SpecularTexture = device->createTexture(specularDesc);

        if (!m_RadianceTexture ||
            !m_DiffuseTexture ||
            !m_SpecularTexture)
        {
            log::error(
                "Failed to create one or more persistent UVSR IBL textures.");
            m_PreparationState.Fail();
            return;
        }

        m_ProbeProcessing =
            std::make_unique<RendererLightProbeProcessing>(
                device,
                shaderFactory,
                commonPasses,
                RadianceCubeDimension,
                nvrhi::Format::RGBA16_FLOAT);
        if (!m_ProbeProcessing->IsValid())
        {
            m_ProbeProcessing.reset();
            log::error("Failed to create UVSR IBL processing resources.");
            m_PreparationState.Fail();
            return;
        }
        m_LightProbe.diffuseMap = m_DiffuseTexture;
        m_LightProbe.specularMap = m_SpecularTexture;
        m_LightProbe.environmentBrdf =
            m_ProbeProcessing->GetEnvironmentBrdfTexture();
        m_LightProbe.diffuseArrayIndex = 0u;
        m_LightProbe.specularArrayIndex = 0u;
        m_LightProbe.diffuseScale = 1.f;
        m_LightProbe.specularScale = 1.f;
    }

    ImageBasedLightingEnvironment::~ImageBasedLightingEnvironment() = default;

    std::optional<ImageBasedLightingEnvironment::PreparedRadiance>
        ImageBasedLightingEnvironment::PrepareRadiance(
            ImageBasedLightingSource source,
            bool neutralize) const
    {
        const ImageBasedLightingSourceInfo& info =
            GetImageBasedLightingSourceInfo(source);
        if (!info.relativePath[0])
            return std::nullopt;

        const std::filesystem::path path =
            m_EnvironmentAssetDirectory / info.relativePath;
        int width = 0;
        int height = 0;
        int sourceChannels = 0;
        float* decoded = stbi_loadf(
            path.string().c_str(),
            &width,
            &height,
            &sourceChannels,
            3);
        if (!decoded)
        {
            log::warning(
                "Could not load IBL environment '%s': %s",
                path.generic_string().c_str(),
                stbi_failure_reason()
                    ? stbi_failure_reason()
                    : "unknown image error");
            return std::nullopt;
        }

        const auto releaseDecoded = [&decoded]()
        {
            stbi_image_free(decoded);
            decoded = nullptr;
        };
        if (width < 4 ||
            height < 2 ||
            std::abs(int64_t(width) - int64_t(height) * 2) > 2)
        {
            log::warning(
                "IBL environment '%s' is not a valid 2:1 lat-long image "
                "(%d x %d, source channels %d).",
                path.generic_string().c_str(),
                width,
                height,
                sourceChannels);
            releaseDecoded();
            return std::nullopt;
        }

        PreparedRadiance imported;
        imported.source = source;
        imported.neutralize = neutralize;
        imported.width = uint32_t(width);
        imported.height = uint32_t(height);
        imported.pixels.resize(
            size_t(width) * size_t(height) * 3u);
        for (size_t pixel = 0u;
            pixel < size_t(width) * size_t(height);
            ++pixel)
        {
            const DirectX::XMFLOAT3 radiance = SanitizeRadiance(
                DirectX::XMFLOAT3(
                    decoded[pixel * 3u + 0u],
                    decoded[pixel * 3u + 1u],
                    decoded[pixel * 3u + 2u]),
                neutralize);
            imported.pixels[pixel * 3u + 0u] = radiance.x;
            imported.pixels[pixel * 3u + 1u] = radiance.y;
            imported.pixels[pixel * 3u + 2u] = radiance.z;
        }
        releaseDecoded();

        const std::optional<RendererDiffuseEnvironmentProjection>
            projection = ProjectRendererDiffuseEnvironmentLatLongRgb(
                imported.pixels.data(),
                imported.width,
                imported.height);
        if (!projection)
        {
            log::warning(
                "IBL environment '%s' did not project to finite positive "
                "diffuse SH9.",
                path.generic_string().c_str());
            return std::nullopt;
        }
        imported.diffuseSh = projection->sh;
        imported.averageLuminance = projection->averageLuminance;

        imported.radianceFaces.resize(
            size_t(6u) * RadianceCubeDimension *
            RadianceCubeDimension);
        for (uint32_t face = 0u; face < 6u; ++face)
        {
            ImageBasedLightingHalf4* destination =
                imported.radianceFaces.data() +
                size_t(face) * RadianceCubeDimension *
                    RadianceCubeDimension;
            for (uint32_t y = 0u;
                y < RadianceCubeDimension;
                ++y)
            {
                for (uint32_t x = 0u;
                    x < RadianceCubeDimension;
                    ++x)
                {
                    const DirectX::XMFLOAT3 direction =
                        RendererEnvironmentCubeDirection(
                            face,
                            x,
                            y,
                            RadianceCubeDimension);
                    const DirectX::XMFLOAT3 radiance =
                        SampleLatLongBilinear(imported, direction);
                    destination[
                        size_t(y) * RadianceCubeDimension + x] =
                        ToHalf4(SanitizeRadiance(radiance, false));
                }
            }
        }

        imported.diffuseFaces.resize(
            size_t(6u) * DiffuseCubeDimension *
            DiffuseCubeDimension);
        for (uint32_t face = 0u; face < 6u; ++face)
        {
            ImageBasedLightingHalf4* destination =
                imported.diffuseFaces.data() +
                size_t(face) * DiffuseCubeDimension *
                    DiffuseCubeDimension;
            for (uint32_t y = 0u;
                y < DiffuseCubeDimension;
                ++y)
            {
                for (uint32_t x = 0u;
                    x < DiffuseCubeDimension;
                    ++x)
                {
                    const DirectX::XMFLOAT3 direction =
                        RendererEnvironmentCubeDirection(
                            face,
                            x,
                            y,
                            DiffuseCubeDimension);
                    const DirectX::XMFLOAT3 response =
                        RendererClampFloat3(
                            EvaluateRendererEnvironmentSh(
                                imported.diffuseSh, direction),
                            0.f,
                            RendererEnvironmentHalfMaximum);
                    destination[
                        y * DiffuseCubeDimension + x] =
                        ToHalf4(response);
                }
            }
        }

        // The lat-long source is no longer needed after CPU cubemap
        // preparation. Release it on the worker rather than carrying another
        // full HDR copy into scene activation.
        std::vector<float>().swap(imported.pixels);
        log::info(
            "Prepared %s IBL source (%d x %d, average luminance %.4f).",
            info.displayName,
            width,
            height,
            imported.averageLuminance);
        return imported;
    }

    void ImageBasedLightingEnvironment::StagePreparedRadiance(
        PreparedRadiance prepared)
    {
        m_PreparedRadiance = std::move(prepared);
        m_PreparedRadianceStage =
            PreparedRadianceGpuStage::EnvironmentBrdf;
        m_PreparedRadianceStep = 0u;
        m_Uploaded = false;
        m_PreparationState.Begin();
    }

    bool ImageBasedLightingEnvironment::AdvancePreparedRadiance(
        nvrhi::ICommandList* commandList)
    {
        const auto fail = [this]()
        {
            m_PreparedRadiance.reset();
            m_PreparedRadianceStage = PreparedRadianceGpuStage::None;
            m_PreparedRadianceStep = 0u;
            m_Uploaded = false;
            m_PreparationState.Fail();
            return false;
        };
        if (!commandList ||
            !m_PreparedRadiance ||
            !m_ProbeProcessing ||
            !m_RadianceTexture ||
            !m_DiffuseTexture ||
            !m_SpecularTexture)
        {
            return fail();
        }

        PreparedRadiance& prepared = *m_PreparedRadiance;
        const size_t expectedRadianceTexels =
            size_t(6u) * RadianceCubeDimension *
            RadianceCubeDimension;
        const size_t expectedDiffuseTexels =
            size_t(6u) * DiffuseCubeDimension *
            DiffuseCubeDimension;
        if (prepared.radianceFaces.size() != expectedRadianceTexels ||
            prepared.diffuseFaces.size() != expectedDiffuseTexels)
        {
            return fail();
        }

        switch (m_PreparedRadianceStage)
        {
        case PreparedRadianceGpuStage::EnvironmentBrdf:
            if (!m_BrdfReady)
            {
                if (!m_ProbeProcessing->RenderEnvironmentBrdfTexture(
                        commandList))
                {
                    return fail();
                }
                m_BrdfReady = true;
            }
            m_PreparedRadianceStage =
                PreparedRadianceGpuStage::RadianceFaceUpload;
            m_PreparedRadianceStep = 0u;
            return false;

        case PreparedRadianceGpuStage::RadianceFaceUpload:
        {
            const uint32_t face = m_PreparedRadianceStep;
            commandList->writeTexture(
                m_RadianceTexture,
                face,
                0u,
                prepared.radianceFaces.data() +
                    size_t(face) * RadianceCubeDimension *
                        RadianceCubeDimension,
                size_t(RadianceCubeDimension) *
                    sizeof(ImageBasedLightingHalf4));
            ++m_PreparedRadianceStep;
            if (m_PreparedRadianceStep == 6u)
            {
                m_PreparedRadianceStage =
                    PreparedRadianceGpuStage::DiffuseUpload;
                m_PreparedRadianceStep = 0u;
            }
            return false;
        }

        case PreparedRadianceGpuStage::DiffuseUpload:
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                commandList->writeTexture(
                    m_DiffuseTexture,
                    face,
                    0u,
                    prepared.diffuseFaces.data() +
                        size_t(face) * DiffuseCubeDimension *
                            DiffuseCubeDimension,
                    size_t(DiffuseCubeDimension) *
                        sizeof(ImageBasedLightingHalf4));
            }
            m_PreparedRadianceStage =
                PreparedRadianceGpuStage::RadianceMipGeneration;
            m_PreparedRadianceStep = 0u;
            return false;

        case PreparedRadianceGpuStage::RadianceMipGeneration:
            if (!m_ProbeProcessing->GenerateCubemapMips(
                    commandList,
                    m_RadianceTexture,
                    0u,
                    m_PreparedRadianceStep,
                    1u))
            {
                return fail();
            }
            ++m_PreparedRadianceStep;
            if (m_PreparedRadianceStep == RadianceCubeMipCount - 1u)
            {
                m_PreparedRadianceStage =
                    PreparedRadianceGpuStage::SpecularBaseBlit;
                m_PreparedRadianceStep = 0u;
            }
            return false;

        case PreparedRadianceGpuStage::SpecularBaseBlit:
            if (!m_ProbeProcessing->BlitCubemap(
                    commandList,
                    m_RadianceTexture,
                    0u,
                    0u,
                    m_SpecularTexture,
                    0u,
                    0u))
            {
                return fail();
            }
            m_PreparedRadianceStage =
                PreparedRadianceGpuStage::SpecularMipGeneration;
            m_PreparedRadianceStep = 1u;
            return false;

        case PreparedRadianceGpuStage::SpecularMipGeneration:
        {
            const uint32_t mip = m_PreparedRadianceStep;
            const float normalizedMip =
                float(mip) / float(SpecularCubeMipCount - 1u);
            const float perceptualRoughness =
                ImageBasedLightingGenerationRoughness(normalizedMip);
            const nvrhi::TextureSubresourceSet radianceSubresources(
                0u,
                RadianceCubeMipCount,
                0u,
                6u);
            if (!m_ProbeProcessing->RenderSpecularMap(
                    commandList,
                    perceptualRoughness,
                    m_RadianceTexture,
                    radianceSubresources,
                    m_SpecularTexture,
                    0u,
                    mip))
            {
                return fail();
            }
            ++m_PreparedRadianceStep;
            if (m_PreparedRadianceStep < SpecularCubeMipCount)
                return false;

            m_Uploaded = true;
            m_PreparationState.Complete();
            m_PreparedRadiance.reset();
            m_PreparedRadianceStage = PreparedRadianceGpuStage::None;
            m_PreparedRadianceStep = 0u;
            return true;
        }

        case PreparedRadianceGpuStage::None:
        default:
            m_PreparationState.Fail();
            return false;
        }
    }

    bool ImageBasedLightingEnvironment::RebuildRadiance(
        nvrhi::ICommandList* commandList,
        const PreparedRadiance& prepared)
    {
        if (!commandList ||
            !m_ProbeProcessing ||
            !m_RadianceTexture ||
            !m_DiffuseTexture ||
            !m_SpecularTexture)
        {
            return false;
        }

        const size_t expectedRadianceTexels =
            size_t(6u) * RadianceCubeDimension *
            RadianceCubeDimension;
        const size_t expectedDiffuseTexels =
            size_t(6u) * DiffuseCubeDimension *
            DiffuseCubeDimension;
        if (prepared.radianceFaces.size() != expectedRadianceTexels ||
            prepared.diffuseFaces.size() != expectedDiffuseTexels)
        {
            return false;
        }

        for (uint32_t face = 0u; face < 6u; ++face)
        {
            commandList->writeTexture(
                m_RadianceTexture,
                face,
                0u,
                prepared.radianceFaces.data() +
                    size_t(face) * RadianceCubeDimension *
                        RadianceCubeDimension,
                size_t(RadianceCubeDimension) *
                    sizeof(ImageBasedLightingHalf4));
        }

        for (uint32_t face = 0u; face < 6u; ++face)
        {
            commandList->writeTexture(
                m_DiffuseTexture,
                face,
                0u,
                prepared.diffuseFaces.data() +
                    size_t(face) * DiffuseCubeDimension *
                        DiffuseCubeDimension,
                size_t(DiffuseCubeDimension) *
                    sizeof(ImageBasedLightingHalf4));
        }

        commandList->beginMarker("UVSR IBL Prefilter");
        if (!m_ProbeProcessing->GenerateCubemapMips(
                commandList,
                m_RadianceTexture,
                0u,
                0u,
                RadianceCubeMipCount - 1u) ||
            !m_ProbeProcessing->BlitCubemap(
                commandList,
                m_RadianceTexture,
                0u,
                0u,
                m_SpecularTexture,
                0u,
                0u))
        {
            commandList->endMarker();
            return false;
        }
        const nvrhi::TextureSubresourceSet radianceSubresources(
            0u,
            RadianceCubeMipCount,
            0u,
            6u);
        for (uint32_t mip = 1u;
            mip < SpecularCubeMipCount;
            ++mip)
        {
            const float normalizedMip =
                float(mip) / float(SpecularCubeMipCount - 1u);
            // The receiver selects sqrt(perceptual roughness), so a squared
            // generation schedule maps each mip to its intended response.
            const float perceptualRoughness =
                ImageBasedLightingGenerationRoughness(normalizedMip);
            if (!m_ProbeProcessing->RenderSpecularMap(
                    commandList,
                    perceptualRoughness,
                    m_RadianceTexture,
                    radianceSubresources,
                    m_SpecularTexture,
                    0u,
                    mip))
            {
                commandList->endMarker();
                return false;
            }
        }
        commandList->endMarker();
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
        if (!commandList)
        {
            m_PreparationState.Fail();
            return false;
        }

        if (!m_ProbeProcessing ||
            !m_LightProbe.diffuseMap ||
            !m_LightProbe.specularMap ||
            !m_LightProbe.environmentBrdf)
        {
            m_PreparedRadiance.reset();
            m_PreparedRadianceStage =
                PreparedRadianceGpuStage::None;
            m_PreparedRadianceStep = 0u;
            m_Uploaded = false;
            m_PreparationState.Fail();
            return false;
        }

        const ImageBasedLightingScales scales =
            ResolveImageBasedLightingScales(
                outputScale,
                exposureStops,
                diffuseEnabled,
                diffuseStrength,
                specularEnabled,
                specularStrength);
        m_RadianceScale = scales.radiance;
        m_LightProbe.diffuseScale = scales.diffuse;
        m_LightProbe.specularScale = scales.specular;

        const ImageBasedLightingSource requestedSource =
            uint32_t(source) < uint32_t(ImageBasedLightingSource::Count)
                ? source
                : ImageBasedLightingSource::Kloppenheim03Day;

        if (m_PreparedRadianceStage !=
            PreparedRadianceGpuStage::None)
        {
            const bool preparedRequestMatches =
                m_PreparedRadiance &&
                m_PreparedRadiance->source == requestedSource &&
                m_PreparedRadiance->neutralize == neutralize;
            if (preparedRequestMatches)
            {
                m_LastRequestedSource = requestedSource;
                m_LastNeutralize = neutralize;
                const bool rebuilt =
                    AdvancePreparedRadiance(commandList);
                return rebuilt;
            }

            // A changed UI request supersedes worker-prepared startup data.
            // Fall through to the existing synchronous path so interactive
            // environment changes retain their immediate behavior.
            m_PreparedRadiance.reset();
            m_PreparedRadianceStage = PreparedRadianceGpuStage::None;
            m_PreparedRadianceStep = 0u;
            m_PreparationState.Begin();
        }

        if (!m_BrdfReady)
        {
            if (!m_ProbeProcessing->RenderEnvironmentBrdfTexture(
                    commandList))
            {
                m_Uploaded = false;
                m_PreparationState.Fail();
                return false;
            }
            m_BrdfReady = true;
        }

        const bool requestChanged =
            m_LastRequestedSource != requestedSource ||
            m_LastNeutralize != neutralize;
        const bool shouldAttemptLoad =
            requestChanged ||
            (!m_Uploaded && !m_PreparationState.HasFailed());
        bool rebuilt = false;
        if (shouldAttemptLoad)
        {
            m_PreparationState.Begin();
            m_LastRequestedSource = requestedSource;
            m_LastNeutralize = neutralize;
            std::optional<PreparedRadiance> imported;
            if (m_PreparedRadiance &&
                m_PreparedRadiance->source == requestedSource &&
                m_PreparedRadiance->neutralize == neutralize)
            {
                imported = std::move(m_PreparedRadiance);
            }
            m_PreparedRadiance.reset();
            if (!imported)
                imported = PrepareRadiance(requestedSource, neutralize);
            if (!imported)
            {
                // A missing or invalid source must never reveal the retired
                // procedural or hemispherical ambient paths. Deactivate the
                // probe and background, then latch the failed request so the
                // render loop does not retry synchronous disk I/O each frame.
                m_Uploaded = false;
                m_PreparationState.Fail();
            }
            else
            {
                rebuilt = RebuildRadiance(commandList, *imported);
                if (rebuilt)
                {
                    m_Uploaded = true;
                    m_PreparationState.Complete();
                }
                else
                {
                    m_Uploaded = false;
                    m_PreparationState.Fail();
                }
            }
        }

        return rebuilt;
    }

    nvrhi::ITexture*
        ImageBasedLightingEnvironment::GetEnvironmentBrdfTexture() const
    {
        return m_BrdfReady && m_ProbeProcessing
            ? m_ProbeProcessing->GetEnvironmentBrdfTexture()
            : nullptr;
    }
}
