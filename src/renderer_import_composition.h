#pragma once

#include "renderer_import_description.h"
#include "renderer_import_scene.h"

namespace uvsr
{
    // temporary model inputs from ConvertImportScene, with glTF TRS animation
    // channels. application animation properties belong to the description.
    // the parser can die before these owners are made.
    // composition consumes them only after every fallible output step succeeds.
    struct ImportModel
    {
        RendererScene scene;
        ImportGeometry geometry;
        ImportTextures textures;
    };

    enum class ImportModelAvailability : uint8_t { Pending, Available, Unavailable };

    struct ImportCompositionOptions
    {
        uint64_t generation = 0;
        size_t maxScratchBytes = SIZE_MAX;
        size_t maxGeometryBytes = SIZE_MAX;
        size_t maxImageBytes = SIZE_MAX;
        // empty means all models are available. otherwise one state per model.
        // an unavailable slot must own no payload; its descriptor subtrees skip.
        // pending is never a valid composition input. borrowed synchronously.
        ArrayView<const ImportModelAvailability> modelAvailability;
        ImportRuntimeLightOptions runtimeLights;
    };

    struct ImportCompositionStats
    {
        size_t peakScratchBytes = 0;
        uint32_t skippedParentSubtrees = 0;
        uint32_t ignoredAnimationTargets = 0;
        uint32_t ambiguousMaterialTargets = 0;
        uint32_t skippedModelSubtrees = 0;
    };

    // one tuple per descriptor model, in source order. outputs must be empty.
    // failure preserves all inputs and outputs. success consumes the model
    // owners, transfers packed bytes and copies all retained strings and images.
    // optional runtime IDs commit with those owners; errors preserve prior IDs.
    [[nodiscard]] ImportResult ComposeImportScene(const ImportSceneDescription& description,
        ArrayView<ImportModel> models, const ImportCompositionOptions& options,
        RendererScene& scene, ImportGeometry& geometry, ImportTextures& textures,
        ImportCompositionStats* stats = nullptr, ImportRuntimeLightIds* runtimeLights = nullptr) noexcept;
}
