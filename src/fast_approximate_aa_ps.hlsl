// Fast Approximate AA for UVSR.
//
// This is an independently expressed HLSL adaptation of Google Filament's
// G3D-patched NVIDIA FXAA 3.11 PC-console path at commit
// 47c86eec22e56d75897e16651eb4d2abd64fc29a:
// filament/src/materials/antiAliasing/fxaa/fxaa.fs
//
// Modified for UVSR's undithered, display-linear RGBA16F output after AgX.
// Perceptual luma is reconstructed for every sample while the original linear
// RGB is filtered; transfer encoding and dithering remain downstream.
// Distributed with third_party/google_filament_fxaa/ATTRIBUTION.md and the
// Apache-2.0 and BSD-2-Clause texts in third_party/licenses/.
//
// G3D Innovation Engine, http://casual-effects.com/g3d
// Copyright 2000-2018, Morgan McGuire. All rights reserved.
// Available under the BSD License.
//
// NVIDIA FXAA 3.11 by Timothy Lottes.
// Copyright (C) 2010, 2011 NVIDIA Corporation. All rights reserved.
// To the maximum extent permitted by applicable law, this software is provided
// as is, and NVIDIA and its suppliers disclaim all warranties, either express
// or implied, including merchantability and fitness for a particular purpose.
// In no event shall NVIDIA or its suppliers be liable for special, incidental,
// indirect, or consequential damages arising from use of this software, even
// if advised of the possibility of such damages.

#pragma pack_matrix(row_major)

Texture2D<float4> t_DisplayLinear : register(t0);
SamplerState s_LinearClamp : register(s0);

cbuffer FastApproximateAaConstants : register(b0)
{
    float2 g_ReciprocalSourceSize;
    float g_EdgeSharpness;
    float g_EdgeThreshold;

    float g_DarkEdgeThreshold;
    float3 g_Padding;
}

float4 SampleDisplayLinear(float2 uv)
{
    return t_DisplayLinear.SampleLevel(s_LinearClamp, uv, 0.0);
}

float PerceptualLuma(float3 displayLinear)
{
    return sqrt(dot(
        saturate(displayLinear),
        float3(0.299, 0.587, 0.114)));
}

void main(
    in float4 position : SV_Position,
    in float2 uv : UV,
    out float4 outputColor : SV_Target)
{
    const float2 halfTexel = 0.5 * g_ReciprocalSourceSize;
    const float2 minimumCorner = uv - halfTexel;
    const float2 maximumCorner = uv + halfTexel;

    const float4 colorNorthWest = SampleDisplayLinear(minimumCorner);
    const float4 colorSouthWest = SampleDisplayLinear(
        float2(minimumCorner.x, maximumCorner.y));
    const float4 colorNorthEast = SampleDisplayLinear(
        float2(maximumCorner.x, minimumCorner.y));
    const float4 colorSouthEast = SampleDisplayLinear(maximumCorner);
    const float4 colorCenter = SampleDisplayLinear(uv);

    const float lumaNorthWest = PerceptualLuma(colorNorthWest.rgb);
    const float lumaSouthWest = PerceptualLuma(colorSouthWest.rgb);
    const float lumaNorthEast = PerceptualLuma(colorNorthEast.rgb);
    const float lumaSouthEast = PerceptualLuma(colorSouthEast.rgb);
    const float lumaCenter = PerceptualLuma(colorCenter.rgb);

    const float cornerMaximum = max(
        max(lumaNorthWest, lumaSouthWest),
        max(lumaNorthEast, lumaSouthEast));
    const float cornerMinimum = min(
        min(lumaNorthWest, lumaSouthWest),
        min(lumaNorthEast, lumaSouthEast));
    const float neighborhoodMaximum = max(cornerMaximum, lumaCenter);
    const float neighborhoodMinimum = min(cornerMinimum, lumaCenter);
    const float lumaRange = neighborhoodMaximum - neighborhoodMinimum;
    const float requiredRange = max(
        g_DarkEdgeThreshold,
        cornerMaximum * g_EdgeThreshold);
    if (lumaRange < requiredRange)
    {
        outputColor = colorCenter;
        return;
    }

    const float southWestMinusNorthEast =
        lumaSouthWest - lumaNorthEast;
    const float southEastMinusNorthWest =
        lumaSouthEast - lumaNorthWest;
    const float2 edgeTangent = float2(
        southWestMinusNorthEast + southEastMinusNorthWest,
        southWestMinusNorthEast - southEastMinusNorthWest);
    const float tangentLength = length(edgeTangent);
    // Match Filament's MEDIUMP_FLT_MIN guard so nearly cancelling gradients
    // cannot normalize into an unstable arbitrary search direction.
    if (tangentLength < 0.00006103515625)
    {
        outputColor = colorCenter;
        return;
    }

    const float2 unitTangent = edgeTangent / tangentLength;
    const float4 colorNearNegative = SampleDisplayLinear(
        uv - unitTangent * g_ReciprocalSourceSize);
    const float4 colorNearPositive = SampleDisplayLinear(
        uv + unitTangent * g_ReciprocalSourceSize);
    const float lumaNearNegative = PerceptualLuma(colorNearNegative.rgb);
    const float lumaNearPositive = PerceptualLuma(colorNearPositive.rgb);

    const float spanDenominator =
        max(abs(unitTangent.x), abs(unitTangent.y)) *
        g_EdgeSharpness * 0.015;
    const float2 farTangent = unitTangent * min(
        lumaRange / spanDenominator,
        3.0);
    const float4 colorFarNegative = SampleDisplayLinear(
        uv - farTangent * (2.0 * g_ReciprocalSourceSize));
    const float4 colorFarPositive = SampleDisplayLinear(
        uv + farTangent * (2.0 * g_ReciprocalSourceSize));
    const float lumaFarNegative = PerceptualLuma(colorFarNegative.rgb);
    const float lumaFarPositive = PerceptualLuma(colorFarPositive.rgb);

    const float4 nearSum = colorNearNegative + colorNearPositive;
    float4 filtered =
        0.25 * (colorFarNegative + colorFarPositive + nearSum);
    const float filteredLuma = 0.25 * (
        lumaFarNegative + lumaFarPositive +
        lumaNearNegative + lumaNearPositive);
    if (filteredLuma < cornerMinimum || filteredLuma > cornerMaximum)
        filtered.rgb = 0.5 * nearSum.rgb;

    // Filament's G3D patch keeps a quarter of the center sample so thin lines
    // survive and the fast console search does not over-blur the image.
    filtered.rgb = lerp(filtered.rgb, colorCenter.rgb, 0.25);
    outputColor = float4(filtered.rgb, colorCenter.a);
}
