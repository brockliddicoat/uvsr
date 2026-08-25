#include "flashlight_shared.h"
#include "ray_traced_flashlight_shadows_shared.h"
#include "ray_traced_material_visibility.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    constexpr float TestEmitterRadiusMeters = 0.025f;

    bool Near(float left, float right, float tolerance = 1e-5f)
    {
        return std::abs(left - right) <= tolerance;
    }

    FlashlightBeamProfile MakeProfile()
    {
        constexpr float RadiansPerHalfDegree =
            3.14159265358979323846f / 360.f;
        FlashlightBeamProfile profile = {};
        profile.beamRightX = 1.f;
        profile.shapeExponent = 4.f;
        profile.spillInnerCosine = std::cos(
            12.f * RadiansPerHalfDegree);
        profile.spillOuterCosine = std::cos(
            16.f * RadiansPerHalfDegree);
        profile.spillWeight = 0.3f;
        profile.hotspotWeight = 0.7f;
        profile.hotspotInnerCosine = std::cos(
            4.f * RadiansPerHalfDegree);
        profile.hotspotOuterCosine = std::cos(
            6.f * RadiansPerHalfDegree);
        profile.emitterRadiusMeters = TestEmitterRadiusMeters;
        profile.active = 1.f;
        return profile;
    }

    FlashlightSharedFloat3 DirectionAtDegrees(float degrees)
    {
        constexpr float RadiansPerDegree =
            3.14159265358979323846f / 180.f;
        const float radians = degrees * RadiansPerDegree;
        return { std::sin(radians), 0.f, std::cos(radians) };
    }

    RayTracedFlashlightShadowLight MakeLight()
    {
        RayTracedFlashlightShadowLight light;
        light.position = { 0.f, 0.f, 10.f };
        light.rangeMeters = 30.f;
        light.direction = { 0.f, 0.f, -1.f };
        light.emitterRadiusMeters = TestEmitterRadiusMeters;
        light.beamProfile = MakeProfile();
        return light;
    }

    RayTracedFlashlightShadowSurface MakeSurface()
    {
        RayTracedFlashlightShadowSurface surface;
        surface.rayOrigin = { 0.f, 0.f, 0.002f };
        surface.receiverPosition = { 0.f, 0.f, 0.f };
        surface.geometricNormal = { 0.f, 0.f, 1.f };
        surface.shadingNormal = { 0.f, 0.f, 1.f };
        surface.viewDirection = { 0.f, 0.f, 1.f };
        return surface;
    }

    void TestProfileWeightsAndBinding()
    {
        const FlashlightBeamProfile profile = MakeProfile();
        assert(FlashlightBeamProfileIsValid(profile));
        assert(Near(profile.spillInnerCosine,
            std::cos(12.f * 3.14159265358979323846f / 360.f)));
        assert(Near(profile.hotspotOuterCosine,
            std::cos(6.f * 3.14159265358979323846f / 360.f)));

        const FlashlightSharedFloat3 lightDirection = { 0.f, 0.f, 1.f };
        assert(Near(EvaluateFlashlightBeamProfile(
            profile,
            lightDirection,
            { 0.f, 0.f, 1.f }), 1.f));
        assert(Near(EvaluateFlashlightBeamProfile(
            profile,
            lightDirection,
            DirectionAtDegrees(4.f)), 0.3f));
        assert(Near(EvaluateFlashlightBeamProfile(
            profile,
            lightDirection,
            DirectionAtDegrees(9.f)), 0.f));
        assert(Near(EvaluateFlashlightBeamProfile(
            profile,
            lightDirection,
            { 0.f, 0.f, -1.f }), 0.f));

        FlashlightBeamProfileBinding binding = {};
        binding.profile = profile;
        binding.lightIndex = 7;
        assert(binding.lightIndex == 7);
        assert(sizeof(binding) == 64u);
    }

    void TestFiniteReceiverToLightRay()
    {
        const RayTracedFlashlightShadowRay ray =
            ResolveRayTracedFlashlightShadowRay(
                MakeSurface(),
                MakeLight(),
                0.f,
                0.f);
        assert(ray.eligible == 1u);
        assert(Near(ray.directionToLight.x, 0.f));
        assert(Near(ray.directionToLight.y, 0.f));
        assert(Near(ray.directionToLight.z, 1.f));
        assert(std::isfinite(ray.tMax));
        assert(Near(
            ray.tMax,
            9.998f - TestEmitterRadiusMeters));
        assert(ray.tMax < 9.998f);
        assert(Near(ray.beamWeight, 1.f));
    }

    void TestPointEmitterRay()
    {
        RayTracedFlashlightShadowLight light = MakeLight();
        light.emitterRadiusMeters = 0.f;
        light.beamProfile.emitterRadiusMeters = 0.f;
        const RayTracedFlashlightShadowRay ray =
            ResolveRayTracedFlashlightShadowRay(
                MakeSurface(), light, 0.1f, 0.2f);
        assert(ray.eligible == 1u);
        assert(std::isfinite(ray.directionToLight.x));
        assert(std::isfinite(ray.directionToLight.y));
        assert(std::isfinite(ray.directionToLight.z));
        assert(Near(ray.tMax, 9.998f));
        assert(Near(ray.beamWeight, 1.f));
        const RayTracedFlashlightShadowRay otherSample =
            ResolveRayTracedFlashlightShadowRay(
                MakeSurface(), light, 0.9f, 0.8f);
        assert(Near(otherSample.directionToLight.x,
            ray.directionToLight.x));
        assert(Near(otherSample.directionToLight.y,
            ray.directionToLight.y));
        assert(Near(otherSample.directionToLight.z,
            ray.directionToLight.z));
        assert(Near(otherSample.tMax, ray.tMax));
    }

    void TestFiniteEmitterSampling()
    {
        const RayTracedFlashlightShadowSurface surface = MakeSurface();
        const RayTracedFlashlightShadowLight light = MakeLight();
        const RayTracedFlashlightShadowRay first =
            ResolveRayTracedFlashlightShadowRay(
                surface, light, 0.25f, 0.1f);
        const RayTracedFlashlightShadowRay second =
            ResolveRayTracedFlashlightShadowRay(
                surface, light, 0.75f, 0.6f);
        assert(first.eligible == 1u);
        assert(second.eligible == 1u);
        assert(!Near(first.directionToLight.x,
            second.directionToLight.x, 1e-7f) ||
            !Near(first.directionToLight.y,
                second.directionToLight.y, 1e-7f));

        const auto endpointDistance = [&](const auto& ray)
        {
            const float x = surface.rayOrigin.x +
                ray.directionToLight.x * ray.tMax - light.position.x;
            const float y = surface.rayOrigin.y +
                ray.directionToLight.y * ray.tMax - light.position.y;
            const float z = surface.rayOrigin.z +
                ray.directionToLight.z * ray.tMax - light.position.z;
            return std::sqrt(x * x + y * y + z * z);
        };
        assert(Near(endpointDistance(first),
            light.emitterRadiusMeters, 2e-4f));
        assert(Near(endpointDistance(second),
            light.emitterRadiusMeters, 2e-4f));

        RayTracedFlashlightShadowLight largerLight = light;
        largerLight.emitterRadiusMeters = 0.1f;
        largerLight.beamProfile.emitterRadiusMeters = 0.1f;
        const RayTracedFlashlightShadowRay larger =
            ResolveRayTracedFlashlightShadowRay(
                surface, largerLight, 0.75f, 0.6f);
        const float smallSpread = std::sqrt(
            second.directionToLight.x * second.directionToLight.x +
            second.directionToLight.y * second.directionToLight.y);
        const float largeSpread = std::sqrt(
            larger.directionToLight.x * larger.directionToLight.x +
            larger.directionToLight.y * larger.directionToLight.y);
        assert(largeSpread > smallSpread);
    }

    void TestEligibilityRejections()
    {
        RayTracedFlashlightShadowSurface surface = MakeSurface();
        RayTracedFlashlightShadowLight light = MakeLight();

        surface.receiverPosition = { 0.f, 0.f, 20.f };
        surface.rayOrigin = { 0.f, 0.f, 19.998f };
        assert(ResolveRayTracedFlashlightShadowRay(
            surface, light, 0.5f, 0.5f).eligible == 0u);

        surface = MakeSurface();
        surface.geometricNormal = { 0.f, 0.f, -1.f };
        assert(ResolveRayTracedFlashlightShadowRay(
            surface, light, 0.5f, 0.5f).eligible == 0u);

        surface = MakeSurface();
        light.rangeMeters = 5.f;
        assert(ResolveRayTracedFlashlightShadowRay(
            surface, light, 0.5f, 0.5f).eligible == 0u);

        light = MakeLight();
        surface.rayOrigin = { 0.f, 0.f, 9.99f };
        surface.receiverPosition = surface.rayOrigin;
        assert(ResolveRayTracedFlashlightShadowRay(
            surface, light, 0.5f, 0.5f).eligible == 0u);

        light = MakeLight();
        light.emitterRadiusMeters = 20.f;
        light.beamProfile.emitterRadiusMeters = 0.f;
        assert(ResolveRayTracedFlashlightShadowRay(
            MakeSurface(), light, 0.5f, 0.5f).eligible == 0u);
    }

    void TestOutputEncoding()
    {
        const RayTracedFlashlightShadowEncoding invalid =
            ResolveRayTracedFlashlightShadowEncoding(0u, 0u, 0.f);
        assert(Near(invalid.visibility, 1.f));
        assert(Near(invalid.hitDistance, 0.f));

        const RayTracedFlashlightShadowEncoding miss =
            ResolveRayTracedFlashlightShadowEncoding(1u, 0u, 0.f);
        assert(Near(miss.visibility, 1.f));
        assert(Near(
            miss.hitDistance,
            RayTracedFlashlightMissHitDistance));

        const RayTracedFlashlightShadowEncoding hit =
            ResolveRayTracedFlashlightShadowEncoding(1u, 1u, 2.5f);
        assert(Near(hit.visibility, 0.f));
        assert(Near(hit.hitDistance, 2.5f));

        const RayTracedFlashlightShadowEncoding malformed =
            ResolveRayTracedFlashlightShadowEncoding(
                1u,
                1u,
                std::numeric_limits<float>::quiet_NaN());
        assert(Near(malformed.visibility, 1.f));
        assert(Near(
            malformed.hitDistance,
            RayTracedFlashlightMissHitDistance));

        for (unsigned int occluded = 0u; occluded <= 4u; ++occluded)
        {
            const RayTracedFlashlightShadowEncoding aggregate =
                ResolveRayTracedFlashlightShadowAggregate(
                    4u, occluded, 2.5f);
            assert(Near(
                aggregate.visibility,
                1.f - float(occluded) * 0.25f));
            assert(Near(
                aggregate.hitDistance,
                occluded == 0u
                    ? RayTracedFlashlightMissHitDistance
                    : 2.5f));
        }
    }

    void TestTextureCompatibility()
    {
        using namespace uvsr;
        constexpr RayTracedFlashlightTextureShape Reference = {
            1920u, 1080u, 1u, 1u, 1u, 1u, true, true
        };
        static_assert(IsRayTracedFlashlightTextureShapeValid(Reference));
        static_assert(IsRayTracedFlashlightTextureShapeCompatible(
            Reference, Reference));
        static_assert(!IsRayTracedFlashlightTextureShapeCompatible(
            Reference,
            { 1280u, 720u, 1u, 1u, 1u, 1u, true, true }));
        static_assert(!IsRayTracedFlashlightTextureShapeCompatible(
            Reference,
            { 1920u, 1080u, 1u, 1u, 1u, 4u, true, true }));
        static_assert(!IsRayTracedFlashlightTextureShapeCompatible(
            Reference,
            { 1920u, 1080u, 1u, 1u, 1u, 1u, false, true }));
        static_assert(!IsRayTracedFlashlightTextureShapeCompatible(
            Reference,
            { 1920u, 1080u, 1u, 1u, 1u, 1u, true, false }));
    }

    void TestMaterialVisibilityInputIdentity()
    {
        using uvsr::RayTracedMaterialVisibilityInputs;
        auto* const geometry = reinterpret_cast<nvrhi::IBuffer*>(
            std::uintptr_t{ 0x1000u });
        auto* const material = reinterpret_cast<nvrhi::IBuffer*>(
            std::uintptr_t{ 0x2000u });
        auto* const geometryMap = reinterpret_cast<nvrhi::IBuffer*>(
            std::uintptr_t{ 0x3000u });
        auto* const instances = reinterpret_cast<nvrhi::IBuffer*>(
            std::uintptr_t{ 0x4000u });
        auto* const descriptorTable =
            reinterpret_cast<nvrhi::IDescriptorTable*>(
                std::uintptr_t{ 0x5000u });

        const RayTracedMaterialVisibilityInputs complete = {
            geometry,
            material,
            geometryMap,
            instances,
            descriptorTable
        };
        assert(static_cast<bool>(complete));
        assert(complete == complete);

        RayTracedMaterialVisibilityInputs changed = complete;
        changed.geometryBuffer = nullptr;
        assert(!static_cast<bool>(changed) && changed != complete);
        changed = complete;
        changed.materialBuffer = nullptr;
        assert(!static_cast<bool>(changed) && changed != complete);
        changed = complete;
        changed.geometryIndexMap = nullptr;
        assert(!static_cast<bool>(changed) && changed != complete);
        changed = complete;
        changed.instanceBuffer = nullptr;
        assert(static_cast<bool>(changed) && changed != complete);
        changed = complete;
        changed.descriptorTable = nullptr;
        assert(!static_cast<bool>(changed) && changed != complete);
    }
}

int main()
{
    TestProfileWeightsAndBinding();
    TestFiniteReceiverToLightRay();
    TestPointEmitterRay();
    TestFiniteEmitterSampling();
    TestEligibilityRejections();
    TestOutputEncoding();
    TestTextureCompatibility();
    TestMaterialVisibilityInputIdentity();
    return EXIT_SUCCESS;
}
