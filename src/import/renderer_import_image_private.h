#pragma once

#include "renderer_import_image.h"

namespace uvsr
{
    struct ImportImagePlan
    {
        ImportDecodedImageInfo info;
        size_t dataOffset = 0;
        size_t dataBytes = 0;
        size_t subresources = 0;
    };

    [[nodiscard]] ImportResult PlanImportDDS(ArrayView<const uint8_t> bytes,
        bool forceSRGB, ImportImagePlan& output) noexcept;
    // repeats the already checked flat layout operation without retaining input.
    [[nodiscard]] ImportResult WriteImportImageLayout(const ImportImagePlan& plan,
        ArrayView<ImportImageSubresource> output) noexcept;
}
