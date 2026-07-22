#include "scene_light_names.h"

#include <iostream>
#include <string_view>

namespace
{
    bool ExpectName(
        std::string_view source,
        std::string_view expected)
    {
        const std::string normalized =
            uvsr::NormalizeSceneLightName(source);
        if (normalized == expected)
            return true;

        std::cerr << "FAIL: '" << source << "' normalized to '"
                  << normalized << "' instead of '" << expected << "'.\n";
        return false;
    }
}

int main()
{
    bool passed = true;
    passed &= ExpectName("HDRI_SKY", "hdri_sky_1");
    passed &= ExpectName("hdri_sky", "hdri_sky_1");
    passed &= ExpectName("HdRi_SkY_1", "hdri_sky_1");
    passed &= ExpectName("SUN", "sun_1");
    passed &= ExpectName("Sun_1", "sun_1");
    passed &= ExpectName(
        "lamp_light_1st_floor_12",
        "lamp_light_1st_floor_12");

    if (!passed)
        return 1;

    std::cout << "Scene light names normalized successfully.\n";
    return 0;
}
