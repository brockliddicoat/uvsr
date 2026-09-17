#include "renderer_targets_nvrhi.h"

#include "array_view.h"
#include "renderer_log.h"
#include "renderer_target_layout.h"

#include <nvrhi/utils.h>

#if defined(UVSR_RENDER_TARGETS_TEST_FAILURES)
namespace uvsr
{
    namespace
    {
        thread_local RendererTargetTestOperation failureOperation{};
        thread_local unsigned failureCountdown = 0;
        thread_local bool failureReached = false;
        bool RejectTargetOperation(RendererTargetTestOperation operation) noexcept
        {
            if (operation != failureOperation || failureCountdown == 0)
                return false;
            failureReached = --failureCountdown == 0;
            return failureReached;
        }
    }
    void SetRendererTargetTestFailure(RendererTargetTestOperation operation, unsigned occurrence) noexcept
    {
        failureOperation = operation;
        failureCountdown = occurrence;
        failureReached = false;
    }
    bool WasRendererTargetTestFailureReached() noexcept { return failureReached; }
}
#define UVSR_REJECT_TARGET(operation) uvsr::RejectTargetOperation(uvsr::RendererTargetTestOperation::operation)
#else
#define UVSR_REJECT_TARGET(operation) false
#endif

namespace
{
    nvrhi::Format ChooseDepthFormat(nvrhi::IDevice* device)
    {
        const nvrhi::Format formats[] = { nvrhi::Format::D24S8, nvrhi::Format::D32S8, nvrhi::Format::D32, nvrhi::Format::D16 };
        const auto features = nvrhi::FormatSupport::Texture | nvrhi::FormatSupport::DepthStencil | nvrhi::FormatSupport::ShaderLoad;
        return nvrhi::utils::ChooseFormat(device, features, formats, sizeof(formats) / sizeof(formats[0]));
    }

    nvrhi::FramebufferHandle CreateFramebuffer(
        nvrhi::IDevice* device,
        uvsr::ArrayView<nvrhi::ITexture* const> colorTargets,
        nvrhi::ITexture* depthTarget = nullptr)
    {
        // framebuffer creation consumes the borrowed attachment array synchronously.
        if (!colorTargets.IsValid() || colorTargets.count > nvrhi::c_MaxRenderTargets ||
            UVSR_REJECT_TARGET(Framebuffer))
            return nullptr;
        nvrhi::FramebufferDesc description;
        for (size_t index = 0; index < colorTargets.count; ++index)
        {
            if (!colorTargets.data[index])
                return nullptr;
            description.addColorAttachment(colorTargets.data[index]);
        }
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
        struct Placement { nvrhi::ITexture* texture; uint64_t offset; };
        Placement virtualTextures[2]{}; // only HDR and presentation color use placed memory.
        size_t virtualTextureCount = 0;
        const auto create = [&](nvrhi::TextureHandle& target, nvrhi::TextureDesc description,
            nvrhi::Format format, const char* name)
        {
            description.format = format;
            description.debugName = name;
            target = description.format == nvrhi::Format::UNKNOWN || UVSR_REJECT_TARGET(Texture)
                ? nullptr : device->createTexture(description);
            texturesCreated = texturesCreated && bool(target);
            if (target && description.isVirtual)
            {
                if (virtualTextureCount == sizeof(virtualTextures) / sizeof(virtualTextures[0]))
                    texturesCreated = false;
                else
                    virtualTextures[virtualTextureCount++] = { target.Get(), 0 };
            }
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

        if (virtualTextureCount != 0)
        {
            uint64_t heapSize = 0;
            for (size_t index = 0; index < virtualTextureCount; ++index)
            {
                auto& placement = virtualTextures[index];
                const nvrhi::MemoryRequirements requirements = device->getTextureMemoryRequirements(placement.texture);
                if (!AppendRendererTargetPlacement(requirements.size, requirements.alignment, heapSize, placement.offset))
                {
                    log::error("Renderer target memory requirements are invalid");
                    return false;
                }
            }
            nvrhi::HeapDesc heapDescription;
            heapDescription.type = nvrhi::HeapType::DeviceLocal;
            heapDescription.capacity = heapSize;
            heapDescription.debugName = "RenderTargetHeap";
            candidate.Heap = UVSR_REJECT_TARGET(Heap) ? nullptr : device->createHeap(heapDescription);
            if (!candidate.Heap)
                return false;
            for (size_t index = 0; index < virtualTextureCount; ++index)
            {
                const auto& placement = virtualTextures[index];
                if (UVSR_REJECT_TARGET(Bind) ||
                    !device->bindTextureMemory(placement.texture, candidate.Heap, placement.offset))
                {
                    log::error("Renderer target heap binding failed");
                    return false;
                }
            }
        }
        if (enableRasterLighting)
        {
            nvrhi::ITexture* const gbufferTargets[] = {
                candidate.GBufferDiffuse.Get(), candidate.GBufferSpecular.Get(), candidate.GBufferNormals.Get(),
                candidate.GBufferEmissive.Get(), candidate.MaterialAmbientOcclusion.Get()
            };
            nvrhi::ITexture* const hdrTargets[] = { candidate.HdrColor.Get() };
            candidate.GBufferFramebuffer = CreateFramebuffer(device, gbufferTargets, candidate.Depth);
            candidate.HdrFramebuffer = CreateFramebuffer(device, hdrTargets, candidate.Depth);
        }
        nvrhi::ITexture* const ldrTargets[] = { candidate.LdrColor.Get() };
        candidate.LdrFramebuffer = CreateFramebuffer(device, ldrTargets);
        if ((enableRasterLighting && (!candidate.GBufferFramebuffer || !candidate.HdrFramebuffer)) ||
            !candidate.LdrFramebuffer)
        {
            log::error("Renderer framebuffer creation failed");
            return false;
        }
        candidate.m_Valid = true;
        *this = static_cast<RenderTargets&&>(candidate);
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
        // integer picking clears must preserve the full no-hit ID.
        description.isUAV = true;
        description.format = nvrhi::Format::RG32_UINT;
        description.initialState = nvrhi::ResourceStates::RenderTarget;
        description.debugName = "MaterialIDs";
        nvrhi::TextureHandle ids = UVSR_REJECT_TARGET(Texture) ? nullptr : device->createTexture(description);
        nvrhi::TextureHandle depth = Depth;
        if (!depth)
        {
            description.isUAV = false;
            description.isTypeless = true;
            description.format = ChooseDepthFormat(device);
            if (description.format == nvrhi::Format::UNKNOWN)
                return false;
            description.initialState = nvrhi::ResourceStates::DepthWrite;
            description.clearValue = m_UseReverseProjection ? nvrhi::Color(0.f) : nvrhi::Color(1.f);
            description.debugName = "MaterialIDDepth";
            depth = UVSR_REJECT_TARGET(Texture) ? nullptr : device->createTexture(description);
        }
        if (!ids || !depth)
            return false;
        nvrhi::ITexture* const targets[] = { ids.Get() };
        nvrhi::FramebufferHandle framebuffer = CreateFramebuffer(device, targets, depth);
        if (!framebuffer)
            return false;
        MaterialIDs = static_cast<nvrhi::TextureHandle&&>(ids);
        MaterialIDDepth = static_cast<nvrhi::TextureHandle&&>(depth);
        MaterialIDFramebuffer = static_cast<nvrhi::FramebufferHandle&&>(framebuffer);
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
        nvrhi::ITexture* const clearTargets[] = { GBufferDiffuse.Get(), GBufferSpecular.Get(), GBufferNormals.Get(),
            GBufferEmissive.Get(), HdrColor.Get(),
            LdrColor.Get() };
        for (nvrhi::ITexture* texture : clearTargets)
        {
            if (texture)
                commandList->clearTextureFloat(texture, nvrhi::AllSubresources, nvrhi::Color(0.f));
        }
        if (MaterialAmbientOcclusion)
            commandList->clearTextureFloat(MaterialAmbientOcclusion, nvrhi::AllSubresources, nvrhi::Color(1.f));
    }
}
