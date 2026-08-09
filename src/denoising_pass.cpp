#include "denoising_pass.h"

#ifndef UVSR_WITH_NRD
#define UVSR_WITH_NRD 0
#endif

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include "denoising_cb.h"

namespace
{
    using namespace uvsr;

    [[nodiscard]] std::array<float, 16> ToRowMajorArray(
        const dm::float4x4& matrix)
    {
        std::array<float, 16> result{};
        std::memcpy(result.data(), matrix.m_data, sizeof(result));
        return result;
    }

    [[nodiscard]] DenoiserMatrix4x4 ToDenoiserMatrix(
        const dm::float4x4& matrix)
    {
        return RowMajorToColumnMajor(ToRowMajorArray(matrix));
    }

    [[nodiscard]] uint64_t GetTextureBytes(
        const nvrhi::TextureHandle& texture)
    {
        if (!texture)
            return 0;
        const nvrhi::TextureDesc& desc = texture->getDesc();
        const nvrhi::FormatInfo info = nvrhi::getFormatInfo(desc.format);
        if (info.blockSize == 0)
            return 0;
        const uint64_t blocksX =
            (uint64_t(desc.width) + info.blockSize - 1u) / info.blockSize;
        const uint64_t blocksY =
            (uint64_t(desc.height) + info.blockSize - 1u) / info.blockSize;
        return blocksX * blocksY * uint64_t(info.bytesPerBlock);
    }

    [[nodiscard]] DenoisingEffect ToEffect(DenoiserSignalType type) noexcept
    {
        switch (type)
        {
        case DenoiserSignalType::AmbientOcclusion:
            return DenoisingEffect::AmbientOcclusion;
        case DenoiserSignalType::DiffuseGi:
            return DenoisingEffect::DiffuseGi;
        case DenoiserSignalType::SkyVisibility:
            return DenoisingEffect::SkyVisibility;
        default:
            return DenoisingEffect::Shadows;
        }
    }

    [[nodiscard]] bool ResolveMethod(
        DenoiserSignalType type,
        DenoisingMethodChoice choice,
        DenoiserMethod& output) noexcept
    {
        if (choice == DenoisingMethodChoice::None)
            return false;
        switch (type)
        {
        case DenoiserSignalType::AmbientOcclusion:
            if (choice != DenoisingMethodChoice::Reblur)
                return false;
            output = DenoiserMethod::ReblurDiffuse;
            return true;
        case DenoiserSignalType::DiffuseGi:
        case DenoiserSignalType::SkyVisibility:
            if (choice == DenoisingMethodChoice::Reblur)
            {
                output = DenoiserMethod::ReblurDiffuse;
                return true;
            }
            if (choice == DenoisingMethodChoice::Relax)
            {
                output = DenoiserMethod::RelaxDiffuse;
                return true;
            }
            return false;
        case DenoiserSignalType::SunShadow:
        case DenoiserSignalType::FlashlightShadow:
            if (choice != DenoisingMethodChoice::Sigma)
                return false;
            output = DenoiserMethod::SigmaShadow;
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsSingleSampleTexture(
        nvrhi::ITexture* texture,
        dm::uint2 extent) noexcept
    {
        if (!texture)
            return false;
        const nvrhi::TextureDesc& desc = texture->getDesc();
        return desc.dimension == nvrhi::TextureDimension::Texture2D &&
            desc.sampleCount == 1 &&
            desc.width == extent.x && desc.height == extent.y;
    }
}

namespace uvsr
{
    DenoisingPass::DenoisingPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        uint32_t framesInFlight)
        : m_Device(device)
        , m_Backend(CreateDenoiserBackend())
        , m_FramesInFlight(std::max(framesInFlight, 1u))
    {
        for (SignalState& signal : m_Signals)
        {
            signal.bindingCache =
                std::make_unique<BindingCache>(device);
        }
#if UVSR_WITH_NRD
        if (!device || !shaderFactory)
        {
            for (SignalState& signal : m_Signals)
            {
                signal.lastStatus = DenoiserStatus::Error(
                    DenoiserStatusCode::InvalidArgument,
                    "DenoisingPass needs a device and shader factory");
            }
            return;
        }

        nvrhi::BufferDesc constantDesc;
        constantDesc.byteSize = sizeof(DenoisingConstants);
        constantDesc.debugName = "DenoisingConstants";
        constantDesc.isConstantBuffer = true;
        constantDesc.isVolatile = true;
        constantDesc.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(constantDesc);

        for (uint32_t classIndex = 0;
            classIndex < uint32_t(SignalClass::Count); ++classIndex)
        {
            std::vector<ShaderMacro> macros = {
                ShaderMacro("DENOISING_SIGNAL_CLASS",
                    std::to_string(classIndex)) };

            Pipeline& prepare = m_PreparePipelines[classIndex];
            prepare.bindingLayout = device->createBindingLayout(
                nvrhi::BindingLayoutDesc()
                    .setVisibility(nvrhi::ShaderType::Compute)
                    .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(3))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(4))
                    .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
                    .addItem(nvrhi::BindingLayoutItem::Texture_UAV(1))
                    .addItem(nvrhi::BindingLayoutItem::Texture_UAV(2))
                    .addItem(nvrhi::BindingLayoutItem::Texture_UAV(3)));
            prepare.shader = shaderFactory->CreateShader(
                "uvsr/denoising_prepare_cs.hlsl", "main", &macros,
                nvrhi::ShaderType::Compute);
            if (prepare.shader && prepare.bindingLayout)
            {
                nvrhi::ComputePipelineDesc pipelineDesc;
                pipelineDesc.CS = prepare.shader;
                pipelineDesc.bindingLayouts = { prepare.bindingLayout };
                prepare.pipeline = device->createComputePipeline(pipelineDesc);
            }

            Pipeline& resolve = m_ResolvePipelines[classIndex];
            resolve.bindingLayout = device->createBindingLayout(
                nvrhi::BindingLayoutDesc()
                    .setVisibility(nvrhi::ShaderType::Compute)
                    .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(3))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(4))
                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(5))
                    .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0)));
            resolve.shader = shaderFactory->CreateShader(
                "uvsr/denoising_resolve_cs.hlsl", "main", &macros,
                nvrhi::ShaderType::Compute);
            if (resolve.shader && resolve.bindingLayout)
            {
                nvrhi::ComputePipelineDesc pipelineDesc;
                pipelineDesc.CS = resolve.shader;
                pipelineDesc.bindingLayouts = { resolve.bindingLayout };
                resolve.pipeline = device->createComputePipeline(pipelineDesc);
            }
        }

        m_FrontEndAvailable = bool(m_ConstantBuffer);
        for (uint32_t index = 0;
            index < uint32_t(SignalClass::Count); ++index)
        {
            m_FrontEndAvailable = m_FrontEndAvailable &&
                bool(m_PreparePipelines[index].pipeline) &&
                bool(m_ResolvePipelines[index].pipeline);
        }
        DenoiserStatus initialization = m_Backend
            ? m_Backend->Initialize(device, m_FramesInFlight)
            : DenoiserStatus::Error(
                DenoiserStatusCode::InitializationFailed,
                "denoiser backend factory returned null");
        m_BackendInitialized = static_cast<bool>(initialization);
        for (SignalState& signal : m_Signals)
        {
            signal.lastStatus = m_FrontEndAvailable
                ? initialization
                : DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "denoising guide or resolve pipeline creation failed");
        }
#else
        (void)device;
        (void)shaderFactory;
        for (SignalState& signal : m_Signals)
        {
            signal.lastStatus = DenoiserStatus::Error(
                DenoiserStatusCode::Unavailable,
                "NRD support is unavailable in this build");
        }
#endif
    }

    DenoisingPass::~DenoisingPass()
    {
        ReleaseResources();
    }

    DenoisingResult DenoisingPass::ProcessAmbientOcclusion(
        nvrhi::ICommandList* commandList,
        const DenoisingSignalSettings& settings,
        const DenoisingInputs& inputs)
    {
        return Process(DenoiserSignalType::AmbientOcclusion,
            commandList, settings, inputs);
    }

    DenoisingResult DenoisingPass::ProcessDiffuseGi(
        nvrhi::ICommandList* commandList,
        const DenoisingSignalSettings& settings,
        const DenoisingInputs& inputs)
    {
        return Process(DenoiserSignalType::DiffuseGi,
            commandList, settings, inputs);
    }

    DenoisingResult DenoisingPass::ProcessSkyVisibility(
        nvrhi::ICommandList* commandList,
        const DenoisingSignalSettings& settings,
        const DenoisingInputs& inputs)
    {
        return Process(DenoiserSignalType::SkyVisibility,
            commandList, settings, inputs);
    }

    DenoisingResult DenoisingPass::ProcessSunShadow(
        nvrhi::ICommandList* commandList,
        const DenoisingSignalSettings& settings,
        const DenoisingInputs& inputs)
    {
        return Process(DenoiserSignalType::SunShadow,
            commandList, settings, inputs);
    }

    DenoisingResult DenoisingPass::ProcessFlashlightShadow(
        nvrhi::ICommandList* commandList,
        const DenoisingSignalSettings& settings,
        const DenoisingInputs& inputs)
    {
        return Process(DenoiserSignalType::FlashlightShadow,
            commandList, settings, inputs);
    }

    DenoisingResult DenoisingPass::Process(
        DenoiserSignalType type,
        nvrhi::ICommandList* commandList,
        const DenoisingSignalSettings& requestedSettings,
        const DenoisingInputs& inputs)
    {
        SignalState& state = m_Signals[SignalIndex(type)];
        const DenoisingSignalSettings settings = SanitizeDenoisingSettings(
            ToEffect(type), requestedSettings);
        DenoiserMethod method = DenoiserMethod::ReblurDiffuse;
        if (!ResolveMethod(type, settings.method, method))
        {
            ReleaseSignal(state);
            state.lastStatus = DenoiserStatus::Ok();
            return { inputs.rawSignal, false };
        }
        if (!m_FrontEndAvailable || !m_Backend ||
            !m_BackendInitialized ||
            !m_Backend->GetCapabilities().backendAvailable)
        {
            state.lastStatus = DenoiserStatus::Error(
                m_Backend &&
                    !m_Backend->GetCapabilities().backendAvailable
                    ? DenoiserStatusCode::Unavailable
                    : DenoiserStatusCode::InitializationFailed,
                m_Backend &&
                    !m_Backend->GetCapabilities().backendAvailable
                    ? "NRD support is unavailable in this build"
                    : "the denoising front end or NRD backend is unavailable");
            return { inputs.rawSignal, false };
        }

        const dm::uint2 sourceSize =
            inputs.sourceSize.x > 0 && inputs.sourceSize.y > 0
            ? inputs.sourceSize : inputs.signalSize;
        if (!inputs.hitDistanceMatchesSignal)
        {
            ReleaseSignal(state);
            state.lastStatus = DenoiserStatus::Error(
                DenoiserStatusCode::Unsupported,
                "denoising requires a matched scalar signal and hit distance");
            return { inputs.rawSignal, false };
        }
        if (!inputs.hitDistance)
        {
            ReleaseSignal(state);
            state.lastStatus = DenoiserStatus::Error(
                DenoiserStatusCode::Unsupported,
                "enable Output Hit Distance for the selected denoiser");
            return { inputs.rawSignal, false };
        }

        const bool commonInputsValid = commandList &&
            inputs.signalSize.x > 0 && inputs.signalSize.y > 0 &&
            IsSingleSampleTexture(inputs.rawSignal, sourceSize) &&
            IsSingleSampleTexture(inputs.hitDistance, sourceSize) &&
            IsSingleSampleTexture(inputs.depth, inputs.signalSize) &&
            IsSingleSampleTexture(inputs.normalRoughness, inputs.signalSize) &&
            IsSingleSampleTexture(inputs.motionVectors, inputs.signalSize) &&
            inputs.currentView &&
            std::isfinite(inputs.hitDistanceNormalization) &&
            inputs.hitDistanceNormalization > 0.f &&
            std::isfinite(inputs.frameDeltaSeconds) &&
            inputs.frameDeltaSeconds >= 0.f;
        bool lightInputsValid = true;
        if (type == DenoiserSignalType::SunShadow)
        {
            lightInputsValid =
                std::isfinite(inputs.directionalTanAngularRadius) &&
                inputs.directionalTanAngularRadius > 0.f &&
                std::isfinite(inputs.lightDirectionWorld.x) &&
                std::isfinite(inputs.lightDirectionWorld.y) &&
                std::isfinite(inputs.lightDirectionWorld.z) &&
                length(inputs.lightDirectionWorld) > 1e-4f;
        }
        else if (type == DenoiserSignalType::FlashlightShadow)
        {
            lightInputsValid =
                std::isfinite(inputs.localLightPosition.x) &&
                std::isfinite(inputs.localLightPosition.y) &&
                std::isfinite(inputs.localLightPosition.z) &&
                std::isfinite(inputs.localLightRadius) &&
                inputs.localLightRadius >= 0.f;
        }
        if (!commonInputsValid || !lightInputsValid)
        {
            ReleaseSignal(state);
            state.lastStatus = DenoiserStatus::Error(
                DenoiserStatusCode::InvalidArgument,
                "denoising received incomplete or invalid signal inputs");
            return { inputs.rawSignal, false };
        }

        const DenoiserExtent denoiserExtent = CalculateDenoiserExtent(
            { inputs.signalSize.x, inputs.signalSize.y },
            GetDenoisingResolutionScale(settings.resolution));
        if (!denoiserExtent.IsValid())
        {
            state.lastStatus = DenoiserStatus::Error(
                DenoiserStatusCode::InvalidArgument,
                "denoising resolution produced an invalid extent");
            return { inputs.rawSignal, false };
        }
        if (!EnsureResources(state, type, method,
            inputs.signalSize, denoiserExtent))
        {
            state.lastStatus = DenoiserStatus::Error(
                DenoiserStatusCode::InitializationFailed,
                "denoising texture allocation failed");
            return { inputs.rawSignal, false };
        }

        const std::array<nvrhi::ITexture*, 5> inputTextures = {
            inputs.rawSignal,
            inputs.hitDistance,
            inputs.depth,
            inputs.normalRoughness,
            inputs.motionVectors };
        if (state.inputTextures != inputTextures ||
            state.sourceSize.x != sourceSize.x ||
            state.sourceSize.y != sourceSize.y)
        {
            state.bindingCache->Clear();
            if (state.handle)
            {
                (void)m_Backend->RequestHistoryReset(
                    state.handle,
                    DenoiserHistoryReset::ClearAndRestart);
            }
            state.inputTextures = inputTextures;
            state.sourceSize = sourceSize;
        }

        const DenoiserSettings backendSettings = BuildBackendSettings(
            type, method, settings, inputs);
        DenoiserSignalDescription description;
        description.type = type;
        description.method = method;
        description.allocationExtent = denoiserExtent;
        description.viewKey =
            reinterpret_cast<uintptr_t>(inputs.currentView);
        description.hitDistanceAvailable = true;
        description.checkerboard = false;
        if (!state.configured || description != state.description ||
            backendSettings != state.backendSettings)
        {
            state.lastStatus = state.handle
                ? m_Backend->ConfigureSignal(state.handle,
                    description, state.resources, backendSettings)
                : m_Backend->RegisterSignal(description,
                    state.resources, backendSettings, state.handle);
            state.configured = static_cast<bool>(state.lastStatus);
            if (!state.configured)
                return { inputs.rawSignal, false };
            state.description = description;
            state.backendSettings = backendSettings;
        }

        DenoisingConstants constants{};
        inputs.currentView->FillPlanarViewConstants(constants.view);
        constants.fullResolution = float2(inputs.signalSize);
        constants.denoiserResolution = float2(
            denoiserExtent.width, denoiserExtent.height);
        constants.sourceResolution = float2(sourceSize);
        constants.fullResolutionInv = 1.f / constants.fullResolution;
        constants.denoiserResolutionInv = 1.f /
            constants.denoiserResolution;
        constants.sourceResolutionInv = 1.f /
            constants.sourceResolution;
        constants.hitDistanceNormalization =
            inputs.hitDistanceNormalization;
        constants.motionScaleX = float(denoiserExtent.width) /
            float(inputs.signalSize.x);
        constants.motionScaleY = float(denoiserExtent.height) /
            float(inputs.signalSize.y);
        constants.denoisingRange = 500000.f;
        constants.localLightPosition = inputs.localLightPosition;
        constants.localLightRadius = inputs.localLightRadius;
        constants.directionalTanAngularRadius =
            inputs.directionalTanAngularRadius;
        constants.reverseDepth =
            inputs.currentView->IsReverseDepth() ? 1u : 0u;
        constants.method = uint32_t(method);
        constants.signalType = uint32_t(type);
        commandList->writeBuffer(m_ConstantBuffer,
            &constants, sizeof(constants));

        const SignalClass signalClass = GetSignalClass(type, method);
        const Pipeline& prepare =
            m_PreparePipelines[size_t(signalClass)];
        nvrhi::ITexture* preparedSignal = nullptr;
        switch (signalClass)
        {
        case SignalClass::Radiance:
        case SignalClass::ScalarRadiance:
            preparedSignal = state.noisyRadianceHitDistance;
            break;
        default:
            preparedSignal = state.penumbra;
            break;
        }
        nvrhi::BindingSetDesc prepareDesc;
        prepareDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, inputs.rawSignal),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.hitDistance),
            nvrhi::BindingSetItem::Texture_SRV(2, inputs.depth),
            nvrhi::BindingSetItem::Texture_SRV(3, inputs.normalRoughness),
            nvrhi::BindingSetItem::Texture_SRV(4, inputs.motionVectors),
            nvrhi::BindingSetItem::Texture_UAV(0, state.motionVectors),
            nvrhi::BindingSetItem::Texture_UAV(1, state.normalRoughness),
            nvrhi::BindingSetItem::Texture_UAV(2, state.viewZ),
            nvrhi::BindingSetItem::Texture_UAV(3, preparedSignal) };
        nvrhi::BindingSetHandle prepareBinding =
            state.bindingCache->GetOrCreateBindingSet(
                prepareDesc, prepare.bindingLayout);
        if (!prepareBinding)
        {
            state.lastStatus = DenoiserStatus::Error(
                DenoiserStatusCode::InitializationFailed,
                "denoising guide binding creation failed");
            return { inputs.rawSignal, false };
        }
        nvrhi::ComputeState prepareState;
        prepareState.pipeline = prepare.pipeline;
        prepareState.bindings = { prepareBinding };
        commandList->beginMarker("Denoising Guide Preparation");
        commandList->setComputeState(prepareState);
        commandList->dispatch(
            (denoiserExtent.width + 7u) / 8u,
            (denoiserExtent.height + 7u) / 8u, 1u);
        commandList->endMarker();

        state.lastStatus = m_Backend->Execute(
            state.handle, commandList,
            BuildFrameSettings(denoiserExtent, inputs));
        if (!state.lastStatus)
        {
            (void)m_Backend->RequestHistoryReset(
                state.handle, DenoiserHistoryReset::Restart);
            return { inputs.rawSignal, false };
        }

        nvrhi::ITexture* denoisedSignal = nullptr;
        switch (signalClass)
        {
        case SignalClass::Radiance:
        case SignalClass::ScalarRadiance:
            denoisedSignal = state.denoisedRadianceHitDistance;
            break;
        default:
            denoisedSignal = state.shadow;
            break;
        }
        const Pipeline& resolve =
            m_ResolvePipelines[size_t(signalClass)];
        nvrhi::BindingSetDesc resolveDesc;
        resolveDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0, denoisedSignal),
            nvrhi::BindingSetItem::Texture_SRV(1, state.viewZ),
            nvrhi::BindingSetItem::Texture_SRV(2, state.normalRoughness),
            nvrhi::BindingSetItem::Texture_SRV(3, inputs.depth),
            nvrhi::BindingSetItem::Texture_SRV(4, inputs.normalRoughness),
            nvrhi::BindingSetItem::Texture_SRV(5, inputs.rawSignal),
            nvrhi::BindingSetItem::Texture_UAV(0, state.resolved) };
        nvrhi::BindingSetHandle resolveBinding =
            state.bindingCache->GetOrCreateBindingSet(
                resolveDesc, resolve.bindingLayout);
        if (!resolveBinding)
        {
            state.lastStatus = DenoiserStatus::Error(
                DenoiserStatusCode::InitializationFailed,
                "denoising resolve binding creation failed");
            return { inputs.rawSignal, false };
        }
        nvrhi::ComputeState resolveState;
        resolveState.pipeline = resolve.pipeline;
        resolveState.bindings = { resolveBinding };
        commandList->beginMarker("Denoising Resolve");
        commandList->setComputeState(resolveState);
        commandList->dispatch(
            (inputs.signalSize.x + 7u) / 8u,
            (inputs.signalSize.y + 7u) / 8u, 1u);
        commandList->endMarker();
        return { state.resolved.Get(), true };
    }

    bool DenoisingPass::EnsureResources(
        SignalState& state,
        DenoiserSignalType type,
        DenoiserMethod method,
        dm::uint2 fullSize,
        DenoiserExtent extent)
    {
        const bool sameIdentity = state.extent == extent &&
            state.fullSize.x == fullSize.x &&
            state.fullSize.y == fullSize.y &&
            state.method == method;
        if (sameIdentity && state.motionVectors &&
            state.normalRoughness && state.viewZ && state.resolved)
        {
            const SignalClass signalClass = GetSignalClass(type, method);
            if (((signalClass == SignalClass::Radiance ||
                    signalClass == SignalClass::ScalarRadiance) &&
                    state.noisyRadianceHitDistance &&
                    state.denoisedRadianceHitDistance) ||
                (signalClass == SignalClass::Shadow &&
                    state.penumbra && state.shadow))
            {
                return true;
            }
        }

        ReleaseSignal(state);
        state.method = method;
        state.extent = extent;
        state.fullSize = fullSize;

        const auto createTexture = [this](
            uint32_t width,
            uint32_t height,
            nvrhi::Format format,
            const char* debugName)
        {
            nvrhi::TextureDesc desc;
            desc.width = width;
            desc.height = height;
            desc.format = format;
            desc.dimension = nvrhi::TextureDimension::Texture2D;
            desc.mipLevels = 1;
            desc.sampleCount = 1;
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            desc.debugName = debugName;
            return m_Device->createTexture(desc);
        };

        state.motionVectors = createTexture(extent.width, extent.height,
            nvrhi::Format::RGBA16_FLOAT, "Denoising/Motion");
        state.normalRoughness = createTexture(extent.width, extent.height,
            nvrhi::Format::RGBA16_SNORM, "Denoising/NormalRoughness");
        state.viewZ = createTexture(extent.width, extent.height,
            nvrhi::Format::R32_FLOAT, "Denoising/ViewZ");

        const SignalClass signalClass = GetSignalClass(type, method);
        nvrhi::Format resolvedFormat = nvrhi::Format::RGBA16_FLOAT;
        if (signalClass == SignalClass::Radiance ||
            signalClass == SignalClass::ScalarRadiance)
        {
            state.noisyRadianceHitDistance = createTexture(
                extent.width, extent.height, nvrhi::Format::RGBA16_FLOAT,
                "Denoising/NoisyRadianceHitDistance");
            state.denoisedRadianceHitDistance = createTexture(
                extent.width, extent.height, nvrhi::Format::RGBA16_FLOAT,
                "Denoising/DenoisedRadianceHitDistance");
            if (signalClass == SignalClass::ScalarRadiance)
                resolvedFormat = nvrhi::Format::R16_FLOAT;
        }
        else
        {
            state.penumbra = createTexture(
                extent.width, extent.height, nvrhi::Format::R16_FLOAT,
                "Denoising/Penumbra");
            state.shadow = createTexture(
                extent.width, extent.height, nvrhi::Format::R8_UNORM,
                "Denoising/Shadow");
            resolvedFormat = nvrhi::Format::R8_UNORM;
        }
        state.resolved = createTexture(fullSize.x, fullSize.y,
            resolvedFormat, "Denoising/Resolved");

        const bool commonReady = state.motionVectors &&
            state.normalRoughness && state.viewZ && state.resolved;
        const bool signalReady =
            ((signalClass == SignalClass::Radiance ||
                    signalClass == SignalClass::ScalarRadiance) &&
                state.noisyRadianceHitDistance &&
                state.denoisedRadianceHitDistance) ||
            (signalClass == SignalClass::Shadow &&
                state.penumbra && state.shadow);
        if (!commonReady || !signalReady)
        {
            ReleaseSignal(state);
            return false;
        }

        state.resources = {};
        state.resources.motionVectors = state.motionVectors;
        state.resources.normalRoughness = state.normalRoughness;
        state.resources.viewZ = state.viewZ;
        state.resources.noisyRadianceHitDistance =
            state.noisyRadianceHitDistance;
        state.resources.denoisedRadianceHitDistance =
            state.denoisedRadianceHitDistance;
        state.resources.penumbra = state.penumbra;
        state.resources.shadow = state.shadow;
        (void)type;
        return true;
    }

    void DenoisingPass::ReleaseSignal(SignalState& state) noexcept
    {
        const bool hasAllocatedState = state.handle || state.configured ||
            std::any_of(
                state.inputTextures.begin(),
                state.inputTextures.end(),
                [](nvrhi::ITexture* texture) { return texture != nullptr; }) ||
            state.motionVectors || state.normalRoughness || state.viewZ ||
            state.noisyRadianceHitDistance ||
            state.denoisedRadianceHitDistance || state.penumbra || state.shadow ||
            state.resolved;
        if (!hasAllocatedState)
            return;

        // Cached prepare and resolve sets retain texture references. Clearing
        // only this signal's cache releases its allocations without disturbing
        // bindings already recorded for another active signal.
        state.bindingCache->Clear();
        if (m_Backend && state.handle)
            (void)m_Backend->UnregisterSignal(state.handle);
        state.handle = {};
        state.description = {};
        state.backendSettings = {};
        state.configured = false;
        state.extent = {};
        state.fullSize = dm::uint2::zero();
        state.sourceSize = dm::uint2::zero();
        state.inputTextures = {};
        state.motionVectors = nullptr;
        state.normalRoughness = nullptr;
        state.viewZ = nullptr;
        state.noisyRadianceHitDistance = nullptr;
        state.denoisedRadianceHitDistance = nullptr;
        state.penumbra = nullptr;
        state.shadow = nullptr;
        state.resolved = nullptr;
        state.resources = {};
    }

    DenoiserSettings DenoisingPass::BuildBackendSettings(
        DenoiserSignalType type,
        DenoiserMethod method,
        const DenoisingSignalSettings& settings,
        const DenoisingInputs& inputs) const noexcept
    {
        DenoiserSettings result;
        result.quality = DenoiserQuality(uint8_t(settings.quality));
        result.historyLength = method == DenoiserMethod::RelaxDiffuse
            ? std::clamp(settings.historyLength, 1u, 255u)
            : std::clamp(settings.historyLength, 1u, 63u);
        result.disocclusionThreshold = settings.disocclusionThreshold;
        result.antiLagStrength = settings.antiLagStrength;
        result.hitDistanceReconstruction =
            DenoiserHitDistanceReconstruction::Off;
        result.hitDistanceParameters = {
            std::max(inputs.hitDistanceNormalization, 1e-5f),
            1e-6f,
            1.f };
        if (type == DenoiserSignalType::SunShadow)
        {
            const dm::float3 direction = normalize(inputs.lightDirectionWorld);
            result.lightDirectionWorld = {
                direction.x, direction.y, direction.z };
        }
        return result;
    }

    DenoiserFrameSettings DenoisingPass::BuildFrameSettings(
        DenoiserExtent extent,
        const DenoisingInputs& inputs) const noexcept
    {
        DenoiserFrameSettings frame;
        const IView& current = *inputs.currentView;
        const IView& previous = inputs.previousView
            ? *inputs.previousView : current;
        frame.viewToClip = ToDenoiserMatrix(
            current.GetProjectionMatrix(false));
        frame.viewToClipPrev = ToDenoiserMatrix(
            previous.GetProjectionMatrix(false));
        frame.worldToView = ToDenoiserMatrix(
            dm::affineToHomogeneous(current.GetViewMatrix()));
        frame.worldToViewPrev = ToDenoiserMatrix(
            dm::affineToHomogeneous(previous.GetViewMatrix()));

        const float scaleX = float(extent.width) /
            std::max(float(inputs.signalSize.x), 1.f);
        const float scaleY = float(extent.height) /
            std::max(float(inputs.signalSize.y), 1.f);
        const dm::float2 jitter = current.GetPixelOffset();
        const dm::float2 jitterPrev = previous.GetPixelOffset();
        frame.cameraJitterPixels = {
            std::clamp(jitter.x * scaleX, -0.5f, 0.5f),
            std::clamp(jitter.y * scaleY, -0.5f, 0.5f) };
        frame.cameraJitterPixelsPrev = {
            std::clamp(jitterPrev.x * scaleX, -0.5f, 0.5f),
            std::clamp(jitterPrev.y * scaleY, -0.5f, 0.5f) };
        frame.rectExtent = extent;
        frame.rectExtentPrev = extent;
        frame.viewZScale = 1.f;
        frame.timeDeltaMilliseconds = std::clamp(
            inputs.frameDeltaSeconds * 1000.f, 0.f, 1000.f);
        frame.denoisingRange = 500000.f;
        frame.frameIndex = inputs.frameIndex;
        return frame;
    }

    void DenoisingPass::DisableSignal(DenoiserSignalType type) noexcept
    {
        SignalState& state = m_Signals[SignalIndex(type)];
        ReleaseSignal(state);
        state.lastStatus = DenoiserStatus::Ok();
    }

    void DenoisingPass::RequestHistoryReset(
        DenoiserHistoryReset reset) noexcept
    {
        if (!m_Backend)
            return;
        for (SignalState& signal : m_Signals)
        {
            if (signal.handle)
                (void)m_Backend->RequestHistoryReset(signal.handle, reset);
        }
    }

    void DenoisingPass::ReleaseResources() noexcept
    {
        for (SignalState& signal : m_Signals)
            ReleaseSignal(signal);
        if (m_Backend && m_BackendInitialized)
            m_Backend->Shutdown();
        m_BackendInitialized = false;
    }

    const DenoiserBackendCapabilities& DenoisingPass::GetCapabilities()
        const noexcept
    {
        static const DenoiserBackendCapabilities unavailable{};
        return m_Backend ? m_Backend->GetCapabilities() : unavailable;
    }

    const DenoiserStatus& DenoisingPass::GetLastStatus(
        DenoiserSignalType type) const noexcept
    {
        return m_Signals[SignalIndex(type)].lastStatus;
    }

    DenoiserMemoryStats DenoisingPass::GetBackendMemoryStats() const noexcept
    {
        return m_Backend ? m_Backend->GetMemoryStats() : DenoiserMemoryStats{};
    }

    uint64_t DenoisingPass::GetCallerOwnedBytes() const noexcept
    {
        uint64_t result = 0;
        for (const SignalState& state : m_Signals)
        {
            const std::array<nvrhi::TextureHandle, 8> textures = {
                state.motionVectors,
                state.normalRoughness,
                state.viewZ,
                state.noisyRadianceHitDistance,
                state.denoisedRadianceHitDistance,
                state.penumbra,
                state.shadow,
                state.resolved };
            for (const nvrhi::TextureHandle& texture : textures)
            {
                const uint64_t bytes = GetTextureBytes(texture);
                result = bytes > std::numeric_limits<uint64_t>::max() - result
                    ? std::numeric_limits<uint64_t>::max()
                    : result + bytes;
            }
        }
        return result;
    }

    bool DenoisingPass::IsOperational() const noexcept
    {
        return m_FrontEndAvailable && m_BackendInitialized && m_Backend &&
            GetCapabilities().backendAvailable;
    }

    size_t DenoisingPass::SignalIndex(DenoiserSignalType type) noexcept
    {
        const size_t index = size_t(type);
        return index < c_SignalCount ? index : 0;
    }

    DenoisingPass::SignalClass DenoisingPass::GetSignalClass(
        DenoiserSignalType type,
        DenoiserMethod method) noexcept
    {
        if (type == DenoiserSignalType::AmbientOcclusion ||
            type == DenoiserSignalType::SkyVisibility)
            return SignalClass::ScalarRadiance;
        switch (method)
        {
        case DenoiserMethod::SigmaShadow:
            return SignalClass::Shadow;
        default:
            return SignalClass::Radiance;
        }
    }
}
