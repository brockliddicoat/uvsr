#pragma once

#include "renderer_gpu_scalar.h"

namespace uvsr
{
    struct RendererScenePlane
    {
        gpu_contract::Float3 normal{};
        float distance = 0;
    };

    struct RendererSceneFrustum
    {
        // outward normals, near/far/left/right/top/bottom. dot(normal, point) <= distance is inside.
        RendererScenePlane planes[6];
    };
}
