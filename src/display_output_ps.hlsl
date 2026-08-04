// Final UVSR SDR presentation. The source is display-linear RGBA16F after
// tone mapping and presentation AA. Apply the display transfer and stable
// triangular dither only now, so neither becomes presentation-AA edge input.

#pragma pack_matrix(row_major)

Texture2D<float4> t_DisplayLinear : register(t0);

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
    float4 source = t_DisplayLinear.Load(int3(position.xy, 0));
    float3 displayColor = LinearToSrgb(saturate(source.rgb));

    // Dither in encoded space immediately before the 8-bit output target.
    float noise = Hash12(position.xy) - Hash12(position.yx + 19.19);
    displayColor = saturate(displayColor + noise / 255.0);

    // The SRGBA8 swap-chain target performs hardware sRGB encoding, so return
    // to linear transport here to preserve the intended encoded display value.
    outputColor = float4(SrgbToLinear(displayColor), source.a);
}
