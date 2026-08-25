#include "path_tracing_accumulation_contract.h"
#include "path_tracing_bindings.h"
#include "path_tracing_miss_contract.h"
#include "path_tracing_transport_contract.h"
#include "pbr_surface_light_contract.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    constexpr float Pi = 3.14159265358979323846f;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Path tracing semantic validation failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool Near(float actual, float expected, float tolerance = 1e-5f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    bool Near(
        PathTracingAccumulationFloat3 actual,
        PathTracingAccumulationFloat3 expected,
        float tolerance = 1e-5f)
    {
        return Near(actual.x, expected.x, tolerance) &&
            Near(actual.y, expected.y, tolerance) &&
            Near(actual.z, expected.z, tolerance);
    }

    bool Near(
        PathTracingTransportFloat3 actual,
        PathTracingTransportFloat3 expected,
        float tolerance = 1e-5f)
    {
        return Near(actual.x, expected.x, tolerance) &&
            Near(actual.y, expected.y, tolerance) &&
            Near(actual.z, expected.z, tolerance);
    }

    void RequireDirectLightDraws(
        PathTracingRandomStream& stream,
        std::uint32_t expectedSelectionBits,
        std::uint32_t expectedSampleSeed,
        std::uint32_t expectedDimension,
        const char* message)
    {
        const PathTracingDirectLightRandomDraws draws =
            PathTracingDrawDirectLightRandoms(stream);
        Require(
            Near(
                draws.selection,
                PathTracingUintToUnitFloat(expectedSelectionBits),
                0.0f) &&
                draws.sampleSeed == expectedSampleSeed &&
                stream.dimension == expectedDimension,
            message);
    }

    void RequireBsdfDraws(
        PathTracingRandomStream& stream,
        std::uint32_t expectedBranchBits,
        std::uint32_t expectedSampleXBits,
        std::uint32_t expectedSampleYBits,
        std::uint32_t expectedDimension,
        const char* message)
    {
        const PathTracingBsdfRandomDraws draws =
            PathTracingDrawBsdfRandoms(stream);
        Require(
            Near(
                draws.branch,
                PathTracingUintToUnitFloat(expectedBranchBits),
                0.0f) &&
                Near(
                    draws.sampleX,
                    PathTracingUintToUnitFloat(expectedSampleXBits),
                    0.0f) &&
                Near(
                    draws.sampleY,
                    PathTracingUintToUnitFloat(expectedSampleYBits),
                    0.0f) &&
                stream.dimension == expectedDimension,
            message);
    }
}

int main()
{
    Require(
        uvsr::PathTracingUavSlots ==
            std::array<std::uint32_t, 5>{ 0u, 1u, 2u, 3u, 4u },
        "path-tracing history, output, or retry UAV slots changed");
    PathTracingRandomStream replay = PathTracingCreateRandomStream(
        PathTracingTransportMakeUint2(0x12345678u, 0x9abcdef0u),
        0x50415448u);
    Require(
        replay.seed.x == 760726995u && replay.seed.y == 2038676650u &&
            replay.dimension == 0u &&
            PathTracingRandomUint(replay) == 3770629490u &&
            PathTracingRandomUint(replay) == 587979643u &&
            PathTracingRandomUint(replay) == 3312729204u &&
            PathTracingRandomUint(replay) == 3680601844u &&
            replay.dimension == 4u,
        "counter-based path RNG known answers changed");
    PathTracingRandomStream camera = PathTracingCreateRandomStream(
        PathTracingTransportMakeUint2(0x12345678u, 0x9abcdef0u),
        0x43414d45u);
    const PathTracingCameraRandomDraws cameraDraws =
        PathTracingDrawCameraRandoms(camera);
    Require(
        Near(
            cameraDraws.jitterX,
            PathTracingUintToUnitFloat(2616363384u),
            0.0f) &&
            Near(
                cameraDraws.jitterY,
                PathTracingUintToUnitFloat(897765030u),
                0.0f) &&
            camera.dimension == 2u,
        "camera jitter no longer consumes x then y in its isolated domain");
    PathTracingRandomStream bounceSchedule = PathTracingCreateRandomStream(
        PathTracingTransportMakeUint2(0x12345678u, 0x9abcdef0u),
        0x50415448u);
    RequireDirectLightDraws(
        bounceSchedule,
        3770629490u,
        587979643u,
        2u,
        "bounce zero direct-light draws or dimension changed");
    RequireBsdfDraws(
        bounceSchedule,
        3312729204u,
        3680601844u,
        2178515750u,
        5u,
        "bounce zero BSDF draws or dimension changed");
    RequireDirectLightDraws(
        bounceSchedule,
        3402677836u,
        1313725663u,
        7u,
        "bounce one direct-light draws or dimension changed");
    RequireBsdfDraws(
        bounceSchedule,
        3867406099u,
        2286880489u,
        3164319798u,
        10u,
        "bounce one BSDF draws or dimension changed");
    RequireDirectLightDraws(
        bounceSchedule,
        2188890170u,
        3097686502u,
        12u,
        "bounce two direct-light draws or dimension changed");
    RequireBsdfDraws(
        bounceSchedule,
        575884726u,
        1166118829u,
        1801251354u,
        15u,
        "bounce two BSDF draws or dimension changed");
    const float rouletteDraw =
        PathTracingDrawRouletteRandom(bounceSchedule);
    Require(
        Near(
            rouletteDraw,
            PathTracingUintToUnitFloat(3985603113u),
            0.0f) &&
            bounceSchedule.dimension == 16u,
        "bounce two roulette draw or dimension changed");
    RequireDirectLightDraws(
        bounceSchedule,
        846377759u,
        3848338464u,
        18u,
        "terminal bounce direct-light draws or dimension changed");
    const PathTracingTransportUint2 sampleSeed = PathTracingMakeSampleSeed(
        PathTracingTransportMakeUint2(37u, 91u),
        12u,
        5u,
        0u,
        0.375f);
    Require(
        sampleSeed.x == 3418498407u && sampleSeed.y == 581247694u &&
            PathTracingNoiseToUint(1.0f) == 0xffffffffu &&
            PathTracingNoiseToUint(-1.0f) == 0u,
        "sample seed no longer binds pixel, phase, accepted count, and noise");
    const std::uint32_t retryPhase = PathTracingMakeAttemptPhase(5u, 1u);
    const PathTracingTransportUint2 retrySeed = PathTracingMakeSampleSeed(
        PathTracingTransportMakeUint2(37u, 91u),
        retryPhase,
        5u,
        1u,
        0.375f);
    const PathTracingTransportUint2 retryControlSeed =
        PathTracingMakeSampleSeed(
            PathTracingTransportMakeUint2(37u, 91u),
            retryPhase,
            5u,
            0u,
            0.375f);
    Require(
        PathTracingMakeAttemptPhase(5u, 0u) == 5u &&
            retryPhase == 2654435774u &&
            retryControlSeed.x == 2545574768u &&
            retryControlSeed.y == 3974641560u &&
            retrySeed.x == 3857388923u &&
            retrySeed.y == 1091638391u &&
            (retrySeed.x != retryControlSeed.x ||
                retrySeed.y != retryControlSeed.y),
        "retry generation no longer changes the noise phase and sample seed");

    const PathTracingRetryGenerationTransition firstReject =
        ResolvePathTracingRetryGeneration(0u, 0u);
    const PathTracingRetryGenerationTransition secondReject =
        ResolvePathTracingRetryGeneration(firstReject.generation, 0u);
    const PathTracingRetryGenerationTransition acceptedRetry =
        ResolvePathTracingRetryGeneration(secondReject.generation, 1u);
    const PathTracingRetryGenerationTransition acceptedInitial =
        ResolvePathTracingRetryGeneration(0u, 1u);
    const PathTracingRetryGenerationTransition wrappedReject =
        ResolvePathTracingRetryGeneration(
            std::numeric_limits<std::uint32_t>::max(),
            0u);
    Require(
        UVSR_PATH_TRACING_RETRY_GENERATION_CLEARED == 0u &&
            UVSR_PATH_TRACING_RETRY_GENERATION_FIRST == 1u &&
            firstReject.generation == 1u && firstReject.changed != 0u &&
            secondReject.generation == 2u && secondReject.changed != 0u &&
            acceptedRetry.generation == 0u &&
            acceptedRetry.changed != 0u &&
            acceptedInitial.generation == 0u &&
            acceptedInitial.changed == 0u &&
            wrappedReject.generation == 1u &&
            wrappedReject.changed != 0u,
        "retry generation no longer advances on rejection or clears on acceptance");

    const PathTracingTransportFloat3 cosineSample =
        PathTracingSampleCosineHemisphereLocal(
            PathTracingTransportMakeFloat2(0.25f, 0.5f));
    const PathTracingTransportFloat3 ggxSample =
        PathTracingSampleGgxHalfVectorLocal(
            PathTracingTransportMakeFloat2(0.25f, 0.5f),
            0.4f);
    Require(
        Near(cosineSample, { -0.5f, 0.0f, 0.8660254f }) &&
            Near(PbrContractDot(cosineSample, cosineSample), 1.0f) &&
            Near(ggxSample, { -0.22501758f, 0.0f, 0.9743547f }) &&
            Near(PbrContractDot(ggxSample, ggxSample), 1.0f),
        "cosine or GGX production sampler known answers changed");

    const PathTracingPreparedMaterialContract transportMaterial =
        ResolvePathTracingPreparedMaterial(
            { 0.8f, 0.2f, 0.1f },
            0.25f,
            0.5f,
            0.08f,
            false);
    Require(
        Near(transportMaterial.diffuseColor, { 0.6f, 0.15f, 0.075f }) &&
            Near(transportMaterial.specularF0, { 0.26f, 0.11f, 0.085f }) &&
            Near(transportMaterial.alpha, 0.25f) &&
            Near(
                ResolvePathTracingDiffuseSelectionProbability(
                    transportMaterial),
                0.6316848f),
        "metallic-roughness transport preparation changed");
    const PathTracingBsdfContractEvaluation bsdf =
        ResolvePathTracingBsdfEvaluation(
            transportMaterial,
            0.8f,
            0.6f,
            0.8f,
            0.6f,
            0.9f,
            0.7f);
    Require(
        Near(bsdf.diffuse, { 0.14098616f, 0.04239111f, 0.021790935f }) &&
            Near(bsdf.specular, { 0.04523298f, 0.01937925f, 0.01507030f }) &&
            Near(bsdf.diffusePdf, 0.19098593f) &&
            Near(bsdf.specularPdf, 0.11044171f),
        "Lambert plus exact GGX evaluation/PDF known answers changed");
    const PathTracingBsdfWeightContract weighted =
        ResolvePathTracingBsdfWeight(
            bsdf,
            ResolvePathTracingDiffuseSelectionProbability(transportMaterial),
            0.6f,
            0.6f);
    Require(
        weighted.valid != 0u && Near(weighted.pdf, 0.16132027f) &&
            Near(weighted.weight,
                { 0.69260658f, 0.22974308f, 0.13709833f }),
        "Lambert/GGX balance estimator weight changed");

    constexpr std::uint32_t PdfIntegrationSteps = 65536u;
    double lambertIntegral = 0.0;
    double ggxNormalIntegral = 0.0;
    for (std::uint32_t index = 0u; index < PdfIntegrationSteps; ++index)
    {
        const float cosine =
            (static_cast<float>(index) + 0.5f) /
            static_cast<float>(PdfIntegrationSteps);
        lambertIntegral += 2.0 * Pi * PathTracingPdfLambert(cosine) /
            static_cast<double>(PdfIntegrationSteps);
        ggxNormalIntegral += 2.0 * Pi *
            PathTracingD_GGXExact(cosine, 0.4f) * cosine /
            static_cast<double>(PdfIntegrationSteps);
    }
    Require(
        std::abs(lambertIntegral - 1.0) < 1e-6 &&
            std::abs(ggxNormalIntegral - 1.0) < 1e-5,
        "Lambert or GGX normal-distribution PDF is not normalized");

    Require(
        UVSR_PATH_TRACING_BOUNCE_COUNT == 4u &&
            UVSR_PATH_TRACING_SAMPLES_PER_FRAME == 1u &&
            UVSR_PATH_TRACING_RUSSIAN_ROULETTE_START == 3u &&
            PathTracingBounceSamplesBsdf(1u) &&
            PathTracingBounceSamplesBsdf(3u) &&
            !PathTracingBounceSamplesBsdf(4u) &&
            !PathTracingRouletteRequiresRandom(2u) &&
            PathTracingRouletteRequiresRandom(3u),
        "fixed path length, sample count, or roulette start changed");
    const PathTracingRouletteContract beforeRoulette =
        ResolvePathTracingRoulette(
            2u,
            { 0.25f, 0.5f, 0.1f },
            0.99f);
    const PathTracingRouletteContract survivedRoulette =
        ResolvePathTracingRoulette(
            3u,
            { 0.25f, 0.5f, 0.1f },
            0.49f);
    const PathTracingRouletteContract terminatedRoulette =
        ResolvePathTracingRoulette(
            3u,
            { 0.25f, 0.5f, 0.1f },
            0.5f);
    Require(
        beforeRoulette.transportValid != 0u &&
            beforeRoulette.continuePath != 0u &&
            Near(beforeRoulette.throughput, { 0.25f, 0.5f, 0.1f }) &&
            survivedRoulette.transportValid != 0u &&
            survivedRoulette.continuePath != 0u &&
            Near(survivedRoulette.survival, 0.5f) &&
            Near(survivedRoulette.throughput, { 0.5f, 1.0f, 0.2f }) &&
            terminatedRoulette.transportValid != 0u &&
            terminatedRoulette.continuePath == 0u &&
            Near(terminatedRoulette.survival, 0.5f),
        "roulette timing, threshold, or unbiased survivor scale changed");
    const float transportNan = std::numeric_limits<float>::quiet_NaN();
    const PathTracingRouletteContract invalidThroughput =
        ResolvePathTracingRoulette(
            1u,
            { transportNan, 1.0f, 1.0f },
            0.0f);
    const PathTracingRouletteContract floorSurvivor =
        ResolvePathTracingRoulette(
            3u,
            { 0.0f, 0.0f, 0.0f },
            0.049f);
    const PathTracingRouletteContract capTermination =
        ResolvePathTracingRoulette(
            3u,
            { 2.0f, 1.0f, 0.5f },
            0.95f);
    Require(
        PathTracingThroughputIsValid({ 0.0f, 1.0f, 2.0f }) &&
            !PathTracingThroughputIsValid({ -0.01f, 1.0f, 2.0f }) &&
            !PathTracingThroughputIsValid(
                { transportNan, 1.0f, 1.0f }) &&
            invalidThroughput.transportValid == 0u &&
            invalidThroughput.continuePath == 0u &&
            floorSurvivor.continuePath != 0u &&
            Near(floorSurvivor.survival, 0.05f) &&
            capTermination.continuePath == 0u &&
            Near(capTermination.survival, 0.95f),
        "non-finite throughput or roulette probability clamps changed");

    Require(
        !PathTracingMissUsesEnvironment(0u, false) &&
            PathTracingMissUsesEnvironment(0u, true) &&
            PathTracingMissUsesEnvironment(1u, false) &&
            PathTracingMissUsesEnvironment(7u, false),
        "primary background suppression changed secondary-miss environment transport");

    const PbrFiniteDirectionalEmitterContract directional =
        ResolvePbrFiniteDirectionalEmitter(6.f, 0.2f);
    Require(
        directional.valid != 0 && Near(
            directional.oneMinusCosineMaximum,
            0.004995835f,
            1e-7f) && Near(
            directional.solidAngle,
            0.031389754f,
            1e-7f) && Near(
            directional.directionalPdf,
            31.857529f,
            1e-4f),
        "finite directional cone known answers changed");
    Require(Near(
        directional.directionalPdf * directional.solidAngle,
        1.f,
        1e-6f),
        "finite directional cone PDF does not integrate to one");
    Require(Near(
        directional.radianceScale * Pi *
            std::sin(0.1f) * std::sin(0.1f),
        6.f),
        "finite directional radiance no longer integrates to authored irradiance");

    const PbrFiniteSphereEmitterContract sphere =
        ResolvePbrFiniteSphereEmitter(12.f, 1.f, 5.f);
    Require(
        sphere.valid != 0 && sphere.receiverOutside != 0 && Near(
            sphere.oneMinusCosineMaximum,
            0.020204103f,
            1e-7f) && Near(
            sphere.directionalPdf,
            7.8773575f) && Near(
            sphere.directionalPdf * sphere.solidAngle,
            1.f,
            1e-6f),
        "exterior sphere cone or normalized PDF changed");
    const PbrFiniteSphereEndpointContract nearEndpoint =
        ResolvePbrFiniteSphereEndpoint(5.f, 25.f, 1.f, true);
    Require(
        nearEndpoint.valid != 0 && Near(nearEndpoint.distance, 4.f),
        "exterior sphere ray no longer terminates at the near shell");
    Require(Near(
        sphere.radianceScale * Pi * (1.f / 25.f),
        12.f / 25.f),
        "finite sphere radiance no longer integrates to inverse-square irradiance");

    const PbrFiniteSphereEmitterContract enclosingSphere =
        ResolvePbrFiniteSphereEmitter(12.f, 1.f, 0.f);
    const PbrFiniteSphereEndpointContract exitEndpoint =
        ResolvePbrFiniteSphereEndpoint(0.f, 0.f, 1.f, false);
    Require(
        enclosingSphere.valid != 0 &&
            enclosingSphere.receiverOutside == 0 && Near(
                enclosingSphere.solidAngle,
                4.f * Pi) && Near(
                enclosingSphere.directionalPdf *
                    enclosingSphere.solidAngle,
                1.f,
                1e-6f) &&
            exitEndpoint.valid != 0 && Near(exitEndpoint.distance, 1.f),
        "enclosing sphere must sample 4pi and terminate at its exit shell");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const PathTracingAccumulationState repaired =
        RepairPathTracingAccumulation(
            { nan, 2.f, 3.f },
            17u);
    Require(
        repaired.count == 0u && repaired.accepted == 0u &&
            repaired.publish == 1u && Near(
                repaired.mean,
                PathTracingAccumulationFloat3{}),
        "non-finite prior mean is not repaired to empty history");

    const PathTracingAccumulationState prior =
        RepairPathTracingAccumulation({ 2.f, 4.f, 6.f }, 3u);
    const PathTracingAccumulationState invalidAttempt =
        ResolvePathTracingAccumulation(
            prior,
            { 9.f, 9.f, 9.f },
            false);
    const PathTracingAccumulationState nonfiniteAttempt =
        ResolvePathTracingAccumulation(
            prior,
            { nan, 1.f, 1.f },
            true);
    Require(
        invalidAttempt.count == 3u && invalidAttempt.accepted == 0u &&
            invalidAttempt.publish == 0u && Near(
                invalidAttempt.mean,
                prior.mean) &&
            nonfiniteAttempt.count == 3u &&
            nonfiniteAttempt.accepted == 0u && Near(
                nonfiniteAttempt.mean,
                prior.mean),
        "invalid attempt changed a valid mean or accepted-sample count");

    PathTracingAccumulationState history =
        RepairPathTracingAccumulation({}, 0u);
    history = ResolvePathTracingAccumulation(
        history,
        { 0.f, 0.f, 0.f },
        true);
    Require(
        history.accepted == 1u && history.count == 1u && Near(
            history.mean,
            PathTracingAccumulationFloat3{}),
        "finite black miss was not accepted exactly once");
    history = ResolvePathTracingAccumulation(
        history,
        { 3.f, 6.f, 9.f },
        true);
    Require(
        history.accepted == 1u && history.count == 2u && Near(
            history.mean,
            PathTracingAccumulationFloat3{ 1.5f, 3.f, 4.5f }),
        "finite hit was not accumulated exactly once");
    history = ResolvePathTracingAccumulation(
        history,
        { 0.f, 3.f, 0.f },
        true);
    Require(
        history.accepted == 1u && history.count == 3u && Near(
            history.mean,
            PathTracingAccumulationFloat3{ 1.f, 3.f, 3.f }),
        "cumulative lerp diverged from one accepted history");

    const PathTracingAccumulationState saturated =
        ResolvePathTracingAccumulation(
            RepairPathTracingAccumulation(
                { 1.f, 2.f, 3.f },
                UVSR_PATH_TRACING_SATURATED_SAMPLE_COUNT),
            { 9.f, 9.f, 9.f },
            true);
    Require(
        saturated.count == UVSR_PATH_TRACING_SATURATED_SAMPLE_COUNT &&
            saturated.accepted == 0u && saturated.publish == 0u && Near(
                saturated.mean,
                PathTracingAccumulationFloat3{ 1.f, 2.f, 3.f }),
        "saturated accumulation did not stop without mutation");

    return EXIT_SUCCESS;
}
