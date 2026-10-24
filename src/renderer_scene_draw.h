#pragma once

#include "renderer_scene.h"
#include "renderer_frustum.h"

namespace uvsr
{
    struct RendererSceneDraw
    {
        uint32_t material = 0;
        uint32_t buffers = 0;
        uint32_t mesh = 0;
        uint32_t instance = 0;
        uint32_t geometry = 0;
    };

    [[nodiscard]] bool IntersectsRendererSceneBounds(const RendererSceneFrustum& frustum,
        const RendererSceneBounds& bounds) noexcept;

    // one scene generation owns the maximum geometry-instance capacity. Build
    // borrows published records synchronously and allocates nothing per view.
    class RendererSceneDrawList final
    {
    public:
        RendererSceneDrawList() noexcept = default;
        ~RendererSceneDrawList();
        RendererSceneDrawList(const RendererSceneDrawList&) = delete;
        RendererSceneDrawList& operator=(const RendererSceneDrawList&) = delete;
        [[nodiscard]] RendererSceneResult Prepare(const RendererSceneView& scene) noexcept;
        [[nodiscard]] RendererSceneResult Build(const RendererSceneView& scene,
            const RendererSceneFrustum& frustum) noexcept;
        void Reset() noexcept;
        [[nodiscard]] ArrayView<const RendererSceneDraw> View() const noexcept { return {m_items, m_count}; }
        [[nodiscard]] size_t Capacity() const noexcept { return m_capacity; }
        [[nodiscard]] size_t StorageBytes() const noexcept { return m_capacity * sizeof(RendererSceneDraw); }
        [[nodiscard]] size_t ChunkCount() const noexcept { return m_chunks; }
        [[nodiscard]] size_t MaximumChunk() const noexcept { return m_maxChunk; }
    private:
        RendererSceneDraw* m_items = nullptr;
        size_t m_capacity = 0;
        size_t m_count = 0;
        size_t m_chunks = 0;
        size_t m_maxChunk = 0;
        uint64_t m_generation = 0;
    };

#if defined(UVSR_BUILD_TESTING)
    void SetRendererSceneDrawAllocationFailure(bool fail) noexcept;
#endif
}
