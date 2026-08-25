#include "uvsr_internal.h"

auto UvsrSceneViewer::ApplySceneInitialCamera(const SceneInitialCamera& preset) -> void {
        const float3 position(
            preset.Position[0],
            preset.Position[1],
            preset.Position[2]);
        const float3 direction = normalize(float3(
            preset.Direction[0],
            preset.Direction[1],
            preset.Direction[2]));
        const float3 upHint = normalize(float3(
            preset.Up[0],
            preset.Up[1],
            preset.Up[2]));
        const float3 right = normalize(cross(direction, upHint));
        const float3 up = normalize(cross(right, direction));
        ApplyCameraPose(
            position,
            direction,
            up,
            right,
            preset.VerticalFovDegrees);
    }

auto UvsrSceneViewer::GetAvailableScenes() const -> const std::vector<SceneCatalogEntry>& {
        return m_SceneCatalog;
    }

auto UvsrSceneViewer::GetSceneDir() const -> std::filesystem::path const& {
        return m_SceneDir;
    }

auto UvsrSceneViewer::GetCurrentSceneName() const -> std::string {
        return m_CurrentSceneName;
    }

auto UvsrSceneViewer::GetCurrentSceneDisplayName() const -> std::string {
        if (const SceneCatalogEntry* entry = FindSceneCatalogEntry(m_SceneCatalog, m_CurrentSceneName))
            return entry->DisplayName;

        // Explicit command-line paths are allowed even when they are not in
        // the picker. Preserve the old in-tree relative-path presentation for
        // those scenes and show an external path verbatim.
        return MakeSceneDisplayName(m_SceneDir, m_CurrentSceneName);
    }

auto UvsrSceneViewer::IsSceneLoading() const -> bool {
        return m_SceneRetirementPending ||
            m_SceneLoadWorker.GetState() !=
                RendererSceneLoadWorkerState::Idle;
    }

auto UvsrSceneViewer::IsSceneLoaded() const -> bool {
        return m_RendererSceneLoaded;
    }

auto UvsrSceneViewer::StartPendingSceneLoad() -> void {
        std::shared_ptr<IFileSystem> fileSystem =
            std::move(m_PendingSceneFileSystem);
        std::filesystem::path fileName =
            std::move(m_PendingSceneFileName);
        m_PendingSceneFileSystem.reset();
        m_PendingSceneFileName.clear();
        if (!fileSystem || fileName.empty())
        {
            throw std::runtime_error(
                "UVSR scene loader received an empty pending task");
        }
        if (!m_SceneLoadWorker.Start(
                [this,
                 fileSystem = std::move(fileSystem),
                 fileName = std::move(fileName)]() mutable
                {
                    return LoadScene(
                        std::move(fileSystem), fileName);
                }))
        {
            throw std::runtime_error(
                "UVSR could not start its scene-load worker");
        }
    }

auto UvsrSceneViewer::BeginLoadingScene(
        std::shared_ptr<IFileSystem> fileSystem,
        const std::filesystem::path& sceneFileName) -> void {
        if (!fileSystem || sceneFileName.empty())
        {
            throw std::invalid_argument(
                "UVSR scene loading requires a file system and descriptor");
        }
        if (IsSceneLoading())
        {
            throw std::runtime_error(
                "UVSR cannot replace an active scene-load task");
        }

        m_PendingSceneFileSystem = std::move(fileSystem);
        m_PendingSceneFileName = sceneFileName;
        m_RendererSceneLoaded = false;
        m_SceneLoadFailure.clear();
        if (m_HasRendererSceneResources)
        {
            if (!m_SceneRetirement.Begin())
            {
                throw std::runtime_error(
                    "UVSR could not arm scene GPU retirement");
            }
            m_SceneRetirementPending = true;
            return;
        }

        if (m_TextureCache)
            m_TextureCache->Reset();
        GetDevice()->runGarbageCollection();
        StartPendingSceneLoad();
    }

auto UvsrSceneViewer::SetCurrentSceneName(const std::string& sceneName) -> void {
        const SceneCatalogEntry* catalogEntry = FindSceneCatalogEntry(m_SceneCatalog, sceneName);
        const std::string resolvedSceneName = catalogEntry ? catalogEntry->FileName : sceneName;
        if (m_CurrentSceneName == resolvedSceneName)
            return;

		m_CurrentSceneName = resolvedSceneName;

		BeginLoadingScene(m_NativeFs, m_CurrentSceneName);
    }

auto UvsrSceneViewer::RetryCurrentSceneLoad() -> void {
        if (IsSceneBusy() || m_CurrentSceneName.empty())
            return;
        BeginLoadingScene(m_NativeFs, m_CurrentSceneName);
    }

auto UvsrSceneViewer::HasSceneLoadFailure() const noexcept -> bool {
        return !m_SceneLoadFailure.empty();
    }

auto UvsrSceneViewer::GetSceneLoadFailure() const noexcept -> const std::string& {
        return m_SceneLoadFailure;
    }

auto UvsrSceneViewer::SceneUnloading() -> void {
        m_SceneFinishedLoading = false;
        m_SceneGpuUploadPending = false;
        m_ScenePreparationStage = ScenePreparationStage::Complete;
        m_RenderPassPreparationStage =
            RenderPassPreparationStage::Idle;
        if (m_PbrDeferredLightingPass) m_PbrDeferredLightingPass->ResetBindingCache();
        if (m_DirectionalRayVisibilityPass)
            m_DirectionalRayVisibilityPass->ResetBindingCache();
        if (m_RayTracedFlashlightShadowPass)
            m_RayTracedFlashlightShadowPass->ResetBindingCache();
        if (m_RayTracedSkyVisibilityPass)
            m_RayTracedSkyVisibilityPass->ResetBindingCache();
        if (m_WorldSpaceRepresentation)
            m_WorldSpaceRepresentation->Reset();
        if (m_PathTracingPass)
        {
            m_PathTracingPass->ResetHistory();
            m_PathTracingPass->ResetBindingCache();
        }
        if (m_LightingAccumulationPass)
        {
            m_LightingAccumulationPass->ResetHistory();
            m_LightingAccumulationPass->ResetBindingCache();
        }
        if (m_ScreenSpaceVisibilityPass)
            m_ScreenSpaceVisibilityPass->ResetBindingCache();
        if (m_AutoExposurePass)
            m_AutoExposurePass->Reset();
        ResetAntiAliasingState();
        if (m_GBufferGeometryPass)
            m_GBufferGeometryPass->ResetBindingCache();
        if (m_MaterialIdGeometryPass)
            m_MaterialIdGeometryPass->ResetBindingCache();
        m_BindingCache.Clear();
        m_Flashlight.reset();
        m_FlashlightNode.reset();
        m_SceneLightsWithoutFlashlight.clear();
        m_EditableLights.clear();
        ResetFlashlightMotion();
        m_SunLight.reset();
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;
        m_ui.ShowMaterialDrawer = false;
        m_MaterialPickPurpose = MaterialPickPurpose::None;
        m_MaterialPickScene = nullptr;
        m_OriginalMaterials.clear();
        m_PreviousView.reset();
        // Move the large vectors without freeing them here. The next loader
        // worker releases this retired world before allocating its replacement,
        // keeping hundreds of megabytes of allocator work off the render thread.
        m_RetiredCameraCollisionWorld.emplace(
            std::move(m_CameraCollisionWorld));
        m_CameraCollisionWorld = CameraCollisionWorld{};
        m_PendingSceneCpuState.reset();
        m_SubmittedMainViewTriangles = 0u;
        m_Scene.reset();

    }

auto UvsrSceneViewer::LoadScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& fileName) -> bool {
        using namespace std::chrono;

        // SceneUnloading transfers the previous BVH here so its large vector
        // allocations are released by the loader rather than by a present
        // frame. This also lowers the peak before the replacement is built.
        m_RetiredCameraCollisionWorld.reset();
        m_PendingSceneCpuState.reset();

        std::unique_ptr<engine::Scene> scene = std::make_unique<engine::Scene>(GetDevice(),
            *m_ShaderFactory, fs, m_TextureCache, m_DescriptorTable, nullptr);

        const auto startTime = high_resolution_clock::now();
        const uint32_t workerCount = ResolveSceneLoadWorkerCount(
            std::thread::hardware_concurrency());
        engine::ThreadPool threadPool(workerCount);
        uvsr::log::info(
            "Scene import is using %u background workers",
            workerCount);

        if (scene->LoadWithThreadPool(fileName, &threadPool))
        {
            const auto importFinished = high_resolution_clock::now();

            // The scene is private to this worker, so transforms and CPU
            // importer arrays can be consumed without synchronizing with the
            // renderer. FinishedLoading later frees these arrays after their
            // bounded GPU upload completes.
            scene->RefreshSceneGraph(0u);
            const box3 loadedSceneBounds = scene->GetSceneGraph()
                ->GetRootNode()->GetGlobalBoundingBox();
            PreparedSceneCpuState prepared;
            prepared.sceneDiagonal = std::max(
                length(loadedSceneBounds.diagonal()),
                100.f);
            prepared.collisionRadius = std::max(
                0.1f,
                prepared.sceneDiagonal * 0.0005f);
            prepared.collisionWorld = BuildCameraCollisionWorld(
                *scene,
                prepared.collisionRadius);

            if (m_ImageBasedLightingEnvironment &&
                !m_ImageBasedLightingEnvironment->GetRadianceTexture())
            {
                prepared.environmentRadiance =
                    m_ImageBasedLightingEnvironment->PrepareRadiance(
                        m_ui.EnvironmentSource,
                        m_ui.WhiteWorld != WhiteWorldMode::Off);
            }

            // ApplicationBase publishes the LoadScene return value through an
            // atomic completion flag. Write the complete handoff before that
            // release so SceneLoaded never observes a partial scene state.
            m_PendingSceneCpuState.emplace(std::move(prepared));
            m_Scene = std::move(scene);

            const auto endTime = high_resolution_clock::now();
            const auto importDuration = duration_cast<milliseconds>(
                importFinished - startTime).count();
            const auto preparationDuration = duration_cast<milliseconds>(
                endTime - importFinished).count();
            uvsr::log::info(
                "Scene worker completed import in %lld ms and CPU preparation in %lld ms",
                static_cast<long long>(importDuration),
                static_cast<long long>(preparationDuration));

            return true;
        }

        m_PendingSceneCpuState.reset();
        return false;
    }

auto UvsrSceneViewer::SceneLoaded() -> void {
        if (!m_PendingSceneCpuState || !m_Scene)
        {
            throw std::runtime_error(
                "Scene worker completed without a prepared CPU handoff");
        }

        // The render thread drained all texture finalization before joining
        // the successful worker. Close the retained TextureCache load before
        // publishing any scene-owned renderer state.
        if (m_TextureCache)
        {
            m_TextureCache->ProcessRenderingThreadCommands(
                *m_CommonPasses, 0.f);
            m_TextureCache->LoadingFinished();
        }
        m_RendererSceneLoaded = true;
        m_HasRendererSceneResources = true;
        m_SceneLoadFailure.clear();

        m_CameraCollisionWorld = std::move(
            m_PendingSceneCpuState->collisionWorld);
        ResetFlashlightMotion();
        m_SceneDiagonal = m_PendingSceneCpuState->sceneDiagonal;
        m_CameraCollisionRadius =
            m_PendingSceneCpuState->collisionRadius;
        if (m_ImageBasedLightingEnvironment &&
            m_PendingSceneCpuState->environmentRadiance)
        {
            m_ImageBasedLightingEnvironment->StagePreparedRadiance(
                std::move(
                    *m_PendingSceneCpuState->environmentRadiance));
        }
        m_PendingSceneCpuState.reset();

        m_Scene->BeginLoadingBuffers();
        m_SceneGpuUploadPending = true;
        m_ScenePreparationStage = ScenePreparationStage::MeshUpload;
        m_SceneGpuUploadStart =
            std::chrono::high_resolution_clock::now();
    }

auto UvsrSceneViewer::CompleteSceneActivation() -> void {

        InvalidateLightingAccumulationHistory();
        m_HasLightingHistorySignatures = false;
        m_LightingHistoryChangedByViewOnly = false;
        if (m_PathTracingPass)
            m_PathTracingPass->ResetHistory();
        if (m_LightingAccumulationPass)
            m_LightingAccumulationPass->ResetHistory();

        m_OriginalMaterials.clear();
        for (const auto& material : m_Scene->GetSceneGraph()->GetMaterials())
            m_OriginalMaterials.emplace_back(material, *material);
        SetWhiteWorldMode(m_ui.WhiteWorld);

        for (auto light : m_Scene->GetSceneGraph()->GetLights())
        {
            const std::string normalizedLightName =
                NormalizeSceneLightName(light->GetName());
            if (normalizedLightName != light->GetName())
                light->SetName(normalizedLightName);

            if (!m_SunLight &&
                light->GetLightType() == UVSR_LIGHT_TYPE_DIRECTIONAL)
            {
                m_SunLight = std::static_pointer_cast<DirectionalLight>(light);
                m_SunLight->irradiance = DefaultSunIrradiance;
                m_SunLight->angularSize = DefaultSunAngularSizeDegrees;
            }
        }

        if (!m_SunLight)
        {
            m_SunLight = std::make_shared<DirectionalLight>();
            m_SunLight->angularSize = DefaultSunAngularSizeDegrees;
            m_SunLight->irradiance = DefaultSunIrradiance;

            auto node = std::make_shared<SceneGraphNode>();
            node->SetLeaf(m_SunLight);
            m_SunLight->SetDirection(dm::double3(0.1, -0.9, 0.1));
            m_SunLight->SetName("sun_1");
            m_Scene->GetSceneGraph()->Attach(m_Scene->GetSceneGraph()->GetRootNode(), node);
        }

        AttachFlashlightToScene();
        m_Scene->RefreshSceneGraph(GetFrameIndex());
        m_SceneLightsWithoutFlashlight.clear();
        m_EditableLights.clear();
        if (m_Flashlight)
            m_EditableLights.push_back(m_Flashlight);
        for (const auto& light :
            m_Scene->GetSceneGraph()->GetLights())
        {
            if (light && light != m_Flashlight)
            {
                m_SceneLightsWithoutFlashlight.push_back(light);
                m_EditableLights.push_back(light);
            }
        }

        const SceneCatalogEntry* currentCatalogEntry =
            FindSceneCatalogEntry(m_SceneCatalog, m_CurrentSceneName);
        const SceneInitialCamera* sceneInitialCamera =
            currentCatalogEntry && currentCatalogEntry->InitialCamera
            ? &*currentCatalogEntry->InitialCamera
            : nullptr;
        if (sceneInitialCamera)
            m_CameraVerticalFov = sceneInitialCamera->VerticalFovDegrees;
        else
            m_CameraVerticalFov = 60.f;

        std::shared_ptr<SceneGraphNode> cameraTarget = m_Scene->GetSceneGraph()->GetRootNode();
        // Prefer the compact asteroid core when present so the initial view
        // includes the full rocky platform instead of tightly framing only the
        // temple. Older Jungle Ruins exports retain the pyramid marker fallback.
        float cameraDistanceScale = 1.f;
        if (auto asteroid = FindDescendantByName(cameraTarget, "UVSR_AsteroidCore"))
        {
            cameraTarget = asteroid;
            cameraDistanceScale = 1.45f;
        }
        else if (auto pyramid = FindDescendantByName(cameraTarget, "Pyramid_EmitterShell"))
            cameraTarget = pyramid;
        PointThirdPersonCameraAt(cameraTarget, cameraDistanceScale, true);

        if (sceneInitialCamera)
        {
            ApplySceneInitialCamera(*sceneInitialCamera);
            uvsr::log::info(
                "Applied descriptor initial camera to '%s' at %.3f, %.3f, %.3f and %.1f degrees vertical FOV",
                m_CurrentSceneName.c_str(),
                sceneInitialCamera->Position[0],
                sceneInitialCamera->Position[1],
                sceneInitialCamera->Position[2],
                sceneInitialCamera->VerticalFovDegrees);
        }

        m_ui.Camera = CameraMode::ThirdPerson;

        if (!sceneInitialCamera)
        {
            const float3 initialPosition = m_ThirdPersonCamera.GetPosition();
            const float3 initialDirection = m_ThirdPersonCamera.GetDir();
            const float3 initialUp = m_ThirdPersonCamera.GetUp();
            m_FirstPersonCamera.LookTo(initialPosition, initialDirection, initialUp);
            m_PivotCamera.LookTo(initialPosition, initialDirection, initialUp);
            m_StaticCamera.LookTo(initialPosition, initialDirection, initialUp);
        }

        m_SceneFinishedLoading = true;

    }

auto UvsrSceneViewer::SetWhiteWorldMode(WhiteWorldMode mode) -> void {
        const bool modeChanged = m_ui.WhiteWorld != mode;
        const bool shaderModeChanged = (m_ui.WhiteWorld == WhiteWorldMode::Off) !=
            (mode == WhiteWorldMode::Off);
        m_ui.WhiteWorld = mode;

        if (modeChanged)
            ResetImageBasedLightingHistory();

        const bool enabled = mode != WhiteWorldMode::Off;
        const bool preserveDetailMaps = mode == WhiteWorldMode::PreserveDetail;
        const bool preserveLighting = mode == WhiteWorldMode::PreserveLighting;

        if (!m_Scene)
            return;

        for (auto& [material, original] : m_OriginalMaterials)
        {
            *material = original;

            if (enabled)
            {
                const bool originalUsesAlpha =
                    original.domain == MaterialDomain::AlphaTested ||
                    original.domain == MaterialDomain::AlphaBlended ||
                    original.domain == MaterialDomain::TransmissiveAlphaTested ||
                    original.domain == MaterialDomain::TransmissiveAlphaBlended;
                const bool hasSeparateOpacity = originalUsesAlpha &&
                    original.enableOpacityTexture && original.opacityTexture;
                const bool hasBaseAlpha = originalUsesAlpha && !hasSeparateOpacity &&
                    original.enableBaseOrDiffuseTexture && original.baseOrDiffuseTexture;

                // Preserve the coverage source but normalize all alpha domains
                // to depth-writing alpha test. WHITE_WORLD shader permutations
                // replace sampled RGB with white before material evaluation.
                material->domain = originalUsesAlpha
                    ? MaterialDomain::AlphaTested
                    : MaterialDomain::Opaque;
                material->useSpecularGlossModel = false;
                material->baseOrDiffuseColor = dm::float3(1.f);
                material->specularColor = dm::float3(0.04f);
                material->emissiveColor = preserveLighting
                    ? original.emissiveColor
                    : dm::float3(0.f);
                material->emissiveIntensity = preserveLighting
                    ? original.emissiveIntensity
                    : 1.f;
                material->metalness = 0.f;
                material->roughness = 0.72f;
                material->opacity = originalUsesAlpha ? original.opacity : 1.f;
                material->alphaCutoff = originalUsesAlpha
                    ? std::clamp(original.alphaCutoff, 0.01f, 0.99f)
                    : 0.5f;
                material->transmissionFactor = 0.f;
                material->enableBaseOrDiffuseTexture = hasBaseAlpha;
                material->enableMetalRoughOrSpecularTexture = false;
                material->enableEmissiveTexture =
                    preserveLighting && original.enableEmissiveTexture;
                material->enableTransmissionTexture = false;
                material->enableOpacityTexture = hasSeparateOpacity;
                material->enableNormalTexture = preserveDetailMaps && original.enableNormalTexture;
                material->enableOcclusionTexture =
                    preserveDetailMaps && original.enableOcclusionTexture;
                material->enableSubsurfaceScattering = false;
                material->enableHair = false;
            }

            ApplyPbrMaterialParameters(*material);
        }

        m_Scene->GetSceneGraph()->GetRootNode()->InvalidateContent();
        if (shaderModeChanged)
            m_ui.ShaderReloadRequested = true;
    }

auto UvsrSceneViewer::FindDescendantByName(
        const std::shared_ptr<SceneGraphNode>& node,
        const std::string& name) -> std::shared_ptr<SceneGraphNode> {
        if (!node || node->GetName() == name)
            return node;

        for (size_t childIndex = 0; childIndex < node->GetNumChildren(); ++childIndex)
        {
            SceneGraphNode* child = node->GetChild(childIndex);
            if (!child)
                continue;

            if (auto found = FindDescendantByName(child->shared_from_this(), name))
                return found;
        }

        return nullptr;
    }

auto UvsrSceneViewer::PointThirdPersonCameraAt(
        const std::shared_ptr<SceneGraphNode>& node,
        float distanceScale ,
        bool resetOrientation ) -> void {
        if (!node)
            return;

        dm::box3 bounds = node->GetGlobalBoundingBox();
        if (bounds.isempty()
            || !all(dm::isfinite(bounds.m_mins))
            || !all(dm::isfinite(bounds.m_maxs)))
            return;

        float radius = length(bounds.diagonal()) * 0.5f;
        float distance = radius * distanceScale / sinf(dm::radians(m_CameraVerticalFov * 0.5f));
        if (!std::isfinite(distance) || distance <= 0.f)
            return;

        if (resetOrientation)
        {
            // Reuse Donut's established orbit framing math only to calculate
            // the initial eye pose. Runtime Freelook is a free-moving camera
            // and retains no pivot or orbit state from this temporary object.
            ThirdPersonCamera framingCamera;
            framingCamera.SetRotation(dm::radians(135.f), dm::radians(20.f));
            framingCamera.SetTargetPosition(bounds.center());
            framingCamera.SetDistance(distance);
            framingCamera.Animate(0.f);
            m_ThirdPersonCamera.LookTo(
                framingCamera.GetPosition(),
                framingCamera.GetDir(),
                framingCamera.GetUp());
        }
        else
        {
            const float3 direction = m_ThirdPersonCamera.GetDir();
            const float3 up = m_ThirdPersonCamera.GetUp();
            m_ThirdPersonCamera.LookTo(
                bounds.center() - direction * distance,
                direction,
                up);
        }
        m_ThirdPersonCamera.ResetZoomReferenceDistance(distance);
        // Framing a picked node is another camera teleport. Start the mounted
        // emitter at this new pose instead of sweeping it across the scene.
        ResetFlashlightMotion();
    }

auto UvsrSceneViewer::GetTextureCache() -> std::shared_ptr<TextureCache> {
        return m_TextureCache;
    }

auto UvsrSceneViewer::IsSceneBusy() const -> bool {
        return IsSceneLoading() || m_SceneGpuUploadPending;
    }

auto UvsrSceneViewer::IsSceneGpuUploadPending() const -> bool {
        return m_SceneGpuUploadPending;
    }

auto UvsrSceneViewer::GetScene() -> std::shared_ptr<Scene> {
        return m_Scene;
    }

auto UvsrSceneViewer::SetMaterialDrawerVisible(bool visible) -> void {
        const bool centerPickPending =
            m_MaterialPickPurpose ==
                MaterialPickPurpose::RefreshMaterialDrawerSelection;
        if (!visible)
        {
            m_ui.ShowMaterialDrawer = false;
            if (centerPickPending)
            {
                m_MaterialPickPurpose = MaterialPickPurpose::None;
                m_MaterialPickScene = nullptr;
            }
            return;
        }

        m_ui.ShowUI = true;
        m_ui.ShowMaterialDrawer = true;
        if (!m_Scene || IsSceneBusy())
            return;

        // Never reveal the previous click selection while a fresh center sample
        // is pending. A miss leaves the drawer open with its aiming guidance.
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;
        m_MaterialPickPurpose =
            MaterialPickPurpose::RefreshMaterialDrawerSelection;
        m_MaterialPickScene = m_Scene.get();
    }

auto UvsrSceneViewer::GetOriginalMaterial(
        const std::shared_ptr<Material>& material) const -> const Material* {
        const auto original = std::find_if(
            m_OriginalMaterials.begin(),
            m_OriginalMaterials.end(),
            [&material](const auto& entry)
            {
                return entry.first == material;
            });
        return original != m_OriginalMaterials.end()
            ? &original->second
            : nullptr;
    }

auto UvsrSceneViewer::NotifyMaterialCommandChanged(
        const std::shared_ptr<Material>& material) -> void {
        if (!material)
            return;
        material->dirty = true;
        if (m_Scene && m_Scene->GetSceneGraph() &&
            m_Scene->GetSceneGraph()->GetRootNode())
        {
            m_Scene->GetSceneGraph()->GetRootNode()->
                InvalidateContent();
        }
        ResetImageBasedLightingHistory();
    }

auto UvsrSceneViewer::RecordLoadingPresentationFrame() -> void {
        const auto now = std::chrono::steady_clock::now();
        if (m_LoadingPresentationFrameCount > 0u)
        {
            const double gapMilliseconds =
                std::chrono::duration<double, std::milli>(
                    now - m_LastLoadingPresentationFrame).count();
            m_MaximumLoadingPresentationGapMs = std::max(
                m_MaximumLoadingPresentationGapMs,
                gapMilliseconds);
        }
        m_LastLoadingPresentationFrame = now;
        ++m_LoadingPresentationFrameCount;
    }

auto UvsrSceneViewer::RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) -> void {
        RecordLoadingPresentationFrame();
        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->open();
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
    }

auto UvsrSceneViewer::PrepareLoadingRenderTargets(nvrhi::IFramebuffer* framebuffer) -> bool {
        const nvrhi::FramebufferInfoEx& framebufferInfo =
            framebuffer->getFramebufferInfo();
        if (framebufferInfo.width == 0u || framebufferInfo.height == 0u)
            return false;

        const DirectX::XMUINT2 presentationSize(
            framebufferInfo.width,
            framebufferInfo.height);
        const MsaaRasterTopology msaaTopology = ResolveSupportedMsaaTopology(
            GetDevice(),
            m_ui.GetResolvedAntiAliasingSettings().rasterSampleCount);
        const MsaaRenderExtent renderExtent = ScaleMsaaRenderExtent(
            presentationSize.x,
            presentationSize.y,
            msaaTopology.linearResolutionScale);
        if (!msaaTopology || !renderExtent)
            return false;
        const DirectX::XMUINT2 renderSize(
            renderExtent.width,
            renderExtent.height);
        const uint32_t sampleCount = msaaTopology.rasterSampleCount;
        const bool screenSpaceVisibilityResourcesRequired =
            m_ui.HasActiveScreenSpaceVisibilityConsumer();
        const bool msaaClosestSurfaceResolveResourcesRequired =
            sampleCount > 1u;
        const bool visibilityResourcesRequired =
            screenSpaceVisibilityResourcesRequired ||
            msaaClosestSurfaceResolveResourcesRequired;
        const bool submittedLightingAvailable =
            !m_SceneLightsWithoutFlashlight.empty() ||
            (ShouldSubmitFlashlight(m_FlashlightTransition) &&
                bool(m_Flashlight));
        const bool visibilitySourceRadianceRequired =
            screenSpaceVisibilityResourcesRequired &&
            m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
            (submittedLightingAvailable ||
                IsAmbientFillLobeActive(
                    m_ui.EnableAmbientFill,
                    m_ui.EnableDiffuseIbl,
                    m_ui.DiffuseIblStrength));
        const bool motionVectorsRequired =
            m_ui.UsesLongTermTemporalAA() ||
            (visibilityResourcesRequired && sampleCount > 1u);

        bool needNewPasses = false;
        if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(
                renderSize,
                sampleCount,
                presentationSize,
                msaaTopology.presentationSampleCount,
                visibilityResourcesRequired,
                visibilitySourceRadianceRequired,
                motionVectorsRequired))
        {
            m_RenderTargets.reset();
            m_BindingCache.Clear();
            m_RenderTargets = std::make_unique<RenderTargets>();
            if (!m_RenderTargets->Init(
                    GetDevice(),
                    renderSize,
                    sampleCount,
                    presentationSize,
                    msaaTopology.presentationSampleCount,
                    motionVectorsRequired,
                    true,
                    visibilityResourcesRequired,
                    visibilitySourceRadianceRequired))
            {
                throw std::runtime_error(
                    "UVSR loading render targets failed to initialize");
            }
            m_PreviousView.reset();
            needNewPasses = true;
        }

        if (SetupView())
        {
            needNewPasses = true;
            m_PreviousView.reset();
        }

        if (needNewPasses || !m_GBufferGeometryPass ||
            !m_MaterialIdGeometryPass || !m_AutoExposurePass ||
            !m_AgxToneMappingPass)
        {
            BeginRenderPassPreparation(true);
        }
        else
        {
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::Complete;
        }
        return true;
    }

auto UvsrSceneViewer::RenderSceneGpuUploadFrame(nvrhi::IFramebuffer* framebuffer) -> void {
        RecordLoadingPresentationFrame();
        if (m_ScenePreparationStage == ScenePreparationStage::RenderPasses &&
            ProcessRenderPassPreparationStep())
        {
            m_ScenePreparationStage = ScenePreparationStage::Complete;
        }

        nvrhi::ITexture* framebufferTexture =
            framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->open();
        m_CommandList->clearTextureFloat(
            framebufferTexture,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        switch (m_ScenePreparationStage)
        {
        case ScenePreparationStage::MeshUpload:
            if (m_Scene->ProcessLoadingBuffers(
                    m_CommandList,
                    c_SceneUploadBytesPerFrame,
                    GetFrameIndex()))
            {
                // Activation has its own loading frame so its material, light,
                // and camera setup cannot stack on the final mesh-buffer work.
                m_ScenePreparationStage =
                    ScenePreparationStage::SceneActivation;
            }
            break;

        case ScenePreparationStage::SceneActivation:
            CompleteSceneActivation();
            m_ScenePreparationStage =
                ScenePreparationStage::MaterialBuffers;
            break;

        case ScenePreparationStage::MaterialBuffers:
            // Activation applies the renderer's PBR defaults and can attach
            // its fallback lights, which dirties Donut's material and scene
            // buffers after the importer's final refresh. Consume those
            // writes on their own loading frame so the first visible scene
            // frame does not inherit the whole update.
            m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());
            m_ScenePreparationStage =
                ScenePreparationStage::WorldRepresentation;
            break;

        case ScenePreparationStage::WorldRepresentation:
        {
            const bool worldRepresentationRequested =
                m_ui.Representation.allowRayTraversal &&
                m_BindlessLayout &&
                ((m_ui.Lighting == LightingSolution::PathTracing &&
                    GetPathTracingSceneDomainStatus() !=
                        PathTracingSceneDomainStatus::Unsupported) ||
                (m_ui.DirectionalShadows.enabled &&
                    SupportsDirectionalRayVisibility()) ||
                (m_ui.FlashlightEnabled &&
                    m_ui.Flashlight.castShadows &&
                    m_Flashlight &&
                    m_BindlessLayout &&
                    RayTracedFlashlightShadowPass::IsDeviceSupported(
                        GetDevice())) ||
                (m_ui.RayTracedSkyVisibility.enabled &&
                    (HasRayTracedSkyVisibilityConsumer(
                            m_ui.RayTracedSkyVisibility) ||
                        (m_ui.Lighting ==
                                LightingSolution::RayMarching &&
                            m_ui.LightingDebugView ==
                                PbrLightingDebugView::SkyVisibility)) &&
                    SupportsRayTracedSkyVisibility()));
            if (!worldRepresentationRequested ||
                !m_WorldSpaceRepresentation ||
                !m_WorldSpaceRepresentation->IsSupported() ||
                m_WorldSpaceRepresentation->GetStatus().state ==
                    WorldSpaceRepresentationState::Failed ||
                m_WorldSpaceRepresentation->Update(
                    m_CommandList,
                    m_Scene.get(),
                    m_ui.Representation,
                    uint32_t(GetFrameIndex()),
                    true))
            {
                m_ScenePreparationStage =
                    ScenePreparationStage::RenderTargets;
            }
            break;
        }

        case ScenePreparationStage::RenderTargets:
            if (PrepareLoadingRenderTargets(framebuffer))
            {
                m_ScenePreparationStage =
                    ScenePreparationStage::RenderPasses;
            }
            break;

        case ScenePreparationStage::RenderPasses:
            break;

        case ScenePreparationStage::Complete:
            break;
        }

        // Consume worker-prepared HDR data one GPU unit per loading frame.
        // Partially generated environment maps are not exposed to rendering.
        UpdateImageBasedLighting(m_CommandList);
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

        if (m_ImageBasedLightingEnvironment &&
            m_ImageBasedLightingEnvironment->HasPreparedRadianceFailed())
        {
            uvsr::log::error(
                "Required image-based lighting preparation failed");
            GetDeviceManager()->ReportRenderDisposition(
                uvsr::RendererRenderDisposition::Failed);
            return;
        }

        const bool environmentReady =
            !m_ImageBasedLightingEnvironment ||
            m_ImageBasedLightingEnvironment->IsPreparedRadianceReady();
        if (m_ScenePreparationStage == ScenePreparationStage::Complete &&
            environmentReady)
        {
            m_SceneGpuUploadPending = false;
            const auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() -
                    m_SceneGpuUploadStart).count();
            uvsr::log::info(
                "Staged scene upload and renderer preparation completed in %lld ms across %llu loading frames (maximum presentation gap %.2f ms)",
                static_cast<long long>(duration),
                static_cast<unsigned long long>(
                    m_LoadingPresentationFrameCount),
                m_MaximumLoadingPresentationGapMs);
            m_LastLoadingPresentationFrame = {};
            m_MaximumLoadingPresentationGapMs = 0.0;
            m_LoadingPresentationFrameCount = 0u;
        }
    }

auto UvsrSceneViewer::Render(nvrhi::IFramebuffer* framebuffer) -> void {
        if (m_SceneRetirementPending)
        {
            const RendererSceneRetirementStatus status =
                m_SceneRetirement.Poll();
            if (status == RendererSceneRetirementStatus::Pending)
            {
                RenderSplashScreen(framebuffer);
                return;
            }
            if (status != RendererSceneRetirementStatus::Ready)
            {
                throw std::runtime_error(
                    "UVSR scene retirement lost its pending state");
            }

            SceneUnloading();
            if (m_TextureCache)
                m_TextureCache->Reset();
            GetDevice()->runGarbageCollection();
            m_HasRendererSceneResources = false;
            if (!m_SceneRetirement.Consume())
            {
                throw std::runtime_error(
                    "UVSR scene retirement could not be consumed");
            }
            m_SceneRetirementPending = false;
            StartPendingSceneLoad();
            RenderSplashScreen(framebuffer);
            return;
        }

        RendererSceneLoadWorkerState workerState =
            m_SceneLoadWorker.GetState();
        bool processedTexture = false;
        if (workerState == RendererSceneLoadWorkerState::Running ||
            workerState == RendererSceneLoadWorkerState::Succeeded)
        {
            if (m_TextureCache)
            {
                // Texture creation remains a render-thread responsibility.
                // Bound it per splash frame and join only after the successful
                // worker has published its CPU handoff and the queue drains.
                processedTexture =
                    m_TextureCache->ProcessRenderingThreadCommands(
                        *m_CommonPasses, 4.f);
            }
            workerState = m_SceneLoadWorker.GetState();
        }

        if (workerState == RendererSceneLoadWorkerState::Failed)
        {
            const std::exception_ptr failure =
                m_SceneLoadWorker.GetException();
            (void)m_SceneLoadWorker.Join();
            m_SceneLoadFailure =
                "The scene importer returned a failure result.";
            if (failure)
            {
                try
                {
                    std::rethrow_exception(failure);
                }
                catch (const std::exception& error)
                {
                    m_SceneLoadFailure = error.what();
                    uvsr::log::error(
                        "Scene worker failed: %s", error.what());
                }
                catch (...)
                {
                    m_SceneLoadFailure =
                        "The scene importer threw an unknown exception.";
                    uvsr::log::error(
                        "Scene worker failed with an unknown exception");
                }
            }
            else
            {
                uvsr::log::error(
                    "Scene worker rejected the scene descriptor");
            }
            m_SceneLoadWorker.Reset();
            m_PendingSceneCpuState.reset();
            m_RendererSceneLoaded = false;
            if (m_TextureCache)
                m_TextureCache->Reset();
            GetDevice()->runGarbageCollection();
            RenderSplashScreen(framebuffer);
            return;
        }

        if (workerState == RendererSceneLoadWorkerState::Running ||
            (workerState == RendererSceneLoadWorkerState::Succeeded &&
                processedTexture))
        {
            RenderSplashScreen(framebuffer);
            return;
        }

        if (workerState == RendererSceneLoadWorkerState::Succeeded)
        {
            if (!m_SceneLoadWorker.Join())
            {
                throw std::runtime_error(
                    "UVSR successful scene worker failed while joining");
            }
            m_SceneLoadWorker.Reset();
            SceneLoaded();
        }

        if (!m_RendererSceneLoaded)
        {
            RenderSplashScreen(framebuffer);
            return;
        }
        RenderScene(framebuffer);
    }
