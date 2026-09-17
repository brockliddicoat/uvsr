#include "checked_worklist.h"

#include <stdio.h>

#if defined(UVSR_REQUIRE_NO_EXCEPTIONS) && (defined(_CPPUNWIND) || defined(__cpp_exceptions))
#error this probe must compile without C++ exceptions
#endif

int main()
{
    uint32_t storage[2]{};
    uvsr::CheckedWorklist<uint32_t> work{ uvsr::ArrayView<uint32_t>(storage) };
    uint32_t output = 99;
    if (!work.IsValid() || work.Remaining() != 2 || work.TryPop(output) || output != 99 ||
        !work.TryPush(7) || !work.TryPush(11) || work.TryPush(13) || work.Remaining() != 0 ||
        !work.TryPop(output) || output != 11 || !work.TryPop(output) || output != 7 ||
        work.TryPop(output) || output != 7 || work.Remaining() != 2)
        return 1;
    uvsr::CheckedWorklist<uint32_t> empty{ {} };
    uvsr::CheckedWorklist<uint32_t> missing{ { nullptr, 1 } };
    uvsr::CheckedWorklist<uint32_t> oversized{ { storage, SIZE_MAX / sizeof(uint32_t) + 1 } };
    if (!empty.IsValid() || empty.TryPush(1) || empty.TryPop(output) ||
        missing.IsValid() || missing.TryPush(1) || oversized.IsValid() || oversized.TryPush(1))
        return 2;
    alignas(uint32_t) unsigned char bytes[2 * sizeof(uint32_t)]{};
    const uvsr::ArrayView<uint32_t> misaligned{ reinterpret_cast<uint32_t*>(bytes + 1), 1 };
    if (misaligned.IsValid())
        return 3;
    const uvsr::ArrayView<const uint32_t> readOnly(storage);
    if (!readOnly.IsValid() || readOnly.count != 2 || readOnly.data != storage)
        return 4;

    // all maximum-width median task sizes, without allocating scene-sized data.
    uint32_t pendingStorage[30]{};
    uvsr::CheckedWorklist<uint32_t> pending{ uvsr::ArrayView<uint32_t>(pendingStorage) };
    if (!pending.TryPush(UINT32_MAX))
        return 5;
    unsigned peak = 1;
    uint32_t count = 0;
    while (pending.TryPop(count) && count > 8)
    {
        if (pending.Remaining() < 2 || !pending.TryPush(count - count / 2) || !pending.TryPush(count / 2))
            return 6;
        peak = unsigned(30 - pending.Remaining());
    }
    if (count != 7 || peak != 30)
        return 7;
    puts("checked LIFO capacity, unchanged failure output, borrowed storage and uint32 depth bound passed");
    return 0;
}
