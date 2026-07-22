#include "taa_miniengine.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include <donut/shaders/view_cb.h>

namespace
{
    // MiniEngine source:
    // Microsoft/DirectX-Graphics-Samples
    // commit 357ade6ec6ff0d9dcadc48f35c7a28e37c0cdf7a
    //
    // The scalar defaults remain verbatim: maximum temporal lerp 1.0, speed
    // limit 64 pixels, and sharpness 0.5. The projection is an integration-only
    // addition that lets the shader express MiniEngine's forward-linear-depth
    // disocclusion inequality over UVSR's infinite reverse-Z device depth.
    struct alignas(16) MiniEngineTaaBlendConstants
    {
        float4x4 projection;
        float2 reciprocalBufferDimensions;
        float temporalBlendFactor;
        float reciprocalSpeedLimiter;
        float2 currentJitter;
        float2 currentToPreviousJitter;
        uint2 bufferDimensions;
        uint32_t historyValid;
        uint32_t dispatchGroupYOffset;
        float sourceDepthPairQuantizationError;
        float maximumHistoryWeight;
        uint32_t depthQuantizationPadding1;
        uint32_t depthQuantizationPadding2;
#if UVSR_AA_DEVELOPER_OVERRIDES
        PlanarViewConstants currentView;
        PlanarViewConstants immediateHistoryView;
        PlanarViewConstants persistentHistoryView0;
        PlanarViewConstants persistentHistoryView1;
        uint32_t persistentValidMask;
        uint32_t persistentPadding0;
        uint32_t persistentPadding1;
        uint32_t persistentPadding2;
#endif
    };

    struct alignas(16) MiniEngineTaaOutputConstants
    {
        float centerWeight;
        float lateralWeight;
        uint2 bufferDimensions;
    };

#if UVSR_AA_DEVELOPER_OVERRIDES
    static_assert(
        sizeof(MiniEngineTaaBlendConstants) ==
        128u + 4u * sizeof(PlanarViewConstants) + 16u);
#else
    static_assert(sizeof(MiniEngineTaaBlendConstants) == 128u);
#endif
    static_assert(
        offsetof(
            MiniEngineTaaBlendConstants,
            sourceDepthPairQuantizationError) == 112u);
    static_assert(sizeof(MiniEngineTaaOutputConstants) == 16u);

#if UVSR_AA_DEVELOPER_OVERRIDES
    std::shared_ptr<PlanarView> CapturePlanarView(
        const IView& source)
    {
        const nvrhi::ViewportState viewportState =
            source.GetViewportState();
        if (viewportState.viewports.empty())
            return nullptr;

        auto result = std::make_shared<PlanarView>();
        result->SetViewport(viewportState.viewports.front());
        result->SetVariableRateShadingState(
            source.GetVariableRateShadingState());
        result->SetMatrices(
            source.GetViewMatrix(),
            source.GetProjectionMatrix(false));
        result->SetPixelOffset(source.GetPixelOffset());
        result->SetArraySlice(static_cast<int>(
            source.GetSubresources().baseArraySlice));
        result->UpdateCache();
        return result;
    }
#endif
}

namespace uvsr
{
    MiniEngineTemporalAAPass::MiniEngineTemporalAAPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        nvrhi::ITexture* sceneColor,
        nvrhi::ITexture* currentDepth,
        nvrhi::ITexture* motionVectors,
        bool enableMomentHistory)
        : m_Device(device)
        , m_ShaderFactory(shaderFactory)
        , m_SceneColor(sceneColor)
        , m_SourceDepthPairQuantizationError(
            currentDepth
                ? GetTemporalAaSourceDepthPairQuantizationError(
                    currentDepth->getDesc().format)
                : 0.f)
        , m_MomentHistoryEnabled(enableMomentHistory)
    {
        const nvrhi::TextureDesc& sceneColorDesc = sceneColor->getDesc();
        m_Size = uint2(sceneColorDesc.width, sceneColorDesc.height);
        // Developer pixel-path framebuffers expose the same seven MRTs as the
        // compute ABI. D3D12 requires every attachment to have identical
        // dimensions, so the otherwise inert moment pair must remain full
        // size in developer builds even when Stable Interior is off.
        const bool allocateFullSizeMoments =
            m_MomentHistoryEnabled ||
            UVSR_AA_DEVELOPER_OVERRIDES != 0;
        m_Timings.historyTextureBytes =
            GetMiniEngineTaaHistoryBytes(
                m_Size.x,
                m_Size.y,
                allocateFullSizeMoments);
#if UVSR_AA_DEVELOPER_OVERRIDES
        m_Timings.historyTextureBytes +=
            GetMiniEngineTaaPersistentHistoryBytes(
                m_Size.x,
                m_Size.y);
        m_Timings.debugTextureBytes =
            GetMiniEngineTaaDebugBytes(m_Size.x, m_Size.y);
#endif

        TemporalHistoryDesc temporalHistoryDesc;
        temporalHistoryDesc.size = m_Size;
        temporalHistoryDesc.debugName =
            "MiniEngineTAA/TemporalHistory";
        temporalHistoryDesc.colorUnorderedAccess = true;
        temporalHistoryDesc.depthUnorderedAccess = true;
#if UVSR_AA_DEVELOPER_OVERRIDES
        temporalHistoryDesc.colorRenderTarget = true;
        temporalHistoryDesc.depthRenderTarget = true;
#endif
        temporalHistoryDesc.maximumAccumulation = 65504u;
        m_History.Initialize(device, temporalHistoryDesc);

        nvrhi::TextureDesc historyDesc;
        historyDesc.width = m_Size.x;
        historyDesc.height = m_Size.y;
        historyDesc.dimension = nvrhi::TextureDimension::Texture2D;
        historyDesc.mipLevels = 1u;
        historyDesc.isUAV = true;
#if UVSR_AA_DEVELOPER_OVERRIDES
        historyDesc.isRenderTarget = true;
#endif
        historyDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        historyDesc.keepInitialState = true;
        // The shared binding layout retains t5/u2 in every static permutation.
        // Release compute-only presets can bind two inert 1x1 texels while
        // Stable Interior is inactive. Developer builds keep full dimensions
        // for the image-equivalent fullscreen-pixel MRT topology above.
        historyDesc.width = allocateFullSizeMoments ? m_Size.x : 1u;
        historyDesc.height = allocateFullSizeMoments ? m_Size.y : 1u;
        historyDesc.format = nvrhi::Format::RG16_FLOAT;
        historyDesc.debugName = "MiniEngineTAA/MomentHistory0";
        m_MomentHistory[0] = device->createTexture(historyDesc);
        historyDesc.debugName = "MiniEngineTAA/MomentHistory1";
        m_MomentHistory[1] = device->createTexture(historyDesc);
        historyDesc.width = m_Size.x;
        historyDesc.height = m_Size.y;

#if UVSR_AA_DEVELOPER_OVERRIDES
        historyDesc.format = nvrhi::Format::RGBA16_FLOAT;
        historyDesc.debugName = "MiniEngineTAA/PersistentColor0";
        m_PersistentColor[0] = device->createTexture(historyDesc);
        historyDesc.debugName = "MiniEngineTAA/PersistentColor1";
        m_PersistentColor[1] = device->createTexture(historyDesc);
        historyDesc.format = nvrhi::Format::R32_FLOAT;
        historyDesc.debugName = "MiniEngineTAA/PersistentDepth0";
        m_PersistentDepth[0] = device->createTexture(historyDesc);
        historyDesc.debugName = "MiniEngineTAA/PersistentDepth1";
        m_PersistentDepth[1] = device->createTexture(historyDesc);
#endif

#if UVSR_AA_DEVELOPER_OVERRIDES
        historyDesc.format = nvrhi::Format::RG16_FLOAT;
        historyDesc.debugName = "MiniEngineTAA/DebugValues";
        m_DebugValues = device->createTexture(historyDesc);
#endif
        historyDesc.format = sceneColorDesc.format;
        historyDesc.debugName = "MiniEngineTAA/FusedOutput";
        m_FusedOutput = device->createTexture(historyDesc);
        historyDesc.format = sceneColorDesc.format;
        historyDesc.debugName = "MiniEngineTAA/SelectiveMorphologyCurrent";
        m_SelectiveCurrent = device->createTexture(historyDesc);
        historyDesc.format = nvrhi::Format::R16_FLOAT;
        historyDesc.debugName = "MiniEngineTAA/SelectiveMorphologyRejection";
        m_SelectiveRejection = device->createTexture(historyDesc);
#if !UVSR_AA_DEVELOPER_OVERRIDES
        // Shipping blend shaders compile debug output away. Reuse the
        // selective rejection allocation for the inert ABI slots instead of
        // carrying a dedicated debug texture in release builds.
        m_DebugValues = m_SelectiveRejection;
#endif

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(MiniEngineTaaBlendConstants);
        constantBufferDesc.debugName = "MiniEngineTAA/BlendConstants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_BlendConstantBuffer = device->createBuffer(constantBufferDesc);

        constantBufferDesc.byteSize = sizeof(MiniEngineTaaOutputConstants);
        constantBufferDesc.debugName = "MiniEngineTAA/OutputConstants";
        m_OutputConstantBuffer = device->createBuffer(constantBufferDesc);

        // NVRHI's default sampler is MiniEngine's s0 contract: min/mag/mip
        // linear with clamp addressing.
        m_LinearClampSampler = commonPasses->m_LinearClampSampler;

        nvrhi::BindingLayoutDesc blendLayoutDesc;
        blendLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        blendLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_SRV(5),
            nvrhi::BindingLayoutItem::Texture_SRV(6),
            nvrhi::BindingLayoutItem::Texture_SRV(7),
            nvrhi::BindingLayoutItem::Texture_SRV(8),
            nvrhi::BindingLayoutItem::Texture_SRV(9),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2),
            nvrhi::BindingLayoutItem::Texture_UAV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(5),
            nvrhi::BindingLayoutItem::Texture_UAV(6)
        };
        m_BlendBindingLayout = device->createBindingLayout(blendLayoutDesc);

        auto createBlendPermutation =
            [&](const MiniEngineTaaOptions& options,
                uint32_t exportSelective,
                uint32_t sampleResurrection,
                const MiniEngineTaaStaticPerformanceOptions&
                    performance,
                nvrhi::ShaderHandle& shader,
                nvrhi::ComputePipelineHandle& pipeline)
        {
            CreateBlendComputePermutation(
                options,
                exportSelective,
                sampleResurrection,
                performance,
                shader,
                pipeline);
        };

#if UVSR_AA_DEVELOPER_OVERRIDES
        // Developer permutations are compiled into the shader package but
        // their D3D12 PSOs are created on first use. Eagerly materializing the
        // complete matrix stalls startup for more than a minute and consumes
        // over a gigabyte before the first frame.
#else
        const MiniEngineTaaStaticPerformanceOptions
            baselinePerformance{};
        constexpr std::array<AntiAliasingPreset, 4>
            productionPresets = {
                AntiAliasingPreset::TemporalPerformance,
                AntiAliasingPreset::TemporalBalanced,
                AntiAliasingPreset::TemporalQuality,
                AntiAliasingPreset::TemporalUltra
            };
        for (AntiAliasingPreset preset : productionPresets)
        {
            const MiniEngineTaaOptions options =
                GetPresetTemporalOptions(preset);
            // Selective morphology is no longer part of the shipping path.
            constexpr uint32_t exportSelective = 0u;
            for (uint32_t fused = 0u;
                fused < 2u;
                ++fused)
            {
                MiniEngineTaaStaticPerformanceOptions performance =
                    baselinePerformance;
                performance.fusedOutput = fused != 0u;
                const uint32_t permutation =
                    GetMiniEngineTaaBlendPermutationIndex(
                        options) *
                        (2u *
                            MiniEngineTaaSampleResurrectionCount *
                            2u) +
                    exportSelective *
                        (MiniEngineTaaSampleResurrectionCount *
                            2u) +
                    fused;
                createBlendPermutation(
                    options,
                    exportSelective,
                    0u,
                    performance,
                    m_BlendShaders[permutation],
                    m_BlendPipelines[permutation]);
            }
        }
#endif

#if UVSR_AA_DEVELOPER_OVERRIDES
        // Performance-experiment PSOs use the same lazy creation policy.
#endif

#if UVSR_AA_DEVELOPER_OVERRIDES
        m_FullscreenVS = commonPasses->m_FullscreenVS;
        nvrhi::BindingLayoutDesc pixelBlendLayout;
        pixelBlendLayout.visibility = nvrhi::ShaderType::Pixel;
        pixelBlendLayout.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_SRV(5)
        };
        m_PixelBlendBindingLayout =
            device->createBindingLayout(pixelBlendLayout);

        for (uint32_t source = 0u; source < 2u; ++source)
        {
            const uint32_t destination = source ^ 1u;
            m_PixelBlendFramebuffers[source] =
                device->createFramebuffer(
                    nvrhi::FramebufferDesc()
                        .addColorAttachment(
                            m_History.Color(destination))
                        .addColorAttachment(
                            m_History.Depth(destination))
                        .addColorAttachment(
                            m_MomentHistory[destination])
                        .addColorAttachment(m_DebugValues)
                        .addColorAttachment(m_SelectiveCurrent)
                        .addColorAttachment(m_SelectiveRejection)
                        .addColorAttachment(m_FusedOutput));
        }

        // Pixel-path PSOs are likewise created only when selected.
#endif

        nvrhi::BindingLayoutDesc outputLayoutDesc;
        outputLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        outputLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(0)
        };
        m_OutputBindingLayout = device->createBindingLayout(outputLayoutDesc);

        nvrhi::ComputePipelineDesc outputPipelineDesc;
        outputPipelineDesc.bindingLayouts = { m_OutputBindingLayout };
#if UVSR_AA_DEVELOPER_OVERRIDES
        constexpr uint32_t compiledDebugViewCount =
            MiniEngineTaaResolveDebugViewCount;
#else
        constexpr uint32_t compiledDebugViewCount = 1u;
#endif
        for (uint32_t debugView = 0u;
            debugView < compiledDebugViewCount;
            ++debugView)
        {
            std::vector<ShaderMacro> macros = {
                { "TAA_DEBUG_VIEW", std::to_string(debugView) }
            };
            m_ResolveShaders[debugView] = shaderFactory->CreateShader(
                "uvsr/taa_miniengine_resolve_cs.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Compute);
            outputPipelineDesc.CS = m_ResolveShaders[debugView];
            m_ResolvePipelines[debugView] =
                device->createComputePipeline(outputPipelineDesc);
        }
        const auto createSharpenShader =
            [&](bool premultipliedInput)
        {
            std::vector<ShaderMacro> macros = {
                { "TAA_SHARPEN_INPUT_PREMULTIPLIED",
                    premultipliedInput ? "1" : "0" }
            };
            return shaderFactory->CreateShader(
                "uvsr/taa_miniengine_sharpen_cs.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Compute);
        };
        m_SharpenShader = createSharpenShader(true);
        outputPipelineDesc.CS = m_SharpenShader;
        m_SharpenPipeline =
            device->createComputePipeline(outputPipelineDesc);
        m_PresentationSharpenShader = createSharpenShader(false);
        outputPipelineDesc.CS = m_PresentationSharpenShader;
        m_PresentationSharpenPipeline =
            device->createComputePipeline(outputPipelineDesc);

        for (uint32_t source = 0u; source < 2u; ++source)
        {
            const uint32_t destination = source ^ 1u;
            nvrhi::BindingSetDesc blendBindings;
            blendBindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(1, m_BlendConstantBuffer),
                nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
                nvrhi::BindingSetItem::Texture_SRV(0, motionVectors),
                nvrhi::BindingSetItem::Texture_SRV(1, sceneColor),
                nvrhi::BindingSetItem::Texture_SRV(
                    2, m_History.Color(source)),
                nvrhi::BindingSetItem::Texture_SRV(3, currentDepth),
                nvrhi::BindingSetItem::Texture_SRV(
                    4, m_History.Depth(source)),
                nvrhi::BindingSetItem::Texture_SRV(
                    5, m_MomentHistory[source]),
#if UVSR_AA_DEVELOPER_OVERRIDES
                nvrhi::BindingSetItem::Texture_SRV(
                    6, m_PersistentColor[0]),
                nvrhi::BindingSetItem::Texture_SRV(
                    7, m_PersistentDepth[0]),
                nvrhi::BindingSetItem::Texture_SRV(
                    8, m_PersistentColor[1]),
                nvrhi::BindingSetItem::Texture_SRV(
                    9, m_PersistentDepth[1]),
#else
                // Production compiles resurrection out and aliases these
                // otherwise-unused binding slots instead of allocating its
                // developer-only history layout.
                nvrhi::BindingSetItem::Texture_SRV(
                    6, m_History.Color(source)),
                nvrhi::BindingSetItem::Texture_SRV(
                    7, m_History.Depth(source)),
                nvrhi::BindingSetItem::Texture_SRV(
                    8, m_History.Color(source)),
                nvrhi::BindingSetItem::Texture_SRV(
                    9, m_History.Depth(source)),
#endif
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_History.Color(destination)),
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_History.Depth(destination)),
                nvrhi::BindingSetItem::Texture_UAV(
                    2, m_MomentHistory[destination]),
                nvrhi::BindingSetItem::Texture_UAV(
                    3, m_DebugValues),
                nvrhi::BindingSetItem::Texture_UAV(
                    4, m_SelectiveCurrent),
                nvrhi::BindingSetItem::Texture_UAV(
                    5, m_SelectiveRejection)
                ,
                nvrhi::BindingSetItem::Texture_UAV(
                    6, m_FusedOutput)
            };
            m_BlendBindingSets[source] = device->createBindingSet(
                blendBindings, m_BlendBindingLayout);

#if UVSR_AA_DEVELOPER_OVERRIDES
            nvrhi::BindingSetDesc pixelBlendBindings;
            pixelBlendBindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    1, m_BlendConstantBuffer),
                nvrhi::BindingSetItem::Sampler(
                    0, m_LinearClampSampler),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, motionVectors),
                nvrhi::BindingSetItem::Texture_SRV(
                    1, sceneColor),
                nvrhi::BindingSetItem::Texture_SRV(
                    2, m_History.Color(source)),
                nvrhi::BindingSetItem::Texture_SRV(
                    3, currentDepth),
                nvrhi::BindingSetItem::Texture_SRV(
                    4, m_History.Depth(source)),
                nvrhi::BindingSetItem::Texture_SRV(
                    5, m_MomentHistory[source])
            };
            m_PixelBlendBindingSets[source] =
                device->createBindingSet(
                    pixelBlendBindings,
                    m_PixelBlendBindingLayout);
#endif

            nvrhi::BindingSetDesc outputBindings;
            outputBindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_OutputConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_History.Color(destination)),
                nvrhi::BindingSetItem::Texture_SRV(
                    1, m_DebugValues),
                nvrhi::BindingSetItem::Texture_UAV(0, sceneColor)
            };
            m_OutputBindingSets[source] = device->createBindingSet(
                outputBindings, m_OutputBindingLayout);
        }

        for (auto& stageQueries : m_TimerQueries)
            for (nvrhi::TimerQueryHandle& query : stageQueries)
                query = device->createTimerQuery();
    }

    bool MiniEngineTemporalAAPass::CreateBlendComputePermutation(
        const MiniEngineTaaOptions& options,
        uint32_t exportSelective,
        uint32_t sampleResurrection,
        const MiniEngineTaaStaticPerformanceOptions& performance,
        nvrhi::ShaderHandle& shader,
        nvrhi::ComputePipelineHandle& pipeline)
    {
        if (pipeline)
            return true;

        std::vector<ShaderMacro> macros = {
            { "TAA_MOTION_SOURCE",
                std::to_string(
                    static_cast<uint32_t>(options.motionSource)) },
            { "TAA_CURRENT_RECONSTRUCTION",
                std::to_string(static_cast<uint32_t>(
                    options.currentReconstruction)) },
            { "TAA_INTERIOR_WEIGHTING",
                std::to_string(static_cast<uint32_t>(
                    options.interiorWeighting)) },
            { "TAA_HISTORY_FILTER",
                std::to_string(
                    static_cast<uint32_t>(options.historyFilter)) },
            { "TAA_RECTIFICATION",
                std::to_string(
                    static_cast<uint32_t>(options.rectification)) },
            { "TAA_EXPORT_SELECTIVE",
                std::to_string(exportSelective) },
            { "TAA_SAMPLE_RESURRECTION",
                std::to_string(sampleResurrection) },
            { "TAA_COMPUTE_KERNEL",
                performance.computeKernel ==
                        MiniEngineTaaComputeKernel::Threads16x8OnePixel
                    ? "1"
                    : "0" },
            { "TAA_LDS_LAYOUT",
                std::to_string(
                    performance.ldsLayout ==
                            MiniEngineTaaLdsLayout::SplitAndPacked
                        ? UVSR_TAA_LDS_SPLIT_PACKED
                        : performance.ldsLayout ==
                                MiniEngineTaaLdsLayout::Split
                            ? UVSR_TAA_LDS_SPLIT
                            : UVSR_TAA_LDS_LEGACY) },
            { "TAA_SHARED_WORK_REUSE",
                performance.sharedWorkReuse ? "1" : "0" },
            { "TAA_EARLY_HISTORY_REJECTION",
                performance.earlyHistoryRejection ? "1" : "0" },
            { "TAA_FUSED_OUTPUT",
                performance.fusedOutput ? "1" : "0" },
#if UVSR_AA_DEVELOPER_OVERRIDES
            { "TAA_DEVELOPER_DEBUG", "1" }
#else
            { "TAA_DEVELOPER_DEBUG", "0" }
#endif
        };
        shader = m_ShaderFactory->CreateShader(
            "uvsr/taa_miniengine_blend_cs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Compute);
        if (!shader)
        {
            if (!m_ReportedMissingComputePermutation)
            {
                log::error(
                    "Missing precompiled MiniEngine TAA compute permutation "
                    "(algorithm=%u, performance=%u, selective=%u, "
                    "resurrection=%u). TAA will bypass instead of creating "
                    "an invalid pipeline.",
                    GetMiniEngineTaaBlendPermutationIndex(options),
                    GetMiniEngineTaaStaticPerformanceIndex(performance),
                    exportSelective,
                    sampleResurrection);
                m_ReportedMissingComputePermutation = true;
            }
            return false;
        }

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.bindingLayouts = { m_BlendBindingLayout };
        pipelineDesc.CS = shader;
        pipeline = m_Device->createComputePipeline(pipelineDesc);
        if (!pipeline)
        {
            if (!m_ReportedMissingComputePermutation)
            {
                log::error(
                    "Failed to create MiniEngine TAA compute pipeline "
                    "(algorithm=%u, performance=%u, selective=%u, "
                    "resurrection=%u). TAA will bypass.",
                    GetMiniEngineTaaBlendPermutationIndex(options),
                    GetMiniEngineTaaStaticPerformanceIndex(performance),
                    exportSelective,
                    sampleResurrection);
                m_ReportedMissingComputePermutation = true;
            }
            return false;
        }
        return true;
    }

#if UVSR_AA_DEVELOPER_OVERRIDES
    bool MiniEngineTemporalAAPass::CreateBlendPixelPermutation(
        const MiniEngineTaaOptions& options,
        uint32_t exportSelective,
        bool earlyHistoryRejection,
        bool fusedOutput,
        nvrhi::ShaderHandle& shader,
        nvrhi::GraphicsPipelineHandle& pipeline)
    {
        if (pipeline)
            return true;

        std::vector<ShaderMacro> macros = {
            { "TAA_PIXEL_SHADER", "1" },
            { "TAA_MOTION_SOURCE",
                std::to_string(
                    static_cast<uint32_t>(options.motionSource)) },
            { "TAA_CURRENT_RECONSTRUCTION",
                std::to_string(static_cast<uint32_t>(
                    options.currentReconstruction)) },
            { "TAA_INTERIOR_WEIGHTING",
                std::to_string(static_cast<uint32_t>(
                    options.interiorWeighting)) },
            { "TAA_HISTORY_FILTER",
                std::to_string(
                    static_cast<uint32_t>(options.historyFilter)) },
            { "TAA_RECTIFICATION",
                std::to_string(
                    static_cast<uint32_t>(options.rectification)) },
            { "TAA_EXPORT_SELECTIVE",
                std::to_string(exportSelective) },
            { "TAA_SAMPLE_RESURRECTION", "0" },
            { "TAA_COMPUTE_KERNEL", "0" },
            { "TAA_LDS_LAYOUT", "0" },
            { "TAA_SHARED_WORK_REUSE", "0" },
            { "TAA_EARLY_HISTORY_REJECTION",
                earlyHistoryRejection ? "1" : "0" },
            { "TAA_FUSED_OUTPUT", fusedOutput ? "1" : "0" },
            { "TAA_DEVELOPER_DEBUG", "1" }
        };
        shader = m_ShaderFactory->CreateShader(
            "uvsr/taa_miniengine_blend_cs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Pixel);
        if (!shader)
        {
            if (!m_ReportedMissingPixelPermutation)
            {
                log::error(
                    "Missing precompiled MiniEngine TAA fullscreen-pixel "
                    "permutation (algorithm=%u, selective=%u, early=%u, "
                    "fused=%u). TAA will bypass instead of creating an "
                    "invalid pipeline.",
                    GetMiniEngineTaaBlendPermutationIndex(options),
                    exportSelective,
                    uint32_t(earlyHistoryRejection),
                    uint32_t(fusedOutput));
                m_ReportedMissingPixelPermutation = true;
            }
            return false;
        }

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType =
            nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = m_FullscreenVS;
        pipelineDesc.PS = shader;
        pipelineDesc.bindingLayouts = {
            m_PixelBlendBindingLayout
        };
        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState.depthTestEnable =
            false;
        pipelineDesc.renderState.depthStencilState.stencilEnable =
            false;
        pipeline = m_Device->createGraphicsPipeline(
            pipelineDesc,
            m_PixelBlendFramebuffers[0]->getFramebufferInfo());
        if (!pipeline)
        {
            if (!m_ReportedMissingPixelPermutation)
            {
                log::error(
                    "Failed to create MiniEngine TAA fullscreen-pixel "
                    "pipeline (algorithm=%u, selective=%u, early=%u, "
                    "fused=%u). TAA will bypass.",
                    GetMiniEngineTaaBlendPermutationIndex(options),
                    exportSelective,
                    uint32_t(earlyHistoryRejection),
                    uint32_t(fusedOutput));
                m_ReportedMissingPixelPermutation = true;
            }
            return false;
        }
        return true;
    }
#endif

    void MiniEngineTemporalAAPass::ResetHistory()
    {
        (void)m_History.Invalidate();
        m_LastHistoryInputValid = false;
        m_PersistentValid = {};
        m_PersistentViews = {};
    }

    void MiniEngineTemporalAAPass::AdvanceTimers()
    {
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        for (uint32_t stageIndex = 0u;
            stageIndex < static_cast<uint32_t>(Stage::Count);
            ++stageIndex)
        {
            m_TimerActive[stageIndex] = false;
            if (!m_TimerPending[stageIndex][slot])
                continue;

            nvrhi::ITimerQuery* query = m_TimerQueries[stageIndex][slot];
            if (!m_Device->pollTimerQuery(query))
                continue;

            const float milliseconds =
                m_Device->getTimerQueryTime(query) * 1000.f;
            m_Device->resetTimerQuery(query);
            m_TimerPending[stageIndex][slot] = false;

            switch (static_cast<Stage>(stageIndex))
            {
            case Stage::Blend:
                m_Timings.blendMilliseconds = milliseconds;
                break;
            case Stage::Output:
                m_Timings.outputMilliseconds = milliseconds;
                break;
            case Stage::PresentationSharpen:
                m_Timings.presentationSharpenMilliseconds =
                    milliseconds;
                break;
            default:
                break;
            }
        }
    }

    void MiniEngineTemporalAAPass::BeginStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        if (m_TimerPending[stageIndex][slot])
            return;

        commandList->beginTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerActive[stageIndex] = true;
    }

    void MiniEngineTemporalAAPass::EndStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        if (!m_TimerActive[stageIndex])
            return;

        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        commandList->endTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerPending[stageIndex][slot] = true;
        m_TimerActive[stageIndex] = false;
    }

    nvrhi::ITexture* MiniEngineTemporalAAPass::Render(
        nvrhi::ICommandList* commandList,
        const IView& currentView,
        const IView* previousView,
        uint64_t frameIndex,
        const ResolvedAntiAliasingSettings& settings,
        MiniEngineTaaDebugView debugView,
        bool exportSelectiveMorphology,
        bool enableSharpen,
        bool deferSharpenToPresentation,
        float sharpness)
    {
        const MiniEngineTaaOptions& options = settings.temporal;
        const MiniEngineTaaSampleResurrection sampleResurrection =
            settings.sampleResurrection;
        const bool usesSampleResurrection =
            UsesSampleResurrection(sampleResurrection);
        AdvanceTimers();
        if (!deferSharpenToPresentation)
            m_Timings.presentationSharpenMilliseconds = 0.f;

        const uint32_t source = uint32_t(frameIndex & 1u);
        if (!previousView)
            (void)m_History.Invalidate();
        m_LastHistoryInputValid =
            m_History.CanRead(
                source,
                previousView != nullptr,
                frameIndex);

        if (m_History.PrepareForFirstUse(commandList))
        {
            // The shared core clears both base color/depth pairs exactly once.
            // Clear MiniEngine-only attachments in the same first-use
            // transaction so no stale moment or resurrection payload survives.
            for (uint32_t index = 0u; index < 2u; ++index)
            {
                commandList->clearTextureFloat(
                    m_MomentHistory[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
#if UVSR_AA_DEVELOPER_OVERRIDES
                commandList->clearTextureFloat(
                    m_PersistentColor[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
                commandList->clearTextureFloat(
                    m_PersistentDepth[index],
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
#endif
            }
            commandList->clearTextureFloat(
                m_DebugValues,
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
        }

        MiniEngineTaaBlendConstants blendConstants{};
        // Donut vector default constructors deliberately leave their scalar
        // lanes uninitialized. A cut has no previous view, so the
        // current-to-previous jitter delta must be assigned explicitly
        // instead of relying on aggregate value-initialization.
        blendConstants.currentToPreviousJitter = float2::zero();
        blendConstants.dispatchGroupYOffset = 0u;
        blendConstants.sourceDepthPairQuantizationError =
            m_SourceDepthPairQuantizationError;
        blendConstants.maximumHistoryWeight =
            float(settings.historyFrames) /
            float(settings.historyFrames + 1u);
        blendConstants.depthQuantizationPadding1 = 0u;
        blendConstants.depthQuantizationPadding2 = 0u;
        blendConstants.projection =
            currentView.GetProjectionMatrix(false);
        blendConstants.reciprocalBufferDimensions =
            1.f / float2(m_Size);
        blendConstants.temporalBlendFactor =
            settings.historyStrength;
        blendConstants.reciprocalSpeedLimiter = 1.f / 64.f;
        blendConstants.currentJitter =
            currentView.GetPixelOffset();
        if (previousView)
        {
            const float2 currentJitter = currentView.GetPixelOffset();
            const float2 previousJitter = previousView->GetPixelOffset();
            const MiniEngineTaaJitterSample jitterDelta =
                GetMiniEngineTaaCurrentToPreviousJitter(
                    { currentJitter.x, currentJitter.y },
                    { previousJitter.x, previousJitter.y });
            blendConstants.currentToPreviousJitter =
                float2(jitterDelta.x, jitterDelta.y);
        }
        blendConstants.bufferDimensions = m_Size;
        blendConstants.historyValid =
            m_LastHistoryInputValid
                ? 1u
                : 0u;
#if UVSR_AA_DEVELOPER_OVERRIDES
        currentView.FillPlanarViewConstants(
            blendConstants.currentView);
        const IView& immediateHistoryView =
            previousView ? *previousView : currentView;
        immediateHistoryView.FillPlanarViewConstants(
            blendConstants.immediateHistoryView);
        if (m_PersistentValid[0] &&
            m_PersistentViews[0])
        {
            m_PersistentViews[0]->FillPlanarViewConstants(
                blendConstants.persistentHistoryView0);
        }
        else
        {
            blendConstants.persistentHistoryView0 =
                blendConstants.immediateHistoryView;
        }
        if (m_PersistentValid[1] &&
            m_PersistentViews[1])
        {
            m_PersistentViews[1]->FillPlanarViewConstants(
                blendConstants.persistentHistoryView1);
        }
        else
        {
            blendConstants.persistentHistoryView1 =
                blendConstants.persistentHistoryView0;
        }
        blendConstants.persistentValidMask =
            (m_PersistentValid[0] ? 1u : 0u) |
            (sampleResurrection ==
                        MiniEngineTaaSampleResurrection::
                            TwoOlderFrames &&
                    m_PersistentValid[1]
                ? 2u
                : 0u);
#endif
        const float clampedSharpness =
            ClampMiniEngineTaaSharpness(sharpness);
        const bool useSharpen =
            ShouldSharpenMiniEngineTaa(enableSharpen, clampedSharpness);
        const MiniEngineTaaSharpenWeights sharpenWeights =
            GetMiniEngineTaaSharpenWeights(clampedSharpness);
        MiniEngineTaaOutputConstants outputConstants{};
        outputConstants.centerWeight = sharpenWeights.center;
        outputConstants.lateralWeight = sharpenWeights.lateral;
        outputConstants.bufferDimensions = m_Size;
        commandList->writeBuffer(
            m_OutputConstantBuffer,
            &outputConstants,
            sizeof(outputConstants));

        nvrhi::ComputeState outputState;
        // The temporal resolve accepts only its own compact diagnostic range.
#if UVSR_AA_DEVELOPER_OVERRIDES
        const uint32_t requestedDebugIndex =
            static_cast<uint32_t>(debugView);
        const uint32_t debugIndex =
            requestedDebugIndex <
                    MiniEngineTaaResolveDebugViewCount
                ? requestedDebugIndex
                : UVSR_TAA_DEBUG_OFF;
#else
        // Production packages only the Off resolve permutation. Treat any
        // hostile or stale programmatic value as Off before indexing PSOs.
        (void)debugView;
        constexpr uint32_t debugIndex = UVSR_TAA_DEBUG_OFF;
#endif
        const bool showDebug =
            debugIndex != UVSR_TAA_DEBUG_OFF;
        const bool useFusedOutput =
            (settings.passFusion ==
                    MiniEngineTaaPassFusion::Fused ||
                deferSharpenToPresentation) &&
            !useSharpen &&
            !showDebug;

        MiniEngineTaaStaticPerformanceOptions performance =
            GetMiniEngineTaaStaticPerformanceOptions(
                settings,
                useFusedOutput);
        if (usesSampleResurrection)
        {
            // Resurrection is deliberately constrained to the known baseline
            // until each image-equivalent optimization is separately proven.
            // This prevents the old Intel Auto path from compiling the option
            // out while continuing to pay snapshot-copy traffic.
            performance = MiniEngineTaaStaticPerformanceOptions{};
            performance.fusedOutput = useFusedOutput;
        }
        const bool baselinePerformance =
            performance.computeKernel ==
                    MiniEngineTaaComputeKernel::
                        Threads8x8TwoPixels &&
                performance.ldsLayout ==
                    MiniEngineTaaLdsLayout::Legacy &&
                !performance.sharedWorkReuse &&
                !performance.earlyHistoryRejection;

        bool usePixelPath = false;
#if UVSR_AA_DEVELOPER_OVERRIDES
        usePixelPath =
            settings.executionPath ==
                MiniEngineTaaExecutionPath::FullscreenPixelShader &&
            !usesSampleResurrection;
#endif
        const auto bypassMissingPermutation = [&]()
        {
            // Seed dormant selective exports deterministically if a developer
            // caller explicitly requests them.
            if (exportSelectiveMorphology)
            {
                commandList->copyTexture(
                    m_SelectiveCurrent,
                    nvrhi::TextureSlice(),
                    m_SceneColor,
                    nvrhi::TextureSlice());
                commandList->clearTextureFloat(
                    m_SelectiveRejection,
                    nvrhi::AllSubresources,
                    nvrhi::Color(0.f));
            }
            ResetHistory();
            m_LastHistoryInputValid = false;
            m_Timings.historyValid = false;
            m_Timings.accumulationCount = 0u;
            m_Timings.historyResetCount =
                m_History.ResetCount();
            ++m_TimerFrame;
            return m_SceneColor;
        };
        nvrhi::ComputeState blendState;
        if (!usePixelPath && baselinePerformance)
        {
            const uint32_t blendPermutation =
                GetMiniEngineTaaBlendPermutationIndex(options) *
                    (2u *
                        MiniEngineTaaSampleResurrectionCount *
                        2u) +
                uint32_t(exportSelectiveMorphology) *
                    (MiniEngineTaaSampleResurrectionCount *
                        2u) +
                static_cast<uint32_t>(sampleResurrection) * 2u +
                uint32_t(useFusedOutput);
#if UVSR_AA_DEVELOPER_OVERRIDES
            if (!CreateBlendComputePermutation(
                options,
                uint32_t(exportSelectiveMorphology),
                static_cast<uint32_t>(sampleResurrection),
                performance,
                m_BlendShaders[blendPermutation],
                m_BlendPipelines[blendPermutation]))
            {
                return bypassMissingPermutation();
            }
#endif
            blendState.pipeline =
                m_BlendPipelines[blendPermutation];
        }
        else if (!usePixelPath)
        {
#if UVSR_AA_DEVELOPER_OVERRIDES
            const uint32_t algorithmIndex =
                GetMiniEngineTaaBlendPermutationIndex(options);
#else
            const uint32_t algorithmIndex =
                settings.implementation ==
                        AntiAliasingPreset::TemporalBalanced
                    ? 1u
                    : settings.implementation ==
                            AntiAliasingPreset::TemporalQuality
                        ? 2u
                        : 0u;
#endif
            const uint32_t performanceIndex =
                GetMiniEngineTaaStaticPerformanceIndex(
                    performance);
            const uint32_t permutation =
                algorithmIndex *
                    MiniEngineTaaStaticPerformanceCount * 2u +
                performanceIndex * 2u +
                uint32_t(exportSelectiveMorphology);
            if (!CreateBlendComputePermutation(
                options,
                uint32_t(exportSelectiveMorphology),
                0u,
                performance,
                m_PerformanceBlendShaders[permutation],
                m_PerformanceBlendPipelines[permutation]))
            {
                return bypassMissingPermutation();
            }
            blendState.pipeline =
                m_PerformanceBlendPipelines[permutation];
        }
        if (!usePixelPath && !blendState.pipeline)
            return bypassMissingPermutation();
        if (!usePixelPath)
            blendState.bindings = { m_BlendBindingSets[source] };
#if UVSR_AA_DEVELOPER_OVERRIDES
        uint32_t pixelPermutation = 0u;
        if (usePixelPath)
        {
            pixelPermutation =
                GetMiniEngineTaaBlendPermutationIndex(options) * 8u +
                uint32_t(settings.earlyHistoryRejection) * 4u +
                uint32_t(exportSelectiveMorphology) * 2u +
                uint32_t(useFusedOutput);
            if (!CreateBlendPixelPermutation(
                options,
                uint32_t(exportSelectiveMorphology),
                settings.earlyHistoryRejection,
                useFusedOutput,
                m_PixelBlendShaders[pixelPermutation],
                m_PixelBlendPipelines[pixelPermutation]))
            {
                return bypassMissingPermutation();
            }
        }
#endif
        m_Timings.historyColorSamples =
            GetMiniEngineTaaHistoryColorSampleCount(
                options.historyFilter) +
            (sampleResurrection ==
                    MiniEngineTaaSampleResurrection::TwoOlderFrames
                ? 2u
                : sampleResurrection ==
                        MiniEngineTaaSampleResurrection::OneOlderFrame
                    ? 1u
                    : 0u);
        m_Timings.historyMomentSamples =
            GetMiniEngineTaaHistoryMomentSampleCount(
                options.interiorWeighting);
        m_Timings.historyDepthGathers = 1u;
        m_Timings.historyDepthSamples =
            GetMiniEngineTaaHistoryDepthSampleCount(
                options.historyFilter) +
            (sampleResurrection ==
                    MiniEngineTaaSampleResurrection::TwoOlderFrames
                ? 2u
                : sampleResurrection ==
                        MiniEngineTaaSampleResurrection::OneOlderFrame
                    ? 1u
                    : 0u);

        commandList->beginMarker(usePixelPath
            ? useFusedOutput
                ? "MiniEngine TAA Fullscreen Pixel + Fused Output"
                : "MiniEngine TAA Fullscreen Pixel"
            : useFusedOutput
                ? "MiniEngine TAA Temporal Blend + Fused Output"
                : "MiniEngine TAA Temporal Blend");
        BeginStage(commandList, Stage::Blend);
#if UVSR_AA_DEVELOPER_OVERRIDES
        if (usePixelPath)
        {
            commandList->writeBuffer(
                m_BlendConstantBuffer,
                &blendConstants,
                sizeof(blendConstants));
            nvrhi::GraphicsState pixelState;
            pixelState.pipeline =
                m_PixelBlendPipelines[pixelPermutation];
            pixelState.framebuffer =
                m_PixelBlendFramebuffers[source];
            pixelState.bindings = {
                m_PixelBlendBindingSets[source]
            };
            pixelState.viewport = nvrhi::ViewportState()
                .addViewportAndScissorRect(
                    nvrhi::Viewport(
                        float(m_Size.x),
                        float(m_Size.y)));
            commandList->setGraphicsState(pixelState);
            nvrhi::DrawArguments arguments;
            arguments.instanceCount = 1u;
            arguments.vertexCount = 4u;
            commandList->draw(arguments);
        }
        else
#endif
        {
            // Cache-blocking is a dispatch-scheduling experiment, not a shader
            // branch. Each band owns complete 16x8 tiles; LDS halo loads cross
            // band boundaries normally, so image results are unchanged.
            const uint32_t bandCount =
                settings.cacheBlocking ==
                        MiniEngineTaaCacheBlocking::Bands2
                    ? 2u
                    : settings.cacheBlocking ==
                            MiniEngineTaaCacheBlocking::Bands3
                        ? 3u
                        : settings.cacheBlocking ==
                                MiniEngineTaaCacheBlocking::Bands4
                            ? 4u
                            : 1u;
            const uint32_t tileRows =
                (m_Size.y + 7u) / 8u;
            for (uint32_t band = 0u;
                band < bandCount;
                ++band)
            {
                const uint32_t firstRow =
                    tileRows * band / bandCount;
                const uint32_t lastRow =
                    tileRows * (band + 1u) / bandCount;
                blendConstants.dispatchGroupYOffset =
                    firstRow;
                commandList->writeBuffer(
                    m_BlendConstantBuffer,
                    &blendConstants,
                    sizeof(blendConstants));
                // Volatile constant-buffer versions are assigned by the
                // write. Bind the state afterward so validation and the D3D12
                // backend both observe the version used by this band.
                commandList->setComputeState(blendState);
                commandList->dispatch(
                    (m_Size.x + 15u) / 16u,
                    lastRow - firstRow,
                    1u);
            }
        }
        EndStage(commandList, Stage::Blend);
        commandList->endMarker();

        outputState.pipeline = showDebug
            ? m_ResolvePipelines[debugIndex]
            : useSharpen
                ? m_SharpenPipeline
                : m_ResolvePipelines[debugIndex];
        outputState.bindings = { m_OutputBindingSets[source] };
        m_Timings.outputWasSharpened =
            useSharpen && !showDebug;

        if (!useFusedOutput)
        {
            commandList->beginMarker(showDebug
                ? "MiniEngine TAA Developer Visualization"
                : useSharpen
                    ? "MiniEngine TAA Sharpen"
                    : "MiniEngine TAA Resolve");
            BeginStage(commandList, Stage::Output);
            commandList->setComputeState(outputState);
            commandList->dispatch(
                (m_Size.x + 7u) / 8u,
                (m_Size.y + 7u) / 8u,
                1u);
            EndStage(commandList, Stage::Output);
            commandList->endMarker();
        }
        else
        {
            m_Timings.outputMilliseconds = 0.f;
        }

        const uint32_t destination = source ^ 1u;
        m_History.Commit(destination, frameIndex);
#if UVSR_AA_DEVELOPER_OVERRIDES
        // Preserve exact v1-style ages at interval one. At the end of frame N,
        // the source ping-pong texture is frame N-1. On frame N+1, slot zero is
        // therefore age two and slot one is age three. This avoids both an
        // immediate-history duplicate and the old undocumented ages four/eight.
        if (usesSampleResurrection &&
            m_LastHistoryInputValid &&
            previousView)
        {
            if (sampleResurrection ==
                    MiniEngineTaaSampleResurrection::TwoOlderFrames &&
                m_PersistentValid[0])
            {
                commandList->copyTexture(
                    m_PersistentColor[1],
                    nvrhi::TextureSlice(),
                    m_PersistentColor[0],
                    nvrhi::TextureSlice());
                commandList->copyTexture(
                    m_PersistentDepth[1],
                    nvrhi::TextureSlice(),
                    m_PersistentDepth[0],
                    nvrhi::TextureSlice());
                m_PersistentViews[1] = m_PersistentViews[0];
                m_PersistentValid[1] =
                    m_PersistentViews[1] != nullptr;
            }
            else
            {
                m_PersistentValid[1] = false;
            }

            commandList->copyTexture(
                m_PersistentColor[0],
                nvrhi::TextureSlice(),
                m_History.Color(source),
                nvrhi::TextureSlice());
            commandList->copyTexture(
                m_PersistentDepth[0],
                nvrhi::TextureSlice(),
                m_History.Depth(source),
                nvrhi::TextureSlice());
            m_PersistentViews[0] =
                CapturePlanarView(*previousView);
            m_PersistentValid[0] =
                m_PersistentViews[0] != nullptr;
        }
#else
        (void)usesSampleResurrection;
#endif
        m_Timings.historyValid =
            m_History.ValidSlotCount() > 0u;
        m_Timings.accumulationCount =
            m_History.AccumulationCount();
        m_Timings.historyResetCount =
            m_History.ResetCount();
        ++m_TimerFrame;
        return useFusedOutput
            ? m_FusedOutput.Get()
            : m_SceneColor;
    }

    nvrhi::ITexture*
        MiniEngineTemporalAAPass::SharpenPresentation(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* sourceTexture)
    {
        if (!sourceTexture || sourceTexture == m_SceneColor)
            return sourceTexture;

        if (!m_PresentationSharpenBindingSet ||
            m_BoundPresentationSharpenSource != sourceTexture)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_OutputConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, sourceTexture),
                // The sharpen shader does not consume t1. Bind the existing
                // developer texture to satisfy the shared output layout.
                nvrhi::BindingSetItem::Texture_SRV(
                    1, m_DebugValues),
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_SceneColor)
            };
            m_PresentationSharpenBindingSet =
                m_Device->createBindingSet(
                    bindings,
                    m_OutputBindingLayout);
            m_BoundPresentationSharpenSource = sourceTexture;
        }

        nvrhi::ComputeState state;
        state.pipeline = m_PresentationSharpenPipeline;
        state.bindings = { m_PresentationSharpenBindingSet };
        commandList->beginMarker(
            "MiniEngine TAA Presentation Sharpen");
        BeginStage(commandList, Stage::PresentationSharpen);
        commandList->setComputeState(state);
        commandList->dispatch(
            (m_Size.x + 7u) / 8u,
            (m_Size.y + 7u) / 8u,
            1u);
        EndStage(commandList, Stage::PresentationSharpen);
        commandList->endMarker();
        m_Timings.outputWasSharpened = true;
        return m_SceneColor;
    }
}
