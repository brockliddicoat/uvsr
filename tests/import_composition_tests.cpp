#include "renderer_import_composition.h"
#include "renderer_scene_light.h"
#include "import_runtime_light_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include <math.h>
#include <Windows.h>

namespace
{
    using namespace uvsr;
    constexpr uint32_t invalid = InvalidSceneIndex;
    size_t rejected = 0;

    void Require(bool condition, const char* message)
    { if (!condition) { fprintf(stderr, "composition: %s\n", message); exit(1); } }
    void Good(ImportResult result, const char* message)
    {
        if (!result)
        {
            fprintf(stderr, "composition: %s: %s, object %u, index %zu\n", message,
                ImportErrorText(result.error), unsigned(result.object), result.index);
            exit(1);
        }
    }
    void Bad(ImportResult result, ImportError expected, const char* message)
    {
        if (result.error != expected)
        {
            fprintf(stderr, "composition: %s: expected %s, got %s, object %u, index %zu\n", message,
                ImportErrorText(expected), ImportErrorText(result.error), unsigned(result.object), result.index);
            exit(1);
        }
        ++rejected;
    }
    ArrayView<const uint8_t> Bytes(const char* value) { return {reinterpret_cast<const uint8_t*>(value), strlen(value)}; }
    ArrayView<const char> Text(const char* value) { return {value, strlen(value)}; }
    std::string Text(const RendererSceneView& scene, RendererSceneString value)
    { const auto view = RendererSceneText(scene, value); return view.count ? std::string(view.data, view.count) : std::string{}; }
    ImportCompositionOptions Options() { ImportCompositionOptions value; value.generation = 907; return value; }

    ImportModel Model(const char* material = "M")
    {
        std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"positions.bin","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"type":"VEC3","count":3}],"materials":[{"name":")";
        json += material;
        json += R"("}],"meshes":[{"name":"triangle","primitives":[{"attributes":{"POSITION":0},"material":0}]}],"nodes":[{"name":"mesh","mesh":0,"children":[1]},{"name":"joint","translation":[0,1,0]}],"scenes":[{"nodes":[0]}]})";
        float positions[]{0,0,0, 1,0,0, 0,1,0};
        ImportDocument document;
        Good(document.Parse(Bytes(json.c_str())), "model parse");
        Good(document.SupplyBuffer(0, {reinterpret_cast<const uint8_t*>(positions), sizeof(positions)}), "model buffer");
        ImportSceneOptions options;
        options.generation = 17;
        options.modelName = Text("model.gltf");
        options.modelPath = Text("C:/scenes/model.gltf");
        ImportModel output;
        Good(ConvertImportScene(document, options, output.scene, output.geometry, &output.textures), "model conversion");
        document.Reset();
        memset(positions, 0xa5, sizeof(positions));
        memset(json.data(), 0xa5, json.size());
        return output;
    }

    ImportSceneDescription Description(const char* json)
    {
        std::string bytes(json);
        ImportSceneDescription result;
        Good(result.Parse(Bytes(bytes.c_str()), Text("C:/scenes/main.scene.json")), "description parse");
        memset(bytes.data(), 0xcc, bytes.size());
        return result;
    }
    uint32_t Child(const RendererSceneView& scene, uint32_t parent, const char* name)
    {
        uint32_t current = scene.nodes.data[parent].firstChildIndex;
        while (current != invalid && Text(scene, scene.nodes.data[current].name) != name) current = scene.nodes.data[current].nextSiblingIndex;
        Require(current != invalid, name);
        return current;
    }
    uint32_t RootChild(const RendererSceneView& scene, const char* name) { return Child(scene, scene.root, name); }

    const char* composition = R"({"models":["model.gltf","unused.gltf"],"graph":[{"name":"A","model":0,"translation":[2,3,4],"scaling":[2,1,1],"type":"PerspectiveCamera","verticalFov":0.75,"children":[{"name":"lamp","type":"PointLight","intensity":4}]},{"name":"B","model":0}],"animations":[{"name":"mixed","channels":[{"attribute":"translation","mode":"linear","targets":["/A/mesh","material:M","/absent"],"data":[{"time":0,"value":[1,2,3]},{"time":2,"value":4}]}]},{"name":"empty","channels":[{"attribute":"radius","target":"/missing"}]}]})";

    void Empty()
    {
        auto description = Description("{}");
        RendererScene scene;
        ImportGeometry geometry;
        ImportTextures textures;
        Good(ComposeImportScene(description, {}, Options(), scene, geometry, textures), "empty composition");
        const auto view = scene.View();
        Require(view.nodes.count == 1 && Text(view, view.nodes.data[0].name) == "SceneRoot" && !view.instances.count, "empty root");
    }

    void CheckComposed(const RendererScene& scene, const ImportGeometry& geometry, const ImportCompositionStats& stats)
    {
        const auto view = scene.View();
        Require(view.nodes.count == 11 && view.meshes.count == 1 && view.instances.count == 2 && view.materials.count == 1 &&
            view.lights.count == 2 && view.cameras.count == 2 && view.animations.count == 1 && view.channels.count == 2 && view.samplers.count == 1,
            "composed counts exclude unused model");
        const uint32_t a = RootChild(view, "A"), b = RootChild(view, "B");
        Require(memcmp(&view.nodes.data[a].transform, &view.nodes.data[b].transform, sizeof(RendererSceneTransform)) == 0, "second placement inherits first TRS");
        Require(view.nodes.data[a].transform.translation[0] == 2 && view.nodes.data[b].transform.scaling[0] == 2, "authored TRS retained");
        const uint32_t am = Child(view, a, "mesh"), bm = Child(view, b, "mesh");
        Require(view.instances.data[view.nodes.data[am].leafIndex].meshIndex == view.instances.data[view.nodes.data[bm].leafIndex].meshIndex, "static mesh shared");
        Require(Child(view, a, "lamp") != Child(view, b, "lamp"), "added child cloned");
        Require(view.nodes.data[a].leafKind == RendererSceneLeafKind::Camera && view.nodes.data[b].leafKind == RendererSceneLeafKind::Camera &&
            view.nodes.data[a].leafIndex != view.nodes.data[b].leafIndex, "root leaf replacement cloned with distinct owner");
        Require(view.cameras.data[0].verticalFov == 0.75f && view.cameras.data[1].verticalFov == 0.75f &&
            view.lights.data[0].values.intensity == 4 && view.lights.data[1].values.intensity == 4, "typed fields retained");
        Require(view.channels.data[0].nodeIndex == am && view.channels.data[0].attribute == RendererSceneAnimationAttribute::Translation &&
            view.channels.data[1].materialIndex == 0 && view.channels.data[1].attribute == RendererSceneAnimationAttribute::LeafProperty &&
            Text(view, view.channels.data[1].property) == "translation", "mixed target attributes");
        Require(view.channels.data[0].samplerIndex == view.channels.data[1].samplerIndex && view.keyframes.count == 2 &&
            view.keyframes.data[1].value.w == 4 && view.animations.data[0].duration == 2, "shared sampler and scalar key");
        Require(stats.ignoredAnimationTargets == 2 && stats.ambiguousMaterialTargets == 0 && stats.peakScratchBytes > 0, "composition diagnostics");
        Require(geometry.BufferCount() == 1 && geometry.Buffer(0).vertices.count != 0 && geometry.ConversionScratchBytes() == stats.peakScratchBytes,
            "owned payload and scratch accounting");
        for (size_t n = 0; n < view.nodes.count; ++n)
            Require(memcmp(&view.nodes.data[n].world, &view.nodes.data[n].previousWorld, sizeof(RendererSceneAffine)) == 0, "initial previous transforms");
    }

    void Placement()
    {
        auto description = Description(composition);
        ImportModel models[]{Model(), Model("unused")};
        const auto* original = models[0].geometry.Buffer(0).vertices.data;
        RendererScene scene;
        ImportGeometry geometry;
        ImportTextures textures;
        ImportCompositionStats stats;
        Good(ComposeImportScene(description, models, Options(), scene, geometry, textures, &stats), "composition");
        description.Reset();
        CheckComposed(scene, geometry, stats);
        Require(geometry.Buffer(0).vertices.data == original, "geometry moved without a payload copy");
        Require(!models[0].scene.StorageBytes() && !models[1].scene.StorageBytes() && !models[0].geometry.StorageBytes() &&
            !models[1].geometry.StorageBytes(), "all temporary model owners consumed");
        RendererScene moved = std::move(scene);
        ImportGeometry movedGeometry = std::move(geometry);
        ImportTextures movedTextures = std::move(textures);
        CheckComposed(moved, movedGeometry, stats);
        movedGeometry = std::move(movedGeometry);
        CheckComposed(moved, movedGeometry, stats);
    }

    void ParentOrdering()
    {
        auto description = Description(R"({"graph":[{"name":"skipped","parent":"/future","children":[{"name":"also skipped"}]},{"name":"future","children":[{"name":""},{"name":"."}]},{"name":"backward","parent":"/future"},{"name":"relative","parent":"future"},{"name":"dup"},{"name":"dup"},{"name":"first duplicate","parent":"/dup"},{"name":"empty child","parent":"/future/"},{"name":"dot child","parent":"/future/."}]})");
        RendererScene scene;
        ImportGeometry geometry;
        ImportTextures textures;
        ImportCompositionStats stats;
        Good(ComposeImportScene(description, {}, Options(), scene, geometry, textures, &stats), "custom parents");
        const auto view = scene.View();
        const uint32_t future = RootChild(view, "future");
        Require(stats.skippedParentSubtrees == 2 && view.nodes.count == 10, "unresolved parents skip full subtrees");
        Child(view, future, "backward");
        Child(view, Child(view, future, ""), "empty child");
        Child(view, Child(view, future, "."), "dot child");
        const uint32_t duplicate = RootChild(view, "dup");
        Child(view, duplicate, "first duplicate");
        Require(view.nodes.data[view.nodes.data[duplicate].nextSiblingIndex].firstChildIndex == invalid, "duplicate name chooses first sibling");
    }

    void Hazards()
    {
        const char* cases[]{
            R"({"models":["m.gltf"],"graph":[{"model":0,"children":[{"model":0}]}]})",
            R"({"models":["m.gltf"],"graph":[{"name":"A","model":0},{"model":0,"parent":"/A/mesh"}]})",
            R"({"models":["m.gltf"],"graph":[{"parent":"/.."}]})"};
        for (const auto* json : cases)
        {
            auto description = Description(json);
            ImportModel models[]{Model()};
            const auto* input = models[0].geometry.Buffer(0).vertices.data;
            RendererScene scene;
            ImportGeometry geometry;
            ImportTextures textures;
            Bad(ComposeImportScene(description, models, Options(), scene, geometry, textures), ImportError::InvalidHierarchy, "unsafe hierarchy");
            Require(models[0].geometry.Buffer(0).vertices.data == input && models[0].scene.IsPublished() &&
                !scene.StorageBytes() && !geometry.StorageBytes() && !textures.StorageBytes(), "hazard preserves owners");
        }
    }

    void FailuresAndLimits()
    {
        auto description = Description(composition);
        size_t scratch = 0, storage = 0;
        {
            ImportModel models[]{Model(), Model("unused")};
            RendererScene scene; ImportGeometry geometry; ImportTextures textures;
            ImportCompositionStats stats;
            Good(ComposeImportScene(description, models, Options(), scene, geometry, textures, &stats), "measure composition capacities");
            scratch = stats.peakScratchBytes; storage = geometry.StorageBytes();
        }
        for (uint32_t test = 0; test < 4; ++test)
        {
            ImportModel models[]{Model(), Model("unused")};
            const auto* pointer = models[0].geometry.Buffer(0).vertices.data;
            auto options = Options(); options.maxScratchBytes = scratch; options.maxGeometryBytes = storage;
            if (test == 1) --options.maxScratchBytes;
            if (test == 2) --options.maxGeometryBytes;
            if (test == 3) options.generation = 0;
            RendererScene scene; ImportGeometry geometry; ImportTextures textures;
            ImportCompositionStats stats; stats.peakScratchBytes = 765;
            const auto result = ComposeImportScene(description, models, options, scene, geometry, textures, &stats);
            if (!test) { Good(result, "exact composition budgets"); CheckComposed(scene, geometry, stats); }
            else
            {
                Bad(result, test == 1 ? ImportError::Workspace : test == 2 ? ImportError::Capacity : ImportError::InvalidState, "composition limit");
                Require(models[0].geometry.Buffer(0).vertices.data == pointer && models[0].scene.IsPublished() &&
                    !scene.StorageBytes() && !geometry.StorageBytes() && !textures.StorageBytes() && stats.peakScratchBytes == 765,
                    "capacity failure preserves inputs outputs and stats");
                Good(ComposeImportScene(description, models, Options(), scene, geometry, textures, &stats), "capacity retry");
                CheckComposed(scene, geometry, stats);
            }
        }
        size_t importFailures = 0, sceneFailures = 0;
        for (uint32_t kind = 0; kind < 2; ++kind)
        {
            bool completed = false;
            for (uint32_t fault = kind ? 1 : 0; fault < 256; ++fault)
            {
                ImportModel models[]{Model(), Model("unused")};
                const auto* pointer = models[0].geometry.Buffer(0).vertices.data;
                const auto oldView = models[0].scene.View();
                RendererScene scene; ImportGeometry geometry; ImportTextures textures;
                ImportCompositionStats stats; stats.peakScratchBytes = 765;
                if (kind) SetRendererSceneAllocationFailure(fault);
                else SetImportAllocationFailureCountdown(fault);
                const auto result = ComposeImportScene(description, models, Options(), scene, geometry, textures, &stats);
                SetRendererSceneAllocationFailure(0); SetImportAllocationFailureCountdown(-1);
                if (result) { CheckComposed(scene, geometry, stats); completed = true; break; }
                Bad(result, ImportError::OutOfMemory, "injected allocation failure");
                if (kind) ++sceneFailures; else ++importFailures;
                Require(models[0].geometry.Buffer(0).vertices.data == pointer && models[0].scene.View().nodes.data == oldView.nodes.data &&
                    !scene.StorageBytes() && !geometry.StorageBytes() && !textures.StorageBytes() && stats.peakScratchBytes == 765,
                    "allocation failure preserves exact owner identities");
                Good(ComposeImportScene(description, models, Options(), scene, geometry, textures, &stats), "same-input allocation retry");
                CheckComposed(scene, geometry, stats);
            }
            Require(completed, "allocation sweep reached uninjected success");
        }
        {
            ImportModel models[]{Model(), Model("unused")};
            RendererScene scene; ImportGeometry geometry; ImportTextures textures;
            Bad(ComposeImportScene(description, {models, 1}, Options(), scene, geometry, textures), ImportError::InvalidInput, "model count mismatch");
            ImportSceneDescription empty;
            Bad(ComposeImportScene(empty, models, Options(), scene, geometry, textures), ImportError::InvalidState, "missing description");
            Bad(ComposeImportScene(description, {nullptr, 2}, Options(), scene, geometry, textures), ImportError::InvalidState, "invalid model view");
            auto occupied = Model("occupied");
            const auto* source = occupied.scene.View().nodes.data;
            Bad(ComposeImportScene(description, models, Options(), occupied.scene, geometry, textures), ImportError::InvalidState, "nonempty scene output");
            Require(occupied.scene.View().nodes.data == source && models[0].scene.IsPublished(), "occupied owner preserved");
            Bad(ComposeImportScene(description, models, Options(), scene, occupied.geometry, textures), ImportError::InvalidState, "nonempty geometry output");
        }
        printf("composition allocation failures: import %zu, canonical %zu, exact scratch %zu and geometry %zu bytes, all retries passed\n",
            importFailures, sceneFailures, scratch, storage);
    }

    ImportModel SkinModel()
    {
        struct Input
        {
            float positions[9]{0,0,0, 1,0,0, 0,1,0};
            uint16_t joints[12]{};
            float weights[12]{};
            float inverse[16]{};
            float times[2]{0,1};
            float translations[6]{1,0,0, 2,0,0};
        } input;
        static_assert(sizeof(Input) == 204 && offsetof(Input, inverse) == 108 && offsetof(Input, times) == 172);
        for (uint32_t i = 0; i < 3; ++i) input.weights[i * 4] = 1;
        for (uint32_t i = 0; i < 16; i += 5) input.inverse[i] = 1;
        const char* json = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"rig.bin","byteLength":204}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":48},{"buffer":0,"byteOffset":108,"byteLength":64},{"buffer":0,"byteOffset":172,"byteLength":8},{"buffer":0,"byteOffset":180,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"VEC4"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5126,"count":1,"type":"MAT4"},{"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR"},{"bufferView":5,"componentType":5126,"count":2,"type":"VEC3"}],"meshes":[{"name":"rig","primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}]}],"skins":[{"joints":[1],"inverseBindMatrices":3}],"nodes":[{"name":"skin","mesh":0,"skin":0},{"name":"joint","translation":[1,0,0]}],"scenes":[{"nodes":[0,1]}],"animations":[{"name":"move","samplers":[{"input":4,"output":5}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}]})";
        ImportDocument document;
        Good(document.Parse(Bytes(json)), "skin model parse");
        Good(document.SupplyBuffer(0, {reinterpret_cast<const uint8_t*>(&input), sizeof(input)}), "skin model supply");
        ImportSceneOptions options; options.generation = 15; options.modelName = Text("rig.gltf");
        ImportModel output;
        Good(ConvertImportScene(document, options, output.scene, output.geometry, &output.textures), "skin model conversion");
        document.Reset(); memset(&input, 0xa5, sizeof(input));
        return output;
    }

    void SkinClones()
    {
        auto description = Description(R"({"models":["rig.gltf"],"graph":[{"name":"A","model":0,"translation":[2,3,4]},{"name":"B","model":0,"translation":[10,5,6],"euler":[0.2,0.3,0.4],"scaling":[1.3,0.8,1.1]}]})");
        ImportModel models[]{SkinModel()};
        const auto original = models[0].scene.View();
        const auto& originalMesh = original.meshes.data[original.instances.data[0].meshIndex];
        const uint32_t originalGroup = original.meshes.data[originalMesh.skinPrototypeIndex].bufferGroupIndex;
        const auto* payload = models[0].geometry.Buffer(originalGroup).vertices.data;
        RendererScene scene; ImportGeometry geometry; ImportTextures textures;
        Good(ComposeImportScene(description, models, Options(), scene, geometry, textures), "skin clones");
        description.Reset();
        const auto view = scene.View();
        Require(view.instances.count == 2 && view.meshes.count == 3 && view.joints.count == 2 && view.bufferGroups.count == 3 &&
            view.animations.count == 2 && view.samplers.count == 1 && view.channels.count == 2, "skin clone ownership counts");
        const uint32_t roots[]{RootChild(view, "A"), RootChild(view, "B")};
        uint32_t prototype = invalid;
        for (uint32_t i = 0; i < 2; ++i)
        {
            const auto& instance = view.instances.data[i];
            const auto& mesh = view.meshes.data[instance.meshIndex];
            if (!i) prototype = mesh.skinPrototypeIndex;
            Require(mesh.skinPrototypeIndex == prototype && view.joints.data[instance.joints.first].nodeIndex == Child(view, roots[i], "joint"), "joint identity remapped inside each clone");
            Require(view.channels.data[i].nodeIndex == Child(view, roots[i], "joint"), "animation target remapped inside each clone");
            const auto group = geometry.Buffer(mesh.bufferGroupIndex);
            Require(group.skinInstanceIndex == i && group.vertices.count == 0 && group.jointMatrices.count == 1 &&
                fabsf(group.jointMatrices.data[0].values[12] - 1) < 0.00001f, "final-pose palette owned by each derived group");
        }
        Require(geometry.Buffer(view.meshes.data[prototype].bufferGroupIndex).vertices.data == payload, "skin prototype payload transferred once");
        auto bad = Description(R"({"models":["rig.gltf"],"graph":[{"name":"singular","model":0,"scaling":[0,1,1]}]})");
        ImportModel retryModels[]{SkinModel()};
        const auto* before = retryModels[0].scene.View().nodes.data;
        RendererScene retryScene; ImportGeometry retryGeometry; ImportTextures retryTextures;
        Bad(ComposeImportScene(bad, retryModels, Options(), retryScene, retryGeometry, retryTextures), ImportError::InvalidData, "singular final skin pose");
        Require(retryModels[0].scene.View().nodes.data == before && retryModels[0].geometry.BufferCount() == 2 &&
            !retryScene.StorageBytes() && !retryGeometry.StorageBytes(), "late skin failure preserves all model resources");
        auto good = Description(R"({"models":["rig.gltf"],"graph":[{"name":"restored","model":0}]})");
        Good(ComposeImportScene(good, retryModels, Options(), retryScene, retryGeometry, retryTextures), "late skin retry");
    }

    void DeepWide()
    {
        ULONG_PTR low = 0, high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        Require(high > low && high - low <= 65536, "composition stack budget");
        constexpr uint32_t count = 100000;
        for (uint32_t wide = 0; wide < 2; ++wide)
        {
            ImportModel model;
            RendererSceneCounts counts; counts.nodes = count;
            Require(model.scene.Prepare(counts).Succeeded(), "large canonical input allocation");
            for (uint32_t i = 0; i < count; ++i)
            {
                RendererSceneNode node;
                node.parentIndex = i == 0 ? invalid : wide ? 0 : i - 1;
                node.firstChildIndex = wide ? (i == 0 ? 1 : invalid) : (i + 1 < count ? i + 1 : invalid);
                node.nextSiblingIndex = wide && i && i + 1 < count ? i + 1 : invalid;
                Require(model.scene.Write(i, node).Succeeded(), "large input node");
            }
            const size_t size = model.scene.SealWorkspaceBytes();
            auto* work = static_cast<uint8_t*>(malloc(size)); Require(work != nullptr, "large input workspace");
            Require(model.scene.Seal(0, {work, size}).Succeeded() && model.scene.Publish(15).Succeeded(), "large input seal");
            free(work);
            auto description = Description(R"({"models":["large.gltf"],"graph":[{"name":"A","model":0},{"name":"B","model":0}]})");
            RendererScene scene; ImportGeometry geometry; ImportTextures textures;
            Good(ComposeImportScene(description, {&model, 1}, Options(), scene, geometry, textures), "large composition");
            const auto view = scene.View();
            Require(view.nodes.count == count * 2 + 1 && !model.scene.StorageBytes(), "large clone counts");
            const uint32_t a = RootChild(view, "A"), b = RootChild(view, "B");
            Require(a == 1 && b == count + 1 && view.nodes.data[a].subtreeEnd == count + 1 && view.nodes.data[b].subtreeEnd == count * 2 + 1,
                "large subtree ranges");
            const RendererSceneAffine identity;
            for (uint32_t i = 0; i < view.nodes.count; ++i)
            {
                const auto& node = view.nodes.data[i];
                Require(view.preorder.data[i] == i && node.preorderIndex == i &&
                    memcmp(&node.world, &identity, sizeof(identity)) == 0 && memcmp(&node.previousWorld, &identity, sizeof(identity)) == 0,
                    "every large node order and transform");
                if (i == 0 || i == a || i == b) continue;
                const uint32_t expectedParent = wide ? (i < b ? a : b) : i - 1;
                Require(node.parentIndex == expectedParent, "every large parent");
            }
        }
        printf("composition hierarchy: 100000-node deep and wide sources cloned on %zu-byte stack\n", size_t(high - low));
    }
    DWORD WINAPI SmallStack(void*) { DeepWide(); return 0; }

    void AmbiguousMaterialTarget()
    {
        auto description = Description(R"({"models":["first.gltf","second.gltf"],"graph":[{"name":"A","model":0},{"name":"B","model":1}],"animations":[{"name":"material","channels":[{"attribute":"roughness","target":"material:M","data":[{"time":0,"value":0.5}]}]}]})");
        ImportModel models[]{Model(), Model()};
        RendererScene scene;
        ImportGeometry geometry;
        ImportTextures textures;
        ImportCompositionStats stats;
        Good(ComposeImportScene(description, models, Options(), scene, geometry, textures, &stats), "ambiguous material composition");
        const auto view = scene.View();
        Require(view.materials.count == 2 && view.instances.count == 2 && view.channels.count == 1 &&
            stats.ambiguousMaterialTargets == 1 && stats.ignoredAnimationTargets == 0, "ambiguous material keeps both source identities");
        const auto& channel = view.channels.data[0];
        Require(channel.materialIndex == 0 && channel.nodeIndex == invalid &&
            channel.attribute == RendererSceneAnimationAttribute::LeafProperty && Text(view, channel.property) == "roughness" &&
            view.materials.data[0].selectionId != view.materials.data[1].selectionId, "ambiguous material uses first canonical identity");
    }

    ImportModel RootLightModel(bool directionalChild)
    {
        ImportModel model;
        RendererSceneCounts counts; counts.nodes = counts.lights = 2; counts.stringBytes = 7;
        Require(model.scene.Prepare(counts).Succeeded(), "root-light model capacity");
        RendererSceneNode root, child;
        root.name = {0,4}; root.firstChildIndex = 1; root.leafKind = RendererSceneLeafKind::Light; root.leafIndex = 0;
        child.name = {4,3}; child.parentIndex = 0; child.leafKind = RendererSceneLeafKind::Light; child.leafIndex = 1;
        child.hasLocalTransform = true; child.transform.translation[1] = 6;
        RendererSceneLight first, second;
        first.nodeIndex = 0; first.values.irradiance = 99; first.values.color = {0.9f,0.8f,0.7f};
        second.nodeIndex = 1; second.kind = directionalChild ? RendererSceneLightKind::Directional : RendererSceneLightKind::Point;
        second.values.irradiance = 3; second.values.color = {0.1f,0.2f,0.3f};
        Require(model.scene.WriteStrings(0, Text("rootSun")).Succeeded() && model.scene.Write(0, root).Succeeded() &&
            model.scene.Write(1, child).Succeeded() && model.scene.Write(0, first).Succeeded() && model.scene.Write(1, second).Succeeded(),
            "root-light model records");
        uint8_t workspace[512]{};
        Require(model.scene.Seal(0, workspace).Succeeded() && model.scene.Publish(91).Succeeded() && model.scene.AdvancePreviousTransforms().Succeeded(),
            "root-light model publication");
        return model;
    }
    void RuntimeLights()
    {
        const char* json = R"({"models":["lights.gltf"],"graph":[{"name":"A","model":0,"type":"PointLight","intensity":4,
            "children":[{"name":"flashlight_1","type":"SpotLight","intensity":5}]},{"name":"B","model":0},
            {"name":"HdRi_SkY","type":"PointLight","intensity":9}]})";
        auto description = Description(json);
        auto options = Options(); options.runtimeLights = tests::RuntimeLightFixtureOptions();
        const auto check = [](const RendererScene& scene, ImportRuntimeLightIds ids, bool directionalChild)
        {
            const auto view = scene.View();
            const auto* sun = FindRendererSceneLight(view, ids.sun);
            const auto* flashlight = FindRendererSceneLight(view, ids.flashlight);
            Require(sun && flashlight && view.lights.count == (directionalChild ? 8u : 9u) && sun->kind == RendererSceneLightKind::Directional &&
                flashlight->kind == RendererSceneLightKind::Spot && sun->values.irradiance == 8 && sun->values.angularSize == 0.2f,
                "composed runtime light values and IDs");
            const auto a = RootChild(view, "A"), b = RootChild(view, "B");
            const auto first = Child(view, a, "sun_1"), second = Child(view, b, "sun_1");
            Require(view.lights.data[view.nodes.data[a].leafIndex].kind == RendererSceneLightKind::Point &&
                view.lights.data[view.nodes.data[b].leafIndex].kind == RendererSceneLightKind::Point, "dead directional roots stay replaced in clones");
            if (directionalChild)
            {
                Require(sun->nodeIndex == first && ids.sun.index == 0 && sun->values.color.x == 0.1f && sun->values.color.y == 0.2f &&
                    sun->values.color.z == 0.3f && view.lights.data[view.nodes.data[second].leafIndex].values.irradiance == 3 &&
                    view.nodes.data[first].world.translation[1] == 6, "first live directional keeps authored color/pose and other sun stays unchanged");
            }
            else Require(view.nodes.data[sun->nodeIndex].parentIndex == view.root && sun->nodeIndex + 1 == flashlight->nodeIndex &&
                view.nodes.data[sun->nodeIndex].world.translation[1] == 3, "dead directional does not suppress fallback sun");
            Require(Child(view, a, "flashlight_1") != flashlight->nodeIndex && Child(view, b, "flashlight_1") != flashlight->nodeIndex &&
                view.nodes.data[flashlight->nodeIndex].parentIndex == view.root && view.nodes.data[flashlight->nodeIndex].nextSiblingIndex == invalid &&
                ids.flashlight.index == view.lights.count - 1, "distinct runtime flashlight after authored and cloned duplicates");
            RootChild(view, "hdri_sky_1");
        };
        for (bool directionalChild : {false, true})
        {
            ImportModel model = RootLightModel(directionalChild);
            RendererScene scene; ImportGeometry geometry; ImportTextures textures;
            ImportCompositionStats stats;
            ImportRuntimeLightIds ids{{55,66}, {77,88}};
            Good(ComposeImportScene(description, {&model,1}, options, scene, geometry, textures, &stats, &ids), "composed runtime lights");
            check(scene, ids, directionalChild);
            const size_t scratch = stats.peakScratchBytes;
            for (uint32_t test = 0; test < 4; ++test)
            {
                model = RootLightModel(directionalChild); scene.Reset(); geometry.Reset(); textures.Reset();
                const auto before = model.scene.View(); const auto oldIds = ids;
                auto limits = options; limits.maxScratchBytes = scratch;
                ImportError expected = ImportError::None;
                if (test == 1) { --limits.maxScratchBytes; expected = ImportError::Workspace; }
                if (test == 2) { limits.runtimeLights.flashlight.values.intensity = NAN; expected = ImportError::InvalidData; }
                if (test == 3) { limits.generation = 0; expected = ImportError::InvalidState; }
                stats.peakScratchBytes = 765;
                const auto result = ComposeImportScene(description, {&model,1}, limits, scene, geometry, textures, &stats, &ids);
                if (!test) { Good(result, "exact runtime composition scratch"); check(scene, ids, directionalChild); }
                else
                {
                    Bad(result, expected, "runtime composition transaction rejection");
                    Require(model.scene.View().nodes.data == before.nodes.data && model.scene.View().lights.data[0].values.irradiance == 99 &&
                        !scene.StorageBytes() && !geometry.StorageBytes() && !textures.StorageBytes() && stats.peakScratchBytes == 765 &&
                        ids.sun == oldIds.sun && ids.flashlight == oldIds.flashlight, "runtime composition failure preserves input, outputs, stats and IDs");
                    Good(ComposeImportScene(description, {&model,1}, options, scene, geometry, textures, &stats, &ids), "runtime composition retry");
                    check(scene, ids, directionalChild);
                }
            }
            for (uint32_t kind = 0; kind < 2; ++kind)
            {
                bool completed = false;
                for (uint32_t fault = 0; fault < 256; ++fault)
                {
                    model = RootLightModel(directionalChild); scene.Reset(); geometry.Reset(); textures.Reset();
                    const auto* input = model.scene.View().nodes.data; const auto oldIds = ids;
                    if (kind) SetRendererSceneAllocationFailure(fault + 1);
                    else SetImportAllocationFailureCountdown(fault);
                    const auto result = ComposeImportScene(description, {&model,1}, options, scene, geometry, textures, nullptr, &ids);
                    SetRendererSceneAllocationFailure(0); SetImportAllocationFailureCountdown(-1);
                    if (result) { check(scene, ids, directionalChild); completed = true; break; }
                    Bad(result, ImportError::OutOfMemory, "runtime composition allocation failure");
                    Require(model.scene.View().nodes.data == input && !scene.StorageBytes() && !geometry.StorageBytes() && !textures.StorageBytes() &&
                        ids.sun == oldIds.sun && ids.flashlight == oldIds.flashlight, "runtime allocation failure preserves IDs and owner identity");
                    Good(ComposeImportScene(description, {&model,1}, options, scene, geometry, textures, nullptr, &ids), "runtime allocation retry");
                    check(scene, ids, directionalChild);
                }
                Require(completed, "runtime composition allocation fixture bound");
            }
        }
        puts("runtime composition: dead and cloned lights, alias names, authored duplicates, exact budgets, rollback and retries passed");
    }
    void ModelAnimationBoundary()
    {
        auto description = Description(R"({"models":["model.gltf"],"graph":[{"model":0}]})");
        for (uint32_t material = 0; material < 2; ++material)
        {
            ImportModel model;
            RendererSceneCounts counts;
            counts.nodes = counts.animations = counts.channels = counts.samplers = 1;
            counts.materials = material; counts.stringBytes = 9;
            Require(model.scene.Prepare(counts).Succeeded(), "non-glTF animation model allocation");
            RendererSceneNode node; node.leafKind = RendererSceneLeafKind::Animation; node.leafIndex = 0;
            RendererSceneAnimation animation{0, {0,1}};
            RendererSceneAnimationChannel channel;
            channel.nodeIndex = material ? invalid : 0;
            channel.materialIndex = material ? 0 : invalid;
            channel.attribute = RendererSceneAnimationAttribute::LeafProperty;
            channel.property = {0,9}; channel.samplerIndex = 0;
            Require(model.scene.Write(0, node).Succeeded() && model.scene.Write(0, animation).Succeeded() && model.scene.Write(0, channel).Succeeded() &&
                model.scene.Write(0, RendererSceneAnimationSampler{}).Succeeded() && model.scene.WriteStrings(0, Text("roughness")).Succeeded(), "non-glTF animation records");
            if (material)
            {
                RendererSceneMaterial value; value.selectionId = 0;
                Require(model.scene.Write(0, value).Succeeded(), "unreferenced model animation material");
            }
            uint8_t workspace[256]{};
            Require(model.scene.Seal(0, workspace).Succeeded() && model.scene.Publish(27).Succeeded(), "non-glTF animation model seal");
            const auto* before = model.scene.View().nodes.data;
            RendererScene scene; ImportGeometry geometry; ImportTextures textures;
            Bad(ComposeImportScene(description, {&model,1}, Options(), scene, geometry, textures), ImportError::UnsupportedData, "application properties cannot enter through model inputs");
            Require(model.scene.View().nodes.data == before && !scene.StorageBytes() && !geometry.StorageBytes(), "unsupported model animation preserves owners");
        }
    }
}

void RunImportCompositionReferenceTests();

int main()
{
    Empty();
    Placement();
    ParentOrdering();
    Hazards();
    FailuresAndLimits();
    SkinClones();
    AmbiguousMaterialTarget();
    ModelAnimationBoundary();
    RuntimeLights();
    HANDLE thread = CreateThread(nullptr, 65536, SmallStack, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    Require(thread != nullptr && WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0, "small-stack composition worker");
    DWORD code = 1; Require(GetExitCodeThread(thread, &code) && code == 0, "small-stack completion"); CloseHandle(thread);
    printf("composition increment: placement, shared payload, parents, animation targets, lifetime and %zu explicit rejections\n", rejected);
    RunImportCompositionReferenceTests();
    return 0;
}
