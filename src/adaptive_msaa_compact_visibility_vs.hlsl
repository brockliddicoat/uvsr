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

/*
 * Copyright (c) 2017-2025 The Forge Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

// Exact provenance: UVSR preserves the pinned Donut buffer-load transform and
// alpha texcoord path. The minimal position-only opaque visibility structure
// and separate alpha payload are adapted from The Forge Visibility_Buffer
// VisibilityBufferPass.vert.fsl and VisibilityBufferPassAlpha.vert.fsl at
// commit a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0). UVSR uses
// its immutable generation-bearing handle map instead of a draw-local key.

#pragma pack_matrix(row_major)

#include <donut/shaders/bindless.h>
#include <donut/shaders/gbuffer_cb.h>
#include <donut/shaders/binding_helpers.hlsli>
#include "adaptive_msaa_raster_cb.h"

#ifndef ALPHA_TESTED
#define ALPHA_TESTED 0
#endif

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

struct CompactVisibilityVertex
{
    precise float4 position : SV_Position;
#if ALPHA_TESTED
    float2 texCoord : TEXCOORD;
#endif
    nointerpolation uint geometryInstanceHandle
        : GEOMETRY_INSTANCE_HANDLE;
};

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

    const uint handle = t_GeometryInstanceHandleMap[mapIndex];
    const uint generation =
        (handle >> ADAPTIVE_MSAA_GEOMETRY_INSTANCE_GENERATION_SHIFT) &
        ADAPTIVE_MSAA_GEOMETRY_INSTANCE_GENERATION_MASK;
    return generation >= ADAPTIVE_MSAA_GEOMETRY_INSTANCE_MIN_GENERATION &&
        generation <= ADAPTIVE_MSAA_GEOMETRY_INSTANCE_MAX_GENERATION
        ? handle
        : ADAPTIVE_MSAA_INVALID_GEOMETRY_INSTANCE_HANDLE;
}

CompactVisibilityVertex main(
    in uint vertexIndex : SV_VertexID,
    in uint instanceIndex : SV_InstanceID)
{
    CompactVisibilityVertex output = (CompactVisibilityVertex)0;
    output.position = float4(2.0f, 2.0f, 0.0f, 1.0f);
    output.geometryInstanceHandle =
        ADAPTIVE_MSAA_INVALID_GEOMETRY_INSTANCE_HANDLE;

    if (instanceIndex > 0xffffffffu - g_Push.startInstanceLocation ||
        vertexIndex > 0xffffffffu - g_Push.startVertexLocation)
    {
        return output;
    }
    instanceIndex += g_Push.startInstanceLocation;
    vertexIndex += g_Push.startVertexLocation;

    uint instanceCount;
    uint instanceStride;
    t_Instances.GetDimensions(instanceCount, instanceStride);
    if (instanceStride != c_SizeOfInstanceData ||
        instanceIndex >= instanceCount)
    {
        return output;
    }

    const InstanceData instance = t_Instances[instanceIndex];
    const uint handle = LoadGeometryInstanceHandle(instance);
    if (handle == ADAPTIVE_MSAA_INVALID_GEOMETRY_INSTANCE_HANDLE)
        return output;

    const float3 objectPosition = asfloat(t_Vertices.Load3(
        g_Push.positionOffset + vertexIndex * c_SizeOfPosition));
#if ALPHA_TESTED
    output.texCoord = asfloat(t_Vertices.Load2(
        g_Push.texCoordOffset + vertexIndex * c_SizeOfTexcoord));
#endif
    precise float3 worldPosition;
    precise float4 clipPosition;
    AdaptiveMsaaTransformPosition(
        objectPosition,
        instance.transform,
        c_GBuffer.view.matWorldToClip,
        worldPosition,
        clipPosition);
    output.position = clipPosition;
    output.geometryInstanceHandle = handle;
    return output;
}
