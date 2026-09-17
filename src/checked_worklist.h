#pragma once

#include "array_view.h"

namespace uvsr
{
    // LIFO scratch over already-live plain records. backing storage must outlive
    // this worklist. no ownership, allocation, growth or destruction of elements.
    template<class T>
    class CheckedWorklist final
    {
        static_assert(noexcept(*static_cast<T*>(nullptr) = *static_cast<const T*>(nullptr)),
            "worklist assignment must not throw");
    public:
        explicit CheckedWorklist(ArrayView<T> storage) noexcept : m_Storage(storage) {}
        CheckedWorklist(const CheckedWorklist&) = delete;
        CheckedWorklist& operator=(const CheckedWorklist&) = delete;

        [[nodiscard]] bool IsValid() const noexcept { return m_Storage.IsValid(); }
        [[nodiscard]] size_t Remaining() const noexcept
        {
            return IsValid() ? m_Storage.count - m_Count : 0;
        }
        [[nodiscard]] bool TryPush(const T& value) noexcept
        {
            if (Remaining() == 0)
                return false;
            m_Storage.data[m_Count] = value;
            ++m_Count;
            return true;
        }
        [[nodiscard]] bool TryPop(T& output) noexcept
        {
            if (m_Count == 0)
                return false;
            output = m_Storage.data[m_Count - 1];
            --m_Count;
            return true;
        }

    private:
        ArrayView<T> m_Storage;
        size_t m_Count = 0;
    };
}
