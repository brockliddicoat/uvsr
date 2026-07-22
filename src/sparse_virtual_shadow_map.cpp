#include "sparse_virtual_shadow_map.h"
#include "gpu_crash_diagnostics.h"

#include <donut/core/log.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/MaterialBindingCache.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/render/GeometryPasses.h>
#include <directx/d3d12.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

using namespace donut;
using namespace donut::engine;
using namespace donut::math;
using namespace donut::render;

#include <donut/shaders/gbuffer_cb.h>
#include "sparse_virtual_shadow_map_cb.h"
#include "sparse_virtual_shadow_map_sparse_cb.h"

static_assert(sizeof(SparseVirtualShadowMapResolveConstants) % 16u == 0u,
    "SVSM resolve constants must preserve HLSL register alignment.");
static_assert(offsetof(
        SparseVirtualShadowMapResolveConstants,
        filterMode) ==
    offsetof(SparseVirtualShadowMapResolveConstants, depthBias) + 8u &&
    sizeof(SparseVirtualShadowMapResolveConstants) ==
        offsetof(SparseVirtualShadowMapResolveConstants, depthBias) + 16u,
    "SVSM dense resolve tail must occupy one exact HLSL register.");
static_assert(sizeof(SparseVirtualShadowMapSparseConstants) % 16u == 0u,
    "SVSM sparse constants must preserve HLSL register alignment.");
static_assert(offsetof(
        SparseVirtualShadowMapSparseConstants,
        debugView) ==
    offsetof(SparseVirtualShadowMapSparseConstants, selectedClipmap) + 8u &&
    offsetof(SparseVirtualShadowMapSparseConstants, markingMode) ==
        offsetof(
            SparseVirtualShadowMapSparseConstants,
            selectedClipmap) + 16u &&
    sizeof(SparseVirtualShadowMapSparseConstants) ==
        offsetof(
            SparseVirtualShadowMapSparseConstants,
            drawPacketCount) + 16u,
    "SVSM sparse constant-buffer tail rows must match HLSL packing.");
static_assert(SVSM_CLIPMAP_COUNT == uvsr::SvsmClipmapCount,
    "The CPU and HLSL clipmap counts must match.");
static_assert(
    SVSM_SPARSE_FLAG_SCATTER_ALPHA_TEST_EARLY_REJECT ==
        uvsr::SvsmSparseFlagScatterAlphaTestEarlyReject,
    "The CPU and HLSL scatter alpha-test flags must match.");
static_assert(
    SVSM_SPARSE_FLAG_ALLOCATION_BUDGET_SATURATION_EARLY_OUT ==
        uvsr::SvsmSparseFlagAllocationBudgetSaturationEarlyOut,
    "The CPU and HLSL allocation saturation flags must match.");
static_assert(
    SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_AMPLIFICATION_GUARD ==
        uvsr::SvsmSparseFlagDirtyPageScatterAmplificationGuard,
    "The CPU and HLSL scatter amplification flags must match.");
static_assert(
    SVSM_SPARSE_FLAG_COARSEST_PAGE_RENDER_BUDGET ==
        uvsr::SvsmSparseFlagCoarsestPageRenderBudget,
    "The CPU and HLSL coarsest-page budget flags must match.");
static_assert(sizeof(nvrhi::DrawIndexedIndirectArguments) == 20u,
    "SVSM relies on NVRHI's packed indexed indirect layout.");
static_assert(
    offsetof(
        nvrhi::DrawIndexedIndirectArguments,
        instanceCount) == sizeof(uint32_t),
    "SVSM indirect instance counts must remain the second word.");
static_assert(
    sizeof(SparseVirtualShadowMapPacketMetadata) ==
        4u * sizeof(uint32_t),
    "SVSM packet-page metadata must remain four uint32 words.");
static_assert(
    offsetof(SparseVirtualShadowMapPacketMetadata, packedMinimumPage) == 0u &&
    offsetof(SparseVirtualShadowMapPacketMetadata, packedMaximumPage) == 4u &&
    offsetof(SparseVirtualShadowMapPacketMetadata, pageListOffset) == 8u &&
    offsetof(SparseVirtualShadowMapPacketMetadata, objectInstanceIndex) == 12u,
    "SVSM packet-page metadata member offsets must match HLSL.");
static_assert(
    sizeof(SparseVirtualShadowMapPushConstants) ==
        8u * sizeof(uint32_t),
    "SVSM sparse depth push constants must remain eight uint32 words.");
static_assert(
    offsetof(SparseVirtualShadowMapPushConstants, startInstanceLocation) == 0u &&
    offsetof(SparseVirtualShadowMapPushConstants, startVertexLocation) == 4u &&
    offsetof(SparseVirtualShadowMapPushConstants, positionOffset) == 8u &&
    offsetof(SparseVirtualShadowMapPushConstants, texCoordOffset) == 12u &&
    offsetof(SparseVirtualShadowMapPushConstants, originalInstanceCount) == 16u &&
    offsetof(SparseVirtualShadowMapPushConstants, physicalPageCount) == 20u &&
    offsetof(SparseVirtualShadowMapPushConstants, flags) == 24u &&
    offsetof(SparseVirtualShadowMapPushConstants, packetIndex) == 28u,
    "SVSM sparse depth push-constant member offsets must match HLSL.");
static_assert(
    sizeof(nvrhi::DispatchIndirectArguments) == 3u * sizeof(uint32_t),
    "SVSM compact page dispatch relies on NVRHI's packed dispatch layout.");
static_assert(
    offsetof(nvrhi::DispatchIndirectArguments, groupsX) == 0u,
    "SVSM compact page counts must remain the first dispatch word.");
static_assert(
    SVSM_PACKET_FILL_DISPATCH_WIDTH ==
        uvsr::SvsmMaximumDispatchGroupsPerDimension,
    "SVSM CPU and HLSL packet-fill dispatch widths must match.");
static_assert(
    SVSM_PACKET_FILL_THREADS ==
        uvsr::SvsmPacketFillThreadsPerGroup,
    "SVSM CPU and HLSL packet-fill thread counts must match.");
static_assert(
    SVSM_PACKET_PAGE_RUNTIME_WORDS ==
        uvsr::SvsmPacketPageRuntimeWords,
    "SVSM CPU and HLSL packet-runtime strides must match.");
static_assert(
    SVSM_PACKET_PAGE_RUNTIME_PER_PAGE ==
            uvsr::SvsmPacketPageRuntimePerPageBit &&
        SVSM_PACKET_PAGE_RUNTIME_FAIL_OPEN ==
            uvsr::SvsmPacketPageRuntimeFailOpenBit &&
        SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK ==
            uvsr::SvsmPacketPageRuntimeCountMask,
    "SVSM CPU and HLSL packet-runtime state bits must match.");
static_assert(
    SVSM_PACKET_PAGE_RUNTIME_STATE_WORD ==
            uvsr::SvsmPacketPageRuntimeStateWord &&
        SVSM_PACKET_PAGE_RUNTIME_MINIMUM_WORD ==
            uvsr::SvsmPacketPageRuntimeMinimumWord &&
        SVSM_PACKET_PAGE_RUNTIME_MAXIMUM_WORD ==
            uvsr::SvsmPacketPageRuntimeMaximumWord,
    "SVSM CPU and HLSL packet-runtime word offsets must match.");
static_assert(
    SVSM_DIRTY_PAGE_RECT_WORDS_PER_LEVEL ==
        uvsr::SvsmDirtyPageRectangleWordsPerLevel,
    "SVSM CPU and HLSL dirty-rectangle sizes must match.");
static_assert(
    SVSM_SPARSE_RECENT_PAGE_EVICTION_GRACE_FRAMES ==
        uvsr::SvsmRecentPageEvictionGraceFrames,
    "SVSM CPU and HLSL recent-page grace windows must match.");
static_assert(
    SVSM_SPARSE_LEVEL_HAS_WORK_COUNTER_BASE ==
        uvsr::SvsmLevelHasWorkCounterBase,
    "SVSM CPU and HLSL level-work counter bases must match.");
static_assert(
    SVSM_SPARSE_LEVEL_HAS_WORK_DISPATCH_GATE ==
            uvsr::SvsmLevelHasWorkDispatchGate,
    "SVSM CPU and HLSL level-work dispatch gates must match.");
static_assert(
    SVSM_SPARSE_COUNTER_COUNT == uvsr::SvsmCounterCount,
    "SVSM CPU and HLSL counter buffer sizes must match.");

namespace uvsr
{
    namespace
    {
        constexpr uint32_t TimerPageMarking = 0u;
        constexpr uint32_t TimerAllocation = 1u;
        constexpr uint32_t TimerClearing = 2u;
        constexpr uint32_t TimerPacketPageCulling = 3u;
        constexpr uint32_t TimerPageRendering = 4u;
        constexpr uint32_t TimerFiltering = 5u;
        constexpr uint32_t TimerTotal = 6u;
        constexpr uint32_t SparsePrepare = 0u;
        constexpr uint32_t SparseMark = 1u;
        constexpr uint32_t SparseRecycle = 2u;
        constexpr uint32_t SparseAllocate = 3u;
        constexpr uint32_t SparseClear = 4u;
        constexpr uint32_t SparseFinalize = 5u;
        constexpr uint32_t SparseStats = 6u;
        constexpr uint32_t SparseFillIndirect = 7u;
        constexpr uint32_t CompactPageDispatchArgumentBase = 0u;
        constexpr uint32_t PacketFillDispatchArgumentBase =
            SvsmClipmapCount;
        constexpr uint32_t GatedDispatchArgumentCount =
            SvsmClipmapCount * 2u;
        constexpr uint32_t MaximumPacketRenderPageEntries =
            16u * 1024u * 1024u;
        bool HasExtendedCommandInfoSupport(nvrhi::IDevice* device)
        {
            if (!device ||
                device->getGraphicsAPI() !=
                    nvrhi::GraphicsAPI::D3D12)
            {
                return false;
            }

            ID3D12Device* nativeDevice =
                device->getNativeObject(
                    nvrhi::ObjectTypes::D3D12_Device);
            if (!nativeDevice)
                return false;

            D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = {
                D3D_SHADER_MODEL_6_8
            };
            if (FAILED(nativeDevice->CheckFeatureSupport(
                    D3D12_FEATURE_SHADER_MODEL,
                    &shaderModel,
                    sizeof(shaderModel))) ||
                shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_8)
            {
                return false;
            }

            D3D12_FEATURE_DATA_D3D12_OPTIONS21 options = {};
            return SUCCEEDED(nativeDevice->CheckFeatureSupport(
                    D3D12_FEATURE_D3D12_OPTIONS21,
                    &options,
                    sizeof(options))) &&
                options.ExtendedCommandInfoSupported;
        }

        bool HasRequiredFormatSupport(nvrhi::IDevice* device)
        {
            const nvrhi::FormatSupport r32Required =
                nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::ShaderLoad |
                nvrhi::FormatSupport::ShaderUavLoad |
                nvrhi::FormatSupport::ShaderUavStore |
                nvrhi::FormatSupport::ShaderAtomic;
            const nvrhi::FormatSupport r8Required =
                nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::ShaderLoad |
                nvrhi::FormatSupport::ShaderSample |
                nvrhi::FormatSupport::ShaderUavStore;
            return (device->queryFormatSupport(nvrhi::Format::R32_UINT) &
                    r32Required) == r32Required &&
                (device->queryFormatSupport(nvrhi::Format::R8_UNORM) &
                    r8Required) == r8Required;
        }

        uint64_t TextureByteSize(
            uint32_t width,
            uint32_t height,
            uint32_t arraySize,
            uint32_t bytesPerPixel)
        {
            return uint64_t(width) * uint64_t(height) *
                uint64_t(arraySize) * uint64_t(bytesPerPixel);
        }

        SparseVirtualShadowMapPacketMetadata
        BuildPacketPageMetadata(
            const DrawItem& item,
            const float4x4& worldToClip,
            uint32_t pageListOffset,
            uint32_t& pageListCapacity)
        {
            SparseVirtualShadowMapPacketMetadata metadata = {};
            metadata.packedMinimumPage =
                SvsmInvalidPacketPageBounds;
            metadata.packedMaximumPage =
                SvsmInvalidPacketPageBounds;
            metadata.pageListOffset = pageListOffset;
            metadata.objectInstanceIndex = item.instance
                ? uint32_t(std::max(
                    item.instance->GetInstanceIndex(), 0))
                : 0u;
            pageListCapacity = 0u;

            const SceneGraphNode* node = item.instance
                ? item.instance->GetNode()
                : nullptr;
            if (!node || item.instance->GetInstanceIndex() < 0)
                return metadata;

            if (!item.mesh ||
                !item.geometry ||
                !CanUseSvsmStaticPacketBounds(
                    bool(item.mesh->skinPrototype),
                    item.mesh->isSkinPrototype,
                    item.mesh->isMorphTargetAnimationMesh))
            {
                // Donut's skinned and morph bounds are fixed prototype
                // bounds, not a proven envelope for every deformed pose.
                // Retaining invalid metadata makes those packets use the
                // complete dirty-page list instead of risking cut shadows.
                return metadata;
            }

            const box3& objectBounds =
                item.geometry->objectSpaceBounds;
            const affine3 localToWorld =
                node->GetLocalToWorldTransformFloat();
            if (objectBounds.isempty() ||
                !dm::all(dm::isfinite(objectBounds.m_mins)) ||
                !dm::all(dm::isfinite(objectBounds.m_maxs)))
            {
                return metadata;
            }

            float2 minimumVirtual =
                float2(std::numeric_limits<float>::infinity());
            float2 maximumVirtual =
                float2(-std::numeric_limits<float>::infinity());
            for (uint32_t corner = 0u; corner < 8u; ++corner)
            {
                const float3 objectPosition = {
                    (corner & 1u) != 0u
                        ? objectBounds.m_maxs.x
                        : objectBounds.m_mins.x,
                    (corner & 2u) != 0u
                        ? objectBounds.m_maxs.y
                        : objectBounds.m_mins.y,
                    (corner & 4u) != 0u
                        ? objectBounds.m_maxs.z
                        : objectBounds.m_mins.z
                };
                // Project the eight corners of the transformed object box
                // directly. Forming a world-axis-aligned box first encloses
                // the rotated box a second time and can turn a small caster
                // into many unnecessary packet/page intersections.
                const float3 worldPosition =
                    localToWorld.transformPoint(objectPosition);
                const float4 clip =
                    float4(worldPosition, 1.f) * worldToClip;
                if (!dm::all(dm::isfinite(clip)) ||
                    !(clip.w > 1e-8f))
                {
                    return metadata;
                }
                const float2 ndc = clip.xy() / clip.w;
                const float2 virtualPosition =
                    (ndc * float2(0.5f, -0.5f) +
                        float2(0.5f)) *
                    float(SvsmVirtualResolution);
                minimumVirtual.x = std::min(
                    minimumVirtual.x, virtualPosition.x);
                minimumVirtual.y = std::min(
                    minimumVirtual.y, virtualPosition.y);
                maximumVirtual.x = std::max(
                    maximumVirtual.x, virtualPosition.x);
                maximumVirtual.y = std::max(
                    maximumVirtual.y, virtualPosition.y);
            }

            if (!dm::all(dm::isfinite(minimumVirtual)) ||
                !dm::all(dm::isfinite(maximumVirtual)))
            {
                return metadata;
            }
            const SvsmPacketPageRectangle rectangle =
                GetSvsmPacketPageRectangle(
                    minimumVirtual.x,
                    minimumVirtual.y,
                    maximumVirtual.x,
                    maximumVirtual.y);
            metadata.packedMinimumPage = rectangle.packedMinimum;
            metadata.packedMaximumPage = rectangle.packedMaximum;
            pageListCapacity = GetSvsmPacketPageListCapacity(
                metadata.packedMinimumPage,
                metadata.packedMaximumPage);
            return metadata;
        }

        bool IsDenseInputValid(
            const IView& cameraView,
            const nvrhi::TextureDesc& depthDesc)
        {
            return cameraView.IsReverseDepth() &&
                depthDesc.width > 0u &&
                depthDesc.height > 0u &&
                depthDesc.sampleCount == 1u &&
                depthDesc.dimension == nvrhi::TextureDimension::Texture2D;
        }
    }

    class SparseVirtualShadowMapPass::DenseDepthPass final
        : public GBufferFillPass
    {
    private:
        nvrhi::ITexture* m_PhysicalDepth;
        std::array<nvrhi::BindingSetHandle, SvsmClipmapCount>
            m_SliceBindingSets;
        uint32_t m_SelectedSlice = 0u;

    protected:
        nvrhi::ShaderHandle CreatePixelShader(
            ShaderFactory& shaderFactory,
            const CreateParameters&,
            bool alphaTested) override
        {
            std::vector<ShaderMacro> macros;
            macros.emplace_back(
                "ALPHA_TESTED", alphaTested ? "1" : "0");
            return shaderFactory.CreateShader(
                "uvsr/sparse_virtual_shadow_map_depth_ps.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Pixel);
        }

        void CreateViewBindings(
            nvrhi::BindingLayoutHandle& layout,
            nvrhi::BindingSetHandle& set,
            const CreateParameters& params) override
        {
            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility =
                nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
            layoutDesc.registerSpace = GBUFFER_SPACE_VIEW;
            layoutDesc.registerSpaceIsDescriptorSet = true;
            layoutDesc.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    GBUFFER_BINDING_VIEW_CONSTANTS),
                nvrhi::BindingLayoutItem::Sampler(
                    GBUFFER_BINDING_MATERIAL_SAMPLER),
                nvrhi::BindingLayoutItem::Texture_UAV(0)
            };
            layout = m_Device->createBindingLayout(layoutDesc);

            for (uint32_t slice = 0u;
                slice < SvsmClipmapCount;
                ++slice)
            {
                nvrhi::BindingSetDesc setDesc;
                setDesc.trackLiveness = params.trackLiveness;
                setDesc.bindings = {
                    nvrhi::BindingSetItem::ConstantBuffer(
                        GBUFFER_BINDING_VIEW_CONSTANTS, m_GBufferCB),
                    nvrhi::BindingSetItem::Sampler(
                        GBUFFER_BINDING_MATERIAL_SAMPLER,
                        m_CommonPasses->m_AnisotropicWrapSampler),
                    nvrhi::BindingSetItem::Texture_UAV(
                        0,
                        m_PhysicalDepth,
                        nvrhi::Format::R32_UINT,
                        nvrhi::TextureSubresourceSet(0, 1, slice, 1),
                        nvrhi::TextureDimension::Texture2D)
                };
                m_SliceBindingSets[slice] =
                    m_Device->createBindingSet(setDesc, layout);
            }
            set = m_SliceBindingSets[0];
        }

        nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(
            PipelineKey key,
            nvrhi::FramebufferInfo const& framebufferInfo) override
        {
            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.inputLayout = m_InputLayout;
            pipelineDesc.VS = m_VertexShader;
            pipelineDesc.PS = key.bits.alphaTested
                ? m_PixelShaderAlphaTested
                : m_PixelShader;
            pipelineDesc.renderState.rasterState
                .setFrontCounterClockwise(
                    key.bits.frontCounterClockwise)
                .setCullMode(key.bits.cullMode);
            if (key.bits.alphaTested)
                pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.renderState.depthStencilState.disableDepthTest();
            pipelineDesc.renderState.blendState.disableAlphaToCoverage();
            pipelineDesc.bindingLayouts = {
                m_MaterialBindings->GetLayout(),
                m_ViewBindingLayout
            };
            if (!m_UseInputAssembler)
                pipelineDesc.bindingLayouts.push_back(m_InputBindingLayout);
            return m_Device->createGraphicsPipeline(
                pipelineDesc, framebufferInfo);
        }

    public:
        DenseDepthPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<CommonRenderPasses>& commonPasses,
            nvrhi::ITexture* physicalDepth)
            : GBufferFillPass(device, commonPasses)
            , m_PhysicalDepth(physicalDepth)
        {
        }

        void SelectSlice(uint32_t slice)
        {
            m_SelectedSlice = std::min(slice, SvsmClipmapCount - 1u);
            m_ViewBindings = m_SliceBindingSets[m_SelectedSlice];
        }

        void SetupView(
            GeometryPassContext& context,
            nvrhi::ICommandList* commandList,
            const IView* view,
            const IView* viewPrev) override
        {
            m_ViewBindings = m_SliceBindingSets[m_SelectedSlice];
            GBufferFillPass::SetupView(
                context, commandList, view, viewPrev);
        }
    };

    class SparseVirtualShadowMapPass::SparseDepthPass final
        : public GBufferFillPass
    {
    private:
        nvrhi::ITexture* m_PhysicalDepth;
        nvrhi::ITexture* m_PageTable;
        nvrhi::IBuffer* m_CompactRenderPages;
        nvrhi::IBuffer* m_RenderPages;
        nvrhi::IBuffer* m_SparseConstants;
        nvrhi::IBuffer* m_Counters;
        nvrhi::IBuffer* m_IndirectDrawArguments;
        nvrhi::IBuffer* m_PacketPageMetadata;
        nvrhi::IBuffer* m_PacketPageRuntime;
        nvrhi::IBuffer* m_PacketRenderPages;
        uint32_t m_PhysicalPageCount;
        nvrhi::ShaderHandle m_BatchedVertexShader;
        std::array<nvrhi::GraphicsPipelineHandle, PipelineKey::Count>
            m_BatchedPipelines;
        bool m_BatchedDrawSupported = false;
        bool m_BatchedPipelineActive = false;
        bool m_TrackViewBindingLiveness = false;

        struct RenderPacket
        {
            std::shared_ptr<Material> material;
            std::shared_ptr<BufferGroup> buffers;
            nvrhi::RasterCullMode cullMode =
                nvrhi::RasterCullMode::Back;
            SvsmBatchedDrawStateKey stateKey;
            nvrhi::DrawArguments arguments;
            uint32_t argumentIndex = 0u;
            SparseVirtualShadowMapPacketMetadata pageMetadata = {};
            bool batchable = false;
        };

        struct RenderPacketGroup
        {
            uint32_t firstPacket = 0u;
            uint32_t packetCount = 0u;
            bool batchable = false;
        };

        std::array<std::vector<RenderPacket>, SvsmClipmapCount>
            m_RenderPackets;
        std::array<std::vector<RenderPacketGroup>, SvsmClipmapCount>
            m_RenderPacketGroups;
        std::array<float4x4, SvsmClipmapCount>
            m_RenderPacketMatrices{};
        std::array<uint32_t, SvsmClipmapCount>
            m_RenderPacketOffsets{};
        uint32_t m_BatchedRasterStateMask = 0u;
        std::shared_ptr<SceneGraphNode> m_RenderPacketRoot;
        uint64_t m_RenderPacketSceneStateHash = 0u;
        uint32_t m_RenderPacketCount = 0u;
        uint32_t m_RenderPacketPageEntryCount = 0u;
        uint32_t m_RenderPacketFirstClipmap = 0u;
        bool m_RenderPacketPageMetadataRequested = false;
        bool m_RenderPacketExactPageListsRequested = false;
        bool m_RenderPacketDirtyPageScatterRasterRequested = false;
        bool m_RenderPacketStateSortingRequested = false;
        bool m_RenderPacketPageMetadataSupported = false;
        bool m_RenderPacketPageDispatchSupported = false;
        bool m_RenderPacketCacheValid = false;

        nvrhi::BindingSetHandle CreateSparseViewBindingSet(
            bool trackLiveness)
        {
            if (!m_ViewBindingLayout ||
                !m_PacketPageMetadata ||
                !m_PacketPageRuntime ||
                !m_PacketRenderPages ||
                !m_PageTable ||
                !m_RenderPages)
            {
                return nullptr;
            }
            nvrhi::BindingSetDesc setDesc;
            setDesc.trackLiveness = trackLiveness;
            setDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    GBUFFER_BINDING_VIEW_CONSTANTS, m_GBufferCB),
                nvrhi::BindingSetItem::Sampler(
                    GBUFFER_BINDING_MATERIAL_SAMPLER,
                    m_CommonPasses->m_AnisotropicWrapSampler),
                nvrhi::BindingSetItem::ConstantBuffer(
                    3, m_SparseConstants),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    7, m_CompactRenderPages),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    8, m_PacketPageMetadata),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    9, m_PacketPageRuntime),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    10, m_PacketRenderPages),
                nvrhi::BindingSetItem::Texture_SRV(
                    11, m_PageTable),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    12, m_RenderPages),
                nvrhi::BindingSetItem::Texture_UAV(
                    0,
                    m_PhysicalDepth,
                    nvrhi::Format::R32_UINT,
                    nvrhi::TextureSubresourceSet(0, 1, 0, 1),
                    nvrhi::TextureDimension::Texture2D)
            };
            return m_Device->createBindingSet(
                setDesc, m_ViewBindingLayout);
        }

    protected:
        nvrhi::ShaderHandle CreateVertexShader(
            ShaderFactory& shaderFactory,
            const CreateParameters&) override
        {
            std::vector<ShaderMacro> macros;
            macros.emplace_back("SVSM_BATCHED_DRAW", "0");
            return shaderFactory.CreateShader(
                "uvsr/sparse_virtual_shadow_map_sparse_depth.hlsl",
                "vertexMain",
                &macros,
                nvrhi::ShaderType::Vertex);
        }

        nvrhi::ShaderHandle CreatePixelShader(
            ShaderFactory& shaderFactory,
            const CreateParameters&,
            bool alphaTested) override
        {
            std::vector<ShaderMacro> macros;
            macros.emplace_back(
                "ALPHA_TESTED", alphaTested ? "1" : "0");
            return shaderFactory.CreateShader(
                "uvsr/sparse_virtual_shadow_map_sparse_depth.hlsl",
                "pixelMain",
                &macros,
                nvrhi::ShaderType::Pixel);
        }

        nvrhi::BindingLayoutHandle CreateInputBindingLayout() override
        {
            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility = nvrhi::ShaderType::Vertex;
            layoutDesc.registerSpace = GBUFFER_SPACE_INPUT;
            layoutDesc.registerSpaceIsDescriptorSet = true;
            layoutDesc.bindings = {
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    GBUFFER_BINDING_INSTANCE_BUFFER),
                nvrhi::BindingLayoutItem::RawBuffer_SRV(
                    GBUFFER_BINDING_VERTEX_BUFFER),
                nvrhi::BindingLayoutItem::PushConstants(
                    GBUFFER_BINDING_PUSH_CONSTANTS,
                    sizeof(SparseVirtualShadowMapPushConstants))
            };
            return m_Device->createBindingLayout(layoutDesc);
        }

        nvrhi::BindingSetHandle CreateInputBindingSet(
            const BufferGroup* bufferGroup) override
        {
            nvrhi::BindingSetDesc setDesc;
            setDesc.bindings = {
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    GBUFFER_BINDING_INSTANCE_BUFFER,
                    bufferGroup->instanceBuffer),
                nvrhi::BindingSetItem::RawBuffer_SRV(
                    GBUFFER_BINDING_VERTEX_BUFFER,
                    bufferGroup->vertexBuffer),
                nvrhi::BindingSetItem::PushConstants(
                    GBUFFER_BINDING_PUSH_CONSTANTS,
                    sizeof(SparseVirtualShadowMapPushConstants))
            };
            return m_Device->createBindingSet(
                setDesc, m_InputBindingLayout);
        }

        void CreateViewBindings(
            nvrhi::BindingLayoutHandle& layout,
            nvrhi::BindingSetHandle& set,
            const CreateParameters& params) override
        {
            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility =
                nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
            layoutDesc.registerSpace = GBUFFER_SPACE_VIEW;
            layoutDesc.registerSpaceIsDescriptorSet = true;
            layoutDesc.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    GBUFFER_BINDING_VIEW_CONSTANTS),
                nvrhi::BindingLayoutItem::Sampler(
                    GBUFFER_BINDING_MATERIAL_SAMPLER),
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(3),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(9),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
                nvrhi::BindingLayoutItem::Texture_SRV(11),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),
                nvrhi::BindingLayoutItem::Texture_UAV(0)
            };
            layout = m_Device->createBindingLayout(layoutDesc);
            m_TrackViewBindingLiveness = params.trackLiveness;
            set = CreateSparseViewBindingSet(params.trackLiveness);
        }

        nvrhi::GraphicsPipelineHandle CreateGraphicsPipelineForVertexShader(
            PipelineKey key,
            nvrhi::FramebufferInfo const& framebufferInfo,
            nvrhi::IShader* vertexShader)
        {
            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.inputLayout = nullptr;
            pipelineDesc.VS = vertexShader;
            pipelineDesc.PS = key.bits.alphaTested
                ? m_PixelShaderAlphaTested
                : m_PixelShader;
            pipelineDesc.renderState.rasterState
                .setFrontCounterClockwise(
                    key.bits.frontCounterClockwise)
                .setCullMode(key.bits.cullMode);
            if (key.bits.alphaTested)
                pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.renderState.depthStencilState.disableDepthTest();
            pipelineDesc.renderState.blendState.disableAlphaToCoverage();
            pipelineDesc.bindingLayouts = {
                m_MaterialBindings->GetLayout(),
                m_ViewBindingLayout,
                m_InputBindingLayout
            };
            return m_Device->createGraphicsPipeline(
                pipelineDesc, framebufferInfo);
        }

        nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(
            PipelineKey key,
            nvrhi::FramebufferInfo const& framebufferInfo) override
        {
            return CreateGraphicsPipelineForVertexShader(
                key, framebufferInfo, m_VertexShader);
        }

    public:
        SparseDepthPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<CommonRenderPasses>& commonPasses,
            nvrhi::ITexture* physicalDepth,
            nvrhi::ITexture* pageTable,
            nvrhi::IBuffer* compactRenderPages,
            nvrhi::IBuffer* renderPages,
            nvrhi::IBuffer* sparseConstants,
            nvrhi::IBuffer* counters,
            nvrhi::IBuffer* indirectDrawArguments,
            nvrhi::IBuffer* packetPageMetadata,
            nvrhi::IBuffer* packetPageRuntime,
            nvrhi::IBuffer* packetRenderPages,
            uint32_t physicalPageCount)
            : GBufferFillPass(device, commonPasses)
            , m_PhysicalDepth(physicalDepth)
            , m_PageTable(pageTable)
            , m_CompactRenderPages(compactRenderPages)
            , m_RenderPages(renderPages)
            , m_SparseConstants(sparseConstants)
            , m_Counters(counters)
            , m_IndirectDrawArguments(indirectDrawArguments)
            , m_PacketPageMetadata(packetPageMetadata)
            , m_PacketPageRuntime(packetPageRuntime)
            , m_PacketRenderPages(packetRenderPages)
            , m_PhysicalPageCount(physicalPageCount)
        {
            m_BatchedDrawSupported =
                HasExtendedCommandInfoSupport(device);
        }

        void Init(
            ShaderFactory& shaderFactory,
            const CreateParameters& parameters) override
        {
            GBufferFillPass::Init(shaderFactory, parameters);
            if (m_BatchedDrawSupported)
            {
                std::vector<ShaderMacro> macros;
                macros.emplace_back("SVSM_BATCHED_DRAW", "1");
                m_BatchedVertexShader = shaderFactory.CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_depth.hlsl",
                    "vertexMain",
                    &macros,
                    nvrhi::ShaderType::Vertex);
                m_BatchedDrawSupported =
                    bool(m_BatchedVertexShader);
            }
        }

        [[nodiscard]] bool SupportsBatchedDrawSubmission() const
        {
            return m_BatchedDrawSupported;
        }

        bool SetupMaterial(
            GeometryPassContext& abstractContext,
            const Material* material,
            nvrhi::RasterCullMode cullMode,
            nvrhi::GraphicsState& state) override
        {
            if (!m_BatchedPipelineActive)
            {
                return GBufferFillPass::SetupMaterial(
                    abstractContext, material, cullMode, state);
            }

            auto& context =
                static_cast<Context&>(abstractContext);
            PipelineKey key = context.keyTemplate;
            key.bits.cullMode = cullMode;
            switch (material->domain)
            {
            case MaterialDomain::Opaque:
            case MaterialDomain::AlphaBlended:
            case MaterialDomain::Transmissive:
            case MaterialDomain::TransmissiveAlphaTested:
            case MaterialDomain::TransmissiveAlphaBlended:
                key.bits.alphaTested = false;
                break;
            case MaterialDomain::AlphaTested:
                key.bits.alphaTested = true;
                break;
            default:
                return false;
            }

            nvrhi::IBindingSet* materialBindingSet =
                m_MaterialBindings->GetMaterialBindingSet(material);
            if (!materialBindingSet)
                return false;

            const nvrhi::FramebufferInfo& framebufferInfo =
                state.framebuffer->getFramebufferInfo();
            nvrhi::GraphicsPipelineHandle& pipeline =
                m_BatchedPipelines[key.value];
            if (!pipeline)
            {
                std::lock_guard<std::mutex> lockGuard(m_Mutex);
                if (!pipeline)
                {
                    pipeline = CreateGraphicsPipelineForVertexShader(
                        key,
                        framebufferInfo,
                        m_BatchedVertexShader);
                }
                if (!pipeline)
                    return false;
            }

            assert(pipeline->getFramebufferInfo() == framebufferInfo);
            state.pipeline = pipeline;
            state.bindings = {
                materialBindingSet,
                m_ViewBindings,
                context.inputBindingSet
            };
            return true;
        }

        void SetIndirectDrawArguments(nvrhi::IBuffer* indirectDrawArguments)
        {
            m_IndirectDrawArguments = indirectDrawArguments;
        }

        bool SetPacketPageBuffers(
            nvrhi::IBuffer* metadata,
            nvrhi::IBuffer* runtime,
            nvrhi::IBuffer* renderPages)
        {
            if (!metadata || !runtime || !renderPages)
                return false;
            nvrhi::IBuffer* previousMetadata =
                m_PacketPageMetadata;
            nvrhi::IBuffer* previousRuntime =
                m_PacketPageRuntime;
            nvrhi::IBuffer* previousRenderPages =
                m_PacketRenderPages;
            m_PacketPageMetadata = metadata;
            m_PacketPageRuntime = runtime;
            m_PacketRenderPages = renderPages;
            nvrhi::BindingSetHandle bindingSet =
                CreateSparseViewBindingSet(
                    m_TrackViewBindingLiveness);
            if (!bindingSet)
            {
                m_PacketPageMetadata = previousMetadata;
                m_PacketPageRuntime = previousRuntime;
                m_PacketRenderPages = previousRenderPages;
                return false;
            }
            m_ViewBindings = bindingSet;
            return true;
        }

        bool PrepareRenderPackets(
            const std::shared_ptr<SceneGraphNode>& rootNode,
            const std::array<std::shared_ptr<PlanarView>,
                SvsmClipmapCount>& views,
            uint64_t sceneStateHash,
            IDrawStrategy& drawStrategy,
            uint32_t firstClipmap,
            bool buildPacketPageMetadata,
            bool reserveExactPacketPageLists,
            bool dirtyPageScatterRaster,
            bool sortPacketsByState,
            bool allowReuse,
            bool& rebuilt)
        {
            rebuilt = false;
            reserveExactPacketPageLists =
                buildPacketPageMetadata && reserveExactPacketPageLists;
            if (dirtyPageScatterRaster !=
                    (buildPacketPageMetadata &&
                        !reserveExactPacketPageLists))
            {
                return false;
            }
            firstClipmap = std::min(
                firstClipmap, SvsmClipmapCount - 1u);
            bool matricesMatch = m_RenderPacketCacheValid;
            if (matricesMatch)
            {
                for (uint32_t level = firstClipmap;
                    level < SvsmClipmapCount;
                    ++level)
                {
                    const float4x4 matrix =
                        views[level]->GetViewProjectionMatrix(false);
                    if (std::memcmp(
                            &matrix,
                            &m_RenderPacketMatrices[level],
                            sizeof(matrix)) != 0)
                    {
                        matricesMatch = false;
                        break;
                    }
                }
            }

            if (allowReuse &&
                m_RenderPacketCacheValid &&
                m_RenderPacketRoot == rootNode &&
                m_RenderPacketSceneStateHash == sceneStateHash &&
                m_RenderPacketFirstClipmap == firstClipmap &&
                m_RenderPacketPageMetadataRequested ==
                    buildPacketPageMetadata &&
                m_RenderPacketExactPageListsRequested ==
                    reserveExactPacketPageLists &&
                m_RenderPacketDirtyPageScatterRasterRequested ==
                    dirtyPageScatterRaster &&
                m_RenderPacketStateSortingRequested ==
                    sortPacketsByState &&
                matricesMatch)
            {
                return true;
            }

            for (auto& packets : m_RenderPackets)
                packets.clear();
            for (auto& groups : m_RenderPacketGroups)
                groups.clear();
            m_BatchedRasterStateMask = 0u;
            m_RenderPacketRoot = rootNode;
            m_RenderPacketSceneStateHash = sceneStateHash;
            m_RenderPacketCount = 0u;
            m_RenderPacketPageEntryCount = 0u;
            m_RenderPacketFirstClipmap = firstClipmap;
            m_RenderPacketPageMetadataRequested =
                buildPacketPageMetadata;
            m_RenderPacketExactPageListsRequested =
                buildPacketPageMetadata && reserveExactPacketPageLists;
            m_RenderPacketDirtyPageScatterRasterRequested =
                dirtyPageScatterRaster;
            m_RenderPacketStateSortingRequested =
                sortPacketsByState;
            m_RenderPacketPageMetadataSupported =
                buildPacketPageMetadata;
            m_RenderPacketPageDispatchSupported =
                buildPacketPageMetadata;
            m_RenderPacketCacheValid = false;

            constexpr uint32_t maximumPacketCount =
                std::numeric_limits<uint32_t>::max() /
                uint32_t(sizeof(
                    nvrhi::DrawIndexedIndirectArguments));
            for (uint32_t level = 0u;
                level < SvsmClipmapCount;
                ++level)
            {
                m_RenderPacketOffsets[level] =
                    m_RenderPacketCount;
                m_RenderPacketMatrices[level] =
                    views[level]->GetViewProjectionMatrix(false);
                // Resolution bias never renders finer levels. Omitting their
                // packet enumeration keeps the bias consistent with culling
                // and prevents unused metadata from consuming the fixed list
                // budget.
                if (!ShouldPrepareSvsmRenderPacketsForClipmap(
                        level, firstClipmap))
                    continue;
                std::vector<RenderPacket>& levelPackets =
                    m_RenderPackets[level];
                std::vector<RenderPacketGroup>& levelGroups =
                    m_RenderPacketGroups[level];
                drawStrategy.PrepareForView(
                    rootNode, *views[level]);
                while (const DrawItem* item =
                        drawStrategy.GetNextItem())
                {
                    if (!item->instance ||
                        !item->mesh ||
                        !item->geometry ||
                        !item->material ||
                        !item->buffers)
                    {
                        continue;
                    }

                    const std::shared_ptr<Material> material =
                        item->geometry->material;
                    const std::shared_ptr<BufferGroup> buffers =
                        item->mesh->buffers;
                    if (!material ||
                        !buffers ||
                        material.get() != item->material ||
                        buffers.get() != item->buffers ||
                        item->instance->GetInstanceIndex() < 0)
                    {
                        continue;
                    }
                    if (m_RenderPacketCount >=
                        maximumPacketCount)
                    {
                        log::error(
                            "SVSM render packet count exceeds the 32-bit indirect argument range.");
                        for (auto& packets : m_RenderPackets)
                            packets.clear();
                        for (auto& groups : m_RenderPacketGroups)
                            groups.clear();
                        m_RenderPacketCount = 0u;
                        return false;
                    }

                    const uint64_t vertexOffset =
                        uint64_t(item->mesh->vertexOffset) +
                        item->geometry->vertexOffsetInMesh;
                    const uint64_t indexOffset =
                        uint64_t(item->mesh->indexOffset) +
                        item->geometry->indexOffsetInMesh;
                    if (vertexOffset >
                            std::numeric_limits<uint32_t>::max() ||
                        indexOffset >
                            std::numeric_limits<uint32_t>::max())
                    {
                        log::error(
                            "SVSM caster packet offsets exceed the 32-bit indexed draw range.");
                        for (auto& packets : m_RenderPackets)
                            packets.clear();
                        for (auto& groups : m_RenderPacketGroups)
                            groups.clear();
                        m_RenderPacketCount = 0u;
                        return false;
                    }

                    RenderPacket packet;
                    packet.material = material;
                    packet.buffers = buffers;
                    packet.cullMode = item->cullMode;
                    const bool alphaTested =
                        material->domain == MaterialDomain::AlphaTested;
                    packet.stateKey = MakeSvsmBatchedDrawStateKey(
                        reinterpret_cast<uintptr_t>(buffers.get()),
                        reinterpret_cast<uintptr_t>(material.get()),
                        uint32_t(item->cullMode),
                        alphaTested);
                    packet.arguments.vertexCount =
                        item->geometry->numIndices;
                    packet.arguments.instanceCount = 1u;
                    packet.arguments.startVertexLocation =
                        uint32_t(vertexOffset);
                    packet.arguments.startIndexLocation =
                        uint32_t(indexOffset);
                    packet.arguments.startInstanceLocation =
                        uint32_t(
                            item->instance->GetInstanceIndex());
                    packet.argumentIndex =
                        m_RenderPacketCount++;
                    if (buildPacketPageMetadata)
                    {
                        uint32_t packetPageCapacity = 0u;
                        const uint32_t pageListOffset =
                            reserveExactPacketPageLists
                                ? m_RenderPacketPageEntryCount
                                : 0u;
                        packet.pageMetadata =
                            BuildPacketPageMetadata(
                                *item,
                                m_RenderPacketMatrices[level],
                                pageListOffset,
                                packetPageCapacity);
                        packetPageCapacity = std::min(
                            packetPageCapacity,
                            m_PhysicalPageCount);
                        if (reserveExactPacketPageLists &&
                            (!m_RenderPacketPageMetadataSupported ||
                            packetPageCapacity >
                                MaximumPacketRenderPageEntries -
                                    m_RenderPacketPageEntryCount))
                        {
                            m_RenderPacketPageMetadataSupported = false;
                            packet.pageMetadata.packedMinimumPage =
                                SvsmInvalidPacketPageBounds;
                            packet.pageMetadata.packedMaximumPage =
                                SvsmInvalidPacketPageBounds;
                        }
                        else if (reserveExactPacketPageLists)
                        {
                            m_RenderPacketPageEntryCount +=
                                packetPageCapacity;
                        }
                    }
                    const bool batchable =
                        CanEncodeSvsmBatchedDraw(
                            packet.arguments.startVertexLocation,
                            packet.arguments.startInstanceLocation,
                            m_PhysicalPageCount);
                    packet.batchable = batchable;
                    if (batchable)
                    {
                        const uint32_t rasterState =
                            uint32_t(item->cullMode) |
                            (alphaTested ? 1u << 2u : 0u);
                        if (rasterState < 32u)
                            m_BatchedRasterStateMask |= 1u << rasterState;
                    }
                    const uint32_t packetIndex =
                        uint32_t(levelPackets.size());
                    const bool extendLastGroup =
                        !levelGroups.empty() &&
                        CanMergeSvsmPacketStateGroup(
                            levelPackets[
                                levelGroups.back().firstPacket].stateKey,
                            levelGroups.back().batchable,
                            packet.stateKey,
                            packet.batchable);
                    levelPackets.push_back(std::move(packet));
                    if (extendLastGroup)
                    {
                        ++levelGroups.back().packetCount;
                    }
                    else
                    {
                        levelGroups.push_back({
                            packetIndex,
                            1u,
                            batchable
                        });
                    }
                }
                if (sortPacketsByState)
                {
                    std::stable_sort(
                        levelPackets.begin(),
                        levelPackets.end(),
                        [](const RenderPacket& left,
                           const RenderPacket& right) {
                            const SvsmPacketStateSortKey leftKey =
                                MakeSvsmPacketStateSortKey(
                                    left.stateKey,
                                    reinterpret_cast<uintptr_t>(
                                        left.material.get()),
                                    left.batchable);
                            const SvsmPacketStateSortKey rightKey =
                                MakeSvsmPacketStateSortKey(
                                    right.stateKey,
                                    reinterpret_cast<uintptr_t>(
                                        right.material.get()),
                                    right.batchable);
                            return IsSvsmPacketStateSortKeyLess(
                                leftKey, rightKey);
                        });

                    levelGroups.clear();
                    for (uint32_t packetIndex = 0u;
                        packetIndex < uint32_t(levelPackets.size());
                        ++packetIndex)
                    {
                        RenderPacket& packet =
                            levelPackets[packetIndex];
                        packet.argumentIndex =
                            m_RenderPacketOffsets[level] + packetIndex;
                        const bool extendLastGroup =
                            !levelGroups.empty() &&
                            CanMergeSvsmPacketStateGroup(
                                levelPackets[
                                    levelGroups.back().firstPacket].stateKey,
                                levelGroups.back().batchable,
                                packet.stateKey,
                                packet.batchable);
                        if (extendLastGroup)
                        {
                            ++levelGroups.back().packetCount;
                        }
                        else
                        {
                            levelGroups.push_back({
                                packetIndex,
                                1u,
                                packet.batchable
                            });
                        }
                    }
                    assert(
                        uint64_t(m_RenderPacketOffsets[level]) +
                            levelPackets.size() ==
                        m_RenderPacketCount);
                }
                if (buildPacketPageMetadata &&
                    !CanDispatchSvsmPacketPageCulling(
                        uint32_t(m_RenderPackets[level].size()),
                        dirtyPageScatterRaster))
                {
                    m_RenderPacketPageDispatchSupported = false;
                }
            }

            m_RenderPacketCacheValid = true;
            rebuilt = true;
            return true;
        }

        bool PrepareBatchedPipelines(
            nvrhi::IFramebuffer* framebuffer,
            const std::array<std::shared_ptr<PlanarView>,
                SvsmClipmapCount>& views)
        {
            if (!m_BatchedDrawSupported ||
                !m_BatchedVertexShader ||
                !framebuffer)
            {
                return false;
            }

            std::array<bool, PipelineKey::Count> requiredKeys{};
            for (uint32_t level = 0u;
                level < SvsmClipmapCount;
                ++level)
            {
                if (!views[level])
                {
                    m_BatchedDrawSupported = false;
                    return false;
                }
                for (uint32_t rasterState = 0u;
                    rasterState < 8u;
                    ++rasterState)
                {
                    if ((m_BatchedRasterStateMask &
                            (1u << rasterState)) == 0u)
                    {
                        continue;
                    }
                    PipelineKey key;
                    key.value = 0u;
                    key.bits.cullMode = nvrhi::RasterCullMode(
                        rasterState & 3u);
                    key.bits.alphaTested =
                        (rasterState & (1u << 2u)) != 0u;
                    key.bits.frontCounterClockwise =
                        views[level]->IsMirrored();
                    key.bits.reverseDepth =
                        views[level]->IsReverseDepth();
                    requiredKeys[key.value] = true;
                }
            }

            const nvrhi::FramebufferInfo& framebufferInfo =
                framebuffer->getFramebufferInfo();
            for (uint32_t keyValue = 0u;
                keyValue < PipelineKey::Count;
                ++keyValue)
            {
                if (!requiredKeys[keyValue])
                    continue;
                nvrhi::GraphicsPipelineHandle& pipeline =
                    m_BatchedPipelines[keyValue];
                if (!pipeline)
                {
                    PipelineKey key;
                    key.value = keyValue;
                    pipeline = CreateGraphicsPipelineForVertexShader(
                        key,
                        framebufferInfo,
                        m_BatchedVertexShader);
                }
                if (!pipeline)
                {
                    log::warning(
                        "SVSM batched draw pipeline creation failed; retaining the per-packet reference path.");
                    m_BatchedDrawSupported = false;
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] uint32_t GetRenderPacketCount() const
        {
            return m_RenderPacketCount;
        }

        [[nodiscard]] uint32_t GetRenderPacketOffset(
            uint32_t level) const
        {
            return m_RenderPacketOffsets[
                std::min(level, SvsmClipmapCount - 1u)];
        }

        [[nodiscard]] uint32_t GetRenderPacketCount(
            uint32_t level) const
        {
            return uint32_t(m_RenderPackets[
                std::min(level, SvsmClipmapCount - 1u)].size());
        }

        [[nodiscard]] bool SupportsPacketPageCulling() const
        {
            return m_RenderPacketCacheValid &&
                m_RenderPacketPageMetadataRequested &&
                m_RenderPacketPageMetadataSupported &&
                m_RenderPacketPageDispatchSupported;
        }

        [[nodiscard]] bool HasRenderPacketCache() const
        {
            return m_RenderPacketCacheValid;
        }

        [[nodiscard]] bool UsesExactPacketPageLists() const
        {
            return m_RenderPacketExactPageListsRequested;
        }

        [[nodiscard]] uint32_t GetPacketPageEntryCount() const
        {
            return m_RenderPacketPageEntryCount;
        }

        [[nodiscard]] bool GetPacketPageMetadata(
            std::vector<SparseVirtualShadowMapPacketMetadata>& metadata) const
        {
            metadata.assign(m_RenderPacketCount, {});
            if (!SupportsPacketPageCulling())
            {
                return false;
            }
            for (const auto& levelPackets : m_RenderPackets)
            {
                for (const RenderPacket& packet : levelPackets)
                {
                    if (packet.argumentIndex >= metadata.size())
                        return false;
                    metadata[packet.argumentIndex] =
                        packet.pageMetadata;
                }
            }
            return true;
        }

        void BuildIndirectArguments(
            std::vector<nvrhi::DrawIndexedIndirectArguments>&
                arguments,
            bool batched,
            bool packetPageCulling) const
        {
            arguments.assign(m_RenderPacketCount, {});
            for (const auto& levelPackets : m_RenderPackets)
            {
                for (const RenderPacket& packet : levelPackets)
                {
                    nvrhi::DrawIndexedIndirectArguments& output =
                        arguments[packet.argumentIndex];
                    output.indexCount =
                        packet.arguments.vertexCount;
                    output.instanceCount = 0u;
                    output.startIndexLocation =
                        packet.arguments.startIndexLocation;
                    const bool encodePacket =
                        batched &&
                        CanEncodeSvsmBatchedDraw(
                            packet.arguments.startVertexLocation,
                            packet.arguments.startInstanceLocation,
                            m_PhysicalPageCount);
                    output.baseVertexLocation = encodePacket
                        ? EncodeSvsmBatchedBaseVertex(
                            packet.arguments.startVertexLocation)
                        : 0;
                    output.startInstanceLocation = encodePacket
                        ? (packetPageCulling
                            ? packet.argumentIndex
                            : EncodeSvsmBatchedStartInstance(
                                packet.arguments.startInstanceLocation,
                                m_PhysicalPageCount))
                        : 0u;
                }
            }
        }

        void SetSparsePushConstants(
            GeometryPassContext& abstractContext,
            nvrhi::ICommandList* commandList,
            nvrhi::DrawArguments& args,
            bool packetPageCulling,
            bool dirtyPageScatterRaster,
            uint32_t packetIndex)
        {
            auto& context = static_cast<Context&>(abstractContext);
            SparseVirtualShadowMapPushConstants constants = {};
            constants.startInstanceLocation =
                args.startInstanceLocation;
            constants.startVertexLocation =
                args.startVertexLocation;
            constants.positionOffset = context.positionOffset;
            constants.texCoordOffset = context.texCoordOffset;
            constants.originalInstanceCount = args.instanceCount;
            constants.physicalPageCount = m_PhysicalPageCount;
            constants.flags =
                (packetPageCulling
                    ? SVSM_SPARSE_DEPTH_FLAG_PACKET_PAGE_CULLING
                    : 0u) |
                (dirtyPageScatterRaster
                    ? SVSM_SPARSE_DEPTH_FLAG_DIRTY_PAGE_SCATTER_RASTER
                    : 0u);
            constants.packetIndex = packetIndex;
            commandList->setPushConstants(
                &constants, sizeof(constants));

            args.startInstanceLocation = 0u;
            args.startVertexLocation = 0u;
        }

        void SetPushConstants(
            GeometryPassContext& abstractContext,
            nvrhi::ICommandList* commandList,
            nvrhi::GraphicsState&,
            nvrhi::DrawArguments& args) override
        {
            SetSparsePushConstants(
                abstractContext,
                commandList,
                args,
                false,
                false,
                0u);
        }

        void SetBatchedPushConstants(
            const Context& context,
            nvrhi::ICommandList* commandList,
            bool packetPageCulling,
            bool dirtyPageScatterRaster)
        {
            SparseVirtualShadowMapPushConstants constants = {};
            constants.positionOffset = context.positionOffset;
            constants.texCoordOffset = context.texCoordOffset;
            constants.physicalPageCount = m_PhysicalPageCount;
            constants.flags =
                SVSM_SPARSE_DEPTH_FLAG_BATCHED_DRAW |
                (packetPageCulling
                    ? SVSM_SPARSE_DEPTH_FLAG_PACKET_PAGE_CULLING
                    : 0u) |
                (dirtyPageScatterRaster
                    ? SVSM_SPARSE_DEPTH_FLAG_DIRTY_PAGE_SCATTER_RASTER
                    : 0u);
            commandList->setPushConstants(
                &constants, sizeof(constants));
        }

        void RenderViewReference(
            nvrhi::ICommandList* commandList,
            const IView* view,
            nvrhi::IFramebuffer* framebuffer,
            IDrawStrategy& drawStrategy,
            Context& context,
            uint32_t selectedClipmap)
        {
            m_BatchedPipelineActive = false;
            SetupView(context, commandList, view, view);

            const Material* lastMaterial = nullptr;
            const BufferGroup* lastBuffers = nullptr;
            nvrhi::RasterCullMode lastCullMode =
                nvrhi::RasterCullMode::Back;
            bool drawMaterial = true;

            nvrhi::GraphicsState graphicsState;
            graphicsState.framebuffer = framebuffer;
            graphicsState.viewport = view->GetViewportState();
            graphicsState.shadingRateState =
                view->GetVariableRateShadingState();
            graphicsState.indirectParams =
                m_IndirectDrawArguments;

            while (const DrawItem* item = drawStrategy.GetNextItem())
            {
                if (!item->material)
                    continue;

                const bool newBuffers =
                    item->buffers != lastBuffers;
                const bool newBindings =
                    newBuffers ||
                    item->material != lastMaterial ||
                    item->cullMode != lastCullMode;
                if (newBuffers)
                {
                    SetupInputBuffers(
                        context, item->buffers, graphicsState);
                    lastBuffers = item->buffers;
                }
                if (newBindings)
                {
                    drawMaterial = SetupMaterial(
                        context,
                        item->material,
                        item->cullMode,
                        graphicsState);
                    lastMaterial = item->material;
                    lastCullMode = item->cullMode;
                }
                if (!drawMaterial)
                    continue;

                nvrhi::DrawArguments args;
                args.vertexCount = item->geometry->numIndices;
                args.instanceCount = 1u;
                args.startVertexLocation =
                    item->mesh->vertexOffset +
                    item->geometry->vertexOffsetInMesh;
                args.startIndexLocation =
                    item->mesh->indexOffset +
                    item->geometry->indexOffsetInMesh;
                args.startInstanceLocation =
                    item->instance->GetInstanceIndex();

                nvrhi::DrawIndexedIndirectArguments indirectArgs;
                indirectArgs.indexCount = args.vertexCount;
                indirectArgs.instanceCount = 0u;
                indirectArgs.startIndexLocation =
                    args.startIndexLocation;
                indirectArgs.baseVertexLocation = 0;
                indirectArgs.startInstanceLocation = 0u;
                commandList->writeBuffer(
                    m_IndirectDrawArguments,
                    &indirectArgs,
                    sizeof(indirectArgs));
                commandList->copyBuffer(
                    m_IndirectDrawArguments,
                    offsetof(
                        nvrhi::DrawIndexedIndirectArguments,
                        instanceCount),
                    m_Counters,
                    uint64_t(c_DebugCounterCount +
                        selectedClipmap) *
                        sizeof(uint32_t),
                    sizeof(uint32_t));
                commandList->setBufferState(
                    m_IndirectDrawArguments,
                    nvrhi::ResourceStates::IndirectArgument);
                commandList->commitBarriers();
                commandList->setGraphicsState(graphicsState);
                SetPushConstants(
                    context, commandList, graphicsState, args);
                commandList->drawIndexedIndirect(0u, 1u);
            }
        }

        void RenderPackets(
            nvrhi::ICommandList* commandList,
            const IView* view,
            nvrhi::IFramebuffer* framebuffer,
            Context& context,
            uint32_t selectedClipmap,
            uint32_t indirectDrawCapacity,
            bool gpuGated,
            bool batched,
            bool levelEmptyWorkSkip,
            bool packetPageCulling,
            bool dirtyPageScatterRaster)
        {
            m_BatchedPipelineActive = false;
            selectedClipmap = std::min(
                selectedClipmap, SvsmClipmapCount - 1u);
            SetupView(context, commandList, view, view);

            const Material* lastMaterial = nullptr;
            const BufferGroup* lastBuffers = nullptr;
            nvrhi::RasterCullMode lastCullMode =
                nvrhi::RasterCullMode::Back;
            bool drawMaterial = true;
            bool stateValid = false;

            nvrhi::GraphicsState graphicsState;
            graphicsState.framebuffer = framebuffer;
            graphicsState.viewport = view->GetViewportState();
            graphicsState.shadingRateState =
                view->GetVariableRateShadingState();
            graphicsState.indirectParams =
                m_IndirectDrawArguments;
            if (gpuGated)
                graphicsState.indirectCountBuffer = m_Counters;

            const auto& packets = m_RenderPackets[selectedClipmap];
            if (gpuGated && batched)
            {
                const auto& groups =
                    m_RenderPacketGroups[selectedClipmap];
                for (const RenderPacketGroup& group : groups)
                {
                    if (group.packetCount == 0u ||
                        group.firstPacket >= packets.size() ||
                        group.packetCount >
                            packets.size() - group.firstPacket)
                    {
                        continue;
                    }

                    const RenderPacket& firstPacket =
                        packets[group.firstPacket];
                    const uint64_t groupArgumentEnd =
                        uint64_t(firstPacket.argumentIndex) +
                        uint64_t(group.packetCount);
                    if (groupArgumentEnd >
                            uint64_t(m_RenderPacketCount) ||
                        groupArgumentEnd >
                            uint64_t(indirectDrawCapacity))
                    {
                        assert(false &&
                            "SVSM packet group exceeds indirect argument bounds");
                        continue;
                    }
#ifndef NDEBUG
                    for (uint32_t packetOffset = 0u;
                        packetOffset < group.packetCount;
                        ++packetOffset)
                    {
                        assert(
                            uint64_t(packets[
                                group.firstPacket + packetOffset]
                                .argumentIndex) ==
                            uint64_t(firstPacket.argumentIndex) +
                                uint64_t(packetOffset));
                    }
#endif
                    const bool pipelineChanged =
                        m_BatchedPipelineActive != group.batchable;
                    m_BatchedPipelineActive = group.batchable;
                    const bool newBuffers =
                        firstPacket.buffers.get() != lastBuffers;
                    const bool newBindings =
                        pipelineChanged || newBuffers ||
                        firstPacket.material.get() != lastMaterial ||
                        firstPacket.cullMode != lastCullMode;
                    if (newBuffers)
                    {
                        SetupInputBuffers(
                            context,
                            firstPacket.buffers.get(),
                            graphicsState);
                        lastBuffers = firstPacket.buffers.get();
                        stateValid = false;
                    }
                    if (newBindings)
                    {
                        drawMaterial = SetupMaterial(
                            context,
                            firstPacket.material.get(),
                            firstPacket.cullMode,
                            graphicsState);
                        lastMaterial = firstPacket.material.get();
                        lastCullMode = firstPacket.cullMode;
                        stateValid = false;
                    }
                    if (!drawMaterial)
                        continue;

                    if (!stateValid)
                    {
                        commandList->setGraphicsState(graphicsState);
                        stateValid = true;
                    }

                    if (group.batchable)
                    {
                        SetBatchedPushConstants(
                            context,
                            commandList,
                            packetPageCulling,
                            dirtyPageScatterRaster);
                        const uint32_t argumentOffset =
                            firstPacket.argumentIndex *
                            uint32_t(sizeof(
                                nvrhi::DrawIndexedIndirectArguments));
                        if (levelEmptyWorkSkip)
                        {
                            commandList->drawIndexedIndirectCount(
                                argumentOffset,
                                (c_LevelHasWorkCounterBase +
                                    selectedClipmap) *
                                    uint32_t(sizeof(uint32_t)),
                                group.packetCount);
                        }
                        else
                        {
                            commandList->drawIndexedIndirect(
                                argumentOffset,
                                group.packetCount);
                        }
                    }
                    else
                    {
                        for (uint32_t packetOffset = 0u;
                            packetOffset < group.packetCount;
                            ++packetOffset)
                        {
                            const RenderPacket& packet =
                                packets[
                                    group.firstPacket + packetOffset];
                            nvrhi::DrawArguments args =
                                packet.arguments;
                            SetSparsePushConstants(
                                context,
                                commandList,
                                args,
                                packetPageCulling,
                                dirtyPageScatterRaster,
                                packet.argumentIndex);
                            commandList->drawIndexedIndirectCount(
                                packet.argumentIndex *
                                    uint32_t(sizeof(
                                        nvrhi::DrawIndexedIndirectArguments)),
                                (c_DebugCounterCount +
                                    selectedClipmap) *
                                    uint32_t(sizeof(uint32_t)),
                                1u);
                        }
                    }
                }
                return;
            }

            for (const RenderPacket& packet : packets)
            {
                const bool newBuffers =
                    packet.buffers.get() != lastBuffers;
                const bool newBindings =
                    newBuffers ||
                    packet.material.get() != lastMaterial ||
                    packet.cullMode != lastCullMode;
                if (newBuffers)
                {
                    SetupInputBuffers(
                        context,
                        packet.buffers.get(),
                        graphicsState);
                    lastBuffers = packet.buffers.get();
                    stateValid = false;
                }
                if (newBindings)
                {
                    drawMaterial = SetupMaterial(
                        context,
                        packet.material.get(),
                        packet.cullMode,
                        graphicsState);
                    lastMaterial = packet.material.get();
                    lastCullMode = packet.cullMode;
                    stateValid = false;
                }
                if (!drawMaterial)
                    continue;

                nvrhi::DrawArguments args = packet.arguments;
                if (!gpuGated)
                {
                    nvrhi::DrawIndexedIndirectArguments
                        indirectArgs;
                    indirectArgs.indexCount = args.vertexCount;
                    indirectArgs.instanceCount = 0u;
                    indirectArgs.startIndexLocation =
                        args.startIndexLocation;
                    indirectArgs.baseVertexLocation = 0;
                    indirectArgs.startInstanceLocation = 0u;
                    commandList->writeBuffer(
                        m_IndirectDrawArguments,
                        &indirectArgs,
                        sizeof(indirectArgs));
                    commandList->copyBuffer(
                        m_IndirectDrawArguments,
                        offsetof(
                            nvrhi::DrawIndexedIndirectArguments,
                            instanceCount),
                        m_Counters,
                        uint64_t(
                            c_DebugCounterCount +
                            selectedClipmap) *
                            sizeof(uint32_t),
                        sizeof(uint32_t));
                    commandList->setBufferState(
                        m_IndirectDrawArguments,
                        nvrhi::ResourceStates::IndirectArgument);
                    commandList->commitBarriers();
                    stateValid = false;
                }

                if (!stateValid)
                {
                    commandList->setGraphicsState(graphicsState);
                    stateValid = true;
                }
                SetSparsePushConstants(
                    context,
                    commandList,
                    args,
                    packetPageCulling,
                    dirtyPageScatterRaster,
                    packet.argumentIndex);
                if (gpuGated)
                {
                    commandList->drawIndexedIndirectCount(
                        packet.argumentIndex *
                            uint32_t(sizeof(
                                nvrhi::DrawIndexedIndirectArguments)),
                        (c_DebugCounterCount +
                            selectedClipmap) *
                            uint32_t(sizeof(uint32_t)),
                        1u);
                }
                else
                {
                    commandList->drawIndexedIndirect(0u, 1u);
                }
            }
        }
    };

    SparseVirtualShadowMapPass::SparseVirtualShadowMapPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses)
        : m_Device(device)
        , m_ShaderFactory(shaderFactory)
        , m_CommonPasses(commonPasses)
    {
        m_Timings.supported = HasRequiredFormatSupport(device);
        if (!m_Timings.supported)
        {
            log::error(
                "SVSM requires R32_UINT texture atomics and R8_UNORM texture, load, sample, and UAV-store support.");
            return;
        }

        // Both depth backends write through a pixel-shader UAV and do not
        // produce an output-merger color or depth value. An attachmentless
        // framebuffer is therefore the exact raster contract and avoids a
        // redundant 8192-square coverage texture.
        m_RasterFramebuffer =
            device->createFramebuffer(nvrhi::FramebufferDesc{});
        if (!m_RasterFramebuffer)
        {
            m_Timings.supported = false;
            log::error(
                "SVSM could not create its attachmentless raster framebuffer.");
            return;
        }

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1)
        };
        m_ResolveBindingLayout =
            device->createBindingLayout(layoutDesc);

        nvrhi::BufferDesc constantsDesc;
        constantsDesc.byteSize =
            sizeof(SparseVirtualShadowMapResolveConstants);
        constantsDesc.debugName = "SVSM Resolve Constants";
        constantsDesc.isConstantBuffer = true;
        constantsDesc.isVolatile = true;
        constantsDesc.maxVersions =
            engine::c_MaxRenderPassConstantBufferVersions;
        m_ResolveConstants = device->createBuffer(constantsDesc);

        m_ResolveShader = shaderFactory->CreateShader(
            "uvsr/sparse_virtual_shadow_map_resolve_cs.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Compute);
        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.CS = m_ResolveShader;
        pipelineDesc.bindingLayouts = { m_ResolveBindingLayout };
        m_ResolvePipeline = device->createComputePipeline(pipelineDesc);

        nvrhi::BindingLayoutDesc sparseLayoutDesc;
        sparseLayoutDesc.visibility = nvrhi::ShaderType::Compute;
        sparseLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(4),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(5),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(6),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(7),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(8),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(9)
        };
        m_SparseBindingLayout =
            device->createBindingLayout(sparseLayoutDesc);

        static const char* sparseEntries[] = {
            "prepare",
            "mark",
            "recycle",
            "allocate",
            "clearPages",
            "finalize",
            "stats",
            "fillIndirect"
        };
        for (uint32_t stage = 0u;
            stage < m_SparseShaders.size();
            ++stage)
        {
            m_SparseShaders[stage] = shaderFactory->CreateShader(
                "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                sparseEntries[stage],
                nullptr,
                nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS = m_SparseShaders[stage];
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparsePipelines[stage] =
                device->createComputePipeline(sparsePipelineDesc);
        }

        nvrhi::BindingLayoutDesc sparseResolveLayoutDesc;
        sparseResolveLayoutDesc.visibility =
            nvrhi::ShaderType::Compute;
        sparseResolveLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::Texture_UAV(1),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2)
        };
        m_SparseResolveBindingLayout =
            device->createBindingLayout(sparseResolveLayoutDesc);
        static constexpr const char* c_SparseResolveTapMacros[] = {
            "1",
            "4",
            "8",
            "16"
        };
        for (uint32_t translationCache = 0u;
            translationCache <
                c_SparseResolveTranslationPermutationCount;
            ++translationCache)
        {
            for (uint32_t tapPermutation = 0u;
                tapPermutation <
                    c_SparseResolveTapPermutationCount;
                ++tapPermutation)
            {
                const uint32_t permutation =
                    translationCache *
                        c_SparseResolveTapPermutationCount +
                    tapPermutation;
                std::vector<ShaderMacro> macros;
                macros.emplace_back(
                    "SVSM_PAGE_TRANSLATION_CACHE",
                    translationCache != 0u ? "1" : "0");
                macros.emplace_back(
                    "SVSM_FILTER_TAPS",
                    c_SparseResolveTapMacros[tapPermutation]);
                m_SparseResolveShaders[permutation] =
                    shaderFactory->CreateShader(
                        "uvsr/sparse_virtual_shadow_map_sparse_resolve_cs.hlsl",
                        "main",
                        &macros,
                        nvrhi::ShaderType::Compute);
                nvrhi::ComputePipelineDesc sparseResolvePipelineDesc;
                sparseResolvePipelineDesc.CS =
                    m_SparseResolveShaders[permutation];
                sparseResolvePipelineDesc.bindingLayouts = {
                    m_SparseResolveBindingLayout
                };
                m_SparseResolvePipelines[permutation] =
                    device->createComputePipeline(
                        sparseResolvePipelineDesc);
            }
        }

        nvrhi::BindingLayoutDesc debugLayoutDesc;
        debugLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
        debugLayoutDesc.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0)
        };
        m_DebugBindingLayout =
            device->createBindingLayout(debugLayoutDesc);
        m_DebugPixelShader = shaderFactory->CreateShader(
            "uvsr/sparse_virtual_shadow_map_debug_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);

        for (auto& stageQueries : m_TimerQueries)
        {
            for (nvrhi::TimerQueryHandle& query : stageQueries)
                query = device->createTimerQuery();
        }
    }

    SparseVirtualShadowMapPass::~SparseVirtualShadowMapPass() = default;

    bool SparseVirtualShadowMapPass::EnsureDenseResources(
        nvrhi::ITexture* cameraDepth)
    {
        if (!cameraDepth || !m_Timings.supported)
            return false;

        const nvrhi::TextureDesc& cameraDesc = cameraDepth->getDesc();
        const bool recreate =
            RequiresSvsmResourceRecreation(
                m_ResourceBackend,
                SvsmResourceBackend::Dense) ||
            !m_DenseDepth ||
            !m_Visibility ||
            m_Visibility->getDesc().width != cameraDesc.width ||
            m_Visibility->getDesc().height != cameraDesc.height;
        if (!recreate &&
            m_ResolveBindingSet &&
            m_BoundCameraDepth == cameraDepth)
        {
            return true;
        }

        m_ResolveBindingSet = nullptr;
        if (recreate)
        {
            InvalidateUiTimings();
            InvalidateDebugCounters();
            m_ResourceBackend = SvsmResourceBackend::None;
            m_DenseDepthPass.reset();
            m_DenseDepth = nullptr;
            m_Visibility = nullptr;
            m_SparseVisibilityCache = {};
            m_SparseResolveBindingSets = {};
            m_DebugVisualization = nullptr;
            m_DebugBindingSet = nullptr;

            nvrhi::TextureDesc depthDesc;
            depthDesc.width = SvsmVirtualResolution;
            depthDesc.height = SvsmVirtualResolution;
            depthDesc.arraySize = SvsmClipmapCount;
            depthDesc.format = nvrhi::Format::R32_UINT;
            depthDesc.dimension =
                nvrhi::TextureDimension::Texture2DArray;
            depthDesc.isUAV = true;
            depthDesc.debugName = "SVSM Dense Atomic Depth";
            depthDesc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            m_DenseDepth = m_Device->createTexture(depthDesc);

            nvrhi::TextureDesc visibilityDesc;
            visibilityDesc.width = cameraDesc.width;
            visibilityDesc.height = cameraDesc.height;
            visibilityDesc.format = nvrhi::Format::R8_UNORM;
            visibilityDesc.dimension =
                nvrhi::TextureDimension::Texture2D;
            visibilityDesc.isUAV = true;
            visibilityDesc.debugName =
                "SVSM Full-Resolution Visibility";
            visibilityDesc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            m_Visibility = m_Device->createTexture(visibilityDesc);
            visibilityDesc.debugName = "SVSM Debug Visualization";
            m_DebugVisualization =
                m_Device->createTexture(visibilityDesc);

            if (!m_DenseDepth ||
                !m_Visibility ||
                !m_DebugVisualization)
            {
                log::error(
                    "SVSM could not allocate the explicit dense reference resources.");
                m_DenseDepth = nullptr;
                m_Visibility = nullptr;
                m_DebugVisualization = nullptr;
                return false;
            }

            m_DenseDepthPass = std::make_unique<DenseDepthPass>(
                m_Device, m_CommonPasses, m_DenseDepth);
            GBufferFillPass::CreateParameters depthParameters;
            depthParameters.enableDepthWrite = false;
            depthParameters.enableMotionVectors = false;
            depthParameters.trackLiveness = false;
            m_DenseDepthPass->Init(
                *m_ShaderFactory, depthParameters);

            for (uint32_t level = 0u;
                level < SvsmClipmapCount;
                ++level)
            {
                if (!m_ClipmapViews[level])
                {
                    m_ClipmapViews[level] =
                        std::make_shared<PlanarView>();
                }
                m_ClipmapViews[level]->SetViewport(
                    nvrhi::Viewport(
                        float(SvsmVirtualResolution),
                        float(SvsmVirtualResolution)));
                m_ClipmapViews[level]->SetArraySlice(0u);
            }
        }

        nvrhi::BindingSetDesc resolveSetDesc;
        resolveSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(
                0, m_ResolveConstants),
            nvrhi::BindingSetItem::Texture_SRV(0, cameraDepth),
            nvrhi::BindingSetItem::Texture_SRV(1, m_DenseDepth),
            nvrhi::BindingSetItem::Texture_UAV(0, m_Visibility),
            nvrhi::BindingSetItem::Texture_UAV(
                1, m_DebugVisualization)
        };
        m_ResolveBindingSet = m_Device->createBindingSet(
            resolveSetDesc, m_ResolveBindingLayout);
        m_BoundCameraDepth = cameraDepth;

        if (recreate)
        {
            m_Timings.physicalDepthBytes = TextureByteSize(
                SvsmVirtualResolution,
                SvsmVirtualResolution,
                SvsmClipmapCount,
                sizeof(uint32_t));
            m_Timings.visibilityBytes = TextureByteSize(
                cameraDesc.width, cameraDesc.height, 1u, 1u);
            m_Timings.packetPageMetadataBytes = 0u;
            m_Timings.packetPageListBytes = 0u;
        }
        const bool ready = bool(m_ResolveBindingSet);
        if (ready)
            m_ResourceBackend = SvsmResourceBackend::Dense;
        return ready;
    }

    bool SparseVirtualShadowMapPass::EnsureSparseResources(
        nvrhi::ITexture* cameraDepth,
        uint32_t physicalPageCount)
    {
        if (!cameraDepth ||
            !m_Timings.supported ||
            physicalPageCount == 0u ||
            physicalPageCount > SvsmPagesPerClipmap)
        {
            return false;
        }

        const nvrhi::TextureDesc& cameraDesc =
            cameraDepth->getDesc();
        const bool recreate =
            RequiresSvsmResourceRecreation(
                m_ResourceBackend,
                SvsmResourceBackend::Sparse) ||
            !m_PageTable ||
            !m_SparsePhysicalDepth ||
            !m_DirtyPageRectangles ||
            !m_Visibility ||
            m_Visibility->getDesc().width != cameraDesc.width ||
            m_Visibility->getDesc().height != cameraDesc.height ||
            m_AllocatedPhysicalPageCount != physicalPageCount;
        const bool rebind =
            recreate ||
            !m_SparseBindingSet ||
            !m_SparseResolveBindingSets[0] ||
            m_BoundCameraDepth != cameraDepth;
        if (!rebind)
            return true;

        m_SparseBindingSet = nullptr;
        m_SparseResolveBindingSets = {};
        if (m_BoundCameraDepth != cameraDepth)
        {
            m_StaticVisibilityValid.fill(false);
        }
        if (recreate)
        {
            InvalidateUiTimings();
            InvalidateDebugCounters();
            m_ResourceBackend = SvsmResourceBackend::None;
            m_SparseDepthPass.reset();
            m_PageTable = nullptr;
            m_SparsePhysicalDepth = nullptr;
            m_PhysicalOwners = nullptr;
            m_RenderPages = nullptr;
            m_CompactRenderPages = nullptr;
            m_DirtyPageRectangles = nullptr;
            m_Counters = nullptr;
            m_IndirectPageDispatchArguments = nullptr;
            m_IndirectDrawArguments = nullptr;
            m_IndirectDrawCapacity = 0u;
            m_IndirectDrawArgumentsInitialized = false;
            m_IndirectDrawArgumentsBatched = false;
            m_IndirectDrawArgumentsPacketPageCulling = false;
            m_PacketPageMetadata = nullptr;
            m_PacketPageRuntime = nullptr;
            m_PacketRenderPages = nullptr;
            m_PacketPageMetadataCapacity = 0u;
            m_PacketRenderPageCapacity = 0u;
            m_PacketPageCullingReady = false;
            m_PacketPageCullingUnavailableForPacketCache = false;
            m_ReportedPacketPageCullingFallback = false;
            m_DebugCounterReadbacks = {};
            m_DebugCounterReadbackPending.fill(false);
            m_DebugCounterReadbackGenerations.fill(0u);
            m_DebugCounterReadbackSourceFrames.fill(0u);
            m_SparseConstants = nullptr;
            m_Visibility = nullptr;
            m_SparseVisibilityCache = {};
            m_DebugVisualization = nullptr;
            m_DebugBindingSet = nullptr;

            nvrhi::TextureDesc pageTableDesc;
            pageTableDesc.width = SvsmPagesPerAxis;
            pageTableDesc.height = SvsmPagesPerAxis;
            pageTableDesc.arraySize = SvsmClipmapCount;
            pageTableDesc.format = nvrhi::Format::R32_UINT;
            pageTableDesc.dimension =
                nvrhi::TextureDimension::Texture2DArray;
            pageTableDesc.isUAV = true;
            pageTableDesc.debugName = "SVSM Page Tables";
            pageTableDesc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            m_PageTable = m_Device->createTexture(pageTableDesc);

            nvrhi::TextureDesc physicalDepthDesc;
            physicalDepthDesc.width = SvsmVirtualResolution;
            const uint32_t physicalPageRows =
                div_ceil(physicalPageCount, SvsmPagesPerAxis);
            physicalDepthDesc.height =
                physicalPageRows * SvsmPageSize;
            physicalDepthDesc.format = nvrhi::Format::R32_UINT;
            physicalDepthDesc.dimension =
                nvrhi::TextureDimension::Texture2D;
            physicalDepthDesc.isUAV = true;
            physicalDepthDesc.debugName =
                "SVSM Sparse Physical Depth Pool";
            physicalDepthDesc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            m_SparsePhysicalDepth =
                m_Device->createTexture(physicalDepthDesc);

            nvrhi::TextureDesc visibilityDesc;
            visibilityDesc.width = cameraDesc.width;
            visibilityDesc.height = cameraDesc.height;
            visibilityDesc.format = nvrhi::Format::R8_UNORM;
            visibilityDesc.dimension =
                nvrhi::TextureDimension::Texture2D;
            visibilityDesc.isUAV = true;
            visibilityDesc.debugName =
                "SVSM Full-Resolution Visibility";
            visibilityDesc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            for (nvrhi::TextureHandle& visibility :
                m_SparseVisibilityCache)
            {
                visibility = m_Device->createTexture(visibilityDesc);
            }
            m_Visibility = m_SparseVisibilityCache[0];
            visibilityDesc.debugName = "SVSM Debug Visualization";
            m_DebugVisualization =
                m_Device->createTexture(visibilityDesc);

            auto createUintBuffer =
                [this](uint32_t elementCount,
                    const char* debugName,
                    bool indirectArguments = false) {
                nvrhi::BufferDesc desc;
                desc.byteSize =
                    uint64_t(elementCount) * sizeof(uint32_t);
                desc.structStride = sizeof(uint32_t);
                desc.canHaveUAVs = true;
                desc.isDrawIndirectArgs = indirectArguments;
                desc.debugName = debugName;
                desc.enableAutomaticStateTracking(
                    nvrhi::ResourceStates::ShaderResource);
                return m_Device->createBuffer(desc);
            };
            m_PhysicalOwners = createUintBuffer(
                physicalPageCount, "SVSM Physical Page Owners");
            m_RenderPages = createUintBuffer(
                physicalPageCount, "SVSM Dirty Render Pages");
            m_CompactRenderPages = createUintBuffer(
                physicalPageCount * (SvsmClipmapCount + 4u),
                "SVSM Compact Per-Clipmap Render Pages");
            m_DirtyPageRectangles = createUintBuffer(
                SvsmClipmapCount *
                    SVSM_DIRTY_PAGE_RECT_WORDS_PER_LEVEL,
                "SVSM Dirty Page Rectangles");
            m_Counters = createUintBuffer(
                c_CounterCount,
                "SVSM Counters",
                true);

            nvrhi::BufferDesc pageDispatchDesc;
            pageDispatchDesc.byteSize =
                sizeof(nvrhi::DispatchIndirectArguments) *
                GatedDispatchArgumentCount;
            pageDispatchDesc.structStride = sizeof(uint32_t);
            pageDispatchDesc.isDrawIndirectArgs = true;
            pageDispatchDesc.debugName =
                "SVSM GPU-Gated Dispatch Arguments";
            pageDispatchDesc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::IndirectArgument);
            m_IndirectPageDispatchArguments =
                m_Device->createBuffer(pageDispatchDesc);

            nvrhi::BufferDesc indirectDesc;
                indirectDesc.byteSize =
                sizeof(nvrhi::DrawIndexedIndirectArguments);
            indirectDesc.structStride = sizeof(uint32_t);
            indirectDesc.canHaveUAVs = true;
            indirectDesc.isDrawIndirectArgs = true;
            indirectDesc.debugName =
                "SVSM GPU-Counted Indirect Draw Arguments";
            indirectDesc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::IndirectArgument);
            m_IndirectDrawArguments =
                m_Device->createBuffer(indirectDesc);
            if (m_IndirectDrawArguments)
                m_IndirectDrawCapacity = 1u;
            m_IndirectDrawArgumentsInitialized = false;
            m_IndirectDrawArgumentsBatched = false;
            m_IndirectDrawArgumentsPacketPageCulling = false;

            nvrhi::BufferDesc packetMetadataDesc;
            packetMetadataDesc.byteSize =
                sizeof(SparseVirtualShadowMapPacketMetadata);
            packetMetadataDesc.structStride =
                sizeof(SparseVirtualShadowMapPacketMetadata);
            packetMetadataDesc.debugName =
                "SVSM Packet Page Metadata";
            packetMetadataDesc.enableAutomaticStateTracking(
                nvrhi::ResourceStates::ShaderResource);
            m_PacketPageMetadata =
                m_Device->createBuffer(packetMetadataDesc);
            m_PacketPageRuntime = createUintBuffer(
                SVSM_PACKET_PAGE_RUNTIME_WORDS,
                "SVSM Packet Page Runtime");
            m_PacketRenderPages = createUintBuffer(
                1u, "SVSM Per-Packet Render Pages");
            if (m_PacketPageMetadata &&
                m_PacketPageRuntime &&
                m_PacketRenderPages)
            {
                m_PacketPageMetadataCapacity = 1u;
                m_PacketRenderPageCapacity = 1u;
            }

            nvrhi::BufferDesc readbackDesc;
            readbackDesc.byteSize =
                uint64_t(c_DebugCounterCount) * sizeof(uint32_t);
            readbackDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
            readbackDesc.debugName =
                "SVSM Optional Debug Counter Readback";
            for (nvrhi::BufferHandle& readback :
                m_DebugCounterReadbacks)
            {
                readback = m_Device->createBuffer(readbackDesc);
            }

            nvrhi::BufferDesc sparseConstantsDesc;
            sparseConstantsDesc.byteSize =
                sizeof(SparseVirtualShadowMapSparseConstants);
            sparseConstantsDesc.debugName = "SVSM Sparse Constants";
            sparseConstantsDesc.isConstantBuffer = true;
            sparseConstantsDesc.isVolatile = true;
            // The compact GPU path writes one version per clipmap for both
            // clearing and finalization in addition to allocation and draw
            // submission. Keep enough versions for the worst-case frame.
            sparseConstantsDesc.maxVersions = 64u;
            m_SparseConstants =
                m_Device->createBuffer(sparseConstantsDesc);

            if (!m_PageTable ||
                !m_SparsePhysicalDepth ||
                std::any_of(
                    m_SparseVisibilityCache.begin(),
                    m_SparseVisibilityCache.end(),
                    [](const nvrhi::TextureHandle& visibility) {
                        return !visibility;
                    }) ||
                !m_DebugVisualization ||
                !m_PhysicalOwners ||
                !m_RenderPages ||
                !m_CompactRenderPages ||
                !m_DirtyPageRectangles ||
                !m_Counters ||
                !m_IndirectPageDispatchArguments ||
                !m_IndirectDrawArguments ||
                !m_PacketPageMetadata ||
                !m_PacketPageRuntime ||
                !m_PacketRenderPages ||
                std::any_of(
                    m_DebugCounterReadbacks.begin(),
                    m_DebugCounterReadbacks.end(),
                    [](const nvrhi::BufferHandle& readback) {
                        return !readback;
                    }) ||
                !m_SparseConstants)
            {
                log::error(
                    "SVSM could not allocate the fixed sparse physical pool.");
                return false;
            }

            m_SparseDepthPass = std::make_unique<SparseDepthPass>(
                m_Device,
                m_CommonPasses,
                m_SparsePhysicalDepth,
                m_PageTable,
                m_CompactRenderPages,
                m_RenderPages,
                m_SparseConstants,
                m_Counters,
                m_IndirectDrawArguments,
                m_PacketPageMetadata,
                m_PacketPageRuntime,
                m_PacketRenderPages,
                physicalPageCount);
            GBufferFillPass::CreateParameters depthParameters;
            depthParameters.enableDepthWrite = false;
            depthParameters.enableMotionVectors = false;
            // Packet-page buffer growth can replace this pass's custom view
            // binding set while prior frames are still in flight. Let NVRHI
            // retain those mutable sets and their resources through GPU use.
            depthParameters.trackLiveness = true;
            m_SparseDepthPass->Init(
                *m_ShaderFactory, depthParameters);

            for (uint32_t level = 0u;
                level < SvsmClipmapCount;
                ++level)
            {
                if (!m_ClipmapViews[level])
                {
                    m_ClipmapViews[level] =
                        std::make_shared<PlanarView>();
                }
                m_ClipmapViews[level]->SetViewport(
                    nvrhi::Viewport(
                        float(SvsmVirtualResolution),
                        float(SvsmVirtualResolution)));
                m_ClipmapViews[level]->SetArraySlice(0u);
            }

            m_AllocatedPhysicalPageCount = physicalPageCount;
            m_SparseResourcesNeedClear = true;
            m_CacheStateValid = false;
            m_StaticPageRequestCacheReady = false;
            m_StaticPageRequestJitterActive = false;
            m_StaticPageDrainFramesRemaining = 0u;
            m_StaticPageRequestPageRenderBudget =
                std::numeric_limits<uint32_t>::max();
            m_StaticPageRequestCoarsestPageRenderBudgetEnabled = false;
            m_StaticPageRequestCameraDepth = nullptr;
            m_StaticJitterOffsetValid.fill(false);
            m_StaticVisibilityValid.fill(false);
            m_StaticVisibilitySettingsValid = false;
            m_DebugCounterReadbackPending.fill(false);
            m_DebugCounterReadbackGenerations.fill(0u);
            m_DebugCounterReadbackSourceFrames.fill(0u);
            m_Timings.physicalDepthBytes = TextureByteSize(
                SvsmVirtualResolution,
                physicalPageRows * SvsmPageSize,
                1u,
                sizeof(uint32_t));
            m_Timings.visibilityBytes = TextureByteSize(
                cameraDesc.width,
                cameraDesc.height,
                c_StaticVisibilityCacheSlotCount,
                1u);
            m_Timings.packetPageMetadataBytes =
                sizeof(SparseVirtualShadowMapPacketMetadata) +
                uint64_t(SVSM_PACKET_PAGE_RUNTIME_WORDS) *
                    sizeof(uint32_t) +
                uint64_t(SvsmClipmapCount) *
                    SVSM_DIRTY_PAGE_RECT_WORDS_PER_LEVEL *
                    sizeof(uint32_t);
            m_Timings.packetPageListBytes = sizeof(uint32_t);
        }

        if (!CreateSparseComputeBindingSet(cameraDepth))
            return false;

        for (uint32_t slot = 0u;
            slot < c_StaticVisibilityCacheSlotCount;
            ++slot)
        {
            nvrhi::BindingSetDesc resolveSetDesc;
            resolveSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_SparseConstants),
                nvrhi::BindingSetItem::Texture_SRV(0, cameraDepth),
                nvrhi::BindingSetItem::Texture_SRV(1, m_PageTable),
                nvrhi::BindingSetItem::Texture_SRV(
                    2, m_SparsePhysicalDepth),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    3, m_PhysicalOwners),
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    4, m_RenderPages),
                nvrhi::BindingSetItem::Texture_UAV(
                    0, m_SparseVisibilityCache[slot]),
                nvrhi::BindingSetItem::Texture_UAV(
                    1, m_DebugVisualization),
                nvrhi::BindingSetItem::StructuredBuffer_UAV(
                    2, m_Counters)
            };
            m_SparseResolveBindingSets[slot] =
                m_Device->createBindingSet(
                    resolveSetDesc, m_SparseResolveBindingLayout);
        }

        m_BoundCameraDepth = cameraDepth;
        const bool ready = bool(m_SparseBindingSet) &&
            std::all_of(
                m_SparseResolveBindingSets.begin(),
                m_SparseResolveBindingSets.end(),
                [](const nvrhi::BindingSetHandle& bindingSet) {
                    return bool(bindingSet);
                });
        if (ready)
            m_ResourceBackend = SvsmResourceBackend::Sparse;
        return ready;
    }

    bool SparseVirtualShadowMapPass::CreateSparseComputeBindingSet(
        nvrhi::ITexture* cameraDepth)
    {
        m_SparseBindingSet =
            CreateSparseComputeBindingSetForResources(
                cameraDepth,
                m_IndirectDrawArguments,
                m_PacketPageMetadata,
                m_PacketPageRuntime,
                m_PacketRenderPages);
        return bool(m_SparseBindingSet);
    }

    nvrhi::BindingSetHandle
        SparseVirtualShadowMapPass::
            CreateSparseComputeBindingSetForResources(
                nvrhi::ITexture* cameraDepth,
                nvrhi::IBuffer* indirectDrawArguments,
                nvrhi::IBuffer* packetPageMetadata,
                nvrhi::IBuffer* packetPageRuntime,
                nvrhi::IBuffer* packetRenderPages) const
    {
        if (!cameraDepth ||
            !m_SparseConstants ||
            !m_PageTable ||
            !m_PhysicalOwners ||
            !m_RenderPages ||
            !m_Counters ||
            !m_SparsePhysicalDepth ||
            !m_CompactRenderPages ||
            !m_DirtyPageRectangles ||
            !indirectDrawArguments ||
            !packetPageMetadata ||
            !packetPageRuntime ||
            !packetRenderPages)
        {
            return nullptr;
        }

        nvrhi::BindingSetDesc sparseSetDesc;
        sparseSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(
                0, m_SparseConstants),
            nvrhi::BindingSetItem::Texture_SRV(0, cameraDepth),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                1, packetPageMetadata),
            nvrhi::BindingSetItem::Texture_UAV(0, m_PageTable),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                1, m_PhysicalOwners),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                2, m_RenderPages),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                3, m_Counters),
            nvrhi::BindingSetItem::Texture_UAV(
                4, m_SparsePhysicalDepth),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                5, m_CompactRenderPages),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                6, indirectDrawArguments),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                7, packetPageRuntime),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                8, packetRenderPages),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                9, m_DirtyPageRectangles)
        };
        return m_Device->createBindingSet(
            sparseSetDesc, m_SparseBindingLayout);
    }

    bool SparseVirtualShadowMapPass::EnsureIndirectDrawCapacity(
        uint32_t requiredPackets,
        bool& recreated)
    {
        recreated = false;
        const uint32_t requiredCapacity =
            std::max(requiredPackets, 1u);
        if (m_IndirectDrawArguments &&
            m_IndirectDrawCapacity >= requiredCapacity)
        {
            return true;
        }

        constexpr uint32_t maximumCapacity =
            std::numeric_limits<uint32_t>::max() /
            uint32_t(sizeof(
                nvrhi::DrawIndexedIndirectArguments));
        if (requiredCapacity > maximumCapacity)
            return false;

        uint32_t newCapacity =
            std::max(m_IndirectDrawCapacity, 1u);
        while (newCapacity < requiredCapacity)
        {
            if (newCapacity > maximumCapacity / 2u)
            {
                newCapacity = requiredCapacity;
                break;
            }
            newCapacity *= 2u;
        }

        nvrhi::BufferDesc indirectDesc;
        indirectDesc.byteSize =
            uint64_t(newCapacity) *
            sizeof(nvrhi::DrawIndexedIndirectArguments);
        indirectDesc.structStride = sizeof(uint32_t);
        indirectDesc.canHaveUAVs = true;
        indirectDesc.isDrawIndirectArgs = true;
        indirectDesc.debugName =
            "SVSM Persistent Indirect Draw Arguments";
        indirectDesc.enableAutomaticStateTracking(
            nvrhi::ResourceStates::IndirectArgument);
        nvrhi::BufferHandle newBuffer =
            m_Device->createBuffer(indirectDesc);
        if (!newBuffer)
            return false;

        nvrhi::BindingSetHandle newSparseBindingSet =
            CreateSparseComputeBindingSetForResources(
                m_BoundCameraDepth,
                newBuffer,
                m_PacketPageMetadata,
                m_PacketPageRuntime,
                m_PacketRenderPages);
        if (!newSparseBindingSet)
            return false;

        m_IndirectDrawArguments = newBuffer;
        m_IndirectDrawCapacity = newCapacity;
        m_IndirectDrawArgumentsInitialized = false;
        if (m_SparseDepthPass)
        {
            m_SparseDepthPass->SetIndirectDrawArguments(
                m_IndirectDrawArguments);
        }
        m_SparseBindingSet = newSparseBindingSet;

        recreated = true;
        return true;
    }

    bool SparseVirtualShadowMapPass::EnsurePacketPageCapacity(
        uint32_t requiredPackets,
        uint32_t requiredPageEntries,
        bool& recreated)
    {
        recreated = false;
        const uint32_t requiredMetadataCapacity =
            std::max(requiredPackets, 1u);
        const uint32_t requiredListCapacity =
            std::max(requiredPageEntries, 1u);
        if (requiredListCapacity >
                MaximumPacketRenderPageEntries)
        {
            return false;
        }
        if (m_PacketPageMetadata &&
            m_PacketPageRuntime &&
            m_PacketRenderPages &&
            m_PacketPageMetadataCapacity >=
                requiredMetadataCapacity &&
            m_PacketRenderPageCapacity >=
                requiredListCapacity)
        {
            return true;
        }

        auto growCapacity = [](uint32_t current,
                                uint32_t required,
                                uint32_t maximum) {
            uint32_t capacity = std::max(current, 1u);
            while (capacity < required)
            {
                if (capacity > maximum / 2u)
                {
                    capacity = required;
                    break;
                }
                capacity *= 2u;
            }
            return capacity;
        };
        const uint32_t metadataCapacity = growCapacity(
            m_PacketPageMetadataCapacity,
            requiredMetadataCapacity,
            std::numeric_limits<uint32_t>::max() /
                uint32_t(sizeof(
                    SparseVirtualShadowMapPacketMetadata)));
        const uint32_t listCapacity = growCapacity(
            m_PacketRenderPageCapacity,
            requiredListCapacity,
            MaximumPacketRenderPageEntries);

        nvrhi::BufferDesc metadataDesc;
        metadataDesc.byteSize =
            uint64_t(metadataCapacity) *
            sizeof(SparseVirtualShadowMapPacketMetadata);
        metadataDesc.structStride =
            sizeof(SparseVirtualShadowMapPacketMetadata);
        metadataDesc.debugName = "SVSM Packet Page Metadata";
        metadataDesc.enableAutomaticStateTracking(
            nvrhi::ResourceStates::ShaderResource);
        nvrhi::BufferHandle metadata =
            m_Device->createBuffer(metadataDesc);

        auto createUintBuffer =
            [this](uint32_t elementCount, const char* debugName) {
                nvrhi::BufferDesc desc;
                desc.byteSize =
                    uint64_t(elementCount) * sizeof(uint32_t);
                desc.structStride = sizeof(uint32_t);
                desc.canHaveUAVs = true;
                desc.debugName = debugName;
                desc.enableAutomaticStateTracking(
                    nvrhi::ResourceStates::ShaderResource);
                return m_Device->createBuffer(desc);
            };
        nvrhi::BufferHandle runtime = createUintBuffer(
            metadataCapacity *
                SVSM_PACKET_PAGE_RUNTIME_WORDS,
            "SVSM Packet Page Runtime");
        nvrhi::BufferHandle renderPages = createUintBuffer(
            listCapacity, "SVSM Per-Packet Render Pages");
        if (!metadata || !runtime || !renderPages ||
            !m_SparseDepthPass)
        {
            return false;
        }

        nvrhi::BindingSetHandle newSparseBindingSet =
            CreateSparseComputeBindingSetForResources(
                m_BoundCameraDepth,
                m_IndirectDrawArguments,
                metadata,
                runtime,
                renderPages);
        if (!newSparseBindingSet)
            return false;
        if (!m_SparseDepthPass->SetPacketPageBuffers(
                metadata, runtime, renderPages))
        {
            return false;
        }
        m_PacketPageMetadata = metadata;
        m_PacketPageRuntime = runtime;
        m_PacketRenderPages = renderPages;
        m_PacketPageMetadataCapacity = metadataCapacity;
        m_PacketRenderPageCapacity = listCapacity;
        m_PacketPageCullingReady = false;
        m_SparseBindingSet = newSparseBindingSet;

        m_Timings.packetPageMetadataBytes =
            uint64_t(metadataCapacity) *
            (sizeof(SparseVirtualShadowMapPacketMetadata) +
                uint64_t(SVSM_PACKET_PAGE_RUNTIME_WORDS) *
                    sizeof(uint32_t)) +
            uint64_t(SvsmClipmapCount) *
                SVSM_DIRTY_PAGE_RECT_WORDS_PER_LEVEL *
                sizeof(uint32_t);
        m_Timings.packetPageListBytes =
            uint64_t(listCapacity) * sizeof(uint32_t);
        recreated = true;
        return true;
    }

    bool SparseVirtualShadowMapPass::UpdateClipmapViews(
        const SparseVirtualShadowMapSettings& settings,
        const IView& cameraView,
        const DirectionalLight& light,
        const std::shared_ptr<SceneGraphNode>& rootNode)
    {
        auto isFloatRepresentable = [](const double3& value) {
            const double maximum =
                double(std::numeric_limits<float>::max());
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z) &&
                std::abs(value.x) <= maximum &&
                std::abs(value.y) <= maximum &&
                std::abs(value.z) <= maximum;
        };
        auto isFiniteAffine = [](const affine3& value) {
            return all(donut::math::isfinite(
                    value.m_linear.row0)) &&
                all(donut::math::isfinite(
                    value.m_linear.row1)) &&
                all(donut::math::isfinite(
                    value.m_linear.row2)) &&
                all(donut::math::isfinite(
                    value.m_translation));
        };
        auto isFiniteMatrix = [](const float4x4& value) {
            return all(donut::math::isfinite(value.row0)) &&
                all(donut::math::isfinite(value.row1)) &&
                all(donut::math::isfinite(value.row2)) &&
                all(donut::math::isfinite(value.row3));
        };
        daffine3 viewToWorld =
            light.GetNode()->GetLocalToWorldTransform();
        viewToWorld.m_translation = double3(0.0);
        viewToWorld =
            scaling(double3(1.0, 1.0, -1.0)) * viewToWorld;
        const daffine3 uncenteredWorldToViewDouble =
            inverse(viewToWorld);
        if (!isFloatRepresentable(
                uncenteredWorldToViewDouble.m_linear.row0) ||
            !isFloatRepresentable(
                uncenteredWorldToViewDouble.m_linear.row1) ||
            !isFloatRepresentable(
                uncenteredWorldToViewDouble.m_linear.row2) ||
            !isFloatRepresentable(
                uncenteredWorldToViewDouble.m_translation))
        {
            return false;
        }
        const affine3 uncenteredWorldToView =
            affine3(uncenteredWorldToViewDouble);
        if (!isFiniteAffine(uncenteredWorldToView) ||
            !isFiniteAffine(inverse(uncenteredWorldToView)))
        {
            return false;
        }
        const float3 anchor = cameraView.GetViewOrigin();
        float3 anchorView =
            uncenteredWorldToView.transformPoint(anchor);
        if (!all(donut::math::isfinite(anchorView)))
            return false;
        if (rootNode)
        {
            const box3& sceneBounds =
                rootNode->GetGlobalBoundingBox();
            if (!sceneBounds.isempty() &&
                all(donut::math::isfinite(
                    sceneBounds.m_mins)) &&
                all(donut::math::isfinite(
                    sceneBounds.m_maxs)))
            {
                anchorView.z =
                    uncenteredWorldToView.transformPoint(
                        sceneBounds.center()).z;
            }
        }
        if (!all(donut::math::isfinite(anchorView)))
            return false;

        std::array<int2, SvsmClipmapCount> renderOrigins{};
        std::array<float3, SvsmClipmapCount> centers{};
        std::array<float, SvsmClipmapCount> extents{};
        float extent = settings.firstClipmapExtent;
        for (uint32_t level = 0u;
            level < SvsmClipmapCount;
            ++level)
        {
            const float pageWorldSize =
                extent / float(SvsmPagesPerAxis);
            float3 snappedAnchorView = anchorView;
            int32_t originX = 0;
            int32_t originY = 0;
            if (!TryQuantizeSvsmRenderOrigin(
                    snappedAnchorView.x,
                    pageWorldSize,
                    originX) ||
                !TryQuantizeSvsmRenderOrigin(
                    snappedAnchorView.y,
                    pageWorldSize,
                    originY))
            {
                return false;
            }
            renderOrigins[level] = int2(originX, originY);
            snappedAnchorView.x = float(originX) * pageWorldSize;
            snappedAnchorView.y = float(originY) * pageWorldSize;
            const double3 center =
                viewToWorld.transformPoint(double3(snappedAnchorView));
            if (!isFloatRepresentable(center))
                return false;
            centers[level] = float3(center);
            extents[level] = extent;
            if (level + 1u < SvsmClipmapCount)
                extent *= 2.f;
        }

        std::array<affine3, SvsmClipmapCount> worldToViews{};
        std::array<affine3, SvsmClipmapCount> inverseWorldToViews{};
        for (uint32_t level = 0u;
            level < SvsmClipmapCount;
            ++level)
        {
            const float3& center = centers[level];
            worldToViews[level] =
                translation(-center) * uncenteredWorldToView;
            inverseWorldToViews[level] =
                inverse(worldToViews[level]);
            if (!isFiniteAffine(worldToViews[level]) ||
                !isFiniteAffine(inverseWorldToViews[level]))
            {
                return false;
            }
        }

        std::array<float4x4, SvsmClipmapCount>
            reverseProjections{};
        for (uint32_t level = 0u;
            level < SvsmClipmapCount;
            ++level)
        {
            const float halfExtent = extents[level] * 0.5f;
            const float halfDepth =
                settings.maximumLightDepth * 0.5f;
            reverseProjections[level] = orthoProjD3DStyle(
                -halfExtent,
                halfExtent,
                -halfExtent,
                halfExtent,
                halfDepth,
                -halfDepth);
            const float4x4& reverseProjection =
                reverseProjections[level];
            const float4x4 inverseProjection =
                inverse(reverseProjection);
            const float4x4 worldToClip =
                affineToHomogeneous(worldToViews[level]) *
                reverseProjection;
            const float4x4 clipToWorld =
                inverseProjection *
                affineToHomogeneous(inverseWorldToViews[level]);
            const float worldToClipDeterminant =
                determinant(worldToClip);
            const float clipToWorldDeterminant =
                determinant(clipToWorld);
            if (!isFiniteMatrix(reverseProjection) ||
                !isFiniteMatrix(inverseProjection) ||
                !isFiniteMatrix(worldToClip) ||
                !isFiniteMatrix(clipToWorld) ||
                !std::isfinite(worldToClipDeterminant) ||
                !std::isfinite(clipToWorldDeterminant) ||
                worldToClipDeterminant == 0.f ||
                clipToWorldDeterminant == 0.f ||
                reverseProjection.m00 == 0.f ||
                reverseProjection.m11 == 0.f ||
                reverseProjection.m22 == 0.f)
            {
                return false;
            }
        }

        m_CurrentLightDepthOrigin = anchorView.z;
        m_CurrentRenderOrigins = renderOrigins;
        for (uint32_t level = 0u;
            level < SvsmClipmapCount;
            ++level)
        {
            m_ClipmapViews[level]->SetMatrices(
                worldToViews[level], reverseProjections[level]);
            m_ClipmapViews[level]->UpdateCache();
            assert(m_ClipmapViews[level]->IsReverseDepth());
        }
        return true;
    }

    uint64_t SparseVirtualShadowMapPass::ComputeSceneStateHash(
        const std::shared_ptr<SceneGraphNode>& rootNode) const
    {
        if (!rootNode)
            return 0u;

        uint64_t hash = 1469598103934665603ull;
        auto appendBytes = [&hash](
            const void* data,
            size_t size) {
            const auto* bytes =
                static_cast<const uint8_t*>(data);
            for (size_t index = 0; index < size; ++index)
            {
                hash ^= uint64_t(bytes[index]);
                hash *= 1099511628211ull;
            }
        };
        auto appendFloat3 = [&appendBytes](const float3& value) {
            appendBytes(&value.x, sizeof(value.x));
            appendBytes(&value.y, sizeof(value.y));
            appendBytes(&value.z, sizeof(value.z));
        };
        auto appendAffine = [&appendFloat3](
            const affine3& value) {
            appendFloat3(value.m_linear.row0);
            appendFloat3(value.m_linear.row1);
            appendFloat3(value.m_linear.row2);
            appendFloat3(value.m_translation);
        };
        auto appendBox = [&appendFloat3](const box3& value) {
            appendFloat3(value.m_mins);
            appendFloat3(value.m_maxs);
        };
        const uintptr_t rootIdentity =
            reinterpret_cast<uintptr_t>(rootNode.get());
        appendBytes(&rootIdentity, sizeof(rootIdentity));

        SceneGraphWalker walker(rootNode.get());
        while (walker)
        {
            const auto& leaf = walker->GetLeaf();
            if (const auto* instance =
                    dynamic_cast<const MeshInstance*>(leaf.get()))
            {
                const affine3& transform =
                    walker->GetLocalToWorldTransformFloat();
                const box3& bounds =
                    walker->GetGlobalBoundingBox();
                appendAffine(transform);
                appendBox(bounds);
                const uintptr_t instanceIdentity =
                    reinterpret_cast<uintptr_t>(instance);
                const int instanceIndex =
                    instance->GetInstanceIndex();
                appendBytes(
                    &instanceIdentity,
                    sizeof(instanceIdentity));
                appendBytes(
                    &instanceIndex,
                    sizeof(instanceIndex));
                if (const auto* skinned =
                        dynamic_cast<
                            const SkinnedMeshInstance*>(
                            instance))
                {
                    const uint32_t updateFrame =
                        skinned->GetLastUpdateFrameIndex();
                    appendBytes(
                        &updateFrame,
                        sizeof(updateFrame));
                }
                const auto& mesh = instance->GetMesh();
                if (!mesh)
                {
                    walker.Next(true);
                    continue;
                }
                const uintptr_t meshIdentity =
                    reinterpret_cast<uintptr_t>(mesh.get());
                appendBytes(
                    &meshIdentity, sizeof(meshIdentity));
                appendBytes(
                    &mesh->indexOffset,
                    sizeof(mesh->indexOffset));
                appendBytes(
                    &mesh->vertexOffset,
                    sizeof(mesh->vertexOffset));
                appendBytes(
                    &mesh->totalIndices,
                    sizeof(mesh->totalIndices));
                appendBytes(
                    &mesh->totalVertices,
                    sizeof(mesh->totalVertices));
                const bool hasSkinPrototype =
                    bool(mesh->skinPrototype);
                appendBytes(
                    &hasSkinPrototype,
                    sizeof(hasSkinPrototype));
                appendBytes(
                    &mesh->isSkinPrototype,
                    sizeof(mesh->isSkinPrototype));
                appendBytes(
                    &mesh->isMorphTargetAnimationMesh,
                    sizeof(mesh->isMorphTargetAnimationMesh));
                const uintptr_t bufferGroupIdentity =
                    reinterpret_cast<uintptr_t>(
                        mesh->buffers.get());
                appendBytes(
                    &bufferGroupIdentity,
                    sizeof(bufferGroupIdentity));
                if (mesh->buffers)
                {
                    const uintptr_t indexBufferIdentity =
                        reinterpret_cast<uintptr_t>(
                            mesh->buffers->indexBuffer.Get());
                    const uintptr_t vertexBufferIdentity =
                        reinterpret_cast<uintptr_t>(
                            mesh->buffers->vertexBuffer.Get());
                    const uintptr_t instanceBufferIdentity =
                        reinterpret_cast<uintptr_t>(
                            mesh->buffers->instanceBuffer.Get());
                    appendBytes(
                        &indexBufferIdentity,
                        sizeof(indexBufferIdentity));
                    appendBytes(
                        &vertexBufferIdentity,
                        sizeof(vertexBufferIdentity));
                    appendBytes(
                        &instanceBufferIdentity,
                        sizeof(instanceBufferIdentity));
                    const nvrhi::BufferRange& positionRange =
                        mesh->buffers->getVertexBufferRange(
                            VertexAttribute::Position);
                    const nvrhi::BufferRange& texCoordRange =
                        mesh->buffers->getVertexBufferRange(
                            VertexAttribute::TexCoord1);
                    appendBytes(
                        &positionRange.byteOffset,
                        sizeof(positionRange.byteOffset));
                    appendBytes(
                        &positionRange.byteSize,
                        sizeof(positionRange.byteSize));
                    appendBytes(
                        &texCoordRange.byteOffset,
                        sizeof(texCoordRange.byteOffset));
                    appendBytes(
                        &texCoordRange.byteSize,
                        sizeof(texCoordRange.byteSize));
                }
                for (const auto& geometry : mesh->geometries)
                {
                    if (!geometry || !geometry->material)
                        continue;
                    const uintptr_t geometryIdentity =
                        reinterpret_cast<uintptr_t>(
                            geometry.get());
                    appendBytes(
                        &geometryIdentity,
                        sizeof(geometryIdentity));
                    appendBytes(
                        &geometry->indexOffsetInMesh,
                        sizeof(geometry->indexOffsetInMesh));
                    appendBytes(
                        &geometry->vertexOffsetInMesh,
                        sizeof(geometry->vertexOffsetInMesh));
                    appendBytes(
                        &geometry->numIndices,
                        sizeof(geometry->numIndices));
                    appendBox(geometry->objectSpaceBounds);
                    const Material& material = *geometry->material;
                    const uintptr_t materialIdentity =
                        reinterpret_cast<uintptr_t>(
                            geometry->material.get());
                    const uintptr_t materialConstantsIdentity =
                        reinterpret_cast<uintptr_t>(
                            material.materialConstants.Get());
                    appendBytes(
                        &materialIdentity,
                        sizeof(materialIdentity));
                    appendBytes(
                        &materialConstantsIdentity,
                        sizeof(materialConstantsIdentity));
                    appendBytes(
                        &material.domain, sizeof(material.domain));
                    appendBytes(
                        &material.alphaCutoff,
                        sizeof(material.alphaCutoff));
                    appendBytes(
                        &material.opacity,
                        sizeof(material.opacity));
                    appendBytes(
                        &material.doubleSided,
                        sizeof(material.doubleSided));
                    appendBytes(
                        &material.enableBaseOrDiffuseTexture,
                        sizeof(
                            material.enableBaseOrDiffuseTexture));
                    appendBytes(
                        &material.enableOpacityTexture,
                        sizeof(material.enableOpacityTexture));
                    const uintptr_t baseTexture =
                        reinterpret_cast<uintptr_t>(
                            material.baseOrDiffuseTexture.get());
                    const uintptr_t opacityTexture =
                        reinterpret_cast<uintptr_t>(
                            material.opacityTexture.get());
                    appendBytes(
                        &baseTexture, sizeof(baseTexture));
                    appendBytes(
                        &opacityTexture, sizeof(opacityTexture));
                    if (material.baseOrDiffuseTexture)
                    {
                        const uintptr_t textureIdentity =
                            reinterpret_cast<uintptr_t>(
                                material.baseOrDiffuseTexture->
                                    texture.Get());
                        appendBytes(
                            &textureIdentity,
                            sizeof(textureIdentity));
                    }
                    if (material.opacityTexture)
                    {
                        const uintptr_t textureIdentity =
                            reinterpret_cast<uintptr_t>(
                                material.opacityTexture->
                                    texture.Get());
                        appendBytes(
                            &textureIdentity,
                            sizeof(textureIdentity));
                    }
                }
            }
            walker.Next(true);
        }
        return hash;
    }

    void SparseVirtualShadowMapPass::InvalidateUiTimings()
    {
        ++m_UiTimingGeneration;
        if (m_UiTimingGeneration == 0u)
            m_UiTimingGeneration = 1u;

        m_LastAcceptedUiTimingSourceFrame = 0u;
        m_LastAcceptedUiTimingSourceFrameValid = false;
        m_Timings.gpuTimingSource =
            SvsmGpuTimingSource::Unavailable;
        m_Timings.gpuTimingAgeFrames = 0u;
        m_Timings.pageMarkingMilliseconds = 0.f;
        m_Timings.allocationMilliseconds = 0.f;
        m_Timings.clearingMilliseconds = 0.f;
        m_Timings.packetPageCullingMilliseconds = 0.f;
        m_Timings.pageRenderingMilliseconds = 0.f;
        m_Timings.filteringMilliseconds = 0.f;
        m_Timings.totalMilliseconds = 0.f;
    }

    void SparseVirtualShadowMapPass::UpdateUiTimingContext(
        const SparseVirtualShadowMapSettings& settings,
        SvsmResourceBackend backend,
        bool detailedGpuTimingEnabled)
    {
        const bool unchanged = m_UiTimingContextValid &&
            IsSameSvsmConfiguration(
                m_UiTimingContext.settings, settings) &&
            m_UiTimingContext.backend == backend &&
            m_UiTimingContext.detailedGpuTimingEnabled ==
                detailedGpuTimingEnabled &&
            m_UiTimingContext.staticPageRequestReuseActive ==
                m_Timings.staticPageRequestReuseActive &&
            m_UiTimingContext.staticPageDrainActive ==
                m_Timings.staticPageDrainActive &&
            m_UiTimingContext.staticVisibilityReuseActive ==
                m_Timings.staticVisibilityReuseActive &&
            m_UiTimingContext.batchedDrawSupported ==
                m_Timings.batchedDrawSupported &&
            m_UiTimingContext.batchedDrawActive ==
                m_Timings.batchedDrawActive &&
            m_UiTimingContext.packetStateSortingActive ==
                m_Timings.packetStateSortingActive &&
            m_UiTimingContext.levelEmptyWorkSkipActive ==
                m_Timings.levelEmptyWorkSkipActive &&
            m_UiTimingContext.packetPageCullingActive ==
                m_Timings.packetPageCullingActive &&
            m_UiTimingContext.dirtyPageScatterRasterActive ==
                m_Timings.dirtyPageScatterRasterActive &&
            m_UiTimingContext.packetPageCullingUnavailable ==
                m_Timings.packetPageCullingUnavailable &&
            m_UiTimingContext.staticPageRequestReuseRejectMask ==
                m_Timings.staticPageRequestReuseRejectMask;
        if (unchanged)
            return;

        m_UiTimingContext.settings = settings;
        m_UiTimingContext.backend = backend;
        m_UiTimingContext.detailedGpuTimingEnabled =
            detailedGpuTimingEnabled;
        m_UiTimingContext.staticPageRequestReuseActive =
            m_Timings.staticPageRequestReuseActive;
        m_UiTimingContext.staticPageDrainActive =
            m_Timings.staticPageDrainActive;
        m_UiTimingContext.staticVisibilityReuseActive =
            m_Timings.staticVisibilityReuseActive;
        m_UiTimingContext.batchedDrawSupported =
            m_Timings.batchedDrawSupported;
        m_UiTimingContext.batchedDrawActive =
            m_Timings.batchedDrawActive;
        m_UiTimingContext.packetStateSortingActive =
            m_Timings.packetStateSortingActive;
        m_UiTimingContext.levelEmptyWorkSkipActive =
            m_Timings.levelEmptyWorkSkipActive;
        m_UiTimingContext.packetPageCullingActive =
            m_Timings.packetPageCullingActive;
        m_UiTimingContext.dirtyPageScatterRasterActive =
            m_Timings.dirtyPageScatterRasterActive;
        m_UiTimingContext.packetPageCullingUnavailable =
            m_Timings.packetPageCullingUnavailable;
        m_UiTimingContext.staticPageRequestReuseRejectMask =
            m_Timings.staticPageRequestReuseRejectMask;
        m_UiTimingContextValid = true;
        InvalidateUiTimings();
    }

    void SparseVirtualShadowMapPass::PublishKnownZeroUiTiming()
    {
        m_Timings.pageMarkingMilliseconds = 0.f;
        m_Timings.allocationMilliseconds = 0.f;
        m_Timings.clearingMilliseconds = 0.f;
        m_Timings.packetPageCullingMilliseconds = 0.f;
        m_Timings.pageRenderingMilliseconds = 0.f;
        m_Timings.filteringMilliseconds = 0.f;
        m_Timings.totalMilliseconds = 0.f;
        m_Timings.gpuTimingSource =
            SvsmGpuTimingSource::KnownZero;
        m_Timings.gpuTimingAgeFrames = 0u;
        m_LastAcceptedUiTimingSourceFrame = m_TimerFrame;
        m_LastAcceptedUiTimingSourceFrameValid = true;
    }

    void SparseVirtualShadowMapPass::AdvanceTimers()
    {
        bool hasNewestUiSample = false;
        uint64_t newestUiFrame = 0u;
        SparseVirtualShadowMapGpuTiming newestUiSample;

        for (uint32_t slot = 0u; slot < c_TimerLatency; ++slot)
        {
            const uint32_t issuedMask = m_TimerIssuedStageMasks[slot];
            if (issuedMask == 0u)
                continue;

            for (uint32_t stage = 0u;
                stage < c_TimerStageCount;
                ++stage)
            {
                const uint32_t stageBit = 1u << stage;
                if ((issuedMask & stageBit) == 0u ||
                    !m_TimerPending[stage][slot])
                {
                    continue;
                }

                nvrhi::ITimerQuery* query =
                    m_TimerQueries[stage][slot];
                if (!m_Device->pollTimerQuery(query))
                    continue;

                m_TimerSlotValues[slot][stage] =
                    m_Device->getTimerQueryTime(query) * 1000.f;
                m_Device->resetTimerQuery(query);
                m_TimerPending[stage][slot] = false;
            }

            bool complete = true;
            for (uint32_t stage = 0u;
                stage < c_TimerStageCount;
                ++stage)
            {
                if ((issuedMask & (1u << stage)) != 0u &&
                    m_TimerPending[stage][slot])
                {
                    complete = false;
                    break;
                }
            }
            if (!complete)
                continue;

            SparseVirtualShadowMapGpuTiming sample;
            sample.sourceTag = m_TimerSourceTags[slot];
            sample.detailedGpuTimingEnabled =
                m_TimerDetailedStagesEnabled[slot];
            sample.pageMarkingMilliseconds =
                m_TimerSlotValues[slot][TimerPageMarking];
            sample.allocationMilliseconds =
                m_TimerSlotValues[slot][TimerAllocation];
            sample.clearingMilliseconds =
                m_TimerSlotValues[slot][TimerClearing];
            sample.packetPageCullingMilliseconds =
                m_TimerSlotValues[slot][TimerPacketPageCulling];
            sample.pageRenderingMilliseconds =
                m_TimerSlotValues[slot][TimerPageRendering];
            sample.filteringMilliseconds =
                m_TimerSlotValues[slot][TimerFiltering];
            sample.totalMilliseconds =
                m_TimerSlotValues[slot][TimerTotal];

            if (ShouldAcceptSvsmTelemetrySample(
                    m_TimerUiTimingGenerations[slot],
                    m_UiTimingGeneration,
                    m_TimerSourceFrames[slot],
                    m_LastAcceptedUiTimingSourceFrame,
                    m_LastAcceptedUiTimingSourceFrameValid) &&
                (!hasNewestUiSample ||
                    m_TimerSourceFrames[slot] > newestUiFrame))
            {
                hasNewestUiSample = true;
                newestUiFrame = m_TimerSourceFrames[slot];
                newestUiSample = sample;
            }

            ReadDebugCounters(slot);
            if (sample.sourceTag != 0u)
            {
                if (m_TimingAccounting.outstanding > 0u)
                    --m_TimingAccounting.outstanding;
                ++m_TimingAccounting.retired;
                if (m_CompletedTimingSamples.size() <
                    c_MaxCompletedTimingSamples)
                {
                    m_CompletedTimingSamples.push_back(sample);
                }
                else
                {
                    ++m_TimingAccounting.dropped;
                }
            }

            m_TimerIssuedStageMasks[slot] = 0u;
            m_TimerSourceTags[slot] = 0u;
            m_TimerSourceFrames[slot] = 0u;
            m_TimerUiTimingGenerations[slot] = 0u;
            m_TimerSlotValues[slot].fill(0.f);
        }

        if (hasNewestUiSample)
        {
            m_Timings.pageMarkingMilliseconds =
                newestUiSample.pageMarkingMilliseconds;
            m_Timings.allocationMilliseconds =
                newestUiSample.allocationMilliseconds;
            m_Timings.clearingMilliseconds =
                newestUiSample.clearingMilliseconds;
            m_Timings.packetPageCullingMilliseconds =
                newestUiSample.packetPageCullingMilliseconds;
            m_Timings.pageRenderingMilliseconds =
                newestUiSample.pageRenderingMilliseconds;
            m_Timings.filteringMilliseconds =
                newestUiSample.filteringMilliseconds;
            m_Timings.totalMilliseconds =
                newestUiSample.totalMilliseconds;
            m_Timings.detailedGpuTimingEnabled =
                newestUiSample.detailedGpuTimingEnabled;
            m_Timings.gpuTimingSource =
                SvsmGpuTimingSource::TimerQuery;
            m_LastAcceptedUiTimingSourceFrame = newestUiFrame;
            m_LastAcceptedUiTimingSourceFrameValid = true;
        }

        if (m_Timings.gpuTimingSource !=
                SvsmGpuTimingSource::Unavailable &&
            m_LastAcceptedUiTimingSourceFrameValid)
        {
            const uint64_t ageFrames =
                m_TimerFrame >= m_LastAcceptedUiTimingSourceFrame
                ? m_TimerFrame - m_LastAcceptedUiTimingSourceFrame
                : 0u;
            m_Timings.gpuTimingAgeFrames = uint32_t(std::min(
                ageFrames,
                uint64_t(std::numeric_limits<uint32_t>::max())));
        }
    }

    void SparseVirtualShadowMapPass::InvalidateDebugCounters()
    {
        ++m_DebugCounterGeneration;
        if (m_DebugCounterGeneration == 0u)
            m_DebugCounterGeneration = 1u;

        m_LastAcceptedDebugCounterSourceFrame = 0u;
        m_LastAcceptedDebugCounterSourceFrameValid = false;
        m_Timings.debugCountersAvailable = false;
        m_Timings.debugCounterAgeFrames = 0u;
        m_Timings.requiredPages = 0u;
        m_Timings.residentPages = 0u;
        m_Timings.cachedPages = 0u;
        m_Timings.dirtyPages = 0u;
        m_Timings.renderedPages = 0u;
        m_Timings.outOfRangePixels = 0u;
        m_Timings.allocationFailures = 0u;
        m_Timings.resolveMissingPixels = 0u;
        m_Timings.overBudgetPages = 0u;
        m_Timings.fallbackPixels = 0u;
        m_Timings.packetPageCandidatePackets = 0u;
        m_Timings.packetPageCompactedPackets = 0u;
        m_Timings.packetPageFailOpenPackets = 0u;
    }

    void SparseVirtualShadowMapPass::SetDebugCounterRequestedBackend(
        SvsmResourceBackend backend)
    {
        if (m_DebugCounterRequestedBackend == backend)
            return;

        m_DebugCounterRequestedBackend = backend;
        InvalidateDebugCounters();
    }

    void SparseVirtualShadowMapPass::ReadDebugCounters(uint32_t slot)
    {
        if (!m_DebugCounterReadbackPending[slot] ||
            !m_DebugCounterReadbacks[slot])
        {
            return;
        }

        const uint64_t generation =
            m_DebugCounterReadbackGenerations[slot];
        const uint64_t sourceFrame =
            m_DebugCounterReadbackSourceFrames[slot];
        m_DebugCounterReadbackPending[slot] = false;
        m_DebugCounterReadbackGenerations[slot] = 0u;
        m_DebugCounterReadbackSourceFrames[slot] = 0u;

        void* mapped = m_Device->mapBuffer(
            m_DebugCounterReadbacks[slot],
            nvrhi::CpuAccessMode::Read);
        if (!mapped)
            return;

        const bool publish =
            ShouldAcceptSvsmTelemetrySample(
                generation,
                m_DebugCounterGeneration,
                sourceFrame,
                m_LastAcceptedDebugCounterSourceFrame,
                m_LastAcceptedDebugCounterSourceFrameValid) &&
            m_DebugCounterRequestedBackend ==
                SvsmResourceBackend::Sparse;
        if (publish)
        {
            const auto* counters =
                static_cast<const uint32_t*>(mapped);
            m_Timings.requiredPages = counters[0];
            m_Timings.residentPages = counters[4];
            m_Timings.renderedPages = counters[3];
            m_Timings.outOfRangePixels = counters[5];
            m_Timings.fallbackPixels = counters[7];
            m_Timings.cachedPages = counters[9];
            m_Timings.dirtyPages = counters[10];
            m_Timings.overBudgetPages = counters[11];
            m_Timings.allocationFailures = counters[12];
            m_Timings.resolveMissingPixels = counters[13];
            m_Timings.packetPageCandidatePackets = counters[14];
            m_Timings.packetPageCompactedPackets = counters[15];
            m_Timings.packetPageFailOpenPackets = counters[16];
            m_LastAcceptedDebugCounterSourceFrame = sourceFrame;
            m_LastAcceptedDebugCounterSourceFrameValid = true;
            const uint64_t ageFrames = m_TimerFrame >= sourceFrame
                ? m_TimerFrame - sourceFrame
                : 0u;
            m_Timings.debugCounterAgeFrames = uint32_t(std::min(
                ageFrames,
                uint64_t(std::numeric_limits<uint32_t>::max())));
            m_Timings.debugCountersAvailable = true;
        }
        m_Device->unmapBuffer(m_DebugCounterReadbacks[slot]);
    }

    void SparseVirtualShadowMapPass::BeginTimerFrame(
        uint64_t sourceTag,
        bool detailedGpuTimingEnabled)
    {
        m_CurrentTimerSourceTag = sourceTag;
        m_CurrentDetailedGpuTimingEnabled = detailedGpuTimingEnabled;
        m_Timings.detailedGpuTimingEnabled = detailedGpuTimingEnabled;
        m_CurrentTimerSlot = uint32_t(m_TimerFrame % c_TimerLatency);
        m_CurrentTimerIssuedStageMask = 0u;
        m_TimerFrameAdmitted =
            m_TimerIssuedStageMasks[m_CurrentTimerSlot] == 0u;
        for (uint32_t stage = 0u;
            stage < c_TimerStageCount && m_TimerFrameAdmitted;
            ++stage)
        {
            m_TimerFrameAdmitted =
                !m_TimerPending[stage][m_CurrentTimerSlot];
        }
        m_TimerFrameDropRecorded = false;
        m_TimerStageActive.fill(false);

        if (!m_TimerFrameAdmitted && sourceTag != 0u)
        {
            ++m_TimingAccounting.dropped;
            m_TimerFrameDropRecorded = true;
        }
        if (m_TimerFrameAdmitted)
        {
            m_TimerSlotValues[m_CurrentTimerSlot].fill(0.f);
            m_TimerUiTimingGenerations[m_CurrentTimerSlot] = 0u;
        }
    }

    void SparseVirtualShadowMapPass::EndTimerFrame()
    {
        if (m_CurrentTimerSourceTag != 0u &&
            m_CurrentTimerIssuedStageMask == 0u &&
            !m_TimerFrameDropRecorded)
        {
            ++m_TimingAccounting.dropped;
        }
        m_TimerFrameAdmitted = false;
        m_CurrentTimerSourceTag = 0u;
        m_CurrentTimerIssuedStageMask = 0u;
        m_TimerFrameDropRecorded = false;
        ++m_TimerFrame;
    }

    void SparseVirtualShadowMapPass::BeginTimer(
        nvrhi::ICommandList* commandList,
        uint32_t stage)
    {
        if (!m_TimerFrameAdmitted ||
            !ShouldIssueSvsmGpuTimerStage(
                m_CurrentDetailedGpuTimingEnabled,
                stage == TimerTotal))
            return;
        commandList->beginTimerQuery(
            m_TimerQueries[stage][m_CurrentTimerSlot]);
        m_TimerStageActive[stage] = true;
        m_CurrentTimerIssuedStageMask |= 1u << stage;
    }

    void SparseVirtualShadowMapPass::EndTimer(
        nvrhi::ICommandList* commandList,
        uint32_t stage)
    {
        if (!m_TimerStageActive[stage])
            return;
        commandList->endTimerQuery(
            m_TimerQueries[stage][m_CurrentTimerSlot]);
        m_TimerPending[stage][m_CurrentTimerSlot] = true;
        m_TimerStageActive[stage] = false;
        if (stage == TimerTotal)
        {
            m_TimerIssuedStageMasks[m_CurrentTimerSlot] =
                m_CurrentTimerIssuedStageMask;
            m_TimerSourceTags[m_CurrentTimerSlot] =
                m_CurrentTimerSourceTag;
            m_TimerSourceFrames[m_CurrentTimerSlot] = m_TimerFrame;
            m_TimerUiTimingGenerations[m_CurrentTimerSlot] =
                m_UiTimingGeneration;
            m_TimerDetailedStagesEnabled[m_CurrentTimerSlot] =
                m_CurrentDetailedGpuTimingEnabled;
            if (m_CurrentTimerSourceTag != 0u)
            {
                ++m_TimingAccounting.issued;
                ++m_TimingAccounting.outstanding;
            }
        }
    }

    SparseVirtualShadowMapResult SparseVirtualShadowMapPass::Render(
        nvrhi::ICommandList* commandList,
        const SparseVirtualShadowMapSettings& settings,
        const IView& cameraView,
        nvrhi::ITexture* cameraDepth,
        const DirectionalLight* light,
        const std::shared_ptr<SceneGraphNode>& rootNode,
        uint64_t sceneStateRevision,
        bool sceneStateRevisionReliable,
        InstancedOpaqueDrawStrategy& drawStrategy,
        uint64_t timingSourceTag,
        bool forceTotalOnlyGpuTiming)
    {
        const auto totalCpuStart =
            std::chrono::steady_clock::now();
        auto finishCpuTiming = [this, &totalCpuStart]() {
            m_Timings.totalCpuMilliseconds =
                std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() -
                    totalCpuStart).count();
        };
        AdvanceTimers();
        const SvsmResourceBackend requestedDebugCounterBackend =
            settings.enabled &&
                settings.debugView != SvsmDebugView::None
            ? (settings.mode == SvsmMode::DenseReference
                ? SvsmResourceBackend::Dense
                : SvsmResourceBackend::Sparse)
            : SvsmResourceBackend::None;
        SetDebugCounterRequestedBackend(
            requestedDebugCounterBackend);
        if (m_Timings.debugCountersAvailable &&
            m_LastAcceptedDebugCounterSourceFrameValid)
        {
            const uint64_t ageFrames =
                m_TimerFrame >= m_LastAcceptedDebugCounterSourceFrame
                ? m_TimerFrame -
                    m_LastAcceptedDebugCounterSourceFrame
                : 0u;
            m_Timings.debugCounterAgeFrames = uint32_t(std::min(
                ageFrames,
                uint64_t(std::numeric_limits<uint32_t>::max())));
        }
        BeginTimerFrame(
            timingSourceTag,
            IsDetailedSvsmGpuTimingEnabled(
                settings.detailedGpuTimingEnabled,
                forceTotalOnlyGpuTiming));
        m_Timings.active = false;
        m_Timings.staticPageRequestReuseActive = false;
        m_Timings.staticPageDrainActive = false;
        m_Timings.staticPageDrainFramesRemaining = 0u;
        m_Timings.staticVisibilityReuseActive = false;
        m_Timings.batchedDrawSupported = false;
        m_Timings.batchedDrawActive = false;
        m_Timings.packetStateSortingActive = false;
        m_Timings.levelEmptyWorkSkipActive = false;
        m_Timings.packetPageCullingActive = false;
        m_Timings.dirtyPageScatterRasterActive = false;
        m_Timings.packetPageCullingUnavailable = false;
        m_Timings.staticPageRequestReuseRejectMask = 0u;
        m_Timings.sceneValidationCpuMilliseconds = 0.f;
        m_Timings.clipmapUpdateCpuMilliseconds = 0.f;

        if (!settings.enabled ||
            !commandList ||
            !cameraDepth ||
            !light ||
            !rootNode ||
            !m_Timings.supported)
        {
            InvalidateUiTimings();
            InvalidateDebugCounters();
            EndTimerFrame();
            finishCpuTiming();
            return {};
        }

        if (!light->GetNode())
        {
            InvalidateUiTimings();
            InvalidateDebugCounters();
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "SVSM requires an attached directional-light scene node.");
                m_ReportedInvalidInput = true;
            }
            EndTimerFrame();
            finishCpuTiming();
            return {};
        }

        if (!ValidateSvsmSettings(settings))
        {
            InvalidateUiTimings();
            InvalidateDebugCounters();
            if (!m_ReportedInvalidInput)
            {
                log::error("SVSM settings are invalid; visibility remains white.");
                m_ReportedInvalidInput = true;
            }
            EndTimerFrame();
            finishCpuTiming();
            return {};
        }

        const auto sceneValidationStart =
            std::chrono::steady_clock::now();
        const bool reuseSceneStateHash =
            settings.sceneStateCachingEnabled &&
            sceneStateRevisionReliable &&
            m_CachedSceneStateRoot == rootNode.get() &&
            m_CachedSceneStateRevision == sceneStateRevision;
        const uint64_t sceneStateHash = reuseSceneStateHash
            ? m_CachedSceneStateHash
            : ComputeSceneStateHash(rootNode);
        m_CachedSceneStateRoot = rootNode.get();
        m_CachedSceneStateRevision = sceneStateRevision;
        m_CachedSceneStateHash = sceneStateHash;
        m_Timings.sceneValidationCpuMilliseconds =
            std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() -
                sceneValidationStart).count();
        SparseVirtualShadowMapResult result =
            settings.mode == SvsmMode::DenseReference
            ? RenderDense(
                commandList,
                settings,
                cameraView,
                cameraDepth,
                light,
                rootNode,
                drawStrategy)
            : RenderSparse(
                commandList,
                settings,
                cameraView,
                cameraDepth,
                light,
                rootNode,
                drawStrategy,
                sceneStateHash);
        finishCpuTiming();
        return result;
    }

    SparseVirtualShadowMapResult SparseVirtualShadowMapPass::RenderDense(
        nvrhi::ICommandList* commandList,
        const SparseVirtualShadowMapSettings& settings,
        const IView& cameraView,
        nvrhi::ITexture* cameraDepth,
        const DirectionalLight* light,
        const std::shared_ptr<SceneGraphNode>& rootNode,
        InstancedOpaqueDrawStrategy& drawStrategy)
    {
        const nvrhi::TextureDesc& cameraDepthDesc =
            cameraDepth->getDesc();
        if (!IsDenseInputValid(cameraView, cameraDepthDesc) ||
            !EnsureDenseResources(cameraDepth))
        {
            InvalidateUiTimings();
            InvalidateDebugCounters();
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "SVSM dense reference requires reverse-Z, non-empty, single-sample 2D camera depth.");
                m_ReportedInvalidInput = true;
            }
            EndTimerFrame();
            return {};
        }

        m_ReportedUnsupportedMode = false;
        const auto clipmapUpdateStart =
            std::chrono::steady_clock::now();
        const bool clipmapViewsValid = UpdateClipmapViews(
            settings, cameraView, *light, rootNode);
        m_Timings.clipmapUpdateCpuMilliseconds =
            std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() -
                clipmapUpdateStart).count();
        if (!clipmapViewsValid)
        {
            InvalidateUiTimings();
            InvalidateDebugCounters();
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "SVSM clipmap mapping is non-finite or outside its representable page range; visibility remains white.");
                m_ReportedInvalidInput = true;
            }
            EndTimerFrame();
            return {};
        }
        m_ReportedInvalidInput = false;

        UpdateUiTimingContext(
            settings,
            SvsmResourceBackend::Dense,
            m_CurrentDetailedGpuTimingEnabled);
        commandList->beginMarker("SVSM Dense Reference");
        BeginTimer(commandList, TimerTotal);
        m_Timings.pageMarkingMilliseconds = 0.f;
        m_Timings.allocationMilliseconds = 0.f;
        m_Timings.clearingMilliseconds = 0.f;
        m_Timings.packetPageCullingMilliseconds = 0.f;
        m_Timings.cullingCpuMilliseconds = 0.f;
        const uint32_t firstLevel =
            GetSvsmFirstClipmapLevel(settings.resolutionBias);
        BeginTimer(commandList, TimerPageRendering);
        commandList->clearTextureUInt(
            m_DenseDepth, nvrhi::AllSubresources, 0u);

        for (uint32_t level = firstLevel;
            level < SvsmClipmapCount;
            ++level)
        {
            DenseDepthPass::Context context;
            m_DenseDepthPass->SelectSlice(level);
            drawStrategy.PrepareForView(
                rootNode, *m_ClipmapViews[level]);
            RenderView(
                commandList,
                m_ClipmapViews[level].get(),
                m_ClipmapViews[level].get(),
                m_RasterFramebuffer,
                drawStrategy,
                *m_DenseDepthPass,
                context,
                false);
        }
        EndTimer(commandList, TimerPageRendering);

        SparseVirtualShadowMapResolveConstants constants = {};
        cameraView.FillPlanarViewConstants(constants.cameraView);
        float extent = settings.firstClipmapExtent;
        for (uint32_t level = 0u;
            level < SvsmClipmapCount;
            ++level)
        {
            constants.worldToClip[level] =
                m_ClipmapViews[level]->GetViewProjectionMatrix(false);
            constants.clipmapExtentAndTexelSize[level] = float4(
                extent,
                extent / float(SvsmVirtualResolution),
                settings.maximumLightDepth,
                0.f);
            extent *= 2.f;
        }
        constants.outputSize = uint2(
            cameraDepthDesc.width, cameraDepthDesc.height);
        constants.tapCount = uint32_t(settings.tapCount);
        constants.resolutionBias =
            uint32_t(settings.resolutionBias);
        constants.depthBias = 0.0001f;
        constants.debugView = uint32_t(settings.debugView);
        constants.filterMode =
            uint32_t(settings.filterMode);
        constants.adaptiveFiltering =
            settings.adaptiveFiltering ? 1u : 0u;

        BeginTimer(commandList, TimerFiltering);
        commandList->writeBuffer(
            m_ResolveConstants, &constants, sizeof(constants));
        nvrhi::ComputeState resolveState;
        resolveState.pipeline = m_ResolvePipeline;
        resolveState.bindings = { m_ResolveBindingSet };
        commandList->setComputeState(resolveState);
        commandList->dispatch(
            div_ceil(cameraDepthDesc.width, 8u),
            div_ceil(cameraDepthDesc.height, 8u));
        EndTimer(commandList, TimerFiltering);
        EndTimer(commandList, TimerTotal);
        commandList->endMarker();

        m_Timings.active = true;
        const uint32_t activePageCount =
            SvsmPagesPerClipmap *
            (SvsmClipmapCount - firstLevel);
        m_Timings.requiredPages = activePageCount;
        m_Timings.residentPages =
            SvsmPagesPerClipmap * SvsmClipmapCount;
        m_Timings.cachedPages = 0u;
        m_Timings.dirtyPages = 0u;
        m_Timings.renderedPages = activePageCount;
        m_Timings.outOfRangePixels = 0u;
        m_Timings.allocationFailures = 0u;
        m_Timings.resolveMissingPixels = 0u;
        m_Timings.overBudgetPages = 0u;
        m_Timings.fallbackPixels = 0u;
        m_Timings.packetPageCandidatePackets = 0u;
        m_Timings.packetPageCompactedPackets = 0u;
        m_Timings.packetPageFailOpenPackets = 0u;
        // Dense mode has deterministic page totals but no GPU counter
        // readback. Do not present its zero-valued pixel diagnostics as
        // measurements.
        m_Timings.debugCountersAvailable = false;
        m_Timings.debugCounterAgeFrames = 0u;
        EndTimerFrame();
        return {
            m_Visibility,
            light,
            settings.debugView != SvsmDebugView::None
        };
    }

    SparseVirtualShadowMapResult SparseVirtualShadowMapPass::RenderSparse(
        nvrhi::ICommandList* commandList,
        const SparseVirtualShadowMapSettings& settings,
        const IView& cameraView,
        nvrhi::ITexture* cameraDepth,
        const DirectionalLight* light,
        const std::shared_ptr<SceneGraphNode>& rootNode,
        InstancedOpaqueDrawStrategy& drawStrategy,
        uint64_t sceneStateHash)
    {
        const nvrhi::TextureDesc& cameraDepthDesc =
            cameraDepth->getDesc();
        if (!IsDenseInputValid(cameraView, cameraDepthDesc) ||
            !EnsureSparseResources(
                cameraDepth,
                settings.physicalPageCount))
        {
            InvalidateUiTimings();
            InvalidateDebugCounters();
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "SVSM sparse mode requires reverse-Z, non-empty, single-sample 2D camera depth and a valid fixed pool.");
                m_ReportedInvalidInput = true;
            }
            EndTimerFrame();
            return {};
        }

        m_ReportedUnsupportedMode = false;
        const auto clipmapUpdateStart =
            std::chrono::steady_clock::now();
        const bool clipmapViewsValid = UpdateClipmapViews(
            settings, cameraView, *light, rootNode);
        m_Timings.clipmapUpdateCpuMilliseconds =
            std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() -
                clipmapUpdateStart).count();
        if (!clipmapViewsValid)
        {
            InvalidateUiTimings();
            InvalidateDebugCounters();
            if (!m_ReportedInvalidInput)
            {
                log::error(
                    "SVSM clipmap mapping is non-finite or outside its representable page range; visibility remains white.");
                m_ReportedInvalidInput = true;
            }
            EndTimerFrame();
            return {};
        }
        m_ReportedInvalidInput = false;

        const bool cacheEnabled =
            settings.mode == SvsmMode::SparseCached &&
            settings.cachingEnabled;
        const nvrhi::Viewport cameraViewport =
            cameraView.GetViewportState().viewports[0];
        const float2 cameraPixelOffset =
            cameraView.GetPixelOffset();
        constexpr float StaticReuseMaximumPixelOffset = 0.5f;
        const bool staticReuseJitterSupported =
            all(abs(cameraPixelOffset) <=
                float2(StaticReuseMaximumPixelOffset));
        const bool staticBudgetSupportsPageRequestReuse =
            settings.pageRenderBudget >= settings.physicalPageCount ||
            (settings.finiteBudgetStaticDrainEnabled &&
                settings.pageRenderBudget > 0u);
        const bool staticPageRequestConfiguration =
            CanUseSvsmStaticPageRequestConfiguration(
                settings.staticPageRequestReuseEnabled,
                cacheEnabled,
                settings.physicalPageCount,
                settings.pageRenderBudget,
                staticReuseJitterSupported,
                settings.finiteBudgetStaticDrainEnabled);
        const affine3 lightView =
            m_ClipmapViews[0]->GetViewMatrix();
        const std::array<float3, 3> lightBasis = {
            lightView.m_linear.row0,
            lightView.m_linear.row1,
            lightView.m_linear.row2
        };
        const bool lightBasisChanged =
            !m_PreviousLightBasisValid ||
            any(lightBasis[0] != m_PreviousLightBasis[0]) ||
            any(lightBasis[1] != m_PreviousLightBasis[1]) ||
            any(lightBasis[2] != m_PreviousLightBasis[2]);
        const bool mappingChanged =
            !m_CacheStateValid ||
            light != m_PreviousProducingLight ||
            lightBasisChanged ||
            m_CurrentLightDepthOrigin !=
                m_PreviousLightDepthOrigin ||
            sceneStateHash != m_PreviousSceneStateHash ||
            settings.firstClipmapExtent !=
                m_PreviousFirstClipmapExtent ||
            settings.maximumLightDepth !=
                m_PreviousMaximumLightDepth;
        const bool fullInvalidation =
            m_SparseResourcesNeedClear ||
            !cacheEnabled ||
            mappingChanged;

        SparseVirtualShadowMapSparseConstants constants = {};
        cameraView.FillPlanarViewConstants(constants.cameraView);
        for (uint32_t level = 0u;
            level < SvsmClipmapCount;
            ++level)
        {
            constants.worldToClip[level] =
                m_ClipmapViews[level]->GetViewProjectionMatrix(false);
            const SvsmPageCoordinate currentOrigin = {
                m_CurrentRenderOrigins[level].x,
                m_CurrentRenderOrigins[level].y
            };
            const SvsmPageCoordinate previousOrigin = {
                m_PreviousRenderOrigins[level].x,
                m_PreviousRenderOrigins[level].y
            };
            const SvsmPageCoordinate tableOffset =
                SvsmPageTableOffsetForRenderOrigin(currentOrigin);
            const SvsmPageCoordinate tableDelta = m_CacheStateValid
                ? SvsmPageTableDeltaForRenderOrigins(
                    currentOrigin, previousOrigin)
                : SvsmPageCoordinate{};
            constants.pageTableOffsetAndDelta[level] = int4(
                tableOffset.x,
                tableOffset.y,
                tableDelta.x,
                tableDelta.y);
        }
        constants.cameraSize = uint2(
            cameraDepthDesc.width, cameraDepthDesc.height);
        constants.frameIndex =
            uint32_t(m_TimerFrame & SvsmPageAgeMask);
        constants.physicalPageCount =
            settings.physicalPageCount;
        constants.pageRenderBudget =
            settings.pageRenderBudget;
        constants.tapCount = uint32_t(settings.tapCount);
        constants.resolutionBias =
            uint32_t(settings.resolutionBias);
        constants.flags =
            (fullInvalidation
                ? SVSM_SPARSE_FLAG_FULL_INVALIDATION
                : 0u) |
            (cacheEnabled ? SVSM_SPARSE_FLAG_CACHING : 0u) |
            (settings.gpuGatedDrawSubmission
                ? SVSM_SPARSE_FLAG_COMPACT_PAGE_DISPATCH
                : 0u) |
            (cacheEnabled &&
                    settings.recentPageEvictionGraceEnabled
                ? SVSM_SPARSE_FLAG_RECENT_PAGE_EVICTION_GRACE
                : 0u) |
            (settings.perPixelMarkingDedupeEnabled &&
                    settings.markingMode ==
                        SvsmMarkingMode::PerPixel
                ? SVSM_SPARSE_FLAG_PER_PIXEL_MARKING_DEDUPE
                : 0u) |
            (ShouldEnableSvsmAllocationBudgetSaturationEarlyOut(
                    settings.allocationBudgetSaturationEarlyOutEnabled,
                    settings.pageRenderBudget)
                ? SVSM_SPARSE_FLAG_ALLOCATION_BUDGET_SATURATION_EARLY_OUT
                : 0u) |
            (settings.coarsestPageRenderBudgetEnabled
                ? SVSM_SPARSE_FLAG_COARSEST_PAGE_RENDER_BUDGET
                : 0u);
        constants.selectedClipmap = 0u;
        constants.depthBias = 0.0001f;
        constants.debugView = uint32_t(settings.debugView);
        constants.markingMode =
            uint32_t(settings.markingMode);
        constants.filterMode = uint32_t(settings.filterMode);
        constants.adaptiveFiltering =
            settings.adaptiveFiltering ? 1u : 0u;
        constants.drawPacketOffset = 0u;
        constants.drawPacketCount = 0u;
        constants.dirtyPageScatterMaximumAmplification = std::clamp(
            settings.dirtyPageScatterMaximumAmplification,
            1u,
            SvsmMaximumDirtyPageScatterAmplification);

        const float4x4 cameraWorldToClip =
            cameraView.GetViewProjectionMatrix(false);
        const bool staticJitterActive = IsSvsmStaticJitterActive(
            cameraPixelOffset.x,
            cameraPixelOffset.y);
        const bool resetStaticJitterCache =
            ShouldResetSvsmStaticJitterCache(
                m_StaticPageRequestCacheReady,
                m_StaticPageRequestJitterActive,
                cameraPixelOffset.x,
                cameraPixelOffset.y);
        const bool staticCameraKeyMatches =
            m_StaticPageRequestCacheReady &&
            !resetStaticJitterCache &&
            m_StaticPageRequestCameraDepth == cameraDepth &&
            m_StaticPageRequestWidth ==
                cameraDepthDesc.width &&
            m_StaticPageRequestHeight ==
                cameraDepthDesc.height &&
            m_StaticPageRequestViewport == cameraViewport &&
            m_StaticPageRequestMarkingMode ==
                settings.markingMode &&
            m_StaticPageRequestFilterMode ==
                settings.filterMode &&
            m_StaticPageRequestTapCount ==
                settings.tapCount &&
            m_StaticPageRequestResolutionBias ==
                settings.resolutionBias &&
            std::memcmp(
                &cameraWorldToClip,
                &m_StaticPageRequestCameraWorldToClip,
                sizeof(cameraWorldToClip)) == 0;
        const bool staticPageRequestStateCompatible =
            staticPageRequestConfiguration &&
            staticCameraKeyMatches &&
            !fullInvalidation;
        const bool staticPageRequestBudgetChanged =
            staticPageRequestStateCompatible &&
            (m_StaticPageRequestPageRenderBudget !=
                    settings.pageRenderBudget ||
                m_StaticPageRequestCoarsestPageRenderBudgetEnabled !=
                    settings.coarsestPageRenderBudgetEnabled);
        const bool staticPageMaintenancePending =
            staticPageRequestStateCompatible &&
            (staticPageRequestBudgetChanged ||
                m_StaticPageDrainFramesRemaining > 0u);
        if (!staticPageRequestStateCompatible)
        {
            m_StaticJitterOffsetValid.fill(false);
            m_StaticVisibilityValid.fill(false);
        }

        auto pixelOffsetsMatch = [](
            const float2& first,
            const float2& second) {
                return first.x == second.x &&
                    first.y == second.y;
            };
        uint32_t staticJitterSlot =
            c_StaticVisibilityCacheSlotCount;
        bool staticJitterPreviouslySeen = false;
        if (staticPageRequestStateCompatible)
        {
            for (uint32_t slot = 0u;
                slot < c_StaticVisibilityCacheSlotCount;
                ++slot)
            {
                if (m_StaticJitterOffsetValid[slot] &&
                    pixelOffsetsMatch(
                        m_StaticJitterOffsets[slot],
                        cameraPixelOffset))
                {
                    staticJitterSlot = slot;
                    staticJitterPreviouslySeen = true;
                    break;
                }
            }
        }
        if (staticJitterSlot ==
            c_StaticVisibilityCacheSlotCount)
        {
            for (uint32_t slot = 0u;
                slot < c_StaticVisibilityCacheSlotCount;
                ++slot)
            {
                if (!m_StaticJitterOffsetValid[slot])
                {
                    staticJitterSlot = slot;
                    break;
                }
            }
        }
        const SvsmStaticPageRequestAction staticPageRequestAction =
            SelectSvsmStaticPageRequestAction(
                staticPageRequestStateCompatible,
                staticJitterPreviouslySeen,
                staticPageMaintenancePending);
        const bool reuseStaticPageRequests =
            staticPageRequestAction ==
                SvsmStaticPageRequestAction::Reuse;
        const bool markStaticPageRequests =
            ShouldMarkSvsmStaticPageRequests(
                staticPageRequestAction);
        const bool performStaticPageMaintenance =
            ShouldMaintainSvsmStaticPages(
                staticPageRequestAction);
        const uint32_t staticPageDrainPassCount =
            staticPageRequestConfiguration
                ? GetSvsmStaticPageDrainPassCount(
                    settings.physicalPageCount,
                    settings.pageRenderBudget)
                : 0u;
        uint32_t staticPageDrainPassesIncludingThisFrame = 0u;
        if (performStaticPageMaintenance &&
            staticPageRequestConfiguration)
        {
            if (staticPageRequestAction ==
                    SvsmStaticPageRequestAction::Rebuild ||
                staticPageRequestAction ==
                    SvsmStaticPageRequestAction::ExtendUnion ||
                staticPageRequestBudgetChanged)
            {
                staticPageDrainPassesIncludingThisFrame =
                    staticPageDrainPassCount;
            }
            else
            {
                staticPageDrainPassesIncludingThisFrame =
                    m_StaticPageDrainFramesRemaining;
            }
        }
        const uint32_t staticPageDrainFramesRemainingAfterThisFrame =
            staticPageDrainPassesIncludingThisFrame > 0u
                ? staticPageDrainPassesIncludingThisFrame - 1u
                : 0u;
        if (ShouldInvalidateSvsmStaticVisibility(
                staticPageRequestAction))
        {
            // Rebuilding or extending the exact jitter union can allocate or
            // evict pages, changing fallback results for every resolved slot.
            m_StaticVisibilityValid.fill(false);
        }
        if (staticPageRequestAction ==
            SvsmStaticPageRequestAction::ExtendUnion)
        {
            constants.flags |= SVSM_SPARSE_FLAG_PRESERVE_REQUIRED;
        }
        const bool staticVisibilitySettingsMatch =
            m_StaticVisibilitySettingsValid &&
            m_StaticVisibilityFilterMode == settings.filterMode &&
            m_StaticVisibilityTapCount == settings.tapCount &&
            m_StaticVisibilityResolutionBias ==
                settings.resolutionBias &&
            m_StaticVisibilityPageTranslationCaching ==
                settings.pageTranslationCachingEnabled &&
            m_StaticVisibilityAdaptiveFiltering ==
                settings.adaptiveFiltering;
        if (!staticVisibilitySettingsMatch)
            m_StaticVisibilityValid.fill(false);
        const bool reuseStaticVisibility =
            CanReuseSvsmStaticVisibility(
                settings.staticVisibilityCachingEnabled,
                settings.debugView == SvsmDebugView::None,
                reuseStaticPageRequests,
                staticJitterSlot <
                        c_StaticVisibilityCacheSlotCount &&
                    m_StaticVisibilityValid[staticJitterSlot]);
        uint32_t staticPageRequestReuseRejectMask = 0u;
        staticPageRequestReuseRejectMask |=
            settings.staticPageRequestReuseEnabled ? 0u : (1u << 0u);
        staticPageRequestReuseRejectMask |=
            cacheEnabled ? 0u : (1u << 1u);
        staticPageRequestReuseRejectMask |=
            settings.physicalPageCount > 0u &&
                    settings.physicalPageCount <= SvsmPagesPerClipmap
                ? 0u
                : (1u << 2u);
        staticPageRequestReuseRejectMask |=
            staticBudgetSupportsPageRequestReuse
                ? 0u
                : (1u << 3u);
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestCacheReady ? 0u : (1u << 4u);
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestCameraDepth == cameraDepth
                ? 0u
                : (1u << 5u);
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestWidth == cameraDepthDesc.width &&
                    m_StaticPageRequestHeight == cameraDepthDesc.height
                ? 0u
                : (1u << 6u);
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestMarkingMode == settings.markingMode
                ? 0u
                : (1u << 7u);
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestFilterMode == settings.filterMode
                ? 0u
                : (1u << 8u);
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestTapCount == settings.tapCount
                ? 0u
                : (1u << 9u);
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestResolutionBias ==
                    settings.resolutionBias
                ? 0u
                : (1u << 10u);
        staticPageRequestReuseRejectMask |=
            std::memcmp(
                &cameraWorldToClip,
                &m_StaticPageRequestCameraWorldToClip,
                sizeof(cameraWorldToClip)) == 0
                ? 0u
                : (1u << 11u);
        staticPageRequestReuseRejectMask |=
            m_SparseResourcesNeedClear ? (1u << 12u) : 0u;
        staticPageRequestReuseRejectMask |=
            m_CacheStateValid ? 0u : (1u << 13u);
        staticPageRequestReuseRejectMask |=
            lightBasisChanged ? (1u << 14u) : 0u;
        staticPageRequestReuseRejectMask |=
            m_CurrentLightDepthOrigin !=
                    m_PreviousLightDepthOrigin
                ? (1u << 15u)
                : 0u;
        staticPageRequestReuseRejectMask |=
            sceneStateHash == m_PreviousSceneStateHash
                ? 0u
                : (1u << 16u);
        staticPageRequestReuseRejectMask |=
            settings.firstClipmapExtent ==
                    m_PreviousFirstClipmapExtent
                ? 0u
                : (1u << 17u);
        staticPageRequestReuseRejectMask |=
            settings.maximumLightDepth ==
                    m_PreviousMaximumLightDepth
                ? 0u
                : (1u << 18u);
        staticPageRequestReuseRejectMask |=
            light == m_PreviousProducingLight
                ? 0u
                : (1u << 19u);
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestViewport == cameraViewport
                ? 0u
                : (1u << 20u);
        staticPageRequestReuseRejectMask |=
            staticReuseJitterSupported ? 0u : (1u << 21u);
        staticPageRequestReuseRejectMask |=
            staticJitterPreviouslySeen ? 0u : (1u << 22u);
        m_Timings.staticPageRequestReuseActive =
            staticPageRequestStateCompatible &&
            !markStaticPageRequests;
        m_Timings.staticPageDrainActive =
            staticPageDrainFramesRemainingAfterThisFrame > 0u ||
            staticPageRequestAction ==
                SvsmStaticPageRequestAction::Drain;
        m_Timings.staticPageDrainFramesRemaining =
            staticPageDrainFramesRemainingAfterThisFrame;
        m_Timings.staticVisibilityReuseActive =
            reuseStaticVisibility;
        m_Timings.staticPageRequestReuseRejectMask =
            staticPageRequestReuseRejectMask;

        const bool useRenderPackets =
            (cacheEnabled && settings.renderPacketCachingEnabled) ||
            settings.gpuGatedDrawSubmission;
        const uint32_t firstScheduledClipmap = std::min(
            uint32_t(settings.resolutionBias),
            SvsmClipmapCount - 1u);
        bool useBatchedDrawSubmission =
            settings.gpuGatedDrawSubmission &&
            settings.batchedDrawSubmissionEnabled &&
            m_SparseDepthPass->SupportsBatchedDrawSubmission();
        m_Timings.batchedDrawSupported =
            m_SparseDepthPass->SupportsBatchedDrawSubmission();
        const bool packetStateSortingRequested =
            settings.packetStateSortingEnabled &&
            useBatchedDrawSubmission;
        bool renderPacketsRebuilt = false;
        bool indirectDrawBufferRecreated = false;
        bool packetPageBuffersRecreated = false;
        bool indirectArgumentTemplatesPrepared = false;
        const bool packetPageCullingRequested =
            settings.packetPageCullingEnabled &&
            settings.gpuGatedDrawSubmission;
        const bool dirtyPageScatterSafetyBounded =
            IsSvsmDirtyPageScatterSafetyBounded(
                settings.dirtyPageScatterAmplificationGuardEnabled,
                settings.coarsestPageRenderBudgetEnabled,
                settings.pageRenderBudget,
                settings.dirtyPageScatterMaximumAmplification);
        // Virtual scatter has two potentially expensive fail-open shapes: a
        // large virtual rectangle and a compact-page instance expansion. Only
        // activate it when the independent guard is enabled and one small
        // shared reservation hard-bounds every clipmap, including coarsest.
        // Otherwise retain the exact compact per-page reference path.
        const bool dirtyPageScatterRasterRequested =
            packetPageCullingRequested &&
            settings.dirtyPageScatterRasterEnabled &&
            dirtyPageScatterSafetyBounded;
        const bool exactPacketPageListsRequested =
            packetPageCullingRequested &&
            !dirtyPageScatterRasterRequested;
        if (!packetPageCullingRequested)
        {
            m_PacketPageCullingUnavailableForPacketCache = false;
        }
        bool usePacketPageCulling =
            packetPageCullingRequested &&
            m_PacketPageCullingReady;
        const bool packetPageModeTransition =
            RequiresSvsmPacketPageModeTransition(
                settings.gpuGatedDrawSubmission,
                packetPageCullingRequested,
                m_IndirectDrawArgumentsPacketPageCulling,
                m_PacketPageCullingReady,
                m_PacketPageCullingUnavailableForPacketCache,
                m_SparseDepthPass->HasRenderPacketCache(),
                m_SparseDepthPass->UsesExactPacketPageLists(),
                exactPacketPageListsRequested);
        std::vector<nvrhi::DrawIndexedIndirectArguments>
            indirectArgumentTemplates;
        std::vector<SparseVirtualShadowMapPacketMetadata>
            packetPageMetadata;
        bool packetPageMetadataUploadPending = false;
        float packetPreparationMilliseconds = 0.f;
        if (useRenderPackets &&
            (!reuseStaticPageRequests || packetPageModeTransition))
        {
            const auto packetPreparationStart =
                std::chrono::steady_clock::now();
            const bool allowPacketReuse =
                cacheEnabled &&
                settings.renderPacketCachingEnabled;
            if (!m_SparseDepthPass->PrepareRenderPackets(
                    rootNode,
                    m_ClipmapViews,
                    sceneStateHash,
                    drawStrategy,
                    firstScheduledClipmap,
                    packetPageCullingRequested,
                    exactPacketPageListsRequested,
                    dirtyPageScatterRasterRequested,
                    packetStateSortingRequested,
                    allowPacketReuse,
                    renderPacketsRebuilt))
            {
                InvalidateUiTimings();
                InvalidateDebugCounters();
                log::error(
                    "SVSM could not build the conservative caster packet cache.");
                EndTimerFrame();
                return {};
            }
            if (renderPacketsRebuilt)
            {
                m_PacketPageCullingReady = false;
                m_PacketPageCullingUnavailableForPacketCache = false;
            }
            if (useBatchedDrawSubmission &&
                !m_SparseDepthPass->PrepareBatchedPipelines(
                    m_RasterFramebuffer,
                    m_ClipmapViews))
            {
                useBatchedDrawSubmission = false;
            }
            if (settings.gpuGatedDrawSubmission)
            {
                if (!EnsureIndirectDrawCapacity(
                        m_SparseDepthPass->
                            GetRenderPacketCount(),
                        indirectDrawBufferRecreated))
                {
                    InvalidateUiTimings();
                    InvalidateDebugCounters();
                    log::error(
                        "SVSM could not allocate the persistent indirect draw packet buffer.");
                    EndTimerFrame();
                    return {};
                }
                if (packetPageCullingRequested &&
                    !m_PacketPageCullingUnavailableForPacketCache)
                {
                    const uint32_t packetPageEntryCount =
                        m_SparseDepthPass->
                            GetPacketPageEntryCount();
                    bool metadataReady =
                        m_SparseDepthPass->
                            SupportsPacketPageCulling();
                    const bool packetPageBuffersReady =
                        metadataReady &&
                        EnsurePacketPageCapacity(
                            m_SparseDepthPass->
                                GetRenderPacketCount(),
                            packetPageEntryCount,
                            packetPageBuffersRecreated);
                    const bool metadataUploadRequired =
                        renderPacketsRebuilt ||
                        packetPageBuffersRecreated ||
                        !m_PacketPageCullingReady;
                    if (packetPageBuffersReady &&
                        metadataUploadRequired)
                    {
                        metadataReady =
                            m_SparseDepthPass->GetPacketPageMetadata(
                                packetPageMetadata);
                        if (metadataReady &&
                            !packetPageMetadata.empty())
                        {
                            packetPageMetadataUploadPending = true;
                        }
                        if (metadataReady)
                            m_PacketPageCullingReady = true;
                    }
                    if (packetPageBuffersReady && metadataReady)
                    {
                        usePacketPageCulling =
                            m_PacketPageCullingReady;
                        m_ReportedPacketPageCullingFallback = false;
                    }
                    else
                    {
                        usePacketPageCulling = false;
                        m_PacketPageCullingReady = false;
                        m_PacketPageCullingUnavailableForPacketCache =
                            true;
                        if (!m_ReportedPacketPageCullingFallback)
                        {
                            log::warning(
                                "SVSM packet-page culling exceeded its conservative memory or dispatch limits, or could not allocate resources; retaining the full dirty-page path.");
                            m_ReportedPacketPageCullingFallback = true;
                        }
                    }
                }
                else
                {
                    usePacketPageCulling = false;
                    if (!packetPageCullingRequested)
                    {
                        m_PacketPageCullingUnavailableForPacketCache =
                            false;
                    }
                }
                if (renderPacketsRebuilt ||
                    indirectDrawBufferRecreated ||
                    !m_IndirectDrawArgumentsInitialized ||
                    m_IndirectDrawArgumentsBatched !=
                        useBatchedDrawSubmission ||
                    m_IndirectDrawArgumentsPacketPageCulling !=
                        usePacketPageCulling)
                {
                    m_SparseDepthPass->BuildIndirectArguments(
                        indirectArgumentTemplates,
                        useBatchedDrawSubmission,
                        usePacketPageCulling);
                    indirectArgumentTemplatesPrepared = true;
                }
            }
            const auto packetPreparationEnd =
                std::chrono::steady_clock::now();
            packetPreparationMilliseconds =
                std::chrono::duration<float, std::milli>(
                    packetPreparationEnd -
                    packetPreparationStart).count();
        }
        const bool useDirtyPageScatterRaster =
            dirtyPageScatterRasterRequested &&
            usePacketPageCulling;
        if (usePacketPageCulling)
        {
            constants.flags |= SVSM_SPARSE_FLAG_PACKET_PAGE_CULLING;
            if (settings.packetRectangleDirectScanEnabled &&
                !useDirtyPageScatterRaster)
            {
                constants.flags |=
                    SVSM_SPARSE_FLAG_PACKET_RECTANGLE_DIRECT_SCAN;
            }
        }
        if (useDirtyPageScatterRaster)
        {
            constants.flags |=
                SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_RASTER;
            if (settings.scatterAlphaTestEarlyRejectEnabled)
            {
                constants.flags |=
                    SVSM_SPARSE_FLAG_SCATTER_ALPHA_TEST_EARLY_REJECT;
            }
            if (settings.dirtyPageScatterAmplificationGuardEnabled)
            {
                constants.flags |=
                    SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_AMPLIFICATION_GUARD;
            }
        }
        const bool useLevelEmptyWorkSkip =
            settings.levelEmptyWorkSkipEnabled &&
            useBatchedDrawSubmission;
        if (useLevelEmptyWorkSkip)
        {
            constants.flags |=
                SVSM_SPARSE_FLAG_LEVEL_EMPTY_WORK_SKIP;
        }
        const bool useStaticZeroWorkFastPath =
            CanUseSvsmStaticZeroWorkFastPath(
                reuseStaticPageRequests,
                reuseStaticVisibility,
                packetPageMetadataUploadPending,
                indirectArgumentTemplatesPrepared,
                m_CurrentTimerSourceTag != 0u);
        m_Timings.packetPageCullingActive =
            IsSvsmStaticPageMaintenanceOptimizationActive(
                usePacketPageCulling,
                staticPageRequestAction);
        m_Timings.dirtyPageScatterRasterActive =
            IsSvsmStaticPageMaintenanceOptimizationActive(
                useDirtyPageScatterRaster,
                staticPageRequestAction);
        m_Timings.batchedDrawSupported =
            m_SparseDepthPass->SupportsBatchedDrawSubmission();
        m_Timings.batchedDrawActive = useBatchedDrawSubmission;
        m_Timings.packetStateSortingActive =
            packetStateSortingRequested && useBatchedDrawSubmission;
        m_Timings.levelEmptyWorkSkipActive =
            useLevelEmptyWorkSkip;
        m_Timings.packetPageCullingUnavailable =
            m_PacketPageCullingUnavailableForPacketCache;
        UpdateUiTimingContext(
            settings,
            SvsmResourceBackend::Sparse,
            m_CurrentDetailedGpuTimingEnabled);

        const uint32_t resolveVisibilitySlot =
            settings.staticVisibilityCachingEnabled &&
                staticJitterSlot <
                    c_StaticVisibilityCacheSlotCount
            ? staticJitterSlot
            : 0u;
        if (useStaticZeroWorkFastPath)
        {
            // The exact page-request union and exact full-resolution R8
            // visibility slice are already valid. Avoid even the otherwise
            // empty GPU marker/query pair: NVRHI resolves timer queries to a
            // readback buffer, which is measurable bookkeeping on a true
            // zero-work static frame.
            m_Timings.pageMarkingMilliseconds = 0.f;
            m_Timings.allocationMilliseconds = 0.f;
            m_Timings.clearingMilliseconds = 0.f;
            m_Timings.packetPageCullingMilliseconds = 0.f;
            m_Timings.pageRenderingMilliseconds = 0.f;
            m_Timings.filteringMilliseconds = 0.f;
            m_Timings.totalMilliseconds = 0.f;
            m_Timings.cullingCpuMilliseconds =
                packetPreparationMilliseconds;
            m_Timings.renderedPages = 0u;
            m_Timings.allocationFailures = 0u;
            m_Timings.overBudgetPages = 0u;
            m_Timings.packetPageCandidatePackets = 0u;
            m_Timings.packetPageCompactedPackets = 0u;
            m_Timings.packetPageFailOpenPackets = 0u;
            PublishKnownZeroUiTiming();
            m_Timings.active = true;
            EndTimerFrame();
            return {
                m_SparseVisibilityCache[resolveVisibilitySlot],
                light,
                false
            };
        }

        commandList->beginMarker(
            cacheEnabled ? "SVSM Sparse Cached" : "SVSM Sparse Uncached");
#ifdef _WIN32
        SetD3d12DredMarker(commandList, L"SVSM Sparse Begin");
#endif
        BeginTimer(commandList, TimerTotal);

        if (packetPageMetadataUploadPending)
        {
            const auto metadataUploadStart =
                std::chrono::steady_clock::now();
            commandList->writeBuffer(
                m_PacketPageMetadata,
                packetPageMetadata.data(),
                uint64_t(packetPageMetadata.size()) *
                    sizeof(SparseVirtualShadowMapPacketMetadata));
            packetPreparationMilliseconds +=
                std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() -
                    metadataUploadStart).count();
        }

        if (m_SparseResourcesNeedClear)
        {
#ifdef _WIN32
            SetD3d12DredMarker(
                commandList,
                L"SVSM Clear Persistent Resources");
#endif
            commandList->clearTextureUInt(
                m_PageTable, nvrhi::AllSubresources, 0u);
            commandList->clearTextureUInt(
                m_SparsePhysicalDepth,
                nvrhi::AllSubresources,
                0u);
            commandList->clearBufferUInt(
                m_PhysicalOwners, SvsmInvalidPhysicalPage);
        }
        if (performStaticPageMaintenance ||
            settings.debugView != SvsmDebugView::None)
        {
            commandList->clearBufferUInt(
                m_RenderPages, SvsmInvalidPhysicalPage);
            commandList->clearBufferUInt(m_Counters, 0u);
        }

        auto dispatchSparse =
            [this, commandList, &constants](
                uint32_t stage,
                uint32_t x,
                uint32_t y,
                uint32_t z = 1u) {
                commandList->writeBuffer(
                    m_SparseConstants,
                    &constants,
                    sizeof(constants));
                nvrhi::ComputeState state;
                state.pipeline = m_SparsePipelines[stage];
                state.bindings = { m_SparseBindingSet };
                commandList->setComputeState(state);
                commandList->dispatch(x, y, z);
            };

        // A toggle can change the persistent indirect encoding while a
        // static frame legitimately skips all page work. Publish that new
        // template once so the next dirty frame does not need repeated CPU
        // packet preparation and cannot observe a stale encoding.
        if (reuseStaticPageRequests &&
            settings.gpuGatedDrawSubmission &&
            indirectArgumentTemplatesPrepared)
        {
            if (!indirectArgumentTemplates.empty())
            {
                commandList->writeBuffer(
                    m_IndirectDrawArguments,
                    indirectArgumentTemplates.data(),
                    uint64_t(indirectArgumentTemplates.size()) *
                        sizeof(
                            nvrhi::DrawIndexedIndirectArguments));
            }
            m_IndirectDrawArgumentsInitialized = true;
            m_IndirectDrawArgumentsBatched =
                useBatchedDrawSubmission;
            m_IndirectDrawArgumentsPacketPageCulling =
                usePacketPageCulling;
        }

        if (performStaticPageMaintenance)
        {
        if (useDirtyPageScatterRaster)
        {
            commandList->clearBufferUInt(
                m_DirtyPageRectangles, 0u);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_DirtyPageRectangles);
            commandList->commitBarriers();
        }
        if (markStaticPageRequests)
        {
#ifdef _WIN32
            SetD3d12DredMarker(commandList, L"SVSM Mark Required Pages");
#endif
            BeginTimer(commandList, TimerPageMarking);
            dispatchSparse(
                SparsePrepare,
                div_ceil(SvsmPagesPerAxis, 8u),
                div_ceil(SvsmPagesPerAxis, 8u),
                SvsmClipmapCount);
            nvrhi::utils::TextureUavBarrier(
                commandList, m_PageTable);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_PhysicalOwners);
            commandList->commitBarriers();
            const uint32_t markingGroupCoverage =
                settings.markingMode == SvsmMarkingMode::Tile16
                    ? 16u
                    : 8u;
            dispatchSparse(
                SparseMark,
                div_ceil(cameraDepthDesc.width, markingGroupCoverage),
                div_ceil(cameraDepthDesc.height, markingGroupCoverage));
            nvrhi::utils::TextureUavBarrier(
                commandList, m_PageTable);
            commandList->commitBarriers();
            EndTimer(commandList, TimerPageMarking);
        }
        else
        {
            m_Timings.pageMarkingMilliseconds = 0.f;
        }

#ifdef _WIN32
        SetD3d12DredMarker(commandList, L"SVSM Allocate Pages");
#endif
        BeginTimer(commandList, TimerAllocation);
        dispatchSparse(
            SparseRecycle,
            div_ceil(settings.physicalPageCount, 64u),
            1u);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_PhysicalOwners);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_CompactRenderPages);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_Counters);
        commandList->commitBarriers();
        for (int level = int(SvsmClipmapCount) - 1;
            level >= int(firstScheduledClipmap);
            --level)
        {
            constants.selectedClipmap = uint32_t(level);
            dispatchSparse(
                SparseAllocate,
                div_ceil(SvsmPagesPerClipmap, 64u),
                1u);
            nvrhi::utils::TextureUavBarrier(
                commandList, m_PageTable);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_PhysicalOwners);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_RenderPages);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_CompactRenderPages);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_Counters);
            if (useDirtyPageScatterRaster)
            {
                nvrhi::utils::BufferUavBarrier(
                    commandList, m_DirtyPageRectangles);
            }
            commandList->commitBarriers();
        }
        constants.selectedClipmap = 0u;
        EndTimer(commandList, TimerAllocation);

#ifdef _WIN32
        SetD3d12DredMarker(commandList, L"SVSM Clear Scheduled Pages");
#endif
        BeginTimer(commandList, TimerClearing);
        if (settings.gpuGatedDrawSubmission)
        {
            std::array<
                nvrhi::DispatchIndirectArguments,
                GatedDispatchArgumentCount> gatedDispatchArguments = {};
            for (uint32_t level = 0u;
                level < SvsmClipmapCount;
                ++level)
            {
                nvrhi::DispatchIndirectArguments& pageArgument =
                    gatedDispatchArguments[
                        CompactPageDispatchArgumentBase + level];
                pageArgument.groupsX = 0u;
                pageArgument.groupsY = 1u;
                pageArgument.groupsZ = 1u;

                const SvsmDispatchDimensions fillDimensions =
                    GetSvsmIndirectFillDispatchDimensions(
                        m_SparseDepthPass->
                            GetRenderPacketCount(level),
                        usePacketPageCulling,
                        useDirtyPageScatterRaster);
                nvrhi::DispatchIndirectArguments& fillArgument =
                    gatedDispatchArguments[
                        PacketFillDispatchArgumentBase + level];
                fillArgument.groupsX = fillDimensions.groupsX;
                fillArgument.groupsY = fillDimensions.groupsY;
                // Allocation copies a same-frame zero-or-one work gate here.
                // A zero Z dimension skips the packet scan entirely.
                fillArgument.groupsZ = 0u;
            }
            commandList->writeBuffer(
                m_IndirectPageDispatchArguments,
                gatedDispatchArguments.data(),
                sizeof(gatedDispatchArguments));
            for (uint32_t level = firstScheduledClipmap;
                level < SvsmClipmapCount;
                ++level)
            {
                commandList->copyBuffer(
                    m_IndirectPageDispatchArguments,
                    uint64_t(
                        CompactPageDispatchArgumentBase + level) *
                        sizeof(nvrhi::DispatchIndirectArguments) +
                        offsetof(
                            nvrhi::DispatchIndirectArguments,
                            groupsX),
                    m_Counters,
                    uint64_t(c_DebugCounterCount + level) *
                        sizeof(uint32_t),
                    sizeof(uint32_t));
                if (useLevelEmptyWorkSkip)
                {
                    commandList->copyBuffer(
                        m_IndirectPageDispatchArguments,
                        uint64_t(
                            PacketFillDispatchArgumentBase + level) *
                            sizeof(
                                nvrhi::DispatchIndirectArguments) +
                            offsetof(
                                nvrhi::DispatchIndirectArguments,
                                groupsZ),
                        m_Counters,
                        uint64_t(
                            c_LevelHasWorkCounterBase + level) *
                            sizeof(uint32_t),
                        sizeof(uint32_t));
                }
            }
            commandList->setBufferState(
                m_IndirectPageDispatchArguments,
                nvrhi::ResourceStates::IndirectArgument);
            commandList->commitBarriers();
        }

        auto dispatchCompactPageStage =
            [this, commandList, &constants,
                firstScheduledClipmap](uint32_t stage) {
                for (uint32_t level = firstScheduledClipmap;
                    level < SvsmClipmapCount;
                    ++level)
                {
                    constants.selectedClipmap = level;
                    commandList->writeBuffer(
                        m_SparseConstants,
                        &constants,
                        sizeof(constants));
                    nvrhi::ComputeState state;
                    state.pipeline = m_SparsePipelines[stage];
                    state.bindings = { m_SparseBindingSet };
                    state.indirectParams =
                        m_IndirectPageDispatchArguments;
                    commandList->setComputeState(state);
                    commandList->dispatchIndirect(
                        (CompactPageDispatchArgumentBase + level) *
                            uint32_t(sizeof(
                                nvrhi::DispatchIndirectArguments)));
                }
                constants.selectedClipmap = 0u;
            };

        if (settings.gpuGatedDrawSubmission)
        {
            dispatchCompactPageStage(SparseClear);
        }
        else
        {
            dispatchSparse(
                SparseClear,
                settings.physicalPageCount,
                1u);
        }
        nvrhi::utils::TextureUavBarrier(
            commandList, m_SparsePhysicalDepth);
        commandList->commitBarriers();
        EndTimer(commandList, TimerClearing);

        if (settings.gpuGatedDrawSubmission)
        {
#ifdef _WIN32
            SetD3d12DredMarker(
                commandList,
                L"SVSM Build Indirect Arguments");
#endif
            if (indirectArgumentTemplatesPrepared)
            {
                if (!indirectArgumentTemplates.empty())
                {
                    commandList->writeBuffer(
                        m_IndirectDrawArguments,
                        indirectArgumentTemplates.data(),
                        uint64_t(indirectArgumentTemplates.size()) *
                            sizeof(
                                nvrhi::DrawIndexedIndirectArguments));
                }
                m_IndirectDrawArgumentsInitialized = true;
                m_IndirectDrawArgumentsBatched =
                    useBatchedDrawSubmission;
                m_IndirectDrawArgumentsPacketPageCulling =
                    usePacketPageCulling;
            }
            if (usePacketPageCulling)
                BeginTimer(commandList, TimerPacketPageCulling);
            for (uint32_t level = firstScheduledClipmap;
                level < SvsmClipmapCount;
                ++level)
            {
                constants.selectedClipmap = level;
                constants.drawPacketOffset =
                    m_SparseDepthPass->
                        GetRenderPacketOffset(level);
                constants.drawPacketCount =
                    m_SparseDepthPass->
                        GetRenderPacketCount(level);
                if (constants.drawPacketCount == 0u)
                    continue;
                const SvsmDispatchDimensions dispatchDimensions =
                    GetSvsmIndirectFillDispatchDimensions(
                        constants.drawPacketCount,
                        usePacketPageCulling,
                        useDirtyPageScatterRaster);
                if (useLevelEmptyWorkSkip)
                {
                    commandList->writeBuffer(
                        m_SparseConstants,
                        &constants,
                        sizeof(constants));
                    nvrhi::ComputeState state;
                    state.pipeline =
                        m_SparsePipelines[SparseFillIndirect];
                    state.bindings = { m_SparseBindingSet };
                    state.indirectParams =
                        m_IndirectPageDispatchArguments;
                    commandList->setComputeState(state);
                    commandList->dispatchIndirect(
                        (PacketFillDispatchArgumentBase + level) *
                            uint32_t(sizeof(
                                nvrhi::DispatchIndirectArguments)));
                }
                else
                {
                    dispatchSparse(
                        SparseFillIndirect,
                        dispatchDimensions.groupsX,
                        dispatchDimensions.groupsY);
                }
            }
            constants.selectedClipmap = 0u;
            constants.drawPacketOffset = 0u;
            constants.drawPacketCount = 0u;
            if (m_SparseDepthPass->GetRenderPacketCount() > 0u)
            {
                // The UAV-to-indirect transitions below order fillIndirect
                // writes. Do not queue a UAV barrier for either transitioned
                // buffer in this same batch: pinned NVRHI combines repeated
                // pending buffer states, producing an invalid state union.
                if (usePacketPageCulling)
                {
                    nvrhi::utils::BufferUavBarrier(
                        commandList,
                        m_PacketPageRuntime);
                    nvrhi::utils::BufferUavBarrier(
                        commandList,
                        m_PacketRenderPages);
                }
                commandList->setBufferState(
                    m_IndirectDrawArguments,
                    nvrhi::ResourceStates::IndirectArgument);
                commandList->setBufferState(
                    m_Counters,
                    nvrhi::ResourceStates::IndirectArgument);
                commandList->commitBarriers();
            }
        }
        if (usePacketPageCulling)
            EndTimer(commandList, TimerPacketPageCulling);
#ifdef _WIN32
        SetD3d12DredMarker(commandList, L"SVSM Render Dirty Pages");
#endif
        BeginTimer(commandList, TimerPageRendering);

        const auto submissionStart =
            std::chrono::steady_clock::now();
        for (uint32_t level = firstScheduledClipmap;
            level < SvsmClipmapCount;
            ++level)
        {
            constants.selectedClipmap = level;
            commandList->writeBuffer(
                m_SparseConstants, &constants, sizeof(constants));
            SparseDepthPass::Context context;
            if (useRenderPackets)
            {
                if (m_SparseDepthPass->
                        GetRenderPacketCount(level) > 0u)
                {
                    m_SparseDepthPass->RenderPackets(
                        commandList,
                        m_ClipmapViews[level].get(),
                        m_RasterFramebuffer,
                        context,
                        level,
                        m_IndirectDrawCapacity,
                        settings.gpuGatedDrawSubmission,
                        useBatchedDrawSubmission,
                        useLevelEmptyWorkSkip,
                        usePacketPageCulling,
                        useDirtyPageScatterRaster);
                }
            }
            else
            {
                drawStrategy.PrepareForView(
                    rootNode, *m_ClipmapViews[level]);
                m_SparseDepthPass->RenderViewReference(
                    commandList,
                    m_ClipmapViews[level].get(),
                    m_RasterFramebuffer,
                    drawStrategy,
                    context,
                    level);
            }
        }
        const auto submissionEnd =
            std::chrono::steady_clock::now();
        m_Timings.cullingCpuMilliseconds =
            packetPreparationMilliseconds +
            std::chrono::duration<float, std::milli>(
                submissionEnd - submissionStart).count();

        constants.selectedClipmap = 0u;
        nvrhi::utils::TextureUavBarrier(
            commandList, m_SparsePhysicalDepth);
        commandList->commitBarriers();
#ifdef _WIN32
        SetD3d12DredMarker(commandList, L"SVSM Finalize Pages");
#endif
        if (settings.gpuGatedDrawSubmission)
        {
            dispatchCompactPageStage(SparseFinalize);
        }
        else
        {
            dispatchSparse(
                SparseFinalize,
                div_ceil(settings.physicalPageCount, 64u),
                1u);
        }
        nvrhi::utils::TextureUavBarrier(
            commandList, m_PageTable);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_Counters);
        commandList->commitBarriers();
        if (settings.debugView != SvsmDebugView::None)
        {
            dispatchSparse(
                SparseStats,
                div_ceil(
                    SvsmPagesPerClipmap * SvsmClipmapCount,
                    64u),
                1u);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_Counters);
            commandList->commitBarriers();
        }
        EndTimer(commandList, TimerPageRendering);
        }
        else
        {
            m_Timings.pageMarkingMilliseconds = 0.f;
            m_Timings.allocationMilliseconds = 0.f;
            m_Timings.clearingMilliseconds = 0.f;
            m_Timings.packetPageCullingMilliseconds = 0.f;
            m_Timings.pageRenderingMilliseconds = 0.f;
            m_Timings.cullingCpuMilliseconds =
                packetPreparationMilliseconds;
            m_Timings.renderedPages = 0u;
            m_Timings.allocationFailures = 0u;
            m_Timings.overBudgetPages = 0u;
            m_Timings.packetPageCandidatePackets = 0u;
            m_Timings.packetPageCompactedPackets = 0u;
            m_Timings.packetPageFailOpenPackets = 0u;
            if (settings.debugView != SvsmDebugView::None)
            {
                dispatchSparse(
                    SparseStats,
                    div_ceil(
                        SvsmPagesPerClipmap *
                            SvsmClipmapCount,
                        64u),
                    1u);
                nvrhi::utils::BufferUavBarrier(
                    commandList, m_Counters);
                commandList->commitBarriers();
            }
        }

        bool resolvedVisibilityThisFrame = false;
        if (!reuseStaticVisibility)
        {
#ifdef _WIN32
            SetD3d12DredMarker(commandList, L"SVSM Resolve Visibility");
#endif
            // Slice zero is also the scratch target when visibility caching
            // is disabled or all jitter slots are occupied. Invalidate the
            // destination before overwriting it so a later cache lookup can
            // never reuse unrelated visibility.
            m_StaticVisibilityValid[resolveVisibilitySlot] = false;
            BeginTimer(commandList, TimerFiltering);
            commandList->writeBuffer(
                m_SparseConstants, &constants, sizeof(constants));
            nvrhi::ComputeState resolveState;
            const uint32_t resolvePermutation =
                (settings.pageTranslationCachingEnabled ? 1u : 0u) *
                    c_SparseResolveTapPermutationCount +
                SvsmTapCountPermutationIndex(settings.tapCount);
            resolveState.pipeline =
                m_SparseResolvePipelines[resolvePermutation];
            resolveState.bindings = {
                m_SparseResolveBindingSets[
                    resolveVisibilitySlot]
            };
            commandList->setComputeState(resolveState);
            commandList->dispatch(
                div_ceil(cameraDepthDesc.width, 8u),
                div_ceil(cameraDepthDesc.height, 8u));
            resolvedVisibilityThisFrame = true;
            if (settings.debugView != SvsmDebugView::None &&
                m_TimerFrameAdmitted)
            {
                const uint32_t readbackSlot = m_CurrentTimerSlot;
                if (!m_DebugCounterReadbackPending[readbackSlot])
                {
                    commandList->copyBuffer(
                        m_DebugCounterReadbacks[readbackSlot],
                        0u,
                        m_Counters,
                        0u,
                        uint64_t(c_DebugCounterCount) *
                            sizeof(uint32_t));
                    m_DebugCounterReadbackPending[readbackSlot] = true;
                    m_DebugCounterReadbackGenerations[readbackSlot] =
                        m_DebugCounterGeneration;
                    m_DebugCounterReadbackSourceFrames[readbackSlot] =
                        m_TimerFrame;
                }
            }
            EndTimer(commandList, TimerFiltering);
        }
        else
        {
            m_Timings.filteringMilliseconds = 0.f;
        }
        EndTimer(commandList, TimerTotal);
        commandList->endMarker();

        m_PreviousRenderOrigins = m_CurrentRenderOrigins;
        m_PreviousLightDepthOrigin = m_CurrentLightDepthOrigin;
        m_PreviousLightBasis = lightBasis;
        m_PreviousLightBasisValid = true;
        m_PreviousProducingLight = light;
        m_PreviousSceneStateHash = sceneStateHash;
        m_PreviousFirstClipmapExtent =
            settings.firstClipmapExtent;
        m_PreviousMaximumLightDepth =
            settings.maximumLightDepth;
        m_StaticPageRequestCacheReady =
            staticPageRequestConfiguration;
        m_StaticPageRequestJitterActive = staticJitterActive;
        if (staticPageRequestConfiguration)
        {
            m_StaticPageDrainFramesRemaining =
                staticPageDrainFramesRemainingAfterThisFrame;
            m_StaticPageRequestPageRenderBudget =
                settings.pageRenderBudget;
            m_StaticPageRequestCoarsestPageRenderBudgetEnabled =
                settings.coarsestPageRenderBudgetEnabled;
        }
        else
        {
            m_StaticPageDrainFramesRemaining = 0u;
            m_StaticPageRequestPageRenderBudget =
                std::numeric_limits<uint32_t>::max();
            m_StaticPageRequestCoarsestPageRenderBudgetEnabled = false;
        }
        m_StaticPageRequestCameraWorldToClip =
            cameraWorldToClip;
        m_StaticPageRequestCameraDepth = cameraDepth;
        m_StaticPageRequestWidth = cameraDepthDesc.width;
        m_StaticPageRequestHeight = cameraDepthDesc.height;
        m_StaticPageRequestViewport = cameraViewport;
        m_StaticPageRequestMarkingMode =
            settings.markingMode;
        m_StaticPageRequestFilterMode =
            settings.filterMode;
        m_StaticPageRequestTapCount = settings.tapCount;
        m_StaticPageRequestResolutionBias =
            settings.resolutionBias;
        if (staticPageRequestConfiguration &&
            staticJitterSlot <
                c_StaticVisibilityCacheSlotCount)
        {
            m_StaticJitterOffsets[staticJitterSlot] =
                cameraPixelOffset;
            m_StaticJitterOffsetValid[staticJitterSlot] = true;
            if (settings.staticVisibilityCachingEnabled &&
                resolvedVisibilityThisFrame)
            {
                m_StaticVisibilityValid[
                    staticJitterSlot] = true;
            }
        }
        m_StaticVisibilitySettingsValid = true;
        m_StaticVisibilityFilterMode = settings.filterMode;
        m_StaticVisibilityTapCount = settings.tapCount;
        m_StaticVisibilityResolutionBias =
            settings.resolutionBias;
        m_StaticVisibilityPageTranslationCaching =
            settings.pageTranslationCachingEnabled;
        m_StaticVisibilityAdaptiveFiltering =
            settings.adaptiveFiltering;
        m_CacheStateValid = true;
        m_SparseResourcesNeedClear = false;
        m_Timings.active = true;
        EndTimerFrame();
        return {
            m_SparseVisibilityCache[resolveVisibilitySlot],
            light,
            settings.debugView != SvsmDebugView::None
        };
    }

    void SparseVirtualShadowMapPass::PresentDebug(
        nvrhi::ICommandList* commandList,
        nvrhi::IFramebuffer* framebuffer)
    {
        if (!commandList ||
            !framebuffer ||
            !m_DebugVisualization ||
            !m_DebugPixelShader)
        {
            return;
        }

        if (!m_DebugPipeline)
        {
            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.primType =
                nvrhi::PrimitiveType::TriangleStrip;
            pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
            pipelineDesc.PS = m_DebugPixelShader;
            pipelineDesc.bindingLayouts = {
                m_DebugBindingLayout
            };
            pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.renderState.depthStencilState
                .depthTestEnable = false;
            pipelineDesc.renderState.depthStencilState
                .stencilEnable = false;
            m_DebugPipeline = m_Device->createGraphicsPipeline(
                pipelineDesc,
                framebuffer->getFramebufferInfo());
        }

        if (!m_DebugBindingSet)
        {
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_DebugVisualization)
            };
            m_DebugBindingSet = m_Device->createBindingSet(
                bindings, m_DebugBindingLayout);
        }

        nvrhi::GraphicsState state;
        state.pipeline = m_DebugPipeline;
        state.framebuffer = framebuffer;
        state.bindings = { m_DebugBindingSet };
        const nvrhi::FramebufferInfoEx& info =
            framebuffer->getFramebufferInfo();
        state.viewport.addViewport(
            nvrhi::Viewport(float(info.width), float(info.height)));
        state.viewport.addScissorRect(
            nvrhi::Rect(int(info.width), int(info.height)));
        commandList->beginMarker("SVSM Debug View");
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments arguments;
        arguments.instanceCount = 1u;
        arguments.vertexCount = 4u;
        commandList->draw(arguments);
        commandList->endMarker();
    }

    void SparseVirtualShadowMapPass::Deactivate()
    {
        if (m_Timings.active ||
            m_DebugCounterRequestedBackend !=
                SvsmResourceBackend::None ||
            m_Timings.debugCountersAvailable)
        {
            m_DebugCounterRequestedBackend =
                SvsmResourceBackend::None;
            InvalidateDebugCounters();
        }
        if (m_Timings.active ||
            m_UiTimingContextValid ||
            m_Timings.gpuTimingSource !=
                SvsmGpuTimingSource::Unavailable)
        {
            m_UiTimingContextValid = false;
            InvalidateUiTimings();
        }
        m_Timings.active = false;
        m_Timings.renderedPages = 0u;
    }

    bool SparseVirtualShadowMapPass::PopCompletedTiming(
        SparseVirtualShadowMapGpuTiming& timing)
    {
        if (m_CompletedTimingSamples.empty())
            return false;
        timing = m_CompletedTimingSamples.front();
        m_CompletedTimingSamples.pop_front();
        return true;
    }

    void SparseVirtualShadowMapPass::ResetTimingAccounting()
    {
        m_CompletedTimingSamples.clear();
        m_TimingAccounting = {};
        m_CurrentTimerSourceTag = 0u;
        for (uint32_t slot = 0u; slot < c_TimerLatency; ++slot)
            m_TimerSourceTags[slot] = 0u;
    }
}
