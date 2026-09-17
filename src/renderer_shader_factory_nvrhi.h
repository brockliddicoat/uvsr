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

#include "array_view.h"
#include "shader_bytecode.h"
#include <nvrhi/nvrhi.h>

namespace uvsr
{
    struct RendererShaderFactoryTestAccess;

    // selected NVRHI implementation. cache storage owns loaded family bytes;
    // CreateShader borrows them synchronously. ClearCache preserves GPU shaders.
    class RendererShaderFactory final
    {
    public:
        RendererShaderFactory(nvrhi::IDevice* device, const wchar_t* packagedShaderDirectory) noexcept;
        ~RendererShaderFactory() noexcept;
        RendererShaderFactory(const RendererShaderFactory&) = delete;
        RendererShaderFactory& operator=(const RendererShaderFactory&) = delete;

        void ClearCache() noexcept;
        [[nodiscard]] nvrhi::ShaderHandle CreateShader(const char* fileName, const char* entryName,
            ArrayView<const shader_blob::Constant> defines, nvrhi::ShaderType shaderType) noexcept;

    private:
        struct CachedBlob;
        [[nodiscard]] CachedBlob* LoadBlob(const char* fileName, const char* entryName) noexcept;
        [[nodiscard]] bool SelectBytecode(const char* fileName, const char* entryName,
            ArrayView<const shader_blob::Constant> defines, const void*& bytes, size_t& size) noexcept;
        void ReportError(const char* message) const noexcept;
        static void FailAllocationAfter(size_t successfulAllocations) noexcept;
        nvrhi::DeviceHandle m_Device;
        wchar_t* m_Directory = nullptr;
        CachedBlob* m_Cache = nullptr;
        friend struct RendererShaderFactoryTestAccess;
    };
}
