#include "visibility_performance_plan.h"

#include <algorithm>
#include <array>
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

    bool IsReduced(VisibilityPerformanceResolution resolution)
    {
        return resolution == VisibilityPerformanceResolution::Half ||
            resolution == VisibilityPerformanceResolution::Quarter;
    }

    bool IsFused(
        const VisibilityPerformanceProfileConfiguration& configuration)
    {
        return configuration.application ==
                VisibilityApplicationMode::FusedResolveAndApplyExact ||
            configuration.application ==
                VisibilityApplicationMode::FusedResolveAndApplyPackedEdges;
    }

    bool IsPacked(
        const VisibilityPerformanceProfileConfiguration& configuration)
    {
        return configuration.edgeStorage == VisibilityEdgeStorage::R8Uint &&
            configuration.reconstruction ==
                VisibilityReconstructionMode::PackedEdges2x2;
    }

    uint64_t ExpectedResources(
        const VisibilityPerformanceProfileConfiguration& configuration,
        const VisibilityPerformanceWorkload& workload)
    {
        const bool hasAo = HasAo(workload.consumer);
        const bool hasGi = HasGi(workload.consumer);
        const bool needsReconstruction = workload.spatialEnabled ||
            IsReduced(workload.resolution) ||
            configuration.reconstruction !=
                VisibilityReconstructionMode::Legacy;

        uint64_t mask =
            ResourceBit(VisibilityExecutionResource::ToroidalNoise);
        if (hasAo)
            mask |= ResourceBit(VisibilityExecutionResource::RawAmbient);
        if (hasGi)
        {
            mask |= ResourceBit(VisibilityExecutionResource::RawIndirect);
            if (workload.bounceCount > 1u)
            {
                mask |= ResourceBit(
                    VisibilityExecutionResource::CumulativeIndirect);
            }
        }
        if (configuration.edgeStorage == VisibilityEdgeStorage::R8Uint)
        {
            mask |= ResourceBit(
                VisibilityExecutionResource::PackedEdgesR8Uint);
        }
        if (workload.temporalEnabled)
        {
            mask |=
                ResourceBit(VisibilityExecutionResource::TemporalDepth) |
                ResourceBit(
                    VisibilityExecutionResource::TemporalNormalRgba8);
            if (hasAo)
            {
                mask |= ResourceBit(
                    VisibilityExecutionResource::TemporalAmbient);
            }
            if (hasGi)
            {
                mask |= ResourceBit(
                    VisibilityExecutionResource::TemporalIndirect);
            }
        }
        if (needsReconstruction && !IsFused(configuration))
        {
            if (hasAo)
                mask |= ResourceBit(VisibilityExecutionResource::FinalAmbient);
            if (hasGi)
            {
                mask |= ResourceBit(
                    VisibilityExecutionResource::FinalIndirect);
            }
        }
        if (workload.depthHierarchyEnabled)
        {
            mask |= ResourceBit(
                VisibilityExecutionResource::DepthHierarchy);
        }
        return mask;
    }

    uint64_t ExpectedBindings(
        const VisibilityPerformanceProfileConfiguration& configuration,
        const VisibilityPerformanceWorkload& workload)
    {
        const bool hasAo = HasAo(workload.consumer);
        const bool hasGi = HasGi(workload.consumer);
        uint64_t mask =
            BindingBit(VisibilityExecutionBinding::Depth) |
            BindingBit(VisibilityExecutionBinding::Normals) |
            BindingBit(VisibilityExecutionBinding::ToroidalNoise) |
            BindingBit(VisibilityExecutionBinding::GBufferMaterial) |
            BindingBit(VisibilityExecutionBinding::BaseLighting) |
            BindingBit(VisibilityExecutionBinding::OutputLighting);
        if (workload.temporalEnabled)
            mask |= BindingBit(VisibilityExecutionBinding::MotionVectors);
        if (hasAo)
            mask |= BindingBit(VisibilityExecutionBinding::AmbientOutput);
        if (hasGi)
        {
            mask |=
                BindingBit(VisibilityExecutionBinding::SourceRadiance) |
                BindingBit(VisibilityExecutionBinding::IndirectOutput);
        }
        if (workload.temporalEnabled && hasAo)
        {
            mask |= BindingBit(
                VisibilityExecutionBinding::AmbientHistory);
        }
        if (workload.temporalEnabled && hasGi)
        {
            mask |= BindingBit(
                VisibilityExecutionBinding::IndirectHistory);
        }
        if (workload.depthHierarchyEnabled)
        {
            mask |= BindingBit(
                VisibilityExecutionBinding::DepthHierarchy);
        }
        if (IsPacked(configuration))
            mask |= BindingBit(VisibilityExecutionBinding::PackedEdges);
        return mask;
    }

    uint64_t ExpectedPasses(
        const VisibilityPerformanceProfileConfiguration& configuration,
        const VisibilityPerformanceWorkload& workload)
    {
        const bool hasGi = HasGi(workload.consumer);
        const bool needsReconstruction = workload.spatialEnabled ||
            IsReduced(workload.resolution) ||
            configuration.reconstruction !=
                VisibilityReconstructionMode::Legacy;

        uint64_t mask = PassBit(VisibilityExecutionPass::RuntimeTrace);
        if (workload.depthHierarchyEnabled)
            mask |= PassBit(VisibilityExecutionPass::DepthPreparation);
        if (hasGi && workload.bounceCount > 1u)
        {
            mask |= PassBit(
                VisibilityExecutionPass::RuntimeLaterBounceTrace);
        }
        if (workload.temporalEnabled)
            mask |= PassBit(VisibilityExecutionPass::Temporal);
        if (IsFused(configuration))
        {
            mask |= PassBit(
                VisibilityExecutionPass::FusedResolveAndApply);
        }
        else
        {
            if (needsReconstruction)
                mask |= PassBit(VisibilityExecutionPass::Reconstruction);
            mask |= PassBit(VisibilityExecutionPass::Composition);
        }
        return mask;
    }

    uint32_t ExpectedDispatchCount(
        const VisibilityPerformanceWorkload& workload,
        uint64_t passMask)
    {
        uint32_t count = CountBits(passMask);
        if (HasGi(workload.consumer) && workload.bounceCount > 2u)
            count += workload.bounceCount - 2u;
        if (HasGi(workload.consumer) && workload.bounceCount > 8u)
            count += workload.bounceCount - 1u;
        return count;
    }

    uint32_t ExpectedFirstTraceSrvCount(
        const VisibilityPerformanceWorkload& workload)
    {
        return 4u + (HasGi(workload.consumer) ? 1u : 0u);
    }

    uint32_t ExpectedFirstTraceUavCount(
        const VisibilityPerformanceProfileConfiguration& configuration,
        const VisibilityPerformanceWorkload& workload)
    {
        return (HasAo(workload.consumer) ? 1u : 0u) +
            (HasGi(workload.consumer) ? 1u : 0u) +
            (configuration.edgeStorage != VisibilityEdgeStorage::None
                ? 1u : 0u);
    }

    std::array<uint32_t, 2> ExpectedPeakDescriptorCounts(
        const VisibilityPerformanceProfileConfiguration& configuration,
        const VisibilityPerformanceWorkload& workload)
    {
        uint32_t peakSrv = ExpectedFirstTraceSrvCount(workload);
        uint32_t peakUav =
            ExpectedFirstTraceUavCount(configuration, workload);
        const auto includeLayout = [&peakSrv, &peakUav](
            uint32_t srvCount,
            uint32_t uavCount)
        {
            peakSrv = std::max(peakSrv, srvCount);
            peakUav = std::max(peakUav, uavCount);
        };

        if (workload.depthHierarchyEnabled)
            includeLayout(1u, 5u);
        if (HasGi(workload.consumer) && workload.bounceCount > 1u)
        {
            includeLayout(8u,
                workload.bounceCount > 8u ? 3u : 2u);
        }
        if (workload.temporalEnabled)
            includeLayout(9u, 4u);
        if (IsFused(configuration))
        {
            includeLayout(11u, 1u);
        }
        else
        {
            const bool needsReconstruction = workload.spatialEnabled ||
                IsReduced(workload.resolution) ||
                configuration.reconstruction !=
                    VisibilityReconstructionMode::Legacy;
            if (needsReconstruction)
            {
                if (IsPacked(configuration))
                    includeLayout(2u, 1u);
                else
                    includeLayout(4u, 2u);
            }
            includeLayout(12u, 1u);
        }
        return { peakSrv, peakUav };
    }

    void RequireExactPlan(
        const VisibilityExecutionPlan& plan,
        const VisibilityPerformanceProfileConfiguration& configuration,
        const VisibilityPerformanceWorkload& workload,
        const std::string& context)
    {
        Require(plan.valid,
            context + " resolves (" + plan.errorMessage + ")");
        const uint64_t expectedResources =
            ExpectedResources(configuration, workload);
        const uint64_t expectedBindings =
            ExpectedBindings(configuration, workload);
        const uint64_t expectedPasses =
            ExpectedPasses(configuration, workload);
        const std::array<uint32_t, 2> expectedPeak =
            ExpectedPeakDescriptorCounts(configuration, workload);
        Require(plan.resourceMask == expectedResources,
            context + " has the exact allocation families");
        Require(plan.bindingMask == expectedBindings,
            context + " has the exact conceptual binding union");
        Require(plan.passMask == expectedPasses,
            context + " has the exact dispatch family");
        Require(plan.optionalResourceMask ==
                (expectedResources & VisibilityOptionalResourceMask),
            context + " reports only selected optional resources");
        Require(plan.candidateBindingMask ==
                (expectedBindings & VisibilityCandidateBindingMask),
            context + " reports only selected candidate bindings");
        Require(plan.candidatePassMask ==
                (expectedPasses & VisibilityCandidatePassMask),
            context + " reports only selected candidate passes");
        Require(plan.dispatchCount ==
                ExpectedDispatchCount(workload, expectedPasses),
            context + " counts repeated and contribution-control dispatches");
        Require(plan.firstTraceSrvCount ==
                ExpectedFirstTraceSrvCount(workload) &&
                plan.firstTraceUavCount ==
                    ExpectedFirstTraceUavCount(configuration, workload),
            context + " reports the exact first-trace descriptor layout");
        Require(plan.peakSrvCount == expectedPeak[0] &&
                plan.peakUavCount == expectedPeak[1],
            context + " reports the exact peak descriptor counts");
        Require(plan.preservesProductionBitmask,
            context + " retains the production bitmask implementation");
        Require(plan.laterBounceRuntimeSamples ==
                VisibilityRuntimeSampleContract::Guarded,
            context + " keeps later-bounce counts on the guarded Runtime path");
        Require(plan.shaderPermutationKey != 0u &&
                plan.permutationKey != 0u &&
                plan.historyResetKey != 0u &&
                !plan.permutationName.empty(),
            context + " has complete pipeline, evidence, and history identity");
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
        const uint8_t depthOnlyNormalBoundary =
            ComputePackedEdgesReference(
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
            "Reduced-resolution receiver mapping is centered and clamped");

        const uint8_t fromRight = 3u;
        const uint8_t toLeft = 1u;
        Require(std::min(fromRight, toLeft) == 1u,
            "Symmetric enforcement uses the weaker opposing edge lane");
    }

    void RequireIncomplete(
        const VisibilityPerformanceProfileConfiguration& configuration,
        const std::string& field)
    {
        Require(!IsVisibilityPerformanceProfileFullyAssigned(configuration),
            "Unset " + field + " fails the full-assignment audit");
        const VisibilityExecutionPlan plan =
            ResolveVisibilityExecutionPlan(configuration, {});
        Require(!plan.valid &&
                plan.error == VisibilityPlanError::IncompleteProfile,
            "Unset " + field + " fails before plan construction");
    }

    void TestEveryProfileAndFullAssignmentValidation()
    {
        constexpr std::array<VisibilityPerformanceProfile, 8> profiles = {
            VisibilityPerformanceProfile::Reference,
            VisibilityPerformanceProfile::Runtime,
            VisibilityPerformanceProfile::ExactFusedResolveApply,
            VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2,
            VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesDepthNormal2x2,
            VisibilityPerformanceProfile::AlgorithmicPackedEdgesSlope2x2,
            VisibilityPerformanceProfile::AlgorithmicPackedEdgesLeakage2x2,
            VisibilityPerformanceProfile::AlgorithmicFusedPackedEdges2x2
        };

        std::set<uint64_t> shaderKeys;
        std::set<uint64_t> permutationKeys;
        std::set<uint64_t> historyKeys;
        for (VisibilityPerformanceProfile profile : profiles)
        {
            const VisibilityPerformanceProfileConfiguration configuration =
                GetVisibilityPerformanceProfileConfiguration(profile);
            Require(configuration.profile == profile &&
                    !configuration.name.empty(),
                "Every current profile maps to its own named configuration");
            Require(configuration.assignmentMask ==
                    VisibilityProfileAllAssignments &&
                    IsVisibilityPerformanceProfileFullyAssigned(configuration),
                "Every current profile assigns the complete contract");
            Require(configuration.implementationStatus ==
                    VisibilityImplementationStatus::Implemented &&
                    configuration.trace ==
                        VisibilityTraceImplementation::RuntimeBitmask &&
                    configuration.firstBounceSamples ==
                        VisibilitySampleSpecialization::Runtime &&
                    configuration.laterBounceSamples ==
                        VisibilitySampleSpecialization::Runtime &&
                    configuration.bindings ==
                        VisibilityBindingStrategy::MinimalConditional,
                "Every current profile uses the implemented Runtime path");

            const VisibilityPerformanceWorkload workload;
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(configuration, workload);
            RequireExactPlan(plan, configuration, workload,
                std::string(configuration.name));
            Require(shaderKeys.insert(plan.shaderPermutationKey).second &&
                    permutationKeys.insert(plan.permutationKey).second &&
                    historyKeys.insert(plan.historyResetKey).second,
                "Every current profile has a distinct identity");
        }

        const auto reference = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Reference);
        Require(reference.optimizationClass ==
                    VisibilityOptimizationClass::Reference &&
                reference.application ==
                    VisibilityApplicationMode::LegacySeparateComposition &&
                reference.edgeStorage == VisibilityEdgeStorage::None,
            "Reference remains the named comparison identity on Runtime");

        const auto runtime = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        Require(runtime.optimizationClass ==
                    VisibilityOptimizationClass::Exact &&
                runtime.consumerRequirement ==
                    VisibilityConsumerRequirement::Any &&
                runtime.estimatorRequirement ==
                    VisibilityEstimatorRequirement::Any &&
                runtime.resolutionRequirement ==
                    VisibilityResolutionRequirement::Any,
            "Runtime accepts every retained product workload");

        const auto exactFused = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::ExactFusedResolveApply);
        Require(exactFused.application ==
                    VisibilityApplicationMode::FusedResolveAndApplyExact &&
                exactFused.consumerRequirement ==
                    VisibilityConsumerRequirement::AmbientOcclusionOnly &&
                exactFused.resolutionRequirement ==
                    VisibilityResolutionRequirement::Reduced &&
                exactFused.explicitHalfRoundtrip,
            "Exact fusion records all restrictions and the R16F roundtrip");

        const auto packed = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2);
        Require(IsPacked(packed) &&
                packed.resolutionRequirement ==
                    VisibilityResolutionRequirement::Reduced &&
                !packed.explicitHalfRoundtrip,
            "Packed reconstruction retains separate R8_UINT edge metadata");

        const auto packedFused =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::
                    AlgorithmicFusedPackedEdges2x2);
        Require(IsPacked(packedFused) &&
                packedFused.application ==
                    VisibilityApplicationMode::
                        FusedResolveAndApplyPackedEdges &&
                packedFused.consumerRequirement ==
                    VisibilityConsumerRequirement::AmbientOcclusionOnly,
            "Packed fusion retains its AO-only edge-guided contract");

        Require(!IsVisibilityPerformanceProfileFullyAssigned(
                GetVisibilityPerformanceProfileConfiguration(
                    VisibilityPerformanceProfile::Unset)) &&
                !IsVisibilityPerformanceProfileFullyAssigned(
                    GetVisibilityPerformanceProfileConfiguration(
                        VisibilityPerformanceProfile::Count)),
            "Profile sentinels cannot masquerade as curated profiles");

        VisibilityPerformanceProfileConfiguration changed = runtime;
        changed.profile = VisibilityPerformanceProfile::Unset;
        RequireIncomplete(changed, "profile");
        changed = runtime;
        changed.name = {};
        RequireIncomplete(changed, "name");
        changed = runtime;
        changed.optimizationClass = VisibilityOptimizationClass::Unset;
        RequireIncomplete(changed, "optimization class");
        changed = runtime;
        changed.trace = VisibilityTraceImplementation::Unset;
        RequireIncomplete(changed, "trace implementation");
        changed = runtime;
        changed.firstBounceSamples = VisibilitySampleSpecialization::Unset;
        RequireIncomplete(changed, "first-bounce sample contract");
        changed = runtime;
        changed.laterBounceSamples = VisibilitySampleSpecialization::Unset;
        RequireIncomplete(changed, "later-bounce sample contract");
        changed = runtime;
        changed.math = VisibilityMathMode::Unset;
        RequireIncomplete(changed, "math mode");
        changed = runtime;
        changed.rawAoStorage = VisibilityRawAoStorage::Unset;
        RequireIncomplete(changed, "AO storage");
        changed = runtime;
        changed.edgeStorage = VisibilityEdgeStorage::Unset;
        RequireIncomplete(changed, "edge storage");
        changed = runtime;
        changed.reconstruction = VisibilityReconstructionMode::Unset;
        RequireIncomplete(changed, "reconstruction");
        changed = runtime;
        changed.temporal = VisibilityTemporalMode::Unset;
        RequireIncomplete(changed, "temporal mode");
        changed = runtime;
        changed.application = VisibilityApplicationMode::Unset;
        RequireIncomplete(changed, "application");
        changed = runtime;
        changed.depth = VisibilityDepthMode::Unset;
        RequireIncomplete(changed, "depth mode");
        changed = runtime;
        changed.bindings = VisibilityBindingStrategy::Unset;
        RequireIncomplete(changed, "binding strategy");
        changed = runtime;
        changed.traversal = VisibilityTraversalOrder::Unset;
        RequireIncomplete(changed, "traversal");
        changed = runtime;
        changed.consumerRequirement = VisibilityConsumerRequirement::Unset;
        RequireIncomplete(changed, "consumer requirement");
        changed = runtime;
        changed.estimatorRequirement = VisibilityEstimatorRequirement::Unset;
        RequireIncomplete(changed, "estimator requirement");
        changed = runtime;
        changed.resolutionRequirement = VisibilityResolutionRequirement::Unset;
        RequireIncomplete(changed, "resolution requirement");
        changed = runtime;
        changed.implementationStatus = VisibilityImplementationStatus::Unset;
        RequireIncomplete(changed, "implementation status");
        changed = runtime;
        changed.implementationStatus =
            VisibilityImplementationStatus::PartialBenchmarkControl;
        changed.implementationNote = {};
        RequireIncomplete(changed, "non-implemented status note");
        changed = runtime;
        changed.assignmentMask = 0u;
        RequireIncomplete(changed, "assignment mask");
    }

    void TestRuntimeSampleContracts()
    {
        constexpr std::array<uint32_t, 6> counts = {
            1u, 2u, 20u, 21u, 63u, 64u
        };
        constexpr std::array<VisibilityPerformanceScheduler, 2> schedulers = {
            VisibilityPerformanceScheduler::IndependentHash,
            VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField
        };

        for (VisibilityPerformanceScheduler scheduler : schedulers)
        for (uint32_t count : counts)
        {
            VisibilityPerformanceWorkload workload;
            workload.consumer = VisibilityPerformanceConsumer::
                AmbientOcclusionAndIndirectDiffuse;
            workload.estimator =
                VisibilityPerformanceEstimator::UniformSolidAngle;
            workload.scheduler = scheduler;
            workload.firstBounceSampleCount = count;
            workload.laterBounceSampleCount = count;
            workload.bounceCount = 1u;
            workload.radialExponent = 2.0f;

            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, workload);
            const VisibilityRuntimeSampleContract expected =
                (count & 1u) == 0u
                    ? VisibilityRuntimeSampleContract::TrustedEven
                    : VisibilityRuntimeSampleContract::TrustedOdd;
            Require(plan.valid &&
                    plan.firstBounceRuntimeSamples == expected &&
                    plan.laterBounceRuntimeSamples ==
                        VisibilityRuntimeSampleContract::Guarded,
                "Runtime chooses only its compact first-bounce parity contract");

            workload.radialExponent = 3.0f;
            const VisibilityExecutionPlan dynamicExponent =
                ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, workload);
            workload.radialExponent = 2.0f;
            workload.depthHierarchyEnabled = true;
            const VisibilityExecutionPlan hierarchy =
                ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, workload);
            Require(dynamicExponent.valid && hierarchy.valid &&
                    dynamicExponent.firstBounceRuntimeSamples == expected &&
                    hierarchy.firstBounceRuntimeSamples == expected,
                "Runtime-uniform exponent and depth selection preserve parity");
        }

        for (uint32_t count : counts)
        {
            VisibilityPerformanceWorkload guarded;
            guarded.firstBounceSampleCount = count;
            guarded.laterBounceSampleCount = count;
            Require(ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, guarded).
                        firstBounceRuntimeSamples ==
                    VisibilityRuntimeSampleContract::Guarded,
                "AO-only Runtime uses the guarded count contract");

            guarded.consumer = VisibilityPerformanceConsumer::
                AmbientOcclusionAndIndirectDiffuse;
            guarded.estimator =
                VisibilityPerformanceEstimator::UniformProjectedAngle;
            Require(ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, guarded).
                        firstBounceRuntimeSamples ==
                    VisibilityRuntimeSampleContract::Guarded,
                "Non-solid-angle Runtime uses the guarded count contract");

            guarded.estimator =
                VisibilityPerformanceEstimator::UniformSolidAngle;
            guarded.bounceCount = 2u;
            Require(ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, guarded).
                        firstBounceRuntimeSamples ==
                    VisibilityRuntimeSampleContract::Guarded,
                "Multi-bounce Runtime uses the guarded count contract");

            VisibilityPerformanceWorkload edges;
            edges.firstBounceSampleCount = count;
            edges.laterBounceSampleCount = count;
            Require(ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::
                        AlgorithmicPackedEdges2x2,
                    edges).firstBounceRuntimeSamples ==
                    VisibilityRuntimeSampleContract::Guarded,
                "Packed-edge Runtime uses the guarded count contract");
        }
    }

    void TestRuntimePlansExhaustively()
    {
        constexpr std::array<VisibilityPerformanceProfile, 2> profiles = {
            VisibilityPerformanceProfile::Reference,
            VisibilityPerformanceProfile::Runtime
        };
        constexpr std::array<VisibilityPerformanceConsumer, 3> consumers = {
            VisibilityPerformanceConsumer::AmbientOcclusion,
            VisibilityPerformanceConsumer::IndirectDiffuse,
            VisibilityPerformanceConsumer::
                AmbientOcclusionAndIndirectDiffuse
        };
        constexpr std::array<VisibilityPerformanceEstimator, 3> estimators = {
            VisibilityPerformanceEstimator::UniformProjectedAngle,
            VisibilityPerformanceEstimator::UniformSolidAngle,
            VisibilityPerformanceEstimator::CosineWeightedSolidAngle
        };
        constexpr std::array<VisibilityPerformanceResolution, 3> resolutions = {
            VisibilityPerformanceResolution::Full,
            VisibilityPerformanceResolution::Half,
            VisibilityPerformanceResolution::Quarter
        };
        constexpr std::array<VisibilityPerformanceScheduler, 2> schedulers = {
            VisibilityPerformanceScheduler::IndependentHash,
            VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField
        };
        constexpr std::array<uint32_t, 6> bounceCounts = {
            1u, 2u, 3u, 8u, 9u, 16u
        };

        for (VisibilityPerformanceProfile profile : profiles)
        for (VisibilityPerformanceConsumer consumer : consumers)
        for (VisibilityPerformanceEstimator estimator : estimators)
        for (VisibilityPerformanceResolution resolution : resolutions)
        for (VisibilityPerformanceScheduler scheduler : schedulers)
        for (uint32_t bounceCount : bounceCounts)
        for (uint32_t flags = 0u; flags < 8u; ++flags)
        {
            VisibilityPerformanceWorkload workload;
            workload.consumer = consumer;
            workload.estimator = estimator;
            workload.resolution = resolution;
            workload.scheduler = scheduler;
            workload.bounceCount = bounceCount;
            workload.temporalEnabled = (flags & 1u) != 0u;
            workload.spatialEnabled = (flags & 2u) != 0u;
            workload.depthHierarchyEnabled = (flags & 4u) != 0u;

            const VisibilityPerformanceProfileConfiguration configuration =
                GetVisibilityPerformanceProfileConfiguration(profile);
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(configuration, workload);
            RequireExactPlan(plan, configuration, workload,
                profile == VisibilityPerformanceProfile::Reference
                    ? "Reference matrix case"
                    : "Runtime matrix case");
            Require(plan.selectsLegacyReference ==
                    (profile == VisibilityPerformanceProfile::Reference),
                "Only the named Reference profile selects reference identity");
            Require(plan.optionalResourceMask == 0u &&
                    plan.candidateBindingMask == 0u &&
                    plan.candidatePassMask == 0u,
                "Unpacked Runtime plans incur no packed or fused candidate cost");
            Require(HasVisibilityExecutionResource(plan.resourceMask,
                    VisibilityExecutionResource::ToroidalNoise) &&
                    HasVisibilityExecutionBinding(plan.bindingMask,
                        VisibilityExecutionBinding::ToroidalNoise),
                "Both schedulers share the one Runtime Toroidal layout");
        }
    }

    void TestAoGiMultiBounceAndCandidatePlans()
    {
        {
            const VisibilityPerformanceWorkload workload;
            const auto configuration =
                GetVisibilityPerformanceProfileConfiguration(
                    VisibilityPerformanceProfile::Runtime);
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(configuration, workload);
            RequireExactPlan(plan, configuration, workload, "Runtime AO");
            Require(plan.resourceMask ==
                    (ResourceBit(
                        VisibilityExecutionResource::RawAmbient) |
                     ResourceBit(
                        VisibilityExecutionResource::FinalAmbient) |
                     ResourceBit(
                        VisibilityExecutionResource::ToroidalNoise)) &&
                    plan.passMask ==
                    (PassBit(VisibilityExecutionPass::RuntimeTrace) |
                     PassBit(VisibilityExecutionPass::Reconstruction) |
                     PassBit(VisibilityExecutionPass::Composition)) &&
                    plan.firstTraceSrvCount == 4u &&
                    plan.firstTraceUavCount == 1u &&
                    plan.peakSrvCount == 12u &&
                    plan.peakUavCount == 2u &&
                    plan.dispatchCount == 3u,
                "Runtime AO retains only its required allocations and passes");
        }

        {
            VisibilityPerformanceWorkload workload;
            workload.consumer =
                VisibilityPerformanceConsumer::IndirectDiffuse;
            workload.resolution = VisibilityPerformanceResolution::Full;
            const auto configuration =
                GetVisibilityPerformanceProfileConfiguration(
                    VisibilityPerformanceProfile::Runtime);
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(configuration, workload);
            RequireExactPlan(plan, configuration, workload, "Runtime GI");
            Require(!HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::RawAmbient) &&
                    HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::RawIndirect) &&
                    !HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::FinalIndirect) &&
                    plan.firstTraceSrvCount == 5u &&
                    plan.firstTraceUavCount == 1u &&
                    plan.dispatchCount == 2u,
                "Full-resolution single-bounce GI avoids AO and reconstruction");
        }

        {
            VisibilityPerformanceWorkload workload;
            workload.consumer = VisibilityPerformanceConsumer::
                AmbientOcclusionAndIndirectDiffuse;
            workload.resolution = VisibilityPerformanceResolution::Quarter;
            workload.bounceCount = 3u;
            workload.temporalEnabled = true;
            workload.spatialEnabled = true;
            workload.depthHierarchyEnabled = true;
            const auto configuration =
                GetVisibilityPerformanceProfileConfiguration(
                    VisibilityPerformanceProfile::Runtime);
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(configuration, workload);
            RequireExactPlan(plan, configuration, workload,
                "Runtime AO+GI multi-bounce");
            Require(HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::RawAmbient) &&
                    HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::RawIndirect) &&
                    HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::CumulativeIndirect) &&
                    HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::TemporalAmbient) &&
                    HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::TemporalIndirect) &&
                    HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::DepthHierarchy) &&
                    HasVisibilityExecutionPass(plan.passMask,
                        VisibilityExecutionPass::RuntimeLaterBounceTrace) &&
                    plan.firstTraceSrvCount == 5u &&
                    plan.firstTraceUavCount == 2u &&
                    plan.peakSrvCount == 12u &&
                    plan.peakUavCount == 5u &&
                    plan.dispatchCount == 7u,
                "AO+GI owns every temporal, hierarchy, and later-bounce cost");

            workload.bounceCount = 9u;
            workload.temporalEnabled = false;
            workload.spatialEnabled = false;
            workload.depthHierarchyEnabled = false;
            const VisibilityExecutionPlan contributionTerminated =
                ResolveVisibilityExecutionPlan(configuration, workload);
            RequireExactPlan(contributionTerminated, configuration, workload,
                "Runtime contribution-terminated AO+GI");
            Require(contributionTerminated.dispatchCount == 19u &&
                    contributionTerminated.peakUavCount == 3u,
                "Bounce nine includes every indirect and GPU-control dispatch");
        }

        {
            const auto configuration =
                GetVisibilityPerformanceProfileConfiguration(
                    VisibilityPerformanceProfile::ExactFusedResolveApply);
            const VisibilityPerformanceWorkload workload;
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(configuration, workload);
            RequireExactPlan(plan, configuration, workload,
                "Exact Runtime fusion");
            Require(plan.requiresExplicitHalfRoundtrip &&
                    !HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::FinalAmbient) &&
                    plan.passMask ==
                    (PassBit(VisibilityExecutionPass::RuntimeTrace) |
                     PassBit(
                        VisibilityExecutionPass::FusedResolveAndApply)) &&
                    plan.peakSrvCount == 11u &&
                    plan.peakUavCount == 1u &&
                    plan.dispatchCount == 2u,
                "Exact fusion removes only the final AO allocation and dispatch");
        }

        {
            const auto configuration =
                GetVisibilityPerformanceProfileConfiguration(
                    VisibilityPerformanceProfile::
                        AlgorithmicPackedEdgesDepthNormal2x2);
            VisibilityPerformanceWorkload workload;
            workload.consumer = VisibilityPerformanceConsumer::
                AmbientOcclusionAndIndirectDiffuse;
            workload.bounceCount = 2u;
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(configuration, workload);
            RequireExactPlan(plan, configuration, workload,
                "Runtime packed-edge AO+GI");
            Require(HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::PackedEdgesR8Uint) &&
                    HasVisibilityExecutionBinding(plan.bindingMask,
                        VisibilityExecutionBinding::PackedEdges) &&
                    HasVisibilityExecutionPass(plan.passMask,
                        VisibilityExecutionPass::RuntimeTrace) &&
                    HasVisibilityExecutionPass(plan.passMask,
                        VisibilityExecutionPass::RuntimeLaterBounceTrace) &&
                    HasVisibilityExecutionPass(plan.passMask,
                        VisibilityExecutionPass::Reconstruction) &&
                    plan.optionalResourceMask == ResourceBit(
                        VisibilityExecutionResource::PackedEdgesR8Uint) &&
                    plan.candidateBindingMask == BindingBit(
                        VisibilityExecutionBinding::PackedEdges) &&
                    plan.firstTraceSrvCount == 5u &&
                    plan.firstTraceUavCount == 3u,
                "Packed edges add only their R8_UINT output and consumer");
        }

        {
            const auto configuration =
                GetVisibilityPerformanceProfileConfiguration(
                    VisibilityPerformanceProfile::
                        AlgorithmicFusedPackedEdges2x2);
            const VisibilityPerformanceWorkload workload;
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(configuration, workload);
            RequireExactPlan(plan, configuration, workload,
                "Fused Runtime packed-edge AO");
            Require(!plan.requiresExplicitHalfRoundtrip &&
                    !HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::FinalAmbient) &&
                    HasVisibilityExecutionResource(plan.resourceMask,
                        VisibilityExecutionResource::PackedEdgesR8Uint) &&
                    plan.passMask ==
                    (PassBit(VisibilityExecutionPass::RuntimeTrace) |
                     PassBit(
                        VisibilityExecutionPass::FusedResolveAndApply)) &&
                    plan.candidatePassMask == PassBit(
                        VisibilityExecutionPass::FusedResolveAndApply) &&
                    plan.firstTraceUavCount == 2u &&
                    plan.peakSrvCount == 11u &&
                    plan.peakUavCount == 2u &&
                    plan.dispatchCount == 2u,
                "Packed fusion preserves edge metadata without a final AO texture");
        }
    }

    void TestPermutationAndHistoryKeys()
    {
        VisibilityPerformanceWorkload parityWorkload;
        parityWorkload.consumer = VisibilityPerformanceConsumer::
            AmbientOcclusionAndIndirectDiffuse;
        parityWorkload.estimator =
            VisibilityPerformanceEstimator::UniformSolidAngle;
        parityWorkload.firstBounceSampleCount = 20u;
        parityWorkload.laterBounceSampleCount = 20u;

        const VisibilityExecutionPlan base = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::Runtime, parityWorkload);
        const VisibilityExecutionPlan repeated =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Runtime, parityWorkload);
        Require(base.valid && repeated.valid &&
                base.shaderPermutationKey == repeated.shaderPermutationKey &&
                base.permutationKey == repeated.permutationKey &&
                base.historyResetKey == repeated.historyResetKey &&
                base.permutationName == repeated.permutationName,
            "Identical Runtime settings produce stable plan identities");

        constexpr std::array<uint32_t, 3> evenCounts = { 2u, 20u, 64u };
        constexpr std::array<uint32_t, 3> oddCounts = { 1u, 21u, 63u };
        std::set<uint64_t> evenShaderKeys;
        std::set<uint64_t> oddShaderKeys;
        std::set<uint64_t> evidenceKeys;
        std::set<uint64_t> historyKeys;
        for (uint32_t count : evenCounts)
        {
            parityWorkload.firstBounceSampleCount = count;
            parityWorkload.laterBounceSampleCount = count;
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, parityWorkload);
            Require(plan.valid &&
                    plan.firstBounceRuntimeSamples ==
                        VisibilityRuntimeSampleContract::TrustedEven,
                "Every retained even count selects TrustedEven");
            evenShaderKeys.insert(plan.shaderPermutationKey);
            evidenceKeys.insert(plan.permutationKey);
            historyKeys.insert(plan.historyResetKey);
        }
        for (uint32_t count : oddCounts)
        {
            parityWorkload.firstBounceSampleCount = count;
            parityWorkload.laterBounceSampleCount = count;
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, parityWorkload);
            Require(plan.valid &&
                    plan.firstBounceRuntimeSamples ==
                        VisibilityRuntimeSampleContract::TrustedOdd,
                "Every retained odd count selects TrustedOdd");
            oddShaderKeys.insert(plan.shaderPermutationKey);
            evidenceKeys.insert(plan.permutationKey);
            historyKeys.insert(plan.historyResetKey);
        }
        Require(evenShaderKeys.size() == 1u &&
                oddShaderKeys.size() == 1u &&
                *evenShaderKeys.begin() != *oddShaderKeys.begin() &&
                evidenceKeys.size() == 6u &&
                historyKeys.size() == 6u,
            "Counts 1-64 share two parity shaders but retain numeric identity");

        parityWorkload.firstBounceSampleCount = 20u;
        parityWorkload.laterBounceSampleCount = 20u;
        const VisibilityExecutionPlan toroidal =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Runtime, parityWorkload);
        parityWorkload.scheduler =
            VisibilityPerformanceScheduler::IndependentHash;
        const VisibilityExecutionPlan independent =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Runtime, parityWorkload);
        Require(independent.valid &&
                independent.shaderPermutationKey ==
                    toroidal.shaderPermutationKey &&
                independent.permutationKey != toroidal.permutationKey &&
                independent.historyResetKey != toroidal.historyResetKey,
            "Runtime scheduler selection reuses one shader but resets evidence and history");

        parityWorkload.scheduler =
            VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField;
        const VisibilityExecutionPlan continuousBase =
            ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Runtime, parityWorkload);
        std::vector<VisibilityPerformanceWorkload> continuousVariants;
        auto changed = parityWorkload;
        changed.outputWidth = 1600u;
        continuousVariants.push_back(changed);
        changed = parityWorkload;
        changed.outputHeight = 900u;
        continuousVariants.push_back(changed);
        changed = parityWorkload;
        changed.radius = 4.0f;
        continuousVariants.push_back(changed);
        changed = parityWorkload;
        changed.thickness = 0.75f;
        continuousVariants.push_back(changed);
        changed = parityWorkload;
        changed.radialExponent = 3.0f;
        continuousVariants.push_back(changed);
        changed = parityWorkload;
        changed.laterBounceSampleCount = 63u;
        continuousVariants.push_back(changed);
        changed = parityWorkload;
        changed.depthHierarchyEnabled = true;
        continuousVariants.push_back(changed);

        for (const VisibilityPerformanceWorkload& variant :
            continuousVariants)
        {
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, variant);
            Require(plan.valid &&
                    plan.shaderPermutationKey ==
                        continuousBase.shaderPermutationKey &&
                    plan.firstBounceRuntimeSamples ==
                        continuousBase.firstBounceRuntimeSamples &&
                    plan.permutationKey != continuousBase.permutationKey &&
                    plan.historyResetKey != continuousBase.historyResetKey,
                "Runtime-uniform values reuse a shader but retain evidence identity");
        }

        std::vector<VisibilityPerformanceWorkload> topologyVariants;
        changed = parityWorkload;
        changed.consumer = VisibilityPerformanceConsumer::AmbientOcclusion;
        topologyVariants.push_back(changed);
        changed = parityWorkload;
        changed.estimator =
            VisibilityPerformanceEstimator::UniformProjectedAngle;
        topologyVariants.push_back(changed);
        changed = parityWorkload;
        changed.resolution = VisibilityPerformanceResolution::Full;
        topologyVariants.push_back(changed);
        changed = parityWorkload;
        changed.bounceCount = 2u;
        topologyVariants.push_back(changed);
        changed = parityWorkload;
        changed.temporalEnabled = true;
        topologyVariants.push_back(changed);
        changed = parityWorkload;
        changed.spatialEnabled = true;
        topologyVariants.push_back(changed);
        changed = parityWorkload;
        changed.runtimeConfigurationKey = 1u;
        topologyVariants.push_back(changed);

        for (const VisibilityPerformanceWorkload& variant : topologyVariants)
        {
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, variant);
            Require(plan.valid &&
                    plan.shaderPermutationKey !=
                        continuousBase.shaderPermutationKey,
                "A shader topology change produces a distinct pipeline key");
        }

        const VisibilityExecutionPlan reference = ResolveVisibilityExecutionPlan(
            VisibilityPerformanceProfile::Reference, parityWorkload);
        Require(reference.valid &&
                reference.shaderPermutationKey !=
                    continuousBase.shaderPermutationKey &&
                reference.permutationKey !=
                    continuousBase.permutationKey &&
                reference.historyResetKey !=
                    continuousBase.historyResetKey,
            "A profile identity change invalidates pipeline and history identity");

        Require(base.permutationName.find("output=1920x1080") !=
                    std::string::npos &&
                base.permutationName.find("group=8x8") != std::string::npos &&
                base.permutationName.find("/trace=") != std::string::npos &&
                base.permutationName.find("/first-specialization=") !=
                    std::string::npos &&
                base.permutationName.find("/scheduler=") !=
                    std::string::npos &&
                base.permutationName.find("/runtime-config=") !=
                    std::string::npos,
            "The reported identity names implementation and workload settings");
    }

    void TestInvalidWorkloads()
    {
        const VisibilityPerformanceWorkload valid;
        std::vector<VisibilityPerformanceWorkload> invalidWorkloads;
        auto invalid = valid;
        invalid.consumer =
            static_cast<VisibilityPerformanceConsumer>(255u);
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.estimator =
            static_cast<VisibilityPerformanceEstimator>(255u);
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.resolution =
            static_cast<VisibilityPerformanceResolution>(255u);
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.scheduler =
            static_cast<VisibilityPerformanceScheduler>(255u);
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
        invalid.bounceCount = 17u;
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
        invalid.radius = -1.0f;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.radius = std::numeric_limits<float>::infinity();
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.radius = std::numeric_limits<float>::quiet_NaN();
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.thickness = -0.1f;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.thickness = std::numeric_limits<float>::infinity();
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.thickness = std::numeric_limits<float>::quiet_NaN();
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.radialExponent = 0.0f;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.radialExponent = std::numeric_limits<float>::infinity();
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
        invalid.threadGroupSizeX = 1025u;
        invalidWorkloads.push_back(invalid);
        invalid = valid;
        invalid.threadGroupSizeX = 1024u;
        invalid.threadGroupSizeY = 2u;
        invalidWorkloads.push_back(invalid);

        for (const VisibilityPerformanceWorkload& workload : invalidWorkloads)
        {
            const VisibilityExecutionPlan plan =
                ResolveVisibilityExecutionPlan(
                    VisibilityPerformanceProfile::Runtime, workload);
            Require(!plan.valid &&
                    plan.error == VisibilityPlanError::InvalidWorkload &&
                    !plan.errorMessage.empty(),
                "Every malformed Runtime workload fails before plan construction");
        }
    }

    void TestInvalidProfileAndCandidateSafeguards()
    {
        auto changedReference =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::Reference);
        changedReference.name = "Changed Reference";
        Require(ResolveVisibilityExecutionPlan(changedReference, {}).error ==
                VisibilityPlanError::ReferenceContractViolation,
            "Reference cannot silently change its named contract");
        changedReference =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::Reference);
        changedReference.benchmarkOnly = true;
        Require(ResolveVisibilityExecutionPlan(changedReference, {}).error ==
                VisibilityPlanError::ReferenceContractViolation,
            "Reference cannot silently become a benchmark-only profile");
        changedReference =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::Reference);
        changedReference.implementationStatus =
            VisibilityImplementationStatus::Unavailable;
        changedReference.implementationNote = "Mutated reference";
        Require(ResolveVisibilityExecutionPlan(changedReference, {}).error ==
                VisibilityPlanError::ReferenceContractViolation,
            "Reference validation precedes implementation availability");

        auto unavailable = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        unavailable.implementationStatus =
            VisibilityImplementationStatus::Unavailable;
        unavailable.implementationNote = "Unavailable Runtime fixture";
        const VisibilityExecutionPlan unavailablePlan =
            ResolveVisibilityExecutionPlan(unavailable, {});
        Require(unavailablePlan.error ==
                    VisibilityPlanError::ProfileImplementationUnavailable &&
                unavailablePlan.errorMessage == unavailable.implementationNote,
            "Unavailable profiles fail with their authored reason");

        auto runtime = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        runtime.consumerRequirement =
            VisibilityConsumerRequirement::AmbientOcclusionOnly;
        VisibilityPerformanceWorkload gi;
        gi.consumer =
            VisibilityPerformanceConsumer::IndirectDiffuse;
        Require(ResolveVisibilityExecutionPlan(runtime, gi).error ==
                VisibilityPlanError::ProfileConsumerMismatch,
            "AO-only profiles reject GI");

        runtime = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        runtime.consumerRequirement =
            VisibilityConsumerRequirement::IncludesAmbientOcclusion;
        Require(ResolveVisibilityExecutionPlan(runtime, gi).error ==
                VisibilityPlanError::ProfileConsumerMismatch,
            "AO-present profiles reject GI-only workloads");

        runtime = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        runtime.consumerRequirement =
            VisibilityConsumerRequirement::IncludesIndirectDiffuse;
        Require(ResolveVisibilityExecutionPlan(runtime, {}).error ==
                VisibilityPlanError::
                    LaterBounceSpecializationRequiresIndirectDiffuse,
            "Later-bounce requirements reject single-bounce AO");
        gi.bounceCount = 1u;
        Require(ResolveVisibilityExecutionPlan(runtime, gi).error ==
                VisibilityPlanError::
                    LaterBounceSpecializationRequiresIndirectDiffuse,
            "Later-bounce requirements reject single-bounce GI");

        runtime = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        runtime.estimatorRequirement =
            VisibilityEstimatorRequirement::UniformProjectedAngle;
        Require(ResolveVisibilityExecutionPlan(runtime, {}).error ==
                VisibilityPlanError::ProfileEstimatorMismatch,
            "Estimator-specialized profiles reject another estimator");

        runtime = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        runtime.resolutionRequirement =
            VisibilityResolutionRequirement::Full;
        Require(ResolveVisibilityExecutionPlan(runtime, {}).error ==
                VisibilityPlanError::ProfileResolutionMismatch,
            "Full-resolution profiles reject reduced workloads");

        VisibilityPerformanceWorkload wrongGroup;
        wrongGroup.threadGroupSizeX = 16u;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::Runtime, wrongGroup).error ==
                VisibilityPlanError::ProfileThreadGroupMismatch,
            "Runtime rejects removed thread-group shapes");

        runtime = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        runtime.edgeStorage = VisibilityEdgeStorage::R8Uint;
        Require(ResolveVisibilityExecutionPlan(runtime, {}).error ==
                VisibilityPlanError::InvalidPackedReconstruction,
            "An edge allocation requires packed reconstruction");
        runtime = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        runtime.reconstruction =
            VisibilityReconstructionMode::PackedEdges2x2;
        Require(ResolveVisibilityExecutionPlan(runtime, {}).error ==
                VisibilityPlanError::InvalidPackedReconstruction,
            "Packed reconstruction requires an R8_UINT edge allocation");

        VisibilityPerformanceWorkload fullResolution;
        fullResolution.resolution = VisibilityPerformanceResolution::Full;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::
                    AlgorithmicPackedEdges2x2,
                fullResolution).error ==
                VisibilityPlanError::ProfileResolutionMismatch,
            "Packed reconstruction rejects full-resolution workloads");
        VisibilityPerformanceWorkload packedSpatial;
        packedSpatial.spatialEnabled = true;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::
                    AlgorithmicPackedEdges2x2,
                packedSpatial).error ==
                VisibilityPlanError::
                    PackedReconstructionDoesNotSupportSpatialFilter,
            "Packed reconstruction rejects the legacy spatial filter");

        VisibilityPerformanceWorkload fusedGi;
        fusedGi.consumer = VisibilityPerformanceConsumer::
            AmbientOcclusionAndIndirectDiffuse;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactFusedResolveApply,
                fusedGi).error ==
                VisibilityPlanError::ProfileConsumerMismatch,
            "Exact fusion cannot reorder GI composition");
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactFusedResolveApply,
                fullResolution).error ==
                VisibilityPlanError::ProfileResolutionMismatch,
            "Exact fusion requires reduced resolution");
        VisibilityPerformanceWorkload fusedSpatial;
        fusedSpatial.spatialEnabled = true;
        Require(ResolveVisibilityExecutionPlan(
                VisibilityPerformanceProfile::ExactFusedResolveApply,
                fusedSpatial).error ==
                VisibilityPlanError::
                    FusedApplicationDoesNotSupportSpatialFilter,
            "Exact fusion cannot silently discard spatial filtering");

        auto inexactFusion = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::ExactFusedResolveApply);
        inexactFusion.explicitHalfRoundtrip = false;
        Require(ResolveVisibilityExecutionPlan(inexactFusion, {}).error ==
                VisibilityPlanError::FusedApplicationRequiresHalfRoundtrip,
            "Exact fusion cannot omit the eliminated R16F roundtrip");
        inexactFusion =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::ExactFusedResolveApply);
        inexactFusion.edgeStorage = VisibilityEdgeStorage::R8Uint;
        inexactFusion.reconstruction =
            VisibilityReconstructionMode::PackedEdges2x2;
        Require(ResolveVisibilityExecutionPlan(inexactFusion, {}).error ==
                VisibilityPlanError::FusedApplicationRequiresHalfRoundtrip,
            "Exact fusion cannot substitute packed reconstruction");

        auto strayRoundtrip = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        strayRoundtrip.explicitHalfRoundtrip = true;
        Require(ResolveVisibilityExecutionPlan(strayRoundtrip, {}).error ==
                VisibilityPlanError::FusedApplicationRequiresHalfRoundtrip,
            "Only exact fusion may request an explicit half roundtrip");

        auto missingPackedFusion =
            GetVisibilityPerformanceProfileConfiguration(
                VisibilityPerformanceProfile::Runtime);
        missingPackedFusion.application =
            VisibilityApplicationMode::FusedResolveAndApplyPackedEdges;
        missingPackedFusion.consumerRequirement =
            VisibilityConsumerRequirement::AmbientOcclusionOnly;
        missingPackedFusion.resolutionRequirement =
            VisibilityResolutionRequirement::Reduced;
        Require(ResolveVisibilityExecutionPlan(missingPackedFusion, {}).error ==
                VisibilityPlanError::FusedApplicationRequiresPackedEdges,
            "Packed fusion requires packed edge production");

        auto packedFusion = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::
                AlgorithmicFusedPackedEdges2x2);
        packedFusion.explicitHalfRoundtrip = true;
        Require(ResolveVisibilityExecutionPlan(packedFusion, {}).error ==
                VisibilityPlanError::FusedApplicationRequiresPackedEdges,
            "Packed fusion cannot add the exact-fusion half roundtrip");

        auto reorderedGi = GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile::Runtime);
        reorderedGi.traversal = VisibilityTraversalOrder::GroupedBySide;
        gi.bounceCount = 2u;
        Require(ResolveVisibilityExecutionPlan(reorderedGi, gi).error ==
                VisibilityPlanError::IndirectDiffuseTraversalReordered,
            "GI cannot change near-to-far source ownership");
    }

    void TestVerificationProfilesAndReasons()
    {
        struct ExpectedVerification
        {
            VisibilityVerificationProfile profile;
            VisibilityPerformanceProfile implementation;
            VisibilityPerformanceConsumer consumer;
            uint32_t samples;
            uint32_t bounces;
        };
        constexpr std::array<ExpectedVerification, 8> expected = {{
            {
                VisibilityVerificationProfile::ReferenceAo8T,
                VisibilityPerformanceProfile::Reference,
                VisibilityPerformanceConsumer::AmbientOcclusion,
                8u,
                1u
            },
            {
                VisibilityVerificationProfile::RuntimeAo8T,
                VisibilityPerformanceProfile::Runtime,
                VisibilityPerformanceConsumer::AmbientOcclusion,
                8u,
                1u
            },
            {
                VisibilityVerificationProfile::PackedEdgeAo8T,
                VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2,
                VisibilityPerformanceConsumer::AmbientOcclusion,
                8u,
                1u
            },
            {
                VisibilityVerificationProfile::ReferenceAoGi8T,
                VisibilityPerformanceProfile::Reference,
                VisibilityPerformanceConsumer::
                    AmbientOcclusionAndIndirectDiffuse,
                8u,
                1u
            },
            {
                VisibilityVerificationProfile::RuntimeAoGi8T,
                VisibilityPerformanceProfile::Runtime,
                VisibilityPerformanceConsumer::
                    AmbientOcclusionAndIndirectDiffuse,
                8u,
                1u
            },
            {
                VisibilityVerificationProfile::RuntimeAoGi12T,
                VisibilityPerformanceProfile::Runtime,
                VisibilityPerformanceConsumer::
                    AmbientOcclusionAndIndirectDiffuse,
                12u,
                1u
            },
            {
                VisibilityVerificationProfile::RuntimeAoGi16T,
                VisibilityPerformanceProfile::Runtime,
                VisibilityPerformanceConsumer::
                    AmbientOcclusionAndIndirectDiffuse,
                16u,
                1u
            },
            {
                VisibilityVerificationProfile::RuntimeMultiBounce,
                VisibilityPerformanceProfile::Runtime,
                VisibilityPerformanceConsumer::
                    AmbientOcclusionAndIndirectDiffuse,
                8u,
                2u
            }
        }};

        std::set<uint64_t> keys;
        for (const ExpectedVerification& item : expected)
        {
            const VisibilityVerificationProfileDefinition definition =
                GetVisibilityVerificationProfileDefinition(item.profile);
            Require(definition.profile == item.profile &&
                    !definition.name.empty() &&
                    definition.implementationProfile ==
                        item.implementation &&
                    definition.implementationStatus ==
                        VisibilityImplementationStatus::Implemented,
                "Every retained verification profile names its implementation");
            const VisibilityPerformanceWorkload& workload =
                definition.expectedWorkload;
            Require(workload.consumer == item.consumer &&
                    workload.estimator ==
                        VisibilityPerformanceEstimator::UniformSolidAngle &&
                    workload.resolution ==
                        VisibilityPerformanceResolution::Half &&
                    workload.scheduler ==
                        VisibilityPerformanceScheduler::
                            ToroidalBlueNoiseRankField &&
                    workload.firstBounceSampleCount == item.samples &&
                    workload.laterBounceSampleCount == item.samples &&
                    workload.bounceCount == item.bounces &&
                    workload.outputWidth == 1920u &&
                    workload.outputHeight == 1080u &&
                    workload.radius == 3.0f &&
                    workload.thickness == 0.5f &&
                    workload.radialExponent == 2.0f &&
                    workload.threadGroupSizeX == 8u &&
                    workload.threadGroupSizeY == 8u &&
                    !workload.temporalEnabled &&
                    !workload.spatialEnabled &&
                    !workload.depthHierarchyEnabled,
                "Every verification profile fully assigns its target workload");

            const VisibilityVerificationProfileResolution resolution =
                ResolveVisibilityVerificationProfile(
                    item.profile, workload);
            Require(resolution.valid &&
                    resolution.executionPlan.valid &&
                    !resolution.reason.empty() &&
                    keys.insert(
                        resolution.executionPlan.permutationKey).second,
                "Every retained verification profile resolves distinctly");
            Require(resolution.executionPlan.selectsLegacyReference ==
                    (item.implementation ==
                        VisibilityPerformanceProfile::Reference),
                "Verification preserves the requested profile identity");
            if (item.profile ==
                VisibilityVerificationProfile::PackedEdgeAo8T)
            {
                Require(HasVisibilityExecutionResource(
                        resolution.executionPlan.resourceMask,
                        VisibilityExecutionResource::PackedEdgesR8Uint),
                    "Packed-edge verification exercises its retained metadata");
            }
        }

        const VisibilityVerificationProfileDefinition reference =
            GetVisibilityVerificationProfileDefinition(
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
        changed.threadGroupSizeY = 4u;
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
            const VisibilityVerificationProfileResolution resolution =
                ResolveVisibilityVerificationProfile(
                    VisibilityVerificationProfile::ReferenceAo8T,
                    mismatch);
            Require(!resolution.valid && !resolution.reason.empty(),
                "Every changed verification field clears profile validity");
        }

        const auto wrongImplementation =
            ResolveVisibilityVerificationProfile(
                VisibilityVerificationProfile::ReferenceAo8T,
                VisibilityPerformanceProfile::Runtime,
                reference.expectedWorkload);
        Require(!wrongImplementation.valid &&
                wrongImplementation.reason.find(
                    "Implementation profile") == 0u,
            "A stale implementation profile invalidates verification");

        Require(GetVisibilityVerificationProfileDefinition(
                    VisibilityVerificationProfile::Unset).profile ==
                    VisibilityVerificationProfile::Unset &&
                GetVisibilityVerificationProfileDefinition(
                    VisibilityVerificationProfile::Count).profile ==
                    VisibilityVerificationProfile::Unset &&
                !ResolveVisibilityVerificationProfile(
                    VisibilityVerificationProfile::Unset, {}).valid &&
                !ResolveVisibilityVerificationProfile(
                    VisibilityVerificationProfile::Count, {}).valid,
            "Verification sentinels cannot masquerade as profiles");
    }
}

int main()
{
    TestCountAndEdgePackingExhaustively();
    TestPackedEdgeGenerationReferenceCases();
    TestEveryProfileAndFullAssignmentValidation();
    TestRuntimeSampleContracts();
    TestRuntimePlansExhaustively();
    TestAoGiMultiBounceAndCandidatePlans();
    TestPermutationAndHistoryKeys();
    TestInvalidWorkloads();
    TestInvalidProfileAndCandidateSafeguards();
    TestVerificationProfilesAndReasons();

    std::cout << "UVSR visibility performance plan validation passed\n";
    return EXIT_SUCCESS;
}
