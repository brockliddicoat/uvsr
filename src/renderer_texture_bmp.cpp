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

#include "renderer_texture_bmp.h"

#if !defined(UVSR_RENDERER_BMP_ENCODER_ONLY)
#include "renderer_common_passes.h"
#endif

#include <array>
#include <fstream>
#include <limits>
#include <vector>

namespace uvsr
{
namespace
{
    constexpr std::uint32_t BmpHeaderSize = 54u;

    void StoreU16(std::array<std::uint8_t, BmpHeaderSize>& header,
        std::size_t offset, std::uint16_t value)
    {
        header[offset] = std::uint8_t(value);
        header[offset + 1u] = std::uint8_t(value >> 8u);
    }

    void StoreU32(std::array<std::uint8_t, BmpHeaderSize>& header,
        std::size_t offset, std::uint32_t value)
    {
        for (std::size_t byte = 0u; byte < 4u; ++byte)
            header[offset + byte] = std::uint8_t(value >> (byte * 8u));
    }
}

bool WriteRendererBmp(
    const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t sourceRowPitch,
    const void* rgbaPixels)
{
    constexpr std::uint64_t BytesPerPixel = 4u;
    if (path.empty() || !rgbaPixels || width == 0u || height == 0u ||
        width > std::uint32_t(std::numeric_limits<std::int32_t>::max()) ||
        height > std::uint32_t(std::numeric_limits<std::int32_t>::max()))
    {
        return false;
    }

    const std::uint64_t packedRowBytes = std::uint64_t(width) * BytesPerPixel;
    const std::uint64_t pixelBytes = packedRowBytes * height;
    if (sourceRowPitch < packedRowBytes ||
        pixelBytes > std::numeric_limits<std::uint32_t>::max() - BmpHeaderSize)
    {
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;

    std::array<std::uint8_t, BmpHeaderSize> header{};
    header[0] = 'B';
    header[1] = 'M';
    StoreU32(header, 2u, BmpHeaderSize + std::uint32_t(pixelBytes));
    StoreU32(header, 10u, BmpHeaderSize);
    StoreU32(header, 14u, 40u);
    StoreU32(header, 18u, width);
    StoreU32(header, 22u, height);
    StoreU16(header, 26u, 1u);
    StoreU16(header, 28u, 32u);
    StoreU32(header, 34u, std::uint32_t(pixelBytes));
    output.write(
        reinterpret_cast<const char*>(header.data()),
        std::streamsize(header.size()));

    const auto* source = static_cast<const std::uint8_t*>(rgbaPixels);
    std::vector<std::uint8_t> bgraRow(
        static_cast<std::size_t>(packedRowBytes),
        0u);
    for (std::uint32_t outputRow = 0u; outputRow < height; ++outputRow)
    {
        const std::uint32_t sourceRow = height - outputRow - 1u;
        const std::uint8_t* sourcePixel =
            source + std::size_t(sourceRow) * sourceRowPitch;
        for (std::uint32_t column = 0u; column < width; ++column)
        {
            const std::size_t offset = std::size_t(column) * 4u;
            bgraRow[offset] = sourcePixel[offset + 2u];
            bgraRow[offset + 1u] = sourcePixel[offset + 1u];
            bgraRow[offset + 2u] = sourcePixel[offset];
            bgraRow[offset + 3u] = sourcePixel[offset + 3u];
        }
        output.write(
            reinterpret_cast<const char*>(bgraRow.data()),
            std::streamsize(bgraRow.size()));
    }
    output.close();
    return output.good();
}

#if !defined(UVSR_RENDERER_BMP_ENCODER_ONLY)
bool SaveRendererTextureBmp(
    nvrhi::IDevice* device,
    RendererCommonPasses* commonPasses,
    nvrhi::ITexture* texture,
    nvrhi::ResourceStates textureState,
    const std::filesystem::path& path)
{
    if (!device || !texture || path.empty())
        return false;

    const nvrhi::TextureDesc& sourceDescription = texture->getDesc();
    if (sourceDescription.dimension != nvrhi::TextureDimension::Texture2D ||
        sourceDescription.width == 0u || sourceDescription.height == 0u ||
        sourceDescription.sampleCount != 1u)
    {
        return false;
    }

    const bool directlyReadable =
        sourceDescription.format == nvrhi::Format::RGBA8_UNORM ||
        sourceDescription.format == nvrhi::Format::SRGBA8_UNORM;
    nvrhi::TextureDesc readbackDescription;
    readbackDescription.width = sourceDescription.width;
    readbackDescription.height = sourceDescription.height;
    readbackDescription.mipLevels = 1u;
    readbackDescription.format = directlyReadable
        ? sourceDescription.format
        : nvrhi::Format::SRGBA8_UNORM;
    readbackDescription.dimension = nvrhi::TextureDimension::Texture2D;

    nvrhi::TextureHandle convertedTexture;
    nvrhi::FramebufferHandle convertedFramebuffer;
    nvrhi::ITexture* copySource = texture;
    if (!directlyReadable)
    {
        if (!commonPasses || !commonPasses->IsValid())
            return false;
        nvrhi::TextureDesc convertedDescription = readbackDescription;
        convertedDescription.isRenderTarget = true;
        convertedDescription.initialState =
            nvrhi::ResourceStates::RenderTarget;
        convertedDescription.keepInitialState = true;
        convertedDescription.debugName = "Renderer/BMP Conversion";
        convertedTexture = device->createTexture(convertedDescription);
        if (!convertedTexture)
            return false;
        convertedFramebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(convertedTexture));
        if (!convertedFramebuffer)
            return false;
        copySource = convertedTexture;
    }

    nvrhi::StagingTextureHandle stagingTexture =
        device->createStagingTexture(
            readbackDescription,
            nvrhi::CpuAccessMode::Read);
    nvrhi::CommandListHandle commandList = device->createCommandList();
    if (!stagingTexture || !commandList)
        return false;

    commandList->open();
    if (textureState != nvrhi::ResourceStates::Unknown)
    {
        commandList->beginTrackingTextureState(
            texture,
            nvrhi::TextureSubresourceSet(0u, 1u, 0u, 1u),
            textureState);
    }
    if (!directlyReadable && !commonPasses->BlitTexture(
            commandList,
            convertedFramebuffer,
            texture))
    {
        commandList->close();
        return false;
    }
    commandList->copyTexture(
        stagingTexture,
        nvrhi::TextureSlice(),
        copySource,
        nvrhi::TextureSlice());
    if (textureState != nvrhi::ResourceStates::Unknown)
    {
        commandList->setTextureState(
            texture,
            nvrhi::TextureSubresourceSet(0u, 1u, 0u, 1u),
            textureState);
        commandList->commitBarriers();
    }
    commandList->close();
    device->executeCommandList(commandList);

    std::size_t rowPitch = 0u;
    const void* pixels = device->mapStagingTexture(
        stagingTexture,
        nvrhi::TextureSlice(),
        nvrhi::CpuAccessMode::Read,
        &rowPitch);
    if (!pixels)
        return false;
    const bool written = WriteRendererBmp(
        path,
        readbackDescription.width,
        readbackDescription.height,
        rowPitch,
        pixels);
    device->unmapStagingTexture(stagingTexture);
    return written;
}
#endif
}
