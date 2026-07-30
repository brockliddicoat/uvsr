#pragma once

#include "diagnostic_cascaded_shadow_map_settings.h"

#include <array>
#include <cstdint>
#include <memory>

namespace nvrhi
{
    class ICommandList;
    class IDevice;
    class ITexture;
}

namespace donut::engine
{
    class CommonRenderPasses;
    class DirectionalLight;
    class IView;
    class SceneGraphNode;
    class ShaderFactory;
}

namespace donut::render
{
    class InstancedOpaqueDrawStrategy;
}

namespace uvsr
{
    enum DiagnosticCsmInvalidationFlags : uint32_t
    {
        DiagnosticCsmInvalidation_None = 0u,
        DiagnosticCsmInvalidation_FirstFrame = 1u << 0u,
        DiagnosticCsmInvalidation_Light = 1u << 1u,
        DiagnosticCsmInvalidation_Projection = 1u << 2u,
        DiagnosticCsmInvalidation_DepthMapping = 1u << 3u,
        DiagnosticCsmInvalidation_Resources = 1u << 4u,
        DiagnosticCsmInvalidation_Bias = 1u << 5u,
        DiagnosticCsmInvalidation_Scene = 1u << 6u,
        DiagnosticCsmInvalidation_UnreliableBounds = 1u << 7u,
        DiagnosticCsmInvalidation_DirtyOverflow = 1u << 8u,
        DiagnosticCsmInvalidation_ScrollIncompatible = 1u << 9u,
        DiagnosticCsmInvalidation_Profile = 1u << 10u
    };

    [[nodiscard]] inline uint32_t
        GetDiagnosticCsmProjectionInvalidationFlags(
            const DiagnosticCsmProjectionCompatibility& previous,
            const DiagnosticCsmProjectionCompatibility& current)
    {
        if (ClassifyDiagnosticCsmProjectionChange(
                previous, current, 1.f).exactReuse)
        {
            return DiagnosticCsmInvalidation_None;
        }
        if (previous.lightIdentity != current.lightIdentity ||
            previous.lightBasis != current.lightBasis)
        {
            return DiagnosticCsmInvalidation_Light;
        }
        if (previous.resolution != current.resolution ||
            previous.formatKey != current.formatKey)
        {
            return DiagnosticCsmInvalidation_Resources;
        }
        if (previous.depthBias != current.depthBias ||
            previous.slopeScaledDepthBias !=
                current.slopeScaledDepthBias)
        {
            return DiagnosticCsmInvalidation_Bias;
        }
        if (previous.snappedCenterZ != current.snappedCenterZ ||
            previous.depthNear != current.depthNear ||
            previous.depthFar != current.depthFar ||
            previous.normalDepth != current.normalDepth)
        {
            return DiagnosticCsmInvalidation_DepthMapping;
        }
        return DiagnosticCsmInvalidation_Projection;
    }

    enum class DiagnosticCsmGpuTimingSource : uint32_t
    {
        Unavailable,
        TimerQuery,
        KnownZero
    };

    struct DiagnosticCsmTimings
    {
        bool supported = false;
        bool active = false;
        bool detailedGpuTimingEnabled = false;
        DiagnosticCsmGpuTimingSource gpuTimingSource =
            DiagnosticCsmGpuTimingSource::Unavailable;
        uint64_t gpuTimingSourceFrame = 0u;
        uint32_t gpuTimingAgeFrames = 0u;

        float setupCpuMilliseconds = 0.f;
        float cullingCpuMilliseconds = 0.f;
        float recordingCpuMilliseconds = 0.f;
        float totalCpuMilliseconds = 0.f;
        float cullingGpuMilliseconds = 0.f;
        float clearUpdateMilliseconds = 0.f;
        float rasterMilliseconds = 0.f;
        float samplingMilliseconds = 0.f;
        float totalMilliseconds = 0.f;
    };

    enum class DiagnosticCsmTimingFrameOrder
    {
        Unavailable,
        New,
        Duplicate,
        OutOfOrder
    };

    [[nodiscard]] inline DiagnosticCsmTimingFrameOrder
        ClassifyDiagnosticCsmTimingSourceFrame(
            uint64_t lastSourceFrame,
            bool lastSourceFrameValid,
            const DiagnosticCsmTimings& timings)
    {
        if (timings.gpuTimingSource !=
            DiagnosticCsmGpuTimingSource::TimerQuery)
        {
            return DiagnosticCsmTimingFrameOrder::Unavailable;
        }
        if (!lastSourceFrameValid ||
            timings.gpuTimingSourceFrame > lastSourceFrame)
        {
            return DiagnosticCsmTimingFrameOrder::New;
        }
        if (timings.gpuTimingSourceFrame == lastSourceFrame)
            return DiagnosticCsmTimingFrameOrder::Duplicate;
        return DiagnosticCsmTimingFrameOrder::OutOfOrder;
    }

    struct DiagnosticCsmStats
    {
        uint32_t outputWidth = 0u;
        uint32_t outputHeight = 0u;
        uint32_t cascadeCount = 0u;
        uint32_t shadowMapResolution = 0u;
        uint32_t depthBitsPerTexel = 0u;
        uint32_t filterSampleCount = 0u;
        uint32_t filterComparisonCount = 0u;
        float maximumShadowDistance = 0.f;
        float maximumLightDepth = 0.f;
        float maximumActualLightDepthSpan = 0.f;
        float filterRadiusTexels = 0.f;
        float finestCoverageExtent = 0.f;
        float coarsestCoverageExtent = 0.f;
        float finestWorldTexelSize = 0.f;
        float coarsestWorldTexelSize = 0.f;

        uint32_t candidateCasterProjectionPairs = 0u;
        uint32_t coarseCasterProjectionPairs = 0u;
        uint32_t accuratelyCulledCasterProjectionPairs = 0u;
        uint32_t radiusCulledCasterProjectionPairs = 0u;
        uint32_t renderedCasterProjectionPairs = 0u;
        uint32_t alphaTestedCasterProjectionPairs = 0u;
        uint32_t submittedDrawCalls = 0u;
        uint32_t submittedAlphaTestedDrawCalls = 0u;
        uint32_t submittedTranslationOnlyDrawCalls = 0u;
        uint32_t manualCasterProjectionPairs = 0u;
        uint32_t inputAssemblerCasterProjectionPairs = 0u;
        uint64_t submittedInstances = 0u;
        uint64_t submittedTriangles = 0u;
        uint64_t submittedTranslationOnlyTriangles = 0u;
        uint32_t casterSceneTraversals = 0u;
        uint32_t casterSorts = 0u;
        uint32_t cachedShadowDrawListHits = 0u;
        uint32_t cachedShadowDrawListMisses = 0u;
        uint32_t cachedShadowDrawListEntries = 0u;
        uint32_t cachedShadowDrawListCasterProjectionPairs = 0u;
        uint32_t reusedCascades = 0u;
        uint32_t scrolledCascades = 0u;
        uint32_t dirtyCascades = 0u;
        uint32_t redrawnCascades = 0u;
        uint32_t dirtyRectangleCount = 0u;
        uint32_t receiverRasterScissoredCascades = 0u;

        uint64_t logicalTexels = 0u;
        uint64_t updatedTexels = 0u;
        uint64_t copiedTexels = 0u;
        uint64_t clearedTexels = 0u;
        uint64_t fullRedrawRasterBoundTexels = 0u;
        uint64_t fullRedrawRasterExcludedTexels = 0u;
        uint64_t depthBytes = 0u;
        uint64_t visibilityBytes = 0u;
        uint64_t debugVisualizationBytes = 0u;
        uint64_t scrollingScratchBytes = 0u;
        uint32_t invalidationMask = DiagnosticCsmInvalidation_None;

        bool submissionStatsAvailable = false;
        bool opaqueDepthStateMergingEnabled = false;
        bool positionOnlyOpaqueEnabled = false;
        bool translationOnlyCasterTransformRequested = false;
        bool translationOnlyCasterTransformEnabled = false;
        bool inputAssemblerCasterFetchRequested = false;
        bool inputAssemblerCasterFetchEnabled = false;
        bool precomputedDepthAxisInverseLengthRequested = false;
        bool precomputedDepthAxisInverseLengthEnabled = false;
        bool conservativeSaturatedSlopeRequested = false;
        bool conservativeSaturatedSlopeActive = false;
        bool algebraicSlowSlopeRequested = false;
        bool algebraicSlowSlopeActive = false;
        bool preNormalizedReceiverLightDirectionRequested = false;
        bool preNormalizedReceiverLightDirectionEnabled = false;
        bool precomposedClipToShadowRequested = false;
        bool precomposedClipToShadowEnabled = false;
        bool accurateCasterCullingRequested = false;
        bool accurateCasterCullingEnabled = false;
        bool ueCasterRadiusThresholdRequested = false;
        bool ueCasterRadiusThresholdEnabled = false;
        bool singleTraversalCasterClassificationRequested = false;
        bool singleTraversalCasterClassificationEnabled = false;
        bool precomputedReceiverHullAxesRequested = false;
        bool precomputedReceiverHullAxesEnabled = false;
        bool sharedCasterLightProjectionRequested = false;
        bool sharedCasterLightProjectionEnabled = false;
        bool directCasterSubmissionRequested = false;
        bool directCasterSubmissionEnabled = false;
        bool cachedShadowDrawListsRequested = false;
        bool cachedShadowDrawListsActive = false;
        bool batchedFullRedrawClearRequested = false;
        bool batchedFullRedrawClearActive = false;
        bool receiverRasterScissorRequested = false;
        bool receiverRasterScissorEnabled = false;
        float casterRadiusThreshold = 0.f;

        const donut::engine::DirectionalLight* light = nullptr;
        DiagnosticCsmFilter filter = DiagnosticCsmFilter::Ue5Pcf5x5;
        std::array<DiagnosticCsmUpdateAction,
            DiagnosticCsmMaximumCascades> cascadeActions{};
    };

    // Runs deterministic differential coverage over the legacy and optimized
    // projected-caster overlap paths. This is intentionally side-effect free
    // so the focused reference test can verify all four toggle combinations.
    [[nodiscard]] bool
        ValidateDiagnosticCsmProjectedCasterOptimizationParity();

    struct DiagnosticCascadedShadowMapResult
    {
        nvrhi::ITexture* visibility = nullptr;
        nvrhi::ITexture* debugVisualization = nullptr;
        const donut::engine::DirectionalLight* light = nullptr;
        bool showDebug = false;
    };

    class DiagnosticCascadedShadowMapPass
    {
    public:
        DiagnosticCascadedShadowMapPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses);
        ~DiagnosticCascadedShadowMapPass();

        DiagnosticCascadedShadowMapPass(
            const DiagnosticCascadedShadowMapPass&) = delete;
        DiagnosticCascadedShadowMapPass& operator=(
            const DiagnosticCascadedShadowMapPass&) = delete;

        [[nodiscard]] DiagnosticCascadedShadowMapResult Render(
            nvrhi::ICommandList* commandList,
            const DiagnosticCascadedShadowMapSettings& settings,
            const donut::engine::IView& cameraView,
            nvrhi::ITexture* cameraDepth,
            nvrhi::ITexture* cameraNormals,
            const donut::engine::DirectionalLight* light,
            const std::shared_ptr<donut::engine::SceneGraphNode>& rootNode,
            uint64_t sceneStateRevision,
            bool sceneStateRevisionReliable,
            bool requiresFullSceneInvalidation,
            donut::render::InstancedOpaqueDrawStrategy& drawStrategy);

        void Deactivate();
        void ResetSceneState();

        [[nodiscard]] const DiagnosticCsmTimings& GetTimings() const;
        [[nodiscard]] const DiagnosticCsmStats& GetStats() const;

    private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
