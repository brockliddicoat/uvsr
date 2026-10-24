#pragma once

#include <cstdint>

namespace uvsr
{
    enum class DenoiserSignalType : uint8_t
    {
        AmbientOcclusion, DiffuseGi, SkyVisibility, SunShadow, FlashlightShadow, PathTracing, Count
    };
}
