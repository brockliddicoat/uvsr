#pragma once

#include "camera_collision.h"
#include "camera_controllers.h"
#include "image_based_lighting_environment_nvrhi.h"
#include "renderer_scene_retirement_nvrhi.h"
#include "renderer_scene_load_worker.h"
#include "renderer_import_load_status.h"
#include "renderer_scene_resources_nvrhi.h"
#include "renderer_scene_draw.h"
#include "renderer_scene_gpu_nvrhi.h"
#include "scene_catalog.h"
#include "world_space_representation_nvrhi.h"
#include "renderer_scene_descriptors_nvrhi.h"
#include <chrono>
#include "windows_executable_path.h"
#include <memory>
#include <optional>
#include <utility>

namespace uvsr
{
    enum class ScenePreparationStage
    {
        MeshUpload,
        SceneActivation,
        CanonicalScene,
        CameraCollision,
        WorldRepresentation,
        RenderTargets,
        RenderPasses,
        Complete
    };

    enum class SceneWorkerPurpose { Import, Collision };

    struct PreparedSceneCandidate
    {
        ImportLoadedScene loaded;
        PreparedImageBasedLightingRadiance
            environmentRadiance;
    };

    struct SceneLoadPreparationInputs
    {
        ImageBasedLightingSource environmentSource = ImageBasedLightingSource::Kloppenheim03Day;
        bool prepareEnvironmentRadiance = false;
        bool neutralizeEnvironment = false;
        ImportSceneLoadOptions load;
    };

    struct RendererSceneState
    {
        static constexpr uint64_t UploadBytesPerFrame = 8ull * 1024ull * 1024ull;
        explicit RendererSceneState(nvrhi::IDevice* device) : resources(device), gpuTables(device), sceneRetirement(device) {}
        // immutable after startup. request metadata borrows this catalog.
        SceneCatalog sceneCatalog;
        SceneLoadRequest currentSceneRequest;
        WindowsPath sceneDir;
        RendererScene canonical;
        ImportGeometry geometry;
        ImportDecodedImages images;
        ImportRuntimeLightIds runtimeLights;
        ImportLoadStatus loadStatus;
        RendererSceneDrawList draws;
        // table-owned slots are released before this manager, after retirement.
        nvrhi::BindingLayoutHandle bindlessLayout;
        std::unique_ptr<RendererSceneDescriptorsNvrhi> descriptorTable;
        RendererSceneResourcesNvrhi resources;
        RendererSceneGpuTablesNvrhi gpuTables;
        uint64_t lastSceneGeneration = 0;
        uint64_t lastSubmittedContentRevision = 0;
        RendererSceneRetirementNvrhi sceneRetirement;
        bool sceneRetirementPending = false;
        bool restartSceneAfterRetirement = true;
        SceneWorkerPurpose sceneWorkerPurpose = SceneWorkerPurpose::Import;
        bool rendererSceneLoaded = false;
        bool hasRendererSceneResources = false;
        uint64_t sceneLoadErrorCount = 0;
        RendererSceneLoadFailureText sceneLoadFailure;
        std::unique_ptr<WorldSpaceRepresentation> worldSpaceRepresentation;
        UvsrFirstPersonCamera firstPersonCamera{ true };
        UvsrThirdPersonCamera thirdPersonCamera;
        UvsrFirstPersonCamera pivotCamera{ false };
        StaticViewCamera staticCamera;
        CameraCollisionWorld cameraCollisionWorld;
        // the worker alone writes the candidate and reads the frozen inputs.
        // the render thread consumes the complete aggregate only after Join.
        std::optional<PreparedSceneCandidate> pendingSceneCandidate;
        std::optional<CameraCollisionWorld> pendingCameraCollisionWorld;
        SceneLoadPreparationInputs sceneLoadPreparationInputs;
        std::optional<CameraCollisionWorld> retiredCameraCollisionWorld;
        float cameraVerticalFov = 60.f;
        float sceneDiagonal = 100.f;
        float cameraCollisionRadius = 0.1f;
        bool sceneGpuUploadPending = false;
        ScenePreparationStage scenePreparationStage = ScenePreparationStage::Complete;
        std::chrono::high_resolution_clock::time_point sceneGpuUploadStart;
        std::chrono::steady_clock::time_point lastLoadingPresentationFrame;
        double maximumLoadingPresentationGapMs = 0.0;
        uint64_t loadingPresentationFrameCount = 0u;
    };
}
