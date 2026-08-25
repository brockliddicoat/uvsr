/*
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
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

#include "renderer_resource_contract.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace uvsr
{
    class RendererShaderFactory;

    inline constexpr std::uint32_t RendererMaxConstantBufferVersions = 16u;

    class RendererCommonPasses final
    {
    public:
        RendererCommonPasses(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>& shaderFactory);

        [[nodiscard]] bool IsValid() const;
        [[nodiscard]] nvrhi::IShader* FullscreenVertexShader(
            bool farDepth = false) const;
        [[nodiscard]] nvrhi::ISampler* LinearClampSampler() const;
        [[nodiscard]] nvrhi::ISampler* LinearWrapSampler() const;
        [[nodiscard]] nvrhi::ITexture* BlackTexture() const;
        [[nodiscard]] nvrhi::ITexture* WhiteTexture() const;
        [[nodiscard]] nvrhi::ITexture* BlackCubeArray() const;
        [[nodiscard]] nvrhi::ITexture* BlackDepthArray() const;
        [[nodiscard]] bool HasBlitPipelineFailure() const;

        bool BlitTexture(
            nvrhi::ICommandList* commandList,
            nvrhi::IFramebuffer* targetFramebuffer,
            nvrhi::ITexture* sourceTexture);

    private:
        [[nodiscard]] nvrhi::GraphicsPipelineHandle GetBlitPipeline(
            const nvrhi::FramebufferInfo& framebufferInfo);

        nvrhi::DeviceHandle m_Device;
        nvrhi::ShaderHandle m_FullscreenVS;
        nvrhi::ShaderHandle m_FullscreenAtOneVS;
        nvrhi::ShaderHandle m_BlitPS;
        nvrhi::SamplerHandle m_LinearClampSampler;
        nvrhi::SamplerHandle m_LinearWrapSampler;
        nvrhi::TextureHandle m_BlackTexture;
        nvrhi::TextureHandle m_WhiteTexture;
        nvrhi::TextureHandle m_BlackCubeArray;
        nvrhi::TextureHandle m_BlackDepthArray;
        nvrhi::BindingLayoutHandle m_BlitBindingLayout;
        RendererCommonInitializationContract m_Initialization;
        RendererBlitPipelineFailureLatch m_BlitPipelineFailure;
        std::vector<std::pair<
            nvrhi::FramebufferInfo,
            nvrhi::GraphicsPipelineHandle>> m_BlitPipelines;
    };
}
