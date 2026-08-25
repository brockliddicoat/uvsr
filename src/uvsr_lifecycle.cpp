#include "uvsr_internal.h"

auto UvsrSceneViewer::FailOpenRendererFrame(const char* passName) -> void {
        uvsr::log::error("Required renderer pass failed: %s", passName);
        m_CommandList->close();
        GetDeviceManager()->ReportRenderDisposition(
            uvsr::RendererRenderDisposition::Failed);
    }

auto UvsrSceneViewer::SubmitPendingRendererPreparationFrame() -> void {
        EndRendererStage(RendererTimingStage::SceneSetup);
        EndRendererStage(RendererTimingStage::CompleteFrame);
        CompleteRendererTimerFrame();
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        GetDeviceManager()->ReportRenderDisposition(
            uvsr::RendererRenderDisposition::Pending);
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
        , m_SceneRetirement(deviceManager->GetDevice())
        , m_BindingCache(deviceManager->GetDevice())
        , m_ui(ui) {
        m_RootFs = std::make_shared<RootFileSystem>();

        const std::filesystem::path executableDirectory =
            GetExecutableDirectoryWide();
        std::filesystem::path mediaDir = executableDirectory.parent_path() / "media";
        std::filesystem::path frameworkShaderDir = executableDirectory / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
        std::filesystem::path appShaderDir = executableDirectory / "shaders/uvsr" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        m_RootFs->mount("/media", mediaDir);
        m_RootFs->mount("/shaders/donut", frameworkShaderDir);
        m_RootFs->mount("/shaders/uvsr", appShaderDir);

        m_NativeFs = std::make_shared<NativeFileSystem>();
        m_RendererShaderFactory =
            std::make_shared<uvsr::RendererShaderFactory>(
                GetDevice(), appShaderDir);
        m_RendererCommonPasses =
            std::make_shared<uvsr::RendererCommonPasses>(
                GetDevice(), m_RendererShaderFactory);
        if (!m_RendererCommonPasses->IsValid())
        {
            throw std::runtime_error(
                "UVSR common renderer resources failed to initialize");
        }

        m_SceneDir = mediaDir / "glTF-Sample-Assets/Models/";
        const std::array retainedSceneDescriptors = {
            m_SceneDir /
                "bistro_interior_retextured/"
                "bistro_interior_retextured.scene.json",
            m_SceneDir /
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
        m_SceneCatalog = BuildSceneCatalog(
            m_SceneDir,
            retainedSceneFiles);

        if (m_SceneCatalog.size() != retainedSceneDescriptors.size())
        {
            uvsr::log::fatal(
                "The retained scene catalog must resolve exactly Bistro and "
                "San Miguel; resolved %zu entries",
                m_SceneCatalog.size());
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
            m_BindlessLayout = GetDevice()->createBindlessLayout(
                bindlessLayoutDescription);
        }
        if (m_BindlessLayout)
        {
            m_DescriptorTable =
                std::make_shared<DescriptorTableManager>(
                    GetDevice(),
                    m_BindlessLayout);
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
            m_NativeFs,
            m_DescriptorTable);

        m_ShaderFactory = std::make_shared<ShaderFactory>(GetDevice(), m_RootFs, "/shaders");
        m_CommonPasses = std::make_shared<CommonRenderPasses>(GetDevice(), m_ShaderFactory);
        m_ImageBasedLightingEnvironment =
            std::make_unique<ImageBasedLightingEnvironment>(
                GetDevice(),
                m_RendererShaderFactory,
                m_RendererCommonPasses,
                mediaDir / "environments");
        m_NoiseTextureLibrary = std::make_unique<NoiseTextureLibrary>(
            GetDevice(),
            mediaDir / "uvsr/noise");

        m_OpaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>();


        m_CommandList = GetDevice()->createCommandList();
        m_WorldSpaceRepresentation =
            std::make_unique<WorldSpaceRepresentation>(GetDevice());
        m_LightingAccumulationPass =
            std::make_unique<LightingAccumulationPass>(
                GetDevice(),
                m_RendererShaderFactory);
        for (auto& stageQueries : m_RendererTimerQueries)
        {
            for (nvrhi::TimerQueryHandle& query : stageQueries)
                query = GetDevice()->createTimerQuery();
        }

        if (sceneName.empty())
        {
            // Prefer the smaller retained scene as the startup fallback. This
            // ordering is not evidence that it is more runtime-reliable.
            const std::string defaultScene = (m_SceneDir
                / "bistro_interior_retextured/bistro_interior_retextured.scene.json").lexically_normal().generic_string();
            if (const SceneCatalogEntry* entry = FindSceneCatalogEntry(m_SceneCatalog, defaultScene))
                SetCurrentSceneName(entry->FileName);
            else
            {
                uvsr::log::warning(
                    "Default Bistro descriptor '%s' was not found; loading '%s' instead.",
                    defaultScene.c_str(),
                    m_SceneCatalog.front().FileName.c_str());
                SetCurrentSceneName(m_SceneCatalog.front().FileName);
            }
        }
        else
            SetCurrentSceneName(sceneName);

    }

UvsrSceneViewer::~UvsrSceneViewer() {
        // The task executes this derived class's LoadScene. Join it before
        // any state captured through `this` can be destroyed.
        m_SceneLoadWorker.Reset();
        if (m_SceneRetirementPending)
            GetDevice()->waitForIdle();
    }

auto UvsrSceneViewer::GetRootFs() const -> std::shared_ptr<vfs::IFileSystem> {
		return m_RootFs;
	}

auto UvsrSceneViewer::GetActiveCamera() const -> BaseCamera& {
        switch (m_ui.Camera)
        {
        case CameraMode::FirstPerson: return (BaseCamera&)m_FirstPersonCamera;
        case CameraMode::ThirdPerson: return (BaseCamera&)m_ThirdPersonCamera;
        case CameraMode::Static: return (BaseCamera&)m_StaticCamera;
        case CameraMode::Pivot: return (BaseCamera&)m_PivotCamera;
        default: return (BaseCamera&)m_FirstPersonCamera;
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
            m_FirstPersonCamera.LookTo(position, direction, up);
            break;

        case CameraMode::ThirdPerson:
            m_ThirdPersonCamera.LookTo(position, direction, up);
            m_ThirdPersonCamera.CancelPendingMotion();
            break;

        case CameraMode::Static:
            m_StaticCamera.LookTo(position, direction, up);
            break;

        case CameraMode::Pivot:
            m_PivotCamera.LookTo(position, direction, up);
            break;
        }

        m_ui.Camera = mode;
    }

auto UvsrSceneViewer::ResetAntiAliasingState() -> void {
        if (m_TemporalAAPass)
            m_TemporalAAPass->ResetHistory();
        if (m_DenoisingPass)
            m_DenoisingPass->RequestHistoryReset();
        m_AntiAliasingPhase = 0u;
    }

auto UvsrSceneViewer::ApplyCameraPose(
        float3 position,
        float3 direction,
        float3 up,
        float3 right,
        float verticalFovDegrees) -> void {
        m_CameraVerticalFov = verticalFovDegrees;
        const float zoomReferenceDistance =
            m_ThirdPersonCamera.GetReferenceZoomDistance();
        m_ThirdPersonCamera.ResetZoomReferenceDistance(zoomReferenceDistance);
        m_ThirdPersonCamera.SetExactPose(
            position,
            direction,
            up,
            right);
        m_FirstPersonCamera.SetExactPose(
            position,
            direction,
            up,
            right);
        m_PivotCamera.SetExactPose(
            position,
            direction,
            up,
            right);
        m_StaticCamera.SetExactPose(
            position,
            direction,
            up,
            right);

        // A preset jump is a teleport, not a traversable flashlight motion.
        // Reinitialize the emitter at the new camera pose so the collision
        // sweep cannot strand it against geometry between the two locations.
        ResetFlashlightMotion();
        m_PreviousView.reset();
        ResetAntiAliasingState();
        if (m_AutoExposurePass)
            m_AutoExposurePass->Reset();
    }

auto UvsrSceneViewer::ResetAllRendererSettings() -> void {
        // Restore modes through their public setters first so material shader
        // permutations cannot retain state from the old setup.
        SetWhiteWorldMode(WhiteWorldMode::Off);

        m_ui.Lighting = LightingSolution::RayMarching;
        m_ui.AccumulateSamples = false;
        m_ui.AntiAliasing = AntiAliasingSettings{};
        m_ui.TemporalAaSharpenEnabled = false;
        m_ui.TemporalAaSharpness = TemporalAaDefaultSharpness;
        m_ui.DirectionalShadows = DirectionalShadowSettings{};
        m_ui.Denoising = DenoisingSettings{};
        m_ui.Representation = WorldSpaceRepresentationSettings{};
        m_ui.Noise = NoiseSettings{};
        if (m_DirectionalRayVisibilityPass)
            m_DirectionalRayVisibilityPass->ResetBindingCache();
        if (m_RayTracedFlashlightShadowPass)
            m_RayTracedFlashlightShadowPass->ResetBindingCache();
        m_ui.RayTracedSkyVisibility = RayTracedSkyVisibilitySettings{};
        if (m_RayTracedSkyVisibilityPass)
            m_RayTracedSkyVisibilityPass->ResetBindingCache();
        if (m_WorldSpaceRepresentation)
            m_WorldSpaceRepresentation->Reset();
        m_ui.ScreenSpaceVisibility = ScreenSpaceVisibilitySettings{};
        m_ui.PixelZoom = PixelZoomMode::Off;
        m_ui.FlashlightEnabled = DefaultFlashlightEnabled;
        m_ui.Flashlight = DefaultFlashlightSettings;
        m_FlashlightTransition = 0.f;
        if (m_Flashlight)
            m_Flashlight->intensity = 0.f;
        ResetFlashlightMotion();
        m_ui.ShowEnvironmentBackground = true;
        m_ui.EnableAmbientFill = true;
        m_ui.EnableDiffuseIbl = true;
        m_ui.DiffuseIblStrength = 1.f;
        m_ui.EnableSpecularIbl = true;
        m_ui.SpecularIblStrength = 1.f;
        m_ui.EnvironmentSource =
            ImageBasedLightingSource::Kloppenheim03Day;
        m_ui.EnvironmentExposureStops =
            GetImageBasedLightingSourceInfo(
                m_ui.EnvironmentSource).defaultExposureStops;
        m_ui.AutoExposure = AutoExposureSettings{};
        if (m_AutoExposurePass)
            m_AutoExposurePass->Reset();
        m_ui.LightingDebugView = PbrLightingDebugView::None;
        m_ScreenSpaceVisibilityPhase = 0u;
        m_RayTracedFlashlightShadowPhase = 0u;
        m_RayTracedSkyVisibilityPhase = 0u;
        InvalidateLightingAccumulationHistory();

        // Recreate passes and material permutations from the restored state.
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
            GLFW_KEY_LEFT_CONTROL,
            GLFW_KEY_RIGHT_CONTROL,
            GLFW_KEY_LEFT_ALT
        };

        const bool firstPersonActive = m_ui.Camera == CameraMode::FirstPerson;
        const bool thirdPersonActive = m_ui.Camera == CameraMode::ThirdPerson;
        const bool pivotActive = m_ui.Camera == CameraMode::Pivot;
        const bool allowKeyboard = windowFocused && !keyboardCaptured;
        for (int key : CameraKeys)
        {
            // GLFW polling is used only to clear stale latches. Synthesizing a
            // press here would turn a key held while closing UI into a new
            // camera action even though the camera never received its press.
            const bool physicallyPressed = allowKeyboard &&
                glfwGetKey(window, key) == GLFW_PRESS;
            if (!firstPersonActive || !physicallyPressed)
                m_FirstPersonCamera.KeyboardUpdate(key, 0, GLFW_RELEASE, 0);
            if (!thirdPersonActive || !physicallyPressed)
                m_ThirdPersonCamera.KeyboardUpdate(key, 0, GLFW_RELEASE, 0);
            if (!pivotActive || !physicallyPressed)
                m_PivotCamera.KeyboardUpdate(key, 0, GLFW_RELEASE, 0);
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
        m_FirstPersonCamera.MousePosUpdate(cursorX, cursorY);
        m_ThirdPersonCamera.MousePosUpdate(cursorX, cursorY);
        m_PivotCamera.MousePosUpdate(cursorX, cursorY);

        static constexpr int CameraMouseButtons[] = {
            GLFW_MOUSE_BUTTON_LEFT,
            GLFW_MOUSE_BUTTON_MIDDLE,
            GLFW_MOUSE_BUTTON_RIGHT
        };

        const bool allowMouse = windowFocused && !mouseCaptured;
        for (int button : CameraMouseButtons)
        {
            const bool physicallyPressed = allowMouse &&
                glfwGetMouseButton(window, button) == GLFW_PRESS;
            if (!firstPersonActive || !physicallyPressed)
                m_FirstPersonCamera.MouseButtonUpdate(button, GLFW_RELEASE, 0);
            if (!thirdPersonActive || !physicallyPressed)
                m_ThirdPersonCamera.MouseButtonUpdate(button, GLFW_RELEASE, 0);
            if (!pivotActive || !physicallyPressed)
                m_PivotCamera.MouseButtonUpdate(button, GLFW_RELEASE, 0);
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
        GetActiveCamera().KeyboardUpdate(key, scancode, action, mods);
        return true;
    }

auto UvsrSceneViewer::MousePosUpdate(double xpos, double ypos) -> bool {
        // Keep all interactive controllers synchronized while inactive. A
        // later press can then begin from the current cursor position instead
        // of applying all motion accumulated since the last mode switch.
        m_FirstPersonCamera.MousePosUpdate(xpos, ypos);
        m_ThirdPersonCamera.MousePosUpdate(xpos, ypos);
        m_PivotCamera.MousePosUpdate(xpos, ypos);

        if (m_MaterialPickPurpose == MaterialPickPurpose::None)
        {
            m_PickPosition =
                uint2(static_cast<uint>(xpos), static_cast<uint>(ypos));
        }

        return true;
    }

auto UvsrSceneViewer::MouseButtonUpdate(int button, int action, int mods) -> bool {
        GetActiveCamera().MouseButtonUpdate(button, action, mods);

        if (action == GLFW_PRESS &&
            button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            // Snapshot the cursor coordinate at the press. Subsequent camera
            // motion cannot slide the pending material-ID readback elsewhere.
            m_MaterialPickPurpose =
                MaterialPickPurpose::FocusCameraAtCursor;
            m_MaterialPickScene = m_Scene.get();
        }

        return true;
    }

auto UvsrSceneViewer::MouseScrollUpdate(double xoffset, double yoffset) -> bool {
        GetActiveCamera().MouseScrollUpdate(xoffset, yoffset);

        return true;
    }

auto UvsrSceneViewer::Animate(float fElapsedTimeSeconds) -> void {
        m_FrameDeltaSeconds = std::isfinite(fElapsedTimeSeconds)
            ? std::clamp(fElapsedTimeSeconds, 0.f, 1.f)
            : 0.f;
        SynchronizeCameraInput();

        switch (m_ui.Camera)
        {
        case CameraMode::ThirdPerson:
        {
            // Freelook combines mouse/arrow look, W/S dolly, and A/D strafe.
            // It moves the eye directly with no orbit target or pivot state.
            const float3 start = m_ThirdPersonCamera.GetPosition();
            m_ThirdPersonCamera.Animate(fElapsedTimeSeconds);

            const float3 desiredPosition = m_ThirdPersonCamera.GetPosition();
            const float3 resolvedPosition = m_CameraCollisionWorld.MoveSphere(
                start, desiredPosition, m_CameraCollisionRadius);
            if (lengthSquared(resolvedPosition - desiredPosition) > 1e-12f)
            {
                // The correction becomes the free-look camera's next origin;
                // its look direction and dolly sensitivity stay unchanged.
                m_ThirdPersonCamera.ApplyCollisionPosition(resolvedPosition);
            }
            break;
        }

        case CameraMode::Pivot:
            m_PivotCamera.Animate(fElapsedTimeSeconds);
            break;

        case CameraMode::Static:
            break;

        case CameraMode::FirstPerson:
        {
            const float3 start = m_FirstPersonCamera.GetPosition();
            m_FirstPersonCamera.Animate(fElapsedTimeSeconds);

            const float3 desiredPosition = m_FirstPersonCamera.GetPosition();
            const float3 resolvedPosition = m_CameraCollisionWorld.MoveSphere(
                start, desiredPosition, m_CameraCollisionRadius);
            if (lengthSquared(resolvedPosition - desiredPosition) > 1e-12f)
            {
                m_FirstPersonCamera.LookTo(
                    resolvedPosition,
                    m_FirstPersonCamera.GetDir(),
                    m_FirstPersonCamera.GetUp());
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
        m_RuntimeOutputCapturePath =
            std::filesystem::temp_directory_path() /
            ("uvsr-retained-runtime-" +
                std::to_string(GetCurrentProcessId())) /
            ("case-" + std::to_string(caseIndex) + "-" + safeName +
                ".bmp");
        m_RuntimeOutputEvidence.reset();
        m_RuntimeOutputCaptureRequested = true;
    }
#endif

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::ConsumeRuntimeOutputEvidence() -> std::optional<RuntimeOutputEvidence> {
        std::optional<RuntimeOutputEvidence> evidence =
            std::move(m_RuntimeOutputEvidence);
        m_RuntimeOutputEvidence.reset();
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
            m_CameraVerticalFov);
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
            m_CameraVerticalFov
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
        return bool(m_SunLight);
    }

auto UvsrSceneViewer::GetPrimaryDirectionalLight() const -> std::shared_ptr<DirectionalLight> {
        return m_SunLight;
    }

auto UvsrSceneViewer::GetEditableLights() const -> const std::vector<std::shared_ptr<Light>>& {
        return m_EditableLights;
    }
