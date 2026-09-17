#pragma once

#include "renderer_import_scene.h"

void CompareImportGeometryReference(uvsr::ArrayView<const char> json, uvsr::ArrayView<const uint8_t> bytes,
    const uvsr::RendererSceneView& scene, const uvsr::ImportGeometry& geometry) noexcept;
void FinishImportGeometryReference() noexcept;
