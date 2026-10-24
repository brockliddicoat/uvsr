#include "flashlight_shared.h"
#include "ray_traced_flashlight_shadows_shared.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

namespace
{
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
        profile.emitterRadiusMeters =
            UVSR_FLASHLIGHT_EMITTER_RADIUS_METERS;
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
        light.emitterRadiusMeters =
            UVSR_FLASHLIGHT_EMITTER_RADIUS_METERS;
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
                MakeLight());
        assert(ray.eligible == 1u);
        assert(Near(ray.directionToLight.x, 0.f));
        assert(Near(ray.directionToLight.y, 0.f));
        assert(Near(ray.directionToLight.z, 1.f));
        assert(std::isfinite(ray.tMax));
        assert(Near(
            ray.tMax,
            9.998f - UVSR_FLASHLIGHT_EMITTER_RADIUS_METERS));
        assert(ray.tMax < 9.998f);
        assert(Near(ray.beamWeight, 1.f));
    }

    void TestEligibilityRejections()
    {
        RayTracedFlashlightShadowSurface surface = MakeSurface();
        RayTracedFlashlightShadowLight light = MakeLight();

        surface.receiverPosition = { 0.f, 0.f, 20.f };
        surface.rayOrigin = { 0.f, 0.f, 19.998f };
        assert(ResolveRayTracedFlashlightShadowRay(
            surface, light).eligible == 0u);

        surface = MakeSurface();
        surface.geometricNormal = { 0.f, 0.f, -1.f };
        assert(ResolveRayTracedFlashlightShadowRay(
            surface, light).eligible == 0u);

        surface = MakeSurface();
        light.rangeMeters = 5.f;
        assert(ResolveRayTracedFlashlightShadowRay(
            surface, light).eligible == 0u);

        light = MakeLight();
        surface.rayOrigin = { 0.f, 0.f, 9.99f };
        surface.receiverPosition = surface.rayOrigin;
        assert(ResolveRayTracedFlashlightShadowRay(
            surface, light).eligible == 0u);
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

    std::string ReadSource(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        assert(stream.good());
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
    }

    void RequireContains(
        std::string_view source,
        std::string_view token)
    {
        if (source.find(token) == std::string_view::npos)
        {
            std::cerr << "Missing source contract token: " << token << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void TestSourceContract(const std::filesystem::path& root)
    {
        const std::string shader = ReadSource(
            root / "src/ray_traced_flashlight_shadows_cs.hlsl");
        const std::string pass = ReadSource(
            root / "src/ray_traced_flashlight_shadows.cpp");
        const std::string header = ReadSource(
            root / "src/ray_traced_flashlight_shadows.h");
        const std::string profile = ReadSource(
            root / "src/flashlight_shared.h");

        RequireContains(shader, "RAY_FLAG_FORCE_OPAQUE |");
        RequireContains(shader, "RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES");
        assert(shader.find("RAY_FLAG_ACCEPT_FIRST_HIT") ==
            std::string::npos);
        RequireContains(shader, "ray.TMax = flashlightRay.tMax;");
        RequireContains(shader, "query.CommittedRayT()");
        RequireContains(shader, "void GenerateVisibility(");
        RequireContains(shader, "void GenerateVisibilityAndHitDistance(");
        RequireContains(pass, "nvrhi::Format::R8_UNORM");
        RequireContains(pass, "nvrhi::Format::R16_FLOAT");
        RequireContains(pass, "UVSR_FLASHLIGHT_EMITTER_RADIUS_METERS");
        RequireContains(header, "const donut::engine::SpotLight* light");
        assert(shader.find("shadowChannel") == std::string::npos);
        assert(pass.find("shadowChannel") == std::string::npos);
        assert(profile.find("SHAPE_RADIUS_TAG") == std::string::npos);
        assert(profile.find("shadowChannel") == std::string::npos);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: ray_traced_flashlight_shadow_tests <source root>\n";
        return EXIT_FAILURE;
    }

    TestProfileWeightsAndBinding();
    TestFiniteReceiverToLightRay();
    TestEligibilityRejections();
    TestOutputEncoding();
    TestTextureCompatibility();
    TestSourceContract(argv[1]);
    return EXIT_SUCCESS;
}
