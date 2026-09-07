#include "renderer_targets.h"

#include "renderer_log.h"

#include <nvrhi/common/misc.h>
#include <nvrhi/utils.h>

#include <array>
#include <utility>
#include <vector>

namespace
{
    nvrhi::Format ChooseDepthFormat(nvrhi::IDevice* device)
    {
        constexpr std::array formats = { nvrhi::Format::D24S8, nvrhi::Format::D32S8, nvrhi::Format::D32, nvrhi::Format::D16 };
        const auto features = nvrhi::FormatSupport::Texture | nvrhi::FormatSupport::DepthStencil | nvrhi::FormatSupport::ShaderLoad;
        return nvrhi::utils::ChooseFormat(device, features, formats.data(), formats.size());
    }

    nvrhi::FramebufferHandle CreateFramebuffer(
        nvrhi::IDevice* device,
        const std::vector<nvrhi::TextureHandle>& colorTargets,
        nvrhi::ITexture* depthTarget = nullptr)
    {
        nvrhi::FramebufferDesc description;
        for (const nvrhi::TextureHandle& target : colorTargets)
            description.addColorAttachment(target);
        if (depthTarget)
            description.setDepthAttachment(depthTarget);
        return device->createFramebuffer(description);
    }
}

namespace uvsr
{
    bool RenderTargets::Init(
        nvrhi::IDevice* device,
        DirectX::XMUINT2 size,
        DirectX::XMUINT2 presentationSize,
        bool useReverseProjection,
        bool enableRasterLighting)
    {
        if (!device || !size.x || !size.y || !presentationSize.x || !presentationSize.y)
            return false;

        RenderTargets candidate;
        candidate.RasterLightingEnabled = enableRasterLighting;
        candidate.m_Size = size;
        candidate.m_PresentationSize = presentationSize;
        candidate.m_UseReverseProjection = useReverseProjection;

        bool texturesCreated = true;
        std::vector<std::pair<nvrhi::ITexture*, std::uint64_t>> virtualTextures;
        const auto create = [&](nvrhi::TextureHandle& target, nvrhi::TextureDesc description,
            nvrhi::Format format, const char* name)
        {
            description.format = format;
            description.debugName = name;
            target = device->createTexture(description);
            texturesCreated = texturesCreated && bool(target);
            if (target && description.isVirtual)
                virtualTextures.emplace_back(target.Get(), 0u);
        };
        nvrhi::TextureDesc raster;
        raster.width = size.x;
        raster.height = size.y;
        raster.initialState = nvrhi::ResourceStates::RenderTarget;
        raster.isRenderTarget = true;
        raster.useClearValue = true;
        raster.clearValue = nvrhi::Color(0.f);
        raster.keepInitialState = true;
        raster.mipLevels = 1u;
        if (enableRasterLighting)
        {
            create(candidate.GBufferDiffuse, raster, nvrhi::Format::SRGBA8_UNORM, "GBufferDiffuse");
            create(candidate.GBufferSpecular, raster, nvrhi::Format::RGBA8_UNORM, "PbrGBufferMaterial");
            create(candidate.GBufferNormals, raster, nvrhi::Format::RGBA16_SNORM, "GBufferNormals");
            create(candidate.GBufferEmissive, raster, nvrhi::Format::RGBA16_FLOAT, "GBufferEmissive");
            nvrhi::TextureDesc depth = raster;
            depth.isTypeless = true;
            depth.initialState = nvrhi::ResourceStates::DepthWrite;
            depth.clearValue = useReverseProjection ? nvrhi::Color(0.f) : nvrhi::Color(1.f);
            create(candidate.Depth, depth, ChooseDepthFormat(device), "GBufferDepth");

            nvrhi::TextureDesc ambient = raster;
            ambient.clearValue = nvrhi::Color(1.f);
            create(candidate.MaterialAmbientOcclusion, ambient, nvrhi::Format::R8_UNORM, "PbrMaterialAmbientOcclusion");
        }

        nvrhi::TextureDesc hdr = raster;
        hdr.isUAV = true;
        hdr.isVirtual = device->queryFeatureSupport(nvrhi::Feature::VirtualResources);
        if (enableRasterLighting)
        {
            create(candidate.HdrColor, hdr, nvrhi::Format::RGBA16_FLOAT, "HdrColor");

        }
        nvrhi::TextureDesc presentation = hdr;
        presentation.sampleCount = 1u;
        presentation.dimension = nvrhi::TextureDimension::Texture2D;
        presentation.width = presentationSize.x;
        presentation.height = presentationSize.y;
        presentation.isUAV = false;
        create(candidate.LdrColor, presentation, nvrhi::Format::RGBA16_FLOAT, "LdrColor");
        if (!texturesCreated)
        {
            log::error("Renderer target creation failed");
            return false;
        }

        if (!virtualTextures.empty())
        {
            std::uint64_t heapSize = 0u;
            for (auto& [texture, offset] : virtualTextures)
            {
                const nvrhi::MemoryRequirements requirements = device->getTextureMemoryRequirements(texture);
                offset = nvrhi::align(heapSize, requirements.alignment);
                heapSize = offset + requirements.size;
            }
            nvrhi::HeapDesc heapDescription;
            heapDescription.type = nvrhi::HeapType::DeviceLocal;
            heapDescription.capacity = heapSize;
            heapDescription.debugName = "RenderTargetHeap";
            candidate.Heap = device->createHeap(heapDescription);
            if (!candidate.Heap)
                return false;
            for (const auto& [texture, offset] : virtualTextures)
            {
                if (!device->bindTextureMemory(texture, candidate.Heap, offset))
                {
                    log::error("Renderer target heap binding failed");
                    return false;
                }
            }
        }
        if (enableRasterLighting)
        {
            std::vector<nvrhi::TextureHandle> gbufferTargets = {
                candidate.GBufferDiffuse, candidate.GBufferSpecular, candidate.GBufferNormals,
                candidate.GBufferEmissive, candidate.MaterialAmbientOcclusion
            };
            candidate.GBufferFramebuffer = CreateFramebuffer(device, gbufferTargets, candidate.Depth);
            candidate.HdrFramebuffer = CreateFramebuffer(device, { candidate.HdrColor }, candidate.Depth);
        }
        candidate.LdrFramebuffer = CreateFramebuffer(device, { candidate.LdrColor });
        if ((enableRasterLighting && (!candidate.GBufferFramebuffer || !candidate.HdrFramebuffer)) ||
            !candidate.LdrFramebuffer)
        {
            log::error("Renderer framebuffer creation failed");
            return false;
        }
        candidate.m_Valid = true;
        *this = std::move(candidate);
        return true;
    }

    bool RenderTargets::EnsureMaterialPickingTargets(nvrhi::IDevice* device)
    {
        if (MaterialIDFramebuffer)
            return true;
        if (!m_Valid || !device)
            return false;
        nvrhi::TextureDesc description;
        description.width = m_Size.x;
        description.height = m_Size.y;
        description.isRenderTarget = description.useClearValue = description.keepInitialState = true;
        description.format = nvrhi::Format::RG16_UINT;
        description.initialState = nvrhi::ResourceStates::RenderTarget;
        description.debugName = "MaterialIDs";
        nvrhi::TextureHandle ids = device->createTexture(description);
        nvrhi::TextureHandle depth = Depth;
        if (!depth)
        {
            description.isTypeless = true;
            description.format = ChooseDepthFormat(device);
            description.initialState = nvrhi::ResourceStates::DepthWrite;
            description.clearValue = m_UseReverseProjection ? nvrhi::Color(0.f) : nvrhi::Color(1.f);
            description.debugName = "MaterialIDDepth";
            depth = device->createTexture(description);
        }
        if (!ids || !depth)
            return false;
        nvrhi::FramebufferHandle framebuffer = CreateFramebuffer(device, { ids }, depth);
        if (!framebuffer)
            return false;
        MaterialIDs = std::move(ids);
        MaterialIDDepth = std::move(depth);
        MaterialIDFramebuffer = std::move(framebuffer);
        return true;
    }

    bool RenderTargets::IsUpdateRequired(
        DirectX::XMUINT2 size,
        DirectX::XMUINT2 presentationSize,
        bool enableRasterLighting) const noexcept
    {
        return !m_Valid || RasterLightingEnabled != enableRasterLighting ||
            m_Size.x != size.x ||
            m_Size.y != size.y ||
            m_PresentationSize.x != presentationSize.x ||
            m_PresentationSize.y != presentationSize.y;
    }

    void RenderTargets::Clear(nvrhi::ICommandList* commandList) const
    {
        if (!commandList || !m_Valid)
            return;
        if (Depth)
        {
            const bool hasStencil = nvrhi::getFormatInfo(Depth->getDesc().format).hasStencil;
            commandList->clearDepthStencilTexture(Depth, nvrhi::AllSubresources,
                true, m_UseReverseProjection ? 0.f : 1.f, hasStencil, 0u);
        }
        for (nvrhi::ITexture* texture : { GBufferDiffuse.Get(), GBufferSpecular.Get(), GBufferNormals.Get(),
            GBufferEmissive.Get(), HdrColor.Get(),
             LdrColor.Get() })
        {
            if (texture)
                commandList->clearTextureFloat(texture, nvrhi::AllSubresources, nvrhi::Color(0.f));
        }
        if (MaterialAmbientOcclusion)
            commandList->clearTextureFloat(MaterialAmbientOcclusion, nvrhi::AllSubresources, nvrhi::Color(1.f));
    }
}
