#include "world_space_representation.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/Scene.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

namespace uvsr
{
    namespace
    {
        nvrhi::rt::AccelStructBuildFlags GetBuildPreferenceFlags(
            BvhBuildPreference preference)
        {
            switch (preference)
            {
            case BvhBuildPreference::FastBuild:
                return nvrhi::rt::AccelStructBuildFlags::PreferFastBuild;
            case BvhBuildPreference::Balanced:
                return nvrhi::rt::AccelStructBuildFlags::None;
            case BvhBuildPreference::FastTrace:
            default:
                return nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;
            }
        }

        uint64_t HashCombine(uint64_t hash, uint64_t value)
        {
            value ^= value >> 33u;
            value *= 0xff51afd7ed558ccdull;
            value ^= value >> 33u;
            hash ^= value + 0x9e3779b97f4a7c15ull +
                (hash << 6u) + (hash >> 2u);
            return hash;
        }

        uint64_t GetMeshTopologySignature(const MeshInfo& mesh)
        {
            uint64_t signature = HashCombine(
                uint64_t(mesh.type),
                uint64_t(mesh.geometries.size()));
            signature = HashCombine(
                signature,
                mesh.skinPrototype ? 1u : 0u);
            signature = HashCombine(
                signature,
                mesh.isMorphTargetAnimationMesh ? 1u : 0u);
            signature = HashCombine(signature, mesh.indexOffset);
            signature = HashCombine(signature, mesh.vertexOffset);
            signature = HashCombine(signature, mesh.totalIndices);
            signature = HashCombine(signature, mesh.totalVertices);
            if (mesh.buffers)
            {
                signature = HashCombine(
                    signature,
                    reinterpret_cast<uintptr_t>(
                        mesh.buffers->indexBuffer.Get()));
                signature = HashCombine(
                    signature,
                    reinterpret_cast<uintptr_t>(
                        mesh.buffers->vertexBuffer.Get()));
                const nvrhi::BufferRange& positions =
                    mesh.buffers->getVertexBufferRange(
                        VertexAttribute::Position);
                signature = HashCombine(signature, positions.byteOffset);
                signature = HashCombine(signature, positions.byteSize);
            }
            else
            {
                signature = HashCombine(signature, ~0ull);
            }
            for (const auto& geometry : mesh.geometries)
            {
                if (!geometry)
                {
                    signature = HashCombine(signature, ~0ull);
                    continue;
                }
                signature = HashCombine(signature, uint64_t(geometry->type));
                signature = HashCombine(
                    signature, geometry->indexOffsetInMesh);
                signature = HashCombine(
                    signature, geometry->vertexOffsetInMesh);
                signature = HashCombine(signature, geometry->numIndices);
                signature = HashCombine(signature, geometry->numVertices);
                signature = HashCombine(
                    signature,
                    reinterpret_cast<uintptr_t>(geometry->material.get()));
                signature = HashCombine(
                    signature,
                    geometry->material
                        ? uint64_t(geometry->material->domain)
                        : ~0ull);
            }
            return signature;
        }

        bool BuildMeshDescription(
            const std::shared_ptr<MeshInfo>& mesh,
            const WorldSpaceRepresentationSettings& settings,
            nvrhi::rt::AccelStructDesc& description,
            std::vector<uint32_t>& geometryIndices)
        {
            if (!mesh || mesh->type != MeshType::Triangles ||
                !mesh->buffers || !mesh->buffers->indexBuffer ||
                !mesh->buffers->vertexBuffer)
            {
                return false;
            }

            const nvrhi::BufferRange& positions =
                mesh->buffers->getVertexBufferRange(
                    VertexAttribute::Position);
            if (positions.byteSize == 0u)
                return false;

            description = nvrhi::rt::AccelStructDesc();
            geometryIndices.clear();
            description.isTopLevel = false;
            description.debugName = "UVSR BLAS: " + mesh->name;
            description.buildFlags = GetBuildPreferenceFlags(
                settings.bvhBuildPreference);
            const bool dynamic = bool(mesh->skinPrototype) ||
                mesh->isMorphTargetAnimationMesh;
            if (dynamic &&
                settings.blasUpdateMode == BlasUpdateMode::Refit)
            {
                description.buildFlags = description.buildFlags |
                    nvrhi::rt::AccelStructBuildFlags::AllowUpdate;
            }

            for (const auto& geometry : mesh->geometries)
            {
                if (!geometry ||
                    geometry->type != MeshGeometryPrimitiveType::Triangles ||
                    geometry->numIndices < 3u ||
                    geometry->numIndices % 3u != 0u ||
                    geometry->numVertices == 0u)
                {
                    continue;
                }

                const MaterialDomain domain = geometry->material
                    ? geometry->material->domain
                    : MaterialDomain::Count;
                if (domain != MaterialDomain::Opaque &&
                    domain != MaterialDomain::AlphaTested)
                {
                    continue;
                }

                nvrhi::rt::GeometryTriangles triangles;
                triangles.indexBuffer = mesh->buffers->indexBuffer;
                triangles.indexFormat = nvrhi::Format::R32_UINT;
                triangles.indexOffset =
                    uint64_t(mesh->indexOffset +
                        geometry->indexOffsetInMesh) *
                    sizeof(uint32_t);
                triangles.indexCount = geometry->numIndices;
                triangles.vertexBuffer = mesh->buffers->vertexBuffer;
                triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
                triangles.vertexOffset = positions.byteOffset +
                    uint64_t(mesh->vertexOffset +
                        geometry->vertexOffsetInMesh) *
                    sizeof(float3);
                triangles.vertexCount = geometry->numVertices;
                triangles.vertexStride = sizeof(float3);

                const nvrhi::rt::GeometryFlags geometryFlags =
                    geometry->material &&
                        geometry->material->domain == MaterialDomain::Opaque
                    ? nvrhi::rt::GeometryFlags::Opaque
                    : nvrhi::rt::GeometryFlags::None;
                description.addBottomLevelGeometry(
                    nvrhi::rt::GeometryDesc()
                        .setTriangles(triangles)
                        .setFlags(geometryFlags));
                geometryIndices.push_back(
                    uint32_t(geometry->globalGeometryIndex));
            }
            return !description.bottomLevelGeometries.empty() &&
                description.bottomLevelGeometries.size() ==
                    geometryIndices.size();
        }

        bool TransformsEqual(
            const std::array<float, 12>& left,
            const nvrhi::rt::AffineTransform& right)
        {
            return std::memcmp(
                left.data(),
                &right,
                sizeof(float) * left.size()) == 0;
        }

        bool IsFrameIndexNewer(uint32_t candidate, uint32_t baseline)
        {
            return int32_t(candidate - baseline) > 0;
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

    void WorldSpaceRepresentation::Reset()
    {
        ++m_Status.generation;
        ++m_Status.contentRevision;
        m_Tlas = nullptr;
        m_InstanceSnapshots.clear();
        m_GeometryIndexMapUpload.clear();
        m_GeometryIndexMap = nullptr;
        m_GeometryIndexMapUploaded = false;
        m_SourceInstanceTopology.clear();
        m_Instances.clear();
        m_BlasRecords.clear();
        m_NextBlas = 0u;
        m_Scene = nullptr;
        m_HasSettings = false;
        m_ReportedFailure = false;
        m_Status.builtBlasCount = 0u;
        m_Status.totalBlasCount = 0u;
        m_Status.instanceCount = 0u;
        m_Status.state = IsSupported()
            ? WorldSpaceRepresentationState::Idle
            : WorldSpaceRepresentationState::Unsupported;
    }

    void WorldSpaceRepresentation::Invalidate(
        WorldSpaceRepresentationInvalidation invalidation)
    {
        if (invalidation ==
            WorldSpaceRepresentationInvalidation::None)
        {
            return;
        }
        if (!IsSupported())
        {
            m_Status.state = WorldSpaceRepresentationState::Unsupported;
            return;
        }
        if (invalidation ==
            WorldSpaceRepresentationInvalidation::BlasAndTlas)
        {
            Reset();
            return;
        }

        if (m_Status.state == WorldSpaceRepresentationState::Failed)
        {
            Reset();
            return;
        }

        ++m_Status.generation;
        ++m_Status.contentRevision;
        m_Tlas = nullptr;
        m_InstanceSnapshots.clear();
        if (m_BlasRecords.empty())
        {
            m_Status.state = WorldSpaceRepresentationState::Idle;
        }
        else if (m_NextBlas < m_BlasRecords.size())
        {
            m_Status.state = WorldSpaceRepresentationState::BuildingBlas;
        }
        else
        {
            m_Status.state = WorldSpaceRepresentationState::BuildingTlas;
        }
    }

    void WorldSpaceRepresentation::Fail(const char* message)
    {
        ++m_Status.generation;
        ++m_Status.contentRevision;
        m_Status.state = WorldSpaceRepresentationState::Failed;
        m_Tlas = nullptr;
        m_InstanceSnapshots.clear();
        m_GeometryIndexMapUpload.clear();
        m_GeometryIndexMap = nullptr;
        m_GeometryIndexMapUploaded = false;
        m_SourceInstanceTopology.clear();
        m_Instances.clear();
        m_BlasRecords.clear();
        m_NextBlas = 0u;
        m_Status.builtBlasCount = 0u;
        m_Status.totalBlasCount = 0u;
        m_Status.instanceCount = 0u;
        if (!m_ReportedFailure)
        {
            log::error("World-space representation failed: %s", message);
            m_ReportedFailure = true;
        }
    }

    bool WorldSpaceRepresentation::BeginGeneration(
        Scene* scene,
        const WorldSpaceRepresentationSettings& settings)
    {
        ++m_Status.generation;
        ++m_Status.contentRevision;
        m_Tlas = nullptr;
        m_InstanceSnapshots.clear();
        m_GeometryIndexMapUpload.clear();
        m_GeometryIndexMap = nullptr;
        m_GeometryIndexMapUploaded = false;
        m_SourceInstanceTopology.clear();
        m_Instances.clear();
        m_BlasRecords.clear();
        m_NextBlas = 0u;
        m_Scene = scene;
        m_Settings = settings;
        m_HasSettings = true;
        m_ReportedFailure = false;

        if (!scene || !scene->GetSceneGraph())
        {
            Fail("no scene graph is available");
            return false;
        }

        std::unordered_map<const MeshInfo*, size_t> meshIndices;
        const auto& sourceInstances =
            scene->GetSceneGraph()->GetMeshInstances();
        m_SourceInstanceTopology.reserve(sourceInstances.size());
        for (const auto& instance : sourceInstances)
        {
            SourceInstanceTopology sourceTopology;
            sourceTopology.instance = instance.get();
            sourceTopology.mesh = instance && instance->GetMesh()
                ? instance->GetMesh().get()
                : nullptr;
            sourceTopology.node = instance
                ? instance->GetNode()
                : nullptr;
            sourceTopology.meshTopologySignature =
                sourceTopology.mesh
                ? GetMeshTopologySignature(*sourceTopology.mesh)
                : 0u;
            m_SourceInstanceTopology.push_back(sourceTopology);

            if (!instance || !instance->GetMesh() || !instance->GetNode())
                continue;

            const std::shared_ptr<MeshInfo>& mesh = instance->GetMesh();
            auto found = meshIndices.find(mesh.get());
            if (found == meshIndices.end())
            {
                nvrhi::rt::AccelStructDesc description;
                std::vector<uint32_t> geometryIndices;
                if (!BuildMeshDescription(
                        mesh,
                        settings,
                        description,
                        geometryIndices))
                {
                    log::warning(
                        "World-space representation skipped unsupported or empty mesh '%s'",
                        mesh->name.c_str());
                    continue;
                }

                if (!IsRayVisibilityGeometryMapOffsetSupported(
                        m_GeometryIndexMapUpload.size()))
                {
                    Fail("the geometry index map exceeded the DXR 24-bit "
                        "instance contribution limit");
                    return false;
                }

                BlasRecord record;
                record.mesh = mesh;
                record.dynamic = bool(mesh->skinPrototype) ||
                    mesh->isMorphTargetAnimationMesh;
                record.topologySignature =
                    GetMeshTopologySignature(*mesh);
                record.geometryMapOffset = uint32_t(
                    m_GeometryIndexMapUpload.size());
                record.description = std::move(description);
                m_GeometryIndexMapUpload.insert(
                    m_GeometryIndexMapUpload.end(),
                    geometryIndices.begin(),
                    geometryIndices.end());
                const size_t index = m_BlasRecords.size();
                meshIndices.emplace(mesh.get(), index);
                m_BlasRecords.push_back(std::move(record));
            }
            m_Instances.push_back(instance);
        }

        if (m_BlasRecords.empty() || m_Instances.empty())
        {
            Fail("the scene has no supported triangle instances");
            return false;
        }

        nvrhi::BufferDesc geometryMapDescription;
        geometryMapDescription.byteSize =
            m_GeometryIndexMapUpload.size() * sizeof(uint32_t);
        geometryMapDescription.structStride = sizeof(uint32_t);
        geometryMapDescription.debugName =
            "UVSR Ray Visibility Geometry Index Map";
        geometryMapDescription.initialState =
            nvrhi::ResourceStates::ShaderResource;
        geometryMapDescription.keepInitialState = true;
        m_GeometryIndexMap = m_Device->createBuffer(
            geometryMapDescription);
        if (!m_GeometryIndexMap)
        {
            Fail("the geometry index map buffer could not be created");
            return false;
        }

        // Remove instances whose mesh was rejected.
        m_Instances.erase(
            std::remove_if(
                m_Instances.begin(),
                m_Instances.end(),
                [&meshIndices](const std::shared_ptr<MeshInstance>& instance)
                {
                    return !instance ||
                        meshIndices.find(instance->GetMesh().get()) ==
                            meshIndices.end();
                }),
            m_Instances.end());

        m_Status.builtBlasCount = 0u;
        m_Status.totalBlasCount =
            uint32_t(m_BlasRecords.size());
        m_Status.instanceCount = uint32_t(m_Instances.size());
        m_Status.state = WorldSpaceRepresentationState::BuildingBlas;
        return true;
    }

    bool WorldSpaceRepresentation::BuildNextBlas(
        nvrhi::ICommandList* commandList,
        uint32_t frameIndex)
    {
        if (!commandList || m_NextBlas >= m_BlasRecords.size())
            return false;

        if (!m_GeometryIndexMapUploaded)
        {
            if (!m_GeometryIndexMap || m_GeometryIndexMapUpload.empty())
                return false;
            commandList->writeBuffer(
                m_GeometryIndexMap,
                m_GeometryIndexMapUpload.data(),
                m_GeometryIndexMapUpload.size() * sizeof(uint32_t));
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
            record.mesh->buffers->indexBuffer,
            nvrhi::ResourceStates::AccelStructBuildInput);
        commandList->setBufferState(
            record.mesh->buffers->vertexBuffer,
            nvrhi::ResourceStates::AccelStructBuildInput);
        commandList->commitBarriers();
        commandList->buildBottomLevelAccelStruct(
            record.accelerationStructure,
            record.description.bottomLevelGeometries.data(),
            record.description.bottomLevelGeometries.size(),
            record.description.buildFlags);
        commandList->endMarker();

        record.built = true;
        record.lastSynchronizedFrameIndex = frameIndex;
        ++m_NextBlas;
        m_Status.builtBlasCount = uint32_t(m_NextBlas);
        if (m_NextBlas == m_BlasRecords.size())
            m_Status.state = WorldSpaceRepresentationState::BuildingTlas;
        return true;
    }

    bool WorldSpaceRepresentation::BuildOrUpdateTlas(
        nvrhi::ICommandList* commandList,
        bool performUpdate)
    {
        if (!commandList || m_Instances.empty())
            return false;

        std::unordered_map<const MeshInfo*, const BlasRecord*> records;
        for (const BlasRecord& record : m_BlasRecords)
        {
            if (!record.built || !record.accelerationStructure)
                return false;
            records.emplace(record.mesh.get(), &record);
        }

        std::vector<nvrhi::rt::InstanceDesc> instanceDescriptions;
        std::vector<InstanceSnapshot> snapshots;
        instanceDescriptions.reserve(m_Instances.size());
        snapshots.reserve(m_Instances.size());
        for (const auto& instance : m_Instances)
        {
            if (!instance || !instance->GetNode())
                return false;
            const auto found = records.find(
                instance->GetMesh().get());
            if (found == records.end())
                return false;

            nvrhi::rt::InstanceDesc description;
            affineToColumnMajor(
                instance->GetNode()->GetLocalToWorldTransformFloat(),
                description.transform);
            const uint32_t instanceId =
                uint32_t(std::max(instance->GetInstanceIndex(), 0));
            description.setInstanceID(instanceId)
                .setInstanceMask(0xffu)
                .setInstanceContributionToHitGroupIndex(
                    found->second->geometryMapOffset)
                .setBLAS(found->second->accelerationStructure);
            instanceDescriptions.push_back(description);

            InstanceSnapshot snapshot;
            snapshot.instance = instance.get();
            snapshot.mesh = instance->GetMesh().get();
            snapshot.instanceId = instanceId;
            std::memcpy(
                snapshot.transform.data(),
                &description.transform,
                sizeof(description.transform));
            snapshots.push_back(snapshot);
        }

        const nvrhi::rt::AccelStructBuildFlags preference =
            GetBuildPreferenceFlags(m_Settings.bvhBuildPreference);
        nvrhi::rt::AccelStructBuildFlags buildFlags = preference;
        if (m_Settings.tlasUpdateMode == TlasUpdateMode::Refit)
        {
            buildFlags = buildFlags |
                nvrhi::rt::AccelStructBuildFlags::AllowUpdate;
        }

        if (!m_Tlas || performUpdate &&
            m_Settings.tlasUpdateMode != TlasUpdateMode::Refit)
        {
            m_Tlas = nullptr;
        }
        if (!m_Tlas)
        {
            nvrhi::rt::AccelStructDesc description;
            description.setTopLevelMaxInstances(
                    instanceDescriptions.size())
                .setBuildFlags(buildFlags)
                .setDebugName("UVSR World TLAS");
            m_Tlas = m_Device->createAccelStruct(description);
            performUpdate = false;
        }
        if (!m_Tlas)
            return false;

        if (performUpdate)
        {
            buildFlags = buildFlags |
                nvrhi::rt::AccelStructBuildFlags::PerformUpdate;
        }

        commandList->beginMarker(
            performUpdate
                ? "World Representation TLAS Refit"
                : "World Representation TLAS Build");
        commandList->buildTopLevelAccelStruct(
            m_Tlas,
            instanceDescriptions.data(),
            instanceDescriptions.size(),
            buildFlags);
        commandList->endMarker();

        m_InstanceSnapshots = std::move(snapshots);
        m_Status.instanceCount = uint32_t(m_Instances.size());
        m_Status.state = WorldSpaceRepresentationState::Ready;
        ++m_Status.contentRevision;
        return true;
    }

    bool WorldSpaceRepresentation::TopologyMatches() const
    {
        if (!m_Scene || !m_Scene->GetSceneGraph())
            return false;

        const auto& currentInstances =
            m_Scene->GetSceneGraph()->GetMeshInstances();
        if (currentInstances.size() != m_SourceInstanceTopology.size())
            return false;

        for (size_t index = 0u; index < currentInstances.size(); ++index)
        {
            const auto& instance = currentInstances[index];
            const SourceInstanceTopology& expected =
                m_SourceInstanceTopology[index];
            const MeshInfo* mesh = instance && instance->GetMesh()
                ? instance->GetMesh().get()
                : nullptr;
            const SceneGraphNode* node =
                instance ? instance->GetNode() : nullptr;
            if (instance.get() != expected.instance ||
                mesh != expected.mesh || node != expected.node ||
                (mesh ? GetMeshTopologySignature(*mesh) : 0u) !=
                    expected.meshTopologySignature)
            {
                return false;
            }
        }

        for (const BlasRecord& record : m_BlasRecords)
        {
            if (!record.mesh ||
                record.topologySignature !=
                    GetMeshTopologySignature(*record.mesh))
            {
                return false;
            }
        }
        return true;
    }

    bool WorldSpaceRepresentation::InstanceTransformsChanged() const
    {
        if (m_InstanceSnapshots.size() != m_Instances.size())
            return true;
        for (size_t index = 0u; index < m_Instances.size(); ++index)
        {
            const auto& instance = m_Instances[index];
            const InstanceSnapshot& snapshot = m_InstanceSnapshots[index];
            const uint32_t instanceId = instance
                ? uint32_t(std::max(instance->GetInstanceIndex(), 0))
                : 0u;
            if (!instance || !instance->GetNode() ||
                snapshot.instance != instance.get() ||
                snapshot.mesh != instance->GetMesh().get() ||
                snapshot.instanceId != instanceId)
            {
                return true;
            }

            nvrhi::rt::AffineTransform transform;
            affineToColumnMajor(
                instance->GetNode()->GetLocalToWorldTransformFloat(),
                transform);
            if (!TransformsEqual(snapshot.transform, transform))
                return true;
        }
        return false;
    }

    bool WorldSpaceRepresentation::UpdateDynamicBlases(
        nvrhi::ICommandList* commandList,
        uint32_t frameIndex,
        bool forceAll,
        bool& anyUpdated)
    {
        anyUpdated = false;
        if (!commandList || !m_Scene || !m_Scene->GetSceneGraph())
            return false;

        std::unordered_set<const MeshInfo*> dirtyMeshes;
        const auto& skinnedInstances =
            m_Scene->GetSceneGraph()->GetSkinnedMeshInstances();
        for (const BlasRecord& record : m_BlasRecords)
        {
            if (!record.dynamic)
                continue;

            bool dirty = forceAll ||
                record.mesh->isMorphTargetAnimationMesh;
            if (!dirty)
            {
                for (const auto& skinned : skinnedInstances)
                {
                    if (skinned && skinned->GetMesh().get() ==
                            record.mesh.get() &&
                        IsFrameIndexNewer(
                            skinned->GetLastUpdateFrameIndex(),
                            record.lastSynchronizedFrameIndex))
                    {
                        dirty = true;
                        break;
                    }
                }
            }
            if (dirty)
                dirtyMeshes.insert(record.mesh.get());
        }
        if (dirtyMeshes.empty())
            return true;

        for (BlasRecord& record : m_BlasRecords)
        {
            if (!record.dynamic ||
                dirtyMeshes.count(record.mesh.get()) == 0u)
            {
                continue;
            }
            commandList->setAccelStructState(
                record.accelerationStructure,
                nvrhi::ResourceStates::AccelStructWrite);
            commandList->setBufferState(
                record.mesh->buffers->indexBuffer,
                nvrhi::ResourceStates::AccelStructBuildInput);
            commandList->setBufferState(
                record.mesh->buffers->vertexBuffer,
                nvrhi::ResourceStates::AccelStructBuildInput);
        }
        commandList->commitBarriers();

        commandList->beginMarker("World Representation Dynamic BLAS Updates");
        for (BlasRecord& record : m_BlasRecords)
        {
            if (!record.dynamic ||
                dirtyMeshes.count(record.mesh.get()) == 0u)
            {
                continue;
            }
            nvrhi::rt::AccelStructBuildFlags flags =
                record.description.buildFlags;
            if (m_Settings.blasUpdateMode == BlasUpdateMode::Refit)
            {
                flags = flags |
                    nvrhi::rt::AccelStructBuildFlags::PerformUpdate;
            }
            commandList->buildBottomLevelAccelStruct(
                record.accelerationStructure,
                record.description.bottomLevelGeometries.data(),
                record.description.bottomLevelGeometries.size(),
                flags);
            record.lastSynchronizedFrameIndex = frameIndex;
            anyUpdated = true;
        }
        commandList->endMarker();
        return true;
    }

    bool WorldSpaceRepresentation::Update(
        nvrhi::ICommandList* commandList,
        Scene* scene,
        const WorldSpaceRepresentationSettings& settings,
        uint32_t frameIndex,
        bool activeConsumer)
    {
        if (!IsSupported() || !settings.allowRayTraversal)
            return false;
        if (!activeConsumer)
            return IsReady() && scene == m_Scene;
        if (!commandList || !scene)
            return false;

        if (scene != m_Scene || !m_HasSettings)
        {
            if (!BeginGeneration(scene, settings))
                return false;
        }
        else
        {
            const WorldSpaceRepresentationInvalidation invalidation =
                GetWorldSpaceRepresentationInvalidation(
                    m_Settings, settings);
            if (invalidation ==
                WorldSpaceRepresentationInvalidation::BlasAndTlas)
            {
                Reset();
                if (!BeginGeneration(scene, settings))
                    return false;
            }
            else if (invalidation ==
                WorldSpaceRepresentationInvalidation::Tlas)
            {
                m_Settings = settings;
                Invalidate(invalidation);
            }
        }

        if (m_Status.state == WorldSpaceRepresentationState::Failed)
            return false;

        if (!TopologyMatches())
        {
            Reset();
            if (!BeginGeneration(scene, settings))
                return false;
            return false;
        }

        if (m_Status.state ==
            WorldSpaceRepresentationState::BuildingBlas)
        {
            if (!BuildNextBlas(commandList, frameIndex))
                Fail("a staged BLAS build could not be submitted");
            return false;
        }
        if (m_Status.state ==
            WorldSpaceRepresentationState::BuildingTlas)
        {
            bool dynamicBlasUpdated = false;
            if (!UpdateDynamicBlases(
                    commandList, frameIndex, true,
                    dynamicBlasUpdated))
            {
                Fail("dynamic BLAS synchronization before the TLAS failed");
                return false;
            }
            if (!BuildOrUpdateTlas(commandList, false))
                Fail("the TLAS build could not be submitted");
            return IsReady();
        }
        if (!IsReady())
            return false;

        bool dynamicBlasUpdated = false;
        if (!UpdateDynamicBlases(
                commandList, frameIndex, false,
                dynamicBlasUpdated))
        {
            Fail("a dynamic BLAS update could not be submitted");
            return false;
        }
        const bool transformsChanged = InstanceTransformsChanged();
        if (dynamicBlasUpdated || transformsChanged)
        {
            const bool performUpdate =
                m_Settings.tlasUpdateMode == TlasUpdateMode::Refit;
            if (!BuildOrUpdateTlas(commandList, performUpdate))
            {
                Fail("the TLAS update could not be submitted");
                return false;
            }
        }
        return IsReady();
    }
}
