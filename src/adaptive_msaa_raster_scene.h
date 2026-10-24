#pragma once

#include "adaptive_msaa_scene.h"

#include <nvrhi/nvrhi.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace donut::engine
{
    class Scene;
}

namespace uvsr::adaptive_msaa
{
    // The raster ownership map is written, consumed, replaced, and retired on
    // one explicit queue. A caller must submit the last use before replacing
    // a published snapshot so its retirement query is ordered after that use.
    inline constexpr nvrhi::CommandQueue RasterSceneCommandQueue =
        nvrhi::CommandQueue::Graphics;
    static_assert(sizeof(uint32_t) == 4u);

    using RasterSceneSnapshotVersion = CompactSceneSnapshotVersion;

    struct RasterSceneBuildParameters
    {
        uint64_t topologyEpoch = 0u;
        uint32_t topologyRevision = 0u;
        uint32_t lodRevision = 0u;
        uint32_t deformationRevision = 0u;
        uint32_t materialRevision = 0u;
        bool supportSkinnedMeshesForCompactVisibility = true;
        bool supportAlphaTestedMaterialsForCompactVisibility = false;
    };

    // This device-independent source form keeps signed Donut indices signed
    // through validation. There is exactly one source entry for every local
    // geometry ordinal, including entries that will retain an invalid GPU
    // handle because their topology cannot produce a raster SurfaceKey.
    struct RasterSceneGeometrySource
    {
        std::shared_ptr<const void> stableInstanceOwner;
        int64_t instanceIndex = -1;
        int64_t firstGeometryInstanceIndex = -1;
        uint64_t instanceGeometryCount = 0u;
        uint64_t geometryIndex = 0u;
        uint64_t indexCount = 0u;

        bool geometryPresent = false;
        bool meshUsesTriangleList = false;
        bool geometryUsesTriangleList = false;
        bool compactDeformationSupported = false;
        bool compactMaterialSupported = false;
    };

    struct RasterScenePlanBuildDesc
    {
        RasterSceneBuildParameters parameters;
        uint64_t sceneInstanceCount = 0u;
        uint64_t flattenedGeometryInstanceCount = 0u;
        const RasterSceneGeometrySource* geometries = nullptr;
        size_t geometryCount = 0u;
    };

    // One assignment is retained for every source entry. Unsupported raster
    // entries preserve their exact flattened location but carry the invalid
    // sentinel and a non-None mapping reason. Compact support is deliberately
    // metadata only and never determines raster ownership.
    struct RasterSceneGeometryAssignment
    {
        uint64_t stableInstanceHandle = 0u;
        uint64_t topologyEpoch = 0u;
        uint32_t instanceIndex = InvalidVisibilityWord;
        uint32_t firstGeometryInstanceIndex = InvalidVisibilityWord;
        uint32_t geometryIndex = InvalidVisibilityWord;
        uint32_t flattenedGeometryInstanceIndex = InvalidVisibilityWord;
        uint32_t geometryInstanceHandle = InvalidVisibilityWord;
        uint32_t primitiveCount = 0u;

        uint32_t topologyRevision = 0u;
        uint32_t lodRevision = 0u;
        uint32_t deformationRevision = 0u;
        uint32_t materialRevision = 0u;

        bool rasterSupported = false;
        bool compactDeformationSupported = false;
        bool compactMaterialSupported = false;
        FallbackReason mappingReason = FallbackReason::InvalidSentinel;

        [[nodiscard]] bool Succeeded() const
        {
            return rasterSupported &&
                geometryInstanceHandle != InvalidVisibilityWord &&
                mappingReason == FallbackReason::None;
        }
    };

    struct RasterSceneStatus
    {
        bool eligible = false;
        FallbackReason reason = FallbackReason::ReconstructionFailure;
        uint32_t failingSource = InvalidVisibilityWord;

        [[nodiscard]] bool Succeeded() const
        {
            return eligible && reason == FallbackReason::None;
        }
    };

    struct RasterScenePlan
    {
        RasterSceneSnapshotVersion version;
        std::vector<uint32_t> geometryInstanceHandles;
        std::vector<RasterSceneGeometryAssignment> assignments;
    };

    struct RasterScenePlanBuildResult
    {
        std::shared_ptr<const RasterScenePlan> plan;
        RasterSceneStatus status;

        [[nodiscard]] bool Succeeded() const
        {
            return plan != nullptr && status.Succeeded();
        }
    };

    // The pure planner is copyable so a runtime builder can stage a complete
    // candidate, allocate its GPU buffer, and commit allocator/registry state
    // only after every operation succeeds.
    class RasterScenePlanBuilder final
    {
    public:
        explicit RasterScenePlanBuilder(
            uint32_t geometryInstanceCapacity);

        RasterScenePlanBuilder(const RasterScenePlanBuilder&) = default;
        RasterScenePlanBuilder& operator=(
            const RasterScenePlanBuilder&) = default;
        RasterScenePlanBuilder(RasterScenePlanBuilder&&) noexcept = default;
        RasterScenePlanBuilder& operator=(
            RasterScenePlanBuilder&&) noexcept = default;

        [[nodiscard]] RasterScenePlanBuildResult Build(
            const RasterScenePlanBuildDesc& description);

        [[nodiscard]] FallbackReason ConfigurationReason() const;
        [[nodiscard]] uint32_t GeometryInstanceCapacity() const;
        [[nodiscard]] uint32_t ActiveGeometryInstanceCount() const;
        [[nodiscard]] uint64_t RegistryTopologyEpoch() const;
        [[nodiscard]] uint64_t NextBuildSerial() const;
        [[nodiscard]] bool RequiresEpochReset() const;
        [[nodiscard]] SurfaceResolutionResult ResolveSurfaceKey(
            const GpuVisibilityKey& key,
            const RegistryValidation& validation) const;

    private:
        uint32_t m_GeometryInstanceCapacity = 0u;
        FallbackReason m_ConfigurationReason = FallbackReason::None;
        StableInstanceHandleAllocator m_StableInstances;
        CompactGeometryHandleRegistry m_GeometryHandles;
        std::shared_ptr<const RasterScenePlan> m_LatestPlan;
        uint64_t m_NextBuildSerial = 1u;
    };

    // A topology epoch dominates the per-builder serial. At the same epoch,
    // only a strictly newer successful build may replace published ownership.
    [[nodiscard]] FallbackReason ValidateRasterSceneSnapshotReplacement(
        const RasterSceneSnapshotVersion& candidate,
        const RasterSceneSnapshotVersion& highestPublished);

    // Device-independent monotonic publication state. It intentionally has no
    // clear operation: clearing the active GPU snapshot must never make an old
    // version publishable again.
    class RasterScenePublicationOrder final
    {
    public:
        [[nodiscard]] FallbackReason Accept(
            const RasterSceneSnapshotVersion& candidate);
        [[nodiscard]] bool HasPublishedVersion() const;
        [[nodiscard]] RasterSceneSnapshotVersion
            HighestPublishedVersion() const;

    private:
        RasterSceneSnapshotVersion m_HighestPublishedVersion;
        bool m_HasPublishedVersion = false;
    };

    class RasterSceneSnapshot final
    {
    public:
        ~RasterSceneSnapshot() = default;

        RasterSceneSnapshot(const RasterSceneSnapshot&) = delete;
        RasterSceneSnapshot& operator=(const RasterSceneSnapshot&) = delete;

        [[nodiscard]] const RasterSceneStatus& Status() const;
        [[nodiscard]] uint64_t TopologyEpoch() const;
        [[nodiscard]] uint64_t BuildSerial() const;
        [[nodiscard]] RasterSceneSnapshotVersion Version() const;
        [[nodiscard]] nvrhi::CommandQueue RequiredCommandQueue() const;
        [[nodiscard]] nvrhi::IBuffer* GeometryInstanceHandleMap() const;

        // Call on RequiredCommandQueue() before binding the snapshot. A later
        // pipeline state change or explicit commitBarriers() submits the state
        // transition; this method performs no readback or dual rendering.
        [[nodiscard]] FallbackReason PrepareReadState(
            nvrhi::ICommandList* commandList) const;

        [[nodiscard]] const std::vector<uint32_t>&
            CpuGeometryInstanceHandles() const;
        [[nodiscard]] const std::vector<RasterSceneGeometryAssignment>&
            CpuAssignments() const;

    private:
        friend class RasterSceneBuilder;

        RasterSceneSnapshot() = default;

        RasterSceneStatus m_Status;
        nvrhi::CommandQueue m_RequiredCommandQueue =
            RasterSceneCommandQueue;
        nvrhi::BufferHandle m_GeometryInstanceHandleMap;
        std::shared_ptr<const RasterScenePlan> m_Plan;
    };

    struct RasterSceneBuildResult
    {
        std::shared_ptr<const RasterSceneSnapshot> snapshot;
        RasterSceneStatus status;

        [[nodiscard]] bool Succeeded() const
        {
            return snapshot != nullptr && status.Succeeded();
        }
    };

    // Builds only the lossless four-byte ownership map consumed by the
    // adaptive raster vertex shader. Compact materials, descriptors, geometry
    // reconstruction records, and texture resources are outside this module.
    class RasterSceneBuilder final
    {
    public:
        RasterSceneBuilder(
            nvrhi::IDevice* device,
            uint32_t geometryInstanceCapacity,
            nvrhi::CommandQueue commandQueue = RasterSceneCommandQueue);
        ~RasterSceneBuilder();

        RasterSceneBuilder(const RasterSceneBuilder&) = delete;
        RasterSceneBuilder& operator=(const RasterSceneBuilder&) = delete;

        [[nodiscard]] RasterSceneBuildResult Build(
            nvrhi::ICommandList* commandList,
            const donut::engine::Scene& scene,
            const RasterSceneBuildParameters& parameters);

        [[nodiscard]] FallbackReason ConfigurationReason() const;
        [[nodiscard]] uint32_t GeometryInstanceCapacity() const;
        [[nodiscard]] nvrhi::CommandQueue RequiredCommandQueue() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // Publish/clear calls occur only after the caller has submitted the last
    // Graphics-queue command list that uses the old snapshot. The publisher
    // enqueues an event behind that work and retains the old buffer and CPU
    // plan until completion is observed.
    class RasterSceneSnapshotPublisher final
    {
    public:
        explicit RasterSceneSnapshotPublisher(
            nvrhi::IDevice* device,
            nvrhi::CommandQueue commandQueue = RasterSceneCommandQueue);
        ~RasterSceneSnapshotPublisher();

        RasterSceneSnapshotPublisher(
            const RasterSceneSnapshotPublisher&) = delete;
        RasterSceneSnapshotPublisher& operator=(
            const RasterSceneSnapshotPublisher&) = delete;

        [[nodiscard]] FallbackReason PublishAtFrameBoundary(
            std::shared_ptr<const RasterSceneSnapshot> replacement);
        [[nodiscard]] FallbackReason ClearAtFrameBoundary();

        void PollRetired();
        void DrainRetired();

        [[nodiscard]] std::shared_ptr<const RasterSceneSnapshot>
            ActiveSnapshot() const;
        [[nodiscard]] size_t RetiredSnapshotCount() const;
        [[nodiscard]] nvrhi::CommandQueue RequiredCommandQueue() const;

    private:
        struct RetiredSnapshot
        {
            std::shared_ptr<const RasterSceneSnapshot> snapshot;
            nvrhi::EventQueryHandle completionQuery;
        };

        [[nodiscard]] FallbackReason RetireActiveAtFrameBoundary();

        nvrhi::DeviceHandle m_Device;
        nvrhi::CommandQueue m_CommandQueue = RasterSceneCommandQueue;
        std::shared_ptr<const RasterSceneSnapshot> m_Active;
        std::vector<RetiredSnapshot> m_Retired;
        RasterScenePublicationOrder m_PublicationOrder;
    };
}
