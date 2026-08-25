#pragma once

#include "msaa_visibility_resolve_contract.h"

#include <array>
#include <cstdint>
#include <memory>

namespace uvsr
{
    class RendererShaderFactory;

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
        std::shared_ptr<RendererShaderFactory> m_ShaderFactory;
        uint32_t m_PipelinePreparationStep = 0u;
        bool m_PipelinesReady = false;
        bool m_PipelinePreparationFailed = false;

    public:
        explicit MsaaVisibilityResolvePass(nvrhi::IDevice* device);

        void Init(
            const std::shared_ptr<RendererShaderFactory>&
                shaderFactory,
            bool deferPipelineCreation = false);

        [[nodiscard]] bool PreparePipelinesStep();
        [[nodiscard]] bool ArePipelinesReady() const
        {
            return m_PipelinesReady;
        }

        [[nodiscard]] bool DidPipelinePreparationFail() const noexcept
        {
            return m_PipelinePreparationFailed;
        }

        [[nodiscard]] bool Render(
            nvrhi::ICommandList* commandList,
            const MsaaVisibilityResolveInputs& inputs,
            const MsaaVisibilityResolveOutputs& outputs,
            uint32_t sampleCount) const;
    };
}
