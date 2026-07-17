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
        Diagnostic,
        Exact,
        Numerical,
        Algorithmic
    };

    enum class VisibilityPerformanceProfile : uint8_t
    {
        Unset,
        Reference,
        ExactFixed8,
        ExactFixed12,
        ExactFixed16,
        ExactFixed20,
        ExactFixedLaterBounce8,
        ExactFixedAllBounce8,
        ExactPackedCurrentFast,
        ExactFusedResolveApply,
        ExactFixed8FusedResolveApply,
        DiagnosticFusedFullResolutionAoOutput,
        ExactGroup16x8,
        ExactGroup8x16,
        ExactDuplicatePixelRejectionOff,
        ExactFullMaskEarlyExitOff,
        AlgorithmicProjectedRadiusClamp32,
        AlgorithmicProjectedRadiusClamp64,
        AlgorithmicProjectedRadiusClamp128,
        DiagnosticConstantScheduler,
        DiagnosticConstantTrace,
        DiagnosticDepthOnlyTrace,
        DiagnosticBitmaskOnlyTrace,
        DiagnosticTemporalCopy,
        DiagnosticNearestResolve,
        DiagnosticBilinearResolve,
        DiagnosticCompositionOnly,
        DiagnosticCompositionBypass,
        ConservativeNumerical,
        AlgorithmicPackedEdges2x2,
        AlgorithmicPackedEdges4x4,
        AlgorithmicPackedEdgesDepthNormal2x2,
        AlgorithmicPackedEdgesSlope2x2,
        AlgorithmicPackedEdgesLeakage2x2,
        AlgorithmicFusedPackedEdges2x2,
        AlgorithmicFusedPackedEdges4x4,
        AlgorithmicActivisionSchedule,
        ActivisionClosestMatch,
        ActivisionPs4Schedule,
        ActivisionPs4PackedGather,
        XeGtaoClosestMatch,
        XeGtaoHighInlineHilbert,
        XeGtaoHighFp32,
        GenericFallback,
        Count
    };

    enum class VisibilityTraceImplementation : uint8_t
    {
        Unset,
        LegacyGenericBitmask,
        FixedInterleavedBitmask,
        ConstantDiagnostic,
        DepthOnlyDiagnostic,
        BitmaskOnlyDiagnostic,
        ActivisionHorizon,
        XeGtaoHorizon
    };

    enum class VisibilitySampleSpecialization : uint8_t
    {
        Unset,
        Runtime,
        Fixed8,
        Fixed12,
        Fixed16,
        Fixed18,
        Fixed20
    };

    enum class VisibilityNoiseDelivery : uint8_t
    {
        Unset,
        Legacy,
        PackedCurrentFast,
        ConstantDiagnostic,
        ActivisionInterleavedGradient,
        XeGtaoHilbertR2,
        XeGtaoInlineHilbertR2
    };

    enum class VisibilityMathMode : uint8_t
    {
        Unset,
        ReferenceFp32,
        ConservativeNumericalFp32,
        ActivisionFastFp32,
        XeGtaoMixedPrecision
    };

    enum class VisibilityRawAoStorage : uint8_t
    {
        Unset,
        R16Float,
        R8Unorm,
        PackedCountAndEdgesR16Uint
    };

    enum class VisibilityEdgeStorage : uint8_t
    {
        Unset,
        None,
        R8Uint,
        R8Unorm
    };

    enum class VisibilityReconstructionMode : uint8_t
    {
        Unset,
        Legacy,
        NearestDiagnostic,
        BilinearDiagnostic,
        PackedEdges2x2,
        PackedEdges4x4,
        ActivisionBilateral4x4,
        XeGtaoDenoise
    };

    enum class VisibilityTemporalMode : uint8_t
    {
        Unset,
        Legacy,
        CopyDiagnostic,
        ActivisionSixDirectionEma
    };

    enum class VisibilityApplicationMode : uint8_t
    {
        Unset,
        LegacySeparateComposition,
        FusedResolveAndApplyExact,
        FusedResolveAndApplyPackedEdges,
        IsolatedCompositionDiagnostic,
        BypassCompositionDiagnostic
    };

    enum class VisibilityDepthMode : uint8_t
    {
        Unset,
        Legacy,
        ActivisionClampedScreenRadius,
        XeGtaoPrefilteredMips
    };

    enum class VisibilityBindingStrategy : uint8_t
    {
        Unset,
        LegacyBroad,
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
        ToroidalBlueNoiseRankField,
        FilterAdaptedSpatiotemporalRankField,
        Activision4x4SixPhase,
        XeGtaoHilbertR2,
        ConstantDiagnostic
    };

    enum class VisibilityImplementationStatus : uint8_t
    {
        Unset,
        Implemented,
        PartialBenchmarkControl,
        Unavailable
    };

    inline constexpr uint64_t VisibilityProfileAllAssignments =
        (uint64_t{ 1 } << 23u) - 1u;

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
        VisibilityNoiseDelivery noise = VisibilityNoiseDelivery::Unset;
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
        bool adaptiveSamplingEnabled = false;
        bool temporalEnabled = false;
        bool spatialEnabled = false;
        bool depthHierarchyEnabled = false;
    };

    enum class VisibilityPlanError : uint8_t
    {
        None,
        IncompleteProfile,
        InvalidWorkload,
        ProfileImplementationUnavailable,
        ReferenceContractViolation,
        SampleCountMismatch,
        FixedExponentMismatch,
        AdaptiveFixedCountConflict,
        ProfileConsumerMismatch,
        ProfileEstimatorMismatch,
        ProfileResolutionMismatch,
        ProfileSchedulerMismatch,
        ProfileThreadGroupMismatch,
        TemporalModeRequiresHistory,
        UnsupportedAoEncoding,
        InvalidPackedReconstruction,
        PackedReconstructionDoesNotSupportSpatialFilter,
        FusedApplicationRequiresAoOnly,
        FusedApplicationRequiresReducedResolution,
        FusedApplicationRequiresHalfRoundtrip,
        FusedApplicationDoesNotSupportSpatialFilter,
        FusedApplicationRequiresPackedEdges,
        IndirectDiffuseTraversalReordered,
        LaterBounceSpecializationRequiresIndirectDiffuse,
        BenchmarkProfileContractViolation
    };

    enum class VisibilityExecutionResource : uint64_t
    {
        // Each bit denotes an allocation family, not one texture instance.
        // Temporal families and XeGTAO working AO are ping-pong allocations.
        RawAmbientR16 = uint64_t{ 1 } << 0u,
        RawAmbientR8 = uint64_t{ 1 } << 1u,
        RawAmbientPackedCountEdgesR16 = uint64_t{ 1 } << 2u,
        FinalAmbientR16 = uint64_t{ 1 } << 3u,
        FinalAmbientR8 = uint64_t{ 1 } << 4u,
        RawIndirectRgba16 = uint64_t{ 1 } << 5u,
        CumulativeIndirectRgba16 = uint64_t{ 1 } << 6u,
        FinalIndirectRgba16 = uint64_t{ 1 } << 7u,
        TemporalAmbientR16 = uint64_t{ 1 } << 8u,
        TemporalAmbientR8 = uint64_t{ 1 } << 9u,
        TemporalIndirectRgba16 = uint64_t{ 1 } << 10u,
        TemporalDepthR32 = uint64_t{ 1 } << 11u,
        TemporalNormalRgba8 = uint64_t{ 1 } << 12u,
        DepthHierarchy = uint64_t{ 1 } << 13u,
        LegacyToroidalNoise = uint64_t{ 1 } << 14u,
        LegacyCurrentFastNoise = uint64_t{ 1 } << 15u,
        PackedCurrentFastNoise = uint64_t{ 1 } << 16u,
        PackedEdgesR8Uint = uint64_t{ 1 } << 17u,
        PackedEdgesR8Unorm = uint64_t{ 1 } << 18u,
        ActivisionSpatialAmbientR16 = uint64_t{ 1 } << 19u,
        ActivisionPackedDepthGuideR32Uint = uint64_t{ 1 } << 20u,
        XeGtaoWorkingAoR16 = uint64_t{ 1 } << 21u,
        XeGtaoHilbertLutR16Uint = uint64_t{ 1 } << 22u
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
        LegacyToroidalNoise = uint64_t{ 1 } << 7u,
        LegacyCurrentFastNoise = uint64_t{ 1 } << 8u,
        PackedCurrentFastNoise = uint64_t{ 1 } << 9u,
        DepthHierarchy = uint64_t{ 1 } << 10u,
        AmbientHistory = uint64_t{ 1 } << 11u,
        IndirectHistory = uint64_t{ 1 } << 12u,
        AmbientOutput = uint64_t{ 1 } << 13u,
        IndirectOutput = uint64_t{ 1 } << 14u,
        PackedEdges = uint64_t{ 1 } << 15u,
        XeGtaoEdges = uint64_t{ 1 } << 16u,
        XeGtaoHilbertLut = uint64_t{ 1 } << 17u,
        ActivisionPreparedDepth = uint64_t{ 1 } << 18u
    };

    enum class VisibilityExecutionPass : uint64_t
    {
        DepthPreparation = uint64_t{ 1 } << 0u,
        LegacyTrace = uint64_t{ 1 } << 1u,
        CandidateGenericTrace = uint64_t{ 1 } << 2u,
        FixedTrace = uint64_t{ 1 } << 3u,
        DiagnosticTrace = uint64_t{ 1 } << 4u,
        ActivisionHorizonTrace = uint64_t{ 1 } << 5u,
        XeGtaoHorizonTrace = uint64_t{ 1 } << 6u,
        LegacyLaterBounceTrace = uint64_t{ 1 } << 7u,
        FixedLaterBounceTrace = uint64_t{ 1 } << 8u,
        Temporal = uint64_t{ 1 } << 9u,
        Reconstruction = uint64_t{ 1 } << 10u,
        Composition = uint64_t{ 1 } << 11u,
        FusedResolveAndApply = uint64_t{ 1 } << 12u,
        CompositionBypass = uint64_t{ 1 } << 13u,
        SpatialDenoise = uint64_t{ 1 } << 14u
    };

    inline constexpr uint64_t VisibilityOptionalResourceMask =
        static_cast<uint64_t>(VisibilityExecutionResource::RawAmbientR8) |
        static_cast<uint64_t>(
            VisibilityExecutionResource::RawAmbientPackedCountEdgesR16) |
        static_cast<uint64_t>(VisibilityExecutionResource::FinalAmbientR8) |
        static_cast<uint64_t>(
            VisibilityExecutionResource::TemporalAmbientR8) |
        static_cast<uint64_t>(
            VisibilityExecutionResource::PackedCurrentFastNoise) |
        static_cast<uint64_t>(
            VisibilityExecutionResource::PackedEdgesR8Uint) |
        static_cast<uint64_t>(
            VisibilityExecutionResource::PackedEdgesR8Unorm) |
        static_cast<uint64_t>(
            VisibilityExecutionResource::ActivisionSpatialAmbientR16) |
        static_cast<uint64_t>(
            VisibilityExecutionResource::ActivisionPackedDepthGuideR32Uint) |
        static_cast<uint64_t>(
            VisibilityExecutionResource::XeGtaoWorkingAoR16) |
        static_cast<uint64_t>(
            VisibilityExecutionResource::XeGtaoHilbertLutR16Uint);

    inline constexpr uint64_t VisibilityCandidateBindingMask =
        static_cast<uint64_t>(
            VisibilityExecutionBinding::PackedCurrentFastNoise) |
        static_cast<uint64_t>(VisibilityExecutionBinding::PackedEdges) |
        static_cast<uint64_t>(VisibilityExecutionBinding::XeGtaoEdges) |
        static_cast<uint64_t>(
            VisibilityExecutionBinding::XeGtaoHilbertLut) |
        static_cast<uint64_t>(
            VisibilityExecutionBinding::ActivisionPreparedDepth);

    inline constexpr uint64_t VisibilityCandidatePassMask =
        static_cast<uint64_t>(
            VisibilityExecutionPass::CandidateGenericTrace) |
        static_cast<uint64_t>(VisibilityExecutionPass::FixedTrace) |
        static_cast<uint64_t>(VisibilityExecutionPass::DiagnosticTrace) |
        static_cast<uint64_t>(
            VisibilityExecutionPass::ActivisionHorizonTrace) |
        static_cast<uint64_t>(VisibilityExecutionPass::XeGtaoHorizonTrace) |
        static_cast<uint64_t>(
            VisibilityExecutionPass::FixedLaterBounceTrace) |
        static_cast<uint64_t>(
            VisibilityExecutionPass::FusedResolveAndApply) |
        static_cast<uint64_t>(VisibilityExecutionPass::CompositionBypass) |
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
        uint32_t fixedFirstBounceSampleCount = 0u;
        uint32_t fixedLaterBounceSampleCount = 0u;
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
        ExactFastAo8T,
        MixedPrecisionAo8T,
        PackedEdgeAo8T,
        ActivisionScheduleAo8T,
        ReferenceAoGi8T,
        ExactFastAoGi8T,
        ExactFastAoGi12T,
        ExactFastAoGi16T,
        ExactFastMultiBounce,
        AggressiveExperimentalAo8T,
        XeGtaoClosestMatch,
        Ps4GtaoClosestMatch,
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

    enum class VisibilitySampleSide : uint8_t
    {
        Negative,
        Positive
    };

    struct VisibilityFixedSampleVisit
    {
        bool valid = false;
        uint32_t visitIndex = 0u;
        uint32_t pairIndex = 0u;
        uint32_t sideStepIndex = 0u;
        VisibilitySampleSide side = VisibilitySampleSide::Negative;
    };

    [[nodiscard]] constexpr bool IsSupportedFixedVisibilitySampleCount(
        uint32_t sampleCount) noexcept
    {
        return sampleCount == 8u || sampleCount == 12u ||
            sampleCount == 16u || sampleCount == 20u;
    }

    // Mirrors the generic trace's outer near-to-far iteration and inner
    // sideIndex loop: negative first, then positive, for every radial pair.
    [[nodiscard]] constexpr VisibilityFixedSampleVisit
        GetFixedInterleavedVisibilitySampleVisit(
            uint32_t sampleCount,
            uint32_t visitIndex) noexcept
    {
        if (!IsSupportedFixedVisibilitySampleCount(sampleCount) ||
            visitIndex >= sampleCount)
        {
            return {};
        }

        const uint32_t pairIndex = visitIndex >> 1u;
        return {
            true,
            visitIndex,
            pairIndex,
            pairIndex,
            (visitIndex & 1u) == 0u
                ? VisibilitySampleSide::Negative
                : VisibilitySampleSide::Positive
        };
    }
}
