#include "agx_tone_mapping_pass.h"
#include "color_lut.h"
#include "renderer_common_passes.h"
#include "renderer_shader_factory.h"

#include <donut/engine/View.h>
#include <fstream>
#include <utility>
#include <vector>

namespace uvsr
{
    namespace
    {
        struct alignas(16) ToneMappingConstants
        {
            std::array<float, 4> exposureContrastSaturationWarmth;
            std::array<float, 4> tintSlopePowerLutSize;
            std::array<float, 4> domainMin;
            std::array<float, 4> domainMax;
        };
        static_assert(sizeof(ToneMappingConstants) == 64);
    }

    bool LoadColorLutResource(nvrhi::IDevice* device, const std::filesystem::path& path,
        ColorLutResource& result, std::string& error)
    {
        std::ifstream stream(path);
        ColorLutData data;
        if (!device || !stream || !ReadColorLut(stream, data, error))
        {
            if (error.empty()) error = "Cannot open film LUT: " + path.u8string();
            return false;
        }
        nvrhi::TextureDesc desc;
        desc.width = desc.height = desc.depth = data.size;
        desc.dimension = nvrhi::TextureDimension::Texture3D;
        desc.format = nvrhi::Format::RGBA32_FLOAT;
        desc.initialState = nvrhi::ResourceStates::Common;
        desc.debugName = path.stem().string();
        ColorLutResource candidate;
        candidate.texture = device->createTexture(desc);
        const auto commandList = device->createCommandList();
        if (!candidate.texture || !commandList)
        {
            error = "Cannot create film LUT GPU resources";
            return false;
        }
        commandList->open();
        commandList->beginTrackingTextureState(candidate.texture, nvrhi::AllSubresources,
            nvrhi::ResourceStates::Common);
        commandList->writeTexture(candidate.texture, 0, 0, data.values.data(),
            size_t(data.size) * sizeof(data.values[0]),
            size_t(data.size) * data.size * sizeof(data.values[0]));
        commandList->setPermanentTextureState(candidate.texture, nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();
        commandList->close();
        device->executeCommandList(commandList);
        candidate.size = data.size;
        candidate.domainMin = data.domainMin;
        candidate.domainMax = data.domainMax;
        result = std::move(candidate);
        error.clear();
        return true;
    }

    AgxToneMappingPass::AgxToneMappingPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
        const std::shared_ptr<RendererCommonPasses>& commonPasses,
        nvrhi::FramebufferHandle framebuffer)
        : m_Device(device), m_CommonPasses(commonPasses), m_Framebuffer(std::move(framebuffer))
    {
        if (!device || !shaderFactory || !commonPasses || !m_Framebuffer ||
            !commonPasses->FullscreenVertexShader())
            return;
        m_OutputPixelShader = shaderFactory->CreateShader(
            "uvsr/display_output_ps.hlsl", "main", nullptr, nvrhi::ShaderType::Pixel);
        nvrhi::BindingLayoutDesc layout;
        layout.visibility = nvrhi::ShaderType::Pixel;
        layout.bindings = { nvrhi::BindingLayoutItem::Texture_SRV(0) };
        m_TextureOnlyBindingLayout = device->createBindingLayout(layout);
        nvrhi::SamplerDesc sampler;
        sampler.setAllFilters(true);
        sampler.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
        m_LutSampler = device->createSampler(sampler);
        for (unsigned index = 0; index < m_Variants.size(); ++index)
        {
            const bool automaticExposure = (index & 1) != 0;
            const bool useLut = (index & 2) != 0;
            auto& variant = m_Variants[index];
            const std::vector<RendererShaderMacro> macros = {
                { "UVSR_UNITY_EXPOSURE", automaticExposure ? "0" : "1" },
                { "UVSR_USE_LUT", useLut ? "1" : "0" }
            };
            variant.shader = shaderFactory->CreateShader(
                "uvsr/agx_tonemapping_ps.hlsl", "main", &macros, nvrhi::ShaderType::Pixel);
            layout.bindings = {
                nvrhi::BindingLayoutItem::PushConstants(0, sizeof(ToneMappingConstants)),
                nvrhi::BindingLayoutItem::Texture_SRV(0)
            };
            if (automaticExposure)
                layout.bindings.push_back(nvrhi::BindingLayoutItem::TypedBuffer_SRV(1));
            if (useLut)
            {
                layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(2));
                layout.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(0));
            }
            variant.layout = device->createBindingLayout(layout);
            if (!variant.shader || !variant.layout || (useLut && !m_LutSampler))
                return;
            nvrhi::GraphicsPipelineDesc pipeline;
            pipeline.primType = nvrhi::PrimitiveType::TriangleStrip;
            pipeline.VS = commonPasses->FullscreenVertexShader();
            pipeline.PS = variant.shader;
            pipeline.bindingLayouts = { variant.layout };
            pipeline.renderState.rasterState.setCullNone();
            pipeline.renderState.depthStencilState.depthTestEnable = false;
            pipeline.renderState.depthStencilState.stencilEnable = false;
            variant.pipeline = device->createGraphicsPipeline(pipeline, m_Framebuffer->getFramebufferInfo());
        }
    }

    bool AgxToneMappingPass::Render(
        nvrhi::ICommandList* commandList,
        const donut::engine::ICompositeView& compositeView,
        nvrhi::ITexture* sourceTexture,
        nvrhi::IBuffer* exposureBuffer,
        const ToneMappingSettings& settings,
        const ColorLutResource& lut)
    {
        const bool useLut = settings.enabled && settings.lut != ToneMappingLut::None;
        if (!commandList || !sourceTexture || !IsValid() ||
            (useLut && (!lut.texture || lut.size < 2)))
            return false;
        auto& variant = m_Variants[(exposureBuffer ? 1 : 0) | (useLut ? 2 : 0)];
        nvrhi::ITexture* lutTexture = useLut ? lut.texture.Get() : nullptr;
        if (!variant.bindingSet || variant.source != sourceTexture ||
            variant.exposure != exposureBuffer || variant.lut != lutTexture)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::PushConstants(0, sizeof(ToneMappingConstants)),
                nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture)
            };
            if (exposureBuffer)
                bindings.bindings.push_back(nvrhi::BindingSetItem::TypedBuffer_SRV(1, exposureBuffer));
            if (useLut)
            {
                bindings.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(2, lutTexture));
                bindings.bindings.push_back(nvrhi::BindingSetItem::Sampler(0, m_LutSampler));
            }
            variant.bindingSet = m_Device->createBindingSet(bindings, variant.layout);
            if (!variant.bindingSet) return false;
            variant.source = sourceTexture;
            variant.exposure = exposureBuffer;
            variant.lut = lutTexture;
        }
        const ToneMappingSettings& grade = settings.enabled ? settings : DefaultToneMappingSettings;
        const ToneMappingConstants constants{
            { grade.exposure, grade.contrast, grade.saturation, grade.warmth },
            { grade.tint, grade.slope, grade.power, float(lut.size) },
            { lut.domainMin[0], lut.domainMin[1], lut.domainMin[2], 0.f },
            { lut.domainMax[0], lut.domainMax[1], lut.domainMax[2], 0.f }
        };
        const auto viewCount = compositeView.GetNumChildViews(donut::engine::ViewType::PLANAR);
        if (!viewCount) return false;
        commandList->beginMarker("AgX Tone Mapping");
        for (uint32_t index = 0; index < viewCount; ++index)
        {
            const auto* view = compositeView.GetChildView(donut::engine::ViewType::PLANAR, index);
            if (!view) { commandList->endMarker(); return false; }
            nvrhi::GraphicsState state;
            state.pipeline = variant.pipeline;
            state.framebuffer = m_Framebuffer;
            state.bindings = { variant.bindingSet };
            state.viewport = view->GetViewportState();
            commandList->setGraphicsState(state);
            commandList->setPushConstants(&constants, sizeof(constants));
            nvrhi::DrawArguments arguments;
            arguments.instanceCount = 1;
            arguments.vertexCount = 4;
            commandList->draw(arguments);
        }
        commandList->endMarker();
        return true;
    }

    bool AgxToneMappingPass::RenderOutput(
        nvrhi::ICommandList* commandList,
        const donut::engine::ICompositeView& compositeView,
        nvrhi::IFramebuffer* framebuffer,
        nvrhi::ITexture* sourceTexture)
    {
        if (!commandList || !framebuffer || !sourceTexture ||
            !m_Device || !m_CommonPasses || !m_OutputPixelShader ||
            !m_TextureOnlyBindingLayout ||
            !m_CommonPasses->FullscreenVertexShader())
        {
            return false;
        }

        const nvrhi::FramebufferInfoEx& framebufferInfo =
            framebuffer->getFramebufferInfo();
        if (framebufferInfo.colorFormats.empty())
            return false;
        const nvrhi::Format framebufferFormat =
            framebufferInfo.colorFormats[0];
        if (!m_OutputPipeline ||
            framebufferFormat != m_OutputFramebufferFormat)
        {
            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
            pipelineDesc.VS = m_CommonPasses->FullscreenVertexShader();
            pipelineDesc.PS = m_OutputPixelShader;
            pipelineDesc.bindingLayouts = { m_TextureOnlyBindingLayout };
            pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
            pipelineDesc.renderState.depthStencilState.stencilEnable = false;
            m_OutputPipeline = m_Device->createGraphicsPipeline(
                pipelineDesc,
                framebufferInfo);
            m_OutputFramebufferFormat = framebufferFormat;
        }
        if (!m_OutputPipeline)
            return false;

        if (!m_OutputBindingSet || m_BoundOutputSource != sourceTexture)
        {
            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture)
            };
            m_OutputBindingSet = m_Device->createBindingSet(
                bindingSetDesc,
                m_TextureOnlyBindingLayout);
            if (m_OutputBindingSet)
                m_BoundOutputSource = sourceTexture;
        }
        if (!m_OutputBindingSet)
            return false;

        const std::uint32_t viewCount = compositeView.GetNumChildViews(
            donut::engine::ViewType::PLANAR);
        if (viewCount == 0u)
            return false;

        commandList->beginMarker("Display Transfer and Dither");
        for (std::uint32_t viewIndex = 0; viewIndex < viewCount; ++viewIndex)
        {
            const donut::engine::IView* view = compositeView.GetChildView(
                donut::engine::ViewType::PLANAR,
                viewIndex);
            if (!view)
            {
                commandList->endMarker();
                return false;
            }
            nvrhi::GraphicsState state;
            state.pipeline = m_OutputPipeline;
            state.framebuffer = framebuffer;
            state.bindings = { m_OutputBindingSet };
            state.viewport = view->GetViewportState();
            commandList->setGraphicsState(state);

            nvrhi::DrawArguments arguments;
            arguments.instanceCount = 1;
            arguments.vertexCount = 4;
            commandList->draw(arguments);
        }
        commandList->endMarker();
        return true;
    }
}
