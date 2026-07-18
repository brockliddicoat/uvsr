#pragma pack_matrix(row_major)

#ifndef COMPOSITE
#define COMPOSITE 0
#endif

struct BackdropBlurConstants
{
    float2 reciprocalSourceSize;
    float2 sampleDirection;

    float blurRadius;
    float sigma;
    float2 panelMin;

    float2 panelSize;
    float2 reciprocalWindowSize;

    float cornerRadius;
    float3 padding;
};

cbuffer c_BackdropBlur : register(b0)
{
    BackdropBlurConstants g_BackdropBlur;
};

SamplerState s_LinearClamp : register(s0);
Texture2D<float4> t_Source : register(t0);

float4 SampleGaussianBlur(float2 centerUv)
{
    const float2 sampleStep =
        g_BackdropBlur.reciprocalSourceSize *
        g_BackdropBlur.sampleDirection;
    const float inverseTwoSigmaSquared =
        0.5 / max(
            g_BackdropBlur.sigma * g_BackdropBlur.sigma,
            0.0001);

    float4 result =
        t_Source.SampleLevel(s_LinearClamp, centerUv, 0);
    float totalWeight = 1.0;

    [loop]
    for (int sampleIndex = 1; sampleIndex <= 32; ++sampleIndex)
    {
        if (float(sampleIndex) > g_BackdropBlur.blurRadius)
            break;

        const float sampleOffset = float(sampleIndex);
        const float weight = exp(
            -sampleOffset * sampleOffset * inverseTwoSigmaSquared);
        const float2 uvOffset = sampleStep * sampleOffset;
        result +=
            t_Source.SampleLevel(
                s_LinearClamp,
                centerUv + uvOffset,
                0) * weight;
        result +=
            t_Source.SampleLevel(
                s_LinearClamp,
                centerUv - uvOffset,
                0) * weight;
        totalWeight += 2.0 * weight;
    }

    return result / totalWeight;
}

float RoundedRectangleMask(
    float2 localPosition,
    float2 rectangleSize,
    float cornerRadius)
{
    const float clampedRadius = min(
        cornerRadius,
        min(rectangleSize.x, rectangleSize.y) * 0.5);
    const float2 halfSize = rectangleSize * 0.5;
    const float2 centeredPosition =
        abs(localPosition - halfSize);
    const float2 distanceToCorner =
        centeredPosition - (halfSize - clampedRadius);
    const float signedDistance =
        length(max(distanceToCorner, 0.0)) +
        min(max(distanceToCorner.x, distanceToCorner.y), 0.0) -
        clampedRadius;

    return saturate(0.5 - signedDistance);
}

void main(
    in float4 position : SV_Position,
    in float2 uv : UV,
    out float4 outputColor : SV_Target)
{
#if COMPOSITE
    const float2 windowPosition =
        g_BackdropBlur.panelMin +
        uv * g_BackdropBlur.panelSize;
    const float2 sourceUv =
        windowPosition * g_BackdropBlur.reciprocalWindowSize;
    outputColor = SampleGaussianBlur(sourceUv);
    outputColor.a = RoundedRectangleMask(
        uv * g_BackdropBlur.panelSize,
        g_BackdropBlur.panelSize,
        g_BackdropBlur.cornerRadius);
#else
    outputColor = SampleGaussianBlur(uv);
    outputColor.a = 1.0;
#endif
}
