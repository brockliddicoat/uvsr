#include "adaptive_msaa_raster_scene.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace uvsr::adaptive_msaa;

    bool g_Passed = true;

    struct OwnerRecord
    {
        uint32_t first = 0u;
        uint32_t second = 0u;
    };

    void Check(bool condition, const std::string& message)
    {
        if (condition)
            return;

        g_Passed = false;
        std::cerr << "FAILED: " << message << '\n';
    }

    std::shared_ptr<const void> MakeOwner(
        const std::shared_ptr<OwnerRecord>& owner,
        bool useSecondAlias = false)
    {
        const void* address = useSecondAlias
            ? static_cast<const void*>(&owner->second)
            : static_cast<const void*>(&owner->first);
        return std::shared_ptr<const void>(owner, address);
    }

    RasterSceneBuildParameters MakeParameters(
        uint64_t topologyEpoch = 1u)
    {
        RasterSceneBuildParameters parameters;
        parameters.topologyEpoch = topologyEpoch;
        parameters.topologyRevision = 10u;
        parameters.lodRevision = 20u;
        parameters.deformationRevision = 30u;
        parameters.materialRevision = 40u;
        return parameters;
    }

    RasterSceneGeometrySource MakeTriangle(
        const std::shared_ptr<const void>& owner,
        int64_t instanceIndex,
        int64_t firstGeometryInstanceIndex,
        uint64_t instanceGeometryCount,
        uint64_t geometryIndex,
        uint64_t indexCount = 3u)
    {
        RasterSceneGeometrySource source;
        source.stableInstanceOwner = owner;
        source.instanceIndex = instanceIndex;
        source.firstGeometryInstanceIndex =
            firstGeometryInstanceIndex;
        source.instanceGeometryCount = instanceGeometryCount;
        source.geometryIndex = geometryIndex;
        source.indexCount = indexCount;
        source.geometryPresent = true;
        source.meshUsesTriangleList = true;
        source.geometryUsesTriangleList = true;
        source.compactDeformationSupported = true;
        source.compactMaterialSupported = true;
        return source;
    }

    RasterScenePlanBuildResult BuildPlan(
        RasterScenePlanBuilder& builder,
        const std::vector<RasterSceneGeometrySource>& geometries,
        uint64_t sceneInstanceCount,
        const RasterSceneBuildParameters& parameters,
        uint64_t flattenedGeometryInstanceCount =
            std::numeric_limits<uint64_t>::max())
    {
        RasterScenePlanBuildDesc description;
        description.parameters = parameters;
        description.sceneInstanceCount = sceneInstanceCount;
        description.flattenedGeometryInstanceCount =
            flattenedGeometryInstanceCount ==
                    std::numeric_limits<uint64_t>::max()
                ? geometries.size()
                : flattenedGeometryInstanceCount;
        description.geometries = geometries.empty()
            ? nullptr
            : geometries.data();
        description.geometryCount = geometries.size();
        return builder.Build(description);
    }

    const RasterSceneGeometryAssignment* FindAssignment(
        const RasterScenePlan& plan,
        uint32_t flattenedIndex)
    {
        for (const RasterSceneGeometryAssignment& assignment :
            plan.assignments)
        {
            if (assignment.flattenedGeometryInstanceIndex ==
                flattenedIndex)
            {
                return &assignment;
            }
        }
        return nullptr;
    }

    void TestExactFlattenedMappingAndRasterDxrIdentity()
    {
        RasterScenePlanBuilder builder(3u);
        const auto ownerA = std::make_shared<OwnerRecord>();
        const auto ownerB = std::make_shared<OwnerRecord>();
        const std::shared_ptr<const void> tokenA =
            MakeOwner(ownerA);
        const std::shared_ptr<const void> tokenB =
            MakeOwner(ownerB);

        // Deliberately use source order that differs from flattened order.
        std::vector<RasterSceneGeometrySource> geometries = {
            MakeTriangle(tokenB, 1, 2, 1u, 0u, 9u),
            MakeTriangle(tokenA, 0, 0, 2u, 1u, 6u),
            MakeTriangle(tokenA, 0, 0, 2u, 0u, 3u)
        };
        const RasterSceneBuildParameters parameters =
            MakeParameters(5u);
        const RasterScenePlanBuildResult result =
            BuildPlan(builder, geometries, 2u, parameters);
        Check(result.Succeeded(),
            "an exact out-of-order flattened scene builds");
        if (!result.Succeeded())
            return;

        const RasterScenePlan& plan = *result.plan;
        Check(plan.geometryInstanceHandles.size() == 3u &&
                plan.assignments.size() == 3u,
            "the immutable plan retains one handle and assignment per flattened slot");
        Check(plan.version.topologyEpoch == 5u &&
                plan.version.buildSerial == 1u,
            "the first successful plan receives the supplied epoch and first serial");

        const RasterSceneGeometryAssignment* flat0 =
            FindAssignment(plan, 0u);
        const RasterSceneGeometryAssignment* flat1 =
            FindAssignment(plan, 1u);
        const RasterSceneGeometryAssignment* flat2 =
            FindAssignment(plan, 2u);
        Check(flat0 && flat1 && flat2,
            "every exact flattened location has a CPU assignment");
        if (!flat0 || !flat1 || !flat2)
            return;

        Check(flat0->geometryIndex == 0u &&
                flat1->geometryIndex == 1u &&
                flat2->geometryIndex == 0u,
            "geometry indices remain local to each Donut instance");
        Check(flat0->stableInstanceHandle ==
                flat1->stableInstanceHandle &&
                flat0->stableInstanceHandle !=
                    flat2->stableInstanceHandle,
            "submeshes share only their stable instance owner");
        Check(plan.geometryInstanceHandles[0] ==
                flat0->geometryInstanceHandle &&
                plan.geometryInstanceHandles[1] ==
                    flat1->geometryInstanceHandle &&
                plan.geometryInstanceHandles[2] ==
                    flat2->geometryInstanceHandle,
            "GPU handles are indexed by exact first-plus-local flattened slots");
        Check(flat0->primitiveCount == 1u &&
                flat1->primitiveCount == 2u &&
                flat2->primitiveCount == 3u,
            "triangle counts remain geometry local");

        const GpuVisibilityKeyResult rasterKey =
            MakeGpuVisibilityKey(
                flat1->geometryInstanceHandle,
                1u);
        RegistryValidation validation;
        validation.topologyEpoch = parameters.topologyEpoch;
        validation.lodRevision = parameters.lodRevision;
        validation.deformationRevision =
            parameters.deformationRevision;
        validation.materialRevision =
            parameters.materialRevision;
        const SurfaceResolutionResult rasterHit = rasterKey.Succeeded()
            ? builder.ResolveSurfaceKey(rasterKey.key, validation)
            : SurfaceResolutionResult{};
        const SurfaceKey futureDxrKey = {
            flat1->stableInstanceHandle,
            flat1->geometryIndex,
            1u
        };
        Check(rasterKey.Succeeded() && rasterHit.Succeeded() &&
                SameSurface(rasterHit.key, futureDxrKey),
            "raster and future DXR triples resolve to one logical SurfaceKey");

        GpuVisibilityKey invalidGeneration = rasterKey.key;
        invalidGeneration.geometryInstanceHandle &=
            GeometryInstanceSlotMask;
        Check(builder.ResolveSurfaceKey(
                invalidGeneration,
                validation).reason ==
                    FallbackReason::InvalidHandleEncoding,
            "a zero-generation raster handle cannot forge valid ownership");
    }

    void TestStableOwnerReuseAndReplacement()
    {
        RasterScenePlanBuilder builder(1u);
        auto originalOwner = std::make_shared<OwnerRecord>();
        std::vector<RasterSceneGeometrySource> geometries = {
            MakeTriangle(MakeOwner(originalOwner), 0, 0, 1u, 0u)
        };
        const RasterScenePlanBuildResult first =
            BuildPlan(builder, geometries, 1u, MakeParameters(8u));
        Check(first.Succeeded(),
            "the original owner builds at exact registry capacity");
        if (!first.Succeeded())
            return;

        geometries[0].stableInstanceOwner =
            MakeOwner(originalOwner, true);
        const RasterScenePlanBuildResult alias =
            BuildPlan(builder, geometries, 1u, MakeParameters(8u));
        Check(alias.Succeeded() &&
                alias.plan->assignments[0].stableInstanceHandle ==
                    first.plan->assignments[0].stableInstanceHandle &&
                alias.plan->geometryInstanceHandles[0] ==
                    first.plan->geometryInstanceHandles[0],
            "different pointers sharing one control block reuse stable ownership");

        const uint64_t originalStableHandle =
            first.plan->assignments[0].stableInstanceHandle;
        const uint32_t originalGpuHandle =
            first.plan->geometryInstanceHandles[0];
        geometries.clear();
        originalOwner.reset();

        const auto replacementOwner =
            std::make_shared<OwnerRecord>();
        geometries = {
            MakeTriangle(
                MakeOwner(replacementOwner),
                0,
                0,
                1u,
                0u)
        };
        const RasterScenePlanBuildResult replacement =
            BuildPlan(builder, geometries, 1u, MakeParameters(8u));
        Check(replacement.Succeeded() &&
                replacement.plan->assignments[0]
                    .stableInstanceHandle > originalStableHandle,
            "a destroyed owner replacement receives a monotonic stable handle");
        Check(replacement.Succeeded() &&
                replacement.plan->geometryInstanceHandles[0] !=
                    originalGpuHandle,
            "replacement ownership cannot alias the old packed generation");
    }

    void TestIdentityAndMetadataRevisions()
    {
        RasterScenePlanBuilder builder(1u);
        const auto owner = std::make_shared<OwnerRecord>();
        std::vector<RasterSceneGeometrySource> geometries = {
            MakeTriangle(MakeOwner(owner), 0, 0, 1u, 0u)
        };

        RasterSceneBuildParameters parameters = MakeParameters(11u);
        const RasterScenePlanBuildResult baseline =
            BuildPlan(builder, geometries, 1u, parameters);
        Check(baseline.Succeeded(),
            "revision testing starts from a valid mapping");
        if (!baseline.Succeeded())
            return;

        const uint32_t baselineHandle =
            baseline.plan->geometryInstanceHandles[0];
        geometries[0].compactDeformationSupported = false;
        geometries[0].compactMaterialSupported = false;
        ++parameters.deformationRevision;
        ++parameters.materialRevision;
        const RasterScenePlanBuildResult metadata =
            BuildPlan(builder, geometries, 1u, parameters);
        Check(metadata.Succeeded() &&
                metadata.plan->geometryInstanceHandles[0] ==
                    baselineHandle,
            "deformation and material metadata changes preserve ownership generation");
        Check(metadata.Succeeded() &&
                !metadata.plan->assignments[0]
                    .compactDeformationSupported &&
                !metadata.plan->assignments[0]
                    .compactMaterialSupported &&
                metadata.plan->assignments[0].Succeeded(),
            "compact support remains separate from valid full-G-buffer ownership");

        ++parameters.topologyRevision;
        const RasterScenePlanBuildResult topology =
            BuildPlan(builder, geometries, 1u, parameters);
        ++parameters.lodRevision;
        const RasterScenePlanBuildResult lod =
            BuildPlan(builder, geometries, 1u, parameters);
        const DecodedGeometryInstanceHandle baselineDecoded =
            DecodeGeometryInstanceHandle(baselineHandle);
        const DecodedGeometryInstanceHandle topologyDecoded =
            topology.Succeeded()
                ? DecodeGeometryInstanceHandle(
                    topology.plan->geometryInstanceHandles[0])
                : DecodedGeometryInstanceHandle{};
        const DecodedGeometryInstanceHandle lodDecoded = lod.Succeeded()
            ? DecodeGeometryInstanceHandle(
                lod.plan->geometryInstanceHandles[0])
            : DecodedGeometryInstanceHandle{};
        Check(topology.Succeeded() && lod.Succeeded() &&
                baselineDecoded.Succeeded() &&
                topologyDecoded.Succeeded() &&
                lodDecoded.Succeeded() &&
                topologyDecoded.slot == baselineDecoded.slot &&
                topologyDecoded.generation ==
                    baselineDecoded.generation + 1u &&
                lodDecoded.slot == topologyDecoded.slot &&
                lodDecoded.generation ==
                    topologyDecoded.generation + 1u,
            "topology and LOD revisions each advance the packed generation");

        const uint32_t preEpochHandle =
            lod.Succeeded()
                ? lod.plan->geometryInstanceHandles[0]
                : InvalidVisibilityWord;
        ++parameters.topologyEpoch;
        const RasterScenePlanBuildResult widerEpoch =
            BuildPlan(builder, geometries, 1u, parameters);
        RegistryValidation staleValidation;
        staleValidation.topologyEpoch =
            parameters.topologyEpoch - 1u;
        staleValidation.lodRevision = parameters.lodRevision;
        staleValidation.deformationRevision =
            parameters.deformationRevision;
        staleValidation.materialRevision =
            parameters.materialRevision;
        const GpuVisibilityKey staleKey = {
            widerEpoch.Succeeded()
                ? widerEpoch.plan->geometryInstanceHandles[0]
                : preEpochHandle,
            0u
        };
        Check(widerEpoch.Succeeded() &&
                builder.ResolveSurfaceKey(
                    staleKey,
                    staleValidation).reason ==
                        FallbackReason::TopologyEpochMismatch,
            "a wider topology epoch rejects stale use even if packed bits repeat");
    }

    void TestInvalidSentinelsAndCompactIndependence()
    {
        RasterScenePlanBuilder builder(1u);
        const auto owner = std::make_shared<OwnerRecord>();
        const auto token = MakeOwner(owner);
        std::vector<RasterSceneGeometrySource> geometries;
        for (uint64_t geometryIndex = 0u;
            geometryIndex < 6u;
            ++geometryIndex)
        {
            geometries.push_back(MakeTriangle(
                token,
                0,
                0,
                6u,
                geometryIndex));
        }
        geometries[0].geometryPresent = false;
        geometries[1].meshUsesTriangleList = false;
        geometries[2].geometryUsesTriangleList = false;
        geometries[3].indexCount = 0u;
        geometries[4].indexCount = 4u;
        geometries[5].compactDeformationSupported = false;
        geometries[5].compactMaterialSupported = false;

        const RasterScenePlanBuildResult result =
            BuildPlan(builder, geometries, 1u, MakeParameters(20u));
        Check(result.Succeeded(),
            "unsupported per-entry raster topology does not invalidate "
            "an exact scene plan");
        if (!result.Succeeded())
            return;

        for (uint32_t index = 0u; index < 5u; ++index)
        {
            Check(result.plan->geometryInstanceHandles[index] ==
                    InvalidVisibilityWord &&
                    !result.plan->assignments[index].Succeeded() &&
                    result.plan->assignments[index].mappingReason !=
                        FallbackReason::None,
                "unsupported geometry retains an explicit invalid sentinel");
        }
        const RasterSceneGeometryAssignment& valid =
            result.plan->assignments[5];
        Check(valid.Succeeded() &&
                result.plan->geometryInstanceHandles[5] ==
                    valid.geometryInstanceHandle &&
                !valid.compactDeformationSupported &&
                !valid.compactMaterialSupported,
            "compact-ineligible triangles still receive exact raster ownership");
        Check(builder.ActiveGeometryInstanceCount() == 1u,
            "only supported raster triangles consume compact handle slots");
        for (const RasterSceneGeometryAssignment& assignment :
            result.plan->assignments)
        {
            Check(assignment.stableInstanceHandle ==
                    valid.stableInstanceHandle,
                "all entries retain their shared stable instance assignment");
        }

        RegistryValidation compactValidation;
        compactValidation.topologyEpoch =
            result.plan->version.topologyEpoch;
        compactValidation.lodRevision = valid.lodRevision;
        compactValidation.deformationRevision =
            valid.deformationRevision;
        compactValidation.materialRevision = valid.materialRevision;
        compactValidation.requireSupportedDeformation = true;
        const GpuVisibilityKey compactKey = {
            valid.geometryInstanceHandle,
            0u
        };
        Check(builder.ResolveSurfaceKey(
                compactKey,
                compactValidation).reason ==
                    FallbackReason::UnsupportedDeformation,
            "compact validation sees support metadata without gating raster ownership");
    }

    void TestCollisionMalformedAndOverflowFailures()
    {
        const auto ownerA = std::make_shared<OwnerRecord>();
        const auto ownerB = std::make_shared<OwnerRecord>();
        const auto tokenA = MakeOwner(ownerA);
        const auto tokenB = MakeOwner(ownerB);
        const RasterSceneBuildParameters parameters =
            MakeParameters(30u);

        {
            RasterScenePlanBuilder builder(2u);
            std::vector<RasterSceneGeometrySource> collision = {
                MakeTriangle(tokenA, 0, 0, 1u, 0u),
                MakeTriangle(tokenB, 1, 0, 1u, 0u)
            };
            Check(BuildPlan(
                    builder,
                    collision,
                    2u,
                    parameters).status.reason ==
                        FallbackReason::TransientMappingCollision,
                "overlapping flattened ranges fail the whole plan");
        }
        {
            RasterScenePlanBuilder builder(2u);
            std::vector<RasterSceneGeometrySource> ownerCollision = {
                MakeTriangle(tokenA, 0, 0, 1u, 0u),
                MakeTriangle(tokenA, 1, 1, 1u, 0u)
            };
            Check(BuildPlan(
                    builder,
                    ownerCollision,
                    2u,
                    parameters).status.reason ==
                        FallbackReason::TransientMappingCollision,
                "one stable owner cannot impersonate two instance indices");
        }
        {
            RasterScenePlanBuilder builder(2u);
            std::vector<RasterSceneGeometrySource> instanceCollision = {
                MakeTriangle(tokenA, 0, 0, 2u, 0u),
                MakeTriangle(tokenB, 0, 0, 2u, 1u)
            };
            Check(BuildPlan(
                    builder,
                    instanceCollision,
                    1u,
                    parameters).status.reason ==
                        FallbackReason::TransientMappingCollision,
                "one instance index cannot alias two owner control blocks");
        }
        {
            RasterScenePlanBuilder builder(1u);
            RasterSceneGeometrySource negative =
                MakeTriangle(tokenA, -1, 0, 1u, 0u);
            std::vector<RasterSceneGeometrySource> geometries = {
                negative
            };
            Check(BuildPlan(
                    builder,
                    geometries,
                    1u,
                    parameters).status.reason ==
                        FallbackReason::TransientMappingFailure,
                "negative signed instance indices fail before narrowing");
            geometries[0].instanceIndex = 0;
            geometries[0].firstGeometryInstanceIndex = -1;
            Check(BuildPlan(
                    builder,
                    geometries,
                    1u,
                    parameters).status.reason ==
                        FallbackReason::TransientMappingFailure,
                "negative signed flattened bases fail before narrowing");
        }
        {
            RasterScenePlanBuilder builder(1u);
            std::vector<RasterSceneGeometrySource> mismatch = {
                MakeTriangle(tokenA, 0, 0, 1u, 0u)
            };
            Check(BuildPlan(
                    builder,
                    mismatch,
                    1u,
                    parameters,
                    2u).status.reason ==
                        FallbackReason::TransientMappingFailure,
                "source-count disagreement cannot publish a partial map");
        }
        {
            RasterScenePlanBuilder builder(1u);
            RasterSceneGeometrySource overflow =
                MakeTriangle(tokenA, 0, 1, 1u, 0u);
            overflow.instanceGeometryCount =
                std::numeric_limits<uint64_t>::max();
            std::vector<RasterSceneGeometrySource> geometries = {
                overflow
            };
            Check(BuildPlan(
                    builder,
                    geometries,
                    1u,
                    parameters).status.reason ==
                        FallbackReason::CapacityOverflow,
                "range-addition overflow fails before allocating a map");

            geometries[0] =
                MakeTriangle(tokenA, 0, 0, 1u, 0u);
            geometries[0].indexCount =
                uint64_t(std::numeric_limits<uint32_t>::max()) + 1u;
            Check(BuildPlan(
                    builder,
                    geometries,
                    1u,
                    parameters).status.reason ==
                        FallbackReason::CapacityOverflow,
                "unrepresentable primitive input counts fail before narrowing");
        }
    }

    void TestTransactionalFailureAndEpochRollback()
    {
        RasterScenePlanBuilder builder(1u);
        const auto ownerA = std::make_shared<OwnerRecord>();
        const auto ownerB = std::make_shared<OwnerRecord>();
        const auto tokenA = MakeOwner(ownerA);
        const auto tokenB = MakeOwner(ownerB);
        RasterSceneBuildParameters parameters = MakeParameters(50u);

        std::vector<RasterSceneGeometrySource> oneA = {
            MakeTriangle(tokenA, 0, 0, 1u, 0u)
        };
        const RasterScenePlanBuildResult baseline =
            BuildPlan(builder, oneA, 1u, parameters);
        Check(baseline.Succeeded(),
            "transaction testing starts from a successful mapping");
        if (!baseline.Succeeded())
            return;

        const uint32_t baselineHandle =
            baseline.plan->geometryInstanceHandles[0];
        std::vector<RasterSceneGeometrySource> overCapacity = {
            MakeTriangle(tokenA, 0, 0, 1u, 0u),
            MakeTriangle(tokenB, 1, 1, 1u, 0u)
        };
        const RasterScenePlanBuildResult failed =
            BuildPlan(builder, overCapacity, 2u, parameters);
        Check(failed.status.reason == FallbackReason::CapacityOverflow &&
                !failed.plan,
            "registry exhaustion rejects the complete candidate");
        Check(builder.NextBuildSerial() == 2u &&
                builder.ActiveGeometryInstanceCount() == 1u,
            "failed reconciliation does not consume a serial or replace live mappings");

        const RasterScenePlanBuildResult restored =
            BuildPlan(builder, oneA, 1u, parameters);
        Check(restored.Succeeded() &&
                restored.plan->version.buildSerial == 2u &&
                restored.plan->geometryInstanceHandles[0] ==
                    baselineHandle,
            "the prior mapping remains intact after a failed candidate");

        std::vector<RasterSceneGeometrySource> oneB = {
            MakeTriangle(tokenB, 0, 0, 1u, 0u)
        };
        const RasterScenePlanBuildResult replacement =
            BuildPlan(builder, oneB, 1u, parameters);
        Check(replacement.Succeeded() &&
                replacement.plan->assignments[0]
                    .stableInstanceHandle == 2u,
            "a failed candidate does not consume stable owner handles");

        RasterSceneBuildParameters staleParameters = parameters;
        --staleParameters.topologyEpoch;
        const uint64_t serialBeforeStale =
            builder.NextBuildSerial();
        const RasterScenePlanBuildResult stale =
            BuildPlan(builder, oneB, 1u, staleParameters);
        Check(stale.status.reason ==
                FallbackReason::TopologyEpochMismatch &&
                builder.NextBuildSerial() == serialBeforeStale,
            "epoch rollback fails transactionally without consuming a version");
        const RasterScenePlanBuildResult afterStale =
            BuildPlan(builder, oneB, 1u, parameters);
        Check(afterStale.Succeeded() &&
                afterStale.plan->version.buildSerial ==
                    serialBeforeStale,
            "the next valid same-epoch plan receives the unconsumed serial");
    }

    void TestGenerationExhaustionRequiresWiderEpoch()
    {
        RasterScenePlanBuilder builder(1u);
        const auto owner = std::make_shared<OwnerRecord>();
        std::vector<RasterSceneGeometrySource> geometries = {
            MakeTriangle(MakeOwner(owner), 0, 0, 1u, 0u)
        };
        RasterSceneBuildParameters parameters = MakeParameters(70u);
        parameters.topologyRevision = 0u;

        RasterScenePlanBuildResult result =
            BuildPlan(builder, geometries, 1u, parameters);
        bool reachedMaximumGeneration = result.Succeeded();
        uint32_t lastHandle = result.Succeeded()
            ? result.plan->geometryInstanceHandles[0]
            : InvalidVisibilityWord;
        for (uint32_t revision = 1u;
            reachedMaximumGeneration &&
                revision < MaximumGeometryInstanceGeneration;
            ++revision)
        {
            parameters.topologyRevision = revision;
            result = BuildPlan(builder, geometries, 1u, parameters);
            reachedMaximumGeneration = result.Succeeded();
            if (result.Succeeded())
                lastHandle = result.plan->geometryInstanceHandles[0];
        }

        const DecodedGeometryInstanceHandle maximum =
            DecodeGeometryInstanceHandle(lastHandle);
        Check(reachedMaximumGeneration && maximum.Succeeded() &&
                maximum.generation ==
                    MaximumGeometryInstanceGeneration,
            "raster reconciliation reaches the last legal packed generation exactly");
        if (!reachedMaximumGeneration || !maximum.Succeeded())
            return;

        const uint64_t serialBeforeFailure =
            builder.NextBuildSerial();
        parameters.topologyRevision =
            MaximumGeometryInstanceGeneration;
        const RasterScenePlanBuildResult exhausted =
            BuildPlan(builder, geometries, 1u, parameters);
        Check(exhausted.status.reason ==
                FallbackReason::GenerationExhausted &&
                builder.RequiresEpochReset() &&
                builder.ActiveGeometryInstanceCount() == 1u &&
                builder.NextBuildSerial() == serialBeforeFailure,
            "generation exhaustion retains live ownership and latches a "
            "wider-epoch reset");
        Check(BuildPlan(
                builder,
                geometries,
                1u,
                parameters).status.reason ==
                    FallbackReason::RegistryResetRequired,
            "same-epoch rebuilding remains blocked after generation exhaustion");

        ++parameters.topologyEpoch;
        const RasterScenePlanBuildResult reset =
            BuildPlan(builder, geometries, 1u, parameters);
        Check(reset.Succeeded() &&
                !builder.RequiresEpochReset() &&
                builder.RegistryTopologyEpoch() ==
                    parameters.topologyEpoch &&
                reset.plan->version.buildSerial ==
                    serialBeforeFailure,
            "a wider epoch clears the latch without consuming failed serials");
    }

    void TestSnapshotVersionOrdering()
    {
        Check(ValidateRasterSceneSnapshotReplacement(
                { 4u, 9u },
                { 5u, 1u }) ==
                    FallbackReason::TopologyEpochMismatch,
            "an older topology epoch cannot replace a published raster snapshot");
        Check(ValidateRasterSceneSnapshotReplacement(
                { 5u, 1u },
                { 5u, 1u }) ==
                    FallbackReason::TransientMappingFailure &&
                ValidateRasterSceneSnapshotReplacement(
                    { 5u, 0u },
                    { 5u, 1u }) ==
                        FallbackReason::TransientMappingFailure,
            "same or invalid serials cannot replace a published raster snapshot");
        Check(ValidateRasterSceneSnapshotReplacement(
                { 5u, 2u },
                { 5u, 1u }) == FallbackReason::None &&
                ValidateRasterSceneSnapshotReplacement(
                    { 6u, 1u },
                    { 5u, 99u }) == FallbackReason::None,
            "a newer same-epoch serial or wider epoch may replace ownership");

        RasterScenePublicationOrder order;
        Check(order.Accept({ 5u, 1u }) == FallbackReason::None &&
                order.Accept({ 5u, 1u }) ==
                    FallbackReason::TransientMappingFailure &&
                order.Accept({ 5u, 2u }) == FallbackReason::None &&
                order.Accept({ 6u, 1u }) == FallbackReason::None &&
                order.Accept({ 5u, 100u }) ==
                    FallbackReason::TopologyEpochMismatch,
            "publication order retains the monotonic high-water mark "
            "across active clears");
        const RasterSceneSnapshotVersion highest =
            order.HighestPublishedVersion();
        Check(order.HasPublishedVersion() &&
                highest.topologyEpoch == 6u &&
                highest.buildSerial == 1u,
            "rejected candidates never mutate publication ordering state");
    }

    void TestConfigurationAndNonemptyContract()
    {
        const RasterSceneBuildParameters defaultParameters;
        Check(!defaultParameters.
                supportAlphaTestedMaterialsForCompactVisibility,
            "compact raster alpha admission is fail-closed by default");

        RasterScenePlanBuilder zeroCapacity(0u);
        RasterScenePlanBuilder excessiveCapacity(
            GeometryInstanceSlotCount + 1u);
        Check(zeroCapacity.ConfigurationReason() ==
                FallbackReason::CapacityOverflow &&
                excessiveCapacity.ConfigurationReason() ==
                    FallbackReason::CapacityOverflow,
            "invalid registry capacities fail configuration explicitly");

        RasterScenePlanBuilder builder(1u);
        std::vector<RasterSceneGeometrySource> empty;
        Check(BuildPlan(
                builder,
                empty,
                1u,
                MakeParameters(1u),
                0u).status.reason ==
                    FallbackReason::ReconstructionFailure,
            "a raster snapshot cannot create an empty structured buffer");
        RasterSceneBuildParameters zeroEpoch = MakeParameters(0u);
        const auto owner = std::make_shared<OwnerRecord>();
        std::vector<RasterSceneGeometrySource> one = {
            MakeTriangle(MakeOwner(owner), 0, 0, 1u, 0u)
        };
        Check(BuildPlan(
                builder,
                one,
                1u,
                zeroEpoch).status.reason ==
                    FallbackReason::TopologyEpochMismatch,
            "zero is never a publishable topology epoch");
        Check(RasterSceneCommandQueue ==
                nvrhi::CommandQueue::Graphics,
            "the raster ownership lifecycle is explicitly Graphics-queue only");
    }
}

int main()
{
    TestExactFlattenedMappingAndRasterDxrIdentity();
    TestStableOwnerReuseAndReplacement();
    TestIdentityAndMetadataRevisions();
    TestInvalidSentinelsAndCompactIndependence();
    TestCollisionMalformedAndOverflowFailures();
    TestTransactionalFailureAndEpochRollback();
    TestGenerationExhaustionRequiresWiderEpoch();
    TestSnapshotVersionOrdering();
    TestConfigurationAndNonemptyContract();

    if (g_Passed)
    {
        std::cout <<
            "UVSR adaptive MSAA raster scene validation passed\n";
    }
    return g_Passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
