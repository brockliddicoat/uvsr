#include "adaptive_msaa_raster_scene.h"

// Implementation provenance:
// - UVSR-native resource flow informed by The Forge Visibility_Buffer sample
//   at commit a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0),
//   especially its explicit visibility-resource production and consumption.
//   Source: https://github.com/ConfettiFX/The-Forge/tree/
//   a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b/Examples_3/Visibility_Buffer
// - Scene indexing uses only public APIs from UVSR's pinned Donut dependency
//   (MIT); Donut is not modified.
// - Written for UVSR source base 578496047c4753077e36f39a68df8c2c323cee58.
// No external source text is copied into this file.

#include <donut/engine/Scene.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <utility>

namespace
{
    using namespace donut::engine;
    using namespace uvsr::adaptive_msaa;

    [[nodiscard]] bool TryAdd(
        uint64_t first,
        uint64_t second,
        uint64_t& result)
    {
        if (first > std::numeric_limits<uint64_t>::max() - second)
            return false;
        result = first + second;
        return true;
    }

    [[nodiscard]] bool TryMultiply(
        uint64_t first,
        uint64_t second,
        uint64_t& result)
    {
        if (first != 0u &&
            second > std::numeric_limits<uint64_t>::max() / first)
        {
            return false;
        }
        result = first * second;
        return true;
    }

    [[nodiscard]] uint32_t NarrowSourceIndex(size_t sourceIndex)
    {
        return sourceIndex < InvalidVisibilityWord
            ? static_cast<uint32_t>(sourceIndex)
            : InvalidVisibilityWord;
    }

    [[nodiscard]] RasterSceneStatus MakeStatus(
        FallbackReason reason,
        uint32_t failingSource = InvalidVisibilityWord)
    {
        RasterSceneStatus status;
        status.eligible = reason == FallbackReason::None;
        status.reason = reason;
        status.failingSource = failingSource;
        return status;
    }

    [[nodiscard]] std::shared_ptr<const void> MakeOwnerToken(
        const std::shared_ptr<MeshInstance>& instance)
    {
        return std::shared_ptr<const void>(
            instance,
            static_cast<const void*>(instance.get()));
    }

    struct InstanceShape
    {
        std::shared_ptr<const void> owner;
        uint32_t firstGeometryInstanceIndex = 0u;
        uint32_t geometryCount = 0u;
        uint32_t seenGeometryCount = 0u;
    };
}

namespace uvsr::adaptive_msaa
{
    RasterScenePlanBuilder::RasterScenePlanBuilder(
        uint32_t geometryInstanceCapacity)
        : m_GeometryInstanceCapacity(geometryInstanceCapacity)
        , m_GeometryHandles(geometryInstanceCapacity)
    {
        if (geometryInstanceCapacity == 0u ||
            geometryInstanceCapacity > GeometryInstanceSlotCount)
        {
            m_ConfigurationReason = FallbackReason::CapacityOverflow;
        }
    }

    RasterScenePlanBuildResult RasterScenePlanBuilder::Build(
        const RasterScenePlanBuildDesc& description)
    {
        RasterScenePlanBuildResult result;
        const auto Fail = [&result](
            FallbackReason reason,
            uint32_t failingSource = InvalidVisibilityWord)
        {
            result.plan.reset();
            result.status = MakeStatus(reason, failingSource);
            return result;
        };

        if (m_ConfigurationReason != FallbackReason::None)
            return Fail(m_ConfigurationReason);
        if (description.parameters.topologyEpoch == 0u)
            return Fail(FallbackReason::TopologyEpochMismatch);
        if (m_NextBuildSerial == 0u)
            return Fail(FallbackReason::CapacityOverflow);
        if (description.sceneInstanceCount == 0u ||
            description.flattenedGeometryInstanceCount == 0u)
        {
            return Fail(FallbackReason::ReconstructionFailure);
        }
        if (description.sceneInstanceCount >= InvalidVisibilityWord ||
            description.flattenedGeometryInstanceCount >=
                InvalidVisibilityWord ||
            description.geometryCount >= InvalidVisibilityWord)
        {
            return Fail(FallbackReason::CapacityOverflow);
        }
        if (description.geometryCount !=
            description.flattenedGeometryInstanceCount)
        {
            return Fail(FallbackReason::TransientMappingFailure);
        }
        if (!description.geometries)
            return Fail(FallbackReason::TransientMappingFailure);

        try
        {
            std::vector<RasterSceneGeometryAssignment> assignments(
                description.geometryCount);
            std::vector<uint32_t> geometryInstanceHandles(
                description.geometryCount,
                InvalidVisibilityWord);
            std::vector<uint8_t> occupiedFlattenedSlots(
                description.geometryCount,
                uint8_t(0u));

            std::map<uint32_t, InstanceShape> instances;
            std::map<
                std::shared_ptr<const void>,
                uint32_t,
                std::owner_less<std::shared_ptr<const void>>> ownerInstances;

            for (size_t sourceIndex = 0u;
                sourceIndex < description.geometryCount;
                ++sourceIndex)
            {
                const RasterSceneGeometrySource& source =
                    description.geometries[sourceIndex];
                const uint32_t failingSource =
                    NarrowSourceIndex(sourceIndex);

                if (!source.stableInstanceOwner ||
                    source.instanceIndex < 0 ||
                    source.firstGeometryInstanceIndex < 0)
                {
                    return Fail(
                        FallbackReason::TransientMappingFailure,
                        failingSource);
                }
                if (uint64_t(source.instanceIndex) >=
                        description.sceneInstanceCount ||
                    uint64_t(source.instanceIndex) >=
                        InvalidVisibilityWord ||
                    uint64_t(source.firstGeometryInstanceIndex) >=
                        InvalidVisibilityWord)
                {
                    return Fail(
                        FallbackReason::TransientMappingFailure,
                        failingSource);
                }
                if (source.instanceGeometryCount == 0u ||
                    source.instanceGeometryCount >=
                        InvalidVisibilityWord ||
                    source.geometryIndex >=
                        source.instanceGeometryCount ||
                    source.geometryIndex >= InvalidVisibilityWord ||
                    source.indexCount >
                        std::numeric_limits<uint32_t>::max())
                {
                    return Fail(
                        FallbackReason::CapacityOverflow,
                        failingSource);
                }

                const uint64_t firstGeometryInstanceIndex =
                    uint64_t(source.firstGeometryInstanceIndex);
                uint64_t instanceRangeEnd = 0u;
                uint64_t flattenedGeometryInstanceIndex = 0u;
                if (!TryAdd(
                        firstGeometryInstanceIndex,
                        source.instanceGeometryCount,
                        instanceRangeEnd) ||
                    !TryAdd(
                        firstGeometryInstanceIndex,
                        source.geometryIndex,
                        flattenedGeometryInstanceIndex))
                {
                    return Fail(
                        FallbackReason::CapacityOverflow,
                        failingSource);
                }
                if (instanceRangeEnd >
                        description.flattenedGeometryInstanceCount ||
                    flattenedGeometryInstanceIndex >=
                        description.flattenedGeometryInstanceCount ||
                    flattenedGeometryInstanceIndex >=
                        InvalidVisibilityWord)
                {
                    return Fail(
                        FallbackReason::TransientMappingFailure,
                        failingSource);
                }

                const uint32_t instanceIndex =
                    static_cast<uint32_t>(source.instanceIndex);
                const uint32_t firstIndex =
                    static_cast<uint32_t>(firstGeometryInstanceIndex);
                const uint32_t geometryCount =
                    static_cast<uint32_t>(
                        source.instanceGeometryCount);
                const uint32_t geometryIndex =
                    static_cast<uint32_t>(source.geometryIndex);
                const uint32_t flattenedIndex =
                    static_cast<uint32_t>(
                        flattenedGeometryInstanceIndex);

                if (occupiedFlattenedSlots[flattenedIndex] != 0u)
                {
                    return Fail(
                        FallbackReason::TransientMappingCollision,
                        failingSource);
                }
                occupiedFlattenedSlots[flattenedIndex] = 1u;

                auto [instanceIterator, insertedInstance] =
                    instances.emplace(
                        instanceIndex,
                        InstanceShape{
                            source.stableInstanceOwner,
                            firstIndex,
                            geometryCount,
                            0u
                        });
                if (!insertedInstance)
                {
                    const InstanceShape& shape =
                        instanceIterator->second;
                    const bool sameOwner =
                        !shape.owner.owner_before(
                            source.stableInstanceOwner) &&
                        !source.stableInstanceOwner.owner_before(
                            shape.owner);
                    if (!sameOwner ||
                        shape.firstGeometryInstanceIndex != firstIndex ||
                        shape.geometryCount != geometryCount)
                    {
                        return Fail(
                            FallbackReason::TransientMappingCollision,
                            failingSource);
                    }
                }
                ++instanceIterator->second.seenGeometryCount;

                auto [ownerIterator, insertedOwner] =
                    ownerInstances.emplace(
                        source.stableInstanceOwner,
                        instanceIndex);
                if (!insertedOwner &&
                    ownerIterator->second != instanceIndex)
                {
                    return Fail(
                        FallbackReason::TransientMappingCollision,
                        failingSource);
                }

                RasterSceneGeometryAssignment& assignment =
                    assignments[sourceIndex];
                assignment.topologyEpoch =
                    description.parameters.topologyEpoch;
                assignment.instanceIndex = instanceIndex;
                assignment.firstGeometryInstanceIndex = firstIndex;
                assignment.geometryIndex = geometryIndex;
                assignment.flattenedGeometryInstanceIndex =
                    flattenedIndex;
                assignment.topologyRevision =
                    description.parameters.topologyRevision;
                assignment.lodRevision =
                    description.parameters.lodRevision;
                assignment.deformationRevision =
                    description.parameters.deformationRevision;
                assignment.materialRevision =
                    description.parameters.materialRevision;
                assignment.compactDeformationSupported =
                    source.compactDeformationSupported;
                assignment.compactMaterialSupported =
                    source.compactMaterialSupported;

                if (!source.geometryPresent)
                {
                    assignment.mappingReason =
                        FallbackReason::ReconstructionFailure;
                }
                else if (!source.meshUsesTriangleList ||
                    !source.geometryUsesTriangleList)
                {
                    assignment.mappingReason =
                        FallbackReason::FormatUnsupported;
                }
                else if (source.indexCount == 0u ||
                    source.indexCount % 3u != 0u)
                {
                    assignment.mappingReason =
                        FallbackReason::ReconstructionFailure;
                }
                else
                {
                    assignment.rasterSupported = true;
                    assignment.mappingReason = FallbackReason::None;
                    assignment.primitiveCount =
                        static_cast<uint32_t>(source.indexCount / 3u);
                }
            }

            for (uint8_t occupied : occupiedFlattenedSlots)
            {
                if (occupied == 0u)
                {
                    return Fail(
                        FallbackReason::TransientMappingFailure);
                }
            }
            for (const auto& [instanceIndex, shape] : instances)
            {
                (void)instanceIndex;
                if (shape.seenGeometryCount != shape.geometryCount)
                {
                    return Fail(
                        FallbackReason::TransientMappingFailure);
                }
            }

            StableInstanceHandleAllocator candidateStableInstances =
                m_StableInstances;
            CompactGeometryHandleRegistry candidateGeometryHandles =
                m_GeometryHandles;
            candidateStableInstances.PruneExpired();

            std::vector<CompactGeometryIdentityDesc> identities;
            std::vector<size_t> supportedAssignmentIndices;
            identities.reserve(assignments.size());
            supportedAssignmentIndices.reserve(assignments.size());

            for (size_t sourceIndex = 0u;
                sourceIndex < description.geometryCount;
                ++sourceIndex)
            {
                const StableInstanceHandleResult stable =
                    candidateStableInstances.Resolve(
                        description.geometries[sourceIndex]
                            .stableInstanceOwner);
                if (!stable.Succeeded())
                {
                    return Fail(
                        stable.reason,
                        NarrowSourceIndex(sourceIndex));
                }

                RasterSceneGeometryAssignment& assignment =
                    assignments[sourceIndex];
                assignment.stableInstanceHandle =
                    stable.stableInstanceHandle;
                if (!assignment.rasterSupported)
                    continue;

                CompactGeometryIdentityDesc identity;
                identity.stableInstanceHandle =
                    assignment.stableInstanceHandle;
                identity.geometryIndex = assignment.geometryIndex;
                identity.topologyRevision =
                    assignment.topologyRevision;
                identity.lodRevision = assignment.lodRevision;
                identity.deformationRevision =
                    assignment.deformationRevision;
                identity.materialRevision =
                    assignment.materialRevision;

                // The shared registry currently validates these flags during
                // reconciliation. Raster ownership must not be gated by
                // compact eligibility, so the registry receives true while
                // the exact support flags remain in the CPU assignment.
                identity.deformationSupported = true;
                identity.materialSupported = true;
                identities.push_back(identity);
                supportedAssignmentIndices.push_back(sourceIndex);
            }

            const CompactGeometryHandleReconcileResult reconciliation =
                candidateGeometryHandles.Reconcile(
                    description.parameters.topologyEpoch,
                    identities.empty() ? nullptr : identities.data(),
                    identities.size());
            if (!reconciliation.Succeeded())
            {
                if (candidateGeometryHandles.RequiresEpochReset())
                {
                    // The candidate registry retains the last valid mappings
                    // and adds only the mandatory wider-epoch reset latch.
                    m_GeometryHandles =
                        std::move(candidateGeometryHandles);
                }
                return Fail(reconciliation.reason);
            }
            if (reconciliation.assignments.size() !=
                supportedAssignmentIndices.size())
            {
                return Fail(FallbackReason::TransientMappingFailure);
            }

            for (size_t supportedIndex = 0u;
                supportedIndex < supportedAssignmentIndices.size();
                ++supportedIndex)
            {
                const size_t assignmentIndex =
                    supportedAssignmentIndices[supportedIndex];
                RasterSceneGeometryAssignment& assignment =
                    assignments[assignmentIndex];
                const CompactGeometryHandleAssignment& reconciled =
                    reconciliation.assignments[supportedIndex];
                const uint32_t handle =
                    reconciled.geometryInstanceHandle;
                const DecodedGeometryInstanceHandle decoded =
                    DecodeGeometryInstanceHandle(handle);
                const RegistryLookupResult lookup =
                    candidateGeometryHandles.Inspect(handle);
                if (!decoded.Succeeded() || !lookup.Succeeded())
                {
                    return Fail(
                        decoded.Succeeded()
                            ? lookup.reason
                            : decoded.reason,
                        NarrowSourceIndex(assignmentIndex));
                }
                if (lookup.entry.stableInstanceHandle !=
                        assignment.stableInstanceHandle ||
                    lookup.entry.geometryIndex !=
                        assignment.geometryIndex ||
                    lookup.entry.topologyEpoch !=
                        assignment.topologyEpoch ||
                    lookup.entry.lodRevision !=
                        assignment.lodRevision ||
                    lookup.entry.deformationRevision !=
                        assignment.deformationRevision ||
                    lookup.entry.materialRevision !=
                        assignment.materialRevision)
                {
                    return Fail(
                        FallbackReason::TransientMappingFailure,
                        NarrowSourceIndex(assignmentIndex));
                }

                const uint32_t flattenedIndex =
                    assignment.flattenedGeometryInstanceIndex;
                if (geometryInstanceHandles[flattenedIndex] !=
                    InvalidVisibilityWord)
                {
                    return Fail(
                        FallbackReason::TransientMappingCollision,
                        NarrowSourceIndex(assignmentIndex));
                }
                geometryInstanceHandles[flattenedIndex] = handle;
                assignment.geometryInstanceHandle = handle;
            }

            auto mutablePlan = std::make_shared<RasterScenePlan>();
            mutablePlan->version = {
                description.parameters.topologyEpoch,
                m_NextBuildSerial
            };
            mutablePlan->geometryInstanceHandles =
                std::move(geometryInstanceHandles);
            mutablePlan->assignments = std::move(assignments);

            m_StableInstances = std::move(candidateStableInstances);
            m_GeometryHandles = std::move(candidateGeometryHandles);
            m_LatestPlan = mutablePlan;
            m_NextBuildSerial = m_NextBuildSerial ==
                    std::numeric_limits<uint64_t>::max()
                ? 0u
                : m_NextBuildSerial + 1u;

            result.plan = std::move(mutablePlan);
            result.status = MakeStatus(FallbackReason::None);
            return result;
        }
        catch (const std::bad_alloc&)
        {
            return Fail(FallbackReason::CapacityOverflow);
        }
    }

    FallbackReason RasterScenePlanBuilder::ConfigurationReason() const
    {
        return m_ConfigurationReason;
    }

    uint32_t RasterScenePlanBuilder::GeometryInstanceCapacity() const
    {
        return m_GeometryInstanceCapacity;
    }

    uint32_t RasterScenePlanBuilder::ActiveGeometryInstanceCount() const
    {
        return m_GeometryHandles.ActiveEntryCount();
    }

    uint64_t RasterScenePlanBuilder::RegistryTopologyEpoch() const
    {
        return m_GeometryHandles.TopologyEpoch();
    }

    uint64_t RasterScenePlanBuilder::NextBuildSerial() const
    {
        return m_NextBuildSerial;
    }

    bool RasterScenePlanBuilder::RequiresEpochReset() const
    {
        return m_GeometryHandles.RequiresEpochReset();
    }

    SurfaceResolutionResult RasterScenePlanBuilder::ResolveSurfaceKey(
        const GpuVisibilityKey& key,
        const RegistryValidation& validation) const
    {
        RegistryValidation ownershipValidation = validation;
        ownershipValidation.requireSupportedDeformation = false;
        ownershipValidation.requireSupportedMaterial = false;
        SurfaceResolutionResult result =
            m_GeometryHandles.ResolveSurfaceKey(
                key,
                ownershipValidation);
        if (!result.Succeeded())
            return result;

        if (!m_LatestPlan)
        {
            result.reason = FallbackReason::TransientMappingFailure;
            return result;
        }
        const auto assignment = std::find_if(
            m_LatestPlan->assignments.begin(),
            m_LatestPlan->assignments.end(),
            [&key](const RasterSceneGeometryAssignment& candidate)
            {
                return candidate.geometryInstanceHandle ==
                    key.geometryInstanceHandle;
            });
        if (assignment == m_LatestPlan->assignments.end() ||
            !assignment->Succeeded() ||
            assignment->stableInstanceHandle !=
                result.key.stableInstanceHandle ||
            assignment->geometryIndex != result.key.geometryIndex)
        {
            result.reason = FallbackReason::TransientMappingFailure;
            return result;
        }

        result.metadata.deformationSupported =
            assignment->compactDeformationSupported;
        result.metadata.materialSupported =
            assignment->compactMaterialSupported;
        if (validation.requireSupportedDeformation &&
            !result.metadata.deformationSupported)
        {
            result.reason = FallbackReason::UnsupportedDeformation;
        }
        else if (validation.requireSupportedMaterial &&
            !result.metadata.materialSupported)
        {
            result.reason = FallbackReason::UnsupportedMaterial;
        }
        return result;
    }

    FallbackReason ValidateRasterSceneSnapshotReplacement(
        const RasterSceneSnapshotVersion& candidate,
        const RasterSceneSnapshotVersion& highestPublished)
    {
        return ValidateCompactSceneSnapshotReplacement(
            candidate,
            highestPublished);
    }

    FallbackReason RasterScenePublicationOrder::Accept(
        const RasterSceneSnapshotVersion& candidate)
    {
        if (candidate.topologyEpoch == 0u ||
            candidate.buildSerial == 0u)
        {
            return FallbackReason::TransientMappingFailure;
        }
        if (m_HasPublishedVersion)
        {
            const FallbackReason reason =
                ValidateRasterSceneSnapshotReplacement(
                    candidate,
                    m_HighestPublishedVersion);
            if (reason != FallbackReason::None)
                return reason;
        }

        m_HighestPublishedVersion = candidate;
        m_HasPublishedVersion = true;
        return FallbackReason::None;
    }

    bool RasterScenePublicationOrder::HasPublishedVersion() const
    {
        return m_HasPublishedVersion;
    }

    RasterSceneSnapshotVersion
        RasterScenePublicationOrder::HighestPublishedVersion() const
    {
        return m_HighestPublishedVersion;
    }

    const RasterSceneStatus& RasterSceneSnapshot::Status() const
    {
        return m_Status;
    }

    uint64_t RasterSceneSnapshot::TopologyEpoch() const
    {
        return m_Plan ? m_Plan->version.topologyEpoch : 0u;
    }

    uint64_t RasterSceneSnapshot::BuildSerial() const
    {
        return m_Plan ? m_Plan->version.buildSerial : 0u;
    }

    RasterSceneSnapshotVersion RasterSceneSnapshot::Version() const
    {
        return m_Plan
            ? m_Plan->version
            : RasterSceneSnapshotVersion{};
    }

    nvrhi::CommandQueue RasterSceneSnapshot::RequiredCommandQueue() const
    {
        return m_RequiredCommandQueue;
    }

    nvrhi::IBuffer*
        RasterSceneSnapshot::GeometryInstanceHandleMap() const
    {
        return m_GeometryInstanceHandleMap;
    }

    FallbackReason RasterSceneSnapshot::PrepareReadState(
        nvrhi::ICommandList* commandList) const
    {
        if (!commandList || !m_Status.Succeeded() || !m_Plan ||
            !m_GeometryInstanceHandleMap ||
            m_Plan->geometryInstanceHandles.empty())
        {
            return FallbackReason::ReconstructionFailure;
        }

        const nvrhi::BufferDesc& bufferDesc =
            m_GeometryInstanceHandleMap->getDesc();
        uint64_t requiredByteSize = 0u;
        if (!TryMultiply(
                m_Plan->geometryInstanceHandles.size(),
                sizeof(uint32_t),
                requiredByteSize) ||
            bufferDesc.structStride != sizeof(uint32_t) ||
            bufferDesc.byteSize != requiredByteSize)
        {
            return FallbackReason::FormatUnsupported;
        }

        commandList->setBufferState(
            m_GeometryInstanceHandleMap,
            nvrhi::ResourceStates::ShaderResource);
        return FallbackReason::None;
    }

    const std::vector<uint32_t>&
        RasterSceneSnapshot::CpuGeometryInstanceHandles() const
    {
        return m_Plan->geometryInstanceHandles;
    }

    const std::vector<RasterSceneGeometryAssignment>&
        RasterSceneSnapshot::CpuAssignments() const
    {
        return m_Plan->assignments;
    }

    struct RasterSceneBuilder::Impl
    {
        Impl(
            nvrhi::IDevice* sourceDevice,
            uint32_t geometryInstanceCapacity,
            nvrhi::CommandQueue sourceCommandQueue)
            : device(sourceDevice)
            , commandQueue(sourceCommandQueue)
            , planBuilder(geometryInstanceCapacity)
        {
            if (!sourceDevice)
            {
                configurationReason =
                    FallbackReason::ReconstructionFailure;
            }
            else if (sourceCommandQueue != RasterSceneCommandQueue)
            {
                configurationReason = FallbackReason::FormatUnsupported;
            }
            else
            {
                configurationReason =
                    planBuilder.ConfigurationReason();
            }
        }

        nvrhi::DeviceHandle device;
        nvrhi::CommandQueue commandQueue = RasterSceneCommandQueue;
        FallbackReason configurationReason = FallbackReason::None;
        RasterScenePlanBuilder planBuilder;
    };

    RasterSceneBuilder::RasterSceneBuilder(
        nvrhi::IDevice* device,
        uint32_t geometryInstanceCapacity,
        nvrhi::CommandQueue commandQueue)
        : m_Impl(std::make_unique<Impl>(
            device,
            geometryInstanceCapacity,
            commandQueue))
    {
    }

    RasterSceneBuilder::~RasterSceneBuilder() = default;

    RasterSceneBuildResult RasterSceneBuilder::Build(
        nvrhi::ICommandList* commandList,
        const donut::engine::Scene& scene,
        const RasterSceneBuildParameters& parameters)
    {
        RasterSceneBuildResult result;
        const auto Fail = [&result](
            FallbackReason reason,
            uint32_t failingSource = InvalidVisibilityWord)
        {
            result.snapshot.reset();
            result.status = MakeStatus(reason, failingSource);
            return result;
        };

        if (!m_Impl ||
            m_Impl->configurationReason != FallbackReason::None)
        {
            return Fail(m_Impl
                ? m_Impl->configurationReason
                : FallbackReason::ReconstructionFailure);
        }
        if (!commandList)
            return Fail(FallbackReason::ReconstructionFailure);

        try
        {
            const std::shared_ptr<SceneGraph> sceneGraph =
                scene.GetSceneGraph();
            if (!sceneGraph || !scene.GetInstanceBuffer() ||
                sceneGraph->HasPendingStructureChanges())
            {
                return Fail(FallbackReason::TransientMappingFailure);
            }

            const auto& sceneInstances =
                sceneGraph->GetMeshInstances();
            const uint64_t geometryInstanceCount =
                sceneGraph->GetGeometryInstancesCount();
            if (sceneInstances.empty() ||
                sceneInstances.size() >= InvalidVisibilityWord ||
                geometryInstanceCount == 0u)
            {
                return Fail(sceneInstances.size() >=
                        InvalidVisibilityWord
                    ? FallbackReason::CapacityOverflow
                    : FallbackReason::ReconstructionFailure);
            }

            std::vector<RasterSceneGeometrySource> sources;
            if (geometryInstanceCount >= InvalidVisibilityWord ||
                geometryInstanceCount >
                    std::numeric_limits<size_t>::max())
            {
                return Fail(FallbackReason::CapacityOverflow);
            }
            sources.reserve(
                static_cast<size_t>(geometryInstanceCount));

            std::vector<uint8_t> seenInstanceIndices(
                sceneInstances.size(),
                uint8_t(0u));
            std::map<
                std::shared_ptr<const void>,
                uint32_t,
                std::owner_less<std::shared_ptr<const void>>>
                    ownerInstances;
            uint64_t summedGeometryCount = 0u;

            for (const std::shared_ptr<MeshInstance>& instance :
                sceneInstances)
            {
                if (!instance || !instance->GetMesh())
                    return Fail(FallbackReason::TransientMappingFailure);
                if (instance->GetInstanceIndex() < 0 ||
                    instance->GetGeometryInstanceIndex() < 0 ||
                    uint64_t(instance->GetInstanceIndex()) >=
                        sceneInstances.size())
                {
                    return Fail(FallbackReason::TransientMappingFailure);
                }

                const uint32_t instanceIndex =
                    static_cast<uint32_t>(
                        instance->GetInstanceIndex());
                if (seenInstanceIndices[instanceIndex] != 0u)
                {
                    return Fail(
                        FallbackReason::TransientMappingCollision);
                }
                seenInstanceIndices[instanceIndex] = 1u;

                const std::shared_ptr<const void> owner =
                    MakeOwnerToken(instance);
                auto [ownerIterator, insertedOwner] =
                    ownerInstances.emplace(owner, instanceIndex);
                if (!insertedOwner ||
                    ownerIterator->second != instanceIndex)
                {
                    return Fail(
                        FallbackReason::TransientMappingCollision);
                }

                const std::shared_ptr<MeshInfo>& mesh =
                    instance->GetMesh();
                const uint64_t instanceGeometryCount =
                    mesh->geometries.size();
                uint64_t instanceRangeEnd = 0u;
                if (instanceGeometryCount >= InvalidVisibilityWord ||
                    !TryAdd(
                        uint64_t(
                            instance->GetGeometryInstanceIndex()),
                        instanceGeometryCount,
                        instanceRangeEnd) ||
                    !TryAdd(
                        summedGeometryCount,
                        instanceGeometryCount,
                        summedGeometryCount))
                {
                    return Fail(FallbackReason::CapacityOverflow);
                }
                if (instanceRangeEnd > geometryInstanceCount)
                {
                    return Fail(
                        FallbackReason::TransientMappingFailure);
                }

                const bool skinned =
                    mesh->skinPrototype != nullptr;
                const bool morphTarget =
                    mesh->isMorphTargetAnimationMesh ||
                    (mesh->skinPrototype &&
                        mesh->skinPrototype
                            ->isMorphTargetAnimationMesh);
                const bool compactDeformationSupported =
                    !morphTarget &&
                    (!skinned || parameters
                        .supportSkinnedMeshesForCompactVisibility);

                for (size_t geometryIndex = 0u;
                    geometryIndex < mesh->geometries.size();
                    ++geometryIndex)
                {
                    const std::shared_ptr<MeshGeometry>& geometry =
                        mesh->geometries[geometryIndex];

                    RasterSceneGeometrySource source;
                    source.stableInstanceOwner = owner;
                    source.instanceIndex =
                        instance->GetInstanceIndex();
                    source.firstGeometryInstanceIndex =
                        instance->GetGeometryInstanceIndex();
                    source.instanceGeometryCount =
                        instanceGeometryCount;
                    source.geometryIndex = geometryIndex;
                    source.geometryPresent = geometry != nullptr;
                    source.meshUsesTriangleList =
                        mesh->type == MeshType::Triangles;
                    source.geometryUsesTriangleList = geometry &&
                        geometry->type ==
                            MeshGeometryPrimitiveType::Triangles;
                    source.indexCount = geometry
                        ? geometry->numIndices
                        : 0u;
                    source.compactDeformationSupported =
                        compactDeformationSupported;

                    const Material* material = geometry
                        ? geometry->material.get()
                        : nullptr;
                    source.compactMaterialSupported = material &&
                        (material->domain == MaterialDomain::Opaque ||
                         (parameters
                            .supportAlphaTestedMaterialsForCompactVisibility &&
                          material->domain ==
                            MaterialDomain::AlphaTested));
                    sources.push_back(std::move(source));
                }
            }

            if (std::find(
                    seenInstanceIndices.begin(),
                    seenInstanceIndices.end(),
                    uint8_t(0u)) != seenInstanceIndices.end() ||
                summedGeometryCount != geometryInstanceCount ||
                sources.size() != geometryInstanceCount)
            {
                return Fail(FallbackReason::TransientMappingFailure);
            }

            RasterScenePlanBuilder candidatePlanBuilder =
                m_Impl->planBuilder;
            RasterScenePlanBuildDesc planDescription;
            planDescription.parameters = parameters;
            planDescription.sceneInstanceCount =
                sceneInstances.size();
            planDescription.flattenedGeometryInstanceCount =
                geometryInstanceCount;
            planDescription.geometries = sources.data();
            planDescription.geometryCount = sources.size();

            const RasterScenePlanBuildResult planResult =
                candidatePlanBuilder.Build(planDescription);
            if (!planResult.Succeeded())
            {
                if (candidatePlanBuilder.RequiresEpochReset())
                {
                    m_Impl->planBuilder =
                        std::move(candidatePlanBuilder);
                }
                return Fail(
                    planResult.status.reason,
                    planResult.status.failingSource);
            }

            uint64_t byteSize = 0u;
            if (!TryMultiply(
                    planResult.plan
                        ->geometryInstanceHandles.size(),
                    sizeof(uint32_t),
                    byteSize) ||
                byteSize == 0u ||
                byteSize > std::numeric_limits<size_t>::max())
            {
                return Fail(FallbackReason::CapacityOverflow);
            }

            nvrhi::BufferDesc bufferDesc;
            bufferDesc.byteSize = byteSize;
            bufferDesc.debugName =
                "Adaptive MSAA Raster Geometry Instance Handles";
            bufferDesc.structStride = sizeof(uint32_t);
            bufferDesc.initialState =
                nvrhi::ResourceStates::ShaderResource;
            bufferDesc.keepInitialState = true;
            nvrhi::BufferHandle handleMap =
                m_Impl->device->createBuffer(bufferDesc);
            if (!handleMap)
                return Fail(FallbackReason::ReconstructionFailure);

            commandList->writeBuffer(
                handleMap,
                planResult.plan
                    ->geometryInstanceHandles.data(),
                static_cast<size_t>(byteSize));

            auto snapshot = std::shared_ptr<RasterSceneSnapshot>(
                new RasterSceneSnapshot());
            snapshot->m_Status = MakeStatus(FallbackReason::None);
            snapshot->m_RequiredCommandQueue =
                m_Impl->commandQueue;
            snapshot->m_GeometryInstanceHandleMap =
                std::move(handleMap);
            snapshot->m_Plan = planResult.plan;

            m_Impl->planBuilder =
                std::move(candidatePlanBuilder);
            result.snapshot = std::move(snapshot);
            result.status = MakeStatus(FallbackReason::None);
            return result;
        }
        catch (const std::bad_alloc&)
        {
            return Fail(FallbackReason::CapacityOverflow);
        }
    }

    FallbackReason RasterSceneBuilder::ConfigurationReason() const
    {
        return m_Impl
            ? m_Impl->configurationReason
            : FallbackReason::ReconstructionFailure;
    }

    uint32_t RasterSceneBuilder::GeometryInstanceCapacity() const
    {
        return m_Impl
            ? m_Impl->planBuilder.GeometryInstanceCapacity()
            : 0u;
    }

    nvrhi::CommandQueue RasterSceneBuilder::RequiredCommandQueue() const
    {
        return m_Impl
            ? m_Impl->commandQueue
            : RasterSceneCommandQueue;
    }

    RasterSceneSnapshotPublisher::RasterSceneSnapshotPublisher(
        nvrhi::IDevice* device,
        nvrhi::CommandQueue commandQueue)
        : m_Device(device)
        , m_CommandQueue(commandQueue)
    {
    }

    RasterSceneSnapshotPublisher::~RasterSceneSnapshotPublisher()
    {
        if (m_Active)
        {
            const FallbackReason reason =
                RetireActiveAtFrameBoundary();
            if (reason != FallbackReason::None && m_Device)
            {
                m_Device->waitForIdle();
                m_Active.reset();
            }
        }
        DrainRetired();
    }

    FallbackReason
        RasterSceneSnapshotPublisher::PublishAtFrameBoundary(
            std::shared_ptr<const RasterSceneSnapshot> replacement)
    {
        if (m_CommandQueue != RasterSceneCommandQueue ||
            (replacement &&
                replacement->RequiredCommandQueue() !=
                    RasterSceneCommandQueue))
        {
            return FallbackReason::FormatUnsupported;
        }
        if (!m_Device || !replacement ||
            !replacement->Status().Succeeded() ||
            !replacement->GeometryInstanceHandleMap())
        {
            return FallbackReason::ReconstructionFailure;
        }
        if (replacement == m_Active)
            return FallbackReason::None;

        RasterScenePublicationOrder candidateOrder =
            m_PublicationOrder;
        FallbackReason reason = candidateOrder.Accept(
            replacement->Version());
        if (reason != FallbackReason::None)
            return reason;

        reason = RetireActiveAtFrameBoundary();
        if (reason != FallbackReason::None)
            return reason;

        m_PublicationOrder = std::move(candidateOrder);
        m_Active = std::move(replacement);
        return FallbackReason::None;
    }

    FallbackReason
        RasterSceneSnapshotPublisher::ClearAtFrameBoundary()
    {
        if (m_CommandQueue != RasterSceneCommandQueue)
            return FallbackReason::FormatUnsupported;
        return RetireActiveAtFrameBoundary();
    }

    FallbackReason
        RasterSceneSnapshotPublisher::RetireActiveAtFrameBoundary()
    {
        if (!m_Active)
            return FallbackReason::None;
        if (!m_Device ||
            m_CommandQueue != RasterSceneCommandQueue)
        {
            return FallbackReason::ReconstructionFailure;
        }

        nvrhi::EventQueryHandle completionQuery =
            m_Device->createEventQuery();
        if (!completionQuery)
            return FallbackReason::ReconstructionFailure;

        try
        {
            m_Retired.push_back({ m_Active, completionQuery });
        }
        catch (const std::bad_alloc&)
        {
            return FallbackReason::CapacityOverflow;
        }

        // The signal is enqueued after the caller-submitted last use. The
        // retained shared_ptr owns both the buffer and immutable CPU plan.
        m_Device->setEventQuery(completionQuery, m_CommandQueue);
        m_Active.reset();
        return FallbackReason::None;
    }

    void RasterSceneSnapshotPublisher::PollRetired()
    {
        if (!m_Device)
            return;

        for (auto iterator = m_Retired.begin();
            iterator != m_Retired.end();)
        {
            if (!m_Device->pollEventQuery(
                    iterator->completionQuery))
            {
                ++iterator;
                continue;
            }
            m_Device->resetEventQuery(
                iterator->completionQuery);
            iterator = m_Retired.erase(iterator);
        }
    }

    void RasterSceneSnapshotPublisher::DrainRetired()
    {
        if (!m_Device)
        {
            m_Retired.clear();
            return;
        }

        for (RetiredSnapshot& retired : m_Retired)
        {
            m_Device->waitEventQuery(retired.completionQuery);
            m_Device->resetEventQuery(retired.completionQuery);
        }
        m_Retired.clear();
    }

    std::shared_ptr<const RasterSceneSnapshot>
        RasterSceneSnapshotPublisher::ActiveSnapshot() const
    {
        return m_Active;
    }

    size_t RasterSceneSnapshotPublisher::RetiredSnapshotCount() const
    {
        return m_Retired.size();
    }

    nvrhi::CommandQueue
        RasterSceneSnapshotPublisher::RequiredCommandQueue() const
    {
        return m_CommandQueue;
    }
}
