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
#include <bitset>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
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
static_assert(
    offsetof(
        SparseVirtualShadowMapSparseConstants,
        receiverToClip) ==
        offsetof(
            SparseVirtualShadowMapSparseConstants,
            worldToClip) +
            sizeof(
                SparseVirtualShadowMapSparseConstants::worldToClip),
    "SVSM caster and receiver transforms must occupy distinct HLSL rows.");
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
            staticDepthHierarchyBias) + 16u,
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
static_assert(
    SVSM_SPARSE_FLAG_BILINEAR_PCF ==
        uvsr::SvsmSparseFlagBilinearPcf,
    "The CPU and HLSL bilinear-filter flags must match.");
static_assert(
    SVSM_SPARSE_FLAG_PAIRED_STATIC_DYNAMIC_DEPTH ==
        uvsr::SvsmSparseFlagPairedStaticDynamicDepth,
    "The CPU and HLSL paired-depth flags must match.");
static_assert(
    SVSM_SPARSE_FLAG_PRESERVE_PHYSICAL_MAPPINGS ==
        uvsr::SvsmSparseFlagPreservePhysicalMappings,
    "The CPU and HLSL allocation-preservation flags must match.");
static_assert(
    SVSM_SPARSE_FLAG_STATIC_DEPTH_HIERARCHY_CULLING ==
        uvsr::SvsmSparseFlagStaticDepthHierarchyCulling,
    "The CPU and HLSL static-depth hierarchy flags must match.");
static_assert(
    SVSM_SPARSE_FLAG_STATIC_DEPTH_HIERARCHY_RESOURCE ==
        uvsr::SvsmSparseFlagStaticDepthHierarchyResource,
    "The CPU and HLSL static-depth hierarchy resource flags must match.");
static_assert(
    SVSM_SPARSE_FLAG_STATIC_DEPTH_HIERARCHY_BOOTSTRAP ==
        uvsr::SvsmSparseFlagStaticDepthHierarchyBootstrap,
    "The CPU and HLSL static-depth hierarchy bootstrap flags must match.");
static_assert(
    SVSM_SPARSE_FLAG_RECEIVER_PAGE_MASK_CULLING ==
        uvsr::SvsmSparseFlagReceiverPageMaskCulling,
    "The CPU and HLSL receiver-page mask flags must match.");
static_assert(
    SVSM_PACKET_STATIC_CASTER_BIT ==
            uvsr::SvsmPacketStaticCasterBit &&
        SVSM_PACKET_OBJECT_INSTANCE_MASK ==
            uvsr::SvsmPacketObjectInstanceMask,
    "The CPU and HLSL packet caster encoding must match.");
static_assert(
    SVSM_PAGE_STATIC_DIRTY_BIT ==
        uvsr::SvsmPageStaticDirtyBit,
    "The CPU and HLSL static-dirty page bit must match.");
static_assert(
    SVSM_LOCAL_INVALIDATION_STATIC_BIT ==
            uvsr::SvsmLocalInvalidationStaticBit &&
        SVSM_LOCAL_INVALIDATION_OWNER_MASK ==
            uvsr::SvsmLocalInvalidationOwnerMask,
    "The CPU and HLSL local-invalidation encoding must match.");
static_assert(sizeof(nvrhi::DrawIndexedIndirectArguments) == 20u,
    "SVSM relies on NVRHI's packed indexed indirect layout.");
static_assert(
    offsetof(
        nvrhi::DrawIndexedIndirectArguments,
        instanceCount) == sizeof(uint32_t),
    "SVSM indirect instance counts must remain the second word.");
static_assert(
    sizeof(SparseVirtualShadowMapPacketMetadata) ==
        8u * sizeof(uint32_t),
    "SVSM packet-page metadata must remain eight uint32 words.");
static_assert(
    offsetof(SparseVirtualShadowMapPacketMetadata, packedMinimumPage) == 0u &&
    offsetof(SparseVirtualShadowMapPacketMetadata, packedMaximumPage) == 4u &&
    offsetof(SparseVirtualShadowMapPacketMetadata, pageListOffset) == 8u &&
    offsetof(SparseVirtualShadowMapPacketMetadata, objectInstanceIndex) == 12u &&
    offsetof(SparseVirtualShadowMapPacketMetadata, packedMinimumTexel) == 16u &&
    offsetof(SparseVirtualShadowMapPacketMetadata, packedMaximumTexel) == 20u &&
    offsetof(SparseVirtualShadowMapPacketMetadata, nearestReverseDepth) == 24u,
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
static_assert(
    SVSM_STATIC_DEPTH_HIERARCHY_BASE_CELL_WIDTH ==
            uvsr::SvsmStaticDepthHierarchyBaseCellWidth &&
        SVSM_STATIC_DEPTH_HIERARCHY_BASE_AXIS ==
            uvsr::SvsmStaticDepthHierarchyBaseAxis &&
        SVSM_STATIC_DEPTH_HIERARCHY_LEVEL_ONE_OFFSET ==
            uvsr::SvsmStaticDepthHierarchyLevelOneOffset &&
        SVSM_STATIC_DEPTH_HIERARCHY_LEVEL_TWO_OFFSET ==
            uvsr::SvsmStaticDepthHierarchyLevelTwoOffset &&
        SVSM_STATIC_DEPTH_HIERARCHY_ROOT_OFFSET ==
            uvsr::SvsmStaticDepthHierarchyRootOffset &&
        SVSM_STATIC_DEPTH_HIERARCHY_TAG_OFFSET ==
            uvsr::SvsmStaticDepthHierarchyTagOffset &&
        SVSM_STATIC_DEPTH_HIERARCHY_WORDS_PER_PAGE ==
            uvsr::SvsmStaticDepthHierarchyWordsPerPage,
    "SVSM CPU and HLSL static-depth hierarchy layouts must match.");
static_assert(
    SVSM_STATIC_DEPTH_HIERARCHY_QUERY_COUNTER ==
            uvsr::SvsmStaticDepthHierarchyQueryCounter &&
        SVSM_STATIC_DEPTH_HIERARCHY_CULL_COUNTER ==
            uvsr::SvsmStaticDepthHierarchyCullCounter &&
        SVSM_STATIC_DEPTH_HIERARCHY_FAIL_OPEN_COUNTER ==
            uvsr::SvsmStaticDepthHierarchyFailOpenCounter &&
        SVSM_STATIC_DEPTH_HIERARCHY_BUILT_PAGE_COUNTER ==
            uvsr::SvsmStaticDepthHierarchyBuiltPageCounter,
    "SVSM CPU and HLSL static-depth hierarchy counters must match.");
static_assert(
    SVSM_RECEIVER_PAGE_MASK_QUERY_COUNTER ==
            uvsr::SvsmReceiverPageMaskQueryCounter &&
        SVSM_RECEIVER_PAGE_MASK_CULL_COUNTER ==
            uvsr::SvsmReceiverPageMaskCullCounter &&
        SVSM_RECEIVER_PAGE_MASK_FAIL_OPEN_COUNTER ==
            uvsr::SvsmReceiverPageMaskFailOpenCounter,
    "SVSM CPU and HLSL receiver-page mask counters must match.");
static_assert(
    SVSM_RECEIVER_PAGE_MASK_CELL_WIDTH ==
            uvsr::SvsmReceiverPageMaskCellWidth &&
        SVSM_RECEIVER_PAGE_MASK_AXIS ==
            uvsr::SvsmReceiverPageMaskAxis &&
        SVSM_RECEIVER_PAGE_MASK_QUADRANT_AXIS ==
            uvsr::SvsmReceiverPageMaskQuadrantAxis &&
        SVSM_RECEIVER_PAGE_MASK_QUADRANT_CELL_AXIS ==
            uvsr::SvsmReceiverPageMaskQuadrantCellAxis &&
        SVSM_RECEIVER_PAGE_MASK_TAG_OFFSET ==
            uvsr::SvsmReceiverPageMaskTagOffset &&
        SVSM_RECEIVER_PAGE_MASK_QUADRANT_OFFSET ==
            uvsr::SvsmReceiverPageMaskQuadrantOffset &&
        SVSM_RECEIVER_PAGE_MASK_QUADRANT_COUNT ==
            uvsr::SvsmReceiverPageMaskQuadrantCount &&
        SVSM_RECEIVER_PAGE_MASK_WORDS_PER_PAGE ==
            uvsr::SvsmReceiverPageMaskWordsPerPage,
    "SVSM CPU and HLSL receiver-page mask layouts must match.");
static_assert(
    SVSM_SCHEDULED_TILE_PAGE_WIDTH ==
            uvsr::SvsmScheduledTilePageWidth &&
        SVSM_SCHEDULED_TILES_PER_AXIS ==
            uvsr::SvsmScheduledTilesPerAxis &&
        SVSM_SCHEDULED_TILE_MASK_WORDS_PER_LEVEL ==
            uvsr::SvsmScheduledTileMaskWordsPerLevel,
    "SVSM CPU and HLSL scheduled-page hierarchy layouts must match.");
static_assert(
    SVSM_SCHEDULED_TILE_MASK_QUERY_COUNTER ==
            uvsr::SvsmScheduledTileMaskQueryCounter &&
        SVSM_SCHEDULED_TILE_MASK_EARLY_REJECT_COUNTER ==
            uvsr::SvsmScheduledTileMaskEarlyRejectCounter &&
        SVSM_SCHEDULED_TILE_MASK_FAIL_OPEN_COUNTER ==
            uvsr::SvsmScheduledTileMaskFailOpenCounter &&
        SVSM_SCHEDULED_TILE_MASK_POSITIVE_EXACT_ZERO_COUNTER ==
            uvsr::SvsmScheduledTileMaskPositiveExactZeroCounter,
    "SVSM CPU and HLSL scheduled-page hierarchy counters must match.");

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
        constexpr uint32_t SparseInvalidatePages = 8u;
        constexpr uint32_t SparseBuildScheduledPageTileMasks = 9u;
        constexpr uint32_t SparseBuildStaticDepthHierarchy = 10u;
        constexpr uint32_t SparseScheduleFine = 11u;
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

        uint32_t FloatBits(float value)
        {
            uint32_t bits = 0u;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
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
            metadata.packedMinimumTexel =
                SvsmInvalidPacketPageBounds;
            metadata.packedMaximumTexel =
                SvsmInvalidPacketPageBounds;
            metadata.nearestReverseDepth = 0u;
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
            float nearestReverseDepth =
                -std::numeric_limits<float>::infinity();
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
                const float ndcDepth = clip.z / clip.w;
                if (!std::isfinite(ndcDepth))
                    return metadata;
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
                nearestReverseDepth = std::max(
                    nearestReverseDepth, ndcDepth);
            }

            if (!dm::all(dm::isfinite(minimumVirtual)) ||
                !dm::all(dm::isfinite(maximumVirtual)) ||
                !std::isfinite(nearestReverseDepth))
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
            const SvsmPacketTexelRectangle texelRectangle =
                GetSvsmPacketTexelRectangle(
                    minimumVirtual.x,
                    minimumVirtual.y,
                    maximumVirtual.x,
                    maximumVirtual.y);
            metadata.packedMinimumTexel =
                texelRectangle.packedMinimum;
            metadata.packedMaximumTexel =
                texelRectangle.packedMaximum;
            const float conservativeNearestReverseDepth =
                std::nextafter(
                    std::clamp(nearestReverseDepth, 0.f, 1.f),
                    std::numeric_limits<float>::infinity());
            metadata.nearestReverseDepth =
                FloatBits(conservativeNearestReverseDepth);
            pageListCapacity = GetSvsmPacketPageListCapacity(
                metadata.packedMinimumPage,
                metadata.packedMaximumPage);
            return metadata;
        }

        bool IsFiniteSvsmBox(const box3& bounds)
        {
            return !bounds.isempty() &&
                dm::all(dm::isfinite(bounds.m_mins)) &&
                dm::all(dm::isfinite(bounds.m_maxs)) &&
                dm::all(bounds.m_mins <= bounds.m_maxs);
        }

        bool IsSvsmStaticCacheCandidate(const DrawItem& item)
        {
            if (!item.instance ||
                !item.mesh ||
                !item.geometry ||
                !item.material ||
                item.material->domain != MaterialDomain::Opaque ||
                item.instance->GetInstanceIndex() < 0 ||
                uint32_t(item.instance->GetInstanceIndex()) >
                    SvsmPacketObjectInstanceMask ||
                !item.instance->GetNode())
            {
                return false;
            }
            return CanUseSvsmStaticPacketBounds(
                    bool(item.mesh->skinPrototype),
                    item.mesh->isSkinPrototype,
                    item.mesh->isMorphTargetAnimationMesh) &&
                IsFiniteSvsmBox(item.geometry->objectSpaceBounds);
        }

        struct SvsmCasterKey
        {
            const MeshInstance* instance = nullptr;
            const MeshGeometry* geometry = nullptr;

            [[nodiscard]] bool operator==(
                const SvsmCasterKey& other) const
            {
                return instance == other.instance &&
                    geometry == other.geometry;
            }
        };

        struct SvsmCasterKeyHash
        {
            [[nodiscard]] size_t operator()(
                const SvsmCasterKey& key) const
            {
                const size_t left =
                    std::hash<const void*>{}(key.instance);
                const size_t right =
                    std::hash<const void*>{}(key.geometry);
                return left ^ (right + 0x9e3779b9u +
                    (left << 6u) + (left >> 2u));
            }
        };

        struct SvsmMaterialShadowSignature
        {
            MaterialDomain domain = MaterialDomain::Count;
            const void* material = nullptr;
            const void* materialConstants = nullptr;
            const void* baseTextureObject = nullptr;
            const void* opacityTextureObject = nullptr;
            const void* baseTexture = nullptr;
            const void* opacityTexture = nullptr;
            float opacity = 1.f;
            float alphaCutoff = 0.5f;
            bool baseTextureEnabled = false;
            bool opacityTextureEnabled = false;
            bool doubleSided = false;

            [[nodiscard]] bool operator==(
                const SvsmMaterialShadowSignature& other) const
            {
                return domain == other.domain &&
                    material == other.material &&
                    materialConstants == other.materialConstants &&
                    baseTextureObject == other.baseTextureObject &&
                    opacityTextureObject ==
                        other.opacityTextureObject &&
                    baseTexture == other.baseTexture &&
                    opacityTexture == other.opacityTexture &&
                    opacity == other.opacity &&
                    alphaCutoff == other.alphaCutoff &&
                    baseTextureEnabled == other.baseTextureEnabled &&
                    opacityTextureEnabled ==
                        other.opacityTextureEnabled &&
                    doubleSided == other.doubleSided;
            }
        };

        struct SvsmCasterTopologySignature
        {
            const void* mesh = nullptr;
            const void* buffers = nullptr;
            const void* indexBuffer = nullptr;
            const void* vertexBuffer = nullptr;
            const void* instanceBuffer = nullptr;
            uint32_t meshVertexOffset = 0u;
            uint32_t meshIndexOffset = 0u;
            uint32_t meshVertexCount = 0u;
            uint32_t meshIndexCount = 0u;
            uint32_t geometryVertexOffset = 0u;
            uint32_t geometryIndexOffset = 0u;
            uint32_t vertexCount = 0u;
            uint32_t indexCount = 0u;
            uint64_t positionByteOffset = 0u;
            uint64_t positionByteSize = 0u;
            uint64_t texCoordByteOffset = 0u;
            uint64_t texCoordByteSize = 0u;
            MeshGeometryPrimitiveType primitiveType =
                MeshGeometryPrimitiveType::Triangles;

            [[nodiscard]] bool operator==(
                const SvsmCasterTopologySignature& other) const
            {
                return mesh == other.mesh &&
                    buffers == other.buffers &&
                    indexBuffer == other.indexBuffer &&
                    vertexBuffer == other.vertexBuffer &&
                    instanceBuffer == other.instanceBuffer &&
                    meshVertexOffset == other.meshVertexOffset &&
                    meshIndexOffset == other.meshIndexOffset &&
                    meshVertexCount == other.meshVertexCount &&
                    meshIndexCount == other.meshIndexCount &&
                    geometryVertexOffset ==
                        other.geometryVertexOffset &&
                    geometryIndexOffset ==
                        other.geometryIndexOffset &&
                    vertexCount == other.vertexCount &&
                    indexCount == other.indexCount &&
                    positionByteOffset == other.positionByteOffset &&
                    positionByteSize == other.positionByteSize &&
                    texCoordByteOffset == other.texCoordByteOffset &&
                    texCoordByteSize == other.texCoordByteSize &&
                    primitiveType == other.primitiveType;
            }
        };

        [[nodiscard]] SvsmCasterTopologySignature
        GetSvsmCasterTopologySignature(
            const MeshInfo& mesh,
            const MeshGeometry& geometry)
        {
            const nvrhi::BufferRange& positionRange =
                mesh.buffers->getVertexBufferRange(
                    VertexAttribute::Position);
            const nvrhi::BufferRange& texCoordRange =
                mesh.buffers->getVertexBufferRange(
                    VertexAttribute::TexCoord1);
            return {
                &mesh,
                mesh.buffers.get(),
                mesh.buffers->indexBuffer.Get(),
                mesh.buffers->vertexBuffer.Get(),
                mesh.buffers->instanceBuffer.Get(),
                mesh.vertexOffset,
                mesh.indexOffset,
                mesh.totalVertices,
                mesh.totalIndices,
                geometry.vertexOffsetInMesh,
                geometry.indexOffsetInMesh,
                geometry.numVertices,
                geometry.numIndices,
                positionRange.byteOffset,
                positionRange.byteSize,
                texCoordRange.byteOffset,
                texCoordRange.byteSize,
                geometry.type
            };
        }

        struct SvsmObjectInvalidationPolicyConfiguration
        {
            SvsmObjectInvalidationMode defaultMode =
                SvsmObjectInvalidationMode::Auto;
            SvsmObjectInvalidationResolver resolver;
            bool resolverEnabled = false;
            bool valid = true;

            [[nodiscard]] bool operator==(
                const SvsmObjectInvalidationPolicyConfiguration&
                    other) const
            {
                if (valid != other.valid)
                    return false;
                return
                    HasSameSvsmObjectInvalidationPolicyConfiguration(
                        defaultMode,
                        resolverEnabled ? &resolver : nullptr,
                        other.defaultMode,
                        other.resolverEnabled
                            ? &other.resolver
                            : nullptr);
            }
        };

        [[nodiscard]] SvsmObjectInvalidationPolicyConfiguration
        MakeSvsmObjectInvalidationPolicyConfiguration(
            SvsmObjectInvalidationMode defaultMode,
            const SvsmObjectInvalidationResolver* resolver)
        {
            SvsmObjectInvalidationPolicyConfiguration result;
            result.defaultMode = defaultMode;
            result.resolverEnabled = resolver != nullptr;
            result.valid =
                IsSvsmObjectInvalidationModeValid(defaultMode);
            if (resolver)
            {
                result.resolver = *resolver;
                result.valid =
                    result.valid && resolver->IsValid();
            }
            return result;
        }

        struct SvsmCasterSnapshot
        {
            SvsmCasterKey key;
            // Keep structural identities alive across the old/new diff. This
            // prevents allocator-address reuse from aliasing a removed caster
            // with a newly added one while localized invalidation is pending.
            std::shared_ptr<MeshInstance> instanceReference;
            std::shared_ptr<MeshGeometry> geometryReference;
            box3 objectSpaceBounds = box3::empty();
            box3 worldBounds = box3::empty();
            std::array<float, 12u> localToWorld{};
            SvsmMaterialShadowSignature material;
            SvsmCasterTopologySignature topology;
            bool reliableBounds = false;
            bool deforming = false;
            bool deformationRevisionReliable = false;
            uint32_t deformationRevision = 0u;
            SvsmObjectInvalidationMode invalidationMode =
                SvsmObjectInvalidationMode::Auto;
            bool staticCacheEligible = false;
            bool staticCacheCandidate = false;
            // A suppressed state can be opportunistically rasterized into
            // pages dirtied for another caster. Until a full authoritative
            // refresh, cached coverage may therefore be a union of more than
            // the observed and published snapshots.
            bool suppressedCoverageDebt = false;
            uint64_t promotionDeadline =
                SvsmNoPromotionDeadline;
        };

        [[nodiscard]] std::array<float, 12u>
        MakeSvsmTransformSignature(const affine3& transform)
        {
            SvsmAffineSignatureParts parts;
            uint32_t element = 0u;
            for (uint32_t row = 0u; row < 3u; ++row)
            {
                for (uint32_t column = 0u; column < 3u; ++column)
                    parts.linear[element++] =
                        transform.m_linear[row][column];
            }
            parts.translation = {
                transform.m_translation.x,
                transform.m_translation.y,
                transform.m_translation.z
            };
            return MakeSvsmAffineSignature(parts);
        }

        [[nodiscard]] bool IsFiniteSvsmTransformSignature(
            const std::array<float, 12u>& signature)
        {
            return std::all_of(
                signature.begin(),
                signature.end(),
                [](float value) { return std::isfinite(value); });
        }

        [[nodiscard]] bool TryMakeSvsmAffineFromTransformSignature(
            const std::array<float, 12u>& signature,
            affine3& transform)
        {
            SvsmAffineSignatureParts parts;
            if (!DecodeSvsmAffineSignature(signature, parts))
                return false;
            uint32_t element = 0u;
            for (uint32_t row = 0u; row < 3u; ++row)
            {
                for (uint32_t column = 0u; column < 3u; ++column)
                    transform.m_linear[row][column] =
                        parts.linear[element++];
            }
            transform.m_translation = float3(
                parts.translation[0],
                parts.translation[1],
                parts.translation[2]);
            return true;
        }

        [[nodiscard]] SvsmMaterialShadowSignature
        GetSvsmMaterialShadowSignature(const Material* material)
        {
            SvsmMaterialShadowSignature result;
            if (!material)
                return result;
            result.domain = material->domain;
            result.material = material;
            result.materialConstants =
                material->materialConstants.Get();
            result.baseTextureObject =
                material->baseOrDiffuseTexture.get();
            result.opacityTextureObject =
                material->opacityTexture.get();
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

        enum class SvsmAlphaTextureSource : uint32_t
        {
            None,
            BaseOrDiffuse,
            Opacity
        };

        struct SvsmAlphaTextureState
        {
            SvsmAlphaTextureSource source =
                SvsmAlphaTextureSource::None;
            const void* texture = nullptr;

            [[nodiscard]] bool operator==(
                const SvsmAlphaTextureState& other) const
            {
                return source == other.source &&
                    texture == other.texture;
            }
        };

        [[nodiscard]] SvsmAlphaTextureState
        GetSvsmAlphaTextureState(
            const SvsmMaterialShadowSignature& material)
        {
            if (material.opacityTextureEnabled &&
                material.opacityTextureObject)
            {
                return {
                    SvsmAlphaTextureSource::Opacity,
                    material.opacityTexture
                };
            }
            if (material.baseTextureEnabled &&
                material.baseTextureObject)
            {
                return {
                    SvsmAlphaTextureSource::BaseOrDiffuse,
                    material.baseTexture
                };
            }
            return {};
        }

        [[nodiscard]] bool HasSvsmDepthMaterialChange(
            const SvsmMaterialShadowSignature& previous,
            const SvsmMaterialShadowSignature& current)
        {
            if (previous.domain != current.domain ||
                previous.doubleSided != current.doubleSided)
            {
                return true;
            }
            const bool alphaRelevant =
                previous.domain == MaterialDomain::AlphaTested ||
                current.domain == MaterialDomain::AlphaTested;
            if (!alphaRelevant)
                return false;
            return HasSvsmAlphaTestScalarDepthChange(
                    true,
                    previous.opacity,
                    current.opacity,
                    previous.alphaCutoff,
                    current.alphaCutoff) ||
                !(GetSvsmAlphaTextureState(previous) ==
                    GetSvsmAlphaTextureState(current));
        }

        [[nodiscard]] bool EqualSvsmBox(
            const box3& left,
            const box3& right);

        [[nodiscard]] SvsmCopiedBoundsSignature
        MakeSvsmCopiedBoundsSignature(const box3& bounds)
        {
            return {
                {
                    bounds.m_mins.x,
                    bounds.m_mins.y,
                    bounds.m_mins.z
                },
                {
                    bounds.m_maxs.x,
                    bounds.m_maxs.y,
                    bounds.m_maxs.z
                }
            };
        }

        [[nodiscard]] SvsmCasterEvent GetSvsmCasterEvent(
            const SvsmCasterSnapshot* previous,
            const SvsmCasterSnapshot* current,
            bool includeStaticClassification)
        {
            SvsmCasterEvent event;
            event.previousExists = previous != nullptr;
            event.currentExists = current != nullptr;
            if (!previous || !current)
                return event;

            event.transformChanged =
                previous->localToWorld != current->localToWorld ||
                HasSvsmCopiedBoundsChanged(
                    MakeSvsmCopiedBoundsSignature(
                        previous->objectSpaceBounds),
                    MakeSvsmCopiedBoundsSignature(
                        current->objectSpaceBounds)) ||
                (!current->deforming &&
                    !EqualSvsmBox(
                        previous->worldBounds,
                        current->worldBounds));
            const bool rawMaterialChanged =
                !(previous->material == current->material);
            event.depthMaterialChanged =
                HasSvsmDepthMaterialChange(
                    previous->material,
                    current->material);
            event.bindingOnlyMaterialChanged =
                rawMaterialChanged &&
                !event.depthMaterialChanged;
            event.deformationChanged =
                previous->deforming != current->deforming ||
                ((previous->deforming || current->deforming) &&
                    (!previous->deformationRevisionReliable ||
                        !current->deformationRevisionReliable ||
                        previous->deformationRevision !=
                            current->deformationRevision));
            event.topologyChanged =
                !(previous->topology == current->topology);
            event.invalidationModeChanged =
                previous->invalidationMode !=
                    current->invalidationMode;
            event.staticClassificationChanged =
                includeStaticClassification &&
                previous->staticCacheCandidate !=
                    current->staticCacheCandidate;
            event.reliable =
                IsSvsmCasterSnapshotTrackable(
                    previous->deforming,
                    previous->reliableBounds,
                    previous->deformationRevisionReliable) &&
                IsSvsmCasterSnapshotTrackable(
                    current->deforming,
                    current->reliableBounds,
                    current->deformationRevisionReliable);
            return event;
        }

        [[nodiscard]] bool EqualSvsmBox(
            const box3& left,
            const box3& right)
        {
            return all(left.m_mins == right.m_mins) &&
                all(left.m_maxs == right.m_maxs);
        }

        [[nodiscard]] SvsmPacketPageRectangle
        ProjectSvsmWorldBoundsToPages(
            const box3& worldBounds,
            const float4x4& worldToClip)
        {
            if (!IsFiniteSvsmBox(worldBounds))
                return {};

            float2 minimumVirtual =
                float2(std::numeric_limits<float>::infinity());
            float2 maximumVirtual =
                float2(-std::numeric_limits<float>::infinity());
            for (uint32_t corner = 0u; corner < 8u; ++corner)
            {
                const float3 worldPosition = {
                    (corner & 1u) != 0u
                        ? worldBounds.m_maxs.x
                        : worldBounds.m_mins.x,
                    (corner & 2u) != 0u
                        ? worldBounds.m_maxs.y
                        : worldBounds.m_mins.y,
                    (corner & 4u) != 0u
                        ? worldBounds.m_maxs.z
                        : worldBounds.m_mins.z
                };
                const float4 clip =
                    float4(worldPosition, 1.f) * worldToClip;
                if (!all(isfinite(clip)) || !(clip.w > 1e-8f))
                    return {};
                const float2 ndc = clip.xy() / clip.w;
                const float2 virtualPosition =
                    (ndc * float2(0.5f, -0.5f) +
                        float2(0.5f)) *
                    float(SvsmVirtualResolution);
                minimumVirtual = min(
                    minimumVirtual, virtualPosition);
                maximumVirtual = max(
                    maximumVirtual, virtualPosition);
            }
            return GetSvsmPacketPageRectangle(
                minimumVirtual.x,
                minimumVirtual.y,
                maximumVirtual.x,
                maximumVirtual.y);
        }

        [[nodiscard]] SvsmTightInvalidationProjection
        ProjectSvsmObjectBoundsToPages(
            const box3& objectBounds,
            const std::array<float, 12u>& localToWorldSignature,
            const float4x4& worldToClip)
        {
            SvsmTightInvalidationProjection result;
            if (!IsFiniteSvsmBox(objectBounds))
                return result;

            affine3 localToWorld;
            if (!TryMakeSvsmAffineFromTransformSignature(
                    localToWorldSignature,
                    localToWorld))
            {
                return result;
            }

            std::array<std::array<float, 4u>, 8u> clipCorners{};
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
                const float3 worldPosition =
                    localToWorld.transformPoint(objectPosition);
                const float4 clip =
                    float4(worldPosition, 1.f) * worldToClip;
                clipCorners[corner] = {
                    clip.x,
                    clip.y,
                    clip.z,
                    clip.w
                };
            }
            return ProjectSvsmClipCornersForInvalidation(
                clipCorners);
        }

        bool GatherSvsmCasterSnapshots(
            const std::shared_ptr<SceneGraphNode>& rootNode,
            const SvsmObjectInvalidationPolicyConfiguration&
                policyConfiguration,
            std::vector<SvsmCasterSnapshot>& snapshots,
            bool& containsAlwaysMode,
            bool& policyResolutionReliable)
        {
            snapshots.clear();
            containsAlwaysMode = false;
            policyResolutionReliable =
                policyConfiguration.valid;
            if (!rootNode || !policyConfiguration.valid)
                return false;

            bool reliable = true;
            std::unordered_map<SvsmCasterKey, uint8_t, SvsmCasterKeyHash>
                seenKeys;
            SceneGraphWalker walker(rootNode.get());
            while (walker)
            {
                const SceneContentFlags relevantContentFlags =
                    SceneContentFlags::OpaqueMeshes |
                    SceneContentFlags::AlphaTestedMeshes;
                const bool subgraphRelevant =
                    (walker->GetSubgraphContentFlags() &
                        relevantContentFlags) != 0;
                const bool leafRelevant =
                    (walker->GetLeafContentFlags() &
                        relevantContentFlags) != 0;

                if (leafRelevant)
                {
                    const auto instanceReference =
                        std::dynamic_pointer_cast<MeshInstance>(
                            walker->GetLeaf());
                    auto* instance = instanceReference.get();
                    const MeshInfo* mesh =
                        instance ? instance->GetMesh().get() : nullptr;
                    SceneGraphNode* node =
                        instance ? instance->GetNode() : nullptr;
                    if (!instance ||
                        !mesh ||
                        !node ||
                        !mesh->buffers ||
                        instance->GetInstanceIndex() < 0)
                    {
                        reliable = false;
                    }
                    else
                    {
                        const bool deforming =
                            bool(mesh->skinPrototype) ||
                            mesh->isSkinPrototype ||
                            mesh->isMorphTargetAnimationMesh;
                        const auto* skinnedInstance =
                            dynamic_cast<const SkinnedMeshInstance*>(
                                instance);
                        const affine3 localToWorld =
                            node->GetLocalToWorldTransformFloat();
                        const auto transformSignature =
                            MakeSvsmTransformSignature(localToWorld);
                        const bool transformReliable =
                            IsFiniteSvsmTransformSignature(
                                transformSignature);

                        for (size_t geometryOrdinal = 0u;
                            geometryOrdinal <
                                mesh->geometries.size();
                            ++geometryOrdinal)
                        {
                            const auto& geometry =
                                mesh->geometries[geometryOrdinal];
                            if (!geometry ||
                                geometry->type !=
                                    MeshGeometryPrimitiveType::Triangles)
                            {
                                continue;
                            }
                            const Material* material =
                                geometry->material.get();
                            if (!material ||
                                (material->domain !=
                                        MaterialDomain::Opaque &&
                                    material->domain !=
                                        MaterialDomain::AlphaTested))
                            {
                                continue;
                            }

                            SvsmCasterSnapshot snapshot;
                            snapshot.key = {
                                instance,
                                geometry.get()
                            };
                            snapshot.instanceReference =
                                instanceReference;
                            snapshot.geometryReference = geometry;
                            const
                                SvsmObjectInvalidationResolverKey
                                    resolverKey = {
                                        uint32_t(instance->
                                            GetInstanceIndex()),
                                        geometryOrdinal <=
                                            std::numeric_limits<
                                                uint32_t>::max()
                                            ? uint32_t(
                                                geometryOrdinal)
                                            : 0u,
                                        geometry.get(),
                                        instance
                                    };
                            if (geometryOrdinal >
                                    std::numeric_limits<
                                        uint32_t>::max() ||
                                !TryResolveSvsmObjectInvalidationMode(
                                    policyConfiguration.defaultMode,
                                    policyConfiguration.resolverEnabled
                                        ? &policyConfiguration.resolver
                                        : nullptr,
                                    resolverKey,
                                    snapshot.invalidationMode))
                            {
                                reliable = false;
                                policyResolutionReliable = false;
                            }
                            containsAlwaysMode =
                                containsAlwaysMode ||
                                snapshot.invalidationMode ==
                                    SvsmObjectInvalidationMode::
                                        Always;
                            snapshot.localToWorld =
                                transformSignature;
                            snapshot.material =
                                GetSvsmMaterialShadowSignature(
                                    material);
                            snapshot.topology =
                                GetSvsmCasterTopologySignature(
                                    *mesh, *geometry);
                            snapshot.deforming = deforming;
                            snapshot.deformationRevisionReliable =
                                !deforming ||
                                (skinnedInstance != nullptr &&
                                    !mesh->
                                        isMorphTargetAnimationMesh);
                            snapshot.deformationRevision =
                                skinnedInstance
                                ? skinnedInstance->
                                    GetLastUpdateFrameIndex()
                                : 0u;
                            snapshot.objectSpaceBounds =
                                geometry->objectSpaceBounds;
                            // Donut's node/global boxes are derived from the
                            // bind-pose mesh box and are not an application
                            // guarantee that encloses arbitrary skin or morph
                            // deformation. Never project them as localized
                            // invalidation bounds.
                            snapshot.worldBounds = deforming
                                ? box3::empty()
                                : snapshot.objectSpaceBounds *
                                    localToWorld;
                            snapshot.reliableBounds =
                                !deforming &&
                                transformReliable &&
                                IsFiniteSvsmBox(
                                    snapshot.worldBounds);
                            snapshot.staticCacheEligible =
                                snapshot.reliableBounds &&
                                !deforming &&
                                material->domain ==
                                    MaterialDomain::Opaque &&
                                uint32_t(
                                    instance->GetInstanceIndex()) <=
                                    SvsmPacketObjectInstanceMask;
                            snapshot.staticCacheCandidate =
                                snapshot.staticCacheEligible;
                            snapshot.promotionDeadline =
                                SvsmNoPromotionDeadline;

                            const bool snapshotTrackable =
                                transformReliable &&
                                IsSvsmObjectInvalidationModeValid(
                                    snapshot.invalidationMode) &&
                                IsSvsmCasterSnapshotTrackable(
                                    snapshot.deforming,
                                    snapshot.reliableBounds,
                                    snapshot.
                                        deformationRevisionReliable);
                            if (!snapshotTrackable ||
                                !seenKeys.emplace(
                                    snapshot.key, uint8_t(1u)).second)
                            {
                                reliable = false;
                            }
                            snapshots.push_back(
                                std::move(snapshot));
                        }
                    }
                }

                walker.Next(subgraphRelevant);
            }
            return reliable;
        }

        bool ReconcileSvsmAdaptiveCasterClassification(
            const std::vector<SvsmCasterSnapshot>& previousObserved,
            const std::vector<SvsmCasterSnapshot>& previousPublished,
            std::vector<SvsmCasterSnapshot>& current,
            bool adaptiveClassificationEnabled,
            uint64_t successfulSparseStateCommits,
            bool& classificationTransition)
        {
            classificationTransition = false;
            std::unordered_map<SvsmCasterKey, size_t, SvsmCasterKeyHash>
                previousObservedByKey;
            previousObservedByKey.reserve(previousObserved.size());
            for (size_t index = 0u;
                index < previousObserved.size();
                ++index)
            {
                if (!previousObservedByKey.emplace(
                        previousObserved[index].key, index).second)
                {
                    return false;
                }
            }
            std::unordered_map<SvsmCasterKey, size_t, SvsmCasterKeyHash>
                previousPublishedByKey;
            previousPublishedByKey.reserve(previousPublished.size());
            for (size_t index = 0u;
                index < previousPublished.size();
                ++index)
            {
                if (!previousPublishedByKey.emplace(
                        previousPublished[index].key, index).second ||
                    previousObservedByKey.find(
                        previousPublished[index].key) ==
                            previousObservedByKey.end())
                {
                    return false;
                }
            }

            for (SvsmCasterSnapshot& snapshot : current)
            {
                const auto previousObservedIt =
                    previousObservedByKey.find(snapshot.key);
                const SvsmCasterSnapshot* oldObserved =
                    previousObservedIt == previousObservedByKey.end()
                    ? nullptr
                    : &previousObserved[
                        previousObservedIt->second];
                const auto previousPublishedIt =
                    previousPublishedByKey.find(snapshot.key);
                const SvsmCasterSnapshot* oldPublished =
                    previousPublishedIt ==
                        previousPublishedByKey.end()
                    ? nullptr
                    : &previousPublished[
                        previousPublishedIt->second];

                if (!oldObserved)
                {
                    if (oldPublished)
                        return false;
                    // Donut exposes no authoritative primitive mobility.
                    // Treat a newly observed rigid opaque caster as authored
                    // static, then demote it immediately on its first
                    // shadow-relevant change. This preserves fast initial
                    // warmup while learning movable behavior over time.
                    snapshot.staticCacheCandidate =
                        snapshot.staticCacheEligible;
                    snapshot.promotionDeadline =
                        SvsmNoPromotionDeadline;
                }
                else
                {
                    if (!oldPublished)
                        return false;
                    const SvsmCasterEventDecision eventDecision =
                        ReconcileSvsmCasterEvent(
                            snapshot.invalidationMode,
                            GetSvsmCasterEvent(
                                oldPublished,
                                &snapshot,
                                false));
                    if (eventDecision.category ==
                        SvsmCasterEventCategory::Unexplained)
                    {
                        return false;
                    }
                    const bool depthMatchesPublished =
                        eventDecision.category ==
                            SvsmCasterEventCategory::Unchanged ||
                        eventDecision.category ==
                            SvsmCasterEventCategory::BindingOnly;
                    const SvsmCasterSnapshot* classificationBaseline =
                        depthMatchesPublished
                        ? oldPublished
                        : oldObserved;
                    const bool invalidated =
                        eventDecision.category ==
                            SvsmCasterEventCategory::Invalidating;
                    const SvsmAdaptiveCasterDeadlineClassification
                        classification =
                            AdvanceSvsmAdaptiveCasterDeadlineClassification(
                                adaptiveClassificationEnabled,
                                snapshot.staticCacheEligible,
                                invalidated,
                                classificationBaseline->
                                    staticCacheCandidate,
                                classificationBaseline->
                                    promotionDeadline,
                                successfulSparseStateCommits);
                    snapshot.staticCacheCandidate =
                        classification.staticCacheCandidate;
                    snapshot.promotionDeadline =
                        classification.promotionDeadline;
                    snapshot.suppressedCoverageDebt =
                        ShouldAccumulateSvsmSuppressedCoverageDebt(
                            oldObserved->suppressedCoverageDebt,
                            eventDecision.category);
                }

                if (oldObserved &&
                    oldObserved->staticCacheCandidate !=
                        snapshot.staticCacheCandidate)
                {
                    classificationTransition = true;
                }
            }
            return true;
        }

        [[nodiscard]] uint64_t GetSvsmMinimumPromotionDeadline(
            const std::vector<SvsmCasterSnapshot>& snapshots)
        {
            uint64_t minimumDeadline = SvsmNoPromotionDeadline;
            for (const SvsmCasterSnapshot& snapshot : snapshots)
            {
                if (snapshot.staticCacheEligible &&
                    !snapshot.staticCacheCandidate)
                {
                    minimumDeadline = std::min(
                        minimumDeadline,
                        snapshot.promotionDeadline);
                }
            }
            return minimumDeadline;
        }

        bool PromoteDueSvsmDynamicCasters(
            std::vector<SvsmCasterSnapshot>& snapshots,
            uint64_t successfulSparseStateCommits,
            bool& classificationTransition)
        {
            classificationTransition = false;
            for (SvsmCasterSnapshot& snapshot : snapshots)
            {
                if (!snapshot.staticCacheEligible ||
                    snapshot.staticCacheCandidate ||
                    !IsSvsmDynamicCasterPromotionDue(
                        successfulSparseStateCommits,
                        snapshot.promotionDeadline))
                {
                    continue;
                }
                snapshot.staticCacheCandidate = true;
                snapshot.promotionDeadline =
                    SvsmNoPromotionDeadline;
                classificationTransition = true;
            }
            return true;
        }

        bool AppendSvsmSnapshotInvalidationPages(
            const SvsmCasterSnapshot& snapshot,
            bool staticDirty,
            const std::array<std::shared_ptr<PlanarView>,
                SvsmClipmapCount>& views,
            bool tightLocalizedInvalidationBoundsEnabled,
            std::array<std::bitset<SvsmPagesPerClipmap>,
                SvsmClipmapCount>& dynamicPages,
            std::array<std::bitset<SvsmPagesPerClipmap>,
                SvsmClipmapCount>& staticPages)
        {
            if (!snapshot.reliableBounds)
                return false;

            for (uint32_t level = 0u;
                level < SvsmClipmapCount;
                ++level)
            {
                if (!views[level])
                    return false;
                const float4x4 worldToClip =
                    views[level]->GetViewProjectionMatrix(false);
                SvsmPacketPageRectangle rectangle;
                if (tightLocalizedInvalidationBoundsEnabled)
                {
                    const SvsmTightInvalidationProjection
                        projection =
                            ProjectSvsmObjectBoundsToPages(
                                snapshot.objectSpaceBounds,
                                snapshot.localToWorld,
                                worldToClip);
                    if (!projection.valid)
                        return false;
                    rectangle = projection.pages;
                }
                else
                {
                    rectangle =
                        ProjectSvsmWorldBoundsToPages(
                            snapshot.worldBounds,
                            worldToClip);
                }
                if (rectangle.packedMinimum ==
                        SvsmInvalidPacketPageBounds ||
                    rectangle.packedMaximum ==
                        SvsmInvalidPacketPageBounds)
                {
                    return false;
                }
                if (rectangle.packedMinimum ==
                        SvsmEmptyPacketPageBounds &&
                    rectangle.packedMaximum ==
                        SvsmEmptyPacketPageBounds)
                {
                    continue;
                }
                if (rectangle.packedMinimum ==
                        SvsmEmptyPacketPageBounds ||
                    rectangle.packedMaximum ==
                        SvsmEmptyPacketPageBounds)
                {
                    return false;
                }

                const SvsmPageCoordinate minimum =
                    UnpackSvsmPacketPageCoordinate(
                        rectangle.packedMinimum);
                const SvsmPageCoordinate maximum =
                    UnpackSvsmPacketPageCoordinate(
                        rectangle.packedMaximum);
                if (minimum.x < 0 ||
                    minimum.y < 0 ||
                    maximum.x < minimum.x ||
                    maximum.y < minimum.y ||
                    maximum.x >= int32_t(SvsmPagesPerAxis) ||
                    maximum.y >= int32_t(SvsmPagesPerAxis))
                {
                    return false;
                }

                for (int32_t y = minimum.y;
                    y <= maximum.y;
                    ++y)
                {
                    for (int32_t x = minimum.x;
                        x <= maximum.x;
                        ++x)
                    {
                        const uint32_t localPage =
                            uint32_t(y) * SvsmPagesPerAxis +
                            uint32_t(x);
                        if (staticDirty)
                            staticPages[level].set(localPage);
                        else
                            dynamicPages[level].set(localPage);
                    }
                }
            }
            return true;
        }

        struct SvsmLocalizedInvalidationReconciliation
        {
            std::array<uint32_t, 5u> observedCategories{};
            std::array<uint32_t, 5u> publishedCategories{};
            bool observedSceneEvent = false;
            bool unexplainedEvent = false;
        };

        void RecordSvsmCasterEventCategory(
            std::array<uint32_t, 5u>& categories,
            SvsmCasterEventCategory category)
        {
            const uint32_t index = uint32_t(category);
            if (index < categories.size())
                ++categories[index];
        }

        bool BuildSvsmLocalizedInvalidationPages(
            const std::vector<SvsmCasterSnapshot>& previousObserved,
            const std::vector<SvsmCasterSnapshot>& previousPublished,
            std::vector<SvsmCasterSnapshot>& currentObserved,
            const std::array<std::shared_ptr<PlanarView>,
                SvsmClipmapCount>& views,
            bool tightLocalizedInvalidationBoundsEnabled,
            bool requireExplainedSceneStateChange,
            bool callerChangeChannelsExhaustive,
            std::vector<uint32_t>& pages,
            std::vector<SvsmCasterSnapshot>& nextPublished,
            SvsmLocalizedInvalidationReconciliation& reconciliation)
        {
            pages.clear();
            nextPublished.clear();
            reconciliation = {};
            if (previousObserved.size() != previousPublished.size())
            {
                reconciliation.unexplainedEvent = true;
                return false;
            }
            std::unordered_map<SvsmCasterKey, size_t, SvsmCasterKeyHash>
                previousObservedByKey;
            previousObservedByKey.reserve(previousObserved.size());
            for (size_t index = 0u;
                index < previousObserved.size();
                ++index)
            {
                const bool trackable =
                    IsSvsmObjectInvalidationModeValid(
                        previousObserved[index].
                            invalidationMode) &&
                    IsSvsmCasterSnapshotTrackable(
                        previousObserved[index].deforming,
                        previousObserved[index].reliableBounds,
                        previousObserved[index].
                            deformationRevisionReliable);
                if (!trackable ||
                    !previousObservedByKey.emplace(
                        previousObserved[index].key, index).second)
                {
                    reconciliation.unexplainedEvent = true;
                    return false;
                }
            }
            std::unordered_map<SvsmCasterKey, size_t, SvsmCasterKeyHash>
                previousPublishedByKey;
            previousPublishedByKey.reserve(previousPublished.size());
            for (size_t index = 0u;
                index < previousPublished.size();
                ++index)
            {
                const bool trackable =
                    IsSvsmObjectInvalidationModeValid(
                        previousPublished[index].
                            invalidationMode) &&
                    IsSvsmCasterSnapshotTrackable(
                        previousPublished[index].deforming,
                        previousPublished[index].reliableBounds,
                        previousPublished[index].
                            deformationRevisionReliable);
                if (!trackable ||
                    !previousPublishedByKey.emplace(
                        previousPublished[index].key, index).second ||
                    previousObservedByKey.find(
                        previousPublished[index].key) ==
                            previousObservedByKey.end())
                {
                    reconciliation.unexplainedEvent = true;
                    return false;
                }
            }

            std::vector<bool> previousObservedSeen(
                previousObserved.size(), false);
            std::vector<bool> previousPublishedSeen(
                previousPublished.size(), false);
            std::array<std::bitset<SvsmPagesPerClipmap>,
                SvsmClipmapCount> dynamicPages;
            std::array<std::bitset<SvsmPagesPerClipmap>,
                SvsmClipmapCount> staticPages;
            nextPublished.reserve(currentObserved.size());

            for (SvsmCasterSnapshot& snapshot : currentObserved)
            {
                const bool trackable =
                    IsSvsmObjectInvalidationModeValid(
                        snapshot.invalidationMode) &&
                    IsSvsmCasterSnapshotTrackable(
                        snapshot.deforming,
                        snapshot.reliableBounds,
                        snapshot.deformationRevisionReliable);
                if (!trackable)
                {
                    reconciliation.unexplainedEvent = true;
                    return false;
                }

                const auto previousObservedIt =
                    previousObservedByKey.find(snapshot.key);
                const SvsmCasterSnapshot* oldObserved =
                    previousObservedIt == previousObservedByKey.end()
                    ? nullptr
                    : &previousObserved[
                        previousObservedIt->second];
                if (oldObserved)
                {
                    previousObservedSeen[
                        previousObservedIt->second] = true;
                }

                const SvsmCasterEventDecision observedDecision =
                    ReconcileSvsmCasterEvent(
                        snapshot.invalidationMode,
                        GetSvsmCasterEvent(
                            oldObserved,
                            &snapshot,
                            true));
                RecordSvsmCasterEventCategory(
                    reconciliation.observedCategories,
                    observedDecision.category);
                reconciliation.observedSceneEvent =
                    reconciliation.observedSceneEvent ||
                    observedDecision.sceneEventPresent;
                if (observedDecision.category ==
                    SvsmCasterEventCategory::Unexplained)
                {
                    reconciliation.unexplainedEvent = true;
                    return false;
                }

                const auto previousPublishedIt =
                    previousPublishedByKey.find(snapshot.key);
                const SvsmCasterSnapshot* oldPublished =
                    previousPublishedIt ==
                        previousPublishedByKey.end()
                    ? nullptr
                    : &previousPublished[
                        previousPublishedIt->second];
                if ((oldObserved == nullptr) !=
                    (oldPublished == nullptr))
                {
                    reconciliation.unexplainedEvent = true;
                    return false;
                }
                if (oldPublished)
                {
                    previousPublishedSeen[
                        previousPublishedIt->second] = true;
                }

                const SvsmCasterEventDecision publishedDecision =
                    ReconcileSvsmCasterEvent(
                        snapshot.invalidationMode,
                        GetSvsmCasterEvent(
                            oldPublished,
                            &snapshot,
                            true));
                RecordSvsmCasterEventCategory(
                    reconciliation.publishedCategories,
                    publishedDecision.category);
                if (publishedDecision.category ==
                    SvsmCasterEventCategory::Unexplained)
                {
                    reconciliation.unexplainedEvent = true;
                    return false;
                }

                const bool previousSuppressedCoverageDebt =
                    oldObserved &&
                    oldObserved->suppressedCoverageDebt;
                snapshot.suppressedCoverageDebt =
                    ShouldAccumulateSvsmSuppressedCoverageDebt(
                        previousSuppressedCoverageDebt,
                        publishedDecision.category);
                if (RequiresSvsmFullRefreshForSuppressedCoverageDebt(
                        previousSuppressedCoverageDebt,
                        observedDecision) ||
                    RequiresSvsmFullRefreshForSuppressedCoverageDebt(
                        previousSuppressedCoverageDebt,
                        publishedDecision))
                {
                    // Cached pages may contain any suppressed intermediate
                    // state. Old/current bounds alone are insufficient to
                    // clear that union when the policy, layer, topology, or
                    // caster state next becomes authoritative.
                    reconciliation.unexplainedEvent = true;
                    return false;
                }

                switch (publishedDecision.publishedAction)
                {
                case SvsmPublishedCasterAction::PublishCurrent:
                {
                    const SvsmCasterInvalidationClasses
                        invalidationClasses =
                            GetSvsmCasterInvalidationClasses(
                                oldPublished
                                    ? oldPublished->
                                        staticCacheCandidate
                                    : false,
                                snapshot.staticCacheCandidate);
                    if ((publishedDecision.
                                invalidatePreviousCoverage &&
                            (!oldPublished ||
                                !AppendSvsmSnapshotInvalidationPages(
                                    *oldPublished,
                                    invalidationClasses.
                                        previousStatic,
                                    views,
                                    tightLocalizedInvalidationBoundsEnabled,
                                    dynamicPages,
                                    staticPages))) ||
                        (publishedDecision.
                                invalidateCurrentCoverage &&
                            !AppendSvsmSnapshotInvalidationPages(
                                snapshot,
                                invalidationClasses.currentStatic,
                                views,
                                tightLocalizedInvalidationBoundsEnabled,
                                dynamicPages,
                                staticPages)))
                    {
                        reconciliation.unexplainedEvent = true;
                        return false;
                    }
                    nextPublished.push_back(snapshot);
                    break;
                }
                case SvsmPublishedCasterAction::Retain:
                    if (!oldPublished)
                    {
                        reconciliation.unexplainedEvent = true;
                        return false;
                    }
                    nextPublished.push_back(*oldPublished);
                    break;
                case SvsmPublishedCasterAction::Remove:
                default:
                    reconciliation.unexplainedEvent = true;
                    return false;
                }
            }

            for (size_t index = 0u;
                index < previousObserved.size();
                ++index)
            {
                if (previousObservedSeen[index])
                    continue;
                const SvsmCasterEventDecision observedDecision =
                    ReconcileSvsmCasterEvent(
                        previousObserved[index].invalidationMode,
                        GetSvsmCasterEvent(
                            &previousObserved[index],
                            nullptr,
                            true));
                RecordSvsmCasterEventCategory(
                    reconciliation.observedCategories,
                    observedDecision.category);
                reconciliation.observedSceneEvent =
                    reconciliation.observedSceneEvent ||
                    observedDecision.sceneEventPresent;
                if (previousObserved[index].
                        suppressedCoverageDebt)
                {
                    reconciliation.unexplainedEvent = true;
                    return false;
                }
                if (observedDecision.category !=
                        SvsmCasterEventCategory::Invalidating ||
                    !observedDecision.invalidatePreviousCoverage ||
                    !AppendSvsmSnapshotInvalidationPages(
                        previousObserved[index],
                        previousObserved[index].
                            staticCacheCandidate,
                        views,
                        tightLocalizedInvalidationBoundsEnabled,
                        dynamicPages,
                        staticPages))
                {
                    reconciliation.unexplainedEvent = true;
                    return false;
                }
            }

            for (size_t index = 0u;
                index < previousPublished.size();
                ++index)
            {
                if (previousPublishedSeen[index])
                    continue;
                const SvsmCasterEventDecision publishedDecision =
                    ReconcileSvsmCasterEvent(
                        previousPublished[index].
                            invalidationMode,
                        GetSvsmCasterEvent(
                            &previousPublished[index],
                            nullptr,
                            true));
                RecordSvsmCasterEventCategory(
                    reconciliation.publishedCategories,
                    publishedDecision.category);
                if (publishedDecision.category !=
                        SvsmCasterEventCategory::Invalidating ||
                    !publishedDecision.invalidatePreviousCoverage ||
                    !AppendSvsmSnapshotInvalidationPages(
                        previousPublished[index],
                        previousPublished[index].
                            staticCacheCandidate,
                        views,
                        tightLocalizedInvalidationBoundsEnabled,
                        dynamicPages,
                        staticPages))
                {
                    reconciliation.unexplainedEvent = true;
                    return false;
                }
            }

            if (!IsSvsmObservedSceneChangeExplained(
                    requireExplainedSceneStateChange,
                    reconciliation.observedSceneEvent,
                    reconciliation.unexplainedEvent,
                    callerChangeChannelsExhaustive))
            {
                // A scene hash or reliable revision changed without a
                // corresponding observed caster event in the exhaustive
                // snapshot schema. Conservatively refresh the full cache.
                reconciliation.unexplainedEvent = true;
                return false;
            }

            pages.reserve(
                SvsmPagesPerClipmap * SvsmClipmapCount);
            for (uint32_t level = 0u;
                level < SvsmClipmapCount;
                ++level)
            {
                for (uint32_t localPage = 0u;
                    localPage < SvsmPagesPerClipmap;
                    ++localPage)
                {
                    if (staticPages[level].test(localPage))
                    {
                        pages.push_back(
                            PackSvsmLocalInvalidationPage(
                                level * SvsmPagesPerClipmap +
                                    localPage,
                                true));
                    }
                    else if (dynamicPages[level].test(localPage))
                    {
                        pages.push_back(
                            PackSvsmLocalInvalidationPage(
                                level * SvsmPagesPerClipmap +
                                    localPage,
                                false));
                    }
                }
            }
            return pages.size() <=
                SvsmPagesPerClipmap * SvsmClipmapCount;
        }

        bool TryBuildSvsmCommonLightBounds(
            const box3& objectBounds,
            const affine3& localToWorld,
            const affine3& worldToLight,
            box3& lightBounds)
        {
            lightBounds = box3::empty();
            if (!IsFiniteSvsmBox(objectBounds))
                return false;

            float3 minimumLight =
                float3(std::numeric_limits<float>::infinity());
            float3 maximumLight =
                float3(-std::numeric_limits<float>::infinity());
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
                const float3 lightPosition =
                    worldToLight.transformPoint(
                        localToWorld.transformPoint(objectPosition));
                if (!dm::all(dm::isfinite(lightPosition)))
                    return false;
                minimumLight = dm::min(minimumLight, lightPosition);
                maximumLight = dm::max(maximumLight, lightPosition);
            }
            lightBounds.m_mins = minimumLight;
            lightBounds.m_maxs = maximumLight;
            return IsFiniteSvsmBox(lightBounds);
        }

        bool TryBuildSvsmCommonLightBounds(
            const DrawItem& item,
            const affine3& localToWorld,
            const affine3& worldToLight,
            box3& lightBounds)
        {
            if (!item.instance ||
                !item.mesh ||
                !item.geometry ||
                !CanUseSvsmStaticPacketBounds(
                    bool(item.mesh->skinPrototype),
                    item.mesh->isSkinPrototype,
                    item.mesh->isMorphTargetAnimationMesh))
            {
                lightBounds = box3::empty();
                return false;
            }
            return TryBuildSvsmCommonLightBounds(
                item.geometry->objectSpaceBounds,
                localToWorld,
                worldToLight,
                lightBounds);
        }

        SparseVirtualShadowMapPacketMetadata
        BuildPacketPageMetadataFromCommonLightBounds(
            uint32_t objectInstanceIndex,
            const box3& lightBounds,
            const float4x4& lightToClip,
            uint32_t pageListOffset,
            uint32_t& pageListCapacity)
        {
            SparseVirtualShadowMapPacketMetadata metadata = {};
            metadata.packedMinimumPage =
                SvsmInvalidPacketPageBounds;
            metadata.packedMaximumPage =
                SvsmInvalidPacketPageBounds;
            metadata.pageListOffset = pageListOffset;
            metadata.objectInstanceIndex = objectInstanceIndex;
            metadata.packedMinimumTexel =
                SvsmInvalidPacketPageBounds;
            metadata.packedMaximumTexel =
                SvsmInvalidPacketPageBounds;
            metadata.nearestReverseDepth = 0u;
            pageListCapacity = 0u;
            if (!IsFiniteSvsmBox(lightBounds))
            {
                return metadata;
            }

            float2 minimumVirtual =
                float2(std::numeric_limits<float>::infinity());
            float2 maximumVirtual =
                float2(-std::numeric_limits<float>::infinity());
            float nearestReverseDepth =
                -std::numeric_limits<float>::infinity();
            for (uint32_t corner = 0u; corner < 8u; ++corner)
            {
                const float3 lightPosition = {
                    (corner & 1u) != 0u
                        ? lightBounds.m_maxs.x
                        : lightBounds.m_mins.x,
                    (corner & 2u) != 0u
                        ? lightBounds.m_maxs.y
                        : lightBounds.m_mins.y,
                    (corner & 4u) != 0u
                        ? lightBounds.m_maxs.z
                        : lightBounds.m_mins.z
                };
                const float4 clip =
                    float4(lightPosition, 1.f) * lightToClip;
                if (!dm::all(dm::isfinite(clip)) ||
                    !(clip.w > 1e-8f))
                {
                    return metadata;
                }
                const float2 ndc = clip.xy() / clip.w;
                const float ndcDepth = clip.z / clip.w;
                if (!std::isfinite(ndcDepth))
                    return metadata;
                const float2 virtualPosition =
                    (ndc * float2(0.5f, -0.5f) +
                        float2(0.5f)) *
                    float(SvsmVirtualResolution);
                minimumVirtual =
                    dm::min(minimumVirtual, virtualPosition);
                maximumVirtual =
                    dm::max(maximumVirtual, virtualPosition);
                nearestReverseDepth = std::max(
                    nearestReverseDepth, ndcDepth);
            }

            if (!dm::all(dm::isfinite(minimumVirtual)) ||
                !dm::all(dm::isfinite(maximumVirtual)) ||
                !std::isfinite(nearestReverseDepth))
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
            const SvsmPacketTexelRectangle texelRectangle =
                GetSvsmPacketTexelRectangle(
                    minimumVirtual.x,
                    minimumVirtual.y,
                    maximumVirtual.x,
                    maximumVirtual.y);
            metadata.packedMinimumTexel =
                texelRectangle.packedMinimum;
            metadata.packedMaximumTexel =
                texelRectangle.packedMaximum;
            const float conservativeNearestReverseDepth =
                std::nextafter(
                    std::clamp(nearestReverseDepth, 0.f, 1.f),
                    std::numeric_limits<float>::infinity());
            metadata.nearestReverseDepth =
                FloatBits(conservativeNearestReverseDepth);
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

    struct SparseVirtualShadowMapPass::CasterSnapshotState
    {
        // Observed state follows the live scene after a successful
        // transaction. Published state follows only depth that was actually
        // invalidated and rendered; policy-suppressed changes must not move it.
        std::vector<SvsmCasterSnapshot> observed;
        std::vector<SvsmCasterSnapshot> published;
        std::vector<SvsmCasterSnapshot> pendingObserved;
        std::vector<SvsmCasterSnapshot> pendingPublished;
        std::shared_ptr<SceneGraphNode> root;
        std::shared_ptr<SceneGraphNode> pendingRoot;
        SvsmObjectInvalidationPolicyConfiguration
            policyConfiguration;
        SvsmObjectInvalidationPolicyConfiguration
            pendingPolicyConfiguration;
        bool containsAlwaysMode = false;
        bool pendingContainsAlwaysMode = false;
        bool policyResolutionReliable = true;
        bool pendingPolicyResolutionReliable = true;
        bool adaptiveClassificationEnabled = true;
        bool pendingAdaptiveClassificationEnabled = true;
        uint64_t classificationGeneration = 0u;
        uint64_t pendingClassificationGeneration = 0u;
        uint64_t successfulSparseStateCommits = 0u;
        uint64_t minimumPromotionDeadline =
            SvsmNoPromotionDeadline;
        uint64_t pendingMinimumPromotionDeadline =
            SvsmNoPromotionDeadline;
        bool valid = false;
        bool reliable = false;
        bool pendingReady = false;
        bool pendingReliable = false;
    };

    [[nodiscard]] bool IsSvsmRasterStateComplete(
        const nvrhi::GraphicsState& state,
        bool indirectSubmission)
    {
        if (!state.pipeline ||
            !state.framebuffer ||
            !state.indexBuffer.buffer ||
            (indirectSubmission && !state.indirectParams))
        {
            return false;
        }
        for (nvrhi::IBindingSet* bindingSet : state.bindings)
        {
            if (!bindingSet)
                return false;
        }
        return !state.bindings.empty();
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

        bool RenderViewReference(
            nvrhi::ICommandList* commandList,
            const IView* view,
            nvrhi::IFramebuffer* framebuffer,
            IDrawStrategy& drawStrategy,
            Context& context)
        {
            if (!commandList ||
                !view ||
                !framebuffer ||
                !m_ViewBindings)
            {
                return false;
            }

            SetupView(context, commandList, view, view);

            const Material* lastMaterial = nullptr;
            const BufferGroup* lastBuffers = nullptr;
            nvrhi::RasterCullMode lastCullMode =
                nvrhi::RasterCullMode::Back;
            bool stateValid = false;

            nvrhi::GraphicsState graphicsState;
            graphicsState.framebuffer = framebuffer;
            graphicsState.viewport = view->GetViewportState();
            graphicsState.shadingRateState =
                view->GetVariableRateShadingState();

            nvrhi::DrawArguments currentDraw;
            currentDraw.instanceCount = 0u;
            auto flushDraw = [&]() {
                if (currentDraw.instanceCount == 0u)
                    return;
                SetPushConstants(
                    context,
                    commandList,
                    graphicsState,
                    currentDraw);
                commandList->drawIndexed(currentDraw);
                currentDraw.instanceCount = 0u;
            };

            while (const DrawItem* item = drawStrategy.GetNextItem())
            {
                if (!item->material)
                    continue;
                if (!item->buffers ||
                    !item->geometry ||
                    !item->mesh ||
                    !item->instance ||
                    item->instance->GetInstanceIndex() < 0)
                {
                    return false;
                }

                const bool newBuffers =
                    item->buffers != lastBuffers;
                const bool newMaterial =
                    item->material != lastMaterial ||
                    item->cullMode != lastCullMode;
                if (newBuffers || newMaterial)
                    flushDraw();

                if (newBuffers)
                {
                    SetupInputBuffers(
                        context, item->buffers, graphicsState);
                    lastBuffers = item->buffers;
                    stateValid = false;
                }
                if (newMaterial)
                {
                    if (!SetupMaterial(
                            context,
                            item->material,
                            item->cullMode,
                            graphicsState))
                    {
                        return false;
                    }
                    lastMaterial = item->material;
                    lastCullMode = item->cullMode;
                    stateValid = false;
                }
                if (!IsSvsmRasterStateComplete(
                        graphicsState, false))
                {
                    return false;
                }
                if (!stateValid)
                {
                    commandList->setGraphicsState(graphicsState);
                    stateValid = true;
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
                    return false;
                }

                nvrhi::DrawArguments args;
                args.vertexCount = item->geometry->numIndices;
                args.instanceCount = 1u;
                args.startVertexLocation = uint32_t(vertexOffset);
                args.startIndexLocation = uint32_t(indexOffset);
                args.startInstanceLocation =
                    uint32_t(item->instance->GetInstanceIndex());

                if (currentDraw.instanceCount > 0u &&
                    currentDraw.startIndexLocation ==
                        args.startIndexLocation &&
                    uint64_t(currentDraw.startInstanceLocation) +
                            currentDraw.instanceCount ==
                        uint64_t(args.startInstanceLocation))
                {
                    ++currentDraw.instanceCount;
                }
                else
                {
                    flushDraw();
                    currentDraw = args;
                }
            }

            flushDraw();
            return true;
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
        nvrhi::IBuffer* m_ReceiverPageMasks;
        uint32_t m_PhysicalPageCount;
        bool m_PairedStaticDynamicDepthEnabled = false;
        bool m_DeferredStaticDepthMergeEnabled = false;
        nvrhi::ShaderHandle m_BatchedVertexShader;
        nvrhi::ShaderHandle m_PositionOnlyVertexShader;
        nvrhi::ShaderHandle m_BatchedPositionOnlyVertexShader;
        nvrhi::ShaderHandle m_MaterialFreeOpaquePixelShader;
        std::array<nvrhi::GraphicsPipelineHandle, PipelineKey::Count>
            m_BatchedPipelines;
        std::array<nvrhi::GraphicsPipelineHandle, PipelineKey::Count>
            m_PositionOnlyPipelines;
        std::array<nvrhi::GraphicsPipelineHandle, PipelineKey::Count>
            m_BatchedPositionOnlyPipelines;
        nvrhi::BindingLayoutHandle m_OpaqueViewBindingLayout;
        nvrhi::BindingSetHandle m_OpaqueViewBindings;
        bool m_BatchedDrawSupported = false;
        bool m_BatchedPipelineActive = false;
        bool m_OpaqueRasterSpecializationRequested = true;
        bool m_OpaqueRasterSpecializationSupported = false;
        bool m_OpaqueRasterSpecializationFailureLogged = false;
        bool m_LeanAlphaTestedBindingsEnabled = false;
        bool m_TrackMaterialBindingLiveness = true;
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

        struct SharedCasterRecord
        {
            DrawItem draw;
            std::shared_ptr<Material> materialReference;
            std::shared_ptr<BufferGroup> buffersReference;
            nvrhi::DrawArguments arguments;
            box3 commonLightBounds = box3::empty();
            uint32_t clipmapMask = 0u;
            bool reliableCommonLightBounds = false;
            bool staticCacheCandidate = false;
            bool argumentsValid = false;
            bool alphaTested = false;
            bool validatedStableDrawState = false;
        };

        struct PersistentCasterSourceRecord
        {
            std::shared_ptr<MeshInstance> instance;
            std::shared_ptr<MeshInfo> mesh;
            std::shared_ptr<MeshGeometry> geometry;
            std::shared_ptr<BufferGroup> buffers;
            std::shared_ptr<Material> material;
            affine3 localToWorld;
            box3 objectSpaceBounds = box3::empty();
            box3 worldBounds = box3::empty();
            SvsmCasterTopologySignature topology;
            SvsmMaterialShadowSignature materialSignature;
            nvrhi::RasterCullMode cullMode =
                nvrhi::RasterCullMode::Back;
            nvrhi::DrawArguments arguments;
            uint32_t instanceIndex = 0u;
            bool staticCacheCandidate = false;
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
        const DirectionalLight* m_RenderPacketLight = nullptr;
        const IDrawStrategy* m_RenderPacketDrawStrategy = nullptr;
        uint64_t m_RenderPacketSceneStateHash = 0u;
        uint64_t m_RenderPacketSceneStateRevision = 0u;
        bool m_RenderPacketSceneStateRevisionReliable = false;
        uint32_t m_RenderPacketCount = 0u;
        uint32_t m_RenderPacketPageEntryCount = 0u;
        uint32_t m_RenderPacketFirstClipmap = 0u;
        bool m_RenderPacketPageMetadataRequested = false;
        bool m_RenderPacketExactPageListsRequested = false;
        bool m_RenderPacketDirtyPageScatterRasterRequested = false;
        bool m_RenderPacketStateSortingRequested = false;
        bool m_RenderPacketSharedBuilderActive = false;
        bool m_RenderPacketPairedDepthClassificationActive = false;
        bool m_RenderPacketAdaptiveClassificationActive = false;
        uint64_t m_RenderPacketClassificationGeneration = 0u;
        bool m_RenderPacketPageMetadataSupported = false;
        bool m_RenderPacketPageDispatchSupported = false;
        bool m_RenderPacketCacheValid = false;
        std::vector<SharedCasterRecord> m_SharedCasterScratch;
        std::vector<uint32_t> m_SharedCasterMaskStackScratch;
        std::unordered_set<const SceneGraphNode*>
            m_SharedUnboundedDeformerAncestorsScratch;
        std::shared_ptr<SceneGraphNode>
            m_SharedUnboundedDeformerAncestorRoot;
        uint64_t m_SharedUnboundedDeformerAncestorSceneStateHash = 0u;
        bool m_SharedUnboundedDeformerAncestorCacheValid = false;
        std::array<float4x4, SvsmClipmapCount>
            m_SharedLightToClipMatrices{};
        std::vector<PersistentCasterSourceRecord>
            m_PersistentCasterSources;
        std::vector<PersistentCasterSourceRecord>
            m_PendingPersistentCasterSources;
        SvsmPersistentCasterSourceKey
            m_PersistentCasterSourceKey;
        SvsmPersistentCasterSourceKey
            m_PendingPersistentCasterSourceKey;
        bool m_PersistentCasterSourceCacheValid = false;
        bool m_PendingPersistentCasterSourceReady = false;
        bool m_PersistentCasterSourceRequested = false;
        bool m_PersistentCasterSourceActive = false;
        bool m_PersistentCasterSourceRebuilt = false;
        bool m_PersistentCasterSourceReused = false;
        std::unordered_map<SvsmCasterKey, bool, SvsmCasterKeyHash>
            m_AdaptiveStaticCasterClassification;
        bool m_AdaptiveStaticCasterClassificationActive = false;
        const void* m_AdaptiveStaticCasterClassificationRoot = nullptr;
        uint64_t m_AdaptiveStaticCasterClassificationGeneration = 0u;
        uint32_t m_AdaptiveStaticCasterClassificationRecordCount = 0u;

        [[nodiscard]] bool IsPacketStaticCacheCandidate(
            const DrawItem& item) const
        {
            if (!m_AdaptiveStaticCasterClassificationActive)
                return IsSvsmStaticCacheCandidate(item);
            if (!item.instance || !item.geometry)
                return false;
            const auto found =
                m_AdaptiveStaticCasterClassification.find({
                    item.instance,
                    item.geometry
                });
            return found !=
                    m_AdaptiveStaticCasterClassification.end() &&
                found->second;
        }

        [[nodiscard]] bool ValidateCachedPersistentCasterSourceRecord(
            const PersistentCasterSourceRecord& record) const
        {
            return record.instance &&
                record.mesh &&
                record.geometry &&
                record.buffers &&
                record.material &&
                record.topology.mesh == record.mesh.get() &&
                record.topology.buffers == record.buffers.get() &&
                record.topology.indexBuffer != nullptr &&
                record.topology.vertexBuffer != nullptr &&
                record.topology.instanceBuffer != nullptr &&
                record.materialSignature.material ==
                    record.material.get() &&
                record.topology.primitiveType ==
                    MeshGeometryPrimitiveType::Triangles &&
                (record.materialSignature.domain ==
                        MaterialDomain::Opaque ||
                    record.materialSignature.domain ==
                        MaterialDomain::AlphaTested) &&
                record.arguments.instanceCount == 1u &&
                record.arguments.startInstanceLocation ==
                    record.instanceIndex &&
                IsFiniteSvsmTransformSignature(
                    MakeSvsmTransformSignature(
                        record.localToWorld)) &&
                IsFiniteSvsmBox(record.objectSpaceBounds) &&
                IsFiniteSvsmBox(record.worldBounds);
        }

        [[nodiscard]] bool ValidatePersistentCasterSourceRecordForBuild(
            const PersistentCasterSourceRecord& record) const
        {
            return ValidateCachedPersistentCasterSourceRecord(record) &&
                record.instance->GetMesh().get() ==
                    record.mesh.get() &&
                record.mesh->buffers.get() ==
                    record.buffers.get() &&
                record.geometry->material.get() ==
                    record.material.get() &&
                record.topology.indexBuffer ==
                    record.buffers->indexBuffer.Get() &&
                record.topology.vertexBuffer ==
                    record.buffers->vertexBuffer.Get() &&
                record.topology.instanceBuffer ==
                    record.buffers->instanceBuffer.Get() &&
                record.geometry->type ==
                    record.topology.primitiveType &&
                record.material->domain ==
                    record.materialSignature.domain &&
                record.instance->GetInstanceIndex() >= 0 &&
                uint32_t(record.instance->GetInstanceIndex()) ==
                    record.instanceIndex;
        }

        bool PreparePersistentCasterSources(
            const std::shared_ptr<SceneGraphNode>& rootNode,
            const std::vector<SvsmCasterSnapshot>& snapshots,
            uint64_t sourceGeneration,
            uint64_t casterStateHash)
        {
            if (!rootNode ||
                sourceGeneration == 0u ||
                snapshots.size() >
                    std::numeric_limits<uint32_t>::max())
            {
                return false;
            }

            const SvsmPersistentCasterSourceKey key = {
                rootNode.get(),
                sourceGeneration,
                casterStateHash,
                uint32_t(snapshots.size()),
                true
            };
            if (m_PersistentCasterSourceCacheValid &&
                IsSameSvsmPersistentCasterSourceKey(
                    m_PersistentCasterSourceKey, key))
            {
                m_PersistentCasterSourceReused = true;
                return true;
            }

            std::vector<PersistentCasterSourceRecord> pending;
            pending.reserve(snapshots.size());
            std::unordered_set<SvsmCasterKey, SvsmCasterKeyHash>
                seenKeys;
            seenKeys.reserve(snapshots.size());
            for (const SvsmCasterSnapshot& snapshot : snapshots)
            {
                if (!snapshot.instanceReference ||
                    !snapshot.geometryReference ||
                    snapshot.key.instance !=
                        snapshot.instanceReference.get() ||
                    snapshot.key.geometry !=
                        snapshot.geometryReference.get() ||
                    !seenKeys.insert(snapshot.key).second ||
                    snapshot.deforming ||
                    !snapshot.reliableBounds ||
                    !IsFiniteSvsmBox(snapshot.objectSpaceBounds) ||
                    !IsFiniteSvsmBox(snapshot.worldBounds))
                {
                    return false;
                }

                const std::shared_ptr<MeshInfo> mesh =
                    snapshot.instanceReference->GetMesh();
                const std::shared_ptr<MeshGeometry> geometry =
                    snapshot.geometryReference;
                const std::shared_ptr<BufferGroup> buffers =
                    mesh ? mesh->buffers : nullptr;
                const std::shared_ptr<Material> material =
                    geometry ? geometry->material : nullptr;
                if (!mesh ||
                    !buffers ||
                    !material ||
                    !buffers->indexBuffer ||
                    !buffers->vertexBuffer ||
                    !buffers->instanceBuffer ||
                    geometry->type !=
                        MeshGeometryPrimitiveType::Triangles ||
                    (material->domain != MaterialDomain::Opaque &&
                        material->domain !=
                            MaterialDomain::AlphaTested) ||
                    std::find(
                        mesh->geometries.begin(),
                        mesh->geometries.end(),
                        geometry) == mesh->geometries.end() ||
                    !(GetSvsmCasterTopologySignature(
                        *mesh, *geometry) == snapshot.topology) ||
                    !(GetSvsmMaterialShadowSignature(
                        material.get()) == snapshot.material))
                {
                    return false;
                }

                affine3 localToWorld;
                if (!TryMakeSvsmAffineFromTransformSignature(
                        snapshot.localToWorld, localToWorld))
                {
                    return false;
                }
                const box3 rebuiltWorldBounds =
                    snapshot.objectSpaceBounds * localToWorld;
                if (!IsFiniteSvsmBox(rebuiltWorldBounds) ||
                    !EqualSvsmBox(
                        rebuiltWorldBounds,
                        snapshot.worldBounds))
                {
                    return false;
                }

                const int instanceIndex =
                    snapshot.instanceReference->
                        GetInstanceIndex();
                const uint64_t vertexOffset =
                    uint64_t(mesh->vertexOffset) +
                    geometry->vertexOffsetInMesh;
                const uint64_t indexOffset =
                    uint64_t(mesh->indexOffset) +
                    geometry->indexOffsetInMesh;
                if (instanceIndex < 0 ||
                    vertexOffset >
                        std::numeric_limits<uint32_t>::max() ||
                    indexOffset >
                        std::numeric_limits<uint32_t>::max())
                {
                    return false;
                }

                PersistentCasterSourceRecord record;
                record.instance = snapshot.instanceReference;
                record.mesh = mesh;
                record.geometry = geometry;
                record.buffers = buffers;
                record.material = material;
                record.localToWorld = localToWorld;
                record.objectSpaceBounds =
                    snapshot.objectSpaceBounds;
                record.worldBounds = snapshot.worldBounds;
                record.topology = snapshot.topology;
                record.materialSignature = snapshot.material;
                record.cullMode = material->doubleSided
                    ? nvrhi::RasterCullMode::None
                    : nvrhi::RasterCullMode::Back;
                record.arguments.vertexCount =
                    geometry->numIndices;
                record.arguments.instanceCount = 1u;
                record.arguments.startVertexLocation =
                    uint32_t(vertexOffset);
                record.arguments.startIndexLocation =
                    uint32_t(indexOffset);
                record.arguments.startInstanceLocation =
                    uint32_t(instanceIndex);
                record.instanceIndex = uint32_t(instanceIndex);
                record.staticCacheCandidate =
                    snapshot.staticCacheCandidate;
                if (!ValidatePersistentCasterSourceRecordForBuild(
                        record))
                    return false;
                pending.push_back(std::move(record));
            }

            m_PendingPersistentCasterSources.swap(pending);
            m_PendingPersistentCasterSourceKey = key;
            m_PendingPersistentCasterSourceReady = true;
            m_PersistentCasterSourceRebuilt = true;
            return true;
        }

        bool BuildSharedCasterRecordsFromPersistentSources(
            const std::array<frustum, SvsmClipmapCount>&
                clipmapFrusta,
            uint32_t firstClipmap,
            uint32_t activeClipmapMask,
            const affine3& worldToCommonLight)
        {
            const std::vector<PersistentCasterSourceRecord>& sources =
                m_PendingPersistentCasterSourceReady
                ? m_PendingPersistentCasterSources
                : m_PersistentCasterSources;
            for (const PersistentCasterSourceRecord& source : sources)
            {
                if (!ValidateCachedPersistentCasterSourceRecord(source))
                    return false;
                uint32_t clipmapMask = 0u;
                for (uint32_t level = firstClipmap;
                    level < SvsmClipmapCount;
                    ++level)
                {
                    const uint32_t bit = 1u << level;
                    if ((activeClipmapMask & bit) != 0u &&
                        clipmapFrusta[level].intersectsWith(
                            source.worldBounds))
                    {
                        clipmapMask |= bit;
                    }
                }
                if (clipmapMask == 0u)
                    continue;

                SharedCasterRecord record;
                record.draw.instance = source.instance.get();
                record.draw.mesh = source.mesh.get();
                record.draw.geometry = source.geometry.get();
                record.draw.material = source.material.get();
                record.draw.buffers = source.buffers.get();
                record.draw.cullMode = source.cullMode;
                record.draw.distanceToCamera = 0.f;
                record.draw.userData = nullptr;
                record.materialReference = source.material;
                record.buffersReference = source.buffers;
                record.arguments = source.arguments;
                record.argumentsValid = true;
                record.alphaTested =
                    source.materialSignature.domain ==
                        MaterialDomain::AlphaTested;
                record.validatedStableDrawState = true;
                record.clipmapMask = clipmapMask;
                record.reliableCommonLightBounds =
                    TryBuildSvsmCommonLightBounds(
                        source.objectSpaceBounds,
                        source.localToWorld,
                        worldToCommonLight,
                        record.commonLightBounds);
                if (!record.reliableCommonLightBounds)
                    return false;
                record.staticCacheCandidate =
                    source.staticCacheCandidate;
                m_SharedCasterScratch.push_back(
                    std::move(record));
            }
            return true;
        }

        bool BuildSharedCasterRecords(
            const std::shared_ptr<SceneGraphNode>& rootNode,
            const std::array<std::shared_ptr<PlanarView>,
                SvsmClipmapCount>& views,
            uint32_t firstClipmap,
            uint64_t sceneStateHash,
            bool persistentCasterSourceCachingEnabled,
            const std::vector<SvsmCasterSnapshot>*
                sourceSnapshots,
            uint64_t sourceGeneration)
        {
            m_SharedCasterScratch.clear();
            m_SharedCasterMaskStackScratch.clear();
            m_PersistentCasterSourceRequested =
                persistentCasterSourceCachingEnabled;
            m_PersistentCasterSourceActive = false;
            m_PersistentCasterSourceRebuilt = false;
            m_PersistentCasterSourceReused = false;
            m_PendingPersistentCasterSources.clear();
            m_PendingPersistentCasterSourceKey = {};
            m_PendingPersistentCasterSourceReady = false;
            if (!rootNode ||
                firstClipmap >= SvsmClipmapCount ||
                !views[firstClipmap])
            {
                return false;
            }

            std::array<frustum, SvsmClipmapCount> clipmapFrusta;
            uint32_t activeClipmapMask = 0u;
            for (uint32_t level = firstClipmap;
                level < SvsmClipmapCount;
                ++level)
            {
                if (!views[level])
                    return false;
                clipmapFrusta[level] = views[level]->GetViewFrustum();
                activeClipmapMask |= 1u << level;
            }

            // Every directional clipmap has the same linear light basis. Strip
            // the independently snapped translation once, then project the
            // exact caster box into that common basis for all six levels.
            affine3 worldToCommonLight =
                views[firstClipmap]->GetViewMatrix();
            worldToCommonLight.m_translation = float3(0.f);
            const affine3 commonLightToWorld =
                inverse(worldToCommonLight);
            for (uint32_t level = firstClipmap;
                level < SvsmClipmapCount;
                ++level)
            {
                m_SharedLightToClipMatrices[level] =
                    affineToHomogeneous(commonLightToWorld) *
                    views[level]->GetViewProjectionMatrix(false);
            }

            auto clipmapMaskForBounds =
                [&](const box3& bounds, uint32_t candidateMask) {
                    if (!IsFiniteSvsmBox(bounds))
                        return candidateMask;
                    uint32_t result = 0u;
                    for (uint32_t level = firstClipmap;
                        level < SvsmClipmapCount;
                        ++level)
                    {
                        const uint32_t bit = 1u << level;
                        if ((candidateMask & bit) != 0u &&
                            clipmapFrusta[level].intersectsWith(bounds))
                        {
                            result |= bit;
                        }
                    }
                    return result;
                };

            if (ShouldUseSvsmPersistentCasterSource(
                    persistentCasterSourceCachingEnabled,
                    true,
                    sourceSnapshots != nullptr,
                    sourceSnapshots != nullptr &&
                        sourceGeneration != 0u,
                    false) &&
                PreparePersistentCasterSources(
                    rootNode,
                    *sourceSnapshots,
                    sourceGeneration,
                    sceneStateHash))
            {
                if (BuildSharedCasterRecordsFromPersistentSources(
                        clipmapFrusta,
                        firstClipmap,
                        activeClipmapMask,
                        worldToCommonLight))
                {
                    m_PersistentCasterSourceActive = true;
                    return true;
                }
                m_SharedCasterScratch.clear();
            }

            // Donut's aggregate node boxes are formed from prototype mesh
            // bounds. Before using those boxes for hierarchical pruning, mark
            // every ancestor of a skinned or morph caster whose deformed
            // envelope is not authoritative. Only those ancestor paths fail
            // open; unrelated rigid subtrees retain coarse culling.
            const bool reuseUnboundedDeformerAncestors =
                m_SharedUnboundedDeformerAncestorCacheValid &&
                m_SharedUnboundedDeformerAncestorRoot == rootNode &&
                m_SharedUnboundedDeformerAncestorSceneStateHash ==
                    sceneStateHash;
            if (!reuseUnboundedDeformerAncestors)
            {
                std::unordered_set<const SceneGraphNode*>
                    pendingUnboundedDeformerAncestors;
                SceneGraphWalker deformerWalker(rootNode.get());
                while (deformerWalker)
                {
                    const SceneContentFlags relevantContentFlags =
                        SceneContentFlags::OpaqueMeshes |
                        SceneContentFlags::AlphaTestedMeshes;
                    const bool subgraphRelevant =
                        (deformerWalker->
                            GetSubgraphContentFlags() &
                            relevantContentFlags) != 0;
                    const bool leafRelevant =
                        (deformerWalker->GetLeafContentFlags() &
                            relevantContentFlags) != 0;
                    if (leafRelevant)
                    {
                        const auto* instance =
                            dynamic_cast<const MeshInstance*>(
                                deformerWalker->GetLeaf().get());
                        const MeshInfo* mesh = instance
                            ? instance->GetMesh().get()
                            : nullptr;
                        const bool unboundedDeformer =
                            mesh &&
                            !CanUseSvsmStaticPacketBounds(
                                bool(mesh->skinPrototype),
                                mesh->isSkinPrototype,
                                mesh->isMorphTargetAnimationMesh);
                        if (unboundedDeformer)
                        {
                            for (const SceneGraphNode* node =
                                    deformerWalker.Get();
                                node;
                                node = node->GetParent())
                            {
                                pendingUnboundedDeformerAncestors
                                    .insert(node);
                                if (node == rootNode.get())
                                    break;
                            }
                        }
                    }
                    deformerWalker.Next(subgraphRelevant);
                }
                m_SharedUnboundedDeformerAncestorsScratch =
                    std::move(pendingUnboundedDeformerAncestors);
                m_SharedUnboundedDeformerAncestorRoot = rootNode;
                m_SharedUnboundedDeformerAncestorSceneStateHash =
                    sceneStateHash;
                m_SharedUnboundedDeformerAncestorCacheValid = true;
            }

            m_SharedCasterMaskStackScratch.reserve(32u);
            m_SharedCasterMaskStackScratch.push_back(
                activeClipmapMask);
            SceneGraphWalker walker(rootNode.get());
            while (walker)
            {
                const uint32_t parentMask =
                    m_SharedCasterMaskStackScratch.back();
                const SceneContentFlags relevantContentFlags =
                    SceneContentFlags::OpaqueMeshes |
                    SceneContentFlags::AlphaTestedMeshes;
                const bool subgraphRelevant =
                    (walker->GetSubgraphContentFlags() &
                        relevantContentFlags) != 0;
                const bool leafRelevant =
                    (walker->GetLeafContentFlags() &
                        relevantContentFlags) != 0;

                uint32_t nodeMask = 0u;
                if (subgraphRelevant)
                {
                    const bool containsUnboundedDeformer =
                        m_SharedUnboundedDeformerAncestorsScratch
                            .find(walker.Get()) !=
                        m_SharedUnboundedDeformerAncestorsScratch
                            .end();
                    nodeMask = containsUnboundedDeformer
                        ? parentMask
                        : clipmapMaskForBounds(
                            walker->GetGlobalBoundingBox(),
                            parentMask);
                }
                const bool nodeVisible = nodeMask != 0u;
                if (nodeVisible && leafRelevant)
                {
                    auto* instance = dynamic_cast<MeshInstance*>(
                        walker->GetLeaf().get());
                    const MeshInfo* mesh =
                        instance ? instance->GetMesh().get() : nullptr;
                    SceneGraphNode* instanceNode =
                        instance ? instance->GetNode() : nullptr;
                    if (instance &&
                        mesh &&
                        instanceNode &&
                        instance->GetInstanceIndex() >= 0 &&
                        mesh->buffers)
                    {
                        const affine3 localToWorld =
                            instanceNode->
                                GetLocalToWorldTransformFloat();
                        const bool reliableRigidBounds =
                            CanUseSvsmStaticPacketBounds(
                                bool(mesh->skinPrototype),
                                mesh->isSkinPrototype,
                                mesh->isMorphTargetAnimationMesh);
                        for (const auto& geometry : mesh->geometries)
                        {
                            const Material* material = geometry
                                ? geometry->material.get()
                                : nullptr;
                            if (!geometry ||
                                !material ||
                                (material->domain !=
                                        MaterialDomain::Opaque &&
                                    material->domain !=
                                        MaterialDomain::AlphaTested))
                            {
                                continue;
                            }

                            uint32_t geometryMask = nodeMask;
                            if (reliableRigidBounds &&
                                IsFiniteSvsmBox(
                                    geometry->objectSpaceBounds))
                            {
                                const box3 worldBounds =
                                    geometry->objectSpaceBounds *
                                    localToWorld;
                                geometryMask = clipmapMaskForBounds(
                                    worldBounds, nodeMask);
                            }
                            if (geometryMask == 0u)
                                continue;

                            SharedCasterRecord record;
                            record.draw.instance = instance;
                            record.draw.mesh = mesh;
                            record.draw.geometry = geometry.get();
                            record.draw.material = material;
                            record.draw.buffers =
                                mesh->buffers.get();
                            record.draw.cullMode =
                                material->doubleSided
                                ? nvrhi::RasterCullMode::None
                                : nvrhi::RasterCullMode::Back;
                            record.draw.distanceToCamera = 0.f;
                            record.draw.userData = nullptr;
                            const uint64_t vertexOffset =
                                uint64_t(mesh->vertexOffset) +
                                geometry->vertexOffsetInMesh;
                            const uint64_t indexOffset =
                                uint64_t(mesh->indexOffset) +
                                geometry->indexOffsetInMesh;
                            if (vertexOffset <=
                                    std::numeric_limits<
                                        uint32_t>::max() &&
                                indexOffset <=
                                    std::numeric_limits<
                                        uint32_t>::max())
                            {
                                record.arguments.vertexCount =
                                    geometry->numIndices;
                                record.arguments.instanceCount = 1u;
                                record.arguments.
                                    startVertexLocation =
                                        uint32_t(vertexOffset);
                                record.arguments.
                                    startIndexLocation =
                                        uint32_t(indexOffset);
                                record.arguments.
                                    startInstanceLocation =
                                        uint32_t(instance->
                                            GetInstanceIndex());
                                record.argumentsValid = true;
                            }
                            record.clipmapMask = geometryMask;
                            record.reliableCommonLightBounds =
                                reliableRigidBounds &&
                                TryBuildSvsmCommonLightBounds(
                                    record.draw,
                                    localToWorld,
                                    worldToCommonLight,
                                    record.commonLightBounds);
                            record.staticCacheCandidate =
                                IsPacketStaticCacheCandidate(
                                    record.draw);
                            m_SharedCasterScratch.push_back(
                                std::move(record));
                        }
                    }
                }

                const int depthChange = walker.Next(nodeVisible);
                if (depthChange > 0)
                {
                    m_SharedCasterMaskStackScratch.push_back(
                        nodeMask);
                }
                else if (depthChange < 0)
                {
                    const size_t popCount = std::min(
                        size_t(-depthChange),
                        m_SharedCasterMaskStackScratch.size() - 1u);
                    m_SharedCasterMaskStackScratch.resize(
                        m_SharedCasterMaskStackScratch.size() -
                        popCount);
                }
            }
            return true;
        }

        nvrhi::BindingLayoutHandle CreateSparseViewBindingLayout(
            bool includeMaterialSampler)
        {
            nvrhi::BindingLayoutDesc layoutDesc;
            layoutDesc.visibility =
                nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
            layoutDesc.registerSpace = GBUFFER_SPACE_VIEW;
            layoutDesc.registerSpaceIsDescriptorSet = true;
            layoutDesc.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    GBUFFER_BINDING_VIEW_CONSTANTS)
            };
            if (includeMaterialSampler)
            {
                layoutDesc.bindings.push_back(
                    nvrhi::BindingLayoutItem::Sampler(
                        GBUFFER_BINDING_MATERIAL_SAMPLER));
            }
            layoutDesc.bindings.insert(
                layoutDesc.bindings.end(), {
                    nvrhi::BindingLayoutItem::VolatileConstantBuffer(3),
                    nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7),
                    nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8),
                    nvrhi::BindingLayoutItem::StructuredBuffer_SRV(9),
                    nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
                    nvrhi::BindingLayoutItem::Texture_SRV(11),
                    nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),
                    nvrhi::BindingLayoutItem::StructuredBuffer_SRV(13),
                    nvrhi::BindingLayoutItem::Texture_UAV(0)
                });
            return m_Device->createBindingLayout(layoutDesc);
        }

        nvrhi::BindingSetHandle CreateSparseViewBindingSet(
            nvrhi::IBindingLayout* layout,
            bool includeMaterialSampler,
            bool trackLiveness)
        {
            if (!layout ||
                !m_PacketPageMetadata ||
                !m_PacketPageRuntime ||
                !m_PacketRenderPages ||
                !m_PageTable ||
                !m_RenderPages ||
                !m_Counters)
            {
                return nullptr;
            }
            nvrhi::BindingSetDesc setDesc;
            setDesc.trackLiveness = trackLiveness;
            setDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    GBUFFER_BINDING_VIEW_CONSTANTS, m_GBufferCB)
            };
            if (includeMaterialSampler)
            {
                setDesc.bindings.push_back(
                    nvrhi::BindingSetItem::Sampler(
                        GBUFFER_BINDING_MATERIAL_SAMPLER,
                        m_CommonPasses->m_AnisotropicWrapSampler));
            }
            setDesc.bindings.insert(
                setDesc.bindings.end(), {
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
                nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    13,
                    m_ReceiverPageMasks
                        ? m_ReceiverPageMasks
                        : m_Counters),
                nvrhi::BindingSetItem::Texture_UAV(
                    0,
                    m_PhysicalDepth,
                    nvrhi::Format::R32_UINT,
                    nvrhi::TextureSubresourceSet(
                        0,
                        1,
                        0,
                        m_PairedStaticDynamicDepthEnabled ? 2u : 1u),
                    nvrhi::TextureDimension::Texture2DArray)
                });
            return m_Device->createBindingSet(
                setDesc, layout);
        }

    protected:
        nvrhi::ShaderHandle CreateVertexShader(
            ShaderFactory& shaderFactory,
            const CreateParameters&) override
        {
            std::vector<ShaderMacro> macros;
            macros.emplace_back("SVSM_BATCHED_DRAW", "0");
            macros.emplace_back("SVSM_POSITION_ONLY", "0");
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
            // Shader-library permutation keys are emitted in macro-name
            // order. Keep this request in that exact order or an otherwise
            // compiled permutation cannot be found at runtime.
            macros.emplace_back(
                "SVSM_DEFER_STATIC_MERGE",
                m_DeferredStaticDepthMergeEnabled ? "1" : "0");
            macros.emplace_back(
                "SVSM_LEAN_ALPHA_BINDINGS",
                m_LeanAlphaTestedBindingsEnabled ? "1" : "0");
            macros.emplace_back("SVSM_MATERIAL_FREE_OPAQUE", "0");
            return shaderFactory.CreateShader(
                "uvsr/sparse_virtual_shadow_map_sparse_depth.hlsl",
                "pixelMain",
                &macros,
                nvrhi::ShaderType::Pixel);
        }

        std::shared_ptr<MaterialBindingCache>
        CreateMaterialBindingCache(
            CommonRenderPasses& commonPasses) override
        {
            if (!m_LeanAlphaTestedBindingsEnabled)
            {
                return GBufferFillPass::CreateMaterialBindingCache(
                    commonPasses);
            }

            const std::vector<MaterialResourceBinding> bindings = {
                {
                    MaterialResource::ConstantBuffer,
                    GBUFFER_BINDING_MATERIAL_CONSTANTS
                },
                {
                    MaterialResource::DiffuseTexture,
                    GBUFFER_BINDING_MATERIAL_DIFFUSE_TEXTURE
                },
                {
                    MaterialResource::OpacityTexture,
                    GBUFFER_BINDING_MATERIAL_OPACITY_TEXTURE
                }
            };
            return std::make_shared<MaterialBindingCache>(
                m_Device,
                nvrhi::ShaderType::Pixel,
                GBUFFER_SPACE_MATERIAL,
                true,
                bindings,
                commonPasses.m_AnisotropicWrapSampler,
                commonPasses.m_GrayTexture,
                m_TrackMaterialBindingLiveness);
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
            m_TrackViewBindingLiveness = params.trackLiveness;
            layout = CreateSparseViewBindingLayout(true);
            set = CreateSparseViewBindingSet(
                layout, true, params.trackLiveness);
            m_OpaqueViewBindingLayout =
                CreateSparseViewBindingLayout(false);
            m_OpaqueViewBindings = CreateSparseViewBindingSet(
                m_OpaqueViewBindingLayout,
                false,
                params.trackLiveness);
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

        nvrhi::GraphicsPipelineHandle
        CreateOpaqueSpecializedGraphicsPipeline(
            PipelineKey key,
            nvrhi::FramebufferInfo const& framebufferInfo,
            nvrhi::IShader* vertexShader)
        {
            if (key.bits.alphaTested ||
                !vertexShader ||
                !m_MaterialFreeOpaquePixelShader ||
                !m_OpaqueViewBindingLayout ||
                !m_OpaqueViewBindings)
            {
                return nullptr;
            }

            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.inputLayout = nullptr;
            pipelineDesc.VS = vertexShader;
            pipelineDesc.PS = m_MaterialFreeOpaquePixelShader;
            pipelineDesc.renderState.rasterState
                .setFrontCounterClockwise(
                    key.bits.frontCounterClockwise)
                .setCullMode(key.bits.cullMode);
            pipelineDesc.renderState.depthStencilState.disableDepthTest();
            pipelineDesc.renderState.blendState.disableAlphaToCoverage();
            pipelineDesc.bindingLayouts = {
                m_OpaqueViewBindingLayout,
                m_InputBindingLayout
            };
            return m_Device->createGraphicsPipeline(
                pipelineDesc, framebufferInfo);
        }

        void DisableOpaqueRasterSpecialization()
        {
            m_OpaqueRasterSpecializationSupported = false;
            if (!m_OpaqueRasterSpecializationFailureLogged)
            {
                log::warning(
                    "SVSM opaque raster specialization is unavailable; retaining the exact material-bound sparse raster path.");
                m_OpaqueRasterSpecializationFailureLogged = true;
            }
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
            nvrhi::IBuffer* receiverPageMasks,
            uint32_t physicalPageCount,
            bool pairedStaticDynamicDepthEnabled,
            bool deferredStaticDepthMergeEnabled,
            bool leanAlphaTestedBindingsEnabled)
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
            , m_ReceiverPageMasks(receiverPageMasks)
            , m_PhysicalPageCount(physicalPageCount)
            , m_PairedStaticDynamicDepthEnabled(
                pairedStaticDynamicDepthEnabled)
            , m_DeferredStaticDepthMergeEnabled(
                deferredStaticDepthMergeEnabled)
            , m_LeanAlphaTestedBindingsEnabled(
                leanAlphaTestedBindingsEnabled)
        {
            m_BatchedDrawSupported =
                HasExtendedCommandInfoSupport(device);
        }

        void Init(
            ShaderFactory& shaderFactory,
            const CreateParameters& parameters) override
        {
            m_TrackMaterialBindingLiveness =
                parameters.trackLiveness;
            GBufferFillPass::Init(shaderFactory, parameters);

            {
                std::vector<ShaderMacro> macros;
                macros.emplace_back("SVSM_BATCHED_DRAW", "0");
                macros.emplace_back("SVSM_POSITION_ONLY", "1");
                m_PositionOnlyVertexShader =
                    shaderFactory.CreateShader(
                        "uvsr/sparse_virtual_shadow_map_sparse_depth.hlsl",
                        "vertexMain",
                        &macros,
                        nvrhi::ShaderType::Vertex);
            }
            {
                std::vector<ShaderMacro> macros;
                macros.emplace_back("ALPHA_TESTED", "0");
                macros.emplace_back(
                    "SVSM_DEFER_STATIC_MERGE",
                    m_DeferredStaticDepthMergeEnabled ? "1" : "0");
                macros.emplace_back(
                    "SVSM_LEAN_ALPHA_BINDINGS", "0");
                macros.emplace_back(
                    "SVSM_MATERIAL_FREE_OPAQUE", "1");
                m_MaterialFreeOpaquePixelShader =
                    shaderFactory.CreateShader(
                        "uvsr/sparse_virtual_shadow_map_sparse_depth.hlsl",
                        "pixelMain",
                        &macros,
                        nvrhi::ShaderType::Pixel);
            }

            if (m_BatchedDrawSupported)
            {
                std::vector<ShaderMacro> macros;
                macros.emplace_back("SVSM_BATCHED_DRAW", "1");
                macros.emplace_back("SVSM_POSITION_ONLY", "0");
                m_BatchedVertexShader = shaderFactory.CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_depth.hlsl",
                    "vertexMain",
                    &macros,
                    nvrhi::ShaderType::Vertex);
                m_BatchedDrawSupported =
                    bool(m_BatchedVertexShader);

                std::vector<ShaderMacro> positionOnlyMacros;
                positionOnlyMacros.emplace_back(
                    "SVSM_BATCHED_DRAW", "1");
                positionOnlyMacros.emplace_back(
                    "SVSM_POSITION_ONLY", "1");
                m_BatchedPositionOnlyVertexShader =
                    shaderFactory.CreateShader(
                        "uvsr/sparse_virtual_shadow_map_sparse_depth.hlsl",
                        "vertexMain",
                        &positionOnlyMacros,
                        nvrhi::ShaderType::Vertex);
            }

            m_OpaqueRasterSpecializationSupported =
                bool(m_PositionOnlyVertexShader) &&
                bool(m_MaterialFreeOpaquePixelShader) &&
                bool(m_OpaqueViewBindingLayout) &&
                bool(m_OpaqueViewBindings) &&
                (!m_BatchedDrawSupported ||
                    bool(m_BatchedPositionOnlyVertexShader));
            if (!m_OpaqueRasterSpecializationSupported)
                DisableOpaqueRasterSpecialization();
        }

        [[nodiscard]] bool IsReferenceRasterReady() const
        {
            return bool(m_VertexShader) &&
                bool(m_PixelShader) &&
                bool(m_PixelShaderAlphaTested) &&
                bool(m_InputBindingLayout) &&
                bool(m_ViewBindingLayout) &&
                bool(m_ViewBindings) &&
                bool(m_GBufferCB) &&
                bool(m_MaterialBindings) &&
                m_MaterialBindings->GetLayout() != nullptr;
        }

        [[nodiscard]] bool SupportsBatchedDrawSubmission() const
        {
            return m_BatchedDrawSupported;
        }

        void InvalidateRenderPacketCache()
        {
            m_RenderPacketCacheValid = false;
        }

        void InvalidatePersistentCasterSourceCache()
        {
            m_PersistentCasterSources.clear();
            m_PendingPersistentCasterSources.clear();
            m_PersistentCasterSourceKey = {};
            m_PendingPersistentCasterSourceKey = {};
            m_PersistentCasterSourceCacheValid = false;
            m_PendingPersistentCasterSourceReady = false;
            m_PersistentCasterSourceActive = false;
            m_PersistentCasterSourceRebuilt = false;
            m_PersistentCasterSourceReused = false;
            m_AdaptiveStaticCasterClassification.clear();
            m_AdaptiveStaticCasterClassificationActive = false;
            m_AdaptiveStaticCasterClassificationRoot = nullptr;
            m_AdaptiveStaticCasterClassificationGeneration = 0u;
            m_AdaptiveStaticCasterClassificationRecordCount = 0u;
        }

        void SetOpaqueRasterSpecializationEnabled(bool enabled)
        {
            m_OpaqueRasterSpecializationRequested = enabled;
        }

        bool PrepareAdaptiveStaticCasterClassification(
            const std::vector<SvsmCasterSnapshot>& snapshots,
            bool active,
            const void* rootIdentity,
            uint64_t generation)
        {
            if (!active)
            {
                m_AdaptiveStaticCasterClassification.clear();
                m_AdaptiveStaticCasterClassificationActive = false;
                m_AdaptiveStaticCasterClassificationRoot = nullptr;
                m_AdaptiveStaticCasterClassificationGeneration = 0u;
                m_AdaptiveStaticCasterClassificationRecordCount = 0u;
                return true;
            }
            if (snapshots.size() >
                std::numeric_limits<uint32_t>::max())
            {
                return false;
            }
            if (ShouldReuseSvsmAdaptiveCasterClassification(
                    m_AdaptiveStaticCasterClassificationActive,
                    m_AdaptiveStaticCasterClassificationRoot,
                    m_AdaptiveStaticCasterClassificationGeneration,
                    m_AdaptiveStaticCasterClassificationRecordCount,
                    rootIdentity,
                    generation,
                    uint32_t(snapshots.size())))
            {
                return true;
            }

            std::unordered_map<
                SvsmCasterKey,
                bool,
                SvsmCasterKeyHash> pendingClassification;
            pendingClassification.reserve(
                snapshots.size());
            for (const SvsmCasterSnapshot& snapshot : snapshots)
            {
                if (!pendingClassification.emplace(
                        snapshot.key,
                        snapshot.staticCacheCandidate).second)
                {
                    return false;
                }
            }
            m_AdaptiveStaticCasterClassification.swap(
                pendingClassification);
            m_AdaptiveStaticCasterClassificationActive = true;
            m_AdaptiveStaticCasterClassificationRoot = rootIdentity;
            m_AdaptiveStaticCasterClassificationGeneration =
                generation;
            m_AdaptiveStaticCasterClassificationRecordCount =
                uint32_t(snapshots.size());
            return true;
        }

        bool SetupMaterial(
            GeometryPassContext& abstractContext,
            const Material* material,
            nvrhi::RasterCullMode cullMode,
            nvrhi::GraphicsState& state) override
        {
            auto& context =
                static_cast<Context&>(abstractContext);
            const bool opaqueMaterial =
                material &&
                material->domain == MaterialDomain::Opaque;
            if (ShouldUseSvsmOpaqueRasterSpecialization(
                    m_OpaqueRasterSpecializationRequested,
                    m_OpaqueRasterSpecializationSupported,
                    opaqueMaterial))
            {
                PipelineKey key = context.keyTemplate;
                key.bits.cullMode = cullMode;
                key.bits.alphaTested = false;
                nvrhi::GraphicsPipelineHandle& pipeline =
                    m_BatchedPipelineActive
                    ? m_BatchedPositionOnlyPipelines[key.value]
                    : m_PositionOnlyPipelines[key.value];
                if (!pipeline)
                {
                    std::lock_guard<std::mutex> lockGuard(m_Mutex);
                    if (!pipeline)
                    {
                        pipeline =
                            CreateOpaqueSpecializedGraphicsPipeline(
                                key,
                                state.framebuffer->
                                    getFramebufferInfo(),
                                m_BatchedPipelineActive
                                    ? m_BatchedPositionOnlyVertexShader
                                        .Get()
                                    : m_PositionOnlyVertexShader.Get());
                    }
                }
                if (pipeline)
                {
                    assert(
                        pipeline->getFramebufferInfo() ==
                        state.framebuffer->getFramebufferInfo());
                    state.pipeline = pipeline;
                    state.bindings = {
                        m_OpaqueViewBindings,
                        context.inputBindingSet
                    };
                    return true;
                }

                // Shader or pipeline creation can fail independently of the
                // reference path. Disable this optional specialization and
                // retry the same material through the original bindings.
                DisableOpaqueRasterSpecialization();
            }

            if (!m_BatchedPipelineActive)
            {
                return GBufferFillPass::SetupMaterial(
                    abstractContext, material, cullMode, state);
            }

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
                    m_ViewBindingLayout,
                    true,
                    m_TrackViewBindingLiveness);
            if (!bindingSet)
            {
                m_PacketPageMetadata = previousMetadata;
                m_PacketPageRuntime = previousRuntime;
                m_PacketRenderPages = previousRenderPages;
                return false;
            }
            nvrhi::BindingSetHandle opaqueBindingSet =
                CreateSparseViewBindingSet(
                    m_OpaqueViewBindingLayout,
                    false,
                    m_TrackViewBindingLiveness);
            m_ViewBindings = bindingSet;
            if (opaqueBindingSet)
            {
                m_OpaqueViewBindings = opaqueBindingSet;
            }
            else
            {
                m_OpaqueViewBindings = nullptr;
                DisableOpaqueRasterSpecialization();
            }
            return true;
        }

        bool PrepareRenderPackets(
            const std::shared_ptr<SceneGraphNode>& rootNode,
            const std::array<std::shared_ptr<PlanarView>,
                SvsmClipmapCount>& views,
            uint64_t sceneStateHash,
            uint64_t sceneStateRevision,
            bool sceneStateRevisionReliable,
            const DirectionalLight* light,
            IDrawStrategy& drawStrategy,
            uint32_t firstClipmap,
            bool buildPacketPageMetadata,
            bool reserveExactPacketPageLists,
            bool dirtyPageScatterRaster,
            bool sortPacketsByState,
            bool sharedClipmapPacketBuilderEnabled,
            bool persistentCasterSourceCachingEnabled,
            const std::vector<SvsmCasterSnapshot>*
                sourceSnapshots,
            uint64_t sourceGeneration,
            bool pairedStaticDynamicDepthEnabled,
            const std::vector<SvsmCasterSnapshot>*
                classificationSnapshots,
            bool adaptiveClassificationActive,
            uint64_t classificationGeneration,
            bool allowReuse,
            bool& rebuilt)
        {
            rebuilt = false;
            m_PendingPersistentCasterSources.clear();
            m_PendingPersistentCasterSourceKey = {};
            m_PendingPersistentCasterSourceReady = false;
            m_PersistentCasterSourceRequested =
                persistentCasterSourceCachingEnabled;
            m_PersistentCasterSourceActive = false;
            m_PersistentCasterSourceRebuilt = false;
            m_PersistentCasterSourceReused = false;
            reserveExactPacketPageLists =
                buildPacketPageMetadata && reserveExactPacketPageLists;
            if (dirtyPageScatterRaster && !buildPacketPageMetadata)
            {
                return false;
            }
            firstClipmap = std::min(
                firstClipmap, SvsmClipmapCount - 1u);
            const bool sharedBuilderCompatible =
                sharedClipmapPacketBuilderEnabled &&
                typeid(drawStrategy) ==
                    typeid(InstancedOpaqueDrawStrategy);
            const bool pairedDepthClassificationActive =
                pairedStaticDynamicDepthEnabled &&
                buildPacketPageMetadata;
            adaptiveClassificationActive =
                pairedDepthClassificationActive &&
                adaptiveClassificationActive;
            if (adaptiveClassificationActive &&
                classificationSnapshots == nullptr)
            {
                return false;
            }
            if (!adaptiveClassificationActive)
                classificationGeneration = 0u;
            bool exactPacketKeyMatches = false;
            if (allowReuse)
            {
                bool matricesMatch = m_RenderPacketCacheValid;
                if (matricesMatch)
                {
                    for (uint32_t level = firstClipmap;
                        level < SvsmClipmapCount;
                        ++level)
                    {
                        const float4x4 matrix =
                            views[level]->
                                GetViewProjectionMatrix(false);
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
                exactPacketKeyMatches =
                    m_RenderPacketCacheValid &&
                    m_RenderPacketRoot == rootNode &&
                    m_RenderPacketLight == light &&
                    m_RenderPacketDrawStrategy == &drawStrategy &&
                    m_RenderPacketSceneStateHash == sceneStateHash &&
                    m_RenderPacketSceneStateRevisionReliable ==
                        sceneStateRevisionReliable &&
                    (!sceneStateRevisionReliable ||
                        m_RenderPacketSceneStateRevision ==
                            sceneStateRevision) &&
                    m_RenderPacketFirstClipmap == firstClipmap &&
                    m_RenderPacketPageMetadataRequested ==
                        buildPacketPageMetadata &&
                    m_RenderPacketExactPageListsRequested ==
                        reserveExactPacketPageLists &&
                    m_RenderPacketDirtyPageScatterRasterRequested ==
                        dirtyPageScatterRaster &&
                    m_RenderPacketStateSortingRequested ==
                        sortPacketsByState &&
                    m_RenderPacketSharedBuilderActive ==
                        sharedBuilderCompatible &&
                    m_RenderPacketPairedDepthClassificationActive ==
                        pairedDepthClassificationActive &&
                    m_RenderPacketAdaptiveClassificationActive ==
                        adaptiveClassificationActive &&
                    m_RenderPacketClassificationGeneration ==
                        classificationGeneration &&
                    matricesMatch;
            }
            if (ShouldReuseSvsmRenderPackets(
                    allowReuse, exactPacketKeyMatches))
            {
                return true;
            }

            static const std::vector<SvsmCasterSnapshot>
                emptyCasterSnapshots;
            if (!PrepareAdaptiveStaticCasterClassification(
                    classificationSnapshots
                        ? *classificationSnapshots
                        : emptyCasterSnapshots,
                    adaptiveClassificationActive,
                    rootNode.get(),
                    classificationGeneration))
            {
                // Validation is transactional: retain the last valid packet
                // cache and classifier rather than destroying them before the
                // caller can fail open.
                return false;
            }

            for (auto& packets : m_RenderPackets)
                packets.clear();
            for (auto& groups : m_RenderPacketGroups)
                groups.clear();
            m_BatchedRasterStateMask = 0u;
            m_RenderPacketRoot = rootNode;
            m_RenderPacketLight = light;
            m_RenderPacketDrawStrategy = &drawStrategy;
            m_RenderPacketSceneStateHash = sceneStateHash;
            m_RenderPacketSceneStateRevision = sceneStateRevision;
            m_RenderPacketSceneStateRevisionReliable =
                sceneStateRevisionReliable;
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
            m_RenderPacketSharedBuilderActive =
                sharedBuilderCompatible;
            m_RenderPacketPairedDepthClassificationActive =
                pairedDepthClassificationActive;
            m_RenderPacketAdaptiveClassificationActive =
                adaptiveClassificationActive;
            m_RenderPacketClassificationGeneration =
                classificationGeneration;
            m_RenderPacketPageMetadataSupported =
                buildPacketPageMetadata;
            m_RenderPacketPageDispatchSupported =
                buildPacketPageMetadata;
            m_RenderPacketCacheValid = false;

            constexpr uint32_t maximumPacketCount =
                std::numeric_limits<uint32_t>::max() /
                uint32_t(sizeof(
                    nvrhi::DrawIndexedIndirectArguments));
            if (m_RenderPacketSharedBuilderActive &&
                !BuildSharedCasterRecords(
                    rootNode,
                    views,
                    firstClipmap,
                    sceneStateHash,
                    persistentCasterSourceCachingEnabled,
                    sourceSnapshots,
                    sourceGeneration))
            {
                // Any unsupported or unreliable shared-builder setup falls
                // back to the six-view reference traversal for this cache.
                m_RenderPacketSharedBuilderActive = false;
            }
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
                if (!m_RenderPacketSharedBuilderActive)
                {
                    drawStrategy.PrepareForView(
                        rootNode, *views[level]);
                }
                size_t sharedCasterIndex = 0u;
                while (true)
                {
                    const SharedCasterRecord* sharedCaster = nullptr;
                    const DrawItem* item = nullptr;
                    if (m_RenderPacketSharedBuilderActive)
                    {
                        const uint32_t clipmapBit = 1u << level;
                        while (sharedCasterIndex <
                            m_SharedCasterScratch.size())
                        {
                            const SharedCasterRecord& candidate =
                                m_SharedCasterScratch[
                                    sharedCasterIndex++];
                            if ((candidate.clipmapMask &
                                    clipmapBit) != 0u)
                            {
                                sharedCaster = &candidate;
                                item = &candidate.draw;
                                break;
                            }
                        }
                    }
                    else
                    {
                        item = drawStrategy.GetNextItem();
                    }
                    if (!item)
                        break;

                    SvsmPacketDrawItemDisposition disposition =
                        SvsmPacketDrawItemDisposition::Accept;
                    if (!sharedCaster ||
                        !sharedCaster->validatedStableDrawState)
                    {
                        const bool geometryHasMaterial =
                            item->geometry &&
                            bool(item->geometry->material);
                        const bool meshHasBuffers =
                            item->mesh &&
                            bool(item->mesh->buffers);
                        disposition =
                            ClassifySvsmPacketDrawItem(
                                item->material != nullptr,
                                item->instance != nullptr,
                                item->mesh != nullptr,
                                item->geometry != nullptr,
                                item->buffers != nullptr,
                                geometryHasMaterial,
                                meshHasBuffers,
                                geometryHasMaterial &&
                                    item->geometry->material.get() ==
                                        item->material,
                                meshHasBuffers &&
                                    item->mesh->buffers.get() ==
                                        item->buffers,
                                item->instance &&
                                    item->instance->
                                        GetInstanceIndex() >= 0);
                    }
                    if (disposition ==
                        SvsmPacketDrawItemDisposition::Skip)
                    {
                        continue;
                    }
                    if (disposition ==
                        SvsmPacketDrawItemDisposition::Abort)
                    {
                        log::error(
                            "SVSM caster packet contains incomplete or inconsistent draw state.");
                        for (auto& packets : m_RenderPackets)
                            packets.clear();
                        for (auto& groups : m_RenderPacketGroups)
                            groups.clear();
                        m_RenderPacketCount = 0u;
                        m_RenderPacketPageEntryCount = 0u;
                        return false;
                    }

                    const std::shared_ptr<Material> material =
                        sharedCaster &&
                            sharedCaster->
                                validatedStableDrawState
                        ? sharedCaster->materialReference
                        : item->geometry->material;
                    const std::shared_ptr<BufferGroup> buffers =
                        sharedCaster &&
                            sharedCaster->
                                validatedStableDrawState
                        ? sharedCaster->buffersReference
                        : item->mesh->buffers;
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

                    nvrhi::DrawArguments drawArguments;
                    if (sharedCaster &&
                        sharedCaster->argumentsValid)
                    {
                        drawArguments =
                            sharedCaster->arguments;
                    }
                    else
                    {
                        const uint64_t vertexOffset =
                            uint64_t(item->mesh->vertexOffset) +
                            item->geometry->vertexOffsetInMesh;
                        const uint64_t indexOffset =
                            uint64_t(item->mesh->indexOffset) +
                            item->geometry->indexOffsetInMesh;
                        if (vertexOffset >
                                std::numeric_limits<
                                    uint32_t>::max() ||
                            indexOffset >
                                std::numeric_limits<
                                    uint32_t>::max())
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
                        drawArguments.vertexCount =
                            item->geometry->numIndices;
                        drawArguments.instanceCount = 1u;
                        drawArguments.startVertexLocation =
                            uint32_t(vertexOffset);
                        drawArguments.startIndexLocation =
                            uint32_t(indexOffset);
                        drawArguments.startInstanceLocation =
                            uint32_t(
                                item->instance->
                                    GetInstanceIndex());
                    }

                    RenderPacket packet;
                    packet.material = material;
                    packet.buffers = buffers;
                    packet.cullMode = item->cullMode;
                    const bool alphaTested =
                        sharedCaster &&
                            sharedCaster->
                                validatedStableDrawState
                        ? sharedCaster->alphaTested
                        : material->domain ==
                            MaterialDomain::AlphaTested;
                    packet.stateKey = MakeSvsmBatchedDrawStateKey(
                        reinterpret_cast<uintptr_t>(buffers.get()),
                        reinterpret_cast<uintptr_t>(material.get()),
                        uint32_t(item->cullMode),
                        alphaTested);
                    packet.arguments = drawArguments;
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
                            sharedCaster &&
                                sharedCaster->
                                    reliableCommonLightBounds
                            ? BuildPacketPageMetadataFromCommonLightBounds(
                                packet.arguments.
                                    startInstanceLocation,
                                sharedCaster->commonLightBounds,
                                m_SharedLightToClipMatrices[level],
                                pageListOffset,
                                packetPageCapacity)
                            : BuildPacketPageMetadata(
                                *item,
                                m_RenderPacketMatrices[level],
                                pageListOffset,
                                 packetPageCapacity);
                        const bool staticCacheCandidate =
                            sharedCaster
                            ? sharedCaster->staticCacheCandidate
                            : IsPacketStaticCacheCandidate(*item);
                        if (m_RenderPacketPairedDepthClassificationActive &&
                            staticCacheCandidate)
                        {
                            packet.pageMetadata.objectInstanceIndex |=
                                SvsmPacketStaticCasterBit;
                        }
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

            if (m_PendingPersistentCasterSourceReady)
            {
                m_PersistentCasterSources.swap(
                    m_PendingPersistentCasterSources);
                m_PersistentCasterSourceKey =
                    m_PendingPersistentCasterSourceKey;
                m_PersistentCasterSourceCacheValid = true;
                m_PendingPersistentCasterSources.clear();
                m_PendingPersistentCasterSourceKey = {};
                m_PendingPersistentCasterSourceReady = false;
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
                PipelineKey key;
                key.value = keyValue;
                const bool useOpaqueSpecialization =
                    ShouldUseSvsmOpaqueRasterSpecialization(
                        m_OpaqueRasterSpecializationRequested,
                        m_OpaqueRasterSpecializationSupported,
                        !key.bits.alphaTested);
                nvrhi::GraphicsPipelineHandle* pipeline =
                    useOpaqueSpecialization
                    ? std::addressof(
                        m_BatchedPositionOnlyPipelines[keyValue])
                    : std::addressof(m_BatchedPipelines[keyValue]);
                if (!*pipeline)
                {
                    *pipeline = useOpaqueSpecialization
                        ? CreateOpaqueSpecializedGraphicsPipeline(
                            key,
                            framebufferInfo,
                            m_BatchedPositionOnlyVertexShader)
                        : CreateGraphicsPipelineForVertexShader(
                            key,
                            framebufferInfo,
                            m_BatchedVertexShader);
                }
                if (!*pipeline && useOpaqueSpecialization)
                {
                    DisableOpaqueRasterSpecialization();
                    pipeline = std::addressof(
                        m_BatchedPipelines[keyValue]);
                    if (!*pipeline)
                    {
                        *pipeline =
                            CreateGraphicsPipelineForVertexShader(
                                key,
                                framebufferInfo,
                                m_BatchedVertexShader);
                    }
                }
                if (!*pipeline)
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

        [[nodiscard]] bool IsPersistentCasterSourceRequested() const
        {
            return m_PersistentCasterSourceRequested;
        }

        [[nodiscard]] bool IsPersistentCasterSourceActive() const
        {
            return m_PersistentCasterSourceActive;
        }

        [[nodiscard]] bool WasPersistentCasterSourceRebuilt() const
        {
            return m_PersistentCasterSourceRebuilt;
        }

        [[nodiscard]] bool WasPersistentCasterSourceReused() const
        {
            return m_PersistentCasterSourceReused;
        }

        [[nodiscard]] uint32_t GetPersistentCasterSourceCount() const
        {
            return uint32_t(std::min(
                m_PersistentCasterSources.size(),
                size_t(std::numeric_limits<uint32_t>::max())));
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

        [[nodiscard]] bool RenderViewReference(
            nvrhi::ICommandList* commandList,
            const IView* view,
            nvrhi::IFramebuffer* framebuffer,
            IDrawStrategy& drawStrategy,
            Context& context,
            uint32_t selectedClipmap)
        {
            if (!commandList ||
                !view ||
                !framebuffer ||
                selectedClipmap >= SvsmClipmapCount ||
                !m_IndirectDrawArguments ||
                !m_Counters)
            {
                return false;
            }
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
                if (!item->buffers ||
                    !item->geometry ||
                    !item->mesh ||
                    !item->instance ||
                    item->instance->GetInstanceIndex() < 0)
                {
                    return false;
                }

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
                    return false;
                if (!IsSvsmRasterStateComplete(
                        graphicsState, true))
                {
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
                    return false;
                }

                nvrhi::DrawArguments args;
                args.vertexCount = item->geometry->numIndices;
                args.instanceCount = 1u;
                args.startVertexLocation = uint32_t(vertexOffset);
                args.startIndexLocation = uint32_t(indexOffset);
                args.startInstanceLocation = uint32_t(
                    item->instance->GetInstanceIndex());

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
            return true;
        }

        [[nodiscard]] bool RenderPackets(
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
            bool dirtyPageScatterRaster,
            bool validateOnly = false)
        {
            if (!commandList ||
                !view ||
                !framebuffer ||
                selectedClipmap >= SvsmClipmapCount ||
                !m_IndirectDrawArguments ||
                !m_Counters)
            {
                return false;
            }
            m_BatchedPipelineActive = false;
            if (validateOnly)
            {
                // Pipeline selection needs only these two view bits.
                // Prevalidation must not record GBuffer constant-buffer
                // writes outside the SVSM timer or consume volatile versions.
                context.keyTemplate.bits.frontCounterClockwise =
                    view->IsMirrored();
                context.keyTemplate.bits.reverseDepth =
                    view->IsReverseDepth();
            }
            else
            {
                SetupView(context, commandList, view, view);
            }

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
            const uint64_t levelArgumentBegin =
                m_RenderPacketOffsets[selectedClipmap];
            const uint64_t levelArgumentEnd =
                levelArgumentBegin + uint64_t(packets.size());
            if (levelArgumentEnd >
                    uint64_t(m_RenderPacketCount) ||
                (gpuGated &&
                    levelArgumentEnd >
                        uint64_t(indirectDrawCapacity)))
            {
                return false;
            }
            for (size_t packetIndex = 0u;
                packetIndex < packets.size();
                ++packetIndex)
            {
                const RenderPacket& packet = packets[packetIndex];
                if (!packet.material ||
                    !packet.buffers ||
                    uint64_t(packet.argumentIndex) !=
                        levelArgumentBegin +
                            uint64_t(packetIndex))
                {
                    return false;
                }
            }
            if (gpuGated && batched)
            {
                const auto& groups =
                    m_RenderPacketGroups[selectedClipmap];
                size_t expectedFirstPacket = 0u;
                for (const RenderPacketGroup& group : groups)
                {
                    if (group.packetCount == 0u ||
                        group.firstPacket != expectedFirstPacket ||
                        group.firstPacket >= packets.size() ||
                        group.packetCount >
                            packets.size() - group.firstPacket)
                    {
                        return false;
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
                        return false;
                    }
                    for (uint32_t packetOffset = 0u;
                        packetOffset < group.packetCount;
                        ++packetOffset)
                    {
                        const RenderPacket& packet =
                            packets[
                                group.firstPacket + packetOffset];
                        if (!packet.material ||
                            !packet.buffers ||
                            uint64_t(packet.argumentIndex) !=
                                uint64_t(firstPacket.argumentIndex) +
                                    uint64_t(packetOffset))
                        {
                            return false;
                        }
                    }
                    expectedFirstPacket += group.packetCount;
                }
                if (expectedFirstPacket != packets.size())
                    return false;

                for (const RenderPacketGroup& group : groups)
                {
                    const RenderPacket& firstPacket =
                        packets[group.firstPacket];
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
                        return false;
                    if (!IsSvsmRasterStateComplete(
                            graphicsState, true))
                    {
                        return false;
                    }

                    if (!stateValid)
                    {
                        if (!validateOnly)
                        {
                            commandList->setGraphicsState(
                                graphicsState);
                        }
                        stateValid = true;
                    }

                    if (group.batchable)
                    {
                        if (validateOnly)
                            continue;
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
                            if (validateOnly)
                                continue;
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
                return true;
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
                    return false;
                if (!IsSvsmRasterStateComplete(
                        graphicsState, true))
                {
                    return false;
                }

                nvrhi::DrawArguments args = packet.arguments;
                if (!gpuGated && !validateOnly)
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
                    if (!validateOnly)
                    {
                        commandList->setGraphicsState(
                            graphicsState);
                    }
                    stateValid = true;
                }
                if (validateOnly)
                    continue;
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
            return true;
        }
    };

    SparseVirtualShadowMapPass::SparseVirtualShadowMapPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses)
        : m_Device(device)
        , m_ShaderFactory(shaderFactory)
        , m_CommonPasses(commonPasses)
        , m_CasterSnapshotState(
            std::make_unique<CasterSnapshotState>())
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
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
            nvrhi::BindingLayoutItem::Texture_UAV(0),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3),
            nvrhi::BindingLayoutItem::Texture_UAV(4),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(5),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(6),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(7),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(8),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(9),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(10),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(11),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(12),
            nvrhi::BindingLayoutItem::StructuredBuffer_UAV(13)
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
            "fillIndirect",
            "invalidatePages",
            "buildScheduledPageTileMasks",
            "buildStaticDepthHierarchy",
            "scheduleFine"
        };
        for (uint32_t stage = 0u;
            stage < m_SparseShaders.size();
            ++stage)
        {
            std::vector<ShaderMacro> macros;
            if (stage == SparseMark)
            {
                macros.emplace_back(
                    "SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS", "0");
                macros.emplace_back(
                    "SVSM_RECEIVER_PAGE_MASK", "0");
            }
            else if (stage == SparseFillIndirect)
            {
                macros.emplace_back(
                    "SVSM_SCHEDULED_TILE_MASK", "0");
                macros.emplace_back(
                    "SVSM_STATIC_DEPTH_HIERARCHY", "0");
                macros.emplace_back(
                    "SVSM_RECEIVER_PAGE_MASK", "0");
            }
            else if (stage == SparseBuildStaticDepthHierarchy)
            {
                macros.emplace_back(
                    "SVSM_DEFER_STATIC_MERGE", "0");
            }
            m_SparseShaders[stage] = shaderFactory->CreateShader(
                "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                sparseEntries[stage],
                macros.empty() ? nullptr : &macros,
                nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS = m_SparseShaders[stage];
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparsePipelines[stage] =
                device->createComputePipeline(sparsePipelineDesc);
        }
        if (!m_ResolveBindingLayout ||
            !m_ResolveConstants ||
            !m_ResolveShader ||
            !m_ResolvePipeline ||
            !m_SparseBindingLayout ||
            std::any_of(
                m_SparseShaders.begin(),
                m_SparseShaders.end(),
                [](const nvrhi::ShaderHandle& shader) {
                    return !shader;
                }) ||
            std::any_of(
                m_SparsePipelines.begin(),
                m_SparsePipelines.end(),
                [](const nvrhi::ComputePipelineHandle& pipeline) {
                    return !pipeline;
                }))
        {
            // A staged shader set can lag the executable after a new
            // permutation key is introduced. Treat every base compute stage
            // as mandatory and disable SVSM cleanly instead of dispatching a
            // null pipeline or rendering partial shadow contents.
            m_Timings.supported = false;
            log::error(
                "SVSM base compute shaders or pipelines are unavailable; visibility will remain white. Rebuild and restage the SVSM shader set.");
            return;
        }
        {
            std::vector<ShaderMacro> macros = {
                ShaderMacro("SVSM_DEFER_STATIC_MERGE", "1")
            };
            m_SparseDeferredStaticDepthMergeShader =
                shaderFactory->CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                    "buildStaticDepthHierarchy",
                    &macros,
                    nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS =
                m_SparseDeferredStaticDepthMergeShader;
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparseDeferredStaticDepthMergePipeline =
                device->createComputePipeline(sparsePipelineDesc);
        }
        {
            std::vector<ShaderMacro> macros = {
                ShaderMacro("SVSM_SCHEDULED_TILE_MASK", "1"),
                ShaderMacro("SVSM_STATIC_DEPTH_HIERARCHY", "0"),
                ShaderMacro("SVSM_RECEIVER_PAGE_MASK", "0")
            };
            m_SparseScheduledTileMaskFillShader =
                shaderFactory->CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                    "fillIndirect",
                    &macros,
                    nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS =
                m_SparseScheduledTileMaskFillShader;
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparseScheduledTileMaskFillPipeline =
                device->createComputePipeline(sparsePipelineDesc);
        }
        {
            std::vector<ShaderMacro> macros = {
                ShaderMacro("SVSM_SCHEDULED_TILE_MASK", "0"),
                ShaderMacro("SVSM_STATIC_DEPTH_HIERARCHY", "1"),
                ShaderMacro("SVSM_RECEIVER_PAGE_MASK", "0")
            };
            m_SparseStaticDepthHierarchyFillShader =
                shaderFactory->CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                    "fillIndirect",
                    &macros,
                    nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS =
                m_SparseStaticDepthHierarchyFillShader;
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparseStaticDepthHierarchyFillPipeline =
                device->createComputePipeline(sparsePipelineDesc);
        }
        {
            std::vector<ShaderMacro> macros = {
                ShaderMacro("SVSM_SCHEDULED_TILE_MASK", "1"),
                ShaderMacro("SVSM_STATIC_DEPTH_HIERARCHY", "1"),
                ShaderMacro("SVSM_RECEIVER_PAGE_MASK", "0")
            };
            m_SparseScheduledTileMaskStaticDepthHierarchyFillShader =
                shaderFactory->CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                    "fillIndirect",
                    &macros,
                    nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS =
                m_SparseScheduledTileMaskStaticDepthHierarchyFillShader;
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparseScheduledTileMaskStaticDepthHierarchyFillPipeline =
                device->createComputePipeline(sparsePipelineDesc);
        }
        {
            std::vector<ShaderMacro> macros = {
                ShaderMacro(
                    "SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS", "1"),
                ShaderMacro("SVSM_RECEIVER_PAGE_MASK", "0")
            };
            m_SparsePrecomposedMarkShader =
                shaderFactory->CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                    "mark",
                    &macros,
                    nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS = m_SparsePrecomposedMarkShader;
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparsePrecomposedMarkPipeline =
                device->createComputePipeline(sparsePipelineDesc);
        }
        {
            std::vector<ShaderMacro> macros = {
                ShaderMacro(
                    "SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS", "0"),
                ShaderMacro("SVSM_RECEIVER_PAGE_MASK", "1")
            };
            m_SparseReceiverPageMaskMarkShader =
                shaderFactory->CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                    "mark",
                    &macros,
                    nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS =
                m_SparseReceiverPageMaskMarkShader;
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparseReceiverPageMaskMarkPipeline =
                device->createComputePipeline(sparsePipelineDesc);
        }
        {
            std::vector<ShaderMacro> macros = {
                ShaderMacro(
                    "SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS", "1"),
                ShaderMacro("SVSM_RECEIVER_PAGE_MASK", "1")
            };
            m_SparsePrecomposedReceiverPageMaskMarkShader =
                shaderFactory->CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                    "mark",
                    &macros,
                    nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS =
                m_SparsePrecomposedReceiverPageMaskMarkShader;
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparsePrecomposedReceiverPageMaskMarkPipeline =
                device->createComputePipeline(sparsePipelineDesc);
        }
        {
            std::vector<ShaderMacro> macros = {
                ShaderMacro("SVSM_SCHEDULED_TILE_MASK", "0"),
                ShaderMacro("SVSM_STATIC_DEPTH_HIERARCHY", "0"),
                ShaderMacro("SVSM_RECEIVER_PAGE_MASK", "1")
            };
            m_SparseReceiverPageMaskFillShader =
                shaderFactory->CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                    "fillIndirect",
                    &macros,
                    nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS =
                m_SparseReceiverPageMaskFillShader;
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparseReceiverPageMaskFillPipeline =
                device->createComputePipeline(sparsePipelineDesc);
        }
        {
            std::vector<ShaderMacro> macros = {
                ShaderMacro("SVSM_SCHEDULED_TILE_MASK", "1"),
                ShaderMacro("SVSM_STATIC_DEPTH_HIERARCHY", "0"),
                ShaderMacro("SVSM_RECEIVER_PAGE_MASK", "1")
            };
            m_SparseScheduledTileReceiverPageMaskFillShader =
                shaderFactory->CreateShader(
                    "uvsr/sparse_virtual_shadow_map_sparse_cs.hlsl",
                    "fillIndirect",
                    &macros,
                    nvrhi::ShaderType::Compute);
            nvrhi::ComputePipelineDesc sparsePipelineDesc;
            sparsePipelineDesc.CS =
                m_SparseScheduledTileReceiverPageMaskFillShader;
            sparsePipelineDesc.bindingLayouts = {
                m_SparseBindingLayout
            };
            m_SparseScheduledTileReceiverPageMaskFillPipeline =
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
        static constexpr SvsmTapCount c_SparseResolveTapCounts[] = {
            SvsmTapCount::One,
            SvsmTapCount::Four,
            SvsmTapCount::Eight,
            SvsmTapCount::Sixteen
        };
        for (uint32_t poissonOrdering = 0u;
            poissonOrdering <
                c_SparseResolvePoissonOrderingPermutationCount;
            ++poissonOrdering)
        {
            for (uint32_t filterKernel = 0u;
                filterKernel <
                    c_SparseResolveFilterKernelPermutationCount;
                ++filterKernel)
            {
                for (uint32_t receiverTransform = 0u;
                    receiverTransform <
                        c_SparseResolveReceiverTransformPermutationCount;
                    ++receiverTransform)
                {
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
                            const SvsmPoissonOrdering ordering =
                                SvsmPoissonOrdering(poissonOrdering);
                            const SvsmFilterKernel kernel =
                                filterKernel != 0u
                                    ? SvsmFilterKernel::BilinearPcf
                                    : SvsmFilterKernel::NearestPoisson;
                            const SvsmTapCount tapCount =
                                c_SparseResolveTapCounts[tapPermutation];
                            const uint32_t permutation =
                                GetSvsmSparseResolvePermutationIndex(
                                    ordering,
                                    kernel,
                                    receiverTransform != 0u,
                                    translationCache != 0u,
                                    tapCount);
                            std::vector<ShaderMacro> macros;
                            macros.emplace_back(
                                "SVSM_PAGE_TRANSLATION_CACHE",
                                translationCache != 0u ? "1" : "0");
                            macros.emplace_back(
                                "SVSM_BALANCED_POISSON",
                                poissonOrdering != 0u ? "1" : "0");
                            macros.emplace_back(
                                "SVSM_FILTER_TAPS",
                                c_SparseResolveTapMacros[tapPermutation]);
                            macros.emplace_back(
                                "SVSM_PRECOMPOSED_CLIPMAP_TRANSFORMS",
                                receiverTransform != 0u ? "1" : "0");
                            macros.emplace_back(
                                "SVSM_BILINEAR_PCF",
                                filterKernel != 0u ? "1" : "0");
                            const char* shaderPath =
                                translationCache != 0u
                                    ? (poissonOrdering != 0u
                                        ? "uvsr/sparse_virtual_shadow_map_sparse_resolve_cs_translation_cache_balanced.hlsl"
                                        : "uvsr/sparse_virtual_shadow_map_sparse_resolve_cs_translation_cache_legacy.hlsl")
                                    : (poissonOrdering != 0u
                                        ? "uvsr/sparse_virtual_shadow_map_sparse_resolve_cs_reference_balanced.hlsl"
                                        : "uvsr/sparse_virtual_shadow_map_sparse_resolve_cs_reference_legacy.hlsl");
                            m_SparseResolveShaders[permutation] =
                                shaderFactory->CreateShader(
                                    shaderPath,
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
                }
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
            m_Timings.staticDepthHierarchyBytes = 0u;
            m_Timings.receiverPageMaskBytes = 0u;
        }
        const bool ready = bool(m_ResolveBindingSet);
        if (ready)
            m_ResourceBackend = SvsmResourceBackend::Dense;
        return ready;
    }

    bool SparseVirtualShadowMapPass::EnsureSparseResources(
        nvrhi::ITexture* cameraDepth,
        uint32_t physicalPageCount,
        bool pairedStaticDynamicDepthEnabled,
        bool deferredStaticDepthMergeEnabled,
        bool leanAlphaTestedBindingsEnabled)
    {
        // A runtime graphics-pipeline failure is latched to the exact
        // dual-atomic reference permutation. Toggling the feature off is the
        // explicit retry boundary; ordinary resize/resource recreation must
        // not retry a failing optional specialization every frame.
        m_DeferredStaticDepthMergeRasterFallbackLatched =
            GetNextSvsmDeferredStaticDepthRasterFallbackLatched(
                m_DeferredStaticDepthMergeRasterFallbackLatched,
                deferredStaticDepthMergeEnabled,
                false,
                true);

        if (!cameraDepth ||
            !m_Timings.supported ||
            physicalPageCount == 0u ||
            physicalPageCount > SvsmPagesPerClipmap)
        {
            return false;
        }

        const nvrhi::TextureDesc& cameraDesc =
            cameraDepth->getDesc();
        const SvsmSparseAlphaBindingLayout requestedAlphaBindingLayout =
            GetSvsmSparseAlphaBindingLayout(
                leanAlphaTestedBindingsEnabled);
        const bool recreate =
            RequiresSvsmResourceRecreation(
                m_ResourceBackend,
                SvsmResourceBackend::Sparse) ||
            !m_PageTable ||
            !m_SparsePhysicalDepth ||
            !m_DirtyPageRectangles ||
            !m_LocalInvalidationPages ||
            !m_FinePageCandidateMasks ||
            !m_Visibility ||
            m_Visibility->getDesc().width != cameraDesc.width ||
            m_Visibility->getDesc().height != cameraDesc.height ||
            m_AllocatedPhysicalPageCount != physicalPageCount ||
            m_AllocatedPairedStaticDynamicDepth !=
                pairedStaticDynamicDepthEnabled;
        const bool recreateSparseDepthPass =
            recreate ||
            !m_SparseDepthPass ||
            !m_AllocatedSparseAlphaBindingLayoutValid ||
            !m_AllocatedDeferredStaticDepthMergeValid ||
            !m_DeferredStaticDepthMergeRequestValid ||
            RequiresSvsmSparseDepthPassRecreation(
                m_AllocatedSparseAlphaBindingLayout,
                m_DeferredStaticDepthMergeRequest,
                leanAlphaTestedBindingsEnabled,
                deferredStaticDepthMergeEnabled);
        const bool rebindComputeResources =
            recreate ||
            !m_SparseBindingSet ||
            !m_SparseResolveBindingSets[0] ||
            m_BoundCameraDepth != cameraDepth;
        if (!rebindComputeResources && !recreateSparseDepthPass)
            return true;

        if (rebindComputeResources)
        {
            m_SparseBindingSet = nullptr;
            m_SparseResolveBindingSets = {};
            if (m_BoundCameraDepth != cameraDepth)
                m_StaticVisibilityValid.fill(false);
        }
        if (recreateSparseDepthPass)
            m_SparseDepthPass.reset();
        if (recreate)
        {
            InvalidateUiTimings();
            InvalidateDebugCounters();
            m_ResourceBackend = SvsmResourceBackend::None;
            m_SparseDepthPass.reset();
            m_PageTable = nullptr;
            m_SparsePhysicalDepth = nullptr;
            m_AllocatedPairedStaticDynamicDepth = false;
            m_AllocatedDeferredStaticDepthMergeValid = false;
            m_DeferredStaticDepthMergeRequestValid = false;
            m_AllocatedSparseAlphaBindingLayoutValid = false;
            m_PhysicalOwners = nullptr;
            m_RenderPages = nullptr;
            m_CompactRenderPages = nullptr;
            m_DirtyPageRectangles = nullptr;
            m_LocalInvalidationPages = nullptr;
            m_ScheduledPageTileMasks = nullptr;
            m_StaticDepthHierarchy = nullptr;
            m_ReceiverPageMasks = nullptr;
            m_FinePageCandidateMasks = nullptr;
            m_StaticDepthHierarchyBootstrapRequired = true;
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
            m_ReportedScheduledTileMaskFallback = false;
            m_ReportedStaticDepthHierarchyFallback = false;
            m_ReportedReceiverPageMaskFallback = false;
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
            physicalDepthDesc.arraySize =
                GetSvsmPhysicalDepthArraySize(
                    pairedStaticDynamicDepthEnabled);
            physicalDepthDesc.format = nvrhi::Format::R32_UINT;
            physicalDepthDesc.dimension =
                nvrhi::TextureDimension::Texture2DArray;
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
            m_LocalInvalidationPages = createUintBuffer(
                SvsmClipmapCount * SvsmPagesPerClipmap,
                "SVSM Local Invalidation Pages");
            m_ScheduledPageTileMasks = createUintBuffer(
                SvsmClipmapCount *
                    SvsmScheduledTileMaskWordsPerLevel,
                "SVSM Scheduled Page Tile Masks");
            // At 86 uints per physical page this is about 1.34 MiB for the
            // 4096-page reference pool. Keeping the compact optional resource
            // resident avoids any cache or physical-pool recreation when its
            // independent runtime toggle changes.
            m_StaticDepthHierarchy = createUintBuffer(
                physicalPageCount *
                    SvsmStaticDepthHierarchyWordsPerPage,
                "SVSM Static Depth Page Hierarchy");
            // Five uints per virtual page hold a generation plus four 4x4
            // quadrants, representing an 8x8 receiver mask inside every
            // 128x128 virtual page. The fixed six-clipmap allocation is
            // 480 KiB and is optional: any failure keeps the exact path.
            m_ReceiverPageMasks = createUintBuffer(
                SvsmClipmapCount *
                    SvsmPagesPerClipmap *
                    SvsmReceiverPageMaskWordsPerPage,
                "SVSM Receiver Subpage Masks");
            // One bit per virtual page in each fine clipmap. Allocation writes
            // these 2.5 KiB of centered-Morton candidate masks so one global
            // selector can preserve stable level-first winners without
            // rescanning five page-table slices.
            m_FinePageCandidateMasks = createUintBuffer(
                SvsmFinePageCandidateMaskWordCount,
                "SVSM Fine Page Candidate Masks");
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
                uint64_t(c_DebugCounterReadbackCount) *
                    sizeof(uint32_t);
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
                !m_LocalInvalidationPages ||
                !m_FinePageCandidateMasks ||
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
            if (!m_StaticDepthHierarchy)
            {
                log::warning(
                    "SVSM static-depth hierarchy allocation is unavailable; dynamic caster HZB rejection will fail open.");
            }
            if (!m_ReceiverPageMasks)
            {
                log::warning(
                    "SVSM receiver-page mask allocation is unavailable; uncached caster culling will retain the exact packet-page list.");
            }

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
            m_AllocatedPairedStaticDynamicDepth =
                pairedStaticDynamicDepthEnabled;
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
                GetSvsmPhysicalDepthArraySize(
                    pairedStaticDynamicDepthEnabled),
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
                    sizeof(uint32_t) +
                uint64_t(SvsmClipmapCount) *
                    SvsmPagesPerClipmap *
                    sizeof(uint32_t) +
                uint64_t(SvsmFinePageCandidateMaskWordCount) *
                    sizeof(uint32_t) +
                (m_ScheduledPageTileMasks
                    ? uint64_t(SvsmClipmapCount) *
                        SvsmScheduledTileMaskWordsPerLevel *
                        sizeof(uint32_t)
                    : 0u);
            m_Timings.staticDepthHierarchyBytes =
                m_StaticDepthHierarchy
                ? uint64_t(physicalPageCount) *
                    SvsmStaticDepthHierarchyWordsPerPage *
                    sizeof(uint32_t)
                : 0u;
            m_Timings.receiverPageMaskBytes =
                m_ReceiverPageMasks
                ? uint64_t(SvsmClipmapCount) *
                    SvsmPagesPerClipmap *
                    SvsmReceiverPageMaskWordsPerPage *
                    sizeof(uint32_t)
                : 0u;
            m_Timings.packetPageListBytes = sizeof(uint32_t);
        }

        if (recreateSparseDepthPass)
        {
            const bool effectiveDeferredStaticDepthMergeRequest =
                IsSvsmDeferredStaticDepthMergeRequestEffective(
                    deferredStaticDepthMergeEnabled,
                    m_DeferredStaticDepthMergeRasterFallbackLatched);
            const SvsmDeferredStaticDepthPassAttempt deferredAttempt =
                GetSvsmDeferredStaticDepthPassAttempt(
                    effectiveDeferredStaticDepthMergeRequest,
                    pairedStaticDynamicDepthEnabled,
                    bool(m_SparseDeferredStaticDepthMergePipeline));
            bool effectiveDeferredStaticDepthMerge =
                deferredAttempt ==
                SvsmDeferredStaticDepthPassAttempt::
                    DeferredThenReference;
            const auto createDepthPass =
                [&](bool leanBindings, bool deferredStaticMerge) {
                    auto pass = std::make_unique<SparseDepthPass>(
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
                        m_ReceiverPageMasks,
                        physicalPageCount,
                        pairedStaticDynamicDepthEnabled,
                        deferredStaticMerge,
                        leanBindings);
                    GBufferFillPass::CreateParameters depthParameters;
                    depthParameters.enableDepthWrite = false;
                    depthParameters.enableMotionVectors = false;
                    // Packet-page buffer growth can replace this pass's custom
                    // bindings while prior frames are still in flight.
                    depthParameters.trackLiveness = true;
                    pass->Init(*m_ShaderFactory, depthParameters);
                    return pass;
                };

            m_SparseDepthPass = createDepthPass(
                leanAlphaTestedBindingsEnabled,
                effectiveDeferredStaticDepthMerge);
            if ((!m_SparseDepthPass ||
                    !m_SparseDepthPass->IsReferenceRasterReady()) &&
                leanAlphaTestedBindingsEnabled)
            {
                log::warning(
                    "SVSM lean alpha-tested bindings are unavailable; retaining the full GBuffer material-binding reference path.");
                m_SparseDepthPass = createDepthPass(
                    false,
                    effectiveDeferredStaticDepthMerge);
            }
            if (ShouldFallbackSvsmDeferredStaticDepthPass(
                    deferredAttempt,
                    m_SparseDepthPass &&
                        m_SparseDepthPass->
                            IsReferenceRasterReady()))
            {
                effectiveDeferredStaticDepthMerge = false;
                if (!m_ReportedDeferredStaticDepthMergeFallback)
                {
                    log::warning(
                        "SVSM deferred static-depth merge raster is unavailable; retaining the exact dual-atomic static raster reference path.");
                    m_ReportedDeferredStaticDepthMergeFallback =
                        true;
                }
                m_SparseDepthPass = createDepthPass(
                    leanAlphaTestedBindingsEnabled,
                    false);
                if ((!m_SparseDepthPass ||
                        !m_SparseDepthPass->
                            IsReferenceRasterReady()) &&
                    leanAlphaTestedBindingsEnabled)
                {
                    m_SparseDepthPass = createDepthPass(
                        false,
                        false);
                }
            }
            if (!m_SparseDepthPass ||
                !m_SparseDepthPass->IsReferenceRasterReady())
            {
                log::error(
                    "SVSM could not create the sparse depth raster pass.");
                m_SparseDepthPass.reset();
                m_AllocatedSparseAlphaBindingLayoutValid = false;
                m_AllocatedDeferredStaticDepthMergeValid = false;
                m_DeferredStaticDepthMergeRequestValid = false;
                return false;
            }
            if (deferredStaticDepthMergeEnabled &&
                pairedStaticDynamicDepthEnabled &&
                deferredAttempt ==
                    SvsmDeferredStaticDepthPassAttempt::
                        ReferenceOnly &&
                !m_DeferredStaticDepthMergeRasterFallbackLatched &&
                !m_ReportedDeferredStaticDepthMergeFallback)
            {
                log::warning(
                    "SVSM deferred static-depth merge compute pipeline is unavailable; retaining the exact dual-atomic static raster reference path.");
                m_ReportedDeferredStaticDepthMergeFallback = true;
            }
            if (effectiveDeferredStaticDepthMerge ||
                !deferredStaticDepthMergeEnabled)
            {
                m_ReportedDeferredStaticDepthMergeFallback = false;
            }
            // Store the requested identity even when the optional layout fell
            // back. That prevents a failed specialization from retrying every
            // frame; toggling away and back deliberately retries it.
            m_AllocatedSparseAlphaBindingLayout =
                requestedAlphaBindingLayout;
            m_AllocatedSparseAlphaBindingLayoutValid = true;
            m_AllocatedDeferredStaticDepthMerge =
                effectiveDeferredStaticDepthMerge;
            m_AllocatedDeferredStaticDepthMergeValid = true;
            m_DeferredStaticDepthMergeRequest =
                deferredStaticDepthMergeEnabled;
            m_DeferredStaticDepthMergeRequestValid = true;
        }

        if (!rebindComputeResources)
        {
            m_ResourceBackend = SvsmResourceBackend::Sparse;
            return true;
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
            !m_LocalInvalidationPages ||
            !m_FinePageCandidateMasks ||
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
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                2, m_LocalInvalidationPages),
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
                9, m_DirtyPageRectangles),
            // The hierarchy is optional. Binding the counter buffer as a
            // harmless fallback keeps the exact fill permutation available
            // when the tiny mask allocation fails; neither the reference
            // fill nor any other stage reads or writes u10.
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                10,
                m_ScheduledPageTileMasks
                    ? m_ScheduledPageTileMasks.Get()
                    : m_Counters.Get()),
            // The optional allocation is never accessed unless the effective
            // HZB flag is active. A harmless counter-buffer fallback keeps all
            // reference permutations valid on allocation failure.
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                11,
                m_StaticDepthHierarchy
                    ? m_StaticDepthHierarchy.Get()
                    : m_Counters.Get()),
            // Receiver masks are optional and only accessed by their explicit
            // mark/fill permutations when the effective runtime flag is set.
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                12,
                m_ReceiverPageMasks
                    ? m_ReceiverPageMasks.Get()
                    : m_Counters.Get()),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                13, m_FinePageCandidateMasks)
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
                sizeof(uint32_t) +
            uint64_t(SvsmFinePageCandidateMaskWordCount) *
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
        const float cameraAnchorDepth = anchorView.z;
        SvsmProjectedDepthInterval projectedRootDepthInterval;
        bool projectedRootDepthIntervalValid = false;
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
                const float projectedRootCenter =
                    uncenteredWorldToView.transformPoint(
                        sceneBounds.center()).z;
                projectedRootDepthIntervalValid =
                    TryBuildSvsmProjectedAabbDepthInterval(
                        projectedRootCenter,
                        {
                            sceneBounds.m_mins.x,
                            sceneBounds.m_mins.y,
                            sceneBounds.m_mins.z
                        },
                        {
                            sceneBounds.m_maxs.x,
                            sceneBounds.m_maxs.y,
                            sceneBounds.m_maxs.z
                        },
                        {
                            uncenteredWorldToView.m_linear.row2.x,
                            uncenteredWorldToView.m_linear.row2.y,
                            uncenteredWorldToView.m_linear.row2.z
                        },
                        true,
                        cameraAnchorDepth,
                        projectedRootDepthInterval);
                // Preserve the existing requested mapping even when the
                // interval proof fails. An invalid or unrepresentable bound
                // only disables retention; it does not invent a new center.
                anchorView.z = projectedRootCenter;
            }
        }
        if (!all(donut::math::isfinite(anchorView)))
            return false;

        const std::array<float3, 3> currentLightBasis = {
            uncenteredWorldToView.m_linear.row0,
            uncenteredWorldToView.m_linear.row1,
            uncenteredWorldToView.m_linear.row2
        };
        const bool sameLightBasis =
            m_PreviousLightBasisValid &&
            !any(currentLightBasis[0] != m_PreviousLightBasis[0]) &&
            !any(currentLightBasis[1] != m_PreviousLightBasis[1]) &&
            !any(currentLightBasis[2] != m_PreviousLightBasis[2]);
        const SvsmLightDepthOriginDecision depthOriginDecision =
            SelectSvsmLightDepthOrigin(
                settings.lightDepthOriginGuardBandEnabled,
                settings.mode == SvsmMode::SparseCached &&
                    settings.cachingEnabled,
                m_CacheStateValid,
                m_PreviousLightBasisValid,
                m_PreviousProducingLight == &light,
                sameLightBasis,
                settings.maximumLightDepth ==
                    m_PreviousMaximumLightDepth,
                anchorView.z,
                m_PreviousLightDepthOrigin,
                settings.maximumLightDepth,
                settings.lightDepthOriginGuardBandFraction,
                projectedRootDepthIntervalValid,
                projectedRootDepthInterval);
        if (!depthOriginDecision.valid)
            return false;
        anchorView.z = depthOriginDecision.selectedOrigin;
        m_Timings.lightDepthOriginGuardBandRetained =
            depthOriginDecision.retainedCommittedOrigin;

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

    SparseVirtualShadowMapPass::BindingResourceSignature
    SparseVirtualShadowMapPass::ComputeBindingResourceSignature(
        const std::shared_ptr<SceneGraphNode>& rootNode) const
    {
        BindingResourceSignature result;
        if (!rootNode)
            return result;

        uint64_t hash = 1469598103934665603ull;
        auto appendBytes = [&hash](
            const void* data,
            size_t size) {
            const auto* bytes =
                static_cast<const uint8_t*>(data);
            for (size_t index = 0u; index < size; ++index)
            {
                hash ^= uint64_t(bytes[index]);
                hash *= 1099511628211ull;
            }
        };
        auto appendPointer = [&appendBytes](const void* pointer) {
            const uintptr_t identity =
                reinterpret_cast<uintptr_t>(pointer);
            appendBytes(&identity, sizeof(identity));
        };

        appendPointer(rootNode.get());
        SceneGraphWalker walker(rootNode.get());
        while (walker)
        {
            const auto& leaf = walker->GetLeaf();
            const auto* instance =
                dynamic_cast<const MeshInstance*>(leaf.get());
            if (instance)
            {
                appendPointer(instance);
                const int instanceIndex =
                    instance->GetInstanceIndex();
                appendBytes(&instanceIndex, sizeof(instanceIndex));

                const auto& mesh = instance->GetMesh();
                appendPointer(mesh.get());
                if (mesh)
                {
                    appendBytes(
                        &mesh->vertexOffset,
                        sizeof(mesh->vertexOffset));
                    appendBytes(
                        &mesh->indexOffset,
                        sizeof(mesh->indexOffset));
                    appendBytes(
                        &mesh->totalVertices,
                        sizeof(mesh->totalVertices));
                    appendBytes(
                        &mesh->totalIndices,
                        sizeof(mesh->totalIndices));

                    const auto& buffers = mesh->buffers;
                    appendPointer(buffers.get());
                    if (buffers)
                    {
                        appendPointer(buffers->indexBuffer.Get());
                        appendPointer(buffers->vertexBuffer.Get());
                        appendPointer(buffers->instanceBuffer.Get());
                        const nvrhi::BufferRange& positionRange =
                            buffers->getVertexBufferRange(
                                VertexAttribute::Position);
                        const nvrhi::BufferRange& texCoordRange =
                            buffers->getVertexBufferRange(
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
                        const Material* material = geometry
                            ? geometry->material.get()
                            : nullptr;
                        if (!geometry ||
                            geometry->type !=
                                MeshGeometryPrimitiveType::Triangles ||
                            !material ||
                            (material->domain !=
                                    MaterialDomain::Opaque &&
                                material->domain !=
                                    MaterialDomain::AlphaTested))
                        {
                            continue;
                        }

                        ++result.casterCount;
                        appendPointer(geometry.get());
                        appendBytes(
                            &geometry->vertexOffsetInMesh,
                            sizeof(geometry->vertexOffsetInMesh));
                        appendBytes(
                            &geometry->indexOffsetInMesh,
                            sizeof(geometry->indexOffsetInMesh));
                        appendBytes(
                            &geometry->numVertices,
                            sizeof(geometry->numVertices));
                        appendBytes(
                            &geometry->numIndices,
                            sizeof(geometry->numIndices));
                        appendPointer(material);
                        appendBytes(
                            &material->domain,
                            sizeof(material->domain));
                        appendPointer(
                            material->materialConstants.Get());
                        appendPointer(
                            material->baseOrDiffuseTexture.get());
                        appendPointer(
                            material->baseOrDiffuseTexture
                                ? material->baseOrDiffuseTexture->
                                    texture.Get()
                                : nullptr);
                        appendPointer(
                            material->opacityTexture.get());
                        appendPointer(
                            material->opacityTexture
                                ? material->opacityTexture->
                                    texture.Get()
                                : nullptr);
                    }
                }
            }
            walker.Next(true);
        }

        result.hash = hash;
        return result;
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
        const uint32_t firstClipmapLevel =
            GetSvsmFirstClipmapLevel(settings.resolutionBias);
        const float finestExtent = settings.firstClipmapExtent *
            float(1u << firstClipmapLevel);
        const float coarsestExtent = settings.firstClipmapExtent *
            float(1u << (SvsmClipmapCount - 1u));
        const uint32_t filterSamples = uint32_t(settings.tapCount);
        const bool bilinearFilterActive =
            backend == SvsmResourceBackend::Sparse &&
            settings.filterKernel == SvsmFilterKernel::BilinearPcf;
        const uint32_t filterComparisons =
            filterSamples *
            (bilinearFilterActive
                ? 4u
                : 1u);
        m_Timings.comparisonVirtualResolution = SvsmVirtualResolution;
        m_Timings.comparisonClipmapCount = SvsmClipmapCount;
        m_Timings.comparisonFirstClipmapLevel = firstClipmapLevel;
        m_Timings.comparisonFilterSampleCount = filterSamples;
        m_Timings.comparisonFilterComparisonCount =
            filterComparisons;
        m_Timings.comparisonFinestCoverageExtent = finestExtent;
        m_Timings.comparisonCoarsestCoverageExtent = coarsestExtent;
        m_Timings.comparisonFinestWorldTexelSize =
            finestExtent / float(SvsmVirtualResolution);
        m_Timings.comparisonCoarsestWorldTexelSize =
            coarsestExtent / float(SvsmVirtualResolution);
        m_Timings.comparisonMaximumLightDepth =
            settings.maximumLightDepth;
        m_Timings.comparisonFilterRadiusTexels =
            float(GetSvsmFilterRadius(
                settings.tapCount,
                bilinearFilterActive
                    ? settings.filterKernel
                    : SvsmFilterKernel::NearestPoisson));
        m_Timings.comparisonFilterMode = settings.filterMode;
        m_Timings.comparisonAdaptiveFiltering =
            settings.adaptiveFiltering;

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
            m_UiTimingContext.hierarchicalScheduledPageMaskActive ==
                m_Timings.hierarchicalScheduledPageMaskActive &&
            m_UiTimingContext.
                    hierarchicalScheduledPageMaskUnavailable ==
                m_Timings.
                    hierarchicalScheduledPageMaskUnavailable &&
            m_UiTimingContext.receiverPageMaskCullingRequested ==
                m_Timings.receiverPageMaskCullingRequested &&
            m_UiTimingContext.receiverPageMaskCullingActive ==
                m_Timings.receiverPageMaskCullingActive &&
            m_UiTimingContext.receiverPageMaskCullingUnavailable ==
                m_Timings.receiverPageMaskCullingUnavailable &&
            m_UiTimingContext.staticDepthHierarchyCullingRequested ==
                m_Timings.staticDepthHierarchyCullingRequested &&
            m_UiTimingContext.staticDepthHierarchyCullingActive ==
                m_Timings.staticDepthHierarchyCullingActive &&
            m_UiTimingContext.staticDepthHierarchyCullingUnavailable ==
                m_Timings.staticDepthHierarchyCullingUnavailable &&
            m_UiTimingContext.deferredStaticDepthMergeRequested ==
                m_Timings.deferredStaticDepthMergeRequested &&
            m_UiTimingContext.deferredStaticDepthMergeActive ==
                m_Timings.deferredStaticDepthMergeActive &&
            m_UiTimingContext.deferredStaticDepthMergeUnavailable ==
                m_Timings.deferredStaticDepthMergeUnavailable &&
            m_UiTimingContext.dirtyPageScatterRasterActive ==
                m_Timings.dirtyPageScatterRasterActive &&
            m_UiTimingContext.packetPageCullingUnavailable ==
                m_Timings.packetPageCullingUnavailable &&
            m_UiTimingContext.movingLightUncachedActive ==
                m_Timings.movingLightUncachedActive &&
            m_UiTimingContext.movingLightCacheTransitionActive ==
                m_Timings.movingLightCacheTransitionActive &&
            m_UiTimingContext.effectivePairedStaticDynamicDepth ==
                m_Timings.effectivePairedStaticDynamicDepth &&
            m_UiTimingContext.physicalMappingRetentionActive ==
                m_Timings.physicalMappingRetentionActive &&
            m_UiTimingContext.
                    effectiveReceiverDistanceMipClampStart ==
                m_Timings.
                    effectiveReceiverDistanceMipClampStart &&
            m_UiTimingContext.
                    receiverDistanceMipClampMaximumLevel ==
                m_Timings.
                    receiverDistanceMipClampMaximumLevel &&
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
        m_UiTimingContext.hierarchicalScheduledPageMaskActive =
            m_Timings.hierarchicalScheduledPageMaskActive;
        m_UiTimingContext.hierarchicalScheduledPageMaskUnavailable =
            m_Timings.hierarchicalScheduledPageMaskUnavailable;
        m_UiTimingContext.receiverPageMaskCullingRequested =
            m_Timings.receiverPageMaskCullingRequested;
        m_UiTimingContext.receiverPageMaskCullingActive =
            m_Timings.receiverPageMaskCullingActive;
        m_UiTimingContext.receiverPageMaskCullingUnavailable =
            m_Timings.receiverPageMaskCullingUnavailable;
        m_UiTimingContext.staticDepthHierarchyCullingRequested =
            m_Timings.staticDepthHierarchyCullingRequested;
        m_UiTimingContext.staticDepthHierarchyCullingActive =
            m_Timings.staticDepthHierarchyCullingActive;
        m_UiTimingContext.staticDepthHierarchyCullingUnavailable =
            m_Timings.staticDepthHierarchyCullingUnavailable;
        m_UiTimingContext.deferredStaticDepthMergeRequested =
            m_Timings.deferredStaticDepthMergeRequested;
        m_UiTimingContext.deferredStaticDepthMergeActive =
            m_Timings.deferredStaticDepthMergeActive;
        m_UiTimingContext.deferredStaticDepthMergeUnavailable =
            m_Timings.deferredStaticDepthMergeUnavailable;
        m_UiTimingContext.dirtyPageScatterRasterActive =
            m_Timings.dirtyPageScatterRasterActive;
        m_UiTimingContext.packetPageCullingUnavailable =
            m_Timings.packetPageCullingUnavailable;
        m_UiTimingContext.movingLightUncachedActive =
            m_Timings.movingLightUncachedActive;
        m_UiTimingContext.movingLightCacheTransitionActive =
            m_Timings.movingLightCacheTransitionActive;
        m_UiTimingContext.effectivePairedStaticDynamicDepth =
            m_Timings.effectivePairedStaticDynamicDepth;
        m_UiTimingContext.physicalMappingRetentionActive =
            m_Timings.physicalMappingRetentionActive;
        m_UiTimingContext.effectiveReceiverDistanceMipClampStart =
            m_Timings.effectiveReceiverDistanceMipClampStart;
        m_UiTimingContext.receiverDistanceMipClampMaximumLevel =
            m_Timings.receiverDistanceMipClampMaximumLevel;
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

            const bool discardSample =
                m_TimerSlotDiscarded[slot];
            // Keep work history independently of latest-frame UI freshness.
            // A newer KnownZero publication is allowed to supersede this
            // query in the primary UI, but must not erase evidence that the
            // older frame submitted real SVSM GPU work.
            RetainSvsmCompletedUntaggedWorkTiming(
                m_Timings,
                sample,
                m_TimerSourceFrames[slot],
                discardSample);
            const SvsmTimerRetirementAction retirementAction =
                GetSvsmTimerRetirementAction(
                    discardSample,
                    sample.sourceTag != 0u,
                    m_CompletedTimingSamples.size() <
                        c_MaxCompletedTimingSamples);
            if (retirementAction.allowUiPublication &&
                ShouldAcceptSvsmTelemetrySample(
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

            // Retire any readback ownership even for a discarded timing slot.
            // Its generation was invalidated at the failure site, so this can
            // release the slot without publishing partial counters.
            ReadDebugCounters(slot);
            if (retirementAction.retireTaggedSample)
            {
                if (m_TimingAccounting.outstanding > 0u)
                    --m_TimingAccounting.outstanding;
                ++m_TimingAccounting.retired;
                if (retirementAction.enqueueTaggedSample)
                {
                    m_CompletedTimingSamples.push_back(sample);
                }
                else if (retirementAction.dropTaggedSample)
                {
                    ++m_TimingAccounting.dropped;
                }
            }

            m_TimerIssuedStageMasks[slot] = 0u;
            m_TimerSlotDiscarded[slot] = false;
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
        m_Timings.scheduledTileMaskQueries = 0u;
        m_Timings.scheduledTileMaskEarlyRejects = 0u;
        m_Timings.scheduledTileMaskFailOpens = 0u;
        m_Timings.scheduledTileMaskPositiveExactZero = 0u;
        m_Timings.receiverPageMaskQueries = 0u;
        m_Timings.receiverPageMaskCulledPages = 0u;
        m_Timings.receiverPageMaskFailOpens = 0u;
        m_Timings.staticDepthHierarchyQueries = 0u;
        m_Timings.staticDepthHierarchyCulledPages = 0u;
        m_Timings.staticDepthHierarchyFailOpens = 0u;
        m_Timings.staticDepthHierarchyBuiltPages = 0u;
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
            m_Timings.scheduledTileMaskQueries =
                counters[SvsmScheduledTileMaskQueryCounter];
            m_Timings.scheduledTileMaskEarlyRejects =
                counters[
                    SvsmScheduledTileMaskEarlyRejectCounter];
            m_Timings.scheduledTileMaskFailOpens =
                counters[
                    SvsmScheduledTileMaskFailOpenCounter];
            m_Timings.scheduledTileMaskPositiveExactZero =
                counters[
                    SvsmScheduledTileMaskPositiveExactZeroCounter];
            m_Timings.receiverPageMaskQueries =
                counters[SvsmReceiverPageMaskQueryCounter];
            m_Timings.receiverPageMaskCulledPages =
                counters[SvsmReceiverPageMaskCullCounter];
            m_Timings.receiverPageMaskFailOpens =
                counters[SvsmReceiverPageMaskFailOpenCounter];
            m_Timings.staticDepthHierarchyQueries =
                counters[SvsmStaticDepthHierarchyQueryCounter];
            m_Timings.staticDepthHierarchyCulledPages =
                counters[SvsmStaticDepthHierarchyCullCounter];
            m_Timings.staticDepthHierarchyFailOpens =
                counters[SvsmStaticDepthHierarchyFailOpenCounter];
            m_Timings.staticDepthHierarchyBuiltPages =
                counters[SvsmStaticDepthHierarchyBuiltPageCounter];
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
            m_TimerSlotDiscarded[m_CurrentTimerSlot] = false;
            m_TimerUiTimingGenerations[m_CurrentTimerSlot] = 0u;
        }
    }

    void SparseVirtualShadowMapPass::DiscardCurrentTimerFrame()
    {
        if (m_TimerFrameAdmitted &&
            m_CurrentTimerIssuedStageMask != 0u)
        {
            m_TimerSlotDiscarded[m_CurrentTimerSlot] = true;
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
        bool requiresFullSceneInvalidation,
        bool requiresDepthBindingCacheReset,
        InstancedOpaqueDrawStrategy& drawStrategy,
        uint64_t timingSourceTag,
        bool forceTotalOnlyGpuTiming,
        const SvsmObjectInvalidationResolver*
            objectInvalidationResolver)
    {
        m_DepthBindingCacheResetLatched =
            m_DepthBindingCacheResetLatched ||
            requiresDepthBindingCacheReset;
        m_RequiresFullSceneInvalidationLatched =
            GetEffectiveSvsmFullInvalidation(
                m_RequiresFullSceneInvalidationLatched,
                requiresFullSceneInvalidation);
        const bool effectiveFullSceneInvalidation =
            m_RequiresFullSceneInvalidationLatched;
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
        m_Timings.cachedShadowDrawListsRequested =
            settings.renderPacketCachingEnabled;
        m_Timings.cachedShadowDrawListsActive = false;
        m_Timings.cachedShadowDrawListsReused = false;
        m_Timings.cachedShadowDrawListsRebuilt = false;
        m_Timings.cachedShadowDrawListPacketCount = 0u;
        m_Timings.persistentCasterSourceRequested =
            settings.persistentCasterSourceCachingEnabled;
        m_Timings.persistentCasterSourceActive = false;
        m_Timings.persistentCasterSourceReused = false;
        m_Timings.persistentCasterSourceRebuilt = false;
        m_Timings.persistentCasterSourceRecordCount =
            m_SparseDepthPass
            ? m_SparseDepthPass->
                GetPersistentCasterSourceCount()
            : 0u;
        m_Timings.casterOnlySceneRevisionActive =
            settings.sceneStateCachingEnabled &&
            settings.casterOnlySceneRevisionEnabled;
        m_Timings.batchedDrawSupported = false;
        m_Timings.batchedDrawActive = false;
        m_Timings.packetStateSortingActive = false;
        m_Timings.levelEmptyWorkSkipActive = false;
        m_Timings.packetPageCullingActive = false;
        m_Timings.hierarchicalScheduledPageMaskActive = false;
        m_Timings.hierarchicalScheduledPageMaskUnavailable = false;
        m_Timings.receiverPageMaskCullingRequested =
            settings.receiverPageMaskCullingEnabled;
        m_Timings.receiverPageMaskCullingActive = false;
        m_Timings.receiverPageMaskCullingUnavailable = false;
        m_Timings.staticDepthHierarchyCullingRequested =
            settings.staticDepthHierarchyCullingEnabled;
        m_Timings.staticDepthHierarchyCullingActive = false;
        m_Timings.staticDepthHierarchyCullingUnavailable = false;
        m_Timings.deferredStaticDepthMergeRequested =
            settings.deferredStaticDepthMergeEnabled;
        m_Timings.deferredStaticDepthMergeActive = false;
        m_Timings.deferredStaticDepthMergeUnavailable =
            settings.deferredStaticDepthMergeEnabled &&
            settings.mode != SvsmMode::DenseReference &&
            settings.pairedStaticDynamicDepthEnabled &&
            !m_SparseDeferredStaticDepthMergePipeline;
        m_Timings.dirtyPageScatterRasterActive = false;
        m_Timings.packetPageCullingUnavailable = false;
        m_Timings.movingLightUncachedActive = false;
        m_Timings.movingLightCacheTransitionActive = false;
        m_Timings.effectivePairedStaticDynamicDepth = false;
        m_Timings.physicalMappingRetentionActive = false;
        m_Timings.lightDepthOriginGuardBandRequested =
            settings.lightDepthOriginGuardBandEnabled &&
            settings.mode == SvsmMode::SparseCached &&
            settings.cachingEnabled;
        m_Timings.lightDepthOriginGuardBandRetained = false;
        m_Timings.movingLightLodRecoveryFactor = 0.f;
        m_Timings.effectiveResolutionBias = settings.resolutionBias;
        m_Timings.receiverDistanceMipClampActive = false;
        m_Timings.movingLightContinuousReceiverBiasActive = false;
        m_Timings.effectiveReceiverDistanceMipClampStart = 0.f;
        m_Timings.receiverDistanceMipClampMaximumLevel = 0u;
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
            !effectiveFullSceneInvalidation &&
            sceneStateRevisionReliable &&
            m_CachedSceneStateRoot == rootNode.get() &&
            m_CachedSceneStateRevision == sceneStateRevision;
        const uint64_t sceneStateHash = reuseSceneStateHash
            ? m_CachedSceneStateHash
            : ComputeSceneStateHash(rootNode);
        const bool reuseBindingResourceSignature =
            settings.sceneStateCachingEnabled &&
            !effectiveFullSceneInvalidation &&
            !requiresDepthBindingCacheReset &&
            sceneStateRevisionReliable &&
            m_CachedBindingResourceRoot == rootNode.get() &&
            m_CachedBindingResourceRevision ==
                sceneStateRevision;
        const BindingResourceSignature bindingResourceSignature =
            reuseBindingResourceSignature
            ? m_CachedBindingResourceSignature
            : ComputeBindingResourceSignature(rootNode);
        m_CachedSceneStateRoot = rootNode.get();
        m_CachedSceneStateRevision = sceneStateRevision;
        m_CachedSceneStateHash = sceneStateHash;
        m_CachedBindingResourceRoot = rootNode.get();
        m_CachedBindingResourceRevision = sceneStateRevision;
        m_CachedBindingResourceSignature =
            bindingResourceSignature;
        m_DepthBindingCacheResetLatched =
            GetEffectiveSvsmDepthBindingCacheReset(
                m_DepthBindingCacheResetLatched,
                requiresDepthBindingCacheReset,
                m_CommittedBindingResourceSignatureValid,
                m_CommittedBindingResourceSignature.hash,
                m_CommittedBindingResourceSignature.casterCount,
                bindingResourceSignature.hash,
                bindingResourceSignature.casterCount);
        if (m_DepthBindingCacheResetLatched)
        {
            if (m_DenseDepthPass)
                m_DenseDepthPass->ResetBindingCache();
            if (m_SparseDepthPass)
            {
                m_SparseDepthPass->ResetBindingCache();
                m_SparseDepthPass->InvalidateRenderPacketCache();
                m_SparseDepthPass->
                    InvalidatePersistentCasterSourceCache();
            }
        }
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
                sceneStateHash,
                sceneStateRevision,
                sceneStateRevisionReliable,
                effectiveFullSceneInvalidation,
                objectInvalidationResolver);
        if (result.visibility)
        {
            m_CommittedBindingResourceSignature =
                bindingResourceSignature;
            m_CommittedBindingResourceSignatureValid = true;
            m_DepthBindingCacheResetLatched = false;
        }
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
        RecordSvsmGpuWorkSubmission(m_Timings);
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

        bool rasterSubmissionSucceeded = true;
        for (uint32_t level = firstLevel;
            level < SvsmClipmapCount;
            ++level)
        {
            DenseDepthPass::Context context;
            m_DenseDepthPass->SelectSlice(level);
            drawStrategy.PrepareForView(
                rootNode, *m_ClipmapViews[level]);
            if (!m_DenseDepthPass->RenderViewReference(
                    commandList,
                    m_ClipmapViews[level].get(),
                    m_RasterFramebuffer,
                    drawStrategy,
                    context))
            {
                rasterSubmissionSucceeded = false;
                break;
            }
        }
        EndTimer(commandList, TimerPageRendering);
        if (!rasterSubmissionSucceeded)
        {
            const SvsmRasterSubmissionTransactionAction action =
                GetSvsmRasterSubmissionTransactionAction(false, false);
            EndTimer(commandList, TimerTotal);
            commandList->endMarker();
            // Seal the timer slot before changing its generation so the
            // partial sample can never be accepted as a later valid frame.
            DiscardCurrentTimerFrame();
            EndTimerFrame();
            if (action.invalidateVisibilityCaches)
            {
                m_StaticVisibilityValid.fill(false);
                m_StaticVisibilitySettingsValid = false;
            }
            if (action.resetDepthBindings)
                m_DepthBindingCacheResetLatched = true;
            InvalidateUiTimings();
            InvalidateDebugCounters();
            m_Timings.active = false;
            if (!m_ReportedRasterSubmissionFailure)
            {
                log::error(
                    "SVSM dense raster submission failed; discarding partial depth and returning white visibility.");
                m_ReportedRasterSubmissionFailure = true;
            }
            return {};
        }
        m_ReportedRasterSubmissionFailure = false;

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
        m_Timings.receiverPageMaskQueries = 0u;
        m_Timings.receiverPageMaskCulledPages = 0u;
        m_Timings.receiverPageMaskFailOpens = 0u;
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
        uint64_t sceneStateHash,
        uint64_t sceneStateRevision,
        bool sceneStateRevisionReliable,
        bool requiresFullSceneInvalidation,
        const SvsmObjectInvalidationResolver*
            objectInvalidationResolver)
    {
        const nvrhi::TextureDesc& cameraDepthDesc =
            cameraDepth->getDesc();
        const bool cacheEnabled =
            settings.mode == SvsmMode::SparseCached &&
            settings.cachingEnabled;
        const bool configuredPairedStaticDynamicDepthEnabled =
            cacheEnabled &&
            settings.pairedStaticDynamicDepthEnabled;
        if (!IsDenseInputValid(cameraView, cameraDepthDesc) ||
            !EnsureSparseResources(
                cameraDepth,
                settings.physicalPageCount,
                configuredPairedStaticDynamicDepthEnabled,
                configuredPairedStaticDynamicDepthEnabled &&
                    settings.deferredStaticDepthMergeEnabled,
                settings.leanAlphaTestedBindingsEnabled))
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
        const bool committedLightKeyChanged =
            light != m_PreviousProducingLight ||
            lightBasisChanged;
        const SvsmMovingLightFramePolicy movingLightPolicy =
            GetSvsmMovingLightFramePolicy(
                settings.movingLightUncachedEnabled,
                cacheEnabled,
                settings.pairedStaticDynamicDepthEnabled,
                committedLightKeyChanged,
                m_PreviousSparseLightUncached,
                m_MovingLightLodRecoveryFramesRemaining,
                settings.movingLightLodRecoveryFrames);
        const bool effectiveCacheEnabled =
            movingLightPolicy.effectiveCacheEnabled;
        const bool effectivePairedStaticDynamicDepthEnabled =
            movingLightPolicy.effectivePairedDepthEnabled;
        const bool continuousReceiverBiasActive =
            settings.receiverDistanceMipClampEnabled &&
            settings.movingLightContinuousReceiverBiasEnabled;
        const SvsmResolutionBias effectiveResolutionBias =
            GetEffectiveSvsmReceiverAwareMovingLightResolutionBias(
                settings.resolutionBias,
                settings.movingLightLodBiasEnabled,
                settings.movingLightResolutionBias,
                movingLightPolicy.lodRecoveryFactor,
                settings.receiverDistanceMipClampEnabled,
                settings.movingLightContinuousReceiverBiasEnabled);
        const float effectiveReceiverDistanceMipClampStart =
            GetEffectiveSvsmReceiverDistanceMipClampStart(
                settings.receiverDistanceMipClampEnabled,
                settings.firstClipmapExtent,
                settings.receiverDistanceMipClampStartScale,
                continuousReceiverBiasActive,
                settings.movingLightLodBiasEnabled,
                settings.movingLightResolutionBias,
                movingLightPolicy.lodRecoveryFactor);
        const uint32_t receiverDistanceMipClampMaximumLevel =
            effectiveReceiverDistanceMipClampStart > 0.f
            ? std::min(
                settings.receiverDistanceMipClampMaximumLevel,
                SvsmMaximumReceiverDistanceMipClampLevel)
            : 0u;
        const bool staticPageRequestConfiguration =
            CanUseSvsmStaticPageRequestConfiguration(
                settings.staticPageRequestReuseEnabled,
                effectiveCacheEnabled,
                settings.physicalPageCount,
                settings.pageRenderBudget,
                staticReuseJitterSupported,
                settings.finiteBudgetStaticDrainEnabled);
        m_Timings.movingLightUncachedActive =
            movingLightPolicy.uncached;
        m_Timings.movingLightCacheTransitionActive =
            movingLightPolicy.transitioningToCached;
        m_Timings.effectivePairedStaticDynamicDepth =
            effectivePairedStaticDynamicDepthEnabled;
        const bool deferredStaticDepthMergeRasterPermutationActive =
            m_AllocatedDeferredStaticDepthMergeValid &&
            m_AllocatedDeferredStaticDepthMerge;
        const bool deferredStaticDepthMergeImplementationAvailable =
            bool(m_SparseDeferredStaticDepthMergePipeline) &&
            deferredStaticDepthMergeRasterPermutationActive;
        const bool deferredStaticDepthMergeActive =
            IsSvsmDeferredStaticDepthMergeActive(
                settings.deferredStaticDepthMergeEnabled,
                effectivePairedStaticDynamicDepthEnabled,
                true,
                deferredStaticDepthMergeImplementationAvailable);
        m_Timings.deferredStaticDepthMergeActive =
            deferredStaticDepthMergeActive;
        m_Timings.deferredStaticDepthMergeUnavailable =
            settings.deferredStaticDepthMergeEnabled &&
            settings.pairedStaticDynamicDepthEnabled &&
            !deferredStaticDepthMergeImplementationAvailable;
        m_Timings.movingLightLodRecoveryFactor =
            movingLightPolicy.lodRecoveryFactor;
        m_Timings.effectiveResolutionBias =
            effectiveResolutionBias;
        m_Timings.receiverDistanceMipClampActive =
            receiverDistanceMipClampMaximumLevel > 0u;
        m_Timings.movingLightContinuousReceiverBiasActive =
            continuousReceiverBiasActive &&
            settings.movingLightLodBiasEnabled &&
            uint32_t(settings.movingLightResolutionBias) > 0u;
        m_Timings.effectiveReceiverDistanceMipClampStart =
            effectiveReceiverDistanceMipClampStart;
        m_Timings.receiverDistanceMipClampMaximumLevel =
            receiverDistanceMipClampMaximumLevel;
        const bool sceneRevisionChanged =
            m_PreviousSceneStateRevisionReliable !=
                sceneStateRevisionReliable ||
            (sceneStateRevisionReliable &&
                m_PreviousSceneStateRevision != sceneStateRevision);
        const bool sceneStateChanged =
            !m_CacheStateValid ||
            sceneStateHash != m_PreviousSceneStateHash ||
            sceneRevisionChanged;
        // The caller's revision includes light-node transforms. The caster
        // hash is exhaustive for the stable metadata in these snapshots, so a
        // light-only revision must not force a second caster traversal.
        const bool casterStateHashChanged =
            !m_CacheStateValid ||
            sceneStateHash != m_PreviousSceneStateHash ||
            m_PreviousSceneStateRevisionReliable !=
                sceneStateRevisionReliable;
        const bool casterCacheConfigurationChanged =
            m_CacheStateValid &&
            (settings.localizedInvalidationEnabled !=
                    m_PreviousLocalizedInvalidationEnabled ||
                settings.adaptiveCasterCacheClassificationEnabled !=
                    m_PreviousAdaptiveCasterCacheClassificationEnabled);
        const bool mappingChanged =
            !m_CacheStateValid ||
            light != m_PreviousProducingLight ||
            lightBasisChanged ||
            m_CurrentLightDepthOrigin !=
                m_PreviousLightDepthOrigin ||
            settings.firstClipmapExtent !=
                m_PreviousFirstClipmapExtent ||
            settings.maximumLightDepth !=
                m_PreviousMaximumLightDepth ||
            casterCacheConfigurationChanged;

        std::vector<uint32_t> localInvalidationPages;
        CasterSnapshotState& casterSnapshots =
            *m_CasterSnapshotState;
        const SvsmObjectInvalidationPolicyConfiguration
            policyConfiguration =
                MakeSvsmObjectInvalidationPolicyConfiguration(
                    settings.defaultObjectInvalidationMode,
                    objectInvalidationResolver);
        casterSnapshots.pendingReady = false;
        casterSnapshots.pendingReliable = false;
        casterSnapshots.pendingContainsAlwaysMode = false;
        casterSnapshots.pendingPolicyResolutionReliable = true;
        casterSnapshots.pendingObserved.clear();
        casterSnapshots.pendingPublished.clear();
        casterSnapshots.pendingMinimumPromotionDeadline =
            SvsmNoPromotionDeadline;
        casterSnapshots.pendingClassificationGeneration =
            casterSnapshots.classificationGeneration;
        const bool snapshotRootChanged =
            casterSnapshots.valid &&
            casterSnapshots.root.get() != rootNode.get();
        const bool snapshotPolicyConfigurationChanged =
            casterSnapshots.valid &&
            !(casterSnapshots.policyConfiguration ==
                policyConfiguration);
        const bool snapshotClassifierChanged =
            casterSnapshots.valid &&
            casterSnapshots.adaptiveClassificationEnabled !=
                settings.adaptiveCasterCacheClassificationEnabled;
        const bool snapshotGatherRequested =
            cacheEnabled &&
            settings.localizedInvalidationEnabled &&
            (!casterSnapshots.valid ||
                !casterSnapshots.policyResolutionReliable ||
                !policyConfiguration.valid ||
                snapshotRootChanged ||
                snapshotPolicyConfigurationChanged ||
                snapshotClassifierChanged ||
                casterStateHashChanged ||
                casterSnapshots.containsAlwaysMode ||
                (!policyConfiguration.resolverEnabled &&
                    policyConfiguration.defaultMode ==
                        SvsmObjectInvalidationMode::Always));
        bool localizedInvalidationFailedOpen = false;
        bool adaptiveClassificationTransition = false;
        if (snapshotGatherRequested)
        {
            casterSnapshots.pendingReliable =
                GatherSvsmCasterSnapshots(
                    rootNode,
                    policyConfiguration,
                    casterSnapshots.pendingObserved,
                    casterSnapshots.pendingContainsAlwaysMode,
                    casterSnapshots.
                        pendingPolicyResolutionReliable);
            casterSnapshots.pendingRoot = rootNode;
            casterSnapshots.pendingPolicyConfiguration =
                policyConfiguration;
            casterSnapshots.pendingAdaptiveClassificationEnabled =
                settings.adaptiveCasterCacheClassificationEnabled;
            casterSnapshots.pendingClassificationGeneration =
                GetNextSvsmPacketClassificationGeneration(
                    casterSnapshots.classificationGeneration);
            casterSnapshots.pendingReady = true;
            if (casterSnapshots.pendingReliable)
            {
                static const std::vector<SvsmCasterSnapshot>
                    emptyCasterSnapshots;
                casterSnapshots.pendingReliable =
                    ReconcileSvsmAdaptiveCasterClassification(
                        casterSnapshots.valid
                            ? casterSnapshots.observed
                            : emptyCasterSnapshots,
                        casterSnapshots.valid
                            ? casterSnapshots.published
                            : emptyCasterSnapshots,
                        casterSnapshots.pendingObserved,
                        settings.
                            adaptiveCasterCacheClassificationEnabled,
                        casterSnapshots.
                            successfulSparseStateCommits,
                        adaptiveClassificationTransition);
            }
            if (casterSnapshots.pendingReliable)
            {
                casterSnapshots.pendingMinimumPromotionDeadline =
                    GetSvsmMinimumPromotionDeadline(
                        casterSnapshots.pendingObserved);
            }
        }
        else if (cacheEnabled &&
            settings.localizedInvalidationEnabled &&
            settings.pairedStaticDynamicDepthEnabled &&
            settings.adaptiveCasterCacheClassificationEnabled &&
            casterSnapshots.valid &&
            casterSnapshots.reliable &&
            IsSvsmDynamicCasterPromotionDue(
                casterSnapshots.successfulSparseStateCommits,
                casterSnapshots.minimumPromotionDeadline))
        {
            // Normal waiting frames are O(1). Only copy/reconcile the caster
            // table when the absolute minimum deadline is actually due.
            casterSnapshots.pendingObserved =
                casterSnapshots.observed;
            casterSnapshots.pendingReliable =
                PromoteDueSvsmDynamicCasters(
                    casterSnapshots.pendingObserved,
                    casterSnapshots.successfulSparseStateCommits,
                    adaptiveClassificationTransition);
            casterSnapshots.pendingMinimumPromotionDeadline =
                GetSvsmMinimumPromotionDeadline(
                    casterSnapshots.pendingObserved);
            if (adaptiveClassificationTransition)
            {
                casterSnapshots.pendingRoot = rootNode;
                casterSnapshots.pendingPolicyConfiguration =
                    casterSnapshots.policyConfiguration;
                casterSnapshots.pendingContainsAlwaysMode =
                    casterSnapshots.containsAlwaysMode;
                casterSnapshots.pendingPolicyResolutionReliable =
                    casterSnapshots.policyResolutionReliable;
                casterSnapshots.
                    pendingAdaptiveClassificationEnabled = true;
                casterSnapshots.pendingClassificationGeneration =
                    GetNextSvsmPacketClassificationGeneration(
                        casterSnapshots.classificationGeneration);
                casterSnapshots.pendingReady = true;
            }
            else
            {
                // A stale/saturated minimum cannot justify GPU work. Repair
                // the O(1) deadline state after the already-due O(N) scan.
                casterSnapshots.minimumPromotionDeadline =
                    casterSnapshots.pendingMinimumPromotionDeadline;
                casterSnapshots.pendingObserved.clear();
            }
        }

        SvsmLocalizedInvalidationReconciliation
            localizedReconciliation;
        if (casterSnapshots.pendingReady)
        {
            const bool localDiffRequested =
                casterStateHashChanged ||
                snapshotPolicyConfigurationChanged ||
                !casterSnapshots.policyResolutionReliable ||
                !casterSnapshots.
                    pendingPolicyResolutionReliable ||
                adaptiveClassificationTransition ||
                casterSnapshots.pendingContainsAlwaysMode;
            const bool canLocalize =
                localDiffRequested &&
                CanUseSvsmLocalizedInvalidation(
                    effectiveCacheEnabled,
                    settings.localizedInvalidationEnabled,
                    m_CacheStateValid,
                    casterSnapshots.valid &&
                        casterSnapshots.reliable,
                    !snapshotRootChanged,
                    !snapshotClassifierChanged,
                    casterSnapshots.pendingReliable,
                    sceneStateRevisionReliable,
                    m_PreviousSceneStateRevisionReliable,
                    requiresFullSceneInvalidation,
                    mappingChanged);
            if (canLocalize)
            {
                localizedInvalidationFailedOpen =
                    !BuildSvsmLocalizedInvalidationPages(
                        casterSnapshots.observed,
                        casterSnapshots.published,
                        casterSnapshots.pendingObserved,
                        m_ClipmapViews,
                        settings.
                            tightLocalizedInvalidationBoundsEnabled,
                        casterStateHashChanged,
                        !requiresFullSceneInvalidation,
                        localInvalidationPages,
                        casterSnapshots.pendingPublished,
                        localizedReconciliation);
            }
            else if (localDiffRequested)
            {
                localizedInvalidationFailedOpen = true;
            }
        }
        else if (casterStateHashChanged)
        {
            localizedInvalidationFailedOpen = true;
        }

        bool fullInvalidation =
            m_SparseResourcesNeedClear ||
            !effectiveCacheEnabled ||
            mappingChanged ||
            movingLightPolicy.forceContentInvalidation ||
            requiresFullSceneInvalidation ||
            (cacheEnabled &&
                settings.localizedInvalidationEnabled &&
                !policyConfiguration.valid) ||
            localizedInvalidationFailedOpen;
        if (!fullInvalidation &&
            casterSnapshots.pendingReady &&
            casterSnapshots.pendingPublished.size() !=
                casterSnapshots.pendingObserved.size())
        {
            // A partial publication must retain exactly one published state
            // for every observed caster. Any bookkeeping mismatch fails open
            // before page work is submitted.
            fullInvalidation = true;
            localizedInvalidationFailedOpen = true;
        }
        if (fullInvalidation)
        {
            localInvalidationPages.clear();
            if (casterSnapshots.pendingReady)
            {
                // A full refresh renders the current observed scene, so it
                // also resolves every previously suppressed publication debt.
                for (SvsmCasterSnapshot& snapshot :
                    casterSnapshots.pendingObserved)
                {
                    snapshot.suppressedCoverageDebt = false;
                }
                casterSnapshots.pendingPublished =
                    casterSnapshots.pendingObserved;
            }
        }
        const bool preservePhysicalMappings =
            fullInvalidation &&
            settings.
                retainPhysicalMappingsOnContentInvalidationEnabled &&
            !m_SparseResourcesNeedClear;
        m_Timings.physicalMappingRetentionActive =
            preservePhysicalMappings;
        const bool invalidateCasterSnapshotsOnSuccess =
            ShouldInvalidateSvsmCasterSnapshotsOnSuccess(
                cacheEnabled,
                settings.localizedInvalidationEnabled);
        const bool publishObservedOnFullInvalidation =
            fullInvalidation && !casterSnapshots.pendingReady;
        auto commitCasterSnapshots = [
            &casterSnapshots,
            invalidateCasterSnapshotsOnSuccess,
            publishObservedOnFullInvalidation]() {
            if (invalidateCasterSnapshotsOnSuccess)
            {
                casterSnapshots.observed.clear();
                casterSnapshots.published.clear();
                casterSnapshots.pendingObserved.clear();
                casterSnapshots.pendingPublished.clear();
                casterSnapshots.root.reset();
                casterSnapshots.pendingRoot.reset();
                casterSnapshots.valid = false;
                casterSnapshots.reliable = false;
                casterSnapshots.containsAlwaysMode = false;
                casterSnapshots.policyResolutionReliable = true;
                casterSnapshots.classificationGeneration = 0u;
                casterSnapshots.pendingClassificationGeneration = 0u;
                casterSnapshots.minimumPromotionDeadline =
                    SvsmNoPromotionDeadline;
                casterSnapshots.pendingReady = false;
                casterSnapshots.pendingContainsAlwaysMode = false;
                casterSnapshots.pendingPolicyResolutionReliable =
                    true;
                return;
            }
            if (!casterSnapshots.pendingReady)
            {
                if (publishObservedOnFullInvalidation &&
                    casterSnapshots.valid)
                {
                    for (SvsmCasterSnapshot& snapshot :
                        casterSnapshots.observed)
                    {
                        snapshot.suppressedCoverageDebt = false;
                    }
                    casterSnapshots.published =
                        casterSnapshots.observed;
                }
                return;
            }
            casterSnapshots.observed =
                std::move(casterSnapshots.pendingObserved);
            casterSnapshots.published =
                std::move(casterSnapshots.pendingPublished);
            casterSnapshots.root =
                casterSnapshots.pendingRoot;
            casterSnapshots.policyConfiguration =
                casterSnapshots.pendingPolicyConfiguration;
            casterSnapshots.containsAlwaysMode =
                casterSnapshots.pendingContainsAlwaysMode;
            casterSnapshots.policyResolutionReliable =
                casterSnapshots.
                    pendingPolicyResolutionReliable;
            casterSnapshots.adaptiveClassificationEnabled =
                casterSnapshots.
                    pendingAdaptiveClassificationEnabled;
            casterSnapshots.classificationGeneration =
                casterSnapshots.pendingClassificationGeneration;
            casterSnapshots.minimumPromotionDeadline =
                casterSnapshots.pendingMinimumPromotionDeadline;
            casterSnapshots.valid =
                true;
            casterSnapshots.reliable =
                casterSnapshots.pendingReliable;
            casterSnapshots.pendingReady = false;
            casterSnapshots.pendingObserved.clear();
            casterSnapshots.pendingPublished.clear();
            casterSnapshots.pendingRoot.reset();
        };
        auto commitSuccessfulSparseState = [
            this,
            &casterSnapshots,
            &commitCasterSnapshots,
            &movingLightPolicy,
            &lightBasis,
            light]() {
            commitCasterSnapshots();
            ++casterSnapshots.successfulSparseStateCommits;
            m_RequiresFullSceneInvalidationLatched = false;
            m_PreviousLightBasis = lightBasis;
            m_PreviousLightBasisValid = true;
            m_PreviousProducingLight = light;
            m_PreviousSparseLightUncached =
                movingLightPolicy.previousUncachedAfterCommit;
            m_MovingLightLodRecoveryFramesRemaining =
                movingLightPolicy.recoveryFramesAfterCommit;
        };
        auto discardFailedSparseRasterTransaction = [
            this,
            deferredStaticDepthMergeRasterPermutationActive]() {
            if (ShouldLatchSvsmDeferredStaticDepthRasterFallback(
                    deferredStaticDepthMergeRasterPermutationActive,
                    false))
            {
                m_DeferredStaticDepthMergeRasterFallbackLatched =
                    GetNextSvsmDeferredStaticDepthRasterFallbackLatched(
                        m_DeferredStaticDepthMergeRasterFallbackLatched,
                        true,
                        deferredStaticDepthMergeRasterPermutationActive,
                        false);
                // Force one reference-pass recreation on the next frame.
                // The raw requested identity remains unchanged so the latch
                // then prevents retries until the user toggles the feature
                // off and back on.
                m_AllocatedDeferredStaticDepthMergeValid = false;
                m_ReportedDeferredStaticDepthMergeFallback = true;
            }
            const SvsmRasterSubmissionTransactionAction action =
                GetSvsmRasterSubmissionTransactionAction(false, true);
            if (action.latchFullRebuild)
            {
                m_RequiresFullSceneInvalidationLatched = true;
                m_CacheStateValid = false;
                m_StaticDepthHierarchyBootstrapRequired = true;
            }
            if (action.clearSparseResources)
            {
                m_SparseResourcesNeedClear = true;
                m_IndirectDrawArgumentsInitialized = false;
                m_PacketPageCullingReady = false;
            }
            if (action.invalidateVisibilityCaches)
            {
                m_StaticPageRequestCacheReady = false;
                m_StaticPageRequestJitterActive = false;
                m_StaticPageDrainFramesRemaining = 0u;
                m_StaticPageRequestCameraDepth = nullptr;
                m_StaticJitterOffsetValid.fill(false);
                m_StaticVisibilityValid.fill(false);
                m_StaticVisibilitySettingsValid = false;
            }
            if (action.resetDepthBindings)
            {
                m_DepthBindingCacheResetLatched = true;
                if (m_SparseDepthPass)
                    m_SparseDepthPass->InvalidateRenderPacketCache();
            }
        };

        SparseVirtualShadowMapSparseConstants constants = {};
        cameraView.FillPlanarViewConstants(constants.cameraView);
        for (uint32_t level = 0u;
            level < SvsmClipmapCount;
            ++level)
        {
            const auto transforms = BuildSvsmClipmapTransformPair(
                settings.precomposedClipmapTransformsEnabled,
                constants.cameraView.matClipToWorld,
                m_ClipmapViews[level]->
                    GetViewProjectionMatrix(false));
            constants.worldToClip[level] =
                transforms.worldToClip;
            constants.receiverToClip[level] =
                transforms.receiverToClip;
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
            uint32_t(effectiveResolutionBias);
        constants.flags =
            (fullInvalidation
                ? SVSM_SPARSE_FLAG_FULL_INVALIDATION
                : 0u) |
            (effectiveCacheEnabled
                ? SVSM_SPARSE_FLAG_CACHING
                : 0u) |
            (preservePhysicalMappings
                ? SVSM_SPARSE_FLAG_PRESERVE_PHYSICAL_MAPPINGS
                : 0u) |
            (settings.gpuGatedDrawSubmission
                ? SVSM_SPARSE_FLAG_COMPACT_PAGE_DISPATCH
                : 0u) |
            (effectiveCacheEnabled &&
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
                : 0u) |
            (settings.filterKernel == SvsmFilterKernel::BilinearPcf
                ? SVSM_SPARSE_FLAG_BILINEAR_PCF
                : 0u) |
            (effectivePairedStaticDynamicDepthEnabled
                ? SVSM_SPARSE_FLAG_PAIRED_STATIC_DYNAMIC_DEPTH
                : 0u) |
            (m_StaticDepthHierarchy
                ? SVSM_SPARSE_FLAG_STATIC_DEPTH_HIERARCHY_RESOURCE
                : 0u);
        constants.selectedClipmap = 0u;
        constants.depthBias = 0.0001f;
        constants.debugView = uint32_t(settings.debugView);
        constants.padding0 = GetSvsmFilterRadius(
            settings.tapCount,
            settings.filterKernel);
        constants.markingMode =
            uint32_t(settings.markingMode);
        constants.filterMode = uint32_t(settings.filterMode);
        constants.adaptiveFiltering =
            settings.adaptiveFiltering ? 1u : 0u;
        constants.drawPacketOffset = 0u;
        constants.drawPacketCount = 0u;
        constants.localInvalidationPageCount =
            uint32_t(localInvalidationPages.size());
        constants.dirtyPageScatterMaximumAmplification = std::clamp(
            settings.dirtyPageScatterMaximumAmplification,
            1u,
            SvsmMaximumDirtyPageScatterAmplification);
        constants.hierarchyGeneration = 0u;
        constants.staticDepthHierarchyBias =
            settings.staticDepthHierarchyBias;
        constants.receiverDistanceMipClampStart =
            effectiveReceiverDistanceMipClampStart;
        constants.receiverDistanceMipClampMaximumLevel =
            receiverDistanceMipClampMaximumLevel;

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
            m_StaticPageRequestFilterKernel ==
                settings.filterKernel &&
            m_StaticPageRequestPoissonOrdering ==
                settings.poissonOrdering &&
            m_StaticPageRequestTapCount ==
                settings.tapCount &&
            m_StaticPageRequestResolutionBias ==
                effectiveResolutionBias &&
            m_StaticPageRequestReceiverDistanceMipClampStart ==
                effectiveReceiverDistanceMipClampStart &&
            m_StaticPageRequestReceiverDistanceMipClampMaximumLevel ==
                receiverDistanceMipClampMaximumLevel &&
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
        const bool localizedPageMaintenancePending =
            !localInvalidationPages.empty();
        const bool staticPageMaintenancePending =
            staticPageRequestStateCompatible &&
            (staticPageRequestBudgetChanged ||
                localizedPageMaintenancePending ||
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
                staticPageMaintenancePending,
                sceneStateChanged);
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
                staticPageRequestBudgetChanged ||
                localizedPageMaintenancePending)
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
            IsSvsmStaticVisibilityConfigurationCompatible(
                m_StaticVisibilitySettingsValid,
                m_StaticVisibilityFilterMode,
                m_StaticVisibilityFilterKernel,
                m_StaticVisibilityPoissonOrdering,
                m_StaticVisibilityTapCount,
                m_StaticVisibilityResolutionBias,
                m_StaticVisibilityPageTranslationCaching,
                m_StaticVisibilityAdaptiveFiltering,
                settings.filterMode,
                settings.filterKernel,
                settings.poissonOrdering,
                settings.tapCount,
                effectiveResolutionBias,
                settings.pageTranslationCachingEnabled,
                settings.adaptiveFiltering) &&
            m_StaticVisibilityReceiverDistanceMipClampStart ==
                effectiveReceiverDistanceMipClampStart &&
            m_StaticVisibilityReceiverDistanceMipClampMaximumLevel ==
                receiverDistanceMipClampMaximumLevel;
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
            effectiveCacheEnabled ? 0u : (1u << 1u);
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
                    effectiveResolutionBias
                ? 0u
                : (1u << 10u);
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestPoissonOrdering ==
                    settings.poissonOrdering
                ? 0u
                : (1u << 25u);
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
        staticPageRequestReuseRejectMask |=
            sceneRevisionChanged ? (1u << 23u) : 0u;
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestFilterKernel == settings.filterKernel
                ? 0u
                : (1u << 24u);
        staticPageRequestReuseRejectMask |=
            m_StaticPageRequestReceiverDistanceMipClampStart ==
                    effectiveReceiverDistanceMipClampStart &&
                m_StaticPageRequestReceiverDistanceMipClampMaximumLevel ==
                    receiverDistanceMipClampMaximumLevel
                ? 0u
                : (1u << 26u);
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

        m_SparseDepthPass->SetOpaqueRasterSpecializationEnabled(
            settings.opaqueRasterSpecializationEnabled);
        const bool useRenderPackets =
            ShouldUseSvsmShadowDrawLists(
                settings.renderPacketCachingEnabled ||
                    effectivePairedStaticDynamicDepthEnabled,
                settings.gpuGatedDrawSubmission);
        const uint32_t firstScheduledClipmap = std::min(
            uint32_t(effectiveResolutionBias),
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
        // Production scatter derives an exact scheduled-page list and bounds
        // for each packet. Its independent amplification guard can therefore
        // choose the one-draw virtual path or that packet's exact per-page
        // fallback without a tiny global render-budget restriction.
        const bool dirtyPageScatterRasterRequested =
            packetPageCullingRequested &&
            settings.dirtyPageScatterRasterEnabled &&
            dirtyPageScatterSafetyBounded;
        const bool exactPacketPageListsRequested =
            packetPageCullingRequested;
        if (!packetPageCullingRequested)
        {
            m_PacketPageCullingUnavailableForPacketCache = false;
        }
        bool usePacketPageCulling =
            packetPageCullingRequested &&
            m_PacketPageCullingReady;
        std::vector<nvrhi::DrawIndexedIndirectArguments>
            indirectArgumentTemplates;
        std::vector<SparseVirtualShadowMapPacketMetadata>
            packetPageMetadata;
        bool packetPageMetadataUploadPending = false;
        float packetPreparationMilliseconds = 0.f;
        if (useRenderPackets)
        {
            const auto packetPreparationStart =
                std::chrono::steady_clock::now();
            const std::vector<SvsmCasterSnapshot>*
                classificationSnapshots = nullptr;
            if (casterSnapshots.pendingReady &&
                casterSnapshots.pendingReliable)
            {
                classificationSnapshots =
                    &casterSnapshots.pendingObserved;
            }
            else if (!casterSnapshots.pendingReady &&
                casterSnapshots.valid &&
                casterSnapshots.reliable)
            {
                classificationSnapshots =
                    &casterSnapshots.observed;
            }
            const bool adaptiveClassificationActive =
                settings.pairedStaticDynamicDepthEnabled &&
                settings.localizedInvalidationEnabled &&
                settings.
                    adaptiveCasterCacheClassificationEnabled &&
                classificationSnapshots != nullptr;
            const uint64_t classificationGeneration =
                !adaptiveClassificationActive
                ? 0u
                : casterSnapshots.pendingReady
                ? casterSnapshots.pendingClassificationGeneration
                : casterSnapshots.classificationGeneration;
            const std::vector<SvsmCasterSnapshot>*
                persistentSourceSnapshots = nullptr;
            uint64_t persistentSourceGeneration = 0u;
            if (!casterSnapshots.pendingReady &&
                casterSnapshots.valid &&
                casterSnapshots.reliable &&
                casterSnapshots.root.get() == rootNode.get())
            {
                persistentSourceSnapshots =
                    &casterSnapshots.observed;
                persistentSourceGeneration =
                    casterSnapshots.classificationGeneration;
            }
            const bool allowPacketReuse =
                CanAttemptSvsmRenderPacketReuse(
                    settings.renderPacketCachingEnabled);
            if (!m_SparseDepthPass->PrepareRenderPackets(
                    rootNode,
                    m_ClipmapViews,
                    sceneStateHash,
                    sceneStateRevision,
                    sceneStateRevisionReliable,
                    light,
                    drawStrategy,
                    firstScheduledClipmap,
                    packetPageCullingRequested,
                    exactPacketPageListsRequested,
                    dirtyPageScatterRasterRequested,
                    packetStateSortingRequested,
                    settings.sharedClipmapPacketBuilderEnabled,
                    settings.
                        persistentCasterSourceCachingEnabled,
                    persistentSourceSnapshots,
                    persistentSourceGeneration,
                    effectivePairedStaticDynamicDepthEnabled,
                    classificationSnapshots,
                    adaptiveClassificationActive,
                    classificationGeneration,
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
            m_Timings.cachedShadowDrawListsRebuilt =
                settings.renderPacketCachingEnabled &&
                renderPacketsRebuilt;
            m_Timings.cachedShadowDrawListsReused =
                settings.renderPacketCachingEnabled &&
                !renderPacketsRebuilt;
            m_Timings.persistentCasterSourceRequested =
                m_SparseDepthPass->
                    IsPersistentCasterSourceRequested();
            m_Timings.persistentCasterSourceActive =
                m_SparseDepthPass->
                    IsPersistentCasterSourceActive();
            m_Timings.persistentCasterSourceReused =
                m_SparseDepthPass->
                    WasPersistentCasterSourceReused();
            m_Timings.persistentCasterSourceRebuilt =
                m_SparseDepthPass->
                    WasPersistentCasterSourceRebuilt();
            m_Timings.persistentCasterSourceRecordCount =
                m_SparseDepthPass->
                    GetPersistentCasterSourceCount();
            if (renderPacketsRebuilt)
            {
                m_PacketPageCullingReady = false;
                m_PacketPageCullingUnavailableForPacketCache = false;
            }
            m_IndirectDrawArgumentsInitialized =
                KeepSvsmIndirectArgumentTemplatesInitialized(
                    m_IndirectDrawArgumentsInitialized,
                    renderPacketsRebuilt);
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
        if (useRenderPackets && renderPacketsRebuilt)
        {
            const auto packetPrevalidationStart =
                std::chrono::steady_clock::now();
            // Packet rebuilds are the only time their material/input state can
            // change without an exact-key miss. Validate those unique states
            // before page clears, so the normal failure case cannot create a
            // partially cleared or partially rendered cache transaction.
            const bool prevalidationDirtyPageScatterRaster =
                dirtyPageScatterRasterRequested &&
                usePacketPageCulling;
            bool packetStatesValid = true;
            for (uint32_t level = firstScheduledClipmap;
                level < SvsmClipmapCount;
                ++level)
            {
                if (m_SparseDepthPass->
                        GetRenderPacketCount(level) == 0u)
                {
                    continue;
                }
                SparseDepthPass::Context context;
                if (!m_SparseDepthPass->RenderPackets(
                        commandList,
                        m_ClipmapViews[level].get(),
                        m_RasterFramebuffer,
                        context,
                        level,
                        m_IndirectDrawCapacity,
                        settings.gpuGatedDrawSubmission,
                        useBatchedDrawSubmission,
                        false,
                        usePacketPageCulling,
                        prevalidationDirtyPageScatterRaster,
                        true))
                {
                    packetStatesValid = false;
                    break;
                }
            }
            packetPreparationMilliseconds +=
                std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() -
                    packetPrevalidationStart).count();
            if (!packetStatesValid)
            {
                // No page work has been recorded yet, but apply the same
                // conservative recovery contract as a mid-raster failure.
                EndTimerFrame();
                discardFailedSparseRasterTransaction();
                InvalidateUiTimings();
                InvalidateDebugCounters();
                m_Timings.active = false;
                if (!m_ReportedRasterSubmissionFailure)
                {
                    log::error(
                        "SVSM sparse raster state prevalidation failed; latching a full rebuild and returning white visibility.");
                    m_ReportedRasterSubmissionFailure = true;
                }
                return {};
            }
        }
        if (ShouldUpgradeSvsmLocalizedPagesToStatic(
                effectivePairedStaticDynamicDepthEnabled,
                usePacketPageCulling))
        {
            // The reference raster path deliberately treats every caster as
            // static because it has no per-packet classification. Upgrade
            // localized dirtiness to clear both slices in that mode; restoring
            // slice one for a moved caster would otherwise resurrect its old
            // silhouette before the all-static raster rejects the page.
            for (uint32_t& page : localInvalidationPages)
                page |= SvsmLocalInvalidationStaticBit;
        }
        const bool useDirtyPageScatterRaster =
            dirtyPageScatterRasterRequested &&
            usePacketPageCulling;
        const bool scheduledTileMaskRequested =
            settings.hierarchicalScheduledPageMaskEnabled &&
            usePacketPageCulling;
        const bool scheduledTileMaskResourcesAvailable =
            bool(m_ScheduledPageTileMasks) &&
            bool(m_SparsePipelines[
                SparseBuildScheduledPageTileMasks]) &&
            bool(m_SparseScheduledTileMaskFillPipeline);
        const uint32_t scheduledTileMaskGeneration =
            uint32_t(
                m_TimerFrame %
                uint64_t(std::numeric_limits<uint32_t>::max())) + 1u;
        const bool useScheduledTileMask =
            ShouldUseSvsmScheduledPageTileMask(
                settings.hierarchicalScheduledPageMaskEnabled,
                usePacketPageCulling,
                useDirtyPageScatterRaster,
                scheduledTileMaskResourcesAvailable,
                scheduledTileMaskGeneration);
        if (useScheduledTileMask)
        {
            constants.hierarchyGeneration =
                scheduledTileMaskGeneration;
            m_ReportedScheduledTileMaskFallback = false;
        }
        else if (scheduledTileMaskRequested &&
            !scheduledTileMaskResourcesAvailable &&
            !m_ReportedScheduledTileMaskFallback)
        {
            log::warning(
                "SVSM scheduled-page tile-mask resources are unavailable; retaining the exact packet-page scan.");
            m_ReportedScheduledTileMaskFallback = true;
        }
        else if (!scheduledTileMaskRequested)
        {
            m_ReportedScheduledTileMaskFallback = false;
        }
        const bool receiverPageMaskRequestedForFrame =
            settings.receiverPageMaskCullingEnabled &&
            usePacketPageCulling &&
            !effectiveCacheEnabled &&
            !effectivePairedStaticDynamicDepthEnabled &&
            markStaticPageRequests;
        const nvrhi::ComputePipelineHandle&
            selectedReceiverPageMaskMarkPipeline =
                settings.precomposedClipmapTransformsEnabled
                ? m_SparsePrecomposedReceiverPageMaskMarkPipeline
                : m_SparseReceiverPageMaskMarkPipeline;
        const nvrhi::ComputePipelineHandle&
            selectedReceiverPageMaskFillPipeline =
                useScheduledTileMask
                ? m_SparseScheduledTileReceiverPageMaskFillPipeline
                : m_SparseReceiverPageMaskFillPipeline;
        const bool receiverPageMaskResourcesAvailable =
            bool(m_ReceiverPageMasks) &&
            bool(selectedReceiverPageMaskMarkPipeline) &&
            bool(selectedReceiverPageMaskFillPipeline);
        const bool useReceiverPageMask =
            ShouldUseSvsmReceiverPageMaskCulling(
                settings.mode,
                settings.receiverPageMaskCullingEnabled,
                usePacketPageCulling,
                effectiveCacheEnabled,
                effectivePairedStaticDynamicDepthEnabled,
                useDirtyPageScatterRaster,
                markStaticPageRequests,
                receiverPageMaskResourcesAvailable,
                scheduledTileMaskGeneration);
        if (useReceiverPageMask)
        {
            constants.hierarchyGeneration =
                scheduledTileMaskGeneration;
            m_ReportedReceiverPageMaskFallback = false;
        }
        else if (receiverPageMaskRequestedForFrame &&
            !receiverPageMaskResourcesAvailable &&
            !m_ReportedReceiverPageMaskFallback)
        {
            log::warning(
                "SVSM receiver-page mask resources are unavailable; retaining the exact uncached packet-page list.");
            m_ReportedReceiverPageMaskFallback = true;
        }
        else if (!receiverPageMaskRequestedForFrame)
        {
            m_ReportedReceiverPageMaskFallback = false;
        }
        const bool staticDepthHierarchyRequested =
            settings.staticDepthHierarchyCullingEnabled &&
            usePacketPageCulling &&
            effectivePairedStaticDynamicDepthEnabled &&
            !useDirtyPageScatterRaster;
        const nvrhi::ComputePipelineHandle&
            staticDepthPostRasterPipeline =
                deferredStaticDepthMergeActive
                ? m_SparseDeferredStaticDepthMergePipeline
                : m_SparsePipelines[
                    SparseBuildStaticDepthHierarchy];
        const bool staticDepthHierarchyResourcesAvailable =
            bool(m_StaticDepthHierarchy) &&
            bool(staticDepthPostRasterPipeline) &&
            bool(m_SparseStaticDepthHierarchyFillPipeline) &&
            (!useScheduledTileMask ||
                bool(
                    m_SparseScheduledTileMaskStaticDepthHierarchyFillPipeline));
        const bool useStaticDepthHierarchy =
            ShouldUseSvsmStaticDepthHierarchyCulling(
                settings.mode,
                settings.staticDepthHierarchyCullingEnabled,
                usePacketPageCulling,
                effectivePairedStaticDynamicDepthEnabled,
                useDirtyPageScatterRaster,
                staticDepthHierarchyResourcesAvailable);
        if (!useStaticDepthHierarchy)
        {
            m_StaticDepthHierarchyBootstrapRequired =
                GetNextSvsmStaticDepthHierarchyBootstrapRequired(
                    m_StaticDepthHierarchyBootstrapRequired,
                    false,
                    false);
        }
        const bool bootstrapStaticDepthHierarchy =
            ShouldBootstrapSvsmStaticDepthHierarchy(
                m_StaticDepthHierarchyBootstrapRequired,
                useStaticDepthHierarchy);
        if (useStaticDepthHierarchy)
        {
            constants.flags |=
                SVSM_SPARSE_FLAG_STATIC_DEPTH_HIERARCHY_CULLING;
            if (bootstrapStaticDepthHierarchy)
            {
                constants.flags |=
                    SVSM_SPARSE_FLAG_STATIC_DEPTH_HIERARCHY_BOOTSTRAP;
            }
            constants.hierarchyGeneration =
                scheduledTileMaskGeneration;
            m_ReportedStaticDepthHierarchyFallback = false;
        }
        else if (staticDepthHierarchyRequested &&
            !staticDepthHierarchyResourcesAvailable &&
            !m_ReportedStaticDepthHierarchyFallback)
        {
            log::warning(
                "SVSM static-depth hierarchy resources are unavailable; retaining the exact dynamic caster packet-page list.");
            m_ReportedStaticDepthHierarchyFallback = true;
        }
        else if (!staticDepthHierarchyRequested)
        {
            m_ReportedStaticDepthHierarchyFallback = false;
        }
        if (usePacketPageCulling)
        {
            constants.flags |= SVSM_SPARSE_FLAG_PACKET_PAGE_CULLING;
            if (settings.packetRectangleDirectScanEnabled)
            {
                constants.flags |=
                    SVSM_SPARSE_FLAG_PACKET_RECTANGLE_DIRECT_SCAN;
            }
        }
        if (useReceiverPageMask)
        {
            constants.flags |=
                SVSM_SPARSE_FLAG_RECEIVER_PAGE_MASK_CULLING;
        }
        const bool useGlobalDirtyPageRectangle =
            useDirtyPageScatterRaster &&
            !settings.dirtyPageScatterAmplificationGuardEnabled;
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
                m_CurrentTimerSourceTag != 0u) &&
            !bootstrapStaticDepthHierarchy &&
            !ShouldBlockSvsmStaticZeroWorkForSnapshotTransaction(
                casterSnapshots.pendingReady,
                sceneStateChanged,
                requiresFullSceneInvalidation);
        m_Timings.packetPageCullingActive =
            IsSvsmStaticPageMaintenanceOptimizationActive(
                usePacketPageCulling,
                staticPageRequestAction);
        m_Timings.hierarchicalScheduledPageMaskActive =
            IsSvsmStaticPageMaintenanceOptimizationActive(
                useScheduledTileMask,
                staticPageRequestAction);
        m_Timings.hierarchicalScheduledPageMaskUnavailable =
            scheduledTileMaskRequested &&
            !scheduledTileMaskResourcesAvailable;
        m_Timings.receiverPageMaskCullingRequested =
            settings.receiverPageMaskCullingEnabled;
        m_Timings.receiverPageMaskCullingActive =
            useReceiverPageMask;
        m_Timings.receiverPageMaskCullingUnavailable =
            receiverPageMaskRequestedForFrame &&
            !receiverPageMaskResourcesAvailable;
        m_Timings.staticDepthHierarchyCullingRequested =
            settings.staticDepthHierarchyCullingEnabled;
        m_Timings.staticDepthHierarchyCullingActive =
            bootstrapStaticDepthHierarchy ||
            IsSvsmStaticPageMaintenanceOptimizationActive(
                useStaticDepthHierarchy,
                staticPageRequestAction);
        m_Timings.staticDepthHierarchyCullingUnavailable =
            staticDepthHierarchyRequested &&
            !staticDepthHierarchyResourcesAvailable;
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
        m_Timings.cachedShadowDrawListsActive =
            settings.renderPacketCachingEnabled &&
            useRenderPackets &&
            m_SparseDepthPass->HasRenderPacketCache();
        if (m_Timings.cachedShadowDrawListsActive)
        {
            m_Timings.cachedShadowDrawListPacketCount =
                m_SparseDepthPass->GetRenderPacketCount();
            if (!m_Timings.cachedShadowDrawListsRebuilt)
                m_Timings.cachedShadowDrawListsReused = true;
        }
        SparseVirtualShadowMapSettings effectiveTimingSettings = settings;
        effectiveTimingSettings.resolutionBias =
            effectiveResolutionBias;
        UpdateUiTimingContext(
            effectiveTimingSettings,
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
            m_Timings.receiverPageMaskQueries = 0u;
            m_Timings.receiverPageMaskCulledPages = 0u;
            m_Timings.receiverPageMaskFailOpens = 0u;
            m_Timings.staticDepthHierarchyQueries = 0u;
            m_Timings.staticDepthHierarchyCulledPages = 0u;
            m_Timings.staticDepthHierarchyFailOpens = 0u;
            m_Timings.staticDepthHierarchyBuiltPages = 0u;
            RecordSvsmStaticZeroWorkFrame(m_Timings);
            PublishKnownZeroUiTiming();
            m_Timings.active = true;
            commitSuccessfulSparseState();
            EndTimerFrame();
            return {
                m_SparseVisibilityCache[resolveVisibilitySlot],
                light,
                false
            };
        }

        RecordSvsmGpuWorkSubmission(m_Timings);
        commandList->beginMarker(
            effectiveCacheEnabled
                ? "SVSM Sparse Cached"
                : "SVSM Sparse Uncached");
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
        if (!localInvalidationPages.empty())
        {
            commandList->writeBuffer(
                m_LocalInvalidationPages,
                localInvalidationPages.data(),
                uint64_t(localInvalidationPages.size()) *
                    sizeof(uint32_t));
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
            if (m_StaticDepthHierarchy)
            {
                commandList->clearBufferUInt(
                    m_StaticDepthHierarchy, 0u);
            }
            if (m_ReceiverPageMasks)
            {
                commandList->clearBufferUInt(
                    m_ReceiverPageMasks, 0u);
            }
        }
        if (performStaticPageMaintenance ||
            settings.debugView != SvsmDebugView::None)
        {
            commandList->clearBufferUInt(
                m_RenderPages, SvsmInvalidPhysicalPage);
            commandList->clearBufferUInt(m_Counters, 0u);
        }

        auto dispatchSparse =
            [this, commandList, &constants, &settings,
                useScheduledTileMask,
                useReceiverPageMask,
                useStaticDepthHierarchy,
                deferredStaticDepthMergeActive](
                uint32_t stage,
                uint32_t x,
                uint32_t y,
                uint32_t z = 1u) {
                commandList->writeBuffer(
                    m_SparseConstants,
                    &constants,
                    sizeof(constants));
                nvrhi::ComputeState state;
                state.pipeline =
                    stage == SparseMark
                    ? (useReceiverPageMask
                        ? (settings.precomposedClipmapTransformsEnabled
                            ? m_SparsePrecomposedReceiverPageMaskMarkPipeline
                            : m_SparseReceiverPageMaskMarkPipeline)
                        : (settings.precomposedClipmapTransformsEnabled
                            ? m_SparsePrecomposedMarkPipeline
                            : m_SparsePipelines[SparseMark]))
                    : stage == SparseFillIndirect
                    ? (useReceiverPageMask
                        ? (useScheduledTileMask
                            ? m_SparseScheduledTileReceiverPageMaskFillPipeline
                            : m_SparseReceiverPageMaskFillPipeline)
                        : (useStaticDepthHierarchy
                            ? (useScheduledTileMask
                                ? m_SparseScheduledTileMaskStaticDepthHierarchyFillPipeline
                                : m_SparseStaticDepthHierarchyFillPipeline)
                            : (useScheduledTileMask
                                ? m_SparseScheduledTileMaskFillPipeline
                                : m_SparsePipelines[SparseFillIndirect])))
                    : stage == SparseBuildStaticDepthHierarchy &&
                            deferredStaticDepthMergeActive
                    ? m_SparseDeferredStaticDepthMergePipeline
                    : m_SparsePipelines[stage];
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
        if (useGlobalDirtyPageRectangle)
        {
            commandList->clearBufferUInt(
                m_DirtyPageRectangles, 0u);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_DirtyPageRectangles);
            commandList->commitBarriers();
        }
        if (markStaticPageRequests ||
            !localInvalidationPages.empty())
        {
#ifdef _WIN32
            SetD3d12DredMarker(commandList, L"SVSM Mark Required Pages");
#endif
            BeginTimer(commandList, TimerPageMarking);
            if (markStaticPageRequests)
            {
                if (useReceiverPageMask)
                {
                    // Receiver masks are current-camera coverage. Clear all
                    // words before atomically rebuilding them so a newly
                    // published generation cannot inherit stale coverage.
                    commandList->clearBufferUInt(
                        m_ReceiverPageMasks, 0u);
                    nvrhi::utils::BufferUavBarrier(
                        commandList, m_ReceiverPageMasks);
                    commandList->commitBarriers();
                }
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
                    div_ceil(
                        cameraDepthDesc.width,
                        markingGroupCoverage),
                    div_ceil(
                        cameraDepthDesc.height,
                        markingGroupCoverage));
                nvrhi::utils::TextureUavBarrier(
                    commandList, m_PageTable);
                if (useReceiverPageMask)
                {
                    nvrhi::utils::BufferUavBarrier(
                        commandList, m_ReceiverPageMasks);
                }
                commandList->commitBarriers();
            }
            if (!localInvalidationPages.empty())
            {
                dispatchSparse(
                    SparseInvalidatePages,
                    div_ceil(
                        uint32_t(localInvalidationPages.size()),
                        64u),
                    1u);
                nvrhi::utils::TextureUavBarrier(
                    commandList, m_PageTable);
                commandList->commitBarriers();
            }
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
        const bool deterministicFinePageBudget =
            ShouldUseSvsmDeterministicFinePageBudget(
                settings.pageRenderBudget,
                settings.physicalPageCount,
                settings.coarsestPageRenderBudgetEnabled);
        if (deterministicFinePageBudget)
        {
            commandList->clearBufferUInt(
                m_FinePageCandidateMasks, 0u);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_FinePageCandidateMasks);
            commandList->commitBarriers();
        }
        dispatchSparse(
            SparseRecycle,
            div_ceil(
                settings.physicalPageCount,
                64u),
            1u);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_PhysicalOwners);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_CompactRenderPages);
        nvrhi::utils::BufferUavBarrier(
            commandList, m_Counters);
        if (deterministicFinePageBudget)
        {
            // Recycle records required fine owners in a priority bitmask.
            // Coarse allocation assigns unique reverse-priority victim ranks
            // only after every classification write is visible.
            nvrhi::utils::BufferUavBarrier(
                commandList, m_FinePageCandidateMasks);
        }
        commandList->commitBarriers();
        auto dispatchAllocationLevel =
            [&](uint32_t level, uint32_t groupCount) {
                constants.selectedClipmap = level;
                dispatchSparse(
                    SparseAllocate,
                    groupCount,
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
                if (deterministicFinePageBudget &&
                    level == SvsmClipmapCount - 1u)
                {
                    nvrhi::utils::BufferUavBarrier(
                        commandList, m_FinePageCandidateMasks);
                }
                if (useGlobalDirtyPageRectangle)
                {
                    nvrhi::utils::BufferUavBarrier(
                        commandList, m_DirtyPageRectangles);
                }
                commandList->commitBarriers();
            };
        if (deterministicFinePageBudget)
        {
            // Publish the complete coarse fallback first. Each fine allocation
            // scan then validates residency and records compact candidates.
            // One global selector consumes those masks level-first and
            // centered-Morton within each level.
            const uint32_t coarsestLevel =
                SvsmClipmapCount - 1u;
            if (firstScheduledClipmap <= coarsestLevel)
            {
                dispatchAllocationLevel(
                    coarsestLevel,
                    div_ceil(
                        SvsmPagesPerClipmap,
                        64u));
            }
            // Coarse fallback has finished reading the deterministic
            // required-fine victim mask. Clear and reuse the same 2.5 KiB
            // allocation for current fine candidates.
            commandList->clearBufferUInt(
                m_FinePageCandidateMasks, 0u);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_FinePageCandidateMasks);
            commandList->commitBarriers();
            for (uint32_t level = firstScheduledClipmap;
                level < coarsestLevel;
                ++level)
            {
                dispatchAllocationLevel(
                    level,
                    div_ceil(
                        SvsmPagesPerClipmap,
                        64u));
            }
            if (firstScheduledClipmap < coarsestLevel)
            {
                nvrhi::utils::BufferUavBarrier(
                    commandList, m_FinePageCandidateMasks);
                commandList->commitBarriers();
                constants.selectedClipmap = 0u;
                dispatchSparse(
                    SparseScheduleFine,
                    1u,
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
                if (useGlobalDirtyPageRectangle)
                {
                    nvrhi::utils::BufferUavBarrier(
                        commandList, m_DirtyPageRectangles);
                }
                commandList->commitBarriers();
            }
        }
        else
        {
            for (int level = int(SvsmClipmapCount) - 1;
                level >= int(firstScheduledClipmap);
                --level)
            {
                dispatchAllocationLevel(
                    uint32_t(level),
                    div_ceil(
                        SvsmPagesPerClipmap,
                        64u));
            }
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
                firstScheduledClipmap,
                deferredStaticDepthMergeActive](uint32_t stage) {
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
                    state.pipeline =
                        stage == SparseBuildStaticDepthHierarchy &&
                            deferredStaticDepthMergeActive
                        ? m_SparseDeferredStaticDepthMergePipeline
                        : m_SparsePipelines[stage];
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
        if (m_StaticDepthHierarchy)
        {
            nvrhi::utils::BufferUavBarrier(
                commandList, m_StaticDepthHierarchy);
        }
        commandList->commitBarriers();
        EndTimer(commandList, TimerClearing);

        if (usePacketPageCulling)
            BeginTimer(commandList, TimerPacketPageCulling);
        if (useScheduledTileMask)
        {
#ifdef _WIN32
            SetD3d12DredMarker(
                commandList,
                L"SVSM Build Scheduled Page Tile Masks");
#endif
            dispatchSparse(
                SparseBuildScheduledPageTileMasks,
                1u,
                1u,
                SvsmClipmapCount);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_ScheduledPageTileMasks);
            commandList->commitBarriers();
        }

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
                        useReceiverPageMask
                        ? (useScheduledTileMask
                            ? m_SparseScheduledTileReceiverPageMaskFillPipeline
                            : m_SparseReceiverPageMaskFillPipeline)
                        : (useStaticDepthHierarchy
                            ? (useScheduledTileMask
                                ? m_SparseScheduledTileMaskStaticDepthHierarchyFillPipeline
                                : m_SparseStaticDepthHierarchyFillPipeline)
                            : (useScheduledTileMask
                                ? m_SparseScheduledTileMaskFillPipeline
                                : m_SparsePipelines[SparseFillIndirect]));
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
        bool rasterSubmissionSucceeded = true;
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
                    rasterSubmissionSucceeded =
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
                rasterSubmissionSucceeded =
                    m_SparseDepthPass->RenderViewReference(
                        commandList,
                        m_ClipmapViews[level].get(),
                        m_RasterFramebuffer,
                        drawStrategy,
                        context,
                        level);
            }
            if (!rasterSubmissionSucceeded)
                break;
        }
        const auto submissionEnd =
            std::chrono::steady_clock::now();
        m_Timings.cullingCpuMilliseconds =
            packetPreparationMilliseconds +
            std::chrono::duration<float, std::milli>(
                submissionEnd - submissionStart).count();

        if (!rasterSubmissionSucceeded)
        {
            EndTimer(commandList, TimerPageRendering);
            EndTimer(commandList, TimerTotal);
            commandList->endMarker();
            // Seal issued queries with the old generation. Invalidating after
            // this point makes the partial transaction impossible to publish
            // through delayed timing or counter readback.
            DiscardCurrentTimerFrame();
            EndTimerFrame();
            discardFailedSparseRasterTransaction();
            InvalidateUiTimings();
            InvalidateDebugCounters();
            m_Timings.active = false;
            if (!m_ReportedRasterSubmissionFailure)
            {
                log::error(
                    "SVSM sparse raster submission failed; discarding the page transaction, latching a full rebuild, and returning white visibility.");
                m_ReportedRasterSubmissionFailure = true;
            }
            return {};
        }
        m_ReportedRasterSubmissionFailure = false;

        constants.selectedClipmap = 0u;
        nvrhi::utils::TextureUavBarrier(
            commandList, m_SparsePhysicalDepth);
        commandList->commitBarriers();
        const SvsmStaticDepthPostRasterWork staticDepthPostRasterWork =
            GetSvsmStaticDepthPostRasterWork(
                deferredStaticDepthMergeActive,
                useStaticDepthHierarchy);
        if (staticDepthPostRasterWork !=
            SvsmStaticDepthPostRasterWork::None)
        {
#ifdef _WIN32
            SetD3d12DredMarker(
                commandList,
                staticDepthPostRasterWork ==
                        SvsmStaticDepthPostRasterWork::
                            MergeAndHierarchy
                    ? L"SVSM Merge Static Depth And Build Hierarchy"
                    : staticDepthPostRasterWork ==
                            SvsmStaticDepthPostRasterWork::MergeOnly
                        ? L"SVSM Merge Static Depth"
                        : L"SVSM Build Static Depth Hierarchy");
#endif
            if (bootstrapStaticDepthHierarchy)
            {
                // Bootstrap scans the fixed physical pool once. Clean pages
                // need no shadow redraw: their paired static slice is already
                // complete. The deferred permutation also merges only
                // scheduled static-dirty pages after their raster work.
                dispatchSparse(
                    SparseBuildStaticDepthHierarchy,
                    settings.physicalPageCount,
                    1u);
            }
            else if (!settings.gpuGatedDrawSubmission)
            {
                // Without compact indirect lists, scan the fixed pool once.
                // The shader accepts only exact current render-page owners.
                dispatchSparse(
                    SparseBuildStaticDepthHierarchy,
                    settings.physicalPageCount,
                    1u);
            }
            else
            {
                // Steady-state rebuilds consume only the exact compact
                // per-level dirty-page counts.
                dispatchCompactPageStage(
                    SparseBuildStaticDepthHierarchy);
            }
            if (useStaticDepthHierarchy)
            {
                nvrhi::utils::BufferUavBarrier(
                    commandList, m_StaticDepthHierarchy);
            }
            if (deferredStaticDepthMergeActive)
            {
                nvrhi::utils::TextureUavBarrier(
                    commandList, m_SparsePhysicalDepth);
            }
            commandList->commitBarriers();
        }
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
            m_Timings.receiverPageMaskQueries = 0u;
            m_Timings.receiverPageMaskCulledPages = 0u;
            m_Timings.receiverPageMaskFailOpens = 0u;
            m_Timings.staticDepthHierarchyQueries = 0u;
            m_Timings.staticDepthHierarchyCulledPages = 0u;
            m_Timings.staticDepthHierarchyFailOpens = 0u;
            m_Timings.staticDepthHierarchyBuiltPages = 0u;
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
        if (bootstrapStaticDepthHierarchy &&
            !performStaticPageMaintenance)
        {
#ifdef _WIN32
            SetD3d12DredMarker(
                commandList,
                L"SVSM Bootstrap Static Depth Hierarchy");
#endif
            BeginTimer(commandList, TimerPageRendering);
            dispatchSparse(
                SparseBuildStaticDepthHierarchy,
                settings.physicalPageCount,
                1u);
            nvrhi::utils::BufferUavBarrier(
                commandList, m_StaticDepthHierarchy);
            if (deferredStaticDepthMergeActive)
            {
                nvrhi::utils::TextureUavBarrier(
                    commandList, m_SparsePhysicalDepth);
            }
            commandList->commitBarriers();
            EndTimer(commandList, TimerPageRendering);
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
                GetSvsmSparseResolvePermutationIndex(
                    settings.poissonOrdering,
                    settings.filterKernel,
                    settings.precomposedClipmapTransformsEnabled,
                    settings.pageTranslationCachingEnabled,
                    settings.tapCount);
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
                        uint64_t(c_DebugCounterReadbackCount) *
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
        m_PreviousLightDepthOrigin =
            GetNextSvsmCommittedLightDepthOrigin(
                m_PreviousLightDepthOrigin,
                m_CurrentLightDepthOrigin,
                true);
        m_PreviousSceneStateHash = sceneStateHash;
        m_PreviousSceneStateRevision = sceneStateRevision;
        m_PreviousSceneStateRevisionReliable =
            sceneStateRevisionReliable;
        m_PreviousFirstClipmapExtent =
            settings.firstClipmapExtent;
        m_PreviousMaximumLightDepth =
            settings.maximumLightDepth;
        m_PreviousLocalizedInvalidationEnabled =
            settings.localizedInvalidationEnabled;
        m_PreviousAdaptiveCasterCacheClassificationEnabled =
            settings.adaptiveCasterCacheClassificationEnabled;
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
        m_StaticPageRequestFilterKernel =
            settings.filterKernel;
        m_StaticPageRequestPoissonOrdering =
            settings.poissonOrdering;
        m_StaticPageRequestTapCount = settings.tapCount;
        m_StaticPageRequestResolutionBias =
            effectiveResolutionBias;
        m_StaticPageRequestReceiverDistanceMipClampStart =
            effectiveReceiverDistanceMipClampStart;
        m_StaticPageRequestReceiverDistanceMipClampMaximumLevel =
            receiverDistanceMipClampMaximumLevel;
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
        m_StaticVisibilityFilterKernel = settings.filterKernel;
        m_StaticVisibilityPoissonOrdering =
            settings.poissonOrdering;
        m_StaticVisibilityTapCount = settings.tapCount;
        m_StaticVisibilityResolutionBias =
            effectiveResolutionBias;
        m_StaticVisibilityReceiverDistanceMipClampStart =
            effectiveReceiverDistanceMipClampStart;
        m_StaticVisibilityReceiverDistanceMipClampMaximumLevel =
            receiverDistanceMipClampMaximumLevel;
        m_StaticVisibilityPageTranslationCaching =
            settings.pageTranslationCachingEnabled;
        m_StaticVisibilityAdaptiveFiltering =
            settings.adaptiveFiltering;
        m_StaticDepthHierarchyBootstrapRequired =
            GetNextSvsmStaticDepthHierarchyBootstrapRequired(
                m_StaticDepthHierarchyBootstrapRequired,
                useStaticDepthHierarchy,
                bootstrapStaticDepthHierarchy);
        commitSuccessfulSparseState();
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
        // Deactivation can span arbitrary scene edits while no sparse state is
        // being committed. The first later sparse frame must rebuild rather
        // than trusting cache state preserved across the inactive interval.
        m_RequiresFullSceneInvalidationLatched = true;
        m_DepthBindingCacheResetLatched = true;
        m_StaticDepthHierarchyBootstrapRequired = true;
        if (m_SparseDepthPass)
        {
            m_SparseDepthPass->InvalidateRenderPacketCache();
            m_SparseDepthPass->
                InvalidatePersistentCasterSourceCache();
        }
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
