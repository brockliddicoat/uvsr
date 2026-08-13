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
        static_assert(Defaults.maxBounces == 8u);
        static_assert(Defaults.russianRouletteStart == 3u);
        static_assert(Defaults.neeCandidateCount == 1u);
        static_assert(!Defaults.useSer);
        static_assert(!Defaults.useRtxdi);
        static_assert(!Defaults.reuseDirectReservoirs);
        static_assert(!Defaults.reusePathReservoirs);
        static_assert(!Defaults.reuseIndirectGiReservoirs);
        static_assert(Defaults.stablePlaneCount == 0u);
        static_assert(!Defaults.usePsr);
        static_assert(!Defaults.enableFireflyFilter);
        static_assert(Defaults.fireflyThreshold == 5.f);
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
            std::string_view("RTX PT"));
        assert(GetPathTracingSolverLabel(PathTracingSolver::RestirPt) ==
            std::string_view("ReSTIR PT"));
        assert(GetPathTracingSolverLabel(PathTracingSolver::RestirGi) ==
            std::string_view("ReSTIR GI"));

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
            std::string_view("NEE-AT"));

        assert(IsValidPathTracingDenoiser(PathTracingDenoiser::Raw));
        assert(IsValidPathTracingDenoiser(
            PathTracingDenoiser::StablePlaneResolve));
        assert(IsValidPathTracingDenoiser(PathTracingDenoiser::NrdReblur));
        assert(IsValidPathTracingDenoiser(PathTracingDenoiser::NrdRelax));
        assert(GetPathTracingDenoiserLabel(PathTracingDenoiser::Raw) ==
            std::string_view("Raw (No Denoising)"));
        assert(GetPathTracingDenoiserLabel(
            PathTracingDenoiser::StablePlaneResolve) ==
            std::string_view("Stable Plane Resolve"));
        assert(GetPathTracingDenoiserLabel(PathTracingDenoiser::NrdReblur) ==
            std::string_view("NRD ReBLUR"));
        assert(GetPathTracingDenoiserLabel(PathTracingDenoiser::NrdRelax) ==
            std::string_view("NRD ReLAX"));

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
            PathTracingDebugView::RetryProbability));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::StablePlane));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::DirectReservoir));
        assert(IsValidPathTracingDebugView(
            PathTracingDebugView::IndirectReservoir));
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
            PathTracingDebugView::RetryProbability) ==
            std::string_view("Retry Probability"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::StablePlane) ==
            std::string_view("Stable Plane"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::DirectReservoir) ==
            std::string_view("Direct Reservoir"));
        assert(GetPathTracingDebugViewLabel(
            PathTracingDebugView::IndirectReservoir) ==
            std::string_view("Indirect Reservoir"));

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
        static_assert(!RtxPt.reuseDirectReservoirs);
        static_assert(!RtxPt.reusePathReservoirs);
        static_assert(!RtxPt.reuseIndirectGiReservoirs);
        static_assert(IsValidPathTracingSettings(RtxPt));

        constexpr PathTracingSettings RestirPt =
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirPt);
        static_assert(RestirPt.solver == PathTracingSolver::RestirPt);
        static_assert(RestirPt.neeMode == PathTracingNeeMode::Power);
        static_assert(RestirPt.maxBounces == 3u);
        static_assert(RestirPt.russianRouletteStart == 3u);
        static_assert(RestirPt.neeCandidateCount == 1u);
        static_assert(!RestirPt.useRtxdi);
        static_assert(RestirPt.reuseDirectReservoirs);
        static_assert(RestirPt.reusePathReservoirs);
        static_assert(!RestirPt.reuseIndirectGiReservoirs);
        static_assert(IsValidPathTracingSettings(RestirPt));

        constexpr PathTracingSettings RestirGi =
            ApplyPathTracingSolverPreset(PathTracingSolver::RestirGi);
        static_assert(RestirGi.solver == PathTracingSolver::RestirGi);
        static_assert(RestirGi.neeMode == PathTracingNeeMode::Power);
        static_assert(RestirGi.neeCandidateCount == 1u);
        static_assert(!RestirGi.useRtxdi);
        static_assert(RestirGi.reuseDirectReservoirs);
        static_assert(!RestirGi.reusePathReservoirs);
        static_assert(RestirGi.reuseIndirectGiReservoirs);
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
        Invalid.russianRouletteStart = std::numeric_limits<uint32_t>::max();
        Invalid.neeCandidateCount = 0u;
        Invalid.useRtxdi = false;
        Invalid.reuseDirectReservoirs = true;
        Invalid.reusePathReservoirs = true;
        Invalid.reuseIndirectGiReservoirs = true;
        Invalid.stablePlaneCount = 4u;
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
        assert(Sanitized.russianRouletteStart ==
            PathTracingMinBounceCount);
        assert(Sanitized.neeCandidateCount ==
            PathTracingMinNeeCandidateCount);
        // Sanitization preserves temporarily gated RTXDI sub-options so
        // toggling the parent feature off and back on does not lose intent.
        assert(Sanitized.reuseDirectReservoirs);
        assert(Sanitized.reusePathReservoirs);
        assert(Sanitized.reuseIndirectGiReservoirs);
        assert(Sanitized.stablePlaneCount ==
            PathTracingMaxStablePlaneCount);
        assert(Sanitized.fireflyThreshold ==
            PathTracingDefaultFireflyThreshold);
        assert(Sanitized.denoiser == PathTracingDenoiser::Raw);
        assert(Sanitized.debugView == PathTracingDebugView::FinalImage);

        Invalid = {};
        Invalid.maxBounces = std::numeric_limits<uint32_t>::max();
        Invalid.russianRouletteStart = std::numeric_limits<uint32_t>::max();
        Invalid.neeCandidateCount = std::numeric_limits<uint32_t>::max();
        Invalid.fireflyThreshold =
            std::numeric_limits<float>::infinity();
        const PathTracingSettings Maximum =
            SanitizePathTracingSettings(Invalid);
        assert(Maximum.maxBounces == PathTracingMaxBounceCount);
        assert(Maximum.russianRouletteStart == PathTracingMaxBounceCount);
        assert(Maximum.neeCandidateCount ==
            PathTracingMaxNeeCandidateCount);
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
            settings.russianRouletteStart = 200u;
            return SanitizePathTracingSettings(settings);
        }();
        static_assert(CompileTimeSanitized.maxBounces == 96u);
        static_assert(CompileTimeSanitized.russianRouletteStart == 96u);
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
        Different.reuseDirectReservoirs = true;
        Different.useRtxdi = true;
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
        static_assert(Exact.requestedVariant == 7u);
        static_assert(Exact.effectiveVariant == 7u);
        static_assert(
            Exact.effectiveSettings.solver == PathTracingSolver::RestirPt);
        static_assert(Exact.effectiveSettings.reusePathReservoirs);
        static_assert(!Exact.effectiveSettings.reuseDirectReservoirs);

        constexpr PathTracingPipelineResolution SameNeeWithoutRtxdi = []
        {
            PathTracingSettings settings = ApplyPathTracingSolverPreset(
                PathTracingSolver::RestirPt);
            settings.useRtxdi = true;
            settings.debugView = PathTracingDebugView::DirectReservoir;
            return ResolvePathTracingPipeline(settings, 1u << 7u);
        }();
        static_assert(SameNeeWithoutRtxdi.executable);
        static_assert(SameNeeWithoutRtxdi.fallbackApplied);
        static_assert(SameNeeWithoutRtxdi.requestedVariant == 10u);
        static_assert(SameNeeWithoutRtxdi.effectiveVariant == 7u);
        static_assert(!SameNeeWithoutRtxdi.effectiveSettings.useRtxdi);
        static_assert(
            SameNeeWithoutRtxdi.effectiveSettings.solver ==
                PathTracingSolver::RestirPt);
        static_assert(
            !SameNeeWithoutRtxdi.effectiveSettings.reuseDirectReservoirs);
        static_assert(
            SameNeeWithoutRtxdi.effectiveSettings.reusePathReservoirs);
        static_assert(
            !SameNeeWithoutRtxdi.effectiveSettings.reuseIndirectGiReservoirs);
        static_assert(
            SameNeeWithoutRtxdi.effectiveSettings.neeMode ==
                PathTracingNeeMode::Power);
        static_assert(
            SameNeeWithoutRtxdi.effectiveSettings.debugView ==
                PathTracingDebugView::FinalImage);

        constexpr PathTracingPipelineResolution SameSolverUniform =
            ResolvePathTracingPipeline(RestirPt, 1u << 6u);
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
            ResolvePathTracingPipeline(RestirPt, 1u << 1u);
        static_assert(RtxPtSameNee.executable);
        static_assert(RtxPtSameNee.fallbackApplied);
        static_assert(RtxPtSameNee.effectiveVariant == 1u);
        static_assert(
            RtxPtSameNee.effectiveSettings.solver ==
                PathTracingSolver::RtxPt);
        static_assert(!RtxPtSameNee.effectiveSettings.reusePathReservoirs);

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
        static_assert(!Baseline.effectiveSettings.reuseDirectReservoirs);
        static_assert(!Baseline.effectiveSettings.reusePathReservoirs);
        static_assert(!Baseline.effectiveSettings.reuseIndirectGiReservoirs);

        constexpr PathTracingPipelineResolution Unsupported =
            ResolvePathTracingPipeline(RestirPt, 0u);
        static_assert(!Unsupported.executable);
        static_assert(Unsupported.fallbackApplied);
    }

    void TestAccumulationMath()
    {
        static_assert(GetAccumulationRetryProbability(0u) == 1.f);
        static_assert(GetAccumulationRetryProbability(1u) == 0.5f);
        static_assert(GetAccumulationRetryProbability(3u) == 0.25f);

        float previousProbability =
            GetAccumulationRetryProbability(0u);
        for (uint32_t count = 1u; count < 100u; ++count)
        {
            const float probability =
                GetAccumulationRetryProbability(count);
            assert(probability > 0.f);
            assert(probability < previousProbability);
            previousProbability = probability;
        }

        assert(ShouldAttemptAccumulationSample(0u, 0.f));
        assert(ShouldAttemptAccumulationSample(0u, 0.999999f));
        assert(ShouldAttemptAccumulationSample(
            0u,
            std::numeric_limits<float>::quiet_NaN()));
        assert(ShouldAttemptAccumulationSample(1u, 0.f));
        assert(ShouldAttemptAccumulationSample(1u, 0.499999f));
        assert(!ShouldAttemptAccumulationSample(1u, 0.5f));
        assert(!ShouldAttemptAccumulationSample(1u, 0.999999f));
        assert(!ShouldAttemptAccumulationSample(1u, -0.1f));
        assert(!ShouldAttemptAccumulationSample(1u, 1.f));
        assert(!ShouldAttemptAccumulationSample(
            1u,
            std::numeric_limits<float>::quiet_NaN()));

        constexpr uint32_t Maximum =
            std::numeric_limits<uint32_t>::max();
        assert(GetAccumulationRetryProbability(Maximum) > 0.f);
        assert(ShouldAttemptAccumulationSample(Maximum, 0.f));
        assert(!ShouldAttemptAccumulationSample(
            Maximum,
            GetAccumulationRetryProbability(Maximum)));

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

    void TestStablePlaneResolveCapability()
    {
        PathTracingSettings settings;
        assert(!IsStablePlaneResolveRequested(settings));
        assert(!CanUseStablePlaneResolve(settings, true));

        settings.denoiser = PathTracingDenoiser::StablePlaneResolve;
        settings.stablePlaneCount = 0u;
        const PathTracingSettings sanitized =
            SanitizePathTracingSettings(settings);
        assert(sanitized.stablePlaneCount == 1u);
        assert(IsStablePlaneResolveRequested(sanitized));
        assert(CanUseStablePlaneResolve(sanitized, true));
        assert(!CanUseStablePlaneResolve(sanitized, false));

        settings = sanitized;
        settings.solver = PathTracingSolver::RestirPt;
        assert(IsStablePlaneResolveRequested(settings));
        assert(!CanUseStablePlaneResolve(settings, true));
        settings.solver = PathTracingSolver::RestirGi;
        assert(!CanUseStablePlaneResolve(settings, true));
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
    TestStablePlaneResolveCapability();
    return 0;
}
