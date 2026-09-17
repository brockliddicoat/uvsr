#pragma once

#include "renderer_import.h"

namespace uvsr
{
    struct ImportDescriptionLimits
    {
        // candidate first-party storage only. parser internals and an existing
        // description retained during replacement are measured separately.
        size_t maxStorageBytes = SIZE_MAX;
        size_t maxScratchBytes = SIZE_MAX;
    };

    struct ImportDescriptionState;
    struct ImportDescriptionAccess;

    // one loading operation owns the flat application scene description. glTF
    // parsing remains separate. all returned text borrows this owner until a
    // successful Parse, reset, move assignment or destruction.
    class ImportSceneDescription final
    {
    public:
        ImportSceneDescription() noexcept = default;
        ~ImportSceneDescription() noexcept;
        ImportSceneDescription(const ImportSceneDescription&) = delete;
        ImportSceneDescription& operator=(const ImportSceneDescription&) = delete;
        ImportSceneDescription(ImportSceneDescription&& other) noexcept;
        ImportSceneDescription& operator=(ImportSceneDescription&& other) noexcept;

        // input is borrowed synchronously. failure preserves the current owner.
        [[nodiscard]] ImportResult Parse(ArrayView<const uint8_t> json, ArrayView<const char> fileName,
            const ImportDescriptionLimits& limits = {}) noexcept;
        [[nodiscard]] size_t ModelCount() const noexcept;
        [[nodiscard]] ArrayView<const char> ModelPath(size_t index) const noexcept;
        [[nodiscard]] size_t StorageBytes() const noexcept;
        [[nodiscard]] size_t ScratchBytes() const noexcept;
        void Reset() noexcept;

    private:
        ImportDescriptionState* m_State = nullptr;
        friend struct ImportDescriptionAccess;
    };
}
