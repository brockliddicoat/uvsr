#include "taa_miniengine_reference.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
    bool NearlyEqual(float actual, float expected, float epsilon = 1e-6f)
    {
        return std::abs(actual - expected) <= epsilon;
    }

    bool Check(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << "FAIL: " << message << '\n';
        return condition;
    }
}

int main()
{
    bool passed = true;

    constexpr uvsr::MiniEngineTaaJitterSample expected[8] = {
        { -0.5f, -0.5f },
        { 0.0f, -1.0f / 6.0f },
        { -0.25f, 1.0f / 6.0f },
        { 0.25f, -7.0f / 18.0f },
        { -0.375f, -1.0f / 18.0f },
        { 0.125f, 5.0f / 18.0f },
        { -0.125f, -5.0f / 18.0f },
        { 0.375f, 1.0f / 18.0f }
    };

    for (uint64_t frame = 0u; frame < 16u; ++frame)
    {
        const uvsr::MiniEngineTaaJitterSample sample =
            uvsr::GetMiniEngineTaaJitter(frame);
        const auto& reference = expected[frame % 8u];
        passed &= Check(
            NearlyEqual(sample.x, reference.x) &&
                NearlyEqual(sample.y, reference.y),
            "MiniEngine Halton phase or centering changed");
    }

    passed &= Check(
        uvsr::GetMiniEngineTaaHistoryBytes(1920u, 1080u) == 49'766'400u,
        "1080p history payload must remain exactly 24 bytes per pixel");
    passed &= Check(
        uvsr::GetMiniEngineTaaHistoryBytes(3840u, 2160u) == 199'065'600u,
        "4K history payload must remain exactly 24 bytes per pixel");

    constexpr uvsr::MiniEngineTaaSharpenWeights defaultWeights =
        uvsr::GetMiniEngineTaaSharpenWeights(
            uvsr::MiniEngineTaaDefaultSharpness);
    passed &= Check(
        NearlyEqual(defaultWeights.center, 1.5f) &&
            NearlyEqual(defaultWeights.lateral, 0.125f),
        "MiniEngine sharpness 0.5 must retain its reference weights");
    passed &= Check(
        NearlyEqual(uvsr::ClampMiniEngineTaaSharpness(-1.f), 0.f) &&
            NearlyEqual(uvsr::ClampMiniEngineTaaSharpness(2.f), 1.f),
        "MiniEngine sharpness control must stay in its reference range");
    passed &= Check(
        uvsr::ShouldSharpenMiniEngineTaa(true, 0.001f) &&
            !uvsr::ShouldSharpenMiniEngineTaa(true, 0.f) &&
            !uvsr::ShouldSharpenMiniEngineTaa(false, 0.5f),
        "disabled or zero sharpness must select MiniEngine's resolve path");

    passed &= Check(
        uvsr::IsMiniEngineTaaAvailable(true, true, true),
        "deferred PBR must support MiniEngine TAA");
    passed &= Check(
        !uvsr::IsMiniEngineTaaAvailable(true, true, false) &&
            !uvsr::IsMiniEngineTaaAvailable(true, false, true) &&
            !uvsr::IsMiniEngineTaaAvailable(false, true, true),
        "unsupported or disabled modes must not claim a valid TAA contract");

    if (!passed)
        return 1;

    std::cout << "MiniEngine TAA reference tests passed\n";
    return 0;
}
