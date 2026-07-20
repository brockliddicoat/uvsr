#pragma once

#include "taa_miniengine_options_shared.h"

#include <array>
#include <cstdint>

namespace uvsr
{
    enum class AntiAliasingMethod : uint32_t
    {
        TemporalSubpixelMorphological,
        IntelCmaa2,
        Msaa,
        SubpixelMorphological,
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

    [[nodiscard]] inline constexpr bool IsAntiAliasingQualitySupported(
        AntiAliasingMethod method,
        AntiAliasingQuality quality)
    {
        if (method == AntiAliasingMethod::IntelCmaa2)
        {
            return quality != AntiAliasingQuality::Ultra;
        }
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
        Smaa1x,
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

    enum class SmaaApplication : uint32_t
    {
        Off,
        FullScreen,
        ConservativeMorphological,
        SelectiveShort,
        SelectiveLong,
        Count
    };

    [[nodiscard]] inline constexpr bool UsesSmaaApplication(
        SmaaApplication application)
    {
        return application == SmaaApplication::FullScreen ||
            application == SmaaApplication::SelectiveShort ||
            application == SmaaApplication::SelectiveLong;
    }

    enum class MiniEngineTaaMotionSource : uint32_t
    {
        Center = UVSR_TAA_MOTION_CENTER,
        ClosestCross = UVSR_TAA_MOTION_CLOSEST_CROSS,
        CenterFirstEdgeDilation =
            UVSR_TAA_MOTION_CENTER_FIRST_EDGE_DILATION,
        Count = UVSR_TAA_MOTION_SOURCE_COUNT
    };

    enum class MiniEngineTaaCurrentReconstruction : uint32_t
    {
        Direct = UVSR_TAA_CURRENT_DIRECT,
        DeJittered = UVSR_TAA_CURRENT_DEJITTERED,
        Count = UVSR_TAA_CURRENT_RECONSTRUCTION_COUNT
    };

    enum class MiniEngineTaaInteriorWeighting : uint32_t
    {
        Off = UVSR_TAA_INTERIOR_OFF,
        StableInterior = UVSR_TAA_INTERIOR_STABLE,
        Count = UVSR_TAA_INTERIOR_WEIGHTING_COUNT
    };

    enum class MiniEngineTaaHistoryFilter : uint32_t
    {
        Bilinear = UVSR_TAA_HISTORY_BILINEAR,
        OneSampleBicubic = UVSR_TAA_HISTORY_ONE_SAMPLE_BICUBIC,
        FiveTapCatmullRom = UVSR_TAA_HISTORY_FIVE_TAP_CATMULL_ROM,
        Count = UVSR_TAA_HISTORY_FILTER_COUNT
    };

    enum class MiniEngineTaaRectification : uint32_t
    {
        PairRgb = UVSR_TAA_RECTIFICATION_PAIR_RGB,
        PerPixelRgb = UVSR_TAA_RECTIFICATION_PER_PIXEL_RGB,
        PerPixelYCoCg = UVSR_TAA_RECTIFICATION_PER_PIXEL_YCOCG,
        VarianceYCoCg = UVSR_TAA_RECTIFICATION_VARIANCE_YCOCG,
        Count = UVSR_TAA_RECTIFICATION_COUNT
    };

    enum class MiniEngineTaaSampleResurrection : uint32_t
    {
        Off = UVSR_TAA_SAMPLE_RESURRECTION_OFF,
        OneOlderFrame =
            UVSR_TAA_SAMPLE_RESURRECTION_ONE_OLDER_FRAME,
        TwoOlderFrames =
            UVSR_TAA_SAMPLE_RESURRECTION_TWO_OLDER_FRAMES,
        Count = UVSR_TAA_SAMPLE_RESURRECTION_MODE_COUNT
    };

    enum class MiniEngineTaaMotionSourceOverride : uint32_t
    {
        FromPreset,
        Center,
        ClosestCross,
        CenterFirstEdgeDilation,
        Count
    };

    enum class MiniEngineTaaCurrentReconstructionOverride : uint32_t
    {
        FromPreset,
        Direct,
        DeJittered,
        Count
    };

    enum class MiniEngineTaaHistoryFilterOverride : uint32_t
    {
        FromPreset,
        Bilinear,
        OneSampleBicubic,
        FiveTapCatmullRom,
        Count
    };

    enum class MiniEngineTaaRectificationOverride : uint32_t
    {
        FromPreset,
        PairRgb,
        PerPixelRgb,
        PerPixelYCoCg,
        VarianceYCoCg,
        Count
    };

    enum class MiniEngineTaaStableInteriorOverride : uint32_t
    {
        FromPreset,
        Off,
        On,
        Count
    };

    enum class MiniEngineTaaSampleResurrectionOverride : uint32_t
    {
        FromPreset,
        Off,
        OneOlderFrame,
        TwoOlderFrames,
        Count
    };

    enum class SmaaApplicationOverride : uint32_t
    {
        FromPreset,
        Off,
        FullScreen,
        ConservativeMorphological,
        SelectiveShort,
        SelectiveLong,
        Count
    };

    enum class MiniEngineTaaExecutionPath : uint32_t
    {
        Auto,
        Compute,
        FullscreenPixelShader,
        Count
    };

    enum class MiniEngineTaaComputeKernel : uint32_t
    {
        Auto,
        Threads8x8TwoPixels,
        Threads16x8OnePixel,
        Count
    };

    enum class MiniEngineTaaLdsLayout : uint32_t
    {
        Auto,
        Legacy,
        Split,
        SplitAndPacked,
        Count
    };

    enum class MiniEngineTaaAutoToggle : uint32_t
    {
        Auto,
        Off,
        On,
        Count
    };

    enum class MiniEngineTaaPassFusion : uint32_t
    {
        Auto,
        Separate,
        Fused,
        Count
    };

    enum class MiniEngineTaaCacheBlocking : uint32_t
    {
        Auto,
        Off,
        Bands2,
        Bands3,
        Bands4,
        Count
    };

    enum class MiniEngineTaaDebugView : uint32_t
    {
        Off = UVSR_TAA_DEBUG_OFF,
        StableInterior = UVSR_TAA_DEBUG_STABLE_INTERIOR,
        FinalHistoryWeight = UVSR_TAA_DEBUG_FINAL_HISTORY_WEIGHT,
        SampleResurrection = UVSR_TAA_DEBUG_SAMPLE_RESURRECTION,
        SmaaEdgeMask = UVSR_SMAA_DEBUG_EDGE_MASK,
        SmaaBlendWeights = UVSR_SMAA_DEBUG_BLEND_WEIGHTS,
        SmaaOutputDelta = UVSR_SMAA_DEBUG_OUTPUT_DELTA,
        Count = UVSR_AA_DEBUG_VIEW_COUNT
    };

    struct MiniEngineTaaOptions
    {
        MiniEngineTaaMotionSource motionSource =
            MiniEngineTaaMotionSource::Center;
        MiniEngineTaaCurrentReconstruction currentReconstruction =
            MiniEngineTaaCurrentReconstruction::Direct;
        MiniEngineTaaInteriorWeighting interiorWeighting =
            MiniEngineTaaInteriorWeighting::Off;
        MiniEngineTaaHistoryFilter historyFilter =
            MiniEngineTaaHistoryFilter::Bilinear;
        MiniEngineTaaRectification rectification =
            MiniEngineTaaRectification::PairRgb;

        [[nodiscard]] constexpr bool operator==(
            const MiniEngineTaaOptions& other) const
        {
            return motionSource == other.motionSource &&
                   currentReconstruction == other.currentReconstruction &&
                   interiorWeighting == other.interiorWeighting &&
                   historyFilter == other.historyFilter &&
                   rectification == other.rectification;
        }

        [[nodiscard]] constexpr bool operator!=(
            const MiniEngineTaaOptions& other) const
        {
            return !(*this == other);
        }
    };

    struct MiniEngineTaaAlgorithmOverrides
    {
        MiniEngineTaaMotionSourceOverride motionSource =
            MiniEngineTaaMotionSourceOverride::FromPreset;
        MiniEngineTaaCurrentReconstructionOverride currentReconstruction =
            MiniEngineTaaCurrentReconstructionOverride::FromPreset;
        MiniEngineTaaHistoryFilterOverride historyFilter =
            MiniEngineTaaHistoryFilterOverride::FromPreset;
        MiniEngineTaaRectificationOverride rectification =
            MiniEngineTaaRectificationOverride::FromPreset;
        MiniEngineTaaStableInteriorOverride stableInterior =
            MiniEngineTaaStableInteriorOverride::FromPreset;
        MiniEngineTaaSampleResurrectionOverride sampleResurrection =
            MiniEngineTaaSampleResurrectionOverride::FromPreset;
        SmaaApplicationOverride subpixelMorphology =
            SmaaApplicationOverride::FromPreset;
        // -1 inherits the selected preset. Non-negative values are explicit
        // prior-frame horizons; the resolver clamps temporal presets to the
        // normal-menu slider's closed [1, 31] range.
        int32_t historyFrames = -1;
        // A negative value means that the selected preset owns the strength.
        // Non-negative values are an explicit image-quality override in the
        // closed [0, 1] interval.
        float historyStrength = -1.f;

        [[nodiscard]] constexpr bool IsCustom() const
        {
            return motionSource !=
                    MiniEngineTaaMotionSourceOverride::FromPreset ||
                currentReconstruction !=
                    MiniEngineTaaCurrentReconstructionOverride::FromPreset ||
                historyFilter !=
                    MiniEngineTaaHistoryFilterOverride::FromPreset ||
                rectification !=
                    MiniEngineTaaRectificationOverride::FromPreset ||
                stableInterior !=
                    MiniEngineTaaStableInteriorOverride::FromPreset ||
                sampleResurrection !=
                    MiniEngineTaaSampleResurrectionOverride::FromPreset ||
                subpixelMorphology !=
                    SmaaApplicationOverride::FromPreset ||
                historyFrames >= 0 ||
                historyStrength >= 0.f;
        }

        [[nodiscard]] constexpr bool operator==(
            const MiniEngineTaaAlgorithmOverrides& other) const
        {
            return motionSource == other.motionSource &&
                currentReconstruction ==
                    other.currentReconstruction &&
                historyFilter == other.historyFilter &&
                rectification == other.rectification &&
                stableInterior == other.stableInterior &&
                sampleResurrection == other.sampleResurrection &&
                subpixelMorphology == other.subpixelMorphology &&
                historyFrames == other.historyFrames &&
                historyStrength == other.historyStrength;
        }

        [[nodiscard]] constexpr bool operator!=(
            const MiniEngineTaaAlgorithmOverrides& other) const
        {
            return !(*this == other);
        }
    };

    struct MiniEngineTaaPerformanceOverrides
    {
        MiniEngineTaaExecutionPath executionPath =
            MiniEngineTaaExecutionPath::Auto;
        MiniEngineTaaComputeKernel computeKernel =
            MiniEngineTaaComputeKernel::Auto;
        MiniEngineTaaLdsLayout ldsLayout =
            MiniEngineTaaLdsLayout::Auto;
        MiniEngineTaaAutoToggle sharedWorkReuse =
            MiniEngineTaaAutoToggle::Auto;
        MiniEngineTaaAutoToggle earlyHistoryRejection =
            MiniEngineTaaAutoToggle::Auto;
        MiniEngineTaaPassFusion passFusion =
            MiniEngineTaaPassFusion::Auto;
        MiniEngineTaaCacheBlocking cacheBlocking =
            MiniEngineTaaCacheBlocking::Auto;

        [[nodiscard]] constexpr bool IsCustom() const
        {
            return executionPath != MiniEngineTaaExecutionPath::Auto ||
                computeKernel != MiniEngineTaaComputeKernel::Auto ||
                ldsLayout != MiniEngineTaaLdsLayout::Auto ||
                sharedWorkReuse != MiniEngineTaaAutoToggle::Auto ||
                earlyHistoryRejection != MiniEngineTaaAutoToggle::Auto ||
                passFusion != MiniEngineTaaPassFusion::Auto ||
                cacheBlocking != MiniEngineTaaCacheBlocking::Auto;
        }

        [[nodiscard]] constexpr bool operator==(
            const MiniEngineTaaPerformanceOverrides& other) const
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
            const MiniEngineTaaPerformanceOverrides& other) const
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
        MiniEngineTaaAlgorithmOverrides algorithmOverrides;
        MiniEngineTaaPerformanceOverrides performanceOverrides;
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
        MiniEngineTaaOptions temporal;
        SmaaApplication subpixelMorphology =
            SmaaApplication::SelectiveShort;
        MiniEngineTaaExecutionPath executionPath =
            MiniEngineTaaExecutionPath::Compute;
        MiniEngineTaaComputeKernel computeKernel =
            MiniEngineTaaComputeKernel::Threads8x8TwoPixels;
        MiniEngineTaaLdsLayout ldsLayout =
            MiniEngineTaaLdsLayout::Legacy;
        bool sharedWorkReuse = false;
        bool earlyHistoryRejection = false;
        MiniEngineTaaPassFusion passFusion =
            MiniEngineTaaPassFusion::Separate;
        MiniEngineTaaCacheBlocking cacheBlocking =
            MiniEngineTaaCacheBlocking::Off;
        MiniEngineTaaSampleResurrection sampleResurrection =
            MiniEngineTaaSampleResurrection::Off;
        // Logical temporal horizon, expressed as prior frames that may
        // influence the current result. This is intentionally distinct from
        // the shared two-slot physical ping-pong allocation.
        uint32_t historyFrames = 0u;
        // Normalized strength within MiniEngine TAA's horizon-derived
        // N/(N+1) maximum.
        float historyStrength = 0.f;
        uint32_t rasterSampleCount = 1u;
    };

    struct MiniEngineTaaStaticPerformanceOptions
    {
        MiniEngineTaaComputeKernel computeKernel =
            MiniEngineTaaComputeKernel::Threads8x8TwoPixels;
        MiniEngineTaaLdsLayout ldsLayout =
            MiniEngineTaaLdsLayout::Legacy;
        bool sharedWorkReuse = false;
        bool earlyHistoryRejection = false;
        bool fusedOutput = false;
    };

    inline constexpr uint32_t MiniEngineTaaStaticPerformanceCount =
        UVSR_TAA_KERNEL_COUNT *
        UVSR_TAA_LDS_LAYOUT_COUNT * 2u * 2u * 2u;

    [[nodiscard]] inline constexpr uint32_t
        GetMiniEngineTaaStaticPerformanceIndex(
            const MiniEngineTaaStaticPerformanceOptions& options)
    {
        uint32_t index =
            options.computeKernel ==
                MiniEngineTaaComputeKernel::Threads16x8OnePixel
                ? UVSR_TAA_KERNEL_16X8_ONE_PIXEL
                : UVSR_TAA_KERNEL_8X8_TWO_PIXELS;
        index = index * UVSR_TAA_LDS_LAYOUT_COUNT +
            (options.ldsLayout ==
                    MiniEngineTaaLdsLayout::SplitAndPacked
                ? UVSR_TAA_LDS_SPLIT_PACKED
                : options.ldsLayout ==
                        MiniEngineTaaLdsLayout::Split
                    ? UVSR_TAA_LDS_SPLIT
                    : UVSR_TAA_LDS_LEGACY);
        index = index * 2u + uint32_t(options.sharedWorkReuse);
        index = index * 2u +
            uint32_t(options.earlyHistoryRejection);
        index = index * 2u + uint32_t(options.fusedOutput);
        return index;
    }

    [[nodiscard]] inline constexpr
        MiniEngineTaaStaticPerformanceOptions
        GetMiniEngineTaaStaticPerformanceOptions(
            const ResolvedAntiAliasingSettings& settings,
            bool fusedOutput)
    {
        MiniEngineTaaStaticPerformanceOptions result;
        result.computeKernel = settings.computeKernel;
        result.ldsLayout = settings.ldsLayout;
        result.sharedWorkReuse = settings.sharedWorkReuse;
        result.earlyHistoryRejection =
            settings.earlyHistoryRejection;
        result.fusedOutput = fusedOutput;
        return result;
    }

    inline constexpr uint32_t MiniEngineTaaMotionSourceCount =
        UVSR_TAA_MOTION_SOURCE_COUNT;
    inline constexpr uint32_t MiniEngineTaaCurrentReconstructionCount =
        UVSR_TAA_CURRENT_RECONSTRUCTION_COUNT;
    inline constexpr uint32_t MiniEngineTaaInteriorWeightingCount =
        UVSR_TAA_INTERIOR_WEIGHTING_COUNT;
    inline constexpr uint32_t MiniEngineTaaHistoryFilterCount =
        UVSR_TAA_HISTORY_FILTER_COUNT;
    inline constexpr uint32_t MiniEngineTaaRectificationCount =
        UVSR_TAA_RECTIFICATION_COUNT;
    inline constexpr uint32_t MiniEngineTaaSampleResurrectionCount =
        UVSR_TAA_SAMPLE_RESURRECTION_MODE_COUNT;
    inline constexpr uint32_t MiniEngineTaaResolveDebugViewCount =
        UVSR_TAA_DEBUG_VIEW_COUNT;
    inline constexpr uint32_t MiniEngineTaaDebugViewCount =
        UVSR_AA_DEBUG_VIEW_COUNT;
    inline constexpr uint32_t MiniEngineTaaBlendPermutationCount =
        UVSR_TAA_BLEND_PERMUTATION_COUNT;

    static_assert(
        MiniEngineTaaMotionSourceCount *
            MiniEngineTaaCurrentReconstructionCount *
            MiniEngineTaaInteriorWeightingCount *
            MiniEngineTaaHistoryFilterCount *
            MiniEngineTaaRectificationCount ==
        MiniEngineTaaBlendPermutationCount);

    [[nodiscard]] inline constexpr uint32_t
        GetMiniEngineTaaBlendPermutationIndex(
            const MiniEngineTaaOptions& options)
    {
        uint32_t index = static_cast<uint32_t>(options.motionSource);
        index = index * MiniEngineTaaCurrentReconstructionCount +
            static_cast<uint32_t>(options.currentReconstruction);
        index = index * MiniEngineTaaInteriorWeightingCount +
            static_cast<uint32_t>(options.interiorWeighting);
        index = index * MiniEngineTaaHistoryFilterCount +
            static_cast<uint32_t>(options.historyFilter);
        index = index * MiniEngineTaaRectificationCount +
            static_cast<uint32_t>(options.rectification);
        return index;
    }

    [[nodiscard]] inline constexpr bool MiniEngineTaaOptionsRequireReset(
        const MiniEngineTaaOptions& active,
        const MiniEngineTaaOptions& requested)
    {
        return active != requested;
    }

    [[nodiscard]] inline constexpr AntiAliasingPreset
        GetAntiAliasingImplementation(
            AntiAliasingMethod method,
            AntiAliasingQuality quality)
    {
        if (method == AntiAliasingMethod::SubpixelMorphological)
            return AntiAliasingPreset::Smaa1x;
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
        MiniEngineTaaSampleResurrection mode)
    {
        return mode != MiniEngineTaaSampleResurrection::Off;
    }

    [[nodiscard]] inline constexpr SmaaApplication
        GetPresetSmaaApplication(AntiAliasingPreset preset)
    {
        switch (preset)
        {
        case AntiAliasingPreset::Smaa1x:
            return SmaaApplication::FullScreen;
        case AntiAliasingPreset::TemporalPerformance:
        case AntiAliasingPreset::TemporalBalanced:
            return SmaaApplication::SelectiveShort;
        case AntiAliasingPreset::TemporalQuality:
            return SmaaApplication::SelectiveLong;
        case AntiAliasingPreset::TemporalUltra:
            return SmaaApplication::FullScreen;
        case AntiAliasingPreset::IntelCmaa2:
            return SmaaApplication::ConservativeMorphological;
        case AntiAliasingPreset::Msaa2x:
        case AntiAliasingPreset::Msaa4x:
        case AntiAliasingPreset::Msaa8x:
        case AntiAliasingPreset::Msaa16x:
            return SmaaApplication::ConservativeMorphological;
        default:
            return SmaaApplication::Off;
        }
    }

    [[nodiscard]] inline constexpr bool
        IsMorphologyApplicationSupported(
            AntiAliasingPreset preset,
            SmaaApplication application)
    {
        if (preset == AntiAliasingPreset::Smaa1x)
        {
            return application == SmaaApplication::FullScreen;
        }
        if (preset == AntiAliasingPreset::IntelCmaa2)
        {
            return application ==
                SmaaApplication::ConservativeMorphological;
        }
        if (IsLongTermTemporalPreset(preset))
            return application < SmaaApplication::Count;
        if (preset == AntiAliasingPreset::Msaa2x ||
            preset == AntiAliasingPreset::Msaa4x ||
            preset == AntiAliasingPreset::Msaa8x ||
            preset == AntiAliasingPreset::Msaa16x)
        {
            return application == SmaaApplication::Off ||
                application == SmaaApplication::FullScreen ||
                application ==
                    SmaaApplication::ConservativeMorphological;
        }
        return application == SmaaApplication::Off;
    }

    [[nodiscard]] inline constexpr MiniEngineTaaOptions
        GetPresetTemporalOptions(AntiAliasingPreset preset)
    {
        MiniEngineTaaOptions result;
        switch (preset)
        {
        case AntiAliasingPreset::TemporalPerformance:
            result.motionSource = MiniEngineTaaMotionSource::Center;
            result.currentReconstruction =
                MiniEngineTaaCurrentReconstruction::Direct;
            result.historyFilter =
                MiniEngineTaaHistoryFilter::OneSampleBicubic;
            result.rectification =
                MiniEngineTaaRectification::PairRgb;
            result.interiorWeighting =
                MiniEngineTaaInteriorWeighting::Off;
            break;
        case AntiAliasingPreset::TemporalBalanced:
            result.motionSource =
                MiniEngineTaaMotionSource::CenterFirstEdgeDilation;
            result.currentReconstruction =
                MiniEngineTaaCurrentReconstruction::DeJittered;
            result.historyFilter =
                MiniEngineTaaHistoryFilter::OneSampleBicubic;
            result.rectification =
                MiniEngineTaaRectification::PerPixelYCoCg;
            result.interiorWeighting =
                MiniEngineTaaInteriorWeighting::Off;
            break;
        case AntiAliasingPreset::TemporalQuality:
        case AntiAliasingPreset::TemporalUltra:
            result.motionSource =
                MiniEngineTaaMotionSource::CenterFirstEdgeDilation;
            result.currentReconstruction =
                MiniEngineTaaCurrentReconstruction::DeJittered;
            result.historyFilter =
                MiniEngineTaaHistoryFilter::FiveTapCatmullRom;
            result.rectification =
                MiniEngineTaaRectification::VarianceYCoCg;
            result.interiorWeighting =
                MiniEngineTaaInteriorWeighting::Off;
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
            return 7u;
        case AntiAliasingPreset::TemporalQuality:
            return 15u;
        case AntiAliasingPreset::TemporalUltra:
            return 31u;
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
                value < 1 ? 1 : value > 31 ? 31 : value);
    }

    [[nodiscard]] inline constexpr MiniEngineTaaSampleResurrection
        GetPresetSampleResurrection(AntiAliasingPreset)
    {
        // Sample resurrection remains opt-in developer functionality until
        // its extra history layout and traffic have been validated.
        return MiniEngineTaaSampleResurrection::Off;
    }

    [[nodiscard]] inline constexpr MiniEngineTaaMotionSource
        ResolveMotionSourceOverride(
            MiniEngineTaaMotionSource preset,
            MiniEngineTaaMotionSourceOverride overrideValue)
    {
        switch (overrideValue)
        {
        case MiniEngineTaaMotionSourceOverride::Center:
            return MiniEngineTaaMotionSource::Center;
        case MiniEngineTaaMotionSourceOverride::ClosestCross:
            return MiniEngineTaaMotionSource::ClosestCross;
        case MiniEngineTaaMotionSourceOverride::CenterFirstEdgeDilation:
            return MiniEngineTaaMotionSource::CenterFirstEdgeDilation;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr MiniEngineTaaCurrentReconstruction
        ResolveCurrentReconstructionOverride(
            MiniEngineTaaCurrentReconstruction preset,
            MiniEngineTaaCurrentReconstructionOverride overrideValue)
    {
        switch (overrideValue)
        {
        case MiniEngineTaaCurrentReconstructionOverride::Direct:
            return MiniEngineTaaCurrentReconstruction::Direct;
        case MiniEngineTaaCurrentReconstructionOverride::DeJittered:
            return MiniEngineTaaCurrentReconstruction::DeJittered;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr MiniEngineTaaHistoryFilter
        ResolveHistoryFilterOverride(
            MiniEngineTaaHistoryFilter preset,
            MiniEngineTaaHistoryFilterOverride overrideValue)
    {
        switch (overrideValue)
        {
        case MiniEngineTaaHistoryFilterOverride::Bilinear:
            return MiniEngineTaaHistoryFilter::Bilinear;
        case MiniEngineTaaHistoryFilterOverride::OneSampleBicubic:
            return MiniEngineTaaHistoryFilter::OneSampleBicubic;
        case MiniEngineTaaHistoryFilterOverride::FiveTapCatmullRom:
            return MiniEngineTaaHistoryFilter::FiveTapCatmullRom;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr MiniEngineTaaRectification
        ResolveRectificationOverride(
            MiniEngineTaaRectification preset,
            MiniEngineTaaRectificationOverride overrideValue)
    {
        switch (overrideValue)
        {
        case MiniEngineTaaRectificationOverride::PairRgb:
            return MiniEngineTaaRectification::PairRgb;
        case MiniEngineTaaRectificationOverride::PerPixelRgb:
            return MiniEngineTaaRectification::PerPixelRgb;
        case MiniEngineTaaRectificationOverride::PerPixelYCoCg:
            return MiniEngineTaaRectification::PerPixelYCoCg;
        case MiniEngineTaaRectificationOverride::VarianceYCoCg:
            return MiniEngineTaaRectification::VarianceYCoCg;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr MiniEngineTaaInteriorWeighting
        ResolveStableInteriorOverride(
            MiniEngineTaaInteriorWeighting preset,
            MiniEngineTaaStableInteriorOverride overrideValue)
    {
        switch (overrideValue)
        {
        case MiniEngineTaaStableInteriorOverride::Off:
            return MiniEngineTaaInteriorWeighting::Off;
        case MiniEngineTaaStableInteriorOverride::On:
            return MiniEngineTaaInteriorWeighting::StableInterior;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr MiniEngineTaaSampleResurrection
        ResolveSampleResurrectionOverride(
            MiniEngineTaaSampleResurrection preset,
            MiniEngineTaaSampleResurrectionOverride overrideValue)
    {
        switch (overrideValue)
        {
        case MiniEngineTaaSampleResurrectionOverride::Off:
            return MiniEngineTaaSampleResurrection::Off;
        case MiniEngineTaaSampleResurrectionOverride::OneOlderFrame:
            return MiniEngineTaaSampleResurrection::OneOlderFrame;
        case MiniEngineTaaSampleResurrectionOverride::TwoOlderFrames:
            return MiniEngineTaaSampleResurrection::TwoOlderFrames;
        default:
            return preset;
        }
    }

    [[nodiscard]] inline constexpr SmaaApplication
        ResolveSmaaApplicationOverride(
            SmaaApplication preset,
            SmaaApplicationOverride overrideValue)
    {
        switch (overrideValue)
        {
        case SmaaApplicationOverride::Off:
            return SmaaApplication::Off;
        case SmaaApplicationOverride::FullScreen:
            return SmaaApplication::FullScreen;
        case SmaaApplicationOverride::ConservativeMorphological:
            return SmaaApplication::ConservativeMorphological;
        case SmaaApplicationOverride::SelectiveShort:
            return SmaaApplication::SelectiveShort;
        case SmaaApplicationOverride::SelectiveLong:
            return SmaaApplication::SelectiveLong;
        default:
            return preset;
        }
    }

    // Auto is intentionally conservative until matched target-adapter
    // measurements promote an optimization. The present MiniEngine 8x8
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
        result.implementation = settings.enabled
            ? implementation
            : AntiAliasingPreset::Off;
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
                : requestedHistoryStrength < 0.f
                    ? 0.f
                    : requestedHistoryStrength > 1.f
                        ? 1.f
                        : requestedHistoryStrength;
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
        result.temporal.interiorWeighting =
            ResolveStableInteriorOverride(
                result.temporal.interiorWeighting,
                settings.algorithmOverrides.stableInterior);

        // Spatial SMAA 1x and standalone CMAA2 are complete methods, not
        // presentation modes. An override left over
        // from a combined Temporal or MSAA configuration must not replace
        // their defining morphology.
        result.subpixelMorphology =
            implementation == AntiAliasingPreset::Smaa1x
                ? SmaaApplication::FullScreen
                : implementation == AntiAliasingPreset::IntelCmaa2
                    ? SmaaApplication::ConservativeMorphological
                : ResolveSmaaApplicationOverride(
                    GetPresetSmaaApplication(implementation),
                    settings.algorithmOverrides.subpixelMorphology);
        // Selective SMAA consumes the long-term temporal rejection mask.
        // MSAA has no equivalent history classification, but full-screen SMAA
        // and CMAA2 are valid post-resolve choices.
        if (!IsMorphologyApplicationSupported(
                implementation,
                result.subpixelMorphology))
        {
            result.subpixelMorphology =
                GetPresetSmaaApplication(implementation);
        }
        result.executionPath =
            settings.performanceOverrides.executionPath ==
                    MiniEngineTaaExecutionPath::Auto
                ? MiniEngineTaaExecutionPath::Compute
                : settings.performanceOverrides.executionPath;
        result.computeKernel =
            settings.performanceOverrides.computeKernel ==
                    MiniEngineTaaComputeKernel::Auto
                ? MiniEngineTaaComputeKernel::Threads8x8TwoPixels
                : settings.performanceOverrides.computeKernel;
        result.ldsLayout =
            settings.performanceOverrides.ldsLayout ==
                    MiniEngineTaaLdsLayout::Auto
                ? MiniEngineTaaLdsLayout::Legacy
                : settings.performanceOverrides.ldsLayout;
        result.sharedWorkReuse =
            settings.performanceOverrides.sharedWorkReuse ==
                MiniEngineTaaAutoToggle::On;
        result.earlyHistoryRejection =
            settings.performanceOverrides.earlyHistoryRejection ==
                MiniEngineTaaAutoToggle::On;
        result.passFusion =
            settings.performanceOverrides.passFusion ==
                    MiniEngineTaaPassFusion::Auto
                ? MiniEngineTaaPassFusion::Separate
                : settings.performanceOverrides.passFusion;
        result.cacheBlocking =
            settings.performanceOverrides.cacheBlocking ==
                    MiniEngineTaaCacheBlocking::Auto
                ? MiniEngineTaaCacheBlocking::Off
                : settings.performanceOverrides.cacheBlocking;
        result.sampleResurrection =
            IsLongTermTemporalPreset(implementation)
                ? ResolveSampleResurrectionOverride(
                    GetPresetSampleResurrection(implementation),
                    settings.algorithmOverrides.sampleResurrection)
                : MiniEngineTaaSampleResurrection::Off;

        // Resurrection has a different history layout and has only been
        // validated on the MiniEngine baseline compute implementation.
        // Prevent experimental execution overrides from silently selecting a
        // permutation that cannot consume the older-frame resources.
        if (UsesSampleResurrection(result.sampleResurrection))
        {
            result.executionPath = MiniEngineTaaExecutionPath::Compute;
            result.computeKernel =
                MiniEngineTaaComputeKernel::Threads8x8TwoPixels;
            result.ldsLayout = MiniEngineTaaLdsLayout::Legacy;
            result.sharedWorkReuse = false;
            result.earlyHistoryRejection = false;
            result.cacheBlocking = MiniEngineTaaCacheBlocking::Off;
        }
#if !UVSR_AA_DEVELOPER_OVERRIDES
        // Production contains only the deterministic shipping topology.
        // Image-equivalent experiments stay developer-only until a repeated
        // adapter benchmark promotes them.
        result.passFusion = MiniEngineTaaPassFusion::Separate;
#endif
        return result;
    }

    // Resolve only settings backed by the shader/PSO bundle compiled into the
    // current build. Developer builds retain the complete experiment matrix.
    // Production builds deliberately discard hidden override state before it
    // reaches PSO selection, so a stale config or programmatic caller cannot
    // request an uncompiled permutation and silently bypass anti-aliasing.
    [[nodiscard]] inline constexpr AntiAliasingSettings
        GetCompiledAntiAliasingSettings(
            const AntiAliasingSettings& settings)
    {
#if UVSR_AA_DEVELOPER_OVERRIDES
        return settings;
#else
        AntiAliasingSettings compiled = settings;
        const int32_t historyFrames =
            compiled.algorithmOverrides.historyFrames;
        const float historyStrength =
            compiled.algorithmOverrides.historyStrength;
        compiled.algorithmOverrides =
            MiniEngineTaaAlgorithmOverrides{};
        // History horizon and strength are ordinary user controls backed by
        // existing runtime constants. Preserve them in production; the
        // remaining algorithm fields can request developer-only PSOs.
        compiled.algorithmOverrides.historyFrames = historyFrames;
        compiled.algorithmOverrides.historyStrength = historyStrength;
        compiled.performanceOverrides =
            MiniEngineTaaPerformanceOverrides{};
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
            MiniEngineTaaAlgorithmOverrides{};
        presetSettings.performanceOverrides =
            MiniEngineTaaPerformanceOverrides{};
        const ResolvedAntiAliasingSettings requested =
            ResolveAntiAliasingSettings(settings);
        const ResolvedAntiAliasingSettings preset =
            ResolveAntiAliasingSettings(presetSettings);
        const AntiAliasingPreset implementation =
            GetAntiAliasingImplementation(
                settings.method,
                settings.quality);
        const bool miniEnginePerformanceChanged =
            requested.executionPath != preset.executionPath ||
            requested.computeKernel != preset.computeKernel ||
            requested.ldsLayout != preset.ldsLayout ||
            requested.sharedWorkReuse != preset.sharedWorkReuse ||
            requested.earlyHistoryRejection !=
                preset.earlyHistoryRejection ||
            requested.passFusion != preset.passFusion ||
            requested.cacheBlocking != preset.cacheBlocking;

        if (implementation == AntiAliasingPreset::Smaa1x)
        {
            return false;
        }
        if (IsLongTermTemporalPreset(implementation))
        {
            return requested.temporal != preset.temporal ||
                requested.subpixelMorphology !=
                    preset.subpixelMorphology ||
                requested.sampleResurrection !=
                    preset.sampleResurrection ||
                requested.historyFrames != preset.historyFrames ||
                requested.historyStrength !=
                    preset.historyStrength ||
                miniEnginePerformanceChanged;
        }
        if (implementation == AntiAliasingPreset::Msaa2x ||
            implementation == AntiAliasingPreset::Msaa4x ||
            implementation == AntiAliasingPreset::Msaa8x ||
            implementation == AntiAliasingPreset::Msaa16x)
        {
            return requested.subpixelMorphology !=
                preset.subpixelMorphology;
        }
        return false;
    }

    enum class TemporalAntiAliasingTechnique : uint32_t
    {
        None,
        MiniEngineTaa
    };

    struct TemporalAntiAliasingImageKey
    {
        TemporalAntiAliasingTechnique technique =
            TemporalAntiAliasingTechnique::None;
        MiniEngineTaaOptions temporal;
        MiniEngineTaaSampleResurrection sampleResurrection =
            MiniEngineTaaSampleResurrection::Off;
        uint32_t historyFrames = 0u;
        float historyStrength = 0.f;

        [[nodiscard]] constexpr bool operator==(
            const TemporalAntiAliasingImageKey& other) const
        {
            if (technique != other.technique)
                return false;
            if (technique ==
                TemporalAntiAliasingTechnique::MiniEngineTaa)
            {
                return temporal == other.temporal &&
                    sampleResurrection ==
                        other.sampleResurrection &&
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
                TemporalAntiAliasingTechnique::MiniEngineTaa;
            result.temporal = settings.temporal;
            result.sampleResurrection =
                settings.sampleResurrection;
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

        // The effective image key intentionally excludes spatial SMAA 1x,
        // presentation-only SMAA, debug views, sharpness, and every
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
        GetMiniEngineTaaHistoryColorSampleCount(
            MiniEngineTaaHistoryFilter filter)
    {
        return filter == MiniEngineTaaHistoryFilter::FiveTapCatmullRom
            ? 5u
            : 1u;
    }

    [[nodiscard]] inline constexpr uint32_t
        GetMiniEngineTaaHistoryMomentSampleCount(
            MiniEngineTaaInteriorWeighting weighting)
    {
        return weighting ==
                MiniEngineTaaInteriorWeighting::StableInterior
            ? 1u
            : 0u;
    }

    [[nodiscard]] inline constexpr uint32_t
        GetMiniEngineTaaHistoryDepthSampleCount(
            MiniEngineTaaHistoryFilter filter)
    {
        // Every mode retains the central four-texel Gather. Five-Tap alone
        // adds one discrete Gather for each of its four outer color taps.
        return filter == MiniEngineTaaHistoryFilter::FiveTapCatmullRom
            ? 4u
            : 0u;
    }

    [[nodiscard]] inline constexpr const char* GetMiniEngineTaaMotionSourceLabel(
        MiniEngineTaaMotionSource value)
    {
        switch (value)
        {
        case MiniEngineTaaMotionSource::Center: return "Center";
        case MiniEngineTaaMotionSource::ClosestCross: return "Closest Cross";
        case MiniEngineTaaMotionSource::CenterFirstEdgeDilation:
            return "Edge Dilation";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaCurrentReconstructionLabel(
            MiniEngineTaaCurrentReconstruction value)
    {
        switch (value)
        {
        case MiniEngineTaaCurrentReconstruction::Direct: return "Direct";
        case MiniEngineTaaCurrentReconstruction::DeJittered:
            return "De-Jittered";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaInteriorWeightingLabel(
            MiniEngineTaaInteriorWeighting value)
    {
        switch (value)
        {
        case MiniEngineTaaInteriorWeighting::Off: return "Off";
        case MiniEngineTaaInteriorWeighting::StableInterior:
            return "Stable Interior";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char* GetMiniEngineTaaHistoryFilterLabel(
        MiniEngineTaaHistoryFilter value)
    {
        switch (value)
        {
        case MiniEngineTaaHistoryFilter::Bilinear: return "1x Bilinear";
        case MiniEngineTaaHistoryFilter::OneSampleBicubic:
            return "1x Bicubic";
        case MiniEngineTaaHistoryFilter::FiveTapCatmullRom:
            return "5x Bicubic";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaRectificationLabel(MiniEngineTaaRectification value)
    {
        switch (value)
        {
        case MiniEngineTaaRectification::PairRgb:
            return "Pair Tristimulus";
        case MiniEngineTaaRectification::PerPixelRgb:
            return "Per-Pixel Tristimulus";
        case MiniEngineTaaRectification::PerPixelYCoCg:
            return "Per-Pixel Chroma";
        case MiniEngineTaaRectification::VarianceYCoCg:
            return "Variance Chroma";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetAntiAliasingPresetLabel(AntiAliasingPreset value)
    {
        switch (value)
        {
        case AntiAliasingPreset::Off:
            return "Off";
        case AntiAliasingPreset::Smaa1x:
            return "Subpixel Morphological";
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
        case AntiAliasingMethod::SubpixelMorphological:
            return "Subpixel Morphological";
        case AntiAliasingMethod::TemporalSubpixelMorphological:
            return "Temporal";
        case AntiAliasingMethod::IntelCmaa2:
            return "Conservative Morphological";
        case AntiAliasingMethod::Msaa:
            return "Multisample";
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
        GetSmaaApplicationLabel(SmaaApplication value)
    {
        switch (value)
        {
        case SmaaApplication::Off: return "No Pixels";
        case SmaaApplication::FullScreen: return "All Pixels";
        case SmaaApplication::ConservativeMorphological:
            return "Conservative Morphological";
        case SmaaApplication::SelectiveShort:
            return "Selective Short Search";
        case SmaaApplication::SelectiveLong:
            return "Selective Long Search";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaMotionSourceOverrideLabel(
            MiniEngineTaaMotionSourceOverride value)
    {
        switch (value)
        {
        case MiniEngineTaaMotionSourceOverride::FromPreset:
            return "Preset";
        case MiniEngineTaaMotionSourceOverride::Center:
            return "Center";
        case MiniEngineTaaMotionSourceOverride::ClosestCross:
            return "Closest Cross";
        case MiniEngineTaaMotionSourceOverride::CenterFirstEdgeDilation:
            return "Edge Dilation";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaCurrentReconstructionOverrideLabel(
            MiniEngineTaaCurrentReconstructionOverride value)
    {
        switch (value)
        {
        case MiniEngineTaaCurrentReconstructionOverride::FromPreset:
            return "Preset";
        case MiniEngineTaaCurrentReconstructionOverride::Direct:
            return "Direct";
        case MiniEngineTaaCurrentReconstructionOverride::DeJittered:
            return "De-Jittered";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaHistoryFilterOverrideLabel(
            MiniEngineTaaHistoryFilterOverride value)
    {
        switch (value)
        {
        case MiniEngineTaaHistoryFilterOverride::FromPreset:
            return "Preset";
        case MiniEngineTaaHistoryFilterOverride::Bilinear:
            return "1x Bilinear";
        case MiniEngineTaaHistoryFilterOverride::OneSampleBicubic:
            return "1x Bicubic";
        case MiniEngineTaaHistoryFilterOverride::FiveTapCatmullRom:
            return "5x Bicubic";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaRectificationOverrideLabel(
            MiniEngineTaaRectificationOverride value)
    {
        switch (value)
        {
        case MiniEngineTaaRectificationOverride::FromPreset:
            return "Preset";
        case MiniEngineTaaRectificationOverride::PairRgb:
            return "Pair Tristimulus";
        case MiniEngineTaaRectificationOverride::PerPixelRgb:
            return "Per-Pixel Tristimulus";
        case MiniEngineTaaRectificationOverride::PerPixelYCoCg:
            return "Per-Pixel Chroma";
        case MiniEngineTaaRectificationOverride::VarianceYCoCg:
            return "Variance Chroma";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetSmaaApplicationOverrideLabel(SmaaApplicationOverride value)
    {
        switch (value)
        {
        case SmaaApplicationOverride::FromPreset:
            return "Preset";
        case SmaaApplicationOverride::Off:
            return "No Pixels";
        case SmaaApplicationOverride::FullScreen:
            return "All Pixels";
        case SmaaApplicationOverride::ConservativeMorphological:
            return "Conservative Morphological";
        case SmaaApplicationOverride::SelectiveShort:
            return "Selective Short Search";
        case SmaaApplicationOverride::SelectiveLong:
            return "Selective Long Search";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaSampleResurrectionLabel(
            MiniEngineTaaSampleResurrection value)
    {
        switch (value)
        {
        case MiniEngineTaaSampleResurrection::Off:
            return "No Resurrection";
        case MiniEngineTaaSampleResurrection::OneOlderFrame:
            return "1x Frame";
        case MiniEngineTaaSampleResurrection::TwoOlderFrames:
            return "2x Frames";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaSampleResurrectionOverrideLabel(
            MiniEngineTaaSampleResurrectionOverride value)
    {
        switch (value)
        {
        case MiniEngineTaaSampleResurrectionOverride::FromPreset:
            return "Preset";
        case MiniEngineTaaSampleResurrectionOverride::Off:
            return "No Resurrection";
        case MiniEngineTaaSampleResurrectionOverride::OneOlderFrame:
            return "1x Frame";
        case MiniEngineTaaSampleResurrectionOverride::TwoOlderFrames:
            return "2x Frames";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaExecutionPathLabel(
            MiniEngineTaaExecutionPath value)
    {
        switch (value)
        {
        case MiniEngineTaaExecutionPath::Auto: return "Auto";
        case MiniEngineTaaExecutionPath::Compute: return "Compute";
        case MiniEngineTaaExecutionPath::FullscreenPixelShader:
            return "Fullscreen Pixel Shader";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaComputeKernelLabel(
            MiniEngineTaaComputeKernel value)
    {
        switch (value)
        {
        case MiniEngineTaaComputeKernel::Auto: return "Auto";
        case MiniEngineTaaComputeKernel::Threads8x8TwoPixels:
            return "8x8 Threads";
        case MiniEngineTaaComputeKernel::Threads16x8OnePixel:
            return "16x8 Threads, 1 Pixel per Thread";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaLdsLayoutLabel(MiniEngineTaaLdsLayout value)
    {
        switch (value)
        {
        case MiniEngineTaaLdsLayout::Auto: return "Auto";
        case MiniEngineTaaLdsLayout::Legacy: return "Legacy";
        case MiniEngineTaaLdsLayout::Split: return "Split";
        case MiniEngineTaaLdsLayout::SplitAndPacked:
            return "Split and Packed";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaAutoToggleLabel(MiniEngineTaaAutoToggle value)
    {
        switch (value)
        {
        case MiniEngineTaaAutoToggle::Auto: return "Auto";
        case MiniEngineTaaAutoToggle::Off: return "Off";
        case MiniEngineTaaAutoToggle::On: return "On";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaPassFusionLabel(MiniEngineTaaPassFusion value)
    {
        switch (value)
        {
        case MiniEngineTaaPassFusion::Auto: return "Auto";
        case MiniEngineTaaPassFusion::Separate: return "Separate";
        case MiniEngineTaaPassFusion::Fused: return "Fused";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char*
        GetMiniEngineTaaCacheBlockingLabel(
            MiniEngineTaaCacheBlocking value)
    {
        switch (value)
        {
        case MiniEngineTaaCacheBlocking::Auto: return "Auto";
        case MiniEngineTaaCacheBlocking::Off: return "Off";
        case MiniEngineTaaCacheBlocking::Bands2: return "2 Bands";
        case MiniEngineTaaCacheBlocking::Bands3: return "3 Bands";
        case MiniEngineTaaCacheBlocking::Bands4: return "4 Bands";
        default:
            return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr const char* GetMiniEngineTaaDebugViewLabel(
        MiniEngineTaaDebugView value)
    {
        switch (value)
        {
        case MiniEngineTaaDebugView::Off: return "Off";
        case MiniEngineTaaDebugView::StableInterior:
            return "Stable-Interior Score";
        case MiniEngineTaaDebugView::FinalHistoryWeight:
            return "Final History Weight";
        case MiniEngineTaaDebugView::SampleResurrection:
            return "Sample Resurrection";
        case MiniEngineTaaDebugView::SmaaEdgeMask:
            return "SMAA Edge Mask";
        case MiniEngineTaaDebugView::SmaaBlendWeights:
            return "SMAA Blend Weights";
        case MiniEngineTaaDebugView::SmaaOutputDelta:
            return "SMAA Output Delta (16x)";
        default: return "Unavailable";
        }
    }

    [[nodiscard]] inline constexpr bool
        IsMiniEngineTaaDebugVisualization(
            MiniEngineTaaDebugView value)
    {
        const uint32_t index = static_cast<uint32_t>(value);
        return index > UVSR_TAA_DEBUG_OFF &&
            index < UVSR_TAA_DEBUG_VIEW_COUNT;
    }

    [[nodiscard]] inline constexpr bool IsSmaaDebugVisualization(
        MiniEngineTaaDebugView value)
    {
        const uint32_t index = static_cast<uint32_t>(value);
        return index >= UVSR_SMAA_DEBUG_EDGE_MASK &&
            index < UVSR_AA_DEBUG_VIEW_COUNT;
    }

    [[nodiscard]] inline constexpr uint32_t
        GetSmaaDebugPermutationIndex(
            MiniEngineTaaDebugView value)
    {
        return static_cast<uint32_t>(value) -
            UVSR_SMAA_DEBUG_EDGE_MASK;
    }
}
