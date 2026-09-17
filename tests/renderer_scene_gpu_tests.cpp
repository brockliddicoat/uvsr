#include "renderer_scene_descriptors_nvrhi.h"
#include "renderer_scene_gpu_nvrhi.h"
#include "renderer_import_scene.h"
#include "renderer_scene_resources_nvrhi.h"
#include "world_space_representation_nvrhi.h"
#include <filesystem>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene GPU tests require exception-disabled compilation
#endif

namespace
{
    using namespace uvsr;

    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "scene GPU check failed: %s\n", reason);
        exit(1);
    }

    nvrhi::CommandListHandle Commands(nvrhi::IDevice* device)
    {
        nvrhi::CommandListParameters params;
        params.enableImmediateExecution = false;
        auto commands = device->createCommandList(params);
        Require(bool(commands), "command allocation");
        commands->open();
        return commands;
    }

    void ReadBuffer(nvrhi::IDevice* device, nvrhi::IBuffer* source, void* output, size_t bytes)
    {
        nvrhi::BufferDesc desc;
        desc.byteSize = bytes;
        desc.cpuAccess = nvrhi::CpuAccessMode::Read;
        desc.initialState = nvrhi::ResourceStates::CopyDest;
        auto staging = device->createBuffer(desc);
        Require(bool(staging), "readback allocation");
        auto commands = Commands(device);
        commands->copyBuffer(staging, 0, source, 0, bytes);
        commands->close();
        Require(device->executeCommandList(commands) != 0 && device->waitForIdle(), "readback completion");
        const void* mapped = device->mapBuffer(staging, nvrhi::CpuAccessMode::Read);
        Require(mapped != nullptr, "readback map");
        memcpy(output, mapped, bytes);
        device->unmapBuffer(staging);
    }

    nvrhi::ShaderHandle ReadProbe(nvrhi::IDevice* device, const std::filesystem::path& path)
    {
        FILE* file = nullptr;
        Require(_wfopen_s(&file, path.c_str(), L"rb") == 0 && file, "instance probe open");
        Require(_fseeki64(file, 0, SEEK_END) == 0, "instance probe seek");
        const auto bytes = _ftelli64(file);
        Require(bytes > 0 && uint64_t(bytes) <= uint64_t(PTRDIFF_MAX) && _fseeki64(file, 0, SEEK_SET) == 0,
            "instance probe size");
        auto* data = new (std::nothrow) unsigned char[size_t(bytes)];
        Require(data && fread(data, 1, size_t(bytes), file) == size_t(bytes), "instance probe read");
        fclose(file);
        nvrhi::ShaderDesc desc;
        desc.shaderType = nvrhi::ShaderType::Compute;
        desc.debugName = "test canonical instance and TLAS agreement";
        auto shader = device->createShader(desc, data, size_t(bytes));
        delete[] data;
        Require(bool(shader), "instance probe shader");
        return shader;
    }

    void ProbeInstances(nvrhi::IDevice* device, nvrhi::IShader* shader, WorldSpaceRepresentation& world,
        const RendererSceneView& scene, const RendererSceneGpuTablesNvrhi& tables, float expectedMotion)
    {
        const auto ray = world.GetRaySceneView(scene, tables);
        Require(bool(ray) && ray.sceneGeneration == scene.generation, "canonical ray publication identifies its CPU scene");
        nvrhi::BufferDesc outputDesc;
        outputDesc.byteSize = sizeof(uint32_t) * 8;
        outputDesc.structStride = sizeof(uint32_t) * 4;
        outputDesc.canHaveUAVs = true;
        outputDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        outputDesc.keepInitialState = true;
        auto output = device->createBuffer(outputDesc);
        Require(bool(output), "instance probe output");
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1), nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3), nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0)};
        auto layout = device->createBindingLayout(layoutDesc);
        Require(bool(layout), "instance probe layout");
        nvrhi::BindingSetDesc desc;
        desc.bindings = {nvrhi::BindingSetItem::RayTracingAccelStruct(0, ray.tlas),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, tables.InstanceBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, ray.geometryIndexMap),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, ray.geometryBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(4, ray.materialBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, output)};
        auto bindings = device->createBindingSet(desc, layout);
        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = shader;
        pipelineDesc.bindingLayouts = {layout};
        auto pipeline = device->createComputePipeline(pipelineDesc);
        Require(bindings && pipeline, "instance probe bindings and pipeline");
        auto commands = Commands(device);
        nvrhi::ComputeState state;
        state.pipeline = pipeline;
        state.bindings = {bindings};
        commands->setComputeState(state);
        commands->dispatch(1);
        commands->close();
        Require(device->executeCommandList(commands) != 0, "instance probe submission");
        uint32_t results[2][4]{};
        ReadBuffer(device, output, results, sizeof(results));
        for (uint32_t index = 0; index < 2; ++index)
        {
            float distance = 0, motion = 0;
            memcpy(&distance, &results[index][2], sizeof(float));
            memcpy(&motion, &results[index][3], sizeof(float));
            const uint32_t material = scene.geometries.data[scene.meshes.data[scene.instances.data[index].meshIndex].geometries.first].materialIndex;
            Require(results[index][0] == index && results[index][1] == scene.materials.data[material].selectionId &&
                distance == 10.f && motion == (index ? expectedMotion : 0.f),
                "GPU rays agree with instance stream transforms, temporal motion, IDs and canonical materials");
        }
    }

    void RayGeometry(RendererScene& scene, ImportGeometry& geometry, uint64_t generation)
    {
        const float positions[]{0,0,0, 1,0,0, 0,1,0, 3,0,0, 4,0,0, 3,1,0};
        const char json[] = R"({"asset":{"version":"2.0"},"scene":0,"buffers":[{"uri":"triangles.bin","byteLength":72}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"}],"materials":[{"pbrMetallicRoughness":{"roughnessFactor":0.25}},{"pbrMetallicRoughness":{"roughnessFactor":0.5}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]},{"primitives":[{"attributes":{"POSITION":1},"material":1}]}],"nodes":[{"mesh":0},{"mesh":1}],"scenes":[{"nodes":[0,1]}]})";
        ImportDocument document;
        Require(bool(document.Parse({reinterpret_cast<const uint8_t*>(json), sizeof(json) - 1})) &&
            bool(document.SupplyBuffer(0, {reinterpret_cast<const uint8_t*>(positions), sizeof(positions)})), "ray fixture parse");
        ImportSceneOptions options; options.generation = generation;
        Require(bool(ConvertImportScene(document, options, scene, geometry)), "ray fixture conversion"); document.Reset();
    }

    struct TableInput
    {
        RendererSceneNode nodes[5];
        RendererSceneMaterial materials[2];
    };

    void TableFixture(RendererScene& scene, ImportGeometry& geometry, TableInput& input)
    {
        RendererScene imported;
        RayGeometry(imported, geometry, 101);
        const auto source = imported.View();
        Require(source.meshes.count == 2 && source.geometries.count == 2 && source.bufferGroups.count == 1,
            "table fixture has two meshes sharing one upload allocation");
        const uint32_t indices[]{0, 1, 2, 0, 1, 2};
        const float positions[]{0,0,0, 1,0,0, 0,1,0, 3,0,0, 4,0,0, 3,1,0};
        const auto bytes = geometry.Buffer(0);
        const auto position = source.bufferGroups.data[0].attributes[uint32_t(RendererSceneVertexAttribute::Position)];
        Require(bytes.indices.count == sizeof(indices) && !memcmp(bytes.indices.data, indices, sizeof(indices)) &&
            position.offset == 0 && position.size >= sizeof(positions) && !memcmp(bytes.vertices.data, positions, sizeof(positions)),
            "original triangle indices and packed position ranges");
        RendererSceneCounts counts;
        counts.nodes = 5; counts.meshes = counts.geometries = counts.instances = counts.materials = 2;
        counts.bufferGroups = counts.textures = counts.lights = 1;
        Require(scene.Prepare(counts).Succeeded(), "table fixture allocation");
        input.nodes[0].firstChildIndex = 1;
        input.nodes[1].parentIndex = 0; input.nodes[1].firstChildIndex = 2; input.nodes[1].nextSiblingIndex = 3;
        input.nodes[2].parentIndex = 1; input.nodes[2].leafKind = RendererSceneLeafKind::Instance; input.nodes[2].leafIndex = 1;
        input.nodes[3].parentIndex = 0; input.nodes[3].nextSiblingIndex = 4;
        input.nodes[3].leafKind = RendererSceneLeafKind::Light; input.nodes[3].leafIndex = 0;
        input.nodes[4].parentIndex = 0; input.nodes[4].leafKind = RendererSceneLeafKind::Instance; input.nodes[4].leafIndex = 0;
        for (uint32_t index = 0; index < 5; ++index)
            Require(scene.Write(index, input.nodes[index]).Succeeded(), "table fixture node copy");
        for (uint32_t index = 0; index < 2; ++index)
        {
            auto& material = input.materials[index];
            material.selectionId = index + 7;
            material.values.roughness = float(index + 1) * 0.25f;
            material.values.textures[uint32_t(RendererSceneMaterialTextureSlot::BaseOrDiffuse)] = 0;
            material.originalValues = material.values;
            Require(scene.Write(1 - index, material).Succeeded(), "canonical material order differs from its inputs");
            auto mesh = source.meshes.data[1 - index];
            auto part = source.geometries.data[mesh.geometries.first];
            mesh.name = {}; mesh.geometries = {index, 1}; part.materialIndex = index;
            Require(scene.Write(index, mesh).Succeeded() && scene.Write(index, part).Succeeded(), "canonical mesh/geometry copy");
            RendererSceneInstance instance;
            instance.nodeIndex = index == 0 ? 4 : 2; instance.meshIndex = 1 - index;
            Require(scene.Write(index, instance).Succeeded(), "instance order differs from mesh order");
        }
        RendererSceneLight light; light.nodeIndex = 3; light.kind = RendererSceneLightKind::Point;
        Require(scene.Write(0, source.bufferGroups.data[0]).Succeeded() && scene.Write(0, RendererSceneTexture{}).Succeeded() &&
            scene.Write(0, light).Succeeded(), "canonical shared buffer, texture and light");
        uint8_t workspace[5];
        Require(scene.SealWorkspaceBytes() <= sizeof(workspace) && scene.Seal(0, {workspace, sizeof(workspace)}).Succeeded() &&
            scene.Publish(101).Succeeded(), "table fixture publication");
    }

    void ImportedRayTables(nvrhi::IDevice* device, nvrhi::IShader* probe, RendererUploadHealth health)
    {
        RendererScene scene; ImportGeometry geometry; RayGeometry(scene, geometry, 211);
        auto view = scene.View();
        Require(view.instances.count == 2 && view.materials.count == 2 && view.meshes.count == 2, "imported ray fixture counts");
        RendererSceneResourcesNvrhi resources(device);
        RendererUploadOptions upload; upload.rayTracing = true;
        Require(bool(resources.Prepare(view, geometry, {}, nullptr, health, upload)), "ray fixture resources");
        for (uint32_t step = 0; step < 128 && resources.Progress().phase != RendererUploadPhase::Submitted; ++step)
            Require(bool(resources.Step(17, nullptr, health)), "ray fixture upload step");
        Require(resources.Progress().phase == RendererUploadPhase::Submitted && !resources.Progress().cpuBorrows,
            "ray fixture resources are submitted"); geometry.Reset();
        nvrhi::BindlessLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::All; layoutDesc.maxCapacity = 2;
        layoutDesc.registerSpaces = {nvrhi::BindingLayoutItem::RawBuffer_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2)};
        auto layout = device->createBindlessLayout(layoutDesc);
        Require(bool(layout), "ray fixture descriptor layout");
        uvsr::RendererSceneDescriptorsNvrhi descriptors(device, layout);
        RendererSceneGpuTablesNvrhi tables(device);
        Require(tables.Prepare(view, resources, &descriptors).Succeeded() && !descriptors.GetLiveCount(),
            "inactive imported ray geometry has no raw descriptors");
        WorldSpaceRepresentation world(device);
        const auto update = [&]()
        {
            auto commands = Commands(device); tables.BeginRecording();
            Require(tables.RecordMaterials(commands, view).Succeeded() && tables.RecordInstances(commands, view).Succeeded(),
                "imported ray material and instance recording");
            const bool ready = world.Update(commands, view, tables, {}, true);
            Require(world.GetStatus().state != WorldSpaceRepresentationState::Failed, "imported world preparation");
            commands->close();
            Require(device->executeCommandList(commands) != 0 && device->waitForIdle(), "imported ray submission and scratch retirement");
            tables.CommitRecording(); return ready;
        };
        for (uint32_t step = 0; !world.IsReady() && step < 4; ++step) (void)update();
        Require(world.IsReady() && descriptors.GetLiveCount() == 2, "imported BLAS and TLAS use exact lazy slots");
        resources.Reset();
        ProbeInstances(device, probe, world, view, tables, 0);
        const auto node = view.instances.data[1].nodeIndex;
        auto transform = view.nodes.data[node].transform;
        transform.translation[0] = 3; transform.translation[1] = 5; transform.translation[2] = 7;
        Require(scene.SetTransform({view.generation, node}, transform).changed, "imported instance motion");
        view = scene.View(); Require(update(), "imported TLAS motion refit");
        ProbeInstances(device, probe, world, view, tables, 3);
        Require(scene.AdvancePreviousTransforms().changed, "imported motion submission snapshot");
        view = scene.View(); const auto revision = world.GetStatus().contentRevision;
        Require(update() && world.GetStatus().contentRevision == revision, "imported previous-only upload leaves TLAS unchanged");
        ProbeInstances(device, probe, world, view, tables, 0);
        world.Reset(); tables.Reset();
        Require(!descriptors.GetLiveCount() && health.check(health.context), "imported ray tables retire without backend errors");
        printf("imported ray tables: actual ray hits, material IDs, motion and stationary recovery passed\n");
    }
}

void TestRendererSceneGpuTables(nvrhi::IDevice* device, const std::filesystem::path& instanceProbe,
    uvsr::RendererUploadHealth health)
{
    using namespace uvsr;
    nvrhi::BindlessLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.maxCapacity = 65536;
    layoutDesc.registerSpaces = {nvrhi::BindingLayoutItem::RawBuffer_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2)};
    auto layout = device->createBindlessLayout(layoutDesc);
    Require(bool(layout), "descriptor layout");
    RendererSceneDescriptorsNvrhi descriptors(device, layout);
    Require(descriptors.IsValid(), "descriptor owner");
    TableInput input;
    RendererScene scene; ImportGeometry geometry;
    TableFixture(scene, geometry, input);
    auto view = scene.View();
    const uint32_t branch = 1, lightNode = 3;
    input.nodes[branch].transform.translation[0] = 100;
    input.nodes[branch].transform.translation[1] = 200;
    input.nodes[branch].transform.translation[2] = 300;
    const uint32_t firstSelectionId = view.materials.data[0].selectionId;
    const uint32_t secondSelectionId = view.materials.data[1].selectionId;
    input.materials[0].selectionId = 1234; input.materials[1].selectionId = 5678;
    const uint8_t tga[]{0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,32,40,20,40,60,255};
    ImportDecodedImage image;
    Require(bool(image.Decode({{"fixture.tga", 11}, {}, {tga, sizeof(tga)}, true}, {})), "one-pixel table texture");
    RendererSceneResourcesNvrhi resources(device);
    RendererUploadOptions options; options.rayTracing = true; options.generateMips = false;
    Require(bool(resources.Prepare(view, geometry, {&image, 1}, nullptr, health, options)), "table fixture upload preparation");
    for (uint32_t step = 0; step < 128 && resources.Progress().phase != RendererUploadPhase::Submitted; ++step)
        Require(bool(resources.Step(17, nullptr, health)), "table fixture upload");
    Require(resources.Progress().phase == RendererUploadPhase::Submitted && !resources.Progress().cpuBorrows,
        "table fixture submission releases CPU borrows");
    geometry.Reset(); image.Reset();
    RendererSceneGpuTablesNvrhi tables(device);
    Require(tables.Prepare(view, resources, &descriptors).Succeeded(), "table preparation");
    Require(!tables.MaterialsReady(view) && !tables.GeometryReady(view.generation) && !tables.InstancesReady(view),
        "allocation is not upload readiness");
    Require(tables.Prepare(view, resources, &descriptors).error == RendererSceneError::InvalidState,
        "occupied table owner is replaced only through a separate candidate");
    uint32_t failures = 0;
    for (uint32_t ordinal = 1; ordinal < 16; ++ordinal)
    {
        auto* previous = tables.MaterialBuffer();
        RendererSceneDescriptorsNvrhi candidateDescriptors(device, layout);
        RendererSceneGpuTablesNvrhi candidate(device);
        SetRendererSceneGpuAllocationFailure(ordinal);
        const auto result = candidate.Prepare(view, resources, &candidateDescriptors);
        SetRendererSceneGpuAllocationFailure(0);
        if (result.Succeeded()) break;
        Require(result.error == RendererSceneError::Allocation && !candidate.MaterialBuffer() && !candidate.Generation() &&
            !candidateDescriptors.GetLiveCount() && tables.MaterialBuffer() == previous &&
            tables.Generation() == view.generation && descriptors.GetLiveCount() == 1,
            "failed replacement candidate preserves usable table ownership and slots");
        ++failures;
    }
    Require(failures == 6, "all six table allocation failure sites exercised");
    SetRendererSceneGpuAllocationFailure(1);
    Require(tables.PrepareRayGeometry(view).error == RendererSceneError::Allocation &&
        tables.GeometryBuffer() == nullptr, "geometry scratch failure is atomic");
    SetRendererSceneGpuAllocationFailure(0);
    Require(tables.PrepareRayGeometry(view).Succeeded() && !tables.GeometryReady(view.generation), "geometry preparation");
    auto record = [&]()
    {
        auto commands = Commands(device);
        tables.BeginRecording();
        Require(tables.RecordMaterials(commands, view).Succeeded() && tables.RecordInstances(commands, view).Succeeded() &&
            tables.RecordGeometry(commands), "canonical upload recording");
        Require(tables.MaterialsReady(view) && tables.GeometryReady(view.generation) && tables.InstancesReady(view), "same-list readiness");
        commands->close();
        return commands;
    };
    auto abandoned = record();
    abandoned = nullptr;
    tables.BeginRecording();
    Require(!tables.MaterialsReady(view) && !tables.GeometryReady(view.generation) && !tables.MaterialRevision() && !tables.InstancesReady(view),
        "abandoned recording does not commit upload readiness");
    auto submitted = record();
    Require(device->executeCommandList(submitted) != 0, "table submission");
    tables.CommitRecording();
    Require(tables.MaterialsReady(view) && tables.GeometryReady(view.generation) &&
        tables.MaterialRevision() == view.materialRevision, "successful submission commits revisions");
    RendererMaterialTableEntry materialData[2]{};
    GeometryData geometryData[2]{};
    InstanceData instanceData[2]{};
    ReadBuffer(device, tables.MaterialBuffer(), materialData, sizeof(materialData));
    ReadBuffer(device, tables.GeometryBuffer(), geometryData, sizeof(geometryData));
    ReadBuffer(device, tables.InstanceBuffer(), instanceData, sizeof(instanceData));
    Require(tables.InstanceBuffer()->getDesc().byteSize == sizeof(instanceData) &&
        tables.InstanceBuffer()->getDesc().structStride == sizeof(InstanceData) &&
        instanceData[0].firstGeometryIndex == 1 && instanceData[1].firstGeometryIndex == 0 &&
        instanceData[0].firstGeometryInstanceIndex == 0 && instanceData[1].firstGeometryInstanceIndex == 1 &&
        instanceData[1].transform.values[3] == 0 && instanceData[1].transform.values[7] == 0 &&
        memcmp(&instanceData[1].transform, &instanceData[1].prevTransform, sizeof(instanceData[1].transform)) == 0,
        "exact canonical instance allocation, geometry prefix and transforms ignore divergent input copies");
    Require(materialData[0].material.roughness == 0.5f && materialData[1].material.roughness == 0.25f &&
        uint32_t(materialData[0].material.materialID) == firstSelectionId &&
        uint32_t(materialData[1].material.materialID) == secondSelectionId &&
        materialData[0].material.baseOrDiffuseTextureIndex == 0,
        "GPU material bytes preserve canonical order, immutable selection identity and descriptors");
    Require(descriptors.GetDescriptor(0).resourceHandle == resources.Texture(0), "material slot identifies the owned texture");
    Require(geometryData[0].materialIndex == 0 && geometryData[1].materialIndex == 1 &&
        geometryData[0].indexOffset == 12 && geometryData[1].indexOffset == 0 &&
        geometryData[0].positionOffset == 36 && geometryData[1].positionOffset == 0,
        "GPU geometry uses canonical material and geometry order");
    nvrhi::IBuffer* constantBuffer = nullptr;
    nvrhi::BufferRange range;
    Require(tables.GetMaterialBinding(1, constantBuffer, range) && constantBuffer == tables.MaterialBuffer() &&
        range.byteOffset == 256 && range.byteSize == 256 && constantBuffer->getDesc().structStride == 256 &&
        constantBuffer->getDesc().byteSize == 512, "raster and ray share one aligned material allocation");
    nvrhi::BindingLayoutDesc materialLayout;
    materialLayout.visibility = nvrhi::ShaderType::Compute;
    materialLayout.bindings = {nvrhi::BindingLayoutItem::ConstantBuffer(0), nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0)};
    auto bindingLayout = device->createBindingLayout(materialLayout);
    nvrhi::BindingSetDesc bindings;
    bindings.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, constantBuffer, range),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, constantBuffer)};
    auto bindingSet = device->createBindingSet(bindings, bindingLayout);
    Require(bool(bindingSet), "shared CBV and SRV validation");

    auto changed = view.materials.data[0].values;
    changed.roughness = 0.625f;
    Require(scene.SetMaterial({view.generation, 0}, changed).Succeeded(), "canonical edit");
    view = scene.View();
    Require(!tables.MaterialsReady(view), "new revision requires upload");
    auto edited = record();
    Require(device->executeCommandList(edited) != 0, "edited table submission");
    tables.CommitRecording();
    ReadBuffer(device, tables.MaterialBuffer(), materialData, sizeof(materialData));
    Require(materialData[0].material.roughness == 0.625f && materialData[1].material.roughness == 0.25f &&
        input.materials[1].values.roughness == 0.5f, "GPU updates from canonical values independently of preparation copies");
    WorldSpaceRepresentation world(device);
    Require(world.IsSupported(), "instance fixture requires the retained hardware ray-query capability");
    const auto probe = ReadProbe(device, instanceProbe);
    const auto updateWorld = [&]()
    {
        auto commands = record();
        Require(device->executeCommandList(commands) != 0, "instance table preparation submission");
        tables.CommitRecording();
        commands = Commands(device);
        tables.BeginRecording();
        const bool ready = world.Update(commands, view, tables, {}, true);
        Require(world.GetStatus().state != WorldSpaceRepresentationState::Failed, "canonical world preparation");
        commands->close();
        // this fixture creates short-lived lists. their DXR scratch storage must
        // survive submission through completion, as the persistent frame list does.
        Require(device->executeCommandList(commands) != 0 && device->waitForIdle(), "canonical world submission and retirement");
        tables.CommitRecording();
        return ready;
    };
    for (uint32_t step = 0; !world.IsReady() && step < 4; ++step) (void)updateWorld();
    Require(world.IsReady(), "staged canonical BLAS/TLAS completion");
    ProbeInstances(device, probe, world, view, tables, 0);
    auto transform = scene.View().nodes.data[branch].transform;
    transform.translation[0] = 3; transform.translation[1] = 5; transform.translation[2] = 7;
    Require(scene.SetTransform({view.generation, branch}, transform).changed, "canonical parent motion");
    view = scene.View();
    Require(!tables.InstancesReady(view), "motion requires current/previous upload");
    abandoned = record();
    abandoned = nullptr;
    tables.BeginRecording();
    Require(!tables.InstancesReady(view), "abandoned motion upload remains pending");
    ReadBuffer(device, tables.InstanceBuffer(), instanceData, sizeof(instanceData));
    Require(instanceData[1].transform.values[3] == 0, "abandoned recording did not mutate the GPU table");
    auto moving = record();
    Require(device->executeCommandList(moving) != 0, "moving frame submission");
    tables.CommitRecording();
    Require(updateWorld(), "canonical transform refit");
    ProbeInstances(device, probe, world, view, tables, 3);
    ReadBuffer(device, tables.InstanceBuffer(), instanceData, sizeof(instanceData));
    Require(instanceData[0].transform.values[3] == 0 && instanceData[1].transform.values[3] == 3 &&
        instanceData[1].transform.values[7] == 5 && instanceData[1].transform.values[11] == 7 &&
        instanceData[1].prevTransform.values[3] == 0 && instanceData[1].prevTransform.values[7] == 0,
        "moving frame uploads canonical current and last submitted previous worlds");
    Require(scene.AdvancePreviousTransforms().changed, "submitted motion snapshot");
    view = scene.View();
    Require(!tables.InstancesReady(view), "the first stationary frame requires a previous-world upload");
    auto stopped = record();
    Require(device->executeCommandList(stopped) != 0, "stationary frame submission");
    tables.CommitRecording();
    const auto settledWorldRevision = world.GetStatus().contentRevision;
    Require(updateWorld() && world.GetStatus().contentRevision == settledWorldRevision, "previous-only changes do not refit the TLAS");
    ProbeInstances(device, probe, world, view, tables, 0);
    ReadBuffer(device, tables.InstanceBuffer(), instanceData, sizeof(instanceData));
    Require(memcmp(&instanceData[1].transform, &instanceData[1].prevTransform, sizeof(instanceData[1].transform)) == 0 &&
        !scene.AdvancePreviousTransforms().changed && tables.InstancesReady(scene.View()),
        "stationary motion settles and later frames retain ready instance data");
    const auto instanceRevision = scene.View().instanceTransformRevision;
    auto lightTransform = scene.View().nodes.data[lightNode].transform;
    lightTransform.translation[0] = 29;
    Require(scene.SetTransform({view.generation, lightNode}, lightTransform).changed &&
        scene.View().instanceTransformRevision == instanceRevision && tables.InstancesReady(scene.View()) &&
        scene.AdvancePreviousTransforms().changed && tables.InstancesReady(scene.View()),
        "light-only motion leaves both instance upload revisions ready");
    view = scene.View();
    Require(updateWorld() && world.GetStatus().contentRevision == settledWorldRevision, "light-only motion does not refit the TLAS");
    ProbeInstances(device, probe, world, view, tables, 0);
    auto stale = view; ++stale.generation;
    RendererSceneGpuTablesNvrhi staleCandidate(device);
    Require(!tables.MaterialsReady(stale) && !tables.InstancesReady(stale) &&
        staleCandidate.Prepare(stale, resources, &descriptors).error == RendererSceneError::Generation,
        "stale generation rejected");
    auto malformed = view; malformed.materials.data = nullptr;
    auto invalid = Commands(device);
    tables.BeginRecording();
    Require(tables.RecordMaterials(invalid, malformed).error == RendererSceneError::Reference && tables.MaterialsReady(view),
        "invalid view leaves the committed table usable");
    malformed = view; malformed.instances.data = nullptr;
    Require(tables.RecordInstances(invalid, malformed).error == RendererSceneError::Reference &&
        tables.RecordInstances(invalid, stale).error == RendererSceneError::Generation && tables.InstancesReady(view),
        "malformed or stale instance upload leaves the committed table usable");
    invalid->close(); invalid = nullptr;
    Require(device->waitForIdle(), "table retirement");
    world.Reset();
    tables.Reset();
    Require(!tables.MaterialBuffer() && !tables.GeometryBuffer() && !tables.InstanceBuffer() && !tables.InstancesReady(view) &&
        !tables.MaterialsReady(view) && !tables.GeometryReady(view.generation),
        "reset invalidates buffer borrows");
    Require(!descriptors.GetLiveCount(), "table reset releases all owned slots");
    printf("scene GPU tables: %u allocation failures, aborted/submitted uploads, canonical GPU bytes and shared CBV/SRV passed\n", failures);
    ImportedRayTables(device, probe, health);
}
