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

#include <nvrhi/nvrhi.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace uvsr
{
    struct RendererShaderMacro
    {
        std::string name;
        std::string definition;

        RendererShaderMacro(std::string macroName, std::string macroDefinition);
    };

    struct RendererShaderFactoryTestAccess;

    class RendererShaderFactory final
    {
    public:
        RendererShaderFactory(
            nvrhi::DeviceHandle device,
            std::filesystem::path packagedShaderDirectory);

        void ClearCache();

        nvrhi::ShaderHandle CreateShader(
            const char* fileName,
            const char* entryName,
            const std::vector<RendererShaderMacro>* defines,
            nvrhi::ShaderType shaderType);

        nvrhi::ShaderLibraryHandle CreateShaderLibrary(
            const char* fileName,
            const std::vector<RendererShaderMacro>* defines);

    private:
        using Blob = std::vector<uint8_t>;

        struct SelectedBytecode
        {
            std::shared_ptr<const Blob> blob;
            const void* data = nullptr;
            size_t size = 0u;
        };

        [[nodiscard]] static std::optional<std::filesystem::path>
            ResolveBlobPath(
                const std::filesystem::path& packagedShaderDirectory,
                const char* fileName,
                const char* entryName);

        [[nodiscard]] std::shared_ptr<const Blob> LoadBlob(
            const std::filesystem::path& path);

        [[nodiscard]] std::optional<SelectedBytecode> SelectBytecode(
            const char* fileName,
            const char* entryName,
            const std::vector<RendererShaderMacro>* defines);

        void ReportError(const std::string& message) const;

        nvrhi::DeviceHandle m_Device;
        std::filesystem::path m_PackagedShaderDirectory;
        std::unordered_map<std::string, std::shared_ptr<const Blob>> m_Cache;

        friend struct RendererShaderFactoryTestAccess;
    };
}
