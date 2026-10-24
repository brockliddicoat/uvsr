#pragma once

#include "camera_collision.h"
#include "camera_controllers.h"
#include "image_based_lighting_environment.h"
#include "renderer_scene_load_worker.h"
#include "renderer_scene_retirement.h"
#include "scene_catalog.h"
#include "world_space_representation.h"
#include <donut/core/vfs/VFS.h>
#include <donut/engine/DescriptorTableManager.h>
#include <donut/engine/Scene.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace uvsr
{
    enum class ScenePreparationStage
    {
        MeshUpload,
        SceneActivation,
        MaterialBuffers,
        WorldRepresentation,
        RenderTargets,
        RenderPasses,
        Complete
    };

    struct PreparedSceneCpuState
    {
        CameraCollisionWorld collisionWorld;
        float sceneDiagonal = 100.f;
        float collisionRadius = 0.1f;
        std::optional<ImageBasedLightingEnvironment::PreparedRadiance>
            environmentRadiance;
    };

    struct RendererSceneState
    {
        static constexpr uint64_t UploadBytesPerFrame = 8ull * 1024ull * 1024ull;
        explicit RendererSceneState(nvrhi::IDevice* device) : sceneRetirement(device) {}
        std::shared_ptr<donut::vfs::RootFileSystem> rootFs;
        std::shared_ptr<donut::vfs::NativeFileSystem> nativeFs;
        std::vector<SceneCatalogEntry> sceneCatalog;
        std::string currentSceneName;
        std::filesystem::path sceneDir;
        std::shared_ptr<donut::engine::Scene> world;
        RendererSceneLoadWorker sceneLoadWorker;
        RendererSceneRetirement sceneRetirement;
        std::shared_ptr<donut::vfs::IFileSystem> pendingSceneFileSystem;
        std::filesystem::path pendingSceneFileName;
        bool sceneRetirementPending = false;
        bool rendererSceneLoaded = false;
        bool hasRendererSceneResources = false;
        std::string sceneLoadFailure;
        std::vector<std::pair<std::shared_ptr<donut::engine::Material>, donut::engine::Material>> originalMaterials;
        nvrhi::BindingLayoutHandle bindlessLayout;
        std::shared_ptr<donut::engine::DescriptorTableManager> descriptorTable;
        std::unique_ptr<WorldSpaceRepresentation> worldSpaceRepresentation;
        UvsrFirstPersonCamera firstPersonCamera{ true };
        UvsrThirdPersonCamera thirdPersonCamera;
        UvsrFirstPersonCamera pivotCamera{ false };
        StaticViewCamera staticCamera;
        CameraCollisionWorld cameraCollisionWorld;
        std::optional<PreparedSceneCpuState> pendingSceneCpuState;
        std::optional<CameraCollisionWorld> retiredCameraCollisionWorld;
        float cameraVerticalFov = 60.f;
        float sceneDiagonal = 100.f;
        float cameraCollisionRadius = 0.1f;
        bool sceneFinishedLoading = false;
        bool sceneGpuUploadPending = false;
        ScenePreparationStage scenePreparationStage = ScenePreparationStage::Complete;
        std::chrono::high_resolution_clock::time_point sceneGpuUploadStart;
        std::chrono::steady_clock::time_point lastLoadingPresentationFrame;
        double maximumLoadingPresentationGapMs = 0.0;
        uint64_t loadingPresentationFrameCount = 0u;
    };
}
