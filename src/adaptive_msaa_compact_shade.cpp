#include "adaptive_msaa_compact_shade.h"
#include "adaptive_msaa_compact_raster.h"

// Implementation provenance:
// - Resource flow, visibility-only raster ownership, and the depth-classified
//   simple/complex architecture evaluated by this work are adapted from The
//   Forge Visibility_Buffer sample at commit
//   a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0). UVSR's retained
//   compact shader uses exact-key, wave-local invariant-work redistribution;
//   it does not claim to retain The Forge's transient primitive work lists.
// - Exact-key grouping and sample-count weighting are informed by Burns and
//   Hunt's official Visibility Buffer supplemental implementation (BSD-style),
//   SHA-256 6C9A2410ED37270B6CB5F8FED34CE5A330A229ECD426C16E573646FBBE0C76E2.
// - UVSR's PBR material, lighting, shadow, and motion behavior remains native
//   to this repository and its pinned Donut dependency (MIT).
// No Donut source file is modified by this implementation.

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/BindingCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/View.h>
#include <donut/render/DeferredLightingPass.h>
#include <directx/d3d12.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include <donut/shaders/gbuffer_cb.h>
#include "pbr_deferred_lighting_cb.h"

namespace
{
    using namespace uvsr::adaptive_msaa;

    constexpr uint64_t InstanceDataStride = 7u * 16u;
    constexpr uint64_t MaterialConstantsStride = 13u * 16u;
    constexpr uint64_t TriangleIndexStride = sizeof(uint32_t) * 3u;
    constexpr uint64_t PositionStride = sizeof(float) * 3u;
    constexpr uint64_t TexCoordStride = sizeof(float) * 2u;
    constexpr uint64_t PackedDirectionStride = sizeof(uint32_t);

    constexpr uint32_t CompactBindingDepth = 8u;
    constexpr uint32_t CompactBindingResolvedBackground = 17u;
    constexpr uint32_t CompactBindingVisibilityBase = 18u;
    constexpr uint32_t CompactBindingVisibilityComposite = 19u;
    constexpr uint32_t CompactBindingDirectionalVisibility = 20u;
    constexpr uint32_t CompactBindingSurfaceOwnership = 21u;
    constexpr uint32_t CompactBindingGeometryInstances = 22u;
    constexpr uint32_t CompactBindingGeometry = 23u;
    constexpr uint32_t CompactBindingInstanceHandles = 24u;
    constexpr uint32_t CompactBindingDiagnosticsBackground = 25u;
    constexpr uint32_t CompactBindingVisibilityClass = 26u;
    constexpr uint32_t CompactBindingFallbackDiffuse = 27u;
    constexpr uint32_t CompactBindingFallbackMaterial = 28u;
    constexpr uint32_t CompactBindingFallbackNormals = 29u;
    constexpr uint32_t CompactBindingFallbackEmissive = 30u;
    constexpr uint32_t CompactBindingFallbackAmbientOcclusion = 31u;
    constexpr uint32_t CompactBindingFallbackMotionVectors = 32u;
    constexpr uint32_t CompactBindingConstants = 1u;
    constexpr uint32_t CompactBindingDiagnosticsConstants = 2u;

    constexpr std::array<Dx12StandardSamplePosition, 2u> Pattern2 = {{
        { 4, 4 }, { -4, -4 }
    }};
    constexpr std::array<Dx12StandardSamplePosition, 4u> Pattern4 = {{
        { -2, -6 }, { 6, -2 }, { -6, 2 }, { 2, 6 }
    }};
    constexpr std::array<Dx12StandardSamplePosition, 8u> Pattern8 = {{
        { 1, -3 }, { -1, 3 }, { 5, 1 }, { -3, -5 },
        { -5, 5 }, { -7, -1 }, { 3, 7 }, { 7, -7 }
    }};
    constexpr std::array<Dx12StandardSamplePosition, 16u> Pattern16 = {{
        { 1, 1 }, { -1, -3 }, { -3, 2 }, { 4, -1 },
        { -5, -2 }, { 2, 5 }, { 5, 3 }, { 3, -5 },
        { -2, 6 }, { 0, -7 }, { -4, -6 }, { -6, 4 },
        { -8, 0 }, { 7, -4 }, { 6, 7 }, { -7, -8 }
    }};

    bool TryAdd(uint64_t first, uint64_t second, uint64_t& result)
    {
        if (first > std::numeric_limits<uint64_t>::max() - second)
            return false;
        result = first + second;
        return true;
    }

    bool TryMultiply(uint64_t first, uint64_t second, uint64_t& result)
    {
        if (first != 0u &&
            second > std::numeric_limits<uint64_t>::max() / first)
        {
            return false;
        }
        result = first * second;
        return true;
    }

    bool IsFinite(float value)
    {
        return std::isfinite(value);
    }

    bool IsFinite(CompactShadeFloat2 value)
    {
        return IsFinite(value.x) && IsFinite(value.y);
    }

    int64_t RoundToNearestEven(float value)
    {
        const double integralFloor = std::floor(double(value));
        const double fraction = double(value) - integralFloor;
        if (fraction < 0.5)
            return static_cast<int64_t>(integralFloor);
        if (fraction > 0.5)
            return static_cast<int64_t>(integralFloor + 1.0);

        const int64_t lower = static_cast<int64_t>(integralFloor);
        return (lower & 1ll) == 0ll ? lower : lower + 1ll;
    }

    int64_t OrientRaster(
        CompactRasterPosition first,
        CompactRasterPosition second,
        CompactRasterPosition samplePoint)
    {
        return (int64_t(second.x) - int64_t(first.x)) *
                (int64_t(samplePoint.y) - int64_t(first.y)) -
            (int64_t(second.y) - int64_t(first.y)) *
                (int64_t(samplePoint.x) - int64_t(first.x));
    }

    bool RasterEdgeIncludesSample(
        CompactRasterPosition first,
        CompactRasterPosition second,
        CompactRasterPosition samplePoint,
        int32_t windingSign)
    {
        const int64_t rawEdge = OrientRaster(
            first, second, samplePoint);
        const int64_t normalizedEdge = windingSign > 0
            ? rawEdge
            : -rawEdge;
        const int64_t dx = int64_t(windingSign) *
            (int64_t(second.x) - int64_t(first.x));
        const int64_t dy = int64_t(windingSign) *
            (int64_t(second.y) - int64_t(first.y));
        const bool inclusive = dy < 0 || (dy == 0 && dx > 0);
        return normalizedEdge > 0 ||
            (normalizedEdge == 0 && inclusive);
    }

    CompactShadeFloat2 Add(
        CompactShadeFloat2 first,
        CompactShadeFloat2 second)
    {
        return { first.x + second.x, first.y + second.y };
    }

    CompactShadeFloat2 Multiply(
        CompactShadeFloat2 value,
        float scale)
    {
        return { value.x * scale, value.y * scale };
    }

    bool ValidateElementAddress(
        uint64_t base,
        uint64_t element,
        uint64_t stride,
        uint64_t bufferByteSize)
    {
        uint64_t elementOffset = 0u;
        uint64_t address = 0u;
        uint64_t end = 0u;
        return TryMultiply(element, stride, elementOffset) &&
            TryAdd(base, elementOffset, address) &&
            TryAdd(address, stride, end) &&
            end <= bufferByteSize &&
            end <= uint64_t(std::numeric_limits<uint32_t>::max()) + 1u;
    }

    int GetSampleVariant(uint32_t sampleCount)
    {
        switch (sampleCount)
        {
        case 2u: return 0;
        case 4u: return 1;
        case 8u: return 2;
        case 16u: return 3;
        default: return -1;
        }
    }

    bool HasExactTextureTopology(
        const nvrhi::TextureDesc& first,
        const nvrhi::TextureDesc& second)
    {
        return first.width == second.width &&
            first.height == second.height &&
            first.depth == second.depth &&
            first.arraySize == second.arraySize &&
            first.mipLevels == second.mipLevels &&
            first.sampleCount == second.sampleCount &&
            first.sampleQuality == second.sampleQuality &&
            first.dimension == second.dimension;
    }

    bool IsMultisampleSrv(
        nvrhi::ITexture* texture,
        uint32_t sampleCount)
    {
        if (!texture)
            return false;
        const nvrhi::TextureDesc& desc = texture->getDesc();
        return desc.width > 0u && desc.height > 0u &&
            desc.depth == 1u && desc.mipLevels == 1u &&
            desc.sampleCount == sampleCount &&
            desc.dimension == nvrhi::TextureDimension::Texture2DMS &&
            desc.isShaderResource;
    }

    bool IsVisibilityClassSrv(
        nvrhi::ITexture* texture,
        uint32_t sampleCount,
        const nvrhi::TextureDesc& surfaceKeyDesc)
    {
        if (!IsMultisampleSrv(texture, sampleCount))
            return false;

        const nvrhi::TextureDesc& desc = texture->getDesc();
        return (desc.format == nvrhi::Format::R8_UINT ||
                desc.format == nvrhi::Format::R32_UINT) &&
            HasExactTextureTopology(desc, surfaceKeyDesc);
    }

    bool ValidateFallbackGBufferInputs(
        const AdaptiveMsaaCompactFallbackGBufferInputs& inputs,
        uint32_t sampleCount,
        const nvrhi::TextureDesc& surfaceKeyDesc)
    {
        if (inputs.Empty())
            return true;
        if (!inputs.Complete())
            return false;

        const std::array<std::pair<nvrhi::ITexture*, nvrhi::Format>, 6u>
            contracts = {{
                { inputs.diffuse, nvrhi::Format::SRGBA8_UNORM },
                { inputs.material, nvrhi::Format::RGBA8_UNORM },
                { inputs.normals, nvrhi::Format::RGBA16_SNORM },
                { inputs.emissive, nvrhi::Format::RGBA16_FLOAT },
                { inputs.materialAmbientOcclusion,
                    nvrhi::Format::R8_UNORM },
                { inputs.motionVectors, nvrhi::Format::RGBA16_FLOAT }
            }};
        for (const auto& [texture, format] : contracts)
        {
            if (!IsMultisampleSrv(texture, sampleCount))
                return false;
            const nvrhi::TextureDesc& desc = texture->getDesc();
            if (desc.format != format ||
                !HasExactTextureTopology(desc, surfaceKeyDesc))
            {
                return false;
            }
        }
        for (size_t first = 0u; first < contracts.size(); ++first)
        {
            for (size_t second = first + 1u;
                second < contracts.size();
                ++second)
            {
                if (contracts[first].first == contracts[second].first)
                    return false;
            }
        }
        return true;
    }

    bool SameFallbackGBufferInputs(
        const AdaptiveMsaaCompactFallbackGBufferInputs& first,
        const AdaptiveMsaaCompactFallbackGBufferInputs& second)
    {
        return first.diffuse == second.diffuse &&
            first.material == second.material &&
            first.normals == second.normals &&
            first.emissive == second.emissive &&
            first.materialAmbientOcclusion ==
                second.materialAmbientOcclusion &&
            first.motionVectors == second.motionVectors;
    }

    bool FallbackGBufferContains(
        const AdaptiveMsaaCompactFallbackGBufferInputs& inputs,
        nvrhi::ITexture* texture)
    {
        return texture &&
            (inputs.diffuse == texture ||
                inputs.material == texture ||
                inputs.normals == texture ||
                inputs.emissive == texture ||
                inputs.materialAmbientOcclusion == texture ||
                inputs.motionVectors == texture);
    }

    nvrhi::ITexture* FallbackTextureOrDepth(
        nvrhi::ITexture* fallback,
        nvrhi::ITexture* depth)
    {
        return fallback ? fallback : depth;
    }

    bool IsSingleSampleTexture(
        nvrhi::ITexture* texture,
        nvrhi::Format format,
        bool requireUav,
        const nvrhi::TextureDesc& extent)
    {
        if (!texture)
            return false;
        const nvrhi::TextureDesc& desc = texture->getDesc();
        return desc.format == format &&
            desc.width == extent.width &&
            desc.height == extent.height &&
            desc.depth == extent.depth &&
            desc.arraySize == extent.arraySize &&
            desc.mipLevels == extent.mipLevels &&
            desc.sampleCount == 1u &&
            desc.dimension == nvrhi::TextureDimension::Texture2D &&
            desc.isShaderResource &&
            (!requireUav || desc.isUAV);
    }

    bool ValidateDiagnosticsProducer(
        const AdaptiveMsaaDiagnosticsProducerOutputs& producer,
        const AdaptiveMsaaCompactLightingInputs& inputs,
        uint32_t sampleCount)
    {
        constexpr uint32_t AllExecutionFlags =
            DiagnosticsProducerWriteSampleStatus |
            DiagnosticsProducerWritePixelMetadata |
            DiagnosticsProducerWriteSelectedSamples |
            DiagnosticsProducerValidateErrorBudget;
        constexpr uint32_t SupportedCapabilities =
            DiagnosticsCounterFallbackReasonsValid |
            DiagnosticsCounterStaleAndMappingValid |
            DiagnosticsCounterGroupHistogramValid |
            DiagnosticsCounterLightingValid |
            DiagnosticsCounterErrorBudgetValid |
            (CompactPreparationUsesSeparatedWorkLists
                ? DiagnosticsCounterWorkListValid
                : 0u);
        if (!producer.Requested() || !inputs.output ||
            !producer.sampleStatus || !producer.selectedSampleData ||
            !producer.pixelMetadata || !producer.multisampledBackground ||
            (producer.executionFlags & ~AllExecutionFlags) != 0u ||
            (producer.capabilityMask &
                ~SupportedCapabilities) != 0u)
        {
            return false;
        }
        const nvrhi::TextureDesc& output = inputs.output->getDesc();
        const nvrhi::TextureDesc& background =
            producer.multisampledBackground->getDesc();
        if (background.format != nvrhi::Format::RGBA16_FLOAT ||
            background.dimension != nvrhi::TextureDimension::Texture2DMS ||
            !background.isShaderResource ||
            background.sampleCount != sampleCount ||
            background.width != output.width ||
            background.height != output.height)
        {
            return false;
        }
        const nvrhi::TextureDesc& status = producer.sampleStatus->getDesc();
        if (status.format != nvrhi::Format::R8_UINT ||
            status.dimension != nvrhi::TextureDimension::Texture2DArray ||
            !status.isShaderResource || !status.isUAV ||
            ((producer.executionFlags &
                    DiagnosticsProducerWriteSampleStatus) != 0u &&
                (status.width != output.width ||
                    status.height != output.height ||
                    status.arraySize != sampleCount)))
        {
            return false;
        }
        const nvrhi::BufferDesc& selected =
            producer.selectedSampleData->getDesc();
        const nvrhi::BufferDesc& metadata =
            producer.pixelMetadata->getDesc();
        if (!selected.canHaveUAVs ||
            selected.structStride !=
                sizeof(DiagnosticsGpuSampleInputRecord) ||
            !metadata.canHaveUAVs ||
            metadata.structStride !=
                sizeof(DiagnosticsGpuPixelMetadataRecord))
        {
            return false;
        }
        if ((producer.executionFlags &
                DiagnosticsProducerWriteSelectedSamples) != 0u &&
            (selected.byteSize < uint64_t(sampleCount) *
                    sizeof(DiagnosticsGpuSampleInputRecord) ||
                producer.selectedPixelX >= output.width ||
                producer.selectedPixelY >= output.height))
        {
            return false;
        }
        if ((producer.executionFlags &
                DiagnosticsProducerWritePixelMetadata) != 0u &&
            metadata.byteSize < uint64_t(output.width) * output.height *
                sizeof(DiagnosticsGpuPixelMetadataRecord))
        {
            return false;
        }
        const uint32_t statusCapabilities =
            DiagnosticsCounterFallbackReasonsValid |
            DiagnosticsCounterStaleAndMappingValid;
        const uint32_t metadataCapabilities =
            DiagnosticsCounterGroupHistogramValid |
            DiagnosticsCounterLightingValid |
            DiagnosticsCounterErrorBudgetValid |
            (CompactPreparationUsesSeparatedWorkLists
                ? DiagnosticsCounterWorkListValid
                : 0u);
        return ((producer.capabilityMask & statusCapabilities) == 0u ||
                (producer.executionFlags &
                    DiagnosticsProducerWriteSampleStatus) != 0u) &&
            ((producer.capabilityMask & metadataCapabilities) == 0u ||
                (producer.executionFlags &
                    DiagnosticsProducerWritePixelMetadata) != 0u) &&
            ((producer.capabilityMask &
                    DiagnosticsCounterErrorBudgetValid) == 0u ||
                (producer.executionFlags &
                    DiagnosticsProducerValidateErrorBudget) != 0u);
    }

    bool HasCompatiblePlanarViews(
        const ICompositeView& currentView,
        const ICompositeView* previousView)
    {
        const uint32_t currentCount =
            currentView.GetNumChildViews(ViewType::PLANAR);
        if (currentCount == 0u)
            return false;
        if (previousView &&
            previousView->GetNumChildViews(ViewType::PLANAR) != currentCount)
        {
            return false;
        }

        for (uint32_t viewIndex = 0u;
            viewIndex < currentCount;
            ++viewIndex)
        {
            const IView* current = currentView.GetChildView(
                ViewType::PLANAR, viewIndex);
            const IView* previous = previousView
                ? previousView->GetChildView(ViewType::PLANAR, viewIndex)
                : current;
            if (!current || !previous ||
                current->GetViewExtent().width() <= 0 ||
                current->GetViewExtent().height() <= 0)
            {
                return false;
            }
        }
        return true;
    }

    bool IsSnapshotComplete(const CompactSceneSnapshot& snapshot)
    {
        const CompactSceneGpuConstants& constants = snapshot.GpuConstants();
        const CompactScenePartitionSummary& partition =
            snapshot.PartitionSummary();
        const bool reconstructionPayloadComplete =
            partition.reconstructGeometryCount == 0u
                ? constants.geometryRecordCount == 0u &&
                    constants.materialRecordCount == 0u
                : constants.geometryRecordCount != 0u &&
                    constants.materialRecordCount != 0u;
        return snapshot.Eligibility().Succeeded() &&
            HasExactCompactDescriptorPartition(constants) &&
            snapshot.RequiredCommandQueue() == CompactSceneCommandQueue &&
            snapshot.BindlessLayout() && snapshot.DescriptorTable() &&
            snapshot.GeometryInstanceBuffer() && snapshot.GeometryBuffer() &&
            snapshot.MaterialBuffer() && snapshot.InstanceHandleBuffer() &&
            snapshot.SceneInstanceBuffer() &&
            constants.geometryInstanceRecordCount != 0u &&
            partition.identityGeometryCount != 0u &&
            reconstructionPayloadComplete &&
            constants.instanceHandleCount != 0u &&
            constants.descriptorCount != 0u &&
            IsCompactRawBufferDescriptor(
                constants, constants.instanceBufferDescriptor) &&
            IsCompactRawBufferDescriptor(
                constants, constants.materialBufferDescriptor);
    }

    struct alignas(16) CompactShadeConstants
    {
        GBufferFillConstants gbuffer;
        CompactSceneGpuConstants scene;
        uint32_t snapshotBuildSerialLow = 0u;
        uint32_t snapshotBuildSerialHigh = 0u;
        uint32_t sampleCount = 0u;
        uint32_t frontCounterClockwise = 0u;
        uint32_t preparationQueueCapacity =
            CompactPreparationQueueCapacity;
        uint32_t reserved0 = 0u;
        uint32_t reserved1 = 0u;
        uint32_t reserved2 = 0u;
    };

    static_assert(sizeof(GBufferFillConstants) % 16u == 0u);
    static_assert(sizeof(CompactSceneGpuConstants) == 64u);
    static_assert(sizeof(CompactShadeConstants) % 16u == 0u);
    static_assert(offsetof(CompactShadeConstants, scene) ==
        sizeof(GBufferFillConstants));

    uint32_t LowWord(uint64_t value)
    {
        return static_cast<uint32_t>(value & 0xffffffffull);
    }

    uint32_t HighWord(uint64_t value)
    {
        return static_cast<uint32_t>(value >> 32u);
    }

    struct ShadeProofInterval
    {
        double lower = 0.0;
        double upper = 0.0;
    };

    constexpr double Binary32UnitRoundoff = 0x1p-24;
    constexpr double Binary32MinimumNormal = 0x1p-126;
    constexpr double Binary32Maximum =
        double(std::numeric_limits<float>::max());

    double RoundDown(double value)
    {
        return std::nextafter(
            value, -std::numeric_limits<double>::infinity());
    }

    double RoundUp(double value)
    {
        return std::nextafter(
            value, std::numeric_limits<double>::infinity());
    }

    bool IsFiniteInterval(const ShadeProofInterval& interval)
    {
        return std::isfinite(interval.lower) &&
            std::isfinite(interval.upper) &&
            interval.lower <= interval.upper;
    }

    ShadeProofInterval AddIntervals(
        const ShadeProofInterval& first,
        const ShadeProofInterval& second)
    {
        return {
            RoundDown(first.lower + second.lower),
            RoundUp(first.upper + second.upper)
        };
    }

    ShadeProofInterval SubtractIntervals(
        const ShadeProofInterval& first,
        const ShadeProofInterval& second)
    {
        return {
            RoundDown(first.lower - second.upper),
            RoundUp(first.upper - second.lower)
        };
    }

    ShadeProofInterval MultiplyIntervals(
        const ShadeProofInterval& first,
        const ShadeProofInterval& second)
    {
        const std::array<double, 4u> products = {
            first.lower * second.lower,
            first.lower * second.upper,
            first.upper * second.lower,
            first.upper * second.upper
        };
        return {
            RoundDown(*std::min_element(
                products.begin(), products.end())),
            RoundUp(*std::max_element(
                products.begin(), products.end()))
        };
    }

    double MaximumAbsolute(const ShadeProofInterval& interval)
    {
        return std::max(
            std::abs(interval.lower), std::abs(interval.upper));
    }

    double MinimumAbsolute(const ShadeProofInterval& interval)
    {
        if (interval.lower <= 0.0 && interval.upper >= 0.0)
            return 0.0;
        return std::min(
            std::abs(interval.lower), std::abs(interval.upper));
    }

    ShadeProofInterval ExpandForFloatDot(
        ShadeProofInterval interval,
        double absoluteTermSum,
        uint32_t operationCount)
    {
        const double gamma =
            double(operationCount) * Binary32UnitRoundoff /
            (1.0 - double(operationCount) * Binary32UnitRoundoff);
        const double error = std::nextafter(
            gamma * absoluteTermSum + Binary32MinimumNormal,
            std::numeric_limits<double>::infinity());
        interval.lower = RoundDown(interval.lower - error);
        interval.upper = RoundUp(interval.upper + error);
        return interval;
    }

    ShadeProofInterval DivideIntervals(
        const ShadeProofInterval& numerator,
        const ShadeProofInterval& denominator)
    {
        if (MinimumAbsolute(denominator) <= Binary32MinimumNormal)
        {
            return {
                -std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()
            };
        }
        const ShadeProofInterval reciprocal = denominator.lower > 0.0
            ? ShadeProofInterval{
                RoundDown(1.0 / denominator.upper),
                RoundUp(1.0 / denominator.lower) }
            : ShadeProofInterval{
                RoundDown(1.0 / denominator.upper),
                RoundUp(1.0 / denominator.lower) };
        ShadeProofInterval result = MultiplyIntervals(
            numerator, reciprocal);
        const double magnitude = MaximumAbsolute(result);
        const double error = std::nextafter(
            Binary32UnitRoundoff * magnitude + Binary32MinimumNormal,
            std::numeric_limits<double>::infinity());
        result.lower = RoundDown(result.lower - error);
        result.upper = RoundUp(result.upper + error);
        return result;
    }

    std::array<ShadeProofInterval, 4u> MultiplyRowVectorInterval(
        const std::array<ShadeProofInterval, 4u>& value,
        const std::array<float, 16u>& matrix)
    {
        std::array<ShadeProofInterval, 4u> output{};
        for (uint32_t column = 0u; column < 4u; ++column)
        {
            ShadeProofInterval sum{};
            double absoluteTermSum = 0.0;
            for (uint32_t row = 0u; row < 4u; ++row)
            {
                const ShadeProofInterval coefficient = {
                    double(matrix[row * 4u + column]),
                    double(matrix[row * 4u + column])
                };
                const ShadeProofInterval product = MultiplyIntervals(
                    value[row], coefficient);
                sum = row == 0u ? product : AddIntervals(sum, product);
                absoluteTermSum += MaximumAbsolute(product);
            }
            output[column] = ExpandForFloatDot(
                sum, absoluteTermSum, 7u);
        }
        return output;
    }

    bool MatrixIsFinite(const std::array<float, 16u>& matrix)
    {
        return std::all_of(
            matrix.begin(), matrix.end(),
            [](float value) { return std::isfinite(value); });
    }

    bool MatricesFormStableInverse(
        const std::array<float, 16u>& first,
        const std::array<float, 16u>& second)
    {
        for (uint32_t row = 0u; row < 4u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                double product = 0.0;
                double absoluteTermSum = 0.0;
                for (uint32_t index = 0u; index < 4u; ++index)
                {
                    const double term =
                        double(first[row * 4u + index]) *
                        double(second[index * 4u + column]);
                    product += term;
                    absoluteTermSum += std::abs(term);
                }
                const double expected = row == column ? 1.0 : 0.0;
                const double tolerance = std::max(
                    1e-5,
                    4096.0 * Binary32UnitRoundoff *
                        std::max(1.0, absoluteTermSum));
                if (!std::isfinite(product) ||
                    std::abs(product - expected) > tolerance)
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool SameFloatBits(float first, float second)
    {
        return std::memcmp(&first, &second, sizeof(float)) == 0;
    }

    bool SamePlanarViewConstants(
        const PlanarViewConstants& first,
        const PlanarViewConstants& second)
    {
        static_assert(std::is_trivially_copyable_v<PlanarViewConstants>);
        return std::memcmp(
            &first, &second, sizeof(PlanarViewConstants)) == 0;
    }

    bool SameRect(const nvrhi::Rect& first, const nvrhi::Rect& second)
    {
        return first.minX == second.minX &&
            first.maxX == second.maxX &&
            first.minY == second.minY &&
            first.maxY == second.maxY;
    }

    bool SameAcceptedDepthProof(
        const CompactRasterAcceptedDepthInterval& first,
        const CompactRasterAcceptedDepthInterval& second)
    {
        return first.valid == second.valid &&
            first.hasGeometry == second.hasGeometry &&
            SameFloatBits(
                first.minimumStoredDeviceDepth,
                second.minimumStoredDeviceDepth) &&
            SameFloatBits(
                first.maximumStoredDeviceDepth,
                second.maximumStoredDeviceDepth) &&
            SameFloatBits(
                first.conservativeErrorMargin,
                second.conservativeErrorMargin);
    }

    bool SameRasterViewProof(
        const CompactRasterViewProof& first,
        const CompactRasterViewProof& second)
    {
        return first.sourceViewIndex == second.sourceViewIndex &&
            first.view == second.view &&
            first.surfaceKeyTarget == second.surfaceKeyTarget &&
            first.visibilityClassTarget == second.visibilityClassTarget &&
            first.depthTarget == second.depthTarget &&
            first.surfaceKeySubresources ==
                second.surfaceKeySubresources &&
            first.visibilityClassSubresources ==
                second.visibilityClassSubresources &&
            first.depthSubresources == second.depthSubresources &&
            first.surfaceKeyFormat == second.surfaceKeyFormat &&
            first.visibilityClassFormat ==
                second.visibilityClassFormat &&
            first.depthFormat == second.depthFormat &&
            first.sampleCount == second.sampleCount &&
            first.sampleQuality == second.sampleQuality &&
            SameCompactRasterViewportState(
                first.viewport, second.viewport) &&
            SameCompactRasterViewTransformIdentity(
                first.transformIdentity, second.transformIdentity) &&
            SameAcceptedDepthProof(
                first.acceptedDepth, second.acceptedDepth) &&
            first.fullGBufferFallbackPresent ==
                second.fullGBufferFallbackPresent &&
            first.fallbackMayStoreZeroDepth ==
                second.fallbackMayStoreZeroDepth &&
            first.fallbackGBufferTargets ==
                second.fallbackGBufferTargets &&
            first.fallbackGBufferSubresources ==
                second.fallbackGBufferSubresources &&
            first.fallbackMotionTarget == second.fallbackMotionTarget &&
            first.fallbackMotionSubresources ==
                second.fallbackMotionSubresources;
    }

    template<typename Matrix>
    std::array<float, 16u> CaptureMatrix(const Matrix& matrix)
    {
        std::array<float, 16u> result{};
        std::copy_n(matrix.m_data, result.size(), result.begin());
        return result;
    }

    CompactShadeViewReconstructionProofInput
        MakeViewReconstructionProofInput(
            const PlanarViewConstants& constants,
            const CompactRasterViewProof& rasterProof)
    {
        CompactShadeViewReconstructionProofInput result;
        result.worldToClip = CaptureMatrix(constants.matWorldToClip);
        result.clipToWorld = CaptureMatrix(constants.matClipToWorld);
        result.worldToView = CaptureMatrix(constants.matWorldToView);
        result.viewportOrigin = {
            constants.viewportOrigin.x,
            constants.viewportOrigin.y
        };
        result.viewportSize = {
            constants.viewportSize.x,
            constants.viewportSize.y
        };
        result.viewportSizeInv = {
            constants.viewportSizeInv.x,
            constants.viewportSizeInv.y
        };
        result.cameraDirectionOrPosition = {
            constants.cameraDirectionOrPosition.x,
            constants.cameraDirectionOrPosition.y,
            constants.cameraDirectionOrPosition.z,
            constants.cameraDirectionOrPosition.w
        };
        result.acceptedDepthValid = rasterProof.acceptedDepth.valid;
        result.acceptedDepthHasGeometry =
            rasterProof.acceptedDepth.hasGeometry;
        result.minimumStoredDeviceDepth =
            rasterProof.acceptedDepth.minimumStoredDeviceDepth;
        result.maximumStoredDeviceDepth =
            rasterProof.acceptedDepth.maximumStoredDeviceDepth;
        result.conservativeDepthErrorMargin =
            rasterProof.acceptedDepth.conservativeErrorMargin;
        return result;
    }

    CompactRasterViewTransformIdentity CaptureRasterIdentity(
        const PlanarViewConstants& constants)
    {
        CompactRasterViewTransformIdentity result;
        result.worldToClip = CaptureMatrix(constants.matWorldToClip);
        result.clipToWindowScale = {
            constants.clipToWindowScale.x,
            constants.clipToWindowScale.y
        };
        result.clipToWindowBias = {
            constants.clipToWindowBias.x,
            constants.clipToWindowBias.y
        };
        return result;
    }

    bool ViewExtentMatchesViewport(
        const nvrhi::Rect& extent,
        const nvrhi::ViewportState& viewportState,
        const PlanarViewConstants& constants)
    {
        if (viewportState.viewports.size() != 1u ||
            viewportState.scissorRects.size() != 1u ||
            !SameRect(extent, viewportState.scissorRects[0]))
        {
            return false;
        }
        const nvrhi::Viewport& viewport = viewportState.viewports[0];
        const nvrhi::Rect viewportRect(viewport);
        return SameRect(extent, viewportRect) &&
            extent.width() > 0 && extent.height() > 0 &&
            SameFloatBits(viewport.minX, float(extent.minX)) &&
            SameFloatBits(viewport.maxX, float(extent.maxX)) &&
            SameFloatBits(viewport.minY, float(extent.minY)) &&
            SameFloatBits(viewport.maxY, float(extent.maxY)) &&
            SameFloatBits(
                constants.viewportOrigin.x, viewport.minX) &&
            SameFloatBits(
                constants.viewportOrigin.y, viewport.minY) &&
            SameFloatBits(
                constants.viewportSize.x, viewport.width()) &&
            SameFloatBits(
                constants.viewportSize.y, viewport.height()) &&
            SameFloatBits(viewport.minZ, 0.f) &&
            SameFloatBits(viewport.maxZ, 1.f);
    }
}

namespace uvsr::adaptive_msaa
{
    CompactPreparationQueueDecision EvaluateCompactPreparationQueue(
        uint32_t configuredCapacity,
        uint32_t liveWaveLaneCount,
        uint32_t occupancy)
    {
        CompactPreparationQueueDecision result;
        result.occupancy = occupancy;
        if (configuredCapacity == 0u ||
            configuredCapacity > CompactPreparationQueueCapacity ||
            liveWaveLaneCount == 0u ||
            liveWaveLaneCount > CompactPreparationQueueCapacity ||
            occupancy > liveWaveLaneCount)
        {
            return result;
        }
        result.actualCapacity = std::min(
            configuredCapacity,
            liveWaveLaneCount);
        result.overflow = occupancy > result.actualCapacity;
        result.inlineFallback = result.overflow;
        result.reason = result.overflow
            ? FallbackReason::WorkListOverflow
            : FallbackReason::None;
        return result;
    }

    CompactRasterSnapResult SnapCompactRasterPosition(
        CompactShadeFloat2 windowPosition)
    {
        CompactRasterSnapResult result;
        if (!IsFinite(windowPosition))
            return result;
        if (windowPosition.x < -32768.f ||
            windowPosition.x > 32767.f ||
            windowPosition.y < -32768.f ||
            windowPosition.y > 32767.f)
        {
            return result;
        }

        const float scaledX = windowPosition.x *
            float(CompactRasterSubpixelScale);
        const float scaledY = windowPosition.y *
            float(CompactRasterSubpixelScale);
        if (!IsFinite(scaledX) || !IsFinite(scaledY))
            return result;

        const int64_t snappedX = RoundToNearestEven(scaledX);
        const int64_t snappedY = RoundToNearestEven(scaledY);
        if (snappedX < CompactRasterCoordinateMinimum ||
            snappedX > CompactRasterCoordinateMaximum ||
            snappedY < CompactRasterCoordinateMinimum ||
            snappedY > CompactRasterCoordinateMaximum)
        {
            return result;
        }

        result.rasterPosition = {
            static_cast<int32_t>(snappedX),
            static_cast<int32_t>(snappedY)
        };
        result.snappedWindowPosition = {
            float(result.rasterPosition.x) /
                float(CompactRasterSubpixelScale),
            float(result.rasterPosition.y) /
                float(CompactRasterSubpixelScale)
        };
        result.reason = FallbackReason::None;
        return result;
    }

    CompactRasterCoverageResult EvaluateCompactStandardRasterCoverage(
        const std::array<CompactShadeFloat2, 3u>& windowPositions,
        int32_t pixelX,
        int32_t pixelY,
        const Dx12StandardSamplePattern& samplePattern,
        bool mirroredView)
    {
        CompactRasterCoverageResult result;
        const bool supportedSampleCount =
            samplePattern.sampleCount == 2u ||
            samplePattern.sampleCount == 4u ||
            samplePattern.sampleCount == 8u ||
            samplePattern.sampleCount == 16u;
        if (!samplePattern.Succeeded() ||
            !supportedSampleCount)
        {
            result.reason = FallbackReason::SampleCountUnsupported;
            return result;
        }

        for (size_t vertex = 0u;
            vertex < windowPositions.size();
            ++vertex)
        {
            const CompactRasterSnapResult snapped =
                SnapCompactRasterPosition(windowPositions[vertex]);
            if (!snapped.Succeeded())
                return result;
            result.rasterPositions[vertex] = snapped.rasterPosition;
            result.snappedWindowPositions[vertex] =
                snapped.snappedWindowPosition;
        }

        result.signedArea = OrientRaster(
            result.rasterPositions[0],
            result.rasterPositions[1],
            result.rasterPositions[2]);
        if (result.signedArea == 0)
            return result;

        const int64_t pixelCenterX =
            int64_t(pixelX) * CompactRasterSubpixelScale +
            CompactRasterPixelCenter;
        const int64_t pixelCenterY =
            int64_t(pixelY) * CompactRasterSubpixelScale +
            CompactRasterPixelCenter;
        if (pixelCenterX < CompactRasterCoordinateMinimum ||
            pixelCenterX > CompactRasterCoordinateMaximum ||
            pixelCenterY < CompactRasterCoordinateMinimum ||
            pixelCenterY > CompactRasterCoordinateMaximum)
        {
            return result;
        }

        const int32_t windingSign = result.signedArea > 0 ? 1 : -1;
        for (uint32_t sampleIndex = 0u;
            sampleIndex < samplePattern.sampleCount;
            ++sampleIndex)
        {
            const Dx12StandardSamplePosition offset =
                samplePattern.positions[sampleIndex];
            if (offset.xSixteenths < -8 ||
                offset.xSixteenths > 7 ||
                offset.ySixteenths < -8 ||
                offset.ySixteenths > 7)
            {
                return result;
            }

            const int64_t sampleX = pixelCenterX +
                int64_t(offset.xSixteenths) * 16ll;
            const int64_t sampleY = pixelCenterY +
                int64_t(offset.ySixteenths) * 16ll;
            if (sampleX < std::numeric_limits<int32_t>::min() ||
                sampleX > std::numeric_limits<int32_t>::max() ||
                sampleY < std::numeric_limits<int32_t>::min() ||
                sampleY > std::numeric_limits<int32_t>::max())
            {
                return result;
            }
            const CompactRasterPosition samplePoint = {
                static_cast<int32_t>(sampleX),
                static_cast<int32_t>(sampleY)
            };
            if (RasterEdgeIncludesSample(
                    result.rasterPositions[0],
                    result.rasterPositions[1],
                    samplePoint,
                    windingSign) &&
                RasterEdgeIncludesSample(
                    result.rasterPositions[1],
                    result.rasterPositions[2],
                    samplePoint,
                    windingSign) &&
                RasterEdgeIncludesSample(
                    result.rasterPositions[2],
                    result.rasterPositions[0],
                    samplePoint,
                    windingSign))
            {
                result.coverageMask |= 1u << sampleIndex;
            }
        }

        result.frontFace = mirroredView
            ? result.signedArea < 0
            : result.signedArea > 0;
        result.reason = FallbackReason::None;
        return result;
    }

    CompactShadeFloat3 TransformCompactPosition(
        const CompactShadeAffineTransform& transform,
        CompactShadeFloat3 position)
    {
        CompactShadeFloat3 result;
        result.x = transform.rows[0][0] * position.x +
            transform.rows[0][1] * position.y +
            transform.rows[0][2] * position.z +
            transform.rows[0][3];
        result.y = transform.rows[1][0] * position.x +
            transform.rows[1][1] * position.y +
            transform.rows[1][2] * position.z +
            transform.rows[1][3];
        result.z = transform.rows[2][0] * position.x +
            transform.rows[2][1] * position.y +
            transform.rows[2][2] * position.z +
            transform.rows[2][3];
        return result;
    }

    CompactShadeFloat3 TransformCompactDirection(
        const CompactShadeAffineTransform& transform,
        CompactShadeFloat3 direction)
    {
        CompactShadeFloat3 result;
        result.x = transform.rows[0][0] * direction.x +
            transform.rows[0][1] * direction.y +
            transform.rows[0][2] * direction.z;
        result.y = transform.rows[1][0] * direction.x +
            transform.rows[1][1] * direction.y +
            transform.rows[1][2] * direction.z;
        result.z = transform.rows[2][0] * direction.x +
            transform.rows[2][1] * direction.y +
            transform.rows[2][2] * direction.z;
        return result;
    }

    bool DetermineCompactFrontFace(
        const std::array<CompactShadeFloat2, 3u>& windowPositions,
        bool mirroredView)
    {
        const float edge01X = windowPositions[1].x -
            windowPositions[0].x;
        const float edge01Y = windowPositions[1].y -
            windowPositions[0].y;
        const float edge02X = windowPositions[2].x -
            windowPositions[0].x;
        const float edge02Y = windowPositions[2].y -
            windowPositions[0].y;
        const float signedArea =
            edge01X * edge02Y - edge01Y * edge02X;
        if (!IsFinite(signedArea) || signedArea == 0.f)
            return false;
        return mirroredView ? signedArea < 0.f : signedArea > 0.f;
    }

    bool ShouldFlipCompactSurfaceNormals(
        bool doubleSided,
        bool frontFace,
        CompactShadeFloat3 geometryNormal,
        CompactShadeFloat3 viewDirection)
    {
        if (!doubleSided)
            return !frontFace;
        const float hemisphere =
            geometryNormal.x * viewDirection.x +
            geometryNormal.y * viewDirection.y +
            geometryNormal.z * viewDirection.z;
        return IsFinite(hemisphere) && hemisphere < 0.f;
    }

    CompactShadeFloat4 ReconstructCompactMotion(
        const CompactShadeMotionProjection& projection)
    {
        CompactShadeFloat4 result;
        const CompactShadeFloat4 previous =
            projection.previousClipPosition;
        if (!IsFinite(previous.x) || !IsFinite(previous.y) ||
            !IsFinite(previous.z) || !IsFinite(previous.w) ||
            !(previous.w > 0.f))
        {
            return result;
        }
        const float inverseW = 1.f / previous.w;
        const float previousWindowX = previous.x * inverseW *
            projection.clipToWindowScale.x +
            projection.clipToWindowBias.x;
        const float previousWindowY = previous.y * inverseW *
            projection.clipToWindowScale.y +
            projection.clipToWindowBias.y;
        result.x = previousWindowX -
            projection.currentWindowPosition.x +
            projection.currentPixelOffset.x -
            projection.previousPixelOffset.x;
        result.y = previousWindowY -
            projection.currentWindowPosition.y +
            projection.currentPixelOffset.y -
            projection.previousPixelOffset.y;
        result.z = previous.z * inverseW -
            projection.currentDeviceDepth;
        if (!IsFinite(result.x) || !IsFinite(result.y) ||
            !IsFinite(result.z))
        {
            return {};
        }
        result.w = 1.f;
        return result;
    }

    Dx12StandardSamplePattern GetDx12StandardSamplePattern(
        uint32_t sampleCount)
    {
        Dx12StandardSamplePattern result;
        result.sampleCount = sampleCount;
        const auto CopyPattern = [&result](const auto& pattern)
        {
            std::copy(pattern.begin(), pattern.end(),
                result.positions.begin());
        };

        switch (sampleCount)
        {
        case 2u: CopyPattern(Pattern2); break;
        case 4u: CopyPattern(Pattern4); break;
        case 8u: CopyPattern(Pattern8); break;
        case 16u: CopyPattern(Pattern16); break;
        default:
            result.sampleCount = 0u;
            result.reason = FallbackReason::SampleCountUnsupported;
            break;
        }
        return result;
    }

    CompactShadePerspectiveInterpolation
        ReconstructCompactPerspectiveInterpolation(
            const std::array<CompactShadeProjectedVertex, 3u>& triangle,
            CompactShadeFloat2 evaluationPosition)
    {
        CompactShadePerspectiveInterpolation result;
        if (!IsFinite(evaluationPosition))
            return result;
        for (const CompactShadeProjectedVertex& vertex : triangle)
        {
            if (!IsFinite(vertex.windowPosition) ||
                !IsFinite(vertex.reciprocalW) ||
                !IsFinite(vertex.deviceDepth) ||
                !IsFinite(vertex.texCoord) ||
                vertex.reciprocalW == 0.f)
            {
                return result;
            }
        }

        const float x0 = triangle[0].windowPosition.x;
        const float y0 = triangle[0].windowPosition.y;
        const float x1 = triangle[1].windowPosition.x;
        const float y1 = triangle[1].windowPosition.y;
        const float x2 = triangle[2].windowPosition.x;
        const float y2 = triangle[2].windowPosition.y;
        const float denominator =
            (y1 - y2) * (x0 - x2) +
            (x2 - x1) * (y0 - y2);
        if (!IsFinite(denominator) ||
            std::abs(denominator) <= 1e-12f)
        {
            return result;
        }

        const float inverseDenominator = 1.f / denominator;
        result.affineBarycentrics[0] =
            ((y1 - y2) * (evaluationPosition.x - x2) +
                (x2 - x1) * (evaluationPosition.y - y2)) *
            inverseDenominator;
        result.affineBarycentrics[1] =
            ((y2 - y0) * (evaluationPosition.x - x2) +
                (x0 - x2) * (evaluationPosition.y - y2)) *
            inverseDenominator;
        result.affineBarycentrics[2] = 1.f -
            result.affineBarycentrics[0] -
            result.affineBarycentrics[1];

        result.affineBarycentricDx = {
            (y1 - y2) * inverseDenominator,
            (y2 - y0) * inverseDenominator,
            (y0 - y1) * inverseDenominator
        };
        result.affineBarycentricDy = {
            (x2 - x1) * inverseDenominator,
            (x0 - x2) * inverseDenominator,
            (x1 - x0) * inverseDenominator
        };

        float reciprocalW = 0.f;
        CompactShadeFloat2 texCoordOverW{};
        for (size_t vertexIndex = 0u;
            vertexIndex < triangle.size();
            ++vertexIndex)
        {
            const float q = triangle[vertexIndex].reciprocalW;
            const float weightedQ =
                result.affineBarycentrics[vertexIndex] * q;
            reciprocalW += weightedQ;
            texCoordOverW = Add(
                texCoordOverW,
                Multiply(triangle[vertexIndex].texCoord, weightedQ));
            result.deviceDepth +=
                result.affineBarycentrics[vertexIndex] *
                triangle[vertexIndex].deviceDepth;
        }
        if (!IsFinite(reciprocalW) ||
            std::abs(reciprocalW) <= 1e-20f)
        {
            return result;
        }

        const float inverseReciprocalW = 1.f / reciprocalW;
        for (size_t vertexIndex = 0u;
            vertexIndex < triangle.size();
            ++vertexIndex)
        {
            result.perspectiveBarycentrics[vertexIndex] =
                result.affineBarycentrics[vertexIndex] *
                triangle[vertexIndex].reciprocalW *
                inverseReciprocalW;
        }
        result.texCoord = Multiply(
            texCoordOverW, inverseReciprocalW);

        const bool equalReciprocalW = SameFloatBits(
                triangle[0].reciprocalW,
                triangle[1].reciprocalW) &&
            SameFloatBits(
                triangle[0].reciprocalW,
                triangle[2].reciprocalW);
        if (equalReciprocalW)
        {
            // Equal clip W produces one affine UV plane. Edge-relative
            // evaluation preserves its one-pixel deltas when absolute UVs are
            // much larger, mirroring CompactGetAnalyticCoarseGradients.
            const CompactShadeFloat2 texCoordEdge0 = Add(
                triangle[0].texCoord,
                Multiply(triangle[2].texCoord, -1.f));
            const CompactShadeFloat2 texCoordEdge1 = Add(
                triangle[1].texCoord,
                Multiply(triangle[2].texCoord, -1.f));
            result.texCoordDx = Add(
                Multiply(
                    texCoordEdge0,
                    result.affineBarycentricDx[0]),
                Multiply(
                    texCoordEdge1,
                    result.affineBarycentricDx[1]));
            result.texCoordDy = Add(
                Multiply(
                    texCoordEdge0,
                    result.affineBarycentricDy[0]),
                Multiply(
                    texCoordEdge1,
                    result.affineBarycentricDy[1]));
        }
        else
        {
            // Texture.Sample in the unchanged pixel shader derives its
            // implicit LOD from one coarse finite-difference pair per 2x2
            // pixel stamp. A pointwise derivative of the rational perspective
            // plane is not the same quantity. Reconstruct the primitive's
            // helper-lane values at the stamp's top-left, top-right, and
            // bottom-left pixel centers.
            const auto interpolateTexCoord =
                [&](CompactShadeFloat2 position,
                    CompactShadeFloat2& texCoord) -> bool
                {
                    const std::array<float, 3u> affine = {
                        ((y1 - y2) * (position.x - x2) +
                            (x2 - x1) * (position.y - y2)) *
                            inverseDenominator,
                        ((y2 - y0) * (position.x - x2) +
                            (x0 - x2) * (position.y - y2)) *
                            inverseDenominator,
                        0.f
                    };
                    std::array<float, 3u> completeAffine = affine;
                    completeAffine[2] = 1.f - completeAffine[0] -
                        completeAffine[1];
                    float q = 0.f;
                    CompactShadeFloat2 uvOverW{};
                    for (size_t vertexIndex = 0u;
                        vertexIndex < triangle.size();
                        ++vertexIndex)
                    {
                        const float weightedQ =
                            completeAffine[vertexIndex] *
                            triangle[vertexIndex].reciprocalW;
                        q += weightedQ;
                        uvOverW = Add(
                            uvOverW,
                            Multiply(
                                triangle[vertexIndex].texCoord,
                                weightedQ));
                    }
                    if (!IsFinite(q) || std::abs(q) <= 1e-20f ||
                        !IsFinite(uvOverW))
                    {
                        return false;
                    }
                    texCoord = Multiply(uvOverW, 1.f / q);
                    return IsFinite(texCoord);
                };
            const float pixelX = std::floor(evaluationPosition.x);
            const float pixelY = std::floor(evaluationPosition.y);
            const CompactShadeFloat2 topLeft = {
                2.f * std::floor(pixelX * 0.5f) + 0.5f,
                2.f * std::floor(pixelY * 0.5f) + 0.5f
            };
            CompactShadeFloat2 topLeftTexCoord;
            CompactShadeFloat2 topRightTexCoord;
            CompactShadeFloat2 bottomLeftTexCoord;
            if (!interpolateTexCoord(topLeft, topLeftTexCoord) ||
                !interpolateTexCoord(
                    { topLeft.x + 1.f, topLeft.y },
                    topRightTexCoord) ||
                !interpolateTexCoord(
                    { topLeft.x, topLeft.y + 1.f },
                    bottomLeftTexCoord))
            {
                return {};
            }
            result.texCoordDx = Add(
                topRightTexCoord,
                Multiply(topLeftTexCoord, -1.f));
            result.texCoordDy = Add(
                bottomLeftTexCoord,
                Multiply(topLeftTexCoord, -1.f));
        }

        float affineSum = 0.f;
        float perspectiveSum = 0.f;
        for (size_t index = 0u; index < 3u; ++index)
        {
            affineSum += result.affineBarycentrics[index];
            perspectiveSum += result.perspectiveBarycentrics[index];
            if (!IsFinite(result.affineBarycentrics[index]) ||
                !IsFinite(result.perspectiveBarycentrics[index]) ||
                !IsFinite(result.affineBarycentricDx[index]) ||
                !IsFinite(result.affineBarycentricDy[index]))
            {
                return result;
            }
        }
        if (!IsFinite(result.texCoord) ||
            !IsFinite(result.texCoordDx) ||
            !IsFinite(result.texCoordDy) ||
            !IsFinite(result.deviceDepth) ||
            std::abs(affineSum - 1.f) > 1e-4f ||
            std::abs(perspectiveSum - 1.f) > 1e-4f)
        {
            return result;
        }

        result.reason = FallbackReason::None;
        return result;
    }

    uint32_t SelectCompactReferenceCentroidSample(
        uint32_t sameSurfaceSampleMask,
        uint32_t sampleCount)
    {
        if (!IsSupportedSampleCount(sampleCount))
            return InvalidVisibilityWord;
        const uint32_t fullMask = sampleCount == 32u
            ? 0xffffffffu
            : (1u << sampleCount) - 1u;
        sameSurfaceSampleMask &= fullMask;
        if (sameSurfaceSampleMask == 0u ||
            sameSurfaceSampleMask == fullMask)
        {
            return CompactReferencePixelCenter;
        }
        for (uint32_t sampleIndex = 0u;
            sampleIndex < sampleCount;
            ++sampleIndex)
        {
            if ((sameSurfaceSampleMask & (1u << sampleIndex)) != 0u)
                return sampleIndex;
        }
        return CompactReferencePixelCenter;
    }

    FallbackReason ValidateCompactShadeSurface(
        const CompactShadeSurfaceValidationDesc& description)
    {
        const FallbackReason keyReason =
            ValidateGpuVisibilityKey(description.key);
        if (keyReason != FallbackReason::None)
            return keyReason;

        const DecodedGeometryInstanceHandle decoded =
            DecodeGeometryInstanceHandle(
                description.key.geometryInstanceHandle);
        if (!decoded.Succeeded())
            return decoded.reason;

        if (description.geometryInstanceRecordStride !=
                sizeof(CompactGeometryInstanceGpuRecord) ||
            description.geometryRecordStride !=
                sizeof(CompactGeometryGpuRecord) ||
            description.instanceHandleStride != sizeof(uint32_t))
        {
            return FallbackReason::FormatUnsupported;
        }
        if (description.geometryInstanceRecordCount !=
                description.scene.geometryInstanceRecordCount ||
            description.geometryRecordCount !=
                description.scene.geometryRecordCount ||
            description.instanceHandleCount !=
                description.scene.instanceHandleCount ||
            decoded.slot >= description.geometryInstanceRecordCount)
        {
            return FallbackReason::SlotOutOfRange;
        }

        const CompactGeometryInstanceGpuRecord& instance =
            description.geometryInstance;
        if (instance.generation != decoded.generation)
            return FallbackReason::GenerationMismatch;
        if ((instance.flags & CompactGeometryInstanceFlag_Valid) == 0u)
            return FallbackReason::TransientMappingFailure;
        if ((instance.flags &
                CompactGeometryInstanceFlag_DeformationSupported) == 0u)
        {
            return FallbackReason::UnsupportedDeformation;
        }
        if ((instance.flags &
                CompactGeometryInstanceFlag_MaterialSupported) == 0u)
        {
            return FallbackReason::UnsupportedMaterial;
        }
        if (instance.payloadClass != static_cast<uint32_t>(
                CompactGeometryPayloadClass::Reconstruct) ||
            instance.payloadFallbackReason != static_cast<uint32_t>(
                FallbackReason::None))
        {
            return FallbackReason::TransientMappingFailure;
        }
        if (instance.stableInstanceHandleLow == 0u &&
            instance.stableInstanceHandleHigh == 0u)
        {
            return FallbackReason::TransientMappingFailure;
        }
        if (instance.topologyEpochLow !=
                description.scene.topologyEpochLow ||
            instance.topologyEpochHigh !=
                description.scene.topologyEpochHigh ||
            instance.topologyRevision !=
                description.scene.topologyRevision)
        {
            return FallbackReason::TopologyEpochMismatch;
        }
        if (instance.lodRevision != description.scene.lodRevision)
            return FallbackReason::LodRevisionMismatch;
        if (instance.deformationRevision !=
            description.scene.deformationRevision)
        {
            return FallbackReason::DeformationRevisionMismatch;
        }
        if (instance.materialRevision !=
            description.scene.materialRevision)
        {
            return FallbackReason::MaterialRevisionMismatch;
        }

        if (instance.geometryIndex >=
                description.instanceGeometryCount ||
            instance.geometryRecordIndex >=
                description.geometryRecordCount ||
            instance.instanceIndex == InvalidVisibilityWord ||
            instance.geometryIndex == InvalidVisibilityWord)
        {
            return FallbackReason::TransientMappingFailure;
        }
        if (description.key.geometryLocalPrimitiveIndex >=
                instance.primitiveCount)
        {
            return FallbackReason::PrimitiveOverflow;
        }
        if (description.mappedGeometryInstanceHandle !=
                description.key.geometryInstanceHandle)
        {
            return FallbackReason::TransientMappingFailure;
        }

        const CompactGeometryGpuRecord& geometry = description.geometry;
        if (geometry.indexCount == 0u ||
            geometry.indexCount % 3u != 0u ||
            geometry.vertexCount == 0u ||
            instance.primitiveCount != geometry.indexCount / 3u)
        {
            return FallbackReason::ReconstructionFailure;
        }
        if (geometry.materialIndex >=
                description.scene.materialRecordCount ||
            !HasExactCompactDescriptorPartition(description.scene) ||
            !IsCompactRawBufferDescriptor(
                description.scene,
                geometry.indexBufferDescriptor) ||
            !IsCompactRawBufferDescriptor(
                description.scene,
                geometry.vertexBufferDescriptor) ||
            !IsCompactRawBufferDescriptor(
                description.scene,
                description.scene.instanceBufferDescriptor) ||
            !IsCompactRawBufferDescriptor(
                description.scene,
                description.scene.materialBufferDescriptor))
        {
            return FallbackReason::TransientMappingFailure;
        }
        if ((geometry.flags & CompactGeometryFlag_Skinned) != 0u &&
            geometry.previousPositionOffset == geometry.positionOffset)
        {
            return FallbackReason::UnsupportedDeformation;
        }

        if (!ValidateElementAddress(
                0u,
                instance.instanceIndex,
                InstanceDataStride,
                description.sceneInstanceBufferByteSize) ||
            !ValidateElementAddress(
                0u,
                geometry.materialIndex,
                MaterialConstantsStride,
                description.materialBufferByteSize) ||
            !ValidateElementAddress(
                geometry.indexOffset,
                description.key.geometryLocalPrimitiveIndex,
                TriangleIndexStride,
                description.indexBufferByteSize))
        {
            return FallbackReason::ReconstructionFailure;
        }

        for (uint32_t vertexIndex : description.triangleVertexIndices)
        {
            if (vertexIndex >= geometry.vertexCount)
                return FallbackReason::ReconstructionFailure;
            if (!ValidateElementAddress(
                    geometry.positionOffset,
                    vertexIndex,
                    PositionStride,
                    description.vertexBufferByteSize) ||
                !ValidateElementAddress(
                    geometry.previousPositionOffset,
                    vertexIndex,
                    PositionStride,
                    description.vertexBufferByteSize) ||
                !ValidateElementAddress(
                    geometry.texCoordOffset,
                    vertexIndex,
                    TexCoordStride,
                    description.vertexBufferByteSize) ||
                !ValidateElementAddress(
                    geometry.normalOffset,
                    vertexIndex,
                    PackedDirectionStride,
                    description.vertexBufferByteSize) ||
                !ValidateElementAddress(
                    geometry.tangentOffset,
                    vertexIndex,
                    PackedDirectionStride,
                    description.vertexBufferByteSize))
            {
                return FallbackReason::ReconstructionFailure;
            }
        }
        return FallbackReason::None;
    }

    CompactClosestSurfaceResult SelectCompactClosestSurface(
        const CompactClosestSurfaceSample* samples,
        uint32_t sampleCount)
    {
        CompactClosestSurfaceResult result;
        if (!IsSupportedSampleCount(sampleCount))
        {
            result.reason = FallbackReason::SampleCountUnsupported;
            return result;
        }
        if (!samples)
        {
            result.reason = FallbackReason::ReconstructionFailure;
            return result;
        }

        for (uint32_t sampleIndex = 0u;
            sampleIndex < sampleCount;
            ++sampleIndex)
        {
            const CompactClosestSurfaceSample& sample = samples[sampleIndex];
            const bool background = IsClearedVisibilityKey(sample.key);
            const FallbackReason keyReason =
                ValidateGpuVisibilityKey(sample.key);
            if (background)
            {
                if (!IsFinite(sample.deviceDepth) ||
                    sample.deviceDepth != 0.f ||
                    std::signbit(sample.deviceDepth))
                {
                    result.reason = IsFinite(sample.deviceDepth)
                        ? keyReason
                        : FallbackReason::ReconstructionFailure;
                    return result;
                }
                continue;
            }
            if (keyReason != FallbackReason::None)
            {
                result.reason = keyReason;
                return result;
            }
            if (!IsFinite(sample.deviceDepth) ||
                sample.deviceDepth < 0.f || sample.deviceDepth > 1.f)
            {
                result.reason = FallbackReason::ReconstructionFailure;
                return result;
            }
            if (!sample.reconstructionValid ||
                sample.reconstructionReason != FallbackReason::None)
            {
                result.reason = sample.reconstructionReason !=
                        FallbackReason::None
                    ? sample.reconstructionReason
                    : FallbackReason::ReconstructionFailure;
                return result;
            }

            if (!result.foundSurface ||
                sample.deviceDepth > result.deviceDepth)
            {
                result.foundSurface = true;
                result.sampleIndex = sampleIndex;
                result.deviceDepth = sample.deviceDepth;
            }
        }
        return result;
    }

    CompactShadeViewReconstructionProof
        EvaluateCompactShadeViewReconstructionProof(
            const CompactShadeViewReconstructionProofInput& input)
    {
        CompactShadeViewReconstructionProof result;
        if (!input.acceptedDepthValid)
            return result;

        const bool canonicalEmptyDepth =
            input.minimumStoredDeviceDepth == 0.f &&
            !std::signbit(input.minimumStoredDeviceDepth) &&
            input.maximumStoredDeviceDepth == 0.f &&
            !std::signbit(input.maximumStoredDeviceDepth) &&
            input.conservativeDepthErrorMargin == 0.f &&
            !std::signbit(input.conservativeDepthErrorMargin);
        if (!input.acceptedDepthHasGeometry)
        {
            if (!canonicalEmptyDepth)
                return result;
            result.safe = true;
            result.reason = FallbackReason::None;
            return result;
        }

        if (!std::isfinite(input.minimumStoredDeviceDepth) ||
            !std::isfinite(input.maximumStoredDeviceDepth) ||
            !std::isfinite(input.conservativeDepthErrorMargin) ||
            !(input.minimumStoredDeviceDepth > 0.f) ||
            input.minimumStoredDeviceDepth >
                input.maximumStoredDeviceDepth ||
            input.maximumStoredDeviceDepth > 1.f ||
            input.conservativeDepthErrorMargin < 0.f ||
            !MatrixIsFinite(input.worldToClip) ||
            !MatrixIsFinite(input.clipToWorld) ||
            !MatrixIsFinite(input.worldToView))
        {
            return result;
        }
        for (float value : input.cameraDirectionOrPosition)
        {
            if (!std::isfinite(value))
                return result;
        }

        for (uint32_t axis = 0u; axis < 2u; ++axis)
        {
            const float origin = input.viewportOrigin[axis];
            const float size = input.viewportSize[axis];
            const float sizeInv = input.viewportSizeInv[axis];
            if (!std::isfinite(origin) || !std::isfinite(size) ||
                !std::isfinite(sizeInv) || !(size > 0.f) ||
                !(sizeInv > 0.f))
            {
                return result;
            }
            const float expectedInv = 1.f / size;
            if (std::memcmp(&expectedInv, &sizeInv, sizeof(float)) != 0)
                return result;
        }

        // Raster and reconstruction must be inverse descriptions of the same
        // transform. Checking both products rejects a one-sided near-singular
        // inverse whose homogeneous divide could otherwise appear bounded.
        if (!MatricesFormStableInverse(
                input.worldToClip, input.clipToWorld) ||
            !MatricesFormStableInverse(
                input.clipToWorld, input.worldToClip))
        {
            return result;
        }

        std::array<ShadeProofInterval, 2u> clipXY{};
        for (uint32_t axis = 0u; axis < 2u; ++axis)
        {
            const double origin = double(input.viewportOrigin[axis]);
            const double size = double(input.viewportSize[axis]);
            ShadeProofInterval pixels = {
                RoundDown(origin + 0.5),
                RoundUp(origin + size + 0.5)
            };
            pixels = ExpandForFloatDot(
                pixels, MaximumAbsolute(pixels), 2u);
            ShadeProofInterval delta = SubtractIntervals(
                pixels, { origin, origin });
            delta = ExpandForFloatDot(
                delta,
                MaximumAbsolute(pixels) + std::abs(origin),
                1u);
            ShadeProofInterval uv = MultiplyIntervals(
                delta,
                { double(input.viewportSizeInv[axis]),
                    double(input.viewportSizeInv[axis]) });
            uv = ExpandForFloatDot(
                uv, MaximumAbsolute(uv), 1u);
            ShadeProofInterval doubled = MultiplyIntervals(
                uv, { 2.0, 2.0 });
            doubled = ExpandForFloatDot(
                doubled, MaximumAbsolute(doubled), 1u);
            clipXY[axis] = axis == 0u
                ? SubtractIntervals(doubled, { 1.0, 1.0 })
                : SubtractIntervals({ 1.0, 1.0 }, doubled);
            clipXY[axis] = ExpandForFloatDot(
                clipXY[axis],
                MaximumAbsolute(doubled) + 1.0,
                1u);
            if (!IsFiniteInterval(clipXY[axis]))
                return result;
        }

        const std::array<ShadeProofInterval, 4u> clip = {{
            clipXY[0],
            clipXY[1],
            { double(input.minimumStoredDeviceDepth),
                double(input.maximumStoredDeviceDepth) },
            { 1.0, 1.0 }
        }};
        const std::array<ShadeProofInterval, 4u> worldHomogeneous =
            MultiplyRowVectorInterval(clip, input.clipToWorld);
        for (const ShadeProofInterval& component : worldHomogeneous)
        {
            if (!IsFiniteInterval(component))
                return result;
        }
        const double minimumWorldW = MinimumAbsolute(
            worldHomogeneous[3]);
        if (!(minimumWorldW > 16.0 * Binary32MinimumNormal))
            return result;

        std::array<ShadeProofInterval, 3u> world{};
        double maximumWorldPosition = 0.0;
        constexpr double SafeVectorComponent = 0x1p60;
        for (uint32_t component = 0u; component < 3u; ++component)
        {
            world[component] = DivideIntervals(
                worldHomogeneous[component], worldHomogeneous[3]);
            if (!IsFiniteInterval(world[component]))
                return result;
            maximumWorldPosition = std::max(
                maximumWorldPosition,
                MaximumAbsolute(world[component]));
        }
        if (!std::isfinite(maximumWorldPosition) ||
            maximumWorldPosition > SafeVectorComponent)
        {
            return result;
        }

        const std::array<ShadeProofInterval, 4u> worldPosition = {{
            world[0], world[1], world[2], { 1.0, 1.0 }
        }};
        const std::array<ShadeProofInterval, 4u> viewHomogeneous =
            MultiplyRowVectorInterval(worldPosition, input.worldToView);
        for (const ShadeProofInterval& component : viewHomogeneous)
        {
            if (!IsFiniteInterval(component))
                return result;
        }
        const double minimumViewW = MinimumAbsolute(viewHomogeneous[3]);
        if (!(minimumViewW > 16.0 * Binary32MinimumNormal))
            return result;
        const ShadeProofInterval linearViewDepth = DivideIntervals(
            viewHomogeneous[2], viewHomogeneous[3]);
        if (!IsFiniteInterval(linearViewDepth) ||
            MaximumAbsolute(linearViewDepth) > Binary32Maximum)
        {
            return result;
        }

        double footprintSquared = 0.0;
        for (const ShadeProofInterval& component : world)
        {
            const double span = RoundUp(
                component.upper - component.lower);
            if (!std::isfinite(span) || span < 0.0 ||
                span > SafeVectorComponent)
            {
                return result;
            }
            footprintSquared = RoundUp(
                footprintSquared + RoundUp(span * span));
        }
        if (!std::isfinite(footprintSquared) ||
            footprintSquared >= Binary32Maximum * 0.25)
        {
            return result;
        }
        const double maximumPixelFootprint = RoundUp(
            std::sqrt(footprintSquared));

        double viewVectorSquared = 0.0;
        for (uint32_t component = 0u; component < 3u; ++component)
        {
            double maximumComponent = 0.0;
            if (input.cameraDirectionOrPosition[3] > 0.f)
            {
                maximumComponent = std::max(
                    std::abs(world[component].lower -
                        double(input.cameraDirectionOrPosition[component])),
                    std::abs(world[component].upper -
                        double(input.cameraDirectionOrPosition[component])));
            }
            else
            {
                maximumComponent = std::abs(
                    double(input.cameraDirectionOrPosition[component]));
            }
            viewVectorSquared = RoundUp(
                viewVectorSquared +
                    RoundUp(maximumComponent * maximumComponent));
        }
        if (!std::isfinite(viewVectorSquared) ||
            viewVectorSquared >= Binary32Maximum * 0.25)
        {
            return result;
        }

        result.safe = true;
        result.reason = FallbackReason::None;
        result.minimumAbsoluteWorldW = float(std::min(
            minimumWorldW, Binary32Maximum));
        result.minimumAbsoluteViewW = float(std::min(
            minimumViewW, Binary32Maximum));
        result.maximumAbsoluteWorldPosition = float(
            maximumWorldPosition);
        result.maximumPixelFootprint = float(maximumPixelFootprint);
        return result;
    }

    CompactShadePreparedPhaseTransition AdvanceCompactShadePreparedPhase(
        CompactShadePreparedPhase current,
        CompactShadePreparedPhase requested,
        bool allowGuideSkip)
    {
        CompactShadePreparedPhaseTransition result;
        const bool guideTransition =
            current == CompactShadePreparedPhase::Prepared &&
            requested == CompactShadePreparedPhase::GuideRecorded;
        const bool lightingTransition =
            current == CompactShadePreparedPhase::GuideRecorded &&
            requested == CompactShadePreparedPhase::Consumed;
        const bool skippedGuideTransition = allowGuideSkip &&
            current == CompactShadePreparedPhase::Prepared &&
            requested == CompactShadePreparedPhase::Consumed;
        if (!guideTransition && !lightingTransition &&
            !skippedGuideTransition)
            return result;
        result.phase = requested;
        result.reason = FallbackReason::None;
        return result;
    }

    CompactShadeLightingOutcomeSelection
        SelectCompactShadeLightingOutcome(
            bool visibilityRequested,
            bool finalVisibilitySuccess,
            const DirectionalLightVisibility& predictedDirectional,
            const DirectionalLightVisibility& actualDirectional)
    {
        CompactShadeLightingOutcomeSelection result;
        const bool predictedNeutral =
            !predictedDirectional.texture &&
            !predictedDirectional.light;
        const bool predictedActive = predictedDirectional.IsComplete();
        const bool actualNeutral =
            !actualDirectional.texture && !actualDirectional.light;
        const bool actualActive = actualDirectional.IsComplete();
        if ((!predictedNeutral && !predictedActive) ||
            (!actualNeutral && !actualActive) ||
            (finalVisibilitySuccess && !visibilityRequested))
        {
            return result;
        }
        if (actualActive &&
            (!predictedActive ||
                actualDirectional.texture !=
                    predictedDirectional.texture ||
                actualDirectional.light != predictedDirectional.light))
        {
            return result;
        }

        result.visibilitySuccess = finalVisibilitySuccess;
        result.directionalActive = actualActive;
        result.variantIndex =
            (actualActive ? 2u : 0u) |
            (finalVisibilitySuccess ? 1u : 0u);
        result.reason = FallbackReason::None;
        return result;
    }

    CompactShadeDiagnosticsPublication
        ResolveCompactShadeDiagnosticsPublication(
            bool diagnosticsPrepared,
            bool exactOutcomeSelected,
            uint32_t dispatchedViewCount,
            uint32_t preparedViewCount,
            uint32_t preparedCapabilityMask)
    {
        CompactShadeDiagnosticsPublication result;
        if (!diagnosticsPrepared || !exactOutcomeSelected ||
            preparedViewCount == 0u ||
            dispatchedViewCount != preparedViewCount)
        {
            return result;
        }
        result.produced = true;
        result.capabilityMask = preparedCapabilityMask;
        return result;
    }
}

namespace uvsr::adaptive_msaa
{
    struct AdaptiveMsaaCompactShadePass::Impl
    {
        struct Pipeline
        {
            nvrhi::ShaderHandle shader;
            nvrhi::BindingLayoutHandle fixedBindingLayout;
            nvrhi::ComputePipelineHandle pso;

            [[nodiscard]] bool IsValid() const
            {
                return shader && fixedBindingLayout && pso;
            }
        };

        Impl(
            nvrhi::IDevice* device,
            std::shared_ptr<CommonRenderPasses> commonPasses)
            : device(device)
            , commonPasses(std::move(commonPasses))
            , bindingSets(device)
        {
        }

        void ResetPipelines()
        {
            resolvePipelines = {};
            lightingPipelines = {};
            diagnosticLightingPipelines = {};
            pipelinePreparationStep = 0u;
            pipelinePreparationComplete = false;
            pipelinesReady = false;
            bindingSets.Clear();
        }

        [[nodiscard]] bool SnapshotMatches(
            const std::shared_ptr<const CompactSceneSnapshot>& candidate)
            const
        {
            if (!snapshot || !candidate ||
                snapshot.get() != candidate.get())
            {
                return false;
            }
            const CompactSceneSnapshotVersion candidateVersion =
                candidate->Version();
            return candidateVersion.topologyEpoch ==
                    snapshotVersion.topologyEpoch &&
                candidateVersion.buildSerial ==
                    snapshotVersion.buildSerial &&
                candidate->BindlessLayout() == snapshot->BindlessLayout() &&
                candidate->DescriptorTable() == snapshot->DescriptorTable();
        }

        nvrhi::DeviceHandle device;
        std::shared_ptr<CommonRenderPasses> commonPasses;
        std::shared_ptr<ShaderFactory> shaderFactory;
        std::shared_ptr<const CompactSceneSnapshot> snapshot;
        CompactSceneSnapshotVersion snapshotVersion;

        nvrhi::SamplerHandle shadowSamplerComparison;
        nvrhi::BufferHandle compactConstantsBuffer;
        nvrhi::BufferHandle deferredConstantsBuffer;
        nvrhi::BufferHandle diagnosticsProducerConstantsBuffer;
        std::array<Pipeline, 4u> resolvePipelines;
        std::array<std::array<Pipeline, 4u>, 2u> lightingPipelines;
        std::array<std::array<Pipeline, 4u>, 2u>
            diagnosticLightingPipelines;
        BindingCache bindingSets;
        uint32_t pipelinePreparationStep = 0u;
        bool pipelinePreparationComplete = false;
        bool pipelinesReady = false;
        bool initialized = false;
        bool deferPipelineCreation = false;
        bool whiteWorld = false;
    };

    struct CompactShadePreparedFrame::Impl
    {
        struct LightingVariant
        {
            PbrDeferredLightingConstants deferredConstants{};
            nvrhi::ComputePipelineHandle pipeline;
            nvrhi::BindingSetHandle bindings;
            bool available = false;
        };

        struct View
        {
            CompactRasterViewProof rasterProof;
            const IView* current = nullptr;
            const IView* previous = nullptr;
            PlanarViewConstants currentConstants{};
            PlanarViewConstants previousConstants{};
            nvrhi::ViewportState viewport;
            nvrhi::TextureSubresourceSet subresources;
            nvrhi::Rect extent;
            bool mirrored = false;
            CompactShadeConstants compactConstants{};
            DiagnosticsProducerGpuConstants diagnosticsConstants{};
            nvrhi::ComputePipelineHandle guidePipeline;
            nvrhi::BindingSetHandle guideBindings;
            std::array<
                LightingVariant,
                CompactShadeLightingOutcomeCount> lightingVariants;
            uint32_t dispatchX = 0u;
            uint32_t dispatchY = 0u;
        };

        const AdaptiveMsaaCompactShadePass* pass = nullptr;
        const ICompositeView* currentComposite = nullptr;
        const ICompositeView* previousComposite = nullptr;
        std::shared_ptr<const CompactSceneSnapshot> snapshot;
        CompactSceneSnapshotVersion snapshotVersion{};
        std::vector<View> views;
        nvrhi::DescriptorTableHandle descriptorTableKeepAlive;
        nvrhi::BufferHandle compactConstantsBuffer;
        nvrhi::BufferHandle deferredConstantsBuffer;
        nvrhi::BufferHandle diagnosticsConstantsBuffer;
        std::vector<nvrhi::TextureHandle> textureKeepAlive;
        std::vector<nvrhi::BufferHandle> bufferKeepAlive;
        std::vector<std::shared_ptr<Light>> lightKeepAlive;
        AdaptiveMsaaDiagnosticsProducerOutputs* diagnosticsProducer =
            nullptr;
        uint32_t diagnosticsCapabilityMask = 0u;
        bool diagnostics = false;
        bool visibilityRequested = false;
        bool guideRequired = true;
        DirectionalLightVisibility predictedDirectionalVisibility;
        bool validatedForSubmission = false;
        CompactShadePreparedPhase phase =
            CompactShadePreparedPhase::Prepared;
    };

    CompactShadePreparedFrame::CompactShadePreparedFrame() = default;
    CompactShadePreparedFrame::~CompactShadePreparedFrame() = default;
    CompactShadePreparedFrame::CompactShadePreparedFrame(
        CompactShadePreparedFrame&& other) noexcept = default;
    CompactShadePreparedFrame& CompactShadePreparedFrame::operator=(
        CompactShadePreparedFrame&& other) noexcept = default;

    bool CompactShadePreparedFrame::IsSubmittable() const
    {
        return m_Impl != nullptr &&
            m_Reason == FallbackReason::None;
    }

    FallbackReason CompactShadePreparedFrame::Reason() const
    {
        return m_Reason;
    }

    uint32_t CompactShadePreparedFrame::ViewCount() const
    {
        return m_Impl
            ? static_cast<uint32_t>(m_Impl->views.size())
            : 0u;
    }

    AdaptiveMsaaCompactShadePass::AdaptiveMsaaCompactShadePass(
        nvrhi::IDevice* device,
        std::shared_ptr<CommonRenderPasses> commonPasses)
        : m_Impl(std::make_unique<Impl>(
            device,
            std::move(commonPasses)))
    {
    }

    AdaptiveMsaaCompactShadePass::~AdaptiveMsaaCompactShadePass() = default;

    void AdaptiveMsaaCompactShadePass::Init(
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        bool whiteWorld,
        bool deferPipelineCreation)
    {
        m_Impl->shaderFactory = shaderFactory;
        m_Impl->whiteWorld = whiteWorld;
        m_Impl->deferPipelineCreation = deferPipelineCreation;
        m_Impl->initialized = false;
        m_Impl->ResetPipelines();

        if (!m_Impl->device || !m_Impl->commonPasses || !shaderFactory)
            return;
        if (m_Impl->device->getGraphicsAPI() !=
            nvrhi::GraphicsAPI::D3D12)
        {
            return;
        }
        ID3D12Device* nativeDevice = m_Impl->device->getNativeObject(
            nvrhi::ObjectTypes::D3D12_Device);
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 d3d12Options{};
        if (!nativeDevice || FAILED(nativeDevice->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS1,
                &d3d12Options,
                sizeof(d3d12Options))) ||
            !d3d12Options.Int64ShaderOps)
        {
            log::warning(
                "Adaptive MSAA compact visibility requires native 64-bit "
                "shader integer operations for exact D3D top-left coverage; "
                "the per-sample Reference path remains active");
            return;
        }

        nvrhi::BufferDesc compactDesc;
        compactDesc.byteSize = sizeof(CompactShadeConstants);
        compactDesc.debugName = "AdaptiveMsaaCompactShadeConstants";
        compactDesc.isConstantBuffer = true;
        compactDesc.isVolatile = true;
        compactDesc.maxVersions = c_MaxRenderPassConstantBufferVersions;
        m_Impl->compactConstantsBuffer =
            m_Impl->device->createBuffer(compactDesc);

        nvrhi::BufferDesc deferredDesc;
        deferredDesc.byteSize = sizeof(PbrDeferredLightingConstants);
        deferredDesc.debugName =
            "AdaptiveMsaaCompactDeferredLightingConstants";
        deferredDesc.isConstantBuffer = true;
        deferredDesc.isVolatile = true;
        deferredDesc.maxVersions = c_MaxRenderPassConstantBufferVersions;
        m_Impl->deferredConstantsBuffer =
            m_Impl->device->createBuffer(deferredDesc);

        nvrhi::BufferDesc diagnosticsDesc;
        diagnosticsDesc.byteSize =
            sizeof(DiagnosticsProducerGpuConstants);
        diagnosticsDesc.debugName =
            "AdaptiveMsaaCompactDiagnosticsProducerConstants";
        diagnosticsDesc.isConstantBuffer = true;
        diagnosticsDesc.isVolatile = true;
        diagnosticsDesc.maxVersions =
            c_MaxRenderPassConstantBufferVersions;
        m_Impl->diagnosticsProducerConstantsBuffer =
            m_Impl->device->createBuffer(diagnosticsDesc);

        nvrhi::SamplerDesc shadowSamplerDesc;
        shadowSamplerDesc
            .setAllAddressModes(nvrhi::SamplerAddressMode::Border)
            .setBorderColor(1.f)
            .setReductionType(nvrhi::SamplerReductionType::Comparison);
        m_Impl->shadowSamplerComparison =
            m_Impl->device->createSampler(shadowSamplerDesc);

        m_Impl->initialized = m_Impl->compactConstantsBuffer &&
            m_Impl->deferredConstantsBuffer &&
            m_Impl->shadowSamplerComparison;
        if (m_Impl->initialized && m_Impl->snapshot &&
            !deferPipelineCreation)
        {
            while (!PreparePipelinesStep())
            {
            }
        }
    }

    FallbackReason AdaptiveMsaaCompactShadePass::BindSnapshot(
        std::shared_ptr<const CompactSceneSnapshot> snapshot)
    {
        if (!snapshot || !IsSnapshotComplete(*snapshot))
            return FallbackReason::ReconstructionFailure;
        if (m_Impl->SnapshotMatches(snapshot))
            return FallbackReason::None;

        m_Impl->snapshot = std::move(snapshot);
        m_Impl->snapshotVersion = m_Impl->snapshot->Version();
        m_Impl->ResetPipelines();
        if (m_Impl->initialized && !m_Impl->deferPipelineCreation)
        {
            while (!PreparePipelinesStep())
            {
            }
        }
        return FallbackReason::None;
    }

    void AdaptiveMsaaCompactShadePass::ClearSnapshot()
    {
        m_Impl->snapshot.reset();
        m_Impl->snapshotVersion = {};
        m_Impl->ResetPipelines();
    }

    bool AdaptiveMsaaCompactShadePass::IsBoundTo(
        const std::shared_ptr<const CompactSceneSnapshot>& snapshot) const
    {
        return m_Impl->SnapshotMatches(snapshot);
    }

    std::shared_ptr<const CompactSceneSnapshot>
        AdaptiveMsaaCompactShadePass::BoundSnapshot() const
    {
        return m_Impl->snapshot;
    }

    CompactSceneSnapshotVersion
        AdaptiveMsaaCompactShadePass::BoundSnapshotVersion() const
    {
        return m_Impl->snapshotVersion;
    }

    bool AdaptiveMsaaCompactShadePass::PreparePipelinesStep()
    {
        if (m_Impl->pipelinePreparationComplete)
            return true;
        if (!m_Impl->initialized || !m_Impl->snapshot ||
            !m_Impl->snapshot->BindlessLayout())
        {
            return false;
        }

        constexpr uint32_t ResolvePipelineCount = 4u;
        constexpr uint32_t LightingPipelineCount = 16u;
        constexpr uint32_t PipelineCount =
            ResolvePipelineCount + LightingPipelineCount;
        const uint32_t step = m_Impl->pipelinePreparationStep;

        if (step < ResolvePipelineCount)
        {
            const uint32_t sampleCount = 2u << step;
            Impl::Pipeline& pipeline = m_Impl->resolvePipelines[step];

            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::Compute;
            layoutDesc.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    CompactBindingConstants),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingDepth),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingSurfaceOwnership),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingGeometryInstances),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingGeometry),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingInstanceHandles),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingVisibilityClass),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackDiffuse),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackMaterial),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackNormals),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackEmissive),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackAmbientOcclusion),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackMotionVectors),
                nvrhi::BindingLayoutItem::Texture_UAV(0u),
                nvrhi::BindingLayoutItem::Texture_UAV(1u),
                nvrhi::BindingLayoutItem::Texture_UAV(2u),
                nvrhi::BindingLayoutItem::Texture_UAV(3u),
                nvrhi::BindingLayoutItem::Texture_UAV(4u),
                nvrhi::BindingLayoutItem::Texture_UAV(5u),
                nvrhi::BindingLayoutItem::Texture_UAV(6u),
                nvrhi::BindingLayoutItem::Sampler(0u)
            };
            pipeline.fixedBindingLayout =
                m_Impl->device->createBindingLayout(layoutDesc);

            std::vector<ShaderMacro> macros;
            macros.emplace_back(
                "ADAPTIVE_MSAA_COMPACT_SAMPLES",
                std::to_string(sampleCount));
            macros.emplace_back(
                "WHITE_WORLD",
                m_Impl->whiteWorld ? "1" : "0");
            pipeline.shader = m_Impl->shaderFactory->CreateShader(
                "uvsr/adaptive_msaa_compact_shade_resolve_cs.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Compute);

            if (pipeline.shader && pipeline.fixedBindingLayout)
            {
                nvrhi::ComputePipelineDesc pipelineDesc;
                pipelineDesc.CS = pipeline.shader;
                pipelineDesc.bindingLayouts = {
                    pipeline.fixedBindingLayout,
                    m_Impl->snapshot->BindlessLayout()
                };
                pipeline.pso =
                    m_Impl->device->createComputePipeline(pipelineDesc);
            }
        }
        else
        {
            const uint32_t lightingStep = step - ResolvePipelineCount;
            const uint32_t diagnosticVariant = lightingStep / 8u;
            const uint32_t baseLightingStep = lightingStep % 8u;
            const uint32_t sampleVariant = baseLightingStep % 4u;
            const uint32_t visibilityVariant = baseLightingStep / 4u;
            const uint32_t sampleCount = 2u << sampleVariant;
            Impl::Pipeline& pipeline = diagnosticVariant != 0u
                ? m_Impl->diagnosticLightingPipelines
                    [visibilityVariant][sampleVariant]
                : m_Impl->lightingPipelines
                    [visibilityVariant][sampleVariant];

            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::Compute;
            layoutDesc.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0u),
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    CompactBindingConstants),
                nvrhi::BindingLayoutItem::Texture_SRV(0u),
                nvrhi::BindingLayoutItem::Texture_SRV(1u),
                nvrhi::BindingLayoutItem::Texture_SRV(2u),
                nvrhi::BindingLayoutItem::Texture_SRV(3u),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingDepth),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingResolvedBackground),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingDirectionalVisibility),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingSurfaceOwnership),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingGeometryInstances),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingGeometry),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingInstanceHandles),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingVisibilityClass),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackDiffuse),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackMaterial),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackNormals),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackEmissive),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackAmbientOcclusion),
                nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackMotionVectors),
                nvrhi::BindingLayoutItem::Texture_UAV(0u),
                nvrhi::BindingLayoutItem::Sampler(0u),
                nvrhi::BindingLayoutItem::Sampler(1u),
                nvrhi::BindingLayoutItem::Sampler(2u),
                nvrhi::BindingLayoutItem::Sampler(3u)
            };
            if (visibilityVariant != 0u)
            {
                layoutDesc.bindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(
                        CompactBindingVisibilityBase));
                layoutDesc.bindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(
                        CompactBindingVisibilityComposite));
            }
            if (diagnosticVariant != 0u)
            {
                layoutDesc.bindings.push_back(
                    nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                        CompactBindingDiagnosticsConstants));
                layoutDesc.bindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(
                        CompactBindingDiagnosticsBackground));
                layoutDesc.bindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_UAV(1u));
                layoutDesc.bindings.push_back(
                    nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2u));
                layoutDesc.bindings.push_back(
                    nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3u));
            }
            pipeline.fixedBindingLayout =
                m_Impl->device->createBindingLayout(layoutDesc);

            std::vector<ShaderMacro> macros;
            macros.emplace_back(
                "ADAPTIVE_MSAA_COMPACT_SAMPLES",
                std::to_string(sampleCount));
            macros.emplace_back(
                "ADAPTIVE_MSAA_COMPACT_VISIBILITY",
                visibilityVariant != 0u ? "1" : "0");
            macros.emplace_back(
                "WHITE_WORLD",
                m_Impl->whiteWorld ? "1" : "0");
            macros.emplace_back(
                "UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER",
                diagnosticVariant != 0u ? "1" : "0");
            macros.emplace_back(
                "UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY",
                std::to_string(
                    UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY));
            pipeline.shader = m_Impl->shaderFactory->CreateShader(
                "uvsr/adaptive_msaa_compact_shade_lighting_cs.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Compute);

            if (pipeline.shader && pipeline.fixedBindingLayout)
            {
                nvrhi::ComputePipelineDesc pipelineDesc;
                pipelineDesc.CS = pipeline.shader;
                pipelineDesc.bindingLayouts = {
                    pipeline.fixedBindingLayout,
                    m_Impl->snapshot->BindlessLayout()
                };
                pipeline.pso =
                    m_Impl->device->createComputePipeline(pipelineDesc);
            }
        }

        ++m_Impl->pipelinePreparationStep;
        if (m_Impl->pipelinePreparationStep == PipelineCount)
        {
            m_Impl->pipelinePreparationComplete = true;
            m_Impl->pipelinesReady = true;
            for (const Impl::Pipeline& prepared :
                m_Impl->resolvePipelines)
            {
                m_Impl->pipelinesReady &= prepared.IsValid();
            }
            for (const auto& visibilityVariant :
                m_Impl->lightingPipelines)
            {
                for (const Impl::Pipeline& prepared :
                    visibilityVariant)
                {
                    m_Impl->pipelinesReady &= prepared.IsValid();
                }
            }
        }
        return m_Impl->pipelinePreparationComplete;
    }

    bool AdaptiveMsaaCompactShadePass::ArePipelinesReady() const
    {
        return m_Impl->pipelinesReady;
    }

    FallbackReason AdaptiveMsaaCompactShadePass::ValidateGuideDispatch(
        const std::shared_ptr<const CompactSceneSnapshot>& snapshot,
        const ICompositeView& currentView,
        const ICompositeView* previousView,
        const AdaptiveMsaaCompactGuideInputs& inputs,
        const AdaptiveMsaaCompactGuideOutputs& outputs,
        uint32_t sampleCount) const
    {
        if (!m_Impl->SnapshotMatches(snapshot) ||
            !IsSnapshotComplete(*snapshot))
        {
            return FallbackReason::TransientMappingFailure;
        }
        const int sampleVariant = GetSampleVariant(sampleCount);
        if (sampleVariant < 0)
            return FallbackReason::SampleCountUnsupported;
        if (!m_Impl->pipelinesReady ||
            !m_Impl->resolvePipelines[size_t(sampleVariant)].IsValid())
        {
            return FallbackReason::FormatUnsupported;
        }
        if (!HasCompatiblePlanarViews(currentView, previousView) ||
            !IsMultisampleSrv(inputs.depth, sampleCount) ||
            !IsMultisampleSrv(inputs.surfaceOwnership, sampleCount) ||
            !IsMultisampleSrv(inputs.visibilityClass, sampleCount))
        {
            return FallbackReason::ReconstructionFailure;
        }

        const nvrhi::TextureDesc& depthDesc = inputs.depth->getDesc();
        const nvrhi::TextureDesc& keyDesc =
            inputs.surfaceOwnership->getDesc();
        if (keyDesc.format != nvrhi::Format::RG32_UINT ||
            !HasExactTextureTopology(depthDesc, keyDesc) ||
            !IsVisibilityClassSrv(
                inputs.visibilityClass, sampleCount, keyDesc) ||
            !ValidateFallbackGBufferInputs(
                inputs.fallbackGBuffer, sampleCount, keyDesc))
        {
            return FallbackReason::FormatUnsupported;
        }
        if (inputs.depth == inputs.surfaceOwnership ||
            inputs.depth == inputs.visibilityClass ||
            inputs.surfaceOwnership == inputs.visibilityClass ||
            FallbackGBufferContains(
                inputs.fallbackGBuffer, inputs.depth) ||
            FallbackGBufferContains(
                inputs.fallbackGBuffer, inputs.surfaceOwnership) ||
            FallbackGBufferContains(
                inputs.fallbackGBuffer, inputs.visibilityClass))
        {
            return FallbackReason::FormatUnsupported;
        }

        const std::array<std::pair<nvrhi::ITexture*, nvrhi::Format>, 7u>
            outputContracts = {{
                { outputs.depth, nvrhi::Format::R32_FLOAT },
                { outputs.diffuse, nvrhi::Format::RGBA16_FLOAT },
                { outputs.material, nvrhi::Format::RGBA16_FLOAT },
                { outputs.normals, nvrhi::Format::RGBA16_FLOAT },
                { outputs.emissive, nvrhi::Format::RGBA16_FLOAT },
                { outputs.materialAmbientOcclusion,
                    nvrhi::Format::R16_FLOAT },
                { outputs.motionVectors, nvrhi::Format::RGBA16_FLOAT }
            }};
        for (const auto& [texture, format] : outputContracts)
        {
            if (!IsSingleSampleTexture(
                    texture, format, true, keyDesc))
            {
                return FallbackReason::FormatUnsupported;
            }
        }
        for (size_t first = 0u;
            first < outputContracts.size();
            ++first)
        {
            for (size_t second = first + 1u;
                second < outputContracts.size();
                ++second)
            {
                if (outputContracts[first].first ==
                    outputContracts[second].first)
                {
                    return FallbackReason::FormatUnsupported;
                }
            }
        }
        return FallbackReason::None;
    }

    FallbackReason AdaptiveMsaaCompactShadePass::ValidateLightingDispatch(
        const std::shared_ptr<const CompactSceneSnapshot>& snapshot,
        const ICompositeView& currentView,
        const ICompositeView* previousView,
        const AdaptiveMsaaCompactLightingInputs& inputs,
        uint32_t sampleCount) const
    {
        if (!m_Impl->SnapshotMatches(snapshot) ||
            !IsSnapshotComplete(*snapshot))
        {
            return FallbackReason::TransientMappingFailure;
        }
        const int sampleVariant = GetSampleVariant(sampleCount);
        if (sampleVariant < 0)
            return FallbackReason::SampleCountUnsupported;
        if (inputs.preparationQueueCapacity == 0u ||
            inputs.preparationQueueCapacity >
                CompactPreparationQueueCapacity)
        {
            return FallbackReason::WorkListOverflow;
        }
        const bool visibility = inputs.visibilityBaseLighting &&
            inputs.visibilityComposite;
        if ((inputs.visibilityBaseLighting == nullptr) !=
            (inputs.visibilityComposite == nullptr))
        {
            return FallbackReason::ReconstructionFailure;
        }
        if (!m_Impl->pipelinesReady ||
            !m_Impl->lightingPipelines[visibility ? 1u : 0u]
                [size_t(sampleVariant)].IsValid())
        {
            return FallbackReason::FormatUnsupported;
        }
        if (!HasCompatiblePlanarViews(currentView, previousView) ||
            !IsMultisampleSrv(inputs.depth, sampleCount) ||
            !IsMultisampleSrv(inputs.surfaceOwnership, sampleCount) ||
            !IsMultisampleSrv(inputs.visibilityClass, sampleCount))
        {
            return FallbackReason::ReconstructionFailure;
        }

        const nvrhi::TextureDesc& depthDesc = inputs.depth->getDesc();
        const nvrhi::TextureDesc& keyDesc =
            inputs.surfaceOwnership->getDesc();
        if (keyDesc.format != nvrhi::Format::RG32_UINT ||
            !HasExactTextureTopology(depthDesc, keyDesc) ||
            !IsVisibilityClassSrv(
                inputs.visibilityClass, sampleCount, keyDesc) ||
            !ValidateFallbackGBufferInputs(
                inputs.fallbackGBuffer, sampleCount, keyDesc) ||
            !IsSingleSampleTexture(
                inputs.resolvedBackground,
                nvrhi::Format::RGBA16_FLOAT,
                false,
                keyDesc) ||
            !IsSingleSampleTexture(
                inputs.output,
                nvrhi::Format::RGBA16_FLOAT,
                true,
                keyDesc) ||
            inputs.output == inputs.resolvedBackground)
        {
            return FallbackReason::FormatUnsupported;
        }
        if (inputs.depth == inputs.surfaceOwnership ||
            inputs.depth == inputs.visibilityClass ||
            inputs.surfaceOwnership == inputs.visibilityClass ||
            FallbackGBufferContains(
                inputs.fallbackGBuffer, inputs.depth) ||
            FallbackGBufferContains(
                inputs.fallbackGBuffer, inputs.surfaceOwnership) ||
            FallbackGBufferContains(
                inputs.fallbackGBuffer, inputs.visibilityClass))
        {
            return FallbackReason::FormatUnsupported;
        }
        if (visibility &&
            (!IsSingleSampleTexture(
                    inputs.visibilityBaseLighting,
                    nvrhi::Format::RGBA16_FLOAT,
                    false,
                    keyDesc) ||
                !IsSingleSampleTexture(
                    inputs.visibilityComposite,
                    nvrhi::Format::RGBA16_FLOAT,
                    false,
                    keyDesc)))
        {
            return FallbackReason::FormatUnsupported;
        }
        if (inputs.output == inputs.visibilityBaseLighting ||
            inputs.output == inputs.visibilityComposite ||
            (visibility && inputs.visibilityBaseLighting ==
                inputs.visibilityComposite))
        {
            return FallbackReason::FormatUnsupported;
        }
        return FallbackReason::None;
    }

    CompactShadePreparedFrame AdaptiveMsaaCompactShadePass::PrepareFrame(
        const AdaptiveMsaaCompactShadePrepareParameters& parameters)
    {
        CompactShadePreparedFrame prepared;
        auto Reject = [&prepared](FallbackReason reason)
        {
            prepared.m_Impl.reset();
            prepared.m_Reason = reason != FallbackReason::None
                ? reason
                : FallbackReason::ReconstructionFailure;
            return std::move(prepared);
        };

        if (!parameters.raster ||
            !parameters.raster->IsSubmittable() ||
            !parameters.snapshot || !parameters.currentView)
        {
            return Reject(FallbackReason::ReconstructionFailure);
        }
        FallbackReason reason = FallbackReason::None;
        if (parameters.guideRequired)
        {
            reason = ValidateGuideDispatch(
                parameters.snapshot,
                *parameters.currentView,
                parameters.previousView,
                parameters.guideInputs,
                parameters.guideOutputs,
                parameters.sampleCount);
            if (reason != FallbackReason::None)
                return Reject(reason);
        }
        reason = ValidateLightingDispatch(
            parameters.snapshot,
            *parameters.currentView,
            parameters.previousView,
            parameters.lightingInputs,
            parameters.sampleCount);
        if (reason != FallbackReason::None)
            return Reject(reason);
        if (parameters.guideRequired &&
            (parameters.guideInputs.depth !=
                    parameters.lightingInputs.depth ||
                parameters.guideInputs.surfaceOwnership !=
                    parameters.lightingInputs.surfaceOwnership ||
                parameters.guideInputs.visibilityClass !=
                    parameters.lightingInputs.visibilityClass ||
                !SameFallbackGBufferInputs(
                    parameters.guideInputs.fallbackGBuffer,
                    parameters.lightingInputs.fallbackGBuffer)))
        {
            return Reject(FallbackReason::ReconstructionFailure);
        }
        reason = parameters.snapshot->ValidateReadResources();
        if (reason != FallbackReason::None)
            return Reject(reason);

        const bool visibilityRequested =
            parameters.lightingInputs.visibilityBaseLighting &&
            parameters.lightingInputs.visibilityComposite;
        if (parameters.separateIndirect && !visibilityRequested)
            return Reject(FallbackReason::ReconstructionFailure);
        if (visibilityRequested && !parameters.guideRequired)
            return Reject(FallbackReason::ReconstructionFailure);

        const int sampleVariant = GetSampleVariant(parameters.sampleCount);
        if (sampleVariant < 0)
            return Reject(FallbackReason::SampleCountUnsupported);
        const std::vector<CompactRasterViewProof>& rasterProofs =
            parameters.raster->ViewProofs();
        const uint32_t viewCount = parameters.currentView
            ->GetNumChildViews(ViewType::PLANAR);
        if (viewCount == 0u || rasterProofs.size() != viewCount)
            return Reject(FallbackReason::TransientMappingFailure);

        AdaptiveMsaaDiagnosticsProducerOutputs* diagnosticsProducer =
            parameters.lightingInputs.diagnosticsProducer;
        if (diagnosticsProducer)
            diagnosticsProducer->ResetProducedState();
        const bool adaptiveDiagnostics = diagnosticsProducer &&
            diagnosticsProducer->Requested();
        if (adaptiveDiagnostics &&
            (!ValidateDiagnosticsProducer(
                    *diagnosticsProducer,
                    parameters.lightingInputs,
                    parameters.sampleCount) ||
                !m_Impl->diagnosticsProducerConstantsBuffer))
        {
            return Reject(FallbackReason::FormatUnsupported);
        }

        const Impl::Pipeline* guidePipeline = parameters.guideRequired
            ? &m_Impl->resolvePipelines[size_t(sampleVariant)]
            : nullptr;
        auto GetLightingPipeline = [this, adaptiveDiagnostics,
                sampleVariant](bool visibility)
            -> const Impl::Pipeline&
        {
            return adaptiveDiagnostics
                ? m_Impl->diagnosticLightingPipelines
                    [visibility ? 1u : 0u][size_t(sampleVariant)]
                : m_Impl->lightingPipelines
                    [visibility ? 1u : 0u][size_t(sampleVariant)];
        };
        if ((guidePipeline && !guidePipeline->IsValid()) ||
            !GetLightingPipeline(false).IsValid() ||
            (visibilityRequested &&
                !GetLightingPipeline(true).IsValid()))
        {
            return Reject(FallbackReason::FormatUnsupported);
        }

        auto implementation =
            std::make_unique<CompactShadePreparedFrame::Impl>();
        implementation->pass = this;
        implementation->currentComposite = parameters.currentView;
        implementation->previousComposite = parameters.previousView;
        implementation->snapshot = parameters.snapshot;
        implementation->snapshotVersion = parameters.snapshot->Version();
        implementation->descriptorTableKeepAlive =
            parameters.snapshot->DescriptorTable();
        implementation->compactConstantsBuffer =
            m_Impl->compactConstantsBuffer;
        implementation->deferredConstantsBuffer =
            m_Impl->deferredConstantsBuffer;
        implementation->diagnosticsConstantsBuffer =
            m_Impl->diagnosticsProducerConstantsBuffer;
        implementation->diagnosticsProducer = diagnosticsProducer;
        implementation->diagnostics = adaptiveDiagnostics;
        implementation->diagnosticsCapabilityMask = adaptiveDiagnostics
            ? diagnosticsProducer->capabilityMask
            : 0u;
        implementation->visibilityRequested = visibilityRequested;
        implementation->guideRequired = parameters.guideRequired;
        implementation->predictedDirectionalVisibility =
            parameters.directionalVisibility;
        if (!implementation->descriptorTableKeepAlive ||
            !implementation->compactConstantsBuffer ||
            !implementation->deferredConstantsBuffer)
        {
            return Reject(FallbackReason::ReconstructionFailure);
        }

        auto KeepTexture = [&implementation](nvrhi::ITexture* texture)
        {
            if (!texture)
                return;
            const auto duplicate = std::find_if(
                implementation->textureKeepAlive.begin(),
                implementation->textureKeepAlive.end(),
                [texture](const nvrhi::TextureHandle& candidate)
                {
                    return candidate.Get() == texture;
                });
            if (duplicate == implementation->textureKeepAlive.end())
                implementation->textureKeepAlive.emplace_back(texture);
        };
        auto KeepBuffer = [&implementation](nvrhi::IBuffer* buffer)
        {
            if (!buffer)
                return;
            const auto duplicate = std::find_if(
                implementation->bufferKeepAlive.begin(),
                implementation->bufferKeepAlive.end(),
                [buffer](const nvrhi::BufferHandle& candidate)
                {
                    return candidate.Get() == buffer;
                });
            if (duplicate == implementation->bufferKeepAlive.end())
                implementation->bufferKeepAlive.emplace_back(buffer);
        };
        KeepTexture(parameters.lightingInputs.depth);
        KeepTexture(parameters.lightingInputs.surfaceOwnership);
        KeepTexture(parameters.lightingInputs.visibilityClass);
        KeepTexture(parameters.lightingInputs.fallbackGBuffer.diffuse);
        KeepTexture(parameters.lightingInputs.fallbackGBuffer.material);
        KeepTexture(parameters.lightingInputs.fallbackGBuffer.normals);
        KeepTexture(parameters.lightingInputs.fallbackGBuffer.emissive);
        KeepTexture(parameters.lightingInputs.fallbackGBuffer
            .materialAmbientOcclusion);
        KeepTexture(parameters.lightingInputs.fallbackGBuffer.motionVectors);
        if (parameters.guideRequired)
        {
            KeepTexture(parameters.guideOutputs.depth);
            KeepTexture(parameters.guideOutputs.diffuse);
            KeepTexture(parameters.guideOutputs.material);
            KeepTexture(parameters.guideOutputs.normals);
            KeepTexture(parameters.guideOutputs.emissive);
            KeepTexture(
                parameters.guideOutputs.materialAmbientOcclusion);
            KeepTexture(parameters.guideOutputs.motionVectors);
        }
        KeepTexture(parameters.lightingInputs.resolvedBackground);
        KeepTexture(parameters.lightingInputs.visibilityBaseLighting);
        KeepTexture(parameters.lightingInputs.visibilityComposite);
        KeepTexture(parameters.lightingInputs.output);

        implementation->lightKeepAlive =
            parameters.lightingInputs.lights
            ? *parameters.lightingInputs.lights
            : std::vector<std::shared_ptr<Light>>{};

        const nvrhi::TextureDesc& outputDesc =
            parameters.lightingInputs.output->getDesc();
        const bool predictedDirectionalNeutral =
            !parameters.directionalVisibility.texture &&
            !parameters.directionalVisibility.light;
        const bool predictedDirectionalActive =
            parameters.directionalVisibility.IsComplete();
        if (!predictedDirectionalNeutral && !predictedDirectionalActive)
            return Reject(FallbackReason::ReconstructionFailure);
        if (predictedDirectionalActive && !parameters.guideRequired)
            return Reject(FallbackReason::ReconstructionFailure);
        if (predictedDirectionalActive)
        {
            const nvrhi::TextureDesc& visibilityDesc =
                parameters.directionalVisibility.texture->getDesc();
            DirectionalLightVisibilityTextureProperties properties;
            properties.width = visibilityDesc.width;
            properties.height = visibilityDesc.height;
            properties.depth = visibilityDesc.depth;
            properties.arraySize = visibilityDesc.arraySize;
            properties.mipLevels = visibilityDesc.mipLevels;
            properties.sampleCount = visibilityDesc.sampleCount;
            properties.r8Unorm =
                visibilityDesc.format == nvrhi::Format::R8_UNORM;
            properties.texture2D = visibilityDesc.dimension ==
                nvrhi::TextureDimension::Texture2D;
            properties.shaderResource = visibilityDesc.isShaderResource;
            if (!IsDirectionalLightVisibilityTextureCompatible(
                    properties, outputDesc.width, outputDesc.height))
            {
                return Reject(FallbackReason::FormatUnsupported);
            }
            KeepTexture(parameters.directionalVisibility.texture);
        }

        PbrDeferredLightingConstants deferredBase{};
        DeferredLightingConstants& lighting = deferredBase.deferred;
        deferredBase.separateIndirect = 0;
        deferredBase.lightingDebugView = parameters.lightingDebugView;
        deferredBase.visibilityDebugView =
            parameters.visibilityDebugView;
        deferredBase.directionalVisibilityLightIndex = -1;
        lighting.randomOffset = float2(
            parameters.randomOffsetX, parameters.randomOffsetY);
        lighting.noisePattern[0] =
            float4(0.059f, 0.529f, 0.176f, 0.647f);
        lighting.noisePattern[1] =
            float4(0.765f, 0.294f, 0.882f, 0.412f);
        lighting.noisePattern[2] =
            float4(0.235f, 0.706f, 0.118f, 0.588f);
        lighting.noisePattern[3] =
            float4(0.941f, 0.471f, 0.824f, 0.353f);
        lighting.numLightProbes = parameters.environment &&
            parameters.environment->IsActive() ? 1u : 0u;
        if (lighting.numLightProbes > 0u)
        {
            parameters.environment->FillLightProbeConstants(
                lighting.lightProbes[0]);
        }
        lighting.indirectDiffuseScale = 1.f;

        nvrhi::TextureHandle shadowMapTexture;
        int numShadows = 0;
        int predictedDirectionalLightIndex = -1;
        for (const std::shared_ptr<Light>& light :
            implementation->lightKeepAlive)
        {
            if (!light)
                continue;
            if (light->shadowMap)
            {
                nvrhi::ITexture* candidate =
                    light->shadowMap->GetTexture();
                if (!candidate)
                    return Reject(FallbackReason::ReconstructionFailure);
                if (!shadowMapTexture)
                {
                    shadowMapTexture = candidate;
                    lighting.shadowMapTextureSize =
                        float2(light->shadowMap->GetTextureSize());
                    KeepTexture(candidate);
                }
                else if (shadowMapTexture.Get() != candidate)
                {
                    return Reject(FallbackReason::ReconstructionFailure);
                }
            }
            if (lighting.numLights >= DEFERRED_MAX_LIGHTS)
                break;

            LightConstants& lightConstants =
                lighting.lights[lighting.numLights];
            light->FillLightConstants(lightConstants);
            if (TargetsDirectionalLight(
                    parameters.directionalVisibility, light.get()))
            {
                predictedDirectionalLightIndex = int(lighting.numLights);
            }
            if (light->shadowMap)
            {
                const uint32_t cascadeCount = std::min(
                    light->shadowMap->GetNumberOfCascades(), 4u);
                for (uint32_t cascade = 0u;
                    cascade < cascadeCount &&
                        numShadows < DEFERRED_MAX_SHADOWS;
                    ++cascade)
                {
                    const IShadowMap* cascadeShadow =
                        light->shadowMap->GetCascade(cascade);
                    if (!cascadeShadow)
                        return Reject(
                            FallbackReason::ReconstructionFailure);
                    cascadeShadow->FillShadowConstants(
                        lighting.shadows[numShadows]);
                    lightConstants.shadowCascades[cascade] =
                        numShadows++;
                }
                const uint32_t objectShadowCount = std::min(
                    light->shadowMap->GetNumberOfPerObjectShadows(), 4u);
                for (uint32_t objectShadow = 0u;
                    objectShadow < objectShadowCount &&
                        numShadows < DEFERRED_MAX_SHADOWS;
                    ++objectShadow)
                {
                    const IShadowMap* objectShadowMap =
                        light->shadowMap->GetPerObjectShadow(objectShadow);
                    if (!objectShadowMap)
                        return Reject(
                            FallbackReason::ReconstructionFailure);
                    objectShadowMap->FillShadowConstants(
                        lighting.shadows[numShadows]);
                    lightConstants.perObjectShadows[objectShadow] =
                        numShadows++;
                }
            }
            ++lighting.numLights;
        }
        if (predictedDirectionalActive &&
            predictedDirectionalLightIndex < 0)
        {
            return Reject(FallbackReason::TransientMappingFailure);
        }

        nvrhi::TextureHandle shadowBinding = shadowMapTexture
            ? shadowMapTexture
            : m_Impl->commonPasses
                ->m_BlackDepthStencilTexture2DArray;
        nvrhi::TextureHandle diffuseEnvironment =
            parameters.environment && parameters.environment->diffuseMap
            ? parameters.environment->diffuseMap
            : m_Impl->commonPasses->m_BlackCubeMapArray;
        nvrhi::TextureHandle specularEnvironment =
            parameters.environment && parameters.environment->specularMap
            ? parameters.environment->specularMap
            : m_Impl->commonPasses->m_BlackCubeMapArray;
        nvrhi::TextureHandle environmentBrdf =
            parameters.environment && parameters.environment->environmentBrdf
            ? parameters.environment->environmentBrdf
            : m_Impl->commonPasses->m_BlackTexture;
        nvrhi::TextureHandle neutralDirectionalVisibility =
            m_Impl->commonPasses->m_WhiteTexture;
        nvrhi::TextureHandle activeDirectionalVisibility =
            predictedDirectionalActive
            ? nvrhi::TextureHandle(
                parameters.directionalVisibility.texture)
            : neutralDirectionalVisibility;
        if (!shadowBinding || !diffuseEnvironment ||
            !specularEnvironment || !environmentBrdf ||
            !neutralDirectionalVisibility ||
            !activeDirectionalVisibility)
        {
            return Reject(FallbackReason::ReconstructionFailure);
        }
        KeepTexture(shadowBinding);
        KeepTexture(diffuseEnvironment);
        KeepTexture(specularEnvironment);
        KeepTexture(environmentBrdf);
        KeepTexture(neutralDirectionalVisibility);
        KeepTexture(activeDirectionalVisibility);

        if (adaptiveDiagnostics)
        {
            KeepTexture(diagnosticsProducer->multisampledBackground);
            KeepTexture(diagnosticsProducer->sampleStatus);
            KeepBuffer(diagnosticsProducer->selectedSampleData);
            KeepBuffer(diagnosticsProducer->pixelMetadata);
        }

        implementation->views.reserve(viewCount);
        for (uint32_t viewIndex = 0u;
            viewIndex < viewCount;
            ++viewIndex)
        {
            CompactShadePreparedFrame::Impl::View viewState;
            const CompactRasterViewProof& rasterProof =
                rasterProofs[viewIndex];
            viewState.current = parameters.currentView->GetChildView(
                ViewType::PLANAR, viewIndex);
            viewState.previous = parameters.previousView
                ? parameters.previousView->GetChildView(
                    ViewType::PLANAR, viewIndex)
                : viewState.current;
            if (!viewState.current || !viewState.previous)
                return Reject(FallbackReason::TransientMappingFailure);
            viewState.current->FillPlanarViewConstants(
                viewState.currentConstants);
            viewState.previous->FillPlanarViewConstants(
                viewState.previousConstants);
            viewState.viewport = viewState.current->GetViewportState();
            viewState.subresources =
                viewState.current->GetSubresources();
            viewState.extent = viewState.current->GetViewExtent();
            viewState.mirrored = viewState.current->IsMirrored();

            const nvrhi::TextureDesc& keyDesc =
                rasterProof.surfaceKeyTarget
                ? rasterProof.surfaceKeyTarget->getDesc()
                : nvrhi::TextureDesc{};
            const nvrhi::TextureDesc& classDesc =
                rasterProof.visibilityClassTarget
                ? rasterProof.visibilityClassTarget->getDesc()
                : nvrhi::TextureDesc{};
            const nvrhi::TextureDesc& depthDesc = rasterProof.depthTarget
                ? rasterProof.depthTarget->getDesc()
                : nvrhi::TextureDesc{};
            const CompactRasterViewTransformIdentity currentIdentity =
                CaptureRasterIdentity(viewState.currentConstants);
            if (rasterProof.sourceViewIndex != viewIndex ||
                rasterProof.view != viewState.current ||
                rasterProof.surfaceKeyTarget.Get() !=
                    parameters.lightingInputs.surfaceOwnership ||
                rasterProof.visibilityClassTarget.Get() !=
                    parameters.lightingInputs.visibilityClass ||
                rasterProof.depthTarget.Get() !=
                    parameters.lightingInputs.depth ||
                rasterProof.surfaceKeySubresources !=
                    viewState.subresources ||
                rasterProof.visibilityClassSubresources !=
                    viewState.subresources ||
                rasterProof.depthSubresources != viewState.subresources ||
                rasterProof.surfaceKeyFormat !=
                    nvrhi::Format::RG32_UINT ||
                (rasterProof.visibilityClassFormat !=
                        nvrhi::Format::R8_UINT &&
                    rasterProof.visibilityClassFormat !=
                        nvrhi::Format::R32_UINT) ||
                rasterProof.depthFormat != nvrhi::Format::D32 ||
                rasterProof.sampleCount != parameters.sampleCount ||
                keyDesc.sampleCount != rasterProof.sampleCount ||
                classDesc.sampleCount != rasterProof.sampleCount ||
                depthDesc.sampleCount != rasterProof.sampleCount ||
                keyDesc.sampleQuality != rasterProof.sampleQuality ||
                classDesc.sampleQuality != rasterProof.sampleQuality ||
                depthDesc.sampleQuality != rasterProof.sampleQuality ||
                keyDesc.format != nvrhi::Format::RG32_UINT ||
                classDesc.format != rasterProof.visibilityClassFormat ||
                depthDesc.format != nvrhi::Format::D32 ||
                !keyDesc.isShaderResource ||
                !classDesc.isShaderResource ||
                !depthDesc.isShaderResource ||
                !HasExactTextureTopology(keyDesc, classDesc) ||
                !HasExactTextureTopology(keyDesc, depthDesc) ||
                !SameCompactRasterViewportState(
                    rasterProof.viewport, viewState.viewport) ||
                !SameCompactRasterViewTransformIdentity(
                    rasterProof.transformIdentity, currentIdentity) ||
                !ViewExtentMatchesViewport(
                    viewState.extent,
                    viewState.viewport,
                    viewState.currentConstants))
            {
                return Reject(FallbackReason::ReconstructionFailure);
            }

            const AdaptiveMsaaCompactFallbackGBufferInputs&
                fallbackInputs =
                    parameters.lightingInputs.fallbackGBuffer;
            const std::array<nvrhi::ITexture*, 5u>
                expectedFallbackTargets = {
                    fallbackInputs.diffuse,
                    fallbackInputs.material,
                    fallbackInputs.normals,
                    fallbackInputs.emissive,
                    fallbackInputs.materialAmbientOcclusion };
            if (rasterProof.fallbackMayStoreZeroDepth !=
                    rasterProof.fullGBufferFallbackPresent)
            {
                return Reject(FallbackReason::ReconstructionFailure);
            }
            if (rasterProof.fullGBufferFallbackPresent)
            {
                if (!fallbackInputs.Complete())
                    return Reject(FallbackReason::ReconstructionFailure);
                for (size_t fallbackIndex = 0u;
                    fallbackIndex < expectedFallbackTargets.size();
                    ++fallbackIndex)
                {
                    if (rasterProof.fallbackGBufferTargets[fallbackIndex]
                                .Get() !=
                            expectedFallbackTargets[fallbackIndex] ||
                        rasterProof
                                .fallbackGBufferSubresources[fallbackIndex] !=
                            viewState.subresources)
                    {
                        return Reject(
                            FallbackReason::ReconstructionFailure);
                    }
                }
                if (rasterProof.fallbackMotionTarget.Get() !=
                        fallbackInputs.motionVectors ||
                    rasterProof.fallbackMotionSubresources !=
                        viewState.subresources)
                {
                    return Reject(FallbackReason::ReconstructionFailure);
                }
            }
            else
            {
                for (const nvrhi::TextureHandle& fallbackTarget :
                    rasterProof.fallbackGBufferTargets)
                {
                    if (fallbackTarget)
                        return Reject(
                            FallbackReason::ReconstructionFailure);
                }
                if (rasterProof.fallbackMotionTarget)
                    return Reject(FallbackReason::ReconstructionFailure);
            }
            const CompactShadeViewReconstructionProof inverseProof =
                EvaluateCompactShadeViewReconstructionProof(
                    MakeViewReconstructionProofInput(
                        viewState.currentConstants, rasterProof));
            if (!inverseProof.safe ||
                inverseProof.reason != FallbackReason::None)
            {
                return Reject(inverseProof.reason);
            }

            viewState.rasterProof = rasterProof;
            viewState.compactConstants.gbuffer.view =
                viewState.currentConstants;
            viewState.compactConstants.gbuffer.viewPrev =
                viewState.previousConstants;
            viewState.compactConstants.scene =
                parameters.snapshot->GpuConstants();
            viewState.compactConstants.snapshotBuildSerialLow =
                LowWord(parameters.snapshot->BuildSerial());
            viewState.compactConstants.snapshotBuildSerialHigh =
                HighWord(parameters.snapshot->BuildSerial());
            viewState.compactConstants.sampleCount =
                parameters.sampleCount;
            viewState.compactConstants.frontCounterClockwise =
                viewState.mirrored ? 1u : 0u;
            viewState.compactConstants.preparationQueueCapacity =
                parameters.lightingInputs.preparationQueueCapacity;
            if (adaptiveDiagnostics)
            {
                viewState.diagnosticsConstants.executionFlags =
                    diagnosticsProducer->executionFlags;
                viewState.diagnosticsConstants.selectedPixelX =
                    diagnosticsProducer->selectedPixelX;
                viewState.diagnosticsConstants.selectedPixelY =
                    diagnosticsProducer->selectedPixelY;
                viewState.diagnosticsConstants.producerCapabilityMask =
                    diagnosticsProducer->capabilityMask;
                viewState.diagnosticsConstants.viewportOriginX = 0u;
                viewState.diagnosticsConstants.viewportOriginY = 0u;
                viewState.diagnosticsConstants.viewportWidth =
                    outputDesc.width;
                viewState.diagnosticsConstants.viewportHeight =
                    outputDesc.height;
            }

            if (guidePipeline)
            {
                nvrhi::BindingSetDesc guideBindings;
                guideBindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        CompactBindingConstants,
                        implementation->compactConstantsBuffer),
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingDepth,
                        parameters.guideInputs.depth,
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingSurfaceOwnership,
                        parameters.guideInputs.surfaceOwnership,
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingVisibilityClass,
                        parameters.guideInputs.visibilityClass,
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingFallbackDiffuse,
                        FallbackTextureOrDepth(
                            parameters.guideInputs.fallbackGBuffer.diffuse,
                            parameters.guideInputs.depth),
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingFallbackMaterial,
                        FallbackTextureOrDepth(
                            parameters.guideInputs.fallbackGBuffer.material,
                            parameters.guideInputs.depth),
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingFallbackNormals,
                        FallbackTextureOrDepth(
                            parameters.guideInputs.fallbackGBuffer.normals,
                            parameters.guideInputs.depth),
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingFallbackEmissive,
                        FallbackTextureOrDepth(
                            parameters.guideInputs.fallbackGBuffer.emissive,
                            parameters.guideInputs.depth),
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingFallbackAmbientOcclusion,
                        FallbackTextureOrDepth(
                            parameters.guideInputs.fallbackGBuffer
                                .materialAmbientOcclusion,
                            parameters.guideInputs.depth),
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingFallbackMotionVectors,
                        FallbackTextureOrDepth(
                            parameters.guideInputs.fallbackGBuffer
                                .motionVectors,
                            parameters.guideInputs.depth),
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(
                        CompactBindingGeometryInstances,
                        parameters.snapshot
                            ->GeometryInstanceBuffer()),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(
                        CompactBindingGeometry,
                        parameters.snapshot->GeometryBuffer()),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(
                        CompactBindingInstanceHandles,
                        parameters.snapshot
                            ->InstanceHandleBuffer()),
                    nvrhi::BindingSetItem::Texture_UAV(
                        0u, parameters.guideOutputs.depth,
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_UAV(
                        1u, parameters.guideOutputs.diffuse,
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_UAV(
                        2u, parameters.guideOutputs.material,
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_UAV(
                        3u, parameters.guideOutputs.normals,
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_UAV(
                        4u, parameters.guideOutputs.emissive,
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_UAV(
                        5u,
                        parameters.guideOutputs
                            .materialAmbientOcclusion,
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Texture_UAV(
                        6u, parameters.guideOutputs.motionVectors,
                        nvrhi::Format::UNKNOWN,
                        viewState.subresources),
                    nvrhi::BindingSetItem::Sampler(
                        0u,
                        m_Impl->commonPasses
                            ->m_AnisotropicWrapSampler)
                };
                viewState.guideBindings =
                    m_Impl->device->createBindingSet(
                        guideBindings,
                        guidePipeline->fixedBindingLayout);
                if (!viewState.guideBindings)
                {
                    return Reject(
                        FallbackReason::ReconstructionFailure);
                }
                viewState.guidePipeline = guidePipeline->pso;
            }

            for (uint32_t visibilityIndex = 0u;
                visibilityIndex < 2u;
                ++visibilityIndex)
            {
                const bool visibilitySuccess = visibilityIndex != 0u;
                if (visibilitySuccess && !visibilityRequested)
                    continue;
                for (uint32_t directionalIndex = 0u;
                    directionalIndex < 2u;
                    ++directionalIndex)
                {
                    const bool directionalActive =
                        directionalIndex != 0u;
                    if (directionalActive &&
                        !predictedDirectionalActive)
                    {
                        continue;
                    }
                    const uint32_t outcomeIndex =
                        (directionalActive ? 2u : 0u) |
                        (visibilitySuccess ? 1u : 0u);
                    CompactShadePreparedFrame::Impl::LightingVariant&
                        outcome =
                            viewState.lightingVariants[outcomeIndex];
                    outcome.deferredConstants = deferredBase;
                    outcome.deferredConstants.separateIndirect =
                        visibilitySuccess && parameters.separateIndirect
                            ? 1
                            : 0;
                    outcome.deferredConstants
                        .directionalVisibilityLightIndex =
                            directionalActive
                                ? predictedDirectionalLightIndex
                                : -1;
                    outcome.deferredConstants.deferred.view =
                        viewState.currentConstants;

                    const Impl::Pipeline& outcomePipeline =
                        GetLightingPipeline(visibilitySuccess);
                    const nvrhi::TextureHandle&
                        directionalVisibilityBinding =
                            directionalActive
                                ? activeDirectionalVisibility
                                : neutralDirectionalVisibility;
                    nvrhi::BindingSetDesc lightingBindings;
                    lightingBindings.bindings = {
                        nvrhi::BindingSetItem::ConstantBuffer(
                            0u,
                            implementation->deferredConstantsBuffer),
                        nvrhi::BindingSetItem::ConstantBuffer(
                            CompactBindingConstants,
                            implementation->compactConstantsBuffer),
                        nvrhi::BindingSetItem::Texture_SRV(
                            0u, shadowBinding),
                        nvrhi::BindingSetItem::Texture_SRV(
                            1u, diffuseEnvironment),
                        nvrhi::BindingSetItem::Texture_SRV(
                            2u, specularEnvironment),
                        nvrhi::BindingSetItem::Texture_SRV(
                            3u, environmentBrdf),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingDepth,
                            parameters.lightingInputs.depth,
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingResolvedBackground,
                            parameters.lightingInputs.resolvedBackground,
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingDirectionalVisibility,
                            directionalVisibilityBinding),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingSurfaceOwnership,
                            parameters.lightingInputs.surfaceOwnership,
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingVisibilityClass,
                            parameters.lightingInputs.visibilityClass,
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingFallbackDiffuse,
                            FallbackTextureOrDepth(
                                parameters.lightingInputs.fallbackGBuffer
                                    .diffuse,
                                parameters.lightingInputs.depth),
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingFallbackMaterial,
                            FallbackTextureOrDepth(
                                parameters.lightingInputs.fallbackGBuffer
                                    .material,
                                parameters.lightingInputs.depth),
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingFallbackNormals,
                            FallbackTextureOrDepth(
                                parameters.lightingInputs.fallbackGBuffer
                                    .normals,
                                parameters.lightingInputs.depth),
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingFallbackEmissive,
                            FallbackTextureOrDepth(
                                parameters.lightingInputs.fallbackGBuffer
                                    .emissive,
                                parameters.lightingInputs.depth),
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingFallbackAmbientOcclusion,
                            FallbackTextureOrDepth(
                                parameters.lightingInputs.fallbackGBuffer
                                    .materialAmbientOcclusion,
                                parameters.lightingInputs.depth),
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::Texture_SRV(
                            CompactBindingFallbackMotionVectors,
                            FallbackTextureOrDepth(
                                parameters.lightingInputs.fallbackGBuffer
                                    .motionVectors,
                                parameters.lightingInputs.depth),
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::StructuredBuffer_SRV(
                            CompactBindingGeometryInstances,
                            parameters.snapshot
                                ->GeometryInstanceBuffer()),
                        nvrhi::BindingSetItem::StructuredBuffer_SRV(
                            CompactBindingGeometry,
                            parameters.snapshot->GeometryBuffer()),
                        nvrhi::BindingSetItem::StructuredBuffer_SRV(
                            CompactBindingInstanceHandles,
                            parameters.snapshot
                                ->InstanceHandleBuffer()),
                        nvrhi::BindingSetItem::Texture_UAV(
                            0u, parameters.lightingInputs.output,
                            nvrhi::Format::UNKNOWN,
                            viewState.subresources),
                        nvrhi::BindingSetItem::Sampler(
                            0u,
                            m_Impl->commonPasses
                                ->m_AnisotropicWrapSampler),
                        nvrhi::BindingSetItem::Sampler(
                            1u, m_Impl->shadowSamplerComparison),
                        nvrhi::BindingSetItem::Sampler(
                            2u,
                            m_Impl->commonPasses->m_LinearWrapSampler),
                        nvrhi::BindingSetItem::Sampler(
                            3u,
                            m_Impl->commonPasses->m_LinearClampSampler)
                    };
                    if (visibilitySuccess)
                    {
                        lightingBindings.bindings.push_back(
                            nvrhi::BindingSetItem::Texture_SRV(
                                CompactBindingVisibilityBase,
                                parameters.lightingInputs
                                    .visibilityBaseLighting,
                                nvrhi::Format::UNKNOWN,
                                viewState.subresources));
                        lightingBindings.bindings.push_back(
                            nvrhi::BindingSetItem::Texture_SRV(
                                CompactBindingVisibilityComposite,
                                parameters.lightingInputs
                                    .visibilityComposite,
                                nvrhi::Format::UNKNOWN,
                                viewState.subresources));
                    }
                    if (adaptiveDiagnostics)
                    {
                        lightingBindings.bindings.push_back(
                            nvrhi::BindingSetItem::ConstantBuffer(
                                CompactBindingDiagnosticsConstants,
                                implementation
                                    ->diagnosticsConstantsBuffer));
                        lightingBindings.bindings.push_back(
                            nvrhi::BindingSetItem::Texture_SRV(
                                CompactBindingDiagnosticsBackground,
                                diagnosticsProducer
                                    ->multisampledBackground,
                                nvrhi::Format::UNKNOWN,
                                viewState.subresources));
                        lightingBindings.bindings.push_back(
                            nvrhi::BindingSetItem::Texture_UAV(
                                1u,
                                diagnosticsProducer->sampleStatus));
                        lightingBindings.bindings.push_back(
                            nvrhi::BindingSetItem
                                ::StructuredBuffer_UAV(
                                    2u,
                                    diagnosticsProducer
                                        ->selectedSampleData));
                        lightingBindings.bindings.push_back(
                            nvrhi::BindingSetItem
                                ::StructuredBuffer_UAV(
                                    3u,
                                    diagnosticsProducer
                                        ->pixelMetadata));
                    }
                    outcome.bindings =
                        m_Impl->device->createBindingSet(
                            lightingBindings,
                            outcomePipeline.fixedBindingLayout);
                    if (!outcome.bindings)
                    {
                        return Reject(
                            FallbackReason::ReconstructionFailure);
                    }
                    outcome.pipeline = outcomePipeline.pso;
                    outcome.available = true;
                }
            }
            viewState.dispatchX =
                (uint32_t(viewState.extent.width()) +
                    CompactShadeThreadGroupWidth - 1u) /
                CompactShadeThreadGroupWidth;
            viewState.dispatchY =
                (uint32_t(viewState.extent.height()) +
                    CompactShadeThreadGroupHeight - 1u) /
                CompactShadeThreadGroupHeight;
            implementation->views.push_back(std::move(viewState));
        }

        prepared.m_Impl = std::move(implementation);
        prepared.m_Reason = FallbackReason::None;
        return prepared;
    }

    FallbackReason AdaptiveMsaaCompactShadePass::RevalidatePreparedFrame(
        const CompactShadePreparedFrame& prepared,
        const CompactRasterPreparedSubmission& raster) const
    {
        if (!prepared.IsSubmittable() || !prepared.m_Impl ||
            prepared.m_Impl->pass != this ||
            prepared.m_Impl->phase !=
                CompactShadePreparedPhase::Prepared ||
            !raster.IsSubmittable() || !prepared.m_Impl->snapshot)
        {
            return FallbackReason::ReconstructionFailure;
        }
        prepared.m_Impl->validatedForSubmission = false;
        const CompactSceneSnapshotVersion currentVersion =
            prepared.m_Impl->snapshot->Version();
        if (currentVersion.topologyEpoch !=
                prepared.m_Impl->snapshotVersion.topologyEpoch ||
            currentVersion.buildSerial !=
                prepared.m_Impl->snapshotVersion.buildSerial ||
            prepared.m_Impl->snapshot->ValidateReadResources() !=
                FallbackReason::None)
        {
            return FallbackReason::TransientMappingFailure;
        }

        const std::vector<CompactRasterViewProof>& proofs =
            raster.ViewProofs();
        if (!prepared.m_Impl->currentComposite ||
            proofs.size() != prepared.m_Impl->views.size() ||
            prepared.m_Impl->currentComposite->GetNumChildViews(
                ViewType::PLANAR) != proofs.size() ||
            (prepared.m_Impl->previousComposite &&
                prepared.m_Impl->previousComposite->GetNumChildViews(
                    ViewType::PLANAR) != proofs.size()))
        {
            return FallbackReason::TransientMappingFailure;
        }

        for (size_t viewIndex = 0u;
            viewIndex < prepared.m_Impl->views.size();
            ++viewIndex)
        {
            const CompactShadePreparedFrame::Impl::View& captured =
                prepared.m_Impl->views[viewIndex];
            if (!SameRasterViewProof(
                    captured.rasterProof, proofs[viewIndex]))
            {
                return FallbackReason::TransientMappingFailure;
            }
            const IView* current = prepared.m_Impl->currentComposite
                ->GetChildView(
                    ViewType::PLANAR, uint32_t(viewIndex));
            const IView* previous = prepared.m_Impl->previousComposite
                ? prepared.m_Impl->previousComposite->GetChildView(
                    ViewType::PLANAR, uint32_t(viewIndex))
                : current;
            if (!current || !previous ||
                current != captured.current ||
                previous != captured.previous)
            {
                return FallbackReason::TransientMappingFailure;
            }

            PlanarViewConstants currentConstants{};
            PlanarViewConstants previousConstants{};
            current->FillPlanarViewConstants(currentConstants);
            previous->FillPlanarViewConstants(previousConstants);
            const nvrhi::ViewportState viewport =
                current->GetViewportState();
            const nvrhi::TextureSubresourceSet subresources =
                current->GetSubresources();
            const nvrhi::Rect extent = current->GetViewExtent();
            const bool mirrored = current->IsMirrored();
            if (!SamePlanarViewConstants(
                    captured.currentConstants, currentConstants) ||
                !SamePlanarViewConstants(
                    captured.previousConstants, previousConstants) ||
                !SameCompactRasterViewportState(
                    captured.viewport, viewport) ||
                captured.subresources != subresources ||
                !SameRect(captured.extent, extent) ||
                captured.mirrored != mirrored ||
                !SameCompactRasterViewTransformIdentity(
                    captured.rasterProof.transformIdentity,
                    CaptureRasterIdentity(currentConstants)) ||
                !ViewExtentMatchesViewport(
                    extent, viewport, currentConstants))
            {
                return FallbackReason::TransientMappingFailure;
            }
            const CompactShadeViewReconstructionProof inverseProof =
                EvaluateCompactShadeViewReconstructionProof(
                    MakeViewReconstructionProofInput(
                        currentConstants, captured.rasterProof));
            if (!inverseProof.safe ||
                inverseProof.reason != FallbackReason::None)
            {
                return inverseProof.reason;
            }
        }
        prepared.m_Impl->validatedForSubmission = true;
        return FallbackReason::None;
    }

    CompactShadeDispatchResult
        AdaptiveMsaaCompactShadePass::ResolveClosestSurface(
            nvrhi::ICommandList* commandList,
            CompactShadePreparedFrame& prepared)
    {
        CompactShadeDispatchResult result;
        if (!commandList || !prepared.IsSubmittable() ||
            !prepared.m_Impl || prepared.m_Impl->pass != this ||
            !prepared.m_Impl->guideRequired ||
            prepared.m_Impl->phase !=
                CompactShadePreparedPhase::Prepared ||
            !prepared.m_Impl->validatedForSubmission)
        {
            prepared.m_Impl.reset();
            prepared.m_Reason = FallbackReason::ReconstructionFailure;
            return result;
        }

        CompactShadePreparedFrame::Impl& implementation =
            *prepared.m_Impl;
        if (!implementation.compactConstantsBuffer ||
            !implementation.descriptorTableKeepAlive)
        {
            prepared.m_Impl.reset();
            prepared.m_Reason = FallbackReason::ReconstructionFailure;
            return result;
        }
        for (const CompactShadePreparedFrame::Impl::View& view :
            implementation.views)
        {
            if (!view.guidePipeline || !view.guideBindings)
            {
                prepared.m_Impl.reset();
                prepared.m_Reason =
                    FallbackReason::ReconstructionFailure;
                return result;
            }
        }
        const CompactShadePreparedPhaseTransition phaseTransition =
            AdvanceCompactShadePreparedPhase(
                implementation.phase,
                CompactShadePreparedPhase::GuideRecorded);
        if (!phaseTransition.Succeeded())
        {
            prepared.m_Impl.reset();
            prepared.m_Reason = phaseTransition.reason;
            return result;
        }
        implementation.phase = phaseTransition.phase;
        commandList->beginMarker(
            "Adaptive MSAA Compact Closest-Surface Reconstruction");
        for (const CompactShadePreparedFrame::Impl::View& view :
            implementation.views)
        {
            commandList->writeBuffer(
                implementation.compactConstantsBuffer,
                &view.compactConstants,
                sizeof(view.compactConstants));
            nvrhi::ComputeState state;
            state.pipeline = view.guidePipeline;
            state.bindings = {
                view.guideBindings,
                implementation.descriptorTableKeepAlive
            };
            commandList->setComputeState(state);
            commandList->dispatch(
                view.dispatchX, view.dispatchY);
            ++result.dispatchedViewCount;
        }
        commandList->endMarker();
        result.reason = FallbackReason::None;
        return result;
    }

    CompactShadeDispatchResult
        AdaptiveMsaaCompactShadePass::RenderLighting(
            nvrhi::ICommandList* commandList,
            CompactShadePreparedFrame&& prepared,
            const DirectionalLightVisibility& actualDirectionalVisibility,
            bool finalVisibilitySuccess)
    {
        CompactShadeDispatchResult result;
        std::unique_ptr<CompactShadePreparedFrame::Impl> implementation =
            std::move(prepared.m_Impl);
        const FallbackReason preparedReason = prepared.m_Reason;
        prepared.m_Reason = FallbackReason::ReconstructionFailure;
        if (!commandList || preparedReason != FallbackReason::None ||
            !implementation || implementation->pass != this ||
            !implementation->validatedForSubmission ||
            !implementation->compactConstantsBuffer ||
            !implementation->deferredConstantsBuffer ||
            !implementation->descriptorTableKeepAlive ||
            (implementation->diagnostics &&
                !implementation->diagnosticsConstantsBuffer))
        {
            return result;
        }
        const CompactShadeLightingOutcomeSelection outcome =
            SelectCompactShadeLightingOutcome(
                implementation->visibilityRequested,
                finalVisibilitySuccess,
                implementation->predictedDirectionalVisibility,
                actualDirectionalVisibility);
        if (!outcome.Succeeded() ||
            outcome.variantIndex >= CompactShadeLightingOutcomeCount)
        {
            return result;
        }
        const bool allowGuideSkip = !implementation->guideRequired &&
            !outcome.visibilitySuccess && !outcome.directionalActive;
        const CompactShadePreparedPhaseTransition phaseTransition =
            AdvanceCompactShadePreparedPhase(
                implementation->phase,
                CompactShadePreparedPhase::Consumed,
                allowGuideSkip);
        if (!phaseTransition.Succeeded())
            return result;
        for (const CompactShadePreparedFrame::Impl::View& view :
            implementation->views)
        {
            const auto& variant =
                view.lightingVariants[outcome.variantIndex];
            if (!variant.available || !variant.pipeline ||
                !variant.bindings)
            {
                return result;
            }
        }
        implementation->phase = phaseTransition.phase;

        commandList->beginMarker(
            implementation->diagnostics
                ? "PBR Deferred Adaptive Compact Visibility MSAA Lighting Diagnostics"
                : "PBR Deferred Adaptive Compact Visibility MSAA Lighting");
        for (const CompactShadePreparedFrame::Impl::View& view :
            implementation->views)
        {
            const auto& variant =
                view.lightingVariants[outcome.variantIndex];
            commandList->writeBuffer(
                implementation->compactConstantsBuffer,
                &view.compactConstants,
                sizeof(view.compactConstants));
            commandList->writeBuffer(
                implementation->deferredConstantsBuffer,
                &variant.deferredConstants,
                sizeof(variant.deferredConstants));
            if (implementation->diagnostics)
            {
                commandList->writeBuffer(
                    implementation->diagnosticsConstantsBuffer,
                    &view.diagnosticsConstants,
                    sizeof(view.diagnosticsConstants));
            }
            nvrhi::ComputeState state;
            state.pipeline = variant.pipeline;
            state.bindings = {
                variant.bindings,
                implementation->descriptorTableKeepAlive
            };
            commandList->setComputeState(state);
            commandList->dispatch(
                view.dispatchX, view.dispatchY);
            ++result.dispatchedViewCount;
        }
        commandList->endMarker();
        result.reason = FallbackReason::None;
        const CompactShadeDiagnosticsPublication publication =
            ResolveCompactShadeDiagnosticsPublication(
                implementation->diagnostics,
                outcome.Succeeded(),
                result.dispatchedViewCount,
                uint32_t(implementation->views.size()),
                implementation->diagnosticsCapabilityMask);
        if (publication.produced)
        {
            if (implementation->diagnosticsProducer)
            {
                implementation->diagnosticsProducer->producedThisFrame =
                    true;
                implementation->diagnosticsProducer
                    ->producedCapabilityMask =
                    publication.capabilityMask;
            }
            result.diagnosticsProduced = true;
            result.diagnosticsCapabilityMask =
                publication.capabilityMask;
        }
        return result;
    }

    CompactShadeDispatchResult
        AdaptiveMsaaCompactShadePass::ResolveClosestSurface(
            nvrhi::ICommandList* commandList,
            const std::shared_ptr<const CompactSceneSnapshot>& snapshot,
            const ICompositeView& currentView,
            const ICompositeView* previousView,
            const AdaptiveMsaaCompactGuideInputs& inputs,
            const AdaptiveMsaaCompactGuideOutputs& outputs,
            uint32_t sampleCount)
    {
        CompactShadeDispatchResult result;
        if (!commandList)
            return result;
        result.reason = ValidateGuideDispatch(
            snapshot,
            currentView,
            previousView,
            inputs,
            outputs,
            sampleCount);
        if (result.reason != FallbackReason::None)
            return result;
        result.reason = snapshot->PrepareReadStates(commandList);
        if (result.reason != FallbackReason::None)
            return result;

        const size_t sampleVariant = size_t(GetSampleVariant(sampleCount));
        const Impl::Pipeline& pipeline =
            m_Impl->resolvePipelines[sampleVariant];
        const uint32_t viewCount =
            currentView.GetNumChildViews(ViewType::PLANAR);
        commandList->beginMarker(
            "Adaptive MSAA Compact Closest-Surface Reconstruction");
        for (uint32_t viewIndex = 0u;
            viewIndex < viewCount;
            ++viewIndex)
        {
            const IView* view = currentView.GetChildView(
                ViewType::PLANAR, viewIndex);
            const IView* viewPrev = previousView
                ? previousView->GetChildView(ViewType::PLANAR, viewIndex)
                : view;
            const nvrhi::TextureSubresourceSet subresources =
                view->GetSubresources();
            const nvrhi::Rect extent = view->GetViewExtent();

            CompactShadeConstants constants{};
            view->FillPlanarViewConstants(constants.gbuffer.view);
            viewPrev->FillPlanarViewConstants(constants.gbuffer.viewPrev);
            constants.scene = snapshot->GpuConstants();
            constants.snapshotBuildSerialLow =
                LowWord(snapshot->BuildSerial());
            constants.snapshotBuildSerialHigh =
                HighWord(snapshot->BuildSerial());
            constants.sampleCount = sampleCount;
            constants.frontCounterClockwise =
                view->IsMirrored() ? 1u : 0u;
            commandList->writeBuffer(
                m_Impl->compactConstantsBuffer,
                &constants,
                sizeof(constants));

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    CompactBindingConstants,
                    m_Impl->compactConstantsBuffer),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingDepth,
                    inputs.depth,
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingSurfaceOwnership,
                    inputs.surfaceOwnership,
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingVisibilityClass,
                    inputs.visibilityClass,
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackDiffuse,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.diffuse, inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackMaterial,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.material, inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackNormals,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.normals, inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackEmissive,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.emissive, inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackAmbientOcclusion,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.materialAmbientOcclusion,
                        inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackMotionVectors,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.motionVectors,
                        inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    CompactBindingGeometryInstances,
                    snapshot->GeometryInstanceBuffer()),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    CompactBindingGeometry,
                    snapshot->GeometryBuffer()),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    CompactBindingInstanceHandles,
                    snapshot->InstanceHandleBuffer()),
                nvrhi::BindingSetItem::Texture_UAV(
                    0u, outputs.depth,
                    nvrhi::Format::UNKNOWN, subresources),
                nvrhi::BindingSetItem::Texture_UAV(
                    1u, outputs.diffuse,
                    nvrhi::Format::UNKNOWN, subresources),
                nvrhi::BindingSetItem::Texture_UAV(
                    2u, outputs.material,
                    nvrhi::Format::UNKNOWN, subresources),
                nvrhi::BindingSetItem::Texture_UAV(
                    3u, outputs.normals,
                    nvrhi::Format::UNKNOWN, subresources),
                nvrhi::BindingSetItem::Texture_UAV(
                    4u, outputs.emissive,
                    nvrhi::Format::UNKNOWN, subresources),
                nvrhi::BindingSetItem::Texture_UAV(
                    5u, outputs.materialAmbientOcclusion,
                    nvrhi::Format::UNKNOWN, subresources),
                nvrhi::BindingSetItem::Texture_UAV(
                    6u, outputs.motionVectors,
                    nvrhi::Format::UNKNOWN, subresources),
                nvrhi::BindingSetItem::Sampler(
                    0u,
                    m_Impl->commonPasses->m_AnisotropicWrapSampler)
            };
            nvrhi::BindingSetHandle bindingSet =
                m_Impl->bindingSets.GetOrCreateBindingSet(
                    bindingSetDesc,
                    pipeline.fixedBindingLayout);
            if (!bindingSet)
            {
                result.reason = FallbackReason::ReconstructionFailure;
                break;
            }

            nvrhi::ComputeState state;
            state.pipeline = pipeline.pso;
            state.bindings = {
                bindingSet,
                snapshot->DescriptorTable()
            };
            commandList->setComputeState(state);
            commandList->dispatch(
                (uint32_t(extent.width()) +
                    CompactShadeThreadGroupWidth - 1u) /
                    CompactShadeThreadGroupWidth,
                (uint32_t(extent.height()) +
                    CompactShadeThreadGroupHeight - 1u) /
                    CompactShadeThreadGroupHeight);
            ++result.dispatchedViewCount;
        }
        commandList->endMarker();
        return result;
    }

    CompactShadeDispatchResult
        AdaptiveMsaaCompactShadePass::RenderLighting(
            nvrhi::ICommandList* commandList,
            const std::shared_ptr<const CompactSceneSnapshot>& snapshot,
            const ICompositeView& currentView,
            const ICompositeView* previousView,
            const AdaptiveMsaaCompactLightingInputs& inputs,
            uint32_t sampleCount,
            const DirectionalLightVisibility& directionalVisibility,
            const LightProbe* environment,
            bool separateIndirect,
            uint32_t lightingDebugView,
            uint32_t visibilityDebugView,
            float randomOffsetX,
            float randomOffsetY)
    {
        CompactShadeDispatchResult result;
        AdaptiveMsaaDiagnosticsProducerOutputs* diagnosticsProducer =
            inputs.diagnosticsProducer;
        if (diagnosticsProducer)
            diagnosticsProducer->ResetProducedState();
        if (!commandList)
            return result;
        result.reason = ValidateLightingDispatch(
            snapshot,
            currentView,
            previousView,
            inputs,
            sampleCount);
        if (result.reason != FallbackReason::None)
            return result;

        const bool applyVisibility = inputs.visibilityBaseLighting &&
            inputs.visibilityComposite;
        if (separateIndirect && !applyVisibility)
        {
            result.reason = FallbackReason::ReconstructionFailure;
            return result;
        }
        result.reason = snapshot->PrepareReadStates(commandList);
        if (result.reason != FallbackReason::None)
            return result;

        bool adaptiveDiagnostics = diagnosticsProducer &&
            diagnosticsProducer->Requested() &&
            ValidateDiagnosticsProducer(
                *diagnosticsProducer, inputs, sampleCount);
        if (diagnosticsProducer && diagnosticsProducer->Requested() &&
            !adaptiveDiagnostics)
        {
            log::error(
                "Adaptive compact diagnostics producer resources are "
                "incomplete; rendering with the non-diagnostic compact "
                "permutation");
        }
        if (adaptiveDiagnostics &&
            !m_Impl->diagnosticsProducerConstantsBuffer)
        {
            nvrhi::BufferDesc diagnosticsDesc;
            diagnosticsDesc.byteSize =
                sizeof(DiagnosticsProducerGpuConstants);
            diagnosticsDesc.debugName =
                "AdaptiveMsaaCompactDiagnosticsProducerConstants";
            diagnosticsDesc.isConstantBuffer = true;
            diagnosticsDesc.isVolatile = true;
            diagnosticsDesc.maxVersions =
                c_MaxRenderPassConstantBufferVersions;
            m_Impl->diagnosticsProducerConstantsBuffer =
                m_Impl->device->createBuffer(diagnosticsDesc);
            if (!m_Impl->diagnosticsProducerConstantsBuffer)
                adaptiveDiagnostics = false;
        }

        DirectionalLightVisibility activeVisibility;
        const nvrhi::TextureDesc& outputDesc = inputs.output->getDesc();
        if (directionalVisibility.IsComplete())
        {
            DirectionalLightVisibilityTextureProperties properties;
            const nvrhi::TextureDesc& visibilityDesc =
                directionalVisibility.texture->getDesc();
            properties.width = visibilityDesc.width;
            properties.height = visibilityDesc.height;
            properties.depth = visibilityDesc.depth;
            properties.arraySize = visibilityDesc.arraySize;
            properties.mipLevels = visibilityDesc.mipLevels;
            properties.sampleCount = visibilityDesc.sampleCount;
            properties.r8Unorm =
                visibilityDesc.format == nvrhi::Format::R8_UNORM;
            properties.texture2D =
                visibilityDesc.dimension ==
                    nvrhi::TextureDimension::Texture2D;
            properties.shaderResource = visibilityDesc.isShaderResource;
            if (IsDirectionalLightVisibilityTextureCompatible(
                    properties,
                    outputDesc.width,
                    outputDesc.height))
            {
                activeVisibility = directionalVisibility;
            }
        }

        PbrDeferredLightingConstants deferred{};
        DeferredLightingConstants& lighting = deferred.deferred;
        deferred.separateIndirect = separateIndirect ? 1 : 0;
        deferred.lightingDebugView = lightingDebugView;
        deferred.visibilityDebugView = visibilityDebugView;
        deferred.directionalVisibilityLightIndex = -1;
        lighting.randomOffset = float2(randomOffsetX, randomOffsetY);
        lighting.noisePattern[0] =
            float4(0.059f, 0.529f, 0.176f, 0.647f);
        lighting.noisePattern[1] =
            float4(0.765f, 0.294f, 0.882f, 0.412f);
        lighting.noisePattern[2] =
            float4(0.235f, 0.706f, 0.118f, 0.588f);
        lighting.noisePattern[3] =
            float4(0.941f, 0.471f, 0.824f, 0.353f);
        lighting.numLightProbes =
            environment && environment->IsActive() ? 1u : 0u;
        if (lighting.numLightProbes > 0u)
            environment->FillLightProbeConstants(lighting.lightProbes[0]);
        lighting.indirectDiffuseScale = 1.f;

        nvrhi::ITexture* shadowMapTexture = nullptr;
        int numShadows = 0;
        if (inputs.lights)
        {
            for (const std::shared_ptr<Light>& light : *inputs.lights)
            {
                if (!light)
                    continue;
                if (light->shadowMap)
                {
                    if (!shadowMapTexture)
                    {
                        shadowMapTexture = light->shadowMap->GetTexture();
                        lighting.shadowMapTextureSize =
                            float2(light->shadowMap->GetTextureSize());
                    }
                    else if (shadowMapTexture !=
                        light->shadowMap->GetTexture())
                    {
                        result.reason =
                            FallbackReason::ReconstructionFailure;
                        return result;
                    }
                }
                if (lighting.numLights >= DEFERRED_MAX_LIGHTS)
                    break;

                LightConstants& lightConstants =
                    lighting.lights[lighting.numLights];
                light->FillLightConstants(lightConstants);
                if (TargetsDirectionalLight(
                        activeVisibility, light.get()))
                {
                    deferred.directionalVisibilityLightIndex =
                        int(lighting.numLights);
                }

                if (light->shadowMap)
                {
                    const uint32_t cascadeCount = std::min(
                        light->shadowMap->GetNumberOfCascades(), 4u);
                    for (uint32_t cascade = 0u;
                        cascade < cascadeCount &&
                            numShadows < DEFERRED_MAX_SHADOWS;
                        ++cascade)
                    {
                        light->shadowMap->GetCascade(cascade)
                            ->FillShadowConstants(
                                lighting.shadows[numShadows]);
                        lightConstants.shadowCascades[cascade] =
                            numShadows++;
                    }

                    const uint32_t objectShadowCount = std::min(
                        light->shadowMap->GetNumberOfPerObjectShadows(),
                        4u);
                    for (uint32_t objectShadow = 0u;
                        objectShadow < objectShadowCount &&
                            numShadows < DEFERRED_MAX_SHADOWS;
                        ++objectShadow)
                    {
                        light->shadowMap
                            ->GetPerObjectShadow(objectShadow)
                            ->FillShadowConstants(
                                lighting.shadows[numShadows]);
                        lightConstants.perObjectShadows[objectShadow] =
                            numShadows++;
                    }
                }
                ++lighting.numLights;
            }
        }

        const size_t sampleVariant = size_t(GetSampleVariant(sampleCount));
        const Impl::Pipeline* pipeline = adaptiveDiagnostics
            ? &m_Impl->diagnosticLightingPipelines
                [applyVisibility ? 1u : 0u][sampleVariant]
            : &m_Impl->lightingPipelines
                [applyVisibility ? 1u : 0u][sampleVariant];
        if (adaptiveDiagnostics && !pipeline->IsValid())
        {
            adaptiveDiagnostics = false;
            pipeline = &m_Impl->lightingPipelines
                [applyVisibility ? 1u : 0u][sampleVariant];
        }
        const uint32_t viewCount =
            currentView.GetNumChildViews(ViewType::PLANAR);
        commandList->beginMarker(
            adaptiveDiagnostics
                ? "PBR Deferred Adaptive Compact Visibility MSAA Lighting Diagnostics"
                : "PBR Deferred Adaptive Compact Visibility MSAA Lighting");
        for (uint32_t viewIndex = 0u;
            viewIndex < viewCount;
            ++viewIndex)
        {
            const IView* view = currentView.GetChildView(
                ViewType::PLANAR, viewIndex);
            const IView* viewPrev = previousView
                ? previousView->GetChildView(ViewType::PLANAR, viewIndex)
                : view;
            const nvrhi::TextureSubresourceSet subresources =
                view->GetSubresources();
            const nvrhi::Rect extent = view->GetViewExtent();

            CompactShadeConstants compact{};
            view->FillPlanarViewConstants(compact.gbuffer.view);
            viewPrev->FillPlanarViewConstants(compact.gbuffer.viewPrev);
            compact.scene = snapshot->GpuConstants();
            compact.snapshotBuildSerialLow =
                LowWord(snapshot->BuildSerial());
            compact.snapshotBuildSerialHigh =
                HighWord(snapshot->BuildSerial());
            compact.sampleCount = sampleCount;
            compact.frontCounterClockwise =
                view->IsMirrored() ? 1u : 0u;
            compact.preparationQueueCapacity =
                inputs.preparationQueueCapacity;
            view->FillPlanarViewConstants(lighting.view);
            commandList->writeBuffer(
                m_Impl->compactConstantsBuffer,
                &compact,
                sizeof(compact));
            commandList->writeBuffer(
                m_Impl->deferredConstantsBuffer,
                &deferred,
                sizeof(deferred));
            if (adaptiveDiagnostics)
            {
                DiagnosticsProducerGpuConstants diagnosticsConstants;
                diagnosticsConstants.executionFlags =
                    diagnosticsProducer->executionFlags;
                diagnosticsConstants.selectedPixelX =
                    diagnosticsProducer->selectedPixelX;
                diagnosticsConstants.selectedPixelY =
                    diagnosticsProducer->selectedPixelY;
                diagnosticsConstants.producerCapabilityMask =
                    diagnosticsProducer->capabilityMask;
                // Producer buffers are full-surface and pixel positions are
                // absolute even when this child view has a nonzero origin.
                diagnosticsConstants.viewportOriginX = 0u;
                diagnosticsConstants.viewportOriginY = 0u;
                diagnosticsConstants.viewportWidth = outputDesc.width;
                diagnosticsConstants.viewportHeight = outputDesc.height;
                commandList->writeBuffer(
                    m_Impl->diagnosticsProducerConstantsBuffer,
                    &diagnosticsConstants,
                    sizeof(diagnosticsConstants));
            }

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0u, m_Impl->deferredConstantsBuffer),
                nvrhi::BindingSetItem::ConstantBuffer(
                    CompactBindingConstants,
                    m_Impl->compactConstantsBuffer),
                nvrhi::BindingSetItem::Texture_SRV(
                    0u,
                    shadowMapTexture
                        ? shadowMapTexture
                        : m_Impl->commonPasses
                            ->m_BlackDepthStencilTexture2DArray.Get()),
                nvrhi::BindingSetItem::Texture_SRV(
                    1u,
                    environment && environment->diffuseMap
                        ? environment->diffuseMap.Get()
                        : m_Impl->commonPasses
                            ->m_BlackCubeMapArray.Get()),
                nvrhi::BindingSetItem::Texture_SRV(
                    2u,
                    environment && environment->specularMap
                        ? environment->specularMap.Get()
                        : m_Impl->commonPasses
                            ->m_BlackCubeMapArray.Get()),
                nvrhi::BindingSetItem::Texture_SRV(
                    3u,
                    environment && environment->environmentBrdf
                        ? environment->environmentBrdf.Get()
                        : m_Impl->commonPasses->m_BlackTexture.Get()),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingDepth,
                    inputs.depth,
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingResolvedBackground,
                    inputs.resolvedBackground,
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingDirectionalVisibility,
                    activeVisibility.texture
                        ? activeVisibility.texture
                        : m_Impl->commonPasses->m_WhiteTexture.Get()),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingSurfaceOwnership,
                    inputs.surfaceOwnership,
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingVisibilityClass,
                    inputs.visibilityClass,
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackDiffuse,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.diffuse, inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackMaterial,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.material, inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackNormals,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.normals, inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackEmissive,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.emissive, inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackAmbientOcclusion,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.materialAmbientOcclusion,
                        inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Texture_SRV(
                    CompactBindingFallbackMotionVectors,
                    FallbackTextureOrDepth(
                        inputs.fallbackGBuffer.motionVectors,
                        inputs.depth),
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    CompactBindingGeometryInstances,
                    snapshot->GeometryInstanceBuffer()),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    CompactBindingGeometry,
                    snapshot->GeometryBuffer()),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    CompactBindingInstanceHandles,
                    snapshot->InstanceHandleBuffer()),
                nvrhi::BindingSetItem::Texture_UAV(
                    0u,
                    inputs.output,
                    nvrhi::Format::UNKNOWN,
                    subresources),
                nvrhi::BindingSetItem::Sampler(
                    0u,
                    m_Impl->commonPasses->m_AnisotropicWrapSampler),
                nvrhi::BindingSetItem::Sampler(
                    1u, m_Impl->shadowSamplerComparison),
                nvrhi::BindingSetItem::Sampler(
                    2u, m_Impl->commonPasses->m_LinearWrapSampler),
                nvrhi::BindingSetItem::Sampler(
                    3u, m_Impl->commonPasses->m_LinearClampSampler)
            };
            if (applyVisibility)
            {
                bindingSetDesc.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingVisibilityBase,
                        inputs.visibilityBaseLighting,
                        nvrhi::Format::UNKNOWN,
                        subresources));
                bindingSetDesc.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingVisibilityComposite,
                        inputs.visibilityComposite,
                        nvrhi::Format::UNKNOWN,
                        subresources));
            }
            if (adaptiveDiagnostics)
            {
                bindingSetDesc.bindings.push_back(
                    nvrhi::BindingSetItem::ConstantBuffer(
                        CompactBindingDiagnosticsConstants,
                        m_Impl->diagnosticsProducerConstantsBuffer));
                bindingSetDesc.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_SRV(
                        CompactBindingDiagnosticsBackground,
                        diagnosticsProducer->multisampledBackground,
                        nvrhi::Format::UNKNOWN,
                        subresources));
                bindingSetDesc.bindings.push_back(
                    nvrhi::BindingSetItem::Texture_UAV(
                        1u, diagnosticsProducer->sampleStatus));
                bindingSetDesc.bindings.push_back(
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(
                        2u, diagnosticsProducer->selectedSampleData));
                bindingSetDesc.bindings.push_back(
                    nvrhi::BindingSetItem::StructuredBuffer_UAV(
                        3u, diagnosticsProducer->pixelMetadata));
            }

            nvrhi::BindingSetHandle bindingSet =
                m_Impl->bindingSets.GetOrCreateBindingSet(
                    bindingSetDesc,
                    pipeline->fixedBindingLayout);
            if (!bindingSet)
            {
                result.reason = FallbackReason::ReconstructionFailure;
                break;
            }
            nvrhi::ComputeState state;
            state.pipeline = pipeline->pso;
            state.bindings = {
                bindingSet,
                snapshot->DescriptorTable()
            };
            commandList->setComputeState(state);
            commandList->dispatch(
                (uint32_t(extent.width()) +
                    CompactShadeThreadGroupWidth - 1u) /
                    CompactShadeThreadGroupWidth,
                (uint32_t(extent.height()) +
                    CompactShadeThreadGroupHeight - 1u) /
                    CompactShadeThreadGroupHeight);
            ++result.dispatchedViewCount;
        }
        commandList->endMarker();
        if (result.reason == FallbackReason::None && adaptiveDiagnostics)
        {
            diagnosticsProducer->producedThisFrame = true;
            diagnosticsProducer->producedCapabilityMask =
                diagnosticsProducer->capabilityMask;
            result.diagnosticsProduced = true;
            result.diagnosticsCapabilityMask =
                diagnosticsProducer->capabilityMask;
        }
        return result;
    }

    void AdaptiveMsaaCompactShadePass::ResetBindingCache()
    {
        m_Impl->bindingSets.Clear();
    }

    void AdaptiveMsaaCompactShadePass::ResetAdaptiveDiagnosticsResources()
    {
        // The producer constants buffer is topology-independent and must be
        // precreated before any accepted command list opens. Binding sets are
        // still discarded because their diagnostic UAVs are frame resources.
        m_Impl->bindingSets.Clear();
    }
}
