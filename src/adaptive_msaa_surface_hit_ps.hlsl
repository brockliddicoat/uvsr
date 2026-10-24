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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

// UVSR diagnostic-only companion to adaptive_msaa_gbuffer_ps.hlsl. It reuses
// the UVSR-owned adaptive vertex/draw path after the ordinary pixel-frequency
// G-buffer and exact key raster have completed. Matching the final key makes
// alpha testing, overdraw, and depth ownership authoritative without running
// material texture sampling at sample frequency. Donut remains unmodified.

#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer_cb.h>
#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/forward_vertex.hlsli>

static const uint InvalidVisibilityWord = 0xffffffffu;
static const uint GeometryInstanceGenerationShift = 20u;
static const uint GeometryInstanceGenerationMask = 0xfffu;
static const uint MinimumGeometryInstanceGeneration = 1u;
static const uint MaximumGeometryInstanceGeneration = 0xffeu;

Texture2DMS<uint2> t_FinalSurfaceKeys
    : REGISTER_SRV(13, GBUFFER_SPACE_INPUT);

void main(
    in float4 i_position : SV_Position,
    in SceneVertex i_vtx,
    nointerpolation in uint i_instance : INSTANCE,
    nointerpolation in uint i_geometryInstanceHandle
        : GEOMETRY_INSTANCE_HANDLE,
    in bool i_isFrontFace : SV_IsFrontFace,
    in uint i_primitiveIndex : SV_PrimitiveID,
    in uint i_sampleIndex : SV_SampleIndex,
    sample in float3 i_barycentrics : SV_Barycentrics,
    out float4 o_surfaceHit : SV_Target0)
{
    const uint2 finalKey = t_FinalSurfaceKeys.Load(
        uint2(i_position.xy), i_sampleIndex);
    const uint2 rasterKey = uint2(
        i_geometryInstanceHandle, i_primitiveIndex);
    const uint generation =
        (finalKey.x >> GeometryInstanceGenerationShift) &
        GeometryInstanceGenerationMask;

    // Exact final ownership, rather than coverage or depth alone, selects the
    // diagnostic hit. Invalid keys never claim a reconstructable SurfaceHit.
    if (finalKey.x == InvalidVisibilityWord ||
        finalKey.y == InvalidVisibilityWord ||
        generation < MinimumGeometryInstanceGeneration ||
        generation > MaximumGeometryInstanceGeneration ||
        any(finalKey != rasterKey))
    {
        discard;
    }

    // Canonical SurfaceHit follows native DXR: vertex-1 and vertex-2 weights.
    o_surfaceHit = float4(
        i_barycentrics.yz,
        i_isFrontFace ? 1.0f : 0.0f,
        1.0f);
}
