#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    struct RetainedRuntimeCase;
    struct RuntimeOutputEvidence;

    struct RuntimeSemanticSignature
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        double meanLinearLuminance = 0.0;
        double rmsLinearLuminance = 0.0;
        double meanLinearHorizontalGradient = 0.0;
        std::array<std::uint64_t, 16> linearLuminanceHistogram{};
        std::uint64_t linearLuminanceSampleCount = 0u;
    };

    struct RetainedRuntimeSemanticCaptureView
    {
        std::string_view caseName;
        std::string_view sceneToken;
        RuntimeSemanticSignature signature;
    };

    enum class RetainedRuntimeSemanticFailure : std::uint8_t
    {
        None,
        CaseIdentity,
        CaptureIdentity,
        Coverage,
        NotDistinct
    };

    struct RetainedRuntimeSemanticResult
    {
        RetainedRuntimeSemanticFailure failure = RetainedRuntimeSemanticFailure::None;
        // borrows immutable case storage through synchronous failure reporting.
        const RetainedRuntimeCase* runtimeCase = nullptr;
        [[nodiscard]] bool Passed() const noexcept
        {
            return failure == RetainedRuntimeSemanticFailure::None;
        }
    };

    [[nodiscard]] RuntimeSemanticSignature BuildRuntimeSemanticSignature(
        const RuntimeOutputEvidence& output) noexcept;
    [[nodiscard]] bool RuntimeSemanticSignaturesAreDistinct(
        const RuntimeSemanticSignature& left,
        const RuntimeSemanticSignature& right) noexcept;
    // counts describe live arrays, and capture text views remain valid during the call.
    [[nodiscard]] RetainedRuntimeSemanticResult CheckRetainedRuntimeSemanticCaptures(
        const RetainedRuntimeCase* cases, std::size_t caseCount,
        const RetainedRuntimeSemanticCaptureView* captures, std::size_t captureCount) noexcept;

    // begin, record and complete each case once, using that same case and its
    // immutable scene tokens. case storage survives final validation and reporting.
    // failures stay deferred while subsequent cases continue.
    class RetainedRuntimeSemanticSummary
    {
    public:
        void BeginCase() noexcept;
        void Record(const RetainedRuntimeCase& runtimeCase, std::string_view sceneToken,
            const RuntimeSemanticSignature& signature) noexcept;
        void CompleteCase(const RetainedRuntimeCase& runtimeCase) noexcept;
        [[nodiscard]] RetainedRuntimeSemanticResult Validate(
            const RetainedRuntimeCase* cases, std::size_t caseCount) const noexcept;

    private:
        RuntimeSemanticSignature m_FirstSignature;
        std::string_view m_FirstSceneToken;
        std::size_t m_CaseCaptureCount = 0;
        bool m_PairIsDistinct = false;
        std::size_t m_CaptureCount = 0;
        std::size_t m_RepresentedCases = 0;
        bool m_CaptureIdentityDrifted = false;
        const RetainedRuntimeCase* m_FirstFailedCase = nullptr;
    };
}
