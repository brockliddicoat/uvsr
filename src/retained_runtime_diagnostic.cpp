#include "retained_runtime_diagnostic.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace uvsr
{
    namespace
    {
        void Set(
            RetainedRuntimeCase& runtimeCase,
            std::string name,
            std::string value)
        {
            const auto existing = std::find_if(
                runtimeCase.settings.begin(),
                runtimeCase.settings.end(),
                [&name](const auto& setting)
                {
                    return setting.first == name;
                });
            if (existing != runtimeCase.settings.end())
                existing->second = std::move(value);
            else
                runtimeCase.settings.emplace_back(
                    std::move(name), std::move(value));
        }

        [[nodiscard]] std::string_view Get(
            const RetainedRuntimeCase& runtimeCase,
            std::string_view name)
        {
            const auto setting = std::find_if(
                runtimeCase.settings.begin(),
                runtimeCase.settings.end(),
                [name](const auto& candidate)
                {
                    return candidate.first == name;
                });
            return setting == runtimeCase.settings.end()
                ? std::string_view{}
                : std::string_view(setting->second);
        }

        [[nodiscard]] RetainedRuntimeCase Raster(std::string name)
        {
            RetainedRuntimeCase runtimeCase;
            runtimeCase.name = std::move(name);
            runtimeCase.expectedSampleCount = 1u;
            runtimeCase.settings = {
                { "lighting.solution", "ray-marching" },
                { "representation.allow-ray-traversal", "on" },
                { "anti-aliasing.taa.enabled", "off" },
                { "anti-aliasing.msaa.enabled", "off" },
                { "visibility.enabled", "off" },
                { "visibility.ao.enabled", "off" },
                { "visibility.gi.enabled", "off" },
                { "sky.visibility.enabled", "off" },
                { "light.selected", "flashlight_1" },
                { "light.selected.flashlight.enabled", "off" },
                { "light.selected.flashlight.cast-shadows", "on" },
                { "shadows.ray-traced.enabled", "on" }
            };
            return runtimeCase;
        }

        [[nodiscard]] RetainedRuntimeCase Visibility(
            std::string name,
            bool ambientOcclusion,
            bool globalIllumination)
        {
            RetainedRuntimeCase runtimeCase = Raster(std::move(name));
            Set(runtimeCase, "visibility.enabled", "on");
            Set(runtimeCase, "visibility.ao.enabled",
                ambientOcclusion ? "on" : "off");
            Set(runtimeCase, "visibility.gi.enabled",
                globalIllumination ? "on" : "off");
            if (ambientOcclusion)
            {
                Set(runtimeCase, "denoising.ao.method", "reblur");
                Set(runtimeCase,
                    "visibility.ao.output-hit-distance", "on");
                runtimeCase.expectAmbientOcclusionDenoising = true;
            }
            if (globalIllumination)
            {
                Set(runtimeCase, "denoising.gi.method", "relax");
                Set(runtimeCase,
                    "visibility.gi.output-hit-distance", "on");
                runtimeCase.expectGlobalIlluminationDenoising = true;
            }
            runtimeCase.expectScreenVisibility =
                ambientOcclusion || globalIllumination;
            return runtimeCase;
        }

        [[nodiscard]] std::string LowerAscii(std::string value)
        {
            std::transform(
                value.begin(), value.end(), value.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        constexpr double MaximumBaselineFrameMilliseconds = 1000.0;
        constexpr double FrameTimingRelativeTolerance = 4.0;
        constexpr double FrameTimingAdditiveToleranceMilliseconds = 50.0;
        constexpr auto RetainedRuntimeGlobalTimeout =
            std::chrono::hours(6);

        [[nodiscard]] std::size_t ActionCount(
            const RetainedRuntimeCase& runtimeCase) noexcept
        {
            if (runtimeCase.exerciseRetainedStateChanges)
                return 4u;
            return runtimeCase.action == RetainedRuntimeAction::None
                ? 0u
                : 1u;
        }

        [[nodiscard]] RetainedRuntimeAction ActionAt(
            const RetainedRuntimeCase& runtimeCase,
            std::size_t index) noexcept
        {
            if (!runtimeCase.exerciseRetainedStateChanges)
            {
                return index == 0u
                    ? runtimeCase.action
                    : RetainedRuntimeAction::None;
            }
            constexpr std::array Actions = {
                RetainedRuntimeAction::NudgeCamera,
                RetainedRuntimeAction::ResizeViewport,
                RetainedRuntimeAction::ChangeScene,
                RetainedRuntimeAction::ChangeSetting
            };
            return index < Actions.size()
                ? Actions[index]
                : RetainedRuntimeAction::None;
        }

        [[nodiscard]] const char* CaptureLabel(
            RetainedRuntimeAction action) noexcept
        {
            switch (action)
            {
            case RetainedRuntimeAction::None: return "baseline";
            case RetainedRuntimeAction::NudgeCamera: return "camera";
            case RetainedRuntimeAction::ResizeViewport: return "resize";
            case RetainedRuntimeAction::ChangeScene: return "scene";
            case RetainedRuntimeAction::ChangeSetting: return "reference";
            case RetainedRuntimeAction::ChangeMaterial: return "material";
            case RetainedRuntimeAction::ChangeLight: return "light";
            case RetainedRuntimeAction::ToggleFlashlight: return "flashlight";
            case RetainedRuntimeAction::CycleLightingSolution:
                return "lighting-solution";
            }
            return "invalid";
        }

        [[nodiscard]] bool StartsWith(
            std::string_view value,
            std::string_view prefix) noexcept
        {
            return value.size() >= prefix.size() &&
                value.substr(0u, prefix.size()) == prefix;
        }

        [[nodiscard]] std::string DescribeSnapshotMismatch(
            std::string_view expected,
            std::string_view actual)
        {
            std::size_t mismatch = 0u;
            while (mismatch < expected.size() &&
                mismatch < actual.size() &&
                expected[mismatch] == actual[mismatch])
            {
                ++mismatch;
            }
            if (mismatch == expected.size() && mismatch == actual.size())
                return {};

            const std::size_t precedingLineEnd = mismatch == 0u
                ? std::string_view::npos
                : expected.rfind('\n', mismatch - 1u);
            const std::size_t lineStart =
                precedingLineEnd == std::string_view::npos
                    ? 0u
                    : precedingLineEnd + 1u;
            const auto lineAt = [lineStart](std::string_view value)
            {
                if (lineStart >= value.size())
                    return std::string("<end>");
                const std::size_t lineEnd = value.find('\n', lineStart);
                return std::string(value.substr(
                    lineStart,
                    lineEnd == std::string_view::npos
                        ? std::string_view::npos
                        : lineEnd - lineStart));
            };
            const std::size_t line = 1u + static_cast<std::size_t>(
                std::count(expected.begin(),
                    expected.begin() + lineStart, '\n'));
            return " at line " + std::to_string(line) + ": expected '" +
                lineAt(expected) + "', got '" + lineAt(actual) + "'";
        }

        [[nodiscard]] std::pair<std::string, std::size_t>
            SemanticDomain(std::string_view family)
        {
            if (family == "ao-only-save-load-reset" ||
                family == "gi-only-save-load-reset" ||
                family == "ao-gi-save-load-reset" ||
                family == "lighting-accumulation-fixed-cumulative")
            {
                return { "ao-gi-mode", 0u };
            }
            if (StartsWith(family, "visibility-preset-"))
                return { "visibility-preset", 1u };
            if (StartsWith(family, "visibility-custom-estimator-"))
                return { "visibility-estimator", 2u };
            if (StartsWith(family, "visibility-custom-resolution-"))
                return { "visibility-resolution", 3u };
            if (StartsWith(family, "visibility-custom-samples-"))
                return { "visibility-sample-quality", 4u };
            if (StartsWith(family, "ao-gi-filter-"))
                return { "ao-gi-filter", 5u };
            if (StartsWith(family, "ao-gi-quality-"))
                return { "ao-gi-quality", 6u };
            if (StartsWith(family, "ao-gi-resolution-"))
                return { "ao-gi-denoising-resolution", 7u };
            if (StartsWith(family, "visibility-custom-noise-"))
                return { "visibility-noise", 8u };
            if (StartsWith(family, "ao-gi-hit-precision-"))
                return { "ao-gi-hit-precision", 9u };
            if (StartsWith(family, "ao-gi-continuous-"))
                return { "ao-gi-continuous", 10u };
            if (StartsWith(family, "debug-visibility-view-"))
                return { "visibility-debug-view", 11u };
            if (StartsWith(family, "global-noise-domain-"))
                return { "global-noise", 12u };
            return { {}, 13u };
        }
    }

    template <typename Scalar, typename Decode>
    RuntimeOutputEvidence AnalyzeRuntimeLinearRgba(
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowPitchBytes,
        Decode decode)
    {
        RuntimeOutputEvidence evidence;
        evidence.width = width;
        evidence.height = height;
        if (!pixels || width == 0u || height == 0u ||
            rowPitchBytes < std::size_t(width) * sizeof(Scalar) * 4u)
        {
            return evidence;
        }

        float minimum = std::numeric_limits<float>::infinity();
        float maximum = -std::numeric_limits<float>::infinity();
        double luminanceSum = 0.0;
        double luminanceSquareSum = 0.0;
        double horizontalGradientSum = 0.0;
        std::uint64_t luminanceCount = 0u;
        std::uint64_t horizontalGradientCount = 0u;
        std::array<Scalar, 3> firstRgb{};
        bool haveFirst = false;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const auto* row = reinterpret_cast<const Scalar*>(
                static_cast<const unsigned char*>(pixels) +
                std::size_t(y) * rowPitchBytes);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const Scalar* rgba = row + std::size_t(x) * 4u;
                if (!haveFirst)
                {
                    firstRgb = { rgba[0], rgba[1], rgba[2] };
                    haveFirst = true;
                }
                else if (rgba[0] != firstRgb[0] ||
                    rgba[1] != firstRgb[1] || rgba[2] != firstRgb[2])
                {
                    ++evidence.varyingPixelCount;
                }

                float rgb[3]{};
                bool rgbFinite = true;
                for (std::size_t component = 0u; component < 4u;
                    ++component)
                {
                    const auto* componentBytes =
                        reinterpret_cast<const unsigned char*>(
                            &rgba[component]);
                    for (std::size_t byte = 0u; byte < sizeof(Scalar); ++byte)
                    {
                        evidence.linearHash ^= componentBytes[byte];
                        evidence.linearHash *= 1099511628211ull;
                    }
                    const float value = decode(rgba[component]);
                    if (!std::isfinite(value))
                    {
                        ++evidence.nonFiniteComponentCount;
                        if (component < 3u)
                            rgbFinite = false;
                        continue;
                    }
                    ++evidence.finiteComponentCount;
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                    if (component < 3u)
                        rgb[component] = value;
                }
                if (rgbFinite)
                {
                    const double luminance =
                        0.2126 * double(rgb[0]) +
                        0.7152 * double(rgb[1]) +
                        0.0722 * double(rgb[2]);
                    luminanceSum += luminance;
                    luminanceSquareSum += luminance * luminance;
                    const double mappedLuminance = std::sqrt(std::clamp(
                        luminance / 4.0, 0.0, 1.0));
                    const std::size_t histogramBin = std::min<std::size_t>(
                        evidence.linearLuminanceHistogram.size() - 1u,
                        static_cast<std::size_t>(mappedLuminance *
                            double(evidence.linearLuminanceHistogram.size())));
                    ++evidence.linearLuminanceHistogram[histogramBin];
                    ++luminanceCount;
                }
                if (x > 0u && rgbFinite)
                {
                    const Scalar* left = rgba - 4u;
                    const float leftRgb[3] = {
                        decode(left[0]),
                        decode(left[1]),
                        decode(left[2])
                    };
                    const float difference =
                        std::fabs(rgb[0] - leftRgb[0]) +
                        std::fabs(rgb[1] - leftRgb[1]) +
                        std::fabs(rgb[2] - leftRgb[2]);
                    if (std::isfinite(difference) && difference > 0.001f)
                        ++evidence.edgePixelCount;
                    if (std::isfinite(difference))
                    {
                        horizontalGradientSum += double(difference);
                        ++horizontalGradientCount;
                    }
                }
            }
        }
        evidence.minimumLinearValue = std::isfinite(minimum) ? minimum : 0.f;
        evidence.maximumLinearValue = std::isfinite(maximum) ? maximum : 0.f;
        if (luminanceCount > 0u)
        {
            evidence.meanLinearLuminance =
                luminanceSum / double(luminanceCount);
            evidence.rmsLinearLuminance = std::sqrt(
                luminanceSquareSum / double(luminanceCount));
        }
        if (horizontalGradientCount > 0u)
        {
            evidence.meanLinearHorizontalGradient =
                horizontalGradientSum / double(horizontalGradientCount);
        }
        evidence.linearReadbackValid =
            evidence.nonFiniteComponentCount == 0u &&
            evidence.finiteComponentCount ==
                std::uint64_t(width) * std::uint64_t(height) * 4u &&
            evidence.varyingPixelCount > 0u &&
            evidence.edgePixelCount > 0u &&
            evidence.maximumLinearValue > evidence.minimumLinearValue;
        return evidence;
    }

    RuntimeOutputEvidence AnalyzeRuntimeLinearRgba16(
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowPitchBytes)
    {
        const auto decodeHalf = [](std::uint16_t bits)
        {
            const bool negative = (bits & 0x8000u) != 0u;
            const std::uint32_t exponent = (bits >> 10u) & 0x1fu;
            const std::uint32_t mantissa = bits & 0x3ffu;
            float value = 0.f;
            if (exponent == 0u)
            {
                value = mantissa == 0u
                    ? 0.f
                    : std::ldexp(static_cast<float>(mantissa), -24);
            }
            else if (exponent == 0x1fu)
            {
                value = mantissa == 0u
                    ? std::numeric_limits<float>::infinity()
                    : std::numeric_limits<float>::quiet_NaN();
            }
            else
            {
                value = std::ldexp(
                    1.f + static_cast<float>(mantissa) / 1024.f,
                    static_cast<int>(exponent) - 15);
            }
            return negative ? -value : value;
        };
        return AnalyzeRuntimeLinearRgba<std::uint16_t>(
            pixels, width, height, rowPitchBytes, decodeHalf);
    }

    RuntimeOutputEvidence AnalyzeRuntimeLinearRgba32(
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowPitchBytes)
    {
        const auto decodeFloat = [](float value) { return value; };
        return AnalyzeRuntimeLinearRgba<float>(
            pixels, width, height, rowPitchBytes, decodeFloat);
    }

    RuntimeSemanticSignature BuildRuntimeSemanticSignature(
        const RuntimeOutputEvidence& output,
        std::uint32_t receiverSampleCount,
        double cpuFrameMilliseconds,
        double gpuFrameMilliseconds) noexcept
    {
        RuntimeSemanticSignature signature;
        signature.width = output.width;
        signature.height = output.height;
        signature.receiverSampleCount = receiverSampleCount;
        signature.meanLinearLuminance = output.meanLinearLuminance;
        signature.rmsLinearLuminance = output.rmsLinearLuminance;
        signature.meanLinearHorizontalGradient =
            output.meanLinearHorizontalGradient;
        signature.linearLuminanceHistogram =
            output.linearLuminanceHistogram;
        for (const std::uint64_t count : output.linearLuminanceHistogram)
            signature.linearLuminanceSampleCount += count;
        signature.cpuFrameMilliseconds = cpuFrameMilliseconds;
        signature.gpuFrameMilliseconds = gpuFrameMilliseconds;
        return signature;
    }

    bool RuntimeSemanticSignaturesAreDistinct(
        const RuntimeSemanticSignature& left,
        const RuntimeSemanticSignature& right) noexcept
    {
        if (left.width == 0u || left.height == 0u ||
            left.width != right.width || left.height != right.height ||
            left.receiverSampleCount == 0u ||
            right.receiverSampleCount == 0u)
        {
            return false;
        }
        const auto differs = [](
            double leftValue,
            double rightValue,
            double absoluteTolerance,
            double relativeTolerance)
        {
            if (!std::isfinite(leftValue) || !std::isfinite(rightValue))
                return false;
            const double difference = std::fabs(leftValue - rightValue);
            const double scale = std::max(
                std::fabs(leftValue), std::fabs(rightValue));
            return difference > std::max(
                absoluteTolerance, scale * relativeTolerance);
        };
        double histogramDistance = 0.0;
        if (left.linearLuminanceSampleCount > 0u &&
            right.linearLuminanceSampleCount > 0u)
        {
            for (std::size_t index = 0u;
                index < left.linearLuminanceHistogram.size(); ++index)
            {
                const double leftFrequency =
                    double(left.linearLuminanceHistogram[index]) /
                    double(left.linearLuminanceSampleCount);
                const double rightFrequency =
                    double(right.linearLuminanceHistogram[index]) /
                    double(right.linearLuminanceSampleCount);
                histogramDistance += std::fabs(
                    leftFrequency - rightFrequency);
            }
        }
        return
            differs(left.meanLinearLuminance,
                right.meanLinearLuminance, 0.00005, 0.001) ||
            differs(left.rmsLinearLuminance,
                right.rmsLinearLuminance, 0.00005, 0.001) ||
            differs(left.meanLinearHorizontalGradient,
                right.meanLinearHorizontalGradient, 0.00005, 0.002) ||
            histogramDistance > 0.001;
    }

    bool RuntimeSemanticTimingsAreDistinct(
        const RuntimeSemanticSignature& left,
        const RuntimeSemanticSignature& right) noexcept
    {
        if (left.width == 0u || left.height == 0u ||
            left.width != right.width || left.height != right.height)
        {
            return false;
        }
        const auto differs = [](
            double leftValue,
            double rightValue)
        {
            if (!std::isfinite(leftValue) || !std::isfinite(rightValue))
                return false;
            const double difference = std::fabs(leftValue - rightValue);
            const double scale = std::max(
                std::fabs(leftValue), std::fabs(rightValue));
            return difference > std::max(0.25, scale * 0.20);
        };
        return
            differs(left.cpuFrameMilliseconds,
                right.cpuFrameMilliseconds) ||
            differs(left.gpuFrameMilliseconds,
                right.gpuFrameMilliseconds);
    }

    bool ValidateRetainedRuntimeSemanticCaptures(
        const std::vector<RetainedRuntimeCase>& cases,
        const std::vector<RetainedRuntimeSemanticCapture>& captures,
        std::string& reason)
    {
        std::map<std::string, const RetainedRuntimeCase*> expected;
        for (const RetainedRuntimeCase& runtimeCase : cases)
        {
            if (!runtimeCase.requireCrossCaseDistinctness)
                continue;
            if (runtimeCase.semanticFamily.empty() ||
                !expected.emplace(runtimeCase.name, &runtimeCase).second)
            {
                reason = "AO/GI semantic case identity was empty or duplicated";
                return false;
            }
        }

        std::map<std::string,
            std::vector<const RetainedRuntimeSemanticCapture*>> observed;
        std::map<std::string,
            std::vector<const RetainedRuntimeSemanticCapture*>> byFamily;
        std::map<std::string, std::set<std::string>> domainFamilies;
        for (const RetainedRuntimeSemanticCapture& capture : captures)
        {
            const auto expectedCase = expected.find(capture.caseName);
            if (expectedCase == expected.end() ||
                capture.family != expectedCase->second->semanticFamily ||
                capture.domain != expectedCase->second->semanticDomain ||
                capture.sceneToken.empty() ||
                capture.signature.receiverSampleCount !=
                    expectedCase->second->expectedSampleCount ||
                capture.signature.width == 0u ||
                capture.signature.height == 0u)
            {
                reason = "AO/GI semantic capture identity or dimensions drifted";
                return false;
            }
            observed[capture.caseName].push_back(&capture);
            byFamily[capture.family].push_back(&capture);
            if (!capture.domain.empty())
                domainFamilies[capture.domain].insert(capture.family);
        }
        if (observed.size() != expected.size() ||
            captures.size() != expected.size() * 2u)
        {
            reason = "AO/GI semantic capture coverage was incomplete";
            return false;
        }
        for (const auto& [caseName, caseCaptures] : observed)
        {
            if (caseCaptures.size() != 2u ||
                caseCaptures[0]->sceneToken == caseCaptures[1]->sceneToken)
            {
                reason = "AO/GI semantic case '" + caseName +
                    "' lacked two distinct scene captures";
                return false;
            }
        }

        const auto distinct = [&expected]
        (
            const RetainedRuntimeSemanticCapture& left,
            const RetainedRuntimeSemanticCapture& right,
            bool& compatible)
        {
            const bool leftTimingOnly =
                expected.at(left.caseName)->semanticTimingOnly;
            const bool rightTimingOnly =
                expected.at(right.caseName)->semanticTimingOnly;
            compatible = leftTimingOnly == rightTimingOnly;
            if (!compatible)
                return false;
            return leftTimingOnly
                ? RuntimeSemanticTimingsAreDistinct(
                    left.signature, right.signature)
                : RuntimeSemanticSignaturesAreDistinct(
                    left.signature, right.signature);
        };

        for (const auto& [family, familyCaptures] : byFamily)
        {
            const bool requireCrossSampleDistinctness = expected.at(
                familyCaptures.front()->caseName)
                    ->requireCrossSampleDistinctness;
            for (const RetainedRuntimeSemanticCapture* capture :
                familyCaptures)
            {
                if (expected.at(capture->caseName)
                        ->requireCrossSampleDistinctness !=
                    requireCrossSampleDistinctness)
                {
                    reason = "AO/GI family '" + family +
                        "' mixed cross-sample evidence contracts";
                    return false;
                }
            }
            if (!requireCrossSampleDistinctness)
                continue;

            std::set<std::uint32_t> samples;
            for (const RetainedRuntimeSemanticCapture* capture : familyCaptures)
                samples.insert(capture->signature.receiverSampleCount);
            for (auto leftSample = samples.begin();
                leftSample != samples.end(); ++leftSample)
            {
                for (auto rightSample = std::next(leftSample);
                    rightSample != samples.end(); ++rightSample)
                {
                    bool comparable = false;
                    bool pairDistinct = false;
                    for (const RetainedRuntimeSemanticCapture* left :
                        familyCaptures)
                    for (const RetainedRuntimeSemanticCapture* right :
                        familyCaptures)
                    {
                        if (left->signature.receiverSampleCount !=
                                *leftSample ||
                            right->signature.receiverSampleCount !=
                                *rightSample ||
                            left->sceneToken != right->sceneToken ||
                            left->signature.width != right->signature.width ||
                            left->signature.height != right->signature.height)
                        {
                            continue;
                        }
                        bool compatible = false;
                        const bool contextDistinct = distinct(
                            *left, *right, compatible);
                        if (!compatible)
                        {
                            reason = "AO/GI family '" + family +
                                "' mixed output and performance-only evidence";
                            return false;
                        }
                        comparable = true;
                        pairDistinct = pairDistinct || contextDistinct;
                    }
                    if (!comparable || !pairDistinct)
                    {
                        reason = "AO/GI family '" + family + "' samples " +
                            std::to_string(*leftSample) + "x and " +
                            std::to_string(*rightSample) +
                            (comparable
                                ? " mapped identically"
                                : " lacked comparable scene output");
                        return false;
                    }
                }
            }
        }

        for (const auto& [domain, families] : domainFamilies)
        {
            if (families.size() < 2u)
            {
                reason = "AO/GI semantic domain '" + domain +
                    "' had fewer than two option families";
                return false;
            }
            bool compared = false;
            for (auto leftFamily = families.begin();
                leftFamily != families.end(); ++leftFamily)
            {
                for (auto rightFamily = std::next(leftFamily);
                    rightFamily != families.end(); ++rightFamily)
                {
                    bool pairComparable = false;
                    bool pairDistinct = false;
                    for (const auto* left : byFamily[*leftFamily])
                    for (const auto* right : byFamily[*rightFamily])
                    {
                        if (left->sceneToken != right->sceneToken ||
                            left->signature.receiverSampleCount !=
                                right->signature.receiverSampleCount ||
                            left->signature.width != right->signature.width ||
                            left->signature.height != right->signature.height)
                        {
                            continue;
                        }
                        pairComparable = true;
                        bool compatible = false;
                        const bool contextDistinct = distinct(
                            *left, *right, compatible);
                        if (!compatible)
                        {
                            reason = "AO/GI semantic domain '" + domain +
                                "' mixed output and performance-only evidence";
                            return false;
                        }
                        pairDistinct = pairDistinct || contextDistinct;
                    }
                    if (pairComparable)
                    {
                        compared = true;
                        if (!pairDistinct)
                        {
                            reason = "AO/GI options '" + *leftFamily +
                                "' and '" + *rightFamily +
                                "' mapped identically in domain '" +
                                domain + "'";
                            return false;
                        }
                    }
                }
            }
            if (!compared)
            {
                reason = "AO/GI semantic domain '" + domain +
                    "' lacked a same-scene/sample comparison";
                return false;
            }
        }
        reason.clear();
        return true;
    }

    std::string EscapeRuntimeDiagnosticJson(std::string_view value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 8u);
        constexpr char Hex[] = "0123456789abcdef";
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '\"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character < 0x20u)
                {
                    escaped += "\\u00";
                    escaped.push_back(Hex[character >> 4u]);
                    escaped.push_back(Hex[character & 0x0fu]);
                }
                else
                {
                    escaped.push_back(static_cast<char>(character));
                }
                break;
            }
        }
        return escaped;
    }

    namespace
    {
        [[nodiscard]] std::string Quoted(std::string_view value)
        {
            return "\"" + EscapeRuntimeDiagnosticJson(value) + "\"";
        }

        [[nodiscard]] const char* JsonBool(bool value) noexcept
        {
            return value ? "true" : "false";
        }

        [[nodiscard]] const char* ActionName(
            RetainedRuntimeAction action) noexcept
        {
            switch (action)
            {
            case RetainedRuntimeAction::None: return "none";
            case RetainedRuntimeAction::NudgeCamera: return "camera";
            case RetainedRuntimeAction::ResizeViewport: return "resize";
            case RetainedRuntimeAction::ChangeScene: return "scene";
            case RetainedRuntimeAction::ChangeSetting: return "setting";
            case RetainedRuntimeAction::ChangeMaterial: return "material";
            case RetainedRuntimeAction::ChangeLight: return "light";
            case RetainedRuntimeAction::ToggleFlashlight:
                return "flashlight";
            case RetainedRuntimeAction::CycleLightingSolution:
                return "lighting-solution";
            }
            return "invalid";
        }

        [[nodiscard]] std::string Hex64(std::uint64_t value)
        {
            constexpr char Digits[] = "0123456789abcdef";
            std::string text(16u, '0');
            for (std::size_t index = 0u; index < text.size(); ++index)
            {
                const unsigned shift =
                    static_cast<unsigned>((text.size() - index - 1u) * 4u);
                text[index] = Digits[(value >> shift) & 0xfu];
            }
            return text;
        }

        [[nodiscard]] std::int64_t FrameMicroseconds(
            double milliseconds) noexcept
        {
            if (!std::isfinite(milliseconds) || milliseconds <= 0.0)
                return 0;
            return static_cast<std::int64_t>(
                std::llround(milliseconds * 1000.0));
        }

        [[nodiscard]] std::int64_t LinearMicrounits(
            double value) noexcept
        {
            if (!std::isfinite(value))
                return 0;
            return static_cast<std::int64_t>(
                std::llround(value * 1000000.0));
        }

        [[nodiscard]] std::string HistogramJson(
            const std::array<std::uint64_t, 16>& histogram)
        {
            std::string json = "[";
            for (std::size_t index = 0u; index < histogram.size(); ++index)
            {
                if (index != 0u)
                    json.push_back(',');
                json += std::to_string(histogram[index]);
            }
            json.push_back(']');
            return json;
        }

        [[nodiscard]] std::string ProvenanceJsonMembers(
            const RetainedRuntimeProvenance& provenance)
        {
            return
                "\"settingsHash\":" + Quoted(provenance.settingsHash) +
                ",\"engineVersion\":" + Quoted(provenance.engineVersion) +
                ",\"sourceCommit\":" + Quoted(provenance.sourceCommit) +
                ",\"sourceIdentity\":" + Quoted(provenance.sourceIdentity) +
                ",\"sourceClean\":" + JsonBool(provenance.sourceClean) +
                ",\"production\":" + JsonBool(provenance.production) +
                ",\"configuration\":" + Quoted(provenance.configuration) +
                ",\"packagePath\":" + Quoted(provenance.packagePath) +
                ",\"executablePath\":" + Quoted(provenance.executablePath) +
                ",\"executableSha256\":" +
                    Quoted(provenance.executableSha256) +
                ",\"debugLayerRequested\":" +
                    JsonBool(provenance.debugLayerRequested) +
                ",\"nvrhiValidationRequested\":" +
                    JsonBool(provenance.nvrhiValidationRequested);
        }
    }

    std::string BuildRetainedRuntimeStartJson(
        const RetainedRuntimeProvenance& provenance,
        std::size_t caseCount)
    {
        return "{\"event\":\"start\",\"schema\":3," +
            ProvenanceJsonMembers(provenance) +
            ",\"cases\":" + std::to_string(caseCount) +
            ",\"timingPolicy\":\"baseline<=1000ms; phases<=max(4x-baseline,baseline+50ms)\"}";
    }

    std::string BuildRetainedRuntimeFailureJson(
        std::string_view caseName,
        std::string_view message)
    {
        return "{\"event\":\"failure\",\"case\":" +
            Quoted(caseName) + ",\"message\":" + Quoted(message) + "}";
    }

    std::string BuildRetainedRuntimeCaseJson(
        std::size_t caseIndex,
        const RetainedRuntimeCase& runtimeCase,
        const RetainedRuntimeTelemetry& telemetry)
    {
        const RuntimeOutputEvidence output = telemetry.output.value_or(
            RuntimeOutputEvidence{});
        return "{\"event\":\"case\",\"index\":" +
            std::to_string(caseIndex) + ",\"name\":" +
            Quoted(runtimeCase.name) + ",\"status\":\"pass\"," +
            "\"semanticFamily\":" +
            Quoted(runtimeCase.semanticFamily) +
            ",\"semanticDomain\":" +
            Quoted(runtimeCase.semanticDomain) + "," +
            "\"phase\":" + Quoted(runtimeCase.exerciseRetainedStateChanges
                ? "reference"
                : CaptureLabel(runtimeCase.action)) + "," +
            "\"activeScene\":" + Quoted(telemetry.currentScene) +
            ",\"expectedAction\":" +
            Quoted(runtimeCase.exerciseRetainedStateChanges
                ? "camera-resize-scene-reference"
                : ActionName(runtimeCase.action)) +
            ",\"appliedAction\":" +
            Quoted(ActionName(telemetry.lastAppliedAction)) +
            "," +
            "\"receiverSamples\":" +
            std::to_string(telemetry.receiverSampleCount) +
            ",\"pathHistory\":" +
            std::to_string(telemetry.pathHistoryCount) +
            ",\"screenVisibility\":" +
            JsonBool(telemetry.screenVisibilityDispatched) +
            ",\"directional\":" +
            JsonBool(telemetry.directionalVisibilityDispatched) +
            ",\"sky\":" + JsonBool(telemetry.skyVisibilityDispatched) +
            ",\"flashlightLightingSubmitted\":" +
            JsonBool(telemetry.flashlightLightingSubmitted) +
            ",\"flashlightShadow\":" +
            JsonBool(telemetry.flashlightVisibilityDispatched) +
            ",\"shadowDenoising\":" +
            JsonBool(telemetry.shadowDenoisingDispatched) +
            ",\"skyDenoising\":" +
            JsonBool(telemetry.skyDenoisingDispatched) +
            ",\"aoDenoising\":" +
            JsonBool(telemetry.ambientOcclusionDenoisingDispatched) +
            ",\"giDenoising\":" +
            JsonBool(telemetry.globalIlluminationDenoisingDispatched) +
            ",\"accumulation\":" +
            JsonBool(telemetry.lightingAccumulationCommitted) +
            ",\"autoExposure\":" +
            JsonBool(telemetry.autoExposureDispatched) +
            ",\"globalNoise\":{\"pattern\":" +
            Quoted(telemetry.globalNoisePattern) +
            ",\"resolution\":" +
            Quoted(telemetry.globalNoiseResolution) +
            ",\"animateSamples\":" +
            JsonBool(telemetry.globalNoiseAnimateSamples) +
            ",\"accumulateSamples\":" +
            JsonBool(telemetry.globalNoiseAccumulateSamples) + "}" +
            ",\"cpuFrameUs\":" +
            std::to_string(FrameMicroseconds(
                telemetry.cpuFrameMilliseconds)) +
            ",\"gpuFrameUs\":" +
            std::to_string(FrameMicroseconds(
                telemetry.gpuFrameMilliseconds)) +
            ",\"gpuTimingAvailable\":" +
            JsonBool(telemetry.gpuFrameTimingAvailable) +
            ",\"output\":{\"width\":" + std::to_string(output.width) +
            ",\"height\":" + std::to_string(output.height) +
            ",\"encodedBytes\":" + std::to_string(output.encodedBytes) +
            ",\"pixelBytes\":" + std::to_string(output.pixelBytes) +
            ",\"minimumByte\":" +
            std::to_string(static_cast<unsigned int>(output.minimumByte)) +
            ",\"maximumByte\":" +
            std::to_string(static_cast<unsigned int>(output.maximumByte)) +
            ",\"artifactPath\":" + Quoted(output.artifactPath) +
            ",\"linearFinite\":" + JsonBool(output.linearReadbackValid) +
            ",\"nonFiniteComponents\":" +
            std::to_string(output.nonFiniteComponentCount) +
            ",\"varyingPixels\":" +
            std::to_string(output.varyingPixelCount) +
            ",\"edgePixels\":" + std::to_string(output.edgePixelCount) +
            ",\"meanLuminanceMicro\":" +
            std::to_string(LinearMicrounits(
                output.meanLinearLuminance)) +
            ",\"rmsLuminanceMicro\":" +
            std::to_string(LinearMicrounits(
                output.rmsLinearLuminance)) +
            ",\"meanHorizontalGradientMicro\":" +
            std::to_string(LinearMicrounits(
                output.meanLinearHorizontalGradient)) +
            ",\"luminanceHistogram\":" +
            HistogramJson(output.linearLuminanceHistogram) +
            ",\"linearFNV1a64\":" + Quoted(Hex64(output.linearHash)) +
            ",\"fnv1a64\":" + Quoted(Hex64(output.pixelHash)) +
            "}}";
    }

    std::string BuildRetainedRuntimeCaptureJson(
        std::size_t caseIndex,
        const RetainedRuntimeCase& runtimeCase,
        std::string_view phase,
        const RetainedRuntimeTelemetry& telemetry)
    {
        const RuntimeOutputEvidence output = telemetry.output.value_or(
            RuntimeOutputEvidence{});
        return "{\"event\":\"capture\",\"index\":" +
            std::to_string(caseIndex) + ",\"name\":" +
            Quoted(runtimeCase.name) + ",\"phase\":" + Quoted(phase) +
            ",\"semanticFamily\":" +
            Quoted(runtimeCase.semanticFamily) +
            ",\"semanticDomain\":" +
            Quoted(runtimeCase.semanticDomain) +
            ",\"activeScene\":" + Quoted(telemetry.currentScene) +
            ",\"receiverSamples\":" +
            std::to_string(telemetry.receiverSampleCount) +
            ",\"screenVisibility\":" +
            JsonBool(telemetry.screenVisibilityDispatched) +
            ",\"flashlightLightingSubmitted\":" +
            JsonBool(telemetry.flashlightLightingSubmitted) +
            ",\"flashlightShadow\":" +
            JsonBool(telemetry.flashlightVisibilityDispatched) +
            ",\"accumulation\":" +
            JsonBool(telemetry.lightingAccumulationCommitted) +
            ",\"autoExposure\":" +
            JsonBool(telemetry.autoExposureDispatched) +
            ",\"globalNoise\":{\"pattern\":" +
            Quoted(telemetry.globalNoisePattern) +
            ",\"resolution\":" +
            Quoted(telemetry.globalNoiseResolution) +
            ",\"animateSamples\":" +
            JsonBool(telemetry.globalNoiseAnimateSamples) +
            ",\"accumulateSamples\":" +
            JsonBool(telemetry.globalNoiseAccumulateSamples) + "}" +
            ",\"aoDenoising\":" +
            JsonBool(telemetry.ambientOcclusionDenoisingDispatched) +
            ",\"giDenoising\":" +
            JsonBool(telemetry.globalIlluminationDenoisingDispatched) +
            ",\"cpuFrameUs\":" +
            std::to_string(FrameMicroseconds(
                telemetry.cpuFrameMilliseconds)) +
            ",\"gpuFrameUs\":" +
            std::to_string(FrameMicroseconds(
                telemetry.gpuFrameMilliseconds)) +
            ",\"output\":{\"width\":" +
            std::to_string(output.width) + ",\"height\":" +
            std::to_string(output.height) + ",\"artifactPath\":" +
            Quoted(output.artifactPath) + ",\"linearFinite\":" +
            JsonBool(output.linearReadbackValid) +
            ",\"nonFiniteComponents\":" +
            std::to_string(output.nonFiniteComponentCount) +
            ",\"varyingPixels\":" +
            std::to_string(output.varyingPixelCount) +
            ",\"edgePixels\":" +
            std::to_string(output.edgePixelCount) +
            ",\"meanLuminanceMicro\":" +
            std::to_string(LinearMicrounits(
                output.meanLinearLuminance)) +
            ",\"rmsLuminanceMicro\":" +
            std::to_string(LinearMicrounits(
                output.rmsLinearLuminance)) +
            ",\"meanHorizontalGradientMicro\":" +
            std::to_string(LinearMicrounits(
                output.meanLinearHorizontalGradient)) +
            ",\"luminanceHistogram\":" +
            HistogramJson(output.linearLuminanceHistogram) +
            ",\"linearFNV1a64\":" + Quoted(Hex64(output.linearHash)) +
            "}}";
    }

    std::string BuildRetainedRuntimeSummaryJson(
        const RetainedRuntimeProvenance& provenance,
        bool passed,
        std::size_t passedCases,
        std::size_t totalCases,
        std::int64_t elapsedMilliseconds)
    {
        return "{\"event\":\"summary\",\"status\":" +
            Quoted(passed ? "pass" : "fail") + "," +
            ProvenanceJsonMembers(provenance) +
            ",\"passed\":" + std::to_string(passedCases) +
            ",\"total\":" + std::to_string(totalCases) +
            ",\"elapsedMs\":" + std::to_string(elapsedMilliseconds) +
            "}";
    }

    std::vector<RetainedRuntimeCase> BuildRetainedRuntimeCases(
        const std::string& bistroScene,
        const std::string& sanMiguelScene)
    {
        std::vector<RetainedRuntimeCase> cases;
        std::vector<RetainedRuntimeCase> visibilityCases;
        std::size_t visibilityVariantIndex = 0u;
        const auto append = [&cases, &bistroScene](
            RetainedRuntimeCase runtimeCase)
        {
            if (Get(runtimeCase, "scene.current").empty())
                Set(runtimeCase, "scene.current", bistroScene);
            if (runtimeCase.expectedSceneToken.empty())
                runtimeCase.expectedSceneToken =
                    "bistro_interior_retextured";
            cases.push_back(std::move(runtimeCase));
        };
        const auto add = [
            &append,
            &bistroScene,
            &sanMiguelScene,
            &visibilityCases,
            &visibilityVariantIndex](RetainedRuntimeCase runtimeCase)
        {
            if (!runtimeCase.expectScreenVisibility)
            {
                append(std::move(runtimeCase));
                return;
            }
            const std::string semanticFamily = runtimeCase.name;
            const auto [semanticDomain, noiseGroup] =
                SemanticDomain(semanticFamily);
            const bool semanticCase = !StartsWith(
                semanticFamily, "scene-mixed-coverage-");
            for (const std::uint32_t samples :
                { 1u, 2u, 4u, 8u, 16u })
            {
                const bool startsInBistro =
                    visibilityVariantIndex++ % 2u == 0u;
                const std::string_view initialLabel = startsInBistro
                    ? "bistro"
                    : "san-miguel";
                const std::string_view finalLabel = startsInBistro
                    ? "san-miguel"
                    : "bistro";
                const std::string_view initialToken = startsInBistro
                    ? "bistro_interior_retextured"
                    : "san_miguel_retextured";
                const std::string_view finalToken = startsInBistro
                    ? "san_miguel_retextured"
                    : "bistro_interior_retextured";
                const std::string& initialScene = startsInBistro
                    ? bistroScene
                    : sanMiguelScene;
                const std::string& finalScene = startsInBistro
                    ? sanMiguelScene
                    : bistroScene;

                RetainedRuntimeCase variant = runtimeCase;
                const std::size_t noiseVariant = noiseGroup;
                constexpr std::array<std::string_view, 3> GlobalPatterns = {
                    "spatial-white", "spatial-blue", "spatiotemporal-blue"
                };
                constexpr std::array<std::string_view, 4> GlobalResolutions = {
                    "64x64", "128x128", "256x256", "512x512"
                };
                const bool explicitGlobalNoise = StartsWith(
                    semanticFamily, "global-noise-domain-");
                if (!explicitGlobalNoise)
                {
                    const bool accumulate =
                        (noiseVariant % 2u) != 0u ||
                        semanticFamily ==
                            "lighting-accumulation-fixed-cumulative";
                    const bool animate =
                        ((noiseVariant / 2u) % 2u) != 0u;
                    Set(variant, "noise.pattern", std::string(
                        GlobalPatterns[(noiseVariant / 16u) %
                            GlobalPatterns.size()]));
                    Set(variant, "noise.resolution", std::string(
                        GlobalResolutions[(noiseVariant / 4u) %
                            GlobalResolutions.size()]));
                    Set(variant, "noise.animate-samples",
                        animate ? "on" : "off");
                    Set(variant, "noise.accumulate-samples",
                        accumulate ? "on" : "off");
                }
                if (!StartsWith(
                        semanticFamily, "visibility-custom-noise-"))
                {
                    Set(variant, "visibility.specify-noise", "off");
                }
                const bool accumulate =
                    Get(variant, "noise.accumulate-samples") == "on";
                const std::string_view visibilityDebugView =
                    Get(variant, "debug.visibility.view");
                const bool accumulationSelected = accumulate &&
                    (visibilityDebugView.empty() ||
                        visibilityDebugView == "final");
                variant.assertLightingAccumulationState = true;
                variant.expectLightingAccumulation = accumulationSelected;
                if (accumulate)
                {
                    // Accumulation mode never chains an AO/GI denoiser. The
                    // final view commits the cumulative mean; diagnostic
                    // views expose the corresponding raw signals.
                    variant.expectAmbientOcclusionDenoising = false;
                    variant.expectGlobalIlluminationDenoising = false;
                }
                if (Get(variant, "visibility.ao.strength") == "0")
                    variant.expectAmbientOcclusionDenoising = false;
                if (Get(variant, "visibility.gi.intensity") == "0")
                    variant.expectGlobalIlluminationDenoising = false;
                variant.semanticFamily = semanticCase
                    ? semanticFamily
                    : std::string{};
                variant.semanticDomain = semanticCase
                    ? semanticDomain
                    : std::string{};
                variant.requireCrossCaseDistinctness = semanticCase;
                variant.name += "-" + std::string(initialLabel) +
                    "-to-" + std::string(finalLabel) + "-" +
                    std::to_string(samples) + "x";
                variant.actionBaselineSceneToken = initialToken;
                variant.expectedSceneToken = finalToken;
                variant.expectedSampleCount = samples;
                variant.expectDirectionalVisibility = true;
                variant.snapshotRoundTrip = true;
                variant.exerciseRetainedStateChanges = true;
                variant.actionSettingName = "scene.current";
                variant.actionBaselineValue = initialScene;
                variant.actionValue = finalScene;
                variant.resizeWidth = 704;
                variant.resizeHeight = 400;
                Set(variant, "scene.current", initialScene);
                Set(variant, "anti-aliasing.msaa.enabled",
                    samples == 1u ? "off" : "on");
                if (samples > 1u)
                {
                    Set(variant, "anti-aliasing.msaa.samples",
                        std::to_string(samples) + "x");
                }
                visibilityCases.push_back(std::move(variant));
            }
        };

        RetainedRuntimeCase aoOnly =
            Visibility("ao-only-save-load-reset", true, false);
        Set(aoOnly, "denoising.ao.method", "reblur");
        aoOnly.snapshotRoundTrip = true;
        add(std::move(aoOnly));

        RetainedRuntimeCase giOnly =
            Visibility("gi-only-save-load-reset", false, true);
        Set(giOnly, "denoising.gi.method", "relax");
        giOnly.snapshotRoundTrip = true;
        add(std::move(giOnly));

        RetainedRuntimeCase together =
            Visibility("ao-gi-save-load-reset", true, true);
        Set(together, "visibility.samples", "8");
        together.snapshotRoundTrip = true;
        add(std::move(together));

        constexpr std::array<std::string_view, 4> AoMethods = {
            "raw", "joint-bilateral", "gaussian-bilateral", "reblur"
        };
        constexpr std::array<std::string_view, 5> GiMethods = {
            "raw", "joint-bilateral", "gaussian-bilateral", "reblur",
            "relax"
        };
        constexpr std::array<std::string_view, 4> Qualities = {
            "performance", "balanced", "quality", "ultra"
        };
        constexpr std::array<std::string_view, 3> DenoisingResolutions = {
            "quarter", "half", "full"
        };
        constexpr std::array<std::string_view, 3> Estimators = {
            "projected-angle", "solid-angle", "cosine-weighted"
        };
        constexpr std::array<std::string_view, 3> VisibilityResolutions = {
            "full", "half", "quarter"
        };
        constexpr std::array<std::string_view, 7> VisibilitySamples = {
            "1", "2", "4", "8", "16", "32", "64"
        };
        constexpr std::array<std::string_view, 3> NoisePatterns = {
            "spatial-white", "spatial-blue", "spatiotemporal-blue"
        };
        constexpr std::array<std::string_view, 4> NoiseResolutions = {
            "64x64", "128x128", "256x256", "512x512"
        };

        for (const std::string_view quality : {
            "low", "medium", "high", "ultra" })
        {
            RetainedRuntimeCase runtimeCase = Visibility(
                "visibility-preset-" + std::string(quality), true, true);
            Set(runtimeCase, "visibility.quality", std::string(quality));
            add(std::move(runtimeCase));
        }

        for (const std::string_view estimator : Estimators)
        {
            RetainedRuntimeCase runtimeCase = Visibility(
                "visibility-custom-estimator-" +
                    std::string(estimator),
                true,
                true);
            Set(runtimeCase, "visibility.quality", "custom");
            Set(runtimeCase, "visibility.estimator",
                std::string(estimator));
            add(std::move(runtimeCase));
        }

        for (const std::string_view resolution : VisibilityResolutions)
        {
            RetainedRuntimeCase runtimeCase = Visibility(
                "visibility-custom-resolution-" +
                    std::string(resolution),
                true,
                true);
            Set(runtimeCase, "visibility.quality", "custom");
            Set(runtimeCase, "visibility.resolution",
                std::string(resolution));
            add(std::move(runtimeCase));
        }

        for (const std::string_view samples : VisibilitySamples)
        {
            RetainedRuntimeCase runtimeCase = Visibility(
                "visibility-custom-samples-" + std::string(samples),
                true,
                true);
            Set(runtimeCase, "visibility.quality", "custom");
            Set(runtimeCase, "visibility.samples", std::string(samples));
            add(std::move(runtimeCase));
        }

        RetainedRuntimeCase cumulative = Visibility(
            "lighting-accumulation-fixed-cumulative", true, true);
        Set(cumulative, "noise.accumulate-samples", "on");
        cumulative.expectLightingAccumulation = true;
        add(std::move(cumulative));

        for (const std::string_view aoMethod : AoMethods)
        {
            for (const std::string_view giMethod : GiMethods)
            {
                RetainedRuntimeCase runtimeCase = Visibility(
                    "ao-gi-filter-" + std::string(aoMethod) + "-" +
                        std::string(giMethod), true, true);
                Set(runtimeCase, "denoising.ao.method",
                    std::string(aoMethod));
                Set(runtimeCase, "denoising.gi.method",
                    std::string(giMethod));
                runtimeCase.expectAmbientOcclusionDenoising =
                    aoMethod != "raw";
                runtimeCase.expectGlobalIlluminationDenoising =
                    giMethod != "raw";
                add(std::move(runtimeCase));
            }
        }

        for (const std::string_view aoQuality : Qualities)
        {
            for (const std::string_view giQuality : Qualities)
            {
                RetainedRuntimeCase runtimeCase = Visibility(
                    "ao-gi-quality-" + std::string(aoQuality) + "-" +
                        std::string(giQuality), true, true);
                Set(runtimeCase, "denoising.ao.quality",
                    std::string(aoQuality));
                Set(runtimeCase, "denoising.gi.quality",
                    std::string(giQuality));
                Set(runtimeCase, "denoising.ao.method", "reblur");
                Set(runtimeCase, "denoising.gi.method", "relax");
                add(std::move(runtimeCase));
            }
        }

        for (const std::string_view aoResolution : DenoisingResolutions)
        {
            for (const std::string_view giResolution : DenoisingResolutions)
            {
                RetainedRuntimeCase runtimeCase = Visibility(
                    "ao-gi-resolution-" + std::string(aoResolution) + "-" +
                        std::string(giResolution), true, true);
                Set(runtimeCase, "denoising.ao.resolution",
                    std::string(aoResolution));
                Set(runtimeCase, "denoising.gi.resolution",
                    std::string(giResolution));
                add(std::move(runtimeCase));
            }
        }

        for (const std::string_view pattern : NoisePatterns)
        for (const std::string_view resolution : NoiseResolutions)
        for (const bool animate : { false, true })
        {
            RetainedRuntimeCase runtimeCase = Visibility(
                "visibility-custom-noise-" + std::string(pattern) + "-" +
                    std::string(resolution) +
                    (animate ? "-animated" : "-stationary"),
                true,
                true);
            Set(runtimeCase, "visibility.quality", "custom");
            Set(runtimeCase, "visibility.specify-noise", "on");
            Set(runtimeCase, "visibility.noise-pattern",
                std::string(pattern));
            Set(runtimeCase, "visibility.noise-resolution",
                std::string(resolution));
            Set(runtimeCase, "visibility.animate-samples",
                animate ? "on" : "off");
            add(std::move(runtimeCase));
        }

        for (const std::string_view aoOutput : { "off", "on" })
        for (const std::string_view giOutput : { "off", "on" })
        for (const std::string_view aoPrecision : { "16-bit", "32-bit" })
        for (const std::string_view giPrecision : { "16-bit", "32-bit" })
        {
            RetainedRuntimeCase runtimeCase = Visibility(
                "ao-gi-hit-precision-" + std::string(aoOutput) + "-" +
                    std::string(giOutput) + "-" +
                    std::string(aoPrecision) + "-" +
                    std::string(giPrecision), true, true);
            Set(runtimeCase, "visibility.ao.output-hit-distance",
                std::string(aoOutput));
            Set(runtimeCase, "visibility.gi.output-hit-distance",
                std::string(giOutput));
            Set(runtimeCase, "denoising.ao.method",
                aoOutput == "on" ? "reblur" : "joint-bilateral");
            Set(runtimeCase, "denoising.gi.method",
                giOutput == "on" ? "relax" : "joint-bilateral");
            Set(runtimeCase, "visibility.ao.precision",
                std::string(aoPrecision));
            Set(runtimeCase, "visibility.gi.precision",
                std::string(giPrecision));
            add(std::move(runtimeCase));
        }

        struct ContinuousControl
        {
            std::string_view name;
            std::string_view label;
            std::string_view minimum;
            std::string_view maximum;
        };
        constexpr std::array<ContinuousControl, 13> ContinuousControls = {{
            { "visibility.radius", "visibility-radius", "0.1", "10" },
            { "visibility.thickness", "visibility-thickness", "0.01", "2" },
            { "visibility.distribution", "visibility-distribution", "0.25", "8" },
            { "visibility.ao.strength", "ao-strength", "0", "8" },
            { "visibility.gi.intensity", "gi-intensity", "0", "16" },
            { "denoising.ao.radius", "ao-denoising-radius", "1", "8" },
            { "denoising.gi.radius", "gi-denoising-radius", "1", "8" },
            { "denoising.ao.history", "ao-history", "1", "32" },
            { "denoising.gi.history", "gi-history", "1", "32" },
            { "denoising.ao.disocclusion", "ao-disocclusion", "0.001", "0.1" },
            { "denoising.gi.disocclusion", "gi-disocclusion", "0.001", "0.1" },
            { "denoising.ao.anti-lag", "ao-anti-lag", "0", "1" },
            { "denoising.gi.anti-lag", "gi-anti-lag", "0", "1" }
        }};
        for (const ContinuousControl& control : ContinuousControls)
        for (const bool maximum : { false, true })
        {
            RetainedRuntimeCase runtimeCase = Visibility(
                "ao-gi-continuous-" + std::string(control.label) +
                    (maximum ? "-maximum" : "-minimum"),
                true,
                true);
            Set(runtimeCase, std::string(control.name), std::string(
                maximum ? control.maximum : control.minimum));
            add(std::move(runtimeCase));
        }

        for (const std::string_view view : {
            "final", "ambient-visibility", "traced-indirect",
            "applied-indirect" })
        {
            RetainedRuntimeCase runtimeCase = Visibility(
                "debug-visibility-view-" + std::string(view), true, true);
            Set(runtimeCase, "debug.visibility.view", std::string(view));
            // Non-final visibility views present the single-sample
            // visibility composite directly in both raster paths. They still
            // exercise every MSAA pipeline, but their pixels are intentionally
            // independent of raster sample count.
            runtimeCase.requireCrossSampleDistinctness = view == "final";
            add(std::move(runtimeCase));
        }

        for (const std::string_view pattern : NoisePatterns)
        for (const std::string_view resolution : NoiseResolutions)
        for (const bool animate : { false, true })
        for (const bool accumulate : { false, true })
        {
            RetainedRuntimeCase runtimeCase = Visibility(
                "global-noise-domain-" + std::string(pattern) + "-" +
                    std::string(resolution) +
                    (animate ? "-animated" : "-stationary") +
                    (accumulate ? "-cumulative" : "-single"),
                true,
                true);
            Set(runtimeCase, "noise.pattern", std::string(pattern));
            Set(runtimeCase, "noise.resolution", std::string(resolution));
            Set(runtimeCase, "noise.animate-samples",
                animate ? "on" : "off");
            Set(runtimeCase, "noise.accumulate-samples",
                accumulate ? "on" : "off");
            Set(runtimeCase, "visibility.specify-noise", "off");
            add(std::move(runtimeCase));
        }

        for (const std::uint32_t samples : { 1u, 2u, 4u, 8u, 16u })
        {
            const auto configureSamples = [samples](
                RetainedRuntimeCase& runtimeCase)
            {
                runtimeCase.expectedSampleCount = samples;
                Set(runtimeCase, "anti-aliasing.msaa.enabled",
                    samples == 1u ? "off" : "on");
                if (samples > 1u)
                {
                    Set(runtimeCase, "anti-aliasing.msaa.samples",
                        std::to_string(samples) + "x");
                }
            };

            RetainedRuntimeCase unshadowed = Raster(
                "flashlight-visible-unshadowed-" +
                    std::to_string(samples) + "x");
            configureSamples(unshadowed);
            unshadowed.expectFlashlightLightingSubmitted = true;
            unshadowed.assertFlashlightLightingState = true;
            unshadowed.assertFlashlightVisibilityState = true;
            unshadowed.action = RetainedRuntimeAction::ChangeSetting;
            unshadowed.actionSettingName =
                "light.selected.flashlight.enabled";
            unshadowed.actionBaselineValue = "on";
            unshadowed.actionValue = "off";
            unshadowed.requireActionOutputDifference = true;
            Set(unshadowed,
                "light.selected.flashlight.cast-shadows", "off");
            append(std::move(unshadowed));

            RetainedRuntimeCase shadowed = Raster(
                "flashlight-visible-shadowed-" +
                    std::to_string(samples) + "x");
            configureSamples(shadowed);
            shadowed.expectFlashlightLightingSubmitted = true;
            shadowed.assertFlashlightLightingState = true;
            shadowed.expectFlashlightVisibility = true;
            shadowed.assertFlashlightVisibilityState = true;
            shadowed.action = RetainedRuntimeAction::ChangeSetting;
            shadowed.actionSettingName =
                "light.selected.flashlight.cast-shadows";
            shadowed.actionBaselineValue = "on";
            shadowed.actionValue = "off";
            shadowed.requireActionOutputDifference = true;
            Set(shadowed,
                "light.selected.flashlight.enabled", "on");
            Set(shadowed,
                "light.selected.flashlight.output-hit-distance", "off");
            Set(shadowed, "denoising.shadows.method", "raw");
            append(std::move(shadowed));
        }

        for (const std::uint32_t samples : { 1u, 2u, 4u, 8u, 16u })
        {
            RetainedRuntimeCase runtimeCase = Raster(
                "per-sample-directional-sky-flashlight-" +
                    std::to_string(samples) + "x");
            runtimeCase.expectedSampleCount = samples;
            runtimeCase.expectDirectionalVisibility = true;
            runtimeCase.expectSkyVisibility = true;
            runtimeCase.expectFlashlightLightingSubmitted = true;
            runtimeCase.assertFlashlightLightingState = true;
            runtimeCase.expectFlashlightVisibility = true;
            runtimeCase.assertFlashlightVisibilityState = true;
            runtimeCase.expectShadowDenoising = true;
            runtimeCase.expectSkyDenoising = true;
            Set(runtimeCase, "anti-aliasing.msaa.enabled",
                samples == 1u ? "off" : "on");
            if (samples > 1u)
            {
                Set(runtimeCase, "anti-aliasing.msaa.samples",
                    std::to_string(samples) + "x");
            }
            Set(runtimeCase, "sky.visibility.enabled", "on");
            Set(runtimeCase, "sky.visibility.diffuse-ibl", "on");
            Set(runtimeCase, "sky.visibility.specular-ibl", "on");
            Set(runtimeCase, "sky.visibility.samples-per-pixel",
                std::to_string(samples));
            Set(runtimeCase, "sky.visibility.output-hit-distance", "on");
            Set(runtimeCase, "denoising.sky.method", "reblur");
            Set(runtimeCase, "light.selected.flashlight.enabled", "on");
            Set(runtimeCase,
                "light.selected.flashlight.cast-shadows", "on");
            Set(runtimeCase,
                "light.selected.flashlight.output-hit-distance", "on");
            Set(runtimeCase, "denoising.shadows.method", "sigma");
            add(std::move(runtimeCase));
        }

        for (const std::string_view samples : { "32", "64" })
        {
            RetainedRuntimeCase runtimeCase = Raster(
                "sky-quality-" + std::string(samples) + "-rays");
            runtimeCase.expectDirectionalVisibility = true;
            runtimeCase.expectSkyVisibility = true;
            Set(runtimeCase, "sky.visibility.enabled", "on");
            Set(runtimeCase, "sky.visibility.diffuse-ibl", "on");
            Set(runtimeCase, "sky.visibility.samples-per-pixel",
                std::string(samples));
            add(std::move(runtimeCase));
        }

        for (const std::string_view pattern : NoisePatterns)
        for (const std::string_view resolution : NoiseResolutions)
        for (const bool animate : { false, true })
        {
            RetainedRuntimeCase runtimeCase = Raster(
                "sky-custom-noise-" + std::string(pattern) + "-" +
                    std::string(resolution) +
                    (animate ? "-animated" : "-stationary"));
            runtimeCase.expectSkyVisibility = true;
            Set(runtimeCase, "sky.visibility.enabled", "on");
            Set(runtimeCase, "sky.visibility.diffuse-ibl", "on");
            Set(runtimeCase, "sky.visibility.specify-noise", "on");
            Set(runtimeCase, "sky.visibility.noise-pattern",
                std::string(pattern));
            Set(runtimeCase, "sky.visibility.noise-resolution",
                std::string(resolution));
            Set(runtimeCase, "sky.visibility.animate-samples",
                animate ? "on" : "off");
            append(std::move(runtimeCase));
        }

        constexpr std::array<std::string_view, 4> ShadowMethods = {
            "raw", "joint-bilateral", "gaussian-bilateral", "sigma"
        };
        for (const std::string_view method : ShadowMethods)
        {
            RetainedRuntimeCase runtimeCase = Raster(
                "flashlight-shadow-denoising-" + std::string(method));
            runtimeCase.expectFlashlightVisibility = true;
            runtimeCase.expectFlashlightLightingSubmitted = true;
            runtimeCase.assertFlashlightLightingState = true;
            runtimeCase.assertFlashlightVisibilityState = true;
            runtimeCase.expectShadowDenoising = method != "raw";
            Set(runtimeCase, "light.selected.flashlight.enabled", "on");
            Set(runtimeCase,
                "light.selected.flashlight.cast-shadows", "on");
            Set(runtimeCase,
                "light.selected.flashlight.output-hit-distance", "on");
            Set(runtimeCase, "denoising.shadows.method",
                std::string(method));
            append(std::move(runtimeCase));
        }
        for (const std::string_view quality : Qualities)
        for (const std::string_view resolution : DenoisingResolutions)
        {
            RetainedRuntimeCase runtimeCase = Raster(
                "flashlight-shadow-denoising-" + std::string(quality) +
                    "-" + std::string(resolution));
            runtimeCase.expectFlashlightVisibility = true;
            runtimeCase.expectFlashlightLightingSubmitted = true;
            runtimeCase.assertFlashlightLightingState = true;
            runtimeCase.assertFlashlightVisibilityState = true;
            runtimeCase.expectShadowDenoising = true;
            Set(runtimeCase, "light.selected.flashlight.enabled", "on");
            Set(runtimeCase,
                "light.selected.flashlight.cast-shadows", "on");
            Set(runtimeCase,
                "light.selected.flashlight.output-hit-distance", "on");
            Set(runtimeCase, "denoising.shadows.method", "sigma");
            Set(runtimeCase, "denoising.shadows.quality",
                std::string(quality));
            Set(runtimeCase, "denoising.shadows.resolution",
                std::string(resolution));
            append(std::move(runtimeCase));
        }

        for (const std::string_view method : GiMethods)
        {
            RetainedRuntimeCase runtimeCase = Raster(
                "sky-denoising-" + std::string(method));
            runtimeCase.expectSkyVisibility = true;
            runtimeCase.expectSkyDenoising = method != "raw";
            Set(runtimeCase, "sky.visibility.enabled", "on");
            Set(runtimeCase, "sky.visibility.diffuse-ibl", "on");
            Set(runtimeCase, "sky.visibility.output-hit-distance", "on");
            Set(runtimeCase, "denoising.sky.method", std::string(method));
            append(std::move(runtimeCase));
        }
        for (const std::string_view quality : Qualities)
        for (const std::string_view resolution : DenoisingResolutions)
        {
            RetainedRuntimeCase runtimeCase = Raster(
                "sky-denoising-" + std::string(quality) + "-" +
                    std::string(resolution));
            runtimeCase.expectSkyVisibility = true;
            runtimeCase.expectSkyDenoising = true;
            Set(runtimeCase, "sky.visibility.enabled", "on");
            Set(runtimeCase, "sky.visibility.diffuse-ibl", "on");
            Set(runtimeCase, "sky.visibility.output-hit-distance", "on");
            Set(runtimeCase, "denoising.sky.method", "reblur");
            Set(runtimeCase, "denoising.sky.quality", std::string(quality));
            Set(runtimeCase, "denoising.sky.resolution",
                std::string(resolution));
            append(std::move(runtimeCase));
        }

        std::size_t environmentIndex = 0u;
        for (const std::string_view environment : {
            "day", "bright-overcast", "soft-day", "night",
            "starry-night", "cloudy" })
        {
            RetainedRuntimeCase runtimeCase = Raster(
                "hdr-environment-" + std::string(environment));
            runtimeCase.expectSkyVisibility = true;
            Set(runtimeCase, "sky.environment", std::string(environment));
            Set(runtimeCase, "sky.visibility.enabled", "on");
            Set(runtimeCase, "sky.visibility.diffuse-ibl", "on");
            Set(runtimeCase, "sky.visibility.samples-per-pixel", "4");
            const bool automaticExposure =
                (environmentIndex % 2u) != 0u;
            Set(runtimeCase, "sky.auto-exposure.enabled",
                automaticExposure ? "on" : "off");
            if (automaticExposure)
            {
                constexpr std::array<std::string_view, 3> Compensation = {
                    "-18", "0", "8"
                };
                constexpr std::array<std::string_view, 3> Movement = {
                    "0", "8", "16"
                };
                constexpr std::array<std::string_view, 3> Period = {
                    "0.05", "0.2", "5"
                };
                const std::size_t profile = environmentIndex / 2u;
                Set(runtimeCase,
                    "sky.auto-exposure.exposure-compensation",
                    std::string(Compensation[profile]));
                Set(runtimeCase,
                    "sky.auto-exposure.maximum-brightening",
                    std::string(Movement[profile]));
                Set(runtimeCase,
                    "sky.auto-exposure.maximum-darkening",
                    std::string(Movement[2u - profile]));
                Set(runtimeCase,
                    "sky.auto-exposure.adjustment-period",
                    std::string(Period[profile]));
            }
            runtimeCase.expectAutoExposure = automaticExposure;
            runtimeCase.assertAutoExposureState = true;
            add(std::move(runtimeCase));
            ++environmentIndex;
        }

        // Each AO/GI configuration starts in the scene left active by the
        // preceding row, then switches once. This proves both retained scenes
        // without bouncing through an unrelated factory-default scene.
        for (RetainedRuntimeCase& runtimeCase : visibilityCases)
            append(std::move(runtimeCase));

        for (const std::uint32_t samples : { 1u, 2u, 4u, 8u, 16u })
        {
            const bool startsInBistro =
                visibilityVariantIndex++ % 2u == 0u;
            const std::string& initialScene = startsInBistro
                ? bistroScene
                : sanMiguelScene;
            const std::string& finalScene = startsInBistro
                ? sanMiguelScene
                : bistroScene;
            RetainedRuntimeCase runtimeCase = Visibility(
                "scene-mixed-coverage-" +
                    std::string(startsInBistro
                        ? "bistro-to-san-miguel-"
                        : "san-miguel-to-bistro-") +
                    std::to_string(samples) + "x",
                true,
                true);
            runtimeCase.expectedSampleCount = samples;
            runtimeCase.expectDirectionalVisibility = true;
            runtimeCase.expectSkyVisibility = true;
            runtimeCase.expectFlashlightVisibility = true;
            runtimeCase.expectFlashlightLightingSubmitted = true;
            runtimeCase.assertFlashlightLightingState = true;
            runtimeCase.assertFlashlightVisibilityState = true;
            runtimeCase.expectShadowDenoising = true;
            runtimeCase.expectSkyDenoising = true;
            runtimeCase.snapshotRoundTrip = true;
            runtimeCase.exerciseRetainedStateChanges = true;
            runtimeCase.actionSettingName = "scene.current";
            runtimeCase.actionBaselineValue = initialScene;
            runtimeCase.actionValue = finalScene;
            runtimeCase.actionBaselineSceneToken = startsInBistro
                ? "bistro_interior_retextured"
                : "san_miguel_retextured";
            runtimeCase.expectedSceneToken = startsInBistro
                ? "san_miguel_retextured"
                : "bistro_interior_retextured";
            runtimeCase.resizeWidth = 704;
            runtimeCase.resizeHeight = 400;
            Set(runtimeCase, "scene.current", initialScene);
            Set(runtimeCase, "anti-aliasing.msaa.enabled",
                samples == 1u ? "off" : "on");
            if (samples > 1u)
            {
                Set(runtimeCase, "anti-aliasing.msaa.samples",
                    std::to_string(samples) + "x");
            }
            Set(runtimeCase, "sky.visibility.enabled", "on");
            Set(runtimeCase, "sky.visibility.diffuse-ibl", "on");
            Set(runtimeCase, "sky.visibility.specular-ibl", "on");
            Set(runtimeCase, "sky.visibility.samples-per-pixel", "8");
            Set(runtimeCase, "sky.visibility.output-hit-distance", "on");
            Set(runtimeCase, "denoising.sky.method", "reblur");
            Set(runtimeCase,
                "light.selected.flashlight.enabled", "on");
            Set(runtimeCase,
                "light.selected.flashlight.cast-shadows", "on");
            Set(runtimeCase,
                "light.selected.flashlight.output-hit-distance", "on");
            Set(runtimeCase, "denoising.shadows.method", "sigma");
            append(std::move(runtimeCase));
        }

        const auto pathCase = [](
            std::string name,
            const std::string& scene)
        {
            RetainedRuntimeCase runtimeCase;
            runtimeCase.name = std::move(name);
            runtimeCase.expectedSampleCount = 1u;
            runtimeCase.expectedPathHistoryCount = 3u;
            runtimeCase.settings = {
                { "scene.current", scene },
                { "anti-aliasing.msaa.enabled", "off" },
                { "visibility.enabled", "off" },
                { "sky.visibility.enabled", "off" },
                { "lighting.solution", "path-tracing" }
            };
            return runtimeCase;
        };

        RetainedRuntimeCase bistro =
            pathCase("path-tracing-bistro", bistroScene);
        bistro.snapshotRoundTrip = true;
        bistro.expectedSceneToken = "bistro_interior_retextured";
        add(std::move(bistro));

        RetainedRuntimeCase camera =
            pathCase("path-history-camera-reset", bistroScene);
        camera.action = RetainedRuntimeAction::NudgeCamera;
        camera.requirePathHistoryRestart = true;
        camera.expectedSceneToken = "bistro_interior_retextured";
        add(std::move(camera));

        RetainedRuntimeCase resize =
            pathCase("path-history-resize-reset", bistroScene);
        resize.action = RetainedRuntimeAction::ResizeViewport;
        resize.resizeWidth = 800;
        resize.resizeHeight = 448;
        resize.requirePathHistoryRestart = true;
        resize.expectedSceneToken = "bistro_interior_retextured";
        add(std::move(resize));

        RetainedRuntimeCase san = pathCase(
            "path-tracing-san-miguel-scene-reset", sanMiguelScene);
        san.snapshotRoundTrip = true;
        san.action = RetainedRuntimeAction::ChangeScene;
        san.actionSettingName = "scene.current";
        san.actionBaselineValue = bistroScene;
        san.actionBaselineSceneToken = "bistro_interior_retextured";
        san.actionValue = sanMiguelScene;
        san.requirePathHistoryRestart = true;
        san.expectedSceneToken = "san_miguel_retextured";
        add(std::move(san));

        const auto addPathRestart = [
            &add, &pathCase, &bistroScene](
                std::string name,
                RetainedRuntimeAction action,
                std::string settingName = {},
                std::string baselineValue = {},
                std::string actionValue = {},
                bool requireOutputDifference = false)
        {
            RetainedRuntimeCase runtimeCase =
                pathCase(std::move(name), bistroScene);
            runtimeCase.action = action;
            runtimeCase.actionSettingName = std::move(settingName);
            runtimeCase.actionBaselineValue = std::move(baselineValue);
            runtimeCase.actionValue = std::move(actionValue);
            runtimeCase.requirePathHistoryRestart = true;
            runtimeCase.requireActionOutputDifference =
                requireOutputDifference;
            runtimeCase.expectedSceneToken =
                "bistro_interior_retextured";
            add(std::move(runtimeCase));
        };

        addPathRestart(
            "path-history-environment-reset",
            RetainedRuntimeAction::ChangeSetting,
            "sky.environment", "day", "night", true);
        addPathRestart(
            "path-history-exposure-reset",
            RetainedRuntimeAction::ChangeSetting,
            "sky.exposure", "-2.75", "-1.75", true);
        addPathRestart(
            "path-history-global-noise-reset",
            RetainedRuntimeAction::ChangeSetting,
            "noise.pattern", "spatiotemporal-blue", "spatial-blue");
        addPathRestart(
            "path-history-material-reset",
            RetainedRuntimeAction::ChangeMaterial);
        addPathRestart(
            "path-history-light-reset",
            RetainedRuntimeAction::ChangeLight);
        addPathRestart(
            "path-history-flashlight-reset",
            RetainedRuntimeAction::ToggleFlashlight);
        addPathRestart(
            "path-history-lighting-solution-cycle",
            RetainedRuntimeAction::CycleLightingSolution);
        return cases;
    }

    RetainedRuntimeDiagnosticState::RetainedRuntimeDiagnosticState(
        std::vector<RetainedRuntimeCase> cases,
        Clock::time_point start)
        : m_Cases(std::move(cases))
        , m_Start(start)
        , m_CaseStart(start)
    {
    }

    std::int64_t RetainedRuntimeDiagnosticState::ElapsedMilliseconds(
        Clock::time_point now) const noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_Start).count();
    }

    std::size_t RetainedRuntimeDiagnosticState::PassedCaseCount() const noexcept
    {
        return m_PassedCases;
    }

    std::size_t RetainedRuntimeDiagnosticState::TotalCaseCount() const noexcept
    {
        return m_Cases.size();
    }

    bool RetainedRuntimeDiagnosticState::RequiresSettingsSnapshot() const noexcept
    {
        if (m_CaseIndex >= m_Cases.size() ||
            !m_Cases[m_CaseIndex].snapshotRoundTrip ||
            m_SnapshotCompleted)
        {
            return false;
        }
        return m_Phase == Phase::WaitForEvidence ||
            m_Phase == Phase::WaitForResetFrame ||
            m_Phase == Phase::WaitForRestoredEvidence;
    }

    RetainedRuntimeDirective RetainedRuntimeDiagnosticState::Finish(
        bool passed,
        std::string message)
    {
        m_Phase = Phase::Complete;
        return {
            passed
                ? RetainedRuntimeDirectiveKind::FinishPass
                : RetainedRuntimeDirectiveKind::FinishFail,
            m_CaseIndex < m_Cases.size()
                ? &m_Cases[m_CaseIndex]
                : nullptr,
            m_CaseIndex,
            std::move(message)
        };
    }

    RetainedRuntimeDirective RetainedRuntimeDiagnosticState::Abort(
        std::string message,
        Clock::time_point)
    {
        return Finish(false, std::move(message));
    }

    bool RetainedRuntimeDiagnosticState::EvidenceReady(
        const RetainedRuntimeCase& runtimeCase,
        const RetainedRuntimeTelemetry& telemetry,
        bool beforeAction,
        std::string& reason)
    {
        if (telemetry.sceneBusy)
        {
            reason = "scene is busy";
            return false;
        }
        if (!telemetry.sceneLoaded)
        {
            reason = "scene failed to load";
            return false;
        }
        const bool baselineScenePhase = beforeAction ||
            m_CurrentAction == RetainedRuntimeAction::NudgeCamera ||
            m_CurrentAction == RetainedRuntimeAction::ResizeViewport;
        const std::string& expectedScene =
            baselineScenePhase &&
                !runtimeCase.actionBaselineSceneToken.empty()
                ? runtimeCase.actionBaselineSceneToken
                : runtimeCase.expectedSceneToken;
        if (!expectedScene.empty() &&
            LowerAscii(telemetry.currentScene).find(expectedScene) ==
                std::string::npos)
        {
            reason = "active scene does not match '" +
                expectedScene + "'";
            return false;
        }
        if (runtimeCase.expectedSampleCount > 0u &&
            telemetry.receiverSampleCount != runtimeCase.expectedSampleCount)
        {
            reason = "receiver sample count is " +
                std::to_string(telemetry.receiverSampleCount) +
                ", expected " +
                std::to_string(runtimeCase.expectedSampleCount);
            return false;
        }
        const bool disabledVisibilityReference =
            runtimeCase.exerciseRetainedStateChanges &&
            m_CurrentAction == RetainedRuntimeAction::ChangeSetting &&
            m_CompletedActionCount == 3u;
        if (disabledVisibilityReference &&
            telemetry.screenVisibilityDispatched)
        {
            reason = "visibility-off reference still dispatched AO/GI";
            return false;
        }
        if (!disabledVisibilityReference &&
            runtimeCase.expectScreenVisibility &&
            !telemetry.screenVisibilityDispatched)
        {
            reason = "AO/GI visibility did not dispatch";
            return false;
        }
        if (runtimeCase.expectDirectionalVisibility &&
            !telemetry.directionalVisibilityDispatched)
        {
            reason = "directional per-sample visibility did not dispatch";
            return false;
        }
        if (runtimeCase.expectSkyVisibility &&
            !telemetry.skyVisibilityDispatched)
        {
            reason = "sky per-sample visibility did not dispatch";
            return false;
        }
        const bool flashlightDisabledReference = !beforeAction &&
            m_CurrentAction == RetainedRuntimeAction::ChangeSetting &&
            runtimeCase.actionSettingName ==
                "light.selected.flashlight.enabled" &&
            runtimeCase.actionValue == "off";
        const bool flashlightShadowsDisabledReference = !beforeAction &&
            m_CurrentAction == RetainedRuntimeAction::ChangeSetting &&
            (flashlightDisabledReference ||
                (runtimeCase.actionSettingName ==
                        "light.selected.flashlight.cast-shadows" &&
                    runtimeCase.actionValue == "off"));
        const bool expectedFlashlightLighting =
            runtimeCase.expectFlashlightLightingSubmitted &&
            !flashlightDisabledReference;
        if (runtimeCase.assertFlashlightLightingState &&
            telemetry.flashlightLightingSubmitted !=
                expectedFlashlightLighting)
        {
            reason = expectedFlashlightLighting
                ? "flashlight lighting was not submitted"
                : "disabled flashlight still contributed direct lighting";
            return false;
        }
        const bool expectedFlashlightVisibility =
            runtimeCase.expectFlashlightVisibility &&
            !flashlightShadowsDisabledReference;
        if (runtimeCase.assertFlashlightVisibilityState &&
            telemetry.flashlightVisibilityDispatched !=
                expectedFlashlightVisibility)
        {
            reason = expectedFlashlightVisibility
                ? "flashlight per-sample visibility did not dispatch"
                : "flashlight visibility dispatched while shadows were disabled";
            return false;
        }
        if (!runtimeCase.assertFlashlightVisibilityState &&
            runtimeCase.expectFlashlightVisibility &&
            !telemetry.flashlightVisibilityDispatched)
        {
            reason = "flashlight per-sample visibility did not dispatch";
            return false;
        }
        if (!disabledVisibilityReference &&
            runtimeCase.expectShadowDenoising &&
            !telemetry.shadowDenoisingDispatched)
        {
            reason = "shadow denoising did not dispatch";
            return false;
        }
        if (!disabledVisibilityReference &&
            runtimeCase.expectSkyDenoising &&
            !telemetry.skyDenoisingDispatched)
        {
            reason = "sky denoising did not dispatch";
            return false;
        }
        if (!disabledVisibilityReference &&
            runtimeCase.expectAmbientOcclusionDenoising &&
            !telemetry.ambientOcclusionDenoisingDispatched)
        {
            reason = "AO denoising did not dispatch";
            return false;
        }
        if (!disabledVisibilityReference &&
            runtimeCase.expectGlobalIlluminationDenoising &&
            !telemetry.globalIlluminationDenoisingDispatched)
        {
            reason = "GI denoising did not dispatch";
            return false;
        }
        if (!disabledVisibilityReference &&
            runtimeCase.expectLightingAccumulation &&
            !telemetry.lightingAccumulationCommitted)
        {
            reason = "fixed cumulative lighting history did not commit";
            return false;
        }
        if (!disabledVisibilityReference &&
            runtimeCase.expectLightingAccumulation &&
            (telemetry.ambientOcclusionDenoisingDispatched ||
                telemetry.globalIlluminationDenoisingDispatched))
        {
            reason = "fixed cumulative lighting also dispatched AO/GI denoising";
            return false;
        }
        if (runtimeCase.assertLightingAccumulationState &&
            telemetry.lightingAccumulationCommitted !=
                runtimeCase.expectLightingAccumulation)
        {
            reason = runtimeCase.expectLightingAccumulation
                ? "global sample accumulation did not commit"
                : "global sample accumulation committed while disabled";
            return false;
        }
        if (runtimeCase.assertAutoExposureState &&
            telemetry.autoExposureDispatched !=
                runtimeCase.expectAutoExposure)
        {
            reason = runtimeCase.expectAutoExposure
                ? "auto exposure did not dispatch"
                : "auto exposure dispatched while disabled";
            return false;
        }
        const std::string_view expectedNoisePattern =
            Get(runtimeCase, "noise.pattern");
        const std::string_view expectedNoiseResolution =
            Get(runtimeCase, "noise.resolution");
        const std::string_view expectedNoiseAnimate =
            Get(runtimeCase, "noise.animate-samples");
        const std::string_view expectedNoiseAccumulate =
            Get(runtimeCase, "noise.accumulate-samples");
        if ((!expectedNoisePattern.empty() &&
                telemetry.globalNoisePattern != expectedNoisePattern) ||
            (!expectedNoiseResolution.empty() &&
                telemetry.globalNoiseResolution != expectedNoiseResolution) ||
            (!expectedNoiseAnimate.empty() &&
                telemetry.globalNoiseAnimateSamples !=
                    (expectedNoiseAnimate == "on")) ||
            (!expectedNoiseAccumulate.empty() &&
                telemetry.globalNoiseAccumulateSamples !=
                    (expectedNoiseAccumulate == "on")))
        {
            reason = "the applied global noise state did not match the case";
            return false;
        }
        if (!beforeAction &&
            m_CurrentAction != RetainedRuntimeAction::None &&
            telemetry.lastAppliedAction != m_CurrentAction)
        {
            reason = "the named runtime action was not applied";
            return false;
        }
        if (runtimeCase.expectedPathHistoryCount > 0u)
        {
            if (!beforeAction &&
                runtimeCase.requirePathHistoryRestart &&
                telemetry.pathHistoryCount < m_PathCountBeforeAction)
            {
                m_ObservedPathRestart = true;
            }
            if (!beforeAction &&
                runtimeCase.requirePathHistoryRestart &&
                !m_ObservedPathRestart)
            {
                reason = "path history did not restart below its prior count";
                return false;
            }
            if (telemetry.pathHistoryCount <
                runtimeCase.expectedPathHistoryCount)
            {
                reason = "path history count is " +
                    std::to_string(telemetry.pathHistoryCount) +
                    ", expected at least " +
                    std::to_string(runtimeCase.expectedPathHistoryCount);
                return false;
            }
        }
        reason.clear();
        return true;
    }

    RetainedRuntimeDirective RetainedRuntimeDiagnosticState::Tick(
        const RetainedRuntimeTelemetry& telemetry,
        Clock::time_point now)
    {
        if (m_Phase == Phase::Complete)
            return {};
        if (now - m_Start > RetainedRuntimeGlobalTimeout)
            return Finish(false, "global six-hour timeout expired");

        if (m_Phase == Phase::Apply)
        {
            if (m_CaseIndex >= m_Cases.size())
            {
                std::string semanticReason;
                if (!ValidateRetainedRuntimeSemanticCaptures(
                        m_Cases, m_SemanticCaptures, semanticReason))
                {
                    return Finish(false, std::move(semanticReason));
                }
                return Finish(true, {});
            }
            m_CaseStart = now;
            m_SettledFrames = 0u;
            m_CompletedActionCount = 0u;
            m_CurrentAction = RetainedRuntimeAction::None;
            m_PathCountBeforeAction = 0u;
            m_ObservedPathRestart =
                !m_Cases[m_CaseIndex].requirePathHistoryRestart;
            m_SnapshotCompleted =
                !m_Cases[m_CaseIndex].snapshotRoundTrip;
            m_TimingRecoverySampleUsed = false;
            m_LastActiveLinearHash = 0u;
            m_BaselineCpuMilliseconds = 0.0;
            m_BaselineGpuMilliseconds = 0.0;
            m_CaptureCpuMilliseconds = 0.0;
            m_CaptureGpuMilliseconds = 0.0;
            m_SavedSnapshot.clear();
            m_CaptureLabel.clear();
            m_WaitReason.clear();
            m_Phase = Phase::WaitForEvidence;
            return {
                RetainedRuntimeDirectiveKind::ApplyCase,
                &m_Cases[m_CaseIndex],
                m_CaseIndex,
                {}
            };
        }

        if (now - m_CaseStart > std::chrono::minutes(5))
        {
            return Finish(false, "case timeout: " + m_WaitReason);
        }
        RetainedRuntimeCase& runtimeCase = m_Cases[m_CaseIndex];

        if (m_Phase == Phase::WaitForResetFrame)
        {
            if (telemetry.sceneBusy)
                return {};
            if (++m_SettledFrames < 2u)
                return {};
            if (!telemetry.settingsSnapshot)
                return Finish(false, "RESET snapshot was not observed");
            if (*telemetry.settingsSnapshot == m_SavedSnapshot)
            {
                return Finish(false,
                    "RESET left the non-default snapshot unchanged");
            }
            m_SettledFrames = 0u;
            m_Phase = Phase::WaitForRestoredEvidence;
            return {
                RetainedRuntimeDirectiveKind::RestoreSnapshot,
                &runtimeCase,
                m_CaseIndex,
                m_SavedSnapshot
            };
        }

        if (m_Phase == Phase::WaitForEvidence ||
            m_Phase == Phase::WaitForRestoredEvidence)
        {
            const bool beforeAction =
                m_CurrentAction == RetainedRuntimeAction::None;
            if (!EvidenceReady(
                    runtimeCase,
                    telemetry,
                    beforeAction,
                    m_WaitReason))
            {
                m_SettledFrames = 0u;
                m_TimingRecoverySampleUsed = false;
                return {};
            }
            if (++m_SettledFrames < 2u)
                return {};

            if (m_Phase == Phase::WaitForEvidence &&
                !m_SnapshotCompleted)
            {
                if (!telemetry.settingsSnapshot)
                    return Finish(false, "configured snapshot was not observed");
                m_SavedSnapshot = *telemetry.settingsSnapshot;
                m_SettledFrames = 0u;
                m_TimingRecoverySampleUsed = false;
                m_Phase = Phase::WaitForResetFrame;
                return {
                    RetainedRuntimeDirectiveKind::ResetSettings,
                    &runtimeCase,
                    m_CaseIndex,
                    {}
                };
            }
            if (m_Phase == Phase::WaitForRestoredEvidence &&
                (!telemetry.settingsSnapshot ||
                    *telemetry.settingsSnapshot != m_SavedSnapshot))
            {
                return Finish(false,
                    "saved settings did not restore the exact live snapshot" +
                    (telemetry.settingsSnapshot
                        ? DescribeSnapshotMismatch(
                            m_SavedSnapshot, *telemetry.settingsSnapshot)
                        : ": live snapshot was unavailable"));
            }
            if (!std::isfinite(telemetry.cpuFrameMilliseconds) ||
                telemetry.cpuFrameMilliseconds <= 0.0)
            {
                return Finish(false,
                    "stable CPU frame timing was unavailable or non-finite");
            }
            if (!telemetry.gpuFrameTimingAvailable)
            {
                m_SettledFrames = 0u;
                m_WaitReason = "stable GPU frame timing was unavailable";
                return {};
            }
            if (!std::isfinite(telemetry.gpuFrameMilliseconds) ||
                telemetry.gpuFrameMilliseconds <= 0.0)
            {
                return Finish(false,
                    "stable GPU frame timing was unavailable or non-finite");
            }
            m_CaptureCpuMilliseconds = telemetry.cpuFrameMilliseconds;
            m_CaptureGpuMilliseconds = telemetry.gpuFrameMilliseconds;
            const auto deferOneTimingSample =
                [this](std::string waitReason)
                {
                    if (m_TimingRecoverySampleUsed)
                        return false;
                    m_TimingRecoverySampleUsed = true;
                    m_SettledFrames = 1u;
                    m_WaitReason = std::move(waitReason);
                    return true;
                };

            if (m_CurrentAction == RetainedRuntimeAction::None)
            {
                if (m_CaptureCpuMilliseconds >
                        MaximumBaselineFrameMilliseconds ||
                    m_CaptureGpuMilliseconds >
                        MaximumBaselineFrameMilliseconds)
                {
                    if (deferOneTimingSample(
                            "waiting for one post-transition baseline timing "
                            "sample"))
                    {
                        return {};
                    }
                    return Finish(false,
                        "stable baseline frame time exceeded 1000 ms after "
                        "one recovery sample: CPU=" +
                        std::to_string(m_CaptureCpuMilliseconds) +
                        " ms, GPU=" +
                        std::to_string(m_CaptureGpuMilliseconds) + " ms");
                }
            }
            else
            {
                const double cpuLimit = std::max(
                    m_BaselineCpuMilliseconds *
                        FrameTimingRelativeTolerance,
                    m_BaselineCpuMilliseconds +
                        FrameTimingAdditiveToleranceMilliseconds);
                const double gpuLimit = std::max(
                    m_BaselineGpuMilliseconds *
                        FrameTimingRelativeTolerance,
                    m_BaselineGpuMilliseconds +
                        FrameTimingAdditiveToleranceMilliseconds);
                if (m_CaptureCpuMilliseconds > cpuLimit ||
                    m_CaptureGpuMilliseconds > gpuLimit)
                {
                    if (deferOneTimingSample(
                            "waiting for one post-transition action timing "
                            "sample"))
                    {
                        return {};
                    }
                    return Finish(false,
                        "stable post-action frame time exceeded the 4x/50 ms "
                        "baseline tolerance after one recovery sample: CPU=" +
                        std::to_string(m_CaptureCpuMilliseconds) +
                        " ms (limit " + std::to_string(cpuLimit) +
                        " ms), GPU=" +
                        std::to_string(m_CaptureGpuMilliseconds) +
                        " ms (limit " + std::to_string(gpuLimit) + " ms)");
                }
            }
            if (m_Phase == Phase::WaitForRestoredEvidence)
                m_SnapshotCompleted = true;
            m_CaptureLabel = CaptureLabel(m_CurrentAction);
            m_Phase = Phase::WaitForCapture;
            RetainedRuntimeDirective directive{
                RetainedRuntimeDirectiveKind::CaptureOutput,
                &runtimeCase,
                m_CaseIndex,
                m_CaptureLabel
            };
            directive.hasStableFrameTiming = true;
            directive.stableCpuFrameMilliseconds =
                m_CaptureCpuMilliseconds;
            directive.stableGpuFrameMilliseconds =
                m_CaptureGpuMilliseconds;
            return directive;
        }

        if (m_Phase == Phase::WaitForCapture)
        {
            if (!telemetry.output)
                return {};
            const RuntimeOutputEvidence& output = *telemetry.output;
            if (!output.valid || output.pixelBytes == 0u ||
                output.minimumByte == output.maximumByte ||
                !output.linearReadbackValid ||
                output.nonFiniteComponentCount != 0u)
            {
                return Finish(false,
                    "rendered output was empty, uniform, or non-finite: "
                    "encoded-valid=" + std::to_string(output.valid) +
                    ", pixel-bytes=" + std::to_string(output.pixelBytes) +
                    ", byte-range=" +
                    std::to_string(static_cast<unsigned int>(
                        output.minimumByte)) + ".." +
                    std::to_string(static_cast<unsigned int>(
                        output.maximumByte)) +
                    ", linear-valid=" +
                    std::to_string(output.linearReadbackValid) +
                    ", finite=" +
                    std::to_string(output.finiteComponentCount) +
                    ", non-finite=" +
                    std::to_string(output.nonFiniteComponentCount) +
                    ", varying=" +
                    std::to_string(output.varyingPixelCount) +
                    ", edges=" + std::to_string(output.edgePixelCount) +
                    ", linear-range=" +
                    std::to_string(output.minimumLinearValue) + ".." +
                    std::to_string(output.maximumLinearValue));
            }
            if (m_CurrentAction ==
                    RetainedRuntimeAction::ResizeViewport &&
                (output.width != static_cast<std::uint32_t>(
                        runtimeCase.resizeWidth) ||
                    output.height != static_cast<std::uint32_t>(
                        runtimeCase.resizeHeight)))
            {
                return Finish(false,
                    "resize output dimensions did not match the requested viewport");
            }

            if (m_CurrentAction == RetainedRuntimeAction::None)
            {
                m_LastActiveLinearHash = output.linearHash;
                m_BaselineCpuMilliseconds =
                    m_CaptureCpuMilliseconds;
                m_BaselineGpuMilliseconds =
                    m_CaptureGpuMilliseconds;
            }
            else
            {
                const bool referenceCapture =
                    runtimeCase.exerciseRetainedStateChanges &&
                    m_CurrentAction ==
                        RetainedRuntimeAction::ChangeSetting &&
                    m_CompletedActionCount == 3u;
                const bool mustDifferFromPrior =
                    runtimeCase.requireActionOutputDifference ||
                    (runtimeCase.exerciseRetainedStateChanges &&
                        (m_CurrentAction ==
                            RetainedRuntimeAction::NudgeCamera ||
                        m_CurrentAction ==
                            RetainedRuntimeAction::ChangeScene ||
                        referenceCapture));
                if (mustDifferFromPrior &&
                    output.linearHash == m_LastActiveLinearHash)
                {
                    return Finish(false,
                        referenceCapture
                            ? "AO/GI output matched its visibility-off reference"
                            : "named camera/scene action did not change rendered output");
                }
                if (!referenceCapture)
                    m_LastActiveLinearHash = output.linearHash;
            }

            if (runtimeCase.requireCrossCaseDistinctness &&
                (m_CurrentAction == RetainedRuntimeAction::None ||
                    m_CurrentAction == RetainedRuntimeAction::ChangeScene))
            {
                RetainedRuntimeSemanticCapture capture;
                capture.caseName = runtimeCase.name;
                capture.family = runtimeCase.semanticFamily;
                capture.domain = runtimeCase.semanticDomain;
                capture.sceneToken =
                    m_CurrentAction == RetainedRuntimeAction::ChangeScene
                        ? runtimeCase.expectedSceneToken
                        : runtimeCase.actionBaselineSceneToken;
                capture.signature = BuildRuntimeSemanticSignature(
                    output,
                    telemetry.receiverSampleCount,
                    m_CaptureCpuMilliseconds,
                    m_CaptureGpuMilliseconds);
                m_SemanticCaptures.push_back(std::move(capture));
            }

            if (m_CurrentAction != RetainedRuntimeAction::None)
                ++m_CompletedActionCount;
            m_CurrentAction = RetainedRuntimeAction::None;

            if (m_CompletedActionCount < ActionCount(runtimeCase))
            {
                const RetainedRuntimeAction nextAction = ActionAt(
                    runtimeCase, m_CompletedActionCount);
                if (nextAction == RetainedRuntimeAction::None)
                    return Finish(false, "runtime action sequence was incomplete");
                m_CurrentAction = nextAction;
                m_PathCountBeforeAction = telemetry.pathHistoryCount;
                m_ObservedPathRestart =
                    !runtimeCase.requirePathHistoryRestart;
                m_SettledFrames = 0u;
                m_TimingRecoverySampleUsed = false;
                m_Phase = Phase::WaitForEvidence;

                RetainedRuntimeDirective directive{
                    RetainedRuntimeDirectiveKind::ApplyAction,
                    &runtimeCase,
                    m_CaseIndex,
                    m_CaptureLabel
                };
                directive.action = nextAction;
                directive.resizeWidth = runtimeCase.resizeWidth;
                directive.resizeHeight = runtimeCase.resizeHeight;
                if (runtimeCase.exerciseRetainedStateChanges)
                {
                    if (nextAction == RetainedRuntimeAction::ChangeScene)
                    {
                        directive.actionSettingName = "scene.current";
                        directive.actionValue = runtimeCase.actionValue;
                    }
                    else if (nextAction ==
                        RetainedRuntimeAction::ChangeSetting)
                    {
                        directive.actionSettingName = "visibility.enabled";
                        directive.actionValue = "off";
                    }
                }
                else
                {
                    directive.actionSettingName =
                        runtimeCase.actionSettingName;
                    directive.actionValue = runtimeCase.actionValue;
                }
                directive.hasStableFrameTiming = true;
                directive.stableCpuFrameMilliseconds =
                    m_CaptureCpuMilliseconds;
                directive.stableGpuFrameMilliseconds =
                    m_CaptureGpuMilliseconds;
                return directive;
            }

            const RetainedRuntimeCase* completed = &runtimeCase;
            const std::size_t completedIndex = m_CaseIndex;
            ++m_PassedCases;
            ++m_CaseIndex;
            m_Phase = Phase::Apply;
            RetainedRuntimeDirective directive{
                RetainedRuntimeDirectiveKind::ReportCasePass,
                completed,
                completedIndex,
                {}
            };
            directive.hasStableFrameTiming = true;
            directive.stableCpuFrameMilliseconds =
                m_CaptureCpuMilliseconds;
            directive.stableGpuFrameMilliseconds =
                m_CaptureGpuMilliseconds;
            return directive;
        }
        return {};
    }
}
