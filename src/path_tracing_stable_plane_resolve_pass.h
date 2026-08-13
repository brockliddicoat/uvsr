#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>

namespace donut::engine
{
    class ShaderFactory;
}

namespace uvsr
{
    struct PathTracingStablePlaneResolveInputs
    {
        nvrhi::ITexture* rawMean = nullptr;
        nvrhi::ITexture* residualMean = nullptr;
        nvrhi::ITexture* diffuseSuffixMean = nullptr;
        nvrhi::ITexture* primaryNormalRoughness = nullptr;
        nvrhi::ITexture* primaryViewZ = nullptr;
        nvrhi::ITexture* output = nullptr;
        uint32_t stablePlaneCount = 1u;
    };

    // A small first-party, spatial-only reconstruction pass. It intentionally
    // does not claim RTXPT Stable Planes parity and has no temporal state.
    class PathTracingStablePlaneResolvePass final
    {
    public:
        PathTracingStablePlaneResolvePass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory);

        [[nodiscard]] bool IsSupported() const;

        [[nodiscard]] bool Render(
            nvrhi::ICommandList* commandList,
            const PathTracingStablePlaneResolveInputs& inputs);

        void ResetBindingCache();

    private:
        [[nodiscard]] bool EnsureBindingSet(
            const PathTracingStablePlaneResolveInputs& inputs);

        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::ShaderHandle m_Shader;
        nvrhi::ComputePipelineHandle m_Pipeline;
        nvrhi::BindingSetHandle m_BindingSet;
        PathTracingStablePlaneResolveInputs m_BoundInputs;
    };
}
