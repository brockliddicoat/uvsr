#pragma once

#include <nvrhi/nvrhi.h>
#include <memory>
#include <array>
#include <vector>

namespace donut::engine { class IView; class ShaderFactory; }

namespace uvsr
{
    // one frame's conservative view-depth bounds, shared by screen-space consumers.
    class HierarchicalDepthPass
    {
    public:
        HierarchicalDepthPass(nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory);
        nvrhi::ITexture* Render(nvrhi::ICommandList* commandList,
            nvrhi::ITexture* depth, const donut::engine::IView& view);

        float GetMilliseconds() const { return m_Milliseconds; }
        uint64_t GetTextureBytes() const { return m_TextureBytes; }
        bool HasTiming() const { return m_HasTiming; }

    private:
        std::array<nvrhi::TimerQueryHandle, 4> m_Queries;
        std::array<bool, 4> m_Pending{};
        uint32_t m_Frame = 0u;
        float m_Milliseconds = 0.f;
        uint64_t m_TextureBytes = 0u;
        bool m_HasTiming = false;
        nvrhi::DeviceHandle m_Device;
        nvrhi::TextureHandle m_Bounds;
        nvrhi::TextureHandle m_Source;
        nvrhi::BufferHandle m_Constants;
        nvrhi::BindingLayoutHandle m_Layout;
        nvrhi::ComputePipelineHandle m_Pipeline;
        std::vector<nvrhi::BindingSetHandle> m_Bindings;
    };
}
