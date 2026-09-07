#pragma once

#include "direct_light_visibility.h"
#include "flashlight_shared.h"
#include "lighting_surface.h"
#include "pbr_deferred_dispatch_contract.h"

#include <donut/engine/BindingCache.h>
#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace donut::engine
{
    class ICompositeView;
    class Light;
}

namespace uvsr
{
    struct ImageBasedLightingProbe;
    class RendererCommonPasses;
    class RendererShaderFactory;

    struct PbrDeferredLightingInputs
    {
        const donut::engine::ICompositeView* view = nullptr;
        LightingSurfaceView surface;
        const std::vector<std::shared_ptr<donut::engine::Light>>* lights =
            nullptr;
        bool hardShadows = false;
        nvrhi::ITexture* output = nullptr;
        DirectLightVisibilities directLightVisibilities;
        const donut::engine::Light* flashlight = nullptr;
        FlashlightBeamProfile flashlightBeamProfile;
        const ImageBasedLightingProbe* environment = nullptr;
        nvrhi::ITexture* skyVisibility = nullptr;
        bool applySkyVisibilityToDiffuseIbl = false;
        bool applySkyVisibilityToSpecularIbl = false;
        uint32_t lightingDebugView = 0u;
    };
}

class PbrDeferredLightingPass final
{
private:
    struct Pipeline
    {
        nvrhi::ShaderHandle shader;
        nvrhi::ComputePipelineHandle pso;
        nvrhi::BindingLayoutHandle bindingLayout;
    };

    nvrhi::DeviceHandle m_Device;
    nvrhi::BufferHandle m_DeferredLightingCB;
    Pipeline m_Pipeline;
    donut::engine::BindingCache m_BindingSets;
    std::shared_ptr<uvsr::RendererCommonPasses> m_CommonPasses;
    std::shared_ptr<uvsr::RendererShaderFactory> m_ShaderFactory;
    bool m_PipelinesReady = false;
    bool m_PipelinePreparationFailed = false;
    bool m_ResourcesValid = false;

public:
    PbrDeferredLightingPass(
        nvrhi::IDevice* device,
        std::shared_ptr<uvsr::RendererCommonPasses> commonPasses);

    void Init(
        const std::shared_ptr<uvsr::RendererShaderFactory>& shaderFactory,
        bool deferPipelineCreation = false);

    // Pipeline creation can be spread over loading frames. Init remains eager
    // by default for standalone component users.
    [[nodiscard]] bool PreparePipelinesStep();
    [[nodiscard]] bool ArePipelinesReady() const
    {
        return m_PipelinesReady;
    }
    [[nodiscard]] bool DidPipelinePreparationFail() const
    {
        return m_PipelinePreparationFailed;
    }

    [[nodiscard]] uvsr::PbrDeferredLightingRenderResult Render(
        nvrhi::ICommandList* commandList,
        const uvsr::PbrDeferredLightingInputs& inputs);

    void ResetBindingCache();
};
