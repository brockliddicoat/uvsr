#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace uvsr
{
    enum class VisibilityOptimizationClass : uint8_t
    {
        Unset,
        Reference,
        Exact,
        Numerical,
        Algorithmic
    };

    enum class VisibilityPerformanceProfile : uint8_t
    {
        Unset,
        Reference,
        Runtime,
        ExactFusedResolveApply,
        AlgorithmicPackedEdges2x2,
        AlgorithmicPackedEdgesDepthNormal2x2,
        AlgorithmicPackedEdgesSlope2x2,
        AlgorithmicPackedEdgesLeakage2x2,
        AlgorithmicFusedPackedEdges2x2,
        Count
    };

    enum class VisibilityTraceImplementation : uint8_t
    {
        Unset,
        RuntimeBitmask
    };

    enum class VisibilitySampleSpecialization : uint8_t
    {
        Unset,
        Runtime
    };

    enum class VisibilityRuntimeSampleContract : uint8_t
    {
        Guarded,
        TrustedEven,
        TrustedOdd
    };

    enum class VisibilityMathMode : uint8_t
    {
        Unset,
        ReferenceFp32
    };

    enum class VisibilityRawAoStorage : uint8_t
    {
        Unset,
        ScalarFloat
    };

    enum class VisibilityEdgeStorage : uint8_t
    {
        Unset,
        None,
        R8Uint
    };

    enum class VisibilityReconstructionMode : uint8_t
    {
        Unset,
        Legacy,
        PackedEdges2x2
    };

    enum class VisibilityTemporalMode : uint8_t
    {
        Unset,
        Legacy
    };

    enum class VisibilityApplicationMode : uint8_t
    {
        Unset,
        LegacySeparateComposition,
        FusedResolveAndApplyExact,
        FusedResolveAndApplyPackedEdges
    };

    enum class VisibilityDepthMode : uint8_t
    {
        Unset,
        Legacy
    };

    enum class VisibilityBindingStrategy : uint8_t
    {
        Unset,
        MinimalConditional
    };

    enum class VisibilityTraversalOrder : uint8_t
    {
        Unset,
        InterleavedNegativePositiveNearToFar,
        GroupedBySide
    };

    enum class VisibilityConsumerRequirement : uint8_t
    {
        Unset,
        Any,
        AmbientOcclusionOnly,
        IncludesAmbientOcclusion,
        IncludesIndirectDiffuse
    };

    enum class VisibilityEstimatorRequirement : uint8_t
    {
        Unset,
        Any,
        UniformProjectedAngle,
        UniformSolidAngle,
        CosineWeightedSolidAngle
    };

    enum class VisibilityResolutionRequirement : uint8_t
    {
        Unset,
        Any,
        Reduced,
        Half,
        Full
    };

    enum class VisibilityPerformanceConsumer : uint8_t
    {
        AmbientOcclusion,
        IndirectDiffuse,
        AmbientOcclusionAndIndirectDiffuse
    };

    enum class VisibilityPerformanceEstimator : uint8_t
    {
        UniformProjectedAngle,
        UniformSolidAngle,
        CosineWeightedSolidAngle
    };

    enum class VisibilityPerformanceResolution : uint8_t
    {
        Full,
        Half,
        Quarter
    };

    enum class VisibilityPerformanceScheduler : uint8_t
    {
        IndependentHash,
        ToroidalBlueNoiseRankField
    };

    enum class VisibilityImplementationStatus : uint8_t
    {
        Unset,
        Implemented,
        PartialBenchmarkControl,
        Unavailable
    };

    inline constexpr uint64_t VisibilityProfileAllAssignments =
        (uint64_t{ 1 } << 22u) - 1u;

    struct VisibilityPerformanceProfileConfiguration
    {
        VisibilityPerformanceProfile profile =
            VisibilityPerformanceProfile::Unset;
        std::string_view name;
        VisibilityOptimizationClass optimizationClass =
            VisibilityOptimizationClass::Unset;
        VisibilityTraceImplementation trace =
            VisibilityTraceImplementation::Unset;
        VisibilitySampleSpecialization firstBounceSamples =
            VisibilitySampleSpecialization::Unset;
        VisibilitySampleSpecialization laterBounceSamples =
            VisibilitySampleSpecialization::Unset;
        VisibilityMathMode math = VisibilityMathMode::Unset;
        VisibilityRawAoStorage rawAoStorage = VisibilityRawAoStorage::Unset;
        VisibilityEdgeStorage edgeStorage = VisibilityEdgeStorage::Unset;
        VisibilityReconstructionMode reconstruction =
            VisibilityReconstructionMode::Unset;
        VisibilityTemporalMode temporal = VisibilityTemporalMode::Unset;
        VisibilityApplicationMode application =
            VisibilityApplicationMode::Unset;
        VisibilityDepthMode depth = VisibilityDepthMode::Unset;
        VisibilityBindingStrategy bindings =
            VisibilityBindingStrategy::Unset;
        VisibilityTraversalOrder traversal =
            VisibilityTraversalOrder::Unset;
        VisibilityConsumerRequirement consumerRequirement =
            VisibilityConsumerRequirement::Unset;
        VisibilityEstimatorRequirement estimatorRequirement =
            VisibilityEstimatorRequirement::Unset;
        VisibilityResolutionRequirement resolutionRequirement =
            VisibilityResolutionRequirement::Unset;
        VisibilityImplementationStatus implementationStatus =
            VisibilityImplementationStatus::Unset;
        std::string_view implementationNote;
        bool benchmarkOnly = false;
        bool explicitHalfRoundtrip = false;
        uint64_t assignmentMask = 0u;
    };

    struct VisibilityPerformanceWorkload
    {
        VisibilityPerformanceConsumer consumer =
            VisibilityPerformanceConsumer::AmbientOcclusion;
        VisibilityPerformanceEstimator estimator =
            VisibilityPerformanceEstimator::UniformSolidAngle;
        VisibilityPerformanceResolution resolution =
            VisibilityPerformanceResolution::Half;
        VisibilityPerformanceScheduler scheduler =
            VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField;
        uint32_t firstBounceSampleCount = 8u;
        uint32_t laterBounceSampleCount = 8u;
        uint32_t bounceCount = 1u;
        uint32_t outputWidth = 1920u;
        uint32_t outputHeight = 1080u;
        float radius = 3.0f;
        float thickness = 0.5f;
        float radialExponent = 2.0f;
        uint32_t threadGroupSizeX = 8u;
        uint32_t threadGroupSizeY = 8u;
        bool temporalEnabled = false;
        bool spatialEnabled = false;
        bool depthHierarchyEnabled = false;
        // Compact identity for independently composed shader math, thread
        // behavior, and physical buffer precision controls owned by the
        // renderer settings. The planner includes it in every relevant key.
        uint64_t runtimeConfigurationKey = 0u;
    };

    enum class VisibilityPlanError : uint8_t
    {
        None,
        IncompleteProfile,
        InvalidWorkload,
        ProfileImplementationUnavailable,
        ReferenceContractViolation,
        ProfileConsumerMismatch,
        ProfileEstimatorMismatch,
        ProfileResolutionMismatch,
        ProfileThreadGroupMismatch,
        InvalidPackedReconstruction,
        PackedReconstructionDoesNotSupportSpatialFilter,
        FusedApplicationRequiresAoOnly,
        FusedApplicationRequiresReducedResolution,
        FusedApplicationRequiresHalfRoundtrip,
        FusedApplicationDoesNotSupportSpatialFilter,
        FusedApplicationRequiresPackedEdges,
        IndirectDiffuseTraversalReordered,
        LaterBounceSpecializationRequiresIndirectDiffuse
    };

    enum class VisibilityExecutionResource : uint64_t
    {
        // Each bit denotes an allocation family, not one texture instance.
        // Temporal families and legacy source-port staging are ping-pong allocations.
        RawAmbient = uint64_t{ 1 } << 0u,
        FinalAmbient = uint64_t{ 1 } << 3u,
        RawIndirect = uint64_t{ 1 } << 5u,
        CumulativeIndirect = uint64_t{ 1 } << 6u,
        FinalIndirect = uint64_t{ 1 } << 7u,
        TemporalAmbient = uint64_t{ 1 } << 8u,
        TemporalIndirect = uint64_t{ 1 } << 10u,
        TemporalDepth = uint64_t{ 1 } << 11u,
        TemporalNormalRgba8 = uint64_t{ 1 } << 12u,
        DepthHierarchy = uint64_t{ 1 } << 13u,
        ToroidalNoise = uint64_t{ 1 } << 14u,
        PackedEdgesR8Uint = uint64_t{ 1 } << 17u
    };

    enum class VisibilityExecutionBinding : uint64_t
    {
        Depth = uint64_t{ 1 } << 0u,
        Normals = uint64_t{ 1 } << 1u,
        MotionVectors = uint64_t{ 1 } << 2u,
        SourceRadiance = uint64_t{ 1 } << 3u,
        GBufferMaterial = uint64_t{ 1 } << 4u,
        BaseLighting = uint64_t{ 1 } << 5u,
        OutputLighting = uint64_t{ 1 } << 6u,
        ToroidalNoise = uint64_t{ 1 } << 7u,
        DepthHierarchy = uint64_t{ 1 } << 10u,
        AmbientHistory = uint64_t{ 1 } << 11u,
        IndirectHistory = uint64_t{ 1 } << 12u,
        AmbientOutput = uint64_t{ 1 } << 13u,
        IndirectOutput = uint64_t{ 1 } << 14u,
        PackedEdges = uint64_t{ 1 } << 15u
    };

    enum class VisibilityExecutionPass : uint64_t
    {
        DepthPreparation = uint64_t{ 1 } << 0u,
        RuntimeTrace = uint64_t{ 1 } << 1u,
        RuntimeLaterBounceTrace = uint64_t{ 1 } << 7u,
        Temporal = uint64_t{ 1 } << 9u,
        Reconstruction = uint64_t{ 1 } << 10u,
        Composition = uint64_t{ 1 } << 11u,
        FusedResolveAndApply = uint64_t{ 1 } << 12u,
        SpatialDenoise = uint64_t{ 1 } << 14u
    };

    inline constexpr uint64_t VisibilityOptionalResourceMask =
        static_cast<uint64_t>(
            VisibilityExecutionResource::PackedEdgesR8Uint);

    inline constexpr uint64_t VisibilityCandidateBindingMask =
        static_cast<uint64_t>(VisibilityExecutionBinding::PackedEdges);

    inline constexpr uint64_t VisibilityCandidatePassMask =
        static_cast<uint64_t>(
            VisibilityExecutionPass::FusedResolveAndApply) |
        static_cast<uint64_t>(VisibilityExecutionPass::SpatialDenoise);

    struct VisibilityExecutionPlan
    {
        bool valid = false;
        VisibilityPlanError error = VisibilityPlanError::None;
        std::string errorMessage;
        VisibilityPerformanceProfileConfiguration configuration;
        VisibilityPerformanceWorkload workload;
        bool selectsLegacyReference = false;
        bool preservesProductionBitmask = false;
        bool benchmarkOnly = false;
        bool requiresExplicitHalfRoundtrip = false;
        // Runtime uses one CPU-validated parity contract per bounce. This
        // compiles out clamping and the even-count odd-side fetch without
        // creating one shader permutation for every slider value.
        VisibilityRuntimeSampleContract firstBounceRuntimeSamples =
            VisibilityRuntimeSampleContract::Guarded;
        VisibilityRuntimeSampleContract laterBounceRuntimeSamples =
            VisibilityRuntimeSampleContract::Guarded;
        uint32_t dispatchCount = 0u;
        // Exact descriptor counts for the simultaneously bound first-trace
        // layout. These are deliberately separate from bindingMask, which is
        // a conceptual union across every pass in the effect.
        uint32_t firstTraceSrvCount = 0u;
        uint32_t firstTraceUavCount = 0u;
        // Maximum descriptor counts among all selected pass layouts. SRV and
        // UAV maxima can come from different passes; they are not summed
        // simultaneous descriptors.
        uint32_t peakSrvCount = 0u;
        uint32_t peakUavCount = 0u;
        uint64_t resourceMask = 0u;
        uint64_t bindingMask = 0u;
        uint64_t passMask = 0u;
        uint64_t optionalResourceMask = 0u;
        uint64_t candidateBindingMask = 0u;
        uint64_t candidatePassMask = 0u;
        // Compile-time shader/layout identity. Unlike permutationKey, this
        // deliberately excludes output size and continuous runtime constants
        // so slider changes cannot grow the lazy PSO cache without bound.
        uint64_t shaderPermutationKey = 0u;
        uint64_t permutationKey = 0u;
        uint64_t historyResetKey = 0u;
        std::string permutationName;
    };

    enum class VisibilityVerificationProfile : uint8_t
    {
        Unset,
        ReferenceAo8T,
        RuntimeAo8T,
        PackedEdgeAo8T,
        ReferenceAoGi8T,
        RuntimeAoGi8T,
        RuntimeAoGi12T,
        RuntimeAoGi16T,
        RuntimeMultiBounce,
        Count
    };

    struct VisibilityVerificationProfileDefinition
    {
        VisibilityVerificationProfile profile =
            VisibilityVerificationProfile::Unset;
        std::string_view name;
        VisibilityPerformanceProfile implementationProfile =
            VisibilityPerformanceProfile::Unset;
        VisibilityPerformanceWorkload expectedWorkload;
        VisibilityImplementationStatus implementationStatus =
            VisibilityImplementationStatus::Unset;
        std::string_view implementationNote;
    };

    struct VisibilityVerificationProfileResolution
    {
        bool valid = false;
        std::string reason;
        VisibilityVerificationProfileDefinition definition;
        VisibilityExecutionPlan executionPlan;
    };

    [[nodiscard]] VisibilityPerformanceProfileConfiguration
        GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile profile);

    [[nodiscard]] bool IsVisibilityPerformanceProfileFullyAssigned(
        const VisibilityPerformanceProfileConfiguration& configuration);

    [[nodiscard]] VisibilityExecutionPlan ResolveVisibilityExecutionPlan(
        VisibilityPerformanceProfile profile,
        const VisibilityPerformanceWorkload& workload);

    [[nodiscard]] VisibilityExecutionPlan ResolveVisibilityExecutionPlan(
        const VisibilityPerformanceProfileConfiguration& configuration,
        const VisibilityPerformanceWorkload& workload);

    [[nodiscard]] VisibilityVerificationProfileDefinition
        GetVisibilityVerificationProfileDefinition(
            VisibilityVerificationProfile profile);

    [[nodiscard]] VisibilityVerificationProfileResolution
        ResolveVisibilityVerificationProfile(
            VisibilityVerificationProfile profile,
            const VisibilityPerformanceWorkload& observedWorkload);

    [[nodiscard]] VisibilityVerificationProfileResolution
        ResolveVisibilityVerificationProfile(
            VisibilityVerificationProfile profile,
            VisibilityPerformanceProfile observedImplementationProfile,
            const VisibilityPerformanceWorkload& observedWorkload);

    [[nodiscard]] constexpr bool HasVisibilityExecutionResource(
        uint64_t mask,
        VisibilityExecutionResource resource)
    {
        return (mask & static_cast<uint64_t>(resource)) != 0u;
    }

    [[nodiscard]] constexpr bool HasVisibilityExecutionBinding(
        uint64_t mask,
        VisibilityExecutionBinding binding)
    {
        return (mask & static_cast<uint64_t>(binding)) != 0u;
    }

    [[nodiscard]] constexpr bool HasVisibilityExecutionPass(
        uint64_t mask,
        VisibilityExecutionPass pass)
    {
        return (mask & static_cast<uint64_t>(pass)) != 0u;
    }

    inline constexpr uint16_t VisibilityPackedCountMask = 0x003fu;
    inline constexpr uint16_t VisibilityPackedEdgesMask = 0x3fc0u;
    inline constexpr uint16_t VisibilityPackedReservedMask = 0xc000u;

    [[nodiscard]] bool TryPackVisibilityCountAndEdges(
        uint32_t occludedSectorCount,
        uint8_t packedEdges,
        uint16_t& packedValue) noexcept;

    [[nodiscard]] constexpr uint32_t UnpackVisibilitySectorCount(
        uint16_t packedValue) noexcept
    {
        return uint32_t(packedValue & VisibilityPackedCountMask);
    }

    [[nodiscard]] constexpr uint8_t UnpackVisibilityEdges(
        uint16_t packedValue) noexcept
    {
        return uint8_t((packedValue & VisibilityPackedEdgesMask) >> 6u);
    }

    [[nodiscard]] constexpr bool IsCanonicalPackedVisibilityValue(
        uint16_t packedValue) noexcept
    {
        return (packedValue & VisibilityPackedReservedMask) == 0u &&
            UnpackVisibilitySectorCount(packedValue) <= 32u;
    }

    enum class VisibilityPackedEdge : uint8_t
    {
        Left,
        Right,
        Top,
        Bottom
    };

    [[nodiscard]] constexpr uint8_t UnpackVisibilityEdgeWeight(
        uint8_t packedEdges,
        VisibilityPackedEdge edge) noexcept
    {
        return uint8_t((packedEdges >>
            (6u - 2u * static_cast<uint8_t>(edge))) & 0x3u);
    }

}
