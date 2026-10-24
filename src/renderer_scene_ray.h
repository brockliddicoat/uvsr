#pragma once

#include "renderer_scene.h"

namespace uvsr
{
    enum class PathTracingSceneDomainStatus : uint8_t
    {
        Supported,
        BlendedGeometryOmitted,
        Unsupported
    };

    [[nodiscard]] PathTracingSceneDomainStatus ClassifyPathTracingSceneDomain(const RendererSceneView& scene) noexcept;

    struct RendererSceneRayMesh
    {
        uint32_t sceneMesh = 0;
        RendererSceneRange geometries;
    };

    struct RendererSceneRayGeometry
    {
        uint32_t sceneGeometry = 0;
        bool opaque = false;
    };

    struct RendererSceneRayInstance
    {
        uint32_t sceneInstance = 0;
        uint32_t selectedMesh = 0;
    };

    struct RendererSceneRayView
    {
        ArrayView<const RendererSceneRayMesh> meshes;
        ArrayView<const RendererSceneRayGeometry> geometries;
        ArrayView<const RendererSceneRayInstance> instances;
        uint64_t generation = 0;
    };

    // first active ray use owns input-sized selection storage until scene retirement.
    // views borrow that storage. failed preparation preserves the previous selection.
    class RendererSceneRaySelection final
    {
    public:
        RendererSceneRaySelection() noexcept = default;
        ~RendererSceneRaySelection();
        RendererSceneRaySelection(const RendererSceneRaySelection&) = delete;
        RendererSceneRaySelection& operator=(const RendererSceneRaySelection&) = delete;
        [[nodiscard]] RendererSceneResult Prepare(const RendererSceneView& scene) noexcept;
        // sealed topology is identified by generation. a material revision scans
        // referenced geometry classifications; other edits retain the selection.
        [[nodiscard]] bool Matches(const RendererSceneView& scene) noexcept;
        [[nodiscard]] RendererSceneRayView View() const noexcept;
        [[nodiscard]] size_t StorageBytes() const noexcept;
        void Reset() noexcept;

    private:
        struct State;
        State* m_state = nullptr;
    };

#if defined(UVSR_BUILD_TESTING)
    void SetRendererSceneRayAllocationFailure(uint32_t ordinal) noexcept;
#endif
}
