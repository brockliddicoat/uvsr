#include "renderer_light_probe_processing.h"

#include "renderer_common_passes.h"
#include "renderer_light_probe_contract.h"
#include "renderer_log.h"
#include "renderer_shader_factory.h"

#include <algorithm>
#include <cmath>

namespace
{
    void DrawFullscreen(
        nvrhi::ICommandList* commandList,
        nvrhi::IGraphicsPipeline* pipeline,
        nvrhi::IFramebuffer* framebuffer,
        nvrhi::IBindingSet* bindingSet,
        std::uint32_t size)
    {
        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = framebuffer;
        if (bindingSet)
            state.bindings = { bindingSet };
        state.viewport.addViewport(nvrhi::Viewport(float(size), float(size)));
        state.viewport.addScissorRect(nvrhi::Rect(int(size), int(size)));
        commandList->setGraphicsState(state);
        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1u;
        arguments.vertexCount = 4u;
        commandList->draw(arguments);
    }
}

namespace uvsr
{
    RendererLightProbeProcessing::RendererLightProbeProcessing(
        nvrhi::IDevice* device,
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
        const std::shared_ptr<RendererCommonPasses>& commonPasses,
        std::uint32_t intermediateTextureSize,
        nvrhi::Format intermediateTextureFormat)
        : m_Device(device)
        , m_ShaderFactory(shaderFactory)
        , m_CommonPasses(commonPasses)
        , m_IntermediateTextureSize(intermediateTextureSize)
        , m_IntermediateTextureFormat(intermediateTextureFormat)
    {
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
        nvrhi::BindingLayoutDesc bindingLayoutDescription;
        bindingLayoutDescription.visibility = nvrhi::ShaderType::Pixel;
        bindingLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0u),
            nvrhi::BindingLayoutItem::Sampler(0u),
            nvrhi::BindingLayoutItem::Texture_SRV(0u)
        };
        m_BindingLayout =
            device->createBindingLayout(bindingLayoutDescription);

        nvrhi::BufferDesc constantBufferDescription;
        constantBufferDescription.byteSize =
            sizeof(LightProbeProcessingConstants);
        constantBufferDescription.debugName =
            "RendererLightProbeProcessingConstants";
        constantBufferDescription.isConstantBuffer = true;
        constantBufferDescription.isVolatile = true;
        constantBufferDescription.maxVersions = 64u;
        m_ConstantBuffer = device->createBuffer(constantBufferDescription);

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

    RendererLightProbeProcessing::TextureSubresourcesEntry&
        RendererLightProbeProcessing::GetTextureSubresources(
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
        if (found != m_TextureSubresourceCache.end())
            return *found;
        m_TextureSubresourceCache.push_back({ texture, subresources, nullptr, nullptr });
        return m_TextureSubresourceCache.back();
    }

    nvrhi::FramebufferHandle RendererLightProbeProcessing::GetFramebuffer(
        nvrhi::ITexture* texture,
        nvrhi::TextureSubresourceSet subresources)
    {
        auto& entry = GetTextureSubresources(texture, subresources);
        if (!entry.framebuffer)
            entry.framebuffer = m_Device->createFramebuffer(
                nvrhi::FramebufferDesc().addColorAttachment(texture, subresources));
        return entry.framebuffer;
    }

    nvrhi::BindingSetHandle RendererLightProbeProcessing::GetBindingSet(
        nvrhi::ITexture* texture,
        nvrhi::TextureSubresourceSet subresources)
    {
        auto& entry = GetTextureSubresources(texture, subresources);
        if (entry.bindingSet)
            return entry.bindingSet;

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
        entry.bindingSet = m_Device->createBindingSet(description, m_BindingLayout);
        return entry.bindingSet;
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

        DrawFullscreen(commandList, pipeline, framebuffer, bindingSet, outputSize);
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

        if (!m_SpecularPixelShader)
            m_SpecularPixelShader = m_ShaderFactory->CreateShader(
                "uvsr/light_probe_processing.hlsl", "specular_probe_ps",
                nullptr, nvrhi::ShaderType::Pixel);
        if (!m_IntermediateTexture)
        {
            nvrhi::TextureDesc intermediateDescription;
            intermediateDescription.arraySize = 6u;
            intermediateDescription.width = m_IntermediateTextureSize;
            intermediateDescription.height = m_IntermediateTextureSize;
            intermediateDescription.mipLevels = static_cast<std::uint32_t>(
                std::floor(std::log2(float(m_IntermediateTextureSize)))) + 1u;
            intermediateDescription.dimension =
                nvrhi::TextureDimension::TextureCube;
            intermediateDescription.isRenderTarget = true;
            intermediateDescription.format = m_IntermediateTextureFormat;
            intermediateDescription.initialState =
                nvrhi::ResourceStates::RenderTarget;
            intermediateDescription.keepInitialState = true;
            intermediateDescription.clearValue = nvrhi::Color(0.f);
            intermediateDescription.useClearValue = true;
            intermediateDescription.debugName =
                "RendererLightProbeIntermediate";
            m_IntermediateTexture =
                m_Device->createTexture(intermediateDescription);
        }
        if (!m_SpecularPixelShader || !m_IntermediateTexture)
            return false;

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
        DrawFullscreen(commandList, pipeline, framebuffer, bindingSet, intermediateSize);

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

        if (!m_EnvironmentBrdfPipeline)
        {
            const nvrhi::ShaderHandle pixelShader = m_ShaderFactory->CreateShader(
                "uvsr/light_probe_processing.hlsl", "environment_brdf_ps",
                nullptr, nvrhi::ShaderType::Pixel);
            if (!pixelShader)
                return false;
            if (!m_EnvironmentBrdfTexture)
            {
                nvrhi::TextureDesc description;
                description.width = description.height = m_EnvironmentBrdfTextureSize;
                description.format = nvrhi::Format::RG16_FLOAT;
                description.initialState = nvrhi::ResourceStates::ShaderResource;
                description.keepInitialState = true;
                description.isRenderTarget = true;
                description.clearValue = nvrhi::Color(0.f);
                description.useClearValue = true;
                description.debugName = "EnvironmentBrdf";
                m_EnvironmentBrdfTexture = m_Device->createTexture(description);
            }
            if (!m_EnvironmentBrdfTexture)
                return false;
            if (!m_EnvironmentBrdfFramebuffer)
                m_EnvironmentBrdfFramebuffer = m_Device->createFramebuffer(
                    nvrhi::FramebufferDesc().addColorAttachment(m_EnvironmentBrdfTexture));
            if (!m_EnvironmentBrdfFramebuffer)
                return false;
            nvrhi::GraphicsPipelineDesc description;
            description.VS = m_CommonPasses->FullscreenVertexShader();
            description.PS = pixelShader;
            description.primType = nvrhi::PrimitiveType::TriangleStrip;
            description.renderState.rasterState.setCullNone();
            description.renderState.depthStencilState.depthTestEnable = false;
            description.renderState.depthStencilState.stencilEnable = false;
            m_EnvironmentBrdfPipeline = m_Device->createGraphicsPipeline(
                description, m_EnvironmentBrdfFramebuffer->getFramebufferInfo());
            if (!m_EnvironmentBrdfPipeline)
                return false;
        }

        commandList->beginMarker("Environment BRDF");
        DrawFullscreen(commandList, m_EnvironmentBrdfPipeline, m_EnvironmentBrdfFramebuffer,
            nullptr, m_EnvironmentBrdfTextureSize);
        commandList->endMarker();
        return true;
    }
}
