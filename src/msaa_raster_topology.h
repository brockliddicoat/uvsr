#pragma once

#include <array>
#include <cstdint>

namespace uvsr
{
    inline constexpr std::uint32_t MsaaMaximumTextureDimension = 16384u;

    struct MsaaRasterTopology
    {
        std::uint32_t presentationSampleCount = 0u;
        std::uint32_t rasterSampleCount = 0u;
        std::uint32_t linearResolutionScale = 0u;

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return presentationSampleCount != 0u &&
                rasterSampleCount != 0u && linearResolutionScale != 0u;
        }

        [[nodiscard]] constexpr std::uint32_t TotalSampleCount() const noexcept
        {
            return rasterSampleCount * linearResolutionScale *
                linearResolutionScale;
        }
    };

    struct MsaaRasterCandidates
    {
        std::array<MsaaRasterTopology, 2> values{};
        std::uint32_t count = 0u;
    };

    [[nodiscard]] constexpr MsaaRasterCandidates
        GetExactMsaaRasterCandidates(std::uint32_t requestedSampleCount)
        noexcept
    {
        MsaaRasterCandidates candidates;
        switch (requestedSampleCount)
        {
        case 1u:
        case 2u:
        case 4u:
        case 8u:
            candidates.values[0] = {
                requestedSampleCount,
                requestedSampleCount,
                1u
            };
            candidates.count = 1u;
            break;
        case 16u:
            // Prefer native 16x. D3D12 hardware that exposes no 16x quality
            // level still gets sixteen distinct raster samples from four
            // native samples in each pixel of a 2x2 scene-linear grid.
            candidates.values[0] = { 16u, 16u, 1u };
            candidates.values[1] = { 16u, 4u, 2u };
            candidates.count = 2u;
            break;
        default:
            break;
        }
        return candidates;
    }

    struct MsaaRenderExtent
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return width != 0u && height != 0u;
        }
    };

    [[nodiscard]] constexpr MsaaRenderExtent ScaleMsaaRenderExtent(
        std::uint32_t presentationWidth,
        std::uint32_t presentationHeight,
        std::uint32_t linearResolutionScale) noexcept
    {
        if (presentationWidth == 0u || presentationHeight == 0u ||
            linearResolutionScale == 0u ||
            presentationWidth >
                MsaaMaximumTextureDimension / linearResolutionScale ||
            presentationHeight >
                MsaaMaximumTextureDimension / linearResolutionScale)
        {
            return {};
        }
        return {
            presentationWidth * linearResolutionScale,
            presentationHeight * linearResolutionScale
        };
    }
}
