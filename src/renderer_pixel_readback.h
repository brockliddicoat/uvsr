/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include "renderer_pixel_readback_cb.h"
#include "renderer_resource_contract.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace uvsr
{
    class RendererShaderFactory;

    class RendererPixelReadback final
    {
    public:
        RendererPixelReadback(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>& shaderFactory,
            nvrhi::ITexture* inputTexture);

        [[nodiscard]] bool IsValid() const;
        bool Capture(
            nvrhi::ICommandList* commandList,
            std::uint32_t x,
            std::uint32_t y);
        [[nodiscard]] std::optional<RendererReadbackUint4> ReadUInts();

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::ShaderHandle m_Shader;
        nvrhi::ComputePipelineHandle m_Pipeline;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BufferHandle m_IntermediateBuffer;
        nvrhi::BufferHandle m_ReadbackBuffer;
        RendererPixelReadbackInitializationContract m_Initialization;
        bool m_CapturePending = false;
    };
}
