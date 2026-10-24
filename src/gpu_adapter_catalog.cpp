#include "gpu_adapter_catalog.h"
#include "settings_snapshot_storage.h"
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

namespace uvsr
{
    namespace
    {
        bool Fail(SettingsSnapshotError& error, SettingsSnapshotErrorCode code, const char* message) noexcept
        {
            error = {};
            error.code = code;
            error.message = message;
            return false;
        }
    }

    GpuAdapterCatalog::~GpuAdapterCatalog() noexcept { Clear(); }

    GpuAdapterCatalog::GpuAdapterCatalog(GpuAdapterCatalog&& other) noexcept
        : m_Entries(other.m_Entries), m_Count(other.m_Count), m_Capacity(other.m_Capacity)
    {
        other.m_Entries = nullptr;
        other.m_Count = other.m_Capacity = 0;
    }

    GpuAdapterCatalog& GpuAdapterCatalog::operator=(GpuAdapterCatalog&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            m_Entries = other.m_Entries;
            m_Count = other.m_Count;
            m_Capacity = other.m_Capacity;
            other.m_Entries = nullptr;
            other.m_Count = other.m_Capacity = 0;
        }
        return *this;
    }

    void GpuAdapterCatalog::Clear() noexcept
    {
        for (size_t index = 0; index < m_Count; ++index)
        {
            std::free(const_cast<char*>(m_Entries[index].name.data()));
            m_Entries[index].~GpuAdapterChoice();
        }
        std::free(m_Entries);
        m_Entries = nullptr;
        m_Count = m_Capacity = 0;
    }

    bool GpuAdapterCatalog::Append(const GpuAdapterChoice& choice, SettingsSnapshotError& error) noexcept
    {
        // the incoming record and its name may borrow this catalog across growth.
        GpuAdapterChoice prepared = choice;
        const std::string_view name = prepared.name;
        if ((!name.data() && !name.empty()) ||
            name.size() > UINTPTR_MAX - reinterpret_cast<uintptr_t>(name.data()))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "graphics adapter name is invalid");
        constexpr size_t MaximumCount = size_t(PTRDIFF_MAX) / sizeof(GpuAdapterChoice);
        if (m_Count == MaximumCount || name.size() >= size_t(PTRDIFF_MAX))
            return Fail(error, SettingsSnapshotErrorCode::Capacity, "graphics adapter catalog exceeds addressable storage");

        char* text = static_cast<char*>(std::malloc(name.size() + 1));
        if (!text)
            return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "could not allocate graphics adapter name");
        if (!name.empty()) std::memcpy(text, name.data(), name.size());
        text[name.size()] = 0;
        prepared.name = {text, name.size()};

        if (m_Count == m_Capacity)
        {
            const size_t capacity = m_Capacity == 0 ? 1 :
                (m_Capacity > MaximumCount / 2 ? MaximumCount : m_Capacity * 2);
            static_assert(alignof(GpuAdapterChoice) <= alignof(std::max_align_t));
            static_assert(std::is_nothrow_copy_constructible_v<GpuAdapterChoice>);
            static_assert(std::is_trivially_destructible_v<GpuAdapterChoice>);
            auto* entries = static_cast<GpuAdapterChoice*>(std::malloc(capacity * sizeof(GpuAdapterChoice)));
            if (!entries)
            {
                std::free(text);
                return Fail(error, SettingsSnapshotErrorCode::OutOfMemory, "could not allocate graphics adapter records");
            }
            for (size_t index = 0; index < m_Count; ++index)
            {
                new (entries + index) GpuAdapterChoice(m_Entries[index]);
                m_Entries[index].~GpuAdapterChoice();
            }
            std::free(m_Entries);
            m_Entries = entries;
            m_Capacity = capacity;
        }
        new (m_Entries + m_Count) GpuAdapterChoice(prepared);
        ++m_Count;
        error = {};
        return true;
    }
}
