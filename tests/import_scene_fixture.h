#pragma once

#include "renderer_import_scene.h"

struct ImportSceneDefectReference { uint32_t staleOwners, cameras, lights; };

uint32_t CompareImportSceneReference(uvsr::ArrayView<const char> json, uvsr::ArrayView<const uint8_t> bytes,
    const uvsr::RendererSceneView& scene, uint32_t expectedModeDefects) noexcept;
ImportSceneDefectReference ReadImportSceneDefectReference(uvsr::ArrayView<const char> json,
    uvsr::ArrayView<const uint8_t> bytes, bool skin) noexcept;
void FinishImportSceneReference() noexcept;
