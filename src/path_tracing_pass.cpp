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
static_assert(sizeof(InstanceData) == 112u);
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

        constexpr uint64_t EstimateSharedPrimaryWorkUnitsPerPixel(
            const PathTracingSettings& settings,
            uint32_t lightCount)
        {
            if (!settings.sharedPrimarySurface)
                return 0u;
            const uint64_t candidateCount = lightCount > 0u
                ? std::max(settings.neeCandidateCount, 1u)
                : 0u;
            const uint64_t selectionWork =
                settings.neeMode == PathTracingNeeMode::Uniform
                    ? 0u
                    : SaturatingMultiply(uint64_t(lightCount), 2u);
            const uint64_t candidateWork = SaturatingMultiply(
                candidateCount,
                SaturatingAdd(1u, selectionWork));
            const uint64_t donorWork = settings.useRtxdi &&
                    UsesDirectReservoirHistory(settings)
                ? SaturatingAdd(
                    settings.temporalReuse ? 1u : 0u,
                    settings.spatialNeighborCount)
                : 0u;
            return SaturatingAdd(
                SaturatingAdd(1u, donorWork),
                SaturatingAdd(
                    candidateWork,
                    settings.useRtxdi && lightCount > 0u ? 1u : 0u));
        }

        constexpr uint64_t EstimatePathTracingTransportWorkUnitsPerPixel(
            const PathTracingSettings& settings,
            uint32_t lightCount,
            bool directHistoryAvailable,
            bool pathSeedHistoryAvailable,
            bool giHistoryAvailable,
            bool stableSignalsRequired)
        {
            const uint64_t candidateCount = lightCount > 0u
                ? std::max(settings.neeCandidateCount, 1u)
                : 0u;
            const uint64_t selectionWork =
                settings.neeMode == PathTracingNeeMode::Uniform
                    ? 0u
                    : SaturatingMultiply(uint64_t(lightCount), 2u);
            const uint64_t candidateWork = SaturatingMultiply(
                candidateCount,
                SaturatingAdd(1u, selectionWork));
            const uint64_t tracedBounceCount =
                settings.sharedPrimarySurface && settings.maxBounces > 0u
                    ? settings.maxBounces - 1u
                    : settings.maxBounces;
            const uint64_t currentPathWork = std::max<uint64_t>(
                1u,
                SaturatingMultiply(
                    uint64_t(tracedBounceCount),
                    SaturatingAdd(1u, candidateWork)));
            // Replay donor paths are traced once per pixel batch. Shared
            // Primary supplies bounce zero without a ray; otherwise replay
            // still traces it while skipping its discarded lighting work.
            const uint64_t replayPathWork = std::max<uint64_t>(
                1u,
                SaturatingAdd(
                    settings.sharedPrimarySurface ? 0u : 1u,
                    SaturatingMultiply(
                        settings.maxBounces > 0u
                            ? uint64_t(settings.maxBounces - 1u)
                            : 0u,
                        SaturatingAdd(1u, candidateWork))));
            uint64_t replayCount = 0u;
            if (pathSeedHistoryAvailable && UsesPathSeedHistory(settings))
            {
                replayCount = SaturatingAdd(
                    replayCount,
                    settings.temporalReuse ? 1u : 0u);
                replayCount = SaturatingAdd(
                    replayCount,
                    settings.spatialNeighborCount);
            }
            uint64_t directDonorWork = 0u;
            if (!settings.sharedPrimarySurface && directHistoryAvailable &&
                UsesDirectReservoirHistory(settings))
            {
                directDonorWork = SaturatingAdd(
                    settings.temporalReuse ? 1u : 0u,
                    settings.spatialNeighborCount);
            }
            // RTXDI evaluates current and reused candidates without shadow
            // rays, then traces the finally selected light once. Conventional
            // NEE charges visibility in candidateWork instead.
            const uint64_t rtxdiResolveWork = settings.useRtxdi &&
                    lightCount > 0u && !settings.sharedPrimarySurface
                ? 1u
                : 0u;
            const uint64_t workPerFreshSample = SaturatingAdd(
                currentPathWork,
                rtxdiResolveWork);
            const uint64_t replayDonorBatch = SaturatingMultiply(
                replayCount,
                replayPathWork);
            const uint64_t directDonorBatch = SaturatingMultiply(
                directDonorWork,
                settings.sharedPrimarySurface
                    ? 1u
                    : settings.samplesPerPixel);
            uint64_t giDonorWork = 0u;
            if (giHistoryAvailable && UsesGiCheckpointHistory(settings))
            {
                giDonorWork = SaturatingAdd(
                    settings.temporalReuse ? 1u : 0u,
                    settings.spatialNeighborCount);
            }
            // Stable-signal guide reconstruction traces one deterministic
            // center ray after the complete fresh-sample batch.
            return SaturatingAdd(
                SaturatingAdd(
                    SaturatingAdd(
                        SaturatingMultiply(
                            workPerFreshSample,
                            settings.samplesPerPixel),
                        SaturatingAdd(
                            replayDonorBatch,
                            directDonorBatch)),
                    giDonorWork),
                stableSignalsRequired ? 1u : 0u);
        }

        constexpr uint64_t EstimatePathTracingFrameWorkUnitsPerPixel(
            const PathTracingSettings& settings,
            uint32_t lightCount,
            bool directHistoryAvailable,
            bool pathSeedHistoryAvailable,
            bool giHistoryAvailable,
            bool stableSignalsRequired)
        {
            return SaturatingAdd(
                EstimatePathTracingTransportWorkUnitsPerPixel(
                    settings,
                    lightCount,
                    directHistoryAvailable,
                    pathSeedHistoryAvailable,
                    giHistoryAvailable,
                    stableSignalsRequired),
                EstimateSharedPrimaryWorkUnitsPerPixel(
                    settings,
                    lightCount));
        }

        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            PathTracingSettings{}, 0u, false, false, false, false) == 7u);
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            PathTracingSettings{}, 1u, false, false, false, false) == 14u);
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirPt),
            0u, false, false, false, false) == 7u);
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirPt),
            1u, false, true, false, false) == 26u);
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirGi),
            1u, false, false, false, false) == 14u);
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirPt),
            24u, false, true, false, false) == 26u);
        constexpr PathTracingSettings NonSharedRestirPt = []
        {
            PathTracingSettings settings = ApplyPathTracingSolverPreset(
                PathTracingSolver::RestirPt);
            settings.sharedPrimarySurface = false;
            return settings;
        }();
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            NonSharedRestirPt,
            1u,
            false,
            true,
            false,
            false) == 30u);
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            NonSharedRestirPt,
            0u,
            false,
            true,
            false,
            false) == 16u);
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirGi),
            24u, false, false, false, false) == 14u);
        constexpr PathTracingSettings MaximumFreshRestirPt = []
        {
            PathTracingSettings settings = ApplyPathTracingSolverPreset(
                PathTracingSolver::RestirPt);
            settings.samplesPerPixel = PathTracingMaxSamplesPerPixel;
            return settings;
        }();
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            MaximumFreshRestirPt,
            1u,
            false,
            true,
            false,
            true) == 63u);
        constexpr PathTracingSettings MaximumDirectDonorBatch = []
        {
            PathTracingSettings settings;
            settings.useRtxdi = true;
            settings.temporalReuse = true;
            settings.spatialNeighborCount =
                PathTracingMaxSpatialNeighborCount;
            settings.samplesPerPixel = PathTracingMaxSamplesPerPixel;
            return settings;
        }();
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            MaximumDirectDonorBatch,
            1u,
            true,
            false,
            false,
            false) == 56u);
        static_assert(EstimatePathTracingFrameWorkUnitsPerPixel(
            MaximumDirectDonorBatch,
            1u,
            true,
            false,
            false,
            true) == 57u);
        static_assert(3840ull * 2160ull * 16ull <=
            MaxPathTracingWorkUnitsPerDispatch);
        static_assert(3840ull * 2160ull * 72ull <=
            MaxPathTracingWorkUnitsPerDispatch);
        static_assert(3840ull * 2160ull * 129ull <=
            MaxPathTracingWorkUnitsPerDispatch);
        static_assert(1920ull * 1080ull * 163ull <=
            MaxPathTracingWorkUnitsPerDispatch);
        static_assert(3840ull * 2160ull * 163ull >
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
            bool directHistoryAvailable,
            bool pathSeedHistoryAvailable,
            bool giHistoryAvailable,
            bool stableSignalsRequired,
            uint32_t progressivePhase)
        {
            const uint64_t workPerPixel =
                EstimatePathTracingTransportWorkUnitsPerPixel(
                    settings,
                    lightCount,
                    directHistoryAvailable,
                    pathSeedHistoryAvailable,
                    giHistoryAvailable,
                    stableSignalsRequired);
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
                    inputs.materialVisibility.geometryIndexMap),
                reinterpret_cast<uintptr_t>(
                    inputs.materialVisibility.instanceBuffer)
            };
            hash = HashBytes(
                hash,
                resourcePointers,
                sizeof(resourcePointers));

            const PathTracingSettings& settings = inputs.settings;
            hash = HashValue(hash, settings.solver);
            hash = HashValue(hash, settings.neeMode);
            hash = HashValue(hash, settings.maxBounces);
            hash = HashValue(hash, settings.useRussianRoulette);
            hash = HashValue(hash, settings.neeCandidateCount);
            hash = HashValue(hash, settings.samplesPerPixel);
            hash = HashValue(hash, settings.sharedPrimarySurface);
            hash = HashValue(hash, settings.useRtxdi);
            hash = HashValue(hash, settings.temporalReuse);
            hash = HashValue(hash, settings.spatialNeighborCount);
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
        capabilities.spatialGiCheckpointReuseSupported =
            capabilities.rayQuerySupported;
        // The executable GI path reconnects a bounded rough diffuse tail. It
        // deliberately does not claim arbitrary glossy full-path shifting.
        capabilities.fullSampleReconnectionSupported = false;
        capabilities.sharedPrimarySurfaceSupported =
            capabilities.rayQuerySupported && pathSeedFormatSupported;
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
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(14),
            nvrhi::BindingLayoutItem::Texture_SRV(15),
            nvrhi::BindingLayoutItem::Texture_SRV(16),
            nvrhi::BindingLayoutItem::Texture_SRV(17),
            nvrhi::BindingLayoutItem::Texture_SRV(18),
            nvrhi::BindingLayoutItem::Texture_SRV(19),
            nvrhi::BindingLayoutItem::Texture_SRV(20),
            nvrhi::BindingLayoutItem::Texture_SRV(21),
            nvrhi::BindingLayoutItem::Texture_SRV(22),
            nvrhi::BindingLayoutItem::Texture_SRV(23),
            nvrhi::BindingLayoutItem::Sampler(0)
        };
        for (uint32_t slot = 0u; slot <= 15u; ++slot)
        {
            layoutDescription.bindings.push_back(
                nvrhi::BindingLayoutItem::Texture_UAV(slot));
        }
        layoutDescription.bindings.push_back(
            nvrhi::BindingLayoutItem::Texture_UAV(25));
        layoutDescription.bindings.push_back(
            nvrhi::BindingLayoutItem::Texture_UAV(26));
        layoutDescription.bindings.push_back(
            nvrhi::BindingLayoutItem::Texture_UAV(27));
        m_BindingLayout = device->createBindingLayout(layoutDescription);

        nvrhi::BindingLayoutDesc primaryLayoutDescription;
        primaryLayoutDescription.visibility = nvrhi::ShaderType::Compute;
        primaryLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::ConstantBuffer(0),
            nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_SRV(9),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(13),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(14),
            nvrhi::BindingLayoutItem::Texture_SRV(15),
            nvrhi::BindingLayoutItem::Texture_SRV(24),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(2),
            nvrhi::BindingLayoutItem::Texture_UAV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(9),
            nvrhi::BindingLayoutItem::Texture_UAV(13),
            nvrhi::BindingLayoutItem::Texture_UAV(16),
            nvrhi::BindingLayoutItem::Texture_UAV(17),
            nvrhi::BindingLayoutItem::Texture_UAV(18),
            nvrhi::BindingLayoutItem::Texture_UAV(19),
            nvrhi::BindingLayoutItem::Texture_UAV(20),
            nvrhi::BindingLayoutItem::Texture_UAV(21),
            nvrhi::BindingLayoutItem::Texture_UAV(22),
            nvrhi::BindingLayoutItem::Texture_UAV(23),
            nvrhi::BindingLayoutItem::Texture_UAV(24)
        };
        m_PrimaryBindingLayout = device->createBindingLayout(
            primaryLayoutDescription);
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

        for (uint32_t variant = 0u;
            variant < m_PrimaryShaders.size();
            ++variant)
        {
            static constexpr const char* NeeModes[] = { "0", "1", "2" };
            const uint32_t rtxdiVariant =
                variant / PathTracingNeeModeCount;
            const uint32_t neeVariant =
                variant % PathTracingNeeModeCount;
            const std::vector<ShaderMacro> defines = {
                ShaderMacro(
                    "UVSR_PT_RTXDI",
                    rtxdiVariant == 0u ? "0" : "1"),
                ShaderMacro("UVSR_PT_NEE_MODE", NeeModes[neeVariant])
            };
            m_PrimaryShaders[variant] = shaderFactory->CreateShader(
                "uvsr/path_tracing_primary_surface_cs.hlsl",
                "main",
                &defines,
                nvrhi::ShaderType::Compute);
            if (!m_PrimaryShaders[variant] || !m_PrimaryBindingLayout)
                continue;
            nvrhi::ComputePipelineDesc pipelineDescription;
            pipelineDescription.CS = m_PrimaryShaders[variant];
            pipelineDescription.bindingLayouts = {
                m_PrimaryBindingLayout,
                m_BindlessLayout
            };
            m_PrimaryPipelines[variant] = device->createComputePipeline(
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
        m_Capabilities.spatialGiCheckpointReuseSupported &=
            restirGiPipelinesReady;
        m_Capabilities.fullSampleReconnectionSupported &=
            restirGiPipelinesReady;
        m_Capabilities.previousFrameSpatialReuseSupported &=
            rtxdiPipelinesReady || restirPtPipelinesReady ||
            restirGiPipelinesReady;
        const bool sharedPrimaryPipelinesReady = std::all_of(
            m_PrimaryPipelines.begin(),
            m_PrimaryPipelines.end(),
            [](const nvrhi::ComputePipelineHandle& pipeline)
            {
                return bool(pipeline);
            });
        m_Capabilities.sharedPrimarySurfaceSupported &=
            sharedPrimaryPipelinesReady;
    }

    bool PathTracingPass::EnsureResources(
        uint32_t width,
        uint32_t height,
        bool directReuseRequired,
        bool giReuseRequired,
        bool pathReuseRequired,
        bool stableSignalsRequired,
        bool sharedPrimaryRequired)
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
                stableSignalsRequired &&
            m_SharedPrimaryResourcesFullResolution ==
                sharedPrimaryRequired)
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
        const bool sharedPrimaryTopologyChanged = extentChanged ||
            m_SharedPrimaryResourcesFullResolution !=
                sharedPrimaryRequired;

        // Preserve the expensive base transport surfaces when only a solver
        // history family changes. This keeps a GI/PT/RTXDI mode switch from
        // allocating a complete second copy of the output set while the old
        // set is still referenced by an in-flight frame.
        nvrhi::TextureHandle rawMean = m_RawMean;
        nvrhi::TextureHandle successfulSampleCount =
            m_SuccessfulSampleCount;
        nvrhi::TextureHandle colorVariance = m_ColorVariance;
        nvrhi::TextureHandle display = m_Display;
        nvrhi::TextureHandle directMean = m_DirectMean;
        nvrhi::TextureHandle directSampleCount = m_DirectSampleCount;
        nvrhi::TextureHandle indirectMean = m_IndirectMean;
        nvrhi::TextureHandle sharedPositionHit = m_SharedPositionHit;
        std::array<nvrhi::TextureHandle, 2> sharedGeometryMaterial =
            m_SharedGeometryMaterial;
        nvrhi::TextureHandle sharedNormalAlpha = m_SharedNormalAlpha;
        nvrhi::TextureHandle sharedDiffuse = m_SharedDiffuse;
        nvrhi::TextureHandle sharedSpecular = m_SharedSpecular;
        nvrhi::TextureHandle pathMotion = m_PathMotion;
        nvrhi::TextureHandle pathDepth = m_PathDepth;
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
        if (sharedPrimaryTopologyChanged)
        {
            const uint32_t sharedWidth = sharedPrimaryRequired ? width : 1u;
            const uint32_t sharedHeight = sharedPrimaryRequired ? height : 1u;
            directMean = CreatePathTexture(
                m_Device, sharedWidth, sharedHeight,
                nvrhi::Format::RGBA32_FLOAT,
                "Path Tracing/Shared Direct Mean");
            directSampleCount = CreatePathTexture(
                m_Device, sharedWidth, sharedHeight,
                nvrhi::Format::R32_UINT,
                "Path Tracing/Shared Direct Sample Count");
            indirectMean = CreatePathTexture(
                m_Device, sharedWidth, sharedHeight,
                nvrhi::Format::RGBA32_FLOAT,
                "Path Tracing/Indirect Mean");
            sharedPositionHit = CreatePathTexture(
                m_Device, sharedWidth, sharedHeight,
                nvrhi::Format::RGBA32_FLOAT,
                "Path Tracing/Shared Position Hit");
            for (uint32_t index = 0u; index < 2u; ++index)
            {
                sharedGeometryMaterial[index] = CreatePathTexture(
                    m_Device,
                    sharedWidth,
                    sharedHeight,
                    nvrhi::Format::RG32_UINT,
                    index == 0u
                        ? "Path Tracing/Shared Geometry Material A"
                        : "Path Tracing/Shared Geometry Material B");
            }
            sharedNormalAlpha = CreatePathTexture(
                m_Device, sharedWidth, sharedHeight,
                nvrhi::Format::RGBA16_FLOAT,
                "Path Tracing/Shared Normal Alpha");
            sharedDiffuse = CreatePathTexture(
                m_Device, sharedWidth, sharedHeight,
                nvrhi::Format::RGBA16_FLOAT,
                "Path Tracing/Shared Diffuse");
            sharedSpecular = CreatePathTexture(
                m_Device, sharedWidth, sharedHeight,
                nvrhi::Format::RGBA16_FLOAT,
                "Path Tracing/Shared Specular");
            pathMotion = CreatePathTexture(
                m_Device, sharedWidth, sharedHeight,
                nvrhi::Format::RGBA16_FLOAT,
                "Path Tracing/Motion Vectors");
            pathDepth = CreatePathTexture(
                m_Device, sharedWidth, sharedHeight,
                nvrhi::Format::R32_FLOAT,
                "Path Tracing/Device Depth");
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
        std::array<nvrhi::TextureHandle, 2> giLo = m_GiLo;
        std::array<nvrhi::TextureHandle, 2> giNormal = m_GiNormal;
        std::array<nvrhi::TextureHandle, 2> giReceiver = m_GiReceiver;
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
                    1u,
                    1u,
                    nvrhi::Format::R32_UINT,
                    index == 0u
                        ? "Path Tracing/GI Local Checkpoint Count A"
                        : "Path Tracing/GI Local Checkpoint Count B");
                giLo[index] = CreatePathTexture(
                    m_Device,
                    giHistoryWidth,
                    giHistoryHeight,
                    nvrhi::Format::RGBA16_FLOAT,
                    index == 0u
                        ? "Path Tracing/GI Tail Radiance A"
                        : "Path Tracing/GI Tail Radiance B");
                giNormal[index] = CreatePathTexture(
                    m_Device,
                    giHistoryWidth,
                    giHistoryHeight,
                    nvrhi::Format::RGBA16_FLOAT,
                    index == 0u
                        ? "Path Tracing/GI Secondary Normal A"
                        : "Path Tracing/GI Secondary Normal B");
                giReceiver[index] = CreatePathTexture(
                    m_Device,
                    giHistoryWidth,
                    giHistoryHeight,
                    nvrhi::Format::RGBA32_FLOAT,
                    index == 0u
                        ? "Path Tracing/GI Receiver A"
                        : "Path Tracing/GI Receiver B");
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
            display && directMean && directSampleCount && indirectMean &&
            sharedPositionHit && sharedGeometryMaterial[0] &&
            sharedGeometryMaterial[1] &&
            sharedNormalAlpha && sharedDiffuse && sharedSpecular &&
            pathMotion && pathDepth && residualMean && diffuseSuffixMean &&
            primaryNormalRoughness && primaryViewZ &&
            directReservoirs[0] && directReservoirs[1] &&
            surfaceHistory[0] && surfaceHistory[1] &&
            directSampleSeeds[0] && directSampleSeeds[1] &&
            giCheckpointReservoirs[0] && giCheckpointReservoirs[1] &&
            giCheckpointCounts[0] && giCheckpointCounts[1] &&
            giLo[0] && giLo[1] && giNormal[0] && giNormal[1] &&
            giReceiver[0] && giReceiver[1] &&
            pathSeedReservoirs[0] && pathSeedReservoirs[1] &&
            pathSeedStatistics[0] && pathSeedStatistics[1];
        if (!allCreated)
            return false;

        ClearBindingSets();
        m_RawMean = rawMean;
        m_SuccessfulSampleCount = successfulSampleCount;
        m_ColorVariance = colorVariance;
        m_Display = display;
        m_DirectMean = directMean;
        m_DirectSampleCount = directSampleCount;
        m_IndirectMean = indirectMean;
        m_SharedPositionHit = sharedPositionHit;
        m_SharedGeometryMaterial = sharedGeometryMaterial;
        m_SharedNormalAlpha = sharedNormalAlpha;
        m_SharedDiffuse = sharedDiffuse;
        m_SharedSpecular = sharedSpecular;
        m_PathMotion = pathMotion;
        m_PathDepth = pathDepth;
        m_ResidualMean = residualMean;
        m_DiffuseSuffixMean = diffuseSuffixMean;
        m_PrimaryNormalRoughness = primaryNormalRoughness;
        m_PrimaryViewZ = primaryViewZ;
        m_DirectReservoirs = directReservoirs;
        m_SurfaceHistory = surfaceHistory;
        m_DirectSampleSeeds = directSampleSeeds;
        m_GiCheckpointReservoirs = giCheckpointReservoirs;
        m_GiCheckpointCounts = giCheckpointCounts;
        m_GiLo = giLo;
        m_GiNormal = giNormal;
        m_GiReceiver = giReceiver;
        m_PathSeedReservoirs = pathSeedReservoirs;
        m_PathSeedStatistics = pathSeedStatistics;
        m_Width = width;
        m_Height = height;
        m_DirectReuseResourcesFullResolution = directReuseRequired;
        m_GiReuseResourcesFullResolution = giReuseRequired;
        m_PathReuseResourcesFullResolution = pathReuseRequired;
        m_StableSignalResourcesFullResolution = stableSignalsRequired;
        m_SharedPrimaryResourcesFullResolution = sharedPrimaryRequired;
        m_HistoryIndex = 0u;
        m_PrimarySurfaceIndex = 0u;
        m_DirectReservoirHistoryValid = false;
        m_GiCheckpointHistoryValid = false;
        m_PathSeedHistoryValid = false;
        m_PrimarySurfaceHistoryValid = false;
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
        uint32_t historyIndex,
        uint32_t primarySurfaceIndex)
    {
        if (!inputs.worldTlas || !inputs.materialVisibility ||
            !inputs.materialVisibility.instanceBuffer ||
            !inputs.environment || !inputs.noiseTexture ||
            !m_LightBuffer || historyIndex >= 2u ||
            primarySurfaceIndex >= 2u)
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
        const uint32_t bindingIndex =
            historyIndex * 2u + primarySurfaceIndex;
        if (m_BindingSets[bindingIndex])
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
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                14, inputs.materialVisibility.instanceBuffer),
            nvrhi::BindingSetItem::Texture_SRV(
                15, m_SharedPositionHit),
            nvrhi::BindingSetItem::Texture_SRV(
                16, m_SharedGeometryMaterial[primarySurfaceIndex]),
            nvrhi::BindingSetItem::Texture_SRV(
                17, m_SharedNormalAlpha),
            nvrhi::BindingSetItem::Texture_SRV(
                18, m_SharedDiffuse),
            nvrhi::BindingSetItem::Texture_SRV(
                19, m_SharedSpecular),
            nvrhi::BindingSetItem::Texture_SRV(
                20, m_DirectMean),
            nvrhi::BindingSetItem::Texture_SRV(
                21, m_GiLo[previousIndex]),
            nvrhi::BindingSetItem::Texture_SRV(
                22, m_GiNormal[previousIndex]),
            nvrhi::BindingSetItem::Texture_SRV(
                23, m_GiReceiver[previousIndex]),
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
                14, m_ColorVariance),
            nvrhi::BindingSetItem::Texture_UAV(15, m_IndirectMean),
            nvrhi::BindingSetItem::Texture_UAV(25, m_GiLo[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                26, m_GiNormal[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                27, m_GiReceiver[historyIndex])
        };
        m_BindingSets[bindingIndex] = m_Device->createBindingSet(
            description,
            m_BindingLayout);
        return bool(m_BindingSets[bindingIndex]);
    }

    bool PathTracingPass::EnsurePrimaryBindingSet(
        const PathTracingInputs& inputs,
        uint32_t historyIndex,
        uint32_t primarySurfaceIndex)
    {
        if (!inputs.worldTlas || !inputs.materialVisibility ||
            !inputs.materialVisibility.instanceBuffer ||
            !inputs.environment || !inputs.noiseTexture ||
            !m_LightBuffer || historyIndex >= 2u ||
            primarySurfaceIndex >= 2u)
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
        const uint32_t bindingIndex =
            historyIndex * 2u + primarySurfaceIndex;
        if (m_PrimaryBindingSets[bindingIndex])
            return true;

        const uint32_t previousIndex = historyIndex ^ 1u;
        const uint32_t previousPrimarySurfaceIndex =
            primarySurfaceIndex ^ 1u;
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
                9, m_DirectSampleSeeds[previousIndex]),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                10, inputs.materialVisibility.geometryBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                11, inputs.materialVisibility.materialBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                12, inputs.materialVisibility.geometryIndexMap),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                13, m_LightBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                14, inputs.materialVisibility.instanceBuffer),
            nvrhi::BindingSetItem::Texture_SRV(15, m_IndirectMean),
            nvrhi::BindingSetItem::Texture_SRV(
                24,
                m_SharedGeometryMaterial[previousPrimarySurfaceIndex]),
            nvrhi::BindingSetItem::Sampler(0, m_Sampler),
            nvrhi::BindingSetItem::Texture_UAV(0, m_RawMean),
            nvrhi::BindingSetItem::Texture_UAV(2, m_Display),
            nvrhi::BindingSetItem::Texture_UAV(
                3, m_DirectReservoirs[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                4, m_SurfaceHistory[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(9, m_ResidualMean),
            nvrhi::BindingSetItem::Texture_UAV(
                13, m_DirectSampleSeeds[historyIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                16, m_SharedPositionHit),
            nvrhi::BindingSetItem::Texture_UAV(
                17, m_SharedGeometryMaterial[primarySurfaceIndex]),
            nvrhi::BindingSetItem::Texture_UAV(
                18, m_SharedNormalAlpha),
            nvrhi::BindingSetItem::Texture_UAV(
                19, m_SharedDiffuse),
            nvrhi::BindingSetItem::Texture_UAV(
                20, m_SharedSpecular),
            nvrhi::BindingSetItem::Texture_UAV(21, m_DirectMean),
            nvrhi::BindingSetItem::Texture_UAV(
                22, m_DirectSampleCount),
            nvrhi::BindingSetItem::Texture_UAV(23, m_PathMotion),
            nvrhi::BindingSetItem::Texture_UAV(24, m_PathDepth)
        };
        m_PrimaryBindingSets[bindingIndex] = m_Device->createBindingSet(
            description,
            m_PrimaryBindingLayout);
        return bool(m_PrimaryBindingSets[bindingIndex]);
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
            m_DirectMean, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureUInt(
            m_DirectSampleCount, nvrhi::AllSubresources, 0u);
        commandList->clearTextureFloat(
            m_IndirectMean, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_SharedPositionHit,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_SharedNormalAlpha,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_SharedDiffuse, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_SharedSpecular, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_PathMotion, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            m_PathDepth, nvrhi::AllSubresources, nvrhi::Color(0.f));
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
            if (!preserveRevalidatedProposals)
            {
                commandList->clearTextureFloat(
                    m_GiCheckpointReservoirs[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
                commandList->clearTextureUInt(
                    m_GiCheckpointCounts[index],
                    nvrhi::AllSubresources,
                    0u);
                commandList->clearTextureFloat(
                    m_GiLo[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
                commandList->clearTextureFloat(
                    m_GiNormal[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
                commandList->clearTextureFloat(
                    m_GiReceiver[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
            }
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
        if (!preserveRevalidatedProposals)
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
        // Any failed or skipped render breaks the immediate-frame primary
        // signature chain. Capture it at entry, then leave validity false
        // until a new full-resolution primary dispatch is recorded.
        const bool previousPrimarySurfaceHistoryValid =
            m_PrimarySurfaceHistoryValid;
        m_PrimarySurfaceHistoryValid = false;
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
        if (!m_Capabilities.sharedPrimarySurfaceSupported)
            inputs.settings.sharedPrimarySurface = false;
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
        const bool directReuseRequired =
            UsesDirectReservoirHistory(inputs.settings) &&
            m_Capabilities.temporalReservoirReuseSupported;
        const bool giReuseRequired =
            UsesGiCheckpointHistory(inputs.settings) &&
            ((inputs.settings.temporalReuse &&
                m_Capabilities.temporalGiCheckpointReuseSupported) ||
                (inputs.settings.spatialNeighborCount > 0u &&
                    m_Capabilities.spatialGiCheckpointReuseSupported));
        const bool pathReuseRequired =
            UsesPathSeedHistory(inputs.settings) &&
            m_Capabilities.continuationSeedReservoirSupported &&
            m_Capabilities.replayablePathSeedSupported;
        const bool stableSignalsRequested =
            m_Capabilities.CanUseSpatialPathResolve(requestedSettings) &&
            CanUseSpatialPathResolve(
                inputs.settings,
                m_Capabilities.stablePlaneSignalSupported &&
                    m_Capabilities.stablePlaneResolveSupported);
        bool stableSignalsRequired = stableSignalsRequested;
        bool sharedPrimaryRequired =
            inputs.settings.sharedPrimarySurface &&
            m_Capabilities.sharedPrimarySurfaceSupported;
        const bool validInputs = inputs.view && inputs.worldTlas &&
            bool(inputs.materialVisibility) &&
            inputs.materialVisibility.instanceBuffer &&
            validEnvironment && validNoise &&
            IsValidNoiseSettings(inputs.noiseSettings) && validScalars;
        bool resourcesReady = validInputs && EnsureResources(
            inputs.width,
            inputs.height,
            directReuseRequired,
            giReuseRequired,
            pathReuseRequired,
            stableSignalsRequired,
            sharedPrimaryRequired);
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
                false,
                sharedPrimaryRequired);
        }
        if (!resourcesReady && validInputs && sharedPrimaryRequired)
        {
            // Shared Primary is a large optional full-resolution topology.
            // Keep path transport interactive under allocation pressure by
            // retrying the established all-ray integrator with safe 1x1
            // inactive shared bindings instead of falling back to raster.
            sharedPrimaryRequired = false;
            inputs.settings.sharedPrimarySurface = false;
            m_Capabilities.sharedPrimarySurfaceSupported = false;
            log::warning(
                "Shared Primary Surface allocation failed; continuing with the all-ray path integrator until the path pass is recreated");
            resourcesReady = EnsureResources(
                inputs.width,
                inputs.height,
                directReuseRequired,
                giReuseRequired,
                pathReuseRequired,
                stableSignalsRequired,
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
        constants.instanceCount = instanceCount;
        inputs.view->FillPlanarViewConstants(constants.view);
        // The fallback makes every matrix lane deterministic even when no
        // prior view exists. Only the explicit validity word enables shader
        // reprojection; the current view is never mistaken for history.
        constants.previousView = constants.view;
        constants.previousViewValid = 0u;
        if (inputs.previousView)
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
            GetAutomaticRussianRouletteStart(inputs.settings);
        constants.neeCandidateCount = inputs.settings.neeCandidateCount;
        constants.samplesPerPixel = inputs.settings.samplesPerPixel;
        constants.spatialNeighborCount =
            inputs.settings.spatialNeighborCount;
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
        if (inputs.settings.temporalReuse)
            constants.flags |= UVSR_PATH_TRACING_FLAG_TEMPORAL_REUSE;
        if (stableSignalsRequired)
        {
            constants.flags |=
                UVSR_PATH_TRACING_FLAG_WRITE_STABLE_SIGNALS;
        }
        if (inputs.settings.enableFireflyFilter)
            constants.flags |= UVSR_PATH_TRACING_FLAG_FILTER_FIREFLIES;
        if (sharedPrimaryRequired)
        {
            constants.flags |=
                UVSR_PATH_TRACING_FLAG_SHARED_PRIMARY_SURFACE;
        }
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
        // Primary signatures are immediate-frame geometry history, not
        // radiance or reservoir history. Keep them across transport-domain
        // resets (including animated-instance revisions); per-pixel material,
        // normal, bounds, and TAA depth checks reject incompatible donors.
        // Resource/topology changes, explicit pass resets, and render gaps
        // already invalidate m_PrimarySurfaceHistoryValid independently.
        const bool primarySurfaceHistoryAvailable =
            sharedPrimaryRequired &&
            previousPrimarySurfaceHistoryValid &&
            constants.previousViewValid != 0u;
        if (primarySurfaceHistoryAvailable)
        {
            constants.flags |=
                UVSR_PATH_TRACING_FLAG_PRIMARY_SIGNATURE_HISTORY;
        }
        const bool motionProposalHistoryEligible =
            historyReset &&
            inputs.historyResetByViewOnly &&
            inputs.previousView != nullptr &&
            inputs.settings.reuseRevalidatedProposalsDuringMotion &&
            !m_ResetRequested &&
            m_HistoryValid &&
            (m_DirectReservoirHistoryValid ||
                m_PathSeedHistoryValid ||
                m_GiCheckpointHistoryValid);
        PathTracingDispatchSchedule dispatchSchedule =
            BuildPathTracingDispatchSchedule(
                inputs.width,
                inputs.height,
                inputs.settings,
                constants.lightCount,
                !historyReset && m_DirectReservoirHistoryValid,
                !historyReset && m_PathSeedHistoryValid,
                !historyReset && m_GiCheckpointHistoryValid,
                stableSignalsRequired,
                historyReset ? 0u : m_ProgressivePhase);
        bool preserveRevalidatedProposals = false;
        if (motionProposalHistoryEligible &&
            dispatchSchedule.valid &&
            dispatchSchedule.phaseCount == 1u)
        {
            // Re-evaluate the schedule with replay cost. If reuse itself would
            // force sparse tiles, prefer a complete current frame and clear the
            // proposals rather than presenting coarse motion or associating an
            // older reservoir frame with the immediately previous view.
            const PathTracingDispatchSchedule reuseSchedule =
                BuildPathTracingDispatchSchedule(
                    inputs.width,
                    inputs.height,
                    inputs.settings,
                    constants.lightCount,
                    m_DirectReservoirHistoryValid,
                    m_PathSeedHistoryValid,
                    m_GiCheckpointHistoryValid,
                    stableSignalsRequired,
                    0u);
            if (reuseSchedule.valid && reuseSchedule.phaseCount == 1u)
            {
                dispatchSchedule = reuseSchedule;
                preserveRevalidatedProposals = true;
            }
        }
        const uint64_t sharedPrimaryFrameWork = SaturatingMultiply(
            SaturatingMultiply(inputs.width, inputs.height),
            EstimateSharedPrimaryWorkUnitsPerPixel(
                inputs.settings,
                constants.lightCount));
        if (!dispatchSchedule.valid ||
            sharedPrimaryFrameWork > MaxPathTracingWorkUnitsPerDispatch)
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
        const bool previousDirectHistoryAvailable =
            m_DirectReservoirHistoryValid &&
            (!historyReset || preserveRevalidatedProposals);
        const bool previousPathHistoryAvailable =
            m_PathSeedHistoryValid &&
            (!historyReset || preserveRevalidatedProposals);
        const bool previousGiHistoryAvailable =
            m_GiCheckpointHistoryValid &&
            (!historyReset || preserveRevalidatedProposals);
        // Previous-view matrices are always available to the shared-primary
        // motion-vector path. Proposal reprojection is a separate, narrower
        // contract used only while carrying validated history across an
        // authorized camera-only reset. Stationary adaptive skips must retain
        // their same-pixel proposal identities.
        constants.proposalReprojectionValid =
            constants.previousViewValid != 0u &&
            preserveRevalidatedProposals
                ? 1u
                : 0u;
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
        const uint32_t primarySurfaceIndex = m_PrimarySurfaceIndex;
        const uint32_t bindingIndex =
            outputHistoryIndex * 2u + primarySurfaceIndex;
        const uint32_t primaryPipelineVariant =
            (inputs.settings.useRtxdi ? PathTracingNeeModeCount : 0u) +
            static_cast<uint32_t>(inputs.settings.neeMode);
        const bool primaryReady = !sharedPrimaryRequired ||
            (primaryPipelineVariant < m_PrimaryPipelines.size() &&
                m_PrimaryPipelines[primaryPipelineVariant] &&
                EnsurePrimaryBindingSet(
                    inputs,
                    outputHistoryIndex,
                    primarySurfaceIndex));
        if (!EnsureBindingSet(
                inputs,
                outputHistoryIndex,
                primarySurfaceIndex) ||
            !primaryReady)
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
        if (sharedPrimaryRequired)
        {
            nvrhi::ComputeState primaryState;
            primaryState.pipeline =
                m_PrimaryPipelines[primaryPipelineVariant];
            primaryState.bindings = {
                m_PrimaryBindingSets[bindingIndex],
                inputs.materialVisibility.descriptorTable
            };
            commandList->setComputeState(primaryState);
            commandList->dispatch(
                div_ceil(inputs.width, 8u),
                div_ceil(inputs.height, 8u));

            // The primary pass owns current receiver/direct UAVs; publish all
            // of them before indirect transport consumes them as SRVs.
            nvrhi::utils::TextureUavBarrier(
                commandList, m_SharedPositionHit);
            nvrhi::utils::TextureUavBarrier(
                commandList,
                m_SharedGeometryMaterial[primarySurfaceIndex]);
            nvrhi::utils::TextureUavBarrier(
                commandList, m_SharedNormalAlpha);
            nvrhi::utils::TextureUavBarrier(
                commandList, m_SharedDiffuse);
            nvrhi::utils::TextureUavBarrier(
                commandList, m_SharedSpecular);
            nvrhi::utils::TextureUavBarrier(commandList, m_DirectMean);
            nvrhi::utils::TextureUavBarrier(commandList, m_RawMean);
            if (stableSignalsRequired)
            {
                nvrhi::utils::TextureUavBarrier(
                    commandList,
                    m_ResidualMean);
            }
            nvrhi::utils::TextureUavBarrier(commandList, m_Display);
            commandList->commitBarriers();
        }
        nvrhi::ComputeState state;
        state.pipeline = m_Pipelines[pipelineVariant];
        state.bindings = {
            m_BindingSets[bindingIndex],
            inputs.materialVisibility.descriptorTable
        };
        commandList->setComputeState(state);
        commandList->dispatch(
            div_ceil(dispatchSchedule.workExtent.x, 8u),
            div_ceil(dispatchSchedule.workExtent.y, 8u));

        if (!sharedPrimaryRequired && historyReset &&
            dispatchSchedule.phaseCount > 1u)
        {
            // Publish the newly traced phase before a cheap full-resolution
            // pass smoothly reconstructs the phase-zero representatives.
            // This initializes presentation only; raw means/counts and every
            // proposal history remain reset until their pixels are traced.
            nvrhi::utils::TextureUavBarrier(commandList, m_Display);
            commandList->commitBarriers();
            constants.flags |=
                UVSR_PATH_TRACING_FLAG_RECONSTRUCT_PREVIEW;
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
        if (sharedPrimaryRequired)
        {
            m_PrimarySurfaceIndex ^= 1u;
            m_PrimarySurfaceHistoryValid = true;
        }
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
        result.directMean = sharedPrimaryRequired
            ? m_DirectMean.Get()
            : nullptr;
        result.indirectMean = sharedPrimaryRequired
            ? m_IndirectMean.Get()
            : nullptr;
        result.temporalDepth = sharedPrimaryRequired
            ? m_PathDepth.Get()
            : nullptr;
        result.motionVectors = sharedPrimaryRequired
            ? m_PathMotion.Get()
            : nullptr;
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
        result.estimatedWorkUnitsPerPixel = SaturatingAdd(
            dispatchSchedule.estimatedWorkUnitsPerPixel,
            EstimateSharedPrimaryWorkUnitsPerPixel(
                inputs.settings,
                constants.lightCount));
        result.dispatchPhaseCount = dispatchSchedule.phaseCount;
        result.signalEpoch = coherentSignalsAvailable
            ? m_SignalEpoch
            : 0u;
        result.dispatched = true;
        result.historyReset = historyReset;
        result.completedSignalCycle = coherentSignalsAvailable;
        result.directReservoirActive = directReservoirActive;
        result.sharedPrimarySurfaceActive = sharedPrimaryRequired;
        result.sharedPrimarySurfaceRequestedButUnavailable =
            requestedSettings.sharedPrimarySurface &&
            !sharedPrimaryRequired;
        result.temporalReuseActive = inputs.settings.temporalReuse &&
            ((reuseRequested && previousDirectHistoryAvailable) ||
                (giReuseRequired && previousGiHistoryAvailable) ||
                (pathReuseRequired && previousPathHistoryAvailable));
        result.spatialReuseActive =
            inputs.settings.spatialNeighborCount > 0u &&
            m_Capabilities.previousFrameSpatialReuseSupported &&
            ((reuseRequested && previousDirectHistoryAvailable) ||
                (pathReuseRequired && previousPathHistoryAvailable) ||
                (giReuseRequired && previousGiHistoryAvailable));
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
            UsesPathSeedHistory(requestedSettings) && !pathReuseRequired;
        result.giReuseRequestedButUnavailable =
            UsesGiCheckpointHistory(requestedSettings) && !giReuseRequired;
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
            requestedSettings.solver == PathTracingSolver::RestirGi &&
            requestedSettings.spatialNeighborCount > 0u &&
            !m_Capabilities.spatialGiCheckpointReuseSupported;
        result.pipelineFallbackActive = pipelineFallbackActive;
        result.rawMeanBiasedByFireflyFilter =
            inputs.settings.enableFireflyFilter;
        return result;
    }

    void PathTracingPass::ResetHistory()
    {
        m_ResetRequested = true;
        m_PrimarySurfaceHistoryValid = false;
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
        for (nvrhi::BindingSetHandle& bindingSet : m_PrimaryBindingSets)
            bindingSet = nullptr;
    }
}
