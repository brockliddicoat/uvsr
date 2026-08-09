#include "auto_exposure.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

namespace
{
    int g_FailureCount = 0;

    void Require(bool condition, std::string_view contract)
    {
        if (condition)
            return;

        std::cerr << "FAIL: " << contract << '\n';
        ++g_FailureCount;
    }

    void RequireNear(
        float actual,
        float expected,
        float tolerance,
        std::string_view contract)
    {
        if (std::isfinite(actual) && std::isfinite(expected) &&
            std::abs(actual - expected) <= tolerance)
        {
            return;
        }

        std::cerr << "FAIL: " << contract << " (actual " << actual
                  << ", expected " << expected << ", tolerance "
                  << tolerance << ")\n";
        ++g_FailureCount;
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.good())
        {
            std::cerr << "FAIL: cannot open " << path.generic_string()
                      << '\n';
            ++g_FailureCount;
            return {};
        }

        std::string source{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
        if (source.empty())
        {
            std::cerr << "FAIL: source is empty: "
                      << path.generic_string() << '\n';
            ++g_FailureCount;
        }
        return source;
    }

    std::string Compact(std::string_view source)
    {
        std::string result;
        result.reserve(source.size());
        for (const char character : source)
        {
            if (character != ' ' && character != '\t' &&
                character != '\r' && character != '\n')
            {
                result.push_back(character);
            }
        }
        return result;
    }

    void RequireContains(
        std::string_view source,
        std::string_view required,
        std::string_view contract)
    {
        if (source.find(required) != std::string_view::npos)
            return;

        std::cerr << "FAIL: " << contract << " is missing '"
                  << required << "'.\n";
        ++g_FailureCount;
    }

    void RequireAbsent(
        std::string_view source,
        std::string_view forbidden,
        std::string_view contract)
    {
        if (source.find(forbidden) == std::string_view::npos)
            return;

        std::cerr << "FAIL: " << contract << " must not contain '"
                  << forbidden << "'.\n";
        ++g_FailureCount;
    }

    template<size_t Count>
    void RequireOrdered(
        std::string_view source,
        const std::string_view (&required)[Count],
        std::string_view contract)
    {
        size_t cursor = 0u;
        for (const std::string_view value : required)
        {
            const size_t position = source.find(value, cursor);
            if (position == std::string_view::npos)
            {
                std::cerr << "FAIL: " << contract
                          << " is missing ordered value '" << value
                          << "'.\n";
                ++g_FailureCount;
                return;
            }
            cursor = position + value.size();
        }
    }

    void RequireOccurrenceCount(
        std::string_view source,
        std::string_view value,
        size_t expectedCount,
        std::string_view contract)
    {
        size_t count = 0u;
        size_t cursor = 0u;
        while ((cursor = source.find(value, cursor)) !=
            std::string_view::npos)
        {
            ++count;
            cursor += value.size();
        }

        if (count == expectedCount)
            return;

        std::cerr << "FAIL: " << contract << " contains '" << value
                  << "' " << count << " time(s), expected "
                  << expectedCount << ".\n";
        ++g_FailureCount;
    }

    size_t FindMatchingBrace(std::string_view source, size_t openBrace)
    {
        if (openBrace == std::string_view::npos ||
            openBrace >= source.size() || source[openBrace] != '{')
        {
            return std::string_view::npos;
        }

        size_t depth = 0u;
        for (size_t position = openBrace; position < source.size(); ++position)
        {
            if (source[position] == '{')
            {
                ++depth;
            }
            else if (source[position] == '}')
            {
                --depth;
                if (depth == 0u)
                    return position;
            }
        }
        return std::string_view::npos;
    }

    using Color3 = std::array<float, 3>;

    Color3 Multiply(const std::array<Color3, 3>& matrix, const Color3& color)
    {
        Color3 result = {};
        for (size_t row = 0u; row < result.size(); ++row)
        {
            for (size_t column = 0u; column < color.size(); ++column)
                result[row] += matrix[row][column] * color[column];
        }
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

    void TestDefaultsAndSanitization()
    {
        using namespace uvsr;

        const AutoExposureSettings defaults;
        Require(!defaults.enabled, "auto exposure must default off");
        RequireNear(
            defaults.exposureCompensationEV,
            0.f,
            0.f,
            "Exposure Compensation must default to zero EV");
        RequireNear(
            AutoExposureMinimumCompensationEV,
            -18.f,
            0.f,
            "Exposure Compensation minimum must remain -18 EV");
        RequireNear(
            AutoExposureMaximumCompensationEV,
            8.f,
            0.f,
            "Exposure Compensation maximum must remain +8 EV");
        RequireNear(
            defaults.maximumBrighteningEV,
            5.f,
            0.f,
            "Maximum Brightening must default to +5 EV");
        RequireNear(
            defaults.maximumDarkeningEV,
            2.f,
            0.f,
            "Maximum Darkening must default to -2 EV");
        RequireNear(
            AutoExposureMinimumMovementEV,
            0.f,
            0.f,
            "automatic exposure movement limits must start at zero EV");
        RequireNear(
            AutoExposureMaximumMovementEV,
            16.f,
            0.f,
            "automatic exposure movement limits must end at 16 EV");
        RequireNear(
            defaults.adjustmentPeriodSeconds,
            0.2f,
            0.f,
            "Adjustment Period must default to 0.20 seconds");
        RequireNear(
            AutoExposureMinimumAdjustmentPeriodSeconds,
            0.05f,
            0.f,
            "Adjustment Period minimum must remain 0.05 seconds");
        RequireNear(
            AutoExposureMaximumAdjustmentPeriodSeconds,
            5.f,
            0.f,
            "Adjustment Period maximum must remain 5 seconds");
        Require(
            AutoExposureHistogramBinCount == 256u,
            "auto exposure must use 256 histogram bins");

        AutoExposureSettings requested;
        requested.enabled = true;
        requested.exposureCompensationEV = 3.25f;
        requested.maximumBrighteningEV = 4.5f;
        requested.maximumDarkeningEV = 6.5f;
        AutoExposureSettings sanitized =
            SanitizeAutoExposureSettings(requested);
        Require(
            sanitized.enabled,
            "sanitization must preserve the enabled state");
        RequireNear(
            sanitized.exposureCompensationEV,
            3.25f,
            0.f,
            "sanitization must preserve finite Exposure Compensation");
        RequireNear(
            sanitized.maximumBrighteningEV,
            4.5f,
            0.f,
            "sanitization must preserve finite Maximum Brightening");
        RequireNear(
            sanitized.maximumDarkeningEV,
            6.5f,
            0.f,
            "sanitization must preserve finite Maximum Darkening");

        requested.exposureCompensationEV = -100.f;
        sanitized = SanitizeAutoExposureSettings(requested);
        RequireNear(
            sanitized.exposureCompensationEV,
            -18.f,
            0.f,
            "Exposure Compensation below the domain must clamp to -18 EV");

        requested.exposureCompensationEV = 100.f;
        sanitized = SanitizeAutoExposureSettings(requested);
        RequireNear(
            sanitized.exposureCompensationEV,
            8.f,
            0.f,
            "Exposure Compensation above the domain must clamp to +8 EV");

        for (const float invalid : {
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity() })
        {
            requested.exposureCompensationEV = invalid;
            sanitized = SanitizeAutoExposureSettings(requested);
            RequireNear(
                sanitized.exposureCompensationEV,
                0.f,
                0.f,
                "non-finite Exposure Compensation must fall back to zero EV");
        }

        requested.maximumBrighteningEV = -1.f;
        requested.maximumDarkeningEV = 100.f;
        sanitized = SanitizeAutoExposureSettings(requested);
        RequireNear(
            sanitized.maximumBrighteningEV,
            0.f,
            0.f,
            "Maximum Brightening below the domain must clamp to zero EV");
        RequireNear(
            sanitized.maximumDarkeningEV,
            16.f,
            0.f,
            "Maximum Darkening above the domain must clamp to 16 EV");

        for (const float invalid : {
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity() })
        {
            requested.maximumBrighteningEV = invalid;
            requested.maximumDarkeningEV = invalid;
            sanitized = SanitizeAutoExposureSettings(requested);
            RequireNear(
                sanitized.maximumBrighteningEV,
                5.f,
                0.f,
                "non-finite Maximum Brightening must use its logical default");
            RequireNear(
                sanitized.maximumDarkeningEV,
                2.f,
                0.f,
                "non-finite Maximum Darkening must use its logical default");
        }

        AutoExposureSettings periodRequest;
        periodRequest.adjustmentPeriodSeconds = 1.25f;
        sanitized = SanitizeAutoExposureSettings(periodRequest);
        RequireNear(
            sanitized.adjustmentPeriodSeconds,
            1.25f,
            0.f,
            "sanitization must preserve an in-range Adjustment Period");

        periodRequest.adjustmentPeriodSeconds = -100.f;
        sanitized = SanitizeAutoExposureSettings(periodRequest);
        RequireNear(
            sanitized.adjustmentPeriodSeconds,
            0.05f,
            0.f,
            "Adjustment Period values below the domain must clamp to 0.05 seconds");

        periodRequest.adjustmentPeriodSeconds = 100.f;
        sanitized = SanitizeAutoExposureSettings(periodRequest);
        RequireNear(
            sanitized.adjustmentPeriodSeconds,
            5.f,
            0.f,
            "Adjustment Period values above the domain must clamp to 5 seconds");

        for (const float invalid : {
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity() })
        {
            periodRequest.adjustmentPeriodSeconds = invalid;
            sanitized = SanitizeAutoExposureSettings(periodRequest);
            RequireNear(
                sanitized.adjustmentPeriodSeconds,
                0.2f,
                0.f,
                "non-finite Adjustment Period must fall back to 0.20 seconds");
        }
    }

    void TestMeteringTargetAndExposureCompensation()
    {
        using namespace uvsr;

        RequireNear(
            ResolveAutoExposureTarget(0.18f),
            1.f,
            1e-6f,
            "18 percent middle gray must resolve to unity exposure");
        RequireNear(
            ResolveAutoExposureTarget(0.09f),
            2.f,
            1e-6f,
            "halving metered luminance must add one exposure stop");
        RequireNear(
            ResolveAutoExposureTarget(0.36f),
            0.5f,
            1e-6f,
            "doubling metered luminance must remove one exposure stop");
        RequireNear(
            ResolveAutoExposureTarget(
                AutoExposureMiddleGray / std::exp2(24.f)),
            std::exp2(5.f),
            1e-2f,
            "dark metering must clamp target exposure to default +5 EV");
        RequireNear(
            ResolveAutoExposureTarget(
                AutoExposureMiddleGray / std::exp2(-24.f)),
            std::exp2(-2.f),
            1e-10f,
            "bright metering must clamp target exposure to default -2 EV");

        AutoExposureSettings bounded;
        bounded.maximumBrighteningEV = 2.f;
        bounded.maximumDarkeningEV = 3.f;
        RequireNear(
            ResolveAutoExposureTarget(
                AutoExposureMiddleGray / std::exp2(8.f),
                bounded),
            std::exp2(2.f),
            1e-6f,
            "Maximum Brightening must independently cap a dark-scene target");
        RequireNear(
            ResolveAutoExposureTarget(
                AutoExposureMiddleGray * std::exp2(8.f),
                bounded),
            std::exp2(-3.f),
            1e-7f,
            "Maximum Darkening must independently cap a bright-scene target");

        bounded.maximumBrighteningEV = 0.f;
        bounded.maximumDarkeningEV = 0.f;
        bounded.exposureCompensationEV = 2.f;
        const float boundedAutomaticExposure = ResolveAutoExposureTarget(
            AutoExposureMiddleGray / std::exp2(8.f),
            bounded);
        RequireNear(
            boundedAutomaticExposure,
            1.f,
            0.f,
            "zero movement limits must hold automatic exposure at unity");
        RequireNear(
            boundedAutomaticExposure *
                std::exp2(bounded.exposureCompensationEV),
            4.f,
            1e-6f,
            "Exposure Compensation must remain outside automatic limits");

        for (const float invalid : {
                0.f,
                -1.f,
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity() })
        {
            RequireNear(
                ResolveAutoExposureTarget(invalid),
                1.f,
                0.f,
                "invalid metered luminance must fail safely to unity");
        }

        constexpr float AdaptedExposure = 1.5f;
        RequireNear(
            AdaptedExposure * std::exp2(1.f),
            3.f,
            1e-6f,
            "+1 Exposure Compensation EV must double final exposure");
        RequireNear(
            AdaptedExposure * std::exp2(-2.f),
            0.375f,
            1e-6f,
            "-2 Exposure Compensation EV must quarter final exposure");
    }

    void TestTemporalAdaptation()
    {
        using namespace uvsr;

        constexpr float Period =
            AutoExposureDefaultAdjustmentPeriodSeconds;
        RequireNear(
            Period,
            0.2f,
            0.f,
            "default Adjustment Period must remain a 0.20-second half-life");
        RequireNear(
            ResolveAdaptedExposure(1.f, 3.f, 0.f, Period),
            1.f,
            0.f,
            "zero delta time must preserve previous exposure");
        RequireNear(
            ResolveAdaptedExposure(1.f, 3.f, -1.f, Period),
            1.f,
            0.f,
            "negative delta time must be treated as zero");
        RequireNear(
            ResolveAdaptedExposure(
                1.f,
                3.f,
                std::numeric_limits<float>::quiet_NaN(),
                Period),
            1.f,
            0.f,
            "non-finite delta time must preserve previous exposure");

        const float increasingExposure = ResolveAdaptedExposure(
            1.f,
            16.f,
            Period,
            Period);
        const float decreasingExposure = ResolveAdaptedExposure(
            16.f,
            1.f,
            Period,
            Period);
        RequireNear(
            increasingExposure,
            4.f,
            1e-6f,
            "one Adjustment Period must halve an increasing EV gap");
        RequireNear(
            decreasingExposure,
            4.f,
            1e-6f,
            "one Adjustment Period must halve a decreasing EV gap symmetrically");

        const float oneStep = ResolveAdaptedExposure(
            0.5f,
            8.f,
            Period,
            Period);
        const float firstHalfStep = ResolveAdaptedExposure(
            0.5f,
            8.f,
            Period * 0.5f,
            Period);
        const float twoHalfSteps = ResolveAdaptedExposure(
            firstHalfStep,
            8.f,
            Period * 0.5f,
            Period);
        RequireNear(
            twoHalfSteps,
            oneStep,
            2e-6f,
            "EV adaptation must be invariant to frame-time subdivision");

        RequireNear(
            ResolveAdaptedExposure(
                16.f,
                16.f,
                0.f,
                Period,
                1.f,
                16.f),
            2.f,
            1e-6f,
            "tightening Maximum Brightening must immediately bound history");
        RequireNear(
            ResolveAdaptedExposure(
                1.f / 16.f,
                1.f / 16.f,
                0.f,
                Period,
                16.f,
                2.f),
            0.25f,
            1e-7f,
            "tightening Maximum Darkening must immediately bound history");
        RequireNear(
            ResolveAdaptedExposure(
                1.f,
                16.f,
                Period,
                Period,
                1.f,
                16.f),
            std::sqrt(2.f),
            2e-6f,
            "bounded adaptation must remain symmetric in exposure-value space");

        RequireNear(
            ResolveAdaptedExposure(1.f, 16.f, 0.05f, -100.f),
            ResolveAdaptedExposure(
                1.f,
                16.f,
                0.05f,
                AutoExposureMinimumAdjustmentPeriodSeconds),
            0.f,
            "adaptation must clamp Adjustment Period below its minimum");
        RequireNear(
            ResolveAdaptedExposure(1.f, 16.f, 5.f, 100.f),
            ResolveAdaptedExposure(
                1.f,
                16.f,
                5.f,
                AutoExposureMaximumAdjustmentPeriodSeconds),
            0.f,
            "adaptation must clamp Adjustment Period above its maximum");
        RequireNear(
            ResolveAdaptedExposure(
                1.f,
                16.f,
                Period,
                std::numeric_limits<float>::quiet_NaN()),
            4.f,
            1e-6f,
            "non-finite Adjustment Period must use the default half-life");

        for (const float invalidPrevious : {
                0.f,
                -1.f,
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity() })
        {
            RequireNear(
                ResolveAdaptedExposure(
                    invalidPrevious,
                    2.5f,
                    0.1f,
                    Period),
                2.5f,
                0.f,
                "invalid previous exposure must snap to the current target");
        }
    }

    void TestEstablishedUnityAgxGolden()
    {
        const Color3 neutral = EvaluateEstablishedUnityAgx(
            { 0.18f, 0.18f, 0.18f });
        RequireNear(
            neutral[0],
            0.4966762f,
            2e-6f,
            "unity AgX neutral-red golden must retain established output");
        RequireNear(
            neutral[1],
            0.4967449f,
            2e-6f,
            "unity AgX neutral-green golden must retain established output");
        RequireNear(
            neutral[2],
            0.4967491f,
            2e-6f,
            "unity AgX neutral-blue golden must retain established output");

        const Color3 saturatedRed = EvaluateEstablishedUnityAgx(
            { 1.f, 0.f, 0.f });
        RequireNear(
            saturatedRed[0],
            0.8609764f,
            2e-6f,
            "unity AgX saturated-red golden must retain established red");
        RequireNear(
            saturatedRed[1],
            0.2301589f,
            2e-6f,
            "unity AgX saturated-red golden must retain established green");
        RequireNear(
            saturatedRed[2],
            0.2303248f,
            2e-6f,
            "unity AgX saturated-red golden must retain established blue");
    }

    void TestGpuSourceContracts(const std::filesystem::path& root)
    {
        const std::string histogram = Compact(ReadFile(
            root / "src" / "auto_exposure_histogram_cs.hlsl"));
        const std::string resolve = Compact(ReadFile(
            root / "src" / "auto_exposure_resolve_cs.hlsl"));
        const std::string pass = Compact(ReadFile(
            root / "src" / "auto_exposure.cpp"));
        const std::string constants = Compact(ReadFile(
            root / "src" / "auto_exposure_cb.h"));
        const std::string agx = Compact(ReadFile(
            root / "src" / "agx_tonemapping_ps.hlsl"));
        const std::string viewer = Compact(ReadFile(
            root / "src" / "uvsr.cpp"));
        const std::string shaderConfig = Compact(ReadFile(
            root / "src" / "shaders.cfg"));

        RequireContains(
            histogram,
            "RWBuffer<uint>u_Histogram:register(u0);",
            "histogram shader GPU-resident storage");
        RequireContains(
            histogram,
            "float3(0.2126f,0.7152f,0.0722f)",
            "histogram shader BT.709 luminance weights");
        constexpr std::string_view HistogramValidationOrder[] = {
            "constfloat3color=t_SceneColor.Load(",
            "if(!all(isfinite(color)))return;",
            "constfloatluminance=dot(max(color,0.0f),",
            "if(!isfinite(luminance)||!(luminance>1e-8f))return;",
            "constuintbin=min(uint(normalized*256.0f),255u);",
            "InterlockedAdd(u_Histogram[bin],1u);"
        };
        RequireOrdered(
            histogram,
            HistogramValidationOrder,
            "finite positive scene-linear luminance histogram accumulation");

        RequireContains(
            resolve,
            "Buffer<uint>t_Histogram:register(t0);",
            "resolve shader histogram SRV");
        RequireContains(
            resolve,
            "RWBuffer<float>u_Exposure:register(u0);",
            "resolve shader GPU-resident exposure output");
        constexpr std::string_view MedianOrder[] = {
            "for(uintbin=0u;bin<256u;++bin)validPixelCount+=t_Histogram[bin];",
            "constuintmedianRank=(validPixelCount+1u)/2u;",
            "cumulative+=t_Histogram[bin];",
            "if(cumulative>=medianRank)",
            "medianBin=bin;",
            "(float(medianBin)+0.5f)/256.0f;",
            "constfloatmeteredLuminance=exp2(lerp("
        };
        RequireOrdered(
            resolve,
            MedianOrder,
            "256-bin median luminance resolution");
        constexpr std::string_view ConstantFieldOrder[] = {
            "floatframeDeltaSeconds;",
            "floatexposureCompensationEV;",
            "floatadjustmentPeriodSeconds;",
            "uintresetExposure;",
            "floatmaximumBrighteningEV;",
            "floatmaximumDarkeningEV;",
            "float2padding;"
        };
        RequireOrdered(
            constants,
            ConstantFieldOrder,
            "bounded auto-exposure shared constant-buffer field ordering");
        constexpr std::string_view AdaptationOrder[] = {
            "constfloatminimumAutomaticEV=-max("
                "g_AutoExposure.maximumDarkeningEV,0.0f);",
            "constfloatmaximumAutomaticEV=max("
                "g_AutoExposure.maximumBrighteningEV,0.0f);",
            "constfloatboundedPreviousEV=clamp(log2(previousExposure),"
                "minimumAutomaticEV,maximumAutomaticEV);",
            "floatadaptedExposure=exp2(boundedPreviousEV);",
            "constfloattargetEV=clamp(log2(AutoExposureMiddleGray/"
                "max(meteredLuminance,1e-8f)),minimumAutomaticEV,"
                "maximumAutomaticEV);",
            "constfloattargetExposure=exp2(targetEV);",
            "if(g_AutoExposure.resetExposure!=0u){",
            "adaptedExposure=targetExposure;",
            "constfloatblend=1.0f-exp2(",
            "-max(g_AutoExposure.frameDeltaSeconds,0.0f)/",
            "max(g_AutoExposure.adjustmentPeriodSeconds,1e-4f));",
            "adaptedExposure=exp2(clamp(lerp(",
            "boundedPreviousEV,",
            "targetEV,",
            "saturate(blend)),",
            "minimumAutomaticEV,",
            "maximumAutomaticEV));"
        };
        RequireOrdered(
            resolve,
            AdaptationOrder,
            "independently bounded symmetric EV-domain adaptation");
        RequireContains(
            resolve,
            "elseif(g_AutoExposure.resetExposure!=0u){"
                "adaptedExposure=1.0f;}",
            "empty reset histogram unity fallback");
        constexpr std::string_view ExposureOutputOrder[] = {
            "u_Exposure[0]=adaptedExposure;",
            "u_Exposure[1]=adaptedExposure*exp2("
                "g_AutoExposure.exposureCompensationEV);"
        };
        RequireOrdered(
            resolve,
            ExposureOutputOrder,
            "automatic bounds before final Exposure Compensation");

        RequireContains(
            pass,
            "histogramDescription.byteSize=sizeof(uint32_t)*"
                "AutoExposureHistogramBinCount;",
            "256-entry histogram buffer allocation");
        RequireContains(
            pass,
            "histogramDescription.format=nvrhi::Format::R32_UINT;",
            "R32_UINT histogram storage");
        RequireContains(
            pass,
            "exposureDescription.byteSize=sizeof(float)*2u;",
            "adapted and final exposure buffer allocation");
        RequireContains(
            pass,
            "exposureDescription.format=nvrhi::Format::R32_FLOAT;",
            "R32_FLOAT exposure storage");
        RequireContains(
            pass,
            "constants.exposureCompensationEV="
                "settings.exposureCompensationEV;",
            "sanitized Exposure Compensation upload");
        RequireContains(
            pass,
            "constants.adjustmentPeriodSeconds="
                "settings.adjustmentPeriodSeconds;",
            "sanitized Adjustment Period upload for histogram and resolve");
        RequireContains(
            pass,
            "constants.maximumBrighteningEV="
                "settings.maximumBrighteningEV;",
            "sanitized Maximum Brightening upload");
        RequireContains(
            pass,
            "constants.maximumDarkeningEV="
                "settings.maximumDarkeningEV;",
            "sanitized Maximum Darkening upload");
        constexpr std::string_view DispatchOrder[] = {
            "commandList->clearBufferUInt(m_HistogramBuffer,0u);",
            "state.pipeline=m_HistogramPipeline;",
            "commandList->dispatch(",
            "histogramDispatched=true;",
            "if(histogramDispatched){",
            "state.pipeline=m_ResolvePipeline;",
            "commandList->dispatch(1u);"
        };
        RequireOrdered(
            pass,
            DispatchOrder,
            "all planar histograms before one GPU exposure resolve");
        RequireOccurrenceCount(
            pass,
            "state.pipeline=m_ResolvePipeline;",
            1u,
            "auto exposure must resolve exactly once per frame");
        const size_t viewLoop = pass.find(
            "for(uint32_tviewIndex=0u;");
        const size_t viewLoopOpen = pass.find('{', viewLoop);
        const size_t viewLoopClose = FindMatchingBrace(pass, viewLoopOpen);
        const size_t resolvePipeline = pass.find(
            "state.pipeline=m_ResolvePipeline;");
        Require(
            viewLoop != std::string_view::npos &&
                viewLoopClose != std::string_view::npos &&
                resolvePipeline != std::string_view::npos &&
                resolvePipeline > viewLoopClose,
            "exposure resolve must occur after the planar histogram loop closes");
        RequireAbsent(
            pass,
            "readBuffer(",
            "auto-exposure GPU path CPU readback");
        RequireContains(
            pass,
            "constboolresetExposure=m_ResetRequested||!m_WasEnabled||"
                "!m_ExposureInitialized;",
            "first-frame and explicit exposure reset policy");
        RequireContains(
            pass,
            "if(!settings.enabled||diagnosticView)",
            "disabled and diagnostic-view nullptr exposure policy");
        RequireContains(
            pass,
            "if(!commandList||!m_ExposureBuffer)returnnullptr;",
            "missing command list or exposure storage must return nullptr");
        RequireContains(
            pass,
            "constautoreturnUnityExposure=[&]()->nvrhi::IBuffer*{"
                "m_ResetRequested=true;m_WasEnabled=false;"
                "m_ExposureInitialized=false;returnnullptr;};",
            "every optional fallback must select the exact unity pipeline");
        RequireContains(
            pass,
            "if(!IsAvailable()||!sceneColor)"
                "returnreturnUnityExposure();",
            "unavailable optional auto exposure must fail open to unity");
        RequireContains(
            pass,
            "if(!m_HistogramBindingSet)"
                "returnreturnUnityExposure();",
            "histogram binding failure must fail open to unity");
        RequireContains(
            pass,
            "if(!histogramDispatched)returnreturnUnityExposure();",
            "empty composite views must fail open to unity");
        RequireAbsent(
            pass,
            "clearBufferFloat(m_ExposureBuffer",
            "unity fallback must not synthesize a buffered exposure");

        RequireContains(
            agx,
            "Buffer<float>t_AutoExposure:register(t1);",
            "AgX exposure buffer binding");
        RequireContains(
            agx,
            "#if!UVSR_UNITY_EXPOSURE"
                "Buffer<float>t_AutoExposure:register(t1);"
                "#endif",
            "unity AgX permutation omits the exposure-buffer binding");
        RequireContains(
            agx,
            "#ifUVSR_UNITY_EXPOSURE"
                "float3color=saturate(AgxDefaultContrast("
                "AgxLogEncode(sceneSample.rgb)));",
            "buffer-free unity AgX branch retains established contrast clamp");
        RequireContains(
            agx,
            "#elsefloat3color=saturate(AgxDefaultContrast(AgxLogEncode("
                "sceneSample.rgb*t_AutoExposure[1])));#endif",
            "automatic AgX branch differs only by scene-linear exposure");
        constexpr std::string_view AgxOrder[] = {
            "float4sceneSample=t_SceneColor.Load(",
            "AgxLogEncode(sceneSample.rgb)",
            "AgxLogEncode(sceneSample.rgb*t_AutoExposure[1])",
            "color=saturate(mul(AGX_OUTSET,color));",
            "outputColor=float4(color,sceneSample.a);"
        };
        RequireOrdered(
            agx,
            AgxOrder,
            "unity, automatic exposure, established outset clamp, and output");
        RequireOccurrenceCount(
            agx,
            "saturate(AgxDefaultContrast(",
            2u,
            "both AgX permutations must retain the contrast clamp");
        RequireOccurrenceCount(
            agx,
            "sceneSample.rgb*t_AutoExposure[1]",
            1u,
            "only the automatic permutation may multiply scene-linear input");
        RequireAbsent(
            agx,
            "color=pow(",
            "AgX output must not add the rejected global power transform");
        RequireContains(
            shaderConfig,
            "agx_tonemapping_ps.hlsl-Tps-Emain-DUVSR_UNITY_EXPOSURE={0,1}",
            "packaged unity-exposure AgX permutation");
        RequireContains(
            viewer,
            "ShaderMacro(\"UVSR_UNITY_EXPOSURE\",\"0\")",
            "exact buffered AgX permutation lookup");
        RequireContains(
            viewer,
            "ShaderMacro(\"UVSR_UNITY_EXPOSURE\",\"1\")",
            "exact unity AgX permutation lookup");
        RequireContains(
            viewer,
            "booluseAutomaticExposure=exposureBuffer&&m_Pipeline;",
            "nullable exposure selects the buffer-free AgX pipeline");
        constexpr std::string_view AutomaticBindingFallbackOrder[] = {
            "bindingSet=m_BindingSet;",
            "if(!bindingSet){",
            "useAutomaticExposure=false;",
            "pipeline=m_UnityExposurePipeline.Get();",
            "if(!useAutomaticExposure){",
            "bindingSet=m_UnityExposureBindingSet;",
            "if(!pipeline||!bindingSet)returnfalse;"
        };
        RequireOrdered(
            viewer,
            AutomaticBindingFallbackOrder,
            "automatic binding failure must retry exact unity AgX");
        constexpr std::string_view DisabledFrameOrder[] = {
            "constboolautoExposureExpected=m_ui.AutoExposure.enabled&&"
                "!diagnosticExposureView;",
            "nvrhi::IBuffer*autoExposureBuffer=nullptr;",
            "if(autoExposureExpected&&m_AutoExposurePass){",
            "autoExposureBuffer=m_AutoExposurePass->Render(",
            "elseif(m_AutoExposurePass){",
            "m_AutoExposurePass->Reset();",
            "antiAliasedTexture,autoExposureBuffer);"
        };
        RequireOrdered(
            viewer,
            DisabledFrameOrder,
            "disabled and diagnostic frames must keep a null exposure buffer");
        RequireContains(
            viewer,
            "m_TextureOnlyBindingLayout="
                "device->createBindingLayout(layoutDesc);",
            "texture-only unity and output binding layout");
        RequireContains(
            viewer,
            "pipelineDesc.PS=m_UnityExposurePixelShader;"
                "pipelineDesc.bindingLayouts={m_TextureOnlyBindingLayout};",
            "unity AgX pipeline uses the texture-only layout");
        RequireContains(
            viewer,
            "m_UnityExposureBindingSet=m_Device->createBindingSet("
                "bindingSetDesc,m_TextureOnlyBindingLayout);",
            "unity AgX set satisfies its texture-only layout");
        RequireContains(
            viewer,
            "pipelineDesc.PS=m_OutputPixelShader;"
                "pipelineDesc.bindingLayouts={m_TextureOnlyBindingLayout};",
            "display-output pipeline uses the texture-only layout");
        RequireContains(
            viewer,
            "m_OutputBindingSet=m_Device->createBindingSet("
                "bindingSetDesc,m_TextureOnlyBindingLayout);",
            "display-output set satisfies its texture-only layout");
        RequireContains(
            viewer,
            "if(!pipeline||!bindingSet)returnfalse;",
            "missing unity AgX resources fail open without recording commands");
        RequireContains(
            viewer,
            "constbooltoneMapped=m_AgxToneMappingPass&&"
                "m_AgxToneMappingPass->Render("
                "m_CommandList,*m_View,antiAliasedTexture,"
                "autoExposureBuffer);",
            "renderer continues through tone mapping with nullable exposure");
        RequireAbsent(
            viewer,
            "if(!autoExposureBuffer){",
            "exposure allocation failure must not abandon an open frame");
        constexpr std::string_view FrameCompletionOrder[] = {
            "constbooltoneMapped=m_AgxToneMappingPass&&",
            "EndRendererStage(RendererTimingStage::CompleteFrame);",
            "m_CommandList->close();",
            "GetDevice()->executeCommandList(m_CommandList);"
        };
        RequireOrdered(
            viewer,
            FrameCompletionOrder,
            "unity-exposure fallback still completes and submits the frame");
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: auto_exposure_tests <source-root>\n";
        return 2;
    }

    TestDefaultsAndSanitization();
    TestMeteringTargetAndExposureCompensation();
    TestTemporalAdaptation();
    TestEstablishedUnityAgxGolden();
    TestGpuSourceContracts(argv[1]);

    if (g_FailureCount != 0)
    {
        std::cerr << g_FailureCount
                  << " auto-exposure contract failure(s).\n";
        return EXIT_FAILURE;
    }

    std::cout << "Auto-exposure contracts passed.\n";
    return EXIT_SUCCESS;
}
