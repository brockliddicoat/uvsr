#pragma once

#include "temporal_aa_options_shared.h"

#include <cstdint>

namespace uvsr
{
    enum class AntiAliasingQuality : uint32_t
    {
        Low,
        Medium,
        High,
        Ultra,
        Count
    };

    enum class Cmaa2EdgeDetector : uint32_t
    {
        Luma,
        FullColor,
        Count
    };

    inline constexpr float FastApproximateAaMinimumEdgeSharpness = 2.f;
    inline constexpr float FastApproximateAaMaximumEdgeSharpness = 8.f;
    inline constexpr float FastApproximateAaDefaultEdgeSharpness = 8.f;
    inline constexpr float FastApproximateAaMinimumEdgeThreshold = 0.08f;
    inline constexpr float FastApproximateAaMaximumEdgeThreshold = 0.25f;
    inline constexpr float FastApproximateAaDefaultEdgeThreshold = 0.08f;
    inline constexpr float FastApproximateAaMinimumDarkEdgeThreshold = 0.04f;
    inline constexpr float FastApproximateAaMaximumDarkEdgeThreshold = 0.06f;
    inline constexpr float FastApproximateAaDefaultDarkEdgeThreshold = 0.04f;
    inline constexpr float Cmaa2MinimumEdgeThreshold = 0.05f;
    inline constexpr float Cmaa2MaximumEdgeThreshold = 0.15f;
    inline constexpr float Cmaa2DefaultEdgeThreshold = 0.05f;

    struct FastApproximateAaQualityPreset
    {
        float edgeSharpness;
        float edgeThreshold;
        float darkEdgeThreshold;
    };

    struct Cmaa2QualityPreset
    {
        float edgeThreshold;
        Cmaa2EdgeDetector detector;
    };

    enum class TemporalAaJitterSequence : uint32_t
    {
        RotatedGrid4,
        UniformHelix4,
        Halton23x8,
        Halton23x16,
        Halton23x32,
        Sobol32,
        Count
    };

    enum class TemporalAaCostMode : uint32_t
    {
        FullQuality,
        Reduced,
        Minimum,
        Count
    };

    enum class TemporalAaMotionSource : uint32_t
    {
        Center = UVSR_TAA_MOTION_CENTER,
        ClosestCross = UVSR_TAA_MOTION_CLOSEST_CROSS,
        CenterFirstEdgeDilation =
            UVSR_TAA_MOTION_CENTER_FIRST_EDGE_DILATION,
        Count = UVSR_TAA_MOTION_SOURCE_COUNT
    };

    enum class TemporalAaCurrentReconstruction : uint32_t
    {
        Direct = UVSR_TAA_CURRENT_DIRECT,
        DeJittered = UVSR_TAA_CURRENT_DEJITTERED,
        Count = UVSR_TAA_CURRENT_RECONSTRUCTION_COUNT
    };

    enum class TemporalAaHistoryFilter : uint32_t
    {
        Bilinear = UVSR_TAA_HISTORY_BILINEAR,
        OneSampleBicubic = UVSR_TAA_HISTORY_ONE_SAMPLE_BICUBIC,
        FiveTapCatmullRom = UVSR_TAA_HISTORY_FIVE_TAP_CATMULL_ROM,
        NineTapCatmullRom = UVSR_TAA_HISTORY_NINE_TAP_CATMULL_ROM,
        Count = UVSR_TAA_HISTORY_FILTER_COUNT
    };

    enum class TemporalAaRectification : uint32_t
    {
        PairRgb = UVSR_TAA_RECTIFICATION_PAIR_RGB,
        VarianceYCoCg = UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG,
        Count = UVSR_TAA_RECTIFICATION_COUNT
    };

    enum class TemporalAaHistoryStorage : uint32_t
    {
        Robust,
        Compact,
        Count
    };

    enum class TemporalAaDepthValidation : uint32_t
    {
        FourTexelFootprint,
        NearestTexel,
        Count
    };

    enum class TemporalAaHistoryWeightPolicy : uint32_t
    {
        ConfidenceRecurrence,
        ImmediateHorizon,
        Count
    };

    enum class TemporalAaMotionTrust : uint32_t
    {
        LinearSpeed,
        SquaredSpeed,
        Count
    };

    enum class TemporalAaRectificationClip : uint32_t
    {
        VelocityDilatedLine,
        TightComponent,
        Count
    };

    enum class TemporalAaBlendDomain : uint32_t
    {
        LuminanceCompressed,
        LinearRgb,
        Count
    };

    enum class TemporalAaMotionSourceOverride : uint32_t
    {
        FromPreset,
        Center,
        ClosestCross,
        CenterFirstEdgeDilation,
        Count
    };

    enum class TemporalAaCurrentReconstructionOverride : uint32_t
    {
        FromPreset,
        Direct,
        DeJittered,
        Count
    };

    enum class TemporalAaHistoryFilterOverride : uint32_t
    {
        FromPreset,
        Bilinear,
        OneSampleBicubic,
        FiveTapCatmullRom,
        NineTapCatmullRom,
        Count
    };

    enum class TemporalAaRectificationOverride : uint32_t
    {
        FromPreset,
        PairRgb,
        VarianceYCoCg,
        Count
    };

    enum class TemporalAaHistoryStorageOverride : uint32_t
    {
        FromTemporalCost,
        Robust,
        Compact,
        Count
    };

    enum class TemporalAaHistoryWeightPolicyOverride : uint32_t
    {
        FromTemporalCost,
        ConfidenceRecurrence,
        ImmediateHorizon,
        Count
    };

    enum class TemporalAaMotionTrustOverride : uint32_t
    {
        FromTemporalCost,
        LinearSpeed,
        SquaredSpeed,
        Count
    };

    enum class TemporalAaRectificationClipOverride : uint32_t
    {
        FromTemporalCost,
        VelocityDilatedLine,
        TightComponent,
        Count
    };

    enum class TemporalAaBlendDomainOverride : uint32_t
    {
        FromTemporalCost,
        LuminanceCompressed,
        LinearRgb,
        Count
    };

    enum class TemporalAaAutoToggle : uint32_t
    {
        Auto,
        Off,
        On,
        Count
    };

    struct TemporalAaOptions
    {
        TemporalAaMotionSource motionSource =
            TemporalAaMotionSource::Center;
        TemporalAaCurrentReconstruction currentReconstruction =
            TemporalAaCurrentReconstruction::Direct;
        TemporalAaHistoryFilter historyFilter =
            TemporalAaHistoryFilter::Bilinear;
        TemporalAaRectification rectification =
            TemporalAaRectification::PairRgb;

        [[nodiscard]] constexpr bool operator==(
            const TemporalAaOptions& other) const
        {
            return motionSource == other.motionSource &&
                currentReconstruction == other.currentReconstruction &&
                historyFilter == other.historyFilter &&
                rectification == other.rectification;
        }

    };

    struct TemporalAaAlgorithmOverrides
    {
        TemporalAaMotionSourceOverride motionSource =
            TemporalAaMotionSourceOverride::FromPreset;
        TemporalAaCurrentReconstructionOverride currentReconstruction =
            TemporalAaCurrentReconstructionOverride::FromPreset;
        TemporalAaHistoryFilterOverride historyFilter =
            TemporalAaHistoryFilterOverride::FromPreset;
        TemporalAaRectificationOverride rectification =
            TemporalAaRectificationOverride::FromPreset;
        int32_t historyFrames = -1;
        float historyStrength = -1.f;

        [[nodiscard]] constexpr bool operator==(
            const TemporalAaAlgorithmOverrides& other) const
        {
            return motionSource == other.motionSource &&
                currentReconstruction == other.currentReconstruction &&
                historyFilter == other.historyFilter &&
                rectification == other.rectification &&
                historyFrames == other.historyFrames &&
                historyStrength == other.historyStrength;
        }

    };

    struct TemporalAaBehaviorOverrides
    {
        TemporalAaHistoryStorageOverride historyStorage =
            TemporalAaHistoryStorageOverride::FromTemporalCost;
        TemporalAaHistoryWeightPolicyOverride historyWeight =
            TemporalAaHistoryWeightPolicyOverride::FromTemporalCost;
        TemporalAaMotionTrustOverride motionTrust =
            TemporalAaMotionTrustOverride::FromTemporalCost;
        TemporalAaRectificationClipOverride rectificationClip =
            TemporalAaRectificationClipOverride::FromTemporalCost;
        TemporalAaBlendDomainOverride blendDomain =
            TemporalAaBlendDomainOverride::FromTemporalCost;
        TemporalAaAutoToggle sharpening = TemporalAaAutoToggle::Auto;

        [[nodiscard]] constexpr bool operator==(
            const TemporalAaBehaviorOverrides& other) const
        {
            return historyStorage == other.historyStorage &&
                historyWeight == other.historyWeight &&
                motionTrust == other.motionTrust &&
                rectificationClip == other.rectificationClip &&
                blendDomain == other.blendDomain &&
                sharpening == other.sharpening;
        }

    };

    struct TemporalAaSettings
    {
        bool enabled = false;
        AntiAliasingQuality quality = AntiAliasingQuality::Medium;
        TemporalAaCostMode costMode = TemporalAaCostMode::Reduced;
        TemporalAaJitterSequence jitterSequence =
            TemporalAaJitterSequence::Halton23x8;
        bool nearestTexelDepth = false;
        TemporalAaAlgorithmOverrides algorithmOverrides;
        TemporalAaBehaviorOverrides behaviorOverrides;

        [[nodiscard]] constexpr bool operator==(
            const TemporalAaSettings& other) const
        {
            return enabled == other.enabled &&
                quality == other.quality &&
                costMode == other.costMode &&
                jitterSequence == other.jitterSequence &&
                nearestTexelDepth == other.nearestTexelDepth &&
                algorithmOverrides == other.algorithmOverrides &&
                behaviorOverrides == other.behaviorOverrides;
        }

    };

    struct FastApproximateAaSettings
    {
        bool enabled = false;
        AntiAliasingQuality quality = AntiAliasingQuality::Ultra;
        float edgeSharpness = FastApproximateAaDefaultEdgeSharpness;
        float edgeThreshold = FastApproximateAaDefaultEdgeThreshold;
        float darkEdgeThreshold =
            FastApproximateAaDefaultDarkEdgeThreshold;

        [[nodiscard]] constexpr bool operator==(
            const FastApproximateAaSettings& other) const
        {
            return enabled == other.enabled &&
                quality == other.quality &&
                edgeSharpness == other.edgeSharpness &&
                edgeThreshold == other.edgeThreshold &&
                darkEdgeThreshold == other.darkEdgeThreshold;
        }

    };

    struct Cmaa2Settings
    {
        bool enabled = false;
        AntiAliasingQuality quality = AntiAliasingQuality::Ultra;
        float edgeThreshold = Cmaa2DefaultEdgeThreshold;
        Cmaa2EdgeDetector detector = Cmaa2EdgeDetector::FullColor;

        [[nodiscard]] constexpr bool operator==(
            const Cmaa2Settings& other) const
        {
            return enabled == other.enabled &&
                quality == other.quality &&
                edgeThreshold == other.edgeThreshold &&
                detector == other.detector;
        }

    };

    struct MsaaSettings
    {
        bool enabled = false;
        AntiAliasingQuality quality = AntiAliasingQuality::Medium;
        uint32_t sampleCount = 4u;

        [[nodiscard]] constexpr bool operator==(
            const MsaaSettings& other) const
        {
            return enabled == other.enabled &&
                quality == other.quality &&
                sampleCount == other.sampleCount;
        }

    };

    struct AntiAliasingSettings
    {
        TemporalAaSettings temporal;
        FastApproximateAaSettings fastApproximate;
        Cmaa2Settings cmaa2;
        MsaaSettings msaa;

        [[nodiscard]] constexpr bool operator==(
            const AntiAliasingSettings& other) const
        {
            return temporal == other.temporal &&
                fastApproximate == other.fastApproximate &&
                cmaa2 == other.cmaa2 &&
                msaa == other.msaa;
        }

    };

    struct ResolvedAntiAliasingSettings
    {
        bool temporalEnabled = false;
        AntiAliasingQuality temporalQuality =
            AntiAliasingQuality::Medium;
        TemporalAaCostMode temporalCostMode =
            TemporalAaCostMode::Reduced;
        TemporalAaJitterSequence temporalJitterSequence =
            TemporalAaJitterSequence::Halton23x8;
        TemporalAaOptions temporal;
        TemporalAaHistoryStorage historyStorage =
            TemporalAaHistoryStorage::Robust;
        TemporalAaDepthValidation depthValidation =
            TemporalAaDepthValidation::NearestTexel;
        TemporalAaHistoryWeightPolicy historyWeight =
            TemporalAaHistoryWeightPolicy::ConfidenceRecurrence;
        TemporalAaMotionTrust motionTrust =
            TemporalAaMotionTrust::LinearSpeed;
        TemporalAaRectificationClip rectificationClip =
            TemporalAaRectificationClip::VelocityDilatedLine;
        TemporalAaBlendDomain blendDomain =
            TemporalAaBlendDomain::LinearRgb;
        bool sharpeningAllowed = true;
        uint32_t historyFrames = 6u;
        float historyStrength = 1.f;
        bool optimizedCompute = true;
        bool fusedOutput = true;
        bool fastApproximateEnabled = false;
        float fastApproximateEdgeSharpness =
            FastApproximateAaDefaultEdgeSharpness;
        float fastApproximateEdgeThreshold =
            FastApproximateAaDefaultEdgeThreshold;
        float fastApproximateDarkEdgeThreshold =
            FastApproximateAaDefaultDarkEdgeThreshold;
        bool cmaa2Enabled = false;
        float cmaa2EdgeThreshold = Cmaa2DefaultEdgeThreshold;
        Cmaa2EdgeDetector cmaa2EdgeDetector =
            Cmaa2EdgeDetector::FullColor;
        uint32_t rasterSampleCount = 1u;
    };

    struct TemporalAaStaticPerformanceOptions
    {
        bool optimizedCompute = false;
        bool fusedOutput = false;
    };

    inline constexpr uint32_t TemporalAaCurrentReconstructionCount =
        UVSR_TAA_CURRENT_RECONSTRUCTION_COUNT;
    inline constexpr uint32_t TemporalAaHistoryFilterCount =
        UVSR_TAA_HISTORY_FILTER_COUNT;
    inline constexpr uint32_t TemporalAaRectificationCount =
        UVSR_TAA_RECTIFICATION_COUNT;
    inline constexpr uint32_t TemporalAaBlendPermutationCount =
        UVSR_TAA_BLEND_PERMUTATION_COUNT;
    inline constexpr uint32_t TemporalAaStaticPerformanceCount = 4u;

    [[nodiscard]] inline constexpr AntiAliasingQuality
        SanitizeAntiAliasingQuality(AntiAliasingQuality value)
    {
        return value < AntiAliasingQuality::Count
            ? value
            : AntiAliasingQuality::Medium;
    }

    [[nodiscard]] inline constexpr TemporalAaCostMode
        SanitizeTemporalAaCostMode(TemporalAaCostMode value)
    {
        return value < TemporalAaCostMode::Count
            ? value
            : TemporalAaCostMode::Reduced;
    }

    [[nodiscard]] inline constexpr TemporalAaJitterSequence
        SanitizeTemporalAaJitterSequence(TemporalAaJitterSequence value)
    {
        return value < TemporalAaJitterSequence::Count
            ? value
            : TemporalAaJitterSequence::Halton23x8;
    }

    [[nodiscard]] inline constexpr Cmaa2EdgeDetector
        SanitizeCmaa2EdgeDetector(Cmaa2EdgeDetector value)
    {
        return value < Cmaa2EdgeDetector::Count
            ? value
            : Cmaa2EdgeDetector::FullColor;
    }

    [[nodiscard]] inline constexpr float ClampFastApproximateAaEdgeSharpness(
        float value)
    {
        return value >= FastApproximateAaMinimumEdgeSharpness
            ? value <= FastApproximateAaMaximumEdgeSharpness
                ? value
                : FastApproximateAaMaximumEdgeSharpness
            : FastApproximateAaMinimumEdgeSharpness;
    }

    [[nodiscard]] inline constexpr float ClampFastApproximateAaEdgeThreshold(
        float value)
    {
        return value >= FastApproximateAaMinimumEdgeThreshold
            ? value <= FastApproximateAaMaximumEdgeThreshold
                ? value
                : FastApproximateAaMaximumEdgeThreshold
            : FastApproximateAaMinimumEdgeThreshold;
    }

    [[nodiscard]] inline constexpr float
        ClampFastApproximateAaDarkEdgeThreshold(float value)
    {
        return value >= FastApproximateAaMinimumDarkEdgeThreshold
            ? value <= FastApproximateAaMaximumDarkEdgeThreshold
                ? value
                : FastApproximateAaMaximumDarkEdgeThreshold
            : FastApproximateAaMinimumDarkEdgeThreshold;
    }

    [[nodiscard]] inline constexpr float ClampCmaa2EdgeThreshold(float value)
    {
        return value >= Cmaa2MinimumEdgeThreshold
            ? value <= Cmaa2MaximumEdgeThreshold
                ? value
                : Cmaa2MaximumEdgeThreshold
            : Cmaa2MinimumEdgeThreshold;
    }

    [[nodiscard]] inline constexpr uint32_t
        SanitizeMsaaSampleCount(uint32_t value)
    {
        return value == 2u || value == 4u ||
                value == 8u || value == 16u
            ? value
            : 4u;
    }

    [[nodiscard]] inline constexpr FastApproximateAaQualityPreset
        GetFastApproximateAaQualityPreset(AntiAliasingQuality quality)
    {
        switch (SanitizeAntiAliasingQuality(quality))
        {
        case AntiAliasingQuality::Low:
            return { 2.f, 0.25f, 0.06f };
        case AntiAliasingQuality::Medium:
            return { 4.f, 0.1875f, 0.055f };
        case AntiAliasingQuality::High:
            return { 8.f, 0.125f, 0.05f };
        case AntiAliasingQuality::Ultra:
            return { 8.f, 0.08f, 0.04f };
        default:
            return { 4.f, 0.1875f, 0.055f };
        }
    }

    inline constexpr void ApplyFastApproximateAaQualityPreset(
        FastApproximateAaSettings& settings,
        AntiAliasingQuality quality)
    {
        settings.quality = SanitizeAntiAliasingQuality(quality);
        const FastApproximateAaQualityPreset preset =
            GetFastApproximateAaQualityPreset(settings.quality);
        settings.edgeSharpness = preset.edgeSharpness;
        settings.edgeThreshold = preset.edgeThreshold;
        settings.darkEdgeThreshold = preset.darkEdgeThreshold;
    }

    [[nodiscard]] inline constexpr bool
        MatchesFastApproximateAaQualityPreset(
            const FastApproximateAaSettings& settings)
    {
        const FastApproximateAaQualityPreset preset =
            GetFastApproximateAaQualityPreset(settings.quality);
        return settings.edgeSharpness == preset.edgeSharpness &&
            settings.edgeThreshold == preset.edgeThreshold &&
            settings.darkEdgeThreshold == preset.darkEdgeThreshold;
    }

    [[nodiscard]] inline constexpr Cmaa2QualityPreset
        GetCmaa2QualityPreset(AntiAliasingQuality quality)
    {
        switch (SanitizeAntiAliasingQuality(quality))
        {
        case AntiAliasingQuality::Low:
            return { 0.15f, Cmaa2EdgeDetector::Luma };
        case AntiAliasingQuality::Medium:
            return { 0.10f, Cmaa2EdgeDetector::Luma };
        case AntiAliasingQuality::High:
            return { 0.07f, Cmaa2EdgeDetector::Luma };
        case AntiAliasingQuality::Ultra:
            return { 0.05f, Cmaa2EdgeDetector::FullColor };
        default:
            return { 0.10f, Cmaa2EdgeDetector::Luma };
        }
    }

    inline constexpr void ApplyCmaa2QualityPreset(
        Cmaa2Settings& settings,
        AntiAliasingQuality quality)
    {
        settings.quality = SanitizeAntiAliasingQuality(quality);
        const Cmaa2QualityPreset preset =
            GetCmaa2QualityPreset(settings.quality);
        settings.edgeThreshold = preset.edgeThreshold;
        settings.detector = preset.detector;
    }

    [[nodiscard]] inline constexpr bool MatchesCmaa2QualityPreset(
        const Cmaa2Settings& settings)
    {
        const Cmaa2QualityPreset preset =
            GetCmaa2QualityPreset(settings.quality);
        return settings.edgeThreshold == preset.edgeThreshold &&
            settings.detector == preset.detector;
    }

    [[nodiscard]] inline constexpr uint32_t
        GetMultisampleQualitySampleCount(AntiAliasingQuality quality)
    {
        switch (SanitizeAntiAliasingQuality(quality))
        {
        case AntiAliasingQuality::Low: return 2u;
        case AntiAliasingQuality::Medium: return 4u;
        case AntiAliasingQuality::High: return 8u;
        case AntiAliasingQuality::Ultra: return 16u;
        default: return 4u;
        }
    }

    inline constexpr void ApplyMultisampleQualityPreset(
        MsaaSettings& settings,
        AntiAliasingQuality quality)
    {
        settings.quality = SanitizeAntiAliasingQuality(quality);
        settings.sampleCount =
            GetMultisampleQualitySampleCount(settings.quality);
    }

    [[nodiscard]] inline constexpr bool MatchesMultisampleQualityPreset(
        const MsaaSettings& settings)
    {
        return settings.sampleCount ==
            GetMultisampleQualitySampleCount(settings.quality);
    }

    [[nodiscard]] inline constexpr uint32_t
        GetTemporalAaBlendPermutationIndex(
            const TemporalAaOptions& options)
    {
        uint32_t index = static_cast<uint32_t>(options.motionSource);
        index = index * TemporalAaCurrentReconstructionCount +
            static_cast<uint32_t>(options.currentReconstruction);
        index = index * TemporalAaHistoryFilterCount +
            static_cast<uint32_t>(options.historyFilter);
        index = index * TemporalAaRectificationCount +
            static_cast<uint32_t>(options.rectification);
        return index;
    }

    [[nodiscard]] inline constexpr uint32_t
        GetTemporalAaStaticPerformanceIndex(
            const TemporalAaStaticPerformanceOptions& options)
    {
        return uint32_t(options.optimizedCompute) * 2u +
            uint32_t(options.fusedOutput);
    }

    [[nodiscard]] inline constexpr TemporalAaStaticPerformanceOptions
        GetTemporalAaStaticPerformanceOptions(
            const ResolvedAntiAliasingSettings& settings,
            bool fusedOutput)
    {
        return { settings.optimizedCompute, fusedOutput };
    }

    [[nodiscard]] inline constexpr TemporalAaOptions
        GetPresetTemporalOptions(AntiAliasingQuality quality)
    {
        TemporalAaOptions result;
        switch (SanitizeAntiAliasingQuality(quality))
        {
        case AntiAliasingQuality::Low:
            break;
        case AntiAliasingQuality::Medium:
            result.motionSource =
                TemporalAaMotionSource::CenterFirstEdgeDilation;
            break;
        case AntiAliasingQuality::High:
            result.motionSource =
                TemporalAaMotionSource::CenterFirstEdgeDilation;
            result.historyFilter =
                TemporalAaHistoryFilter::OneSampleBicubic;
            result.rectification =
                TemporalAaRectification::VarianceYCoCg;
            break;
        case AntiAliasingQuality::Ultra:
            result.motionSource =
                TemporalAaMotionSource::CenterFirstEdgeDilation;
            result.currentReconstruction =
                TemporalAaCurrentReconstruction::DeJittered;
            result.historyFilter =
                TemporalAaHistoryFilter::FiveTapCatmullRom;
            result.rectification =
                TemporalAaRectification::VarianceYCoCg;
            break;
        default:
            break;
        }
        return result;
    }

    [[nodiscard]] inline constexpr uint32_t
        GetPresetHistoryFrames(AntiAliasingQuality quality)
    {
        switch (SanitizeAntiAliasingQuality(quality))
        {
        case AntiAliasingQuality::Low: return 3u;
        case AntiAliasingQuality::Medium: return 6u;
        case AntiAliasingQuality::High: return 9u;
        case AntiAliasingQuality::Ultra: return 12u;
        default: return 6u;
        }
    }

    [[nodiscard]] inline constexpr uint32_t ResolveHistoryFramesOverride(
        uint32_t preset,
        int32_t value)
    {
        return value < 0
            ? preset
            : static_cast<uint32_t>(
                value < 1 ? 1 : value > 32 ? 32 : value);
    }

    [[nodiscard]] inline constexpr float ClampTemporalAaHistoryStrength(
        float value)
    {
        return value < 0.f ? 0.f : value > 2.f ? 2.f : value;
    }

    [[nodiscard]] inline constexpr TemporalAaMotionSource
        ResolveMotionSourceOverride(
            TemporalAaMotionSource preset,
            TemporalAaMotionSourceOverride value)
    {
        switch (value)
        {
        case TemporalAaMotionSourceOverride::Center:
            return TemporalAaMotionSource::Center;
        case TemporalAaMotionSourceOverride::ClosestCross:
            return TemporalAaMotionSource::ClosestCross;
        case TemporalAaMotionSourceOverride::CenterFirstEdgeDilation:
            return TemporalAaMotionSource::CenterFirstEdgeDilation;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaCurrentReconstruction
        ResolveCurrentReconstructionOverride(
            TemporalAaCurrentReconstruction preset,
            TemporalAaCurrentReconstructionOverride value)
    {
        switch (value)
        {
        case TemporalAaCurrentReconstructionOverride::Direct:
            return TemporalAaCurrentReconstruction::Direct;
        case TemporalAaCurrentReconstructionOverride::DeJittered:
            return TemporalAaCurrentReconstruction::DeJittered;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaHistoryFilter
        ResolveHistoryFilterOverride(
            TemporalAaHistoryFilter preset,
            TemporalAaHistoryFilterOverride value)
    {
        switch (value)
        {
        case TemporalAaHistoryFilterOverride::Bilinear:
            return TemporalAaHistoryFilter::Bilinear;
        case TemporalAaHistoryFilterOverride::OneSampleBicubic:
            return TemporalAaHistoryFilter::OneSampleBicubic;
        case TemporalAaHistoryFilterOverride::FiveTapCatmullRom:
            return TemporalAaHistoryFilter::FiveTapCatmullRom;
        case TemporalAaHistoryFilterOverride::NineTapCatmullRom:
            return TemporalAaHistoryFilter::NineTapCatmullRom;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaRectification
        ResolveRectificationOverride(
            TemporalAaRectification preset,
            TemporalAaRectificationOverride value)
    {
        switch (value)
        {
        case TemporalAaRectificationOverride::PairRgb:
            return TemporalAaRectification::PairRgb;
        case TemporalAaRectificationOverride::VarianceYCoCg:
            return TemporalAaRectification::VarianceYCoCg;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaHistoryStorage
        ResolveHistoryStorageOverride(
            TemporalAaHistoryStorage preset,
            TemporalAaHistoryStorageOverride value)
    {
        switch (value)
        {
        case TemporalAaHistoryStorageOverride::Robust:
            return TemporalAaHistoryStorage::Robust;
        case TemporalAaHistoryStorageOverride::Compact:
            return TemporalAaHistoryStorage::Compact;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaHistoryWeightPolicy
        ResolveHistoryWeightPolicyOverride(
            TemporalAaHistoryWeightPolicy preset,
            TemporalAaHistoryWeightPolicyOverride value)
    {
        switch (value)
        {
        case TemporalAaHistoryWeightPolicyOverride::ConfidenceRecurrence:
            return TemporalAaHistoryWeightPolicy::ConfidenceRecurrence;
        case TemporalAaHistoryWeightPolicyOverride::ImmediateHorizon:
            return TemporalAaHistoryWeightPolicy::ImmediateHorizon;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaMotionTrust
        ResolveMotionTrustOverride(
            TemporalAaMotionTrust preset,
            TemporalAaMotionTrustOverride value)
    {
        switch (value)
        {
        case TemporalAaMotionTrustOverride::LinearSpeed:
            return TemporalAaMotionTrust::LinearSpeed;
        case TemporalAaMotionTrustOverride::SquaredSpeed:
            return TemporalAaMotionTrust::SquaredSpeed;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaRectificationClip
        ResolveRectificationClipOverride(
            TemporalAaRectificationClip preset,
            TemporalAaRectificationClipOverride value)
    {
        switch (value)
        {
        case TemporalAaRectificationClipOverride::VelocityDilatedLine:
            return TemporalAaRectificationClip::VelocityDilatedLine;
        case TemporalAaRectificationClipOverride::TightComponent:
            return TemporalAaRectificationClip::TightComponent;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaBlendDomain
        ResolveBlendDomainOverride(
            TemporalAaBlendDomain preset,
            TemporalAaBlendDomainOverride value)
    {
        switch (value)
        {
        case TemporalAaBlendDomainOverride::LuminanceCompressed:
            return TemporalAaBlendDomain::LuminanceCompressed;
        case TemporalAaBlendDomainOverride::LinearRgb:
            return TemporalAaBlendDomain::LinearRgb;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr ResolvedAntiAliasingSettings
        ResolveAntiAliasingSettings(const AntiAliasingSettings& settings)
    {
        ResolvedAntiAliasingSettings result;
        const TemporalAaSettings& temporal = settings.temporal;
        result.temporalEnabled = temporal.enabled;
        result.temporalQuality =
            SanitizeAntiAliasingQuality(temporal.quality);
        result.temporalCostMode =
            SanitizeTemporalAaCostMode(temporal.costMode);
        result.temporalJitterSequence =
            SanitizeTemporalAaJitterSequence(temporal.jitterSequence);
        result.temporal =
            GetPresetTemporalOptions(result.temporalQuality);
        result.historyFrames = ResolveHistoryFramesOverride(
            GetPresetHistoryFrames(result.temporalQuality),
            temporal.algorithmOverrides.historyFrames);
        result.historyStrength = ClampTemporalAaHistoryStrength(
            temporal.algorithmOverrides.historyStrength >= 0.f
                ? temporal.algorithmOverrides.historyStrength
                : 1.f);

        if (result.temporalCostMode == TemporalAaCostMode::Minimum)
        {
            result.temporal = TemporalAaOptions{};
            result.historyStorage = TemporalAaHistoryStorage::Compact;
            result.historyWeight =
                TemporalAaHistoryWeightPolicy::ImmediateHorizon;
            result.motionTrust = TemporalAaMotionTrust::SquaredSpeed;
            result.rectificationClip =
                TemporalAaRectificationClip::TightComponent;
            result.blendDomain = TemporalAaBlendDomain::LinearRgb;
            result.sharpeningAllowed = false;
        }

        result.depthValidation = temporal.nearestTexelDepth
            ? TemporalAaDepthValidation::NearestTexel
            : TemporalAaDepthValidation::FourTexelFootprint;
        result.temporal.motionSource = ResolveMotionSourceOverride(
            result.temporal.motionSource,
            temporal.algorithmOverrides.motionSource);
        result.temporal.currentReconstruction =
            ResolveCurrentReconstructionOverride(
                result.temporal.currentReconstruction,
                temporal.algorithmOverrides.currentReconstruction);
        result.temporal.historyFilter = ResolveHistoryFilterOverride(
            result.temporal.historyFilter,
            temporal.algorithmOverrides.historyFilter);
        result.temporal.rectification = ResolveRectificationOverride(
            result.temporal.rectification,
            temporal.algorithmOverrides.rectification);
        result.historyStorage = ResolveHistoryStorageOverride(
            result.historyStorage,
            temporal.behaviorOverrides.historyStorage);
        result.historyWeight = ResolveHistoryWeightPolicyOverride(
            result.historyWeight,
            temporal.behaviorOverrides.historyWeight);
        result.motionTrust = ResolveMotionTrustOverride(
            result.motionTrust,
            temporal.behaviorOverrides.motionTrust);
        result.rectificationClip = ResolveRectificationClipOverride(
            result.rectificationClip,
            temporal.behaviorOverrides.rectificationClip);
        result.blendDomain = ResolveBlendDomainOverride(
            result.blendDomain,
            temporal.behaviorOverrides.blendDomain);
        if (temporal.behaviorOverrides.sharpening !=
            TemporalAaAutoToggle::Auto)
        {
            result.sharpeningAllowed =
                temporal.behaviorOverrides.sharpening ==
                    TemporalAaAutoToggle::On;
        }

        result.optimizedCompute =
            result.temporalCostMode != TemporalAaCostMode::FullQuality;
        result.fusedOutput = result.optimizedCompute;
        result.fastApproximateEnabled =
            settings.fastApproximate.enabled;
        result.fastApproximateEdgeSharpness =
            ClampFastApproximateAaEdgeSharpness(
                settings.fastApproximate.edgeSharpness);
        result.fastApproximateEdgeThreshold =
            ClampFastApproximateAaEdgeThreshold(
                settings.fastApproximate.edgeThreshold);
        result.fastApproximateDarkEdgeThreshold =
            ClampFastApproximateAaDarkEdgeThreshold(
                settings.fastApproximate.darkEdgeThreshold);
        result.cmaa2Enabled = settings.cmaa2.enabled;
        result.cmaa2EdgeThreshold =
            ClampCmaa2EdgeThreshold(settings.cmaa2.edgeThreshold);
        result.cmaa2EdgeDetector =
            SanitizeCmaa2EdgeDetector(settings.cmaa2.detector);
        result.rasterSampleCount = settings.msaa.enabled
            ? SanitizeMsaaSampleCount(settings.msaa.sampleCount)
            : 1u;
        return result;
    }

    [[nodiscard]] inline constexpr bool
        IsTemporalAaCompactHistoryCompatible(
            const ResolvedAntiAliasingSettings& settings)
    {
        return settings.temporalEnabled &&
            settings.temporal.motionSource ==
                TemporalAaMotionSource::Center &&
            settings.temporal.currentReconstruction ==
                TemporalAaCurrentReconstruction::Direct &&
            settings.temporal.historyFilter ==
                TemporalAaHistoryFilter::Bilinear &&
            settings.temporal.rectification ==
                TemporalAaRectification::PairRgb &&
            settings.historyWeight ==
                TemporalAaHistoryWeightPolicy::ImmediateHorizon;
    }

    struct TemporalAntiAliasingImageKey
    {
        bool enabled = false;
        TemporalAaJitterSequence jitterSequence =
            TemporalAaJitterSequence::Halton23x8;
        TemporalAaOptions temporal;
        TemporalAaDepthValidation depthValidation =
            TemporalAaDepthValidation::FourTexelFootprint;
        TemporalAaHistoryWeightPolicy historyWeight =
            TemporalAaHistoryWeightPolicy::ConfidenceRecurrence;
        TemporalAaMotionTrust motionTrust =
            TemporalAaMotionTrust::LinearSpeed;
        TemporalAaRectificationClip rectificationClip =
            TemporalAaRectificationClip::VelocityDilatedLine;
        TemporalAaBlendDomain blendDomain =
            TemporalAaBlendDomain::LinearRgb;
        uint32_t historyFrames = 0u;
        float historyStrength = 0.f;

        [[nodiscard]] constexpr bool operator==(
            const TemporalAntiAliasingImageKey& other) const
        {
            return enabled == other.enabled &&
                jitterSequence == other.jitterSequence &&
                temporal == other.temporal &&
                depthValidation == other.depthValidation &&
                historyWeight == other.historyWeight &&
                motionTrust == other.motionTrust &&
                rectificationClip == other.rectificationClip &&
                blendDomain == other.blendDomain &&
                historyFrames == other.historyFrames &&
                historyStrength == other.historyStrength;
        }

        [[nodiscard]] constexpr bool operator!=(
            const TemporalAntiAliasingImageKey& other) const
        {
            return !(*this == other);
        }
    };

    [[nodiscard]] inline constexpr TemporalAntiAliasingImageKey
        GetTemporalAntiAliasingImageKey(
            const ResolvedAntiAliasingSettings& settings)
    {
        TemporalAntiAliasingImageKey result;
        if (!settings.temporalEnabled)
            return result;
        result.enabled = true;
        result.jitterSequence = settings.temporalJitterSequence;
        result.temporal = settings.temporal;
        result.depthValidation = settings.depthValidation;
        result.historyWeight = settings.historyWeight;
        result.motionTrust = settings.motionTrust;
        result.rectificationClip = settings.rectificationClip;
        result.blendDomain = settings.blendDomain;
        result.historyFrames = settings.historyFrames;
        result.historyStrength = settings.historyStrength;
        return result;
    }

    [[nodiscard]] inline constexpr bool
        AntiAliasingSettingsRequireTemporalReset(
            const AntiAliasingSettings& active,
            const AntiAliasingSettings& requested)
    {
        return GetTemporalAntiAliasingImageKey(
                ResolveAntiAliasingSettings(active)) !=
            GetTemporalAntiAliasingImageKey(
                ResolveAntiAliasingSettings(requested));
    }

    [[nodiscard]] inline constexpr uint32_t
        GetTemporalAaHistoryColorSampleCount(
            TemporalAaHistoryFilter filter)
    {
        return filter == TemporalAaHistoryFilter::NineTapCatmullRom
            ? 9u
            : filter == TemporalAaHistoryFilter::FiveTapCatmullRom
                ? 5u
                : 1u;
    }

    [[nodiscard]] inline constexpr uint32_t
        GetTemporalAaHistoryDepthSampleCount(
            TemporalAaHistoryFilter filter)
    {
        return filter == TemporalAaHistoryFilter::NineTapCatmullRom
            ? 8u
            : filter == TemporalAaHistoryFilter::FiveTapCatmullRom
                ? 4u
                : 0u;
    }

    [[nodiscard]] inline constexpr uint32_t GetTemporalAaBehaviorFlags(
        TemporalAaDepthValidation depthValidation,
        TemporalAaHistoryWeightPolicy historyWeight,
        TemporalAaMotionTrust motionTrust,
        TemporalAaRectificationClip rectificationClip,
        TemporalAaBlendDomain blendDomain)
    {
        return
            (depthValidation == TemporalAaDepthValidation::NearestTexel
                ? UVSR_TAA_BEHAVIOR_NEAREST_TEXEL_DEPTH : 0u) |
            (historyWeight ==
                    TemporalAaHistoryWeightPolicy::ImmediateHorizon
                ? UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT : 0u) |
            (motionTrust == TemporalAaMotionTrust::SquaredSpeed
                ? UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST : 0u) |
            (rectificationClip ==
                    TemporalAaRectificationClip::TightComponent
                ? UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION : 0u) |
            (blendDomain == TemporalAaBlendDomain::LinearRgb
                ? UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN : 0u);
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaCostModeLabel(TemporalAaCostMode value)
    {
        switch (value)
        {
        case TemporalAaCostMode::FullQuality: return "Full Quality";
        case TemporalAaCostMode::Reduced: return "Reduced";
        case TemporalAaCostMode::Minimum: return "Minimum";
        default: return "Unavailable";
        }
    }

}
