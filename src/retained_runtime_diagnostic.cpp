#include "retained_runtime_diagnostic.h"
#include "json_document.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace uvsr
{
    namespace
    {
        [[nodiscard]] UiSettingsValue DomainTokenValue(
            SettingId id,
            std::size_t tokenIndex)
        {
            const UiSettingsCommandDefinition* definition =
                FindSettingsCommandDefinition(id);
            if (!definition ||
                tokenIndex >= definition->typedDomain.tokenCount)
            {
                throw std::logic_error(
                    "retained runtime case has an invalid domain token");
            }
            return UiSettingsValue::Token(std::string(
                definition->typedDomain.tokens[tokenIndex]));
        }

        [[nodiscard]] RetainedRuntimeCase::Setting BooleanSetting(
            SettingId id, bool value)
        {
            return { id, UiSettingsValue::Boolean(value) };
        }

        [[nodiscard]] RetainedRuntimeCase::Setting FloatSetting(
            SettingId id, float value)
        {
            return { id, UiSettingsValue::Float(value) };
        }

        [[nodiscard]] RetainedRuntimeCase::Setting TokenSetting(
            SettingId id, std::size_t tokenIndex)
        {
            return { id, DomainTokenValue(id, tokenIndex) };
        }

        [[nodiscard]] RetainedRuntimeCase::Setting SelectorSetting(
            SettingId id, std::string value)
        {
            return { id, UiSettingsValue::Selector(std::move(value)) };
        }

        void Set(
            RetainedRuntimeCase& runtimeCase,
            RetainedRuntimeCase::Setting setting)
        {
            const auto existing = std::find_if(
                runtimeCase.settings.begin(),
                runtimeCase.settings.end(),
                [id = setting.id](const auto& candidate)
                {
                    return candidate.id == id;
                });
            if (existing != runtimeCase.settings.end())
                existing->value = std::move(setting.value);
            else
                runtimeCase.settings.push_back(std::move(setting));
        }

        [[nodiscard]] const UiSettingsValue* Get(
            const RetainedRuntimeCase& runtimeCase,
            SettingId id)
        {
            const auto setting = std::find_if(
                runtimeCase.settings.begin(),
                runtimeCase.settings.end(),
                [id](const auto& candidate)
                {
                    return candidate.id == id;
                });
            return setting == runtimeCase.settings.end()
                ? nullptr
                : &setting->value;
        }

        [[nodiscard]] bool IsBoolean(
            const UiSettingsValue* value, bool expected) noexcept
        {
            return value && value->kind == UiSettingsValueKind::Boolean &&
                value->boolean == expected;
        }

        [[nodiscard]] RetainedRuntimeCase Raster(std::string name)
        {
            RetainedRuntimeCase runtimeCase;
            runtimeCase.name = std::move(name);
            runtimeCase.settings = {
                TokenSetting(SettingId::LightingSolution, 0u),
                BooleanSetting(
                    SettingId::RepresentationAllowRayTraversal, true),
                BooleanSetting(SettingId::SkyVisibilityEnabled, false),
                SelectorSetting(SettingId::LightSelected, "flashlight_1"),
                BooleanSetting(
                    SettingId::LightSelectedFlashlightEnabled, false),
                BooleanSetting(
                    SettingId::LightSelectedFlashlightCastShadows, true),
                BooleanSetting(SettingId::ShadowsRayTracedEnabled, true)
            };
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
                return 3u;
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
                RetainedRuntimeAction::ChangeScene,
                RetainedRuntimeAction::ResizeViewport
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
            case RetainedRuntimeAction::CyclePrerequisite: return "prerequisite-cycle";
            case RetainedRuntimeAction::CycleLightingSolution:
                return "lighting-solution";
            }
            return "invalid";
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
        const RuntimeOutputEvidence& output) noexcept
    {
        RuntimeSemanticSignature signature;
        signature.width = output.width;
        signature.height = output.height;
        signature.meanLinearLuminance = output.meanLinearLuminance;
        signature.rmsLinearLuminance = output.rmsLinearLuminance;
        signature.meanLinearHorizontalGradient =
            output.meanLinearHorizontalGradient;
        signature.linearLuminanceHistogram =
            output.linearLuminanceHistogram;
        for (const std::uint64_t count : output.linearLuminanceHistogram)
            signature.linearLuminanceSampleCount += count;
        return signature;
    }

    bool RuntimeSemanticSignaturesAreDistinct(
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

    bool ValidateRetainedRuntimeSemanticCaptures(
        const std::vector<RetainedRuntimeCase>& cases,
        const std::vector<RetainedRuntimeSemanticCapture>& captures,
        std::string& reason)
    {
        std::map<std::string, const RetainedRuntimeCase*> expected;
        for (const auto& runtimeCase : cases)
        {
            if (runtimeCase.exerciseRetainedStateChanges &&
                (runtimeCase.name.empty() ||
                 !expected.emplace(runtimeCase.name, &runtimeCase).second))
            {
                reason = "runtime scene case identity was empty or duplicated";
                return false;
            }
        }
        std::map<std::string, std::vector<const RetainedRuntimeSemanticCapture*>> observed;
        for (const auto& capture : captures)
        {
            const auto found = expected.find(capture.caseName);
            if (found == expected.end() || capture.sceneToken.empty() ||
                (capture.sceneToken != found->second->actionBaselineSceneToken &&
                 capture.sceneToken != found->second->expectedSceneToken) ||
                capture.signature.width == 0u || capture.signature.height == 0u)
            {
                reason = "runtime scene capture identity or dimensions drifted";
                return false;
            }
            observed[capture.caseName].push_back(&capture);
        }
        if (observed.size() != expected.size() || captures.size() != expected.size() * 2u)
        {
            reason = "runtime scene capture coverage was incomplete";
            return false;
        }
        for (const auto& [name, pair] : observed)
        {
            if (pair.size() != 2u || pair[0]->sceneToken == pair[1]->sceneToken ||
                !RuntimeSemanticSignaturesAreDistinct(pair[0]->signature, pair[1]->signature))
            {
                reason = "runtime scene case '" + name + "' lacked distinct scene output";
                return false;
            }
        }
        reason.clear();
        return true;
    }

    namespace
    {
        [[nodiscard]] std::string Quoted(std::string_view value)
        {
            return "\"" + json::Escape(value) + "\"";
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
            case RetainedRuntimeAction::CyclePrerequisite: return "prerequisite-cycle";
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
        return "{\"event\":\"start\",\"schema\":4," +
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
            "\"phase\":" + Quoted(runtimeCase.exerciseRetainedStateChanges
                ? "resize"
                : CaptureLabel(runtimeCase.action)) + "," +
            "\"activeScene\":" + Quoted(telemetry.currentScene) +
            ",\"expectedAction\":" +
            Quoted(runtimeCase.exerciseRetainedStateChanges
                ? "camera-scene-resize-reference"
                : ActionName(runtimeCase.action)) +
            ",\"appliedAction\":" +
            Quoted(ActionName(telemetry.lastAppliedAction)) +
            ",\"pathHistory\":" +
            std::to_string(telemetry.pathHistoryCount) +
            ",\"directional\":" +
            JsonBool(telemetry.directionalVisibilityDispatched) +
            ",\"sky\":" + JsonBool(telemetry.skyVisibilityDispatched) +
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
            ",\"activeScene\":" + Quoted(telemetry.currentScene) +
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
        constexpr std::string_view BistroToken =
            "bistro_interior_retextured";
        constexpr std::string_view SanMiguelToken =
            "san_miguel_retextured";

        std::vector<RetainedRuntimeCase> cases;
        cases.reserve(30u);

        const auto setScene = [](
            RetainedRuntimeCase& runtimeCase,
            const std::string& scene,
            std::string_view token)
        {
            Set(runtimeCase, SelectorSetting(SettingId::SceneCurrent, scene));
            runtimeCase.expectedSceneToken = token;
        };

        struct DiscreteSpec
        {
            std::string_view name;
            RetainedRuntimeCase::Setting value;
        };
        const std::array<DiscreteSpec, 7> DiscreteCases = {{
            { "noise-pattern-spatial-white",
                TokenSetting(SettingId::NoisePattern, 0u) },
            { "noise-pattern-spatial-blue",
                TokenSetting(SettingId::NoisePattern, 1u) },
            { "noise-resolution-64x64",
                TokenSetting(SettingId::NoiseResolution, 0u) },
            { "noise-resolution-256x256",
                TokenSetting(SettingId::NoiseResolution, 2u) },
            { "noise-resolution-512x512",
                TokenSetting(SettingId::NoiseResolution, 3u) },
            { "noise-animate-off",
                BooleanSetting(SettingId::NoiseAnimateSamples, false) },
            { "noise-accumulate-on",
                BooleanSetting(SettingId::NoiseAccumulateSamples, true) },
        }};
        for (std::size_t index = 0u; index < DiscreteCases.size(); ++index)
        {
            const DiscreteSpec& spec = DiscreteCases[index];
            RetainedRuntimeCase runtimeCase = Raster(std::string(spec.name));
            Set(runtimeCase, TokenSetting(SettingId::NoisePattern, 2u));
            Set(runtimeCase, TokenSetting(SettingId::NoiseResolution, 1u));
            Set(runtimeCase, BooleanSetting(SettingId::NoiseAnimateSamples, true));
            Set(runtimeCase, BooleanSetting(SettingId::NoiseAccumulateSamples, false));
            runtimeCase.expectDirectionalVisibility = true;
            runtimeCase.assertLightingAccumulationState = true;
            runtimeCase.expectLightingAccumulation =
                spec.value.id == SettingId::NoiseAccumulateSamples;
            Set(runtimeCase, spec.value);
            if ((index % 2u) == 0u)
                setScene(runtimeCase, bistroScene, BistroToken);
            else
                setScene(runtimeCase, sanMiguelScene, SanMiguelToken);
            cases.push_back(std::move(runtimeCase));
        }

        struct AllSignalSpec
        {
            std::string_view name;
            bool startsInBistro;
        };
        constexpr std::array<AllSignalSpec, 2> AllSignalCases = {{
            { "all-signal-bistro-to-san-miguel", true },
            { "all-signal-san-miguel-to-bistro", false }
        }};
        for (const AllSignalSpec& spec : AllSignalCases)
        {
            RetainedRuntimeCase runtimeCase = Raster(std::string(spec.name));
            const std::string& initialScene = spec.startsInBistro
                ? bistroScene
                : sanMiguelScene;
            const std::string& finalScene = spec.startsInBistro
                ? sanMiguelScene
                : bistroScene;
            runtimeCase.snapshotRoundTrip = true;
            runtimeCase.exerciseRetainedStateChanges = true;
            runtimeCase.resizeWidth = 704;
            runtimeCase.resizeHeight = 400;
            runtimeCase.actionBaselineSceneToken = spec.startsInBistro
                ? BistroToken
                : SanMiguelToken;
            runtimeCase.expectedSceneToken = spec.startsInBistro
                ? SanMiguelToken
                : BistroToken;
            runtimeCase.actionSettingId = SettingId::SceneCurrent;
            runtimeCase.actionBaselineValue =
                UiSettingsValue::Selector(initialScene);
            runtimeCase.actionValue = UiSettingsValue::Selector(finalScene);
            Set(runtimeCase,
                SelectorSetting(SettingId::SceneCurrent, initialScene));
            Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilityEnabled, true));
            Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilityDiffuseIbl, true));
            Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilitySpecularIbl, true));
            Set(runtimeCase,
                TokenSetting(SettingId::SkyVisibilitySamplesPerPixel, 3u));
            Set(runtimeCase,
                SelectorSetting(SettingId::LightSelected, "flashlight_1"));
            Set(runtimeCase, BooleanSetting(
                SettingId::LightSelectedFlashlightEnabled, true));
            Set(runtimeCase, BooleanSetting(
                SettingId::LightSelectedFlashlightCastShadows, true));
            Set(runtimeCase,
                BooleanSetting(SettingId::ShadowsRayTracedEnabled, true));
            runtimeCase.expectDirectionalVisibility = true;
            runtimeCase.expectSkyVisibility = true;
            runtimeCase.expectFlashlightLightingSubmitted = true;
            runtimeCase.assertFlashlightLightingState = true;
            runtimeCase.expectFlashlightVisibility = true;
            runtimeCase.assertFlashlightVisibilityState = true;
            cases.push_back(std::move(runtimeCase));
        }

        const std::array<UiSettingsValue, 6> Environments = {
            DomainTokenValue(SettingId::SkyEnvironment, 0u),
            DomainTokenValue(SettingId::SkyEnvironment, 1u),
            DomainTokenValue(SettingId::SkyEnvironment, 2u),
            DomainTokenValue(SettingId::SkyEnvironment, 3u),
            DomainTokenValue(SettingId::SkyEnvironment, 4u),
            DomainTokenValue(SettingId::SkyEnvironment, 5u)
        };
        constexpr std::array<float, 3> Compensation = { -18.f, 0.f, 8.f };
        constexpr std::array<float, 3> Brightening = { 0.f, 8.f, 16.f };
        constexpr std::array<float, 3> Darkening = { 16.f, 8.f, 0.f };
        constexpr std::array<float, 3> AdjustmentPeriod = {
            0.05f, 0.2f, 5.f
        };
        for (std::size_t index = 0u; index < Environments.size(); ++index)
        {
            RetainedRuntimeCase runtimeCase = Raster(
                "hdr-environment-" + Environments[index].text);
            setScene(runtimeCase,
                (index % 2u) == 0u ? bistroScene : sanMiguelScene,
                (index % 2u) == 0u ? BistroToken : SanMiguelToken);
            Set(runtimeCase,
                { SettingId::SkyEnvironment, Environments[index] });
            runtimeCase.action = RetainedRuntimeAction::ChangeSetting;
            runtimeCase.actionSettingId = SettingId::SkyEnvironment;
            runtimeCase.actionBaselineValue = Environments[
                (index + Environments.size() - 1u) % Environments.size()];
            runtimeCase.actionValue = Environments[index];
            runtimeCase.requireActionOutputDifference = true;
            Set(runtimeCase,
                BooleanSetting(SettingId::SkyDiffuseIbl, true));
            Set(runtimeCase,
                BooleanSetting(SettingId::SkySpecularIbl, true));
            Set(runtimeCase,
                BooleanSetting(SettingId::SkyEnvironmentBackground, true));
            Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilityEnabled, true));
            Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilityDiffuseIbl, true));
            Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilitySpecularIbl, true));
            Set(runtimeCase,
                TokenSetting(SettingId::SkyVisibilitySamplesPerPixel, 2u));
            const bool automaticExposure = (index % 2u) != 0u;
            Set(runtimeCase, BooleanSetting(
                SettingId::SkyAutoExposureEnabled, automaticExposure));
            if (automaticExposure)
            {
                const std::size_t profile = index / 2u;
                Set(runtimeCase, FloatSetting(
                    SettingId::SkyAutoExposureExposureCompensation,
                    Compensation[profile]));
                Set(runtimeCase, FloatSetting(
                    SettingId::SkyAutoExposureMaximumBrightening,
                    Brightening[profile]));
                Set(runtimeCase, FloatSetting(
                    SettingId::SkyAutoExposureMaximumDarkening,
                    Darkening[profile]));
                Set(runtimeCase, FloatSetting(
                    SettingId::SkyAutoExposureAdjustmentPeriod,
                    AdjustmentPeriod[profile]));
            }
            runtimeCase.expectSkyVisibility = true;
            runtimeCase.expectAutoExposure = automaticExposure;
            runtimeCase.assertAutoExposureState = true;
            cases.push_back(std::move(runtimeCase));
        }

        RetainedRuntimeCase flashlightLighting = Raster(
            "flashlight-lighting-toggle-bistro-1x");
        setScene(flashlightLighting, bistroScene, BistroToken);
        Set(flashlightLighting, BooleanSetting(
            SettingId::LightSelectedFlashlightEnabled, true));
        Set(flashlightLighting, BooleanSetting(
            SettingId::LightSelectedFlashlightCastShadows, false));
        flashlightLighting.expectFlashlightLightingSubmitted = true;
        flashlightLighting.assertFlashlightLightingState = true;
        flashlightLighting.assertFlashlightVisibilityState = true;
        flashlightLighting.action = RetainedRuntimeAction::ChangeSetting;
        flashlightLighting.actionSettingId =
            SettingId::LightSelectedFlashlightEnabled;
        flashlightLighting.actionBaselineValue =
            UiSettingsValue::Boolean(true);
        flashlightLighting.actionValue = UiSettingsValue::Boolean(false);
        flashlightLighting.requireActionOutputDifference = true;
        cases.push_back(std::move(flashlightLighting));

        RetainedRuntimeCase flashlightShadow = Raster(
            "flashlight-shadow-toggle-san-miguel");
        setScene(flashlightShadow, sanMiguelScene, SanMiguelToken);
        Set(flashlightShadow, BooleanSetting(
            SettingId::LightSelectedFlashlightEnabled, true));
        Set(flashlightShadow, BooleanSetting(
            SettingId::LightSelectedFlashlightCastShadows, true));
        flashlightShadow.expectFlashlightLightingSubmitted = true;
        flashlightShadow.assertFlashlightLightingState = true;
        flashlightShadow.expectFlashlightVisibility = true;
        flashlightShadow.assertFlashlightVisibilityState = true;
        flashlightShadow.action = RetainedRuntimeAction::ChangeSetting;
        flashlightShadow.actionSettingId =
            SettingId::LightSelectedFlashlightCastShadows;
        flashlightShadow.actionBaselineValue =
            UiSettingsValue::Boolean(true);
        flashlightShadow.actionValue = UiSettingsValue::Boolean(false);
        flashlightShadow.requireActionOutputDifference = true;
        cases.push_back(std::move(flashlightShadow));

        const auto pathCase = [&bistroScene](std::string name)
        {
            RetainedRuntimeCase runtimeCase = Raster(std::move(name));
            runtimeCase.expectedPathHistoryCount = 3u;
            Set(runtimeCase,
                SelectorSetting(SettingId::SceneCurrent, bistroScene));
            Set(runtimeCase,
                BooleanSetting(SettingId::SkyVisibilityEnabled, false));
            Set(runtimeCase,
                TokenSetting(SettingId::LightingSolution, 1u));
            Set(runtimeCase,
                TokenSetting(SettingId::NoisePattern, 2u));
            runtimeCase.expectedSceneToken =
                "bistro_interior_retextured";
            return runtimeCase;
        };

        RetainedRuntimeCase pathBaseline =
            pathCase("path-tracing-bistro");
        pathBaseline.snapshotRoundTrip = true;
        cases.push_back(std::move(pathBaseline));

        RetainedRuntimeCase pathCamera =
            pathCase("path-history-camera-reset");
        pathCamera.action = RetainedRuntimeAction::NudgeCamera;
        pathCamera.requirePathHistoryRestart = true;
        cases.push_back(std::move(pathCamera));

        RetainedRuntimeCase pathResize =
            pathCase("path-history-resize-reset");
        pathResize.action = RetainedRuntimeAction::ResizeViewport;
        pathResize.resizeWidth = 800;
        pathResize.resizeHeight = 448;
        pathResize.requirePathHistoryRestart = true;
        cases.push_back(std::move(pathResize));

        RetainedRuntimeCase pathScene =
            pathCase("path-tracing-san-miguel-scene-reset");
        pathScene.action = RetainedRuntimeAction::ChangeScene;
        pathScene.actionSettingId = SettingId::SceneCurrent;
        pathScene.actionBaselineValue =
            UiSettingsValue::Selector(bistroScene);
        pathScene.actionBaselineSceneToken = BistroToken;
        pathScene.actionValue = UiSettingsValue::Selector(sanMiguelScene);
        pathScene.expectedSceneToken = SanMiguelToken;
        pathScene.requirePathHistoryRestart = true;
        cases.push_back(std::move(pathScene));

        const auto appendPathRestart = [
            &cases, &pathCase](
                std::string name,
                RetainedRuntimeAction action,
                SettingId settingId = SettingId::Invalid,
                UiSettingsValue baselineValue = {},
                UiSettingsValue actionValue = {},
                bool requireOutputDifference = false)
        {
            RetainedRuntimeCase runtimeCase = pathCase(std::move(name));
            runtimeCase.action = action;
            runtimeCase.actionSettingId = settingId;
            runtimeCase.actionBaselineValue = std::move(baselineValue);
            runtimeCase.actionValue = std::move(actionValue);
            if (runtimeCase.action == RetainedRuntimeAction::ChangeSetting)
            {
                Set(runtimeCase, { runtimeCase.actionSettingId,
                    runtimeCase.actionBaselineValue });
            }
            runtimeCase.requirePathHistoryRestart = true;
            runtimeCase.requireActionOutputDifference =
                requireOutputDifference;
            cases.push_back(std::move(runtimeCase));
        };
        appendPathRestart(
            "path-history-environment-reset",
            RetainedRuntimeAction::ChangeSetting,
            SettingId::SkyEnvironment,
            DomainTokenValue(SettingId::SkyEnvironment, 0u),
            DomainTokenValue(SettingId::SkyEnvironment, 3u), true);
        appendPathRestart(
            "path-history-exposure-reset",
            RetainedRuntimeAction::ChangeSetting,
            SettingId::SkyExposure,
            UiSettingsValue::Float(-2.75f),
            UiSettingsValue::Float(-1.75f), true);
        appendPathRestart(
            "path-history-global-noise-reset",
            RetainedRuntimeAction::ChangeSetting,
            SettingId::NoisePattern,
            DomainTokenValue(SettingId::NoisePattern, 2u),
            DomainTokenValue(SettingId::NoisePattern, 1u));
        appendPathRestart(
            "path-history-material-reset",
            RetainedRuntimeAction::ChangeMaterial);
        appendPathRestart(
            "path-history-light-reset",
            RetainedRuntimeAction::ChangeLight);
        appendPathRestart(
            "path-history-flashlight-reset",
            RetainedRuntimeAction::ToggleFlashlight);
        appendPathRestart(
            "path-history-lighting-solution-cycle",
            RetainedRuntimeAction::CycleLightingSolution);

        appendPathRestart("path-history-maximum-bounces-reset",
            RetainedRuntimeAction::ChangeSetting, SettingId::PathingMaximumBounces,
            UiSettingsValue::Integer(1), UiSettingsValue::Integer(8), true);
        appendPathRestart("path-history-minimum-bounces-reset",
            RetainedRuntimeAction::ChangeSetting, SettingId::PathingMinimumBounces,
            UiSettingsValue::Integer(1), UiSettingsValue::Integer(4));
        appendPathRestart("path-history-firefly-filter-reset",
            RetainedRuntimeAction::ChangeSetting, SettingId::PathingFireflyFilter,
            UiSettingsValue::Boolean(false), UiSettingsValue::Boolean(true), true);
        Set(cases.back(), FloatSetting(SettingId::PathingFireflyThreshold, 10.f));
        Set(cases.back(), FloatSetting(SettingId::SkyExposure, 3.f));
        appendPathRestart("path-history-firefly-threshold-reset",
            RetainedRuntimeAction::ChangeSetting, SettingId::PathingFireflyThreshold,
            UiSettingsValue::Float(10.f), UiSettingsValue::Float(1000000.f), true);
        Set(cases.back(), FloatSetting(SettingId::SkyExposure, 3.f));

        const auto appendCycle = [&](RetainedRuntimeCase runtimeCase, SettingId id,
            UiSettingsValue baselineValue = UiSettingsValue::Boolean(true),
            UiSettingsValue disabledValue = UiSettingsValue::Boolean(false))
        {
            runtimeCase.action = RetainedRuntimeAction::CyclePrerequisite;
            runtimeCase.actionSettingId = id;
            runtimeCase.actionBaselineValue = std::move(baselineValue);
            runtimeCase.actionValue = std::move(disabledValue);
            setScene(runtimeCase, bistroScene, BistroToken);
            cases.push_back(std::move(runtimeCase));
        };
        auto pathTraversal = pathCase("path-traversal-cycle");
        pathTraversal.requirePathHistoryRestart = true;
        appendCycle(std::move(pathTraversal), SettingId::RepresentationAllowRayTraversal);
        auto rasterTraversal = Raster("ray-marching-traversal-cycle");
        rasterTraversal.expectDirectionalVisibility = true;
        appendCycle(std::move(rasterTraversal), SettingId::RepresentationAllowRayTraversal);

        std::rotate(cases.begin(), cases.end() - 2, cases.end());
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
            (runtimeCase.exerciseRetainedStateChanges &&
                m_CurrentAction == RetainedRuntimeAction::NudgeCamera);
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
            runtimeCase.actionSettingId ==
                SettingId::LightSelectedFlashlightEnabled &&
            IsBoolean(&runtimeCase.actionValue, false);
        const bool flashlightShadowsDisabledReference = !beforeAction &&
            m_CurrentAction == RetainedRuntimeAction::ChangeSetting &&
            (flashlightDisabledReference ||
                (runtimeCase.actionSettingId ==
                        SettingId::LightSelectedFlashlightCastShadows &&
                    IsBoolean(&runtimeCase.actionValue, false)));
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
        if (runtimeCase.expectLightingAccumulation &&
            !telemetry.lightingAccumulationCommitted)
        {
            reason = "fixed cumulative lighting history did not commit";
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
        const auto phaseValue = [&](SettingId id) -> const UiSettingsValue*
        {
            if (!beforeAction &&
                m_CurrentAction == RetainedRuntimeAction::ChangeSetting &&
                runtimeCase.actionSettingId == id)
                return &runtimeCase.actionValue;
            return Get(runtimeCase, id);
        };
        const UiSettingsValue* expectedNoisePattern =
            phaseValue(SettingId::NoisePattern);
        const UiSettingsValue* expectedNoiseResolution =
            phaseValue(SettingId::NoiseResolution);
        const UiSettingsValue* expectedNoiseAnimate =
            phaseValue(SettingId::NoiseAnimateSamples);
        const UiSettingsValue* expectedNoiseAccumulate =
            phaseValue(SettingId::NoiseAccumulateSamples);
        if ((expectedNoisePattern &&
                telemetry.globalNoisePattern != expectedNoisePattern->text) ||
            (expectedNoiseResolution &&
                telemetry.globalNoiseResolution !=
                    expectedNoiseResolution->text) ||
            (expectedNoiseAnimate &&
                telemetry.globalNoiseAnimateSamples !=
                    expectedNoiseAnimate->boolean) ||
            (expectedNoiseAccumulate &&
                telemetry.globalNoiseAccumulateSamples !=
                    expectedNoiseAccumulate->boolean))
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
            if ((*telemetry.settingsSnapshot != m_SavedSnapshot) != runtimeCase.expectSnapshotResetChange)
            {
                return Finish(false, runtimeCase.expectSnapshotResetChange
                    ? "RESET left the non-default snapshot unchanged"
                    : "RESET changed the factory-default snapshot");
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
                const bool mustDifferFromPrior =
                    runtimeCase.requireActionOutputDifference ||
                    (runtimeCase.exerciseRetainedStateChanges &&
                        (m_CurrentAction ==
                            RetainedRuntimeAction::NudgeCamera ||
                        m_CurrentAction ==
                            RetainedRuntimeAction::ChangeScene));
                if (mustDifferFromPrior &&
                    output.linearHash == m_LastActiveLinearHash)
                {
                    return Finish(false,
                        "named camera/scene action did not change rendered output");
                }
                m_LastActiveLinearHash = output.linearHash;
            }

            if (runtimeCase.exerciseRetainedStateChanges &&
                (m_CurrentAction == RetainedRuntimeAction::None ||
                    m_CurrentAction == RetainedRuntimeAction::ChangeScene))
            {
                RetainedRuntimeSemanticCapture capture;
                capture.caseName = runtimeCase.name;
                capture.sceneToken =
                    m_CurrentAction == RetainedRuntimeAction::ChangeScene
                        ? runtimeCase.expectedSceneToken
                        : runtimeCase.actionBaselineSceneToken;
                capture.signature = BuildRuntimeSemanticSignature(output);
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
                        directive.actionSettingId = SettingId::SceneCurrent;
                        directive.actionValue = runtimeCase.actionValue;
                    }
                }
                else
                {
                    directive.actionSettingId = runtimeCase.actionSettingId;
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
