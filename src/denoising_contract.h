#pragma once

#include <cstdint>

namespace uvsr
{
    enum class DenoiserSignalType : uint8_t
    {
        AmbientOcclusion, DiffuseGi, SkyVisibility, SunShadow, FlashlightShadow, PathTracing, Count
    };
    enum class DenoiserHistoryReset : uint8_t { None, Restart, ClearAndRestart };
}
