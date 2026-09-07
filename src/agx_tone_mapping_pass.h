#pragma once

#include <memory>
#include <array>
#include <filesystem>
#include <string>
#include "tone_mapping_settings.h"

#include <nvrhi/nvrhi.h>

namespace donut::engine
{
    class ICompositeView;
}

namespace uvsr
{
    class RendererShaderFactory;
    class RendererCommonPasses;

    struct ColorLutResource
    {
        nvrhi::TextureHandle texture;
        uint32_t size = 0;
        std::array<float, 3> domainMin{};
        std::array<float, 3> domainMax{ 1.f, 1.f, 1.f };
    };

    [[nodiscard]] bool LoadColorLutResource(nvrhi::IDevice* device,
        const std::filesystem::path& path, ColorLutResource& result, std::string& error);

    class AgxToneMappingPass
    {
    public:
        AgxToneMappingPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>& shaderFactory,
            const std::shared_ptr<RendererCommonPasses>& commonPasses,
            nvrhi::FramebufferHandle framebuffer);

        [[nodiscard]] bool IsValid() const noexcept
        {
            if (!m_Device || !m_Framebuffer || !m_OutputPixelShader || !m_TextureOnlyBindingLayout)
                return false;
            for (const auto& variant : m_Variants)
                if (!variant.shader || !variant.layout || !variant.pipeline)
                    return false;
            return true;
        }

        bool Render(
            nvrhi::ICommandList* commandList,
            const donut::engine::ICompositeView& compositeView,
            nvrhi::ITexture* sourceTexture,
            nvrhi::IBuffer* exposureBuffer,
            const ToneMappingSettings& settings,
            const ColorLutResource& lut);

        bool RenderOutput(
            nvrhi::ICommandList* commandList,
            const donut::engine::ICompositeView& compositeView,
            nvrhi::IFramebuffer* framebuffer,
            nvrhi::ITexture* sourceTexture);

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::ShaderHandle m_OutputPixelShader;
        nvrhi::BindingLayoutHandle m_TextureOnlyBindingLayout;
        nvrhi::BindingSetHandle m_OutputBindingSet;
        nvrhi::GraphicsPipelineHandle m_OutputPipeline;
        struct Variant
        {
            nvrhi::ShaderHandle shader;
            nvrhi::BindingLayoutHandle layout;
            nvrhi::GraphicsPipelineHandle pipeline;
            nvrhi::BindingSetHandle bindingSet;
            nvrhi::ITexture* source = nullptr;
            nvrhi::IBuffer* exposure = nullptr;
            nvrhi::ITexture* lut = nullptr;
        };
        std::array<Variant, 4> m_Variants;
        nvrhi::SamplerHandle m_LutSampler;
        nvrhi::ITexture* m_BoundOutputSource = nullptr;
        nvrhi::Format m_OutputFramebufferFormat = nvrhi::Format::UNKNOWN;
        std::shared_ptr<RendererCommonPasses> m_CommonPasses;
        nvrhi::FramebufferHandle m_Framebuffer;
    };
}
