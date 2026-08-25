#include "auto_exposure.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    int g_FailureCount = 0;

    void Require(bool condition, std::string_view contract)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << contract << '\n';
            ++g_FailureCount;
        }
    }

    void RequireNear(
        float actual,
        float expected,
        float tolerance,
        std::string_view contract)
    {
        Require(
            std::isfinite(actual) && std::isfinite(expected) &&
                std::abs(actual - expected) <= tolerance,
            contract);
    }

    void TestDefaultsAndSanitization()
    {
        using namespace uvsr;
        const AutoExposureSettings defaults;
        Require(!defaults.enabled, "auto exposure defaults off");
        RequireNear(defaults.exposureCompensationEV, 0.f, 0.f,
            "compensation defaults to zero EV");
        RequireNear(defaults.maximumBrighteningEV, 5.f, 0.f,
            "brightening defaults to five EV");
        RequireNear(defaults.maximumDarkeningEV, 2.f, 0.f,
            "darkening defaults to two EV");
        RequireNear(defaults.adjustmentPeriodSeconds, 0.2f, 0.f,
            "adaptation half-life defaults to 0.2 seconds");
        Require(AutoExposureHistogramBinCount == 256u,
            "metering retains 256 histogram bins");

        AutoExposureSettings requested;
        requested.enabled = true;
        requested.exposureCompensationEV = -100.f;
        requested.maximumBrighteningEV = 100.f;
        requested.maximumDarkeningEV = -1.f;
        requested.adjustmentPeriodSeconds = 100.f;
        AutoExposureSettings sanitized =
            SanitizeAutoExposureSettings(requested);
        Require(sanitized.enabled, "sanitization preserves enable state");
        RequireNear(sanitized.exposureCompensationEV, -18.f, 0.f,
            "compensation clamps to minimum");
        RequireNear(sanitized.maximumBrighteningEV, 16.f, 0.f,
            "brightening clamps to maximum");
        RequireNear(sanitized.maximumDarkeningEV, 0.f, 0.f,
            "darkening clamps to minimum");
        RequireNear(sanitized.adjustmentPeriodSeconds, 5.f, 0.f,
            "period clamps to maximum");

        requested.exposureCompensationEV =
            std::numeric_limits<float>::quiet_NaN();
        requested.maximumBrighteningEV =
            std::numeric_limits<float>::infinity();
        requested.maximumDarkeningEV =
            -std::numeric_limits<float>::infinity();
        requested.adjustmentPeriodSeconds =
            std::numeric_limits<float>::quiet_NaN();
        sanitized = SanitizeAutoExposureSettings(requested);
        RequireNear(sanitized.exposureCompensationEV, 0.f, 0.f,
            "invalid compensation uses default");
        RequireNear(sanitized.maximumBrighteningEV, 5.f, 0.f,
            "invalid brightening uses default");
        RequireNear(sanitized.maximumDarkeningEV, 2.f, 0.f,
            "invalid darkening uses default");
        RequireNear(sanitized.adjustmentPeriodSeconds, 0.2f, 0.f,
            "invalid period uses default");
    }

    void TestMeteringAndAdaptation()
    {
        using namespace uvsr;
        RequireNear(ResolveAutoExposureTarget(0.18f), 1.f, 1e-6f,
            "middle gray resolves to unity");
        RequireNear(ResolveAutoExposureTarget(0.09f), 2.f, 1e-6f,
            "half luminance adds one stop");
        RequireNear(ResolveAutoExposureTarget(0.36f), 0.5f, 1e-6f,
            "double luminance removes one stop");
        RequireNear(
            ResolveAutoExposureTarget(
                AutoExposureMiddleGray / std::exp2(24.f)),
            std::exp2(5.f),
            1e-2f,
            "dark target respects brightening bound");
        RequireNear(
            ResolveAutoExposureTarget(
                AutoExposureMiddleGray * std::exp2(24.f)),
            std::exp2(-2.f),
            1e-7f,
            "bright target respects darkening bound");

        AutoExposureSettings bounds;
        bounds.maximumBrighteningEV = 2.f;
        bounds.maximumDarkeningEV = 3.f;
        bounds.exposureCompensationEV = 1.f;
        const float automatic = ResolveAutoExposureTarget(
            AutoExposureMiddleGray / std::exp2(8.f), bounds);
        RequireNear(automatic, 4.f, 1e-6f,
            "custom automatic bound is independent");
        RequireNear(
            automatic * std::exp2(bounds.exposureCompensationEV),
            8.f,
            1e-6f,
            "compensation remains outside automatic bounds");

        for (const float invalid : {
            0.f,
            -1.f,
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity() })
        {
            RequireNear(ResolveAutoExposureTarget(invalid), 1.f, 0.f,
                "invalid metering fails to unity");
        }

        constexpr float Period =
            AutoExposureDefaultAdjustmentPeriodSeconds;
        RequireNear(
            ResolveAdaptedExposure(1.f, 16.f, Period, Period),
            4.f,
            1e-6f,
            "one period halves an increasing EV gap");
        RequireNear(
            ResolveAdaptedExposure(16.f, 1.f, Period, Period),
            4.f,
            1e-6f,
            "one period halves a decreasing EV gap");
        const float halfStep = ResolveAdaptedExposure(
            0.5f, 8.f, Period * 0.5f, Period);
        RequireNear(
            ResolveAdaptedExposure(
                halfStep, 8.f, Period * 0.5f, Period),
            ResolveAdaptedExposure(0.5f, 8.f, Period, Period),
            2e-6f,
            "adaptation is invariant to frame subdivision");
        RequireNear(
            ResolveAdaptedExposure(1.f, 3.f, -1.f, Period),
            1.f,
            0.f,
            "negative delta preserves history");
        RequireNear(
            ResolveAdaptedExposure(
                std::numeric_limits<float>::quiet_NaN(),
                2.5f,
                0.1f,
                Period),
            2.5f,
            0.f,
            "invalid history snaps to target");
    }

    void TestFrameStateTransitions()
    {
        using namespace uvsr;
        AutoExposureSettings settings;
        AutoExposureFrameHistory history;

        const auto expectUnity = [&]
        (
            bool enabled,
            bool diagnostic,
            bool resources,
            bool scene,
            std::string_view contract)
        {
            settings.enabled = enabled;
            history = { false, true, true };
            const AutoExposureFrameDecision decision =
                BeginAutoExposureFrame(
                    history, settings, diagnostic, resources, scene);
            Require(
                !decision.dispatch && !decision.resetExposure &&
                    history.resetRequested && !history.wasEnabled &&
                    !history.exposureInitialized,
                contract);
        };
        expectUnity(false, false, true, true,
            "disabled exposure abandons history to unity");
        expectUnity(true, true, true, true,
            "diagnostic view abandons history to unity");
        expectUnity(true, false, false, true,
            "missing GPU resources abandon history to unity");
        expectUnity(true, false, true, false,
            "missing scene input abandons history to unity");

        settings.enabled = true;
        history = {};
        AutoExposureFrameDecision decision = BeginAutoExposureFrame(
            history, settings, false, true, true);
        Require(decision.dispatch && decision.resetExposure,
            "first automatic frame dispatches with reset");
        Require(!history.resetRequested && history.wasEnabled &&
                !history.exposureInitialized,
            "begin records an uncommitted automatic frame");

        CompleteAutoExposureFrame(history, true);
        decision = BeginAutoExposureFrame(
            history, settings, false, true, true);
        Require(decision.dispatch && !decision.resetExposure,
            "successful history continues without reset");

        RequestAutoExposureReset(history);
        decision = BeginAutoExposureFrame(
            history, settings, false, true, true);
        Require(decision.dispatch && decision.resetExposure,
            "explicit reset affects the next successful frame exactly once");
        CompleteAutoExposureFrame(history, true);
        decision = BeginAutoExposureFrame(
            history, settings, false, true, true);
        Require(decision.dispatch && !decision.resetExposure,
            "completed explicit reset returns to continuous history");

        CompleteAutoExposureFrame(history, false);
        Require(history.resetRequested && !history.wasEnabled &&
                !history.exposureInitialized,
            "failed histogram dispatch abandons partial history");
        decision = BeginAutoExposureFrame(
            history, settings, false, true, true);
        Require(decision.dispatch && decision.resetExposure,
            "recovery after failed dispatch starts from reset");
    }

    using Color3 = std::array<float, 3>;

    Color3 Multiply(const std::array<Color3, 3>& matrix, const Color3& color)
    {
        Color3 result{};
        for (std::size_t row = 0u; row < result.size(); ++row)
        for (std::size_t column = 0u; column < color.size(); ++column)
            result[row] += matrix[row][column] * color[column];
        return result;
    }

    Color3 EvaluateEstablishedUnityAgx(Color3 color)
    {
        constexpr float MinimumEV = -12.47393f;
        constexpr float MaximumEV = 4.026069f;
        constexpr std::array<Color3, 3> Inset = {{
            { 0.842479062253094f, 0.0784335999999992f,
                0.0792237451477643f },
            { 0.0423282422610123f, 0.878468636469772f,
                0.0791661274605434f },
            { 0.0423756549057051f, 0.0784336f,
                0.879142973793104f }
        }};
        constexpr std::array<Color3, 3> Outset = {{
            { 1.19687900512017f, -0.0980208811401368f,
                -0.0990297440797205f },
            { -0.0528968517574562f, 1.15190312990417f,
                -0.0989611768448433f },
            { -0.0529716355144438f, -0.0980434501171241f,
                1.15107367264116f }
        }};
        const auto saturate = [](float value)
        {
            return std::fmax(0.f, std::fmin(1.f, value));
        };

        for (float& channel : color)
            channel = std::fmax(channel, 0.f);
        color = Multiply(Inset, color);
        for (float& channel : color)
        {
            channel = saturate(
                (std::log2(std::fmax(channel, 1e-10f)) - MinimumEV) /
                (MaximumEV - MinimumEV));
            const float x2 = channel * channel;
            const float x4 = x2 * x2;
            channel = saturate(
                15.5f * x4 * x2 - 40.14f * x4 * channel +
                31.96f * x4 - 6.868f * x2 * channel +
                0.4298f * x2 + 0.1191f * channel - 0.00232f);
        }
        color = Multiply(Outset, color);
        for (float& channel : color)
            channel = saturate(channel);
        return color;
    }

    void TestEstablishedUnityAgxGolden()
    {
        const Color3 neutral = EvaluateEstablishedUnityAgx(
            { 0.18f, 0.18f, 0.18f });
        RequireNear(neutral[0], 0.4966762f, 2e-6f,
            "AgX neutral red golden");
        RequireNear(neutral[1], 0.4967449f, 2e-6f,
            "AgX neutral green golden");
        RequireNear(neutral[2], 0.4967491f, 2e-6f,
            "AgX neutral blue golden");

        const Color3 red = EvaluateEstablishedUnityAgx({ 1.f, 0.f, 0.f });
        RequireNear(red[0], 0.8609764f, 2e-6f,
            "AgX saturated red golden");
        RequireNear(red[1], 0.2301589f, 2e-6f,
            "AgX saturated red green-channel golden");
        RequireNear(red[2], 0.2303248f, 2e-6f,
            "AgX saturated red blue-channel golden");
    }
}

int main()
{
    TestDefaultsAndSanitization();
    TestMeteringAndAdaptation();
    TestFrameStateTransitions();
    TestEstablishedUnityAgxGolden();

    if (g_FailureCount != 0)
    {
        std::cerr << g_FailureCount
                  << " auto-exposure behavior failure(s).\n";
        return EXIT_FAILURE;
    }
    std::cout << "Auto-exposure behavior passed.\n";
    return EXIT_SUCCESS;
}
