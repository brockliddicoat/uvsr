#include "visibility_performance_plan.h"

#include <cmath>
#include <cstring>
#include <sstream>

namespace uvsr
{
    namespace
    {
        constexpr uint32_t c_MaximumRadialSampleCount = 64u;
        constexpr uint32_t c_MaximumBounceCount = 16u;
        constexpr uint32_t c_MaximumExplicitBounceCount = 8u;
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
            case VisibilitySampleSpecialization::Fixed24:
                return 24u;
            case VisibilitySampleSpecialization::Fixed48:
                return 48u;
            case VisibilitySampleSpecialization::Fixed64:
                return 64u;
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
                    FilterAdaptedSpatiotemporalRankField;
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
                validScheduler && validCounts && validGeometry;
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
                << "/temporal=" << (workload.temporalEnabled ? 1 : 0)
                << "/spatial=" << (workload.spatialEnabled ? 1 : 0)
                << "/hierarchy=" << (workload.depthHierarchyEnabled ? 1 : 0)
                << "/runtime-config=" << workload.runtimeConfigurationKey;
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
            HashBool(hash, workload.temporalEnabled);
            HashBool(hash, workload.spatialEnabled);
            HashBool(hash, workload.depthHierarchyEnabled);
            HashUint64(hash, workload.runtimeConfigurationKey);
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
            HashBool(hash, workload.temporalEnabled);
            HashBool(hash, workload.spatialEnabled);
            HashBool(hash, workload.depthHierarchyEnabled);
            HashUint64(hash, workload.runtimeConfigurationKey);
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
            // accumulated history.
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
            HashBool(hash, workload.temporalEnabled);
            HashBool(hash, workload.spatialEnabled);
            HashUint64(hash, workload.runtimeConfigurationKey);
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
                Storage::ScalarFloat, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed8:
            return MakeConfiguration(
                profile, "Exact Fixed 8", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Fixed8, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed12:
            return MakeConfiguration(
                profile, "Exact Fixed 12", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed12,
                Samples::Fixed12, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed16:
            return MakeConfiguration(
                profile, "Exact Fixed 16", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed16,
                Samples::Fixed16, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed20:
            return MakeConfiguration(
                profile, "Exact Fixed 20", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed20,
                Samples::Fixed20, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed24:
            return MakeConfiguration(
                profile, "Exact Fixed 24", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed24,
                Samples::Fixed24, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed48:
            return MakeConfiguration(
                profile, "Exact Fixed 48", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed48,
                Samples::Fixed48, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any, false, false);
        case VisibilityPerformanceProfile::ExactFixed64:
            return MakeConfiguration(
                profile, "Exact Fixed 64", Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed64,
                Samples::Fixed64, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::Legacy,
                Temporal::Legacy, Application::LegacySeparateComposition,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Any,
                false, false);
        case VisibilityPerformanceProfile::ExactPackedCurrentFast:
            return MakeConfiguration(
                profile, "Offline Packed Spacetime Noise",
                Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Fixed8, Noise::PackedCurrentFast,
                Math::ReferenceFp32, Storage::ScalarFloat,
                Reconstruction::Legacy, Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::IncludesAmbientOcclusion, Resolution::Any,
                false, false, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::ExactFusedResolveApply:
            return MakeConfiguration(
                profile, "Fused Apply", Class::Exact,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::Legacy,
                Temporal::Legacy, Application::FusedResolveAndApplyExact,
                Depth::Legacy, Bindings::LegacyBroad,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, true);
        case VisibilityPerformanceProfile::ExactFixed8FusedResolveApply:
            return MakeConfiguration(
                profile, "Fixed 8 Fused Apply",
                Class::Exact,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Fixed8, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::Legacy,
                Temporal::Legacy, Application::FusedResolveAndApplyExact,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, true, Edge::None,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2:
            return MakeConfiguration(
                profile, "Depth-Guided Reconstruction",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Fixed8, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat,
                Reconstruction::PackedEdges2x2, Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesDepthNormal2x2:
            return MakeConfiguration(
                profile, "Depth-Normal Reconstruction",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Fixed8, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::PackedEdges2x2,
                Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicPackedEdgesSlope2x2:
            return MakeConfiguration(
                profile, "Slope-Aware Reconstruction",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Fixed8, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::PackedEdges2x2,
                Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicPackedEdgesLeakage2x2:
            return MakeConfiguration(
                profile, "Leakage-Limited Reconstruction",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Fixed8, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::PackedEdges2x2,
                Temporal::Legacy,
                Application::LegacySeparateComposition, Depth::Legacy,
                Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::Any, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::AlgorithmicFusedPackedEdges2x2:
            return MakeConfiguration(
                profile, "Fused Depth-Normal Apply",
                Class::Algorithmic,
                Trace::FixedInterleavedBitmask, Samples::Fixed8,
                Samples::Fixed8, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::PackedEdges2x2,
                Temporal::Legacy,
                Application::FusedResolveAndApplyPackedEdges,
                Depth::Legacy, Bindings::MinimalConditional,
                Traversal::InterleavedNegativePositiveNearToFar,
                Consumer::AmbientOcclusionOnly, Resolution::Reduced,
                false, false, Edge::R8Uint,
                Estimator::UniformSolidAngle);
        case VisibilityPerformanceProfile::GenericFallback:
            return MakeConfiguration(
                profile, "Generic Fallback", Class::Exact,
                Trace::LegacyGenericBitmask, Samples::Runtime,
                Samples::Runtime, Noise::Legacy, Math::ReferenceFp32,
                Storage::ScalarFloat, Reconstruction::Legacy,
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
                VisibilityTraceImplementation::FixedInterleavedBitmask) &&
            IsAssignedEnum(configuration.firstBounceSamples,
                VisibilitySampleSpecialization::Fixed64) &&
            IsAssignedEnum(configuration.laterBounceSamples,
                VisibilitySampleSpecialization::Fixed64) &&
            IsAssignedEnum(configuration.noise,
                VisibilityNoiseDelivery::PackedCurrentFast) &&
            IsAssignedEnum(configuration.math,
                VisibilityMathMode::ReferenceFp32) &&
            IsAssignedEnum(configuration.rawAoStorage,
                VisibilityRawAoStorage::ScalarFloat) &&
            IsAssignedEnum(configuration.edgeStorage,
                VisibilityEdgeStorage::R8Uint) &&
            IsAssignedEnum(configuration.reconstruction,
                VisibilityReconstructionMode::PackedEdges2x2) &&
            IsAssignedEnum(configuration.temporal,
                VisibilityTemporalMode::Legacy) &&
            IsAssignedEnum(configuration.application,
                VisibilityApplicationMode::FusedResolveAndApplyPackedEdges) &&
            IsAssignedEnum(configuration.depth,
                VisibilityDepthMode::Legacy) &&
            IsAssignedEnum(configuration.bindings,
                VisibilityBindingStrategy::MinimalConditional) &&
            IsAssignedEnum(configuration.traversal,
                VisibilityTraversalOrder::GroupedBySide) &&
            IsAssignedEnum(configuration.consumerRequirement,
                VisibilityConsumerRequirement::IncludesIndirectDiffuse) &&
            IsAssignedEnum(configuration.estimatorRequirement,
                VisibilityEstimatorRequirement::CosineWeightedSolidAngle) &&
            IsAssignedEnum(configuration.resolutionRequirement,
                VisibilityResolutionRequirement::Full) &&
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
        const bool usesLegacyDepthPreparation =
            workload.depthHierarchyEnabled;
        const bool usesDepthPreparation = usesLegacyDepthPreparation;
        const bool bindsDepthHierarchy = usesLegacyDepthPreparation;

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
        if (configuration.resolutionRequirement ==
                VisibilityResolutionRequirement::Full &&
            workload.resolution != VisibilityPerformanceResolution::Full)
        {
            return reject(VisibilityPlanError::ProfileResolutionMismatch,
                "This profile requires full resolution.");
        }
        if (!MatchesEstimatorRequirement(
                configuration.estimatorRequirement, workload.estimator))
        {
            return reject(VisibilityPlanError::ProfileEstimatorMismatch,
                "The workload estimator does not match the compiled shader.");
        }
        if (workload.threadGroupSizeX != 8u ||
            workload.threadGroupSizeY != 8u)
        {
            return reject(VisibilityPlanError::ProfileThreadGroupMismatch,
                "Visibility profiles use the retained 8x8 thread-group shape.");
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
        const bool fixedQuadraticExponent =
            configuration.trace ==
                VisibilityTraceImplementation::FixedInterleavedBitmask;
        if (fixedQuadraticExponent &&
            (fixedFirstCount != 0u || fixedLaterCount != 0u) &&
            workload.radialExponent != 2.0f)
        {
            return reject(VisibilityPlanError::FixedExponentMismatch,
                "The fixed shader is compiled for a quadratic radial exponent.");
        }
        if (configuration.noise == VisibilityNoiseDelivery::PackedCurrentFast &&
            workload.scheduler != VisibilityPerformanceScheduler::
                FilterAdaptedSpatiotemporalRankField)
        {
            return reject(VisibilityPlanError::ProfileSchedulerMismatch,
                "Offline Packed Spacetime Noise requires the matching "
                "Offline Spacetime Noise schedule.");
        }
        const bool packedReconstruction =
            configuration.reconstruction ==
                VisibilityReconstructionMode::PackedEdges2x2;
        const bool separatePackedEdges =
            configuration.edgeStorage == VisibilityEdgeStorage::R8Uint;
        const bool validSeparatePacking =
            packedReconstruction && separatePackedEdges;
        const bool noPacking = !packedReconstruction &&
            !separatePackedEdges;
        if (!validSeparatePacking && !noPacking)
        {
            return reject(VisibilityPlanError::InvalidPackedReconstruction,
                "Packed reconstruction requires R16F AO plus a separate "
                "R8_UINT edge buffer.");
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
                    VisibilityRawAoStorage::ScalarFloat ||
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
                    "Edge-guided fusion requires R16F AO, a separate R8 edge "
                    "buffer, and edge-guided reconstruction.");
            }
        }
        else if (configuration.explicitHalfRoundtrip)
        {
            return reject(
                VisibilityPlanError::FusedApplicationRequiresHalfRoundtrip,
                "An explicit resolve roundtrip is meaningful only for exact fusion.");
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

        const bool schedulerTextureBound =
            configuration.noise ==
                    VisibilityNoiseDelivery::PackedCurrentFast ||
                (configuration.noise == VisibilityNoiseDelivery::Legacy &&
                    (workload.scheduler ==
                            VisibilityPerformanceScheduler::
                                ToroidalBlueNoiseRankField ||
                        workload.scheduler ==
                            VisibilityPerformanceScheduler::
                                FilterAdaptedSpatiotemporalRankField));

        if (usesLegacyBroadPipeline)
        {
            // The legacy shader layout is intentionally broad for every
            // consumer permutation, including resources compiled out by a
            // particular shader.
            plan.firstTraceSrvCount = 8u;
            plan.firstTraceUavCount = 3u;
        }
        else
        {
            plan.firstTraceSrvCount = 2u +
                (hasIndirectDiffuse ? 1u : 0u) +
                (usesLegacyDepthPreparation ? 1u : 0u) +
                (schedulerTextureBound ? 1u : 0u);
            plan.firstTraceUavCount =
                (hasAmbientOcclusion ? 1u : 0u) +
                (hasIndirectDiffuse ? 1u : 0u) +
                (configuration.edgeStorage != VisibilityEdgeStorage::None
                    ? 1u : 0u);
        }

        if (hasAmbientOcclusion)
        {
            plan.resourceMask |=
                Bit(VisibilityExecutionResource::RawAmbient);
        }
        switch (configuration.edgeStorage)
        {
        case VisibilityEdgeStorage::R8Uint:
            plan.resourceMask |=
                Bit(VisibilityExecutionResource::PackedEdgesR8Uint);
            break;
        default:
            break;
        }
        if (hasIndirectDiffuse)
        {
            plan.resourceMask |=
                Bit(VisibilityExecutionResource::RawIndirect);
            if (workload.bounceCount > 1u)
            {
                plan.resourceMask |= Bit(
                    VisibilityExecutionResource::CumulativeIndirect);
            }
        }

        if (workload.temporalEnabled)
        {
            plan.resourceMask |=
                Bit(VisibilityExecutionResource::TemporalDepth) |
                Bit(VisibilityExecutionResource::TemporalNormalRgba8);
            if (hasAmbientOcclusion)
            {
                plan.resourceMask |=
                    Bit(VisibilityExecutionResource::TemporalAmbient);
            }
            if (hasIndirectDiffuse)
            {
                plan.resourceMask |= Bit(
                    VisibilityExecutionResource::TemporalIndirect);
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
                plan.resourceMask |=
                    Bit(VisibilityExecutionResource::FinalAmbient);
            }
            if (hasIndirectDiffuse)
            {
                plan.resourceMask |= Bit(
                    VisibilityExecutionResource::FinalIndirect);
            }
        }

        if (bindsDepthHierarchy)
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
        else if (configuration.noise == VisibilityNoiseDelivery::Legacy)
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
        else if (configuration.noise ==
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
            if (workload.temporalEnabled)
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
            plan.bindingMask |=
                Bit(VisibilityExecutionBinding::BaseLighting) |
                Bit(VisibilityExecutionBinding::OutputLighting) |
                Bit(VisibilityExecutionBinding::GBufferMaterial);
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
            if (bindsDepthHierarchy)
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
                    VisibilityExecutionResource::PackedEdgesR8Uint))
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
            plan.passMask |= Bit(VisibilityExecutionPass::Composition);
        }

        plan.optionalResourceMask =
            plan.resourceMask & VisibilityOptionalResourceMask;
        plan.candidateBindingMask =
            plan.bindingMask & VisibilityCandidateBindingMask;
        plan.candidatePassMask =
            plan.passMask & VisibilityCandidatePassMask;

        plan.dispatchCount = CountBits(plan.passMask);
        if (hasIndirectDiffuse && workload.bounceCount > 2u)
        {
            // The pass bit represents all later bounces; count the remaining
            // dispatches after the one already represented by that bit.
            plan.dispatchCount += workload.bounceCount - 2u;
        }
        if (hasIndirectDiffuse &&
            workload.bounceCount > c_MaximumExplicitBounceCount)
        {
            // Contribution-terminated mode runs one single-thread GPU control
            // dispatch after every possible later bounce. It writes either
            // the full next dispatch or zero groups without a CPU readback.
            plan.dispatchCount += workload.bounceCount - 1u;
        }

        plan.peakSrvCount = plan.firstTraceSrvCount;
        plan.peakUavCount = plan.firstTraceUavCount;
        auto includeLayout = [&plan](uint32_t srvCount, uint32_t uavCount)
        {
            plan.peakSrvCount = std::max(plan.peakSrvCount, srvCount);
            plan.peakUavCount = std::max(plan.peakUavCount, uavCount);
        };
        if (usesDepthPreparation)
        {
            includeLayout(1u, 5u);
        }
        if (hasIndirectDiffuse && workload.bounceCount > 1u)
        {
            includeLayout(fixedLaterCount != 0u
                    ? 6u + (schedulerTextureBound ? 1u : 0u)
                    : 11u,
                workload.bounceCount > c_MaximumExplicitBounceCount
                    ? 3u : 2u);
        }
        if (workload.temporalEnabled)
        {
            includeLayout(9u, 4u);
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
                else
                    includeLayout(4u, 2u);
            }
            includeLayout(8u, 1u);
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
        case VisibilityVerificationProfile::PackedEdgeAo8T:
            return MakeVerificationDefinition(profile, "Packed-Edge AO 8T",
                Implementation::AlgorithmicPackedEdges2x2, workload,
                Status::Implemented,
                "Uses R16F raw AO plus a separate R8_UINT edge texture.");
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
            workload.laterBounceSampleCount = 12u;
            return MakeVerificationDefinition(
                profile, "Exact-Fast AO+GI 12T",
                Implementation::ExactFixed12, workload,
                Status::PartialBenchmarkControl,
                "The exact fixed-12 trace is compiled; offline-computed "
                "packed noise is only compiled for the fixed-8 candidate.");
        case VisibilityVerificationProfile::ExactFastAoGi16T:
            workload.consumer =
                Consumer::AmbientOcclusionAndIndirectDiffuse;
            workload.firstBounceSampleCount = 16u;
            workload.laterBounceSampleCount = 16u;
            return MakeVerificationDefinition(
                profile, "Exact-Fast AO+GI 16T",
                Implementation::ExactFixed16, workload,
                Status::PartialBenchmarkControl,
                "The exact fixed-16 trace is compiled; offline-computed "
                "packed noise is only compiled for the fixed-8 candidate.");
        case VisibilityVerificationProfile::ExactFastMultiBounce:
            workload.consumer =
                Consumer::AmbientOcclusionAndIndirectDiffuse;
            workload.bounceCount = 2u;
            return MakeVerificationDefinition(
                profile, "Exact-Fast Multi-Bounce",
                Implementation::ExactFixed8, workload,
                Status::PartialBenchmarkControl,
                "The first and later traces use exact fixed-8 shaders; "
                "offline-computed packed noise and fused application are not "
                "compiled for the multi-bounce path.");
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
