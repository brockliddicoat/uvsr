/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#ifndef UVSR_SVGF_COMMON_HLSLI
#define UVSR_SVGF_COMMON_HLSLI

#pragma pack_matrix(row_major)
#include "svgf_constants.h"

cbuffer c_Svgf : register(b0) { SvgfConstants g; }
Texture2D<float4> t_Input : register(t0);
Texture2D<float4> t_Surface : register(t1);
Texture2D<float4> t_PreviousSurface : register(t2);
Texture2D<float4> t_Reprojection : register(t3);
Texture2D<float4> t_Feedback : register(t4);
Texture2D<float4> t_Moments : register(t5);
Texture2D<float4> t_Albedo : register(t6);
Texture2D<float4> t_Emission : register(t7);
RWTexture2D<float4> u_Output : register(u0);
RWTexture2D<float4> u_Second : register(u1);

float4 unpackIllumination(float4 value)
{
    return g.scalar != 0 ? float4(value.xxx, value.y) : value;
}
float4 packIllumination(float4 value)
{
    return g.scalar != 0 ? float4(value.x, value.w, 0, 0) : value;
}

bool inside(int2 p) { return all(p >= 0) && all(p < int2(g.extent)); }
float luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float2 signNotZero(float2 v) { return float2(v.x >= 0 ? 1 : -1, v.y >= 0 ? 1 : -1); }
float2 packNormal(float3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    return n.z >= 0 ? n.xy : (1 - abs(n.yx)) * signNotZero(n.xy);
}
float3 unpackNormal(float2 p)
{
    float3 n = float3(p, 1 - abs(p.x) - abs(p.y));
    if (n.z < 0) n.xy = (1 - abs(n.yx)) * signNotZero(n.xy);
    return normalize(n);
}
uint2 surfacePixel(uint2 p)
{
    return min(p * g.sampleStride + g.sampleStride / 2u, g.surfaceExtent - 1u);
}

// adapted from Falcor SVGFCommon.slang. the guide's derivative sets the depth
// scale. it is independent of the noisy illumination and material texture.
float computeWeight(float z, float zp, float phiDepth, float3 n, float3 np,
    float phiNormal, float l, float lp, float phiIllum)
{
    if (z < 0 || zp < 0) return 0;
    float normalWeight = pow(saturate(dot(n, np)), phiNormal);
    float depthWeight = phiDepth == 0 ? 0 : abs(z - zp) / phiDepth;
    float illuminationWeight = abs(l - lp) / max(phiIllum, 1e-10);
    return exp(-max(illuminationWeight, 0) - max(depthWeight, 0)) * normalWeight;
}

#endif
