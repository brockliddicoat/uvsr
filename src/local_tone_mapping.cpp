#include "local_tone_mapping.h"

#include <donut/core/log.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "local_tone_mapping_cb.h"

static_assert(sizeof(float4) == 16, "Local tone-mapping CB rows must be 16 bytes");
static_assert(sizeof(uint4) == 16, "Local tone-mapping CB rows must be 16 bytes");
static_assert(alignof(LocalToneMappingConstants) == 16,
    "LocalToneMappingConstants must retain 16-byte alignment");
static_assert(sizeof(LocalToneMappingConstants) == 5 * 16,
    "LocalToneMappingConstants CPU/HLSL size mismatch");
static_assert(offsetof(LocalToneMappingConstants, inputDimensionsStrideCell) == 0 * 16);
static_assert(offsetof(LocalToneMappingConstants, gridDimensionsVoxelCount) == 1 * 16);
static_assert(offsetof(LocalToneMappingConstants, rangeAndFixedPoint) == 2 * 16);
static_assert(offsetof(LocalToneMappingConstants, filterAndReference) == 3 * 16);
static_assert(offsetof(LocalToneMappingConstants, blurAxisRadiusAndFlags) == 4 * 16);
static_assert(alignof(LocalToneMappingDisplayConstants) == 16,
    "LocalToneMappingDisplayConstants must retain 16-byte alignment");
static_assert(sizeof(LocalToneMappingDisplayConstants) == 3 * 16,
    "LocalToneMappingDisplayConstants CPU/HLSL size mismatch");
static_assert(offsetof(LocalToneMappingDisplayConstants,
    gridDimensionsAndCellSize) == 0 * 16);
static_assert(offsetof(LocalToneMappingDisplayConstants,
    rangeStrengthAndMaximumCorrection) == 1 * 16);
static_assert(offsetof(LocalToneMappingDisplayConstants,
    directionDebugAndActive) == 2 * 16);

namespace
{
    constexpr uint32_t kSplatThreadGroupSize = 8u;
    constexpr uint32_t kGridThreadGroupSize = 4u;
    constexpr uint32_t kMaximumSpatialKernelRadius = 24u;
    constexpr float kConfidenceNormalization = 1.f;

    template <typename T>
    T DivideRoundUp(T value, T divisor)
    {
        return (value + divisor - 1) / divisor;
    }

    float SanitizeFinite(float value, float fallback, float minimum, float maximum)
    {
        return std::isfinite(value)
            ? std::clamp(value, minimum, maximum)
            : fallback;
    }

    uint32_t ScaleAt1080p(uint32_t valueAt1080p, uint32_t viewportHeight)
    {
        const double scaled = static_cast<double>(valueAt1080p) *
            static_cast<double>(std::max(viewportHeight, 1u)) / 1080.0;
        return std::max(1u, static_cast<uint32_t>(std::llround(scaled)));
    }

    bool SameConfiguration(
        const uvsr::LocalToneMappingGridConfiguration& a,
        const uvsr::LocalToneMappingGridConfiguration& b)
    {
        return all(a.inputResolution == b.inputResolution) &&
            all(a.gridDimensions == b.gridDimensions) &&
            a.spatialCellSize == b.spatialCellSize &&
            a.samplingStride == b.samplingStride &&
            a.voxelCount == b.voxelCount;
    }

    bool HasFormatSupport(
        nvrhi::IDevice* device,
        nvrhi::Format format,
        nvrhi::FormatSupport required)
    {
        const nvrhi::FormatSupport available = device->queryFormatSupport(format);
        return (available & required) == required;
    }
}

namespace uvsr
{
    void SanitizeLocalToneMappingSettings(LocalToneMappingSettings& settings)
    {
        const LocalToneMappingSettings defaults;
        settings.strength = SanitizeFinite(
            settings.strength, defaults.strength, 0.f, 1.f);
        settings.radiusAt1080p = SanitizeFinite(
            settings.radiusAt1080p, defaults.radiusAt1080p, 16.f, 192.f);
        settings.edgePreservation = SanitizeFinite(
            settings.edgePreservation, defaults.edgePreservation, 0.f, 1.f);
        settings.shadowRecovery = SanitizeFinite(
            settings.shadowRecovery, defaults.shadowRecovery, 0.f, 1.f);
        settings.highlightCompression = SanitizeFinite(
            settings.highlightCompression, defaults.highlightCompression, 0.f, 1.f);
        settings.maximumCorrectionEV = SanitizeFinite(
            settings.maximumCorrectionEV, defaults.maximumCorrectionEV, 0.f, 2.5f);
        settings.referenceResponseMs = SanitizeFinite(
            settings.referenceResponseMs, defaults.referenceResponseMs, 0.f, 1000.f);

        switch (settings.quality)
        {
        case LocalToneMappingQuality::Performance:
        case LocalToneMappingQuality::Balanced:
        case LocalToneMappingQuality::MaximumQuality:
            break;
        default:
            settings.quality = defaults.quality;
            break;
        }

        switch (settings.debugMode)
        {
        case LocalToneMappingDebugMode::Off:
        case LocalToneMappingDebugMode::InputLogLuminance:
        case LocalToneMappingDebugMode::BaseLocalEV:
        case LocalToneMappingDebugMode::FinalLocalEV:
        case LocalToneMappingDebugMode::GridConfidence:
            break;
        default:
            settings.debugMode = defaults.debugMode;
            break;
        }
    }

    void ApplyLocalToneMappingQualityPreset(
        LocalToneMappingSettings& settings,
        LocalToneMappingQuality quality)
    {
        settings.quality = quality;
        SanitizeLocalToneMappingSettings(settings);
    }

    LocalToneMappingQualityParameters GetLocalToneMappingQualityParameters(
        LocalToneMappingQuality quality)
    {
        switch (quality)
        {
        case LocalToneMappingQuality::Performance:
            return { 32u, 8u, 32u };
        case LocalToneMappingQuality::MaximumQuality:
            return { 16u, 4u, 64u };
        case LocalToneMappingQuality::Balanced:
        default:
            return { 24u, 4u, 48u };
        }
    }

    LocalToneMappingGridConfiguration ComputeLocalToneMappingGridConfiguration(
        uint32_t width,
        uint32_t height,
        LocalToneMappingQuality quality)
    {
        LocalToneMappingGridConfiguration result;
        width = std::clamp(width, 1u, LocalToneMappingMaximum2DTextureDimension);
        height = std::clamp(height, 1u, LocalToneMappingMaximum2DTextureDimension);
        result.inputResolution = uint2(width, height);

        const LocalToneMappingQualityParameters preset =
            GetLocalToneMappingQualityParameters(quality);
        result.spatialCellSize = ScaleAt1080p(
            preset.spatialCellSizeAt1080p, height);
        result.samplingStride = ScaleAt1080p(
            preset.samplingStrideAt1080p, height);

        // Texture3D axes are limited to 2048 on D3D12. Normal viewports never
        // approach that limit after height scaling, but this clamp keeps very
        // wide diagnostic windows valid without changing the range dimension.
        result.spatialCellSize = std::max({
            result.spatialCellSize,
            DivideRoundUp(width, LocalToneMappingMaximum3DTextureDimension),
            DivideRoundUp(height, LocalToneMappingMaximum3DTextureDimension)
        });

        result.gridDimensions = uint3(
            DivideRoundUp(width, result.spatialCellSize),
            DivideRoundUp(height, result.spatialCellSize),
            preset.rangeBinCount);
        const uint64_t voxelCount =
            static_cast<uint64_t>(result.gridDimensions.x) *
            result.gridDimensions.y * result.gridDimensions.z;
        assert(voxelCount <= std::numeric_limits<uint32_t>::max());
        result.voxelCount = static_cast<uint32_t>(voxelCount);
        return result;
    }

    float ComputeLocalToneMappingLogLuminance(float red, float green, float blue)
    {
        if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue))
            return LocalToneMappingMinEV;

        red = std::max(red, 0.f);
        green = std::max(green, 0.f);
        blue = std::max(blue, 0.f);
        const float luminance = red * 0.2126f + green * 0.7152f + blue * 0.0722f;
        if (!std::isfinite(luminance))
            return LocalToneMappingMinEV;

        return std::clamp(
            std::log2(std::max(luminance, 1e-6f)),
            LocalToneMappingMinEV,
            LocalToneMappingMaxEV);
    }

    float ComputeLocalToneMappingRangeCoordinate(float logLuminance)
    {
        if (!std::isfinite(logLuminance))
            return 0.f;
        return std::clamp(
            (logLuminance - LocalToneMappingMinEV) /
                (LocalToneMappingMaxEV - LocalToneMappingMinEV),
            0.f, 1.f);
    }

    uint32_t EncodeLocalToneMappingLogLuminance(float logLuminance)
    {
        const float clamped = std::clamp(
            std::isfinite(logLuminance) ? logLuminance : LocalToneMappingMinEV,
            LocalToneMappingMinEV,
            LocalToneMappingMaxEV);
        return static_cast<uint32_t>(std::lround(
            (clamped - LocalToneMappingMinEV) *
            LocalToneMappingFixedPointScale));
    }

    float ComputeLocalToneMappingBaseDeltaEV(
        float globalReferenceLogY,
        float filteredLogY)
    {
        if (!std::isfinite(globalReferenceLogY) || !std::isfinite(filteredLogY))
            return 0.f;
        return std::clamp(
            globalReferenceLogY - filteredLogY,
            LocalToneMappingMinEV - LocalToneMappingMaxEV,
            LocalToneMappingMaxEV - LocalToneMappingMinEV);
    }

    float ComputeLocalToneMappingCorrectionEV(
        float baseDeltaEV,
        float confidence,
        const LocalToneMappingSettings& inputSettings,
        bool eligibleForFinalOutput)
    {
        LocalToneMappingSettings settings = inputSettings;
        SanitizeLocalToneMappingSettings(settings);
        if (!eligibleForFinalOutput || !settings.enabled ||
            settings.debugMode != LocalToneMappingDebugMode::Off ||
            !(settings.strength > 0.f) || !std::isfinite(baseDeltaEV))
        {
            return 0.f;
        }

        confidence = std::isfinite(confidence)
            ? std::clamp(confidence, 0.f, 1.f)
            : 0.f;
        const float directionalScale = baseDeltaEV >= 0.f
            ? settings.shadowRecovery
            : settings.highlightCompression;
        const float correction = baseDeltaEV * directionalScale *
            settings.strength * confidence;
        return std::clamp(correction,
            -settings.maximumCorrectionEV,
            settings.maximumCorrectionEV);
    }

    float ComputeMaximumPositiveLocalCorrectionEV(
        const LocalToneMappingSettings& inputSettings,
        bool eligibleForFinalOutput)
    {
        LocalToneMappingSettings settings = inputSettings;
        SanitizeLocalToneMappingSettings(settings);
        if (!eligibleForFinalOutput || !settings.enabled ||
            settings.debugMode != LocalToneMappingDebugMode::Off ||
            !(settings.strength > 0.f) || !(settings.shadowRecovery > 0.f))
        {
            return 0.f;
        }

        const float maximumRawPositiveDelta =
            LocalToneMappingMaxEV - LocalToneMappingMinEV;
        return std::min(settings.maximumCorrectionEV,
            maximumRawPositiveDelta * settings.shadowRecovery * settings.strength);
    }

    float ComputeLocalToneMappingReferenceCurrentWeight(
        float deltaSeconds,
        float responseMilliseconds)
    {
        if (std::isnan(deltaSeconds) || deltaSeconds <= 0.f)
            return 0.f;
        if (std::isinf(deltaSeconds))
            return 1.f;
        if (!std::isfinite(responseMilliseconds))
            responseMilliseconds = LocalToneMappingSettings{}.referenceResponseMs;
        if (responseMilliseconds <= 0.f)
            return 1.f;

        const double responseSeconds =
            std::max(static_cast<double>(responseMilliseconds) * 0.001, 1e-7);
        const double weight = -std::expm1(
            -static_cast<double>(deltaSeconds) / responseSeconds);
        return static_cast<float>(std::clamp(weight, 0.0, 1.0));
    }

    float ComputeLocalToneMappingRangeSigmaBins(float edgePreservation)
    {
        edgePreservation = SanitizeFinite(edgePreservation,
            LocalToneMappingSettings{}.edgePreservation, 0.f, 1.f);
        // This strictly monotonic map gives low preservation a broad six-bin
        // range blur and high preservation a narrow 0.65-bin blur.
        return 6.f + (0.65f - 6.f) * edgePreservation;
    }

    uint32_t ComputeLocalToneMappingMaximumCellSampleCount(
        const LocalToneMappingGridConfiguration& configuration)
    {
        const uint32_t stride = std::max(configuration.samplingStride, 1u);
        const uint32_t cell = std::max(configuration.spatialCellSize, 1u);
        // Any half-open interval of C pixels contains at most ceil(C/S)
        // members of a sample lattice with stride S. X and Y are independent.
        const uint32_t samplesPerAxis = DivideRoundUp(cell, stride);
        return samplesPerAxis * samplesPerAxis;
    }

    uint64_t ComputeLocalToneMappingMaximumFixedPointAccumulation(
        const LocalToneMappingGridConfiguration& configuration)
    {
        const uint64_t maximumEncodedSample = EncodeLocalToneMappingLogLuminance(
            LocalToneMappingMaxEV);
        return maximumEncodedSample *
            ComputeLocalToneMappingMaximumCellSampleCount(configuration);
    }

    bool IsLocalToneMappingFixedPointAccumulationSafe(
        const LocalToneMappingGridConfiguration& configuration)
    {
        return ComputeLocalToneMappingMaximumFixedPointAccumulation(configuration) <=
            std::numeric_limits<uint32_t>::max();
    }

    const char* GetLocalToneMappingQualityName(LocalToneMappingQuality quality)
    {
        switch (quality)
        {
        case LocalToneMappingQuality::Performance: return "Performance";
        case LocalToneMappingQuality::MaximumQuality: return "Maximum Quality";
        case LocalToneMappingQuality::Balanced:
        default: return "Balanced";
        }
    }

    const char* GetLocalToneMappingDebugModeName(LocalToneMappingDebugMode mode)
    {
        switch (mode)
        {
        case LocalToneMappingDebugMode::InputLogLuminance:
            return "Input Log Luminance";
        case LocalToneMappingDebugMode::BaseLocalEV:
            return "Base Local EV";
        case LocalToneMappingDebugMode::FinalLocalEV:
            return "Final Local EV";
        case LocalToneMappingDebugMode::GridConfidence:
            return "Grid Confidence";
        case LocalToneMappingDebugMode::Off:
        default:
            return "Off";
        }
    }

    LocalToneMappingPass::LocalToneMappingPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        std::shared_ptr<CommonRenderPasses> commonPasses)
        : m_Device(device)
        , m_CommonPasses(std::move(commonPasses))
        , m_BindingSets(device)
    {
        assert(device && shaderFactory && m_CommonPasses);

        const nvrhi::FormatSupport integerRequirements =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavStore |
            nvrhi::FormatSupport::ShaderAtomic;
        const nvrhi::FormatSupport floatRequirements =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderSample |
            nvrhi::FormatSupport::ShaderUavStore;
        m_FormatSupportValid =
            HasFormatSupport(device, nvrhi::Format::R32_UINT, integerRequirements) &&
            HasFormatSupport(device, nvrhi::Format::RG32_FLOAT, floatRequirements);
        if (!m_FormatSupportValid)
        {
            log::error("Local tone mapping requires R32_UINT atomic UAV and "
                "RG32_FLOAT sampled/UAV texture support; the effect will stay neutral.");
        }

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(LocalToneMappingConstants);
        constantBufferDesc.debugName = "Local Tone Mapping/Constants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions = engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantBufferDesc);

        CreatePipelines(shaderFactory);
        CreateNeutralGrid();

        for (auto& stageQueries : m_TimerQueries)
        {
            for (auto& query : stageQueries)
                query = device->createTimerQuery();
        }
    }

    void LocalToneMappingPass::CreatePipelines(
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

        createPipeline(m_SplatPipeline,
            "uvsr/local_tone_mapping_splat_cs.hlsl", {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(1)
            });
        createPipeline(m_ReferencePipeline,
            "uvsr/local_tone_mapping_reduce_cs.hlsl", {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_UAV(0)
            });

        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            std::vector<ShaderMacro> macros;
            macros.emplace_back("LOCAL_BLUR_SOURCE_UINT", axis == 0u ? "1" : "0");
            macros.emplace_back("LOCAL_BLUR_AXIS", std::to_string(axis));
            std::vector<nvrhi::BindingLayoutItem> bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0)
            };
            if (axis == 0u)
                bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(1));
            bindings.push_back(nvrhi::BindingLayoutItem::Texture_UAV(0));
            createPipeline(m_BlurPipelines[axis],
                "uvsr/local_tone_mapping_blur_cs.hlsl", bindings, &macros);
        }

        createPipeline(m_FinalizePipeline,
            "uvsr/local_tone_mapping_finalize_cs.hlsl", {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_UAV(0)
            });

        auto pipelineValid = [](const Pipeline& pipeline)
        {
            return pipeline.shader && pipeline.bindingLayout && pipeline.pipeline;
        };
        m_PipelinesValid = pipelineValid(m_SplatPipeline) &&
            pipelineValid(m_ReferencePipeline) &&
            pipelineValid(m_FinalizePipeline);
        for (const Pipeline& pipeline : m_BlurPipelines)
            m_PipelinesValid = m_PipelinesValid && pipelineValid(pipeline);
        if (!m_PipelinesValid)
            log::error("Local tone-mapping compute pipeline creation failed; the effect will stay neutral.");
    }

    void LocalToneMappingPass::CreateNeutralGrid()
    {
        if (!m_FormatSupportValid)
            return;

        nvrhi::TextureDesc desc;
        desc.width = 1u;
        desc.height = 1u;
        desc.depth = 1u;
        desc.dimension = nvrhi::TextureDimension::Texture3D;
        desc.format = nvrhi::Format::RG32_FLOAT;
        desc.mipLevels = 1u;
        desc.isUAV = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        desc.keepInitialState = true;
        desc.debugName = "Local Tone Mapping/Neutral Grid";
        m_NeutralGrid = m_Device->createTexture(desc);
        if (!m_NeutralGrid)
        {
            log::error("Could not create the local tone-mapping neutral grid; using Donut's black 3D texture.");
            return;
        }

        nvrhi::CommandListHandle commandList = m_Device->createCommandList();
        commandList->open();
        commandList->clearTextureFloat(
            m_NeutralGrid, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->close();
        m_Device->executeCommandList(commandList);
    }

    bool LocalToneMappingPass::EnsureResources(
        const LocalToneMappingGridConfiguration& configuration)
    {
        if (!m_FormatSupportValid || !m_PipelinesValid || !m_ConstantBuffer)
            return false;

        const bool resourcesComplete = m_AccumulatedLog && m_AccumulatedWeight &&
            m_BlurGrid[0] && m_BlurGrid[1] && m_FinalGrid &&
            m_ReferenceHistory[0] && m_ReferenceHistory[1];
        if (resourcesComplete && SameConfiguration(m_Configuration, configuration))
            return true;

        ReleaseResources();
        m_Configuration = configuration;

        auto createGridTexture = [this, &configuration](
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc desc;
            desc.width = configuration.gridDimensions.x;
            desc.height = configuration.gridDimensions.y;
            desc.depth = configuration.gridDimensions.z;
            desc.dimension = nvrhi::TextureDimension::Texture3D;
            desc.format = format;
            desc.mipLevels = 1u;
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            desc.debugName = debugName;
            return m_Device->createTexture(desc);
        };
        auto createReferenceTexture = [this](const char* debugName)
        {
            nvrhi::TextureDesc desc;
            desc.width = 1u;
            desc.height = 1u;
            desc.dimension = nvrhi::TextureDimension::Texture2D;
            desc.format = nvrhi::Format::RG32_FLOAT;
            desc.mipLevels = 1u;
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            desc.debugName = debugName;
            return m_Device->createTexture(desc);
        };

        m_AccumulatedLog = createGridTexture(
            nvrhi::Format::R32_UINT, "Local Tone Mapping/Accumulated Log");
        m_AccumulatedWeight = createGridTexture(
            nvrhi::Format::R32_UINT, "Local Tone Mapping/Accumulated Weight");
        m_BlurGrid[0] = createGridTexture(
            nvrhi::Format::RG32_FLOAT, "Local Tone Mapping/Blur Grid 0");
        m_BlurGrid[1] = createGridTexture(
            nvrhi::Format::RG32_FLOAT, "Local Tone Mapping/Blur Grid 1");
        m_FinalGrid = createGridTexture(
            nvrhi::Format::RG32_FLOAT, "Local Tone Mapping/Final Grid");
        m_ReferenceHistory[0] = createReferenceTexture(
            "Local Tone Mapping/Reference History 0");
        m_ReferenceHistory[1] = createReferenceTexture(
            "Local Tone Mapping/Reference History 1");

        if (!m_AccumulatedLog || !m_AccumulatedWeight ||
            !m_BlurGrid[0] || !m_BlurGrid[1] || !m_FinalGrid ||
            !m_ReferenceHistory[0] || !m_ReferenceHistory[1])
        {
            log::error("Could not allocate the %ux%ux%u local tone-mapping grid; "
                "using neutral global AgX output for this frame.",
                configuration.gridDimensions.x,
                configuration.gridDimensions.y,
                configuration.gridDimensions.z);
            ReleaseResources();
            return false;
        }

        const uint64_t voxels = configuration.voxelCount;
        m_MemoryStats.configuration = configuration;
        m_MemoryStats.accumulationBytes = voxels * 2ull * 4ull;
        m_MemoryStats.blurBytes = voxels * 2ull * 8ull;
        m_MemoryStats.finalGridBytes = voxels * 8ull;
        m_MemoryStats.referenceBytes = 2ull * 8ull;
        m_MemoryStats.totalBytes = m_MemoryStats.accumulationBytes +
            m_MemoryStats.blurBytes + m_MemoryStats.finalGridBytes +
            m_MemoryStats.referenceBytes;
        ResetHistory();
        m_BindingSets.Clear();
        return true;
    }

    void LocalToneMappingPass::ResetHistory()
    {
        m_ReferenceReadIndex = 0u;
        m_HistoryValid = false;
        m_OutputValid = false;
    }

    void LocalToneMappingPass::ReleaseResources()
    {
        m_BindingSets.Clear();
        m_AccumulatedLog = nullptr;
        m_AccumulatedWeight = nullptr;
        m_BlurGrid[0] = nullptr;
        m_BlurGrid[1] = nullptr;
        m_FinalGrid = nullptr;
        m_ReferenceHistory[0] = nullptr;
        m_ReferenceHistory[1] = nullptr;
        m_Configuration = {};
        m_MemoryStats = {};
        ResetHistory();
    }

    void LocalToneMappingPass::Deactivate()
    {
        if (m_WasActive || m_AccumulatedLog || m_FinalGrid)
            ReleaseResources();
        m_WasActive = false;
        m_Timings = {};
    }

    void LocalToneMappingPass::ReportFrameFailure(const char* reason)
    {
        m_OutputValid = false;
        m_HistoryValid = false;
        if (!m_LoggedFrameFailure)
        {
            log::error("Local tone mapping disabled for this frame: %s", reason);
            m_LoggedFrameFailure = true;
        }
    }

    void LocalToneMappingPass::AdvanceTimers()
    {
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        for (uint32_t stageIndex = 0u;
            stageIndex < static_cast<uint32_t>(Stage::Count); ++stageIndex)
        {
            if (!m_TimerPending[stageIndex][slot])
                continue;
            nvrhi::ITimerQuery* query = m_TimerQueries[stageIndex][slot];
            if (!query || !m_Device->pollTimerQuery(query))
                continue;

            const float milliseconds = m_Device->getTimerQueryTime(query) * 1000.f;
            m_Device->resetTimerQuery(query);
            m_TimerPending[stageIndex][slot] = false;
            switch (static_cast<Stage>(stageIndex))
            {
            case Stage::Splat: m_Timings.splatMs = milliseconds; break;
            case Stage::Reference: m_Timings.referenceMs = milliseconds; break;
            case Stage::Blur: m_Timings.blurMs = milliseconds; break;
            case Stage::Finalize: m_Timings.finalizeMs = milliseconds; break;
            case Stage::Total: m_Timings.totalMs = milliseconds; break;
            default: break;
            }
        }
    }

    void LocalToneMappingPass::BeginStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        if (!m_TimerQueries[stageIndex][slot] ||
            m_TimerPending[stageIndex][slot] || m_TimerActive[stageIndex])
        {
            return;
        }
        commandList->beginTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerActive[stageIndex] = true;
    }

    void LocalToneMappingPass::EndStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        if (!m_TimerActive[stageIndex])
            return;
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        commandList->endTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerPending[stageIndex][slot] = true;
        m_TimerActive[stageIndex] = false;
    }

    bool LocalToneMappingPass::Render(
        nvrhi::ICommandList* commandList,
        LocalToneMappingSettings& settings,
        nvrhi::ITexture* displaySource,
        float deltaSeconds)
    {
        SanitizeLocalToneMappingSettings(settings);
        m_OutputValid = false;
        if (!settings.enabled ||
            (!(settings.strength > 0.f) &&
                settings.debugMode == LocalToneMappingDebugMode::Off))
        {
            Deactivate();
            return false;
        }
        if (!commandList || !displaySource)
        {
            ReportFrameFailure("missing command list or display source");
            return false;
        }

        const nvrhi::TextureDesc& sourceDesc = displaySource->getDesc();
        if (sourceDesc.dimension != nvrhi::TextureDimension::Texture2D ||
            sourceDesc.width == 0u || sourceDesc.height == 0u ||
            sourceDesc.width > LocalToneMappingMaximum2DTextureDimension ||
            sourceDesc.height > LocalToneMappingMaximum2DTextureDimension)
        {
            ReportFrameFailure("unsupported display-source dimensions");
            return false;
        }

        const LocalToneMappingGridConfiguration configuration =
            ComputeLocalToneMappingGridConfiguration(
                sourceDesc.width, sourceDesc.height, settings.quality);
        if (!IsLocalToneMappingFixedPointAccumulationSafe(configuration))
        {
            ReportFrameFailure("fixed-point overflow proof failed");
            return false;
        }
        if (!EnsureResources(configuration))
        {
            ReportFrameFailure("required texture formats or resources are unavailable");
            return false;
        }
        AdvanceTimers();

        const bool resetReference = !m_HistoryValid || !m_WasActive;
        const uint32_t referenceWriteIndex = 1u - m_ReferenceReadIndex;
        const float scaledRadius = std::max(1.f, std::round(
            settings.radiusAt1080p * float(configuration.inputResolution.y) / 1080.f));
        const float spatialSigmaCells = std::max(
            scaledRadius / float(configuration.spatialCellSize), 0.25f);
        const float rangeSigmaBins =
            ComputeLocalToneMappingRangeSigmaBins(settings.edgePreservation);
        const uint32_t spatialKernelRadius = std::clamp(
            static_cast<uint32_t>(std::ceil(spatialSigmaCells * 2.f)),
            1u, kMaximumSpatialKernelRadius);
        const uint32_t rangeKernelRadius = std::clamp(
            static_cast<uint32_t>(std::ceil(rangeSigmaBins * 2.f)),
            1u, kMaximumSpatialKernelRadius);

        LocalToneMappingConstants constants{};
        constants.inputDimensionsStrideCell = uint4(
            configuration.inputResolution.x,
            configuration.inputResolution.y,
            configuration.samplingStride,
            configuration.spatialCellSize);
        constants.gridDimensionsVoxelCount = uint4(
            configuration.gridDimensions,
            configuration.voxelCount);
        constants.rangeAndFixedPoint = float4(
            LocalToneMappingMinEV,
            LocalToneMappingMaxEV,
            1.f / (LocalToneMappingMaxEV - LocalToneMappingMinEV),
            LocalToneMappingFixedPointScale);
        constants.filterAndReference = float4(
            spatialSigmaCells,
            rangeSigmaBins,
            ComputeLocalToneMappingReferenceCurrentWeight(
                deltaSeconds, settings.referenceResponseMs),
            kConfidenceNormalization);
        constants.blurAxisRadiusAndFlags = uint4(
            0u, spatialKernelRadius, resetReference ? 1u : 0u, 0u);

        nvrhi::BindingSetDesc splatBindings;
        splatBindings.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, displaySource),
            nvrhi::BindingSetItem::Texture_UAV(0, m_AccumulatedLog),
            nvrhi::BindingSetItem::Texture_UAV(1, m_AccumulatedWeight)
        };
        nvrhi::BindingSetDesc referenceBindings;
        referenceBindings.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, m_AccumulatedLog),
            nvrhi::BindingSetItem::Texture_SRV(1, m_AccumulatedWeight),
            nvrhi::BindingSetItem::Texture_SRV(
                2, m_ReferenceHistory[m_ReferenceReadIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                0, m_ReferenceHistory[referenceWriteIndex])
        };
        nvrhi::BindingSetDesc blurXBindings;
        blurXBindings.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, m_AccumulatedLog),
            nvrhi::BindingSetItem::Texture_SRV(1, m_AccumulatedWeight),
            nvrhi::BindingSetItem::Texture_UAV(0, m_BlurGrid[0])
        };
        nvrhi::BindingSetDesc blurYBindings;
        blurYBindings.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, m_BlurGrid[0]),
            nvrhi::BindingSetItem::Texture_UAV(0, m_BlurGrid[1])
        };
        nvrhi::BindingSetDesc blurRangeBindings;
        blurRangeBindings.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, m_BlurGrid[1]),
            nvrhi::BindingSetItem::Texture_UAV(0, m_BlurGrid[0])
        };
        nvrhi::BindingSetDesc finalizeBindings;
        finalizeBindings.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, m_BlurGrid[0]),
            nvrhi::BindingSetItem::Texture_SRV(
                1, m_ReferenceHistory[referenceWriteIndex]),
            nvrhi::BindingSetItem::Texture_UAV(0, m_FinalGrid)
        };

        const nvrhi::BindingSetHandle splatBindingSet =
            m_BindingSets.GetOrCreateBindingSet(
                splatBindings, m_SplatPipeline.bindingLayout);
        const nvrhi::BindingSetHandle referenceBindingSet =
            m_BindingSets.GetOrCreateBindingSet(
                referenceBindings, m_ReferencePipeline.bindingLayout);
        const nvrhi::BindingSetHandle blurXBindingSet =
            m_BindingSets.GetOrCreateBindingSet(
                blurXBindings, m_BlurPipelines[0].bindingLayout);
        const nvrhi::BindingSetHandle blurYBindingSet =
            m_BindingSets.GetOrCreateBindingSet(
                blurYBindings, m_BlurPipelines[1].bindingLayout);
        const nvrhi::BindingSetHandle blurRangeBindingSet =
            m_BindingSets.GetOrCreateBindingSet(
                blurRangeBindings, m_BlurPipelines[2].bindingLayout);
        const nvrhi::BindingSetHandle finalizeBindingSet =
            m_BindingSets.GetOrCreateBindingSet(
                finalizeBindings, m_FinalizePipeline.bindingLayout);
        if (!splatBindingSet || !referenceBindingSet || !blurXBindingSet ||
            !blurYBindingSet || !blurRangeBindingSet || !finalizeBindingSet)
        {
            ReportFrameFailure("binding-set creation failed");
            return false;
        }

        const uint32_t sampleCountX = DivideRoundUp(
            configuration.inputResolution.x, configuration.samplingStride);
        const uint32_t sampleCountY = DivideRoundUp(
            configuration.inputResolution.y, configuration.samplingStride);
        const uint32_t gridDispatchX = DivideRoundUp(
            configuration.gridDimensions.x, kGridThreadGroupSize);
        const uint32_t gridDispatchY = DivideRoundUp(
            configuration.gridDimensions.y, kGridThreadGroupSize);
        const uint32_t gridDispatchZ = DivideRoundUp(
            configuration.gridDimensions.z, kGridThreadGroupSize);

        auto dispatch = [commandList](
            const Pipeline& pipeline,
            nvrhi::IBindingSet* bindingSet,
            uint32_t x, uint32_t y, uint32_t z)
        {
            nvrhi::ComputeState state;
            state.pipeline = pipeline.pipeline;
            state.bindings = { bindingSet };
            commandList->setComputeState(state);
            commandList->dispatch(x, y, z);
        };

        commandList->beginMarker("Local Tone Mapping");
        BeginStage(commandList, Stage::Total);

        commandList->beginMarker("Local Tone Mapping/Splat");
        BeginStage(commandList, Stage::Splat);
        commandList->clearTextureUInt(
            m_AccumulatedLog, nvrhi::AllSubresources, 0u);
        commandList->clearTextureUInt(
            m_AccumulatedWeight, nvrhi::AllSubresources, 0u);
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
        dispatch(m_SplatPipeline, splatBindingSet,
            DivideRoundUp(sampleCountX, kSplatThreadGroupSize),
            DivideRoundUp(sampleCountY, kSplatThreadGroupSize), 1u);
        EndStage(commandList, Stage::Splat);
        commandList->endMarker();

        commandList->beginMarker("Local Tone Mapping/Reference");
        BeginStage(commandList, Stage::Reference);
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
        dispatch(m_ReferencePipeline, referenceBindingSet, 1u, 1u, 1u);
        EndStage(commandList, Stage::Reference);
        commandList->endMarker();

        BeginStage(commandList, Stage::Blur);
        commandList->beginMarker("Local Tone Mapping/Blur X");
        constants.blurAxisRadiusAndFlags = uint4(
            0u, spatialKernelRadius, resetReference ? 1u : 0u, 0u);
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
        dispatch(m_BlurPipelines[0], blurXBindingSet,
            gridDispatchX, gridDispatchY, gridDispatchZ);
        commandList->endMarker();

        commandList->beginMarker("Local Tone Mapping/Blur Y");
        constants.blurAxisRadiusAndFlags = uint4(
            1u, spatialKernelRadius, resetReference ? 1u : 0u, 0u);
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
        dispatch(m_BlurPipelines[1], blurYBindingSet,
            gridDispatchX, gridDispatchY, gridDispatchZ);
        commandList->endMarker();

        commandList->beginMarker("Local Tone Mapping/Blur Range");
        constants.blurAxisRadiusAndFlags = uint4(
            2u, rangeKernelRadius, resetReference ? 1u : 0u, 0u);
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
        dispatch(m_BlurPipelines[2], blurRangeBindingSet,
            gridDispatchX, gridDispatchY, gridDispatchZ);
        commandList->endMarker();
        EndStage(commandList, Stage::Blur);

        commandList->beginMarker("Local Tone Mapping/Finalize");
        BeginStage(commandList, Stage::Finalize);
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
        dispatch(m_FinalizePipeline, finalizeBindingSet,
            gridDispatchX, gridDispatchY, gridDispatchZ);
        EndStage(commandList, Stage::Finalize);
        commandList->endMarker();

        EndStage(commandList, Stage::Total);
        commandList->endMarker();

        m_ReferenceReadIndex = referenceWriteIndex;
        m_HistoryValid = true;
        m_WasActive = true;
        m_OutputValid = true;
        m_LoggedFrameFailure = false;
        ++m_TimerFrame;
        return true;
    }

    LocalToneMappingDisplayState LocalToneMappingPass::GetDisplayState() const
    {
        LocalToneMappingDisplayState state;
        if (m_OutputValid && m_FinalGrid)
        {
            state.grid = m_FinalGrid;
            state.gridDimensions = m_Configuration.gridDimensions;
            state.spatialCellSize = m_Configuration.spatialCellSize;
            state.active = true;
            return state;
        }

        state.grid = m_NeutralGrid
            ? m_NeutralGrid.Get()
            : (m_CommonPasses ? m_CommonPasses->m_BlackTexture3D.Get() : nullptr);
        return state;
    }
}
