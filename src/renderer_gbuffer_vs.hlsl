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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma pack_matrix(row_major)

#include "renderer_gpu_helpers.hlsli"

ConstantBuffer<GBufferFillConstants> c_GBuffer : register(b2, space2);
ConstantBuffer<GBufferPushConstants> g_Push : register(b1, space1);
StructuredBuffer<InstanceData> t_Instances : register(t10, space1);
ByteAddressBuffer t_Vertices : register(t11, space1);

void buffer_loads(
    in uint vertexId : SV_VertexID,
    in uint instanceId : SV_InstanceID,
    out float4 position : SV_Position,
    out SceneVertex vertex,
    out uint instance : INSTANCE)
{
    instance = instanceId;
    const uint sourceInstance =
        instanceId + g_Push.startInstanceLocation;
    const uint sourceVertex = vertexId + g_Push.startVertexLocation;
    const InstanceData instanceData = t_Instances[sourceInstance];

    const float3 localPosition = asfloat(t_Vertices.Load3(
        g_Push.positionOffset + sourceVertex * c_SizeOfPosition));
    const float3 localPreviousPosition = asfloat(t_Vertices.Load3(
        g_Push.prevPositionOffset + sourceVertex * c_SizeOfPosition));
    vertex.texCoord = asfloat(t_Vertices.Load2(
        g_Push.texCoordOffset + sourceVertex * c_SizeOfTexcoord));
    const float3 localNormal = Unpack_RGB8_SNORM(t_Vertices.Load(
        g_Push.normalOffset + sourceVertex * c_SizeOfNormal));
    const float4 localTangent = Unpack_RGBA8_SNORM(t_Vertices.Load(
        g_Push.tangentOffset + sourceVertex * c_SizeOfNormal));

    vertex.pos = mul(
        instanceData.transform, float4(localPosition, 1.f));
    vertex.normal = mul(
        instanceData.transform, float4(localNormal, 0.f));
    vertex.tangent.xyz = mul(
        instanceData.transform, float4(localTangent.xyz, 0.f));
    vertex.tangent.w = localTangent.w;
#if MOTION_VECTORS
    vertex.prevPos = mul(
        instanceData.prevTransform,
        float4(localPreviousPosition, 1.f));
#else
    vertex.prevPos = vertex.pos;
#endif

    position = mul(float4(vertex.pos, 1.f), c_GBuffer.view.matWorldToClip);
}
