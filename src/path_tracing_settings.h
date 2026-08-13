#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace uvsr
{
    enum class LightingSolution : uint8_t
    {
        RayMarching,
        PathTracing
    };

    enum class PathTracingSolver : uint8_t
    {
        RtxPt,
        RestirPt,
        RestirGi
    };

    enum class PathTracingNeeMode : uint8_t
    {
        Uniform,
        Power,
        NeeAdaptiveTree
    };

    enum class PathTracingDenoiser : uint8_t
    {
        Raw,
        SpatialPathResolve
    };

    enum class PathTracingDebugView : uint8_t
    {
        FinalImage,
        Albedo,
        GeometricNormal,
        ShadingNormal,
        SampleCount,
        UpdateRate,
        SignalGroup,
        DirectReservoir,
        IndirectReservoir,
        PrimaryTransport,
        IndirectTransport
    };

    inline constexpr uint32_t PathTracingMinBounceCount = 1u;
    inline constexpr uint32_t PathTracingMaxBounceCount = 96u;
    inline constexpr uint32_t PathTracingMinNeeCandidateCount = 1u;
    inline constexpr uint32_t PathTracingMaxNeeCandidateCount = 63u;
    inline constexpr uint32_t PathTracingMaxStablePlaneCount = 3u;
    inline constexpr uint32_t PathTracingSolverCount = 3u;
    inline constexpr uint32_t PathTracingNeeModeCount = 3u;
    inline constexpr uint32_t PathTracingRtxdiModeCount = 2u;
    inline constexpr uint32_t PathTracingPipelineVariantsPerSolver =
        PathTracingNeeModeCount * PathTracingRtxdiModeCount;
    inline constexpr uint32_t PathTracingPipelineVariantCount =
        PathTracingSolverCount * PathTracingPipelineVariantsPerSolver;
    static_assert(PathTracingPipelineVariantCount < 32u);
    inline constexpr uint32_t PathTracingAllPipelineVariantsMask =
        (1u << PathTracingPipelineVariantCount) - 1u;
    inline constexpr float PathTracingDefaultFireflyThreshold = 5.f;
    inline constexpr float PathTracingMinFireflyThreshold = 0.01f;
    inline constexpr float PathTracingMaxFireflyThreshold = 1000000.f;
    inline constexpr float PathTracingDefaultSpatialResolveStrength = 1.f;
    inline constexpr float PathTracingMinSpatialResolveStrength = 0.f;
    inline constexpr float PathTracingMaxSpatialResolveStrength = 1.f;

    struct PathTracingSettings
    {
        // Transport controls consumed by the shared path integrator.
        PathTracingSolver solver = PathTracingSolver::RtxPt;
        PathTracingNeeMode neeMode = PathTracingNeeMode::Uniform;
        uint32_t maxBounces = 8u;
        uint32_t russianRouletteStart = 3u;
        uint32_t neeCandidateCount = 1u;
        bool useSer = false;

        // These switches select the optional reservoir stages around the same
        // transport core. Presets initialize them, but the UI may edit them.
        bool useRtxdi = false;
        bool reuseDirectReservoirs = false;
        bool reusePathReservoirs = false;
        bool reuseIndirectGiReservoirs = false;
        // Camera motion may retain only proposal identities that are
        // revalidated at the current pixel. Radiance means and RESTIR GI
        // checkpoints always reset.
        bool reuseRevalidatedProposalsDuringMotion = false;

        // Reconstruction controls consumed after raw transport completes.
        // One group filters the combined path. Two groups independently
        // filter primary and indirect transport for every solver. The third
        // RTX PT-only group separates diffuse and specular continuation.
        uint32_t stablePlaneCount = 3u;
        float spatialResolveStrength =
            PathTracingDefaultSpatialResolveStrength;
        bool enableFireflyFilter = false;
        float fireflyThreshold = PathTracingDefaultFireflyThreshold;
        PathTracingDenoiser denoiser =
            PathTracingDenoiser::SpatialPathResolve;
        PathTracingDebugView debugView = PathTracingDebugView::FinalImage;
    };

    struct PathTracingPipelineResolution
    {
        PathTracingSettings effectiveSettings;
        uint32_t requestedVariant = 0u;
        uint32_t effectiveVariant = 0u;
        bool executable = false;
        bool fallbackApplied = false;
    };

    [[nodiscard]] constexpr uint32_t GetPathTracingPipelineVariant(
        const PathTracingSettings& settings) noexcept
    {
        return static_cast<uint32_t>(settings.solver) *
                PathTracingPipelineVariantsPerSolver +
            (settings.useRtxdi ? PathTracingNeeModeCount : 0u) +
            static_cast<uint32_t>(settings.neeMode);
    }

    [[nodiscard]] constexpr bool IsPathTracingPipelineAvailable(
        const PathTracingSettings& settings,
        uint32_t availabilityMask) noexcept
    {
        const uint32_t variant = GetPathTracingPipelineVariant(settings);
        return variant < PathTracingPipelineVariantCount &&
            (availabilityMask & (1u << variant)) != 0u;
    }

    [[nodiscard]] constexpr bool IsSpatialPathResolveRequested(
        const PathTracingSettings& settings) noexcept
    {
        return settings.denoiser ==
                PathTracingDenoiser::SpatialPathResolve &&
            settings.stablePlaneCount >= 1u;
    }

    [[nodiscard]] constexpr bool CanUseSpatialPathResolve(
        const PathTracingSettings& settings,
        bool resolveSupported) noexcept
    {
        // Candidate resampling keeps primary + solved-indirect transport
        // additive, so one or two groups are valid for every solver. Only
        // un-resampled RTX PT retains a trustworthy first-lobe identity for
        // the three-group diffuse/specular split.
        return resolveSupported &&
            IsSpatialPathResolveRequested(settings) &&
            (settings.stablePlaneCount <= 2u ||
                settings.solver == PathTracingSolver::RtxPt);
    }

    [[nodiscard]] constexpr PathTracingSettings
        NormalizePathTracingEffectiveSettings(
            PathTracingSettings settings) noexcept
    {
        if (!settings.useRtxdi)
        {
            settings.reuseDirectReservoirs = false;
            if (settings.debugView ==
                PathTracingDebugView::DirectReservoir)
            {
                settings.debugView = PathTracingDebugView::FinalImage;
            }
        }

        if (settings.solver != PathTracingSolver::RestirPt)
            settings.reusePathReservoirs = false;
        if (settings.solver != PathTracingSolver::RestirGi)
            settings.reuseIndirectGiReservoirs = false;
        const bool indirectDebugAvailable =
            (settings.solver == PathTracingSolver::RestirPt &&
                settings.reusePathReservoirs) ||
            (settings.solver == PathTracingSolver::RestirGi &&
                settings.reuseIndirectGiReservoirs);
        if (!indirectDebugAvailable &&
            settings.debugView ==
                PathTracingDebugView::IndirectReservoir)
        {
            settings.debugView = PathTracingDebugView::FinalImage;
        }
        return settings;
    }

    [[nodiscard]] constexpr PathTracingPipelineResolution
        ResolvePathTracingPipeline(
            const PathTracingSettings& requestedSettings,
            uint32_t availabilityMask) noexcept
    {
        PathTracingPipelineResolution resolution;
        resolution.effectiveSettings = requestedSettings;
        resolution.requestedVariant =
            GetPathTracingPipelineVariant(requestedSettings);
        resolution.effectiveVariant = resolution.requestedVariant;
        if (IsPathTracingPipelineAvailable(
                requestedSettings,
                availabilityMask))
        {
            resolution.executable = true;
            resolution.effectiveSettings =
                NormalizePathTracingEffectiveSettings(
                    resolution.effectiveSettings);
            return resolution;
        }

        resolution.fallbackApplied = true;
        PathTracingSettings candidate = requestedSettings;
        candidate.useRtxdi = false;
        if (IsPathTracingPipelineAvailable(candidate, availabilityMask))
        {
            resolution.effectiveSettings =
                NormalizePathTracingEffectiveSettings(candidate);
            resolution.effectiveVariant =
                GetPathTracingPipelineVariant(candidate);
            resolution.executable = true;
            return resolution;
        }

        candidate.neeMode = PathTracingNeeMode::Uniform;
        if (IsPathTracingPipelineAvailable(candidate, availabilityMask))
        {
            resolution.effectiveSettings =
                NormalizePathTracingEffectiveSettings(candidate);
            resolution.effectiveVariant =
                GetPathTracingPipelineVariant(candidate);
            resolution.executable = true;
            return resolution;
        }

        candidate = requestedSettings;
        candidate.solver = PathTracingSolver::RtxPt;
        candidate.useRtxdi = false;
        if (IsPathTracingPipelineAvailable(candidate, availabilityMask))
        {
            resolution.effectiveSettings =
                NormalizePathTracingEffectiveSettings(candidate);
            resolution.effectiveVariant =
                GetPathTracingPipelineVariant(candidate);
            resolution.executable = true;
            return resolution;
        }

        candidate.neeMode = PathTracingNeeMode::Uniform;
        resolution.effectiveSettings =
            NormalizePathTracingEffectiveSettings(candidate);
        resolution.effectiveVariant = 0u;
        resolution.executable =
            IsPathTracingPipelineAvailable(candidate, availabilityMask);
        return resolution;
    }

    [[nodiscard]] constexpr bool IsValidLightingSolution(
        LightingSolution solution) noexcept
    {
        switch (solution)
        {
        case LightingSolution::RayMarching:
        case LightingSolution::PathTracing:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] constexpr const char* GetLightingSolutionLabel(
        LightingSolution solution) noexcept
    {
        switch (solution)
        {
        case LightingSolution::RayMarching: return "Ray Marching";
        case LightingSolution::PathTracing: return "Path Tracing";
        default: return "";
        }
    }

    [[nodiscard]] constexpr bool IsValidPathTracingSolver(
        PathTracingSolver solver) noexcept
    {
        switch (solver)
        {
        case PathTracingSolver::RtxPt:
        case PathTracingSolver::RestirPt:
        case PathTracingSolver::RestirGi:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] constexpr const char* GetPathTracingSolverLabel(
        PathTracingSolver solver) noexcept
    {
        switch (solver)
        {
        case PathTracingSolver::RtxPt: return "RTX PT";
        case PathTracingSolver::RestirPt: return "RESTIR PT";
        case PathTracingSolver::RestirGi: return "RESTIR GI";
        default: return "";
        }
    }

    [[nodiscard]] constexpr bool IsValidPathTracingNeeMode(
        PathTracingNeeMode mode) noexcept
    {
        switch (mode)
        {
        case PathTracingNeeMode::Uniform:
        case PathTracingNeeMode::Power:
        case PathTracingNeeMode::NeeAdaptiveTree:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] constexpr const char* GetPathTracingNeeModeLabel(
        PathTracingNeeMode mode) noexcept
    {
        switch (mode)
        {
        case PathTracingNeeMode::Uniform: return "Uniform";
        case PathTracingNeeMode::Power: return "Power";
        case PathTracingNeeMode::NeeAdaptiveTree: return "NEE-AT";
        default: return "";
        }
    }

    [[nodiscard]] constexpr bool IsValidPathTracingDenoiser(
        PathTracingDenoiser denoiser) noexcept
    {
        switch (denoiser)
        {
        case PathTracingDenoiser::Raw:
        case PathTracingDenoiser::SpatialPathResolve:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] constexpr const char* GetPathTracingDenoiserLabel(
        PathTracingDenoiser denoiser) noexcept
    {
        switch (denoiser)
        {
        case PathTracingDenoiser::Raw: return "Raw (No Denoising)";
        case PathTracingDenoiser::SpatialPathResolve:
            return "Spatial Path Resolve";
        default: return "";
        }
    }

    [[nodiscard]] constexpr bool IsValidPathTracingDebugView(
        PathTracingDebugView view) noexcept
    {
        switch (view)
        {
        case PathTracingDebugView::FinalImage:
        case PathTracingDebugView::Albedo:
        case PathTracingDebugView::GeometricNormal:
        case PathTracingDebugView::ShadingNormal:
        case PathTracingDebugView::SampleCount:
        case PathTracingDebugView::UpdateRate:
        case PathTracingDebugView::SignalGroup:
        case PathTracingDebugView::DirectReservoir:
        case PathTracingDebugView::IndirectReservoir:
        case PathTracingDebugView::PrimaryTransport:
        case PathTracingDebugView::IndirectTransport:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] constexpr const char* GetPathTracingDebugViewLabel(
        PathTracingDebugView view) noexcept
    {
        switch (view)
        {
        case PathTracingDebugView::FinalImage: return "Final Image";
        case PathTracingDebugView::Albedo: return "Albedo";
        case PathTracingDebugView::GeometricNormal:
            return "Geometric Normal";
        case PathTracingDebugView::ShadingNormal: return "Shading Normal";
        case PathTracingDebugView::SampleCount: return "Sample Count";
        case PathTracingDebugView::UpdateRate:
            return "Update Rate";
        case PathTracingDebugView::SignalGroup: return "Signal Group";
        case PathTracingDebugView::DirectReservoir:
            return "Direct Reservoir";
        case PathTracingDebugView::IndirectReservoir:
            return "Indirect Reservoir";
        case PathTracingDebugView::PrimaryTransport:
            return "Primary Transport";
        case PathTracingDebugView::IndirectTransport:
            return "Indirect Transport";
        default: return "";
        }
    }

    [[nodiscard]] constexpr bool IsFinitePathTracingFloat(float value) noexcept
    {
        constexpr float Maximum = std::numeric_limits<float>::max();
        return value == value && value >= -Maximum && value <= Maximum;
    }

    template<typename T>
    [[nodiscard]] constexpr T ClampPathTracingSetting(
        T value,
        T minimum,
        T maximum) noexcept
    {
        return value < minimum
            ? minimum
            : (value > maximum ? maximum : value);
    }

    [[nodiscard]] constexpr PathTracingSettings ApplyPathTracingSolverPreset(
        PathTracingSolver solver) noexcept
    {
        PathTracingSettings settings;

        switch (solver)
        {
        case PathTracingSolver::RestirPt:
            settings.solver = PathTracingSolver::RestirPt;
            settings.neeMode = PathTracingNeeMode::Power;
            settings.maxBounces = 3u;
            settings.russianRouletteStart = 3u;
            settings.neeCandidateCount = 1u;
            // RTXDI is an optional orthogonal primary-direct stage. Retain
            // its reuse preference while keeping the solver executable
            // without allocating the direct-reservoir history by default.
            settings.useRtxdi = false;
            settings.reuseDirectReservoirs = true;
            settings.reusePathReservoirs = true;
            settings.stablePlaneCount = 2u;
            break;

        case PathTracingSolver::RestirGi:
            settings.solver = PathTracingSolver::RestirGi;
            settings.neeMode = PathTracingNeeMode::Power;
            settings.neeCandidateCount = 1u;
            settings.useRtxdi = false;
            settings.reuseDirectReservoirs = true;
            settings.reuseIndirectGiReservoirs = true;
            settings.stablePlaneCount = 2u;
            break;

        case PathTracingSolver::RtxPt:
        default:
            // The unbiased RTX PT preset never enables reservoir resampling.
            settings.solver = PathTracingSolver::RtxPt;
            break;
        }

        return settings;
    }

    [[nodiscard]] constexpr bool operator==(
        const PathTracingSettings& left,
        const PathTracingSettings& right) noexcept
    {
        return left.solver == right.solver &&
            left.neeMode == right.neeMode &&
            left.maxBounces == right.maxBounces &&
            left.russianRouletteStart == right.russianRouletteStart &&
            left.neeCandidateCount == right.neeCandidateCount &&
            left.useSer == right.useSer &&
            left.useRtxdi == right.useRtxdi &&
            left.reuseDirectReservoirs == right.reuseDirectReservoirs &&
            left.reusePathReservoirs == right.reusePathReservoirs &&
            left.reuseIndirectGiReservoirs ==
                right.reuseIndirectGiReservoirs &&
            left.reuseRevalidatedProposalsDuringMotion ==
                right.reuseRevalidatedProposalsDuringMotion &&
            left.stablePlaneCount == right.stablePlaneCount &&
            left.spatialResolveStrength ==
                right.spatialResolveStrength &&
            left.enableFireflyFilter == right.enableFireflyFilter &&
            left.fireflyThreshold == right.fireflyThreshold &&
            left.denoiser == right.denoiser &&
            left.debugView == right.debugView;
    }

    [[nodiscard]] constexpr bool operator!=(
        const PathTracingSettings& left,
        const PathTracingSettings& right) noexcept
    {
        return !(left == right);
    }

    [[nodiscard]] constexpr bool IsValidPathTracingSettings(
        const PathTracingSettings& settings) noexcept
    {
        return IsValidPathTracingSolver(settings.solver) &&
            IsValidPathTracingNeeMode(settings.neeMode) &&
            settings.maxBounces >= PathTracingMinBounceCount &&
            settings.maxBounces <= PathTracingMaxBounceCount &&
            settings.russianRouletteStart >= PathTracingMinBounceCount &&
            settings.russianRouletteStart <= settings.maxBounces &&
            settings.neeCandidateCount >= PathTracingMinNeeCandidateCount &&
            settings.neeCandidateCount <= PathTracingMaxNeeCandidateCount &&
            settings.stablePlaneCount <= PathTracingMaxStablePlaneCount &&
            IsFinitePathTracingFloat(settings.spatialResolveStrength) &&
            settings.spatialResolveStrength >=
                PathTracingMinSpatialResolveStrength &&
            settings.spatialResolveStrength <=
                PathTracingMaxSpatialResolveStrength &&
            IsFinitePathTracingFloat(settings.fireflyThreshold) &&
            settings.fireflyThreshold >= PathTracingMinFireflyThreshold &&
            settings.fireflyThreshold <= PathTracingMaxFireflyThreshold &&
            IsValidPathTracingDenoiser(settings.denoiser) &&
            IsValidPathTracingDebugView(settings.debugView);
    }

    [[nodiscard]] constexpr PathTracingSettings SanitizePathTracingSettings(
        PathTracingSettings settings) noexcept
    {
        if (!IsValidPathTracingSolver(settings.solver))
            settings.solver = PathTracingSolver::RtxPt;
        if (!IsValidPathTracingNeeMode(settings.neeMode))
            settings.neeMode = PathTracingSettings{}.neeMode;

        settings.maxBounces = ClampPathTracingSetting(
            settings.maxBounces,
            PathTracingMinBounceCount,
            PathTracingMaxBounceCount);
        settings.russianRouletteStart = ClampPathTracingSetting(
            settings.russianRouletteStart,
            PathTracingMinBounceCount,
            settings.maxBounces);
        settings.neeCandidateCount = ClampPathTracingSetting(
            settings.neeCandidateCount,
            PathTracingMinNeeCandidateCount,
            PathTracingMaxNeeCandidateCount);

        settings.stablePlaneCount = ClampPathTracingSetting(
            settings.stablePlaneCount,
            0u,
            PathTracingMaxStablePlaneCount);
        if (!IsFinitePathTracingFloat(settings.spatialResolveStrength))
        {
            settings.spatialResolveStrength =
                PathTracingDefaultSpatialResolveStrength;
        }
        settings.spatialResolveStrength = ClampPathTracingSetting(
            settings.spatialResolveStrength,
            PathTracingMinSpatialResolveStrength,
            PathTracingMaxSpatialResolveStrength);
        if (!IsFinitePathTracingFloat(settings.fireflyThreshold))
            settings.fireflyThreshold = PathTracingDefaultFireflyThreshold;
        settings.fireflyThreshold = ClampPathTracingSetting(
            settings.fireflyThreshold,
            PathTracingMinFireflyThreshold,
            PathTracingMaxFireflyThreshold);

        if (!IsValidPathTracingDenoiser(settings.denoiser))
            settings.denoiser = PathTracingDenoiser::Raw;
        if (settings.denoiser == PathTracingDenoiser::SpatialPathResolve)
        {
            settings.stablePlaneCount = settings.stablePlaneCount == 0u
                ? 1u
                : settings.stablePlaneCount;
            if (settings.solver != PathTracingSolver::RtxPt)
            {
                settings.stablePlaneCount = std::min(
                    settings.stablePlaneCount,
                    2u);
            }
        }
        if (!IsValidPathTracingDebugView(settings.debugView))
            settings.debugView = PathTracingDebugView::FinalImage;

        return settings;
    }

    [[nodiscard]] constexpr uint32_t SaturatingIncrementSuccessfulSampleCount(
        uint32_t successfulSampleCount) noexcept
    {
        return successfulSampleCount == std::numeric_limits<uint32_t>::max()
            ? successfulSampleCount
            : successfulSampleCount + 1u;
    }

    [[nodiscard]] constexpr float GetAccumulationOnlineMeanWeight(
        uint32_t newSuccessfulSampleCount) noexcept
    {
        return newSuccessfulSampleCount == 0u
            ? 0.f
            : static_cast<float>(1.0 /
                static_cast<double>(newSuccessfulSampleCount));
    }
}
