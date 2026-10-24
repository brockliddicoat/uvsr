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

#include "renderer_shader_factory_nvrhi.h"

#include <Windows.h>
#include <limits.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_BUILD_TESTING)
        size_t allocationCountdown = SIZE_MAX;
#endif
        unsigned char* Allocate(size_t bytes) noexcept
        {
            if (!bytes || bytes > size_t(PTRDIFF_MAX)) return nullptr;
#if defined(UVSR_BUILD_TESTING)
            if (allocationCountdown != SIZE_MAX)
            {
                if (!allocationCountdown) return nullptr;
                --allocationCountdown;
            }
#endif
            return new (std::nothrow) unsigned char[bytes];
        }

        struct File
        {
            HANDLE handle = INVALID_HANDLE_VALUE;
            ~File() noexcept { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        };

        struct BlobName
        {
            const char* stem = nullptr;
            const char* entry = nullptr;
            size_t stemSize = 0;
            size_t entrySize = 0;
            size_t size = 0;

            bool Parse(const char* fileName, const char* entryName) noexcept
            {
                if (!fileName) return false;
                const size_t length = strlen(fileName);
                if (length < 11 || memcmp(fileName, "uvsr/", 5) ||
                    memcmp(fileName + length - 5, ".hlsl", 5)) return false;
                stem = fileName + 5; stemSize = length - 10;
                for (size_t i = 0; i < stemSize; ++i)
                    if (stem[i] == '/' || stem[i] == '\\' || stem[i] == ':') return false;
                entry = entryName ? entryName : "main";
                entrySize = strlen(entry);
                if (!entrySize) return false;
                for (size_t i = 0; i < entrySize; ++i)
                {
                    const char ch = entry[i];
                    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' ||
                        (i && ch >= '0' && ch <= '9'))) return false;
                }
                if (strcmp(entry, "main") == 0) entrySize = 0;
                if (stemSize > size_t(PTRDIFF_MAX) - 6 || entrySize > size_t(PTRDIFF_MAX) - stemSize - 6)
                    return false;
                size = stemSize + (entrySize ? entrySize + 1 : 0) + 4;
                return size <= INT_MAX;
            }
            bool Matches(const char* name, size_t length) const noexcept
            {
                if (length != size || memcmp(stem, name, stemSize)) return false;
                const size_t offset = stemSize + (entrySize ? entrySize + 1 : 0);
                return (!entrySize || (name[stemSize] == '_' && !memcmp(entry, name + stemSize + 1, entrySize))) &&
                    !memcmp(name + offset, ".bin", 4);
            }
            void Write(char* name) const noexcept
            {
                memcpy(name, stem, stemSize);
                size_t offset = stemSize;
                if (entrySize)
                {
                    name[offset++] = '_'; memcpy(name + offset, entry, entrySize); offset += entrySize;
                }
                memcpy(name + offset, ".bin", 5);
            }
        };
    }

    struct RendererShaderFactory::CachedBlob
    {
        CachedBlob* next = nullptr;
        size_t nameSize = 0;
        size_t byteOffset = 0;
        size_t byteCount = 0;
        char* Name() noexcept { return reinterpret_cast<char*>(this + 1); }
        unsigned char* Bytes() noexcept { return reinterpret_cast<unsigned char*>(this) + byteOffset; }
        void Destroy() noexcept
        {
            auto* allocation = reinterpret_cast<unsigned char*>(this);
            this->~CachedBlob();
            delete[] allocation;
        }
    };

    RendererShaderFactory::RendererShaderFactory(nvrhi::IDevice* device,
        const wchar_t* packagedShaderDirectory) noexcept : m_Device(device)
    {
        if (!packagedShaderDirectory || !*packagedShaderDirectory) return;
        const size_t length = wcslen(packagedShaderDirectory);
        if (length >= size_t(PTRDIFF_MAX) / sizeof(wchar_t)) return;
        auto* storage = Allocate((length + 1) * sizeof(wchar_t));
        if (!storage) { ReportError("Shader directory allocation failed."); return; }
        m_Directory = ::new (storage) wchar_t[length + 1];
        memcpy(m_Directory, packagedShaderDirectory, (length + 1) * sizeof(wchar_t));
    }

    RendererShaderFactory::~RendererShaderFactory() noexcept
    {
        ClearCache();
        delete[] reinterpret_cast<unsigned char*>(m_Directory);
    }

    void RendererShaderFactory::ClearCache() noexcept
    {
        while (m_Cache)
        {
            CachedBlob* removed = m_Cache;
            m_Cache = removed->next;
            removed->Destroy();
        }
    }

    RendererShaderFactory::CachedBlob* RendererShaderFactory::LoadBlob(
        const char* fileName, const char* entryName) noexcept
    {
        BlobName name;
        if (!m_Directory || !name.Parse(fileName, entryName))
        {
            ReportError("Invalid packaged shader request."); return nullptr;
        }
        for (CachedBlob* cached = m_Cache; cached; cached = cached->next)
            if (name.Matches(cached->Name(), cached->nameSize)) return cached;

        auto* nameStorage = Allocate(name.size + 1);
        if (!nameStorage) { ReportError("Shader name allocation failed."); return nullptr; }
        name.Write(reinterpret_cast<char*>(nameStorage));
        const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            reinterpret_cast<char*>(nameStorage), int(name.size), nullptr, 0);
        const size_t directorySize = wcslen(m_Directory);
        if (!wideSize || directorySize > size_t(PTRDIFF_MAX) / sizeof(wchar_t) - 2 ||
            size_t(wideSize) > size_t(PTRDIFF_MAX) / sizeof(wchar_t) - directorySize - 2)
        {
            delete[] nameStorage; ReportError("Invalid shader file path."); return nullptr;
        }
        auto* pathStorage = Allocate((directorySize + size_t(wideSize) + 2) * sizeof(wchar_t));
        if (!pathStorage) { delete[] nameStorage; ReportError("Shader path allocation failed."); return nullptr; }
        auto* path = ::new (pathStorage) wchar_t[directorySize + size_t(wideSize) + 2];
        memcpy(path, m_Directory, directorySize * sizeof(wchar_t));
        path[directorySize] = L'/';
        const int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            reinterpret_cast<char*>(nameStorage), int(name.size), path + directorySize + 1, wideSize);
        path[directorySize + size_t(wideSize) + 1] = L'\0';
        File file;
        if (converted == wideSize)
            file.handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        delete[] pathStorage;
        LARGE_INTEGER fileSize{};
        if (file.handle == INVALID_HANDLE_VALUE || !GetFileSizeEx(file.handle, &fileSize) || fileSize.QuadPart <= 0 ||
            uint64_t(fileSize.QuadPart) > uint64_t(PTRDIFF_MAX))
        {
            delete[] nameStorage; ReportError("Could not read compiled shader blob."); return nullptr;
        }
        const size_t dataOffset = (sizeof(CachedBlob) + name.size + 1 + 7) & ~size_t(7);
        const size_t size = size_t(fileSize.QuadPart);
        if (dataOffset > size_t(PTRDIFF_MAX) || size > size_t(PTRDIFF_MAX) - dataOffset)
        {
            delete[] nameStorage; ReportError("Shader blob exceeds addressable storage."); return nullptr;
        }
        auto* storage = Allocate(dataOffset + size);
        if (!storage) { delete[] nameStorage; ReportError("Shader blob allocation failed."); return nullptr; }
        auto* candidate = ::new (storage) CachedBlob{nullptr, name.size, dataOffset, size};
        memcpy(candidate->Name(), nameStorage, name.size + 1);
        delete[] nameStorage;
        size_t position = 0;
        while (position < size)
        {
            const DWORD requested = size - position < 1024u * 1024u ? DWORD(size - position) : 1024u * 1024u;
            DWORD received = 0;
            if (!ReadFile(file.handle, candidate->Bytes() + position, requested, &received, nullptr) ||
                !received || received > requested)
            {
                candidate->Destroy(); ReportError("Could not read complete shader blob."); return nullptr;
            }
            position += received;
        }
        LARGE_INTEGER finalSize{};
        if (!GetFileSizeEx(file.handle, &finalSize) || finalSize.QuadPart != fileSize.QuadPart)
        {
            candidate->Destroy(); ReportError("Shader file read failed."); return nullptr;
        }
        const HANDLE completedFile = file.handle;
        file.handle = INVALID_HANDLE_VALUE;
        if (!CloseHandle(completedFile))
        {
            candidate->Destroy(); ReportError("Shader file close failed."); return nullptr;
        }
        candidate->next = m_Cache;
        m_Cache = candidate;
        return candidate;
    }

    bool RendererShaderFactory::SelectBytecode(const char* fileName, const char* entryName,
        ArrayView<const shader_blob::Constant> defines, const void*& bytes, size_t& size) noexcept
    {
        if (!defines.IsValid() || defines.count > UINT32_MAX) return false;
        auto* blob = LoadBlob(fileName, entryName);
        if (!blob) return false;
        if (!shader_blob::find_permutation(blob->Bytes(), blob->byteCount, defines.data, uint32_t(defines.count), &bytes, &size))
        {
            ReportError("Required shader permutation is missing or the blob is corrupt."); return false;
        }
        return true;
    }

    nvrhi::ShaderHandle RendererShaderFactory::CreateShader(const char* fileName, const char* entryName,
        ArrayView<const shader_blob::Constant> defines, nvrhi::ShaderType shaderType) noexcept
    {
        if (!m_Device) return nullptr;
        const void* bytes = nullptr;
        size_t size = 0;
        if (!SelectBytecode(fileName, entryName, defines, bytes, size))
        {
            char message[768];
            const int length = snprintf(message, sizeof(message), "Shader request failed: %s, entry %s.",
                fileName ? fileName : "<null>", entryName ? entryName : "main");
            if (length < 0) ReportError("Shader request failed.");
            else
            {
                if (size_t(length) >= sizeof(message)) memcpy(message + sizeof(message) - 4, "...", 4);
                ReportError(message);
            }
            return nullptr;
        }
        nvrhi::ShaderDesc description;
        description.shaderType = shaderType;
        description.entryName = entryName ? entryName : "main";
        description.debugName = fileName;
        return m_Device->createShader(description, bytes, size);
    }

    void RendererShaderFactory::ReportError(const char* message) const noexcept
    {
        if (m_Device && m_Device->getMessageCallback())
            m_Device->getMessageCallback()->message(nvrhi::MessageSeverity::Error, message);
    }

#if defined(UVSR_BUILD_TESTING)
    void RendererShaderFactory::FailAllocationAfter(size_t successfulAllocations) noexcept
    {
        allocationCountdown = successfulAllocations;
    }
#endif
}
