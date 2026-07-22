#pragma pack_matrix(row_major)

#include <donut/shaders/bindless.h>
#include <donut/shaders/forward_vertex.hlsli>
#include <donut/shaders/gbuffer_cb.h>
#include <donut/shaders/binding_helpers.hlsli>
#include "sparse_virtual_shadow_map_sparse_cb.h"

#define MATERIAL_REGISTER_SPACE     GBUFFER_SPACE_MATERIAL
#define MATERIAL_CB_SLOT            GBUFFER_BINDING_MATERIAL_CONSTANTS
#define MATERIAL_DIFFUSE_SLOT       GBUFFER_BINDING_MATERIAL_DIFFUSE_TEXTURE
#define MATERIAL_SPECULAR_SLOT      GBUFFER_BINDING_MATERIAL_SPECULAR_TEXTURE
#define MATERIAL_NORMALS_SLOT       GBUFFER_BINDING_MATERIAL_NORMAL_TEXTURE
#define MATERIAL_EMISSIVE_SLOT      GBUFFER_BINDING_MATERIAL_EMISSIVE_TEXTURE
#define MATERIAL_OCCLUSION_SLOT     GBUFFER_BINDING_MATERIAL_OCCLUSION_TEXTURE
#define MATERIAL_TRANSMISSION_SLOT  GBUFFER_BINDING_MATERIAL_TRANSMISSION_TEXTURE
#define MATERIAL_OPACITY_SLOT       GBUFFER_BINDING_MATERIAL_OPACITY_TEXTURE
#define MATERIAL_SAMPLER_REGISTER_SPACE GBUFFER_SPACE_VIEW
#define MATERIAL_SAMPLER_SLOT GBUFFER_BINDING_MATERIAL_SAMPLER

#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/material_bindings.hlsli>

#define SVSM_INVALID_PAGE 0xffffffffu
#define SVSM_PAGES_PER_AXIS 64u
#define SVSM_PAGES_PER_CLIPMAP 4096u
#define SVSM_PAGE_SIZE 128u
#define SVSM_ATLAS_SIZE 8192u
#define SVSM_COMPACT_OWNER_MASK 0x7fffu
#define SVSM_COMPACT_PHYSICAL_SHIFT 15u
#define SVSM_PHYSICAL_MASK 0x7fffu
#define SVSM_RESIDENT_BIT (1u << 15u)
#define SVSM_DIRTY_BIT (1u << 17u)

#ifndef SVSM_BATCHED_DRAW
#define SVSM_BATCHED_DRAW 0
#endif

cbuffer c_Svsm : REGISTER_CBUFFER(3, GBUFFER_SPACE_VIEW)
{
    SparseVirtualShadowMapSparseConstants g_Svsm;
};

StructuredBuffer<uint> t_CompactRenderPages :
    REGISTER_SRV(7, GBUFFER_SPACE_VIEW);
StructuredBuffer<SparseVirtualShadowMapPacketMetadata>
    t_PacketPageMetadata : REGISTER_SRV(8, GBUFFER_SPACE_VIEW);
StructuredBuffer<uint> t_PacketPageRuntime :
    REGISTER_SRV(9, GBUFFER_SPACE_VIEW);
StructuredBuffer<uint> t_PacketRenderPages :
    REGISTER_SRV(10, GBUFFER_SPACE_VIEW);
Texture2DArray<uint> t_PageTable :
    REGISTER_SRV(11, GBUFFER_SPACE_VIEW);
StructuredBuffer<uint> t_RenderPages :
    REGISTER_SRV(12, GBUFFER_SPACE_VIEW);
RWTexture2D<uint> u_PhysicalDepth :
    REGISTER_UAV(0, GBUFFER_SPACE_VIEW);

StructuredBuffer<InstanceData> t_Instances :
    REGISTER_SRV(GBUFFER_BINDING_INSTANCE_BUFFER, GBUFFER_SPACE_INPUT);
ByteAddressBuffer t_Vertices :
    REGISTER_SRV(GBUFFER_BINDING_VERTEX_BUFFER, GBUFFER_SPACE_INPUT);
DECLARE_PUSH_CONSTANTS(
    SparseVirtualShadowMapPushConstants,
    g_Push,
    GBUFFER_BINDING_PUSH_CONSTANTS,
    GBUFFER_SPACE_INPUT);

int WrapPage(int coordinate)
{
    int wrapped = coordinate % int(SVSM_PAGES_PER_AXIS);
    return wrapped < 0 ? wrapped + int(SVSM_PAGES_PER_AXIS) : wrapped;
}

bool TryLoadDirtyPageRectangle(
    uint packetIndex,
    out uint2 minimumPage,
    out uint2 maximumPage)
{
    minimumPage = 0u;
    maximumPage = 0u;
    uint runtimeBase =
        packetIndex * SVSM_PACKET_PAGE_RUNTIME_WORDS;
    uint runtimeState = t_PacketPageRuntime[
        runtimeBase +
        SVSM_PACKET_PAGE_RUNTIME_STATE_WORD];
    if ((runtimeState &
            SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK) == 0u)
    {
        return false;
    }

    uint packedMinimum = t_PacketPageRuntime[
        runtimeBase +
        SVSM_PACKET_PAGE_RUNTIME_MINIMUM_WORD];
    uint packedMaximum = t_PacketPageRuntime[
        runtimeBase +
        SVSM_PACKET_PAGE_RUNTIME_MAXIMUM_WORD];
    if (packedMinimum == SVSM_PACKET_PAGE_EMPTY_BOUNDS ||
        packedMaximum == SVSM_PACKET_PAGE_EMPTY_BOUNDS)
    {
        minimumPage = 0u;
        maximumPage = SVSM_PAGES_PER_AXIS - 1u;
        return true;
    }

    minimumPage = uint2(
        packedMinimum & 0xffu,
        (packedMinimum >> 8u) & 0xffu);
    maximumPage = uint2(
        packedMaximum & 0xffu,
        (packedMaximum >> 8u) & 0xffu);
    if (any(minimumPage > maximumPage) ||
        any(maximumPage >= SVSM_PAGES_PER_AXIS))
    {
        minimumPage = 0u;
        maximumPage = SVSM_PAGES_PER_AXIS - 1u;
    }
    return true;
}

void vertexMain(
    uint vertex : SV_VertexID,
    uint instanceId : SV_InstanceID,
#if SVSM_BATCHED_DRAW
    int drawVertexOffset : SV_StartVertexLocation,
    uint drawInstanceOffset : SV_StartInstanceLocation,
#endif
    out float4 outputPosition : SV_Position,
    out float2 outputTexCoord : TEXCOORD,
    nointerpolation out uint outputScatterRaster : TEXCOORD1,
    out float4 clipDistance : SV_ClipDistance)
{
    outputPosition = float4(-2.0f, -2.0f, 0.0f, 1.0f);
    outputTexCoord = 0.0f;
    outputScatterRaster = 0u;
    clipDistance = -1.0f;
    if (g_Push.physicalPageCount == 0u)
        return;

    bool packetPageCulling =
        (g_Push.flags &
            SVSM_SPARSE_DEPTH_FLAG_PACKET_PAGE_CULLING) != 0u;
    bool dirtyPageScatterRaster =
        packetPageCulling &&
        (g_Push.flags &
            SVSM_SPARSE_DEPTH_FLAG_DIRTY_PAGE_SCATTER_RASTER) != 0u;
    uint packetIndex = 0u;
    uint pageInstance = 0u;
    uint instanceIndex = 0u;
    uint vertexIndex = 0u;

#if SVSM_BATCHED_DRAW
    if ((g_Push.flags &
            SVSM_SPARSE_DEPTH_FLAG_BATCHED_DRAW) == 0u ||
        drawVertexOffset < 0)
    {
        return;
    }
    pageInstance = instanceId;
    vertexIndex = uint(drawVertexOffset) + vertex;
    if (packetPageCulling)
    {
        packetIndex = drawInstanceOffset;
        instanceIndex =
            t_PacketPageMetadata[packetIndex].objectInstanceIndex;
    }
    else
    {
        if (drawInstanceOffset % g_Push.physicalPageCount != 0u)
            return;
        instanceIndex =
            drawInstanceOffset / g_Push.physicalPageCount;
    }
#else
    if ((g_Push.flags &
            SVSM_SPARSE_DEPTH_FLAG_BATCHED_DRAW) != 0u ||
        g_Push.originalInstanceCount == 0u)
    {
        return;
    }
    vertexIndex = g_Push.startVertexLocation + vertex;
    if (packetPageCulling)
    {
        packetIndex = g_Push.packetIndex;
        pageInstance = instanceId;
        instanceIndex =
            t_PacketPageMetadata[packetIndex].objectInstanceIndex;
    }
    else
    {
        pageInstance =
            instanceId / g_Push.originalInstanceCount;
        instanceIndex =
            g_Push.startInstanceLocation +
            instanceId % g_Push.originalInstanceCount;
    }
#endif
    uint compactPage = SVSM_INVALID_PAGE;
    uint physicalPage = SVSM_INVALID_PAGE;
    uint level = g_Svsm.selectedClipmap;
    int2 localPage = 0;
    uint packetRuntimeState = 0u;
    bool dirtyPagePerPageRaster = false;
    bool dirtyPageScatterAmplificationGuard =
        dirtyPageScatterRaster &&
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_DIRTY_PAGE_SCATTER_AMPLIFICATION_GUARD) != 0u;
    if (dirtyPageScatterAmplificationGuard)
    {
        packetRuntimeState = t_PacketPageRuntime[
            packetIndex * SVSM_PACKET_PAGE_RUNTIME_WORDS +
            SVSM_PACKET_PAGE_RUNTIME_STATE_WORD];
        dirtyPagePerPageRaster =
            (packetRuntimeState &
                SVSM_PACKET_PAGE_RUNTIME_PER_PAGE) != 0u;
    }
    bool virtualDirtyPageScatterRaster =
        dirtyPageScatterRaster && !dirtyPagePerPageRaster;
    if (!virtualDirtyPageScatterRaster)
    {
        if (pageInstance >= g_Push.physicalPageCount)
            return;
        if (packetPageCulling)
        {
            uint runtimeState = dirtyPageScatterAmplificationGuard
                ? packetRuntimeState
                : t_PacketPageRuntime[
                    packetIndex * SVSM_PACKET_PAGE_RUNTIME_WORDS +
                    SVSM_PACKET_PAGE_RUNTIME_STATE_WORD];
            uint packetPageCount =
                runtimeState & SVSM_PACKET_PAGE_RUNTIME_COUNT_MASK;
            if (pageInstance >= packetPageCount)
                return;
            if ((runtimeState &
                    (SVSM_PACKET_PAGE_RUNTIME_FAIL_OPEN |
                        SVSM_PACKET_PAGE_RUNTIME_PER_PAGE)) != 0u)
            {
                uint compactIndex =
                    g_Svsm.selectedClipmap *
                        g_Push.physicalPageCount +
                    pageInstance;
                compactPage = t_CompactRenderPages[compactIndex];
            }
            else
            {
                compactPage = t_PacketRenderPages[
                    t_PacketPageMetadata[packetIndex].pageListOffset +
                    pageInstance];
            }
        }
        else
        {
            uint compactIndex =
                g_Svsm.selectedClipmap *
                    g_Push.physicalPageCount +
                pageInstance;
            compactPage = t_CompactRenderPages[compactIndex];
        }
        if (compactPage == SVSM_INVALID_PAGE)
            return;
        uint owner = compactPage & SVSM_COMPACT_OWNER_MASK;
        physicalPage =
            compactPage >> SVSM_COMPACT_PHYSICAL_SHIFT;
        if (physicalPage >= g_Push.physicalPageCount)
            return;
        level = owner / SVSM_PAGES_PER_CLIPMAP;
        if (level != g_Svsm.selectedClipmap)
            return;

        uint tablePageIndex = owner % SVSM_PAGES_PER_CLIPMAP;
        int2 tablePage = int2(
            tablePageIndex % SVSM_PAGES_PER_AXIS,
            tablePageIndex / SVSM_PAGES_PER_AXIS);
        int2 pageOffset =
            g_Svsm.pageTableOffsetAndDelta[level].xy;
        localPage = int2(
            WrapPage(tablePage.x - pageOffset.x),
            WrapPage(tablePage.y - pageOffset.y));
        // Ratio-guard fallback can still clip compact pages to trustworthy
        // packet bounds. Guarded fail-open deliberately has no trustworthy
        // rectangle, so it must traverse every bounded compact page instead.
        bool clipPerPageRasterToPacketBounds =
            dirtyPagePerPageRaster &&
            (packetRuntimeState &
                SVSM_PACKET_PAGE_RUNTIME_FAIL_OPEN) == 0u;
        if (clipPerPageRasterToPacketBounds)
        {
            uint2 minimumPage;
            uint2 maximumPage;
            if (!TryLoadDirtyPageRectangle(
                    packetIndex, minimumPage, maximumPage) ||
                any(localPage < int2(minimumPage)) ||
                any(localPage > int2(maximumPage)))
            {
                return;
            }
        }
    }

    InstanceData instance = t_Instances[instanceIndex];
    float3 objectPosition = asfloat(t_Vertices.Load3(
        g_Push.positionOffset +
        vertexIndex * c_SizeOfPosition));
    outputTexCoord = asfloat(t_Vertices.Load2(
        g_Push.texCoordOffset +
        vertexIndex * c_SizeOfTexcoord));
    float3 worldPosition =
        mul(instance.transform, float4(objectPosition, 1.0f)).xyz;
    float4 clip = mul(
        float4(worldPosition, 1.0f),
        g_Svsm.worldToClip[level]);
    if (!(clip.w != 0.0f) || !all(isfinite(clip)))
        return;

    float3 ndc = clip.xyz / clip.w;
    float2 virtualPosition =
        (ndc.xy * float2(0.5f, -0.5f) + 0.5f) *
        float(SVSM_ATLAS_SIZE);
    if (virtualDirtyPageScatterRaster)
    {
        uint2 minimumPage;
        uint2 maximumPage;
        if (!TryLoadDirtyPageRectangle(
                packetIndex, minimumPage, maximumPage))
        {
            return;
        }

        float2 minimumPixel =
            float2(minimumPage) * float(SVSM_PAGE_SIZE);
        float2 maximumPixel =
            float2(maximumPage + 1u) * float(SVSM_PAGE_SIZE);
        clipDistance = float4(
            virtualPosition.x - minimumPixel.x,
            maximumPixel.x - virtualPosition.x,
            virtualPosition.y - minimumPixel.y,
            maximumPixel.y - virtualPosition.y);
        outputPosition = float4(ndc, 1.0f);
        outputScatterRaster = 1u;
        return;
    }

    float2 pagePosition =
        virtualPosition / float(SVSM_PAGE_SIZE) -
        float2(localPage);
    clipDistance = float4(
        pagePosition.x,
        1.0f - pagePosition.x,
        pagePosition.y,
        1.0f - pagePosition.y);

    uint2 physicalCoordinate = uint2(
        physicalPage % SVSM_PAGES_PER_AXIS,
        physicalPage / SVSM_PAGES_PER_AXIS);
    float2 atlasPixel =
        (float2(physicalCoordinate) + pagePosition) *
        float(SVSM_PAGE_SIZE);
    float2 atlasNdc =
        atlasPixel / float(SVSM_ATLAS_SIZE) *
        float2(2.0f, -2.0f) +
        float2(-1.0f, 1.0f);
    outputPosition = float4(atlasNdc, ndc.z, 1.0f);
}

void pixelMain(
    in float4 position : SV_Position,
    in float2 texCoord : TEXCOORD,
    nointerpolation in uint inputScatterRaster : TEXCOORD1)
{
    bool dirtyPageScatterRaster = inputScatterRaster != 0u;
#if ALPHA_TESTED
    bool scatterAlphaTestEarlyReject =
        dirtyPageScatterRaster &&
        (g_Svsm.flags &
            SVSM_SPARSE_FLAG_SCATTER_ALPHA_TEST_EARLY_REJECT) != 0u;
    float2 texCoordDdx = 0.0f;
    float2 texCoordDdy = 0.0f;
    if (scatterAlphaTestEarlyReject)
    {
        // Capture derivatives before page ownership can reject neighboring
        // lanes. The deferred alpha test below then preserves the original
        // texture LOD while avoiding texture work for unscheduled holes.
        texCoordDdx = ddx(texCoord);
        texCoordDdy = ddy(texCoord);
    }
    else
    {
        MaterialTextureSample textures = DefaultMaterialTextures();
        if ((g_Material.flags &
                MaterialFlags_UseBaseOrDiffuseTexture) != 0)
        {
            textures.baseOrDiffuse =
                t_BaseOrDiffuse.Sample(s_MaterialSampler, texCoord);
        }
        if ((g_Material.flags & MaterialFlags_UseOpacityTexture) != 0)
        {
            textures.opacity =
                t_Opacity.Sample(s_MaterialSampler, texCoord).r;
        }
        MaterialSample material = EvaluateSceneMaterial(
            float3(1.0f, 0.0f, 0.0f),
            float4(0.0f, 1.0f, 0.0f, 0.0f),
            g_Material,
            textures);
        clip(material.opacity - g_Material.alphaCutoff);
    }
#endif

    uint2 depthCoordinate = uint2(position.xy);
    if (dirtyPageScatterRaster)
    {
        if (g_Svsm.selectedClipmap >=
                SVSM_SPARSE_CLIPMAP_COUNT ||
            any(position.xy < 0.0f) ||
            any(position.xy >= float(SVSM_ATLAS_SIZE)))
        {
            return;
        }

        uint2 virtualTexel = uint2(position.xy);
        uint2 localPage = virtualTexel / SVSM_PAGE_SIZE;
        uint2 pageTexel = virtualTexel % SVSM_PAGE_SIZE;
        int2 pageTableOffset =
            g_Svsm.pageTableOffsetAndDelta[
                g_Svsm.selectedClipmap].xy;
        uint2 tablePage = uint2(
            WrapPage(int(localPage.x) + pageTableOffset.x),
            WrapPage(int(localPage.y) + pageTableOffset.y));
        uint owner =
            g_Svsm.selectedClipmap * SVSM_PAGES_PER_CLIPMAP +
            tablePage.y * SVSM_PAGES_PER_AXIS +
            tablePage.x;
        uint packed = t_PageTable.Load(int4(
            int2(tablePage),
            int(g_Svsm.selectedClipmap),
            0));
        if ((packed & (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT)) !=
                (SVSM_RESIDENT_BIT | SVSM_DIRTY_BIT))
        {
            return;
        }

        uint physicalPage = packed & SVSM_PHYSICAL_MASK;
        if (physicalPage >= g_Svsm.physicalPageCount)
            return;
        if (t_RenderPages[physicalPage] != owner)
            return;

#if ALPHA_TESTED
        if (scatterAlphaTestEarlyReject)
        {
            MaterialTextureSample textures = DefaultMaterialTextures();
            if ((g_Material.flags &
                    MaterialFlags_UseBaseOrDiffuseTexture) != 0)
            {
                textures.baseOrDiffuse = t_BaseOrDiffuse.SampleGrad(
                    s_MaterialSampler,
                    texCoord,
                    texCoordDdx,
                    texCoordDdy);
            }
            if ((g_Material.flags &
                    MaterialFlags_UseOpacityTexture) != 0)
            {
                textures.opacity = t_Opacity.SampleGrad(
                    s_MaterialSampler,
                    texCoord,
                    texCoordDdx,
                    texCoordDdy).r;
            }
            MaterialSample material = EvaluateSceneMaterial(
                float3(1.0f, 0.0f, 0.0f),
                float4(0.0f, 1.0f, 0.0f, 0.0f),
                g_Material,
                textures);
            clip(material.opacity - g_Material.alphaCutoff);
        }
#endif

        uint2 physicalPageCoordinate = uint2(
            physicalPage % SVSM_PAGES_PER_AXIS,
            physicalPage / SVSM_PAGES_PER_AXIS);
        depthCoordinate =
            physicalPageCoordinate * SVSM_PAGE_SIZE +
            pageTexel;
    }

    uint ignored;
    InterlockedMax(
        u_PhysicalDepth[depthCoordinate],
        asuint(saturate(position.z)),
        ignored);
}
