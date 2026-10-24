#pragma once

#include "adaptive_msaa_scene.h"
#include "adaptive_msaa_diagnostics_pass.h"
#include "directional_light_visibility.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#ifndef UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY
#error UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY must be explicit
#endif
#if UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY != 0 && \
    UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY != 1
#error UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY is invalid
#endif

namespace donut::engine
{
    class CommonRenderPasses;
    class ICompositeView;
    class IView;
    class Light;
    struct LightProbe;
    class ShaderFactory;
}

namespace uvsr::adaptive_msaa
{
    class CompactRasterPreparedSubmission;

    enum class CompactPreparationTopology : uint8_t
    {
        Inline = 0u,
        Separated = 1u
    };

    inline constexpr CompactPreparationTopology
        SelectedCompactPreparationTopology =
            static_cast<CompactPreparationTopology>(
                UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY);
    inline constexpr bool CompactPreparationUsesSeparatedWorkLists =
        SelectedCompactPreparationTopology ==
            CompactPreparationTopology::Separated;
    // Shader Model 6 ballots admit at most 128 lanes. The live queue capacity
    // is min(this accepted maximum, WaveGetLaneCount()); tests may request a
    // smaller positive value to force and verify the inline overflow fallback.
    inline constexpr uint32_t CompactPreparationQueueCapacity = 128u;

    struct CompactPreparationQueueDecision
    {
        uint32_t actualCapacity = 0u;
        uint32_t occupancy = 0u;
        bool overflow = true;
        bool inlineFallback = true;
        FallbackReason reason = FallbackReason::WorkListOverflow;
    };

    // Device-independent mirror of separated-wave capacity accounting. An
    // overflow always selects the local inline preparation fallback and never
    // permits a covered sample to be dropped.
    [[nodiscard]] CompactPreparationQueueDecision
        EvaluateCompactPreparationQueue(
            uint32_t configuredCapacity,
            uint32_t liveWaveLaneCount,
            uint32_t occupancy);

    inline constexpr uint32_t CompactShadeThreadGroupWidth = 8u;
    inline constexpr uint32_t CompactShadeThreadGroupHeight = 8u;

    // Canonical Direct3D standard-pattern fixtures in signed sixteenths of a
    // pixel. Runtime reconstruction deliberately queries the bound
    // Texture2DMS with GetSamplePosition: quality level zero is allowed to be
    // a vendor pattern, and parity requires the positions used by the exact
    // key/depth resources rather than an assumed pattern.
    struct Dx12StandardSamplePosition
    {
        int8_t xSixteenths = 0;
        int8_t ySixteenths = 0;
    };

    struct Dx12StandardSamplePattern
    {
        std::array<
            Dx12StandardSamplePosition,
            MaximumAdaptiveSampleCount> positions{};
        uint32_t sampleCount = 0u;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    [[nodiscard]] Dx12StandardSamplePattern
        GetDx12StandardSamplePattern(uint32_t sampleCount);

    struct CompactShadeFloat2
    {
        float x = 0.f;
        float y = 0.f;
    };

    inline constexpr int32_t CompactRasterSubpixelScale = 256;
    inline constexpr int32_t CompactRasterPixelCenter = 128;
    inline constexpr int32_t CompactRasterCoordinateMinimum =
        -32768 * CompactRasterSubpixelScale;
    inline constexpr int32_t CompactRasterCoordinateMaximum =
        32767 * CompactRasterSubpixelScale;

    struct CompactRasterPosition
    {
        int32_t x = 0;
        int32_t y = 0;
    };

    struct CompactRasterSnapResult
    {
        CompactRasterPosition rasterPosition;
        CompactShadeFloat2 snappedWindowPosition;
        FallbackReason reason = FallbackReason::ReconstructionFailure;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    // Mirrors the Direct3D 16.8 fixed-point conversion used by the compact
    // visibility reconstruction, including round-to-nearest-even at exact
    // half-subpixel values and the legal D3D12 viewport-coordinate domain.
    [[nodiscard]] CompactRasterSnapResult SnapCompactRasterPosition(
        CompactShadeFloat2 windowPosition);

    struct CompactRasterCoverageResult
    {
        std::array<CompactRasterPosition, 3u> rasterPositions{};
        std::array<CompactShadeFloat2, 3u> snappedWindowPositions{};
        uint32_t coverageMask = 0u;
        int64_t signedArea = 0;
        bool frontFace = false;
        FallbackReason reason = FallbackReason::ReconstructionFailure;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    // Device-independent mirror of the exact top-left coverage test in
    // adaptive_msaa_compact_shade_common.hlsli. Standard sample patterns are
    // fixtures only; production shaders query the bound MSAA depth texture.
    [[nodiscard]] CompactRasterCoverageResult
        EvaluateCompactStandardRasterCoverage(
            const std::array<CompactShadeFloat2, 3u>& windowPositions,
            int32_t pixelX,
            int32_t pixelY,
            const Dx12StandardSamplePattern& samplePattern,
            bool mirroredView = false);

    struct CompactShadeFloat3
    {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

    struct CompactShadeFloat4
    {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        float w = 0.f;
    };

    struct CompactShadeAffineTransform
    {
        std::array<std::array<float, 4u>, 3u> rows{};
    };

    [[nodiscard]] CompactShadeFloat3 TransformCompactPosition(
        const CompactShadeAffineTransform& transform,
        CompactShadeFloat3 position);
    [[nodiscard]] CompactShadeFloat3 TransformCompactDirection(
        const CompactShadeAffineTransform& transform,
        CompactShadeFloat3 direction);
    [[nodiscard]] bool DetermineCompactFrontFace(
        const std::array<CompactShadeFloat2, 3u>& windowPositions,
        bool mirroredView);
    [[nodiscard]] bool ShouldFlipCompactSurfaceNormals(
        bool doubleSided,
        bool frontFace,
        CompactShadeFloat3 geometryNormal,
        CompactShadeFloat3 viewDirection);

    struct CompactShadeMotionProjection
    {
        CompactShadeFloat2 currentWindowPosition;
        float currentDeviceDepth = 0.f;
        CompactShadeFloat4 previousClipPosition;
        CompactShadeFloat2 clipToWindowScale;
        CompactShadeFloat2 clipToWindowBias;
        CompactShadeFloat2 currentPixelOffset;
        CompactShadeFloat2 previousPixelOffset;
    };

    // Alpha is one for a finite previous point in front of the camera and
    // zero otherwise, matching UVSR's valid-zero-motion convention.
    [[nodiscard]] CompactShadeFloat4 ReconstructCompactMotion(
        const CompactShadeMotionProjection& projection);

    struct CompactShadeProjectedVertex
    {
        // Window coordinates use Direct3D's top-left origin. reciprocalW and
        // deviceDepth are clip.w^-1 and clip.z / clip.w respectively.
        CompactShadeFloat2 windowPosition;
        float reciprocalW = 1.f;
        float deviceDepth = 0.f;
        CompactShadeFloat2 texCoord;
    };

    struct CompactShadePerspectiveInterpolation
    {
        std::array<float, 3u> affineBarycentrics{};
        std::array<float, 3u> perspectiveBarycentrics{};
        std::array<float, 3u> affineBarycentricDx{};
        std::array<float, 3u> affineBarycentricDy{};
        CompactShadeFloat2 texCoord;
        CompactShadeFloat2 texCoordDx;
        CompactShadeFloat2 texCoordDy;
        float deviceDepth = 0.f;
        FallbackReason reason = FallbackReason::ReconstructionFailure;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    // Device-independent mirror of the shader's perspective interpolation
    // and 2x2 coarse finite-difference gradients. It is used by focused tests
    // and CPU-side preflight tools; no raster attributes or G-buffer
    // attachment are required.
    [[nodiscard]] CompactShadePerspectiveInterpolation
        ReconstructCompactPerspectiveInterpolation(
            const std::array<CompactShadeProjectedVertex, 3u>& triangle,
            CompactShadeFloat2 evaluationPosition);

    inline constexpr uint32_t CompactReferencePixelCenter =
        InvalidVisibilityWord;

    // Mirrors Direct3D centroid interpolation for UVSR's unqualified
    // SceneVertex varyings: a fully covered primitive uses pixel center;
    // partial coverage uses the first covered sample in increasing index
    // order. pos/prevPos/texCoord remain pixel-center varyings regardless.
    [[nodiscard]] uint32_t SelectCompactReferenceCentroidSample(
        uint32_t sameSurfaceSampleMask,
        uint32_t sampleCount);

    struct CompactShadeSurfaceValidationDesc
    {
        GpuVisibilityKey key;
        CompactSceneGpuConstants scene;
        CompactGeometryInstanceGpuRecord geometryInstance;
        CompactGeometryGpuRecord geometry;

        uint32_t geometryInstanceRecordCount = 0u;
        uint32_t geometryInstanceRecordStride = 0u;
        uint32_t geometryRecordCount = 0u;
        uint32_t geometryRecordStride = 0u;
        uint32_t instanceHandleCount = 0u;
        uint32_t instanceHandleStride = 0u;
        uint32_t mappedGeometryInstanceHandle = InvalidVisibilityWord;
        uint32_t instanceGeometryCount = 0u;

        uint64_t sceneInstanceBufferByteSize = 0u;
        uint64_t materialBufferByteSize = 0u;
        uint64_t indexBufferByteSize = 0u;
        uint64_t vertexBufferByteSize = 0u;
        std::array<uint32_t, 3u> triangleVertexIndices{};
    };

    // Mirrors every stable mapping, revision, descriptor, primitive, index,
    // and vertex-range guard that precedes shader indirection. It intentionally
    // returns the first stable fail-closed reason.
    [[nodiscard]] FallbackReason ValidateCompactShadeSurface(
        const CompactShadeSurfaceValidationDesc& description);

    struct CompactClosestSurfaceSample
    {
        GpuVisibilityKey key;
        float deviceDepth = 0.f;
        bool reconstructionValid = true;
        FallbackReason reconstructionReason = FallbackReason::None;
    };

    struct CompactClosestSurfaceResult
    {
        bool foundSurface = false;
        uint32_t sampleIndex = 0u;
        float deviceDepth = 0.f;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    // UVSR is reverse-Z. Exact final key identity proves coverage, including
    // a finite far-plane hit at depth zero. The greatest finite depth wins;
    // exact ties keep the lowest sample index. If any covered owner is
    // invalid, the whole pixel fails closed instead of silently substituting
    // another sample.
    [[nodiscard]] CompactClosestSurfaceResult SelectCompactClosestSurface(
        const CompactClosestSurfaceSample* samples,
        uint32_t sampleCount);

    // Command-free proof input for every inverse-projection operation used by
    // compact reconstruction. Matrix storage is Donut row-major storage and
    // matches HLSL mul(rowVector, matrix). The pixel domain includes the
    // right/down neighbor needed by CompactPixelFootprint.
    struct CompactShadeViewReconstructionProofInput
    {
        std::array<float, 16u> worldToClip{};
        std::array<float, 16u> clipToWorld{};
        std::array<float, 16u> worldToView{};
        std::array<float, 2u> viewportOrigin{};
        std::array<float, 2u> viewportSize{};
        std::array<float, 2u> viewportSizeInv{};
        std::array<float, 4u> cameraDirectionOrPosition{};
        bool acceptedDepthValid = false;
        bool acceptedDepthHasGeometry = false;
        float minimumStoredDeviceDepth = 0.f;
        float maximumStoredDeviceDepth = 0.f;
        float conservativeDepthErrorMargin = 0.f;
    };

    struct CompactShadeViewReconstructionProof
    {
        bool safe = false;
        FallbackReason reason = FallbackReason::ReconstructionFailure;
        float minimumAbsoluteWorldW = 0.f;
        float minimumAbsoluteViewW = 0.f;
        float maximumAbsoluteWorldPosition = 0.f;
        float maximumPixelFootprint = 0.f;
    };

    // Rejects singular, non-finite, ill-conditioned, or overflow-prone
    // inverse reconstruction before a command list is opened. Empty raster
    // views are accepted only with the exact canonical empty-depth proof.
    [[nodiscard]] CompactShadeViewReconstructionProof
        EvaluateCompactShadeViewReconstructionProof(
            const CompactShadeViewReconstructionProofInput& input);

    // Optional full-G-buffer payload for samples marked as local compact
    // fallback after final depth testing. The tuple is all-or-none. Compact-
    // only scenes leave it empty and bind the proven depth SRV as an
    // unreachable descriptor placeholder, so they retain no full-resolution
    // multisampled material attachments.
    struct AdaptiveMsaaCompactFallbackGBufferInputs
    {
        nvrhi::ITexture* diffuse = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* emissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
        nvrhi::ITexture* motionVectors = nullptr;

        [[nodiscard]] bool Empty() const
        {
            return !diffuse && !material && !normals && !emissive &&
                !materialAmbientOcclusion && !motionVectors;
        }

        [[nodiscard]] bool Complete() const
        {
            return diffuse && material && normals && emissive &&
                materialAmbientOcclusion && motionVectors;
        }
    };

    struct AdaptiveMsaaCompactGuideInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* surfaceOwnership = nullptr;
        nvrhi::ITexture* visibilityClass = nullptr;
        AdaptiveMsaaCompactFallbackGBufferInputs fallbackGBuffer;
    };

    struct AdaptiveMsaaCompactGuideOutputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* diffuse = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* emissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
        nvrhi::ITexture* motionVectors = nullptr;
    };

    struct AdaptiveMsaaCompactLightingInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* surfaceOwnership = nullptr;
        nvrhi::ITexture* visibilityClass = nullptr;
        AdaptiveMsaaCompactFallbackGBufferInputs fallbackGBuffer;
        nvrhi::ITexture* resolvedBackground = nullptr;
        nvrhi::ITexture* visibilityBaseLighting = nullptr;
        nvrhi::ITexture* visibilityComposite = nullptr;
        nvrhi::ITexture* output = nullptr;
        const std::vector<std::shared_ptr<donut::engine::Light>>*
            lights = nullptr;
        AdaptiveMsaaDiagnosticsProducerOutputs* diagnosticsProducer =
            nullptr;
        uint32_t preparationQueueCapacity =
            CompactPreparationQueueCapacity;
    };

    struct CompactShadeDispatchResult
    {
        FallbackReason reason = FallbackReason::ReconstructionFailure;
        uint32_t dispatchedViewCount = 0u;
        bool diagnosticsProduced = false;
        uint32_t diagnosticsCapabilityMask = 0u;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    enum class CompactShadePreparedPhase : uint8_t
    {
        Prepared = 0u,
        GuideRecorded = 1u,
        Consumed = 2u
    };

    struct CompactShadePreparedPhaseTransition
    {
        CompactShadePreparedPhase phase =
            CompactShadePreparedPhase::Consumed;
        FallbackReason reason = FallbackReason::ReconstructionFailure;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    // A prepared frame normally records guide reconstruction before lighting.
    // The caller may authorize Prepared -> Consumed only after preparation
    // captured that no guide consumer exists and exact outcome selection chose
    // neutral screen-space and directional visibility. Every other repeat,
    // skip, or post-consume transition fails closed.
    [[nodiscard]] CompactShadePreparedPhaseTransition
        AdvanceCompactShadePreparedPhase(
            CompactShadePreparedPhase current,
            CompactShadePreparedPhase requested,
            bool allowGuideSkip = false);

    inline constexpr uint32_t CompactShadeLightingOutcomeCount = 4u;

    struct CompactShadeLightingOutcomeSelection
    {
        uint32_t variantIndex = InvalidVisibilityWord;
        bool visibilitySuccess = false;
        bool directionalActive = false;
        FallbackReason reason = FallbackReason::ReconstructionFailure;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    // Selects one exact precreated variant. Neutral directional visibility is
    // exactly {nullptr, nullptr}; partial tuples and any active tuple other
    // than the prediction fail closed.
    [[nodiscard]] CompactShadeLightingOutcomeSelection
        SelectCompactShadeLightingOutcome(
            bool visibilityRequested,
            bool finalVisibilitySuccess,
            const DirectionalLightVisibility& predictedDirectional,
            const DirectionalLightVisibility& actualDirectional);

    struct CompactShadeDiagnosticsPublication
    {
        bool produced = false;
        uint32_t capabilityMask = 0u;
    };

    // Diagnostics are published only after an exact outcome dispatched every
    // prepared view. Failed or partial prepared recording remains invisible
    // to the diagnostics consumer.
    [[nodiscard]] CompactShadeDiagnosticsPublication
        ResolveCompactShadeDiagnosticsPublication(
            bool diagnosticsPrepared,
            bool exactOutcomeSelected,
            uint32_t dispatchedViewCount,
            uint32_t preparedViewCount,
            uint32_t preparedCapabilityMask);

    struct AdaptiveMsaaCompactShadePrepareParameters
    {
        const CompactRasterPreparedSubmission* raster = nullptr;
        std::shared_ptr<const CompactSceneSnapshot> snapshot;
        const donut::engine::ICompositeView* currentView = nullptr;
        const donut::engine::ICompositeView* previousView = nullptr;
        AdaptiveMsaaCompactGuideInputs guideInputs;
        AdaptiveMsaaCompactGuideOutputs guideOutputs;
        AdaptiveMsaaCompactLightingInputs lightingInputs;
        uint32_t sampleCount = 0u;
        // TAA, screen-space visibility/shadows, and reconstruction comparison
        // set this. With no guide consumer, prepared lighting may skip the
        // guide phase only for the neutral screen-space visibility outcome.
        bool guideRequired = true;
        // Exact command-free prediction from the prepared directional
        // producer. Canonical neutral is {nullptr, nullptr}.
        DirectionalLightVisibility directionalVisibility;
        const donut::engine::LightProbe* environment = nullptr;
        bool separateIndirect = false;
        uint32_t lightingDebugView = 0u;
        uint32_t visibilityDebugView = 0u;
        float randomOffsetX = 0.f;
        float randomOffsetY = 0.f;
    };

    // A single-use, move-only transaction containing the exact view
    // constants, resource identities, PSOs, binding sets, and dispatch sizes
    // for lighting and, when requested, compact guide reconstruction. A
    // successful token performs no allocation or scene/view/resource discovery
    // while commands are being recorded.
    class CompactShadePreparedFrame final
    {
    public:
        CompactShadePreparedFrame();
        ~CompactShadePreparedFrame();

        CompactShadePreparedFrame(
            CompactShadePreparedFrame&& other) noexcept;
        CompactShadePreparedFrame& operator=(
            CompactShadePreparedFrame&& other) noexcept;

        CompactShadePreparedFrame(
            const CompactShadePreparedFrame&) = delete;
        CompactShadePreparedFrame& operator=(
            const CompactShadePreparedFrame&) = delete;

        [[nodiscard]] bool IsSubmittable() const;
        [[nodiscard]] FallbackReason Reason() const;
        [[nodiscard]] uint32_t ViewCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        FallbackReason m_Reason = FallbackReason::ReconstructionFailure;

        friend class AdaptiveMsaaCompactShadePass;
    };

    // Owns PSOs compiled against one exact immutable compact-scene bindless
    // layout. Render calls additionally require pointer and snapshot-version
    // equality, preventing a descriptor table from being mixed with records
    // or constants from another publication.
    class AdaptiveMsaaCompactShadePass final
    {
    public:
        AdaptiveMsaaCompactShadePass(
            nvrhi::IDevice* device,
            std::shared_ptr<donut::engine::CommonRenderPasses>
                commonPasses);
        ~AdaptiveMsaaCompactShadePass();

        AdaptiveMsaaCompactShadePass(
            const AdaptiveMsaaCompactShadePass&) = delete;
        AdaptiveMsaaCompactShadePass& operator=(
            const AdaptiveMsaaCompactShadePass&) = delete;

        void Init(
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory,
            bool whiteWorld,
            bool deferPipelineCreation = false);

        [[nodiscard]] FallbackReason BindSnapshot(
            std::shared_ptr<const CompactSceneSnapshot> snapshot);
        void ClearSnapshot();

        [[nodiscard]] bool IsBoundTo(
            const std::shared_ptr<const CompactSceneSnapshot>&
                snapshot) const;
        [[nodiscard]] std::shared_ptr<const CompactSceneSnapshot>
            BoundSnapshot() const;
        [[nodiscard]] CompactSceneSnapshotVersion
            BoundSnapshotVersion() const;

        [[nodiscard]] bool PreparePipelinesStep();
        [[nodiscard]] bool ArePipelinesReady() const;

        [[nodiscard]] FallbackReason ValidateGuideDispatch(
            const std::shared_ptr<const CompactSceneSnapshot>& snapshot,
            const donut::engine::ICompositeView& currentView,
            const donut::engine::ICompositeView* previousView,
            const AdaptiveMsaaCompactGuideInputs& inputs,
            const AdaptiveMsaaCompactGuideOutputs& outputs,
            uint32_t sampleCount) const;

        [[nodiscard]] FallbackReason ValidateLightingDispatch(
            const std::shared_ptr<const CompactSceneSnapshot>& snapshot,
            const donut::engine::ICompositeView& currentView,
            const donut::engine::ICompositeView* previousView,
            const AdaptiveMsaaCompactLightingInputs& inputs,
            uint32_t sampleCount) const;

        // Performs the complete recoverable shade-side transaction before a
        // command list is opened. ViewProofs are consumed one-for-one but the
        // raster token remains owned by its caller for immediate submission.
        [[nodiscard]] CompactShadePreparedFrame PrepareFrame(
            const AdaptiveMsaaCompactShadePrepareParameters& parameters);

        // Call immediately before command-list open and before moving the
        // raster token. This command-free check rejects any view, proof,
        // constant, viewport, subresource, or raster-identity mutation.
        [[nodiscard]] FallbackReason RevalidatePreparedFrame(
            const CompactShadePreparedFrame& prepared,
            const CompactRasterPreparedSubmission& raster) const;

        // Prepared recording overloads. Resolve advances a guide-required
        // token to its lighting phase. RenderLighting consumes every token,
        // selects only the exact precreated final visibility/directional
        // outcome, and may skip Resolve only for a prepared no-guide token's
        // fully neutral visibility outcome.
        [[nodiscard]] CompactShadeDispatchResult ResolveClosestSurface(
            nvrhi::ICommandList* commandList,
            CompactShadePreparedFrame& prepared);
        [[nodiscard]] CompactShadeDispatchResult RenderLighting(
            nvrhi::ICommandList* commandList,
            CompactShadePreparedFrame&& prepared,
            const DirectionalLightVisibility& actualDirectionalVisibility,
            bool finalVisibilitySuccess);

        [[nodiscard]] CompactShadeDispatchResult ResolveClosestSurface(
            nvrhi::ICommandList* commandList,
            const std::shared_ptr<const CompactSceneSnapshot>& snapshot,
            const donut::engine::ICompositeView& currentView,
            const donut::engine::ICompositeView* previousView,
            const AdaptiveMsaaCompactGuideInputs& inputs,
            const AdaptiveMsaaCompactGuideOutputs& outputs,
            uint32_t sampleCount);

        [[nodiscard]] CompactShadeDispatchResult RenderLighting(
            nvrhi::ICommandList* commandList,
            const std::shared_ptr<const CompactSceneSnapshot>& snapshot,
            const donut::engine::ICompositeView& currentView,
            const donut::engine::ICompositeView* previousView,
            const AdaptiveMsaaCompactLightingInputs& inputs,
            uint32_t sampleCount,
            const DirectionalLightVisibility& directionalVisibility,
            const donut::engine::LightProbe* environment,
            bool separateIndirect,
            uint32_t lightingDebugView,
            uint32_t visibilityDebugView,
            float randomOffsetX = 0.f,
            float randomOffsetY = 0.f);

        void ResetBindingCache();
        void ResetAdaptiveDiagnosticsResources();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
