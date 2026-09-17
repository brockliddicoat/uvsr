#pragma once

#include "renderer_import.h"
#include "renderer_scene.h"
#include "renderer_import_runtime_lights.h"

namespace uvsr
{
    struct ImportSceneOptions
    {
        ArrayView<const char> modelName;
        size_t sceneIndex = SIZE_MAX; // default scene, or first scene when omitted.
        uint64_t generation = 0;
        size_t maxScratchBytes = SIZE_MAX;
        size_t maxGeometryBytes = SIZE_MAX;
        ArrayView<const char> modelPath;
        size_t maxImageBytes = SIZE_MAX;
        // required for file images. synchronous, read-only; no view is retained.
        bool (*fileExists)(void* context, ArrayView<const char> path) noexcept = nullptr;
        void* fileContext = nullptr;
        ImportRuntimeLightOptions runtimeLights;
    };

    struct ImportGeometryBufferView
    {
        ArrayView<const uint8_t> indices;
        ArrayView<const uint8_t> vertices;
        ArrayView<const uint8_t> morphs;
        uint32_t indexOwner = InvalidSceneIndex;
        // a derived group has no CPU vertex payload. these owned initial-pose
        // matrices drive GPU skinning into the scene layout before publication.
        ArrayView<const gpu_contract::Float4x4> jointMatrices;
        uint32_t skinInstanceIndex = InvalidSceneIndex;
    };

    struct ImportGeometryState;
    struct ImportCompositionAccess;

    // owns packed upload/collision bytes, independently of parser and scene records.
    // views borrow until reset, move assignment or destruction; all calls are exclusive.
    class ImportGeometry final
    {
    public:
        ImportGeometry() noexcept = default;
        ~ImportGeometry() noexcept;
        ImportGeometry(const ImportGeometry&) = delete;
        ImportGeometry& operator=(const ImportGeometry&) = delete;
        ImportGeometry(ImportGeometry&& other) noexcept;
        ImportGeometry& operator=(ImportGeometry&& other) noexcept;
        [[nodiscard]] size_t BufferCount() const noexcept;
        [[nodiscard]] ImportGeometryBufferView Buffer(size_t index) const noexcept;
        [[nodiscard]] size_t StorageBytes() const noexcept;
        [[nodiscard]] size_t ConversionScratchBytes() const noexcept;
        // upload and collision consumers must finish their borrows first.
        void Reset() noexcept;

    private:
        ImportGeometryState* m_State = nullptr;
        friend struct ImportCompositionAccess;
        friend ImportResult ConvertImportScene(const ImportDocument&, const ImportSceneOptions&,
            RendererScene&, ImportGeometry&, ImportTextures*, ImportRuntimeLightIds*) noexcept;
    };

    struct ImportImageView
    {
        ArrayView<const char> path;
        ArrayView<const char> mimeType;
        ArrayView<const uint8_t> bytes;
        bool embedded = false;
    };

    struct ImportTextureSwizzle
    {
        uint32_t imageIndex = InvalidSceneIndex;
        uint32_t channelCount = 0;
        int32_t channels[4]{-1, -1, -1, -1};
    };

    struct ImportTextureView
    {
        uint32_t imageIndex = InvalidSceneIndex;
        bool forceSRGB = false;
        ArrayView<const ImportTextureSwizzle> swizzles;
        uint32_t requestIndex = InvalidSceneIndex;
    };

    struct ImportTexturesState;

    // owns image paths, encoded embedded bytes and texture semantics. image and
    // swizzle indices belong to this owner; texture indices match the scene table.
    // decoding/upload consumers finish all borrows before reset or destruction.
    class ImportTextures final
    {
    public:
        ImportTextures() noexcept = default;
        ~ImportTextures() noexcept;
        ImportTextures(const ImportTextures&) = delete;
        ImportTextures& operator=(const ImportTextures&) = delete;
        ImportTextures(ImportTextures&& other) noexcept;
        ImportTextures& operator=(ImportTextures&& other) noexcept;
        [[nodiscard]] size_t ImageCount() const noexcept;
        [[nodiscard]] ImportImageView Image(size_t index) const noexcept;
        [[nodiscard]] size_t TextureCount() const noexcept;
        [[nodiscard]] ImportTextureView Texture(size_t index) const noexcept;
        // source request order includes unused materials. it determines shared
        // image color space and swizzle metadata when composing several models.
        [[nodiscard]] size_t TextureRequestCount() const noexcept;
        [[nodiscard]] ImportTextureView TextureRequest(size_t index) const noexcept;
        [[nodiscard]] size_t StorageBytes() const noexcept;
        void Reset() noexcept;

    private:
        ImportTexturesState* m_State = nullptr;
        friend struct ImportCompositionAccess;
        friend ImportResult ConvertImportScene(const ImportDocument&, const ImportSceneOptions&,
            RendererScene&, ImportGeometry&, ImportTextures*, ImportRuntimeLightIds*) noexcept;
    };

    // scene/geometry/textures must be empty; errors leave them unchanged.
    // optional runtime IDs commit with those owners; errors preserve prior IDs.
    // a texture output is required when the selected scene references textures.
    // the caller may release the document immediately after success.
    [[nodiscard]] ImportResult ConvertImportScene(const ImportDocument& document,
        const ImportSceneOptions& options, RendererScene& scene, ImportGeometry& geometry,
        ImportTextures* textures = nullptr, ImportRuntimeLightIds* runtimeLights = nullptr) noexcept;
}
