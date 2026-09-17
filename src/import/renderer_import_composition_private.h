#pragma once

#include "renderer_import_composition.h"

namespace uvsr
{
    struct CompositionGeometrySource
    {
        uint32_t model = InvalidSceneIndex;
        uint32_t group = InvalidSceneIndex;
        uint32_t indexOwner = InvalidSceneIndex;
        uint32_t skinInstance = InvalidSceneIndex;
    };

    struct ImportCompositionAccess
    {
        static bool Empty(const ImportGeometry& geometry, const ImportTextures& textures) noexcept;
        static ImportResult CopyTextures(ArrayView<const ImportImageView> images,
            ArrayView<const ImportTextureView> requests, ArrayView<const uint32_t> textureMap,
            size_t maxBytes, ImportTextures& output) noexcept;
        // borrows model payloads until the final non-failing ownership transfer.
        // final scene validation and every other output allocation precede this call.
        static ImportResult TakeGeometry(ArrayView<ImportModel> models,
            ArrayView<const CompositionGeometrySource> sources, const RendererSceneView& scene,
            size_t maxBytes, size_t scratchBytes, ImportGeometry& output) noexcept;
    };
}
