#pragma once

#include <stddef.h>
#include <stdint.h>

namespace uvsr
{
    // borrowing never extends storage lifetime. the caller provides count live,
    // contiguous objects; IsValid checks shape, not whether memory is accessible.
    template<class T>
    struct ArrayView
    {
        T* data = nullptr;
        size_t count = 0;

        constexpr ArrayView() = default;
        constexpr ArrayView(T* elements, size_t length) : data(elements), count(length) {}
        template<size_t Count>
        constexpr ArrayView(T (&elements)[Count]) : data(elements), count(Count) {}

        [[nodiscard]] bool IsValid() const noexcept
        {
            return count == 0 || (data && count <= SIZE_MAX / sizeof(T) &&
                reinterpret_cast<uintptr_t>(data) % alignof(T) == 0);
        }
    };
}
