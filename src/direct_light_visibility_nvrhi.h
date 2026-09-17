#pragma once

#include "direct_light_visibility.h"

namespace nvrhi { class ITexture; }

namespace uvsr
{
    // frame-local visibility for one exact light; incomplete values are neutral.
    struct DirectLightVisibility
    {
        nvrhi::ITexture* texture = nullptr;
        RendererSceneHandle light;
        [[nodiscard]] constexpr bool IsComplete() const
        {
            return texture != nullptr && bool(light);
        }
    };

    // slot zero belongs to the finite flashlight producer. slot one belongs
    // to the primary directional sun. Either slot may be absent.
    struct DirectLightVisibilities
    {
        DirectLightVisibility flashlight;
        DirectLightVisibility sun;
    };

    [[nodiscard]] constexpr bool TargetsDirectLight(
        const DirectLightVisibility& visibility,
        RendererSceneHandle light)
    {
        return visibility.IsComplete() && visibility.light == light;
    }

}
