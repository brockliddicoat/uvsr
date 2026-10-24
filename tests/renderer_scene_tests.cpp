#include "renderer_scene.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error renderer scene tests require exception-disabled compilation
#endif

namespace
{
    using namespace uvsr;

    void Require(bool condition, const char* reason)
    {
        if (condition) return;
        fprintf(stderr, "scene owner check failed: %s\n", reason);
        exit(1);
    }

    void CheckSelectionIdentity()
    {
        RendererSceneMaterial materials[3]{};
        materials[0].selectionId = 90000;
        materials[1].selectionId = 7;
        RendererSceneNode nodes[2]{};
        const char text[]{'a', 'b', 'c'};
        RendererSceneView view;
        view.generation = 42;
        view.materials = {materials, 3};
        view.nodes = {nodes, 2};
        view.strings = {text, 3};
        const auto first = FindRendererSceneMaterialSelection(view, 90000);
        const auto second = FindRendererSceneMaterialSelection(view, 7);
        Require(first == RendererSceneHandle{42, 0} && second == RendererSceneHandle{42, 1} &&
            FindRendererSceneMaterial(view, first)->selectionId == 90000,
            "persisted selection numbers do not become canonical indices");
        Require(!FindRendererSceneMaterialSelection(view, 0) && !FindRendererSceneMaterialSelection(view, InvalidSceneIndex),
            "absent and unselectable identities do not select a record");
        materials[2].selectionId = 7;
        Require(!FindRendererSceneMaterialSelection(view, 7), "ambiguous selection identity is rejected");
        Require(!FindRendererSceneMaterial(view, {41, 0}) && !FindRendererSceneMaterial(view, {42, 3}) &&
            !FindRendererSceneNode(view, {41, 0}) && !FindRendererSceneNode(view, {42, 2}) &&
            FindRendererSceneNode(view, {42, 1}) == &nodes[1], "stale and out-of-range handles are rejected");
        const auto span = RendererSceneText(view, {1, 2});
        Require(span.count == 2 && span.data[0] == 'b' && span.data[1] == 'c' &&
            RendererSceneText(view, {2, 2}).count == 0 && RendererSceneText(view, {UINT32_MAX, 1}).count == 0,
            "UI text borrows bounded spans without requiring nul termination");
    }

    struct Fixture
    {
        RendererSceneNode nodes[5]{};
        RendererSceneMesh meshes[1]{};
        RendererSceneGeometry geometries[1]{};
        RendererSceneInstance instances[1]{};
        RendererSceneMaterial materials[2]{};
        RendererSceneTexture textures[1]{};
        RendererSceneBufferGroup bufferGroups[1]{};
        RendererSceneByteRange morphRanges[1]{};
        RendererSceneJoint joints[1]{};
        RendererSceneLight lights[1]{};
        RendererSceneCamera cameras[1]{};
        RendererSceneAnimation animations[1]{};
        RendererSceneAnimationChannel channels[1]{};
        RendererSceneAnimationSampler samplers[1]{};
        RendererSceneKeyframe keyframes[2]{};
        uint32_t materialCount = 1;
    };

    Fixture PrepareFixture(RendererScene& scene, uint32_t materialCount = 1)
    {
        RendererSceneCounts counts;
        counts.nodes = 5;
        counts.meshes = 1;
        counts.geometries = 1;
        counts.instances = 1;
        counts.materials = materialCount;
        counts.bufferGroups = 1;
        counts.lights = 1;
        counts.cameras = 1;
        counts.animations = 1;
        counts.channels = 1;
        counts.samplers = 1;
        counts.keyframes = 2;
        Require(scene.Prepare(counts).Succeeded(), "fixture allocation");
        Fixture build;
        build.materialCount = materialCount;
        build.nodes[0].firstChildIndex = 1;
        for (uint32_t index = 1; index < counts.nodes; ++index)
        {
            build.nodes[index].parentIndex = 0;
            build.nodes[index].nextSiblingIndex = index + 1 < counts.nodes ? index + 1 : InvalidSceneIndex;
            build.nodes[index].leafIndex = 0;
        }
        build.nodes[1].leafKind = RendererSceneLeafKind::Instance;
        build.nodes[2].leafKind = RendererSceneLeafKind::Light;
        build.nodes[3].leafKind = RendererSceneLeafKind::Camera;
        build.nodes[4].leafKind = RendererSceneLeafKind::Animation;
        build.instances[0].nodeIndex = 1;
        build.instances[0].meshIndex = 0;
        build.meshes[0].bufferGroupIndex = 0;
        build.meshes[0].geometries = {0, 1};
        build.meshes[0].vertexCount = 3;
        build.meshes[0].indexCount = 3;
        build.meshes[0].objectBounds = {{-1, -2, -3}, {1, 2, 3}, false};
        build.geometries[0].materialIndex = 0;
        build.geometries[0].vertexCount = 3;
        build.geometries[0].indexCount = 3;
        build.geometries[0].objectBounds = build.meshes[0].objectBounds;
        build.bufferGroups[0].indexBytes = 12;
        build.bufferGroups[0].vertexBytes = 36;
        build.bufferGroups[0].attributes[0] = {0, 36};
        build.lights[0].nodeIndex = 2;
        build.cameras[0].nodeIndex = 3;
        build.animations[0].nodeIndex = 4;
        build.animations[0].channels = {0, 1};
        build.channels[0].nodeIndex = 1;
        build.channels[0].samplerIndex = 0;
        build.channels[0].attribute = RendererSceneAnimationAttribute::Translation;
        build.samplers[0].keyframes = {0, 2};
        build.keyframes[0].time = 0;
        build.keyframes[1].time = 2;
        return build;
    }

    RendererSceneResult SealFixture(RendererScene& scene, const Fixture& build)
    {
        for (uint32_t i = 0; i < 5; ++i)
            Require(scene.Write(i, build.nodes[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.meshes[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.geometries[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.instances[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < build.materialCount; ++i)
            Require(scene.Write(i, build.materials[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.bufferGroups[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.lights[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.cameras[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.animations[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.channels[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < 1; ++i)
            Require(scene.Write(i, build.samplers[i]).Succeeded(), "fixture input copy");
        for (uint32_t i = 0; i < 2; ++i)
            Require(scene.Write(i, build.keyframes[i]).Succeeded(), "fixture input copy");
        uint8_t workspace[5]{};
        return scene.Seal(0, {workspace, 5});
    }

    void CheckInstanceRevisions()
    {
        RendererScene scene;
        const auto build = PrepareFixture(scene);
        Require(SealFixture(scene, build).Succeeded() && scene.Publish(71).Succeeded(), "instance revision fixture");
        const auto initial = scene.View();
        const uint32_t nonInstanceNodes[]{2, 3};
        for (uint32_t node : nonInstanceNodes)
        {
            auto pose = scene.View().nodes.data[node].transform;
            pose.translation[0] = node * 2;
            Require(scene.SetTransform({71, node}, pose).changed &&
                scene.View().instanceTransformRevision == initial.instanceTransformRevision &&
                scene.AdvancePreviousTransforms().changed &&
                scene.View().previousInstanceTransformRevision == initial.previousInstanceTransformRevision,
                "light and camera motion leave both instance revisions unchanged");
        }
        auto pose = scene.View().nodes.data[0].transform;
        pose.translation[1] = 7;
        Require(scene.SetTransform({71, 0}, pose).changed && scene.View().instanceTransformRevision == 2 &&
            scene.View().previousInstanceTransformRevision == 1, "ancestor motion invalidates current instances");
        const auto moving = scene.View();
        Require(scene.SetTransform({70, 0}, pose).error == RendererSceneError::Generation &&
            scene.View().instanceTransformRevision == moving.instanceTransformRevision,
            "stale generation cannot advance instance revisions");
        Require(scene.AdvancePreviousTransforms().changed && scene.View().previousInstanceTransformRevision == 2 &&
            !scene.AdvancePreviousTransforms().changed, "previous instance snapshot advances once");
        Require(!scene.SetTransform({71, 0}, pose).changed && scene.View().instanceTransformRevision == 2,
            "identical transform does not invalidate instances");
    }

    void CheckPreparationCopies()
    {
        RendererScene scene;
        RendererSceneNode node;
        Require(!scene.Write(0, node).Succeeded() && !scene.WriteStrings(0, {}).Succeeded(), "unprepared write");
        RendererSceneCounts counts;
        counts.nodes = 1;
        counts.stringBytes = 4;
        Require(scene.Prepare(counts).Succeeded(), "copy fixture allocation");
        char text[4]{'r', 'o', 'o', 't'};
        node.name = {0, 4};
        Require(scene.Write(0, node).Succeeded() && !scene.Write(1, node).Succeeded(), "copy record index");
        Require(scene.WriteStrings(0, {text, 4}).Succeeded() && !scene.WriteStrings(1, {text, 4}).Succeeded() &&
            !scene.WriteStrings(0, {nullptr, 1}).Succeeded(), "copy string bounds");
        node.name.length = 99;
        text[0] = 'x';
        uint8_t workspace[1]{};
        Require(scene.Seal(0, {workspace, 1}).Succeeded() && !scene.Write(0, node).Succeeded() &&
            !scene.WriteStrings(0, {text, 4}).Succeeded() && scene.Publish(7).Succeeded(), "copy seal boundary");
        Require(scene.View().nodes.data[0].name.length == 4 && scene.View().strings.data[0] == 'r', "source mutation leaked");
    }

    void CheckDeclaredMeshBounds()
    {
        for (uint32_t invalid = 0; invalid < 4; ++invalid)
        {
            RendererScene scene;
            auto build = PrepareFixture(scene);
            build.meshes[0].hasDeclaredBounds = true;
            build.meshes[0].objectBounds = {{-4, -5, -6}, {4, 5, 6}, false};
            if (invalid == 1) build.meshes[0].objectBounds.minimum.x = 0;
            if (invalid == 2) build.meshes[0].objectBounds.maximum.z = NAN;
            if (invalid == 3) build.meshes[0].objectBounds = {};
            const auto result = SealFixture(scene, build);
            if (invalid)
                Require(result.error == RendererSceneError::Value && !scene.IsSealed(), "invalid declared mesh envelope rejected");
            else
            {
                Require(result.Succeeded() && scene.Publish(43).Succeeded(), "conservative declared mesh envelope accepted");
                Require(scene.View().meshes.data[0].objectBounds.minimum.x == -4 &&
                    scene.View().nodes.data[0].worldBounds.maximum.z == 6, "declared mesh envelope preserved through hierarchy bounds");
            }
        }
        RendererScene retry;
        auto build = PrepareFixture(retry);
        build.materials[0].values.roughness = NAN;
        Require(SealFixture(retry, build).error == RendererSceneError::Value, "late failure after deriving mesh bounds");
        build.materials[0].values.roughness = 0.5f;
        build.geometries[0].objectBounds.maximum.x = 8;
        Require(retry.Write(0, build.materials[0]).Succeeded() && retry.Write(0, build.geometries[0]).Succeeded(),
            "unpublished candidate accepts corrections");
        uint8_t workspace[5]{};
        Require(retry.Seal(0, {workspace, 5}).Succeeded() && retry.Publish(44).Succeeded() &&
            retry.View().meshes.data[0].objectBounds.maximum.x == 8, "retry rederives bounds without treating a prior cache as declared");
        puts("scene bounds: conservative declaration, 3 invalid envelopes, late-failure retry passed");
    }

    void CheckPublicationAndMutation()
    {
        RendererScene scene;
        Require(!scene.IsSealed() && !scene.IsPublished() && scene.StorageBytes() == 0, "empty owner");
        auto build = PrepareFixture(scene);
        Require(scene.View().nodes.count == 0 && !scene.Publish(1).Succeeded(), "unsealed visibility");
        build.nodes[1].previousLocal.translation[0] = 7;
        build.nodes[1].previousWorld.translation[0] = 19;
        build.nodes[0].world.linear[0] = NAN;
        build.animations[0].duration = NAN;
        build.meshes[0].objectBounds = {{NAN, NAN, NAN}, {NAN, NAN, NAN}, false};
        Require(SealFixture(scene, build).Succeeded(), "valid fixture seal derives current values");
        Require(!scene.Write(0, RendererSceneNode{}).Succeeded() && scene.SealWorkspaceBytes() == 5, "seal closes mutable borrow");
        Require(scene.View().nodes.count == 0, "sealed unpublished candidate escaped");
        Require(!scene.Publish(0).Succeeded() && scene.Publish(17).Succeeded() && !scene.Publish(18).Succeeded(), "one publication generation");
        auto view = scene.View();
        Require(view.nodes.data[0].world.linear[0] == 1 && view.animations.data[0].duration == 2, "derived fields replaced");
        Require(view.meshes.data[0].objectBounds.minimum.y == -2 && view.meshes.data[0].objectBounds.maximum.z == 3,
            "mesh bounds did not derive from geometry bounds");
        Require(view.nodes.data[1].previousLocal.translation[0] == 7 && view.nodes.data[1].previousWorld.translation[0] == 19,
            "independent previous-world snapshot was recomposed");
        Require(view.nodes.data[0].subtreeContent == (SceneContentOpaque | SceneContentLight | SceneContentCamera | SceneContentAnimation) &&
            view.nodes.data[0].worldBounds.minimum.y == -2 && view.nodes.data[0].subtreeEnd == 5, "derived root data");
        Require(!scene.SetMaterial({16, 0}, view.materials.data[0].values).Succeeded(), "stale generation accepted");
        Require(!scene.SetLight({17, 99}, view.lights.data[0].values).Succeeded(), "bad light index accepted");
        const auto initial = scene.View();
        auto material = initial.materials.data[0].values;
        Require(!scene.SetMaterial({17, 0}, material).changed && scene.View().contentRevision == initial.contentRevision,
            "equal material command invalidated history");
        material.domain = RendererMaterialDomain::AlphaTested;
        const auto materialResult = scene.SetMaterial({17, 0}, material);
        Require(materialResult.Succeeded() && materialResult.changed &&
            scene.View().nodes.data[0].subtreeContent == (SceneContentAlphaTested | SceneContentLight | SceneContentCamera | SceneContentAnimation),
            "material transaction did not update derived content");
        material.opacity = NAN;
        const auto beforeFailure = scene.View();
        Require(!scene.SetMaterial({17, 0}, material).Succeeded() && scene.View().contentRevision == beforeFailure.contentRevision &&
            scene.View().materials.data[0].values.opacity == 1, "invalid material changed live state");
        auto light = scene.View().lights.data[0].values;
        light.intensity = 3;
        Require(scene.SetLight({17, 0}, light).changed && !scene.SetLight({17, 0}, light).changed, "light value transaction");
        auto transform = scene.View().nodes.data[1].transform;
        transform.translation[0] = 5;
        Require(scene.SetTransform({17, 1}, transform).changed && !scene.SetTransform({17, 1}, transform).changed, "transform value transaction");
        view = scene.View();
        Require(view.nodes.data[1].world.translation[0] == 5 && view.nodes.data[0].worldBounds.minimum.x == 4 &&
            view.nodes.data[1].previousWorld.translation[0] == 19, "transform or temporal snapshot changed incorrectly");
        const uint64_t revision = view.contentRevision;
        transform.translation[0] = double(FLT_MAX) * 2;
        Require(!scene.SetTransform({17, 2}, transform).Succeeded() && scene.View().contentRevision == revision &&
            scene.View().nodes.data[2].world.translation[0] == 0, "out-of-float light world escaped transaction");
        transform = view.nodes.data[0].transform;
        transform.scaling[0] = double(FLT_MAX);
        Require(!scene.SetTransform({17, 0}, transform).Succeeded() && scene.View().contentRevision == revision &&
            scene.View().nodes.data[0].world.linear[0] == 1 && scene.View().nodes.data[1].world.translation[0] == 5,
            "invalid descendant world partially committed parent transform");
        const auto beforeAdvance = scene.View();
        const auto advance = scene.AdvancePreviousTransforms();
        Require(advance.Succeeded() && advance.changed &&
            scene.View().previousTransformRevision == beforeAdvance.previousTransformRevision + 1 &&
            scene.View().contentRevision == beforeAdvance.contentRevision &&
            scene.View().transformRevision == beforeAdvance.transformRevision &&
            !scene.AdvancePreviousTransforms().changed, "previous snapshot revision semantics");
        Require(scene.View().nodes.data[1].previousLocal.translation[0] == 5 && scene.View().nodes.data[1].previousWorld.translation[0] == 5,
            "previous transform advance did not copy current affines");
        RendererScene moved(static_cast<RendererScene&&>(scene));
        Require(moved.IsPublished() && !scene.IsPublished() && scene.StorageBytes() == 0, "move retained two owners");
        moved.Reset();
        Require(!moved.IsPublished() && moved.StorageBytes() == 0, "reset left published data");
    }

    void CheckMaterialBatch()
    {
        RendererScene scene;
        auto build = PrepareFixture(scene, 2);
        Require(SealFixture(scene, build).Succeeded() && scene.Publish(31).Succeeded(), "batch fixture");
        const auto before = scene.View();
        RendererSceneMaterial bytesBefore[2];
        memcpy(bytesBefore, before.materials.data, sizeof(bytesBefore));
        const uint32_t contentBefore = before.nodes.data[0].subtreeContent;
        RendererSceneMaterialValues values[]{before.materials.data[0].values, before.materials.data[1].values};
        values[0].domain = RendererMaterialDomain::AlphaTested;
        values[1].opacity = NAN;
        Require(!scene.SetMaterials(31, {values, 2}).Succeeded(), "late invalid batch accepted");
        auto after = scene.View();
        Require(memcmp(bytesBefore, after.materials.data, sizeof(bytesBefore)) == 0 &&
            after.contentRevision == before.contentRevision && after.materialRevision == before.materialRevision &&
            after.lightRevision == before.lightRevision && after.transformRevision == before.transformRevision &&
            after.nodes.data[0].subtreeContent == contentBefore, "failed batch partially changed scene");
        values[1].opacity = 0.5f;
        Require(!scene.SetMaterials(30, {values, 2}).Succeeded() && !scene.SetMaterials(31, {values, 1}).Succeeded(),
            "stale or incomplete batch accepted");
        const auto result = scene.SetMaterials(31, {values, 2});
        after = scene.View();
        Require(result.Succeeded() && result.changed && after.materialRevision == before.materialRevision + 1 &&
            after.contentRevision == before.contentRevision + 1 && after.materials.data[1].values.opacity == 0.5f &&
            (after.nodes.data[0].subtreeContent & SceneContentAlphaTested) != 0 &&
            (after.nodes.data[0].subtreeContent & SceneContentOpaque) == 0, "batch commit or derived content");
        const auto equal = scene.SetMaterials(31, {values, 2});
        Require(equal.Succeeded() && !equal.changed && scene.View().contentRevision == after.contentRevision,
            "equal batch advanced revision");
    }

    void CheckMalformedInputs()
    {
        for (uint32_t fault = 0; fault < 22; ++fault)
        {
            RendererScene scene;
            auto build = PrepareFixture(scene);
                switch (fault)
            {
            case 0: build.nodes[1].parentIndex = 2; build.nodes[2].parentIndex = 1; break;
            case 1: build.nodes[1].nextSiblingIndex = 1; break;
            case 2: build.nodes[0].firstChildIndex = 99; break;
            case 3: build.nodes[3].nextSiblingIndex = InvalidSceneIndex; break;
            case 4: build.nodes[0].nextSiblingIndex = 1; break;
            case 5: build.nodes[1].leafKind = RendererSceneLeafKind::Count; break;
            case 6: build.nodes[2].leafKind = RendererSceneLeafKind::Instance; break;
            case 7: build.meshes[0].geometries = {1, 1}; break;
            case 8: build.meshes[0].indexOffset = UINT32_MAX; break;
            case 9: build.geometries[0].vertexOffsetInMesh = 3; break;
            case 10: build.bufferGroups[0].attributes[0].offset = 1; build.bufferGroups[0].vertexBytes = 37; break;
            case 11: build.bufferGroups[0].attributes[0].size = 24; break;
            case 12: build.materials[0].values.textures[0] = 1; break;
            case 13: build.channels[0].materialIndex = 0; break;
            case 14: build.keyframes[1].time = -1; break;
            case 15: build.nodes[2].previousWorld.translation[0] = double(FLT_MAX) * 2; break;
            case 16: build.nodes[2].hasLocalTransform = true; build.nodes[2].transform.translation[0] = double(FLT_MAX) * 2; break;
            case 17: build.channels[0].attribute = RendererSceneAnimationAttribute::Undefined; break;
            case 18: build.channels[0].attribute = RendererSceneAnimationAttribute::LeafProperty; break;
            case 19: build.cameras[0].hasFarPlane = true; build.cameras[0].farPlane = NAN; break;
            case 20: build.cameras[0].hasAspectRatio = true; build.cameras[0].aspectRatio = NAN; break;
            case 21: build.cameras[0].kind = RendererSceneCameraKind::Orthographic; build.cameras[0].xMagnitude = NAN; break;
            default: Require(false, "unknown malformed case");
            }
            Require(!SealFixture(scene, build).Succeeded() && !scene.IsSealed() && scene.View().nodes.count == 0,
                "malformed input became published");
        }
        RendererScene scene;
        auto build = PrepareFixture(scene);
        Require(SealFixture(scene, build).Succeeded(), "workspace fixture input");
        scene.Reset();
        build = PrepareFixture(scene);
        for (uint32_t i = 0; i < 5; ++i) Require(scene.Write(i, build.nodes[i]).Succeeded(), "workspace input");
        uint8_t small[4]{};
        Require(scene.Seal(0, {small, 4}).error == RendererSceneError::Workspace && SealFixture(scene, build).Succeeded(),
            "checked workspace failure did not preserve candidate retry");
    }

    void CheckRetainedRepresentation()
    {
        for (uint32_t variant = 0; variant < 4; ++variant)
        {
            RendererScene scene;
            auto build = PrepareFixture(scene);
            auto& camera = build.cameras[0];
            if (variant == 0)
            {
                camera.farPlane = camera.aspectRatio = camera.xMagnitude = camera.yMagnitude = NAN;
                build.geometries[0].indexCount = 1;
            }
            else
            {
                camera.kind = RendererSceneCameraKind::Orthographic;
                camera.verticalFov = camera.aspectRatio = NAN;
                build.meshes[0].type = RendererSceneMeshType(variant);
                build.geometries[0].primitive = RendererScenePrimitive::Lines;
                build.geometries[0].indexCount = 3;
            }
            Require(SealFixture(scene, build).Succeeded() && scene.Publish(41).Succeeded(),
                "scene storage narrowed consumer eligibility or inactive camera fields");
        }
    }

    void CheckAllocationFailures()
    {
        RendererSceneCounts counts;
        counts.nodes = counts.meshes = counts.geometries = counts.instances = counts.materials = 1;
        counts.textures = counts.bufferGroups = counts.morphRanges = counts.joints = counts.lights = 1;
        counts.cameras = counts.animations = counts.channels = counts.samplers = counts.keyframes = counts.stringBytes = 1;
        for (uint32_t ordinal = 1; ordinal <= 20; ++ordinal)
        {
            SetRendererSceneAllocationFailure(ordinal);
            RendererScene scene;
            const auto result = scene.Prepare(counts);
            if (ordinal <= 19)
                Require(result.error == RendererSceneError::Allocation && scene.StorageBytes() == 0 &&
                    !scene.Write(0, RendererSceneNode{}).Succeeded(), "partial allocation escaped owner cleanup");
            else
                Require(result.Succeeded() && scene.StorageBytes() != 0, "allocation sequence count changed");
        }
        SetRendererSceneAllocationFailure(0);
    }

    DWORD WINAPI CheckDeepAndWide(void*)
    {
        constexpr uint32_t count = 100000;
        ULONG_PTR low = 0, high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        Require(high > low && high - low <= 65536, "actual hierarchy stack reservation");
        uint8_t* workspace = new (std::nothrow) uint8_t[count];
        Require(workspace != nullptr, "stress workspace allocation");
        for (uint32_t wide = 0; wide < 2; ++wide)
        {
            RendererScene scene;
            RendererSceneCounts counts;
            counts.nodes = count;
            Require(scene.Prepare(counts).Succeeded(), "stress scene allocation");
            for (uint32_t index = 0; index < count; ++index)
            {
                RendererSceneNode node;
                node.parentIndex = index == 0 ? InvalidSceneIndex : wide ? 0 : index - 1;
                if (index + 1 < count)
                {
                    if (wide && index != 0) node.nextSiblingIndex = index + 1;
                    else node.firstChildIndex = index + 1;
                }
                if (index == 0 || index % 1024 == 0)
                {
                    node.hasLocalTransform = true;
                    node.transform.translation[0] = index ? 0.25 : 2;
                    node.transform.translation[1] = index ? -0.5 : 3;
                    node.transform.translation[2] = index ? 0.125 : 5;
                }
                Require(scene.Write(index, node).Succeeded(), "stress input copy");
            }
            Require(scene.Seal(0, {workspace, count}).Succeeded() && scene.Publish(1).Succeeded(), "deep/wide seal");
            auto view = scene.View();
            const auto check = [&](double x, double y, double previousX, double previousY, bool committed)
            {
                Require(view.nodes.count == count && view.preorder.count == count, "deep/wide complete coverage");
                for (uint32_t index = 0; index < count; ++index)
                {
                    const auto& node = view.nodes.data[index];
                    const uint32_t parent = index == 0 ? InvalidSceneIndex : wide ? 0 : index - 1;
                    const uint32_t child = index + 1 < count && (!wide || index == 0) ? index + 1 : InvalidSceneIndex;
                    const uint32_t sibling = wide && index && index + 1 < count ? index + 1 : InvalidSceneIndex;
                    Require(node.parentIndex == parent && node.firstChildIndex == child && node.nextSiblingIndex == sibling &&
                        view.preorder.data[index] == index && node.preorderIndex == index &&
                        node.subtreeEnd == (wide && index ? index + 1 : count), "complete hierarchy order");
                    const double steps = wide ? (index && index % 1024 == 0 ? 1 : 0) : index / 1024;
                    RendererSceneAffine world, previous, previousLocal;
                    world.translation[0] = x + steps * 0.25; world.translation[1] = y - steps * 0.5;
                    world.translation[2] = 5 + steps * 0.125;
                    if (committed)
                    {
                        previous.translation[0] = previousX + steps * 0.25;
                        previous.translation[1] = previousY - steps * 0.5; previous.translation[2] = world.translation[2];
                        if (index == 0 || index % 1024 == 0)
                        {
                            previousLocal.translation[0] = index ? 0.25 : previousX;
                            previousLocal.translation[1] = index ? -0.5 : previousY;
                            previousLocal.translation[2] = index ? 0.125 : 5;
                        }
                    }
                    Require(!memcmp(&node.world, &world, sizeof(world)) && !memcmp(&node.previousWorld, &previous, sizeof(previous)) &&
                        !memcmp(&node.previousLocal, &previousLocal, sizeof(previousLocal)),
                        "closed-form deep/wide current and independent previous affines");
                }
            };
            check(2, 3, 0, 0, false);
            Require(scene.AdvancePreviousTransforms().changed, "deep/wide initial previous commit");
            auto transform = view.nodes.data[0].transform;
            transform.translation[0] += 4; transform.translation[1] -= 2;
            Require(scene.SetTransform({1, 0}, transform).changed, "deep/wide parent edit");
            check(6, 1, 2, 3, true);
            transform.translation[0] += 3;
            Require(scene.SetTransform({1, 0}, transform).changed, "second edit before previous commit");
            check(9, 1, 2, 3, true);
            const auto revision = scene.View().contentRevision;
            auto invalid = transform; invalid.translation[0] = DBL_MAX;
            Require(!scene.SetTransform({1, 0}, invalid).Succeeded() && scene.View().contentRevision == revision &&
                view.nodes.data[0].transform.translation[0] == transform.translation[0], "rejected hierarchy edit is atomic");
            Require(scene.AdvancePreviousTransforms().changed, "deep/wide final previous commit");
            check(9, 1, 9, 1, true);
            view = {};
            scene.Reset(); scene.Reset();
            Require(!scene.StorageBytes() && !scene.IsPublished() && !scene.View().nodes.count,
                "deep/wide owner drains on the small stack");
        }
        delete[] workspace;
        puts("scene hierarchy: depth/width 100000, complete order, two edits, previous snapshots and reset on an actual 64 KiB stack passed");
        return 0;
    }
}

void CheckRendererSceneNativeMath();
void CheckRendererSceneMaterialModes();

int main()
{
    CheckSelectionIdentity();
    CheckInstanceRevisions();
    CheckPreparationCopies();
    CheckPublicationAndMutation();
    CheckDeclaredMeshBounds();
    CheckMaterialBatch();
    CheckRendererSceneMaterialModes();
    CheckRendererSceneNativeMath();
    CheckMalformedInputs();
    CheckRetainedRepresentation();
    CheckAllocationFailures();
    HANDLE worker = CreateThread(nullptr, 64 * 1024, CheckDeepAndWide, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    Require(worker != nullptr && WaitForSingleObject(worker, INFINITE) == WAIT_OBJECT_0, "bounded-stack worker");
    DWORD result = 1;
    Require(GetExitCodeThread(worker, &result) && result == 0, "bounded-stack result");
    CloseHandle(worker);
    puts("scene owner passed: 22 malformed cases, 4 retained-representation cases, 19 allocation failures, copied preparation, transactional edits, depth/width 100000 on 64 KiB stack");
    return 0;
}
