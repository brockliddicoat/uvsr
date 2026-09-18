#include "renderer_scene_descriptors_nvrhi.h"
#include "renderer_import_scene.h"
#include "renderer_shader_factory_nvrhi.h"
#include "renderer_scene_resources_nvrhi.h"
#include "renderer_scene_gpu_nvrhi.h"
#include "../cmake/RequireNoCppExceptions.h"
#include "renderer_gpu_fixture.h"
#include <filesystem>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;
    using Attribute = RendererSceneVertexAttribute;
    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "import skin GPU check failed: %s\n", reason);
        exit(1);
    }
    void Imported(ImportResult result, const char* reason)
    {
        if (result) return;
        fprintf(stderr, "import skin GPU check failed: %s: %s, object %u, index %zu\n",
            reason, ImportErrorText(result.error), unsigned(result.object), result.index);
        exit(1);
    }

    struct Input
    {
        float positions[9]{0,0,0, 1,0,0, 0,1,0};
        float normals[9]{0,0,1, 0,0,1, 0,0,1};
        float tangents[12]{1,0,0,-1, 1,0,0,-1, 1,0,0,-1};
        float uvs[6]{0,0, 1,0, 0,1};
        uint16_t joints[12]{0,1,0,1, 0,1,0,1, 0,1,0,1};
        float weights[12]{0.25f,0.75f,0,0, 0.25f,0.75f,0,0, 0.25f,0.75f,0,0};
        float inverseBinds[32]{};
        float secondUVs[6]{0.125f,0.375f, 0.625f,0.75f, 0.875f,-0.25f};
    };
    static_assert(sizeof(Input) == 368 && offsetof(Input, inverseBinds) == 216 && offsetof(Input, secondUVs) == 344);

    bool FloatWithinUlp(uint32_t expected, uint32_t actual, uint32_t& distance)
    {
        distance = expected > actual ? expected - actual : actual - expected;
        return !((expected ^ actual) & 0x80000000u) &&
            (expected & 0x7f800000u) != 0x7f800000u &&
            (actual & 0x7f800000u) != 0x7f800000u && distance <= 4;
    }
    bool Compare(const uint8_t* control, const uint8_t* candidate, size_t size,
        uint32_t mode, uint32_t instance, Attribute attribute, bool transformedFloat,
        bool& boundedDifference)
    {
        Require(!(size % 4), "compared skin range contains complete words");
        for (size_t word = 0; word < size; word += 4)
        {
            uint32_t expected = 0, actual = 0;
            memcpy(&expected, control + word, 4); memcpy(&actual, candidate + word, 4);
            if (expected != actual)
            {
                uint32_t ulp = 0;
                if (transformedFloat && FloatWithinUlp(expected, actual, ulp))
                {
                    boundedDifference = true;
                    fprintf(stderr, "skin mode %u, instance %u, attribute %u, word %zu: captured GPU %08x, imported GPU %08x, accepted %u ULP\n",
                        mode, instance, uint32_t(attribute), word / 4, expected, actual, ulp);
                    continue;
                }
                fprintf(stderr, "skin mode %u, instance %u, attribute %u, word %zu: captured GPU %08x, imported GPU %08x\n",
                    mode, instance, uint32_t(attribute), word / 4, expected, actual);
                return false;
            }
        }
        return true;
    }
}

void TestImportSkinGpu(nvrhi::IDevice* device, const std::filesystem::path& appShaderDirectory,
    RendererUploadHealth health)
{
    RendererGpuReference reference("renderer_skin_gpu_fixture.bin");
    RendererShaderFactory candidateShaders(device, appShaderDirectory.c_str());
    const auto shader = candidateShaders.CreateShader("uvsr/renderer_skinning_cs.hlsl", "main", {}, nvrhi::ShaderType::Compute);
    Require(bool(shader), "first-party skin shader");
    bool equal = true;
    bool boundedDifference = false;
    uint32_t comparisons = 0;
    for (uint32_t mode = 0; mode < 3; ++mode)
    {
        Input input;
        for (uint32_t joint = 0; joint < 2; ++joint)
            for (uint32_t lane = 0; lane < 16; lane += 5) input.inverseBinds[joint * 16 + lane] = 1;
        input.inverseBinds[12] = -0.5f;
        if (mode != 0)
        {
            const float position[]{0.123f,-0.234f,0.345f, 1.234f,0.321f,-0.456f, -0.135f,1.357f,0.246f};
            const float normal[]{0.2f,0.5f,0.8f, -0.3f,0.7f,0.6f, 0.4f,-0.2f,0.9f};
            const float tangent[]{0.8f,0.4f,-0.2f,-1, 0.7f,0.2f,0.3f,1, -0.5f,0.8f,0.1f,-1};
            const float weights[]{0.13f,0.29f,0.21f,0.37f, 0.43f,0.31f,0.17f,0.09f, 0.07f,0.23f,0.39f,0.31f};
            memcpy(input.positions, position, sizeof(position)); memcpy(input.normals, normal, sizeof(normal));
            memcpy(input.tangents, tangent, sizeof(tangent)); memcpy(input.weights, weights, sizeof(weights));
        }
        if (mode == 2)
        {
            input.inverseBinds[0] = 0.97f; input.inverseBinds[1] = 0.17f;
            input.inverseBinds[5] = 1.23f; input.inverseBinds[6] = -0.09f;
            input.inverseBinds[16] = 1.15f; input.inverseBinds[20] = -0.13f;
            input.inverseBinds[26] = 0.86f; input.inverseBinds[29] = 0.29f;
        }
        const char* nodes = mode == 0
            ? R"([{"mesh":0,"skin":0,"translation":[10,0,0]},{"translation":[11,0,0],"children":[2]},{"translation":[1,0,0]},{"mesh":0,"skin":0,"translation":[20,0,0]}])"
            : R"([{"mesh":0,"skin":0,"translation":[0.25,-1.5,2.75],"scale":[0.75,1.25,1.5],"rotation":[0,0,0.38268343,0.9238795]},{"translation":[1.125,0.375,-0.75],"rotation":[0.25881904,0,0,0.9659258],"scale":[1.25,0.875,1.125],"children":[2]},{"translation":[1.75,-0.25,0.625],"rotation":[0,0.17364818,0,0.9848077],"scale":[0.75,1.5,1.25]},{"mesh":0,"skin":0,"translation":[-3,4.5,-6],"rotation":[0,0.34202014,0,0.9396926],"scale":[1.75,0.875,1.125]}])";
        char json[4096];
        const int count = snprintf(json, sizeof(json), R"({"asset":{"version":"2.0"},"scene":0,"buffers":[{"uri":"fixture.bin","byteLength":368}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":48},{"buffer":0,"byteOffset":120,"byteLength":24},{"buffer":0,"byteOffset":144,"byteLength":24},{"buffer":0,"byteOffset":168,"byteLength":48},{"buffer":0,"byteOffset":216,"byteLength":128},{"buffer":0,"byteOffset":344,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":4,"componentType":5123,"count":3,"type":"VEC4"},{"bufferView":5,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":6,"componentType":5126,"count":2,"type":"MAT4"},{"bufferView":7,"componentType":5126,"count":3,"type":"VEC2"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TANGENT":2,"TEXCOORD_0":3,"TEXCOORD_1":7,"JOINTS_0":4,"WEIGHTS_0":5}}]}],"skins":[{"joints":[1,2],"inverseBindMatrices":6}],"nodes":%s,"scenes":[{"nodes":[0,1,3]}]})", nodes);
        Require(count > 0 && size_t(count) < sizeof(json), "fixed fixture text capacity");
        const ArrayView<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(&input), sizeof(input)};
        reference.Match(&mode, sizeof(mode));
        reference.Match(json, size_t(count)); reference.Match(bytes.data, bytes.count);
        ImportDocument document;
        Imported(document.Parse({reinterpret_cast<const uint8_t*>(json), size_t(count)}), "parse");
        Imported(document.SupplyBuffer(0, bytes), "supply");
        RendererScene scene;
        ImportGeometry geometry;
        ImportSceneOptions options; options.generation = 81; options.modelName = {"fixture.gltf", 12};
        Imported(ConvertImportScene(document, options, scene, geometry), "convert");
        document.Reset();
        const auto view = scene.View();
        Require(view.instances.count == 2 && view.meshes.count == 3, "candidate skin instance count");
        RendererSceneMesh expectedMeshes[2];
        RendererSceneBufferGroup expectedLayouts[2];
        for (uint32_t i = 0; i < 2; ++i)
        {
            expectedMeshes[i] = view.meshes.data[view.instances.data[i].meshIndex];
            expectedLayouts[i] = view.bufferGroups.data[expectedMeshes[i].bufferGroupIndex];
        }
        RendererSceneResourcesNvrhi candidateResources(device);
        if (mode == 0)
        {
            uint32_t failures = 0;
            for (uint32_t ordinal = 1; ordinal < 32; ++ordinal)
            {
                SetRendererUploadFailure(RendererUploadFailure::Creation, ordinal);
                const auto result = candidateResources.Prepare(view, geometry, {}, shader, health);
                SetRendererUploadFailure(RendererUploadFailure::None, 0);
                if (result) { candidateResources.Reset(); break; }
                Require(result.error == RendererUploadError::Gpu && candidateResources.Progress().phase == RendererUploadPhase::Empty &&
                    !candidateResources.Progress().cpuBorrows, "skin creation failure releases every partial resource");
                ++failures;
            }
            Require(failures == 12, "all skin buffers, layout, pipeline and binding-set failures covered");
            Require(candidateResources.Prepare(view, geometry, {}, nullptr, health).error == RendererUploadError::Input &&
                candidateResources.Progress().phase == RendererUploadPhase::Empty, "missing skin shader cannot prepare a candidate");
            printf("owned skin preparation: %u resource creation failures and missing shader recovered\n", failures);
        }
        Require(bool(candidateResources.Prepare(view, geometry, {}, shader, health)), "prepare owned skin resources");
        for (uint32_t group = 0; group < geometry.BufferCount(); ++group)
        {
            const auto source = geometry.Buffer(group);
            if (source.indices.count)
                Require(candidateResources.Buffer(group).indices == candidateResources.Buffer(source.indexOwner).indices,
                    "derived groups share the single canonical index allocation");
        }
        for (uint32_t step = 0; step < 1000 && candidateResources.Progress().phase != RendererUploadPhase::Submitted; ++step)
            Require(bool(candidateResources.Step(17, nullptr, health)), "bounded owned skin upload");
        Require(candidateResources.Progress().phase == RendererUploadPhase::Submitted && !candidateResources.Progress().cpuBorrows,
            "owned skin submission releases input borrows");
        if (mode == 0)
        {
            nvrhi::BindlessLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::All; layoutDesc.maxCapacity = 4;
            layoutDesc.registerSpaces = {nvrhi::BindingLayoutItem::RawBuffer_SRV(1)};
            auto tableLayout = device->createBindlessLayout(layoutDesc);
            Require(bool(tableLayout), "skin descriptor layout");
            uvsr::RendererSceneDescriptorsNvrhi descriptors(device, tableLayout);
            RendererSceneGpuTablesNvrhi tables(device);
            Require(candidateResources.BufferCount() == 3 && candidateResources.Buffer(0).indexOwner == 1 &&
                tables.Prepare(view, candidateResources, &descriptors).Succeeded() && !descriptors.GetLiveCount() &&
                tables.PrepareRayGeometry(view).Succeeded() && descriptors.GetLiveCount() == 4,
                "later index owner creates one shared slot and three distinct vertex slots");
            nvrhi::BufferDesc desc; desc.byteSize = view.geometries.count * sizeof(GeometryData); desc.cpuAccess = nvrhi::CpuAccessMode::Read;
            auto readback = device->createBuffer(desc); auto list = device->createCommandList();
            Require(readback && list && view.geometries.count == 3, "skin geometry table readback");
            list->open(); tables.BeginRecording();
            Require(tables.RecordGeometry(list), "skin geometry table recording");
            list->copyBuffer(readback, 0, tables.GeometryBuffer(), 0, desc.byteSize); list->close();
            Require(device->executeCommandList(list) != 0 && device->waitForIdle(), "skin table submission and retirement");
            tables.CommitRecording();
            const auto* mapped = device->mapBuffer(readback, nvrhi::CpuAccessMode::Read);
            Require(mapped != nullptr, "skin table map");
            GeometryData data[3]; memcpy(data, mapped, sizeof(data)); device->unmapBuffer(readback);
            const int32_t shared = data[0].indexBufferIndex;
            for (uint32_t meshIndex = 0; meshIndex < view.meshes.count; ++meshIndex)
            {
                const auto& mesh = view.meshes.data[meshIndex];
                const auto buffers = candidateResources.Buffer(mesh.bufferGroupIndex);
                const auto& entry = data[mesh.geometries.first];
                Require(entry.indexBufferIndex == shared &&
                    descriptors.GetDescriptor(entry.indexBufferIndex).resourceHandle == buffers.indices &&
                    descriptors.GetDescriptor(entry.vertexBufferIndex).resourceHandle == buffers.vertices,
                    "skin table aliases the canonical index slot and retains each output vertex buffer");
            }
            tables.Reset(); Require(!descriptors.GetLiveCount() && health.check(health.context), "shared skin descriptor releases once");
            printf("imported skin bindings: later index owner, exact four-slot capacity and alias retirement passed\n");
        }
        const auto jointNode = view.joints.data[view.instances.data[0].joints.first].nodeIndex;
        auto movedJoint = view.nodes.data[jointNode].transform;
        movedJoint.translation[0] += 100;
        Require(scene.SetTransform({view.generation, jointNode}, movedJoint).changed, "joint edit after static skin initialization");
        const auto submissions = candidateResources.Progress().submissions;
        Require(bool(candidateResources.Step(17, nullptr, health)) && bool(candidateResources.Step(17, nullptr, health)) &&
            candidateResources.Progress().submissions == submissions && !candidateResources.Progress().cpuBorrows,
            "completed static skin upload does not dispatch again after joint edits");
        scene.Reset();

        auto commands = device->createCommandList();
        Require(bool(commands), "skin command list");
        for (uint32_t i = 0; i < 2; ++i)
        {
            const auto& mesh = expectedMeshes[i];
            const auto derived = geometry.Buffer(mesh.bufferGroupIndex);
            const auto& layout = expectedLayouts[i];
            Require(derived.vertices.count == 0 && derived.jointMatrices.count == 2 && derived.skinInstanceIndex == i &&
                layout.vertexBytes <= 512, "owned candidate palette and output layout");
            auto* candidateOutput = candidateResources.Buffer(mesh.bufferGroupIndex).vertices;
            Require(candidateOutput && candidateOutput->getDesc().byteSize == layout.vertexBytes, "owned skin buffer allocation");
            const uint32_t paletteKey[]{mode, i}; reference.Match(paletteKey, sizeof(paletteKey));
            reference.Match(derived.jointMatrices.data, derived.jointMatrices.count * sizeof(gpu_contract::Float4x4));
            nvrhi::BufferDesc readbackDesc;
            readbackDesc.byteSize = layout.vertexBytes;
            readbackDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
            readbackDesc.initialState = nvrhi::ResourceStates::CopyDest;
            auto readback = device->createBuffer(readbackDesc);
            Require(bool(readback), "readback allocation");
            commands->open(); commands->copyBuffer(readback, 0, candidateOutput, 0, layout.vertexBytes); commands->close();
            Require(device->executeCommandList(commands) != 0 && device->waitForIdle(), "readback completion before resource release");
            const auto* mapped = static_cast<const uint8_t*>(device->mapBuffer(readback, nvrhi::CpuAccessMode::Read));
            Require(mapped != nullptr, "readback map");
            uint8_t candidateBytes[512]{}; memcpy(candidateBytes, mapped, size_t(layout.vertexBytes));
            device->unmapBuffer(readback);
            const ArrayView<const uint8_t> candidate{candidateBytes, size_t(layout.vertexBytes)};
            struct Range { Attribute candidate; size_t stride; };
            const Range ranges[]{
                {Attribute::Position,12}, {Attribute::PreviousPosition,12}, {Attribute::Normal,4},
                {Attribute::Tangent,4}, {Attribute::TexCoord0,8}, {Attribute::TexCoord1,8}};
            for (const auto& range : ranges)
            {
                const auto actual = layout.attributes[uint32_t(range.candidate)];
                const size_t size = 3 * range.stride;
                Require(actual.offset <= candidate.count && size <= candidate.count - actual.offset, "compared range excludes alignment padding");
                const uint32_t key[]{mode, i, uint32_t(range.candidate)}; reference.Match(key, sizeof(key));
                uint8_t expected[36]; reference.Read(expected, size);
                const bool transformedFloat = range.candidate == Attribute::Position ||
                    range.candidate == Attribute::PreviousPosition || range.candidate == Attribute::TexCoord1;
                // The retained control's TexCoord1 range aliases transformed storage.
                // Preserve that historical range while allowing only cross-adapter float rounding.
                if (!Compare(expected, candidate.data + actual.offset, size, mode, i,
                    range.candidate, transformedFloat, boundedDifference)) equal = false;
                ++comparisons;
            }
        }
        Require(device->waitForIdle(), "skin fixture retirement");
        geometry.Reset();
        Require(bool(candidateResources.PollCompletion(health)) && candidateResources.Progress().phase == RendererUploadPhase::Complete,
            "skin completion after geometry owner destruction");
        device->runGarbageCollection();
    }
    reference.Finish(93);
    puts("static skin: joint edits and repeated completed upload steps preserve all captured output ranges");
    printf("initial skin GPU comparison: %u attribute ranges, result %s\n", comparisons,
        equal ? (boundedDifference ? "within 4 ULP" : "exact") : "mismatch");
    Require(equal, "converted buffers and joint matrices preserve retained GPU initialization");
}
