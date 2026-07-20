#pragma once

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class ShaderFactory;
}

namespace uvsr
{
    struct MsaaVisibilityResolveInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* diffuse = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* emissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
        nvrhi::ITexture* motionVectors = nullptr;
    };

    struct MsaaVisibilityResolveOutputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* diffuse = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* emissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
        nvrhi::ITexture* motionVectors = nullptr;
    };

    // Produces a coherent single-surface G-buffer for screen-space visibility.
    // UVSR uses reverse-Z, so the greatest valid raw depth owns the pixel.
    // Every attribute is copied from that one sample; attributes are never
    // averaged across coverage or silhouettes.
    class MsaaVisibilityResolvePass final
    {
    private:
        struct Pipeline
        {
            nvrhi::ShaderHandle shader;
            nvrhi::BindingLayoutHandle bindingLayout;
            nvrhi::ComputePipelineHandle pso;
        };

        nvrhi::DeviceHandle m_Device;
        std::array<Pipeline, 4> m_Pipelines;

    public:
        explicit MsaaVisibilityResolvePass(nvrhi::IDevice* device);

        void Init(
            const std::shared_ptr<donut::engine::ShaderFactory>&
                shaderFactory);

        void Render(
            nvrhi::ICommandList* commandList,
            const MsaaVisibilityResolveInputs& inputs,
            const MsaaVisibilityResolveOutputs& outputs,
            uint32_t sampleCount) const;
    };
}
