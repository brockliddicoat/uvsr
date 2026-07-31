#pragma once

#include "screen_space_directional_shadows_settings.h"

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
    struct ScreenSpaceDirectionalShadowTimings
    {
        float traceMilliseconds = 0.f;
        uint32_t dispatchCount = 0u;
        uint32_t totalGroups = 0u;
        uint32_t sampleCount = 0u;
        uint64_t outputTextureBytes = 0u;
        bool active = false;
        bool supported = true;
    };

    struct ScreenSpaceDirectionalShadowResult
    {
        nvrhi::ITexture* nearVisibility = nullptr;
        const donut::engine::Light* light = nullptr;
        bool showDebug = false;

        [[nodiscard]] explicit operator bool() const
        {
            return nearVisibility && light;
        }
    };

    // Full-screen, one-dispatch directional ray marcher. Its public result is
    // deliberately producer-neutral: full-resolution R8 visibility where one
    // means unoccluded, associated with the exact light it attenuates.
    class ScreenSpaceDirectionalShadowPass
    {
    public:
        ScreenSpaceDirectionalShadowPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses);

        ScreenSpaceDirectionalShadowResult Render(
            nvrhi::ICommandList* commandList,
            const ScreenSpaceDirectionalShadowSettings& settings,
            const donut::engine::IView& view,
            nvrhi::ITexture* depth,
            const donut::engine::DirectionalLight* light);

        void PresentDebug(
            nvrhi::ICommandList* commandList,
            nvrhi::IFramebuffer* framebuffer);

        [[nodiscard]] const ScreenSpaceDirectionalShadowTimings&
            GetTimings() const
        {
            return m_Timings;
        }

    private:
        static constexpr uint32_t c_TimerLatency = 4u;

        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
        std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
        nvrhi::ShaderHandle m_TraceShader;
        nvrhi::ComputePipelineHandle m_TracePipeline;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::SamplerHandle m_PointClampSampler;
        nvrhi::TextureHandle m_NearVisibility;
        nvrhi::ITexture* m_BoundDepth = nullptr;

        nvrhi::ShaderHandle m_DebugPixelShader;
        nvrhi::BindingLayoutHandle m_DebugBindingLayout;
        nvrhi::BindingSetHandle m_DebugBindingSet;
        nvrhi::GraphicsPipelineHandle m_DebugPipeline;

        std::array<nvrhi::TimerQueryHandle, c_TimerLatency> m_TimerQueries;
        std::array<bool, c_TimerLatency> m_TimerPending{};
        bool m_TimerActive = false;
        uint32_t m_TimerFrame = 0u;
        ScreenSpaceDirectionalShadowTimings m_Timings;
        bool m_ReportedInvalidConfiguration = false;
        bool m_ReportedInvalidInput = false;

        bool EnsureResources(nvrhi::ITexture* depth);
        void AdvanceTimer();
        void BeginTimer(nvrhi::ICommandList* commandList);
        void EndTimer(nvrhi::ICommandList* commandList);
    };
}
