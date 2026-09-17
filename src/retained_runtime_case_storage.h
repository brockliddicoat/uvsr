#pragma once

#include "settings_snapshot_storage.h"

namespace uvsr
{
    // implemented only for retained case and setting records. successful mutation
    // can invalidate views; failed reserve/append preserves both operands.
    template<class T> class RetainedRuntimeList final
    {
    public:
        RetainedRuntimeList() noexcept = default;
        ~RetainedRuntimeList() noexcept;
        RetainedRuntimeList(const RetainedRuntimeList&) = delete;
        RetainedRuntimeList& operator=(const RetainedRuntimeList&) = delete;
        RetainedRuntimeList(RetainedRuntimeList&& other) noexcept;
        RetainedRuntimeList& operator=(RetainedRuntimeList&& other) noexcept;

        [[nodiscard]] size_t Count() const noexcept { return m_Count; }
        [[nodiscard]] T* Data() noexcept { return m_Entries; }
        [[nodiscard]] const T* Data() const noexcept { return m_Entries; }
        [[nodiscard]] T* begin() noexcept { return m_Entries; }
        [[nodiscard]] const T* begin() const noexcept { return m_Entries; }
        [[nodiscard]] T* end() noexcept { return m_Count ? m_Entries + m_Count : m_Entries; }
        [[nodiscard]] const T* end() const noexcept { return m_Count ? m_Entries + m_Count : m_Entries; }
        [[nodiscard]] T& operator[](size_t index) noexcept { return m_Entries[index]; }
        [[nodiscard]] const T& operator[](size_t index) const noexcept { return m_Entries[index]; }
        [[nodiscard]] T& Back() noexcept { return m_Entries[m_Count - 1]; }
        [[nodiscard]] const T& Back() const noexcept { return m_Entries[m_Count - 1]; }
        [[nodiscard]] bool Reserve(size_t capacity, SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool Append(T&& value, SettingsSnapshotError& error) noexcept;

    private:
        T* m_Entries = nullptr;
        size_t m_Count = 0;
        size_t m_Capacity = 0;
        void Clear() noexcept;
    };

#if defined(UVSR_RETAINED_CASE_TEST_HOOKS)
    void FailRetainedCaseAllocationAfter(size_t successfulAllocations) noexcept;
    void ClearRetainedCaseAllocationFailure() noexcept;
#endif
}
