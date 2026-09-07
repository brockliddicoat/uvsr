#include "path_tracing_accumulation_contract.h"
#include "path_tracing_miss_contract.h"
#include "path_tracing_settings.h"
#include "path_tracing_firefly_contract.h"
#include "lighting_accumulation_contract.h"

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
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
    bool Near(float a, float b, float tolerance = 1e-5f) { return std::abs(a - b) <= tolerance; }
    template<typename Vector>
    bool Near3(Vector a, Vector b, float tolerance = 1e-5f)
    {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
    }
    constexpr float Pi = 3.14159265358979323846f;
    const float Nan = std::numeric_limits<float>::quiet_NaN();

    void CheckRandomSchedule()
    {
        const auto seed = ShaderMakeUint2(0x12345678u, 0x9abcdef0u);
        auto replay = PathTracingCreateRandomStream(seed, 0x50415448u);
        Require(replay.seed.x == 760726995u && replay.seed.y == 2038676650u && replay.dimension == 0,
            "path RNG seed or domain changed");
        for (const auto expected : { 3770629490u, 587979643u, 3312729204u, 3680601844u })
            Require(PathTracingRandomUint(replay) == expected, "path RNG known answer changed");
        Require(replay.dimension == 4, "path RNG did not consume one dimension per draw");
        auto camera = PathTracingCreateRandomStream(seed, 0x43414d45u);
        const auto jitter = PathTracingDrawCameraRandoms(camera);
        Require(jitter.jitterX == 0.6091696024f && jitter.jitterY == 0.2090272009f && camera.dimension == 2,
            "camera x/y draws lost their isolated domain");

        struct Bounce { float selection; uint32_t sampleSeed; float branch, x, y; };
        const Bounce expected[] = {
            { 0.8779181242f, 587979643u, 0.7713048458f, 0.8569568396f, 0.5072252750f },
            { 0.7922476530f, 1313725663u, 0.9004507065f, 0.5324558020f, 0.7367506027f },
            { 0.5096406937f, 3097686502u, 0.1340835989f, 0.2715081871f, 0.4193865359f }
        };
        auto stream = PathTracingCreateRandomStream(seed, 0x50415448u);
        for (unsigned bounce = 0; bounce < 3; ++bounce)
        {
            const auto direct = PathTracingDrawDirectLightRandoms(stream);
            Require(direct.selection == expected[bounce].selection && direct.sampleSeed == expected[bounce].sampleSeed &&
                stream.dimension == bounce * 5 + 2, "direct light draw order or seed changed");
            const auto bsdf = PathTracingDrawBsdfRandoms(stream);
            Require(bsdf.branch == expected[bounce].branch && bsdf.sampleX == expected[bounce].x &&
                bsdf.sampleY == expected[bounce].y && stream.dimension == bounce * 5 + 5,
                "BSDF branch/x/y draw order changed");
        }
        Require(PathTracingDrawRouletteRandom(stream) == 0.9279705286f && stream.dimension == 16,
            "roulette draw shifted its dimension");
        const auto terminal = PathTracingDrawDirectLightRandoms(stream);
        Require(terminal.selection == 0.1970627010f && terminal.sampleSeed == 3848338464u && stream.dimension == 18,
            "terminal direct-light draw schedule changed");

        const auto pixel = ShaderMakeUint2(37, 91);
        const auto first = PathTracingMakeSampleSeed(pixel, 12, 5, 0, .375f);
        const auto phase = PathTracingMakeAttemptPhase(5, 1);
        const auto retry = PathTracingMakeSampleSeed(pixel, phase, 5, 1, .375f);
        const auto control = PathTracingMakeSampleSeed(pixel, phase, 5, 0, .375f);
        Require(first.x == 3418498407u && first.y == 581247694u &&
            PathTracingNoiseToUint(1) == 0xffffffffu && PathTracingNoiseToUint(-1) == 0 &&
            PathTracingMakeAttemptPhase(5, 0) == 5 && phase == 2654435774u &&
            retry.x == 3857388923u && retry.y == 1091638391u &&
            control.x == 2545574768u && control.y == 3974641560u,
            "pixel, accepted count, retry generation or noise lost its sample identity");
        struct Retry { uint32_t before, accepted, after; bool changed; };
        for (const Retry test : { Retry{ 0, 0, 1, true }, { 1, 0, 2, true }, { 2, 1, 0, true },
                 { 0, 1, 0, false }, { 0xffffffffu, 0, 1, true } })
        {
            const auto result = ResolvePathTracingRetryGeneration(test.before, test.accepted);
            Require(result.generation == test.after && bool(result.changed) == test.changed,
                "rejected path retry did not advance, wrap or clear on acceptance");
        }
    }

    void CheckTransport()
    {
        const auto cosine = PathTracingSampleCosineHemisphereLocal(ShaderMakeFloat2(.25f, .5f));
        const auto ggx = PathTracingSampleGgxHalfVectorLocal(ShaderMakeFloat2(.25f, .5f), .4f);
        Require(Near3(cosine, { -.5f, 0, .8660254f }) && Near(ShaderDot(cosine, cosine), 1) &&
            Near3(ggx, { -.22501758f, 0, .9743547f }) && Near(ShaderDot(ggx, ggx), 1),
            "production cosine/GGX samplers changed direction or normalization");
        const auto material = ResolvePathTracingPreparedMaterial({ .8f, .2f, .1f }, .25f, .5f, .08f, false);
        Require(Near3(material.diffuseColor, { .6f, .15f, .075f }) &&
            Near3(material.specularF0, { .26f, .11f, .085f }) && Near(material.alpha, .25f) &&
            Near(ResolvePathTracingDiffuseSelectionProbability(material), .6316848f),
            "metallic/roughness transport preparation changed");
        const auto bsdf = ResolvePathTracingBsdfEvaluation(material, .8f, .6f, .8f, .6f, .9f, .7f);
        Require(Near3(bsdf.diffuse, { .14098616f, .04239111f, .021790935f }) &&
            Near3(bsdf.specular, { .04523298f, .01937925f, .01507030f }) &&
            Near(bsdf.diffusePdf, .19098593f) && Near(bsdf.specularPdf, .11044171f),
            "Lambert/GGX evaluation or PDF known answer changed");
        const auto weighted = ResolvePathTracingBsdfWeight(bsdf, ResolvePathTracingDiffuseSelectionProbability(material), .6f, .6f);
        Require(weighted.valid && Near(weighted.pdf, .16132027f) &&
            Near3(weighted.weight, { .69260658f, .22974308f, .13709833f }), "BSDF balance weight changed");
        double lambertIntegral = 0, ggxIntegral = 0;
        constexpr unsigned steps = 65536;
        for (unsigned index = 0; index < steps; ++index)
        {
            const float angleCosine = (float(index) + .5f) / steps;
            lambertIntegral += 2.0 * Pi * PathTracingPdfLambert(angleCosine) / steps;
            ggxIntegral += 2.0 * Pi * PathTracingD_GGXExact(angleCosine, .4f) * angleCosine / steps;
        }
        Require(std::abs(lambertIntegral - 1) < 1e-6 && std::abs(ggxIntegral - 1) < 1e-5,
            "Lambert/GGX PDF lost unit hemisphere mass");

        Require(uvsr::IsValidPathTracingSettings(uvsr::DefaultPathTracingSettings), "default path settings must dispatch");
        for (const int invalidMaximum : { -1, 0, 31, std::numeric_limits<int>::max() })
        {
            auto settings = uvsr::DefaultPathTracingSettings;
            settings.maximumBounces = invalidMaximum;
            Require(!uvsr::IsValidPathTracingSettings(settings), "invalid bounce limit must reject before unsigned conversion");
        }
        for (const int invalidMinimum : { -1, 0, 31 })
        {
            auto settings = uvsr::DefaultPathTracingSettings;
            settings.minimumBounces = invalidMinimum;
            Require(!uvsr::IsValidPathTracingSettings(settings), "invalid roulette floor must reject");
        }
        for (const float invalidThreshold : { Nan, std::numeric_limits<float>::infinity(), -1.f, 0.f, 9.f, 1000001.f })
        {
            auto settings = uvsr::DefaultPathTracingSettings;
            settings.fireflyFilter = false;
            settings.fireflyThreshold = invalidThreshold;
            Require(!uvsr::IsValidPathTracingSettings(settings), "invalid latent threshold must reject even when disabled");
        }
        Require(uvsr::IsValidPathTracingSettings({ 1, 1, false, 10.f }) &&
            uvsr::IsValidPathTracingSettings({ 30, 30, true, 1000000.f }) &&
            !uvsr::IsValidPathTracingSettings({ 1, 2, true, 5000.f }),
            "path settings must retain inclusive bounds and reject inverted intervals");
        for (unsigned maximum = 1; maximum <= 30; ++maximum)
        {
            Require(PathTracingBounceSamplesBsdf(maximum, maximum) &&
                !PathTracingBounceSamplesBsdf(maximum + 1, maximum),
                "the camera hit must not consume a scattering bounce");
            for (unsigned minimum = 1; minimum <= maximum; ++minimum)
                Require(!PathTracingRouletteRequiresRandom(minimum + 1, minimum) &&
                    PathTracingRouletteRequiresRandom(minimum + 2, minimum),
                    "roulette must preserve Capsaicin's strict minimum-depth boundary");
        }
        const auto before = ResolvePathTracingRoulette(3, 2, { .25f, .5f, .1f }, .99f);
        const auto survived = ResolvePathTracingRoulette(4, 2, { .25f, .5f, .1f }, .49f);
        const auto stopped = ResolvePathTracingRoulette(4, 2, { .25f, .5f, .1f }, .5f);
        const auto invalid = ResolvePathTracingRoulette(1, 2, { Nan, 1, 1 }, 0);
        const auto floorSurvivor = ResolvePathTracingRoulette(4, 2, { 0, 0, 0 }, .049f);
        const auto cap = ResolvePathTracingRoulette(4, 2, { 2, 1, .5f }, .95f);
        Require(before.transportValid && before.continuePath && Near3(before.throughput, { .25f, .5f, .1f }) &&
            survived.transportValid && survived.continuePath && Near(survived.survival, .5f) &&
            Near3(survived.throughput, { .5f, 1, .2f }) && stopped.transportValid && !stopped.continuePath &&
            Near(stopped.survival, .5f), "roulette threshold or unbiased survivor energy changed");
        Require(PathTracingThroughputIsValid({ 0, 1, 2 }) && !PathTracingThroughputIsValid({ -.01f, 1, 2 }) &&
            !PathTracingThroughputIsValid({ Nan, 1, 2 }) && !invalid.transportValid && !invalid.continuePath &&
            floorSurvivor.continuePath && Near(floorSurvivor.survival, .05f) && !cap.continuePath && Near(cap.survival, .95f),
            "roulette lost finite throughput or probability bounds");
        Require(Near(PathTracingAdvanceFireflyFilter(1.f, 0.f, 1.f), 1.f) &&
            Near(PathTracingAdvanceFireflyFilter(1.f, 1.f / float(2.0 * Pi), 1.f),
                float(32.0 / (32.0 + Pi * Pi)), 1.e-6f) &&
            Near(PathTracingAdvanceFireflyFilter(1.f, 0.f, .25f), .5f) &&
            Near(PathTracingAdvanceFireflyFilter(1.e-6f, 1.f, 1.f), 1.e-5f),
            "firefly scatter factor lost probability, lobe weight or floor");
        Require(Near3(PathTracingFilterFirefly({ 300, 0, 0 }, 10, 1), { 30, 0, 0 }) &&
            Near3(PathTracingFilterFirefly({ 60, 30, 0 }, 10, .5f), { 10, 5, 0 }) &&
            Near3(PathTracingFilterFirefly({ 3, 2, 1 }, 10, 1), { 3, 2, 1 }) &&
            Near3(PathTracingFilterFirefly({ 300, 20, 1 }, 0, .01f), { 300, 20, 1 }),
            "firefly cap changed hue, uncapped radiance or the exact off path");
        float factor = 1.f;
        for (unsigned bounce = 0; bounce != 30; ++bounce)
        {
            const float next = PathTracingAdvanceFireflyFilter(factor, .2f, .5f);
            Require(next <= factor && next >= 1.e-5f, "scatter history must decay monotonically to its floor");
            factor = next;
        }
        Require(!PathTracingMissUsesEnvironment(0, false) && PathTracingMissUsesEnvironment(0, true) &&
            PathTracingMissUsesEnvironment(1, false) && PathTracingMissUsesEnvironment(7, false),
            "background control disabled secondary environment transport");

        const auto directional = ResolvePbrFiniteDirectionalEmitter(6, .2f);
        Require(directional.valid && Near(directional.oneMinusCosineMaximum, .004995835f, 1e-7f) &&
            Near(directional.solidAngle, .031389754f, 1e-7f) && Near(directional.directionalPdf, 31.857529f, 1e-4f) &&
            Near(directional.directionalPdf * directional.solidAngle, 1) &&
            Near(directional.radianceScale * Pi * std::sin(.1f) * std::sin(.1f), 6),
            "finite directional emitter lost its cone, PDF or irradiance");
        const auto sphere = ResolvePbrFiniteSphereEmitter(12, 1, 5);
        const auto nearEndpoint = ResolvePbrFiniteSphereEndpoint(5, 25, 1, true);
        const auto enclosing = ResolvePbrFiniteSphereEmitter(12, 1, 0);
        const auto exitEndpoint = ResolvePbrFiniteSphereEndpoint(0, 0, 1, false);
        Require(sphere.valid && sphere.receiverOutside && Near(sphere.oneMinusCosineMaximum, .020204103f, 1e-7f) &&
            Near(sphere.directionalPdf, 7.8773575f) && Near(sphere.directionalPdf * sphere.solidAngle, 1) &&
            Near(sphere.radianceScale * Pi / 25, 12.f / 25) && nearEndpoint.valid && Near(nearEndpoint.distance, 4),
            "exterior sphere lost inverse-square irradiance or its near-shell endpoint");
        Require(enclosing.valid && !enclosing.receiverOutside && Near(enclosing.solidAngle, 4 * Pi) &&
            Near(enclosing.directionalPdf * enclosing.solidAngle, 1) && exitEndpoint.valid && Near(exitEndpoint.distance, 1),
            "enclosing sphere lost full-sphere sampling or its exit shell");
    }

    void CheckAccumulation()
    {
        const auto repaired = RepairPathTracingAccumulation({ Nan, 2, 3 }, 17);
        Require(!repaired.count && !repaired.accepted && repaired.publish &&
            Near3(repaired.mean, {}), "invalid path history was not repaired");
        const auto prior = RepairPathTracingAccumulation({ 2, 4, 6 }, 3);
        for (const bool valid : { false, true })
        {
            const auto rejected = ResolvePathTracingAccumulation(prior, { valid ? Nan : 9.f, 1, 1 }, valid);
            Require(rejected.count == 3 && !rejected.accepted && !rejected.publish && Near3(rejected.mean, prior.mean),
                "invalid path attempt changed published history");
        }
        auto path = RepairPathTracingAccumulation({}, 0);
        const ShaderFloat3 samples[] = { { 0, 0, 0 }, { 3, 6, 9 }, { 0, 3, 0 } };
        const ShaderFloat3 means[] = { { 0, 0, 0 }, { 1.5f, 3, 4.5f }, { 1, 3, 3 } };
        for (unsigned index = 0; index < 3; ++index)
        {
            path = ResolvePathTracingAccumulation(path, samples[index], true);
            Require(path.accepted && path.count == index + 1 && Near3(path.mean, means[index]),
                "finite black miss or hit did not contribute exactly one path sample");
        }
        const auto terminalPath = ResolvePathTracingAccumulation(
            RepairPathTracingAccumulation({ 1, 2, 3 }, UVSR_PATH_TRACING_SATURATED_SAMPLE_COUNT), { 9, 9, 9 }, true);
        Require(terminalPath.count == UVSR_PATH_TRACING_SATURATED_SAMPLE_COUNT &&
            !terminalPath.accepted && !terminalPath.publish && Near3(terminalPath.mean, { 1, 2, 3 }),
            "saturated path history changed");

        Require(ResolveLightingAccumulationAttemptToken(0, false) == 1 &&
            ResolveLightingAccumulationAttemptToken(17, false) == 18 &&
            ResolveLightingAccumulationAttemptToken(0xffffffffu, false) == 0xffffffffu &&
            ResolveLightingAccumulationAttemptToken(0xffffffffu, true) == 1, "lighting attempt token overflowed");
        for (unsigned reason = 0; reason < 3; ++reason)
        {
            const auto empty = RepairLightingAccumulation({ reason == 2 ? Nan : 1.f, 2, 3, 1 },
                reason == 1 ? 0 : 9, reason == 0);
            Require(empty.count == 0 && empty.publish && Near3(empty.mean, {}) && empty.mean.w == 0,
                "reset, empty or invalid lighting history was not repaired");
        }
        const auto history = RepairLightingAccumulation({ 2, 4, 6, 1 }, 3, false);
        for (const bool attempted : { false, true })
        {
            const auto rejected = ResolveLightingAccumulationCandidate(history, attempted ? 4 : 0,
                { 9, attempted ? Nan : 9.f, 9, 1 });
            Require(bool(rejected.attempted) == attempted && !rejected.accepted && rejected.publish &&
                rejected.count == 3 && Near3(rejected.mean, history.mean) && rejected.mean.w == 1,
                "skipped/invalid lighting attempt stopped publishing preserved history");
        }
        auto lighting = RepairLightingAccumulation({}, 0, false);
        const ShaderFloat4 candidates[] = { { -2, 4, 8, -7 }, { 2, 0, 4, 0 }, { 4, 2, 0, 0 } };
        const ShaderFloat4 expected[] = { { 0, 4, 8, 1 }, { 1, 2, 6, 1 }, { 2, 2, 4, 1 } };
        for (unsigned index = 0; index < 3; ++index)
        {
            lighting = ResolveLightingAccumulationCandidate(lighting, index + 1, candidates[index]);
            Require(lighting.accepted && lighting.count == index + 1 &&
                Near3(lighting.mean, expected[index]) && lighting.mean.w == 1, "lighting mean or alpha clamping changed");
        }
        const auto terminalLighting = ResolveLightingAccumulationCandidate(
            RepairLightingAccumulation({ 1, 2, 3, 1 }, 0xffffffffu, false), 0xffffffffu, { 9, 9, 9, 1 });
        Require(terminalLighting.count == 0xffffffffu && terminalLighting.attempted && !terminalLighting.accepted &&
            terminalLighting.publish && Near3(terminalLighting.mean, { 1, 2, 3, 1 }) && terminalLighting.mean.w == 1,
            "terminal lighting history mutated or stopped publishing");
    }

    void CheckSelection()
    {
        using namespace uvsr;
        using Solution = LightingSolution;
        using State = SelectedLightingTransportState;
        const auto enter = ResolveLightingSolutionTransition(Solution::RayMarching, Solution::PathTracing);
        const auto reapply = ResolveLightingSolutionTransition(Solution::PathTracing, Solution::PathTracing);
        const auto reject = ResolveLightingSolutionTransition(Solution::PathTracing, static_cast<Solution>(255));
        Require(IsValidLightingSolution(Solution::RayMarching) && IsValidLightingSolution(Solution::PathTracing) &&
            !IsValidLightingSolution(static_cast<Solution>(255)) && enter.accepted && enter.changed &&
            enter.openPathTracingDrawer && enter.resetHistory && enter.selection == Solution::PathTracing &&
            reapply.accepted && !reapply.changed && !reapply.openPathTracingDrawer && reapply.resetHistory &&
            !reject.accepted && !reject.changed && !reject.resetHistory && reject.selection == Solution::PathTracing,
            "lighting selection lost its drawer/reset behavior or retained invalid input");
        const auto raster = ResolveSelectedLightingTransport(Solution::RayMarching, false, false);
        Require(raster.renderRayMarching && !raster.retainPathTracingSelection && raster.state == State::RayMarching,
            "Ray Marching did not select raster transport");
        struct Phase { bool active, unavailable; State expected; };
        for (const auto phase : { Phase{ false, false, State::PathTracingPreparing },
                 { true, false, State::PathTracingActive }, { false, true, State::PathTracingUnavailable } })
        {
            const auto selection = ResolveSelectedLightingTransport(Solution::PathTracing, phase.active, phase.unavailable);
            Require(!selection.renderRayMarching && selection.retainPathTracingSelection && selection.state == phase.expected,
                "path transport availability erased selection or silently selected raster");
        }
        const PathTracingPipelineResources resources{ true, true, true, true, true, true };
        const auto complete = ResolvePathTracingAvailability(true, resources);
        const auto failed = ResolvePathTracingAvailability(true, {});
        const auto unsupported = ResolvePathTracingAvailability(false, resources);
        Require(complete.rayQuerySupported && complete.executablePipelineAvailable &&
            failed.rayQuerySupported && !failed.executablePipelineAvailable &&
            !unsupported.rayQuerySupported && !unsupported.executablePipelineAvailable,
            "pipeline failure was confused with unsupported DXR hardware");
    }
}

int main()
{
    CheckRandomSchedule();
    CheckTransport();
    CheckAccumulation();
    CheckSelection();
    return EXIT_SUCCESS;
}
