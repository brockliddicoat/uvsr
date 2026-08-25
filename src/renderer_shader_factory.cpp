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

#include "renderer_shader_factory.h"

#include "shader_blob.h"

#include <cctype>
#include <fstream>
#include <limits>
#include <utility>

namespace uvsr
{
namespace
{
    bool IsIdentifier(const char* value)
    {
        if (value == nullptr || *value == '\0')
            return false;
        const auto first = static_cast<unsigned char>(*value);
        if (!std::isalpha(first) && *value != '_')
            return false;
        for (++value; *value != '\0'; ++value)
        {
            const auto character = static_cast<unsigned char>(*value);
            if (!std::isalnum(character) && *value != '_')
                return false;
        }
        return true;
    }
}

RendererShaderMacro::RendererShaderMacro(
    std::string macroName,
    std::string macroDefinition)
    : name(std::move(macroName))
    , definition(std::move(macroDefinition))
{
}

RendererShaderFactory::RendererShaderFactory(
    nvrhi::DeviceHandle device,
    std::filesystem::path packagedShaderDirectory)
    : m_Device(std::move(device))
    , m_PackagedShaderDirectory(std::move(packagedShaderDirectory))
{
}

void RendererShaderFactory::ClearCache()
{
    m_Cache.clear();
}

std::optional<std::filesystem::path> RendererShaderFactory::ResolveBlobPath(
    const std::filesystem::path& packagedShaderDirectory,
    const char* fileName,
    const char* entryName)
{
    if (fileName == nullptr || *fileName == '\0')
        return std::nullopt;

    const std::string requested(fileName);
    if (requested.find('\\') != std::string::npos)
        return std::nullopt;

    const std::filesystem::path logical = std::filesystem::u8path(requested);
    if (logical.is_absolute() || logical.has_root_name() ||
        logical.has_root_directory() || logical.extension() != ".hlsl" ||
        logical.lexically_normal().generic_string() != requested)
    {
        return std::nullopt;
    }

    auto component = logical.begin();
    if (component == logical.end() || component->generic_string() != "uvsr")
        return std::nullopt;
    ++component;
    if (component == logical.end())
        return std::nullopt;
    std::filesystem::path relative = *component;
    if (++component != logical.end() || relative == "." || relative == "..")
        return std::nullopt;

    const char* resolvedEntry = entryName == nullptr ? "main" : entryName;
    if (!IsIdentifier(resolvedEntry))
        return std::nullopt;

    std::string family = relative.stem().generic_string();
    if (family.empty())
        return std::nullopt;
    if (std::string(resolvedEntry) != "main")
        family += "_" + std::string(resolvedEntry);
    relative.replace_filename(family + ".bin");
    return packagedShaderDirectory / relative;
}

std::shared_ptr<const RendererShaderFactory::Blob>
RendererShaderFactory::LoadBlob(const std::filesystem::path& path)
{
    const std::string key = path.lexically_normal().generic_string();
    const auto cached = m_Cache.find(key);
    if (cached != m_Cache.end())
        return cached->second;

    std::error_code error;
    const uintmax_t fileSize = std::filesystem::file_size(path, error);
    if (error || fileSize == 0u ||
        fileSize > std::numeric_limits<size_t>::max() ||
        fileSize > uintmax_t(std::numeric_limits<std::streamsize>::max()))
    {
        ReportError("Could not read compiled shader blob " + key + ".");
        return nullptr;
    }

    auto bytes = std::make_shared<Blob>(static_cast<size_t>(fileSize));
    std::ifstream input(path, std::ios::binary);
    input.read(
        reinterpret_cast<char*>(bytes->data()),
        static_cast<std::streamsize>(bytes->size()));
    if (!input || input.peek() != std::ifstream::traits_type::eof())
    {
        ReportError("Could not read complete shader blob " + key + ".");
        return nullptr;
    }

    m_Cache.emplace(key, bytes);
    return bytes;
}

std::optional<RendererShaderFactory::SelectedBytecode>
RendererShaderFactory::SelectBytecode(
    const char* fileName,
    const char* entryName,
    const std::vector<RendererShaderMacro>* defines)
{
    const auto path = ResolveBlobPath(
        m_PackagedShaderDirectory, fileName, entryName);
    if (!path)
    {
        ReportError("Invalid packaged shader request.");
        return std::nullopt;
    }

    std::shared_ptr<const Blob> blob = LoadBlob(*path);
    if (!blob)
        return std::nullopt;

    std::vector<shader_blob::Constant> constants;
    if (defines)
    {
        if (defines->size() > std::numeric_limits<uint32_t>::max())
        {
            ReportError("Shader permutation contains too many constants.");
            return std::nullopt;
        }
        constants.reserve(defines->size());
        for (const RendererShaderMacro& define : *defines)
        {
            constants.push_back({
                define.name.c_str(),
                define.definition.c_str()
            });
        }
    }

    SelectedBytecode selected;
    selected.blob = std::move(blob);
    if (!shader_blob::find_permutation(
            selected.blob->data(),
            selected.blob->size(),
            constants.data(),
            static_cast<uint32_t>(constants.size()),
            &selected.data,
            &selected.size))
    {
        ReportError(
            path->generic_string() + ": " +
            shader_blob::format_not_found_message(
                selected.blob->data(),
                selected.blob->size(),
                constants.data(),
                static_cast<uint32_t>(constants.size())));
        return std::nullopt;
    }
    return selected;
}

nvrhi::ShaderHandle RendererShaderFactory::CreateShader(
    const char* fileName,
    const char* entryName,
    const std::vector<RendererShaderMacro>* defines,
    nvrhi::ShaderType shaderType)
{
    if (!m_Device)
        return nullptr;
    const auto selected = SelectBytecode(fileName, entryName, defines);
    if (!selected)
        return nullptr;

    const char* resolvedEntry = entryName == nullptr ? "main" : entryName;
    nvrhi::ShaderDesc description;
    description.shaderType = shaderType;
    description.entryName = resolvedEntry;
    description.debugName = fileName ? fileName : "";
    return m_Device->createShader(
        description, selected->data, selected->size);
}

nvrhi::ShaderLibraryHandle RendererShaderFactory::CreateShaderLibrary(
    const char* fileName,
    const std::vector<RendererShaderMacro>* defines)
{
    if (!m_Device)
        return nullptr;
    const auto selected = SelectBytecode(fileName, "main", defines);
    if (!selected)
        return nullptr;
    return m_Device->createShaderLibrary(selected->data, selected->size);
}

void RendererShaderFactory::ReportError(const std::string& message) const
{
    if (!m_Device)
        return;
    nvrhi::IMessageCallback* callback = m_Device->getMessageCallback();
    if (callback)
        callback->message(nvrhi::MessageSeverity::Error, message.c_str());
}
}
