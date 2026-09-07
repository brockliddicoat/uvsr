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

#include <donut/app/ApplicationBase.h>
#include <donut/core/math/math.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace donut::app { class BaseCamera; }
namespace donut::engine
{
    class Scene;
    class Material;
    class Light;
    class DirectionalLight;
    class SpotLight;
    class SceneGraphNode;
    class ShaderFactory;
    class TextureCache;
    class IView;
}
namespace uvsr
{
    struct UIData;
    struct SceneInitialCamera;
    struct SceneCatalogEntry;
    struct NoiseSettings;
    struct RendererTimings;
    struct WorldSpaceRepresentationStatus;
    struct PathTracingCapabilities;
    struct RaySceneView;
    struct RuntimeOutputEvidence;
    class CameraCollisionWorld;
    class RendererGeometryPass;
    class RendererShaderFactory;
    class RendererCommonPasses;
    enum class CameraMode;
    enum class WhiteWorldMode;
    enum class ToneMappingLut;
    enum class RendererGeometryOutput : std::uint8_t;
    enum class RendererTimingStage : std::uint32_t;
    enum class SelectedLightingTransportState : std::uint8_t;
    enum class PathTracingSceneDomainStatus : std::uint8_t;
    struct RendererSceneState;
    struct RendererLightingState;
    struct RendererFrameState;
}

#if defined(UVSR_BUILD_TESTING)
struct RetainedRuntimeCameraPose
{
    donut::math::float3 position = 0.f;
    donut::math::float3 direction = donut::math::float3(0.f, 0.f, -1.f);
    donut::math::float3 up = donut::math::float3(0.f, 1.f, 0.f);
    donut::math::float3 right = donut::math::float3(1.f, 0.f, 0.f);
    float verticalFovDegrees = 60.f;
};
#endif

class UvsrSceneViewer : public donut::app::ApplicationBase
{
    using Super = donut::app::ApplicationBase;
    std::unique_ptr<uvsr::RendererSceneState> m_scene;
    std::unique_ptr<uvsr::RendererLightingState> m_lighting;
    std::unique_ptr<uvsr::RendererFrameState> m_frame;
    uvsr::UIData& m_ui;

    void AdvanceRendererTimers();
    void BeginRendererStage(uvsr::RendererTimingStage stage);
    void EndRendererStage(uvsr::RendererTimingStage stage);
    void CompleteRendererTimerFrame();
    void InvalidateRendererStageTiming(uvsr::RendererTimingStage stage);
    struct FrameExecution;
    void PrepareFrameTargets(FrameExecution& execution);
    bool PrepareWorldRepresentation(FrameExecution& execution);
    bool PrepareLightingInputs(FrameExecution& execution);
    bool PrepareLightingSchedule(FrameExecution& execution);
    bool RenderPathTracingFrame(FrameExecution& execution);
    bool RenderRayVisibility(FrameExecution& execution);
    bool RenderDeferredLighting(FrameExecution& execution);
    bool RenderMaterialSelection(FrameExecution& execution);
    bool ResolveSceneLighting(FrameExecution& execution);
    bool RenderFramePresentation(FrameExecution& execution);
    void CompleteSceneFrame(FrameExecution& execution);

public:
    bool ShouldAnimateUnfocused() override;
    bool ShouldRenderUnfocused() override;
    UvsrSceneViewer(donut::app::DeviceManager* deviceManager, uvsr::UIData& ui, const std::string& sceneName);
    ~UvsrSceneViewer() override;
    std::shared_ptr<donut::vfs::IFileSystem> GetRootFs() const;
    donut::app::BaseCamera& GetActiveCamera() const;
    void SetCameraMode(uvsr::CameraMode mode);
    bool ToggleFlashlight();
    void SetFlashlightEnabled(bool enabled, bool invalidateHistory = true);
    void ApplyCameraPose(
        donut::math::float3 position, donut::math::float3 direction, donut::math::float3 up,
        donut::math::float3 right, float verticalFovDegrees);
    void ApplySceneInitialCamera(const uvsr::SceneInitialCamera& preset);
    const std::vector<uvsr::SceneCatalogEntry>& GetAvailableScenes() const;
    std::filesystem::path const& GetSceneDir() const;
    bool SetToneMappingLut(uvsr::ToneMappingLut lut, std::string& error);
    std::string GetCurrentSceneName() const;
    std::string GetCurrentSceneDisplayName() const;
    [[nodiscard]] bool IsSceneLoading() const;
    [[nodiscard]] bool IsSceneLoaded() const;
    void StartPendingSceneLoad();
    void BeginLoadingScene(
        std::shared_ptr<donut::vfs::IFileSystem> fileSystem, const std::filesystem::path& sceneFileName) override;
    void SetCurrentSceneName(const std::string& sceneName);
    void RetryCurrentSceneLoad();
    [[nodiscard]] bool HasSceneLoadFailure() const noexcept;
    [[nodiscard]] const std::string& GetSceneLoadFailure() const noexcept;
    void ResetFactorySettingsRuntimeState();
    void SynchronizeCameraInput();
    static uvsr::CameraCollisionWorld BuildCameraCollisionWorld(
        const donut::engine::Scene& scene, float collisionRadius);
    bool KeyboardUpdate(int key, int scancode, int action, int mods) override;
    bool MousePosUpdate(double xpos, double ypos) override;
    bool MouseButtonUpdate(int button, int action, int mods) override;
    bool MouseScrollUpdate(double xoffset, double yoffset) override;
    void ResetFlashlightMotion();
    void ApplyFlashlightPresentation();
    void UpdateFlashlightAnimation(float elapsedSeconds);
    static donut::math::float3 ClampFlashlightAimLag(
        donut::math::float3 candidate, donut::math::float3 target);
    static donut::math::float3 InterpolateFlashlightAim(
        donut::math::float3 current, donut::math::float3 target, float blend);
    void UpdateFlashlightMotion(float elapsedSeconds);
    static void SetFlashlightDirectionAndRoll(
        const std::shared_ptr<donut::engine::SpotLight>& light, const donut::math::float3& direction,
        const donut::math::float3& right);
    void UpdateFlashlightTransform();
    void AttachFlashlightToScene();
    void Animate(float fElapsedTimeSeconds) override;
    void SceneUnloading() override;
    bool LoadScene(std::shared_ptr<donut::vfs::IFileSystem> fs, const std::filesystem::path& fileName) override;
    void SceneLoaded() override;
    void CompleteSceneActivation();
    void SetWhiteWorldMode(uvsr::WhiteWorldMode mode);
    static std::shared_ptr<donut::engine::SceneGraphNode> FindDescendantByName(
        const std::shared_ptr<donut::engine::SceneGraphNode>& node, const std::string& name);
    void PointThirdPersonCameraAt(
        const std::shared_ptr<donut::engine::SceneGraphNode>& node, float distanceScale = 1.f,
        bool resetOrientation = false);
    std::shared_ptr<donut::engine::TextureCache> GetTextureCache();
    [[nodiscard]] bool IsSceneBusy() const;
    [[nodiscard]] bool IsSceneGpuUploadPending() const;
    std::shared_ptr<donut::engine::Scene> GetScene();
    void SetMaterialDrawerVisible(bool visible);
    const donut::engine::Material* GetOriginalMaterial(
        const std::shared_ptr<donut::engine::Material>& material) const;
    void NotifyMaterialCommandChanged(const std::shared_ptr<donut::engine::Material>& material);
    bool SetupView();
    void CreateFastApproximateAAPass();
    [[nodiscard]] std::unique_ptr<uvsr::RendererGeometryPass> CreateGeometryPass(
        uvsr::RendererGeometryOutput output);
    [[nodiscard]] bool RenderGeometry(
        uvsr::RendererGeometryPass& pass, nvrhi::IFramebuffer* framebuffer,
        const donut::engine::IView* view, const char* marker);
    void BeginRenderPassPreparation(bool waitForIbl);
    bool ProcessRenderPassPreparationStep();
    void EnsurePathTracingPass();
    void EnsureDirectionalRayVisibilityPass();
    void EnsureRayTracedFlashlightShadowPass();
    void EnsureRayTracedSkyVisibilityPass();
    void UpdateImageBasedLighting(nvrhi::ICommandList* commandList);
    void RecordLoadingPresentationFrame();
    void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override;
    bool PrepareLoadingRenderTargets(nvrhi::IFramebuffer* framebuffer);
    void RenderSceneGpuUploadFrame(nvrhi::IFramebuffer* framebuffer);
    void Render(nvrhi::IFramebuffer* framebuffer) override;
    void RenderScene(nvrhi::IFramebuffer* framebuffer) override;
    std::shared_ptr<donut::engine::ShaderFactory> GetShaderFactory();
    std::shared_ptr<uvsr::RendererShaderFactory> GetRendererShaderFactory();
    std::shared_ptr<uvsr::RendererCommonPasses> GetRendererCommonPasses();
    void InvalidateLightingAccumulationHistory();
    void SynchronizeLightingAccumulationHistory(
        uint32_t width, uint32_t height,
        const std::vector<std::shared_ptr<donut::engine::Light>>& submittedLights,
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
    void RequestRuntimeOutputEvidence(size_t caseIndex, std::string_view caseName);
    [[nodiscard]] std::optional<uvsr::RuntimeOutputEvidence> ConsumeRuntimeOutputEvidence();
    void NudgeCameraForRuntimeDiagnostic();
    [[nodiscard]] RetainedRuntimeCameraPose CaptureRetainedRuntimeCameraPose() const;
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
    uvsr::SelectedLightingTransportState GetSelectedLightingTransportState() const noexcept;
    uvsr::PathTracingSceneDomainStatus GetPathTracingSceneDomainStatus() const;
    std::shared_ptr<donut::engine::DirectionalLight> GetPrimaryDirectionalLight() const;
    const std::vector<std::shared_ptr<donut::engine::Light>>& GetEditableLights() const;
    bool IsFlashlight(const std::shared_ptr<donut::engine::Light>& light) const;
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
