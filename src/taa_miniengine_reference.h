#pragma once

#include "taa_miniengine_options.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace uvsr
{
    struct MiniEngineTaaJitterSample
    {
        float x;
        float y;
    };

    struct MiniEngineTaaColor
    {
        float x;
        float y;
        float z;
    };

    struct MiniEngineTaaRange
    {
        float minimum;
        float maximum;
    };

    struct MiniEngineTaaMotionCandidate
    {
        float viewDepth;
        bool depthValid;
        bool motionValid;
    };

    inline constexpr float MiniEngineTaaDefaultSharpness = 0.5f;
    inline constexpr float MiniEngineTaaMinimumSharpness = 0.0f;
    inline constexpr float MiniEngineTaaMaximumSharpness = 1.0f;
    inline constexpr float MiniEngineTaaSharpenThreshold = 0.001f;
    inline constexpr float MiniEngineTaaStableInteriorFloor =
        UVSR_TAA_STABLE_INTERIOR_FLOOR;
    struct MiniEngineTaaSharpenWeights
    {
        float center;
        float lateral;
    };

    // MiniEngine's exact eight-position Halton 2/3 table. MiniEngine moves a
    // positive viewport origin and therefore stores samples in [0, 1), with
    // 0.5 documented as neutral. UVSR jitters its projection in signed pixel
    // units, so only the constant 0.5 center is removed; phase order and all
    // previous-minus-current deltas remain identical.
    inline constexpr std::array<MiniEngineTaaJitterSample, 8>
        MiniEngineTaaHalton23 = {{
            { 0.0f / 8.0f, 0.0f / 9.0f },
            { 4.0f / 8.0f, 3.0f / 9.0f },
            { 2.0f / 8.0f, 6.0f / 9.0f },
            { 6.0f / 8.0f, 1.0f / 9.0f },
            { 1.0f / 8.0f, 4.0f / 9.0f },
            { 5.0f / 8.0f, 7.0f / 9.0f },
            { 3.0f / 8.0f, 2.0f / 9.0f },
            { 7.0f / 8.0f, 5.0f / 9.0f }
        }};

    [[nodiscard]] inline constexpr MiniEngineTaaJitterSample
        GetMiniEngineTaaJitter(uint64_t frameIndex)
    {
        const MiniEngineTaaJitterSample sample =
            MiniEngineTaaHalton23[frameIndex % MiniEngineTaaHalton23.size()];
        return { sample.x - 0.5f, sample.y - 0.5f };
    }

    inline constexpr uint32_t TemporalAaBaseHistorySlotCount = 2u;
    inline constexpr uint32_t TemporalAaRgba16fBytesPerPixel = 8u;
    inline constexpr uint32_t TemporalAaR32fBytesPerPixel = 4u;

    [[nodiscard]] inline constexpr uint32_t
        GetTemporalAaPhaseSlot(uint64_t sequenceIndex)
    {
        return static_cast<uint32_t>(sequenceIndex & 1u);
    }

    [[nodiscard]] inline constexpr uint32_t
        GetTemporalAaPreviousPhaseSlot(uint64_t sequenceIndex)
    {
        return GetTemporalAaPhaseSlot(sequenceIndex) ^ 1u;
    }

    [[nodiscard]] inline constexpr uint64_t
        GetTemporalAaBaseHistoryBytes(uint32_t width, uint32_t height)
    {
        // Exactly two RGBA16F color slots and two R32F raw-depth slots.
        return uint64_t(width) * uint64_t(height) *
            TemporalAaBaseHistorySlotCount *
            (TemporalAaRgba16fBytesPerPixel +
                TemporalAaR32fBytesPerPixel);
    }

    // Pure behavior model for the shared TemporalHistoryState. GPU validation
    // exercises the real NVRHI-backed state; this model keeps sequence and
    // reset edge cases deterministic in the platform-independent test.
    struct TemporalAaHistoryReferenceState
    {
        std::array<bool, TemporalAaBaseHistorySlotCount> valid = {};
        std::array<uint64_t, TemporalAaBaseHistorySlotCount>
            committedSequence = {};
        uint32_t maximumAccumulation = 65504u;
        uint32_t accumulationCount = 0u;
        uint32_t resetCount = 0u;
        uint64_t lastCommittedSequence = 0u;
        bool hasCommittedSequence = false;

        [[nodiscard]] constexpr bool CanRead(
            uint32_t slot,
            bool previousViewAvailable,
            uint64_t sequenceIndex) const
        {
            return slot < valid.size() &&
                previousViewAvailable &&
                valid[slot] &&
                accumulationCount > 0u &&
                hasCommittedSequence &&
                sequenceIndex > 0u &&
                lastCommittedSequence == sequenceIndex - 1u &&
                committedSequence[slot] == sequenceIndex - 1u;
        }

        constexpr void Commit(
            uint32_t slot,
            uint64_t sequenceIndex)
        {
            if (slot >= valid.size())
                return;

            const bool continuous =
                hasCommittedSequence &&
                sequenceIndex > 0u &&
                lastCommittedSequence == sequenceIndex - 1u;
            if (!continuous)
            {
                valid = {};
                committedSequence = {};
                accumulationCount = 0u;
            }

            valid[slot] = true;
            committedSequence[slot] = sequenceIndex;
            accumulationCount = std::min(
                accumulationCount + 1u,
                std::max(maximumAccumulation, 1u));
            lastCommittedSequence = sequenceIndex;
            hasCommittedSequence = true;
        }

        [[nodiscard]] constexpr bool Invalidate()
        {
            const bool changed =
                valid[0] ||
                valid[1] ||
                accumulationCount != 0u ||
                hasCommittedSequence;
            valid = {};
            committedSequence = {};
            accumulationCount = 0u;
            lastCommittedSequence = 0u;
            hasCommittedSequence = false;
            if (changed)
                ++resetCount;
            return changed;
        }
    };

    struct TemporalAaVector2
    {
        float x;
        float y;
    };

    [[nodiscard]] inline bool IsTemporalAaDeviceDepthValid(float depth)
    {
        return std::isfinite(depth) && depth > 0.f && depth <= 1.f;
    }

    [[nodiscard]] inline bool IsTemporalAaMotionValid(
        const std::array<float, 4>& packedMotion)
    {
        return packedMotion[3] > 0.5f &&
            std::all_of(
                packedMotion.begin(),
                packedMotion.end(),
                [](float value) { return std::isfinite(value); });
    }

    [[nodiscard]] inline constexpr bool
        IsTemporalAaPointPositionInBounds(
            MiniEngineTaaJitterSample pixelPosition,
            uint32_t width,
            uint32_t height)
    {
        const float windowX = pixelPosition.x + 0.5f;
        const float windowY = pixelPosition.y + 0.5f;
        return windowX > 0.f &&
            windowY > 0.f &&
            windowX < static_cast<float>(width) &&
            windowY < static_cast<float>(height);
    }

    [[nodiscard]] inline constexpr bool
        IsTemporalAaLinearFootprintInBounds(
            MiniEngineTaaJitterSample pixelPosition,
            uint32_t width,
            uint32_t height)
    {
        if (width == 0u || height == 0u)
            return false;
        return pixelPosition.x >= 0.f &&
            pixelPosition.y >= 0.f &&
            pixelPosition.x <= static_cast<float>(width - 1u) &&
            pixelPosition.y <= static_cast<float>(height - 1u);
    }

    [[nodiscard]] inline constexpr bool
        IsTemporalAaHistoryPositionInBounds(
            MiniEngineTaaJitterSample pixelPosition,
            uint32_t width,
            uint32_t height)
    {
        // Compatibility behavior matches the shared HLSL point-sample name.
        return IsTemporalAaPointPositionInBounds(
            pixelPosition,
            width,
            height);
    }

    struct TemporalAaReverseZFootprint
    {
        float farthestValidDeviceDepth = 1.f;
        float nearestValidDeviceDepth = 0.f;
        uint32_t validMask = 0u;
        uint32_t backgroundMask = 0u;
    };

    [[nodiscard]] inline TemporalAaReverseZFootprint
        ReduceTemporalAaReverseZFootprint(
            const std::array<float, 4>& deviceDepths)
    {
        TemporalAaReverseZFootprint result;
        for (uint32_t lane = 0u; lane < deviceDepths.size(); ++lane)
        {
            const float depth = deviceDepths[lane];
            if (std::isfinite(depth) && depth == 0.f)
                result.backgroundMask |= 1u << lane;
            if (IsTemporalAaDeviceDepthValid(depth))
            {
                result.validMask |= 1u << lane;
                result.farthestValidDeviceDepth =
                    std::min(result.farthestValidDeviceDepth, depth);
                result.nearestValidDeviceDepth =
                    std::max(result.nearestValidDeviceDepth, depth);
            }
        }
        return result;
    }

    [[nodiscard]] inline constexpr bool
        TemporalAaFootprintHasConsistentGeometry(
            const TemporalAaReverseZFootprint& footprint)
    {
        // Match the shared HLSL ownership gate: every Gather lane must be
        // finite geometry. Mixed geometry/background and corrupt partial
        // footprints cannot authorize an unrelated bilinear history color.
        return footprint.validMask == 0xfu &&
            footprint.backgroundMask == 0u;
    }

    inline constexpr float TemporalAaFp16RelativeHalfUlp =
        0.00048828125f;
    inline constexpr float TemporalAaFp16SubnormalHalfUlp =
        0.0000000298023223876953125f;
    inline constexpr float
        TemporalAaMinimumRelativeDeviceDepthPrecision =
            0.0009765625f;
    inline constexpr float TemporalAaMaximumViewDepth = 65504.f;
    inline constexpr float
        TemporalAaD24SourceDepthPairQuantizationError =
            1.f / 16777215.f;
    inline constexpr float
        TemporalAaD16SourceDepthPairQuantizationError =
            1.f / 65535.f;
    [[nodiscard]] inline float
        GetTemporalAaFp16DeviceDepthDeltaError(
            float quantizedDeviceDepthDelta)
    {
        if (!std::isfinite(quantizedDeviceDepthDelta))
            return 0.f;

        return std::max(
            std::abs(quantizedDeviceDepthDelta) *
                TemporalAaFp16RelativeHalfUlp,
            TemporalAaFp16SubnormalHalfUlp);
    }

    [[nodiscard]] inline float
        GetTemporalAaDeviceDepthError(
            float quantizedDeviceDepthDelta,
            float sourceDepthPairQuantizationError)
    {
        if (!std::isfinite(quantizedDeviceDepthDelta) ||
            !std::isfinite(sourceDepthPairQuantizationError) ||
            sourceDepthPairQuantizationError < 0.f)
        {
            return TemporalAaMaximumViewDepth;
        }

        return GetTemporalAaFp16DeviceDepthDeltaError(
                quantizedDeviceDepthDelta) +
            sourceDepthPairQuantizationError;
    }

    [[nodiscard]] inline bool
        IsTemporalAaDeviceDepthPrecisionValid(
            float expectedPreviousDeviceDepth,
            float quantizedDeviceDepthDelta,
            float sourceDepthPairQuantizationError)
    {
        return IsTemporalAaDeviceDepthValid(
                expectedPreviousDeviceDepth) &&
            std::isfinite(quantizedDeviceDepthDelta) &&
            std::isfinite(sourceDepthPairQuantizationError) &&
            sourceDepthPairQuantizationError >= 0.f &&
            GetTemporalAaDeviceDepthError(
                quantizedDeviceDepthDelta,
                sourceDepthPairQuantizationError) <=
                expectedPreviousDeviceDepth *
                    TemporalAaMinimumRelativeDeviceDepthPrecision;
    }

    [[nodiscard]] inline float
        GetTemporalAaInfiniteReverseZViewDepth(
            float deviceDepth,
            float nearPlane)
    {
        if (!IsTemporalAaDeviceDepthValid(deviceDepth) ||
            !std::isfinite(nearPlane) ||
            nearPlane <= 0.f)
        {
            return TemporalAaMaximumViewDepth;
        }

        return std::min(
            nearPlane / deviceDepth,
            TemporalAaMaximumViewDepth);
    }

    [[nodiscard]] inline float
        GetTemporalAaDeviceDepthViewAllowance(
            float expectedPreviousDeviceDepth,
            float quantizedDeviceDepthDelta,
            float sourceDepthPairQuantizationError,
            float nearPlane)
    {
        if (!IsTemporalAaDeviceDepthValid(
                expectedPreviousDeviceDepth) ||
            !std::isfinite(quantizedDeviceDepthDelta) ||
            !std::isfinite(sourceDepthPairQuantizationError) ||
            sourceDepthPairQuantizationError < 0.f)
        {
            return 0.f;
        }

        const float nearestPlausibleDeviceDepth = std::min(
            expectedPreviousDeviceDepth +
                GetTemporalAaDeviceDepthError(
                    quantizedDeviceDepthDelta,
                    sourceDepthPairQuantizationError),
            1.f);
        return std::max(
            GetTemporalAaInfiniteReverseZViewDepth(
                expectedPreviousDeviceDepth,
                nearPlane) -
                GetTemporalAaInfiniteReverseZViewDepth(
                    nearestPlausibleDeviceDepth,
                    nearPlane),
            0.f);
    }

    [[nodiscard]] inline bool
        IsTemporalAaInfiniteReverseZDepthAccepted(
            float expectedPreviousDeviceDepth,
            float quantizedDeviceDepthDelta,
            float sourceDepthPairQuantizationError,
            const TemporalAaReverseZFootprint& footprint,
            float nearPlane,
            float baseViewDepthAllowance)
    {
        if (!IsTemporalAaDeviceDepthValid(
                expectedPreviousDeviceDepth) ||
            !std::isfinite(quantizedDeviceDepthDelta) ||
            !IsTemporalAaDeviceDepthPrecisionValid(
                expectedPreviousDeviceDepth,
                quantizedDeviceDepthDelta,
                sourceDepthPairQuantizationError) ||
            !TemporalAaFootprintHasConsistentGeometry(footprint))
        {
            return false;
        }

        const float expectedViewDepth =
            GetTemporalAaInfiniteReverseZViewDepth(
                expectedPreviousDeviceDepth,
                nearPlane);
        const float historyViewDepth =
            GetTemporalAaInfiniteReverseZViewDepth(
                footprint.farthestValidDeviceDepth,
                nearPlane);
        return expectedViewDepth <=
            historyViewDepth +
                baseViewDepthAllowance +
                GetTemporalAaDeviceDepthViewAllowance(
                    expectedPreviousDeviceDepth,
                    quantizedDeviceDepthDelta,
                    sourceDepthPairQuantizationError,
                    nearPlane);
    }

    [[nodiscard]] inline float
        GetTemporalAaDeviceDepthFartherViewAllowance(
            float expectedPreviousDeviceDepth,
            float quantizedDeviceDepthDelta,
            float sourceDepthPairQuantizationError,
            float nearPlane)
    {
        if (!IsTemporalAaDeviceDepthValid(
                expectedPreviousDeviceDepth) ||
            !std::isfinite(quantizedDeviceDepthDelta) ||
            !std::isfinite(sourceDepthPairQuantizationError) ||
            sourceDepthPairQuantizationError < 0.f)
        {
            return 0.f;
        }

        const float deviceDepthError =
            GetTemporalAaDeviceDepthError(
                quantizedDeviceDepthDelta,
                sourceDepthPairQuantizationError);
        if (deviceDepthError >= expectedPreviousDeviceDepth)
            return TemporalAaMaximumViewDepth;

        return std::max(
            GetTemporalAaInfiniteReverseZViewDepth(
                expectedPreviousDeviceDepth - deviceDepthError,
                nearPlane) -
                GetTemporalAaInfiniteReverseZViewDepth(
                    expectedPreviousDeviceDepth,
                    nearPlane),
            0.f);
    }

    [[nodiscard]] inline constexpr uint64_t
        GetMiniEngineTaaHistoryBytes(
            uint32_t width,
            uint32_t height,
            bool includeMomentHistory)
    {
        // The shared pair is two RGBA16F confidence/color histories plus two
        // R32F device-depth histories (24 B/pixel). Stable Interior alone adds
        // two RG16F luminance-moment histories (8 B/pixel). This is exact
        // logical texel payload before API alignment.
        return uint64_t(width) * uint64_t(height) *
            (includeMomentHistory ? 32u : 24u);
    }

    [[nodiscard]] inline constexpr uint64_t
        GetMiniEngineTaaPersistentHistoryBytes(
            uint32_t width,
            uint32_t height)
    {
        // Developer resurrection owns two RGBA16F resolved-color snapshots
        // and two R32F raw-depth snapshots: 24 bytes per pixel.
        return uint64_t(width) * uint64_t(height) * 24u;
    }

    [[nodiscard]] inline constexpr uint64_t
        GetMiniEngineTaaDebugBytes(uint32_t width, uint32_t height)
    {
        // RG16F stores stable-interior and resurrection diagnostics without a
        // runtime selector branch in any shipping algorithm permutation.
        return uint64_t(width) * uint64_t(height) * 4u;
    }

    [[nodiscard]] inline constexpr MiniEngineTaaJitterSample
        AddMiniEngineTaaPixelVectors(
            MiniEngineTaaJitterSample a,
            MiniEngineTaaJitterSample b)
    {
        return { a.x + b.x, a.y + b.y };
    }

    [[nodiscard]] inline constexpr MiniEngineTaaJitterSample
        GetMiniEngineTaaCurrentToPreviousJitter(
            MiniEngineTaaJitterSample currentJitter,
            MiniEngineTaaJitterSample previousJitter)
    {
        // Depth history stores the previous frame's jittered device-depth
        // grid, so de-jittered current-to-previous pixel motion needs this
        // previous-minus-current offset exactly once.
        return {
            previousJitter.x - currentJitter.x,
            previousJitter.y - currentJitter.y
        };
    }

    [[nodiscard]] inline constexpr MiniEngineTaaJitterSample
        GetMiniEngineTaaCurrentInputPosition(
            MiniEngineTaaJitterSample unjitteredPixel,
            MiniEngineTaaJitterSample currentJitter)
    {
        // Donut PlanarView jitter is signed full-resolution pixels and maps the
        // unjittered output center to input at pixel + currentJitter.
        return AddMiniEngineTaaPixelVectors(unjitteredPixel, currentJitter);
    }

    [[nodiscard]] inline constexpr std::array<float, 4>
        GetMiniEngineTaaCatmullRomWeights(float phase)
    {
        const float phase2 = phase * phase;
        const float phase3 = phase2 * phase;
        return {
            -0.5f * phase + phase2 - 0.5f * phase3,
            1.f - 2.5f * phase2 + 1.5f * phase3,
            0.5f * phase + 2.f * phase2 - 1.5f * phase3,
            -0.5f * phase2 + 0.5f * phase3
        };
    }

    [[nodiscard]] inline std::array<MiniEngineTaaJitterSample, 5>
        GetMiniEngineTaaFiveTapHistoryPositions(
            MiniEngineTaaJitterSample pixelPosition)
    {
        const float baseX = std::floor(pixelPosition.x);
        const float baseY = std::floor(pixelPosition.y);
        const float phaseX = pixelPosition.x - baseX;
        const float phaseY = pixelPosition.y - baseY;
        const std::array<float, 4> weightsX =
            GetMiniEngineTaaCatmullRomWeights(phaseX);
        const std::array<float, 4> weightsY =
            GetMiniEngineTaaCatmullRomWeights(phaseY);
        const float middleWeightX = weightsX[1] + weightsX[2];
        const float middleWeightY = weightsY[1] + weightsY[2];
        const MiniEngineTaaJitterSample center = {
            baseX + weightsX[2] / std::max(middleWeightX, 1e-5f),
            baseY + weightsY[2] / std::max(middleWeightY, 1e-5f)
        };

        // Order matches the shader's optimized cross: center, left, right,
        // north, and south.
        return {{
            center,
            { baseX - 1.f, center.y },
            { baseX + 2.f, center.y },
            { center.x, baseY - 1.f },
            { center.x, baseY + 2.f }
        }};
    }

    [[nodiscard]] inline constexpr MiniEngineTaaRange
        GetMiniEngineTaaPositiveFootprintRange(
            const std::array<float, 4>& centralCell,
            MiniEngineTaaJitterSample jitter)
    {
        // Values are top-left, top-right, bottom-left, bottom-right in the
        // positive central Catmull-Rom cell. A zero jitter axis collapses that
        // cell to one column or row and must not let a zero-weight sample
        // expand the anti-ringing bounds.
        MiniEngineTaaRange range =
            { centralCell[0], centralCell[0] };
        const bool useSecondColumn = jitter.x != 0.f;
        const bool useSecondRow = jitter.y != 0.f;
        if (useSecondColumn)
        {
            range.minimum = std::min(
                range.minimum,
                centralCell[1]);
            range.maximum = std::max(
                range.maximum,
                centralCell[1]);
        }
        if (useSecondRow)
        {
            range.minimum = std::min(
                range.minimum,
                centralCell[2]);
            range.maximum = std::max(
                range.maximum,
                centralCell[2]);
        }
        if (useSecondColumn && useSecondRow)
        {
            range.minimum = std::min(
                range.minimum,
                centralCell[3]);
            range.maximum = std::max(
                range.maximum,
                centralCell[3]);
        }
        return range;
    }

    [[nodiscard]] inline constexpr float ClipMiniEngineTaaScalar(
        float value,
        MiniEngineTaaRange range)
    {
        const float center =
            (range.maximum + range.minimum) * 0.5f;
        const float halfDimension =
            (range.maximum - range.minimum) * 0.5f + 0.001f;
        const float displacement = value - center;
        const float normalized =
            (displacement < 0.f ? -displacement : displacement) /
            halfDimension;
        const float scale = normalized > 1.f ? normalized : 1.f;
        return center + displacement / scale;
    }

    [[nodiscard]] inline constexpr MiniEngineTaaJitterSample
        GetMiniEngineTaaHistoryColorPosition(
            MiniEngineTaaJitterSample currentPixel,
            MiniEngineTaaJitterSample currentToPreviousMotionPixels)
    {
        // G-buffer motion is already de-jittered. Color history is on the
        // unjittered output grid, so no jitter term belongs here.
        return AddMiniEngineTaaPixelVectors(
            currentPixel,
            currentToPreviousMotionPixels);
    }

    [[nodiscard]] inline constexpr MiniEngineTaaJitterSample
        GetMiniEngineTaaHistoryDepthPosition(
            MiniEngineTaaJitterSample currentPixel,
            MiniEngineTaaJitterSample currentToPreviousMotionPixels,
            MiniEngineTaaJitterSample currentJitter,
            MiniEngineTaaJitterSample previousJitter)
    {
        return AddMiniEngineTaaPixelVectors(
            GetMiniEngineTaaHistoryColorPosition(
                currentPixel,
                currentToPreviousMotionPixels),
            GetMiniEngineTaaCurrentToPreviousJitter(
                currentJitter,
                previousJitter));
    }

    [[nodiscard]] inline constexpr bool
        IsMiniEngineTaaHistoryPositionInBounds(
            MiniEngineTaaJitterSample pixelPosition,
            uint32_t width,
            uint32_t height)
    {
        // MiniEngine history color, depth, and moments use linear filtering
        // or Gather, so their requested center must remain between the first
        // and last real texel centers.
        return IsTemporalAaLinearFootprintInBounds(
            pixelPosition,
            width,
            height);
    }

    [[nodiscard]] inline std::array<bool, 5>
        GetMiniEngineTaaFiveTapHistoryPositionValidity(
            MiniEngineTaaJitterSample pixelPosition,
            MiniEngineTaaJitterSample currentToPreviousJitter,
            uint32_t width,
            uint32_t height)
    {
        const std::array<MiniEngineTaaJitterSample, 5> positions =
            GetMiniEngineTaaFiveTapHistoryPositions(pixelPosition);
        std::array<bool, 5> validity = {};
        for (uint32_t index = 0u; index < positions.size(); ++index)
        {
            const MiniEngineTaaJitterSample depthPosition =
                AddMiniEngineTaaPixelVectors(
                    positions[index],
                    currentToPreviousJitter);
            validity[index] =
                IsMiniEngineTaaHistoryPositionInBounds(
                    positions[index],
                    width,
                    height) &&
                IsMiniEngineTaaHistoryPositionInBounds(
                    depthPosition,
                    width,
                    height);
        }
        return validity;
    }

    [[nodiscard]] inline constexpr int32_t
        SelectMiniEngineTaaClosestCrossCandidate(
            const std::array<MiniEngineTaaMotionCandidate, 5>& candidates)
    {
        int32_t selected = -1;
        float closestViewDepth = 3.402823466e+38f;
        for (uint32_t index = 0u; index < candidates.size(); ++index)
        {
            const MiniEngineTaaMotionCandidate& candidate = candidates[index];
            if (candidate.depthValid &&
                candidate.motionValid &&
                candidate.viewDepth < closestViewDepth)
            {
                closestViewDepth = candidate.viewDepth;
                selected = static_cast<int32_t>(index);
            }
        }
        return selected;
    }

    [[nodiscard]] inline constexpr float
        SelectMiniEngineTaaResurrectionGeometryDepth(
            float centerDeviceDepth,
            float motionOwnedDeviceDepth)
    {
        // A dilated motion sample may own a different surface. Resurrection
        // reprojects the current pixel's geometry, so its center ray must
        // always remain paired with the center depth.
        (void)motionOwnedDeviceDepth;
        return centerDeviceDepth;
    }

    [[nodiscard]] inline constexpr float ClampMiniEngineTaaUnit(float value)
    {
        return value < 0.f ? 0.f : value > 1.f ? 1.f : value;
    }

    [[nodiscard]] inline constexpr float
        GetMiniEngineTaaSupportedHistoryEstimate(
            float centerHistory,
            float neighborEstimate,
            float depthSupport)
    {
        // A rejected one-sample bicubic estimate must collapse exactly to the
        // real center history sample before anti-ringing bounds are reduced.
        const float support = ClampMiniEngineTaaUnit(depthSupport);
        return centerHistory +
            (neighborEstimate - centerHistory) * support;
    }

    [[nodiscard]] inline constexpr float MiniEngineTaaSmoothStep(
        float minimum,
        float maximum,
        float value)
    {
        const float normalized = ClampMiniEngineTaaUnit(
            (value - minimum) / (maximum - minimum));
        return normalized * normalized * (3.f - 2.f * normalized);
    }

    [[nodiscard]] inline constexpr int32_t
        SelectMiniEngineTaaCenterFirstEdgeCandidate(
            const std::array<MiniEngineTaaMotionCandidate, 5>& candidates)
    {
        const int32_t closest =
            SelectMiniEngineTaaClosestCrossCandidate(candidates);
        const bool centerDepthValid = candidates[0].depthValid;
        const bool centerMotionValid = candidates[0].motionValid;
        const bool centerValid =
            centerDepthValid && centerMotionValid;
        if (closest < 0)
            return centerValid ? 0 : -1;

        const float centerViewDepth = candidates[0].viewDepth;
        const float closestViewDepth =
            candidates[static_cast<size_t>(closest)].viewDepth;
        const float minimumViewDepth =
            centerViewDepth < closestViewDepth
                ? centerViewDepth
                : closestViewDepth;
        const float depthDifference =
            centerViewDepth - closestViewDepth;
        const float relativeDepthDifference =
            (depthDifference < 0.f
                ? -depthDifference
                : depthDifference) /
            (minimumViewDepth > 1e-3f ? minimumViewDepth : 1e-3f);
        const float silhouetteActivation = centerDepthValid
            ? MiniEngineTaaSmoothStep(
                0.005f,
                0.02f,
                relativeDepthDifference)
            : 1.f;
        const bool nearestIsForeground =
            closestViewDepth + 1e-3f < centerViewDepth;
        const float borrowActivation =
            silhouetteActivation *
            ((!centerValid || nearestIsForeground) ? 1.f : 0.f);

        // Smooth activation may decide whether dilation is warranted, but it
        // must never interpolate independently owned motion/depth samples.
        if (borrowActivation >= 0.5f)
            return closest;
        return centerValid ? 0 : -1;
    }

    [[nodiscard]] inline constexpr float MinMiniEngineTaaFour(
        const std::array<float, 4>& values)
    {
        const float firstPair =
            values[0] < values[1] ? values[0] : values[1];
        const float secondPair =
            values[2] < values[3] ? values[2] : values[3];
        return firstPair < secondPair ? firstPair : secondPair;
    }

    [[nodiscard]] inline constexpr float
        GetMiniEngineTaaCurrentReconstructionSupport(
            const std::array<float, 9>& depthCoherence,
            MiniEngineTaaJitterSample jitter)
    {
        // Row-major 3x3 support around center. The positive reconstruction
        // cell contains center, the phase-selected X/Y neighbors, and their
        // diagonal when both jitter axes are nonzero.
        float support = ClampMiniEngineTaaUnit(depthCoherence[4]);
        const int32_t xOffset =
            jitter.x < 0.f ? -1 : jitter.x > 0.f ? 1 : 0;
        const int32_t yOffset =
            jitter.y < 0.f ? -1 : jitter.y > 0.f ? 1 : 0;
        if (xOffset != 0)
        {
            support = std::min(
                support,
                ClampMiniEngineTaaUnit(
                    depthCoherence[4 + xOffset]));
        }
        if (yOffset != 0)
        {
            support = std::min(
                support,
                ClampMiniEngineTaaUnit(
                    depthCoherence[4 + 3 * yOffset]));
        }
        if (xOffset != 0 && yOffset != 0)
        {
            support = std::min(
                support,
                ClampMiniEngineTaaUnit(
                    depthCoherence[
                        4 + xOffset + 3 * yOffset]));
        }
        return support;
    }

    [[nodiscard]] inline constexpr float
        GetMiniEngineTaaStableInteriorScore(
            const std::array<float, 4>& depthCoherence,
            const std::array<float, 4>& velocityCoherence,
            float lumaCoherence,
            float temporalLumaCoherence,
            float historyDepthAgreement)
    {
        // An interior must remain coherent in every cardinal direction.
        // Averaging the cross would assign a high score to a pixel with one
        // discontinuous neighbor, which is precisely a silhouette.
        return ClampMiniEngineTaaUnit(
            ClampMiniEngineTaaUnit(
                MinMiniEngineTaaFour(depthCoherence)) *
            ClampMiniEngineTaaUnit(
                MinMiniEngineTaaFour(velocityCoherence)) *
            ClampMiniEngineTaaUnit(lumaCoherence) *
            ClampMiniEngineTaaUnit(temporalLumaCoherence) *
            ClampMiniEngineTaaUnit(historyDepthAgreement));
    }

    [[nodiscard]] inline constexpr float
        GetMiniEngineTaaLocalLumaCoherence(
            float minimumLuma,
            float maximumLuma,
            float reconstructedCurrentLuma)
    {
        const float magnitude = reconstructedCurrentLuma < 0.f
            ? -reconstructedCurrentLuma
            : reconstructedCurrentLuma;
        const float scale = magnitude > 0.1f ? magnitude : 0.1f;
        const float footprintMinimum =
            minimumLuma < reconstructedCurrentLuma
                ? minimumLuma
                : reconstructedCurrentLuma;
        const float footprintMaximum =
            maximumLuma > reconstructedCurrentLuma
                ? maximumLuma
                : reconstructedCurrentLuma;
        const float relativeRange =
            (footprintMaximum - footprintMinimum) / scale;
        return 1.f - MiniEngineTaaSmoothStep(
            0.05f,
            0.5f,
            relativeRange);
    }

    [[nodiscard]] inline float GetMiniEngineTaaTemporalLumaCoherence(
        float currentLuma,
        float previousMean,
        float previousSecondMoment)
    {
        if (!std::isfinite(currentLuma) ||
            !std::isfinite(previousMean) ||
            !std::isfinite(previousSecondMoment))
        {
            return 0.f;
        }

        const float previousVariance = std::max(
            previousSecondMoment - previousMean * previousMean,
            0.f);
        const float currentMagnitude = std::abs(currentLuma);
        const float previousMagnitude = std::abs(previousMean);
        const float lumaScale = std::max(
            std::max(currentMagnitude, previousMagnitude),
            0.1f);
        const float tolerance =
            std::sqrt(previousVariance) + 0.02f * lumaScale + 0.001f;
        const float normalizedDifference =
            std::abs(currentLuma - previousMean) / tolerance;
        return 1.f - MiniEngineTaaSmoothStep(
            1.f,
            4.f,
            normalizedDifference);
    }

    [[nodiscard]] inline constexpr float
        GetMiniEngineTaaHistoryDepthFootprintCoherence(
            float nearestViewDepth,
            float farthestViewDepth,
            bool nearestValid,
            bool farthestValid)
    {
        if (!nearestValid || !farthestValid)
            return 0.f;

        const float scale =
            nearestViewDepth > 1e-3f ? nearestViewDepth : 1e-3f;
        const float difference = farthestViewDepth - nearestViewDepth;
        const float absoluteDifference =
            difference < 0.f ? -difference : difference;
        return 1.f - MiniEngineTaaSmoothStep(
            0.005f,
            0.05f,
            absoluteDifference / scale);
    }

    [[nodiscard]] inline constexpr float
        GetMiniEngineTaaHistoryTapDepthCoherence(
            float expectedViewDepth,
            float tapViewDepth,
            bool expectedValid,
            bool tapValid)
    {
        if (!expectedValid || !tapValid)
            return 0.f;

        const float minimumDepth =
            expectedViewDepth < tapViewDepth
                ? expectedViewDepth
                : tapViewDepth;
        const float scale = minimumDepth > 1e-3f
            ? minimumDepth
            : 1e-3f;
        const float difference = expectedViewDepth - tapViewDepth;
        const float absoluteDifference =
            difference < 0.f ? -difference : difference;
        return 1.f - MiniEngineTaaSmoothStep(
            0.005f,
            0.05f,
            absoluteDifference / scale);
    }

    [[nodiscard]] inline constexpr float
        SelectMiniEngineTaaFarthestReverseZDeviceDepth(
            const std::array<float, 4>& deviceDepths)
    {
        // MiniEngine deliberately takes the farthest of its four forward
        // linear-depth samples for conservative disocclusion. UVSR stores
        // infinite reverse-Z device depth, so smaller is farther and cleared
        // background is zero.
        const float firstPair = deviceDepths[0] < deviceDepths[1]
            ? deviceDepths[0]
            : deviceDepths[1];
        const float secondPair = deviceDepths[2] < deviceDepths[3]
            ? deviceDepths[2]
            : deviceDepths[3];
        return firstPair < secondPair ? firstPair : secondPair;
    }

    [[nodiscard]] inline constexpr float ReduceMiniEngineTaaHistoryForInterior(
        float acceptedHistoryWeight,
        float stableInteriorScore)
    {
        const float score = ClampMiniEngineTaaUnit(stableInteriorScore);
        // Stable Interior is a clarity control, not another edge rejection
        // gate. Preserve MiniEngine's accepted history at uncertain edges and
        // reduce it only where all signals identify a stable interior. The
        // floor is absolute: already-lower accepted history remains unchanged.
        const float target =
            acceptedHistoryWeight < MiniEngineTaaStableInteriorFloor
                ? acceptedHistoryWeight
                : MiniEngineTaaStableInteriorFloor;
        return acceptedHistoryWeight +
            (target - acceptedHistoryWeight) * score;
    }

    [[nodiscard]] inline constexpr float
        StoreMiniEngineTaaConfidence(float baseHistoryWeight)
    {
        const float clamped = ClampMiniEngineTaaUnit(baseHistoryWeight);
        return ClampMiniEngineTaaUnit(1.f / (2.f - clamped));
    }

    [[nodiscard]] inline constexpr float
        GetMiniEngineTaaSelectiveRejection(float acceptedHistoryWeight)
    {
        // Stored MiniEngine confidence begins at 0.5 and reaches 0.8 after
        // four accepted contributions. Use that per-pixel recurrence as the
        // continuous selective-morphology fade instead of a binary depth test plus
        // a global frame count. This prevents a phase-varying silhouette from
        // switching an entire dilated tile between spatial and temporal color.
        const float rejection =
            1.f - MiniEngineTaaSmoothStep(
            UVSR_TAA_SELECTIVE_HISTORY_MINIMUM,
            UVSR_TAA_SELECTIVE_HISTORY_TRUSTED,
            ClampMiniEngineTaaUnit(acceptedHistoryWeight));
        // Match the shader's R16F export contract: fade the last sub-visible
        // presentation step continuously to exact zero, then classify the
        // same stored mask without a second hard threshold.
        return ClampMiniEngineTaaUnit(
            (rejection - UVSR_TAA_SELECTIVE_REJECTION_FLOOR) /
            (1.f - UVSR_TAA_SELECTIVE_REJECTION_FLOOR));
    }

    [[nodiscard]] inline constexpr MiniEngineTaaColor
        MiniEngineTaaRgbToYCoCg(MiniEngineTaaColor rgb)
    {
        return {
            0.25f * rgb.x + 0.5f * rgb.y + 0.25f * rgb.z,
            0.5f * rgb.x - 0.5f * rgb.z,
            -0.25f * rgb.x + 0.5f * rgb.y - 0.25f * rgb.z
        };
    }

    [[nodiscard]] inline constexpr MiniEngineTaaColor
        MiniEngineTaaYCoCgToRgb(MiniEngineTaaColor ycocg)
    {
        return {
            ycocg.x + ycocg.y - ycocg.z,
            ycocg.x + ycocg.z,
            ycocg.x - ycocg.y - ycocg.z
        };
    }

    [[nodiscard]] inline constexpr float ClampMiniEngineTaaSharpness(
        float sharpness)
    {
        return sharpness < MiniEngineTaaMinimumSharpness
            ? MiniEngineTaaMinimumSharpness
            : sharpness > MiniEngineTaaMaximumSharpness
                ? MiniEngineTaaMaximumSharpness
                : sharpness;
    }

    [[nodiscard]] inline constexpr bool ShouldSharpenMiniEngineTaa(
        bool enabled,
        float sharpness)
    {
        return enabled &&
            ClampMiniEngineTaaSharpness(sharpness) >=
                MiniEngineTaaSharpenThreshold;
    }

    [[nodiscard]] inline constexpr MiniEngineTaaSharpenWeights
        GetMiniEngineTaaSharpenWeights(float sharpness)
    {
        const float clamped = ClampMiniEngineTaaSharpness(sharpness);
        return { 1.f + clamped, 0.25f * clamped };
    }

    [[nodiscard]] inline constexpr bool IsMiniEngineTaaAvailable(
        bool enabled,
        bool pbrEnabled,
        bool deferredShading,
        bool visibilityTemporalEnabled)
    {
        // UVSR's required XYZ+validity motion contract is produced by the
        // first-party deferred PBR G-buffer. Pretending another renderer mode
        // has the same contract would create a plausible-looking but incorrect
        // temporal path.
        return enabled &&
               pbrEnabled &&
               deferredShading &&
               !visibilityTemporalEnabled;
    }
}
