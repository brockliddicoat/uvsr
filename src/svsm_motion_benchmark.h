#pragma once

#include "sparse_virtual_shadow_map_settings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr
{
    [[nodiscard]] inline SparseVirtualShadowMapSettings
    BuildSvsmMotionBenchmarkAcceptanceSettings()
    {
        SparseVirtualShadowMapSettings settings;
        settings.enabled = true;
        ApplySvsmPreset(settings, SvsmPreset::Performance);
        settings.physicalPageCount = 4096u;
        settings.packetStateSortingEnabled = true;
        settings.levelEmptyWorkSkipEnabled = true;
        ApplySvsmFinePageRenderBudget(settings, 4u);
        settings.finiteBudgetStaticDrainEnabled = true;
        settings.allocationBudgetSaturationEarlyOutEnabled = true;
        settings.perPixelMarkingDedupeEnabled = true;
        settings.packetPageCullingEnabled = true;
        settings.dirtyPageScatterRasterEnabled = false;
        settings.scatterAlphaTestEarlyRejectEnabled = true;
        settings.dirtyPageScatterAmplificationGuardEnabled = true;
        settings.dirtyPageScatterMaximumAmplification = 1u;
        settings.packetRectangleDirectScanEnabled = true;
        settings.recentPageEvictionGraceEnabled = true;
        settings.detailedGpuTimingEnabled = false;
        // The benchmark workload is a frozen configuration, not a mutable
        // user-facing preset. Label it Custom after applying every accepted
        // override so a later preset edit cannot silently redefine evidence.
        settings.preset = SvsmPreset::Custom;
        return settings;
    }

    [[nodiscard]] inline std::string
    BuildSvsmMotionBenchmarkConfigurationIdentity(
        const SparseVirtualShadowMapSettings& settings)
    {
        std::ostringstream identity;
        identity.imbue(std::locale::classic());
        identity << std::setprecision(
            std::numeric_limits<float>::max_digits10)
            << "svsm-motion-timing-v2"
            << ";enabled=" << uint32_t(settings.enabled)
            << ";preset=" << uint32_t(settings.preset)
            << ";mode=" << uint32_t(settings.mode)
            << ";markingMode=" << uint32_t(settings.markingMode)
            << ";filterMode=" << uint32_t(settings.filterMode)
            << ";filterKernel=" << uint32_t(settings.filterKernel)
            << ";poissonOrdering=" << uint32_t(settings.poissonOrdering)
            << ";tapCount=" << uint32_t(settings.tapCount)
            << ";resolutionBias=" << uint32_t(settings.resolutionBias)
            << ";debugView=" << uint32_t(settings.debugView)
            << ";firstClipmapExtent=" << settings.firstClipmapExtent
            << ";maximumLightDepth=" << settings.maximumLightDepth
            << ";physicalPageCount=" << settings.physicalPageCount
            << ";pageRenderBudget=" << settings.pageRenderBudget
            << ";coarsestPageRenderBudgetEnabled="
            << uint32_t(settings.coarsestPageRenderBudgetEnabled)
            << ";perPixelMarkingDedupeEnabled="
            << uint32_t(settings.perPixelMarkingDedupeEnabled)
            << ";cachingEnabled=" << uint32_t(settings.cachingEnabled)
            << ";lightDepthOriginGuardBandEnabled="
            << uint32_t(settings.lightDepthOriginGuardBandEnabled)
            << ";lightDepthOriginGuardBandFraction="
            << settings.lightDepthOriginGuardBandFraction
            << ";staticPageRequestReuseEnabled="
            << uint32_t(settings.staticPageRequestReuseEnabled)
            << ";allocationBudgetSaturationEarlyOutEnabled="
            << uint32_t(
                settings.allocationBudgetSaturationEarlyOutEnabled)
            << ";finiteBudgetStaticDrainEnabled="
            << uint32_t(settings.finiteBudgetStaticDrainEnabled)
            << ";staticVisibilityCachingEnabled="
            << uint32_t(settings.staticVisibilityCachingEnabled)
            << ";sceneStateCachingEnabled="
            << uint32_t(settings.sceneStateCachingEnabled)
            << ";casterOnlySceneRevisionEnabled="
            << uint32_t(settings.casterOnlySceneRevisionEnabled)
            << ";renderPacketCachingEnabled="
            << uint32_t(settings.renderPacketCachingEnabled)
            << ";sharedClipmapPacketBuilderEnabled="
            << uint32_t(settings.sharedClipmapPacketBuilderEnabled)
            << ";persistentCasterSourceCachingEnabled="
            << uint32_t(settings.persistentCasterSourceCachingEnabled)
            << ";opaqueRasterSpecializationEnabled="
            << uint32_t(settings.opaqueRasterSpecializationEnabled)
            << ";leanAlphaTestedBindingsEnabled="
            << uint32_t(settings.leanAlphaTestedBindingsEnabled)
            << ";pairedStaticDynamicDepthEnabled="
            << uint32_t(settings.pairedStaticDynamicDepthEnabled)
            << ";deferredStaticDepthMergeEnabled="
            << uint32_t(settings.deferredStaticDepthMergeEnabled)
            << ";movingLightUncachedEnabled="
            << uint32_t(settings.movingLightUncachedEnabled)
            << ";retainPhysicalMappingsOnContentInvalidationEnabled="
            << uint32_t(settings
                .retainPhysicalMappingsOnContentInvalidationEnabled)
            << ";movingLightLodBiasEnabled="
            << uint32_t(settings.movingLightLodBiasEnabled)
            << ";movingLightResolutionBias="
            << uint32_t(settings.movingLightResolutionBias)
            << ";movingLightLodRecoveryFrames="
            << settings.movingLightLodRecoveryFrames
            << ";receiverDistanceMipClampEnabled="
            << uint32_t(settings.receiverDistanceMipClampEnabled)
            << ";receiverDistanceMipClampStartScale="
            << settings.receiverDistanceMipClampStartScale
            << ";receiverDistanceMipClampMaximumLevel="
            << settings.receiverDistanceMipClampMaximumLevel
            << ";movingLightContinuousReceiverBiasEnabled="
            << uint32_t(
                settings.movingLightContinuousReceiverBiasEnabled)
            << ";localizedInvalidationEnabled="
            << uint32_t(settings.localizedInvalidationEnabled)
            << ";tightLocalizedInvalidationBoundsEnabled="
            << uint32_t(settings.tightLocalizedInvalidationBoundsEnabled)
            << ";adaptiveCasterCacheClassificationEnabled="
            << uint32_t(
                settings.adaptiveCasterCacheClassificationEnabled)
            << ";defaultObjectInvalidationMode="
            << uint32_t(settings.defaultObjectInvalidationMode)
            << ";gpuGatedDrawSubmission="
            << uint32_t(settings.gpuGatedDrawSubmission)
            << ";batchedDrawSubmissionEnabled="
            << uint32_t(settings.batchedDrawSubmissionEnabled)
            << ";packetStateSortingEnabled="
            << uint32_t(settings.packetStateSortingEnabled)
            << ";levelEmptyWorkSkipEnabled="
            << uint32_t(settings.levelEmptyWorkSkipEnabled)
            << ";packetPageCullingEnabled="
            << uint32_t(settings.packetPageCullingEnabled)
            << ";hierarchicalScheduledPageMaskEnabled="
            << uint32_t(settings.hierarchicalScheduledPageMaskEnabled)
            << ";receiverPageMaskCullingEnabled="
            << uint32_t(settings.receiverPageMaskCullingEnabled)
            << ";staticDepthHierarchyCullingEnabled="
            << uint32_t(settings.staticDepthHierarchyCullingEnabled)
            << ";staticDepthHierarchyBias="
            << settings.staticDepthHierarchyBias
            << ";dirtyPageScatterRasterEnabled="
            << uint32_t(settings.dirtyPageScatterRasterEnabled)
            << ";scatterAlphaTestEarlyRejectEnabled="
            << uint32_t(settings.scatterAlphaTestEarlyRejectEnabled)
            << ";dirtyPageScatterAmplificationGuardEnabled="
            << uint32_t(
                settings.dirtyPageScatterAmplificationGuardEnabled)
            << ";dirtyPageScatterMaximumAmplification="
            << settings.dirtyPageScatterMaximumAmplification
            << ";packetRectangleDirectScanEnabled="
            << uint32_t(settings.packetRectangleDirectScanEnabled)
            << ";recentPageEvictionGraceEnabled="
            << uint32_t(settings.recentPageEvictionGraceEnabled)
            << ";precomposedClipmapTransformsEnabled="
            << uint32_t(settings.precomposedClipmapTransformsEnabled)
            << ";pageTranslationCachingEnabled="
            << uint32_t(settings.pageTranslationCachingEnabled)
            << ";detailedGpuTimingEnabled="
            << uint32_t(settings.detailedGpuTimingEnabled)
            << ";adaptiveFiltering="
            << uint32_t(settings.adaptiveFiltering);
        return identity.str();
    }

    [[nodiscard]] inline uint64_t
    HashSvsmMotionBenchmarkConfigurationIdentity(
        std::string_view identity)
    {
        uint64_t hash = 14695981039346656037ull;
        for (const unsigned char value : identity)
        {
            hash ^= uint64_t(value);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] inline std::string
    BuildSvsmMotionBenchmarkConfigurationId(
        const SparseVirtualShadowMapSettings& settings)
    {
        const std::string identity =
            BuildSvsmMotionBenchmarkConfigurationIdentity(settings);
        std::ostringstream result;
        result.imbue(std::locale::classic());
        result << std::hex << std::uppercase << std::setfill('0')
            << std::setw(16)
            << HashSvsmMotionBenchmarkConfigurationIdentity(identity);
        return result.str();
    }

    [[nodiscard]] inline bool
    IsSvsmMotionBenchmarkAcceptanceConfiguration(
        const SparseVirtualShadowMapSettings& settings)
    {
        return IsSameSvsmConfiguration(
            settings,
            BuildSvsmMotionBenchmarkAcceptanceSettings());
    }

    [[nodiscard]] inline bool IsValidSvsmMotionBenchmarkTimingValue(
        float value)
    {
        return std::isfinite(value) && value >= 0.f;
    }

    [[nodiscard]] inline bool IsValidSvsmMotionBenchmarkGpuTiming(
        float pageMarkingMilliseconds,
        float allocationMilliseconds,
        float clearingMilliseconds,
        float packetPageCullingMilliseconds,
        float pageRenderingMilliseconds,
        float filteringMilliseconds,
        float totalMilliseconds,
        bool detailedGpuTimingEnabled,
        bool expectedDetailedGpuTimingEnabled)
    {
        const bool timingValuesValid =
            IsValidSvsmMotionBenchmarkTimingValue(
                pageMarkingMilliseconds) &&
            IsValidSvsmMotionBenchmarkTimingValue(
                allocationMilliseconds) &&
            IsValidSvsmMotionBenchmarkTimingValue(
                clearingMilliseconds) &&
            IsValidSvsmMotionBenchmarkTimingValue(
                packetPageCullingMilliseconds) &&
            IsValidSvsmMotionBenchmarkTimingValue(
                pageRenderingMilliseconds) &&
            IsValidSvsmMotionBenchmarkTimingValue(
                filteringMilliseconds) &&
            std::isfinite(totalMilliseconds) &&
            totalMilliseconds > 0.f;
        if (detailedGpuTimingEnabled !=
                expectedDetailedGpuTimingEnabled ||
            !timingValuesValid)
        {
            return false;
        }

        const double total = double(totalMilliseconds);
        const double nestingTolerance =
            std::max(0.0001, total * 0.0001);
        const double stageSum =
            double(pageMarkingMilliseconds) +
            double(allocationMilliseconds) +
            double(clearingMilliseconds) +
            double(packetPageCullingMilliseconds) +
            double(pageRenderingMilliseconds) +
            double(filteringMilliseconds);
        const bool nestedStageTimingsValid =
            double(pageMarkingMilliseconds) <=
                total + nestingTolerance &&
            double(allocationMilliseconds) <=
                total + nestingTolerance &&
            double(clearingMilliseconds) <=
                total + nestingTolerance &&
            double(packetPageCullingMilliseconds) <=
                total + nestingTolerance &&
            double(pageRenderingMilliseconds) <=
                total + nestingTolerance &&
            double(filteringMilliseconds) <=
                total + nestingTolerance &&
            stageSum <= total + nestingTolerance;

        // A total-only slot issues only the total timer query. Every stage
        // value is initialized to exact zero, so any positive stage value is
        // stale or mixed-mode evidence and must fail closed.
        return detailedGpuTimingEnabled
            ? nestedStageTimingsValid
            : (pageMarkingMilliseconds == 0.f &&
                allocationMilliseconds == 0.f &&
                clearingMilliseconds == 0.f &&
                packetPageCullingMilliseconds == 0.f &&
                pageRenderingMilliseconds == 0.f &&
                filteringMilliseconds == 0.f);
    }

    [[nodiscard]] inline bool IsValidSvsmMotionBenchmarkCpuTiming(
        float sceneValidationMilliseconds,
        float clipmapUpdateMilliseconds,
        float packetCullingMilliseconds,
        float totalMilliseconds)
    {
        const bool timingValuesValid =
            IsValidSvsmMotionBenchmarkTimingValue(
                sceneValidationMilliseconds) &&
            IsValidSvsmMotionBenchmarkTimingValue(
                clipmapUpdateMilliseconds) &&
            IsValidSvsmMotionBenchmarkTimingValue(
                packetCullingMilliseconds) &&
            std::isfinite(totalMilliseconds) &&
            totalMilliseconds > 0.f;
        if (!timingValuesValid)
            return false;

        const double total = double(totalMilliseconds);
        const double nestingTolerance =
            std::max(0.0001, total * 0.0001);
        const double stageSum =
            double(sceneValidationMilliseconds) +
            double(clipmapUpdateMilliseconds) +
            double(packetCullingMilliseconds);
        return double(sceneValidationMilliseconds) <=
                total + nestingTolerance &&
            double(clipmapUpdateMilliseconds) <=
                total + nestingTolerance &&
            double(packetCullingMilliseconds) <=
                total + nestingTolerance &&
            stageSum <= total + nestingTolerance;
    }

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

    struct SvsmMotionBenchmarkPathObservation
    {
        bool requestedObserved = false;
        bool activeObserved = false;
        bool inactiveObserved = false;
        bool unavailableObserved = false;
    };

    constexpr void ObserveSvsmMotionBenchmarkPath(
        SvsmMotionBenchmarkPathObservation& observation,
        bool requested,
        bool active,
        bool unavailable)
    {
        observation.requestedObserved |= requested;
        observation.activeObserved |= requested && active;
        observation.inactiveObserved |=
            requested && !active && !unavailable;
        observation.unavailableObserved |=
            requested && unavailable;
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

    enum class SvsmMotionBenchmarkKind : uint32_t
    {
        Camera,
        SunSlow
    };

    enum class SvsmMotionBenchmarkPhase : uint32_t
    {
        Warm,
        Baseline,
        Forward,
        Recovery,
        Reverse,
        FinalRecovery,
        Complete
    };

    constexpr uint32_t SvsmMotionBenchmarkWarmFrames = 180u;
    constexpr uint32_t SvsmMotionBenchmarkTurnFrames = 450u;
    // The stationary hold brings the measured camera lane to exactly 1,000
    // frames without changing either 0.1-degree motion sweep.
    constexpr uint32_t SvsmMotionBenchmarkHoldFrames = 100u;
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
    // The moving-sun lane deliberately includes stationary measurements
    // before, between, and after its two sweeps. The 164-frame recovery is
    // UE's 100-frame static-light qualification window plus 64 frames in
    // which the recovered cache can be measured without another SetDirection.
    constexpr uint32_t SvsmSunMotionBenchmarkWarmFrames = 120u;
    constexpr uint32_t SvsmSunMotionBenchmarkBaselineFrames = 60u;
    constexpr uint32_t SvsmSunMotionBenchmarkTurnFrames = 450u;
    constexpr uint32_t SvsmSunMotionBenchmarkRecoveryFrames = 164u;
    constexpr int32_t SvsmSunMotionBenchmarkMaximumTenthDegreeTicks = 450;
    constexpr uint32_t SvsmSunMotionBenchmarkEndFrame =
        SvsmSunMotionBenchmarkWarmFrames +
        SvsmSunMotionBenchmarkBaselineFrames +
        SvsmSunMotionBenchmarkTurnFrames +
        SvsmSunMotionBenchmarkRecoveryFrames +
        SvsmSunMotionBenchmarkTurnFrames +
        SvsmSunMotionBenchmarkRecoveryFrames;
    constexpr uint32_t SvsmSunMotionBenchmarkMeasurementFrames =
        SvsmSunMotionBenchmarkEndFrame -
        SvsmSunMotionBenchmarkWarmFrames;
    constexpr uint32_t SvsmMotionBenchmarkMaximumMeasurementFrames =
        std::max(
            SvsmMotionBenchmarkMeasurementFrames,
            SvsmSunMotionBenchmarkMeasurementFrames);
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
    IsSvsmMotionBenchmarkHierarchyRequested(
        bool packetPageCullingRequested,
        bool hierarchicalScheduledPageMaskEnabled)
    {
        // Scatter consumes the same scheduled-page hierarchy as exact
        // per-page raster. Keep the request contract independent of the
        // downstream raster submission mode so unavailable hierarchy
        // resources cannot silently pass the evidence gate.
        return packetPageCullingRequested &&
            hierarchicalScheduledPageMaskEnabled;
    }

    [[nodiscard]] constexpr bool
    IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
        bool requested,
        bool active,
        bool unavailable,
        bool staticPageRequestReuseActive,
        bool staticPageDrainActive)
    {
        // Packet-page maintenance is deliberately inactive on the exact
        // zero-work cache path. Accept that state only when requests are
        // already reusable and no finite-budget drain remains. Unsupported
        // resources still fail closed even when this frame has no work.
        const bool noPageMaintenanceRequired =
            staticPageRequestReuseActive && !staticPageDrainActive;
        return !unavailable &&
            (!requested || active || noPageMaintenanceRequired);
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

    [[nodiscard]] constexpr bool IsSvsmMotionBenchmarkEvidenceValidForFrameCount(
        uint64_t expectedSamples,
        uint64_t gpuSamples,
        uint64_t cpuSamples,
        uint64_t issued,
        uint64_t dropped,
        uint64_t retired,
        uint64_t outstanding,
        bool duplicateTag,
        bool invalidTag,
        bool invalidGpuTiming,
        bool invalidCpuTiming)
    {
        return gpuSamples == expectedSamples &&
            cpuSamples == expectedSamples &&
            issued == expectedSamples &&
            dropped == 0u &&
            retired == expectedSamples &&
            outstanding == 0u &&
            !duplicateTag &&
            !invalidTag &&
            !invalidGpuTiming &&
            !invalidCpuTiming;
    }

    [[nodiscard]] constexpr bool IsSvsmMotionBenchmarkEvidenceValid(
        uint64_t gpuSamples,
        uint64_t cpuSamples,
        uint64_t issued,
        uint64_t dropped,
        uint64_t retired,
        uint64_t outstanding,
        bool duplicateTag,
        bool invalidTag,
        bool invalidGpuTiming,
        bool invalidCpuTiming)
    {
        return IsSvsmMotionBenchmarkEvidenceValidForFrameCount(
            SvsmMotionBenchmarkMeasurementFrames,
            gpuSamples,
            cpuSamples,
            issued,
            dropped,
            retired,
            outstanding,
            duplicateTag,
            invalidTag,
            invalidGpuTiming,
            invalidCpuTiming);
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

    [[nodiscard]] constexpr uint32_t
    GetSvsmMotionBenchmarkWarmFrameCount(
        SvsmMotionBenchmarkKind kind)
    {
        return kind == SvsmMotionBenchmarkKind::SunSlow
            ? SvsmSunMotionBenchmarkWarmFrames
            : SvsmMotionBenchmarkWarmFrames;
    }

    [[nodiscard]] constexpr uint32_t
    GetSvsmMotionBenchmarkEndFrame(
        SvsmMotionBenchmarkKind kind)
    {
        return kind == SvsmMotionBenchmarkKind::SunSlow
            ? SvsmSunMotionBenchmarkEndFrame
            : SvsmMotionBenchmarkEndFrame;
    }

    [[nodiscard]] constexpr uint32_t
    GetSvsmMotionBenchmarkMeasurementFrameCount(
        SvsmMotionBenchmarkKind kind)
    {
        return kind == SvsmMotionBenchmarkKind::SunSlow
            ? SvsmSunMotionBenchmarkMeasurementFrames
            : SvsmMotionBenchmarkMeasurementFrames;
    }

    [[nodiscard]] constexpr SvsmMotionBenchmarkPhase
    GetSvsmMotionBenchmarkPhase(
        SvsmMotionBenchmarkKind kind,
        uint64_t frame)
    {
        if (kind == SvsmMotionBenchmarkKind::Camera)
        {
            switch (GetSvsmMotionBenchmarkSegment(frame))
            {
            case SvsmMotionBenchmarkSegment::Warm:
                return SvsmMotionBenchmarkPhase::Warm;
            case SvsmMotionBenchmarkSegment::TurnRight:
                return SvsmMotionBenchmarkPhase::Forward;
            case SvsmMotionBenchmarkSegment::HoldRight:
                return SvsmMotionBenchmarkPhase::Recovery;
            case SvsmMotionBenchmarkSegment::TurnBack:
                return SvsmMotionBenchmarkPhase::Reverse;
            case SvsmMotionBenchmarkSegment::Complete:
                return SvsmMotionBenchmarkPhase::Complete;
            }
            return SvsmMotionBenchmarkPhase::Complete;
        }

        uint64_t boundary = SvsmSunMotionBenchmarkWarmFrames;
        if (frame < boundary)
            return SvsmMotionBenchmarkPhase::Warm;
        boundary += SvsmSunMotionBenchmarkBaselineFrames;
        if (frame < boundary)
            return SvsmMotionBenchmarkPhase::Baseline;
        boundary += SvsmSunMotionBenchmarkTurnFrames;
        if (frame < boundary)
            return SvsmMotionBenchmarkPhase::Forward;
        boundary += SvsmSunMotionBenchmarkRecoveryFrames;
        if (frame < boundary)
            return SvsmMotionBenchmarkPhase::Recovery;
        boundary += SvsmSunMotionBenchmarkTurnFrames;
        if (frame < boundary)
            return SvsmMotionBenchmarkPhase::Reverse;
        boundary += SvsmSunMotionBenchmarkRecoveryFrames;
        if (frame < boundary)
            return SvsmMotionBenchmarkPhase::FinalRecovery;
        return SvsmMotionBenchmarkPhase::Complete;
    }

    [[nodiscard]] constexpr int32_t
    GetSvsmMotionBenchmarkTenthDegreeTicks(
        SvsmMotionBenchmarkKind kind,
        uint64_t frame)
    {
        if (kind == SvsmMotionBenchmarkKind::Camera)
        {
            switch (GetSvsmMotionBenchmarkSegment(frame))
            {
            case SvsmMotionBenchmarkSegment::TurnRight:
                return int32_t(
                    frame - SvsmMotionBenchmarkWarmFrames + 1u);
            case SvsmMotionBenchmarkSegment::HoldRight:
                return int32_t(SvsmMotionBenchmarkTurnFrames);
            case SvsmMotionBenchmarkSegment::TurnBack:
                return int32_t(SvsmMotionBenchmarkTurnFrames) -
                    int32_t(
                        frame -
                        SvsmMotionBenchmarkWarmFrames -
                        SvsmMotionBenchmarkTurnFrames -
                        SvsmMotionBenchmarkHoldFrames +
                        1u);
            case SvsmMotionBenchmarkSegment::Warm:
            case SvsmMotionBenchmarkSegment::Complete:
                return 0;
            }
            return 0;
        }

        const SvsmMotionBenchmarkPhase phase =
            GetSvsmMotionBenchmarkPhase(kind, frame);
        const uint64_t forwardBegin =
            SvsmSunMotionBenchmarkWarmFrames +
            SvsmSunMotionBenchmarkBaselineFrames;
        const uint64_t reverseBegin =
            forwardBegin +
            SvsmSunMotionBenchmarkTurnFrames +
            SvsmSunMotionBenchmarkRecoveryFrames;
        switch (phase)
        {
        case SvsmMotionBenchmarkPhase::Forward:
            return int32_t(frame - forwardBegin + 1u);
        case SvsmMotionBenchmarkPhase::Recovery:
            return SvsmSunMotionBenchmarkMaximumTenthDegreeTicks;
        case SvsmMotionBenchmarkPhase::Reverse:
            return SvsmSunMotionBenchmarkMaximumTenthDegreeTicks -
                int32_t(frame - reverseBegin + 1u);
        case SvsmMotionBenchmarkPhase::Warm:
        case SvsmMotionBenchmarkPhase::Baseline:
        case SvsmMotionBenchmarkPhase::FinalRecovery:
        case SvsmMotionBenchmarkPhase::Complete:
            return 0;
        }
        return 0;
    }

    [[nodiscard]] constexpr float
    GetSvsmMotionBenchmarkAngleDegrees(
        SvsmMotionBenchmarkKind kind,
        uint64_t frame)
    {
        return float(GetSvsmMotionBenchmarkTenthDegreeTicks(kind, frame)) *
            0.1f;
    }

    [[nodiscard]] constexpr bool IsSvsmMotionBenchmarkMeasurementFrame(
        SvsmMotionBenchmarkKind kind,
        uint64_t frame)
    {
        return frame >= GetSvsmMotionBenchmarkWarmFrameCount(kind) &&
            frame < GetSvsmMotionBenchmarkEndFrame(kind);
    }

    [[nodiscard]] constexpr bool
    IsSvsmMotionBenchmarkDirectionUpdateFrame(
        SvsmMotionBenchmarkKind kind,
        uint64_t frame)
    {
        if (kind != SvsmMotionBenchmarkKind::SunSlow)
            return false;
        const SvsmMotionBenchmarkPhase phase =
            GetSvsmMotionBenchmarkPhase(kind, frame);
        return phase == SvsmMotionBenchmarkPhase::Forward ||
            phase == SvsmMotionBenchmarkPhase::Reverse;
    }
}
