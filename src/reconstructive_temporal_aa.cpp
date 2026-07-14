#include "reconstructive_temporal_aa.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <string>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "reconstructive_temporal_aa_cb.h"

namespace
{
    constexpr uint32_t kThreadGroupSize = 8;
    constexpr uint64_t kFNVOffsetBasis = 1469598103934665603ull;
    constexpr uint64_t kFNVPrime = 1099511628211ull;

    uint32_t GetPerformanceTier(
        const uvsr::ReconstructiveTemporalAASettings& settings)
    {
        return static_cast<uint32_t>(settings.performanceProfile);
    }

    bool UsesMaximumQuality(
        const uvsr::ReconstructiveTemporalAASettings& settings)
    {
        return settings.performanceProfile ==
            uvsr::ReconstructiveTemporalPerformanceProfile::MaximumQuality;
    }

    uvsr::HistorySampleFilter GetEffectiveHistoryFilter(
        const uvsr::ReconstructiveTemporalAASettings& settings)
    {
        return UsesMaximumQuality(settings)
            ? settings.historyFilter : uvsr::HistorySampleFilter::Bilinear;
    }

    bool UsesResurrection(
        const uvsr::ReconstructiveTemporalAASettings& settings)
    {
        return UsesMaximumQuality(settings) && settings.resurrectionEnabled;
    }

    uint64_t HashBytes(uint64_t hash, const void* data, size_t size)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= kFNVPrime;
        }
        return hash;
    }

    template <typename T>
    uint64_t HashValue(uint64_t hash, const T& value)
    {
        return HashBytes(hash, &value, sizeof(value));
    }

    float SquaredDistance(float3 a, float3 b)
    {
        const float3 difference = a - b;
        return dot(difference, difference);
    }

    float SafeDirectionDot(float3 a, float3 b)
    {
        const float aLengthSquared = dot(a, a);
        const float bLengthSquared = dot(b, b);
        if (aLengthSquared <= 1e-12f || bLengthSquared <= 1e-12f)
            return -1.f;

        return dot(a, b) / std::sqrt(aLengthSquared * bLengthSquared);
    }

    float MaximumMatrixDifference(const float4x4& a, const float4x4& b)
    {
        float maximumDifference = 0.f;
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                maximumDifference = std::max(maximumDifference,
                    std::abs(a[row][column] - b[row][column]));
            }
        }
        return maximumDifference;
    }

    float CurrentWeightFromMilliseconds(float deltaSeconds, float responseMilliseconds)
    {
        if (std::isnan(deltaSeconds) || deltaSeconds <= 0.f)
            return 0.f;
        if (std::isinf(deltaSeconds))
            return 1.f;

        const double timeConstant = std::max(
            static_cast<double>(responseMilliseconds) * 0.001, 1e-7);
        const double weight = -std::expm1(-static_cast<double>(deltaSeconds) / timeConstant);
        return static_cast<float>(std::clamp(weight, 0.0, 1.0));
    }

    std::shared_ptr<PlanarView> CaptureView(const IView& source)
    {
        const nvrhi::ViewportState viewportState = source.GetViewportState();
        assert(!viewportState.viewports.empty());
        if (viewportState.viewports.empty())
            return nullptr;

        auto result = std::make_shared<PlanarView>();
        result->SetViewport(viewportState.viewports.front());
        result->SetVariableRateShadingState(source.GetVariableRateShadingState());
        result->SetMatrices(source.GetViewMatrix(), source.GetProjectionMatrix(false));
        result->SetPixelOffset(source.GetPixelOffset());
        result->SetArraySlice(static_cast<int>(source.GetSubresources().baseArraySlice));
        result->UpdateCache();
        return result;
    }

    bool TextureMatches(nvrhi::ITexture* texture, uint2 resolution)
    {
        if (!texture)
            return false;
        const nvrhi::TextureDesc& desc = texture->getDesc();
        return desc.width == resolution.x && desc.height == resolution.y &&
            desc.dimension == nvrhi::TextureDimension::Texture2D;
    }
}

namespace uvsr
{
    ReconstructiveTemporalAAPass::ReconstructiveTemporalAAPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory)
        : m_Device(device)
    {
        assert(device);
        assert(shaderFactory);

        const nvrhi::FormatSupport surfaceSupport =
            device->queryFormatSupport(nvrhi::Format::RG32_UINT);
        const nvrhi::FormatSupport requiredSurfaceSupport =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavStore;
        assert((surfaceSupport & requiredSurfaceSupport) == requiredSurfaceSupport);

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(ReconstructiveTemporalAAConstants);
        constantBufferDesc.debugName = "NRA-RTAA/Constants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions = engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);

        m_LinearClampSampler = device->createSampler(nvrhi::SamplerDesc()
            .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
            .setAllFilters(true));

        CreatePipelines(shaderFactory);

        for (auto& stageQueries : m_TimerQueries)
        {
            for (auto& query : stageQueries)
                query = device->createTimerQuery();
        }
    }

    void ReconstructiveTemporalAAPass::CreatePipelines(
        const std::shared_ptr<ShaderFactory>& shaderFactory)
    {
        auto createPipeline = [this, &shaderFactory](
            Pipeline& destination,
            const char* shaderName,
            const std::vector<nvrhi::BindingLayoutItem>& bindings,
            const std::vector<ShaderMacro>* macros = nullptr)
        {
            destination.shader = shaderFactory->CreateShader(
                shaderName, "main", macros, nvrhi::ShaderType::Compute);

            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::Compute;
            layoutDesc.bindings.assign(bindings.begin(), bindings.end());
            destination.bindingLayout = m_Device->createBindingLayout(layoutDesc);

            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = destination.shader;
            pipelineDesc.bindingLayouts = { destination.bindingLayout };
            destination.pipeline = m_Device->createComputePipeline(pipelineDesc);
        };

        createPipeline(m_PreparePipeline, "uvsr/rtaa_prepare_cs.hlsl", {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1)
        });

        // Performance/Balanced force bilinear and compile out resurrection.
        // Maximum Quality supports both filters and optional resurrection.
        // Building only reachable variants avoids six dead shader blobs/PSOs.
        constexpr std::array<uint32_t, 6> ResolveVariants = {
            0u, 4u, 8u, 9u, 10u, 11u
        };
        for (uint32_t variant : ResolveVariants)
        {
            std::vector<ShaderMacro> macros;
            macros.emplace_back("RTAA_VARIANT", std::to_string(variant));

            std::vector<nvrhi::BindingLayoutItem> resolveBindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Sampler(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(4),
                nvrhi::BindingLayoutItem::Texture_SRV(6),
                nvrhi::BindingLayoutItem::Texture_SRV(7),
                nvrhi::BindingLayoutItem::Texture_SRV(8),
                nvrhi::BindingLayoutItem::Texture_SRV(9),
                nvrhi::BindingLayoutItem::Texture_SRV(10),
                nvrhi::BindingLayoutItem::Texture_SRV(11),
                nvrhi::BindingLayoutItem::Texture_SRV(12),
                nvrhi::BindingLayoutItem::Texture_SRV(13),
                nvrhi::BindingLayoutItem::Texture_SRV(14)
            };
            if ((variant & 2u) != 0u)
            {
                for (uint32_t slot = 15u; slot <= 22u; ++slot)
                    resolveBindings.push_back(
                        nvrhi::BindingLayoutItem::Texture_SRV(slot));
            }
            for (uint32_t slot = 0u; slot <= 5u; ++slot)
            {
                resolveBindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_UAV(slot));
            }

            createPipeline(m_ResolvePipelines[variant],
                "uvsr/rtaa_resolve_cs.hlsl", resolveBindings, &macros);
        }
    }

    bool ReconstructiveTemporalAAPass::DebugNeedsFullResolution(
        ReconstructiveTemporalDebugMode mode)
    {
        return mode != ReconstructiveTemporalDebugMode::FinalOutput &&
            mode != ReconstructiveTemporalDebugMode::FinalNraRtaaOutput &&
            mode != ReconstructiveTemporalDebugMode::SharpeningContribution;
    }

    bool ReconstructiveTemporalAAPass::EnsureResources(
        uint2 resolution,
        const ReconstructiveTemporalAASettings& settings)
    {
        const uint32_t persistentCount = UsesResurrection(settings)
            ? std::min(settings.persistentFrameCount, 2u)
            : 0u;
        const uint32_t persistentInterval = std::clamp(
            settings.persistentFrameInterval, 1u, 2u);
        const uint32_t historySlotCount = 2u + persistentCount * persistentInterval;
        const bool historyResourcesChanged = !m_PreparedData || !m_Classification ||
            !all(m_Resolution == resolution) || m_History.size() != historySlotCount;

        auto createTexture = [this](
            uint2 size,
            nvrhi::Format format,
            const std::string& debugName)
        {
            nvrhi::TextureDesc desc;
            desc.width = size.x;
            desc.height = size.y;
            desc.format = format;
            desc.dimension = nvrhi::TextureDimension::Texture2D;
            desc.mipLevels = 1;
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            desc.debugName = debugName;
            return m_Device->createTexture(desc);
        };

        if (historyResourcesChanged)
        {
            m_PreparedData = nullptr;
            m_Classification = nullptr;
            m_History.clear();

            m_Resolution = resolution;
            m_PreparedData = createTexture(resolution, nvrhi::Format::RG16_FLOAT,
                "NRA-RTAA/PreparedData");
            m_Classification = createTexture(resolution, nvrhi::Format::RGBA8_UNORM,
                "NRA-RTAA/Classification");

            m_History.resize(historySlotCount);
            for (uint32_t index = 0; index < historySlotCount; ++index)
            {
                const std::string prefix = "NRA-RTAA/History" + std::to_string(index) + "/";
                HistorySlot& slot = m_History[index];
                slot.color = createTexture(resolution, nvrhi::Format::RGBA16_FLOAT,
                    prefix + "Color");
                slot.moments = createTexture(resolution, nvrhi::Format::RG16_FLOAT,
                    prefix + "Moments");
                slot.metadata = createTexture(resolution, nvrhi::Format::RGBA8_UNORM,
                    prefix + "Metadata");
                slot.depth = createTexture(resolution, nvrhi::Format::R32_FLOAT,
                    prefix + "Depth");
                // Exact 32-bit material identity plus packed normal/object
                // token. DX12 guarantees the typed load/store support checked
                // by the constructor on every supported adapter.
                slot.normalMaterial = createTexture(resolution, nvrhi::Format::RG32_UINT,
                    prefix + "NormalMaterial");
            }

            ResetHistory();
        }

        if (!m_DebugSink)
        {
            m_DebugSink = createTexture(uint2(1u, 1u), nvrhi::Format::RGBA16_FLOAT,
                "NRA-RTAA/DebugSink1x1");
        }

        const bool needsFullDebug = DebugNeedsFullResolution(settings.debugMode);
        if (needsFullDebug && (!m_DebugOutput ||
            !TextureMatches(m_DebugOutput, resolution)))
        {
            m_DebugOutput = createTexture(resolution, nvrhi::Format::RGBA16_FLOAT,
                "NRA-RTAA/DebugOutput");
        }
        else if (!needsFullDebug)
        {
            m_DebugOutput = nullptr;
        }

        m_PersistentCount = persistentCount;
        m_PersistentInterval = persistentInterval;

        const uint64_t pixelCount = static_cast<uint64_t>(resolution.x) * resolution.y;
        m_MemoryStats.resolution = resolution;
        m_MemoryStats.historySlotCount = historySlotCount;
        m_MemoryStats.persistentHistoryCount = persistentCount;
        m_MemoryStats.transientBytes = pixelCount * 8ull;
        m_MemoryStats.historyBytes = pixelCount * 28ull * historySlotCount;
        m_MemoryStats.debugBytes = 8ull +
            (needsFullDebug ? pixelCount * 8ull : 0ull);
        m_MemoryStats.totalBytes = m_MemoryStats.transientBytes +
            m_MemoryStats.historyBytes + m_MemoryStats.debugBytes;
        // Steady-state logical texel footprint before cache reuse. Prepare's
        // valid-center path reads depth, RGBA16F motion, diffuse, and specular
        // (20 B); uncovered pixels may conditionally inspect eight more depths.
        // Resolve amortizes a 10x10 or 12x12 28-B shared tile over each 8x8
        // group, then directly reads prepared/classification/raw motion (16 B).
        // Immediate history adds a 4-texel bilinear or 16-texel Catmull color
        // footprint plus 20 B of metadata/depth/surface. Conditional dilation
        // and persistent resurrection reads are scene-dependent and excluded.
        const float prepareReadBytes = 20.f;
        const HistorySampleFilter effectiveFilter = GetEffectiveHistoryFilter(settings);
        const float immediateHistoryReadBytes =
            effectiveFilter == HistorySampleFilter::CatmullRom
            ? 148.f : 52.f;
        const float resolveBaseReadBytes = UsesMaximumQuality(settings)
            ? 79.f : 60.f;
        m_MemoryStats.approximateReadBytesPerPixel = prepareReadBytes +
            resolveBaseReadBytes + immediateHistoryReadBytes;
        m_MemoryStats.approximateWriteBytesPerPixel = needsFullDebug ? 44.f : 36.f;

        return historyResourcesChanged;
    }

    void ReconstructiveTemporalAAPass::ReleaseResources()
    {
        m_PreparedData = nullptr;
        m_Classification = nullptr;
        m_DebugOutput = nullptr;
        m_DebugSink = nullptr;
        m_History.clear();
        m_Resolution = uint2::zero();
        m_PersistentCount = 0;
        m_PersistentInterval = 1;
        m_Output = nullptr;
        m_MemoryStats = {};
        ResetHistory();
    }

    void ReconstructiveTemporalAAPass::ResetHistory()
    {
        for (HistorySlot& slot : m_History)
        {
            slot.view.reset();
            slot.frameIndex = 0;
            slot.valid = false;
        }

        m_NextWriteIndex = 0;
        m_LatestOutputIndex = 0;
        m_HistoryValid = false;
        m_HistorySignature = 0;
        m_HasPreviousCamera = false;
        m_ReverseDepth = false;
        m_PreviousCameraOrigin = float3::zero();
        m_PreviousCameraDirection = float3(0.f, 0.f, 1.f);
        m_PreviousProjection = float4x4::identity();
        m_PreviousJitter = float2::zero();
        m_Output = nullptr;
    }

    void ReconstructiveTemporalAAPass::Deactivate()
    {
        // If a caller deactivates between Resolve and the downstream timer
        // hook, retain the pending state. The next Render has a command list
        // and can close the query safely; silently clearing this bit would
        // strand an active NVRHI timer query forever.
        ReleaseResources();
        m_WasEnabled = false;
        m_Timings = {};
        m_Weights = {};
    }

    uint32_t ReconstructiveTemporalAAPass::SlotAtAge(uint32_t age) const
    {
        assert(!m_History.empty());
        assert(age < m_History.size());
        return (m_NextWriteIndex + static_cast<uint32_t>(m_History.size()) -
            age) % static_cast<uint32_t>(m_History.size());
    }

    ReconstructiveTemporalAAPass::HistorySlot*
        ReconstructiveTemporalAAPass::GetValidSlotAtAge(uint32_t age)
    {
        if (m_History.empty() || age >= m_History.size())
            return nullptr;
        HistorySlot& slot = m_History[SlotAtAge(age)];
        return slot.valid && slot.view ? &slot : nullptr;
    }

    const ReconstructiveTemporalAAPass::HistorySlot*
        ReconstructiveTemporalAAPass::GetLatestSlot() const
    {
        if (m_History.empty() || m_LatestOutputIndex >= m_History.size())
            return nullptr;
        const HistorySlot& slot = m_History[m_LatestOutputIndex];
        return slot.valid ? &slot : nullptr;
    }

    nvrhi::ITexture* ReconstructiveTemporalAAPass::GetHistoryMetadata() const
    {
        const HistorySlot* slot = GetLatestSlot();
        return slot ? slot->metadata.Get() : nullptr;
    }

    nvrhi::ITexture* ReconstructiveTemporalAAPass::GetHistoryMoments() const
    {
        const HistorySlot* slot = GetLatestSlot();
        return slot ? slot->moments.Get() : nullptr;
    }

    uint64_t ReconstructiveTemporalAAPass::ComputeHistorySignature(
        const ReconstructiveTemporalAASettings& settings)
    {
        uint64_t hash = kFNVOffsetBasis;
        hash = HashValue(hash, settings.preset);
        hash = HashValue(hash, settings.performanceProfile);
        hash = HashValue(hash, settings.jitterSequence);
        hash = HashValue(hash, settings.jitterPeriod);
        hash = HashValue(hash, settings.jitterScale);
        hash = HashValue(hash, settings.historyFilter);
        hash = HashValue(hash, settings.velocityDilationEnabled);
        hash = HashValue(hash, settings.motionResponseStartPixels);
        hash = HashValue(hash, settings.motionResponseEndPixels);
        hash = HashValue(hash, settings.absoluteDepthThreshold);
        hash = HashValue(hash, settings.relativeDepthThreshold);
        hash = HashValue(hash, settings.normalRejectCosine);
        hash = HashValue(hash, settings.normalAcceptCosine);
        hash = HashValue(hash, settings.validateMaterialIdentity);
        hash = HashValue(hash, settings.validateObjectIdentity);
        hash = HashValue(hash, settings.explicitReactiveMaskEnabled);
        hash = HashValue(hash, settings.automaticReactiveMaskEnabled);
        hash = HashValue(hash, settings.automaticReactiveStrength);
        hash = HashValue(hash, settings.reactiveLuminanceThreshold);
        hash = HashValue(hash, settings.reactiveChromaThreshold);
        hash = HashValue(hash, settings.thinGeometryEnabled);
        hash = HashValue(hash, settings.thinGeometryDepthRange);
        hash = HashValue(hash, settings.thinGeometryContrastThreshold);
        hash = HashValue(hash, settings.thinGeometryCoverageResponseMs);
        hash = HashValue(hash, settings.thinGeometryMaximumRelaxation);
        hash = HashValue(hash, settings.thinGeometryClusterDiffusion);
        hash = HashValue(hash, settings.varianceClipSigma);
        hash = HashValue(hash, settings.luminanceClipStrength);
        hash = HashValue(hash, settings.chromaClipStrength);
        hash = HashValue(hash, settings.thinGeometryClipExpansion);
        hash = HashValue(hash, settings.stableResponseMs);
        hash = HashValue(hash, settings.movingResponseMs);
        hash = HashValue(hash, settings.reactiveResponseMs);
        hash = HashValue(hash, settings.maximumHistorySamples);
        hash = HashValue(hash, settings.maximumMovingHistorySamples);
        hash = HashValue(hash, settings.spatialFallbackEnabled);
        hash = HashValue(hash, settings.spatialFallbackRadius);
        hash = HashValue(hash, settings.spatialDepthWeight);
        hash = HashValue(hash, settings.spatialNormalWeight);
        hash = HashValue(hash, settings.spatialLuminanceWeight);
        hash = HashValue(hash, settings.resurrectionEnabled);
        hash = HashValue(hash, settings.persistentFrameCount);
        hash = HashValue(hash, settings.persistentFrameInterval);
        hash = HashValue(hash, settings.maximumResurrectionWeight);
        hash = HashValue(hash, settings.resurrectionMatchThreshold);
        hash = HashValue(hash, settings.freezeJitter);
        hash = HashValue(hash, settings.holdJitterPhase);
        hash = HashValue(hash, settings.heldJitterPhase);
        return hash;
    }

    bool ReconstructiveTemporalAAPass::UpdateHistoryValidity(
        const ReconstructiveTemporalAASettings& settings,
        const IView& currentView,
        const IView* previousView,
        bool resourcesChanged,
        bool forceReset)
    {
        const uint64_t signature = ComputeHistorySignature(settings);
        bool valid = m_HistoryValid && m_WasEnabled && !resourcesChanged &&
            !forceReset && signature == m_HistorySignature && previousView;

        if (!m_HasPreviousCamera || !previousView)
        {
            valid = false;
        }
        else
        {
            // Ten world units in one frame is treated as a teleport. Regular
            // translation is handled analytically by the stored view matrices.
            constexpr float kTeleportDistance = 10.f;
            if (SquaredDistance(currentView.GetViewOrigin(), previousView->GetViewOrigin()) >
                kTeleportDistance * kTeleportDistance)
            {
                valid = false;
            }
            if (SafeDirectionDot(currentView.GetViewDirection(),
                previousView->GetViewDirection()) < 0.75f)
            {
                valid = false;
            }
            if (MaximumMatrixDifference(currentView.GetProjectionMatrix(false),
                previousView->GetProjectionMatrix(false)) > 1e-4f)
            {
                valid = false;
            }
            if (currentView.IsReverseDepth() != previousView->IsReverseDepth() ||
                currentView.IsReverseDepth() != m_ReverseDepth)
            {
                valid = false;
            }

            // The caller's previous view must identify the exact view that
            // produced the immediate history, not merely another nearby camera.
            const HistorySlot* latest = GetLatestSlot();
            const float2 previousJitterDifference = latest && latest->view
                ? previousView->GetPixelOffset() - latest->view->GetPixelOffset()
                : float2(std::numeric_limits<float>::infinity());
            if (!latest || !latest->view ||
                SquaredDistance(previousView->GetViewOrigin(),
                    latest->view->GetViewOrigin()) > 1e-6f ||
                SafeDirectionDot(previousView->GetViewDirection(),
                    latest->view->GetViewDirection()) < 0.9999f ||
                dot(previousJitterDifference, previousJitterDifference) > 1e-8f ||
                MaximumMatrixDifference(previousView->GetProjectionMatrix(false),
                    latest->view->GetProjectionMatrix(false)) > 1e-5f)
            {
                valid = false;
            }
        }

        // This renderer has no pre-exposure multiplier; scene color and every
        // history slot therefore share the same radiometric convention.
        m_HistorySignature = signature;
        m_PreviousCameraOrigin = currentView.GetViewOrigin();
        m_PreviousCameraDirection = currentView.GetViewDirection();
        m_PreviousProjection = currentView.GetProjectionMatrix(false);
        m_PreviousJitter = currentView.GetPixelOffset();
        m_ReverseDepth = currentView.IsReverseDepth();
        m_HasPreviousCamera = true;
        return valid;
    }

    void ReconstructiveTemporalAAPass::AdvanceTimers()
    {
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        for (uint32_t stageIndex = 0;
            stageIndex < static_cast<uint32_t>(Stage::Count); ++stageIndex)
        {
            if (!m_TimerPending[stageIndex][slot])
                continue;

            nvrhi::ITimerQuery* query = m_TimerQueries[stageIndex][slot];
            if (!m_Device->pollTimerQuery(query))
                continue;

            const float milliseconds = m_Device->getTimerQueryTime(query) * 1000.f;
            m_Device->resetTimerQuery(query);
            m_TimerPending[stageIndex][slot] = false;

            switch (static_cast<Stage>(stageIndex))
            {
            case Stage::Prepare: m_Timings.prepareMs = milliseconds; break;
            case Stage::Resolve: m_Timings.resolveMs = milliseconds; break;
            case Stage::FusedSharpening: m_Timings.fusedSharpeningMs = milliseconds; break;
            case Stage::Total: m_Timings.totalMs = milliseconds; break;
            default: break;
            }
        }

        m_Timings.resurrectionOverheadMs = 0.f;
        m_Timings.resurrectionOverheadIsFused = true;
    }

    void ReconstructiveTemporalAAPass::BeginStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        if (m_TimerPending[stageIndex][slot] || m_TimerActive[stageIndex])
            return;

        commandList->beginTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerActive[stageIndex] = true;
        m_TimerActiveSlot[stageIndex] = slot;
    }

    void ReconstructiveTemporalAAPass::EndStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        if (!m_TimerActive[stageIndex])
            return;

        const uint32_t slot = m_TimerActiveSlot[stageIndex];
        commandList->endTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerPending[stageIndex][slot] = true;
        m_TimerActive[stageIndex] = false;
    }

    void ReconstructiveTemporalAAPass::BeginFusedSharpeningTimer(
        nvrhi::ICommandList* commandList)
    {
        if (!commandList || !m_AwaitingFusedSharpening)
            return;
        BeginStage(commandList, Stage::FusedSharpening);
    }

    void ReconstructiveTemporalAAPass::EndFusedSharpeningTimer(
        nvrhi::ICommandList* commandList)
    {
        if (!commandList || !m_AwaitingFusedSharpening)
            return;

        EndStage(commandList, Stage::FusedSharpening);
        EndStage(commandList, Stage::Total);
        m_AwaitingFusedSharpening = false;
        ++m_TimerFrame;
    }

    void ReconstructiveTemporalAAPass::Render(
        nvrhi::ICommandList* commandList,
        ReconstructiveTemporalAASettings& settings,
        const IView& currentView,
        const IView* previousView,
        const Inputs& inputs,
        float deltaSeconds,
        uint64_t frameIndex)
    {
        SanitizeReconstructiveTemporalSettings(settings);
        if (!settings.enabled)
        {
            if (commandList && m_AwaitingFusedSharpening)
            {
                EndStage(commandList, Stage::FusedSharpening);
                EndStage(commandList, Stage::Total);
                m_AwaitingFusedSharpening = false;
                ++m_TimerFrame;
            }
            Deactivate();
            return;
        }

        assert(commandList);
        assert(inputs.sceneColor && inputs.depth && inputs.gbufferDiffuse &&
            inputs.gbufferSpecular && inputs.surfaceIds && inputs.motionVectors);
        if (!commandList || !inputs.sceneColor || !inputs.depth ||
            !inputs.gbufferDiffuse || !inputs.gbufferSpecular ||
            !inputs.surfaceIds || !inputs.motionVectors)
        {
            ResetHistory();
            m_Output = inputs.sceneColor;
            return;
        }

        const nvrhi::TextureDesc& colorDesc = inputs.sceneColor->getDesc();
        const uint2 resolution(colorDesc.width, colorDesc.height);
        const nvrhi::ViewportState viewportState = currentView.GetViewportState();
        const bool viewMatches = !viewportState.viewports.empty() &&
            std::abs(viewportState.viewports.front().width() - float(resolution.x)) < 0.5f &&
            std::abs(viewportState.viewports.front().height() - float(resolution.y)) < 0.5f;
        const bool inputsMatch = resolution.x > 0 && resolution.y > 0 &&
            viewMatches &&
            TextureMatches(inputs.depth, resolution) &&
            TextureMatches(inputs.gbufferDiffuse, resolution) &&
            TextureMatches(inputs.gbufferSpecular, resolution) &&
            TextureMatches(inputs.surfaceIds, resolution) &&
            TextureMatches(inputs.motionVectors, resolution) &&
            (!inputs.explicitReactiveMask ||
                TextureMatches(inputs.explicitReactiveMask, resolution));
        assert(inputsMatch);
        if (!inputsMatch)
        {
            ResetHistory();
            m_Output = inputs.sceneColor;
            return;
        }

        // Close a total query if integration skipped the optional downstream
        // sharpening bracket. This keeps the four-frame ring nonblocking.
        if (m_AwaitingFusedSharpening)
        {
            EndStage(commandList, Stage::FusedSharpening);
            EndStage(commandList, Stage::Total);
            m_AwaitingFusedSharpening = false;
            ++m_TimerFrame;
        }

        const bool forceReset = settings.forceHistoryReset;
        settings.forceHistoryReset = false;
        const bool resourcesChanged = EnsureResources(resolution, settings);
        AdvanceTimers();

        bool historyValid = UpdateHistoryValidity(settings, currentView,
            previousView, resourcesChanged, forceReset);
        if (!historyValid)
        {
            for (HistorySlot& slot : m_History)
            {
                slot.valid = false;
                slot.view.reset();
            }
            m_NextWriteIndex = 0;
            m_HistoryValid = false;
        }

        m_Weights = ComputeReconstructiveTemporalWeights(settings, deltaSeconds);
        const float prepareReadBytes = 20.f +
            (inputs.explicitReactiveMask && settings.explicitReactiveMaskEnabled
                ? 1.f : 0.f);
        const HistorySampleFilter effectiveFilter = GetEffectiveHistoryFilter(settings);
        const float immediateHistoryReadBytes =
            effectiveFilter == HistorySampleFilter::CatmullRom
            ? 148.f : 52.f;
        const float resolveBaseReadBytes = UsesMaximumQuality(settings)
            ? 79.f : 60.f;
        m_MemoryStats.approximateReadBytesPerPixel = prepareReadBytes +
            resolveBaseReadBytes + immediateHistoryReadBytes;

        HistorySlot& immediateStorage = m_History[SlotAtAge(1)];
        HistorySlot* immediate = historyValid ? GetValidSlotAtAge(1) : nullptr;
        const uint32_t persistentAge0 = 1u + m_PersistentInterval;
        const uint32_t persistentAge1 = 1u + 2u * m_PersistentInterval;
        HistorySlot* persistent0 = m_PersistentCount >= 1
            ? GetValidSlotAtAge(persistentAge0) : nullptr;
        HistorySlot* persistent1 = m_PersistentCount >= 2
            ? GetValidSlotAtAge(persistentAge1) : nullptr;
        uint32_t persistentValidMask = 0;
        if (persistent0) persistentValidMask |= 1u;
        if (persistent1) persistentValidMask |= 2u;

        const IView& immediateConstantsView = immediate && immediate->view
            ? static_cast<const IView&>(*immediate->view)
            : (previousView ? *previousView : currentView);
        const IView& persistentConstantsView0 = persistent0 && persistent0->view
            ? static_cast<const IView&>(*persistent0->view)
            : immediateConstantsView;
        const IView& persistentConstantsView1 = persistent1 && persistent1->view
            ? static_cast<const IView&>(*persistent1->view)
            : persistentConstantsView0;

        ReconstructiveTemporalAAConstants constants{};
        currentView.FillPlanarViewConstants(constants.currentView);
        immediateConstantsView.FillPlanarViewConstants(constants.immediateHistoryView);
        persistentConstantsView0.FillPlanarViewConstants(constants.persistentHistoryView0);
        persistentConstantsView1.FillPlanarViewConstants(constants.persistentHistoryView1);
        constants.resolution = float2(resolution);
        constants.invResolution = 1.f / constants.resolution;
        constants.currentJitter = currentView.GetPixelOffset();
        constants.previousJitter = immediateConstantsView.GetPixelOffset();
        constants.stableCurrentWeight = m_Weights.stableCurrentWeight;
        constants.movingCurrentWeight = m_Weights.movingCurrentWeight;
        constants.reactiveCurrentWeight = m_Weights.reactiveCurrentWeight;
        constants.thinCoverageCurrentWeight = CurrentWeightFromMilliseconds(
            deltaSeconds, settings.thinGeometryCoverageResponseMs);
        constants.motionWeightStartPixels = settings.motionResponseStartPixels;
        constants.motionWeightEndPixels = settings.motionResponseEndPixels;
        constants.depthThresholdAbsolute = settings.absoluteDepthThreshold;
        constants.depthThresholdRelative = settings.relativeDepthThreshold;
        constants.normalRejectCosine = settings.normalRejectCosine;
        constants.normalAcceptCosine = settings.normalAcceptCosine;
        constants.automaticReactiveStrength = settings.automaticReactiveStrength;
        constants.automaticReactiveLumaThreshold = settings.reactiveLuminanceThreshold;
        constants.automaticReactiveChromaThreshold = settings.reactiveChromaThreshold;
        constants.thinDepthThreshold = settings.thinGeometryDepthRange;
        constants.thinContrastThreshold = settings.thinGeometryContrastThreshold;
        constants.thinMaxRelaxation = settings.thinGeometryMaximumRelaxation;
        const float thinLockFrames =
            static_cast<float>(std::max(settings.jitterPeriod, 1u)) + 2.f;
        // The lifetime is stored in R8. Quantize the decrement upward once on
        // the CPU so repeated UNORM writes can neither stall nor outlive the
        // configured jitter cycle through nearest-even rounding.
        constants.thinLockDecayPerFrame =
            std::ceil(255.f / thinLockFrames) / 255.f;
        constants.varianceSigma = settings.varianceClipSigma;
        constants.varianceLumaScale = settings.luminanceClipStrength;
        constants.varianceChromaScale = settings.chromaClipStrength;
        constants.thinClipExpansion = settings.thinGeometryClipExpansion;
        constants.spatialDepthWeight = settings.spatialDepthWeight;
        constants.spatialNormalWeight = settings.spatialNormalWeight;
        constants.spatialLumaWeight = settings.spatialLuminanceWeight;
        constants.resurrectionMaxWeight = settings.maximumResurrectionWeight;
        constants.resurrectionMatchThreshold = settings.resurrectionMatchThreshold;
        constants.maxHistorySamples = settings.maximumHistorySamples;
        constants.maxMovingHistorySamples = settings.maximumMovingHistorySamples;
        constants.spatialFallbackRadius = settings.spatialFallbackRadius;
        constants.debugMode = static_cast<uint32_t>(settings.debugMode);
        constants.historyValid = historyValid ? 1u : 0u;
        constants.reverseZ = currentView.IsReverseDepth() ? 1u : 0u;
        constants.enableVelocityDilation = settings.velocityDilationEnabled ? 1u : 0u;
        constants.enableMaterialValidation = settings.validateMaterialIdentity ? 1u : 0u;
        constants.enableObjectValidation = settings.validateObjectIdentity ? 1u : 0u;
        constants.enableExplicitReactive = settings.explicitReactiveMaskEnabled &&
            inputs.explicitReactiveMask ? 1u : 0u;
        constants.enableAutomaticReactive = settings.automaticReactiveMaskEnabled ? 1u : 0u;
        constants.enableThinGeometry = settings.thinGeometryEnabled ? 1u : 0u;
        constants.enableThinDiffusion = UsesMaximumQuality(settings) &&
            settings.thinGeometryClusterDiffusion ? 1u : 0u;
        constants.enableSpatialFallback = settings.spatialFallbackEnabled ? 1u : 0u;
        constants.enableResurrection = UsesResurrection(settings) &&
            persistentValidMask != 0u ? 1u : 0u;
        constants.persistentValidMask = persistentValidMask;
        constants.writeDebug = DebugNeedsFullResolution(settings.debugMode) ? 1u : 0u;
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

        const uint32_t dispatchX = (resolution.x + kThreadGroupSize - 1u) /
            kThreadGroupSize;
        const uint32_t dispatchY = (resolution.y + kThreadGroupSize - 1u) /
            kThreadGroupSize;
        // t4 is scalar. When the optional mask is absent, bind the compatible
        // depth SRV; enableExplicitReactive guarantees that it is never read.
        nvrhi::ITexture* reactiveTexture = inputs.explicitReactiveMask
            ? inputs.explicitReactiveMask : inputs.depth;

        commandList->beginMarker("NRA-RTAA");
        BeginStage(commandList, Stage::Total);

        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, inputs.depth),
                nvrhi::BindingSetItem::Texture_SRV(1, inputs.motionVectors),
                nvrhi::BindingSetItem::Texture_SRV(2, inputs.gbufferDiffuse),
                nvrhi::BindingSetItem::Texture_SRV(3, inputs.gbufferSpecular),
                nvrhi::BindingSetItem::Texture_SRV(4, reactiveTexture),
                nvrhi::BindingSetItem::Texture_UAV(0, m_PreparedData),
                nvrhi::BindingSetItem::Texture_UAV(1, m_Classification)
            };
            nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(
                bindings, m_PreparePipeline.bindingLayout);
            nvrhi::ComputeState state;
            state.pipeline = m_PreparePipeline.pipeline;
            state.bindings = { bindingSet };
            commandList->beginMarker("RTAA Prepare");
            BeginStage(commandList, Stage::Prepare);
            commandList->setComputeState(state);
            commandList->dispatch(dispatchX, dispatchY, 1);
            EndStage(commandList, Stage::Prepare);
            commandList->endMarker();
        }

        HistorySlot& writeSlot = m_History[m_NextWriteIndex];
        HistorySlot& persistentStorage0 = persistent0 ? *persistent0 : immediateStorage;
        HistorySlot& persistentStorage1 = persistent1 ? *persistent1 : persistentStorage0;
        nvrhi::ITexture* debugTarget = m_DebugOutput
            ? m_DebugOutput.Get() : m_DebugSink.Get();
        const bool useResurrection = UsesResurrection(settings) &&
            persistentValidMask != 0u;
        const uint32_t resolveVariant =
            (effectiveFilter == HistorySampleFilter::CatmullRom ? 1u : 0u) |
            (useResurrection ? 2u : 0u) |
            (GetPerformanceTier(settings) << 2u);
        Pipeline& resolvePipeline = m_ResolvePipelines[resolveVariant];

        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
                nvrhi::BindingSetItem::Texture_SRV(0, inputs.sceneColor),
                nvrhi::BindingSetItem::Texture_SRV(1, inputs.depth),
                nvrhi::BindingSetItem::Texture_SRV(4, inputs.gbufferSpecular),
                nvrhi::BindingSetItem::Texture_SRV(6, inputs.surfaceIds),
                nvrhi::BindingSetItem::Texture_SRV(7, m_PreparedData),
                nvrhi::BindingSetItem::Texture_SRV(8, m_Classification),
                nvrhi::BindingSetItem::Texture_SRV(9, inputs.motionVectors),
                nvrhi::BindingSetItem::Texture_SRV(10, immediateStorage.color),
                nvrhi::BindingSetItem::Texture_SRV(11, immediateStorage.moments),
                nvrhi::BindingSetItem::Texture_SRV(12, immediateStorage.metadata),
                nvrhi::BindingSetItem::Texture_SRV(13, immediateStorage.depth),
                nvrhi::BindingSetItem::Texture_SRV(14, immediateStorage.normalMaterial)
            };
            if (useResurrection)
            {
                bindings.bindings.insert(bindings.bindings.end(), {
                nvrhi::BindingSetItem::Texture_SRV(15, persistentStorage0.color),
                nvrhi::BindingSetItem::Texture_SRV(16, persistentStorage0.metadata),
                nvrhi::BindingSetItem::Texture_SRV(17, persistentStorage0.depth),
                nvrhi::BindingSetItem::Texture_SRV(18, persistentStorage0.normalMaterial),
                nvrhi::BindingSetItem::Texture_SRV(19, persistentStorage1.color),
                nvrhi::BindingSetItem::Texture_SRV(20, persistentStorage1.metadata),
                nvrhi::BindingSetItem::Texture_SRV(21, persistentStorage1.depth),
                nvrhi::BindingSetItem::Texture_SRV(22, persistentStorage1.normalMaterial)
                });
            }
            bindings.bindings.insert(bindings.bindings.end(), {
                nvrhi::BindingSetItem::Texture_UAV(0, writeSlot.color),
                nvrhi::BindingSetItem::Texture_UAV(1, writeSlot.moments),
                nvrhi::BindingSetItem::Texture_UAV(2, writeSlot.metadata),
                nvrhi::BindingSetItem::Texture_UAV(3, writeSlot.depth),
                nvrhi::BindingSetItem::Texture_UAV(4, writeSlot.normalMaterial),
                nvrhi::BindingSetItem::Texture_UAV(5, debugTarget)
            });
            nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(
                bindings, resolvePipeline.bindingLayout);
            nvrhi::ComputeState state;
            state.pipeline = resolvePipeline.pipeline;
            state.bindings = { bindingSet };
            commandList->beginMarker(useResurrection
                ? "RTAA Resolve + Conditional Resurrection" : "RTAA Resolve");
            BeginStage(commandList, Stage::Resolve);
            commandList->setComputeState(state);
            commandList->dispatch(dispatchX, dispatchY, 1);
            EndStage(commandList, Stage::Resolve);
            commandList->endMarker();
        }

        commandList->endMarker();

        writeSlot.view = CaptureView(currentView);
        writeSlot.frameIndex = frameIndex;
        writeSlot.valid = writeSlot.view != nullptr;
        m_LatestOutputIndex = m_NextWriteIndex;
        m_NextWriteIndex = (m_NextWriteIndex + 1u) %
            static_cast<uint32_t>(m_History.size());
        m_HistoryValid = writeSlot.valid;
        m_WasEnabled = true;
        m_Output = m_DebugOutput ? m_DebugOutput.Get() : writeSlot.color.Get();

        const bool sharpeningRunsThisFrame = settings.sharpeningEnabled &&
            settings.performanceProfile !=
                ReconstructiveTemporalPerformanceProfile::Performance &&
            (settings.debugMode == ReconstructiveTemporalDebugMode::FinalOutput ||
             settings.debugMode == ReconstructiveTemporalDebugMode::FinalNraRtaaOutput ||
             settings.debugMode == ReconstructiveTemporalDebugMode::SharpeningContribution);
        if (sharpeningRunsThisFrame)
        {
            m_AwaitingFusedSharpening = true;
        }
        else
        {
            m_Timings.fusedSharpeningMs = 0.f;
            EndStage(commandList, Stage::Total);
            ++m_TimerFrame;
        }
    }
}
