#include "visibility_sample_rotation.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool NearlyEqual(float a, float b)
    {
        return std::abs(a - b) <= 1e-6f;
    }

    void TestDisabledAndUnsupportedModesUseLegacyReceiver()
    {
        for (uint32_t scale : { 0u, 1u, 2u, 4u })
        {
            const auto state = uvsr::ResolveVisibilitySampleRotationState(
                false, scale, 8u, 8u, 123u);
            const uint32_t safeScale = scale > 0u ? scale : 1u;
            Require(!state.IsEnabled(),
                "disabled setting must never resolve to rotation");
            Require(state.phaseIndex == 0u && state.phaseCount == 1u,
                "disabled setting must use the one-phase legacy convention");
            Require(state.outputPixelOffsetX == safeScale / 2u &&
                    state.outputPixelOffsetY == safeScale / 2u,
                "disabled setting must preserve the legacy receiver offset");
            Require(NearlyEqual(state.centeredOutputPixelOffsetX, 0.f) &&
                    NearlyEqual(state.centeredOutputPixelOffsetY, 0.f),
                "disabled setting must report no experimental centered offset");
            const uint32_t packedOffset =
                uvsr::PackVisibilitySampleRotationOutputPixelOffset(state);
            Require((packedOffset & 0xffffu) == state.outputPixelOffsetX &&
                    (packedOffset >> 16u) == state.outputPixelOffsetY,
                "disabled frame constant must preserve the legacy receiver");
        }

        for (uint32_t scale : { 1u, 4u, 8u })
        {
            const auto state = uvsr::ResolveVisibilitySampleRotationState(
                true, scale, 8u, 8u, 3u);
            Require(!state.IsEnabled(),
                "unsupported scales must fall back instead of rotating");
            Require(state.outputPixelOffsetX == scale / 2u &&
                    state.outputPixelOffsetY == scale / 2u,
                "unsupported scales must preserve their legacy receiver");
        }
    }

    void TestQuarterRateSequenceCoverageAndCentering()
    {
        std::array<std::array<bool, 2>, 2> visited{};
        float sumX = 0.f;
        float sumY = 0.f;
        const std::array<std::array<uint32_t, 2>, 4> expected = {{
            {{ 0u, 0u }},
            {{ 1u, 1u }},
            {{ 1u, 0u }},
            {{ 0u, 1u }}
        }};

        uint64_t historyConvention = 0u;
        for (uint32_t phase = 0u; phase < expected.size(); ++phase)
        {
            const auto state = uvsr::ResolveVisibilitySampleRotationState(
                true, 2u, 8u, 8u, phase);
            Require(state.IsEnabled(),
                "scale-two request must resolve to four-phase rotation");
            Require(state.mode ==
                    uvsr::VisibilitySampleRotationMode::QuarterRateFourPhase,
                "scale-two request must use the quarter-rate mode");
            Require(state.phaseIndex == phase && state.phaseCount == 4u,
                "resolved phase metadata must match the sequence");
            Require(state.outputPixelOffsetX == expected[phase][0] &&
                    state.outputPixelOffsetY == expected[phase][1],
                "quarter-rate phase ordering changed unexpectedly");
            Require(NearlyEqual(
                        state.centeredOutputPixelOffsetX,
                        float(expected[phase][0]) - 0.5f) &&
                    NearlyEqual(
                        state.centeredOutputPixelOffsetY,
                        float(expected[phase][1]) - 0.5f),
                "centered output-pixel offsets must match phase positions");
            Require(!visited[state.outputPixelOffsetY]
                            [state.outputPixelOffsetX],
                "each output position must be visited once per cycle");
            visited[state.outputPixelOffsetY][state.outputPixelOffsetX] = true;
            sumX += state.centeredOutputPixelOffsetX;
            sumY += state.centeredOutputPixelOffsetY;

            constexpr float BlockIndex = 7.f;
            const float representedCenterX = BlockIndex * 2.f +
                float(state.outputPixelOffsetX) + 0.5f;
            const float representedCenterY = BlockIndex * 2.f +
                float(state.outputPixelOffsetY) + 0.5f;
            const float reconstructedSamplingX =
                (representedCenterX - state.centeredOutputPixelOffsetX) /
                    2.f - 0.5f;
            const float reconstructedSamplingY =
                (representedCenterY - state.centeredOutputPixelOffsetY) /
                    2.f - 0.5f;
            Require(NearlyEqual(reconstructedSamplingX, BlockIndex) &&
                    NearlyEqual(reconstructedSamplingY, BlockIndex),
                "phase-aware upsample mapping must place the receiver at its "
                "represented output-pixel center");

            const uint64_t convention =
                uvsr::GetVisibilitySampleRotationHistoryConvention(state);
            if (phase == 0u)
                historyConvention = convention;
            else
                Require(convention == historyConvention,
                    "per-frame phases must share one block-history convention");

            const uint32_t packedOffset =
                uvsr::PackVisibilitySampleRotationOutputPixelOffset(state);
            Require((packedOffset & 0xffffu) == state.outputPixelOffsetX &&
                    (packedOffset >> 16u) == state.outputPixelOffsetY,
                "packed frame constant must preserve both receiver coordinates");
        }

        for (const auto& row : visited)
            for (bool value : row)
                Require(value, "four phases must cover the complete 2x2 block");
        Require(NearlyEqual(sumX, 0.f) && NearlyEqual(sumY, 0.f),
            "centered phase offsets must have zero mean over a cycle");

        const auto disabled = uvsr::ResolveVisibilitySampleRotationState(
            false, 2u, 8u, 8u, 0u);
        Require(uvsr::GetVisibilitySampleRotationHistoryConvention(disabled) !=
                historyConvention,
            "toggling rotation must change the history convention");
    }

    void TestDeterminismAndRenderedFrameClock()
    {
        for (uint32_t phase = 0u; phase < 4u; ++phase)
        {
            const auto first = uvsr::ResolveVisibilitySampleRotationState(
                true, 2u, 8u, 8u, phase);
            const auto nextCycle = uvsr::ResolveVisibilitySampleRotationState(
                true, 2u, 8u, 8u, phase + 4u);
            Require(first.phaseIndex == nextCycle.phaseIndex &&
                    first.outputPixelOffsetX == nextCycle.outputPixelOffsetX &&
                    first.outputPixelOffsetY == nextCycle.outputPixelOffsetY,
                "phase sequence must repeat deterministically every four frames");
        }

        const auto wrapped = uvsr::ResolveVisibilitySampleRotationState(
            true, 2u, 8u, 8u, std::numeric_limits<uint32_t>::max());
        Require(wrapped.phaseIndex == 3u,
            "large sequence indices must resolve without signed overflow");

        uvsr::VisibilitySampleRotationSequence sequence;
        const auto frameZero = sequence.Resolve(true, 2u, 8u, 8u);
        const auto repeatedWithoutRender = sequence.Resolve(
            true, 2u, 8u, 8u);
        Require(frameZero.phaseIndex == 0u &&
                repeatedWithoutRender.phaseIndex == 0u,
            "skipped frames must not advance the rendered-sample clock");

        sequence.Advance(frameZero);
        Require(sequence.Resolve(true, 2u, 8u, 8u).phaseIndex == 1u,
            "one rendered sample must advance exactly one phase");
        sequence.Advance(sequence.Resolve(true, 2u, 8u, 8u));
        Require(sequence.Resolve(true, 2u, 8u, 8u).phaseIndex == 2u,
            "rendered-sample clock must preserve sequence ordering");

        sequence.Reset();
        Require(sequence.GetIndex() == 0u &&
                sequence.Resolve(true, 2u, 8u, 8u).phaseIndex == 0u,
            "history reset must restart the rotation cycle safely");

        const auto unsupported = sequence.Resolve(true, 4u, 8u, 8u);
        sequence.Advance(unsupported);
        Require(sequence.GetIndex() == 0u,
            "unsupported modes must keep the experimental clock reset");

        uvsr::VisibilitySampleRotationState invalidEnabledState;
        invalidEnabledState.mode =
            uvsr::VisibilitySampleRotationMode::QuarterRateFourPhase;
        invalidEnabledState.phaseCount = 0u;
        sequence.Advance(invalidEnabledState);
        Require(sequence.GetIndex() == 0u,
            "invalid zero-phase state must fail closed without division by zero");
    }

    void TestOddResolutionFallsBackWithoutMisregistration()
    {
        const std::array<std::array<uint32_t, 2>, 4> unsupportedExtents = {{
            {{ 5u, 4u }},
            {{ 4u, 5u }},
            {{ 5u, 3u }},
            {{ 1u, 1u }}
        }};
        for (const auto& extent : unsupportedExtents)
        {
            const auto state = uvsr::ResolveVisibilitySampleRotationState(
                true, 2u, extent[0], extent[1], 3u);
            Require(!state.IsEnabled(),
                "partial 2x2 edge blocks must use the legacy convention");
            Require(state.outputPixelOffsetX == 1u &&
                    state.outputPixelOffsetY == 1u &&
                    NearlyEqual(state.centeredOutputPixelOffsetX, 0.f) &&
                    NearlyEqual(state.centeredOutputPixelOffsetY, 0.f),
                "odd output fallback must preserve legacy registration");

            const auto boundary = uvsr::MapVisibilitySampleToFullPixel(
                (extent[0] - 1u) / 2u,
                (extent[1] - 1u) / 2u,
                2u,
                extent[0],
                extent[1],
                state);
            Require(boundary.x < extent[0] && boundary.y < extent[1],
                "legacy edge receiver must remain inside an odd output");
        }

        for (uint32_t phase = 0u; phase < 4u; ++phase)
        {
            const auto state = uvsr::ResolveVisibilitySampleRotationState(
                true, 2u, 8u, 8u, phase);
            const auto interior = uvsr::MapVisibilitySampleToFullPixel(
                1u, 1u, 2u, 8u, 8u, state);
            Require(interior.x == 2u + state.outputPixelOffsetX &&
                    interior.y == 2u + state.outputPixelOffsetY,
                "interior receiver mapping must match the resolved phase");
        }

        const auto legacy = uvsr::ResolveVisibilitySampleRotationState(
            false, 2u, 8u, 8u, 0u);
        const auto interior = uvsr::MapVisibilitySampleToFullPixel(
            1u, 2u, 2u, 8u, 8u, legacy);
        Require(interior.x == 3u && interior.y == 5u,
            "disabled mapping must match sampling*scale+scale/2 exactly");
    }
}

int main()
{
    TestDisabledAndUnsupportedModesUseLegacyReceiver();
    TestQuarterRateSequenceCoverageAndCentering();
    TestDeterminismAndRenderedFrameClock();
    TestOddResolutionFallsBackWithoutMisregistration();
    std::cout << "Visibility sample-rotation validation passed\n";
    return EXIT_SUCCESS;
}
