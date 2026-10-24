#pragma once

#include "direct_light_visibility_nvrhi.h"
#include "flashlight_shared.h"
#include "lighting_surface_nvrhi.h"
#include "pbr_deferred_dispatch_contract.h"
#include "renderer_scene_light.h"

#include "pbr_binding_sets_nvrhi.h"
#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <memory>

namespace uvsr { struct RendererView; }

namespace uvsr
{
    struct ImageBasedLightingProbe;
    class RendererCommonPasses;
    class RendererShaderFactory;

    struct PbrDeferredLightingInputs
    {
        const RendererView* view = nullptr;
        LightingSurfaceView surface;
        RendererSceneLightRange lights;
        bool hardShadows = false;
        nvrhi::ITexture* output = nullptr;
        DirectLightVisibilities directLightVisibilities;
        RendererSceneHandle flashlight;
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
    uvsr::PbrBindingSetsNvrhi m_BindingSets;
    uvsr::RendererCommonPasses* m_CommonPasses = nullptr;
    uvsr::RendererShaderFactory* m_ShaderFactory = nullptr;
    bool m_PipelinesReady = false;
    bool m_PipelinePreparationFailed = false;
    bool m_ResourcesValid = false;

public:
    PbrDeferredLightingPass(
        nvrhi::IDevice* device,
        uvsr::RendererCommonPasses* commonPasses);

    void Init(
        uvsr::RendererShaderFactory* shaderFactory,
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
#if defined(UVSR_BUILD_TESTING)
    [[nodiscard]] size_t GetBindingCachePeakSize() { return m_BindingSets.GetPeakSize(); }
#endif
};
