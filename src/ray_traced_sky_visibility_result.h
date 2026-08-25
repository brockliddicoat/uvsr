#pragma once

#include "renderer_receiver_texture_contract.h"

#include <cstdint>

namespace uvsr
{
    struct RayTracedSkyVisibilityResult
    {
        // R8_UNORM Texture2D at 1x or Texture2DArray at MSAA, where slice N
        // belongs to raster sample N.
        nvrhi::ITexture* visibility = nullptr;
        // Coherent single-surface signal and optional hit distance selected
        // from the closest covered raster sample for AO/GI and denoising.
        nvrhi::ITexture* closestVisibility = nullptr;
        nvrhi::ITexture* hitDistance = nullptr;
        std::uint32_t receiverSampleCount = 0u;
        bool dispatched = false;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return visibility != nullptr && closestVisibility != nullptr &&
                IsRendererReceiverSampleCountSupported(
                    receiverSampleCount) &&
                dispatched;
        }
    };
}
