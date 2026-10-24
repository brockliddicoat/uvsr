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

#include "renderer_texture_bmp_nvrhi.h"
#include "renderer_common_passes_nvrhi.h"

namespace uvsr
{
bool SaveRendererTextureBmp(
    nvrhi::IDevice* device,
    RendererCommonPasses* commonPasses,
    nvrhi::ITexture* texture,
    nvrhi::ResourceStates textureState,
    const wchar_t* path)
{
    if (!device || !texture || !path || !path[0])
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
    if (!device->executeCommandList(commandList))
        return false;

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
}
