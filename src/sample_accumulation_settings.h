#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string_view>

namespace uvsr
{
    enum class SampleAccumulationPreset : uint32_t
    {
        Progressive,
        Responsive,
        VarianceGuided,
        Count
    };

    enum class SampleAccumulationHistoryPreset : uint32_t
    {
        QuickPreview,
        Responsive,
        Balanced,
        Stable,
        VeryStable,
        Count
    };

    enum class SampleAccumulationWorkloadPreset : uint32_t
    {
        FullQuality,
        Balanced,
        Performance,
        MaximumSavings,
        Count
    };

    enum class SampleAccumulationAveraging : uint32_t
    {
        Cumulative,
        Exponential,
        Count
    };

    enum class SampleAccumulationScheduling : uint32_t
    {
        EveryPixel,
        VarianceGuided,
        Count
    };

    inline constexpr uint32_t SampleAccumulationMinimumEffectiveHistory = 2u;
    inline constexpr uint32_t SampleAccumulationMaximumEffectiveHistory =
        4096u;
    inline constexpr uint32_t SampleAccumulationMinimumWarmupSamples = 2u;
    inline constexpr uint32_t SampleAccumulationMaximumWarmupSamples = 256u;
    inline constexpr float SampleAccumulationMinimumTargetRelativeError =
        0.001f;
    inline constexpr float SampleAccumulationMaximumTargetRelativeError =
        0.25f;
    inline constexpr float SampleAccumulationMinimumUpdateRate =
        1.f / 256.f;
    inline constexpr float SampleAccumulationMaximumUpdateRate = 1.f;
    inline constexpr float SampleAccumulationRelativeErrorChannelFloor =
        0.001f;

    struct SampleAccumulationSettings
    {
        SampleAccumulationPreset preset =
            SampleAccumulationPreset::VarianceGuided;
        SampleAccumulationAveraging averaging =
            SampleAccumulationAveraging::Cumulative;
        SampleAccumulationScheduling scheduling =
            SampleAccumulationScheduling::VarianceGuided;
        uint32_t effectiveHistory = 64u;
        uint32_t minimumSamples = 16u;
        float targetRelativeError = 0.02f;
        float minimumUpdateRate = 1.f / 16.f;

        [[nodiscard]] constexpr bool operator==(
            const SampleAccumulationSettings& other) const
        {
            return preset == other.preset &&
                averaging == other.averaging &&
                scheduling == other.scheduling &&
                effectiveHistory == other.effectiveHistory &&
                minimumSamples == other.minimumSamples &&
                targetRelativeError == other.targetRelativeError &&
                minimumUpdateRate == other.minimumUpdateRate;
        }

        [[nodiscard]] constexpr bool operator!=(
            const SampleAccumulationSettings& other) const
        {
            return !(*this == other);
        }
    };

    [[nodiscard]] inline constexpr bool IsValidSampleAccumulationPreset(
        SampleAccumulationPreset value)
    {
        return static_cast<uint32_t>(value) <
            static_cast<uint32_t>(SampleAccumulationPreset::Count);
    }

    [[nodiscard]] inline constexpr bool
        IsValidSampleAccumulationHistoryPreset(
            SampleAccumulationHistoryPreset value)
    {
        return static_cast<uint32_t>(value) <
            static_cast<uint32_t>(SampleAccumulationHistoryPreset::Count);
    }

    [[nodiscard]] inline constexpr bool
        IsValidSampleAccumulationWorkloadPreset(
            SampleAccumulationWorkloadPreset value)
    {
        return static_cast<uint32_t>(value) <
            static_cast<uint32_t>(SampleAccumulationWorkloadPreset::Count);
    }

    [[nodiscard]] inline constexpr bool IsValidSampleAccumulationAveraging(
        SampleAccumulationAveraging value)
    {
        return static_cast<uint32_t>(value) <
            static_cast<uint32_t>(SampleAccumulationAveraging::Count);
    }

    [[nodiscard]] inline constexpr bool IsValidSampleAccumulationScheduling(
        SampleAccumulationScheduling value)
    {
        return static_cast<uint32_t>(value) <
            static_cast<uint32_t>(SampleAccumulationScheduling::Count);
    }

    [[nodiscard]] inline constexpr std::string_view
        GetSampleAccumulationPresetLabel(SampleAccumulationPreset value)
    {
        switch (value)
        {
        case SampleAccumulationPreset::Progressive:
            return "Progressive Mean";
        case SampleAccumulationPreset::Responsive:
            return "Responsive Mean";
        case SampleAccumulationPreset::VarianceGuided:
            return "Variance Guided";
        default:
            return {};
        }
    }

    [[nodiscard]] inline constexpr std::string_view
        GetSampleAccumulationHistoryPresetLabel(
            SampleAccumulationHistoryPreset value)
    {
        switch (value)
        {
        case SampleAccumulationHistoryPreset::QuickPreview:
            return "Quick Preview";
        case SampleAccumulationHistoryPreset::Responsive:
            return "Responsive";
        case SampleAccumulationHistoryPreset::Balanced:
            return "Balanced";
        case SampleAccumulationHistoryPreset::Stable:
            return "Stable";
        case SampleAccumulationHistoryPreset::VeryStable:
            return "Very Stable";
        default:
            return {};
        }
    }

    [[nodiscard]] inline constexpr uint32_t
        GetSampleAccumulationHistoryPresetValue(
            SampleAccumulationHistoryPreset value)
    {
        switch (value)
        {
        case SampleAccumulationHistoryPreset::QuickPreview:
            return 8u;
        case SampleAccumulationHistoryPreset::Responsive:
            return 32u;
        case SampleAccumulationHistoryPreset::Balanced:
            return 64u;
        case SampleAccumulationHistoryPreset::Stable:
            return 256u;
        case SampleAccumulationHistoryPreset::VeryStable:
            return 1024u;
        default:
            return 64u;
        }
    }

    [[nodiscard]] inline constexpr std::string_view
        GetSampleAccumulationWorkloadPresetLabel(
            SampleAccumulationWorkloadPreset value)
    {
        switch (value)
        {
        case SampleAccumulationWorkloadPreset::FullQuality:
            return "Full Quality";
        case SampleAccumulationWorkloadPreset::Balanced:
            return "Balanced";
        case SampleAccumulationWorkloadPreset::Performance:
            return "Performance";
        case SampleAccumulationWorkloadPreset::MaximumSavings:
            return "Maximum Savings";
        default:
            return {};
        }
    }

    struct SampleAccumulationWorkloadValues
    {
        uint32_t minimumSamples;
        float targetRelativeError;
        float minimumUpdateRate;
    };

    [[nodiscard]] inline constexpr SampleAccumulationWorkloadValues
        GetSampleAccumulationWorkloadPresetValues(
            SampleAccumulationWorkloadPreset value)
    {
        switch (value)
        {
        case SampleAccumulationWorkloadPreset::FullQuality:
            return { 32u, 0.01f, 1.f / 4.f };
        case SampleAccumulationWorkloadPreset::Performance:
            return { 8u, 0.04f, 1.f / 32.f };
        case SampleAccumulationWorkloadPreset::MaximumSavings:
            return { 4u, 0.08f, 1.f / 64.f };
        case SampleAccumulationWorkloadPreset::Balanced:
        default:
            return { 16u, 0.02f, 1.f / 16.f };
        }
    }

    [[nodiscard]] inline constexpr std::string_view
        GetSampleAccumulationAveragingLabel(
            SampleAccumulationAveraging value)
    {
        switch (value)
        {
        case SampleAccumulationAveraging::Cumulative:
            return "Cumulative Mean";
        case SampleAccumulationAveraging::Exponential:
            return "Exponential Mean";
        default:
            return {};
        }
    }

    [[nodiscard]] inline constexpr std::string_view
        GetSampleAccumulationSchedulingLabel(
            SampleAccumulationScheduling value)
    {
        switch (value)
        {
        case SampleAccumulationScheduling::EveryPixel:
            return "Every Pixel";
        case SampleAccumulationScheduling::VarianceGuided:
            return "Variance Guided";
        default:
            return {};
        }
    }

    [[nodiscard]] inline constexpr SampleAccumulationSettings
        ApplySampleAccumulationPreset(
            SampleAccumulationSettings settings,
            SampleAccumulationPreset preset)
    {
        if (!IsValidSampleAccumulationPreset(preset))
            preset = SampleAccumulationPreset::VarianceGuided;

        settings.preset = preset;
        switch (preset)
        {
        case SampleAccumulationPreset::Progressive:
            settings.averaging = SampleAccumulationAveraging::Cumulative;
            settings.scheduling = SampleAccumulationScheduling::EveryPixel;
            settings.effectiveHistory = 64u;
            settings.minimumSamples = 16u;
            settings.targetRelativeError = 0.02f;
            settings.minimumUpdateRate = 1.f / 16.f;
            break;
        case SampleAccumulationPreset::Responsive:
            settings.averaging = SampleAccumulationAveraging::Exponential;
            settings.scheduling = SampleAccumulationScheduling::EveryPixel;
            settings.effectiveHistory = 32u;
            settings.minimumSamples = 16u;
            settings.targetRelativeError = 0.02f;
            settings.minimumUpdateRate = 1.f / 16.f;
            break;
        case SampleAccumulationPreset::VarianceGuided:
            settings.averaging = SampleAccumulationAveraging::Cumulative;
            settings.scheduling =
                SampleAccumulationScheduling::VarianceGuided;
            settings.effectiveHistory = 64u;
            settings.minimumSamples = 16u;
            settings.targetRelativeError = 0.02f;
            settings.minimumUpdateRate = 1.f / 16.f;
            break;
        case SampleAccumulationPreset::Count:
            break;
        }
        return settings;
    }

    [[nodiscard]] inline constexpr SampleAccumulationSettings
        ApplySampleAccumulationHistoryPreset(
            SampleAccumulationSettings settings,
            SampleAccumulationHistoryPreset preset)
    {
        if (!IsValidSampleAccumulationHistoryPreset(preset))
            preset = SampleAccumulationHistoryPreset::Balanced;
        settings.effectiveHistory =
            GetSampleAccumulationHistoryPresetValue(preset);
        return settings;
    }

    [[nodiscard]] inline constexpr SampleAccumulationSettings
        ApplySampleAccumulationWorkloadPreset(
            SampleAccumulationSettings settings,
            SampleAccumulationWorkloadPreset preset)
    {
        if (!IsValidSampleAccumulationWorkloadPreset(preset))
            preset = SampleAccumulationWorkloadPreset::Balanced;
        const SampleAccumulationWorkloadValues values =
            GetSampleAccumulationWorkloadPresetValues(preset);
        settings.minimumSamples = values.minimumSamples;
        settings.targetRelativeError = values.targetRelativeError;
        settings.minimumUpdateRate = values.minimumUpdateRate;
        return settings;
    }

    [[nodiscard]] inline constexpr SampleAccumulationHistoryPreset
        GetMatchingSampleAccumulationHistoryPreset(
            const SampleAccumulationSettings& settings)
    {
        for (uint32_t index = 0u;
            index < uint32_t(SampleAccumulationHistoryPreset::Count);
            ++index)
        {
            const auto preset =
                static_cast<SampleAccumulationHistoryPreset>(index);
            if (settings.effectiveHistory ==
                GetSampleAccumulationHistoryPresetValue(preset))
            {
                return preset;
            }
        }
        return SampleAccumulationHistoryPreset::Count;
    }

    [[nodiscard]] inline constexpr SampleAccumulationWorkloadPreset
        GetMatchingSampleAccumulationWorkloadPreset(
            const SampleAccumulationSettings& settings)
    {
        for (uint32_t index = 0u;
            index < uint32_t(SampleAccumulationWorkloadPreset::Count);
            ++index)
        {
            const auto preset =
                static_cast<SampleAccumulationWorkloadPreset>(index);
            const SampleAccumulationWorkloadValues values =
                GetSampleAccumulationWorkloadPresetValues(preset);
            if (settings.minimumSamples == values.minimumSamples &&
                settings.targetRelativeError == values.targetRelativeError &&
                settings.minimumUpdateRate == values.minimumUpdateRate)
            {
                return preset;
            }
        }
        return SampleAccumulationWorkloadPreset::Count;
    }

    [[nodiscard]] inline constexpr bool SampleAccumulationControlsEqual(
        const SampleAccumulationSettings& left,
        const SampleAccumulationSettings& right)
    {
        return left.averaging == right.averaging &&
            left.scheduling == right.scheduling &&
            left.effectiveHistory == right.effectiveHistory &&
            left.minimumSamples == right.minimumSamples &&
            left.targetRelativeError == right.targetRelativeError &&
            left.minimumUpdateRate == right.minimumUpdateRate;
    }

    [[nodiscard]] inline constexpr bool MatchesSampleAccumulationPreset(
        const SampleAccumulationSettings& settings,
        SampleAccumulationPreset preset)
    {
        if (!IsValidSampleAccumulationPreset(preset))
            return false;
        const SampleAccumulationSettings baseline =
            ApplySampleAccumulationPreset(
                SampleAccumulationSettings{}, preset);
        return SampleAccumulationControlsEqual(settings, baseline);
    }

    [[nodiscard]] inline constexpr bool
        IsSampleAccumulationPresetCustomized(
            const SampleAccumulationSettings& settings)
    {
        return !MatchesSampleAccumulationPreset(
            settings, settings.preset);
    }

    [[nodiscard]] inline SampleAccumulationSettings
        SanitizeSampleAccumulationSettings(
            const SampleAccumulationSettings& settings)
    {
        const SampleAccumulationSettings defaults;
        SampleAccumulationSettings result = settings;
        if (!IsValidSampleAccumulationPreset(result.preset))
            result.preset = defaults.preset;
        if (!IsValidSampleAccumulationAveraging(result.averaging))
            result.averaging = defaults.averaging;
        if (!IsValidSampleAccumulationScheduling(result.scheduling))
            result.scheduling = defaults.scheduling;
        result.effectiveHistory = std::clamp(
            result.effectiveHistory,
            SampleAccumulationMinimumEffectiveHistory,
            SampleAccumulationMaximumEffectiveHistory);
        result.minimumSamples = std::clamp(
            result.minimumSamples,
            SampleAccumulationMinimumWarmupSamples,
            SampleAccumulationMaximumWarmupSamples);
        result.targetRelativeError = std::clamp(
            std::isfinite(result.targetRelativeError)
                ? result.targetRelativeError
                : defaults.targetRelativeError,
            SampleAccumulationMinimumTargetRelativeError,
            SampleAccumulationMaximumTargetRelativeError);
        result.minimumUpdateRate = std::clamp(
            std::isfinite(result.minimumUpdateRate)
                ? result.minimumUpdateRate
                : defaults.minimumUpdateRate,
            SampleAccumulationMinimumUpdateRate,
            SampleAccumulationMaximumUpdateRate);
        return result;
    }

    [[nodiscard]] inline float GetSampleAccumulationMeanWeight(
        const SampleAccumulationSettings& settings,
        uint32_t previousCount)
    {
        const SampleAccumulationSettings sanitized =
            SanitizeSampleAccumulationSettings(settings);
        const uint32_t newCount = previousCount ==
                std::numeric_limits<uint32_t>::max()
            ? previousCount
            : previousCount + 1u;
        if (sanitized.averaging ==
            SampleAccumulationAveraging::Exponential)
        {
            return 1.f / float(std::min(
                std::max(newCount, 1u),
                sanitized.effectiveHistory));
        }
        return newCount > previousCount
            ? 1.f / float(newCount)
            : 0.f;
    }

    [[nodiscard]] inline float GetSampleAccumulationUpdateRate(
        const SampleAccumulationSettings& settings,
        uint32_t previousCount,
        float previousMeanChannel,
        float previousChannelVariance)
    {
        const SampleAccumulationSettings sanitized =
            SanitizeSampleAccumulationSettings(settings);
        if (sanitized.scheduling ==
                SampleAccumulationScheduling::EveryPixel ||
            previousCount < sanitized.minimumSamples ||
            !std::isfinite(previousMeanChannel) ||
            !std::isfinite(previousChannelVariance))
        {
            return 1.f;
        }

        uint32_t effectiveSamples = std::max(previousCount, 1u);
        if (sanitized.averaging ==
            SampleAccumulationAveraging::Exponential)
        {
            const uint32_t exponentialSamples =
                sanitized.effectiveHistory * 2u - 1u;
            effectiveSamples = std::min(
                effectiveSamples,
                exponentialSamples);
        }
        const float standardError = std::sqrt(
            std::max(previousChannelVariance, 0.f) /
            float(effectiveSamples));
        const float relativeError = standardError / std::max(
            std::abs(previousMeanChannel),
            SampleAccumulationRelativeErrorChannelFloor);
        return std::clamp(
            relativeError / sanitized.targetRelativeError,
            sanitized.minimumUpdateRate,
            1.f);
    }

    [[nodiscard]] inline uint32_t GetSampleAccumulationUpdateInterval(
        const SampleAccumulationSettings& settings,
        uint32_t previousCount,
        float previousMeanChannel,
        float previousChannelVariance)
    {
        const float updateRate = GetSampleAccumulationUpdateRate(
            settings,
            previousCount,
            previousMeanChannel,
            previousChannelVariance);
        return std::clamp(
            static_cast<uint32_t>(std::floor(1.f / std::max(
                updateRate,
                SampleAccumulationMinimumUpdateRate))),
            1u,
            256u);
    }

    [[nodiscard]] inline constexpr uint32_t
        GetSampleAccumulationCoverageStep(uint32_t updateInterval)
    {
        return updateInterval > 1u
            ? ((updateInterval & 1u) == 0u ? 1u : 0u)
            : 0u;
    }

    [[nodiscard]] inline constexpr bool
        IsSampleAccumulationUpdateScheduled(
            uint64_t schedulingSerial,
            uint32_t schedulePhase,
            uint32_t updateInterval,
            uint32_t previousCount)
    {
        if (updateInterval <= 1u)
            return true;
        const uint32_t serialModulo = static_cast<uint32_t>(
            schedulingSerial % updateInterval);
        const uint32_t successfulPhaseAdvance =
            (previousCount % updateInterval) *
            GetSampleAccumulationCoverageStep(updateInterval);
        return (serialModulo + schedulePhase + successfulPhaseAdvance) %
            updateInterval == 0u;
    }

    struct SampleAccumulationScalarState
    {
        float mean = 0.f;
        float variance = 0.f;
        uint32_t count = 0u;
    };

    [[nodiscard]] inline SampleAccumulationScalarState
        AccumulateScalarSample(
            const SampleAccumulationSettings& settings,
            SampleAccumulationScalarState state,
            float sample)
    {
        if (!std::isfinite(sample))
            return state;
        if (!std::isfinite(state.mean) || !std::isfinite(state.variance))
            state = {};

        const float weight = GetSampleAccumulationMeanWeight(
            settings,
            state.count);
        const float delta = sample - state.mean;
        const float newMean = state.mean + delta * weight;
        const SampleAccumulationSettings sanitized =
            SanitizeSampleAccumulationSettings(settings);
        float newVariance = 0.f;
        if (state.count > 0u)
        {
            if (sanitized.averaging ==
                SampleAccumulationAveraging::Exponential)
            {
                newVariance = (1.f - weight) *
                    (state.variance + weight * delta * delta);
            }
            else
            {
                const uint32_t newCount = state.count ==
                        std::numeric_limits<uint32_t>::max()
                    ? state.count
                    : state.count + 1u;
                newVariance = newCount > state.count
                    ? (float(state.count) * state.variance +
                        delta * (sample - newMean)) / float(newCount)
                    : state.variance;
            }
        }
        state.mean = newMean;
        state.variance = std::max(newVariance, 0.f);
        if (state.count != std::numeric_limits<uint32_t>::max())
            ++state.count;
        return state;
    }
}
