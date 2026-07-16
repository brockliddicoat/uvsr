#include "visibility_performance_plan.h"

#include <cmath>
#include <cstring>
#include <sstream>

namespace uvsr
{
    namespace
    {
        constexpr uint32_t c_MaximumRadialSampleCount = 64u;
        constexpr uint32_t c_MaximumBounceCount = 8u;
        constexpr uint64_t c_FnvOffsetBasis = 14695981039346656037ull;
        constexpr uint64_t c_FnvPrime = 1099511628211ull;

        VisibilityPerformanceProfileConfiguration MakeConfiguration(
            VisibilityPerformanceProfile profile,
            std::string_view name,
            VisibilityOptimizationClass optimizationClass,
            VisibilityTraceImplementation trace,
            VisibilitySampleSpecialization firstBounceSamples,
            VisibilitySampleSpecialization laterBounceSamples,
            VisibilityNoiseDelivery noise,
            VisibilityMathMode math,
            VisibilityRawAoStorage rawAoStorage,
            VisibilityReconstructionMode reconstruction,
            VisibilityTemporalMode temporal,
            VisibilityApplicationMode application,
            VisibilityDepthMode depth,
            VisibilityBindingStrategy bindings,
            VisibilityTraversalOrder traversal,
            VisibilityConsumerRequirement consumerRequirement,
            VisibilityResolutionRequirement resolutionRequirement,
            bool benchmarkOnly,
            bool explicitHalfRoundtrip,
            VisibilityEdgeStorage edgeStorage =
                VisibilityEdgeStorage::None,
            VisibilityEstimatorRequirement estimatorRequirement =
                VisibilityEstimatorRequirement::Any,
            VisibilityImplementationStatus implementationStatus =
                VisibilityImplementationStatus::Implemented,
            std::string_view implementationNote = {})
        {
            VisibilityPerformanceProfileConfiguration result;
            result.profile = profile;
            result.name = name;
            result.optimizationClass = optimizationClass;
            result.trace = trace;
            result.firstBounceSamples = firstBounceSamples;
            result.laterBounceSamples = laterBounceSamples;
            result.noise = noise;
            result.math = math;
            result.rawAoStorage = rawAoStorage;
            result.edgeStorage = edgeStorage;
            result.reconstruction = reconstruction;
            result.temporal = temporal;
            result.application = application;
            result.depth = depth;
            result.bindings = bindings;
            result.traversal = traversal;
            result.consumerRequirement = consumerRequirement;
            result.estimatorRequirement = estimatorRequirement;
            result.resolutionRequirement = resolutionRequirement;
            result.implementationStatus = implementationStatus;
            result.implementationNote = implementationNote;
            result.benchmarkOnly = benchmarkOnly;
            result.explicitHalfRoundtrip = explicitHalfRoundtrip;
            result.assignmentMask = VisibilityProfileAllAssignments;
            return result;
        }

        bool HasAmbientOcclusion(VisibilityPerformanceConsumer consumer)
        {
            return consumer ==
                    VisibilityPerformanceConsumer::AmbientOcclusion ||
                consumer == VisibilityPerformanceConsumer::
                    AmbientOcclusionAndIndirectDiffuse;
        }

        bool HasIndirectDiffuse(VisibilityPerformanceConsumer consumer)
        {
            return consumer ==
                    VisibilityPerformanceConsumer::IndirectDiffuse ||
                consumer == VisibilityPerformanceConsumer::
                    AmbientOcclusionAndIndirectDiffuse;
        }

        bool IsReducedResolution(VisibilityPerformanceResolution resolution)
        {
            return resolution == VisibilityPerformanceResolution::Half ||
                resolution == VisibilityPerformanceResolution::Quarter;
        }

        bool MatchesEstimatorRequirement(
            VisibilityEstimatorRequirement requirement,
            VisibilityPerformanceEstimator estimator)
        {
            switch (requirement)
            {
            case VisibilityEstimatorRequirement::Any:
                return true;
            case VisibilityEstimatorRequirement::UniformProjectedAngle:
                return estimator == VisibilityPerformanceEstimator::
                    UniformProjectedAngle;
            case VisibilityEstimatorRequirement::UniformSolidAngle:
                return estimator == VisibilityPerformanceEstimator::
                    UniformSolidAngle;
            case VisibilityEstimatorRequirement::CosineWeightedSolidAngle:
                return estimator == VisibilityPerformanceEstimator::
                    CosineWeightedSolidAngle;
            default:
                return false;
            }
        }

        uint32_t SpecializedSampleCount(
            VisibilitySampleSpecialization specialization)
        {
            switch (specialization)
            {
            case VisibilitySampleSpecialization::Runtime:
                return 0u;
            case VisibilitySampleSpecialization::Fixed8:
                return 8u;
            case VisibilitySampleSpecialization::Fixed12:
                return 12u;
            case VisibilitySampleSpecialization::Fixed16:
                return 16u;
            case VisibilitySampleSpecialization::Fixed20:
                return 20u;
            default:
                return 0u;
            }
        }

        uint64_t Bit(VisibilityExecutionResource resource)
        {
            return static_cast<uint64_t>(resource);
        }

        uint64_t Bit(VisibilityExecutionBinding binding)
        {
            return static_cast<uint64_t>(binding);
        }

        uint64_t Bit(VisibilityExecutionPass pass)
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

        void HashByte(uint64_t& hash, uint8_t value)
        {
            hash ^= value;
            hash *= c_FnvPrime;
        }

        void HashUint32(uint64_t& hash, uint32_t value)
        {
            for (uint32_t byte = 0u; byte < 4u; ++byte)
                HashByte(hash, uint8_t(value >> (byte * 8u)));
        }

        void HashUint64(uint64_t& hash, uint64_t value)
        {
            for (uint32_t byte = 0u; byte < 8u; ++byte)
                HashByte(hash, uint8_t(value >> (byte * 8u)));
        }

        void HashFloat(uint64_t& hash, float value)
        {
            uint32_t bits = 0u;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            HashUint32(hash, bits);
        }

        template <typename T>
        void HashEnum(uint64_t& hash, T value)
        {
            HashUint32(hash, static_cast<uint32_t>(value));
        }

        template <typename T>
        bool IsAssignedEnum(T value, T lastValue)
        {
            return static_cast<uint32_t>(value) > 0u &&
                static_cast<uint32_t>(value) <=
                    static_cast<uint32_t>(lastValue);
        }

        void HashBool(uint64_t& hash, bool value)
        {
            HashByte(hash, value ? uint8_t{ 1 } : uint8_t{ 0 });
        }

        bool IsValidWorkload(const VisibilityPerformanceWorkload& workload)
        {
            const bool validConsumer =
                workload.consumer ==
                    VisibilityPerformanceConsumer::AmbientOcclusion ||
                workload.consumer ==
                    VisibilityPerformanceConsumer::IndirectDiffuse ||
                workload.consumer == VisibilityPerformanceConsumer::
                    AmbientOcclusionAndIndirectDiffuse;
            const bool validEstimator =
                workload.estimator == VisibilityPerformanceEstimator::
                    UniformProjectedAngle ||
                workload.estimator ==
                    VisibilityPerformanceEstimator::UniformSolidAngle ||
                workload.estimator == VisibilityPerformanceEstimator::
                    CosineWeightedSolidAngle;
            const bool validResolution =
                workload.resolution ==
                    VisibilityPerformanceResolution::Full ||
                workload.resolution ==
                    VisibilityPerformanceResolution::Half ||
                workload.resolution ==
                    VisibilityPerformanceResolution::Quarter;
            const bool validScheduler =
                workload.scheduler ==
                    VisibilityPerformanceScheduler::IndependentHash ||
                workload.scheduler == VisibilityPerformanceScheduler::
                    ToroidalBlueNoiseRankField ||
                workload.scheduler == VisibilityPerformanceScheduler::
                    FilterAdaptedSpatiotemporalRankField ||
                workload.scheduler == VisibilityPerformanceScheduler::
                    Activision4x4SixPhase ||
                workload.scheduler ==
                    VisibilityPerformanceScheduler::XeGtaoHilbertR2;
            const bool constantScheduler = workload.scheduler ==
                VisibilityPerformanceScheduler::ConstantDiagnostic;
            const bool validCounts =
                workload.firstBounceSampleCount >= 1u &&
                workload.firstBounceSampleCount <=
                    c_MaximumRadialSampleCount &&
                workload.laterBounceSampleCount >= 1u &&
                workload.laterBounceSampleCount <=
                    c_MaximumRadialSampleCount &&
                workload.bounceCount >= 1u &&
                workload.bounceCount <= c_MaximumBounceCount;
            const bool validGeometry =
                workload.outputWidth >= 1u &&
                workload.outputWidth <= 16384u &&
                workload.outputHeight >= 1u &&
                workload.outputHeight <= 16384u &&
                std::isfinite(workload.radius) && workload.radius > 0.0f &&
                std::isfinite(workload.thickness) &&
                workload.thickness >= 0.0f &&
                std::isfinite(workload.radialExponent) &&
                workload.radialExponent > 0.0f &&
                workload.threadGroupSizeX >= 1u &&
                workload.threadGroupSizeY >= 1u &&
                workload.threadGroupSizeX <= 1024u &&
                workload.threadGroupSizeY <= 1024u &&
                uint64_t(workload.threadGroupSizeX) *
                    uint64_t(workload.threadGroupSizeY) <= 1024u;
            return validConsumer && validEstimator && validResolution &&
                (validScheduler || constantScheduler) && validCounts &&
                validGeometry;
        }

        bool HasExactReferenceContract(
            const VisibilityPerformanceProfileConfiguration& configuration)
        {
            const VisibilityPerformanceProfileConfiguration reference =
                GetVisibilityPerformanceProfileConfiguration(
                    VisibilityPerformanceProfile::Reference);
            return configuration.profile == reference.profile &&
                configuration.name == reference.name &&
                configuration.optimizationClass ==
                    reference.optimizationClass &&
                configuration.trace == reference.trace &&
                configuration.firstBounceSamples ==
                    reference.firstBounceSamples &&
                configuration.laterBounceSamples ==
                    reference.laterBounceSamples &&
                configuration.noise == reference.noise &&
                configuration.math == reference.math &&
                configuration.rawAoStorage == reference.rawAoStorage &&
                configuration.edgeStorage == reference.edgeStorage &&
                configuration.reconstruction == reference.reconstruction &&
                configuration.temporal == reference.temporal &&
                configuration.application == reference.application &&
                configuration.depth == reference.depth &&
                configuration.bindings == reference.bindings &&
                configuration.traversal == reference.traversal &&
                configuration.consumerRequirement ==
                    reference.consumerRequirement &&
                configuration.estimatorRequirement ==
                    reference.estimatorRequirement &&
                configuration.resolutionRequirement ==
                    reference.resolutionRequirement &&
                configuration.implementationStatus ==
                    reference.implementationStatus &&
                configuration.implementationNote ==
                    reference.implementationNote &&
                configuration.benchmarkOnly == reference.benchmarkOnly &&
                configuration.explicitHalfRoundtrip ==
                    reference.explicitHalfRoundtrip &&
                configuration.assignmentMask == reference.assignmentMask;
        }

        std::string MakePermutationName(
            const VisibilityPerformanceProfileConfiguration& configuration,
            const VisibilityPerformanceWorkload& workload)
        {
            std::ostringstream name;
            name << configuration.name
                << "/class=" << static_cast<uint32_t>(
                    configuration.optimizationClass)
                << "/trace=" << static_cast<uint32_t>(configuration.trace)
                << "/first-specialization=" << static_cast<uint32_t>(
                    configuration.firstBounceSamples)
                << "/later-specialization=" << static_cast<uint32_t>(
                    configuration.laterBounceSamples)
                << "/noise=" << static_cast<uint32_t>(configuration.noise)
                << "/math=" << static_cast<uint32_t>(configuration.math)
                << "/raw-ao=" << static_cast<uint32_t>(
                    configuration.rawAoStorage)
                << "/edges=" << static_cast<uint32_t>(
                    configuration.edgeStorage)
                << "/reconstruction=" << static_cast<uint32_t>(
                    configuration.reconstruction)
                << "/temporal-mode=" << static_cast<uint32_t>(
                    configuration.temporal)
                << "/application=" << static_cast<uint32_t>(
                    configuration.application)
                << "/depth=" << static_cast<uint32_t>(configuration.depth)
                << "/bindings=" << static_cast<uint32_t>(
                    configuration.bindings)
                << "/consumer=" << static_cast<uint32_t>(workload.consumer)
                << "/estimator=" << static_cast<uint32_t>(workload.estimator)
                << "/resolution=" << static_cast<uint32_t>(workload.resolution)
                << "/scheduler=" << static_cast<uint32_t>(workload.scheduler)
                << "/first=" << workload.firstBounceSampleCount
                << "/later=" << workload.laterBounceSampleCount
                << "/bounces=" << workload.bounceCount
                << "/output=" << workload.outputWidth << 'x'
                    << workload.outputHeight
                << "/radius=" << workload.radius
                << "/thickness=" << workload.thickness
                << "/exponent=" << workload.radialExponent
                << "/group=" << workload.threadGroupSizeX << 'x'
                    << workload.threadGroupSizeY
                << "/adaptive=" << (workload.adaptiveSamplingEnabled ? 1 : 0)
                << "/temporal=" << (workload.temporalEnabled ? 1 : 0)
                << "/spatial=" << (workload.spatialEnabled ? 1 : 0)
                << "/hierarchy=" << (workload.depthHierarchyEnabled ? 1 : 0);
            return name.str();
        }

        uint64_t MakePermutationKey(
            const VisibilityPerformanceProfileConfiguration& configuration,
            const VisibilityPerformanceWorkload& workload,
            const VisibilityExecutionPlan& plan)
        {
            uint64_t hash = c_FnvOffsetBasis;
            HashEnum(hash, configuration.profile);
            HashEnum(hash, configuration.optimizationClass);
            HashEnum(hash, configuration.trace);
            HashEnum(hash, configuration.firstBounceSamples);
            HashEnum(hash, configuration.laterBounceSamples);
            HashEnum(hash, configuration.noise);
            HashEnum(hash, configuration.math);
            HashEnum(hash, configuration.rawAoStorage);
            HashEnum(hash, configuration.edgeStorage);
            HashEnum(hash, configuration.reconstruction);
            HashEnum(hash, configuration.temporal);
            HashEnum(hash, configuration.application);
            HashEnum(hash, configuration.depth);
            HashEnum(hash, configuration.bindings);
            HashEnum(hash, configuration.traversal);
            HashEnum(hash, configuration.estimatorRequirement);
            HashEnum(hash, configuration.implementationStatus);
            HashEnum(hash, workload.consumer);
            HashEnum(hash, workload.estimator);
            HashEnum(hash, workload.resolution);
            HashEnum(hash, workload.scheduler);
            HashUint32(hash, workload.firstBounceSampleCount);
            HashUint32(hash, workload.laterBounceSampleCount);
            HashUint32(hash, workload.bounceCount);
            HashUint32(hash, workload.outputWidth);
            HashUint32(hash, workload.outputHeight);
            HashFloat(hash, workload.radius);
            HashFloat(hash, workload.thickness);
            HashFloat(hash, workload.radialExponent);
            HashUint32(hash, workload.threadGroupSizeX);
            HashUint32(hash, workload.threadGroupSizeY);
            HashBool(hash, workload.adaptiveSamplingEnabled);
            HashBool(hash, workload.temporalEnabled);
            HashBool(hash, workload.spatialEnabled);
            HashBool(hash, workload.depthHierarchyEnabled);
            HashUint64(hash, plan.resourceMask);
            HashUint64(hash, plan.bindingMask);
            HashUint64(hash, plan.passMask);
            return hash;
        }

        uint64_t MakeShaderPermutationKey(
            const VisibilityPerformanceProfileConfiguration& configuration,
            const VisibilityPerformanceWorkload& workload,
            const VisibilityExecutionPlan& plan)
        {
            uint64_t hash = c_FnvOffsetBasis;
            HashEnum(hash, configuration.profile);
            HashEnum(hash, configuration.trace);
            HashEnum(hash, configuration.firstBounceSamples);
            HashEnum(hash, configuration.laterBounceSamples);
            HashEnum(hash, configuration.noise);
            HashEnum(hash, configuration.math);
            HashEnum(hash, configuration.rawAoStorage);
            HashEnum(hash, configuration.edgeStorage);
            HashEnum(hash, configuration.reconstruction);
            HashEnum(hash, configuration.temporal);
            HashEnum(hash, configuration.application);
            HashEnum(hash, configuration.depth);
            HashEnum(hash, configuration.bindings);
            HashEnum(hash, workload.consumer);
            HashEnum(hash, workload.estimator);
            HashEnum(hash, workload.resolution);
            HashEnum(hash, workload.scheduler);
            HashUint32(hash, workload.firstBounceSampleCount);
            HashUint32(hash, workload.laterBounceSampleCount);
            HashUint32(hash, workload.bounceCount);
            HashUint32(hash, workload.threadGroupSizeX);
            HashUint32(hash, workload.threadGroupSizeY);
            HashBool(hash, workload.adaptiveSamplingEnabled);
            HashBool(hash, workload.temporalEnabled);
            HashBool(hash, workload.spatialEnabled);
            HashBool(hash, workload.depthHierarchyEnabled);
            HashUint64(hash, plan.resourceMask);
            HashUint64(hash, plan.bindingMask);
            HashUint64(hash, plan.passMask);
            return hash;
        }

        uint64_t MakeHistoryResetKey(
            const VisibilityPerformanceProfileConfiguration& configuration,
            const VisibilityPerformanceWorkload& workload,
            uint64_t permutationKey)
        {
            uint64_t hash = c_FnvOffsetBasis;
            // Profile identity intentionally participates even for exact
            // profiles so benchmark switches cannot reuse another profile's
            // accumulated history or adaptive feedback.
            HashEnum(hash, configuration.profile);
            HashUint64(hash, permutationKey);
            HashEnum(hash, workload.consumer);
            HashEnum(hash, workload.estimator);
            HashEnum(hash, workload.resolution);
            HashEnum(hash, configuration.rawAoStorage);
            HashEnum(hash, configuration.edgeStorage);
            HashEnum(hash, configuration.reconstruction);
            HashEnum(hash, configuration.temporal);
            HashEnum(hash, configuration.application);
            HashUint32(hash, workload.firstBounceSampleCount);
            HashUint32(hash, workload.laterBounceSampleCount);
            HashUint32(hash, workload.bounceCount);
            HashUint32(hash, workload.outputWidth);
            HashUint32(hash, workload.outputHeight);
            HashFloat(hash, workload.radius);
            HashFloat(hash, workload.thickness);
            HashFloat(hash, workload.radialExponent);
            HashBool(hash, workload.adaptiveSamplingEnabled);
            HashBool(hash, workload.temporalEnabled);
            HashBool(hash, workload.spatialEnabled);
            return hash;
        }

        VisibilityPerformanceWorkload MakeTargetVerificationWorkload()
        {
            VisibilityPerformanceWorkload workload;
            workload.consumer =
                VisibilityPerformanceConsumer::AmbientOcclusion;
            workload.estimator =
                VisibilityPerformanceEstimator::UniformSolidAngle;
            workload.resolution = VisibilityPerformanceResolution::Half;
            workload.scheduler = VisibilityPerformanceScheduler::
                ToroidalBlueNoiseRankField;
            workload.firstBounceSampleCount = 8u;
            workload.laterBounceSampleCount = 8u;
            workload.bounceCount = 1u;
            workload.outputWidth = 1920u;
            workload.outputHeight = 1080u;
            workload.radius = 3.0f;
            workload.thickness = 0.5f;
            workload.radialExponent = 2.0f;
            workload.threadGroupSizeX = 8u;
            workload.threadGroupSizeY = 8u;
            workload.adaptiveSamplingEnabled = false;
            workload.temporalEnabled = false;
            workload.spatialEnabled = false;
            workload.depthHierarchyEnabled = false;
            return workload;
        }

        std::string FindVerificationWorkloadMismatch(
            const VisibilityPerformanceWorkload& expected,
            const VisibilityPerformanceWorkload& observed)
        {
            if (expected.consumer != observed.consumer)
                return "Consumer does not match the profile.";
            if (expected.estimator != observed.estimator)
                return "Estimator does not match the profile.";
            if (expected.resolution != observed.resolution)
                return "Trace resolution does not match the profile.";
            if (expected.scheduler != observed.scheduler)
                return "Scheduler does not match the profile.";
            if (expected.firstBounceSampleCount !=
                observed.firstBounceSampleCount)
            {
                return "First-bounce sample count does not match the profile.";
            }
            if (expected.laterBounceSampleCount !=
                observed.laterBounceSampleCount)
            {
                return "Later-bounce sample count does not match the profile.";
            }
            if (expected.bounceCount != observed.bounceCount)
                return "Bounce count does not match the profile.";
            if (expected.outputWidth != observed.outputWidth ||
                expected.outputHeight != observed.outputHeight)
            {
                return "GPU output size does not match the profile.";
            }
            if (expected.radius != observed.radius)
                return "Radius does not match the profile.";
            if (expected.thickness != observed.thickness)
                return "Thickness does not match the profile.";
            if (expected.radialExponent != observed.radialExponent)
                return "Radial exponent does not match the profile.";
            if (expected.threadGroupSizeX != observed.threadGroupSizeX ||
                expected.threadGroupSizeY != observed.threadGroupSizeY)
            {
                return "Thread-group shape does not match the profile.";
            }
            if (expected.adaptiveSamplingEnabled !=
                observed.adaptiveSamplingEnabled)
            {
                return "Adaptive-sampling state does not match the profile.";
            }
            if (expected.temporalEnabled != observed.temporalEnabled)
                return "Temporal state does not match the profile.";
            if (expected.spatialEnabled != observed.spatialEnabled)
                return "Spatial-filter state does not match the profile.";
            if (expected.depthHierarchyEnabled !=
                observed.depthHierarchyEnabled)
            {
                return "Depth-hierarchy state does not match the profile.";
            }
            return {};
        }

        VisibilityVerificationProfileDefinition MakeVerificationDefinition(
            VisibilityVerificationProfile profile,
            std::string_view name,
            VisibilityPerformanceProfile implementationProfile,
            const VisibilityPerformanceWorkload& expectedWorkload,
            VisibilityImplementationStatus implementationStatus,
            std::string_view implementationNote = {})
        {
            VisibilityVerificationProfileDefinition definition;
            definition.profile = profile;
            definition.name = name;
            definition.implementationProfile = implementationProfile;
            definition.expectedWorkload = expectedWorkload;
            definition.implementationStatus = implementationStatus;
            definition.implementationNote = implementationNote;
            return definition;
        }
    }

    VisibilityPerformanceProfileConfiguration
        GetVisibilityPerformanceProfileConfiguration(
            VisibilityPerformanceProfile profile)
    {
        using Class = VisibilityOptimizationClass;
        using Trace = VisibilityTraceImplementation;
        using Samples = VisibilitySampleSpecialization;
        using Noise = VisibilityNoiseDelivery;
        using Math = VisibilityMathMode;
        using Storage = VisibilityRawAoStorage;
        using Edge = VisibilityEdgeStorage;
        using Reconstruction = VisibilityReconstructionMode;
        using Temporal = VisibilityTemporalMode;
        using Application = VisibilityApplicationMode;
        using Depth = VisibilityDepthMode;
        using Bindings = VisibilityBindingStrategy;
        using Traversal = VisibilityTraversalOrder;
        using Consumer = VisibilityConsumerRequirement;
        using Estimator = VisibilityEstimatorRequirement;
        using Resolution = VisibilityResolutionRequirement;
        using Status = VisibilityImplementationStatus;

        switch (profile)
        {
        case VisibilityPerformanceProfile::Reference:
            return MakeConfiguration(
                profile, "Reference", Class::Reference,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed8:
            return MakeConfiguration(
                profile, "Exact Fixed 8", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed12:
            return MakeConfiguration(
                profile, "Exact Fixed 12", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed12,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed16:
            return MakeConfiguration(
                profile, "Exact Fixed 16", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed16,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed20:
            return MakeConfiguration(
                profile, "Exact Fixed 20", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed20,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixedLaterBounce8:
            return MakeConfiguration(
                profile, "Exact Fixed Later-Bounce 8", Class::Exact,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Fixed8, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                 Consumer::IncludesIndirectDiffuse, Resolution::Any,
                 false, false);
        case VisibilityPerformanceProfile::ExactFixedAllBounce8:
            return MakeConfiguration(
                profile, "Exact Fixed All-Bounce 8", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Fixed8, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::IncludesIndirectDiffuse, Resolution::Any,
                false, false);
        case VisibilityPerformanceProfile::ExactPackedCurrentFast:
            return MakeConfiguration(
                profile, "Exact Packed Current FAST", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::PackedCurrentFast,
                Math::ReferenceFp32, Storage::R16Float,
                Reconstruction::Legacy, Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::IncludesAmbientOcclusion, Resolution::Any,
                false, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::ExactFusedResolveApply:
            return MakeConfiguration(
                profile, "Exact Fused Resolve And Apply", Class::Exact,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::FusedResolveAndApplyExact,
                Depth::Legacy, Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, true);
        case VisibilityPerformanceProfile::DiagnosticFusedFullResolutionAoOutput:
            return MakeConfiguration(
                profile, "Fused Debug Full-Resolution AO Output",
                Class::Diagnostic,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                true, false);
        case VisibilityPerformanceProfile::ExactGroup16x8:
            return MakeConfiguration(
                profile, "Exact Trace Group 16x8", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                false, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::ExactGroup8x16:
            return MakeConfiguration(
                profile, "Exact Trace Group 8x16", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                false, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::ExactDuplicatePixelRejectionOff:
            return MakeConfiguration(
                profile, "Exact Duplicate Rejection Off", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                true, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::ExactFullMaskEarlyExitOff:
            return MakeConfiguration(
                profile, "Exact Full-Mask Early Exit Off", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                true, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicProjectedRadiusClamp32:
        case VisibilityPerformanceProfile::AlgorithmicProjectedRadiusClamp64:
        case VisibilityPerformanceProfile::AlgorithmicProjectedRadiusClamp128:
            return MakeConfiguration(
                profile,
                profile == VisibilityPerformanceProfile::
                        AlgorithmicProjectedRadiusClamp32
                    ? "Algorithmic Projected Radius Clamp 32"
                    : profile == VisibilityPerformanceProfile::
                            AlgorithmicProjectedRadiusClamp64
                        ? "Algorithmic Projected Radius Clamp 64"
                        : "Algorithmic Projected Radius Clamp 128",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                false, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::DiagnosticConstantScheduler:
            return MakeConfiguration(
                profile, "Diagnostic Constant Scheduler", Class::Diagnostic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::ConstantDiagnostic,
                Math::ReferenceFp32, Storage::R16Float,
                Reconstruction::Legacy, Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                true, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::DiagnosticConstantTrace:
            return MakeConfiguration(
                profile, "Diagnostic Constant Trace", Class::Diagnostic,
                Trace::ConstantDiagnostic, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                true, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::DiagnosticDepthOnlyTrace:
            return MakeConfiguration(
                profile, "Diagnostic Depth-Only Trace", Class::Diagnostic,
                Trace::DepthOnlyDiagnostic, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                true, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::DiagnosticBitmaskOnlyTrace:
            return MakeConfiguration(
                profile, "Diagnostic Bitmask-Only Trace", Class::Diagnostic,
                Trace::BitmaskOnlyDiagnostic, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                true, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::DiagnosticTemporalCopy:
            return MakeConfiguration(
                profile, "Diagnostic Temporal Copy", Class::Diagnostic,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::CopyDiagnostic,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                true, false);
        case VisibilityPerformanceProfile::DiagnosticNearestResolve:
            return MakeConfiguration(
                profile, "Diagnostic Nearest Resolve", Class::Diagnostic,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::NearestDiagnostic,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                true, false);
        case VisibilityPerformanceProfile::DiagnosticBilinearResolve:
            return MakeConfiguration(
                profile, "Diagnostic Bilinear Resolve", Class::Diagnostic,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::BilinearDiagnostic,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                true, false);
        case VisibilityPerformanceProfile::DiagnosticCompositionOnly:
            return MakeConfiguration(
                profile, "Diagnostic Composition Only", Class::Diagnostic,
                Trace::ConstantDiagnostic, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy,
                Application::IsolatedCompositionDiagnostic, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                true, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::DiagnosticCompositionBypass:
            return MakeConfiguration(
                profile, "Diagnostic Composition Bypass", Class::Diagnostic,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy,
                Application::BypassCompositionDiagnostic, Depth::Legacy,
                Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                true, false);
        case VisibilityPerformanceProfile::ConservativeNumerical:
            return MakeConfiguration(
                profile, "Conservative Numerical", Class::Numerical,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy,
                Math::ConservativeNumericalFp32, Storage::R16Float,
                Reconstruction::Legacy, Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Any,
                false, false, Edge::None, Estimator::Any,
                Status::PartialBenchmarkControl,
                "Only the exact-fast FP32 AO filter algebra is compiled; "
                "no mixed-precision trace permutation exists.");
        case VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2:
            return MakeConfiguration(
                profile, "Algorithmic Depth Edges 2x2", Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float,
                Reconstruction::PackedEdges2x2, Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicPackedEdges4x4:
            return MakeConfiguration(
                profile, "Algorithmic Depth Edges 4x4", Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float,
                Reconstruction::PackedEdges4x4, Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesDepthNormal2x2:
            return MakeConfiguration(
                profile, "Algorithmic Depth And Normal Edges 2x2",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::PackedEdges2x2,
                Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicPackedEdgesSlope2x2:
            return MakeConfiguration(
                profile, "Algorithmic Slope-Adjusted Edges 2x2",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::PackedEdges2x2,
                Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicPackedEdgesLeakage2x2:
            return MakeConfiguration(
                profile, "Algorithmic Controlled-Leakage Edges 2x2",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::PackedEdges2x2,
                Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicFusedPackedEdges2x2:
            return MakeConfiguration(
                profile, "Algorithmic Fused Packed Edges 2x2",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::PackedEdges2x2,
                Temporal::Legacy,
                Application::FusedResolveAndApplyPackedEdges,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicFusedPackedEdges4x4:
            return MakeConfiguration(
                profile, "Algorithmic Fused Packed Edges 4x4",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::PackedEdges4x4,
                Temporal::Legacy,
                Application::FusedResolveAndApplyPackedEdges,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicActivisionSchedule:
            return MakeConfiguration(
                profile, "Algorithmic Activision Schedule 8", Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Runtime, Noise::ActivisionInterleavedGradient,
                Math::ReferenceFp32, Storage::R16Float,
                Reconstruction::Legacy, Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Half,
                false, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::ActivisionClosestMatch:
            return MakeConfiguration(
                profile, "UVSR Horizon GTAO Control 8", Class::Algorithmic,
                Trace::ActivisionHorizon, Samples::Fixed8,
                Samples::Runtime, Noise::Legacy,
                Math::ReferenceFp32, Storage::R16Float,
                Reconstruction::Legacy, Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Half,
                true, false, Edge::None,
                Estimator::UniformSolidAngle,
                Status::PartialBenchmarkControl,
                "This is the compiled same-engine analytic-horizon control, "
                "not an Activision PS4 pipeline reproduction.");
        case VisibilityPerformanceProfile::ActivisionPs4Schedule:
            return MakeConfiguration(
                profile, "PS4 GTAO Schedule Control 12", Class::Algorithmic,
                Trace::ActivisionHorizon, Samples::Fixed12,
                Samples::Runtime, Noise::ActivisionInterleavedGradient,
                Math::ReferenceFp32, Storage::R16Float,
                Reconstruction::Legacy, Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Half,
                true, false, Edge::None,
                Estimator::UniformSolidAngle,
                Status::PartialBenchmarkControl,
                "Implements the compiled 12-sample horizon and Activision "
                "4x4-by-6 schedule control; it retains UVSR R16F resolve, "
                "filter, temporal, and composition paths.");
        case VisibilityPerformanceProfile::XeGtaoClosestMatch:
            return MakeConfiguration(
                profile, "XeGTAO Closest Match (Unavailable)", Class::Algorithmic,
                Trace::XeGtaoHorizon, Samples::Fixed8,
                Samples::Runtime, Noise::XeGtaoHilbertR2,
                Math::XeGtaoMixedPrecision, Storage::R8Unorm,
                Reconstruction::XeGtaoDenoise, Temporal::Legacy,
                Application::LegacySeparateComposition,
                Depth::XeGtaoPrefilteredMips,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Half,
                true, false, Edge::None,
                Estimator::UniformSolidAngle, Status::Unavailable,
                "No compiled same-engine XeGTAO path exists: the available "
                "horizon control does not implement XeGTAO Hilbert/R2 noise, "
                "depth prefilter mips, denoiser, or mixed-precision path.");
        case VisibilityPerformanceProfile::GenericFallback:
            return MakeConfiguration(
                profile, "Generic Fallback", Class::Exact,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::R16Float, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        default:
            return {};
        }
    }

    bool IsVisibilityPerformanceProfileFullyAssigned(
        const VisibilityPerformanceProfileConfiguration& configuration)
    {
        return configuration.profile > VisibilityPerformanceProfile::Unset &&
            configuration.profile < VisibilityPerformanceProfile::Count &&
            !configuration.name.empty() &&
            IsAssignedEnum(configuration.optimizationClass,
                VisibilityOptimizationClass::Algorithmic) &&
            IsAssignedEnum(configuration.trace,
                VisibilityTraceImplementation::XeGtaoHorizon) &&
            IsAssignedEnum(configuration.firstBounceSamples,
                VisibilitySampleSpecialization::Fixed20) &&
            IsAssignedEnum(configuration.laterBounceSamples,
                VisibilitySampleSpecialization::Fixed20) &&
            IsAssignedEnum(configuration.noise,
                VisibilityNoiseDelivery::XeGtaoHilbertR2) &&
            IsAssignedEnum(configuration.math,
                VisibilityMathMode::XeGtaoMixedPrecision) &&
            IsAssignedEnum(configuration.rawAoStorage,
                VisibilityRawAoStorage::PackedCountAndEdgesR16Uint) &&
            IsAssignedEnum(configuration.edgeStorage,
                VisibilityEdgeStorage::R8Unorm) &&
            IsAssignedEnum(configuration.reconstruction,
                VisibilityReconstructionMode::XeGtaoDenoise) &&
            IsAssignedEnum(configuration.temporal,
                VisibilityTemporalMode::ActivisionSixDirectionEma) &&
            IsAssignedEnum(configuration.application,
                VisibilityApplicationMode::BypassCompositionDiagnostic) &&
            IsAssignedEnum(configuration.depth,
                VisibilityDepthMode::XeGtaoPrefilteredMips) &&
            IsAssignedEnum(configuration.bindings,
                VisibilityBindingStrategy::MinimalConditional) &&
            IsAssignedEnum(configuration.traversal,
                VisibilityTraversalOrder::GroupedBySide) &&
            IsAssignedEnum(configuration.consumerRequirement,
                VisibilityConsumerRequirement::IncludesIndirectDiffuse) &&
            IsAssignedEnum(configuration.estimatorRequirement,
                VisibilityEstimatorRequirement::CosineWeightedSolidAngle) &&
            IsAssignedEnum(configuration.resolutionRequirement,
                VisibilityResolutionRequirement::Half) &&
            IsAssignedEnum(configuration.implementationStatus,
                VisibilityImplementationStatus::Unavailable) &&
            (configuration.implementationStatus ==
                    VisibilityImplementationStatus::Implemented ||
                !configuration.implementationNote.empty()) &&
            configuration.assignmentMask ==
                VisibilityProfileAllAssignments;
    }

    VisibilityExecutionPlan ResolveVisibilityExecutionPlan(
        VisibilityPerformanceProfile profile,
        const VisibilityPerformanceWorkload& workload)
    {
        return ResolveVisibilityExecutionPlan(
            GetVisibilityPerformanceProfileConfiguration(profile), workload);
    }

    VisibilityExecutionPlan ResolveVisibilityExecutionPlan(
        const VisibilityPerformanceProfileConfiguration& configuration,
        const VisibilityPerformanceWorkload& workload)
    {
        VisibilityExecutionPlan plan;
        plan.configuration = configuration;
        plan.workload = workload;

        const auto reject = [&plan](
            VisibilityPlanError error,
            std::string_view message)
        {
            plan.valid = false;
            plan.error = error;
            plan.errorMessage.assign(message);
            return plan;
        };

        if (!IsVisibilityPerformanceProfileFullyAssigned(configuration))
        {
            return reject(VisibilityPlanError::IncompleteProfile,
                "The performance profile does not assign every field.");
        }
        if (!IsValidWorkload(workload))
        {
            return reject(VisibilityPlanError::InvalidWorkload,
                "The visibility workload contains an invalid enum or count.");
        }
        if (configuration.profile ==
                VisibilityPerformanceProfile::Reference &&
            !HasExactReferenceContract(configuration))
        {
            return reject(VisibilityPlanError::ReferenceContractViolation,
                "Reference must select the unmodified legacy contract.");
        }
        if (configuration.implementationStatus ==
            VisibilityImplementationStatus::Unavailable)
        {
            return reject(
                VisibilityPlanError::ProfileImplementationUnavailable,
                configuration.implementationNote.empty()
                    ? "The selected profile is not implemented."
                    : configuration.implementationNote);
        }

        const bool hasAmbientOcclusion =
            HasAmbientOcclusion(workload.consumer);
        const bool hasIndirectDiffuse =
            HasIndirectDiffuse(workload.consumer);
        const bool reducedResolution =
            IsReducedResolution(workload.resolution);
        const bool usesDepthPreparation =
            workload.depthHierarchyEnabled ||
            configuration.depth == VisibilityDepthMode::XeGtaoPrefilteredMips;

        if (configuration.consumerRequirement ==
                VisibilityConsumerRequirement::AmbientOcclusionOnly &&
            (!hasAmbientOcclusion || hasIndirectDiffuse))
        {
            return reject(VisibilityPlanError::ProfileConsumerMismatch,
                "This profile is valid only for AO-only workloads.");
        }
        if (configuration.consumerRequirement ==
                VisibilityConsumerRequirement::IncludesAmbientOcclusion &&
            !hasAmbientOcclusion)
        {
            return reject(VisibilityPlanError::ProfileConsumerMismatch,
                "This profile requires an AO-producing workload.");
        }
        if (configuration.consumerRequirement ==
                VisibilityConsumerRequirement::IncludesIndirectDiffuse &&
            (!hasIndirectDiffuse || workload.bounceCount < 2u))
        {
            return reject(
                VisibilityPlanError::
                    LaterBounceSpecializationRequiresIndirectDiffuse,
                "This profile requires indirect diffuse with a later bounce.");
        }
        if (configuration.resolutionRequirement ==
                VisibilityResolutionRequirement::Reduced &&
            !reducedResolution)
        {
            return reject(VisibilityPlanError::ProfileResolutionMismatch,
                "This profile requires half or quarter resolution.");
        }
        if (configuration.resolutionRequirement ==
                VisibilityResolutionRequirement::Half &&
            workload.resolution != VisibilityPerformanceResolution::Half)
        {
            return reject(VisibilityPlanError::ProfileResolutionMismatch,
                "This profile requires half resolution.");
        }
        if (!MatchesEstimatorRequirement(
                configuration.estimatorRequirement, workload.estimator))
        {
            return reject(VisibilityPlanError::ProfileEstimatorMismatch,
                "The workload estimator does not match the compiled shader.");
        }
        uint32_t requiredGroupSizeX = 8u;
        uint32_t requiredGroupSizeY = 8u;
        if (configuration.profile ==
            VisibilityPerformanceProfile::ExactGroup16x8)
        {
            requiredGroupSizeX = 16u;
        }
        else if (configuration.profile ==
            VisibilityPerformanceProfile::ExactGroup8x16)
        {
            requiredGroupSizeY = 16u;
        }
        if (workload.threadGroupSizeX != requiredGroupSizeX ||
            workload.threadGroupSizeY != requiredGroupSizeY)
        {
            return reject(VisibilityPlanError::ProfileThreadGroupMismatch,
                "The workload thread-group shape does not match the "
                "compiled profile.");
        }

        const uint32_t fixedFirstCount =
            SpecializedSampleCount(configuration.firstBounceSamples);
        const uint32_t fixedLaterCount =
            SpecializedSampleCount(configuration.laterBounceSamples);
        if (fixedFirstCount != 0u &&
            workload.firstBounceSampleCount != fixedFirstCount)
        {
            return reject(VisibilityPlanError::SampleCountMismatch,
                "The workload does not match the fixed first-bounce count.");
        }
        if (fixedLaterCount != 0u &&
            workload.laterBounceSampleCount != fixedLaterCount)
        {
            return reject(VisibilityPlanError::SampleCountMismatch,
                "The workload does not match the fixed later-bounce count.");
        }
        if ((fixedFirstCount != 0u || fixedLaterCount != 0u) &&
            workload.radialExponent != 2.0f)
        {
            return reject(VisibilityPlanError::FixedExponentMismatch,
                "The fixed shader is compiled for a quadratic radial exponent.");
        }
        if ((fixedFirstCount != 0u || fixedLaterCount != 0u) &&
            workload.adaptiveSamplingEnabled)
        {
            return reject(VisibilityPlanError::AdaptiveFixedCountConflict,
                "A fixed-count shader cannot use adaptive sample counts.");
        }
        if (fixedLaterCount != 0u &&
            (!hasIndirectDiffuse || workload.bounceCount < 2u))
        {
            return reject(
                VisibilityPlanError::
                    LaterBounceSpecializationRequiresIndirectDiffuse,
                "A fixed later-bounce shader requires a later GI bounce.");
        }
        if (configuration.noise == VisibilityNoiseDelivery::PackedCurrentFast &&
            workload.scheduler != VisibilityPerformanceScheduler::
                FilterAdaptedSpatiotemporalRankField)
        {
            return reject(VisibilityPlanError::ProfileSchedulerMismatch,
                "Packed Current FAST requires the current FAST scheduler.");
        }
        if (configuration.noise == VisibilityNoiseDelivery::Legacy &&
            (workload.scheduler ==
                    VisibilityPerformanceScheduler::Activision4x4SixPhase ||
                workload.scheduler ==
                    VisibilityPerformanceScheduler::XeGtaoHilbertR2 ||
                workload.scheduler ==
                    VisibilityPerformanceScheduler::ConstantDiagnostic))
        {
            return reject(VisibilityPlanError::ProfileSchedulerMismatch,
                "The legacy shader supports only Independent, Toroidal, or "
                "current scalar FAST scheduling.");
        }
        if (configuration.noise ==
                VisibilityNoiseDelivery::ConstantDiagnostic &&
            workload.scheduler !=
                VisibilityPerformanceScheduler::ConstantDiagnostic)
        {
            return reject(VisibilityPlanError::ProfileSchedulerMismatch,
                "The constant-scheduler diagnostic requires its fixed "
                "deterministic scheduler.");
        }
        if (configuration.noise ==
                VisibilityNoiseDelivery::ActivisionInterleavedGradient &&
            workload.scheduler !=
                VisibilityPerformanceScheduler::Activision4x4SixPhase)
        {
            return reject(VisibilityPlanError::ProfileSchedulerMismatch,
                "The Activision schedule requires its 4x4-by-6 scheduler.");
        }
        if (configuration.noise == VisibilityNoiseDelivery::XeGtaoHilbertR2 &&
            workload.scheduler !=
                VisibilityPerformanceScheduler::XeGtaoHilbertR2)
        {
            return reject(VisibilityPlanError::ProfileSchedulerMismatch,
                "The XeGTAO profile requires its Hilbert/R2 scheduler.");
        }
        if ((configuration.temporal ==
                    VisibilityTemporalMode::CopyDiagnostic ||
                configuration.temporal ==
                    VisibilityTemporalMode::ActivisionSixDirectionEma) &&
            !workload.temporalEnabled)
        {
            return reject(
                VisibilityPlanError::TemporalModeRequiresHistory,
                "The selected temporal mode requires temporal history.");
        }

        const bool encodedAo = configuration.rawAoStorage !=
            VisibilityRawAoStorage::R16Float;
        if (encodedAo && (!hasAmbientOcclusion ||
                workload.estimator != VisibilityPerformanceEstimator::
                    UniformProjectedAngle))
        {
            return reject(VisibilityPlanError::UnsupportedAoEncoding,
                "R8 and packed-count AO are valid only for the bounded "
                "Uniform Projected Angle estimator.");
        }
        if (configuration.rawAoStorage == VisibilityRawAoStorage::R8Unorm)
        {
            return reject(
                VisibilityPlanError::ProfileImplementationUnavailable,
                "Raw R8_UNORM AO is mathematically permitted for this "
                "estimator, but no compiled trace permutation writes it.");
        }
        if (configuration.rawAoStorage ==
            VisibilityRawAoStorage::PackedCountAndEdgesR16Uint)
        {
            return reject(
                VisibilityPlanError::ProfileImplementationUnavailable,
                "Packed R16_UINT AO plus edges is modeled as an experiment, "
                "but no compiled trace or resolve permutation implements it.");
        }
        if (configuration.edgeStorage == VisibilityEdgeStorage::R8Unorm)
        {
            return reject(
                VisibilityPlanError::ProfileImplementationUnavailable,
                "R8_UNORM edge metadata has no compiled writer or resolve; "
                "the implemented separate edge texture is R8_UINT.");
        }
        const bool packedAo = configuration.rawAoStorage ==
            VisibilityRawAoStorage::PackedCountAndEdgesR16Uint;
        const bool packedReconstruction =
            configuration.reconstruction ==
                VisibilityReconstructionMode::PackedEdges2x2 ||
            configuration.reconstruction ==
                VisibilityReconstructionMode::PackedEdges4x4;
        const bool separatePackedEdges =
            configuration.edgeStorage == VisibilityEdgeStorage::R8Uint ||
            configuration.edgeStorage == VisibilityEdgeStorage::R8Unorm;
        const bool validEmbeddedPacking = packedAo &&
            packedReconstruction && !separatePackedEdges;
        const bool validSeparatePacking = !packedAo &&
            configuration.rawAoStorage == VisibilityRawAoStorage::R16Float &&
            packedReconstruction && separatePackedEdges;
        const bool noPacking = !packedAo && !packedReconstruction &&
            !separatePackedEdges;
        if ((!validEmbeddedPacking && !validSeparatePacking && !noPacking) ||
            (packedAo && workload.temporalEnabled))
        {
            return reject(VisibilityPlanError::InvalidPackedReconstruction,
                "Packed reconstruction requires either embedded R16_UINT "
                "count/edges or R16F AO plus a separate R8 edge buffer.");
        }
        if (packedReconstruction && workload.spatialEnabled)
        {
            return reject(
                VisibilityPlanError::
                    PackedReconstructionDoesNotSupportSpatialFilter,
                "Packed-edge reconstruction replaces the optional legacy "
                "spatial filter; the two modes cannot be enabled together.");
        }

        if (hasIndirectDiffuse && configuration.traversal !=
                VisibilityTraversalOrder::
                    InterleavedNegativePositiveNearToFar)
        {
            return reject(
                VisibilityPlanError::IndirectDiffuseTraversalReordered,
                "GI must retain interleaved negative/positive near-to-far ownership.");
        }

        const bool exactFusedApplication = configuration.application ==
            VisibilityApplicationMode::FusedResolveAndApplyExact;
        const bool packedFusedApplication = configuration.application ==
            VisibilityApplicationMode::FusedResolveAndApplyPackedEdges;
        if (exactFusedApplication || packedFusedApplication)
        {
            if (!hasAmbientOcclusion || hasIndirectDiffuse)
            {
                return reject(
                    VisibilityPlanError::FusedApplicationRequiresAoOnly,
                    "Fused resolve/apply is valid only for AO-only.");
            }
            if (!reducedResolution)
            {
                return reject(VisibilityPlanError::
                        FusedApplicationRequiresReducedResolution,
                    "Fused resolve/apply requires a reduced-resolution source.");
            }
            if (workload.spatialEnabled)
            {
                return reject(VisibilityPlanError::
                        FusedApplicationDoesNotSupportSpatialFilter,
                    "Fused resolve/apply implements its selected reconstruction "
                    "kernel, not the optional legacy spatial filter.");
            }
        }
        if (exactFusedApplication)
        {
            if (!configuration.explicitHalfRoundtrip ||
                configuration.rawAoStorage !=
                    VisibilityRawAoStorage::R16Float ||
                configuration.reconstruction !=
                    VisibilityReconstructionMode::Legacy)
            {
                return reject(
                    VisibilityPlanError::FusedApplicationRequiresHalfRoundtrip,
                    "Exact fusion must preserve the eliminated R16F roundtrip explicitly.");
            }
        }
        else if (packedFusedApplication)
        {
            if (configuration.explicitHalfRoundtrip ||
                !validSeparatePacking)
            {
                return reject(
                    VisibilityPlanError::FusedApplicationRequiresPackedEdges,
                    "Packed-edge fusion requires R16F AO, a separate R8 edge "
                    "buffer, and a packed 2x2 or 4x4 reconstruction mode.");
            }
        }
        else if (configuration.explicitHalfRoundtrip)
        {
            return reject(
                VisibilityPlanError::FusedApplicationRequiresHalfRoundtrip,
                "An explicit resolve roundtrip is meaningful only for exact fusion.");
        }

        const bool horizonTrace =
            configuration.trace ==
                VisibilityTraceImplementation::ActivisionHorizon ||
            configuration.trace ==
                VisibilityTraceImplementation::XeGtaoHorizon;
        if (horizonTrace && (!configuration.benchmarkOnly ||
                !hasAmbientOcclusion || hasIndirectDiffuse))
        {
            return reject(
                VisibilityPlanError::BenchmarkProfileContractViolation,
                "Horizon GTAO is an AO-only benchmark and cannot replace the bitmask product.");
        }

        plan.selectsLegacyReference = configuration.profile ==
            VisibilityPerformanceProfile::Reference;
        const bool usesLegacyBroadPipeline =
            configuration.bindings == VisibilityBindingStrategy::LegacyBroad;
        plan.preservesProductionBitmask =
            configuration.trace ==
                VisibilityTraceImplementation::LegacyGenericBitmask ||
            configuration.trace ==
                VisibilityTraceImplementation::FixedInterleavedBitmask;
        plan.benchmarkOnly = configuration.benchmarkOnly;
        plan.requiresExplicitHalfRoundtrip =
            configuration.explicitHalfRoundtrip;
        plan.fixedFirstBounceSampleCount = fixedFirstCount;
        plan.fixedLaterBounceSampleCount = fixedLaterCount;

        const bool constantOrBitmaskDiagnostic =
            configuration.trace ==
                VisibilityTraceImplementation::ConstantDiagnostic ||
            configuration.trace ==
                VisibilityTraceImplementation::BitmaskOnlyDiagnostic;
        const bool depthDiagnostic = configuration.trace ==
            VisibilityTraceImplementation::DepthOnlyDiagnostic;
        const bool traceUsesScheduler = !constantOrBitmaskDiagnostic;
        const bool schedulerTextureBound = traceUsesScheduler &&
            (configuration.noise ==
                    VisibilityNoiseDelivery::PackedCurrentFast ||
                (configuration.noise == VisibilityNoiseDelivery::Legacy &&
                    (workload.scheduler ==
                            VisibilityPerformanceScheduler::
                                ToroidalBlueNoiseRankField ||
                        workload.scheduler ==
                            VisibilityPerformanceScheduler::
                                FilterAdaptedSpatiotemporalRankField)));

        if (usesLegacyBroadPipeline)
        {
            // The legacy shader layout is intentionally broad for every
            // consumer permutation, including resources compiled out by a
            // particular shader.
            plan.firstTraceSrvCount = 8u;
            plan.firstTraceUavCount = 3u;
        }
        else if (constantOrBitmaskDiagnostic)
        {
            plan.firstTraceSrvCount = 0u;
            plan.firstTraceUavCount = 1u;
        }
        else if (depthDiagnostic)
        {
            // t0/t1 preserve the normal trace's receiver inputs; t3 preserves
            // its matched sample-depth address stream. Noise is a fourth SRV
            // only for texture-backed scheduler specializations.
            plan.firstTraceSrvCount = 3u +
                (schedulerTextureBound ? 1u : 0u);
            plan.firstTraceUavCount = 1u;
        }
        else
        {
            plan.firstTraceSrvCount = 2u +
                (hasIndirectDiffuse ? 1u : 0u) +
                (usesDepthPreparation ? 1u : 0u) +
                (schedulerTextureBound ? 1u : 0u);
            plan.firstTraceUavCount =
                (hasAmbientOcclusion ? 1u : 0u) +
                (hasIndirectDiffuse ? 1u : 0u) +
                (configuration.edgeStorage != VisibilityEdgeStorage::None
                    ? 1u : 0u);
        }

        if (hasAmbientOcclusion)
        {
            switch (configuration.rawAoStorage)
            {
            case VisibilityRawAoStorage::R16Float:
                plan.resourceMask |=
                    Bit(VisibilityExecutionResource::RawAmbientR16);
                break;
            case VisibilityRawAoStorage::R8Unorm:
                plan.resourceMask |=
                    Bit(VisibilityExecutionResource::RawAmbientR8);
                break;
            case VisibilityRawAoStorage::PackedCountAndEdgesR16Uint:
                plan.resourceMask |= Bit(VisibilityExecutionResource::
                    RawAmbientPackedCountEdgesR16);
                break;
            default:
                break;
            }
            switch (configuration.edgeStorage)
            {
            case VisibilityEdgeStorage::R8Uint:
                plan.resourceMask |=
                    Bit(VisibilityExecutionResource::PackedEdgesR8Uint);
                break;
            case VisibilityEdgeStorage::R8Unorm:
                plan.resourceMask |=
                    Bit(VisibilityExecutionResource::PackedEdgesR8Unorm);
                break;
            default:
                break;
            }
        }
        if (hasIndirectDiffuse)
        {
            plan.resourceMask |=
                Bit(VisibilityExecutionResource::RawIndirectRgba16);
            if (workload.bounceCount > 1u)
            {
                plan.resourceMask |= Bit(
                    VisibilityExecutionResource::CumulativeIndirectRgba16);
            }
        }

        if (workload.temporalEnabled)
        {
            plan.resourceMask |=
                Bit(VisibilityExecutionResource::TemporalDepthR32) |
                Bit(VisibilityExecutionResource::TemporalNormalRgba8);
            if (hasAmbientOcclusion)
            {
                plan.resourceMask |= configuration.rawAoStorage ==
                        VisibilityRawAoStorage::R8Unorm
                    ? Bit(VisibilityExecutionResource::TemporalAmbientR8)
                    : Bit(VisibilityExecutionResource::TemporalAmbientR16);
            }
            if (hasIndirectDiffuse)
            {
                plan.resourceMask |= Bit(
                    VisibilityExecutionResource::TemporalIndirectRgba16);
            }
        }

        const bool needsReconstruction = workload.spatialEnabled ||
            reducedResolution || configuration.reconstruction !=
                VisibilityReconstructionMode::Legacy;
        const bool fusedApplication = exactFusedApplication ||
            packedFusedApplication;
        if (needsReconstruction && !fusedApplication)
        {
            if (hasAmbientOcclusion)
            {
                plan.resourceMask |= configuration.rawAoStorage ==
                        VisibilityRawAoStorage::R8Unorm
                    ? Bit(VisibilityExecutionResource::FinalAmbientR8)
                    : Bit(VisibilityExecutionResource::FinalAmbientR16);
            }
            if (hasIndirectDiffuse)
            {
                plan.resourceMask |= Bit(
                    VisibilityExecutionResource::FinalIndirectRgba16);
            }
        }

        if (usesDepthPreparation)
        {
            plan.resourceMask |=
                Bit(VisibilityExecutionResource::DepthHierarchy);
        }
        if (usesLegacyBroadPipeline)
        {
            // The legacy pass constructs and binds both assets eagerly.
            plan.resourceMask |=
                Bit(VisibilityExecutionResource::LegacyToroidalNoise) |
                Bit(VisibilityExecutionResource::LegacyCurrentFastNoise);
        }
        else if (traceUsesScheduler &&
            configuration.noise == VisibilityNoiseDelivery::Legacy)
        {
            if (workload.scheduler == VisibilityPerformanceScheduler::
                ToroidalBlueNoiseRankField)
            {
                plan.resourceMask |=
                    Bit(VisibilityExecutionResource::LegacyToroidalNoise);
            }
            else if (workload.scheduler == VisibilityPerformanceScheduler::
                FilterAdaptedSpatiotemporalRankField)
            {
                plan.resourceMask |=
                    Bit(VisibilityExecutionResource::LegacyCurrentFastNoise);
            }
        }
        else if (traceUsesScheduler && configuration.noise ==
            VisibilityNoiseDelivery::PackedCurrentFast)
        {
            plan.resourceMask |=
                Bit(VisibilityExecutionResource::PackedCurrentFastNoise);
        }

        if (usesLegacyBroadPipeline)
        {
            // Broad layouts bind dummy resources for inactive consumers. This
            // deliberately models the current contract instead of minimizing it.
            plan.bindingMask =
                Bit(VisibilityExecutionBinding::Depth) |
                Bit(VisibilityExecutionBinding::Normals) |
                Bit(VisibilityExecutionBinding::MotionVectors) |
                Bit(VisibilityExecutionBinding::SourceRadiance) |
                Bit(VisibilityExecutionBinding::GBufferMaterial) |
                Bit(VisibilityExecutionBinding::BaseLighting) |
                Bit(VisibilityExecutionBinding::OutputLighting) |
                Bit(VisibilityExecutionBinding::LegacyToroidalNoise) |
                Bit(VisibilityExecutionBinding::LegacyCurrentFastNoise) |
                Bit(VisibilityExecutionBinding::DepthHierarchy) |
                Bit(VisibilityExecutionBinding::AmbientHistory) |
                Bit(VisibilityExecutionBinding::IndirectHistory) |
                Bit(VisibilityExecutionBinding::AmbientOutput) |
                Bit(VisibilityExecutionBinding::IndirectOutput);
        }
        else
        {
            plan.bindingMask =
                Bit(VisibilityExecutionBinding::Depth) |
                Bit(VisibilityExecutionBinding::Normals);
            if (workload.adaptiveSamplingEnabled || workload.temporalEnabled)
            {
                plan.bindingMask |=
                    Bit(VisibilityExecutionBinding::MotionVectors);
            }
            if (hasAmbientOcclusion)
            {
                plan.bindingMask |=
                    Bit(VisibilityExecutionBinding::AmbientOutput);
            }
            if (hasIndirectDiffuse)
            {
                plan.bindingMask |=
                    Bit(VisibilityExecutionBinding::SourceRadiance) |
                    Bit(VisibilityExecutionBinding::GBufferMaterial) |
                    Bit(VisibilityExecutionBinding::IndirectOutput);
            }
            if (configuration.application !=
                VisibilityApplicationMode::BypassCompositionDiagnostic)
            {
                plan.bindingMask |=
                    Bit(VisibilityExecutionBinding::BaseLighting) |
                    Bit(VisibilityExecutionBinding::OutputLighting) |
                    Bit(VisibilityExecutionBinding::GBufferMaterial);
            }
            if (workload.temporalEnabled)
            {
                if (hasAmbientOcclusion)
                {
                    plan.bindingMask |=
                        Bit(VisibilityExecutionBinding::AmbientHistory);
                }
                if (hasIndirectDiffuse)
                {
                    plan.bindingMask |=
                        Bit(VisibilityExecutionBinding::IndirectHistory);
                }
            }
            if (usesDepthPreparation)
            {
                plan.bindingMask |=
                    Bit(VisibilityExecutionBinding::DepthHierarchy);
            }
            if (HasVisibilityExecutionResource(plan.resourceMask,
                    VisibilityExecutionResource::LegacyToroidalNoise))
            {
                plan.bindingMask |=
                    Bit(VisibilityExecutionBinding::LegacyToroidalNoise);
            }
            if (HasVisibilityExecutionResource(plan.resourceMask,
                    VisibilityExecutionResource::LegacyCurrentFastNoise))
            {
                plan.bindingMask |=
                    Bit(VisibilityExecutionBinding::LegacyCurrentFastNoise);
            }
            if (HasVisibilityExecutionResource(plan.resourceMask,
                    VisibilityExecutionResource::PackedCurrentFastNoise))
            {
                plan.bindingMask |=
                    Bit(VisibilityExecutionBinding::PackedCurrentFastNoise);
            }
            if (HasVisibilityExecutionResource(plan.resourceMask,
                    VisibilityExecutionResource::PackedEdgesR8Uint) ||
                HasVisibilityExecutionResource(plan.resourceMask,
                    VisibilityExecutionResource::PackedEdgesR8Unorm))
            {
                plan.bindingMask |=
                    Bit(VisibilityExecutionBinding::PackedEdges);
            }
        }

        if (usesDepthPreparation)
        {
            plan.passMask |=
                Bit(VisibilityExecutionPass::DepthPreparation);
        }
        if (usesLegacyBroadPipeline)
        {
            plan.passMask |= Bit(VisibilityExecutionPass::LegacyTrace);
        }
        else
        {
            switch (configuration.trace)
            {
            case VisibilityTraceImplementation::LegacyGenericBitmask:
                plan.passMask |=
                    Bit(VisibilityExecutionPass::CandidateGenericTrace);
                break;
            case VisibilityTraceImplementation::FixedInterleavedBitmask:
                plan.passMask |= Bit(VisibilityExecutionPass::FixedTrace);
                break;
            case VisibilityTraceImplementation::ConstantDiagnostic:
            case VisibilityTraceImplementation::DepthOnlyDiagnostic:
            case VisibilityTraceImplementation::BitmaskOnlyDiagnostic:
                plan.passMask |=
                    Bit(VisibilityExecutionPass::DiagnosticTrace);
                break;
            case VisibilityTraceImplementation::ActivisionHorizon:
                plan.passMask |=
                    Bit(VisibilityExecutionPass::ActivisionHorizonTrace);
                break;
            case VisibilityTraceImplementation::XeGtaoHorizon:
                plan.passMask |=
                    Bit(VisibilityExecutionPass::XeGtaoHorizonTrace);
                break;
            default:
                break;
            }
        }
        if (hasIndirectDiffuse && workload.bounceCount > 1u)
        {
            plan.passMask |= fixedLaterCount != 0u
                ? Bit(VisibilityExecutionPass::FixedLaterBounceTrace)
                : Bit(VisibilityExecutionPass::LegacyLaterBounceTrace);
        }
        if (workload.temporalEnabled)
        {
            plan.passMask |= Bit(VisibilityExecutionPass::Temporal);
        }
        if (fusedApplication)
        {
            plan.passMask |=
                Bit(VisibilityExecutionPass::FusedResolveAndApply);
        }
        else
        {
            if (needsReconstruction)
            {
                plan.passMask |=
                    Bit(VisibilityExecutionPass::Reconstruction);
            }
            if (configuration.application == VisibilityApplicationMode::
                BypassCompositionDiagnostic)
            {
                plan.passMask |=
                    Bit(VisibilityExecutionPass::CompositionBypass);
            }
            else
            {
                plan.passMask |=
                    Bit(VisibilityExecutionPass::Composition);
            }
        }

        plan.optionalResourceMask =
            plan.resourceMask & VisibilityOptionalResourceMask;
        plan.candidateBindingMask =
            plan.bindingMask & VisibilityCandidateBindingMask;
        plan.candidatePassMask =
            plan.passMask & VisibilityCandidatePassMask;

        uint64_t dispatchedPasses = plan.passMask &
            ~Bit(VisibilityExecutionPass::CompositionBypass);
        plan.dispatchCount = CountBits(dispatchedPasses);
        if (hasIndirectDiffuse && workload.bounceCount > 2u)
        {
            // The pass bit represents all later bounces; count the remaining
            // dispatches after the one already represented by that bit.
            plan.dispatchCount += workload.bounceCount - 2u;
        }

        plan.peakSrvCount = plan.firstTraceSrvCount;
        plan.peakUavCount = plan.firstTraceUavCount;
        auto includeLayout = [&plan](uint32_t srvCount, uint32_t uavCount)
        {
            plan.peakSrvCount = std::max(plan.peakSrvCount, srvCount);
            plan.peakUavCount = std::max(plan.peakUavCount, uavCount);
        };
        if (usesDepthPreparation)
            includeLayout(1u, 5u);
        if (hasIndirectDiffuse && workload.bounceCount > 1u)
        {
            includeLayout(fixedLaterCount != 0u
                    ? 6u + (schedulerTextureBound ? 1u : 0u)
                    : 11u,
                2u);
        }
        if (workload.temporalEnabled)
        {
            includeLayout(configuration.temporal ==
                    VisibilityTemporalMode::CopyDiagnostic
                    ? 3u : 9u,
                configuration.temporal ==
                    VisibilityTemporalMode::CopyDiagnostic
                    ? 3u : 4u);
        }
        if (fusedApplication)
        {
            includeLayout(8u, 1u);
        }
        else
        {
            if (needsReconstruction)
            {
                if (packedReconstruction)
                    includeLayout(2u, 1u);
                else if (configuration.math ==
                    VisibilityMathMode::ConservativeNumericalFp32)
                    includeLayout(3u, 1u);
                else if (configuration.reconstruction ==
                        VisibilityReconstructionMode::NearestDiagnostic ||
                    configuration.reconstruction ==
                        VisibilityReconstructionMode::BilinearDiagnostic)
                    includeLayout(1u, 1u);
                else
                    includeLayout(4u, 2u);
            }
            if (configuration.application !=
                VisibilityApplicationMode::BypassCompositionDiagnostic)
            {
                includeLayout(8u, 1u);
            }
        }

        if (plan.selectsLegacyReference &&
            (plan.optionalResourceMask != 0u ||
                plan.candidateBindingMask != 0u ||
                plan.candidatePassMask != 0u))
        {
            return reject(VisibilityPlanError::ReferenceContractViolation,
                "Reference unexpectedly selected an optional candidate cost.");
        }

        plan.permutationName =
            MakePermutationName(configuration, workload);
        plan.shaderPermutationKey =
            MakeShaderPermutationKey(configuration, workload, plan);
        plan.permutationKey =
            MakePermutationKey(configuration, workload, plan);
        plan.historyResetKey = MakeHistoryResetKey(
            configuration, workload, plan.permutationKey);
        plan.valid = true;
        plan.error = VisibilityPlanError::None;
        plan.errorMessage.clear();
        return plan;
    }

    VisibilityVerificationProfileDefinition
        GetVisibilityVerificationProfileDefinition(
            VisibilityVerificationProfile profile)
    {
        using Consumer = VisibilityPerformanceConsumer;
        using Implementation = VisibilityPerformanceProfile;
        using Scheduler = VisibilityPerformanceScheduler;
        using Status = VisibilityImplementationStatus;

        VisibilityPerformanceWorkload workload =
            MakeTargetVerificationWorkload();
        switch (profile)
        {
        case VisibilityVerificationProfile::ReferenceAo8T:
            return MakeVerificationDefinition(profile, "Reference AO 8T",
                Implementation::Reference, workload, Status::Implemented);
        case VisibilityVerificationProfile::ExactFastAo8T:
            workload.scheduler =
                Scheduler::FilterAdaptedSpatiotemporalRankField;
            return MakeVerificationDefinition(profile, "Exact-Fast AO 8T",
                Implementation::ExactPackedCurrentFast, workload,
                Status::Implemented);
        case VisibilityVerificationProfile::MixedPrecisionAo8T:
            return MakeVerificationDefinition(
                profile, "Mixed-Precision AO 8T",
                Implementation::ConservativeNumerical, workload,
                Status::Unavailable,
                "No mixed-precision trace permutation is compiled; the "
                "conservative candidate only changes FP32 filter algebra.");
        case VisibilityVerificationProfile::PackedEdgeAo8T:
            return MakeVerificationDefinition(profile, "Packed-Edge AO 8T",
                Implementation::AlgorithmicPackedEdges4x4, workload,
                Status::Implemented,
                "Uses R16F raw AO plus a separate R8_UINT edge texture.");
        case VisibilityVerificationProfile::ActivisionScheduleAo8T:
            workload.scheduler = Scheduler::Activision4x4SixPhase;
            return MakeVerificationDefinition(
                profile, "Activision-Schedule AO 8T",
                Implementation::AlgorithmicActivisionSchedule, workload,
                Status::Implemented,
                "Changes only the sample schedule; the 32-sector UVSR "
                "bitmask estimator and legacy resolve remain active.");
        case VisibilityVerificationProfile::ReferenceAoGi8T:
            workload.consumer =
                Consumer::AmbientOcclusionAndIndirectDiffuse;
            return MakeVerificationDefinition(profile, "Reference AO+GI 8T",
                Implementation::Reference, workload, Status::Implemented);
        case VisibilityVerificationProfile::ExactFastAoGi8T:
            workload.consumer =
                Consumer::AmbientOcclusionAndIndirectDiffuse;
            workload.scheduler =
                Scheduler::FilterAdaptedSpatiotemporalRankField;
            return MakeVerificationDefinition(
                profile, "Exact-Fast AO+GI 8T",
                Implementation::ExactPackedCurrentFast, workload,
                Status::Implemented);
        case VisibilityVerificationProfile::ExactFastAoGi12T:
            workload.consumer =
                Consumer::AmbientOcclusionAndIndirectDiffuse;
            workload.firstBounceSampleCount = 12u;
            return MakeVerificationDefinition(
                profile, "Exact-Fast AO+GI 12T",
                Implementation::ExactFixed12, workload,
                Status::PartialBenchmarkControl,
                "The exact fixed-12 trace is compiled; packed FAST is only "
                "compiled for the fixed-8 candidate.");
        case VisibilityVerificationProfile::ExactFastAoGi16T:
            workload.consumer =
                Consumer::AmbientOcclusionAndIndirectDiffuse;
            workload.firstBounceSampleCount = 16u;
            return MakeVerificationDefinition(
                profile, "Exact-Fast AO+GI 16T",
                Implementation::ExactFixed16, workload,
                Status::PartialBenchmarkControl,
                "The exact fixed-16 trace is compiled; packed FAST is only "
                "compiled for the fixed-8 candidate.");
        case VisibilityVerificationProfile::ExactFastMultiBounce:
            workload.consumer =
                Consumer::AmbientOcclusionAndIndirectDiffuse;
            workload.bounceCount = 2u;
            return MakeVerificationDefinition(
                profile, "Exact-Fast Multi-Bounce",
                Implementation::ExactFixedAllBounce8, workload,
                Status::PartialBenchmarkControl,
                "The first and later traces use exact fixed-8 shaders; "
                "packed FAST and fused application are not compiled for the "
                "multi-bounce path.");
        case VisibilityVerificationProfile::AggressiveExperimentalAo8T:
            return MakeVerificationDefinition(
                profile, "Aggressive Experimental AO 8T",
                Implementation::Unset, workload, Status::Unavailable,
                "No aggressive mixed-precision or compact-AO permutation is "
                "compiled, so this preset cannot honestly select one.");
        case VisibilityVerificationProfile::XeGtaoClosestMatch:
            workload.scheduler = Scheduler::XeGtaoHilbertR2;
            return MakeVerificationDefinition(
                profile, "XeGTAO Closest Match",
                Implementation::XeGtaoClosestMatch, workload,
                Status::Unavailable,
                "The compiled analytic-horizon control is not XeGTAO: it "
                "lacks Hilbert/R2 noise, depth prefilter mips, the XeGTAO "
                "denoiser, and its mixed-precision path.");
        case VisibilityVerificationProfile::Ps4GtaoClosestMatch:
            workload.firstBounceSampleCount = 12u;
            workload.scheduler = Scheduler::Activision4x4SixPhase;
            return MakeVerificationDefinition(
                profile, "PS4 GTAO Closest Match",
                Implementation::ActivisionPs4Schedule, workload,
                Status::PartialBenchmarkControl,
                "This control implements the 12-sample analytic horizon and "
                "4x4-by-6 schedule, but retains UVSR's R16F resolve, filter, "
                "temporal, and composition paths.");
        default:
            return {};
        }
    }

    VisibilityVerificationProfileResolution
        ResolveVisibilityVerificationProfile(
            VisibilityVerificationProfile profile,
            const VisibilityPerformanceWorkload& observedWorkload)
    {
        const VisibilityVerificationProfileDefinition definition =
            GetVisibilityVerificationProfileDefinition(profile);
        return ResolveVisibilityVerificationProfile(
            profile, definition.implementationProfile, observedWorkload);
    }

    VisibilityVerificationProfileResolution
        ResolveVisibilityVerificationProfile(
            VisibilityVerificationProfile profile,
            VisibilityPerformanceProfile observedImplementationProfile,
            const VisibilityPerformanceWorkload& observedWorkload)
    {
        VisibilityVerificationProfileResolution result;
        result.definition =
            GetVisibilityVerificationProfileDefinition(profile);
        if (result.definition.profile == VisibilityVerificationProfile::Unset)
        {
            result.reason = "The requested verification profile is invalid.";
            return result;
        }
        if (result.definition.implementationStatus ==
            VisibilityImplementationStatus::Unavailable)
        {
            result.reason.assign(result.definition.implementationNote);
            return result;
        }
        if (observedImplementationProfile !=
            result.definition.implementationProfile)
        {
            result.reason =
                "Implementation profile does not match the preset.";
            return result;
        }

        result.reason = FindVerificationWorkloadMismatch(
            result.definition.expectedWorkload, observedWorkload);
        if (!result.reason.empty())
            return result;

        result.executionPlan = ResolveVisibilityExecutionPlan(
            result.definition.implementationProfile, observedWorkload);
        if (!result.executionPlan.valid)
        {
            result.reason = result.executionPlan.errorMessage;
            return result;
        }

        result.valid = true;
        if (result.definition.implementationStatus ==
            VisibilityImplementationStatus::PartialBenchmarkControl)
        {
            result.reason = "Valid partial benchmark control: ";
            result.reason.append(result.definition.implementationNote);
        }
        else if (!result.definition.implementationNote.empty())
        {
            result.reason = "Valid: ";
            result.reason.append(result.definition.implementationNote);
        }
        else
        {
            result.reason = "Valid: all expected profile values match.";
        }
        return result;
    }

    bool TryPackVisibilityCountAndEdges(
        uint32_t occludedSectorCount,
        uint8_t packedEdges,
        uint16_t& packedValue) noexcept
    {
        if (occludedSectorCount > 32u)
            return false;

        packedValue = uint16_t(occludedSectorCount) |
            uint16_t(uint16_t(packedEdges) << 6u);
        return true;
    }
}
