#pragma once

#include "renderer_import_image.h"

void CompareImportImageReference(const uvsr::ImportImageView& input, bool srgb,
    const uvsr::ImportDecodedImage& image) noexcept;
uint32_t ReadImportImageArrayReference(const uvsr::ImportImageView& input) noexcept;
void FinishImportImageReference() noexcept;
