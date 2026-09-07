#include "flashlight.h"
#include "ray_traced_flashlight_shadows_shared.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    using namespace uvsr;
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
    bool Near(float a, float b, float tolerance = 1e-5f) { return std::abs(a - b) <= tolerance; }
    const float Nan = std::numeric_limits<float>::quiet_NaN();
    const float Infinity = std::numeric_limits<float>::infinity();

    float AdvanceForDuration(float initial, bool enabled, float duration, int steps)
    {
        for (int step = 0; step < steps; ++step)
            initial = AdvanceFlashlightTransition(initial, enabled, duration / float(steps));
        return initial;
    }

    void CheckFadeAndMotion()
    {
        Require(AdvanceFlashlightTransition(0, true, 0) == 0 && AdvanceFlashlightTransition(1, false, 0) == 1 &&
            AdvanceFlashlightTransition(.5f, true, -1) == .5f && AdvanceFlashlightTransition(.5f, true, Nan) == .5f &&
            AdvanceFlashlightTransition(Nan, false, 0) == 0, "flashlight invalid timing changed its current fade");
        Require(AdvanceFlashlightTransition(0, true, .18f) == 1 && AdvanceFlashlightTransition(1, false, .24f) == 0 &&
            AdvanceFlashlightTransition(0, true, 10) == 1 && AdvanceFlashlightTransition(1, false, 10) == 0,
            "flashlight fade duration or endpoint changed");
        for (const int steps : { 3, 6, 14 })
            Require(Near(AdvanceForDuration(0, true, .1f, steps), AdvanceForDuration(0, true, .1f, 1)),
                "flashlight fade depends on frame partition");
        float previous = 0;
        for (unsigned step = 1; step <= 100; ++step)
        {
            const float emission = GetFlashlightEmissionScale(float(step) / 100);
            Require(std::isfinite(emission) && emission >= previous && emission <= 1, "flashlight fade is not bounded and monotonic");
            previous = emission;
        }
        const float before = AdvanceFlashlightTransition(0, true, .135f);
        const float after = AdvanceFlashlightTransition(before, false, 1.f / 60);
        Require(Near(before, .75f) && AdvanceFlashlightTransition(before, false, 0) == before && after > 0 && after < before &&
            GetFlashlightEmissionScale(0) == 0 && GetFlashlightEmissionScale(1) == 1 &&
            GetFlashlightEmissionScale(Infinity) == 0 && !ShouldSubmitFlashlight(0) && ShouldSubmitFlashlight(1),
            "flashlight reversal or emission eligibility changed");

        FlashlightSettings stationary;
        stationary.stationaryWhenIdle = true;
        auto moving = stationary;
        moving.stationaryWhenIdle = false;
        Require(!ShouldAdvanceFlashlightMotion(stationary, true, false, false) &&
            ShouldAdvanceFlashlightMotion(stationary, false, false, false) &&
            ShouldAdvanceFlashlightMotion(stationary, true, true, false) &&
            ShouldAdvanceFlashlightMotion(stationary, true, false, true) &&
            ShouldAdvanceFlashlightMotion(moving, true, false, false), "stationary control lost motion/pose eligibility");
        for (float FlashlightSettings::* field : { &FlashlightSettings::cameraHorizontalOffsetMeters,
                 &FlashlightSettings::cameraVerticalOffsetMeters, &FlashlightSettings::angularSizeDegrees,
                 &FlashlightSettings::swayDegrees, &FlashlightSettings::aimCorrectionSeconds })
        {
            auto changed = stationary;
            changed.*field = std::nextafter(changed.*field, Infinity);
            Require(!SameFlashlightMotionSettings(stationary, changed), "live motion control failed to invalidate its pose");
        }
        auto lens = stationary;
        lens.realisticLens = !lens.realisticLens;
        auto presentation = stationary;
        presentation.peakIntensityCandela += 1;
        presentation.colorLinearRed *= .5f;
        Require(!SameFlashlightMotionSettings(stationary, moving) && !SameFlashlightMotionSettings(stationary, lens) &&
            SameFlashlightMotionSettings(stationary, presentation), "lens or presentation changed the wrong pose identity");

        const float whole = GetFlashlightAimCorrectionBlend(.1f, .05f);
        const float half = GetFlashlightAimCorrectionBlend(.05f, .05f);
        Require(Near(whole, 1 - (1 - half) * (1 - half)) && GetFlashlightAimCorrectionBlend(0, .1f) == 0 &&
            GetFlashlightAimCorrectionBlend(Nan, .1f) == 0, "aim lag lost equal-time composition or invalid-time rejection");
        float partitioned = 0;
        for (unsigned step = 0; step < 60; ++step)
            partitioned = AdvanceFlashlightSwayTime(partitioned, 1.f / 60);
        Require(Near(AdvanceFlashlightSwayTime(0, 1), partitioned, 1e-4f), "sway depends on frame partition");
        const float wrapped = AdvanceFlashlightSwayTime(FlashlightSwayPeriodSeconds - .001f, .002f);
        const auto beforeWrap = ResolveFlashlightSwayOffset(FlashlightSwayPeriodSeconds - .001f, 2);
        const auto afterWrap = ResolveFlashlightSwayOffset(wrapped, 2);
        const auto origin = ResolveFlashlightSwayOffset(0, 2);
        Require(Near(wrapped, .001f, 1e-4f) && std::abs(beforeWrap.yawDegrees - afterWrap.yawDegrees) < .02f &&
            std::abs(beforeWrap.pitchDegrees - afterWrap.pitchDegrees) < .02f && origin.yawDegrees == 0 && origin.pitchDegrees == 0,
            "sway lost phase continuity or its neutral origin");
        for (unsigned sample = 0; sample < 1000; ++sample)
        {
            const auto offset = ResolveFlashlightSwayOffset(float(sample) * .01f, 2);
            Require(std::isfinite(offset.yawDegrees) && std::isfinite(offset.pitchDegrees) &&
                std::abs(offset.yawDegrees) <= 2 && std::abs(offset.pitchDegrees) <= 2, "sway escaped its angular amplitude");
        }
    }

    void CheckMountAndSettings()
    {
        Require(std::string_view(FlashlightPublicName) == "flashlight_1", "flashlight lost its scene identity");
        struct Offset { float x, y; };
        for (const Offset offset : { Offset{ 0, 0 }, { .4f, 0 }, { 0, .4f }, { .4f, .4f }, { -.4f, -.4f }, { .2f, -.1f } })
        {
            const auto mount = ResolveFlashlightMountPose(offset.x, offset.y);
            const float distance = (6 - mount.positionForwardMeters) / mount.directionForward;
            Require(mount.positionRightMeters == offset.x && mount.positionUpMeters == offset.y &&
                mount.positionForwardMeters == .04f && mount.directionForward > 0 &&
                Near(mount.directionRight * mount.directionRight + mount.directionUp * mount.directionUp +
                    mount.directionForward * mount.directionForward, 1) &&
                Near(mount.positionRightMeters + mount.directionRight * distance, 0) &&
                Near(mount.positionUpMeters + mount.directionUp * distance, 0), "offset mount lost unit direction or camera-axis convergence");
        }
        for (float value : { -1.f, 1.f })
        {
            const auto mount = ResolveFlashlightMountPose(value, value);
            Require(mount.positionRightMeters == value * .4f && mount.positionUpMeters == value * .4f,
                "flashlight mount escaped its offset limits");
        }
        const auto invalidX = ResolveFlashlightMountPose(Nan, 0), invalidY = ResolveFlashlightMountPose(0, Infinity);
        Require(invalidX.positionRightMeters == DefaultFlashlightSettings.cameraHorizontalOffsetMeters &&
            invalidX.positionUpMeters == 0 && invalidY.positionRightMeters == 0 &&
            invalidY.positionUpMeters == DefaultFlashlightSettings.cameraVerticalOffsetMeters,
            "invalid mount input replaced its independent axis");
        Require(Near(ResolveFlashlightEmitterRadiusMeters(2), .017455066f, 1e-6f) &&
            ResolveFlashlightEmitterRadiusMeters(0) == 0, "angular emitter control lost its meter conversion");
        for (const float angle : { 0.f, 2.f, 20.f })
        {
            const float radius = ResolveFlashlightCollisionRadiusMeters(angle, .1f);
            Require(radius >= .1f && radius >= ResolveFlashlightEmitterRadiusMeters(angle),
                "collision sphere stopped enclosing camera clearance and emitter");
        }
        Require(ResolveFlashlightCollisionRadiusMeters(0, Nan) == .1f, "invalid collision clearance lost its minimum");

        struct Bound { float FlashlightSettings::* field; float minimum, maximum; };
        const Bound bounds[] = {
            { &FlashlightSettings::peakIntensityCandela, 25, 4000 }, { &FlashlightSettings::rangeMeters, 2, 100 },
            { &FlashlightSettings::cameraHorizontalOffsetMeters, -.4f, .4f },
            { &FlashlightSettings::cameraVerticalOffsetMeters, -.4f, .4f },
            { &FlashlightSettings::beamSizeDegrees, 8, 100 }, { &FlashlightSettings::angularSizeDegrees, 0, 20 },
            { &FlashlightSettings::beamRoundness, 0, 1 }, { &FlashlightSettings::edgeSoftness, 0, 1 },
            { &FlashlightSettings::colorLinearRed, 0, 1 }, { &FlashlightSettings::colorLinearGreen, 0, 1 },
            { &FlashlightSettings::colorLinearBlue, 0, 1 }, { &FlashlightSettings::hotspotSize, .2f, .75f },
            { &FlashlightSettings::hotspotStrength, 0, .9f }, { &FlashlightSettings::swayDegrees, 0, 2 },
            { &FlashlightSettings::aimCorrectionSeconds, .01f, .5f }
        };
        for (const auto bound : bounds)
        for (const float input : { Nan, Infinity, -Infinity, -1000.f, 10000.f })
        {
            FlashlightSettings settings;
            settings.*bound.field = input;
            const float actual = SanitizeFlashlightSettings(settings).*bound.field;
            Require(actual >= bound.minimum && actual <= bound.maximum, "runtime flashlight setting remained invalid or unbounded");
        }
    }

    void CheckBeamProfile()
    {
        FlashlightSettings settings;
        settings.peakIntensityCandela = 600;
        settings.realisticLens = true;
        const auto profile = ResolveFlashlightBeamProfile(settings, 1, 0, 0);
        Require(FlashlightBeamProfileIsValid(profile) && profile.spillWeight > 0 && profile.hotspotWeight > 0 &&
            Near(profile.spillWeight + profile.hotspotWeight, 1) &&
            Near(settings.peakIntensityCandela * EvaluateFlashlightBeamProfile(profile, { 0, 0, 1 }, { 0, 0, 1 }), 600),
            "realistic lens lost its authored on-axis intensity");
        Require(profile.spillInnerCosine > profile.spillOuterCosine &&
            profile.hotspotInnerCosine > profile.hotspotOuterCosine && profile.hotspotOuterCosine > profile.spillOuterCosine,
            "lens hotspot/spill geometry lost its ordering");
        const float spillAngle = .5f * (std::acos(profile.hotspotOuterCosine) + std::acos(profile.spillInnerCosine));
        const float spill = EvaluateFlashlightBeamProfile(profile, { 0, 0, 1 }, { std::sin(spillAngle), 0, std::cos(spillAngle) });
        Require(Near(spill, 1 - settings.hotspotStrength), "hotspot control lost the visible spill intensity");
        auto widerHotspot = settings;
        widerHotspot.hotspotSize = .75f;
        const auto widerProfile = ResolveFlashlightBeamProfile(widerHotspot, 1, 0, 0);
        const ShaderFloat3 shoulder{ .069756474f, 0, .99756405f };
        Require(EvaluateFlashlightBeamProfile(widerProfile, { 0, 0, 1 }, shoulder) >
            EvaluateFlashlightBeamProfile(profile, { 0, 0, 1 }, shoulder), "hotspot size did not broaden visible illumination");
        widerHotspot = settings;
        widerHotspot.hotspotStrength = .9f;
        Require(EvaluateFlashlightBeamProfile(ResolveFlashlightBeamProfile(widerHotspot, 1, 0, 0),
            { 0, 0, 1 }, { std::sin(spillAngle), 0, std::cos(spillAngle) }) < spill,
            "hotspot strength did not redistribute visible spill intensity");
        settings.realisticLens = false;
        const auto simple = ResolveFlashlightBeamProfile(settings, 1, 0, 0);
        Require(simple.spillWeight == 1 && simple.hotspotWeight == 0 &&
            EvaluateFlashlightBeamProfile(simple, { 0, 0, 1 }, { 0, 0, 1 }) == 1 &&
            EvaluateFlashlightBeamProfile(simple, { 0, 0, 1 }, { 0, 0, -1 }) == 0,
            "simple lens lost its full forward beam or emitted behind the light");

        settings.beamSizeDegrees = 90;
        settings.edgeSoftness = 0;
        float previous = 1;
        for (unsigned step = 0; step <= 100; ++step)
        {
            settings.beamRoundness = float(step) / 100;
            const auto beam = ResolveFlashlightBeamProfile(settings, 1, 0, 0);
            const float diagonal = EvaluateFlashlightBeamProfile(beam, { 0, 0, 1 }, { .8f, .8f, 1 });
            Require(std::isfinite(diagonal) && diagonal >= 0 && diagonal <= previous + 1e-6f &&
                Near(diagonal, EvaluateFlashlightBeamProfile(beam, { 0, 0, 1 }, { -.8f, -.8f, 1 })),
                "beam roundness lost visible monotonic shape or symmetry");
            if (step == 0 || step == 100)
                Require(Near(diagonal, step == 0 ? 1.f : 0.f), "square/circle beam endpoints changed");
            previous = diagonal;
        }
        settings.edgeSoftness = 0;
        const auto tight = ResolveFlashlightBeamProfile(settings, 1, 0, 0);
        settings.edgeSoftness = 1;
        const auto broad = ResolveFlashlightBeamProfile(settings, 1, 0, 0);
        Require(tight.spillOuterCosine == broad.spillOuterCosine &&
            EvaluateFlashlightBeamProfile(tight, { 0, 0, 1 }, { .839099631f, 0, 1 }) >
            EvaluateFlashlightBeamProfile(broad, { 0, 0, 1 }, { .839099631f, 0, 1 }),
            "edge softness changed beam width or lost its visible transition");
    }

    void CheckShadowRays()
    {
        RayTracedFlashlightShadowLight light;
        light.position = { 0, 0, 10 };
        light.direction = { 0, 0, -1 };
        light.rangeMeters = 30;
        light.emitterRadiusMeters = .025f;
        light.beamProfile = ResolveFlashlightBeamProfile(FlashlightSettings{}, 1, 0, 0);
        light.beamProfile.emitterRadiusMeters = .025f;
        RayTracedFlashlightShadowSurface surface;
        surface.rayOrigin = { 0, 0, .002f };
        surface.receiverPosition = { 0, 0, 0 };
        surface.geometricNormal = surface.shadingNormal = surface.viewDirection = { 0, 0, 1 };
        const auto center = ResolveRayTracedFlashlightShadowRay(surface, light, 0, 0);
        Require(center.eligible && Near(center.directionToLight.x, 0) && Near(center.directionToLight.y, 0) &&
            Near(center.directionToLight.z, 1) && Near(center.tMax, 9.998f - .025f) && Near(center.beamWeight, 1),
            "finite shadow ray lost its receiver origin or emitter near shell");
        auto point = light;
        point.emitterRadiusMeters = point.beamProfile.emitterRadiusMeters = 0;
        const auto pointA = ResolveRayTracedFlashlightShadowRay(surface, point, .1f, .2f);
        const auto pointB = ResolveRayTracedFlashlightShadowRay(surface, point, .9f, .8f);
        Require(pointA.eligible && pointB.eligible && Near(pointA.tMax, 9.998f) && Near(pointA.beamWeight, 1) &&
            Near(pointA.directionToLight.x, pointB.directionToLight.x) &&
            Near(pointA.directionToLight.y, pointB.directionToLight.y) &&
            Near(pointA.directionToLight.z, pointB.directionToLight.z) && Near(pointA.tMax, pointB.tMax),
            "point-emitter ray depends on area sample");
        const auto first = ResolveRayTracedFlashlightShadowRay(surface, light, .25f, .1f);
        const auto second = ResolveRayTracedFlashlightShadowRay(surface, light, .75f, .6f);
        Require(first.eligible && second.eligible && (!Near(first.directionToLight.x, second.directionToLight.x, 1e-7f) ||
            !Near(first.directionToLight.y, second.directionToLight.y, 1e-7f)), "finite emitter samples collapsed to a point");
        for (const auto ray : { first, second })
        {
            const float x = surface.rayOrigin.x + ray.directionToLight.x * ray.tMax - light.position.x;
            const float y = surface.rayOrigin.y + ray.directionToLight.y * ray.tMax - light.position.y;
            const float z = surface.rayOrigin.z + ray.directionToLight.z * ray.tMax - light.position.z;
            Require(Near(std::sqrt(x * x + y * y + z * z), .025f, 2e-4f), "shadow ray did not stop on the sampled emitter shell");
        }
        auto large = light;
        large.emitterRadiusMeters = large.beamProfile.emitterRadiusMeters = .1f;
        const auto wider = ResolveRayTracedFlashlightShadowRay(surface, large, .75f, .6f);
        Require(std::hypot(wider.directionToLight.x, wider.directionToLight.y) >
            std::hypot(second.directionToLight.x, second.directionToLight.y), "larger emitter lost angular spread");
        for (unsigned defect = 0; defect < 5; ++defect)
        {
            auto receiver = surface;
            auto source = light;
            switch (defect)
            {
            case 0: receiver.receiverPosition = { 0, 0, 20 }; receiver.rayOrigin = { 0, 0, 19.998f }; break;
            case 1: receiver.geometricNormal = { 0, 0, -1 }; break;
            case 2: source.rangeMeters = 5; break;
            case 3: receiver.receiverPosition = receiver.rayOrigin = { 0, 0, 9.99f }; break;
            case 4: source.emitterRadiusMeters = 20; source.beamProfile.emitterRadiusMeters = 0; break;
            }
            Require(!ResolveRayTracedFlashlightShadowRay(receiver, source, .5f, .5f).eligible,
                "behind-light, backface, range, interior or invalid-emitter receiver became eligible");
        }
        Require(ResolveRayTracedFlashlightShadowAggregate(0, 0) == 1 &&
            ResolveRayTracedFlashlightShadowAggregate(4, 8) == 0,
            "empty or overfull shadow reduction changed");
        for (unsigned occluded = 0; occluded <= 4; ++occluded)
        {
            const auto aggregate = ResolveRayTracedFlashlightShadowAggregate(4, occluded);
            Require(Near(aggregate, 1 - float(occluded) * .25f), "shadow aggregate lost per-sample visibility");
        }
    }
}

int main()
{
    CheckFadeAndMotion();
    CheckMountAndSettings();
    CheckBeamProfile();
    CheckShadowRays();
    return EXIT_SUCCESS;
}
