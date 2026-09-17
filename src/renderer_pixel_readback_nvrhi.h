#pragma once

#include "renderer_pixel_readback.h"

#include <nvrhi/nvrhi.h>

namespace uvsr
{
    // creation consumes these borrowed handles synchronously; the published
    // readback retains the device, graphics list, source and shader itself.
    struct RendererPixelReadbackNvrhi
    {
        static RendererReadbackError Initialize(
            RendererPixelReadback& readback,
            nvrhi::IDevice* device,
            nvrhi::ICommandList* graphicsCommands,
            nvrhi::IShader* shader,
            nvrhi::ITexture* source);
    };
}
