#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene.h"
#include "uvsr_renderer_lighting.h"
#include "uvsr_renderer_frame.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include "gpu_capabilities.h"
#include "windows_executable_path.h"
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <Windows.h>
#include <cstring>
#include <cctype>

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;

bool RestartCurrentProcess()
{
    std::wstring commandLine = GetCommandLineW();
    if (g_RestartAdapterIndex >= 0)
    {
        // ParseUvsrCommandLine applies options from left to right, so appending
        // the requested adapter also replaces an older -adapter option carried
        // by a previous renderer restart without rewriting unrelated arguments.
        commandLine += L" -adapter ";
        commandLine += std::to_wstring(g_RestartAdapterIndex);
    }

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
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

UvsrSceneViewer::UvsrSceneViewer(
        DeviceManager* deviceManager,
        UIData& ui,
        const std::string& sceneName)
        : Super(deviceManager)
        , m_scene(std::make_unique<RendererSceneState>(deviceManager->GetDevice()))
        , m_lighting(std::make_unique<RendererLightingState>())
        , m_frame(std::make_unique<RendererFrameState>(deviceManager->GetDevice()))
        , m_ui(ui) {
        m_scene->rootFs = std::make_shared<RootFileSystem>();

        const std::filesystem::path executableDirectory =
            GetExecutableDirectoryWide();
        std::filesystem::path mediaDir = executableDirectory.parent_path() / "media";
        std::filesystem::path frameworkShaderDir = executableDirectory / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
        std::filesystem::path appShaderDir = executableDirectory / "shaders/uvsr" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        m_scene->rootFs->mount("/media", mediaDir);
        m_scene->rootFs->mount("/shaders/donut", frameworkShaderDir);
        m_scene->rootFs->mount("/shaders/uvsr", appShaderDir);

        m_scene->nativeFs = std::make_shared<NativeFileSystem>();
        m_frame->rendererShaderFactory =
            std::make_shared<uvsr::RendererShaderFactory>(
                GetDevice(), appShaderDir);
        m_frame->rendererCommonPasses =
            std::make_shared<uvsr::RendererCommonPasses>(
                GetDevice(), m_frame->rendererShaderFactory);
        if (!m_frame->rendererCommonPasses->IsValid())
        {
            throw std::runtime_error(
                "UVSR common renderer resources failed to initialize");
        }

        m_scene->sceneDir = mediaDir / "glTF-Sample-Assets/Models/";
        const std::array retainedSceneDescriptors = {
            m_scene->sceneDir /
                "bistro_interior_retextured/"
                "bistro_interior_retextured.scene.json",
            m_scene->sceneDir /
                "san_miguel_retextured/"
                "san_miguel_retextured.scene.json"
        };
        std::vector<std::string> retainedSceneFiles;
        retainedSceneFiles.reserve(retainedSceneDescriptors.size());
        for (const std::filesystem::path& descriptor :
            retainedSceneDescriptors)
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(descriptor, error))
            {
                uvsr::log::fatal(
                    "Required retained scene descriptor is unavailable: "
                    "%s (%s)",
                    descriptor.generic_string().c_str(),
                    error ? error.message().c_str() : "not a regular file");
            }
            retainedSceneFiles.push_back(
                descriptor.lexically_normal().generic_string());
        }
        m_scene->sceneCatalog = BuildSceneCatalog(
            m_scene->sceneDir,
            retainedSceneFiles);

        if (m_scene->sceneCatalog.size() != retainedSceneDescriptors.size())
        {
            uvsr::log::fatal(
                "The retained scene catalog must resolve exactly Bistro and "
                "San Miguel; resolved %zu entries",
                m_scene->sceneCatalog.size());
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
            m_scene->descriptorTable =
                std::make_shared<DescriptorTableManager>(
                    GetDevice(),
                    m_scene->bindlessLayout);
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
        m_TextureCache = std::make_shared<TextureCache>(
            GetDevice(),
            m_scene->nativeFs,
            m_scene->descriptorTable);

        m_frame->shaderFactory = std::make_shared<ShaderFactory>(GetDevice(), m_scene->rootFs, "/shaders");
        m_CommonPasses = std::make_shared<CommonRenderPasses>(GetDevice(), m_frame->shaderFactory);
        m_lighting->imageBasedLightingEnvironment =
            std::make_unique<ImageBasedLightingEnvironment>(
                GetDevice(),
                m_frame->rendererShaderFactory,
                m_frame->rendererCommonPasses,
                mediaDir / "environments");
        m_lighting->noiseTextureLibrary = std::make_unique<NoiseTextureLibrary>(
            GetDevice(),
            mediaDir / "uvsr/noise");

        m_frame->opaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>();


        m_frame->commandList = GetDevice()->createCommandList();
        m_scene->worldSpaceRepresentation =
            std::make_unique<WorldSpaceRepresentation>(GetDevice());
        for (auto& stageQueries : m_frame->rendererTimerQueries)
        {
            for (nvrhi::TimerQueryHandle& query : stageQueries)
                query = GetDevice()->createTimerQuery();
        }

        if (sceneName.empty())
        {
            // Prefer the smaller retained scene as the startup fallback. This
            // ordering is not evidence that it is more runtime-reliable.
            const std::string defaultScene = (m_scene->sceneDir
                / "bistro_interior_retextured/bistro_interior_retextured.scene.json").lexically_normal().generic_string();
            if (const SceneCatalogEntry* entry = FindSceneCatalogEntry(m_scene->sceneCatalog, defaultScene))
                SetCurrentSceneName(entry->FileName);
            else
            {
                uvsr::log::warning(
                    "Default Bistro descriptor '%s' was not found; loading '%s' instead.",
                    defaultScene.c_str(),
                    m_scene->sceneCatalog.front().FileName.c_str());
                SetCurrentSceneName(m_scene->sceneCatalog.front().FileName);
            }
        }
        else
            SetCurrentSceneName(sceneName);

    }

UvsrSceneViewer::~UvsrSceneViewer() {
        // The task executes this derived class's LoadScene. Join it before
        // any state captured through `this` can be destroyed.
        m_scene->sceneLoadWorker.Reset();
        if (m_scene->sceneRetirementPending &&
            m_scene->sceneRetirement.CompleteBlocking() !=
                RendererSceneRetirementStatus::Ready)
        {
            uvsr::log::fatal(
                "UVSR could not prove scene GPU retirement during shutdown");
        }
    }

auto UvsrSceneViewer::GetRootFs() const -> std::shared_ptr<vfs::IFileSystem> {
		return m_scene->rootFs;
	}

auto UvsrSceneViewer::GetActiveCamera() const -> BaseCamera& {
        switch (m_ui.Camera)
        {
        case CameraMode::FirstPerson: return (BaseCamera&)m_scene->firstPersonCamera;
        case CameraMode::ThirdPerson: return (BaseCamera&)m_scene->thirdPersonCamera;
        case CameraMode::Static: return (BaseCamera&)m_scene->staticCamera;
        case CameraMode::Pivot: return (BaseCamera&)m_scene->pivotCamera;
        default: return (BaseCamera&)m_scene->firstPersonCamera;
        }
    }

auto UvsrSceneViewer::SetCameraMode(CameraMode mode) -> void {
        if (mode != CameraMode::ThirdPerson && mode != CameraMode::Static)
            return;

        if (mode == m_ui.Camera)
            return;

        const BaseCamera& source = GetActiveCamera();
        const float3 position = source.GetPosition();
        const float3 direction = source.GetDir();
        const float3 up = source.GetUp();

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
        float3 position,
        float3 direction,
        float3 up,
        float3 right,
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

auto UvsrSceneViewer::ResetFactorySettingsRuntimeState() -> void {
        if (m_lighting->directionalRayVisibilityPass)
            m_lighting->directionalRayVisibilityPass->ResetBindingCache();
        if (m_lighting->rayTracedFlashlightShadowPass)
            m_lighting->rayTracedFlashlightShadowPass->ResetBindingCache();
        if (m_lighting->rayTracedSkyVisibilityPass)
            m_lighting->rayTracedSkyVisibilityPass->ResetBindingCache();
        if (m_scene->worldSpaceRepresentation)
            m_scene->worldSpaceRepresentation->Reset();

        m_lighting->flashlightTransition = 0.f;
        if (m_lighting->flashlight)
            m_lighting->flashlight->intensity = 0.f;
        ResetFlashlightMotion();
        if (m_frame->autoExposurePass)
            m_frame->autoExposurePass->Reset();

        m_lighting->rayTracedFlashlightShadowPhase = 0u;
        m_lighting->rayTracedSkyVisibilityPhase = 0u;
        ResetImageBasedLightingHistory();
        m_ui.ShaderReloadRequested = true;
        uvsr::log::info("All renderer settings restored to factory defaults");
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
        const Scene& scene,
        float collisionRadius) -> CameraCollisionWorld {
        const auto extractionStart =
            std::chrono::high_resolution_clock::now();
        std::vector<CameraCollisionWorld::Triangle> triangles;
        const auto& instances =
            scene.GetSceneGraph()->GetMeshInstances();

        size_t triangleCapacity = 0;
        for (const auto& instance : instances)
        {
            if (!instance)
                continue;

            std::shared_ptr<MeshInfo> mesh = instance->GetMesh();
            if (const auto skinnedInstance = std::dynamic_pointer_cast<SkinnedMeshInstance>(instance))
                mesh = skinnedInstance->GetPrototypeMesh();

            if (!mesh)
                continue;

            for (const auto& geometry : mesh->geometries)
            {
                if (geometry && geometry->type == MeshGeometryPrimitiveType::Triangles)
                    triangleCapacity += geometry->numIndices / 3;
            }
        }
        triangles.reserve(triangleCapacity);

        for (const auto& instance : instances)
        {
            if (!instance || !instance->GetNode())
                continue;

            std::shared_ptr<MeshInfo> mesh = instance->GetMesh();
            if (const auto skinnedInstance = std::dynamic_pointer_cast<SkinnedMeshInstance>(instance))
                mesh = skinnedInstance->GetPrototypeMesh();

            if (!mesh || !mesh->buffers || mesh->buffers->indexData.empty() ||
                mesh->buffers->positionData.empty())
            {
                continue;
            }

            const auto& indices = mesh->buffers->indexData;
            const auto& positions = mesh->buffers->positionData;
            const affine3 localToWorld = instance->GetNode()->GetLocalToWorldTransformFloat();

            for (const auto& geometry : mesh->geometries)
            {
                if (!geometry || geometry->type != MeshGeometryPrimitiveType::Triangles)
                    continue;

                const size_t firstIndex = size_t(mesh->indexOffset) + geometry->indexOffsetInMesh;
                const size_t firstVertex = size_t(mesh->vertexOffset) + geometry->vertexOffsetInMesh;
                if (firstIndex + geometry->numIndices > indices.size())
                {
                    uvsr::log::warning("Skipping camera collision geometry with an invalid index range");
                    continue;
                }

                for (uint32_t index = 0; index + 2 < geometry->numIndices; index += 3)
                {
                    const size_t vertex0 = firstVertex + indices[firstIndex + index];
                    const size_t vertex1 = firstVertex + indices[firstIndex + index + 1];
                    const size_t vertex2 = firstVertex + indices[firstIndex + index + 2];
                    if (vertex0 >= positions.size() || vertex1 >= positions.size() ||
                        vertex2 >= positions.size())
                    {
                        continue;
                    }

                    triangles.push_back({
                        localToWorld.transformPoint(positions[vertex0]),
                        localToWorld.transformPoint(positions[vertex1]),
                        localToWorld.transformPoint(positions[vertex2])
                    });
                }
            }
        }

        const auto buildStart = std::chrono::high_resolution_clock::now();
        const auto extractionDuration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                buildStart - extractionStart).count();
        CameraCollisionWorld collisionWorld;
        collisionWorld.Build(std::move(triangles));
        const auto buildDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - buildStart).count();
        uvsr::log::info(
            "Camera collision: %zu triangles, %.3f-unit radius, extracted in %lld ms and built in %lld ms on the scene worker",
            collisionWorld.GetTriangleCount(),
            collisionRadius,
            static_cast<long long>(extractionDuration),
            static_cast<long long>(buildDuration));
        return collisionWorld;
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
                uint2(static_cast<uint>(xpos), static_cast<uint>(ypos));
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
            m_frame->materialPickScene = m_scene->world.get();
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

        switch (m_ui.Camera)
        {
        case CameraMode::ThirdPerson:
        {
            // Freelook combines mouse/arrow look, W/S dolly, and A/D strafe.
            // It moves the eye directly with no orbit target or pivot state.
            const float3 start = m_scene->thirdPersonCamera.GetPosition();
            m_scene->thirdPersonCamera.Animate(fElapsedTimeSeconds);

            const float3 desiredPosition = m_scene->thirdPersonCamera.GetPosition();
            const float3 resolvedPosition = m_scene->cameraCollisionWorld.MoveSphere(
                start, desiredPosition, m_scene->cameraCollisionRadius);
            if (lengthSquared(resolvedPosition - desiredPosition) > 1e-12f)
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
            const float3 start = m_scene->firstPersonCamera.GetPosition();
            m_scene->firstPersonCamera.Animate(fElapsedTimeSeconds);

            const float3 desiredPosition = m_scene->firstPersonCamera.GetPosition();
            const float3 resolvedPosition = m_scene->cameraCollisionWorld.MoveSphere(
                start, desiredPosition, m_scene->cameraCollisionRadius);
            if (lengthSquared(resolvedPosition - desiredPosition) > 1e-12f)
            {
                m_scene->firstPersonCamera.LookTo(
                    resolvedPosition,
                    m_scene->firstPersonCamera.GetDir(),
                    m_scene->firstPersonCamera.GetUp());
            }
            break;
        }
        }

        UpdateFlashlightAnimation(fElapsedTimeSeconds);
        UpdateFlashlightMotion(fElapsedTimeSeconds);
    }

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::RequestRuntimeOutputEvidence(
        size_t caseIndex,
        std::string_view caseName) -> void {
        std::string safeName(caseName);
        for (char& character : safeName)
        {
            if (!std::isalnum(static_cast<unsigned char>(character)) &&
                character != '-' && character != '_')
            {
                character = '-';
            }
        }
        m_frame->runtimeOutputCapturePath =
            std::filesystem::temp_directory_path() /
            ("uvsr-retained-runtime-" +
                std::to_string(GetCurrentProcessId())) /
            ("case-" + std::to_string(caseIndex) + "-" + safeName +
                ".bmp");
        m_frame->runtimeOutputEvidence.reset();
        m_frame->runtimeOutputCaptureRequested = true;
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
        const BaseCamera& camera = GetActiveCamera();
        const float3 direction = normalize(camera.GetDir());
        const float3 upHint = normalize(camera.GetUp());
        const float3 right = normalize(cross(direction, upHint));
        const float3 up = normalize(cross(right, direction));
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
        const BaseCamera& camera = GetActiveCamera();
        return {
            camera.GetPosition(),
            camera.GetDir(),
            camera.GetUp(),
            normalize(cross(camera.GetDir(), camera.GetUp())),
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
        return bool(m_lighting->sunLight);
    }

auto UvsrSceneViewer::GetPrimaryDirectionalLight() const -> std::shared_ptr<DirectionalLight> {
        return m_lighting->sunLight;
    }

auto UvsrSceneViewer::GetEditableLights() const -> const std::vector<std::shared_ptr<Light>>& {
        return m_lighting->editableLights;
    }
