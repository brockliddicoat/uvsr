#pragma once

#include "sparse_virtual_shadow_map_settings.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

namespace donut::engine
{
    class CommonRenderPasses;
    class DirectionalLight;
    class IView;
    class Light;
    class PlanarView;
    class SceneGraphNode;
    class ShaderFactory;
}

namespace donut::render
{
    class InstancedOpaqueDrawStrategy;
}

namespace uvsr
{
    inline constexpr uint32_t SvsmFineClipmapCount =
        SvsmClipmapCount - 1u;
    inline constexpr uint32_t SvsmFinePageCandidateMaskWordsPerLevel =
        SvsmPagesPerClipmap / 32u;
    inline constexpr uint32_t SvsmFinePageCandidateMaskWordCount =
        SvsmFineClipmapCount *
        SvsmFinePageCandidateMaskWordsPerLevel;
    static_assert(SvsmPagesPerClipmap % 32u == 0u);

    enum class SvsmGpuTimingSource : uint32_t
    {
        Unavailable,
        TimerQuery,
        KnownZero
    };

    struct SparseVirtualShadowMapResult
    {
        nvrhi::ITexture* visibility = nullptr;
        const donut::engine::DirectionalLight* light = nullptr;
        bool showDebug = false;
    };

    struct SvsmObjectInvalidationResolverKey
    {
        // Donut may renumber instanceIndex after structural edits, so it is
        // only the current renderer ordinal. instanceIdentity is the stable
        // MeshInstance/leaf identity for its lifetime. geometryOrdinal is the
        // stable position in MeshInfo::geometries, while geometryIdentity
        // disambiguates replacement and shared geometry.
        uint32_t instanceIndex = 0u;
        uint32_t geometryOrdinal = 0u;
        const void* geometryIdentity = nullptr;
        const void* instanceIdentity = nullptr;
    };

    struct SvsmObjectInvalidationResolver
    {
        using ResolveFunction = bool (*)(
            const void* context,
            const SvsmObjectInvalidationResolverKey& key,
            SvsmObjectInvalidationMode& mode);

        // configurationIdentity and revision form the caller-owned policy
        // version. Both the callback and context are also compared so a
        // replacement cannot silently reuse cached object modes.
        const void* configurationIdentity = nullptr;
        uint64_t revision = 0u;
        const void* context = nullptr;
        ResolveFunction resolve = nullptr;

        [[nodiscard]] bool IsValid() const
        {
            return configurationIdentity != nullptr &&
                resolve != nullptr;
        }

        [[nodiscard]] bool TryResolve(
            const SvsmObjectInvalidationResolverKey& key,
            SvsmObjectInvalidationMode& mode) const
        {
            if (!IsValid() || !resolve(context, key, mode))
                return false;
            return IsSvsmObjectInvalidationModeValid(mode);
        }

        [[nodiscard]] bool HasSameConfiguration(
            const SvsmObjectInvalidationResolver& other) const
        {
            return configurationIdentity ==
                    other.configurationIdentity &&
                revision == other.revision &&
                context == other.context &&
                resolve == other.resolve;
        }
    };

    [[nodiscard]] inline bool TryResolveSvsmObjectInvalidationMode(
        SvsmObjectInvalidationMode defaultMode,
        const SvsmObjectInvalidationResolver* resolver,
        const SvsmObjectInvalidationResolverKey& key,
        SvsmObjectInvalidationMode& mode)
    {
        mode = defaultMode;
        if (!IsSvsmObjectInvalidationModeValid(defaultMode))
            return false;
        if (!resolver)
            return true;

        SvsmObjectInvalidationMode resolvedMode = defaultMode;
        if (!resolver->TryResolve(key, resolvedMode))
            return false;
        mode = resolvedMode;
        return true;
    }

    [[nodiscard]] inline bool
    HasSameSvsmObjectInvalidationPolicyConfiguration(
        SvsmObjectInvalidationMode leftDefaultMode,
        const SvsmObjectInvalidationResolver* leftResolver,
        SvsmObjectInvalidationMode rightDefaultMode,
        const SvsmObjectInvalidationResolver* rightResolver)
    {
        if (leftDefaultMode != rightDefaultMode ||
            (leftResolver == nullptr) != (rightResolver == nullptr))
        {
            return false;
        }
        return !leftResolver ||
            leftResolver->HasSameConfiguration(*rightResolver);
    }

    struct SparseVirtualShadowMapTimings
    {
        bool supported = false;
        bool active = false;
        uint64_t physicalDepthBytes = 0u;
        uint64_t visibilityBytes = 0u;
        uint64_t packetPageMetadataBytes = 0u;
        uint64_t packetPageListBytes = 0u;
        uint64_t staticDepthHierarchyBytes = 0u;
        uint64_t receiverPageMaskBytes = 0u;
        uint32_t requiredPages = 0u;
        uint32_t residentPages = 0u;
        uint32_t cachedPages = 0u;
        uint32_t dirtyPages = 0u;
        uint32_t renderedPages = 0u;
        uint32_t outOfRangePixels = 0u;
        uint32_t allocationFailures = 0u;
        uint32_t resolveMissingPixels = 0u;
        uint32_t overBudgetPages = 0u;
        uint32_t fallbackPixels = 0u;
        uint32_t packetPageCandidatePackets = 0u;
        uint32_t packetPageCompactedPackets = 0u;
        uint32_t packetPageFailOpenPackets = 0u;
        uint32_t scheduledTileMaskQueries = 0u;
        uint32_t scheduledTileMaskEarlyRejects = 0u;
        uint32_t scheduledTileMaskFailOpens = 0u;
        uint32_t scheduledTileMaskPositiveExactZero = 0u;
        uint32_t staticDepthHierarchyQueries = 0u;
        uint32_t staticDepthHierarchyCulledPages = 0u;
        uint32_t staticDepthHierarchyFailOpens = 0u;
        uint32_t staticDepthHierarchyBuiltPages = 0u;
        uint32_t receiverPageMaskQueries = 0u;
        uint32_t receiverPageMaskCulledPages = 0u;
        uint32_t receiverPageMaskFailOpens = 0u;
        bool debugCountersAvailable = false;
        uint32_t debugCounterAgeFrames = 0u;
        bool staticPageRequestReuseActive = false;
        bool staticPageDrainActive = false;
        uint32_t staticPageDrainFramesRemaining = 0u;
        bool staticVisibilityReuseActive = false;
        bool cachedShadowDrawListsRequested = false;
        bool cachedShadowDrawListsActive = false;
        bool cachedShadowDrawListsReused = false;
        bool cachedShadowDrawListsRebuilt = false;
        uint32_t cachedShadowDrawListPacketCount = 0u;
        bool persistentCasterSourceRequested = false;
        bool persistentCasterSourceActive = false;
        bool persistentCasterSourceReused = false;
        bool persistentCasterSourceRebuilt = false;
        uint32_t persistentCasterSourceRecordCount = 0u;
        bool casterOnlySceneRevisionActive = false;
        bool batchedDrawSupported = false;
        bool batchedDrawActive = false;
        bool packetStateSortingActive = false;
        bool levelEmptyWorkSkipActive = false;
        bool packetPageCullingActive = false;
        bool hierarchicalScheduledPageMaskActive = false;
        bool hierarchicalScheduledPageMaskUnavailable = false;
        bool receiverPageMaskCullingRequested = false;
        bool receiverPageMaskCullingActive = false;
        bool receiverPageMaskCullingUnavailable = false;
        bool staticDepthHierarchyCullingRequested = false;
        bool staticDepthHierarchyCullingActive = false;
        bool staticDepthHierarchyCullingUnavailable = false;
        bool deferredStaticDepthMergeRequested = false;
        bool deferredStaticDepthMergeActive = false;
        bool deferredStaticDepthMergeUnavailable = false;
        bool dirtyPageScatterRasterActive = false;
        bool packetPageCullingUnavailable = false;
        bool movingLightUncachedActive = false;
        bool movingLightCacheTransitionActive = false;
        bool effectivePairedStaticDynamicDepth = false;
        bool physicalMappingRetentionActive = false;
        bool lightDepthOriginGuardBandRequested = false;
        bool lightDepthOriginGuardBandRetained = false;
        float movingLightLodRecoveryFactor = 0.f;
        SvsmResolutionBias effectiveResolutionBias =
            SvsmResolutionBias::Zero;
        bool receiverDistanceMipClampActive = false;
        bool movingLightContinuousReceiverBiasActive = false;
        float effectiveReceiverDistanceMipClampStart = 0.f;
        uint32_t receiverDistanceMipClampMaximumLevel = 0u;
        uint32_t staticPageRequestReuseRejectMask = 0u;
        float pageMarkingMilliseconds = 0.f;
        float allocationMilliseconds = 0.f;
        float clearingMilliseconds = 0.f;
        float cullingCpuMilliseconds = 0.f;
        float sceneValidationCpuMilliseconds = 0.f;
        float clipmapUpdateCpuMilliseconds = 0.f;
        float totalCpuMilliseconds = 0.f;
        float packetPageCullingMilliseconds = 0.f;
        float pageRenderingMilliseconds = 0.f;
        float filteringMilliseconds = 0.f;
        float totalMilliseconds = 0.f;
        bool detailedGpuTimingEnabled = true;
        SvsmGpuTimingSource gpuTimingSource =
            SvsmGpuTimingSource::Unavailable;
        uint32_t gpuTimingAgeFrames = 0u;
        // Latest-frame UI publication intentionally prefers a newer
        // KnownZero result over an older timer query. Keep a separate history
        // of completed untagged work queries so that preference cannot erase
        // evidence of an isolated GPU-work frame.
        bool lastCompletedWorkTimingAvailable = false;
        bool lastCompletedWorkDetailedGpuTimingEnabled = true;
        uint64_t lastCompletedWorkSourceFrame = 0u;
        uint64_t completedWorkSampleCount = 0u;
        float lastCompletedWorkPageMarkingMilliseconds = 0.f;
        float lastCompletedWorkAllocationMilliseconds = 0.f;
        float lastCompletedWorkClearingMilliseconds = 0.f;
        float lastCompletedWorkPacketPageCullingMilliseconds = 0.f;
        float lastCompletedWorkPageRenderingMilliseconds = 0.f;
        float lastCompletedWorkFilteringMilliseconds = 0.f;
        float lastCompletedWorkTotalMilliseconds = 0.f;
        // These CPU-only counters do not issue timer queries or readbacks.
        // Comparing the submission serial across a zero-work streak proves
        // that the sparse fast path recorded no SVSM GPU command stream.
        uint64_t gpuWorkSubmissionSerial = 0u;
        uint64_t staticZeroWorkFrameStreak = 0u;
        uint64_t staticZeroWorkFrameTotal = 0u;

        uint32_t comparisonVirtualResolution = SvsmVirtualResolution;
        uint32_t comparisonClipmapCount = SvsmClipmapCount;
        uint32_t comparisonFirstClipmapLevel = 0u;
        uint32_t comparisonFilterSampleCount = 0u;
        uint32_t comparisonFilterComparisonCount = 0u;
        float comparisonFinestCoverageExtent = 0.f;
        float comparisonCoarsestCoverageExtent = 0.f;
        float comparisonFinestWorldTexelSize = 0.f;
        float comparisonCoarsestWorldTexelSize = 0.f;
        float comparisonMaximumLightDepth = 0.f;
        float comparisonFilterRadiusTexels = 0.f;
        SvsmFilterMode comparisonFilterMode =
            SvsmFilterMode::ManualPageSafe;
        bool comparisonAdaptiveFiltering = false;
    };

    struct SparseVirtualShadowMapGpuTiming
    {
        uint64_t sourceTag = 0u;
        float pageMarkingMilliseconds = 0.f;
        float allocationMilliseconds = 0.f;
        float clearingMilliseconds = 0.f;
        float packetPageCullingMilliseconds = 0.f;
        float pageRenderingMilliseconds = 0.f;
        float filteringMilliseconds = 0.f;
        float totalMilliseconds = 0.f;
        bool detailedGpuTimingEnabled = true;
    };

    inline void IncrementSvsmMonotonicCounter(uint64_t& counter)
    {
        constexpr uint64_t MaximumCounter = ~uint64_t(0);
        if (counter != MaximumCounter)
            ++counter;
    }

    inline void RecordSvsmGpuWorkSubmission(
        SparseVirtualShadowMapTimings& timings)
    {
        IncrementSvsmMonotonicCounter(
            timings.gpuWorkSubmissionSerial);
        timings.staticZeroWorkFrameStreak = 0u;
    }

    inline void RecordSvsmStaticZeroWorkFrame(
        SparseVirtualShadowMapTimings& timings)
    {
        IncrementSvsmMonotonicCounter(
            timings.staticZeroWorkFrameStreak);
        IncrementSvsmMonotonicCounter(
            timings.staticZeroWorkFrameTotal);
    }

    inline bool RetainSvsmCompletedUntaggedWorkTiming(
        SparseVirtualShadowMapTimings& timings,
        const SparseVirtualShadowMapGpuTiming& sample,
        uint64_t sourceFrame,
        bool discarded)
    {
        if (discarded || sample.sourceTag != 0u)
            return false;

        timings.lastCompletedWorkTimingAvailable = true;
        timings.lastCompletedWorkDetailedGpuTimingEnabled =
            sample.detailedGpuTimingEnabled;
        timings.lastCompletedWorkSourceFrame = sourceFrame;
        IncrementSvsmMonotonicCounter(
            timings.completedWorkSampleCount);
        timings.lastCompletedWorkPageMarkingMilliseconds =
            sample.pageMarkingMilliseconds;
        timings.lastCompletedWorkAllocationMilliseconds =
            sample.allocationMilliseconds;
        timings.lastCompletedWorkClearingMilliseconds =
            sample.clearingMilliseconds;
        timings.lastCompletedWorkPacketPageCullingMilliseconds =
            sample.packetPageCullingMilliseconds;
        timings.lastCompletedWorkPageRenderingMilliseconds =
            sample.pageRenderingMilliseconds;
        timings.lastCompletedWorkFilteringMilliseconds =
            sample.filteringMilliseconds;
        timings.lastCompletedWorkTotalMilliseconds =
            sample.totalMilliseconds;
        return true;
    }

    struct SparseVirtualShadowMapTimingAccounting
    {
        uint64_t issued = 0u;
        uint64_t dropped = 0u;
        uint64_t retired = 0u;
        uint64_t outstanding = 0u;
    };

    class SparseVirtualShadowMapPass
    {
    private:
        class DenseDepthPass;
        class SparseDepthPass;
        struct CasterSnapshotState;

        struct UiTimingContext
        {
            SparseVirtualShadowMapSettings settings;
            SvsmResourceBackend backend = SvsmResourceBackend::None;
            bool detailedGpuTimingEnabled = true;
            bool staticPageRequestReuseActive = false;
            bool staticPageDrainActive = false;
            bool staticVisibilityReuseActive = false;
            bool batchedDrawSupported = false;
            bool batchedDrawActive = false;
            bool packetStateSortingActive = false;
            bool levelEmptyWorkSkipActive = false;
            bool packetPageCullingActive = false;
            bool hierarchicalScheduledPageMaskActive = false;
            bool hierarchicalScheduledPageMaskUnavailable = false;
            bool receiverPageMaskCullingRequested = false;
            bool receiverPageMaskCullingActive = false;
            bool receiverPageMaskCullingUnavailable = false;
            bool staticDepthHierarchyCullingRequested = false;
            bool staticDepthHierarchyCullingActive = false;
            bool staticDepthHierarchyCullingUnavailable = false;
            bool deferredStaticDepthMergeRequested = false;
            bool deferredStaticDepthMergeActive = false;
            bool deferredStaticDepthMergeUnavailable = false;
            bool dirtyPageScatterRasterActive = false;
            bool packetPageCullingUnavailable = false;
            bool movingLightUncachedActive = false;
            bool movingLightCacheTransitionActive = false;
            bool effectivePairedStaticDynamicDepth = false;
            bool physicalMappingRetentionActive = false;
            float effectiveReceiverDistanceMipClampStart = 0.f;
            uint32_t receiverDistanceMipClampMaximumLevel = 0u;
            uint32_t staticPageRequestReuseRejectMask = 0u;
        };

        struct BindingResourceSignature
        {
            uint64_t hash = 0u;
            uint32_t casterCount = 0u;
        };

        static constexpr uint32_t c_TimerLatency = 4u;
        static constexpr uint32_t c_TimerStageCount = 7u;
        static constexpr size_t c_MaxCompletedTimingSamples = 2048u;
        static constexpr uint32_t c_DebugCounterCount =
            SvsmDebugCounterCount;
        static constexpr uint32_t c_DebugCounterReadbackCount =
            SvsmCounterCount;
        static constexpr uint32_t c_AllocatorCounterCount =
            SvsmLevelHasWorkCounterBase;
        static constexpr uint32_t c_LevelHasWorkCounterBase =
            SvsmLevelHasWorkCounterBase;
        static constexpr uint32_t c_CounterCount =
            SvsmCounterCount;

        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
        std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;

        nvrhi::TextureHandle m_DenseDepth;
        nvrhi::FramebufferHandle m_RasterFramebuffer;
        nvrhi::TextureHandle m_Visibility;
        nvrhi::TextureHandle m_DebugVisualization;
        SvsmResourceBackend m_ResourceBackend =
            SvsmResourceBackend::None;
        nvrhi::BufferHandle m_ResolveConstants;
        nvrhi::BindingLayoutHandle m_ResolveBindingLayout;
        nvrhi::BindingSetHandle m_ResolveBindingSet;
        nvrhi::ShaderHandle m_ResolveShader;
        nvrhi::ComputePipelineHandle m_ResolvePipeline;
        nvrhi::ITexture* m_BoundCameraDepth = nullptr;
        std::unique_ptr<DenseDepthPass> m_DenseDepthPass;
        nvrhi::TextureHandle m_PageTable;
        nvrhi::TextureHandle m_SparsePhysicalDepth;
        bool m_AllocatedPairedStaticDynamicDepth = false;
        bool m_AllocatedDeferredStaticDepthMerge = false;
        bool m_AllocatedDeferredStaticDepthMergeValid = false;
        bool m_DeferredStaticDepthMergeRequest = false;
        bool m_DeferredStaticDepthMergeRequestValid = false;
        SvsmSparseAlphaBindingLayout m_AllocatedSparseAlphaBindingLayout =
            SvsmSparseAlphaBindingLayout::FullGBuffer;
        bool m_AllocatedSparseAlphaBindingLayoutValid = false;
        nvrhi::BufferHandle m_PhysicalOwners;
        nvrhi::BufferHandle m_RenderPages;
        nvrhi::BufferHandle m_CompactRenderPages;
        nvrhi::BufferHandle m_DirtyPageRectangles;
        nvrhi::BufferHandle m_LocalInvalidationPages;
        nvrhi::BufferHandle m_ScheduledPageTileMasks;
        nvrhi::BufferHandle m_StaticDepthHierarchy;
        nvrhi::BufferHandle m_ReceiverPageMasks;
        nvrhi::BufferHandle m_FinePageCandidateMasks;
        bool m_StaticDepthHierarchyBootstrapRequired = true;
        nvrhi::BufferHandle m_Counters;
        nvrhi::BufferHandle m_IndirectPageDispatchArguments;
        nvrhi::BufferHandle m_IndirectDrawArguments;
        uint32_t m_IndirectDrawCapacity = 0u;
        bool m_IndirectDrawArgumentsInitialized = false;
        bool m_IndirectDrawArgumentsBatched = false;
        bool m_IndirectDrawArgumentsPacketPageCulling = false;
        nvrhi::BufferHandle m_PacketPageMetadata;
        nvrhi::BufferHandle m_PacketPageRuntime;
        nvrhi::BufferHandle m_PacketRenderPages;
        uint32_t m_PacketPageMetadataCapacity = 0u;
        uint32_t m_PacketRenderPageCapacity = 0u;
        bool m_PacketPageCullingReady = false;
        bool m_PacketPageCullingUnavailableForPacketCache = false;
        bool m_ReportedPacketPageCullingFallback = false;
        std::array<nvrhi::BufferHandle, c_TimerLatency>
            m_DebugCounterReadbacks;
        std::array<bool, c_TimerLatency>
            m_DebugCounterReadbackPending{};
        std::array<uint64_t, c_TimerLatency>
            m_DebugCounterReadbackGenerations{};
        std::array<uint64_t, c_TimerLatency>
            m_DebugCounterReadbackSourceFrames{};
        SvsmResourceBackend m_DebugCounterRequestedBackend =
            SvsmResourceBackend::None;
        uint64_t m_DebugCounterGeneration = 1u;
        uint64_t m_LastAcceptedDebugCounterSourceFrame = 0u;
        bool m_LastAcceptedDebugCounterSourceFrameValid = false;
        nvrhi::BufferHandle m_SparseConstants;
        nvrhi::BindingLayoutHandle m_SparseBindingLayout;
        nvrhi::BindingSetHandle m_SparseBindingSet;
        std::array<nvrhi::ShaderHandle, 12> m_SparseShaders;
        std::array<nvrhi::ComputePipelineHandle, 12>
            m_SparsePipelines;
        nvrhi::ShaderHandle m_SparseScheduledTileMaskFillShader;
        nvrhi::ComputePipelineHandle
            m_SparseScheduledTileMaskFillPipeline;
        nvrhi::ShaderHandle m_SparseStaticDepthHierarchyFillShader;
        nvrhi::ComputePipelineHandle
            m_SparseStaticDepthHierarchyFillPipeline;
        nvrhi::ShaderHandle m_SparseDeferredStaticDepthMergeShader;
        nvrhi::ComputePipelineHandle
            m_SparseDeferredStaticDepthMergePipeline;
        nvrhi::ShaderHandle
            m_SparseScheduledTileMaskStaticDepthHierarchyFillShader;
        nvrhi::ComputePipelineHandle
            m_SparseScheduledTileMaskStaticDepthHierarchyFillPipeline;
        nvrhi::ShaderHandle m_SparsePrecomposedMarkShader;
        nvrhi::ComputePipelineHandle m_SparsePrecomposedMarkPipeline;
        nvrhi::ShaderHandle m_SparseReceiverPageMaskMarkShader;
        nvrhi::ComputePipelineHandle
            m_SparseReceiverPageMaskMarkPipeline;
        nvrhi::ShaderHandle
            m_SparsePrecomposedReceiverPageMaskMarkShader;
        nvrhi::ComputePipelineHandle
            m_SparsePrecomposedReceiverPageMaskMarkPipeline;
        nvrhi::ShaderHandle m_SparseReceiverPageMaskFillShader;
        nvrhi::ComputePipelineHandle
            m_SparseReceiverPageMaskFillPipeline;
        nvrhi::ShaderHandle
            m_SparseScheduledTileReceiverPageMaskFillShader;
        nvrhi::ComputePipelineHandle
            m_SparseScheduledTileReceiverPageMaskFillPipeline;
        nvrhi::BindingLayoutHandle m_SparseResolveBindingLayout;
        static constexpr uint32_t c_SparseResolveTapPermutationCount = 4u;
        static constexpr uint32_t c_SparseResolveTranslationPermutationCount =
            2u;
        static constexpr uint32_t
            c_SparseResolveReceiverTransformPermutationCount = 2u;
        static constexpr uint32_t
            c_SparseResolveFilterKernelPermutationCount = 2u;
        static constexpr uint32_t
            c_SparseResolvePoissonOrderingPermutationCount = 2u;
        static constexpr uint32_t c_SparseResolvePermutationCount =
            c_SparseResolveTapPermutationCount *
            c_SparseResolveTranslationPermutationCount *
            c_SparseResolveReceiverTransformPermutationCount *
            c_SparseResolveFilterKernelPermutationCount *
            c_SparseResolvePoissonOrderingPermutationCount;
        static constexpr uint32_t c_StaticVisibilityCacheSlotCount = 8u;
        std::array<nvrhi::BindingSetHandle,
            c_StaticVisibilityCacheSlotCount>
            m_SparseResolveBindingSets;
        std::array<nvrhi::TextureHandle,
            c_StaticVisibilityCacheSlotCount>
            m_SparseVisibilityCache;
        std::array<nvrhi::ShaderHandle, c_SparseResolvePermutationCount>
            m_SparseResolveShaders;
        std::array<nvrhi::ComputePipelineHandle,
            c_SparseResolvePermutationCount>
            m_SparseResolvePipelines;
        nvrhi::BindingLayoutHandle m_DebugBindingLayout;
        nvrhi::BindingSetHandle m_DebugBindingSet;
        nvrhi::ShaderHandle m_DebugPixelShader;
        nvrhi::GraphicsPipelineHandle m_DebugPipeline;
        std::unique_ptr<SparseDepthPass> m_SparseDepthPass;
        uint32_t m_AllocatedPhysicalPageCount = 0u;
        bool m_SparseResourcesNeedClear = true;
        std::array<std::shared_ptr<donut::engine::PlanarView>,
            SvsmClipmapCount> m_ClipmapViews;
        std::array<donut::math::int2, SvsmClipmapCount>
            m_CurrentRenderOrigins{};
        std::array<donut::math::int2, SvsmClipmapCount>
            m_PreviousRenderOrigins{};
        float m_CurrentLightDepthOrigin = 0.f;
        float m_PreviousLightDepthOrigin = 0.f;
        std::array<donut::math::float3, 3>
            m_PreviousLightBasis{};
        bool m_PreviousLightBasisValid = false;
        const donut::engine::DirectionalLight*
            m_PreviousProducingLight = nullptr;
        bool m_PreviousSparseLightUncached = true;
        uint32_t m_MovingLightLodRecoveryFramesRemaining = 0u;
        const donut::engine::SceneGraphNode*
            m_CachedSceneStateRoot = nullptr;
        uint64_t m_CachedSceneStateRevision =
            std::numeric_limits<uint64_t>::max();
        uint64_t m_CachedSceneStateHash = 0u;
        const donut::engine::SceneGraphNode*
            m_CachedBindingResourceRoot = nullptr;
        uint64_t m_CachedBindingResourceRevision =
            std::numeric_limits<uint64_t>::max();
        BindingResourceSignature
            m_CachedBindingResourceSignature;
        BindingResourceSignature
            m_CommittedBindingResourceSignature;
        bool m_CommittedBindingResourceSignatureValid = false;
        // Reset requests are transactions. A failed/disabled frame cannot
        // consume one and leave a stale Donut material or input binding set.
        bool m_DepthBindingCacheResetLatched = true;
        uint64_t m_PreviousSceneStateHash = 0u;
        uint64_t m_PreviousSceneStateRevision = 0u;
        bool m_PreviousSceneStateRevisionReliable = false;
        float m_PreviousFirstClipmapExtent = 0.f;
        float m_PreviousMaximumLightDepth = 0.f;
        bool m_PreviousLocalizedInvalidationEnabled = false;
        bool m_PreviousAdaptiveCasterCacheClassificationEnabled = false;
        // A full-refresh request is a transaction, not a one-frame hint.
        // Retain it across invalid inputs, unsupported modes, dense-reference
        // frames, and sparse failures until a sparse state commit succeeds.
        bool m_RequiresFullSceneInvalidationLatched = true;
        bool m_CacheStateValid = false;
        bool m_StaticPageRequestCacheReady = false;
        bool m_StaticPageRequestJitterActive = false;
        uint32_t m_StaticPageDrainFramesRemaining = 0u;
        uint32_t m_StaticPageRequestPageRenderBudget =
            std::numeric_limits<uint32_t>::max();
        bool m_StaticPageRequestCoarsestPageRenderBudgetEnabled = false;
        donut::math::float4x4
            m_StaticPageRequestCameraWorldToClip{};
        nvrhi::ITexture* m_StaticPageRequestCameraDepth = nullptr;
        uint32_t m_StaticPageRequestWidth = 0u;
        uint32_t m_StaticPageRequestHeight = 0u;
        nvrhi::Viewport m_StaticPageRequestViewport{};
        SvsmMarkingMode m_StaticPageRequestMarkingMode =
            SvsmMarkingMode::PerPixel;
        SvsmFilterMode m_StaticPageRequestFilterMode =
            SvsmFilterMode::ManualPageSafe;
        SvsmFilterKernel m_StaticPageRequestFilterKernel =
            SvsmFilterKernel::NearestPoisson;
        SvsmPoissonOrdering m_StaticPageRequestPoissonOrdering =
            SvsmPoissonOrdering::LegacyStride;
        SvsmTapCount m_StaticPageRequestTapCount =
            SvsmTapCount::Sixteen;
        SvsmResolutionBias m_StaticPageRequestResolutionBias =
            SvsmResolutionBias::Zero;
        float m_StaticPageRequestReceiverDistanceMipClampStart = 0.f;
        uint32_t m_StaticPageRequestReceiverDistanceMipClampMaximumLevel =
            0u;
        std::array<donut::math::float2,
            c_StaticVisibilityCacheSlotCount>
            m_StaticJitterOffsets{};
        std::array<bool, c_StaticVisibilityCacheSlotCount>
            m_StaticJitterOffsetValid{};
        std::array<bool, c_StaticVisibilityCacheSlotCount>
            m_StaticVisibilityValid{};
        bool m_StaticVisibilitySettingsValid = false;
        SvsmFilterMode m_StaticVisibilityFilterMode =
            SvsmFilterMode::ManualPageSafe;
        SvsmFilterKernel m_StaticVisibilityFilterKernel =
            SvsmFilterKernel::NearestPoisson;
        SvsmPoissonOrdering m_StaticVisibilityPoissonOrdering =
            SvsmPoissonOrdering::LegacyStride;
        SvsmTapCount m_StaticVisibilityTapCount =
            SvsmTapCount::Sixteen;
        SvsmResolutionBias m_StaticVisibilityResolutionBias =
            SvsmResolutionBias::Zero;
        float m_StaticVisibilityReceiverDistanceMipClampStart = 0.f;
        uint32_t m_StaticVisibilityReceiverDistanceMipClampMaximumLevel =
            0u;
        bool m_StaticVisibilityPageTranslationCaching = false;
        bool m_StaticVisibilityAdaptiveFiltering = false;
        std::unique_ptr<CasterSnapshotState> m_CasterSnapshotState;

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            c_TimerStageCount>
            m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            c_TimerStageCount> m_TimerPending{};
        std::array<bool, c_TimerStageCount> m_TimerStageActive{};
        std::array<uint32_t, c_TimerLatency> m_TimerIssuedStageMasks{};
        std::array<bool, c_TimerLatency> m_TimerSlotDiscarded{};
        std::array<uint64_t, c_TimerLatency> m_TimerSourceTags{};
        std::array<uint64_t, c_TimerLatency> m_TimerSourceFrames{};
        std::array<uint64_t, c_TimerLatency>
            m_TimerUiTimingGenerations{};
        std::array<bool, c_TimerLatency>
            m_TimerDetailedStagesEnabled{};
        std::array<std::array<float, c_TimerStageCount>, c_TimerLatency>
            m_TimerSlotValues{};
        std::deque<SparseVirtualShadowMapGpuTiming>
            m_CompletedTimingSamples;
        SparseVirtualShadowMapTimingAccounting m_TimingAccounting;
        SparseVirtualShadowMapTimings m_Timings;
        UiTimingContext m_UiTimingContext;
        bool m_UiTimingContextValid = false;
        uint64_t m_UiTimingGeneration = 1u;
        uint64_t m_LastAcceptedUiTimingSourceFrame = 0u;
        bool m_LastAcceptedUiTimingSourceFrameValid = false;
        uint64_t m_TimerFrame = 0u;
        uint64_t m_CurrentTimerSourceTag = 0u;
        uint32_t m_CurrentTimerSlot = 0u;
        uint32_t m_CurrentTimerIssuedStageMask = 0u;
        bool m_CurrentDetailedGpuTimingEnabled = true;
        bool m_TimerFrameAdmitted = false;
        bool m_TimerFrameDropRecorded = false;
        bool m_ReportedUnsupportedMode = false;
        bool m_ReportedInvalidInput = false;
        bool m_ReportedScheduledTileMaskFallback = false;
        bool m_ReportedStaticDepthHierarchyFallback = false;
        bool m_ReportedReceiverPageMaskFallback = false;
        bool m_ReportedDeferredStaticDepthMergeFallback = false;
        bool m_DeferredStaticDepthMergeRasterFallbackLatched = false;
        bool m_ReportedRasterSubmissionFailure = false;

        bool EnsureDenseResources(nvrhi::ITexture* cameraDepth);
        bool EnsureSparseResources(
            nvrhi::ITexture* cameraDepth,
            uint32_t physicalPageCount,
            bool pairedStaticDynamicDepthEnabled,
            bool deferredStaticDepthMergeEnabled,
            bool leanAlphaTestedBindingsEnabled);
        bool CreateSparseComputeBindingSet(
            nvrhi::ITexture* cameraDepth);
        nvrhi::BindingSetHandle CreateSparseComputeBindingSetForResources(
            nvrhi::ITexture* cameraDepth,
            nvrhi::IBuffer* indirectDrawArguments,
            nvrhi::IBuffer* packetPageMetadata,
            nvrhi::IBuffer* packetPageRuntime,
            nvrhi::IBuffer* packetRenderPages) const;
        bool EnsureIndirectDrawCapacity(
            uint32_t requiredPackets,
            bool& recreated);
        bool EnsurePacketPageCapacity(
            uint32_t requiredPackets,
            uint32_t requiredPageEntries,
            bool& recreated);
        bool UpdateClipmapViews(
            const SparseVirtualShadowMapSettings& settings,
            const donut::engine::IView& cameraView,
            const donut::engine::DirectionalLight& light,
            const std::shared_ptr<donut::engine::SceneGraphNode>&
                rootNode);
        uint64_t ComputeSceneStateHash(
            const std::shared_ptr<donut::engine::SceneGraphNode>&
                rootNode) const;
        BindingResourceSignature ComputeBindingResourceSignature(
            const std::shared_ptr<donut::engine::SceneGraphNode>&
                rootNode) const;
        SparseVirtualShadowMapResult RenderDense(
            nvrhi::ICommandList* commandList,
            const SparseVirtualShadowMapSettings& settings,
            const donut::engine::IView& cameraView,
            nvrhi::ITexture* cameraDepth,
            const donut::engine::DirectionalLight* light,
            const std::shared_ptr<donut::engine::SceneGraphNode>& rootNode,
            donut::render::InstancedOpaqueDrawStrategy& drawStrategy);
        SparseVirtualShadowMapResult RenderSparse(
            nvrhi::ICommandList* commandList,
            const SparseVirtualShadowMapSettings& settings,
            const donut::engine::IView& cameraView,
            nvrhi::ITexture* cameraDepth,
            const donut::engine::DirectionalLight* light,
            const std::shared_ptr<donut::engine::SceneGraphNode>& rootNode,
            donut::render::InstancedOpaqueDrawStrategy& drawStrategy,
            uint64_t sceneStateHash,
            uint64_t sceneStateRevision,
            bool sceneStateRevisionReliable,
            bool requiresFullSceneInvalidation,
            const SvsmObjectInvalidationResolver*
                objectInvalidationResolver);
        void AdvanceTimers();
        void InvalidateUiTimings();
        void UpdateUiTimingContext(
            const SparseVirtualShadowMapSettings& settings,
            SvsmResourceBackend backend,
            bool detailedGpuTimingEnabled);
        void PublishKnownZeroUiTiming();
        void InvalidateDebugCounters();
        void SetDebugCounterRequestedBackend(
            SvsmResourceBackend backend);
        void ReadDebugCounters(uint32_t slot);
        void BeginTimerFrame(
            uint64_t sourceTag,
            bool detailedGpuTimingEnabled);
        void DiscardCurrentTimerFrame();
        void EndTimerFrame();
        void BeginTimer(nvrhi::ICommandList* commandList, uint32_t stage);
        void EndTimer(nvrhi::ICommandList* commandList, uint32_t stage);

    public:
        SparseVirtualShadowMapPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses);
        ~SparseVirtualShadowMapPass();

        // requiresFullSceneInvalidation must be true for shadow-relevant
        // writes that the caller cannot assign to stable caster snapshots,
        // such as same-handle geometry-buffer or alpha-texture content edits.
        SparseVirtualShadowMapResult Render(
            nvrhi::ICommandList* commandList,
            const SparseVirtualShadowMapSettings& settings,
            const donut::engine::IView& cameraView,
            nvrhi::ITexture* cameraDepth,
            const donut::engine::DirectionalLight* light,
            const std::shared_ptr<donut::engine::SceneGraphNode>& rootNode,
            uint64_t sceneStateRevision,
            bool sceneStateRevisionReliable,
            bool requiresFullSceneInvalidation,
            bool requiresDepthBindingCacheReset,
            donut::render::InstancedOpaqueDrawStrategy& drawStrategy,
            uint64_t timingSourceTag = 0u,
            bool forceTotalOnlyGpuTiming = false,
            const SvsmObjectInvalidationResolver*
                objectInvalidationResolver = nullptr);

        const SparseVirtualShadowMapTimings& GetTimings() const
        {
            return m_Timings;
        }

        bool PopCompletedTiming(SparseVirtualShadowMapGpuTiming& timing);

        [[nodiscard]] const SparseVirtualShadowMapTimingAccounting&
        GetTimingAccounting() const
        {
            return m_TimingAccounting;
        }

        void ResetTimingAccounting();

        void PresentDebug(
            nvrhi::ICommandList* commandList,
            nvrhi::IFramebuffer* framebuffer);

        void Deactivate();
    };
}
