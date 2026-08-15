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

namespace
{
    // Portions of the quality path are adapted from Microsoft DirectX Graphics
    // Samples and are distributed under
    // legal/licenses/Microsoft-DirectX-Graphics-Samples-MIT.txt.
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
        uint32_t depthStorageFlags;
    };

    struct alignas(16) TemporalAaOutputConstants
    {
        float centerWeight;
        float lateralWeight;
        uint2 bufferDimensions;
    };

    static_assert(sizeof(TemporalAaBlendConstants) == 128u);
    static_assert(
        offsetof(
            TemporalAaBlendConstants,
            sourceDepthPairQuantizationError) == 112u);
    static_assert(
        offsetof(TemporalAaBlendConstants, behaviorFlags) == 120u);
    static_assert(
        offsetof(
            TemporalAaBlendConstants,
            depthStorageFlags) == 124u);
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
        m_Timings.activeHistoryTextureBytes =
            m_Timings.robustHistoryTextureBytes;

        TemporalHistoryDesc temporalHistoryDesc;
        temporalHistoryDesc.size = m_Size;
        temporalHistoryDesc.debugName =
            "TemporalAA/QualityHistory";
        temporalHistoryDesc.colorUnorderedAccess = true;
        temporalHistoryDesc.depthUnorderedAccess = true;
        temporalHistoryDesc.maximumAccumulation = 65504u;
        m_History.Initialize(device, temporalHistoryDesc);

        nvrhi::TextureDesc historyDesc;
        historyDesc.width = m_Size.x;
        historyDesc.height = m_Size.y;
        historyDesc.dimension = nvrhi::TextureDimension::Texture2D;
        historyDesc.mipLevels = 1u;
        historyDesc.isUAV = true;
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
                m_Timings.minimumDepthIsR16 ? 2u : 4u);

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


        historyDesc.format = sceneColorDesc.format;
        historyDesc.debugName = "TemporalAA/FusedOutput";
        m_FusedOutput = device->createTexture(historyDesc);
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
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(2)
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


        nvrhi::BindingLayoutDesc outputLayoutDesc;
        outputLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        outputLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
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
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_History.Color(destination)),
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_History.Depth(destination)),
                nvrhi::BindingSetItem::Texture_UAV(
                    2, m_FusedOutput)
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


            nvrhi::BindingSetDesc outputBindings;
            outputBindings.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_OutputConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_History.Color(destination)),
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
        constexpr uint32_t productionBlendPipelineCount = 16u;
        constexpr uint32_t resolvePipelineCount = 1u;
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
            const uint32_t productionStep =
                m_PipelinePreparationStep - productionBlendBegin;
            const uint32_t qualityIndex = productionStep / 4u;
            const bool optimized =
                ((productionStep / 2u) & 1u) != 0u;
            const uint32_t fused = productionStep % 2u;
            const TemporalAaOptions options =
                GetPresetTemporalOptions(
                    AntiAliasingQuality(qualityIndex));
            TemporalAaStaticPerformanceOptions performance{};
            performance.optimizedCompute = optimized;
            performance.fusedOutput = fused != 0u;
            const uint32_t algorithmIndex =
                GetTemporalAaBlendPermutationIndex(options);
            const uint32_t performanceIndex =
                GetTemporalAaStaticPerformanceIndex(performance);
            const uint32_t permutation =
                algorithmIndex *
                    TemporalAaStaticPerformanceCount +
                performanceIndex;
            CreateBlendComputePermutation(
                options,
                performance,
                m_PerformanceBlendShaders[permutation],
                m_PerformanceBlendPipelines[permutation]);
        }
        else if (m_PipelinePreparationStep < sharpenBegin)
        {
            m_ResolveShader = m_ShaderFactory->CreateShader(
                "uvsr/temporal_aa_resolve_cs.hlsl",
                "main",
                nullptr,
                nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.CS = m_ResolveShader;
            pipelineDesc.bindingLayouts = { m_OutputBindingLayout };
            m_ResolvePipeline =
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

    bool TemporalAAPass::PrepareForRender(
        const ResolvedAntiAliasingSettings& settings,
        bool enableSharpen,
        bool deferSharpenToPresentation,
        float sharpness)
    {
        m_RenderedThisFrame = false;
        const auto rejectFrame = [&]()
        {
            ResetHistory();
            return false;
        };
        if (!m_PipelinesReady)
            return rejectFrame();

        const uint32_t behaviorFlags = GetTemporalAaBehaviorFlags(
            settings.depthValidation,
            settings.historyWeight,
            settings.motionTrust,
            settings.rectificationClip,
            settings.blendDomain);
        constexpr uint32_t minimumDefaultBehaviorFlags =
            UVSR_TAA_BEHAVIOR_NEAREST_TEXEL_DEPTH |
            UVSR_TAA_BEHAVIOR_IMMEDIATE_HISTORY_WEIGHT |
            UVSR_TAA_BEHAVIOR_SQUARED_MOTION_TRUST |
            UVSR_TAA_BEHAVIOR_TIGHT_RECTIFICATION |
            UVSR_TAA_BEHAVIOR_LINEAR_BLEND_DOMAIN;
        const uint32_t minimumBehaviorIndex =
            behaviorFlags == minimumDefaultBehaviorFlags ? 0u : 1u;
        const bool useMinimum =
            settings.historyStorage == TemporalAaHistoryStorage::Compact &&
            m_MinimumPipelines[minimumBehaviorIndex] &&
            m_MinimumBindingSets[0] &&
            m_MinimumBindingSets[1] &&
            IsTemporalAaCompactHistoryCompatible(settings);
        if (deferSharpenToPresentation &&
            !m_PresentationSharpenPipeline)
        {
            return rejectFrame();
        }
        if (useMinimum)
            return true;

        const bool useSharpen =
            !deferSharpenToPresentation &&
            ShouldSharpenTemporalAa(
                enableSharpen,
                ClampTemporalAaSharpness(sharpness));
        const bool useFusedOutput =
            (settings.fusedOutput || deferSharpenToPresentation) &&
            !useSharpen;
        if (!m_BlendBindingSets[0] || !m_BlendBindingSets[1] ||
            !m_OutputBindingSets[0] || !m_OutputBindingSets[1] ||
            (!useFusedOutput &&
                !(useSharpen ? m_SharpenPipeline : m_ResolvePipeline)))
        {
            return rejectFrame();
        }

        const TemporalAaOptions& options = settings.temporal;
        const TemporalAaStaticPerformanceOptions performance =
            GetTemporalAaStaticPerformanceOptions(
                settings,
                useFusedOutput);
        const uint32_t algorithmIndex =
            GetTemporalAaBlendPermutationIndex(options);
        const uint32_t performanceIndex =
            GetTemporalAaStaticPerformanceIndex(performance);
        const uint32_t permutation =
            algorithmIndex * TemporalAaStaticPerformanceCount +
            performanceIndex;
        if (!CreateBlendComputePermutation(
            options,
            performance,
            m_PerformanceBlendShaders[permutation],
            m_PerformanceBlendPipelines[permutation]))
        {
            return rejectFrame();
        }
        return true;
    }

    bool TemporalAAPass::CreateBlendComputePermutation(
        const TemporalAaOptions& options,
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
            { "TAA_OPTIMIZED_COMPUTE",
                performance.optimizedCompute ? "1" : "0" },
            { "TAA_FUSED_OUTPUT",
                performance.fusedOutput ? "1" : "0" }
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
                    "(algorithm=%u, performance=%u). "
                    "TAA will bypass instead of creating "
                    "an invalid pipeline.",
                    GetTemporalAaBlendPermutationIndex(options),
                    GetTemporalAaStaticPerformanceIndex(performance));
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
                    "(algorithm=%u, performance=%u). "
                    "TAA will bypass.",
                    GetTemporalAaBlendPermutationIndex(options),
                    GetTemporalAaStaticPerformanceIndex(performance));
                m_ReportedMissingComputePermutation = true;
            }
            return false;
        }
        return true;
    }


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
    }

    void TemporalAAPass::AdvanceTimers()
    {
        const uint32_t slot = m_TimerFrame % c_TimerLatency;
        m_TimerSubmissionSlot = slot;
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

        m_TimerFrameWritable = true;
        for (uint32_t stageIndex = 0u;
            stageIndex < static_cast<uint32_t>(Stage::Count);
            ++stageIndex)
        {
            if (m_TimerPending[stageIndex][slot])
            {
                m_TimerFrameWritable = false;
                break;
            }
        }
        if (m_TimerFrameWritable && m_TimerHasSubmission[slot])
        {
            m_Timings.available = true;
            m_TimerHasSubmission[slot] = false;
        }
    }

    void TemporalAAPass::BeginStage(
        nvrhi::ICommandList* commandList,
        Stage stage)
    {
        if (!m_TimerFrameWritable)
            return;

        const uint32_t stageIndex = static_cast<uint32_t>(stage);
        const uint32_t slot = m_TimerSubmissionSlot;
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

        const uint32_t slot = m_TimerSubmissionSlot;
        commandList->endTimerQuery(m_TimerQueries[stageIndex][slot]);
        m_TimerPending[stageIndex][slot] = true;
        m_TimerHasSubmission[slot] = true;
        m_TimerActive[stageIndex] = false;
    }

    nvrhi::ITexture* TemporalAAPass::Render(
        nvrhi::ICommandList* commandList,
        const IView& currentView,
        const IView* previousView,
        uint64_t frameIndex,
        const ResolvedAntiAliasingSettings& settings,
        bool enableSharpen,
        bool deferSharpenToPresentation,
        float sharpness)
    {
        m_RenderedThisFrame = false;
        if (!m_PipelinesReady)
        {
            log::error(
                "TemporalAAPass rendered before pipeline preparation completed.");
            return m_SceneColor;
        }

        const TemporalAaOptions& options = settings.temporal;
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
            UVSR_TAA_BEHAVIOR_NEAREST_TEXEL_DEPTH |
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
        const bool minimumAlgorithmCompatible =
            IsTemporalAaCompactHistoryCompatible(settings);
        const bool useMinimum =
            compactHistoryRequested &&
            m_MinimumPipelines[minimumBehaviorIndex] &&
            m_MinimumBindingSets[0] &&
            m_MinimumBindingSets[1] &&
            minimumAlgorithmCompatible;
        m_Timings.effectiveCostMode = useMinimum
            ? TemporalAaCostMode::Minimum
            : settings.temporalCostMode ==
                    TemporalAaCostMode::Minimum
                ? TemporalAaCostMode::Reduced
                : settings.temporalCostMode;
        m_Timings.activeHistoryTextureBytes = useMinimum
            ? m_Timings.minimumHistoryTextureBytes
            : m_Timings.robustHistoryTextureBytes;
        m_Timings.dispatchCount = 0u;

        if (compactHistoryRequested && !useMinimum &&
            !m_ReportedMinimumFallback)
        {
            log::warning(
                "Temporal AA compact history fell back to the robust path because its algorithm, history weight, or format support is incompatible with this frame.");
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
        // Minimum always packs current depth through binary16 LDS and normally
        // stores persistent depth as R16_FLOAT. Track the two conversions
        // independently because the persistent texture can fall back to R32.
        blendConstants.depthStorageFlags = useMinimum
            ? 1u | (m_Timings.minimumDepthIsR16 ? 2u : 0u)
            : 0u;
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
                        TemporalAaDepthValidation::NearestTexel
                    ? 1u
                    : 0u;
            m_Timings.dispatchCount = 1u;
            m_Timings.historyValid = true;
            m_Timings.accumulationCount =
                m_MinimumAccumulationCount;
            m_Timings.historyResetCount =
                m_MinimumResetCount;
            ++m_TimerFrame;
            m_RenderedThisFrame = true;
            return m_MinimumColor[destination].Get();
        }

        nvrhi::ComputeState outputState;
        const bool useFusedOutput =
            (settings.fusedOutput || effectiveDeferredSharpen) &&
            !useSharpen;
        const TemporalAaStaticPerformanceOptions performance =
            GetTemporalAaStaticPerformanceOptions(
                settings,
                useFusedOutput);

        const auto bypassMissingPermutation = [&]()
        {
            ResetHistory();
            m_LastHistoryInputValid = false;
            m_Timings.historyValid = false;
            m_Timings.accumulationCount = 0u;
            m_Timings.historyResetCount = m_History.ResetCount();
            ++m_TimerFrame;
            return m_SceneColor;
        };

        const uint32_t algorithmIndex =
            GetTemporalAaBlendPermutationIndex(options);
        const uint32_t performanceIndex =
            GetTemporalAaStaticPerformanceIndex(performance);
        const uint32_t permutation =
            algorithmIndex * TemporalAaStaticPerformanceCount +
            performanceIndex;
        if (!CreateBlendComputePermutation(
                options,
                performance,
                m_PerformanceBlendShaders[permutation],
                m_PerformanceBlendPipelines[permutation]))
        {
            return bypassMissingPermutation();
        }

        nvrhi::ComputeState blendState;
        blendState.pipeline =
            m_PerformanceBlendPipelines[permutation];
        blendState.bindings = { m_BlendBindingSets[source] };
        m_Timings.historyColorSamples =
            GetTemporalAaHistoryColorSampleCount(options.historyFilter);
        const bool nearestTexelDepthValidation =
            settings.depthValidation ==
                TemporalAaDepthValidation::NearestTexel;
        m_Timings.historyDepthGathers =
            nearestTexelDepthValidation ? 0u : 1u;
        m_Timings.historyDepthSamples =
            nearestTexelDepthValidation
                ? 1u
                : GetTemporalAaHistoryDepthSampleCount(
                    options.historyFilter);

        commandList->beginMarker(useFusedOutput
            ? "Temporal AA Blend + Fused Output"
            : "Temporal AA Blend");
        BeginStage(commandList, Stage::Blend);
        commandList->writeBuffer(
            m_BlendConstantBuffer,
            &blendConstants,
            sizeof(blendConstants));
        commandList->setComputeState(blendState);
        commandList->dispatch(
            (m_Size.x + 15u) / 16u,
            (m_Size.y + 7u) / 8u,
            1u);
        ++m_Timings.dispatchCount;
        EndStage(commandList, Stage::Blend);
        commandList->endMarker();

        outputState.pipeline = useSharpen
            ? m_SharpenPipeline
            : m_ResolvePipeline;
        outputState.bindings = { m_OutputBindingSets[source] };
        m_Timings.outputWasSharpened = useSharpen;

        if (!useFusedOutput)
        {
            commandList->beginMarker(useSharpen
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
        m_Timings.historyValid =
            m_History.ValidSlotCount() > 0u;
        m_Timings.accumulationCount =
            m_History.AccumulationCount();
        m_Timings.historyResetCount =
            m_History.ResetCount();
        ++m_TimerFrame;
        m_RenderedThisFrame = true;
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
        ++m_Timings.dispatchCount;
        EndStage(commandList, Stage::PresentationSharpen);
        commandList->endMarker();
        m_Timings.outputWasSharpened = true;
        return m_SceneColor;
    }
}
