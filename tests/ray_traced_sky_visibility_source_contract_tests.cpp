#include "ray_traced_sky_visibility_result.h"
#include "ray_traced_sky_visibility_settings.h"
#include "ray_visibility_trace_contract.h"
#include "renderer_receiver_texture_contract.h"
#include "renderer_resource_contract.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::cerr << "Sky visibility contract validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const char* message)
    {
        if (!condition)
            Fail(message);
    }

    nvrhi::TextureDesc MakeReceiver(std::uint32_t sampleCount)
    {
        nvrhi::TextureDesc descriptor;
        descriptor.width = 640u;
        descriptor.height = 360u;
        descriptor.depth = 1u;
        descriptor.arraySize = 1u;
        descriptor.mipLevels = 1u;
        descriptor.sampleCount = sampleCount;
        descriptor.dimension = sampleCount == 1u
            ? nvrhi::TextureDimension::Texture2D
            : nvrhi::TextureDimension::Texture2DMS;
        return descriptor;
    }

    void TestSettingsAndReceiverTopologies()
    {
        for (std::int32_t exponent = 0; exponent <= 6; ++exponent)
        {
            Require(uvsr::IsRayTracedSkyVisibilitySampleRateSupported(
                    exponent),
                "supported sky sample rate was rejected");
            Require(uvsr::ResolveRayTracedSkyVisibilitySampleCount(exponent) ==
                    (1u << std::uint32_t(exponent)),
                "sky sample-rate exponent changed meaning");
        }
        Require(!uvsr::IsRayTracedSkyVisibilitySampleRateSupported(-1) &&
                !uvsr::IsRayTracedSkyVisibilitySampleRateSupported(7),
            "out-of-domain sky sample rate was accepted");

        constexpr std::array<std::uint32_t, 5> ReceiverCounts = {
            1u, 2u, 4u, 8u, 16u
        };
        for (const std::uint32_t sampleCount : ReceiverCounts)
        {
            const nvrhi::TextureDesc descriptor = MakeReceiver(sampleCount);
            Require(uvsr::AreRendererReceiverTextureDescriptorsCompatible(
                    descriptor, descriptor, descriptor),
                "exact receiver descriptor topology was rejected");
        }
        nvrhi::TextureDesc invalid = MakeReceiver(4u);
        invalid.dimension = nvrhi::TextureDimension::Texture2D;
        Require(!uvsr::AreRendererReceiverTextureDescriptorsCompatible(
                invalid, MakeReceiver(4u), MakeReceiver(4u)),
            "single-sample dimension accepted a multisample receiver");
        invalid = MakeReceiver(3u);
        Require(!uvsr::AreRendererReceiverTextureDescriptorsCompatible(
                invalid, invalid, invalid),
            "unsupported receiver sample count was accepted");
    }

    void TestMandatoryResourcePublication()
    {
        std::array<int, 2> published = { 10, 20 };
        const std::array<bool, 2> replace = { true, true };
        Require(!uvsr::TryReplaceRendererResources(
                published,
                replace,
                [](std::size_t index) { return index == 1u ? 0 : 100; }),
            "partial mandatory sky output was accepted");
        Require(published == std::array<int, 2>{ 10, 20 },
            "partial mandatory sky output changed published resources");
        Require(uvsr::TryReplaceRendererResources(
                published,
                replace,
                [](std::size_t index) { return 100 + int(index); }),
            "complete mandatory sky outputs were rejected");
        Require(published == std::array<int, 2>{ 100, 101 },
            "complete mandatory sky outputs were not committed atomically");
    }

    void TestResultPublication()
    {
        auto* visibility = reinterpret_cast<nvrhi::ITexture*>(
            std::uintptr_t{ 0x1000u });
        auto* closest = reinterpret_cast<nvrhi::ITexture*>(
            std::uintptr_t{ 0x2000u });
        auto* hit = reinterpret_cast<nvrhi::ITexture*>(
            std::uintptr_t{ 0x3000u });
        constexpr std::array<std::uint32_t, 5> ReceiverCounts = {
            1u, 2u, 4u, 8u, 16u
        };
        for (const std::uint32_t sampleCount : ReceiverCounts)
        {
            Require(bool(uvsr::RayTracedSkyVisibilityResult{
                    visibility, closest, nullptr, sampleCount, true }),
                "dispatched mandatory result without hit distance was rejected");
            Require(bool(uvsr::RayTracedSkyVisibilityResult{
                    visibility, closest, hit, sampleCount, true }),
                "dispatched hit-distance result was rejected");
        }
        Require(!bool(uvsr::RayTracedSkyVisibilityResult{
                nullptr, closest, hit, 1u, true }),
            "missing visibility output was published");
        Require(!bool(uvsr::RayTracedSkyVisibilityResult{
                visibility, nullptr, hit, 1u, true }),
            "missing closest output was published");
        Require(!bool(uvsr::RayTracedSkyVisibilityResult{
                visibility, closest, hit, 3u, true }),
            "unsupported receiver topology was published");
        Require(!bool(uvsr::RayTracedSkyVisibilityResult{
                visibility, closest, hit, 1u, false }),
            "undispatched result was published");
    }

    void TestTraceAggregation()
    {
        constexpr float Maximum = 100.f;
        constexpr float Miss = 200.f;
        RayVisibilityTraceAggregate aggregate =
            BeginRayVisibilityTraceAggregate(Miss);
        aggregate = AccumulateRayVisibilityTraceSample(
            aggregate,
            ResolveRayVisibilityTraceSample(
                false, 0.f, true, Maximum, Miss));
        aggregate = AccumulateRayVisibilityTraceSample(
            aggregate,
            ResolveRayVisibilityTraceSample(
                true, 40.f, true, Maximum, Miss));
        aggregate = AccumulateRayVisibilityTraceSample(
            aggregate,
            ResolveRayVisibilityTraceSample(
                false, 0.f, true, Maximum, Miss));
        aggregate = AccumulateRayVisibilityTraceSample(
            aggregate,
            ResolveRayVisibilityTraceSample(
                true, 20.f, true, Maximum, Miss));
        Require(RayVisibilityTraceAggregateIsComplete(aggregate, 4u),
            "one query per requested sample was not retained");
        Require(aggregate.closestHitDistance == 20.f,
            "closest committed blocker was not retained");
        Require(ResolveRayVisibilityTraceAverage(aggregate) == 0.5f,
            "binary sky visibility mean changed");

        const RayVisibilityTraceSample malformed =
            ResolveRayVisibilityTraceSample(
                true,
                std::numeric_limits<float>::quiet_NaN(),
                true,
                Maximum,
                Miss);
        Require(malformed.occluded == 1u &&
                malformed.hitDistance == Maximum,
            "malformed committed hit was converted to a miss");
        const RayVisibilityTraceSample optionalOff =
            ResolveRayVisibilityTraceSample(
                true, 12.f, false, Maximum, Miss);
        Require(optionalOff.occluded == 1u && optionalOff.hitDistance == 0.f,
            "disabled hit-distance output changed binary visibility");
    }
}

int main()
{
    TestSettingsAndReceiverTopologies();
    TestMandatoryResourcePublication();
    TestResultPublication();
    TestTraceAggregation();
    std::cout << "UVSR sky visibility direct contracts passed\n";
    return EXIT_SUCCESS;
}
