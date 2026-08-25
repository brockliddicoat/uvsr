#include "renderer_targets.h"

#include "renderer_log.h"

#include <nvrhi/common/misc.h>
#include <nvrhi/utils.h>

#include <array>
#include <utility>
#include <vector>

namespace
{
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
        std::uint32_t sampleCount,
        DirectX::XMUINT2 presentationSize,
        std::uint32_t presentationSampleCount,
        bool enableMotionVectors,
        bool useReverseProjection,
        bool enableVisibilityResources,
        bool enableVisibilitySourceRadiance)
    {
        if (!device || size.x == 0u || size.y == 0u || sampleCount == 0u ||
            presentationSize.x == 0u || presentationSize.y == 0u ||
            presentationSampleCount == 0u ||
            size.x % presentationSize.x != 0u ||
            size.y % presentationSize.y != 0u)
        {
            return false;
        }
        const std::uint32_t presentationResolutionScale =
            size.x / presentationSize.x;
        if (presentationResolutionScale == 0u ||
            size.y / presentationSize.y != presentationResolutionScale ||
            sampleCount * presentationResolutionScale *
                    presentationResolutionScale !=
                presentationSampleCount)
        {
            return false;
        }

        RenderTargets candidate;
        candidate.VisibilityResourcesEnabled = enableVisibilityResources;
        candidate.VisibilitySourceRadianceEnabled =
            enableVisibilityResources && enableVisibilitySourceRadiance;
        candidate.MotionVectorsEnabled = enableMotionVectors;
        candidate.m_Size = size;
        candidate.m_SampleCount = sampleCount;
        candidate.m_PresentationSize = presentationSize;
        candidate.m_PresentationSampleCount = presentationSampleCount;
        candidate.m_PresentationResolutionScale =
            presentationResolutionScale;
        candidate.m_UseReverseProjection = useReverseProjection;

        nvrhi::TextureDesc description;
        description.width = size.x;
        description.height = size.y;
        description.initialState = nvrhi::ResourceStates::RenderTarget;
        description.isRenderTarget = true;
        description.useClearValue = true;
        description.clearValue = nvrhi::Color(0.f);
        description.sampleCount = sampleCount;
        description.dimension = sampleCount > 1u
            ? nvrhi::TextureDimension::Texture2DMS
            : nvrhi::TextureDimension::Texture2D;
        description.keepInitialState = true;
        description.mipLevels = 1u;

        description.format = nvrhi::Format::SRGBA8_UNORM;
        description.debugName = "GBufferDiffuse";
        candidate.GBufferDiffuse = device->createTexture(description);

        description.format = nvrhi::Format::RGBA8_UNORM;
        description.debugName = "PbrGBufferMaterial";
        candidate.GBufferSpecular = device->createTexture(description);

        description.format = nvrhi::Format::RGBA16_SNORM;
        description.debugName = "GBufferNormals";
        candidate.GBufferNormals = device->createTexture(description);

        description.format = nvrhi::Format::RGBA16_FLOAT;
        description.debugName = "GBufferEmissive";
        candidate.GBufferEmissive = device->createTexture(description);

        constexpr std::array depthFormats = {
            nvrhi::Format::D24S8,
            nvrhi::Format::D32S8,
            nvrhi::Format::D32,
            nvrhi::Format::D16
        };
        const nvrhi::FormatSupport depthFeatures =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::DepthStencil |
            nvrhi::FormatSupport::ShaderLoad;
        description.format = nvrhi::utils::ChooseFormat(
            device,
            depthFeatures,
            depthFormats.data(),
            depthFormats.size());
        description.isTypeless = true;
        description.initialState = nvrhi::ResourceStates::DepthWrite;
        description.clearValue = useReverseProjection
            ? nvrhi::Color(0.f)
            : nvrhi::Color(1.f);
        description.debugName = "GBufferDepth";
        candidate.Depth = device->createTexture(description);

        description.isTypeless = false;
        description.format = nvrhi::Format::RGBA16_FLOAT;
        description.initialState = nvrhi::ResourceStates::RenderTarget;
        description.clearValue = nvrhi::Color(0.f);
        description.debugName = "PbrGBufferMotionVectorsWithDepth";
        if (!enableMotionVectors)
        {
            description.width = 1u;
            description.height = 1u;
            description.sampleCount = 1u;
            description.dimension = nvrhi::TextureDimension::Texture2D;
        }
        candidate.MotionVectors = device->createTexture(description);

        description.width = size.x;
        description.height = size.y;
        description.sampleCount = sampleCount;
        description.dimension = sampleCount > 1u
            ? nvrhi::TextureDimension::Texture2DMS
            : nvrhi::TextureDimension::Texture2D;
        description.format = nvrhi::Format::R8_UNORM;
        description.clearValue = nvrhi::Color(1.f);
        description.debugName = "PbrMaterialAmbientOcclusion";
        candidate.MaterialAmbientOcclusion =
            device->createTexture(description);

        if (!candidate.Depth ||
            !candidate.GBufferDiffuse ||
            !candidate.GBufferSpecular ||
            !candidate.GBufferNormals ||
            !candidate.GBufferEmissive ||
            !candidate.MotionVectors ||
            !candidate.MaterialAmbientOcclusion)
        {
            log::error("Core renderer target creation failed");
            return false;
        }

        std::vector<nvrhi::TextureHandle> gbufferTargets = {
            candidate.GBufferDiffuse,
            candidate.GBufferSpecular,
            candidate.GBufferNormals,
            candidate.GBufferEmissive,
            candidate.MaterialAmbientOcclusion
        };
        if (enableMotionVectors)
            gbufferTargets.push_back(candidate.MotionVectors);
        candidate.GBufferFramebuffer = CreateFramebuffer(
            device, gbufferTargets, candidate.Depth);

        description.clearValue = nvrhi::Color(0.f);
        description.isUAV = sampleCount == 1u;
        description.format = nvrhi::Format::RGBA16_FLOAT;
        description.initialState = nvrhi::ResourceStates::RenderTarget;
        description.isVirtual = device->queryFeatureSupport(
            nvrhi::Feature::VirtualResources);
        description.debugName = "HdrColor";
        candidate.HdrColor = device->createTexture(description);

        if (sampleCount > 1u)
        {
            nvrhi::TextureDesc resolvedDescription = description;
            resolvedDescription.sampleCount = 1u;
            resolvedDescription.dimension = nvrhi::TextureDimension::Texture2D;
            resolvedDescription.isRenderTarget = false;
            resolvedDescription.useClearValue = false;
            resolvedDescription.isUAV = false;
            resolvedDescription.initialState =
                nvrhi::ResourceStates::ShaderResource;
            resolvedDescription.debugName = "ResolvedHdrColor";
            candidate.ResolvedHdrColor =
                device->createTexture(resolvedDescription);

            resolvedDescription.isUAV = true;
            resolvedDescription.initialState =
                nvrhi::ResourceStates::UnorderedAccess;
            resolvedDescription.debugName = "DeferredMsaaColor";
            candidate.DeferredMsaaColor =
                device->createTexture(resolvedDescription);
        }

        if (enableVisibilityResources)
        {
            nvrhi::TextureDesc visibilityDescription = description;
            visibilityDescription.sampleCount = 1u;
            visibilityDescription.dimension =
                nvrhi::TextureDimension::Texture2D;
            visibilityDescription.isRenderTarget = false;
            visibilityDescription.isUAV = true;
            visibilityDescription.useClearValue = false;
            visibilityDescription.initialState =
                nvrhi::ResourceStates::UnorderedAccess;
            visibilityDescription.format = nvrhi::Format::RGBA16_FLOAT;
            visibilityDescription.debugName =
                "ScreenSpaceVisibility/BaseLighting";
            candidate.BaseLighting =
                device->createTexture(visibilityDescription);

            if (candidate.VisibilitySourceRadianceEnabled)
            {
                visibilityDescription.debugName =
                    "ScreenSpaceVisibility/DirectDiffuseRadiance";
                candidate.DirectDiffuseRadiance =
                    device->createTexture(visibilityDescription);
            }

            if (sampleCount > 1u)
            {
                visibilityDescription.debugName =
                    "ScreenSpaceVisibility/MsaaComposite";
                candidate.VisibilityComposite =
                    device->createTexture(visibilityDescription);

                visibilityDescription.format = nvrhi::Format::R32_FLOAT;
                visibilityDescription.debugName =
                    "ScreenSpaceVisibility/ResolvedDepth";
                candidate.VisibilityDepth =
                    device->createTexture(visibilityDescription);

                visibilityDescription.format = nvrhi::Format::RGBA16_FLOAT;
                visibilityDescription.debugName =
                    "ScreenSpaceVisibility/ResolvedDiffuse";
                candidate.VisibilityGBufferDiffuse =
                    device->createTexture(visibilityDescription);
                visibilityDescription.debugName =
                    "ScreenSpaceVisibility/ResolvedMaterial";
                candidate.VisibilityGBufferMaterial =
                    device->createTexture(visibilityDescription);
                visibilityDescription.debugName =
                    "ScreenSpaceVisibility/ResolvedNormals";
                candidate.VisibilityGBufferNormals =
                    device->createTexture(visibilityDescription);
                visibilityDescription.debugName =
                    "ScreenSpaceVisibility/ResolvedEmissive";
                candidate.VisibilityGBufferEmissive =
                    device->createTexture(visibilityDescription);

                visibilityDescription.format = nvrhi::Format::R16_FLOAT;
                visibilityDescription.debugName =
                    "ScreenSpaceVisibility/ResolvedMaterialAO";
                candidate.VisibilityMaterialAmbientOcclusion =
                    device->createTexture(visibilityDescription);

                visibilityDescription.format = nvrhi::Format::RGBA16_FLOAT;
                visibilityDescription.debugName =
                    "ScreenSpaceVisibility/ResolvedMotion";
                candidate.VisibilityMotionVectors =
                    device->createTexture(visibilityDescription);
            }
        }

        description.sampleCount = 1u;
        description.dimension = nvrhi::TextureDimension::Texture2D;
        description.width = presentationSize.x;
        description.height = presentationSize.y;
        description.format = nvrhi::Format::RGBA16_FLOAT;
        if (presentationResolutionScale > 1u)
        {
            description.debugName = "PresentationSceneLinear";
            candidate.PresentationColor = device->createTexture(description);
        }

        description.format = nvrhi::Format::RG16_UINT;
        description.isUAV = false;
        description.width = size.x;
        description.height = size.y;
        description.debugName = "MaterialIDs";
        candidate.MaterialIDs = device->createTexture(description);

        description.width = presentationSize.x;
        description.height = presentationSize.y;
        description.format = nvrhi::Format::RGBA16_FLOAT;
        description.debugName = "LdrColor";
        candidate.LdrColor = device->createTexture(description);

        const bool requiredTexturesCreated =
            candidate.HdrColor &&
            candidate.LdrColor &&
            candidate.MaterialIDs &&
            (presentationResolutionScale == 1u ||
                candidate.PresentationColor) &&
            (sampleCount == 1u ||
                (candidate.ResolvedHdrColor && candidate.DeferredMsaaColor)) &&
            (!enableVisibilityResources || candidate.BaseLighting) &&
            (!candidate.VisibilitySourceRadianceEnabled ||
                candidate.DirectDiffuseRadiance) &&
            (!enableVisibilityResources || sampleCount == 1u ||
                (candidate.VisibilityComposite &&
                    candidate.VisibilityDepth &&
                    candidate.VisibilityGBufferDiffuse &&
                    candidate.VisibilityGBufferMaterial &&
                    candidate.VisibilityGBufferNormals &&
                    candidate.VisibilityGBufferEmissive &&
                    candidate.VisibilityMaterialAmbientOcclusion &&
                    candidate.VisibilityMotionVectors));
        if (!requiredTexturesCreated)
        {
            log::error("Renderer target creation failed");
            return false;
        }

        if (description.isVirtual)
        {
            std::vector<nvrhi::ITexture*> virtualTextures = {
                candidate.HdrColor,
                candidate.MaterialIDs,
                candidate.LdrColor
            };
            const auto addIfPresent = [&virtualTextures](nvrhi::ITexture* texture)
            {
                if (texture)
                    virtualTextures.push_back(texture);
            };
            addIfPresent(candidate.ResolvedHdrColor);
            addIfPresent(candidate.DeferredMsaaColor);
            addIfPresent(candidate.BaseLighting);
            addIfPresent(candidate.DirectDiffuseRadiance);
            addIfPresent(candidate.VisibilityComposite);
            addIfPresent(candidate.VisibilityDepth);
            addIfPresent(candidate.VisibilityGBufferDiffuse);
            addIfPresent(candidate.VisibilityGBufferMaterial);
            addIfPresent(candidate.VisibilityGBufferNormals);
            addIfPresent(candidate.VisibilityGBufferEmissive);
            addIfPresent(candidate.VisibilityMaterialAmbientOcclusion);
            addIfPresent(candidate.VisibilityMotionVectors);
            addIfPresent(candidate.PresentationColor);

            std::uint64_t heapSize = 0u;
            for (nvrhi::ITexture* texture : virtualTextures)
            {
                if (!texture)
                    return false;
                const nvrhi::MemoryRequirements requirements =
                    device->getTextureMemoryRequirements(texture);
                heapSize = nvrhi::align(heapSize, requirements.alignment);
                heapSize += requirements.size;
            }

            nvrhi::HeapDesc heapDescription;
            heapDescription.type = nvrhi::HeapType::DeviceLocal;
            heapDescription.capacity = heapSize;
            heapDescription.debugName = "RenderTargetHeap";
            candidate.Heap = device->createHeap(heapDescription);
            if (!candidate.Heap)
                return false;

            std::uint64_t offset = 0u;
            for (nvrhi::ITexture* texture : virtualTextures)
            {
                const nvrhi::MemoryRequirements requirements =
                    device->getTextureMemoryRequirements(texture);
                offset = nvrhi::align(offset, requirements.alignment);
                if (!device->bindTextureMemory(
                        texture, candidate.Heap, offset))
                {
                    log::error("Renderer target heap binding failed");
                    return false;
                }
                offset += requirements.size;
            }
        }

        candidate.HdrFramebuffer = CreateFramebuffer(
            device, { candidate.HdrColor }, candidate.Depth);
        if (candidate.PresentationColor)
        {
            candidate.PresentationFramebuffer = CreateFramebuffer(
                device, { candidate.PresentationColor });
        }
        candidate.LdrFramebuffer = CreateFramebuffer(
            device, { candidate.LdrColor });

        if (sampleCount > 1u)
        {
            nvrhi::TextureDesc pickDepthDescription =
                candidate.Depth->getDesc();
            pickDepthDescription.sampleCount = 1u;
            pickDepthDescription.dimension =
                nvrhi::TextureDimension::Texture2D;
            pickDepthDescription.isVirtual = false;
            pickDepthDescription.debugName = "MaterialIDDepth";
            candidate.MaterialIDDepth =
                device->createTexture(pickDepthDescription);
        }
        else
        {
            candidate.MaterialIDDepth = candidate.Depth;
        }
        candidate.MaterialIDFramebuffer = CreateFramebuffer(
            device, { candidate.MaterialIDs }, candidate.MaterialIDDepth);

        const bool requiredResourcesCreated =
            candidate.GBufferFramebuffer &&
            candidate.MaterialIDDepth &&
            candidate.HdrFramebuffer &&
            (!candidate.PresentationColor ||
                candidate.PresentationFramebuffer) &&
            candidate.LdrFramebuffer &&
            candidate.MaterialIDFramebuffer;
        if (!requiredResourcesCreated)
        {
            log::error("Renderer target creation failed");
            return false;
        }

        candidate.m_Valid = true;
        *this = std::move(candidate);
        return true;
    }

    bool RenderTargets::IsUpdateRequired(
        DirectX::XMUINT2 size,
        std::uint32_t sampleCount,
        DirectX::XMUINT2 presentationSize,
        std::uint32_t presentationSampleCount,
        bool enableVisibilityResources,
        bool enableVisibilitySourceRadiance,
        bool enableMotionVectors) const noexcept
    {
        return !m_Valid ||
            m_Size.x != size.x ||
            m_Size.y != size.y ||
            m_SampleCount != sampleCount ||
            m_PresentationSize.x != presentationSize.x ||
            m_PresentationSize.y != presentationSize.y ||
            m_PresentationSampleCount != presentationSampleCount ||
            VisibilityResourcesEnabled != enableVisibilityResources ||
            VisibilitySourceRadianceEnabled !=
                (enableVisibilityResources && enableVisibilitySourceRadiance) ||
            MotionVectorsEnabled != enableMotionVectors;
    }

    void RenderTargets::Clear(nvrhi::ICommandList* commandList) const
    {
        if (!commandList || !m_Valid)
            return;

        const nvrhi::FormatInfo& depthInfo =
            nvrhi::getFormatInfo(Depth->getDesc().format);
        commandList->clearDepthStencilTexture(
            Depth,
            nvrhi::AllSubresources,
            true,
            m_UseReverseProjection ? 0.f : 1.f,
            depthInfo.hasStencil,
            0u);
        commandList->clearTextureFloat(
            GBufferDiffuse, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            GBufferSpecular, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            GBufferNormals, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            GBufferEmissive, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            MotionVectors, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            MaterialAmbientOcclusion,
            nvrhi::AllSubresources,
            nvrhi::Color(1.f));
        commandList->clearTextureFloat(
            HdrColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
        if (BaseLighting)
        {
            commandList->clearTextureFloat(
                BaseLighting, nvrhi::AllSubresources, nvrhi::Color(0.f));
        }
        if (DirectDiffuseRadiance)
        {
            commandList->clearTextureFloat(
                DirectDiffuseRadiance,
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
        }
        if (PresentationColor)
        {
            commandList->clearTextureFloat(
                PresentationColor,
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
        }
        commandList->clearTextureFloat(
            LdrColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
    }
}
