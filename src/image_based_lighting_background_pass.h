#pragma once

#include <cstdint>

namespace uvsr
{
    enum class ImageBasedLightingBackgroundRenderStatus : std::uint8_t
    {
        Failed,
        Dispatched
    };

    struct ImageBasedLightingBackgroundRenderResult
    {
        ImageBasedLightingBackgroundRenderStatus status =
            ImageBasedLightingBackgroundRenderStatus::Failed;
        std::uint32_t dispatchedViewCount = 0u;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status ==
                ImageBasedLightingBackgroundRenderStatus::Dispatched;
        }
    };

    template <typename RenderView>
    [[nodiscard]] ImageBasedLightingBackgroundRenderResult
        ExecuteImageBasedLightingBackgroundViews(
            bool passReady,
            std::uint32_t viewCount,
            RenderView&& renderView)
    {
        if (!passReady || viewCount == 0u)
            return {};

        std::uint32_t dispatchedViewCount = 0u;
        for (std::uint32_t viewIndex = 0u;
            viewIndex < viewCount;
            ++viewIndex)
        {
            if (!renderView(viewIndex))
            {
                return {
                    ImageBasedLightingBackgroundRenderStatus::Failed,
                    dispatchedViewCount
                };
            }
            ++dispatchedViewCount;
        }
        return {
            ImageBasedLightingBackgroundRenderStatus::Dispatched,
            dispatchedViewCount
        };
    }

}
