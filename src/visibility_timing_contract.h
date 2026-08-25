#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace uvsr
{
    enum class VisibilityTimingStage : std::uint32_t
    {
        FirstTrace,
        Reconstruction,
        Upsample = Reconstruction,
        Composition,
        EffectEnvelope,
        Count
    };

    struct VisibilityTimingSnapshot
    {
        float firstTraceMs = 0.f;
        float reconstructionMs = 0.f;
        float compositionMs = 0.f;
        float effectEnvelopeMs = 0.f;
        bool available = false;

        [[nodiscard]] constexpr bool operator==(
            const VisibilityTimingSnapshot& other) const noexcept
        {
            return firstTraceMs == other.firstTraceMs &&
                reconstructionMs == other.reconstructionMs &&
                compositionMs == other.compositionMs &&
                effectEnvelopeMs == other.effectEnvelopeMs &&
                available == other.available;
        }
    };

    struct VisibilityTimingSlot
    {
        std::uint32_t submittedStageMask = 0u;
        std::uint32_t resolvedStageMask = 0u;
        std::array<float,
            static_cast<std::size_t>(VisibilityTimingStage::Count)>
            resolvedStageMilliseconds{};
    };

    enum class VisibilityTimingResolveStatus : std::uint32_t
    {
        Invalid,
        Pending,
        Published
    };

    struct VisibilityTimingResolution
    {
        VisibilityTimingSnapshot snapshot;
        VisibilityTimingResolveStatus status =
            VisibilityTimingResolveStatus::Invalid;
    };

    [[nodiscard]] inline constexpr std::uint32_t
        VisibilityTimingStageMask(VisibilityTimingStage stage) noexcept
    {
        const std::uint32_t index = static_cast<std::uint32_t>(stage);
        return index < static_cast<std::uint32_t>(
            VisibilityTimingStage::Count)
            ? 1u << index
            : 0u;
    }

    [[nodiscard]] inline constexpr bool VisibilityTimingSlotIsWritable(
        const VisibilityTimingSlot& slot) noexcept
    {
        return slot.submittedStageMask == 0u &&
            slot.resolvedStageMask == 0u;
    }

    [[nodiscard]] inline constexpr bool VisibilityTimingCanSubmitStage(
        const VisibilityTimingSlot& slot,
        VisibilityTimingStage stage) noexcept
    {
        const std::uint32_t mask = VisibilityTimingStageMask(stage);
        return mask != 0u &&
            slot.resolvedStageMask == 0u &&
            (slot.submittedStageMask & mask) == 0u;
    }

    [[nodiscard]] inline bool SubmitVisibilityTimingStage(
        VisibilityTimingSlot& slot,
        VisibilityTimingStage stage) noexcept
    {
        if (!VisibilityTimingCanSubmitStage(slot, stage))
            return false;
        slot.submittedStageMask |= VisibilityTimingStageMask(stage);
        return true;
    }

    [[nodiscard]] inline constexpr float VisibilityTimingMillisecondsOrZero(
        const VisibilityTimingSlot& slot,
        VisibilityTimingStage stage) noexcept
    {
        const std::uint32_t mask = VisibilityTimingStageMask(stage);
        const std::size_t index = static_cast<std::size_t>(stage);
        return mask != 0u && (slot.submittedStageMask & mask) != 0u
            ? slot.resolvedStageMilliseconds[index]
            : 0.f;
    }

    [[nodiscard]] inline VisibilityTimingResolution
        ResolveVisibilityTimingStage(
            VisibilityTimingSlot& slot,
            VisibilityTimingStage stage,
            float milliseconds) noexcept
    {
        VisibilityTimingResolution result;
        const std::uint32_t mask = VisibilityTimingStageMask(stage);
        if (mask == 0u ||
            (slot.submittedStageMask & mask) == 0u ||
            (slot.resolvedStageMask & mask) != 0u)
        {
            return result;
        }

        const std::size_t index = static_cast<std::size_t>(stage);
        slot.resolvedStageMilliseconds[index] = milliseconds;
        slot.resolvedStageMask |= mask;
        if (slot.resolvedStageMask != slot.submittedStageMask)
        {
            result.status = VisibilityTimingResolveStatus::Pending;
            return result;
        }

        result.snapshot.firstTraceMs = VisibilityTimingMillisecondsOrZero(
            slot,
            VisibilityTimingStage::FirstTrace);
        result.snapshot.reconstructionMs =
            VisibilityTimingMillisecondsOrZero(
                slot,
                VisibilityTimingStage::Reconstruction);
        result.snapshot.compositionMs = VisibilityTimingMillisecondsOrZero(
            slot,
            VisibilityTimingStage::Composition);
        result.snapshot.effectEnvelopeMs =
            VisibilityTimingMillisecondsOrZero(
                slot,
                VisibilityTimingStage::EffectEnvelope);
        result.snapshot.available = true;
        result.status = VisibilityTimingResolveStatus::Published;
        slot = {};
        return result;
    }
}
