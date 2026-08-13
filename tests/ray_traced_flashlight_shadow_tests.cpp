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
        const std::string materialVisibility = ReadSource(
            root / "src/ray_traced_material_visibility.hlsli");
        const std::string materialVisibilityHeader = ReadSource(
            root / "src/ray_traced_material_visibility.h");
        const std::string denoisingPass = ReadSource(
            root / "src/denoising_pass.cpp");

        assert(shader.find("RAY_FLAG_FORCE_OPAQUE") == std::string::npos);
        RequireContains(shader, "RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES");
        assert(shader.find("RAY_FLAG_ACCEPT_FIRST_HIT") ==
            std::string::npos);
        RequireContains(
            shader,
            "UVSR_COMMIT_COVERED_RAY_QUERY_CANDIDATE(query)");
        RequireContains(shader, "ray.TMax = flashlightRay.tMax;");
        RequireContains(shader, "Texture2DArray<float> t_Noise : register(t4);");
        RequireContains(shader, "UVSRSamplePrecomputedNoise(");
        RequireContains(shader, "sampleIndex < sampleCount;");
        RequireContains(shader, "ResolveRayTracedFlashlightShadowAggregate(");
        RequireContains(shader, "query.CommittedRayT()");
        RequireContains(shader, "void GenerateVisibility(");
        RequireContains(shader, "void GenerateVisibilityAndHitDistance(");

        const std::size_t opacityInitialization = materialVisibility.find(
            "float opacity = material.opacity;");
        const std::size_t opacityPreference = materialVisibility.find(
            "const bool useBaseAlphaTexture = !useOpacityTexture &&");
        const std::size_t opacityBranch = materialVisibility.find(
            "if (useOpacityTexture)");
        const std::size_t opacitySample = materialVisibility.find(
            ").r;",
            opacityBranch);
        const std::size_t baseAlphaBranch = materialVisibility.find(
            "else if (useBaseAlphaTexture)");
        const std::size_t baseAlphaSample = materialVisibility.find(
            ").a;",
            baseAlphaBranch);
        assert(opacityInitialization != std::string::npos);
        assert(opacityPreference != std::string::npos);
        assert(opacityBranch != std::string::npos);
        assert(opacitySample != std::string::npos);
        assert(baseAlphaBranch != std::string::npos);
        assert(baseAlphaSample != std::string::npos);
        assert(opacityPreference < opacityInitialization);
        assert(opacityInitialization < opacityBranch);
        assert(opacityBranch < opacitySample);
        assert(opacitySample < baseAlphaBranch);
        assert(baseAlphaBranch < baseAlphaSample);
        for (const std::string_view token : {
                "MaterialDomain_AlphaTested",
                "NonUniformResourceIndex(geometry.indexBufferIndex)",
                "NonUniformResourceIndex(geometry.vertexBufferIndex)",
                "NonUniformResourceIndex(material.opacityTextureIndex)",
                "NonUniformResourceIndex(material.baseOrDiffuseTextureIndex)",
                "CandidateInstanceContributionToHitGroupIndex()",
                "CandidateGeometryIndex()",
                "CandidatePrimitiveIndex()",
                "CandidateTriangleBarycentrics()",
                "CommitNonOpaqueTriangleHit();",
                "return saturate(opacity) >= material.alphaCutoff;" })
        {
            RequireContains(materialVisibility, token);
        }
        RequireContains(
            materialVisibility,
            "opacity *= opacityTexture.SampleLevel(");
        RequireContains(
            materialVisibility,
            "opacity *= baseTexture.SampleLevel(");

        RequireContains(pass, "nvrhi::Format::R8_UNORM");
        RequireContains(pass, "nvrhi::Format::R16_FLOAT");
        RequireContains(pass, "nvrhi::BindingLayoutItem::Texture_SRV(4)");
        RequireContains(pass, "m_BoundNoiseTexture == noiseTexture");
        RequireContains(pass, "constants.sampleSequencePhase = samplingPhase;");
        RequireContains(
            pass,
            "light->range > beamProfile.emitterRadiusMeters");
        RequireContains(
            pass,
            "nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10)");
        RequireContains(
            pass,
            "nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11)");
        RequireContains(
            pass,
            "nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12)");
        RequireContains(pass, "nvrhi::BindingLayoutItem::Sampler(0)");
        RequireContains(pass, "m_BindlessLayout");
        RequireContains(
            pass,
            "m_BoundMaterialVisibility == materialVisibility &&");
        RequireContains(
            pass,
            "materialVisibility.descriptorTable");
        RequireContains(header, "const donut::engine::SpotLight* light");
        RequireContains(
            header,
            "RayTracedMaterialVisibilityInputs m_BoundMaterialVisibility;");
        RequireContains(
            denoisingPass,
            "inputs.localLightRadius >= 0.f");
        for (const std::string_view resource : {
                "geometryBuffer == other.geometryBuffer",
                "materialBuffer == other.materialBuffer",
                "geometryIndexMap == other.geometryIndexMap",
                "descriptorTable == other.descriptorTable" })
        {
            RequireContains(materialVisibilityHeader, resource);
        }
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
    TestPointEmitterRay();
    TestFiniteEmitterSampling();
    TestEligibilityRejections();
    TestOutputEncoding();
    TestTextureCompatibility();
    TestSourceContract(argv[1]);
    return EXIT_SUCCESS;
}
