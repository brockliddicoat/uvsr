#include "renderer_ui_context.h"
#include "renderer_ui_nvrhi.h"
#include "sha256.h"
#include "renderer_shader_factory_nvrhi.h"
#include "renderer_texture_bmp.h"
#include "renderer_ui_input_glfw.h"

#include "renderer_gpu_fixture.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <directx/d3d12.h>
#include <wrl/client.h>
#include <Windows.h>
#include <cmath>
#include <filesystem>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace uvsr
{
    struct RendererUiTestAccess
    {
        static void FailAllocation(size_t count) { RendererUiNvrhi::FailAllocationAfter(count); }
        static void FailFontAllocation(unsigned count) { RendererUiContext::FailAllocationAfter(count); }
        static void FailOperation(size_t count) { RendererUiNvrhi::FailOperationAfter(count); }
        static bool Reached() { return RendererUiNvrhi::FailureReached(); }
        static ImGuiContext* Context(RendererUiContext& owner) { return owner.m_Context; }
        static nvrhi::IResource* Atlas(RendererUiNvrhi& owner) { return owner.m_FontTexture.Get(); }
        static nvrhi::IResource* Binding(RendererUiNvrhi& owner) { return owner.m_FontBinding.Get(); }
        static nvrhi::IResource* Vertices(RendererUiNvrhi& owner) { return owner.m_VertexBuffer.Get(); }
        static nvrhi::IResource* Indices(RendererUiNvrhi& owner) { return owner.m_IndexBuffer.Get(); }
        static size_t VertexCapacity(RendererUiNvrhi& owner) { return owner.m_VertexCapacity; }
        static const unsigned char* FontData(RendererUiContext& owner, unsigned index)
            { return index < 2 ? owner.m_Fonts[index].data : nullptr; }
        static size_t FontSize(RendererUiContext& owner, unsigned index)
            { return index < 2 ? size_t(owner.m_Fonts[index].size) : 0; }
        static bool OversizedGeometry(RendererUiNvrhi& owner, bool indices)
        {
            ImDrawData data;
            data.TotalVtxCount = indices ? 0 : INT_MAX;
            data.TotalIdxCount = indices ? INT_MAX : 0;
            return !owner.UpdateGeometry(data);
        }
        static bool RejectFont(const wchar_t* path)
        {
            RendererUiContext::Font font;
            const bool result = RendererUiContext::LoadFont(path, font);
            delete[] font.data;
            return !result;
        }
    };
}

namespace
{
    using namespace uvsr;
    using Access = RendererUiTestAccess;

    void Require(bool value, const char* message)
    {
        if (value) return;
        fprintf(stderr, "UI renderer test failed: %s\n", message);
        exit(EXIT_FAILURE);
    }

    struct Bytes
    {
        unsigned char* data = nullptr;
        size_t size = 0;
        ~Bytes() { delete[] data; }
        Bytes() = default;
        Bytes(const Bytes&) = delete;
        Bytes& operator=(const Bytes&) = delete;
        void Allocate(size_t count)
        {
            Require(!data && count > 0, "byte storage state");
            data = new (std::nothrow) unsigned char[count];
            Require(data != nullptr, "byte storage allocation");
            size = count;
        }
    };

    uint64_t Hash(uint64_t value, const void* data, size_t count)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < count; ++i) value = (value ^ bytes[i]) * 1099511628211ull;
        return value;
    }

    uint64_t DrawHash()
    {
        uint64_t value = 14695981039346656037ull;
        const auto& data = *ImGui::GetDrawData();
        value = Hash(value, &data.TotalVtxCount, sizeof(data.TotalVtxCount));
        value = Hash(value, &data.TotalIdxCount, sizeof(data.TotalIdxCount));
        for (const auto* list : data.CmdLists)
        {
            value = Hash(value, list->VtxBuffer.Data, size_t(list->VtxBuffer.Size) * sizeof(ImDrawVert));
            value = Hash(value, list->IdxBuffer.Data, size_t(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
            for (const auto& command : list->CmdBuffer)
            {
                value = Hash(value, &command.ClipRect, sizeof(command.ClipRect));
                value = Hash(value, &command.ElemCount, sizeof(command.ElemCount));
                value = Hash(value, &command.IdxOffset, sizeof(command.IdxOffset));
                value = Hash(value, &command.VtxOffset, sizeof(command.VtxOffset));
            }
        }
        return value;
    }

    bool CapturedFontFiles(RendererUiContext& context)
    {
        constexpr const char* captured[] = {
            "2d9b22d71f72de2823fee5d9c8bc1b0fc32b2577c4c27b9ec6abdbb8df0e1731",
            "aeb9e4a6ec5cc59f4d72df8189032d7dbb28f45161cf1552174818b5465dac4e"};
        bool exact = true;
        for (unsigned index = 0; index < 2; ++index)
        {
            Sha256Digest digest;
            Sha256Result result;
            Require(Sha256(Access::FontData(context, index), Access::FontSize(context, index), digest, result),
                "Windows UI font hash");
            printf("UI font %u SHA-256: %s%s\n", index, digest.text,
                strcmp(digest.text, captured[index]) ? " (not captured revision)" : " (captured revision)");
            exact &= strcmp(digest.text, captured[index]) == 0;
        }
        return exact;
    }

    void Atlas(Bytes& output, int& width, int& height)
    {
        unsigned char* pixels = nullptr;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Require(pixels && width > 0 && height > 0, "atlas pixels");
        output.Allocate(size_t(width) * size_t(height) * 4);
        memcpy(output.data, pixels, output.size);
    }

    void DrawUi(ImFont* body, ImFont* header, float scale)
    {
        ImGui::SetNextWindowPos(ImVec2(12.f * scale, 14.f * scale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(286.f * scale, 212.f * scale), ImGuiCond_Always);
        ImGui::PushFont(body);
        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
        ImGui::PushFont(header);
        ImGui::TextUnformatted("Lighting and postprocess");
        ImGui::PopFont();
        bool enabled = true;
        float exposure = 0.75f;
        ImGui::Checkbox("Vertical Sync", &enabled);
        ImGui::SliderFloat("Exposure", &exposure, 0.f, 2.f);
        ImGui::TextUnformatted("Segoe UI: Aa Bb 0123456789");
        ImGui::TextUnformatted("Symbols: + - / ( ) [ ] % # @");
        ImGui::Button("Reset");
        ImGui::SameLine();
        ImGui::Button("Copy snapshot");
        ImGui::BeginChild("clipped", ImVec2(150.f * scale, 36.f * scale), true);
        ImGui::TextUnformatted("clipped text across the right boundary");
        ImGui::TextUnformatted("second clipped line");
        ImGui::EndChild();
        ImGui::End();
        ImGui::PopFont();
        auto* draw = ImGui::GetForegroundDrawList();
        draw->AddCircleFilled(ImVec2(340.5f * scale, 120.5f * scale), 17.f * scale,
            IM_COL32(238, 156, 41, 173), 20);
        draw->AddTriangleFilled(ImVec2(325.f * scale, 156.f * scale), ImVec2(374.f * scale, 204.f * scale),
            ImVec2(314.f * scale, 205.f * scale), IM_COL32(26, 148, 224, 192));
        draw->AddText(ImGui::GetIO().Fonts->Fonts[0], 13.f * scale,
            ImVec2(18.f * scale, 254.f * scale), IM_COL32_WHITE, "default font, alpha, clipping, and geometry");
    }

    struct Target
    {
        nvrhi::TextureHandle texture;
        nvrhi::FramebufferHandle framebuffer;
        uint32_t width = 0, height = 0, bytesPerPixel = 0;
        Target(nvrhi::IDevice* device, uint32_t w, uint32_t h, nvrhi::Format format) : width(w), height(h),
            bytesPerPixel(format == nvrhi::Format::RGBA16_FLOAT ? 8u : 4u)
        {
            nvrhi::TextureDesc desc;
            desc.width = width; desc.height = height;
            desc.format = format;
            desc.isRenderTarget = true;
            desc.initialState = nvrhi::ResourceStates::RenderTarget;
            desc.keepInitialState = true;
            texture = device->createTexture(desc);
            Require(bool(texture), "target texture");
            auto framebufferDesc = nvrhi::FramebufferDesc().addColorAttachment(texture);
            framebuffer = device->createFramebuffer(framebufferDesc);
            Require(framebuffer, "target framebuffer");
        }
        void Clear(nvrhi::IDevice* device)
        {
            auto command = device->createCommandList();
            Require(bool(command), "clear command");
            command->open();
            command->clearTextureFloat(texture, nvrhi::AllSubresources, nvrhi::Color(0.0625f, 0.125f, 0.25f, 1.f));
            command->close();
            Require(device->executeCommandList(command) != 0, "clear submission");
        }
        void Read(nvrhi::IDevice* device, Bytes& output)
        {
            auto staging = device->createStagingTexture(texture->getDesc(), nvrhi::CpuAccessMode::Read);
            auto command = device->createCommandList();
            Require(staging && command, "readback resources");
            command->open();
            command->copyTexture(staging, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());
            command->close();
            Require(device->executeCommandList(command) != 0 && device->waitForIdle(), "readback completion");
            size_t pitch = 0;
            const auto* mapped = static_cast<const unsigned char*>(device->mapStagingTexture(
                staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
            Require(mapped && pitch >= size_t(width) * bytesPerPixel, "readback mapping");
            output.Allocate(size_t(width) * height * bytesPerPixel);
            for (uint32_t y = 0; y < height; ++y)
                memcpy(output.data + size_t(y) * width * bytesPerPixel, mapped + size_t(y) * pitch, size_t(width) * bytesPerPixel);
            device->unmapStagingTexture(staging);
        }
    };

    void InvalidDraws(RendererUiNvrhi& gpu, nvrhi::IFramebuffer* framebuffer)
    {
        auto& data = *ImGui::GetDrawData();
        Require(data.CmdListsCount > 0 && data.CmdLists[0]->CmdBuffer.Size > 0, "negative fixture draw data");
        auto& command = data.CmdLists[0]->CmdBuffer[0];
        auto& io = ImGui::GetIO();
        const auto rejected = [&]()
        {
            Access::FailOperation(0);
            Require(!gpu.Render(framebuffer) && !Access::Reached(), "invalid draw recorded GPU work");
            Access::FailOperation(SIZE_MAX);
        };
        const int vertices = data.TotalVtxCount; data.TotalVtxCount = -1; rejected(); data.TotalVtxCount = vertices;
        const int indices = data.TotalIdxCount; ++data.TotalIdxCount; rejected(); data.TotalIdxCount = indices;
        const int lists = data.CmdListsCount; --data.CmdListsCount; rejected(); data.CmdListsCount = lists;
        const ImVec2 position = data.DisplayPos; data.DisplayPos.x = 1.f; rejected(); data.DisplayPos = position;
        const float oldScale = io.DisplayFramebufferScale.x; io.DisplayFramebufferScale.x = 0.f; rejected();
        io.DisplayFramebufferScale.x = INFINITY; rejected(); io.DisplayFramebufferScale.x = oldScale;
        const ImVec4 clip = command.ClipRect; command.ClipRect.x = NAN; rejected(); command.ClipRect = clip;
        const ImTextureRef texture = command.TexRef; command.TexRef = ImTextureRef(reinterpret_cast<void*>(uintptr_t(1))); rejected(); command.TexRef = texture;
        const unsigned int index = command.IdxOffset; command.IdxOffset = UINT32_MAX; rejected(); command.IdxOffset = index;
        const unsigned int vertex = command.VtxOffset; command.VtxOffset = UINT32_MAX; rejected(); command.VtxOffset = vertex;
        const unsigned int count = command.ElemCount; command.ElemCount = UINT32_MAX; rejected(); command.ElemCount = count;
        Require(!gpu.Render(nullptr), "null target accepted");
        Access::FailOperation(0);
        data.CmdListsCount = data.CmdLists.Size = data.TotalVtxCount = data.TotalIdxCount = 0;
        Require(gpu.Render(framebuffer) && !Access::Reached(), "empty UI performed GPU work");
        data.CmdListsCount = data.CmdLists.Size = lists; data.TotalVtxCount = vertices; data.TotalIdxCount = indices;
        Access::FailOperation(SIZE_MAX);
    }
}

void TestRendererUi(nvrhi::IDevice* device, ID3D12Device* nativeDevice,
    ID3D12CommandQueue* nativeQueue, const std::filesystem::path& shaderDirectory)
{
    using namespace uvsr;
    using Access = RendererUiTestAccess;
    RendererGpuReference reference("renderer_ui_gpu_fixture.bin");
    RendererShaderFactory shaders(device, shaderDirectory.c_str());
    std::error_code error;
    wchar_t previewName[96]{};
    swprintf_s(previewName, L"ui-renderer-reference/run-%lu", GetCurrentProcessId());
    const std::filesystem::path previews(previewName);
    std::filesystem::create_directories(previews, error);
    Require(!error, "preview directory");
    printf("UI previews: %ls\n", std::filesystem::absolute(previews).c_str());
    Require(Access::RejectFont(L"missing-ui-font.ttf"), "missing font accepted");
    for (unsigned failure = 0; failure < 3; ++failure)
    {
        RendererUiContext context;
        Access::FailFontAllocation(failure);
        Require(!context.LoadWindowsFonts() && !Access::Context(context), "font allocation failure published a context");
        Access::FailFontAllocation(UINT_MAX);
        Require(context.LoadWindowsFonts(), "font allocation retry failed");
    }
    bool capturedFontFiles = false;
    {
        RendererUiContext context;
        Require(context.LoadWindowsFonts(), "font identity fixture");
        capturedFontFiles = CapturedFontFiles(context);
    }
    struct Case { int width, height; float scale; bool explicitScaling; nvrhi::Format format; };
    const Case cases[] = {
        {640,480,1.f,true,nvrhi::Format::RGBA8_UNORM}, {640,480,1.25f,true,nvrhi::Format::RGBA8_UNORM},
        {640,480,1.5f,true,nvrhi::Format::RGBA8_UNORM}, {640,480,2.f,true,nvrhi::Format::RGBA8_UNORM},
        {1280,720,1.f,true,nvrhi::Format::RGBA8_UNORM}, {1280,720,1.25f,true,nvrhi::Format::RGBA8_UNORM},
        {1280,720,1.5f,true,nvrhi::Format::RGBA8_UNORM}, {1280,720,2.f,true,nvrhi::Format::RGBA8_UNORM},
        {640,480,1.25f,false,nvrhi::Format::RGBA8_UNORM}, {640,480,2.f,false,nvrhi::Format::RGBA8_UNORM},
        {640,480,1.5f,true,nvrhi::Format::SRGBA8_UNORM}, {640,480,1.5f,true,nvrhi::Format::RGBA16_FLOAT}
    };
    unsigned caseIndex = 0;
    for (const auto& test : cases)
    {
        RendererUiContext context;
        Require(context.LoadWindowsFonts(), "owned Segoe UI data");
        context.DisplayScaleChanged(test.scale, test.explicitScaling);
        const float fontScale = test.explicitScaling ? test.scale : 1.f;
        Require(context.EnsureFonts(fontScale), "owned fonts");
        int candidateAtlasWidth = 0, candidateAtlasHeight = 0;
        Bytes candidateAtlas; Atlas(candidateAtlas, candidateAtlasWidth, candidateAtlasHeight);
        RendererUiNvrhi gpu;
        Require(gpu.Init(device, shaders, nullptr) && gpu.UpdateFontTexture(), "owned UI GPU initialization");
        const uint32_t width = uint32_t(float(test.width) * (test.explicitScaling ? 1.f : test.scale));
        const uint32_t height = uint32_t(float(test.height) * (test.explicitScaling ? 1.f : test.scale));
        Target target(device, width, height, test.format);
        target.Clear(device);
        for (int frame = 0; frame < 2; ++frame)
        {
            Require(context.BeginFrame(test.width, test.height, test.scale, test.scale, 1.f / 60.f, test.explicitScaling), "owned UI frame");
            DrawUi(context.BodyFont(), context.HeaderFont(), fontScale);
            context.Render();
        }
        const uint64_t candidateDrawHash = DrawHash();
        if (caseIndex == 0) InvalidDraws(gpu, target.framebuffer);
        Require(gpu.Render(target.framebuffer), "owned UI draw");
        Bytes candidate; target.Read(device, candidate);

        uint32_t expectedKey[8]; reference.Read(expectedKey, sizeof(expectedKey));
        uint32_t scaleBits; memcpy(&scaleBits, &test.scale, sizeof(scaleBits));
        const uint32_t key[]{caseIndex, uint32_t(test.width), uint32_t(test.height), scaleBits, uint32_t(test.explicitScaling), uint32_t(test.format)};
        Require(!memcmp(key, expectedKey, sizeof(key)), "original UI case inputs");
        const uint32_t referenceAtlasWidth = expectedKey[6], referenceAtlasHeight = expectedKey[7];
        Bytes referenceAtlas; referenceAtlas.Allocate(size_t(referenceAtlasWidth) * referenceAtlasHeight * 4);
        reference.Read(referenceAtlas.data, referenceAtlas.size);
        if (capturedFontFiles)
        {
            Require(referenceAtlasWidth == uint32_t(candidateAtlasWidth) && referenceAtlasHeight == uint32_t(candidateAtlasHeight),
                "font atlas dimensions match captured RegisteredFont");
            Require(referenceAtlas.size == candidateAtlas.size &&
                memcmp(referenceAtlas.data, candidateAtlas.data, candidateAtlas.size) == 0,
                "font atlas differs from captured RegisteredFont");
        }
        uint64_t referenceDrawHash; reference.Read(&referenceDrawHash, sizeof(referenceDrawHash));
        if (capturedFontFiles)
            Require(referenceDrawHash == candidateDrawHash, "owned UI draw data differs from captured font registration");
        Bytes control; control.Allocate(candidate.size); reference.Read(control.data, control.size);
        if (target.bytesPerPixel == 4)
        {
            wchar_t candidateName[96]{}, controlName[96]{};
            swprintf_s(candidateName, L"candidate-%02u.bmp", caseIndex);
            swprintf_s(controlName, L"control-%02u.bmp", caseIndex);
            const auto candidatePath = previews / candidateName;
            const auto controlPath = previews / controlName;
            Require(WriteRendererBmp(candidatePath.c_str(), width, height, size_t(width) * 4, candidate.data) &&
                WriteRendererBmp(controlPath.c_str(), width, height, size_t(width) * 4, control.data), "UI preview capture");
        }
        target.Clear(device);
        Require(context.BeginFrame(test.width, test.height, test.scale, test.scale, 1.f / 60.f, test.explicitScaling), "repeat owned UI frame");
        DrawUi(context.BodyFont(), context.HeaderFont(), fontScale); context.Render();
        Require(gpu.Render(target.framebuffer), "repeat owned UI draw");
        Bytes repeated; target.Read(device, repeated);
        Require(repeated.size == candidate.size && memcmp(repeated.data, candidate.data, candidate.size) == 0,
            "owned UI pixels are not repeatable");
        if (capturedFontFiles)
            Require(control.size == candidate.size && memcmp(control.data, candidate.data, control.size) == 0,
                "owned UI pixels differ from captured GPU rendering");
        printf("UI case %u: %dx%d scale %.2f explicit %d format %u, atlas %dx%d, draw %016llx, pixels %016llx %s\n",
            caseIndex, test.width, test.height, test.scale, int(test.explicitScaling), unsigned(test.format),
            candidateAtlasWidth, candidateAtlasHeight, static_cast<unsigned long long>(candidateDrawHash),
            static_cast<unsigned long long>(Hash(14695981039346656037ull, candidate.data, candidate.size)),
            capturedFontFiles ? "exact captured reference" : "exact repeat with different Windows font revision");
        if (caseIndex == 0)
        {
            Require(context.BeginFrame(test.width, test.height, 1,1,1.f/60.f,true), "unfocused frame");
            context.DisplayScaleChanged(1.5f, true);
            Require(!context.FrameOpened() && !context.BodyFont() && !context.HeaderFont(), "DPI change retained a locked frame or stale fonts");
            Require(context.EnsureFonts(1.5f) && gpu.UpdateFontTexture(), "DPI atlas replacement");
            Require(context.BeginFrame(test.width, test.height, 1.5f,1.5f,1.f/60.f,true), "DPI replacement frame");
            DrawUi(context.BodyFont(), context.HeaderFont(), 1.5f); context.Render();
            Require(gpu.Render(target.framebuffer), "DPI replacement draw");
            Require(device->waitForIdle(), "DPI replacement completion");
        }
        ++caseIndex;
    }

    reference.Finish(48);
    RendererUiContext context;
    Require(context.LoadWindowsFonts() && context.EnsureFonts(1.f), "failure fixture fonts");
    for (size_t failure = 0; failure < 6; ++failure)
    {
        RendererUiNvrhi gpu;
        Access::FailOperation(failure);
        Require(!gpu.Init(device, shaders, nullptr) && Access::Reached(), "UI initialization failure did not reject");
        Access::FailOperation(SIZE_MAX);
        Require(gpu.Init(device, shaders, nullptr), "UI initialization retry failed");
    }
    for (size_t failure = 0; failure < 3; ++failure)
    {
        RendererUiNvrhi gpu;
        Require(gpu.Init(device, shaders, nullptr), "font failure fixture initialization");
        Access::FailOperation(failure);
        Require(!gpu.UpdateFontTexture() && Access::Reached() && !ImGui::GetIO().Fonts->TexRef.GetTexID(),
            "font failure published a partial atlas");
        Access::FailOperation(SIZE_MAX);
        Require(gpu.UpdateFontTexture(), "font retry failed");
    }
    Target target(device, 640, 480, nvrhi::Format::SRGBA8_UNORM);
    for (size_t failure = 0; failure < 5; ++failure)
    {
        RendererUiNvrhi gpu;
        Require(gpu.Init(device, shaders, nullptr) && gpu.UpdateFontTexture(), "draw failure fixture initialization");
        Require(context.BeginFrame(640,480,1,1,1.f/60.f,true), "draw failure frame");
        DrawUi(context.BodyFont(), context.HeaderFont(), 1.f); context.Render();
        Access::FailOperation(failure);
        Require(!gpu.Render(target.framebuffer) && Access::Reached(), "draw operation failure did not reject");
        Access::FailOperation(SIZE_MAX);
        Require(gpu.Render(target.framebuffer), "draw operation retry failed");
    }
    for (size_t failure = 0; failure < 3; ++failure)
    {
        RendererUiNvrhi gpu;
        Require(gpu.Init(device, shaders, nullptr) && gpu.UpdateFontTexture(), "allocation fixture initialization");
        Require(context.BeginFrame(640,480,1,1,1.f/60.f,true), "allocation failure frame");
        DrawUi(context.BodyFont(), context.HeaderFont(), 1.f); context.Render();
        Access::FailAllocation(failure);
        Require(!gpu.Render(target.framebuffer), "draw allocation failure did not reject");
        Access::FailAllocation(SIZE_MAX);
        Require(gpu.Render(target.framebuffer), "draw allocation retry failed");
    }
    Require(device->waitForIdle(), "UI failure recovery completion");
    device->runGarbageCollection();
    {
        RendererUiNvrhi gpu;
        Require(gpu.Init(device, shaders, nullptr) && gpu.UpdateFontTexture(), "capacity fixture initialization");
        Require(Access::OversizedGeometry(gpu, false) && Access::OversizedGeometry(gpu, true), "oversized geometry accepted");
        Require(context.BeginFrame(640,480,1,1,1.f/60.f,true), "capacity fixture frame");
        DrawUi(context.BodyFont(), context.HeaderFont(), 1.f); context.Render();
        Require(gpu.Render(target.framebuffer), "capacity fixture draw");
        const size_t oldCapacity = Access::VertexCapacity(gpu);
        Require(context.BeginFrame(640,480,1,1,1.f/60.f,true), "geometry growth frame");
        DrawUi(context.BodyFont(), context.HeaderFont(), 1.f);
        auto* draw = ImGui::GetForegroundDrawList();
        for (unsigned i = 0; i < 2400; ++i)
        {
            const float x = 400.f + float(i % 100), y = 20.f + float(i / 100);
            draw->AddRectFilled(ImVec2(x,y), ImVec2(x+1,y+1), IM_COL32_WHITE);
        }
        context.Render();
        Require(gpu.Render(target.framebuffer) && Access::VertexCapacity(gpu) > oldCapacity, "geometry growth failed");
        Target alternate(device, 640,480,nvrhi::Format::RGBA16_FLOAT);
        Require(gpu.Render(alternate.framebuffer), "framebuffer format change retained an incompatible pipeline");
        Require(device->waitForIdle(), "geometry growth completion");
    }
    device->runGarbageCollection();
    {
        Microsoft::WRL::ComPtr<ID3D12Fence> gate;
        Require(SUCCEEDED(nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))), "UI lifetime gate");
        Require(SUCCEEDED(nativeQueue->Wait(gate.Get(), 1)), "UI lifetime queue wait");
        nvrhi::IResource* references[4]{};
        const bool held = [&]()
        {
            RendererUiNvrhi gpu;
            if (!gpu.Init(device, shaders, nullptr) || !gpu.UpdateFontTexture() ||
                !context.BeginFrame(640,480,1,1,1.f/60.f,true)) return false;
            DrawUi(context.BodyFont(), context.HeaderFont(), 1.f); context.Render();
            if (!gpu.Render(target.framebuffer)) return false;
            references[0] = Access::Binding(gpu); references[1] = Access::Atlas(gpu);
            references[2] = Access::Vertices(gpu); references[3] = Access::Indices(gpu);
            for (auto* resource : references) resource->AddRef();
            gpu.BackBufferResizing();
            return gate->GetCompletedValue() == 0;
        }();
        bool retained = held;
        for (auto* resource : references) retained = retained && resource && resource->GetRefCount() > 1;
        const HRESULT released = gate->Signal(1);
        Require(SUCCEEDED(released) && device->waitForIdle(), "UI lifetime release");
        device->runGarbageCollection();
        bool retired = true;
        for (auto* resource : references)
        {
            if (!resource) { retired = false; continue; }
            retired = retired && resource->GetRefCount() == 1;
            const unsigned remaining = resource->Release();
            retired = retired && remaining == 0;
        }
        Require(retained && retired, "UI resources did not survive pending GPU work and retire at completion");
    }
    {
        auto& io = ImGui::GetIO();
        io.ClearEventsQueue();
        io.WantCaptureKeyboard = io.WantCaptureMouse = true;
        Require(RendererUiKeyboard(nullptr, GLFW_KEY_F8, GLFW_PRESS) && RendererUiCharacter('x') &&
            RendererUiMousePosition(123.5, 71.25) && RendererUiMouseScroll(1, -2) &&
            RendererUiMouseButton(nullptr, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS), "input capture forwarding");
        bool key = false, text = false, position = false, wheel = false, button = false;
        for (const auto& event : ImGui::GetCurrentContext()->InputEventsQueue)
        {
            key |= event.Type == ImGuiInputEventType_Key && event.Key.Key == ImGuiKey_F8 && event.Key.Down;
            text |= event.Type == ImGuiInputEventType_Text && event.Text.Char == 'x';
            // ImGui floors mouse positions when it queues the forwarded event.
            position |= event.Type == ImGuiInputEventType_MousePos && event.MousePos.PosX == 123.f && event.MousePos.PosY == 71.f;
            wheel |= event.Type == ImGuiInputEventType_MouseWheel && event.MouseWheel.WheelX == 1.f && event.MouseWheel.WheelY == -2.f;
            button |= event.Type == ImGuiInputEventType_MouseButton && event.MouseButton.Button == 0 && event.MouseButton.Down;
        }
        Require(key && text && position && wheel && button, "input event translation");
        io.ClearEventsQueue();
    }
    printf("UI ownership: 12 image comparisons, 12 invalid draws, empty UI, DPI replacement, 14 GPU failures, 6 allocation retries, capacity growth, format change, 4 pending GPU retirements and input forwarding passed\n");
}
