#include "path_tracing_pass.h"
#include "renderer_common_passes.h"
#include "renderer_log.h"
#include "renderer_shader_factory.h"

#include <donut/core/math/math.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "renderer_gpu_contract.h"
#include "path_tracing_cb.h"
#include "path_tracing_bindings.h"

static_assert(sizeof(PathTracingConstants) % 16u == 0u);
static_assert(offsetof(PathTracingConstants, previousView) ==
    sizeof(PlanarViewConstants));
static_assert(sizeof(GeometryData) == 64u);
static_assert(sizeof(InstanceData) == 112u);
static_assert(sizeof(MaterialConstants) == 208u);
static_assert(std::is_trivially_copyable_v<LightConstants>);

namespace uvsr
{
    namespace
    {
        bool HasFormatSupport(
            nvrhi::IDevice* device,
            nvrhi::Format format,
            nvrhi::FormatSupport required)
        {
            return device &&
                (device->queryFormatSupport(format) & required) == required;
        }

        nvrhi::TextureHandle CreateTexture(
            nvrhi::IDevice* device,
            uint32_t width,
            uint32_t height,
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc desc;
            desc.width = width;
            desc.height = height;
            desc.dimension = nvrhi::TextureDimension::Texture2D;
            desc.format = format;
            desc.isUAV = true;
            desc.debugName = debugName;
            desc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            return device->createTexture(desc);
        }

        bool TryGetStructuredBufferCount(
            nvrhi::IBuffer* buffer,
            uint32_t expectedStride,
            uint32_t& count)
        {
            count = 0u;
            if (!buffer || expectedStride == 0u)
                return false;
            const nvrhi::BufferDesc& desc = buffer->getDesc();
            if (desc.byteSize % expectedStride != 0u ||
                desc.byteSize / expectedStride >
                    std::numeric_limits<uint32_t>::max())
            {
                return false;
            }
            count = uint32_t(desc.byteSize / expectedStride);
            return count != 0u;
        }

        uint64_t HashBytes(
            uint64_t hash,
            const void* data,
            size_t size)
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (size_t index = 0u; index < size; ++index)
            {
                hash ^= bytes[index];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        template<typename T>
        uint64_t HashValue(uint64_t hash, const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            return HashBytes(hash, &value, sizeof(value));
        }

        uint64_t BuildInputSignature(
            const PathTracingInputs& inputs,
            const std::vector<LightConstants>& lights)
        {
            uint64_t hash = 1469598103934665603ull;
            hash = HashValue(hash, inputs.width);
            hash = HashValue(hash, inputs.height);
            hash = HashValue(hash, inputs.environmentScale);
            hash = HashValue(hash, inputs.showEnvironmentBackground);
            hash = HashValue(hash, inputs.rayBias);
            hash = HashValue(hash, inputs.maximumRayDistance);
            hash = HashValue(hash, inputs.noiseSettings.pattern);
            hash = HashValue(hash, inputs.flashlightProfile);
            const uintptr_t pointers[] = {
                reinterpret_cast<uintptr_t>(inputs.worldTlas),
                reinterpret_cast<uintptr_t>(inputs.environment),
                reinterpret_cast<uintptr_t>(inputs.noiseTexture),
                reinterpret_cast<uintptr_t>(
                    inputs.materialVisibility.geometryBuffer),
                reinterpret_cast<uintptr_t>(
                    inputs.materialVisibility.materialBuffer),
                reinterpret_cast<uintptr_t>(
                    inputs.materialVisibility.geometryIndexMap),
                reinterpret_cast<uintptr_t>(
                    inputs.materialVisibility.instanceBuffer),
                reinterpret_cast<uintptr_t>(inputs.flashlight)
            };
            hash = HashBytes(hash, pointers, sizeof(pointers));
            if (!lights.empty())
            {
                hash = HashBytes(
                    hash,
                    lights.data(),
                    lights.size() * sizeof(LightConstants));
            }
            return hash;
        }
    }

    PathTracingCapabilities PathTracingPass::QueryCapabilities(
        nvrhi::IDevice* device)
    {
        const nvrhi::FormatSupport historySupport =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavLoad |
            nvrhi::FormatSupport::ShaderUavStore;
        const nvrhi::FormatSupport outputSupport =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavStore;
        PathTracingCapabilities result;
        result.rayQuerySupported = device &&
            device->queryFeatureSupport(
                nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayQuery) &&
            HasFormatSupport(
                device,
                nvrhi::Format::RGBA32_FLOAT,
                historySupport) &&
            HasFormatSupport(
                device,
                nvrhi::Format::R32_UINT,
                historySupport) &&
            HasFormatSupport(
                device,
                nvrhi::Format::RGBA16_FLOAT,
                outputSupport) &&
            HasFormatSupport(
                device,
                nvrhi::Format::R32_FLOAT,
                outputSupport);
        return result;
    }

    PathTracingPass::PathTracingPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
        nvrhi::IBindingLayout* bindlessLayout)
        : m_Device(device)
        , m_BindlessLayout(bindlessLayout)
        , m_Capabilities(QueryCapabilities(device))
    {
        if (!m_Capabilities.rayQuerySupported)
        {
            log::warning(
                "Path tracing requires DXR 1.1 ray queries and typed transport UAV support");
            return;
        }
        if (!shaderFactory || !m_BindlessLayout)
        {
            log::error(
                "The standard path-tracing pipeline dependencies are unavailable");
            return;
        }

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(PathTracingConstants);
        constantBufferDesc.debugName = "PathTracingConstants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            RendererMaxConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);
        m_Sampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                PathTracingConstantBufferSlot),
            nvrhi::BindingLayoutItem::RayTracingAccelStruct(
                PathTracingWorldTlasSlot),
            nvrhi::BindingLayoutItem::Texture_SRV(
                PathTracingEnvironmentSlot),
            nvrhi::BindingLayoutItem::Texture_SRV(PathTracingNoiseSlot),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                PathTracingLightsSlot),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                PathTracingInstancesSlot),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_UAV(
                PathTracingRawMeanUavSlot),
            nvrhi::BindingLayoutItem::Texture_UAV(
                PathTracingAcceptedCountUavSlot),
            nvrhi::BindingLayoutItem::Texture_UAV(
                PathTracingMotionUavSlot),
            nvrhi::BindingLayoutItem::Texture_UAV(
                PathTracingDepthUavSlot),
            nvrhi::BindingLayoutItem::Texture_UAV(
                PathTracingRetryGenerationUavSlot)
        };
        m_BindingLayout = device->createBindingLayout(layoutDesc);
        m_Shader = shaderFactory->CreateShader(
            "uvsr/path_tracing_cs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Compute);
        if (m_Shader && m_BindingLayout)
        {
            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = m_Shader;
            pipelineDesc.bindingLayouts = {
                m_BindingLayout,
                m_BindlessLayout
            };
            m_Pipeline = device->createComputePipeline(pipelineDesc);
        }
        if (!m_ConstantBuffer || !m_Sampler || !m_Pipeline)
        {
            log::error("The standard path-tracing pipeline could not be created");
        }
    }

    PathTracingAvailability PathTracingPass::GetAvailability()
        const noexcept
    {
        return ResolvePathTracingAvailability(
            m_Capabilities.rayQuerySupported,
            {
                bool(m_BindlessLayout),
                bool(m_ConstantBuffer),
                bool(m_Sampler),
                bool(m_BindingLayout),
                bool(m_Shader),
                bool(m_Pipeline)
            });
    }

    bool PathTracingPass::EnsureResources(uint32_t width, uint32_t height)
    {
        if (width == 0u || height == 0u)
            return false;
        if (m_RawMean && m_SuccessfulSampleCount && m_Motion && m_Depth &&
            m_RetryGeneration && m_Width == width && m_Height == height)
        {
            return true;
        }

        nvrhi::TextureHandle rawMean = CreateTexture(
            m_Device,
            width,
            height,
            nvrhi::Format::RGBA32_FLOAT,
            "Path Tracing/Cumulative Mean");
        nvrhi::TextureHandle count = CreateTexture(
            m_Device,
            width,
            height,
            nvrhi::Format::R32_UINT,
            "Path Tracing/Successful Sample Count");
        nvrhi::TextureHandle motion = CreateTexture(
            m_Device,
            width,
            height,
            nvrhi::Format::RGBA16_FLOAT,
            "Path Tracing/Motion");
        nvrhi::TextureHandle depth = CreateTexture(
            m_Device,
            width,
            height,
            nvrhi::Format::R32_FLOAT,
            "Path Tracing/Depth");
        nvrhi::TextureHandle retryGeneration = CreateTexture(
            m_Device,
            width,
            height,
            nvrhi::Format::R32_UINT,
            "Path Tracing/Retry Generation");
        if (!rawMean || !count || !motion || !depth || !retryGeneration)
            return false;

        nvrhi::TextureDesc readbackDesc;
        readbackDesc.width = 1u;
        readbackDesc.height = 1u;
        readbackDesc.dimension = nvrhi::TextureDimension::Texture2D;
        readbackDesc.format = nvrhi::Format::R32_UINT;
        readbackDesc.debugName =
            "Path Tracing/Center Accepted Sample Readback";
        for (AcceptedSampleReadbackSlot& slot :
            m_AcceptedSampleReadbacks)
        {
            if (!slot.texture)
            {
                slot.texture = m_Device->createStagingTexture(
                    readbackDesc,
                    nvrhi::CpuAccessMode::Read);
            }
            if (!slot.query)
                slot.query = m_Device->createEventQuery();
            if (!slot.texture || !slot.query)
                return false;
        }

        m_RawMean = rawMean;
        m_SuccessfulSampleCount = count;
        m_Motion = motion;
        m_Depth = depth;
        m_RetryGeneration = retryGeneration;
        m_Width = width;
        m_Height = height;
        m_BindingSet = nullptr;
        m_ResetRequested = true;
        m_HistoryValid = false;
        m_CurrentCenterPixelAcceptedSampleCount = 0u;
        return true;
    }

    bool PathTracingPass::EnsureLightBuffer(uint32_t lightCount)
    {
        const uint32_t required = std::max(lightCount, 1u);
        if (m_LightBuffer && m_LightCapacity >= required)
            return true;
        uint32_t capacity = std::max(m_LightCapacity, 1u);
        while (capacity < required &&
            capacity <= std::numeric_limits<uint32_t>::max() / 2u)
        {
            capacity *= 2u;
        }
        capacity = std::max(capacity, required);
        nvrhi::BufferDesc desc;
        desc.byteSize = uint64_t(capacity) * sizeof(LightConstants);
        desc.structStride = sizeof(LightConstants);
        desc.debugName = "Path Tracing/Analytic Lights";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        desc.keepInitialState = true;
        nvrhi::BufferHandle buffer = m_Device->createBuffer(desc);
        if (!buffer)
            return false;
        m_LightBuffer = buffer;
        m_LightCapacity = capacity;
        m_BindingSet = nullptr;
        return true;
    }

    bool PathTracingPass::EnsureBindingSet(const PathTracingInputs& inputs)
    {
        if (!inputs.worldTlas || !inputs.materialVisibility ||
            !inputs.materialVisibility.instanceBuffer ||
            !inputs.environment || !inputs.noiseTexture || !m_LightBuffer)
        {
            return false;
        }
        if (m_BoundTlas != inputs.worldTlas ||
            m_BoundMaterialVisibility != inputs.materialVisibility ||
            m_BoundEnvironment != inputs.environment ||
            m_BoundNoiseTexture != inputs.noiseTexture)
        {
            m_BindingSet = nullptr;
            m_BoundTlas = inputs.worldTlas;
            m_BoundMaterialVisibility = inputs.materialVisibility;
            m_BoundEnvironment = inputs.environment;
            m_BoundNoiseTexture = inputs.noiseTexture;
        }
        if (m_BindingSet)
            return true;

        nvrhi::BindingSetDesc desc;
        desc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(
                PathTracingConstantBufferSlot, m_ConstantBuffer),
            nvrhi::BindingSetItem::RayTracingAccelStruct(
                PathTracingWorldTlasSlot, inputs.worldTlas),
            nvrhi::BindingSetItem::Texture_SRV(
                PathTracingEnvironmentSlot, inputs.environment),
            nvrhi::BindingSetItem::Texture_SRV(
                PathTracingNoiseSlot, inputs.noiseTexture),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                10, inputs.materialVisibility.geometryBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                11, inputs.materialVisibility.materialBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                12, inputs.materialVisibility.geometryIndexMap),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                PathTracingLightsSlot, m_LightBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                PathTracingInstancesSlot,
                inputs.materialVisibility.instanceBuffer),
            nvrhi::BindingSetItem::Sampler(0, m_Sampler),
            nvrhi::BindingSetItem::Texture_UAV(
                PathTracingRawMeanUavSlot, m_RawMean),
            nvrhi::BindingSetItem::Texture_UAV(
                PathTracingAcceptedCountUavSlot,
                m_SuccessfulSampleCount),
            nvrhi::BindingSetItem::Texture_UAV(
                PathTracingMotionUavSlot, m_Motion),
            nvrhi::BindingSetItem::Texture_UAV(
                PathTracingDepthUavSlot, m_Depth),
            nvrhi::BindingSetItem::Texture_UAV(
                PathTracingRetryGenerationUavSlot,
                m_RetryGeneration)
        };
        m_BindingSet = m_Device->createBindingSet(desc, m_BindingLayout);
        return bool(m_BindingSet);
    }

    PathTracingResult PathTracingPass::Render(
        nvrhi::ICommandList* commandList,
        const PathTracingInputs& requestedInputs)
    {
        PathTracingResult failure;
        failure.capabilities = m_Capabilities;
        if (!IsSupported() || !commandList)
            return failure;

        PathTracingInputs inputs = requestedInputs;
        if (inputs.view && (inputs.width == 0u || inputs.height == 0u))
        {
            const nvrhi::Rect extent = inputs.view->GetViewExtent();
            inputs.width = uint32_t(std::max(extent.width(), 0));
            inputs.height = uint32_t(std::max(extent.height(), 0));
        }
        const bool valid = inputs.view && inputs.worldTlas &&
            bool(inputs.materialVisibility) &&
            inputs.materialVisibility.instanceBuffer &&
            inputs.environment &&
            inputs.environment->getDesc().dimension ==
                nvrhi::TextureDimension::TextureCube &&
            inputs.noiseTexture &&
            inputs.noiseTexture->getDesc().dimension ==
                nvrhi::TextureDimension::Texture2DArray &&
            IsValidNoiseSettings(inputs.noiseSettings) &&
            std::isfinite(inputs.environmentScale) &&
            inputs.environmentScale >= 0.f &&
            std::isfinite(inputs.rayBias) && inputs.rayBias > 0.f &&
            std::isfinite(inputs.maximumRayDistance) &&
            inputs.maximumRayDistance > inputs.rayBias &&
            EnsureResources(inputs.width, inputs.height);
        if (!valid)
        {
            if (!m_ReportedInvalidInput)
            {
                log::error("Path tracing received invalid resources or scalars");
                m_ReportedInvalidInput = true;
            }
            return failure;
        }

        PathTracingConstants constants{};
        inputs.view->FillPlanarViewConstants(constants.view);
        constants.previousView = constants.view;
        if (inputs.previousView)
        {
            inputs.previousView->FillPlanarViewConstants(
                constants.previousView);
            constants.previousViewValid = 1u;
        }
        uint32_t geometryMapCount = 0u;
        uint32_t geometryCount = 0u;
        uint32_t materialCount = 0u;
        uint32_t instanceCount = 0u;
        const uint32_t descriptorCapacity =
            inputs.materialVisibility.descriptorTable->getCapacity();
        if (!TryGetStructuredBufferCount(
                inputs.materialVisibility.geometryIndexMap,
                sizeof(uint32_t),
                geometryMapCount) ||
            !TryGetStructuredBufferCount(
                inputs.materialVisibility.geometryBuffer,
                sizeof(GeometryData),
                geometryCount) ||
            !TryGetStructuredBufferCount(
                inputs.materialVisibility.materialBuffer,
                sizeof(MaterialConstants),
                materialCount) ||
            !TryGetStructuredBufferCount(
                inputs.materialVisibility.instanceBuffer,
                sizeof(InstanceData),
                instanceCount) ||
            descriptorCapacity == 0u)
        {
            return failure;
        }
        constants.rayMaterialLimits = {
            geometryMapCount,
            geometryCount,
            materialCount,
            descriptorCapacity };
        constants.instanceCount = instanceCount;

        if (inputs.lights.size() >
            size_t(std::numeric_limits<uint32_t>::max()))
        {
            return failure;
        }
        std::vector<LightConstants> submittedLights;
        submittedLights.reserve(inputs.lights.size());
        constants.flashlight.lightIndex = -1;
        for (const std::shared_ptr<Light>& light : inputs.lights)
        {
            if (!light)
                continue;
            submittedLights.emplace_back();
            LightConstants& lightConstants = submittedLights.back();
            std::memset(&lightConstants, 0, sizeof(lightConstants));
            light->FillLightConstants(lightConstants);
            if (light.get() == inputs.flashlight)
                constants.flashlight.lightIndex =
                    int(submittedLights.size() - 1u);
        }
        constants.lightCount = uint32_t(submittedLights.size());
        if (!EnsureLightBuffer(constants.lightCount))
            return failure;
        if (constants.flashlight.lightIndex >= 0 &&
            FlashlightBeamProfileIsValid(inputs.flashlightProfile))
        {
            constants.flashlight.profile = inputs.flashlightProfile;
        }
        else
        {
            constants.flashlight = {};
            constants.flashlight.lightIndex = -1;
        }

        constants.environmentScale = inputs.environmentScale;
        constants.rayBias = inputs.rayBias;
        constants.maximumRayDistance = inputs.maximumRayDistance;
        constants.noisePattern =
            static_cast<uint32_t>(inputs.noiseSettings.pattern);
        constants.dispatchExtent = { inputs.width, inputs.height };
        if (inputs.view->IsReverseDepth())
            constants.flags |= UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH;
        if (inputs.showEnvironmentBackground)
        {
            constants.flags |=
                UVSR_PATH_TRACING_FLAG_SHOW_ENVIRONMENT_BACKGROUND;
        }

        const uint64_t signature =
            BuildInputSignature(inputs, submittedLights);
        const bool historyReset = m_ResetRequested || !m_HistoryValid ||
            m_LastHistoryEpoch != inputs.historyEpoch ||
            m_LastInputSignature != signature;
        if (historyReset)
            ClearHistory(commandList);
        m_LastHistoryEpoch = inputs.historyEpoch;
        m_LastInputSignature = signature;

        if (!EnsureBindingSet(inputs))
        {
            log::error("Path-tracing binding-set creation failed");
            return failure;
        }
        m_ReportedInvalidInput = false;
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));
        if (!submittedLights.empty())
        {
            commandList->writeBuffer(
                m_LightBuffer,
                submittedLights.data(),
                submittedLights.size() * sizeof(LightConstants));
        }

        nvrhi::ComputeState state;
        state.pipeline = m_Pipeline;
        state.bindings = {
            m_BindingSet,
            inputs.materialVisibility.descriptorTable
        };
        commandList->beginMarker("Standard Path Tracing");
        commandList->setComputeState(state);
        commandList->dispatch(
            div_ceil(inputs.width, 8u),
            div_ceil(inputs.height, 8u));
        for (size_t index = 0u;
            index < m_AcceptedSampleReadbacks.size();
            ++index)
        {
            AcceptedSampleReadbackSlot& slot =
                m_AcceptedSampleReadbacks[index];
            if (slot.submitted ||
                int(index) == m_PendingAcceptedSampleReadback)
            {
                continue;
            }
            nvrhi::TextureSlice sourceSlice;
            sourceSlice.setOrigin(
                inputs.width / 2u,
                inputs.height / 2u)
                .setSize(1u, 1u, 1u);
            commandList->copyTexture(
                slot.texture,
                nvrhi::TextureSlice{},
                m_SuccessfulSampleCount,
                sourceSlice);
            slot.generation = m_AcceptedSampleGeneration;
            m_PendingAcceptedSampleReadback = int(index);
            break;
        }
        commandList->endMarker();

        m_HistoryValid = true;
        m_ResetRequested = false;
        PathTracingResult result;
        result.sceneLinearDisplay = m_RawMean;
        result.rawMean = m_RawMean;
        result.temporalDepth = m_Depth;
        result.motionVectors = m_Motion;
        result.currentCenterPixelAcceptedSampleCount =
            m_CurrentCenterPixelAcceptedSampleCount;
        result.capabilities = m_Capabilities;
        result.dispatched = true;
        result.historyReset = historyReset;
        return result;
    }

    void PathTracingPass::ClearHistory(nvrhi::ICommandList* commandList)
    {
        commandList->clearTextureFloat(
            m_RawMean,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureUInt(
            m_SuccessfulSampleCount,
            nvrhi::AllSubresources,
            0u);
        commandList->clearTextureFloat(
            m_Motion,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_Depth,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureUInt(
            m_RetryGeneration,
            nvrhi::AllSubresources,
            UVSR_PATH_TRACING_RETRY_GENERATION_CLEARED);
        ++m_AcceptedSampleGeneration;
        if (m_AcceptedSampleGeneration == 0u)
            m_AcceptedSampleGeneration = 1u;
        m_CurrentCenterPixelAcceptedSampleCount = 0u;
        m_HistoryValid = false;
        m_ResetRequested = false;
    }

    void PathTracingPass::ResetHistory()
    {
        m_ResetRequested = true;
        m_CurrentCenterPixelAcceptedSampleCount = 0u;
    }

    void PathTracingPass::ResetBindingCache()
    {
        m_BindingSet = nullptr;
        m_BoundTlas = nullptr;
        m_BoundMaterialVisibility = {};
        m_BoundEnvironment = nullptr;
        m_BoundNoiseTexture = nullptr;
        ResetHistory();
    }

    void PathTracingPass::SubmitAcceptedSampleReadback()
    {
        if (m_PendingAcceptedSampleReadback < 0)
            return;
        AcceptedSampleReadbackSlot& slot = m_AcceptedSampleReadbacks[
            size_t(m_PendingAcceptedSampleReadback)];
        m_Device->setEventQuery(
            slot.query,
            nvrhi::CommandQueue::Graphics);
        slot.submitted = true;
        m_PendingAcceptedSampleReadback = -1;
    }

    void PathTracingPass::PollAcceptedSampleReadback()
    {
        for (AcceptedSampleReadbackSlot& slot :
            m_AcceptedSampleReadbacks)
        {
            if (!slot.submitted ||
                !m_Device->pollEventQuery(slot.query))
            {
                continue;
            }
            size_t rowPitch = 0u;
            const void* mapped = m_Device->mapStagingTexture(
                slot.texture,
                nvrhi::TextureSlice{},
                nvrhi::CpuAccessMode::Read,
                &rowPitch);
            if (mapped && rowPitch >= sizeof(uint32_t) &&
                slot.generation == m_AcceptedSampleGeneration)
            {
                m_CurrentCenterPixelAcceptedSampleCount =
                    *static_cast<const uint32_t*>(mapped);
            }
            if (mapped)
                m_Device->unmapStagingTexture(slot.texture);
            m_Device->resetEventQuery(slot.query);
            slot.submitted = false;
        }
    }
}
