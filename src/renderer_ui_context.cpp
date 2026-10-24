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

#include "renderer_ui_context.h"

#include <imgui.h>
#include <Windows.h>
#include <cmath>
#include <limits.h>
#include <new>
#include <wchar.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_BUILD_TESTING)
        unsigned allocationCountdown = UINT_MAX;
#endif
        bool CanAllocate() noexcept
        {
#if defined(UVSR_BUILD_TESTING)
            if (allocationCountdown == 0) return false;
            if (allocationCountdown != UINT_MAX) --allocationCountdown;
#endif
            return true;
        }
    }

    RendererUiContext::~RendererUiContext() noexcept
    {
        if (m_Context)
        {
            CloseFrame();
            ImGui::DestroyContext(m_Context);
        }
        for (auto& font : m_Fonts) delete[] font.data;
    }

    bool RendererUiContext::LoadFont(const wchar_t* path, Font& font) noexcept
    {
        const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER size{};
        bool valid = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= INT_MAX;
        auto* data = valid && CanAllocate() ? new (std::nothrow) unsigned char[size_t(size.QuadPart)] : nullptr;
        valid = valid && data;
        size_t offset = 0;
        while (valid && offset < size_t(size.QuadPart))
        {
            const size_t remaining = size_t(size.QuadPart) - offset;
            const DWORD requested = DWORD(remaining > 1024 * 1024 ? 1024 * 1024 : remaining);
            DWORD read = 0;
            valid = ReadFile(file, data + offset, requested, &read, nullptr) && read == requested;
            offset += read;
        }
        valid = CloseHandle(file) && valid;
        if (!valid) { delete[] data; return false; }
        font.data = data;
        font.size = int(size.QuadPart);
        return true;
    }

    bool RendererUiContext::LoadWindowsFonts() noexcept
    {
        if (m_Context) return false;
        constexpr UINT capacity = 32768;
        auto* path = CanAllocate() ? new (std::nothrow) wchar_t[capacity] : nullptr;
        if (!path) return false;
        const UINT count = GetWindowsDirectoryW(path, capacity);
        const wchar_t* const names[] = {L"\\Fonts\\seguisb.ttf", L"\\Fonts\\segoeuib.ttf"};
        Font candidates[2];
        bool valid = count > 0 && count < capacity;
        for (unsigned i = 0; valid && i < 2; ++i)
        {
            const size_t suffix = wcslen(names[i]);
            valid = suffix < capacity - count;
            if (valid)
            {
                wmemcpy(path + count, names[i], suffix + 1);
                valid = LoadFont(path, candidates[i]);
            }
        }
        delete[] path;
        if (valid)
        {
            IMGUI_CHECKVERSION();
            m_Context = ImGui::CreateContext();
            valid = m_Context != nullptr;
        }
        if (!valid)
        {
            for (auto& font : candidates) delete[] font.data;
            return false;
        }
        m_Fonts[0] = candidates[0];
        m_Fonts[1] = candidates[1];
        ImGui::SetCurrentContext(m_Context);
        ImGui::GetIO().IniFilename = nullptr;
        return true;
    }

    bool RendererUiContext::EnsureFonts(float scale) noexcept
    {
        if (!m_Context || m_FrameOpened || !std::isfinite(scale) || scale <= 0.f || !std::isfinite(16.f * scale)) return false;
        ImGui::SetCurrentContext(m_Context);
        auto& atlas = *ImGui::GetIO().Fonts;
        if (!m_DefaultFont)
        {
            ImFontConfig config;
            config.SizePixels = 13.f * scale;
            m_DefaultFont = atlas.AddFontDefault(&config);
            atlas.TexRef = ImTextureRef();
            if (!m_DefaultFont) return false;
        }
        for (auto& font : m_Fonts)
        {
            if (font.scaled) continue;
            ImFontConfig config;
            config.SizePixels = 16.f * scale;
            config.FontDataOwnedByAtlas = false;
            font.scaled = atlas.AddFontFromMemoryTTF(font.data, font.size, 0.f, &config);
            atlas.TexRef = ImTextureRef();
            if (!font.scaled) return false;
        }
        return true;
    }

    bool RendererUiContext::BeginFrame(int width, int height, float scaleX, float scaleY,
        float elapsedSeconds, bool explicitScaling) noexcept
    {
        if (!m_Context || m_FrameOpened || width <= 0 || height <= 0 ||
            !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0.f || scaleY <= 0.f ||
            !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.f)
            return false;
        ImGui::SetCurrentContext(m_Context);
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(float(width), float(height));
        io.DisplayFramebufferScale = explicitScaling ? ImVec2(1.f, 1.f) : ImVec2(scaleX, scaleY);
        io.DeltaTime = elapsedSeconds;
        io.MouseDrawCursor = false;
        ImGui::NewFrame();
        m_FrameOpened = true;
        return true;
    }

    void RendererUiContext::CloseFrame() noexcept
    {
        if (!m_FrameOpened) return;
        ImGui::SetCurrentContext(m_Context);
        ImGui::EndFrame();
        m_FrameOpened = false;
    }

    void RendererUiContext::Render() noexcept
    {
        if (!m_FrameOpened) return;
        ImGui::SetCurrentContext(m_Context);
        ImGui::Render();
        m_FrameOpened = false;
    }

    void RendererUiContext::DisplayScaleChanged(float scale, bool explicitScaling) noexcept
    {
        if (!m_Context || !explicitScaling || !std::isfinite(scale) || scale <= 0.f) return;
        CloseFrame();
        ImGui::SetCurrentContext(m_Context);
        auto& io = ImGui::GetIO();
        io.Fonts->Clear();
        io.Fonts->TexRef = ImTextureRef();
        m_DefaultFont = nullptr;
        for (auto& font : m_Fonts) font.scaled = nullptr;
        ImGui::GetStyle() = ImGuiStyle();
        ImGui::GetStyle().ScaleAllSizes(scale);
    }

    void RendererUiContext::BeginFullScreenWindow() noexcept
    {
        const auto& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x / io.DisplayFramebufferScale.x,
            io.DisplaySize.y / io.DisplayFramebufferScale.y), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::SetNextWindowBgAlpha(0.f);
        ImGui::Begin(" ", nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    }

    void RendererUiContext::DrawScreenCenteredText(const char* text) noexcept
    {
        const auto& io = ImGui::GetIO();
        const ImVec2 size = ImGui::CalcTextSize(text);
        ImGui::SetCursorPosX((io.DisplaySize.x / io.DisplayFramebufferScale.x - size.x) * 0.5f);
        ImGui::SetCursorPosY((io.DisplaySize.y / io.DisplayFramebufferScale.y - size.y) * 0.5f);
        ImGui::TextUnformatted(text);
    }

    void RendererUiContext::EndFullScreenWindow() noexcept
    {
        ImGui::End();
        ImGui::PopStyleVar();
    }

#if defined(UVSR_BUILD_TESTING)
    void RendererUiContext::FailAllocationAfter(unsigned count) noexcept { allocationCountdown = count; }
#endif
}
