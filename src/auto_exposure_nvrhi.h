#pragma once

#include "auto_exposure.h"
#include <nvrhi/nvrhi.h>
#include <memory>

namespace uvsr
{
    class RendererShaderFactory;
    struct RendererView;

    class AutoExposurePass final
    {
    public:
        AutoExposurePass(
            nvrhi::IDevice* device,
            RendererShaderFactory*
                shaderFactory);

        [[nodiscard]] bool IsAvailable() const;
        [[nodiscard]] bool DidDispatchThisFrame() const
        {
            return m_DispatchedThisFrame;
        }

        [[nodiscard]] nvrhi::IBuffer* Render(
            nvrhi::ICommandList* commandList,
            const RendererView& frameView,
            nvrhi::ITexture* sceneColor,
            const AutoExposureSettings& settings,
            float frameDeltaSeconds,
            bool diagnosticView);

        void Reset();

    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::BufferHandle m_HistogramBuffer;
        nvrhi::BufferHandle m_ExposureBuffer;
        nvrhi::ShaderHandle m_HistogramShader;
        nvrhi::ShaderHandle m_ResolveShader;
        nvrhi::BindingLayoutHandle m_HistogramBindingLayout;
        nvrhi::BindingLayoutHandle m_ResolveBindingLayout;
        nvrhi::BindingSetHandle m_HistogramBindingSet;
        nvrhi::BindingSetHandle m_ResolveBindingSet;
        nvrhi::ComputePipelineHandle m_HistogramPipeline;
        nvrhi::ComputePipelineHandle m_ResolvePipeline;
        nvrhi::ITexture* m_BoundSceneColor = nullptr;
        AutoExposureFrameHistory m_FrameHistory;
        bool m_DispatchedThisFrame = false;
    };
}
