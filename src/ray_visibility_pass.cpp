#include "ray_visibility_pass.h"
#include "denoising_replay_sampling_cb.h"
#include <cstring>
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
            HasFormatSupport(device, nvrhi::Format::R8_UNORM, kind != Kind::Sun) &&
            (kind != Kind::Sun || HasFormatSupport(device, nvrhi::Format::R32_FLOAT, false));
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
        m_HitDistanceSupported = sun || HasFormatSupport(device, nvrhi::Format::R16_FLOAT, true);
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
        for (uint32_t output = sun ? 1u : 0u; output < 2u; ++output)
        {
            if (output != 0u && !m_HitDistanceSupported)
                continue;
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
            if (!sun)
            {
                layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(4));
                layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(5));
            }
            if (output != 0u)
                layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_UAV(1));
            m_BindingLayouts[output] = device->createBindingLayout(layout);

                std::vector<RendererShaderMacro> macros;
                if (kind == Kind::Sky)
                    macros.emplace_back("OUTPUT_HIT_DISTANCE", std::to_string(output));
                const char* entry = sun ? "main" : kind == Kind::Sky ? "Generate" :
                    output ? "GenerateVisibilityAndHitDistance" : "GenerateVisibility";
                const auto shader = shaderFactory->CreateShader(file, entry, &macros, nvrhi::ShaderType::Compute);
                if (!shader || !m_BindingLayouts[output])
                    continue;
                nvrhi::ComputePipelineDesc pipeline;
                pipeline.CS = shader;
                pipeline.bindingLayouts = {m_BindingLayouts[output], m_BindlessLayout};
                m_Pipelines[output] = device->createComputePipeline(pipeline);
            
        }
        nvrhi::BindingLayoutDesc replayLayout;
        replayLayout.visibility = nvrhi::ShaderType::Compute;
        replayLayout.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
            nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),
            nvrhi::BindingLayoutItem::Texture_SRV(20),
            nvrhi::BindingLayoutItem::Texture_SRV(23),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_UAV(6)
        };
        if (!sun) replayLayout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(4));
        m_ReplayLayout = device->createBindingLayout(replayLayout);
        std::vector<RendererShaderMacro> replayMacros;
        if (kind == Kind::Sky) replayMacros.emplace_back("OUTPUT_HIT_DISTANCE", "0");
        const auto replayShader = shaderFactory->CreateShader(file, "GenerateReplay", &replayMacros, nvrhi::ShaderType::Compute);
        if (replayShader && m_ReplayLayout)
        {
            nvrhi::ComputePipelineDesc description;
            description.CS = replayShader;
            description.bindingLayouts = { m_ReplayLayout, m_BindlessLayout };
            m_ReplayPipeline = device->createComputePipeline(description);
        }
        buffer.byteSize = sizeof(DenoisingReplaySamplingConstants);
        buffer.debugName = "RayVisibilityReplayConstants";
        m_ReplayConstants = device->createBuffer(buffer);
        const auto complete = [this](uint32_t output)
        {
            return m_BindingLayouts[output] && m_Pipelines[output];
        };
        m_HitDistanceSupported = m_HitDistanceSupported && complete(1u);
        if (!m_HitDistanceSupported)
        {
            m_BindingLayouts[1] = nullptr;
            m_Pipelines[1] = nullptr;
        }
        m_Supported = m_ConstantBuffer && m_MaterialSampler && complete(sun ? 1u : 0u);
        if (!m_Supported)
            log::error("%s pipelines could not be created", PassName(kind));
    }

    void RayVisibilityPass::ResetBindingCache()
    {
        m_BindingSets = {};
        m_BoundSurface = {};
        m_BoundRayScene = {};
        m_BoundNoise = m_BoundAttemptMask = nullptr;
    }

    bool RayVisibilityPass::Prepare(const LightingSurfaceView& surface, const RaySceneView& rayScene,
        nvrhi::ITexture* noise, nvrhi::ITexture* attemptMask, bool& outputHitDistance)
    {
        if (!surface.HasRayTracingInputs() || !rayScene ||
            (m_Kind != Kind::Sun && !noise) || (outputHitDistance && !m_HitDistanceSupported))
            return false;
        const auto& depth = surface.depth->getDesc();
        if (!AreRendererReceiverTextureDescriptorsCompatible(depth,
            surface.material->getDesc(), surface.normals->getDesc()) || !depth.isShaderResource ||
            !surface.material->getDesc().isShaderResource || !surface.normals->getDesc().isShaderResource)
            return false;

        auto outputs = m_Outputs;
        for (uint32_t index = 0u; index < outputs.size(); ++index)
        {
            if (index == 1u && !outputHitDistance)
            {
                outputs[index] = nullptr;
                continue;
            }
            if (outputs[index] && outputs[index]->getDesc().width == depth.width &&
                outputs[index]->getDesc().height == depth.height)
                continue;
            nvrhi::TextureDesc description;
            description.width = depth.width;
            description.height = depth.height;
            description.format = index != 1u ? nvrhi::Format::R8_UNORM :
                m_Kind == Kind::Sun ? nvrhi::Format::R32_FLOAT : nvrhi::Format::R16_FLOAT;
            description.isUAV = true;
            description.debugName = std::string(PassName(m_Kind)) + (index == 0u ? "/Visibility" : "/Hit Distance");
            description.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
            outputs[index] = m_Device->createTexture(description);
            if (!outputs[index])
            {
                if (index != 1u || m_Kind != Kind::Sky)
                    return false;
                m_HitDistanceSupported = outputHitDistance = false;
            }
        }
        if (outputs != m_Outputs || !m_BoundSurface.HasSameRayTracingInputs(surface) ||
            !m_BoundRayScene.HasSameBindings(rayScene) || m_BoundNoise != noise || m_BoundAttemptMask != attemptMask)
        {
            ResetBindingCache();
            m_Outputs = std::move(outputs);
        }
        auto& bindingSet = m_BindingSets[outputHitDistance ? 1u : 0u];
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
                nvrhi::BindingSetItem::Texture_UAV(0, m_Outputs[0])
            };
            if (m_Kind != Kind::Sun)
            {
                description.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(4, noise));
                description.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(5, attemptMask));
            }
            if (outputHitDistance)
                description.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(1, m_Outputs[1]));
            bindingSet = m_Device->createBindingSet(description, m_BindingLayouts[outputHitDistance ? 1u : 0u]);
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
        const LightingSurfaceView& surface, const RaySceneView& rayScene, bool outputHitDistance,
        const void* constants, size_t constantSize, uint32_t phase, const DenoisingReplayInputs& replay)
    {
        if (replay.selection && m_ReplayPipeline && m_ReplayConstants)
        {
            const auto& extent = replay.selection->getDesc();
            if (!m_Gradient || m_Gradient->getDesc().width != extent.width || m_Gradient->getDesc().height != extent.height)
            {
                auto description = extent;
                description.format = nvrhi::Format::RGBA32_FLOAT;
                description.debugName = "RayVisibilityTemporalGradient";
                m_Gradient = m_Device->createTexture(description);
                m_ReplayFrame = ~uint64_t(0);
            }
            if (m_Gradient)
            {
                DenoisingReplaySamplingConstants replayConstants{};
                replayConstants.sourceSize = { surface.depth->getDesc().width, surface.depth->getDesc().height };
                replayConstants.gradientSize = { extent.width, extent.height };
                replayConstants.frame = uint32_t(replay.frame);
                replayConstants.phase = m_PreviousPhase;
                replayConstants.valid = replay.valid && m_ReplayFrame != ~uint64_t(0) && m_ReplayFrame + 1u == replay.frame;
                std::vector<uint8_t> replayData(constantSize);
                std::memcpy(replayData.data(), constants, constantSize);
                std::memcpy(replayData.data(), &replay.previousView, sizeof(PlanarViewConstants));
                nvrhi::BindingSetDesc bindings;
                bindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                    nvrhi::BindingSetItem::ConstantBuffer(1, m_ReplayConstants),
                    nvrhi::BindingSetItem::RayTracingAccelStruct(0, rayScene.tlas),
                    nvrhi::BindingSetItem::Texture_SRV(1, replay.depth),
                    nvrhi::BindingSetItem::Texture_SRV(2, replay.material),
                    nvrhi::BindingSetItem::Texture_SRV(3, replay.normals),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(10, rayScene.geometryBuffer),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(11, rayScene.materialBuffer),
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(12, rayScene.geometryIndexMap),
                    nvrhi::BindingSetItem::Texture_SRV(20, replay.selection),
                    nvrhi::BindingSetItem::Texture_SRV(23, m_Outputs[0]),
                    nvrhi::BindingSetItem::Sampler(0, m_MaterialSampler),
                    nvrhi::BindingSetItem::Texture_UAV(6, m_Gradient)
                };
                if (m_Kind != Kind::Sun) bindings.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(4, m_BoundNoise));
                auto binding = m_Device->createBindingSet(bindings, m_ReplayLayout);
                if (binding)
                {
                    commandList->writeBuffer(m_ConstantBuffer, replayData.data(), constantSize);
                    commandList->writeBuffer(m_ReplayConstants, &replayConstants, sizeof(replayConstants));
                    nvrhi::ComputeState state;
                    state.pipeline = m_ReplayPipeline;
                    state.bindings = { binding, rayScene.descriptorTable };
                    commandList->beginMarker("Ray Visibility Temporal Gradient");
                    commandList->setComputeState(state);
                    commandList->dispatch((extent.width + 7u) / 8u, (extent.height + 7u) / 8u);
                    commandList->endMarker();
                }
                else m_Gradient = nullptr;
            }
            m_ReplayFrame = replay.frame;
            m_PreviousPhase = phase;
        }
        else { m_Gradient = nullptr; m_ReplayFrame = ~uint64_t(0); }
        commandList->writeBuffer(m_ConstantBuffer, constants, constantSize);
        nvrhi::ComputeState state;
        state.pipeline = m_Pipelines[outputHitDistance ? 1u : 0u];
        state.bindings = { m_BindingSets[outputHitDistance ? 1u : 0u], rayScene.descriptorTable };
        commandList->beginMarker(PassName(m_Kind));
        commandList->setComputeState(state);
        const auto extent = view.GetViewExtent();
        commandList->dispatch(div_ceil(extent.width(), 8), div_ceil(extent.height(), 8));
        commandList->endMarker();
    }

    DirectionalRayVisibilityResult RayVisibilityPass::RenderDirectional(nvrhi::ICommandList* commandList,
        const DirectionalShadowSettings& settings, const IView& view, const LightingSurfaceView& surface,
        const RaySceneView& rayScene, const DirectionalLight* light, float sceneDiagonal, const DenoisingReplayInputs& replay)
    {
        if (m_Kind != Kind::Sun || !m_Supported || !settings.enabled || !commandList || !rayScene ||
            !light || !IsDirectionalShadowSettingsValid(settings))
            return {};
        const float rayDistance = ResolveRayVisibilityMaxDistance(settings.maxDistance, sceneDiagonal);
        const float3 propagationDirection = float3(light->GetDirection());
        const float directionLengthSquared = dot(propagationDirection, propagationDirection);
        if (!std::isfinite(rayDistance) || !(rayDistance > 0.f) || !std::isfinite(directionLengthSquared) ||
            !(directionLengthSquared > 1.e-12f))
            return {};
        bool outputHitDistance = true;
        if (!Prepare(surface, rayScene, nullptr, nullptr, outputHitDistance))
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

        Dispatch(commandList, view, surface, rayScene, true, &constants, sizeof(constants), 0u, replay);
        return { m_Outputs[0], m_Outputs[1], light, true };
    }

    RayTracedFlashlightShadowResult RayVisibilityPass::RenderFlashlight(nvrhi::ICommandList* commandList,
        const IView& view, const LightingSurfaceView& surface, const RaySceneView& rayScene,
        const SpotLight* light, const FlashlightBeamProfile& beamProfile, const NoiseSettings& noiseSettings,
        nvrhi::ITexture* noiseTexture, uint32_t samplingPhase, float rayBiasMeters, bool outputHitDistance,
        const LightingSampleSchedule& sampleSchedule, const DenoisingReplayInputs& replay)
    {
        if (m_Kind != Kind::Flashlight || !m_Supported || !commandList || !rayScene || !light ||
            !noiseTexture || !sampleSchedule)
            return {};
        const float3 lightPosition = float3(light->GetPosition());
        float3 lightDirection = float3(light->GetDirection());
        const float directionLengthSquared = dot(lightDirection, lightDirection);
        if ((outputHitDistance && !m_HitDistanceSupported) || !FlashlightBeamProfileIsValid(beamProfile) ||
            !IsValidNoiseSettings(noiseSettings) || !std::isfinite(rayBiasMeters) || rayBiasMeters < 0.f ||
            rayBiasMeters > RayTracedFlashlightMaximumRayBias || !std::isfinite(light->range) ||
            !(light->range > beamProfile.emitterRadiusMeters) || !std::isfinite(light->radius) ||
            std::abs(light->radius - beamProfile.emitterRadiusMeters) > 1e-6f || !IsFiniteFloat3(lightPosition) ||
            !IsFiniteFloat3(lightDirection) || !(directionLengthSquared > 1e-12f) || !std::isfinite(directionLengthSquared) ||
            !Prepare(surface, rayScene, noiseTexture, sampleSchedule.attemptMask, outputHitDistance))
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
            ? RayTracedFlashlightFiniteEmitterSampleCount
            : 1u;
        constants.noisePattern =
            static_cast<uint32_t>(noiseSettings.pattern);
        constants.sampleSequenceMode = static_cast<uint32_t>(
            ResolveLightingSampleSequenceMode(
                sampleSchedule,
                stochastic,
                noiseSettings.animate));

        Dispatch(commandList, view, surface, rayScene, outputHitDistance, &constants, sizeof(constants),
            samplingPhase, replay);
        return { m_Outputs[0], outputHitDistance ? m_Outputs[1].Get() : nullptr,
            light, true, stochastic };
    }

    RayTracedSkyVisibilityResult RayVisibilityPass::RenderSky(nvrhi::ICommandList* commandList,
        const RayTracedSkyVisibilitySettings& settings, const IView& view, const LightingSurfaceView& surface,
        const RaySceneView& rayScene, const NoiseSettings& noiseSettings, nvrhi::ITexture* noiseTexture,
        uint32_t samplingPhase, float sceneDiagonal, const LightingSampleSchedule& sampleSchedule, const DenoisingReplayInputs& replay)
    {
        if (m_Kind != Kind::Sky || !m_Supported || !commandList || !rayScene)
            return {};
        const float rayDistance = ResolveRayVisibilityMaxDistance(settings.maxDistance, sceneDiagonal);
        bool outputHitDistance = settings.outputHitDistance && m_HitDistanceSupported;
        if (!noiseTexture || !sampleSchedule || !IsValidNoiseSettings(noiseSettings) ||
            !IsRayTracedSkyVisibilityConfigurationSupported(settings) || std::isnan(rayDistance) ||
            !Prepare(surface, rayScene, noiseTexture, sampleSchedule.attemptMask, outputHitDistance))
        {
            ReportInvalidInput();
            return {};
        }
        if (settings.outputHitDistance && !outputHitDistance && !m_ReportedHitDistanceUnavailable)
        {
            log::warning("Ray traced sky visibility hit distance output is unavailable");
            m_ReportedHitDistanceUnavailable = true;
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

        Dispatch(commandList, view, surface, rayScene, outputHitDistance, &constants, sizeof(constants),
            samplingPhase, replay);
        return { m_Outputs[0], outputHitDistance ? m_Outputs[1].Get() : nullptr,
            true };
    }
}
