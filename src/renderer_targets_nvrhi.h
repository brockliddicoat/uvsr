#pragma once

#include "lighting_surface_nvrhi.h"

#include <DirectXMath.h>
#include <nvrhi/nvrhi.h>

namespace uvsr
{
    // direct ownership of the fixed planar renderer surfaces. framebuffers
    // are immutable because uvsr has one planar view and never retargets these
    // attachments to cubemap slices or array layers.
    class RenderTargets final
    {
    public:
        nvrhi::TextureHandle Depth;
        nvrhi::TextureHandle GBufferDiffuse;
        nvrhi::TextureHandle GBufferSpecular;
        nvrhi::TextureHandle GBufferNormals;
        nvrhi::TextureHandle GBufferEmissive;

        nvrhi::TextureHandle HdrColor;
        nvrhi::TextureHandle LdrColor;
        nvrhi::TextureHandle MaterialIDs;
        nvrhi::TextureHandle MaterialIDDepth;
        nvrhi::TextureHandle MaterialAmbientOcclusion;

        nvrhi::FramebufferHandle GBufferFramebuffer;
        nvrhi::FramebufferHandle HdrFramebuffer;
        nvrhi::FramebufferHandle LdrFramebuffer;
        nvrhi::FramebufferHandle MaterialIDFramebuffer;
        nvrhi::HeapHandle Heap;
        bool RasterLightingEnabled = false;

        [[nodiscard]] bool Init(
            nvrhi::IDevice* device,
            DirectX::XMUINT2 size,
            DirectX::XMUINT2 presentationSize,
            bool useReverseProjection,
            bool enableRasterLighting);

        [[nodiscard]] bool IsUpdateRequired(
            DirectX::XMUINT2 size,
            DirectX::XMUINT2 presentationSize,
            bool enableRasterLighting) const noexcept;

        [[nodiscard]] bool EnsureMaterialPickingTargets(nvrhi::IDevice* device);

        [[nodiscard]] DirectX::XMUINT2 GetSize() const noexcept
        {
            return m_Size;
        }
        [[nodiscard]] DirectX::XMUINT2 GetPresentationSize() const noexcept
        {
            return m_PresentationSize;
        }
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_Valid;
        }

        [[nodiscard]] LightingSurfaceView GetRasterSurface() const noexcept
        {
            return {
                Depth,
                GBufferDiffuse,
                GBufferSpecular,
                GBufferNormals,
                GBufferEmissive,
                MaterialAmbientOcclusion
            };
        }
        void Clear(nvrhi::ICommandList* commandList) const;

    private:
#if defined(UVSR_RENDER_TARGETS_TEST_FAILURES)
        friend struct RenderTargetsTestAccess;
#endif
        DirectX::XMUINT2 m_Size{};
        DirectX::XMUINT2 m_PresentationSize{};
        bool m_UseReverseProjection = false;
        bool m_Valid = false;
    };

#if defined(UVSR_RENDER_TARGETS_TEST_FAILURES)
    enum class RendererTargetTestOperation { Texture, Heap, Bind, Framebuffer };
    void SetRendererTargetTestFailure(RendererTargetTestOperation operation, unsigned occurrence) noexcept;
    [[nodiscard]] bool WasRendererTargetTestFailureReached() noexcept;
#endif
}
