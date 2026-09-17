#include "renderer_import_load_status.h"
#include "import/renderer_import_allocation.h"

#include <Windows.h>
#include <new>
#include <stdlib.h>
#include <string.h>

namespace uvsr
{
    struct ImportLoadStatus::State
    {
        SRWLOCK lock = SRWLOCK_INIT;
        ImportLoadStatusSnapshot snapshot;
    };

    ImportLoadStatus::~ImportLoadStatus() noexcept { Reset(); }
    ImportResult ImportLoadStatus::Prepare() noexcept
    {
        if (m_State) return {ImportError::InvalidState};
        auto* storage = ImportAllocate(sizeof(State));
        if (!storage) return {ImportError::OutOfMemory};
        m_State = new (storage) State{}; return {};
    }
    ImportLoadStatusSnapshot ImportLoadStatus::Read() const noexcept
    {
        if (!m_State) return {};
        AcquireSRWLockShared(&m_State->lock);
        const auto result = m_State->snapshot;
        ReleaseSRWLockShared(&m_State->lock);
        return result;
    }
    void ImportLoadStatus::Report(void* context, const ImportLoadProgress& progress, ArrayView<const char> path) noexcept
    {
        auto* self = static_cast<ImportLoadStatus*>(context);
        if (!self || !self->m_State) return;
        ImportLoadStatusSnapshot candidate;
        candidate.progress = progress;
        if (path.IsValid())
        {
            const size_t length = path.count < sizeof(candidate.path) ? path.count : sizeof(candidate.path) - 1;
            if (length) memcpy(candidate.path, path.data, length);
            candidate.path[length] = '\0';
            candidate.progress.pathTruncated = candidate.progress.pathTruncated || length != path.count;
        }
        auto& state = *self->m_State;
        AcquireSRWLockExclusive(&state.lock);
        state.snapshot = candidate;
        ReleaseSRWLockExclusive(&state.lock);
    }
    void ImportLoadStatus::Reset() noexcept
    {
        if (m_State) { m_State->~State(); free(m_State); m_State = nullptr; }
    }
}
