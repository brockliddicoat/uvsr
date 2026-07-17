#include "radial_visibility_mask.h"
#include "visibility_estimator_cpu.h"
#include "visibility_performance_plan.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace uvsr;

    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Visibility performance plan validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    constexpr uint64_t ResourceBit(VisibilityExecutionResource resource)
    {
        return static_cast<uint64_t>(resource);
    }

    constexpr uint64_t BindingBit(VisibilityExecutionBinding binding)
    {
        return static_cast<uint64_t>(binding);
    }

    constexpr uint64_t PassBit(VisibilityExecutionPass pass)
    {
        return static_cast<uint64_t>(pass);
    }

    uint32_t CountBits(uint64_t value)
    {
        uint32_t count = 0u;
        while (value != 0u)
        {
            value &= value - 1u;
            ++count;
        }
        return count;
    }

    enum class EdgeReferenceMode
    {
        DepthOnly,
        DepthAndNormal,
        SlopeAdjustedDepthAndNormal
    };

    bool IsDeviceDepthValidReference(float depth, bool reverseDepth)
    {
        if (!std::isfinite(depth))
            return false;
        return reverseDepth
            ? depth > 0.0f && depth <= 1.0f
            : depth >= 0.0f && depth < 1.0f;
    }

    float Saturate(float value)
    {
        return std::clamp(value, 0.0f, 1.0f);
    }

    uint8_t PackEdgeReference(const std::array<float, 4>& continuity)
    {
        std::array<uint8_t, 4> quantized{};
        for (size_t index = 0u; index < quantized.size(); ++index)
        {
            quantized[index] = static_cast<uint8_t>(std::lround(
                Saturate(continuity[index]) * 3.0f));
        }
        return uint8_t((quantized[0] << 6u) |
            (quantized[1] << 4u) |
            (quantized[2] << 2u) | quantized[3]);
    }

    uint8_t ComputePackedEdgesReference(
        float receiverLinearDepth,
        const std::array<float, 4>& neighborLinearDepth,
        const std::array<float, 4>& receiverNeighborNormalDot,
        const std::array<bool, 4>& neighborValid,
        EdgeReferenceMode mode)
    {
        std::array<float, 4> depthDiscontinuity{};
        std::array<float, 4> normalDiscontinuity{};
        for (size_t index = 0u; index < depthDiscontinuity.size(); ++index)
        {
            if (!neighborValid[index])
            {
                depthDiscontinuity[index] = 1.0f;
                normalDiscontinuity[index] = 1.0f;
                continue;
            }
            depthDiscontinuity[index] = Saturate(std::abs(
                neighborLinearDepth[index] - receiverLinearDepth) /
                std::max(receiverLinearDepth * 0.08f, 0.01f));
            if (mode != EdgeReferenceMode::DepthOnly)
            {
                normalDiscontinuity[index] = Saturate(
                    (1.0f - receiverNeighborNormalDot[index]) * 4.0f);
            }
        }
        if (mode == EdgeReferenceMode::SlopeAdjustedDepthAndNormal)
        {
            const float horizontalSlope = std::min(
                depthDiscontinuity[0], depthDiscontinuity[1]);
            const float verticalSlope = std::min(
                depthDiscontinuity[2], depthDiscontinuity[3]);
            depthDiscontinuity[0] = Saturate(
                depthDiscontinuity[0] - horizontalSlope);
            depthDiscontinuity[1] = Saturate(
                depthDiscontinuity[1] - horizontalSlope);
            depthDiscontinuity[2] = Saturate(
                depthDiscontinuity[2] - verticalSlope);
            depthDiscontinuity[3] = Saturate(
                depthDiscontinuity[3] - verticalSlope);
        }
        std::array<float, 4> continuity{};
        for (size_t index = 0u; index < continuity.size(); ++index)
        {
            continuity[index] = 1.0f - std::max(
                depthDiscontinuity[index], normalDiscontinuity[index]);
        }
        return PackEdgeReference(continuity);
    }

    std::array<uint32_t, 2> SamplingToFullPixelReference(
        std::array<uint32_t, 2> samplingPixel,
        uint32_t scale,
        std::array<uint32_t, 2> fullSize)
    {
        scale = std::max(scale, 1u);
        return {
            std::min(samplingPixel[0] * scale + scale / 2u,
                fullSize[0] - 1u),
            std::min(samplingPixel[1] * scale + scale / 2u,
                fullSize[1] - 1u)
        };
    }

    bool HasAo(VisibilityPerformanceConsumer consumer)
    {
        return consumer == VisibilityPerformanceConsumer::AmbientOcclusion ||
            consumer == VisibilityPerformanceConsumer::
                AmbientOcclusionAndIndirectDiffuse;
    }

    bool HasGi(VisibilityPerformanceConsumer consumer)
    {
        return consumer == VisibilityPerformanceConsumer::IndirectDiffuse ||
            consumer == VisibilityPerformanceConsumer::
                AmbientOcclusionAndIndirectDiffuse;
    }

    VisibilityPerformanceWorkload MakeCompatibleWorkload(
        VisibilityPerformanceProfile profile)
    {
        VisibilityPerformanceWorkload workload;
        switch (profile)
        {
        case VisibilityPerformanceProfile::ExactFixed12:
            workload.firstBounceSampleCount = 12u;
            break;
        case VisibilityPerformanceProfile::ExactFixed16:
            workload.firstBounceSampleCount = 16u;
            break;
        case VisibilityPerformanceProfile::ExactFixed20:
            workload.firstBounceSampleCount = 20u;
            break;
        case VisibilityPerformanceProfile::ExactFixed8FusedResolveApply:
            workload.firstBounceSampleCount = 8u;
            break;
        case VisibilityPerformanceProfile::ExactFixedLaterBounce8:
            workload.consumer =
                VisibilityPerformanceConsumer::IndirectDiffuse;
            workload.bounceCount = 2u;
            break;
        case VisibilityPerformanceProfile::ExactFixedAllBounce8:
            workload.consumer = VisibilityPerformanceConsumer::
                AmbientOcclusionAndIndirectDiffuse;
            workload.bounceCount = 2u;
            break;
        case VisibilityPerformanceProfile::ExactPackedCurrentFast:
            workload.scheduler = VisibilityPerformanceScheduler::
                FilterAdaptedSpatiotemporalRankField;
            break;
        case VisibilityPerformanceProfile::ExactGroup16x8:
            workload.threadGroupSizeX = 16u;
            break;
        case VisibilityPerformanceProfile::ExactGroup8x16:
            workload.threadGroupSizeY = 16u;
            break;
        case VisibilityPerformanceProfile::DiagnosticConstantScheduler:
            workload.scheduler =
                VisibilityPerformanceScheduler::ConstantDiagnostic;
            break;
        case VisibilityPerformanceProfile::DiagnosticTemporalCopy:
            workload.temporalEnabled = true;
            break;
        case VisibilityPerformanceProfile::AlgorithmicActivisionSchedule:
            workload.scheduler =
                VisibilityPerformanceScheduler::Activision4x4SixPhase;
            break;
        case VisibilityPerformanceProfile::ActivisionPs4Schedule:
        case VisibilityPerformanceProfile::ActivisionPs4PackedGather:
            workload.firstBounceSampleCount = 8u;
            workload.scheduler =
                VisibilityPerformanceScheduler::Activision4x4SixPhase;
            workload.radialExponent = 1.0f;
            workload.temporalEnabled = true;
            workload.spatialEnabled = true;
            break;
        case VisibilityPerformanceProfile::XeGtaoClosestMatch:
        case VisibilityPerformanceProfile::XeGtaoHighInlineHilbert:
        case VisibilityPerformanceProfile::XeGtaoHighFp32:
            workload.scheduler =
                VisibilityPerformanceScheduler::XeGtaoHilbertR2;
            workload.resolution = VisibilityPerformanceResolution::Full;
            workload.firstBounceSampleCount = 18u;
            workload.radius = 0.5f;
            workload.thickness = 0.0f;
            workload.spatialEnabled = true;
            break;
        default:
            break;
        }
        return workload;
    }

    uint64_t ExpectedReferenceResources(
        const VisibilityPerformanceWorkload& workload)
    {
        uint64_t mask =
            ResourceBit(VisibilityExecutionResource::LegacyToroidalNoise) |
            ResourceBit(VisibilityExecutionResource::LegacyCurrentFastNoise);
        const bool hasAo = HasAo(workload.consumer);
        const bool hasGi = HasGi(workload.consumer);
        if (hasAo)
            mask |= ResourceBit(VisibilityExecutionResource::RawAmbientR16);
        if (hasGi)
        {
            mask |= ResourceBit(
                VisibilityExecutionResource::RawIndirectRgba16);
            if (workload.bounceCount > 1u)
            {
                mask |= ResourceBit(
                    VisibilityExecutionResource::CumulativeIndirectRgba16);
            }
        }
        if (workload.temporalEnabled)
        {
            mask |= ResourceBit(
                    VisibilityExecutionResource::TemporalDepthR32) |
                ResourceBit(
                    VisibilityExecutionResource::TemporalNormalRgba8);
            if (hasAo)
            {
                mask |= ResourceBit(
                    VisibilityExecutionResource::TemporalAmbientR16);
            }
            if (hasGi)
            {
                mask |= ResourceBit(
                    VisibilityExecutionResource::TemporalIndirectRgba16);
            }
        }
        const bool needsReconstruction = workload.spatialEnabled ||
            workload.resolution != VisibilityPerformanceResolution::Full;
        if (needsReconstruction)
        {
            if (hasAo)
            {
                mask |= ResourceBit(
                    VisibilityExecutionResource::FinalAmbientR16);
            }
            if (hasGi)
            {
                mask |= ResourceBit(
                    VisibilityExecutionResource::FinalIndirectRgba16);
            }
        }
        if (workload.depthHierarchyEnabled)
            mask |= ResourceBit(VisibilityExecutionResource::DepthHierarchy);
        return mask;
    }

    uint64_t ExpectedReferenceBindings()
    {
        return BindingBit(VisibilityExecutionBinding::Depth) |
            BindingBit(VisibilityExecutionBinding::Normals) |
            BindingBit(VisibilityExecutionBinding::MotionVectors) |
            BindingBit(VisibilityExecutionBinding::SourceRadiance) |
            BindingBit(VisibilityExecutionBinding::GBufferMaterial) |
            BindingBit(VisibilityExecutionBinding::BaseLighting) |
            BindingBit(VisibilityExecutionBinding::OutputLighting) |
            BindingBit(VisibilityExecutionBinding::LegacyToroidalNoise) |
            BindingBit(VisibilityExecutionBinding::LegacyCurrentFastNoise) |
            BindingBit(VisibilityExecutionBinding::DepthHierarchy) |
            BindingBit(VisibilityExecutionBinding::AmbientHistory) |
            BindingBit(VisibilityExecutionBinding::IndirectHistory) |
            BindingBit(VisibilityExecutionBinding::AmbientOutput) |
            BindingBit(VisibilityExecutionBinding::IndirectOutput);
    }

    uint64_t ExpectedReferencePasses(
        const VisibilityPerformanceWorkload& workload)
    {
        uint64_t mask = PassBit(VisibilityExecutionPass::LegacyTrace) |
            PassBit(VisibilityExecutionPass::Composition);
        if (workload.depthHierarchyEnabled)
            mask |= PassBit(VisibilityExecutionPass::DepthPreparation);
        if (HasGi(workload.consumer) && workload.bounceCount > 1u)
            mask |= PassBit(VisibilityExecutionPass::LegacyLaterBounceTrace);
        if (workload.temporalEnabled)
            mask |= PassBit(VisibilityExecutionPass::Temporal);
        if (workload.spatialEnabled ||
            workload.resolution != VisibilityPerformanceResolution::Full)
        {
            mask |= PassBit(VisibilityExecutionPass::Reconstruction);
        }
        return mask;
    }

    uint32_t ExpectedReferenceDispatchCount(
        const VisibilityPerformanceWorkload& workload,
        uint64_t passMask)
    {
        uint32_t count = CountBits(passMask);
        if (HasGi(workload.consumer) && workload.bounceCount > 2u)
            count += workload.bounceCount - 2u;
        return count;
    }

    void TestCountAndEdgePackingExhaustively()
    {
        for (uint32_t edgeByte = 0u; edgeByte <= 255u; ++edgeByte)
        {
            for (uint32_t count = 0u; count <= 32u; ++count)
            {
                uint16_t packed = 0xffffu;
                Require(TryPackVisibilityCountAndEdges(
                        count, static_cast<uint8_t>(edgeByte), packed),
                    "Every valid count and edge byte packs");
                Require(UnpackVisibilitySectorCount(packed) == count,
                    "The six-bit sector count round-trips");
                Require(UnpackVisibilityEdges(packed) ==
                        static_cast<uint8_t>(edgeByte),
                    "The eight-bit edge payload round-trips");
                Require(IsCanonicalPackedVisibilityValue(packed),
                    "A packed helper result is canonical");
                Require((packed & VisibilityPackedReservedMask) == 0u,
                    "Packing leaves both reserved bits clear");

                const std::array<VisibilityPackedEdge, 4> edges = {
                    VisibilityPackedEdge::Left,
                    VisibilityPackedEdge::Right,
                    VisibilityPackedEdge::Top,
                    VisibilityPackedEdge::Bottom
                };
                for (size_t edgeIndex = 0u;
                    edgeIndex < edges.size();
                    ++edgeIndex)
                {
                    const uint8_t expected = static_cast<uint8_t>(
                        (edgeByte >> (6u - edgeIndex * 2u)) & 0x3u);
                    Require(UnpackVisibilityEdgeWeight(
                            UnpackVisibilityEdges(packed),
                            edges[edgeIndex]) == expected,
                        "Every two-bit edge lane decodes independently");
                }
            }
        }

        uint16_t unchanged = 0x5a5au;
        Require(!TryPackVisibilityCountAndEdges(33u, 0xffu, unchanged),
            "A count larger than the 32-sector mask is rejected");
        Require(unchanged == 0x5a5au,
            "A failed pack does not overwrite the caller's value");
        Require(!IsCanonicalPackedVisibilityValue(static_cast<uint16_t>(
                VisibilityPackedReservedMask | 1u)),
            "Reserved packed bits are rejected");
        Require(!IsCanonicalPackedVisibilityValue(33u),
            "An out-of-range six-bit sector count is rejected");
    }

    void TestPackedEdgeGenerationReferenceCases()
    {
        const std::array<float, 4> equalDepth = {
            10.0f, 10.0f, 10.0f, 10.0f
        };
        const std::array<float, 4> matchingNormal = {
            1.0f, 1.0f, 1.0f, 1.0f
        };
        const std::array<bool, 4> allValid = {
            true, true, true, true
        };

        Require(IsDeviceDepthValidReference(0.0f, false) &&
                !IsDeviceDepthValidReference(1.0f, false) &&
                !IsDeviceDepthValidReference(0.0f, true) &&
                IsDeviceDepthValidReference(1.0f, true),
            "Forward and reverse device-depth backgrounds use opposite endpoints");
        Require(!IsDeviceDepthValidReference(
                std::numeric_limits<float>::quiet_NaN(), false),
            "Non-finite device depth is never an edge receiver");

        const uint8_t continuous = ComputePackedEdgesReference(
            10.0f, equalDepth, matchingNormal, allValid,
            EdgeReferenceMode::DepthAndNormal);
        Require(continuous == 0xffu,
            "A flat coplanar neighborhood preserves every edge");
        Require(UnpackVisibilityEdgeWeight(
                    continuous, VisibilityPackedEdge::Left) == 3u &&
                UnpackVisibilityEdgeWeight(
                    continuous, VisibilityPackedEdge::Bottom) == 3u,
            "Packed lane order matches the shader's L/R/T/B bit layout");

        std::array<bool, 4> backgroundNeighbor = allValid;
        backgroundNeighbor[0] = false;
        const uint8_t backgroundBoundary = ComputePackedEdgesReference(
            10.0f, equalDepth, matchingNormal, backgroundNeighbor,
            EdgeReferenceMode::DepthAndNormal);
        Require(UnpackVisibilityEdgeWeight(
                    backgroundBoundary, VisibilityPackedEdge::Left) == 0u &&
                UnpackVisibilityEdgeWeight(
                    backgroundBoundary, VisibilityPackedEdge::Right) == 3u,
            "A background neighbor closes only its boundary lane");

        std::array<float, 4> hardDepthEdge = equalDepth;
        hardDepthEdge[1] = 20.0f;
        const uint8_t hardBoundary = ComputePackedEdgesReference(
            10.0f, hardDepthEdge, matchingNormal, allValid,
            EdgeReferenceMode::DepthOnly);
        Require(UnpackVisibilityEdgeWeight(
                    hardBoundary, VisibilityPackedEdge::Right) == 0u &&
                UnpackVisibilityEdgeWeight(
                    hardBoundary, VisibilityPackedEdge::Top) == 3u,
            "A hard depth edge is isolated from continuous neighbors");

        std::array<float, 4> normalDot = matchingNormal;
        normalDot[2] = 0.0f;
        const uint8_t depthOnlyNormalBoundary = ComputePackedEdgesReference(
            10.0f, equalDepth, normalDot, allValid,
            EdgeReferenceMode::DepthOnly);
        const uint8_t depthNormalBoundary = ComputePackedEdgesReference(
            10.0f, equalDepth, normalDot, allValid,
            EdgeReferenceMode::DepthAndNormal);
        Require(UnpackVisibilityEdgeWeight(depthOnlyNormalBoundary,
                    VisibilityPackedEdge::Top) == 3u &&
                UnpackVisibilityEdgeWeight(depthNormalBoundary,
                    VisibilityPackedEdge::Top) == 0u,
            "Depth-plus-normal mode detects a normal-only discontinuity");

        const std::array<float, 4> shallowPlanarSlope = {
            9.0f, 11.0f, 9.0f, 11.0f
        };
        const uint8_t unsmoothedSlope = ComputePackedEdgesReference(
            10.0f, shallowPlanarSlope, matchingNormal, allValid,
            EdgeReferenceMode::DepthOnly);
        const uint8_t adjustedSlope = ComputePackedEdgesReference(
            10.0f, shallowPlanarSlope, matchingNormal, allValid,
            EdgeReferenceMode::SlopeAdjustedDepthAndNormal);
        Require(unsmoothedSlope == 0u && adjustedSlope == 0xffu,
            "Slope adjustment removes matched opposing depth gradients");

        Require(SamplingToFullPixelReference(
                    { 0u, 0u }, 2u, { 1920u, 1080u }) ==
                std::array<uint32_t, 2>{ 1u, 1u } &&
                SamplingToFullPixelReference(
                    { 2u, 1u }, 2u, { 5u, 3u }) ==
                std::array<uint32_t, 2>{ 4u, 2u },
            "Half-resolution receiver mapping uses the centered source and clamps edges");

        const uint8_t fromRight = 3u;
        const uint8_t toLeft = 1u;
        Require(std::min(fromRight, toLeft) == 1u,
            "Symmetric enforcement uses the weaker of opposing edge lanes");
    }

    constexpr std::array<uint32_t, 33> FixedTraceProgressivePrefixMasks = {
        0x00000000u, 0x00000001u, 0x00010001u, 0x00010101u,
        0x01010101u, 0x01010111u, 0x01110111u, 0x01111111u,
        0x11111111u, 0x11111115u, 0x11151115u, 0x11151515u,
        0x15151515u, 0x15151555u, 0x15551555u, 0x15555555u,
        0x55555555u, 0x55555557u, 0x55575557u, 0x55575757u,
        0x57575757u, 0x57575777u, 0x57775777u, 0x57777777u,
        0x77777777u, 0x7777777fu, 0x777f777fu, 0x777f7f7fu,
        0x7f7f7f7fu, 0x7f7f7fffu, 0x7fff7fffu, 0x7fffffffu,
        0xffffffffu
    };

    uint32_t RotateFixedTracePrefix(uint32_t mask, uint32_t shift)
    {
        shift &= 31u;
        return shift == 0u
            ? mask
            : (mask << shift) | (mask >> (32u - shift));
    }

    uint32_t FirstFixedTraceStratum(uint32_t mask)
    {
        Require(mask != 0u,
            "A fixed-trace fixture cannot consume an empty radial mask");
        uint32_t index = 0u;
        while ((mask & 1u) == 0u)
        {
            mask >>= 1u;
            ++index;
        }
        return index;
    }

    uint32_t FixedTraceFixtureHash(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    struct FixedTraceEquivalenceConfiguration
    {
        uint32_t sampleCount = 8u;
        std::array<uint32_t, 2> radialShift{};
        std::array<float, 2> radialRotation{};
        float sliceRotation = 0.f;
        float sectorPhase = 0.f;
        uint32_t activeSideMask = 3u;
        uint32_t sceneSeed = 0u;
        VisibilityPerformanceConsumer consumer =
            VisibilityPerformanceConsumer::AmbientOcclusion;
        VisibilityPerformanceEstimator estimator =
            VisibilityPerformanceEstimator::UniformProjectedAngle;
        bool rejectDuplicatePixels = true;
        bool exitOnFullMask = true;
        bool forceFullMask = false;
    };

    struct FixedTraceEvaluatedVisit
    {
        uint32_t side = 0u;
        uint32_t radialStratum = 0u;
        uint32_t samplePixel = 0u;
        uint32_t candidateBits = 0u;
        uint32_t newlyCoveredBits = 0u;

        bool operator==(const FixedTraceEvaluatedVisit& other) const noexcept
        {
            return side == other.side &&
                radialStratum == other.radialStratum &&
                samplePixel == other.samplePixel &&
                candidateBits == other.candidateBits &&
                newlyCoveredBits == other.newlyCoveredBits;
        }
    };

    struct FixedTraceEquivalenceResult
    {
        uint32_t finalMask = 0u;
        float rawAmbient = 1.f;
        std::array<float, 3> rawIndirect{};
        std::array<uint32_t, RadialVisibilitySectorCount> sourceOwner{};
        std::vector<uint32_t> scheduledVisits;
        std::vector<FixedTraceEvaluatedVisit> evaluatedVisits;
        uint32_t rejectedDuplicateCount = 0u;
        bool exitedOnFullMask = false;
    };

    struct FixedTraceEquivalenceState
    {
        std::array<uint32_t, 2> remainingRadialStrata{};
        std::array<uint32_t, 2> previousSamplePixel{};
        std::array<bool, 2> hasPreviousSample{};
        std::array<float, 2> sideProjectedRadius{};
        float sliceDirectionX = 1.f;
        float sliceDirectionY = 0.f;
        SliceMeasure sliceMeasure{};
        RadialVisibilityMask visibilityMask;
        FixedTraceEquivalenceResult result;
    };

    FixedTraceEquivalenceState MakeFixedTraceEquivalenceState(
        const FixedTraceEquivalenceConfiguration& configuration)
    {
        FixedTraceEquivalenceState state;
        const uint32_t stepsPerSide = configuration.sampleCount / 2u;
        const uint32_t prefixMask =
            FixedTraceProgressivePrefixMasks[stepsPerSide];
        for (uint32_t side = 0u; side < 2u; ++side)
        {
            state.remainingRadialStrata[side] =
                (configuration.activeSideMask & (1u << side)) != 0u
                ? RotateFixedTracePrefix(
                    prefixMask, configuration.radialShift[side])
                : 0u;
            state.sideProjectedRadius[side] = 32.f + float(
                FixedTraceFixtureHash(
                    configuration.sceneSeed ^ (0x9e3779b9u * (side + 1u))) &
                63u);
        }
        constexpr float Pi = 3.14159265358979323846f;
        const float sliceAzimuth = configuration.sliceRotation * Pi;
        state.sliceDirectionX = std::cos(sliceAzimuth);
        state.sliceDirectionY = std::sin(sliceAzimuth);
        const uint32_t normalHash = FixedTraceFixtureHash(
            configuration.sceneSeed ^ 0xa511e9b3u);
        const VisibilityEstimatorFloat3 receiverNormal =
            VisibilityEstimatorSafeNormalize({
                (float(normalHash & 0xffu) / 255.f - 0.5f) * 1.2f,
                (float((normalHash >> 8u) & 0xffu) / 255.f - 0.5f) *
                    1.2f,
                0.5f +
                    float((normalHash >> 16u) & 0xffu) / 255.f
            }, { 0.f, 0.f, 1.f });
        state.sliceMeasure = BuildSliceMeasure(
            { 0.f, 0.f, 1.f },
            { state.sliceDirectionX, state.sliceDirectionY, 0.f },
            receiverNormal);
        state.result.sourceOwner.fill(
            std::numeric_limits<uint32_t>::max());
        state.result.scheduledVisits.reserve(configuration.sampleCount);
        state.result.evaluatedVisits.reserve(configuration.sampleCount);
        return state;
    }

    void EvaluateFixedTraceFixtureVisit(
        const FixedTraceEquivalenceConfiguration& configuration,
        FixedTraceEquivalenceState& state,
        uint32_t side,
        uint32_t radialStratum)
    {
        state.result.scheduledVisits.push_back(
            (side << 8u) | radialStratum);

        const float normalizedStep = std::clamp(
            (float(radialStratum) +
                configuration.radialRotation[side]) / 32.f,
            0.f, 1.f);
        const float sampleDistance = std::max(
            normalizedStep * normalizedStep *
                state.sideProjectedRadius[side],
            0.5f);
        const float samplingSide = side == 0u ? -1.f : 1.f;
        const uint32_t sampleX = uint32_t(256.5f +
            samplingSide * state.sliceDirectionX * sampleDistance);
        const uint32_t sampleY = uint32_t(256.5f +
            samplingSide * state.sliceDirectionY * sampleDistance);
        const uint32_t samplePixel = (sampleY << 16u) | sampleX;

        if (configuration.rejectDuplicatePixels &&
            state.hasPreviousSample[side] &&
            state.previousSamplePixel[side] == samplePixel)
        {
            ++state.result.rejectedDuplicateCount;
            return;
        }
        if (configuration.rejectDuplicatePixels)
        {
            state.previousSamplePixel[side] = samplePixel;
            state.hasPreviousSample[side] = true;
        }

        const uint32_t materialHash = FixedTraceFixtureHash(
            samplePixel ^ configuration.sceneSeed ^
            (0x85ebca6bu * (side + 1u)) ^
            (0xc2b2ae35u * (radialStratum + 1u)));
        VisibilityInterval interval;
        if (configuration.forceFullMask &&
            state.result.evaluatedVisits.empty())
        {
            interval = MakeVisibilityInterval(0.f, 1.f);
        }
        else
        {
            if (configuration.estimator ==
                VisibilityPerformanceEstimator::UniformProjectedAngle)
            {
                const float minimumAngle =
                    float(materialHash & 0x3ffu) / 1280.f;
                const float angularWidth =
                    float(16u + ((materialHash >> 10u) & 0xffu)) /
                    640.f;
                interval = MakeVisibilityInterval(
                    minimumAngle,
                    std::min(minimumAngle + angularWidth, 1.f));
            }
            else
            {
                const float samplingSide = side == 0u ? -1.f : 1.f;
                const float frontAngle = samplingSide *
                    (0.025f +
                        float(materialHash & 0x3ffu) / 1023.f * 1.25f);
                const float backAngle = frontAngle + samplingSide *
                    (0.01f +
                        float((materialHash >> 10u) & 0xffu) / 255.f *
                            0.2f);
                const VisibilityEstimatorFloat3 frontDirection =
                    state.sliceMeasure.V * std::cos(frontAngle) +
                    state.sliceMeasure.S * std::sin(frontAngle);
                const VisibilityEstimatorFloat3 backDirection =
                    state.sliceMeasure.V * std::cos(backAngle) +
                    state.sliceMeasure.S * std::sin(backAngle);
                interval = configuration.estimator ==
                    VisibilityPerformanceEstimator::UniformSolidAngle
                    ? BuildGtInterval(
                        frontDirection, backDirection, state.sliceMeasure)
                    : BuildGtCosineInterval(
                        frontDirection, backDirection, state.sliceMeasure);
            }
        }

        const uint32_t candidateBits = MakeStochasticSectorRangeMask(
            interval, configuration.sectorPhase);
        const uint32_t newlyCoveredBits = AccumulateOccluder(
            state.visibilityMask, candidateBits);
        state.result.evaluatedVisits.push_back({
            side,
            radialStratum,
            samplePixel,
            candidateBits,
            newlyCoveredBits
        });

        const uint32_t sourceIdentity =
            (side << 31u) | (radialStratum << 24u) |
            ((sampleX & 0xfffu) << 12u) | (sampleY & 0xfffu);
        for (uint32_t bit = 0u;
            bit < RadialVisibilitySectorCount;
            ++bit)
        {
            if ((newlyCoveredBits & (uint32_t{ 1 } << bit)) != 0u)
                state.result.sourceOwner[bit] = sourceIdentity;
        }

        if (HasGi(configuration.consumer))
        {
            const float angularCoverage =
                float(CountBits(newlyCoveredBits)) /
                float(RadialVisibilitySectorCount);
            const float receiverCosine = 0.25f + 0.75f *
                (float((materialHash >> 18u) & 0xffu) / 255.f);
            const float sourceCosine = 0.125f + 0.875f *
                (float((materialHash >> 2u) & 0xffu) / 255.f);
            const std::array<float, 3> sourceRadiance = {
                float(1u + (materialHash & 0x1fu)) / 16.f,
                float(1u + ((materialHash >> 5u) & 0x1fu)) / 20.f,
                float(1u + ((materialHash >> 10u) & 0x1fu)) / 24.f
            };
            float sampleWeight =
                angularCoverage * receiverCosine * sourceCosine;
            if (configuration.estimator ==
                VisibilityPerformanceEstimator::UniformSolidAngle)
            {
                sampleWeight = ComputeGtUniformGiSampleWeight(
                    CountBits(newlyCoveredBits),
                    receiverCosine,
                    sourceCosine);
            }
            else if (configuration.estimator ==
                VisibilityPerformanceEstimator::CosineWeightedSolidAngle)
            {
                sampleWeight = ComputeGtCosineGiSampleWeight(
                    CountBits(newlyCoveredBits),
                    state.sliceMeasure.cosineSliceMass,
                    sourceCosine);
            }
            for (size_t channel = 0u;
                channel < state.result.rawIndirect.size();
                ++channel)
            {
                state.result.rawIndirect[channel] +=
                    sourceRadiance[channel] * sampleWeight;
            }
        }
    }

    FixedTraceEquivalenceResult FinalizeFixedTraceEquivalenceState(
        const FixedTraceEquivalenceConfiguration& configuration,
        FixedTraceEquivalenceState state)
    {
        state.result.finalMask = state.visibilityMask.occludedBits;
        if (HasAo(configuration.consumer))
        {
            switch (configuration.estimator)
            {
            case VisibilityPerformanceEstimator::UniformProjectedAngle:
                state.result.rawAmbient =
                    GetSliceVisibility(state.visibilityMask);
                break;
            case VisibilityPerformanceEstimator::UniformSolidAngle:
                state.result.rawAmbient =
                    ResolveGtUniformAmbientVisibility(state.visibilityMask);
                break;
            case VisibilityPerformanceEstimator::CosineWeightedSolidAngle:
                state.result.rawAmbient = ResolveGtCosineAmbientVisibility(
                    state.visibilityMask, state.sliceMeasure);
                break;
            }
        }
        if (HasGi(configuration.consumer))
        {
            const float normalization = configuration.estimator ==
                VisibilityPerformanceEstimator::UniformSolidAngle
                ? GetGtUniformIrradianceNormalization()
                : configuration.estimator ==
                    VisibilityPerformanceEstimator::
                        CosineWeightedSolidAngle
                    ? GetGtCosineIrradianceNormalization()
                    : VisibilityEstimatorPi;
            for (float& channel : state.result.rawIndirect)
                channel *= normalization;
        }
        return state.result;
    }

    FixedTraceEquivalenceResult RunGenericFixedCountTraceFixture(
        const FixedTraceEquivalenceConfiguration& configuration)
    {
        FixedTraceEquivalenceState state =
            MakeFixedTraceEquivalenceState(configuration);
        while ((state.remainingRadialStrata[0] |
            state.remainingRadialStrata[1]) != 0u)
        {
            for (uint32_t side = 0u; side < 2u; ++side)
            {
                if (configuration.exitOnFullMask &&
                    state.visibilityMask.occludedBits ==
                        RadialVisibilityFullMask)
                {
                    state.result.exitedOnFullMask = true;
                    break;
                }
                const uint32_t radialMask =
                    state.remainingRadialStrata[side];
                if (radialMask == 0u)
                    continue;
                const uint32_t radialStratum =
                    FirstFixedTraceStratum(radialMask);
                state.remainingRadialStrata[side] =
                    radialMask & (radialMask - 1u);
                EvaluateFixedTraceFixtureVisit(
                    configuration, state, side, radialStratum);
            }
            if (configuration.exitOnFullMask &&
                state.visibilityMask.occludedBits ==
                    RadialVisibilityFullMask)
            {
                state.result.exitedOnFullMask = true;
                break;
            }
        }
        return FinalizeFixedTraceEquivalenceState(
            configuration, std::move(state));
    }

    template<uint32_t SampleCount>
    FixedTraceEquivalenceResult RunSpecializedFixedTraceFixture(
        const FixedTraceEquivalenceConfiguration& configuration)
    {
        static_assert(SampleCount == 8u || SampleCount == 12u ||
            SampleCount == 16u || SampleCount == 20u);
        FixedTraceEquivalenceState state =
            MakeFixedTraceEquivalenceState(configuration);
        for (uint32_t fixedStepIndex = 0u;
            fixedStepIndex < SampleCount / 2u;
            ++fixedStepIndex)
        {
            for (uint32_t side = 0u; side < 2u; ++side)
            {
                if (configuration.exitOnFullMask &&
                    state.visibilityMask.occludedBits ==
                        RadialVisibilityFullMask)
                {
                    state.result.exitedOnFullMask = true;
                    break;
                }
                const uint32_t radialMask =
                    state.remainingRadialStrata[side];
                if (radialMask == 0u)
                    continue;
                const uint32_t radialStratum =
                    FirstFixedTraceStratum(radialMask);
                state.remainingRadialStrata[side] =
                    radialMask & (radialMask - 1u);
                EvaluateFixedTraceFixtureVisit(
                    configuration, state, side, radialStratum);
            }
            if (configuration.exitOnFullMask &&
                state.visibilityMask.occludedBits ==
                    RadialVisibilityFullMask)
            {
                state.result.exitedOnFullMask = true;
                break;
            }
        }
        return FinalizeFixedTraceEquivalenceState(
            configuration, std::move(state));
    }

    FixedTraceEquivalenceResult RunSpecializedFixedTraceFixture(
        const FixedTraceEquivalenceConfiguration& configuration)
    {
        switch (configuration.sampleCount)
        {
        case 8u:
            return RunSpecializedFixedTraceFixture<8u>(configuration);
        case 12u:
            return RunSpecializedFixedTraceFixture<12u>(configuration);
        case 16u:
            return RunSpecializedFixedTraceFixture<16u>(configuration);
        case 20u:
            return RunSpecializedFixedTraceFixture<20u>(configuration);
        default:
            Fail("A fixed-trace fixture requested an unsupported count");
        }
    }

    void RequireEquivalentFixedTraceResults(
        const FixedTraceEquivalenceConfiguration& configuration,
        const FixedTraceEquivalenceResult& generic,
        const FixedTraceEquivalenceResult& specialized)
    {
        const bool indirectMatches =
            std::abs(generic.rawIndirect[0] -
                specialized.rawIndirect[0]) < 1e-7f &&
            std::abs(generic.rawIndirect[1] -
                specialized.rawIndirect[1]) < 1e-7f &&
            std::abs(generic.rawIndirect[2] -
                specialized.rawIndirect[2]) < 1e-7f;
        if (generic.scheduledVisits == specialized.scheduledVisits &&
            generic.evaluatedVisits == specialized.evaluatedVisits &&
            generic.finalMask == specialized.finalMask &&
            generic.sourceOwner == specialized.sourceOwner &&
            generic.rejectedDuplicateCount ==
                specialized.rejectedDuplicateCount &&
            generic.exitedOnFullMask == specialized.exitedOnFullMask &&
            std::abs(generic.rawAmbient -
                specialized.rawAmbient) < 1e-7f &&
            indirectMatches)
        {
            return;
        }

        Fail("fixed specialization diverged from the generic traversal for " +
            std::to_string(configuration.sampleCount) +
            " samples, side mask " +
            std::to_string(configuration.activeSideMask) +
            ", radial shifts " +
            std::to_string(configuration.radialShift[0]) + "/" +
            std::to_string(configuration.radialShift[1]));
    }

    void TestFixedSpecializationsAgainstRuntimeTraversal()
    {
        for (uint32_t prefixLength = 0u;
            prefixLength < FixedTraceProgressivePrefixMasks.size();
            ++prefixLength)
        {
            const uint32_t prefixMask =
                FixedTraceProgressivePrefixMasks[prefixLength];
            Require(CountBits(prefixMask) == prefixLength,
                "Every progressive radial prefix has its declared population");
            if (prefixLength != 0u)
            {
                Require((FixedTraceProgressivePrefixMasks[prefixLength - 1u] &
                        prefixMask) ==
                        FixedTraceProgressivePrefixMasks[prefixLength - 1u],
                    "Every progressive radial prefix contains its predecessor");
            }
        }

        constexpr std::array<uint32_t, 4> fixedCounts = {
            8u, 12u, 16u, 20u
        };
        constexpr std::array<VisibilityPerformanceProfile, 4> fixedProfiles = {
            VisibilityPerformanceProfile::ExactFixed8,
            VisibilityPerformanceProfile::ExactFixed12,
            VisibilityPerformanceProfile::ExactFixed16,
            VisibilityPerformanceProfile::ExactFixed20
        };
        constexpr std::array<VisibilityPerformanceConsumer, 3> consumers = {
            VisibilityPerformanceConsumer::AmbientOcclusion,
            VisibilityPerformanceConsumer::IndirectDiffuse,
            VisibilityPerformanceConsumer::AmbientOcclusionAndIndirectDiffuse
        };
        constexpr std::array<VisibilityPerformanceEstimator, 3> estimators = {
            VisibilityPerformanceEstimator::UniformProjectedAngle,
            VisibilityPerformanceEstimator::UniformSolidAngle,
            VisibilityPerformanceEstimator::CosineWeightedSolidAngle
        };
        constexpr std::array<float, 8> fractionalRotations = {
            0.f, 0.000001f, 0.031249f, 0.125f,
            0.499999f, 0.5f, 0.875f, 0.999999f
        };

        uint32_t comparisonCount = 0u;
        bool exercisedDuplicateRejection = false;
        bool exercisedFullMaskExit = false;
        bool exercisedPartialMask = false;
        bool exercisedNonzeroGi = false;
        for (size_t fixedIndex = 0u;
            fixedIndex < fixedCounts.size();
            ++fixedIndex)
        {
            const uint32_t sampleCount = fixedCounts[fixedIndex];
            for (VisibilityPerformanceConsumer consumer : consumers)
            for (VisibilityPerformanceEstimator estimator : estimators)
            {
                VisibilityPerformanceWorkload workload;
                workload.consumer = consumer;
                workload.estimator = estimator;
                workload.firstBounceSampleCount = sampleCount;
                const VisibilityExecutionPlan plan =
                    ResolveVisibilityExecutionPlan(
                        fixedProfiles[fixedIndex], workload);
                Require(plan.valid &&
                        plan.fixedFirstBounceSampleCount == sampleCount &&
                        plan.configuration.trace ==
                            VisibilityTraceImplementation::
                                FixedInterleavedBitmask &&
                        plan.configuration.traversal ==
                            VisibilityTraversalOrder::
                                InterleavedNegativePositiveNearToFar &&
                        HasVisibilityExecutionPass(
                            plan.passMask,
                            VisibilityExecutionPass::FixedTrace),
                    "Every estimator and AO/GI consumer selects the requested fixed permutation");
            }

            for (uint32_t negativeShift = 0u;
                negativeShift < 32u;
                ++negativeShift)
            for (uint32_t positiveShift = 0u;
                positiveShift < 32u;
                ++positiveShift)
            for (uint32_t variant = 0u; variant < 4u; ++variant)
            for (uint32_t activeSideMask = 0u;
                activeSideMask < 4u;
                ++activeSideMask)
            for (VisibilityPerformanceConsumer consumer : consumers)
            for (VisibilityPerformanceEstimator estimator : estimators)
            {
                FixedTraceEquivalenceConfiguration configuration;
                configuration.sampleCount = sampleCount;
                configuration.radialShift = {
                    negativeShift, positiveShift
                };
                configuration.radialRotation = {
                    fractionalRotations[
                        (negativeShift + variant) & 7u],
                    fractionalRotations[
                        (positiveShift + variant * 3u) & 7u]
                };
                configuration.sliceRotation = fractionalRotations[
                    (negativeShift * 5u + positiveShift * 3u +
                        variant) & 7u];
                configuration.sectorPhase = fractionalRotations[
                    (negativeShift * 3u + positiveShift * 7u +
                        variant * 5u) & 7u];
                configuration.activeSideMask = activeSideMask;
                configuration.sceneSeed = FixedTraceFixtureHash(
                    sampleCount | (negativeShift << 5u) |
                    (positiveShift << 10u) | (variant << 15u));
                configuration.consumer = consumer;
                configuration.estimator = estimator;
                configuration.rejectDuplicatePixels =
                    (variant & 1u) != 0u;
                configuration.exitOnFullMask = (variant & 2u) != 0u;
                configuration.forceFullMask =
                    ((negativeShift + positiveShift * 3u + variant) &
                        31u) == 0u;

                const FixedTraceEquivalenceResult generic =
                    RunGenericFixedCountTraceFixture(configuration);
                const FixedTraceEquivalenceResult specialized =
                    RunSpecializedFixedTraceFixture(configuration);
                RequireEquivalentFixedTraceResults(
                    configuration, generic, specialized);

                exercisedDuplicateRejection =
                    exercisedDuplicateRejection ||
                    generic.rejectedDuplicateCount != 0u;
                exercisedFullMaskExit = exercisedFullMaskExit ||
                    generic.exitedOnFullMask;
                exercisedPartialMask = exercisedPartialMask ||
                    (generic.finalMask != 0u &&
                        generic.finalMask != RadialVisibilityFullMask);
                exercisedNonzeroGi = exercisedNonzeroGi ||
                    generic.rawIndirect[0] != 0.f ||
                    generic.rawIndirect[1] != 0.f ||
                    generic.rawIndirect[2] != 0.f;
                ++comparisonCount;
            }
        }

        Require(comparisonCount == 589824u,
            "The fixed equivalence matrix retains its exhaustive scenario count");
        Require(exercisedDuplicateRejection,
            "The fixed equivalence matrix exercises duplicate-pixel rejection");
        Require(exercisedFullMaskExit,
            "The fixed equivalence matrix exercises full-mask early exit");
        Require(exercisedPartialMask,
            "The fixed equivalence matrix exercises partial visibility masks");
        Require(exercisedNonzeroGi,
            "The fixed equivalence matrix exercises nonzero raw GI accumulation");
    }

    void TestFixedInterleavedOrders()
    {
        const std::array<uint32_t, 4> fixedCounts = {
            8u, 12u, 16u, 20u
        };
        for (uint32_t sampleCount : fixedCounts)
        {
            Require(IsSupportedFixedVisibilitySampleCount(sampleCount),
                "Every curated fixed count is recognized");
            uint32_t negativeVisits = 0u;
            uint32_t positiveVisits = 0u;
            for (uint32_t visitIndex = 0u;
                visitIndex < sampleCount;
                ++visitIndex)
            {
                const VisibilityFixedSampleVisit visit =
                    GetFixedInterleavedVisibilitySampleVisit(
                        sampleCount, visitIndex);
                Require(visit.valid && visit.visitIndex == visitIndex,
                    "Every fixed visit retains its generic visit index");
                Require(visit.pairIndex == visitIndex / 2u &&
                        visit.sideStepIndex == visitIndex / 2u,
                    "Both sides advance through the same radial pair");
                const VisibilitySampleSide expectedSide =
                    (visitIndex & 1u) == 0u
                    ? VisibilitySampleSide::Negative
                    : VisibilitySampleSide::Positive;
                Require(visit.side == expectedSide,
                    "Every pair visits negative then positive");
                if (visit.side == VisibilitySampleSide::Negative)
                    ++negativeVisits;
                else
                    ++positiveVisits;
            }
            Require(negativeVisits == sampleCount / 2u &&
                    positiveVisits == sampleCount / 2u,
                "Fixed traversal retains equal side budgets");
            Require(!GetFixedInterleavedVisibilitySampleVisit(
                    sampleCount, sampleCount).valid,
                "The first out-of-range visit is rejected");
        }
        Require(!IsSupportedFixedVisibilitySampleCount(10u),
            "An uncurated count is not silently specialized");
        Require(!GetFixedInterleavedVisibilitySampleVisit(10u, 0u).valid,
            "An uncurated fixed traversal is rejected");
    }

    void TestEveryPerformanceProfileIsHonestAndFullyAssigned()
    {
        std::set<uint64_t> permutationKeys;
        std::set<uint64_t> historyKeys;
        for (uint32_t rawProfile =
                static_cast<uint32_t>(VisibilityPerformanceProfile::Reference);
            rawProfile <
                static_cast<uint32_t>(VisibilityPerformanceProfile::Count);
            ++rawProfile)
        {
            const auto profile =
                static_cast<VisibilityPerformanceProfile>(rawProfile);
            const VisibilityPerformanceProfileConfiguration configuration =
                GetVisibilityPerformanceProfileConfiguration(profile);
            Require(configuration.profile == profile,
                "Every enum maps to its own configuration");
            Require(configuration.assignmentMask ==
                    VisibilityProfileAllAssignments,
                "Every curated profile explicitly assigns every field");
            Require(IsVisibilityPerformanceProfileFullyAssigned(
                    configuration),
                "Every curated profile passes the completeness audit");
            Require(configuration.implementationStatus !=
                    VisibilityImplementationStatus::Unset,
                "Every profile states implementation availability");
            if (configuration.implementationStatus !=
                VisibilityImplementationStatus::Implemented)
            {
                Require(!configuration.implementationNote.empty(),
                    "Partial and unavailable profiles explain their limits");
            }

            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(
                    configuration, MakeCompatibleWorkload(profile));
            Require(plan.valid,
                std::string("A compatible workload resolves profile: ") +
                    std::string(configuration.name) + " (" +
                    plan.errorMessage + ")");
            Require(plan.permutationKey != 0u &&
                    plan.historyResetKey != 0u &&
                    !plan.permutationName.empty(),
                "Every implemented profile has complete stable identity");
            Require(permutationKeys.insert(plan.permutationKey).second,
                "Implemented profile permutation keys are distinct");
            Require(historyKeys.insert(plan.historyResetKey).second,
                "Implemented profile history keys are distinct");
        }

        const auto packedFast = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::ExactPackedCurrentFast);
        Require(packedFast.trace ==
                VisibilityTraceImplementation::FixedInterleavedBitmask &&
                packedFast.firstBounceSamples ==
                    VisibilitySampleSpecialization::Fixed8 &&
                packedFast.estimatorRequirement ==
                    VisibilityEstimatorRequirement::UniformSolidAngle &&
                packedFast.consumerRequirement ==
                    VisibilityConsumerRequirement::IncludesAmbientOcclusion,
            "Packed FAST exactly models its fixed-8 AO-present wrapper");

        const auto duplicateOff =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::
                    ExactDuplicatePixelRejectionOff);
        const auto fullMaskOff =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::ExactFullMaskEarlyExitOff);
        const auto radiusClamp =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::
                    AlgorithmicProjectedRadiusClamp64);
        const auto constantScheduler =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::DiagnosticConstantScheduler);
        Require(duplicateOff.optimizationClass ==
                    VisibilityOptimizationClass::Exact &&
                duplicateOff.benchmarkOnly &&
                fullMaskOff.optimizationClass ==
                    VisibilityOptimizationClass::Exact &&
                fullMaskOff.benchmarkOnly,
            "Exact early-out controls are independently benchmarkable");
        Require(radiusClamp.optimizationClass ==
                    VisibilityOptimizationClass::Algorithmic &&
                !radiusClamp.benchmarkOnly &&
                constantScheduler.optimizationClass ==
                    VisibilityOptimizationClass::Diagnostic &&
                constantScheduler.noise ==
                    VisibilityNoiseDelivery::ConstantDiagnostic &&
                constantScheduler.benchmarkOnly,
            "Radius-clamp and constant-scheduler controls expose honest classes");

        const auto edge2 = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2);
        const auto edge4 = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::AlgorithmicPackedEdges4x4);
        Require(edge2.rawAoStorage == VisibilityRawAoStorage::R16Float &&
                edge4.rawAoStorage == VisibilityRawAoStorage::R16Float &&
                edge2.edgeStorage == VisibilityEdgeStorage::R8Uint &&
                edge4.edgeStorage == VisibilityEdgeStorage::R8Uint &&
                edge2.firstBounceSamples ==
                    VisibilitySampleSpecialization::Fixed8 &&
                edge4.firstBounceSamples ==
                    VisibilitySampleSpecialization::Fixed8,
            "Packed-edge wrappers model R16F AO plus separate R8_UINT edges");

        const auto ps4 = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::ActivisionPs4Schedule);
        Require(ps4.firstBounceSamples ==
                VisibilitySampleSpecialization::Fixed8 &&
                ps4.rawAoStorage == VisibilityRawAoStorage::R16Float &&
                ps4.reconstruction ==
                    VisibilityReconstructionMode::ActivisionBilateral4x4 &&
                ps4.temporal ==
                    VisibilityTemporalMode::ActivisionSixDirectionEma &&
                ps4.implementationStatus ==
                    VisibilityImplementationStatus::Implemented,
            "The PS4 prototype models the complete source-like pass order");

        const auto xe = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::XeGtaoClosestMatch);
        Require(xe.implementationStatus ==
                VisibilityImplementationStatus::Implemented &&
                xe.trace == VisibilityTraceImplementation::XeGtaoHorizon &&
                xe.firstBounceSamples ==
                    VisibilitySampleSpecialization::Fixed18 &&
                xe.noise == VisibilityNoiseDelivery::XeGtaoHilbertR2 &&
                xe.math == VisibilityMathMode::XeGtaoMixedPrecision &&
                xe.edgeStorage == VisibilityEdgeStorage::R8Unorm &&
                xe.reconstruction ==
                    VisibilityReconstructionMode::XeGtaoDenoise &&
                xe.depth == VisibilityDepthMode::XeGtaoPrefilteredMips &&
                xe.resolutionRequirement ==
                    VisibilityResolutionRequirement::Full &&
                xe.name.find("LUT") != std::string_view::npos &&
                !xe.implementationNote.empty(),
            "XeGTAO advertises its implemented source-derived pipeline");

        const auto xeInline = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::XeGtaoHighInlineHilbert);
        const auto xeFp32 = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::XeGtaoHighFp32);
        Require(xeInline.implementationStatus ==
                    VisibilityImplementationStatus::Implemented &&
                xeInline.noise ==
                    VisibilityNoiseDelivery::XeGtaoInlineHilbertR2 &&
                xeInline.math ==
                    VisibilityMathMode::XeGtaoMixedPrecision &&
                xeFp32.implementationStatus ==
                    VisibilityImplementationStatus::Implemented &&
                xeFp32.noise == VisibilityNoiseDelivery::XeGtaoHilbertR2 &&
                xeFp32.math == VisibilityMathMode::ReferenceFp32,
            "XeGTAO exposes independent LUT, inline-Hilbert, and FP32 controls");

        const auto fixedFused = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::ExactFixed8FusedResolveApply);
        Require(fixedFused.implementationStatus ==
                    VisibilityImplementationStatus::Implemented &&
                fixedFused.firstBounceSamples ==
                    VisibilitySampleSpecialization::Fixed8 &&
                fixedFused.application ==
                    VisibilityApplicationMode::FusedResolveAndApplyExact &&
                fixedFused.consumerRequirement ==
                    VisibilityConsumerRequirement::AmbientOcclusionOnly &&
                fixedFused.resolutionRequirement ==
                    VisibilityResolutionRequirement::Reduced,
            "The combined exact profile composes fixed-8 tracing with exact fusion");

        const auto ps4Packed = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::ActivisionPs4PackedGather);
        Require(ps4Packed.implementationStatus ==
                    VisibilityImplementationStatus::Implemented &&
                ps4Packed.trace == ps4.trace &&
                ps4Packed.reconstruction == ps4.reconstruction &&
                ps4Packed.temporal == ps4.temporal &&
                ps4Packed.name != ps4.name,
            "The packed-gather PS4 control isolates filter delivery without changing the algorithm");

        const auto unset = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Unset);
        Require(!IsVisibilityPerformanceProfileFullyAssigned(unset),
            "The unset sentinel cannot masquerade as a curated profile");
    }

    void TestReferenceContractExhaustively()
    {
        const std::array<VisibilityPerformanceConsumer, 3> consumers = {
            VisibilityPerformanceConsumer::AmbientOcclusion,
            VisibilityPerformanceConsumer::IndirectDiffuse,
            VisibilityPerformanceConsumer::AmbientOcclusionAndIndirectDiffuse
        };
        const std::array<VisibilityPerformanceEstimator, 3> estimators = {
            VisibilityPerformanceEstimator::UniformProjectedAngle,
            VisibilityPerformanceEstimator::UniformSolidAngle,
            VisibilityPerformanceEstimator::CosineWeightedSolidAngle
        };
        const std::array<VisibilityPerformanceResolution, 3> resolutions = {
            VisibilityPerformanceResolution::Full,
            VisibilityPerformanceResolution::Half,
            VisibilityPerformanceResolution::Quarter
        };
        const std::array<VisibilityPerformanceScheduler, 3> schedulers = {
            VisibilityPerformanceScheduler::IndependentHash,
            VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField,
            VisibilityPerformanceScheduler::
                FilterAdaptedSpatiotemporalRankField
        };
        const std::array<uint32_t, 2> bounceCounts = { 1u, 3u };

        for (VisibilityPerformanceConsumer consumer : consumers)
        for (VisibilityPerformanceEstimator estimator : estimators)
        for (VisibilityPerformanceResolution resolution : resolutions)
        for (VisibilityPerformanceScheduler scheduler : schedulers)
        for (uint32_t bounceCount : bounceCounts)
        for (uint32_t flags = 0u; flags < 16u; ++flags)
        {
            VisibilityPerformanceWorkload workload;
            workload.consumer = consumer;
            workload.estimator = estimator;
            workload.resolution = resolution;
            workload.scheduler = scheduler;
            workload.bounceCount = bounceCount;
            workload.adaptiveSamplingEnabled = (flags & 1u) != 0u;
            workload.temporalEnabled = (flags & 2u) != 0u;
            workload.spatialEnabled = (flags & 4u) != 0u;
            workload.depthHierarchyEnabled = (flags & 8u) != 0u;

            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Reference, workload);
            Require(plan.valid && plan.selectsLegacyReference,
                "Every legacy-compatible workload selects Reference");
            Require(plan.configuration.profile ==
                    VisibilityPerformanceProfile::Reference &&
                    plan.configuration.bindings ==
                        VisibilityBindingStrategy::LegacyBroad,
                "Reference retains the exact original CPU-selected profile");
            Require(plan.resourceMask == ExpectedReferenceResources(workload),
                "Reference resource mask exactly matches the legacy contract");
            Require(plan.bindingMask == ExpectedReferenceBindings(),
                "Reference always retains the broad legacy binding layout");
            const uint64_t expectedPasses =
                ExpectedReferencePasses(workload);
            Require(plan.passMask == expectedPasses,
                "Reference pass mask exactly matches the legacy dispatches");
            Require(plan.dispatchCount ==
                    ExpectedReferenceDispatchCount(workload, expectedPasses),
                "Reference dispatch count includes every later bounce");
            Require(plan.optionalResourceMask == 0u &&
                    plan.candidateBindingMask == 0u &&
                    plan.candidatePassMask == 0u,
                "Reference incurs zero inactive candidate cost");
            Require(!plan.requiresExplicitHalfRoundtrip &&
                    plan.preservesProductionBitmask &&
                    !plan.benchmarkOnly,
                "Reference retains the product bitmask without fusion work");
        }
    }

    void TestCandidateResourcePlansExactly()
    {
        VisibilityPerformanceWorkload fastWorkload;
        fastWorkload.scheduler = VisibilityPerformanceScheduler::
            FilterAdaptedSpatiotemporalRankField;
        const VisibilityExecutionPlan packedFast =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactPackedCurrentFast,
                fastWorkload);
        const uint64_t expectedFastResources =
            ResourceBit(VisibilityExecutionResource::RawAmbientR16) |
            ResourceBit(VisibilityExecutionResource::FinalAmbientR16) |
            ResourceBit(VisibilityExecutionResource::PackedCurrentFastNoise);
        Require(packedFast.valid &&
                packedFast.resourceMask == expectedFastResources &&
                packedFast.firstTraceSrvCount == 3u &&
                packedFast.firstTraceUavCount == 1u &&
                packedFast.peakSrvCount == 8u &&
                packedFast.peakUavCount == 2u &&
                packedFast.optionalResourceMask == ResourceBit(
                    VisibilityExecutionResource::PackedCurrentFastNoise) &&
                packedFast.candidateBindingMask == BindingBit(
                    VisibilityExecutionBinding::PackedCurrentFastNoise) &&
                HasVisibilityExecutionPass(packedFast.passMask,
                    VisibilityExecutionPass::FixedTrace),
            "Packed FAST exposes only its fixed trace, packed texture, and binding");

        const VisibilityExecutionPlan constantTrace =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::DiagnosticConstantTrace, {});
        const VisibilityExecutionPlan bitmaskTrace =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::DiagnosticBitmaskOnlyTrace, {});
        const VisibilityExecutionPlan depthTrace =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::DiagnosticDepthOnlyTrace, {});
        Require(constantTrace.valid && bitmaskTrace.valid && depthTrace.valid &&
                constantTrace.firstTraceSrvCount == 0u &&
                constantTrace.firstTraceUavCount == 1u &&
                bitmaskTrace.firstTraceSrvCount == 0u &&
                bitmaskTrace.firstTraceUavCount == 1u &&
                depthTrace.firstTraceSrvCount == 4u &&
                depthTrace.firstTraceUavCount == 1u &&
                !HasVisibilityExecutionResource(
                    constantTrace.resourceMask,
                    VisibilityExecutionResource::LegacyToroidalNoise) &&
                !HasVisibilityExecutionResource(
                    bitmaskTrace.resourceMask,
                    VisibilityExecutionResource::LegacyToroidalNoise),
            "Diagnostic first-trace descriptor counts match their reflected layouts");

        VisibilityPerformanceWorkload temporalWorkload;
        temporalWorkload.temporalEnabled = true;
        const VisibilityExecutionPlan temporalReference =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Reference, temporalWorkload);
        Require(temporalReference.valid &&
                temporalReference.peakSrvCount == 9u &&
                temporalReference.peakUavCount == 4u,
            "Temporal reference reports the exact peak descriptor layouts");

        VisibilityPerformanceWorkload laterBounceWorkload;
        laterBounceWorkload.consumer =
            VisibilityPerformanceConsumer::AmbientOcclusionAndIndirectDiffuse;
        laterBounceWorkload.bounceCount = 2u;
        const VisibilityExecutionPlan laterBounceReference =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Reference,
                laterBounceWorkload);
        Require(laterBounceReference.valid &&
                laterBounceReference.peakSrvCount == 11u &&
                laterBounceReference.peakUavCount == 3u,
            "Legacy later bounce reports eleven SRVs while the broad first trace retains the three-UAV peak");

        auto invalidPackedSpatial = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2);
        VisibilityPerformanceWorkload packedSpatialWorkload;
        packedSpatialWorkload.spatialEnabled = true;
        Require(ResolveVisibilityExecutionPlan(
                invalidPackedSpatial, packedSpatialWorkload).error ==
                VisibilityPlanError::
                    PackedReconstructionDoesNotSupportSpatialFilter,
            "Packed reconstruction rejects a silently ignored legacy spatial filter");

        const VisibilityExecutionPlan packedEdges =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::AlgorithmicPackedEdges4x4, {});
        const uint64_t expectedEdgeResources =
            ResourceBit(VisibilityExecutionResource::RawAmbientR16) |
            ResourceBit(VisibilityExecutionResource::FinalAmbientR16) |
            ResourceBit(VisibilityExecutionResource::LegacyToroidalNoise) |
            ResourceBit(VisibilityExecutionResource::PackedEdgesR8Uint);
        Require(packedEdges.valid &&
                packedEdges.resourceMask == expectedEdgeResources &&
                packedEdges.optionalResourceMask == ResourceBit(
                    VisibilityExecutionResource::PackedEdgesR8Uint) &&
                packedEdges.candidateBindingMask == BindingBit(
                    VisibilityExecutionBinding::PackedEdges) &&
                !HasVisibilityExecutionResource(
                    packedEdges.resourceMask,
                    VisibilityExecutionResource::
                        RawAmbientPackedCountEdgesR16),
            "Packed edges use a separate conditional R8_UINT allocation");

        const VisibilityExecutionPlan fused = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::ExactFusedResolveApply, {});
        Require(fused.valid && fused.requiresExplicitHalfRoundtrip &&
                fused.resourceMask ==
                    (ResourceBit(VisibilityExecutionResource::RawAmbientR16) |
                     ResourceBit(
                         VisibilityExecutionResource::LegacyToroidalNoise) |
                     ResourceBit(
                         VisibilityExecutionResource::LegacyCurrentFastNoise)) &&
                fused.passMask ==
                    (PassBit(VisibilityExecutionPass::LegacyTrace) |
                     PassBit(
                         VisibilityExecutionPass::FusedResolveAndApply)) &&
                fused.dispatchCount == 2u,
            "Exact fusion removes the final AO texture and one dispatch while retaining the broad legacy trace contract");

        const VisibilityExecutionPlan fixed8 = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::ExactFixed8, {});
        const VisibilityExecutionPlan fixedFused =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::
                    ExactFixed8FusedResolveApply,
                {});
        Require(fixed8.valid && fixedFused.valid &&
                fixedFused.fixedFirstBounceSampleCount == 8u &&
                fixedFused.requiresExplicitHalfRoundtrip &&
                fixedFused.resourceMask ==
                    (ResourceBit(
                        VisibilityExecutionResource::RawAmbientR16) |
                     ResourceBit(
                        VisibilityExecutionResource::LegacyToroidalNoise)) &&
                fixedFused.passMask ==
                    (PassBit(VisibilityExecutionPass::FixedTrace) |
                     PassBit(
                        VisibilityExecutionPass::FusedResolveAndApply)) &&
                fixedFused.firstTraceSrvCount == 3u &&
                fixedFused.firstTraceUavCount == 1u &&
                fixedFused.dispatchCount == 2u &&
                !HasVisibilityExecutionResource(
                    fixedFused.resourceMask,
                    VisibilityExecutionResource::FinalAmbientR16) &&
                fixedFused.bindingMask == fixed8.bindingMask &&
                fixedFused.shaderPermutationKey !=
                    fixed8.shaderPermutationKey &&
                fixedFused.permutationKey != fixed8.permutationKey,
            "The exact combined profile retains fixed-8 bindings while removing the resolved AO texture and standalone reconstruction/application passes");

        const VisibilityExecutionPlan fusedPacked =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::
                    AlgorithmicFusedPackedEdges2x2,
                {});
        Require(fusedPacked.valid &&
                !fusedPacked.requiresExplicitHalfRoundtrip &&
                fusedPacked.resourceMask ==
                    (ResourceBit(
                        VisibilityExecutionResource::RawAmbientR16) |
                     ResourceBit(
                        VisibilityExecutionResource::LegacyToroidalNoise) |
                     ResourceBit(
                        VisibilityExecutionResource::PackedEdgesR8Uint)) &&
                HasVisibilityExecutionPass(fusedPacked.passMask,
                    VisibilityExecutionPass::FixedTrace) &&
                HasVisibilityExecutionPass(fusedPacked.passMask,
                    VisibilityExecutionPass::FusedResolveAndApply) &&
                !HasVisibilityExecutionPass(fusedPacked.passMask,
                    VisibilityExecutionPass::Reconstruction) &&
                !HasVisibilityExecutionResource(
                    fusedPacked.resourceMask,
                    VisibilityExecutionResource::FinalAmbientR16),
            "Packed-edge fusion retains only reduced AO and R8 edge storage");

        VisibilityPerformanceWorkload multiBounce;
        multiBounce.consumer = VisibilityPerformanceConsumer::
            AmbientOcclusionAndIndirectDiffuse;
        multiBounce.bounceCount = 2u;
        const VisibilityExecutionPlan fixedAll = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::ExactFixedAllBounce8, multiBounce);
        Require(fixedAll.valid &&
                fixedAll.fixedFirstBounceSampleCount == 8u &&
                fixedAll.fixedLaterBounceSampleCount == 8u &&
                HasVisibilityExecutionPass(fixedAll.passMask,
                    VisibilityExecutionPass::FixedTrace) &&
                HasVisibilityExecutionPass(fixedAll.passMask,
                    VisibilityExecutionPass::FixedLaterBounceTrace) &&
                !HasVisibilityExecutionPass(fixedAll.passMask,
                    VisibilityExecutionPass::LegacyLaterBounceTrace) &&
                fixedAll.dispatchCount == 4u,
            "All-bounce fixed8 selects both curated trace permutations");

        VisibilityPerformanceWorkload scheduleWorkload;
        scheduleWorkload.scheduler =
            VisibilityPerformanceScheduler::Activision4x4SixPhase;
        const VisibilityExecutionPlan schedule = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::AlgorithmicActivisionSchedule,
            scheduleWorkload);
        Require(schedule.valid &&
                schedule.resourceMask ==
                    (ResourceBit(
                        VisibilityExecutionResource::RawAmbientR16) |
                     ResourceBit(
                        VisibilityExecutionResource::FinalAmbientR16)) &&
                HasVisibilityExecutionPass(schedule.passMask,
                    VisibilityExecutionPass::FixedTrace),
            "The procedural Activision schedule allocates no noise texture");

        VisibilityPerformanceWorkload ps4Workload = scheduleWorkload;
        ps4Workload.firstBounceSampleCount = 8u;
        ps4Workload.radialExponent = 1.0f;
        ps4Workload.temporalEnabled = true;
        ps4Workload.spatialEnabled = true;
        const VisibilityExecutionPlan ps4 = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::ActivisionPs4Schedule,
            ps4Workload);
        const VisibilityExecutionPlan ps4Packed =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ActivisionPs4PackedGather,
                ps4Workload);
        const uint64_t expectedPs4Resources =
            ResourceBit(VisibilityExecutionResource::RawAmbientR16) |
            ResourceBit(VisibilityExecutionResource::FinalAmbientR16) |
            ResourceBit(VisibilityExecutionResource::TemporalAmbientR16) |
            ResourceBit(VisibilityExecutionResource::TemporalDepthR32) |
            ResourceBit(
                VisibilityExecutionResource::ActivisionSpatialAmbientR16) |
            ResourceBit(
                VisibilityExecutionResource::
                    ActivisionPackedDepthGuideR32Uint);
        const uint64_t expectedPs4Bindings =
            BindingBit(VisibilityExecutionBinding::Depth) |
            BindingBit(VisibilityExecutionBinding::Normals) |
            BindingBit(VisibilityExecutionBinding::MotionVectors) |
            BindingBit(VisibilityExecutionBinding::GBufferMaterial) |
            BindingBit(VisibilityExecutionBinding::BaseLighting) |
            BindingBit(VisibilityExecutionBinding::OutputLighting) |
            BindingBit(VisibilityExecutionBinding::AmbientHistory) |
            BindingBit(VisibilityExecutionBinding::AmbientOutput) |
            BindingBit(
                VisibilityExecutionBinding::ActivisionPreparedDepth);
        const uint64_t expectedPs4Passes =
            PassBit(VisibilityExecutionPass::DepthPreparation) |
            PassBit(VisibilityExecutionPass::ActivisionHorizonTrace) |
            PassBit(VisibilityExecutionPass::Temporal) |
            PassBit(VisibilityExecutionPass::SpatialDenoise) |
            PassBit(VisibilityExecutionPass::Reconstruction) |
            PassBit(VisibilityExecutionPass::Composition);
        Require(ps4.valid && ps4.benchmarkOnly &&
                ps4.fixedFirstBounceSampleCount == 8u &&
                ps4.resourceMask == expectedPs4Resources &&
                ps4.bindingMask == expectedPs4Bindings &&
                ps4.passMask == expectedPs4Passes &&
                ps4.firstTraceSrvCount == 3u &&
                ps4.firstTraceUavCount == 1u &&
                ps4.peakSrvCount == 8u &&
                ps4.peakUavCount == 2u &&
                !HasVisibilityExecutionResource(ps4.resourceMask,
                    VisibilityExecutionResource::RawAmbientR8) &&
                !HasVisibilityExecutionResource(ps4.resourceMask,
                    VisibilityExecutionResource::DepthHierarchy) &&
                !HasVisibilityExecutionResource(ps4.resourceMask,
                    VisibilityExecutionResource::TemporalNormalRgba8) &&
                !HasVisibilityExecutionBinding(ps4.bindingMask,
                    VisibilityExecutionBinding::DepthHierarchy) &&
                HasVisibilityExecutionResource(ps4.resourceMask,
                    VisibilityExecutionResource::
                        ActivisionSpatialAmbientR16) &&
                HasVisibilityExecutionResource(ps4.resourceMask,
                    VisibilityExecutionResource::
                        ActivisionPackedDepthGuideR32Uint) &&
                ps4.dispatchCount == 6u,
            "PS4 prototype reports trace, depth, spatial, temporal, and upsample work");
        Require(ps4Packed.valid && ps4Packed.benchmarkOnly &&
                ps4Packed.resourceMask == ps4.resourceMask &&
                ps4Packed.bindingMask == ps4.bindingMask &&
                ps4Packed.passMask == ps4.passMask &&
                ps4Packed.firstTraceSrvCount == ps4.firstTraceSrvCount &&
                ps4Packed.firstTraceUavCount == ps4.firstTraceUavCount &&
                ps4Packed.shaderPermutationKey != ps4.shaderPermutationKey &&
                ps4Packed.permutationKey != ps4.permutationKey &&
                ps4Packed.historyResetKey != ps4.historyResetKey,
            "Scalar and packed-gather PS4 controls isolate the spatial fetch strategy behind distinct shader and history identities");

        const VisibilityPerformanceWorkload xeWorkload =
            MakeCompatibleWorkload(
                VisibilityPerformanceProfile::XeGtaoClosestMatch);
        const VisibilityExecutionPlan xeLut = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::XeGtaoClosestMatch, xeWorkload);
        const VisibilityExecutionPlan xeInline =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::XeGtaoHighInlineHilbert,
                xeWorkload);
        const VisibilityExecutionPlan xeFp32 = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::XeGtaoHighFp32, xeWorkload);
        const uint64_t hilbertLut = ResourceBit(
            VisibilityExecutionResource::XeGtaoHilbertLutR16Uint);
        const uint64_t hilbertBinding = BindingBit(
            VisibilityExecutionBinding::XeGtaoHilbertLut);
        const uint64_t xeRequiredResources =
            ResourceBit(VisibilityExecutionResource::DepthHierarchy) |
            ResourceBit(VisibilityExecutionResource::PackedEdgesR8Unorm) |
            ResourceBit(VisibilityExecutionResource::XeGtaoWorkingAoR16) |
            ResourceBit(VisibilityExecutionResource::FinalAmbientR16);
        const uint64_t xeInlineBindings =
            BindingBit(VisibilityExecutionBinding::Depth) |
            BindingBit(VisibilityExecutionBinding::Normals) |
            BindingBit(VisibilityExecutionBinding::GBufferMaterial) |
            BindingBit(VisibilityExecutionBinding::BaseLighting) |
            BindingBit(VisibilityExecutionBinding::OutputLighting) |
            BindingBit(VisibilityExecutionBinding::DepthHierarchy) |
            BindingBit(VisibilityExecutionBinding::AmbientOutput) |
            BindingBit(VisibilityExecutionBinding::XeGtaoEdges);
        Require(xeLut.valid && xeInline.valid && xeFp32.valid &&
                xeInline.resourceMask == xeRequiredResources &&
                xeLut.resourceMask ==
                    (xeRequiredResources | hilbertLut) &&
                xeFp32.resourceMask ==
                    (xeRequiredResources | hilbertLut) &&
                HasVisibilityExecutionResource(
                    xeLut.resourceMask,
                    VisibilityExecutionResource::XeGtaoHilbertLutR16Uint) &&
                !HasVisibilityExecutionResource(
                    xeInline.resourceMask,
                    VisibilityExecutionResource::XeGtaoHilbertLutR16Uint) &&
                xeLut.resourceMask == (xeInline.resourceMask | hilbertLut) &&
                xeFp32.resourceMask == xeLut.resourceMask &&
                xeLut.optionalResourceMask ==
                    (xeInline.optionalResourceMask | hilbertLut) &&
                xeFp32.optionalResourceMask == xeLut.optionalResourceMask &&
                xeLut.firstTraceSrvCount == 3u &&
                xeInline.firstTraceSrvCount == 2u &&
                xeFp32.firstTraceSrvCount == 3u &&
                xeLut.firstTraceUavCount == 2u &&
                xeInline.firstTraceUavCount == 2u &&
                xeFp32.firstTraceUavCount == 2u &&
                xeLut.peakSrvCount == 8u &&
                xeLut.peakUavCount == 5u &&
                xeInline.peakSrvCount == 8u &&
                xeInline.peakUavCount == 5u &&
                xeFp32.peakSrvCount == 8u &&
                xeFp32.peakUavCount == 5u &&
                !HasVisibilityExecutionResource(
                    xeLut.resourceMask,
                    VisibilityExecutionResource::RawAmbientR16),
            "XeGTAO LUT delivery adds exactly one 64x64 R16_UINT resource and one trace SRV while inline Hilbert removes both");
        Require(xeInline.bindingMask == xeInlineBindings &&
                xeLut.bindingMask ==
                    (xeInline.bindingMask | hilbertBinding) &&
                xeFp32.bindingMask == xeLut.bindingMask &&
                xeLut.candidateBindingMask ==
                    (xeInline.candidateBindingMask | hilbertBinding) &&
                xeFp32.candidateBindingMask ==
                    xeLut.candidateBindingMask &&
                xeLut.passMask == xeInline.passMask &&
                xeLut.passMask == xeFp32.passMask &&
                HasVisibilityExecutionPass(
                    xeLut.passMask,
                    VisibilityExecutionPass::DepthPreparation) &&
                HasVisibilityExecutionPass(
                    xeLut.passMask,
                    VisibilityExecutionPass::XeGtaoHorizonTrace) &&
                HasVisibilityExecutionPass(
                    xeLut.passMask,
                    VisibilityExecutionPass::SpatialDenoise) &&
                !HasVisibilityExecutionPass(
                    xeLut.passMask,
                    VisibilityExecutionPass::Reconstruction) &&
                xeLut.dispatchCount == 4u &&
                !HasVisibilityExecutionResource(
                    xeLut.resourceMask,
                    VisibilityExecutionResource::LegacyToroidalNoise) &&
                !HasVisibilityExecutionResource(
                    xeInline.resourceMask,
                    VisibilityExecutionResource::LegacyCurrentFastNoise),
            "Every XeGTAO variant retains the same prefilter, horizon, denoise, and application pass graph while LUT delivery adds only its explicit binding");
        Require(xeLut.configuration.math ==
                    VisibilityMathMode::XeGtaoMixedPrecision &&
                xeFp32.configuration.math ==
                    VisibilityMathMode::ReferenceFp32 &&
                xeLut.shaderPermutationKey != xeInline.shaderPermutationKey &&
                xeLut.shaderPermutationKey != xeFp32.shaderPermutationKey &&
                xeInline.shaderPermutationKey != xeFp32.shaderPermutationKey &&
                xeLut.permutationKey != xeInline.permutationKey &&
                xeLut.permutationKey != xeFp32.permutationKey,
            "XeGTAO mixed precision, FP32, LUT, and inline-Hilbert variants retain distinct compiled identities");
    }

    void TestPermutationAndHistoryKeysAreComplete()
    {
        const VisibilityPerformanceWorkload baseWorkload;
        const VisibilityExecutionPlan base = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::Reference, baseWorkload);
        const VisibilityExecutionPlan repeated = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::Reference, baseWorkload);
        Require(base.valid && repeated.valid &&
                base.shaderPermutationKey ==
                    repeated.shaderPermutationKey &&
                base.permutationKey == repeated.permutationKey &&
                base.historyResetKey == repeated.historyResetKey &&
                base.permutationName == repeated.permutationName,
            "Identical settings produce stable plan identities");

        for (uint32_t runtimeVariant = 0u;
            runtimeVariant < 4u;
            ++runtimeVariant)
        {
            VisibilityPerformanceWorkload runtimeOnly = baseWorkload;
            switch (runtimeVariant)
            {
            case 0u:
                runtimeOnly.outputWidth = 1600u;
                runtimeOnly.outputHeight = 900u;
                break;
            case 1u:
                runtimeOnly.radius = 4.0f;
                break;
            case 2u:
                runtimeOnly.thickness = 0.75f;
                break;
            default:
                runtimeOnly.radialExponent = 3.0f;
                break;
            }
            const VisibilityExecutionPlan runtimePlan =
                ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Reference,
                    runtimeOnly);
            Require(runtimePlan.valid &&
                    runtimePlan.shaderPermutationKey ==
                        base.shaderPermutationKey &&
                    runtimePlan.permutationKey != base.permutationKey,
                "Runtime constants retain full evidence identity without "
                "duplicating cached shader pipelines");
        }
        Require(base.permutationName.find("output=1920x1080") !=
                std::string::npos &&
                base.permutationName.find("group=8x8") != std::string::npos &&
                base.permutationName.find("/trace=") != std::string::npos &&
                base.permutationName.find("/math=") != std::string::npos &&
                base.permutationName.find("/raw-ao=") != std::string::npos &&
                base.permutationName.find("/edges=") != std::string::npos &&
                base.permutationName.find("/application=") !=
                    std::string::npos,
            "The reported key names implementation and workload settings");

        std::vector<VisibilityPerformanceWorkload> variants;
        auto changed = baseWorkload;
        changed.consumer = VisibilityPerformanceConsumer::
            AmbientOcclusionAndIndirectDiffuse;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.estimator =
            VisibilityPerformanceEstimator::UniformProjectedAngle;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.resolution = VisibilityPerformanceResolution::Full;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.scheduler = VisibilityPerformanceScheduler::IndependentHash;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.firstBounceSampleCount = 12u;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.laterBounceSampleCount = 12u;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.bounceCount = 2u;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.outputWidth = 1919u;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.outputHeight = 1079u;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.radius = 4.0f;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.thickness = 0.75f;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.radialExponent = 3.0f;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.adaptiveSamplingEnabled = true;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.temporalEnabled = true;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.spatialEnabled = true;
        variants.push_back(changed);
        changed = baseWorkload;
        changed.depthHierarchyEnabled = true;
        variants.push_back(changed);

        std::set<uint64_t> permutationKeys = { base.permutationKey };
        std::set<uint64_t> historyKeys = { base.historyResetKey };
        for (const VisibilityPerformanceWorkload& variant : variants)
        {
            const VisibilityExecutionPlan plan = ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Reference, variant);
            Require(plan.valid && plan.permutationKey != base.permutationKey &&
                    plan.historyResetKey != base.historyResetKey,
                "Every result-affecting workload field changes both keys");
            Require(permutationKeys.insert(plan.permutationKey).second &&
                    historyKeys.insert(plan.historyResetKey).second,
                "Every tested workload variant has a distinct identity");
        }

        const VisibilityExecutionPlan generic = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::GenericFallback, baseWorkload);
        Require(generic.valid &&
                generic.permutationKey != base.permutationKey &&
                generic.historyResetKey != base.historyResetKey,
            "A profile switch always invalidates accumulated history");
        Require(!generic.selectsLegacyReference &&
                generic.configuration.bindings ==
                    VisibilityBindingStrategy::LegacyBroad &&
                generic.resourceMask == base.resourceMask &&
                generic.bindingMask == base.bindingMask &&
                generic.passMask == base.passMask,
            "Generic fallback honestly models the broad legacy runtime pipeline");
    }

    void TestInvalidWorkloadsExhaustively()
    {
        const VisibilityPerformanceWorkload valid;
        std::vector<VisibilityPerformanceWorkload> invalidWorkloads;
        auto invalid = valid;
        invalid.consumer = static_cast<VisibilityPerformanceConsumer>(255u);
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.estimator = static_cast<VisibilityPerformanceEstimator>(255u);
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.resolution = static_cast<VisibilityPerformanceResolution>(255u);
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.scheduler = static_cast<VisibilityPerformanceScheduler>(255u);
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.firstBounceSampleCount = 0u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.firstBounceSampleCount = 65u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.laterBounceSampleCount = 0u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.laterBounceSampleCount = 65u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.bounceCount = 0u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.bounceCount = 9u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.outputWidth = 0u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.outputWidth = 16385u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.outputHeight = 0u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.outputHeight = 16385u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.radius = 0.0f;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.radius = std::numeric_limits<float>::quiet_NaN();
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.thickness = -0.01f;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.thickness = std::numeric_limits<float>::infinity();
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.radialExponent = 0.0f;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.radialExponent =
            std::numeric_limits<float>::quiet_NaN();
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.threadGroupSizeX = 0u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.threadGroupSizeY = 0u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.threadGroupSizeX = 1024u;
        invalid.threadGroupSizeY = 2u;
        invalidWorkloads.push_back(invalid);

        for (const VisibilityPerformanceWorkload& workload : invalidWorkloads)
        {
            const VisibilityExecutionPlan plan = ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Reference, workload);
            Require(!plan.valid &&
                    plan.error == VisibilityPlanError::InvalidWorkload &&
                    !plan.errorMessage.empty(),
                "Every malformed workload fails before plan construction");
        }
    }

    void TestInvalidProfileSafeguards()
    {
        VisibilityPerformanceProfileConfiguration incomplete =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::GenericFallback);
        incomplete.assignmentMask = 0u;
        Require(ResolveVisibilityExecutionPlan(incomplete, {}).error ==
                VisibilityPlanError::IncompleteProfile,
            "An incompletely assigned profile is rejected");

        auto changedReference = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Reference);
        changedReference.bindings = VisibilityBindingStrategy::MinimalConditional;
        Require(ResolveVisibilityExecutionPlan(changedReference, {}).error ==
                VisibilityPlanError::ReferenceContractViolation,
            "Reference cannot silently adopt a candidate binding layout");
        changedReference = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Reference);
        changedReference.edgeStorage = VisibilityEdgeStorage::R8Uint;
        Require(ResolveVisibilityExecutionPlan(changedReference, {}).error ==
                VisibilityPlanError::ReferenceContractViolation,
            "Reference cannot silently allocate candidate edge metadata");
        changedReference = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Reference);
        changedReference.implementationStatus =
            VisibilityImplementationStatus::Unavailable;
        changedReference.implementationNote = "Mutated reference";
        Require(ResolveVisibilityExecutionPlan(changedReference, {}).error ==
                VisibilityPlanError::ReferenceContractViolation,
            "Reference contract validation precedes candidate availability");

        VisibilityPerformanceWorkload wrongCount;
        wrongCount.firstBounceSampleCount = 12u;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactPackedCurrentFast,
                wrongCount).error ==
                VisibilityPlanError::SampleCountMismatch,
            "Packed FAST cannot pretend its fixed8 wrapper is generic");

        VisibilityPerformanceWorkload wrongExponent;
        wrongExponent.radialExponent = 1.5f;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactFixed8,
                wrongExponent).error ==
                VisibilityPlanError::FixedExponentMismatch,
            "Fixed shaders require their compiled quadratic exponent");

        VisibilityPerformanceWorkload adaptive;
        adaptive.adaptiveSamplingEnabled = true;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactFixed8, adaptive).error ==
                VisibilityPlanError::AdaptiveFixedCountConflict,
            "Adaptive sampling cannot select a fixed-count permutation");

        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactFixedLaterBounce8, {}).error ==
                VisibilityPlanError::
                    LaterBounceSpecializationRequiresIndirectDiffuse,
            "A later-bounce profile requires a later GI bounce");

        VisibilityPerformanceWorkload fast;
        fast.scheduler = VisibilityPerformanceScheduler::
            FilterAdaptedSpatiotemporalRankField;
        fast.estimator =
            VisibilityPerformanceEstimator::UniformProjectedAngle;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactPackedCurrentFast,
                fast).error == VisibilityPlanError::ProfileEstimatorMismatch,
            "Packed FAST reports its hard-coded Uniform Solid Angle estimator");
        fast.estimator = VisibilityPerformanceEstimator::UniformSolidAngle;
        fast.consumer = VisibilityPerformanceConsumer::IndirectDiffuse;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactPackedCurrentFast,
                fast).error == VisibilityPlanError::ProfileConsumerMismatch,
            "Packed FAST reports that its wrapper always writes AO");

        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactPackedCurrentFast, {}).error ==
                VisibilityPlanError::ProfileSchedulerMismatch,
            "Packed FAST cannot reinterpret a different scheduler");
        VisibilityPerformanceWorkload activisionScheduler;
        activisionScheduler.scheduler =
            VisibilityPerformanceScheduler::Activision4x4SixPhase;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Reference,
                activisionScheduler).error ==
                VisibilityPlanError::ProfileSchedulerMismatch,
            "Reference cannot silently select a candidate scheduler");

        VisibilityPerformanceWorkload xeMismatch = MakeCompatibleWorkload(
            VisibilityPerformanceProfile::XeGtaoClosestMatch);
        xeMismatch.firstBounceSampleCount = 16u;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::XeGtaoClosestMatch,
                xeMismatch).error == VisibilityPlanError::SampleCountMismatch,
            "XeGTAO High rejects a workload that does not retain its 18 horizon samples");
        xeMismatch = MakeCompatibleWorkload(
            VisibilityPerformanceProfile::XeGtaoHighInlineHilbert);
        xeMismatch.resolution = VisibilityPerformanceResolution::Half;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::XeGtaoHighInlineHilbert,
                xeMismatch).error ==
                    VisibilityPlanError::ProfileResolutionMismatch,
            "The source-faithful XeGTAO High controls reject reduced-resolution tracing");
        xeMismatch = MakeCompatibleWorkload(
            VisibilityPerformanceProfile::XeGtaoHighFp32);
        xeMismatch.scheduler =
            VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::XeGtaoHighFp32,
                xeMismatch).error ==
                    VisibilityPlanError::ProfileSchedulerMismatch,
            "XeGTAO FP32 cannot silently replace Hilbert/R2 scheduling with legacy noise");

        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::DiagnosticTemporalCopy, {}).error ==
                VisibilityPlanError::TemporalModeRequiresHistory,
            "Temporal-copy diagnostics require active history");

        auto rawR8 = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::GenericFallback);
        rawR8.rawAoStorage = VisibilityRawAoStorage::R8Unorm;
        rawR8.consumerRequirement =
            VisibilityConsumerRequirement::AmbientOcclusionOnly;
        Require(ResolveVisibilityExecutionPlan(rawR8, {}).error ==
                VisibilityPlanError::UnsupportedAoEncoding,
            "Uniform Solid Angle cannot be clamped into raw R8_UNORM");
        VisibilityPerformanceWorkload projected;
        projected.estimator =
            VisibilityPerformanceEstimator::UniformProjectedAngle;
        const VisibilityExecutionPlan projectedR8 =
            ResolveVisibilityExecutionPlan(rawR8, projected);
        Require(!projectedR8.valid && projectedR8.error ==
                VisibilityPlanError::ProfileImplementationUnavailable &&
                projectedR8.errorMessage.find("mathematically permitted") !=
                    std::string::npos,
            "Bounded raw R8 remains unavailable without a compiled writer");

        auto separateEdges = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::GenericFallback);
        separateEdges.edgeStorage = VisibilityEdgeStorage::R8Uint;
        separateEdges.reconstruction =
            VisibilityReconstructionMode::PackedEdges2x2;
        separateEdges.consumerRequirement =
            VisibilityConsumerRequirement::AmbientOcclusionOnly;
        separateEdges.resolutionRequirement =
            VisibilityResolutionRequirement::Reduced;
        VisibilityPerformanceWorkload cosine;
        cosine.estimator =
            VisibilityPerformanceEstimator::CosineWeightedSolidAngle;
        const VisibilityExecutionPlan cosineEdges =
            ResolveVisibilityExecutionPlan(separateEdges, cosine);
        Require(cosineEdges.valid &&
                HasVisibilityExecutionResource(cosineEdges.resourceMask,
                    VisibilityExecutionResource::PackedEdgesR8Uint) &&
                HasVisibilityExecutionResource(cosineEdges.resourceMask,
                    VisibilityExecutionResource::RawAmbientR16),
            "Separate R8 edge metadata remains valid for every estimator");
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2,
                cosine).error ==
                VisibilityPlanError::ProfileEstimatorMismatch,
            "The curated edge wrapper still reports its hard-coded estimator");
        separateEdges.edgeStorage = VisibilityEdgeStorage::R8Unorm;
        Require(ResolveVisibilityExecutionPlan(separateEdges, cosine).error ==
                VisibilityPlanError::ProfileImplementationUnavailable,
            "R8_UNORM edges remain unavailable without compiled permutations");

        auto embedded = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::GenericFallback);
        embedded.rawAoStorage =
            VisibilityRawAoStorage::PackedCountAndEdgesR16Uint;
        embedded.reconstruction =
            VisibilityReconstructionMode::PackedEdges2x2;
        embedded.consumerRequirement =
            VisibilityConsumerRequirement::AmbientOcclusionOnly;
        embedded.estimatorRequirement =
            VisibilityEstimatorRequirement::UniformProjectedAngle;
        embedded.resolutionRequirement =
            VisibilityResolutionRequirement::Reduced;
        const VisibilityExecutionPlan embeddedPlan =
            ResolveVisibilityExecutionPlan(embedded, projected);
        Require(!embeddedPlan.valid && embeddedPlan.error ==
                VisibilityPlanError::ProfileImplementationUnavailable &&
                embeddedPlan.errorMessage.find("no compiled trace") !=
                    std::string::npos,
            "Packed R16 count-and-edge storage is not claimed as implemented");

        auto mismatchedEdges = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::GenericFallback);
        mismatchedEdges.edgeStorage = VisibilityEdgeStorage::R8Uint;
        Require(ResolveVisibilityExecutionPlan(mismatchedEdges, {}).error ==
                VisibilityPlanError::InvalidPackedReconstruction,
            "An edge allocation requires a packed-edge consumer");

        VisibilityPerformanceWorkload fusedGi;
        fusedGi.consumer = VisibilityPerformanceConsumer::
            AmbientOcclusionAndIndirectDiffuse;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactFusedResolveApply,
                fusedGi).error ==
                VisibilityPlanError::ProfileConsumerMismatch,
            "Exact fused application cannot reorder GI composition");
        VisibilityPerformanceWorkload fullResolution;
        fullResolution.resolution = VisibilityPerformanceResolution::Full;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactFusedResolveApply,
                fullResolution).error ==
                VisibilityPlanError::ProfileResolutionMismatch,
            "Resolve fusion requires a reduced-resolution source");
        auto inexactFusion = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::ExactFusedResolveApply);
        inexactFusion.explicitHalfRoundtrip = false;
        Require(ResolveVisibilityExecutionPlan(inexactFusion, {}).error ==
                VisibilityPlanError::FusedApplicationRequiresHalfRoundtrip,
            "Exact fusion cannot omit the eliminated R16F roundtrip");
        VisibilityPerformanceWorkload spatialFusion;
        spatialFusion.spatialEnabled = true;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactFusedResolveApply,
                spatialFusion).error == VisibilityPlanError::
                    FusedApplicationDoesNotSupportSpatialFilter,
            "Exact 2x2 fusion cannot silently discard spatial filtering");

        auto reorderedGi = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::GenericFallback);
        reorderedGi.traversal = VisibilityTraversalOrder::GroupedBySide;
        VisibilityPerformanceWorkload gi;
        gi.consumer = VisibilityPerformanceConsumer::IndirectDiffuse;
        Require(ResolveVisibilityExecutionPlan(reorderedGi, gi).error ==
                VisibilityPlanError::IndirectDiffuseTraversalReordered,
            "GI cannot change near-to-far source ownership");

        auto productionHorizon = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::ActivisionClosestMatch);
        productionHorizon.benchmarkOnly = false;
        Require(ResolveVisibilityExecutionPlan(
                productionHorizon,
                MakeCompatibleWorkload(
                    VisibilityPerformanceProfile::ActivisionClosestMatch)).error ==
                VisibilityPlanError::BenchmarkProfileContractViolation,
            "Horizon GTAO cannot masquerade as the product bitmask");

        VisibilityPerformanceWorkload wrongGroup;
        wrongGroup.threadGroupSizeX = 16u;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Reference, wrongGroup).error ==
                VisibilityPlanError::ProfileThreadGroupMismatch,
            "No plan claims an unmodeled thread-group diagnostic");

        VisibilityPerformanceWorkload group16x8;
        group16x8.threadGroupSizeX = 16u;
        const VisibilityExecutionPlan group16x8Plan =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactGroup16x8, group16x8);
        Require(group16x8Plan.valid &&
                group16x8Plan.configuration.optimizationClass ==
                    VisibilityOptimizationClass::Exact &&
                group16x8Plan.fixedFirstBounceSampleCount == 8u,
            "The compiled 16x8 fixed8 trace profile is independently selectable");

        VisibilityPerformanceWorkload group8x16;
        group8x16.threadGroupSizeY = 16u;
        const VisibilityExecutionPlan group8x16Plan =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactGroup8x16, group8x16);
        Require(group8x16Plan.valid &&
                group8x16Plan.configuration.optimizationClass ==
                    VisibilityOptimizationClass::Exact &&
                group8x16Plan.fixedFirstBounceSampleCount == 8u,
            "The compiled 8x16 fixed8 trace profile is independently selectable");

        group16x8.threadGroupSizeX = 8u;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactGroup16x8,
                group16x8).error ==
                VisibilityPlanError::ProfileThreadGroupMismatch,
            "The 16x8 profile rejects an 8x8 dispatch contract");
    }

    void TestRequestedVerificationProfilesAndReasons()
    {
        const std::array<VisibilityVerificationProfile, 13> profiles = {
            VisibilityVerificationProfile::ReferenceAo8T,
            VisibilityVerificationProfile::ExactFastAo8T,
            VisibilityVerificationProfile::MixedPrecisionAo8T,
            VisibilityVerificationProfile::PackedEdgeAo8T,
            VisibilityVerificationProfile::ActivisionScheduleAo8T,
            VisibilityVerificationProfile::ReferenceAoGi8T,
            VisibilityVerificationProfile::ExactFastAoGi8T,
            VisibilityVerificationProfile::ExactFastAoGi12T,
            VisibilityVerificationProfile::ExactFastAoGi16T,
            VisibilityVerificationProfile::ExactFastMultiBounce,
            VisibilityVerificationProfile::AggressiveExperimentalAo8T,
            VisibilityVerificationProfile::XeGtaoClosestMatch,
            VisibilityVerificationProfile::Ps4GtaoClosestMatch
        };

        std::set<uint64_t> validKeys;
        uint32_t unavailableCount = 0u;
        for (VisibilityVerificationProfile profile : profiles)
        {
            const VisibilityVerificationProfileDefinition definition =
                GetVisibilityVerificationProfileDefinition(profile);
            Require(definition.profile == profile && !definition.name.empty(),
                "Every requested one-click profile has a definition");
            Require(definition.implementationStatus !=
                    VisibilityImplementationStatus::Unset,
                "Every requested profile states availability");
            const bool ps4Prototype = profile ==
                VisibilityVerificationProfile::Ps4GtaoClosestMatch;
            const bool xeGtao = profile ==
                VisibilityVerificationProfile::XeGtaoClosestMatch;
            Require(definition.expectedWorkload.outputWidth == 1920u &&
                    definition.expectedWorkload.outputHeight == 1080u &&
                    definition.expectedWorkload.resolution ==
                        (xeGtao
                            ? VisibilityPerformanceResolution::Full
                            : VisibilityPerformanceResolution::Half) &&
                    definition.expectedWorkload.estimator ==
                        VisibilityPerformanceEstimator::UniformSolidAngle &&
                    definition.expectedWorkload.radius ==
                        (xeGtao ? 0.5f : 3.0f) &&
                    definition.expectedWorkload.thickness ==
                        (xeGtao ? 0.0f : 0.5f) &&
                    definition.expectedWorkload.radialExponent ==
                        (ps4Prototype ? 1.0f : 2.0f) &&
                    definition.expectedWorkload.threadGroupSizeX == 8u &&
                    definition.expectedWorkload.threadGroupSizeY == 8u &&
                    !definition.expectedWorkload.adaptiveSamplingEnabled &&
                    definition.expectedWorkload.temporalEnabled ==
                        ps4Prototype &&
                    definition.expectedWorkload.spatialEnabled ==
                        (ps4Prototype || xeGtao) &&
                    !definition.expectedWorkload.depthHierarchyEnabled,
                "Every one-click definition explicitly assigns target settings");
            if (xeGtao)
            {
                Require(definition.implementationStatus ==
                            VisibilityImplementationStatus::Implemented &&
                        definition.implementationProfile ==
                            VisibilityPerformanceProfile::
                                XeGtaoClosestMatch &&
                        definition.expectedWorkload.scheduler ==
                            VisibilityPerformanceScheduler::XeGtaoHilbertR2 &&
                        definition.expectedWorkload.firstBounceSampleCount ==
                            18u &&
                        definition.expectedWorkload.laterBounceSampleCount ==
                            9u,
                    "The XeGTAO one-click preset selects the implemented full-resolution High contract");
                VisibilityPerformanceWorkload runtimeDerivedWorkload =
                    definition.expectedWorkload;
                runtimeDerivedWorkload.laterBounceSampleCount =
                    std::max(
                        1u,
                        runtimeDerivedWorkload.firstBounceSampleCount / 2u);
                const VisibilityVerificationProfileResolution
                    runtimeDerivedResolution =
                        ResolveVisibilityVerificationProfile(
                            profile, runtimeDerivedWorkload);
                Require(
                    runtimeDerivedResolution.valid &&
                        runtimeDerivedResolution.executionPlan.valid,
                    "The one-click XeGTAO settings match the runtime-derived later-bounce count");
            }
            if (ps4Prototype)
            {
                Require(definition.implementationStatus ==
                            VisibilityImplementationStatus::Implemented &&
                        definition.implementationProfile ==
                            VisibilityPerformanceProfile::
                                ActivisionPs4Schedule &&
                        definition.expectedWorkload.scheduler ==
                            VisibilityPerformanceScheduler::
                                Activision4x4SixPhase &&
                        definition.expectedWorkload.firstBounceSampleCount ==
                            8u,
                    "The PS4 one-click preset selects the repaired scalar prototype");
            }

            const VisibilityVerificationProfileResolution resolution =
                ResolveVisibilityVerificationProfile(
                    profile, definition.expectedWorkload);
            Require(!resolution.reason.empty(),
                "Every requested profile returns a validity reason");
            if (definition.implementationStatus ==
                VisibilityImplementationStatus::Unavailable)
            {
                ++unavailableCount;
                Require(!resolution.valid &&
                        resolution.reason == definition.implementationNote &&
                        !definition.implementationNote.empty(),
                    "Unavailable one-click profiles fail with their exact reason");
            }
            else
            {
                Require(resolution.valid && resolution.executionPlan.valid,
                    std::string("Implemented one-click profile resolves: ") +
                        std::string(definition.name) + " (" +
                        resolution.reason + ")");
                Require(validKeys.insert(
                        resolution.executionPlan.permutationKey).second,
                    "Every valid one-click profile has a distinct key");
                if (definition.implementationStatus ==
                    VisibilityImplementationStatus::PartialBenchmarkControl)
                {
                    Require(resolution.reason.find(
                            "Valid partial benchmark control:") == 0u,
                        "Partial controls are visibly labeled as partial");
                }
                if (profile ==
                        VisibilityVerificationProfile::ReferenceAo8T ||
                    profile ==
                        VisibilityVerificationProfile::ReferenceAoGi8T)
                {
                    Require(resolution.executionPlan.selectsLegacyReference &&
                            resolution.executionPlan.optionalResourceMask == 0u &&
                            resolution.executionPlan.candidateBindingMask == 0u &&
                            resolution.executionPlan.candidatePassMask == 0u,
                        "Reference one-click profiles retain zero candidate cost");
                }
            }
        }
        Require(unavailableCount == 2u,
            "Only the uncompiled mixed-precision and aggressive presets remain unavailable");

        const auto reference = GetVisibilityVerificationProfileDefinition(
            VisibilityVerificationProfile::ReferenceAo8T);
        std::vector<VisibilityPerformanceWorkload> mismatches;
        auto changed = reference.expectedWorkload;
        changed.consumer = VisibilityPerformanceConsumer::
            AmbientOcclusionAndIndirectDiffuse;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.estimator =
            VisibilityPerformanceEstimator::UniformProjectedAngle;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.resolution = VisibilityPerformanceResolution::Full;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.scheduler = VisibilityPerformanceScheduler::IndependentHash;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.firstBounceSampleCount = 12u;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.laterBounceSampleCount = 12u;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.bounceCount = 2u;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.outputWidth = 1280u;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.outputHeight = 720u;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.radius = 4.0f;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.thickness = 0.6f;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.radialExponent = 1.0f;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.threadGroupSizeX = 16u;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.adaptiveSamplingEnabled = true;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.temporalEnabled = true;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.spatialEnabled = true;
        mismatches.push_back(changed);
        changed = reference.expectedWorkload;
        changed.depthHierarchyEnabled = true;
        mismatches.push_back(changed);

        for (const VisibilityPerformanceWorkload& mismatch : mismatches)
        {
            const auto resolution = ResolveVisibilityVerificationProfile(
                VisibilityVerificationProfile::ReferenceAo8T, mismatch);
            Require(!resolution.valid && !resolution.reason.empty(),
                "Every stale or changed profile field clears Profile Valid");
        }

        const auto wrongImplementation = ResolveVisibilityVerificationProfile(
            VisibilityVerificationProfile::ReferenceAo8T,
            VisibilityPerformanceProfile::GenericFallback,
            reference.expectedWorkload);
        Require(!wrongImplementation.valid &&
                wrongImplementation.reason.find("Implementation profile") == 0u,
            "A stale math, format, filter, or application profile is invalid");

        Require(GetVisibilityVerificationProfileDefinition(
                VisibilityVerificationProfile::Unset).profile ==
                VisibilityVerificationProfile::Unset &&
                !ResolveVisibilityVerificationProfile(
                    VisibilityVerificationProfile::Count, {}).valid,
            "Verification sentinels cannot masquerade as profiles");
    }
}

int main()
{
    TestCountAndEdgePackingExhaustively();
    TestPackedEdgeGenerationReferenceCases();
    TestFixedInterleavedOrders();
    TestFixedSpecializationsAgainstRuntimeTraversal();
    TestEveryPerformanceProfileIsHonestAndFullyAssigned();
    TestReferenceContractExhaustively();
    TestCandidateResourcePlansExactly();
    TestPermutationAndHistoryKeysAreComplete();
    TestInvalidWorkloadsExhaustively();
    TestInvalidProfileSafeguards();
    TestRequestedVerificationProfilesAndReasons();

    std::cout << "UVSR visibility performance plan validation passed\n";
    return EXIT_SUCCESS;
}
