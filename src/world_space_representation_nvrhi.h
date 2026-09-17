#pragma once

#include "ray_scene_view_nvrhi.h"
#include "renderer_scene_ray.h"
#include "world_space_representation_contract.h"
#include "world_space_representation_settings.h"

#include <nvrhi/nvrhi.h>

#include <cstddef>
#include <cstdint>

namespace uvsr
{
    class RendererSceneGpuTablesNvrhi;
    enum class WorldSpaceRepresentationState : uint32_t
    {
        Unsupported,
        Idle,
        BuildingBlas,
        BuildingTlas,
        Ready,
        Failed
    };

    struct WorldSpaceRepresentationStatus
    {
        WorldSpaceRepresentationState state =
            WorldSpaceRepresentationState::Idle;
        uint32_t builtBlasCount = 0u;
        uint32_t totalBlasCount = 0u;
        uint32_t instanceCount = 0u;
        uint64_t generation = 0u;
        // Generation identifies allocation/binding changes. Content revision
        // also advances when an in-place BLAS/TLAS update changes what rays
        // can hit, so progressive consumers never retain stale samples merely
        // because the acceleration-structure handles stayed the same.
        uint64_t contentRevision = 0u;
        bool accelerationStructuresSupported = false;
        bool rayQueriesSupported = false;
    };

    // Consumer-neutral ownership for UVSR's world-space triangle BVH. Work is
    // staged one BLAS per frame until a coherent TLAS generation is ready.
    class WorldSpaceRepresentation final
    {
    public:
        explicit WorldSpaceRepresentation(nvrhi::IDevice* device);
        ~WorldSpaceRepresentation();
        WorldSpaceRepresentation(const WorldSpaceRepresentation&) = delete;
        WorldSpaceRepresentation& operator=(const WorldSpaceRepresentation&) = delete;
        WorldSpaceRepresentation(WorldSpaceRepresentation&&) = delete;
        WorldSpaceRepresentation& operator=(WorldSpaceRepresentation&&) = delete;

        void Reset();

        // Returns true only when a coherent TLAS is ready for this scene
        // generation. When activeConsumer is false, no new build or
        // update work is submitted.
        [[nodiscard]] bool Update(
            nvrhi::ICommandList* commandList,
            const RendererSceneView& scene,
            RendererSceneGpuTablesNvrhi& tables,
            const WorldSpaceRepresentationSettings& settings,
            bool activeConsumer);

        [[nodiscard]] RaySceneView GetRaySceneView(
            const RendererSceneView& scene,
            const RendererSceneGpuTablesNvrhi& tables) const;

        [[nodiscard]] bool IsSupported() const
        {
            return m_Status.accelerationStructuresSupported &&
                m_Status.rayQueriesSupported;
        }

        [[nodiscard]] bool IsReady() const
        {
            return m_Status.state ==
                    WorldSpaceRepresentationState::Ready &&
                bool(m_Tlas);
        }

        [[nodiscard]] const WorldSpaceRepresentationStatus& GetStatus() const
        {
            return m_Status;
        }

    private:
        struct BlasRecord
        {
            uint32_t sceneMesh = 0;
            uint32_t bufferGroup = 0;
            nvrhi::BufferHandle indexBuffer;
            nvrhi::BufferHandle vertexBuffer;
            nvrhi::rt::AccelStructDesc description;
            nvrhi::rt::AccelStructHandle accelerationStructure;
            uint32_t geometryMapOffset = 0u;
            bool built = false;
        };

        nvrhi::DeviceHandle m_Device;
        const RendererSceneGpuTablesNvrhi* m_Tables = nullptr;
        uint64_t m_SceneGeneration = 0;
        uint64_t m_InstanceTransformRevision = 0;
        RendererSceneRaySelection m_Selection;
        // preparation owns the live prefixes. reset keeps capacity, while each
        // BLAS description still owns its native vendor geometry/name storage.
        BlasRecord* m_BlasRecords = nullptr;
        size_t m_BlasCount = 0;
        size_t m_BlasCapacity = 0;
        nvrhi::rt::InstanceDesc* m_InstanceDescriptions = nullptr;
        size_t m_InstanceCount = 0;
        size_t m_InstanceCapacity = 0;
        uint32_t* m_GeometryIndexMapUpload = nullptr;
        size_t m_GeometryIndexMapCount = 0;
        size_t m_GeometryIndexMapCapacity = 0;
        nvrhi::BufferHandle m_GeometryIndexMap;
        bool m_GeometryIndexMapUploaded = false;
        nvrhi::rt::AccelStructHandle m_Tlas;
        size_t m_NextBlas = 0u;
        WorldSpaceRepresentationStatus m_Status;
        bool m_ReportedFailure = false;

        [[nodiscard]] bool BeginGeneration(
            const RendererSceneView& scene,
            const RendererSceneGpuTablesNvrhi& tables);
        [[nodiscard]] bool BuildNextBlas(
            nvrhi::ICommandList* commandList);
        [[nodiscard]] bool BuildOrUpdateTlas(
            nvrhi::ICommandList* commandList,
            const RendererSceneView& scene,
            bool performUpdate);
        [[nodiscard]] bool UploadedResourcesMatch() const;
        [[nodiscard]] bool InstanceTransformsChanged(const RendererSceneView& scene);
        void ClearGeneration();
        void Fail(const char* message);
    };
}
