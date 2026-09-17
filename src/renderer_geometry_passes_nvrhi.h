#pragma once

#include "renderer_geometry_passes.h"
#include "renderer_gpu_contract.h"
#include "renderer_material_contract.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace uvsr
{
    class RendererShaderFactory;

    struct RendererGeometryMaterial
    {
        static constexpr std::size_t TextureCount = 7u;

        const void* cacheKey = nullptr;
        nvrhi::IBuffer* constants = nullptr;
        nvrhi::BufferRange constantRange;
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
            RendererShaderFactory* shaderFactory,
            nvrhi::ITexture* fallbackTexture,
            RendererGeometryPassDescription description);

        ~RendererGeometryPass() noexcept;
        RendererGeometryPass(const RendererGeometryPass&) = delete;
        RendererGeometryPass& operator=(const RendererGeometryPass&) = delete;
        RendererGeometryPass(RendererGeometryPass&&) = delete;
        RendererGeometryPass& operator=(RendererGeometryPass&&) = delete;

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
        void SetMaterialRevision(uint64_t revision);

        [[nodiscard]] std::uint64_t GetSubmittedTriangles() const noexcept
        {
            return m_SubmittedTriangles;
        }

    private:
        enum class MaterialResult : uint8_t { Applied, Skipped, Failed };

        class BindingCache final
        {
        public:
            BindingCache() noexcept = default;
            ~BindingCache() noexcept;
            BindingCache(const BindingCache&) = delete;
            BindingCache& operator=(const BindingCache&) = delete;
            [[nodiscard]] nvrhi::BindingSetHandle Find(const void* key) const noexcept;
            [[nodiscard]] bool Insert(const void* key, nvrhi::BindingSetHandle binding) noexcept;
            void Clear() noexcept;
        private:
            struct Entry
            {
                const void* key = nullptr;
                nvrhi::BindingSetHandle binding;
            };
            [[nodiscard]] static std::size_t FindSlot(const Entry* entries,
                std::size_t capacity, const void* key) noexcept;
            Entry* m_Entries = nullptr;
            std::size_t m_Count = 0;
            std::size_t m_Capacity = 0;
        };

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
        [[nodiscard]] MaterialResult ApplyMaterial(
            const RendererGeometryMaterial& material,
            nvrhi::RasterCullMode cullMode);
        [[nodiscard]] bool Flush();

        nvrhi::DeviceHandle m_Device;
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
        BindingCache m_MaterialBindings;
        BindingCache m_InputBindings;
        PipelineEntry* m_Pipelines = nullptr;
        std::size_t m_PipelineCount = 0;
        std::size_t m_PipelineCapacity = 0;
        RendererGeometryInitializationContract m_Initialization;
        uint64_t m_MaterialRevision = 0;

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
