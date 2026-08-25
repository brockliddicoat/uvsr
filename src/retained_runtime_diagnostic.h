#pragma once

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace uvsr
{
    enum class RetainedRuntimeAction : std::uint8_t
    {
        None,
        NudgeCamera,
        ResizeViewport,
        ChangeScene,
        ChangeSetting,
        ChangeMaterial,
        ChangeLight,
        ToggleFlashlight,
        CycleLightingSolution
    };

    struct RuntimeOutputEvidence
    {
        bool valid = false;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::uint64_t encodedBytes = 0u;
        std::uint64_t pixelBytes = 0u;
        std::uint64_t pixelHash = 1469598103934665603ull;
        unsigned char minimumByte = 0u;
        unsigned char maximumByte = 0u;
        std::string artifactPath;
        bool linearReadbackValid = false;
        std::uint64_t linearHash = 1469598103934665603ull;
        std::uint64_t finiteComponentCount = 0u;
        std::uint64_t nonFiniteComponentCount = 0u;
        std::uint64_t varyingPixelCount = 0u;
        std::uint64_t edgePixelCount = 0u;
        float minimumLinearValue = 0.f;
        float maximumLinearValue = 0.f;
        double meanLinearLuminance = 0.0;
        double rmsLinearLuminance = 0.0;
        double meanLinearHorizontalGradient = 0.0;
        std::array<std::uint64_t, 16> linearLuminanceHistogram{};
    };

    [[nodiscard]] RuntimeOutputEvidence AnalyzeRuntimeLinearRgba16(
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowPitchBytes);

    [[nodiscard]] RuntimeOutputEvidence AnalyzeRuntimeLinearRgba32(
        const void* pixels,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowPitchBytes);

    struct RuntimeLinearReadbackLayout
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::uint32_t format = 0u;
        std::uint32_t sampleCount = 0u;
    };

    [[nodiscard]] constexpr bool RuntimeLinearReadbackLayoutsMatch(
        RuntimeLinearReadbackLayout current,
        RuntimeLinearReadbackLayout requested) noexcept
    {
        return current.width == requested.width &&
            current.height == requested.height &&
            current.format == requested.format &&
            current.sampleCount == requested.sampleCount;
    }

    struct RetainedRuntimeCase
    {
        std::string name;
        std::vector<std::pair<std::string, std::string>> settings;
        std::uint32_t expectedSampleCount = 0u;
        std::uint64_t expectedPathHistoryCount = 0u;
        bool expectScreenVisibility = false;
        bool expectDirectionalVisibility = false;
        bool expectSkyVisibility = false;
        bool expectFlashlightLightingSubmitted = false;
        bool assertFlashlightLightingState = false;
        bool expectFlashlightVisibility = false;
        bool assertFlashlightVisibilityState = false;
        bool expectShadowDenoising = false;
        bool expectSkyDenoising = false;
        bool expectAmbientOcclusionDenoising = false;
        bool expectGlobalIlluminationDenoising = false;
        bool expectLightingAccumulation = false;
        bool assertLightingAccumulationState = false;
        bool expectAutoExposure = false;
        bool assertAutoExposureState = false;
        bool snapshotRoundTrip = false;
        bool exerciseRetainedStateChanges = false;
        RetainedRuntimeAction action = RetainedRuntimeAction::None;
        bool requireActionOutputDifference = false;
        std::string actionSettingName;
        std::string actionBaselineValue;
        std::string actionBaselineSceneToken;
        std::string actionValue;
        bool requirePathHistoryRestart = false;
        int resizeWidth = 0;
        int resizeHeight = 0;
        std::string expectedSceneToken;
        std::string semanticFamily;
        std::string semanticDomain;
        bool requireCrossCaseDistinctness = false;
        bool requireCrossSampleDistinctness = true;
        bool semanticTimingOnly = false;
    };

    struct RuntimeSemanticSignature
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::uint32_t receiverSampleCount = 0u;
        double meanLinearLuminance = 0.0;
        double rmsLinearLuminance = 0.0;
        double meanLinearHorizontalGradient = 0.0;
        std::array<std::uint64_t, 16> linearLuminanceHistogram{};
        std::uint64_t linearLuminanceSampleCount = 0u;
        double cpuFrameMilliseconds = 0.0;
        double gpuFrameMilliseconds = 0.0;
    };

    struct RetainedRuntimeSemanticCapture
    {
        std::string caseName;
        std::string family;
        std::string domain;
        std::string sceneToken;
        RuntimeSemanticSignature signature;
    };

    [[nodiscard]] RuntimeSemanticSignature
        BuildRuntimeSemanticSignature(
            const RuntimeOutputEvidence& output,
            std::uint32_t receiverSampleCount,
            double cpuFrameMilliseconds,
            double gpuFrameMilliseconds) noexcept;
    [[nodiscard]] bool RuntimeSemanticSignaturesAreDistinct(
        const RuntimeSemanticSignature& left,
        const RuntimeSemanticSignature& right) noexcept;
    [[nodiscard]] bool RuntimeSemanticTimingsAreDistinct(
        const RuntimeSemanticSignature& left,
        const RuntimeSemanticSignature& right) noexcept;
    [[nodiscard]] bool ValidateRetainedRuntimeSemanticCaptures(
        const std::vector<RetainedRuntimeCase>& cases,
        const std::vector<RetainedRuntimeSemanticCapture>& captures,
        std::string& reason);

    [[nodiscard]] std::vector<RetainedRuntimeCase>
        BuildRetainedRuntimeCases(
            const std::string& bistroScene,
            const std::string& sanMiguelScene);

    [[nodiscard]] std::string EscapeRuntimeDiagnosticJson(
        std::string_view value);

    struct RetainedRuntimeTelemetry
    {
        bool sceneBusy = false;
        bool sceneLoaded = false;
        std::string currentScene;
        std::uint32_t receiverSampleCount = 1u;
        std::uint64_t pathHistoryCount = 0u;
        bool screenVisibilityDispatched = false;
        bool directionalVisibilityDispatched = false;
        bool skyVisibilityDispatched = false;
        bool flashlightLightingSubmitted = false;
        bool flashlightVisibilityDispatched = false;
        bool shadowDenoisingDispatched = false;
        bool skyDenoisingDispatched = false;
        bool ambientOcclusionDenoisingDispatched = false;
        bool globalIlluminationDenoisingDispatched = false;
        bool lightingAccumulationCommitted = false;
        bool autoExposureDispatched = false;
        std::string globalNoisePattern;
        std::string globalNoiseResolution;
        bool globalNoiseAnimateSamples = false;
        bool globalNoiseAccumulateSamples = false;
        double cpuFrameMilliseconds = 0.0;
        double gpuFrameMilliseconds = 0.0;
        bool gpuFrameTimingAvailable = false;
        RetainedRuntimeAction lastAppliedAction =
            RetainedRuntimeAction::None;
        std::optional<std::string> settingsSnapshot;
        std::optional<RuntimeOutputEvidence> output;
    };

    struct RetainedRuntimeProvenance
    {
        std::string settingsHash;
        std::string engineVersion;
        std::string sourceCommit;
        std::string sourceIdentity;
        std::string configuration;
        std::string packagePath;
        std::string executablePath;
        std::string executableSha256;
        bool sourceClean = false;
        bool production = false;
        bool debugLayerRequested = false;
        bool nvrhiValidationRequested = false;
    };

    [[nodiscard]] std::string BuildRetainedRuntimeStartJson(
        const RetainedRuntimeProvenance& provenance,
        std::size_t caseCount);
    [[nodiscard]] std::string BuildRetainedRuntimeFailureJson(
        std::string_view caseName,
        std::string_view message);
    [[nodiscard]] std::string BuildRetainedRuntimeCaseJson(
        std::size_t caseIndex,
        const RetainedRuntimeCase& runtimeCase,
        const RetainedRuntimeTelemetry& telemetry);
    [[nodiscard]] std::string BuildRetainedRuntimeCaptureJson(
        std::size_t caseIndex,
        const RetainedRuntimeCase& runtimeCase,
        std::string_view phase,
        const RetainedRuntimeTelemetry& telemetry);
    [[nodiscard]] std::string BuildRetainedRuntimeSummaryJson(
        const RetainedRuntimeProvenance& provenance,
        bool passed,
        std::size_t passedCases,
        std::size_t totalCases,
        std::int64_t elapsedMilliseconds);

    enum class RetainedRuntimeDirectiveKind : std::uint8_t
    {
        Wait,
        ApplyCase,
        ApplyAction,
        ResetSettings,
        RestoreSnapshot,
        CaptureOutput,
        ReportCasePass,
        FinishPass,
        FinishFail
    };

    struct RetainedRuntimeDirective
    {
        RetainedRuntimeDirectiveKind kind =
            RetainedRuntimeDirectiveKind::Wait;
        const RetainedRuntimeCase* runtimeCase = nullptr;
        std::size_t caseIndex = 0u;
        std::string payload;
        RetainedRuntimeAction action = RetainedRuntimeAction::None;
        std::string actionSettingName;
        std::string actionValue;
        int resizeWidth = 0;
        int resizeHeight = 0;
        bool hasStableFrameTiming = false;
        double stableCpuFrameMilliseconds = 0.0;
        double stableGpuFrameMilliseconds = 0.0;
    };

    class RetainedRuntimeDiagnosticState final
    {
    public:
        using Clock = std::chrono::steady_clock;

        RetainedRuntimeDiagnosticState(
            std::vector<RetainedRuntimeCase> cases,
            Clock::time_point start);

        [[nodiscard]] RetainedRuntimeDirective Tick(
            const RetainedRuntimeTelemetry& telemetry,
            Clock::time_point now);
        [[nodiscard]] RetainedRuntimeDirective Abort(
            std::string message,
            Clock::time_point now);
        [[nodiscard]] bool RequiresSettingsSnapshot() const noexcept;
        [[nodiscard]] std::size_t PassedCaseCount() const noexcept;
        [[nodiscard]] std::size_t TotalCaseCount() const noexcept;
        [[nodiscard]] std::int64_t ElapsedMilliseconds(
            Clock::time_point now) const noexcept;

    private:
        enum class Phase : std::uint8_t
        {
            Apply,
            WaitForEvidence,
            WaitForResetFrame,
            WaitForRestoredEvidence,
            WaitForCapture,
            Complete
        };

        [[nodiscard]] bool EvidenceReady(
            const RetainedRuntimeCase& runtimeCase,
            const RetainedRuntimeTelemetry& telemetry,
            bool beforeAction,
            std::string& reason);
        [[nodiscard]] RetainedRuntimeDirective Finish(
            bool passed,
            std::string message);

        std::vector<RetainedRuntimeCase> m_Cases;
        std::size_t m_CaseIndex = 0u;
        std::size_t m_PassedCases = 0u;
        std::uint32_t m_SettledFrames = 0u;
        std::size_t m_CompletedActionCount = 0u;
        RetainedRuntimeAction m_CurrentAction =
            RetainedRuntimeAction::None;
        std::uint64_t m_PathCountBeforeAction = 0u;
        bool m_ObservedPathRestart = false;
        bool m_SnapshotCompleted = false;
        bool m_TimingRecoverySampleUsed = false;
        std::uint64_t m_LastActiveLinearHash = 0u;
        double m_BaselineCpuMilliseconds = 0.0;
        double m_BaselineGpuMilliseconds = 0.0;
        double m_CaptureCpuMilliseconds = 0.0;
        double m_CaptureGpuMilliseconds = 0.0;
        std::string m_SavedSnapshot;
        std::string m_CaptureLabel;
        std::string m_WaitReason;
        std::vector<RetainedRuntimeSemanticCapture> m_SemanticCaptures;
        Clock::time_point m_Start;
        Clock::time_point m_CaseStart;
        Phase m_Phase = Phase::Apply;
    };
}
