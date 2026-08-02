#include "temporal_aa.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;

#include <donut/shaders/view_cb.h>

namespace
{
    // Portions of the quality path are adapted from Microsoft DirectX Graphics
    // Samples and are distributed under
    // third_party/microsoft_directx_graphics_samples/LICENSE.txt.
    //
    // The scalar defaults remain verbatim: maximum temporal lerp 1.0, speed
    // limit 64 pixels, and sharpness 0.5. The projection is an integration-only
    // addition that lets the shader express Temporal AA's forward-linear-depth
    // disocclusion inequality over UVSR's infinite reverse-Z device depth.
    struct alignas(16) TemporalAaBlendConstants
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
        uint32_t behaviorFlags;
        uint32_t behaviorPadding;
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
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

    struct alignas(16) TemporalAaOutputConstants
    {
        float centerWeight;
        float lateralWeight;
        uint2 bufferDimensions;
    };

#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
    static_assert(
        sizeof(TemporalAaBlendConstants) ==
        128u + 4u * sizeof(PlanarViewConstants) + 16u);
#else
    static_assert(sizeof(TemporalAaBlendConstants) == 128u);
#endif
    static_assert(
        offsetof(
            TemporalAaBlendConstants,
            sourceDepthPairQuantizationError) == 112u);
    static_assert(
        offsetof(TemporalAaBlendConstants, behaviorFlags) == 120u);
    static_assert(sizeof(TemporalAaOutputConstants) == 16u);

    bool SupportsMinimumHistoryFormat(
        nvrhi::IDevice* device,
        nvrhi::Format format)
    {
        const nvrhi::FormatSupport required =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderSample |
            nvrhi::FormatSupport::ShaderUavStore;
        return (device->queryFormatSupport(format) & required) ==
            required;
    }

#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
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
    TemporalAAPass::TemporalAAPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        nvrhi::ITexture* sceneColor,
        nvrhi::ITexture* currentDepth,
        nvrhi::ITexture* motionVectors,
        bool deferPipelineCreation)
        : m_Device(device)
        , m_ShaderFactory(shaderFactory)
        , m_SceneColor(sceneColor)
        , m_SourceDepthPairQuantizationError(
            currentDepth
                ? GetTemporalAaSourceDepthPairQuantizationError(
                    currentDepth->getDesc().format)
                : 0.f)
    {
        const nvrhi::TextureDesc& sceneColorDesc = sceneColor->getDesc();
        m_Size = uint2(sceneColorDesc.width, sceneColorDesc.height);
        m_Timings.robustHistoryTextureBytes =
            GetTemporalAaHistoryBytes(
                m_Size.x,
                m_Size.y);
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
        m_Timings.persistentHistoryTextureBytes =
            GetTemporalAaPersistentHistoryBytes(
                m_Size.x,
                m_Size.y);
#endif
#if UVSR_AA_DEVELOPER_OVERRIDES
        m_Timings.debugTextureBytes =
            GetTemporalAaDebugBytes(m_Size.x, m_Size.y);
#endif
        m_Timings.activeHistoryTextureBytes =
            m_Timings.robustHistoryTextureBytes;

        TemporalHistoryDesc temporalHistoryDesc;
        temporalHistoryDesc.size = m_Size;
        temporalHistoryDesc.debugName =
            "TemporalAA/QualityHistory";
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

        const nvrhi::Format minimumColorFormat =
            SupportsMinimumHistoryFormat(
                device,
                nvrhi::Format::R11G11B10_FLOAT)
                ? nvrhi::Format::R11G11B10_FLOAT
                : nvrhi::Format::RGBA16_FLOAT;
        const nvrhi::Format minimumDepthFormat =
            SupportsMinimumHistoryFormat(
                device,
                nvrhi::Format::R16_FLOAT)
                ? nvrhi::Format::R16_FLOAT
                : nvrhi::Format::R32_FLOAT;
        m_Timings.minimumColorIsR11G11B10 =
            minimumColorFormat ==
            nvrhi::Format::R11G11B10_FLOAT;
        m_Timings.minimumDepthIsR16 =
            minimumDepthFormat == nvrhi::Format::R16_FLOAT;
        m_Timings.minimumHistoryTextureBytes =
            GetTemporalAaMinimumHistoryBytes(
                m_Size.x,
                m_Size.y,
                m_Timings.minimumColorIsR11G11B10 ? 4u : 8u,
                m_Timings.minimumDepthIsR16 ? 2u : 4u);
        m_Timings.residentHistoryTextureBytes =
            GetTemporalAaResidentHistoryBytes(
                m_Size.x,
                m_Size.y,
                m_Timings.minimumColorIsR11G11B10 ? 4u : 8u,
                m_Timings.minimumDepthIsR16 ? 2u : 4u,
                m_Timings.persistentHistoryTextureBytes != 0u);

#if UVSR_AA_DEVELOPER_OVERRIDES
        historyDesc.isRenderTarget = false;
#endif
        for (uint32_t slot = 0u; slot < 2u; ++slot)
        {
            historyDesc.format = minimumColorFormat;
            historyDesc.debugName =
                "TemporalAA/MinimumColor" +
                std::to_string(slot);
            m_MinimumColor[slot] =
                device->createTexture(historyDesc);
            historyDesc.format = minimumDepthFormat;
            historyDesc.debugName =
                "TemporalAA/MinimumDepth" +
                std::to_string(slot);
            m_MinimumDepth[slot] =
                device->createTexture(historyDesc);
        }
#if UVSR_AA_DEVELOPER_OVERRIDES
        historyDesc.isRenderTarget = true;
#endif

#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
        historyDesc.format = nvrhi::Format::RGBA16_FLOAT;
        historyDesc.debugName = "TemporalAA/PersistentColor0";
        m_PersistentColor[0] = device->createTexture(historyDesc);
        historyDesc.debugName = "TemporalAA/PersistentColor1";
        m_PersistentColor[1] = device->createTexture(historyDesc);
        historyDesc.format = nvrhi::Format::R32_FLOAT;
        historyDesc.debugName = "TemporalAA/PersistentDepth0";
        m_PersistentDepth[0] = device->createTexture(historyDesc);
        historyDesc.debugName = "TemporalAA/PersistentDepth1";
        m_PersistentDepth[1] = device->createTexture(historyDesc);
#endif

#if UVSR_AA_DEVELOPER_OVERRIDES
        historyDesc.format = nvrhi::Format::R16_FLOAT;
        historyDesc.debugName = "TemporalAA/DebugValues";
        m_DebugValues = device->createTexture(historyDesc);
#endif
        historyDesc.format = sceneColorDesc.format;
        historyDesc.debugName = "TemporalAA/FusedOutput";
        m_FusedOutput = device->createTexture(historyDesc);
        historyDesc.format = sceneColorDesc.format;
        historyDesc.debugName = "TemporalAA/SelectiveMorphologyCurrent";
        m_SelectiveCurrent = device->createTexture(historyDesc);
        historyDesc.format = nvrhi::Format::R16_FLOAT;
        historyDesc.debugName = "TemporalAA/SelectiveMorphologyRejection";
        m_SelectiveRejection = device->createTexture(historyDesc);
#if !UVSR_AA_DEVELOPER_OVERRIDES
        // Shipping blend shaders compile debug output away. Reuse the
        // selective rejection allocation for the inert ABI slots instead of
        // carrying a dedicated debug texture in release builds.
        m_DebugValues = m_SelectiveRejection;
#endif

        nvrhi::BufferDesc constantBufferDesc;
        constantBufferDesc.byteSize = sizeof(TemporalAaBlendConstants);
        constantBufferDesc.debugName = "TemporalAA/BlendConstants";
        constantBufferDesc.isConstantBuffer = true;
        constantBufferDesc.isVolatile = true;
        constantBufferDesc.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_BlendConstantBuffer = device->createBuffer(constantBufferDesc);

        constantBufferDesc.byteSize = sizeof(TemporalAaOutputConstants);
        constantBufferDesc.debugName = "TemporalAA/OutputConstants";
        m_OutputConstantBuffer = device->createBuffer(constantBufferDesc);

        // NVRHI's default sampler is Temporal AA's s0 contract: min/mag/mip
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
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2),
            nvrhi::BindingLayoutItem::Texture_UAV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(5)
        };
        m_BlendBindingLayout = device->createBindingLayout(blendLayoutDesc);

        nvrhi::BindingLayoutDesc minimumLayoutDesc;
        minimumLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        minimumLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
            nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
            nvrhi::BindingLayoutItem::Texture_SRV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1)
        };
        m_MinimumBindingLayout =
            device->createBindingLayout(minimumLayoutDesc);

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
            nvrhi::BindingLayoutItem::Texture_SRV(4)
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
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
                nvrhi::BindingSetItem::Texture_SRV(
                    5, m_PersistentColor[0]),
                nvrhi::BindingSetItem::Texture_SRV(
                    6, m_PersistentDepth[0]),
                nvrhi::BindingSetItem::Texture_SRV(
                    7, m_PersistentColor[1]),
                nvrhi::BindingSetItem::Texture_SRV(
                    8, m_PersistentDepth[1]),
#else
                // The factory-startup shader experiment compiles resurrection
                // out and aliases its otherwise-unused binding slots.
                nvrhi::BindingSetItem::Texture_SRV(
                    5, m_History.Color(source)),
                nvrhi::BindingSetItem::Texture_SRV(
                    6, m_History.Depth(source)),
                nvrhi::BindingSetItem::Texture_SRV(
                    7, m_History.Color(source)),
                nvrhi::BindingSetItem::Texture_SRV(
                    8, m_History.Depth(source)),
#endif
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_History.Color(destination)),
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_History.Depth(destination)),
                nvrhi::BindingSetItem::Texture_UAV(
                    2, m_DebugValues),
                nvrhi::BindingSetItem::Texture_UAV(
                    3, m_SelectiveCurrent),
                nvrhi::BindingSetItem::Texture_UAV(
                    4, m_SelectiveRejection)
                ,
                nvrhi::BindingSetItem::Texture_UAV(
                    5, m_FusedOutput)
            };
            m_BlendBindingSets[source] = device->createBindingSet(
                blendBindings, m_BlendBindingLayout);

            if (m_MinimumColor[0] &&
                m_MinimumColor[1] &&
                m_MinimumDepth[0] &&
                m_MinimumDepth[1])
            {
                nvrhi::BindingSetDesc minimumBindings;
                minimumBindings.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        1, m_BlendConstantBuffer),
                    nvrhi::BindingSetItem::Sampler(
                        0, m_LinearClampSampler),
                    nvrhi::BindingSetItem::Texture_SRV(
                        0, motionVectors),
                    nvrhi::BindingSetItem::Texture_SRV(
                        1, sceneColor),
                    nvrhi::BindingSetItem::Texture_SRV(
                        2, m_MinimumColor[source]),
                    nvrhi::BindingSetItem::Texture_SRV(
                        3, currentDepth),
                    nvrhi::BindingSetItem::Texture_SRV(
                        4, m_MinimumDepth[source]),
                    nvrhi::BindingSetItem::Texture_UAV(
                        0, m_MinimumColor[destination]),
                    nvrhi::BindingSetItem::Texture_UAV(
                        1, m_MinimumDepth[destination])
                };
                m_MinimumBindingSets[source] =
                    device->createBindingSet(
                        minimumBindings,
                        m_MinimumBindingLayout);
            }

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
                    4, m_History.Depth(source))
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

        if (!deferPipelineCreation)
        {
            while (!PreparePipelinesStep())
            {
            }
        }
    }

    bool TemporalAAPass::PreparePipelinesStep()
    {
        if (m_PipelinesReady)
            return true;

        constexpr uint32_t minimumPipelineCount = 2u;
#if UVSR_AA_DEVELOPER_OVERRIDES
        constexpr uint32_t productionBlendPipelineCount = 0u;
        constexpr uint32_t resolvePipelineCount =
            TemporalAaResolveDebugViewCount;
#else
        constexpr uint32_t productionBlendPipelineCount = 8u;
        constexpr uint32_t resolvePipelineCount = 1u;
#endif
        constexpr uint32_t sharpenPipelineCount = 2u;
        constexpr uint32_t productionBlendBegin = minimumPipelineCount;
        constexpr uint32_t resolveBegin =
            productionBlendBegin + productionBlendPipelineCount;
        constexpr uint32_t sharpenBegin =
            resolveBegin + resolvePipelineCount;
        constexpr uint32_t pipelineCount =
            sharpenBegin + sharpenPipelineCount;

        if (m_PipelinePreparationStep < minimumPipelineCount)
        {
            const uint32_t runtimeBehavior = m_PipelinePreparationStep;
            const std::vector<ShaderMacro> macros = {
                { "TAA_RUNTIME_BEHAVIOR",
                    std::to_string(runtimeBehavior) }
            };
            m_MinimumShaders[runtimeBehavior] =
                m_ShaderFactory->CreateShader(
                    "uvsr/temporal_aa_minimum_cs.hlsl",
                    "main",
                    &macros,
                    nvrhi::ShaderType::Compute);
            if (m_MinimumShaders[runtimeBehavior] &&
                m_MinimumColor[0] &&
                m_MinimumColor[1] &&
                m_MinimumDepth[0] &&
                m_MinimumDepth[1])
            {
                nvrhi::ComputePipelineDesc pipelineDesc;
                pipelineDesc.CS = m_MinimumShaders[runtimeBehavior];
                pipelineDesc.bindingLayouts = {
                    m_MinimumBindingLayout
                };
                m_MinimumPipelines[runtimeBehavior] =
                    m_Device->createComputePipeline(pipelineDesc);
            }
        }
        else if (m_PipelinePreparationStep < resolveBegin)
        {
#if !UVSR_AA_DEVELOPER_OVERRIDES
            constexpr std::array<AntiAliasingPreset, 4>
                productionPresets = {
                    AntiAliasingPreset::TemporalPerformance,
                    AntiAliasingPreset::TemporalBalanced,
                    AntiAliasingPreset::TemporalQuality,
                    AntiAliasingPreset::TemporalUltra
                };
            const uint32_t productionStep =
                m_PipelinePreparationStep - productionBlendBegin;
            const uint32_t presetIndex = productionStep / 2u;
            const uint32_t fused = productionStep % 2u;
            const TemporalAaOptions options =
                GetPresetTemporalOptions(productionPresets[presetIndex]);
            constexpr uint32_t exportSelective = 0u;
            TemporalAaStaticPerformanceOptions performance{};
            performance.fusedOutput = fused != 0u;
            const uint32_t permutation =
                GetTemporalAaBlendPermutationIndex(options) *
                    (2u * TemporalAaSampleResurrectionCount * 2u) +
                exportSelective *
                    (TemporalAaSampleResurrectionCount * 2u) +
                fused;
            CreateBlendComputePermutation(
                options,
                exportSelective,
                0u,
                performance,
                m_BlendShaders[permutation],
                m_BlendPipelines[permutation]);
#endif
        }
        else if (m_PipelinePreparationStep < sharpenBegin)
        {
            const uint32_t debugView =
                m_PipelinePreparationStep - resolveBegin;
            const std::vector<ShaderMacro> macros = {
                { "TAA_DEBUG_VIEW", std::to_string(debugView) }
            };
            m_ResolveShaders[debugView] = m_ShaderFactory->CreateShader(
                "uvsr/temporal_aa_resolve_cs.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = m_ResolveShaders[debugView];
            pipelineDesc.bindingLayouts = { m_OutputBindingLayout };
            m_ResolvePipelines[debugView] =
                m_Device->createComputePipeline(pipelineDesc);
        }
        else
        {
            const bool premultipliedInput =
                m_PipelinePreparationStep == sharpenBegin;
            const std::vector<ShaderMacro> macros = {
                { "TAA_SHARPEN_INPUT_PREMULTIPLIED",
                    premultipliedInput ? "1" : "0" }
            };
            nvrhi::ShaderHandle shader = m_ShaderFactory->CreateShader(
                "uvsr/temporal_aa_sharpen_cs.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = shader;
            pipelineDesc.bindingLayouts = { m_OutputBindingLayout };
            nvrhi::ComputePipelineHandle pipeline =
                m_Device->createComputePipeline(pipelineDesc);
            if (premultipliedInput)
            {
                m_SharpenShader = std::move(shader);
                m_SharpenPipeline = std::move(pipeline);
            }
            else
            {
                m_PresentationSharpenShader = std::move(shader);
                m_PresentationSharpenPipeline = std::move(pipeline);
            }
        }

        ++m_PipelinePreparationStep;
        if (m_PipelinePreparationStep == pipelineCount)
        {
            m_Timings.minimumPathSupported =
                bool(m_MinimumPipelines[0]) &&
                bool(m_MinimumPipelines[1]) &&
                bool(m_MinimumBindingSets[0]) &&
                bool(m_MinimumBindingSets[1]);
            m_PipelinesReady = true;
        }
        return m_PipelinesReady;
    }

    bool TemporalAAPass::CreateBlendComputePermutation(
        const TemporalAaOptions& options,
        uint32_t exportSelective,
        uint32_t sampleResurrection,
        const TemporalAaStaticPerformanceOptions& performance,
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
                        TemporalAaComputeKernel::Threads16x8OnePixel
                    ? "1"
                    : "0" },
            { "TAA_LDS_LAYOUT",
                std::to_string(
                    performance.ldsLayout ==
                            TemporalAaLdsLayout::SplitAndPacked
                        ? UVSR_TAA_LDS_SPLIT_PACKED
                        : performance.ldsLayout ==
                                TemporalAaLdsLayout::Split
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
            "uvsr/temporal_aa_blend_cs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Compute);
        if (!shader)
        {
            if (!m_ReportedMissingComputePermutation)
            {
                log::error(
                    "Missing precompiled Temporal AA compute permutation "
                    "(algorithm=%u, performance=%u, selective=%u, "
                    "resurrection=%u). TAA will bypass instead of creating "
                    "an invalid pipeline.",
                    GetTemporalAaBlendPermutationIndex(options),
                    GetTemporalAaStaticPerformanceIndex(performance),
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
                    "Failed to create Temporal AA compute pipeline "
                    "(algorithm=%u, performance=%u, selective=%u, "
                    "resurrection=%u). TAA will bypass.",
                    GetTemporalAaBlendPermutationIndex(options),
                    GetTemporalAaStaticPerformanceIndex(performance),
                    exportSelective,
                    sampleResurrection);
                m_ReportedMissingComputePermutation = true;
            }
            return false;
        }
        return true;
    }

#if UVSR_AA_DEVELOPER_OVERRIDES
    bool TemporalAAPass::CreateBlendPixelPermutation(
        const TemporalAaOptions& options,
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
            "uvsr/temporal_aa_blend_cs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Pixel);
        if (!shader)
        {
            if (!m_ReportedMissingPixelPermutation)
            {
                log::error(
                    "Missing precompiled Temporal AA fullscreen-pixel "
                    "permutation (algorithm=%u, selective=%u, early=%u, "
                    "fused=%u). TAA will bypass instead of creating an "
                    "invalid pipeline.",
                    GetTemporalAaBlendPermutationIndex(options),
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
                    "Failed to create Temporal AA fullscreen-pixel "
                    "pipeline (algorithm=%u, selective=%u, early=%u, "
                    "fused=%u). TAA will bypass.",
                    GetTemporalAaBlendPermutationIndex(options),
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

    void TemporalAAPass::ResetHistory()
    {
        (void)m_History.Invalidate();
        const bool minimumChanged =
            m_MinimumValid[0] ||
            m_MinimumValid[1] ||
            m_MinimumAccumulationCount != 0u ||
            m_MinimumHasCommittedSequence;
        m_MinimumValid = {};
        m_MinimumCommittedSequence = {};
        m_MinimumLastCommittedSequence = 0u;
        m_MinimumAccumulationCount = 0u;
        m_MinimumHasCommittedSequence = false;
        if (minimumChanged)
            ++m_MinimumResetCount;
        m_LastHistoryInputValid = false;
        m_LastRenderUsedMinimum = false;
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
        m_PersistentValid = {};
        m_PersistentViews = {};
#endif
    }

    void TemporalAAPass::AdvanceTimers()
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

    void TemporalAAPass::BeginStage(
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

    void TemporalAAPass::EndStage(
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

    nvrhi::ITexture* TemporalAAPass::Render(
        nvrhi::ICommandList* commandList,
        const IView& currentView,
        const IView* previousView,
        uint64_t frameIndex,
        const ResolvedAntiAliasingSettings& settings,
        TemporalAaDebugView debugView,
        bool exportSelectiveMorphology,
        bool enableSharpen,
        bool deferSharpenToPresentation,
        float sharpness)
    {
        if (!m_PipelinesReady)
        {
            log::error(
                "TemporalAAPass rendered before pipeline preparation completed.");
            return m_SceneColor;
        }

        const TemporalAaOptions& options = settings.temporal;
        const TemporalAaSampleResurrection sampleResurrection =
            settings.sampleResurrection;
        const bool usesSampleResurrection =
            UsesSampleResurrection(sampleResurrection);
        const bool compactHistoryRequested =
            settings.historyStorage ==
            TemporalAaHistoryStorage::Compact;
        const uint32_t behaviorFlags = GetTemporalAaBehaviorFlags(
            settings.depthValidation,
            settings.historyWeight,
            settings.motionTrust,
            settings.rectificationClip,
            settings.blendDomain);
        constexpr uint32_t minimumDefaultBehaviorFlags =
            UVSR_TAA_BEHAVIOR_MOVING_POINT_DEPTH |
            UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT |
            UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST |
            UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION |
            UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN;
        const uint32_t minimumBehaviorIndex =
            behaviorFlags == minimumDefaultBehaviorFlags ? 0u : 1u;
        const bool effectiveDeferredSharpen =
            deferSharpenToPresentation;
        AdvanceTimers();
        if (!effectiveDeferredSharpen)
            m_Timings.presentationSharpenMilliseconds = 0.f;

        const float clampedSharpness =
            ClampTemporalAaSharpness(sharpness);
        const bool useSharpen =
            !effectiveDeferredSharpen &&
            ShouldSharpenTemporalAa(enableSharpen, clampedSharpness);
        const TemporalAaSharpenWeights sharpenWeights =
            GetTemporalAaSharpenWeights(clampedSharpness);
#if UVSR_AA_DEVELOPER_OVERRIDES
        const uint32_t requestedDebugIndex =
            static_cast<uint32_t>(debugView);
        const uint32_t debugIndex =
            requestedDebugIndex < TemporalAaResolveDebugViewCount
                ? requestedDebugIndex
                : UVSR_TAA_DEBUG_OFF;
#else
        // Production packages only the Off resolve permutation.
        (void)debugView;
        constexpr uint32_t debugIndex = UVSR_TAA_DEBUG_OFF;
#endif
        const bool showDebug =
            debugIndex != UVSR_TAA_DEBUG_OFF;
        const bool minimumPresentationCompatible =
            settings.subpixelMorphology ==
                MorphologyApplication::Off ||
            !m_Timings.minimumColorIsR11G11B10;
        const bool minimumAlgorithmCompatible =
            IsTemporalAaCompactHistoryCompatible(settings);
        const bool useMinimum =
            compactHistoryRequested &&
            m_MinimumPipelines[minimumBehaviorIndex] &&
            m_MinimumBindingSets[0] &&
            m_MinimumBindingSets[1] &&
            minimumAlgorithmCompatible &&
            !usesSampleResurrection &&
            !exportSelectiveMorphology &&
            minimumPresentationCompatible &&
            !showDebug;
        m_Timings.effectiveCostMode = useMinimum
            ? TemporalAaCostMode::Minimum
            : settings.temporalCostMode ==
                    TemporalAaCostMode::Minimum
                ? TemporalAaCostMode::Reduced
                : settings.temporalCostMode;
        m_Timings.activeHistoryTextureBytes = useMinimum
            ? m_Timings.minimumHistoryTextureBytes
            : m_Timings.robustHistoryTextureBytes +
                (usesSampleResurrection
                    ? m_Timings.persistentHistoryTextureBytes
                    : 0u);
        m_Timings.dispatchCount = 0u;

        if (compactHistoryRequested && !useMinimum &&
            !m_ReportedMinimumFallback)
        {
            log::warning(
                "Temporal AA compact history fell back to the robust path because its algorithm, history weight, format support, presentation morphology, diagnostics, or selective output is incompatible with this frame.");
            m_ReportedMinimumFallback = true;
        }
        else if (!compactHistoryRequested || useMinimum)
        {
            m_ReportedMinimumFallback = false;
        }

        const auto invalidateMinimumHistory = [&]()
        {
            const bool changed =
                m_MinimumValid[0] ||
                m_MinimumValid[1] ||
                m_MinimumAccumulationCount != 0u ||
                m_MinimumHasCommittedSequence;
            m_MinimumValid = {};
            m_MinimumCommittedSequence = {};
            m_MinimumLastCommittedSequence = 0u;
            m_MinimumAccumulationCount = 0u;
            m_MinimumHasCommittedSequence = false;
            if (changed)
                ++m_MinimumResetCount;
        };

        if (useMinimum != m_LastRenderUsedMinimum)
        {
            (void)m_History.Invalidate();
            invalidateMinimumHistory();
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
            m_PersistentValid = {};
            m_PersistentViews = {};
#endif
        }
        m_LastRenderUsedMinimum = useMinimum;

        const uint32_t source = uint32_t(frameIndex & 1u);
        if (useMinimum)
        {
            if (!previousView)
                invalidateMinimumHistory();
            m_LastHistoryInputValid =
                source < m_MinimumValid.size() &&
                previousView != nullptr &&
                m_MinimumValid[source] &&
                m_MinimumAccumulationCount > 0u &&
                m_MinimumHasCommittedSequence &&
                frameIndex > 0u &&
                m_MinimumLastCommittedSequence ==
                    frameIndex - 1u &&
                m_MinimumCommittedSequence[source] ==
                    frameIndex - 1u;
        }
        else
        {
            if (!previousView)
                (void)m_History.Invalidate();
            m_LastHistoryInputValid =
                m_History.CanRead(
                    source,
                    previousView != nullptr,
                    frameIndex);
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
            // A pass-global history break also invalidates every older
            // snapshot. Resurrection may repair a locally rejected sample,
            // but it must never bridge a cut, skipped frame, or missing view.
            if (!m_LastHistoryInputValid)
            {
                m_PersistentValid = {};
                m_PersistentViews = {};
            }
#endif

            // Validity is authoritative and every in-bounds destination texel
            // is overwritten. Avoid reset-time color, depth, snapshot, and
            // diagnostic clears for both robust and compact histories.
            (void)m_History.PrepareForFirstWrite();
        }

        TemporalAaBlendConstants blendConstants{};
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
        blendConstants.behaviorFlags = behaviorFlags;
        blendConstants.behaviorPadding = 0u;
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
            const TemporalAaJitterSample jitterDelta =
                GetTemporalAaCurrentToPreviousJitter(
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
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
        blendConstants.persistentValidMask = 0u;
        blendConstants.persistentPadding0 = 0u;
        blendConstants.persistentPadding1 = 0u;
        blendConstants.persistentPadding2 = 0u;
        if (usesSampleResurrection)
        {
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
                            TemporalAaSampleResurrection::
                                TwoOlderFrames &&
                        m_PersistentValid[1]
                    ? 2u
                    : 0u);
        }
#endif

        TemporalAaOutputConstants outputConstants{};
        outputConstants.centerWeight = sharpenWeights.center;
        outputConstants.lateralWeight = sharpenWeights.lateral;
        outputConstants.bufferDimensions = m_Size;
        commandList->writeBuffer(
            m_OutputConstantBuffer,
            &outputConstants,
            sizeof(outputConstants));

        if (useMinimum)
        {
            const uint32_t destination = source ^ 1u;
            commandList->beginMarker(
                "Temporal AA Minimum One-Dispatch Resolve");
            BeginStage(commandList, Stage::Blend);
            commandList->writeBuffer(
                m_BlendConstantBuffer,
                &blendConstants,
                sizeof(blendConstants));
            nvrhi::ComputeState minimumState;
            minimumState.pipeline =
                m_MinimumPipelines[minimumBehaviorIndex];
            minimumState.bindings = {
                m_MinimumBindingSets[source]
            };
            commandList->setComputeState(minimumState);
            commandList->dispatch(
                (m_Size.x + 15u) / 16u,
                (m_Size.y + 7u) / 8u,
                1u);
            EndStage(commandList, Stage::Blend);
            commandList->endMarker();

            const bool sequenceIsContinuous =
                m_MinimumHasCommittedSequence &&
                frameIndex > 0u &&
                m_MinimumLastCommittedSequence ==
                    frameIndex - 1u;
            if (!sequenceIsContinuous)
            {
                m_MinimumValid = {};
                m_MinimumCommittedSequence = {};
                m_MinimumAccumulationCount = 0u;
            }
            m_MinimumValid[destination] = true;
            m_MinimumCommittedSequence[destination] = frameIndex;
            m_MinimumAccumulationCount = std::min(
                m_MinimumAccumulationCount + 1u,
                std::max(settings.historyFrames, 1u));
            m_MinimumLastCommittedSequence = frameIndex;
            m_MinimumHasCommittedSequence = true;

            m_Timings.outputMilliseconds = 0.f;
            m_Timings.outputWasSharpened = false;
            m_Timings.historyColorSamples =
                m_LastHistoryInputValid ? 1u : 0u;
            m_Timings.historyDepthGathers =
                settings.depthValidation ==
                        TemporalAaDepthValidation::FourTexelFootprint
                    ? 1u
                    : 0u;
            m_Timings.historyDepthSamples =
                m_LastHistoryInputValid &&
                    settings.depthValidation ==
                        TemporalAaDepthValidation::MovingPoint
                    ? 1u
                    : 0u;
            m_Timings.dispatchCount = 1u;
            m_Timings.historyValid = true;
            m_Timings.accumulationCount =
                m_MinimumAccumulationCount;
            m_Timings.historyResetCount =
                m_MinimumResetCount;
            ++m_TimerFrame;
            return m_MinimumColor[destination].Get();
        }

        nvrhi::ComputeState outputState;
        const bool useFusedOutput =
            (settings.passFusion ==
                    TemporalAaPassFusion::Fused ||
                effectiveDeferredSharpen) &&
            !useSharpen &&
            !showDebug;

        TemporalAaStaticPerformanceOptions performance =
            GetTemporalAaStaticPerformanceOptions(
                settings,
                useFusedOutput);
        if (usesSampleResurrection)
        {
            // Resurrection is deliberately constrained to the known baseline
            // until each image-equivalent optimization is separately proven.
            // This prevents the old Intel Auto path from compiling the option
            // out while continuing to pay snapshot-copy traffic.
            performance = TemporalAaStaticPerformanceOptions{};
            performance.fusedOutput = useFusedOutput;
        }
        const bool baselinePerformance =
            performance.computeKernel ==
                    TemporalAaComputeKernel::
                        Threads8x8TwoPixels &&
                performance.ldsLayout ==
                    TemporalAaLdsLayout::Legacy &&
                !performance.sharedWorkReuse &&
                !performance.earlyHistoryRejection;

        bool usePixelPath = false;
#if UVSR_AA_DEVELOPER_OVERRIDES
        usePixelPath =
            settings.executionPath ==
                TemporalAaExecutionPath::FullscreenPixelShader &&
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
                GetTemporalAaBlendPermutationIndex(options) *
                    (2u *
                        TemporalAaSampleResurrectionCount *
                        2u) +
                uint32_t(exportSelectiveMorphology) *
                    (TemporalAaSampleResurrectionCount *
                        2u) +
                static_cast<uint32_t>(sampleResurrection) * 2u +
                uint32_t(useFusedOutput);
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
            blendState.pipeline =
                m_BlendPipelines[blendPermutation];
        }
        else if (!usePixelPath)
        {
            const uint32_t algorithmIndex =
                GetTemporalAaBlendPermutationIndex(options);
            const uint32_t performanceIndex =
                GetTemporalAaStaticPerformanceIndex(
                    performance);
            const uint32_t permutation =
                algorithmIndex *
                    TemporalAaStaticPerformanceCount * 2u +
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
                GetTemporalAaBlendPermutationIndex(options) * 8u +
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
            GetTemporalAaHistoryColorSampleCount(
                options.historyFilter) +
            (sampleResurrection ==
                    TemporalAaSampleResurrection::TwoOlderFrames
                ? 2u
                : sampleResurrection ==
                        TemporalAaSampleResurrection::OneOlderFrame
                    ? 1u
                    : 0u);
        const bool movingPointDepthValidation =
            settings.depthValidation ==
            TemporalAaDepthValidation::MovingPoint;
        m_Timings.historyDepthGathers =
            movingPointDepthValidation ? 0u : 1u;
        m_Timings.historyDepthSamples =
            (movingPointDepthValidation
                ? 1u
                : GetTemporalAaHistoryDepthSampleCount(
                    options.historyFilter)) +
            (sampleResurrection ==
                    TemporalAaSampleResurrection::TwoOlderFrames
                ? 2u
                : sampleResurrection ==
                        TemporalAaSampleResurrection::OneOlderFrame
                    ? 1u
                    : 0u);

        commandList->beginMarker(usePixelPath
            ? useFusedOutput
                ? "Temporal AA Fullscreen Pixel + Fused Output"
                : "Temporal AA Fullscreen Pixel"
            : useFusedOutput
                ? "Temporal AA Blend + Fused Output"
                : "Temporal AA Blend");
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
            ++m_Timings.dispatchCount;
        }
        else
#endif
        {
            // Cache-blocking is a dispatch-scheduling experiment, not a shader
            // branch. Each band owns complete 16x8 tiles; LDS halo loads cross
            // band boundaries normally, so image results are unchanged.
            const uint32_t bandCount =
                settings.cacheBlocking ==
                        TemporalAaCacheBlocking::Bands2
                    ? 2u
                    : settings.cacheBlocking ==
                            TemporalAaCacheBlocking::Bands3
                        ? 3u
                        : settings.cacheBlocking ==
                                TemporalAaCacheBlocking::Bands4
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
                ++m_Timings.dispatchCount;
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
                ? "Temporal AA Developer Visualization"
                : useSharpen
                    ? "Temporal AA Sharpen"
                    : "Temporal AA Resolve");
            BeginStage(commandList, Stage::Output);
            commandList->setComputeState(outputState);
            commandList->dispatch(
                (m_Size.x + 7u) / 8u,
                (m_Size.y + 7u) / 8u,
                1u);
            ++m_Timings.dispatchCount;
            EndStage(commandList, Stage::Output);
            commandList->endMarker();
        }
        else
        {
            m_Timings.outputMilliseconds = 0.f;
        }

        const uint32_t destination = source ^ 1u;
        m_History.Commit(destination, frameIndex);
#if UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE
        // Preserve exact v1-style ages at interval one. At the end of frame N,
        // the source ping-pong texture is frame N-1. On frame N+1, slot zero is
        // therefore age two and slot one is age three. This avoids both an
        // immediate-history duplicate and the old undocumented ages four/eight.
        if (usesSampleResurrection &&
            m_LastHistoryInputValid &&
            previousView)
        {
            if (sampleResurrection ==
                    TemporalAaSampleResurrection::TwoOlderFrames &&
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
        TemporalAAPass::SharpenPresentation(
            nvrhi::ICommandList* commandList,
            nvrhi::ITexture* sourceTexture)
    {
        if (!m_PipelinesReady)
        {
            log::error(
                "TemporalAAPass sharpened before pipeline preparation completed.");
            return sourceTexture;
        }

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
            "Temporal AA Presentation Sharpen");
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
