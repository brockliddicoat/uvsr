#include "renderer_scene.h"
#include "renderer_scene_encoding.h"
#include "renderer_scene_math_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;

    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "native scene oracle failed: %s\n", reason);
        exit(1);
    }

    void Same(const RendererSceneAffine& value, const double (&expected)[12])
    {
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                Require(value.linear[row * 3 + column] == expected[row * 3 + column], "affine matrix lane");
        for (int column = 0; column < 3; ++column)
            Require(value.translation[column] == expected[9 + column], "affine translation lane");
    }
}

void CheckRendererSceneNativeMath()
{
    static_assert(sizeof(test_reference::SceneMathCases) / sizeof(test_reference::SceneMathCases[0]) == 96);
    for (uint32_t seed = 1; seed <= 96; ++seed)
    {
        const auto& reference = test_reference::SceneMathCases[seed - 1];
        RendererScene scene;
        RendererSceneCounts counts;
        counts.nodes = 3;
        counts.meshes = counts.geometries = counts.instances = counts.materials = counts.bufferGroups = 1;
        Require(scene.Prepare(counts).Succeeded(), "allocation");
        struct Input
        {
            RendererSceneNode nodes[3];
            RendererSceneMesh meshes[1];
            RendererSceneGeometry geometries[1];
            RendererSceneInstance instances[1];
            RendererSceneBufferGroup bufferGroups[1];
        } build;
        build.nodes[0].firstChildIndex = 1;
        build.nodes[1].parentIndex = 0;
        build.nodes[1].firstChildIndex = 2;
        build.nodes[2].parentIndex = 1;
        build.nodes[2].leafKind = RendererSceneLeafKind::Instance;
        build.nodes[2].leafIndex = 0;
        for (uint32_t index = 0; index < 2; ++index)
        {
            auto& node = build.nodes[index];
            node.hasLocalTransform = true;
            const double phase = double(seed * 2 + index);
            // static imported rotations retain raw components, including zero.
            memcpy(node.transform.rotation, reference.rotation[index], sizeof(node.transform.rotation));
            node.transform.translation[0] = phase * -.37;
            node.transform.translation[1] = phase * .053;
            node.transform.translation[2] = phase * .29;
            node.transform.scaling[0] = index ? -1.7 : .35;
            node.transform.scaling[1] = .5 + phase * .002;
            node.transform.scaling[2] = index ? .8 : 2.3;
        }
        build.instances[0].nodeIndex = 2;
        build.instances[0].meshIndex = 0;
        build.meshes[0].bufferGroupIndex = 0;
        build.meshes[0].geometries = {0, 1};
        build.meshes[0].vertexCount = build.meshes[0].indexCount = 3;
        auto& geometry = build.geometries[0];
        geometry.materialIndex = 0;
        geometry.vertexCount = geometry.indexCount = 3;
        geometry.objectBounds = {{-1.7f, -.5f, -2.3f}, {.7f, 3.1f, .02f}, false};
        build.bufferGroups[0].indexBytes = 12;
        build.bufferGroups[0].vertexBytes = 36;
        build.bufferGroups[0].attributes[0] = {0, 36};
        const auto& parent = reference.parent;
        const auto& child = reference.child;
        const auto& world = reference.world;
        const auto& bounds = reference.bounds;
        for (uint32_t i = 0; i < 3; ++i)
            Require(scene.Write(i, build.nodes[i]).Succeeded(), "native input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.meshes[i]).Succeeded(), "native input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.geometries[i]).Succeeded(), "native input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.instances[i]).Succeeded(), "native input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.bufferGroups[i]).Succeeded(), "native input copy");
        uint8_t workspace[3]{};
        Require(scene.Seal(0, {workspace, 3}).Succeeded() && scene.Publish(seed).Succeeded(), "seal/publication");
        const auto view = scene.View();
        InstanceData encoded;
        const auto& expected = reference.current;
        const float initialPrevious[]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        Require(EncodeRendererSceneInstance(view, 0, 0, encoded).Succeeded() &&
            encoded.flags == 0 && encoded.firstGeometryIndex == 0 && encoded.firstGeometryInstanceIndex == 0 &&
            encoded.numGeometries == 1 && memcmp(&encoded.transform, expected, sizeof(expected)) == 0 &&
            memcmp(&encoded.prevTransform, initialPrevious, sizeof(initialPrevious)) == 0,
            "current GPU bytes match native math while supplied previous snapshots retain their own identity");
        Same(view.nodes.data[0].local, parent);
        Same(view.nodes.data[1].local, child);
        Same(view.nodes.data[1].world, world);
        Same(view.nodes.data[2].world, world);
        const auto actualBounds = view.nodes.data[0].worldBounds;
        Require(!actualBounds.empty && actualBounds.minimum.x == bounds[0] && actualBounds.minimum.y == bounds[1] &&
            actualBounds.minimum.z == bounds[2] && actualBounds.maximum.x == bounds[3] &&
            actualBounds.maximum.y == bounds[4] && actualBounds.maximum.z == bounds[5], "transformed bounds");
        auto next = build.nodes[1].transform;
        Require(scene.AdvancePreviousTransforms().changed, "initial submitted frame establishes its previous world");
        next.translation[0] += 17;
        Require(scene.SetTransform({seed, 1}, next).changed && scene.View().instanceTransformRevision == 2 &&
            scene.View().previousInstanceTransformRevision == 2, "inherited motion advances only the current instance revision");
        next.translation[2] -= 5;
        Require(scene.SetTransform({seed, 1}, next).changed, "second edit before submission");
        const auto& moved = reference.moved;
        Require(EncodeRendererSceneInstance(scene.View(), 0, 0, encoded).Succeeded() &&
            memcmp(&encoded.transform, moved, sizeof(moved)) == 0 &&
            memcmp(&encoded.prevTransform, expected, sizeof(expected)) == 0,
            "multiple edits retain the last submitted previous world");
        Require(scene.AdvancePreviousTransforms().changed && scene.View().previousInstanceTransformRevision == 3 &&
            EncodeRendererSceneInstance(scene.View(), 0, 0, encoded).Succeeded() &&
            memcmp(&encoded.prevTransform, moved, sizeof(moved)) == 0 && !scene.AdvancePreviousTransforms().changed,
            "successful frame snapshot settles previous motion exactly once");
    }
    puts("native scene math passed: 96 noncommuting hierarchies, exact current/previous GPU bytes, signed scale, inherited motion and bounds");
}
