#include "lighting_accumulation_contract.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    bool Near(float actual, float expected, float tolerance = 1e-5f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    bool Near(
        LightingAccumulationFloat4 actual,
        LightingAccumulationFloat4 expected,
        float tolerance = 1e-5f)
    {
        return Near(actual.x, expected.x, tolerance) &&
            Near(actual.y, expected.y, tolerance) &&
            Near(actual.z, expected.z, tolerance) &&
            Near(actual.w, expected.w, tolerance);
    }

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Lighting accumulation semantic validation failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    Require(
        ResolveLightingAccumulationAttemptToken(0u, false) == 1u &&
            ResolveLightingAccumulationAttemptToken(17u, false) == 18u &&
            ResolveLightingAccumulationAttemptToken(
                UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT,
                false) == UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT &&
            ResolveLightingAccumulationAttemptToken(
                UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT,
                true) == 1u,
        "attempt token no longer encodes accepted-count plus one safely");

    const LightingAccumulationState reset = RepairLightingAccumulation(
        { 1.0f, 2.0f, 3.0f, 1.0f },
        9u,
        true);
    const LightingAccumulationState empty = RepairLightingAccumulation(
        { 9.0f, 9.0f, 9.0f, 9.0f },
        0u,
        false);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const LightingAccumulationState repaired = RepairLightingAccumulation(
        { nan, 2.0f, 3.0f, 1.0f },
        9u,
        false);
    Require(
        reset.count == 0u && empty.count == 0u && repaired.count == 0u &&
            reset.publish != 0u && empty.publish != 0u &&
            repaired.publish != 0u &&
            Near(reset.mean, { 0.0f, 0.0f, 0.0f, 0.0f }) &&
            Near(empty.mean, { 0.0f, 0.0f, 0.0f, 0.0f }) &&
            Near(repaired.mean, { 0.0f, 0.0f, 0.0f, 0.0f }),
        "reset, empty, or non-finite history repair changed");

    const LightingAccumulationState prior = RepairLightingAccumulation(
        { 2.0f, 4.0f, 6.0f, 1.0f },
        3u,
        false);
    const LightingAccumulationState skipped =
        ResolveLightingAccumulationCandidate(
            prior,
            0u,
            { 9.0f, 9.0f, 9.0f, 1.0f });
    const LightingAccumulationState invalid =
        ResolveLightingAccumulationCandidate(
            prior,
            4u,
            { 9.0f, nan, 9.0f, 1.0f });
    Require(
        skipped.attempted == 0u && skipped.accepted == 0u &&
            skipped.publish != 0u && skipped.count == prior.count &&
            Near(skipped.mean, prior.mean) &&
            invalid.attempted != 0u && invalid.accepted == 0u &&
            invalid.publish != 0u && invalid.count == prior.count &&
            Near(invalid.mean, prior.mean),
        "skip or invalid candidate failed to publish preserved history");

    LightingAccumulationState history = RepairLightingAccumulation(
        { 0.0f, 0.0f, 0.0f, 0.0f },
        0u,
        false);
    history = ResolveLightingAccumulationCandidate(
        history,
        1u,
        { -2.0f, 4.0f, 8.0f, -7.0f });
    Require(
        history.accepted != 0u && history.count == 1u &&
            Near(history.mean, { 0.0f, 4.0f, 8.0f, 1.0f }),
        "first finite candidate was not clamped and accepted exactly once");
    history = ResolveLightingAccumulationCandidate(
        history,
        2u,
        { 2.0f, 0.0f, 4.0f, 0.0f });
    history = ResolveLightingAccumulationCandidate(
        history,
        3u,
        { 4.0f, 2.0f, 0.0f, 0.0f });
    Require(
        history.count == 3u && history.accepted != 0u &&
            Near(history.mean,
                { 2.0f, 2.0f, 4.0f, 1.0f }),
        "cumulative Ray Marching mean diverged from accepted history");

    const LightingAccumulationState terminal =
        ResolveLightingAccumulationCandidate(
            RepairLightingAccumulation(
                { 1.0f, 2.0f, 3.0f, 1.0f },
                UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT,
                false),
            UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT,
            { 9.0f, 9.0f, 9.0f, 1.0f });
    Require(
        terminal.count == UVSR_LIGHTING_ACCUMULATION_TERMINAL_COUNT &&
            terminal.attempted != 0u && terminal.accepted == 0u &&
            terminal.publish != 0u &&
            Near(terminal.mean, { 1.0f, 2.0f, 3.0f, 1.0f }),
        "UINT32 terminal history mutated or stopped publishing");

    return EXIT_SUCCESS;
}
