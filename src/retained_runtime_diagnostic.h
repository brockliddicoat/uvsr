#pragma once

#include "retained_runtime_message.h"
#include "retained_runtime_provenance.h"
#include "retained_runtime_case_storage.h"

#include "uvsr_settings_commands.h"
#include "retained_runtime_semantic.h"
#include "windows_path_text.h"

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

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
        CycleLightingSolution,
        CyclePrerequisite
    };

    [[nodiscard]] inline const char* RetainedRuntimeCaptureLabel(
        RetainedRuntimeAction action) noexcept
    {
        switch (action)
        {
        case RetainedRuntimeAction::None: return "baseline";
        case RetainedRuntimeAction::NudgeCamera: return "camera";
        case RetainedRuntimeAction::ResizeViewport: return "resize";
        case RetainedRuntimeAction::ChangeScene: return "scene";
        case RetainedRuntimeAction::ChangeSetting: return "reference";
        case RetainedRuntimeAction::ChangeMaterial: return "material";
        case RetainedRuntimeAction::ChangeLight: return "light";
        case RetainedRuntimeAction::ToggleFlashlight: return "flashlight";
        case RetainedRuntimeAction::CyclePrerequisite: return "prerequisite-cycle";
        case RetainedRuntimeAction::CycleLightingSolution:
            return "lighting-solution";
        }
        return "invalid";
    }

    struct RuntimeOutputEvidence
    {
        bool valid = false;
        bool deterministicCapture = false;
        std::uint32_t capturedPathDispatchCount = 0u;
        bool capturedSkySamplePhaseValid = false;
        std::uint32_t capturedSkySamplePhase = 0u;
        std::uint32_t capturedRasterProducerMask = 0u;
        std::uint32_t capturedDirectionalSamplePhase = 0u;
        std::uint32_t capturedFlashlightSamplePhase = 0u;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::uint64_t encodedBytes = 0u;
        std::uint64_t pixelBytes = 0u;
        std::uint64_t pixelHash = 1469598103934665603ull;
        unsigned char minimumByte = 0u;
        unsigned char maximumByte = 0u;
        WindowsPathText artifactPath;
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

    struct RetainedRuntimeSetting
    {
        SettingId id = SettingId::Invalid;
        UiSettingsValue value;
    };

    struct RetainedRuntimeCase
    {
        using Setting = RetainedRuntimeSetting;
        SettingsSnapshotText name;
        RetainedRuntimeList<Setting> settings;
        std::uint64_t expectedPathHistoryCount = 0u;
        bool expectDirectionalVisibility = false;
        bool expectSkyVisibility = false;
        bool expectFlashlightLightingSubmitted = false;
        bool assertFlashlightLightingState = false;
        bool expectFlashlightVisibility = false;
        bool assertFlashlightVisibilityState = false;
        bool expectLightingAccumulation = false;
        bool assertLightingAccumulationState = false;
        bool expectAutoExposure = false;
        bool assertAutoExposureState = false;
        bool snapshotRoundTrip = false;
        bool expectSnapshotResetChange = true;
        bool exerciseRetainedStateChanges = false;
        RetainedRuntimeAction action = RetainedRuntimeAction::None;
        bool requireActionOutputDifference = false;
        SettingId actionSettingId = SettingId::Invalid;
        UiSettingsValue actionBaselineValue;
        SettingsSnapshotText actionBaselineSceneToken;
        UiSettingsValue actionValue;
        bool requirePathHistoryRestart = false;
        int resizeWidth = 0;
        int resizeHeight = 0;
        SettingsSnapshotText expectedSceneToken;
    };

    using RetainedRuntimeCases = RetainedRuntimeList<RetainedRuntimeCase>;

    [[nodiscard]] bool BuildRetainedRuntimeCases(
            std::string_view bistroScene,
            std::string_view sanMiguelScene,
            RetainedRuntimeCases& output,
            SettingsSnapshotError& error) noexcept;

    // counts sampled only from the published scene. these are owner capacities,
    // not allocator calls, resident VRAM or native driver memory.
    struct RetainedRuntimeStorage
    {
        std::uint64_t collisionTriangles = 0, collisionTriangleCapacity = 0;
        std::uint64_t collisionNodes = 0, collisionNodeCapacity = 0;
        std::uint64_t sceneMeshes = 0, sceneMaterials = 0, sceneInstances = 0;
        std::uint64_t sceneGeometries = 0, sceneGeometryInstances = 0;
        std::uint64_t materialBackupCount = 0, materialBackupCapacity = 0;
        std::uint64_t editableLightCapacity = 0, unmountedLightCapacity = 0;
        std::uint64_t descriptorCapacity = 0, targetHeapBytes = 0;
        std::uint64_t descriptorLive = 0, descriptorPeakLive = 0, descriptorPeakCapacity = 0;
        std::uint64_t textureQueuePeak = 0, texturesRequested = 0, texturesLoaded = 0, texturesFinalized = 0;
        std::uint64_t pbrBindingPeak = 0, pathLightCapacity = 0;
        std::uint64_t materialBufferBytes = 0, geometryBufferBytes = 0, instanceBufferBytes = 0;
        std::uint64_t retainedSourceArrays = 0;
    };

    // text borrows producer storage through Tick and synchronous record encoding.
    struct RetainedRuntimeTelemetry
    {
        bool sceneBusy = false;
        bool sceneLoaded = false;
        std::string_view currentScene;
        std::uint64_t pathHistoryCount = 0u;
        std::uint64_t pathHistoryGeneration = 0u;
        bool directionalVisibilityDispatched = false;
        bool skyVisibilityDispatched = false;
        bool flashlightLightingSubmitted = false;
        bool flashlightVisibilityDispatched = false;
        bool lightingAccumulationCommitted = false;
        bool autoExposureDispatched = false;
        std::string_view globalNoisePattern;
        std::string_view globalNoiseResolution;
        bool globalNoiseAnimateSamples = false;
        bool globalNoiseAccumulateSamples = false;
        double cpuFrameMilliseconds = 0.0;
        double gpuFrameMilliseconds = 0.0;
        bool gpuFrameTimingAvailable = false;
        RetainedRuntimeAction lastAppliedAction =
            RetainedRuntimeAction::None;
        std::optional<std::string_view> settingsSnapshot;
        std::optional<RuntimeOutputEvidence> output;
        RetainedRuntimeStorage storage;
    };

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
        // borrowed from the diagnostic state's immutable case storage.
        const RetainedRuntimeCase* runtimeCase = nullptr;
        std::size_t caseIndex = 0u;
        // snapshot storage belongs to the state. labels have static storage.
        std::string_view snapshot;
        std::string_view captureLabel;
        RetainedRuntimeMessage failure;
        RetainedRuntimeAction action = RetainedRuntimeAction::None;
        SettingId actionSettingId = SettingId::Invalid;
        int resizeWidth = 0;
        int resizeHeight = 0;
        bool hasStableFrameTiming = false;
        double stableCpuFrameMilliseconds = 0.0;
        double stableGpuFrameMilliseconds = 0.0;
        RetainedRuntimeSemanticResult semanticFailure;
    };

    class RetainedRuntimeDiagnosticState final
    {
    public:
        using Clock = std::chrono::steady_clock;

        RetainedRuntimeDiagnosticState(
            RetainedRuntimeCases cases,
            Clock::time_point start) noexcept;

        [[nodiscard]] RetainedRuntimeDirective Tick(
            const RetainedRuntimeTelemetry& telemetry,
            Clock::time_point now) noexcept;
        [[nodiscard]] RetainedRuntimeDirective Abort(
            RetainedRuntimeMessage message,
            Clock::time_point now) noexcept;
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
            RetainedRuntimeMessage& reason) noexcept;
        [[nodiscard]] RetainedRuntimeDirective Finish(
            bool passed,
            RetainedRuntimeMessage message) noexcept;

        RetainedRuntimeCases m_Cases;
        std::size_t m_CaseIndex = 0u;
        std::size_t m_PassedCases = 0u;
        std::uint32_t m_SettledFrames = 0u;
        std::size_t m_CompletedActionCount = 0u;
        RetainedRuntimeAction m_CurrentAction =
            RetainedRuntimeAction::None;
        std::uint64_t m_PathGenerationBeforeAction = 0u;
        bool m_ObservedPathRestart = false;
        bool m_SnapshotCompleted = false;
        bool m_TimingRecoverySampleUsed = false;
        std::uint64_t m_LastActiveLinearHash = 0u;
        double m_BaselineCpuMilliseconds = 0.0;
        double m_BaselineGpuMilliseconds = 0.0;
        double m_CaptureCpuMilliseconds = 0.0;
        double m_CaptureGpuMilliseconds = 0.0;
        SettingsSnapshotText m_SavedSnapshot;
        RetainedRuntimeMessage m_WaitReason;
        RetainedRuntimeSemanticSummary m_SemanticSummary;
        Clock::time_point m_Start;
        Clock::time_point m_CaseStart;
        Phase m_Phase = Phase::Apply;
    };
}
