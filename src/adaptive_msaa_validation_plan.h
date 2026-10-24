#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr::adaptive_msaa
{
    inline constexpr uint32_t AdaptiveMsaaValidationRecipeSchema = 1u;
    inline constexpr std::string_view AdaptiveMsaaValidationSuiteId =
        "uvsr-adaptive-msaa-image-diff-v1";
    inline constexpr std::string_view AdaptiveMsaaValidationRecipeSha256 =
        "0ff1ec5e89f0eaefbb5f2422e2561af316caf6c9d31f53c8020c306fd86e5883";
    inline constexpr uint32_t AdaptiveMsaaValidationWidth = 1920u;
    inline constexpr uint32_t AdaptiveMsaaValidationHeight = 1080u;
    inline constexpr size_t AdaptiveMsaaValidationPhaseCount = 2u;
    inline constexpr size_t AdaptiveMsaaValidationCaseCount = 15u;
    inline constexpr size_t AdaptiveMsaaValidationSampleCountCount = 4u;
    inline constexpr size_t AdaptiveMsaaValidationReferenceJobCount = 60u;
    inline constexpr size_t AdaptiveMsaaValidationCandidateJobCount = 120u;
    inline constexpr size_t AdaptiveMsaaValidationCaptureJobCount = 180u;
    inline constexpr size_t AdaptiveMsaaValidationComparisonCount = 120u;

    inline constexpr std::array<uint32_t,
        AdaptiveMsaaValidationSampleCountCount>
        AdaptiveMsaaValidationSampleCounts{{ 2u, 4u, 8u, 16u }};

    inline constexpr std::array<std::string_view,
        AdaptiveMsaaValidationCaseCount>
        AdaptiveMsaaValidationRequiredCases{{
            "crossing-triangles",
            "shared-material-submeshes",
            "instancing",
            "alpha-cutouts",
            "double-sided",
            "tiny-steep-geometry",
            "sharp-specular",
            "normal-mapped",
            "emissive-detail",
            "sky-edges",
            "motion-taa",
            "moving-shadow-edges",
            "nearby-lights",
            "grazing-geometry",
            "screen-space-visibility"
        }};

    enum class AdaptiveMsaaValidationImplementation : uint32_t
    {
        Reference = 0u,
        AdaptiveGBuffer,
        CompactVisibility
    };

    struct AdaptiveMsaaValidationPlanRoi
    {
        uint32_t x = 0u;
        uint32_t y = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    struct AdaptiveMsaaValidationPlanCamera
    {
        std::array<double, 3u> position{};
        std::array<double, 3u> direction{};
        std::array<double, 3u> up{};
        double verticalFovDegrees = 0.0;
    };

    struct AdaptiveMsaaValidationPlanSettings
    {
        bool taa = false;
        bool flashlight = false;
        bool flashlightShadows = false;
        bool directionalShadows = false;
        bool screenSpaceVisibility = false;
    };

    struct AdaptiveMsaaValidationPlanCase
    {
        std::string id;
        AdaptiveMsaaValidationPlanRoi roi;
        std::vector<std::string> nodes;
        std::vector<std::string> materials;
        AdaptiveMsaaValidationPlanSettings settings;
    };

    struct AdaptiveMsaaValidationRecipe
    {
        uint32_t schema = 0u;
        std::string suiteId;
        uint32_t width = 0u;
        uint32_t height = 0u;
        std::array<double, AdaptiveMsaaValidationPhaseCount> phases{};
        AdaptiveMsaaValidationPlanCamera camera;
        std::vector<AdaptiveMsaaValidationPlanCase> cases;
        std::string sha256;
    };

    struct AdaptiveMsaaValidationCaptureFrame
    {
        uint32_t phaseIndex = 0u;
        double phase = 0.0;
        std::string phaseId;
        // Per-frame receipt identity. A two-phase job has one job ID but two
        // distinct capture IDs so candidate evidence cannot alias a phase.
        std::string captureId;
        std::filesystem::path relativeBmpPath;
        std::filesystem::path relativeReceiptPath;
    };

    struct AdaptiveMsaaValidationCaptureJob
    {
        std::string id;
        AdaptiveMsaaValidationImplementation implementation =
            AdaptiveMsaaValidationImplementation::Reference;
        uint32_t sampleCount = 0u;
        size_t caseIndex = 0u;
        std::string caseId;
        std::array<AdaptiveMsaaValidationCaptureFrame,
            AdaptiveMsaaValidationPhaseCount> frames{};
    };

    struct AdaptiveMsaaValidationComparison
    {
        std::string id;
        std::string caseId;
        uint32_t sampleCount = 0u;
        AdaptiveMsaaValidationImplementation candidateImplementation =
            AdaptiveMsaaValidationImplementation::AdaptiveGBuffer;
        size_t referenceCaptureJobIndex = 0u;
        size_t candidateCaptureJobIndex = 0u;
    };

    struct AdaptiveMsaaValidationPlan
    {
        AdaptiveMsaaValidationRecipe recipe;
        std::vector<AdaptiveMsaaValidationCaptureJob> captureJobs;
        std::vector<AdaptiveMsaaValidationComparison> comparisons;
    };

    enum class AdaptiveMsaaValidationPlanReason : uint32_t
    {
        None = 0u,
        NotInitialized,
        PathMissing,
        FileMissing,
        FileNotRegular,
        FileOpenFailed,
        FileSizeInvalid,
        FileReadFailed,
        HashProviderUnavailable,
        HashPropertyQueryFailed,
        HashStateAllocationFailed,
        HashCreationFailed,
        HashDataFailed,
        HashFinishFailed,
        ExpectedHashInvalid,
        ExpectedHashMismatch,
        JsonParseFailed,
        InvalidRootFields,
        InvalidSchema,
        InvalidSuite,
        InvalidResolution,
        InvalidPhases,
        InvalidCamera,
        InvalidCaseCount,
        InvalidCaseOrder,
        InvalidCaseFields,
        InvalidCaseId,
        InvalidRoi,
        InvalidTargets,
        InvalidSettings,
        InvalidImplementation,
        InvalidSampleCount,
        InvalidCaptureJob,
        InvalidComparison,
        DuplicateId,
        UnsafePath,
        AllocationFailed
    };

    struct AdaptiveMsaaValidationPlanStatus
    {
        AdaptiveMsaaValidationPlanReason reason =
            AdaptiveMsaaValidationPlanReason::NotInitialized;
        size_t itemIndex = 0u;
        uint32_t systemError = 0u;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return reason == AdaptiveMsaaValidationPlanReason::None;
        }
    };

    struct AdaptiveMsaaValidationPlanResult
    {
        AdaptiveMsaaValidationPlanStatus status;
        AdaptiveMsaaValidationPlan plan;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status.Succeeded();
        }
    };

    [[nodiscard]] const char* GetAdaptiveMsaaValidationPlanReasonName(
        AdaptiveMsaaValidationPlanReason reason) noexcept;

    [[nodiscard]] const char*
        GetAdaptiveMsaaValidationPlanImplementationName(
        AdaptiveMsaaValidationImplementation implementation) noexcept;

    // Validates both recipe data and the exact deterministic job graph. This
    // is intentionally independent from renderer and NVRHI types so callers
    // can fail closed before mutating runtime state.
    [[nodiscard]] AdaptiveMsaaValidationPlanStatus
        ValidateAdaptiveMsaaValidationPlan(
            const AdaptiveMsaaValidationPlan& plan) noexcept;

    // Reads and hashes the exact recipe bytes, strictly parses schema 1, then
    // emits the fixed 180-capture/120-comparison graph. When expectedSha256 is
    // nonempty it must be canonical lowercase SHA-256 and match the file.
    [[nodiscard]] AdaptiveMsaaValidationPlanResult
        LoadAdaptiveMsaaValidationPlan(
            const std::filesystem::path& recipePath,
            std::string_view expectedSha256 = {});
}
