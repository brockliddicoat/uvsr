#include "agx_tone_mapping_pass_nvrhi.h"
#include "color_lut.h"
#include "color_lut_asset_path.h"
#include "windows_path_text.h"
#include "file_bytes.h"
#include "settings_snapshot_storage.h"
#include "renderer_common_passes_nvrhi.h"
#include "renderer_shader_factory_nvrhi.h"

#include "renderer_view_nvrhi.h"
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

        SettingsSnapshotError LutOpenError(const WindowsPath& path,
            SettingsSnapshotErrorCode code = SettingsSnapshotErrorCode::InvalidInput, uint32_t nativeCode = 0) noexcept
        {
            WindowsPathText text;
            WindowsPathTextResult encoded;
            if (!text.Assign(path.Data(), path.Size(), WindowsPathTextForm::Native,
                    WindowsPathTextEncoding::Utf8, encoded))
                return {encoded.error == WindowsPathTextError::Allocation ? SettingsSnapshotErrorCode::OutOfMemory :
                    SettingsSnapshotErrorCode::Path, encoded.error == WindowsPathTextError::Allocation ? nativeCode : encoded.nativeCode, 0,
                    "Cannot encode film LUT path", {}};
            return ComposeSettingsSnapshotError({"Cannot open film LUT: ", {text.Data(), text.Size()}}, code, nativeCode);
        }
    }

    bool LoadColorLutResource(nvrhi::IDevice* device, const wchar_t* directory, ToneMappingLut lut,
        ColorLutResource& result, SettingsSnapshotError& error)
    {
        ColorLutAssetPath asset;
        SettingsSnapshotError pathError;
        if (!asset.Prepare(directory, lut, pathError))
        {
            error = std::move(pathError);
            return false;
        }
        const WindowsPath& path = asset.Path();
        FileBytes bytes;
        FileReadResult readError;
        if (!device)
        {
            if (error.MessageView().empty()) error = LutOpenError(path);
            return false;
        }
        if (!ReadFileBytes(path.Data(), uint64_t(PTRDIFF_MAX) - 1, bytes, readError))
        {
            if (readError.error == FileReadError::OutOfMemory)
                error = {SettingsSnapshotErrorCode::OutOfMemory, readError.systemCode, 0, "Cannot allocate film LUT file", {}};
            else if (readError.error == FileReadError::TooLarge)
                error = {SettingsSnapshotErrorCode::Capacity, readError.systemCode, 0, "Film LUT input is too large", {}};
            else
                error = LutOpenError(path, SettingsSnapshotErrorCode::Path, readError.systemCode);
            return false;
        }
        std::string_view text(bytes.Data(), bytes.Size());
        // preserve the previous Windows text stream's end-of-file marker.
        text = text.substr(0, text.find('\x1a'));
        ColorLutData data;
        if (!ReadColorLut(text, data, error)) return false;
        nvrhi::TextureDesc desc;
        desc.width = desc.height = desc.depth = data.Size();
        desc.dimension = nvrhi::TextureDimension::Texture3D;
        desc.format = nvrhi::Format::RGBA32_FLOAT;
        desc.initialState = nvrhi::ResourceStates::Common;
        desc.debugName = asset.DebugName();
        ColorLutResource candidate;
        candidate.texture = device->createTexture(desc);
        const auto commandList = device->createCommandList();
        if (!candidate.texture || !commandList)
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Cannot create film LUT GPU resources", {}};
            return false;
        }
        commandList->open();
        commandList->beginTrackingTextureState(candidate.texture, nvrhi::AllSubresources,
            nvrhi::ResourceStates::Common);
        commandList->writeTexture(candidate.texture, 0, 0, data.Values().data,
            size_t(data.Size()) * sizeof(ColorLutValue),
            size_t(data.Size()) * data.Size() * sizeof(ColorLutValue));
        commandList->setPermanentTextureState(candidate.texture, nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();
        commandList->close();
        device->executeCommandList(commandList);
        candidate.size = data.Size();
        candidate.domainMin = data.DomainMin();
        candidate.domainMax = data.DomainMax();
        result = std::move(candidate);
        error = {};
        return true;
    }

    AgxToneMappingPass::AgxToneMappingPass(
        nvrhi::IDevice* device,
        RendererShaderFactory* shaderFactory,
        RendererCommonPasses* commonPasses,
        nvrhi::FramebufferHandle framebuffer)
        : m_Device(device), m_CommonPasses(commonPasses), m_Framebuffer(std::move(framebuffer))
    {
        if (!device || !shaderFactory || !commonPasses || !m_Framebuffer ||
            !commonPasses->FullscreenVertexShader())
            return;
        m_OutputPixelShader = shaderFactory->CreateShader(
            "uvsr/display_output_ps.hlsl", "main", {}, nvrhi::ShaderType::Pixel);
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
            const shader_blob::Constant macros[] = {
                { "UVSR_UNITY_EXPOSURE", automaticExposure ? "0" : "1" },
                { "UVSR_USE_LUT", useLut ? "1" : "0" }
            };
            variant.shader = shaderFactory->CreateShader(
                "uvsr/agx_tonemapping_ps.hlsl", "main", macros, nvrhi::ShaderType::Pixel);
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
        const RendererView& frameView,
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
        if (!frameView.valid) return false;
        commandList->beginMarker("AgX Tone Mapping");
        
        {
            const auto* view = &frameView;
            nvrhi::GraphicsState state;
            state.pipeline = variant.pipeline;
            state.framebuffer = m_Framebuffer;
            state.bindings = { variant.bindingSet };
            state.viewport = RendererViewportNvrhi(*view);
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
        const RendererView& frameView,
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

        if (!frameView.valid)
            return false;

        commandList->beginMarker("Display Transfer and Dither");
        
        {
            const RendererView* view = &frameView;
            nvrhi::GraphicsState state;
            state.pipeline = m_OutputPipeline;
            state.framebuffer = framebuffer;
            state.bindings = { m_OutputBindingSet };
            state.viewport = RendererViewportNvrhi(*view);
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
