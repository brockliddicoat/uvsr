#include "ray_traced_sky_visibility_settings.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace
{
    struct Float2
    {
        float x;
        float y;
    };

    struct Float3
    {
        float x;
        float y;
        float z;
    };

    constexpr float TwoPi = 6.28318530717958647692f;

    bool Near(float actual, float expected, float tolerance = 1e-6f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    Float3 operator+(Float3 left, Float3 right)
    {
        return {
            left.x + right.x,
            left.y + right.y,
            left.z + right.z
        };
    }

    Float3 operator*(Float3 value, float scale)
    {
        return {
            value.x * scale,
            value.y * scale,
            value.z * scale
        };
    }

    float Dot(Float3 left, Float3 right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    Float3 Cross(Float3 left, Float3 right)
    {
        return {
            left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x
        };
    }

    Float3 Normalize(Float3 value, Float3 fallback = { 0.f, 0.f, 1.f })
    {
        const float lengthSquared = Dot(value, value);
        if (!(lengthSquared > 1e-20f) || !std::isfinite(lengthSquared))
            return fallback;
        return value * (1.f / std::sqrt(lengthSquared));
    }

    Float3 SampleCosineHemisphere(Float3 normal, Float2 sample)
    {
        normal = Normalize(normal);
        const float radialSquared = std::clamp(sample.x, 0.f, 1.f);
        const float radial = std::sqrt(radialSquared);
        const float phi = TwoPi * sample.y;
        const float normalDistance =
            std::sqrt(std::max(1.f - radialSquared, 0.f));
        const Float3 helper = std::abs(normal.z) < 0.999f
            ? Float3{ 0.f, 0.f, 1.f }
            : Float3{ 1.f, 0.f, 0.f };
        const Float3 tangent = Normalize(
            Cross(helper, normal),
            { 1.f, 0.f, 0.f });
        const Float3 bitangent = Cross(normal, tangent);
        return Normalize(
            tangent * (std::cos(phi) * radial) +
                bitangent * (std::sin(phi) * radial) +
                normal * normalDistance,
            normal);
    }

    float ResolveBinaryVisibility(uint32_t misses, uint32_t sampleCount)
    {
        assert(sampleCount > 0u);
        assert(misses <= sampleCount);
        return float(misses) / float(sampleCount);
    }

    float QuantizeR8Unorm(float value)
    {
        return std::round(std::clamp(value, 0.f, 1.f) * 255.f) / 255.f;
    }

    void TestDefaultsAndExactSampleDomain()
    {
        using namespace uvsr;

        const RayTracedSkyVisibilitySettings settings;
        assert(!settings.enabled);
        assert(settings.applyToDiffuseIbl);
        assert(settings.applyToSpecularIbl);
        assert(settings.useRatioEstimator);
        assert(!settings.outputHitDistance);
        assert(HasRayTracedSkyVisibilityConsumer(settings));
        assert(settings.sampleRateLog2 == 0);
        assert(ResolveRayTracedSkyVisibilitySampleCount(
            settings.sampleRateLog2) == 1u);
        assert(ResolveRayTracedSkyVisibilityTraceCount(settings) == 1u);
        const NoiseSettings defaultNoise;
        assert(!settings.noise.specifyNoise);
        assert(settings.noise.custom == defaultNoise);
        assert(Near(settings.rayBias, 0.002f));
        assert(settings.maxDistance ==
            RayVisibilityMaxDistance::Maximum);
        assert(IsRayTracedSkyVisibilityConfigurationSupported(settings));

        RayTracedSkyVisibilitySettings noConsumers = settings;
        noConsumers.applyToDiffuseIbl = false;
        noConsumers.applyToSpecularIbl = false;
        assert(!HasRayTracedSkyVisibilityConsumer(noConsumers));
        assert(IsRayTracedSkyVisibilityConfigurationSupported(noConsumers));

        constexpr std::array<std::string_view, 7> ExpectedLabels = {
            "1", "2", "4", "8", "16", "32", "64"
        };
        assert(RayTracedSkyVisibilityMinimumSampleRateLog2 == 0);
        assert(RayTracedSkyVisibilityMaximumSampleRateLog2 == 6);
        assert(RayTracedSkyVisibilityMaximumSamplesPerPixel == 64u);
        assert(RayTracedSkyVisibilitySampleRateLabels == ExpectedLabels);
        for (int32_t exponent = 0; exponent <= 6; ++exponent)
        {
            assert(IsRayTracedSkyVisibilitySampleRateSupported(exponent));
            assert(GetRayTracedSkyVisibilitySampleRateLabel(exponent) ==
                ExpectedLabels[size_t(exponent)]);
            assert(ResolveRayTracedSkyVisibilitySampleCount(exponent) ==
                (1u << uint32_t(exponent)));
        }
        assert(!IsRayTracedSkyVisibilitySampleRateSupported(-1));
        assert(!IsRayTracedSkyVisibilitySampleRateSupported(7));
        assert(GetRayTracedSkyVisibilitySampleRateLabel(-1).empty());
        assert(GetRayTracedSkyVisibilitySampleRateLabel(7).empty());
        assert(ResolveRayTracedSkyVisibilitySampleCount(-1) == 1u);
        assert(ResolveRayTracedSkyVisibilitySampleCount(7) == 1u);

        RayTracedSkyVisibilitySettings scalarSettings = settings;
        scalarSettings.sampleRateLog2 = 6;
        assert(ResolveRayTracedSkyVisibilityTraceCount(
            scalarSettings) == 64u);
        scalarSettings.useRatioEstimator = false;
        assert(ResolveRayTracedSkyVisibilityTraceCount(
            scalarSettings) == 1u);
        assert(RayTracedSkyVisibilityHitDistanceInvalid == 0.f);
        assert(RayTracedSkyVisibilityHitDistanceMaximum == 65472.f);
        assert(RayTracedSkyVisibilityHitDistanceMiss == 65504.f);

        constexpr std::array<RayVisibilityMaxDistance, 6> DistanceModes = {
            RayVisibilityMaxDistance::Maximum,
            RayVisibilityMaxDistance::Meters32,
            RayVisibilityMaxDistance::Meters16,
            RayVisibilityMaxDistance::Meters8,
            RayVisibilityMaxDistance::Meters4,
            RayVisibilityMaxDistance::Meters2
        };
        constexpr std::array<std::string_view, 6> DistanceLabels = {
            "Max", "32m", "16m", "8m", "4m", "2m"
        };
        constexpr std::array<float, 5> FiniteDistances = {
            32.f, 16.f, 8.f, 4.f, 2.f
        };
        for (size_t index = 0u; index < DistanceModes.size(); ++index)
        {
            assert(IsRayVisibilityMaxDistanceSupported(
                DistanceModes[index]));
            assert(GetRayVisibilityMaxDistanceLabel(
                DistanceModes[index]) == DistanceLabels[index]);
        }
        assert(Near(ResolveRayVisibilityMaxDistance(
            RayVisibilityMaxDistance::Maximum, 0.25f), 1.f));
        assert(Near(ResolveRayVisibilityMaxDistance(
            RayVisibilityMaxDistance::Maximum, 7.f), 14.f));
        for (size_t index = 0u; index < FiniteDistances.size(); ++index)
        {
            assert(Near(ResolveRayVisibilityMaxDistance(
                DistanceModes[index + 1u], 1000.f),
                FiniteDistances[index]));
        }
        assert(!IsRayVisibilityMaxDistanceSupported(
            RayVisibilityMaxDistance::Count));
        assert(!IsRayVisibilityMaxDistanceSupported(
            static_cast<RayVisibilityMaxDistance>(-1)));
        assert(GetRayVisibilityMaxDistanceLabel(
            RayVisibilityMaxDistance::Count).empty());
    }

    void TestConfigurationAndBiasValidation()
    {
        using namespace uvsr;

        RayTracedSkyVisibilitySettings settings;
        settings.rayBias = 0.f;
        assert(IsRayTracedSkyVisibilityConfigurationSupported(settings));
        settings.rayBias = RayTracedSkyVisibilityMaximumRayBias;
        assert(IsRayTracedSkyVisibilityConfigurationSupported(settings));

        settings = {};
        settings.sampleRateLog2 = -1;
        assert(!IsRayTracedSkyVisibilityConfigurationSupported(settings));
        settings.sampleRateLog2 = 7;
        assert(!IsRayTracedSkyVisibilityConfigurationSupported(settings));

        settings = {};
        settings.noise.custom.pattern = NoisePattern::Count;
        assert(!IsRayTracedSkyVisibilityConfigurationSupported(settings));
        settings.noise.custom.pattern =
            static_cast<NoisePattern>(-1);
        assert(!IsRayTracedSkyVisibilityConfigurationSupported(settings));

        settings = {};
        settings.noise.custom.resolution =
            static_cast<NoiseResolution>(0u);
        assert(!IsRayTracedSkyVisibilityConfigurationSupported(settings));

        settings = {};
        settings.maxDistance = RayVisibilityMaxDistance::Count;
        assert(!IsRayTracedSkyVisibilityConfigurationSupported(settings));
        settings.maxDistance =
            static_cast<RayVisibilityMaxDistance>(-1);
        assert(!IsRayTracedSkyVisibilityConfigurationSupported(settings));

        for (const float invalidBias : {
                -0.001f,
                RayTracedSkyVisibilityMaximumRayBias + 0.001f,
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity() })
        {
            settings = {};
            settings.rayBias = invalidBias;
            assert(!IsRayTracedSkyVisibilityConfigurationSupported(settings));
        }

        constexpr float UserBias = 0.002f;
        constexpr float SmallerDepthStep = 0.0005f;
        constexpr float LargerDepthStep = 0.003f;
        assert(Near(std::max(UserBias, SmallerDepthStep), UserBias));
        assert(Near(std::max(UserBias, LargerDepthStep), LargerDepthStep));
    }

    void TestNoiseInheritanceAndValidation()
    {
        using namespace uvsr;

        NoiseSettings global;
        global.pattern = NoisePattern::SpatialWhite;
        global.resolution = NoiseResolution::Size256;
        global.animate = false;
        RayTracedSkyVisibilitySettings settings;
        assert(!settings.noise.specifyNoise);
        assert(ResolveNoiseSettings(global, settings.noise) == global);

        settings.noise.custom.pattern = NoisePattern::SpatialBlue;
        settings.noise.custom.resolution = NoiseResolution::Size64;
        settings.noise.custom.animate = true;
        assert(ResolveNoiseSettings(global, settings.noise) == global);

        settings.noise.specifyNoise = true;
        assert(ResolveNoiseSettings(global, settings.noise) ==
            settings.noise.custom);
        assert(ResolveNoiseSettings(global, settings.noise) != global);
        assert(IsRayTracedSkyVisibilityConfigurationSupported(settings));

        settings.noise.custom.resolution =
            static_cast<NoiseResolution>(1024u);
        assert(!IsRayTracedSkyVisibilityConfigurationSupported(settings));
    }

    void TestCosineWeightedHemisphereMapping()
    {
        const std::array<Float3, 4> normals = {
            Normalize({ 0.f, 0.f, 1.f }),
            Normalize({ 0.f, 0.f, -1.f }),
            Normalize({ 1.f, 2.f, 3.f }),
            Normalize({ 1.f, -3.f, 0.25f })
        };
        constexpr uint32_t RadialSteps = 128u;
        constexpr uint32_t AzimuthSteps = 128u;
        constexpr float InverseSampleCount =
            1.f / float(RadialSteps * AzimuthSteps);

        for (const Float3 normal : normals)
        {
            Float3 sum = {};
            for (uint32_t radial = 0u; radial < RadialSteps; ++radial)
            {
                const float u =
                    (float(radial) + 0.5f) / float(RadialSteps);
                for (uint32_t azimuth = 0u;
                    azimuth < AzimuthSteps;
                    ++azimuth)
                {
                    const float v =
                        (float(azimuth) + 0.5f) /
                        float(AzimuthSteps);
                    const Float3 direction =
                        SampleCosineHemisphere(normal, { u, v });
                    assert(Near(Dot(direction, direction), 1.f, 2e-5f));
                    assert(Dot(direction, normal) >= -1e-6f);
                    sum = sum + direction;
                }
            }

            const Float3 mean = sum * InverseSampleCount;
            assert(Near(Dot(mean, normal), 2.f / 3.f, 2e-3f));
            const Float3 expected = normal * (2.f / 3.f);
            assert(Near(mean.x, expected.x, 2e-3f));
            assert(Near(mean.y, expected.y, 2e-3f));
            assert(Near(mean.z, expected.z, 2e-3f));
        }
    }

    void TestBinaryMeanAndR8UnormStorage()
    {
        using namespace uvsr;

        assert(QuantizeR8Unorm(0.f) == 0.f);
        assert(QuantizeR8Unorm(1.f) == 1.f);
        constexpr float MaximumR8Error = 0.5f / 255.f;
        for (int32_t exponent =
                RayTracedSkyVisibilityMinimumSampleRateLog2;
            exponent <= RayTracedSkyVisibilityMaximumSampleRateLog2;
            ++exponent)
        {
            const uint32_t sampleCount =
                ResolveRayTracedSkyVisibilitySampleCount(exponent);
            assert(ResolveBinaryVisibility(0u, sampleCount) == 0.f);
            assert(ResolveBinaryVisibility(sampleCount, sampleCount) == 1.f);
            for (uint32_t misses = 0u;
                misses <= sampleCount;
                ++misses)
            {
                const float visibility =
                    ResolveBinaryVisibility(misses, sampleCount);
                assert(Near(
                    visibility,
                    float(misses) / float(sampleCount)));
                const float stored = QuantizeR8Unorm(visibility);
                assert(std::abs(stored - visibility) <=
                    MaximumR8Error + 1e-7f);
            }
        }
    }
}

int main()
{
    TestDefaultsAndExactSampleDomain();
    TestConfigurationAndBiasValidation();
    TestNoiseInheritanceAndValidation();
    TestCosineWeightedHemisphereMapping();
    TestBinaryMeanAndR8UnormStorage();
    return 0;
}
