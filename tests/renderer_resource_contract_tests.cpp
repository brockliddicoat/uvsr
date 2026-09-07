#include "image_based_lighting_background_pass.h"
#include "image_based_lighting_environment.h"
#include "pbr_deferred_dispatch_contract.h"
#include "pbr_deferred_lighting_bindings.h"
#include "ray_scene_view.h"
#include "lighting_surface.h"
#include "renderer_environment_bindings.h"
#include "renderer_geometry_passes.h"
#include "renderer_pixel_readback_cb.h"
#include "renderer_producer_contract.h"
#include "renderer_resource_contract.h"
#include "renderer_targets.h"
#include "renderer_texture_bmp.h"
#include "sky_visibility_application.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

void CheckReceivers();

namespace
{
    using namespace uvsr;

    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void CheckResourcePublication()
    {
        for (size_t failure = 0; failure <= 4; ++failure)
        {
            RendererResourceCreationSequence sequence;
            size_t calls = 0;
            for (size_t stage = 0; stage < 4; ++stage)
                (void)sequence.Require([&] { ++calls; return stage != failure; });
            Require(sequence.IsValid() == (failure == 4) && calls == std::min(failure + 1, size_t{4}),
                "dependent resource creation continued after failure");
        }
        constexpr RendererReadbackUint4 expected{ 7, 11, 13, 17 };
        for (const bool mapped : { false, true })
        {
            unsigned unmaps = 0;
            const auto value = ReadRendererUint4(
                [&]() -> const void* { return mapped ? &expected : nullptr; }, [&] { ++unmaps; });
            Require(bool(value) == mapped && unmaps == unsigned(mapped),
                "failed readback published a value or unmapped an absent mapping");
            if (value)
                Require(value->x == 7 && value->y == 11 && value->z == 13 && value->w == 17,
                    "readback lost the picked identity");
        }

        RenderTargets targets;
        Require(!targets.IsValid() &&
            !targets.Init(nullptr, { 1280, 720 }, { 1280, 720 }, true, true) &&
            !targets.IsValid() &&
            targets.IsUpdateRequired({ 1280, 720 }, { 1280, 720 }, true),
            "failed target initialization became valid or suppressed retry");
        Require(!targets.EnsureMaterialPickingTargets(nullptr), "null device published picking targets");
        targets.Clear(nullptr);
    }

    void CheckDispatchTransactions()
    {
        for (int failure = -1; failure < 3; ++failure)
        {
            unsigned writes = 0;
            PbrDeferredLightingRenderTransaction transaction(3);
            for (int view = 0; view < 3; ++view)
                if (!ExecutePbrDeferredLightingView(transaction, view != failure, [&] { ++writes; }))
                    break;
            const auto result = transaction.Finish();
            Require(result.Succeeded() == (failure < 0) && result.dispatchedViewCount == writes &&
                writes == (failure < 0 ? 3u : unsigned(failure)), "PBR published incomplete output as success");

            writes = 0;
            const auto background = ExecuteImageBasedLightingBackgroundViews(true, 3, [&](uint32_t view) {
                if (int(view) == failure) return false;
                ++writes;
                return true;
            });
            Require(background.Succeeded() == (failure < 0) && background.dispatchedViewCount == writes &&
                writes == (failure < 0 ? 3u : unsigned(failure)), "IBL background wrote past a failed view");
        }
        Require(!PbrDeferredLightingRenderTransaction(0).Finish().Succeeded(), "empty PBR work became success");
        unsigned writes = 0;
        Require(!ExecuteImageBasedLightingBackgroundViews(false, 1,
            [&](uint32_t) { ++writes; return true; }).Succeeded() && writes == 0,
            "invalid IBL background touched output");

        const RendererProducerDispatchContract incomplete[] = {
            { true, false }, { false, false, true, false },
            { false, false, false, false, true, false }
        };
        Require(RendererProducerDispatchContract{}.IsComplete(), "inactive producers became failed work");
        for (const auto& state : incomplete)
            Require(!state.IsComplete(), "selected producer failure collapsed to inactive");

        ImageBasedLightingPreparationState invalid;
        invalid.Complete();
        Require(invalid.HasFailed(), "IBL completion without a request succeeded");
        ImageBasedLightingPreparationState preparation;
        Require(!preparation.IsReady() && !preparation.HasFailed(), "idle IBL preparation has a terminal state");
        preparation.Begin();
        Require(preparation.Get() == ImageBasedLightingPreparationStatus::Preparing, "IBL upload did not begin");
        preparation.Fail();
        Require(preparation.HasFailed() && !preparation.IsReady(), "failed IBL upload became ready");
        preparation.Begin();
        preparation.Complete();
        Require(preparation.IsReady() && !preparation.HasFailed(), "successful IBL retry retained failure");
    }

    void CheckGeometryAndProbes()
    {
        using Domain = RendererMaterialDomain;
        Require(ClassifyRendererMaterialDomain(Domain::Opaque) == RendererMaterialRasterClass::Opaque &&
            ClassifyRendererMaterialDomain(Domain::AlphaTested) == RendererMaterialRasterClass::AlphaTested &&
            ClassifyRendererMaterialDomain(Domain::TransmissiveAlphaTested) == RendererMaterialRasterClass::Opaque &&
            ClassifyRendererMaterialDomain(Domain::Count) == RendererMaterialRasterClass::Rejected,
            "geometry material classification changed");
        nvrhi::DrawArguments pending;
        pending.vertexCount = 12; pending.instanceCount = 2;
        pending.startIndexLocation = 9; pending.startVertexLocation = 20; pending.startInstanceLocation = 4;
        RendererGeometryDraw next;
        next.indexCount = 12; next.startIndexLocation = 9; next.startVertexLocation = 20; next.startInstanceLocation = 6;
        Require(CanMergeRendererGeometryDraws(pending, next), "contiguous instances did not batch");
        for (uint32_t RendererGeometryDraw::* field : {
                &RendererGeometryDraw::indexCount, &RendererGeometryDraw::startIndexLocation,
                &RendererGeometryDraw::startVertexLocation, &RendererGeometryDraw::startInstanceLocation })
        {
            auto changed = next;
            ++(changed.*field);
            Require(!CanMergeRendererGeometryDraws(pending, changed), "unrelated geometry draws were merged");
        }
        next.instanceCount = UINT32_MAX;
        Require(!CanMergeRendererGeometryDraws(pending, next), "geometry instance count overflow was accepted");

        Require(IsImageBasedLightingProbeActive(true, false, false, 1, 0) &&
            IsImageBasedLightingProbeActive(false, true, true, 0, 1) &&
            !IsImageBasedLightingProbeActive(false, true, false, 0, 1) &&
            !IsImageBasedLightingProbeActive(true, true, true, std::numeric_limits<float>::quiet_NaN(), 0),
            "IBL lobe activation ignored its required resources or finite scale");
        const auto constants = MakeImageBasedLightingProbeConstants(2, 3, 4, 5, 9);
        Require(constants.diffuseArrayIndex == 2 && constants.specularArrayIndex == 3 &&
            constants.diffuseScale == 4 && constants.specularScale == 5 && constants.mipLevels == 9,
            "probe packing changed its texture/scalar identity");
        for (const auto& plane : constants.frustumPlanes)
            Require(plane.x == 0 && plane.y == 0 && plane.z == 0 && plane.w == 1, "global probe became bounded");
        for (uint32_t face = 0; face < 6; ++face)
        {
            const auto direction = RendererEnvironmentCubeDirection(face, 7, 11, 16);
            const auto length = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&direction)));
            Require(std::isfinite(length) && std::abs(length - 1) < 1e-5f, "cubemap direction is not normalized");
        }
    }

    void CheckBindingIdentity()
    {
        static_assert(PbrVisibilitySlots[0] == 20 && PbrVisibilitySlots[1] == 21 && PbrVisibilitySlots[2] == 22);
        int flashlight = 1, sun = 2, sky = 3, white = 4, diffuse = 5, specular = 6, brdf = 7, blackCube = 8, blackTexture = 9;
        const auto active = ResolvePbrVisibilityResources(PbrVisibilityResources<int*>{ &flashlight, &sun, &sky }, &white);
        const auto neutral = ResolvePbrVisibilityResources(PbrVisibilityResources<int*>{}, &white);
        Require(active.flashlight == &flashlight && active.sun == &sun && active.sky == &sky &&
            neutral.flashlight == &white && neutral.sun == &white && neutral.sky == &white,
            "PBR visibility replaced active resources or lost neutral white");
        for (const bool available : { false, true })
        {
            const auto pbr = MakePbrDeferredEnvironmentBindings(ResolvePbrDeferredEnvironmentResources(
                available ? PbrDeferredEnvironmentResources<int*>{ &diffuse, &specular, &brdf }
                          : PbrDeferredEnvironmentResources<int*>{}, &blackCube, &blackTexture));
            const std::array<int*, 3> pbrExpected = available ? std::array{ &diffuse, &specular, &brdf }
                : std::array{ &blackCube, &blackCube, &blackTexture };
            for (size_t i = 0; i < pbr.size(); ++i)
                Require(pbr[i].slot == i + 1 && pbr[i].resource == pbrExpected[i], "deferred IBL identity changed");
            for (unsigned application = 0; application < 4; ++application)
            {
                const auto actual = ResolveSkyVisibilityApplication(available, bool(application & 1), bool(application & 2));
                Require(actual == (available ? application : 0u) &&
                    SkyVisibilityAppliesToDiffuseIbl(actual) == (available && bool(application & 1)) &&
                    SkyVisibilityAppliesToSpecularIbl(actual) == (available && bool(application & 2)),
                    "sky visibility did not preserve independent diffuse/specular controls");
            }
        }
        int object = 0;
        auto* buffer = reinterpret_cast<nvrhi::IBuffer*>(&object);
        RaySceneView reference{ reinterpret_cast<nvrhi::rt::IAccelStruct*>(&object), buffer, buffer, buffer,
            reinterpret_cast<nvrhi::IDescriptorTable*>(&object), 13, 17 };
        Require(reference.HasSameBindings(reference), "unchanged ray scene missed its binding cache");
        for (unsigned field = 0; field < 6; ++field)
        {
            auto changed = reference;
            switch (field)
            {
            case 0: changed.tlas = nullptr; break;
            case 1: changed.geometryBuffer = nullptr; break;
            case 2: changed.materialBuffer = nullptr; break;
            case 3: changed.geometryIndexMap = nullptr; break;
            case 4: changed.descriptorTable = nullptr; break;
            case 5: ++changed.generation; break;
            }
            Require(!reference.HasSameBindings(changed), "changed ray scene reused stale bindings");
        }
        auto updated = reference;
        ++updated.contentRevision;
        Require(reference.HasSameBindings(updated), "in-place scene content forced a resource rebind");
        LightingSurfaceView surface;
        surface.depth = surface.material = surface.normals = reinterpret_cast<nvrhi::ITexture*>(&object);
        Require(surface.HasSameRayTracingInputs(surface), "unchanged ray surface missed its binding cache");
        for (auto field : { &LightingSurfaceView::depth, &LightingSurfaceView::material, &LightingSurfaceView::normals })
        {
            auto changed = surface;
            changed.*field = nullptr;
            Require(!surface.HasSameRayTracingInputs(changed), "changed receiver reused stale ray bindings");
        }
        auto lightingOnly = surface;
        lightingOnly.diffuse = lightingOnly.emissive = lightingOnly.materialAmbientOcclusion = surface.depth;
        Require(surface.HasSameRayTracingInputs(lightingOnly), "non-ray surface inputs forced a binding rebuild");

    }



    void CheckBmp(const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory);
        const auto output = directory / "known-answer.bmp";
        constexpr std::array<uint8_t, 24> rgba{
            0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0xee, 0xee, 0xee, 0xee,
            0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0xff, 0xdd, 0xdd, 0xdd, 0xdd
        };
        Require(WriteRendererBmp(output, 2, 2, 12, rgba.data()), "known-answer BMP was not written");
        std::ifstream input(output, std::ios::binary);
        const std::vector<uint8_t> bytes{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        constexpr std::array<uint8_t, 16> pixels{
            0xb0, 0xa0, 0x90, 0xc0, 0xf0, 0xe0, 0xd0, 0xff, 0x30, 0x20, 0x10, 0x40, 0x70, 0x60, 0x50, 0x80
        };
        Require(bytes.size() == 70 && bytes[0] == 'B' && bytes[1] == 'M', "BMP header/length changed");
        const auto word = [&](size_t offset) { uint32_t value; std::memcpy(&value, bytes.data() + offset, 4); return value; };
        Require(word(2) == 70 && word(10) == 54 && word(18) == 2 && word(22) == 2 && word(34) == 16 &&
            std::equal(pixels.begin(), pixels.end(), bytes.begin() + 54), "BMP lost source pitch, row order or BGRA bytes");
        const auto invalid = directory / "invalid.bmp";
        std::filesystem::remove(invalid);
        Require(!WriteRendererBmp(invalid, 0, 2, 12, rgba.data()) && !WriteRendererBmp(invalid, 2, 0, 12, rgba.data()) &&
            !WriteRendererBmp(invalid, 2, 2, 7, rgba.data()) && !WriteRendererBmp(invalid, 2, 2, 12, nullptr) &&
            !std::filesystem::exists(invalid), "invalid BMP input published an artifact");
        Require(!WriteRendererBmp(directory, 2, 2, 12, rgba.data()), "unwritable BMP destination reported success");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "usage: uvsr_renderer_resource_contract_tests <scratch-directory>");
        CheckResourcePublication();
        CheckDispatchTransactions();
        CheckGeometryAndProbes();
        CheckBindingIdentity();
        CheckBmp(argv[1]);
        CheckReceivers();
        std::cout << "renderer resource, receiver acceptance passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "renderer acceptance failed: " << error.what() << '\n';
        return 1;
    }
}
