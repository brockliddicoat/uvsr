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
#include <stddef.h>

namespace uvsr
{
    class RendererShaderFactory;

#if defined(UVSR_BLIT_PIPELINE_TEST_HOOKS)
    void FailNextRendererBlitPipelineAllocation() noexcept;
    [[nodiscard]] bool RendererBlitPipelineAllocationFailurePending() noexcept;
#endif

    class RendererCommonPasses final
    {
    public:
        RendererCommonPasses(
            nvrhi::IDevice* device,
            RendererShaderFactory* shaderFactory);

        [[nodiscard]] bool IsValid() const;
        [[nodiscard]] nvrhi::IShader* FullscreenVertexShader(
            bool farDepth = false) const;
        [[nodiscard]] nvrhi::ISampler* LinearClampSampler() const;
        [[nodiscard]] nvrhi::ISampler* LinearWrapSampler() const;
        [[nodiscard]] nvrhi::ITexture* BlackTexture() const;
        [[nodiscard]] nvrhi::ITexture* WhiteTexture() const;
        [[nodiscard]] nvrhi::ITexture* BlackCubeArray() const;
        [[nodiscard]] bool HasBlitPipelineFailure() const;

        bool BlitTexture(
            nvrhi::ICommandList* commandList,
            nvrhi::IFramebuffer* targetFramebuffer,
            nvrhi::ITexture* sourceTexture);
        bool BlitTextureMip(
            nvrhi::ICommandList* commandList,
            nvrhi::IFramebuffer* targetFramebuffer,
            nvrhi::ITexture* sourceTexture,
            uint32_t sourceMip);

    private:
        class BlitPipelineCache final
        {
        public:
            BlitPipelineCache() noexcept = default;
            ~BlitPipelineCache() noexcept;
            BlitPipelineCache(const BlitPipelineCache&) = delete;
            BlitPipelineCache& operator=(const BlitPipelineCache&) = delete;
            [[nodiscard]] nvrhi::GraphicsPipelineHandle Find(
                const nvrhi::FramebufferInfo& framebuffer) const noexcept;
            [[nodiscard]] bool Append(const nvrhi::FramebufferInfo& framebuffer,
                const nvrhi::GraphicsPipelineHandle& pipeline) noexcept;
        private:
            struct Entry
            {
                nvrhi::FramebufferInfo framebuffer;
                nvrhi::GraphicsPipelineHandle pipeline;
            };
            Entry* m_Entries = nullptr;
            size_t m_Count = 0;
            size_t m_Capacity = 0;
        };

        bool BlitTextureSubresources(nvrhi::ICommandList* commandList,
            nvrhi::IFramebuffer* targetFramebuffer, nvrhi::ITexture* sourceTexture,
            nvrhi::TextureSubresourceSet sourceSubresources);
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
        nvrhi::BindingLayoutHandle m_BlitBindingLayout;
        RendererCommonInitializationContract m_Initialization;
        RendererBlitPipelineFailureLatch m_BlitPipelineFailure;
        BlitPipelineCache m_BlitPipelines;
    };
}
