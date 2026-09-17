#pragma once

#include "renderer_import_load.h"

namespace uvsr
{
    struct ImportLoadStatusSnapshot
    {
        ImportLoadProgress progress;
        char path[512]{};
    };

    // one loading owner allocates this fixed status block before starting work.
    // Report copies a callback snapshot; Read may run on the UI thread. neither
    // operation allocates. Reset/destruction requires all callers to be joined.
    // the store itself cannot move while it is a callback context.
    class ImportLoadStatus final
    {
    public:
        ImportLoadStatus() noexcept = default;
        ~ImportLoadStatus() noexcept;
        ImportLoadStatus(const ImportLoadStatus&) = delete;
        ImportLoadStatus& operator=(const ImportLoadStatus&) = delete;
        [[nodiscard]] ImportResult Prepare() noexcept;
        [[nodiscard]] ImportLoadStatusSnapshot Read() const noexcept;
        static void Report(void* context, const ImportLoadProgress& progress, ArrayView<const char> path) noexcept;
        void Reset() noexcept;
    private:
        struct State;
        State* m_State = nullptr;
    };
}
