#pragma pack_matrix(row_major)

struct PixelZoomConstants
{
    uint2 sourceSize;
    uint2 panelMin;

    uint2 panelSize;
    uint zoomFactor;
    float cornerRadius;

    float opacity;
    float outlineWidth;
    float shadowBlur;
    float shadowOpacity;

    float4 outlineTopColor;
    float4 outlineBottomColor;
};

cbuffer c_PixelZoom : register(b0)
{
    PixelZoomConstants g_PixelZoom;
};

Texture2D<float4> t_Source : register(t0);

float RoundedRectangleSignedDistance(
    float2 localPosition,
    float2 rectangleSize,
    float cornerRadius)
{
    const float clampedRadius = min(
        cornerRadius,
        min(rectangleSize.x, rectangleSize.y) * 0.5);
    const float2 halfSize = rectangleSize * 0.5;
    const float2 distanceToCorner =
        abs(localPosition - halfSize) -
        (halfSize - clampedRadius);
    return
        length(max(distanceToCorner, 0.0)) +
        min(max(distanceToCorner.x, distanceToCorner.y), 0.0) -
        clampedRadius;
}

void main(
    in float4 position : SV_Position,
    in float2 uv : UV,
    out float4 outputColor : SV_Target)
{
    const int2 panelPixel =
        int2(floor(position.xy)) -
        int2(g_PixelZoom.panelMin);
    const float2 pixelCenter = float2(panelPixel) + 0.5;
    const float signedDistance = RoundedRectangleSignedDistance(
        pixelCenter,
        float2(g_PixelZoom.panelSize),
        g_PixelZoom.cornerRadius);

    // The rounded silhouette is a binary cutout. Interior pixels are never
    // coverage-filtered, so the magnified image remains an exact texel load.
    const bool insidePanelBounds =
        all(panelPixel >= 0) &&
        all(panelPixel < int2(g_PixelZoom.panelSize));
    if (!insidePanelBounds || signedDistance > 0.0)
    {
        // The shadow is an analytic layer outside the cutout. Its softness
        // never touches or resamples the magnified image.
        const float2 shadowOffset = float2(0.0, 3.0);
        const float shadowDistance = RoundedRectangleSignedDistance(
            pixelCenter - shadowOffset,
            float2(g_PixelZoom.panelSize),
            g_PixelZoom.cornerRadius);
        const float shadowCoverage =
            1.0 - smoothstep(
                -0.5,
                max(g_PixelZoom.shadowBlur, 0.5),
                shadowDistance);
        const float shadowAlpha =
            shadowCoverage *
            g_PixelZoom.shadowOpacity *
            saturate(g_PixelZoom.opacity);
        if (shadowAlpha <= 0.001)
            discard;
        outputColor = float4(0.0, 0.0, 0.0, shadowAlpha);
        return;
    }

    const int factor = max(1, int(g_PixelZoom.zoomFactor));
    const int2 groupOrigin =
        (int2(g_PixelZoom.panelSize) - int2(factor, factor)) / 2;
    const int2 groupDelta = panelPixel - groupOrigin;
    const int2 sourceOffset = int2(floor(
        float2(groupDelta) / float(factor)));
    const int2 sourcePixel = clamp(
        int2(g_PixelZoom.sourceSize / 2u) + sourceOffset,
        int2(0, 0),
        int2(g_PixelZoom.sourceSize) - 1);

    float4 magnifiedPixel =
        t_Source.Load(int3(sourcePixel, 0));

    // Match the Settings panel's one-pixel translucent vertical-gradient
    // outline. This is layered only over the innermost outline band after the
    // exact source load; the magnified interior is not resampled or softened.
    const float gradientPosition =
        g_PixelZoom.panelSize.y > 1u
            ? saturate(
                (pixelCenter.y - 0.5) /
                float(g_PixelZoom.panelSize.y - 1u))
            : 0.0;
    const float4 outlineColor = lerp(
        g_PixelZoom.outlineTopColor,
        g_PixelZoom.outlineBottomColor,
        gradientPosition);
    const float outlineCoverage = saturate(
        signedDistance + g_PixelZoom.outlineWidth);
    magnifiedPixel.rgb = lerp(
        magnifiedPixel.rgb,
        outlineColor.rgb,
        outlineColor.a * outlineCoverage);

    outputColor = float4(
        magnifiedPixel.rgb,
        saturate(g_PixelZoom.opacity));
}
