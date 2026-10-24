#include "ui_light_defaults.h"
#include <new>
#include <string.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_UI_LIGHT_DEFAULTS_TEST_HOOKS)
        thread_local size_t AllocationsBeforeFailure = SIZE_MAX;
#endif
        bool CanAllocate() noexcept
        {
#if defined(UVSR_UI_LIGHT_DEFAULTS_TEST_HOOKS)
            if (AllocationsBeforeFailure != SIZE_MAX)
            {
                if (!AllocationsBeforeFailure) return false;
                --AllocationsBeforeFailure;
            }
#endif
            return true;
        }

        struct Part { const char* data; size_t size; };
        struct KeyPlan
        {
            Part parts[5]{};
            char ordinal[10]{};
            size_t size = 0;
            uint64_t hash = 14695981039346656037ull;
        };

        bool Fail(UiLightDefaultsError& error, UiLightDefaultsError value) noexcept
        {
            error = value;
            return false;
        }

        bool Prepare(const UiLightDefaultsKey& key, size_t maximumSize, KeyPlan& plan,
            UiLightDefaultsError& error) noexcept
        {
            if ((!key.scene && key.sceneSize) || (!key.light && key.lightSize) ||
                key.sceneSize > UINTPTR_MAX - reinterpret_cast<uintptr_t>(key.scene) ||
                key.lightSize > UINTPTR_MAX - reinterpret_cast<uintptr_t>(key.light))
                return Fail(error, UiLightDefaultsError::InvalidInput);
            if (key.sceneSize > size_t(PTRDIFF_MAX) || key.lightSize > size_t(PTRDIFF_MAX))
                return Fail(error, UiLightDefaultsError::Capacity);
            size_t first = sizeof(plan.ordinal);
            uint32_t ordinal = key.ordinal;
            do
            {
                plan.ordinal[--first] = char('0' + ordinal % 10);
                ordinal /= 10;
            } while (ordinal);
            plan.parts[0] = {key.scene, key.sceneSize};
            plan.parts[1] = {"\n", 1};
            plan.parts[2] = {plan.ordinal + first, sizeof(plan.ordinal) - first};
            plan.parts[3] = {"\n", 1};
            plan.parts[4] = {key.light, key.lightSize};
            for (const Part& part : plan.parts)
            {
                if (part.size > maximumSize - plan.size)
                    return Fail(error, UiLightDefaultsError::Capacity);
                plan.size += part.size;
            }
            for (const Part& part : plan.parts)
                for (size_t index = 0; index < part.size; ++index)
                {
                    plan.hash ^= static_cast<unsigned char>(part.data[index]);
                    plan.hash *= 1099511628211ull;
                }
            return true;
        }
    }

    struct UiLightDefaultsCache::Entry
    {
        uint64_t hash;
        size_t keySize;
        UiLightDefaults value;

        const char* Key() const noexcept { return reinterpret_cast<const char*>(this + 1); }
        bool Matches(const KeyPlan& plan) const noexcept
        {
            if (hash != plan.hash || keySize != plan.size) return false;
            size_t offset = 0;
            for (const Part& part : plan.parts)
            {
                if (part.size && memcmp(Key() + offset, part.data, part.size)) return false;
                offset += part.size;
            }
            return true;
        }
    };

    UiLightDefaultsCache::~UiLightDefaultsCache() noexcept { Clear(); }

    void UiLightDefaultsCache::Clear() noexcept
    {
        for (size_t index = 0; index < m_Capacity; ++index)
            if (Entry* entry = m_Entries[index])
            {
                auto* storage = reinterpret_cast<unsigned char*>(entry);
                entry->~Entry();
                delete[] storage;
            }
        delete[] m_Entries;
        m_Entries = nullptr;
        m_Capacity = m_Count = 0;
    }

    bool UiLightDefaultsCache::ReadOrCapture(const UiLightDefaultsKey& key,
        const UiLightDefaults& current, UiLightDefaults& output, UiLightDefaultsError& error) noexcept
    {
        error = UiLightDefaultsError::None;
        KeyPlan plan;
        static_assert(alignof(Entry) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
        if (!Prepare(key, size_t(PTRDIFF_MAX) - sizeof(Entry), plan, error)) return false;
        if (m_Capacity)
        {
            size_t index = size_t(plan.hash) & (m_Capacity - 1);
            while (const Entry* entry = m_Entries[index])
            {
                if (entry->Matches(plan))
                {
                    output = entry->value;
                    return true;
                }
                index = (index + 1) & (m_Capacity - 1);
            }
        }
        Entry** grown = nullptr;
        size_t capacity = m_Capacity;
        // a half-full power-of-two table bounds probe termination without a cap on entries.
        if (m_Count >= m_Capacity / 2)
        {
            if (m_Capacity > size_t(PTRDIFF_MAX) / sizeof(Entry*) / 2)
                return Fail(error, UiLightDefaultsError::Capacity);
            capacity = m_Capacity ? m_Capacity * 2 : 8;
            grown = CanAllocate() ? new (std::nothrow) Entry*[capacity]{} : nullptr;
            if (!grown) return Fail(error, UiLightDefaultsError::Allocation);
        }
        const size_t bytes = sizeof(Entry) + plan.size;
        auto* storage = CanAllocate() ? new (std::nothrow) unsigned char[bytes] : nullptr;
        if (!storage)
        {
            delete[] grown;
            return Fail(error, UiLightDefaultsError::Allocation);
        }
        auto* candidate = ::new (storage) Entry{plan.hash, plan.size, current};
        auto* destination = ::new (storage + sizeof(Entry)) char[plan.size];
        for (const Part& part : plan.parts)
        {
            if (part.size) memcpy(destination, part.data, part.size);
            destination += part.size;
        }

        Entry** table = grown ? grown : m_Entries;
        if (grown)
            for (size_t old = 0; old < m_Capacity; ++old)
                if (Entry* entry = m_Entries[old])
                {
                    size_t index = size_t(entry->hash) & (capacity - 1);
                    while (table[index]) index = (index + 1) & (capacity - 1);
                    table[index] = entry;
                }
        size_t index = size_t(plan.hash) & (capacity - 1);
        while (table[index]) index = (index + 1) & (capacity - 1);
        table[index] = candidate;
        if (grown)
        {
            delete[] m_Entries;
            m_Entries = grown;
            m_Capacity = capacity;
        }
        ++m_Count;
        output = candidate->value;
        return true;
    }

#if defined(UVSR_UI_LIGHT_DEFAULTS_TEST_HOOKS)
    void FailUiLightDefaultsAllocationAfter(size_t count) noexcept { AllocationsBeforeFailure = count; }
    void ClearUiLightDefaultsAllocationFailure() noexcept { AllocationsBeforeFailure = SIZE_MAX; }
#endif
}
