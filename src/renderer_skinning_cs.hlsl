/*
* Copyright (c) 2021-2024, NVIDIA CORPORATION. All rights reserved.
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

#pragma pack_matrix(row_major)

#include "renderer_skinning_contract.h"
#include "renderer_packing.hlsli"

static const uint c_SizeOfPosition = 12;
static const uint c_SizeOfNormal = 4;
static const uint c_SizeOfTexcoord = 8;
static const uint c_SizeOfJointIndices = 8;
static const uint c_SizeOfJointWeights = 16;


ByteAddressBuffer t_VertexBuffer : register(t0);
ByteAddressBuffer t_JointMatrices : register(t1);

RWByteAddressBuffer u_VertexBuffer : register(u0);

ConstantBuffer<RendererSkinningConstants> g_Const : register(b0);

[numthreads(256, 1, 1)]
void main(in uint i_globalIdx : SV_DispatchThreadID)
{
	if (i_globalIdx >= g_Const.vertexCount)
		return;

	float3 position = asfloat(t_VertexBuffer.Load3(i_globalIdx * c_SizeOfPosition + g_Const.inputPosition));
	float4 normal = 0;
	float4 tangent = 0;
	float2 texCoord1 = 0;
	float2 texCoord2 = 0;

	if (g_Const.flags & RendererSkinNormals)
		normal = Unpack_RGBA8_SNORM(t_VertexBuffer.Load(i_globalIdx * c_SizeOfNormal + g_Const.inputNormal));

	if (g_Const.flags & RendererSkinTangents)
		tangent = Unpack_RGBA8_SNORM(t_VertexBuffer.Load(i_globalIdx * c_SizeOfNormal + g_Const.inputTangent));

	if (g_Const.flags & RendererSkinUV0)
		texCoord1 = asfloat(t_VertexBuffer.Load2(i_globalIdx * c_SizeOfTexcoord + g_Const.inputUV0));

	if (g_Const.flags & RendererSkinUV1)
		texCoord2 = asfloat(t_VertexBuffer.Load2(i_globalIdx * c_SizeOfTexcoord + g_Const.inputUV1));

	uint2 jointIndicesPacked = t_VertexBuffer.Load2(i_globalIdx * c_SizeOfJointIndices + g_Const.inputJoints);
	uint4 jointIndices = uint4(
		jointIndicesPacked.x & 0xffff, jointIndicesPacked.x >> 16,
		jointIndicesPacked.y & 0xffff, jointIndicesPacked.y >> 16);
	float4 jointWeights = asfloat(t_VertexBuffer.Load4(i_globalIdx * c_SizeOfJointWeights + g_Const.inputWeights));

	float4x4 jointMatrix = 0;
	[unroll]
	for (int i = 0; i < 4; i++)
	{
		if (jointWeights[i] > 0)
		{
			uint index = jointIndices[i];
			float4x4 currentMatrix;
			currentMatrix[0] = asfloat(t_JointMatrices.Load4(index * 64 + 0));
			currentMatrix[1] = asfloat(t_JointMatrices.Load4(index * 64 + 16));
			currentMatrix[2] = asfloat(t_JointMatrices.Load4(index * 64 + 32));
			currentMatrix[3] = asfloat(t_JointMatrices.Load4(index * 64 + 48));
			jointMatrix += currentMatrix * jointWeights[i];
		}
	}

	position = mul(float4(position, 1.0), jointMatrix).xyz;
	normal.xyz = normalize(mul(float4(normal.xyz, 0.0), jointMatrix).xyz);
	tangent.xyz = normalize(mul(float4(tangent.xyz, 0.0), jointMatrix).xyz);

	float3 prevPosition;
	if (g_Const.flags & RendererSkinFirstFrame) 
		prevPosition = position;
	else
		prevPosition = asfloat(u_VertexBuffer.Load3(i_globalIdx * c_SizeOfPosition + g_Const.outputPosition));
	u_VertexBuffer.Store3(i_globalIdx * c_SizeOfPosition + g_Const.outputPrevious, asuint(prevPosition));

	u_VertexBuffer.Store3(i_globalIdx * c_SizeOfPosition + g_Const.outputPosition, asuint(position));
	
	if (g_Const.flags & RendererSkinNormals)
		u_VertexBuffer.Store(i_globalIdx * c_SizeOfNormal + g_Const.outputNormal, Pack_RGBA8_SNORM(normal));
	
	if (g_Const.flags & RendererSkinTangents)
		u_VertexBuffer.Store(i_globalIdx * c_SizeOfNormal + g_Const.outputTangent, Pack_RGBA8_SNORM(tangent));
	
	if (g_Const.flags & RendererSkinUV0)
		u_VertexBuffer.Store2(i_globalIdx * c_SizeOfTexcoord + g_Const.outputUV0, asuint(texCoord1));

	if (g_Const.flags & RendererSkinUV1)
		u_VertexBuffer.Store2(i_globalIdx * c_SizeOfTexcoord + g_Const.outputUV1, asuint(texCoord2));
}