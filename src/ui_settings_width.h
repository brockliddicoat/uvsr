#pragma once

#include <algorithm>

namespace uvsr
{
    struct OgSettingsWidthInputs
    {
        float windowPaddingX = 0.f;
        float scrollbarSize = 0.f;
        float footerBodyWidth = 0.f;
        float adapterBodyWidth = 0.f;
        float sceneBodyWidth = 0.f;
        float generalChoiceBodyWidth = 0.f;
        float debugBodyWidth = 0.f;
        float nestedHeaderWidth = 0.f;
        float rendererLineWidth = 0.f;
        float statusRow0Width = 0.f;
        float statusRow1Width = 0.f;
    };

    [[nodiscard]] constexpr float ResolveRequiredOgSettingsWidth(
        const OgSettingsWidthInputs& inputs)
    {
        const float bodyContentWidth = std::max(
            std::max(
                std::max(inputs.footerBodyWidth, inputs.adapterBodyWidth),
                std::max(inputs.sceneBodyWidth, inputs.generalChoiceBodyWidth)),
            std::max(inputs.debugBodyWidth, inputs.nestedHeaderWidth));
        const float statusContentWidth = std::max(
            inputs.rendererLineWidth,
            std::max(inputs.statusRow0Width, inputs.statusRow1Width));
        const float bodyOuterWidth =
            inputs.windowPaddingX * 2.f +
            inputs.scrollbarSize +
            bodyContentWidth;
        const float statusOuterWidth =
            inputs.windowPaddingX * 2.f +
            statusContentWidth;
        return std::max(bodyOuterWidth, statusOuterWidth);
    }
}
