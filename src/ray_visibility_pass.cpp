#include "ray_visibility_pass.h"
#include "renderer_common_passes.h"
#include "renderer_log.h"
#include "renderer_receiver_texture_contract.h"
#include "renderer_shader_factory.h"
#include "ray_traced_flashlight_shadows_shared.h"
#include "directional_ray_visibility_cb.h"
#include "ray_traced_flashlight_shadows_cb.h"
#include "ray_traced_sky_visibility_cb.h"

#include <donut/core/math/math.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/View.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace donut::engine;
using namespace donut::math;

static_assert(offsetof(DirectionalRayVisibilityConstants, directionToLightAndDistance) == sizeof(PlanarViewConstants));
static_assert(offsetof(RayTracedFlashlightShadowConstants, lightPositionAndRange) == sizeof(PlanarViewConstants));
static_assert(offsetof(RayTracedFlashlightShadowConstants, beamProfile) == sizeof(PlanarViewConstants) + 32u);
static_assert(offsetof(RayTracedSkyVisibilityConstants, sampleSequencePhase) == sizeof(PlanarViewConstants));

namespace uvsr
{
    namespace
    {
        const char* PassName(RayVisibilityPass::Kind kind)
        {
            switch (kind)
            {
            case RayVisibilityPass::Kind::Sun: return "Directional Ray Visibility";
            case RayVisibilityPass::Kind::Flashlight: return "Ray Traced Flashlight Shadows";
            case RayVisibilityPass::Kind::Sky: return "Ray Traced Sky Visibility";
            }
            return "Ray Visibility";
        }

        bool HasFormatSupport(nvrhi::IDevice* device, nvrhi::Format format, bool sampled)
        {
            auto required = nvrhi::FormatSupport::Texture | nvrhi::FormatSupport::ShaderLoad |
                nvrhi::FormatSupport::ShaderUavStore;
            if (sampled)
                required = required | nvrhi::FormatSupport::ShaderSample;
            return device && (device->queryFormatSupport(format) & required) == required;
        }

        float GetDepthQuantizationStep(nvrhi::Format format)
        {
            switch (format)
            {
            case nvrhi::Format::D16: return 1.f / 65535.f;
            case nvrhi::Format::D24S8: return 1.f / 16777215.f;
            default: return 0.f;
            }
        }

        bool IsFloatingPointDepth(nvrhi::Format format)
        {
            return format == nvrhi::Format::D32 || format == nvrhi::Format::D32S8 ||
                format == nvrhi::Format::R32_FLOAT;
        }

        bool IsFiniteFloat3(float3 value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    }

    bool RayVisibilityPass::IsDeviceSupported(nvrhi::IDevice* device, Kind kind)
    {
        return device && device->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayQuery) &&
            HasFormatSupport(device, nvrhi::Format::R8_UNORM, kind != Kind::Sun);
    }

    RayVisibilityPass::RayVisibilityPass(nvrhi::IDevice* device,
        const std::shared_ptr<RendererShaderFactory>& shaderFactory,
        nvrhi::IBindingLayout* bindlessLayout, Kind kind)
        : m_Device(device), m_Kind(kind), m_BindlessLayout(bindlessLayout)
    {
        if (!shaderFactory || !bindlessLayout || !IsDeviceSupported(device, kind))
        {
            log::warning("%s requires DXR 1.1 ray queries and supported output formats", PassName(kind));
            return;
        }
        const bool sun = kind == Kind::Sun;
        nvrhi::BufferDesc buffer;
        buffer.byteSize = sun ? sizeof(DirectionalRayVisibilityConstants) :
            kind == Kind::Flashlight ? sizeof(RayTracedFlashlightShadowConstants) : sizeof(RayTracedSkyVisibilityConstants);
        buffer.debugName = PassName(kind);
        buffer.isConstantBuffer = buffer.isVolatile = true;
        buffer.maxVersions = RendererMaxConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(buffer);
        m_MaterialSampler = device->createSampler(nvrhi::SamplerDesc()
            .setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

        const char* file = sun ? "uvsr/directional_ray_visibility_cs.hlsl" :
            kind == Kind::Flashlight ? "uvsr/ray_traced_flashlight_shadows_cs.hlsl" : "uvsr/ray_traced_sky_visibility_cs.hlsl";
        {
            nvrhi::BindingLayoutDesc layout;
            layout.visibility = nvrhi::ShaderType::Compute;
            layout.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_SRV(3),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),
                nvrhi::BindingLayoutItem::Sampler(0),
                nvrhi::BindingLayoutItem::Texture_UAV(0)
            };
            layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(4));
            layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(5));
            m_BindingLayout = device->createBindingLayout(layout);

            const char* entry = sun ? "main" : kind == Kind::Sky ? "Generate" : "GenerateVisibility";
            const auto shader = shaderFactory->CreateShader(file, entry, nullptr, nvrhi::ShaderType::Compute);
                if (!shader || !m_BindingLayout)
                    return;
                nvrhi::ComputePipelineDesc pipeline;
                pipeline.CS = shader;
                pipeline.bindingLayouts = {m_BindingLayout, m_BindlessLayout};
                m_Pipeline = device->createComputePipeline(pipeline);
            
        }
        m_Supported = m_ConstantBuffer && m_MaterialSampler && m_BindingLayout && m_Pipeline;
        if (!m_Supported)
            log::error("%s pipelines could not be created", PassName(kind));
    }

    void RayVisibilityPass::ResetBindingCache()
    {
        m_BindingSet = nullptr;
        m_BoundSurface = {};
        m_BoundRayScene = {};
        m_BoundNoise = m_BoundAttemptMask = nullptr;
    }

    bool RayVisibilityPass::Prepare(const LightingSurfaceView& surface, const RaySceneView& rayScene,
        nvrhi::ITexture* noise, nvrhi::ITexture* attemptMask)
    {
        if (!surface.HasRayTracingInputs() || !rayScene || !noise)
            return false;
        const auto& depth = surface.depth->getDesc();
        if (!AreRendererReceiverTextureDescriptorsCompatible(depth,
            surface.material->getDesc(), surface.normals->getDesc()) || !depth.isShaderResource ||
            !surface.material->getDesc().isShaderResource || !surface.normals->getDesc().isShaderResource)
            return false;

        auto output = m_Output;
        if (!output || output->getDesc().width != depth.width || output->getDesc().height != depth.height)
        {
            nvrhi::TextureDesc description;
            description.width = depth.width;
            description.height = depth.height;
            description.format = nvrhi::Format::R8_UNORM;
            description.isUAV = true;
            description.debugName = std::string(PassName(m_Kind)) + "/Visibility";
            description.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
            output = m_Device->createTexture(description);
            if (!output)
                return false;
        }
        if (output != m_Output || !m_BoundSurface.HasSameRayTracingInputs(surface) ||
            !m_BoundRayScene.HasSameBindings(rayScene) || m_BoundNoise != noise || m_BoundAttemptMask != attemptMask)
        {
            ResetBindingCache();
            m_Output = std::move(output);
        }
        auto& bindingSet = m_BindingSet;
        if (!bindingSet)
        {
            nvrhi::BindingSetDesc description;
            description.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::RayTracingAccelStruct(0, rayScene.tlas),
                nvrhi::BindingSetItem::Texture_SRV(1, surface.depth),
                nvrhi::BindingSetItem::Texture_SRV(2, surface.material),
                nvrhi::BindingSetItem::Texture_SRV(3, surface.normals),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(10, rayScene.geometryBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(11, rayScene.materialBuffer),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(12, rayScene.geometryIndexMap),
                nvrhi::BindingSetItem::Sampler(0, m_MaterialSampler),
                nvrhi::BindingSetItem::Texture_UAV(0, m_Output)
            };
            description.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(4, noise));
            description.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(5, attemptMask));
            bindingSet = m_Device->createBindingSet(description, m_BindingLayout);
            if (!bindingSet)
                return false;
        }
        m_BoundSurface = surface;
        m_BoundRayScene = rayScene;
        m_BoundNoise = noise;
        m_BoundAttemptMask = attemptMask;
        m_ReportedInvalidInput = false;
        return true;
    }

    void RayVisibilityPass::ReportInvalidInput()
    {
        if (!m_ReportedInvalidInput)
            log::error("%s received incompatible or unavailable resources", PassName(m_Kind));
        m_ReportedInvalidInput = true;
    }

    void RayVisibilityPass::Dispatch(nvrhi::ICommandList* commandList, const IView& view,
        const LightingSurfaceView& surface, const RaySceneView& rayScene)
    {
        nvrhi::ComputeState state;
        state.pipeline = m_Pipeline;
        state.bindings = { m_BindingSet, rayScene.descriptorTable };
        commandList->beginMarker(PassName(m_Kind));
        commandList->setComputeState(state);
        const auto extent = view.GetViewExtent();
        commandList->dispatch(div_ceil(extent.width(), 8), div_ceil(extent.height(), 8));
        commandList->endMarker();
    }

    DirectionalRayVisibilityResult RayVisibilityPass::RenderDirectional(nvrhi::ICommandList* commandList,
        const DirectionalShadowSettings& settings, const IView& view, const LightingSurfaceView& surface,
        const RaySceneView& rayScene, const DirectionalLight* light, float sceneDiagonal,
        const NoiseSettings& noiseSettings, nvrhi::ITexture* noiseTexture,
        uint32_t samplingPhase, const LightingSampleSchedule& sampleSchedule)
    {
        if (m_Kind != Kind::Sun || !m_Supported || !settings.enabled || !commandList || !rayScene ||
            !light || !IsDirectionalShadowSettingsValid(settings) ||
            !IsValidNoiseSettings(noiseSettings) || !sampleSchedule)
            return {};
        const float rayDistance = ResolveRayVisibilityMaxDistance(settings.maxDistance, sceneDiagonal);
        const float3 propagationDirection = float3(light->GetDirection());
        const float directionLengthSquared = dot(propagationDirection, propagationDirection);
        if (!std::isfinite(rayDistance) || !(rayDistance > 0.f) || !std::isfinite(directionLengthSquared) ||
            !(directionLengthSquared > 1.e-12f) || !std::isfinite(light->angularSize))
            return {};
        if (!Prepare(surface, rayScene, noiseTexture, sampleSchedule.attemptMask))
        {
            ReportInvalidInput();
            return {};
        }
        DirectionalRayVisibilityConstants constants{};
        view.FillPlanarViewConstants(constants.view);
        const float3 directionToLight =
            -propagationDirection / std::sqrt(directionLengthSquared);
        constants.directionToLightAndDistance = {
            directionToLight.x,
            directionToLight.y,
            directionToLight.z,
            rayDistance };
        constants.rayBias = settings.rayBias;
        constants.depthQuantizationStep =
            GetDepthQuantizationStep(surface.depth->getDesc().format);
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;
        constants.floatDepth = IsFloatingPointDepth(
            surface.depth->getDesc().format) ? 1u : 0u;
        constants.angularDiameter = ResolveShadowEmitterSize(
            std::clamp(light->angularSize, 0.f, 90.f) * PI_f / 180.f, settings.hardShadows);
        const bool stochastic = constants.angularDiameter > 0.f;
        constants.sampleCount = stochastic ? ResolveRayShadowSampleCount(settings) : 1u;
        constants.sampleSequencePhase = samplingPhase;
        constants.sampleSequenceMode = static_cast<uint32_t>(
            ResolveLightingSampleSequenceMode(sampleSchedule, stochastic, noiseSettings.animate));
        constants.noisePattern = static_cast<uint32_t>(noiseSettings.pattern);
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));

        Dispatch(commandList, view, surface, rayScene);
        return { m_Output, light, true };
    }

    RayTracedFlashlightShadowResult RayVisibilityPass::RenderFlashlight(nvrhi::ICommandList* commandList,
        const IView& view, const LightingSurfaceView& surface, const RaySceneView& rayScene,
        const SpotLight* light, const FlashlightBeamProfile& beamProfile, const NoiseSettings& noiseSettings,
        nvrhi::ITexture* noiseTexture, uint32_t samplingPhase, float rayBiasMeters,
        const LightingSampleSchedule& sampleSchedule, uint32_t sampleCount)
    {
        if (m_Kind != Kind::Flashlight || !m_Supported || !commandList || !rayScene || !light ||
            !noiseTexture || !sampleSchedule || sampleCount < 1u || sampleCount > 64u)
            return {};
        const float3 lightPosition = float3(light->GetPosition());
        float3 lightDirection = float3(light->GetDirection());
        const float directionLengthSquared = dot(lightDirection, lightDirection);
        if (!FlashlightBeamProfileIsValid(beamProfile) ||
            !IsValidNoiseSettings(noiseSettings) || !std::isfinite(rayBiasMeters) || rayBiasMeters < 0.f ||
            rayBiasMeters > RayTracedFlashlightMaximumRayBias || !std::isfinite(light->range) ||
            !(light->range > beamProfile.emitterRadiusMeters) || !std::isfinite(light->radius) ||
            std::abs(light->radius - beamProfile.emitterRadiusMeters) > 1e-6f || !IsFiniteFloat3(lightPosition) ||
            !IsFiniteFloat3(lightDirection) || !(directionLengthSquared > 1e-12f) || !std::isfinite(directionLengthSquared) ||
            !Prepare(surface, rayScene, noiseTexture, sampleSchedule.attemptMask))
        {
            ReportInvalidInput();
            return {};
        }
        lightDirection /= std::sqrt(directionLengthSquared);
        RayTracedFlashlightShadowConstants constants = {};
        view.FillPlanarViewConstants(constants.view);
        constants.lightPositionAndRange = {
            lightPosition.x,
            lightPosition.y,
            lightPosition.z,
            light->range };
        constants.lightDirectionAndEmitterRadius = {
            lightDirection.x,
            lightDirection.y,
            lightDirection.z,
            light->radius };
        constants.beamProfile = beamProfile;
        constants.depthQuantizationStep = GetDepthQuantizationStep(
            surface.depth->getDesc().format);
        constants.rayBias = rayBiasMeters;
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;
        constants.floatDepth = IsFloatingPointDepth(
            surface.depth->getDesc().format) ? 1u : 0u;
        const bool stochastic = beamProfile.emitterRadiusMeters > 0.f;
        constants.sampleSequencePhase = samplingPhase;
        constants.sampleCount = stochastic
            ? sampleCount
            : 1u;
        constants.noisePattern =
            static_cast<uint32_t>(noiseSettings.pattern);
        constants.sampleSequenceMode = static_cast<uint32_t>(
            ResolveLightingSampleSequenceMode(
                sampleSchedule,
                stochastic,
                noiseSettings.animate));
        commandList->writeBuffer(
            m_ConstantBuffer,
            &constants,
            sizeof(constants));

        Dispatch(commandList, view, surface, rayScene);
        return { m_Output,
            light, true, stochastic };
    }

    RayTracedSkyVisibilityResult RayVisibilityPass::RenderSky(nvrhi::ICommandList* commandList,
        const RayTracedSkyVisibilitySettings& settings, const IView& view, const LightingSurfaceView& surface,
        const RaySceneView& rayScene, const NoiseSettings& noiseSettings, nvrhi::ITexture* noiseTexture,
        uint32_t samplingPhase, float sceneDiagonal, const LightingSampleSchedule& sampleSchedule)
    {
        if (m_Kind != Kind::Sky || !m_Supported || !commandList || !rayScene)
            return {};
        const float rayDistance = ResolveRayVisibilityMaxDistance(settings.maxDistance, sceneDiagonal);
        if (!noiseTexture || !sampleSchedule || !IsValidNoiseSettings(noiseSettings) ||
            !IsRayTracedSkyVisibilityConfigurationSupported(settings) || std::isnan(rayDistance) ||
            !Prepare(surface, rayScene, noiseTexture, sampleSchedule.attemptMask))
        {
            ReportInvalidInput();
            return {};
        }
        RayTracedSkyVisibilityConstants constants = {};
        view.FillPlanarViewConstants(constants.view);
        constants.sampleSequencePhase = samplingPhase;
        constants.sampleCount = ResolveRayTracedSkyVisibilitySampleCount(
            settings.sampleRateLog2);
        constants.noisePattern =
            static_cast<uint32_t>(noiseSettings.pattern);
        constants.rayDistance = rayDistance;
        constants.depthQuantizationStep = GetDepthQuantizationStep(
            surface.depth->getDesc().format);
        constants.rayBias = settings.rayBias;
        constants.reverseDepth = view.IsReverseDepth() ? 1u : 0u;
        constants.floatDepth = IsFloatingPointDepth(
            surface.depth->getDesc().format) ? 1u : 0u;
        constants.sampleSequenceMode = static_cast<uint32_t>(
            ResolveLightingSampleSequenceMode(
                sampleSchedule,
                true,
                noiseSettings.animate));
        commandList->writeBuffer(
            m_ConstantBuffer, &constants, sizeof(constants));

        Dispatch(commandList, view, surface, rayScene);
        return { m_Output,
            true };
    }
}
