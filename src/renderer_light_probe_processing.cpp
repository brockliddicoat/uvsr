#include "renderer_light_probe_processing.h"

#include "renderer_common_passes.h"
#include "renderer_light_probe_contract.h"
#include "renderer_log.h"
#include "renderer_shader_factory.h"

#include <algorithm>
#include <cmath>

namespace uvsr
{
    RendererLightProbeProcessing::RendererLightProbeProcessing(
        nvrhi::IDevice* device,
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
        const std::shared_ptr<RendererCommonPasses>& commonPasses,
        std::uint32_t intermediateTextureSize,
        nvrhi::Format intermediateTextureFormat)
        : m_Device(device)
        , m_CommonPasses(commonPasses)
        , m_IntermediateTextureSize(intermediateTextureSize)
    {
        m_Initialization.device = device != nullptr;
        m_Initialization.shaderFactory = shaderFactory != nullptr;
        m_Initialization.commonPasses =
            commonPasses && commonPasses->IsValid();
        if (!device || !shaderFactory || !commonPasses ||
            !commonPasses->IsValid() || intermediateTextureSize == 0u)
        {
            return;
        }

        m_GeometryShader = shaderFactory->CreateShader(
            "uvsr/light_probe_processing.hlsl",
            "cubemap_gs",
            nullptr,
            nvrhi::ShaderType::Geometry);
        m_MipPixelShader = shaderFactory->CreateShader(
            "uvsr/light_probe_processing.hlsl",
            "mip_ps",
            nullptr,
            nvrhi::ShaderType::Pixel);
        m_SpecularPixelShader = shaderFactory->CreateShader(
            "uvsr/light_probe_processing.hlsl",
            "specular_probe_ps",
            nullptr,
            nvrhi::ShaderType::Pixel);
        m_EnvironmentBrdfPixelShader = shaderFactory->CreateShader(
            "uvsr/light_probe_processing.hlsl",
            "environment_brdf_ps",
            nullptr,
            nvrhi::ShaderType::Pixel);
        m_Initialization.geometryShader = bool(m_GeometryShader);
        m_Initialization.mipPixelShader = bool(m_MipPixelShader);
        m_Initialization.specularPixelShader = bool(m_SpecularPixelShader);
        m_Initialization.environmentBrdfPixelShader =
            bool(m_EnvironmentBrdfPixelShader);

        nvrhi::BindingLayoutDesc bindingLayoutDescription;
        bindingLayoutDescription.visibility = nvrhi::ShaderType::Pixel;
        bindingLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0u),
            nvrhi::BindingLayoutItem::Sampler(0u),
            nvrhi::BindingLayoutItem::Texture_SRV(0u)
        };
        m_BindingLayout =
            device->createBindingLayout(bindingLayoutDescription);
        m_Initialization.bindingLayout = bool(m_BindingLayout);

        nvrhi::BufferDesc constantBufferDescription;
        constantBufferDescription.byteSize =
            sizeof(LightProbeProcessingConstants);
        constantBufferDescription.debugName =
            "RendererLightProbeProcessingConstants";
        constantBufferDescription.isConstantBuffer = true;
        constantBufferDescription.isVolatile = true;
        constantBufferDescription.maxVersions = 64u;
        m_ConstantBuffer = device->createBuffer(constantBufferDescription);
        m_Initialization.constantBuffer = bool(m_ConstantBuffer);

        nvrhi::TextureDesc intermediateDescription;
        intermediateDescription.arraySize = 6u;
        intermediateDescription.width = intermediateTextureSize;
        intermediateDescription.height = intermediateTextureSize;
        intermediateDescription.mipLevels = static_cast<std::uint32_t>(
            std::floor(std::log2(float(intermediateTextureSize)))) + 1u;
        intermediateDescription.dimension =
            nvrhi::TextureDimension::TextureCube;
        intermediateDescription.isRenderTarget = true;
        intermediateDescription.format = intermediateTextureFormat;
        intermediateDescription.initialState =
            nvrhi::ResourceStates::RenderTarget;
        intermediateDescription.keepInitialState = true;
        intermediateDescription.clearValue = nvrhi::Color(0.f);
        intermediateDescription.useClearValue = true;
        intermediateDescription.debugName =
            "RendererLightProbeIntermediate";
        m_IntermediateTexture =
            device->createTexture(intermediateDescription);
        m_Initialization.intermediateTexture = bool(m_IntermediateTexture);

        nvrhi::TextureDesc brdfDescription;
        brdfDescription.width = m_EnvironmentBrdfTextureSize;
        brdfDescription.height = m_EnvironmentBrdfTextureSize;
        brdfDescription.format = nvrhi::Format::RG16_FLOAT;
        brdfDescription.initialState = nvrhi::ResourceStates::ShaderResource;
        brdfDescription.keepInitialState = true;
        brdfDescription.isRenderTarget = true;
        brdfDescription.clearValue = nvrhi::Color(0.f);
        brdfDescription.useClearValue = true;
        brdfDescription.debugName = "EnvironmentBrdf";
        m_EnvironmentBrdfTexture = device->createTexture(brdfDescription);
        m_Initialization.environmentBrdfTexture =
            bool(m_EnvironmentBrdfTexture);

        if (m_EnvironmentBrdfTexture)
        {
            m_EnvironmentBrdfFramebuffer = device->createFramebuffer(
                nvrhi::FramebufferDesc().addColorAttachment(
                    m_EnvironmentBrdfTexture));
        }
        m_Initialization.environmentBrdfFramebuffer =
            bool(m_EnvironmentBrdfFramebuffer);

        if (m_EnvironmentBrdfFramebuffer &&
            commonPasses->FullscreenVertexShader() &&
            m_EnvironmentBrdfPixelShader)
        {
            nvrhi::GraphicsPipelineDesc pipelineDescription;
            pipelineDescription.VS = commonPasses->FullscreenVertexShader();
            pipelineDescription.PS = m_EnvironmentBrdfPixelShader;
            pipelineDescription.primType =
                nvrhi::PrimitiveType::TriangleStrip;
            pipelineDescription.renderState.rasterState.setCullNone();
            pipelineDescription.renderState.depthStencilState.depthTestEnable =
                false;
            pipelineDescription.renderState.depthStencilState.stencilEnable =
                false;
            m_EnvironmentBrdfPipeline = device->createGraphicsPipeline(
                pipelineDescription,
                m_EnvironmentBrdfFramebuffer->getFramebufferInfo());
        }
        m_Initialization.environmentBrdfPipeline =
            bool(m_EnvironmentBrdfPipeline);

        if (!IsValid())
            log::error("Renderer light-probe processing initialization failed");
    }

    bool RendererLightProbeProcessing::IsCubeTexture(
        const nvrhi::TextureDesc& description) noexcept
    {
        return description.dimension == nvrhi::TextureDimension::TextureCube ||
            description.dimension ==
                nvrhi::TextureDimension::TextureCubeArray;
    }

    bool RendererLightProbeProcessing::HasCubeSubresources(
        const nvrhi::TextureDesc& description,
        std::uint32_t baseArraySlice,
        std::uint32_t mipLevel) noexcept
    {
        return IsCubeTexture(description) &&
            mipLevel < description.mipLevels &&
            baseArraySlice <= description.arraySize &&
            description.arraySize - baseArraySlice >= 6u;
    }

    nvrhi::FramebufferHandle RendererLightProbeProcessing::GetFramebuffer(
        nvrhi::ITexture* texture,
        nvrhi::TextureSubresourceSet subresources)
    {
        const auto found = std::find_if(
            m_TextureSubresourceCache.begin(),
            m_TextureSubresourceCache.end(),
            [&](const TextureSubresourcesEntry& entry)
            {
                return entry.texture.Get() == texture &&
                    entry.subresources == subresources;
            });
        if (found != m_TextureSubresourceCache.end() && found->framebuffer)
            return found->framebuffer;

        nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(
                texture, subresources));
        if (!framebuffer)
            return nullptr;
        if (found != m_TextureSubresourceCache.end())
        {
            found->framebuffer = framebuffer;
        }
        else
        {
            m_TextureSubresourceCache.push_back(
                { texture, subresources, framebuffer, nullptr });
        }
        return framebuffer;
    }

    nvrhi::BindingSetHandle RendererLightProbeProcessing::GetBindingSet(
        nvrhi::ITexture* texture,
        nvrhi::TextureSubresourceSet subresources)
    {
        const auto found = std::find_if(
            m_TextureSubresourceCache.begin(),
            m_TextureSubresourceCache.end(),
            [&](const TextureSubresourcesEntry& entry)
            {
                return entry.texture.Get() == texture &&
                    entry.subresources == subresources;
            });
        if (found != m_TextureSubresourceCache.end() && found->bindingSet)
            return found->bindingSet;

        nvrhi::BindingSetDesc description;
        description.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0u, m_ConstantBuffer),
            nvrhi::BindingSetItem::Sampler(
                0u, m_CommonPasses->LinearWrapSampler()),
            nvrhi::BindingSetItem::Texture_SRV(
                0u,
                texture,
                nvrhi::Format::UNKNOWN,
                subresources)
        };
        nvrhi::BindingSetHandle bindingSet =
            m_Device->createBindingSet(description, m_BindingLayout);
        if (!bindingSet)
            return nullptr;
        if (found != m_TextureSubresourceCache.end())
        {
            found->bindingSet = bindingSet;
        }
        else
        {
            m_TextureSubresourceCache.push_back(
                { texture, subresources, nullptr, bindingSet });
        }
        return bindingSet;
    }

    nvrhi::GraphicsPipelineHandle RendererLightProbeProcessing::GetPipeline(
        PipelineKind kind,
        const nvrhi::FramebufferInfo& framebufferInfo)
    {
        const auto found = std::find_if(
            m_PipelineCache.begin(),
            m_PipelineCache.end(),
            [&](const PipelineEntry& entry)
            {
                return entry.kind == kind &&
                    entry.framebufferInfo == framebufferInfo;
            });
        if (found != m_PipelineCache.end())
            return found->pipeline;

        nvrhi::GraphicsPipelineDesc description;
        description.VS = m_CommonPasses->FullscreenVertexShader();
        description.GS = m_GeometryShader;
        description.PS = kind == PipelineKind::Blit
            ? m_MipPixelShader.Get()
            : m_SpecularPixelShader.Get();
        description.bindingLayouts = { m_BindingLayout };
        description.primType = nvrhi::PrimitiveType::TriangleStrip;
        description.renderState.rasterState.setCullNone();
        description.renderState.depthStencilState.depthTestEnable = false;
        description.renderState.depthStencilState.stencilEnable = false;
        nvrhi::GraphicsPipelineHandle pipeline =
            m_Device->createGraphicsPipeline(description, framebufferInfo);
        if (pipeline)
            m_PipelineCache.push_back({ kind, framebufferInfo, pipeline });
        return pipeline;
    }

    bool RendererLightProbeProcessing::BlitCubemap(
        nvrhi::ICommandList* commandList,
        nvrhi::ITexture* input,
        std::uint32_t inputBaseArraySlice,
        std::uint32_t inputMipLevel,
        nvrhi::ITexture* output,
        std::uint32_t outputBaseArraySlice,
        std::uint32_t outputMipLevel)
    {
        if (!IsValid() || !commandList || !input || !output ||
            !HasCubeSubresources(
                input->getDesc(), inputBaseArraySlice, inputMipLevel) ||
            !HasCubeSubresources(
                output->getDesc(), outputBaseArraySlice, outputMipLevel))
        {
            return false;
        }

        const nvrhi::TextureSubresourceSet outputSubresources(
            outputMipLevel, 1u, outputBaseArraySlice, 6u);
        nvrhi::FramebufferHandle framebuffer =
            GetFramebuffer(output, outputSubresources);
        if (!framebuffer)
            return false;
        nvrhi::GraphicsPipelineHandle pipeline = GetPipeline(
            PipelineKind::Blit, framebuffer->getFramebufferInfo());
        nvrhi::BindingSetHandle bindingSet = GetBindingSet(
            input,
            nvrhi::TextureSubresourceSet(
                inputMipLevel, 1u, inputBaseArraySlice, 6u));
        if (!pipeline || !bindingSet)
            return false;

        const LightProbeProcessingConstants constants{};
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));
        const std::uint32_t outputSize = std::max(
            output->getDesc().width >> outputMipLevel, 1u);

        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = framebuffer;
        state.bindings = { bindingSet };
        state.viewport.addViewport(nvrhi::Viewport(
            float(outputSize), float(outputSize)));
        state.viewport.addScissorRect(
            nvrhi::Rect(int(outputSize), int(outputSize)));
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1u;
        arguments.vertexCount = 4u;
        commandList->draw(arguments);
        return true;
    }

    bool RendererLightProbeProcessing::GenerateCubemapMips(
        nvrhi::ICommandList* commandList,
        nvrhi::ITexture* cubemap,
        std::uint32_t baseArraySlice,
        std::uint32_t sourceMipLevel,
        std::uint32_t levelsToGenerate)
    {
        if (!commandList || !cubemap || levelsToGenerate == 0u ||
            sourceMipLevel >= cubemap->getDesc().mipLevels ||
            levelsToGenerate >
                cubemap->getDesc().mipLevels - sourceMipLevel - 1u)
        {
            return false;
        }

        commandList->beginMarker("Cubemap Mips");
        bool succeeded = true;
        for (std::uint32_t index = 0u;
            index < levelsToGenerate;
            ++index)
        {
            const std::uint32_t mipLevel = sourceMipLevel + index;
            if (!BlitCubemap(
                    commandList,
                    cubemap,
                    baseArraySlice,
                    mipLevel,
                    cubemap,
                    baseArraySlice,
                    mipLevel + 1u))
            {
                succeeded = false;
                break;
            }
        }
        commandList->endMarker();
        return succeeded;
    }

    bool RendererLightProbeProcessing::RenderSpecularMap(
        nvrhi::ICommandList* commandList,
        float roughness,
        nvrhi::ITexture* input,
        nvrhi::TextureSubresourceSet inputSubresources,
        nvrhi::ITexture* output,
        std::uint32_t outputBaseArraySlice,
        std::uint32_t outputMipLevel)
    {
        const nvrhi::TextureDesc* inputDescription = input
            ? &input->getDesc()
            : nullptr;
        if (!IsValid() || !commandList || !input || !output ||
            !std::isfinite(roughness) ||
            !HasCubeSubresources(
                *inputDescription,
                inputSubresources.baseArraySlice,
                inputSubresources.baseMipLevel) ||
            inputSubresources.numArraySlices < 6u ||
            inputSubresources.numArraySlices >
                inputDescription->arraySize -
                    inputSubresources.baseArraySlice ||
            inputSubresources.numMipLevels == 0u ||
            inputSubresources.numMipLevels >
                inputDescription->mipLevels -
                    inputSubresources.baseMipLevel ||
            !HasCubeSubresources(
                output->getDesc(), outputBaseArraySlice, outputMipLevel))
        {
            return false;
        }

        const std::uint32_t inputSize = std::max(
            input->getDesc().width >> inputSubresources.baseMipLevel, 1u);
        const std::uint32_t outputSize = std::max(
            output->getDesc().width >> outputMipLevel, 1u);
        const float mipEstimate = std::max(
            std::log2(float(m_IntermediateTextureSize) /
                float(outputSize)) - 2.f,
            0.f);
        const std::uint32_t intermediateMip =
            static_cast<std::uint32_t>(mipEstimate);
        if (intermediateMip + 1u >=
            m_IntermediateTexture->getDesc().mipLevels)
        {
            return false;
        }
        const std::uint32_t intermediateSize = std::max(
            m_IntermediateTextureSize >> intermediateMip, 1u);

        const nvrhi::TextureSubresourceSet intermediateSubresources(
            intermediateMip, 1u, 0u, 6u);
        nvrhi::FramebufferHandle framebuffer = GetFramebuffer(
            m_IntermediateTexture, intermediateSubresources);
        if (!framebuffer)
            return false;
        nvrhi::GraphicsPipelineHandle pipeline = GetPipeline(
            PipelineKind::Specular, framebuffer->getFramebufferInfo());
        nvrhi::BindingSetHandle bindingSet =
            GetBindingSet(input, inputSubresources);
        if (!pipeline || !bindingSet)
            return false;

        LightProbeProcessingConstants constants{};
        constants.sampleCount = 1024u;
        constants.lodBias = 1.f;
        constants.roughness = std::max(roughness, 0.01f);
        constants.inputCubeSize = float(inputSize);
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

        commandList->beginMarker("Specular Light Probe");
        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = framebuffer;
        state.bindings = { bindingSet };
        state.viewport.addViewport(nvrhi::Viewport(
            float(intermediateSize), float(intermediateSize)));
        state.viewport.addScissorRect(
            nvrhi::Rect(int(intermediateSize), int(intermediateSize)));
        commandList->setGraphicsState(state);
        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1u;
        arguments.vertexCount = 4u;
        commandList->draw(arguments);

        const bool succeeded =
            BlitCubemap(
                commandList,
                m_IntermediateTexture,
                0u,
                intermediateMip,
                m_IntermediateTexture,
                0u,
                intermediateMip + 1u) &&
            BlitCubemap(
                commandList,
                m_IntermediateTexture,
                0u,
                intermediateMip + 1u,
                output,
                outputBaseArraySlice,
                outputMipLevel);
        commandList->endMarker();
        return succeeded;
    }

    bool RendererLightProbeProcessing::RenderEnvironmentBrdfTexture(
        nvrhi::ICommandList* commandList)
    {
        if (!IsValid() || !commandList)
            return false;

        commandList->beginMarker("Environment BRDF");
        nvrhi::GraphicsState state;
        state.pipeline = m_EnvironmentBrdfPipeline;
        state.framebuffer = m_EnvironmentBrdfFramebuffer;
        state.viewport.addViewport(nvrhi::Viewport(
            float(m_EnvironmentBrdfTextureSize),
            float(m_EnvironmentBrdfTextureSize)));
        state.viewport.addScissorRect(nvrhi::Rect(
            int(m_EnvironmentBrdfTextureSize),
            int(m_EnvironmentBrdfTextureSize)));
        commandList->setGraphicsState(state);
        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1u;
        arguments.vertexCount = 4u;
        commandList->draw(arguments);
        commandList->endMarker();
        return true;
    }

    void RendererLightProbeProcessing::ResetCaches()
    {
        m_TextureSubresourceCache.clear();
        m_PipelineCache.clear();
    }
}
