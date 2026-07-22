#pragma once

#include "bend_screen_space_shadows_settings.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class CommonRenderPasses;
    class DirectionalLight;
    class IView;
    class Light;
    class ShaderFactory;
}

namespace uvsr
{
    struct BendScreenSpaceShadowTimings
    {
        float traceMilliseconds = 0.f;
        uint32_t dispatchCount = 0u;
        uint32_t totalGroups = 0u;
        uint32_t sampleCount = 0u;
        uint64_t outputTextureBytes = 0u;
        bool active = false;
        bool supported = true;
    };

    struct BendScreenSpaceShadowResult
    {
        nvrhi::ITexture* nearVisibility = nullptr;
        const donut::engine::Light* light = nullptr;
        bool showDebug = false;

        [[nodiscard]] explicit operator bool() const
        {
            return nearVisibility && light;
        }
    };

    // Thin UVSR adapter around Bend Studio's untouched CPU and GPU headers.
    // The result is deliberately a standalone near-visibility producer so a
    // future far tracer can be composed by the renderer without changing Bend.
    class BendScreenSpaceShadowPass
    {
    public:
        BendScreenSpaceShadowPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses);

        BendScreenSpaceShadowResult Render(
            nvrhi::ICommandList* commandList,
            const BendScreenSpaceShadowSettings& settings,
            const donut::engine::IView& view,
            nvrhi::ITexture* depth,
            const donut::engine::DirectionalLight* light);

        void PresentDebug(
            nvrhi::ICommandList* commandList,
            nvrhi::IFramebuffer* framebuffer);

        [[nodiscard]] const BendScreenSpaceShadowTimings& GetTimings() const
        {
            return m_Timings;
        }

    private:
        struct Pipeline
        {
            nvrhi::ShaderHandle shader;
            nvrhi::ComputePipelineHandle pso;
        };

        static constexpr uint32_t c_TimerLatency = 4u;

        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
        std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::BufferHandle m_ConstantBuffer;
        std::array<nvrhi::SamplerHandle, 2> m_PointBorderSamplers;
        nvrhi::TextureHandle m_NearVisibility;
        nvrhi::ITexture* m_BoundDepth = nullptr;
        bool m_BoundReverseDepth = true;

        std::array<std::array<std::array<Pipeline, 3>, 3>, 5> m_Pipelines;

        nvrhi::ShaderHandle m_DebugPixelShader;
        nvrhi::BindingLayoutHandle m_DebugBindingLayout;
        nvrhi::BindingSetHandle m_DebugBindingSet;
        nvrhi::GraphicsPipelineHandle m_DebugPipeline;

        std::array<nvrhi::TimerQueryHandle, c_TimerLatency> m_TimerQueries;
        std::array<bool, c_TimerLatency> m_TimerPending{};
        bool m_TimerActive = false;
        uint32_t m_TimerFrame = 0u;
        BendScreenSpaceShadowTimings m_Timings;
        bool m_ReportedInvalidVariant = false;
        bool m_ReportedInvalidInput = false;

        bool EnsureResources(nvrhi::ITexture* depth, bool reverseDepth);
        Pipeline* EnsurePipeline(
            const BendScreenSpaceShadowSettings& settings);
        void AdvanceTimer();
        void BeginTimer(nvrhi::ICommandList* commandList);
        void EndTimer(nvrhi::ICommandList* commandList);
    };
}
