/*
* Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// UVSR modification and exact provenance:
// Adapted from donut/shaders/passes/gbuffer_vs.hlsl at pinned Donut commit
// bc1ea24b0486f1c00d89327fe16c0b4dd11c5937 for UVSR source base
// 578496047c4753077e36f39a68df8c2c323cee58. The original structured-instance
// and raw-vertex loads and transforms are preserved. UVSR adds the exact
// geometry-local ordinal, checked global handle-map lookup, and noninterpolated
// geometry-instance handle output used by adaptive visibility.

#pragma pack_matrix(row_major)

#include <donut/shaders/bindless.h>
#include <donut/shaders/forward_vertex.hlsli>
#include <donut/shaders/gbuffer_cb.h>
#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/packing.hlsli>
#include "adaptive_msaa_raster_cb.h"

DECLARE_CBUFFER(
    GBufferFillConstants,
    c_GBuffer,
    GBUFFER_BINDING_VIEW_CONSTANTS,
    GBUFFER_SPACE_VIEW);

StructuredBuffer<InstanceData> t_Instances : REGISTER_SRV(
    GBUFFER_BINDING_INSTANCE_BUFFER,
    GBUFFER_SPACE_INPUT);
ByteAddressBuffer t_Vertices : REGISTER_SRV(
    GBUFFER_BINDING_VERTEX_BUFFER,
    GBUFFER_SPACE_INPUT);
StructuredBuffer<uint> t_GeometryInstanceHandleMap : REGISTER_SRV(
    ADAPTIVE_MSAA_BINDING_GEOMETRY_INSTANCE_HANDLE_MAP,
    GBUFFER_SPACE_INPUT);

DECLARE_PUSH_CONSTANTS(
    AdaptiveMsaaGBufferPushConstants,
    g_Push,
    GBUFFER_BINDING_PUSH_CONSTANTS,
    GBUFFER_SPACE_INPUT);

uint LoadGeometryInstanceHandle(InstanceData instance)
{
    if (g_Push.geometryLocalIndex >= instance.numGeometries ||
        instance.firstGeometryInstanceIndex >
            0xffffffffu - g_Push.geometryLocalIndex)
    {
        return ADAPTIVE_MSAA_INVALID_GEOMETRY_INSTANCE_HANDLE;
    }

    const uint mapIndex =
        instance.firstGeometryInstanceIndex + g_Push.geometryLocalIndex;
    uint mapElementCount;
    uint mapStride;
    t_GeometryInstanceHandleMap.GetDimensions(mapElementCount, mapStride);
    if (mapStride != 4u || mapIndex >= mapElementCount)
        return ADAPTIVE_MSAA_INVALID_GEOMETRY_INSTANCE_HANDLE;

    const uint mappedHandle = t_GeometryInstanceHandleMap[mapIndex];
    const uint mappedGeneration =
        (mappedHandle >>
            ADAPTIVE_MSAA_GEOMETRY_INSTANCE_GENERATION_SHIFT) &
        ADAPTIVE_MSAA_GEOMETRY_INSTANCE_GENERATION_MASK;

    // Generation zero is reserved for the uncovered render-target clear and
    // generation all-ones is reserved for explicit invalid/stale mappings.
    // Normalize every reserved encoding so covered mapping failures cannot be
    // mistaken for either background or a live SurfaceKey.
    return mappedGeneration >=
            ADAPTIVE_MSAA_GEOMETRY_INSTANCE_MIN_GENERATION &&
        mappedGeneration <=
            ADAPTIVE_MSAA_GEOMETRY_INSTANCE_MAX_GENERATION
        ? mappedHandle
        : ADAPTIVE_MSAA_INVALID_GEOMETRY_INSTANCE_HANDLE;
}

void main(
    in uint i_vertex : SV_VertexID,
    in uint i_instance : SV_InstanceID,
    out float4 o_position : SV_Position,
    out SceneVertex o_vtx,
    out uint o_instance : INSTANCE,
    nointerpolation out uint o_geometryInstanceHandle
        : GEOMETRY_INSTANCE_HANDLE)
{
    o_instance = i_instance;
    o_position = float4(2.0f, 2.0f, 0.0f, 1.0f);
    o_vtx = (SceneVertex)0;
    o_geometryInstanceHandle =
        ADAPTIVE_MSAA_INVALID_GEOMETRY_INSTANCE_HANDLE;

    if (i_instance > 0xffffffffu - g_Push.startInstanceLocation)
        return;

    i_instance += g_Push.startInstanceLocation;
    i_vertex += g_Push.startVertexLocation;

    uint instanceCount;
    uint instanceStride;
    t_Instances.GetDimensions(instanceCount, instanceStride);
    if (instanceStride != c_SizeOfInstanceData ||
        i_instance >= instanceCount)
    {
        return;
    }

    const InstanceData instance = t_Instances[i_instance];
    o_geometryInstanceHandle = LoadGeometryInstanceHandle(instance);

    float3 pos = asfloat(t_Vertices.Load3(
        g_Push.positionOffset + i_vertex * c_SizeOfPosition));
    float3 prevPos = asfloat(t_Vertices.Load3(
        g_Push.prevPositionOffset + i_vertex * c_SizeOfPosition));
    float2 texCoord = asfloat(t_Vertices.Load2(
        g_Push.texCoordOffset + i_vertex * c_SizeOfTexcoord));
    uint packedNormal = t_Vertices.Load(
        g_Push.normalOffset + i_vertex * c_SizeOfNormal);
    uint packedTangent = t_Vertices.Load(
        g_Push.tangentOffset + i_vertex * c_SizeOfNormal);
    float3 normal = Unpack_RGB8_SNORM(packedNormal);
    float4 tangent = Unpack_RGBA8_SNORM(packedTangent);

    o_vtx.pos = mul(instance.transform, float4(pos, 1.0f)).xyz;
    o_vtx.texCoord = texCoord;
    o_vtx.normal = mul(instance.transform, float4(normal, 0.0f)).xyz;
    o_vtx.tangent.xyz = mul(
        instance.transform,
        float4(tangent.xyz, 0.0f)).xyz;
    o_vtx.tangent.w = tangent.w;
#if MOTION_VECTORS
    o_vtx.prevPos = mul(
        instance.prevTransform,
        float4(prevPos, 1.0f)).xyz;
#else
    o_vtx.prevPos = o_vtx.pos;
#endif

    const float4 worldPos = float4(o_vtx.pos, 1.0f);
    o_position = mul(worldPos, c_GBuffer.view.matWorldToClip);
}
