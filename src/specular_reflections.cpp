#include "specular_reflections.h"

#include <donut/core/log.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/render/MipMapGenPass.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "specular_reflections_cb.h"

static_assert(sizeof(SpecularReflectionConstants) % 16u == 0u,
    "Specular constants must preserve HLSL register alignment.");
static_assert(offsetof(SpecularReflectionConstants, renderWidth) ==
    sizeof(PlanarViewConstants),
    "Specular constants drifted after the view block.");

namespace uvsr
{
    namespace
    {
        constexpr uint32_t PipelineStepCount = 6u;

        bool HasFormatSupport(
            nvrhi::IDevice* device,
            nvrhi::Format format,
            bool requiresSampling,
            bool requiresUav)
        {
            if (!device)
                return false;
            nvrhi::FormatSupport required =
                nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::ShaderLoad;
            if (requiresSampling)
                required = required | nvrhi::FormatSupport::ShaderSample;
            if (requiresUav)
                required = required | nvrhi::FormatSupport::ShaderUavStore;
            return (device->queryFormatSupport(format) & required) == required;
        }

        uint32_t ResolveMipCount(uint32_t width, uint32_t height)
        {
            uint32_t largest = std::max(width, height);
            uint32_t mipCount = 1u;
            while (largest > 1u)
            {
                largest >>= 1u;
                ++mipCount;
            }
            return mipCount;
        }

        nvrhi::TextureHandle CreateTexture(
            nvrhi::IDevice* device,
            uint32_t width,
            uint32_t height,
            uint32_t mipLevels,
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc description;
            description.width = width;
            description.height = height;
            description.mipLevels = mipLevels;
            description.dimension = nvrhi::TextureDimension::Texture2D;
            description.format = format;
            description.isUAV = true;
            description.debugName = debugName;
            description.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            return device->createTexture(description);
        }

        nvrhi::BufferHandle CreateStructuredBuffer(
            nvrhi::IDevice* device,
            uint64_t elementCount,
            uint32_t elementSize,
            const char* debugName)
        {
            nvrhi::BufferDesc description;
            description.byteSize =
                std::max<uint64_t>(elementCount, 1u) * elementSize;
            description.structStride = elementSize;
            description.canHaveUAVs = true;
            description.initialState =
                nvrhi::ResourceStates::UnorderedAccess;
            description.keepInitialState = true;
            description.debugName = debugName;
            return device->createBuffer(description);
        }

        nvrhi::BufferHandle CreateRawBuffer(
            nvrhi::IDevice* device,
            uint64_t byteSize,
            bool indirectArguments,
            const char* debugName)
        {
            nvrhi::BufferDesc description;
            description.byteSize = std::max<uint64_t>(byteSize, 16u);
            description.canHaveUAVs = true;
            description.canHaveRawViews = true;
            description.isDrawIndirectArgs = indirectArguments;
            description.initialState =
                nvrhi::ResourceStates::UnorderedAccess;
            description.keepInitialState = true;
            description.debugName = debugName;
            return device->createBuffer(description);
        }

        bool SameBoundInputs(
            const SpecularReflectionInputs& left,
            const SpecularReflectionInputs& right)
        {
            return left.depth == right.depth &&
                left.diffuse == right.diffuse &&
                left.material == right.material &&
                left.normals == right.normals &&
                left.emissive == right.emissive &&
                left.materialAmbientOcclusion ==
                    right.materialAmbientOcclusion &&
                left.sceneColor == right.sceneColor &&
                left.specularEnvironment == right.specularEnvironment &&
                left.environmentBrdf == right.environmentBrdf &&
                left.skySpecularEstimate == right.skySpecularEstimate &&
                left.skyEstimateKind == right.skyEstimateKind &&
                left.worldTlas == right.worldTlas;
        }

        bool IsMatchingSingleSampleTexture(
            nvrhi::ITexture* texture,
            const nvrhi::TextureDesc& reference)
        {
            if (!texture)
                return false;
            const nvrhi::TextureDesc& description = texture->getDesc();
            return description.dimension ==
                    nvrhi::TextureDimension::Texture2D &&
                description.sampleCount == 1u &&
                description.width == reference.width &&
                description.height == reference.height &&
                description.isShaderResource;
        }

        bool IsSupportedSkyEstimateKind(uint32_t kind)
        {
            return kind == UVSR_SKY_ESTIMATE_NONE ||
                kind == UVSR_SKY_ESTIMATE_SCALAR_VISIBILITY ||
                kind == UVSR_SKY_ESTIMATE_RATIO_MODULATION ||
                kind == UVSR_SKY_ESTIMATE_FULL_STOCHASTIC_RADIANCE;
        }

        bool IsMatchingSkySpecularEstimate(
            nvrhi::ITexture* texture,
            uint32_t kind,
            const nvrhi::TextureDesc& reference)
        {
            if (kind == UVSR_SKY_ESTIMATE_NONE)
                return true;
            if (!texture || !IsMatchingSingleSampleTexture(
                    texture, reference))
            {
                return false;
            }
            const nvrhi::TextureDesc& description = texture->getDesc();
            if (description.depth != 1u || description.arraySize != 1u ||
                description.mipLevels != 1u)
            {
                return false;
            }
            return kind == UVSR_SKY_ESTIMATE_SCALAR_VISIBILITY
                ? description.format == nvrhi::Format::R8_UNORM
                : description.format == nvrhi::Format::RGBA16_FLOAT;
        }
    }

    bool SpecularReflectionPass::IsDeviceSupported(
        nvrhi::IDevice* device)
    {
        return device &&
            HasFormatSupport(
                device, nvrhi::Format::RGBA16_FLOAT, true, true) &&
            HasFormatSupport(
                device, nvrhi::Format::RGBA32_FLOAT, false, true) &&
            HasFormatSupport(
                device, nvrhi::Format::RGBA32_UINT, false, true) &&
            HasFormatSupport(
                device, nvrhi::Format::R16_FLOAT, false, true) &&
            HasFormatSupport(
                device, nvrhi::Format::R32_FLOAT, true, true);
    }

    SpecularReflectionPass::SpecularReflectionPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        bool deferPipelineCreation)
        : m_Device(device)
        , m_ShaderFactory(shaderFactory)
    {
        m_Supported = shaderFactory && IsDeviceSupported(device);
        m_RayQuerySupported = m_Supported &&
            device->queryFeatureSupport(
                nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayQuery);
        if (!m_Supported)
        {
            m_Readiness.status =
                SpecularReflectionStatus::UnsupportedDevice;
            log::warning(
                "Specular reflections require UAV-capable RGBA16F, "
                "RGBA32F, RGBA32_UINT, R16F, and R32F textures");
            return;
        }

        m_LinearClampSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

        nvrhi::BufferDesc constantBufferDescription;
        constantBufferDescription.byteSize =
            sizeof(SpecularReflectionConstants);
        constantBufferDescription.debugName =
            "SpecularReflectionConstants";
        constantBufferDescription.isConstantBuffer = true;
        constantBufferDescription.isVolatile = true;
        constantBufferDescription.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(
            constantBufferDescription);
        if (!m_LinearClampSampler || !m_ConstantBuffer)
        {
            m_Supported = false;
            m_Readiness.status =
                SpecularReflectionStatus::UnsupportedDevice;
            return;
        }

        if (!deferPipelineCreation)
        {
            while (!PreparePipelinesStep())
            {
            }
        }
    }

    SpecularReflectionPass::~SpecularReflectionPass() = default;

    bool SpecularReflectionPass::CreatePipelineStep(uint32_t step)
    {
        Pipeline* destination = nullptr;
        const char* shaderPath = nullptr;
        const char* entryPoint = nullptr;
        nvrhi::BindingLayoutDesc layout;
        layout.visibility = nvrhi::ShaderType::Compute;

        switch (step)
        {
        case 0u:
            destination = &m_PreparePipeline;
            shaderPath = "uvsr/specular_reflections_prepare_cs.hlsl";
            entryPoint = "Prepare";
            layout.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(1)
            };
            break;
        case 1u:
            destination = &m_TracePipeline;
            shaderPath = "uvsr/specular_reflections_trace_cs.hlsl";
            entryPoint = "Trace";
            layout.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_SRV(3),
                nvrhi::BindingLayoutItem::Texture_SRV(4),
                nvrhi::BindingLayoutItem::Texture_SRV(5),
                nvrhi::BindingLayoutItem::Texture_SRV(6),
                nvrhi::BindingLayoutItem::Texture_SRV(7),
                nvrhi::BindingLayoutItem::Texture_SRV(8),
                nvrhi::BindingLayoutItem::Sampler(0),
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(1),
                nvrhi::BindingLayoutItem::Texture_UAV(2),
                nvrhi::BindingLayoutItem::Texture_UAV(3),
                nvrhi::BindingLayoutItem::Texture_UAV(4),
                nvrhi::BindingLayoutItem::StructuredBuffer_UAV(5),
                nvrhi::BindingLayoutItem::RawBuffer_UAV(6)
            };
            break;
        case 2u:
            destination = &m_ArgumentPipeline;
            shaderPath = "uvsr/specular_reflections_arguments_cs.hlsl";
            entryPoint = "PrepareArguments";
            layout.bindings = {
                nvrhi::BindingLayoutItem::RawBuffer_UAV(0),
                nvrhi::BindingLayoutItem::RawBuffer_UAV(1)
            };
            break;
        case 3u:
        case 4u:
            if (!m_RayQuerySupported)
                return true;
            destination = step == 3u
                ? &m_RayQueryCompactPipeline
                : &m_RayQueryDensePipeline;
            shaderPath = "uvsr/specular_reflections_ray_query_cs.hlsl";
            entryPoint = step == 3u ? "TraceCompact" : "TraceDense";
            layout.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_SRV(3),
                nvrhi::BindingLayoutItem::Texture_SRV(4),
                nvrhi::BindingLayoutItem::Texture_SRV(5),
                nvrhi::BindingLayoutItem::Texture_SRV(6),
                nvrhi::BindingLayoutItem::RawBuffer_SRV(7),
                nvrhi::BindingLayoutItem::Sampler(0),
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(1),
                nvrhi::BindingLayoutItem::Texture_UAV(2),
                nvrhi::BindingLayoutItem::Texture_UAV(3),
                nvrhi::BindingLayoutItem::Texture_UAV(4)
            };
            break;
        case 5u:
            destination = &m_ResolvePipeline;
            shaderPath = "uvsr/specular_reflections_resolve_cs.hlsl";
            entryPoint = "Resolve";
            layout.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_SRV(3),
                nvrhi::BindingLayoutItem::Texture_SRV(4),
                nvrhi::BindingLayoutItem::Texture_SRV(5),
                nvrhi::BindingLayoutItem::Texture_SRV(6),
                nvrhi::BindingLayoutItem::Texture_SRV(7),
                nvrhi::BindingLayoutItem::Texture_SRV(8),
                nvrhi::BindingLayoutItem::Texture_SRV(9),
                nvrhi::BindingLayoutItem::Texture_SRV(10),
                nvrhi::BindingLayoutItem::Texture_SRV(11),
                nvrhi::BindingLayoutItem::Texture_SRV(12),
                nvrhi::BindingLayoutItem::Texture_SRV(13),
                nvrhi::BindingLayoutItem::Texture_SRV(14),
                nvrhi::BindingLayoutItem::Sampler(0),
                nvrhi::BindingLayoutItem::Texture_UAV(0)
            };
            break;
        default:
            return false;
        }

        if (step == 4u)
        {
            // Both entry points share one binding set, so they must also share
            // the exact same layout handle on backends that key compatibility
            // by object identity rather than descriptor equivalence.
            destination->bindingLayout =
                m_RayQueryCompactPipeline.bindingLayout;
        }
        else
        {
            destination->bindingLayout =
                m_Device->createBindingLayout(layout);
        }
        destination->shader = m_ShaderFactory->CreateShader(
            shaderPath,
            entryPoint,
            nullptr,
            nvrhi::ShaderType::Compute);
        if (!destination->bindingLayout || !destination->shader)
            return false;
        nvrhi::ComputePipelineDesc pipelineDescription;
        pipelineDescription.CS = destination->shader;
        pipelineDescription.bindingLayouts = {
            destination->bindingLayout
        };
        destination->pipeline = m_Device->createComputePipeline(
            pipelineDescription);
        return destination->pipeline != nullptr;
    }

    bool SpecularReflectionPass::PreparePipelinesStep()
    {
        if (!m_Supported)
            return true;
        if (m_PipelinePreparationStep < PipelineStepCount)
        {
            if (!CreatePipelineStep(m_PipelinePreparationStep))
            {
                log::error(
                    "Specular reflection pipeline step %u failed",
                    m_PipelinePreparationStep);
            }
            ++m_PipelinePreparationStep;
        }
        if (m_PipelinePreparationStep < PipelineStepCount)
            return false;

        m_PipelinesReady = m_PreparePipeline.pipeline &&
            m_TracePipeline.pipeline && m_ResolvePipeline.pipeline;
        m_RayQueryPipelinesReady = m_RayQuerySupported &&
            m_ArgumentPipeline.pipeline &&
            m_RayQueryCompactPipeline.pipeline &&
            m_RayQueryDensePipeline.pipeline;
        return true;
    }

    bool SpecularReflectionPass::ValidateInputs(
        const SpecularReflectionInputs& inputs,
        SpecularReflectionStatus& failure) const
    {
        const nvrhi::ITexture* resolvedInputs[] = {
            inputs.depth,
            inputs.diffuse,
            inputs.material,
            inputs.normals,
            inputs.emissive,
            inputs.materialAmbientOcclusion,
            inputs.sceneColor
        };
        for (const nvrhi::ITexture* texture : resolvedInputs)
        {
            if (texture && texture->getDesc().sampleCount != 1u)
            {
                failure = SpecularReflectionStatus::MsaaUnavailable;
                return false;
            }
        }
        if (!inputs.view || !inputs.depth || !inputs.diffuse ||
            !inputs.material || !inputs.normals || !inputs.emissive ||
            !inputs.materialAmbientOcclusion || !inputs.sceneColor)
        {
            failure = SpecularReflectionStatus::MissingResolvedGBuffer;
            return false;
        }

        const nvrhi::TextureDesc& sceneDescription =
            inputs.sceneColor->getDesc();
        if (sceneDescription.dimension !=
                nvrhi::TextureDimension::Texture2D ||
            sceneDescription.sampleCount != 1u ||
            sceneDescription.format != nvrhi::Format::RGBA16_FLOAT ||
            !sceneDescription.isShaderResource ||
            sceneDescription.width == 0u || sceneDescription.height == 0u)
        {
            failure = SpecularReflectionStatus::MissingResolvedGBuffer;
            return false;
        }
        for (nvrhi::ITexture* texture : {
                inputs.depth,
                inputs.diffuse,
                inputs.material,
                inputs.normals,
                inputs.emissive,
                inputs.materialAmbientOcclusion })
        {
            if (!IsMatchingSingleSampleTexture(texture, sceneDescription))
            {
                failure =
                    SpecularReflectionStatus::MissingResolvedGBuffer;
                return false;
            }
        }

        const nvrhi::Rect viewExtent = inputs.view->GetViewExtent();
        if (viewExtent.minX != 0 || viewExtent.minY != 0 ||
            viewExtent.width() != int(sceneDescription.width) ||
            viewExtent.height() != int(sceneDescription.height))
        {
            // Mip reduction covers the whole allocation. Reject sub-views so
            // stale pixels outside the view cannot enter a coarse footprint.
            failure = SpecularReflectionStatus::MissingResolvedGBuffer;
            return false;
        }
        const uint64_t pixelCount = uint64_t(sceneDescription.width) *
            uint64_t(sceneDescription.height);
        if (pixelCount > std::numeric_limits<uint32_t>::max())
        {
            failure = SpecularReflectionStatus::MissingResolvedGBuffer;
            return false;
        }

        if (!inputs.specularEnvironment || !inputs.environmentBrdf)
        {
            failure = SpecularReflectionStatus::MissingEnvironment;
            return false;
        }
        const nvrhi::TextureDesc& specularEnvironmentDescription =
            inputs.specularEnvironment->getDesc();
        const nvrhi::TextureDesc& environmentBrdfDescription =
            inputs.environmentBrdf->getDesc();
        const uint32_t environmentCubeCount =
            specularEnvironmentDescription.arraySize / 6u;
        if (!specularEnvironmentDescription.isShaderResource ||
            specularEnvironmentDescription.dimension !=
                nvrhi::TextureDimension::TextureCubeArray ||
            specularEnvironmentDescription.mipLevels == 0u ||
            environmentCubeCount == 0u ||
            inputs.specularEnvironmentArrayIndex >= environmentCubeCount ||
            !std::isfinite(inputs.specularEnvironmentScale) ||
            !std::isfinite(inputs.specularEnvironmentMipLevels) ||
            inputs.specularEnvironmentMipLevels < 1.f ||
            !environmentBrdfDescription.isShaderResource ||
            environmentBrdfDescription.dimension !=
                nvrhi::TextureDimension::Texture2D ||
            environmentBrdfDescription.sampleCount != 1u ||
            environmentBrdfDescription.width == 0u ||
            environmentBrdfDescription.height == 0u ||
            !IsSupportedSkyEstimateKind(inputs.skyEstimateKind) ||
            !IsMatchingSkySpecularEstimate(
                inputs.skySpecularEstimate,
                inputs.skyEstimateKind,
                sceneDescription))
        {
            failure = SpecularReflectionStatus::MissingEnvironment;
            return false;
        }
        return true;
    }

    bool SpecularReflectionPass::EnsureResources(
        uint2 size,
        bool reverseDepth,
        bool sourceRadianceEnabled)
    {
        const uint32_t mipLevels = ResolveMipCount(size.x, size.y);
        if (m_Size.x == size.x && m_Size.y == size.y &&
            m_MipLevels == mipLevels &&
            m_ReverseDepth == reverseDepth &&
            m_SourceRadianceEnabled == sourceRadianceEnabled &&
            m_SceneColorPyramid && m_DepthPyramid &&
            m_ResolvedSpecular)
        {
            return true;
        }

        ReleaseResources();
        const uint64_t pixelCount = uint64_t(size.x) * uint64_t(size.y);
        m_SceneColorPyramid = CreateTexture(
            m_Device,
            size.x,
            size.y,
            mipLevels,
            nvrhi::Format::RGBA16_FLOAT,
            "Specular/Scene Color Pyramid");
        m_DepthPyramid = CreateTexture(
            m_Device,
            size.x,
            size.y,
            mipLevels,
            nvrhi::Format::R32_FLOAT,
            "Specular/Depth Pyramid");
        m_HitUvPdfDistance = CreateTexture(
            m_Device,
            size.x,
            size.y,
            1u,
            nvrhi::Format::RGBA32_FLOAT,
            "Specular/Hit UV PDF Distance");
        m_HitDirectionFootprint = CreateTexture(
            m_Device,
            size.x,
            size.y,
            1u,
            nvrhi::Format::RGBA16_FLOAT,
            "Specular/Hit Direction Footprint");
        m_HitMetadata = CreateTexture(
            m_Device,
            size.x,
            size.y,
            1u,
            nvrhi::Format::RGBA32_UINT,
            "Specular/Hit Metadata");
        m_HitConfidence = CreateTexture(
            m_Device,
            size.x,
            size.y,
            1u,
            nvrhi::Format::R16_FLOAT,
            "Specular/Hit Confidence");
        if (sourceRadianceEnabled)
        {
            m_SourceRadiance = CreateTexture(
                m_Device,
                size.x,
                size.y,
                1u,
                nvrhi::Format::RGBA16_FLOAT,
                "Specular/Stored Source Radiance");
        }
        m_DummyRadiance = CreateTexture(
            m_Device,
            1u,
            1u,
            1u,
            nvrhi::Format::RGBA16_FLOAT,
            "Specular/Dummy Radiance");
        m_ResolvedSpecular = CreateTexture(
            m_Device,
            size.x,
            size.y,
            1u,
            nvrhi::Format::RGBA16_FLOAT,
            "Specular/Resolved Indirect Specular");
        m_LowConfidenceRays = CreateStructuredBuffer(
            m_Device,
            pixelCount,
            sizeof(uint32_t),
            "Specular/Low Confidence Rays");
        m_RayCounter = CreateRawBuffer(
            m_Device,
            16u,
            false,
            "Specular/Ray Counter");
        m_IndirectArguments = CreateRawBuffer(
            m_Device,
            sizeof(nvrhi::DispatchIndirectArguments),
            true,
            "Specular/Ray Dispatch Arguments");

        if (!m_SceneColorPyramid || !m_DepthPyramid ||
            !m_HitUvPdfDistance || !m_HitDirectionFootprint ||
            !m_HitMetadata || !m_HitConfidence ||
            (sourceRadianceEnabled && !m_SourceRadiance) ||
            !m_DummyRadiance ||
            !m_ResolvedSpecular || !m_LowConfidenceRays ||
            !m_RayCounter || !m_IndirectArguments)
        {
            ReleaseResources();
            return false;
        }

        m_SceneColorMipGenerator =
            std::make_unique<donut::render::MipMapGenPass>(
                m_Device,
                m_ShaderFactory,
                m_SceneColorPyramid,
                donut::render::MipMapGenPass::MODE_COLOR);
        m_DepthMipGenerator =
            std::make_unique<donut::render::MipMapGenPass>(
                m_Device,
                m_ShaderFactory,
                m_DepthPyramid,
                reverseDepth
                    ? donut::render::MipMapGenPass::MODE_MAX
                    : donut::render::MipMapGenPass::MODE_MIN);
        m_Size = size;
        m_MipLevels = mipLevels;
        m_ReverseDepth = reverseDepth;
        m_SourceRadianceEnabled = sourceRadianceEnabled;
        return true;
    }

    bool SpecularReflectionPass::EnsureBindingSets(
        const SpecularReflectionInputs& inputs,
        bool rayTracingAvailable)
    {
        if (!SameBoundInputs(m_BoundInputs, inputs))
        {
            ResetBindingCache();
            m_BoundInputs = inputs;
        }
        nvrhi::ITexture* sourceRadiance = m_SourceRadianceEnabled
            ? m_SourceRadiance.Get()
            : m_DummyRadiance.Get();
        nvrhi::ITexture* skySpecularEstimate =
            inputs.skyEstimateKind != UVSR_SKY_ESTIMATE_NONE
            ? inputs.skySpecularEstimate
            : m_DummyRadiance.Get();

        if (!m_PrepareBindingSet)
        {
            nvrhi::BindingSetDesc description;
            description.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
                nvrhi::BindingSetItem::Texture_SRV(1, inputs.sceneColor),
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_DepthPyramid).setSubresources(
                        nvrhi::TextureSubresourceSet(0u, 1u, 0u, 1u)),
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_SceneColorPyramid).setSubresources(
                        nvrhi::TextureSubresourceSet(0u, 1u, 0u, 1u))
            };
            m_PrepareBindingSet = m_Device->createBindingSet(
                description, m_PreparePipeline.bindingLayout);
        }

        if (!m_TraceBindingSet)
        {
            nvrhi::BindingSetDesc description;
            description.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, m_DepthPyramid),
                nvrhi::BindingSetItem::Texture_SRV(1, inputs.depth),
                nvrhi::BindingSetItem::Texture_SRV(2, inputs.diffuse),
                nvrhi::BindingSetItem::Texture_SRV(3, inputs.material),
                nvrhi::BindingSetItem::Texture_SRV(4, inputs.normals),
                nvrhi::BindingSetItem::Texture_SRV(5, inputs.emissive),
                nvrhi::BindingSetItem::Texture_SRV(
                    6, inputs.materialAmbientOcclusion),
                nvrhi::BindingSetItem::Texture_SRV(
                    7, m_SceneColorPyramid),
                nvrhi::BindingSetItem::Texture_SRV(
                    8, inputs.specularEnvironment),
                nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_HitUvPdfDistance),
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_HitDirectionFootprint),
                nvrhi::BindingSetItem::Texture_UAV(2, m_HitMetadata),
                nvrhi::BindingSetItem::Texture_UAV(3, m_HitConfidence),
                nvrhi::BindingSetItem::Texture_UAV(4, sourceRadiance),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(
                    5, m_LowConfidenceRays),
                nvrhi::BindingSetItem::RawBuffer_UAV(6, m_RayCounter)
            };
            m_TraceBindingSet = m_Device->createBindingSet(
                description, m_TracePipeline.bindingLayout);
        }

        if (!m_ArgumentBindingSet)
        {
            nvrhi::BindingSetDesc description;
            description.bindings = {
                nvrhi::BindingSetItem::RawBuffer_UAV(0, m_RayCounter),
                nvrhi::BindingSetItem::RawBuffer_UAV(
                    1, m_IndirectArguments)
            };
            m_ArgumentBindingSet = m_Device->createBindingSet(
                description, m_ArgumentPipeline.bindingLayout);
        }

        if (rayTracingAvailable && !m_RayQueryBindingSet)
        {
            nvrhi::BindingSetDesc description;
            description.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::RayTracingAccelStruct(
                    0, inputs.worldTlas),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    1, m_LowConfidenceRays),
                nvrhi::BindingSetItem::Texture_SRV(2, inputs.depth),
                nvrhi::BindingSetItem::Texture_SRV(3, inputs.material),
                nvrhi::BindingSetItem::Texture_SRV(4, inputs.normals),
                nvrhi::BindingSetItem::Texture_SRV(
                    5, m_SceneColorPyramid),
                nvrhi::BindingSetItem::Texture_SRV(
                    6, inputs.specularEnvironment),
                nvrhi::BindingSetItem::RawBuffer_SRV(7, m_RayCounter),
                nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_HitUvPdfDistance),
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_HitDirectionFootprint),
                nvrhi::BindingSetItem::Texture_UAV(2, m_HitMetadata),
                nvrhi::BindingSetItem::Texture_UAV(3, m_HitConfidence),
                nvrhi::BindingSetItem::Texture_UAV(4, sourceRadiance)
            };
            const nvrhi::BindingLayoutHandle& layout =
                m_RayQueryCompactPipeline.bindingLayout;
            m_RayQueryBindingSet = m_Device->createBindingSet(
                description, layout);
        }

        if (!m_ResolveBindingSet)
        {
            nvrhi::BindingSetDesc description;
            description.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, inputs.diffuse),
                nvrhi::BindingSetItem::Texture_SRV(1, inputs.material),
                nvrhi::BindingSetItem::Texture_SRV(2, inputs.normals),
                nvrhi::BindingSetItem::Texture_SRV(3, inputs.emissive),
                nvrhi::BindingSetItem::Texture_SRV(
                    4, inputs.materialAmbientOcclusion),
                nvrhi::BindingSetItem::Texture_SRV(
                    5, m_HitUvPdfDistance),
                nvrhi::BindingSetItem::Texture_SRV(
                    6, m_HitDirectionFootprint),
                nvrhi::BindingSetItem::Texture_SRV(7, m_HitMetadata),
                nvrhi::BindingSetItem::Texture_SRV(8, m_HitConfidence),
                nvrhi::BindingSetItem::Texture_SRV(9, sourceRadiance),
                nvrhi::BindingSetItem::Texture_SRV(
                    10, m_SceneColorPyramid),
                nvrhi::BindingSetItem::Texture_SRV(
                    11, inputs.specularEnvironment),
                nvrhi::BindingSetItem::Texture_SRV(
                    12, inputs.environmentBrdf),
                nvrhi::BindingSetItem::Texture_SRV(
                    13, skySpecularEstimate),
                nvrhi::BindingSetItem::Texture_SRV(14, inputs.depth),
                nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_ResolvedSpecular)
            };
            m_ResolveBindingSet = m_Device->createBindingSet(
                description, m_ResolvePipeline.bindingLayout);
        }

        return m_PrepareBindingSet && m_TraceBindingSet &&
            m_ArgumentBindingSet && m_ResolveBindingSet &&
            (!rayTracingAvailable || m_RayQueryBindingSet);
    }

    SpecularReflectionReadiness SpecularReflectionPass::PrepareForRender(
        const SpecularSettings& requestedSettings,
        const SpecularReflectionInputs& inputs)
    {
        const SpecularSettings settings =
            SanitizeSpecularSettings(requestedSettings);
        const SpecularTechnique technique =
            ResolveSpecularTechnique(settings);
        m_Readiness = {};
        m_Readiness.technique = technique;
        if (technique == SpecularTechnique::None)
        {
            Deactivate();
            return m_Readiness;
        }
        if (!m_Supported)
        {
            m_Readiness.status =
                SpecularReflectionStatus::UnsupportedDevice;
            return m_Readiness;
        }
        if (!m_PipelinesReady)
        {
            m_Readiness.status = SpecularReflectionStatus::Preparing;
            return m_Readiness;
        }

        SpecularReflectionStatus failure =
            SpecularReflectionStatus::MissingResolvedGBuffer;
        if (!ValidateInputs(inputs, failure))
        {
            m_Readiness.status = failure;
            return m_Readiness;
        }

        const nvrhi::TextureDesc& sceneDescription =
            inputs.sceneColor->getDesc();
        const bool reverseDepth = inputs.view->IsReverseDepth();
        if (!EnsureResources(
                uint2(sceneDescription.width, sceneDescription.height),
                reverseDepth,
                !settings.sssr.deferredColorLookup))
        {
            m_Readiness.status = SpecularReflectionStatus::Preparing;
            return m_Readiness;
        }

        m_Readiness.rayTracingRequested =
            technique == SpecularTechnique::Hybrid &&
            settings.hybrid.rayTracing;
        m_Readiness.rayTracingAvailable =
            m_Readiness.rayTracingRequested &&
            m_RayQueryPipelinesReady &&
            inputs.globalRayTraversalAllowed &&
            inputs.coherentTlasReady && inputs.worldTlas;
        if (!EnsureBindingSets(inputs, m_Readiness.rayTracingAvailable))
        {
            m_Readiness.status = SpecularReflectionStatus::Preparing;
            return m_Readiness;
        }

        m_Readiness.operational = true;
        m_Readiness.status = m_Readiness.rayTracingAvailable
            ? SpecularReflectionStatus::ReadyHybrid
            : SpecularReflectionStatus::ReadyScreenSpace;
        return m_Readiness;
    }

    void SpecularReflectionPass::PublishRayRecordWrites(
        nvrhi::ICommandList* commandList) const
    {
        nvrhi::utils::TextureUavBarrier(
            commandList, m_HitUvPdfDistance);
        nvrhi::utils::TextureUavBarrier(
            commandList, m_HitDirectionFootprint);
        nvrhi::utils::TextureUavBarrier(
            commandList, m_HitMetadata);
        nvrhi::utils::TextureUavBarrier(
            commandList, m_HitConfidence);
        if (m_SourceRadianceEnabled)
        {
            nvrhi::utils::TextureUavBarrier(
                commandList, m_SourceRadiance);
        }
        nvrhi::utils::BufferUavBarrier(
            commandList, m_LowConfidenceRays);
        nvrhi::utils::BufferUavBarrier(commandList, m_RayCounter);
        commandList->commitBarriers();
    }

    SpecularReflectionResult SpecularReflectionPass::Render(
        nvrhi::ICommandList* commandList,
        const SpecularSettings& requestedSettings,
        const SpecularReflectionInputs& inputs,
        uint32_t frameIndex)
    {
        const SpecularSettings settings =
            SanitizeSpecularSettings(requestedSettings);
        const SpecularReflectionReadiness readiness =
            PrepareForRender(settings, inputs);
        SpecularReflectionResult result;
        result.status = readiness.status;
        result.technique = readiness.technique;
        result.ratioEstimator = settings.sssr.ratioEstimator;
        result.deferredColorLookup =
            settings.sssr.deferredColorLookup;
        result.skyEstimateKind = inputs.skyEstimateKind;
        if (!commandList || !readiness.operational)
            return result;

        SpecularReflectionConstants constants = {};
        inputs.view->FillPlanarViewConstants(constants.view);
        constants.renderWidth = float(m_Size.x);
        constants.renderHeight = float(m_Size.y);
        constants.inverseRenderWidth = 1.f / float(m_Size.x);
        constants.inverseRenderHeight = 1.f / float(m_Size.y);
        constants.specularEnvironmentScale = std::max(
            inputs.specularEnvironmentScale, 0.f);
        constants.specularEnvironmentMipLevels = std::clamp(
            inputs.specularEnvironmentMipLevels,
            1.f,
            float(inputs.specularEnvironment->getDesc().mipLevels));
        constants.specularEnvironmentArrayIndex = float(
            inputs.specularEnvironmentArrayIndex);
        constants.skyEstimateKind = inputs.skyEstimateKind;
        constants.maxRoughness = ResolveSpecularMaxRoughness(settings);
        constants.depthThickness = settings.sssr.depthThickness;
        constants.traceAndFilterBias =
            settings.sssr.traceAndFilterBias;
        constants.screenSpaceConfidence =
            settings.hybrid.screenSpaceConfidence;
        constants.rayMaxDistance = readiness.technique ==
                SpecularTechnique::Hybrid
            ? settings.hybrid.rayMaxDistance
            : std::max(
                std::isfinite(inputs.sceneDiagonal)
                    ? inputs.sceneDiagonal
                    : SpecularDefaultRayMaxDistance,
                0.001f);
        constants.rayBias = settings.hybrid.rayBias;
        constants.backfaceRejection = readiness.technique ==
                SpecularTechnique::Hybrid
            ? settings.hybrid.backfaceRejection
            : SpecularDefaultBackfaceRejection;
        constants.depthRejection = readiness.technique ==
                SpecularTechnique::Hybrid
            ? settings.hybrid.depthRejection
            : SpecularDefaultDepthRejection;
        constants.frameIndex = frameIndex;
        constants.reflectionTechnique =
            static_cast<uint32_t>(readiness.technique);
        constants.samplesPerQuad = ResolveSpecularSamplesPerQuad(
            settings.sssr.samplesPerQuad);
        constants.ratioEstimator = settings.sssr.ratioEstimator ? 1u : 0u;
        constants.deferredColorLookup =
            settings.sssr.deferredColorLookup ? 1u : 0u;
        constants.reverseDepth = m_ReverseDepth ? 1u : 0u;
        constants.maxTraversalIterations =
            settings.sssr.maxTraversalIterations;
        constants.minTraversalOccupancy =
            settings.sssr.minTraversalOccupancy;
        constants.mostDetailedMip = std::min(
            settings.sssr.mostDetailedMip,
            m_MipLevels - 1u);
        constants.diagnosticView = static_cast<uint32_t>(
            ResolveSpecularDiagnosticView(settings));
        constants.sceneColorMipLevels = m_MipLevels;
        constants.depthMipLevels = m_MipLevels;
        constants.useRayTracing =
            readiness.rayTracingAvailable ? 1u : 0u;
        constants.compactRayTracing =
            settings.hybrid.compactDispatch ? 1u : 0u;
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

        commandList->beginMarker("Specular Reflections");
        nvrhi::ComputeState state;
        state.pipeline = m_PreparePipeline.pipeline;
        state.bindings = { m_PrepareBindingSet };
        commandList->setComputeState(state);
        commandList->dispatch(
            div_ceil(m_Size.x, 8u),
            div_ceil(m_Size.y, 8u),
            1u);
        nvrhi::utils::TextureUavBarrier(
            commandList, m_DepthPyramid);
        nvrhi::utils::TextureUavBarrier(
            commandList, m_SceneColorPyramid);
        commandList->commitBarriers();

        m_DepthMipGenerator->Dispatch(commandList);
        m_SceneColorMipGenerator->Dispatch(commandList);
        nvrhi::utils::TextureUavBarrier(
            commandList, m_DepthPyramid);
        nvrhi::utils::TextureUavBarrier(
            commandList, m_SceneColorPyramid);
        commandList->commitBarriers();

        commandList->clearBufferUInt(m_RayCounter, 0u);
        nvrhi::utils::BufferUavBarrier(commandList, m_RayCounter);
        commandList->commitBarriers();

        state = {};
        state.pipeline = m_TracePipeline.pipeline;
        state.bindings = { m_TraceBindingSet };
        commandList->setComputeState(state);
        commandList->dispatch(
            div_ceil(m_Size.x, 8u),
            div_ceil(m_Size.y, 8u),
            1u);
        PublishRayRecordWrites(commandList);
        result.dispatchedScreenSpace = true;

        if (readiness.rayTracingAvailable)
        {
            if (settings.hybrid.compactDispatch)
            {
                commandList->setBufferState(
                    m_IndirectArguments,
                    nvrhi::ResourceStates::UnorderedAccess);
                commandList->commitBarriers();
                state = {};
                state.pipeline = m_ArgumentPipeline.pipeline;
                state.bindings = { m_ArgumentBindingSet };
                commandList->setComputeState(state);
                commandList->dispatch(1u, 1u, 1u);
                nvrhi::utils::BufferUavBarrier(
                    commandList, m_IndirectArguments);
                commandList->commitBarriers();
                commandList->setBufferState(
                    m_IndirectArguments,
                    nvrhi::ResourceStates::IndirectArgument);
                commandList->commitBarriers();

                state = {};
                state.pipeline = m_RayQueryCompactPipeline.pipeline;
                state.bindings = { m_RayQueryBindingSet };
                state.indirectParams = m_IndirectArguments;
                commandList->setComputeState(state);
                commandList->dispatchIndirect(0u);
                commandList->setBufferState(
                    m_IndirectArguments,
                    nvrhi::ResourceStates::UnorderedAccess);
                commandList->commitBarriers();
            }
            else
            {
                state = {};
                state.pipeline = m_RayQueryDensePipeline.pipeline;
                state.bindings = { m_RayQueryBindingSet };
                commandList->setComputeState(state);
                commandList->dispatch(
                    div_ceil(m_Size.x, 8u),
                    div_ceil(m_Size.y, 8u),
                    1u);
            }
            PublishRayRecordWrites(commandList);
            result.dispatchedRayTracing = true;
        }

        state = {};
        state.pipeline = m_ResolvePipeline.pipeline;
        state.bindings = { m_ResolveBindingSet };
        commandList->setComputeState(state);
        commandList->dispatch(
            div_ceil(m_Size.x, 8u),
            div_ceil(m_Size.y, 8u),
            1u);
        nvrhi::utils::TextureUavBarrier(
            commandList, m_ResolvedSpecular);
        commandList->setTextureState(
            m_ResolvedSpecular,
            nvrhi::AllSubresources,
            nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();
        commandList->endMarker();

        result.indirectSpecular = m_ResolvedSpecular;
        result.environmentFallback = true;
        return result;
    }

    void SpecularReflectionPass::ResetBindingCache()
    {
        m_PrepareBindingSet = nullptr;
        m_TraceBindingSet = nullptr;
        m_ArgumentBindingSet = nullptr;
        m_RayQueryBindingSet = nullptr;
        m_ResolveBindingSet = nullptr;
        m_BoundInputs = {};
    }

    void SpecularReflectionPass::ReleaseResources()
    {
        ResetBindingCache();
        m_SceneColorMipGenerator.reset();
        m_DepthMipGenerator.reset();
        m_SceneColorPyramid = nullptr;
        m_DepthPyramid = nullptr;
        m_HitUvPdfDistance = nullptr;
        m_HitDirectionFootprint = nullptr;
        m_HitMetadata = nullptr;
        m_HitConfidence = nullptr;
        m_SourceRadiance = nullptr;
        m_DummyRadiance = nullptr;
        m_ResolvedSpecular = nullptr;
        m_LowConfidenceRays = nullptr;
        m_RayCounter = nullptr;
        m_IndirectArguments = nullptr;
        m_Size = uint2::zero();
        m_MipLevels = 0u;
        m_SourceRadianceEnabled = false;
    }

    void SpecularReflectionPass::Deactivate()
    {
        ReleaseResources();
        m_Readiness = {};
    }

    void SpecularReflectionPass::DeactivateForExternalDebugView(
        const SpecularSettings& requestedSettings)
    {
        const SpecularSettings settings =
            SanitizeSpecularSettings(requestedSettings);
        ReleaseResources();
        m_Readiness = {};
        m_Readiness.status =
            SpecularReflectionStatus::ExternalDebugViewActive;
        m_Readiness.technique = ResolveSpecularTechnique(settings);
    }
}
