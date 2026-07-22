#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace uvsr
{
    enum class SvsmMotionMeasurementMarkerState : uint32_t
    {
        Invalid,
        Ready,
        Complete,
        Contaminated
    };

    struct SvsmMotionMeasurementMarker
    {
        SvsmMotionMeasurementMarkerState state =
            SvsmMotionMeasurementMarkerState::Invalid;
        std::string_view runIdentity;
        std::string_view rendererPath;
        uint64_t monitorProcessId = 0u;
        uint64_t rendererProcessId = 0u;
        uint64_t measurementStartUnixMilliseconds = 0u;
        uint64_t measurementDeadlineUnixMilliseconds = 0u;
        uint64_t measurementEndUnixMilliseconds = 0u;
        bool identityValid = false;
        bool timingValid = false;
        bool completionTimingValid = false;
    };

    [[nodiscard]] inline std::string_view
    GetSvsmMotionMeasurementMarkerFirstLine(
        std::string_view contents)
    {
        const std::size_t lineEnd = contents.find('\n');
        std::string_view line = contents.substr(
            0u,
            lineEnd == std::string_view::npos
                ? contents.size()
                : lineEnd);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1u);
        if (line.size() >= 3u &&
            static_cast<unsigned char>(line[0]) == 0xefu &&
            static_cast<unsigned char>(line[1]) == 0xbbu &&
            static_cast<unsigned char>(line[2]) == 0xbfu)
        {
            line.remove_prefix(3u);
        }
        return line;
    }

    [[nodiscard]] inline SvsmMotionMeasurementMarkerState
    GetSvsmMotionMeasurementMarkerState(std::string_view contents)
    {
        const std::string_view firstLine =
            GetSvsmMotionMeasurementMarkerFirstLine(contents);
        if (firstLine == "state=ready")
            return SvsmMotionMeasurementMarkerState::Ready;
        if (firstLine == "state=complete")
            return SvsmMotionMeasurementMarkerState::Complete;
        if (firstLine == "state=contaminated")
            return SvsmMotionMeasurementMarkerState::Contaminated;
        return SvsmMotionMeasurementMarkerState::Invalid;
    }

    [[nodiscard]] inline bool ParseSvsmMotionMarkerUnsigned(
        std::string_view value,
        uint64_t& result)
    {
        if (value.empty())
            return false;

        uint64_t parsed = 0u;
        for (const char character : value)
        {
            if (character < '0' || character > '9')
                return false;
            const uint64_t digit = uint64_t(character - '0');
            if (parsed >
                (std::numeric_limits<uint64_t>::max() - digit) / 10u)
            {
                return false;
            }
            parsed = parsed * 10u + digit;
        }
        result = parsed;
        return true;
    }

    [[nodiscard]] inline SvsmMotionMeasurementMarker
    ParseSvsmMotionMeasurementMarker(std::string_view contents)
    {
        SvsmMotionMeasurementMarker marker;
        marker.state = GetSvsmMotionMeasurementMarkerState(contents);
        if (marker.state == SvsmMotionMeasurementMarkerState::Invalid)
            return marker;

        bool runIdentitySeen = false;
        bool monitorProcessIdSeen = false;
        bool rendererProcessIdSeen = false;
        bool rendererPathSeen = false;
        bool measurementStartSeen = false;
        bool measurementDeadlineSeen = false;
        bool measurementEndSeen = false;
        bool malformed = false;
        std::size_t position = contents.find('\n');
        position = position == std::string_view::npos
            ? contents.size()
            : position + 1u;
        while (position < contents.size())
        {
            const std::size_t lineEnd = contents.find('\n', position);
            std::string_view line = contents.substr(
                position,
                lineEnd == std::string_view::npos
                    ? contents.size() - position
                    : lineEnd - position);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1u);
            const std::size_t separator = line.find('=');
            if (separator != std::string_view::npos)
            {
                const std::string_view key = line.substr(0u, separator);
                const std::string_view value = line.substr(separator + 1u);
                if (key == "state")
                {
                    malformed = true;
                }
                else if (key == "runIdentity")
                {
                    malformed |= runIdentitySeen || value.empty();
                    runIdentitySeen = true;
                    marker.runIdentity = value;
                }
                else if (key == "monitorProcessId")
                {
                    malformed |= monitorProcessIdSeen ||
                        !ParseSvsmMotionMarkerUnsigned(
                            value, marker.monitorProcessId);
                    monitorProcessIdSeen = true;
                }
                else if (key == "rendererProcessId")
                {
                    malformed |= rendererProcessIdSeen ||
                        !ParseSvsmMotionMarkerUnsigned(
                            value, marker.rendererProcessId);
                    rendererProcessIdSeen = true;
                }
                else if (key == "rendererPath")
                {
                    malformed |= rendererPathSeen || value.empty();
                    rendererPathSeen = true;
                    marker.rendererPath = value;
                }
                else if (key == "measurementStartUnixMs")
                {
                    malformed |= measurementStartSeen ||
                        !ParseSvsmMotionMarkerUnsigned(
                            value,
                            marker.measurementStartUnixMilliseconds);
                    measurementStartSeen = true;
                }
                else if (key == "measurementDeadlineUnixMs")
                {
                    malformed |= measurementDeadlineSeen ||
                        !ParseSvsmMotionMarkerUnsigned(
                            value,
                            marker.measurementDeadlineUnixMilliseconds);
                    measurementDeadlineSeen = true;
                }
                else if (key == "measurementEndUnixMs")
                {
                    malformed |= measurementEndSeen ||
                        !ParseSvsmMotionMarkerUnsigned(
                            value,
                            marker.measurementEndUnixMilliseconds);
                    measurementEndSeen = true;
                }
            }
            if (lineEnd == std::string_view::npos)
                break;
            position = lineEnd + 1u;
        }

        marker.identityValid = !malformed &&
            runIdentitySeen && monitorProcessIdSeen &&
            rendererProcessIdSeen && rendererPathSeen &&
            marker.monitorProcessId > 0u &&
            marker.rendererProcessId > 0u;
        marker.timingValid = marker.identityValid &&
            measurementStartSeen && measurementDeadlineSeen &&
            marker.measurementStartUnixMilliseconds > 0u &&
            marker.measurementDeadlineUnixMilliseconds >
                marker.measurementStartUnixMilliseconds;
        const bool terminalState =
            marker.state == SvsmMotionMeasurementMarkerState::Complete ||
            marker.state == SvsmMotionMeasurementMarkerState::Contaminated;
        if ((marker.state == SvsmMotionMeasurementMarkerState::Ready &&
                measurementEndSeen) ||
            (terminalState && !measurementEndSeen))
        {
            marker.identityValid = false;
            marker.timingValid = false;
        }
        marker.completionTimingValid = marker.timingValid &&
            terminalState && measurementEndSeen &&
            marker.measurementEndUnixMilliseconds >=
                marker.measurementStartUnixMilliseconds &&
            (marker.state ==
                    SvsmMotionMeasurementMarkerState::Contaminated ||
                marker.measurementEndUnixMilliseconds >=
                    marker.measurementDeadlineUnixMilliseconds);
        return marker;
    }

    [[nodiscard]] inline bool IsSvsmMotionMeasurementMarkerReady(
        std::string_view contents)
    {
        const SvsmMotionMeasurementMarker marker =
            ParseSvsmMotionMeasurementMarker(contents);
        return marker.state ==
                SvsmMotionMeasurementMarkerState::Ready &&
            marker.identityValid && marker.timingValid;
    }

    [[nodiscard]] inline bool IsSvsmMotionMeasurementMarkerReadyForRenderer(
        const SvsmMotionMeasurementMarker& marker,
        uint64_t rendererProcessId,
        std::string_view rendererPath,
        uint64_t currentUnixMilliseconds)
    {
        return marker.state ==
                SvsmMotionMeasurementMarkerState::Ready &&
            marker.identityValid && marker.timingValid &&
            marker.rendererProcessId == rendererProcessId &&
            marker.rendererPath == rendererPath &&
            currentUnixMilliseconds >=
                marker.measurementStartUnixMilliseconds &&
            currentUnixMilliseconds <=
                marker.measurementDeadlineUnixMilliseconds;
    }

    [[nodiscard]] inline bool IsSameSvsmMotionMeasurementRun(
        const SvsmMotionMeasurementMarker& ready,
        const SvsmMotionMeasurementMarker& terminal)
    {
        return ready.identityValid && ready.timingValid &&
            terminal.identityValid && terminal.timingValid &&
            ready.runIdentity == terminal.runIdentity &&
            ready.monitorProcessId == terminal.monitorProcessId &&
            ready.rendererProcessId == terminal.rendererProcessId &&
            ready.rendererPath == terminal.rendererPath &&
            ready.measurementStartUnixMilliseconds ==
                terminal.measurementStartUnixMilliseconds &&
            ready.measurementDeadlineUnixMilliseconds ==
                terminal.measurementDeadlineUnixMilliseconds;
    }

    [[nodiscard]] inline bool
    IsSvsmMotionMeasurementMarkerCleanCompletion(
        const SvsmMotionMeasurementMarker& ready,
        const SvsmMotionMeasurementMarker& terminal,
        uint64_t benchmarkEndUnixMilliseconds)
    {
        return terminal.state ==
                SvsmMotionMeasurementMarkerState::Complete &&
            terminal.completionTimingValid &&
            IsSameSvsmMotionMeasurementRun(ready, terminal) &&
            terminal.measurementEndUnixMilliseconds >=
                benchmarkEndUnixMilliseconds;
    }

    struct SvsmMotionBenchmarkTimingSummary
    {
        std::size_t sampleCount = 0u;
        float median = 0.f;
        float p95 = 0.f;
        float p99 = 0.f;
        float maximum = 0.f;
    };

    [[nodiscard]] inline SvsmMotionBenchmarkTimingSummary
    SummarizeSvsmMotionBenchmarkSamples(std::vector<float> values)
    {
        SvsmMotionBenchmarkTimingSummary summary;
        summary.sampleCount = values.size();
        if (values.empty())
            return summary;

        std::sort(values.begin(), values.end());
        const std::size_t middle = values.size() / 2u;
        if ((values.size() & 1u) != 0u)
            summary.median = values[middle];
        else
            summary.median =
                (values[middle - 1u] + values[middle]) * 0.5f;

        auto nearestRank = [&values](float percentile) {
            const std::size_t rank = std::max<std::size_t>(
                1u,
                std::size_t(std::ceil(
                    percentile * float(values.size()))));
            return values[std::min(rank, values.size()) - 1u];
        };
        summary.p95 = nearestRank(0.95f);
        summary.p99 = nearestRank(0.99f);
        summary.maximum = values.back();
        return summary;
    }

    [[nodiscard]] constexpr float SumSvsmMotionBenchmarkGpuStages(
        float pageMarkingMilliseconds,
        float allocationMilliseconds,
        float clearingMilliseconds,
        float packetPageCullingMilliseconds,
        float pageRenderingMilliseconds,
        float filteringMilliseconds)
    {
        return pageMarkingMilliseconds +
            allocationMilliseconds +
            clearingMilliseconds +
            packetPageCullingMilliseconds +
            pageRenderingMilliseconds +
            filteringMilliseconds;
    }

    enum class SvsmMotionBenchmarkSegment : uint32_t
    {
        Warm,
        TurnRight,
        HoldRight,
        TurnBack,
        Complete
    };

    constexpr uint32_t SvsmMotionBenchmarkWarmFrames = 180u;
    constexpr uint32_t SvsmMotionBenchmarkTurnFrames = 450u;
    constexpr uint32_t SvsmMotionBenchmarkHoldFrames = 16u;
    constexpr float SvsmMotionBenchmarkDegreesPerFrame = 0.1f;
    constexpr float SvsmMotionBenchmarkMaximumAngleDegrees = 45.f;
    constexpr uint32_t SvsmMotionBenchmarkEndFrame =
        SvsmMotionBenchmarkWarmFrames +
        SvsmMotionBenchmarkTurnFrames +
        SvsmMotionBenchmarkHoldFrames +
        SvsmMotionBenchmarkTurnFrames;
    constexpr uint32_t SvsmMotionBenchmarkMeasurementFrames =
        SvsmMotionBenchmarkEndFrame -
        SvsmMotionBenchmarkWarmFrames;
    constexpr uint32_t SvsmMotionBenchmarkPreparationFrameLimit = 120u;
    constexpr uint32_t SvsmMotionBenchmarkDrainFrameLimit = 240u;
    constexpr uint32_t SvsmMotionAutostartBaselineFrames = 8u;
    constexpr uint32_t SvsmMotionAutostartStableSvsmFrames = 8u;
    constexpr uint32_t SvsmMotionAutostartWarmupFrameLimit = 4096u;
    constexpr float SvsmMotionBenchmarkMedianTargetMilliseconds = 0.4f;
    constexpr float SvsmMotionBenchmarkSpikeCeilingMilliseconds = 0.7f;

    enum class SvsmMotionAutostartStage : uint32_t
    {
        Baseline,
        SvsmWarmup,
        Ready
    };

    struct SvsmMotionAutostartDecision
    {
        SvsmMotionAutostartStage stage =
            SvsmMotionAutostartStage::Baseline;
        uint32_t stageFrames = 0u;
        uint32_t stableFrames = 0u;
        bool enableSvsm = false;
        bool startBenchmark = false;
        bool timedOut = false;
    };

    [[nodiscard]] constexpr SvsmMotionAutostartDecision
    AdvanceSvsmMotionAutostart(
        SvsmMotionAutostartStage stage,
        uint32_t stageFrames,
        uint32_t stableFrames,
        bool svsmActive,
        bool staticPageDrainActive)
    {
        SvsmMotionAutostartDecision decision;
        decision.stage = stage;
        decision.stageFrames = stageFrames;
        decision.stableFrames = stableFrames;

        switch (stage)
        {
        case SvsmMotionAutostartStage::Baseline:
            if (decision.stageFrames >=
                SvsmMotionAutostartBaselineFrames)
            {
                decision.stage = SvsmMotionAutostartStage::SvsmWarmup;
                decision.stageFrames = 0u;
                decision.stableFrames = 0u;
                decision.enableSvsm = true;
            }
            else
            {
                ++decision.stageFrames;
            }
            break;

        case SvsmMotionAutostartStage::SvsmWarmup:
            ++decision.stageFrames;
            decision.stableFrames =
                svsmActive && !staticPageDrainActive
                    ? stableFrames + 1u
                    : 0u;
            if (decision.stableFrames >=
                SvsmMotionAutostartStableSvsmFrames)
            {
                decision.stage = SvsmMotionAutostartStage::Ready;
                decision.stageFrames = 0u;
                decision.stableFrames = 0u;
                decision.startBenchmark = true;
            }
            else if (decision.stageFrames >=
                SvsmMotionAutostartWarmupFrameLimit)
            {
                decision.timedOut = true;
            }
            break;

        case SvsmMotionAutostartStage::Ready:
            decision.startBenchmark = true;
            break;
        }

        return decision;
    }

    [[nodiscard]] constexpr bool IsSvsmMotionDiagnosticPoolPageCount(
        uint32_t physicalPageCount)
    {
        return physicalPageCount == 64u ||
            physicalPageCount == 256u ||
            physicalPageCount == 1024u ||
            physicalPageCount == 4096u;
    }

    [[nodiscard]] constexpr bool
    IsSvsmMotionBenchmarkEnvironmentValid(
        bool dredDiagnosticsActive,
        bool diagnosticConfiguration)
    {
        return !dredDiagnosticsActive && !diagnosticConfiguration;
    }

    [[nodiscard]] constexpr bool
    IsSvsmMotionBenchmarkAcceptanceConfiguration(
        uint32_t physicalPageCount,
        bool renderPacketCachingEnabled,
        bool gpuGatedDrawSubmission)
    {
        return physicalPageCount == 4096u &&
            renderPacketCachingEnabled &&
            gpuGatedDrawSubmission;
    }

    [[nodiscard]] inline std::size_t
    CountSvsmMotionBenchmarkSamplesAbove(
        const std::vector<float>& values,
        float threshold)
    {
        return std::count_if(
            values.begin(),
            values.end(),
            [threshold](float value) { return value > threshold; });
    }

    [[nodiscard]] constexpr bool IsSvsmMotionBenchmarkGpuTargetMet(
        bool evidenceValid,
        bool requestedPathActive,
        const SvsmMotionBenchmarkTimingSummary& summary)
    {
        return evidenceValid &&
            requestedPathActive &&
            summary.median <=
                SvsmMotionBenchmarkMedianTargetMilliseconds &&
            summary.maximum <=
                SvsmMotionBenchmarkSpikeCeilingMilliseconds;
    }

    [[nodiscard]] constexpr bool IsSvsmMotionBenchmarkMeasurementFrame(
        uint64_t frame)
    {
        return frame >= SvsmMotionBenchmarkWarmFrames &&
            frame < SvsmMotionBenchmarkEndFrame;
    }

    [[nodiscard]] constexpr bool IsSvsmMotionBenchmarkEvidenceValid(
        uint64_t gpuSamples,
        uint64_t cpuSamples,
        uint64_t issued,
        uint64_t dropped,
        uint64_t retired,
        uint64_t outstanding,
        bool duplicateTag,
        bool invalidTag)
    {
        return gpuSamples == SvsmMotionBenchmarkMeasurementFrames &&
            cpuSamples == SvsmMotionBenchmarkMeasurementFrames &&
            issued == SvsmMotionBenchmarkMeasurementFrames &&
            dropped == 0u &&
            retired == SvsmMotionBenchmarkMeasurementFrames &&
            outstanding == 0u &&
            !duplicateTag &&
            !invalidTag;
    }

    [[nodiscard]] constexpr SvsmMotionBenchmarkSegment
    GetSvsmMotionBenchmarkSegment(uint64_t frame)
    {
        if (frame < SvsmMotionBenchmarkWarmFrames)
            return SvsmMotionBenchmarkSegment::Warm;
        if (frame <
            SvsmMotionBenchmarkWarmFrames +
                SvsmMotionBenchmarkTurnFrames)
        {
            return SvsmMotionBenchmarkSegment::TurnRight;
        }
        if (frame <
            SvsmMotionBenchmarkWarmFrames +
                SvsmMotionBenchmarkTurnFrames +
                SvsmMotionBenchmarkHoldFrames)
        {
            return SvsmMotionBenchmarkSegment::HoldRight;
        }
        if (frame < SvsmMotionBenchmarkEndFrame)
            return SvsmMotionBenchmarkSegment::TurnBack;
        return SvsmMotionBenchmarkSegment::Complete;
    }

    [[nodiscard]] constexpr float
    GetSvsmMotionBenchmarkAngleDegrees(uint64_t frame)
    {
        switch (GetSvsmMotionBenchmarkSegment(frame))
        {
        case SvsmMotionBenchmarkSegment::Warm:
            return 0.f;

        case SvsmMotionBenchmarkSegment::TurnRight:
        {
            const uint64_t turnStep =
                frame - SvsmMotionBenchmarkWarmFrames + 1u;
            return float(turnStep) *
                SvsmMotionBenchmarkDegreesPerFrame;
        }

        case SvsmMotionBenchmarkSegment::HoldRight:
            return SvsmMotionBenchmarkMaximumAngleDegrees;

        case SvsmMotionBenchmarkSegment::TurnBack:
        {
            const uint64_t turnStep =
                frame -
                SvsmMotionBenchmarkWarmFrames -
                SvsmMotionBenchmarkTurnFrames -
                SvsmMotionBenchmarkHoldFrames +
                1u;
            return SvsmMotionBenchmarkMaximumAngleDegrees -
                float(turnStep) *
                    SvsmMotionBenchmarkDegreesPerFrame;
        }

        case SvsmMotionBenchmarkSegment::Complete:
            return 0.f;
        }

        return 0.f;
    }
}
