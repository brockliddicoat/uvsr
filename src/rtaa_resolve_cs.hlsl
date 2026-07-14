#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "reconstructive_temporal_aa_cb.h"

#ifndef RTAA_CATMULL_ROM
#define RTAA_CATMULL_ROM 1
#endif

#ifndef RTAA_RESURRECTION
#define RTAA_RESURRECTION 1
#endif

cbuffer c_ReconstructiveTemporalAA : register(b0)
{
    ReconstructiveTemporalAAConstants g_Rtaa;
};

SamplerState s_LinearClamp : register(s0);

Texture2D<float4> t_CurrentColor : register(t0);
Texture2D<float>  t_CurrentDepth : register(t1);
Texture2D<float4> t_CurrentNormals : register(t2);
Texture2D<float4> t_CurrentDiffuse : register(t3);
Texture2D<float4> t_CurrentSpecular : register(t4);
Texture2D<float4> t_CurrentEmissive : register(t5);
Texture2D<uint2>  t_CurrentSurfaceIds : register(t6);
Texture2D<float4> t_Prepared : register(t7);
Texture2D<float4> t_Classification : register(t8);
Texture2D<float4> t_RawMotion : register(t9);

Texture2D<float4> t_ImmediateHistoryColor : register(t10);
Texture2D<float2> t_ImmediateHistoryMoments : register(t11);
Texture2D<float4> t_ImmediateHistoryMetadata : register(t12);
Texture2D<float>  t_ImmediateHistoryDepth : register(t13);
Texture2D<uint2>  t_ImmediateHistorySurface : register(t14);

Texture2D<float4> t_PersistentHistoryColor0 : register(t15);
Texture2D<float4> t_PersistentHistoryMetadata0 : register(t16);
Texture2D<float>  t_PersistentHistoryDepth0 : register(t17);
Texture2D<uint2>  t_PersistentHistorySurface0 : register(t18);

Texture2D<float4> t_PersistentHistoryColor1 : register(t19);
Texture2D<float4> t_PersistentHistoryMetadata1 : register(t20);
Texture2D<float>  t_PersistentHistoryDepth1 : register(t21);
Texture2D<uint2>  t_PersistentHistorySurface1 : register(t22);

// History color alpha is the temporally accumulated thin-geometry coverage.
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_HistoryColor : register(u0);
// Mean and second moment of log2(1 + luminance).
VK_IMAGE_FORMAT("rg16f") RWTexture2D<float2> u_HistoryMoments : register(u1);
// Normalized history count, confidence, reactive value, and motion factor.
VK_IMAGE_FORMAT("rgba8") RWTexture2D<float4> u_HistoryMetadata : register(u2);
VK_IMAGE_FORMAT("r32f") RWTexture2D<float> u_HistoryDepth : register(u3);
// R is the exact 32-bit material ID. G packs an octahedral UNORM8x2 normal in
// its low word and a stable 16-bit optional object token in its high word.
VK_IMAGE_FORMAT("rg32ui") RWTexture2D<uint2> u_HistorySurface : register(u4);
// Debug is a side channel. No debug mode is allowed to alter u_History*.
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Debug : register(u5);

static const uint RtaaGroupSize = 8u;
static const uint RtaaTileSize = 12u;
static const uint RtaaTileElementCount = RtaaTileSize * RtaaTileSize;
static const float RtaaEpsilon = 1e-6f;

// A 2-pixel halo supports both the mandatory 3x3 neighborhood and the optional
// radius-2 fallback. Each current-frame neighborhood value is fetched once per
// group and reused for variance clipping, thin diffusion, and spatial fallback.
groupshared float4 s_CurrentColor[RtaaTileElementCount];
// xyz is the normalized current normal, w is positive view depth (0 = invalid).
groupshared float4 s_NormalViewDepth[RtaaTileElementCount];
groupshared uint2 s_SurfaceIds[RtaaTileElementCount];
// Prepare supplies authored candidates. Resolve adds structural evidence for
// the 8x8 output ownership set, then shares it for coherent in-group diffusion.
// Halo entries retain authored evidence without redundant structural work.
groupshared float s_ThinCandidate[RtaaTileElementCount];
groupshared float s_CombinedThinCandidate[RtaaTileElementCount];

bool IsInside(int2 pixel)
{
    return all(pixel >= 0) && all(pixel < int2(g_Rtaa.resolution));
}

bool IsValidDeviceDepth(float depth)
{
    if (!isfinite(depth))
        return false;
    return g_Rtaa.reverseZ != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 SanitizeHdr(float3 color)
{
    return all(isfinite(color)) ? max(color, 0.0f) : 0.0f;
}

float3 SafeNormal(float3 value)
{
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12f && isfinite(lengthSquared)
        ? value * rsqrt(lengthSquared)
        : 0.0f;
}

float SmoothRange(float value, float lower, float upper)
{
    return saturate((value - lower) / max(upper - lower, RtaaEpsilon));
}

bool DeviceDepthToViewDepth(
    float deviceDepth,
    PlanarViewConstants view,
    out float viewDepth)
{
    float denominator = deviceDepth * view.matViewToClip[2][3] -
        view.matViewToClip[2][2];
    if (!isfinite(denominator) || abs(denominator) <= RtaaEpsilon)
    {
        viewDepth = 0.0f;
        return false;
    }

    viewDepth = abs((view.matViewToClip[3][2] -
        deviceDepth * view.matViewToClip[3][3]) / denominator);
    return isfinite(viewDepth);
}

bool ReconstructWorldPosition(
    float2 pixelCenter,
    float deviceDepth,
    PlanarViewConstants view,
    out float3 worldPosition)
{
    float2 clipXY = pixelCenter * view.windowToClipScale +
        view.windowToClipBias;
    float4 world = mul(float4(clipXY, deviceDepth, 1.0f),
        view.matClipToWorld);
    if (!all(isfinite(world)) || abs(world.w) <= RtaaEpsilon)
    {
        worldPosition = 0.0f;
        return false;
    }

    worldPosition = world.xyz / world.w;
    return all(isfinite(worldPosition));
}

bool ProjectWorldToHistory(
    float3 worldPosition,
    PlanarViewConstants view,
    out float2 pixelCenter,
    out float expectedDeviceDepth)
{
    // History color is resolved onto the non-jittered display grid, while its
    // stored depth came from the jittered scene render. Use the no-offset clip
    // position for history coordinates and the offset clip position for the
    // expected depth; this applies projection jitter exactly once.
    float4 clipNoOffset = mul(float4(worldPosition, 1.0f),
        view.matWorldToClipNoOffset);
    float4 clipWithOffset = mul(float4(worldPosition, 1.0f),
        view.matWorldToClip);
    if (!all(isfinite(clipNoOffset)) || !all(isfinite(clipWithOffset)) ||
        clipNoOffset.w <= RtaaEpsilon || clipWithOffset.w <= RtaaEpsilon)
    {
        pixelCenter = 0.0f;
        expectedDeviceDepth = 0.0f;
        return false;
    }

    float2 ndc = clipNoOffset.xy / clipNoOffset.w;
    pixelCenter = ndc * view.clipToWindowScale + view.clipToWindowBias;
    expectedDeviceDepth = clipWithOffset.z / clipWithOffset.w;
    return all(isfinite(pixelCenter)) && isfinite(expectedDeviceDepth);
}

float2 EncodeOctahedralNormal(float3 normal)
{
    normal = SafeNormal(normal);
    if (dot(normal, normal) == 0.0f)
        return 0.5f;
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    if (normal.z < 0.0f)
    {
        float2 signValue = float2(
            normal.x >= 0.0f ? 1.0f : -1.0f,
            normal.y >= 0.0f ? 1.0f : -1.0f);
        normal.xy = (1.0f - abs(normal.yx)) * signValue;
    }
    return normal.xy * 0.5f + 0.5f;
}

float3 DecodeOctahedralNormal(float2 encoded)
{
    float2 value = encoded * 2.0f - 1.0f;
    float3 normal = float3(value, 1.0f - abs(value.x) - abs(value.y));
    float correction = saturate(-normal.z);
    float2 signValue = float2(
        normal.x >= 0.0f ? 1.0f : -1.0f,
        normal.y >= 0.0f ? 1.0f : -1.0f);
    normal.xy -= signValue * correction;
    return SafeNormal(normal);
}

uint HashToWord(uint value)
{
    // Small integer avalanche hash. Material identity remains exact; this hash
    // is used only for optional object validation to keep the packed surface at
    // 64 bits per pixel.
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value & 65535u;
}

uint2 PackHistorySurface(float3 normal, uint materialId, uint objectToken)
{
    uint2 encodedNormal = (uint2)round(
        saturate(EncodeOctahedralNormal(normal)) * 255.0f);
    uint packedNormal = encodedNormal.x | (encodedNormal.y << 8u);
    return uint2(materialId, packedNormal | (objectToken << 16u));
}

void UnpackHistorySurface(
    uint2 packed,
    out float3 normal,
    out uint materialId,
    out uint objectToken)
{
    float2 encodedNormal = float2(
        packed.y & 255u, (packed.y >> 8u) & 255u) / 255.0f;
    normal = DecodeOctahedralNormal(encodedNormal);
    materialId = packed.x;
    objectToken = packed.y >> 16u;
}

float3 RgbToCompressedYCoCg(float3 rgb)
{
    rgb = SanitizeHdr(rgb);
    float y = dot(rgb, float3(0.25f, 0.50f, 0.25f));
    float co = 0.5f * (rgb.r - rgb.b);
    float cg = -0.25f * rgb.r + 0.5f * rgb.g - 0.25f * rgb.b;
    float compression = rcp(1.0f + y);
    return float3(log2(1.0f + y), co * compression, cg * compression);
}

float3 CompressedYCoCgToRgb(float3 value)
{
    float y = max(exp2(value.x) - 1.0f, 0.0f);
    float chromaScale = 1.0f + y;
    float co = value.y * chromaScale;
    float cg = value.z * chromaScale;
    return SanitizeHdr(float3(y + co - cg, y + cg, y - co - cg));
}

float4 SampleHistoryColor(Texture2D<float4> textureObject, float2 pixelCenter)
{
    pixelCenter = clamp(pixelCenter, 0.5f, g_Rtaa.resolution - 0.5f);
#if RTAA_CATMULL_ROM
    // Nine bilinear samples implement the separable 4x4 Catmull-Rom kernel.
    // Negative lobes are retained through filtering, then HDR is sanitized once
    // after sampling so ringing cannot create negative or non-finite radiance.
    float2 texelCenter = floor(pixelCenter - 0.5f) + 0.5f;
    float2 f = saturate(pixelCenter - texelCenter);
    float2 f2 = f * f;
    float2 f3 = f2 * f;
    float2 w0 = f2 - 0.5f * (f3 + f);
    float2 w1 = 1.5f * f3 - 2.5f * f2 + 1.0f;
    float2 w3 = 0.5f * (f3 - f2);
    float2 w2 = 1.0f - w0 - w1 - w3;
    float2 w12 = w1 + w2;

    float2 uv0 = (texelCenter - 1.0f) * g_Rtaa.invResolution;
    float2 uv12 = (texelCenter + w2 / max(w12, RtaaEpsilon)) *
        g_Rtaa.invResolution;
    float2 uv3 = (texelCenter + 2.0f) * g_Rtaa.invResolution;

    float4 result =
        textureObject.SampleLevel(s_LinearClamp, float2(uv0.x, uv0.y), 0) * (w0.x * w0.y) +
        textureObject.SampleLevel(s_LinearClamp, float2(uv0.x, uv12.y), 0) * (w0.x * w12.y) +
        textureObject.SampleLevel(s_LinearClamp, float2(uv0.x, uv3.y), 0) * (w0.x * w3.y) +
        textureObject.SampleLevel(s_LinearClamp, float2(uv12.x, uv0.y), 0) * (w12.x * w0.y) +
        textureObject.SampleLevel(s_LinearClamp, float2(uv12.x, uv12.y), 0) * (w12.x * w12.y) +
        textureObject.SampleLevel(s_LinearClamp, float2(uv12.x, uv3.y), 0) * (w12.x * w3.y) +
        textureObject.SampleLevel(s_LinearClamp, float2(uv3.x, uv0.y), 0) * (w3.x * w0.y) +
        textureObject.SampleLevel(s_LinearClamp, float2(uv3.x, uv12.y), 0) * (w3.x * w12.y) +
        textureObject.SampleLevel(s_LinearClamp, float2(uv3.x, uv3.y), 0) * (w3.x * w3.y);
    result.rgb = SanitizeHdr(result.rgb);
    result.a = isfinite(result.a) ? saturate(result.a) : 0.0f;
    return result;
#else
    float4 result = textureObject.SampleLevel(
        s_LinearClamp, pixelCenter * g_Rtaa.invResolution, 0);
    result.rgb = SanitizeHdr(result.rgb);
    result.a = isfinite(result.a) ? saturate(result.a) : 0.0f;
    return result;
#endif
}

float ComputeDepthConfidence(
    float2 previousPixelCenter,
    float actualDeviceDepth,
    float expectedDeviceDepth,
    float3 currentWorld,
    float3 currentNormal,
    PlanarViewConstants historyView)
{
    if (!IsValidDeviceDepth(actualDeviceDepth) ||
        !IsValidDeviceDepth(expectedDeviceDepth))
    {
        return 0.0f;
    }

    float actualViewDepth;
    float expectedViewDepth;
    if (!DeviceDepthToViewDepth(actualDeviceDepth, historyView,
            actualViewDepth) ||
        !DeviceDepthToViewDepth(expectedDeviceDepth, historyView,
            expectedViewDepth))
    {
        return 0.0f;
    }

    float threshold = max(g_Rtaa.depthThresholdAbsolute +
        g_Rtaa.depthThresholdRelative * max(actualViewDepth, expectedViewDepth),
        RtaaEpsilon);
    // A one-pixel depth quantization error becomes a larger position error on
    // a grazing plane. Permit at most 2x tolerance there, while the independent
    // normal/material gates still prevent this allowance from crossing edges.
    float3 viewDirection = historyView.cameraDirectionOrPosition.w > 0.5f
        ? SafeNormal(historyView.cameraDirectionOrPosition.xyz - currentWorld)
        : SafeNormal(-historyView.cameraDirectionOrPosition.xyz);
    float grazingCosine = abs(dot(SafeNormal(currentNormal), viewDirection));
    float grazingScale = lerp(2.0f, 1.0f,
        SmoothRange(grazingCosine, 0.05f, 0.35f));
    threshold *= grazingScale;
    float viewError = abs(actualViewDepth - expectedViewDepth);
    float viewConfidence = 1.0f - SmoothRange(
        viewError, threshold, threshold * 2.0f);

    // The additional FP32 world-space comparison catches parallax cases where
    // similar device-depth values belong to different surfaces. Both positions
    // are reconstructed in the same historical view, so animated-object motion
    // remains valid when motion.z supplied the expected historical depth.
    float3 actualWorld;
    float3 expectedWorld;
    float worldConfidence = 0.0f;
    if (ReconstructWorldPosition(previousPixelCenter, actualDeviceDepth,
            historyView, actualWorld) &&
        ReconstructWorldPosition(previousPixelCenter, expectedDeviceDepth,
            historyView, expectedWorld))
    {
        float worldError = length(actualWorld - expectedWorld);
        worldConfidence = 1.0f - SmoothRange(
            worldError, threshold, threshold * 2.0f);
    }

    return saturate(min(viewConfidence, worldConfidence));
}

uint TileIndex(int2 tilePixel)
{
    return uint(tilePixel.y) * RtaaTileSize + uint(tilePixel.x);
}

void LoadSharedTile(uint2 groupId, uint linearThreadIndex)
{
    int2 tileOrigin = int2(groupId * RtaaGroupSize) - 2;
    int2 maximumPixel = int2(g_Rtaa.resolution) - 1;
    for (uint tileIndex = linearThreadIndex;
        tileIndex < RtaaTileElementCount;
        tileIndex += RtaaGroupSize * RtaaGroupSize)
    {
        int2 tilePixel = int2(
            tileIndex % RtaaTileSize,
            tileIndex / RtaaTileSize);
        int2 sourcePixel = clamp(tileOrigin + tilePixel, 0, maximumPixel);

        float3 color = SanitizeHdr(
            t_CurrentColor.Load(int3(sourcePixel, 0)).rgb);
        float3 normal = SafeNormal(
            t_CurrentNormals.Load(int3(sourcePixel, 0)).xyz);
        float deviceDepth = t_CurrentDepth.Load(int3(sourcePixel, 0));
        float viewDepth = 0.0f;
        if (!IsValidDeviceDepth(deviceDepth) ||
            !DeviceDepthToViewDepth(deviceDepth, g_Rtaa.currentView, viewDepth))
        {
            viewDepth = 0.0f;
        }

        // Alpha carries the exact center device depth so Resolve can persist
        // center geometry without a redundant full-resolution depth load.
        s_CurrentColor[tileIndex] = float4(color, deviceDepth);
        s_NormalViewDepth[tileIndex] = float4(normal, viewDepth);
        s_SurfaceIds[tileIndex] = t_CurrentSurfaceIds.Load(
            int3(sourcePixel, 0));
        float authoredThinCandidate = saturate(
            t_Classification.Load(int3(sourcePixel, 0)).a);
        s_ThinCandidate[tileIndex] = authoredThinCandidate;
        s_CombinedThinCandidate[tileIndex] = authoredThinCandidate;
    }
}

void ComputeNeighborhoodStatistics(
    int2 tileCenter,
    out float3 mean,
    out float3 variance,
    out float3 lowerSample,
    out float3 upperSample,
    out float3 rgbMean)
{
    mean = 0.0f;
    float3 secondMoment = 0.0f;
    lowerSample = float3(1e20f, 1e20f, 1e20f);
    upperSample = float3(-1e20f, -1e20f, -1e20f);
    rgbMean = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float3 rgb = s_CurrentColor[TileIndex(
                tileCenter + int2(x, y))].rgb;
            float3 value = RgbToCompressedYCoCg(rgb);
            mean += value;
            secondMoment += value * value;
            lowerSample = min(lowerSample, value);
            upperSample = max(upperSample, value);
            rgbMean += rgb;
        }
    }

    mean *= 1.0f / 9.0f;
    secondMoment *= 1.0f / 9.0f;
    variance = max(secondMoment - mean * mean, 0.0f);
    rgbMean *= 1.0f / 9.0f;
}

float3 ClipLineToBox(
    float3 history,
    float3 center,
    float3 lowerBound,
    float3 upperBound)
{
    // Intersect the line from the neighborhood center to history with the
    // variance box. Unlike independent channel clamps, this preserves the
    // history chroma direction and avoids hue shifts at saturated edges.
    float3 delta = history - center;
    float scale = 1.0f;
    [unroll]
    for (uint component = 0u; component < 3u; ++component)
    {
        if (delta[component] > RtaaEpsilon)
        {
            scale = min(scale, (upperBound[component] - center[component]) /
                delta[component]);
        }
        else if (delta[component] < -RtaaEpsilon)
        {
            scale = min(scale, (lowerBound[component] - center[component]) /
                delta[component]);
        }
    }
    return center + delta * saturate(scale);
}

float ComputeStructuralThinCandidate(
    int2 tileCenter,
    float centerViewDepth,
    float3 centerNormal,
    uint2 centerSurface)
{
    if (g_Rtaa.enableThinGeometry == 0u || centerViewDepth <= 0.0f)
        return 0.0f;

    static const int2 NeighborOffsets[8] = {
        int2(-1, -1), int2(0, -1), int2(1, -1),
        int2(-1,  0),              int2(1,  0),
        int2(-1,  1), int2(0,  1), int2(1,  1)
    };

    float centerLogLuma = log2(1.0f + Luminance(
        s_CurrentColor[TileIndex(tileCenter)].rgb));
    float edgeSignals[8];
    float gateSignals[8];
    [unroll]
    for (uint neighborIndex = 0u; neighborIndex < 8u; ++neighborIndex)
    {
        uint index = TileIndex(tileCenter + NeighborOffsets[neighborIndex]);
        float4 neighborNormalDepth = s_NormalViewDepth[index];
        bool neighborDepthValid = neighborNormalDepth.w > 0.0f;
        float depthSignal = neighborDepthValid
            ? saturate(abs(neighborNormalDepth.w - centerViewDepth) /
                max(centerViewDepth, 0.1f) /
                max(g_Rtaa.thinDepthThreshold, RtaaEpsilon))
            : 1.0f;
        float normalSignal = neighborDepthValid &&
            dot(centerNormal, centerNormal) > 0.0f &&
            dot(neighborNormalDepth.xyz, neighborNormalDepth.xyz) > 0.0f
            ? saturate(1.0f - abs(dot(centerNormal,
                neighborNormalDepth.xyz)))
            : 1.0f;
        float neighborLogLuma = log2(1.0f + Luminance(
            s_CurrentColor[index].rgb));
        float contrastSignal = saturate(abs(neighborLogLuma -
            centerLogLuma) / max(g_Rtaa.thinContrastThreshold, RtaaEpsilon));
        uint2 neighborSurface = s_SurfaceIds[index];
        float materialSignal = g_Rtaa.enableMaterialValidation != 0u &&
            neighborSurface.x != centerSurface.x ? 1.0f : 0.0f;
        float objectSignal = g_Rtaa.enableObjectValidation != 0u &&
            neighborSurface.y != centerSurface.y ? 1.0f : 0.0f;
        float authoredSignal = max(
            s_ThinCandidate[TileIndex(tileCenter)], s_ThinCandidate[index]);

        // A lone depth edge is a normal polygon silhouette. Thin structure is
        // accepted only when two opposing edges exist and at least one current
        // color/normal/identity/authored signal corroborates them.
        edgeSignals[neighborIndex] = max(depthSignal, normalSignal);
        gateSignals[neighborIndex] = max(max(contrastSignal, normalSignal),
            max(max(materialSignal, objectSignal), authoredSignal));
    }

    float opposingEdges = 0.0f;
    opposingEdges = max(opposingEdges, min(edgeSignals[3], edgeSignals[4]));
    opposingEdges = max(opposingEdges, min(edgeSignals[1], edgeSignals[6]));
    opposingEdges = max(opposingEdges, min(edgeSignals[0], edgeSignals[7]));
    opposingEdges = max(opposingEdges, min(edgeSignals[2], edgeSignals[5]));

    float corroboration = 0.0f;
    [unroll]
    for (uint signalIndex = 0u; signalIndex < 8u; ++signalIndex)
        corroboration = max(corroboration, gateSignals[signalIndex]);

    return saturate(opposingEdges * corroboration);
}

void BuildSharedThinCandidates(uint2 threadId)
{
    // One lane owns one output candidate. Computing the 36 halo candidates as
    // well would add 56% more structural evaluations merely to diffuse across
    // an 8-pixel group boundary; authored halo evidence remains available.
    int2 tilePixel = int2(threadId) + 2;
    uint tileIndex = TileIndex(tilePixel);
    float4 normalDepth = s_NormalViewDepth[tileIndex];
    float structural = ComputeStructuralThinCandidate(
        tilePixel, normalDepth.w, normalDepth.xyz,
        s_SurfaceIds[tileIndex]);
    s_CombinedThinCandidate[tileIndex] = max(
        s_ThinCandidate[tileIndex], structural);
}

float DiffuseThinCandidate(
    int2 tileCenter,
    float centerViewDepth,
    float3 centerNormal,
    uint2 centerSurface,
    float centerCandidate)
{
    float result = centerCandidate;
    if (g_Rtaa.enableThinGeometry == 0u ||
        g_Rtaa.enableThinDiffusion == 0u || centerViewDepth <= 0.0f)
    {
        return g_Rtaa.enableThinGeometry != 0u ? result : 0.0f;
    }

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
                continue;

            uint index = TileIndex(tileCenter + int2(x, y));
            float candidate = s_CombinedThinCandidate[index];
            float4 neighborNormalDepth = s_NormalViewDepth[index];
            if (candidate <= 0.0f || neighborNormalDepth.w <= 0.0f)
                continue;

            float relativeDepthDifference = abs(neighborNormalDepth.w -
                centerViewDepth) / max(centerViewDepth, 0.1f);
            float depthGate = 1.0f - saturate(relativeDepthDifference /
                max(g_Rtaa.thinDepthThreshold, RtaaEpsilon));
            float normalGate = SmoothRange(dot(centerNormal,
                neighborNormalDepth.xyz), g_Rtaa.normalRejectCosine,
                g_Rtaa.normalAcceptCosine);
            uint2 neighborSurface = s_SurfaceIds[index];
            float materialGate = g_Rtaa.enableMaterialValidation == 0u ||
                neighborSurface.x == centerSurface.x ? 1.0f : 0.0f;
            float objectGate = g_Rtaa.enableObjectValidation == 0u ||
                neighborSurface.y == centerSurface.y ? 1.0f : 0.0f;
            float distanceGate = (abs(x) + abs(y)) == 1 ? 0.85f : 0.70f;
            result = max(result, candidate * depthGate * normalGate *
                materialGate * objectGate * distanceGate);
        }
    }
    return saturate(result);
}

void ComputeSharedEdgeDirection(
    int2 tileCenter,
    float centerViewDepth,
    float3 centerNormal,
    uint2 centerSurface,
    out float2 edgeDirection,
    out float edgeStrength)
{
    static const int2 AxisOffsets[4] = {
        int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };

    float centerLogLuma = log2(1.0f + Luminance(
        s_CurrentColor[TileIndex(tileCenter)].rgb));
    float sideSignals[4];
    [unroll]
    for (uint side = 0u; side < 4u; ++side)
    {
        uint index = TileIndex(tileCenter + AxisOffsets[side]);
        float4 neighborNormalDepth = s_NormalViewDepth[index];
        float depthEdge = neighborNormalDepth.w > 0.0f &&
            centerViewDepth > 0.0f
            ? abs(neighborNormalDepth.w - centerViewDepth) /
                max(centerViewDepth, 0.1f)
            : 1.0f;
        float lumaEdge = abs(log2(1.0f + Luminance(
            s_CurrentColor[index].rgb)) - centerLogLuma);
        float normalEdge = neighborNormalDepth.w > 0.0f
            ? 1.0f - saturate(dot(centerNormal, neighborNormalDepth.xyz))
            : 1.0f;
        uint2 neighborSurface = s_SurfaceIds[index];
        float identityEdge =
            (g_Rtaa.enableMaterialValidation != 0u &&
                neighborSurface.x != centerSurface.x) ||
            (g_Rtaa.enableObjectValidation != 0u &&
                neighborSurface.y != centerSurface.y) ? 1.0f : 0.0f;
        sideSignals[side] = 4.0f * depthEdge + lumaEdge +
            normalEdge + identityEdge;
    }

    // This vector is normal to the strongest local edge. Spatial fallback can
    // still gather along the edge tangent while strongly suppressing samples
    // that cross the edge into another surface.
    float2 gradient = float2(
        sideSignals[1] - sideSignals[0],
        sideSignals[3] - sideSignals[2]);
    float gradientLength = length(gradient);
    edgeDirection = gradientLength > RtaaEpsilon
        ? gradient / gradientLength : 0.0f;
    edgeStrength = saturate(gradientLength);
}

float3 ComputeSpatialFallback(
    int2 tileCenter,
    float3 centerColor,
    float centerViewDepth,
    float3 centerNormal,
    uint2 centerSurface,
    float centerThinCoverage)
{
    if (centerViewDepth <= 0.0f)
        return centerColor;

    int radius = int(clamp(g_Rtaa.spatialFallbackRadius, 1u, 2u));
    float centerLogLuma = log2(1.0f + Luminance(centerColor));
    float3 weightedColor = 0.0f;
    float totalWeight = 0.0f;
    float2 edgeDirection;
    float edgeStrength;
    ComputeSharedEdgeDirection(tileCenter, centerViewDepth, centerNormal,
        centerSurface, edgeDirection, edgeStrength);

    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            if (abs(x) > radius || abs(y) > radius)
                continue;

            uint index = TileIndex(tileCenter + int2(x, y));
            float4 neighborNormalDepth = s_NormalViewDepth[index];
            if (neighborNormalDepth.w <= 0.0f)
                continue;

            uint2 neighborSurface = s_SurfaceIds[index];
            if (g_Rtaa.enableMaterialValidation != 0u &&
                neighborSurface.x != centerSurface.x)
            {
                continue;
            }
            if (g_Rtaa.enableObjectValidation != 0u &&
                neighborSurface.y != centerSurface.y)
            {
                continue;
            }

            float3 neighborColor = s_CurrentColor[index].rgb;
            float relativeDepthDifference = abs(neighborNormalDepth.w -
                centerViewDepth) / max(centerViewDepth, 0.1f);
            float normalDifference = 1.0f - saturate(dot(
                centerNormal, neighborNormalDepth.xyz));
            float lumaDifference = abs(log2(1.0f +
                Luminance(neighborColor)) - centerLogLuma);
            float distanceSquared = float(x * x + y * y);
            float neighborThinCoverage = s_ThinCandidate[index];
            float thinActivity = saturate(max(centerThinCoverage,
                neighborThinCoverage) * 4.0f);
            float thinAgreement = 1.0f - saturate(abs(
                neighborThinCoverage - centerThinCoverage) * 2.0f);
            float thinGate = lerp(1.0f, thinAgreement, thinActivity);

            float2 offsetDirection = distanceSquared > 0.0f
                ? float2(x, y) * rsqrt(distanceSquared) : 0.0f;
            float crossEdge = abs(dot(offsetDirection, edgeDirection));
            float directionGate = max(1.0f -
                0.90f * edgeStrength * crossEdge, 0.05f);

            // Reciprocal bilateral weights are deterministic, cheap on low-end
            // GPUs, and avoid per-pixel transcendental evaluation.
            float weight = rcp(1.0f + 0.75f * distanceSquared +
                g_Rtaa.spatialDepthWeight * relativeDepthDifference +
                g_Rtaa.spatialNormalWeight * normalDifference +
                g_Rtaa.spatialLumaWeight * lumaDifference);
            weight *= thinGate * directionGate;
            weightedColor += neighborColor * weight;
            totalWeight += weight;
        }
    }

    return totalWeight > RtaaEpsilon
        ? SanitizeHdr(weightedColor / totalWeight)
        : centerColor;
}

float ComputeAutomaticReactive(float3 currentColor, float3 historyColor)
{
    if (g_Rtaa.enableAutomaticReactive == 0u)
        return 0.0f;

    float3 current = RgbToCompressedYCoCg(currentColor);
    float3 history = RgbToCompressedYCoCg(historyColor);
    float lumaDifference = abs(current.x - history.x);
    float chromaDifference = length(current.yz - history.yz);
    float lumaReactive = SmoothRange(lumaDifference,
        g_Rtaa.automaticReactiveLumaThreshold,
        g_Rtaa.automaticReactiveLumaThreshold * 2.0f);
    float chromaReactive = SmoothRange(chromaDifference,
        g_Rtaa.automaticReactiveChromaThreshold,
        g_Rtaa.automaticReactiveChromaThreshold * 2.0f);
    return saturate(max(lumaReactive, chromaReactive) *
        max(g_Rtaa.automaticReactiveStrength, 0.0f));
}

struct ResurrectionCandidate
{
    float3 color;
    float confidence;
    float match;
    float historyCount;
    uint sourceIndex;
};

#if RTAA_RESURRECTION
ResurrectionCandidate EvaluatePersistentHistory(
    Texture2D<float4> colorTexture,
    Texture2D<float4> metadataTexture,
    Texture2D<float> depthTexture,
    Texture2D<uint2> surfaceTexture,
    PlanarViewConstants historyView,
    uint validityBit,
    uint sourceIndex,
    float3 currentWorld,
    float3 currentNormal,
    uint currentMaterialId,
    uint currentObjectToken,
    float3 currentYCoCg,
    float3 immediateYCoCg,
    float immediateConfidence,
    float3 neighborhoodMean,
    float3 lowerBound,
    float3 upperBound)
{
    ResurrectionCandidate result = (ResurrectionCandidate)0;
    result.sourceIndex = sourceIndex;
    if ((g_Rtaa.persistentValidMask & validityBit) == 0u)
        return result;

    float2 historyPixelCenter;
    float expectedDepth;
    if (!ProjectWorldToHistory(currentWorld, historyView,
            historyPixelCenter, expectedDepth))
    {
        return result;
    }

    bool inside = all(historyPixelCenter >= 0.5f) &&
        all(historyPixelCenter <= g_Rtaa.resolution - 0.5f);
    if (!inside)
        return result;

    int2 historyPixel = clamp(int2(floor(historyPixelCenter)), 0,
        int2(g_Rtaa.resolution) - 1);
    float actualDepth = depthTexture.Load(int3(historyPixel, 0));
    float depthConfidence = ComputeDepthConfidence(historyPixelCenter,
        actualDepth, expectedDepth, currentWorld, currentNormal, historyView);
    if (depthConfidence <= 0.0f)
        return result;

    uint2 storedSurface = surfaceTexture.Load(int3(historyPixel, 0));
    float3 historyNormal;
    uint historyMaterialId;
    uint historyObjectToken;
    UnpackHistorySurface(storedSurface, historyNormal,
        historyMaterialId, historyObjectToken);
    float normalConfidence = SmoothRange(dot(currentNormal, historyNormal),
        g_Rtaa.normalRejectCosine, g_Rtaa.normalAcceptCosine);
    float materialMatch = g_Rtaa.enableMaterialValidation == 0u ||
        historyMaterialId == currentMaterialId ? 1.0f : 0.0f;
    float objectMatch = g_Rtaa.enableObjectValidation == 0u ||
        historyObjectToken == currentObjectToken ? 1.0f : 0.0f;
    if (normalConfidence <= 0.0f || materialMatch == 0.0f ||
        objectMatch == 0.0f)
    {
        return result;
    }

    float4 historySample = SampleHistoryColor(colorTexture,
        historyPixelCenter);
    float3 candidateYCoCg = RgbToCompressedYCoCg(historySample.rgb);
    float lumaError = abs(candidateYCoCg.x - currentYCoCg.x) /
        max(g_Rtaa.automaticReactiveLumaThreshold, RtaaEpsilon);
    float chromaError = length(candidateYCoCg.yz - currentYCoCg.yz) /
        max(g_Rtaa.automaticReactiveChromaThreshold, RtaaEpsilon);
    float candidateError = max(lumaError, chromaError);
    float currentMatch = saturate(1.0f - candidateError);

    float immediateError = max(
        abs(immediateYCoCg.x - currentYCoCg.x) /
            max(g_Rtaa.automaticReactiveLumaThreshold, RtaaEpsilon),
        length(immediateYCoCg.yz - currentYCoCg.yz) /
            max(g_Rtaa.automaticReactiveChromaThreshold, RtaaEpsilon));
    float immediateAgreement = immediateConfidence > 0.0f
        ? saturate(1.0f - max(
            abs(candidateYCoCg.x - immediateYCoCg.x) /
                max(g_Rtaa.automaticReactiveLumaThreshold * 2.0f, RtaaEpsilon),
            length(candidateYCoCg.yz - immediateYCoCg.yz) /
                max(g_Rtaa.automaticReactiveChromaThreshold * 2.0f, RtaaEpsilon)))
        : 1.0f;
    float betterThanImmediate = immediateConfidence <=
        g_Rtaa.resurrectionMatchThreshold || candidateError < immediateError
        ? 1.0f : 0.0f;
    result.match = currentMatch * lerp(1.0f, immediateAgreement, 0.25f) *
        betterThanImmediate;

    float4 metadata = metadataTexture.Load(int3(historyPixel, 0));
    result.confidence = saturate(depthConfidence * normalConfidence *
        materialMatch * objectMatch * metadata.g * result.match);
    result.historyCount = metadata.r * max(float(g_Rtaa.maxHistorySamples), 1.0f);

    // Persistent history is clipped more aggressively than immediate history:
    // halve the permitted line segment around the current neighborhood mean.
    float3 aggressiveLower = lerp(neighborhoodMean, lowerBound, 0.5f);
    float3 aggressiveUpper = lerp(neighborhoodMean, upperBound, 0.5f);
    float3 clipped = ClipLineToBox(candidateYCoCg, neighborhoodMean,
        aggressiveLower, aggressiveUpper);
    result.color = CompressedYCoCgToRgb(clipped);
    return result;
}
#endif

float3 VisualizeMotion(float2 motion)
{
    float scale = rcp(max(max(g_Rtaa.motionWeightEndPixels, 8.0f), 1.0f));
    return saturate(float3(0.5f + motion * scale * 0.5f,
        length(motion) * scale));
}

[numthreads(8, 8, 1)]
void main(
    uint2 groupId : SV_GroupID,
    uint2 threadId : SV_GroupThreadID,
    uint2 pixel : SV_DispatchThreadID)
{
    uint linearThreadIndex = threadId.y * RtaaGroupSize + threadId.x;
    LoadSharedTile(groupId, linearThreadIndex);
    GroupMemoryBarrierWithGroupSync();
    BuildSharedThinCandidates(threadId);
    GroupMemoryBarrierWithGroupSync();

    // Both barriers must execute for every lane. Bounds rejection is therefore
    // deliberately after the cooperative tile and structural-classification
    // phases, which also cover odd native dimensions without out-of-range
    // texture accesses.
    if (any(pixel >= uint2(g_Rtaa.resolution)))
        return;

    int2 outputPixel = int2(pixel);
    int2 tileCenter = int2(threadId) + 2;
    uint centerTileIndex = TileIndex(tileCenter);
    float3 currentColor = s_CurrentColor[centerTileIndex].rgb;
    float4 prepared = t_Prepared.Load(int3(outputPixel, 0));
    float4 classification = saturate(
        t_Classification.Load(int3(outputPixel, 0)));
    int2 sourceOffset = int2(round(classification.rg * 2.0f - 1.0f));
    sourceOffset = clamp(sourceOffset, -1, 1);
    int2 sourcePixel = clamp(outputPixel + sourceOffset, 0,
        int2(g_Rtaa.resolution) - 1);

    // Velocity dilation may borrow reprojection motion from a nearby closest
    // surface, but it must never replace the center pixel's geometry. Using
    // the borrowed depth/normal/IDs here would write foreground identity into
    // background history and let spatial fallback cross a silhouette.
    float currentDeviceDepth = s_CurrentColor[centerTileIndex].a;
    float currentViewDepth = s_NormalViewDepth[centerTileIndex].w;
    float3 currentNormal = s_NormalViewDepth[centerTileIndex].xyz;
    uint2 currentSurface = s_SurfaceIds[centerTileIndex];
    uint currentMaterialId = currentSurface.x;
    uint currentObjectToken = HashToWord(currentSurface.y);
    float velocityConfidence = saturate(prepared.w);
    float2 dilatedMotion = prepared.xy;

    // Sky/background has no stable geometric surface to validate. In final
    // production modes, write a fresh current sample and skip all history,
    // clipping, fallback, and resurrection work after the required group
    // barrier. Full diagnostic modes continue below so their inputs remain
    // visible over background pixels.
    if (!IsValidDeviceDepth(currentDeviceDepth) && g_Rtaa.writeDebug == 0u)
    {
        float backgroundLogLuma = log2(1.0f + Luminance(currentColor));
        float backgroundMotionFactor = SmoothRange(length(dilatedMotion),
            g_Rtaa.motionWeightStartPixels, g_Rtaa.motionWeightEndPixels);
        u_HistoryColor[pixel] = float4(currentColor, 0.0f);
        u_HistoryMoments[pixel] = float2(backgroundLogLuma,
            backgroundLogLuma * backgroundLogLuma);
        u_HistoryMetadata[pixel] = saturate(float4(
            rcp(max(float(g_Rtaa.maxHistorySamples), 1.0f)),
            0.0f, classification.b, backgroundMotionFactor));
        u_HistoryDepth[pixel] = 0.0f;
        u_HistorySurface[pixel] = PackHistorySurface(
            currentNormal, currentMaterialId, currentObjectToken);
        return;
    }

    // The G-buffer producer explicitly de-jitters motion. Preserve its verified
    // convention verbatim: current-to-previous pixels, previous = current +
    // motion. Adding currentJitter-previousJitter here would double-correct it.
    float2 previousPixelCenter = float2(outputPixel) + 0.5f + dilatedMotion;
    float2 previousUv = previousPixelCenter * g_Rtaa.invResolution;
    bool previousInside = all(previousPixelCenter >= 0.5f) &&
        all(previousPixelCenter <= g_Rtaa.resolution - 0.5f);
    int2 previousPixel = clamp(int2(floor(previousPixelCenter)), 0,
        int2(g_Rtaa.resolution) - 1);

    float4 historyColorSample = float4(currentColor, 0.0f);
    float2 historyMoments = 0.0f;
    float4 historyMetadata = 0.0f;
    float historyDeviceDepth = 0.0f;
    uint2 historySurface = uint2(0xffffffffu, 0u);
    if (g_Rtaa.historyValid != 0u && previousInside)
    {
        historyColorSample = SampleHistoryColor(
            t_ImmediateHistoryColor, previousPixelCenter);
        historyMoments = t_ImmediateHistoryMoments.Load(
            int3(previousPixel, 0));
        historyMetadata = saturate(t_ImmediateHistoryMetadata.Load(
            int3(previousPixel, 0)));
        historyDeviceDepth = t_ImmediateHistoryDepth.Load(
            int3(previousPixel, 0));
        historySurface = t_ImmediateHistorySurface.Load(
            int3(previousPixel, 0));
    }

    // Current world position always describes the center surface. The selected
    // source owns only dilated motion; older-history reprojection and surface
    // storage remain tied to the pixel being resolved.
    float3 currentWorld = 0.0f;
    bool currentWorldValid = IsValidDeviceDepth(currentDeviceDepth) &&
        ReconstructWorldPosition(float2(outputPixel) + 0.5f,
            currentDeviceDepth, g_Rtaa.currentView, currentWorld);

    float expectedPreviousDepth = 0.0f;
    bool expectedPreviousDepthValid = false;
    float4 selectedRawMotion = t_RawMotion.Load(int3(sourcePixel, 0));
    // motion.z is the selected source's previous-current device-depth delta.
    // It is valid for the center surface only when no neighboring velocity was
    // borrowed. For a dilated sample, project the center world position below;
    // that expected depth will reject foreground motion on background pixels.
    if (all(sourceOffset == 0) && selectedRawMotion.w > 0.5f &&
        all(isfinite(selectedRawMotion.xyz)))
    {
        expectedPreviousDepth = currentDeviceDepth + selectedRawMotion.z;
        expectedPreviousDepthValid = IsValidDeviceDepth(expectedPreviousDepth);
    }
    if (!expectedPreviousDepthValid && currentWorldValid)
    {
        float2 projectedPixel;
        expectedPreviousDepthValid = ProjectWorldToHistory(currentWorld,
            g_Rtaa.immediateHistoryView, projectedPixel,
            expectedPreviousDepth) && IsValidDeviceDepth(expectedPreviousDepth);
    }

    float depthConfidence = previousInside && expectedPreviousDepthValid
        ? ComputeDepthConfidence(previousPixelCenter, historyDeviceDepth,
            expectedPreviousDepth, currentWorld, currentNormal,
            g_Rtaa.immediateHistoryView)
        : 0.0f;
    float3 previousNormal;
    uint previousMaterialId;
    uint previousObjectToken;
    UnpackHistorySurface(historySurface, previousNormal,
        previousMaterialId, previousObjectToken);
    float normalConfidence = previousInside
        ? SmoothRange(dot(currentNormal, previousNormal),
            g_Rtaa.normalRejectCosine, g_Rtaa.normalAcceptCosine)
        : 0.0f;
    float rawMaterialMatch = currentMaterialId == previousMaterialId
        ? 1.0f : 0.0f;
    float rawObjectMatch = currentObjectToken == previousObjectToken
        ? 1.0f : 0.0f;
    float materialMatch = g_Rtaa.enableMaterialValidation == 0u
        ? 1.0f : rawMaterialMatch;
    float objectMatch = g_Rtaa.enableObjectValidation == 0u
        ? 1.0f : rawObjectMatch;

    // Confidence is established from this frame's geometric evidence. Feeding
    // the prior confidence back multiplicatively creates a zero fixed point:
    // reset writes zero, so the first otherwise-valid frame can never begin
    // accumulation. Prior metadata still supplies sample count and diagnostics,
    // but cannot permanently veto freshly validated history.
    float combinedHistoryConfidence = g_Rtaa.historyValid != 0u &&
        previousInside ? saturate(velocityConfidence * depthConfidence *
            normalConfidence * materialMatch * objectMatch) : 0.0f;

    float explicitReactive = classification.b;
    float automaticReactive = combinedHistoryConfidence > 0.0f
        ? ComputeAutomaticReactive(currentColor, historyColorSample.rgb)
        : 0.0f;
    float finalReactive = saturate(max(explicitReactive, automaticReactive));

    float combinedThinCandidate = s_CombinedThinCandidate[centerTileIndex];
    float thinCandidate = DiffuseThinCandidate(tileCenter,
        currentViewDepth, currentNormal, currentSurface,
        combinedThinCandidate);
    float previousThinCoverage = historyColorSample.a;
    float thinHistoryWeight = saturate((1.0f -
        g_Rtaa.thinCoverageCurrentWeight) * combinedHistoryConfidence *
        (1.0f - finalReactive));
    float accumulatedThinCoverage = g_Rtaa.enableThinGeometry != 0u
        ? saturate(lerp(thinCandidate, previousThinCoverage,
            thinHistoryWeight)) : 0.0f;

    // Thin relaxation is bounded by every hard geometric test. It can soften
    // clipping for a validated subpixel strip, but cannot revive a disoccluded,
    // normal-incompatible, or cross-material sample.
    float hardGeometricConfidence = saturate(velocityConfidence *
        depthConfidence * normalConfidence * materialMatch * objectMatch);
    float thinRelaxation = accumulatedThinCoverage *
        saturate(g_Rtaa.thinMaxRelaxation);
    // Add a small fractional recovery only when every geometric gate has some
    // support. Multiplying by hardGeometricConfidence keeps a zero depth,
    // normal, material, or object gate at zero; the remaining headroom term
    // makes this an actual bounded relaxation instead of a no-op max(a,a*x).
    float thinConfidenceBoost = hardGeometricConfidence * thinRelaxation *
        historyMetadata.g * (1.0f - combinedHistoryConfidence);
    combinedHistoryConfidence = saturate(combinedHistoryConfidence +
        thinConfidenceBoost);

    float3 neighborhoodMean;
    float3 neighborhoodVariance;
    float3 neighborhoodMinimum;
    float3 neighborhoodMaximum;
    float3 neighborhoodRgbMean;
    ComputeNeighborhoodStatistics(tileCenter, neighborhoodMean,
        neighborhoodVariance, neighborhoodMinimum, neighborhoodMaximum,
        neighborhoodRgbMean);

    float3 sigma = sqrt(max(neighborhoodVariance, 0.0f)) *
        max(g_Rtaa.varianceSigma, 0.0f);
    sigma.x *= max(g_Rtaa.varianceLumaScale, 0.0f);
    sigma.yz *= max(g_Rtaa.varianceChromaScale, 0.0f);
    float3 lowerBound = max(neighborhoodMinimum,
        neighborhoodMean - sigma);
    float3 upperBound = min(neighborhoodMaximum,
        neighborhoodMean + sigma);
    float clipExpansion = saturate(accumulatedThinCoverage *
        g_Rtaa.thinClipExpansion * (1.0f - finalReactive));
    lowerBound = lerp(lowerBound, neighborhoodMinimum, clipExpansion);
    upperBound = lerp(upperBound, neighborhoodMaximum, clipExpansion);

    float3 unclippedHistoryYCoCg = RgbToCompressedYCoCg(
        historyColorSample.rgb);
    float3 clippedHistoryYCoCg = ClipLineToBox(unclippedHistoryYCoCg,
        neighborhoodMean, lowerBound, upperBound);
    float3 immediateClippedHistory = CompressedYCoCgToRgb(
        clippedHistoryYCoCg);
    float clipDistance = length(unclippedHistoryYCoCg -
        clippedHistoryYCoCg) /
        max(length(unclippedHistoryYCoCg - neighborhoodMean), RtaaEpsilon);
    float clipConfidence = 1.0f - saturate(clipDistance * 0.5f);
    combinedHistoryConfidence *= clipConfidence;

    // Fallback is a rejection path, not a permanent spatial blur. It reaches
    // full strength below 0.25 confidence and fades out completely by 0.75.
    float fallbackContribution = g_Rtaa.enableSpatialFallback != 0u
        ? 1.0f - SmoothRange(combinedHistoryConfidence, 0.25f, 0.75f)
        : 0.0f;
    float3 spatialFallback = fallbackContribution > 0.0f
        ? ComputeSpatialFallback(tileCenter, currentColor, currentViewDepth,
            currentNormal, currentSurface, accumulatedThinCoverage)
        : currentColor;
    float3 resolvedCurrent = SanitizeHdr(lerp(currentColor,
        spatialFallback, fallbackContribution));

    float3 historyForAccumulation = immediateClippedHistory;
    float2 historyMomentsForAccumulation = historyMoments;
    float effectiveHistoryCount = historyMetadata.r *
        max(float(g_Rtaa.maxHistorySamples), 1.0f);
    float resurrectionEligibility = 0.0f;
    float resurrectionSource = 0.0f;
    float resurrectionConfidence = 0.0f;
    float3 resurrectionColor = 0.0f;

#if RTAA_RESURRECTION
    resurrectionEligibility = g_Rtaa.enableResurrection != 0u &&
        g_Rtaa.persistentValidMask != 0u && currentWorldValid &&
        (combinedHistoryConfidence < g_Rtaa.resurrectionMatchThreshold ||
            clipDistance > 0.5f) ? 1.0f : 0.0f;
    if (resurrectionEligibility > 0.0f)
    {
        ResurrectionCandidate candidate0 = EvaluatePersistentHistory(
            t_PersistentHistoryColor0, t_PersistentHistoryMetadata0,
            t_PersistentHistoryDepth0, t_PersistentHistorySurface0,
            g_Rtaa.persistentHistoryView0, 1u, 1u, currentWorld,
            currentNormal, currentMaterialId, currentObjectToken,
            RgbToCompressedYCoCg(currentColor), unclippedHistoryYCoCg,
            combinedHistoryConfidence, neighborhoodMean, lowerBound,
            upperBound);
        ResurrectionCandidate candidate1 = EvaluatePersistentHistory(
            t_PersistentHistoryColor1, t_PersistentHistoryMetadata1,
            t_PersistentHistoryDepth1, t_PersistentHistorySurface1,
            g_Rtaa.persistentHistoryView1, 2u, 2u, currentWorld,
            currentNormal, currentMaterialId, currentObjectToken,
            RgbToCompressedYCoCg(currentColor), unclippedHistoryYCoCg,
            combinedHistoryConfidence, neighborhoodMean, lowerBound,
            upperBound);
        ResurrectionCandidate best = candidate0;
        if (candidate1.confidence > candidate0.confidence)
            best = candidate1;

        if (best.confidence > combinedHistoryConfidence &&
            best.match >= g_Rtaa.resurrectionMatchThreshold)
        {
            historyForAccumulation = best.color;
            float resurrectedLogLuma = log2(1.0f + Luminance(best.color));
            historyMomentsForAccumulation = float2(resurrectedLogLuma,
                resurrectedLogLuma * resurrectedLogLuma);
            effectiveHistoryCount = best.historyCount;
            resurrectionConfidence = best.confidence;
            resurrectionSource = float(best.sourceIndex);
            resurrectionColor = best.color;
            combinedHistoryConfidence = max(combinedHistoryConfidence,
                best.confidence);
        }
    }
#endif

    float motionMagnitude = length(dilatedMotion);
    float motionFactor = SmoothRange(motionMagnitude,
        g_Rtaa.motionWeightStartPixels, g_Rtaa.motionWeightEndPixels);
    float baseCurrentWeight = lerp(g_Rtaa.stableCurrentWeight,
        g_Rtaa.movingCurrentWeight, motionFactor);
    baseCurrentWeight = lerp(baseCurrentWeight,
        g_Rtaa.reactiveCurrentWeight, finalReactive);

    float maximumSamples = max(1.0f, round(lerp(
        float(max(g_Rtaa.maxHistorySamples, 1u)),
        float(max(g_Rtaa.maxMovingHistorySamples, 1u)), motionFactor)));
    float sampleLimitedWeight = rcp(min(effectiveHistoryCount,
        maximumSamples) + 1.0f);
    float currentWeight = max(saturate(baseCurrentWeight),
        sampleLimitedWeight);
    currentWeight = lerp(currentWeight, 1.0f,
        1.0f - saturate(combinedHistoryConfidence));
    if (resurrectionSource > 0.0f)
    {
        // Older history can help a rejected pixel, but stale illumination is
        // never allowed to dominate the immediate current sample.
        currentWeight = max(currentWeight,
            1.0f - saturate(g_Rtaa.resurrectionMaxWeight));
    }

    float3 outputColor = SanitizeHdr(lerp(historyForAccumulation,
        resolvedCurrent, saturate(currentWeight)));
    bool historyAccepted = combinedHistoryConfidence > 0.05f &&
        currentWeight < 0.999f;
    float newHistoryCount = historyAccepted
        ? min(effectiveHistoryCount + 1.0f, maximumSamples)
        : 1.0f;
    newHistoryCount = lerp(newHistoryCount, 1.0f,
        saturate(finalReactive * 0.5f));

    float currentLogLuma = log2(1.0f + Luminance(resolvedCurrent));
    float2 currentMoments = float2(currentLogLuma,
        currentLogLuma * currentLogLuma);
    float2 outputMoments = historyAccepted
        ? lerp(historyMomentsForAccumulation, currentMoments,
            saturate(currentWeight))
        : currentMoments;
    if (!all(isfinite(outputMoments)))
        outputMoments = currentMoments;

    float metadataCountDenominator = max(
        float(g_Rtaa.maxHistorySamples), 1.0f);
    float4 outputMetadata = saturate(float4(
        newHistoryCount / metadataCountDenominator,
        combinedHistoryConfidence,
        finalReactive,
        motionFactor));
    uint2 outputSurface = PackHistorySurface(currentNormal,
        currentMaterialId, currentObjectToken);

    // History writes are unconditional and precede debug selection. Debugging
    // therefore cannot freeze, replace, clamp, or otherwise contaminate the
    // state consumed by a later frame.
    u_HistoryColor[pixel] = float4(outputColor, accumulatedThinCoverage);
    u_HistoryMoments[pixel] = outputMoments;
    u_HistoryMetadata[pixel] = outputMetadata;
    u_HistoryDepth[pixel] = IsValidDeviceDepth(currentDeviceDepth)
        ? currentDeviceDepth : 0.0f;
    u_HistorySurface[pixel] = outputSurface;

    if (g_Rtaa.writeDebug != 0u)
    {
        float depthDisplay = g_Rtaa.reverseZ != 0u
            ? currentDeviceDepth : 1.0f - currentDeviceDepth;
        float previousDepthDisplay = g_Rtaa.reverseZ != 0u
            ? historyDeviceDepth : 1.0f - historyDeviceDepth;
        float3 varianceDisplay = saturate(sqrt(neighborhoodVariance) *
            float3(2.0f, 4.0f, 4.0f));
        float3 clippingBoundsDisplay = saturate(float3(
            (upperBound.x - lowerBound.x) * 2.0f,
            (upperBound.y - lowerBound.y) * 4.0f,
            (upperBound.z - lowerBound.z) * 4.0f));
        float varianceSuppression = saturate(1.0f -
            sqrt(neighborhoodVariance.x) * 4.0f);
        float sharpeningSuppression = (1.0f - motionFactor) *
            (1.0f - finalReactive) * varianceSuppression *
            combinedHistoryConfidence;
        float3 sharpeningContribution = (resolvedCurrent -
            neighborhoodRgbMean) * sharpeningSuppression;

        float3 rejectionReasons = 0.0f;
        if (g_Rtaa.historyValid == 0u || !previousInside ||
            velocityConfidence <= 0.0f)
            rejectionReasons += float3(1.0f, 0.0f, 0.0f);
        rejectionReasons += (1.0f - depthConfidence) *
            float3(1.0f, 0.45f, 0.0f);
        rejectionReasons += (1.0f - normalConfidence) *
            float3(0.0f, 0.2f, 1.0f);
        rejectionReasons += (1.0f - materialMatch) *
            float3(1.0f, 0.0f, 1.0f);
        rejectionReasons += (1.0f - objectMatch) *
            float3(0.0f, 1.0f, 1.0f);
        rejectionReasons = saturate(rejectionReasons);

        float3 debugColor = outputColor;
        switch (g_Rtaa.debugMode)
        {
        case RTAA_DEBUG_CURRENT_JITTER:
            debugColor = saturate(float3(0.5f + g_Rtaa.currentJitter,
                length(g_Rtaa.currentJitter)));
            break;
        case RTAA_DEBUG_MOTION_VECTORS:
            debugColor = VisualizeMotion(
                t_RawMotion.Load(int3(outputPixel, 0)).xy);
            break;
        case RTAA_DEBUG_DILATED_MOTION_VECTORS:
            debugColor = VisualizeMotion(dilatedMotion);
            break;
        case RTAA_DEBUG_VELOCITY_SOURCE_PIXEL:
            debugColor = float3(classification.rg,
                all(sourceOffset == 0) ? 1.0f : 0.0f);
            break;
        case RTAA_DEBUG_VELOCITY_CONFIDENCE:
            debugColor = velocityConfidence.xxx;
            break;
        case RTAA_DEBUG_REPROJECTED_HISTORY:
            debugColor = historyColorSample.rgb;
            break;
        case RTAA_DEBUG_PREVIOUS_HISTORY_UV:
            debugColor = float3(saturate(previousUv),
                previousInside ? 1.0f : 0.0f);
            break;
        case RTAA_DEBUG_CURRENT_DEPTH:
            debugColor = saturate(depthDisplay).xxx;
            break;
        case RTAA_DEBUG_REPROJECTED_PREVIOUS_DEPTH:
            debugColor = saturate(previousDepthDisplay).xxx;
            break;
        case RTAA_DEBUG_DEPTH_CONFIDENCE:
            debugColor = depthConfidence.xxx;
            break;
        case RTAA_DEBUG_NORMAL_CONFIDENCE:
            debugColor = normalConfidence.xxx;
            break;
        case RTAA_DEBUG_MATERIAL_MATCH:
            debugColor = rawMaterialMatch.xxx;
            break;
        case RTAA_DEBUG_OBJECT_MATCH:
            debugColor = rawObjectMatch.xxx;
            break;
        case RTAA_DEBUG_COMBINED_HISTORY_CONFIDENCE:
            debugColor = combinedHistoryConfidence.xxx;
            break;
        case RTAA_DEBUG_EXPLICIT_REACTIVE_MASK:
            debugColor = explicitReactive.xxx;
            break;
        case RTAA_DEBUG_AUTOMATIC_REACTIVE_MASK:
            debugColor = automaticReactive.xxx;
            break;
        case RTAA_DEBUG_FINAL_REACTIVE_VALUE:
            debugColor = float3(finalReactive, 0.0f,
                1.0f - finalReactive);
            break;
        case RTAA_DEBUG_THIN_GEOMETRY_CLASSIFICATION:
            debugColor = float3(thinCandidate,
                thinCandidate * 0.65f, 0.0f);
            break;
        case RTAA_DEBUG_THIN_GEOMETRY_ACCUMULATED_COVERAGE:
            debugColor = float3(accumulatedThinCoverage,
                0.0f, accumulatedThinCoverage);
            break;
        case RTAA_DEBUG_VARIANCE:
            debugColor = varianceDisplay;
            break;
        case RTAA_DEBUG_CLIPPING_BOUNDS:
            debugColor = clippingBoundsDisplay;
            break;
        case RTAA_DEBUG_UNCLIPPED_HISTORY:
            debugColor = historyColorSample.rgb;
            break;
        case RTAA_DEBUG_CLIPPED_HISTORY:
            debugColor = immediateClippedHistory;
            break;
        case RTAA_DEBUG_CURRENT_FRAME_WEIGHT:
            debugColor = float3(currentWeight, 1.0f - currentWeight, 0.0f);
            break;
        case RTAA_DEBUG_HISTORY_SAMPLE_COUNT:
            debugColor = saturate(newHistoryCount /
                metadataCountDenominator).xxx;
            break;
        case RTAA_DEBUG_SPATIAL_FALLBACK_CONTRIBUTION:
            debugColor = float3(fallbackContribution,
                fallbackContribution * 0.5f, 0.0f);
            break;
        case RTAA_DEBUG_RESURRECTION_ELIGIBILITY:
            debugColor = resurrectionEligibility.xxx;
            break;
        case RTAA_DEBUG_RESURRECTION_SOURCE:
            debugColor = resurrectionSource == 1.0f
                ? float3(0.0f, resurrectionConfidence, 0.0f)
                : (resurrectionSource == 2.0f
                    ? float3(0.0f, 0.0f, resurrectionConfidence)
                    : 0.0f);
            break;
        case RTAA_DEBUG_SHARPENING_CONTRIBUTION:
            debugColor = saturate(0.5f + sharpeningContribution);
            break;
        case RTAA_DEBUG_REJECTION_REASONS:
            debugColor = rejectionReasons;
            break;
        case RTAA_DEBUG_FINAL_OUTPUT:
        case RTAA_DEBUG_FINAL_NRA_RTAA_OUTPUT:
        default:
            debugColor = outputColor;
            break;
        }

        u_Debug[pixel] = float4(SanitizeHdr(debugColor), 1.0f);
    }
}
