#pragma once

#include <DirectXMath.h>
#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace uvsr
{
    // Direct ownership of the fixed planar renderer surfaces. Framebuffers
    // are immutable because UVSR has one planar view and never retargets these
    // attachments to cubemap slices or array layers.
    class RenderTargets final
    {
    public:
        nvrhi::TextureHandle Depth;
        nvrhi::TextureHandle GBufferDiffuse;
        nvrhi::TextureHandle GBufferSpecular;
        nvrhi::TextureHandle GBufferNormals;
        nvrhi::TextureHandle GBufferEmissive;
        nvrhi::TextureHandle MotionVectors;

        nvrhi::TextureHandle HdrColor;
        nvrhi::TextureHandle ResolvedHdrColor;
        nvrhi::TextureHandle DeferredMsaaColor;
        nvrhi::TextureHandle BaseLighting;
        nvrhi::TextureHandle DirectDiffuseRadiance;
        nvrhi::TextureHandle VisibilityComposite;
        nvrhi::TextureHandle VisibilityDepth;
        nvrhi::TextureHandle VisibilityGBufferDiffuse;
        nvrhi::TextureHandle VisibilityGBufferMaterial;
        nvrhi::TextureHandle VisibilityGBufferNormals;
        nvrhi::TextureHandle VisibilityGBufferEmissive;
        nvrhi::TextureHandle VisibilityMaterialAmbientOcclusion;
        nvrhi::TextureHandle VisibilityMotionVectors;
        nvrhi::TextureHandle PresentationColor;
        nvrhi::TextureHandle LdrColor;
        nvrhi::TextureHandle MaterialIDs;
        nvrhi::TextureHandle MaterialIDDepth;
        nvrhi::TextureHandle MaterialAmbientOcclusion;

        nvrhi::FramebufferHandle GBufferFramebuffer;
        nvrhi::FramebufferHandle HdrFramebuffer;
        nvrhi::FramebufferHandle PresentationFramebuffer;
        nvrhi::FramebufferHandle LdrFramebuffer;
        nvrhi::FramebufferHandle MaterialIDFramebuffer;
        nvrhi::HeapHandle Heap;

        bool VisibilityResourcesEnabled = false;
        bool VisibilitySourceRadianceEnabled = false;
        bool MotionVectorsEnabled = false;

        [[nodiscard]] bool Init(
            nvrhi::IDevice* device,
            DirectX::XMUINT2 size,
            std::uint32_t sampleCount,
            DirectX::XMUINT2 presentationSize,
            std::uint32_t presentationSampleCount,
            bool enableMotionVectors,
            bool useReverseProjection,
            bool enableVisibilityResources,
            bool enableVisibilitySourceRadiance);

        [[nodiscard]] bool IsUpdateRequired(
            DirectX::XMUINT2 size,
            std::uint32_t sampleCount,
            DirectX::XMUINT2 presentationSize,
            std::uint32_t presentationSampleCount,
            bool enableVisibilityResources,
            bool enableVisibilitySourceRadiance,
            bool enableMotionVectors) const noexcept;

        [[nodiscard]] DirectX::XMUINT2 GetSize() const noexcept
        {
            return m_Size;
        }

        [[nodiscard]] std::uint32_t GetSampleCount() const noexcept
        {
            return m_SampleCount;
        }

        [[nodiscard]] DirectX::XMUINT2 GetPresentationSize() const noexcept
        {
            return m_PresentationSize;
        }

        [[nodiscard]] std::uint32_t
            GetPresentationSampleCount() const noexcept
        {
            return m_PresentationSampleCount;
        }

        [[nodiscard]] std::uint32_t
            GetPresentationResolutionScale() const noexcept
        {
            return m_PresentationResolutionScale;
        }

        [[nodiscard]] bool GetUseReverseProjection() const noexcept
        {
            return m_UseReverseProjection;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_Valid;
        }

        void Clear(nvrhi::ICommandList* commandList) const;

    private:
        DirectX::XMUINT2 m_Size{};
        std::uint32_t m_SampleCount = 0u;
        DirectX::XMUINT2 m_PresentationSize{};
        std::uint32_t m_PresentationSampleCount = 0u;
        std::uint32_t m_PresentationResolutionScale = 0u;
        bool m_UseReverseProjection = false;
        bool m_Valid = false;
    };
}
