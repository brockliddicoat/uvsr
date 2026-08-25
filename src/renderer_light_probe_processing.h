#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace uvsr
{
    class RendererCommonPasses;
    class RendererShaderFactory;

    struct RendererLightProbeInitializationContract
    {
        bool device = false;
        bool shaderFactory = false;
        bool commonPasses = false;
        bool geometryShader = false;
        bool mipPixelShader = false;
        bool specularPixelShader = false;
        bool environmentBrdfPixelShader = false;
        bool bindingLayout = false;
        bool constantBuffer = false;
        bool intermediateTexture = false;
        bool environmentBrdfTexture = false;
        bool environmentBrdfFramebuffer = false;
        bool environmentBrdfPipeline = false;

        [[nodiscard]] constexpr bool IsComplete() const noexcept
        {
            return device && shaderFactory && commonPasses &&
                geometryShader && mipPixelShader && specularPixelShader &&
                environmentBrdfPixelShader && bindingLayout &&
                constantBuffer && intermediateTexture &&
                environmentBrdfTexture && environmentBrdfFramebuffer &&
                environmentBrdfPipeline;
        }
    };

    class RendererLightProbeProcessing final
    {
    public:
        RendererLightProbeProcessing(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>& shaderFactory,
            const std::shared_ptr<RendererCommonPasses>& commonPasses,
            std::uint32_t intermediateTextureSize,
            nvrhi::Format intermediateTextureFormat);

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_Initialization.IsComplete();
        }

        [[nodiscard]] bool BlitCubemap(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* input,
            std::uint32_t inputBaseArraySlice,
            std::uint32_t inputMipLevel,
            nvrhi::ITexture* output,
            std::uint32_t outputBaseArraySlice,
            std::uint32_t outputMipLevel);

        [[nodiscard]] bool GenerateCubemapMips(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* cubemap,
            std::uint32_t baseArraySlice,
            std::uint32_t sourceMipLevel,
            std::uint32_t levelsToGenerate);

        [[nodiscard]] bool RenderSpecularMap(
            nvrhi::ICommandList* commandList,
            float roughness,
            nvrhi::ITexture* input,
            nvrhi::TextureSubresourceSet inputSubresources,
            nvrhi::ITexture* output,
            std::uint32_t outputBaseArraySlice,
            std::uint32_t outputMipLevel);

        [[nodiscard]] bool RenderEnvironmentBrdfTexture(
            nvrhi::ICommandList* commandList);

        [[nodiscard]] nvrhi::ITexture* GetEnvironmentBrdfTexture() const
            noexcept
        {
            return m_EnvironmentBrdfTexture.Get();
        }

        void ResetCaches();

    private:
        enum class PipelineKind : std::uint8_t
        {
            Blit,
            Specular
        };

        struct TextureSubresourcesEntry
        {
            nvrhi::TextureHandle texture;
            nvrhi::TextureSubresourceSet subresources;
            nvrhi::FramebufferHandle framebuffer;
            nvrhi::BindingSetHandle bindingSet;
        };

        struct PipelineEntry
        {
            PipelineKind kind = PipelineKind::Blit;
            nvrhi::FramebufferInfo framebufferInfo;
            nvrhi::GraphicsPipelineHandle pipeline;
        };

        [[nodiscard]] static bool IsCubeTexture(
            const nvrhi::TextureDesc& description) noexcept;
        [[nodiscard]] static bool HasCubeSubresources(
            const nvrhi::TextureDesc& description,
            std::uint32_t baseArraySlice,
            std::uint32_t mipLevel) noexcept;
        [[nodiscard]] nvrhi::FramebufferHandle GetFramebuffer(
            nvrhi::ITexture* texture,
            nvrhi::TextureSubresourceSet subresources);
        [[nodiscard]] nvrhi::BindingSetHandle GetBindingSet(
            nvrhi::ITexture* texture,
            nvrhi::TextureSubresourceSet subresources);
        [[nodiscard]] nvrhi::GraphicsPipelineHandle GetPipeline(
            PipelineKind kind,
            const nvrhi::FramebufferInfo& framebufferInfo);

        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<RendererCommonPasses> m_CommonPasses;
        nvrhi::ShaderHandle m_GeometryShader;
        nvrhi::ShaderHandle m_MipPixelShader;
        nvrhi::ShaderHandle m_SpecularPixelShader;
        nvrhi::ShaderHandle m_EnvironmentBrdfPixelShader;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::TextureHandle m_IntermediateTexture;
        nvrhi::TextureHandle m_EnvironmentBrdfTexture;
        nvrhi::FramebufferHandle m_EnvironmentBrdfFramebuffer;
        nvrhi::GraphicsPipelineHandle m_EnvironmentBrdfPipeline;
        std::uint32_t m_IntermediateTextureSize = 0u;
        std::uint32_t m_EnvironmentBrdfTextureSize = 64u;
        std::vector<TextureSubresourcesEntry> m_TextureSubresourceCache;
        std::vector<PipelineEntry> m_PipelineCache;
        RendererLightProbeInitializationContract m_Initialization;
    };
}
