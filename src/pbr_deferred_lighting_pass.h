#pragma once

#include <donut/core/math/math.h>
#include <donut/engine/BindingCache.h>
#include <donut/render/DeferredLightingPass.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <memory>

namespace donut::engine
{
    class CommonRenderPasses;
    class ICompositeView;
    class ShaderFactory;
}

// UVSR-owned deferred lighting pass. In addition to the regular HDR target,
// it emits material-weighted direct diffuse plus emissive radiance for
// screen-space indirect-light sampling.
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
    nvrhi::SamplerHandle m_ShadowSamplerComparison;
    nvrhi::BufferHandle m_DeferredLightingCB;
    // No source UAV, compact one-bounce source, and packed multi-bounce source.
    std::array<Pipeline, 3> m_Pipelines;
    // Static 2x, 4x, and 8x per-sample deferred pipelines, each compiled
    // without and with the single-surface visibility correction.
    std::array<std::array<Pipeline, 4>, 2> m_MsaaPipelines;
    donut::engine::BindingCache m_BindingSets;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;

public:
    PbrDeferredLightingPass(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses);

    void Init(const std::shared_ptr<donut::engine::ShaderFactory>& shaderFactory);

    void Render(
        nvrhi::ICommandList* commandList,
        const donut::engine::ICompositeView& compositeView,
        const donut::render::DeferredLightingPass::Inputs& inputs,
        nvrhi::ITexture* sourceRadianceOutput,
        bool separateIndirect,
        bool writeSourceRadiance,
        bool writeBounceMetadata,
        bool includeEmissiveSource,
        float emissiveSourceGain,
        donut::math::float2 randomOffset = donut::math::float2::zero(),
        nvrhi::ITexture* resolvedBackground = nullptr,
        uint32_t msaaSampleCount = 1u,
        nvrhi::ITexture* visibilityBaseLighting = nullptr,
        nvrhi::ITexture* visibilityComposite = nullptr);

    void ResetBindingCache();
};
