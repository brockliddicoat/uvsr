// UVSR AgX display pipeline. The render target is sRGB, so the shader converts
// the dithered display value back to linear as transport compensation; the
// hardware sRGB render-target conversion restores the intended display value.

#pragma pack_matrix(row_major)

Texture2D<float4> t_SceneColor : register(t0);
Texture3D<float4> t_KodakLut : register(t1);
Texture2D<float4> t_RtaaMetadata : register(t2);
Texture2D<float4> t_RtaaMoments : register(t3);
SamplerState s_LinearClamp : register(s0);

cbuffer c_AgxToneMapping : register(b0)
{
    float4 g_ExposureContrastSaturationWarmth;
    float4 g_TintLutSizeUseLutDither;
    float4 g_Slope;
    float4 g_Power;
    float4 g_LutDomainMin;
    float4 g_LutDomainMax;
    // xy: inverse native resolution, z: strength, w: halo-range expansion.
    float4 g_RtaaSharpening;
    // x/y/z: motion/reactive/variance suppression, w: enabled.
    float4 g_RtaaSharpeningSuppression;
    // x: diagnostic bypass, y: sharpening-contribution debug.
    float4 g_RtaaDisplayFlags;
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

float3 ApplyRtaaSharpening(int2 pixel, float3 center, out float3 contribution)
{
    contribution = 0.0f;
    if (g_RtaaSharpeningSuppression.w <= 0.5f || g_RtaaSharpening.z <= 0.0f)
        return center;

    uint width, height;
    t_SceneColor.GetDimensions(width, height);
    int2 maximumPixel = int2(width, height) - 1;
    int2 leftPixel = clamp(pixel + int2(-1, 0), 0, maximumPixel);
    int2 rightPixel = clamp(pixel + int2(1, 0), 0, maximumPixel);
    int2 upPixel = clamp(pixel + int2(0, -1), 0, maximumPixel);
    int2 downPixel = clamp(pixel + int2(0, 1), 0, maximumPixel);

    float3 left = max(t_SceneColor.Load(int3(leftPixel, 0)).rgb, 0.0f);
    float3 right = max(t_SceneColor.Load(int3(rightPixel, 0)).rgb, 0.0f);
    float3 up = max(t_SceneColor.Load(int3(upPixel, 0)).rgb, 0.0f);
    float3 down = max(t_SceneColor.Load(int3(downPixel, 0)).rgb, 0.0f);

    float centerLuminance = SceneLuminance(center);
    float4 neighborLuminance = float4(
        SceneLuminance(left), SceneLuminance(right),
        SceneLuminance(up), SceneLuminance(down));
    float averageLuminance = (neighborLuminance.x + neighborLuminance.y +
        neighborLuminance.z + neighborLuminance.w) * 0.25f;
    float minimumLuminance = min(centerLuminance,
        min(min(neighborLuminance.x, neighborLuminance.y),
            min(neighborLuminance.z, neighborLuminance.w)));
    float maximumLuminance = max(centerLuminance,
        max(max(neighborLuminance.x, neighborLuminance.y),
            max(neighborLuminance.z, neighborLuminance.w)));

    // Resolve metadata packs normalized sample count, validation confidence,
    // final reactive value, and motion factor. Moments are log-luminance first
    // and second moments, making the variance suppression robust to HDR peaks.
    float4 metadata = saturate(t_RtaaMetadata.Load(int3(pixel, 0)));
    float2 moments = t_RtaaMoments.Load(int3(pixel, 0)).xy;
    float temporalVariance = sqrt(max(moments.y - moments.x * moments.x, 0.0f));
    float motionSuppression = 1.0f - saturate(
        metadata.a * g_RtaaSharpeningSuppression.x);
    float reactiveSuppression = 1.0f - saturate(
        metadata.b * g_RtaaSharpeningSuppression.y);
    float varianceSuppression = 1.0f - saturate(
        temporalVariance * g_RtaaSharpeningSuppression.z);
    float validationSuppression = smoothstep(0.15f, 0.75f, metadata.g);
    float effectiveStrength = g_RtaaSharpening.z * motionSuppression *
        reactiveSuppression * varianceSuppression * validationSuppression;

    float sharpenedLuminance = centerLuminance +
        (centerLuminance - averageLuminance) * effectiveStrength;
    float luminanceRange = maximumLuminance - minimumLuminance;
    float haloExpansion = max(g_RtaaSharpening.w, 0.0f) * luminanceRange;
    sharpenedLuminance = clamp(sharpenedLuminance,
        max(minimumLuminance - haloExpansion, 0.0f),
        maximumLuminance + haloExpansion);

    float3 sharpened = centerLuminance > 1e-6f
        ? center * (sharpenedLuminance / centerLuminance)
        : center;
    sharpened = max(sharpened, 0.0f);
    contribution = sharpened - center;
    return sharpened;
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
    // scene-linear HDR -> white balance -> exposure -> AgX inset -> AgX log
    // -> contrast tone scale -> Base-space grade/LUT -> output gamut -> sRGB
    // transfer -> dithering.
    int2 pixel = int2(position.xy);
    float4 sceneSample = t_SceneColor.Load(int3(pixel, 0));
    float3 sharpeningContribution;
    float3 sharpenedSceneColor = ApplyRtaaSharpening(
        pixel, max(sceneSample.rgb, 0.0f), sharpeningContribution);

    if (g_RtaaDisplayFlags.y > 0.5f)
    {
        // This developer view is evaluated here because sharpening is fused
        // into the downstream display pass and intentionally never feeds
        // temporal history.
        // LdrColor is SRGBA8_UNORM, so feed it linear values just like the
        // normal path below. Without this conversion a diagnostic value of
        // 0.5 would be hardware-encoded a second time and displayed as 0.735.
        outputColor = float4(SrgbToLinear(
            saturate(abs(sharpeningContribution) * 8.0f)), 1.0f);
        return;
    }

    if (g_RtaaDisplayFlags.x > 0.5f)
    {
        // Analytical debug surfaces are authored in [0,1] display-relative
        // values. Bypassing exposure, AgX and grading keeps classifications
        // readable and prevents a user's grade from changing diagnostics.
        outputColor = float4(SrgbToLinear(saturate(sceneSample.rgb)), 1.0f);
        return;
    }

    float3 color = ApplyCameraWhiteBalance(sharpenedSceneColor, warmth, tint);
    color *= exp2(exposureEv);
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
