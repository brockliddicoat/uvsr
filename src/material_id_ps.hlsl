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

#include "renderer_gpu_helpers.hlsli"

cbuffer c_Material : register(b0, space0)
{
    MaterialConstants g_Material;
};

Texture2D t_BaseOrDiffuse : register(t0, space0);
Texture2D t_MetalRoughOrSpecular : register(t1, space0);
Texture2D t_Normal : register(t2, space0);
Texture2D t_Emissive : register(t3, space0);
Texture2D t_Occlusion : register(t4, space0);
Texture2D t_Transmission : register(t5, space0);
Texture2D t_Opacity : register(t6, space0);
SamplerState s_MaterialSampler : register(s0, space2);

ConstantBuffer<GBufferPushConstants> g_Push : register(b1, space1);

void main(
    in float4 position : SV_Position,
    in SceneVertex vertex,
    in uint instance : INSTANCE,
    out uint4 output : SV_Target0)
{
#if ALPHA_TESTED
    const MaterialTextureSample textures = SampleMaterialTexturesAuto(
        g_Material,
        t_BaseOrDiffuse,
        t_MetalRoughOrSpecular,
        t_Normal,
        t_Emissive,
        t_Occlusion,
        t_Transmission,
        t_Opacity,
        s_MaterialSampler,
        vertex.texCoord,
        g_Material.normalTextureTransformScale);
    const MaterialSample surface = EvaluateSceneMaterial(
        vertex.normal,
        vertex.tangent,
        g_Material,
        textures);
    clip(surface.opacity - g_Material.alphaCutoff);
#endif

    output = uint4(
        uint(g_Material.materialID),
        g_Push.startInstanceLocation + instance,
        0u,
        0u);
}
