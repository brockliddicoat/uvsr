#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/utils.hlsli>
#include "diagnostic_cascaded_shadow_map_cb.h"
#include "pbr_gbuffer.hlsli"

#ifndef DIAGNOSTIC_CSM_PRENORMALIZED_RECEIVER_LIGHT_DIRECTION
#define DIAGNOSTIC_CSM_PRENORMALIZED_RECEIVER_LIGHT_DIRECTION 0
#endif

#ifndef DIAGNOSTIC_CSM_PRECOMPOSED_CLIP_TO_SHADOW
#define DIAGNOSTIC_CSM_PRECOMPOSED_CLIP_TO_SHADOW 0
#endif

#if DIAGNOSTIC_CSM_PRECOMPOSED_CLIP_TO_SHADOW
#define DIAGNOSTIC_CSM_RECEIVER_POSITION_TYPE float4
#else
#define DIAGNOSTIC_CSM_RECEIVER_POSITION_TYPE float3
#endif

cbuffer c_DiagnosticCsm : register(b0)
{
    DiagnosticCsmResolveConstants g_Csm;
};

Texture2D<float> t_CameraDepth : register(t0);
Texture2DArray<float> t_ShadowDepth : register(t1);
Texture2D<float4> t_GBufferNormals : register(t2);
SamplerState s_ShadowDepth : register(s0);

VK_IMAGE_FORMAT("r8") RWTexture2D<float> u_Visibility : register(u0);
VK_IMAGE_FORMAT("r8") RWTexture2D<float> u_Debug : register(u1);

static const float2 c_Poisson16[16] = {
    float2(-0.3935238f, 0.7530643f),
    float2(-0.3022015f, 0.2976640f),
    float2(0.09813362f, 0.1924510f),
    float2(-0.7593753f, 0.5187950f),
    float2(0.2293134f, 0.7607011f),
    float2(0.6505286f, 0.6297367f),
    float2(0.5322764f, 0.2350069f),
    float2(0.8581018f, -0.01624052f),
    float2(-0.6928226f, 0.07119545f),
    float2(-0.3114384f, -0.3017288f),
    float2(0.2837671f, -0.1797430f),
    float2(-0.3093514f, -0.7492560f),
    float2(-0.7386893f, -0.5215692f),
    float2(0.3988827f, -0.6170120f),
    float2(0.8114883f, -0.4580260f),
    float2(0.08265103f, -0.8939569f)
};

bool GetCascadeCoordinates(
    uint cascade,
    DIAGNOSTIC_CSM_RECEIVER_POSITION_TYPE receiverPosition,
    out float3 uvz)
{
#if DIAGNOSTIC_CSM_PRECOMPOSED_CLIP_TO_SHADOW
    float4 shadowPosition = mul(
        receiverPosition,
        g_Csm.worldToUvzw[cascade]);
#else
    float4 shadowPosition = mul(
        float4(receiverPosition, 1.0f),
        g_Csm.worldToUvzw[cascade]);
#endif
    if (!(shadowPosition.w != 0.0f) ||
        !all(isfinite(shadowPosition)))
    {
        uvz = 0.0f;
        return false;
    }

    uvz = shadowPosition.xyz / shadowPosition.w;
    return all(isfinite(uvz)) &&
        all(uvz.xy >= 0.0f) &&
        all(uvz.xy <= 1.0f) &&
        uvz.z >= 0.0f && uvz.z <= 1.0f;
}

float4 CalculateUeOcclusion(
    float4 shadowDepth,
    float receiverDepth,
    float transitionScale)
{
    // UE deliberately softens the depth comparison by one transition unit.
    // Besides reducing self-shadowing, this makes the gather implementation
    // independent of comparison-sampler precision and reduction behavior.
    const float constantFactor = receiverDepth * transitionScale - 1.0f;
    return saturate(shadowDepth * transitionScale - constantFactor);
}

float2 HorizontalUePcf5x2(
    float2 fraction,
    float4 values00,
    float4 values20,
    float4 values40)
{
    float result0 = values00.w * (1.0f - fraction.x);
    float result1 = values00.x * (1.0f - fraction.x);
    result0 += values00.z + values20.w + values20.z + values40.w;
    result1 += values00.y + values20.x + values20.y + values40.x;
    result0 += values40.z * fraction.x;
    result1 += values40.y * fraction.x;
    return float2(result0, result1);
}

float UeManual5x5Pcf(
    uint cascade,
    float2 shadowPosition,
    float receiverDepth,
    float transitionScale)
{
    // UE's quality-four conventional CSM kernel reconstructs a linearly
    // positioned 5x5 footprint from a 3x3 grid of Gather4 instructions. This
    // is nine texture operations and 36 scalar depth comparisons, rather than
    // 25 bilinear comparison samples (100 comparisons) in the old UVSR path.
    const float2 texelPosition =
        shadowPosition * float(g_Csm.shadowMapResolution) - 0.5f;
    const float2 fraction = frac(texelPosition);
    const float2 samplePosition =
        (floor(texelPosition) + 1.0f) *
        g_Csm.shadowMapResolutionInv;
    const float3 arrayPosition = float3(samplePosition, cascade);

    const float4 values00 = CalculateUeOcclusion(
        t_ShadowDepth.Gather(s_ShadowDepth, arrayPosition, int2(-2, -2)),
        receiverDepth,
        transitionScale);
    const float4 values20 = CalculateUeOcclusion(
        t_ShadowDepth.Gather(s_ShadowDepth, arrayPosition, int2(0, -2)),
        receiverDepth,
        transitionScale);
    const float4 values40 = CalculateUeOcclusion(
        t_ShadowDepth.Gather(s_ShadowDepth, arrayPosition, int2(2, -2)),
        receiverDepth,
        transitionScale);
    const float2 row0 = HorizontalUePcf5x2(
        fraction, values00, values20, values40);
    float result = row0.x * (1.0f - fraction.y) + row0.y;

    const float4 values02 = CalculateUeOcclusion(
        t_ShadowDepth.Gather(s_ShadowDepth, arrayPosition, int2(-2, 0)),
        receiverDepth,
        transitionScale);
    const float4 values22 = CalculateUeOcclusion(
        t_ShadowDepth.Gather(s_ShadowDepth, arrayPosition, int2(0, 0)),
        receiverDepth,
        transitionScale);
    const float4 values42 = CalculateUeOcclusion(
        t_ShadowDepth.Gather(s_ShadowDepth, arrayPosition, int2(2, 0)),
        receiverDepth,
        transitionScale);
    const float2 row1 = HorizontalUePcf5x2(
        fraction, values02, values22, values42);
    result += row1.x + row1.y;

    const float4 values04 = CalculateUeOcclusion(
        t_ShadowDepth.Gather(s_ShadowDepth, arrayPosition, int2(-2, 2)),
        receiverDepth,
        transitionScale);
    const float4 values24 = CalculateUeOcclusion(
        t_ShadowDepth.Gather(s_ShadowDepth, arrayPosition, int2(0, 2)),
        receiverDepth,
        transitionScale);
    const float4 values44 = CalculateUeOcclusion(
        t_ShadowDepth.Gather(s_ShadowDepth, arrayPosition, int2(2, 2)),
        receiverDepth,
        transitionScale);
    const float2 row2 = HorizontalUePcf5x2(
        fraction, values04, values24, values44);
    result += row2.x + row2.y * fraction.y;

    return 0.04f * result;
}

bool FilterCascade(
    uint cascade,
    float3 uvz,
    uint2 pixel,
    out float visibility)
{
    if (g_Csm.filterMode == 0u)
    {
        const float3 receiverNormal = PbrSafeNormalize(
            t_GBufferNormals[pixel].xyz,
            float3(0.0f, 1.0f, 0.0f));
#if DIAGNOSTIC_CSM_PRENORMALIZED_RECEIVER_LIGHT_DIRECTION
        const float noL = saturate(dot(
            receiverNormal,
            g_Csm.directionToLight));
#else
        // Preserve the exact legacy receiver-light calculation.
        const float noL = saturate(dot(
            receiverNormal,
            normalize(g_Csm.directionToLight)));
#endif
        const float receiverBias = saturate(g_Csm.receiverDepthBias);
        const float receiverTransitionScale =
            g_Csm.cascadeParameters[cascade].z *
                lerp(1.0f - receiverBias, 1.0f, noL);
        const float receiverDepth = min(uvz.z, 0.99999f);
        visibility = UeManual5x5Pcf(
            cascade,
            uvz.xy,
            receiverDepth,
            receiverTransitionScale);
        // UE applies this correction to conventional uniform PCF because the
        // box-filter falloff otherwise appears substantially over-blurred.
        visibility *= visibility;
        return true;
    }

    const uint taps = max(g_Csm.tapCount, 1u);
    const float2 virtualPosition =
        uvz.xy * float(g_Csm.shadowMapResolution);
    const float comparisonDepth = uvz.z;
    visibility = 0.0f;
    [loop]
    for (uint tap = 0u; tap < taps; ++tap)
    {
        const uint poissonIndex = taps == 1u
            ? 0u
            : tap * (16u / taps);
        float2 offset = taps == 1u
            ? 0.0f
            : c_Poisson16[poissonIndex] *
                max(g_Csm.filterRadiusTexels, 0.0f);
        const int2 texel = int2(floor(virtualPosition + offset));
        if (any(texel < 0) ||
            any(texel >= int(g_Csm.shadowMapResolution)))
        {
            visibility = 1.0f;
            return false;
        }
        const float casterDepth =
            t_ShadowDepth.Load(int4(texel, cascade, 0));
        visibility += comparisonDepth <= casterDepth
            ? 1.0f
            : 0.0f;
    }
    visibility /= float(taps);
    return true;
}

bool TrySampleCascade(
    uint cascade,
    DIAGNOSTIC_CSM_RECEIVER_POSITION_TYPE receiverPosition,
    uint2 pixel,
    out float visibility)
{
    float3 uvz;
    if (!GetCascadeCoordinates(cascade, receiverPosition, uvz))
    {
        visibility = 1.0f;
        return false;
    }
    return FilterCascade(cascade, uvz, pixel, visibility);
}

float FadeAlpha(float viewDepth, float offset, float length)
{
    if (!(length > 0.0f))
        return viewDepth <= offset ? 1.0f : 0.0f;
    return 1.0f - saturate((viewDepth - offset) / length);
}

float DistanceFadeAlpha(float viewDepth)
{
    const float fadeLength =
        g_Csm.maximumShadowDistance *
        saturate(g_Csm.distanceFadeoutFraction);
    if (!(fadeLength > 0.0f))
    {
        return viewDepth <= g_Csm.maximumShadowDistance
            ? 1.0f
            : 0.0f;
    }
    const float fadeStart =
        g_Csm.maximumShadowDistance - fadeLength;
    const float fadeProgress = saturate(
        (viewDepth - fadeStart) / fadeLength);
    return 1.0f - fadeProgress * fadeProgress;
}

float ResolveVisibility(
    DIAGNOSTIC_CSM_RECEIVER_POSITION_TYPE receiverPosition,
    float viewDepth,
    uint2 pixel,
    out uint selectedCascade)
{
    selectedCascade = DIAGNOSTIC_CSM_MAX_CASCADES;
    [loop]
    for (uint cascade = 0u;
        cascade < g_Csm.cascadeCount;
        ++cascade)
    {
        const float4 range = g_Csm.cascadeDepthRanges[cascade];
        if (viewDepth < range.x || viewDepth > range.z)
            continue;

        float fineVisibility = 1.0f;
        if (!TrySampleCascade(
                cascade, receiverPosition, pixel, fineVisibility))
        {
            continue;
        }

        selectedCascade = cascade;
        const float fadeLength =
            g_Csm.cascadeParameters[cascade].x;
        const float cascadeAlpha = FadeAlpha(
            viewDepth, range.w, fadeLength);
        float visibility = fineVisibility;

        if (fadeLength > 0.0f && viewDepth > range.w)
        {
            if (cascade + 1u < g_Csm.cascadeCount)
            {
                float coarseVisibility = 1.0f;
                TrySampleCascade(
                    cascade + 1u,
                    receiverPosition,
                    pixel,
                    coarseVisibility);
                visibility = lerp(
                    coarseVisibility,
                    fineVisibility,
                    cascadeAlpha);
            }
            else
            {
                visibility = lerp(
                    1.0f,
                    fineVisibility,
                    cascadeAlpha);
            }
        }

        return lerp(
            1.0f,
            saturate(visibility),
            DistanceFadeAlpha(viewDepth));
    }

    return 1.0f;
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= g_Csm.outputSize))
        return;

    const float cameraDepth = t_CameraDepth[pixel];
    if (!(cameraDepth > 0.0f) || !isfinite(cameraDepth))
    {
        u_Visibility[pixel] = 1.0f;
        if (g_Csm.debugView != 0u)
            u_Debug[pixel] = g_Csm.debugView == 1u ? 1.0f : 0.0f;
        return;
    }

#if DIAGNOSTIC_CSM_PRECOMPOSED_CLIP_TO_SHADOW
    const float4 receiverPosition = ReconstructClipPosition(
        g_Csm.cameraView,
        float2(pixel) + 0.5f,
        cameraDepth);
    const float4 viewPosition = mul(
        receiverPosition,
        g_Csm.cameraView.matClipToView);
    if (!(viewPosition.w != 0.0f) ||
        !all(isfinite(receiverPosition)) ||
        !isfinite(viewPosition.z) ||
        !isfinite(viewPosition.w))
    {
        u_Visibility[pixel] = 1.0f;
        if (g_Csm.debugView != 0u)
            u_Debug[pixel] = g_Csm.debugView == 1u ? 1.0f : 0.0f;
        return;
    }
    const float viewDepth = viewPosition.z / viewPosition.w;
    if (!isfinite(viewDepth) ||
        !(viewDepth > 0.0f))
    {
        u_Visibility[pixel] = 1.0f;
        if (g_Csm.debugView != 0u)
            u_Debug[pixel] = g_Csm.debugView == 1u ? 1.0f : 0.0f;
        return;
    }
#else
    const float3 worldPosition = ReconstructWorldPosition(
        g_Csm.cameraView,
        float2(pixel) + 0.5f,
        cameraDepth);
    const float4 viewPosition = mul(
        float4(worldPosition, 1.0f),
        g_Csm.cameraView.matWorldToView);
    const float viewDepth = viewPosition.z;
    if (!all(isfinite(worldPosition)) ||
        !isfinite(viewDepth) ||
        !(viewDepth > 0.0f))
    {
        u_Visibility[pixel] = 1.0f;
        if (g_Csm.debugView != 0u)
            u_Debug[pixel] = g_Csm.debugView == 1u ? 1.0f : 0.0f;
        return;
    }
#endif

    uint selectedCascade;
#if DIAGNOSTIC_CSM_PRECOMPOSED_CLIP_TO_SHADOW
    const float visibility = ResolveVisibility(
        receiverPosition, viewDepth, pixel, selectedCascade);
#else
    const float visibility = ResolveVisibility(
        worldPosition, viewDepth, pixel, selectedCascade);
#endif
    u_Visibility[pixel] = visibility;

    float debugValue = visibility;
    if (g_Csm.debugView == 2u)
    {
        debugValue = selectedCascade < g_Csm.cascadeCount
            ? float(selectedCascade + 1u) /
                float(g_Csm.cascadeCount)
            : 0.0f;
    }
    else if (g_Csm.debugView == 3u)
    {
        debugValue = selectedCascade < g_Csm.cascadeCount
            ? (g_Csm.cascadeParameters[selectedCascade].y + 1.0f) /
                4.0f
            : 0.0f;
    }
    if (g_Csm.debugView != 0u)
        u_Debug[pixel] = debugValue;
}
