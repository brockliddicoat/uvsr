#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene_nvrhi.h"
#include "uvsr_renderer_lighting_nvrhi.h"
#include "uvsr_renderer_frame_nvrhi.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include "settings_snapshot_storage.h"
#include "scene_catalog_path.h"
#include "windows_path_text.h"
#include <limits.h>
#include <stdlib.h>
#include <winerror.h>
#include "renderer_nvrhi_message_callback.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>
#include "renderer_scene_material_mode.h"
#include <string.h>

using namespace donut;
using namespace donut::app;
using namespace uvsr;

namespace
{
constexpr float DefaultSunIrradiance = 8.f;
constexpr float DefaultSunAngularSizeDegrees = 0.2f;
struct ImportWorkerCancellation
{
    const RendererSceneLoadCancellation* cancellation;
    static bool Requested(void* context) noexcept
    {
        const auto* source = static_cast<const ImportWorkerCancellation*>(context)->cancellation;
        return source && source->IsRequested();
    }
};
void ReportUnavailableModel(void*, uint32_t index, ArrayView<const char> path,
    ImportLoadOperation operation, ImportResult result) noexcept
{
    // one diagnostic callback per unavailable model. byte offsets preserve the
    // full UTF-8 path across the logger's fixed 4096-byte message limit.
    size_t offset = 0;
    do
    {
        const size_t count = path.count - offset < 3000 ? path.count - offset : 3000;
        uvsr::log::warning("Scene model %u unavailable: operation %u, error %u, object %u at %zu; path bytes %zu..%zu/%zu: %.*s",
            index, unsigned(operation), unsigned(result.error), unsigned(result.object), result.index,
            offset, offset + count, path.count, int(count), count ? path.data + offset : "");
        offset += count;
    } while (offset < path.count);
}
struct SceneUploadRecording
{
    nvrhi::ICommandList* commands;
    RendererSceneGpuTablesNvrhi& tables;
    bool open = true;
    bool pending = true;
    SceneUploadRecording(nvrhi::ICommandList* list, RendererSceneGpuTablesNvrhi& gpuTables)
        : commands(list), tables(gpuTables)
    {
        commands->open();
        tables.BeginRecording();
    }
    SceneUploadRecording(const SceneUploadRecording&) = delete;
    SceneUploadRecording& operator=(const SceneUploadRecording&) = delete;
    void Close()
    {
        commands->close();
        open = false;
    }
    void Commit()
    {
        pending = false;
        tables.CommitRecording();
    }
    ~SceneUploadRecording()
    {
        if (!pending) return;
        if (open) commands->close();
        tables.AbortRecording();
    }
};
struct SceneUploadHealth
{
    const RendererNvrhiMessageCallback& messages;
    uint64_t expected;
    static bool Check(void* context) noexcept
    {
        const auto& self = *static_cast<const SceneUploadHealth*>(context);
        return self.messages.GetErrorCount() == self.expected;
    }
};
}

auto UvsrSceneViewer::ApplySceneInitialCamera(const SceneInitialCamera& preset) -> void {
        const gpu_contract::Float3 position{
            preset.Position[0],
            preset.Position[1],
            preset.Position[2]};
        const gpu_contract::Float3 direction = Normalize(gpu_contract::Float3{
            preset.Direction[0],
            preset.Direction[1],
            preset.Direction[2]});
        const gpu_contract::Float3 upHint = Normalize(gpu_contract::Float3{
            preset.Up[0],
            preset.Up[1],
            preset.Up[2]});
        const gpu_contract::Float3 right = Normalize(Cross(direction, upHint));
        const gpu_contract::Float3 up = Normalize(Cross(right, direction));
        ApplyCameraPose(
            position,
            direction,
            up,
            right,
            preset.VerticalFovDegrees);
    }

auto UvsrSceneViewer::GetAvailableScenes() const noexcept -> const SceneCatalog& {
        return m_scene->sceneCatalog;
    }

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::CaptureRetainedRuntimeStorage() const -> RetainedRuntimeStorage {
        RetainedRuntimeStorage result;
        if (!m_scene->rendererSceneLoaded)
            return result;
        const auto& collision = m_scene->cameraCollisionWorld;
        result.collisionTriangles = collision.GetTriangleCount();
        result.collisionTriangleCapacity = collision.GetTriangleCapacity();
        result.collisionNodes = collision.GetNodeCount();
        result.collisionNodeCapacity = collision.GetNodeCapacity();
        const auto view = m_scene->canonical.View();
        result.sceneMeshes = view.meshes.count;
        result.sceneMaterials = view.materials.count;
        result.sceneInstances = view.instances.count;
        result.sceneGeometries = view.geometries.count;
        for (size_t i = 0; i < view.instances.count; ++i)
            result.sceneGeometryInstances += view.meshes.data[view.instances.data[i].meshIndex].geometries.count;
        result.retainedSourceArrays = m_scene->geometry.BufferCount();
        result.editableLightCapacity = 0;
        result.unmountedLightCapacity = 0;
        if (const auto table = m_scene->gpuTables.DescriptorTable())
            result.descriptorCapacity = table->getCapacity();
        if (m_scene->descriptorTable)
        {
            result.descriptorLive = m_scene->descriptorTable->GetLiveCount();
            result.descriptorPeakLive = m_scene->descriptorTable->GetPeakLiveCount();
            result.descriptorPeakCapacity = m_scene->descriptorTable->GetPeakCapacity();
        }
        const auto progress = GetSceneLoadProgress();
        result.textureQueuePeak = progress.texturesTotal;
        result.texturesRequested = progress.texturesTotal;
        result.texturesLoaded = progress.texturesDecoded;
        result.texturesFinalized = GetSceneTexturesReady();
        if (m_lighting->pbrDeferredLightingPass)
            result.pbrBindingPeak = m_lighting->pbrDeferredLightingPass->GetBindingCachePeakSize();
        if (m_lighting->pathTracingPass)
            result.pathLightCapacity = m_lighting->pathTracingPass->GetLightCapacity();
        if (m_frame->renderTargets && m_frame->renderTargets->Heap)
            result.targetHeapBytes = m_frame->renderTargets->Heap->getDesc().capacity;
        if (const auto buffer = m_scene->gpuTables.MaterialBuffer())
            result.materialBufferBytes = buffer->getDesc().byteSize;
        if (const auto buffer = m_scene->gpuTables.GeometryBuffer())
            result.geometryBufferBytes = buffer->getDesc().byteSize;
        if (const auto buffer = m_scene->gpuTables.InstanceBuffer())
            result.instanceBufferBytes = buffer->getDesc().byteSize;
        return result;
    }
#endif

auto UvsrSceneViewer::GetSceneDir() const noexcept -> std::wstring_view {
        return {m_scene->sceneDir.Data(), m_scene->sceneDir.Size()};
    }

auto UvsrSceneViewer::GetCurrentSceneName() const noexcept -> std::string_view {
        return m_scene->currentSceneRequest.FileName();
    }

auto UvsrSceneViewer::GetCurrentSceneDisplayName() const noexcept -> std::string_view {
        return m_scene->currentSceneRequest.DisplayName();
    }

auto UvsrSceneViewer::GetCurrentSceneCatalogEntry() const noexcept -> const SceneCatalogEntry* {
        return m_scene->currentSceneRequest.CatalogEntry();
    }

auto UvsrSceneViewer::IsSceneLoading() const -> bool {
        return m_scene->sceneRetirementPending ||
            m_sceneLoadWorker.GetState() !=
                RendererSceneLoadWorkerState::Idle;
    }

auto UvsrSceneViewer::IsSceneLoaded() const -> bool {
        return m_scene->rendererSceneLoaded;
    }

auto UvsrSceneViewer::StartPendingSceneLoad() -> void {
        if (m_scene->currentSceneRequest.ImportFileName().empty() || m_scene->lastSceneGeneration == UINT64_MAX)
        {
            m_scene->sceneLoadFailure.Assign("Scene path is empty or scene generation is exhausted.");
            return;
        }
        m_scene->loadStatus.Reset();
        if (!m_scene->loadStatus.Prepare())
        {
            m_scene->sceneLoadFailure.Assign("Scene load status allocation failed.");
            return;
        }
        m_scene->sceneWorkerPurpose = SceneWorkerPurpose::Import;
        m_scene->sceneLoadPreparationInputs = {
            m_ui.EnvironmentSource,
            m_lighting->imageBasedLightingEnvironment &&
                !m_lighting->imageBasedLightingEnvironment->GetRadianceTexture(),
            m_ui.WhiteWorld != WhiteWorldMode::Off
        };
        auto& options = m_scene->sceneLoadPreparationInputs.load;
        // reserve before any candidate GPU work. canceled/failed generations
        // are never reused, including failures after a partial submission.
        options.generation = ++m_scene->lastSceneGeneration;
        options.runtimeLights.enabled = true;
        options.runtimeLights.sun.name = {"sun_1", 5};
        options.runtimeLights.sun.values.irradiance = DefaultSunIrradiance;
        options.runtimeLights.sun.values.angularSize = DefaultSunAngularSizeDegrees;
        options.runtimeLights.flashlight.name = {FlashlightPublicName, strlen(FlashlightPublicName)};
        RendererSceneLightPose sunPose;
        sunPose.direction[0] = 0.1; sunPose.direction[1] = -0.9; sunPose.direction[2] = 0.1;
        sunPose.setDirection = true;
        if (!ResolveRendererSceneLightTransform({}, {}, sunPose, options.runtimeLights.sun.transform).Succeeded())
        {
            m_scene->sceneLoadFailure.Assign("Scene sun pose is invalid.");
            return;
        }
        m_scene->sceneLoadErrorCount = m_nvrhiMessages.GetErrorCount();
        if (!m_sceneLoadWorker.Start(
                [](void* context, const RendererSceneLoadCancellation& cancellation)
                {
                    auto& viewer = *static_cast<UvsrSceneViewer*>(context);
                    return viewer.LoadSceneCandidate(viewer.m_scene->currentSceneRequest.ImportFileName(), cancellation);
                }, this))
        {
            uvsr::log::error("Could not start the scene-load worker");
        }
    }

auto UvsrSceneViewer::BeginLoadingScene(SceneLoadRequest* replacement,
        SettingsSnapshotError& error) -> bool {
        error = {};
        const auto& request = replacement ? *replacement : m_scene->currentSceneRequest;
        if (request.ImportFileName().empty())
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0,
                "UVSR scene loading requires a descriptor", {}};
            return false;
        }
        if (IsSceneBusy())
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0,
                "UVSR cannot replace an active scene-load task", {}};
            return false;
        }
        if (m_scene->hasRendererSceneResources)
        {
            if (!m_scene->sceneRetirement.Begin())
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0,
                    "UVSR could not arm scene GPU retirement", {}};
                return false;
            }
        }
        else
            GetDevice()->runGarbageCollection();

        // no worker can borrow the old request after the busy check. keep the
        // accepted request intact until retirement or collection succeeds.
        if (replacement)
            m_scene->currentSceneRequest = std::move(*replacement);
        m_scene->restartSceneAfterRetirement = true;
        m_scene->rendererSceneLoaded = false;
        m_scene->sceneLoadFailure.Clear();
        if (m_scene->hasRendererSceneResources)
            m_scene->sceneRetirementPending = true;
        else
            StartPendingSceneLoad();
        return true;
    }

auto UvsrSceneViewer::SetCurrentSceneName(std::string_view sceneName,
        SettingsSnapshotError& error) -> bool {
        auto previousDetail = std::move(error.detail);
        error = {};
        const SceneCatalogEntry* entry = nullptr;
        if (!FindSceneCatalogEntry(m_scene->sceneCatalog, sceneName, entry, error))
            return false;
        const std::string_view resolved = entry ? entry->FileName : sceneName;
        if (m_scene->currentSceneRequest.FileName() == resolved)
            return true;
        SceneLoadRequest candidate;
        if (!PrepareSceneLoadRequest(GetSceneDir(), sceneName, entry, candidate, error))
            return false;
        return BeginLoadingScene(&candidate, error);
    }

auto UvsrSceneViewer::RetryCurrentSceneLoad() -> void {
        if (IsSceneBusy() || m_scene->currentSceneRequest.FileName().empty())
            return;
        SettingsSnapshotError error;
        if (!BeginLoadingScene(nullptr, error))
        {
            m_scene->sceneLoadFailure.Assign(error.Message());
            uvsr::log::error("Could not retry scene loading: %s", error.Message());
        }
    }

auto UvsrSceneViewer::HasSceneLoadFailure() const noexcept -> bool {
        return !m_scene->sceneLoadFailure.Empty();
    }

auto UvsrSceneViewer::GetSceneLoadFailure() const noexcept -> const char* {
        return m_scene->sceneLoadFailure.Data();
    }

auto UvsrSceneViewer::SceneUnloading() -> void {
        m_scene->sceneGpuUploadPending = false;
        m_scene->lastSubmittedContentRevision = 0;
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
        m_lighting->flashlight = {};
        ResetFlashlightMotion();
        m_lighting->sunLight = {};
        m_ui.SelectedMaterial = {};
        m_ui.SelectedNode = {};
        m_ui.ShowMaterialDrawer = false;
        m_frame->materialPickPurpose = MaterialPickPurpose::None;
        m_frame->materialPickGeneration = 0;
        // move the large arrays without freeing them here. The next loader
        // worker releases this retired world before allocating its replacement,
        // keeping hundreds of megabytes of allocator work off the render thread.
        m_scene->retiredCameraCollisionWorld.emplace(
            std::move(m_scene->cameraCollisionWorld));
        m_scene->cameraCollisionWorld = CameraCollisionWorld{};
        m_scene->pendingSceneCandidate.reset();
        m_frame->submittedMainViewTriangles = 0u;
        m_scene->draws.Reset();
        m_scene->gpuTables.Reset();
        m_scene->resources.Reset();
        m_scene->geometry.Reset();
        m_scene->images.Reset();
        m_scene->canonical.Reset();
        m_scene->runtimeLights = {};
        m_scene->hasRendererSceneResources = false;

    }

auto UvsrSceneViewer::LoadSceneCandidate(std::string_view fileName,
        const RendererSceneLoadCancellation& cancellation) -> bool {
        using namespace std::chrono;
        const auto cancelled = [&cancellation] { return cancellation.IsRequested(); };
        if (cancelled())
            return false;

        // SceneUnloading transfers the previous BVH here so its large array
        // allocations are released by the loader rather than by a present
        // frame. This also lowers the peak before the replacement is built.
        m_scene->retiredCameraCollisionWorld.reset();
        m_scene->pendingSceneCandidate.reset();

        const auto startTime = high_resolution_clock::now();
        PreparedSceneCandidate prepared;
        ImportWorkerCancellation cancel{&cancellation};
        ImportLoadCallbacks callbacks;
        callbacks.cancellation = {ImportWorkerCancellation::Requested, &cancel};
        callbacks.report = ImportLoadStatus::Report;
        callbacks.context = &m_scene->loadStatus;
        callbacks.modelUnavailable = ReportUnavailableModel;
        const auto imported = LoadImportScene(NativeImportFileSource(), {fileName.data(), fileName.size()},
            m_scene->sceneLoadPreparationInputs.load, prepared.loaded, callbacks);
        if (!imported)
        {
            if (imported.error != ImportError::Canceled)
                uvsr::log::error("Scene import failed: error %u, object %u at %zu, parser %u, system %u",
                    unsigned(imported.error), unsigned(imported.object), imported.index, imported.parserCode, imported.systemCode);
            return false;
        }
        const auto importFinished = high_resolution_clock::now();
        if (cancelled()) return false;
        const auto& inputs = m_scene->sceneLoadPreparationInputs;
        if (inputs.prepareEnvironmentRadiance)
        {
            const auto result = m_lighting->imageBasedLightingEnvironment->PrepareRadiance(
                inputs.environmentSource, inputs.neutralizeEnvironment, prepared.environmentRadiance);
            if (result == ImageBasedLightingEnvironment::PrepareResult::PathFailure ||
                result == ImageBasedLightingEnvironment::PrepareResult::AllocationFailure)
                return cancellation.Fail(ImageBasedLightingEnvironment::PreparationFailureText(result));
        }
        if (cancelled()) return false;
        m_scene->pendingSceneCandidate.emplace(std::move(prepared));
        const auto endTime = high_resolution_clock::now();
        uvsr::log::info("Scene worker completed import in %lld ms and CPU preparation in %lld ms",
            static_cast<long long>(duration_cast<milliseconds>(importFinished - startTime).count()),
            static_cast<long long>(duration_cast<milliseconds>(endTime - importFinished).count()));
        return true;
    }

auto UvsrSceneViewer::SceneLoaded() -> bool {
        if (!m_scene->pendingSceneCandidate || !m_scene->pendingSceneCandidate->loaded.scene.IsPublished())
        {
            return FailRender("Scene worker completed without a prepared CPU handoff");
        }

        auto& loaded = m_scene->pendingSceneCandidate->loaded;
        m_scene->canonical = std::move(loaded.scene);
        m_scene->geometry = std::move(loaded.geometry);
        m_scene->images = std::move(loaded.images);
        m_scene->runtimeLights = loaded.runtimeLights;
        m_scene->hasRendererSceneResources = true;
        m_scene->sceneLoadFailure.Clear();

        ResetFlashlightMotion();
        if (m_lighting->imageBasedLightingEnvironment &&
            m_scene->pendingSceneCandidate->environmentRadiance)
        {
            m_lighting->imageBasedLightingEnvironment->StagePreparedRadiance(
                std::move(
                    m_scene->pendingSceneCandidate->environmentRadiance));
        }
        m_scene->pendingSceneCandidate.reset();

        m_scene->sceneGpuUploadPending = true;
        m_scene->scenePreparationStage = ScenePreparationStage::MeshUpload;
        m_scene->sceneGpuUploadStart =
            std::chrono::high_resolution_clock::now();
        return true;
    }

auto UvsrSceneViewer::CompleteSceneActivation() -> bool {

        InvalidateLightingAccumulationHistory();
        m_lighting->hasLightingHistorySignatures = false;
        if (m_lighting->pathTracingPass)
            m_lighting->pathTracingPass->ResetHistory();
        if (m_lighting->lightingAccumulationPass)
            m_lighting->lightingAccumulationPass->ResetHistory();

        const auto view = m_scene->canonical.View();
        m_lighting->sunLight = m_scene->runtimeLights.sun;
        m_lighting->flashlight = m_scene->runtimeLights.flashlight;
        const auto* sun = FindRendererSceneLight(view, m_lighting->sunLight);
        const auto* flashlight = FindRendererSceneLight(view, m_lighting->flashlight);
        if (!sun || sun->kind != RendererSceneLightKind::Directional ||
            !flashlight || flashlight->kind != RendererSceneLightKind::Spot)
            return false;
        m_lighting->flashlightSubmittedPoseValid = false;
        return ApplyFlashlightPresentation() && UpdateFlashlightTransform() && SetWhiteWorldMode(m_ui.WhiteWorld);
    }

auto UvsrSceneViewer::StartCameraCollisionPreparation() -> void {
        const auto view = m_scene->canonical.View();
        const auto* root = FindRendererSceneNode(view, {view.generation, view.root});
        m_scene->sceneDiagonal = 100.f;
        if (root && !root->worldBounds.empty)
        {
            const auto& bounds = root->worldBounds;
            const gpu_contract::Float3 diagonal{bounds.maximum.x - bounds.minimum.x,
                bounds.maximum.y - bounds.minimum.y, bounds.maximum.z - bounds.minimum.z};
            m_scene->sceneDiagonal = std::max(Length(diagonal), 100.f);
        }
        m_scene->cameraCollisionRadius = std::max(0.1f, m_scene->sceneDiagonal * 0.0005f);
        m_scene->pendingCameraCollisionWorld.reset();
        m_scene->sceneWorkerPurpose = SceneWorkerPurpose::Collision;
        // Start failure publishes Failed on the same worker state machine; the
        // render owner then retires already-submitted candidate resources.
        if (!m_sceneLoadWorker.Start(
            [](void* context, const RendererSceneLoadCancellation& cancellation)
            { return static_cast<UvsrSceneViewer*>(context)->BuildCameraCollisionWorld(cancellation); }, this))
            uvsr::log::error("Could not start canonical camera collision preparation");
    }

auto UvsrSceneViewer::CompleteCameraActivation() -> void {
        const SceneCatalogEntry* currentCatalogEntry =
            m_scene->currentSceneRequest.CatalogEntry();
        const SceneInitialCamera* sceneInitialCamera =
            currentCatalogEntry && currentCatalogEntry->InitialCamera
            ? &*currentCatalogEntry->InitialCamera
            : nullptr;
        if (sceneInitialCamera)
            m_scene->cameraVerticalFov = sceneInitialCamera->VerticalFovDegrees;
        else
            m_scene->cameraVerticalFov = 60.f;

        const auto view = m_scene->canonical.View();
        RendererSceneHandle cameraTarget{view.generation, view.root};
        const auto find = [&view](const char* name) -> RendererSceneHandle
        {
            const size_t length = strlen(name);
            for (size_t index = 0; index < view.preorder.count; ++index)
            {
                const uint32_t node = view.preorder.data[index];
                const auto text = RendererSceneText(view, view.nodes.data[node].name);
                if (text.count == length && memcmp(text.data, name, length) == 0)
                    return {view.generation, node};
            }
            return {};
        };
        // Prefer the compact asteroid core when present so the initial view
        // includes the full rocky platform instead of tightly framing only the
        // temple. Older Jungle Ruins exports retain the pyramid marker fallback.
        float cameraDistanceScale = 1.f;
        if (auto asteroid = find("UVSR_AsteroidCore"))
        {
            cameraTarget = asteroid;
            cameraDistanceScale = 1.45f;
        }
        else if (auto pyramid = find("Pyramid_EmitterShell"))
            cameraTarget = pyramid;
        PointThirdPersonCameraAt(cameraTarget, cameraDistanceScale, true);

        if (sceneInitialCamera)
        {
            ApplySceneInitialCamera(*sceneInitialCamera);
            uvsr::log::info(
                "Applied descriptor initial camera to '%s' at %.3f, %.3f, %.3f and %.1f degrees vertical FOV",
                m_scene->currentSceneRequest.FileName().data(),
                sceneInitialCamera->Position[0],
                sceneInitialCamera->Position[1],
                sceneInitialCamera->Position[2],
                sceneInitialCamera->VerticalFovDegrees);
        }

        m_ui.Camera = CameraMode::ThirdPerson;

        if (!sceneInitialCamera)
        {
            const gpu_contract::Float3 initialPosition = m_scene->thirdPersonCamera.GetPosition();
            const gpu_contract::Float3 initialDirection = m_scene->thirdPersonCamera.GetDir();
            const gpu_contract::Float3 initialUp = m_scene->thirdPersonCamera.GetUp();
            m_scene->firstPersonCamera.LookTo(initialPosition, initialDirection, initialUp);
            m_scene->pivotCamera.LookTo(initialPosition, initialDirection, initialUp);
            m_scene->staticCamera.LookTo(initialPosition, initialDirection, initialUp);
        }

    }

auto UvsrSceneViewer::SetWhiteWorldMode(WhiteWorldMode mode) -> bool {
        if (mode < WhiteWorldMode::Off || mode > WhiteWorldMode::PreserveLighting)
            return false;
        const bool shaderModeChanged = (m_ui.WhiteWorld == WhiteWorldMode::Off) !=
            (mode == WhiteWorldMode::Off);
        if (m_scene->canonical.IsPublished())
        {
            if (!ApplyRendererSceneMaterialMode(m_scene->canonical, mode).Succeeded())
                return false;
        }
        m_ui.WhiteWorld = mode;
        if (m_scene->canonical.IsPublished() && shaderModeChanged)
            m_ui.ShaderReloadRequested = true;
        return true;
    }

auto UvsrSceneViewer::PointThirdPersonCameraAt(RendererSceneHandle node,
        float distanceScale, bool resetOrientation) -> void {
        const auto view = m_scene->canonical.View();
        const auto* record = FindRendererSceneNode(view, node);
        if (record) FrameCameraAtBounds(record->worldBounds, distanceScale, resetOrientation);
    }

auto UvsrSceneViewer::FrameCameraAtBounds(const RendererSceneBounds& source,
        float distanceScale, bool resetOrientation) -> void {
        if (m_scene->thirdPersonCamera.FrameBounds(source, m_scene->cameraVerticalFov, distanceScale, resetOrientation))
            ResetFlashlightMotion();
    }

auto UvsrSceneViewer::GetSceneLoadProgress() const noexcept -> ImportLoadProgress {
        return m_scene->loadStatus.Read().progress;
    }

auto UvsrSceneViewer::GetSceneTexturesReady() const noexcept -> uint32_t {
        return m_scene->resources.Progress().texturesSubmitted;
    }

auto UvsrSceneViewer::IsSceneBusy() const -> bool {
        return IsSceneLoading() || m_scene->sceneGpuUploadPending;
    }

auto UvsrSceneViewer::IsSceneGpuUploadPending() const -> bool {
        return m_scene->sceneGpuUploadPending;
    }

auto UvsrSceneViewer::GetSceneView() const -> RendererSceneView {
        return m_scene->rendererSceneLoaded ? m_scene->canonical.View() : RendererSceneView{};
    }

auto UvsrSceneViewer::GetSceneMaterial(RendererSceneHandle material) const -> const RendererSceneMaterial* {
        return FindRendererSceneMaterial(GetSceneView(), material);
    }

auto UvsrSceneViewer::GetSceneTexturePath(uint32_t texture) const noexcept -> std::string_view {
        const auto scene = GetSceneView();
        if (texture >= scene.textures.count) return {};
        const auto text = RendererSceneText(scene, scene.textures.data[texture].path);
        return {text.count ? text.data : "", text.count};
    }

auto UvsrSceneViewer::IsSceneTextureReady(uint32_t texture) const -> bool {
        if (!m_scene->rendererSceneLoaded) return false;
        nvrhi::ITexture* resource = nullptr;
        return m_scene->gpuTables.GetTexture(texture, resource) && resource;
    }

namespace
{
bool FailSceneNodePath(WindowsPathTextResult& result, catalog_path::Error error) noexcept
{
    const auto code = error.code == catalog_path::ErrorCode::Capacity ? WindowsPathTextError::Capacity :
        error.code == catalog_path::ErrorCode::Conversion ? WindowsPathTextError::Conversion : WindowsPathTextError::InvalidPath;
    result = {code, error.nativeCode};
    return false;
}

struct SceneNodePathBuffer
{
    wchar_t* data = nullptr;
    size_t size = 0;
    size_t capacity = 0;

    SceneNodePathBuffer() noexcept = default;
    ~SceneNodePathBuffer() noexcept { free(data); }
    SceneNodePathBuffer(const SceneNodePathBuffer&) = delete;
    SceneNodePathBuffer& operator=(const SceneNodePathBuffer&) = delete;

    std::wstring_view View() const noexcept { return {data ? data : L"", size}; }

    bool Reserve(size_t count, WindowsPathTextResult& result) noexcept
    {
        if (count > size_t(INT_MAX) || count >= size_t(PTRDIFF_MAX) / sizeof(wchar_t))
        {
            result = {WindowsPathTextError::Capacity, ERROR_FILENAME_EXCED_RANGE};
            return false;
        }
        if (count + 1 <= capacity) return true;
        auto* candidate = static_cast<wchar_t*>(realloc(data, (count + 1) * sizeof(wchar_t)));
        if (!candidate)
        {
            result = {WindowsPathTextError::Allocation, 0};
            return false;
        }
        data = candidate;
        capacity = count + 1;
        return true;
    }

    bool Decode(ArrayView<const char> name, WindowsPathTextResult& result) noexcept
    {
        const std::string_view text(name.count ? name.data : "", name.count);
        catalog_path::ConversionPlan plan;
        catalog_path::Error error;
        if (!catalog_path::MeasureDecode(text, catalog_path::Encoding::Filesystem, plan, error))
            return FailSceneNodePath(result, error);
        if (!Reserve(plan.size, result)) return false;
        if (!catalog_path::Decode(text, plan, data, capacity, size, error))
            return FailSceneNodePath(result, error);
        return true;
    }

    bool JoinRight(const SceneNodePathBuffer& right, WindowsPathTextResult& result) noexcept
    {
        catalog_path::JoinPlan plan;
        catalog_path::Error error;
        if (!catalog_path::PlanJoin(View(), right.View(), plan, error))
            return FailSceneNodePath(result, error);
        if (!Reserve(plan.size, result)) return false;
        // reserve can move the left input. the accumulated right input is separate.
        if (!catalog_path::Join(View(), right.View(), data, capacity, size, error))
            return FailSceneNodePath(result, error);
        return true;
    }

    void Swap(SceneNodePathBuffer& other) noexcept
    {
        std::swap(data, other.data);
        std::swap(size, other.size);
        std::swap(capacity, other.capacity);
    }
};
}

auto UvsrSceneViewer::GetSceneNodePath(RendererSceneHandle node,
        WindowsPathText& output, WindowsPathTextResult& result) const noexcept -> bool
{
    result = {};
    const auto scene = GetSceneView();
    const auto* record = FindRendererSceneNode(scene, node);
    if (!record)
    {
        output.Clear();
        return true;
    }
    SceneNodePathBuffer path, component;
    for (size_t depth = 0; record && depth < scene.nodes.count; ++depth)
    {
        if (!component.Decode(RendererSceneText(scene, record->name), result) ||
            !component.JoinRight(path, result)) return false;
        path.Swap(component);
        record = record->parentIndex < scene.nodes.count ? scene.nodes.data + record->parentIndex : nullptr;
    }
    if (!component.Decode({"/", 1}, result) || !component.JoinRight(path, result)) return false;
    return output.Assign(component.data, component.size, WindowsPathTextForm::Generic,
        WindowsPathTextEncoding::Filesystem, result);
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
                m_frame->materialPickGeneration = 0;
            }
            return;
        }

        m_ui.ShowUI = true;
        m_ui.ShowMaterialDrawer = true;
        if (!m_scene->rendererSceneLoaded || IsSceneBusy())
            return;

        // Never reveal the previous click selection while a fresh center sample
        // is pending. A miss leaves the drawer open with its aiming guidance.
        m_ui.SelectedMaterial = {};
        m_ui.SelectedNode = {};
        m_frame->materialPickPurpose =
            MaterialPickPurpose::RefreshMaterialDrawerSelection;
        m_frame->materialPickGeneration = m_scene->canonical.View().generation;
    }

auto UvsrSceneViewer::SetSceneMaterial(
        RendererSceneHandle material, const RendererSceneMaterialValues& candidate) -> bool {
        return GetSceneMaterial(material) && m_scene->canonical.SetMaterial(material, candidate).Succeeded();
    }

auto UvsrSceneViewer::NotifyMaterialCommandChanged() -> void {
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
        if (GetDevice()->executeCommandList(m_frame->commandList) == 0)
        {
            uvsr::log::error("Loading presentation submission failed");
            GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
        }
    }

auto UvsrSceneViewer::PrepareLoadingRenderTargets(nvrhi::IFramebuffer* framebuffer) -> PreparationResult {
        const nvrhi::FramebufferInfoEx& framebufferInfo =
            framebuffer->getFramebufferInfo();
        if (framebufferInfo.width == 0u || framebufferInfo.height == 0u)
            return PreparationResult::Pending;

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
            std::unique_ptr<RenderTargets> candidate(new (std::nothrow) RenderTargets());
            if (!candidate || !candidate->Init(
                    GetDevice(),
                    renderSize,

                    presentationSize,

                    true,
                    m_ui.Lighting == LightingSolution::RayMarching))
            {
                return FailPreparation("Loading render targets failed to initialize");
            }
            m_frame->renderTargets = std::move(candidate);
            needNewPasses = true;
        }

        bool viewChanged = false;
        if (!SetupView(viewChanged))
        {
            return FailPreparation("Loading view failed to initialize");
        }
        needNewPasses = needNewPasses || viewChanged;

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
        return PreparationResult::Complete;
    }

auto UvsrSceneViewer::PrepareCanonicalScene() -> bool {
        if (!m_scene->draws.Prepare(m_scene->canonical.View()).Succeeded())
        {
            uvsr::log::error("Scene draw capacity preparation failed");
            return false;
        }
        const auto prepared = m_scene->gpuTables.Prepare(m_scene->canonical.View(),
            m_scene->resources, m_scene->descriptorTable.get());
        if (!prepared.Succeeded())
        {
            m_scene->draws.Reset();
            uvsr::log::error("Canonical GPU table preparation failed: error %u at index %u",
                unsigned(prepared.error), prepared.index);
            return false;
        }
        return true;
    }

auto UvsrSceneViewer::RenderSceneGpuUploadFrame(nvrhi::IFramebuffer* framebuffer) -> void {
        RecordLoadingPresentationFrame();
        auto nextStage = m_scene->scenePreparationStage;
        if (nextStage == ScenePreparationStage::RenderPasses)
        {
            const PreparationResult result = ProcessRenderPassPreparationStep();
            if (result == PreparationResult::Failed) return;
            if (result == PreparationResult::Complete)
                nextStage = ScenePreparationStage::Complete;
        }

        const auto preparationStage = nextStage;
        if (preparationStage == ScenePreparationStage::MeshUpload)
        {
            SceneUploadHealth health{m_nvrhiMessages, m_scene->sceneLoadErrorCount};
            const RendererUploadHealth uploadHealth{SceneUploadHealth::Check, &health};
            RendererUploadResult result;
            if (m_scene->resources.Progress().phase == RendererUploadPhase::Empty)
            {
                nvrhi::ShaderHandle skinShader;
                for (size_t i = 0; i < m_scene->geometry.BufferCount(); ++i)
                    if (m_scene->geometry.Buffer(i).jointMatrices.count)
                    {
                        skinShader = m_frame->rendererShaderFactory->CreateShader("uvsr/renderer_skinning_cs.hlsl", "main", {}, nvrhi::ShaderType::Compute);
                        break;
                    }
                RendererUploadOptions options;
                options.rayTracing = GetDevice()->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct);
                result = m_scene->resources.Prepare(m_scene->canonical.View(), m_scene->geometry,
                    m_scene->images.Images(), skinShader, uploadHealth, options);
            }
            // Step owns and submits its command list. NVRHI state tracking must
            // finish that recording before the frame command list opens.
            if (result)
                result = m_scene->resources.Step(RendererSceneState::UploadBytesPerFrame,
                    m_frame->rendererCommonPasses.get(), uploadHealth);
            if (!result)
            {
                uvsr::log::error("Scene resource upload failed: error %u at %u", unsigned(result.error), result.index);
                GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
                return;
            }
            const auto progress = m_scene->resources.Progress();
            if (!progress.cpuBorrows) m_scene->images.Reset();
            if (progress.phase == RendererUploadPhase::Submitted || progress.phase == RendererUploadPhase::Complete)
                nextStage = ScenePreparationStage::SceneActivation;
        }

        nvrhi::ITexture* framebufferTexture =
            framebuffer->getDesc().colorAttachments[0].texture;
        SceneUploadRecording recording(m_frame->commandList, m_scene->gpuTables);
        m_frame->commandList->clearTextureFloat(
            framebufferTexture,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        switch (preparationStage)
        {
        case ScenePreparationStage::MeshUpload:
            break;

        case ScenePreparationStage::SceneActivation:
            if (!CompleteSceneActivation())
            {
                uvsr::log::error("Scene activation transaction failed");
                GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
                return;
            }
            nextStage =
                ScenePreparationStage::CanonicalScene;
            break;

        case ScenePreparationStage::CanonicalScene:
            // CPU records must exist before derived ray/draw preparation. scene
            // visibility still waits for all preparation submissions below.
            if (!PrepareCanonicalScene() ||
                !m_scene->gpuTables.RecordMaterials(m_frame->commandList, m_scene->canonical.View()).Succeeded() ||
                !m_scene->gpuTables.RecordInstances(m_frame->commandList, m_scene->canonical.View()).Succeeded())
            {
                GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
                return;
            }
            nextStage = ScenePreparationStage::CameraCollision;
            break;

        case ScenePreparationStage::CameraCollision:
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
                !m_scene->worldSpaceRepresentation || !m_scene->worldSpaceRepresentation->IsSupported())
            {
                nextStage = ScenePreparationStage::RenderTargets;
                break;
            }
            const bool ready = m_scene->worldSpaceRepresentation->Update(m_frame->commandList,
                m_scene->canonical.View(), m_scene->gpuTables,
                m_ui.Representation, true);
            if (m_scene->worldSpaceRepresentation->GetStatus().state == WorldSpaceRepresentationState::Failed)
            {
                uvsr::log::error("Requested ray-scene preparation failed; candidate was not published");
                GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
                return;
            }
            if (ready) nextStage = ScenePreparationStage::RenderTargets;
            break;
        }

        case ScenePreparationStage::RenderTargets:
        {
            const PreparationResult result = PrepareLoadingRenderTargets(framebuffer);
            if (result == PreparationResult::Failed) return;
            if (result == PreparationResult::Complete)
                nextStage = ScenePreparationStage::RenderPasses;
            break;
        }

        case ScenePreparationStage::RenderPasses:
            break;

        case ScenePreparationStage::Complete:
            break;
        }

        // Consume worker-prepared HDR data one GPU unit per loading frame.
        // Partially generated environment maps are not exposed to rendering.
        UpdateImageBasedLighting(m_frame->commandList);
        recording.Close();
        if (m_nvrhiMessages.GetErrorCount() != m_scene->sceneLoadErrorCount)
        {
            uvsr::log::error("Scene preparation recorded a GPU error; candidate was not published");
            GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
            return;
        }
        if (GetDevice()->executeCommandList(m_frame->commandList) == 0)
        {
            uvsr::log::error("Scene preparation submission failed");
            GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
            return;
        }
        recording.Commit();
        if (m_nvrhiMessages.GetErrorCount() != m_scene->sceneLoadErrorCount)
        {
            FailRender("Scene preparation execution reported a GPU error");
            return;
        }

        if (m_lighting->imageBasedLightingEnvironment &&
            m_lighting->imageBasedLightingEnvironment->HasPreparedRadianceFailed())
        {
            uvsr::log::error(
                "Required image-based lighting preparation failed");
            GetDeviceManager()->ReportRenderDisposition(
                uvsr::RendererRenderDisposition::Failed);
            return;
        }

        m_scene->scenePreparationStage = nextStage;
        if (m_scene->scenePreparationStage == ScenePreparationStage::CameraCollision &&
            m_sceneLoadWorker.GetState() == RendererSceneLoadWorkerState::Idle)
            StartCameraCollisionPreparation();

        const bool environmentReady =
            !m_lighting->imageBasedLightingEnvironment ||
            m_lighting->imageBasedLightingEnvironment->IsPreparedRadianceReady();
        if (m_scene->scenePreparationStage == ScenePreparationStage::Complete &&
            environmentReady)
        {
            m_scene->sceneGpuUploadPending = false;
            // all preparation commands precede rendering on the same graphics
            // queue. this publishes readiness, not physical GPU completion.
            m_scene->rendererSceneLoaded = true;
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
        const auto uploadPhase = m_scene->resources.Progress().phase;
        if (!m_scene->sceneRetirementPending && uploadPhase != RendererUploadPhase::Empty &&
            uploadPhase != RendererUploadPhase::Complete)
        {
            SceneUploadHealth health{m_nvrhiMessages, m_scene->sceneLoadErrorCount};
            if (!m_scene->resources.PollCompletion({SceneUploadHealth::Check, &health}))
            {
                GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
                return;
            }
        }
        if (IsSceneLoading() || m_scene->sceneGpuUploadPending)
        {
            if (!m_scene->sceneRetirementPending &&
                m_nvrhiMessages.GetErrorCount() != m_scene->sceneLoadErrorCount)
            {
                uvsr::log::error("Scene loading recorded a GPU error; candidate was not published");
                GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
                return;
            }
        }
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
                FailRender("UVSR scene retirement lost its pending state");
                return;
            }

            SceneUnloading();
            GetDevice()->runGarbageCollection();
            m_scene->hasRendererSceneResources = false;
            if (!m_scene->sceneRetirement.Consume())
            {
                FailRender("UVSR scene retirement could not be consumed");
                return;
            }
            m_scene->sceneRetirementPending = false;
            if (m_scene->restartSceneAfterRetirement)
                StartPendingSceneLoad();
            RenderSplashScreen(framebuffer);
            return;
        }

        RendererSceneLoadWorkerState workerState =
            m_sceneLoadWorker.GetState();
        if (m_scene->sceneWorkerPurpose == SceneWorkerPurpose::Collision &&
            workerState != RendererSceneLoadWorkerState::Idle)
        {
            if (workerState == RendererSceneLoadWorkerState::Running ||
                workerState == RendererSceneLoadWorkerState::CancelRequested)
            {
                RenderSplashScreen(framebuffer);
                return;
            }
            const bool succeeded = m_sceneLoadWorker.Join() &&
                workerState == RendererSceneLoadWorkerState::Succeeded && m_scene->pendingCameraCollisionWorld.has_value();
            if (!succeeded)
            {
                const char* failure = m_sceneLoadWorker.GetFailureText();
                m_scene->sceneLoadFailure.Assign(*failure ? failure :
                    "Camera collision preparation failed or was cancelled.");
                uvsr::log::error("Scene worker failed: %s", m_scene->sceneLoadFailure.Data());
            }
            m_sceneLoadWorker.Reset();
            m_scene->sceneWorkerPurpose = SceneWorkerPurpose::Import;
            // Join ends all CPU borrows before the source owner releases arrays.
            m_scene->geometry.Reset();
            if (!succeeded)
            {
                m_scene->pendingCameraCollisionWorld.reset();
                m_scene->sceneGpuUploadPending = false;
                m_scene->rendererSceneLoaded = false;
                m_scene->restartSceneAfterRetirement = false;
                if (!m_scene->sceneRetirement.Begin())
                {
                    GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
                    return;
                }
                m_scene->sceneRetirementPending = true;
                RenderSplashScreen(framebuffer);
                return;
            }
            m_scene->cameraCollisionWorld = std::move(*m_scene->pendingCameraCollisionWorld);
            m_scene->pendingCameraCollisionWorld.reset();
            CompleteCameraActivation();
            m_scene->scenePreparationStage = ScenePreparationStage::WorldRepresentation;
            workerState = RendererSceneLoadWorkerState::Idle;
        }

        if (workerState == RendererSceneLoadWorkerState::Failed ||
            workerState == RendererSceneLoadWorkerState::Cancelled)
        {
            (void)m_sceneLoadWorker.Join();
            m_scene->sceneLoadFailure.Clear();
            if (workerState == RendererSceneLoadWorkerState::Failed)
            {
                const char* failure = m_sceneLoadWorker.GetFailureText();
                m_scene->sceneLoadFailure.Assign(*failure ? failure :
                    "The scene importer returned a failure result.");
                uvsr::log::error("Scene worker failed: %s", m_scene->sceneLoadFailure.Data());
            }
            m_sceneLoadWorker.Reset();
            m_scene->pendingSceneCandidate.reset();
            m_scene->rendererSceneLoaded = false;
            GetDevice()->runGarbageCollection();
            RenderSplashScreen(framebuffer);
            return;
        }

        if (workerState == RendererSceneLoadWorkerState::Running ||
            workerState == RendererSceneLoadWorkerState::CancelRequested)
        {
            RenderSplashScreen(framebuffer);
            return;
        }

        if (workerState == RendererSceneLoadWorkerState::Succeeded)
        {
            if (m_nvrhiMessages.GetErrorCount() != m_scene->sceneLoadErrorCount)
            {
                uvsr::log::error("Scene texture preparation failed; candidate was not published");
                GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
                return;
            }
            if (!m_sceneLoadWorker.Join())
            {
                FailRender("UVSR successful scene worker failed while joining");
                return;
            }
            m_sceneLoadWorker.Reset();
            if (!SceneLoaded()) return;
        }

        if (!m_scene->rendererSceneLoaded && !m_scene->sceneGpuUploadPending)
        {
            RenderSplashScreen(framebuffer);
            return;
        }
        RenderScene(framebuffer);
    }
