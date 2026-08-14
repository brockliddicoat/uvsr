#include "path_tracing_settings.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace
{
    using namespace uvsr;

    [[nodiscard]] bool Near(
        float actual,
        float expected,
        float tolerance = 1e-7f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    void TestDefaults()
    {
        constexpr PathTracingSettings Defaults;
        static_assert(IsValidPathTracingSettings(Defaults));
        static_assert(Defaults.solver == PathTracingSolver::RtxPt);
        static_assert(Defaults.neeMode == PathTracingNeeMode::Uniform);
        static_assert(Defaults.maxBounces == 4u);
        static_assert(Defaults.useRussianRoulette);
        static_assert(GetAutomaticRussianRouletteStart(Defaults) == 3u);
        static_assert(Defaults.neeCandidateCount == 1u);
        static_assert(Defaults.samplesPerPixel == 2u);
        static_assert(!Defaults.useSer);
        static_assert(!Defaults.useRtxdi);
        static_assert(!Defaults.temporalReuse);
        static_assert(Defaults.spatialNeighborCount == 0u);
        static_assert(!Defaults.reuseRevalidatedProposalsDuringMotion);
        static_assert(Defaults.sharedPrimarySurface);
        static_assert(Defaults.stablePlaneCount == 3u);
        static_assert(Defaults.spatialResolveStrength == 1.f);
        static_assert(Defaults.enableFireflyFilter);
        static_assert(Defaults.fireflyThreshold == 3.f);
        static_assert(Defaults.denoiser == PathTracingDenoiser::Raw);
        static_assert(Defaults.debugView ==
            PathTracingDebugView::FinalImage);
    }

    void TestLabelsAndEnumValidation()
    {
        assert(IsValidLightingSolution(LightingSolution::RayMarching));
        assert(IsValidLightingSolution(LightingSolution::PathTracing));
        assert(GetLightingSolutionLabel(LightingSolution::RayMarching) ==
            std::string_view("Ray Marching"));
        assert(GetLightingSolutionLabel(LightingSolution::PathTracing) ==
            std::string_view("Path Tracing"));

        assert(IsValidPathTracingSolver(PathTracingSolver::RtxPt));
        assert(IsValidPathTracingSolver(PathTracingSolver::RestirPt));
        assert(IsValidPathTracingSolver(PathTracingSolver::RestirGi));
        assert(GetPathTracingSolverLabel(PathTracingSolver::RtxPt) ==
            std::string_view("Realtime Path Tracer"));
        assert(GetPathTracingSolverLabel(PathTracingSolver::RestirPt) ==
            std::string_view("Reservoir Path Tracer"));
        assert(GetPathTracingSolverLabel(PathTracingSolver::RestirGi) ==
            std::string_view("Reservoir Indirect Lighting"));

        assert(IsValidPathTracingNeeMode(PathTracingNeeMode::Uniform));
        assert(IsValidPathTracingNeeMode(PathTracingNeeMode::Power));
        assert(IsValidPathTracingNeeMode(
            PathTracingNeeMode::NeeAdaptiveTree));
        assert(GetPathTracingNeeModeLabel(PathTracingNeeMode::Uniform) ==
            std::string_view("Uniform"));
        assert(GetPathTracingNeeModeLabel(PathTracingNeeMode::Power) ==
            std::string_view("Power"));
        assert(GetPathTracingNeeModeLabel(
            PathTracingNeeMode::NeeAdaptiveTree) ==
            std::string_view("Adaptively Temporally"));

        assert(IsValidPathTracingDenoiser(PathTracingDenoiser::Raw));
        assert(IsValidPathTracingDenoiser(
            PathTracingDenoiser::SpatialPathResolve));
        assert(GetPathTracingDenoiserLabel(PathTracingDenoiser::Raw) ==
            std::string_view("Raw (No Denoising)"));
        assert(GetPathTracingDenoiserLabel(
            PathTracingDenoiser::SpatialPathResolve) ==
            std::string_view("Spatial Path Resolve"));

        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::FinalImage));
        assert(IsValidPathTracingDebugView(PathTracingDebugView::Albedo));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::GeometricNormal));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::ShadingNormal));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::SampleCount));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::UpdateRate));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::SignalGroup));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::DirectReservoir));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::IndirectReservoir));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::PrimaryTransport));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::IndirectTransport));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::FinalImage) ==
            std::string_view("Final Image"));
        assert(GetPathTracingDebugViewLabel(PathTracingDebugView::Albedo) ==
            std::string_view("Albedo"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::GeometricNormal) ==
            std::string_view("Geometric Normal"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::ShadingNormal) ==
            std::string_view("Shading Normal"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::SampleCount) ==
            std::string_view("Sample Count"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::UpdateRate) ==
            std::string_view("Update Rate"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::SignalGroup) ==
            std::string_view("Signal Group"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::DirectReservoir) ==
            std::string_view("Direct Reservoir"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::IndirectReservoir) ==
            std::string_view("Indirect Reservoir"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::PrimaryTransport) ==
            std::string_view("Primary Transport"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::IndirectTransport) ==
            std::string_view("Indirect Transport"));

        constexpr auto InvalidLighting =
            static_cast<LightingSolution>(255u);
        constexpr auto InvalidSolver =
            static_cast<PathTracingSolver>(255u);
        constexpr auto InvalidNee =
            static_cast<PathTracingNeeMode>(255u);
        constexpr auto InvalidDenoiser =
            static_cast<PathTracingDenoiser>(255u);
        constexpr auto InvalidDebug =
            static_cast<PathTracingDebugView>(255u);
        static_assert(!IsValidLightingSolution(InvalidLighting));
        static_assert(!IsValidPathTracingSolver(InvalidSolver));
        static_assert(!IsValidPathTracingNeeMode(InvalidNee));
        static_assert(!IsValidPathTracingDenoiser(InvalidDenoiser));
        static_assert(!IsValidPathTracingDebugView(InvalidDebug));
        static_assert(GetLightingSolutionLabel(InvalidLighting)[0] == '\0');
        static_assert(GetPathTracingSolverLabel(InvalidSolver)[0] == '\0');
        static_assert(GetPathTracingNeeModeLabel(InvalidNee)[0] == '\0');
        static_assert(GetPathTracingDenoiserLabel(InvalidDenoiser)[0] == '\0');
        static_assert(GetPathTracingDebugViewLabel(InvalidDebug)[0] == '\0');
    }

    void TestSolverPresets()
    {
        constexpr PathTracingSettings RtxPt =
            ApplyPathTracingSolverPreset(PathTracingSolver::RtxPt);
        static_assert(RtxPt.solver == PathTracingSolver::RtxPt);
        static_assert(!RtxPt.useRtxdi);
        static_assert(!RtxPt.temporalReuse);
        static_assert(RtxPt.spatialNeighborCount == 0u);
        static_assert(!UsesDirectReservoirHistory(RtxPt));
        static_assert(!UsesPathSeedHistory(RtxPt));
        static_assert(!UsesGiCheckpointHistory(RtxPt));
        static_assert(IsValidPathTracingSettings(RtxPt));

        constexpr PathTracingSettings RestirPt =
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirPt);
        static_assert(RestirPt.solver == PathTracingSolver::RestirPt);
        static_assert(RestirPt.neeMode == PathTracingNeeMode::Uniform);
        static_assert(RestirPt.maxBounces == 4u);
        static_assert(RestirPt.useRussianRoulette);
        static_assert(GetAutomaticRussianRouletteStart(RestirPt) == 3u);
        static_assert(RestirPt.neeCandidateCount == 1u);
        static_assert(!RestirPt.useRtxdi);
        static_assert(RestirPt.temporalReuse);
        static_assert(RestirPt.spatialNeighborCount == 1u);
        static_assert(!UsesDirectReservoirHistory(RestirPt));
        static_assert(UsesPathSeedHistory(RestirPt));
        static_assert(RestirPt.stablePlaneCount == 2u);
        static_assert(!UsesGiCheckpointHistory(RestirPt));
        static_assert(IsValidPathTracingSettings(RestirPt));

        constexpr PathTracingSettings RestirGi =
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirGi);
        static_assert(RestirGi.solver == PathTracingSolver::RestirGi);
        static_assert(RestirGi.neeMode == PathTracingNeeMode::Uniform);
        static_assert(RestirGi.neeCandidateCount == 1u);
        static_assert(!RestirGi.useRtxdi);
        static_assert(RestirGi.temporalReuse);
        static_assert(RestirGi.spatialNeighborCount == 0u);
        static_assert(!UsesDirectReservoirHistory(RestirGi));
        static_assert(!UsesPathSeedHistory(RestirGi));
        static_assert(UsesGiCheckpointHistory(RestirGi));
        static_assert(RestirGi.stablePlaneCount == 2u);
        static_assert(IsValidPathTracingSettings(RestirGi));

        constexpr PathTracingSettings Invalid =
            ApplyPathTracingSolverPreset(
                static_cast<PathTracingSolver>(255u));
        static_assert(Invalid == RtxPt);

        PathTracingSettings Editable = RestirPt;
        Editable.maxBounces = 12u;
        Editable.useSer = true;
        assert(Editable.maxBounces == 12u && Editable.useSer);
        assert(IsValidPathTracingSettings(Editable));
    }

    void TestSanitizationAndLimits()
    {
        PathTracingSettings Invalid;
        Invalid.solver = static_cast<PathTracingSolver>(255u);
        Invalid.neeMode = static_cast<PathTracingNeeMode>(255u);
        Invalid.maxBounces = 0u;
        Invalid.neeCandidateCount = 0u;
        Invalid.samplesPerPixel = 0u;
        Invalid.useRtxdi = false;
        Invalid.temporalReuse = true;
        Invalid.spatialNeighborCount =
            std::numeric_limits<uint32_t>::max();
        Invalid.stablePlaneCount = 4u;
        Invalid.spatialResolveStrength =
            std::numeric_limits<float>::quiet_NaN();
        Invalid.fireflyThreshold =
            std::numeric_limits<float>::quiet_NaN();
        Invalid.denoiser = static_cast<PathTracingDenoiser>(255u);
        Invalid.debugView = static_cast<PathTracingDebugView>(255u);
        assert(!IsValidPathTracingSettings(Invalid));

        const PathTracingSettings Sanitized =
            SanitizePathTracingSettings(Invalid);
        assert(IsValidPathTracingSettings(Sanitized));
        assert(Sanitized.solver == PathTracingSolver::RtxPt);
        assert(Sanitized.neeMode == PathTracingNeeMode::Uniform);
        assert(Sanitized.maxBounces == PathTracingMinBounceCount);
        assert(GetAutomaticRussianRouletteStart(Sanitized) ==
            Sanitized.maxBounces + 1u);
        assert(Sanitized.neeCandidateCount ==
            PathTracingMinNeeCandidateCount);
        assert(Sanitized.samplesPerPixel ==
            PathTracingMinSamplesPerPixel);
        // Sanitization preserves temporarily gated reuse preferences so a
        // parent solver or RTXDI toggle does not lose authored intent.
        assert(Sanitized.temporalReuse);
        assert(Sanitized.spatialNeighborCount ==
            PathTracingMaxSpatialNeighborCount);
        assert(Sanitized.stablePlaneCount ==
            PathTracingMaxStablePlaneCount);
        assert(Sanitized.spatialResolveStrength ==
            PathTracingDefaultSpatialResolveStrength);
        assert(Sanitized.fireflyThreshold ==
            PathTracingDefaultFireflyThreshold);
        assert(Sanitized.denoiser == PathTracingDenoiser::Raw);
        assert(Sanitized.debugView == PathTracingDebugView::FinalImage);

        Invalid = {};
        Invalid.maxBounces = std::numeric_limits<uint32_t>::max();
        Invalid.neeCandidateCount = std::numeric_limits<uint32_t>::max();
        Invalid.samplesPerPixel = std::numeric_limits<uint32_t>::max();
        Invalid.fireflyThreshold =
            std::numeric_limits<float>::infinity();
        const PathTracingSettings Maximum =
            SanitizePathTracingSettings(Invalid);
        assert(Maximum.maxBounces == PathTracingMaxBounceCount);
        assert(GetAutomaticRussianRouletteStart(Maximum) == 3u);
        assert(Maximum.neeCandidateCount ==
            PathTracingMaxNeeCandidateCount);
        assert(Maximum.samplesPerPixel ==
            PathTracingMaxSamplesPerPixel);
        assert(Maximum.fireflyThreshold ==
            PathTracingDefaultFireflyThreshold);

        Invalid = {};
        Invalid.fireflyThreshold = -100.f;
        assert(SanitizePathTracingSettings(Invalid).fireflyThreshold ==
            PathTracingMinFireflyThreshold);
        Invalid.fireflyThreshold = PathTracingMaxFireflyThreshold * 2.f;
        assert(SanitizePathTracingSettings(Invalid).fireflyThreshold ==
            PathTracingMaxFireflyThreshold);

        constexpr PathTracingSettings CompileTimeSanitized = []
        {
            PathTracingSettings settings;
            settings.maxBounces = 200u;
            return SanitizePathTracingSettings(settings);
        }();
        static_assert(CompileTimeSanitized.maxBounces == 96u);

        constexpr PathTracingSettings ThreeBounceRoulette = []
        {
            PathTracingSettings settings;
            settings.maxBounces = 3u;
            return settings;
        }();
        static_assert(GetAutomaticRussianRouletteStart(ThreeBounceRoulette) ==
            4u);
        constexpr PathTracingSettings DisabledRoulette = []
        {
            PathTracingSettings settings;
            settings.useRussianRoulette = false;
            return settings;
        }();
        static_assert(GetAutomaticRussianRouletteStart(DisabledRoulette) ==
            DisabledRoulette.maxBounces + 1u);
    }

    void TestEquality()
    {
        constexpr PathTracingSettings Left;
        constexpr PathTracingSettings Equal;
        static_assert(Left == Equal);
        static_assert(!(Left != Equal));

        PathTracingSettings Different = Left;
        Different.useSer = true;
        assert(Different != Left);
        Different = Left;
        Different.fireflyThreshold = 6.f;
        assert(Different != Left);
        Different = Left;
        Different.spatialResolveStrength = 0.5f;
        assert(Different != Left);
        Different = Left;
        Different.temporalReuse = true;
        Different.useRtxdi = true;
        assert(Different != Left);
        Different = Left;
        Different.samplesPerPixel = 3u;
        assert(Different != Left);
        Different = Left;
        Different.useRussianRoulette = false;
        assert(Different != Left);
        Different = Left;
        Different.sharedPrimarySurface = false;
        assert(Different != Left);
        Different = Left;
        Different.spatialNeighborCount = 1u;
        assert(Different != Left);
    }

    void TestPipelineResolution()
    {
        static_assert(PathTracingPipelineVariantCount == 18u);
        static_assert(PathTracingAllPipelineVariantsMask == 0x3ffffu);
        for (uint32_t solver = 0u; solver < PathTracingSolverCount; ++solver)
        {
            for (uint32_t rtxdi = 0u;
                rtxdi < PathTracingRtxdiModeCount;
                ++rtxdi)
            {
                for (uint32_t nee = 0u;
                    nee < PathTracingNeeModeCount;
                    ++nee)
                {
                    PathTracingSettings settings;
                    settings.solver = static_cast<PathTracingSolver>(solver);
                    settings.useRtxdi = rtxdi != 0u;
                    settings.neeMode = static_cast<PathTracingNeeMode>(nee);
                    const uint32_t expected = solver * 6u +
                        rtxdi * 3u + nee;
                    assert(GetPathTracingPipelineVariant(settings) ==
                        expected);
                    assert(IsPathTracingPipelineAvailable(
                        settings,
                        1u << expected));
                }
            }
        }

        constexpr PathTracingSettings RestirPt =
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirPt);

        constexpr PathTracingPipelineResolution Exact =
            ResolvePathTracingPipeline(
                RestirPt,
                PathTracingAllPipelineVariantsMask);
        static_assert(Exact.executable);
        static_assert(!Exact.fallbackApplied);
        static_assert(Exact.requestedVariant == 6u);
        static_assert(Exact.effectiveVariant == 6u);
        static_assert(
            Exact.effectiveSettings.solver == PathTracingSolver::RestirPt);
        static_assert(UsesPathSeedHistory(Exact.effectiveSettings));
        static_assert(!UsesDirectReservoirHistory(Exact.effectiveSettings));

        constexpr PathTracingPipelineResolution SameNeeWithoutRtxdi = []
        {
            PathTracingSettings settings = ApplyPathTracingSolverPreset(
                PathTracingSolver::RestirPt);
            settings.useRtxdi = true;
            settings.debugView = PathTracingDebugView::DirectReservoir;
            return ResolvePathTracingPipeline(settings, 1u << 6u);
        }();
        static_assert(SameNeeWithoutRtxdi.executable);
        static_assert(SameNeeWithoutRtxdi.fallbackApplied);
        static_assert(SameNeeWithoutRtxdi.requestedVariant == 9u);
        static_assert(SameNeeWithoutRtxdi.effectiveVariant == 6u);
        static_assert(!SameNeeWithoutRtxdi.effectiveSettings.useRtxdi);
        static_assert(
            SameNeeWithoutRtxdi.effectiveSettings.solver ==
                PathTracingSolver::RestirPt);
        static_assert(!UsesDirectReservoirHistory(
            SameNeeWithoutRtxdi.effectiveSettings));
        static_assert(UsesPathSeedHistory(
            SameNeeWithoutRtxdi.effectiveSettings));
        static_assert(!UsesGiCheckpointHistory(
            SameNeeWithoutRtxdi.effectiveSettings));
        static_assert(
            SameNeeWithoutRtxdi.effectiveSettings.neeMode ==
                PathTracingNeeMode::Uniform);
        static_assert(
            SameNeeWithoutRtxdi.effectiveSettings.debugView ==
                PathTracingDebugView::FinalImage);

        constexpr PathTracingSettings RestirPtPower = []
        {
            PathTracingSettings settings = ApplyPathTracingSolverPreset(
                PathTracingSolver::RestirPt);
            settings.neeMode = PathTracingNeeMode::Power;
            return settings;
        }();
        constexpr PathTracingPipelineResolution SameSolverUniform =
            ResolvePathTracingPipeline(RestirPtPower, 1u << 6u);
        static_assert(SameSolverUniform.executable);
        static_assert(SameSolverUniform.fallbackApplied);
        static_assert(SameSolverUniform.effectiveVariant == 6u);
        static_assert(
            SameSolverUniform.effectiveSettings.solver ==
                PathTracingSolver::RestirPt);
        static_assert(
            SameSolverUniform.effectiveSettings.neeMode ==
                PathTracingNeeMode::Uniform);

        constexpr PathTracingPipelineResolution RtxPtSameNee =
            ResolvePathTracingPipeline(RestirPtPower, 1u << 1u);
        static_assert(RtxPtSameNee.executable);
        static_assert(RtxPtSameNee.fallbackApplied);
        static_assert(RtxPtSameNee.effectiveVariant == 1u);
        static_assert(
            RtxPtSameNee.effectiveSettings.solver ==
                PathTracingSolver::RtxPt);
        static_assert(!UsesPathSeedHistory(
            RtxPtSameNee.effectiveSettings));

        constexpr PathTracingPipelineResolution Baseline =
            ResolvePathTracingPipeline(RestirPt, 0x01u);
        static_assert(Baseline.executable);
        static_assert(Baseline.fallbackApplied);
        static_assert(Baseline.effectiveVariant == 0u);
        static_assert(
            Baseline.effectiveSettings.solver == PathTracingSolver::RtxPt);
        static_assert(
            Baseline.effectiveSettings.neeMode ==
                PathTracingNeeMode::Uniform);
        static_assert(!Baseline.effectiveSettings.useRtxdi);
        static_assert(!UsesDirectReservoirHistory(
            Baseline.effectiveSettings));
        static_assert(!UsesPathSeedHistory(Baseline.effectiveSettings));
        static_assert(!UsesGiCheckpointHistory(
            Baseline.effectiveSettings));

        constexpr PathTracingPipelineResolution Unsupported =
            ResolvePathTracingPipeline(RestirPt, 0u);
        static_assert(!Unsupported.executable);
        static_assert(Unsupported.fallbackApplied);
    }

    void TestAccumulationMath()
    {
        constexpr uint32_t Maximum =
            std::numeric_limits<uint32_t>::max();
        static_assert(SaturatingIncrementSuccessfulSampleCount(0u) == 1u);
        static_assert(SaturatingIncrementSuccessfulSampleCount(41u) == 42u);
        static_assert(SaturatingIncrementSuccessfulSampleCount(Maximum) ==
            Maximum);
        static_assert(GetAccumulationOnlineMeanWeight(0u) == 0.f);
        static_assert(GetAccumulationOnlineMeanWeight(1u) == 1.f);
        static_assert(GetAccumulationOnlineMeanWeight(2u) == 0.5f);
        assert(Near(GetAccumulationOnlineMeanWeight(4u), 0.25f));
        assert(GetAccumulationOnlineMeanWeight(Maximum) > 0.f);

        // Counting depends only on success, so successful black and miss
        // samples use this same transition and online-mean weight.
        const uint32_t newCount =
            SaturatingIncrementSuccessfulSampleCount(7u);
        assert(newCount == 8u);
        assert(Near(GetAccumulationOnlineMeanWeight(newCount), 0.125f));
    }

    void TestSpatialPathResolveCapability()
    {
        PathTracingSettings settings;
        settings.denoiser = PathTracingDenoiser::SpatialPathResolve;
        assert(IsSpatialPathResolveRequested(settings));
        assert(CanUseSpatialPathResolve(settings, true));
        assert(!CanUseSpatialPathResolve(settings, false));

        settings.stablePlaneCount = 3u;
        assert(CanUseSpatialPathResolve(settings, true));

        settings.solver = PathTracingSolver::RestirPt;
        assert(IsSpatialPathResolveRequested(settings));
        assert(!CanUseSpatialPathResolve(settings, true));
        const PathTracingSettings sanitized = SanitizePathTracingSettings(
            settings);
        assert(sanitized.stablePlaneCount == 2u);
        assert(CanUseSpatialPathResolve(sanitized, true));

        settings = sanitized;
        settings.solver = PathTracingSolver::RestirGi;
        assert(CanUseSpatialPathResolve(settings, true));

        settings.denoiser = PathTracingDenoiser::Raw;
        settings.stablePlaneCount = 0u;
        const PathTracingSettings raw = SanitizePathTracingSettings(settings);
        assert(raw.stablePlaneCount == 0u);
        assert(!IsSpatialPathResolveRequested(raw));
        assert(!CanUseSpatialPathResolve(raw, true));

        settings = {};
        settings.denoiser = PathTracingDenoiser::SpatialPathResolve;
        settings.stablePlaneCount = 0u;
        const PathTracingSettings restored =
            SanitizePathTracingSettings(settings);
        assert(restored.stablePlaneCount == 1u);
    }

}

int main()
{
    TestDefaults();
    TestLabelsAndEnumValidation();
    TestSolverPresets();
    TestSanitizationAndLimits();
    TestEquality();
    TestPipelineResolution();
    TestAccumulationMath();
    TestSpatialPathResolveCapability();
    return 0;
}
