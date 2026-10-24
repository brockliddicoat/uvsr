#pragma once

#include "renderer_import.h"
#include "renderer_import_allocation.h"
#include "renderer_scene_records.h"
#include <fastgltf/types.hpp>

namespace simdjson::dom { class object; }

namespace uvsr
{
    struct ImportBufferStorage
    {
        ArrayView<const uint8_t> bytes;
        uint8_t* owned = nullptr;
    };

    enum ImportMaterialFlag : uint8_t
    {
        ImportMaterialPbr = 1, ImportMaterialEmissiveStrength = 2,
        ImportMaterialSubsurface = 4, ImportMaterialHair = 8,
        ImportMaterialIgnoredTransform = 16, ImportMaterialIgnoredTexCoord = 32
    };

    struct ImportMaterialMetadata
    {
        uint8_t flags = 0;
        RendererSceneMaterialValues::Subsurface subsurface;
        RendererSceneMaterialValues::Hair hair;
    };

    struct ImportSwizzleMetadata
    {
        uint32_t image = InvalidSceneIndex;
        uint32_t channelCount = 0;
        int32_t channels[4]{-1, -1, -1, -1};
    };

    struct ImportImageMetadata
    {
        RendererSceneString mimeType;
        bool named = false;
    };

    // parser-only facts absent from fastgltf. counts are validated JSON array sizes.
    // this owner transfers once into ImportState and dies with the document.
    struct ImportMetadata
    {
        uint8_t* nodeFlags = nullptr;
        size_t nodeCount = 0;
        uint8_t* cameraNames = nullptr;
        size_t cameraCount = 0;
        ImportMaterialMetadata* materials = nullptr;
        size_t materialCount = 0;
        ImportImageMetadata* images = nullptr;
        size_t imageCount = 0;
        char* imageMimeTypes = nullptr;
        size_t imageMimeBytes = 0;
        RendererSceneRange* textureSwizzles = nullptr;
        size_t textureCount = 0;
        ImportSwizzleMetadata* swizzles = nullptr;
        size_t swizzleCount = 0;
        uint64_t requiredExtensions = 0;
        bool requiredSubsurface = false;
        bool requiredHair = false;
        bool requiredSwizzle = false;
        size_t requiredKeyOffset = 0;
        size_t requiredKeyLength = 0;

        ImportMetadata() noexcept = default;
        ~ImportMetadata() noexcept;
        ImportMetadata(const ImportMetadata&) = delete;
        ImportMetadata& operator=(const ImportMetadata&) = delete;
        void TakeFrom(ImportMetadata& source) noexcept;
    };

    // shared only by the parser and converter translation units.
    struct ImportState
    {
        fastgltf::Asset asset;
        ImportBufferStorage* buffers = nullptr;
        ImportMetadata metadata;
        explicit ImportState(fastgltf::Asset&& input) noexcept;
        ~ImportState() noexcept;
    };

    enum ImportNodeFlag : uint8_t { ImportNodeTransform = 1, ImportNodeName = 2, ImportNodeLight = 4 };

    ImportResult ReadImportMaterialMetadata(const simdjson::dom::object& root,
        ArrayView<const uint8_t> json, ImportMetadata& metadata) noexcept;
    ImportResult ReadImportSceneMetadata(const simdjson::dom::object& root, ImportMetadata& metadata) noexcept;
    uint64_t ImportParserExtensions() noexcept;
}
