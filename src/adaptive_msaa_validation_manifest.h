#pragma once

#include "adaptive_msaa_validation_plan.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace uvsr::adaptive_msaa
{
    inline constexpr uint32_t AdaptiveMsaaValidationManifestSchema = 2u;
    inline constexpr std::string_view AdaptiveMsaaValidationManifestFileName =
        "adaptive_msaa_validation_manifest.json";

    enum class AdaptiveMsaaValidationManifestReason : uint32_t
    {
        None = 0u,
        NotInitialized,
        InvalidPlan,
        OutputRootMissing,
        OutputRootNotAbsolute,
        OutputRootNotFound,
        OutputRootNotDirectory,
        OutputRootCanonicalizationFailed,
        OutputRootEnumerationFailed,
        UnsafeEvidencePath,
        EvidenceMissing,
        EvidenceNotRegularFile,
        EvidenceEmpty,
        EvidenceCollision,
        DestinationCollision,
        StaleTemporaryFile,
        SerializationFailed,
        TemporaryNameExhausted,
        AtomicCreateFailed,
        AtomicWriteFailed,
        AtomicFlushFailed,
        AtomicReplaceFailed,
        AllocationFailed
    };

    struct AdaptiveMsaaValidationManifestStatus
    {
        AdaptiveMsaaValidationManifestReason reason =
            AdaptiveMsaaValidationManifestReason::NotInitialized;
        AdaptiveMsaaValidationPlanReason planReason =
            AdaptiveMsaaValidationPlanReason::NotInitialized;
        size_t itemIndex = 0u;
        uint32_t phaseIndex = 0u;
        bool evidenceIsReceipt = false;
        uint32_t systemError = 0u;
        size_t byteCount = 0u;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return reason == AdaptiveMsaaValidationManifestReason::None;
        }
    };

    struct AdaptiveMsaaValidationManifestWriteResult
    {
        AdaptiveMsaaValidationManifestStatus status;
        std::filesystem::path manifestPath;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status.Succeeded();
        }
    };

    [[nodiscard]] const char* GetAdaptiveMsaaValidationManifestReasonName(
        AdaptiveMsaaValidationManifestReason reason) noexcept;

    // Validates the exact deterministic plan and every planned BMP/receipt,
    // then writes the evaluator's fixed schema-2 manifest. The output root must
    // already exist and be absolute. Evidence is never created or modified.
    // A normal prior manifest may be replaced, while directories, reparse
    // aliases, evidence aliases, and stale writer temporaries fail closed.
    [[nodiscard]] AdaptiveMsaaValidationManifestWriteResult
        WriteAdaptiveMsaaValidationManifest(
            const std::filesystem::path& outputRoot,
            const AdaptiveMsaaValidationPlan& plan);
}
