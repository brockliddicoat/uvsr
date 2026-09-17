#pragma once

#include "renderer_import.h"

namespace uvsr
{
    // resolve against the containing file's directory. paths use the retained
    // Windows drive/UNC rules and generic separators, preserving dot segments.
    // references from ImportDocument are already percent-decoded. descriptor
    // model names are literal paths. neither operation decodes or accesses files.
    [[nodiscard]] ImportResult MeasureImportPath(ArrayView<const char> containingFile,
        ArrayView<const char> reference, size_t& length) noexcept;

    // length excludes the required trailing NUL. inputs and output must not
    // overlap. failure preserves length and every output byte; no allocation.
    [[nodiscard]] ImportResult ResolveImportPath(ArrayView<const char> containingFile,
        ArrayView<const char> reference, ArrayView<char> output, size_t& length) noexcept;

    // the returned extension borrows path. drive/UNC roots, leading dots and
    // alternate streams follow the retained Windows filename rules.
    [[nodiscard]] ImportResult ReadImportPathExtension(ArrayView<const char> path,
        ArrayView<const char>& extension) noexcept;

    [[nodiscard]] ImportResult ReadImportPathFilename(ArrayView<const char> path,
        ArrayView<const char>& filename) noexcept;
}
