/*
* Copyright (c) 2014-2025, NVIDIA CORPORATION. All rights reserved.
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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

/*
License for Dear ImGui

Copyright (c) 2014-2025 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <nvrhi/nvrhi.h>
#include <stddef.h>

struct ImDrawVert;
struct ImDrawData;

namespace uvsr
{
    class RendererShaderFactory;
    class RendererNvrhiMessageCallback;
    struct RendererUiTestAccess;

    class RendererUiNvrhi final
    {
    public:
        RendererUiNvrhi() noexcept = default;
        ~RendererUiNvrhi() noexcept;
        RendererUiNvrhi(const RendererUiNvrhi&) = delete;
        RendererUiNvrhi& operator=(const RendererUiNvrhi&) = delete;

        [[nodiscard]] bool Init(nvrhi::IDevice* device, RendererShaderFactory& shaders,
            const RendererNvrhiMessageCallback* messages) noexcept;
        [[nodiscard]] bool UpdateFontTexture() noexcept;
        [[nodiscard]] bool Render(nvrhi::IFramebuffer* framebuffer) noexcept;
        void BackBufferResizing() noexcept;

    private:
        struct FramebufferEntry;
        [[nodiscard]] nvrhi::IFramebuffer* ResolveFramebuffer(nvrhi::IFramebuffer* source) noexcept;
        [[nodiscard]] bool UpdateGeometry(const ImDrawData& data) noexcept;
        [[nodiscard]] bool EnsureCommandList() noexcept;
        [[nodiscard]] bool Healthy(uint64_t before) const noexcept;
        [[nodiscard]] uint64_t ErrorCount() const noexcept;
        friend struct RendererUiTestAccess;
        static void FailAllocationAfter(size_t count) noexcept;
        static void FailOperationAfter(size_t count) noexcept;
        static bool FailureReached() noexcept;
        nvrhi::DeviceHandle m_Device;
        const RendererNvrhiMessageCallback* m_Messages = nullptr;
        nvrhi::CommandListHandle m_CommandList;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::SamplerHandle m_Sampler;
        nvrhi::TextureHandle m_FontTexture;
        nvrhi::BindingSetHandle m_FontBinding;
        nvrhi::GraphicsPipelineDesc m_PipelineDesc;
        nvrhi::GraphicsPipelineHandle m_Pipeline;
        nvrhi::FramebufferInfo m_PipelineInfo;
        FramebufferEntry* m_Framebuffers = nullptr;
        nvrhi::BufferHandle m_VertexBuffer, m_IndexBuffer;
        ImDrawVert* m_Vertices = nullptr;
        unsigned char* m_Indices = nullptr;
        size_t m_VertexCapacity = 0, m_IndexCapacity = 0;
        bool m_Initialized = false;
    };
}
