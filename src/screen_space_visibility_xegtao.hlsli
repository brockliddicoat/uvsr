///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation
//
// SPDX-License-Identifier: MIT
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// This file is a narrowly adapted, AO-only port of Intel XeGTAO 1.30, pinned
// to archived upstream commit a5b1686c7ea37788eeb3576b5be47f7c03db532c:
// https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.hlsli
// https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.h
// https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/vaGTAO.hlsl
//
// Intel implementation: Filip Strugar and Steve McCalla. XeGTAO is based on
// GTAO/GTSO, "Practical Real-Time Strategies for Accurate Indirect Occlusion"
// by Jimenez et al.
//
// Complete list of intentional UVSR adapter differences from that revision:
// - UVSR supplies existing world-space float normals, transformed to view space,
//   instead of XeGTAO's packed R11G11B10_UNORM view-space normal input;
// - the constant buffer appends a source viewport origin, a reserved reverse-Z
//   field, and a row-major world-to-view matrix after the exact 96-byte upstream
//   GTAOConstants prefix;
// - raw depth can occupy a non-zero source viewport, and prefilter edge lanes
//   clamp to that viewport so odd dimensions never sample a neighbouring view;
// - UVSR pads its hierarchy allocation to 16x16 blocks; depth fetch UVs use
//   that physical extent while view-space reconstruction uses the logical view;
// - working depth is always R16_FLOAT, selecting XeGTAO's FP16 depth bias;
// - working/final AO storage is R16_FLOAT instead of R8_UINT so UVSR's generic
//   visibility consumer can sample a normalized float without a uint decoder;
//   values are explicitly quantized to UNORM8 so AO-only arithmetic matches;
// - bent normals, debug visualizations, and depth-derived normal generation are
//   omitted because UVSR's AO-only integration supplies its own normal buffer;
// - entry points are split across three files and the Hilbert LUT binds at t2;
// - bounds, non-finite-depth, zero-radius, and degenerate-vector guards define
//   safe results where the upstream shader otherwise has undefined arithmetic.
// For finite, perspective-projection inputs, the depth hierarchy filter, High
// preset (3 slices x 3 steps x 2 sides), Hilbert/R2 noise, horizon integral,
// edge packing, and sharp denoiser taps/order follow XeGTAO 1.30.

#ifndef UVSR_SCREEN_SPACE_VISIBILITY_XEGTAO_HLSLI
#define UVSR_SCREEN_SPACE_VISIBILITY_XEGTAO_HLSLI

#define XE_GTAO_DEPTH_MIP_LEVELS 5
#define XE_GTAO_NUMTHREADS_X 8
#define XE_GTAO_NUMTHREADS_Y 8

#ifndef XE_GTAO_USE_HALF_FLOAT_PRECISION
#define XE_GTAO_USE_HALF_FLOAT_PRECISION 1
#endif

#ifndef XE_GTAO_USE_DEFAULT_CONSTANTS
#define XE_GTAO_USE_DEFAULT_CONSTANTS 1
#endif

#ifndef XE_GTAO_USE_HILBERT_LUT
#define XE_GTAO_USE_HILBERT_LUT 1
#endif

#ifndef XE_GTAO_QUALITY
// Intel's High preset: 3 slices * 3 steps * 2 sides = 18 depth taps.
#define XE_GTAO_QUALITY 2
#endif

#ifndef XE_GTAO_DENOISE_FINAL
#define XE_GTAO_DENOISE_FINAL 1
#endif

// Preserve upstream AO-only R8_UINT rounding despite UVSR's R16_FLOAT adapter.
#ifndef XE_GTAO_EMULATE_R8_QUANTIZATION
#define XE_GTAO_EMULATE_R8_QUANTIZATION 1
#endif

#define XE_GTAO_DEFAULT_RADIUS_MULTIPLIER          (1.457f)
#define XE_GTAO_DEFAULT_FALLOFF_RANGE              (0.615f)
#define XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER  (2.0f)
#define XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION (0.0f)
#define XE_GTAO_DEFAULT_FINAL_VALUE_POWER          (2.2f)
#define XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET  (3.30f)
#define XE_GTAO_OCCLUSION_TERM_SCALE               (1.5f)

#define XE_HILBERT_LEVEL 6u
#define XE_HILBERT_WIDTH (1u << XE_HILBERT_LEVEL)

static const float XE_GTAO_PI = 3.1415926535897932384626433832795f;
static const float XE_GTAO_PI_HALF = 1.5707963267948966192313216916398f;
static const float XE_GTAO_FLOAT_EPSILON = 1e-6f;
static const float XE_GTAO_R16F_MAX = 65504.0f;

#if XE_GTAO_USE_HALF_FLOAT_PRECISION != 0
typedef min16float lpfloat;
typedef min16float2 lpfloat2;
typedef min16float3 lpfloat3;
typedef min16float4 lpfloat4;
#else
typedef float lpfloat;
typedef float2 lpfloat2;
typedef float3 lpfloat3;
typedef float4 lpfloat4;
#endif

// The first 96 bytes deliberately preserve Intel's GTAOConstants field order.
// The final 80 bytes are the only UVSR adapter extension. The host must upload
// this exact 176-byte row-major layout to b0 for all three passes.
struct UvsrXeGtaoConstants
{
    int2 ViewportSize;
    float2 ViewportPixelSize;

    float2 DepthUnpackConsts;
    float2 CameraTanHalfFOV;

    float2 NDCToViewMul;
    float2 NDCToViewAdd;

    float2 NDCToViewMul_x_PixelSize;
    float EffectRadius;
    float EffectFalloffRange;

    float RadiusMultiplier;
    float Padding0;
    float FinalValuePower;
    float DenoiseBlurBeta;

    float SampleDistributionPower;
    float ThinOccluderCompensation;
    float DepthMIPSamplingOffset;
    int NoiseIndex;

    uint2 ViewportOrigin;
    uint ReverseDepth;
    uint Padding1;

    float4x4 WorldToView;
};

cbuffer c_XeGTAO : register(b0)
{
    UvsrXeGtaoConstants g_XeGTAO;
};

uint XeGTAO_HilbertIndex(uint posX, uint posY)
{
    uint index = 0u;
    for (uint curLevel = XE_HILBERT_WIDTH / 2u;
         curLevel > 0u; curLevel /= 2u)
    {
        uint regionX = (posX & curLevel) > 0u;
        uint regionY = (posY & curLevel) > 0u;
        index += curLevel * curLevel * ((3u * regionX) ^ regionY);
        if (regionY == 0u)
        {
            if (regionX == 1u)
            {
                posX = (XE_HILBERT_WIDTH - 1u) - posX;
                posY = (XE_HILBERT_WIDTH - 1u) - posY;
            }

            uint temporary = posX;
            posX = posY;
            posY = temporary;
        }
    }
    return index;
}

lpfloat XeGTAO_FastSqrt(float value)
{
    return (lpfloat)asfloat(0x1fbd1df5 + (asint(max(value, 0.0f)) >> 1));
}

// Intel/Lagarde approximation. Negative inputs must remain negative: clamping
// to [0,1] here changes an open horizon into pi/2 and causes over-occlusion.
lpfloat XeGTAO_FastACos(lpfloat inputValue)
{
    const lpfloat pi = (lpfloat)3.141593f;
    const lpfloat halfPi = (lpfloat)1.570796f;
    lpfloat clampedInput = clamp(inputValue, (lpfloat)-1.0f, (lpfloat)1.0f);
    lpfloat x = abs(clampedInput);
    lpfloat result = (lpfloat)-0.156583f * x + halfPi;
    result *= XeGTAO_FastSqrt(max((float)((lpfloat)1.0f - x), 0.0f));
    return clampedInput >= (lpfloat)0.0f ? result : pi - result;
}

bool XeGTAO_SafeNormalize(float3 value, out float3 normalizedValue)
{
    float lengthSquared = dot(value, value);
    if (!(lengthSquared > XE_GTAO_FLOAT_EPSILON * XE_GTAO_FLOAT_EPSILON) ||
        !isfinite(lengthSquared))
    {
        normalizedValue = 0.0f;
        return false;
    }
    normalizedValue = value * rsqrt(lengthSquared);
    return all(isfinite(normalizedValue));
}

lpfloat XeGTAO_RadiusMultiplier(const UvsrXeGtaoConstants constants)
{
#if XE_GTAO_USE_DEFAULT_CONSTANTS != 0
    return (lpfloat)XE_GTAO_DEFAULT_RADIUS_MULTIPLIER;
#else
    return (lpfloat)constants.RadiusMultiplier;
#endif
}

lpfloat XeGTAO_FalloffRatio(const UvsrXeGtaoConstants constants)
{
#if XE_GTAO_USE_DEFAULT_CONSTANTS != 0
    return (lpfloat)XE_GTAO_DEFAULT_FALLOFF_RANGE;
#else
    return (lpfloat)constants.EffectFalloffRange;
#endif
}

lpfloat XeGTAO_SampleDistributionPower(const UvsrXeGtaoConstants constants)
{
#if XE_GTAO_USE_DEFAULT_CONSTANTS != 0
    return (lpfloat)XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER;
#else
    return (lpfloat)constants.SampleDistributionPower;
#endif
}

lpfloat XeGTAO_ThinOccluderCompensation(
    const UvsrXeGtaoConstants constants)
{
#if XE_GTAO_USE_DEFAULT_CONSTANTS != 0
    return (lpfloat)XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION;
#else
    return (lpfloat)constants.ThinOccluderCompensation;
#endif
}

float3 XeGTAO_ComputeViewspacePosition(
    float2 screenPosition,
    float viewspaceDepth,
    const UvsrXeGtaoConstants constants)
{
    float3 result;
    result.xy = (constants.NDCToViewMul * screenPosition +
        constants.NDCToViewAdd) * viewspaceDepth;
    result.z = viewspaceDepth;
    return result;
}

float XeGTAO_ScreenSpaceToViewSpaceDepth(
    float screenDepth,
    const UvsrXeGtaoConstants constants)
{
    if (!isfinite(screenDepth) || screenDepth < 0.0f || screenDepth > 1.0f)
        return XE_GTAO_R16F_MAX;
    float denominator = constants.DepthUnpackConsts.y - screenDepth;
    if (!isfinite(denominator) || abs(denominator) <= XE_GTAO_FLOAT_EPSILON)
        return XE_GTAO_R16F_MAX;

    float viewDepth = constants.DepthUnpackConsts.x / denominator;
    if (!isfinite(viewDepth) || !(viewDepth > 0.0f))
        return XE_GTAO_R16F_MAX;
    return min(viewDepth, XE_GTAO_R16F_MAX);
}

lpfloat XeGTAO_FalloffWeight(
    lpfloat distanceToCenter,
    lpfloat effectRadius,
    lpfloat falloffRangeRatio,
    lpfloat falloffFromRatio)
{
    if (!(effectRadius > (lpfloat)XE_GTAO_FLOAT_EPSILON))
        return distanceToCenter <= (lpfloat)XE_GTAO_FLOAT_EPSILON
            ? (lpfloat)1.0f : (lpfloat)0.0f;

    // Intel's exposed range includes zero although the original expression
    // divides by it. This is the analytic hard-cutoff limit as range -> 0.
    if (!(falloffRangeRatio > (lpfloat)XE_GTAO_FLOAT_EPSILON))
        return distanceToCenter <= effectRadius
            ? (lpfloat)1.0f : (lpfloat)0.0f;

    lpfloat falloffRange = falloffRangeRatio * effectRadius;
    // XeGTAO 1.30 deliberately keeps this value dynamic even in its
    // XE_GTAO_USE_DEFAULT_CONSTANTS specialization.
    lpfloat falloffFrom = effectRadius *
        ((lpfloat)1.0f - falloffFromRatio);
    lpfloat falloffMul = (lpfloat)-1.0f / falloffRange;
    lpfloat falloffAdd = falloffFrom / falloffRange + (lpfloat)1.0f;
    return saturate(distanceToCenter * falloffMul + falloffAdd);
}

lpfloat XeGTAO_DepthMipFilter(
    lpfloat depth0,
    lpfloat depth1,
    lpfloat depth2,
    lpfloat depth3,
    const UvsrXeGtaoConstants constants)
{
    lpfloat maximumDepth = max(max(depth0, depth1), max(depth2, depth3));
    const lpfloat depthRangeScaleFactor = (lpfloat)0.75f;
    lpfloat effectRadius = depthRangeScaleFactor *
        (lpfloat)constants.EffectRadius * XeGTAO_RadiusMultiplier(constants);
    lpfloat falloffRangeRatio = XeGTAO_FalloffRatio(constants);
    lpfloat falloffFromRatio = (lpfloat)constants.EffectFalloffRange;

    lpfloat weight0 = XeGTAO_FalloffWeight(
        maximumDepth - depth0, effectRadius,
        falloffRangeRatio, falloffFromRatio);
    lpfloat weight1 = XeGTAO_FalloffWeight(
        maximumDepth - depth1, effectRadius,
        falloffRangeRatio, falloffFromRatio);
    lpfloat weight2 = XeGTAO_FalloffWeight(
        maximumDepth - depth2, effectRadius,
        falloffRangeRatio, falloffFromRatio);
    lpfloat weight3 = XeGTAO_FalloffWeight(
        maximumDepth - depth3, effectRadius,
        falloffRangeRatio, falloffFromRatio);

    lpfloat weightSum = weight0 + weight1 + weight2 + weight3;
    if (!(weightSum > (lpfloat)XE_GTAO_FLOAT_EPSILON))
        return maximumDepth;
    return (weight0 * depth0 + weight1 * depth1 +
        weight2 * depth2 + weight3 * depth3) / weightSum;
}

lpfloat4 XeGTAO_CalculateEdges(
    lpfloat centerZ,
    lpfloat leftZ,
    lpfloat rightZ,
    lpfloat topZ,
    lpfloat bottomZ)
{
    if (!(centerZ > (lpfloat)XE_GTAO_FLOAT_EPSILON) ||
        !isfinite((float)centerZ))
        return (lpfloat4)0.0f;

    lpfloat4 edgesLeftRightTopBottom =
        lpfloat4(leftZ, rightZ, topZ, bottomZ) - centerZ;
    lpfloat slopeLeftRight =
        (edgesLeftRightTopBottom.y - edgesLeftRightTopBottom.x) * (lpfloat)0.5f;
    lpfloat slopeTopBottom =
        (edgesLeftRightTopBottom.w - edgesLeftRightTopBottom.z) * (lpfloat)0.5f;
    lpfloat4 slopeAdjusted = edgesLeftRightTopBottom + lpfloat4(
        slopeLeftRight, -slopeLeftRight, slopeTopBottom, -slopeTopBottom);
    edgesLeftRightTopBottom = min(
        abs(edgesLeftRightTopBottom), abs(slopeAdjusted));
    return saturate((lpfloat)1.25f - edgesLeftRightTopBottom /
        (centerZ * (lpfloat)0.011f));
}

lpfloat XeGTAO_PackEdges(lpfloat4 edgesLeftRightTopBottom)
{
    edgesLeftRightTopBottom = round(
        saturate(edgesLeftRightTopBottom) * (lpfloat)2.9f);
    return dot(edgesLeftRightTopBottom, lpfloat4(
        64.0f / 255.0f,
        16.0f / 255.0f,
        4.0f / 255.0f,
        1.0f / 255.0f));
}

lpfloat4 XeGTAO_UnpackEdges(lpfloat packedValue)
{
    uint packed = (uint)(packedValue * (lpfloat)255.5f);
    lpfloat4 result;
    result.x = (lpfloat)((packed >> 6u) & 0x03u) / (lpfloat)3.0f;
    result.y = (lpfloat)((packed >> 4u) & 0x03u) / (lpfloat)3.0f;
    result.z = (lpfloat)((packed >> 2u) & 0x03u) / (lpfloat)3.0f;
    result.w = (lpfloat)((packed >> 0u) & 0x03u) / (lpfloat)3.0f;
    return saturate(result);
}

lpfloat XeGTAO_QuantizeAoOnlyTerm(lpfloat value)
{
    value = saturate(value);
#if XE_GTAO_EMULATE_R8_QUANTIZATION != 0
    // Matches XeGTAO's uint(value * 255 + 0.5) R8_UINT write/read path.
    return (lpfloat)((uint)(value * (lpfloat)255.0f +
        (lpfloat)0.5f)) / (lpfloat)255.0f;
#else
    return value;
#endif
}

void XeGTAO_OutputWorkingTerm(
    uint2 pixel,
    lpfloat visibility,
    RWTexture2D<float> outputWorkingAo)
{
    outputWorkingAo[pixel] = (float)XeGTAO_QuantizeAoOnlyTerm(
        visibility / (lpfloat)XE_GTAO_OCCLUSION_TERM_SCALE);
}

void XeGTAO_OutputUnoccluded(
    uint2 pixel,
    RWTexture2D<float> outputWorkingAo,
    RWTexture2D<unorm float> outputWorkingEdges)
{
    outputWorkingAo[pixel] = (float)XeGTAO_QuantizeAoOnlyTerm(
        (lpfloat)(1.0f / XE_GTAO_OCCLUSION_TERM_SCALE));
    outputWorkingEdges[pixel] = 0.0f;
}

lpfloat2 XeGTAO_SpatioTemporalNoise(
    uint2 pixel,
    uint temporalIndex,
    Texture2D<uint> hilbertLut)
{
#if XE_GTAO_USE_HILBERT_LUT != 0
    uint index = hilbertLut.Load(
        int3(pixel % XE_HILBERT_WIDTH, 0)).x;
#else
    // This deliberately mirrors vaGTAO.hlsl. HilbertIndex consumes the low
    // six coordinate bits and is therefore tileable without an explicit mod.
    uint index = XeGTAO_HilbertIndex(pixel.x, pixel.y);
#endif
    index += 288u * (temporalIndex % 64u);
    return (lpfloat2)frac(0.5f + (float)index * float2(
        0.75487766624669276005f,
        0.5698402909980532659114f));
}

bool XeGTAO_LoadViewspaceNormal(
    uint2 viewportPixel,
    const UvsrXeGtaoConstants constants,
    Texture2D<float4> sourceNormals,
    out lpfloat3 viewspaceNormal)
{
    uint sourceWidth;
    uint sourceHeight;
    sourceNormals.GetDimensions(sourceWidth, sourceHeight);
    if (sourceWidth == 0u || sourceHeight == 0u)
    {
        viewspaceNormal = 0.0f;
        return false;
    }

    uint2 sourceCoordinate = min(
        constants.ViewportOrigin + viewportPixel,
        uint2(sourceWidth - 1u, sourceHeight - 1u));
    float3 worldNormal = sourceNormals.Load(int3(sourceCoordinate, 0)).xyz;
    float3 transformed = mul(float4(worldNormal, 0.0f),
        constants.WorldToView).xyz;
    float3 normalized;
    if (!XeGTAO_SafeNormalize(transformed, normalized))
    {
        viewspaceNormal = 0.0f;
        return false;
    }
    viewspaceNormal = (lpfloat3)normalized;
    return true;
}

void XeGTAO_MainPass(
    uint2 pixel,
    lpfloat sliceCount,
    lpfloat stepsPerSlice,
    lpfloat2 localNoise,
    lpfloat3 viewspaceNormal,
    const UvsrXeGtaoConstants constants,
    Texture2D<float> sourceViewspaceDepth,
    SamplerState pointClampSampler,
    RWTexture2D<float> outputWorkingAo,
    RWTexture2D<unorm float> outputWorkingEdges)
{
    float2 normalizedScreenPosition =
        ((float2)pixel + 0.5f) * constants.ViewportPixelSize;

    // Upstream's hierarchy has the exact viewport extent. UVSR pads it so all
    // prefilter lanes can reach group barriers safely; keep texture addressing
    // physical while all GTAO geometry remains in logical viewport space.
    uint2 depthTextureSize =
        (uint2(constants.ViewportSize) + 15u) & ~uint2(15u, 15u);
    float2 depthTexturePixelSize = 1.0f / (float2)depthTextureSize;
    float2 centerDepthTexturePosition =
        ((float2)pixel + 0.5f) * depthTexturePixelSize;

    lpfloat4 valuesUpperLeft = (lpfloat4)sourceViewspaceDepth.GatherRed(
        pointClampSampler,
        (float2)pixel * depthTexturePixelSize);
    lpfloat4 valuesBottomRight = (lpfloat4)sourceViewspaceDepth.GatherRed(
        pointClampSampler,
        (float2)pixel * depthTexturePixelSize,
        int2(1, 1));

    lpfloat viewspaceZ = valuesUpperLeft.y;
    lpfloat pixelLeftZ = valuesUpperLeft.x;
    lpfloat pixelTopZ = valuesUpperLeft.z;
    lpfloat pixelRightZ = valuesBottomRight.z;
    lpfloat pixelBottomZ = valuesBottomRight.x;

    if (!(viewspaceZ > (lpfloat)0.0f) ||
        viewspaceZ >= (lpfloat)(XE_GTAO_R16F_MAX - 1.0f) ||
        !isfinite((float)viewspaceZ))
    {
        XeGTAO_OutputUnoccluded(
            pixel, outputWorkingAo, outputWorkingEdges);
        return;
    }

    lpfloat4 edgesLeftRightTopBottom = XeGTAO_CalculateEdges(
        viewspaceZ, pixelLeftZ, pixelRightZ, pixelTopZ, pixelBottomZ);
    outputWorkingEdges[pixel] = XeGTAO_PackEdges(edgesLeftRightTopBottom);

    // The working hierarchy is always R16_FLOAT in UVSR.
    viewspaceZ *= (lpfloat)0.99920f;
    float3 pixelCenterPosition = XeGTAO_ComputeViewspacePosition(
        normalizedScreenPosition, (float)viewspaceZ, constants);
    float3 viewVectorFloat;
    if (!XeGTAO_SafeNormalize(-pixelCenterPosition, viewVectorFloat))
    {
        XeGTAO_OutputUnoccluded(
            pixel, outputWorkingAo, outputWorkingEdges);
        return;
    }
    lpfloat3 viewVector = (lpfloat3)viewVectorFloat;

    lpfloat effectRadius = (lpfloat)constants.EffectRadius *
        XeGTAO_RadiusMultiplier(constants);
    lpfloat sampleDistributionPower =
        XeGTAO_SampleDistributionPower(constants);
    lpfloat thinOccluderCompensation =
        XeGTAO_ThinOccluderCompensation(constants);
    lpfloat falloffRangeRatio = XeGTAO_FalloffRatio(constants);
    lpfloat falloffFromRatio = (lpfloat)constants.EffectFalloffRange;

    if (!(effectRadius > (lpfloat)XE_GTAO_FLOAT_EPSILON) ||
        !(sliceCount > (lpfloat)0.0f) ||
        !(stepsPerSlice > (lpfloat)0.0f))
    {
        XeGTAO_OutputWorkingTerm(pixel, (lpfloat)1.0f, outputWorkingAo);
        return;
    }

    const lpfloat noiseSlice = localNoise.x;
    const lpfloat noiseSample = localNoise.y;
    const lpfloat pixelTooCloseThreshold = (lpfloat)1.3f;

    float2 pixelDirectionViewspaceSizeAtCenterZ =
        (float)viewspaceZ * constants.NDCToViewMul_x_PixelSize;
    float screenRadiusDenominator = pixelDirectionViewspaceSizeAtCenterZ.x;
    if (!(screenRadiusDenominator > XE_GTAO_FLOAT_EPSILON) ||
        !isfinite(screenRadiusDenominator))
    {
        XeGTAO_OutputWorkingTerm(pixel, (lpfloat)1.0f, outputWorkingAo);
        return;
    }

    lpfloat screenspaceRadius =
        effectRadius / (lpfloat)screenRadiusDenominator;
    if (!(screenspaceRadius > (lpfloat)XE_GTAO_FLOAT_EPSILON) ||
        !isfinite((float)screenspaceRadius))
    {
        XeGTAO_OutputWorkingTerm(pixel, (lpfloat)1.0f, outputWorkingAo);
        return;
    }

    lpfloat visibility = saturate(
        ((lpfloat)10.0f - screenspaceRadius) / (lpfloat)100.0f) *
        (lpfloat)0.5f;
    lpfloat minimumS = pixelTooCloseThreshold / screenspaceRadius;

    // Keep the same loop types and unroll policy as XeGTAO 1.30. The entry
    // point passes compile-time preset constants, allowing DXC to specialize.
    for (lpfloat slice = (lpfloat)0.0f;
         slice < sliceCount; slice += (lpfloat)1.0f)
    {
        lpfloat sliceK = (slice + noiseSlice) / sliceCount;
        lpfloat phi = sliceK * (lpfloat)XE_GTAO_PI;
        lpfloat cosinePhi = cos(phi);
        lpfloat sinePhi = sin(phi);
        lpfloat2 omega = lpfloat2(cosinePhi, -sinePhi) * screenspaceRadius;
        lpfloat3 directionVector = lpfloat3(cosinePhi, sinePhi, 0.0f);
        lpfloat3 orthogonalDirection = directionVector -
            dot(directionVector, viewVector) * viewVector;

        float3 axisFloat;
        if (!XeGTAO_SafeNormalize(
                (float3)cross(orthogonalDirection, viewVector), axisFloat))
        {
            visibility += (lpfloat)1.0f;
            continue;
        }
        lpfloat3 axisVector = (lpfloat3)axisFloat;
        lpfloat3 projectedNormal = viewspaceNormal -
            axisVector * dot(viewspaceNormal, axisVector);
        lpfloat projectedNormalLength = length(projectedNormal);
        if (!(projectedNormalLength > (lpfloat)XE_GTAO_FLOAT_EPSILON) ||
            !isfinite((float)projectedNormalLength))
            continue;

        lpfloat signNormal = (lpfloat)sign(
            dot(orthogonalDirection, projectedNormal));
        lpfloat cosineNormal = saturate(
            dot(projectedNormal, viewVector) / projectedNormalLength);
        lpfloat n = signNormal * XeGTAO_FastACos(cosineNormal);
        lpfloat lowHorizonCosine0 = cos(n + (lpfloat)XE_GTAO_PI_HALF);
        lpfloat lowHorizonCosine1 = cos(n - (lpfloat)XE_GTAO_PI_HALF);
        lpfloat horizonCosine0 = lowHorizonCosine0;
        lpfloat horizonCosine1 = lowHorizonCosine1;

        [unroll]
        for (lpfloat step = (lpfloat)0.0f;
             step < stepsPerSlice; step += (lpfloat)1.0f)
        {
            lpfloat stepBaseNoise =
                (slice + step * stepsPerSlice) *
                (lpfloat)0.6180339887498948482f;
            lpfloat stepNoise = frac(noiseSample + stepBaseNoise);
            lpfloat s = (step + stepNoise) / stepsPerSlice;
            s = pow(s, sampleDistributionPower);
            s += minimumS;

            lpfloat2 sampleOffsetPixels = s * omega;
            lpfloat sampleOffsetLength = length(sampleOffsetPixels);
            if (!(sampleOffsetLength > (lpfloat)XE_GTAO_FLOAT_EPSILON) ||
                !isfinite((float)sampleOffsetLength))
                continue;

            lpfloat mipLevel = (lpfloat)clamp(
                log2((float)sampleOffsetLength) -
                    constants.DepthMIPSamplingOffset,
                0.0f,
                (float)XE_GTAO_DEPTH_MIP_LEVELS);

            lpfloat2 snappedSampleOffsetPixels = round(sampleOffsetPixels);
            lpfloat2 screenSampleOffset = snappedSampleOffsetPixels *
                (lpfloat2)constants.ViewportPixelSize;
            float2 depthSampleOffset = (float2)snappedSampleOffsetPixels *
                depthTexturePixelSize;
            float2 sampleScreenPosition0 =
                normalizedScreenPosition + (float2)screenSampleOffset;
            float2 sampleDepthTexturePosition0 =
                centerDepthTexturePosition + depthSampleOffset;
            float sampleZ0 = sourceViewspaceDepth.SampleLevel(
                pointClampSampler, sampleDepthTexturePosition0,
                (float)mipLevel).x;
            float3 samplePosition0 = XeGTAO_ComputeViewspacePosition(
                sampleScreenPosition0, sampleZ0, constants);

            float2 sampleScreenPosition1 =
                normalizedScreenPosition - (float2)screenSampleOffset;
            float2 sampleDepthTexturePosition1 =
                centerDepthTexturePosition - depthSampleOffset;
            float sampleZ1 = sourceViewspaceDepth.SampleLevel(
                pointClampSampler, sampleDepthTexturePosition1,
                (float)mipLevel).x;
            float3 samplePosition1 = XeGTAO_ComputeViewspacePosition(
                sampleScreenPosition1, sampleZ1, constants);

            float3 sampleDelta0 = samplePosition0 - pixelCenterPosition;
            float3 sampleDelta1 = samplePosition1 - pixelCenterPosition;
            lpfloat sampleDistance0 = (lpfloat)length(sampleDelta0);
            lpfloat sampleDistance1 = (lpfloat)length(sampleDelta1);

            bool validSample0 =
                sampleDistance0 > (lpfloat)XE_GTAO_FLOAT_EPSILON &&
                isfinite((float)sampleDistance0) && all(isfinite(sampleDelta0));
            bool validSample1 =
                sampleDistance1 > (lpfloat)XE_GTAO_FLOAT_EPSILON &&
                isfinite((float)sampleDistance1) && all(isfinite(sampleDelta1));

            lpfloat3 sampleHorizonVector0 = validSample0
                ? (lpfloat3)(sampleDelta0 / (float)sampleDistance0)
                : (lpfloat3)0.0f;
            lpfloat3 sampleHorizonVector1 = validSample1
                ? (lpfloat3)(sampleDelta1 / (float)sampleDistance1)
                : (lpfloat3)0.0f;

#if XE_GTAO_USE_DEFAULT_CONSTANTS != 0
            lpfloat falloffBase0 = sampleDistance0;
            lpfloat falloffBase1 = sampleDistance1;
#else
            lpfloat falloffBase0 = validSample0
                ? length(lpfloat3(
                    (lpfloat)sampleDelta0.x,
                    (lpfloat)sampleDelta0.y,
                    (lpfloat)sampleDelta0.z *
                        ((lpfloat)1.0f + thinOccluderCompensation)))
                : (lpfloat)0.0f;
            lpfloat falloffBase1 = validSample1
                ? length(lpfloat3(
                    (lpfloat)sampleDelta1.x,
                    (lpfloat)sampleDelta1.y,
                    (lpfloat)sampleDelta1.z *
                        ((lpfloat)1.0f + thinOccluderCompensation)))
                : (lpfloat)0.0f;
#endif
            lpfloat weight0 = validSample0
                ? XeGTAO_FalloffWeight(
                    falloffBase0, effectRadius,
                    falloffRangeRatio, falloffFromRatio)
                : (lpfloat)0.0f;
            lpfloat weight1 = validSample1
                ? XeGTAO_FalloffWeight(
                    falloffBase1, effectRadius,
                    falloffRangeRatio, falloffFromRatio)
                : (lpfloat)0.0f;

            lpfloat sampleHorizonCosine0 = validSample0
                ? (lpfloat)dot(sampleHorizonVector0, viewVector)
                : lowHorizonCosine0;
            lpfloat sampleHorizonCosine1 = validSample1
                ? (lpfloat)dot(sampleHorizonVector1, viewVector)
                : lowHorizonCosine1;
            sampleHorizonCosine0 = lerp(
                lowHorizonCosine0, sampleHorizonCosine0, weight0);
            sampleHorizonCosine1 = lerp(
                lowHorizonCosine1, sampleHorizonCosine1, weight1);
            horizonCosine0 = max(horizonCosine0, sampleHorizonCosine0);
            horizonCosine1 = max(horizonCosine1, sampleHorizonCosine1);
        }

        projectedNormalLength = lerp(
            projectedNormalLength, (lpfloat)1.0f, (lpfloat)0.05f);
        lpfloat horizon0 = -XeGTAO_FastACos(
            clamp(horizonCosine1, (lpfloat)-1.0f, (lpfloat)1.0f));
        lpfloat horizon1 = XeGTAO_FastACos(
            clamp(horizonCosine0, (lpfloat)-1.0f, (lpfloat)1.0f));

        lpfloat integralArc0 =
            (cosineNormal + (lpfloat)2.0f * horizon0 * sin(n) -
                cos((lpfloat)2.0f * horizon0 - n)) /
            (lpfloat)4.0f;
        lpfloat integralArc1 =
            (cosineNormal + (lpfloat)2.0f * horizon1 * sin(n) -
                cos((lpfloat)2.0f * horizon1 - n)) /
            (lpfloat)4.0f;
        lpfloat localVisibility = projectedNormalLength *
            (integralArc0 + integralArc1);
        if (isfinite((float)localVisibility))
            visibility += localVisibility;
    }

    visibility /= (lpfloat)sliceCount;
    // Upstream keeps FinalValuePower dynamic in both constant specializations.
    lpfloat finalPower = max((lpfloat)constants.FinalValuePower,
        (lpfloat)XE_GTAO_FLOAT_EPSILON);
    visibility = pow(max(visibility, (lpfloat)0.0f), finalPower);
    if (!isfinite((float)visibility))
        visibility = (lpfloat)1.0f;
    visibility = max((lpfloat)0.03f, visibility);
    XeGTAO_OutputWorkingTerm(pixel, visibility, outputWorkingAo);
}

void XeGTAO_AddSample(
    lpfloat aoValue,
    lpfloat edgeValue,
    inout lpfloat sum,
    inout lpfloat sumWeight)
{
    sum += edgeValue * aoValue;
    sumWeight += edgeValue;
}

void XeGTAO_OutputDenoised(
    uint2 pixel,
    lpfloat outputValue,
    bool finalApply,
    RWTexture2D<float> outputAo)
{
    lpfloat scale = finalApply
        ? (lpfloat)XE_GTAO_OCCLUSION_TERM_SCALE
        : (lpfloat)1.0f;
    outputAo[pixel] = (float)XeGTAO_QuantizeAoOnlyTerm(
        outputValue * scale);
}

void XeGTAO_Denoise(
    uint2 pixelBase,
    const UvsrXeGtaoConstants constants,
    Texture2D<float> sourceAo,
    Texture2D<float> sourceEdges,
    SamplerState pointClampSampler,
    RWTexture2D<float> outputAo,
    bool finalApply)
{
    lpfloat blurAmount = finalApply
        ? (lpfloat)constants.DenoiseBlurBeta
        : (lpfloat)constants.DenoiseBlurBeta / (lpfloat)5.0f;
    blurAmount = max(blurAmount, (lpfloat)XE_GTAO_FLOAT_EPSILON);
    const lpfloat diagonalWeight = (lpfloat)(0.85f * 0.5f);

    lpfloat4 edgeQuad0 = (lpfloat4)sourceEdges.GatherRed(
        pointClampSampler,
        (float2)pixelBase * constants.ViewportPixelSize,
        int2(0, 0));
    lpfloat4 edgeQuad1 = (lpfloat4)sourceEdges.GatherRed(
        pointClampSampler,
        (float2)pixelBase * constants.ViewportPixelSize,
        int2(2, 0));
    lpfloat4 edgeQuad2 = (lpfloat4)sourceEdges.GatherRed(
        pointClampSampler,
        (float2)pixelBase * constants.ViewportPixelSize,
        int2(1, 2));

    lpfloat4 visibilityQuad0 = (lpfloat4)sourceAo.GatherRed(
        pointClampSampler,
        (float2)pixelBase * constants.ViewportPixelSize,
        int2(0, 0));
    lpfloat4 visibilityQuad1 = (lpfloat4)sourceAo.GatherRed(
        pointClampSampler,
        (float2)pixelBase * constants.ViewportPixelSize,
        int2(2, 0));
    lpfloat4 visibilityQuad2 = (lpfloat4)sourceAo.GatherRed(
        pointClampSampler,
        (float2)pixelBase * constants.ViewportPixelSize,
        int2(0, 2));
    lpfloat4 visibilityQuad3 = (lpfloat4)sourceAo.GatherRed(
        pointClampSampler,
        (float2)pixelBase * constants.ViewportPixelSize,
        int2(2, 2));

    [unroll]
    for (uint side = 0u; side < 2u; ++side)
    {
        uint2 pixel = pixelBase + uint2(side, 0u);
        if (pixel.x >= (uint)constants.ViewportSize.x ||
            pixel.y >= (uint)constants.ViewportSize.y)
            continue;

        lpfloat4 edgesLeft = XeGTAO_UnpackEdges(
            side == 0u ? edgeQuad0.x : edgeQuad0.y);
        lpfloat4 edgesTop = XeGTAO_UnpackEdges(
            side == 0u ? edgeQuad0.z : edgeQuad1.w);
        lpfloat4 edgesRight = XeGTAO_UnpackEdges(
            side == 0u ? edgeQuad1.x : edgeQuad1.y);
        lpfloat4 edgesBottom = XeGTAO_UnpackEdges(
            side == 0u ? edgeQuad2.w : edgeQuad2.z);
        lpfloat4 edgesCenter = XeGTAO_UnpackEdges(
            side == 0u ? edgeQuad0.y : edgeQuad1.x);

        edgesCenter *= lpfloat4(
            edgesLeft.y,
            edgesRight.x,
            edgesTop.w,
            edgesBottom.z);

        const lpfloat leakThreshold = (lpfloat)2.5f;
        const lpfloat leakStrength = (lpfloat)0.5f;
        lpfloat edginess =
            saturate((lpfloat)4.0f - leakThreshold -
                dot(edgesCenter, (lpfloat4)1.0f)) /
            ((lpfloat)4.0f - leakThreshold) * leakStrength;
        edgesCenter = saturate(edgesCenter + edginess);

        lpfloat weightTopLeft = diagonalWeight *
            (edgesCenter.x * edgesLeft.z + edgesCenter.z * edgesTop.x);
        lpfloat weightTopRight = diagonalWeight *
            (edgesCenter.z * edgesTop.y + edgesCenter.y * edgesRight.z);
        lpfloat weightBottomLeft = diagonalWeight *
            (edgesCenter.w * edgesBottom.x + edgesCenter.x * edgesLeft.w);
        lpfloat weightBottomRight = diagonalWeight *
            (edgesCenter.y * edgesRight.w + edgesCenter.w * edgesBottom.y);

        lpfloat aoCenter = side == 0u
            ? visibilityQuad0.y : visibilityQuad1.x;
        lpfloat aoLeft = side == 0u
            ? visibilityQuad0.x : visibilityQuad0.y;
        lpfloat aoTop = side == 0u
            ? visibilityQuad0.z : visibilityQuad1.w;
        lpfloat aoRight = side == 0u
            ? visibilityQuad1.x : visibilityQuad1.y;
        lpfloat aoBottom = side == 0u
            ? visibilityQuad2.z : visibilityQuad3.w;
        lpfloat aoTopLeft = side == 0u
            ? visibilityQuad0.w : visibilityQuad0.z;
        lpfloat aoBottomRight = side == 0u
            ? visibilityQuad3.w : visibilityQuad3.z;
        lpfloat aoTopRight = side == 0u
            ? visibilityQuad1.w : visibilityQuad1.z;
        lpfloat aoBottomLeft = side == 0u
            ? visibilityQuad2.w : visibilityQuad2.z;

        lpfloat sumWeight = blurAmount;
        lpfloat sum = aoCenter * sumWeight;
        XeGTAO_AddSample(aoLeft, edgesCenter.x, sum, sumWeight);
        XeGTAO_AddSample(aoRight, edgesCenter.y, sum, sumWeight);
        XeGTAO_AddSample(aoTop, edgesCenter.z, sum, sumWeight);
        XeGTAO_AddSample(aoBottom, edgesCenter.w, sum, sumWeight);
        XeGTAO_AddSample(aoTopLeft, weightTopLeft, sum, sumWeight);
        XeGTAO_AddSample(aoTopRight, weightTopRight, sum, sumWeight);
        XeGTAO_AddSample(aoBottomLeft, weightBottomLeft, sum, sumWeight);
        XeGTAO_AddSample(aoBottomRight, weightBottomRight, sum, sumWeight);

        lpfloat denoised = sum / max(
            sumWeight, (lpfloat)XE_GTAO_FLOAT_EPSILON);
        XeGTAO_OutputDenoised(pixel, denoised, finalApply, outputAo);
    }
}

#endif // UVSR_SCREEN_SPACE_VISIBILITY_XEGTAO_HLSLI
