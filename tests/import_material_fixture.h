#pragma once

#include "renderer_import_scene.h"

struct ImportMaterialFixtureFile
{
    const char* path;
    uvsr::ArrayView<const uint8_t> bytes;
};

using ReadImportMaterialFixtureFile = uvsr::ArrayView<const uint8_t> (*)(void*, uvsr::ArrayView<const char>);

void BeginImportMaterialReference(uvsr::ArrayView<const char> json, uvsr::ArrayView<const uint8_t> bytes,
    uvsr::ArrayView<const ImportMaterialFixtureFile> files, const char* model) noexcept;
size_t CompareImportMaterialReference(const uvsr::RendererSceneView& scene, uvsr::ImportTextures& textures,
    void* fileContext, ReadImportMaterialFixtureFile read) noexcept;
void FinishImportMaterialReference() noexcept;
