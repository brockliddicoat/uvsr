#include "color_lut.h"

#include <cmath>
#include <locale>
#include <sstream>

namespace uvsr
{
    bool ReadColorLut(std::istream& stream, ColorLutData& result, std::string& error)
    {
        ColorLutData candidate;
        bool hasSize = false, hasMin = false, hasMax = false;
        const auto fail = [&](const char* message) { error = message; return false; };
        const auto finished = [](std::istringstream& line) {
            line >> std::ws;
            return line.eof();
        };
        std::string text;
        while (std::getline(stream, text))
        {
            text.erase(text.find('#') == std::string::npos ? text.size() : text.find('#'));
            std::istringstream line(text);
            line.imbue(std::locale::classic());
            std::string key;
            if (!(line >> key))
                continue;
            if (key == "TITLE")
            {
                if (!candidate.values.empty())
                    return fail("LUT headers must precede table values");
                continue;
            }
            if (key == "LUT_3D_SIZE")
            {
                if (hasSize || !candidate.values.empty() || !(line >> candidate.size) ||
                    candidate.size < 2 || candidate.size > 128 || !finished(line))
                    return fail("LUT size must be a single integer from 2 to 128");
                hasSize = true;
                candidate.values.reserve(size_t(candidate.size) * candidate.size * candidate.size);
                continue;
            }
            if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX")
            {
                bool& seen = key == "DOMAIN_MIN" ? hasMin : hasMax;
                auto& domain = key == "DOMAIN_MIN" ? candidate.domainMin : candidate.domainMax;
                if (seen || !candidate.values.empty())
                    return fail("LUT domain headers must occur once before table values");
                for (float& channel : domain)
                    if (!(line >> channel) || !std::isfinite(channel))
                        return fail("LUT domain must contain three finite values");
                if (!finished(line))
                    return fail("LUT domain contains extra values");
                seen = true;
                continue;
            }
            if (!hasSize)
                return fail("LUT requires a 3D size before its table");
            std::istringstream values(text);
            values.imbue(std::locale::classic());
            std::array<float, 4> value{ 0.f, 0.f, 0.f, 1.f };
            for (size_t channel = 0; channel < 3; ++channel)
                if (!(values >> value[channel]) || !std::isfinite(value[channel]))
                    return fail("LUT table must contain finite RGB triples");
            if (!finished(values) || candidate.values.size() >=
                size_t(candidate.size) * candidate.size * candidate.size)
                return fail("LUT table contains extra values");
            candidate.values.push_back(value);
        }
        if (stream.bad() || !hasSize || candidate.values.size() !=
            size_t(candidate.size) * candidate.size * candidate.size)
            return fail("LUT table is incomplete");
        for (size_t channel = 0; channel < 3; ++channel)
            if (!(candidate.domainMax[channel] > candidate.domainMin[channel]))
                return fail("LUT domain maximum must exceed its minimum");
        result = std::move(candidate);
        error.clear();
        return true;
    }
}
