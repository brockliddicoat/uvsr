#include "diagnostic_cascaded_shadow_map.h"

#include <donut/core/log.h>
#include <donut/core/math/math.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/MaterialBindingCache.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/render/DepthPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/GeometryPasses.h>
#include <nvrhi/nvrhi.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;
using namespace donut::render;

#include <donut/shaders/depth_cb.h>
#include "diagnostic_cascaded_shadow_map_cb.h"

static_assert(sizeof(DiagnosticCsmResolveConstants) % 16u == 0u,
    "Diagnostic CSM constants must preserve HLSL packing.");
    static_assert(sizeof(DiagnosticCsmResolveConstants) == 1168u,
    "Diagnostic CSM constants must preserve the shared C++/HLSL ABI.");
static_assert(offsetof(DiagnosticCsmResolveConstants, worldToUvzw) == 720u);
static_assert(offsetof(DiagnosticCsmResolveConstants, cascadeDepthRanges) == 976u);
static_assert(offsetof(DiagnosticCsmResolveConstants, cascadeParameters) == 1040u);
static_assert(offsetof(DiagnosticCsmResolveConstants, outputSize) == 1104u);
static_assert(offsetof(DiagnosticCsmResolveConstants, tapCount) == 1120u);
static_assert(offsetof(DiagnosticCsmResolveConstants, maximumShadowDistance) == 1136u);
static_assert(sizeof(DiagnosticCsmScrollConstants) == 16u,
    "Diagnostic CSM scroll constants must preserve HLSL packing.");

namespace uvsr
{
    namespace
    {
        // Eight slots leave headroom for whole-frame fence retirement without
        // adding GPU work to the total-only timing interval. Four slots could
        // occasionally drop an otherwise valid source frame on this renderer.
        constexpr uint32_t TimerLatency = 8u;
        constexpr uint32_t TimerStageCount = 4u;
        constexpr uint32_t TimerTotal = 0u;
        constexpr uint32_t TimerClearUpdate = 1u;
        constexpr uint32_t TimerRaster = 2u;
        constexpr uint32_t TimerSampling = 3u;
        constexpr uint32_t MaximumDirtyRectangles = 16u;
        static_assert(
            DiagnosticCsmReceiverCornerCount == frustum::numCorners);

        struct DiagnosticCsmDepthPushConstants
        {
            uint32_t startInstanceLocation = 0u;
            uint32_t startVertexLocation = 0u;
            uint32_t positionOffset = 0u;
            uint32_t texCoordOffset = 0u;
            uint32_t normalOffset = std::numeric_limits<uint32_t>::max();
            float constantDepthBias = 0.f;
            float slopeDepthBias = 0.f;
            float maximumSlopeDepthBiasOrInverseDepthAxisLength = 1.f;
        };
        static_assert(sizeof(DiagnosticCsmDepthPushConstants) == 32u);

        struct DiagnosticCsmTranslationDepthPushConstants
        {
            DiagnosticCsmDepthPushConstants legacy;
            std::array<float, 3u> worldTranslation{};
            uint32_t transformMode = 0u;
        };
        static_assert(
            sizeof(DiagnosticCsmTranslationDepthPushConstants) == 48u);
        static_assert(
            offsetof(
                DiagnosticCsmTranslationDepthPushConstants,
                worldTranslation) == 32u);
        static_assert(
            offsetof(
                DiagnosticCsmTranslationDepthPushConstants,
                transformMode) == 44u);

        struct TranslationOnlyCasterEntry
        {
            std::array<float, 3u> translation{};
            uint32_t generation = 0u;
        };
        static_assert(sizeof(TranslationOnlyCasterEntry) == 16u);

        [[nodiscard]] float EffectiveFilterRadiusTexels(
            const DiagnosticCascadedShadowMapSettings& settings)
        {
            if (settings.filter == DiagnosticCsmFilter::Ue5Pcf5x5)
                return 2.f;
            return NormalizeDiagnosticCsmTapCount(
                    settings.poissonTapCount) == 1u
                ? 0.f
                : NormalizeDiagnosticCsmFilterRadiusTexels(
                    settings.filterRadiusTexels);
        }

        class ScissoredPlanarView final : public PlanarView
        {
        public:
            void SetScissorRect(const nvrhi::Rect& rect)
            {
                m_ScissorRect = rect;
            }
        };

        class DiagnosticCsmDepthPass final : public DepthPass
        {
        private:
            bool m_PositionOnlyOpaqueEnabled = false;
            bool m_TranslationOnlyCasterTransformEnabled = false;
            bool m_PrecomputedDepthAxisInverseLengthEnabled = false;
            bool m_ConservativeSaturatedSlopeEnabled = false;
            bool m_AlgebraicSlowSlopeEnabled = false;
            nvrhi::ShaderHandle m_AlphaTestedVertexShader;

        public:
            class Context final : public DepthPass::Context
            {
            public:
                uint32_t normalOffset =
                    std::numeric_limits<uint32_t>::max();
                float constantDepthBias = 0.f;
                float slopeDepthBias = 0.f;
                float maximumSlopeDepthBias = 1.f;
                float inverseDepthAxisLength = 0.f;
                const std::vector<TranslationOnlyCasterEntry>*
                    translationOnlyCasters = nullptr;
                uint32_t translationOnlyCasterGeneration = 0u;
                DiagnosticCsmStats* submissionStats = nullptr;
            };

            [[nodiscard]] bool IsReady() const
            {
                return m_VertexShader &&
                    m_AlphaTestedVertexShader &&
                    m_PixelShader &&
                    (!m_UseInputAssembler || m_InputLayout) &&
                    m_InputBindingLayout &&
                    m_ViewBindingLayout &&
                    m_DepthCB &&
                    m_ViewBindingSet &&
                    m_MaterialBindings;
            }

        protected:
            nvrhi::ShaderHandle CreateVertexShader(
                ShaderFactory& shaderFactory,
                const CreateParameters& parameters) override
            {
                std::vector<ShaderMacro> macros = {
                    ShaderMacro(
                        "DIAGNOSTIC_CSM_PRECOMPUTED_DEPTH_AXIS",
                        m_PrecomputedDepthAxisInverseLengthEnabled
                            ? "1"
                            : "0"),
                    ShaderMacro(
                        "DIAGNOSTIC_CSM_CONSERVATIVE_SATURATED_SLOPE",
                        m_ConservativeSaturatedSlopeEnabled
                            ? "1"
                            : "0"),
                    ShaderMacro(
                        "DIAGNOSTIC_CSM_ALGEBRAIC_SLOW_SLOPE",
                        m_AlgebraicSlowSlopeEnabled
                            ? "1"
                            : "0"),
                    ShaderMacro(
                        "DIAGNOSTIC_CSM_TRANSLATION_ONLY_CASTER_TRANSFORM",
                        m_TranslationOnlyCasterTransformEnabled
                            ? "1"
                            : "0")
                };
                const char* alphaEntry = parameters.useInputAssembler
                    ? "alpha_tested_input_assembler"
                    : "alpha_tested";
                m_AlphaTestedVertexShader =
                    shaderFactory.CreateShader(
                        "uvsr/diagnostic_cascaded_shadow_map_depth_vs.hlsl",
                        alphaEntry,
                        &macros,
                        nvrhi::ShaderType::Vertex);
                if (!m_PositionOnlyOpaqueEnabled)
                    return m_AlphaTestedVertexShader;

                const char* opaqueEntry = parameters.useInputAssembler
                    ? "main_input_assembler"
                    : "main";
                return shaderFactory.CreateShader(
                    "uvsr/diagnostic_cascaded_shadow_map_depth_vs.hlsl",
                    opaqueEntry,
                    &macros,
                    nvrhi::ShaderType::Vertex);
            }

            nvrhi::InputLayoutHandle CreateInputLayout(
                nvrhi::IShader* vertexShader,
                const CreateParameters& parameters) override
            {
                if (!parameters.useInputAssembler)
                    return nullptr;

                nvrhi::VertexAttributeDesc inputDescs[] = {
                    GetVertexAttributeDesc(
                        VertexAttribute::Position, "POSITION", 0),
                    GetVertexAttributeDesc(
                        VertexAttribute::TexCoord1, "TEXCOORD", 1),
                    GetVertexAttributeDesc(
                        VertexAttribute::Normal, "NORMAL", 2),
                    GetVertexAttributeDesc(
                        VertexAttribute::Transform, "TRANSFORM", 3)
                };
                return m_Device->createInputLayout(
                    inputDescs, 4u, vertexShader);
            }

            nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(
                PipelineKey key,
                const nvrhi::FramebufferInfo& framebufferInfo) override
            {
                nvrhi::GraphicsPipelineDesc pipelineDesc;
                pipelineDesc.inputLayout = m_InputLayout;
                pipelineDesc.VS = key.bits.alphaTested
                    ? m_AlphaTestedVertexShader
                    : m_VertexShader;
                pipelineDesc.PS = nullptr;
                // UE applies directional CSM bias in normalized shadow depth
                // in the vertex shader. Keeping fixed-function bias at zero
                // makes D16 and D32 behave identically.
                pipelineDesc.renderState.rasterState.depthBias = 0;
                pipelineDesc.renderState.rasterState.depthBiasClamp = 0.f;
                pipelineDesc.renderState.rasterState.slopeScaledDepthBias = 0.f;
                pipelineDesc.renderState.rasterState.frontCounterClockwise =
                    key.bits.frontCounterClockwise;
                pipelineDesc.renderState.rasterState.cullMode =
                    key.bits.cullMode;
                pipelineDesc.renderState.rasterState.scissorEnable = true;
                pipelineDesc.renderState.rasterState.depthClipEnable = true;
                pipelineDesc.renderState.depthStencilState.depthFunc =
                    key.bits.reverseDepth
                    ? nvrhi::ComparisonFunc::GreaterOrEqual
                    : nvrhi::ComparisonFunc::LessOrEqual;
                pipelineDesc.bindingLayouts = { m_ViewBindingLayout };

                if (key.bits.alphaTested)
                {
                    pipelineDesc.PS = m_PixelShader;
                    pipelineDesc.bindingLayouts.push_back(
                        m_MaterialBindings->GetLayout());
                }
                if (m_InputBindingLayout)
                {
                    pipelineDesc.bindingLayouts.push_back(
                        m_InputBindingLayout);
                }
                return m_Device->createGraphicsPipeline(
                    pipelineDesc, framebufferInfo);
            }

            nvrhi::BindingLayoutHandle CreateInputBindingLayout() override
            {
                if (m_UseInputAssembler)
                {
                    auto desc = nvrhi::BindingLayoutDesc()
                        .setVisibility(nvrhi::ShaderType::Vertex)
                        .setRegisterSpaceAndDescriptorSet(
                            DEPTH_SPACE_INPUT)
                        .addItem(nvrhi::BindingLayoutItem::PushConstants(
                            DEPTH_BINDING_PUSH_CONSTANTS,
                            sizeof(DiagnosticCsmDepthPushConstants)));
                    return m_Device->createBindingLayout(desc);
                }

                auto desc = nvrhi::BindingLayoutDesc()
                    .setVisibility(nvrhi::ShaderType::Vertex)
                    .setRegisterSpaceAndDescriptorSet(DEPTH_SPACE_INPUT)
                    .addItem(m_IsDX11
                        ? nvrhi::BindingLayoutItem::RawBuffer_SRV(
                            DEPTH_BINDING_INSTANCE_BUFFER)
                        : nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                            DEPTH_BINDING_INSTANCE_BUFFER))
                    .addItem(nvrhi::BindingLayoutItem::RawBuffer_SRV(
                        DEPTH_BINDING_VERTEX_BUFFER))
                    .addItem(nvrhi::BindingLayoutItem::PushConstants(
                        DEPTH_BINDING_PUSH_CONSTANTS,
                        m_TranslationOnlyCasterTransformEnabled
                            ? sizeof(
                                DiagnosticCsmTranslationDepthPushConstants)
                            : sizeof(DiagnosticCsmDepthPushConstants)));
                return m_Device->createBindingLayout(desc);
            }

            nvrhi::BindingSetHandle CreateInputBindingSet(
                const BufferGroup* bufferGroup) override
            {
                if (m_UseInputAssembler)
                {
                    auto desc = nvrhi::BindingSetDesc()
                        .addItem(nvrhi::BindingSetItem::PushConstants(
                            DEPTH_BINDING_PUSH_CONSTANTS,
                            sizeof(DiagnosticCsmDepthPushConstants)));
                    return m_Device->createBindingSet(
                        desc, m_InputBindingLayout);
                }

                auto desc = nvrhi::BindingSetDesc()
                    .addItem(m_IsDX11
                        ? nvrhi::BindingSetItem::RawBuffer_SRV(
                            DEPTH_BINDING_INSTANCE_BUFFER,
                            bufferGroup->instanceBuffer)
                        : nvrhi::BindingSetItem::StructuredBuffer_SRV(
                            DEPTH_BINDING_INSTANCE_BUFFER,
                            bufferGroup->instanceBuffer))
                    .addItem(nvrhi::BindingSetItem::RawBuffer_SRV(
                        DEPTH_BINDING_VERTEX_BUFFER,
                        bufferGroup->vertexBuffer))
                    .addItem(nvrhi::BindingSetItem::PushConstants(
                        DEPTH_BINDING_PUSH_CONSTANTS,
                        m_TranslationOnlyCasterTransformEnabled
                            ? sizeof(
                                DiagnosticCsmTranslationDepthPushConstants)
                            : sizeof(DiagnosticCsmDepthPushConstants)));
                return m_Device->createBindingSet(desc, m_InputBindingLayout);
            }

        public:
            DiagnosticCsmDepthPass(
                nvrhi::IDevice* device,
                const std::shared_ptr<CommonRenderPasses>& commonPasses,
                bool positionOnlyOpaqueEnabled,
                bool translationOnlyCasterTransformEnabled,
                bool precomputedDepthAxisInverseLengthEnabled,
                bool conservativeSaturatedSlopeEnabled,
                bool algebraicSlowSlopeEnabled)
                : DepthPass(device, commonPasses)
                , m_PositionOnlyOpaqueEnabled(positionOnlyOpaqueEnabled)
                , m_TranslationOnlyCasterTransformEnabled(
                    translationOnlyCasterTransformEnabled)
                , m_PrecomputedDepthAxisInverseLengthEnabled(
                    precomputedDepthAxisInverseLengthEnabled)
                , m_ConservativeSaturatedSlopeEnabled(
                    precomputedDepthAxisInverseLengthEnabled &&
                    conservativeSaturatedSlopeEnabled)
                , m_AlgebraicSlowSlopeEnabled(
                    precomputedDepthAxisInverseLengthEnabled &&
                    algebraicSlowSlopeEnabled)
            {
            }

            bool SetupMaterial(
                GeometryPassContext& abstractContext,
                const Material* material,
                nvrhi::RasterCullMode cullMode,
                nvrhi::GraphicsState& state) override
            {
                if (!material)
                    return false;

                auto& context = static_cast<Context&>(abstractContext);
                PipelineKey key = context.keyTemplate;
                key.bits.cullMode = cullMode;

                if (material->domain == MaterialDomain::AlphaTested)
                {
                    nvrhi::IBindingSet* materialBindingSet =
                        m_MaterialBindings->GetMaterialBindingSet(material);
                    if (!materialBindingSet)
                        return false;
                    state.bindings = {
                        m_ViewBindingSet,
                        materialBindingSet
                    };
                    key.bits.alphaTested = true;
                }
                else if (material->domain == MaterialDomain::Opaque)
                {
                    state.bindings = { m_ViewBindingSet };
                    key.bits.alphaTested = false;
                }
                else
                {
                    return false;
                }

                if (m_InputBindingLayout)
                    state.bindings.push_back(context.inputBindingSet);

                const nvrhi::FramebufferInfo& framebufferInfo =
                    state.framebuffer->getFramebufferInfo();
                nvrhi::GraphicsPipelineHandle& pipeline =
                    m_Pipelines[key.value];
                if (!pipeline)
                {
                    std::lock_guard<std::mutex> lockGuard(m_Mutex);
                    if (!pipeline)
                    {
                        pipeline = CreateGraphicsPipeline(
                            key, framebufferInfo);
                    }
                    if (!pipeline)
                        return false;
                }

                assert(pipeline->getFramebufferInfo() == framebufferInfo);
                state.pipeline = pipeline;
                return true;
            }

            void SetupInputBuffers(
                GeometryPassContext& abstractContext,
                const BufferGroup* buffers,
                nvrhi::GraphicsState& state) override
            {
                auto& context = static_cast<Context&>(abstractContext);
                if (m_UseInputAssembler)
                {
                    state.indexBuffer = {
                        buffers->indexBuffer,
                        nvrhi::Format::R32_UINT,
                        0
                    };
                    state.vertexBuffers = {
                        {
                            buffers->vertexBuffer,
                            0,
                            buffers->getVertexBufferRange(
                                VertexAttribute::Position).byteOffset
                        },
                        {
                            buffers->vertexBuffer,
                            1,
                            buffers->getVertexBufferRange(
                                VertexAttribute::TexCoord1).byteOffset
                        },
                        {
                            buffers->vertexBuffer,
                            2,
                            buffers->getVertexBufferRange(
                                VertexAttribute::Normal).byteOffset
                        },
                        { buffers->instanceBuffer, 3, 0 }
                    };
                    context.inputBindingSet =
                        GetOrCreateInputBindingSet(buffers);
                    if (!state.bindings.empty())
                        state.bindings.back() = context.inputBindingSet;
                    return;
                }

                DepthPass::SetupInputBuffers(
                    abstractContext, buffers, state);
                // Donut's RenderView only calls SetupMaterial when the
                // material or cull mode changes. Canonical opaque materials
                // can therefore remain active while the structured vertex
                // buffer group changes. Refresh the trailing input binding
                // here so state merging cannot retain the preceding group's
                // position/instance SRVs. RenderView already invalidates and
                // reapplies the graphics state on every buffer transition.
                context.normalOffset = buffers->hasAttribute(
                    VertexAttribute::Normal)
                    ? uint32_t(buffers->getVertexBufferRange(
                        VertexAttribute::Normal).byteOffset)
                    : std::numeric_limits<uint32_t>::max();
                if (!state.bindings.empty())
                    state.bindings.back() = context.inputBindingSet;
            }

            void SetPushConstants(
                GeometryPassContext& abstractContext,
                nvrhi::ICommandList* commandList,
                nvrhi::GraphicsState& state,
                nvrhi::DrawArguments& args) override
            {
                auto& context = static_cast<Context&>(abstractContext);
                DiagnosticCsmDepthPushConstants constants;
                constants.startInstanceLocation = args.startInstanceLocation;
                constants.startVertexLocation = args.startVertexLocation;
                constants.positionOffset = context.positionOffset;
                constants.texCoordOffset = context.texCoordOffset;
                constants.normalOffset = context.normalOffset;
                constants.constantDepthBias = context.constantDepthBias;
                constants.slopeDepthBias = context.slopeDepthBias;
                constants.maximumSlopeDepthBiasOrInverseDepthAxisLength =
                    m_PrecomputedDepthAxisInverseLengthEnabled
                    ? context.inverseDepthAxisLength
                    : context.maximumSlopeDepthBias;
                if (m_TranslationOnlyCasterTransformEnabled)
                {
                    DiagnosticCsmTranslationDepthPushConstants
                        translationConstants{};
                    translationConstants.legacy = constants;
                    const uint32_t instanceIndex =
                        args.startInstanceLocation;
                    const bool lookupCurrent =
                        context.translationOnlyCasters &&
                        instanceIndex <
                            context.translationOnlyCasters->size() &&
                        (*context.translationOnlyCasters)[instanceIndex].
                            generation ==
                            context.translationOnlyCasterGeneration;
                    const bool useTranslationOnly =
                        ShouldUseDiagnosticCsmTranslationOnlyDraw(
                            true,
                            lookupCurrent,
                            args.instanceCount);
                    if (useTranslationOnly)
                    {
                        translationConstants.worldTranslation =
                            (*context.translationOnlyCasters)[instanceIndex].
                                translation;
                        translationConstants.transformMode = 1u;
                        if (context.submissionStats)
                        {
                            ++context.submissionStats->
                                submittedTranslationOnlyDrawCalls;
                            context.submissionStats->
                                submittedTranslationOnlyTriangles +=
                                uint64_t(args.vertexCount / 3u) *
                                uint64_t(args.instanceCount);
                        }
                    }
                    commandList->setPushConstants(
                        &translationConstants,
                        sizeof(translationConstants));
                }
                else
                {
                    commandList->setPushConstants(
                        &constants, sizeof(constants));
                }
                if (!m_UseInputAssembler)
                {
                    args.startInstanceLocation = 0u;
                    args.startVertexLocation = 0u;
                }
            }
        };

        struct CasterKey
        {
            const MeshInstance* instance = nullptr;
            const MeshGeometry* geometry = nullptr;

            [[nodiscard]] bool operator==(const CasterKey& other) const
            {
                return instance == other.instance &&
                    geometry == other.geometry;
            }
        };

        struct CasterKeyHash
        {
            [[nodiscard]] size_t operator()(const CasterKey& key) const
            {
                const size_t left = std::hash<const void*>{}(key.instance);
                const size_t right = std::hash<const void*>{}(key.geometry);
                return left ^ (right + 0x9e3779b9u +
                    (left << 6u) + (left >> 2u));
            }
        };

        struct MaterialShadowSignature
        {
            MaterialDomain domain = MaterialDomain::Count;
            const void* baseTexture = nullptr;
            const void* opacityTexture = nullptr;
            float opacity = 1.f;
            float alphaCutoff = 0.5f;
            bool baseTextureEnabled = false;
            bool opacityTextureEnabled = false;
            bool doubleSided = false;

            [[nodiscard]] bool operator==(
                const MaterialShadowSignature& other) const
            {
                return domain == other.domain &&
                    baseTexture == other.baseTexture &&
                    opacityTexture == other.opacityTexture &&
                    opacity == other.opacity &&
                    alphaCutoff == other.alphaCutoff &&
                    baseTextureEnabled == other.baseTextureEnabled &&
                    opacityTextureEnabled == other.opacityTextureEnabled &&
                    doubleSided == other.doubleSided;
            }
        };

        struct CasterRecord
        {
            DrawItem draw{};
            CasterKey key;
            box3 worldBounds = box3::empty();
            std::array<float, 12u> localToWorld{};
            std::array<float, 3u> translationOnlyWorldTranslation{};
            MaterialShadowSignature material;
            bool reliableBounds = false;
            bool translationOnlyTransform = false;
        };

        class CasterSubmissionStatsAccumulator
        {
        private:
            DiagnosticCsmStats* m_Stats = nullptr;
            const Material* m_LastMaterial = nullptr;
            const BufferGroup* m_LastBuffers = nullptr;
            nvrhi::RasterCullMode m_LastCullMode =
                nvrhi::RasterCullMode::Back;
            const DrawItem* m_ActiveDraw = nullptr;
            uint32_t m_ActiveInstanceCount = 0u;

            void Flush()
            {
                if (!m_Stats || !m_ActiveDraw ||
                    m_ActiveInstanceCount == 0u)
                {
                    return;
                }
                if (m_Stats->submittedDrawCalls !=
                    std::numeric_limits<uint32_t>::max())
                {
                    ++m_Stats->submittedDrawCalls;
                }
                if (m_ActiveDraw->material &&
                    m_ActiveDraw->material->domain ==
                        MaterialDomain::AlphaTested &&
                    m_Stats->submittedAlphaTestedDrawCalls !=
                        std::numeric_limits<uint32_t>::max())
                {
                    ++m_Stats->submittedAlphaTestedDrawCalls;
                }
                m_Stats->submittedInstances += m_ActiveInstanceCount;
                m_Stats->submittedTriangles +=
                    uint64_t(m_ActiveDraw->geometry->numIndices / 3u) *
                    m_ActiveInstanceCount;
                m_ActiveDraw = nullptr;
                m_ActiveInstanceCount = 0u;
            }

        public:
            void Reset(DiagnosticCsmStats* stats)
            {
                m_Stats = stats;
                m_LastMaterial = nullptr;
                m_LastBuffers = nullptr;
                m_LastCullMode = nvrhi::RasterCullMode::Back;
                m_ActiveDraw = nullptr;
                m_ActiveInstanceCount = 0u;
            }

            void Record(const DrawItem& appended)
            {
                if (!m_Stats)
                    return;
                if (appended.material &&
                    appended.material->domain ==
                        MaterialDomain::AlphaTested &&
                    m_Stats->alphaTestedCasterProjectionPairs !=
                        std::numeric_limits<uint32_t>::max())
                {
                    ++m_Stats->alphaTestedCasterProjectionPairs;
                }

                const bool newBuffers =
                    appended.buffers != m_LastBuffers;
                const bool newMaterial =
                    appended.material != m_LastMaterial ||
                    appended.cullMode != m_LastCullMode;
                if (newBuffers || newMaterial)
                    Flush();

                const uint32_t itemStartIndex =
                    appended.mesh->indexOffset +
                    appended.geometry->indexOffsetInMesh;
                const uint32_t itemInstance =
                    appended.instance->GetInstanceIndex();
                const bool merge = m_ActiveDraw &&
                    m_ActiveDraw->mesh->indexOffset +
                        m_ActiveDraw->geometry->indexOffsetInMesh ==
                            itemStartIndex &&
                    m_ActiveDraw->instance->GetInstanceIndex() +
                        m_ActiveInstanceCount == itemInstance;
                if (!merge)
                {
                    Flush();
                    m_ActiveDraw = &appended;
                    m_ActiveInstanceCount = 1u;
                }
                else
                {
                    ++m_ActiveInstanceCount;
                }

                m_LastMaterial = appended.material;
                m_LastBuffers = appended.buffers;
                m_LastCullMode = appended.cullMode;
            }

            void Finish()
            {
                Flush();
            }
        };

        class CasterRecordDrawStrategy final : public IDrawStrategy
        {
        private:
            const std::vector<CasterRecord>* m_Casters = nullptr;
            const std::vector<size_t>* m_Indices = nullptr;
            size_t m_ReadIndex = 0u;

        public:
            void SetData(
                const std::vector<CasterRecord>& casters,
                const std::vector<size_t>* indices)
            {
                m_Casters = &casters;
                m_Indices = indices;
                m_ReadIndex = 0u;
            }

            void PrepareForView(
                const std::shared_ptr<SceneGraphNode>&,
                const IView&) override
            {
            }

            const DrawItem* GetNextItem() override
            {
                if (!m_Casters)
                    return nullptr;
                size_t casterIndex = 0u;
                if (!NextDiagnosticCsmCasterSubmissionIndex(
                        m_Casters->size(),
                        m_Indices ? m_Indices->data() : nullptr,
                        m_Indices ? m_Indices->size() : 0u,
                        m_Indices != nullptr,
                        m_ReadIndex,
                        casterIndex))
                {
                    return nullptr;
                }
                return &(*m_Casters)[casterIndex].draw;
            }
        };

        class InstrumentedCasterRecordDrawStrategy final :
            public IDrawStrategy
        {
        private:
            CasterRecordDrawStrategy m_Source;
            CasterSubmissionStatsAccumulator m_Stats;

        public:
            void SetData(
                const std::vector<CasterRecord>& casters,
                const std::vector<size_t>* indices,
                DiagnosticCsmStats& stats)
            {
                m_Source.SetData(casters, indices);
                m_Stats.Reset(&stats);
            }

            void PrepareForView(
                const std::shared_ptr<SceneGraphNode>& rootNode,
                const IView& view) override
            {
                m_Source.PrepareForView(rootNode, view);
            }

            const DrawItem* GetNextItem() override
            {
                const DrawItem* item = m_Source.GetNextItem();
                if (item)
                    m_Stats.Record(*item);
                else
                    m_Stats.Finish();
                return item;
            }
        };

        struct ProjectedCasterCullVolume
        {
            struct ReceiverAxis
            {
                float2 axis = 0.f;
                float receiverMinimum = 0.f;
                float receiverMaximum = 0.f;
            };

            std::array<float2, frustum::numCorners> receiverHull{};
            std::array<ReceiverAxis, frustum::numCorners> receiverAxes{};
            uint32_t receiverHullCount = 0u;
            uint32_t receiverAxisCount = 0u;
            float2 receiverMinimumLight = 0.f;
            float2 receiverMaximumLight = 0.f;
            affine3 worldToLight = affine3::identity();
            float receiverMaximumLightZ =
                -std::numeric_limits<float>::infinity();
            float filterMargin = 0.f;
            bool receiverBoundsReady = false;
            bool valid = false;
        };

        struct ProjectedCasterLightShape
        {
            struct CasterAxis
            {
                float2 axis = 0.f;
                float casterMinimum = 0.f;
                float casterMaximum = 0.f;
                bool valid = false;
            };

            float3 centerLight = 0.f;
            std::array<float3, 3u> basisLight{};
            std::array<CasterAxis, 3u> casterAxes{};
            float2 minimumLight = 0.f;
            float2 maximumLight = 0.f;
            float minimumLightZ = 0.f;
            bool casterAxesReady = false;
            bool valid = false;
        };

        struct CascadeProjection
        {
            DiagnosticCsmCascadeRange range;
            DiagnosticCsmProjectionCompatibility compatibility;
            ProjectedCasterCullVolume casterCullVolume;
            DiagnosticCsmRect receiverRasterScissor;
            float4x4 worldToUvzw = float4x4::identity();
            float inverseDepthAxisLength = 0.f;
            bool receiverRasterScissorValid = false;
            bool valid = false;
        };

        struct CascadeWork
        {
            DiagnosticCsmUpdateAction action =
                DiagnosticCsmUpdateAction::FullRedraw;
            DiagnosticCsmScrollRegions scroll;
            std::vector<DiagnosticCsmRect> dirtyRectangles;
            std::vector<std::vector<size_t>> dirtyCasterIndices;
            std::vector<CasterRecord> currentCasters;
            std::vector<DiagnosticCsmRect> projectedCasterRectangles;
            std::vector<uint8_t> projectedCasterBoundsReliable;
            const std::vector<CasterRecord>* cachedShadowDrawList = nullptr;
            bool currentCastersReady = false;
            bool snapshotReliable = false;
        };

        struct CachedShadowDrawListEntry
        {
            bool valid = false;
            std::shared_ptr<SceneGraphNode> rootNode;
            const DirectionalLight* light = nullptr;
            const IDrawStrategy* drawStrategy = nullptr;
            uint64_t sceneStateRevision = 0u;
            uint64_t lastUse = 0u;
            DiagnosticCascadedShadowMapSettings settings;
            float4x4 cameraWorldToClip = float4x4::identity();
            float4x4 cameraWorldToClipNoOffset = float4x4::identity();
            float3 cameraOrigin = 0.f;
            float2 cameraPixelOffset = 0.f;
            nvrhi::Viewport cameraViewport;
            std::array<float4x4,
                DiagnosticCsmMaximumCascades> worldToUvzw{};
            std::array<std::vector<CasterRecord>,
                DiagnosticCsmMaximumCascades> casters;
        };

        template <typename T>
        [[nodiscard]] bool HasIdenticalObjectRepresentation(
            const T& left,
            const T& right)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            return std::memcmp(&left, &right, sizeof(T)) == 0;
        }

        void SaturatingAddUint32(uint32_t& destination, uint64_t value)
        {
            const uint64_t sum = uint64_t(destination) + value;
            destination = uint32_t(std::min(
                sum,
                uint64_t(std::numeric_limits<uint32_t>::max())));
        }

        [[nodiscard]] bool IsFiniteBox(const box3& bounds)
        {
            return !bounds.isempty() &&
                all(dm::isfinite(bounds.m_mins)) &&
                all(dm::isfinite(bounds.m_maxs));
        }

        [[nodiscard]] bool IsFiniteAffineTransform(
            const affine3& transform)
        {
            return all(dm::isfinite(transform.transformPoint(float3(0.f)))) &&
                all(dm::isfinite(transform.transformVector(
                    float3(1.f, 0.f, 0.f)))) &&
                all(dm::isfinite(transform.transformVector(
                    float3(0.f, 1.f, 0.f)))) &&
                all(dm::isfinite(transform.transformVector(
                    float3(0.f, 0.f, 1.f))));
        }

        [[nodiscard]] bool IsFiniteMatrix(const float4x4& matrix)
        {
            const std::array<float4, 4u> basis = {
                float4(1.f, 0.f, 0.f, 0.f),
                float4(0.f, 1.f, 0.f, 0.f),
                float4(0.f, 0.f, 1.f, 0.f),
                float4(0.f, 0.f, 0.f, 1.f)
            };
            return std::all_of(
                basis.begin(),
                basis.end(),
                [&matrix](const float4& vector)
                {
                    return all(dm::isfinite(vector * matrix));
                });
        }

        [[nodiscard]] float Cross2D(
            const float2& origin,
            const float2& left,
            const float2& right)
        {
            const float2 a = left - origin;
            const float2 b = right - origin;
            return a.x * b.y - a.y * b.x;
        }

        bool BuildProjectedReceiverHull(
            const std::array<float2, frustum::numCorners>& input,
            ProjectedCasterCullVolume& output,
            bool precomputeAxes)
        {
            std::vector<float2> points(input.begin(), input.end());
            for (const float2& point : points)
            {
                if (!all(dm::isfinite(point)))
                    return false;
            }
            std::sort(points.begin(), points.end(),
                [](const float2& left, const float2& right)
                {
                    return left.x < right.x ||
                        (left.x == right.x && left.y < right.y);
                });
            points.erase(std::unique(
                points.begin(), points.end(),
                [](const float2& left, const float2& right)
                {
                    return left.x == right.x && left.y == right.y;
                }), points.end());
            if (points.size() < 3u)
                return false;

            std::vector<float2> hull;
            hull.resize(points.size() * 2u);
            size_t count = 0u;
            constexpr float CollinearTolerance = 1e-6f;
            for (const float2& point : points)
            {
                while (count >= 2u && Cross2D(
                        hull[count - 2u], hull[count - 1u], point) <=
                        CollinearTolerance)
                {
                    --count;
                }
                hull[count++] = point;
            }
            const size_t lowerCount = count;
            for (size_t index = points.size() - 1u;
                index-- > 0u;)
            {
                const float2& point = points[index];
                while (count > lowerCount && Cross2D(
                        hull[count - 2u], hull[count - 1u], point) <=
                        CollinearTolerance)
                {
                    --count;
                }
                hull[count++] = point;
            }
            if (count > 1u)
                --count;
            if (count < 3u || count > output.receiverHull.size())
                return false;

            output.receiverHullCount = uint32_t(count);
            std::copy_n(
                hull.begin(), count, output.receiverHull.begin());
            output.receiverAxisCount = 0u;
            if (precomputeAxes)
            {
                output.receiverMinimumLight =
                    float2(std::numeric_limits<float>::infinity());
                output.receiverMaximumLight =
                    float2(-std::numeric_limits<float>::infinity());
                for (uint32_t receiverVertex = 0u;
                    receiverVertex < output.receiverHullCount;
                    ++receiverVertex)
                {
                    output.receiverMinimumLight = min(
                        output.receiverMinimumLight,
                        output.receiverHull[receiverVertex]);
                    output.receiverMaximumLight = max(
                        output.receiverMaximumLight,
                        output.receiverHull[receiverVertex]);
                }
                output.receiverBoundsReady =
                    all(dm::isfinite(output.receiverMinimumLight)) &&
                    all(dm::isfinite(output.receiverMaximumLight));

                for (uint32_t vertex = 0u;
                    vertex < output.receiverHullCount;
                    ++vertex)
                {
                    const float2 edge =
                        output.receiverHull[
                            (vertex + 1u) %
                                output.receiverHullCount] -
                        output.receiverHull[vertex];
                    float2 axis(-edge.y, edge.x);
                    const float axisLength = length(axis);
                    if (!(axisLength > 1e-6f) ||
                        !std::isfinite(axisLength))
                    {
                        continue;
                    }
                    axis /= axisLength;

                    float receiverMinimum =
                        std::numeric_limits<float>::infinity();
                    float receiverMaximum =
                        -std::numeric_limits<float>::infinity();
                    for (uint32_t receiverVertex = 0u;
                        receiverVertex < output.receiverHullCount;
                        ++receiverVertex)
                    {
                        const float projection = dot(
                            output.receiverHull[receiverVertex],
                            axis);
                        receiverMinimum = std::min(
                            receiverMinimum, projection);
                        receiverMaximum = std::max(
                            receiverMaximum, projection);
                    }

                    auto& receiverAxis =
                        output.receiverAxes[
                            output.receiverAxisCount++];
                    receiverAxis.axis = axis;
                    receiverAxis.receiverMinimum = receiverMinimum;
                    receiverAxis.receiverMaximum = receiverMaximum;
                }
            }
            output.valid = true;
            return true;
        }

        [[nodiscard]] bool
            ProjectedCasterOverlapsReceiverHullLegacy(
            const box3& bounds,
            const ProjectedCasterCullVolume& volume)
        {
            if (!volume.valid || !IsFiniteBox(bounds) ||
                volume.receiverHullCount < 3u)
            {
                return true;
            }

            const float3 centerWorld = bounds.center();
            const float3 extentWorld =
                (bounds.m_maxs - bounds.m_mins) * 0.5f;
            const float3 centerLight3 =
                volume.worldToLight.transformPoint(centerWorld);
            const std::array<float3, 3u> casterBasis3 = {
                volume.worldToLight.transformVector(
                    float3(extentWorld.x, 0.f, 0.f)),
                volume.worldToLight.transformVector(
                    float3(0.f, extentWorld.y, 0.f)),
                volume.worldToLight.transformVector(
                    float3(0.f, 0.f, extentWorld.z))
            };
            if (!all(dm::isfinite(centerLight3)) ||
                !all(dm::isfinite(casterBasis3[0])) ||
                !all(dm::isfinite(casterBasis3[1])) ||
                !all(dm::isfinite(casterBasis3[2])) ||
                !std::isfinite(volume.receiverMaximumLightZ))
            {
                return true;
            }

            // The light-space +Z axis follows the directional-light rays.
            // A caster wholly downstream from every receiver cannot shadow
            // the split even when its XY projection overlaps the hull.
            const float casterMinimumLightZ = centerLight3.z -
                std::abs(casterBasis3[0].z) -
                std::abs(casterBasis3[1].z) -
                std::abs(casterBasis3[2].z);
            if (casterMinimumLightZ >
                volume.receiverMaximumLightZ + volume.filterMargin)
            {
                return false;
            }

            const float2 centerLight = centerLight3.xy();
            const std::array<float2, 3u> casterBasis = {
                casterBasis3[0].xy(),
                casterBasis3[1].xy(),
                casterBasis3[2].xy()
            };

            auto IsSeparatingAxis = [&](float2 axis)
            {
                const float axisLength = length(axis);
                if (!(axisLength > 1e-6f) || !std::isfinite(axisLength))
                    return false;
                axis /= axisLength;

                float receiverMinimum =
                    std::numeric_limits<float>::infinity();
                float receiverMaximum =
                    -std::numeric_limits<float>::infinity();
                for (uint32_t vertex = 0u;
                    vertex < volume.receiverHullCount;
                    ++vertex)
                {
                    const float projection = dot(
                        volume.receiverHull[vertex], axis);
                    receiverMinimum = std::min(
                        receiverMinimum, projection);
                    receiverMaximum = std::max(
                        receiverMaximum, projection);
                }

                const float casterCenter = dot(centerLight, axis);
                const float casterRadius =
                    std::abs(dot(casterBasis[0], axis)) +
                    std::abs(dot(casterBasis[1], axis)) +
                    std::abs(dot(casterBasis[2], axis));
                const float casterMinimum = casterCenter - casterRadius;
                const float casterMaximum = casterCenter + casterRadius;
                return casterMaximum + volume.filterMargin <
                        receiverMinimum ||
                    receiverMaximum + volume.filterMargin < casterMinimum;
            };

            for (uint32_t vertex = 0u;
                vertex < volume.receiverHullCount;
                ++vertex)
            {
                const float2 edge =
                    volume.receiverHull[
                        (vertex + 1u) % volume.receiverHullCount] -
                    volume.receiverHull[vertex];
                if (IsSeparatingAxis(float2(-edge.y, edge.x)))
                    return false;
            }
            for (const float2& edge : casterBasis)
            {
                if (IsSeparatingAxis(float2(-edge.y, edge.x)))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool BuildProjectedCasterLightShape(
            const box3& bounds,
            const affine3& worldToLight,
            ProjectedCasterLightShape& output)
        {
            output = {};
            if (!IsFiniteBox(bounds))
                return false;

            const float3 centerWorld = bounds.center();
            const float3 extentWorld =
                (bounds.m_maxs - bounds.m_mins) * 0.5f;
            output.centerLight =
                worldToLight.transformPoint(centerWorld);
            output.basisLight = {
                worldToLight.transformVector(
                    float3(extentWorld.x, 0.f, 0.f)),
                worldToLight.transformVector(
                    float3(0.f, extentWorld.y, 0.f)),
                worldToLight.transformVector(
                    float3(0.f, 0.f, extentWorld.z))
            };
            if (!all(dm::isfinite(output.centerLight)) ||
                !all(dm::isfinite(output.basisLight[0])) ||
                !all(dm::isfinite(output.basisLight[1])) ||
                !all(dm::isfinite(output.basisLight[2])))
            {
                return false;
            }

            output.minimumLightZ = output.centerLight.z -
                std::abs(output.basisLight[0].z) -
                std::abs(output.basisLight[1].z) -
                std::abs(output.basisLight[2].z);
            const float2 projectedExtent =
                abs(output.basisLight[0].xy()) +
                abs(output.basisLight[1].xy()) +
                abs(output.basisLight[2].xy());
            output.minimumLight =
                output.centerLight.xy() - projectedExtent;
            output.maximumLight =
                output.centerLight.xy() + projectedExtent;
            output.valid = std::isfinite(output.minimumLightZ) &&
                all(dm::isfinite(output.minimumLight)) &&
                all(dm::isfinite(output.maximumLight));
            return output.valid;
        }

        [[nodiscard]] bool PrepareProjectedCasterAxes(
            ProjectedCasterLightShape& caster)
        {
            if (!caster.valid)
                return false;
            if (caster.casterAxesReady)
                return true;

            caster.casterAxes = {};
            const float2 centerLight = caster.centerLight.xy();
            const std::array<float2, 3u> casterBasis = {
                caster.basisLight[0].xy(),
                caster.basisLight[1].xy(),
                caster.basisLight[2].xy()
            };
            for (uint32_t index = 0u;
                index < casterBasis.size();
                ++index)
            {
                const float2 edge = casterBasis[index];
                float2 axis(-edge.y, edge.x);
                const float axisLength = length(axis);
                if (!(axisLength > 1e-6f) ||
                    !std::isfinite(axisLength))
                {
                    continue;
                }
                axis /= axisLength;

                const float casterCenter = dot(centerLight, axis);
                const float casterRadius =
                    std::abs(dot(casterBasis[0], axis)) +
                    std::abs(dot(casterBasis[1], axis)) +
                    std::abs(dot(casterBasis[2], axis));
                auto& casterAxis = caster.casterAxes[index];
                casterAxis.axis = axis;
                casterAxis.casterMinimum =
                    casterCenter - casterRadius;
                casterAxis.casterMaximum =
                    casterCenter + casterRadius;
                casterAxis.valid = true;
            }
            caster.casterAxesReady = true;
            return true;
        }

        [[nodiscard]] bool
            ProjectedCasterLightShapeOverlapsReceiverHull(
                ProjectedCasterLightShape& caster,
                const ProjectedCasterCullVolume& volume,
                bool usePrecomputedReceiverAxes)
        {
            if (!volume.valid || !caster.valid ||
                volume.receiverHullCount < 3u ||
                !std::isfinite(volume.receiverMaximumLightZ))
            {
                return true;
            }

            if (caster.minimumLightZ >
                volume.receiverMaximumLightZ + volume.filterMargin)
            {
                return false;
            }

            if (usePrecomputedReceiverAxes &&
                volume.receiverBoundsReady &&
                (caster.maximumLight.x + volume.filterMargin <
                        volume.receiverMinimumLight.x ||
                    volume.receiverMaximumLight.x +
                            volume.filterMargin <
                        caster.minimumLight.x ||
                    caster.maximumLight.y + volume.filterMargin <
                        volume.receiverMinimumLight.y ||
                    volume.receiverMaximumLight.y +
                            volume.filterMargin <
                        caster.minimumLight.y))
            {
                return false;
            }

            const float2 centerLight = caster.centerLight.xy();
            const std::array<float2, 3u> casterBasis = {
                caster.basisLight[0].xy(),
                caster.basisLight[1].xy(),
                caster.basisLight[2].xy()
            };
            const auto ReceiverAxisSeparates =
                [&](const float2& axis,
                    float receiverMinimum,
                    float receiverMaximum)
                {
                    const float casterCenter =
                        dot(centerLight, axis);
                    const float casterRadius =
                        std::abs(dot(casterBasis[0], axis)) +
                        std::abs(dot(casterBasis[1], axis)) +
                        std::abs(dot(casterBasis[2], axis));
                    const float casterMinimum =
                        casterCenter - casterRadius;
                    const float casterMaximum =
                        casterCenter + casterRadius;
                    return casterMaximum + volume.filterMargin <
                            receiverMinimum ||
                        receiverMaximum + volume.filterMargin <
                            casterMinimum;
                };

            if (usePrecomputedReceiverAxes)
            {
                for (uint32_t axisIndex = 0u;
                    axisIndex < volume.receiverAxisCount;
                    ++axisIndex)
                {
                    const auto& axis =
                        volume.receiverAxes[axisIndex];
                    if (ReceiverAxisSeparates(
                            axis.axis,
                            axis.receiverMinimum,
                            axis.receiverMaximum))
                    {
                        return false;
                    }
                }
            }
            else
            {
                for (uint32_t vertex = 0u;
                    vertex < volume.receiverHullCount;
                    ++vertex)
                {
                    const float2 edge =
                        volume.receiverHull[
                            (vertex + 1u) %
                                volume.receiverHullCount] -
                        volume.receiverHull[vertex];
                    float2 axis(-edge.y, edge.x);
                    const float axisLength = length(axis);
                    if (!(axisLength > 1e-6f) ||
                        !std::isfinite(axisLength))
                    {
                        continue;
                    }
                    axis /= axisLength;

                    float receiverMinimum =
                        std::numeric_limits<float>::infinity();
                    float receiverMaximum =
                        -std::numeric_limits<float>::infinity();
                    for (uint32_t receiverVertex = 0u;
                        receiverVertex < volume.receiverHullCount;
                        ++receiverVertex)
                    {
                        const float projection = dot(
                            volume.receiverHull[receiverVertex],
                            axis);
                        receiverMinimum = std::min(
                            receiverMinimum, projection);
                        receiverMaximum = std::max(
                            receiverMaximum, projection);
                    }
                    if (ReceiverAxisSeparates(
                            axis,
                            receiverMinimum,
                            receiverMaximum))
                    {
                        return false;
                    }
                }
            }

            // Most non-overlapping casters separate on a receiver-hull axis.
            // Defer the three normalized caster-axis projections until a
            // caster survives those cheaper, cascade-local tests. A shared
            // light-space shape prepares them at most once across cascades.
            if (!PrepareProjectedCasterAxes(caster))
                return true;

            for (const auto& axis : caster.casterAxes)
            {
                if (!axis.valid)
                    continue;

                float receiverMinimum =
                    std::numeric_limits<float>::infinity();
                float receiverMaximum =
                    -std::numeric_limits<float>::infinity();
                for (uint32_t vertex = 0u;
                    vertex < volume.receiverHullCount;
                    ++vertex)
                {
                    const float projection = dot(
                        volume.receiverHull[vertex], axis.axis);
                    receiverMinimum = std::min(
                        receiverMinimum, projection);
                    receiverMaximum = std::max(
                        receiverMaximum, projection);
                }
                if (axis.casterMaximum + volume.filterMargin <
                        receiverMinimum ||
                    receiverMaximum + volume.filterMargin <
                        axis.casterMinimum)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool ProjectedCasterOverlapsReceiverHull(
            const box3& bounds,
            const ProjectedCasterCullVolume& volume,
            bool usePrecomputedReceiverAxes)
        {
            if (!usePrecomputedReceiverAxes)
            {
                return ProjectedCasterOverlapsReceiverHullLegacy(
                    bounds, volume);
            }

            ProjectedCasterLightShape caster;
            if (!BuildProjectedCasterLightShape(
                    bounds, volume.worldToLight, caster))
            {
                return true;
            }
            return ProjectedCasterLightShapeOverlapsReceiverHull(
                caster, volume, true);
        }

        [[nodiscard]] std::array<float, 12u> MakeTransformSignature(
            const affine3& transform)
        {
            std::array<float, 12u> result{};
            uint32_t element = 0u;
            for (uint32_t row = 0u; row < 3u; ++row)
            {
                for (uint32_t column = 0u; column < 3u; ++column)
                    result[element++] = transform.m_linear[row][column];
            }
            result[element++] = transform.m_translation.x;
            result[element++] = transform.m_translation.y;
            result[element] = transform.m_translation.z;
            return result;
        }

        [[nodiscard]] MaterialShadowSignature GetMaterialSignature(
            const Material* material)
        {
            MaterialShadowSignature result;
            if (!material)
                return result;
            result.domain = material->domain;
            result.baseTexture = material->baseOrDiffuseTexture
                ? material->baseOrDiffuseTexture->texture.Get()
                : nullptr;
            result.opacityTexture = material->opacityTexture
                ? material->opacityTexture->texture.Get()
                : nullptr;
            result.opacity = material->opacity;
            result.alphaCutoff = material->alphaCutoff;
            result.baseTextureEnabled =
                material->enableBaseOrDiffuseTexture;
            result.opacityTextureEnabled =
                material->enableOpacityTexture;
            result.doubleSided = material->doubleSided;
            return result;
        }

        [[nodiscard]] bool EqualBox(const box3& left, const box3& right)
        {
            return all(left.m_mins == right.m_mins) &&
                all(left.m_maxs == right.m_maxs);
        }

        [[nodiscard]] bool DrawItemLess(
            const CasterRecord& left,
            const CasterRecord& right,
            bool mergeOpaqueDepthState)
        {
            if (mergeOpaqueDepthState)
            {
                const uint32_t leftDomain = left.draw.material &&
                    left.draw.material->domain == MaterialDomain::Opaque
                    ? 0u
                    : 1u;
                const uint32_t rightDomain = right.draw.material &&
                    right.draw.material->domain == MaterialDomain::Opaque
                    ? 0u
                    : 1u;
                if (leftDomain != rightDomain)
                    return leftDomain < rightDomain;
            }
            if (left.draw.material != right.draw.material)
            {
                return std::less<const Material*>{}(
                    left.draw.material, right.draw.material);
            }
            if (mergeOpaqueDepthState &&
                left.draw.cullMode != right.draw.cullMode)
            {
                return uint32_t(left.draw.cullMode) <
                    uint32_t(right.draw.cullMode);
            }
            if (left.draw.buffers != right.draw.buffers)
            {
                return std::less<const BufferGroup*>{}(
                    left.draw.buffers, right.draw.buffers);
            }
            if (left.draw.mesh != right.draw.mesh)
            {
                return std::less<const MeshInfo*>{}(
                    left.draw.mesh, right.draw.mesh);
            }
            if (mergeOpaqueDepthState &&
                left.draw.geometry != right.draw.geometry)
            {
                return std::less<const MeshGeometry*>{}(
                    left.draw.geometry, right.draw.geometry);
            }
            if (mergeOpaqueDepthState &&
                left.draw.instance && right.draw.instance)
            {
                const uint32_t leftIndex =
                    left.draw.instance->GetInstanceIndex();
                const uint32_t rightIndex =
                    right.draw.instance->GetInstanceIndex();
                if (leftIndex != rightIndex)
                    return leftIndex < rightIndex;
            }
            if (left.draw.instance != right.draw.instance)
            {
                return std::less<const MeshInstance*>{}(
                    left.draw.instance, right.draw.instance);
            }
            if (!mergeOpaqueDepthState &&
                left.draw.geometry != right.draw.geometry)
            {
                return std::less<const MeshGeometry*>{}(
                    left.draw.geometry, right.draw.geometry);
            }
            return false;
        }

        [[nodiscard]] bool CanMergeRectsWithoutExpandingCoverage(
            const DiagnosticCsmRect& left,
            const DiagnosticCsmRect& right)
        {
            if (!left.IsValid() || !right.IsValid())
                return false;
            const int32_t overlapWidth = std::max(
                0,
                std::min(left.maxX, right.maxX) -
                    std::max(left.minX, right.minX));
            const int32_t overlapHeight = std::max(
                0,
                std::min(left.maxY, right.maxY) -
                    std::max(left.minY, right.minY));
            const uint64_t overlapArea =
                uint64_t(overlapWidth) * uint64_t(overlapHeight);
            const uint64_t coveredArea =
                left.Area() + right.Area() - overlapArea;
            return UnionDiagnosticCsmRects(left, right).Area() ==
                coveredArea;
        }

        bool AppendMergedRect(
            std::vector<DiagnosticCsmRect>& rectangles,
            DiagnosticCsmRect rectangle)
        {
            if (!rectangle.IsValid())
                return true;

            bool merged = true;
            while (merged)
            {
                merged = false;
                for (auto it = rectangles.begin();
                    it != rectangles.end();
                    ++it)
                {
                    if (CanMergeRectsWithoutExpandingCoverage(
                            *it, rectangle))
                    {
                        rectangle = UnionDiagnosticCsmRects(
                            *it, rectangle);
                        rectangles.erase(it);
                        merged = true;
                        break;
                    }
                }
            }

            if (rectangles.size() >= MaximumDirtyRectangles)
                return false;
            rectangles.push_back(rectangle);
            return true;
        }

        [[nodiscard]] float4x4 MakeWorldToUvzw(
            const IView& view)
        {
            const float4x4 clipToUvzw = {
                0.5f, 0.f, 0.f, 0.f,
                0.f, -0.5f, 0.f, 0.f,
                0.f, 0.f, 1.f, 0.f,
                0.5f, 0.5f, 0.f, 1.f
            };
            return view.GetViewProjectionMatrix(false) * clipToUvzw;
        }

        [[nodiscard]] bool HasDepthFormatSupport(
            nvrhi::IDevice* device,
            nvrhi::Format format)
        {
            if (!device)
                return false;
            const nvrhi::FormatSupport depthRequired =
                nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::DepthStencil |
                nvrhi::FormatSupport::ShaderLoad |
                nvrhi::FormatSupport::ShaderSample;
            return (device->queryFormatSupport(format) & depthRequired) ==
                depthRequired;
        }

        [[nodiscard]] nvrhi::Format SelectDepthFormat(
            nvrhi::IDevice* device,
            bool prefer16Bit)
        {
            if (prefer16Bit &&
                HasDepthFormatSupport(device, nvrhi::Format::D16))
            {
                return nvrhi::Format::D16;
            }
            if (HasDepthFormatSupport(device, nvrhi::Format::D32))
                return nvrhi::Format::D32;
            return nvrhi::Format::UNKNOWN;
        }

        [[nodiscard]] bool HasRequiredFormatSupport(nvrhi::IDevice* device)
        {
            if (!device)
                return false;
            const nvrhi::FormatSupport visibilityRequired =
                nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::ShaderLoad |
                nvrhi::FormatSupport::ShaderSample |
                nvrhi::FormatSupport::ShaderUavStore;
            return SelectDepthFormat(device, true) !=
                    nvrhi::Format::UNKNOWN &&
                (device->queryFormatSupport(nvrhi::Format::R8_UNORM) &
                    visibilityRequired) == visibilityRequired;
        }
    }

    bool ValidateDiagnosticCsmProjectedCasterOptimizationParity()
    {
        const std::array<float2, frustum::numCorners> receiverPoints = {
            float2(-1.f, -1.f),
            float2(1.f, -1.f),
            float2(-1.f, 1.f),
            float2(1.f, 1.f),
            float2(-0.75f, -0.75f),
            float2(0.75f, -0.75f),
            float2(-0.75f, 0.75f),
            float2(0.75f, 0.75f)
        };
        const std::array<box3, 8u> casterBounds = {
            box3(float3(-0.25f), float3(0.25f)),
            box3(
                float3(1.20f, -0.20f, -0.20f),
                float3(1.40f, 0.20f, 0.20f)),
            box3(
                float3(1.04f, -0.20f, -0.20f),
                float3(1.14f, 0.20f, 0.20f)),
            box3(
                float3(-0.20f, -0.20f, 1.06f),
                float3(0.20f, 0.20f, 1.30f)),
            box3(
                float3(-0.20f, -0.20f, -4.f),
                float3(0.20f, 0.20f, -3.f)),
            box3(
                float3(-2.f, -0.03f, -0.10f),
                float3(2.f, 0.03f, 0.10f)),
            box3(
                float3(-0.01f, 0.90f, -0.01f),
                float3(0.01f, 1.10f, 0.01f)),
            box3(
                float3(-1000.f, -1000.f, -1000.f),
                float3(1000.f, 1000.f, 1000.f))
        };
        const std::array<affine3, 2u> worldToLightTransforms = {
            affine3::identity(),
            dm::rotation(float3(0.f, 0.f, 1.f), 0.37f)
        };

        for (const affine3& worldToLight : worldToLightTransforms)
        {
            ProjectedCasterCullVolume legacyVolume;
            legacyVolume.worldToLight = worldToLight;
            legacyVolume.receiverMaximumLightZ = 1.f;
            legacyVolume.filterMargin = 0.05f;
            ProjectedCasterCullVolume optimizedVolume = legacyVolume;
            if (!BuildProjectedReceiverHull(
                    receiverPoints, legacyVolume, false) ||
                !BuildProjectedReceiverHull(
                    receiverPoints, optimizedVolume, true))
            {
                return false;
            }
            if (legacyVolume.receiverAxisCount != 0u ||
                optimizedVolume.receiverAxisCount == 0u ||
                legacyVolume.receiverHullCount !=
                    optimizedVolume.receiverHullCount)
            {
                return false;
            }

            const box3 receiverSeparatedBounds(
                float3(3.f, -0.2f, -0.2f),
                float3(3.2f, 0.2f, 0.2f));
            ProjectedCasterLightShape receiverSeparatedCaster;
            if (!BuildProjectedCasterLightShape(
                    receiverSeparatedBounds,
                    worldToLight,
                    receiverSeparatedCaster) ||
                receiverSeparatedCaster.casterAxesReady ||
                ProjectedCasterLightShapeOverlapsReceiverHull(
                    receiverSeparatedCaster,
                    optimizedVolume,
                    true) ||
                receiverSeparatedCaster.casterAxesReady)
            {
                return false;
            }

            ProjectedCasterLightShape overlappingCaster;
            if (!BuildProjectedCasterLightShape(
                    casterBounds[0],
                    worldToLight,
                    overlappingCaster) ||
                overlappingCaster.casterAxesReady ||
                !ProjectedCasterLightShapeOverlapsReceiverHull(
                    overlappingCaster,
                    optimizedVolume,
                    true) ||
                !overlappingCaster.casterAxesReady)
            {
                return false;
            }

            for (const box3& bounds : casterBounds)
            {
                const bool legacy =
                    ProjectedCasterOverlapsReceiverHullLegacy(
                        bounds, legacyVolume);
                const bool receiverOnly =
                    ProjectedCasterOverlapsReceiverHull(
                        bounds, optimizedVolume, true);

                ProjectedCasterLightShape caster;
                if (!BuildProjectedCasterLightShape(
                        bounds, worldToLight, caster))
                {
                    return false;
                }
                const bool sharedOnly =
                    ProjectedCasterLightShapeOverlapsReceiverHull(
                        caster, legacyVolume, false);
                const bool both =
                    ProjectedCasterLightShapeOverlapsReceiverHull(
                        caster, optimizedVolume, true);
                if (legacy != receiverOnly ||
                    legacy != sharedOnly ||
                    legacy != both)
                {
                    return false;
                }
            }

            // Exercise boundaries, rotated projected boxes, downstream
            // rejection and varied aspect ratios densely enough to catch a
            // fast interval reject that is not conservative with the full
            // legacy SAT.
            for (int32_t z = -2; z <= 2; ++z)
            {
                for (int32_t y = -4; y <= 4; ++y)
                {
                    for (int32_t x = -4; x <= 4; ++x)
                    {
                        const float3 center(
                            float(x) * 0.47f,
                            float(y) * 0.43f,
                            float(z) * 0.71f);
                        const float3 extent(
                            0.03f + float((x + 8) % 4) * 0.16f,
                            0.04f + float((y + 8) % 5) * 0.11f,
                            0.05f + float((z + 4) % 3) * 0.19f);
                        const box3 bounds(
                            center - extent,
                            center + extent);
                        const bool legacy =
                            ProjectedCasterOverlapsReceiverHullLegacy(
                                bounds, legacyVolume);
                        const bool receiverOnly =
                            ProjectedCasterOverlapsReceiverHull(
                                bounds, optimizedVolume, true);
                        ProjectedCasterLightShape caster;
                        if (!BuildProjectedCasterLightShape(
                                bounds, worldToLight, caster))
                        {
                            return false;
                        }
                        const bool sharedOnly =
                            ProjectedCasterLightShapeOverlapsReceiverHull(
                                caster, legacyVolume, false);
                        const bool both =
                            ProjectedCasterLightShapeOverlapsReceiverHull(
                                caster, optimizedVolume, true);
                        if (legacy != receiverOnly ||
                            legacy != sharedOnly ||
                            legacy != both)
                        {
                            return false;
                        }
                    }
                }
            }
        }

        auto malformedPoints = receiverPoints;
        malformedPoints[0].x =
            std::numeric_limits<float>::quiet_NaN();
        ProjectedCasterCullVolume malformedVolume;
        if (BuildProjectedReceiverHull(
                malformedPoints, malformedVolume, true))
        {
            return false;
        }

        const box3 nonFiniteBounds(
            float3(
                std::numeric_limits<float>::quiet_NaN(),
                -1.f,
                -1.f),
            float3(1.f));
        ProjectedCasterCullVolume validVolume;
        validVolume.worldToLight = affine3::identity();
        validVolume.receiverMaximumLightZ = 1.f;
        if (!BuildProjectedReceiverHull(
                receiverPoints, validVolume, true) ||
            !ProjectedCasterOverlapsReceiverHullLegacy(
                nonFiniteBounds, validVolume) ||
            !ProjectedCasterOverlapsReceiverHull(
                nonFiniteBounds, validVolume, true))
        {
            return false;
        }

        ProjectedCasterLightShape invalidCaster;
        if (BuildProjectedCasterLightShape(
                nonFiniteBounds,
                affine3::identity(),
                invalidCaster) ||
            !ProjectedCasterLightShapeOverlapsReceiverHull(
                invalidCaster, validVolume, false) ||
            !ProjectedCasterLightShapeOverlapsReceiverHull(
                invalidCaster, validVolume, true))
        {
            return false;
        }
        return true;
    }

    class DiagnosticCascadedShadowMapPass::Impl
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<ShaderFactory> m_ShaderFactory;
        std::shared_ptr<CommonRenderPasses> m_CommonPasses;

        nvrhi::TextureHandle m_Depth;
        nvrhi::TextureHandle m_ScrollScratch;
        nvrhi::TextureHandle m_Visibility;
        nvrhi::TextureHandle m_DebugVisualization;
        nvrhi::ITexture* m_BoundCameraDepth = nullptr;
        nvrhi::ITexture* m_BoundCameraNormals = nullptr;
        uint32_t m_DepthResolution = 0u;
        nvrhi::Format m_DepthFormat = nvrhi::Format::UNKNOWN;
        nvrhi::Format m_AppliedDepthFormat = nvrhi::Format::UNKNOWN;
        bool m_AppliedPositionOnlyOpaqueEnabled = false;
        bool m_AppliedTranslationOnlyCasterTransformEnabled = false;
        bool m_AppliedInputAssemblerCasterFetchEnabled = false;
        bool m_InputAssemblerCasterFetchCreationFailed = false;
        bool m_AppliedPrecomputedDepthAxisInverseLengthEnabled = false;
        bool m_AppliedConservativeSaturatedSlopeEnabled = false;
        bool m_AppliedAlgebraicSlowSlopeEnabled = false;

        std::unique_ptr<FramebufferFactory> m_FramebufferFactory;
        std::unique_ptr<FramebufferFactory> m_ScrollFramebufferFactory;
        std::unique_ptr<DiagnosticCsmDepthPass> m_DepthPass;
        std::unique_ptr<DiagnosticCsmDepthPass>
            m_InputAssemblerDepthPass;
        std::shared_ptr<ScissoredPlanarView> m_ScrollScratchView;
        std::array<std::shared_ptr<ScissoredPlanarView>,
            DiagnosticCsmMaximumCascades> m_CascadeViews;
        std::array<CascadeProjection,
            DiagnosticCsmMaximumCascades> m_CurrentProjections{};
        std::array<DiagnosticCsmProjectionCompatibility,
            DiagnosticCsmMaximumCascades> m_PreviousCompatibility{};
        std::array<bool,
            DiagnosticCsmMaximumCascades> m_PreviousProjectionValid{};
        std::array<std::vector<CasterRecord>,
            DiagnosticCsmMaximumCascades> m_CachedCasters;
        std::array<CascadeWork,
            DiagnosticCsmMaximumCascades> m_Work;
        std::array<CachedShadowDrawListEntry,
            DiagnosticCsmCachedShadowDrawListSlotCount>
            m_CachedShadowDrawLists;
        uint64_t m_CachedShadowDrawListUse = 0u;
        bool m_CachedShadowDrawListsWereEligible = false;
        std::vector<DrawItem> m_DrawItemsScratch;
        std::vector<size_t> m_ManualCasterIndicesScratch;
        std::vector<size_t> m_InputAssemblerCasterIndicesScratch;
        std::vector<uint32_t> m_CascadeMaskStackScratch;
        std::vector<TranslationOnlyCasterEntry>
            m_TranslationOnlyCasters;
        uint32_t m_TranslationOnlyCasterGeneration = 0u;
        std::array<bool,
            DiagnosticCsmMaximumCascades> m_CasterSnapshotValid{};
        uint32_t m_PreviousCascadeCount = 0u;
        uint64_t m_PreviousSceneRevision = 0u;
        bool m_PreviousSceneRevisionValid = false;
        bool m_CacheValid = false;
        const SceneGraphNode* m_PreviousRoot = nullptr;
        bool m_ResourcesRecreatedThisFrame = false;

        nvrhi::ShaderHandle m_ClearVertexShader;
        nvrhi::GraphicsPipelineHandle m_ClearPipeline;
        nvrhi::ShaderHandle m_ScrollPixelShader;
        nvrhi::GraphicsPipelineHandle m_ScrollPipeline;
        nvrhi::BindingLayoutHandle m_ScrollBindingLayout;
        nvrhi::BindingSetHandle m_DepthScrollBindingSet;
        nvrhi::BindingSetHandle m_ScratchScrollBindingSet;
        nvrhi::BufferHandle m_ScrollConstants;
        std::array<nvrhi::ShaderHandle,
            DiagnosticCsmResolvePermutationCount>
            m_ResolveShaders;
        std::array<nvrhi::ComputePipelineHandle,
            DiagnosticCsmResolvePermutationCount>
            m_ResolvePipelines;
        nvrhi::BindingLayoutHandle m_ResolveBindingLayout;
        nvrhi::BindingSetHandle m_ResolveBindingSet;
        nvrhi::BufferHandle m_ResolveConstants;
        nvrhi::SamplerHandle m_ShadowDepthSampler;

        std::array<std::array<nvrhi::TimerQueryHandle, TimerLatency>,
            TimerStageCount> m_TimerQueries;
        std::array<std::array<bool, TimerLatency>,
            TimerStageCount> m_TimerPending{};
        std::array<std::array<float, TimerStageCount>,
            TimerLatency> m_TimerValues{};
        std::array<uint32_t, TimerLatency> m_TimerMasks{};
        std::array<uint64_t, TimerLatency> m_TimerSourceFrames{};
        std::array<uint64_t, TimerLatency>
            m_TimerConfigurationGenerations{};
        std::array<bool, TimerLatency> m_TimerDetailed{};
        std::array<DiagnosticCsmTimings, TimerLatency>
            m_TimerTimingSnapshots{};
        std::array<DiagnosticCsmStats, TimerLatency>
            m_TimerStatsSnapshots{};
        std::array<bool, TimerStageCount> m_TimerStageActive{};
        uint64_t m_TimerFrame = 0u;
        uint64_t m_LastPublishedTimerFrame = 0u;
        bool m_LastPublishedTimerFrameValid = false;
        uint32_t m_CurrentTimerSlot = 0u;
        uint32_t m_CurrentTimerMask = 0u;
        uint64_t m_CurrentTimerConfigurationGeneration = 0u;
        bool m_CurrentTimerAdmitted = false;
        bool m_CurrentDetailedTiming = false;
        bool m_TimerQueriesSupported = true;
        DiagnosticCascadedShadowMapSettings m_TimerConfiguration{};
        uint32_t m_TimerOutputWidth = 0u;
        uint32_t m_TimerOutputHeight = 0u;
        bool m_TimerConfigurationValid = false;
        uint64_t m_TimerConfigurationGeneration = 1u;

        DiagnosticCsmTimings m_Timings;
        DiagnosticCsmStats m_Stats;
        DiagnosticCsmTimings m_PublishedTimings;
        DiagnosticCsmStats m_PublishedStats;
        bool m_ReportedInvalidInput = false;

        void InvalidateCachedShadowDrawLists()
        {
            for (CachedShadowDrawListEntry& entry :
                m_CachedShadowDrawLists)
            {
                entry = {};
            }
            m_CachedShadowDrawListUse = 0u;
        }

        void InvalidateCache()
        {
            InvalidateCachedShadowDrawLists();
            m_CacheValid = false;
            m_PreviousProjectionValid.fill(false);
            m_CasterSnapshotValid.fill(false);
            for (auto& casters : m_CachedCasters)
                casters.clear();
            m_PreviousCascadeCount = 0u;
            m_PreviousSceneRevisionValid = false;
            m_PreviousRoot = nullptr;
        }

        void ClearPublishedTimingState()
        {
            m_PublishedTimings = {};
            m_PublishedTimings.supported = m_Timings.supported;
            m_PublishedStats = {};
            m_LastPublishedTimerFrame = 0u;
            m_LastPublishedTimerFrameValid = false;
        }

        void InvalidateTimerConfiguration()
        {
            m_TimerConfigurationValid = false;
            ClearPublishedTimingState();
        }

        void UpdateTimerConfiguration(
            const DiagnosticCascadedShadowMapSettings& settings,
            uint32_t outputWidth,
            uint32_t outputHeight)
        {
            if (m_TimerConfigurationValid &&
                IsSameDiagnosticCsmTimingConfiguration(
                    m_TimerConfiguration, settings) &&
                m_TimerOutputWidth == outputWidth &&
                m_TimerOutputHeight == outputHeight)
            {
                return;
            }

            ++m_TimerConfigurationGeneration;
            if (m_TimerConfigurationGeneration == 0u)
                ++m_TimerConfigurationGeneration;
            m_TimerConfiguration = settings;
            m_TimerOutputWidth = outputWidth;
            m_TimerOutputHeight = outputHeight;
            m_TimerConfigurationValid = true;
            ClearPublishedTimingState();
        }

        void AdvanceTimers()
        {
            bool published = false;
            uint64_t newestFrame = 0u;
            std::array<float, TimerStageCount> newest{};
            bool newestDetailed = false;
            DiagnosticCsmTimings newestTimingSnapshot;
            DiagnosticCsmStats newestStatsSnapshot;

            for (uint32_t slot = 0u; slot < TimerLatency; ++slot)
            {
                const uint32_t mask = m_TimerMasks[slot];
                if (mask == 0u)
                    continue;

                bool complete = true;
                for (uint32_t stage = 0u;
                    stage < TimerStageCount;
                    ++stage)
                {
                    if ((mask & (1u << stage)) == 0u)
                        continue;
                    if (m_TimerPending[stage][slot])
                    {
                        nvrhi::ITimerQuery* query =
                            m_TimerQueries[stage][slot];
                        if (m_Device->pollTimerQuery(query))
                        {
                            m_TimerValues[slot][stage] =
                                m_Device->getTimerQueryTime(query) * 1000.f;
                            m_Device->resetTimerQuery(query);
                            m_TimerPending[stage][slot] = false;
                        }
                    }
                    if (m_TimerPending[stage][slot])
                        complete = false;
                }
                if (!complete)
                    continue;

                if (m_TimerConfigurationGenerations[slot] ==
                        m_TimerConfigurationGeneration &&
                    (!published ||
                        m_TimerSourceFrames[slot] > newestFrame))
                {
                    published = true;
                    newestFrame = m_TimerSourceFrames[slot];
                    newest = m_TimerValues[slot];
                    newestDetailed = m_TimerDetailed[slot];
                    newestTimingSnapshot =
                        m_TimerTimingSnapshots[slot];
                    newestStatsSnapshot =
                        m_TimerStatsSnapshots[slot];
                }
                m_TimerMasks[slot] = 0u;
                m_TimerSourceFrames[slot] = 0u;
                m_TimerConfigurationGenerations[slot] = 0u;
                m_TimerValues[slot].fill(0.f);
                m_TimerTimingSnapshots[slot] = {};
                m_TimerStatsSnapshots[slot] = {};
            }

            if (published)
            {
                m_PublishedTimings = newestTimingSnapshot;
                m_PublishedStats = newestStatsSnapshot;
                m_PublishedTimings.totalMilliseconds =
                    newest[TimerTotal];
                m_PublishedTimings.clearUpdateMilliseconds =
                    newest[TimerClearUpdate];
                m_PublishedTimings.rasterMilliseconds =
                    newest[TimerRaster];
                m_PublishedTimings.samplingMilliseconds =
                    newest[TimerSampling];
                m_PublishedTimings.cullingGpuMilliseconds = 0.f;
                m_PublishedTimings.detailedGpuTimingEnabled =
                    newestDetailed;
                m_PublishedTimings.gpuTimingSource =
                    DiagnosticCsmGpuTimingSource::TimerQuery;
                m_PublishedTimings.gpuTimingSourceFrame =
                    newestFrame;
                m_PublishedTimings.active = true;
                m_LastPublishedTimerFrame = newestFrame;
                m_LastPublishedTimerFrameValid = true;
            }

            if (m_LastPublishedTimerFrameValid)
            {
                m_PublishedTimings.gpuTimingAgeFrames = uint32_t(std::min(
                    m_TimerFrame >= m_LastPublishedTimerFrame
                        ? m_TimerFrame - m_LastPublishedTimerFrame
                        : 0u,
                    uint64_t(std::numeric_limits<uint32_t>::max())));
            }
        }

        void CaptureTimerFrameSnapshot()
        {
            if (m_TimerMasks[m_CurrentTimerSlot] != 0u &&
                m_TimerSourceFrames[m_CurrentTimerSlot] == m_TimerFrame)
            {
                m_TimerTimingSnapshots[m_CurrentTimerSlot] = m_Timings;
                m_TimerStatsSnapshots[m_CurrentTimerSlot] = m_Stats;
            }
            else if (!m_TimerQueriesSupported ||
                !m_PublishedTimings.active)
            {
                m_PublishedTimings = m_Timings;
                m_PublishedTimings.gpuTimingSource =
                    DiagnosticCsmGpuTimingSource::Unavailable;
                m_PublishedTimings.gpuTimingSourceFrame = 0u;
                m_PublishedTimings.gpuTimingAgeFrames = 0u;
                m_PublishedStats = m_Stats;
            }
        }

        void BeginTimerFrame(bool detailed)
        {
            m_CurrentTimerSlot = uint32_t(m_TimerFrame % TimerLatency);
            m_CurrentTimerMask = 0u;
            m_CurrentTimerConfigurationGeneration =
                m_TimerConfigurationGeneration;
            m_CurrentDetailedTiming = detailed;
            m_CurrentTimerAdmitted =
                m_TimerQueriesSupported &&
                m_TimerMasks[m_CurrentTimerSlot] == 0u;
            for (uint32_t stage = 0u;
                stage < TimerStageCount && m_CurrentTimerAdmitted;
                ++stage)
            {
                m_CurrentTimerAdmitted =
                    !m_TimerPending[stage][m_CurrentTimerSlot];
            }
            m_TimerStageActive.fill(false);
            if (m_CurrentTimerAdmitted)
                m_TimerValues[m_CurrentTimerSlot].fill(0.f);
        }

        void BeginTimer(nvrhi::ICommandList* commandList, uint32_t stage)
        {
            if (!m_CurrentTimerAdmitted ||
                (stage != TimerTotal && !m_CurrentDetailedTiming))
            {
                return;
            }
            commandList->beginTimerQuery(
                m_TimerQueries[stage][m_CurrentTimerSlot]);
            m_TimerStageActive[stage] = true;
            m_CurrentTimerMask |= 1u << stage;
        }

        void EndTimer(nvrhi::ICommandList* commandList, uint32_t stage)
        {
            if (!m_TimerStageActive[stage])
                return;
            commandList->endTimerQuery(
                m_TimerQueries[stage][m_CurrentTimerSlot]);
            m_TimerPending[stage][m_CurrentTimerSlot] = true;
            m_TimerStageActive[stage] = false;
            if (stage == TimerTotal)
            {
                m_TimerMasks[m_CurrentTimerSlot] = m_CurrentTimerMask;
                m_TimerSourceFrames[m_CurrentTimerSlot] = m_TimerFrame;
                m_TimerConfigurationGenerations[m_CurrentTimerSlot] =
                    m_CurrentTimerConfigurationGeneration;
                m_TimerDetailed[m_CurrentTimerSlot] =
                    m_CurrentDetailedTiming;
            }
        }

        void EndTimerFrame()
        {
            m_CurrentTimerAdmitted = false;
            m_CurrentTimerMask = 0u;
            m_CurrentTimerConfigurationGeneration = 0u;
            ++m_TimerFrame;
        }

        bool EnsureDepthResources(
            const DiagnosticCascadedShadowMapSettings& settings)
        {
            m_ResourcesRecreatedThisFrame = false;
            const uint32_t resolution = settings.shadowMapResolution;
            if (resolution < 128u || resolution > 8192u)
                return false;

            const nvrhi::Format depthFormat = SelectDepthFormat(
                m_Device, settings.use16BitDepthEnabled);
            if (depthFormat == nvrhi::Format::UNKNOWN)
                return false;

            const bool recreateDepth = !m_Depth ||
                m_DepthResolution != resolution ||
                m_DepthFormat != depthFormat;
            const bool conservativeSaturatedSlopeActive =
                settings.precomputedDepthAxisInverseLengthEnabled &&
                settings.conservativeSaturatedSlopeEnabled;
            const bool algebraicSlowSlopeActive =
                settings.precomputedDepthAxisInverseLengthEnabled &&
                settings.algebraicSlowSlopeEnabled;
            const bool recreateDepthPass = !m_DepthPass ||
                m_AppliedDepthFormat != depthFormat ||
                m_AppliedPositionOnlyOpaqueEnabled !=
                    settings.positionOnlyOpaqueEnabled ||
                m_AppliedTranslationOnlyCasterTransformEnabled !=
                    settings.translationOnlyCasterTransformEnabled ||
                m_AppliedPrecomputedDepthAxisInverseLengthEnabled !=
                    settings.precomputedDepthAxisInverseLengthEnabled ||
                m_AppliedConservativeSaturatedSlopeEnabled !=
                    conservativeSaturatedSlopeActive ||
                m_AppliedAlgebraicSlowSlopeEnabled !=
                    algebraicSlowSlopeActive;

            if (recreateDepth)
            {
                m_ResolveBindingSet = nullptr;
                m_DepthScrollBindingSet = nullptr;
                m_ScratchScrollBindingSet = nullptr;
                m_FramebufferFactory.reset();
                m_ScrollFramebufferFactory.reset();
                m_ClearPipeline = nullptr;
                m_ScrollPipeline = nullptr;
                m_Depth = nullptr;
                m_ScrollScratch = nullptr;
                m_ScrollScratchView.reset();

                nvrhi::TextureDesc depthDesc;
                depthDesc.width = resolution;
                depthDesc.height = resolution;
                depthDesc.arraySize = DiagnosticCsmMaximumCascades;
                depthDesc.format = depthFormat;
                depthDesc.dimension =
                    nvrhi::TextureDimension::Texture2DArray;
                depthDesc.isRenderTarget = true;
                depthDesc.isTypeless = true;
                depthDesc.useClearValue = true;
                depthDesc.clearValue = nvrhi::Color(1.f);
                depthDesc.debugName = "Diagnostic CSM Depth";
                depthDesc.enableAutomaticStateTracking(
                    nvrhi::ResourceStates::ShaderResource);
                m_Depth = m_Device->createTexture(depthDesc);
                if (!m_Depth)
                {
                    log::error(
                        "Diagnostic CSM could not allocate its persistent depth array.");
                    return false;
                }

                m_FramebufferFactory =
                    std::make_unique<FramebufferFactory>(m_Device);
                m_FramebufferFactory->DepthTarget = m_Depth;
                for (uint32_t cascade = 0u;
                    cascade < DiagnosticCsmMaximumCascades;
                    ++cascade)
                {
                    if (!m_CascadeViews[cascade])
                    {
                        m_CascadeViews[cascade] =
                            std::make_shared<ScissoredPlanarView>();
                    }
                    m_CascadeViews[cascade]->SetViewport(
                        nvrhi::Viewport(float(resolution), float(resolution)));
                    m_CascadeViews[cascade]->SetArraySlice(int(cascade));
                    m_CascadeViews[cascade]->SetScissorRect(
                        nvrhi::Rect(int(resolution), int(resolution)));
                }

                nvrhi::GraphicsPipelineDesc clearDesc;
                clearDesc.primType = nvrhi::PrimitiveType::TriangleList;
                clearDesc.VS = m_ClearVertexShader;
                clearDesc.renderState.rasterState.setCullNone();
                clearDesc.renderState.rasterState.enableScissor();
                clearDesc.renderState.rasterState.enableDepthClip();
                clearDesc.renderState.depthStencilState.enableDepthTest();
                clearDesc.renderState.depthStencilState.enableDepthWrite();
                clearDesc.renderState.depthStencilState.setDepthFunc(
                    nvrhi::ComparisonFunc::Always);
                m_ClearPipeline = m_Device->createGraphicsPipeline(
                    clearDesc,
                    m_FramebufferFactory->GetFramebufferInfo());
                if (!m_ClearPipeline)
                {
                    log::error(
                        "Diagnostic CSM could not create its rectangular depth-clear pipeline.");
                    return false;
                }

                m_DepthResolution = resolution;
                m_DepthFormat = depthFormat;
                m_ResourcesRecreatedThisFrame = true;
                InvalidateCache();
            }

            if (recreateDepthPass)
            {
                m_DepthPass = std::make_unique<DiagnosticCsmDepthPass>(
                    m_Device,
                    m_CommonPasses,
                    settings.positionOnlyOpaqueEnabled,
                    settings.translationOnlyCasterTransformEnabled,
                    settings.precomputedDepthAxisInverseLengthEnabled,
                    conservativeSaturatedSlopeActive,
                    algebraicSlowSlopeActive);
                DepthPass::CreateParameters parameters;
                parameters.depthBias = 0;
                parameters.slopeScaledDepthBias = 0.f;
                parameters.trackLiveness = false;
                parameters.useInputAssembler = false;
                parameters.numConstantBufferVersions = 64u;
                m_DepthPass->Init(*m_ShaderFactory, parameters);
                if (!m_DepthPass->IsReady())
                {
                    log::error(
                        "Diagnostic CSM could not initialize the shared opaque/alpha-tested depth pass.");
                    m_DepthPass.reset();
                    return false;
                }
                m_AppliedDepthFormat = depthFormat;
                m_AppliedPositionOnlyOpaqueEnabled =
                    settings.positionOnlyOpaqueEnabled;
                m_AppliedTranslationOnlyCasterTransformEnabled =
                    settings.translationOnlyCasterTransformEnabled;
                m_AppliedPrecomputedDepthAxisInverseLengthEnabled =
                    settings.precomputedDepthAxisInverseLengthEnabled;
                m_AppliedConservativeSaturatedSlopeEnabled =
                    conservativeSaturatedSlopeActive;
                m_AppliedAlgebraicSlowSlopeEnabled =
                    algebraicSlowSlopeActive;
                if (!recreateDepth)
                {
                    m_ResourcesRecreatedThisFrame = true;
                    InvalidateCache();
                }
            }

            if (!settings.inputAssemblerCasterFetchEnabled)
            {
                m_InputAssemblerDepthPass.reset();
                m_AppliedInputAssemblerCasterFetchEnabled = false;
                m_InputAssemblerCasterFetchCreationFailed = false;
            }
            else if ((!m_InputAssemblerDepthPass &&
                    !m_InputAssemblerCasterFetchCreationFailed) ||
                recreateDepthPass)
            {
                auto inputAssemblerDepthPass =
                    std::make_unique<DiagnosticCsmDepthPass>(
                        m_Device,
                        m_CommonPasses,
                        settings.positionOnlyOpaqueEnabled,
                        false,
                        settings.precomputedDepthAxisInverseLengthEnabled,
                        conservativeSaturatedSlopeActive,
                        algebraicSlowSlopeActive);
                DepthPass::CreateParameters parameters;
                parameters.depthBias = 0;
                parameters.slopeScaledDepthBias = 0.f;
                parameters.trackLiveness = false;
                parameters.useInputAssembler = true;
                parameters.numConstantBufferVersions = 64u;
                inputAssemblerDepthPass->Init(
                    *m_ShaderFactory, parameters);
                if (!inputAssemblerDepthPass->IsReady())
                {
                    log::warning(
                        "Diagnostic CSM input-assembler caster fetch is unavailable; retaining the manual-fetch fallback.");
                    m_InputAssemblerDepthPass.reset();
                    m_AppliedInputAssemblerCasterFetchEnabled = false;
                    m_InputAssemblerCasterFetchCreationFailed = true;
                }
                else
                {
                    m_InputAssemblerDepthPass =
                        std::move(inputAssemblerDepthPass);
                    m_AppliedInputAssemblerCasterFetchEnabled = true;
                    m_InputAssemblerCasterFetchCreationFailed = false;
                }
            }

            const bool scrollingActive =
                settings.scrollingEnabled &&
                settings.wholeCascadeReuseEnabled;
            if (scrollingActive)
            {
                if (!m_ScrollScratch ||
                    m_ScrollScratch->getDesc().width != resolution ||
                    m_ScrollScratch->getDesc().format != depthFormat)
                {
                    m_ScratchScrollBindingSet = nullptr;
                    m_ScrollFramebufferFactory.reset();
                    m_ScrollScratchView.reset();
                    nvrhi::TextureDesc scratchDesc;
                    scratchDesc.width = resolution;
                    scratchDesc.height = resolution;
                    scratchDesc.arraySize = 1u;
                    scratchDesc.format = depthFormat;
                    scratchDesc.dimension =
                        nvrhi::TextureDimension::Texture2DArray;
                    scratchDesc.isRenderTarget = true;
                    scratchDesc.isTypeless = true;
                    scratchDesc.useClearValue = true;
                    scratchDesc.clearValue = nvrhi::Color(1.f);
                    scratchDesc.debugName =
                        "Diagnostic CSM Scroll Scratch";
                    scratchDesc.enableAutomaticStateTracking(
                        nvrhi::ResourceStates::ShaderResource);
                    m_ScrollScratch =
                        m_Device->createTexture(scratchDesc);
                    if (!m_ScrollScratch)
                    {
                        log::error(
                            "Diagnostic CSM could not allocate its scrolling scratch map.");
                        return false;
                    }
                    m_ResourcesRecreatedThisFrame = true;
                }

                if (!m_ScrollFramebufferFactory)
                {
                    m_ScrollFramebufferFactory =
                        std::make_unique<FramebufferFactory>(m_Device);
                    m_ScrollFramebufferFactory->DepthTarget =
                        m_ScrollScratch;
                }
                if (!m_ScrollScratchView)
                {
                    m_ScrollScratchView =
                        std::make_shared<ScissoredPlanarView>();
                    m_ScrollScratchView->SetViewport(
                        nvrhi::Viewport(float(resolution), float(resolution)));
                    m_ScrollScratchView->SetArraySlice(0);
                    m_ScrollScratchView->SetScissorRect(
                        nvrhi::Rect(int(resolution), int(resolution)));
                }

                if (!m_DepthScrollBindingSet)
                {
                    nvrhi::BindingSetDesc desc;
                    desc.bindings = {
                        nvrhi::BindingSetItem::ConstantBuffer(
                            0, m_ScrollConstants),
                        nvrhi::BindingSetItem::Texture_SRV(0, m_Depth)
                    };
                    m_DepthScrollBindingSet =
                        m_Device->createBindingSet(
                            desc, m_ScrollBindingLayout);
                }
                if (!m_ScratchScrollBindingSet)
                {
                    nvrhi::BindingSetDesc desc;
                    desc.bindings = {
                        nvrhi::BindingSetItem::ConstantBuffer(
                            0, m_ScrollConstants),
                        nvrhi::BindingSetItem::Texture_SRV(
                            0, m_ScrollScratch)
                    };
                    m_ScratchScrollBindingSet =
                        m_Device->createBindingSet(
                            desc, m_ScrollBindingLayout);
                }
                if (!m_ScrollPipeline)
                {
                    nvrhi::GraphicsPipelineDesc desc;
                    desc.primType = nvrhi::PrimitiveType::TriangleList;
                    desc.VS = m_ClearVertexShader;
                    desc.PS = m_ScrollPixelShader;
                    desc.bindingLayouts = { m_ScrollBindingLayout };
                    desc.renderState.rasterState.setCullNone();
                    desc.renderState.rasterState.enableScissor();
                    desc.renderState.rasterState.enableDepthClip();
                    desc.renderState.depthStencilState.enableDepthTest();
                    desc.renderState.depthStencilState.enableDepthWrite();
                    desc.renderState.depthStencilState.setDepthFunc(
                        nvrhi::ComparisonFunc::Always);
                    m_ScrollPipeline =
                        m_Device->createGraphicsPipeline(
                            desc,
                            m_FramebufferFactory->GetFramebufferInfo());
                }
                if (!m_ScrollFramebufferFactory ||
                    !m_ScrollScratchView ||
                    !m_DepthScrollBindingSet ||
                    !m_ScratchScrollBindingSet ||
                    !m_ScrollPipeline)
                {
                    log::error(
                        "Diagnostic CSM could not initialize its D3D12-safe scrolling depth blit resources.");
                    return false;
                }
            }
            else
            {
                m_ScrollScratch = nullptr;
                m_DepthScrollBindingSet = nullptr;
                m_ScratchScrollBindingSet = nullptr;
                m_ScrollFramebufferFactory.reset();
                m_ScrollScratchView.reset();
                m_ScrollPipeline = nullptr;
            }
            return true;
        }

        bool EnsureResolveResources(
            nvrhi::ITexture* cameraDepth,
            nvrhi::ITexture* cameraNormals)
        {
            if (!cameraDepth || !cameraNormals || !m_Depth)
                return false;
            const nvrhi::TextureDesc& cameraDesc = cameraDepth->getDesc();
            const nvrhi::TextureDesc& normalDesc = cameraNormals->getDesc();
            if (cameraDesc.width == 0u || cameraDesc.height == 0u ||
                cameraDesc.dimension != nvrhi::TextureDimension::Texture2D ||
                cameraDesc.sampleCount != 1u ||
                normalDesc.width != cameraDesc.width ||
                normalDesc.height != cameraDesc.height ||
                normalDesc.dimension != nvrhi::TextureDimension::Texture2D ||
                normalDesc.sampleCount != 1u)
            {
                return false;
            }

            const bool recreate = !m_Visibility ||
                m_Visibility->getDesc().width != cameraDesc.width ||
                m_Visibility->getDesc().height != cameraDesc.height;
            if (recreate)
            {
                m_ResolveBindingSet = nullptr;
                nvrhi::TextureDesc visibilityDesc;
                visibilityDesc.width = cameraDesc.width;
                visibilityDesc.height = cameraDesc.height;
                visibilityDesc.format = nvrhi::Format::R8_UNORM;
                visibilityDesc.dimension =
                    nvrhi::TextureDimension::Texture2D;
                visibilityDesc.isUAV = true;
                visibilityDesc.debugName =
                    "Diagnostic CSM Full-Resolution Visibility";
                visibilityDesc.enableAutomaticStateTracking(
                    nvrhi::ResourceStates::ShaderResource);
                m_Visibility = m_Device->createTexture(visibilityDesc);
                visibilityDesc.debugName =
                    "Diagnostic CSM Debug Visualization";
                m_DebugVisualization =
                    m_Device->createTexture(visibilityDesc);
                if (!m_Visibility || !m_DebugVisualization)
                {
                    log::error(
                        "Diagnostic CSM could not allocate its full-resolution outputs.");
                    m_Visibility = nullptr;
                    m_DebugVisualization = nullptr;
                    return false;
                }
                m_ResourcesRecreatedThisFrame = true;
            }

            if (!m_ResolveBindingSet ||
                m_BoundCameraDepth != cameraDepth ||
                m_BoundCameraNormals != cameraNormals)
            {
                nvrhi::BindingSetDesc desc;
                desc.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        0, m_ResolveConstants),
                    nvrhi::BindingSetItem::Texture_SRV(0, cameraDepth),
                    nvrhi::BindingSetItem::Texture_SRV(1, m_Depth),
                    nvrhi::BindingSetItem::Texture_SRV(2, cameraNormals),
                    nvrhi::BindingSetItem::Texture_UAV(0, m_Visibility),
                    nvrhi::BindingSetItem::Texture_UAV(
                        1, m_DebugVisualization),
                    nvrhi::BindingSetItem::Sampler(
                        0, m_ShadowDepthSampler)
                };
                m_ResolveBindingSet = m_Device->createBindingSet(
                    desc, m_ResolveBindingLayout);
                m_ResourcesRecreatedThisFrame = true;
                m_BoundCameraDepth = cameraDepth;
                m_BoundCameraNormals = cameraNormals;
            }
            return bool(m_ResolveBindingSet);
        }

        bool UpdateCascadeViews(
            const DiagnosticCascadedShadowMapSettings& settings,
            const IView& cameraView,
            const DirectionalLight& light)
        {
            if (!cameraView.IsReverseDepth() || !light.GetNode())
                return false;
            const float3 lightDirection = float3(light.GetDirection());
            if (!all(dm::isfinite(lightDirection)) ||
                !(length(lightDirection) > 1e-6f))
            {
                return false;
            }

            using ProjectionCorners =
                std::array<float3, frustum::numCorners>;
            const auto buildProjectionCorners =
                [&settings](
                    const float4x4& projection,
                    ProjectionCorners& corners)
            {
                if (!IsFiniteMatrix(projection))
                    return false;
                frustum projectionFrustum(projection, true);
                plane& nearPlane = projectionFrustum.nearPlane();
                plane& farPlane = projectionFrustum.farPlane();
                if (!all(dm::isfinite(nearPlane.normal)) ||
                    !std::isfinite(nearPlane.distance) ||
                    !(length(nearPlane.normal) > 0.f))
                {
                    return false;
                }
                farPlane.normal = -nearPlane.normal;
                farPlane.distance = -nearPlane.distance +
                    settings.maximumShadowDistance;
                for (uint32_t corner = 0u;
                    corner < frustum::numCorners;
                    ++corner)
                {
                    corners[corner] =
                        projectionFrustum.getCorner(corner);
                    if (!all(dm::isfinite(corners[corner])))
                        return false;
                }
                return true;
            };

            const float4x4 unjitteredProjection =
                cameraView.GetProjectionMatrix(false);
            ProjectionCorners frustumCorners;
            if (!buildProjectionCorners(
                    unjitteredProjection, frustumCorners))
                return false;
            const bool needsJitteredReceiverCorners =
                (settings.accurateCasterCullingEnabled ||
                    settings.receiverRasterScissorEnabled) &&
                CanUseDiagnosticCsmViewDependentCasterCulling(settings);
            ProjectionCorners receiverFrustumCorners;
            if (needsJitteredReceiverCorners &&
                !buildProjectionCorners(
                    cameraView.GetProjectionMatrix(true),
                    receiverFrustumCorners))
            {
                return false;
            }

            const nvrhi::ViewportState cameraViewportState =
                cameraView.GetViewportState();
            if (cameraViewportState.viewports.size() != 1u)
                return false;
            const nvrhi::Viewport& cameraViewport =
                cameraViewportState.viewports[0];
            const float cameraViewportWidth = cameraViewport.width();
            const float cameraViewportHeight = cameraViewport.height();
            const float2 currentPixelOffset = cameraView.GetPixelOffset();
            if (!std::isfinite(cameraViewportWidth) ||
                !std::isfinite(cameraViewportHeight) ||
                !(cameraViewportWidth > 0.f) ||
                !(cameraViewportHeight > 0.f) ||
                !all(dm::isfinite(currentPixelOffset)))
            {
                return false;
            }

            // Keep the snapped projection independent of the current TAA
            // phase while guaranteeing that every receiver generated by the
            // current projection remains covered. UVSR's jitter is subpixel;
            // rounding a minimum one-pixel envelope upward also fails safely
            // for a caller that supplies a larger custom offset.
            const float2 jitterEnvelopePixels(
                std::max(1.f, std::ceil(std::abs(currentPixelOffset.x))),
                std::max(1.f, std::ceil(std::abs(currentPixelOffset.y))));
            const float2 jitterEnvelopeNdc(
                2.f * jitterEnvelopePixels.x / cameraViewportWidth,
                2.f * jitterEnvelopePixels.y / cameraViewportHeight);

            const float3 projectionNearCenter =
                (frustumCorners[frustum::C_BOTTOM |
                    frustum::C_LEFT | frustum::C_NEAR] +
                 frustumCorners[frustum::C_TOP |
                    frustum::C_RIGHT | frustum::C_NEAR]) * 0.5f;
            const float3 projectionFarCenter =
                (frustumCorners[frustum::C_BOTTOM |
                    frustum::C_LEFT | frustum::C_FAR] +
                 frustumCorners[frustum::C_TOP |
                    frustum::C_RIGHT | frustum::C_FAR]) * 0.5f;
            const float nearDistance = projectionNearCenter.z;
            const float farDistance = projectionFarCenter.z;
            if (!std::isfinite(nearDistance) ||
                !std::isfinite(farDistance) ||
                !(nearDistance >= 0.f) ||
                !(farDistance > nearDistance))
            {
                return false;
            }

            const DiagnosticCsmSplitSet splits = ComputeUeCsmSplits(
                nearDistance,
                std::min(settings.maximumShadowDistance, farDistance),
                settings.cascadeDistributionExponent,
                settings.cascadeCount);
            if (!splits.valid)
                return false;

            const daffine3 sourceLightToWorld =
                light.GetNode()->GetLocalToWorldTransform();
            const double3 sourceRight =
                sourceLightToWorld.m_linear.row0;
            const double3 sourceUp =
                sourceLightToWorld.m_linear.row1;
            const double3 sourceBackward =
                sourceLightToWorld.m_linear.row2;
            DiagnosticCsmLightFrame lightFrame;
            if (!TryBuildDiagnosticCsmLightFrame(
                    {
                        -sourceBackward.x,
                        -sourceBackward.y,
                        -sourceBackward.z
                    },
                    { sourceUp.x, sourceUp.y, sourceUp.z },
                    {
                        sourceRight.x,
                        sourceRight.y,
                        sourceRight.z
                    },
                    lightFrame))
            {
                return false;
            }
            const daffine3 lightToWorldDouble(
                double3(
                    lightFrame.x[0],
                    lightFrame.x[1],
                    lightFrame.x[2]),
                double3(
                    lightFrame.y[0],
                    lightFrame.y[1],
                    lightFrame.y[2]),
                double3(
                    lightFrame.z[0],
                    lightFrame.z[1],
                    lightFrame.z[2]),
                double3(0.0));
            const affine3 lightToWorld = affine3(lightToWorldDouble);
            const affine3 worldToLight = affine3(inverse(lightToWorldDouble));
            const affine3 viewToWorld = cameraView.GetInverseViewMatrix();
            if (!IsFiniteAffineTransform(lightToWorld) ||
                !IsFiniteAffineTransform(worldToLight) ||
                !IsFiniteAffineTransform(viewToWorld))
            {
                return false;
            }

            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                CascadeProjection& output = m_CurrentProjections[cascade];
                output = {};
                output.range = ComputeUeCsmCascadeRange(
                    splits.distances[cascade],
                    splits.distances[cascade + 1u],
                    settings.cascadeTransitionFraction,
                    cascade + 1u == settings.cascadeCount);

                const float nearFraction =
                    (output.range.nominalNear - nearDistance) /
                    (farDistance - nearDistance);
                const float farFraction =
                    (output.range.projectedFar - nearDistance) /
                    (farDistance - nearDistance);
                if (!std::isfinite(nearFraction) ||
                    !std::isfinite(farFraction))
                {
                    return false;
                }

                ProjectionCorners cascadeCorners;
                ProjectionCorners receiverCascadeCorners;
                for (uint32_t corner = 0u;
                    corner < frustum::numCorners;
                    ++corner)
                {
                    const float interpolation = (corner & 4u)
                        ? farFraction
                        : nearFraction;
                    cascadeCorners[corner] = lerp(
                        frustumCorners[corner & 3u],
                        frustumCorners[(corner & 3u) + 4u],
                        interpolation);
                    if (needsJitteredReceiverCorners)
                    {
                        receiverCascadeCorners[corner] = lerp(
                            receiverFrustumCorners[corner & 3u],
                            receiverFrustumCorners[
                                (corner & 3u) + 4u],
                            interpolation);
                    }
                }

                const float3 nearCenter =
                    (cascadeCorners[frustum::C_BOTTOM |
                        frustum::C_LEFT | frustum::C_NEAR] +
                     cascadeCorners[frustum::C_TOP |
                        frustum::C_RIGHT | frustum::C_NEAR]) * 0.5f;
                const float3 farCenter =
                    (cascadeCorners[frustum::C_BOTTOM |
                        frustum::C_LEFT | frustum::C_FAR] +
                     cascadeCorners[frustum::C_TOP |
                        frustum::C_RIGHT | frustum::C_FAR]) * 0.5f;
                const float centerDistance = length(farCenter - nearCenter);
                const float nearRadius = length(
                    cascadeCorners[frustum::C_BOTTOM |
                        frustum::C_LEFT | frustum::C_NEAR] -
                    cascadeCorners[frustum::C_TOP |
                        frustum::C_RIGHT | frustum::C_NEAR]) * 0.5f;
                const float farRadius = length(
                    cascadeCorners[frustum::C_BOTTOM |
                        frustum::C_LEFT | frustum::C_FAR] -
                    cascadeCorners[frustum::C_TOP |
                        frustum::C_RIGHT | frustum::C_FAR]) * 0.5f;

                float centerOffset = 0.f;
                if (centerDistance > 1e-5f)
                {
                    centerOffset =
                        (centerDistance * centerDistance +
                            farRadius * farRadius -
                            nearRadius * nearRadius) /
                        (2.f * centerDistance);
                    centerOffset = std::clamp(
                        centerOffset, 0.f, centerDistance);
                }
                const float3 sphereCenterView = centerDistance > 1e-5f
                    ? nearCenter + normalize(farCenter - nearCenter) *
                        centerOffset
                    : nearCenter;
                float sphereRadius = std::sqrt(std::max(
                    farRadius * farRadius +
                        (centerDistance - centerOffset) *
                        (centerDistance - centerOffset),
                    nearRadius * nearRadius));
                for (const float3& corner : cascadeCorners)
                {
                    sphereRadius = std::max(
                        sphereRadius,
                        length(corner - sphereCenterView));
                }
                float maximumHalfWidth = 0.f;
                float maximumHalfHeight = 0.f;
                for (uint32_t planeIndex = 0u;
                    planeIndex < 2u;
                    ++planeIndex)
                {
                    float minimumX =
                        std::numeric_limits<float>::infinity();
                    float maximumX =
                        -std::numeric_limits<float>::infinity();
                    float minimumY =
                        std::numeric_limits<float>::infinity();
                    float maximumY =
                        -std::numeric_limits<float>::infinity();
                    const uint32_t firstCorner = planeIndex * 4u;
                    for (uint32_t planeCorner = 0u;
                        planeCorner < 4u;
                        ++planeCorner)
                    {
                        const float3& corner =
                            cascadeCorners[firstCorner + planeCorner];
                        minimumX = std::min(minimumX, corner.x);
                        maximumX = std::max(maximumX, corner.x);
                        minimumY = std::min(minimumY, corner.y);
                        maximumY = std::max(maximumY, corner.y);
                    }
                    maximumHalfWidth = std::max(
                        maximumHalfWidth,
                        (maximumX - minimumX) * 0.5f);
                    maximumHalfHeight = std::max(
                        maximumHalfHeight,
                        (maximumY - minimumY) * 0.5f);
                }
                const float jitterEnvelopeWidth =
                    jitterEnvelopeNdc.x * maximumHalfWidth;
                const float jitterEnvelopeHeight =
                    jitterEnvelopeNdc.y * maximumHalfHeight;
                const float jitterEnvelopeRadius = std::sqrt(
                    jitterEnvelopeWidth * jitterEnvelopeWidth +
                    jitterEnvelopeHeight * jitterEnvelopeHeight);
                sphereRadius += jitterEnvelopeRadius;
                sphereRadius = std::max(1.f, std::ceil(sphereRadius));
                const float filterGuardTexels =
                    ComputeDiagnosticCsmProjectionGuardTexels(
                        EffectiveFilterRadiusTexels(settings),
                        settings.projectionSnapTexelMultiple);
                const float interiorResolution =
                    float(settings.shadowMapResolution) -
                    2.f * filterGuardTexels;
                if (!(interiorResolution > 0.f))
                    return false;
                sphereRadius = std::ceil(
                    sphereRadius *
                    float(settings.shadowMapResolution) /
                    interiorResolution);
                if (!std::isfinite(sphereRadius))
                    return false;

                const float3 sphereCenterWorld =
                    viewToWorld.transformPoint(sphereCenterView);
                float3 centerLight =
                    worldToLight.transformPoint(sphereCenterWorld);
                if (!all(dm::isfinite(centerLight)))
                    return false;
                const float texelWorldSize =
                    2.f * sphereRadius /
                    float(settings.shadowMapResolution);
                if (!(texelWorldSize > 0.f) ||
                    !std::isfinite(texelWorldSize))
                {
                    return false;
                }
                const float projectionSnapWorldSize = texelWorldSize *
                    float(std::max(
                        settings.projectionSnapTexelMultiple, 1u));
                centerLight.xy() = float2(
                    SnapUeCsmProjectionCoordinate(
                        centerLight.x, projectionSnapWorldSize),
                    SnapUeCsmProjectionCoordinate(
                        centerLight.y, projectionSnapWorldSize));
                if (!all(dm::isfinite(centerLight)))
                    return false;
                const float3 snappedCenterWorld =
                    lightToWorld.transformPoint(centerLight);
                if (!all(dm::isfinite(snappedCenterWorld)))
                    return false;
                const float depthSpan =
                    ComputeDiagnosticCsmLightDepthSpan(
                        sphereRadius,
                        settings.enforceUeMinimumLightDepth
                            ? std::max(
                                settings.maximumLightDepth,
                                10000.f)
                            : settings.maximumLightDepth);
                const float halfDepth = depthSpan * 0.5f;
                if (!std::isfinite(halfDepth) || !(halfDepth > 0.f))
                    return false;

                const affine3 shadowView =
                    translation(-snappedCenterWorld) * worldToLight;
                const float4x4 shadowProjection = orthoProjD3DStyle(
                    -sphereRadius, sphereRadius,
                    -sphereRadius, sphereRadius,
                    -halfDepth, halfDepth);
                ScissoredPlanarView& view = *m_CascadeViews[cascade];
                view.SetMatrices(shadowView, shadowProjection);
                view.SetScissorRect(nvrhi::Rect(
                    int(settings.shadowMapResolution),
                    int(settings.shadowMapResolution)));
                view.UpdateCache();
                if (settings.precomputedDepthAxisInverseLengthEnabled)
                {
                    const float4x4 depthWorldToClip =
                        view.GetViewProjectionMatrix();
                    output.inverseDepthAxisLength =
                        ComputeDiagnosticCsmDepthAxisInverseLength({
                            depthWorldToClip[0][2],
                            depthWorldToClip[1][2],
                            depthWorldToClip[2][2]
                        });
                }

                auto& cullVolume = output.casterCullVolume;
                cullVolume = {};
                if (settings.accurateCasterCullingEnabled &&
                    CanUseDiagnosticCsmViewDependentCasterCulling(settings))
                {
                    cullVolume.worldToLight = worldToLight;
                    cullVolume.receiverMaximumLightZ =
                        -std::numeric_limits<float>::infinity();
                    std::array<float2, frustum::numCorners>
                        receiverLightPoints{};
                    for (uint32_t corner = 0u;
                        corner < frustum::numCorners;
                        ++corner)
                    {
                        const float3 receiverWorld =
                            viewToWorld.transformPoint(
                                receiverCascadeCorners[corner]);
                        const float3 receiverLight =
                            worldToLight.transformPoint(receiverWorld);
                        receiverLightPoints[corner] = receiverLight.xy();
                        cullVolume.receiverMaximumLightZ = std::max(
                            cullVolume.receiverMaximumLightZ,
                            receiverLight.z);
                    }
                    const float lightSpaceScale = std::max({
                        length(worldToLight.transformVector(
                            float3(1.f, 0.f, 0.f))),
                        length(worldToLight.transformVector(
                            float3(0.f, 1.f, 0.f))),
                        length(worldToLight.transformVector(
                            float3(0.f, 0.f, 1.f)))
                    });
                    cullVolume.filterMargin =
                        (std::ceil(
                            EffectiveFilterRadiusTexels(settings) +
                            0.5f) + 1.f) * texelWorldSize *
                        lightSpaceScale;
                    if (!std::isfinite(cullVolume.filterMargin) ||
                        !BuildProjectedReceiverHull(
                            receiverLightPoints,
                            cullVolume,
                            settings.
                                precomputedReceiverHullAxesEnabled))
                    {
                        // A malformed or degenerate hull must only reduce
                        // culling efficiency. It must never turn into missing
                        // casters.
                        cullVolume = {};
                    }
                }

                output.worldToUvzw = MakeWorldToUvzw(view);
                if (!IsFiniteMatrix(output.worldToUvzw))
                    return false;

                output.receiverRasterScissor =
                    MakeFullDiagnosticCsmRect(
                        settings.shadowMapResolution);
                if (CanUseDiagnosticCsmReceiverRasterScissor(
                        settings))
                {
                    std::array<std::array<float, 2u>,
                        DiagnosticCsmReceiverCornerCount>
                            receiverUv{};
                    bool receiverProjectionValid = true;
                    for (uint32_t corner = 0u;
                        corner < DiagnosticCsmReceiverCornerCount;
                        ++corner)
                    {
                        const float3 receiverWorld =
                            viewToWorld.transformPoint(
                                receiverCascadeCorners[corner]);
                        const float4 receiverUvzw =
                            float4(receiverWorld, 1.f) *
                                output.worldToUvzw;
                        if (!all(dm::isfinite(receiverUvzw)) ||
                            !(std::abs(receiverUvzw.w) > 1e-8f))
                        {
                            receiverProjectionValid = false;
                            break;
                        }
                        const float inverseW = 1.f / receiverUvzw.w;
                        receiverUv[corner] = {
                            receiverUvzw.x * inverseW,
                            receiverUvzw.y * inverseW
                        };
                    }
                    const float projectionGuard =
                        ComputeDiagnosticCsmProjectionGuardTexels(
                            EffectiveFilterRadiusTexels(settings),
                            settings.projectionSnapTexelMultiple);
                    const bool guardValid =
                        std::isfinite(projectionGuard) &&
                        projectionGuard >= 0.f &&
                        projectionGuard <=
                            float(std::numeric_limits<uint32_t>::max());
                    output.receiverRasterScissorValid =
                        receiverProjectionValid &&
                        guardValid &&
                        TryBuildDiagnosticCsmReceiverRasterScissor(
                            receiverUv,
                            settings.shadowMapResolution,
                            guardValid
                                ? uint32_t(projectionGuard)
                                : 0u,
                            output.receiverRasterScissor);
                }
                auto& compatibility = output.compatibility;
                compatibility.lightIdentity = &light;
                uint32_t element = 0u;
                for (uint32_t row = 0u; row < 3u; ++row)
                {
                    for (uint32_t column = 0u;
                        column < 3u;
                        ++column)
                    {
                        compatibility.lightBasis[element++] =
                            worldToLight.m_linear[row][column];
                    }
                }
                compatibility.radius = sphereRadius;
                compatibility.texelWorldSize = texelWorldSize;
                compatibility.snappedCenterX = centerLight.x;
                compatibility.snappedCenterY = centerLight.y;
                compatibility.snappedCenterZ = centerLight.z;
                compatibility.depthNear = -halfDepth;
                compatibility.depthFar = halfDepth;
                compatibility.splitNear = output.range.nominalNear;
                compatibility.splitFar = output.range.projectedFar;
                compatibility.depthBias = float(settings.depthBias) *
                    settings.directionalLightShadowBias;
                compatibility.slopeScaledDepthBias =
                    settings.slopeScaledDepthBias *
                    settings.directionalLightShadowSlopeBias;
                compatibility.resolution = settings.shadowMapResolution;
                compatibility.formatKey = uint32_t(m_DepthFormat);
                compatibility.normalDepth = true;
                if (!IsFiniteDiagnosticCsmProjectionCompatibility(
                        compatibility))
                {
                    return false;
                }
                output.valid = true;
            }

            for (uint32_t cascade = settings.cascadeCount;
                cascade < DiagnosticCsmMaximumCascades;
                ++cascade)
            {
                m_CurrentProjections[cascade] = {};
            }
            return true;
        }

        [[nodiscard]] bool ProjectBounds(
            const box3& bounds,
            const float4x4& worldToUvzw,
            uint32_t resolution,
            uint32_t halo,
            DiagnosticCsmRect& rectangle) const
        {
            rectangle = {};
            if (!IsFiniteBox(bounds))
                return false;

            float2 minimum = float2(
                std::numeric_limits<float>::max());
            float2 maximum = float2(
                -std::numeric_limits<float>::max());
            for (uint32_t corner = 0u;
                corner < box3::numCorners;
                ++corner)
            {
                const float4 position =
                    float4(bounds.getCorner(corner), 1.f) * worldToUvzw;
                if (!all(dm::isfinite(position)) ||
                    !(std::abs(position.w) > 1e-8f))
                {
                    return false;
                }
                const float2 uv = position.xy() / position.w;
                if (!all(dm::isfinite(uv)))
                    return false;
                minimum = min(minimum, uv);
                maximum = max(maximum, uv);
            }

            rectangle = MakeClippedDiagnosticCsmRectFromUvBounds(
                minimum.x,
                maximum.x,
                minimum.y,
                maximum.y,
                resolution,
                halo);
            return true;
        }

        void BeginTranslationOnlyCasterGeneration(
            bool enabled,
            const std::shared_ptr<SceneGraphNode>& rootNode)
        {
            if (!enabled)
                return;

            size_t instanceCount = 0u;
            if (rootNode)
            {
                if (const std::shared_ptr<SceneGraph> graph =
                        rootNode->GetGraph())
                {
                    instanceCount = graph->GetMeshInstances().size();
                }
            }
            instanceCount = std::min(
                instanceCount,
                size_t(std::numeric_limits<uint32_t>::max()));
            m_TranslationOnlyCasters.resize(instanceCount);

            ++m_TranslationOnlyCasterGeneration;
            if (m_TranslationOnlyCasterGeneration != 0u)
                return;

            for (TranslationOnlyCasterEntry& entry :
                m_TranslationOnlyCasters)
            {
                entry.generation = 0u;
            }
            m_TranslationOnlyCasterGeneration = 1u;
        }

        bool RegisterTranslationOnlyCaster(
            const MeshInstance* instance,
            const SceneGraphNode* node,
            bool deforming,
            const affine3& localToWorld,
            std::array<float, 3u>* registeredTranslation = nullptr)
        {
            if (!instance || !node || deforming ||
                m_TranslationOnlyCasterGeneration == 0u)
            {
                return false;
            }

            std::array<float, 12u> shaderTransform{};
            affineToColumnMajor(localToWorld, shaderTransform.data());
            std::array<float, 3u> translation{};
            if (!TryGetDiagnosticCsmTranslationOnlyTransform(
                    shaderTransform, translation))
            {
                return false;
            }

            uint32_t instanceIndex = 0u;
            if (!TryGetDiagnosticCsmTranslationRegistryIndex(
                    instance->GetInstanceIndex(),
                    m_TranslationOnlyCasters.size(),
                    instanceIndex))
            {
                return false;
            }
            TranslationOnlyCasterEntry& entry =
                m_TranslationOnlyCasters[instanceIndex];
            entry.translation = translation;
            entry.generation = m_TranslationOnlyCasterGeneration;
            if (registeredTranslation)
                *registeredTranslation = translation;
            return true;
        }

        void RegisterCachedTranslationOnlyCasters(
            const std::vector<CasterRecord>& casters)
        {
            if (m_TranslationOnlyCasterGeneration == 0u)
                return;

            for (const CasterRecord& caster : casters)
            {
                if (!caster.draw.instance)
                    continue;

                std::array<float, 3u> restoredTranslation{};
                if (!TryRestoreDiagnosticCsmCachedTranslationOnlyTransform(
                        caster.translationOnlyTransform,
                        caster.translationOnlyWorldTranslation,
                        restoredTranslation))
                {
                    continue;
                }

                uint32_t instanceIndex = 0u;
                if (!TryGetDiagnosticCsmTranslationRegistryIndex(
                        caster.draw.instance->GetInstanceIndex(),
                        m_TranslationOnlyCasters.size(),
                        instanceIndex))
                {
                    continue;
                }
                TranslationOnlyCasterEntry& entry =
                    m_TranslationOnlyCasters[instanceIndex];
                entry.translation = restoredTranslation;
                entry.generation = m_TranslationOnlyCasterGeneration;
            }
        }

        [[nodiscard]] bool IsCachedShadowDrawListEntryCompatible(
            const CachedShadowDrawListEntry& entry,
            const DiagnosticCascadedShadowMapSettings& settings,
            const IView& cameraView,
            const DirectionalLight* light,
            const std::shared_ptr<SceneGraphNode>& rootNode,
            uint64_t sceneStateRevision,
            const IDrawStrategy* drawStrategy) const
        {
            if (!entry.valid ||
                entry.rootNode.get() != rootNode.get() ||
                entry.light != light ||
                entry.drawStrategy != drawStrategy ||
                entry.sceneStateRevision != sceneStateRevision ||
                !IsSameDiagnosticCsmDrawListConfiguration(
                    entry.settings, settings))
            {
                return false;
            }

            const nvrhi::ViewportState viewportState =
                cameraView.GetViewportState();
            if (viewportState.viewports.size() != 1u ||
                entry.cameraViewport != viewportState.viewports[0] ||
                any(entry.cameraOrigin != cameraView.GetViewOrigin()) ||
                any(entry.cameraPixelOffset !=
                    cameraView.GetPixelOffset()) ||
                !HasIdenticalObjectRepresentation(
                    entry.cameraWorldToClip,
                    cameraView.GetViewProjectionMatrix(true)) ||
                !HasIdenticalObjectRepresentation(
                    entry.cameraWorldToClipNoOffset,
                    cameraView.GetViewProjectionMatrix(false)))
            {
                return false;
            }

            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                if (!HasIdenticalObjectRepresentation(
                        entry.worldToUvzw[cascade],
                        m_CurrentProjections[cascade].worldToUvzw))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] CachedShadowDrawListEntry*
            FindCachedShadowDrawListEntry(
                const DiagnosticCascadedShadowMapSettings& settings,
                const IView& cameraView,
                const DirectionalLight* light,
                const std::shared_ptr<SceneGraphNode>& rootNode,
                uint64_t sceneStateRevision,
                const IDrawStrategy* drawStrategy)
        {
            for (CachedShadowDrawListEntry& entry :
                m_CachedShadowDrawLists)
            {
                if (IsCachedShadowDrawListEntryCompatible(
                        entry,
                        settings,
                        cameraView,
                        light,
                        rootNode,
                        sceneStateRevision,
                        drawStrategy))
                {
                    entry.lastUse = ++m_CachedShadowDrawListUse;
                    return &entry;
                }
            }
            return nullptr;
        }

        void StoreCachedShadowDrawListEntry(
            const DiagnosticCascadedShadowMapSettings& settings,
            const IView& cameraView,
            const DirectionalLight* light,
            const std::shared_ptr<SceneGraphNode>& rootNode,
            uint64_t sceneStateRevision,
            const IDrawStrategy* drawStrategy,
            const std::array<CascadeWork,
                DiagnosticCsmMaximumCascades>& work)
        {
            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                if (work[cascade].action !=
                        DiagnosticCsmUpdateAction::FullRedraw ||
                    !work[cascade].currentCastersReady ||
                    !work[cascade].snapshotReliable)
                {
                    return;
                }
            }

            std::array<bool,
                DiagnosticCsmCachedShadowDrawListSlotCount> valid{};
            std::array<uint64_t,
                DiagnosticCsmCachedShadowDrawListSlotCount> lastUse{};
            for (uint32_t slot = 0u;
                slot < DiagnosticCsmCachedShadowDrawListSlotCount;
                ++slot)
            {
                valid[slot] = m_CachedShadowDrawLists[slot].valid;
                lastUse[slot] =
                    m_CachedShadowDrawLists[slot].lastUse;
            }
            CachedShadowDrawListEntry* destination =
                &m_CachedShadowDrawLists[
                    SelectDiagnosticCsmCachedShadowDrawListSlot(
                        valid, lastUse)];

            destination->valid = false;
            destination->rootNode = rootNode;
            destination->light = light;
            destination->drawStrategy = drawStrategy;
            destination->sceneStateRevision = sceneStateRevision;
            destination->lastUse = ++m_CachedShadowDrawListUse;
            destination->settings = settings;
            destination->cameraWorldToClip =
                cameraView.GetViewProjectionMatrix(true);
            destination->cameraWorldToClipNoOffset =
                cameraView.GetViewProjectionMatrix(false);
            destination->cameraOrigin = cameraView.GetViewOrigin();
            destination->cameraPixelOffset =
                cameraView.GetPixelOffset();
            destination->cameraViewport =
                cameraView.GetViewportState().viewports[0];
            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                destination->worldToUvzw[cascade] =
                    m_CurrentProjections[cascade].worldToUvzw;
                destination->casters[cascade] =
                    work[cascade].currentCasters;
            }
            for (uint32_t cascade = settings.cascadeCount;
                cascade < DiagnosticCsmMaximumCascades;
                ++cascade)
            {
                destination->casters[cascade].clear();
            }
            destination->valid = true;
        }

        void GatherCastersForCascades(
            const std::shared_ptr<SceneGraphNode>& rootNode,
            const float3& cameraOrigin,
            std::array<CascadeWork,
                DiagnosticCsmMaximumCascades>& work,
            std::array<bool,
                DiagnosticCsmMaximumCascades>& reliable,
            uint32_t cascadeMask,
            bool captureCacheMetadata,
            bool mergeOpaqueDepthState,
            bool translationOnlyCasterTransformEnabled,
            bool radiusThresholdEnabled,
            float radiusThreshold,
            bool accurateCasterCullingEnabled,
            bool precomputedReceiverHullAxesEnabled,
            bool sharedCasterLightProjectionEnabled)
        {
            std::array<frustum,
                DiagnosticCsmMaximumCascades> cascadeFrusta;
            std::array<const Material*,
                DiagnosticCsmMaximumCascades> canonicalOpaqueMaterials{};
            uint32_t accurateCullMask = 0u;
            reliable.fill(true);
            for (uint32_t cascade = 0u;
                cascade < DiagnosticCsmMaximumCascades;
                ++cascade)
            {
                if ((cascadeMask & (1u << cascade)) == 0u)
                    continue;
                work[cascade].currentCasters.clear();
                cascadeFrusta[cascade] =
                    m_CascadeViews[cascade]->GetViewFrustum();
                if (accurateCasterCullingEnabled &&
                    m_CurrentProjections[cascade].
                        casterCullVolume.valid)
                {
                    accurateCullMask |= 1u << cascade;
                }
            }

            const affine3* sharedWorldToLight = nullptr;
            std::array<float, 12u> sharedWorldToLightSignature{};
            uint32_t accurateCullCascadeCount = 0u;
            bool sharedCasterProjectionActive =
                sharedCasterLightProjectionEnabled;
            if (sharedCasterProjectionActive)
            {
                for (uint32_t cascade = 0u;
                    cascade < DiagnosticCsmMaximumCascades;
                    ++cascade)
                {
                    const uint32_t bit = 1u << cascade;
                    if ((accurateCullMask & bit) == 0u)
                        continue;

                    ++accurateCullCascadeCount;
                    const auto& worldToLight =
                        m_CurrentProjections[cascade].
                            casterCullVolume.worldToLight;
                    const auto signature =
                        MakeTransformSignature(worldToLight);
                    if (!sharedWorldToLight)
                    {
                        sharedWorldToLight = &worldToLight;
                        sharedWorldToLightSignature = signature;
                    }
                    else if (signature != sharedWorldToLightSignature)
                    {
                        sharedCasterProjectionActive = false;
                    }
                }
            }
            sharedCasterProjectionActive =
                sharedCasterProjectionActive &&
                accurateCullCascadeCount >= 2u &&
                sharedWorldToLight != nullptr;
            m_Stats.sharedCasterLightProjectionEnabled =
                sharedCasterProjectionActive;

            if (m_Stats.casterSceneTraversals !=
                std::numeric_limits<uint32_t>::max())
            {
                ++m_Stats.casterSceneTraversals;
            }

            const bool needsBounds = captureCacheMetadata ||
                radiusThresholdEnabled ||
                accurateCullMask != 0u;
            SceneGraphWalker walker(rootNode.get());
            std::vector<uint32_t>& cascadeMaskStack =
                m_CascadeMaskStackScratch;
            cascadeMaskStack.clear();
            cascadeMaskStack.reserve(32u);
            cascadeMaskStack.push_back(cascadeMask);
            while (walker)
            {
                const uint32_t parentCascadeMask =
                    cascadeMaskStack.back();
                const auto relevantContentFlags =
                    SceneContentFlags::OpaqueMeshes |
                    SceneContentFlags::AlphaTestedMeshes;
                const bool subgraphContentRelevant =
                    (walker->GetSubgraphContentFlags() &
                        relevantContentFlags) != 0;
                const bool nodeContentsRelevant =
                    (walker->GetLeafContentFlags() &
                        relevantContentFlags) != 0;

                uint32_t nodeCascadeMask = 0u;
                if (subgraphContentRelevant)
                {
                    const box3 nodeBounds =
                        walker->GetGlobalBoundingBox();
                    const bool nodeBoundsReliable =
                        IsFiniteBox(nodeBounds);
                    for (uint32_t cascade = 0u;
                        cascade < DiagnosticCsmMaximumCascades;
                        ++cascade)
                    {
                        const uint32_t bit = 1u << cascade;
                        if ((parentCascadeMask & bit) != 0u &&
                            ShouldRetainDiagnosticCsmCoarseBounds(
                                nodeBoundsReliable,
                                nodeBoundsReliable &&
                                    cascadeFrusta[cascade].
                                        intersectsWith(nodeBounds)))
                        {
                            nodeCascadeMask |= bit;
                        }
                    }
                }

                const bool nodeVisible = nodeCascadeMask != 0u;
                if (nodeVisible && nodeContentsRelevant)
                {
                    auto* meshInstance =
                        dynamic_cast<MeshInstance*>(
                            walker->GetLeaf().get());
                    if (meshInstance)
                    {
                        const MeshInfo* mesh =
                            meshInstance->GetMesh().get();
                        SceneGraphNode* instanceNode =
                            meshInstance->GetNode();
                        const bool deforming =
                            bool(mesh->skinPrototype) ||
                            mesh->isMorphTargetAnimationMesh ||
                            mesh->isSkinPrototype;
                        const bool needsGeometryBounds =
                            mesh->geometries.size() > 1u &&
                            !deforming;
                        const bool needsLocalToWorld =
                            needsGeometryBounds ||
                            (needsBounds && instanceNode &&
                                !deforming) ||
                            (translationOnlyCasterTransformEnabled &&
                                instanceNode && !deforming);
                        affine3 localToWorld = affine3::identity();
                        if (needsLocalToWorld)
                        {
                            localToWorld = instanceNode
                                ? instanceNode->
                                    GetLocalToWorldTransformFloat()
                                : walker->
                                    GetLocalToWorldTransformFloat();
                        }
                        bool translationOnlyTransform = false;
                        std::array<float, 3u>
                            translationOnlyWorldTranslation{};
                        if (translationOnlyCasterTransformEnabled &&
                            instanceNode && !deforming &&
                            needsLocalToWorld)
                        {
                            translationOnlyTransform =
                                RegisterTranslationOnlyCaster(
                                    meshInstance,
                                    instanceNode,
                                    false,
                                    localToWorld,
                                    &translationOnlyWorldTranslation);
                        }
                        const box3 deformingWorldBounds =
                            needsBounds && instanceNode && deforming
                            ? instanceNode->GetGlobalBoundingBox()
                            : box3::empty();
                        for (const auto& geometry : mesh->geometries)
                        {
                            if (!geometry ||
                                geometry->type !=
                                    MeshGeometryPrimitiveType::Triangles)
                            {
                                continue;
                            }
                            const Material* sourceMaterial =
                                geometry->material.get();
                            if (!sourceMaterial ||
                                (sourceMaterial->domain !=
                                        MaterialDomain::Opaque &&
                                    sourceMaterial->domain !=
                                        MaterialDomain::AlphaTested))
                            {
                                continue;
                            }

                            uint32_t geometryCascadeMask =
                                nodeCascadeMask;
                            box3 geometryWorldBounds = box3::empty();
                            if (needsGeometryBounds)
                            {
                                geometryWorldBounds =
                                    geometry->objectSpaceBounds *
                                    localToWorld;
                                const bool geometryBoundsReliable =
                                    IsFiniteBox(geometryWorldBounds);
                                geometryCascadeMask = 0u;
                                for (uint32_t cascade = 0u;
                                    cascade <
                                        DiagnosticCsmMaximumCascades;
                                    ++cascade)
                                {
                                    const uint32_t bit = 1u << cascade;
                                    if ((nodeCascadeMask & bit) != 0u &&
                                        ShouldRetainDiagnosticCsmCoarseBounds(
                                            geometryBoundsReliable,
                                            geometryBoundsReliable &&
                                                cascadeFrusta[cascade].
                                                    intersectsWith(
                                                        geometryWorldBounds)))
                                    {
                                        geometryCascadeMask |= bit;
                                    }
                                }
                            }
                            if (geometryCascadeMask == 0u)
                                continue;

                            CasterRecord record;
                            record.draw.instance = meshInstance;
                            record.draw.mesh = mesh;
                            record.draw.geometry = geometry.get();
                            record.draw.material = sourceMaterial;
                            record.draw.buffers = mesh->buffers.get();
                            record.draw.cullMode =
                                sourceMaterial->doubleSided
                                ? nvrhi::RasterCullMode::None
                                : nvrhi::RasterCullMode::Back;
                            record.draw.distanceToCamera = 0.f;
                            record.draw.userData = nullptr;
                            record.translationOnlyTransform =
                                translationOnlyTransform;
                            record.translationOnlyWorldTranslation =
                                translationOnlyWorldTranslation;
                            if (captureCacheMetadata)
                            {
                                record.key = {
                                    meshInstance,
                                    geometry.get()
                                };
                                record.material =
                                    GetMaterialSignature(
                                        sourceMaterial);
                            }
                            if (needsBounds)
                            {
                                if (instanceNode && !deforming)
                                {
                                    if (captureCacheMetadata)
                                    {
                                        record.localToWorld =
                                            MakeTransformSignature(
                                                localToWorld);
                                    }
                                    record.worldBounds =
                                        needsGeometryBounds
                                        ? geometryWorldBounds
                                        : geometry->
                                            objectSpaceBounds *
                                            localToWorld;
                                }
                                else if (instanceNode)
                                {
                                    record.worldBounds =
                                        deformingWorldBounds;
                                }
                                record.reliableBounds =
                                    instanceNode && !deforming &&
                                    IsFiniteBox(
                                        record.worldBounds);
                            }

                            bool radiusRejected = false;
                            if (radiusThresholdEnabled &&
                                record.reliableBounds)
                            {
                                const float3 casterHalfExtents =
                                    (record.worldBounds.m_maxs -
                                        record.worldBounds.m_mins) * 0.5f;
                                const float3 cameraOffset =
                                    record.worldBounds.center() -
                                        cameraOrigin;
                                radiusRejected =
                                    ShouldCullDiagnosticCsmCasterByRadiusSquared(
                                        dot(
                                            casterHalfExtents,
                                            casterHalfExtents),
                                        dot(cameraOffset, cameraOffset),
                                        radiusThreshold);
                            }
                            ProjectedCasterLightShape
                                sharedCasterLightShape;
                            const bool sharedCasterLightShapeReady =
                                ShouldBuildDiagnosticCsmSharedCasterLightShape(
                                    radiusRejected,
                                    sharedCasterProjectionActive,
                                    record.reliableBounds) &&
                                BuildProjectedCasterLightShape(
                                    record.worldBounds,
                                    *sharedWorldToLight,
                                    sharedCasterLightShape);
                            for (uint32_t cascade = 0u;
                                cascade <
                                    DiagnosticCsmMaximumCascades;
                                ++cascade)
                            {
                                const uint32_t bit = 1u << cascade;
                                if ((geometryCascadeMask & bit) == 0u)
                                    continue;

                                if (needsBounds)
                                {
                                    reliable[cascade] =
                                        reliable[cascade] &&
                                        record.reliableBounds;
                                }
                                if (m_Stats.
                                        coarseCasterProjectionPairs !=
                                    std::numeric_limits<
                                        uint32_t>::max())
                                {
                                    ++m_Stats.
                                        coarseCasterProjectionPairs;
                                }
                                if (radiusRejected)
                                {
                                    if (m_Stats.
                                            radiusCulledCasterProjectionPairs !=
                                        std::numeric_limits<
                                            uint32_t>::max())
                                    {
                                        ++m_Stats.
                                            radiusCulledCasterProjectionPairs;
                                    }
                                    continue;
                                }
                                bool overlapsReceiverHull = true;
                                if ((accurateCullMask & bit) != 0u &&
                                    record.reliableBounds)
                                {
                                    const auto& cullVolume =
                                        m_CurrentProjections[cascade].
                                            casterCullVolume;
                                    overlapsReceiverHull =
                                        sharedCasterLightShapeReady
                                        ? ProjectedCasterLightShapeOverlapsReceiverHull(
                                            sharedCasterLightShape,
                                            cullVolume,
                                            precomputedReceiverHullAxesEnabled)
                                        : ProjectedCasterOverlapsReceiverHull(
                                            record.worldBounds,
                                            cullVolume,
                                            precomputedReceiverHullAxesEnabled);
                                }
                                if (!overlapsReceiverHull)
                                {
                                    if (m_Stats.
                                            accuratelyCulledCasterProjectionPairs !=
                                        std::numeric_limits<
                                            uint32_t>::max())
                                    {
                                        ++m_Stats.
                                            accuratelyCulledCasterProjectionPairs;
                                    }
                                    continue;
                                }

                                CasterRecord accepted = record;
                                if (mergeOpaqueDepthState &&
                                    sourceMaterial->domain ==
                                        MaterialDomain::Opaque)
                                {
                                    const Material*& canonical =
                                        canonicalOpaqueMaterials[
                                            cascade];
                                    if (!canonical)
                                        canonical = sourceMaterial;
                                    accepted.draw.material = canonical;
                                }
                                work[cascade].
                                    currentCasters.push_back(
                                        std::move(accepted));
                            }
                        }
                    }
                }

                const int depthChange = walker.Next(nodeVisible);
                if (depthChange > 0)
                {
                    cascadeMaskStack.push_back(nodeCascadeMask);
                }
                else if (depthChange < 0)
                {
                    const size_t popCount = std::min(
                        size_t(-depthChange),
                        cascadeMaskStack.size() - 1u);
                    cascadeMaskStack.resize(
                        cascadeMaskStack.size() - popCount);
                }
            }

            for (uint32_t cascade = 0u;
                cascade < DiagnosticCsmMaximumCascades;
                ++cascade)
            {
                if ((cascadeMask & (1u << cascade)) == 0u)
                    continue;
                auto& casters = work[cascade].currentCasters;
                std::sort(
                    casters.begin(),
                    casters.end(),
                    [mergeOpaqueDepthState](
                        const CasterRecord& left,
                        const CasterRecord& right)
                    {
                        return DrawItemLess(
                            left, right, mergeOpaqueDepthState);
                    });
                if (m_Stats.casterSorts !=
                    std::numeric_limits<uint32_t>::max())
                {
                    ++m_Stats.casterSorts;
                }
            }
        }

        bool BuildDirtyRectangles(
            uint32_t cascade,
            const DiagnosticCascadedShadowMapSettings& settings,
            const std::vector<CasterRecord>& current,
            bool currentReliable,
            std::vector<DiagnosticCsmRect>& rectangles,
            bool& changed)
        {
            rectangles.clear();
            changed = false;
            if (!m_CasterSnapshotValid[cascade] || !currentReliable)
                return false;

            const auto& previous = m_CachedCasters[cascade];
            std::unordered_map<CasterKey, size_t, CasterKeyHash>
                previousByKey;
            previousByKey.reserve(previous.size());
            for (size_t index = 0u; index < previous.size(); ++index)
            {
                if (!previous[index].reliableBounds)
                    return false;
                previousByKey.emplace(previous[index].key, index);
            }

            std::vector<bool> previousSeen(previous.size(), false);
            const uint32_t halo = uint32_t(
                std::ceil(EffectiveFilterRadiusTexels(settings)) + 2.f);
            const float4x4& mapping =
                m_CurrentProjections[cascade].worldToUvzw;

            for (const CasterRecord& record : current)
            {
                auto previousIt = previousByKey.find(record.key);
                const CasterRecord* oldRecord = previousIt ==
                    previousByKey.end()
                    ? nullptr
                    : &previous[previousIt->second];
                if (oldRecord)
                    previousSeen[previousIt->second] = true;
                const bool recordChanged = !oldRecord ||
                    oldRecord->localToWorld != record.localToWorld ||
                    !EqualBox(oldRecord->worldBounds, record.worldBounds) ||
                    !(oldRecord->material == record.material);
                if (!recordChanged)
                    continue;

                changed = true;
                if (oldRecord)
                {
                    DiagnosticCsmRect oldRectangle;
                    if (!ProjectBounds(
                                oldRecord->worldBounds,
                                mapping,
                                settings.shadowMapResolution,
                                halo,
                                oldRectangle) ||
                        !AppendMergedRect(rectangles, oldRectangle))
                    {
                        return false;
                    }
                }
                DiagnosticCsmRect currentRectangle;
                if (!ProjectBounds(
                            record.worldBounds,
                            mapping,
                            settings.shadowMapResolution,
                            halo,
                            currentRectangle) ||
                    !AppendMergedRect(rectangles, currentRectangle))
                {
                    return false;
                }
            }

            for (size_t index = 0u; index < previous.size(); ++index)
            {
                if (previousSeen[index])
                    continue;
                changed = true;
                DiagnosticCsmRect previousRectangle;
                if (!ProjectBounds(
                            previous[index].worldBounds,
                            mapping,
                            settings.shadowMapResolution,
                            halo,
                            previousRectangle) ||
                    !AppendMergedRect(rectangles, previousRectangle))
                {
                    return false;
                }
            }
            return true;
        }

        void AddInvalidationForProjectionMismatch(
            const DiagnosticCsmProjectionCompatibility& previous,
            const DiagnosticCsmProjectionCompatibility& current)
        {
            m_Stats.invalidationMask |=
                GetDiagnosticCsmProjectionInvalidationFlags(
                    previous, current);
        }

        void PrepareWork(
            const DiagnosticCascadedShadowMapSettings& settings,
            const IView& cameraView,
            const DirectionalLight* light,
            const std::shared_ptr<SceneGraphNode>& rootNode,
            uint64_t sceneStateRevision,
            bool sceneStateRevisionReliable,
            bool requiresFullSceneInvalidation,
            InstancedOpaqueDrawStrategy& drawStrategy,
            std::array<CascadeWork,
                DiagnosticCsmMaximumCascades>& work)
        {
            BeginTranslationOnlyCasterGeneration(
                settings.translationOnlyCasterTransformEnabled,
                rootNode);
            const bool cachedShadowDrawListsBaseEligible =
                IsDiagnosticCsmCachedShadowDrawListEligible(
                    settings,
                    sceneStateRevisionReliable);
            if (ShouldReleaseDiagnosticCsmCachedShadowDrawLists(
                    m_CachedShadowDrawListsWereEligible,
                    cachedShadowDrawListsBaseEligible))
            {
                InvalidateCachedShadowDrawLists();
            }
            if (ShouldInvalidateDiagnosticCsmCachedShadowDrawLists(
                    settings.cachedShadowDrawListsEnabled,
                    requiresFullSceneInvalidation,
                    sceneStateRevisionReliable))
            {
                InvalidateCachedShadowDrawLists();
            }
            m_CachedShadowDrawListsWereEligible =
                cachedShadowDrawListsBaseEligible;
            const bool cachedShadowDrawListsEligible =
                cachedShadowDrawListsBaseEligible &&
                !requiresFullSceneInvalidation;
            const bool cachePolicy =
                HasAnyDiagnosticCsmCachePolicy(settings);
            const bool captureCacheMetadata =
                settings.dirtyRectanglesEnabled &&
                    settings.wholeCascadeReuseEnabled ||
                cachedShadowDrawListsEligible;
            const bool viewDependentCullingActive =
                CanUseDiagnosticCsmViewDependentCasterCulling(settings);
            bool accurateCasterCullingActive =
                settings.accurateCasterCullingEnabled &&
                viewDependentCullingActive;
            if (accurateCasterCullingActive)
            {
                accurateCasterCullingActive = false;
                for (uint32_t cascade = 0u;
                    cascade < settings.cascadeCount;
                    ++cascade)
                {
                    if (m_CurrentProjections[cascade].
                        casterCullVolume.valid)
                    {
                        accurateCasterCullingActive = true;
                        break;
                    }
                }
            }
            const bool radiusThresholdActive =
                IsDiagnosticCsmCasterRadiusThresholdActive(settings);
            const bool sceneCompatible =
                sceneStateRevisionReliable &&
                m_PreviousSceneRevisionValid &&
                m_PreviousRoot == rootNode.get();
            const bool sceneChanged =
                IsDiagnosticCsmSceneStateChanged(
                    requiresFullSceneInvalidation,
                    sceneCompatible,
                    sceneStateRevision,
                    m_PreviousSceneRevision);
            const bool forceFullSceneInvalidation =
                sceneChanged && requiresFullSceneInvalidation;
            if (ShouldResetDiagnosticCsmDepthBindings(
                    sceneChanged,
                    requiresFullSceneInvalidation) &&
                m_DepthPass)
            {
                m_DepthPass->ResetBindingCache();
                if (m_InputAssemblerDepthPass)
                    m_InputAssemblerDepthPass->ResetBindingCache();
            }

            std::array<DiagnosticCsmScrollClassification,
                DiagnosticCsmMaximumCascades> classifications{};
            bool allExact = cachePolicy && m_CacheValid &&
                m_PreviousCascadeCount == settings.cascadeCount;
            if (!cachePolicy)
            {
                for (uint32_t cascade = 0u;
                    cascade < settings.cascadeCount;
                    ++cascade)
                {
                    work[cascade].action =
                        DiagnosticCsmUpdateAction::FullRedraw;
                }
            }
            else
            {
                for (uint32_t cascade = 0u;
                    cascade < settings.cascadeCount;
                    ++cascade)
                {
                    if (!m_CacheValid ||
                        cascade >= m_PreviousCascadeCount ||
                        !m_PreviousProjectionValid[cascade])
                    {
                        allExact = false;
                        work[cascade].action =
                            DiagnosticCsmUpdateAction::FullRedraw;
                        m_Stats.invalidationMask |=
                            DiagnosticCsmInvalidation_FirstFrame;
                        continue;
                    }
                    classifications[cascade] =
                        ClassifyDiagnosticCsmProjectionChange(
                            m_PreviousCompatibility[cascade],
                            m_CurrentProjections[cascade].compatibility,
                            settings.minimumScrollOverlap);
                    allExact = allExact &&
                        classifications[cascade].exactReuse;
                }

                if (settings.wholeMapReuseEnabled &&
                    !settings.wholeCascadeReuseEnabled)
                {
                    const bool reuseMap = allExact && !sceneChanged;
                    if (!reuseMap && !allExact)
                    {
                        for (uint32_t cascade = 0u;
                            cascade < settings.cascadeCount;
                            ++cascade)
                        {
                            if (cascade < m_PreviousCascadeCount &&
                                m_PreviousProjectionValid[cascade] &&
                                !classifications[cascade].exactReuse)
                            {
                                AddInvalidationForProjectionMismatch(
                                    m_PreviousCompatibility[cascade],
                                    m_CurrentProjections[cascade].
                                        compatibility);
                            }
                        }
                    }
                    for (uint32_t cascade = 0u;
                        cascade < settings.cascadeCount;
                        ++cascade)
                    {
                        work[cascade].action = reuseMap
                            ? DiagnosticCsmUpdateAction::Reused
                            : DiagnosticCsmUpdateAction::FullRedraw;
                    }
                    if (!reuseMap && sceneChanged)
                    {
                        m_Stats.invalidationMask |=
                            DiagnosticCsmInvalidation_Scene;
                    }
                }
                else
                {
                    for (uint32_t cascade = 0u;
                        cascade < settings.cascadeCount;
                        ++cascade)
                    {
                        if (!m_CacheValid ||
                            cascade >= m_PreviousCascadeCount ||
                            !m_PreviousProjectionValid[cascade])
                        {
                            continue;
                        }
                        const auto& classification =
                            classifications[cascade];
                        if (classification.exactReuse)
                        {
                            if (!sceneChanged)
                            {
                                work[cascade].action =
                                    DiagnosticCsmUpdateAction::Reused;
                            }
                            else if (
                                CanUseDiagnosticCsmPartialSceneUpdate(
                                    sceneChanged,
                                    sceneCompatible,
                                    settings.dirtyRectanglesEnabled,
                                    forceFullSceneInvalidation))
                            {
                                work[cascade].action =
                                    DiagnosticCsmUpdateAction::DirtyRectangles;
                            }
                            else
                            {
                                work[cascade].action =
                                    DiagnosticCsmUpdateAction::FullRedraw;
                                m_Stats.invalidationMask |=
                                    DiagnosticCsmInvalidation_Scene;
                            }
                        }
                        else if (settings.scrollingEnabled &&
                            classification.scrollCompatible &&
                            CanUseDiagnosticCsmPartialSceneUpdate(
                                sceneChanged,
                                sceneCompatible,
                                settings.dirtyRectanglesEnabled,
                                forceFullSceneInvalidation))
                        {
                            work[cascade].action =
                                DiagnosticCsmUpdateAction::Scrolled;
                            work[cascade].scroll =
                                ComputeDiagnosticCsmScrollRegions(
                                    settings.shadowMapResolution,
                                    classification.destinationShiftX,
                                    classification.destinationShiftY);
                            for (uint32_t rect = 0u;
                                rect < work[cascade].scroll.exposedCount;
                                ++rect)
                            {
                                AppendMergedRect(
                                    work[cascade].dirtyRectangles,
                                    work[cascade].scroll.exposed[rect]);
                            }
                        }
                        else
                        {
                            work[cascade].action =
                                DiagnosticCsmUpdateAction::FullRedraw;
                            AddInvalidationForProjectionMismatch(
                                m_PreviousCompatibility[cascade],
                                m_CurrentProjections[cascade].compatibility);
                            if (settings.scrollingEnabled)
                            {
                                m_Stats.invalidationMask |=
                                    DiagnosticCsmInvalidation_ScrollIncompatible;
                            }
                        }
                    }
                }
            }

            CachedShadowDrawListEntry* cachedShadowDrawLists =
                cachedShadowDrawListsEligible
                ? FindCachedShadowDrawListEntry(
                    settings,
                    cameraView,
                    light,
                    rootNode,
                    sceneStateRevision,
                    &drawStrategy)
                : nullptr;
            m_Stats.cachedShadowDrawListsRequested =
                settings.cachedShadowDrawListsEnabled;
            m_Stats.cachedShadowDrawListsActive =
                cachedShadowDrawLists != nullptr;
            if (cachedShadowDrawLists)
            {
                ++m_Stats.cachedShadowDrawListHits;
                for (uint32_t cascade = 0u;
                    cascade < settings.cascadeCount;
                    ++cascade)
                {
                    CascadeWork& cascadeWork = work[cascade];
                    cascadeWork.cachedShadowDrawList =
                        &cachedShadowDrawLists->casters[cascade];
                    cascadeWork.currentCastersReady = true;
                    cascadeWork.snapshotReliable = true;
                    RegisterCachedTranslationOnlyCasters(
                        *cascadeWork.cachedShadowDrawList);
                }
            }
            else if (cachedShadowDrawListsEligible)
            {
                ++m_Stats.cachedShadowDrawListMisses;
            }

            DiagnosticCsmCasterGatherPlan gatherPlan;
            if (!cachedShadowDrawLists &&
                settings.
                singleTraversalCasterClassificationEnabled)
            {
                std::array<DiagnosticCsmUpdateAction,
                    DiagnosticCsmMaximumCascades> actions{};
                for (uint32_t cascade = 0u;
                    cascade < settings.cascadeCount;
                    ++cascade)
                {
                    actions[cascade] = work[cascade].action;
                }
                gatherPlan = BuildDiagnosticCsmCasterGatherPlan(
                    true, settings.cascadeCount, actions);
            }
            m_Stats.singleTraversalCasterClassificationRequested =
                settings.singleTraversalCasterClassificationEnabled;
            m_Stats.singleTraversalCasterClassificationEnabled =
                gatherPlan.singleTraversal;
            std::array<bool,
                DiagnosticCsmMaximumCascades> sharedGatherReliable{};
            if (gatherPlan.singleTraversal)
            {
                GatherCastersForCascades(
                    rootNode,
                    cameraView.GetViewOrigin(),
                    work,
                    sharedGatherReliable,
                    gatherPlan.cascadeMask,
                    captureCacheMetadata,
                    settings.opaqueDepthStateMergingEnabled,
                    settings.translationOnlyCasterTransformEnabled,
                    radiusThresholdActive,
                    settings.casterRadiusThreshold,
                    accurateCasterCullingActive,
                    settings.precomputedReceiverHullAxesEnabled,
                    settings.sharedCasterLightProjectionEnabled);
            }

            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                CascadeWork& cascadeWork = work[cascade];
                if (cascadeWork.action ==
                    DiagnosticCsmUpdateAction::Reused)
                {
                    continue;
                }
                if (cascadeWork.cachedShadowDrawList)
                {
                    SaturatingAddUint32(
                        m_Stats.candidateCasterProjectionPairs,
                        cascadeWork.cachedShadowDrawList->size());
                    continue;
                }

                bool reliable = false;
                if (gatherPlan.singleTraversal)
                {
                    reliable = sharedGatherReliable[cascade];
                }
                else
                {
                    std::array<bool,
                        DiagnosticCsmMaximumCascades>
                        perCascadeReliable{};
                    GatherCastersForCascades(
                        rootNode,
                        cameraView.GetViewOrigin(),
                        work,
                        perCascadeReliable,
                        1u << cascade,
                        captureCacheMetadata,
                        settings.opaqueDepthStateMergingEnabled,
                        settings.translationOnlyCasterTransformEnabled,
                        radiusThresholdActive,
                        settings.casterRadiusThreshold,
                        accurateCasterCullingActive,
                        settings.precomputedReceiverHullAxesEnabled,
                        settings.sharedCasterLightProjectionEnabled);
                    reliable = perCascadeReliable[cascade];
                }
                cascadeWork.currentCastersReady = true;
                cascadeWork.snapshotReliable = reliable;
                SaturatingAddUint32(
                    m_Stats.candidateCasterProjectionPairs,
                    cascadeWork.currentCasters.size());

                if (cascadeWork.action ==
                        DiagnosticCsmUpdateAction::DirtyRectangles ||
                    (cascadeWork.action ==
                        DiagnosticCsmUpdateAction::Scrolled && sceneChanged))
                {
                    std::vector<DiagnosticCsmRect> changedRectangles;
                    bool changed = false;
                    const bool dirtyValid = BuildDirtyRectangles(
                        cascade,
                        settings,
                        cascadeWork.currentCasters,
                        reliable,
                        changedRectangles,
                        changed);
                    if (!dirtyValid)
                    {
                        cascadeWork.action =
                            DiagnosticCsmUpdateAction::FullRedraw;
                        cascadeWork.dirtyRectangles.clear();
                        m_Stats.invalidationMask |= reliable
                            ? DiagnosticCsmInvalidation_DirtyOverflow
                            : DiagnosticCsmInvalidation_UnreliableBounds;
                    }
                    else
                    {
                        bool merged = true;
                        for (const DiagnosticCsmRect& rectangle :
                            changedRectangles)
                        {
                            merged = merged && AppendMergedRect(
                                cascadeWork.dirtyRectangles, rectangle);
                        }
                        if (!merged)
                        {
                            cascadeWork.action =
                                DiagnosticCsmUpdateAction::FullRedraw;
                            cascadeWork.dirtyRectangles.clear();
                            m_Stats.invalidationMask |=
                                DiagnosticCsmInvalidation_DirtyOverflow;
                        }
                        else if (sceneChanged)
                        {
                            // Generic content and material changes were
                            // rejected before this local comparison. A
                            // transform-only revision whose surviving clipped
                            // rectangles do not touch this cascade can retain
                            // it exactly. A scrolled cascade still keeps its
                            // exposed strips in the rectangle list.
                            cascadeWork.action =
                                FinalizeDiagnosticCsmLocalizedSceneAction(
                                    cascadeWork.action,
                                    sceneChanged,
                                    !cascadeWork.dirtyRectangles.empty());
                            if (cascadeWork.action ==
                                DiagnosticCsmUpdateAction::Reused)
                            {
                                cascadeWork.dirtyRectangles.clear();
                            }
                        }
                    }
                }

                if (cascadeWork.action ==
                        DiagnosticCsmUpdateAction::DirtyRectangles ||
                    cascadeWork.action ==
                        DiagnosticCsmUpdateAction::Scrolled)
                {
                    cascadeWork.projectedCasterRectangles.resize(
                        cascadeWork.currentCasters.size());
                    cascadeWork.projectedCasterBoundsReliable.resize(
                        cascadeWork.currentCasters.size());
                    for (size_t casterIndex = 0u;
                        casterIndex < cascadeWork.currentCasters.size();
                        ++casterIndex)
                    {
                        const CasterRecord& caster =
                            cascadeWork.currentCasters[casterIndex];
                        DiagnosticCsmRect& casterRectangle =
                            cascadeWork.
                                projectedCasterRectangles[casterIndex];
                        const bool projectionReliable = ProjectBounds(
                            caster.worldBounds,
                            m_CurrentProjections[cascade].worldToUvzw,
                            settings.shadowMapResolution,
                            0u,
                            casterRectangle);
                        cascadeWork.projectedCasterBoundsReliable[
                            casterIndex] = uint8_t(
                                caster.reliableBounds &&
                                projectionReliable);
                    }
                    cascadeWork.dirtyCasterIndices.resize(
                        cascadeWork.dirtyRectangles.size());
                    for (size_t rectangleIndex = 0u;
                        rectangleIndex <
                            cascadeWork.dirtyRectangles.size();
                        ++rectangleIndex)
                    {
                        const DiagnosticCsmRect& rectangle =
                            cascadeWork.dirtyRectangles[rectangleIndex];
                        std::vector<size_t>& overlapping =
                            cascadeWork.dirtyCasterIndices[rectangleIndex];
                        overlapping.reserve(
                            cascadeWork.currentCasters.size());
                        for (size_t casterIndex = 0u;
                            casterIndex <
                                cascadeWork.currentCasters.size();
                            ++casterIndex)
                        {
                            if (ShouldRenderDiagnosticCsmCasterForUpdateRect(
                                    cascadeWork.
                                        projectedCasterBoundsReliable[
                                            casterIndex] != 0u,
                                    cascadeWork.
                                        projectedCasterRectangles[
                                            casterIndex],
                                    rectangle))
                            {
                                overlapping.push_back(casterIndex);
                            }
                        }
                    }
                }
            }

            if (cachedShadowDrawListsEligible &&
                !cachedShadowDrawLists)
            {
                StoreCachedShadowDrawListEntry(
                    settings,
                    cameraView,
                    light,
                    rootNode,
                    sceneStateRevision,
                    &drawStrategy,
                    work);
            }
            if (settings.cachedShadowDrawListsEnabled)
            {
                for (const CachedShadowDrawListEntry& entry :
                    m_CachedShadowDrawLists)
                {
                    if (!entry.valid)
                        continue;
                    SaturatingAddUint32(
                        m_Stats.cachedShadowDrawListEntries, 1u);
                    for (uint32_t cascade = 0u;
                        cascade < entry.settings.cascadeCount &&
                            cascade < DiagnosticCsmMaximumCascades;
                        ++cascade)
                    {
                        SaturatingAddUint32(
                            m_Stats.
                                cachedShadowDrawListCasterProjectionPairs,
                            entry.casters[cascade].size());
                    }
                }
            }
        }

        void ClearRectangle(
            nvrhi::ICommandList* commandList,
            uint32_t cascade,
            const DiagnosticCsmRect& rectangle)
        {
            if (!rectangle.IsValid())
                return;
            ScissoredPlanarView& view = *m_CascadeViews[cascade];
            view.SetScissorRect(nvrhi::Rect(
                rectangle.minX,
                rectangle.maxX,
                rectangle.minY,
                rectangle.maxY));
            nvrhi::GraphicsState state;
            state.pipeline = m_ClearPipeline;
            state.framebuffer = m_FramebufferFactory->GetFramebuffer(view);
            state.viewport = view.GetViewportState();
            commandList->setGraphicsState(state);
            nvrhi::DrawArguments draw;
            draw.vertexCount = 3u;
            draw.instanceCount = 1u;
            commandList->draw(draw);
        }

        void BlitDepthRectangle(
            nvrhi::ICommandList* commandList,
            nvrhi::IBindingSet* sourceBindingSet,
            FramebufferFactory& targetFramebufferFactory,
            ScissoredPlanarView& targetView,
            const DiagnosticCsmRect& destination,
            const int2& sourceOffset,
            uint32_t sourceArraySlice)
        {
            if (!destination.IsValid() || !sourceBindingSet)
                return;

            DiagnosticCsmScrollConstants constants{};
            constants.sourceOffset = sourceOffset;
            constants.sourceArraySlice = sourceArraySlice;
            commandList->writeBuffer(
                m_ScrollConstants, &constants, sizeof(constants));

            targetView.SetScissorRect(nvrhi::Rect(
                destination.minX,
                destination.maxX,
                destination.minY,
                destination.maxY));
            nvrhi::GraphicsState state;
            state.pipeline = m_ScrollPipeline;
            state.framebuffer =
                targetFramebufferFactory.GetFramebuffer(targetView);
            state.viewport = targetView.GetViewportState();
            state.bindings = { sourceBindingSet };
            commandList->setGraphicsState(state);
            nvrhi::DrawArguments draw;
            draw.vertexCount = 3u;
            draw.instanceCount = 1u;
            commandList->draw(draw);
        }

        void ExecuteClearAndScrolls(
            nvrhi::ICommandList* commandList,
            const DiagnosticCascadedShadowMapSettings& settings,
            const std::array<CascadeWork,
                DiagnosticCsmMaximumCascades>& work)
        {
            if (settings.batchedFullRedrawClearEnabled)
            {
                std::array<DiagnosticCsmUpdateAction,
                    DiagnosticCsmMaximumCascades> actions{};
                for (uint32_t cascade = 0u;
                    cascade < settings.cascadeCount;
                    ++cascade)
                {
                    actions[cascade] = work[cascade].action;
                }

                const DiagnosticCsmFullRedrawClearPlan clearPlan =
                    BuildDiagnosticCsmFullRedrawClearPlan(
                        settings, actions);
                if (clearPlan.batched)
                {
                    commandList->clearDepthStencilTexture(
                        m_Depth,
                        nvrhi::TextureSubresourceSet(
                            0u,
                            1u,
                            clearPlan.baseArraySlice,
                            clearPlan.arraySliceCount),
                        true,
                        1.f,
                        false,
                        0u);
                    const uint64_t texels =
                        uint64_t(settings.shadowMapResolution) *
                        settings.shadowMapResolution *
                        clearPlan.arraySliceCount;
                    m_Stats.clearedTexels += texels;
                    m_Stats.updatedTexels += texels;
                    m_Stats.batchedFullRedrawClearActive = true;
                    return;
                }
            }

            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                const CascadeWork& cascadeWork = work[cascade];
                if (cascadeWork.action ==
                    DiagnosticCsmUpdateAction::FullRedraw)
                {
                    commandList->clearDepthStencilTexture(
                        m_Depth,
                        nvrhi::TextureSubresourceSet(0u, 1u, cascade, 1u),
                        true,
                        1.f,
                        false,
                        0u);
                    const uint64_t texels =
                        uint64_t(settings.shadowMapResolution) *
                        settings.shadowMapResolution;
                    m_Stats.clearedTexels += texels;
                    m_Stats.updatedTexels += texels;
                    continue;
                }

                if (cascadeWork.action ==
                        DiagnosticCsmUpdateAction::Scrolled &&
                    cascadeWork.scroll.valid && m_ScrollScratch)
                {
                    const DiagnosticCsmRect& source =
                        cascadeWork.scroll.source;
                    const DiagnosticCsmRect& destination =
                        cascadeWork.scroll.destination;
                    const std::array<int32_t, 2u> sourceOffset =
                        ComputeDiagnosticCsmScrollSourceOffset(
                            cascadeWork.scroll);
                    BlitDepthRectangle(
                        commandList,
                        m_DepthScrollBindingSet,
                        *m_ScrollFramebufferFactory,
                        *m_ScrollScratchView,
                        source,
                        int2(0, 0),
                        cascade);
                    BlitDepthRectangle(
                        commandList,
                        m_ScratchScrollBindingSet,
                        *m_FramebufferFactory,
                        *m_CascadeViews[cascade],
                        destination,
                        int2(
                            sourceOffset[0], sourceOffset[1]),
                        0u);
                    m_Stats.copiedTexels +=
                        cascadeWork.scroll.copiedTexels;
                }

                for (const DiagnosticCsmRect& rectangle :
                    cascadeWork.dirtyRectangles)
                {
                    ClearRectangle(commandList, cascade, rectangle);
                    m_Stats.clearedTexels += rectangle.Area();
                    m_Stats.updatedTexels += rectangle.Area();
                }
            }
        }

        void RenderCasterList(
            nvrhi::ICommandList* commandList,
            uint32_t cascade,
            const DiagnosticCascadedShadowMapSettings& settings,
            const std::vector<CasterRecord>& casters,
            const DiagnosticCsmRect* scissor,
            const std::vector<size_t>* casterIndices,
            bool collectSubmissionStats)
        {
            if (casters.empty() ||
                (casterIndices && casterIndices->empty()))
                return;

            ScissoredPlanarView& view = *m_CascadeViews[cascade];
            if (scissor)
            {
                view.SetScissorRect(nvrhi::Rect(
                    scissor->minX,
                    scissor->maxX,
                    scissor->minY,
                    scissor->maxY));
            }
            else
            {
                view.SetScissorRect(nvrhi::Rect(
                    int(m_DepthResolution), int(m_DepthResolution)));
            }

            auto RenderSubset =
                [&](const std::vector<size_t>* indices,
                    DiagnosticCsmDepthPass& depthPass,
                    bool translationOnlyCasterTransformEnabled)
            {
                if (indices && indices->empty())
                    return;

                CasterRecordDrawStrategy directDrawStrategy;
                InstrumentedCasterRecordDrawStrategy
                    instrumentedDirectDrawStrategy;
                PassthroughDrawStrategy copiedDrawStrategy;
                IDrawStrategy* drawStrategy = nullptr;
                if (settings.directCasterSubmissionEnabled)
                {
                    size_t probeReadIndex = 0u;
                    size_t probeCasterIndex = 0u;
                    if (!NextDiagnosticCsmCasterSubmissionIndex(
                            casters.size(),
                            indices ? indices->data() : nullptr,
                            indices ? indices->size() : 0u,
                            indices != nullptr,
                            probeReadIndex,
                            probeCasterIndex))
                    {
                        return;
                    }
                    if (collectSubmissionStats)
                    {
                        instrumentedDirectDrawStrategy.SetData(
                            casters, indices, m_Stats);
                        drawStrategy =
                            &instrumentedDirectDrawStrategy;
                    }
                    else
                    {
                        directDrawStrategy.SetData(casters, indices);
                        drawStrategy = &directDrawStrategy;
                    }
                    m_Stats.directCasterSubmissionEnabled = true;
                }
                else
                {
                    std::vector<DrawItem>& drawItems =
                        m_DrawItemsScratch;
                    drawItems.clear();
                    drawItems.reserve(indices
                        ? indices->size()
                        : casters.size());
                    CasterSubmissionStatsAccumulator submissionStats;
                    submissionStats.Reset(collectSubmissionStats
                        ? &m_Stats
                        : nullptr);
                    auto AppendCopiedDrawItem =
                        [&](const DrawItem& item)
                    {
                        drawItems.push_back(item);
                        if (collectSubmissionStats)
                            submissionStats.Record(drawItems.back());
                    };
                    if (indices)
                    {
                        for (size_t casterIndex : *indices)
                        {
                            if (casterIndex < casters.size())
                            {
                                AppendCopiedDrawItem(
                                    casters[casterIndex].draw);
                            }
                        }
                    }
                    else
                    {
                        for (const CasterRecord& caster : casters)
                            AppendCopiedDrawItem(caster.draw);
                    }
                    if (drawItems.empty())
                        return;
                    submissionStats.Finish();
                    copiedDrawStrategy.SetData(
                        drawItems.data(), drawItems.size());
                    drawStrategy = &copiedDrawStrategy;
                }
                if (!drawStrategy)
                    return;

                DiagnosticCsmDepthPass::Context context;
                const auto& compatibility =
                    m_CurrentProjections[cascade].compatibility;
                context.constantDepthBias = ComputeUeCsmShaderDepthBias(
                    compatibility.depthBias,
                    compatibility.depthFar - compatibility.depthNear,
                    compatibility.radius,
                    settings.shadowMapResolution);
                context.slopeDepthBias = context.constantDepthBias *
                    std::max(compatibility.slopeScaledDepthBias, 0.f);
                context.maximumSlopeDepthBias = 1.f;
                context.inverseDepthAxisLength =
                    m_CurrentProjections[cascade].
                        inverseDepthAxisLength;
                if (translationOnlyCasterTransformEnabled)
                {
                    context.translationOnlyCasters =
                        &m_TranslationOnlyCasters;
                    context.translationOnlyCasterGeneration =
                        m_TranslationOnlyCasterGeneration;
                    context.submissionStats = collectSubmissionStats
                        ? &m_Stats
                        : nullptr;
                }
                RenderView(
                    commandList,
                    &view,
                    &view,
                    m_FramebufferFactory->GetFramebuffer(view),
                    *drawStrategy,
                    depthPass,
                    context,
                    false);
            };

            const bool inputAssemblerActive =
                settings.inputAssemblerCasterFetchEnabled &&
                m_AppliedInputAssemblerCasterFetchEnabled &&
                bool(m_InputAssemblerDepthPass);
            if (!inputAssemblerActive)
            {
                RenderSubset(
                    casterIndices,
                    *m_DepthPass,
                    settings.translationOnlyCasterTransformEnabled);
                return;
            }

            std::vector<size_t>& manualIndices =
                m_ManualCasterIndicesScratch;
            std::vector<size_t>& inputAssemblerIndices =
                m_InputAssemblerCasterIndicesScratch;
            manualIndices.clear();
            inputAssemblerIndices.clear();
            const size_t sourceCount = casterIndices
                ? casterIndices->size()
                : casters.size();
            manualIndices.reserve(sourceCount);
            inputAssemblerIndices.reserve(sourceCount);

            auto ClassifyCaster = [&](size_t casterIndex)
            {
                if (casterIndex >= casters.size())
                    return;

                const CasterRecord& caster = casters[casterIndex];
                const DrawItem& draw = caster.draw;
                bool translationOnly = false;
                if (settings.translationOnlyCasterTransformEnabled &&
                    draw.instance &&
                    m_TranslationOnlyCasterGeneration != 0u)
                {
                    uint32_t instanceIndex = 0u;
                    translationOnly =
                        TryGetDiagnosticCsmTranslationRegistryIndex(
                            draw.instance->GetInstanceIndex(),
                            m_TranslationOnlyCasters.size(),
                            instanceIndex) &&
                        m_TranslationOnlyCasters[instanceIndex].
                            generation ==
                            m_TranslationOnlyCasterGeneration;
                }

                const bool deforming = draw.mesh &&
                    (bool(draw.mesh->skinPrototype) ||
                        draw.mesh->isMorphTargetAnimationMesh ||
                        draw.mesh->isSkinPrototype);
                const BufferGroup* buffers = draw.buffers;
                const bool hasInputAssemblerVertexBuffer =
                    buffers &&
                    buffers->vertexBuffer &&
                    buffers->vertexBuffer->getDesc().isVertexBuffer;
                const bool hasInputAssemblerInstanceBuffer =
                    buffers &&
                    buffers->instanceBuffer &&
                    buffers->instanceBuffer->getDesc().isVertexBuffer;
                const bool useInputAssembler =
                    ShouldUseDiagnosticCsmInputAssemblerCasterFetch(
                        true,
                        translationOnly,
                        deforming,
                        hasInputAssemblerVertexBuffer &&
                            buffers->hasAttribute(
                                VertexAttribute::Position),
                        hasInputAssemblerVertexBuffer &&
                            buffers->hasAttribute(
                                VertexAttribute::TexCoord1),
                        hasInputAssemblerVertexBuffer &&
                            buffers->hasAttribute(
                                VertexAttribute::Normal),
                        hasInputAssemblerInstanceBuffer);
                (useInputAssembler
                    ? inputAssemblerIndices
                    : manualIndices).push_back(casterIndex);
            };

            if (casterIndices)
            {
                for (size_t casterIndex : *casterIndices)
                    ClassifyCaster(casterIndex);
            }
            else
            {
                for (size_t casterIndex = 0u;
                    casterIndex < casters.size();
                    ++casterIndex)
                {
                    ClassifyCaster(casterIndex);
                }
            }

            auto AddRoutedPairs =
                [](uint32_t& destination, size_t count)
            {
                const uint32_t add = uint32_t(std::min(
                    count,
                    size_t(std::numeric_limits<uint32_t>::max())));
                destination = add >
                        std::numeric_limits<uint32_t>::max() - destination
                    ? std::numeric_limits<uint32_t>::max()
                    : destination + add;
            };
            AddRoutedPairs(
                m_Stats.manualCasterProjectionPairs,
                manualIndices.size());
            AddRoutedPairs(
                m_Stats.inputAssemblerCasterProjectionPairs,
                inputAssemblerIndices.size());

            RenderSubset(
                &manualIndices,
                *m_DepthPass,
                settings.translationOnlyCasterTransformEnabled);
            RenderSubset(
                &inputAssemblerIndices,
                *m_InputAssemblerDepthPass,
                false);
        }

        void ExecuteRaster(
            nvrhi::ICommandList* commandList,
            const DiagnosticCascadedShadowMapSettings& settings,
            const std::array<CascadeWork,
                DiagnosticCsmMaximumCascades>& work)
        {
            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                const CascadeWork& cascadeWork = work[cascade];
                if (cascadeWork.action ==
                    DiagnosticCsmUpdateAction::FullRedraw)
                {
                    const std::vector<CasterRecord>& casters =
                        cascadeWork.cachedShadowDrawList
                        ? *cascadeWork.cachedShadowDrawList
                        : cascadeWork.currentCasters;
                    const CascadeProjection& projection =
                        m_CurrentProjections[cascade];
                    const DiagnosticCsmRect fullRaster =
                        MakeFullDiagnosticCsmRect(
                            settings.shadowMapResolution);
                    const DiagnosticCsmRect* receiverScissor =
                        projection.receiverRasterScissorValid
                        ? &projection.receiverRasterScissor
                        : nullptr;
                    const uint64_t fullRasterTexels = fullRaster.Area();
                    const uint64_t receiverRasterTexels =
                        receiverScissor
                        ? receiverScissor->Area()
                        : fullRasterTexels;
                    m_Stats.fullRedrawRasterBoundTexels +=
                        receiverRasterTexels;
                    m_Stats.fullRedrawRasterExcludedTexels +=
                        fullRasterTexels - std::min(
                            fullRasterTexels, receiverRasterTexels);
                    if (receiverScissor)
                    {
                        m_Stats.receiverRasterScissorEnabled = true;
                        if (receiverRasterTexels < fullRasterTexels)
                        {
                            ++m_Stats.receiverRasterScissoredCascades;
                        }
                    }
                    RenderCasterList(
                        commandList,
                        cascade,
                        settings,
                        casters,
                        receiverRasterTexels < fullRasterTexels
                            ? receiverScissor
                            : nullptr,
                        nullptr,
                        settings.detailedGpuTimingEnabled);
                    SaturatingAddUint32(
                        m_Stats.renderedCasterProjectionPairs,
                        casters.size());
                    continue;
                }

                if (cascadeWork.action ==
                    DiagnosticCsmUpdateAction::Reused)
                {
                    continue;
                }

                for (size_t rectangleIndex = 0u;
                    rectangleIndex <
                        cascadeWork.dirtyRectangles.size();
                    ++rectangleIndex)
                {
                    const DiagnosticCsmRect& rectangle =
                        cascadeWork.dirtyRectangles[rectangleIndex];
                    const std::vector<size_t>* overlapping =
                        rectangleIndex <
                            cascadeWork.dirtyCasterIndices.size()
                        ? &cascadeWork.
                            dirtyCasterIndices[rectangleIndex]
                        : nullptr;
                    RenderCasterList(
                        commandList,
                        cascade,
                        settings,
                        cascadeWork.currentCasters,
                        &rectangle,
                        overlapping,
                        settings.detailedGpuTimingEnabled);
                    const size_t renderedCasterCount = overlapping
                        ? overlapping->size()
                        : cascadeWork.currentCasters.size();
                    SaturatingAddUint32(
                        m_Stats.renderedCasterProjectionPairs,
                        renderedCasterCount);
                }
            }
        }

        void UpdateStatsAndCache(
            const DiagnosticCascadedShadowMapSettings& settings,
            const std::shared_ptr<SceneGraphNode>& rootNode,
            uint64_t sceneStateRevision,
            bool sceneStateRevisionReliable,
            const std::array<CascadeWork,
                DiagnosticCsmMaximumCascades>& work)
        {
            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                const CascadeWork& cascadeWork = work[cascade];
                m_Stats.cascadeActions[cascade] = cascadeWork.action;
                switch (cascadeWork.action)
                {
                case DiagnosticCsmUpdateAction::Reused:
                    ++m_Stats.reusedCascades;
                    break;
                case DiagnosticCsmUpdateAction::FullRedraw:
                    ++m_Stats.redrawnCascades;
                    break;
                case DiagnosticCsmUpdateAction::DirtyRectangles:
                    ++m_Stats.dirtyCascades;
                    break;
                case DiagnosticCsmUpdateAction::Scrolled:
                    ++m_Stats.scrolledCascades;
                    break;
                default:
                    break;
                }
                SaturatingAddUint32(
                    m_Stats.dirtyRectangleCount,
                    cascadeWork.dirtyRectangles.size());

                m_PreviousCompatibility[cascade] =
                    m_CurrentProjections[cascade].compatibility;
                m_PreviousProjectionValid[cascade] = true;
                const bool dirtyCacheActive =
                    settings.dirtyRectanglesEnabled &&
                    settings.wholeCascadeReuseEnabled;
                if (dirtyCacheActive &&
                    cascadeWork.currentCastersReady)
                {
                    m_CachedCasters[cascade] =
                        cascadeWork.currentCasters;
                    m_CasterSnapshotValid[cascade] =
                        cascadeWork.snapshotReliable;
                }
                else if (!dirtyCacheActive)
                {
                    m_CachedCasters[cascade].clear();
                    m_CasterSnapshotValid[cascade] = false;
                }
            }
            for (uint32_t cascade = settings.cascadeCount;
                cascade < DiagnosticCsmMaximumCascades;
                ++cascade)
            {
                m_PreviousProjectionValid[cascade] = false;
                m_CasterSnapshotValid[cascade] = false;
                m_CachedCasters[cascade].clear();
            }
            m_PreviousCascadeCount = settings.cascadeCount;
            m_PreviousSceneRevision = sceneStateRevision;
            m_PreviousSceneRevisionValid = sceneStateRevisionReliable;
            m_PreviousRoot = rootNode.get();
            m_CacheValid =
                HasAnyDiagnosticCsmCachePolicy(settings);
        }

        void Resolve(
            nvrhi::ICommandList* commandList,
            const DiagnosticCascadedShadowMapSettings& settings,
            const IView& cameraView,
            const DirectionalLight& light,
            const std::array<CascadeWork,
                DiagnosticCsmMaximumCascades>& work)
        {
            DiagnosticCsmResolveConstants constants = {};
            cameraView.FillPlanarViewConstants(constants.cameraView);
            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                const CascadeProjection& projection =
                    m_CurrentProjections[cascade];
                constants.worldToUvzw[cascade] =
                    projection.worldToUvzw;
                constants.cascadeDepthRanges[cascade] = float4(
                    projection.range.nominalNear,
                    projection.range.nominalFar,
                    projection.range.projectedFar,
                    projection.range.cascadeFadeOffset);
                constants.cascadeParameters[cascade] = float4(
                    projection.range.cascadeFadeLength,
                    float(uint32_t(work[cascade].action)),
                    ComputeUeCsmSoftTransitionScale(
                        projection.compatibility.depthBias,
                        projection.compatibility.depthFar -
                            projection.compatibility.depthNear,
                        projection.compatibility.radius,
                        settings.shadowMapResolution),
                    0.f);
            }
            if (settings.precomposedClipToShadowEnabled)
            {
                // Donut shaders use row vectors. Keeping the reconstructed
                // position homogeneous lets its world-space W cancel during
                // the existing shadow-coordinate projective divide.
                for (uint32_t cascade = 0u;
                    cascade < settings.cascadeCount;
                    ++cascade)
                {
                    constants.worldToUvzw[cascade] =
                        BuildDiagnosticCsmReceiverTransform(
                            true,
                            constants.cameraView.matClipToWorld,
                            constants.worldToUvzw[cascade]);
                }
            }
            const nvrhi::TextureDesc& outputDesc =
                m_Visibility->getDesc();
            constants.outputSize = uint2(
                outputDesc.width, outputDesc.height);
            constants.cascadeCount = settings.cascadeCount;
            constants.filterMode = uint32_t(settings.filter);
            constants.tapCount = NormalizeDiagnosticCsmTapCount(
                settings.poissonTapCount);
            constants.debugView = uint32_t(settings.debugView);
            constants.filterRadiusTexels =
                EffectiveFilterRadiusTexels(settings);
            constants.receiverDepthBias = settings.receiverDepthBias;
            constants.maximumShadowDistance =
                settings.maximumShadowDistance;
            constants.distanceFadeoutFraction =
                settings.shadowDistanceFadeoutFraction;
            constants.shadowMapResolutionInv =
                1.f / float(settings.shadowMapResolution);
            constants.shadowMapResolution =
                settings.shadowMapResolution;
            // Preserve the legacy narrowed constant exactly. The optimized
            // path normalizes once more after narrowing so it consumes a
            // unit-length value in the same float domain that the legacy
            // shader renormalizes.
            constants.directionToLight = -float3(normalize(
                light.GetDirection()));
            if (settings.preNormalizedReceiverLightDirectionEnabled)
            {
                constants.directionToLight =
                    normalize(constants.directionToLight);
            }

            commandList->writeBuffer(
                m_ResolveConstants, &constants, sizeof(constants));
            nvrhi::ComputeState state;
            state.pipeline = m_ResolvePipelines[
                GetDiagnosticCsmResolvePermutation(
                    settings.preNormalizedReceiverLightDirectionEnabled,
                    settings.precomposedClipToShadowEnabled)];
            state.bindings = { m_ResolveBindingSet };
            commandList->setComputeState(state);
            commandList->dispatch(
                div_ceil(outputDesc.width, 8u),
                div_ceil(outputDesc.height, 8u));
        }

    public:
        Impl(
            nvrhi::IDevice* device,
            const std::shared_ptr<ShaderFactory>& shaderFactory,
            const std::shared_ptr<CommonRenderPasses>& commonPasses)
            : m_Device(device)
            , m_ShaderFactory(shaderFactory)
            , m_CommonPasses(commonPasses)
        {
            m_Timings.supported =
                HasRequiredFormatSupport(device);
            m_PublishedTimings.supported = m_Timings.supported;
            if (!m_Timings.supported)
            {
                log::error(
                    "Diagnostic CSM requires sampleable D16 or D32 depth and R8_UNORM UAV output support.");
                return;
            }

            m_ClearVertexShader = shaderFactory->CreateShader(
                "uvsr/diagnostic_cascaded_shadow_map_clear_vs.hlsl",
                "main",
                nullptr,
                nvrhi::ShaderType::Vertex);
            m_ScrollPixelShader = shaderFactory->CreateShader(
                "uvsr/diagnostic_cascaded_shadow_map_scroll_ps.hlsl",
                "main",
                nullptr,
                nvrhi::ShaderType::Pixel);
            for (uint32_t permutation = 0u;
                permutation <
                    DiagnosticCsmResolvePermutationCount;
                ++permutation)
            {
                const bool preNormalized =
                    (permutation %
                        DiagnosticCsmResolveLightDirectionPermutationCount) !=
                    0u;
                const bool precomposedClipToShadow =
                    (permutation /
                        DiagnosticCsmResolveLightDirectionPermutationCount) !=
                    0u;
                std::vector<ShaderMacro> macros = {
                    ShaderMacro(
                        "DIAGNOSTIC_CSM_PRENORMALIZED_RECEIVER_LIGHT_DIRECTION",
                        preNormalized ? "1" : "0"),
                    ShaderMacro(
                        "DIAGNOSTIC_CSM_PRECOMPOSED_CLIP_TO_SHADOW",
                        precomposedClipToShadow ? "1" : "0")
                };
                m_ResolveShaders[permutation] =
                    shaderFactory->CreateShader(
                        "uvsr/diagnostic_cascaded_shadow_map_resolve_cs.hlsl",
                        "main",
                        &macros,
                        nvrhi::ShaderType::Compute);
            }

            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::Compute;
            layoutDesc.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::Texture_UAV(1),
                nvrhi::BindingLayoutItem::Sampler(0)
            };
            m_ResolveBindingLayout =
                device->createBindingLayout(layoutDesc);

            nvrhi::BindingLayoutDesc scrollLayoutDesc;
            scrollLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
            scrollLayoutDesc.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0)
            };
            m_ScrollBindingLayout =
                device->createBindingLayout(scrollLayoutDesc);

            nvrhi::BufferDesc constantsDesc;
            constantsDesc.byteSize =
                sizeof(DiagnosticCsmResolveConstants);
            constantsDesc.debugName =
                "Diagnostic CSM Resolve Constants";
            constantsDesc.isConstantBuffer = true;
            constantsDesc.isVolatile = true;
            constantsDesc.maxVersions =
                c_MaxRenderPassConstantBufferVersions;
            m_ResolveConstants = device->createBuffer(constantsDesc);

            constantsDesc.byteSize =
                sizeof(DiagnosticCsmScrollConstants);
            constantsDesc.debugName =
                "Diagnostic CSM Scroll Constants";
            constantsDesc.maxVersions =
                DiagnosticCsmMaximumCascades * 2u;
            m_ScrollConstants = device->createBuffer(constantsDesc);

            nvrhi::ComputePipelineDesc pipelineDesc;
            pipelineDesc.bindingLayouts = { m_ResolveBindingLayout };
            for (uint32_t permutation = 0u;
                permutation <
                    DiagnosticCsmResolvePermutationCount;
                ++permutation)
            {
                pipelineDesc.CS = m_ResolveShaders[permutation];
                m_ResolvePipelines[permutation] =
                    device->createComputePipeline(pipelineDesc);
            }

            nvrhi::SamplerDesc samplerDesc;
            samplerDesc.setAllAddressModes(
                nvrhi::SamplerAddressMode::Clamp);
            samplerDesc.setAllFilters(false);
            samplerDesc.setReductionType(
                nvrhi::SamplerReductionType::Standard);
            m_ShadowDepthSampler =
                device->createSampler(samplerDesc);

            const auto allResolveResourcesValid =
                [](const auto& resources)
                {
                    return std::all_of(
                        resources.begin(),
                        resources.end(),
                        [](const auto& resource)
                        {
                            return bool(resource);
                        });
                };
            if (!m_ClearVertexShader || !m_ScrollPixelShader ||
                !allResolveResourcesValid(m_ResolveShaders) ||
                !allResolveResourcesValid(m_ResolvePipelines) ||
                !m_ScrollBindingLayout ||
                !m_ScrollConstants ||
                !m_ResolveBindingLayout || !m_ResolveConstants ||
                !m_ShadowDepthSampler)
            {
                log::error(
                    "Diagnostic CSM could not initialize its required shaders or NVRHI pipeline resources.");
                m_Timings.supported = false;
                m_PublishedTimings.supported = false;
                return;
            }

            for (auto& stage : m_TimerQueries)
            {
                for (nvrhi::TimerQueryHandle& query : stage)
                {
                    query = device->createTimerQuery();
                    m_TimerQueriesSupported =
                        m_TimerQueriesSupported && bool(query);
                }
            }
            if (!m_TimerQueriesSupported)
            {
                log::warning(
                    "Diagnostic CSM GPU timer queries are unavailable; rendering remains enabled with CPU timings only.");
            }
        }

        DiagnosticCascadedShadowMapResult Render(
            nvrhi::ICommandList* commandList,
            const DiagnosticCascadedShadowMapSettings& inputSettings,
            const IView& cameraView,
            nvrhi::ITexture* cameraDepth,
            nvrhi::ITexture* cameraNormals,
            const DirectionalLight* light,
            const std::shared_ptr<SceneGraphNode>& rootNode,
            uint64_t sceneStateRevision,
            bool sceneStateRevisionReliable,
            bool requiresFullSceneInvalidation,
            InstancedOpaqueDrawStrategy& drawStrategy)
        {
            const auto totalCpuStart =
                std::chrono::steady_clock::now();
            AdvanceTimers();
            DiagnosticCascadedShadowMapSettings settings = inputSettings;
            settings.cascadeCount = std::clamp(
                settings.cascadeCount,
                1u,
                DiagnosticCsmMaximumCascades);
            settings.poissonTapCount = NormalizeDiagnosticCsmTapCount(
                settings.poissonTapCount);
            settings.projectionSnapTexelMultiple = std::clamp(
                settings.projectionSnapTexelMultiple, 1u, 16u);
            settings.cascadeDistributionExponent = std::max(
                std::isfinite(settings.cascadeDistributionExponent)
                    ? settings.cascadeDistributionExponent
                    : 1.f,
                1.f);
            settings.cascadeTransitionFraction = std::clamp(
                std::isfinite(settings.cascadeTransitionFraction)
                    ? settings.cascadeTransitionFraction
                    : 0.f,
                0.f,
                1.f);
            settings.shadowDistanceFadeoutFraction = std::clamp(
                std::isfinite(settings.shadowDistanceFadeoutFraction)
                    ? settings.shadowDistanceFadeoutFraction
                    : 0.f,
                0.f,
                1.f);
            settings.minimumScrollOverlap = std::clamp(
                std::isfinite(settings.minimumScrollOverlap)
                    ? settings.minimumScrollOverlap
                    : 1.f,
                0.f,
                1.f);
            settings.filterRadiusTexels =
                NormalizeDiagnosticCsmFilterRadiusTexels(
                    settings.filterRadiusTexels);
            settings.receiverDepthBias = std::clamp(
                settings.receiverDepthBias, 0.f, 1.f);
            settings.depthBias = std::max(settings.depthBias, 0.f);
            settings.slopeScaledDepthBias = std::max(
                settings.slopeScaledDepthBias, 0.f);
            settings.directionalLightShadowBias = std::clamp(
                settings.directionalLightShadowBias, 0.f, 1.f);
            settings.directionalLightShadowSlopeBias = std::clamp(
                settings.directionalLightShadowSlopeBias, 0.f, 1.f);
            settings.casterRadiusThreshold = std::max(
                settings.casterRadiusThreshold, 0.f);

            m_Timings.active = false;
            m_Timings.setupCpuMilliseconds = 0.f;
            m_Timings.cullingCpuMilliseconds = 0.f;
            m_Timings.recordingCpuMilliseconds = 0.f;
            m_Timings.totalCpuMilliseconds = 0.f;
            m_Timings.cullingGpuMilliseconds = 0.f;
            m_Timings.detailedGpuTimingEnabled =
                settings.detailedGpuTimingEnabled;
            m_Stats = {};

            if (!settings.enabled || !m_Timings.supported ||
                !commandList || !cameraDepth || !cameraNormals || !light ||
                !rootNode ||
                !IsDiagnosticCsmCascadeCountValid(settings.cascadeCount) ||
                !std::isfinite(settings.maximumShadowDistance) ||
                !std::isfinite(settings.maximumLightDepth) ||
                !std::isfinite(settings.filterRadiusTexels) ||
                !std::isfinite(settings.receiverDepthBias) ||
                !std::isfinite(settings.depthBias) ||
                !std::isfinite(settings.slopeScaledDepthBias) ||
                !std::isfinite(settings.directionalLightShadowBias) ||
                !std::isfinite(
                    settings.directionalLightShadowSlopeBias) ||
                !std::isfinite(settings.casterRadiusThreshold) ||
                settings.filter >= DiagnosticCsmFilter::Count ||
                !(settings.maximumShadowDistance > 0.f) ||
                !(settings.maximumLightDepth > 0.f))
            {
                Deactivate();
                return {};
            }
            const nvrhi::TextureDesc& cameraDepthDesc =
                cameraDepth->getDesc();
            UpdateTimerConfiguration(
                settings,
                cameraDepthDesc.width,
                cameraDepthDesc.height);

            const auto setupStart = std::chrono::steady_clock::now();
            const bool resourcesValid =
                EnsureDepthResources(settings) &&
                EnsureResolveResources(cameraDepth, cameraNormals);
            const bool projectionsValid = resourcesValid &&
                UpdateCascadeViews(settings, cameraView, *light);
            m_Timings.setupCpuMilliseconds =
                std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - setupStart).count();
            if (!projectionsValid)
            {
                InvalidateCache();
                InvalidateTimerConfiguration();
                if (!m_ReportedInvalidInput)
                {
                    log::error(
                        "Diagnostic CSM received an unsupported or non-finite camera, light, depth, or projection configuration; visibility remains white.");
                    m_ReportedInvalidInput = true;
                }
                return {};
            }
            m_ReportedInvalidInput = false;

            const nvrhi::TextureDesc& outputDesc =
                m_Visibility->getDesc();
            m_Stats.outputWidth = outputDesc.width;
            m_Stats.outputHeight = outputDesc.height;
            m_Stats.cascadeCount = settings.cascadeCount;
            m_Stats.shadowMapResolution =
                settings.shadowMapResolution;
            m_Stats.depthBitsPerTexel =
                m_DepthFormat == nvrhi::Format::D16 ? 16u : 32u;
            m_Stats.filterSampleCount =
                settings.filter == DiagnosticCsmFilter::Ue5Pcf5x5
                ? 9u
                : settings.poissonTapCount;
            m_Stats.filterComparisonCount =
                settings.filter == DiagnosticCsmFilter::Ue5Pcf5x5
                ? 36u
                : m_Stats.filterSampleCount;
            m_Stats.maximumShadowDistance =
                settings.maximumShadowDistance;
            m_Stats.maximumLightDepth = settings.maximumLightDepth;
            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                const auto& compatibility =
                    m_CurrentProjections[cascade].compatibility;
                m_Stats.maximumActualLightDepthSpan = std::max(
                    m_Stats.maximumActualLightDepthSpan,
                    compatibility.depthFar - compatibility.depthNear);
            }
            m_Stats.filterRadiusTexels =
                EffectiveFilterRadiusTexels(settings);
            const uint32_t coarsestCascade =
                settings.cascadeCount - 1u;
            m_Stats.finestCoverageExtent =
                m_CurrentProjections[0].compatibility.radius * 2.f;
            m_Stats.coarsestCoverageExtent =
                m_CurrentProjections[coarsestCascade].compatibility.radius *
                2.f;
            m_Stats.finestWorldTexelSize =
                m_CurrentProjections[0].compatibility.texelWorldSize;
            m_Stats.coarsestWorldTexelSize =
                m_CurrentProjections[coarsestCascade].compatibility.
                    texelWorldSize;
            m_Stats.logicalTexels =
                uint64_t(settings.shadowMapResolution) *
                settings.shadowMapResolution * settings.cascadeCount;
            m_Stats.depthBytes =
                uint64_t(settings.shadowMapResolution) *
                settings.shadowMapResolution *
                DiagnosticCsmMaximumCascades *
                (m_Stats.depthBitsPerTexel / 8u);
            m_Stats.visibilityBytes =
                uint64_t(outputDesc.width) * outputDesc.height;
            m_Stats.debugVisualizationBytes =
                m_Stats.visibilityBytes;
            m_Stats.scrollingScratchBytes = m_ScrollScratch
                ? uint64_t(settings.shadowMapResolution) *
                    settings.shadowMapResolution *
                    (m_Stats.depthBitsPerTexel / 8u)
                : 0u;
            m_Stats.opaqueDepthStateMergingEnabled =
                settings.opaqueDepthStateMergingEnabled;
            m_Stats.positionOnlyOpaqueEnabled =
                settings.positionOnlyOpaqueEnabled;
            m_Stats.translationOnlyCasterTransformRequested =
                settings.translationOnlyCasterTransformEnabled;
            m_Stats.translationOnlyCasterTransformEnabled =
                settings.translationOnlyCasterTransformEnabled &&
                m_AppliedTranslationOnlyCasterTransformEnabled &&
                bool(m_DepthPass);
            m_Stats.inputAssemblerCasterFetchRequested =
                settings.inputAssemblerCasterFetchEnabled;
            m_Stats.inputAssemblerCasterFetchEnabled =
                settings.inputAssemblerCasterFetchEnabled &&
                m_AppliedInputAssemblerCasterFetchEnabled &&
                bool(m_InputAssemblerDepthPass);
            m_Stats.precomputedDepthAxisInverseLengthRequested =
                settings.precomputedDepthAxisInverseLengthEnabled;
            m_Stats.precomputedDepthAxisInverseLengthEnabled =
                settings.precomputedDepthAxisInverseLengthEnabled;
            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount &&
                    m_Stats.precomputedDepthAxisInverseLengthEnabled;
                ++cascade)
            {
                m_Stats.precomputedDepthAxisInverseLengthEnabled =
                    m_CurrentProjections[cascade].
                        inverseDepthAxisLength > 0.f;
            }
            m_Stats.conservativeSaturatedSlopeRequested =
                settings.conservativeSaturatedSlopeEnabled;
            m_Stats.conservativeSaturatedSlopeActive =
                settings.conservativeSaturatedSlopeEnabled &&
                m_Stats.precomputedDepthAxisInverseLengthEnabled &&
                m_AppliedConservativeSaturatedSlopeEnabled &&
                bool(m_DepthPass);
            m_Stats.algebraicSlowSlopeRequested =
                settings.algebraicSlowSlopeEnabled;
            m_Stats.algebraicSlowSlopeActive =
                settings.algebraicSlowSlopeEnabled &&
                m_Stats.precomputedDepthAxisInverseLengthEnabled &&
                m_AppliedAlgebraicSlowSlopeEnabled &&
                bool(m_DepthPass);
            m_Stats.preNormalizedReceiverLightDirectionRequested =
                settings.preNormalizedReceiverLightDirectionEnabled;
            m_Stats.preNormalizedReceiverLightDirectionEnabled =
                settings.preNormalizedReceiverLightDirectionEnabled &&
                bool(m_ResolvePipelines[
                    GetDiagnosticCsmResolvePermutation(
                        true,
                        settings.precomposedClipToShadowEnabled)]);
            m_Stats.precomposedClipToShadowRequested =
                settings.precomposedClipToShadowEnabled;
            m_Stats.precomposedClipToShadowEnabled =
                settings.precomposedClipToShadowEnabled &&
                bool(m_ResolvePipelines[
                    GetDiagnosticCsmResolvePermutation(
                        settings.preNormalizedReceiverLightDirectionEnabled,
                        true)]);
            m_Stats.directCasterSubmissionRequested =
                settings.directCasterSubmissionEnabled;
            m_Stats.batchedFullRedrawClearRequested =
                settings.batchedFullRedrawClearEnabled;
            m_Stats.receiverRasterScissorRequested =
                settings.receiverRasterScissorEnabled;
            m_Stats.submissionStatsAvailable =
                settings.detailedGpuTimingEnabled;
            m_Stats.accurateCasterCullingRequested =
                settings.accurateCasterCullingEnabled;
            m_Stats.accurateCasterCullingEnabled =
                settings.accurateCasterCullingEnabled &&
                CanUseDiagnosticCsmViewDependentCasterCulling(settings);
            if (m_Stats.accurateCasterCullingEnabled)
            {
                m_Stats.accurateCasterCullingEnabled = false;
                for (uint32_t cascade = 0u;
                    cascade < settings.cascadeCount;
                    ++cascade)
                {
                    if (m_CurrentProjections[cascade].
                        casterCullVolume.valid)
                    {
                        m_Stats.accurateCasterCullingEnabled = true;
                        break;
                    }
                }
            }
            m_Stats.precomputedReceiverHullAxesRequested =
                settings.precomputedReceiverHullAxesEnabled;
            m_Stats.precomputedReceiverHullAxesEnabled =
                settings.precomputedReceiverHullAxesEnabled &&
                m_Stats.accurateCasterCullingEnabled;
            m_Stats.sharedCasterLightProjectionRequested =
                settings.sharedCasterLightProjectionEnabled;
            m_Stats.ueCasterRadiusThresholdRequested =
                settings.ueCasterRadiusThresholdEnabled;
            m_Stats.ueCasterRadiusThresholdEnabled =
                IsDiagnosticCsmCasterRadiusThresholdActive(settings);
            m_Stats.casterRadiusThreshold =
                settings.casterRadiusThreshold;
            m_Stats.light = light;
            m_Stats.filter = settings.filter;
            if (m_ResourcesRecreatedThisFrame)
            {
                m_Stats.invalidationMask |=
                    DiagnosticCsmInvalidation_Resources;
            }
            if (m_PreviousCascadeCount != 0u &&
                m_PreviousCascadeCount != settings.cascadeCount)
            {
                m_Stats.invalidationMask |=
                    DiagnosticCsmInvalidation_Profile;
            }

            auto& work = m_Work;
            for (CascadeWork& cascadeWork : work)
            {
                cascadeWork.action =
                    DiagnosticCsmUpdateAction::FullRedraw;
                cascadeWork.scroll = {};
                cascadeWork.dirtyRectangles.clear();
                for (auto& indices : cascadeWork.dirtyCasterIndices)
                    indices.clear();
                cascadeWork.currentCasters.clear();
                cascadeWork.projectedCasterRectangles.clear();
                cascadeWork.projectedCasterBoundsReliable.clear();
                cascadeWork.cachedShadowDrawList = nullptr;
                cascadeWork.currentCastersReady = false;
                cascadeWork.snapshotReliable = false;
            }
            const auto cullingStart = std::chrono::steady_clock::now();
            PrepareWork(
                settings,
                cameraView,
                light,
                rootNode,
                sceneStateRevision,
                sceneStateRevisionReliable,
                requiresFullSceneInvalidation,
                drawStrategy,
                work);
            m_Timings.cullingCpuMilliseconds =
                std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - cullingStart).count();

            const auto recordingStart =
                std::chrono::steady_clock::now();
            commandList->beginMarker("Diagnostic CSM");
            BeginTimerFrame(settings.detailedGpuTimingEnabled);
            BeginTimer(commandList, TimerTotal);

            bool hasClearUpdate = false;
            bool hasRaster = false;
            for (uint32_t cascade = 0u;
                cascade < settings.cascadeCount;
                ++cascade)
            {
                hasClearUpdate = hasClearUpdate ||
                    work[cascade].action !=
                        DiagnosticCsmUpdateAction::Reused;
                hasRaster = hasRaster ||
                    (work[cascade].action ==
                        DiagnosticCsmUpdateAction::FullRedraw) ||
                    !work[cascade].dirtyRectangles.empty();
            }

            if (hasClearUpdate)
            {
                BeginTimer(commandList, TimerClearUpdate);
                ExecuteClearAndScrolls(commandList, settings, work);
                EndTimer(commandList, TimerClearUpdate);
            }
            if (hasRaster)
            {
                BeginTimer(commandList, TimerRaster);
                ExecuteRaster(commandList, settings, work);
                EndTimer(commandList, TimerRaster);
            }

            BeginTimer(commandList, TimerSampling);
            Resolve(commandList, settings, cameraView, *light, work);
            EndTimer(commandList, TimerSampling);
            EndTimer(commandList, TimerTotal);
            commandList->endMarker();
            m_Timings.recordingCpuMilliseconds =
                std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() -
                    recordingStart).count();

            UpdateStatsAndCache(
                settings,
                rootNode,
                sceneStateRevision,
                sceneStateRevisionReliable,
                work);
            m_Timings.totalCpuMilliseconds =
                std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() -
                    totalCpuStart).count();
            m_Timings.active = true;
            CaptureTimerFrameSnapshot();
            EndTimerFrame();

            return {
                m_Visibility,
                m_DebugVisualization,
                light,
                settings.debugView != DiagnosticCsmDebugView::None
            };
        }

        void Deactivate()
        {
            m_Timings.active = false;
            m_Timings.setupCpuMilliseconds = 0.f;
            m_Timings.cullingCpuMilliseconds = 0.f;
            m_Timings.recordingCpuMilliseconds = 0.f;
            m_Timings.totalCpuMilliseconds = 0.f;
            m_Stats = {};
            InvalidateTimerConfiguration();
            InvalidateCache();
        }

        void ResetSceneState()
        {
            if (m_DepthPass)
                m_DepthPass->ResetBindingCache();
            if (m_InputAssemblerDepthPass)
                m_InputAssemblerDepthPass->ResetBindingCache();
            InvalidateTimerConfiguration();
            InvalidateCache();
        }

        [[nodiscard]] const DiagnosticCsmTimings& GetTimings() const
        {
            return m_PublishedTimings;
        }

        [[nodiscard]] const DiagnosticCsmStats& GetStats() const
        {
            return m_PublishedStats;
        }
    };

    DiagnosticCascadedShadowMapPass::DiagnosticCascadedShadowMapPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses)
        : m_Impl(std::make_unique<Impl>(
            device, shaderFactory, commonPasses))
    {
    }

    DiagnosticCascadedShadowMapPass::~DiagnosticCascadedShadowMapPass() =
        default;

    DiagnosticCascadedShadowMapResult
        DiagnosticCascadedShadowMapPass::Render(
            nvrhi::ICommandList* commandList,
            const DiagnosticCascadedShadowMapSettings& settings,
            const IView& cameraView,
            nvrhi::ITexture* cameraDepth,
            nvrhi::ITexture* cameraNormals,
            const DirectionalLight* light,
            const std::shared_ptr<SceneGraphNode>& rootNode,
            uint64_t sceneStateRevision,
            bool sceneStateRevisionReliable,
            bool requiresFullSceneInvalidation,
            InstancedOpaqueDrawStrategy& drawStrategy)
    {
        return m_Impl->Render(
            commandList,
            settings,
            cameraView,
            cameraDepth,
            cameraNormals,
            light,
            rootNode,
            sceneStateRevision,
            sceneStateRevisionReliable,
            requiresFullSceneInvalidation,
            drawStrategy);
    }

    void DiagnosticCascadedShadowMapPass::Deactivate()
    {
        m_Impl->Deactivate();
    }

    void DiagnosticCascadedShadowMapPass::ResetSceneState()
    {
        m_Impl->ResetSceneState();
    }

    const DiagnosticCsmTimings&
        DiagnosticCascadedShadowMapPass::GetTimings() const
    {
        return m_Impl->GetTimings();
    }

    const DiagnosticCsmStats&
        DiagnosticCascadedShadowMapPass::GetStats() const
    {
        return m_Impl->GetStats();
    }
}
