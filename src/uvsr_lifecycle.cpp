#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene_nvrhi.h"
#include "uvsr_renderer_lighting_nvrhi.h"
#include "uvsr_renderer_frame_nvrhi.h"
#include "uvsr_runtime.h"
#include "uvsr_command_line.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include "settings_snapshot_storage.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <stdio.h>
#include <utility>
#include <new>
#include "gpu_capabilities.h"
#include "windows_executable_path.h"
#include "retained_scene_paths.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <Windows.h>
#include <cstring>
#if defined(UVSR_BUILD_TESTING)
#include "retained_runtime_capture_file.h"
#endif

using namespace donut;
using namespace donut::app;
using namespace uvsr;

bool RestartCurrentProcess()
{
    // ParseUvsrCommandLine applies options from left to right. appending the
    // requested adapter overrides an earlier option without rewriting arguments.
    RestartCommandLine mutableCommandLine;
    RestartCommandLineError error;
    if (!mutableCommandLine.Prepare(GetCommandLineW(), g_RestartAdapterIndex, error))
    {
        uvsr::log::error("Failed to prepare UVSR restart command line (error %u)", unsigned(error));
        return false;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommandLine.Data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created)
    {
        uvsr::log::error("Failed to restart UVSR (Win32 error %lu)", GetLastError());
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}


auto UvsrSceneViewer::ShouldAnimateUnfocused() -> bool {
#if defined(UVSR_BUILD_TESTING)
        return g_VerifySettingsContractRequested ||
            g_VerifyRetainedRuntimeRequested;
#else
        return false;
#endif
    }

auto UvsrSceneViewer::ShouldRenderUnfocused() -> bool {
#if defined(UVSR_BUILD_TESTING)
        return g_VerifySettingsContractRequested ||
            g_VerifyRetainedRuntimeRequested;
#else
        return false;
#endif
    }

UvsrSceneViewer::UvsrSceneViewer(DeviceManager* deviceManager, UIData& ui,
        const RendererNvrhiMessageCallback& nvrhiMessages) noexcept
        : donut::app::IRenderPass(deviceManager), m_ui(ui), m_nvrhiMessages(nvrhiMessages) {}

auto UvsrSceneViewer::Initialize(std::string_view sceneName, SettingsSnapshotError& error) -> bool {
        error = {};
        if (m_scene || m_lighting || m_frame || !GetDeviceManager() || !GetDevice())
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Invalid renderer initialization state.", {}};
            return false;
        }
        const auto allocationFailure = [&error](const char* message) {
            error = {SettingsSnapshotErrorCode::OutOfMemory, 0, 0, message, {}};
            return false;
        };
        const auto resourceFailure = [&error](const char* message) {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, message, {}};
            return false;
        };
        m_scene.reset(new (std::nothrow) RendererSceneState(GetDevice()));
        if (!m_scene) return allocationFailure("Could not allocate renderer scene state.");
        m_lighting.reset(new (std::nothrow) RendererLightingState());
        if (!m_lighting) return allocationFailure("Could not allocate renderer lighting state.");
        m_frame.reset(new (std::nothrow) RendererFrameState());
        if (!m_frame) return allocationFailure("Could not allocate renderer frame state.");
        WindowsPath directory;
        WindowsPathResult pathResult;
        const auto pathFailure = [&error, &pathResult](const char* message) {
            error = {pathResult.error == WindowsPathError::Allocation ? SettingsSnapshotErrorCode::OutOfMemory :
                SettingsSnapshotErrorCode::Path, pathResult.nativeCode, 0, message, {}};
            return false;
        };
        if (!GetExecutableDirectoryWide(directory, pathResult))
            return pathFailure("UVSR could not identify its executable path.");
        WindowsPath installationDirectory, mediaDir;
        if (!ExecutableDirectoryFromModulePath(directory.Data(), directory.Size(), installationDirectory, pathResult) ||
            !JoinWindowsRelativePath(installationDirectory.Data(), L"media", mediaDir, pathResult))
            return pathFailure("UVSR could not prepare its packaged media path.");
        WindowsPath appShaderDir;
        if (!JoinWindowsRelativePath(directory.Data(), L"shaders/uvsr/dxil", appShaderDir, pathResult))
            return pathFailure("UVSR could not prepare its packaged shader path.");
        m_frame->rendererShaderFactory.reset(new (std::nothrow) RendererShaderFactory(
            GetDevice(), appShaderDir.Data()));
        if (!m_frame->rendererShaderFactory) return allocationFailure("Could not allocate the renderer shader factory.");
        m_frame->rendererCommonPasses.reset(new (std::nothrow) RendererCommonPasses(
            GetDevice(), m_frame->rendererShaderFactory.get()));
        if (!m_frame->rendererCommonPasses) return allocationFailure("Could not allocate renderer common resources.");
        if (!m_frame->rendererCommonPasses->IsValid())
        {
            return resourceFailure("UVSR common renderer resources failed to initialize");
        }

        if (!JoinWindowsRelativePath(mediaDir.Data(), L"glTF-Sample-Assets/Models/", m_scene->sceneDir, pathResult))
            return pathFailure("UVSR could not prepare its retained scene directory.");
        WindowsPath retainedSceneDescriptors[RetainedSceneFileCount];
        for (size_t index = 0; index < RetainedSceneFileCount; ++index)
            if (!JoinWindowsRelativePath(m_scene->sceneDir.Data(), RetainedSceneRelativePaths[index],
                    retainedSceneDescriptors[index], pathResult))
                return pathFailure("UVSR could not prepare a retained scene path.");
        WindowsPathText retainedSceneFiles[RetainedSceneFileCount];
        std::string_view retainedSceneViews[RetainedSceneFileCount];
        SettingsSnapshotError& sceneError = error;
        for (size_t index = 0; index < RetainedSceneFileCount; ++index)
        {
            const auto& descriptor = retainedSceneDescriptors[index];
            if (!ValidateRetainedSceneFile(descriptor.Data(), descriptor.Size(), sceneError) ||
                !PrepareRetainedSceneName(descriptor.Data(), descriptor.Size(), retainedSceneFiles[index], sceneError))
                return false;
            retainedSceneViews[index] = {retainedSceneFiles[index].Data(), retainedSceneFiles[index].Size()};
        }
        if (!BuildSceneCatalog({m_scene->sceneDir.Data(), m_scene->sceneDir.Size()},
                {retainedSceneViews, RetainedSceneFileCount}, m_scene->sceneCatalog, sceneError))
        {
            error = ComposeSettingsSnapshotError({"Could not prepare retained scenes: ", error.MessageView()},
                error.code, error.nativeCode, error.cleanupCode);
            return false;
        }

        if (m_scene->sceneCatalog.Count() != RetainedSceneFileCount)
        {
            char count[32];
            const int length = snprintf(count, sizeof(count), "%zu", m_scene->sceneCatalog.Count());
            if (length <= 0 || size_t(length) >= sizeof(count))
                return resourceFailure("Could not format the retained scene count.");
            error = ComposeSettingsSnapshotError({"The retained scene catalog must resolve exactly Bistro and "
                "San Miguel; resolved ", {count, size_t(length)}, " entries"});
            return false;
        }

        const auto activeAdapter = std::find_if(
            m_ui.GpuAdapterChoices.begin(),
            m_ui.GpuAdapterChoices.end(),
            [this](const GpuAdapterChoice& adapter)
            {
                return adapter.adapterIndex ==
                    m_ui.ActiveGpuAdapterIndex;
            });
        const uint32_t resourceBindingTier =
            activeAdapter != m_ui.GpuAdapterChoices.end()
                ? activeAdapter->resourceBindingTier
                : 0u;
        if (SupportsBindlessResourceTables(resourceBindingTier))
        {
            nvrhi::BindlessLayoutDesc bindlessLayoutDescription;
            bindlessLayoutDescription.visibility =
                nvrhi::ShaderType::Compute;
            bindlessLayoutDescription.firstSlot = 0u;
            bindlessLayoutDescription.maxCapacity = 65536u;
            bindlessLayoutDescription.registerSpaces = {
                nvrhi::BindingLayoutItem::RawBuffer_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2)
            };
            m_scene->bindlessLayout = GetDevice()->createBindlessLayout(
                bindlessLayoutDescription);
        }
        if (m_scene->bindlessLayout)
        {
            m_scene->descriptorTable.reset(new (std::nothrow) RendererSceneDescriptorsNvrhi(
                GetDevice(), m_scene->bindlessLayout));
            if (!m_scene->descriptorTable) return allocationFailure("Could not allocate scene descriptors.");
            if (!m_scene->descriptorTable->IsValid())
                return resourceFailure("Scene descriptor table creation failed");
        }
        else
        {
            uvsr::log::warning(
                "Bindless scene resources require D3D12 Resource Binding "
                "Tier 2; this adapter reports tier %u. Ray-query effects "
                "are disabled while the Shader Model 6.5 baseline remains "
                "available",
                resourceBindingTier);
        }
        WindowsPath environmentDirectory;
        if (!JoinWindowsRelativePath(mediaDir.Data(), L"environments", environmentDirectory, pathResult))
            return pathFailure("UVSR could not prepare its environment asset path.");
        m_lighting->imageBasedLightingEnvironment.reset(new (std::nothrow) ImageBasedLightingEnvironment(
            GetDevice(), m_frame->rendererShaderFactory.get(), m_frame->rendererCommonPasses.get(),
            static_cast<WindowsPath&&>(environmentDirectory)));
        if (!m_lighting->imageBasedLightingEnvironment) return allocationFailure("Could not allocate the IBL environment.");
        WindowsPath noiseDirectory;
        if (!JoinWindowsRelativePath(mediaDir.Data(), L"uvsr/noise", noiseDirectory, pathResult))
            return pathFailure("UVSR could not prepare its noise asset path.");
        m_lighting->noiseTextureLibrary.reset(new (std::nothrow) NoiseTextureLibrary(
            GetDevice(), static_cast<WindowsPath&&>(noiseDirectory)));
        if (!m_lighting->noiseTextureLibrary) return allocationFailure("Could not allocate the noise texture library.");
        if (!JoinWindowsRelativePath(mediaDir.Data(), L"luts/kodak", m_frame->toneMappingLutDirectory, pathResult))
            return pathFailure("UVSR could not prepare its film LUT directory.");



        m_frame->commandList = GetDevice()->createCommandList();
        if (!m_frame->commandList) return resourceFailure("Could not create the renderer command list.");
        m_scene->worldSpaceRepresentation.reset(new (std::nothrow) WorldSpaceRepresentation(GetDevice()));
        if (!m_scene->worldSpaceRepresentation) return allocationFailure("Could not allocate the world-space representation.");
        for (auto& stageQueries : m_frame->rendererTimerQueries)
        {
            for (nvrhi::TimerQueryHandle& query : stageQueries)
            {
                query = GetDevice()->createTimerQuery();
                if (!query) return resourceFailure("Could not create renderer timing queries.");
            }
        }

        if (sceneName.empty())
        {
            // Prefer the smaller retained scene as the startup fallback. This
            // ordering is not evidence that it is more runtime-reliable.
            const std::string_view defaultScene = retainedSceneViews[0];
            const SceneCatalogEntry* entry = nullptr;
            if (!FindSceneCatalogEntry(m_scene->sceneCatalog, defaultScene, entry, sceneError))
            {
                error = ComposeSettingsSnapshotError({"Could not resolve the startup scene: ", error.MessageView()},
                    error.code, error.nativeCode, error.cleanupCode);
                return false;
            }
            if (!entry)
            {
                entry = &m_scene->sceneCatalog[0];
                uvsr::log::warning(
                    "Default Bistro descriptor '%s' was not found; loading '%s' instead.",
                    defaultScene.data(), entry->FileName.data());
            }
            if (!SetCurrentSceneName(entry->FileName, sceneError))
            {
                error = ComposeSettingsSnapshotError({"Could not select the startup scene: ", error.MessageView()},
                    error.code, error.nativeCode, error.cleanupCode);
                return false;
            }
        }
        else if (!SetCurrentSceneName(sceneName, sceneError))
        {
            error = ComposeSettingsSnapshotError({"Could not select the startup scene: ", error.MessageView()},
                error.code, error.nativeCode, error.cleanupCode);
            return false;
        }
        return true;
    }

UvsrSceneViewer::~UvsrSceneViewer() {
        // The task executes this derived class's LoadScene. Join it before
        // any state captured through `this` can be destroyed.
        m_sceneLoadWorker.Reset();
        if (!m_scene || !m_lighting || !m_frame) return;
        if (m_scene->hasRendererSceneResources && !m_scene->sceneRetirementPending)
        {
            if (!m_scene->sceneRetirement.Begin())
                uvsr::log::fatal("UVSR could not arm scene GPU retirement during shutdown");
            m_scene->sceneRetirementPending = true;
        }
        if (m_scene->sceneRetirementPending &&
            m_scene->sceneRetirement.CompleteBlocking() !=
                RendererSceneRetirementStatus::Ready)
        {
            uvsr::log::fatal(
                "UVSR could not prove scene GPU retirement during shutdown");
        }
        SceneUnloading();
        // technique borrows expire before their frame-owned common resources.
        m_lighting.reset();
        m_scene.reset();
        m_frame.reset();
    }

auto UvsrSceneViewer::GetActiveCamera() const -> CameraController& {
        switch (m_ui.Camera)
        {
        case CameraMode::FirstPerson: return m_scene->firstPersonCamera;
        case CameraMode::ThirdPerson: return m_scene->thirdPersonCamera;
        case CameraMode::Static: return m_scene->staticCamera;
        case CameraMode::Pivot: return m_scene->pivotCamera;
        default: return m_scene->firstPersonCamera;
        }
    }

auto UvsrSceneViewer::SetCameraMode(CameraMode mode) -> void {
        if (mode != CameraMode::ThirdPerson && mode != CameraMode::Static)
            return;

        if (mode == m_ui.Camera)
            return;

        const CameraController& source = GetActiveCamera();
        const gpu_contract::Float3 position = source.GetPosition();
        const gpu_contract::Float3 direction = source.GetDir();
        const gpu_contract::Float3 up = source.GetUp();

        switch (mode)
        {
        case CameraMode::FirstPerson:
            m_scene->firstPersonCamera.LookTo(position, direction, up);
            break;

        case CameraMode::ThirdPerson:
            m_scene->thirdPersonCamera.LookTo(position, direction, up);
            m_scene->thirdPersonCamera.CancelPendingMotion();
            break;

        case CameraMode::Static:
            m_scene->staticCamera.LookTo(position, direction, up);
            break;

        case CameraMode::Pivot:
            m_scene->pivotCamera.LookTo(position, direction, up);
            break;
        }

        m_ui.Camera = mode;
    }

auto UvsrSceneViewer::ApplyCameraPose(
        gpu_contract::Float3 position,
        gpu_contract::Float3 direction,
        gpu_contract::Float3 up,
        gpu_contract::Float3 right,
        float verticalFovDegrees) -> void {
        m_scene->cameraVerticalFov = verticalFovDegrees;
        const float zoomReferenceDistance =
            m_scene->thirdPersonCamera.GetReferenceZoomDistance();
        m_scene->thirdPersonCamera.ResetZoomReferenceDistance(zoomReferenceDistance);
        m_scene->thirdPersonCamera.SetExactPose(
            position,
            direction,
            up,
            right);
        m_scene->firstPersonCamera.SetExactPose(
            position,
            direction,
            up,
            right);
        m_scene->pivotCamera.SetExactPose(
            position,
            direction,
            up,
            right);
        m_scene->staticCamera.SetExactPose(
            position,
            direction,
            up,
            right);

        // A preset jump is a teleport, not a traversable flashlight motion.
        // Reinitialize the emitter at the new camera pose so the collision
        // sweep cannot strand it against geometry between the two locations.
        ResetFlashlightMotion();
        if (m_frame->autoExposurePass)
            m_frame->autoExposurePass->Reset();
    }

auto UvsrSceneViewer::ResetFactorySettingsRuntimeState() -> bool {
        if (m_lighting->directionalRayVisibilityPass)
            m_lighting->directionalRayVisibilityPass->ResetBindingCache();
        if (m_lighting->rayTracedFlashlightShadowPass)
            m_lighting->rayTracedFlashlightShadowPass->ResetBindingCache();
        if (m_lighting->rayTracedSkyVisibilityPass)
            m_lighting->rayTracedSkyVisibilityPass->ResetBindingCache();
        if (m_scene->worldSpaceRepresentation)
            m_scene->worldSpaceRepresentation->Reset();

        if (m_lighting->flashlight)
        {
            RendererSceneLightValues candidate;
            const bool read = ReadSceneLightValues(m_lighting->flashlight, candidate);
            candidate.intensity = 0.f;
            if (!read || !SetSceneLightValues(m_lighting->flashlight, candidate))
            {
                uvsr::log::error("Flashlight reset transaction failed");
                GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
                return false;
            }
        }
        m_lighting->flashlightTransition = 0.f;
        ResetFlashlightMotion();
        if (m_frame->autoExposurePass)
            m_frame->autoExposurePass->Reset();

        m_lighting->rayTracedFlashlightShadowPhase = 0u;
        m_lighting->rayTracedSkyVisibilityPhase = 0u;
        ResetImageBasedLightingHistory();
        m_ui.ShaderReloadRequested = true;
        uvsr::log::info("All renderer settings restored to factory defaults");
        return true;
    }


auto UvsrSceneViewer::SynchronizeCameraInput() -> void {
        GLFWwindow* window = GetDeviceManager()->GetWindow();
        if (!window)
            return;

        const bool windowFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
        const bool imguiAvailable = ImGui::GetCurrentContext() != nullptr;
        const bool keyboardCaptured = imguiAvailable && ImGui::GetIO().WantCaptureKeyboard;
        const bool mouseCaptured = imguiAvailable && ImGui::GetIO().WantCaptureMouse;

        // Donut's cameras intentionally keep their own key/button latches, but
        // an ImGui popup or a native focus transition can consume the matching
        // release callback. Polling GLFW once per animated frame reconciles the
        // latches with physical state after focus returns. Inactive controllers
        // are explicitly released so switching modes cannot revive stale input.
        static constexpr int CameraKeys[] = {
            GLFW_KEY_Q,
            GLFW_KEY_E,
            GLFW_KEY_A,
            GLFW_KEY_D,
            GLFW_KEY_W,
            GLFW_KEY_S,
            GLFW_KEY_LEFT,
            GLFW_KEY_RIGHT,
            GLFW_KEY_UP,
            GLFW_KEY_DOWN,
            GLFW_KEY_X,
            GLFW_KEY_C,
            GLFW_KEY_V,
            GLFW_KEY_LEFT_SHIFT,
            GLFW_KEY_RIGHT_SHIFT,
            GLFW_KEY_LEFT_CONTROL,
            GLFW_KEY_RIGHT_CONTROL,
            GLFW_KEY_LEFT_ALT
        };

        const bool firstPersonActive = m_ui.Camera == CameraMode::FirstPerson;
        const bool thirdPersonActive = m_ui.Camera == CameraMode::ThirdPerson;
        const bool pivotActive = m_ui.Camera == CameraMode::Pivot;
        const bool allowKeyboard = windowFocused && !keyboardCaptured && !m_ui.DisplaySyncTestActive;
        for (int key : CameraKeys)
        {
            // GLFW polling is used only to clear stale latches. Synthesizing a
            // press here would turn a key held while closing UI into a new
            // camera action even though the camera never received its press.
            const bool physicallyPressed = allowKeyboard &&
                glfwGetKey(window, key) == GLFW_PRESS;
            if (!firstPersonActive || !physicallyPressed)
                m_scene->firstPersonCamera.KeyboardUpdate(key, 0, GLFW_RELEASE, 0);
            if (!thirdPersonActive || !physicallyPressed)
                m_scene->thirdPersonCamera.KeyboardUpdate(key, 0, GLFW_RELEASE, 0);
            if (!pivotActive || !physicallyPressed)
                m_scene->pivotCamera.KeyboardUpdate(key, 0, GLFW_RELEASE, 0);
        }

        // ImGui consumes mouse-position callbacks while its windows are active.
        // Polling the current position into both cameras prevents the inactive
        // third-person camera from seeing one giant stale delta after a mode
        // switch. Match DeviceManager's display-scale conversion exactly.
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        if (!GetDeviceManager()->GetDeviceParams().supportExplicitDisplayScaling)
        {
            float dpiScaleX = 1.f;
            float dpiScaleY = 1.f;
            GetDeviceManager()->GetDPIScaleInfo(dpiScaleX, dpiScaleY);
            cursorX /= dpiScaleX;
            cursorY /= dpiScaleY;
        }
        m_scene->firstPersonCamera.MousePosUpdate(cursorX, cursorY);
        m_scene->thirdPersonCamera.MousePosUpdate(cursorX, cursorY);
        m_scene->pivotCamera.MousePosUpdate(cursorX, cursorY);

        static constexpr int CameraMouseButtons[] = {
            GLFW_MOUSE_BUTTON_LEFT,
            GLFW_MOUSE_BUTTON_MIDDLE,
            GLFW_MOUSE_BUTTON_RIGHT
        };

        const bool allowMouse = windowFocused && !mouseCaptured && !m_ui.DisplaySyncTestActive;
        for (int button : CameraMouseButtons)
        {
            const bool physicallyPressed = allowMouse &&
                glfwGetMouseButton(window, button) == GLFW_PRESS;
            if (!firstPersonActive || !physicallyPressed)
                m_scene->firstPersonCamera.MouseButtonUpdate(button, GLFW_RELEASE, 0);
            if (!thirdPersonActive || !physicallyPressed)
                m_scene->thirdPersonCamera.MouseButtonUpdate(button, GLFW_RELEASE, 0);
            if (!pivotActive || !physicallyPressed)
                m_scene->pivotCamera.MouseButtonUpdate(button, GLFW_RELEASE, 0);
        }
    }

auto UvsrSceneViewer::BuildCameraCollisionWorld(
        const RendererSceneLoadCancellation& cancellation) -> bool {
        if (cancellation.IsRequested()) return false;
        const auto start = std::chrono::steady_clock::now();
        const auto view = m_scene->canonical.View();
        // only this worker writes the output. all borrowed packed source inputs
        // stay frozen until the render owner joins and releases CPU payloads.
        struct Inputs
        {
            CameraCollisionSourceBuffers* buffers = nullptr;
            ~Inputs() { delete[] buffers; }
        } inputs;
        if (view.bufferGroups.count != m_scene->geometry.BufferCount() || view.bufferGroups.count > UINT32_MAX ||
            view.bufferGroups.count > size_t(PTRDIFF_MAX) / sizeof(CameraCollisionSourceBuffers))
            return false;
        if (view.bufferGroups.count)
        {
            inputs.buffers = new (std::nothrow) CameraCollisionSourceBuffers[view.bufferGroups.count];
            if (!inputs.buffers) return false;
        }
        for (size_t index = 0; index < view.bufferGroups.count; ++index)
        {
            const auto source = m_scene->geometry.Buffer(index);
            if (!source.indices.IsValid() || !source.vertices.IsValid() || source.indices.count % sizeof(uint32_t)) return false;
            // initial skin collision uses the canonical prototype, whose packed
            // positions are present. derived GPU-only groups remain empty here.
            if (!source.vertices.count) continue;
            const auto range = view.bufferGroups.data[index].attributes[uint32_t(RendererSceneVertexAttribute::Position)];
            if (range.offset > source.vertices.count || range.size > source.vertices.count - range.offset) return false;
            inputs.buffers[index] = {source.indices,
                {source.vertices.data + size_t(range.offset), size_t(range.size)}};
        }
        CameraCollisionWorld candidate;
        const auto result = candidate.BuildFromScene(view, {inputs.buffers, view.bufferGroups.count});
        if (result != CameraCollisionBuildError::None)
        {
            uvsr::log::error("Canonical camera collision preparation failed: %u", unsigned(result));
            return false;
        }
        if (cancellation.IsRequested()) return false;
        m_scene->pendingCameraCollisionWorld.emplace(std::move(candidate));
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        uvsr::log::info("Camera collision: %zu triangles, %.3f-unit radius, prepared from canonical records in %lld ms on the scene worker",
            m_scene->pendingCameraCollisionWorld->GetTriangleCount(), m_scene->cameraCollisionRadius,
            static_cast<long long>(milliseconds));
        return true;
    }

auto UvsrSceneViewer::KeyboardUpdate(int key, int scancode, int action, int mods) -> bool {
        if (m_ui.DisplaySyncTestActive)
            return true;
        GetActiveCamera().KeyboardUpdate(key, scancode, action, mods);
        return true;
    }

auto UvsrSceneViewer::MousePosUpdate(double xpos, double ypos) -> bool {
        if (m_ui.DisplaySyncTestActive)
            return true;
        // Keep all interactive controllers synchronized while inactive. A
        // later press can then begin from the current cursor position instead
        // of applying all motion accumulated since the last mode switch.
        m_scene->firstPersonCamera.MousePosUpdate(xpos, ypos);
        m_scene->thirdPersonCamera.MousePosUpdate(xpos, ypos);
        m_scene->pivotCamera.MousePosUpdate(xpos, ypos);

        if (m_frame->materialPickPurpose == MaterialPickPurpose::None)
        {
            m_frame->pickPosition =
                gpu_contract::Uint2{static_cast<uint32_t>(xpos), static_cast<uint32_t>(ypos)};
        }

        return true;
    }

auto UvsrSceneViewer::MouseButtonUpdate(int button, int action, int mods) -> bool {
        if (m_ui.DisplaySyncTestActive)
            return true;
        GetActiveCamera().MouseButtonUpdate(button, action, mods);

        if (action == GLFW_PRESS &&
            button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            // Snapshot the cursor coordinate at the press. Subsequent camera
            // motion cannot slide the pending material-ID readback elsewhere.
            m_frame->materialPickPurpose =
                MaterialPickPurpose::FocusCameraAtCursor;
            m_frame->materialPickGeneration = m_scene->canonical.View().generation;
        }

        return true;
    }

auto UvsrSceneViewer::MouseScrollUpdate(double xoffset, double yoffset) -> bool {
        if (m_ui.DisplaySyncTestActive)
            return true;
        GetActiveCamera().MouseScrollUpdate(xoffset, yoffset);

        return true;
    }

auto UvsrSceneViewer::Animate(float fElapsedTimeSeconds) -> void {
        m_frame->frameDeltaSeconds = std::isfinite(fElapsedTimeSeconds)
            ? std::clamp(fElapsedTimeSeconds, 0.f, 1.f)
            : 0.f;
        SynchronizeCameraInput();
        if (!m_scene->rendererSceneLoaded) return;

        switch (m_ui.Camera)
        {
        case CameraMode::ThirdPerson:
        {
            // Freelook combines mouse/arrow look, W/S dolly, and A/D strafe.
            // It moves the eye directly with no orbit target or pivot state.
            const gpu_contract::Float3 start = m_scene->thirdPersonCamera.GetPosition();
            m_scene->thirdPersonCamera.Animate(fElapsedTimeSeconds);

            const gpu_contract::Float3 desiredPosition = m_scene->thirdPersonCamera.GetPosition();
            const gpu_contract::Float3 resolvedPosition = m_scene->cameraCollisionWorld.MoveSphere(
                start, desiredPosition, m_scene->cameraCollisionRadius);
            if (LengthSquared(resolvedPosition - desiredPosition) > 1e-12f)
            {
                // The correction becomes the free-look camera's next origin;
                // its look direction and dolly sensitivity stay unchanged.
                m_scene->thirdPersonCamera.ApplyCollisionPosition(resolvedPosition);
            }
            break;
        }

        case CameraMode::Pivot:
            m_scene->pivotCamera.Animate(fElapsedTimeSeconds);
            break;

        case CameraMode::Static:
            break;

        case CameraMode::FirstPerson:
        {
            const gpu_contract::Float3 start = m_scene->firstPersonCamera.GetPosition();
            m_scene->firstPersonCamera.Animate(fElapsedTimeSeconds);

            const gpu_contract::Float3 desiredPosition = m_scene->firstPersonCamera.GetPosition();
            const gpu_contract::Float3 resolvedPosition = m_scene->cameraCollisionWorld.MoveSphere(
                start, desiredPosition, m_scene->cameraCollisionRadius);
            if (LengthSquared(resolvedPosition - desiredPosition) > 1e-12f)
            {
                m_scene->firstPersonCamera.LookTo(
                    resolvedPosition,
                    m_scene->firstPersonCamera.GetDir(),
                    m_scene->firstPersonCamera.GetUp());
            }
            break;
        }
        }

        float flashlightDelta = fElapsedTimeSeconds;
#if defined(UVSR_BUILD_TESTING)
        if (m_frame->runtimeOutputCaptureRequested)
            flashlightDelta = 0.f;
#endif
        UpdateFlashlightAnimation(flashlightDelta);
        UpdateFlashlightMotion(flashlightDelta);
    }

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::RequestRuntimeOutputEvidence(
        size_t caseIndex,
        std::string_view caseName,
        std::string_view phase) -> bool {
        RuntimeCaptureFileResult preparation;
        if (!BuildRuntimeCapturePath(caseIndex, caseName, phase,
                m_frame->runtimeOutputCapturePath, preparation))
        {
            uvsr::log::error("Runtime capture path preparation failed (%u, code %u)",
                unsigned(preparation.error), preparation.code);
            // a rejected overlapping request must not orphan the older capture.
            if (m_frame->runtimeOutputCaptureRequested)
            {
                m_frame->FailRuntimeOutputCapture();
                return true;
            }
            return false;
        }
        m_frame->runtimeOutputEvidence.reset();
        const bool pathSelected = m_ui.Lighting == LightingSolution::PathTracing;
        if (!g_VerifyRetainedRuntimeRequested ||
            (pathSelected && !m_lighting->pathTracingPass) ||
            m_frame->runtimeOutputCaptureRequested)
        {
            uvsr::log::error("Runtime capture could not arm its deterministic sequence");
            m_frame->runtimeOutputEvidence = RuntimeOutputEvidence{};
            m_frame->runtimeOutputCaptureRequested = false;
            return true;
        }
        // settle the capture-only pose and its scene-dirty pulse before arming samples.
        m_lighting->flashlightTransition = m_ui.FlashlightEnabled ? 1.f : 0.f;
        if (!ApplyFlashlightPresentation())
        {
            uvsr::log::error("Runtime capture flashlight transaction failed");
            m_frame->runtimeOutputEvidence = RuntimeOutputEvidence{};
            m_frame->runtimeOutputCaptureRequested = false;
            return true;
        }
        ResetFlashlightMotion();
        UpdateFlashlightMotion(0.f);
        m_frame->runtimeCaptureSequence = {};
        m_frame->runtimeCaptureSettlingFrames = 2u;
        m_frame->runtimeOutputCaptureRequested = true;
        return true;
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::ConsumeRuntimeOutputEvidence() -> std::optional<RuntimeOutputEvidence> {
        std::optional<RuntimeOutputEvidence> evidence =
            std::move(m_frame->runtimeOutputEvidence);
        m_frame->runtimeOutputEvidence.reset();
        return evidence;
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::NudgeCameraForRuntimeDiagnostic() -> void {
        const CameraController& camera = GetActiveCamera();
        const gpu_contract::Float3 direction = Normalize(camera.GetDir());
        const gpu_contract::Float3 upHint = Normalize(camera.GetUp());
        const gpu_contract::Float3 right = Normalize(Cross(direction, upHint));
        const gpu_contract::Float3 up = Normalize(Cross(right, direction));
        ApplyCameraPose(
            camera.GetPosition() + right * 0.05f,
            direction,
            up,
            right,
            m_scene->cameraVerticalFov);
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::CaptureRetainedRuntimeCameraPose() const -> RetainedRuntimeCameraPose {
        const CameraController& camera = GetActiveCamera();
        return {
            camera.GetPosition(),
            camera.GetDir(),
            camera.GetUp(),
            Normalize(Cross(camera.GetDir(), camera.GetUp())),
            m_scene->cameraVerticalFov
        };
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::RestoreRetainedRuntimeCameraPose(
        const RetainedRuntimeCameraPose& pose) -> void {
        ApplyCameraPose(
            pose.position,
            pose.direction,
            pose.up,
            pose.right,
            pose.verticalFovDegrees);
    }
#endif

auto UvsrSceneViewer::HasPrimaryDirectionalLight() const -> bool {
        return GetSceneLight(m_lighting->sunLight) != nullptr;
    }

auto UvsrSceneViewer::GetPrimaryDirectionalLight() const -> RendererSceneHandle {
        return HasPrimaryDirectionalLight() ? m_lighting->sunLight : RendererSceneHandle{};
    }

auto UvsrSceneViewer::GetEditableLights() const -> RendererSceneLightRange {
        return {GetSceneView(), m_lighting->flashlight, true};
    }

auto UvsrSceneViewer::GetSceneLight(RendererSceneHandle light) const -> const RendererSceneLight* {
        return FindRendererSceneLight(GetSceneView(), light);
    }

auto UvsrSceneViewer::GetSceneLightName(RendererSceneHandle light) const -> std::string {
        return std::string(GetSceneLightNameView(light));
    }

auto UvsrSceneViewer::GetSceneLightNameView(RendererSceneHandle light) const noexcept -> std::string_view {
        const auto scene = GetSceneView();
        const auto* record = FindRendererSceneLight(scene, light);
        if (!record || record->nodeIndex >= scene.nodes.count) return {};
        const auto name = RendererSceneText(scene, scene.nodes.data[record->nodeIndex].name);
        return name.count ? std::string_view(name.data, name.count) : std::string_view{};
    }
