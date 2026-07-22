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

    struct SparseVirtualShadowMapTimings
    {
        bool supported = false;
        bool active = false;
        uint64_t physicalDepthBytes = 0u;
        uint64_t visibilityBytes = 0u;
        uint64_t packetPageMetadataBytes = 0u;
        uint64_t packetPageListBytes = 0u;
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
        bool debugCountersAvailable = false;
        uint32_t debugCounterAgeFrames = 0u;
        bool staticPageRequestReuseActive = false;
        bool staticPageDrainActive = false;
        uint32_t staticPageDrainFramesRemaining = 0u;
        bool staticVisibilityReuseActive = false;
        bool batchedDrawSupported = false;
        bool batchedDrawActive = false;
        bool packetStateSortingActive = false;
        bool levelEmptyWorkSkipActive = false;
        bool packetPageCullingActive = false;
        bool dirtyPageScatterRasterActive = false;
        bool packetPageCullingUnavailable = false;
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
            bool dirtyPageScatterRasterActive = false;
            bool packetPageCullingUnavailable = false;
            uint32_t staticPageRequestReuseRejectMask = 0u;
        };

        static constexpr uint32_t c_TimerLatency = 4u;
        static constexpr uint32_t c_TimerStageCount = 7u;
        static constexpr size_t c_MaxCompletedTimingSamples = 2048u;
        static constexpr uint32_t c_DebugCounterCount =
            SvsmDebugCounterCount;
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
        nvrhi::BufferHandle m_PhysicalOwners;
        nvrhi::BufferHandle m_RenderPages;
        nvrhi::BufferHandle m_CompactRenderPages;
        nvrhi::BufferHandle m_DirtyPageRectangles;
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
        std::array<nvrhi::ShaderHandle, 8> m_SparseShaders;
        std::array<nvrhi::ComputePipelineHandle, 8>
            m_SparsePipelines;
        nvrhi::BindingLayoutHandle m_SparseResolveBindingLayout;
        static constexpr uint32_t c_SparseResolveTapPermutationCount = 4u;
        static constexpr uint32_t c_SparseResolveTranslationPermutationCount =
            2u;
        static constexpr uint32_t c_SparseResolvePermutationCount =
            c_SparseResolveTapPermutationCount *
            c_SparseResolveTranslationPermutationCount;
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
        const donut::engine::SceneGraphNode*
            m_CachedSceneStateRoot = nullptr;
        uint64_t m_CachedSceneStateRevision =
            std::numeric_limits<uint64_t>::max();
        uint64_t m_CachedSceneStateHash = 0u;
        uint64_t m_PreviousSceneStateHash = 0u;
        float m_PreviousFirstClipmapExtent = 0.f;
        float m_PreviousMaximumLightDepth = 0.f;
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
        SvsmTapCount m_StaticPageRequestTapCount =
            SvsmTapCount::Sixteen;
        SvsmResolutionBias m_StaticPageRequestResolutionBias =
            SvsmResolutionBias::Zero;
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
        SvsmTapCount m_StaticVisibilityTapCount =
            SvsmTapCount::Sixteen;
        SvsmResolutionBias m_StaticVisibilityResolutionBias =
            SvsmResolutionBias::Zero;
        bool m_StaticVisibilityPageTranslationCaching = false;
        bool m_StaticVisibilityAdaptiveFiltering = false;

        std::array<std::array<nvrhi::TimerQueryHandle, c_TimerLatency>,
            c_TimerStageCount>
            m_TimerQueries;
        std::array<std::array<bool, c_TimerLatency>,
            c_TimerStageCount> m_TimerPending{};
        std::array<bool, c_TimerStageCount> m_TimerStageActive{};
        std::array<uint32_t, c_TimerLatency> m_TimerIssuedStageMasks{};
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

        bool EnsureDenseResources(nvrhi::ITexture* cameraDepth);
        bool EnsureSparseResources(
            nvrhi::ITexture* cameraDepth,
            uint32_t physicalPageCount);
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
            uint64_t sceneStateHash);
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

        SparseVirtualShadowMapResult Render(
            nvrhi::ICommandList* commandList,
            const SparseVirtualShadowMapSettings& settings,
            const donut::engine::IView& cameraView,
            nvrhi::ITexture* cameraDepth,
            const donut::engine::DirectionalLight* light,
            const std::shared_ptr<donut::engine::SceneGraphNode>& rootNode,
            uint64_t sceneStateRevision,
            bool sceneStateRevisionReliable,
            donut::render::InstancedOpaqueDrawStrategy& drawStrategy,
            uint64_t timingSourceTag = 0u,
            bool forceTotalOnlyGpuTiming = false);

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
