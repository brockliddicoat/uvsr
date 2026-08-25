#pragma once

#include "world_space_representation_contract.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace donut::engine
{
    class MeshInfo;
    class MeshInstance;
    class Scene;
    class SceneGraphNode;
}

namespace uvsr
{
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

        void Reset();
        void Invalidate(WorldSpaceRepresentationInvalidation invalidation);

        // Returns true only when a coherent TLAS is ready for this scene and
        // settings generation. When activeConsumer is false, no new build or
        // update work is submitted.
        [[nodiscard]] bool Update(
            nvrhi::ICommandList* commandList,
            donut::engine::Scene* scene,
            const WorldSpaceRepresentationSettings& settings,
            uint32_t frameIndex,
            bool activeConsumer);

        [[nodiscard]] nvrhi::rt::IAccelStruct*
            GetTopLevelAccelerationStructure() const
        {
            return IsReady() ? m_Tlas.Get() : nullptr;
        }

        [[nodiscard]] nvrhi::IBuffer* GetGeometryIndexMap() const
        {
            return IsReady() ? m_GeometryIndexMap.Get() : nullptr;
        }

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
            std::shared_ptr<donut::engine::MeshInfo> mesh;
            nvrhi::rt::AccelStructDesc description;
            nvrhi::rt::AccelStructHandle accelerationStructure;
            uint64_t topologySignature = 0u;
            uint32_t geometryMapOffset = 0u;
            uint32_t lastSynchronizedFrameIndex = 0u;
            bool dynamic = false;
            bool built = false;
        };

        struct SourceInstanceTopology
        {
            const donut::engine::MeshInstance* instance = nullptr;
            const donut::engine::MeshInfo* mesh = nullptr;
            const donut::engine::SceneGraphNode* node = nullptr;
            uint64_t meshTopologySignature = 0u;
        };

        struct InstanceSnapshot
        {
            const donut::engine::MeshInstance* instance = nullptr;
            const donut::engine::MeshInfo* mesh = nullptr;
            uint32_t instanceId = 0u;
            std::array<float, 12> transform{};
        };

        nvrhi::DeviceHandle m_Device;
        donut::engine::Scene* m_Scene = nullptr;
        WorldSpaceRepresentationSettings m_Settings;
        bool m_HasSettings = false;
        std::vector<BlasRecord> m_BlasRecords;
        std::vector<std::shared_ptr<donut::engine::MeshInstance>> m_Instances;
        std::vector<SourceInstanceTopology> m_SourceInstanceTopology;
        std::vector<InstanceSnapshot> m_InstanceSnapshots;
        std::vector<uint32_t> m_GeometryIndexMapUpload;
        nvrhi::BufferHandle m_GeometryIndexMap;
        bool m_GeometryIndexMapUploaded = false;
        nvrhi::rt::AccelStructHandle m_Tlas;
        size_t m_NextBlas = 0u;
        WorldSpaceRepresentationStatus m_Status;
        bool m_ReportedFailure = false;

        [[nodiscard]] bool BeginGeneration(
            donut::engine::Scene* scene,
            const WorldSpaceRepresentationSettings& settings);
        [[nodiscard]] bool BuildNextBlas(
            nvrhi::ICommandList* commandList,
            uint32_t frameIndex);
        [[nodiscard]] bool BuildOrUpdateTlas(
            nvrhi::ICommandList* commandList,
            bool performUpdate);
        [[nodiscard]] bool UpdateDynamicBlases(
            nvrhi::ICommandList* commandList,
            uint32_t frameIndex,
            bool forceAll,
            bool& anyUpdated);
        [[nodiscard]] bool TopologyMatches() const;
        [[nodiscard]] bool InstanceTransformsChanged() const;
        void Fail(const char* message);
    };
}
