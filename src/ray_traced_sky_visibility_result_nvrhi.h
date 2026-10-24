#pragma once

#include "renderer_receiver_texture_contract_nvrhi.h"

#include <cstdint>

namespace uvsr
{
    struct RayTracedSkyVisibilityResult
    {


        nvrhi::ITexture* visibility = nullptr;


        bool dispatched = false;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return visibility != nullptr &&
                dispatched;
        }
    };
}
