// MIT License
//
// Copyright (c) 2024 Missing Deadlines (Benjamin Wrensch)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// UVSR adapts Benjamin Wrensch's Minimal AgX Implementation. Its constants in
// turn trace to Troy Sobotka's AgX work. See legal/documentation/agx-display-transform.md.
//
// UVSR AgX display transform. The RGBA16F intermediate stays display-linear
// and undithered so presentation AA classifies exactly the visible tone-mapped
// edge field. Transfer encoding and dithering happen after presentation AA.

#pragma pack_matrix(row_major)

Texture2D<float4> t_SceneColor : register(t0);
#ifndef UVSR_UNITY_EXPOSURE
#define UVSR_UNITY_EXPOSURE 0
#endif

#if !UVSR_UNITY_EXPOSURE
Buffer<float> t_AutoExposure : register(t1);
#endif


#ifndef UVSR_USE_LUT
#define UVSR_USE_LUT 0
#endif
#if UVSR_USE_LUT
Texture3D<float4> t_ColorLut : register(t2);
SamplerState s_LutSampler : register(s0);
#endif
cbuffer c_ToneMapping : register(b0)
{
    float4 g_ExposureContrastSaturationWarmth;
    float4 g_TintSlopePowerLutSize;
    float4 g_LutDomainMin;
    float4 g_LutDomainMax;
};

static const float AGX_MIN_EV = -12.47393;
static const float AGX_MAX_EV = 4.026069;

// AgX inset/outset transforms for a linear Rec.709 working space.
static const float3x3 AGX_INSET = float3x3(
    0.842479062253094,  0.0784335999999992, 0.0792237451477643,
    0.0423282422610123, 0.878468636469772,  0.0791661274605434,
    0.0423756549057051, 0.0784336,            0.879142973793104);

static const float3x3 AGX_OUTSET = float3x3(
     1.19687900512017,   -0.0980208811401368, -0.0990297440797205,
    -0.0528968517574562,  1.15190312990417,   -0.0989611768448433,
    -0.0529716355144438, -0.0980434501171241,  1.15107367264116);

float3 AgxLogEncode(float3 color)
{
    color = mul(AGX_INSET, max(color, 0.0));
    color = log2(max(color, 1e-10));
    return saturate((color - AGX_MIN_EV) / (AGX_MAX_EV - AGX_MIN_EV));
}

float3 AgxDefaultContrast(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5 * x4 * x2
        - 40.14 * x4 * x
        + 31.96 * x4
        - 6.868 * x2 * x
        + 0.4298 * x2
        + 0.1191 * x
        - 0.00232;
}

void main(
    in float4 position : SV_Position,
    in float2 uv : UV,
    out float4 outputColor : SV_Target)
{
    // grading stays before the output gamut transform. transfer and dither remain
    // in display_output_ps.hlsl, after FXAA. neutral controls skip their math.
    float4 sceneSample = t_SceneColor.Load(int3(position.xy, 0));
    float3 color = sceneSample.rgb;
#if !UVSR_UNITY_EXPOSURE
    color *= t_AutoExposure[1];
#endif
    float warmth = g_ExposureContrastSaturationWarmth.w;
    float tint = g_TintSlopePowerLutSize.x;
    if (warmth != 0.0 || tint != 0.0)
        color = max(color * exp2(float3(warmth * 0.30 - tint * 0.05,
            tint * 0.18, -warmth * 0.30 - tint * 0.05)), 0.0);
    if (g_ExposureContrastSaturationWarmth.x != 0.0)
        color *= exp2(g_ExposureContrastSaturationWarmth.x);
    color = AgxDefaultContrast(AgxLogEncode(color));
    if (g_ExposureContrastSaturationWarmth.y != 1.0)
        color = (color - 0.5) * g_ExposureContrastSaturationWarmth.y + 0.5;
    color = saturate(color);
    if (g_TintSlopePowerLutSize.y != 1.0 || g_TintSlopePowerLutSize.z != 1.0)
        color = pow(max(color * g_TintSlopePowerLutSize.y, 0.0), g_TintSlopePowerLutSize.z);
    if (g_ExposureContrastSaturationWarmth.z != 1.0)
    {
        float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
        color = lerp(luminance.xxx, color, g_ExposureContrastSaturationWarmth.z);
    }
    color = saturate(color);
#if UVSR_USE_LUT
    float size = g_TintSlopePowerLutSize.w;
    float3 normalized = saturate((color - g_LutDomainMin.rgb) /
        (g_LutDomainMax.rgb - g_LutDomainMin.rgb));
    float3 uvw = (normalized * (size - 1.0) + 0.5) / size;
    color = t_ColorLut.SampleLevel(s_LutSampler, uvw, 0).rgb;
#endif
    color = saturate(mul(AGX_OUTSET, color));
    outputColor = float4(color, sceneSample.a);
}
