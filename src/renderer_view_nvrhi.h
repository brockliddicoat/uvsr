#pragma once

#include "renderer_view.h"
#include <nvrhi/nvrhi.h>

namespace uvsr
{
    [[nodiscard]] inline nvrhi::ViewportState RendererViewportNvrhi(const RendererView& view)
    {
        const auto& v = view.viewport;
        const auto& r = view.extent;
        return nvrhi::ViewportState()
            .addViewport(nvrhi::Viewport(v.minX, v.maxX, v.minY, v.maxY, v.minZ, v.maxZ))
            .addScissorRect(nvrhi::Rect(r.minX, r.maxX, r.minY, r.maxY));
    }
}
