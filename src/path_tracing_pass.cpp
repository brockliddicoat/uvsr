#include "path_tracing_pass.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include <donut/shaders/bindless.h>
#include "path_tracing_cb.h"

static_assert(sizeof(PathTracingConstants) % 16u == 0u,
    "Path-tracing constants must preserve HLSL register alignment.");
static_assert(sizeof(GeometryData) == 64u);
static_assert(sizeof(MaterialConstants) == 208u);
static_assert(std::is_trivially_copyable_v<LightConstants>);
static_assert(offsetof(PathTracingConstants, previousView) ==
    sizeof(PlanarViewConstants));
static_assert(offsetof(PathTracingConstants, flashlight) ==
    2u * sizeof(PlanarViewConstants));
static_assert(static_cast<uint32_t>(uvsr::PathTracingSolver::RtxPt) == 0u);
static_assert(static_cast<uint32_t>(uvsr::PathTracingSolver::RestirPt) == 1u);
static_assert(static_cast<uint32_t>(uvsr::PathTracingSolver::RestirGi) == 2u);
static_assert(static_cast<uint32_t>(
    uvsr::PathTracingNeeMode::Uniform) == 0u);
static_assert(static_cast<uint32_t>(
    uvsr::PathTracingNeeMode::Power) == 1u);
static_assert(static_cast<uint32_t>(
    uvsr::PathTracingNeeMode::NeeAdaptiveTree) == 2u);
static_assert(static_cast<uint32_t>(uvsr::NoisePattern::SpatialWhite) == 0u);
static_assert(static_cast<uint32_t>(uvsr::NoisePattern::SpatialBlue) == 1u);
static_assert(static_cast<uint32_t>(
    uvsr::NoisePattern::SpatiotemporalBlue) == 2u);
static_assert(static_cast<uint32_t>(
    uvsr::SampleAccumulationAveraging::Cumulative) == 0u);
static_assert(static_cast<uint32_t>(
    uvsr::SampleAccumulationAveraging::Exponential) == 1u);
static_assert(static_cast<uint32_t>(
    uvsr::SampleAccumulationScheduling::EveryPixel) == 0u);
static_assert(static_cast<uint32_t>(
    uvsr::SampleAccumulationScheduling::VarianceGuided) == 1u);
static_assert(static_cast<uint32_t>(
    uvsr::PathTracingDebugView::PrimaryTransport) == 9u);
static_assert(static_cast<uint32_t>(
    uvsr::PathTracingDebugView::IndirectTransport) == 10u);

namespace uvsr
{
    namespace
    {
        // This is a coarse safety estimate, not a ray or instruction count.
        // This synthetic budget is a safety bound, not an ordinary-preset
        // quality throttle. One Gi work units keeps the default 1080p Sponza
        // RESTIR presets and default 4K low-light presets at one full-frame
        // phase while retaining a bounded lattice for pathological settings.
        constexpr uint64_t MaxPathTracingWorkUnitsPerDispatch =
            1024ull * 1024ull * 1024ull;

        struct PathTracingDispatchSchedule
        {
            uint2 workExtent = uint2(1u, 1u);
            uint2 grid = uint2(1u, 1u);
            uint2 phase = uint2(0u, 0u);
            uint64_t estimatedWorkUnitsPerPixel = 0u;
            uint32_t phaseCount = 1u;
            bool valid = false;
        };

        uint64_t CeilDivide(uint64_t value, uint64_t divisor)
        {
            return (value + divisor - 1u) / divisor;
        }

        constexpr uint64_t SaturatingAdd(uint64_t left, uint64_t right)
        {
            return left > std::numeric_limits<uint64_t>::max() - right
                ? std::numeric_limits<uint64_t>::max()
                : left + right;
        }

        constexpr uint64_t SaturatingMultiply(
            uint64_t left,
            uint64_t right)
        {
            return left != 0u &&
                    right > std::numeric_limits<uint64_t>::max() / left
                ? std::numeric_limits<uint64_t>::max()
                : left * right;
        }

        constexpr uint64_t EstimatePathTracingWorkUnitsPerPixel(
            const PathTracingSettings& settings,
            uint32_t lightCount)
        {
            const uint64_t candidateCount = lightCount > 0u
                ? std::max(settings.neeCandidateCount, 1u)
                : 0u;
            const uint64_t selectionWork =
                settings.neeMode == PathTracingNeeMode::Uniform
                    ? 0u
                    : SaturatingMultiply(uint64_t(lightCount), 2u);
            const uint64_t baseWorkPerPixel = std::max<uint64_t>(
                1u,
                SaturatingMultiply(
                    uint64_t(settings.maxBounces),
                    SaturatingAdd(
                        1u,
                        SaturatingMultiply(
                            candidateCount,
                            SaturatingAdd(1u, selectionWork)))));
            // Seed replay evaluates the current continuation plus at most one
            // temporal and one independently selected neighbor continuation.
            const uint64_t solverWorkMultiplier =
                settings.solver == PathTracingSolver::RestirPt &&
                    settings.reusePathReservoirs
                ? 3u
                : 1u;
            return SaturatingMultiply(
                baseWorkPerPixel,
                solverWorkMultiplier);
        }

        static_assert(EstimatePathTracingWorkUnitsPerPixel(
            PathTracingSettings{}, 0u) == 8u);
        static_assert(EstimatePathTracingWorkUnitsPerPixel(
            PathTracingSettings{}, 1u) == 16u);
        static_assert(EstimatePathTracingWorkUnitsPerPixel(
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirPt),
            0u) == 9u);
        static_assert(EstimatePathTracingWorkUnitsPerPixel(
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirPt),
            1u) == 36u);
        static_assert(EstimatePathTracingWorkUnitsPerPixel(
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirGi),
            1u) == 32u);
        static_assert(EstimatePathTracingWorkUnitsPerPixel(
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirPt),
            24u) == 450u);
        static_assert(EstimatePathTracingWorkUnitsPerPixel(
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirGi),
            24u) == 400u);
        static_assert(3840ull * 2160ull * 16ull <=
            MaxPathTracingWorkUnitsPerDispatch);
        static_assert(3840ull * 2160ull * 32ull <=
            MaxPathTracingWorkUnitsPerDispatch);
        static_assert(3840ull * 2160ull * 36ull <=
            MaxPathTracingWorkUnitsPerDispatch);
        static_assert(1920ull * 1080ull * 450ull <=
            MaxPathTracingWorkUnitsPerDispatch);

        bool TryGetStructuredBufferCount(
            nvrhi::IBuffer* buffer,
            uint32_t expectedStride,
            uint32_t& count)
        {
            count = 0u;
            if (!buffer || expectedStride == 0u)
                return false;
            const nvrhi::BufferDesc& description = buffer->getDesc();
            if (description.structStride != expectedStride ||
                description.byteSize == 0u ||
                description.byteSize % expectedStride != 0u)
            {
                return false;
            }
            const uint64_t count64 =
                description.byteSize / expectedStride;
            if (count64 == 0u ||
                count64 > std::numeric_limits<uint32_t>::max())
            {
                return false;
            }
            count = uint32_t(count64);
            return true;
        }

        PathTracingDispatchSchedule BuildPathTracingDispatchSchedule(
            uint32_t width,
            uint32_t height,
            const PathTracingSettings& settings,
            uint32_t lightCount,
            uint32_t progressivePhase)
        {
            const uint64_t workPerPixel =
                EstimatePathTracingWorkUnitsPerPixel(
                    settings,
                    lightCount);
            if (workPerPixel > MaxPathTracingWorkUnitsPerDispatch)
                return {};
            const uint64_t maximumPixels =
                MaxPathTracingWorkUnitsPerDispatch / workPerPixel;

            uint32_t gridX = 1u;
            uint32_t gridY = 1u;
            while (CeilDivide(width, gridX) *
                    CeilDivide(height, gridY) > maximumPixels)
            {
                const uint64_t workWidth = CeilDivide(width, gridX);
                const uint64_t workHeight = CeilDivide(height, gridY);
                if (workWidth >= workHeight && gridX < width)
                    gridX = gridX > width / 2u ? width : gridX * 2u;
                else if (gridY < height)
                    gridY = gridY > height / 2u ? height : gridY * 2u;
                else if (gridX < width)
                    gridX = gridX > width / 2u ? width : gridX * 2u;
                else
                    break;
            }

            PathTracingDispatchSchedule schedule;
            schedule.workExtent = uint2(
                uint32_t(CeilDivide(width, gridX)),
                uint32_t(CeilDivide(height, gridY)));
            schedule.grid = uint2(gridX, gridY);
            const uint64_t phaseCount64 = uint64_t(gridX) * gridY;
            if (phaseCount64 > std::numeric_limits<uint32_t>::max())
                return {};
            schedule.estimatedWorkUnitsPerPixel = workPerPixel;
            schedule.phaseCount = uint32_t(phaseCount64);
            const uint32_t phase = uint32_t(
                uint64_t(progressivePhase) % phaseCount64);
            schedule.phase = uint2(phase % gridX, phase / gridX);
            schedule.valid = true;
            return schedule;
        }

        bool HasFormatSupport(
            nvrhi::IDevice* device,
            nvrhi::Format format,
            nvrhi::FormatSupport required)
        {
            return device &&
                (device->queryFormatSupport(format) & required) == required;
        }

        nvrhi::TextureHandle CreatePathTexture(
            nvrhi::IDevice* device,
            uint32_t width,
            uint32_t height,
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc description;
            description.width = width;
            description.height = height;
            description.format = format;
            description.dimension = nvrhi::TextureDimension::Texture2D;
            description.isUAV = true;
            description.debugName = debugName;
            description.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            return device->createTexture(description);
        }

        uint64_t HashBytes(
            uint64_t hash,
            const void* data,
            size_t byteCount)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t index = 0u; index < byteCount; ++index)
            {
                hash ^= uint64_t(bytes[index]);
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

        uint64_t BuildTransportSignature(
            const PathTracingInputs& inputs,
            const PathTracingConstants& constants,
            const std::vector<LightConstants>& lights,
            bool stableSignalsRequired)
        {
            uint64_t hash = 1469598103934665603ull;
            hash = HashValue(
                hash,
                constants.view.matClipToWorldNoOffset);
            hash = HashValue(
                hash,
                constants.view.cameraDirectionOrPosition);
            hash = HashValue(hash, constants.view.viewportOrigin);
            hash = HashValue(hash, constants.view.viewportSize);
            const uint64_t lightCount = uint64_t(lights.size());
            hash = HashValue(hash, lightCount);
            if (!lights.empty())
            {
                hash = HashBytes(
                    hash,
                    lights.data(),
                    lights.size() * sizeof(LightConstants));
            }
            hash = HashValue(hash, constants.flashlight);
            hash = HashValue(hash, constants.environmentScale);
            hash = HashValue(hash, inputs.showEnvironmentBackground);
            hash = HashValue(hash, constants.rayBias);
            hash = HashValue(hash, constants.maximumRayDistance);
            hash = HashValue(hash, constants.dispatchExtent);

            const uintptr_t resourcePointers[] = {
                reinterpret_cast<uintptr_t>(inputs.worldTlas),
                reinterpret_cast<uintptr_t>(inputs.environment),
                reinterpret_cast<uintptr_t>(
                    inputs.materialVisibility.geometryBuffer),
                reinterpret_cast<uintptr_t>(
                    inputs.materialVisibility.materialBuffer),
                reinterpret_cast<uintptr_t>(
                    inputs.materialVisibility.geometryIndexMap)
            };
            hash = HashBytes(
                hash,
                resourcePointers,
                sizeof(resourcePointers));

            const PathTracingSettings& settings = inputs.settings;
            hash = HashValue(hash, settings.solver);
            hash = HashValue(hash, settings.neeMode);
            hash = HashValue(hash, settings.maxBounces);
            hash = HashValue(hash, settings.russianRouletteStart);
            hash = HashValue(hash, settings.neeCandidateCount);
            hash = HashValue(hash, settings.useRtxdi);
            hash = HashValue(hash, settings.reuseDirectReservoirs);
            hash = HashValue(hash, settings.reusePathReservoirs);
            hash = HashValue(hash, settings.reuseIndirectGiReservoirs);
            hash = HashValue(
                hash,
                settings.reuseRevalidatedProposalsDuringMotion);
            // Only executable signal topology participates. Authored-but-
            // unavailable reconstruction choices cannot contaminate raw
            // transport history.
            hash = HashValue(hash, stableSignalsRequired);
            hash = HashValue(hash, settings.enableFireflyFilter);
            hash = HashValue(hash, settings.fireflyThreshold);
            hash = HashValue(hash, inputs.noiseSettings.pattern);
            hash = HashValue(hash, inputs.noiseSettings.resolution);
            hash = HashValue(hash, inputs.accumulateSamples);
            hash = HashValue(hash, inputs.accumulationSettings);
            return hash;
        }

        uint32_t LowWord(uint64_t value)
        {
            return uint32_t(value & 0xffffffffull);
        }

        uint32_t HighWord(uint64_t value)
        {
            return uint32_t(value >> 32u);
        }
    }

    PathTracingCapabilities PathTracingPass::QueryCapabilities(
        nvrhi::IDevice* device)
    {
        const nvrhi::FormatSupport meanAndReservoirSupport =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavLoad |
            nvrhi::FormatSupport::ShaderUavStore;
        const nvrhi::FormatSupport displaySupport =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavLoad |
            nvrhi::FormatSupport::ShaderUavStore;
        const nvrhi::FormatSupport countSupport =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavLoad |
            nvrhi::FormatSupport::ShaderUavStore;
        const nvrhi::FormatSupport seedSupport =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavStore;
        const bool pathSeedFormatSupported = HasFormatSupport(
            device,
            nvrhi::Format::RG32_UINT,
            seedSupport);
        const bool stableSignalMeanFormatSupported = HasFormatSupport(
            device,
            nvrhi::Format::RGBA16_FLOAT,
            meanAndReservoirSupport);
        const bool stableGuideFormatSupported = HasFormatSupport(
                device,
                nvrhi::Format::RGBA16_FLOAT,
                displaySupport) &&
            HasFormatSupport(
                device,
                nvrhi::Format::R32_FLOAT,
                displaySupport);
        PathTracingCapabilities capabilities;
        capabilities.rayQuerySupported = device &&
            device->queryFeatureSupport(
                nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayQuery) &&
            HasFormatSupport(
                device,
                nvrhi::Format::RGBA32_FLOAT,
                meanAndReservoirSupport) &&
            HasFormatSupport(
                device,
                nvrhi::Format::RGBA16_FLOAT,
                displaySupport) &&
            HasFormatSupport(
                device,
                nvrhi::Format::R32_UINT,
                countSupport);
        // This pass intentionally targets SM 6.5 / DXR 1.1. Native shader
        // execution reordering requires a separately compiled newer path.
        capabilities.serSupported = false;
        capabilities.directReservoirSupported =
            capabilities.rayQuerySupported;
        capabilities.temporalReservoirReuseSupported =
            capabilities.rayQuerySupported;
        capabilities.previousFrameSpatialReuseSupported =
            capabilities.rayQuerySupported;
        capabilities.continuationSeedReservoirSupported =
            capabilities.rayQuerySupported && pathSeedFormatSupported;
        capabilities.replayablePathSeedSupported =
            capabilities.continuationSeedReservoirSupported;
        capabilities.temporalGiCheckpointReuseSupported =
            capabilities.rayQuerySupported;
        capabilities.spatialGiCheckpointReuseSupported = false;
        // These clean-room solver subsets use complete seed replay and
        // same-pixel local checkpoints. Neither performs geometric
        // reconnection or a cross-pixel GI transformation.
        capabilities.fullSampleReconnectionSupported = false;
        capabilities.stablePlaneSignalSupported =
            capabilities.rayQuerySupported &&
            stableSignalMeanFormatSupported && stableGuideFormatSupported;
        capabilities.stablePlaneResolveSupported = false;
        return capabilities;
    }

    PathTracingPass::PathTracingPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        nvrhi::IBindingLayout* bindlessLayout)
        : m_Device(device)
        , m_BindlessLayout(bindlessLayout)
        , m_Capabilities(QueryCapabilities(device))
    {
        if (!shaderFactory || !m_BindlessLayout ||
            !m_Capabilities.rayQuerySupported)
        {
            log::warning(
                "Path tracing requires DXR 1.1 ray queries and the typed UAV operations used by RGBA32F, RGBA16F and R32_UINT transport surfaces");
            m_Capabilities.rayQuerySupported = false;
            return;
        }

        nvrhi::BufferDesc constantBufferDescription;
        constantBufferDescription.byteSize = sizeof(PathTracingConstants);
        constantBufferDescription.debugName = "PathTracingConstants";
        constantBufferDescription.isConstantBuffer = true;
        constantBufferDescription.initialState =
            nvrhi::ResourceStates::ConstantBuffer;
        constantBufferDescription.keepInitialState = true;
        m_ConstantBuffer = device->createBuffer(
            constantBufferDescription);
        m_Sampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

        nvrhi::BindingLayoutDesc layoutDescription;
        layoutDescription.visibility = nvrhi::ShaderType::Compute;
        layoutDescription.bindings = {
            nvrhi::BindingLayoutItem::ConstantBuffer(0),
            nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_SRV(5),
            nvrhi::BindingLayoutItem::Texture_SRV(6),
            nvrhi::BindingLayoutItem::Texture_SRV(7),
            nvrhi::BindingLayoutItem::Texture_SRV(8),
            nvrhi::BindingLayoutItem::Texture_SRV(9),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(13),
            nvrhi::BindingLayoutItem::Sampler(0)
        };
        for (uint32_t slot = 0u; slot <= 14u; ++slot)
        {
            layoutDescription.bindings.push_back(
                nvrhi::BindingLayoutItem::Texture_UAV(slot));
        }
        m_BindingLayout = device->createBindingLayout(layoutDescription);
        for (uint32_t variant = 0u; variant < m_Shaders.size(); ++variant)
        {
            static constexpr const char* NeeModes[] = { "0", "1", "2" };
            static constexpr const char* Solvers[] = { "0", "1", "2" };
            const uint32_t solverVariant =
                variant / PathTracingPipelineVariantsPerSolver;
            const uint32_t solverLocalVariant =
                variant % PathTracingPipelineVariantsPerSolver;
            const uint32_t rtxdiVariant =
                solverLocalVariant / PathTracingNeeModeCount;
            const uint32_t neeVariant =
                solverLocalVariant % PathTracingNeeModeCount;
            const std::vector<ShaderMacro> defines = {
                ShaderMacro("UVSR_PT_SOLVER", Solvers[solverVariant]),
                ShaderMacro("UVSR_PT_RTXDI", rtxdiVariant == 0u ? "0" : "1"),
                ShaderMacro("UVSR_PT_NEE_MODE", NeeModes[neeVariant])
            };
            m_Shaders[variant] = shaderFactory->CreateShader(
                "uvsr/path_tracing_cs.hlsl",
                "main",
                &defines,
                nvrhi::ShaderType::Compute);
            if (!m_Shaders[variant] || !m_BindingLayout)
                continue;

            nvrhi::ComputePipelineDesc pipelineDescription;
            pipelineDescription.CS = m_Shaders[variant];
            pipelineDescription.bindingLayouts = {
                m_BindingLayout,
                m_BindlessLayout
            };
            m_Pipelines[variant] = device->createComputePipeline(
                pipelineDescription);
        }

        std::array<bool, PathTracingPipelineVariantCount>
            variantsAvailable = {};
        for (uint32_t variant = 0u; variant < m_Pipelines.size(); ++variant)
        {
            variantsAvailable[variant] =
                bool(m_Shaders[variant]) && bool(m_Pipelines[variant]);
        }
        for (uint32_t variant = 0u;
            variant < variantsAvailable.size();
            ++variant)
        {
            const uint32_t solverVariant =
                variant / PathTracingPipelineVariantsPerSolver;
            const bool requiredFormatsAvailable =
                solverVariant !=
                    static_cast<uint32_t>(PathTracingSolver::RestirPt) ||
                m_Capabilities.continuationSeedReservoirSupported;
            if (variantsAvailable[variant] && requiredFormatsAvailable)
                m_Capabilities.pipelineAvailabilityMask |= 1u << variant;
        }
        uint32_t rtxdiPipelineVariantsMask = 0u;
        for (uint32_t solver = 0u; solver < PathTracingSolverCount; ++solver)
        {
            for (uint32_t nee = 0u; nee < PathTracingNeeModeCount; ++nee)
            {
                rtxdiPipelineVariantsMask |= 1u <<
                    (solver * PathTracingPipelineVariantsPerSolver +
                        PathTracingNeeModeCount + nee);
            }
        }
        constexpr uint32_t RestirPtPipelineVariantsMask =
            ((1u << PathTracingPipelineVariantsPerSolver) - 1u) <<
                PathTracingPipelineVariantsPerSolver;
        constexpr uint32_t RestirGiPipelineVariantsMask =
            ((1u << PathTracingPipelineVariantsPerSolver) - 1u) <<
                (2u * PathTracingPipelineVariantsPerSolver);
        const bool baselineReady = m_ConstantBuffer && m_Sampler &&
            m_BindingLayout &&
            (m_Capabilities.pipelineAvailabilityMask & 1u) != 0u;
        if (!baselineReady)
        {
            log::error(
                "The baseline shared path-tracing pipeline could not be created (pipeline mask 0x%05x)",
                m_Capabilities.pipelineAvailabilityMask);
            m_Capabilities.pipelineAvailabilityMask = 0u;
        }
        else if (m_Capabilities.pipelineAvailabilityMask !=
            PathTracingAllPipelineVariantsMask)
        {
            log::warning(
                "Some optional path-tracing pipeline variants are unavailable (pipeline mask 0x%05x)",
                m_Capabilities.pipelineAvailabilityMask);
        }
        const bool rtxdiPipelinesReady =
            (m_Capabilities.pipelineAvailabilityMask &
                rtxdiPipelineVariantsMask) != 0u;
        m_Capabilities.directReservoirSupported &= rtxdiPipelinesReady;
        m_Capabilities.temporalReservoirReuseSupported &=
            rtxdiPipelinesReady;
        m_Capabilities.previousFrameSpatialReuseSupported &=
            rtxdiPipelinesReady;
        const bool restirPtPipelinesReady =
            (m_Capabilities.pipelineAvailabilityMask &
                RestirPtPipelineVariantsMask) != 0u;
        m_Capabilities.continuationSeedReservoirSupported &=
            restirPtPipelinesReady;
        m_Capabilities.replayablePathSeedSupported &=
            restirPtPipelinesReady;
        const bool restirGiPipelinesReady =
            (m_Capabilities.pipelineAvailabilityMask &
                RestirGiPipelineVariantsMask) != 0u;
        m_Capabilities.temporalGiCheckpointReuseSupported &=
            restirGiPipelinesReady;
    }

    bool PathTracingPass::EnsureResources(
        uint32_t width,
        uint32_t height,
        bool directReuseRequired,
        bool giReuseRequired,
        bool pathReuseRequired,
        bool stableSignalsRequired)
    {
        if (width == 0u || height == 0u)
            return false;
        if (m_RawMean && m_SuccessfulSampleCount &&
            m_ColorVariance && m_Display &&
            m_Width == width && m_Height == height &&
            m_DirectReuseResourcesFullResolution == directReuseRequired &&
            m_GiReuseResourcesFullResolution == giReuseRequired &&
            m_PathReuseResourcesFullResolution == pathReuseRequired &&
            m_StableSignalResourcesFullResolution ==
                stableSignalsRequired)
        {
            return true;
        }

        const bool extentChanged = !m_RawMean ||
            m_Width != width || m_Height != height;
        const bool directTopologyChanged = extentChanged ||
            m_DirectReuseResourcesFullResolution != directReuseRequired;
        const bool giTopologyChanged = extentChanged ||
            m_GiReuseResourcesFullResolution != giReuseRequired;
        const bool pathTopologyChanged = extentChanged ||
            m_PathReuseResourcesFullResolution != pathReuseRequired;
        const bool stableSignalTopologyChanged = extentChanged ||
            m_StableSignalResourcesFullResolution !=
                stableSignalsRequired;

        // Preserve the expensive base transport surfaces when only a solver
        // history family changes. This keeps a GI/PT/RTXDI mode switch from
        // allocating a complete second copy of the output set while the old
        // set is still referenced by an in-flight frame.
        nvrhi::TextureHandle rawMean = m_RawMean;
        nvrhi::TextureHandle successfulSampleCount =
            m_SuccessfulSampleCount;
        nvrhi::TextureHandle colorVariance = m_ColorVariance;
        nvrhi::TextureHandle display = m_Display;
        nvrhi::TextureHandle residualMean = m_ResidualMean;
        nvrhi::TextureHandle diffuseSuffixMean = m_DiffuseSuffixMean;
        nvrhi::TextureHandle primaryNormalRoughness =
            m_PrimaryNormalRoughness;
        nvrhi::TextureHandle primaryViewZ = m_PrimaryViewZ;
        if (extentChanged)
        {
            rawMean = CreatePathTexture(
                m_Device, width, height, nvrhi::Format::RGBA32_FLOAT,
                "Path Tracing/Raw Mean");
            successfulSampleCount = CreatePathTexture(
                m_Device, width, height, nvrhi::Format::R32_UINT,
                "Path Tracing/Successful Sample Count");
            colorVariance = CreatePathTexture(
                m_Device, width, height, nvrhi::Format::RGBA32_FLOAT,
                "Path Tracing/Color Variance");
            display = CreatePathTexture(
                m_Device, width, height, nvrhi::Format::RGBA16_FLOAT,
                "Path Tracing/Display");
        }
        if (stableSignalTopologyChanged)
        {
            const uint32_t signalWidth = stableSignalsRequired
                ? width
                : 1u;
            const uint32_t signalHeight = stableSignalsRequired
                ? height
                : 1u;
            residualMean = CreatePathTexture(
                m_Device,
                signalWidth,
                signalHeight,
                nvrhi::Format::RGBA16_FLOAT,
                "Path Tracing/Primary Local Mean");
            diffuseSuffixMean = CreatePathTexture(
                m_Device,
                signalWidth,
                signalHeight,
                nvrhi::Format::RGBA16_FLOAT,
                "Path Tracing/Diffuse Suffix Mean");
            primaryNormalRoughness = CreatePathTexture(
                m_Device,
                signalWidth,
                signalHeight,
                nvrhi::Format::RGBA16_FLOAT,
                "Path Tracing/Primary Normal Roughness");
            primaryViewZ = CreatePathTexture(
                m_Device,
                signalWidth,
                signalHeight,
                nvrhi::Format::R32_FLOAT,
                "Path Tracing/Primary View Z");
        }

        std::array<nvrhi::TextureHandle, 2> directReservoirs =
            m_DirectReservoirs;
        std::array<nvrhi::TextureHandle, 2> surfaceHistory =
            m_SurfaceHistory;
        std::array<nvrhi::TextureHandle, 2> directSampleSeeds =
            m_DirectSampleSeeds;
        std::array<nvrhi::TextureHandle, 2> giCheckpointReservoirs =
            m_GiCheckpointReservoirs;
        std::array<nvrhi::TextureHandle, 2> giCheckpointCounts =
            m_GiCheckpointCounts;
        std::array<nvrhi::TextureHandle, 2> pathSeedReservoirs =
            m_PathSeedReservoirs;
        std::array<nvrhi::TextureHandle, 2> pathSeedStatistics =
            m_PathSeedStatistics;
        const uint32_t directHistoryWidth =
            directReuseRequired ? width : 1u;
        const uint32_t directHistoryHeight =
            directReuseRequired ? height : 1u;
        const uint32_t giHistoryWidth = giReuseRequired ? width : 1u;
        const uint32_t giHistoryHeight = giReuseRequired ? height : 1u;
        const uint32_t pathHistoryWidth = pathReuseRequired ? width : 1u;
        const uint32_t pathHistoryHeight = pathReuseRequired ? height : 1u;
        for (uint32_t index = 0u; index < 2u; ++index)
        {
            if (directTopologyChanged)
            {
                directReservoirs[index] = CreatePathTexture(
                    m_Device,
                    directHistoryWidth,
                    directHistoryHeight,
                    nvrhi::Format::RGBA32_FLOAT,
                    index == 0u
                        ? "Path Tracing/Direct Reservoir A"
                        : "Path Tracing/Direct Reservoir B");
                surfaceHistory[index] = CreatePathTexture(
                    m_Device,
                    directHistoryWidth,
                    directHistoryHeight,
                    nvrhi::Format::RGBA32_FLOAT,
                    index == 0u
                        ? "Path Tracing/Surface History A"
                        : "Path Tracing/Surface History B");
                directSampleSeeds[index] = CreatePathTexture(
                    m_Device,
                    directHistoryWidth,
                    directHistoryHeight,
                    nvrhi::Format::R32_UINT,
                    index == 0u
                        ? "Path Tracing/Direct Sample Seed A"
                        : "Path Tracing/Direct Sample Seed B");
            }
            if (giTopologyChanged)
            {
                giCheckpointReservoirs[index] = CreatePathTexture(
                    m_Device,
                    giHistoryWidth,
                    giHistoryHeight,
                    nvrhi::Format::RGBA32_FLOAT,
                    index == 0u
                        ? "Path Tracing/GI Local Checkpoint A"
                        : "Path Tracing/GI Local Checkpoint B");
                giCheckpointCounts[index] = CreatePathTexture(
                    m_Device,
                    giHistoryWidth,
                    giHistoryHeight,
                    nvrhi::Format::R32_UINT,
                    index == 0u
                        ? "Path Tracing/GI Local Checkpoint Count A"
                        : "Path Tracing/GI Local Checkpoint Count B");
            }
            if (pathTopologyChanged)
            {
                pathSeedReservoirs[index] = CreatePathTexture(
                    m_Device,
                    pathHistoryWidth,
                    pathHistoryHeight,
                    pathReuseRequired
                        ? nvrhi::Format::RG32_UINT
                        : nvrhi::Format::R32_UINT,
                    index == 0u
                        ? "Path Tracing/PT Local Seed A"
                        : "Path Tracing/PT Local Seed B");
                pathSeedStatistics[index] = CreatePathTexture(
                    m_Device,
                    pathHistoryWidth,
                    pathHistoryHeight,
                    nvrhi::Format::RGBA32_FLOAT,
                    index == 0u
                        ? "Path Tracing/PT Local Seed Statistics A"
                        : "Path Tracing/PT Local Seed Statistics B");
            }
        }

        const bool allCreated = rawMean && successfulSampleCount &&
            colorVariance &&
            display && residualMean && diffuseSuffixMean &&
            primaryNormalRoughness && primaryViewZ &&
            directReservoirs[0] && directReservoirs[1] &&
            surfaceHistory[0] && surfaceHistory[1] &&
            directSampleSeeds[0] && directSampleSeeds[1] &&
            giCheckpointReservoirs[0] && giCheckpointReservoirs[1] &&
            giCheckpointCounts[0] && giCheckpointCounts[1] &&
            pathSeedReservoirs[0] && pathSeedReservoirs[1] &&
            pathSeedStatistics[0] && pathSeedStatistics[1];
        if (!allCreated)
            return false;

        ClearBindingSets();
        m_RawMean = rawMean;
        m_SuccessfulSampleCount = successfulSampleCount;
        m_ColorVariance = colorVariance;
        m_Display = display;
        m_ResidualMean = residualMean;
        m_DiffuseSuffixMean = diffuseSuffixMean;
        m_PrimaryNormalRoughness = primaryNormalRoughness;
        m_PrimaryViewZ = primaryViewZ;
        m_DirectReservoirs = directReservoirs;
        m_SurfaceHistory = surfaceHistory;
        m_DirectSampleSeeds = directSampleSeeds;
        m_GiCheckpointReservoirs = giCheckpointReservoirs;
        m_GiCheckpointCounts = giCheckpointCounts;
        m_PathSeedReservoirs = pathSeedReservoirs;
        m_PathSeedStatistics = pathSeedStatistics;
        m_Width = width;
        m_Height = height;
        m_DirectReuseResourcesFullResolution = directReuseRequired;
        m_GiReuseResourcesFullResolution = giReuseRequired;
        m_PathReuseResourcesFullResolution = pathReuseRequired;
        m_StableSignalResourcesFullResolution = stableSignalsRequired;
        m_HistoryIndex = 0u;
        m_DirectReservoirHistoryValid = false;
        m_GiCheckpointHistoryValid = false;
        m_PathSeedHistoryValid = false;
        m_ResetRequested = true;
        return true;
    }

    bool PathTracingPass::EnsureLightBuffer(uint32_t lightCount)
    {
        const uint32_t requiredCapacity = std::max(lightCount, 1u);
        if (m_LightBuffer && m_LightCapacity >= requiredCapacity)
            return true;

        uint32_t newCapacity = std::max(m_LightCapacity, 1u);
        while (newCapacity < requiredCapacity &&
            newCapacity <= std::numeric_limits<uint32_t>::max() / 2u)
        {
            newCapacity *= 2u;
        }
        if (newCapacity < requiredCapacity)
            newCapacity = requiredCapacity;

        nvrhi::BufferDesc description;
        description.byteSize =
            uint64_t(newCapacity) * sizeof(LightConstants);
        description.structStride = sizeof(LightConstants);
        description.debugName = "Path Tracing/Analytic Lights";
        description.initialState = nvrhi::ResourceStates::ShaderResource;
        description.keepInitialState = true;
        nvrhi::BufferHandle lightBuffer =
            m_Device->createBuffer(description);
        if (!lightBuffer)
            return false;

        ClearBindingSets();
        m_LightBuffer = lightBuffer;
        m_LightCapacity = newCapacity;
        return true;
    }

    bool PathTracingPass::EnsureBindingSet(
        const PathTracingInputs& inputs,
        uint32_t historyIndex)
    {
        if (!inputs.worldTlas || !inputs.materialVisibility ||
            !inputs.environment || !inputs.noiseTexture ||
            !m_LightBuffer || historyIndex >= 2u)
        {
            return false;
        }
        if (m_BoundTlas != inputs.worldTlas ||
            m_BoundMaterialVisibility != inputs.materialVisibility ||
            m_BoundEnvironment != inputs.environment ||
            m_BoundNoiseTexture != inputs.noiseTexture)
        {
            ClearBindingSets();
            m_BoundTlas = inputs.worldTlas;
            m_BoundMaterialVisibility = inputs.materialVisibility;
            m_BoundEnvironment = inputs.environment;
            m_BoundNoiseTexture = inputs.noiseTexture;
        }
        if (m_BindingSets[historyIndex])
            return true;

        const uint32_t previousIndex = historyIndex ^ 1u;
        nvrhi::BindingSetDesc description;
        description.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::RayTracingAccelStruct(
                0, inputs.worldTlas),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.environment),
            nvrhi::BindingSetItem::Texture_SRV(2, inputs.noiseTexture),
            nvrhi::BindingSetItem::Texture_SRV(
                3, m_DirectReservoirs[previousIndex]),
            nvrhi::BindingSetItem::Texture_SRV(
                4, m_SurfaceHistory[previousIndex]),
            nvrhi::BindingSetItem::Texture_SRV(
                5, m_GiCheckpointReservoirs[previousIndex]),
            nvrhi::BindingSetItem::Texture_SRV(
                6, m_GiCheckpointCounts[previousIndex]),
            nvrhi::BindingSetItem::Texture_SRV(
                7, m_PathSeedReservoirs[previousIndex]),
            nvrhi::BindingSetItem::Texture_SRV(
                8, m_PathSeedStatistics[previousIndex]),
            nvrhi::BindingSetItem::Texture_SRV(
                9, m_DirectSampleSeeds[previousIndex]),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                10, inputs.materialVisibility.geometryBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                11, inputs.materialVisibility.materialBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                12, inputs.materialVisibility.geometryIndexMap),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                13, m_LightBuffer),
            nvrhi::BindingSetItem::Sampler(0, m_Sampler),
            nvrhi::BindingSetItem::Texture_UAV(0, m_RawMean),
            nvrhi::BindingSetItem::Texture_UAV(
                1, m_SuccessfulSampleCount),
            nvrhi::BindingSetItem::Texture_UAV(
                2, m_Display),
            nvrhi::BindingSetItem::Texture_UAV(
                3, m_DirectReservoirs[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                4, m_SurfaceHistory[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                5, m_GiCheckpointReservoirs[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                6, m_GiCheckpointCounts[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                7, m_PathSeedReservoirs[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                8, m_PathSeedStatistics[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(9, m_ResidualMean),
            nvrhi::BindingSetItem::Texture_UAV(
                10, m_DiffuseSuffixMean),
            nvrhi::BindingSetItem::Texture_UAV(
                11, m_PrimaryNormalRoughness),
            nvrhi::BindingSetItem::Texture_UAV(12, m_PrimaryViewZ),
            nvrhi::BindingSetItem::Texture_UAV(
                13, m_DirectSampleSeeds[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                14, m_ColorVariance)
        };
        m_BindingSets[historyIndex] = m_Device->createBindingSet(
            description,
            m_BindingLayout);
        return bool(m_BindingSets[historyIndex]);
    }

    void PathTracingPass::ClearHistory(
        nvrhi::ICommandList* commandList,
        bool preserveRevalidatedProposals)
    {
        commandList->clearTextureFloat(
            m_RawMean, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureUInt(
            m_SuccessfulSampleCount, nvrhi::AllSubresources, 0u);
        commandList->clearTextureFloat(
            m_ColorVariance, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_Display, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_ResidualMean, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_DiffuseSuffixMean,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_PrimaryNormalRoughness,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_PrimaryViewZ, nvrhi::AllSubresources, nvrhi::Color(0.f));
        for (uint32_t index = 0u; index < 2u; ++index)
        {
            if (!preserveRevalidatedProposals)
            {
                commandList->clearTextureFloat(
                    m_DirectReservoirs[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
                commandList->clearTextureFloat(
                    m_SurfaceHistory[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
                commandList->clearTextureUInt(
                    m_DirectSampleSeeds[index],
                    nvrhi::AllSubresources,
                    0u);
            }
            commandList->clearTextureFloat(
                m_GiCheckpointReservoirs[index],
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
            commandList->clearTextureUInt(
                m_GiCheckpointCounts[index],
                nvrhi::AllSubresources,
                0u);
            if (!preserveRevalidatedProposals)
            {
                commandList->clearTextureUInt(
                    m_PathSeedReservoirs[index],
                    nvrhi::AllSubresources,
                    0u);
                commandList->clearTextureFloat(
                    m_PathSeedStatistics[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
            }
        }
        if (!preserveRevalidatedProposals)
        {
            m_HistoryIndex = 0u;
            m_DirectReservoirHistoryValid = false;
            m_PathSeedHistoryValid = false;
        }
        m_GiCheckpointHistoryValid = false;
        m_ProgressivePhase = 0u;
        m_AccumulationSchedulingCycle = 0u;
        m_HistoryValid = false;
        m_CompletedSignalCycle = false;
        m_SignalEpoch = 0u;
        m_ResetRequested = false;
    }

    PathTracingResult PathTracingPass::Render(
        nvrhi::ICommandList* commandList,
        const PathTracingInputs& requestedInputs)
    {
        PathTracingResult failure;
        failure.capabilities = m_Capabilities;
        if (!m_Capabilities.rayQuerySupported || !commandList)
            return failure;

        PathTracingInputs inputs = requestedInputs;
        inputs.settings = SanitizePathTracingSettings(inputs.settings);
        const PathTracingSettings requestedSettings = inputs.settings;
        const PathTracingPipelineResolution pipelineResolution =
            m_Capabilities.ResolvePipeline(requestedSettings);
        const bool pipelineFallbackActive =
            pipelineResolution.fallbackApplied;
        if (pipelineFallbackActive)
        {
            if (!m_ReportedUnavailablePipeline)
            {
                log::warning(
                    "The selected path-tracing pipeline is unavailable; using the closest executable fallback variant");
                m_ReportedUnavailablePipeline = true;
            }
        }
        else
        {
            m_ReportedUnavailablePipeline = false;
        }
        inputs.settings = pipelineResolution.effectiveSettings;
        const uint32_t pipelineVariant = pipelineResolution.effectiveVariant;
        if (!pipelineResolution.executable ||
            !m_Capabilities.IsPipelineAvailable(inputs.settings) ||
            pipelineVariant >= m_Pipelines.size() ||
            !m_Pipelines[pipelineVariant])
        {
            return failure;
        }
        if (inputs.view && (inputs.width == 0u || inputs.height == 0u))
        {
            const nvrhi::Rect extent = inputs.view->GetViewExtent();
            inputs.width = uint32_t(std::max(extent.width(), 0));
            inputs.height = uint32_t(std::max(extent.height(), 0));
        }

        const bool validEnvironment = inputs.environment &&
            inputs.environment->getDesc().dimension ==
                nvrhi::TextureDimension::TextureCube;
        const bool validNoise = inputs.noiseTexture &&
            inputs.noiseTexture->getDesc().dimension ==
                nvrhi::TextureDimension::Texture2DArray;
        const bool validScalars = std::isfinite(inputs.environmentScale) &&
            inputs.environmentScale >= 0.f &&
            std::isfinite(inputs.rayBias) && inputs.rayBias > 0.f &&
            std::isfinite(inputs.maximumRayDistance) &&
            inputs.maximumRayDistance > inputs.rayBias;
        const bool directReuseRequired = inputs.settings.useRtxdi &&
            inputs.settings.reuseDirectReservoirs &&
            m_Capabilities.temporalReservoirReuseSupported;
        const bool giReuseRequired =
            inputs.settings.solver == PathTracingSolver::RestirGi &&
            inputs.settings.reuseIndirectGiReservoirs &&
            m_Capabilities.temporalGiCheckpointReuseSupported;
        const bool pathReuseRequired =
            inputs.settings.solver == PathTracingSolver::RestirPt &&
            inputs.settings.reusePathReservoirs &&
            m_Capabilities.continuationSeedReservoirSupported &&
            m_Capabilities.replayablePathSeedSupported;
        const bool stableSignalsRequested =
            m_Capabilities.CanUseSpatialPathResolve(requestedSettings) &&
            CanUseSpatialPathResolve(
                inputs.settings,
                m_Capabilities.stablePlaneSignalSupported &&
                    m_Capabilities.stablePlaneResolveSupported);
        bool stableSignalsRequired = stableSignalsRequested;
        const bool validInputs = inputs.view && inputs.worldTlas &&
            bool(inputs.materialVisibility) && validEnvironment && validNoise &&
            IsValidNoiseSettings(inputs.noiseSettings) && validScalars;
        bool resourcesReady = validInputs && EnsureResources(
            inputs.width,
            inputs.height,
            directReuseRequired,
            giReuseRequired,
            pathReuseRequired,
            stableSignalsRequired);
        if (!resourcesReady && validInputs && stableSignalsRequired)
        {
            // A reconstruction allocation failure must not discard a valid
            // transport frame. Retry with 1x1 inactive signal surfaces and
            // report raw fallback through the result contract.
            stableSignalsRequired = false;
            resourcesReady = EnsureResources(
                inputs.width,
                inputs.height,
                directReuseRequired,
                giReuseRequired,
                pathReuseRequired,
                false);
        }
        if (!resourcesReady)
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Path tracing received incomplete, mismatched, or unsupported inputs");
                m_ReportedInvalidInput = true;
            }
            return failure;
        }

        PathTracingConstants constants = {};
        uint32_t geometryMapCount = 0u;
        uint32_t geometryCount = 0u;
        uint32_t materialCount = 0u;
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
            descriptorCapacity == 0u)
        {
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "Path tracing received invalid ray-material buffer or descriptor bounds");
                m_ReportedInvalidInput = true;
            }
            return failure;
        }
        constants.rayMaterialLimits = uint4(
            geometryMapCount,
            geometryCount,
            materialCount,
            descriptorCapacity);
        inputs.view->FillPlanarViewConstants(constants.view);
        // The fallback makes every matrix lane deterministic even when no
        // prior view exists. Only the explicit validity word enables shader
        // reprojection; the current view is never mistaken for history.
        constants.previousView = constants.view;
        constants.previousViewValid = 0u;
        if (inputs.previousView && inputs.historyResetByViewOnly)
        {
            inputs.previousView->FillPlanarViewConstants(
                constants.previousView);
            constants.previousViewValid = 1u;
        }
        constants.flashlight.lightIndex = -1;
        if (inputs.lights.size() >
            size_t(std::numeric_limits<uint32_t>::max()))
        {
            log::error("Path tracing cannot address more than UINT32_MAX analytic lights");
            return failure;
        }
        std::vector<LightConstants> submittedLights;
        submittedLights.reserve(inputs.lights.size());
        for (const std::shared_ptr<Light>& light : inputs.lights)
        {
            if (!light)
                continue;
            submittedLights.emplace_back();
            LightConstants& lightConstants = submittedLights.back();
            // Donut intentionally leaves light-type-irrelevant vector lanes
            // untouched. Canonicalize the complete upload/hash record first so
            // those lanes cannot spuriously invalidate accumulation history.
            std::memset(&lightConstants, 0, sizeof(lightConstants));
            light->FillLightConstants(lightConstants);
            if (light.get() == inputs.flashlight)
            {
                constants.flashlight.lightIndex =
                    int(submittedLights.size() - 1u);
            }
        }
        constants.lightCount = uint32_t(submittedLights.size());
        if (!EnsureLightBuffer(constants.lightCount))
        {
            log::error("Path tracing could not allocate its analytic-light buffer");
            return failure;
        }
        if (constants.flashlight.lightIndex >= 0 &&
            FlashlightBeamProfileIsValid(inputs.flashlightProfile))
        {
            constants.flashlight.profile = inputs.flashlightProfile;
        }
        else
        {
            constants.flashlight.profile = {};
            constants.flashlight.lightIndex = -1;
        }

        constants.environmentScale = inputs.environmentScale;
        constants.rayBias = inputs.rayBias;
        constants.maximumRayDistance = inputs.maximumRayDistance;
        constants.fireflyThreshold = inputs.settings.fireflyThreshold;
        constants.dispatchExtent = uint2(inputs.width, inputs.height);
        constants.sampleSequencePhase = inputs.samplingPhase;
        constants.noisePattern =
            static_cast<uint32_t>(inputs.noiseSettings.pattern);
        const SampleAccumulationSettings accumulationSettings =
            SanitizeSampleAccumulationSettings(
                inputs.accumulationSettings);
        constants.accumulationAveraging =
            static_cast<uint32_t>(accumulationSettings.averaging);
        constants.accumulationScheduling =
            static_cast<uint32_t>(accumulationSettings.scheduling);
        constants.accumulationEffectiveHistory =
            accumulationSettings.effectiveHistory;
        constants.accumulationMinimumSamples =
            accumulationSettings.minimumSamples;
        constants.accumulationTargetRelativeError =
            accumulationSettings.targetRelativeError;
        constants.accumulationMinimumUpdateRate =
            accumulationSettings.minimumUpdateRate;
        const bool requestedPathPresetIsExecutable =
            pipelineResolution.executable &&
            inputs.settings.solver == requestedSettings.solver;
        constants.maxBounces = inputs.settings.maxBounces;
        constants.russianRouletteStart =
            inputs.settings.russianRouletteStart;
        constants.neeCandidateCount = inputs.settings.neeCandidateCount;
        constants.debugView =
            static_cast<uint32_t>(inputs.settings.debugView);
        constants.stablePlaneCount = constants.debugView ==
                static_cast<uint32_t>(PathTracingDebugView::SignalGroup)
            ? std::max(inputs.settings.stablePlaneCount, 2u)
            : inputs.settings.stablePlaneCount;
        if (inputs.accumulateSamples)
            constants.flags |= UVSR_PATH_TRACING_FLAG_ACCUMULATE_SAMPLES;
        if (directReuseRequired)
            constants.flags |= UVSR_PATH_TRACING_FLAG_REUSE_DIRECT;
        if (giReuseRequired)
        {
            constants.flags |=
                UVSR_PATH_TRACING_FLAG_REUSE_GI_CHECKPOINT;
        }
        if (pathReuseRequired)
            constants.flags |= UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS;
        if (stableSignalsRequired)
        {
            constants.flags |=
                UVSR_PATH_TRACING_FLAG_WRITE_STABLE_SIGNALS;
        }
        if (inputs.settings.enableFireflyFilter)
            constants.flags |= UVSR_PATH_TRACING_FLAG_FILTER_FIREFLIES;
        if (inputs.view->IsReverseDepth())
            constants.flags |= UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH;
        if (inputs.showEnvironmentBackground)
        {
            constants.flags |=
                UVSR_PATH_TRACING_FLAG_SHOW_ENVIRONMENT_BACKGROUND;
        }
        const bool debugViewChanged = constants.debugView != m_LastDebugView;
        if (debugViewChanged)
        {
            m_LastDebugView = constants.debugView;
            m_ProgressivePhase = 0u;
            m_DebugRefreshActive = true;
        }
        if (m_DebugRefreshActive)
            constants.flags |= UVSR_PATH_TRACING_FLAG_REFRESH_DEBUG;

        const uint64_t schedulingSerial =
            inputs.schedulingSerial == std::numeric_limits<uint64_t>::max()
                ? m_SchedulingSerial++
                : inputs.schedulingSerial;
        constants.schedulingSerialLow = LowWord(schedulingSerial);
        constants.schedulingSerialHigh = HighWord(schedulingSerial);

        const uint64_t transportSignature =
            BuildTransportSignature(
                inputs,
                constants,
                submittedLights,
                stableSignalsRequired);
        const bool historyReset = m_ResetRequested || !m_HistoryValid ||
            m_LastHistoryEpoch != inputs.historyEpoch ||
            m_LastTransportSignature != transportSignature;
        const PathTracingDispatchSchedule dispatchSchedule =
            BuildPathTracingDispatchSchedule(
                inputs.width,
                inputs.height,
                inputs.settings,
                constants.lightCount,
                historyReset ? 0u : m_ProgressivePhase);
        if (!dispatchSchedule.valid)
        {
            if (!m_ReportedUnsafeSchedule)
            {
                log::error(
                    "The selected path-tracing settings exceed the safe per-dispatch work budget");
                m_ReportedUnsafeSchedule = true;
            }
            return failure;
        }
        m_ReportedUnsafeSchedule = false;
        if (historyReset && inputs.noiseSettings.animate)
        {
            constants.flags |=
                UVSR_PATH_TRACING_FLAG_ANIMATE_HISTORY_RESET;
        }
        const bool preserveRevalidatedProposals =
            historyReset &&
            inputs.historyResetByViewOnly &&
            inputs.previousView != nullptr &&
            inputs.settings.reuseRevalidatedProposalsDuringMotion &&
            !m_ResetRequested &&
            m_HistoryValid &&
            dispatchSchedule.phaseCount == 1u &&
            (m_DirectReservoirHistoryValid ||
                m_PathSeedHistoryValid);
        const bool previousDirectHistoryAvailable =
            m_DirectReservoirHistoryValid &&
            (!historyReset || preserveRevalidatedProposals);
        const bool previousPathHistoryAvailable =
            m_PathSeedHistoryValid &&
            (!historyReset || preserveRevalidatedProposals);
        const bool previousGiHistoryAvailable =
            m_GiCheckpointHistoryValid && !historyReset;
        if (historyReset)
        {
            ClearHistory(commandList, preserveRevalidatedProposals);
            if (stableSignalsRequired)
                m_SignalEpoch = inputs.historyEpoch;
        }
        if (inputs.accumulateSamples)
        {
            // Adaptive revisits advance only after a complete successfully
            // submitted lattice cycle. Render gaps cannot skip a pixel's
            // deterministic revisit phase.
            constants.schedulingSerialLow =
                LowWord(m_AccumulationSchedulingCycle);
            constants.schedulingSerialHigh =
                HighWord(m_AccumulationSchedulingCycle);
        }
        m_LastHistoryEpoch = inputs.historyEpoch;
        m_LastTransportSignature = transportSignature;

        constants.schedulingGrid = dispatchSchedule.grid;
        constants.schedulingPhase = dispatchSchedule.phase;
        const uint32_t outputHistoryIndex = m_HistoryIndex;
        if (!EnsureBindingSet(inputs, outputHistoryIndex))
        {
            if (!m_ReportedInvalidInput)
            {
                log::error("Path-tracing binding-set creation failed");
                m_ReportedInvalidInput = true;
            }
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

        commandList->beginMarker("Shared Path Transport");
        nvrhi::ComputeState state;
        state.pipeline = m_Pipelines[pipelineVariant];
        state.bindings = {
            m_BindingSets[outputHistoryIndex],
            inputs.materialVisibility.descriptorTable
        };
        commandList->setComputeState(state);
        commandList->dispatch(
            div_ceil(dispatchSchedule.workExtent.x, 8u),
            div_ceil(dispatchSchedule.workExtent.y, 8u));

        if (historyReset && dispatchSchedule.phaseCount > 1u)
        {
            // Publish the newly traced phase before a cheap full-resolution
            // pass copies each phase-zero representative through its tile.
            // This initializes presentation only; raw means/counts and every
            // proposal history remain reset until their pixels are traced.
            nvrhi::utils::TextureUavBarrier(commandList, m_Display);
            commandList->commitBarriers();
            constants.flags |=
                UVSR_PATH_TRACING_FLAG_REPLICATE_PREVIEW;
            commandList->writeBuffer(
                m_ConstantBuffer,
                &constants,
                sizeof(constants));
            commandList->setComputeState(state);
            commandList->dispatch(
                div_ceil(inputs.width, 8u),
                div_ceil(inputs.height, 8u));
        }
        commandList->endMarker();

        m_HistoryValid = true;
        const uint32_t nextProgressivePhase =
            (m_ProgressivePhase + 1u) % dispatchSchedule.phaseCount;
        const bool completedProgressiveCycle =
            nextProgressivePhase == 0u;
        m_ProgressivePhase = nextProgressivePhase;
        const bool anyReservoirHistoryRequired = directReuseRequired ||
            giReuseRequired || pathReuseRequired;
        if (completedProgressiveCycle && anyReservoirHistoryRequired)
        {
            m_HistoryIndex ^= 1u;
            m_DirectReservoirHistoryValid = directReuseRequired;
            m_GiCheckpointHistoryValid = giReuseRequired;
            m_PathSeedHistoryValid = pathReuseRequired;
        }
        if (completedProgressiveCycle)
        {
            m_DebugRefreshActive = false;
            if (inputs.accumulateSamples)
                ++m_AccumulationSchedulingCycle;
            if (stableSignalsRequired)
                m_CompletedSignalCycle = true;
        }
        ++m_SubmittedSamplePassCount;

        const bool directReservoirActive = inputs.settings.useRtxdi;
        const bool reuseRequested = directReuseRequired;

        PathTracingResult result;
        result.sceneLinearDisplay = m_Display;
        result.rawMean = m_RawMean;
        result.successfulSampleCount = m_SuccessfulSampleCount;
        result.colorVariance = m_ColorVariance;
        result.directReservoir = directReuseRequired
            ? m_DirectReservoirs[outputHistoryIndex].Get()
            : nullptr;
        result.giCheckpointReservoir = giReuseRequired
            ? m_GiCheckpointReservoirs[outputHistoryIndex].Get()
            : nullptr;
        result.pathSeedReservoir = pathReuseRequired
            ? m_PathSeedReservoirs[outputHistoryIndex].Get()
            : nullptr;
        const bool coherentSignalsAvailable = stableSignalsRequired &&
            m_CompletedSignalCycle &&
            m_SignalEpoch == inputs.historyEpoch;
        result.residualMean = coherentSignalsAvailable
            ? m_ResidualMean.Get()
            : nullptr;
        result.diffuseSuffixMean = coherentSignalsAvailable
            ? m_DiffuseSuffixMean.Get()
            : nullptr;
        result.primaryNormalRoughness = coherentSignalsAvailable
            ? m_PrimaryNormalRoughness.Get()
            : nullptr;
        result.primaryViewZ = coherentSignalsAvailable
            ? m_PrimaryViewZ.Get()
            : nullptr;
        result.capabilities = m_Capabilities;
        result.submittedSamplePassCount = m_SubmittedSamplePassCount;
        result.estimatedWorkUnitsPerPixel =
            dispatchSchedule.estimatedWorkUnitsPerPixel;
        result.dispatchPhaseCount = dispatchSchedule.phaseCount;
        result.signalEpoch = coherentSignalsAvailable
            ? m_SignalEpoch
            : 0u;
        result.dispatched = true;
        result.historyReset = historyReset;
        result.completedSignalCycle = coherentSignalsAvailable;
        result.directReservoirActive = directReservoirActive;
        result.temporalReuseActive =
            reuseRequested && previousDirectHistoryAvailable;
        result.spatialReuseActive =
            reuseRequested && previousDirectHistoryAvailable &&
            m_Capabilities.previousFrameSpatialReuseSupported;
        result.giCheckpointReuseActive =
            giReuseRequired && previousGiHistoryAvailable;
        result.pathSeedReplayActive =
            pathReuseRequired && previousPathHistoryAvailable;
        result.serRequestedButUnavailable =
            requestedSettings.useSer && !m_Capabilities.serSupported;
        result.stablePlaneResolveRequestedButUnavailable =
            IsSpatialPathResolveRequested(requestedSettings) &&
            !stableSignalsRequired;
        result.pathReuseRequestedButUnavailable =
            requestedSettings.reusePathReservoirs && !pathReuseRequired;
        result.giReuseRequestedButUnavailable =
            requestedSettings.reuseIndirectGiReservoirs && !giReuseRequired;
        result.cleanRoomSolverSubsetActive =
            (inputs.settings.solver == PathTracingSolver::RestirPt &&
                pathReuseRequired) ||
            (inputs.settings.solver == PathTracingSolver::RestirGi &&
                giReuseRequired);
        result.namesakeParityUnavailable =
            requestedSettings.solver != PathTracingSolver::RtxPt;
        result.solverPresetRequestedButUnavailable =
            !requestedPathPresetIsExecutable;
        result.geometricReconnectionUnavailable =
            requestedSettings.solver != PathTracingSolver::RtxPt &&
            !m_Capabilities.fullSampleReconnectionSupported;
        result.pipelineFallbackActive = pipelineFallbackActive;
        result.rawMeanBiasedByFireflyFilter =
            inputs.settings.enableFireflyFilter;
        return result;
    }

    void PathTracingPass::ResetHistory()
    {
        m_ResetRequested = true;
    }

    void PathTracingPass::ResetBindingCache()
    {
        ClearBindingSets();
        m_BoundTlas = nullptr;
        m_BoundMaterialVisibility = {};
        m_BoundEnvironment = nullptr;
        m_BoundNoiseTexture = nullptr;
    }

    void PathTracingPass::ClearBindingSets()
    {
        for (nvrhi::BindingSetHandle& bindingSet : m_BindingSets)
            bindingSet = nullptr;
    }
}
