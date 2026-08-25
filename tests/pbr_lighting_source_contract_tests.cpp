#include "direct_light_visibility.h"
#include "pbr_deferred_dispatch_contract.h"
#include "pbr_deferred_lighting_bindings.h"
#include "renderer_resource_contract.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::cerr << "PBR lighting contract validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const char* message)
    {
        if (!condition)
            Fail(message);
    }

    void TestDirectLightIdentityAndTopology()
    {
        auto* texture = reinterpret_cast<nvrhi::ITexture*>(
            std::uintptr_t{ 0x1000u });
        auto* light = reinterpret_cast<donut::engine::Light*>(
            std::uintptr_t{ 0x2000u });
        auto* otherLight = reinterpret_cast<donut::engine::Light*>(
            std::uintptr_t{ 0x3000u });
        constexpr std::array<std::uint32_t, 5> SampleCounts = {
            1u, 2u, 4u, 8u, 16u
        };
        for (const std::uint32_t sampleCount : SampleCounts)
        {
            const uvsr::DirectLightVisibility visibility{
                texture, light, sampleCount
            };
            Require(visibility.IsComplete(),
                "supported receiver sample count was rejected");
            Require(uvsr::TargetsDirectLight(visibility, light),
                "pointer-identical direct light was not targeted");
            Require(!uvsr::TargetsDirectLight(visibility, otherLight),
                "visibility targeted a different light object");

            const uvsr::DirectLightVisibilityTextureProperties properties{
                640u, 360u, 1u,
                sampleCount == 1u ? 1u : sampleCount,
                1u, 1u, true,
                sampleCount == 1u,
                sampleCount != 1u,
                true
            };
            Require(uvsr::IsDirectLightVisibilityTextureCompatible(
                    properties, 640u, 360u, sampleCount),
                "exact receiver texture topology was rejected");
        }
        Require(!uvsr::DirectLightVisibility{ texture, light, 3u }.IsComplete(),
            "unsupported receiver sample count was accepted");
        Require(!uvsr::DirectLightVisibility{ nullptr, light, 1u }.IsComplete(),
            "missing visibility texture was accepted");
        Require(!uvsr::DirectLightVisibility{ texture, nullptr, 1u }.IsComplete(),
            "missing light identity was accepted");
    }

    void TestVisibilityComposition()
    {
        Require(uvsr::ComposeDirectLightVisibility(0.8f, 0.25f, true) ==
                0.25f,
            "matching producers did not compose by minimum");
        Require(uvsr::ComposeDirectLightVisibility(0.8f, 0.25f, false) ==
                0.8f,
            "unmatched producer modulated direct light");
        Require(uvsr::ComposeDirectLightVisibility(2.f, -1.f, true) == 0.f,
            "visibility composition did not clamp both operands");
        Require(uvsr::ApplyClosestVisibilityCorrection(0.f, 0.4f, 0.7f) ==
                0.f &&
                uvsr::ApplyClosestVisibilityCorrection(0.4f, 0.4f, 0.7f) ==
                    0.7f &&
                uvsr::ApplyClosestVisibilityCorrection(1.f, 0.4f, 0.7f) ==
                    1.f,
            "closest-surface correction changed its endpoints or pivot");
    }

    void TestBindingFallbacksAndAtomicReplacement()
    {
        static_assert(uvsr::PbrVisibilitySlots[0] == 20u &&
            uvsr::PbrVisibilitySlots[1] == 21u &&
            uvsr::PbrVisibilitySlots[2] == 22u);
        static_assert(uvsr::PbrMsaaSampleCounts[0] == 2u &&
            uvsr::PbrMsaaSampleCounts[1] == 4u &&
            uvsr::PbrMsaaSampleCounts[2] == 8u &&
            uvsr::PbrMsaaSampleCounts[3] == 16u);
        static_assert(uvsr::PbrNeutralVisibilityByte == 0xffu);

        int flashlight = 1;
        int sun = 2;
        int sky = 3;
        int white = 4;
        const auto active = uvsr::ResolvePbrVisibilityResources(
            uvsr::PbrVisibilityResources<int*>{
                &flashlight, &sun, &sky },
            &white);
        Require(active.flashlight == &flashlight && active.sun == &sun &&
                active.sky == &sky,
            "active visibility resource identity changed");
        const auto neutral = uvsr::ResolvePbrVisibilityResources(
            uvsr::PbrVisibilityResources<int*>{},
            &white);
        Require(neutral.flashlight == &white && neutral.sun == &white &&
                neutral.sky == &white,
            "missing visibility did not fail open to white");

        std::array<int, 3> published = { 10, 20, 30 };
        const std::array<bool, 3> replace = { true, true, false };
        Require(!uvsr::TryReplaceRendererResources(
                published,
                replace,
                [](std::size_t index) { return index == 1u ? 0 : 100; }),
            "failed resource transaction was accepted");
        Require(published == std::array<int, 3>{ 10, 20, 30 },
            "failed resource transaction changed published handles");
        Require(uvsr::TryReplaceRendererResources(
                published,
                replace,
                [](std::size_t index) { return 100 + int(index); }),
            "complete resource transaction was rejected");
        Require(published == std::array<int, 3>{ 100, 101, 30 },
            "complete resource transaction was not atomic");
    }

    void TestDispatchPublication()
    {
        using uvsr::PbrDeferredDispatchIsReady;
        Require(PbrDeferredDispatchIsReady(true, true, true, true, true),
            "complete dispatch state was rejected");
        for (std::uint32_t missing = 0u; missing < 5u; ++missing)
        {
            bool state[5] = { true, true, true, true, true };
            state[missing] = false;
            Require(!PbrDeferredDispatchIsReady(
                    state[0], state[1], state[2], state[3], state[4]),
                "incomplete dispatch state was accepted");
        }

        uvsr::PbrDeferredLightingRenderTransaction transaction(2u);
        int dispatchCount = 0;
        Require(uvsr::ExecutePbrDeferredLightingView(
                transaction, true, [&] { ++dispatchCount; }),
            "first view dispatch failed");
        Require(!transaction.Finish().Succeeded(),
            "partial multi-view dispatch was published");
        Require(uvsr::ExecutePbrDeferredLightingView(
                transaction, true, [&] { ++dispatchCount; }),
            "second view dispatch failed");
        const auto result = transaction.Finish();
        Require(result.Succeeded() && result.dispatchedViewCount == 2u &&
                dispatchCount == 2,
            "complete multi-view dispatch was not published");

        uvsr::PbrDeferredLightingRenderTransaction failed(2u);
        Require(!uvsr::ExecutePbrDeferredLightingView(
                failed, false, [] {}),
            "failed prerequisite dispatched");
        Require(!failed.Finish().Succeeded(),
            "failed prerequisite published a PBR result");
    }
}

int main()
{
    TestDirectLightIdentityAndTopology();
    TestVisibilityComposition();
    TestBindingFallbacksAndAtomicReplacement();
    TestDispatchPublication();
    std::cout << "UVSR PBR lighting direct contracts passed\n";
    return EXIT_SUCCESS;
}
