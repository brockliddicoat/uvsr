#include "ray_traced_sky_visibility_settings.h"
#include "visibility_blue_noise.h"

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
#include <vector>

namespace
{
    struct Float2
    {
        float x;
        float y;

        bool operator==(const Float2& other) const
        {
            return x == other.x && y == other.y;
        }

        bool operator!=(const Float2& other) const
        {
            return !(*this == other);
        }
    };

    struct Float3
    {
        float x;
        float y;
        float z;
    };

    constexpr float TwoPi = 6.28318530717958647692f;
    constexpr float Uint24Scale = 1.f / 16777216.f;

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

    float Fraction(float value)
    {
        return value - std::floor(value);
    }

    float RadicalInverse(uint32_t index, uint32_t base)
    {
        const float inverseBase = 1.f / float(base);
        float inversePower = inverseBase;
        float result = 0.f;
        while (index > 0u)
        {
            const uint32_t digit = index % base;
            result += float(digit) * inversePower;
            index /= base;
            inversePower *= inverseBase;
        }
        return result;
    }

    float GoldenWeylPhase(uint32_t frameIndex)
    {
        const uint32_t phase = frameIndex * 0x9e3779b9u;
        return float(phase >> 8u) * Uint24Scale;
    }

    float PermutatedWhiteNoise(
        uint32_t x,
        uint32_t y,
        uint32_t dimension,
        uint32_t phase)
    {
        uint32_t state = x + y * 65537u +
            dimension * 747796405u + phase * 2891336453u + 1u;
        state = state * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) *
            277803737u;
        word = (word >> 22u) ^ word;
        return float((word >> 8u) & 0x00ffffffu) * Uint24Scale;
    }

    float BlueNoise(
        const std::vector<uint16_t>& noise,
        uint32_t x,
        uint32_t y,
        uint32_t layer)
    {
        using namespace uvsr;
        const size_t texel = size_t(y & 63u) * VisibilityBlueNoiseSize +
            size_t(x & 63u);
        return float(noise[
            size_t(layer % VisibilityBlueNoiseLayerCount) *
                VisibilityBlueNoiseTexelCount + texel]) / 65535.f;
    }

    uint32_t ResolvePhase(
        const uvsr::RayTracedSkyVisibilitySettings& settings,
        uint32_t requestedPhase)
    {
        return settings.animateSamples ? requestedPhase : 0u;
    }

    Float2 Sample2D(
        const std::vector<uint16_t>& noise,
        uvsr::RayTracedSkyVisibilityNoisePattern pattern,
        uint32_t x,
        uint32_t y,
        uint32_t sampleIndex,
        uint32_t phase)
    {
        using namespace uvsr;
        const uint32_t firstDimension = sampleIndex * 2u;
        if (pattern ==
            RayTracedSkyVisibilityNoisePattern::PermutatedWhiteNoise)
        {
            return {
                PermutatedWhiteNoise(
                    x, y, firstDimension, phase),
                PermutatedWhiteNoise(
                    x, y, firstDimension + 1u, phase)
            };
        }

        const uint32_t sequenceIndex = sampleIndex + 1u;
        return {
            Fraction(
                RadicalInverse(sequenceIndex, 2u) +
                BlueNoise(noise, x, y, 0u) +
                RadicalInverse(phase + 1u, 5u)),
            Fraction(
                RadicalInverse(sequenceIndex, 3u) +
                BlueNoise(noise, x, y, 1u) +
                GoldenWeylPhase(phase))
        };
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
        assert(!settings.applyToSpecularIbl);
        assert(HasRayTracedSkyVisibilityConsumer(settings));
        assert(settings.sampleRateLog2 == 0);
        assert(ResolveRayTracedSkyVisibilitySampleCount(
            settings.sampleRateLog2) == 1u);
        assert(settings.noisePattern ==
            RayTracedSkyVisibilityNoisePattern::VoidClusterBlueNoise);
        assert(settings.animateSamples);
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
        settings.noisePattern = RayTracedSkyVisibilityNoisePattern::Count;
        assert(!IsRayTracedSkyVisibilityConfigurationSupported(settings));
        settings.noisePattern =
            static_cast<RayTracedSkyVisibilityNoisePattern>(-1);
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

    void TestDeterministicNoiseAndAnimationPolicy()
    {
        using namespace uvsr;

        const std::vector<uint16_t> noise = GenerateVisibilityBlueNoise();
        assert(noise.size() == size_t(VisibilityBlueNoiseTexelCount) *
            VisibilityBlueNoiseLayerCount);

        RayTracedSkyVisibilitySettings settings;
        for (const RayTracedSkyVisibilityNoisePattern pattern : {
                RayTracedSkyVisibilityNoisePattern::PermutatedWhiteNoise,
                RayTracedSkyVisibilityNoisePattern::VoidClusterBlueNoise })
        {
            settings.noisePattern = pattern;
            const uint32_t heldPhase = ResolvePhase(settings, 11u);
            const Float2 held = Sample2D(
                noise, pattern, 17u, 29u, 3u, heldPhase);
            const Float2 repeated = Sample2D(
                noise, pattern, 17u, 29u, 3u, heldPhase);
            const Float2 advanced = Sample2D(
                noise,
                pattern,
                17u,
                29u,
                3u,
                ResolvePhase(settings, 12u));
            assert(held == repeated);
            assert(held.x >= 0.f && held.x < 1.f);
            assert(held.y >= 0.f && held.y < 1.f);
            assert(held != advanced);

            settings.animateSamples = false;
            const Float2 frozenA = Sample2D(
                noise,
                pattern,
                17u,
                29u,
                3u,
                ResolvePhase(settings, 11u));
            const Float2 frozenB = Sample2D(
                noise,
                pattern,
                17u,
                29u,
                3u,
                ResolvePhase(settings, 999u));
            const Float2 phaseZero = Sample2D(
                noise, pattern, 17u, 29u, 3u, 0u);
            assert(frozenA == frozenB);
            assert(frozenA == phaseZero);
            settings.animateSamples = true;
        }

        const Float2 white = Sample2D(
            noise,
            RayTracedSkyVisibilityNoisePattern::PermutatedWhiteNoise,
            17u,
            29u,
            3u,
            11u);
        const Float2 blue = Sample2D(
            noise,
            RayTracedSkyVisibilityNoisePattern::VoidClusterBlueNoise,
            17u,
            29u,
            3u,
            11u);
        assert(white != blue);
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
    TestDeterministicNoiseAndAnimationPolicy();
    TestCosineWeightedHemisphereMapping();
    TestBinaryMeanAndR8UnormStorage();
    return 0;
}
