#include "adaptive_msaa_diagnostics_pass.h"
#include "adaptive_msaa_raster_cb.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

namespace
{
    using namespace uvsr::adaptive_msaa;

    static_assert(DiagnosticsGpuAbiVersion == 6u);
    static_assert(DiagnosticsGpuAbiInitializationSentinel == 0xffffffffu);
    static_assert(DiagnosticsReadbackRingCapacity == 3u);
    static_assert(DiagnosticsGpuTimerRingCapacity == 4u);
    static_assert(DiagnosticsGpuThreadGroupWidth == 8u);
    static_assert(DiagnosticsGpuThreadGroupHeight == 8u);
    static_assert(DiagnosticsGpuUnavailableFallbackReason == 0x1fu);
    static_assert(DiagnosticsGpuUnavailableErrorBudgetReason == 0x7u);
    static_assert(DiagnosticsGpuSampleStatusBytesPerSample == 1u);
    static_assert(DiagnosticsReconstructionDepthThreshold ==
        1.f / 1048576.f);
    static_assert(DiagnosticsReconstructionDiffuseThreshold == 1.f / 255.f);
    static_assert(DiagnosticsReconstructionMaterialThreshold == 1.f / 255.f);
    static_assert(DiagnosticsReconstructionNormalThreshold == 1.f / 1024.f);
    static_assert(DiagnosticsReconstructionEmissiveThreshold ==
        DiagnosticsHdrPercentile99Limit);
    static_assert(DiagnosticsReconstructionAmbientOcclusionThreshold ==
        1.f / 255.f);
    static_assert(DiagnosticsReconstructionMotionThreshold == 1.f / 1024.f);
    static_assert(PackDiagnosticsGpuSampleStatus(23u, 3u) == 0x77u);
    static_assert(PackDiagnosticsGpuSampleStatus(24u, 3u) == 0xffu);
    static_assert(PackDiagnosticsGpuSampleStatus(21u, 7u) == 0xf5u);
    static_assert(PackDiagnosticsGpuSampleStatus(31u, 3u) == 0x7fu);
    static_assert(PackDiagnosticsGpuSampleStatus(21u, 4u) == 0xffu);
    static_assert(IsValidDiagnosticsGpuSampleStatusReasons(23u, 3u));
    static_assert(!IsValidDiagnosticsGpuSampleStatusReasons(24u, 3u));
    static_assert(UnpackDiagnosticsGpuFallbackReason(0x75u) == 21u);
    static_assert(UnpackDiagnosticsGpuErrorBudgetReason(0x75u) == 3u);
    static_assert(sizeof(DiagnosticsGpuSampleInputRecord) == 48u);
    static_assert(sizeof(DiagnosticsGpuPixelMetadataRecord) == 32u);
    static_assert(offsetof(
        DiagnosticsGpuPixelMetadataRecord,
        errorBudgetNonFiniteGroupCount) == 16u);
    static_assert(offsetof(
        DiagnosticsGpuPixelMetadataRecord,
        errorBudgetHdrGroupCount) == 20u);
    static_assert(offsetof(
        DiagnosticsGpuPixelMetadataRecord,
        errorBudgetDisplayGroupCount) == 24u);
    static_assert(offsetof(
        DiagnosticsGpuPixelMetadataRecord,
        errorBudgetRejectedSampleCount) == 28u);
    static_assert(sizeof(DiagnosticsProducerGpuConstants) == 32u);
    static_assert(sizeof(DiagnosticsGpuCounterBlock) == 264u);
    static_assert(sizeof(DiagnosticsGpuSelectedHeader) == 64u);
    static_assert(sizeof(DiagnosticsGpuSelectedSampleRecord) == 64u);
    static_assert(sizeof(DiagnosticsGpuReadbackBlock) == 1352u);
    static_assert(DiagnosticsGpuCounterGroupHistogramOffset == 32u);
    static_assert(DiagnosticsGpuCounterReferenceEvaluationsOffset == 100u);
    static_assert(DiagnosticsGpuCounterFallbackReasonsOffset == 112u);
    static_assert(DiagnosticsGpuCounterInvalidKeysOffset == 208u);
    static_assert(DiagnosticsGpuCounterStaleKeysOffset == 212u);
    static_assert(DiagnosticsGpuCounterMappingFailuresOffset == 216u);
    static_assert(DiagnosticsGpuCounterWorkListObservationsOffset == 220u);
    static_assert(DiagnosticsGpuCounterErrorBudgetReasonsOffset == 240u);
    static_assert(DiagnosticsGpuSelectedHeaderOffset == 264u);
    static_assert(DiagnosticsGpuSelectedSamplesOffset == 328u);
    static_assert(DiagnosticsGpuCounterAbiVersionOffset == 0u);
    static_assert(DiagnosticsGpuCounterByteSizeOffset == 4u);
    static_assert(DiagnosticsGpuSelectedAbiVersionOffset == 264u);
    static_assert(DiagnosticsGpuSelectedByteSizeOffset == 268u);
    static_assert(DiagnosticsCounterDispatchComplete == 2u);
    static_assert(DiagnosticsSelectedHeaderDispatchComplete == 4u);
    static_assert(DiagnosticsCounterGroupHistogramValid == 0x01u);
    static_assert(DiagnosticsCounterLightingValid == 0x02u);
    static_assert(DiagnosticsCounterFallbackReasonsValid == 0x04u);
    static_assert(DiagnosticsCounterInvalidKeysValid == 0x08u);
    static_assert(DiagnosticsCounterStaleAndMappingValid == 0x10u);
    static_assert(DiagnosticsCounterWorkListValid == 0x20u);
    static_assert(DiagnosticsCounterErrorBudgetValid == 0x40u);
    static_assert(DiagnosticsCounterAllValidityBits == 0x7fu);
    static_assert(DiagnosticsProducerWriteSampleStatus == 0x01u);
    static_assert(DiagnosticsProducerWritePixelMetadata == 0x02u);
    static_assert(DiagnosticsProducerWriteSelectedSamples == 0x04u);
    static_assert(DiagnosticsProducerValidateErrorBudget == 0x08u);
    static_assert(DiagnosticsPixelMetadataErrorBudgetValid == 0x02000000u);
    inline constexpr uint32_t PackedMaximumGroupData =
        PackDiagnosticsPixelMetadataGroupData(16u, 0x8001u);
    static_assert(
        (PackedMaximumGroupData & DiagnosticsPixelMetadataGroupDataValid) !=
        0u);
    static_assert(
        UnpackDiagnosticsPixelMetadataGroupCount(PackedMaximumGroupData) ==
        16u);
    static_assert(
        UnpackDiagnosticsPixelMetadataRepresentativeMask(
            PackedMaximumGroupData) == 0x8001u);
    static_assert(PackDiagnosticsPixelMetadataGroupData(17u, 0x0001u) == 0u);
    static_assert(PackDiagnosticsPixelMetadataGroupData(16u, 0x10000u) == 0u);
    static_assert(
        DiagnosticsGpuSelectedSamplesOffset +
            MaximumAdaptiveSampleCount *
                sizeof(DiagnosticsGpuSelectedSampleRecord) ==
        sizeof(DiagnosticsGpuReadbackBlock));

    bool g_Passed = true;

    void Check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "Adaptive MSAA diagnostics pass validation failed: "
                << message << '\n';
            g_Passed = false;
        }
    }

    uint32_t FloatBits(float value)
    {
        uint32_t result = 0u;
        static_assert(sizeof(result) == sizeof(value));
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    float FloatFromBits(uint32_t value)
    {
        float result = 0.f;
        static_assert(sizeof(result) == sizeof(value));
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    DiagnosticsGpuTextureDesc MakeTexture(
        DiagnosticsGpuTextureFormat format,
        DiagnosticsGpuTextureDimension dimension,
        uint32_t width,
        uint32_t height,
        uint32_t sampleCount,
        uint32_t sampleQuality = 0u)
    {
        DiagnosticsGpuTextureDesc result;
        result.supplied = true;
        result.width = width;
        result.height = height;
        result.depth = 1u;
        result.arraySize = 1u;
        result.mipLevels = 1u;
        result.sampleCount = sampleCount;
        result.sampleQuality = sampleQuality;
        result.format = format;
        result.dimension = dimension;
        result.shaderResource = true;
        return result;
    }

    DiagnosticsGpuTextureDesc MakeKeys(
        uint32_t width,
        uint32_t height,
        uint32_t sampleCount,
        uint32_t sampleQuality = 0u)
    {
        return MakeTexture(
            DiagnosticsGpuTextureFormat::Rg32Uint,
            DiagnosticsGpuTextureDimension::Texture2DMS,
            width,
            height,
            sampleCount,
            sampleQuality);
    }

    DiagnosticsGpuTextureDesc MakeDepth(
        uint32_t width,
        uint32_t height,
        uint32_t sampleCount,
        uint32_t sampleQuality = 0u)
    {
        return MakeTexture(
            DiagnosticsGpuTextureFormat::Depth,
            DiagnosticsGpuTextureDimension::Texture2DMS,
            width,
            height,
            sampleCount,
            sampleQuality);
    }

    DiagnosticsGpuTextureDesc MakeRasterSurfaceHits(
        uint32_t width,
        uint32_t height,
        uint32_t sampleCount,
        uint32_t sampleQuality = 0u)
    {
        return MakeTexture(
            DiagnosticsGpuTextureFormat::Rgba32Float,
            DiagnosticsGpuTextureDimension::Texture2DMS,
            width,
            height,
            sampleCount,
            sampleQuality);
    }

    DiagnosticsGpuTextureDesc MakeVisibilityClass(
        uint32_t width,
        uint32_t height,
        uint32_t sampleCount,
        DiagnosticsGpuTextureFormat format =
            DiagnosticsGpuTextureFormat::R8Uint,
        uint32_t sampleQuality = 0u)
    {
        return MakeTexture(
            format,
            DiagnosticsGpuTextureDimension::Texture2DMS,
            width,
            height,
            sampleCount,
            sampleQuality);
    }

    DiagnosticsGpuTextureDesc MakeSampleStatus(
        uint32_t width,
        uint32_t height,
        uint32_t sampleCount)
    {
        DiagnosticsGpuTextureDesc result = MakeTexture(
            DiagnosticsGpuTextureFormat::R8Uint,
            DiagnosticsGpuTextureDimension::Texture2DArray,
            width,
            height,
            1u);
        result.arraySize = sampleCount;
        return result;
    }

    DiagnosticsGpuTextureDesc MakeOutput(uint32_t width, uint32_t height)
    {
        DiagnosticsGpuTextureDesc result = MakeTexture(
            DiagnosticsGpuTextureFormat::Rgba16Float,
            DiagnosticsGpuTextureDimension::Texture2D,
            width,
            height,
            1u);
        result.shaderResource = false;
        result.unorderedAccess = true;
        return result;
    }

    DiagnosticsGpuTextureDesc MakeSingleColor(
        uint32_t width,
        uint32_t height)
    {
        return MakeTexture(
            DiagnosticsGpuTextureFormat::Rgba16Float,
            DiagnosticsGpuTextureDimension::Texture2D,
            width,
            height,
            1u);
    }

    DiagnosticsGpuTextureDesc MakeSingleTyped(
        uint32_t width,
        uint32_t height,
        DiagnosticsGpuTextureFormat format)
    {
        return MakeTexture(
            format,
            DiagnosticsGpuTextureDimension::Texture2D,
            width,
            height,
            1u);
    }

    void PopulateCompleteReconstruction(
        AdaptiveMsaaDiagnosticsPassPlanDesc& description,
        uint32_t width = 1920u,
        uint32_t height = 1080u)
    {
        description.referenceDepth = MakeSingleTyped(
            width, height, DiagnosticsGpuTextureFormat::R32Float);
        description.reconstructedDepth = description.referenceDepth;
        description.referenceDiffuse = MakeSingleColor(width, height);
        description.reconstructedDiffuse = description.referenceDiffuse;
        description.referenceMaterial = MakeSingleColor(width, height);
        description.reconstructedMaterial = description.referenceMaterial;
        description.referenceNormals = MakeSingleColor(width, height);
        description.reconstructedNormals = description.referenceNormals;
        description.referenceEmissive = MakeSingleColor(width, height);
        description.reconstructedEmissive =
            description.referenceEmissive;
        description.referenceAmbientOcclusion = MakeSingleTyped(
            width, height, DiagnosticsGpuTextureFormat::R16Float);
        description.reconstructedAmbientOcclusion =
            description.referenceAmbientOcclusion;
        description.referenceMotion = MakeSingleColor(width, height);
        description.reconstructedMotion = description.referenceMotion;
    }

    AdaptiveMsaaDiagnosticsPassPlanDesc MakePlanDescription(
        uint32_t sampleCount,
        AdaptiveMsaaDebugView view =
            AdaptiveMsaaDebugView::SurfaceOwnershipKeys,
        uint32_t width = 1920u,
        uint32_t height = 1080u)
    {
        AdaptiveMsaaDiagnosticsPassPlanDesc result;
        result.settings.enabled = true;
        result.settings.debugView = view;
        result.surfaceKeys = MakeKeys(width, height, sampleCount);
        result.depth = MakeDepth(width, height, sampleCount);
        if (view != AdaptiveMsaaDebugView::Disabled)
            result.output = MakeOutput(width, height);
        return result;
    }

    void TestDisabledAndNoWorkGates()
    {
        AdaptiveMsaaDiagnosticsPassRequest benchmarkRequest;
        Check(IsAcceptedBenchmarkDiagnosticsPassRequestSafe(
                benchmarkRequest),
            "default diagnostics pass request is accepted-benchmark safe");
        benchmarkRequest.gpuErrorMetricsEnabled = true;
        Check(!IsAcceptedBenchmarkDiagnosticsPassRequestSafe(
                benchmarkRequest),
            "GPU error metrics independently disqualify an accepted benchmark");
        benchmarkRequest = {};
        benchmarkRequest.frozenGpuErrorHistoryReadOnly = true;
        Check(!IsAcceptedBenchmarkDiagnosticsPassRequestSafe(
                benchmarkRequest),
            "frozen history reuse independently disqualifies an accepted benchmark");

        AdaptiveMsaaDiagnosticsPassPlanDesc disabled;
        disabled.surfaceKeys.supplied = true;
        disabled.surfaceKeys.width = std::numeric_limits<uint32_t>::max();
        const AdaptiveMsaaDiagnosticsPassPlan disabledPlan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(disabled);
        Check(disabledPlan.status == DiagnosticsPassStatus::Disabled,
            "disabled state wins before resource inspection");
        Check(!disabledPlan.operations.allocate &&
                !disabledPlan.operations.dispatch &&
                !disabledPlan.operations.copy &&
                !disabledPlan.operations.query &&
                !disabledPlan.operations.timerQuery &&
                !disabledPlan.operations.map &&
                !disabledPlan.operations.retainDualRenderTargets,
            "disabled state plans no GPU or retention operation");

        AdaptiveMsaaDiagnosticsGpuPass pass(nullptr, {});
        AdaptiveMsaaDiagnosticsPassInputs inputs;
        AdaptiveMsaaDiagnosticsPassRequest request;
        const AdaptiveMsaaDiagnosticsPassResult runtimeDisabled =
            pass.Execute(nullptr, inputs, request);
        Check(runtimeDisabled.status == DiagnosticsPassStatus::Disabled &&
                !runtimeDisabled.dispatched &&
                !runtimeDisabled.readbackRecorded &&
                !runtimeDisabled.timerQueryRecorded &&
                !runtimeDisabled.timing.available &&
                !pass.GetLatestGpuTiming().available &&
                !pass.HasAllocatedGpuResources(),
            "disabled runtime call returns before allocation or dispatch");
        request.gpuErrorMetricsEnabled = true;
        request.gpuErrorHistoryEnabled = true;
        request.historyIdentity = { 1u, 1u };
        const AdaptiveMsaaDiagnosticsPassResult runtimeDisabledMetrics =
            pass.Execute(nullptr, inputs, request);
        Check(runtimeDisabledMetrics.status ==
                    DiagnosticsPassStatus::Disabled &&
                !runtimeDisabledMetrics.dispatched &&
                !runtimeDisabledMetrics.historyWriteRecorded &&
                runtimeDisabledMetrics.historyToken == 0u &&
                !runtimeDisabledMetrics.errorOutputs.currentFrameRecorded &&
                !pass.HasAllocatedGpuResources(),
            "top-level disable suppresses metric/history resources even if stale request bits remain set");
        request = {};
        DiagnosticsGpuReadbackBlock ignored;
        const DiagnosticsReadbackPollResult disabledPoll =
            pass.PollReadback(request.settings, ignored);
        Check(disabledPoll.status == DiagnosticsPassStatus::Disabled &&
                !disabledPoll.completed &&
                !pass.HasAllocatedGpuResources(),
            "disabled poll performs no allocation or readback work");

        AdaptiveMsaaDiagnosticsPassPlanDesc noWork;
        noWork.settings.enabled = true;
        noWork.settings.passTimingsEnabled = true;
        const AdaptiveMsaaDiagnosticsPassPlan noWorkPlan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(noWork);
        Check(noWorkPlan.status == DiagnosticsPassStatus::NoWork &&
                !noWorkPlan.operations.allocate &&
                !noWorkPlan.operations.dispatch &&
                !noWorkPlan.operations.timerQuery,
            "timing alone has no pass to measure and stays zero-work");

        AdaptiveMsaaDiagnosticsPassPlanDesc timed =
            MakePlanDescription(4u);
        timed.settings.passTimingsEnabled = true;
        const AdaptiveMsaaDiagnosticsPassPlan timedPlan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(timed);
        Check(timedPlan.status == DiagnosticsPassStatus::Success &&
                timedPlan.operations.allocate &&
                timedPlan.operations.dispatch &&
                timedPlan.operations.timerQuery &&
                !timedPlan.operations.copy &&
                !timedPlan.operations.query &&
                !timedPlan.operations.map,
            "a real diagnostics dispatch opts into only the GPU timer query");

        AdaptiveMsaaDiagnosticsPassPlanDesc readbackGate;
        readbackGate.settings.enabled = true;
        readbackGate.settings.countersEnabled = true;
        const AdaptiveMsaaDiagnosticsPassPlan missingReadback =
            BuildAdaptiveMsaaDiagnosticsPassPlan(readbackGate);
        Check(missingReadback.status ==
                DiagnosticsPassStatus::ReadbackRequired &&
                !missingReadback.operations.allocate,
            "counter request without readback fails before resource inspection");
    }

    void TestVisibilitySampleValidation()
    {
        const HandlePackResult handle = PackGeometryInstanceHandle(7u, 1u);
        const GpuVisibilityKeyResult packed = MakeGpuVisibilityKey(
            handle.handle, 11u);
        Check(handle.Succeeded() && packed.Succeeded(),
            "diagnostics visibility test key must pack exactly");
        const GpuVisibilityKey key = packed.key;

        const DiagnosticsVisibilitySampleValidation background =
            ValidateDiagnosticsVisibilitySample(
                { ClearedVisibilityWord, ClearedVisibilityWord },
                0.f,
                true,
                ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND);
        Check(background.background && background.Valid() &&
                background.reason == FallbackReason::None,
            "exact clear key/class/+0 depth must be the only compact background");

        const DiagnosticsVisibilitySampleValidation reconstructed =
            ValidateDiagnosticsVisibilitySample(
                key,
                0.75f,
                true,
                ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT);
        const uint32_t fallbackClass =
            AdaptiveMsaaEncodeFallbackVisibilityClass(
                static_cast<uint32_t>(FallbackReason::UnsupportedMaterial),
                true);
        const DiagnosticsVisibilitySampleValidation fallback =
            ValidateDiagnosticsVisibilitySample(
                key, 0.f, true, fallbackClass);
        Check(!reconstructed.background && reconstructed.Valid() &&
                !fallback.background && fallback.Valid(),
            "reconstruct must accept positive depth and full-G-buffer fallback must accept covered far-plane zero");

        for (const uint32_t invalidClass : {
                 ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND,
                 2u,
                 ADAPTIVE_MSAA_VISIBILITY_CLASS_INVALID })
        {
            const DiagnosticsVisibilitySampleValidation invalid =
                ValidateDiagnosticsVisibilitySample(
                    key, 0.75f, true, invalidClass);
            Check(!invalid.background &&
                    invalid.reason == FallbackReason::ReconstructionFailure,
                "covered valid key must reject background or malformed visibility class");
        }

        for (const float invalidDepth : {
                 0.f,
                 std::numeric_limits<float>::quiet_NaN(),
                 std::numeric_limits<float>::infinity(),
                 1.01f })
        {
            const DiagnosticsVisibilitySampleValidation invalid =
                ValidateDiagnosticsVisibilitySample(
                    key,
                    invalidDepth,
                    true,
                    ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT);
            Check(!invalid.background &&
                    invalid.reason == FallbackReason::ReconstructionFailure,
                "reconstruct class must reject clear, non-finite, or out-of-range depth");
        }

        const DiagnosticsVisibilitySampleValidation absentClassValid =
            ValidateDiagnosticsVisibilitySample(key, 0.75f, false, 2u);
        const DiagnosticsVisibilitySampleValidation absentClassFarPlane =
            ValidateDiagnosticsVisibilitySample(key, 0.f, false, 2u);
        const DiagnosticsVisibilitySampleValidation absentClassBadDepth =
            ValidateDiagnosticsVisibilitySample(key, -0.01f, false, 2u);
        Check(absentClassValid.Valid() && absentClassFarPlane.Valid() &&
                absentClassBadDepth.reason ==
                    FallbackReason::ReconstructionFailure,
            "adaptive G-buffer diagnostics must accept keyed far-plane coverage but reject depth outside D32 range");
    }

    void TestSupportedCountsAndFootprints()
    {
        constexpr std::array<uint32_t, 4u> sampleCounts = { 2u, 4u, 8u, 16u };
        for (uint32_t sampleCount : sampleCounts)
        {
            AdaptiveMsaaDiagnosticsPassPlanDesc description =
                MakePlanDescription(
                    sampleCount,
                    AdaptiveMsaaDebugView::Disabled,
                    3840u,
                    2160u);
            description.settings.countersEnabled = true;
            description.settings.readbackEnabled = true;
            description.referencePerSample = true;
            const AdaptiveMsaaDiagnosticsPassPlan plan =
                BuildAdaptiveMsaaDiagnosticsPassPlan(description);
            const uint64_t pixels = 3840ull * 2160ull;
            Check(plan.status == DiagnosticsPassStatus::Success,
                "every supported sample count builds a counter plan");
            Check(plan.operations.allocate && plan.operations.dispatch &&
                    plan.operations.copy && plan.operations.query &&
                    !plan.operations.timerQuery &&
                    !plan.operations.map &&
                    !plan.operations.retainDualRenderTargets,
                "readback plan is asynchronous and does not retain dual targets");
            Check(plan.pixelCount == pixels &&
                    plan.frameSampleCount == pixels * sampleCount &&
                    plan.sampleStatusFootprintBytes ==
                        pixels * sampleCount *
                            DiagnosticsGpuSampleStatusBytesPerSample &&
                    plan.requiredSelectedSampleDataBytes ==
                        sampleCount *
                            sizeof(DiagnosticsGpuSampleInputRecord) &&
                    plan.requiredSelectedSampleDataBytes <=
                        MaximumAdaptiveSampleCount *
                            sizeof(DiagnosticsGpuSampleInputRecord) &&
                    plan.requiredPixelMetadataBytes ==
                        pixels *
                            sizeof(DiagnosticsGpuPixelMetadataRecord),
                "resource footprints are exact and selected data stays bounded");
            Check(plan.dispatchWidth == 480u && plan.dispatchHeight == 270u,
                "dispatch dimensions round to the fixed 8x8 group");
            Check(plan.counterValidityMask ==
                    (DiagnosticsCounterInvalidKeysValid |
                        DiagnosticsCounterGroupHistogramValid |
                        DiagnosticsCounterLightingValid |
                        DiagnosticsCounterFallbackReasonsValid),
                "explicit Reference mode exposes consumer-derived singleton "
                "group, lighting, fallback, and invalid-key counters");
        }

        AdaptiveMsaaDiagnosticsPassPlanDesc description4k16 =
            MakePlanDescription(
                16u,
                AdaptiveMsaaDebugView::SurfaceOwnershipKeys,
                3840u,
                2160u);
        description4k16.sampleStatus =
            MakeSampleStatus(3840u, 2160u, 16u);
        const AdaptiveMsaaDiagnosticsPassPlan plan4k16 =
            BuildAdaptiveMsaaDiagnosticsPassPlan(description4k16);
        Check(plan4k16.status == DiagnosticsPassStatus::Success &&
                plan4k16.sampleStatusFootprintBytes == 132710400ull &&
                plan4k16.requiredSelectedSampleDataBytes == 768u,
            "4K 16x status and selected-record footprints remain bounded");
    }

    void TestOptionalInputsAndViews()
    {
        AdaptiveMsaaDiagnosticsPassPlanDesc fallback = MakePlanDescription(
            8u,
            AdaptiveMsaaDebugView::FallbackReasons);
        fallback.visibilityClass = MakeVisibilityClass(
            1920u, 1080u, 8u);
        AdaptiveMsaaDiagnosticsPassPlan plan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(fallback);
        Check(plan.status == DiagnosticsPassStatus::MissingRequiredInput,
            "an adaptive fallback view cannot infer mode from an absent "
            "current-frame producer");

        AdaptiveMsaaDiagnosticsPassPlanDesc referenceFallback = fallback;
        referenceFallback.referencePerSample = true;
        plan = BuildAdaptiveMsaaDiagnosticsPassPlan(referenceFallback);
        Check(plan.status == DiagnosticsPassStatus::Success &&
                plan.viewAvailable,
            "explicit Reference mode derives key failures without an "
            "adaptive status producer");

        fallback.sampleStatus = MakeSampleStatus(1920u, 1080u, 8u);
        fallback.selectedSampleData = {
            true,
            8u * sizeof(DiagnosticsGpuSampleInputRecord),
            sizeof(DiagnosticsGpuSampleInputRecord)
        };
        fallback.pixelMetadata = {
            true,
            1920ull * 1080ull *
                sizeof(DiagnosticsGpuPixelMetadataRecord),
            sizeof(DiagnosticsGpuPixelMetadataRecord)
        };
        fallback.settings.countersEnabled = true;
        fallback.settings.readbackEnabled = true;
        plan = BuildAdaptiveMsaaDiagnosticsPassPlan(fallback);
        Check(plan.status == DiagnosticsPassStatus::MissingRequiredInput,
            "optional handles without current-frame capabilities cannot "
            "masquerade as an adaptive producer");

        constexpr uint32_t producerCapabilities =
            DiagnosticsCounterGroupHistogramValid |
            DiagnosticsCounterLightingValid |
            DiagnosticsCounterFallbackReasonsValid |
            DiagnosticsCounterStaleAndMappingValid;
        fallback.producerCapabilityMask = producerCapabilities;
        plan = BuildAdaptiveMsaaDiagnosticsPassPlan(fallback);
        Check(plan.status == DiagnosticsPassStatus::Success &&
                plan.viewAvailable &&
                (plan.inputAvailabilityMask &
                    DiagnosticsInputVisibilityClass) != 0u &&
                plan.counterValidityMask ==
                    (producerCapabilities |
                        DiagnosticsCounterInvalidKeysValid),
            "the consumer intersects the explicit current-frame capability "
            "subset with matching resources");
        Check((plan.counterValidityMask &
                    (DiagnosticsCounterWorkListValid |
                        DiagnosticsCounterErrorBudgetValid)) == 0u,
            "work-list remains unsupported and error-budget validity awaits an in-band exact GPU record");

        AdaptiveMsaaDiagnosticsPassPlanDesc lighting = MakePlanDescription(
            4u,
            AdaptiveMsaaDebugView::AdaptiveLightingError);
        lighting.settings.comparisonMode =
            DiagnosticsComparisonMode::ErrorHeatmap;
        lighting.referenceHdr = MakeSingleColor(1920u, 1080u);
        lighting.adaptiveHdr = MakeSingleColor(1920u, 1080u);
        plan = BuildAdaptiveMsaaDiagnosticsPassPlan(lighting);
        Check(plan.status == DiagnosticsPassStatus::Success &&
                !plan.viewAvailable && plan.inputAvailabilityMask == 0u,
            "dual targets supplied while dual rendering is off are not retained");
        lighting.settings.dualRenderingEnabled = true;
        plan = BuildAdaptiveMsaaDiagnosticsPassPlan(lighting);
        Check(plan.status == DiagnosticsPassStatus::Success &&
                plan.viewAvailable &&
                (plan.inputAvailabilityMask &
                    (DiagnosticsInputReferenceHdr |
                        DiagnosticsInputAdaptiveHdr)) ==
                    (DiagnosticsInputReferenceHdr |
                        DiagnosticsInputAdaptiveHdr) &&
                !plan.operations.retainDualRenderTargets,
            "explicit dual-render enable exposes lighting comparison inputs");

        AdaptiveMsaaDiagnosticsPassPlanDesc comparisonOnly =
            MakePlanDescription(
                4u,
                AdaptiveMsaaDebugView::Disabled);
        comparisonOnly.settings.comparisonMode =
            DiagnosticsComparisonMode::ErrorHeatmap;
        comparisonOnly.settings.dualRenderingEnabled = true;
        comparisonOnly.output = MakeOutput(1920u, 1080u);
        comparisonOnly.referenceHdr = MakeSingleColor(1920u, 1080u);
        comparisonOnly.adaptiveHdr = MakeSingleColor(1920u, 1080u);
        plan = BuildAdaptiveMsaaDiagnosticsPassPlan(comparisonOnly);
        const DiagnosticsGpuReadbackBlock comparisonOnlyInitial =
            MakeInitialDiagnosticsGpuReadbackBlock(
                plan,
                comparisonOnly);
        Check(plan.status == DiagnosticsPassStatus::Success &&
                plan.viewAvailable &&
                plan.operations.dispatch &&
                !plan.gpuErrorMetricsEnabled &&
                !plan.gpuErrorHistoryEnabled &&
                plan.shaderRequestedView ==
                    AdaptiveMsaaDebugView::AdaptiveLightingError &&
                comparisonOnlyInitial.selected.requestedView ==
                    static_cast<uint32_t>(
                        AdaptiveMsaaDebugView::AdaptiveLightingError),
            "comparison-only error output normalizes Disabled to the shader lighting view");

        AdaptiveMsaaDiagnosticsPassPlanDesc splitScreen =
            MakePlanDescription(4u, AdaptiveMsaaDebugView::Disabled);
        splitScreen.settings.comparisonMode =
            DiagnosticsComparisonMode::SplitScreen;
        splitScreen.settings.dualRenderingEnabled = true;
        splitScreen.output = MakeOutput(1920u, 1080u);
        splitScreen.referenceHdr = MakeSingleColor(1920u, 1080u);
        splitScreen.adaptiveHdr = MakeSingleColor(1920u, 1080u);
        splitScreen.visualizedErrorDomain =
            DiagnosticsGpuErrorDomain::FrameToFrameTemporal;
        plan = BuildAdaptiveMsaaDiagnosticsPassPlan(splitScreen);
        Check(plan.status == DiagnosticsPassStatus::Success &&
                plan.viewAvailable &&
                plan.operations.dispatch &&
                plan.shaderRequestedView ==
                    AdaptiveMsaaDebugView::AdaptiveLightingError &&
                !plan.gpuErrorMetricsEnabled &&
                !plan.gpuErrorHistoryEnabled,
            "split-screen comparison is a first-class visualization even "
            "under a temporal UI domain and without metrics or history");

        AdaptiveMsaaDiagnosticsPassPlanDesc reconstruction =
            MakePlanDescription(
                2u,
                AdaptiveMsaaDebugView::CompactReconstructionError);
        reconstruction.settings.comparisonMode =
            DiagnosticsComparisonMode::SplitScreen;
        reconstruction.settings.dualRenderingEnabled = true;
        reconstruction.referenceAttributes = MakeSingleColor(1920u, 1080u);
        reconstruction.reconstructedAttributes =
            MakeSingleColor(1920u, 1080u);
        plan = BuildAdaptiveMsaaDiagnosticsPassPlan(reconstruction);
        Check(plan.status == DiagnosticsPassStatus::Success &&
                !plan.viewAvailable &&
                !plan.completeReconstructionAvailable,
            "a legacy normals-only pair cannot masquerade as complete compact reconstruction");

        PopulateCompleteReconstruction(reconstruction);
        plan = BuildAdaptiveMsaaDiagnosticsPassPlan(reconstruction);
        Check(plan.status == DiagnosticsPassStatus::Success &&
                plan.viewAvailable &&
                plan.completeReconstructionAvailable &&
                (plan.inputAvailabilityMask &
                    DiagnosticsInputCompleteReconstruction) ==
                    DiagnosticsInputCompleteReconstruction,
            "all seven typed pairs expose the complete reconstruction comparison");
    }

    void TestProducerFrameState()
    {
        AdaptiveMsaaDiagnosticsProducerOutputs producer;
        Check(!producer.Requested() && !producer.producedThisFrame &&
                producer.producedCapabilityMask == 0u,
            "a default diagnostics producer is dormant and unpublished");

        producer.executionFlags =
            DiagnosticsProducerWriteSampleStatus |
            DiagnosticsProducerWritePixelMetadata;
        producer.capabilityMask =
            DiagnosticsCounterFallbackReasonsValid |
            DiagnosticsCounterGroupHistogramValid;
        producer.producedThisFrame = true;
        producer.producedCapabilityMask = producer.capabilityMask;
        Check(producer.Requested(),
            "nonzero producer execution flags request diagnostic writes");
        producer.ResetProducedState();
        Check(producer.Requested() && producer.executionFlags != 0u &&
                producer.capabilityMask != 0u &&
                !producer.producedThisFrame &&
                producer.producedCapabilityMask == 0u,
            "per-frame reset clears publication without discarding the "
            "prepared request or capability contract");
    }

    void TestCompatibilityAndCapacityFailures()
    {
        AdaptiveMsaaDiagnosticsPassPlanDesc description =
            MakePlanDescription(4u);
        description.depth.sampleCount = 2u;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::IncompatibleResources,
            "key/depth sample mismatch fails before dispatch");

        description = MakePlanDescription(1u);
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::UnsupportedSampleCount,
            "unsupported 1x permutation fails closed");

        description = MakePlanDescription(4u);
        description.visualizedSampleIndex = 4u;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::InvalidSampleIndex,
            "visualized sample is range checked");

        description = MakePlanDescription(
            4u,
            AdaptiveMsaaDebugView::Disabled);
        description.settings.selectedPixelCaptureRequested = true;
        description.settings.readbackEnabled = true;
        description.settings.selectedPixelX = description.surfaceKeys.width;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::InvalidSampleIndex,
            "selected pixel coordinates are range checked before dispatch");

        description = MakePlanDescription(4u);
        description.surfaceKeys.width = 0u;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::InvalidDimensions,
            "zero-sized required resources fail closed");

        description = MakePlanDescription(4u);
        description.output = {};
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::MissingRequiredInput,
            "requested visualization requires an output UAV");

        description = MakePlanDescription(4u);
        description.visibilityClass = MakeVisibilityClass(
            1920u, 1080u, 2u);
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::IncompatibleResources,
            "compact visibility class must match key sample count");
        description.visibilityClass = MakeVisibilityClass(
            1920u,
            1080u,
            4u,
            DiagnosticsGpuTextureFormat::R32Uint);
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::Success,
            "lossless R32_UINT visibility-class fallback is accepted");
        description.visibilityClass.format =
            DiagnosticsGpuTextureFormat::Rgba16Float;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::InvalidFormat,
            "visibility-class diagnostics reject non-integer formats");

        description = MakePlanDescription(
            4u,
            AdaptiveMsaaDebugView::FallbackReasons);
        description.sampleStatus = MakeSampleStatus(1920u, 1080u, 2u);
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::IncompatibleResources,
            "status attachment must match exact sample count");
        description.sampleStatus = MakeSampleStatus(1920u, 1080u, 4u);
        description.sampleStatus.format =
            DiagnosticsGpuTextureFormat::Rgba16Float;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::InvalidFormat,
            "status attachment must remain lossless packed R8_UINT");

        description = MakePlanDescription(16u);
        description.selectedSampleData = {
            true,
            15u * sizeof(DiagnosticsGpuSampleInputRecord),
            sizeof(DiagnosticsGpuSampleInputRecord)
        };
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::CapacityOverflow,
            "selected input requires one bounded record per sample");
        description.selectedSampleData.byteSize =
            16u * sizeof(DiagnosticsGpuSampleInputRecord);
        description.selectedSampleData.structStride = 16u;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::InvalidBufferStride,
            "selected input rejects a compact-shade-specific ABI");
        description.selectedSampleData.structStride =
            sizeof(DiagnosticsGpuSampleInputRecord);
        description.selectedSampleData.byteSize =
            17u * sizeof(DiagnosticsGpuSampleInputRecord);
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::CapacityOverflow,
            "selected input is hard-capped at sixteen records");

        description = MakePlanDescription(4u);
        description.pixelMetadata = {
            true,
            1920ull * 1080ull *
                sizeof(DiagnosticsGpuPixelMetadataRecord) - 1u,
            sizeof(DiagnosticsGpuPixelMetadataRecord)
        };
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::CapacityOverflow,
            "pixel metadata rejects undersized buffers");

        description = MakePlanDescription(4u);
        description.maximumWorkListCapacity = 0u;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::CapacityOverflow,
            "zero work-list capacity fails closed");

        constexpr uint32_t largeWidth = 2147483647u;
        description = MakePlanDescription(
            2u,
            AdaptiveMsaaDebugView::SurfaceOwnershipKeys,
            largeWidth,
            1u);
        description.maximumWorkListCapacity = 1u;
        AdaptiveMsaaDiagnosticsPassPlan largePlan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(description);
        Check(largePlan.status == DiagnosticsPassStatus::Success &&
                largePlan.sampleStatusFootprintBytes ==
                    DiagnosticsGpuMaximumBufferBytes - 1u,
            "status footprint accounting remains exact near the 32-bit domain");
        description.sampleStatus = MakeSampleStatus(largeWidth, 1u, 2u);
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::Success,
            "texture status capacity is validated by extent rather than a buffer cap");

        description.sampleStatus = {};
        description.pixelMetadata = {
            true,
            DiagnosticsGpuMaximumBufferBytes + 1u,
            sizeof(DiagnosticsGpuPixelMetadataRecord)
        };
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::CapacityOverflow,
            "metadata larger than the fixed 32-bit counter domain fails closed");

        description = MakePlanDescription(
            2u,
            AdaptiveMsaaDebugView::SurfaceOwnershipKeys,
            2147483648u,
            1u);
        description.maximumWorkListCapacity = 1u;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::CapacityOverflow,
            "frame sample totals outside the 32-bit counter ABI fail closed");

        description = MakePlanDescription(
            16u,
            AdaptiveMsaaDebugView::SurfaceOwnershipKeys,
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max());
        description.maximumWorkListCapacity = 1u;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(description).status ==
                DiagnosticsPassStatus::CapacityOverflow,
            "64-bit footprint multiplication overflow fails closed");
    }

    DiagnosticsGpuReadbackBlock MakeSelectedReadback()
    {
        AdaptiveMsaaDiagnosticsPassPlanDesc description =
            MakePlanDescription(
                4u,
                AdaptiveMsaaDebugView::Disabled,
                64u,
                32u);
        description.settings.selectedPixelCaptureRequested = true;
        description.settings.readbackEnabled = true;
        description.settings.selectedPixelX = 7u;
        description.settings.selectedPixelY = 9u;
        description.settings.selectedPixelCaptureId =
            0x1122334455667788ull;
        description.referencePerSample = true;
        description.rasterSurfaceHits =
            MakeRasterSurfaceHits(64u, 32u, 4u);
        description.frameIndex = 0x8877665544332211ull;
        const AdaptiveMsaaDiagnosticsPassPlan plan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(description);
        Check(plan.status == DiagnosticsPassStatus::Success,
            "selected-record fixture plan succeeds");
        return MakeInitialDiagnosticsGpuReadbackBlock(plan, description);
    }

    void TestCompleteReconstructionAndHistoryPlanning()
    {
        for (uint32_t sampleCount : { 2u, 4u, 8u, 16u })
        {
            AdaptiveMsaaDiagnosticsPassPlanDesc description =
                MakePlanDescription(
                    sampleCount,
                    AdaptiveMsaaDebugView::CompactReconstructionError);
            description.settings.comparisonMode =
                DiagnosticsComparisonMode::ErrorHeatmap;
            description.settings.dualRenderingEnabled = true;
            description.gpuErrorMetricsEnabled = true;
            PopulateCompleteReconstruction(description);

            AdaptiveMsaaDiagnosticsPassPlan plan =
                BuildAdaptiveMsaaDiagnosticsPassPlan(description);
            Check(plan.status == DiagnosticsPassStatus::Success &&
                    plan.operations.allocate && plan.operations.dispatch &&
                    plan.gpuErrorMetricsEnabled &&
                    !plan.gpuErrorHistoryEnabled &&
                    plan.completeReconstructionAvailable &&
                    plan.viewAvailable,
                "complete diagnostics-only reconstruction metrics plan every supported sample count");

            description.settings.debugView =
                AdaptiveMsaaDebugView::AdaptiveLightingError;
            description.referenceHdr = MakeSingleColor(1920u, 1080u);
            description.adaptiveHdr = MakeSingleColor(1920u, 1080u);
            description.gpuErrorHistoryEnabled = true;
            description.historyIdentity = { 11u, 29u };
            description.frameIndex = 100u + sampleCount;
            description.visualizedErrorDomain =
                DiagnosticsGpuErrorDomain::FrameToFrameTemporal;
            plan = BuildAdaptiveMsaaDiagnosticsPassPlan(description);
            Check(plan.status == DiagnosticsPassStatus::Success &&
                    plan.gpuErrorHistoryEnabled &&
                    plan.lightingComparisonAvailable &&
                    plan.viewAvailable &&
                    plan.historyIdentity.sceneEpoch == 11u &&
                    plan.historyIdentity.modeEpoch == 29u,
                "temporal HDR history is explicit and valid for every supported sample count");
        }

        AdaptiveMsaaDiagnosticsPassPlanDesc partial =
            MakePlanDescription(
                4u,
                AdaptiveMsaaDebugView::CompactReconstructionError);
        partial.settings.dualRenderingEnabled = true;
        partial.referenceDepth = MakeSingleTyped(
            1920u, 1080u, DiagnosticsGpuTextureFormat::R32Float);
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(partial).status ==
                DiagnosticsPassStatus::MissingRequiredInput,
            "one-sided reconstruction pairs fail closed");

        AdaptiveMsaaDiagnosticsPassPlanDesc metricsOnly =
            MakePlanDescription(
                4u,
                AdaptiveMsaaDebugView::Disabled);
        metricsOnly.settings.dualRenderingEnabled = true;
        metricsOnly.gpuErrorMetricsEnabled = true;
        PopulateCompleteReconstruction(metricsOnly);
        const AdaptiveMsaaDiagnosticsPassPlan metricsOnlyPlan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(metricsOnly);
        Check(metricsOnlyPlan.status == DiagnosticsPassStatus::Success &&
                metricsOnlyPlan.operations.dispatch &&
                !metricsOnlyPlan.viewAvailable &&
                !metricsOnly.output.supplied &&
                !metricsOnlyPlan.operations.copy &&
                !metricsOnlyPlan.operations.query,
            "GPU-resident metrics can run without a visualization UAV or CPU readback");

        PopulateCompleteReconstruction(partial);
        partial.reconstructedNormals.format =
            DiagnosticsGpuTextureFormat::Rgba16Snorm;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(partial).status ==
                DiagnosticsPassStatus::InvalidFormat,
            "typed reconstruction pairs reject a format-compatible-looking but lossy semantic mismatch");

        AdaptiveMsaaDiagnosticsPassPlanDesc history =
            MakePlanDescription(
                4u,
                AdaptiveMsaaDebugView::AdaptiveLightingError);
        history.settings.comparisonMode =
            DiagnosticsComparisonMode::ErrorHeatmap;
        history.settings.dualRenderingEnabled = true;
        history.referenceHdr = MakeSingleColor(1920u, 1080u);
        history.adaptiveHdr = MakeSingleColor(1920u, 1080u);
        history.gpuErrorHistoryEnabled = true;
        history.historyIdentity = { 1u, 1u };
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(history).status ==
                DiagnosticsPassStatus::InvalidSettings,
            "history cannot allocate or dispatch without its explicit metric gate");

        history.gpuErrorMetricsEnabled = true;
        history.historyIdentity = {};
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(history).status ==
                DiagnosticsPassStatus::InvalidHistoryIdentity,
            "zero scene or mode epochs fail closed before temporal history use");

        history.historyIdentity = { 1u, 1u };
        history.visualizedErrorDomain =
            DiagnosticsGpuErrorDomain::FrameToFrameTemporal;
        const AdaptiveMsaaDiagnosticsPassPlan writableHistoryPlan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(history);
        Check(writableHistoryPlan.status ==
                    DiagnosticsPassStatus::Success &&
                writableHistoryPlan.gpuErrorMetricsEnabled &&
                writableHistoryPlan.gpuErrorHistoryEnabled &&
                !writableHistoryPlan.frozenGpuErrorHistoryReadOnly &&
                writableHistoryPlan.viewAvailable,
            "ordinary temporal heatmaps preserve the writable submission-confirmed history path");
        history.frozenGpuErrorHistoryReadOnly = true;
        AdaptiveMsaaDiagnosticsPassPlan frozenPlan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(history);
        Check(frozenPlan.status == DiagnosticsPassStatus::Success &&
                frozenPlan.gpuErrorHistoryEnabled &&
                frozenPlan.frozenGpuErrorHistoryReadOnly &&
                frozenPlan.shaderRequestedView ==
                    AdaptiveMsaaDebugView::AdaptiveLightingError &&
                frozenPlan.viewAvailable,
            "temporal heatmap planning preserves explicit frozen read-only history");
        history.resetGpuErrorHistory = true;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(history).status ==
                DiagnosticsPassStatus::InvalidSettings,
            "frozen read-only history cannot also request a reset write");
        history.resetGpuErrorHistory = false;
        history.gpuErrorHistoryEnabled = false;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(history).status ==
                DiagnosticsPassStatus::InvalidSettings,
            "frozen read-only history requires the explicit history resource gate");
        history.frozenGpuErrorHistoryReadOnly = false;
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(history).status ==
                DiagnosticsPassStatus::InvalidSettings,
            "a temporal heatmap cannot silently degrade to current-frame error");
        history.visualizedErrorDomain =
            static_cast<DiagnosticsGpuErrorDomain>(99u);
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(history).status ==
                DiagnosticsPassStatus::InvalidSettings,
            "unknown GPU error domains fail closed");
        history.visualizedErrorDomain =
            DiagnosticsGpuErrorDomain::CurrentFrameSpatial;
        history.reconstructionChannel =
            static_cast<DiagnosticsReconstructionChannel>(99u);
        Check(BuildAdaptiveMsaaDiagnosticsPassPlan(history).status ==
                DiagnosticsPassStatus::InvalidSettings,
            "unknown reconstruction channels fail closed");

        AdaptiveMsaaDiagnosticsPassPlanDesc disabled;
        disabled.gpuErrorMetricsEnabled = true;
        disabled.gpuErrorHistoryEnabled = true;
        disabled.historyIdentity = { 1u, 1u };
        const AdaptiveMsaaDiagnosticsPassPlan disabledPlan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(disabled);
        Check(disabledPlan.status == DiagnosticsPassStatus::Disabled &&
                !disabledPlan.operations.allocate &&
                !disabledPlan.operations.dispatch &&
                !disabledPlan.operations.copy &&
                !disabledPlan.operations.query,
            "disabled diagnostics perform no metric/history allocation, dispatch, copy, or query planning");
    }

    void TestGpuHistoryLifecycle()
    {
        for (uint32_t sampleCount : { 2u, 4u, 8u, 16u })
        {
            DiagnosticsGpuHistoryState state;
            const DiagnosticsGpuHistoryKey key = {
                1920u, 1080u, sampleCount, 0u, 7u, 9u };
            DiagnosticsGpuHistoryTransition transition =
                DiagnosticsGpuHistoryTransition::Disabled;
            Check(EvaluateDiagnosticsGpuHistoryTransition(
                    true, false, key, 40u, state, transition) ==
                        DiagnosticsPassStatus::Success &&
                    transition ==
                        DiagnosticsGpuHistoryTransition::Initialize,
                "first history frame initializes for every supported sample count");

            state.valid = true;
            state.key = key;
            state.frameIndex = 40u;
            Check(ValidateDiagnosticsGpuFrozenHistoryRead(
                        key,
                        state) == DiagnosticsPassStatus::Success,
                "frozen history reuses exact submission-confirmed identity without frame advancement");
            DiagnosticsGpuHistoryState missingFrozenState;
            Check(ValidateDiagnosticsGpuFrozenHistoryRead(
                        key,
                        missingFrozenState) ==
                    DiagnosticsPassStatus::InvalidHistoryIdentity,
                "frozen history fails closed without a valid prior surface");
            Check(EvaluateDiagnosticsGpuHistoryTransition(
                    true, false, key, 41u, state, transition) ==
                        DiagnosticsPassStatus::Success &&
                    transition == DiagnosticsGpuHistoryTransition::Continue,
                "only an exact contiguous frame with identical identity continues history");

            DiagnosticsGpuHistoryKey changed = key;
            changed.width++;
            Check(ValidateDiagnosticsGpuFrozenHistoryRead(
                        changed,
                        state) ==
                    DiagnosticsPassStatus::InvalidHistoryIdentity,
                "frozen history rejects a resized identity instead of resetting or writing it");
            Check(EvaluateDiagnosticsGpuHistoryTransition(
                    true, false, changed, 41u, state, transition) ==
                        DiagnosticsPassStatus::Success &&
                    transition == DiagnosticsGpuHistoryTransition::Reset,
                "resize resets history");
            changed = key;
            changed.sampleCount = sampleCount == 16u ? 2u : sampleCount * 2u;
            Check(EvaluateDiagnosticsGpuHistoryTransition(
                    true, false, changed, 41u, state, transition) ==
                        DiagnosticsPassStatus::Success &&
                    transition == DiagnosticsGpuHistoryTransition::Reset,
                "sample-count changes reset history");
            changed = key;
            changed.modeEpoch++;
            Check(EvaluateDiagnosticsGpuHistoryTransition(
                    true, false, changed, 41u, state, transition) ==
                        DiagnosticsPassStatus::Success &&
                    transition == DiagnosticsGpuHistoryTransition::Reset,
                "implementation or permutation epoch changes reset history");
            changed = key;
            changed.sceneEpoch++;
            Check(EvaluateDiagnosticsGpuHistoryTransition(
                    true, false, changed, 41u, state, transition) ==
                        DiagnosticsPassStatus::Success &&
                    transition == DiagnosticsGpuHistoryTransition::Reset,
                "scene/topology/LOD epoch changes reset history");
            Check(EvaluateDiagnosticsGpuHistoryTransition(
                    true, true, key, 41u, state, transition) ==
                        DiagnosticsPassStatus::Success &&
                    transition == DiagnosticsGpuHistoryTransition::Reset,
                "an explicit reset invalidates otherwise compatible history");
            Check(EvaluateDiagnosticsGpuHistoryTransition(
                    true, false, key, 42u, state, transition) ==
                        DiagnosticsPassStatus::Success &&
                    transition == DiagnosticsGpuHistoryTransition::Reset,
                "a skipped diagnostics frame cannot be called frame-to-frame history");
            Check(EvaluateDiagnosticsGpuHistoryTransition(
                    true, false, key, 40u, state, transition) ==
                        DiagnosticsPassStatus::Success &&
                    transition == DiagnosticsGpuHistoryTransition::Reset,
                "a repeated or rewound frame index resets history");
        }

        DiagnosticsGpuHistoryTransition transition =
            DiagnosticsGpuHistoryTransition::Continue;
        DiagnosticsGpuHistoryKey invalid = {
            1920u, 1080u, 4u, 0u, 0u, 1u };
        Check(EvaluateDiagnosticsGpuHistoryTransition(
                true, false, invalid, 1u, {}, transition) ==
                    DiagnosticsPassStatus::InvalidHistoryIdentity &&
                transition == DiagnosticsGpuHistoryTransition::Disabled,
            "invalid history identity never inherits prior validity");
        Check(EvaluateDiagnosticsGpuHistoryTransition(
                false, false, invalid, 1u, {}, transition) ==
                    DiagnosticsPassStatus::Success &&
                transition == DiagnosticsGpuHistoryTransition::Disabled,
            "disabled history ignores identity and performs no transition");
    }

    void TestReadbackAbiAndSelectedRecords()
    {
        DiagnosticsGpuReadbackBlock block = MakeSelectedReadback();
        Check(block.counters.abiVersion ==
                    DiagnosticsGpuAbiInitializationSentinel &&
                block.counters.byteSize == 0u &&
                block.selected.abiVersion ==
                    DiagnosticsGpuAbiInitializationSentinel &&
                block.selected.byteSize == 0u &&
                block.selected.captureIdLow == 0x55667788u &&
                block.selected.captureIdHigh == 0x11223344u &&
                block.selected.frameIndexLow == 0x44332211u &&
                block.selected.frameIndexHigh == 0x88776655u &&
                block.selected.recordCount == 4u &&
                block.selectedSamples[0].deviceDepthBits ==
                    DiagnosticsGpuInvalidFloatBits,
            "initial readback uses the fixed split-word and invalid-field ABI");
        Check(ValidateDiagnosticsGpuReadbackBlock(block) ==
                DiagnosticsPassStatus::AbiMismatch,
            "CPU ABI initialization cannot masquerade as shader completion");
        block.counters.abiVersion = DiagnosticsGpuAbiVersion;
        block.counters.byteSize = sizeof(DiagnosticsGpuCounterBlock);
        block.selected.abiVersion = DiagnosticsGpuAbiVersion;
        block.selected.byteSize =
            sizeof(DiagnosticsGpuSelectedHeader) +
            sizeof(block.selectedSamples);
        block.selected.flags |= DiagnosticsSelectedHeaderDispatchComplete;

        const uint32_t handleA = (1u << GeometryInstanceSlotBits) | 3u;
        const uint32_t handleB = (1u << GeometryInstanceSlotBits) | 7u;
        const std::array<GpuVisibilityKey, 4u> keys = {
            GpuVisibilityKey{ handleA, 11u },
            GpuVisibilityKey{ handleA, 11u },
            GpuVisibilityKey{ handleB, 2u },
            GpuVisibilityKey{}
        };
        for (uint32_t sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex)
        {
            DiagnosticsGpuSelectedSampleRecord& record =
                block.selectedSamples[sampleIndex];
            record.geometryInstanceHandle =
                keys[sampleIndex].geometryInstanceHandle;
            record.geometryLocalPrimitiveIndex =
                keys[sampleIndex].geometryLocalPrimitiveIndex;
            record.deviceDepthBits = FloatBits(1.f + float(sampleIndex));
            record.validFields = DiagnosticsSelectedRecordKeySupplied |
                DiagnosticsSelectedRecordDepthValid |
                DiagnosticsSelectedRecordWeightValid;
            record.weightBits = FloatBits(0.25f);
            if (sampleIndex < 3u)
            {
                record.validFields |= DiagnosticsSelectedRecordCovered |
                    DiagnosticsSelectedRecordKeyValid |
                    DiagnosticsSelectedRecordGroupValid;
                record.groupIndex = sampleIndex < 2u ? 0u : 1u;
                record.representativeSampleIndex =
                    sampleIndex < 2u ? 0u : 2u;
                record.weightBits = FloatBits(
                    sampleIndex < 2u ? 0.5f : 0.25f);
            }
        }
        DiagnosticsGpuSelectedSampleRecord& first = block.selectedSamples[0];
        first.validFields |=
            DiagnosticsSelectedRecordSuppliedBarycentrics |
            DiagnosticsSelectedRecordReconstructedBarycentrics |
            DiagnosticsSelectedRecordFallbackReason |
            DiagnosticsSelectedRecordErrorBudgetReason |
            DiagnosticsSelectedRecordRadiance |
            DiagnosticsSelectedRecordFrontFaceValid |
            DiagnosticsSelectedRecordFrontFace;
        first.suppliedBarycentricXBits = FloatBits(0.2f);
        first.suppliedBarycentricYBits = FloatBits(0.3f);
        first.reconstructedBarycentricXBits = FloatBits(0.21f);
        first.reconstructedBarycentricYBits = FloatBits(0.29f);
        first.fallbackReason = static_cast<uint32_t>(FallbackReason::None);
        first.errorBudgetReason = static_cast<uint32_t>(
            DiagnosticsErrorBudgetRejectionReason::None);
        first.radianceRBits = FloatBits(1.f);
        first.radianceGBits = FloatBits(2.f);
        first.radianceBBits = FloatBits(3.f);

        Check(ValidateDiagnosticsGpuReadbackBlock(block) ==
                DiagnosticsPassStatus::Success,
            "producer final grouping and optional selected fields validate");

        DiagnosticsGpuReadbackBlock conservativelySplit = block;
        conservativelySplit.selectedSamples[0].weightBits = FloatBits(0.25f);
        conservativelySplit.selectedSamples[1].groupIndex = 1u;
        conservativelySplit.selectedSamples[1].representativeSampleIndex = 1u;
        conservativelySplit.selectedSamples[1].weightBits = FloatBits(0.25f);
        conservativelySplit.selectedSamples[2].groupIndex = 2u;
        conservativelySplit.selectedSamples[2].weightBits = FloatBits(0.25f);
        Check(ValidateDiagnosticsGpuReadbackBlock(conservativelySplit) ==
                DiagnosticsPassStatus::Success,
            "equal SurfaceKeys may split when CanShareShading is unsafe");
        Check((block.selectedSamples[3].validFields &
                    DiagnosticsSelectedRecordWeightValid) != 0u &&
                FloatFromBits(block.selectedSamples[3].weightBits) == 0.25f,
            "uncovered samples retain their explicit resolved-background weight");

        DiagnosticsGpuReadbackBlock coveredInvalid = block;
        coveredInvalid.selectedSamples[2].validFields &=
            ~DiagnosticsSelectedRecordKeyValid;
        coveredInvalid.selectedSamples[2].validFields |=
            DiagnosticsSelectedRecordFallbackReason;
        coveredInvalid.selectedSamples[2].geometryInstanceHandle = 7u;
        coveredInvalid.selectedSamples[2].fallbackReason =
            static_cast<uint32_t>(FallbackReason::InvalidHandleEncoding);
        Check(ValidateDiagnosticsGpuReadbackBlock(coveredInvalid) ==
                DiagnosticsPassStatus::Success,
            "covered invalid keys remain inspectable as fallback singletons");

        DiagnosticsGpuReadbackBlock coveredWithoutGroup = block;
        coveredWithoutGroup.selectedSamples[2].validFields &=
            ~DiagnosticsSelectedRecordGroupValid;
        coveredWithoutGroup.selectedSamples[2].validFields |=
            DiagnosticsSelectedRecordFallbackReason;
        coveredWithoutGroup.selectedSamples[2].groupIndex =
            DiagnosticsGpuInvalidWord;
        coveredWithoutGroup.selectedSamples[2].representativeSampleIndex =
            DiagnosticsGpuInvalidWord;
        coveredWithoutGroup.selectedSamples[2].fallbackReason =
            static_cast<uint32_t>(FallbackReason::ReconstructionFailure);
        Check(ValidateDiagnosticsGpuReadbackBlock(coveredWithoutGroup) ==
                DiagnosticsPassStatus::Success,
            "covered compact failures may omit a non-authoritative group");

        DiagnosticsGpuReadbackBlock counters = block;
        counters.counters.validityMask =
            DiagnosticsCounterLightingValid |
            DiagnosticsCounterFallbackReasonsValid |
            DiagnosticsCounterWorkListValid |
            DiagnosticsCounterErrorBudgetValid;
        counters.counters.flags |= DiagnosticsCounterDispatchComplete;
        counters.counters.referenceLightingEvaluations = 10u;
        counters.counters.performedLightingEvaluations = 4u;
        counters.counters.lightingEvaluationsSaved = 6u;
        counters.counters.fallbackReasonCounts[
            static_cast<size_t>(FallbackReason::UnsafeSharing)] = 3u;
        counters.counters.workListObservationCount = 2u;
        counters.counters.workListOccupancyTotal = 4u;
        counters.counters.workListCapacityTotal = 8u;
        counters.counters.workListPeakOccupancy = 3u;
        counters.counters.workListOverflowCount = 1u;
        counters.counters.errorBudgetRejectionReasonCounts[
            static_cast<size_t>(
                DiagnosticsErrorBudgetRejectionReason::HdrLimitExceeded)] =
            1u;
        counters.counters.errorBudgetRejectedGroupCount = 1u;
        counters.counters.errorBudgetRejectedSampleCount = 2u;
        Check(ValidateDiagnosticsGpuReadbackBlock(counters) ==
                DiagnosticsPassStatus::Success,
            "fixed optional counter families validate together");

        DiagnosticsGpuReadbackBlock textureStaleCounters = block;
        textureStaleCounters.counters.validityMask =
            DiagnosticsCounterFallbackReasonsValid |
            DiagnosticsCounterStaleAndMappingValid;
        textureStaleCounters.counters.flags |=
            DiagnosticsCounterDispatchComplete;
        textureStaleCounters.counters.fallbackReasonCounts[
            static_cast<size_t>(
                FallbackReason::TextureContentRevisionMismatch)] = 1u;
        textureStaleCounters.counters.fallbackReasonCounts[
            static_cast<size_t>(
                FallbackReason::TextureQualificationMismatch)] = 2u;
        textureStaleCounters.counters.staleKeyCount = 3u;
        Check(ValidateDiagnosticsGpuReadbackBlock(textureStaleCounters) ==
                DiagnosticsPassStatus::Success,
            "texture revision and qualification failures contribute to the exact stale-key total");
        textureStaleCounters.counters.staleKeyCount = 2u;
        Check(ValidateDiagnosticsGpuReadbackBlock(textureStaleCounters) ==
                DiagnosticsPassStatus::AbiMismatch,
            "texture stale bins cannot diverge from the aggregate stale counter");

        DiagnosticsGpuReadbackBlock brokenCounters = counters;
        brokenCounters.counters.lightingEvaluationsSaved = 5u;
        Check(ValidateDiagnosticsGpuReadbackBlock(brokenCounters) ==
                DiagnosticsPassStatus::AbiMismatch,
            "lighting evaluation accounting must remain internally exact");
        brokenCounters = counters;
        brokenCounters.counters.fallbackReasonCounts[0] = 1u;
        Check(ValidateDiagnosticsGpuReadbackBlock(brokenCounters) ==
                DiagnosticsPassStatus::AbiMismatch,
            "None is not counted as a fallback occurrence");
        brokenCounters = counters;
        brokenCounters.counters.errorBudgetRejectedGroupCount = 2u;
        Check(ValidateDiagnosticsGpuReadbackBlock(brokenCounters) ==
                DiagnosticsPassStatus::AbiMismatch,
            "error-budget group totals match their reason histogram");
        brokenCounters = counters;
        brokenCounters.counters.errorBudgetRejectedSampleCount = 0u;
        Check(ValidateDiagnosticsGpuReadbackBlock(brokenCounters) ==
                DiagnosticsPassStatus::AbiMismatch,
            "a rejected group must account for at least one rejected sample");
        brokenCounters = counters;
        brokenCounters.counters.errorBudgetRejectedSampleCount =
            brokenCounters.counters.pixelCount *
                brokenCounters.counters.sampleCount + 1u;
        Check(ValidateDiagnosticsGpuReadbackBlock(brokenCounters) ==
                DiagnosticsPassStatus::AbiMismatch,
            "error-budget rejected samples cannot exceed the exact frame population");
        brokenCounters = counters;
        brokenCounters.counters.errorBudgetRejectionReasonCounts[
            static_cast<size_t>(
                DiagnosticsErrorBudgetRejectionReason::None)] = 1u;
        Check(ValidateDiagnosticsGpuReadbackBlock(brokenCounters) ==
                DiagnosticsPassStatus::AbiMismatch,
            "the reserved None bin fails closed on malformed exact metadata");
        brokenCounters = block;
        brokenCounters.counters.staleKeyCount = 1u;
        Check(ValidateDiagnosticsGpuReadbackBlock(brokenCounters) ==
                DiagnosticsPassStatus::AbiMismatch,
            "unavailable stale and mapping counters remain physically zero");
        brokenCounters = block;
        brokenCounters.counters.workListPeakOccupancy = 1u;
        Check(ValidateDiagnosticsGpuReadbackBlock(brokenCounters) ==
                DiagnosticsPassStatus::AbiMismatch,
            "unavailable work-list counters remain physically zero");
        brokenCounters = block;
        brokenCounters.counters.errorBudgetRejectionReasonCounts[1] = 1u;
        Check(ValidateDiagnosticsGpuReadbackBlock(brokenCounters) ==
                DiagnosticsPassStatus::AbiMismatch,
            "unavailable error-budget counters remain physically zero");

        DiagnosticsGpuReadbackBlock broken = block;
        broken.selectedSamples[1].geometryLocalPrimitiveIndex = 12u;
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "one final diagnostic group cannot contain different SurfaceKeys");
        broken = block;
        broken.selectedSamples[1].representativeSampleIndex = 1u;
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "representative must be the deterministic lowest sample index");
        broken = block;
        broken.selectedSamples[0].weightBits = FloatBits(0.25f);
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "group weight must equal members divided by total samples");
        broken = block;
        broken.selectedSamples[2].groupIndex = 2u;
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "final diagnostic group identifiers must be contiguous");
        broken = block;
        broken.selectedSamples[3].validFields &=
            ~DiagnosticsSelectedRecordWeightValid;
        broken.selectedSamples[3].weightBits = DiagnosticsGpuInvalidFloatBits;
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "every inspected sample must publish an explicit resolve weight");
        broken = block;
        broken.selectedSamples[2].validFields &=
            ~DiagnosticsSelectedRecordGroupValid;
        broken.selectedSamples[2].groupIndex = DiagnosticsGpuInvalidWord;
        broken.selectedSamples[2].representativeSampleIndex =
            DiagnosticsGpuInvalidWord;
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "covered ungrouped samples require an explicit non-None fallback");
        broken = block;
        broken.selectedSamples[0].radianceRBits =
            DiagnosticsGpuInvalidFloatBits;
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "available selected fields cannot contain invalid sentinels");
        broken = block;
        broken.counters.abiVersion += 1u;
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "ABI version mismatch fails closed");
        broken = block;
        broken.counters.abiVersion =
            DiagnosticsGpuAbiInitializationSentinel;
        broken.counters.byteSize = 0u;
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "a stale shader that leaves the CPU completion sentinel fails closed");
        broken = block;
        broken.selected.abiVersion =
            DiagnosticsGpuAbiInitializationSentinel;
        broken.selected.byteSize = 0u;
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "a stale shader that leaves the selected-header completion sentinel fails closed");
        broken = block;
        broken.selectedSamples[2].geometryInstanceHandle = 7u;
        Check(ValidateDiagnosticsGpuReadbackBlock(broken) ==
                DiagnosticsPassStatus::AbiMismatch,
            "invalid generation encoding fails readback validation");
    }

    void TestReadbackRingState()
    {
        DiagnosticsReadbackRingState ring;
        const DiagnosticsReadbackTicket first = ring.Acquire();
        const DiagnosticsReadbackTicket second = ring.Acquire();
        const DiagnosticsReadbackTicket third = ring.Acquire();
        Check(first.Succeeded() && second.Succeeded() && third.Succeeded() &&
                first.token != second.token && second.token != third.token &&
                ring.OutstandingCount() == 3u,
            "fixed ring acquires exactly three unique recorded slots");
        Check(ring.Acquire().status == DiagnosticsPassStatus::ReadbackRingFull,
            "fourth readback is rejected without waiting");
        Check(ring.MarkSubmitted(first.token) == DiagnosticsPassStatus::Success &&
                ring.OldestSubmittedSlot() == first.slotIndex &&
                ring.SubmittedCount() == 1u,
            "recorded slot advances to submitted state");
        Check(ring.CancelRecorded(first.token) ==
                DiagnosticsPassStatus::InvalidReadbackState,
            "submitted slots cannot be cancelled as unsubmitted work");
        Check(ring.MarkCompleted(first.token) == DiagnosticsPassStatus::Success &&
                ring.OldestSubmittedSlot() == DiagnosticsGpuInvalidWord &&
                ring.OutstandingCount() == 2u,
            "completed nonblocking poll releases one slot");
        const DiagnosticsReadbackTicket reused = ring.Acquire();
        Check(reused.Succeeded() && reused.token != first.token &&
                ring.OutstandingCount() == 3u,
            "released ring storage is reused with a fresh token");
        Check(ring.CancelRecorded(second.token) == DiagnosticsPassStatus::Success &&
                ring.CancelRecorded(third.token) == DiagnosticsPassStatus::Success &&
                ring.CancelRecorded(reused.token) == DiagnosticsPassStatus::Success &&
                ring.OutstandingCount() == 0u,
            "unsubmitted cancellation releases only recorded slots");

        const DiagnosticsReadbackTicket orderedFirst = ring.Acquire();
        const DiagnosticsReadbackTicket orderedSecond = ring.Acquire();
        Check(ring.MarkSubmitted(orderedFirst.token) ==
                DiagnosticsPassStatus::Success &&
            ring.MarkSubmitted(orderedSecond.token) ==
                DiagnosticsPassStatus::Success &&
            ring.OldestSubmittedSlot() == orderedFirst.slotIndex,
            "polling selects the oldest live token rather than physical slot order");
        Check(ring.MarkCompleted(orderedFirst.token) ==
                DiagnosticsPassStatus::Success &&
            ring.OldestSubmittedSlot() == orderedSecond.slotIndex &&
            ring.MarkCompleted(orderedSecond.token) ==
                DiagnosticsPassStatus::Success,
            "ordered completion advances monotonically through live tokens");

        DiagnosticsReadbackRingState reuseOrder;
        const DiagnosticsReadbackTicket reuseFirst = reuseOrder.Acquire();
        const DiagnosticsReadbackTicket reuseSecond = reuseOrder.Acquire();
        const DiagnosticsReadbackTicket heldRecorded = reuseOrder.Acquire();
        Check(reuseOrder.MarkSubmitted(reuseFirst.token) ==
                DiagnosticsPassStatus::Success &&
            reuseOrder.MarkSubmitted(reuseSecond.token) ==
                DiagnosticsPassStatus::Success &&
            reuseOrder.MarkCompleted(reuseFirst.token) ==
                DiagnosticsPassStatus::Success,
            "reuse-order fixture retires its first submitted slot");
        const DiagnosticsReadbackTicket lowerPhysicalReuse =
            reuseOrder.Acquire();
        Check(lowerPhysicalReuse.Succeeded() &&
                lowerPhysicalReuse.slotIndex == reuseFirst.slotIndex &&
                reuseOrder.MarkSubmitted(lowerPhysicalReuse.token) ==
                    DiagnosticsPassStatus::Success &&
                reuseOrder.OldestSubmittedSlot() == reuseSecond.slotIndex,
            "new work in a reused lower physical slot cannot overtake older work");
        Check(reuseOrder.MarkCompleted(reuseSecond.token) ==
                DiagnosticsPassStatus::Success &&
            reuseOrder.OldestSubmittedSlot() ==
                lowerPhysicalReuse.slotIndex &&
            reuseOrder.MarkCompleted(lowerPhysicalReuse.token) ==
                DiagnosticsPassStatus::Success &&
            reuseOrder.CancelRecorded(heldRecorded.token) ==
                DiagnosticsPassStatus::Success,
            "FIFO order survives physical-slot reuse and drains exactly once");
        Check(ring.MarkSubmitted(0u) ==
                DiagnosticsPassStatus::InvalidReadbackToken,
            "zero token is never accepted");
    }

    std::filesystem::path FindSourceFile(const std::string& fileName)
    {
        const std::filesystem::path fromFile =
            std::filesystem::path(__FILE__).parent_path().parent_path() /
            "src" / fileName;
        if (std::filesystem::exists(fromFile))
            return fromFile;

        std::filesystem::path cursor = std::filesystem::current_path();
        for (uint32_t level = 0u; level < 8u; ++level)
        {
            const std::filesystem::path candidate =
                cursor / "src" / fileName;
            if (std::filesystem::exists(candidate))
                return candidate;
            if (!cursor.has_parent_path() ||
                cursor.parent_path() == cursor)
            {
                break;
            }
            cursor = cursor.parent_path();
        }
        return {};
    }

    std::string ReadSourceFile(const std::string& fileName)
    {
        const std::filesystem::path path = FindSourceFile(fileName);
        Check(!path.empty(), fileName + " is available for source audit");
        if (path.empty())
            return {};
        std::ifstream stream(path, std::ios::binary);
        std::ostringstream contents;
        contents << stream.rdbuf();
        return contents.str();
    }

    void TestPassTimerSourceContract()
    {
        const std::string source = ReadSourceFile(
            "adaptive_msaa_diagnostics_pass.cpp");
        if (source.empty())
            return;

        const size_t ensureDefinition = source.find(
            "AdaptiveMsaaDiagnosticsGpuPass::EnsureTimerResources()");
        const size_t createQuery = source.find(
            "createTimerQuery()", ensureDefinition);
        const size_t advanceDefinition = source.find(
            "AdaptiveMsaaDiagnosticsGpuPass::AdvanceTimerQueries()",
            ensureDefinition);
        Check(ensureDefinition != std::string::npos &&
                createQuery != std::string::npos &&
                advanceDefinition != std::string::npos &&
                ensureDefinition < createQuery &&
                createQuery < advanceDefinition,
            "timer queries are created only by the lazy timing-resource path");

        const size_t execute = source.find(
            "AdaptiveMsaaDiagnosticsGpuPass::Execute(");
        const size_t resourceGate = source.find(
            "if (plan.operations.timerQuery)", execute);
        const size_t ensureCall = source.find(
            "status = EnsureTimerResources();", resourceGate);
        const size_t pollCall = source.find(
            "AdvanceTimerQueries();", ensureCall);
        const size_t recordGate = source.find(
            "if (plan.operations.timerQuery)", pollCall);
        const size_t beginCall = source.find(
            "result.timerQueryRecorded = BeginTimerQuery(", recordGate);
        Check(execute != std::string::npos &&
                resourceGate != std::string::npos &&
                ensureCall != std::string::npos &&
                pollCall != std::string::npos &&
                recordGate != std::string::npos &&
                beginCall != std::string::npos &&
                execute < resourceGate && resourceGate < ensureCall &&
                ensureCall < pollCall && pollCall < recordGate &&
                recordGate < beginCall,
            "allocation, polling, and recording stay behind the explicit pass-timing gate");
    }

    void TestReconstructionAndHistorySourceContract()
    {
        const std::string shader = ReadSourceFile(
            "adaptive_msaa_diagnostics_cs.hlsl");
        const std::string pass = ReadSourceFile(
            "adaptive_msaa_diagnostics_pass.cpp");
        if (shader.empty() || pass.empty())
            return;

        Check(shader.find("t_ReferenceDepth") != std::string::npos &&
                shader.find("t_ReconstructedDiffuse") != std::string::npos &&
                shader.find("t_ReconstructedMaterial") != std::string::npos &&
                shader.find("t_ReconstructedNormals") != std::string::npos &&
                shader.find("t_ReconstructedEmissive") != std::string::npos &&
                shader.find("t_ReconstructedAmbientOcclusion") !=
                    std::string::npos &&
                shader.find("t_ReconstructedMotion") != std::string::npos &&
                shader.find("InputCompleteReconstruction") !=
                    std::string::npos &&
                shader.find("t_ReferenceAttributes[pixel]") ==
                    std::string::npos &&
                shader.find("t_ReconstructedAttributes[pixel]") ==
                    std::string::npos,
            "compact reconstruction diagnostics consume every required typed pair");
        Check(shader.find(
                    "return all(key == ClearedVisibilityWord);") !=
                    std::string::npos &&
                shader.find(
                    "return IsBackgroundKey(key) && isfinite(deviceDepth) &&\n"
                    "        asuint(deviceDepth) == 0u;") !=
                    std::string::npos &&
                shader.find(
                    "visibilityClass == ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND") !=
                    std::string::npos &&
                shader.find(
                    "(g_InputAvailabilityMask & InputVisibilityClass) != 0u") !=
                    std::string::npos &&
                shader.find(
                    "DecodeCoveredVisibilityClass(") !=
                    std::string::npos &&
                shader.find(
                    "deviceDepth <= 1.0f && (!requirePositive || deviceDepth > 0.0f);") !=
                    std::string::npos,
            "diagnostics require the exact compact background triad and valid class/depth for every covered sample");
        Check(shader.find("TemporalHdrError(") != std::string::npos &&
                shader.find("adaptiveCurrent - referenceCurrent") !=
                    std::string::npos &&
                shader.find("adaptivePrevious - referencePrevious") !=
                    std::string::npos &&
                shader.find("u_NextReferenceHdr[pixel]") !=
                    std::string::npos &&
                shader.find("ErrorLightingTemporalValid") !=
                    std::string::npos,
            "temporal diagnostics preserve signed residual history and explicit GPU validity");
        Check(shader.find("const bool historyReadRequested") !=
                    std::string::npos &&
                shader.find("const bool historyWriteRequested") !=
                    std::string::npos &&
                shader.find("if (historyReadRequested)") !=
                    std::string::npos &&
                shader.find("if (historyWriteRequested)") !=
                    std::string::npos,
            "shader history reads remain independent from ping-pong writes for frozen diagnostics");
        const size_t lightingErrorView = shader.find(
            "else if (g_RequestedView == DebugViewAdaptiveLightingError)");
        const size_t splitScreenBranch = shader.find(
            "if (g_ComparisonMode == ComparisonSplitScreen)",
            lightingErrorView);
        const size_t temporalValiditySelection = shader.find(
            "const uint requestedValidity = g_ErrorDomain ==",
            splitScreenBranch);
        Check(lightingErrorView != std::string::npos &&
                splitScreenBranch != std::string::npos &&
                temporalValiditySelection != std::string::npos &&
                lightingErrorView < splitScreenBranch &&
                splitScreenBranch < temporalValiditySelection,
            "split-screen comparison is selected before the temporal heatmap validity domain");

        const size_t disabledGate = pass.find(
            "preliminary.status == DiagnosticsPassStatus::Disabled");
        const size_t historyAllocation = pass.find(
            "EnsureErrorMetricResources(", disabledGate);
        const size_t historyPending = pass.find(
            "m_PendingHistoryWrite.valid = true;", historyAllocation);
        Check(disabledGate != std::string::npos &&
                historyAllocation != std::string::npos &&
                historyPending != std::string::npos &&
                disabledGate < historyAllocation &&
                historyAllocation < historyPending,
            "disabled execution returns before metric allocation and submission-owned history publication");
        Check(pass.find("ConfirmErrorHistorySubmission(") !=
                    std::string::npos &&
                pass.find("CancelErrorHistorySubmission(") !=
                    std::string::npos &&
                pass.find("frameIndex != currentState.frameIndex + 1u") !=
                    std::string::npos,
            "recorded history requires submission confirmation and exact contiguous frame identity");
        const size_t constantsRequestedView = pass.find(
            "constants.requestedView =");
        const size_t normalizedView = pass.find(
            "static_cast<uint32_t>(plan.shaderRequestedView)",
            constantsRequestedView);
        const size_t frozenValidation = pass.find(
            "ValidateDiagnosticsGpuFrozenHistoryRead(",
            disabledGate);
        const size_t writeFlagGate = pass.find(
            "if (plan.gpuErrorHistoryEnabled &&\n"
            "            !plan.frozenGpuErrorHistoryReadOnly)",
            frozenValidation);
        const size_t historyWriteFlag = pass.find(
            "constants.errorExecutionFlags |= ErrorExecuteHistoryWrite;",
            writeFlagGate);
        const size_t pendingWrite = pass.find(
            "m_PendingHistoryWrite.valid = true;",
            historyWriteFlag);
        Check(constantsRequestedView != std::string::npos &&
                normalizedView != std::string::npos &&
                constantsRequestedView < normalizedView &&
                frozenValidation != std::string::npos &&
                writeFlagGate != std::string::npos &&
                historyWriteFlag != std::string::npos &&
                pendingWrite != std::string::npos &&
                frozenValidation < writeFlagGate &&
                writeFlagGate < historyWriteFlag &&
                historyWriteFlag < pendingWrite,
            "comparison-only constants use the normalized view, ordinary temporal heatmaps write, and frozen reads cannot publish a history token");
    }

    void TestAutomaticGroupGateSourceContract()
    {
        const std::string lighting = ReadSourceFile(
            "pbr_deferred_lighting_adaptive_msaa_cs.hlsl");
        const std::string producer = ReadSourceFile(
            "adaptive_msaa_diagnostics_producer.hlsli");
        const std::string consumer = ReadSourceFile(
            "adaptive_msaa_diagnostics_cs.hlsl");
        if (lighting.empty() || producer.empty() || consumer.empty())
            return;

        const size_t defaultDisabled = lighting.find(
            "#define UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER 0");
        const size_t producerInclude = lighting.find(
            "#include \"adaptive_msaa_diagnostics_producer.hlsli\"");
        const size_t includeGuard = lighting.rfind(
            "#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER",
            producerInclude);
        Check(defaultDisabled != std::string::npos &&
                producerInclude != std::string::npos &&
                includeGuard != std::string::npos &&
                includeGuard < producerInclude,
            "accepted permutations default off and do not include diagnostic resources");

        const size_t gateMarker = lighting.find(
            "The diagnostic permutation evaluates the complete");
        const size_t gateOpen = lighting.rfind(
            "#if UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER",
            gateMarker);
        const size_t gateClose = lighting.find("#endif", gateMarker);
        const size_t acceptedSharedWrite = lighting.find(
            "finalLinearHdr += representativeRadiance * groupWeight;",
            gateMarker);
        const size_t perSampleReferenceFallback = lighting.find(
            "referenceRadiance * inverseSampleCount;",
            gateMarker);
        const size_t singletonFinalGroup = lighting.find(
            "diagnosticsFinalGroupCount++;",
            perSampleReferenceFallback);
        Check(gateMarker != std::string::npos &&
                gateOpen != std::string::npos &&
                gateClose != std::string::npos &&
                acceptedSharedWrite != std::string::npos &&
                gateOpen < gateMarker &&
                gateMarker < perSampleReferenceFallback &&
                perSampleReferenceFallback < singletonFinalGroup &&
                singletonFinalGroup < gateClose &&
                gateClose < acceptedSharedWrite,
            "complete Reference probes and whole-group fallback are compile-time isolated from accepted shared shading");
        const size_t hdrMetric = lighting.find(
            "DiagnosticsProducerHdrSpatialError(", gateMarker);
        const size_t strictLimit = lighting.find(
            "DiagnosticsHdrGroupLimit", hdrMetric);
        const size_t memberReasonStore = lighting.find(
            "diagnosticsErrorBudgetReasons[sampleIndex] =",
            gateMarker);
        const size_t memberReasonValue = lighting.find(
            "diagnosticsGroupErrorReason;", memberReasonStore);
        Check(hdrMetric != std::string::npos &&
                strictLimit != std::string::npos &&
                memberReasonStore != std::string::npos &&
                memberReasonValue != std::string::npos &&
                hdrMetric < strictLimit && strictLimit < gateClose &&
                memberReasonStore < memberReasonValue &&
                memberReasonValue < gateClose,
            "the automatic gate uses strict immutable HDR comparison and publishes one exact reason on every rejected member");
        Check(producer.find("DiagnosticsPixelErrorBudgetValid") !=
                    std::string::npos &&
                producer.find("errorBudgetNonFiniteGroupCount") !=
                    std::string::npos &&
                producer.find("errorBudgetHdrGroupCount") !=
                    std::string::npos &&
                producer.find("errorBudgetRejectedSampleCount") !=
                    std::string::npos &&
                consumer.find("exactErrorBudgetMetadataAvailable") !=
                    std::string::npos &&
                consumer.find("CounterValidityOffset") !=
                    std::string::npos &&
                consumer.find(
                    "producerMetadata.errorBudgetRejectedSampleCount") !=
                    std::string::npos &&
                consumer.find(
                    "rejectionReason = ErrorBudgetReasonNone") ==
                    std::string::npos &&
                consumer.find("uint2 groupKeys[") ==
                    std::string::npos,
            "exact producer metadata supersedes ownership-key inference for rejected-group counters");
    }
}

int main()
{
    TestVisibilitySampleValidation();
    TestDisabledAndNoWorkGates();
    TestSupportedCountsAndFootprints();
    TestOptionalInputsAndViews();
    TestProducerFrameState();
    TestCompatibilityAndCapacityFailures();
    TestCompleteReconstructionAndHistoryPlanning();
    TestGpuHistoryLifecycle();
    TestReadbackAbiAndSelectedRecords();
    TestReadbackRingState();
    TestPassTimerSourceContract();
    TestReconstructionAndHistorySourceContract();
    TestAutomaticGroupGateSourceContract();

    if (!g_Passed)
        return EXIT_FAILURE;
    std::cout << "Adaptive MSAA diagnostics pass validation passed.\n";
    return EXIT_SUCCESS;
}
