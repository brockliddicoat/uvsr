#include "svgf_pass.h"
#include "svgf_constants.h"
#include "renderer_shader_factory.h"
#include <donut/engine/View.h>

namespace uvsr
{
    static_assert(sizeof(SvgfConstants) % 16u == 0u);

    SvgfPass::SvgfPass(nvrhi::IDevice* device, const std::shared_ptr<RendererShaderFactory>& shaders)
        : m_Device(device)
    {
        if (!device || !shaders) return;
        nvrhi::BufferDesc cb;
        cb.byteSize = sizeof(SvgfConstants);
        cb.debugName = "SVGF/constants";
        cb.isConstantBuffer = true;
        cb.isVolatile = true;
        cb.maxVersions = 512;
        m_Constants = device->createBuffer(cb);
        nvrhi::BindingLayoutDesc layout;
        layout.visibility = nvrhi::ShaderType::Compute;
        layout.bindings = { nvrhi::BindingLayoutItem::VolatileConstantBuffer(0) };
        for (uint32_t i = 0; i < 10; ++i) layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(i));
        layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_UAV(0));
        layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_UAV(1));
        m_Layout = device->createBindingLayout(layout);
        const char* files[] = { "uvsr/svgf_prepare_cs.hlsl", "uvsr/svgf_reproject_cs.hlsl",
            "uvsr/svgf_moments_cs.hlsl", "uvsr/svgf_atrous_cs.hlsl", "uvsr/svgf_output_cs.hlsl" };
        m_Ready = m_Constants && m_Layout;
        for (uint32_t i = 0; i < m_Pipelines.size(); ++i)
        {
            auto shader = shaders->CreateShader(files[i], "main", nullptr, nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc desc;
            desc.CS = shader;
            desc.bindingLayouts = { m_Layout };
            if (shader) m_Pipelines[i].handle = device->createComputePipeline(desc);
            m_Ready = m_Ready && bool(m_Pipelines[i].handle);
        }
        m_Zero = Texture({1, 1}, nvrhi::Format::RGBA32_FLOAT, "SVGF/unused input");
        m_Ready = m_Ready && bool(m_Zero);
    }

    nvrhi::TextureHandle SvgfPass::Texture(dm::uint2 extent, nvrhi::Format format, const char* name)
    {
        nvrhi::TextureDesc desc;
        desc.width = extent.x;
        desc.height = extent.y;
        desc.format = format;
        desc.isUAV = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        desc.keepInitialState = true;
        desc.debugName = name;
        return m_Device->createTexture(desc);
    }

    bool SvgfPass::Dispatch(Signal& signal, nvrhi::ICommandList* command, uint32_t stage, const void* constants,
        const std::array<nvrhi::ITexture*, 10>& inputs, nvrhi::ITexture* output,
        nvrhi::ITexture* secondOutput, dm::uint2 extent)
    {
        std::array<nvrhi::ITexture*, 12> textures{};
        for (uint32_t i = 0; i < inputs.size(); ++i)
            textures[i] = inputs[i] ? inputs[i] : m_Zero.Get();
        textures[10] = output;
        textures[11] = secondOutput ? secondOutput : output;
        nvrhi::BindingSetHandle set;
        for (const auto& cached : signal.bindings)
            if (cached.textures == textures) { set = cached.set; break; }
        if (!set)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings.reserve(13);
            bindings.bindings = { nvrhi::BindingSetItem::ConstantBuffer(0, m_Constants) };
            for (uint32_t i = 0; i < inputs.size(); ++i)
                bindings.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(i, textures[i]));
            bindings.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(0, textures[10]));
            bindings.bindings.push_back(nvrhi::BindingSetItem::Texture_UAV(1, textures[11]));
            set = m_Device->createBindingSet(bindings, m_Layout);
            if (set) signal.bindings.push_back({textures, set});
        }
        if (!set) return false;
        command->writeBuffer(m_Constants, constants, sizeof(SvgfConstants));
        nvrhi::ComputeState state;
        state.pipeline = m_Pipelines[stage].handle;
        state.bindings = { set };
        command->setComputeState(state);
        command->dispatch((extent.x + 7u) / 8u, (extent.y + 7u) / 8u);
        return true;
    }

    SvgfResult SvgfPass::Process(DenoiserSignalType type, nvrhi::ICommandList* command,
        const DenoisingSignalSettings& requested, const SvgfInputs& inputs)
    {
        auto& signal = m_Signals[size_t(type)];
        const auto fail = [&]() -> SvgfResult { signal.valid = false; return {inputs.radiance, false}; };
        if (requested.method == DenoisingMethodChoice::Raw) { DisableSignal(type); return {inputs.radiance, false}; }
        if (!m_Ready || !command || !inputs.radiance || !inputs.deviceDepth || !inputs.worldNormal || !inputs.view)
            return fail();
        const auto& rawDesc = inputs.radiance->getDesc();
        const auto& depthDesc = inputs.deviceDepth->getDesc();
        const dm::uint2 extent(rawDesc.width, rawDesc.height), surfaceExtent(depthDesc.width, depthDesc.height);
        const auto matches = [&](nvrhi::ITexture* texture) {
            return texture && texture->getDesc().width == surfaceExtent.x && texture->getDesc().height == surfaceExtent.y &&
                texture->getDesc().sampleCount == 1;
        };
        if (!extent.x || !extent.y || !inputs.sampleStride.x || !inputs.sampleStride.y || rawDesc.sampleCount != 1 ||
            !matches(inputs.worldNormal) || !matches(inputs.derivatives) || (inputs.pixelMotion && !matches(inputs.pixelMotion)) ||
            (inputs.albedo && !matches(inputs.albedo)) || (inputs.emission && !matches(inputs.emission)) ||
            bool(inputs.albedo) != bool(inputs.emission) ||
            (surfaceExtent.x + inputs.sampleStride.x - 1u) / inputs.sampleStride.x != extent.x ||
            (surfaceExtent.y + inputs.sampleStride.y - 1u) / inputs.sampleStride.y != extent.y) return fail();
        const auto settings = SanitizeDenoisingSettings(DenoisingEffect::DiffuseGi, requested);
        const bool scalar = type != DenoiserSignalType::DiffuseGi && type != DenoiserSignalType::PathTracing;
        const bool material = inputs.albedo != nullptr;
        const std::array<nvrhi::ITexture*, 4> guideInputs = {
            inputs.deviceDepth, inputs.worldNormal, inputs.pixelMotion, inputs.derivatives };
        const auto matchesGuides = [&](const std::shared_ptr<Guides>& candidate) {
            return candidate && candidate->inputs == guideInputs && dm::all(candidate->extent == extent) &&
                dm::all(candidate->surfaceExtent == surfaceExtent) && dm::all(candidate->stride == inputs.sampleStride);
        };
        if (!matchesGuides(signal.guides) || signal.material != material || !signal.temporal ||
            !signal.moments[0] || !signal.moments[1] || !signal.filtered[0] ||
            !signal.filtered[1] || !signal.filtered[2] || (material && !signal.output))
        {
            signal = {};
            signal.extent = extent;
            signal.material = material;
            m_Guides.erase(std::remove_if(m_Guides.begin(), m_Guides.end(),
                [](const auto& guide) { return guide.use_count() == 1; }), m_Guides.end());
            for (const auto& candidate : m_Guides)
                if (matchesGuides(candidate)) { signal.guides = candidate; break; }
            if (!signal.guides)
            {
                auto candidate = std::make_shared<Guides>();
                candidate->inputs = guideInputs;
                candidate->extent = extent;
                candidate->surfaceExtent = surfaceExtent;
                candidate->stride = inputs.sampleStride;
                for (auto& image : candidate->surface)
                    image = Texture(extent, nvrhi::Format::RGBA32_FLOAT, "SVGF/shared linear depth and normal");
                candidate->reprojection = Texture(extent, nvrhi::Format::RGBA32_FLOAT, "SVGF/shared previous correspondence");
                if (!candidate->surface[0] || !candidate->surface[1] || !candidate->reprojection) return fail();
                signal.guides = candidate;
                m_Guides.push_back(candidate);
            }
            const auto illuminationFormat = scalar ? nvrhi::Format::RG32_FLOAT : nvrhi::Format::RGBA32_FLOAT;
            for (auto& image : signal.moments) image = Texture(extent, nvrhi::Format::RGBA32_FLOAT, "SVGF/moments and history length");
            for (auto& image : signal.filtered) image = Texture(extent, illuminationFormat, "SVGF/wavelet illumination");
            signal.temporal = Texture(extent, illuminationFormat, "SVGF/temporal illumination");
            if (material) signal.output = Texture(extent, nvrhi::Format::RGBA32_FLOAT, "SVGF/material output");
        }
        for (const auto& image : signal.moments) if (!image) return fail();
        for (const auto& image : signal.filtered) if (!image) return fail();
        if (!signal.temporal || (material && !signal.output)) return fail();
        auto& guides = *signal.guides;
        const std::array<nvrhi::ITexture*, 7> externalInputs = { inputs.radiance, inputs.deviceDepth,
            inputs.worldNormal, inputs.derivatives, inputs.pixelMotion, inputs.albedo, inputs.emission };
        const bool inputsChanged = signal.inputs != externalInputs;
        if (inputsChanged) { signal.bindings.clear(); signal.inputs = externalInputs; }
        if (!m_Initialized) { command->clearTextureFloat(m_Zero, nvrhi::AllSubresources, nvrhi::Color(0.f)); m_Initialized = true; }
        SvgfConstants cb{};
        inputs.view->FillPlanarViewConstants(cb.currentView);
        (inputs.previousView ? inputs.previousView : inputs.view)->FillPlanarViewConstants(cb.previousView);
        cb.extent = {extent.x, extent.y};
        cb.surfaceExtent = {surfaceExtent.x, surfaceExtent.y};
        cb.sampleStride = {inputs.sampleStride.x, inputs.sampleStride.y};
        const bool reset = inputsChanged || !signal.valid || signal.frame + 1u != inputs.frame || signal.settings != settings ||
            !inputs.previousView || !inputs.pixelMotion;
        cb.alpha = settings.alpha;
        cb.momentsAlpha = settings.momentsAlpha;
        cb.phiColor = settings.colorPhi;
        cb.phiNormal = settings.normalPhi;
        cb.scalar = scalar;
        cb.material = inputs.albedo != nullptr;
        cb.reverseDepth = inputs.view->IsReverseDepth();
        const uint32_t current = signal.current ^ 1u;
        if (reset)
        {
            command->clearTextureFloat(signal.filtered[signal.feedback], nvrhi::AllSubresources, nvrhi::Color(0.f));
            command->clearTextureFloat(signal.moments[signal.current], nvrhi::AllSubresources, nvrhi::Color(0.f));
        }
        std::array<nvrhi::ITexture*, 10> bindings{};
        bindings[0] = inputs.deviceDepth;
        bindings[1] = inputs.worldNormal;
        bindings[2] = inputs.pixelMotion;
        bindings[3] = inputs.derivatives;
        command->beginMarker("SVGF");
        bool ok = true;
        if (guides.frame != inputs.frame)
        {
            cb.reset = guides.frame == ~uint64_t(0) || guides.frame + 1u != inputs.frame ||
                !inputs.previousView || !inputs.pixelMotion;
            const uint32_t write = guides.current ^ 1u;
            if (cb.reset)
                command->clearTextureFloat(guides.surface[guides.current], nvrhi::AllSubresources, nvrhi::Color(-1.f));
            ok = Dispatch(signal, command, 0, &cb, bindings, guides.surface[write], guides.reprojection, extent);
            if (ok) { guides.current = write; guides.frame = inputs.frame; }
        }
        cb.reset = reset;
        bindings = {inputs.radiance, guides.surface[guides.current], guides.surface[guides.current ^ 1u], guides.reprojection,
            signal.filtered[signal.feedback], signal.moments[signal.current], inputs.albedo, inputs.emission, nullptr, nullptr};
        if (ok) ok = Dispatch(signal, command, 1, &cb, bindings, signal.temporal, signal.moments[current], extent);
        bindings[0] = signal.temporal;
        bindings[4] = nullptr;
        bindings[5] = signal.moments[current];
        if (ok) ok = Dispatch(signal, command, 2, &cb, bindings, signal.filtered[0], nullptr, extent);
        uint32_t read = 0;
        uint32_t feedback = 3;
        for (uint32_t i = 0; ok && i < settings.iterations; ++i)
        {
            cb.stepSize = 1u << i;
            bindings[0] = signal.filtered[read];
            uint32_t write = 0;
            while (write == read || write == feedback) ++write;
            ok = Dispatch(signal, command, 3, &cb, bindings, signal.filtered[write], nullptr, extent);
            read = write;
            // preserve the selected tap by giving later iterations the other
            // two textures. next frame consumes it before reusing that storage.
            if (i == std::min(settings.feedbackTap, settings.iterations - 1u)) feedback = read;
        }
        bindings[0] = signal.filtered[read];
        if (ok && material) ok = Dispatch(signal, command, 4, &cb, bindings, signal.output, nullptr, extent);
        command->endMarker();
        if (!ok) return fail();
        signal.current = current;
        signal.frame = inputs.frame;
        signal.settings = settings;
        signal.valid = true;
        signal.feedback = feedback;
        signal.result = read;
        return {material ? signal.output.Get() : signal.filtered[read].Get(), true};
    }

    void SvgfPass::DisableSignal(DenoiserSignalType type) noexcept
    {
        m_Signals[size_t(type)] = {};
        m_Guides.erase(std::remove_if(m_Guides.begin(), m_Guides.end(),
            [](const auto& guide) { return guide.use_count() == 1; }), m_Guides.end());
    }
    void SvgfPass::RequestHistoryReset() noexcept { for (auto& signal : m_Signals) signal.valid = false; }
    void SvgfPass::ReleaseResources() noexcept { m_Signals = {}; m_Guides.clear(); }
    nvrhi::ITexture* SvgfPass::DebugImage(DenoiserSignalType type, SvgfDebugImage image) const noexcept
    {
        const auto& signal = m_Signals[size_t(type)];
        switch (image)
        {
        case SvgfDebugImage::Surface: return signal.guides ? signal.guides->surface[signal.guides->current].Get() : nullptr;
        case SvgfDebugImage::Reprojection: return signal.guides ? signal.guides->reprojection.Get() : nullptr;
        case SvgfDebugImage::Moments: return signal.moments[signal.current];
        case SvgfDebugImage::Temporal: return signal.temporal;
        case SvgfDebugImage::Feedback: return signal.filtered[signal.feedback];
        case SvgfDebugImage::Output: return signal.material ? signal.output.Get() : signal.filtered[signal.result].Get();
        default: return nullptr;
        }
    }
}
