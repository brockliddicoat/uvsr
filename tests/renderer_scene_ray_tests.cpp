#include "renderer_scene_ray.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene ray tests require exception-disabled compilation
#endif

namespace
{
    using namespace uvsr;

    void Require(bool value, const char* message)
    {
        if (value) return;
        fprintf(stderr, "scene ray check failed: %s\n", message);
        exit(1);
    }

    struct Fixture
    {
        RendererSceneNode nodes[4]{};
        RendererSceneMesh meshes[4]{};
        RendererSceneGeometry geometries[10]{};
        RendererSceneInstance instances[4]{};
        RendererSceneMaterial materials[8]{};
        RendererSceneBufferGroup buffers[1]{};
        uint64_t materialRevision = 1;

        Fixture()
        {
            buffers[0].indexBytes = 120;
            buffers[0].vertexBytes = 360;
            buffers[0].attributes[0] = {0, 360};
            for (auto& mesh : meshes) mesh.bufferGroupIndex = 0;
            meshes[0].geometries = {0, 7};
            meshes[1].geometries = {7, 1};
            meshes[1].type = RendererSceneMeshType::CurvePolytubes;
            meshes[2].geometries = {8, 1};
            meshes[3].geometries = {9, 1};
            for (uint32_t index = 0; index < 4; ++index)
                instances[index] = {index, index > 1 ? index - 1 : 0, {}};
            for (auto& geometry : geometries)
            {
                geometry.materialIndex = 0;
                geometry.indexCount = geometry.vertexCount = 3;
            }
            for (uint32_t index = 0; index < 6; ++index)
            {
                materials[index].values.domain = RendererMaterialDomain(index);
                geometries[index].materialIndex = index;
            }
            geometries[6].indexCount = 4;
            geometries[8].primitive = RendererScenePrimitive::Lines;
            geometries[9].materialIndex = 7;
        }

        RendererSceneView View() const
        {
            RendererSceneView scene;
            scene.nodes = nodes; scene.meshes = meshes; scene.geometries = geometries;
            scene.instances = instances; scene.materials = materials; scene.bufferGroups = buffers;
            scene.generation = 17; scene.materialRevision = materialRevision;
            return scene;
        }
    };

    void CheckSelectionAndEdits()
    {
        Fixture fixture;
        RendererSceneRaySelection selection;
        Require(selection.Prepare(fixture.View()).Succeeded(), "valid selection");
        auto view = selection.View();
        Require(view.meshes.count == 1 && view.geometries.count == 2 && view.instances.count == 2,
            "shared triangle mesh selected once; curves, lines, invalid triangle counts and blended domains omitted");
        Require(view.meshes.data[0].sceneMesh == 0 && view.meshes.data[0].geometries.first == 0 &&
            view.meshes.data[0].geometries.count == 2 && view.geometries.data[0].sceneGeometry == 0 &&
            view.geometries.data[0].opaque && view.geometries.data[1].sceneGeometry == 1 && !view.geometries.data[1].opaque,
            "opaque and alpha-tested flags preserve geometry identity");
        Require(view.instances.data[0].sceneInstance == 0 && view.instances.data[1].sceneInstance == 1 &&
            view.instances.data[0].selectedMesh == 0 && view.instances.data[1].selectedMesh == 0,
            "stable canonical instance membership");

        SetRendererSceneRayAllocationFailure(1);
        fixture.materials[0].values.roughness = 0.9f;
        ++fixture.materialRevision;
        Require(selection.Matches(fixture.View()) && selection.View().meshes.data == view.meshes.data,
            "roughness edit retains acceleration selection without allocating");
        fixture.materials[7].values.domain = RendererMaterialDomain::AlphaBlended;
        ++fixture.materialRevision;
        Require(selection.Matches(fixture.View()), "unused material domain does not invalidate selected meshes");
        fixture.nodes[0].world.translation[0] = 3;
        auto transformed = fixture.View(); ++transformed.transformRevision;
        Require(selection.Matches(transformed), "transforms retain membership and are consumed by instance upload");
        fixture.materials[0].values.domain = RendererMaterialDomain::AlphaTested;
        ++fixture.materialRevision;
        Require(!selection.Matches(fixture.View()), "opaque to alpha-tested changes acceleration flags");
        SetRendererSceneRayAllocationFailure(0);
        Require(selection.Prepare(fixture.View()).Succeeded() && !selection.View().geometries.data[0].opaque,
            "new selection uses canonical alpha classification");
        fixture.materials[1].values.domain = RendererMaterialDomain::AlphaBlended;
        ++fixture.materialRevision;
        Require(!selection.Matches(fixture.View()) && selection.Prepare(fixture.View()).Succeeded() &&
            selection.View().geometries.count == 1 && selection.View().instances.count == 2,
            "alpha to blended removes only that caster geometry");
        fixture.materials[0].values.domain = RendererMaterialDomain::AlphaBlended;
        ++fixture.materialRevision;
        Require(selection.Prepare(fixture.View()).Succeeded() && selection.View().meshes.count == 0 &&
            selection.View().instances.count == 0, "empty supported selection is explicit");
        fixture.materials[0].values.domain = RendererMaterialDomain::Opaque;
        ++fixture.materialRevision;
        Require(!selection.Matches(fixture.View()), "previously omitted geometry can become a caster");
        Require(selection.Prepare(fixture.View()).Succeeded() && selection.View().geometries.count == 1,
            "omitted mesh becomes selected after domain edit");
        auto replaced = fixture.View(); ++replaced.generation;
        Require(!selection.Matches(replaced), "new scene generation invalidates selection");
        selection.Reset();
        Require(selection.StorageBytes() == 0 && selection.View().generation == 0 && !selection.Matches(fixture.View()),
            "retirement clears storage and generation");
    }

    void CheckFailures()
    {
        Fixture fixture;
        RendererSceneRaySelection selection;
        Require(selection.Prepare(fixture.View()).Succeeded(), "failure baseline");
        const auto baseline = selection.View();
        const auto reject = [&](RendererSceneView input, RendererSceneError expected)
        {
            Require(ClassifyPathTracingSceneDomain(input) == PathTracingSceneDomainStatus::Unsupported,
                "path domain rejects malformed references and ranges");
            Require(selection.Prepare(input).error == expected && selection.View().meshes.data == baseline.meshes.data &&
                selection.View().generation == 17, "invalid input leaves previous published selection intact");
        };
        auto input = fixture.View(); input.generation = 0; reject(input, RendererSceneError::Generation);
        input = fixture.View(); input.meshes.count = size_t(UINT32_MAX) + 1; reject(input, RendererSceneError::Capacity);
        input = fixture.View(); input.materials.data = nullptr; reject(input, RendererSceneError::Capacity);
        fixture.instances[0].nodeIndex = 4; reject(fixture.View(), RendererSceneError::Reference); fixture.instances[0].nodeIndex = 0;
        fixture.instances[0].meshIndex = 4; reject(fixture.View(), RendererSceneError::Reference); fixture.instances[0].meshIndex = 0;
        fixture.geometries[0].materialIndex = 8; reject(fixture.View(), RendererSceneError::Reference); fixture.geometries[0].materialIndex = 0;
        fixture.meshes[0].bufferGroupIndex = 1; reject(fixture.View(), RendererSceneError::Reference); fixture.meshes[0].bufferGroupIndex = 0;
        fixture.meshes[0].geometries.count = UINT32_MAX; reject(fixture.View(), RendererSceneError::Range); fixture.meshes[0].geometries.count = 7;
        fixture.meshes[1].geometries.first = 0; reject(fixture.View(), RendererSceneError::Range); fixture.meshes[1].geometries.first = 7;

        uint32_t failures = 0;
        input = fixture.View(); input.generation = 18;
        for (uint32_t ordinal = 1; ordinal < 32; ++ordinal)
        {
            SetRendererSceneRayAllocationFailure(ordinal);
            const auto result = selection.Prepare(input);
            SetRendererSceneRayAllocationFailure(0);
            if (result.Succeeded()) break;
            Require(result.error == RendererSceneError::Allocation && selection.View().meshes.data == baseline.meshes.data &&
                selection.View().generation == 17 && selection.Matches(fixture.View()),
                "each allocation failure preserves the complete previous generation");
            ++failures;
        }
        Require(failures > 0 && failures < 31 && selection.View().generation == 18, "failure sweep reaches successful replacement");
        printf("scene ray selection: %u allocation boundaries, malformed references/ranges and atomic replacement passed\n", failures);
    }

    void CheckPathDomain()
    {
        Fixture fixture;
        for (auto& mesh : fixture.meshes) mesh.type = RendererSceneMeshType::Triangles;
        for (auto& geometry : fixture.geometries)
        {
            geometry.primitive = RendererScenePrimitive::Triangles;
            geometry.indexCount = geometry.vertexCount = 3;
            geometry.materialIndex = 0;
        }
        Require(ClassifyPathTracingSceneDomain(fixture.View()) == PathTracingSceneDomainStatus::Supported,
            "complete triangle domain");
        for (uint32_t domain = 0; domain < 6; ++domain)
            for (uint32_t unsupported = 0; unsupported < 4; ++unsupported)
            {
                auto& material = fixture.materials[0].values;
                material.domain = RendererMaterialDomain(domain);
                material.transmissionFactor = unsupported == 1 ? .1f : 0;
                material.enableSubsurfaceScattering = unsupported == 2;
                material.enableHair = unsupported == 3;
                const auto expected = unsupported || domain > uint32_t(RendererMaterialDomain::AlphaBlended)
                    ? PathTracingSceneDomainStatus::Unsupported
                    : domain == uint32_t(RendererMaterialDomain::AlphaBlended)
                        ? PathTracingSceneDomainStatus::BlendedGeometryOmitted : PathTracingSceneDomainStatus::Supported;
                Require(ClassifyPathTracingSceneDomain(fixture.View()) == expected, "all retained material domains and transport exclusions");
            }
        fixture.materials[0].values = {};
        fixture.geometries[0].indexCount = 4;
        Require(ClassifyPathTracingSceneDomain(fixture.View()) == PathTracingSceneDomainStatus::Unsupported, "invalid triangle arity");
        fixture.geometries[0].indexCount = 3;
        fixture.geometries[0].primitive = RendererScenePrimitive::Lines;
        Require(ClassifyPathTracingSceneDomain(fixture.View()) == PathTracingSceneDomainStatus::Unsupported, "line geometry");
        fixture.geometries[0].primitive = RendererScenePrimitive::Triangles;
        fixture.buffers[0].attributes[0].size = 0;
        Require(ClassifyPathTracingSceneDomain(fixture.View()) == PathTracingSceneDomainStatus::Unsupported, "missing position stream");
        fixture.buffers[0].attributes[0].size = 360;
        fixture.meshes[3].type = RendererSceneMeshType::CurvePolytubes;
        Require(ClassifyPathTracingSceneDomain(fixture.View()) == PathTracingSceneDomainStatus::Unsupported, "unused curve mesh retains domain exclusion");
        puts("path domain: 24 material/transport cases, malformed streams/primitives and unused meshes passed");
    }

    void CheckWideInstances()
    {
        constexpr uint32_t Count = 100000;
        auto* nodes = new (std::nothrow) RendererSceneNode[Count]{};
        auto* instances = new (std::nothrow) RendererSceneInstance[Count]{};
        Require(nodes && instances, "wide fixture allocation");
        Fixture fixture;
        for (uint32_t index = 0; index < Count; ++index) instances[index] = {index, 0, {}};
        auto scene = fixture.View(); scene.nodes = {nodes, Count}; scene.instances = {instances, Count};
        RendererSceneRaySelection selection;
        Require(selection.Prepare(scene).Succeeded() && selection.View().instances.count == Count &&
            selection.View().meshes.count == 1 && selection.View().geometries.count == 2,
            "wide instance list deduplicates shared geometry without recursive traversal");
        Require(selection.StorageBytes() < size_t(Count) * 9 + 1024,
            "selection capacity follows input records, not repeated mesh geometry per instance");
        delete[] instances; delete[] nodes;
    }
}

int main()
{
    CheckSelectionAndEdits();
    CheckFailures();
    CheckPathDomain();
    CheckWideInstances();
    puts("scene ray selection: domains, shared meshes, edits, generations and 100000 instances passed");
    return 0;
}
