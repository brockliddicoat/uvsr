#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace uvsr::adaptive_msaa
{
    inline constexpr uint32_t AdaptiveMsaaValidationReceiptSchema = 2u;
    inline constexpr const char* AdaptiveMsaaValidationReceiptKind =
        "uvsr-adaptive-msaa-validation-frame";
    inline constexpr size_t AdaptiveMsaaValidationGroupHistogramBinCount = 17u;
    // Must remain index-compatible with FallbackReason::Count. The renderer
    // enforces that producer/receipt ABI relationship before publication.
    inline constexpr size_t AdaptiveMsaaValidationFallbackReasonCount = 24u;
    inline constexpr size_t
        AdaptiveMsaaValidationErrorBudgetReasonCount = 4u;

    struct AdaptiveMsaaValidationViewport
    {
        uint32_t minX = 0u;
        uint32_t minY = 0u;
        uint32_t maxX = 0u;
        uint32_t maxY = 0u;
    };

    struct AdaptiveMsaaValidationRoi
    {
        uint32_t x = 0u;
        uint32_t y = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    // Schema-2 is a single-frame receipt consumed by the schema-2 Python image
    // evaluator. It records only immutable CPU facts already produced by the
    // renderer; this module performs no GPU work, readback, or UI mutation.
    struct AdaptiveMsaaValidationReceipt
    {
        uint32_t schema = AdaptiveMsaaValidationReceiptSchema;
        std::string kind = AdaptiveMsaaValidationReceiptKind;

        std::string captureId;
        std::string suiteId;
        std::string caseId;
        std::string recipeSha256;
        uint32_t phaseIndex = 0u;
        double phase = 0.0;

        // These two fields are output-only. The writer replaces their input
        // values with the canonical BMP path and its freshly computed digest.
        std::string imagePath;
        std::string imageSha256;

        std::string sourceCommit;
        std::string sourceDiffSha256;
        std::string rendererBuildManifestSha256;
        std::string rendererExecutableSha256;

        std::string sceneName;
        std::string sceneDescriptorSha256;
        std::string sceneGltfSha256;
        std::string cameraSha256;
        std::string settingsSha256;

        uint32_t width = 0u;
        uint32_t height = 0u;
        AdaptiveMsaaValidationViewport viewport;
        uint64_t frozenSeed = 0u;

        uint64_t topologyEpoch = 0u;
        uint32_t topologyRevision = 0u;
        uint32_t lodRevision = 0u;
        uint32_t deformationRevision = 0u;
        uint32_t materialRevision = 0u;

        std::string requestedImplementation;
        std::string resolvedImplementation;
        std::string appliedImplementation;
        uint32_t requestedSampleCount = 0u;
        uint32_t resolvedSampleCount = 0u;
        uint32_t appliedSampleCount = 0u;

        bool selectedPathSucceeded = false;
        uint64_t submittedMainViewTriangles = 0u;

        bool diagnosticsProducerProducedThisFrame = false;
        uint64_t diagnosticsReadbackToken = 0u;
        uint64_t diagnosticsCompletedToken = 0u;
        bool diagnosticsDispatchComplete = false;
        uint32_t diagnosticsValidityMask = 0u;

        std::array<
            uint64_t,
            AdaptiveMsaaValidationGroupHistogramBinCount> groupHistogram{};
        uint64_t referenceLightingEvaluations = 0u;
        uint64_t performedLightingEvaluations = 0u;
        uint64_t lightingEvaluationsSaved = 0u;
        std::array<
            uint64_t,
            AdaptiveMsaaValidationFallbackReasonCount> fallbackReasonCounts{};
        uint64_t invalidKeyCount = 0u;
        uint64_t staleKeyCount = 0u;
        uint64_t mappingFailureCount = 0u;
        std::array<
            uint64_t,
            AdaptiveMsaaValidationErrorBudgetReasonCount>
                errorBudgetRejectionReasonCounts{};
        uint64_t errorBudgetRejectedGroupCount = 0u;
        uint64_t errorBudgetRejectedSampleCount = 0u;

        bool compactShadeSucceeded = false;
        uint32_t compactShadeDispatchedViewCount = 0u;
        uint64_t compactReconstructDrawCount = 0u;
        uint64_t compactReconstructTriangleCount = 0u;
        uint64_t compactFallbackDrawCount = 0u;
        uint64_t compactFallbackTriangleCount = 0u;

        std::string preparationTopology;
        uint64_t workListObservationCount = 0u;
        uint64_t workListCapacityTotal = 0u;
        uint64_t workListOccupancyTotal = 0u;
        uint64_t workListPeakOccupancy = 0u;
        uint64_t workListOverflowCount = 0u;

        AdaptiveMsaaValidationRoi roi;
    };

    enum class AdaptiveMsaaValidationCaptureReason : uint32_t
    {
        None = 0u,
        PathMissing,
        BmpPathRequired,
        ImageMissing,
        ImageNotRegularFile,
        ImageReadFailed,
        ImageHashFailed,
        InvalidSchema,
        InvalidKind,
        InvalidToken,
        InvalidHash,
        InvalidDimensions,
        InvalidViewport,
        InvalidRoi,
        InvalidRevision,
        InvalidImplementation,
        InvalidSampleCount,
        InvalidDiagnostics,
        InvalidLightingWork,
        InvalidCompactWork,
        InvalidPreparationTopology,
        InvalidWorkList,
        SerializationFailed,
        TemporaryNameExhausted,
        AtomicCreateFailed,
        AtomicWriteFailed,
        AtomicFlushFailed,
        AtomicReplaceFailed
    };

    struct AdaptiveMsaaValidationCaptureStatus
    {
        AdaptiveMsaaValidationCaptureReason reason =
            AdaptiveMsaaValidationCaptureReason::None;
        uint32_t systemError = 0u;
        size_t byteCount = 0u;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == AdaptiveMsaaValidationCaptureReason::None;
        }
    };

    struct AdaptiveMsaaValidationCaptureWriteResult
    {
        AdaptiveMsaaValidationCaptureStatus status;
        std::filesystem::path receiptPath;
        std::string canonicalImagePath;
        std::string imageSha256;

        [[nodiscard]] bool Succeeded() const
        {
            return status.Succeeded();
        }
    };

    [[nodiscard]] const char* GetAdaptiveMsaaValidationCaptureReasonName(
        AdaptiveMsaaValidationCaptureReason reason) noexcept;

    // Pure lexical derivation. The writer canonicalizes the existing BMP first
    // and derives the actual sibling receipt from that canonical path.
    [[nodiscard]] std::filesystem::path
        DeriveAdaptiveMsaaValidationReceiptPath(
            const std::filesystem::path& bmpPath);

    // Validates a finalized DTO, including canonical lowercase hashes/tokens
    // and internally consistent diagnostics/work counters. It deliberately
    // does not compare reference and candidate receipts.
    [[nodiscard]] AdaptiveMsaaValidationCaptureStatus
        ValidateAdaptiveMsaaValidationReceipt(
            const AdaptiveMsaaValidationReceipt& receipt) noexcept;

    // Computes imagePath and imageSha256 from bmpPath, serializes compact
    // canonical JSON through JsonCpp, then atomically replaces the sibling
    // receipt. Failure preserves any prior receipt and removes the temporary.
    [[nodiscard]] AdaptiveMsaaValidationCaptureWriteResult
        WriteAdaptiveMsaaValidationReceipt(
            const std::filesystem::path& bmpPath,
            const AdaptiveMsaaValidationReceipt& receipt);
}
