#pragma once

#include <array>
#include <cstdint>

namespace uvsr
{
    enum class VisibilitySampleRotationMode : uint32_t
    {
        Disabled,
        QuarterRateFourPhase
    };

    struct VisibilitySampleRotationState
    {
        VisibilitySampleRotationMode mode =
            VisibilitySampleRotationMode::Disabled;
        uint32_t phaseIndex = 0u;
        uint32_t phaseCount = 1u;
        uint32_t outputPixelOffsetX = 0u;
        uint32_t outputPixelOffsetY = 0u;
        float centeredOutputPixelOffsetX = 0.f;
        float centeredOutputPixelOffsetY = 0.f;

        [[nodiscard]] bool IsEnabled() const
        {
            return mode != VisibilitySampleRotationMode::Disabled;
        }
    };

    struct VisibilityFullPixel
    {
        uint32_t x = 0u;
        uint32_t y = 0u;
    };

    // UVSR's visibility "Half" resolution is one dense receiver per output
    // 2x2 block: one quarter of the output-pixel count. The sequence maximizes
    // the distance between its first two phases, then visits the remaining
    // diagonal. Coordinates are integer pixel indices inside each 2x2 block in
    // D3D output-space orientation: top-left, bottom-right, top-right,
    // bottom-left. Their centered output-pixel offsets sum to zero.
    inline constexpr std::array<std::array<uint32_t, 2>, 4>
        VisibilityQuarterRateSampleOffsets = {{
            {{ 0u, 0u }},
            {{ 1u, 1u }},
            {{ 1u, 0u }},
            {{ 0u, 1u }}
        }};

    [[nodiscard]] inline VisibilitySampleRotationState
        ResolveVisibilitySampleRotationState(
            bool experimentalRotationRequested,
            uint32_t resolutionScale,
            uint32_t fullWidth,
            uint32_t fullHeight,
            uint32_t sequenceIndex)
    {
        resolutionScale = resolutionScale > 0u ? resolutionScale : 1u;

        VisibilitySampleRotationState state;
        // Preserve UVSR's exact legacy receiver for every disabled or
        // unsupported configuration. For even scales this is the lower-right
        // pixel nearest the mathematical block center.
        state.outputPixelOffsetX = resolutionScale / 2u;
        state.outputPixelOffsetY = resolutionScale / 2u;

        // A clamped receiver in a partial edge block no longer has the same
        // centered phase as an interior receiver. Fall back for odd extents so
        // the global reconstruction registration remains exact everywhere.
        const bool supportedExtent = resolutionScale == 2u &&
            fullWidth > 0u && fullHeight > 0u &&
            (fullWidth % resolutionScale) == 0u &&
            (fullHeight % resolutionScale) == 0u;
        if (!experimentalRotationRequested || !supportedExtent)
            return state;

        state.mode = VisibilitySampleRotationMode::QuarterRateFourPhase;
        state.phaseCount = uint32_t(VisibilityQuarterRateSampleOffsets.size());
        state.phaseIndex = sequenceIndex % state.phaseCount;
        state.outputPixelOffsetX =
            VisibilityQuarterRateSampleOffsets[state.phaseIndex][0];
        state.outputPixelOffsetY =
            VisibilityQuarterRateSampleOffsets[state.phaseIndex][1];
        state.centeredOutputPixelOffsetX =
            float(state.outputPixelOffsetX) - 0.5f;
        state.centeredOutputPixelOffsetY =
            float(state.outputPixelOffsetY) - 0.5f;
        return state;
    }

    [[nodiscard]] inline uint64_t
        GetVisibilitySampleRotationHistoryConvention(
            const VisibilitySampleRotationState& state)
    {
        // Per-frame phase is deliberately excluded. Low-resolution temporal
        // and adaptive resources store one block estimate, so all four phases
        // belong to one history convention. Only crossing between the legacy
        // and rotating conventions invalidates that block history.
        return (uint64_t(state.mode) << 32u) | uint64_t(state.phaseCount);
    }

    [[nodiscard]] inline uint32_t
        PackVisibilitySampleRotationOutputPixelOffset(
            const VisibilitySampleRotationState& state)
    {
        // Supported visibility scales need only a few bits. Two 16-bit lanes
        // keep the constant contract explicit without growing the buffer.
        return (state.outputPixelOffsetX & 0xffffu) |
            ((state.outputPixelOffsetY & 0xffffu) << 16u);
    }

    [[nodiscard]] inline VisibilityFullPixel MapVisibilitySampleToFullPixel(
        uint32_t samplingPixelX,
        uint32_t samplingPixelY,
        uint32_t resolutionScale,
        uint32_t fullWidth,
        uint32_t fullHeight,
        const VisibilitySampleRotationState& state)
    {
        resolutionScale = resolutionScale > 0u ? resolutionScale : 1u;
        const uint32_t maximumX = fullWidth > 0u ? fullWidth - 1u : 0u;
        const uint32_t maximumY = fullHeight > 0u ? fullHeight - 1u : 0u;
        const uint64_t mappedX = uint64_t(samplingPixelX) * resolutionScale +
            state.outputPixelOffsetX;
        const uint64_t mappedY = uint64_t(samplingPixelY) * resolutionScale +
            state.outputPixelOffsetY;
        return {
            mappedX < maximumX ? uint32_t(mappedX) : maximumX,
            mappedY < maximumY ? uint32_t(mappedY) : maximumY
        };
    }

    class VisibilitySampleRotationSequence
    {
    public:
        [[nodiscard]] VisibilitySampleRotationState Resolve(
            bool experimentalRotationRequested,
            uint32_t resolutionScale,
            uint32_t fullWidth,
            uint32_t fullHeight) const
        {
            return ResolveVisibilitySampleRotationState(
                experimentalRotationRequested,
                resolutionScale,
                fullWidth,
                fullHeight,
                m_SequenceIndex);
        }

        void Advance(const VisibilitySampleRotationState& state)
        {
            m_SequenceIndex = state.IsEnabled() && state.phaseCount > 0u
                ? (state.phaseIndex + 1u) % state.phaseCount
                : 0u;
        }

        void Reset()
        {
            m_SequenceIndex = 0u;
        }

        [[nodiscard]] uint32_t GetIndex() const
        {
            return m_SequenceIndex;
        }

    private:
        uint32_t m_SequenceIndex = 0u;
    };
}
