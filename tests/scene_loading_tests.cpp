#include "scene_loading.h"

#include <cstdint>
#include <iostream>

namespace
{
    bool ExpectWorkerCount(uint32_t processors, uint32_t expected)
    {
        const uint32_t actual =
            uvsr::ResolveSceneLoadWorkerCount(processors);
        if (actual == expected)
            return true;

        std::cerr << "FAIL: " << processors << " logical processors resolved to "
                  << actual << " scene workers; expected " << expected << ".\n";
        return false;
    }
}

int main()
{
    bool passed = true;
    passed &= ExpectWorkerCount(0u, 1u);
    passed &= ExpectWorkerCount(1u, 1u);
    passed &= ExpectWorkerCount(2u, 1u);
    passed &= ExpectWorkerCount(3u, 1u);
    passed &= ExpectWorkerCount(4u, 2u);
    passed &= ExpectWorkerCount(8u, 6u);
    passed &= ExpectWorkerCount(9u, 7u);
    passed &= ExpectWorkerCount(10u, 8u);
    passed &= ExpectWorkerCount(16u, 8u);
    passed &= ExpectWorkerCount(128u, 8u);

    uint32_t previousWorkerCount = 0u;
    for (uint32_t processors = 2u; processors <= 256u; ++processors)
    {
        const uint32_t workers =
            uvsr::ResolveSceneLoadWorkerCount(processors);
        passed &= workers >= 1u;
        passed &= workers <= uvsr::MaximumSceneLoadWorkerCount;
        passed &= workers < processors;
        passed &= workers >= previousWorkerCount;
        previousWorkerCount = workers;
    }

    if (!passed)
        return 1;

    std::cout << "UVSR scene-loading policy tests passed.\n";
    return 0;
}
