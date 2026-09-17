#include "ui_light_defaults.h"
#include <stdio.h>

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool value, const char* message) noexcept
    {
        ++checks;
        if (!value) { ++failures; fprintf(stderr, "light defaults: %s\n", message); }
    }
}

bool TestUiLightDefaultsOwnership() noexcept
{
    using namespace uvsr;
    UiLightDefaults first, later, output;
    first.intensity = 17.f;
    later.intensity = 91.f;
    UiLightDefaultsError error;
    for (size_t fail = 0; fail < 2; ++fail)
    {
        FailUiLightDefaultsAllocationAfter(fail);
        UiLightDefaultsCache cache;
        output = later;
        Check(cache.Count() == 0 && cache.Capacity() == 0, "construction owns no allocation");
        Check(!cache.ReadOrCapture({"scene", 5, 0, "sun", 3}, first, output, error) &&
            error == UiLightDefaultsError::Allocation && output.intensity == later.intensity &&
            cache.Count() == 0 && cache.Capacity() == 0, "first capture failure preserves cache and output");
        ClearUiLightDefaultsAllocationFailure();
    }

    UiLightDefaultsCache cache;
    char scene[] = "a\n1", light[] = "b";
    Check(cache.ReadOrCapture({scene, 3, 2, light, 1}, first, output, error), "first capture succeeds");
    scene[0] = 'x';
    light[0] = 'y';
    FailUiLightDefaultsAllocationAfter(0);
    Check(cache.ReadOrCapture({"a", 1, 1, "2\nb", 3}, later, output, error) &&
        output.intensity == first.intensity && cache.Count() == 1,
        "delimiter aliases retain the first independently owned value without allocation");
    ClearUiLightDefaultsAllocationFailure();

    constexpr char nulScene[] = {'a', '\0', 'b'};
    Check(cache.ReadOrCapture({nulScene, 3, UINT32_MAX, "", 0}, first, output, error),
        "counted NUL and invalid editable ordinal are preserved");
    Check(cache.ReadOrCapture({"a", 1, UINT32_MAX, "", 0}, later, output, error) &&
        output.intensity == later.intensity, "NUL bytes participate in key equality");

    for (uint32_t ordinal = 0; ordinal < 160; ++ordinal)
    {
        const size_t priorCount = cache.Count(), priorCapacity = cache.Capacity();
        output = later;
        FailUiLightDefaultsAllocationAfter(0);
        Check(!cache.ReadOrCapture({"growth", 6, ordinal, "sun", 3}, first, output, error) &&
            error == UiLightDefaultsError::Allocation && output.intensity == later.intensity &&
            cache.Count() == priorCount && cache.Capacity() == priorCapacity,
            "allocation failure preserves all live state during growth");
        ClearUiLightDefaultsAllocationFailure();
        Check(cache.ReadOrCapture({"growth", 6, ordinal, "sun", 3}, first, output, error),
            "retry captures once after failure");
        FailUiLightDefaultsAllocationAfter(0);
        Check(cache.ReadOrCapture({"a", 1, 1, "2\nb", 3}, later, output, error) &&
            output.intensity == first.intensity, "earlier keys survive every growth");
        ClearUiLightDefaultsAllocationFailure();
    }
    const size_t priorCount = cache.Count();
    output = later;
    Check(!cache.ReadOrCapture({nullptr, 1, 0, "", 0}, first, output, error) &&
        error == UiLightDefaultsError::InvalidInput && cache.Count() == priorCount &&
        output.intensity == later.intensity, "invalid counted input preserves output");
    Check(!cache.ReadOrCapture({reinterpret_cast<const char*>(1), size_t(PTRDIFF_MAX), 0, "", 0},
        first, output, error) && error == UiLightDefaultsError::Capacity &&
        cache.Count() == priorCount && output.intensity == later.intensity,
        "capacity is rejected before source access");
    FailUiLightDefaultsAllocationAfter(0);
    Check(cache.ReadOrCapture({"growth", 6, 159, "sun", 3}, output, output, error) &&
        output.intensity == first.intensity, "input and output may alias on a hit");
    cache.Clear();
    cache.Clear();
    Check(cache.Count() == 0 && cache.Capacity() == 0, "clear is iterative and reusable");
    ClearUiLightDefaultsAllocationFailure();
    Check(cache.ReadOrCapture({nullptr, 0, 0, nullptr, 0}, first, output, error),
        "empty components and reuse succeed");
    printf("light default ownership checks: %u, failures: %u\n", checks, failures);
    return failures == 0;
}
