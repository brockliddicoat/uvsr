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

#include "renderer_ui_nvrhi.h"
#include "renderer_ui_contract.h"
#include "renderer_shader_factory_nvrhi.h"
#include "renderer_nvrhi_message_callback.h"

#include <imgui.h>
#include <cmath>
#include <limits.h>
#include <new>
#include <string.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_BUILD_TESTING)
        size_t allocationCountdown = SIZE_MAX;
        size_t operationCountdown = SIZE_MAX;
        bool failureReached = false;
#endif
        bool CanAllocate() noexcept
        {
#if defined(UVSR_BUILD_TESTING)
            if (allocationCountdown == 0) return false;
            if (allocationCountdown != SIZE_MAX) --allocationCountdown;
#endif
            return true;
        }

        bool CanCreate() noexcept
        {
#if defined(UVSR_BUILD_TESTING)
            if (operationCountdown == 0) { failureReached = true; return false; }
            if (operationCountdown != SIZE_MAX) --operationCountdown;
#endif
            return true;
        }

        bool FinitePositive(float value) noexcept
        {
            return std::isfinite(value) && value > 0.f;
        }

        bool Validate(const ImDrawData& data, ImTextureID atlas) noexcept
        {
            if (!data.Valid || data.TotalVtxCount < 0 || data.TotalIdxCount < 0 ||
                data.CmdListsCount < 0 || data.CmdListsCount != data.CmdLists.Size ||
                (data.CmdListsCount && !data.CmdLists.Data) ||
                data.DisplayPos.x != 0.f || data.DisplayPos.y != 0.f)
                return false;
            uint64_t vertices = 0, indices = 0;
            for (const ImDrawList* list : data.CmdLists)
            {
                if (!list || list->VtxBuffer.Size < 0 || list->IdxBuffer.Size < 0 ||
                    list->CmdBuffer.Size < 0 ||
                    (list->VtxBuffer.Size && !list->VtxBuffer.Data) ||
                    (list->IdxBuffer.Size && !list->IdxBuffer.Data) ||
                    (list->CmdBuffer.Size && !list->CmdBuffer.Data))
                    return false;
                vertices += uint32_t(list->VtxBuffer.Size);
                indices += uint32_t(list->IdxBuffer.Size);
                for (const ImDrawCmd& command : list->CmdBuffer)
                {
                    if (command.UserCallback) continue;
                    if (command.TexRef.GetTexID() != atlas ||
                        uint64_t(command.IdxOffset) + command.ElemCount > uint32_t(list->IdxBuffer.Size) ||
                        command.VtxOffset > uint32_t(list->VtxBuffer.Size) ||
                        (command.ElemCount && command.VtxOffset == uint32_t(list->VtxBuffer.Size)) ||
                        !std::isfinite(command.ClipRect.x) || !std::isfinite(command.ClipRect.y) ||
                        !std::isfinite(command.ClipRect.z) || !std::isfinite(command.ClipRect.w))
                        return false;
                }
            }
            return vertices == uint32_t(data.TotalVtxCount) && indices == uint32_t(data.TotalIdxCount);
        }

        nvrhi::BufferHandle CreateGeometryBuffer(nvrhi::IDevice* device,
            size_t capacity, size_t stride, bool indices) noexcept
        {
            if (capacity > UINT32_MAX / stride) return nullptr;
            nvrhi::BufferDesc desc;
            desc.byteSize = capacity * stride;
            desc.debugName = indices ? "ImGui index buffer" : "ImGui vertex buffer";
            desc.isVertexBuffer = !indices;
            desc.isIndexBuffer = indices;
            desc.initialState = indices ? nvrhi::ResourceStates::IndexBuffer : nvrhi::ResourceStates::VertexBuffer;
            desc.keepInitialState = true;
            return CanCreate() ? device->createBuffer(desc) : nullptr;
        }
    }

    struct RendererUiNvrhi::FramebufferEntry
    {
        FramebufferEntry* next = nullptr;
        nvrhi::FramebufferHandle source, unorm;
    };

    RendererUiNvrhi::~RendererUiNvrhi() noexcept
    {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().Fonts->TexRef.GetTexID() ==
            ImTextureRef(m_FontTexture.Get()).GetTexID())
            ImGui::GetIO().Fonts->TexRef = ImTextureRef();
        delete[] m_Vertices;
        delete[] m_Indices;
        BackBufferResizing();
    }

    uint64_t RendererUiNvrhi::ErrorCount() const noexcept
    {
        return m_Messages ? m_Messages->GetErrorCount() : 0;
    }

    bool RendererUiNvrhi::Healthy(uint64_t before) const noexcept
    {
        return ErrorCount() == before;
    }

    bool RendererUiNvrhi::EnsureCommandList() noexcept
    {
        if (!m_CommandList)
            m_CommandList = CanCreate() ? m_Device->createCommandList() : nullptr;
        return bool(m_CommandList);
    }

    bool RendererUiNvrhi::Init(nvrhi::IDevice* device, RendererShaderFactory& shaders,
        const RendererNvrhiMessageCallback* messages) noexcept
    {
        if (!device || m_Initialized) return false;
        m_Device = device;
        m_Messages = messages;
        const uint64_t before = ErrorCount();
        m_CommandList = CanCreate() ? device->createCommandList() : nullptr;
        auto vertex = CanCreate() ? shaders.CreateShader("uvsr/renderer_imgui_vertex.hlsl", "main", {}, nvrhi::ShaderType::Vertex) : nullptr;
        auto pixel = CanCreate() ? shaders.CreateShader("uvsr/renderer_imgui_pixel.hlsl", "main", {}, nvrhi::ShaderType::Pixel) : nullptr;
        if (!m_CommandList || !vertex || !pixel) return false;
        const nvrhi::VertexAttributeDesc attributes[] = {
            {"POSITION", nvrhi::Format::RG32_FLOAT, 1, 0, offsetof(ImDrawVert, pos), sizeof(ImDrawVert), false},
            {"TEXCOORD", nvrhi::Format::RG32_FLOAT, 1, 0, offsetof(ImDrawVert, uv), sizeof(ImDrawVert), false},
            {"COLOR", nvrhi::Format::RGBA8_UNORM, 1, 0, offsetof(ImDrawVert, col), sizeof(ImDrawVert), false}
        };
        auto input = CanCreate() ? device->createInputLayout(attributes, 3, vertex) : nullptr;
        nvrhi::BindingLayoutDesc layout;
        layout.visibility = nvrhi::ShaderType::All;
        layout.bindings = { nvrhi::BindingLayoutItem::PushConstants(0, sizeof(RendererUiConstants)),
            nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(0) };
        m_BindingLayout = CanCreate() ? device->createBindingLayout(layout) : nullptr;
        m_Sampler = CanCreate() ? device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
            .setAllFilters(true)) : nullptr;
        if (!input || !m_BindingLayout || !m_Sampler || !Healthy(before)) return false;
        m_PipelineDesc = {};
        m_PipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
        m_PipelineDesc.inputLayout = input;
        m_PipelineDesc.VS = vertex;
        m_PipelineDesc.PS = pixel;
        m_PipelineDesc.bindingLayouts = {m_BindingLayout};
        m_PipelineDesc.renderState.blendState.targets[0].setBlendEnable(true)
            .setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
            .setSrcBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha).setDestBlendAlpha(nvrhi::BlendFactor::Zero);
        m_PipelineDesc.renderState.rasterState.setFillSolid().setCullNone().setScissorEnable(true).setDepthClipEnable(true);
        m_PipelineDesc.renderState.depthStencilState.disableDepthTest().enableDepthWrite().disableStencil()
            .setDepthFunc(nvrhi::ComparisonFunc::Always);
        m_Initialized = true;
        return true;
    }

    bool RendererUiNvrhi::UpdateFontTexture() noexcept
    {
        if (!m_Initialized || !ImGui::GetCurrentContext()) return false;
        auto& atlas = *ImGui::GetIO().Fonts;
        if (m_FontTexture && m_FontBinding && atlas.TexRef.GetTexID() == ImTextureRef(m_FontTexture.Get()).GetTexID())
            return true;
        const uint64_t before = ErrorCount();
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        atlas.GetTexDataAsRGBA32(&pixels, &width, &height);
        if (!pixels || width <= 0 || height <= 0 || uint32_t(width) > UINT32_MAX / 4 ||
            uint64_t(width) * uint64_t(height) > SIZE_MAX / 4)
            return false;
        nvrhi::TextureDesc texture;
        texture.width = uint32_t(width);
        texture.height = uint32_t(height);
        texture.format = nvrhi::Format::RGBA8_UNORM;
        texture.debugName = "ImGui font texture";
        auto candidate = CanCreate() ? m_Device->createTexture(texture) : nullptr;
        if (!candidate) return false;
        nvrhi::BindingSetDesc binding;
        binding.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(RendererUiConstants)),
            nvrhi::BindingSetItem::Texture_SRV(0, candidate), nvrhi::BindingSetItem::Sampler(0, m_Sampler)};
        auto candidateBinding = CanCreate() ? m_Device->createBindingSet(binding, m_BindingLayout) : nullptr;
        if (!candidateBinding || !Healthy(before) || !EnsureCommandList()) return false;
        m_CommandList->open();
        m_CommandList->beginTrackingTextureState(candidate, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
        m_CommandList->writeTexture(candidate, 0, 0, pixels, size_t(width) * 4);
        m_CommandList->setPermanentTextureState(candidate, nvrhi::ResourceStates::ShaderResource);
        m_CommandList->commitBarriers();
        m_CommandList->close();
        if (!Healthy(before) || !CanCreate() || m_Device->executeCommandList(m_CommandList) == 0 || !Healthy(before))
        {
            // a closed, unsubmitted immediate list cannot be reopened by NVRHI.
            m_CommandList = nullptr;
            return false;
        }
        // submitted commands retain the previous atlas until their GPU work retires.
        m_FontTexture = candidate;
        m_FontBinding = candidateBinding;
        atlas.TexRef = ImTextureRef(candidate.Get());
        return true;
    }

    bool RendererUiNvrhi::UpdateGeometry(const ImDrawData& data) noexcept
    {
        const size_t vertexCount = size_t(data.TotalVtxCount), indexCount = size_t(data.TotalIdxCount);
        if (!m_VertexBuffer || vertexCount > m_VertexCapacity)
        {
            const size_t capacity = vertexCount + 5000;
            if (capacity > UINT32_MAX / sizeof(ImDrawVert) || !CanAllocate()) return false;
            auto* vertices = new (std::nothrow) ImDrawVert[capacity]{};
            if (!vertices) return false;
            auto buffer = CreateGeometryBuffer(m_Device, capacity, sizeof(ImDrawVert), false);
            if (!buffer) { delete[] vertices; return false; }
            delete[] m_Vertices;
            m_Vertices = vertices;
            m_VertexBuffer = buffer;
            m_VertexCapacity = capacity;
        }
        if (!m_IndexBuffer || indexCount > m_IndexCapacity)
        {
            const size_t capacity = indexCount + 5000;
            if (capacity > UINT32_MAX / sizeof(ImDrawIdx) || !CanAllocate()) return false;
            auto* indices = new (std::nothrow) unsigned char[capacity * sizeof(ImDrawIdx)]{};
            if (!indices) return false;
            auto buffer = CreateGeometryBuffer(m_Device, capacity, sizeof(ImDrawIdx), true);
            if (!buffer) { delete[] indices; return false; }
            delete[] m_Indices;
            m_Indices = indices;
            m_IndexBuffer = buffer;
            m_IndexCapacity = capacity;
        }
        ImDrawVert* vertices = m_Vertices;
        unsigned char* indices = m_Indices;
        for (const ImDrawList* list : data.CmdLists)
        {
            if (list->VtxBuffer.Size) memcpy(vertices, list->VtxBuffer.Data, size_t(list->VtxBuffer.Size) * sizeof(ImDrawVert));
            if (list->IdxBuffer.Size) memcpy(indices, list->IdxBuffer.Data, size_t(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
            vertices += list->VtxBuffer.Size;
            indices += size_t(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        }
        m_CommandList->writeBuffer(m_VertexBuffer, m_Vertices, m_VertexCapacity * sizeof(ImDrawVert));
        m_CommandList->writeBuffer(m_IndexBuffer, m_Indices, m_IndexCapacity * sizeof(ImDrawIdx));
        return true;
    }

    nvrhi::IFramebuffer* RendererUiNvrhi::ResolveFramebuffer(nvrhi::IFramebuffer* source) noexcept
    {
        const auto& info = source->getFramebufferInfo();
        if (info.colorFormats.empty()) return nullptr;
        if (info.colorFormats[0] != nvrhi::Format::SRGBA8_UNORM) return source;
        for (auto* entry = m_Framebuffers; entry; entry = entry->next)
            if (entry->source == source) return entry->unorm;
        if (!CanAllocate()) return nullptr;
        auto* entry = new (std::nothrow) FramebufferEntry;
        if (!entry) return nullptr;
        auto desc = source->getDesc();
        // UI colors are display-encoded, so their render target view is unorm.
        desc.colorAttachments[0].format = nvrhi::Format::RGBA8_UNORM;
        entry->unorm = CanCreate() ? m_Device->createFramebuffer(desc) : nullptr;
        if (!entry->unorm) { delete entry; return nullptr; }
        entry->source = source;
        entry->next = m_Framebuffers;
        m_Framebuffers = entry;
        return entry->unorm;
    }

    bool RendererUiNvrhi::Render(nvrhi::IFramebuffer* framebuffer) noexcept
    {
        if (!m_Initialized || !framebuffer || !m_FontBinding || !ImGui::GetCurrentContext()) return false;
        const ImDrawData* data = ImGui::GetDrawData();
        const auto& io = ImGui::GetIO();
        if (!data || !Validate(*data, ImTextureRef(m_FontTexture.Get()).GetTexID()) ||
            !FinitePositive(io.DisplaySize.x) || !FinitePositive(io.DisplaySize.y) ||
            !FinitePositive(io.DisplayFramebufferScale.x) || !FinitePositive(io.DisplayFramebufferScale.y))
            return false;
        const float width = io.DisplaySize.x * io.DisplayFramebufferScale.x;
        const float height = io.DisplaySize.y * io.DisplayFramebufferScale.y;
        if (!FinitePositive(width) || !FinitePositive(height) || double(width) > INT_MAX || double(height) > INT_MAX)
            return false;
        if (!data->CmdListsCount) return true;
        const uint64_t before = ErrorCount();
        framebuffer = ResolveFramebuffer(framebuffer);
        if (!framebuffer || !Healthy(before)) return false;
        const auto& info = framebuffer->getFramebufferInfo();
        if (!m_Pipeline || m_PipelineInfo != info)
        {
            auto candidate = CanCreate() ? m_Device->createGraphicsPipeline(m_PipelineDesc, info) : nullptr;
            if (!candidate || !Healthy(before)) return false;
            m_Pipeline = candidate;
            m_PipelineInfo = info;
        }
        if (!EnsureCommandList()) return false;
        m_CommandList->open();
        if (!UpdateGeometry(*data) || !Healthy(before))
        {
            m_CommandList->close();
            m_CommandList = nullptr;
            return false;
        }
        m_CommandList->beginMarker("ImGUI");
        nvrhi::GraphicsState state;
        state.framebuffer = framebuffer;
        state.pipeline = m_Pipeline;
        state.bindings = {m_FontBinding};
        state.viewport.viewports.push_back(nvrhi::Viewport(width, height));
        state.viewport.scissorRects.resize(1);
        state.vertexBuffers = {{m_VertexBuffer, 0, 0}};
        state.indexBuffer = {m_IndexBuffer, sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT, 0};
        const RendererUiConstants constants = {{1.f / io.DisplaySize.x, 1.f / io.DisplaySize.y}};
        uint32_t vertexOffset = 0, indexOffset = 0;
        for (const ImDrawList* list : data->CmdLists)
        {
            for (const ImDrawCmd& command : list->CmdBuffer)
            {
                if (command.UserCallback)
                {
                    if (command.UserCallback != ImDrawCallback_ResetRenderState)
                        command.UserCallback(list, &command);
                    continue;
                }
                const auto clip = [](float value, float scale, float maximum) noexcept
                {
                    const float scaled = value * scale;
                    return int(scaled < 0.f ? 0.f : scaled > maximum ? maximum : scaled);
                };
                const int left = clip(command.ClipRect.x, io.DisplayFramebufferScale.x, width);
                const int right = clip(command.ClipRect.z, io.DisplayFramebufferScale.x, width);
                const int top = clip(command.ClipRect.y, io.DisplayFramebufferScale.y, height);
                const int bottom = clip(command.ClipRect.w, io.DisplayFramebufferScale.y, height);
                if (right <= left || bottom <= top || !command.ElemCount) continue;
                state.viewport.scissorRects[0] = nvrhi::Rect(left, right, top, bottom);
                nvrhi::DrawArguments arguments;
                arguments.vertexCount = command.ElemCount;
                arguments.startIndexLocation = indexOffset + command.IdxOffset;
                arguments.startVertexLocation = vertexOffset + command.VtxOffset;
                m_CommandList->setGraphicsState(state);
                m_CommandList->setPushConstants(&constants, sizeof(constants));
                m_CommandList->drawIndexed(arguments);
            }
            vertexOffset += uint32_t(list->VtxBuffer.Size);
            indexOffset += uint32_t(list->IdxBuffer.Size);
        }
        m_CommandList->endMarker();
        m_CommandList->close();
        const bool submitted = Healthy(before) && CanCreate() && m_Device->executeCommandList(m_CommandList) != 0 && Healthy(before);
        if (!submitted) m_CommandList = nullptr;
        return submitted;
    }

    void RendererUiNvrhi::BackBufferResizing() noexcept
    {
        m_Pipeline = nullptr;
        while (m_Framebuffers)
        {
            auto* next = m_Framebuffers->next;
            delete m_Framebuffers;
            m_Framebuffers = next;
        }
    }

#if defined(UVSR_BUILD_TESTING)
    void RendererUiNvrhi::FailAllocationAfter(size_t count) noexcept { allocationCountdown = count; }
    void RendererUiNvrhi::FailOperationAfter(size_t count) noexcept { operationCountdown = count; failureReached = false; }
    bool RendererUiNvrhi::FailureReached() noexcept { return failureReached; }
#endif
}
