#pragma once

#include "renderer_scene_records.h"

namespace uvsr
{
    [[nodiscard]] RendererSceneBounds TransformRendererSceneBounds(const RendererSceneBounds& source,
        const RendererSceneAffine& world) noexcept;

    struct RendererSceneCounts
    {
        uint32_t nodes = 0;
        uint32_t meshes = 0;
        uint32_t geometries = 0;
        uint32_t instances = 0;
        uint32_t materials = 0;
        uint32_t textures = 0;
        uint32_t bufferGroups = 0;
        uint32_t morphRanges = 0;
        uint32_t joints = 0;
        uint32_t lights = 0;
        uint32_t cameras = 0;
        uint32_t animations = 0;
        uint32_t channels = 0;
        uint32_t samplers = 0;
        uint32_t keyframes = 0;
        uint32_t stringBytes = 0;
    };

    struct RendererSceneView
    {
        ArrayView<const RendererSceneNode> nodes;
        ArrayView<const RendererSceneMesh> meshes;
        ArrayView<const RendererSceneGeometry> geometries;
        ArrayView<const RendererSceneInstance> instances;
        ArrayView<const RendererSceneMaterial> materials;
        ArrayView<const RendererSceneTexture> textures;
        ArrayView<const RendererSceneBufferGroup> bufferGroups;
        ArrayView<const RendererSceneByteRange> morphRanges;
        ArrayView<const RendererSceneJoint> joints;
        ArrayView<const RendererSceneLight> lights;
        ArrayView<const RendererSceneCamera> cameras;
        ArrayView<const RendererSceneAnimation> animations;
        ArrayView<const RendererSceneAnimationChannel> channels;
        ArrayView<const RendererSceneAnimationSampler> samplers;
        ArrayView<const RendererSceneKeyframe> keyframes;
        ArrayView<const char> strings;
        ArrayView<const uint32_t> preorder;
        uint32_t root = InvalidSceneIndex;
        uint64_t generation = 0;
        uint64_t contentRevision = 0;
        uint64_t materialRevision = 0;
        uint64_t lightRevision = 0;
        uint64_t transformRevision = 0;
        uint64_t previousTransformRevision = 0;
        // only instance world changes invalidate the GPU stream. camera/light
        // poses still advance the full scene revisions above.
        uint64_t instanceTransformRevision = 0;
        uint64_t previousInstanceTransformRevision = 0;
    };

    enum class RendererSceneError : uint8_t
    {
        None,
        InvalidState,
        Capacity,
        Allocation,
        Workspace,
        Root,
        Reference,
        Range,
        Hierarchy,
        Cycle,
        Value,
        Generation
    };

    struct RendererSceneResult
    {
        RendererSceneError error = RendererSceneError::None;
        uint32_t index = InvalidSceneIndex;
        bool changed = false;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return error == RendererSceneError::None;
        }
    };

    struct RendererSceneHandle
    {
        uint64_t generation = 0;
        uint32_t index = InvalidSceneIndex;

        [[nodiscard]] explicit constexpr operator bool() const noexcept
        { return generation != 0 && index != InvalidSceneIndex; }
        [[nodiscard]] constexpr bool operator==(RendererSceneHandle other) const noexcept
        { return generation == other.generation && index == other.index; }
        [[nodiscard]] constexpr bool operator!=(RendererSceneHandle other) const noexcept
        { return !(*this == other); }
    };

    [[nodiscard]] inline const RendererSceneMaterial* FindRendererSceneMaterial(
        const RendererSceneView& scene, RendererSceneHandle handle) noexcept
    {
        return handle && handle.generation == scene.generation && scene.materials.IsValid() && handle.index < scene.materials.count
            ? scene.materials.data + handle.index : nullptr;
    }

    [[nodiscard]] inline const RendererSceneNode* FindRendererSceneNode(
        const RendererSceneView& scene, RendererSceneHandle handle) noexcept
    {
        return handle && handle.generation == scene.generation && scene.nodes.IsValid() && handle.index < scene.nodes.count
            ? scene.nodes.data + handle.index : nullptr;
    }

    [[nodiscard]] inline RendererSceneHandle FindRendererSceneMaterialSelection(
        const RendererSceneView& scene, uint32_t selectionId) noexcept
    {
        if (!scene.generation || selectionId == InvalidSceneIndex || !scene.materials.IsValid() || scene.materials.count > UINT32_MAX)
            return {};
        RendererSceneHandle found;
        for (uint32_t index = 0; index < scene.materials.count; ++index)
        {
            if (scene.materials.data[index].selectionId != selectionId) continue;
            if (found) return {}; // an ambiguous number cannot identify one material.
            found = {scene.generation, index};
        }
        return found;
    }

    [[nodiscard]] inline ArrayView<const char> RendererSceneText(
        const RendererSceneView& scene, RendererSceneString text) noexcept
    {
        if (!text.length || !scene.strings.IsValid() || text.offset > scene.strings.count || text.length > scene.strings.count - text.offset)
            return {};
        return {scene.strings.data + text.offset, text.length};
    }

    class RendererScene final
    {
    public:
        RendererScene() noexcept = default;
        ~RendererScene();
        RendererScene(const RendererScene&) = delete;
        RendererScene& operator=(const RendererScene&) = delete;
        RendererScene(RendererScene&& other) noexcept;
        RendererScene& operator=(RendererScene&& other) noexcept;

        [[nodiscard]] RendererSceneResult Prepare(const RendererSceneCounts& counts) noexcept;
        // synchronous copied input only, rejected after Seal. unwritten slots retain defaults.
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneNode& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneMesh& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneGeometry& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneInstance& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneMaterial& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneTexture& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneBufferGroup& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneByteRange& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneJoint& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneLight& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneCamera& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneAnimation& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneAnimationChannel& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneAnimationSampler& value) noexcept;
        [[nodiscard]] RendererSceneResult Write(uint32_t index, const RendererSceneKeyframe& value) noexcept;
        [[nodiscard]] RendererSceneResult WriteStrings(uint32_t offset, ArrayView<const char> bytes) noexcept;
        [[nodiscard]] RendererSceneResult Seal(uint32_t root, ArrayView<uint8_t> workspace) noexcept;
        [[nodiscard]] RendererSceneResult Publish(uint64_t generation) noexcept;
        [[nodiscard]] RendererSceneView View() const noexcept;
        [[nodiscard]] size_t StorageBytes() const noexcept;
        [[nodiscard]] size_t SealWorkspaceBytes() const noexcept;
        [[nodiscard]] bool IsSealed() const noexcept;
        [[nodiscard]] bool IsPublished() const noexcept;

        [[nodiscard]] RendererSceneResult SetMaterial(
            RendererSceneHandle material, const RendererSceneMaterialValues& values) noexcept;
        // complete index-ordered batch. validate every value before changing any material.
        [[nodiscard]] RendererSceneResult SetMaterials(
            uint64_t generation, ArrayView<const RendererSceneMaterialValues> values) noexcept;
        // loading metadata only, before visible publication/derived GPU tables.
        // paths, MIME and resource identity remain unchanged.
        [[nodiscard]] RendererSceneResult SetTextureMetadata(RendererSceneHandle texture,
            RendererSceneTextureAlpha alpha, uint32_t originalBitsPerPixel) noexcept;
        [[nodiscard]] RendererSceneResult SetLight(
            RendererSceneHandle light, const RendererSceneLightValues& values) noexcept;
        [[nodiscard]] RendererSceneResult SetTransform(
            RendererSceneHandle node, const RendererSceneTransform& transform) noexcept;
        [[nodiscard]] RendererSceneResult AdvancePreviousTransforms() noexcept;

        // callers retire native consumers and stop all borrows before reset/move assignment.
        void Reset() noexcept;

    private:
        struct State;
        State* m_state = nullptr;
    };

#if defined(UVSR_BUILD_TESTING)
    void SetRendererSceneAllocationFailure(uint32_t ordinal) noexcept;
#endif
}
