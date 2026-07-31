#pragma once

#include "temporal_aa_options_shared.h"

#include <array>
#include <cstdint>

namespace uvsr
{
    enum class AntiAliasingMethod : uint32_t
    {
        TemporalSubpixelMorphological,
        IntelCmaa2,
        Msaa,
        Count
    };

    enum class AntiAliasingQuality : uint32_t
    {
        Low,
        Medium,
        High,
        Ultra,
        Count
    };

    // Controls implementation cost independently from the reconstruction
    // quality preset. FullQuality preserves the established robust history
    // contract. Reduced enables image-equivalent topology optimizations.
    // Minimum selects the compact one-dispatch history path.
    enum class TemporalAaCostMode : uint32_t
    {
        FullQuality,
        Reduced,
        Minimum,
        Count
    };

    [[nodiscard]] inline constexpr TemporalAaCostMode
        SanitizeTemporalAaCostMode(TemporalAaCostMode value)
    {
        return value < TemporalAaCostMode::Count
            ? value
            : TemporalAaCostMode::FullQuality;
    }

    [[nodiscard]] inline constexpr bool IsAntiAliasingQualitySupported(
        AntiAliasingMethod method,
        AntiAliasingQuality quality)
    {
        return quality < AntiAliasingQuality::Count;
    }

    [[nodiscard]] inline constexpr AntiAliasingQuality
        SanitizeAntiAliasingQuality(
            AntiAliasingMethod method,
            AntiAliasingQuality quality)
    {
        return IsAntiAliasingQualitySupported(method, quality)
            ? quality
            : AntiAliasingQuality::High;
    }

    // Internal execution identity. The normal menu never exposes these
    // implementation names; Method + Quality resolves to one of them.
    enum class AntiAliasingPreset : uint32_t
    {
        Off,
        TemporalPerformance,
        TemporalBalanced,
        TemporalQuality,
        TemporalUltra,
        IntelCmaa2,
        Msaa2x,
        Msaa4x,
        Msaa8x,
        Msaa16x,
        Count
    };

    enum class MorphologyApplication : uint32_t
    {
        Off,
        ConservativeMorphological,
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

    enum class TemporalAaSampleResurrection : uint32_t
    {
        Off = UVSR_TAA_SAMPLE_RESURRECTION_OFF,
        OneOlderFrame =
            UVSR_TAA_SAMPLE_RESURRECTION_ONE_OLDER_FRAME,
        TwoOlderFrames =
            UVSR_TAA_SAMPLE_RESURRECTION_TWO_OLDER_FRAMES,
        Count = UVSR_TAA_SAMPLE_RESURRECTION_MODE_COUNT
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
        MovingPoint,
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

    enum class TemporalAaSampleResurrectionOverride : uint32_t
    {
        FromPreset,
        Off,
        OneOlderFrame,
        TwoOlderFrames,
        Count
    };

    enum class TemporalAaHistoryStorageOverride : uint32_t
    {
        FromTemporalCost,
        Robust,
        Compact,
        Count
    };

    enum class TemporalAaDepthValidationOverride : uint32_t
    {
        FromTemporalCost,
        FourTexelFootprint,
        MovingPoint,
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

    enum class MorphologyApplicationOverride : uint32_t
    {
        FromPreset,
        Off,
        ConservativeMorphological,
        Count
    };

    enum class TemporalAaExecutionPath : uint32_t
    {
        Auto,
        Compute,
        FullscreenPixelShader,
        Count
    };

    enum class TemporalAaComputeKernel : uint32_t
    {
        Auto,
        Threads8x8TwoPixels,
        Threads16x8OnePixel,
        Count
    };

    enum class TemporalAaLdsLayout : uint32_t
    {
        Auto,
        Legacy,
        Split,
        SplitAndPacked,
        Count
    };

    enum class TemporalAaAutoToggle : uint32_t
    {
        Auto,
        Off,
        On,
        Count
    };

    enum class TemporalAaPassFusion : uint32_t
    {
        Auto,
        Separate,
        Fused,
        Count
    };

    enum class TemporalAaCacheBlocking : uint32_t
    {
        Auto,
        Off,
        Bands2,
        Bands3,
        Bands4,
        Count
    };

    enum class TemporalAaDebugView : uint32_t
    {
        Off = UVSR_TAA_DEBUG_OFF,
        FinalHistoryWeight = UVSR_TAA_DEBUG_FINAL_HISTORY_WEIGHT,
        SampleResurrection = UVSR_TAA_DEBUG_SAMPLE_RESURRECTION,
        Count = UVSR_TAA_DEBUG_VIEW_COUNT
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

        [[nodiscard]] constexpr bool operator!=(
            const TemporalAaOptions& other) const
        {
            return !(*this == other);
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
        TemporalAaSampleResurrectionOverride sampleResurrection =
            TemporalAaSampleResurrectionOverride::FromPreset;
        MorphologyApplicationOverride subpixelMorphology =
            MorphologyApplicationOverride::FromPreset;
        // -1 inherits the selected method quality. Non-negative values select
        // the CMAA2 presentation quality independently, so changing
        // morphology cannot promote the Temporal or MSAA preset itself.
        int32_t morphologyQuality = -1;
        // -1 inherits the selected preset. Non-negative values are explicit
        // prior-frame horizons; the resolver clamps temporal presets to the
        // normal-menu slider's closed [1, 32] range.
        int32_t historyFrames = -1;
        // A negative value means that the selected preset owns the strength.
        // Non-negative values are an explicit image-quality override in the
        // closed [0, 2] interval.
        float historyStrength = -1.f;

        [[nodiscard]] constexpr bool IsCustom() const
        {
            return motionSource !=
                    TemporalAaMotionSourceOverride::FromPreset ||
                currentReconstruction !=
                    TemporalAaCurrentReconstructionOverride::FromPreset ||
                historyFilter !=
                    TemporalAaHistoryFilterOverride::FromPreset ||
                rectification !=
                    TemporalAaRectificationOverride::FromPreset ||
                sampleResurrection !=
                    TemporalAaSampleResurrectionOverride::FromPreset ||
                subpixelMorphology !=
                    MorphologyApplicationOverride::FromPreset ||
                morphologyQuality >= 0 ||
                historyFrames >= 0 ||
                historyStrength >= 0.f;
        }

        [[nodiscard]] constexpr bool operator==(
            const TemporalAaAlgorithmOverrides& other) const
        {
            return motionSource == other.motionSource &&
                currentReconstruction ==
                    other.currentReconstruction &&
                historyFilter == other.historyFilter &&
                rectification == other.rectification &&
                sampleResurrection == other.sampleResurrection &&
                subpixelMorphology == other.subpixelMorphology &&
                morphologyQuality == other.morphologyQuality &&
                historyFrames == other.historyFrames &&
                historyStrength == other.historyStrength;
        }

        [[nodiscard]] constexpr bool operator!=(
            const TemporalAaAlgorithmOverrides& other) const
        {
            return !(*this == other);
        }
    };

    struct TemporalAaBehaviorOverrides
    {
        TemporalAaHistoryStorageOverride historyStorage =
            TemporalAaHistoryStorageOverride::FromTemporalCost;
        TemporalAaDepthValidationOverride depthValidation =
            TemporalAaDepthValidationOverride::FromTemporalCost;
        TemporalAaHistoryWeightPolicyOverride historyWeight =
            TemporalAaHistoryWeightPolicyOverride::FromTemporalCost;
        TemporalAaMotionTrustOverride motionTrust =
            TemporalAaMotionTrustOverride::FromTemporalCost;
        TemporalAaRectificationClipOverride rectificationClip =
            TemporalAaRectificationClipOverride::FromTemporalCost;
        TemporalAaBlendDomainOverride blendDomain =
            TemporalAaBlendDomainOverride::FromTemporalCost;
        TemporalAaAutoToggle sharpening =
            TemporalAaAutoToggle::Auto;

        [[nodiscard]] constexpr bool IsCustom() const
        {
            return historyStorage !=
                    TemporalAaHistoryStorageOverride::FromTemporalCost ||
                depthValidation !=
                    TemporalAaDepthValidationOverride::FromTemporalCost ||
                historyWeight !=
                    TemporalAaHistoryWeightPolicyOverride::FromTemporalCost ||
                motionTrust !=
                    TemporalAaMotionTrustOverride::FromTemporalCost ||
                rectificationClip !=
                    TemporalAaRectificationClipOverride::FromTemporalCost ||
                blendDomain !=
                    TemporalAaBlendDomainOverride::FromTemporalCost ||
                sharpening != TemporalAaAutoToggle::Auto;
        }

        [[nodiscard]] constexpr bool operator==(
            const TemporalAaBehaviorOverrides& other) const
        {
            return historyStorage == other.historyStorage &&
                depthValidation == other.depthValidation &&
                historyWeight == other.historyWeight &&
                motionTrust == other.motionTrust &&
                rectificationClip == other.rectificationClip &&
                blendDomain == other.blendDomain &&
                sharpening == other.sharpening;
        }

        [[nodiscard]] constexpr bool operator!=(
            const TemporalAaBehaviorOverrides& other) const
        {
            return !(*this == other);
        }
    };

    struct TemporalAaPerformanceOverrides
    {
        TemporalAaExecutionPath executionPath =
            TemporalAaExecutionPath::Auto;
        TemporalAaComputeKernel computeKernel =
            TemporalAaComputeKernel::Auto;
        TemporalAaLdsLayout ldsLayout =
            TemporalAaLdsLayout::Auto;
        TemporalAaAutoToggle sharedWorkReuse =
            TemporalAaAutoToggle::Auto;
        TemporalAaAutoToggle earlyHistoryRejection =
            TemporalAaAutoToggle::Auto;
        TemporalAaPassFusion passFusion =
            TemporalAaPassFusion::Auto;
        TemporalAaCacheBlocking cacheBlocking =
            TemporalAaCacheBlocking::Auto;

        [[nodiscard]] constexpr bool IsCustom() const
        {
            return executionPath != TemporalAaExecutionPath::Auto ||
                computeKernel != TemporalAaComputeKernel::Auto ||
                ldsLayout != TemporalAaLdsLayout::Auto ||
                sharedWorkReuse != TemporalAaAutoToggle::Auto ||
                earlyHistoryRejection != TemporalAaAutoToggle::Auto ||
                passFusion != TemporalAaPassFusion::Auto ||
                cacheBlocking != TemporalAaCacheBlocking::Auto;
        }

        [[nodiscard]] constexpr bool operator==(
            const TemporalAaPerformanceOverrides& other) const
        {
            return executionPath == other.executionPath &&
                computeKernel == other.computeKernel &&
                ldsLayout == other.ldsLayout &&
                sharedWorkReuse == other.sharedWorkReuse &&
                earlyHistoryRejection ==
                    other.earlyHistoryRejection &&
                passFusion == other.passFusion &&
                cacheBlocking == other.cacheBlocking;
        }

        [[nodiscard]] constexpr bool operator!=(
            const TemporalAaPerformanceOverrides& other) const
        {
            return !(*this == other);
        }
    };

    struct AntiAliasingSettings
    {
        // The normal menu exposes bypass as a checkbox while preserving the
        // selected preset for the next enable. AntiAliasingPreset::Off remains
        // an internal resolved state for render-path compatibility.
        bool enabled = true;
        AntiAliasingMethod method =
            AntiAliasingMethod::TemporalSubpixelMorphological;
        AntiAliasingQuality quality =
            AntiAliasingQuality::Medium;
        TemporalAaCostMode temporalCostMode =
            TemporalAaCostMode::Reduced;
        TemporalAaAlgorithmOverrides algorithmOverrides;
        TemporalAaBehaviorOverrides behaviorOverrides;
        TemporalAaPerformanceOverrides performanceOverrides;
    };

    struct ResolvedAntiAliasingSettings
    {
        bool enabled = true;
        AntiAliasingMethod method =
            AntiAliasingMethod::TemporalSubpixelMorphological;
        AntiAliasingQuality quality =
            AntiAliasingQuality::Medium;
        AntiAliasingPreset implementation =
            AntiAliasingPreset::TemporalPerformance;
        TemporalAaCostMode temporalCostMode =
            TemporalAaCostMode::FullQuality;
        TemporalAaOptions temporal;
        MorphologyApplication subpixelMorphology =
            MorphologyApplication::ConservativeMorphological;
        AntiAliasingQuality morphologyQuality =
            AntiAliasingQuality::Medium;
        TemporalAaExecutionPath executionPath =
            TemporalAaExecutionPath::Compute;
        TemporalAaComputeKernel computeKernel =
            TemporalAaComputeKernel::Threads8x8TwoPixels;
        TemporalAaLdsLayout ldsLayout =
            TemporalAaLdsLayout::Legacy;
        bool sharedWorkReuse = false;
        bool earlyHistoryRejection = false;
        TemporalAaPassFusion passFusion =
            TemporalAaPassFusion::Separate;
        TemporalAaCacheBlocking cacheBlocking =
            TemporalAaCacheBlocking::Off;
        TemporalAaSampleResurrection sampleResurrection =
            TemporalAaSampleResurrection::Off;
        TemporalAaHistoryStorage historyStorage =
            TemporalAaHistoryStorage::Robust;
        TemporalAaDepthValidation depthValidation =
            TemporalAaDepthValidation::FourTexelFootprint;
        TemporalAaHistoryWeightPolicy historyWeight =
            TemporalAaHistoryWeightPolicy::ConfidenceRecurrence;
        TemporalAaMotionTrust motionTrust =
            TemporalAaMotionTrust::LinearSpeed;
        TemporalAaRectificationClip rectificationClip =
            TemporalAaRectificationClip::VelocityDilatedLine;
        TemporalAaBlendDomain blendDomain =
            TemporalAaBlendDomain::LuminanceCompressed;
        bool sharpeningAllowed = true;
        // Logical temporal horizon, expressed as prior frames that may
        // influence the current result. This is intentionally distinct from
        // the shared two-slot physical ping-pong allocation.
        uint32_t historyFrames = 0u;
        // Accepted-history scale in [0, 2]. The shader applies it before
        // clamping to the horizon-derived N/(N+1) maximum, so values above
        // one strengthen valid partial history without exceeding the chosen
        // frame horizon.
        float historyStrength = 0.f;
        uint32_t rasterSampleCount = 1u;
    };

    struct TemporalAaStaticPerformanceOptions
    {
        TemporalAaComputeKernel computeKernel =
            TemporalAaComputeKernel::Threads8x8TwoPixels;
        TemporalAaLdsLayout ldsLayout =
            TemporalAaLdsLayout::Legacy;
        bool sharedWorkReuse = false;
        bool earlyHistoryRejection = false;
        bool fusedOutput = false;
    };

    inline constexpr uint32_t TemporalAaStaticPerformanceCount =
        UVSR_TAA_KERNEL_COUNT *
        UVSR_TAA_LDS_LAYOUT_COUNT * 2u * 2u * 2u;

    [[nodiscard]] inline constexpr uint32_t
        GetTemporalAaStaticPerformanceIndex(
            const TemporalAaStaticPerformanceOptions& options)
    {
        uint32_t index =
            options.computeKernel ==
                TemporalAaComputeKernel::Threads16x8OnePixel
                ? UVSR_TAA_KERNEL_16X8_ONE_PIXEL
                : UVSR_TAA_KERNEL_8X8_TWO_PIXELS;
        index = index * UVSR_TAA_LDS_LAYOUT_COUNT +
            (options.ldsLayout ==
                    TemporalAaLdsLayout::SplitAndPacked
                ? UVSR_TAA_LDS_SPLIT_PACKED
                : options.ldsLayout ==
                        TemporalAaLdsLayout::Split
                    ? UVSR_TAA_LDS_SPLIT
                    : UVSR_TAA_LDS_LEGACY);
        index = index * 2u + uint32_t(options.sharedWorkReuse);
        index = index * 2u +
            uint32_t(options.earlyHistoryRejection);
        index = index * 2u + uint32_t(options.fusedOutput);
        return index;
    }

    [[nodiscard]] inline constexpr
        TemporalAaStaticPerformanceOptions
        GetTemporalAaStaticPerformanceOptions(
            const ResolvedAntiAliasingSettings& settings,
            bool fusedOutput)
    {
        TemporalAaStaticPerformanceOptions result;
        result.computeKernel = settings.computeKernel;
        result.ldsLayout = settings.ldsLayout;
        result.sharedWorkReuse = settings.sharedWorkReuse;
        result.earlyHistoryRejection =
            settings.earlyHistoryRejection;
        result.fusedOutput = fusedOutput;
        return result;
    }

    inline constexpr uint32_t TemporalAaMotionSourceCount =
        UVSR_TAA_MOTION_SOURCE_COUNT;
    inline constexpr uint32_t TemporalAaCurrentReconstructionCount =
        UVSR_TAA_CURRENT_RECONSTRUCTION_COUNT;
    inline constexpr uint32_t TemporalAaHistoryFilterCount =
        UVSR_TAA_HISTORY_FILTER_COUNT;
    inline constexpr uint32_t TemporalAaRectificationCount =
        UVSR_TAA_RECTIFICATION_COUNT;
    inline constexpr uint32_t TemporalAaSampleResurrectionCount =
        UVSR_TAA_SAMPLE_RESURRECTION_MODE_COUNT;
    inline constexpr uint32_t TemporalAaResolveDebugViewCount =
        UVSR_TAA_DEBUG_VIEW_COUNT;
    inline constexpr uint32_t TemporalAaDebugViewCount =
        UVSR_AA_DEBUG_VIEW_COUNT;
    inline constexpr uint32_t TemporalAaBlendPermutationCount =
        UVSR_TAA_BLEND_PERMUTATION_COUNT;

    static_assert(
        TemporalAaMotionSourceCount *
            TemporalAaCurrentReconstructionCount *
            TemporalAaHistoryFilterCount *
            TemporalAaRectificationCount ==
        TemporalAaBlendPermutationCount);

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

    [[nodiscard]] inline constexpr bool TemporalAaOptionsRequireReset(
        const TemporalAaOptions& active,
        const TemporalAaOptions& requested)
    {
        return active != requested;
    }

    [[nodiscard]] inline constexpr AntiAliasingPreset
        GetAntiAliasingImplementation(
            AntiAliasingMethod method,
            AntiAliasingQuality quality)
    {
        if (method == AntiAliasingMethod::IntelCmaa2)
            return AntiAliasingPreset::IntelCmaa2;
        if (method == AntiAliasingMethod::Msaa)
        {
            switch (quality)
            {
            case AntiAliasingQuality::Low:
                return AntiAliasingPreset::Msaa2x;
            case AntiAliasingQuality::Medium:
                return AntiAliasingPreset::Msaa4x;
            case AntiAliasingQuality::High:
                return AntiAliasingPreset::Msaa8x;
            case AntiAliasingQuality::Ultra:
                return AntiAliasingPreset::Msaa16x;
            default:
                return AntiAliasingPreset::Off;
            }
        }

        switch (quality)
        {
        case AntiAliasingQuality::Low:
            return AntiAliasingPreset::TemporalPerformance;
        case AntiAliasingQuality::Medium:
            return AntiAliasingPreset::TemporalBalanced;
        case AntiAliasingQuality::High:
            return AntiAliasingPreset::TemporalQuality;
        case AntiAliasingQuality::Ultra:
            return AntiAliasingPreset::TemporalUltra;
        default:
            return AntiAliasingPreset::Off;
        }
    }

    [[nodiscard]] inline constexpr bool IsLongTermTemporalPreset(
        AntiAliasingPreset preset)
    {
        return preset == AntiAliasingPreset::TemporalPerformance ||
            preset == AntiAliasingPreset::TemporalBalanced ||
            preset == AntiAliasingPreset::TemporalQuality ||
            preset == AntiAliasingPreset::TemporalUltra;
    }

    [[nodiscard]] inline constexpr bool UsesTemporalHistory(
        AntiAliasingPreset preset)
    {
        return IsLongTermTemporalPreset(preset);
    }

    [[nodiscard]] inline constexpr bool UsesJitter(
        AntiAliasingPreset preset)
    {
        return UsesTemporalHistory(preset);
    }

    [[nodiscard]] inline constexpr bool UsesAnyAntiAliasing(
        AntiAliasingPreset preset)
    {
        return preset != AntiAliasingPreset::Off;
    }

    [[nodiscard]] inline constexpr bool IsSharpnessRelevant(
        AntiAliasingPreset preset)
    {
        return IsLongTermTemporalPreset(preset);
    }

    [[nodiscard]] inline constexpr bool UsesSampleResurrection(
        TemporalAaSampleResurrection mode)
    {
        return mode != TemporalAaSampleResurrection::Off;
    }

    [[nodiscard]] inline constexpr MorphologyApplication
        GetPresetMorphologyApplication(AntiAliasingPreset preset)
    {
        switch (preset)
        {
        case AntiAliasingPreset::TemporalPerformance:
        case AntiAliasingPreset::TemporalBalanced:
        case AntiAliasingPreset::TemporalQuality:
        case AntiAliasingPreset::TemporalUltra:
        case AntiAliasingPreset::Msaa2x:
        case AntiAliasingPreset::Msaa4x:
        case AntiAliasingPreset::Msaa8x:
        case AntiAliasingPreset::Msaa16x:
            // Temporal and multisample presets are complete AA methods on
            // their own. CMAA2 is an explicit optional post-process, not a
            // hidden full-screen pass charged to every preset.
            return MorphologyApplication::Off;
        case AntiAliasingPreset::IntelCmaa2:
            return MorphologyApplication::ConservativeMorphological;
        default:
            return MorphologyApplication::Off;
        }
    }

    [[nodiscard]] inline constexpr bool
        IsMorphologyApplicationSupported(
            AntiAliasingPreset preset,
            MorphologyApplication application)
    {
        if (preset == AntiAliasingPreset::IntelCmaa2)
        {
            return application ==
                MorphologyApplication::ConservativeMorphological;
        }
        if (IsLongTermTemporalPreset(preset))
            return application < MorphologyApplication::Count;
        if (preset == AntiAliasingPreset::Msaa2x ||
            preset == AntiAliasingPreset::Msaa4x ||
            preset == AntiAliasingPreset::Msaa8x ||
            preset == AntiAliasingPreset::Msaa16x)
        {
            return application == MorphologyApplication::Off ||
                application ==
                    MorphologyApplication::ConservativeMorphological;
        }
        return application == MorphologyApplication::Off;
    }

    [[nodiscard]] inline constexpr TemporalAaOptions
        GetPresetTemporalOptions(AntiAliasingPreset preset)
    {
        TemporalAaOptions result;
        switch (preset)
        {
        case AntiAliasingPreset::TemporalPerformance:
            result.motionSource = TemporalAaMotionSource::Center;
            result.currentReconstruction =
                TemporalAaCurrentReconstruction::Direct;
            result.historyFilter =
                TemporalAaHistoryFilter::Bilinear;
            result.rectification =
                TemporalAaRectification::PairRgb;
            break;
        case AntiAliasingPreset::TemporalBalanced:
            result.motionSource =
                TemporalAaMotionSource::CenterFirstEdgeDilation;
            result.currentReconstruction =
                TemporalAaCurrentReconstruction::Direct;
            result.historyFilter =
                TemporalAaHistoryFilter::Bilinear;
            result.rectification =
                TemporalAaRectification::PairRgb;
            break;
        case AntiAliasingPreset::TemporalQuality:
            result.motionSource =
                TemporalAaMotionSource::CenterFirstEdgeDilation;
            result.currentReconstruction =
                TemporalAaCurrentReconstruction::Direct;
            result.historyFilter =
                TemporalAaHistoryFilter::OneSampleBicubic;
            result.rectification =
                TemporalAaRectification::VarianceYCoCg;
            break;
        case AntiAliasingPreset::TemporalUltra:
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

    [[nodiscard]] inline constexpr bool IsTemporalHistoryFramesConfigurable(
        AntiAliasingPreset preset)
    {
        return IsLongTermTemporalPreset(preset);
    }

    [[nodiscard]] inline constexpr uint32_t GetPresetHistoryFrames(
        AntiAliasingPreset preset)
    {
        switch (preset)
        {
        case AntiAliasingPreset::TemporalPerformance:
            return 3u;
        case AntiAliasingPreset::TemporalBalanced:
            return 6u;
        case AntiAliasingPreset::TemporalQuality:
            return 9u;
        case AntiAliasingPreset::TemporalUltra:
            return 12u;
        default:
            return 0u;
        }
    }

    [[nodiscard]] inline constexpr float GetPresetHistoryStrength(
        AntiAliasingPreset preset)
    {
        return UsesTemporalHistory(preset) ? 1.f : 0.f;
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
        return value < 0.f
            ? 0.f
            : value > 2.f
                ? 2.f
                : value;
    }

    [[nodiscard]] inline constexpr float ApplyTemporalAaHistoryStrength(
        float acceptedHistoryWeight,
        float historyStrength,
        float maximumHistoryWeight)
    {
        const float clampedStrength =
            ClampTemporalAaHistoryStrength(historyStrength);
        const float amplified =
            acceptedHistoryWeight * clampedStrength;
        return amplified < 0.f
            ? 0.f
            : amplified > maximumHistoryWeight
                ? maximumHistoryWeight
                : amplified;
    }

    [[nodiscard]] inline constexpr TemporalAaSampleResurrection
        GetPresetSampleResurrection(AntiAliasingPreset)
    {
        // Sample resurrection remains opt-in Full Quality functionality
        // because its extra history layout and traffic are deliberately not
        // part of the default preset contract.
        return TemporalAaSampleResurrection::Off;
    }

    [[nodiscard]] inline constexpr TemporalAaMotionSource
        ResolveMotionSourceOverride(
            TemporalAaMotionSource preset,
            TemporalAaMotionSourceOverride overrideValue)
    {
        switch (overrideValue)
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
            TemporalAaCurrentReconstructionOverride overrideValue)
    {
        switch (overrideValue)
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
            TemporalAaHistoryFilterOverride overrideValue)
    {
        switch (overrideValue)
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
            TemporalAaRectificationOverride overrideValue)
    {
        switch (overrideValue)
        {
        case TemporalAaRectificationOverride::PairRgb:
            return TemporalAaRectification::PairRgb;
        case TemporalAaRectificationOverride::VarianceYCoCg:
            return TemporalAaRectification::VarianceYCoCg;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaSampleResurrection
        ResolveSampleResurrectionOverride(
            TemporalAaSampleResurrection preset,
            TemporalAaSampleResurrectionOverride overrideValue)
    {
        switch (overrideValue)
        {
        case TemporalAaSampleResurrectionOverride::Off:
            return TemporalAaSampleResurrection::Off;
        case TemporalAaSampleResurrectionOverride::OneOlderFrame:
            return TemporalAaSampleResurrection::OneOlderFrame;
        case TemporalAaSampleResurrectionOverride::TwoOlderFrames:
            return TemporalAaSampleResurrection::TwoOlderFrames;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr MorphologyApplication
        ResolveMorphologyApplicationOverride(
            MorphologyApplication preset,
            MorphologyApplicationOverride overrideValue)
    {
        switch (overrideValue)
        {
        case MorphologyApplicationOverride::Off:
            return MorphologyApplication::Off;
        case MorphologyApplicationOverride::ConservativeMorphological:
            return MorphologyApplication::ConservativeMorphological;
        default:
            return preset;
        }
    }

    // Collapse only overrides whose explicit value is identical to the
    // currently selected preset. This is an explicit settings transition,
    // not a presentation-time mutation: UI composition may call the resolver
    // freely without changing renderer-facing state.
    inline constexpr void NormalizeRedundantAntiAliasingOverrides(
        AntiAliasingSettings& settings)
    {
        const AntiAliasingQuality quality = SanitizeAntiAliasingQuality(
            settings.method,
            settings.quality);
        const AntiAliasingPreset implementation =
            GetAntiAliasingImplementation(settings.method, quality);
        TemporalAaAlgorithmOverrides& overrides =
            settings.algorithmOverrides;

        if (IsLongTermTemporalPreset(implementation))
        {
            const TemporalAaOptions qualityPreset =
                GetPresetTemporalOptions(implementation);
            TemporalAaOptions costPreset = qualityPreset;
            if (SanitizeTemporalAaCostMode(settings.temporalCostMode) ==
                TemporalAaCostMode::Minimum)
            {
                costPreset.motionSource =
                    TemporalAaMotionSource::Center;
                costPreset.currentReconstruction =
                    TemporalAaCurrentReconstruction::Direct;
                costPreset.historyFilter =
                    TemporalAaHistoryFilter::Bilinear;
                costPreset.rectification =
                    TemporalAaRectification::PairRgb;
            }
            if (overrides.motionSource !=
                    TemporalAaMotionSourceOverride::FromPreset &&
                ResolveMotionSourceOverride(
                    costPreset.motionSource,
                    overrides.motionSource) == costPreset.motionSource &&
                ResolveMotionSourceOverride(
                    qualityPreset.motionSource,
                    overrides.motionSource) == qualityPreset.motionSource)
            {
                overrides.motionSource =
                    TemporalAaMotionSourceOverride::FromPreset;
            }
            if (overrides.currentReconstruction !=
                    TemporalAaCurrentReconstructionOverride::FromPreset &&
                ResolveCurrentReconstructionOverride(
                    costPreset.currentReconstruction,
                    overrides.currentReconstruction) ==
                        costPreset.currentReconstruction &&
                ResolveCurrentReconstructionOverride(
                    qualityPreset.currentReconstruction,
                    overrides.currentReconstruction) ==
                        qualityPreset.currentReconstruction)
            {
                overrides.currentReconstruction =
                    TemporalAaCurrentReconstructionOverride::FromPreset;
            }
            if (overrides.historyFilter !=
                    TemporalAaHistoryFilterOverride::FromPreset &&
                ResolveHistoryFilterOverride(
                    costPreset.historyFilter,
                    overrides.historyFilter) == costPreset.historyFilter &&
                ResolveHistoryFilterOverride(
                    qualityPreset.historyFilter,
                    overrides.historyFilter) == qualityPreset.historyFilter)
            {
                overrides.historyFilter =
                    TemporalAaHistoryFilterOverride::FromPreset;
            }
            if (overrides.rectification !=
                    TemporalAaRectificationOverride::FromPreset &&
                ResolveRectificationOverride(
                    costPreset.rectification,
                    overrides.rectification) == costPreset.rectification &&
                ResolveRectificationOverride(
                    qualityPreset.rectification,
                    overrides.rectification) == qualityPreset.rectification)
            {
                overrides.rectification =
                    TemporalAaRectificationOverride::FromPreset;
            }

            const TemporalAaSampleResurrection qualityResurrection =
                GetPresetSampleResurrection(implementation);
            const TemporalAaSampleResurrection costResurrection =
                SanitizeTemporalAaCostMode(settings.temporalCostMode) ==
                        TemporalAaCostMode::FullQuality
                    ? qualityResurrection
                    : TemporalAaSampleResurrection::Off;
            if (overrides.sampleResurrection !=
                    TemporalAaSampleResurrectionOverride::FromPreset &&
                ResolveSampleResurrectionOverride(
                    costResurrection,
                    overrides.sampleResurrection) == costResurrection &&
                ResolveSampleResurrectionOverride(
                    qualityResurrection,
                    overrides.sampleResurrection) == qualityResurrection)
            {
                overrides.sampleResurrection =
                    TemporalAaSampleResurrectionOverride::FromPreset;
            }
        }

        const bool supportsConfigurableMorphology =
            IsLongTermTemporalPreset(implementation) ||
            implementation == AntiAliasingPreset::Msaa2x ||
            implementation == AntiAliasingPreset::Msaa4x ||
            implementation == AntiAliasingPreset::Msaa8x ||
            implementation == AntiAliasingPreset::Msaa16x;
        if (supportsConfigurableMorphology &&
            overrides.morphologyQuality < 0 &&
            overrides.subpixelMorphology !=
                MorphologyApplicationOverride::FromPreset &&
            ResolveMorphologyApplicationOverride(
                GetPresetMorphologyApplication(implementation),
                overrides.subpixelMorphology) ==
                    GetPresetMorphologyApplication(implementation))
        {
            overrides.subpixelMorphology =
                MorphologyApplicationOverride::FromPreset;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaHistoryStorage
        ResolveHistoryStorageOverride(
            TemporalAaHistoryStorage costValue,
            TemporalAaHistoryStorageOverride overrideValue);
    [[nodiscard]] inline constexpr TemporalAaDepthValidation
        ResolveDepthValidationOverride(
            TemporalAaDepthValidation costValue,
            TemporalAaDepthValidationOverride overrideValue);
    [[nodiscard]] inline constexpr TemporalAaHistoryWeightPolicy
        ResolveHistoryWeightPolicyOverride(
            TemporalAaHistoryWeightPolicy costValue,
            TemporalAaHistoryWeightPolicyOverride overrideValue);
    [[nodiscard]] inline constexpr TemporalAaMotionTrust
        ResolveMotionTrustOverride(
            TemporalAaMotionTrust costValue,
            TemporalAaMotionTrustOverride overrideValue);
    [[nodiscard]] inline constexpr TemporalAaRectificationClip
        ResolveRectificationClipOverride(
            TemporalAaRectificationClip costValue,
            TemporalAaRectificationClipOverride overrideValue);
    [[nodiscard]] inline constexpr TemporalAaBlendDomain
        ResolveBlendDomainOverride(
            TemporalAaBlendDomain costValue,
            TemporalAaBlendDomainOverride overrideValue);

    // Auto is intentionally conservative until matched target-adapter
    // measurements promote an optimization. The present Temporal AA 8x8
    // horizontal-pair compute kernel is the only verified execution baseline.
    [[nodiscard]] inline constexpr ResolvedAntiAliasingSettings
    ResolveAntiAliasingSettings(const AntiAliasingSettings& settings)
    {
        ResolvedAntiAliasingSettings result;
        const AntiAliasingQuality quality =
            SanitizeAntiAliasingQuality(
                settings.method,
                settings.quality);
        const AntiAliasingPreset implementation =
            GetAntiAliasingImplementation(
                settings.method,
                quality);
        result.enabled = settings.enabled;
        result.method = settings.method;
        result.quality = quality;
        result.morphologyQuality = quality;
        result.implementation = settings.enabled
            ? implementation
            : AntiAliasingPreset::Off;
        result.temporalCostMode =
            IsLongTermTemporalPreset(result.implementation)
                ? SanitizeTemporalAaCostMode(
                    settings.temporalCostMode)
                : TemporalAaCostMode::FullQuality;
        const uint32_t presetHistoryFrames =
            GetPresetHistoryFrames(result.implementation);
        result.historyFrames =
            IsTemporalHistoryFramesConfigurable(result.implementation)
                ? ResolveHistoryFramesOverride(
                    presetHistoryFrames,
                    settings.algorithmOverrides.historyFrames)
                : presetHistoryFrames;
        const float requestedHistoryStrength =
            settings.algorithmOverrides.historyStrength >= 0.f
                ? settings.algorithmOverrides.historyStrength
                : GetPresetHistoryStrength(result.implementation);
        result.historyStrength =
            !UsesTemporalHistory(result.implementation)
                ? 0.f
                : ClampTemporalAaHistoryStrength(
                    requestedHistoryStrength);
        switch (result.implementation)
        {
        case AntiAliasingPreset::Msaa2x:
            result.rasterSampleCount = 2u;
            break;
        case AntiAliasingPreset::Msaa4x:
            result.rasterSampleCount = 4u;
            break;
        case AntiAliasingPreset::Msaa8x:
            result.rasterSampleCount = 8u;
            break;
        case AntiAliasingPreset::Msaa16x:
            result.rasterSampleCount = 16u;
            break;
        default:
            result.rasterSampleCount = 1u;
            break;
        }
        result.temporal = GetPresetTemporalOptions(implementation);
        result.sampleResurrection =
            IsLongTermTemporalPreset(implementation)
                ? GetPresetSampleResurrection(implementation)
                : TemporalAaSampleResurrection::Off;

        // Temporal Cost is a baseline profile. Supported explicit overrides
        // are applied after these values so every image-affecting difference
        // can be isolated. Older-frame resurrection remains a Full Quality-only
        // feature because its extra history layout is not part of the
        // optimized profiles. Unsupported compact combinations fall back
        // visibly to the robust history path at render time.
        if (result.temporalCostMode == TemporalAaCostMode::Reduced ||
            result.temporalCostMode == TemporalAaCostMode::Minimum)
        {
            // Reduced and Minimum both bypass the expensive stationary-depth
            // footprint while preserving explicit behavior overrides.
            result.depthValidation =
                TemporalAaDepthValidation::MovingPoint;
        }
        if (result.temporalCostMode == TemporalAaCostMode::Minimum)
        {
            result.temporal.motionSource =
                TemporalAaMotionSource::Center;
            result.temporal.currentReconstruction =
                TemporalAaCurrentReconstruction::Direct;
            result.temporal.historyFilter =
                TemporalAaHistoryFilter::Bilinear;
            result.temporal.rectification =
                TemporalAaRectification::PairRgb;
            result.historyStorage =
                TemporalAaHistoryStorage::Compact;
            result.historyWeight =
                TemporalAaHistoryWeightPolicy::ImmediateHorizon;
            result.motionTrust =
                TemporalAaMotionTrust::SquaredSpeed;
            result.rectificationClip =
                TemporalAaRectificationClip::TightComponent;
            result.blendDomain =
                TemporalAaBlendDomain::LinearRgb;
            result.sharpeningAllowed = false;
        }

        // Standalone CMAA2 is a complete method, not a presentation mode.
        // An override left over from a combined Temporal or MSAA
        // configuration must not replace its defining morphology.
        result.subpixelMorphology =
            implementation == AntiAliasingPreset::IntelCmaa2
                    ? MorphologyApplication::ConservativeMorphological
                : ResolveMorphologyApplicationOverride(
                    GetPresetMorphologyApplication(implementation),
                    settings.algorithmOverrides.subpixelMorphology);
        // Temporal and MSAA can optionally apply CMAA2 after their resolved
        // scene color. Standalone CMAA2 always retains that morphology.
        if (!IsMorphologyApplicationSupported(
                implementation,
                result.subpixelMorphology))
        {
            result.subpixelMorphology =
                GetPresetMorphologyApplication(implementation);
        }
        if (implementation != AntiAliasingPreset::IntelCmaa2 &&
            settings.algorithmOverrides.morphologyQuality >= 0)
        {
            const uint32_t morphologyQuality = static_cast<uint32_t>(
                settings.algorithmOverrides.morphologyQuality);
            result.morphologyQuality =
                morphologyQuality <
                        static_cast<uint32_t>(
                            AntiAliasingQuality::Count)
                    ? static_cast<AntiAliasingQuality>(
                        morphologyQuality)
                    : AntiAliasingQuality::Ultra;
        }
        result.temporal.motionSource = ResolveMotionSourceOverride(
            result.temporal.motionSource,
            settings.algorithmOverrides.motionSource);
        result.temporal.currentReconstruction =
            ResolveCurrentReconstructionOverride(
                result.temporal.currentReconstruction,
                settings.algorithmOverrides.currentReconstruction);
        result.temporal.historyFilter = ResolveHistoryFilterOverride(
            result.temporal.historyFilter,
            settings.algorithmOverrides.historyFilter);
        result.temporal.rectification = ResolveRectificationOverride(
            result.temporal.rectification,
            settings.algorithmOverrides.rectification);
        result.sampleResurrection =
            result.temporalCostMode == TemporalAaCostMode::FullQuality
                ? ResolveSampleResurrectionOverride(
                    result.sampleResurrection,
                    settings.algorithmOverrides.sampleResurrection)
                : TemporalAaSampleResurrection::Off;

        result.historyStorage = ResolveHistoryStorageOverride(
            result.historyStorage,
            settings.behaviorOverrides.historyStorage);
        result.depthValidation = ResolveDepthValidationOverride(
            result.depthValidation,
            settings.behaviorOverrides.depthValidation);
        result.historyWeight = ResolveHistoryWeightPolicyOverride(
            result.historyWeight,
            settings.behaviorOverrides.historyWeight);
        result.motionTrust = ResolveMotionTrustOverride(
            result.motionTrust,
            settings.behaviorOverrides.motionTrust);
        result.rectificationClip = ResolveRectificationClipOverride(
            result.rectificationClip,
            settings.behaviorOverrides.rectificationClip);
        result.blendDomain = ResolveBlendDomainOverride(
            result.blendDomain,
            settings.behaviorOverrides.blendDomain);
        if (settings.behaviorOverrides.sharpening !=
            TemporalAaAutoToggle::Auto)
        {
            result.sharpeningAllowed =
                settings.behaviorOverrides.sharpening ==
                TemporalAaAutoToggle::On;
        }

        result.executionPath = TemporalAaExecutionPath::Compute;
        result.computeKernel =
            TemporalAaComputeKernel::Threads8x8TwoPixels;
        result.ldsLayout = TemporalAaLdsLayout::Legacy;
        result.sharedWorkReuse = false;
        result.earlyHistoryRejection = false;
        result.passFusion = TemporalAaPassFusion::Separate;
        result.cacheBlocking = TemporalAaCacheBlocking::Off;
        if (result.temporalCostMode == TemporalAaCostMode::Reduced ||
            result.temporalCostMode == TemporalAaCostMode::Minimum)
        {
            result.ldsLayout =
                TemporalAaLdsLayout::SplitAndPacked;
            result.sharedWorkReuse = true;
            result.earlyHistoryRejection = true;
            result.passFusion = TemporalAaPassFusion::Fused;
        }

        // Full experiment builds may override the cost topology. Production
        // sanitizes these fields before resolution while retaining the
        // runtime-uniform image-behavior and history-storage controls.
        if (settings.performanceOverrides.executionPath !=
            TemporalAaExecutionPath::Auto)
        {
            result.executionPath =
                settings.performanceOverrides.executionPath;
        }
        if (settings.performanceOverrides.computeKernel !=
            TemporalAaComputeKernel::Auto)
        {
            result.computeKernel =
                settings.performanceOverrides.computeKernel;
        }
        if (settings.performanceOverrides.ldsLayout !=
            TemporalAaLdsLayout::Auto)
        {
            result.ldsLayout = settings.performanceOverrides.ldsLayout;
        }
        if (settings.performanceOverrides.sharedWorkReuse !=
            TemporalAaAutoToggle::Auto)
        {
            result.sharedWorkReuse =
                settings.performanceOverrides.sharedWorkReuse ==
                TemporalAaAutoToggle::On;
        }
        if (settings.performanceOverrides.earlyHistoryRejection !=
            TemporalAaAutoToggle::Auto)
        {
            result.earlyHistoryRejection =
                settings.performanceOverrides.earlyHistoryRejection ==
                TemporalAaAutoToggle::On;
        }
        if (settings.performanceOverrides.passFusion !=
            TemporalAaPassFusion::Auto)
        {
            result.passFusion = settings.performanceOverrides.passFusion;
        }
        if (settings.performanceOverrides.cacheBlocking !=
            TemporalAaCacheBlocking::Auto)
        {
            result.cacheBlocking =
                settings.performanceOverrides.cacheBlocking;
        }

        // Resurrection has a different history layout and has only been
        // validated on the baseline robust compute implementation.
        if (UsesSampleResurrection(result.sampleResurrection))
        {
            result.executionPath = TemporalAaExecutionPath::Compute;
            result.computeKernel =
                TemporalAaComputeKernel::Threads8x8TwoPixels;
            result.ldsLayout = TemporalAaLdsLayout::Legacy;
            result.sharedWorkReuse = false;
            result.earlyHistoryRejection = false;
            result.cacheBlocking = TemporalAaCacheBlocking::Off;
        }
        return result;
    }

    [[nodiscard]] inline constexpr bool
        IsTemporalAaCompactHistoryCompatible(
            const ResolvedAntiAliasingSettings& settings)
    {
        return
            IsLongTermTemporalPreset(settings.implementation) &&
            settings.temporal.motionSource ==
                TemporalAaMotionSource::Center &&
            settings.temporal.currentReconstruction ==
                TemporalAaCurrentReconstruction::Direct &&
            settings.temporal.historyFilter ==
                TemporalAaHistoryFilter::Bilinear &&
            settings.temporal.rectification ==
                TemporalAaRectification::PairRgb &&
            settings.sampleResurrection ==
                TemporalAaSampleResurrection::Off &&
            settings.historyWeight ==
                TemporalAaHistoryWeightPolicy::ImmediateHorizon;
    }

    // Resolve only settings backed by the shader/PSO bundle compiled into the
    // current build. Production and developer builds retain Full Quality
    // resurrection; the factory-startup shader experiment omits its resource
    // layout and permutations. Developer execution experiments remain
    // independently gated.
    [[nodiscard]] inline constexpr AntiAliasingSettings
        GetCompiledAntiAliasingSettings(
            const AntiAliasingSettings& settings)
    {
#if UVSR_AA_DEVELOPER_OVERRIDES
        return settings;
#else
        AntiAliasingSettings compiled = settings;
#if !UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
        compiled.algorithmOverrides.sampleResurrection =
            TemporalAaSampleResurrectionOverride::FromPreset;
#endif
        compiled.performanceOverrides =
            TemporalAaPerformanceOverrides{};
        return compiled;
#endif
    }

    [[nodiscard]] inline constexpr ResolvedAntiAliasingSettings
        ResolveCompiledAntiAliasingSettings(
            const AntiAliasingSettings& settings)
    {
        return ResolveAntiAliasingSettings(
            GetCompiledAntiAliasingSettings(settings));
    }

    [[nodiscard]] inline constexpr bool IsAntiAliasingPresetCustom(
        const AntiAliasingSettings& settings)
    {
        AntiAliasingSettings presetSettings = settings;
        presetSettings.algorithmOverrides =
            TemporalAaAlgorithmOverrides{};
        presetSettings.behaviorOverrides =
            TemporalAaBehaviorOverrides{};
        presetSettings.performanceOverrides =
            TemporalAaPerformanceOverrides{};
        const ResolvedAntiAliasingSettings requested =
            ResolveAntiAliasingSettings(settings);
        const ResolvedAntiAliasingSettings preset =
            ResolveAntiAliasingSettings(presetSettings);
        const AntiAliasingPreset implementation =
            GetAntiAliasingImplementation(
                settings.method,
                settings.quality);
        const bool temporalPerformanceChanged =
            requested.executionPath != preset.executionPath ||
            requested.computeKernel != preset.computeKernel ||
            requested.ldsLayout != preset.ldsLayout ||
            requested.sharedWorkReuse != preset.sharedWorkReuse ||
            requested.earlyHistoryRejection !=
                preset.earlyHistoryRejection ||
            requested.passFusion != preset.passFusion ||
            requested.cacheBlocking != preset.cacheBlocking;
        const bool temporalBehaviorChanged =
            requested.historyStorage != preset.historyStorage ||
            requested.depthValidation != preset.depthValidation ||
            requested.historyWeight != preset.historyWeight ||
            requested.motionTrust != preset.motionTrust ||
            requested.rectificationClip !=
                preset.rectificationClip ||
            requested.blendDomain != preset.blendDomain ||
            requested.sharpeningAllowed !=
                preset.sharpeningAllowed;

        if (IsLongTermTemporalPreset(implementation))
        {
            return requested.temporal != preset.temporal ||
                requested.subpixelMorphology !=
                    preset.subpixelMorphology ||
                requested.morphologyQuality !=
                    preset.morphologyQuality ||
                requested.sampleResurrection !=
                    preset.sampleResurrection ||
                requested.historyFrames != preset.historyFrames ||
                requested.historyStrength !=
                    preset.historyStrength ||
                temporalBehaviorChanged ||
                temporalPerformanceChanged;
        }
        if (implementation == AntiAliasingPreset::Msaa2x ||
            implementation == AntiAliasingPreset::Msaa4x ||
            implementation == AntiAliasingPreset::Msaa8x ||
            implementation == AntiAliasingPreset::Msaa16x)
        {
            return requested.subpixelMorphology !=
                    preset.subpixelMorphology ||
                requested.morphologyQuality !=
                    preset.morphologyQuality;
        }
        return false;
    }

    enum class TemporalAntiAliasingTechnique : uint32_t
    {
        None,
        TemporalAa
    };

    struct TemporalAntiAliasingImageKey
    {
        TemporalAntiAliasingTechnique technique =
            TemporalAntiAliasingTechnique::None;
        TemporalAaOptions temporal;
        TemporalAaSampleResurrection sampleResurrection =
            TemporalAaSampleResurrection::Off;
        TemporalAaDepthValidation depthValidation =
            TemporalAaDepthValidation::FourTexelFootprint;
        TemporalAaHistoryWeightPolicy historyWeight =
            TemporalAaHistoryWeightPolicy::ConfidenceRecurrence;
        TemporalAaMotionTrust motionTrust =
            TemporalAaMotionTrust::LinearSpeed;
        TemporalAaRectificationClip rectificationClip =
            TemporalAaRectificationClip::VelocityDilatedLine;
        TemporalAaBlendDomain blendDomain =
            TemporalAaBlendDomain::LuminanceCompressed;
        uint32_t historyFrames = 0u;
        float historyStrength = 0.f;

        [[nodiscard]] constexpr bool operator==(
            const TemporalAntiAliasingImageKey& other) const
        {
            if (technique != other.technique)
                return false;
            if (technique ==
                TemporalAntiAliasingTechnique::TemporalAa)
            {
                return temporal == other.temporal &&
                    sampleResurrection ==
                        other.sampleResurrection &&
                    depthValidation == other.depthValidation &&
                    historyWeight == other.historyWeight &&
                    motionTrust == other.motionTrust &&
                    rectificationClip == other.rectificationClip &&
                    blendDomain == other.blendDomain &&
                    historyFrames == other.historyFrames &&
                    historyStrength == other.historyStrength;
            }
            return true;
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
        if (IsLongTermTemporalPreset(settings.implementation))
        {
            result.technique =
                TemporalAntiAliasingTechnique::TemporalAa;
            result.temporal = settings.temporal;
            result.sampleResurrection =
                settings.sampleResurrection;
            result.depthValidation = settings.depthValidation;
            result.historyWeight = settings.historyWeight;
            result.motionTrust = settings.motionTrust;
            result.rectificationClip =
                settings.rectificationClip;
            result.blendDomain = settings.blendDomain;
            result.historyFrames = settings.historyFrames;
            result.historyStrength = settings.historyStrength;
        }
        return result;
    }

    [[nodiscard]] inline constexpr bool
        AntiAliasingSettingsRequireTemporalReset(
            const AntiAliasingSettings& active,
            const AntiAliasingSettings& requested)
    {
        const ResolvedAntiAliasingSettings activeResolved =
            ResolveAntiAliasingSettings(active);
        const ResolvedAntiAliasingSettings requestedResolved =
            ResolveAntiAliasingSettings(requested);

        // The effective image key intentionally excludes presentation-only
        // morphology, debug views, sharpness, and every
        // image-equivalent execution experiment.
        return GetTemporalAntiAliasingImageKey(activeResolved) !=
            GetTemporalAntiAliasingImageKey(requestedResolved);
    }

    [[nodiscard]] inline constexpr bool
        CompiledAntiAliasingSettingsRequireTemporalReset(
            const AntiAliasingSettings& active,
            const AntiAliasingSettings& requested)
    {
        const ResolvedAntiAliasingSettings activeResolved =
            ResolveCompiledAntiAliasingSettings(active);
        const ResolvedAntiAliasingSettings requestedResolved =
            ResolveCompiledAntiAliasingSettings(requested);
        return GetTemporalAntiAliasingImageKey(activeResolved) !=
            GetTemporalAntiAliasingImageKey(requestedResolved);
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
        // Every mode retains the central four-texel Gather. Wide filters add
        // one discrete Gather for every outer color tap.
        return filter == TemporalAaHistoryFilter::NineTapCatmullRom
            ? 8u
            : filter == TemporalAaHistoryFilter::FiveTapCatmullRom
                ? 4u
                : 0u;
    }

    [[nodiscard]] inline constexpr const char* GetTemporalAaMotionSourceLabel(
        TemporalAaMotionSource value)
    {
        switch (value)
        {
        case TemporalAaMotionSource::Center: return "Center";
        case TemporalAaMotionSource::ClosestCross: return "Closest Cross";
        case TemporalAaMotionSource::CenterFirstEdgeDilation:
            return "Edge Dilation";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaCurrentReconstructionLabel(
            TemporalAaCurrentReconstruction value)
    {
        switch (value)
        {
        case TemporalAaCurrentReconstruction::Direct: return "Direct";
        case TemporalAaCurrentReconstruction::DeJittered:
            return "De-Jittered";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char* GetTemporalAaHistoryFilterLabel(
        TemporalAaHistoryFilter value)
    {
        switch (value)
        {
        case TemporalAaHistoryFilter::Bilinear: return "1x Bilinear";
        case TemporalAaHistoryFilter::OneSampleBicubic:
            return "1x Bicubic";
        case TemporalAaHistoryFilter::FiveTapCatmullRom:
            return "5x Bicubic";
        case TemporalAaHistoryFilter::NineTapCatmullRom:
            return "9x Bicubic";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaRectificationLabel(TemporalAaRectification value)
    {
        switch (value)
        {
        case TemporalAaRectification::PairRgb:
            return "Pair Tristimulus";
        case TemporalAaRectification::VarianceYCoCg:
            return "Variance Chroma";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr TemporalAaHistoryStorage
        ResolveHistoryStorageOverride(
            TemporalAaHistoryStorage costValue,
            TemporalAaHistoryStorageOverride overrideValue)
    {
        switch (overrideValue)
        {
        case TemporalAaHistoryStorageOverride::Robust:
            return TemporalAaHistoryStorage::Robust;
        case TemporalAaHistoryStorageOverride::Compact:
            return TemporalAaHistoryStorage::Compact;
        default:
            return costValue;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaDepthValidation
        ResolveDepthValidationOverride(
            TemporalAaDepthValidation costValue,
            TemporalAaDepthValidationOverride overrideValue)
    {
        switch (overrideValue)
        {
        case TemporalAaDepthValidationOverride::FourTexelFootprint:
            return TemporalAaDepthValidation::FourTexelFootprint;
        case TemporalAaDepthValidationOverride::MovingPoint:
            return TemporalAaDepthValidation::MovingPoint;
        default:
            return costValue;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaHistoryWeightPolicy
        ResolveHistoryWeightPolicyOverride(
            TemporalAaHistoryWeightPolicy costValue,
            TemporalAaHistoryWeightPolicyOverride overrideValue)
    {
        switch (overrideValue)
        {
        case TemporalAaHistoryWeightPolicyOverride::ConfidenceRecurrence:
            return TemporalAaHistoryWeightPolicy::ConfidenceRecurrence;
        case TemporalAaHistoryWeightPolicyOverride::ImmediateHorizon:
            return TemporalAaHistoryWeightPolicy::ImmediateHorizon;
        default:
            return costValue;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaMotionTrust
        ResolveMotionTrustOverride(
            TemporalAaMotionTrust costValue,
            TemporalAaMotionTrustOverride overrideValue)
    {
        switch (overrideValue)
        {
        case TemporalAaMotionTrustOverride::LinearSpeed:
            return TemporalAaMotionTrust::LinearSpeed;
        case TemporalAaMotionTrustOverride::SquaredSpeed:
            return TemporalAaMotionTrust::SquaredSpeed;
        default:
            return costValue;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaRectificationClip
        ResolveRectificationClipOverride(
            TemporalAaRectificationClip costValue,
            TemporalAaRectificationClipOverride overrideValue)
    {
        switch (overrideValue)
        {
        case TemporalAaRectificationClipOverride::VelocityDilatedLine:
            return TemporalAaRectificationClip::VelocityDilatedLine;
        case TemporalAaRectificationClipOverride::TightComponent:
            return TemporalAaRectificationClip::TightComponent;
        default:
            return costValue;
        }
    }

    [[nodiscard]] inline constexpr TemporalAaBlendDomain
        ResolveBlendDomainOverride(
            TemporalAaBlendDomain costValue,
            TemporalAaBlendDomainOverride overrideValue)
    {
        switch (overrideValue)
        {
        case TemporalAaBlendDomainOverride::LuminanceCompressed:
            return TemporalAaBlendDomain::LuminanceCompressed;
        case TemporalAaBlendDomainOverride::LinearRgb:
            return TemporalAaBlendDomain::LinearRgb;
        default:
            return costValue;
        }
    }

    [[nodiscard]] inline constexpr uint32_t
        GetTemporalAaBehaviorFlags(
            TemporalAaDepthValidation depthValidation,
            TemporalAaHistoryWeightPolicy historyWeight,
            TemporalAaMotionTrust motionTrust,
            TemporalAaRectificationClip rectificationClip,
            TemporalAaBlendDomain blendDomain)
    {
        return
            (depthValidation == TemporalAaDepthValidation::MovingPoint
                ? UVSR_TAA_BEHAVIOR_MOVING_POINT_DEPTH
                : 0u) |
            (historyWeight ==
                    TemporalAaHistoryWeightPolicy::ImmediateHorizon
                ? UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT
                : 0u) |
            (motionTrust == TemporalAaMotionTrust::SquaredSpeed
                ? UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST
                : 0u) |
            (rectificationClip ==
                    TemporalAaRectificationClip::TightComponent
                ? UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION
                : 0u) |
            (blendDomain == TemporalAaBlendDomain::LinearRgb
                ? UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN
                : 0u);
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaCostModeLabel(TemporalAaCostMode value)
    {
        switch (value)
        {
        case TemporalAaCostMode::FullQuality:
            return "Full Quality";
        case TemporalAaCostMode::Reduced:
            return "Reduced";
        case TemporalAaCostMode::Minimum:
            return "Minimum";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetAntiAliasingPresetLabel(AntiAliasingPreset value)
    {
        switch (value)
        {
        case AntiAliasingPreset::Off:
            return "Off";
        case AntiAliasingPreset::TemporalPerformance:
            return "Temporal Low";
        case AntiAliasingPreset::TemporalBalanced:
            return "Temporal Medium";
        case AntiAliasingPreset::TemporalQuality:
            return "Temporal High";
        case AntiAliasingPreset::TemporalUltra:
            return "Temporal Ultra";
        case AntiAliasingPreset::IntelCmaa2:
            return "Intel CMAA2";
        case AntiAliasingPreset::Msaa2x:
            return "MSAA 2x";
        case AntiAliasingPreset::Msaa4x:
            return "MSAA 4x";
        case AntiAliasingPreset::Msaa8x:
            return "MSAA 8x";
        case AntiAliasingPreset::Msaa16x:
            return "MSAA 16x";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetAntiAliasingMethodLabel(AntiAliasingMethod value)
    {
        switch (value)
        {
        case AntiAliasingMethod::TemporalSubpixelMorphological:
            return "Temporal Reconstructive";
        case AntiAliasingMethod::IntelCmaa2:
            return "Conservative Morphological";
        case AntiAliasingMethod::Msaa:
            return "Multisample Reference";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetAntiAliasingQualityLabel(AntiAliasingQuality value)
    {
        switch (value)
        {
        case AntiAliasingQuality::Low: return "Low";
        case AntiAliasingQuality::Medium: return "Medium";
        case AntiAliasingQuality::High: return "High";
        case AntiAliasingQuality::Ultra: return "Ultra";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetAntiAliasingQualityMenuLabel(
            AntiAliasingMethod method,
            AntiAliasingQuality quality)
    {
        if (method == AntiAliasingMethod::Msaa)
        {
            switch (quality)
            {
            case AntiAliasingQuality::Low: return "Low (2x)";
            case AntiAliasingQuality::Medium: return "Medium (4x)";
            case AntiAliasingQuality::High: return "High (8x)";
            case AntiAliasingQuality::Ultra: return "Ultra (16x)";
            default: return "Unavailable";
            }
        }
        return GetAntiAliasingQualityLabel(quality);
    }


    [[nodiscard]] inline constexpr const char*
        GetActiveAntiAliasingPresetLabel(
            const AntiAliasingSettings& settings)
    {
        return IsAntiAliasingPresetCustom(settings)
            ? "Custom"
            : GetAntiAliasingMethodLabel(settings.method);
    }

    [[nodiscard]] inline constexpr const char*
        GetMorphologyApplicationLabel(MorphologyApplication value)
    {
        switch (value)
        {
        case MorphologyApplication::Off: return "No Pixels";
        case MorphologyApplication::ConservativeMorphological:
            return "Conservative Morphological";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaMotionSourceOverrideLabel(
            TemporalAaMotionSourceOverride value)
    {
        switch (value)
        {
        case TemporalAaMotionSourceOverride::FromPreset:
            return "Preset";
        case TemporalAaMotionSourceOverride::Center:
            return "Center";
        case TemporalAaMotionSourceOverride::ClosestCross:
            return "Closest Cross";
        case TemporalAaMotionSourceOverride::CenterFirstEdgeDilation:
            return "Edge Dilation";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaCurrentReconstructionOverrideLabel(
            TemporalAaCurrentReconstructionOverride value)
    {
        switch (value)
        {
        case TemporalAaCurrentReconstructionOverride::FromPreset:
            return "Preset";
        case TemporalAaCurrentReconstructionOverride::Direct:
            return "Direct";
        case TemporalAaCurrentReconstructionOverride::DeJittered:
            return "De-Jittered";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaHistoryFilterOverrideLabel(
            TemporalAaHistoryFilterOverride value)
    {
        switch (value)
        {
        case TemporalAaHistoryFilterOverride::FromPreset:
            return "Preset";
        case TemporalAaHistoryFilterOverride::Bilinear:
            return "1x Bilinear";
        case TemporalAaHistoryFilterOverride::OneSampleBicubic:
            return "1x Bicubic";
        case TemporalAaHistoryFilterOverride::FiveTapCatmullRom:
            return "5x Bicubic";
        case TemporalAaHistoryFilterOverride::NineTapCatmullRom:
            return "9x Bicubic";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaRectificationOverrideLabel(
            TemporalAaRectificationOverride value)
    {
        switch (value)
        {
        case TemporalAaRectificationOverride::FromPreset:
            return "Preset";
        case TemporalAaRectificationOverride::PairRgb:
            return "Pair Tristimulus";
        case TemporalAaRectificationOverride::VarianceYCoCg:
            return "Variance Chroma";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMorphologyApplicationOverrideLabel(
            MorphologyApplicationOverride value)
    {
        switch (value)
        {
        case MorphologyApplicationOverride::FromPreset:
            return "Preset";
        case MorphologyApplicationOverride::Off:
            return "No Pixels";
        case MorphologyApplicationOverride::ConservativeMorphological:
            return "Conservative Morphological";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaSampleResurrectionLabel(
            TemporalAaSampleResurrection value)
    {
        switch (value)
        {
        case TemporalAaSampleResurrection::Off:
            return "No Resurrection";
        case TemporalAaSampleResurrection::OneOlderFrame:
            return "1x Frame";
        case TemporalAaSampleResurrection::TwoOlderFrames:
            return "2x Frames";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaSampleResurrectionOverrideLabel(
            TemporalAaSampleResurrectionOverride value)
    {
        switch (value)
        {
        case TemporalAaSampleResurrectionOverride::FromPreset:
            return "Preset";
        case TemporalAaSampleResurrectionOverride::Off:
            return "No Resurrection";
        case TemporalAaSampleResurrectionOverride::OneOlderFrame:
            return "1x Frame";
        case TemporalAaSampleResurrectionOverride::TwoOlderFrames:
            return "2x Frames";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaHistoryStorageLabel(
            TemporalAaHistoryStorage value)
    {
        switch (value)
        {
        case TemporalAaHistoryStorage::Robust:
            return "Robust RGBA16 + R32";
        case TemporalAaHistoryStorage::Compact:
            return "Compact R11/R16 Preferred";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaHistoryStorageOverrideLabel(
            TemporalAaHistoryStorageOverride value)
    {
        switch (value)
        {
        case TemporalAaHistoryStorageOverride::FromTemporalCost:
            return "Temporal Cost";
        case TemporalAaHistoryStorageOverride::Robust:
            return "Robust RGBA16 + R32";
        case TemporalAaHistoryStorageOverride::Compact:
            return "Compact R11/R16 Preferred";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaDepthValidationLabel(
            TemporalAaDepthValidation value)
    {
        switch (value)
        {
        case TemporalAaDepthValidation::FourTexelFootprint:
            return "Four-Texel Footprint";
        case TemporalAaDepthValidation::MovingPoint:
            return "Stationary Bypass";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaDepthValidationOverrideLabel(
            TemporalAaDepthValidationOverride value)
    {
        switch (value)
        {
        case TemporalAaDepthValidationOverride::FromTemporalCost:
            return "Temporal Cost";
        case TemporalAaDepthValidationOverride::FourTexelFootprint:
            return "Four-Texel Footprint";
        case TemporalAaDepthValidationOverride::MovingPoint:
            return "Stationary Bypass";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaHistoryWeightPolicyLabel(
            TemporalAaHistoryWeightPolicy value)
    {
        switch (value)
        {
        case TemporalAaHistoryWeightPolicy::ConfidenceRecurrence:
            return "Confidence Recurrence";
        case TemporalAaHistoryWeightPolicy::ImmediateHorizon:
            return "Immediate Horizon";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaHistoryWeightPolicyOverrideLabel(
            TemporalAaHistoryWeightPolicyOverride value)
    {
        switch (value)
        {
        case TemporalAaHistoryWeightPolicyOverride::FromTemporalCost:
            return "Temporal Cost";
        case TemporalAaHistoryWeightPolicyOverride::ConfidenceRecurrence:
            return "Confidence Recurrence";
        case TemporalAaHistoryWeightPolicyOverride::ImmediateHorizon:
            return "Immediate Horizon";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaMotionTrustLabel(TemporalAaMotionTrust value)
    {
        switch (value)
        {
        case TemporalAaMotionTrust::LinearSpeed:
            return "Linear Speed";
        case TemporalAaMotionTrust::SquaredSpeed:
            return "Squared Speed";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaMotionTrustOverrideLabel(
            TemporalAaMotionTrustOverride value)
    {
        switch (value)
        {
        case TemporalAaMotionTrustOverride::FromTemporalCost:
            return "Temporal Cost";
        case TemporalAaMotionTrustOverride::LinearSpeed:
            return "Linear Speed";
        case TemporalAaMotionTrustOverride::SquaredSpeed:
            return "Squared Speed";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaRectificationClipLabel(
            TemporalAaRectificationClip value)
    {
        switch (value)
        {
        case TemporalAaRectificationClip::VelocityDilatedLine:
            return "Velocity-Dilated Line";
        case TemporalAaRectificationClip::TightComponent:
            return "Tight Component";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaRectificationClipOverrideLabel(
            TemporalAaRectificationClipOverride value)
    {
        switch (value)
        {
        case TemporalAaRectificationClipOverride::FromTemporalCost:
            return "Temporal Cost";
        case TemporalAaRectificationClipOverride::VelocityDilatedLine:
            return "Velocity-Dilated Line";
        case TemporalAaRectificationClipOverride::TightComponent:
            return "Tight Component";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaBlendDomainLabel(TemporalAaBlendDomain value)
    {
        switch (value)
        {
        case TemporalAaBlendDomain::LuminanceCompressed:
            return "Luminance-Compressed";
        case TemporalAaBlendDomain::LinearRgb:
            return "Linear RGB";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaBlendDomainOverrideLabel(
            TemporalAaBlendDomainOverride value)
    {
        switch (value)
        {
        case TemporalAaBlendDomainOverride::FromTemporalCost:
            return "Temporal Cost";
        case TemporalAaBlendDomainOverride::LuminanceCompressed:
            return "Luminance-Compressed";
        case TemporalAaBlendDomainOverride::LinearRgb:
            return "Linear RGB";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaExecutionPathLabel(
            TemporalAaExecutionPath value)
    {
        switch (value)
        {
        case TemporalAaExecutionPath::Auto: return "Auto";
        case TemporalAaExecutionPath::Compute: return "Compute";
        case TemporalAaExecutionPath::FullscreenPixelShader:
            return "Fullscreen Pixel Shader";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaComputeKernelLabel(
            TemporalAaComputeKernel value)
    {
        switch (value)
        {
        case TemporalAaComputeKernel::Auto: return "Auto";
        case TemporalAaComputeKernel::Threads8x8TwoPixels:
            return "8x8 Threads";
        case TemporalAaComputeKernel::Threads16x8OnePixel:
            return "16x8 Threads, 1 Pixel per Thread";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaLdsLayoutLabel(TemporalAaLdsLayout value)
    {
        switch (value)
        {
        case TemporalAaLdsLayout::Auto: return "Auto";
        case TemporalAaLdsLayout::Legacy: return "Legacy";
        case TemporalAaLdsLayout::Split: return "Split";
        case TemporalAaLdsLayout::SplitAndPacked:
            return "Split and Packed";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaAutoToggleLabel(TemporalAaAutoToggle value)
    {
        switch (value)
        {
        case TemporalAaAutoToggle::Auto: return "Auto";
        case TemporalAaAutoToggle::Off: return "Off";
        case TemporalAaAutoToggle::On: return "On";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaPassFusionLabel(TemporalAaPassFusion value)
    {
        switch (value)
        {
        case TemporalAaPassFusion::Auto: return "Auto";
        case TemporalAaPassFusion::Separate: return "Separate";
        case TemporalAaPassFusion::Fused: return "Fused";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetTemporalAaCacheBlockingLabel(
            TemporalAaCacheBlocking value)
    {
        switch (value)
        {
        case TemporalAaCacheBlocking::Auto: return "Auto";
        case TemporalAaCacheBlocking::Off: return "Off";
        case TemporalAaCacheBlocking::Bands2: return "2 Bands";
        case TemporalAaCacheBlocking::Bands3: return "3 Bands";
        case TemporalAaCacheBlocking::Bands4: return "4 Bands";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char* GetTemporalAaDebugViewLabel(
        TemporalAaDebugView value)
    {
        switch (value)
        {
        case TemporalAaDebugView::Off: return "Off";
        case TemporalAaDebugView::FinalHistoryWeight:
            return "Final History Weight";
        case TemporalAaDebugView::SampleResurrection:
            return "Sample Resurrection";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr bool
        IsTemporalAaDebugVisualization(
            TemporalAaDebugView value)
    {
        const uint32_t index = static_cast<uint32_t>(value);
        return index > UVSR_TAA_DEBUG_OFF &&
            index < UVSR_TAA_DEBUG_VIEW_COUNT;
    }
}
