#pragma once

#include "renderer_frustum.h"
#include "renderer_view_contract.h"

namespace uvsr
{
    struct RendererViewport
    {
        float minX = 0, maxX = 0;
        float minY = 0, maxY = 0;
        float minZ = 0, maxZ = 1;
    };

    struct RendererViewExtent
    {
        int32_t minX = 0, maxX = 0;
        int32_t minY = 0, maxY = 0;
        [[nodiscard]] int32_t width() const noexcept { return maxX - minX; }
        [[nodiscard]] int32_t height() const noexcept { return maxY - minY; }
    };

    // the frame owns one complete planar view. passes borrow it synchronously;
    // shader constants are copied into command-owned storage before another update.
    struct RendererView
    {
        RendererViewConstants constants{};
        RendererSceneFrustum frustum{};
        RendererViewport viewport{};
        RendererViewExtent extent{};
        bool reverseDepth = false;
        bool mirrored = false;
        bool valid = false;
    };

    // no allocation. invalid or singular inputs leave the published view unchanged.
    [[nodiscard]] bool BuildRendererView(const RendererViewport& viewport,
        const gpu_contract::Float4x4& worldToView, const gpu_contract::Float4x4& viewToClip,
        gpu_contract::Float2 pixelOffset, RendererView& result) noexcept;

    [[nodiscard]] gpu_contract::Float4x4 RendererClipToTranslatedWorld(const RendererView& view) noexcept;

    [[nodiscard]] gpu_contract::Float4x4 RendererPerspectiveReverseDepth(
        float verticalFovRadians, float aspect, float nearPlane) noexcept;
}
