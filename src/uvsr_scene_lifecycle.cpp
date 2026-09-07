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
#include "pbr_material.h"
#include "scene_loading.h"
#include "scene_light_names.h"
#include <donut/engine/TextureCache.h>
#include <donut/engine/ThreadPool.h>
#include <donut/app/UserInterfaceUtils.h>
#include <thread>
#include <exception>

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;

namespace
{
constexpr float DefaultSunIrradiance = 8.f;
constexpr float DefaultSunAngularSizeDegrees = 0.2f;
void ApplyPbrMaterialParameters(Material& material, float ior = 1.5f)
{
    PbrMaterialParameters parameters;
    parameters.baseColor = material.baseOrDiffuseColor;
    parameters.metalness = material.metalness;
    parameters.perceptualRoughness = material.roughness;
    parameters.ior = ior;
    parameters.emissive = material.emissiveColor * std::max(material.emissiveIntensity, 0.f);
    parameters.opacity = material.opacity;
    if (material.enableSubsurfaceScattering)
    {
        parameters.featureMask |= uint8_t(PbrMaterialFeature::Translucency);
        parameters.featureMask |= uint8_t(PbrMaterialFeature::Scattering);
    }
    if (material.transmissionFactor > 0.f)
        parameters.featureMask |= uint8_t(PbrMaterialFeature::Refraction);

    ValidatePbrMaterialParameters(parameters);
    material.baseOrDiffuseColor = parameters.baseColor;
    material.metalness = parameters.metalness;
    material.roughness = parameters.perceptualRoughness;
    material.emissiveColor = parameters.emissive;
    material.emissiveIntensity = 1.f;
    material.opacity = parameters.opacity;

    // Donut does not consume specularColor in its metallic-roughness workflow,
    // so UVSR uses that existing uploaded field for the dielectric F0 scalar.
    if (!material.useSpecularGlossModel)
        material.specularColor = float3(PbrIorToF0(parameters.ior));

    material.dirty = true;
}

}

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
        return m_scene->sceneCatalog;
    }

auto UvsrSceneViewer::GetSceneDir() const -> std::filesystem::path const& {
        return m_scene->sceneDir;
    }

auto UvsrSceneViewer::GetCurrentSceneName() const -> std::string {
        return m_scene->currentSceneName;
    }

auto UvsrSceneViewer::GetCurrentSceneDisplayName() const -> std::string {
        if (const SceneCatalogEntry* entry = FindSceneCatalogEntry(m_scene->sceneCatalog, m_scene->currentSceneName))
            return entry->DisplayName;

        // Explicit command-line paths are allowed even when they are not in
        // the picker. Preserve the old in-tree relative-path presentation for
        // those scenes and show an external path verbatim.
        return MakeSceneDisplayName(m_scene->sceneDir, m_scene->currentSceneName);
    }

auto UvsrSceneViewer::IsSceneLoading() const -> bool {
        return m_scene->sceneRetirementPending ||
            m_scene->sceneLoadWorker.GetState() !=
                RendererSceneLoadWorkerState::Idle;
    }

auto UvsrSceneViewer::IsSceneLoaded() const -> bool {
        return m_scene->rendererSceneLoaded;
    }

auto UvsrSceneViewer::StartPendingSceneLoad() -> void {
        std::shared_ptr<IFileSystem> fileSystem =
            std::move(m_scene->pendingSceneFileSystem);
        std::filesystem::path fileName =
            std::move(m_scene->pendingSceneFileName);
        m_scene->pendingSceneFileSystem.reset();
        m_scene->pendingSceneFileName.clear();
        if (!fileSystem || fileName.empty())
        {
            throw std::runtime_error(
                "UVSR scene loader received an empty pending task");
        }
        if (!m_scene->sceneLoadWorker.Start(
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

        m_scene->pendingSceneFileSystem = std::move(fileSystem);
        m_scene->pendingSceneFileName = sceneFileName;
        m_scene->rendererSceneLoaded = false;
        m_scene->sceneLoadFailure.clear();
        if (m_scene->hasRendererSceneResources)
        {
            if (!m_scene->sceneRetirement.Begin())
            {
                throw std::runtime_error(
                    "UVSR could not arm scene GPU retirement");
            }
            m_scene->sceneRetirementPending = true;
            return;
        }

        if (m_TextureCache)
            m_TextureCache->Reset();
        GetDevice()->runGarbageCollection();
        StartPendingSceneLoad();
    }

auto UvsrSceneViewer::SetCurrentSceneName(const std::string& sceneName) -> void {
        const SceneCatalogEntry* catalogEntry = FindSceneCatalogEntry(m_scene->sceneCatalog, sceneName);
        const std::string resolvedSceneName = catalogEntry ? catalogEntry->FileName : sceneName;
        if (m_scene->currentSceneName == resolvedSceneName)
            return;

		m_scene->currentSceneName = resolvedSceneName;

		BeginLoadingScene(m_scene->nativeFs, m_scene->currentSceneName);
    }

auto UvsrSceneViewer::RetryCurrentSceneLoad() -> void {
        if (IsSceneBusy() || m_scene->currentSceneName.empty())
            return;
        BeginLoadingScene(m_scene->nativeFs, m_scene->currentSceneName);
    }

auto UvsrSceneViewer::HasSceneLoadFailure() const noexcept -> bool {
        return !m_scene->sceneLoadFailure.empty();
    }

auto UvsrSceneViewer::GetSceneLoadFailure() const noexcept -> const std::string& {
        return m_scene->sceneLoadFailure;
    }

auto UvsrSceneViewer::SceneUnloading() -> void {
        m_scene->sceneFinishedLoading = false;
        m_scene->sceneGpuUploadPending = false;
        m_scene->scenePreparationStage = ScenePreparationStage::Complete;
        m_frame->renderPassPreparationStage =
            RenderPassPreparationStage::Idle;
        if (m_lighting->pbrDeferredLightingPass) m_lighting->pbrDeferredLightingPass->ResetBindingCache();
        if (m_lighting->directionalRayVisibilityPass)
            m_lighting->directionalRayVisibilityPass->ResetBindingCache();
        if (m_lighting->rayTracedFlashlightShadowPass)
            m_lighting->rayTracedFlashlightShadowPass->ResetBindingCache();
        if (m_lighting->rayTracedSkyVisibilityPass)
            m_lighting->rayTracedSkyVisibilityPass->ResetBindingCache();
        if (m_scene->worldSpaceRepresentation)
            m_scene->worldSpaceRepresentation->Reset();
        if (m_lighting->pathTracingPass)
        {
            m_lighting->pathTracingPass->ResetHistory();
            m_lighting->pathTracingPass->ResetBindingCache();
        }
        if (m_lighting->lightingAccumulationPass)
        {
            m_lighting->lightingAccumulationPass->ResetHistory();
            m_lighting->lightingAccumulationPass->ResetBindingCache();
        }
        if (m_frame->autoExposurePass)
            m_frame->autoExposurePass->Reset();
        if (m_frame->gBufferGeometryPass)
            m_frame->gBufferGeometryPass->ResetBindingCache();
        if (m_frame->materialIdGeometryPass)
            m_frame->materialIdGeometryPass->ResetBindingCache();
        m_frame->bindingCache.Clear();
        m_lighting->flashlight.reset();
        m_lighting->flashlightNode.reset();
        m_lighting->sceneLightsWithoutFlashlight.clear();
        m_lighting->editableLights.clear();
        ResetFlashlightMotion();
        m_lighting->sunLight.reset();
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;
        m_ui.ShowMaterialDrawer = false;
        m_frame->materialPickPurpose = MaterialPickPurpose::None;
        m_frame->materialPickScene = nullptr;
        m_scene->originalMaterials.clear();
        // Move the large vectors without freeing them here. The next loader
        // worker releases this retired world before allocating its replacement,
        // keeping hundreds of megabytes of allocator work off the render thread.
        m_scene->retiredCameraCollisionWorld.emplace(
            std::move(m_scene->cameraCollisionWorld));
        m_scene->cameraCollisionWorld = CameraCollisionWorld{};
        m_scene->pendingSceneCpuState.reset();
        m_frame->submittedMainViewTriangles = 0u;
        m_scene->world.reset();

    }

auto UvsrSceneViewer::LoadScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& fileName) -> bool {
        using namespace std::chrono;

        // SceneUnloading transfers the previous BVH here so its large vector
        // allocations are released by the loader rather than by a present
        // frame. This also lowers the peak before the replacement is built.
        m_scene->retiredCameraCollisionWorld.reset();
        m_scene->pendingSceneCpuState.reset();

        std::unique_ptr<engine::Scene> scene = std::make_unique<engine::Scene>(GetDevice(),
            *m_frame->shaderFactory, fs, m_TextureCache, m_scene->descriptorTable, nullptr);

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

            if (m_lighting->imageBasedLightingEnvironment &&
                !m_lighting->imageBasedLightingEnvironment->GetRadianceTexture())
            {
                prepared.environmentRadiance =
                    m_lighting->imageBasedLightingEnvironment->PrepareRadiance(
                        m_ui.EnvironmentSource,
                        m_ui.WhiteWorld != WhiteWorldMode::Off);
            }

            // ApplicationBase publishes the LoadScene return value through an
            // atomic completion flag. Write the complete handoff before that
            // release so SceneLoaded never observes a partial scene state.
            m_scene->pendingSceneCpuState.emplace(std::move(prepared));
            m_scene->world = std::move(scene);

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

        m_scene->pendingSceneCpuState.reset();
        return false;
    }

auto UvsrSceneViewer::SceneLoaded() -> void {
        if (!m_scene->pendingSceneCpuState || !m_scene->world)
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
        m_scene->rendererSceneLoaded = true;
        m_scene->hasRendererSceneResources = true;
        m_scene->sceneLoadFailure.clear();

        m_scene->cameraCollisionWorld = std::move(
            m_scene->pendingSceneCpuState->collisionWorld);
        ResetFlashlightMotion();
        m_scene->sceneDiagonal = m_scene->pendingSceneCpuState->sceneDiagonal;
        m_scene->cameraCollisionRadius =
            m_scene->pendingSceneCpuState->collisionRadius;
        if (m_lighting->imageBasedLightingEnvironment &&
            m_scene->pendingSceneCpuState->environmentRadiance)
        {
            m_lighting->imageBasedLightingEnvironment->StagePreparedRadiance(
                std::move(
                    *m_scene->pendingSceneCpuState->environmentRadiance));
        }
        m_scene->pendingSceneCpuState.reset();

        m_scene->world->BeginLoadingBuffers();
        m_scene->sceneGpuUploadPending = true;
        m_scene->scenePreparationStage = ScenePreparationStage::MeshUpload;
        m_scene->sceneGpuUploadStart =
            std::chrono::high_resolution_clock::now();
    }

auto UvsrSceneViewer::CompleteSceneActivation() -> void {

        InvalidateLightingAccumulationHistory();
        m_lighting->hasLightingHistorySignatures = false;
        if (m_lighting->pathTracingPass)
            m_lighting->pathTracingPass->ResetHistory();
        if (m_lighting->lightingAccumulationPass)
            m_lighting->lightingAccumulationPass->ResetHistory();

        m_scene->originalMaterials.clear();
        for (const auto& material : m_scene->world->GetSceneGraph()->GetMaterials())
            m_scene->originalMaterials.emplace_back(material, *material);
        SetWhiteWorldMode(m_ui.WhiteWorld);

        for (auto light : m_scene->world->GetSceneGraph()->GetLights())
        {
            const std::string normalizedLightName =
                NormalizeSceneLightName(light->GetName());
            if (normalizedLightName != light->GetName())
                light->SetName(normalizedLightName);

            if (!m_lighting->sunLight &&
                light->GetLightType() == UVSR_LIGHT_TYPE_DIRECTIONAL)
            {
                m_lighting->sunLight = std::static_pointer_cast<DirectionalLight>(light);
                m_lighting->sunLight->irradiance = DefaultSunIrradiance;
                m_lighting->sunLight->angularSize = DefaultSunAngularSizeDegrees;
            }
        }

        if (!m_lighting->sunLight)
        {
            m_lighting->sunLight = std::make_shared<DirectionalLight>();
            m_lighting->sunLight->angularSize = DefaultSunAngularSizeDegrees;
            m_lighting->sunLight->irradiance = DefaultSunIrradiance;

            auto node = std::make_shared<SceneGraphNode>();
            node->SetLeaf(m_lighting->sunLight);
            m_lighting->sunLight->SetDirection(dm::double3(0.1, -0.9, 0.1));
            m_lighting->sunLight->SetName("sun_1");
            m_scene->world->GetSceneGraph()->Attach(m_scene->world->GetSceneGraph()->GetRootNode(), node);
        }

        AttachFlashlightToScene();
        m_scene->world->RefreshSceneGraph(GetFrameIndex());
        m_lighting->sceneLightsWithoutFlashlight.clear();
        m_lighting->editableLights.clear();
        if (m_lighting->flashlight)
            m_lighting->editableLights.push_back(m_lighting->flashlight);
        for (const auto& light :
            m_scene->world->GetSceneGraph()->GetLights())
        {
            if (light && light != m_lighting->flashlight)
            {
                m_lighting->sceneLightsWithoutFlashlight.push_back(light);
                m_lighting->editableLights.push_back(light);
            }
        }

        const SceneCatalogEntry* currentCatalogEntry =
            FindSceneCatalogEntry(m_scene->sceneCatalog, m_scene->currentSceneName);
        const SceneInitialCamera* sceneInitialCamera =
            currentCatalogEntry && currentCatalogEntry->InitialCamera
            ? &*currentCatalogEntry->InitialCamera
            : nullptr;
        if (sceneInitialCamera)
            m_scene->cameraVerticalFov = sceneInitialCamera->VerticalFovDegrees;
        else
            m_scene->cameraVerticalFov = 60.f;

        std::shared_ptr<SceneGraphNode> cameraTarget = m_scene->world->GetSceneGraph()->GetRootNode();
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
                m_scene->currentSceneName.c_str(),
                sceneInitialCamera->Position[0],
                sceneInitialCamera->Position[1],
                sceneInitialCamera->Position[2],
                sceneInitialCamera->VerticalFovDegrees);
        }

        m_ui.Camera = CameraMode::ThirdPerson;

        if (!sceneInitialCamera)
        {
            const float3 initialPosition = m_scene->thirdPersonCamera.GetPosition();
            const float3 initialDirection = m_scene->thirdPersonCamera.GetDir();
            const float3 initialUp = m_scene->thirdPersonCamera.GetUp();
            m_scene->firstPersonCamera.LookTo(initialPosition, initialDirection, initialUp);
            m_scene->pivotCamera.LookTo(initialPosition, initialDirection, initialUp);
            m_scene->staticCamera.LookTo(initialPosition, initialDirection, initialUp);
        }

        m_scene->sceneFinishedLoading = true;

    }

auto UvsrSceneViewer::SetWhiteWorldMode(WhiteWorldMode mode) -> void {
        const bool shaderModeChanged = (m_ui.WhiteWorld == WhiteWorldMode::Off) !=
            (mode == WhiteWorldMode::Off);
        m_ui.WhiteWorld = mode;

        const bool enabled = mode != WhiteWorldMode::Off;
        const bool preserveDetailMaps = mode == WhiteWorldMode::PreserveDetail;
        const bool preserveLighting = mode == WhiteWorldMode::PreserveLighting;

        if (!m_scene->world)
            return;

        for (auto& [material, original] : m_scene->originalMaterials)
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

        m_scene->world->GetSceneGraph()->GetRootNode()->InvalidateContent();
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
        float distance = radius * distanceScale / sinf(dm::radians(m_scene->cameraVerticalFov * 0.5f));
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
            m_scene->thirdPersonCamera.LookTo(
                framingCamera.GetPosition(),
                framingCamera.GetDir(),
                framingCamera.GetUp());
        }
        else
        {
            const float3 direction = m_scene->thirdPersonCamera.GetDir();
            const float3 up = m_scene->thirdPersonCamera.GetUp();
            m_scene->thirdPersonCamera.LookTo(
                bounds.center() - direction * distance,
                direction,
                up);
        }
        m_scene->thirdPersonCamera.ResetZoomReferenceDistance(distance);
        // Framing a picked node is another camera teleport. Start the mounted
        // emitter at this new pose instead of sweeping it across the scene.
        ResetFlashlightMotion();
    }

auto UvsrSceneViewer::GetTextureCache() -> std::shared_ptr<TextureCache> {
        return m_TextureCache;
    }

auto UvsrSceneViewer::IsSceneBusy() const -> bool {
        return IsSceneLoading() || m_scene->sceneGpuUploadPending;
    }

auto UvsrSceneViewer::IsSceneGpuUploadPending() const -> bool {
        return m_scene->sceneGpuUploadPending;
    }

auto UvsrSceneViewer::GetScene() -> std::shared_ptr<Scene> {
        return m_scene->world;
    }

auto UvsrSceneViewer::SetMaterialDrawerVisible(bool visible) -> void {
        const bool centerPickPending =
            m_frame->materialPickPurpose ==
                MaterialPickPurpose::RefreshMaterialDrawerSelection;
        if (!visible)
        {
            m_ui.ShowMaterialDrawer = false;
            if (centerPickPending)
            {
                m_frame->materialPickPurpose = MaterialPickPurpose::None;
                m_frame->materialPickScene = nullptr;
            }
            return;
        }

        m_ui.ShowUI = true;
        m_ui.ShowMaterialDrawer = true;
        if (!m_scene->world || IsSceneBusy())
            return;

        // Never reveal the previous click selection while a fresh center sample
        // is pending. A miss leaves the drawer open with its aiming guidance.
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;
        m_frame->materialPickPurpose =
            MaterialPickPurpose::RefreshMaterialDrawerSelection;
        m_frame->materialPickScene = m_scene->world.get();
    }

auto UvsrSceneViewer::GetOriginalMaterial(
        const std::shared_ptr<Material>& material) const -> const Material* {
        const auto original = std::find_if(
            m_scene->originalMaterials.begin(),
            m_scene->originalMaterials.end(),
            [&material](const auto& entry)
            {
                return entry.first == material;
            });
        return original != m_scene->originalMaterials.end()
            ? &original->second
            : nullptr;
    }

auto UvsrSceneViewer::NotifyMaterialCommandChanged(
        const std::shared_ptr<Material>& material) -> void {
        if (!material)
            return;
        material->dirty = true;
        if (m_scene->world && m_scene->world->GetSceneGraph() &&
            m_scene->world->GetSceneGraph()->GetRootNode())
        {
            m_scene->world->GetSceneGraph()->GetRootNode()->
                InvalidateContent();
        }
        ResetImageBasedLightingHistory();
    }

auto UvsrSceneViewer::RecordLoadingPresentationFrame() -> void {
        const auto now = std::chrono::steady_clock::now();
        if (m_scene->loadingPresentationFrameCount > 0u)
        {
            const double gapMilliseconds =
                std::chrono::duration<double, std::milli>(
                    now - m_scene->lastLoadingPresentationFrame).count();
            m_scene->maximumLoadingPresentationGapMs = std::max(
                m_scene->maximumLoadingPresentationGapMs,
                gapMilliseconds);
        }
        m_scene->lastLoadingPresentationFrame = now;
        ++m_scene->loadingPresentationFrameCount;
    }

auto UvsrSceneViewer::RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) -> void {
        RecordLoadingPresentationFrame();
        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_frame->commandList->open();
        m_frame->commandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
        m_frame->commandList->close();
        GetDevice()->executeCommandList(m_frame->commandList);
    }

auto UvsrSceneViewer::PrepareLoadingRenderTargets(nvrhi::IFramebuffer* framebuffer) -> bool {
        const nvrhi::FramebufferInfoEx& framebufferInfo =
            framebuffer->getFramebufferInfo();
        if (framebufferInfo.width == 0u || framebufferInfo.height == 0u)
            return false;

        const DirectX::XMUINT2 presentationSize(
            framebufferInfo.width,
            framebufferInfo.height);
        const DirectX::XMUINT2 renderSize = presentationSize;
        bool needNewPasses = false;
        if (!m_frame->renderTargets || m_frame->renderTargets->IsUpdateRequired(
                renderSize,

                presentationSize,
                m_ui.Lighting == LightingSolution::RayMarching))
        {
            m_frame->renderTargets.reset();
            m_frame->bindingCache.Clear();
            m_frame->renderTargets = std::make_unique<RenderTargets>();
            if (!m_frame->renderTargets->Init(
                    GetDevice(),
                    renderSize,

                    presentationSize,

                    true,
                    m_ui.Lighting == LightingSolution::RayMarching))
            {
                throw std::runtime_error(
                    "UVSR loading render targets failed to initialize");
            }
            needNewPasses = true;
        }

        if (SetupView())
        {
            needNewPasses = true;
        }

        if (needNewPasses ||
            (m_ui.Lighting == LightingSolution::RayMarching && !m_frame->gBufferGeometryPass) || !m_frame->autoExposurePass ||
            !m_frame->agxToneMappingPass)
        {
            BeginRenderPassPreparation(true);
        }
        else
        {
            m_frame->renderPassPreparationStage =
                RenderPassPreparationStage::Complete;
        }
        return true;
    }

auto UvsrSceneViewer::RenderSceneGpuUploadFrame(nvrhi::IFramebuffer* framebuffer) -> void {
        RecordLoadingPresentationFrame();
        if (m_scene->scenePreparationStage == ScenePreparationStage::RenderPasses &&
            ProcessRenderPassPreparationStep())
        {
            m_scene->scenePreparationStage = ScenePreparationStage::Complete;
        }

        nvrhi::ITexture* framebufferTexture =
            framebuffer->getDesc().colorAttachments[0].texture;
        m_frame->commandList->open();
        m_frame->commandList->clearTextureFloat(
            framebufferTexture,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        switch (m_scene->scenePreparationStage)
        {
        case ScenePreparationStage::MeshUpload:
            if (m_scene->world->ProcessLoadingBuffers(
                    m_frame->commandList,
                    RendererSceneState::UploadBytesPerFrame,
                    GetFrameIndex()))
            {
                // Activation has its own loading frame so its material, light,
                // and camera setup cannot stack on the final mesh-buffer work.
                m_scene->scenePreparationStage =
                    ScenePreparationStage::SceneActivation;
            }
            break;

        case ScenePreparationStage::SceneActivation:
            CompleteSceneActivation();
            m_scene->scenePreparationStage =
                ScenePreparationStage::MaterialBuffers;
            break;

        case ScenePreparationStage::MaterialBuffers:
            // Activation applies the renderer's PBR defaults and can attach
            // its fallback lights, which dirties Donut's material and scene
            // buffers after the importer's final refresh. Consume those
            // writes on their own loading frame so the first visible scene
            // frame does not inherit the whole update.
            m_scene->world->RefreshBuffers(m_frame->commandList, GetFrameIndex());
            m_scene->scenePreparationStage =
                ScenePreparationStage::WorldRepresentation;
            break;

        case ScenePreparationStage::WorldRepresentation:
        {
            const bool worldRepresentationRequested =
                m_ui.Representation.allowRayTraversal &&
                m_scene->bindlessLayout &&
                ((m_ui.Lighting == LightingSolution::PathTracing &&
                    GetPathTracingSceneDomainStatus() !=
                        PathTracingSceneDomainStatus::Unsupported) ||
                (m_ui.DirectionalShadows.enabled &&
                    SupportsDirectionalRayVisibility()) ||
                (m_ui.FlashlightEnabled &&
                    m_ui.Flashlight.castShadows &&
                    m_lighting->flashlight &&
                    m_scene->bindlessLayout &&
                    RayVisibilityPass::IsDeviceSupported(
                        GetDevice(), RayVisibilityPass::Kind::Flashlight)) ||
                (m_ui.RayTracedSkyVisibility.enabled &&
                    (HasRayTracedSkyVisibilityConsumer(
                            m_ui.RayTracedSkyVisibility) ||
                        (m_ui.Lighting ==
                                LightingSolution::RayMarching &&
                            m_ui.LightingDebugView ==
                                PbrLightingDebugView::SkyVisibility)) &&
                    SupportsRayTracedSkyVisibility()));
            if (!worldRepresentationRequested ||
                !m_scene->worldSpaceRepresentation ||
                !m_scene->worldSpaceRepresentation->IsSupported() ||
                m_scene->worldSpaceRepresentation->GetStatus().state ==
                    WorldSpaceRepresentationState::Failed ||
                m_scene->worldSpaceRepresentation->Update(
                    m_frame->commandList,
                    m_scene->world.get(),
                    m_ui.Representation,
                    uint32_t(GetFrameIndex()),
                    true))
            {
                m_scene->scenePreparationStage =
                    ScenePreparationStage::RenderTargets;
            }
            break;
        }

        case ScenePreparationStage::RenderTargets:
            if (PrepareLoadingRenderTargets(framebuffer))
            {
                m_scene->scenePreparationStage =
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
        UpdateImageBasedLighting(m_frame->commandList);
        m_frame->commandList->close();
        GetDevice()->executeCommandList(m_frame->commandList);

        if (m_lighting->imageBasedLightingEnvironment &&
            m_lighting->imageBasedLightingEnvironment->HasPreparedRadianceFailed())
        {
            uvsr::log::error(
                "Required image-based lighting preparation failed");
            GetDeviceManager()->ReportRenderDisposition(
                uvsr::RendererRenderDisposition::Failed);
            return;
        }

        const bool environmentReady =
            !m_lighting->imageBasedLightingEnvironment ||
            m_lighting->imageBasedLightingEnvironment->IsPreparedRadianceReady();
        if (m_scene->scenePreparationStage == ScenePreparationStage::Complete &&
            environmentReady)
        {
            m_scene->sceneGpuUploadPending = false;
            const auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() -
                    m_scene->sceneGpuUploadStart).count();
            uvsr::log::info(
                "Staged scene upload and renderer preparation completed in %lld ms across %llu loading frames (maximum presentation gap %.2f ms)",
                static_cast<long long>(duration),
                static_cast<unsigned long long>(
                    m_scene->loadingPresentationFrameCount),
                m_scene->maximumLoadingPresentationGapMs);
            m_scene->lastLoadingPresentationFrame = {};
            m_scene->maximumLoadingPresentationGapMs = 0.0;
            m_scene->loadingPresentationFrameCount = 0u;
        }
    }

auto UvsrSceneViewer::Render(nvrhi::IFramebuffer* framebuffer) -> void {
        if (m_scene->sceneRetirementPending)
        {
            const RendererSceneRetirementStatus status =
                m_scene->sceneRetirement.Poll();
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
            m_scene->hasRendererSceneResources = false;
            if (!m_scene->sceneRetirement.Consume())
            {
                throw std::runtime_error(
                    "UVSR scene retirement could not be consumed");
            }
            m_scene->sceneRetirementPending = false;
            StartPendingSceneLoad();
            RenderSplashScreen(framebuffer);
            return;
        }

        RendererSceneLoadWorkerState workerState =
            m_scene->sceneLoadWorker.GetState();
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
            workerState = m_scene->sceneLoadWorker.GetState();
        }

        if (workerState == RendererSceneLoadWorkerState::Failed)
        {
            const std::exception_ptr failure =
                m_scene->sceneLoadWorker.GetException();
            (void)m_scene->sceneLoadWorker.Join();
            m_scene->sceneLoadFailure =
                "The scene importer returned a failure result.";
            if (failure)
            {
                try
                {
                    std::rethrow_exception(failure);
                }
                catch (const std::exception& error)
                {
                    m_scene->sceneLoadFailure = error.what();
                    uvsr::log::error(
                        "Scene worker failed: %s", error.what());
                }
                catch (...)
                {
                    m_scene->sceneLoadFailure =
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
            m_scene->sceneLoadWorker.Reset();
            m_scene->pendingSceneCpuState.reset();
            m_scene->rendererSceneLoaded = false;
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
            if (!m_scene->sceneLoadWorker.Join())
            {
                throw std::runtime_error(
                    "UVSR successful scene worker failed while joining");
            }
            m_scene->sceneLoadWorker.Reset();
            SceneLoaded();
        }

        if (!m_scene->rendererSceneLoaded)
        {
            RenderSplashScreen(framebuffer);
            return;
        }
        RenderScene(framebuffer);
    }
