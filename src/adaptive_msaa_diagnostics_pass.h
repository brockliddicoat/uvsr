#pragma once

#include "adaptive_msaa_diagnostics.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>

namespace donut::engine
{
    class ShaderFactory;
}

namespace uvsr::adaptive_msaa
{
    inline constexpr uint32_t DiagnosticsGpuAbiVersion = 6u;
    // CPU initialization deliberately uses a value that no shader ABI may
    // claim. The diagnostics shader overwrites both ABI headers only after it
    // has executed the current layout, so stale DXIL cannot inherit a trusted
    // version/size stamp from the upload buffer.
    inline constexpr uint32_t DiagnosticsGpuAbiInitializationSentinel =
        0xffffffffu;
    inline constexpr uint32_t DiagnosticsGpuThreadGroupWidth = 8u;
    inline constexpr uint32_t DiagnosticsGpuThreadGroupHeight = 8u;
    inline constexpr uint32_t DiagnosticsReadbackRingCapacity = 3u;
    inline constexpr uint32_t DiagnosticsGpuTimerRingCapacity = 4u;
    inline constexpr uint32_t DiagnosticsGpuInvalidWord = 0xffffffffu;
    inline constexpr uint32_t DiagnosticsGpuInvalidFloatBits = 0x7fc00000u;
    inline constexpr uint32_t DiagnosticsGpuUnavailableFallbackReason = 0x1fu;
    inline constexpr uint32_t
        DiagnosticsGpuUnavailableErrorBudgetReason = 0x7u;
    inline constexpr uint32_t DiagnosticsGpuSampleStatusFallbackMask = 0x1fu;
    inline constexpr uint32_t DiagnosticsGpuSampleStatusErrorShift = 5u;
    inline constexpr uint32_t DiagnosticsGpuSampleStatusBytesPerSample = 1u;
    static_assert(
        static_cast<uint32_t>(FallbackReason::Count) <
        DiagnosticsGpuUnavailableFallbackReason);
    static_assert(
        static_cast<uint32_t>(
            DiagnosticsErrorBudgetRejectionReason::Count) <
        DiagnosticsGpuUnavailableErrorBudgetReason);

    [[nodiscard]] inline constexpr bool
        IsValidDiagnosticsGpuSampleStatusReasons(
            uint32_t fallbackReason,
            uint32_t errorBudgetReason)
    {
        return (fallbackReason < static_cast<uint32_t>(
                    FallbackReason::Count) ||
                fallbackReason == DiagnosticsGpuUnavailableFallbackReason) &&
            (errorBudgetReason < static_cast<uint32_t>(
                    DiagnosticsErrorBudgetRejectionReason::Count) ||
                errorBudgetReason ==
                    DiagnosticsGpuUnavailableErrorBudgetReason);
    }

    // Invalid or overflowing enum inputs fail closed to fully unavailable,
    // never to a different valid reason.
    [[nodiscard]] inline constexpr uint32_t PackDiagnosticsGpuSampleStatus(
        uint32_t fallbackReason,
        uint32_t errorBudgetReason)
    {
        if (!IsValidDiagnosticsGpuSampleStatusReasons(
                fallbackReason,
                errorBudgetReason))
        {
            fallbackReason = DiagnosticsGpuUnavailableFallbackReason;
            errorBudgetReason =
                DiagnosticsGpuUnavailableErrorBudgetReason;
        }
        return (fallbackReason & DiagnosticsGpuSampleStatusFallbackMask) |
            ((errorBudgetReason &
                DiagnosticsGpuUnavailableErrorBudgetReason) <<
                DiagnosticsGpuSampleStatusErrorShift);
    }

    [[nodiscard]] inline constexpr uint32_t
        UnpackDiagnosticsGpuFallbackReason(uint32_t packedStatus)
    {
        return packedStatus & DiagnosticsGpuSampleStatusFallbackMask;
    }

    [[nodiscard]] inline constexpr uint32_t
        UnpackDiagnosticsGpuErrorBudgetReason(uint32_t packedStatus)
    {
        return (packedStatus >> DiagnosticsGpuSampleStatusErrorShift) &
            DiagnosticsGpuUnavailableErrorBudgetReason;
    }
    inline constexpr uint64_t DiagnosticsGpuMaximumBufferBytes =
        uint64_t(std::numeric_limits<uint32_t>::max());

    enum class DiagnosticsPassStatus : uint32_t
    {
        Success = 0u,
        Disabled = 1u,
        NoWork = 2u,
        InvalidSettings = 3u,
        UnsupportedSampleCount = 4u,
        MissingRequiredInput = 5u,
        InvalidDimensions = 6u,
        InvalidFormat = 7u,
        IncompatibleResources = 8u,
        InvalidSampleIndex = 9u,
        CapacityOverflow = 10u,
        InvalidBufferStride = 11u,
        ReadbackRequired = 12u,
        ResourceCreationFailed = 13u,
        PipelineUnavailable = 14u,
        ReadbackRingFull = 15u,
        InvalidReadbackToken = 16u,
        InvalidReadbackState = 17u,
        ReadbackNotReady = 18u,
        ReadbackEmpty = 19u,
        ReadbackMapFailed = 20u,
        AbiMismatch = 21u,
        InvalidHistoryIdentity = 22u,
        HistorySubmissionPending = 23u,
        InvalidHistoryToken = 24u,
        Count = 25u
    };

    struct DiagnosticsVisibilitySampleValidation
    {
        bool background = false;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Valid() const
        {
            return background || reason == FallbackReason::None;
        }
    };

    // Mirrors the diagnostics shader's fail-closed ownership/class/depth
    // contract so injected CPU tests can lock every malformed combination.
    [[nodiscard]] DiagnosticsVisibilitySampleValidation
        ValidateDiagnosticsVisibilitySample(
            const GpuVisibilityKey& key,
            float deviceDepth,
            bool visibilityClassAvailable,
            uint32_t visibilityClass);

    enum class DiagnosticsGpuTextureFormat : uint32_t
    {
        Unknown = 0u,
        Rg32Uint = 1u,
        Rgba16Float = 2u,
        Depth = 3u,
        R8Uint = 4u,
        Rgba32Float = 5u,
        Rgba8Unorm = 6u,
        Srgba8Unorm = 7u,
        Rgba16Snorm = 8u,
        R8Unorm = 9u,
        Rg16Float = 10u,
        R32Float = 11u,
        R16Float = 12u,
        R32Uint = 13u
    };

    enum class DiagnosticsGpuTextureDimension : uint32_t
    {
        Unknown = 0u,
        Texture2D = 1u,
        Texture2DMS = 2u,
        Texture2DArray = 3u
    };

    struct DiagnosticsGpuTextureDesc
    {
        bool supplied = false;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t depth = 0u;
        uint32_t arraySize = 0u;
        uint32_t mipLevels = 0u;
        uint32_t sampleCount = 0u;
        uint32_t sampleQuality = 0u;
        DiagnosticsGpuTextureFormat format =
            DiagnosticsGpuTextureFormat::Unknown;
        DiagnosticsGpuTextureDimension dimension =
            DiagnosticsGpuTextureDimension::Unknown;
        bool shaderResource = false;
        bool unorderedAccess = false;
    };

    struct DiagnosticsGpuBufferDesc
    {
        bool supplied = false;
        uint64_t byteSize = 0u;
        uint32_t structStride = 0u;
    };

    inline constexpr uint32_t DiagnosticsSampleInputSuppliedBarycentrics =
        1u << 0u;
    inline constexpr uint32_t
        DiagnosticsSampleInputReconstructedBarycentrics = 1u << 1u;
    inline constexpr uint32_t DiagnosticsSampleInputRadiance = 1u << 2u;
    inline constexpr uint32_t DiagnosticsSampleInputFallbackReason =
        1u << 3u;
    inline constexpr uint32_t DiagnosticsSampleInputErrorBudgetReason =
        1u << 4u;
    inline constexpr uint32_t DiagnosticsSampleInputFrontFaceValid =
        1u << 5u;
    inline constexpr uint32_t DiagnosticsSampleInputFrontFace = 1u << 6u;
    inline constexpr uint32_t DiagnosticsSampleInputShadingGroup = 1u << 7u;

    // Generic selected-pixel producer ABI. It deliberately has no dependency
    // on compact shading records: any reference, adaptive G-buffer, compact,
    // or future DXR producer may publish at most sampleCount (and therefore at
    // most 16) records for the explicitly requested pixel.
    struct DiagnosticsGpuSampleInputRecord
    {
        uint32_t validFields = 0u;
        float suppliedBarycentricX = 0.f;
        float suppliedBarycentricY = 0.f;
        float reconstructedBarycentricX = 0.f;
        float reconstructedBarycentricY = 0.f;
        float radianceR = 0.f;
        float radianceG = 0.f;
        float radianceB = 0.f;
        uint32_t fallbackReason = DiagnosticsGpuInvalidWord;
        uint32_t errorBudgetReason = DiagnosticsGpuInvalidWord;
        uint32_t shadingGroupIndex = DiagnosticsGpuInvalidWord;
        uint32_t representativeSampleIndex = DiagnosticsGpuInvalidWord;
    };

    inline constexpr uint32_t DiagnosticsPixelMetadataLightingValid =
        1u << 0u;
    inline constexpr uint32_t DiagnosticsPixelMetadataWorkListValid =
        1u << 1u;
    inline constexpr uint32_t DiagnosticsPixelMetadataWorkListOverflow =
        1u << 2u;
    inline constexpr uint32_t DiagnosticsPixelMetadataGroupCountShift = 3u;
    inline constexpr uint32_t DiagnosticsPixelMetadataGroupCountMask =
        0x1fu << DiagnosticsPixelMetadataGroupCountShift;
    inline constexpr uint32_t
        DiagnosticsPixelMetadataRepresentativeMaskShift = 8u;
    inline constexpr uint32_t
        DiagnosticsPixelMetadataRepresentativeMaskMask =
            0xffffu << DiagnosticsPixelMetadataRepresentativeMaskShift;
    inline constexpr uint32_t DiagnosticsPixelMetadataGroupDataValid =
        1u << 24u;
    inline constexpr uint32_t DiagnosticsPixelMetadataErrorBudgetValid =
        1u << 25u;

    [[nodiscard]] inline constexpr uint32_t
        PackDiagnosticsPixelMetadataGroupData(
            uint32_t groupCount,
            uint32_t representativeSampleMask)
    {
        return groupCount <= MaximumAdaptiveSampleCount &&
                (representativeSampleMask & ~0xffffu) == 0u
            ? DiagnosticsPixelMetadataGroupDataValid |
                (groupCount << DiagnosticsPixelMetadataGroupCountShift) |
                (representativeSampleMask <<
                    DiagnosticsPixelMetadataRepresentativeMaskShift)
            : 0u;
    }

    [[nodiscard]] inline constexpr uint32_t
        UnpackDiagnosticsPixelMetadataGroupCount(uint32_t validFields)
    {
        return (validFields & DiagnosticsPixelMetadataGroupCountMask) >>
            DiagnosticsPixelMetadataGroupCountShift;
    }

    [[nodiscard]] inline constexpr uint32_t
        UnpackDiagnosticsPixelMetadataRepresentativeMask(
            uint32_t validFields)
    {
        return (validFields &
                DiagnosticsPixelMetadataRepresentativeMaskMask) >>
            DiagnosticsPixelMetadataRepresentativeMaskShift;
    }

    // Eight exact 32-bit words. Error-budget fields describe rejected final
    // sharing groups directly; they are not reconstructed from ownership-key
    // groups. A producer sets DiagnosticsPixelMetadataErrorBudgetValid even
    // when every count is zero, distinguishing observed success from absence.
    struct DiagnosticsGpuPixelMetadataRecord
    {
        uint32_t validFields = 0u;
        uint32_t performedLightingEvaluations = 0u;
        uint32_t workListOccupancy = 0u;
        uint32_t workListCapacity = 0u;
        uint32_t errorBudgetNonFiniteGroupCount = 0u;
        uint32_t errorBudgetHdrGroupCount = 0u;
        uint32_t errorBudgetDisplayGroupCount = 0u;
        uint32_t errorBudgetRejectedSampleCount = 0u;
    };

    inline constexpr uint32_t DiagnosticsProducerWriteSampleStatus = 1u << 0u;
    inline constexpr uint32_t DiagnosticsProducerWritePixelMetadata =
        1u << 1u;
    inline constexpr uint32_t DiagnosticsProducerWriteSelectedSamples =
        1u << 2u;
    inline constexpr uint32_t DiagnosticsProducerValidateErrorBudget =
        1u << 3u;

    struct DiagnosticsProducerGpuConstants
    {
        uint32_t executionFlags = 0u;
        uint32_t selectedPixelX = 0u;
        uint32_t selectedPixelY = 0u;
        uint32_t producerCapabilityMask = 0u;
        uint32_t viewportOriginX = 0u;
        uint32_t viewportOriginY = 0u;
        uint32_t viewportWidth = 0u;
        uint32_t viewportHeight = 0u;
    };
    static_assert(sizeof(DiagnosticsProducerGpuConstants) == 32u);

    // The renderer owns these lazily allocated diagnostic-only resources.
    // Diagnostic shader permutations receive all three bindings; an execution
    // bit is set only when the corresponding handle is a full-capacity output.
    // producedThisFrame and producedCapabilityMask are reset before each draw
    // and written only after a diagnostic adaptive permutation is dispatched.
    struct AdaptiveMsaaDiagnosticsProducerOutputs
    {
        nvrhi::ITexture* sampleStatus = nullptr;
        nvrhi::IBuffer* selectedSampleData = nullptr;
        nvrhi::IBuffer* pixelMetadata = nullptr;
        nvrhi::ITexture* multisampledBackground = nullptr;
        // Adaptive G-buffer diagnostics optionally provide raster-authoritative
        // perspective barycentrics and front-face state. Compact visibility
        // reconstructs the same SurfaceHit internally and leaves this null.
        nvrhi::ITexture* multisampledSurfaceHits = nullptr;
        uint32_t executionFlags = 0u;
        uint32_t capabilityMask = 0u;
        uint32_t selectedPixelX = 0u;
        uint32_t selectedPixelY = 0u;
        bool producedThisFrame = false;
        uint32_t producedCapabilityMask = 0u;

        void ResetProducedState()
        {
            producedThisFrame = false;
            producedCapabilityMask = 0u;
        }

        [[nodiscard]] bool Requested() const
        {
            return executionFlags != 0u;
        }
    };

    // The optional R8_UINT Texture2DArray status attachment is intentionally
    // compact and writable by the compute shading producers that discover
    // fallback and error decisions after rasterization. Array slice is sample
    // index. Bits 0..4 hold FallbackReason (0x1f unavailable); bits 5..7 hold
    // DiagnosticsErrorBudgetRejectionReason (0x7 unavailable). Static range
    // proofs above make this one-byte encoding lossless. The attachment must
    // match the exact key/depth extent and have exactly key.sampleCount array
    // slices; it is otherwise single-sampled with one mip. A
    // rejected group publishes the same non-None error reason on every member.
    // Exact rejected-group and rejected-sample totals come from the extended
    // pixel metadata record; the consumer never reconstructs final shading
    // groups from ownership keys.

    inline constexpr uint32_t DiagnosticsCounterGroupHistogramValid =
        1u << 0u;
    inline constexpr uint32_t DiagnosticsCounterLightingValid = 1u << 1u;
    inline constexpr uint32_t DiagnosticsCounterFallbackReasonsValid =
        1u << 2u;
    inline constexpr uint32_t DiagnosticsCounterInvalidKeysValid = 1u << 3u;
    inline constexpr uint32_t DiagnosticsCounterStaleAndMappingValid =
        1u << 4u;
    inline constexpr uint32_t DiagnosticsCounterWorkListValid = 1u << 5u;
    inline constexpr uint32_t DiagnosticsCounterErrorBudgetValid = 1u << 6u;
    inline constexpr uint32_t DiagnosticsCounterViewAvailable = 1u << 0u;
    inline constexpr uint32_t DiagnosticsCounterDispatchComplete = 1u << 1u;
    inline constexpr uint32_t DiagnosticsCounterAllFlagBits =
        DiagnosticsCounterViewAvailable |
        DiagnosticsCounterDispatchComplete;
    inline constexpr uint32_t DiagnosticsCounterAllValidityBits =
        DiagnosticsCounterGroupHistogramValid |
        DiagnosticsCounterLightingValid |
        DiagnosticsCounterFallbackReasonsValid |
        DiagnosticsCounterInvalidKeysValid |
        DiagnosticsCounterStaleAndMappingValid |
        DiagnosticsCounterWorkListValid |
        DiagnosticsCounterErrorBudgetValid;

    // Exactly 66 32-bit words. Every field is a per-dispatch counter and every
    // optional family has a bit in validityMask; zero never masquerades as an
    // observed zero when its producer was absent.
    struct DiagnosticsGpuCounterBlock
    {
        uint32_t abiVersion = DiagnosticsGpuAbiInitializationSentinel;
        uint32_t byteSize = 0u;
        uint32_t validityMask = 0u;
        uint32_t flags = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t sampleCount = 0u;
        uint32_t pixelCount = 0u;

        std::array<uint32_t, DiagnosticsGroupHistogramBinCount>
            groupCountHistogram{};
        uint32_t referenceLightingEvaluations = 0u;
        uint32_t performedLightingEvaluations = 0u;
        uint32_t lightingEvaluationsSaved = 0u;
        std::array<uint32_t, DiagnosticsFallbackReasonBinCount>
            fallbackReasonCounts{};
        uint32_t invalidKeyCount = 0u;
        uint32_t staleKeyCount = 0u;
        uint32_t mappingFailureCount = 0u;
        uint32_t workListObservationCount = 0u;
        uint32_t workListOccupancyTotal = 0u;
        uint32_t workListCapacityTotal = 0u;
        uint32_t workListPeakOccupancy = 0u;
        uint32_t workListOverflowCount = 0u;
        std::array<
            uint32_t,
            DiagnosticsErrorBudgetRejectionReasonBinCount>
            errorBudgetRejectionReasonCounts{};
        uint32_t errorBudgetRejectedGroupCount = 0u;
        uint32_t errorBudgetRejectedSampleCount = 0u;
    };

    inline constexpr uint32_t DiagnosticsSelectedRecordCovered = 1u << 0u;
    inline constexpr uint32_t DiagnosticsSelectedRecordKeySupplied = 1u << 1u;
    inline constexpr uint32_t DiagnosticsSelectedRecordKeyValid = 1u << 2u;
    inline constexpr uint32_t DiagnosticsSelectedRecordDepthValid = 1u << 3u;
    inline constexpr uint32_t DiagnosticsSelectedRecordSuppliedBarycentrics =
        1u << 4u;
    inline constexpr uint32_t
        DiagnosticsSelectedRecordReconstructedBarycentrics = 1u << 5u;
    inline constexpr uint32_t DiagnosticsSelectedRecordGroupValid = 1u << 6u;
    inline constexpr uint32_t DiagnosticsSelectedRecordFallbackReason =
        1u << 7u;
    inline constexpr uint32_t DiagnosticsSelectedRecordErrorBudgetReason =
        1u << 8u;
    inline constexpr uint32_t DiagnosticsSelectedRecordRadiance = 1u << 9u;
    inline constexpr uint32_t DiagnosticsSelectedRecordFrontFaceValid =
        1u << 10u;
    inline constexpr uint32_t DiagnosticsSelectedRecordFrontFace = 1u << 11u;
    inline constexpr uint32_t DiagnosticsSelectedRecordWeightValid =
        1u << 12u;
    inline constexpr uint32_t DiagnosticsSelectedRecordAllValidityBits =
        DiagnosticsSelectedRecordCovered |
        DiagnosticsSelectedRecordKeySupplied |
        DiagnosticsSelectedRecordKeyValid |
        DiagnosticsSelectedRecordDepthValid |
        DiagnosticsSelectedRecordSuppliedBarycentrics |
        DiagnosticsSelectedRecordReconstructedBarycentrics |
        DiagnosticsSelectedRecordGroupValid |
        DiagnosticsSelectedRecordFallbackReason |
        DiagnosticsSelectedRecordErrorBudgetReason |
        DiagnosticsSelectedRecordRadiance |
        DiagnosticsSelectedRecordFrontFaceValid |
        DiagnosticsSelectedRecordFrontFace |
        DiagnosticsSelectedRecordWeightValid;

    // Exactly 16 32-bit words. Invalid or unavailable floating-point fields
    // contain the canonical quiet-NaN bit pattern and integer fields contain
    // 0xffffffff. validFields is authoritative.
    struct DiagnosticsGpuSelectedSampleRecord
    {
        uint32_t geometryInstanceHandle = DiagnosticsGpuInvalidWord;
        uint32_t geometryLocalPrimitiveIndex = DiagnosticsGpuInvalidWord;
        uint32_t deviceDepthBits = DiagnosticsGpuInvalidFloatBits;
        uint32_t validFields = 0u;
        uint32_t suppliedBarycentricXBits = DiagnosticsGpuInvalidFloatBits;
        uint32_t suppliedBarycentricYBits = DiagnosticsGpuInvalidFloatBits;
        uint32_t reconstructedBarycentricXBits = DiagnosticsGpuInvalidFloatBits;
        uint32_t reconstructedBarycentricYBits = DiagnosticsGpuInvalidFloatBits;
        uint32_t groupIndex = DiagnosticsGpuInvalidWord;
        uint32_t representativeSampleIndex = DiagnosticsGpuInvalidWord;
        uint32_t weightBits = DiagnosticsGpuInvalidFloatBits;
        uint32_t fallbackReason = DiagnosticsGpuInvalidWord;
        uint32_t errorBudgetReason = DiagnosticsGpuInvalidWord;
        uint32_t radianceRBits = DiagnosticsGpuInvalidFloatBits;
        uint32_t radianceGBits = DiagnosticsGpuInvalidFloatBits;
        uint32_t radianceBBits = DiagnosticsGpuInvalidFloatBits;
    };

    inline constexpr uint32_t DiagnosticsSelectedHeaderValid = 1u << 0u;
    inline constexpr uint32_t DiagnosticsSelectedHeaderViewAvailable = 1u << 1u;
    inline constexpr uint32_t DiagnosticsSelectedHeaderDispatchComplete =
        1u << 2u;
    inline constexpr uint32_t DiagnosticsSelectedHeaderAllValidityBits =
        DiagnosticsSelectedHeaderValid;
    inline constexpr uint32_t DiagnosticsSelectedHeaderAllFlagBits =
        DiagnosticsSelectedHeaderViewAvailable |
        DiagnosticsSelectedHeaderDispatchComplete;

    // Exactly 16 32-bit words. 64-bit identifiers are split little-word first
    // so the shader never depends on native 64-bit arithmetic.
    struct DiagnosticsGpuSelectedHeader
    {
        uint32_t abiVersion = DiagnosticsGpuAbiInitializationSentinel;
        uint32_t byteSize = 0u;
        uint32_t validityMask = 0u;
        uint32_t flags = 0u;
        uint32_t captureIdLow = 0u;
        uint32_t captureIdHigh = 0u;
        uint32_t frameIndexLow = 0u;
        uint32_t frameIndexHigh = 0u;
        uint32_t pixelX = DiagnosticsGpuInvalidWord;
        uint32_t pixelY = DiagnosticsGpuInvalidWord;
        uint32_t sampleCount = 0u;
        uint32_t recordCount = 0u;
        uint32_t requestedView = 0u;
        uint32_t visualizedSampleIndex = DiagnosticsGpuInvalidWord;
        uint32_t reserved0 = 0u;
        uint32_t reserved1 = 0u;
    };

    struct DiagnosticsGpuReadbackBlock
    {
        DiagnosticsGpuCounterBlock counters;
        DiagnosticsGpuSelectedHeader selected;
        std::array<
            DiagnosticsGpuSelectedSampleRecord,
            MaximumAdaptiveSampleCount> selectedSamples{};
    };

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
    static_assert(std::is_standard_layout_v<
        DiagnosticsGpuPixelMetadataRecord>);
    static_assert(std::is_trivially_copyable_v<
        DiagnosticsGpuPixelMetadataRecord>);
    static_assert(sizeof(DiagnosticsGpuCounterBlock) == 264u);
    static_assert(sizeof(DiagnosticsGpuSelectedHeader) == 64u);
    static_assert(sizeof(DiagnosticsGpuSelectedSampleRecord) == 64u);
    static_assert(sizeof(DiagnosticsGpuReadbackBlock) == 1352u);
    static_assert(std::is_standard_layout_v<DiagnosticsGpuReadbackBlock>);
    static_assert(std::is_trivially_copyable_v<DiagnosticsGpuReadbackBlock>);

    inline constexpr uint32_t DiagnosticsGpuCounterGroupHistogramOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            groupCountHistogram));
    inline constexpr uint32_t DiagnosticsGpuCounterReferenceEvaluationsOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            referenceLightingEvaluations));
    inline constexpr uint32_t DiagnosticsGpuCounterPerformedEvaluationsOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            performedLightingEvaluations));
    inline constexpr uint32_t DiagnosticsGpuCounterSavedEvaluationsOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            lightingEvaluationsSaved));
    inline constexpr uint32_t DiagnosticsGpuCounterFallbackReasonsOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            fallbackReasonCounts));
    inline constexpr uint32_t DiagnosticsGpuCounterInvalidKeysOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            invalidKeyCount));
    inline constexpr uint32_t DiagnosticsGpuCounterStaleKeysOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            staleKeyCount));
    inline constexpr uint32_t DiagnosticsGpuCounterMappingFailuresOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            mappingFailureCount));
    inline constexpr uint32_t DiagnosticsGpuCounterWorkListObservationsOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            workListObservationCount));
    inline constexpr uint32_t DiagnosticsGpuCounterWorkListOccupancyOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            workListOccupancyTotal));
    inline constexpr uint32_t DiagnosticsGpuCounterWorkListCapacityOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            workListCapacityTotal));
    inline constexpr uint32_t DiagnosticsGpuCounterWorkListPeakOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            workListPeakOccupancy));
    inline constexpr uint32_t DiagnosticsGpuCounterWorkListOverflowOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            workListOverflowCount));
    inline constexpr uint32_t DiagnosticsGpuCounterErrorBudgetReasonsOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            errorBudgetRejectionReasonCounts));
    inline constexpr uint32_t
        DiagnosticsGpuCounterErrorBudgetRejectedGroupsOffset =
            static_cast<uint32_t>(offsetof(
                DiagnosticsGpuCounterBlock,
                errorBudgetRejectedGroupCount));
    inline constexpr uint32_t
        DiagnosticsGpuCounterErrorBudgetRejectedSamplesOffset =
            static_cast<uint32_t>(offsetof(
                DiagnosticsGpuCounterBlock,
                errorBudgetRejectedSampleCount));
    inline constexpr uint32_t DiagnosticsGpuSelectedHeaderOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuReadbackBlock,
            selected));
    inline constexpr uint32_t DiagnosticsGpuSelectedSamplesOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuReadbackBlock,
            selectedSamples));
    inline constexpr uint32_t DiagnosticsGpuCounterAbiVersionOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            abiVersion));
    inline constexpr uint32_t DiagnosticsGpuCounterByteSizeOffset =
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuCounterBlock,
            byteSize));
    inline constexpr uint32_t DiagnosticsGpuSelectedAbiVersionOffset =
        DiagnosticsGpuSelectedHeaderOffset +
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuSelectedHeader,
            abiVersion));
    inline constexpr uint32_t DiagnosticsGpuSelectedByteSizeOffset =
        DiagnosticsGpuSelectedHeaderOffset +
        static_cast<uint32_t>(offsetof(
            DiagnosticsGpuSelectedHeader,
            byteSize));

    static_assert(DiagnosticsGpuCounterGroupHistogramOffset == 32u);
    static_assert(DiagnosticsGpuCounterReferenceEvaluationsOffset == 100u);
    static_assert(DiagnosticsGpuCounterPerformedEvaluationsOffset == 104u);
    static_assert(DiagnosticsGpuCounterSavedEvaluationsOffset == 108u);
    static_assert(DiagnosticsGpuCounterFallbackReasonsOffset == 112u);
    static_assert(DiagnosticsGpuCounterInvalidKeysOffset == 208u);
    static_assert(DiagnosticsGpuCounterStaleKeysOffset == 212u);
    static_assert(DiagnosticsGpuCounterMappingFailuresOffset == 216u);
    static_assert(DiagnosticsGpuCounterWorkListObservationsOffset == 220u);
    static_assert(DiagnosticsGpuCounterWorkListOccupancyOffset == 224u);
    static_assert(DiagnosticsGpuCounterWorkListCapacityOffset == 228u);
    static_assert(DiagnosticsGpuCounterWorkListPeakOffset == 232u);
    static_assert(DiagnosticsGpuCounterWorkListOverflowOffset == 236u);
    static_assert(DiagnosticsGpuCounterErrorBudgetReasonsOffset == 240u);
    static_assert(
        DiagnosticsGpuCounterErrorBudgetRejectedGroupsOffset == 256u);
    static_assert(
        DiagnosticsGpuCounterErrorBudgetRejectedSamplesOffset == 260u);
    static_assert(DiagnosticsGpuSelectedHeaderOffset == 264u);
    static_assert(DiagnosticsGpuSelectedSamplesOffset == 328u);
    static_assert(DiagnosticsGpuCounterAbiVersionOffset == 0u);
    static_assert(DiagnosticsGpuCounterByteSizeOffset == 4u);
    static_assert(DiagnosticsGpuSelectedAbiVersionOffset == 264u);
    static_assert(DiagnosticsGpuSelectedByteSizeOffset == 268u);

    inline constexpr uint32_t DiagnosticsInputReferenceHdr = 1u << 0u;
    inline constexpr uint32_t DiagnosticsInputAdaptiveHdr = 1u << 1u;
    inline constexpr uint32_t DiagnosticsInputReferenceAttributes = 1u << 2u;
    inline constexpr uint32_t DiagnosticsInputReconstructedAttributes =
        1u << 3u;
    inline constexpr uint32_t DiagnosticsInputSampleStatus = 1u << 4u;
    inline constexpr uint32_t DiagnosticsInputSelectedSampleData = 1u << 5u;
    inline constexpr uint32_t DiagnosticsInputPixelMetadata = 1u << 6u;
    inline constexpr uint32_t DiagnosticsInputRasterSurfaceHits = 1u << 7u;

    // A compact reconstruction comparison is complete only when every pair
    // below is present. Keeping these bits independent makes a partial or
    // accidentally stale producer fail closed instead of allowing one normal
    // attachment to stand in for reconstruction of the complete G-buffer.
    inline constexpr uint32_t DiagnosticsInputReferenceDepth = 1u << 8u;
    inline constexpr uint32_t DiagnosticsInputReconstructedDepth = 1u << 9u;
    inline constexpr uint32_t DiagnosticsInputReferenceDiffuse = 1u << 10u;
    inline constexpr uint32_t DiagnosticsInputReconstructedDiffuse = 1u << 11u;
    inline constexpr uint32_t DiagnosticsInputReferenceMaterial = 1u << 12u;
    inline constexpr uint32_t DiagnosticsInputReconstructedMaterial = 1u << 13u;
    inline constexpr uint32_t DiagnosticsInputReferenceNormals = 1u << 14u;
    inline constexpr uint32_t DiagnosticsInputReconstructedNormals = 1u << 15u;
    inline constexpr uint32_t DiagnosticsInputReferenceEmissive = 1u << 16u;
    inline constexpr uint32_t DiagnosticsInputReconstructedEmissive = 1u << 17u;
    inline constexpr uint32_t DiagnosticsInputReferenceAmbientOcclusion =
        1u << 18u;
    inline constexpr uint32_t DiagnosticsInputReconstructedAmbientOcclusion =
        1u << 19u;
    inline constexpr uint32_t DiagnosticsInputReferenceMotion = 1u << 20u;
    inline constexpr uint32_t DiagnosticsInputReconstructedMotion = 1u << 21u;
    inline constexpr uint32_t DiagnosticsInputVisibilityClass = 1u << 22u;
    inline constexpr uint32_t DiagnosticsInputCompleteReconstruction =
        DiagnosticsInputReferenceDepth |
        DiagnosticsInputReconstructedDepth |
        DiagnosticsInputReferenceDiffuse |
        DiagnosticsInputReconstructedDiffuse |
        DiagnosticsInputReferenceMaterial |
        DiagnosticsInputReconstructedMaterial |
        DiagnosticsInputReferenceNormals |
        DiagnosticsInputReconstructedNormals |
        DiagnosticsInputReferenceEmissive |
        DiagnosticsInputReconstructedEmissive |
        DiagnosticsInputReferenceAmbientOcclusion |
        DiagnosticsInputReconstructedAmbientOcclusion |
        DiagnosticsInputReferenceMotion |
        DiagnosticsInputReconstructedMotion;

    // Device depth is compared in its shader-loadable [0,1] representation.
    // All color/material values are compared after texture-format conversion;
    // emissive uses the same symmetric HDR relative-error definition as final
    // lighting. World position is deliberately absent: this pass has no exact
    // inverse-view/projection and sample-position contract, so claiming that
    // comparison would be less trustworthy than the source depth comparison.
    inline constexpr float DiagnosticsReconstructionDepthThreshold =
        1.f / 1048576.f;
    inline constexpr float DiagnosticsReconstructionDiffuseThreshold =
        1.f / 255.f;
    inline constexpr float DiagnosticsReconstructionMaterialThreshold =
        1.f / 255.f;
    inline constexpr float DiagnosticsReconstructionNormalThreshold =
        1.f / 1024.f;
    inline constexpr float DiagnosticsReconstructionEmissiveThreshold =
        DiagnosticsHdrPercentile99Limit;
    inline constexpr float
        DiagnosticsReconstructionAmbientOcclusionThreshold = 1.f / 255.f;
    inline constexpr float DiagnosticsReconstructionMotionThreshold =
        1.f / 1024.f;

    enum class DiagnosticsGpuErrorDomain : uint32_t
    {
        CurrentFrameSpatial = 0u,
        FrameToFrameTemporal = 1u,
        Count = 2u
    };

    inline constexpr uint32_t DiagnosticsGpuErrorLightingSpatialValid =
        1u << 0u;
    inline constexpr uint32_t DiagnosticsGpuErrorLightingTemporalValid =
        1u << 1u;
    inline constexpr uint32_t DiagnosticsGpuErrorReconstructionValid =
        1u << 2u;
    inline constexpr uint32_t DiagnosticsGpuErrorHistoryReset = 1u << 3u;

    // The renderer must change sceneEpoch for scene reload and every topology,
    // LOD, deformation, or material revision that changes the compared image.
    // It must change modeEpoch for an implementation/permutation/settings
    // change. Width, height, sample count, and contiguous frame index are
    // validated by the pass itself.
    struct DiagnosticsGpuHistoryIdentity
    {
        uint64_t sceneEpoch = 0u;
        uint64_t modeEpoch = 0u;
    };

    struct DiagnosticsGpuHistoryKey
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t sampleCount = 0u;
        uint32_t reserved = 0u;
        uint64_t sceneEpoch = 0u;
        uint64_t modeEpoch = 0u;
    };

    enum class DiagnosticsGpuHistoryTransition : uint32_t
    {
        Disabled = 0u,
        Initialize = 1u,
        Continue = 2u,
        Reset = 3u,
        Count = 4u
    };

    struct DiagnosticsGpuHistoryState
    {
        bool valid = false;
        DiagnosticsGpuHistoryKey key;
        uint64_t frameIndex = 0u;
    };

    [[nodiscard]] DiagnosticsPassStatus
        EvaluateDiagnosticsGpuHistoryTransition(
            bool enabled,
            bool explicitReset,
            const DiagnosticsGpuHistoryKey& requestedKey,
            uint64_t frameIndex,
            const DiagnosticsGpuHistoryState& currentState,
            DiagnosticsGpuHistoryTransition& transition);

    // Frozen diagnostics reuse the last submission-confirmed read surface.
    // They require an already-valid exact identity but intentionally ignore
    // frame contiguity because they neither write nor advance the history.
    [[nodiscard]] DiagnosticsPassStatus
        ValidateDiagnosticsGpuFrozenHistoryRead(
            const DiagnosticsGpuHistoryKey& requestedKey,
            const DiagnosticsGpuHistoryState& currentState);

    struct AdaptiveMsaaDiagnosticsPassPlanDesc
    {
        AdaptiveMsaaDiagnosticsSettings settings;
        DiagnosticsGpuTextureDesc surfaceKeys;
        DiagnosticsGpuTextureDesc depth;
        // Compact visibility supplies its final per-sample class attachment;
        // adaptive G-buffer intentionally leaves this absent.
        DiagnosticsGpuTextureDesc visibilityClass;
        DiagnosticsGpuTextureDesc output;
        DiagnosticsGpuTextureDesc referenceHdr;
        DiagnosticsGpuTextureDesc adaptiveHdr;
        DiagnosticsGpuTextureDesc referenceAttributes;
        DiagnosticsGpuTextureDesc reconstructedAttributes;
        DiagnosticsGpuTextureDesc referenceDepth;
        DiagnosticsGpuTextureDesc reconstructedDepth;
        DiagnosticsGpuTextureDesc referenceDiffuse;
        DiagnosticsGpuTextureDesc reconstructedDiffuse;
        DiagnosticsGpuTextureDesc referenceMaterial;
        DiagnosticsGpuTextureDesc reconstructedMaterial;
        DiagnosticsGpuTextureDesc referenceNormals;
        DiagnosticsGpuTextureDesc reconstructedNormals;
        DiagnosticsGpuTextureDesc referenceEmissive;
        DiagnosticsGpuTextureDesc reconstructedEmissive;
        DiagnosticsGpuTextureDesc referenceAmbientOcclusion;
        DiagnosticsGpuTextureDesc reconstructedAmbientOcclusion;
        DiagnosticsGpuTextureDesc referenceMotion;
        DiagnosticsGpuTextureDesc reconstructedMotion;
        DiagnosticsGpuTextureDesc sampleStatus;
        DiagnosticsGpuTextureDesc rasterSurfaceHits;
        DiagnosticsGpuBufferDesc selectedSampleData;
        DiagnosticsGpuBufferDesc pixelMetadata;
        uint32_t producerCapabilityMask = 0u;
        // This is an explicit renderer contract, never inferred from missing
        // adaptive producer resources. It permits only deterministic
        // per-sample Reference singleton synthesis.
        bool referencePerSample = false;
        uint64_t frameIndex = 0u;
        uint32_t visualizedSampleIndex = 0u;
        uint32_t maximumWorkListCapacity = MaximumAdaptiveSampleCount;
        bool gpuErrorMetricsEnabled = false;
        bool gpuErrorHistoryEnabled = false;
        bool frozenGpuErrorHistoryReadOnly = false;
        bool resetGpuErrorHistory = false;
        DiagnosticsGpuErrorDomain visualizedErrorDomain =
            DiagnosticsGpuErrorDomain::CurrentFrameSpatial;
        DiagnosticsReconstructionChannel reconstructionChannel =
            DiagnosticsReconstructionChannel::Combined;
        DiagnosticsGpuHistoryIdentity historyIdentity;
    };

    struct DiagnosticsPassOperationPlan
    {
        bool allocate = false;
        bool dispatch = false;
        bool copy = false;
        // Queue-completion event for asynchronous CPU readback.
        bool query = false;
        // GPU timestamp pair around the diagnostics dispatch and optional
        // diagnostics readback copy. This is independent from `query` and is
        // true only for an enabled pass-timing request with real pass work.
        bool timerQuery = false;
        bool map = false;
        bool retainDualRenderTargets = false;
    };

    struct AdaptiveMsaaDiagnosticsPassPlan
    {
        DiagnosticsPassStatus status = DiagnosticsPassStatus::Disabled;
        DiagnosticsPassOperationPlan operations;
        bool viewAvailable = false;
        // Debug Disabled plus an active dual-render comparison is normalized
        // to the final-lighting view before constants and readback metadata are
        // written. This is the actual view requested from the shader.
        AdaptiveMsaaDebugView shaderRequestedView =
            AdaptiveMsaaDebugView::Disabled;
        bool readbackRequested = false;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t sampleCount = 0u;
        uint32_t sampleQuality = 0u;
        uint32_t dispatchWidth = 0u;
        uint32_t dispatchHeight = 0u;
        uint32_t inputAvailabilityMask = 0u;
        // Initial CPU-proven families. The consumer may atomically promote
        // ErrorBudget only after reading an in-band-valid exact metadata
        // record; no texture or buffer handle alone promotes a family.
        uint32_t counterValidityMask = 0u;
        uint64_t pixelCount = 0u;
        uint64_t frameSampleCount = 0u;
        // Logical R8_UINT texel payload; API allocation alignment is owned by
        // the caller that creates the optional status attachment.
        uint64_t sampleStatusFootprintBytes = 0u;
        uint64_t requiredSelectedSampleDataBytes = 0u;
        uint64_t requiredPixelMetadataBytes = 0u;
        uint32_t maximumWorkListCapacity = 0u;
        bool referencePerSample = false;
        bool completeReconstructionAvailable = false;
        bool lightingComparisonAvailable = false;
        bool gpuErrorMetricsEnabled = false;
        bool gpuErrorHistoryEnabled = false;
        bool frozenGpuErrorHistoryReadOnly = false;
        bool resetGpuErrorHistory = false;
        DiagnosticsGpuErrorDomain visualizedErrorDomain =
            DiagnosticsGpuErrorDomain::CurrentFrameSpatial;
        DiagnosticsReconstructionChannel reconstructionChannel =
            DiagnosticsReconstructionChannel::Combined;
        DiagnosticsGpuHistoryIdentity historyIdentity;

        [[nodiscard]] bool Succeeded() const
        {
            return status == DiagnosticsPassStatus::Success ||
                status == DiagnosticsPassStatus::Disabled ||
                status == DiagnosticsPassStatus::NoWork;
        }
    };

    [[nodiscard]] AdaptiveMsaaDiagnosticsPassPlan
        BuildAdaptiveMsaaDiagnosticsPassPlan(
            const AdaptiveMsaaDiagnosticsPassPlanDesc& description);

    [[nodiscard]] DiagnosticsGpuReadbackBlock
        MakeInitialDiagnosticsGpuReadbackBlock(
            const AdaptiveMsaaDiagnosticsPassPlan& plan,
            const AdaptiveMsaaDiagnosticsPassPlanDesc& description);

    // Validates a completed GPU copy. Counter or selected-pixel requests must
    // carry their shader-written completion flag; the CPU initialization
    // block is intentionally rejected until the dispatch has executed.
    [[nodiscard]] DiagnosticsPassStatus ValidateDiagnosticsGpuReadbackBlock(
        const DiagnosticsGpuReadbackBlock& block);

    enum class DiagnosticsReadbackSlotState : uint32_t
    {
        Available = 0u,
        Recorded = 1u,
        Submitted = 2u,
        Count = 3u
    };

    struct DiagnosticsReadbackTicket
    {
        DiagnosticsPassStatus status = DiagnosticsPassStatus::ReadbackRingFull;
        uint64_t token = 0u;
        uint32_t slotIndex = DiagnosticsGpuInvalidWord;

        [[nodiscard]] bool Succeeded() const
        {
            return status == DiagnosticsPassStatus::Success;
        }
    };

    class DiagnosticsReadbackRingState
    {
    public:
        [[nodiscard]] DiagnosticsReadbackTicket Acquire();
        [[nodiscard]] DiagnosticsPassStatus MarkSubmitted(uint64_t token);
        [[nodiscard]] DiagnosticsPassStatus CancelRecorded(uint64_t token);
        [[nodiscard]] DiagnosticsPassStatus MarkCompleted(uint64_t token);

        [[nodiscard]] uint32_t FindSlot(uint64_t token) const;
        [[nodiscard]] DiagnosticsReadbackSlotState SlotState(
            uint32_t slotIndex) const;
        [[nodiscard]] uint64_t SlotToken(uint32_t slotIndex) const;
        [[nodiscard]] uint32_t OldestSubmittedSlot() const;
        [[nodiscard]] uint32_t OutstandingCount() const;
        [[nodiscard]] uint32_t SubmittedCount() const;

    private:
        struct Slot
        {
            DiagnosticsReadbackSlotState state =
                DiagnosticsReadbackSlotState::Available;
            uint64_t token = 0u;
        };

        std::array<Slot, DiagnosticsReadbackRingCapacity> m_Slots{};
        std::array<uint32_t, DiagnosticsReadbackRingCapacity>
            m_SubmittedSlots{};
        uint32_t m_SubmittedHead = 0u;
        uint32_t m_SubmittedCount = 0u;
        uint32_t m_NextSlot = 0u;
        uint64_t m_NextToken = 1u;
    };

    struct AdaptiveMsaaDiagnosticsPassInputs
    {
        nvrhi::ITexture* surfaceKeys = nullptr;
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* visibilityClass = nullptr;
        nvrhi::ITexture* output = nullptr;
        nvrhi::ITexture* referenceHdr = nullptr;
        nvrhi::ITexture* adaptiveHdr = nullptr;
        nvrhi::ITexture* referenceAttributes = nullptr;
        nvrhi::ITexture* reconstructedAttributes = nullptr;
        // Complete compact reconstruction comparison. The legacy generic
        // attribute pair above remains ABI-compatible for callers during the
        // transition, but never makes the complete reconstruction view valid.
        nvrhi::ITexture* referenceDepth = nullptr;
        nvrhi::ITexture* reconstructedDepth = nullptr;
        nvrhi::ITexture* referenceDiffuse = nullptr;
        nvrhi::ITexture* reconstructedDiffuse = nullptr;
        nvrhi::ITexture* referenceMaterial = nullptr;
        nvrhi::ITexture* reconstructedMaterial = nullptr;
        nvrhi::ITexture* referenceNormals = nullptr;
        nvrhi::ITexture* reconstructedNormals = nullptr;
        nvrhi::ITexture* referenceEmissive = nullptr;
        nvrhi::ITexture* reconstructedEmissive = nullptr;
        nvrhi::ITexture* referenceAmbientOcclusion = nullptr;
        nvrhi::ITexture* reconstructedAmbientOcclusion = nullptr;
        nvrhi::ITexture* referenceMotion = nullptr;
        nvrhi::ITexture* reconstructedMotion = nullptr;
        nvrhi::ITexture* sampleStatus = nullptr;
        // Diagnostics-only raster-authored SurfaceHit payload in canonical
        // DXR convention: { vertex-1 weight, vertex-2 weight, frontFace,
        // valid } per MSAA sample; vertex-0 weight is 1-x-y.
        nvrhi::ITexture* rasterSurfaceHits = nullptr;
        nvrhi::IBuffer* selectedSampleData = nullptr;
        nvrhi::IBuffer* pixelMetadata = nullptr;
        // Resource presence proves only that bytes can be read. This mask is
        // the current-frame producer's claim about which counter families the
        // bytes actually contain; the consumer intersects it with resource
        // availability and never infers work-list or error data from a handle.
        uint32_t producerCapabilityMask = 0u;
        bool referencePerSample = false;
    };

    struct AdaptiveMsaaDiagnosticsPassRequest
    {
        AdaptiveMsaaDiagnosticsSettings settings;
        uint64_t frameIndex = 0u;
        uint32_t visualizedSampleIndex = 0u;
        uint32_t maximumWorkListCapacity = MaximumAdaptiveSampleCount;
        // Error metrics are opt-in even while the diagnostics drawer is open.
        // History additionally requires a nonzero scene/mode identity and a
        // lighting reference pair. No resource is allocated for either path
        // until an enabled, valid request reaches Execute.
        bool gpuErrorMetricsEnabled = false;
        bool gpuErrorHistoryEnabled = false;
        // Reuse a matching submission-confirmed history surface without
        // writing the other ping-pong surface, publishing a pending token, or
        // advancing frame identity. This is valid only for temporal history
        // and fails closed when no exact prior identity exists.
        bool frozenGpuErrorHistoryReadOnly = false;
        bool resetGpuErrorHistory = false;
        DiagnosticsGpuErrorDomain visualizedErrorDomain =
            DiagnosticsGpuErrorDomain::CurrentFrameSpatial;
        DiagnosticsReconstructionChannel reconstructionChannel =
            DiagnosticsReconstructionChannel::Combined;
        DiagnosticsGpuHistoryIdentity historyIdentity;
    };

    [[nodiscard]] inline bool
        IsAcceptedBenchmarkDiagnosticsPassRequestSafe(
            const AdaptiveMsaaDiagnosticsPassRequest& request)
    {
        return IsAcceptedBenchmarkDiagnosticsSafe(request.settings) &&
            !request.gpuErrorMetricsEnabled &&
            !request.gpuErrorHistoryEnabled &&
            !request.frozenGpuErrorHistoryReadOnly &&
            !request.resetGpuErrorHistory;
    }

    struct AdaptiveMsaaDiagnosticsPassTiming
    {
        float gpuMilliseconds = 0.f;
        uint64_t frameIndex = 0u;
        bool available = false;
    };

    // Diagnostic-only GPU outputs. The three error textures are single-sample
    // RGBA32_FLOAT and match the render extent. Reconstruction0 stores raw
    // maximum absolute errors
    // for {depth, diffuse, material, normals}. Reconstruction1 stores
    // {relative emissive, ambient occlusion, motion, maximum normalized
    // reconstruction error}. Lighting stores {current spatial HDR error,
    // frame-to-frame HDR residual error, spatial/p99-limit,
    // temporal/p99-limit}. Validity is R8_UINT with the flags above.
    struct AdaptiveMsaaDiagnosticsGpuErrorOutputs
    {
        nvrhi::ITexture* reconstruction0 = nullptr;
        nvrhi::ITexture* reconstruction1 = nullptr;
        nvrhi::ITexture* lighting = nullptr;
        nvrhi::ITexture* validity = nullptr;
        bool currentFrameRecorded = false;
        bool temporalHistoryRead = false;
        bool frozenHistoryReadOnly = false;
        DiagnosticsGpuHistoryTransition historyTransition =
            DiagnosticsGpuHistoryTransition::Disabled;
    };

    struct AdaptiveMsaaDiagnosticsPassResult
    {
        DiagnosticsPassStatus status = DiagnosticsPassStatus::Disabled;
        bool dispatched = false;
        bool viewAvailable = false;
        bool readbackRecorded = false;
        bool timerQueryRecorded = false;
        bool historyWriteRecorded = false;
        uint64_t readbackToken = 0u;
        uint64_t historyToken = 0u;
        uint32_t dispatchWidth = 0u;
        uint32_t dispatchHeight = 0u;
        // Latest completed nonblocking timing. It may describe an earlier
        // frame than this Execute call; frameIndex makes that explicit.
        AdaptiveMsaaDiagnosticsPassTiming timing;
        AdaptiveMsaaDiagnosticsGpuErrorOutputs errorOutputs;
    };

    struct DiagnosticsReadbackPollResult
    {
        DiagnosticsPassStatus status = DiagnosticsPassStatus::ReadbackEmpty;
        bool completed = false;
        uint64_t token = 0u;
    };

    // The constructor records dependencies only and performs no GPU work or
    // allocation. Resources and shader variants are created lazily by the
    // first valid enabled Execute call. External textures/buffers are held only
    // by the caller's command list, never retained by this pass.
    class AdaptiveMsaaDiagnosticsGpuPass final
    {
    public:
        AdaptiveMsaaDiagnosticsGpuPass(
            nvrhi::IDevice* device,
            std::shared_ptr<donut::engine::ShaderFactory> shaderFactory);
        ~AdaptiveMsaaDiagnosticsGpuPass();

        AdaptiveMsaaDiagnosticsGpuPass(
            const AdaptiveMsaaDiagnosticsGpuPass&) = delete;
        AdaptiveMsaaDiagnosticsGpuPass& operator=(
            const AdaptiveMsaaDiagnosticsGpuPass&) = delete;

        [[nodiscard]] AdaptiveMsaaDiagnosticsPassResult Execute(
            nvrhi::ICommandList* commandList,
            const AdaptiveMsaaDiagnosticsPassInputs& inputs,
            const AdaptiveMsaaDiagnosticsPassRequest& request);

        // Call only after the command list containing the returned token has
        // been submitted to the graphics queue. This enqueues a queue event;
        // it never waits. Cancel is valid only when that command list was
        // discarded and will never execute.
        [[nodiscard]] DiagnosticsPassStatus SignalReadbackSubmission(
            uint64_t token);
        [[nodiscard]] DiagnosticsPassStatus CancelUnsubmittedReadback(
            uint64_t token);

        // History becomes readable only after the caller confirms submission
        // of the command list that contains its write. Cancel is valid only
        // for a discarded command list. This mirrors readback ownership and
        // prevents a recorded-but-never-executed frame from becoming history.
        [[nodiscard]] DiagnosticsPassStatus ConfirmErrorHistorySubmission(
            uint64_t token);
        [[nodiscard]] DiagnosticsPassStatus CancelErrorHistorySubmission(
            uint64_t token);
        [[nodiscard]] DiagnosticsPassStatus InvalidateErrorHistory();

        // These recovery methods are valid only after the caller has waited
        // for the graphics queue to become idle. They release quarantined
        // ring state when submission signaling or teardown cannot proceed.
        [[nodiscard]] DiagnosticsPassStatus
            ReleaseReadbackAfterQueueIdle(uint64_t token);
        void ReleaseAllReadbacksAfterQueueIdle();

        // Poll performs only nonblocking event tests. It maps a readback buffer
        // exclusively after pollEventQuery reports completion. Disabled
        // settings return before touching the device or ring.
        [[nodiscard]] DiagnosticsReadbackPollResult PollReadback(
            const AdaptiveMsaaDiagnosticsSettings& settings,
            DiagnosticsGpuReadbackBlock& output);

        // Retires completed queue events without mapping or validating their
        // staging buffers. This is used only for quarantined passes after
        // diagnostics have been disabled or their render targets replaced.
        [[nodiscard]] DiagnosticsPassStatus RetireCompletedReadbacks();

        [[nodiscard]] uint32_t OutstandingReadbackCount() const;
        // Pure getter: it never polls, maps, allocates, or touches the device.
        [[nodiscard]] const AdaptiveMsaaDiagnosticsPassTiming&
            GetLatestGpuTiming() const
        {
            return m_LatestGpuTiming;
        }
        [[nodiscard]] bool HasAllocatedGpuResources() const;

    private:
        struct ReadbackSlot
        {
            nvrhi::BufferHandle buffer;
            nvrhi::EventQueryHandle eventQuery;
        };

        struct GpuConstants
        {
            uint32_t width = 0u;
            uint32_t height = 0u;
            uint32_t sampleCount = 0u;
            uint32_t requestedView = 0u;
            uint32_t comparisonMode = 0u;
            uint32_t visualizedSampleIndex = 0u;
            uint32_t inputAvailabilityMask = 0u;
            uint32_t executionFlags = 0u;
            uint32_t selectedPixelX = 0u;
            uint32_t selectedPixelY = 0u;
            uint32_t captureIdLow = 0u;
            uint32_t captureIdHigh = 0u;
            uint32_t frameIndexLow = 0u;
            uint32_t frameIndexHigh = 0u;
            uint32_t counterValidityMask = 0u;
            uint32_t maximumWorkListCapacity = 0u;
            uint32_t errorDomain = 0u;
            uint32_t reconstructionChannel = 0u;
            uint32_t errorExecutionFlags = 0u;
            uint32_t historyValid = 0u;
        };
        static_assert(sizeof(GpuConstants) == 80u);

        [[nodiscard]] DiagnosticsPassStatus EnsureCoreResources(
            uint32_t sampleCount);
        [[nodiscard]] DiagnosticsPassStatus EnsureReadbackResources();
        [[nodiscard]] DiagnosticsPassStatus EnsureTimerResources();
        [[nodiscard]] DiagnosticsPassStatus EnsureErrorMetricResources(
            uint32_t width,
            uint32_t height,
            bool historyEnabled);
        void AdvanceTimerQueries();
        [[nodiscard]] bool BeginTimerQuery(
            nvrhi::ICommandList* commandList,
            uint64_t frameIndex,
            uint32_t& slotIndex);
        void EndTimerQuery(
            nvrhi::ICommandList* commandList,
            uint32_t slotIndex);
        [[nodiscard]] nvrhi::BindingSetHandle CreateBindingSet(
            const AdaptiveMsaaDiagnosticsPassInputs& inputs,
            const AdaptiveMsaaDiagnosticsPassPlan& plan,
            uint32_t historyReadIndex,
            uint32_t historyWriteIndex) const;
        [[nodiscard]] static uint32_t PipelineIndex(uint32_t sampleCount);

        nvrhi::IDevice* m_Device = nullptr;
        std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        std::array<nvrhi::ShaderHandle, 4u> m_Shaders{};
        std::array<nvrhi::ComputePipelineHandle, 4u> m_Pipelines{};
        nvrhi::BufferHandle m_GpuReadback;
        nvrhi::BufferHandle m_DummySelectedSampleData;
        nvrhi::BufferHandle m_DummyPixelMetadata;
        nvrhi::TextureHandle m_DummyColorInput;
        nvrhi::TextureHandle m_DummyScalarInput;
        nvrhi::TextureHandle m_DummySampleStatus;
        nvrhi::TextureHandle m_DummyOutput;
        nvrhi::TextureHandle m_DummyUintOutput;
        nvrhi::TextureHandle m_ReconstructionError0;
        nvrhi::TextureHandle m_ReconstructionError1;
        nvrhi::TextureHandle m_LightingError;
        nvrhi::TextureHandle m_ErrorValidity;
        std::array<nvrhi::TextureHandle, 2u> m_ReferenceHistory{};
        std::array<nvrhi::TextureHandle, 2u> m_AdaptiveHistory{};
        std::array<ReadbackSlot, DiagnosticsReadbackRingCapacity>
            m_ReadbackSlots{};
        DiagnosticsReadbackRingState m_ReadbackState;
        std::array<nvrhi::TimerQueryHandle,
            DiagnosticsGpuTimerRingCapacity> m_TimerQueries{};
        std::array<bool, DiagnosticsGpuTimerRingCapacity>
            m_TimerPending{};
        std::array<uint64_t, DiagnosticsGpuTimerRingCapacity>
            m_TimerFrameIndices{};
        AdaptiveMsaaDiagnosticsPassTiming m_LatestGpuTiming;
        uint32_t m_NextTimerSlot = 0u;
        struct PendingHistoryWrite
        {
            bool valid = false;
            uint64_t token = 0u;
            uint64_t frameIndex = 0u;
            uint32_t writeIndex = 0u;
            DiagnosticsGpuHistoryKey key;
        };
        DiagnosticsGpuHistoryState m_HistoryState;
        PendingHistoryWrite m_PendingHistoryWrite;
        uint64_t m_NextHistoryToken = 1u;
        uint32_t m_HistoryReadIndex = 0u;
        uint32_t m_ErrorResourceWidth = 0u;
        uint32_t m_ErrorResourceHeight = 0u;
        bool m_CoreResourcesReady = false;
        bool m_ReadbackResourcesReady = false;
        bool m_TimerResourcesReady = false;
        bool m_ErrorMetricResourcesReady = false;
        bool m_ErrorHistoryResourcesReady = false;
    };
}
