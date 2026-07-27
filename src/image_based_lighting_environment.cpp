#include "image_based_lighting_environment.h"

#include <donut/core/log.h>
#include <donut/core/math/float.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/render/LightProbeProcessingPass.h>

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "image_based_lighting_shared.h"

using namespace donut;
using namespace donut::engine;
using namespace donut::math;
using namespace donut::render;

namespace
{
    constexpr dm::float3 LuminanceWeights(
        0.2126f, 0.7152f, 0.0722f);

    dm::float3 SanitizeRadiance(dm::float3 value, bool neutralize)
    {
        value = min(
            max(uvsr::SanitizeFinite(value), dm::float3(0.f)),
            dm::float3(uvsr::DiffuseEnvironmentHalfMaximum));
        if (neutralize)
            value = dot(value, LuminanceWeights);
        return value;
    }

    template <typename Source>
    dm::float3 SampleLatLongBilinear(
        const Source& source,
        dm::float3 direction)
    {
        direction = uvsr::NormalizeDiffuseEnvironmentDirection(
            direction, dm::float3(0.f, 1.f, 0.f));
        float u = std::atan2(direction.z, direction.x) /
            (2.f * dm::PI_f) + 0.5f;
        u -= std::floor(u);
        const float v = std::acos(std::clamp(
            direction.y, -1.f, 1.f)) / dm::PI_f;

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
            return dm::float3(
                source.pixels[offset + 0u],
                source.pixels[offset + 1u],
                source.pixels[offset + 2u]);
        };
        return lerp(
            lerp(load(x0, y0), load(x1, y0), tx),
            lerp(load(x0, y1), load(x1, y1), tx),
            ty);
    }
}

namespace uvsr
{
    ImageBasedLightingEnvironment::ImageBasedLightingEnvironment(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        std::filesystem::path environmentAssetDirectory)
        : m_Device(device)
        , m_EnvironmentAssetDirectory(
            std::move(environmentAssetDirectory))
    {
        if (!device || !shaderFactory || !commonPasses)
            return;

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
            return;
        }

        m_ProbeProcessing =
            std::make_shared<LightProbeProcessingPass>(
                device,
                shaderFactory,
                commonPasses,
                RadianceCubeDimension,
                nvrhi::Format::RGBA16_FLOAT);
        m_LightProbe = std::make_shared<LightProbe>();
        m_LightProbe->name = "UVSR Global Image-Based Lighting";
        m_LightProbe->diffuseMap = m_DiffuseTexture;
        m_LightProbe->specularMap = m_SpecularTexture;
        m_LightProbe->environmentBrdf =
            m_ProbeProcessing->GetEnvironmentBrdfTexture();
        m_LightProbe->diffuseArrayIndex = 0u;
        m_LightProbe->specularArrayIndex = 0u;
        m_LightProbe->diffuseScale = 1.f;
        m_LightProbe->specularScale = 1.f;
    }

    std::optional<ImageBasedLightingEnvironment::ImportedRadiance>
        ImageBasedLightingEnvironment::LoadImportedRadiance(
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

        ImportedRadiance imported;
        imported.width = uint32_t(width);
        imported.height = uint32_t(height);
        imported.pixels.resize(
            size_t(width) * size_t(height) * 3u);
        for (size_t pixel = 0u;
            pixel < size_t(width) * size_t(height);
            ++pixel)
        {
            const dm::float3 radiance = SanitizeRadiance(
                dm::float3(
                    decoded[pixel * 3u + 0u],
                    decoded[pixel * 3u + 1u],
                    decoded[pixel * 3u + 2u]),
                neutralize);
            imported.pixels[pixel * 3u + 0u] = radiance.x;
            imported.pixels[pixel * 3u + 1u] = radiance.y;
            imported.pixels[pixel * 3u + 2u] = radiance.z;
        }
        releaseDecoded();

        const std::optional<ImportedDiffuseEnvironmentProjection>
            projection = ProjectDiffuseEnvironmentLatLongRgb(
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
        log::info(
            "Loaded %s IBL source (%d x %d, average luminance %.4f).",
            info.displayName,
            width,
            height,
            imported.averageLuminance);
        return imported;
    }

    bool ImageBasedLightingEnvironment::RebuildRadiance(
        nvrhi::ICommandList* commandList,
        const ImportedRadiance& imported)
    {
        if (!commandList ||
            !m_ProbeProcessing ||
            !m_RadianceTexture ||
            !m_DiffuseTexture ||
            !m_SpecularTexture)
        {
            return false;
        }

        m_LastSh = imported.diffuseSh;
        m_SourceAverageLuminance = imported.averageLuminance;

        std::vector<float16_t4> radianceFace(
            size_t(RadianceCubeDimension) *
            size_t(RadianceCubeDimension));
        for (uint32_t face = 0u; face < 6u; ++face)
        {
            for (uint32_t y = 0u;
                y < RadianceCubeDimension;
                ++y)
            {
                for (uint32_t x = 0u;
                    x < RadianceCubeDimension;
                    ++x)
                {
                    const dm::float3 direction =
                        DiffuseEnvironmentCubeDirection(
                            face,
                            x,
                            y,
                            RadianceCubeDimension);
                    const dm::float3 radiance =
                        SampleLatLongBilinear(imported, direction);
                    radianceFace[
                        size_t(y) * RadianceCubeDimension + x] =
                        Float32ToFloat16x4(float4(
                            SanitizeRadiance(radiance, false),
                            0.f));
                }
            }
            commandList->writeTexture(
                m_RadianceTexture,
                face,
                0u,
                radianceFace.data(),
                size_t(RadianceCubeDimension) *
                    sizeof(float16_t4));
        }

        std::array<
            float16_t4,
            DiffuseCubeDimension * DiffuseCubeDimension>
            diffuseFace{};
        for (uint32_t face = 0u; face < 6u; ++face)
        {
            for (uint32_t y = 0u;
                y < DiffuseCubeDimension;
                ++y)
            {
                for (uint32_t x = 0u;
                    x < DiffuseCubeDimension;
                    ++x)
                {
                    const dm::float3 direction =
                        DiffuseEnvironmentCubeDirection(
                            face,
                            x,
                            y,
                            DiffuseCubeDimension);
                    const dm::float3 response =
                        ClampDiffuseEnvironmentForHalf(
                            EvaluateDiffuseEnvironmentSh(
                                m_LastSh, direction));
                    diffuseFace[
                        y * DiffuseCubeDimension + x] =
                        Float32ToFloat16x4(float4(response, 0.f));
                }
            }
            commandList->writeTexture(
                m_DiffuseTexture,
                face,
                0u,
                diffuseFace.data(),
                size_t(DiffuseCubeDimension) *
                    sizeof(float16_t4));
        }

        commandList->beginMarker("UVSR IBL Prefilter");
        m_ProbeProcessing->GenerateCubemapMips(
            commandList,
            m_RadianceTexture,
            0u,
            0u,
            RadianceCubeMipCount - 1u);
        m_ProbeProcessing->BlitCubemap(
            commandList,
            m_RadianceTexture,
            0u,
            0u,
            m_SpecularTexture,
            0u,
            0u);
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
            // Donut's receiver selects sqrt(perceptual roughness), so a
            // squared generation schedule makes each mip represent the
            // intended perceptual roughness exactly.
            const float perceptualRoughness =
                ImageBasedLightingGenerationRoughness(normalizedMip);
            m_ProbeProcessing->RenderSpecularMap(
                commandList,
                perceptualRoughness,
                m_RadianceTexture,
                radianceSubresources,
                m_SpecularTexture,
                0u,
                mip);
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
        if (!commandList ||
            !m_ProbeProcessing ||
            !m_LightProbe)
        {
            return false;
        }

        if (!m_BrdfReady)
        {
            m_ProbeProcessing->RenderEnvironmentBrdfTexture(
                commandList);
            m_BrdfReady = true;
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
        m_LightProbe->diffuseScale = scales.diffuse;
        m_LightProbe->specularScale = scales.specular;

        const ImageBasedLightingSource requestedSource =
            uint32_t(source) < uint32_t(ImageBasedLightingSource::Count)
                ? source
                : ImageBasedLightingSource::Kloppenheim03Day;
        const bool requestChanged =
            m_LastRequestedSource != requestedSource ||
            m_LastNeutralize != neutralize;
        const bool shouldAttemptLoad =
            requestChanged ||
            (!m_Uploaded && !m_LastLoadFailed);
        bool rebuilt = false;
        if (shouldAttemptLoad)
        {
            m_LastRequestedSource = requestedSource;
            m_LastNeutralize = neutralize;
            std::optional<ImportedRadiance> imported =
                LoadImportedRadiance(requestedSource, neutralize);
            if (!imported)
            {
                // A missing or invalid source must never reveal the retired
                // procedural or hemispherical ambient paths. Deactivate the
                // probe and background, then latch the failed request so the
                // render loop does not retry synchronous disk I/O each frame.
                m_Uploaded = false;
                m_LastSource = ImageBasedLightingSource::Count;
                m_LastSh = {};
                m_SourceAverageLuminance = 0.f;
                m_LastLoadFailed = true;
            }
            else
            {
                rebuilt = RebuildRadiance(commandList, *imported);
                if (rebuilt)
                {
                    m_Uploaded = true;
                    m_LastSource = requestedSource;
                    m_LastLoadFailed = false;
                }
                else
                {
                    m_Uploaded = false;
                    m_LastSource = ImageBasedLightingSource::Count;
                    m_LastSh = {};
                    m_SourceAverageLuminance = 0.f;
                    m_LastLoadFailed = true;
                }
            }
        }

        m_ActiveLightProbes.clear();
        if (m_Uploaded && m_LightProbe->IsActive())
            m_ActiveLightProbes.push_back(m_LightProbe);
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
