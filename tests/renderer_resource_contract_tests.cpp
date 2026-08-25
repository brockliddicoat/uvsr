#include "pbr_deferred_lighting_bindings.h"
#include "ray_traced_sky_visibility_bindings.h"
#include "renderer_environment_bindings.h"
#include "renderer_resource_contract.h"
#include "renderer_pixel_readback_cb.h"
#include "sky_visibility_application.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Renderer resource contract test failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void TestCommonInitializationFailures()
    {
        using uvsr::RendererCommonInitializationContract;
        const RendererCommonInitializationContract complete = {
            true, true, true, true, true, true, true,
            true, true, true, true, true, true
        };
        Require(complete.IsComplete(), "complete common state was rejected");

        bool RendererCommonInitializationContract::* const fields[] = {
            &RendererCommonInitializationContract::device,
            &RendererCommonInitializationContract::fullscreenZeroShader,
            &RendererCommonInitializationContract::fullscreenOneShader,
            &RendererCommonInitializationContract::blitShader,
            &RendererCommonInitializationContract::linearClampSampler,
            &RendererCommonInitializationContract::linearWrapSampler,
            &RendererCommonInitializationContract::blackTexture,
            &RendererCommonInitializationContract::whiteTexture,
            &RendererCommonInitializationContract::blackCubeArray,
            &RendererCommonInitializationContract::blackDepthArray,
            &RendererCommonInitializationContract::blitBindingLayout,
            &RendererCommonInitializationContract::uploadCommandList,
            &RendererCommonInitializationContract::uploadSubmitted
        };
        for (auto field : fields)
        {
            RendererCommonInitializationContract failed = complete;
            failed.*field = false;
            Require(!failed.IsComplete(),
                "common creation failure was published");
        }
    }

    void TestInjectedCreationFailures()
    {
        constexpr std::size_t StageCount = 4u;
        for (std::size_t failedStage = 0u;
            failedStage < StageCount;
            ++failedStage)
        {
            uvsr::RendererResourceCreationSequence sequence;
            std::size_t calls = 0u;
            for (std::size_t stage = 0u; stage < StageCount; ++stage)
            {
                (void)sequence.Require([&calls, stage, failedStage]
                {
                    ++calls;
                    return stage != failedStage;
                });
            }
            Require(!sequence.IsValid(),
                "injected resource failure was accepted");
            Require(calls == failedStage + 1u,
                "dependent creation ran after sampler/layout/command-list/"
                "pipeline failure");
        }
    }

    void TestPipelineAndReadbackFailures()
    {
        using namespace uvsr;
        Require(RendererBlitDispatchContract{ true, true, true }.CanDispatch(),
            "complete blit state was rejected");
        Require(!RendererBlitDispatchContract{ false, true, true }.CanDispatch(),
            "uninitialized common resources dispatched");
        Require(!RendererBlitDispatchContract{ true, false, true }.CanDispatch(),
            "failed graphics pipeline dispatched");
        Require(!RendererBlitDispatchContract{ true, true, false }.CanDispatch(),
            "failed binding-set creation dispatched");

        const RendererPixelReadbackInitializationContract complete = {
            true, true, true, true, true, true, true, true
        };
        Require(complete.IsComplete(), "complete readback state was rejected");
        bool RendererPixelReadbackInitializationContract::* const fields[] = {
            &RendererPixelReadbackInitializationContract::device,
            &RendererPixelReadbackInitializationContract::shader,
            &RendererPixelReadbackInitializationContract::intermediateBuffer,
            &RendererPixelReadbackInitializationContract::readbackBuffer,
            &RendererPixelReadbackInitializationContract::constantBuffer,
            &RendererPixelReadbackInitializationContract::bindingLayout,
            &RendererPixelReadbackInitializationContract::bindingSet,
            &RendererPixelReadbackInitializationContract::pipeline
        };
        for (auto field : fields)
        {
            RendererPixelReadbackInitializationContract failed = complete;
            failed.*field = false;
            Require(!failed.IsComplete(),
                "readback creation failure was published");
        }
    }

    void TestAtomicResourcePublication()
    {
        std::array<int, 3> published = { 10, 20, 30 };
        const std::array<bool, 3> replace = { true, true, false };
        std::size_t calls = 0u;
        Require(!uvsr::TryReplaceRendererResources(
            published,
            replace,
            [&calls](std::size_t index)
            {
                ++calls;
                return index == 1u ? 0 : 100 + int(index);
            }),
            "failed resource transaction was accepted");
        Require(published == std::array<int, 3>{ 10, 20, 30 },
            "failed resource transaction changed published handles");
        Require(calls == 2u,
            "resource transaction continued after a creation failure");

        Require(uvsr::TryReplaceRendererResources(
            published,
            replace,
            [](std::size_t index)
            {
                return 100 + int(index);
            }),
            "successful resource transaction was rejected");
        Require(published == std::array<int, 3>{ 100, 101, 30 },
            "successful resource transaction was not published atomically");
    }

    void TestReadbackPublication()
    {
        static constexpr uvsr::RendererReadbackUint4 Expected = {
            7u, 11u, 13u, 17u
        };
        bool unmapped = false;
        const auto value = uvsr::ReadRendererUint4(
            []() -> const void*
            {
                return &Expected;
            },
            [&unmapped]()
            {
                unmapped = true;
            });
        Require(value && value->x == 7u && value->y == 11u &&
            value->z == 13u && value->w == 17u && unmapped,
            "successful map did not publish and unmap exact values");

        unmapped = false;
        const auto failed = uvsr::ReadRendererUint4(
            []() -> const void*
            {
                return nullptr;
            },
            [&unmapped]()
            {
                unmapped = true;
            });
        Require(!failed && !unmapped,
            "failed map published a zero ID or attempted to unmap");
    }

    void TestPbrVisibilityBindings()
    {
        using namespace uvsr;
        static_assert(PbrVisibilitySlots[0] == 20u &&
            PbrVisibilitySlots[1] == 21u &&
            PbrVisibilitySlots[2] == 22u);
        static_assert(PbrMsaaSampleCounts[0] == 2u &&
            PbrMsaaSampleCounts[1] == 4u &&
            PbrMsaaSampleCounts[2] == 8u &&
            PbrMsaaSampleCounts[3] == 16u);
        static_assert(PbrNeutralVisibilityByte == 0xffu);

        int flashlight = 1;
        int sun = 2;
        int sky = 3;
        int white = 4;
        const auto active = ResolvePbrVisibilityResources(
            PbrVisibilityResources<int*>{ &flashlight, &sun, &sky },
            &white);
        Require(active.flashlight == &flashlight && active.sun == &sun &&
            active.sky == &sky,
            "active PBR visibility resources were replaced");
        const auto neutral = ResolvePbrVisibilityResources(
            PbrVisibilityResources<int*>{ nullptr, nullptr, nullptr },
            &white);
        Require(neutral.flashlight == &white && neutral.sun == &white &&
            neutral.sky == &white,
            "PBR t20/t21/t22 did not fail open to white");

        const auto closest = ResolvePbrClosestVisibilityResources(
            PbrClosestVisibilityResources<int*>{
                &flashlight, nullptr, &sun, nullptr, &sky, nullptr
            },
            &white);
        Require(closest.flashlightRaw == &flashlight &&
            closest.flashlightDenoised == &white &&
            closest.sunRaw == &sun && closest.sunDenoised == &white &&
            closest.skyRaw == &sky && closest.skyDenoised == &white,
            "PBR MSAA closest-surface resources did not fail open to white");
    }

    void TestEnvironmentBindingsAndApplication()
    {
        using namespace uvsr;
        static_assert(PbrDeferredDiffuseEnvironmentSlot == 1u);
        static_assert(PbrDeferredSpecularEnvironmentSlot == 2u);
        static_assert(PbrDeferredEnvironmentBrdfSlot == 3u);
        static_assert(ScreenSpaceCompositeDiffuseEnvironmentSlot == 7u);
        static_assert(ScreenSpaceCompositeSpecularEnvironmentSlot == 10u);
        static_assert(ScreenSpaceCompositeEnvironmentBrdfSlot == 11u);
        static_assert(ScreenSpaceCompositeSkyVisibilitySlot == 12u);

        int diffuse = 1;
        int specular = 2;
        int brdf = 3;
        int sky = 4;
        int blackCube = 5;
        int blackTexture = 6;
        int whiteTexture = 7;

        const auto pbrActive = ResolvePbrDeferredEnvironmentResources(
            PbrDeferredEnvironmentResources<int*>{
                &diffuse, &specular, &brdf },
            &blackCube,
            &blackTexture);
        const auto pbrBindings =
            MakePbrDeferredEnvironmentBindings(pbrActive);
        Require(pbrBindings[0].slot == 1u &&
                pbrBindings[0].resource == &diffuse &&
                pbrBindings[1].slot == 2u &&
                pbrBindings[1].resource == &specular &&
                pbrBindings[2].slot == 3u &&
                pbrBindings[2].resource == &brdf,
            "deferred PBR IBL descriptor meanings were swapped");
        const auto pbrNeutral = MakePbrDeferredEnvironmentBindings(
            ResolvePbrDeferredEnvironmentResources(
                PbrDeferredEnvironmentResources<int*>{},
                &blackCube,
                &blackTexture));
        Require(pbrNeutral[0].resource == &blackCube &&
                pbrNeutral[1].resource == &blackCube &&
                pbrNeutral[2].resource == &blackTexture,
            "deferred PBR IBL did not fail open to black resources");

        const auto compositeActive =
            ResolveScreenSpaceCompositeEnvironmentResources(
                ScreenSpaceCompositeEnvironmentResources<int*>{
                    &diffuse, &specular, &brdf, &sky },
                &blackCube,
                &blackTexture,
                &whiteTexture);
        const auto compositeBindings =
            MakeScreenSpaceCompositeEnvironmentBindings(compositeActive);
        Require(compositeBindings[0].slot == 7u &&
                compositeBindings[0].resource == &diffuse &&
                compositeBindings[1].slot == 10u &&
                compositeBindings[1].resource == &specular &&
                compositeBindings[2].slot == 11u &&
                compositeBindings[2].resource == &brdf &&
                compositeBindings[3].slot == 12u &&
                compositeBindings[3].resource == &sky,
            "separate-composite IBL/sky descriptor meanings were swapped");
        const auto compositeNeutral =
            MakeScreenSpaceCompositeEnvironmentBindings(
                ResolveScreenSpaceCompositeEnvironmentResources(
                    ScreenSpaceCompositeEnvironmentResources<int*>{},
                    &blackCube,
                    &blackTexture,
                    &whiteTexture));
        Require(compositeNeutral[0].resource == &blackCube &&
                compositeNeutral[1].resource == &blackCube &&
                compositeNeutral[2].resource == &blackTexture &&
                compositeNeutral[3].resource == &whiteTexture,
            "separate-composite IBL/sky fail-open resources changed");

        for (unsigned available = 0u; available < 2u; ++available)
        {
            for (unsigned diffuseEnabled = 0u;
                diffuseEnabled < 2u;
                ++diffuseEnabled)
            {
                for (unsigned specularEnabled = 0u;
                    specularEnabled < 2u;
                    ++specularEnabled)
                {
                    const std::uint32_t expected = available
                        ? diffuseEnabled + 2u * specularEnabled
                        : UVSR_SKY_VISIBILITY_APPLY_NEITHER;
                    const std::uint32_t application =
                        ResolveSkyVisibilityApplication(
                            available != 0u,
                            diffuseEnabled != 0u,
                            specularEnabled != 0u);
                    Require(application == expected &&
                            SkyVisibilityAppliesToDiffuseIbl(application) ==
                                (available != 0u && diffuseEnabled != 0u) &&
                            SkyVisibilityAppliesToSpecularIbl(application) ==
                                (available != 0u && specularEnabled != 0u),
                        "sky visibility application path changed");
                }
            }
        }
    }

    void TestSkyBindingIdentity()
    {
        using namespace uvsr;
        static_assert(SkyVisibilityConstantBufferSlot == 0u);
        static_assert(SkyVisibilityWorldTlasSlot == 0u);
        static_assert(SkyVisibilityGBufferAndNoiseSlots[0] == 1u &&
            SkyVisibilityGBufferAndNoiseSlots[1] == 2u &&
            SkyVisibilityGBufferAndNoiseSlots[2] == 3u &&
            SkyVisibilityGBufferAndNoiseSlots[3] == 4u &&
            SkyVisibilityGBufferAndNoiseSlots[4] == 5u);
        static_assert(RayMaterialGeometrySlot == 10u);
        static_assert(RayMaterialConstantsSlot == 11u);
        static_assert(RayMaterialGeometryIndexSlot == 12u);
        static_assert(RayMaterialSamplerSlot == 0u);
        static_assert(RayMaterialBufferRegisterSpace == 1u);
        static_assert(RayMaterialTextureRegisterSpace == 2u);
        static_assert(SkyVisibilityOutputSlot == 0u);
        static_assert(SkyVisibilityClosestOutputSlot == 1u);
        static_assert(SkyVisibilityHitDistanceOutputSlot == 2u);

        int objects[11]{};
        const RayTracedSkyVisibilityBindingIdentity reference = {
            &objects[0], &objects[1], &objects[2], &objects[3],
            &objects[4], &objects[5], &objects[6], &objects[7],
            &objects[8], &objects[9]
        };
        Require(reference == reference, "identical sky binding key missed cache");
        const void* RayTracedSkyVisibilityBindingIdentity::* const fields[] = {
            &RayTracedSkyVisibilityBindingIdentity::worldTlas,
            &RayTracedSkyVisibilityBindingIdentity::depth,
            &RayTracedSkyVisibilityBindingIdentity::material,
            &RayTracedSkyVisibilityBindingIdentity::normals,
            &RayTracedSkyVisibilityBindingIdentity::geometryBuffer,
            &RayTracedSkyVisibilityBindingIdentity::materialBuffer,
            &RayTracedSkyVisibilityBindingIdentity::geometryIndexMap,
            &RayTracedSkyVisibilityBindingIdentity::descriptorTable,
            &RayTracedSkyVisibilityBindingIdentity::noiseTexture,
            &RayTracedSkyVisibilityBindingIdentity::attemptMask
        };
        for (auto field : fields)
        {
            RayTracedSkyVisibilityBindingIdentity changed = reference;
            changed.*field = &objects[10];
            Require(changed != reference,
                "changed sky resource reused a stale binding set");
        }
    }
}

int main()
{
    TestCommonInitializationFailures();
    TestInjectedCreationFailures();
    TestPipelineAndReadbackFailures();
    TestAtomicResourcePublication();
    TestReadbackPublication();
    TestPbrVisibilityBindings();
    TestEnvironmentBindingsAndApplication();
    TestSkyBindingIdentity();
    return 0;
}
