#include "renderer_scene_draw.h"
#include "renderer_scene_draw_fixture.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene draw tests require exception-disabled compilation
#endif

namespace
{
    using namespace uvsr;

    void Require(bool value, const char* message)
    {
        if (value) return;
        fprintf(stderr, "scene draw check failed: %s\n", message);
        exit(1);
    }

    RendererSceneFrustum FromReference(const float (&source)[6][4])
    {
        RendererSceneFrustum result;
        for (uint32_t index = 0; index < 6; ++index)
        {
            const auto& plane = source[index];
            result.planes[index] = {{plane[0], plane[1], plane[2]}, plane[3]};
        }
        return result;
    }

    struct Fixture
    {
        RendererSceneNode nodes[4]{};
        RendererSceneInstance instances[3]{};
        RendererSceneMesh meshes[2]{};
        RendererSceneGeometry geometries[3]{};
        RendererSceneMaterial materials[3]{};
        uint32_t preorder[4]{0, 1, 2, 3};

        Fixture()
        {
            nodes[0].worldBounds = {{-1, -1, -1}, {18, 1, 1}, false};
            nodes[0].subtreeContent = SceneContentOpaque | SceneContentAlphaTested;
            nodes[0].subtreeEnd = 4;
            nodes[0].firstChildIndex = 1;
            for (uint32_t index = 1; index < 4; ++index)
            {
                nodes[index].parentIndex = 0;
                nodes[index].leafKind = RendererSceneLeafKind::Instance;
                nodes[index].leafIndex = index - 1;
                nodes[index].preorderIndex = index;
                nodes[index].subtreeEnd = index + 1;
                nodes[index].nextSiblingIndex = index < 3 ? index + 1 : InvalidSceneIndex;
                nodes[index].subtreeContent = SceneContentOpaque | SceneContentAlphaTested;
                instances[index - 1].nodeIndex = index;
                instances[index - 1].meshIndex = index == 3 ? 1 : 0;
            }
            nodes[1].worldBounds = {{-1, -1, -1}, {8, 1, 1}, false};
            nodes[2].worldBounds = {{9, -1, -1}, {18, 1, 1}, false};
            nodes[2].world.translation[0] = 10;
            nodes[3].worldBounds = {{0, 0, 0}, {1, 1, 1}, false};
            meshes[0].geometries = {0, 2};
            meshes[1].geometries = {2, 1};
            meshes[0].bufferGroupIndex = meshes[1].bufferGroupIndex = 0;
            geometries[0].objectBounds = {{-1, -1, -1}, {1, 1, 1}, false};
            geometries[1].objectBounds = {{7, -1, -1}, {8, 1, 1}, false};
            geometries[2].objectBounds = {{0, 0, 0}, {1, 1, 1}, false};
            for (uint32_t index = 0; index < 3; ++index) geometries[index].materialIndex = index;
            materials[1].values.domain = RendererMaterialDomain::AlphaTested;
            materials[2].values.domain = RendererMaterialDomain::AlphaBlended;
        }
        RendererSceneView View() const
        {
            RendererSceneView result;
            result.nodes = nodes; result.instances = instances; result.meshes = meshes;
            result.geometries = geometries; result.materials = materials; result.preorder = preorder;
            result.root = 0; result.generation = 17;
            return result;
        }
    };

    void CheckCullingAndFailure()
    {
        Fixture fixture;
        RendererSceneDrawList draws;
        Require(draws.Prepare(fixture.View()).Succeeded() && draws.Capacity() == 5, "exact geometry-instance capacity");
        const auto infinite = FromReference(test_reference::InfiniteFrustum);
        Require(draws.Build(fixture.View(), infinite).Succeeded() && draws.View().count == 4, "opaque and alpha-tested filtering");
        auto output = draws.View();
        Require(output.data[0].material == 0 && output.data[0].instance == 0 &&
            output.data[1].instance == 1 && output.data[2].material == 1 && output.data[3].instance == 1,
            "stable material/buffer/mesh/instance/geometry order");
        const auto narrow = FromReference(test_reference::NarrowFrustum);
        SetRendererSceneDrawAllocationFailure(true);
        Require(draws.Build(fixture.View(), narrow).Succeeded() && draws.View().count == 1 &&
            draws.View().data[0].geometry == 0, "view build allocates nothing and culls moved instance plus outside geometry");
        Require(draws.Prepare(fixture.View()).error == RendererSceneError::Allocation && draws.View().count == 1,
            "failed replacement retains prepared draw state");
        SetRendererSceneDrawAllocationFailure(false);
        fixture.meshes[0].skinPrototypeIndex = 1;
        Require(draws.Build(fixture.View(), narrow).Succeeded() && draws.View().count == 2,
            "skinned instance retains whole-mesh culling");
        fixture.nodes[0].subtreeContent = SceneContentNone;
        Require(draws.Build(fixture.View(), infinite).Succeeded() && draws.View().count == 0, "irrelevant subtree pruned");
        fixture.nodes[0].subtreeContent = SceneContentOpaque;
        fixture.nodes[0].worldBounds = {};
        Require(draws.Build(fixture.View(), infinite).Succeeded() && draws.View().count == 0, "empty bounds rejected");
        auto wrongGeneration = fixture.View(); ++wrongGeneration.generation;
        Require(draws.Build(wrongGeneration, infinite).error == RendererSceneError::Generation && draws.View().count == 0,
            "stale generation yields no draws");
        auto invalidFrustum = infinite; invalidFrustum.planes[0].distance = NAN;
        Require(draws.Build(fixture.View(), invalidFrustum).error == RendererSceneError::Value, "invalid frustum rejected");
        draws.Reset();
        Require(draws.StorageBytes() == 0 && draws.Capacity() == 0 && draws.View().count == 0, "scene retirement releases storage");
    }

    void CheckInstanceOrder()
    {
        Fixture fixture;
        const auto first = fixture.instances[0];
        fixture.instances[0] = fixture.instances[1];
        fixture.instances[1] = first;
        fixture.nodes[1].leafIndex = 1;
        fixture.nodes[2].leafIndex = 0;
        RendererSceneDrawList draws;
        Require(draws.Prepare(fixture.View()).Succeeded() &&
            draws.Build(fixture.View(), FromReference(test_reference::InfiniteFrustum)).Succeeded(), "import-order fixture");
        const auto output = draws.View();
        Require(output.count == 4 && output.data[0].instance == 1 && output.data[1].instance == 0 &&
            output.data[2].instance == 1 && output.data[3].instance == 0,
            "raster ties preserve hierarchy order when imported instance IDs differ");
    }

    void CheckChunkBoundaries()
    {
        Fixture fixture;
        RendererSceneGeometry geometries[129]{};
        for (uint32_t index = 0; index < 129; ++index)
        {
            geometries[index].materialIndex = index & 1;
            geometries[index].objectBounds = {{-1, -1, -1}, {1, 1, 1}, false};
        }
        fixture.meshes[0].geometries = {0, 129};
        auto view = fixture.View();
        view.geometries = geometries;
        view.instances.count = 2;
        view.nodes.count = view.preorder.count = 3;
        fixture.nodes[0].subtreeEnd = 3;
        fixture.nodes[2].nextSiblingIndex = InvalidSceneIndex;
        RendererSceneDrawList draws;
        Require(draws.Prepare(view).Succeeded() && draws.Capacity() == 258, "large mesh capacity includes every instance");
        Require(draws.Build(view, FromReference(test_reference::InfiniteFrustum)).Succeeded() && draws.View().count == 258 &&
            draws.ChunkCount() == 2 && draws.MaximumChunk() == 129, "whole mesh is never truncated at 128 draws");
        const auto rows = draws.View();
        Require(rows.data[128].instance == 0 && rows.data[129].instance == 1, "sorting preserves whole-mesh chunk boundaries");
        for (size_t chunk = 0; chunk < 2; ++chunk)
            for (size_t index = chunk * 129 + 1; index < (chunk + 1) * 129; ++index)
            {
                const auto& a = rows.data[index - 1]; const auto& b = rows.data[index];
                Require(a.material < b.material || (a.material == b.material && a.geometry < b.geometry),
                    "numeric sort is strict within each chunk");
            }
    }

    void CheckReferenceFrustum()
    {
        uint32_t random = 0x17c984b1;
        const auto sample = [&]() { random = random * 1664525u + 1013904223u; return float(int(random >> 16) - 32768) / 512.f; };
        const auto converted = FromReference(test_reference::PerspectiveFrustum);
        for (uint32_t index = 0; index < 10000; ++index)
        {
            // preserve the captured MSVC control's right-to-left vector arguments.
            const float z = sample();
            const float y = sample();
            const float x = sample();
            const float maximumZ = z + fabsf(sample());
            const float maximumY = y + fabsf(sample());
            const float maximumX = x + fabsf(sample());
            const RendererSceneBounds bounds{{x, y, z}, {maximumX, maximumY, maximumZ}, false};
            const bool expected = (test_reference::DrawClassifications[index / 8] & (1u << (index % 8))) != 0;
            Require(expected == IntersectsRendererSceneBounds(converted, bounds),
                "captured and canonical frustum classifications agree");
        }
    }
}

int main()
{
    CheckCullingAndFailure();
    CheckInstanceOrder();
    CheckChunkBoundaries();
    CheckReferenceFrustum();
    puts("scene draws passed: canonical culling/filtering/order, whole-mesh chunks, allocation/generation failure, 10000 captured frustum comparisons");
    return 0;
}
