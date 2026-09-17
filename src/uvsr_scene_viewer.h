#pragma once

/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <donut/app/DeviceManager.h>
#include "renderer_vector_math.h"
#include "renderer_scene_load_worker.h"
#include "renderer_scene_light.h"
#include "renderer_import_load.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr::gpu_contract { struct Float3; }
namespace uvsr
{
    class CameraController;
    struct RendererView;
    struct UIData;
    struct SceneInitialCamera;
    struct SceneCatalogEntry;
    class SceneCatalog;
    class SceneLoadRequest;
    struct SettingsSnapshotError;
    class WindowsPathText;
    struct WindowsPathTextResult;
    struct NoiseSettings;
    struct RendererTimings;
    struct WorldSpaceRepresentationStatus;
    struct PathTracingCapabilities;
    struct RaySceneView;
    struct RuntimeOutputEvidence;
    struct RetainedRuntimeStorage;
    class CameraCollisionWorld;
    class RendererGeometryPass;
    class RendererShaderFactory;
    class RendererCommonPasses;
    class RendererNvrhiMessageCallback;
    class RendererSceneLoadCancellation;
    enum class CameraMode;
    enum class WhiteWorldMode;
    enum class ToneMappingLut;
    enum class RendererGeometryOutput : std::uint8_t;
    enum class RendererTimingStage : std::uint32_t;
    enum class SelectedLightingTransportState : std::uint8_t;
    enum class PathTracingSceneDomainStatus : std::uint8_t;
    struct RendererSceneState;
    struct RendererSceneLightValues;
    struct RendererSceneMaterialValues;
    struct RendererSceneMaterial;
    struct RendererSceneHandle;
    struct RendererSceneView;
    struct RendererSceneBounds;
    struct RendererLightingState;
    struct RendererFrameState;
}

#if defined(UVSR_BUILD_TESTING)
struct RetainedRuntimeCameraPose
{
    uvsr::gpu_contract::Float3 position{};
    uvsr::gpu_contract::Float3 direction = uvsr::gpu_contract::Float3{0.f, 0.f, -1.f};
    uvsr::gpu_contract::Float3 up = uvsr::gpu_contract::Float3{0.f, 1.f, 0.f};
    uvsr::gpu_contract::Float3 right = uvsr::gpu_contract::Float3{1.f, 0.f, 0.f};
    float verticalFovDegrees = 60.f;
};
#endif

class UvsrSceneViewer : public donut::app::IRenderPass
{
    std::unique_ptr<uvsr::RendererSceneState> m_scene;
    std::unique_ptr<uvsr::RendererLightingState> m_lighting;
    std::unique_ptr<uvsr::RendererFrameState> m_frame;
    uvsr::UIData& m_ui;
    const uvsr::RendererNvrhiMessageCallback& m_nvrhiMessages;
    // worker borrows scene and lighting state; shutdown joins before either dies.
    uvsr::RendererSceneLoadWorker m_sceneLoadWorker;

    void AdvanceRendererTimers();
    void BeginRendererStage(uvsr::RendererTimingStage stage);
    void EndRendererStage(uvsr::RendererTimingStage stage);
    void CompleteRendererTimerFrame();
    void InvalidateRendererStageTiming(uvsr::RendererTimingStage stage);
    enum class PreparationResult { Pending, Complete, Failed };
    bool FailRender(const char* message);
    PreparationResult FailPreparation(const char* message);
    struct FrameExecution;
    [[nodiscard]] bool PrepareFrameTargets(FrameExecution& execution);
    bool PrepareWorldRepresentation(FrameExecution& execution);
    bool PrepareLightingInputs(FrameExecution& execution);
    bool PrepareLightingSchedule(FrameExecution& execution);
    bool RenderPathTracingFrame(FrameExecution& execution);
    bool RenderFrameGeometry(FrameExecution& execution);
    bool RenderRayVisibility(FrameExecution& execution);
    bool RenderDeferredLighting(FrameExecution& execution);
    bool RenderMaterialSelection(FrameExecution& execution);
    bool ResolveSceneLighting(FrameExecution& execution);
    bool RenderFramePresentation(FrameExecution& execution);
    void CompleteSceneFrame(FrameExecution& execution);

public:
    bool ShouldAnimateUnfocused() override;
    bool ShouldRenderUnfocused() override;
    UvsrSceneViewer(donut::app::DeviceManager* deviceManager, uvsr::UIData& ui,
        const uvsr::RendererNvrhiMessageCallback& nvrhiMessages) noexcept;
    // one initialization attempt; the application destroys this owner on failure.
    [[nodiscard]] bool Initialize(std::string_view sceneName, uvsr::SettingsSnapshotError& error);
    ~UvsrSceneViewer() override;
    uvsr::CameraController& GetActiveCamera() const;
    void SetCameraMode(uvsr::CameraMode mode);
    bool ToggleFlashlight();
    void SetFlashlightEnabled(bool enabled, bool invalidateHistory = true);
    void ApplyCameraPose(
        uvsr::gpu_contract::Float3 position, uvsr::gpu_contract::Float3 direction, uvsr::gpu_contract::Float3 up,
        uvsr::gpu_contract::Float3 right, float verticalFovDegrees);
    void ApplySceneInitialCamera(const uvsr::SceneInitialCamera& preset);
    const uvsr::SceneCatalog& GetAvailableScenes() const noexcept;
    // terminated native directory, immutable through the viewer lifetime.
    std::wstring_view GetSceneDir() const noexcept;
    bool SetToneMappingLut(uvsr::ToneMappingLut lut, uvsr::SettingsSnapshotError& error);
    std::string_view GetCurrentSceneName() const noexcept;
    std::string_view GetCurrentSceneDisplayName() const noexcept;
    const uvsr::SceneCatalogEntry* GetCurrentSceneCatalogEntry() const noexcept;
    [[nodiscard]] bool IsSceneLoading() const;
    [[nodiscard]] bool IsSceneLoaded() const;
    void StartPendingSceneLoad();
    // null retries the accepted request. replacement is consumed only after
    // retirement is armed, or collection succeeds when no scene needs retiring.
    [[nodiscard]] bool BeginLoadingScene(uvsr::SceneLoadRequest* replacement,
        uvsr::SettingsSnapshotError& error);
    [[nodiscard]] bool SetCurrentSceneName(std::string_view sceneName,
        uvsr::SettingsSnapshotError& error);
    void RetryCurrentSceneLoad();
    [[nodiscard]] bool HasSceneLoadFailure() const noexcept;
    [[nodiscard]] const char* GetSceneLoadFailure() const noexcept;
    [[nodiscard]] bool ResetFactorySettingsRuntimeState();
    void SynchronizeCameraInput();
    [[nodiscard]] bool BuildCameraCollisionWorld(const uvsr::RendererSceneLoadCancellation& cancellation);
    void StartCameraCollisionPreparation();
    void CompleteCameraActivation();
    bool KeyboardUpdate(int key, int scancode, int action, int mods) override;
    bool MousePosUpdate(double xpos, double ypos) override;
    bool MouseButtonUpdate(int button, int action, int mods) override;
    bool MouseScrollUpdate(double xoffset, double yoffset) override;
    void ResetFlashlightMotion();
    [[nodiscard]] bool ApplyFlashlightPresentation();
    void UpdateFlashlightAnimation(float elapsedSeconds);
    static uvsr::gpu_contract::Float3 ClampFlashlightAimLag(
        uvsr::gpu_contract::Float3 candidate, uvsr::gpu_contract::Float3 target);
    static uvsr::gpu_contract::Float3 InterpolateFlashlightAim(
        uvsr::gpu_contract::Float3 current, uvsr::gpu_contract::Float3 target, float blend);
    void UpdateFlashlightMotion(float elapsedSeconds);
    [[nodiscard]] bool SetSceneLightPose(uvsr::RendererSceneHandle light,
        const uvsr::gpu_contract::Float3* position, const uvsr::RendererSceneLightDirection* direction,
        const uvsr::gpu_contract::Float3* right = nullptr);
    [[nodiscard]] bool ReadSceneLightValues(uvsr::RendererSceneHandle light,
        uvsr::RendererSceneLightValues& output) const;
    [[nodiscard]] bool ReadSceneLightDirection(uvsr::RendererSceneHandle light,
        uvsr::RendererSceneLightDirection& output) const;
    [[nodiscard]] bool SetSceneLightValues(uvsr::RendererSceneHandle light,
        const uvsr::RendererSceneLightValues& candidate);
    [[nodiscard]] bool UpdateFlashlightTransform();
    void Animate(float fElapsedTimeSeconds) override;
    void SceneUnloading();
    bool LoadSceneCandidate(std::string_view fileName,
        const uvsr::RendererSceneLoadCancellation& cancellation);
    [[nodiscard]] bool SceneLoaded();
    [[nodiscard]] bool CompleteSceneActivation();
    [[nodiscard]] bool PrepareCanonicalScene();
    [[nodiscard]] bool SetWhiteWorldMode(uvsr::WhiteWorldMode mode);
    void PointThirdPersonCameraAt(uvsr::RendererSceneHandle node, float distanceScale = 1.f, bool resetOrientation = false);
    void FrameCameraAtBounds(const uvsr::RendererSceneBounds& bounds, float distanceScale, bool resetOrientation);
    uvsr::ImportLoadProgress GetSceneLoadProgress() const noexcept;
    uint32_t GetSceneTexturesReady() const noexcept;
    [[nodiscard]] bool IsSceneBusy() const;
    [[nodiscard]] bool IsSceneGpuUploadPending() const;
    [[nodiscard]] uvsr::RendererSceneView GetSceneView() const;
    [[nodiscard]] const uvsr::RendererSceneMaterial* GetSceneMaterial(uvsr::RendererSceneHandle material) const;
    // borrows the published scene until its replacement or destruction.
    [[nodiscard]] std::string_view GetSceneTexturePath(uint32_t texture) const noexcept;
    [[nodiscard]] bool IsSceneTextureReady(uint32_t texture) const;
    // counted filesystem text, including NULs. failure preserves the prior output.
    [[nodiscard]] bool GetSceneNodePath(uvsr::RendererSceneHandle node,
        uvsr::WindowsPathText& output, uvsr::WindowsPathTextResult& result) const noexcept;
    void SetMaterialDrawerVisible(bool visible);
    [[nodiscard]] bool SetSceneMaterial(uvsr::RendererSceneHandle material, const uvsr::RendererSceneMaterialValues& candidate);
    void NotifyMaterialCommandChanged();
    bool SetupView(bool& topologyChanged);
    [[nodiscard]] bool CreateFastApproximateAAPass();
    [[nodiscard]] std::unique_ptr<uvsr::RendererGeometryPass> CreateGeometryPass(
        uvsr::RendererGeometryOutput output);
    [[nodiscard]] bool RenderGeometry(
        uvsr::RendererGeometryPass& pass, nvrhi::IFramebuffer* framebuffer,
        const uvsr::RendererView* view, const char* marker);
    void BeginRenderPassPreparation(bool waitForIbl);
    [[nodiscard]] PreparationResult ProcessRenderPassPreparationStep();
    [[nodiscard]] bool EnsurePathTracingPass(bool requiredForFrame, bool replaceExisting = false);
    [[nodiscard]] bool EnsureDirectionalRayVisibilityPass();
    [[nodiscard]] bool EnsureRayTracedFlashlightShadowPass();
    [[nodiscard]] bool EnsureRayTracedSkyVisibilityPass();
    void UpdateImageBasedLighting(nvrhi::ICommandList* commandList);
    void RecordLoadingPresentationFrame();
    void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer);
    [[nodiscard]] PreparationResult PrepareLoadingRenderTargets(nvrhi::IFramebuffer* framebuffer);
    void RenderSceneGpuUploadFrame(nvrhi::IFramebuffer* framebuffer);
    void Render(nvrhi::IFramebuffer* framebuffer) override;
    void RenderScene(nvrhi::IFramebuffer* framebuffer);
    uvsr::RendererShaderFactory* GetRendererShaderFactory();
    uvsr::RendererCommonPasses* GetRendererCommonPasses();
    void InvalidateLightingAccumulationHistory();
    void SynchronizeLightingAccumulationHistory(
        uint32_t width, uint32_t height,
        const uvsr::RendererSceneLightRange& submittedLights,
        const uvsr::RaySceneView& rayScene, bool sceneContentChanged,
        const uvsr::NoiseSettings& skyNoiseSettings,
        const uvsr::NoiseSettings& flashlightNoiseSettings,
        bool directionalRayVisibilitySelected,
        bool directionalRayVisibilityReady, bool rayTracedFlashlightShadowSelected,
        bool flashlightStochasticRequested, bool rayTracedFlashlightShadowReady,
        bool rayTracedSkyVisibilitySelected, bool skyVisibilityStochasticRequested,
        bool rayTracedSkyVisibilityReady);
    void ResetImageBasedLightingHistory();
    void ResetNoiseSamplingHistory(bool shadows, bool skyVisibility, bool flashlight);
    [[nodiscard]] uint64_t GetNoiseTextureResidentBytes() const;
#if defined(UVSR_BUILD_TESTING)
    [[nodiscard]] uint64_t GetLightingHistoryEpochForRuntimeDiagnostic() const noexcept;
    void SeedNoiseSamplingPhasesForRuntimeDiagnostic() noexcept;
    [[nodiscard]] std::array<uint64_t, 2> GetNoiseSamplingPhasesForRuntimeDiagnostic() const noexcept;
    void ClearShaderReloadRequestForRuntimeDiagnostic() noexcept;
    [[nodiscard]] bool IsShaderReloadRequestedForRuntimeDiagnostic() const noexcept;
    // false means path preparation failed before publishing a new request.
    [[nodiscard]] bool RequestRuntimeOutputEvidence(size_t caseIndex,
        std::string_view caseName, std::string_view phase);
    [[nodiscard]] std::optional<uvsr::RuntimeOutputEvidence> ConsumeRuntimeOutputEvidence();
    void NudgeCameraForRuntimeDiagnostic();
    [[nodiscard]] RetainedRuntimeCameraPose CaptureRetainedRuntimeCameraPose() const;
    [[nodiscard]] uvsr::RetainedRuntimeStorage CaptureRetainedRuntimeStorage() const;
    void RestoreRetainedRuntimeCameraPose(const RetainedRuntimeCameraPose& pose);
#endif
    bool HasPrimaryDirectionalLight() const;
    bool HasDirectionalRayVisibilityHardwareSupport() const;
    bool SupportsDirectionalRayVisibility() const;
    bool HasRayTracedFlashlightShadowHardwareSupport() const;
    bool HasRayTracedSkyVisibilityHardwareSupport() const;
    bool SupportsRayTracedSkyVisibility() const;
    const uvsr::WorldSpaceRepresentationStatus& GetWorldSpaceRepresentationStatus() const;
    const uvsr::PathTracingCapabilities& GetPathTracingCapabilities() const;
    uint64_t GetPathTracingCenterPixelAcceptedSampleCount() const noexcept;
#if defined(UVSR_BUILD_TESTING)
    uint64_t GetPathTracingHistoryGeneration() const noexcept;
#endif
    uvsr::SelectedLightingTransportState GetSelectedLightingTransportState() const noexcept;
    uvsr::PathTracingSceneDomainStatus GetPathTracingSceneDomainStatus() const;
    uvsr::RendererSceneHandle GetPrimaryDirectionalLight() const;
    uvsr::RendererSceneLightRange GetEditableLights() const;
    const uvsr::RendererSceneLight* GetSceneLight(uvsr::RendererSceneHandle light) const;
    std::string GetSceneLightName(uvsr::RendererSceneHandle light) const;
    // counted scene text remains valid until the scene is replaced.
    std::string_view GetSceneLightNameView(uvsr::RendererSceneHandle light) const noexcept;
    bool IsFlashlight(uvsr::RendererSceneHandle light) const;
    [[nodiscard]] uint64_t GetSubmittedMainViewTriangles() const;
    [[nodiscard]] const uvsr::RendererTimings& GetRendererTimings() const;
    [[nodiscard]] bool DidDispatchDirectionalRayVisibilityThisFrame() const;
#if defined(UVSR_BUILD_TESTING)
    [[nodiscard]] bool DidDispatchRayTracedFlashlightShadowThisFrame() const;
    [[nodiscard]] bool DidSubmitFlashlightLightingThisFrame() const;
    [[nodiscard]] bool DidCommitLightingAccumulationThisFrame() const;
#endif
    [[nodiscard]] bool DidDispatchRayTracedSkyVisibilityThisFrame() const;
    [[nodiscard]] bool IsRendererStageActiveThisFrame(uvsr::RendererTimingStage stage) const;
};
