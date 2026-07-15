// UVSR AgX display pipeline. The render target is sRGB, so the shader converts
// the dithered display value back to linear as transport compensation; the
// hardware sRGB render-target conversion restores the intended display value.

#pragma pack_matrix(row_major)

#include "local_tone_mapping_cb.h"

Texture2D<float4> t_SceneColor : register(t0);
Texture3D<float4> t_KodakLut : register(t1);
Texture3D<float2> t_LocalToneMappingGrid : register(t2);
SamplerState s_LinearClamp : register(s0);

cbuffer c_AgxToneMapping : register(b0)
{
    float4 g_ExposureContrastSaturationWarmth;
    float4 g_TintLutSizeUseLutDither;
    float4 g_Slope;
    float4 g_Power;
    float4 g_LutDomainMin;
    float4 g_LutDomainMax;
    LocalToneMappingDisplayConstants g_LocalToneMapping;
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

float3 ApplyCameraWhiteBalance(float3 color, float warmth, float tint)
{
    // Warmth moves energy between red and blue; tint moves it between green
    // and magenta. Exponential gains stay positive and behave symmetrically.
    float3 gains = exp2(float3(
        warmth * 0.30 - tint * 0.05,
        tint * 0.18,
       -warmth * 0.30 - tint * 0.05));
    return max(color * gains, 0.0);
}

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

float3 ApplyAgxBaseGrade(float3 color, float saturation, float3 slope, float3 power)
{
    color = pow(max(color * max(slope, 0.0), 0.0), max(power, 0.01));
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    return saturate(lerp(luminance.xxx, color, saturation));
}

float3 ApplyKodakLut(float3 color, float size, float3 domainMin, float3 domainMax)
{
    float3 domain = max(domainMax - domainMin, 1e-6);
    float3 normalized = saturate((color - domainMin) / domain);
    float3 uvw = (normalized * (size - 1.0) + 0.5) / size;
    return t_KodakLut.SampleLevel(s_LinearClamp, uvw, 0).rgb;
}

float3 LinearToSrgb(float3 color)
{
    float3 low = color * 12.92;
    float3 high = 1.055 * pow(max(color, 0.0), 1.0 / 2.4) - 0.055;
    return lerp(high, low, color <= 0.0031308);
}

float3 SrgbToLinear(float3 color)
{
    float3 low = color / 12.92;
    float3 high = pow((max(color, 0.0) + 0.055) / 1.055, 2.4);
    return lerp(high, low, color <= 0.04045);
}

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float SceneLuminance(float3 color)
{
    return dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
}

void EvaluateLocalToneMapping(
    int2 pixel,
    float3 sceneColor,
    out float inputLogY,
    out float baseDeltaEV,
    out float confidence,
    out float localEV)
{
    inputLogY = g_LocalToneMapping.rangeStrengthAndMaximumCorrection.x;
    baseDeltaEV = 0.0f;
    confidence = 0.0f;
    localEV = 0.0f;
    if (g_LocalToneMapping.directionDebugAndActive.w <= 0.5f ||
        !all(isfinite(sceneColor)))
    {
        return;
    }

    sceneColor = max(sceneColor, 0.0f);
    inputLogY = clamp(log2(max(SceneLuminance(sceneColor), 1e-6f)),
        g_LocalToneMapping.rangeStrengthAndMaximumCorrection.x,
        g_LocalToneMapping.rangeStrengthAndMaximumCorrection.y);

    const float3 gridDimensions = max(
        g_LocalToneMapping.gridDimensionsAndCellSize.xyz, 1.0f);
    const float cellSize = max(
        g_LocalToneMapping.gridDimensionsAndCellSize.w, 1.0f);
    const float range = max(
        g_LocalToneMapping.rangeStrengthAndMaximumCorrection.y -
        g_LocalToneMapping.rangeStrengthAndMaximumCorrection.x,
        1e-6f);
    const float rangeCoordinate = saturate(
        (inputLogY -
            g_LocalToneMapping.rangeStrengthAndMaximumCorrection.x) / range);

    // Full-resolution scene luminance supplies the range coordinate, so an
    // edge is not constrained to the coarse XY grid. The grid only estimates
    // the broad local exposure field; trilinear slicing reconstructs it at the
    // exact display pixel without a full-resolution intermediate texture.
    float3 gridUv;
    gridUv.xy = (float2(pixel) + 0.5f) /
        (gridDimensions.xy * cellSize);
    gridUv.z = (rangeCoordinate * (gridDimensions.z - 1.0f) + 0.5f) /
        gridDimensions.z;
    gridUv = clamp(gridUv,
        0.5f / gridDimensions,
        1.0f - 0.5f / gridDimensions);

    const float2 gridSample = t_LocalToneMappingGrid.SampleLevel(
        s_LinearClamp, gridUv, 0.0f);
    if (!all(isfinite(gridSample)))
        return;

    baseDeltaEV = clamp(gridSample.x, -range, range);
    confidence = saturate(gridSample.y);
    const float directionalScale = baseDeltaEV >= 0.0f
        ? g_LocalToneMapping.directionDebugAndActive.x
        : g_LocalToneMapping.directionDebugAndActive.y;
    localEV = baseDeltaEV * directionalScale *
        g_LocalToneMapping.rangeStrengthAndMaximumCorrection.z * confidence;
    localEV = clamp(localEV,
        -g_LocalToneMapping.rangeStrengthAndMaximumCorrection.w,
         g_LocalToneMapping.rangeStrengthAndMaximumCorrection.w);
}

float3 VisualizeLocalEV(float value, float scale)
{
    const float amount = saturate(abs(value) / max(scale, 1e-3f));
    const float3 neutral = 0.5f;
    const float3 positive = float3(1.0f, 0.28f, 0.08f);
    const float3 negative = float3(0.08f, 0.32f, 1.0f);
    return lerp(neutral, value >= 0.0f ? positive : negative, amount);
}

void main(
    in float4 position : SV_Position,
    in float2 uv : UV,
    out float4 outputColor : SV_Target)
{
    const float exposureEv = g_ExposureContrastSaturationWarmth.x;
    const float contrast = g_ExposureContrastSaturationWarmth.y;
    const float saturation = g_ExposureContrastSaturationWarmth.z;
    const float warmth = g_ExposureContrastSaturationWarmth.w;
    const float tint = g_TintLutSizeUseLutDither.x;
    const float lutSize = g_TintLutSizeUseLutDither.y;
    const bool useLut = g_TintLutSizeUseLutDither.z > 0.5;
    const float ditherStrength = g_TintLutSizeUseLutDither.w;

    // Required processing order:
    // scene-linear HDR -> white balance -> global plus local EV exposure
    // -> AgX inset -> AgX log
    // -> contrast tone scale -> Base-space grade/LUT -> output gamut -> sRGB
    // transfer -> dithering.
    int2 pixel = int2(position.xy);
    float4 sceneSample = t_SceneColor.Load(int3(pixel, 0));

    float inputLogY;
    float baseDeltaEV;
    float localConfidence;
    float localEV;
    EvaluateLocalToneMapping(
        pixel, sceneSample.rgb,
        inputLogY, baseDeltaEV, localConfidence, localEV);

    const uint localDebugMode =
        (uint)round(g_LocalToneMapping.directionDebugAndActive.z);
    if (g_LocalToneMapping.directionDebugAndActive.w > 0.5f &&
        localDebugMode != LOCAL_TONE_MAPPING_DEBUG_OFF)
    {
        float3 debugColor = 0.0f;
        if (localDebugMode == LOCAL_TONE_MAPPING_DEBUG_INPUT_LOG_LUMINANCE)
        {
            const float range = max(
                g_LocalToneMapping.rangeStrengthAndMaximumCorrection.y -
                g_LocalToneMapping.rangeStrengthAndMaximumCorrection.x,
                1e-6f);
            debugColor = saturate(
                (inputLogY -
                    g_LocalToneMapping.rangeStrengthAndMaximumCorrection.x) /
                range).xxx;
        }
        else if (localDebugMode == LOCAL_TONE_MAPPING_DEBUG_BASE_LOCAL_EV)
        {
            debugColor = VisualizeLocalEV(baseDeltaEV,
                g_LocalToneMapping.rangeStrengthAndMaximumCorrection.w);
        }
        else if (localDebugMode == LOCAL_TONE_MAPPING_DEBUG_FINAL_LOCAL_EV)
        {
            debugColor = VisualizeLocalEV(localEV,
                g_LocalToneMapping.rangeStrengthAndMaximumCorrection.w);
        }
        else if (localDebugMode == LOCAL_TONE_MAPPING_DEBUG_GRID_CONFIDENCE)
        {
            debugColor = localConfidence.xxx;
        }

        // Local diagnostics are display-relative and bypass exposure, AgX,
        // grading, LUT and dithering. Compensate for the SRGBA8 target's
        // hardware encoding exactly as the normal display path does.
        outputColor = float4(SrgbToLinear(saturate(debugColor)), 1.0f);
        return;
    }

    float3 color = ApplyCameraWhiteBalance(sceneSample.rgb, warmth, tint);
    color *= exp2(exposureEv + localEV);
    color = AgxLogEncode(color);
    color = saturate((AgxDefaultContrast(color) - 0.5) * contrast + 0.5);
    color = ApplyAgxBaseGrade(color, saturation, g_Slope.rgb, g_Power.rgb);

    if (useLut)
        color = ApplyKodakLut(color, lutSize, g_LutDomainMin.rgb, g_LutDomainMax.rgb);

    color = saturate(mul(AGX_OUTSET, color));
    float3 displayColor = LinearToSrgb(color);

    // Triangular, spatially stable dither in display-encoded space.
    float noise = Hash12(position.xy) - Hash12(position.yx + 19.19);
    displayColor = saturate(displayColor + noise * (ditherStrength / 255.0));

    // The SRGBA8 render target performs the final hardware sRGB encoding.
    outputColor = float4(SrgbToLinear(displayColor), sceneSample.a);
}
