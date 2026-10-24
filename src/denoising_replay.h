#pragma once

#include "renderer_gpu_contract.h"
#include <nvrhi/nvrhi.h>
#include <cstdint>

namespace uvsr
{
    struct DenoisingReplayInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* selection = nullptr;
        PlanarViewConstants previousView{};
        uint64_t frame = ~uint64_t(0);
        bool valid = false;
        bool stationaryView = false;
    };
}
