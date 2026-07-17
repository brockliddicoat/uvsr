// UVSR AgX display pipeline. The render target is sRGB, so the shader converts
// the dithered display value back to linear as transport compensation; the
// hardware sRGB render-target conversion restores the intended display value.

#pragma pack_matrix(row_major)

Texture2D<float4> t_SceneColor : register(t0);

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

void main(
    in float4 position : SV_Position,
    in float2 uv : UV,
    out float4 outputColor : SV_Target)
{
    // UVSR keeps only the fixed neutral display transform while scene lighting
    // is still developing: scene-linear HDR -> AgX inset -> AgX log -> default
    // contrast tone scale -> output gamut -> sRGB transfer -> dithering.
    float4 sceneSample = t_SceneColor.Load(int3(position.xy, 0));
    float3 color = saturate(AgxDefaultContrast(AgxLogEncode(sceneSample.rgb)));
    color = saturate(mul(AGX_OUTSET, color));
    float3 displayColor = LinearToSrgb(color);

    // Triangular, spatially stable dither in display-encoded space.
    float noise = Hash12(position.xy) - Hash12(position.yx + 19.19);
    displayColor = saturate(displayColor + noise / 255.0);

    // The SRGBA8 render target performs the final hardware sRGB encoding.
    outputColor = float4(SrgbToLinear(displayColor), sceneSample.a);
}
