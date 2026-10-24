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

// Exact provenance: the visibility-only primitive output and separate
// alpha-tested path are adapted from The Forge VisibilityBufferPass.frag.fsl
// and VisibilityBufferPassAlpha.frag.fsl at commit
// a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0). UVSR deliberately
// replaces PACK_VB_DATA with its exact uint2 SurfaceKey and replaces Forge's
// fixed diffuse-alpha cutoff with the current pinned PBR opacity precedence,
// material domain, anisotropic-wrap sampler, and per-material alphaCutoff.

#pragma pack_matrix(row_major)

#include <donut/shaders/gbuffer_cb.h>
#include "adaptive_msaa_raster_cb.h"

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
#define MATERIAL_SAMPLER_SLOT           GBUFFER_BINDING_MATERIAL_SAMPLER

#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/material_bindings.hlsli>

#ifndef ALPHA_TESTED
#define ALPHA_TESTED 0
#endif

struct CompactVisibilityVertex
{
    precise float4 position : SV_Position;
#if ALPHA_TESTED
    float2 texCoord : TEXCOORD;
#endif
    nointerpolation uint geometryInstanceHandle
        : GEOMETRY_INSTANCE_HANDLE;
};

struct CompactVisibilityOutput
{
    uint2 surfaceKey : SV_Target0;
    uint visibilityClass : SV_Target1;
};

CompactVisibilityOutput main(
    in CompactVisibilityVertex input,
    in uint primitiveIndex : SV_PrimitiveID)
{
#if ALPHA_TESTED
    float opacity = g_Material.opacity;
    if ((g_Material.flags & MaterialFlags_UseOpacityTexture) != 0)
    {
        opacity *= t_Opacity.Sample(
            s_MaterialSampler,
            input.texCoord).r;
    }
    else if ((g_Material.flags &
        MaterialFlags_UseBaseOrDiffuseTexture) != 0)
    {
        opacity *= t_BaseOrDiffuse.Sample(
            s_MaterialSampler,
            input.texCoord).a;
    }
    opacity = saturate(opacity);
    if (g_Material.domain != MaterialDomain_Opaque)
        clip(opacity - g_Material.alphaCutoff);
#endif

    CompactVisibilityOutput output;
    output.surfaceKey = uint2(
        input.geometryInstanceHandle, primitiveIndex);
    output.visibilityClass =
        ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT;
    return output;
}
