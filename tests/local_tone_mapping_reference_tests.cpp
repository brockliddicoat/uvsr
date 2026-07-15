#include "local_tone_mapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Local tone-mapping validation failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool Near(float actual, float expected, float tolerance = 1e-5f)
    {
        return std::isfinite(actual) &&
            std::abs(actual - expected) <= tolerance;
    }
}

int main()
{
    using namespace uvsr;

    static_assert(static_cast<uint32_t>(LocalToneMappingQuality::Performance) == 0u);
    static_assert(static_cast<uint32_t>(LocalToneMappingQuality::Balanced) == 1u);
    static_assert(static_cast<uint32_t>(LocalToneMappingQuality::MaximumQuality) == 2u);
    static_assert(static_cast<uint32_t>(LocalToneMappingDebugMode::Off) == 0u);
    static_assert(static_cast<uint32_t>(LocalToneMappingDebugMode::GridConfidence) == 4u);

    const LocalToneMappingSettings defaults;
    Require(!defaults.enabled, "factory default is disabled");
    Require(Near(defaults.strength, 0.35f), "default strength");
    Require(Near(defaults.radiusAt1080p, 64.f), "default radius");
    Require(Near(defaults.edgePreservation, 0.70f), "default edge preservation");
    Require(Near(defaults.shadowRecovery, 0.70f), "default shadow recovery");
    Require(Near(defaults.highlightCompression, 0.70f),
        "default highlight compression");
    Require(Near(defaults.maximumCorrectionEV, 1.25f),
        "default maximum correction");
    Require(Near(defaults.referenceResponseMs, 200.f),
        "default reference response");
    Require(defaults.quality == LocalToneMappingQuality::Balanced,
        "default quality");
    Require(defaults.debugMode == LocalToneMappingDebugMode::Off,
        "default debug mode");

    LocalToneMappingSettings settings = defaults;
    settings.enabled = true;

    // Uniform finite input has the same scalar and local log luminance, so it
    // must never manufacture spatial contrast at any scene level.
    for (float luminance : { 1e-5f, 0.01f, 1.f, 16.f, 65536.f })
    {
        const float logY = ComputeLocalToneMappingLogLuminance(
            luminance, luminance, luminance);
        const float base = ComputeLocalToneMappingBaseDeltaEV(logY, logY);
        Require(Near(base, 0.f), "uniform input has zero base EV");
        Require(Near(ComputeLocalToneMappingCorrectionEV(
            base, 1.f, settings), 0.f),
            "uniform input has zero final EV");
    }

    const float darkBase = ComputeLocalToneMappingBaseDeltaEV(0.f, -4.f);
    Require(darkBase > 0.f, "dark local region has a positive base correction");
    Require(ComputeLocalToneMappingCorrectionEV(
        darkBase, 1.f, settings) > 0.f,
        "dark local region is brightened");
    LocalToneMappingSettings noShadows = settings;
    noShadows.shadowRecovery = 0.f;
    Require(Near(ComputeLocalToneMappingCorrectionEV(
        darkBase, 1.f, noShadows), 0.f),
        "zero shadow recovery suppresses positive correction");

    const float brightBase = ComputeLocalToneMappingBaseDeltaEV(0.f, 4.f);
    Require(brightBase < 0.f, "bright local region has a negative base correction");
    Require(ComputeLocalToneMappingCorrectionEV(
        brightBase, 1.f, settings) < 0.f,
        "bright local region is compressed");
    LocalToneMappingSettings noHighlights = settings;
    noHighlights.highlightCompression = 0.f;
    Require(Near(ComputeLocalToneMappingCorrectionEV(
        brightBase, 1.f, noHighlights), 0.f),
        "zero highlight compression suppresses negative correction");

    LocalToneMappingSettings zeroStrength = settings;
    zeroStrength.strength = 0.f;
    Require(Near(ComputeLocalToneMappingCorrectionEV(
        32.f, 1.f, zeroStrength), 0.f),
        "strength zero produces exact zero correction");
    Require(Near(ComputeMaximumPositiveLocalCorrectionEV(zeroStrength), 0.f),
        "strength zero contributes no visibility bound");

    LocalToneMappingSettings tightClamp = settings;
    tightClamp.maximumCorrectionEV = 0.4f;
    Require(Near(ComputeLocalToneMappingCorrectionEV(
        32.f, 1.f, tightClamp), 0.4f),
        "positive correction obeys maximum EV");
    Require(Near(ComputeLocalToneMappingCorrectionEV(
        -32.f, 1.f, tightClamp), -0.4f),
        "negative correction obeys maximum EV");

    // Exhaust the complete finite analysis delta range over representative
    // settings. The visibility gate's scalar bound must never underestimate a
    // positive correction that the reference display equation can produce.
    for (float strength : { 0.05f, 0.35f, 1.f })
    {
        for (float shadowRecovery : { 0.1f, 0.7f, 1.f })
        {
            for (float maximumCorrection : { 0.1f, 1.25f, 2.5f })
            {
                LocalToneMappingSettings candidate = settings;
                candidate.strength = strength;
                candidate.shadowRecovery = shadowRecovery;
                candidate.maximumCorrectionEV = maximumCorrection;
                const float bound =
                    ComputeMaximumPositiveLocalCorrectionEV(candidate);
                for (uint32_t deltaStep = 0u; deltaStep <= 320u; ++deltaStep)
                {
                    const float delta = float(deltaStep) * 0.1f;
                    for (float confidence : { 0.f, 0.25f, 0.5f, 1.f })
                    {
                        const float correction =
                            ComputeLocalToneMappingCorrectionEV(
                                delta, confidence, candidate);
                        Require(correction <= bound + 1e-6f,
                            "maximum positive EV bound is conservative");
                    }
                }
            }
        }
    }
    Require(Near(ComputeMaximumPositiveLocalCorrectionEV(settings, false), 0.f),
        "ineligible presentation contributes no visibility bound");

    LocalToneMappingSettings debugBypass = settings;
    debugBypass.debugMode = LocalToneMappingDebugMode::FinalLocalEV;
    Require(Near(ComputeLocalToneMappingCorrectionEV(
        darkBase, 1.f, debugBypass), 0.f),
        "debug presentation bypasses normal local exposure");
    Require(Near(ComputeMaximumPositiveLocalCorrectionEV(debugBypass), 0.f),
        "debug presentation contributes no visibility bound");

    LocalToneMappingSettings invalid;
    invalid.strength = std::numeric_limits<float>::quiet_NaN();
    invalid.radiusAt1080p = std::numeric_limits<float>::infinity();
    invalid.edgePreservation = -3.f;
    invalid.shadowRecovery = 2.f;
    invalid.highlightCompression = -2.f;
    invalid.maximumCorrectionEV = 99.f;
    invalid.referenceResponseMs = -10.f;
    invalid.quality = static_cast<LocalToneMappingQuality>(999u);
    invalid.debugMode = static_cast<LocalToneMappingDebugMode>(999u);
    SanitizeLocalToneMappingSettings(invalid);
    Require(Near(invalid.strength, defaults.strength),
        "NaN strength sanitizes to the documented default");
    Require(Near(invalid.radiusAt1080p, defaults.radiusAt1080p),
        "infinite radius sanitizes to the documented default");
    Require(Near(invalid.edgePreservation, 0.f),
        "edge preservation clamps low");
    Require(Near(invalid.shadowRecovery, 1.f), "shadow recovery clamps high");
    Require(Near(invalid.highlightCompression, 0.f),
        "highlight compression clamps low");
    Require(Near(invalid.maximumCorrectionEV, 2.5f),
        "maximum correction clamps high");
    Require(Near(invalid.referenceResponseMs, 0.f),
        "reference response clamps low");
    Require(invalid.quality == LocalToneMappingQuality::Balanced,
        "invalid quality sanitizes to Balanced");
    Require(invalid.debugMode == LocalToneMappingDebugMode::Off,
        "invalid debug mode sanitizes to Off");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    Require(Near(ComputeLocalToneMappingLogLuminance(-1.f, -2.f, -3.f),
        LocalToneMappingMinEV), "negative RGB becomes finite minimum luminance");
    Require(Near(ComputeLocalToneMappingLogLuminance(nan, 1.f, 1.f),
        LocalToneMappingMinEV), "NaN RGB cannot propagate");
    Require(Near(ComputeLocalToneMappingLogLuminance(infinity, 1.f, 1.f),
        LocalToneMappingMinEV), "infinite RGB cannot propagate");
    Require(Near(ComputeLocalToneMappingRangeCoordinate(nan), 0.f),
        "invalid log luminance has a finite range coordinate");
    Require(Near(ComputeLocalToneMappingRangeCoordinate(LocalToneMappingMinEV), 0.f),
        "minimum EV maps to the first range coordinate");
    Require(Near(ComputeLocalToneMappingRangeCoordinate(0.f), 0.5f),
        "zero EV maps to the middle range coordinate");
    Require(Near(ComputeLocalToneMappingRangeCoordinate(LocalToneMappingMaxEV), 1.f),
        "maximum EV maps to the last range coordinate");
    Require(EncodeLocalToneMappingLogLuminance(LocalToneMappingMinEV) == 0u,
        "minimum EV encodes to zero");
    Require(EncodeLocalToneMappingLogLuminance(LocalToneMappingMaxEV) ==
        uint32_t((LocalToneMappingMaxEV - LocalToneMappingMinEV) *
            LocalToneMappingFixedPointScale),
        "maximum EV uses the documented 12-bit fixed-point scale");
    Require(Near(ComputeLocalToneMappingCorrectionEV(
        nan, 1.f, settings), 0.f),
        "non-finite base correction cannot propagate");
    Require(Near(ComputeLocalToneMappingCorrectionEV(
        darkBase, infinity, settings), 0.f),
        "non-finite confidence cannot propagate");

    const std::array<dm::uint2, 9> resolutions = {
        dm::uint2(1u, 1u),
        dm::uint2(16u, 9u),
        dm::uint2(320u, 200u),
        dm::uint2(1280u, 720u),
        dm::uint2(1920u, 1080u),
        dm::uint2(2560u, 1440u),
        dm::uint2(3840u, 2160u),
        dm::uint2(LocalToneMappingMaximum2DTextureDimension, 1u),
        dm::uint2(1u, LocalToneMappingMaximum2DTextureDimension)
    };
    const std::array<LocalToneMappingQuality, 3> qualities = {
        LocalToneMappingQuality::Performance,
        LocalToneMappingQuality::Balanced,
        LocalToneMappingQuality::MaximumQuality
    };

    uint64_t worstFixedPointAccumulation = 0u;
    for (LocalToneMappingQuality quality : qualities)
    {
        for (dm::uint2 resolution : resolutions)
        {
            const LocalToneMappingGridConfiguration configuration =
                ComputeLocalToneMappingGridConfiguration(
                    resolution.x, resolution.y, quality);
            Require(all(configuration.gridDimensions > dm::uint3::zero()),
                "all grid dimensions remain nonzero");
            Require(configuration.gridDimensions.x <=
                LocalToneMappingMaximum3DTextureDimension,
                "grid width obeys D3D12 Texture3D limits");
            Require(configuration.gridDimensions.y <=
                LocalToneMappingMaximum3DTextureDimension,
                "grid height obeys D3D12 Texture3D limits");
            Require(IsLocalToneMappingFixedPointAccumulationSafe(configuration),
                "fixed-point cell accumulation cannot overflow");
            worstFixedPointAccumulation = std::max(
                worstFixedPointAccumulation,
                ComputeLocalToneMappingMaximumFixedPointAccumulation(configuration));
        }

        const auto at1080 = ComputeLocalToneMappingGridConfiguration(
            1920u, 1080u, quality);
        const auto at4k = ComputeLocalToneMappingGridConfiguration(
            3840u, 2160u, quality);
        Require(all(at1080.gridDimensions == at4k.gridDimensions),
            "height scaling keeps 1080p and 4K grid costs stable");
    }

    // Scan every supported viewport height and both normal/maximal aspect
    // cases. This is the executable overflow proof for scale-rounding edges.
    for (LocalToneMappingQuality quality : qualities)
    {
        for (uint32_t height = 1u;
            height <= LocalToneMappingMaximum2DTextureDimension;
            ++height)
        {
            for (uint32_t width : {
                std::min(LocalToneMappingMaximum2DTextureDimension,
                    std::max(1u, uint32_t(std::llround(double(height) * 16.0 / 9.0)))),
                LocalToneMappingMaximum2DTextureDimension })
            {
                const auto configuration =
                    ComputeLocalToneMappingGridConfiguration(width, height, quality);
                const uint64_t accumulation =
                    ComputeLocalToneMappingMaximumFixedPointAccumulation(configuration);
                Require(accumulation <= std::numeric_limits<uint32_t>::max(),
                    "exhaustive supported-height overflow bound");
                worstFixedPointAccumulation = std::max(
                    worstFixedPointAccumulation, accumulation);
            }
        }
    }
    Require(worstFixedPointAccumulation < 11000000ull,
        "analytical fixed-point bound retains a large uint32 safety margin");

    LocalToneMappingSettings presetSettings;
    ApplyLocalToneMappingQualityPreset(
        presetSettings, LocalToneMappingQuality::MaximumQuality);
    Require(presetSettings.quality == LocalToneMappingQuality::MaximumQuality,
        "quality preset selects Maximum Quality");
    const auto maximumPreset = GetLocalToneMappingQualityParameters(
        LocalToneMappingQuality::MaximumQuality);
    Require(maximumPreset.spatialCellSizeAt1080p == 16u &&
        maximumPreset.samplingStrideAt1080p == 4u &&
        maximumPreset.rangeBinCount == 64u,
        "Maximum Quality preset dimensions");

    Require(ComputeLocalToneMappingRangeSigmaBins(0.f) >
        ComputeLocalToneMappingRangeSigmaBins(0.5f),
        "edge preservation monotonically narrows range blur");
    Require(ComputeLocalToneMappingRangeSigmaBins(0.5f) >
        ComputeLocalToneMappingRangeSigmaBins(1.f),
        "edge preservation remains monotonic at the high end");
    Require(Near(ComputeLocalToneMappingReferenceCurrentWeight(
        1.f / 60.f, 0.f), 1.f),
        "zero response uses the current reference immediately");
    const float responseWeight = ComputeLocalToneMappingReferenceCurrentWeight(
        1.f / 60.f, 200.f);
    Require(responseWeight > 0.f && responseWeight < 1.f,
        "positive reference response yields a bounded temporal weight");

    LocalToneMappingSettings disabled = settings;
    disabled.enabled = false;
    Require(Near(ComputeLocalToneMappingCorrectionEV(
        darkBase, 1.f, disabled), 0.f),
        "disabled path has no local correction");
    Require(Near(ComputeMaximumPositiveLocalCorrectionEV(disabled), 0.f),
        "disabled path has no local-grid visibility dependency");
    Require(Near(ComputeMaximumPositiveLocalCorrectionEV(noShadows), 0.f),
        "zero shadow recovery contributes no visibility bound");

    std::cout << "UVSR local tone-mapping reference validation passed; "
        "worst fixed-point cell sum = " << worstFixedPointAccumulation << '\n';
    return EXIT_SUCCESS;
}
