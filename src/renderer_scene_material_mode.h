#pragma once

#include "renderer_scene.h"

namespace uvsr
{
    enum class WhiteWorldMode
    {
        Off,
        On,
        PreserveDetail,
        PreserveLighting
    };

    // one UI/loading command owns a checked temporary batch, sized from the
    // published material count. failure preserves every material and revision.
    // authored originals remain authoritative for mode changes and restoration.
    [[nodiscard]] RendererSceneResult ApplyRendererSceneMaterialMode(RendererScene& scene, WhiteWorldMode mode) noexcept;

#if defined(UVSR_BUILD_TESTING)
    void SetRendererSceneMaterialModeAllocationFailure(bool fail) noexcept;
#endif
}
