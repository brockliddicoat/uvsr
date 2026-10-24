#include "world_space_representation_nvrhi.h"
#include "renderer_log.h"
#include "renderer_scene_gpu_nvrhi.h"

#include <cstring>
#include <cstdlib>
#include <new>
#include <type_traits>

namespace uvsr
{
    namespace
    {
        // reserve only empty generations. preparation constructs the live
        // prefix, so no native description or handle needs relocation.
        template<class T>
        bool ReserveGenerationArray(T*& values, size_t& capacity, size_t count) noexcept
        {
            static_assert(alignof(T) <= alignof(std::max_align_t));
            if (count <= capacity) return true;
            if (count > size_t(PTRDIFF_MAX) / sizeof(T)) return false;
            T* candidate = static_cast<T*>(std::malloc(count * sizeof(T)));
            if (!candidate) return false;
            std::free(values);
            values = candidate;
            capacity = count;
            return true;
        }

        template<class T>
        void ClearGenerationArray(T* values, size_t& count) noexcept
        {
            static_assert(std::is_nothrow_destructible_v<T>);
            for (size_t index = 0; index < count; ++index)
                values[index].~T();
            count = 0;
        }

        bool BuildMeshDescription(
            const RendererSceneView& scene,
            const RendererSceneRayView& selection,
            const RendererSceneRayMesh& selected,
            const RendererSceneGpuTablesNvrhi& tables,
            nvrhi::rt::AccelStructDesc& description,
            ArrayView<uint32_t> geometryIndices)
        {
            if (!geometryIndices.IsValid() || selected.geometries.first > geometryIndices.count ||
                selected.geometries.count > geometryIndices.count - selected.geometries.first)
                return false;
            const auto& mesh = scene.meshes.data[selected.sceneMesh];
            const auto& group = scene.bufferGroups.data[mesh.bufferGroupIndex];
            const auto positions = group.attributes[uint32_t(RendererSceneVertexAttribute::Position)];
            nvrhi::IBuffer* indices = nullptr;
            nvrhi::IBuffer* vertices = nullptr;
            if (!tables.GetBuffers(mesh.bufferGroupIndex, indices, vertices) || !indices || !vertices ||
                indices->getDesc().byteSize != group.indexBytes || vertices->getDesc().byteSize != group.vertexBytes ||
                !indices->getDesc().isAccelStructBuildInput || !vertices->getDesc().isAccelStructBuildInput)
                return false;

            description = nvrhi::rt::AccelStructDesc();
            description.isTopLevel = false;
            description.debugName = "UVSR BLAS: ";
            if (mesh.name.length)
                description.debugName.append(scene.strings.data + mesh.name.offset, mesh.name.length);
            description.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;
            description.bottomLevelGeometries.reserve(selected.geometries.count);
            for (uint32_t local = 0; local < selected.geometries.count; ++local)
            {
                const uint32_t selectedIndex = selected.geometries.first + local;
                const auto& entry = selection.geometries.data[selectedIndex];
                const auto& geometry = scene.geometries.data[entry.sceneGeometry];
                nvrhi::rt::GeometryTriangles triangles;
                triangles.indexBuffer = indices;
                triangles.indexFormat = nvrhi::Format::R32_UINT;
                triangles.indexOffset = (uint64_t(mesh.indexOffset) + geometry.indexOffsetInMesh) * sizeof(uint32_t);
                triangles.indexCount = geometry.indexCount;
                triangles.vertexBuffer = vertices;
                triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
                triangles.vertexOffset = positions.offset +
                    (uint64_t(mesh.vertexOffset) + geometry.vertexOffsetInMesh) * sizeof(gpu_contract::Float3);
                triangles.vertexCount = geometry.vertexCount;
                triangles.vertexStride = sizeof(gpu_contract::Float3);
                description.addBottomLevelGeometry(nvrhi::rt::GeometryDesc().setTriangles(triangles).setFlags(
                    entry.opaque ? nvrhi::rt::GeometryFlags::Opaque : nvrhi::rt::GeometryFlags::None));
                geometryIndices.data[selectedIndex] = entry.sceneGeometry;
            }
            return !description.bottomLevelGeometries.empty();
        }

        bool ReadCanonicalInstance(const RendererSceneView& scene,
            uint32_t instanceIndex, nvrhi::rt::AffineTransform& transform)
        {
            if (!scene.generation || !scene.instances.IsValid() || !scene.nodes.IsValid() ||
                instanceIndex >= scene.instances.count || instanceIndex > 0x00ffffffu)
                return false; // DXR InstanceID is 24 bits.
            const auto& instance = scene.instances.data[instanceIndex];
            if (instance.nodeIndex >= scene.nodes.count)
                return false;
            const auto& node = scene.nodes.data[instance.nodeIndex];
            if (node.leafKind != RendererSceneLeafKind::Instance || node.leafIndex != instanceIndex)
                return false;
            gpu_contract::Float3x4 encoded;
            if (!EncodeRendererSceneAffine(node.world, encoded).Succeeded()) return false;
            static_assert(sizeof(encoded) == sizeof(transform));
            std::memcpy(&transform, &encoded, sizeof(encoded));
            return true;
        }
    }

    WorldSpaceRepresentation::WorldSpaceRepresentation(
        nvrhi::IDevice* device)
        : m_Device(device)
    {
        m_Status.accelerationStructuresSupported = device &&
            device->queryFeatureSupport(
                nvrhi::Feature::RayTracingAccelStruct);
        m_Status.rayQueriesSupported = device &&
            device->queryFeatureSupport(nvrhi::Feature::RayQuery);
        m_Status.state = IsSupported()
            ? WorldSpaceRepresentationState::Idle
            : WorldSpaceRepresentationState::Unsupported;
    }

    WorldSpaceRepresentation::~WorldSpaceRepresentation()
    {
        ClearGeneration();
        std::free(m_GeometryIndexMapUpload);
        std::free(m_InstanceDescriptions);
        std::free(m_BlasRecords);
    }

    void WorldSpaceRepresentation::ClearGeneration()
    {
        m_Tlas = nullptr;
        ClearGenerationArray(m_InstanceDescriptions, m_InstanceCount);
        m_InstanceTransformRevision = 0;
        ClearGenerationArray(m_GeometryIndexMapUpload, m_GeometryIndexMapCount);
        m_GeometryIndexMap = nullptr;
        m_GeometryIndexMapUploaded = false;
        ClearGenerationArray(m_BlasRecords, m_BlasCount);
        m_Selection.Reset();
        m_NextBlas = 0;
        m_Status.builtBlasCount = 0;
        m_Status.totalBlasCount = 0;
        m_Status.instanceCount = 0;
    }

    void WorldSpaceRepresentation::Reset()
    {
        ++m_Status.generation;
        ++m_Status.contentRevision;
        ClearGeneration();
        m_Tables = nullptr;
        m_SceneGeneration = 0;
        m_ReportedFailure = false;
        m_Status.state = IsSupported() ? WorldSpaceRepresentationState::Idle : WorldSpaceRepresentationState::Unsupported;
    }

    void WorldSpaceRepresentation::Fail(const char* message)
    {
        ++m_Status.generation;
        ++m_Status.contentRevision;
        ClearGeneration();
        m_Status.state = WorldSpaceRepresentationState::Failed;
        if (!m_ReportedFailure)
        {
            log::error("World-space representation failed: %s", message);
            m_ReportedFailure = true;
        }
    }

    bool WorldSpaceRepresentation::BeginGeneration(const RendererSceneView& scene,
        const RendererSceneGpuTablesNvrhi& tables)
    {
        ++m_Status.generation;
        ++m_Status.contentRevision;
        ClearGeneration();
        m_Tables = &tables;
        m_SceneGeneration = scene.generation;
        m_ReportedFailure = false;
        if (!scene.generation || tables.Generation() != scene.generation)
        {
            Fail("the canonical scene and upload adapter generations do not match");
            return false;
        }
        const auto prepared = m_Selection.Prepare(scene);
        if (!prepared.Succeeded())
        {
            Fail("canonical caster selection could not be prepared");
            return false;
        }
        const auto selected = m_Selection.View();
        if (!selected.meshes.count || !selected.instances.count)
        {
            Fail("the scene has no supported triangle instances");
            return false;
        }
        const auto geometryBuffer = tables.GeometryBuffer();
        const auto instanceBuffer = tables.InstanceBuffer();
        if (!geometryBuffer || !instanceBuffer || !geometryBuffer->getDesc().structStride ||
            !instanceBuffer->getDesc().structStride ||
            scene.geometries.count > geometryBuffer->getDesc().byteSize / geometryBuffer->getDesc().structStride ||
            scene.instances.count > instanceBuffer->getDesc().byteSize / instanceBuffer->getDesc().structStride)
        {
            Fail("geometry and instance upload tables cannot contain the canonical scene");
            return false;
        }
        if (!ReserveGenerationArray(m_GeometryIndexMapUpload, m_GeometryIndexMapCapacity, selected.geometries.count) ||
            !ReserveGenerationArray(m_BlasRecords, m_BlasCapacity, selected.meshes.count))
        {
            Fail("world generation storage could not be allocated");
            return false;
        }
        for (size_t index = 0; index < selected.geometries.count; ++index)
            new (&m_GeometryIndexMapUpload[index]) uint32_t{};
        m_GeometryIndexMapCount = selected.geometries.count;
        for (uint32_t index = 0; index < selected.meshes.count; ++index)
        {
            const auto& entry = selected.meshes.data[index];
            // own the partial record so Fail also releases its native fields.
            static_assert(std::is_nothrow_default_constructible_v<BlasRecord>);
            BlasRecord& record = *new (&m_BlasRecords[m_BlasCount]) BlasRecord();
            ++m_BlasCount;
            if (!TryResolveRayVisibilityGeometryMapOffset(entry.geometries.first, record.geometryMapOffset))
            {
                Fail("the geometry index map exceeded the DXR 24-bit instance contribution limit");
                return false;
            }
            if (!BuildMeshDescription(scene, selected, entry, tables, record.description, {m_GeometryIndexMapUpload, m_GeometryIndexMapCount}))
            {
                Fail("canonical caster geometry does not match its uploaded resources");
                return false;
            }
            const auto& mesh = scene.meshes.data[entry.sceneMesh];
            nvrhi::IBuffer* indices = nullptr;
            nvrhi::IBuffer* vertices = nullptr;
            if (!tables.GetBuffers(mesh.bufferGroupIndex, indices, vertices))
            { Fail("canonical caster buffers are unavailable"); return false; }
            record.sceneMesh = entry.sceneMesh;
            record.bufferGroup = mesh.bufferGroupIndex;
            record.indexBuffer = indices;
            record.vertexBuffer = vertices;
        }
        if (!ReserveGenerationArray(m_InstanceDescriptions, m_InstanceCapacity, selected.instances.count))
        {
            Fail("world instance storage could not be allocated");
            return false;
        }
        // InstanceDesc's pinned default constructor only initializes scalar fields
        // and copies the identity transform; its missing noexcept is not an allocation.
        for (size_t index = 0; index < selected.instances.count; ++index)
            new (&m_InstanceDescriptions[index]) nvrhi::rt::InstanceDesc();
        m_InstanceCount = selected.instances.count;
        nvrhi::BufferDesc geometryMapDescription;
        geometryMapDescription.byteSize = selected.geometries.count * sizeof(uint32_t);
        geometryMapDescription.structStride = sizeof(uint32_t);
        geometryMapDescription.debugName = "UVSR Ray Visibility Geometry Index Map";
        geometryMapDescription.initialState = nvrhi::ResourceStates::ShaderResource;
        geometryMapDescription.keepInitialState = true;
        m_GeometryIndexMap = m_Device->createBuffer(geometryMapDescription);
        if (!m_GeometryIndexMap)
        {
            Fail("the geometry index map buffer could not be created");
            return false;
        }
        m_Status.totalBlasCount = uint32_t(selected.meshes.count);
        m_Status.instanceCount = uint32_t(selected.instances.count);
        m_Status.state = WorldSpaceRepresentationState::BuildingBlas;
        return true;
    }

    bool WorldSpaceRepresentation::BuildNextBlas(
        nvrhi::ICommandList* commandList)
    {
        if (!commandList || m_NextBlas >= m_BlasCount)
            return false;

        if (!m_GeometryIndexMapUploaded)
        {
            if (!m_GeometryIndexMap || m_GeometryIndexMapCount == 0)
                return false;
            commandList->writeBuffer(
                m_GeometryIndexMap,
                m_GeometryIndexMapUpload,
                m_GeometryIndexMapCount * sizeof(uint32_t));
            m_GeometryIndexMapUploaded = true;
        }

        BlasRecord& record = m_BlasRecords[m_NextBlas];
        if (!record.accelerationStructure)
        {
            record.accelerationStructure =
                m_Device->createAccelStruct(record.description);
        }
        if (!record.accelerationStructure)
            return false;

        commandList->beginMarker("World Representation BLAS Build");
        commandList->setAccelStructState(
            record.accelerationStructure,
            nvrhi::ResourceStates::AccelStructWrite);
        commandList->setBufferState(
            record.indexBuffer,
            nvrhi::ResourceStates::AccelStructBuildInput);
        commandList->setBufferState(
            record.vertexBuffer,
            nvrhi::ResourceStates::AccelStructBuildInput);
        commandList->commitBarriers();
        commandList->buildBottomLevelAccelStruct(
            record.accelerationStructure,
            record.description.bottomLevelGeometries.data(),
            record.description.bottomLevelGeometries.size(),
            record.description.buildFlags);
        commandList->endMarker();

        record.built = true;
        ++m_NextBlas;
        m_Status.builtBlasCount = uint32_t(m_NextBlas);
        if (m_NextBlas == m_BlasCount)
            m_Status.state = WorldSpaceRepresentationState::BuildingTlas;
        return true;
    }

    bool WorldSpaceRepresentation::BuildOrUpdateTlas(nvrhi::ICommandList* commandList,
        const RendererSceneView& scene, bool performUpdate)
    {
        const auto selected = m_Selection.View();
        if (!commandList || !selected.instances.count || m_InstanceCount != selected.instances.count)
            return false;
        for (size_t index = 0; index < m_BlasCount; ++index)
            if (!m_BlasRecords[index].built || !m_BlasRecords[index].accelerationStructure) return false;
        for (size_t index = 0; index < selected.instances.count; ++index)
        {
            const auto& entry = selected.instances.data[index];
            if (entry.selectedMesh >= m_BlasCount) return false;
            const auto& record = m_BlasRecords[entry.selectedMesh];
            nvrhi::rt::InstanceDesc description;
            if (!ReadCanonicalInstance(scene, entry.sceneInstance, description.transform)) return false;
            description.setInstanceID(entry.sceneInstance).setInstanceMask(0xffu)
                .setInstanceContributionToHitGroupIndex(record.geometryMapOffset).setBLAS(record.accelerationStructure);
            m_InstanceDescriptions[index] = description;
        }
        nvrhi::rt::AccelStructBuildFlags buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace |
            nvrhi::rt::AccelStructBuildFlags::AllowUpdate;
        if (!m_Tlas)
        {
            nvrhi::rt::AccelStructDesc description;
            description.setTopLevelMaxInstances(m_InstanceCount).setBuildFlags(buildFlags)
                .setDebugName("UVSR World TLAS");
            m_Tlas = m_Device->createAccelStruct(description);
            performUpdate = false;
        }
        if (!m_Tlas) return false;
        if (performUpdate) buildFlags = buildFlags | nvrhi::rt::AccelStructBuildFlags::PerformUpdate;
        commandList->beginMarker(performUpdate ? "World Representation TLAS Refit" : "World Representation TLAS Build");
        commandList->buildTopLevelAccelStruct(m_Tlas, m_InstanceDescriptions, m_InstanceCount, buildFlags);
        commandList->endMarker();
        m_InstanceTransformRevision = scene.instanceTransformRevision;
        m_Status.instanceCount = uint32_t(selected.instances.count);
        m_Status.state = WorldSpaceRepresentationState::Ready;
        ++m_Status.contentRevision;
        return true;
    }

    bool WorldSpaceRepresentation::UploadedResourcesMatch() const
    {
        if (!m_Tables || m_Tables->Generation() != m_SceneGeneration) return false;
        for (size_t index = 0; index < m_BlasCount; ++index)
        {
            const auto& record = m_BlasRecords[index];
            nvrhi::IBuffer* indices = nullptr;
            nvrhi::IBuffer* vertices = nullptr;
            if (!m_Tables->GetBuffers(record.bufferGroup, indices, vertices) ||
                indices != record.indexBuffer || vertices != record.vertexBuffer) return false;
        }
        return true;
    }

    bool WorldSpaceRepresentation::InstanceTransformsChanged(const RendererSceneView& scene)
    {
        if (m_InstanceTransformRevision == scene.instanceTransformRevision) return false;
        const auto selected = m_Selection.View();
        if (selected.instances.count != m_InstanceCount) return true;
        for (size_t index = 0; index < selected.instances.count; ++index)
        {
            const auto& entry = selected.instances.data[index];
            const auto& description = m_InstanceDescriptions[index];
            nvrhi::rt::AffineTransform transform;
            if (!ReadCanonicalInstance(scene, entry.sceneInstance, transform) ||
                entry.sceneInstance != description.instanceID ||
                std::memcmp(&transform, &description.transform, sizeof(transform)) != 0) return true;
        }
        // an unselected instance can move without changing this TLAS.
        m_InstanceTransformRevision = scene.instanceTransformRevision;
        return false;
    }

    bool WorldSpaceRepresentation::Update(
        nvrhi::ICommandList* commandList,
        const RendererSceneView& scene,
        RendererSceneGpuTablesNvrhi& tables,
        const WorldSpaceRepresentationSettings& settings,
        bool activeConsumer)
    {
        if (!IsSupported() || !settings.allowRayTraversal)
            return false;
        if (!activeConsumer)
            return IsReady() && &tables == m_Tables &&
                scene.generation == m_SceneGeneration && tables.Generation() == scene.generation;
        if (!commandList || !scene.generation || !tables.InstancesReady(scene))
            return false;
        if (!tables.PrepareRayGeometry(scene).Succeeded() || !tables.RecordGeometry(commandList))
        {
            Fail("canonical ray geometry could not be prepared or uploaded");
            return false;
        }

        if (&tables != m_Tables || scene.generation != m_SceneGeneration)
        {
            if (!BeginGeneration(scene, tables))
                return false;
        }

        if (m_Status.state == WorldSpaceRepresentationState::Failed)
            return false;

        if (!UploadedResourcesMatch())
        {
            Fail("uploaded resources changed outside the canonical scene transaction");
            return false;
        }
        if (!m_Selection.Matches(scene))
        {
            Reset();
            if (!BeginGeneration(scene, tables))
                return false;
            return false;
        }

        if (m_Status.state ==
            WorldSpaceRepresentationState::BuildingBlas)
        {
            if (!BuildNextBlas(commandList))
                Fail("a staged BLAS build could not be submitted");
            return false;
        }
        if (m_Status.state ==
            WorldSpaceRepresentationState::BuildingTlas)
        {
            if (!BuildOrUpdateTlas(commandList, scene, false))
                Fail("the TLAS build could not be submitted");
            return IsReady();
        }
        if (!IsReady())
            return false;

        const bool transformsChanged = InstanceTransformsChanged(scene);
        if (transformsChanged)
        {
            if (!BuildOrUpdateTlas(commandList, scene, true))
            {
                Fail("the TLAS update could not be submitted");
                return false;
            }
        }
        return IsReady();
    }

    RaySceneView WorldSpaceRepresentation::GetRaySceneView(
        const RendererSceneView& scene,
        const RendererSceneGpuTablesNvrhi& tables) const
    {
        if (!IsReady() || &tables != m_Tables || scene.generation != m_SceneGeneration || !tables.MaterialsReady(scene) ||
            !tables.GeometryReady(scene.generation) || !tables.InstancesReady(scene) || tables.Generation() != m_SceneGeneration)
            return {};

        RaySceneView view = {
            m_Tlas,
            tables.GeometryBuffer(),
            tables.MaterialBuffer(),
            m_GeometryIndexMap,
            tables.DescriptorTable(),
            m_Status.generation,
            m_Status.contentRevision,
            m_SceneGeneration
        };
        return view ? view : RaySceneView{};
    }
}
