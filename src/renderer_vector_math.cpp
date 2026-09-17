#include "renderer_vector_math.h"
#include <math.h>

namespace uvsr
{
    float Length(gpu_contract::Float3 value) noexcept
    {
        return sqrtf(LengthSquared(value));
    }

    gpu_contract::Float3 Normalize(gpu_contract::Float3 value) noexcept
    {
        // preserve division and the existing zero/non-finite result at callers.
        return value / Length(value);
    }
}
