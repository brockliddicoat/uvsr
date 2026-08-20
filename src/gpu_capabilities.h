#pragma once

#include <cstdint>

#ifndef UVSR_REQUIRED_SHADER_MODEL
#error UVSR_REQUIRED_SHADER_MODEL must match the ShaderMake target profile.
#endif

namespace uvsr
{
    constexpr uint32_t MinimumShaderModel = UVSR_REQUIRED_SHADER_MODEL;

    [[nodiscard]] constexpr bool SupportsRequiredShaderModel(
        uint32_t highestShaderModel) noexcept
    {
        return highestShaderModel >= MinimumShaderModel;
    }
}
