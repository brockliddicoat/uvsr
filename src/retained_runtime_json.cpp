#include "retained_runtime_json.h"
#include "retained_runtime_diagnostic.h"

#include <cmath>
#include <cstdio>

namespace uvsr
{
    namespace
    {
        using json::OutputWriter;

        [[nodiscard]] json::TextView View(std::string_view value) noexcept
        {
            return {value.data(), value.size()};
        }

        [[nodiscard]] const char* ActionName(RetainedRuntimeAction action) noexcept
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
            case RetainedRuntimeAction::ToggleFlashlight: return "flashlight";
            case RetainedRuntimeAction::CyclePrerequisite: return "prerequisite-cycle";
            case RetainedRuntimeAction::CycleLightingSolution: return "lighting-solution";
            }
            return "invalid";
        }

        [[nodiscard]] std::int64_t FrameMicroseconds(double milliseconds) noexcept
        {
            if (!std::isfinite(milliseconds) || milliseconds <= 0.0)
                return 0;
            return static_cast<std::int64_t>(std::llround(milliseconds * 1000.0));
        }

        [[nodiscard]] std::int64_t LinearMicrounits(double value) noexcept
        {
            if (!std::isfinite(value))
                return 0;
            return static_cast<std::int64_t>(std::llround(value * 1000000.0));
        }

        [[nodiscard]] bool Hex64(OutputWriter& writer, std::uint64_t value) noexcept
        {
            constexpr char Digits[] = "0123456789abcdef";
            char text[16];
            for (std::size_t index = 0; index < sizeof(text); ++index)
            {
                const unsigned shift = static_cast<unsigned>((sizeof(text) - index - 1) * 4);
                text[index] = Digits[(value >> shift) & 0xfu];
            }
            return writer.String({text, sizeof(text)});
        }

        [[nodiscard]] bool Histogram(OutputWriter& writer,
            const std::array<std::uint64_t, 16>& histogram) noexcept
        {
            if (!writer.Raw("[")) return false;
            for (std::size_t index = 0; index < histogram.size(); ++index)
            {
                if ((index && !writer.Raw(",")) || !writer.Unsigned(histogram[index]))
                    return false;
            }
            return writer.Raw("]");
        }

        [[nodiscard]] bool ProvenanceMembers(OutputWriter& writer,
            const RetainedRuntimeProvenance& value) noexcept
        {
            return writer.Raw("\"settingsHash\":") && writer.String(View(value.settingsHash)) &&
                writer.Raw(",\"engineVersion\":") && writer.String(View(value.engineVersion)) &&
                writer.Raw(",\"sourceCommit\":") && writer.String(View(value.sourceCommit)) &&
                writer.Raw(",\"sourceIdentity\":") && writer.String(View(value.sourceIdentity)) &&
                writer.Raw(",\"sourceClean\":") && writer.Boolean(value.sourceClean) &&
                writer.Raw(",\"production\":") && writer.Boolean(value.production) &&
                writer.Raw(",\"configuration\":") && writer.String(View(value.configuration)) &&
                writer.Raw(",\"packagePath\":") && writer.String(View(value.packagePath.View())) &&
                writer.Raw(",\"executablePath\":") && writer.String({value.executablePath.Data(), value.executablePath.Size()}) &&
                writer.Raw(",\"executableSha256\":") && writer.String(View(value.executableSha256.View())) &&
                writer.Raw(",\"debugLayerRequested\":") && writer.Boolean(value.debugLayerRequested) &&
                writer.Raw(",\"nvrhiValidationRequested\":") && writer.Boolean(value.nvrhiValidationRequested);
        }

        [[nodiscard]] bool Storage(OutputWriter& writer, const RetainedRuntimeStorage& value) noexcept
        {
            const struct { const char* name; std::uint64_t count; } fields[] = {
                {"collisionTriangles", value.collisionTriangles},
                {"collisionTriangleCapacity", value.collisionTriangleCapacity},
                {"collisionNodes", value.collisionNodes},
                {"collisionNodeCapacity", value.collisionNodeCapacity},
                {"sceneMeshes", value.sceneMeshes}, {"sceneMaterials", value.sceneMaterials},
                {"sceneInstances", value.sceneInstances},
                {"sceneGeometries", value.sceneGeometries}, {"sceneGeometryInstances", value.sceneGeometryInstances},
                {"materialBackupCount", value.materialBackupCount},
                {"materialBackupCapacity", value.materialBackupCapacity},
                {"editableLightCapacity", value.editableLightCapacity},
                {"unmountedLightCapacity", value.unmountedLightCapacity},
                {"descriptorCapacity", value.descriptorCapacity},
                {"descriptorLive", value.descriptorLive}, {"descriptorPeakLive", value.descriptorPeakLive},
                {"descriptorPeakCapacity", value.descriptorPeakCapacity},
                {"textureQueuePeak", value.textureQueuePeak}, {"texturesRequested", value.texturesRequested},
                {"texturesLoaded", value.texturesLoaded}, {"texturesFinalized", value.texturesFinalized},
                {"pbrBindingPeak", value.pbrBindingPeak}, {"pathLightCapacity", value.pathLightCapacity},
                {"targetHeapBytes", value.targetHeapBytes},
                {"materialBufferBytes", value.materialBufferBytes},
                {"geometryBufferBytes", value.geometryBufferBytes},
                {"instanceBufferBytes", value.instanceBufferBytes},
                {"retainedSourceArrays", value.retainedSourceArrays}
            };
            if (!writer.Raw("{")) return false;
            bool first = true;
            for (const auto& field : fields)
            {
                if ((!first && !writer.Raw(",")) || !writer.String(field.name) ||
                    !writer.Raw(":") || !writer.Unsigned(field.count)) return false;
                first = false;
            }
            return writer.Raw("}");
        }

        [[nodiscard]] bool LightingMembers(OutputWriter& writer,
            const RetainedRuntimeTelemetry& value) noexcept
        {
            return writer.Raw(",\"flashlightLightingSubmitted\":") && writer.Boolean(value.flashlightLightingSubmitted) &&
                writer.Raw(",\"flashlightShadow\":") && writer.Boolean(value.flashlightVisibilityDispatched) &&
                writer.Raw(",\"accumulation\":") && writer.Boolean(value.lightingAccumulationCommitted) &&
                writer.Raw(",\"autoExposure\":") && writer.Boolean(value.autoExposureDispatched) &&
                writer.Raw(",\"globalNoise\":{\"pattern\":") && writer.String(View(value.globalNoisePattern)) &&
                writer.Raw(",\"resolution\":") && writer.String(View(value.globalNoiseResolution)) &&
                writer.Raw(",\"animateSamples\":") && writer.Boolean(value.globalNoiseAnimateSamples) &&
                writer.Raw(",\"accumulateSamples\":") && writer.Boolean(value.globalNoiseAccumulateSamples) &&
                writer.Raw("},\"cpuFrameUs\":") && writer.Integer(FrameMicroseconds(value.cpuFrameMilliseconds)) &&
                writer.Raw(",\"gpuFrameUs\":") && writer.Integer(FrameMicroseconds(value.gpuFrameMilliseconds));
        }

        [[nodiscard]] bool Output(OutputWriter& writer,
            const RuntimeOutputEvidence& value, bool caseRecord) noexcept
        {
            if (!writer.Raw(",\"output\":{\"width\":") || !writer.Unsigned(value.width) ||
                !writer.Raw(",\"height\":") || !writer.Unsigned(value.height)) return false;
            if (caseRecord && !(writer.Raw(",\"encodedBytes\":") && writer.Unsigned(value.encodedBytes) &&
                writer.Raw(",\"pixelBytes\":") && writer.Unsigned(value.pixelBytes) &&
                writer.Raw(",\"minimumByte\":") && writer.Unsigned(value.minimumByte) &&
                writer.Raw(",\"maximumByte\":") && writer.Unsigned(value.maximumByte))) return false;
            if (!(writer.Raw(",\"artifactPath\":") &&
                writer.String({value.artifactPath.Data(), value.artifactPath.Size()}) &&
                writer.Raw(",\"deterministicCapture\":") && writer.Boolean(value.deterministicCapture) &&
                writer.Raw(",\"capturedPathDispatchCount\":") && writer.Unsigned(value.capturedPathDispatchCount) &&
                writer.Raw(",\"capturedSkySamplePhaseValid\":") && writer.Boolean(value.capturedSkySamplePhaseValid) &&
                writer.Raw(",\"capturedSkySamplePhase\":") && writer.Unsigned(value.capturedSkySamplePhase) &&
                writer.Raw(",\"capturedRasterProducerMask\":") && writer.Unsigned(value.capturedRasterProducerMask) &&
                writer.Raw(",\"capturedDirectionalSamplePhase\":") && writer.Unsigned(value.capturedDirectionalSamplePhase) &&
                writer.Raw(",\"capturedFlashlightSamplePhase\":") && writer.Unsigned(value.capturedFlashlightSamplePhase) &&
                writer.Raw(",\"linearFinite\":") && writer.Boolean(value.linearReadbackValid) &&
                writer.Raw(",\"nonFiniteComponents\":") && writer.Unsigned(value.nonFiniteComponentCount) &&
                writer.Raw(",\"varyingPixels\":") && writer.Unsigned(value.varyingPixelCount) &&
                writer.Raw(",\"edgePixels\":") && writer.Unsigned(value.edgePixelCount) &&
                writer.Raw(",\"meanLuminanceMicro\":") && writer.Integer(LinearMicrounits(value.meanLinearLuminance)) &&
                writer.Raw(",\"rmsLuminanceMicro\":") && writer.Integer(LinearMicrounits(value.rmsLinearLuminance)) &&
                writer.Raw(",\"meanHorizontalGradientMicro\":") && writer.Integer(LinearMicrounits(value.meanLinearHorizontalGradient)) &&
                writer.Raw(",\"luminanceHistogram\":") && Histogram(writer, value.linearLuminanceHistogram) &&
                writer.Raw(",\"linearFNV1a64\":") && Hex64(writer, value.linearHash))) return false;
            if (caseRecord && !(writer.Raw(",\"fnv1a64\":") && Hex64(writer, value.pixelHash))) return false;
            return writer.Raw("}");
        }

        struct StartContext
        {
            const RetainedRuntimeProvenance& provenance;
            std::size_t caseCount;
        };

        [[nodiscard]] bool EmitStart(OutputWriter& writer, const void* opaque) noexcept
        {
            const auto& value = *static_cast<const StartContext*>(opaque);
            return writer.Raw("{\"event\":\"start\",\"schema\":4,") &&
                ProvenanceMembers(writer, value.provenance) &&
                writer.Raw(",\"cases\":") && writer.Unsigned(value.caseCount) &&
                writer.Raw(",\"timingPolicy\":\"baseline<=1000ms; phases<=max(4x-baseline,baseline+50ms)\"}");
        }

        struct FailureContext
        {
            std::string_view caseName;
            const json::TextView* parts;
            std::size_t partCount;
        };

        [[nodiscard]] bool EmitFailure(OutputWriter& writer, const void* opaque) noexcept
        {
            const auto& value = *static_cast<const FailureContext*>(opaque);
            return writer.Raw("{\"event\":\"failure\",\"case\":") && writer.String(View(value.caseName)) &&
                writer.Raw(",\"message\":") && writer.StringParts(value.parts, value.partCount) && writer.Raw("}");
        }

        struct CaptureContext
        {
            std::size_t caseIndex;
            const RetainedRuntimeCase& runtimeCase;
            const RetainedRuntimeTelemetry& telemetry;
            const RuntimeOutputEvidence& output;
            std::string_view phase;
        };

        [[nodiscard]] bool EmitCase(OutputWriter& writer, const void* opaque) noexcept
        {
            const auto& value = *static_cast<const CaptureContext*>(opaque);
            const auto& telemetry = value.telemetry;
            return writer.Raw("{\"event\":\"case\",\"index\":") && writer.Unsigned(value.caseIndex) &&
                writer.Raw(",\"name\":") && writer.String(View(value.runtimeCase.name.View())) &&
                writer.Raw(",\"status\":\"pass\",\"phase\":") && writer.String(View(value.phase)) &&
                writer.Raw(",\"activeScene\":") && writer.String(View(telemetry.currentScene)) &&
                writer.Raw(",\"storage\":") && Storage(writer, telemetry.storage) &&
                writer.Raw(",\"expectedAction\":") && writer.String(value.runtimeCase.exerciseRetainedStateChanges
                    ? "camera-scene-resize-reference" : ActionName(value.runtimeCase.action)) &&
                writer.Raw(",\"appliedAction\":") && writer.String(ActionName(telemetry.lastAppliedAction)) &&
                writer.Raw(",\"pathHistory\":") && writer.Unsigned(telemetry.pathHistoryCount) &&
                writer.Raw(",\"pathHistoryGeneration\":") && writer.Unsigned(telemetry.pathHistoryGeneration) &&
                writer.Raw(",\"directional\":") && writer.Boolean(telemetry.directionalVisibilityDispatched) &&
                writer.Raw(",\"sky\":") && writer.Boolean(telemetry.skyVisibilityDispatched) &&
                LightingMembers(writer, telemetry) &&
                writer.Raw(",\"gpuTimingAvailable\":") && writer.Boolean(telemetry.gpuFrameTimingAvailable) &&
                Output(writer, value.output, true) && writer.Raw("}");
        }

        [[nodiscard]] bool EmitCapture(OutputWriter& writer, const void* opaque) noexcept
        {
            const auto& value = *static_cast<const CaptureContext*>(opaque);
            return writer.Raw("{\"event\":\"capture\",\"index\":") && writer.Unsigned(value.caseIndex) &&
                writer.Raw(",\"name\":") && writer.String(View(value.runtimeCase.name.View())) &&
                writer.Raw(",\"phase\":") && writer.String(View(value.phase)) &&
                writer.Raw(",\"activeScene\":") && writer.String(View(value.telemetry.currentScene)) &&
                writer.Raw(",\"storage\":") && Storage(writer, value.telemetry.storage) &&
                LightingMembers(writer, value.telemetry) &&
                Output(writer, value.output, false) && writer.Raw("}");
        }

        struct SummaryContext
        {
            const RetainedRuntimeProvenance& provenance;
            bool passed;
            std::size_t passedCases;
            std::size_t totalCases;
            std::int64_t elapsedMilliseconds;
        };

        [[nodiscard]] bool EmitSummary(OutputWriter& writer, const void* opaque) noexcept
        {
            const auto& value = *static_cast<const SummaryContext*>(opaque);
            return writer.Raw("{\"event\":\"summary\",\"status\":") && writer.String(value.passed ? "pass" : "fail") &&
                writer.Raw(",") && ProvenanceMembers(writer, value.provenance) &&
                writer.Raw(",\"passed\":") && writer.Unsigned(value.passedCases) &&
                writer.Raw(",\"total\":") && writer.Unsigned(value.totalCases) &&
                writer.Raw(",\"elapsedMs\":") && writer.Integer(value.elapsedMilliseconds) && writer.Raw("}");
        }
    }

    json::EncodedText EncodeRetainedRuntimeStartJson(
        const RetainedRuntimeProvenance& provenance, std::size_t caseCount) noexcept
    {
        const StartContext context{provenance, caseCount};
        return json::EncodedText(EmitStart, &context);
    }

    json::EncodedText EncodeRetainedRuntimeFailureJson(
        std::string_view caseName, std::string_view message) noexcept
    {
        const json::TextView parts[]{View(message)};
        const FailureContext context{caseName, parts, 1};
        return json::EncodedText(EmitFailure, &context);
    }

    RetainedRuntimeMessage RetainedRuntimeMessage::PathCount(
        std::uint64_t observed, std::uint64_t expected) noexcept
    {
        RetainedRuntimeMessage result;
        result.format = Format::PathCount;
        result.integers[0] = observed;
        result.integers[1] = expected;
        return result;
    }

    RetainedRuntimeMessage RetainedRuntimeMessage::SnapshotMismatch(
        std::string_view expected, std::string_view actual) noexcept
    {
        std::size_t mismatch = 0;
        while (mismatch < expected.size() && mismatch < actual.size() && expected[mismatch] == actual[mismatch])
            ++mismatch;
        if (mismatch == expected.size() && mismatch == actual.size())
            return "saved settings did not restore the exact live snapshot";
        const std::size_t precedingLineEnd = mismatch == 0
            ? std::string_view::npos : expected.rfind('\n', mismatch - 1);
        const std::size_t lineStart = precedingLineEnd == std::string_view::npos ? 0 : precedingLineEnd + 1;
        const auto lineAt = [lineStart](std::string_view value) -> std::string_view
        {
            if (lineStart >= value.size()) return "<end>";
            const std::size_t lineEnd = value.find('\n', lineStart);
            return value.substr(lineStart, lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart);
        };
        RetainedRuntimeMessage result;
        result.format = Format::SnapshotMismatch;
        result.parts[0] = lineAt(expected);
        result.parts[1] = lineAt(actual);
        result.integers[0] = 1;
        for (std::size_t index = 0; index < lineStart; ++index)
            if (expected[index] == '\n') ++result.integers[0];
        return result;
    }

    RetainedRuntimeMessage RetainedRuntimeMessage::BaselineTiming(double cpu, double gpu) noexcept
    {
        RetainedRuntimeMessage result;
        result.format = Format::BaselineTiming;
        result.decimals[0] = cpu; result.decimals[1] = gpu;
        return result;
    }

    RetainedRuntimeMessage RetainedRuntimeMessage::ActionTiming(
        double cpu, double cpuLimit, double gpu, double gpuLimit) noexcept
    {
        RetainedRuntimeMessage result;
        result.format = Format::ActionTiming;
        result.decimals[0] = cpu; result.decimals[1] = cpuLimit;
        result.decimals[2] = gpu; result.decimals[3] = gpuLimit;
        return result;
    }

    RetainedRuntimeMessage RetainedRuntimeMessage::InvalidOutput(const RuntimeOutputEvidence& output) noexcept
    {
        RetainedRuntimeMessage result;
        result.format = Format::InvalidOutput;
        result.integers[0] = output.valid;
        result.integers[1] = output.pixelBytes;
        result.integers[2] = output.minimumByte;
        result.integers[3] = output.maximumByte;
        result.integers[4] = output.linearReadbackValid;
        result.integers[5] = output.finiteComponentCount;
        result.integers[6] = output.nonFiniteComponentCount;
        result.integers[7] = output.varyingPixelCount;
        result.integers[8] = output.edgePixelCount;
        result.decimals[0] = output.minimumLinearValue;
        result.decimals[1] = output.maximumLinearValue;
        return result;
    }

    json::EncodedText EncodeRetainedRuntimeMessageJson(
        std::string_view caseName, const RetainedRuntimeMessage& message) noexcept
    {
        using Format = RetainedRuntimeMessage::Format;
        std::size_t integerCount = 0, decimalCount = 0;
        bool valid = true;
        switch (message.format)
        {
        case Format::Parts: valid = message.partCount <= 4; break;
        case Format::PathCount: integerCount = 2; break;
        case Format::SnapshotMismatch: integerCount = 1; break;
        case Format::BaselineTiming: decimalCount = 2; break;
        case Format::ActionTiming: decimalCount = 4; break;
        case Format::InvalidOutput: integerCount = 9; decimalCount = 2; break;
        default: valid = false; break;
        }
        // preserve std::to_string's six-decimal active C locale spelling. format once
        // before either JSON pass. 512 holds binary64's full fixed magnitude;
        // a formatter failure or unexpected expansion rejects the whole record.
        char integerBytes[9][21];
        char decimalBytes[4][512];
        std::string_view integers[9], decimals[4];
        for (std::size_t index = 0; valid && index < integerCount; ++index)
        {
            const int size = std::snprintf(integerBytes[index], sizeof(integerBytes[index]), "%llu",
                static_cast<unsigned long long>(message.integers[index]));
            valid = size >= 0 && static_cast<std::size_t>(size) < sizeof(integerBytes[index]);
            if (valid) integers[index] = {integerBytes[index], static_cast<std::size_t>(size)};
        }
        for (std::size_t index = 0; valid && index < decimalCount; ++index)
        {
            const int size = std::snprintf(decimalBytes[index], sizeof(decimalBytes[index]), "%f", message.decimals[index]);
            valid = size >= 0 && static_cast<std::size_t>(size) < sizeof(decimalBytes[index]);
            if (valid) decimals[index] = {decimalBytes[index], static_cast<std::size_t>(size)};
        }
        // the longest existing shape uses 22 parts, plus the optional timeout.
        json::TextView parts[24];
        std::size_t count = 0;
        const auto append = [&](std::string_view value)
        {
            if (count >= sizeof(parts) / sizeof(parts[0])) valid = false;
            else parts[count++] = View(value);
        };
        if (message.caseTimeout) append("case timeout: ");
        if (valid) switch (message.format)
        {
        case Format::Parts:
            for (std::size_t index = 0; index < message.partCount; ++index) append(message.parts[index]);
            break;
        case Format::PathCount:
            append("path history count is "); append(integers[0]);
            append(", expected at least "); append(integers[1]);
            break;
        case Format::SnapshotMismatch:
            append("saved settings did not restore the exact live snapshot at line "); append(integers[0]);
            append(": expected '"); append(message.parts[0]); append("', got '"); append(message.parts[1]); append("'");
            break;
        case Format::BaselineTiming:
            append("stable baseline frame time exceeded 1000 ms after one recovery sample: CPU="); append(decimals[0]);
            append(" ms, GPU="); append(decimals[1]); append(" ms");
            break;
        case Format::ActionTiming:
            append("stable post-action frame time exceeded the 4x/50 ms baseline tolerance after one recovery sample: CPU=");
            append(decimals[0]); append(" ms (limit "); append(decimals[1]); append(" ms), GPU=");
            append(decimals[2]); append(" ms (limit "); append(decimals[3]); append(" ms)");
            break;
        case Format::InvalidOutput:
            append("rendered output was empty, uniform, or non-finite: encoded-valid="); append(integers[0]);
            append(", pixel-bytes="); append(integers[1]); append(", byte-range="); append(integers[2]); append(".."); append(integers[3]);
            append(", linear-valid="); append(integers[4]); append(", finite="); append(integers[5]); append(", non-finite="); append(integers[6]);
            append(", varying="); append(integers[7]); append(", edges="); append(integers[8]);
            append(", linear-range="); append(decimals[0]); append(".."); append(decimals[1]);
            break;
        default: valid = false; break;
        }
        if (!valid)
            return json::EncodedText([](OutputWriter& writer, const void*) noexcept
            { return writer.Reject(json::ErrorCode::InvalidInput, "invalid retained runtime message or numeric text"); });
        const FailureContext context{caseName, parts, count};
        return json::EncodedText(EmitFailure, &context);
    }

    json::EncodedText EncodeRetainedRuntimeSemanticFailureJson(
        std::string_view caseName, const RetainedRuntimeSemanticResult& result) noexcept
    {
        json::TextView parts[3];
        std::size_t count = 1;
        switch (result.failure)
        {
        case RetainedRuntimeSemanticFailure::CaseIdentity:
            parts[0] = "runtime scene case identity was empty or duplicated";
            break;
        case RetainedRuntimeSemanticFailure::CaptureIdentity:
            parts[0] = "runtime scene capture identity or dimensions drifted";
            break;
        case RetainedRuntimeSemanticFailure::Coverage:
            parts[0] = "runtime scene capture coverage was incomplete";
            break;
        case RetainedRuntimeSemanticFailure::NotDistinct:
            if (!result.runtimeCase) break;
            parts[0] = "runtime scene case '";
            parts[1] = View(result.runtimeCase->name.View());
            parts[2] = "' lacked distinct scene output";
            count = 3;
            break;
        default:
            break;
        }
        if (!parts[0].data)
            return json::EncodedText([](OutputWriter& writer, const void*) noexcept
            { return writer.Reject(json::ErrorCode::InvalidInput, "invalid runtime semantic failure"); });
        const FailureContext context{caseName, parts, count};
        return json::EncodedText(EmitFailure, &context);
    }

    json::EncodedText EncodeRetainedRuntimeCaseJson(std::size_t caseIndex,
        const RetainedRuntimeCase& runtimeCase, const RetainedRuntimeTelemetry& telemetry) noexcept
    {
        const RuntimeOutputEvidence empty;
        const CaptureContext context{caseIndex, runtimeCase, telemetry,
            telemetry.output ? *telemetry.output : empty,
            runtimeCase.exerciseRetainedStateChanges ? "resize" : RetainedRuntimeCaptureLabel(runtimeCase.action)};
        return json::EncodedText(EmitCase, &context);
    }

    json::EncodedText EncodeRetainedRuntimeCaptureJson(std::size_t caseIndex,
        const RetainedRuntimeCase& runtimeCase, std::string_view phase,
        const RetainedRuntimeTelemetry& telemetry) noexcept
    {
        const RuntimeOutputEvidence empty;
        const CaptureContext context{caseIndex, runtimeCase, telemetry,
            telemetry.output ? *telemetry.output : empty, phase};
        return json::EncodedText(EmitCapture, &context);
    }

    json::EncodedText EncodeRetainedRuntimeSummaryJson(const RetainedRuntimeProvenance& provenance,
        bool passed, std::size_t passedCases, std::size_t totalCases,
        std::int64_t elapsedMilliseconds) noexcept
    {
        const SummaryContext context{provenance, passed, passedCases, totalCases, elapsedMilliseconds};
        return json::EncodedText(EmitSummary, &context);
    }
}
