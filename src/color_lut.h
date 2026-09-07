#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace uvsr
{
    struct ColorLutData
    {
        uint32_t size = 0;
        std::array<float, 3> domainMin{ 0.f, 0.f, 0.f };
        std::array<float, 3> domainMax{ 1.f, 1.f, 1.f };
        std::vector<std::array<float, 4>> values;
    };

    // publish only complete tables, so rejected input cannot replace a valid LUT.
    [[nodiscard]] bool ReadColorLut(std::istream& stream, ColorLutData& result, std::string& error);
}
