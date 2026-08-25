#pragma once

#include "renderer_gpu_contract.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace uvsr
{
    class RendererShaderFactory;

    enum class RendererGeometryOutput : std::uint8_t
    {
        Pbr,
        MaterialId
    };

    enum class RendererMaterialDomain : std::uint8_t
    {
        Opaque,
        AlphaTested,
        AlphaBlended,
        Transmissive,
        TransmissiveAlphaTested,
        TransmissiveAlphaBlended,
        Count
    };

    enum class RendererMaterialRasterClass : std::uint8_t
    {
        Opaque,
        AlphaTested,
        Rejected
    };

    [[nodiscard]] constexpr RendererMaterialRasterClass
        ClassifyRendererMaterialDomain(RendererMaterialDomain domain) noexcept
    {
        switch (domain)
        {
        case RendererMaterialDomain::AlphaTested:
            return RendererMaterialRasterClass::AlphaTested;
        case RendererMaterialDomain::Opaque:
        case RendererMaterialDomain::AlphaBlended:
        case RendererMaterialDomain::Transmissive:
        case RendererMaterialDomain::TransmissiveAlphaTested:
        case RendererMaterialDomain::TransmissiveAlphaBlended:
            return RendererMaterialRasterClass::Opaque;
        default:
            return RendererMaterialRasterClass::Rejected;
        }
    }

    struct RendererGeometryInitializationContract
    {
        bool device = false;
        bool shaderFactory = false;
        bool fallbackTexture = false;
        bool vertexShader = false;
        bool pixelShader = false;
        bool alphaTestedPixelShader = false;
        bool materialLayout = false;
        bool viewLayout = false;
        bool inputLayout = false;
        bool viewConstantBuffer = false;
        bool materialSampler = false;
        bool viewBindingSet = false;

        [[nodiscard]] constexpr bool IsComplete() const noexcept
        {
            return device && shaderFactory && fallbackTexture &&
                vertexShader && pixelShader && alphaTestedPixelShader &&
                materialLayout && viewLayout && inputLayout &&
                viewConstantBuffer && materialSampler && viewBindingSet;
        }
    };

    struct RendererGeometryPassDescription
    {
        RendererGeometryOutput output = RendererGeometryOutput::Pbr;
        bool enableMotionVectors = false;
        bool whiteWorld = false;
        bool enableDepthWrite = true;
        bool trackBindingLiveness = true;
    };

    struct RendererGeometryMaterial
    {
        static constexpr std::size_t TextureCount = 7u;

        const void* cacheKey = nullptr;
        nvrhi::IBuffer* constants = nullptr;
        std::array<nvrhi::ITexture*, TextureCount> textures{};
        RendererMaterialDomain domain = RendererMaterialDomain::Count;
    };

    struct RendererGeometryBuffers
    {
        const void* cacheKey = nullptr;
        nvrhi::IBuffer* indexBuffer = nullptr;
        nvrhi::IBuffer* vertexBuffer = nullptr;
        nvrhi::IBuffer* instanceBuffer = nullptr;
        std::uint32_t positionOffset = 0u;
        std::uint32_t previousPositionOffset = 0u;
        std::uint32_t textureCoordinateOffset = 0u;
        std::uint32_t normalOffset = 0u;
        std::uint32_t tangentOffset = 0u;
    };

    struct RendererGeometryDraw
    {
        const RendererGeometryMaterial* material = nullptr;
        const RendererGeometryBuffers* buffers = nullptr;
        nvrhi::RasterCullMode cullMode = nvrhi::RasterCullMode::Back;
        std::uint32_t indexCount = 0u;
        std::uint32_t instanceCount = 1u;
        std::uint32_t startIndexLocation = 0u;
        std::uint32_t startVertexLocation = 0u;
        std::uint32_t startInstanceLocation = 0u;
    };

    [[nodiscard]] constexpr bool CanMergeRendererGeometryDraws(
        const nvrhi::DrawArguments& pending,
        const RendererGeometryDraw& next) noexcept
    {
        return pending.instanceCount > 0u &&
            pending.vertexCount == next.indexCount &&
            pending.startIndexLocation == next.startIndexLocation &&
            pending.startVertexLocation == next.startVertexLocation &&
            std::uint64_t(pending.startInstanceLocation) +
                    pending.instanceCount == next.startInstanceLocation &&
            std::uint64_t(pending.instanceCount) + next.instanceCount <=
                std::numeric_limits<std::uint32_t>::max();
    }

    struct RendererGeometryView
    {
        GBufferFillConstants constants{};
        nvrhi::IFramebuffer* framebuffer = nullptr;
        nvrhi::ViewportState viewport;
        nvrhi::VariableRateShadingState shadingRate;
        bool frontCounterClockwise = false;
        bool reverseDepth = true;
    };

    // A direct NVRHI G-buffer/material-ID encoder. Scene traversal remains at
    // the application boundary: callers translate each retained scene draw
    // into the narrow contracts above, then submit it between Begin/EndView.
    class RendererGeometryPass final
    {
    public:
        RendererGeometryPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<RendererShaderFactory>& shaderFactory,
            nvrhi::ITexture* fallbackTexture,
            RendererGeometryPassDescription description);

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_Initialization.IsComplete();
        }

        [[nodiscard]] bool BeginView(
            nvrhi::ICommandList* commandList,
            const RendererGeometryView& view);

        [[nodiscard]] bool Submit(const RendererGeometryDraw& draw);

        [[nodiscard]] bool EndView();

        void ResetBindingCache();

        [[nodiscard]] std::uint64_t GetSubmittedTriangles() const noexcept
        {
            return m_SubmittedTriangles;
        }

    private:
        struct PipelineKey
        {
            nvrhi::RasterCullMode cullMode = nvrhi::RasterCullMode::Back;
            bool alphaTested = false;
            bool frontCounterClockwise = false;
            bool reverseDepth = true;

            [[nodiscard]] bool operator==(const PipelineKey& other) const
                noexcept
            {
                return cullMode == other.cullMode &&
                    alphaTested == other.alphaTested &&
                    frontCounterClockwise == other.frontCounterClockwise &&
                    reverseDepth == other.reverseDepth;
            }
        };

        struct PipelineEntry
        {
            PipelineKey key;
            nvrhi::FramebufferInfo framebufferInfo;
            nvrhi::GraphicsPipelineHandle pipeline;
        };

        [[nodiscard]] nvrhi::BindingSetHandle GetMaterialBindingSet(
            const RendererGeometryMaterial& material);
        [[nodiscard]] nvrhi::BindingSetHandle GetInputBindingSet(
            const RendererGeometryBuffers& buffers);
        [[nodiscard]] nvrhi::GraphicsPipelineHandle GetPipeline(
            PipelineKey key,
            const nvrhi::FramebufferInfo& framebufferInfo);
        [[nodiscard]] bool ApplyBuffers(
            const RendererGeometryBuffers& buffers);
        [[nodiscard]] bool ApplyMaterial(
            const RendererGeometryMaterial& material,
            nvrhi::RasterCullMode cullMode);
        [[nodiscard]] bool Flush();

        nvrhi::DeviceHandle m_Device;
        RendererGeometryPassDescription m_Description;
        nvrhi::TextureHandle m_FallbackTexture;
        nvrhi::ShaderHandle m_VertexShader;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::ShaderHandle m_AlphaTestedPixelShader;
        nvrhi::BindingLayoutHandle m_MaterialLayout;
        nvrhi::BindingLayoutHandle m_ViewLayout;
        nvrhi::BindingLayoutHandle m_InputLayout;
        nvrhi::BufferHandle m_ViewConstantBuffer;
        nvrhi::SamplerHandle m_MaterialSampler;
        nvrhi::BindingSetHandle m_ViewBindingSet;
        std::unordered_map<const void*, nvrhi::BindingSetHandle>
            m_MaterialBindings;
        std::unordered_map<const void*, nvrhi::BindingSetHandle>
            m_InputBindings;
        std::vector<PipelineEntry> m_Pipelines;
        RendererGeometryInitializationContract m_Initialization;

        nvrhi::ICommandList* m_CommandList = nullptr;
        nvrhi::GraphicsState m_GraphicsState;
        nvrhi::DrawArguments m_PendingDraw;
        GBufferPushConstants m_PushConstants{};
        const void* m_CurrentMaterial = nullptr;
        const void* m_CurrentBuffers = nullptr;
        nvrhi::RasterCullMode m_CurrentCullMode =
            nvrhi::RasterCullMode::Back;
        bool m_FrontCounterClockwise = false;
        bool m_ReverseDepth = true;
        bool m_StateValid = false;
        bool m_CurrentDrawEnabled = false;
        std::uint64_t m_SubmittedTriangles = 0u;
    };
}
