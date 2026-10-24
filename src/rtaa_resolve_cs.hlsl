#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "reconstructive_temporal_aa_cb.h"

#if defined(RTAA_VARIANT)
#define RTAA_CATMULL_ROM (RTAA_VARIANT & 1)
#define RTAA_RESURRECTION ((RTAA_VARIANT >> 1) & 1)
#define RTAA_PERFORMANCE_TIER (RTAA_VARIANT >> 2)
#endif

#ifndef RTAA_CATMULL_ROM
#define RTAA_CATMULL_ROM 1
#endif

#ifndef RTAA_RESURRECTION
#define RTAA_RESURRECTION 1
#endif

#ifndef RTAA_PERFORMANCE_TIER
#define RTAA_PERFORMANCE_TIER 2
#endif

// Keep standalone shader builds and legacy callers feature-complete. Runtime
// PSOs specialize these switches so production does not retain the large debug
// side channel and Performance does not carry unused sharpening moments.
#ifndef RTAA_ENABLE_DEBUG_OUTPUT
#define RTAA_ENABLE_DEBUG_OUTPUT 1
#endif

#ifndef RTAA_ENABLE_MOMENTS
#define RTAA_ENABLE_MOMENTS 1
#endif

#ifndef RTAA_USE_FP16
#define RTAA_USE_FP16 0
#endif

#ifndef RTAA_FUSED_PREPARE
#define RTAA_FUSED_PREPARE 0
#endif

#if RTAA_FUSED_PREPARE && RTAA_PERFORMANCE_TIER != 0
#error RTAA_FUSED_PREPARE is specialized for the Performance tier only
#endif

// Native FP16 is restricted to bounded color statistics, confidence/weight,
// reactive, and thin-feature arithmetic. Geometry, depth, motion, UVs, matrix
// math, reprojection, and shared storage deliberately remain FP32.
#if RTAA_USE_FP16
typedef half RtaaLowp;
typedef half2 RtaaLowp2;
typedef half3 RtaaLowp3;
typedef half4 RtaaLowp4;
#else
typedef float RtaaLowp;
typedef float2 RtaaLowp2;
typedef float3 RtaaLowp3;
typedef float4 RtaaLowp4;
#endif

cbuffer c_ReconstructiveTemporalAA : register(b0)
{
    ReconstructiveTemporalAAConstants g_Rtaa;
};

SamplerState s_LinearClamp : register(s0);

Texture2D<float4> t_CurrentColor : register(t0);
Texture2D<float>  t_CurrentDepth : register(t1);
#if RTAA_FUSED_PREPARE
Texture2D<float4> t_CurrentDiffuse : register(t2);
Texture2D<float>  t_ExplicitReactive : register(t3);
#endif
Texture2D<float4> t_CurrentSpecular : register(t4);
Texture2D<uint2>  t_CurrentSurfaceIds : register(t6);
#if !RTAA_FUSED_PREPARE
Texture2D<float2> t_Prepared : register(t7);
Texture2D<float4> t_Classification : register(t8);
#endif
Texture2D<float4> t_RawMotion : register(t9);

Texture2D<float4> t_ImmediateHistoryColor : register(t10);
#if RTAA_ENABLE_MOMENTS
Texture2D<float2> t_ImmediateHistoryMoments : register(t11);
#endif
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
#if RTAA_ENABLE_MOMENTS
// Mean and second moment of log2(1 + luminance).
VK_IMAGE_FORMAT("rg16f") RWTexture2D<float2> u_HistoryMoments : register(u1);
#endif
// Normalized history count, confidence, thin-lock lifetime, and packed
// reactive/motion nibbles.
VK_IMAGE_FORMAT("rgba8") RWTexture2D<float4> u_HistoryMetadata : register(u2);
VK_IMAGE_FORMAT("r32f") RWTexture2D<float> u_HistoryDepth : register(u3);
// R is the exact 32-bit material ID. G packs an octahedral UNORM8x2 normal in
// its low word and a stable 16-bit optional object token in its high word.
VK_IMAGE_FORMAT("rg32ui") RWTexture2D<uint2> u_HistorySurface : register(u4);
// Debug is a side channel. No debug mode is allowed to alter u_History*.
#if RTAA_ENABLE_DEBUG_OUTPUT
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Debug : register(u5);
#endif

static const uint RtaaGroupSize = 8u;
#if RTAA_PERFORMANCE_TIER >= 2
static const uint RtaaTileHalo = 2u;
#else
static const uint RtaaTileHalo = 1u;
#endif
static const uint RtaaTileSize = RtaaGroupSize + 2u * RtaaTileHalo;
static const uint RtaaTileElementCount = RtaaTileSize * RtaaTileSize;
static const float RtaaEpsilon = 1e-6f;
#if RTAA_FUSED_PREPARE
static const uint RtaaFeatureTranslucency = 1u << 2;
static const uint RtaaFeatureRefraction = 1u << 3;
static const uint RtaaFeatureDoubleSided = 1u << 7;
#endif

// A 2-pixel halo supports both the mandatory 3x3 neighborhood and the optional
// radius-2 fallback. Transform HDR RGB once while cooperatively loading the
// tile, then reuse compressed YCoCg directly for every neighborhood statistic.
// Alpha retains exact device depth. The transformed payload replaces rather
// than augments the old RGB payload; Performance's separate luminance cache is
// budgeted below after its unused thin arrays have been removed.
groupshared float4 s_CurrentCompressedColor[RtaaTileElementCount];
// xyz is the normalized current normal, w is positive view depth (0 = invalid).
groupshared float4 s_NormalViewDepth[RtaaTileElementCount];
groupshared uint2 s_SurfaceIds[RtaaTileElementCount];
// After stripping its unused thin arrays, Performance uses 4,400 B/group
// (68.75 B/lane), also below the older Xe-LP guide's approximate 4,672 B/group
// occupancy step. Xe-LPG occupancy still depends on generated SIMD width and
// GRF mode, so this is a footprint reduction rather than a claimed universal
// threshold. Cache exact Rec.709 luminance once per tile sample to replace
// repeated exp2 decodes in opaque geometry work.
#if RTAA_PERFORMANCE_TIER == 0
groupshared float s_CurrentLuminance[RtaaTileElementCount];
#endif
// Performance consumes the center classification directly and needs no shared
// thin storage. Balanced keeps only authored evidence; its structural result is
// lane-local because it does not diffuse. Maximum retains both arrays so its
// in-group diffusion is bit-for-bit equivalent to the full-quality path.
#if RTAA_PERFORMANCE_TIER > 0
groupshared float s_ThinCandidate[RtaaTileElementCount];
#endif
#if RTAA_PERFORMANCE_TIER >= 2
groupshared float s_CombinedThinCandidate[RtaaTileElementCount];
#endif

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

RtaaLowp SmoothRangeLowp(RtaaLowp value, RtaaLowp lower, RtaaLowp upper)
{
    // 1e-6 is subnormal in IEEE binary16 and may flush to zero on native-half
    // hardware, turning a collapsed tuning range into 0/0. Keep this rare
    // divide and its safety floor in FP32 while returning a bounded lowp value.
    float denominator = max((float)upper - (float)lower, RtaaEpsilon);
    return (RtaaLowp)saturate(
        ((float)value - (float)lower) / denominator);
}

// Metadata A carries two independent 4-bit values without adding a texture:
// reactive shading in the low nibble and motion factor in the high nibble.
// Metadata B remains a full 8-bit lock lifetime so every supported 2..64 phase
// jitter cycle can expire predictably. Coverage magnitude is independently
// stored in color alpha.
float EncodeReactiveAndMotion(float reactive, float motionFactor)
{
    uint reactiveBits = (uint)round(saturate(reactive) * 15.0f);
    uint motionBits = (uint)round(saturate(motionFactor) * 15.0f);
    return float((motionBits << 4u) | reactiveBits) / 255.0f;
}

float DecodeMotionFactor(float packed)
{
    uint bits = (uint)round(saturate(packed) * 255.0f);
    return float((bits >> 4u) & 15u) / 15.0f;
}

float LoadInterpolatedStationaryThinLock(float2 pixelCenter)
{
    pixelCenter = clamp(pixelCenter, 0.5f, g_Rtaa.resolution - 0.5f);
    float2 exactCenter = floor(pixelCenter) + 0.5f;
    if (all(abs(pixelCenter - exactCenter) < 1e-5f))
    {
        float4 metadata = saturate(t_ImmediateHistoryMetadata.Load(
            int3(int2(floor(pixelCenter)), 0)));
        return DecodeMotionFactor(metadata.a) < 0.10f
            ? metadata.b : 0.0f;
    }

    int2 basePixel = int2(floor(pixelCenter - 0.5f));
    int2 maximumPixel = int2(g_Rtaa.resolution) - 1;
    float2 fraction = saturate(pixelCenter -
        (float2(basePixel) + 0.5f));
    float interpolatedLifetime = 0.0f;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            int2 samplePixel = clamp(basePixel + int2(x, y), 0,
                maximumPixel);
            float4 metadata = saturate(t_ImmediateHistoryMetadata.Load(
                int3(samplePixel, 0)));
            float weight = (x == 0 ? 1.0f - fraction.x : fraction.x) *
                (y == 0 ? 1.0f - fraction.y : fraction.y);
            if (DecodeMotionFactor(metadata.a) < 0.10f)
                interpolatedLifetime += metadata.b * weight;
        }
    }
    return interpolatedLifetime > (1.0f / 255.0f)
        ? saturate(interpolatedLifetime) : 0.0f;
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
    out float2 resolvedColorCenter,
    out float2 rawAuxiliaryCenter,
    out float expectedDeviceDepth)
{
    // Resolved color/metadata and raw depth/surface occupy different coordinate
    // spaces. Match immediate p+motion for the resolved grid by preserving the
    // current subpixel phase; raw auxiliaries use the jitter of the history view
    // that produced them.
    float4 clipNoOffset = mul(float4(worldPosition, 1.0f),
        view.matWorldToClipNoOffset);
    float4 clipWithOffset = mul(float4(worldPosition, 1.0f),
        view.matWorldToClip);
    if (!all(isfinite(clipNoOffset)) || !all(isfinite(clipWithOffset)) ||
        clipNoOffset.w <= RtaaEpsilon || clipWithOffset.w <= RtaaEpsilon)
    {
        resolvedColorCenter = 0.0f;
        rawAuxiliaryCenter = 0.0f;
        expectedDeviceDepth = 0.0f;
        return false;
    }

    float2 noOffsetNdc = clipNoOffset.xy / clipNoOffset.w;
    float2 rawNdc = clipWithOffset.xy / clipWithOffset.w;
    float2 noOffsetCenter = noOffsetNdc * view.clipToWindowScale +
        view.clipToWindowBias;
    resolvedColorCenter = noOffsetCenter + g_Rtaa.currentJitter;
    rawAuxiliaryCenter = rawNdc * view.clipToWindowScale +
        view.clipToWindowBias;
    expectedDeviceDepth = clipWithOffset.z / clipWithOffset.w;
    return all(isfinite(resolvedColorCenter)) &&
        all(isfinite(rawAuxiliaryCenter)) && isfinite(expectedDeviceDepth);
}

bool IsValidRawMotion(float4 motion)
{
    float2 generousLimit = max(g_Rtaa.resolution * 2.0f, 1.0f);
    return motion.w > 0.5f && all(isfinite(motion.xyz)) &&
        all(abs(motion.xy) <= generousLimit);
}

#if RTAA_FUSED_PREPARE
bool IsCloserDeviceDepth(float candidate, float reference)
{
    return g_Rtaa.reverseZ != 0u
        ? candidate > reference : candidate < reference;
}

bool ProjectWorldNoOffset(
    float3 worldPosition,
    PlanarViewConstants view,
    out float2 pixelCenter)
{
    float4 clip = mul(float4(worldPosition, 1.0f),
        view.matWorldToClipNoOffset);
    if (!all(isfinite(clip)) || clip.w <= RtaaEpsilon)
    {
        pixelCenter = 0.0f;
        return false;
    }

    float2 ndc = clip.xy / clip.w;
    pixelCenter = ndc * view.clipToWindowScale + view.clipToWindowBias;
    return all(isfinite(pixelCenter));
}

bool ReconstructCameraMotion(
    int2 sourcePixel,
    float sourceDepth,
    out float2 motion)
{
    motion = 0.0f;
    if (g_Rtaa.historyValid == 0u)
        return false;

    float3 worldPosition;
    if (!ReconstructWorldPosition(float2(sourcePixel) + 0.5f,
            sourceDepth, g_Rtaa.currentView, worldPosition))
    {
        return false;
    }

    float2 currentNoOffset;
    float2 previousNoOffset;
    if (!ProjectWorldNoOffset(worldPosition, g_Rtaa.currentView,
            currentNoOffset) ||
        !ProjectWorldNoOffset(worldPosition, g_Rtaa.immediateHistoryView,
            previousNoOffset))
    {
        return false;
    }

    motion = previousNoOffset - currentNoOffset;
    return all(isfinite(motion));
}

bool ReconstructBackgroundMotion(int2 pixel, out float2 motion)
{
    motion = 0.0f;
    if (g_Rtaa.historyValid == 0u)
        return false;

    float3 rayPoint;
    if (!ReconstructWorldPosition(float2(pixel) + 0.5f, 0.5f,
            g_Rtaa.currentView, rayPoint))
    {
        return false;
    }

    float3 cameraOrigin =
        g_Rtaa.currentView.cameraDirectionOrPosition.xyz;
    float3 direction = rayPoint - cameraOrigin;
    float lengthSquared = dot(direction, direction);
    if (!isfinite(lengthSquared) || lengthSquared <= RtaaEpsilon)
        return false;

    float3 distantWorld = cameraOrigin + direction *
        (100000.0f * rsqrt(lengthSquared));
    float2 currentNoOffset;
    float2 previousNoOffset;
    if (!ProjectWorldNoOffset(distantWorld, g_Rtaa.currentView,
            currentNoOffset) ||
        !ProjectWorldNoOffset(distantWorld,
            g_Rtaa.immediateHistoryView, previousNoOffset))
    {
        return false;
    }

    motion = previousNoOffset - currentNoOffset;
    return all(isfinite(motion));
}

float ComputeAuthoredThinCandidate(
    float4 centerDiffuse,
    float4 centerSpecular)
{
    if (g_Rtaa.enableThinGeometry == 0u)
        return 0.0f;

    uint centerFeatureMask = (uint)round(
        saturate(centerSpecular.a) * 255.0f);
    uint thinFeatures = RtaaFeatureTranslucency |
        RtaaFeatureRefraction | RtaaFeatureDoubleSided;
    float authoredThin = (centerFeatureMask & thinFeatures) != 0u
        ? 1.0f : 0.0f;
    float alphaThin = saturate(
        (1.0f - saturate(centerDiffuse.a)) * 2.0f);
    return saturate(max(authoredThin, alphaThin));
}
#endif

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

float3 SanitizedRgbToCompressedYCoCg(float3 rgb)
{
    float y = dot(rgb, float3(0.25f, 0.50f, 0.25f));
    float co = 0.5f * (rgb.r - rgb.b);
    float cg = -0.25f * rgb.r + 0.5f * rgb.g - 0.25f * rgb.b;
    float compression = rcp(1.0f + y);
    return float3(log2(1.0f + y), co * compression, cg * compression);
}

float3 RgbToCompressedYCoCg(float3 rgb)
{
    return SanitizedRgbToCompressedYCoCg(SanitizeHdr(rgb));
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
    float2 exactCenter = floor(pixelCenter) + 0.5f;
    if (all(abs(pixelCenter - exactCenter) < 1e-5f))
    {
        float4 exact = textureObject.Load(int3(int2(floor(pixelCenter)), 0));
        exact.rgb = SanitizeHdr(exact.rgb);
        exact.a = isfinite(exact.a) ? saturate(exact.a) : 0.0f;
        return exact;
    }
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
    float actualDeviceDepth,
    float expectedDeviceDepth,
    PlanarViewConstants historyView,
    float toleranceScale)
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
    threshold *= clamp(toleranceScale, 1.0f, 2.0f);
    float viewError = abs(actualViewDepth - expectedViewDepth);
    float viewConfidence = 1.0f - SmoothRange(
        viewError, threshold, threshold * 2.0f);
    return saturate(viewConfidence);
}

float ComputeGrazingDepthToleranceScale(
    float3 worldPosition,
    float3 geometricNormal,
    PlanarViewConstants historyView)
{
    // A point-loaded auxiliary depth represents the nearest historical texel,
    // while resolved color is sampled fractionally. Bound the resulting
    // one-texel error on grazing planes without reviving the former redundant
    // world-position reconstruction/comparison.
    float3 viewDirection = historyView.cameraDirectionOrPosition.w > 0.5f
        ? SafeNormal(historyView.cameraDirectionOrPosition.xyz - worldPosition)
        : SafeNormal(-historyView.cameraDirectionOrPosition.xyz);
    float grazingCosine = abs(dot(SafeNormal(geometricNormal), viewDirection));
    return lerp(2.0f, 1.0f,
        SmoothRange(grazingCosine, 0.05f, 0.35f));
}

uint TileIndex(int2 tilePixel)
{
    return uint(tilePixel.y) * RtaaTileSize + uint(tilePixel.x);
}

float3 LoadSharedRgb(uint tileIndex)
{
    return CompressedYCoCgToRgb(s_CurrentCompressedColor[tileIndex].rgb);
}

float CompressedYCoCgLuminance(float3 value)
{
    // Recover Rec.709 luma directly from compressed YCoCg without rebuilding
    // three RGB channels: L = Y + (1 + Y) * (0.1404 Co + 0.4304 Cg).
    float y = max(exp2(value.x) - 1.0f, 0.0f);
    float luminance = y + (1.0f + y) *
        (0.1404f * value.y + 0.4304f * value.z);
    return isfinite(luminance) ? max(luminance, 0.0f) : 0.0f;
}

float LoadSharedLuminance(uint tileIndex)
{
#if RTAA_PERFORMANCE_TIER == 0
    return s_CurrentLuminance[tileIndex];
#else
    return CompressedYCoCgLuminance(
        s_CurrentCompressedColor[tileIndex].rgb);
#endif
}

float3 LoadSharedTileSample(
    int2 sourcePixel,
    uint tileIndex,
    out float4 loadedSpecular)
{
    float3 color = SanitizeHdr(
        t_CurrentColor.Load(int3(sourcePixel, 0)).rgb);
    // color was sanitized above; avoid repeating the finite/max checks in the
    // transform for every cooperatively loaded shared-tile sample.
    float3 compressedColor = SanitizedRgbToCompressedYCoCg(color);
    float deviceDepth = t_CurrentDepth.Load(int3(sourcePixel, 0));
    bool depthValid = IsValidDeviceDepth(deviceDepth);
    float3 normal = 0.0f;
    float viewDepth = 0.0f;
    uint2 surface = uint2(0xffffffffu, 0u);
    loadedSpecular = 0.0f;
#if RTAA_PERFORMANCE_TIER > 0
    float authoredThinCandidate = 0.0f;
#endif
    if (depthValid)
    {
        loadedSpecular = t_CurrentSpecular.Load(int3(sourcePixel, 0));
        normal = DecodeOctahedralNormal(loadedSpecular.rg);
        if (!DeviceDepthToViewDepth(deviceDepth,
                g_Rtaa.currentView, viewDepth))
        {
            viewDepth = 0.0f;
        }
        surface = t_CurrentSurfaceIds.Load(int3(sourcePixel, 0));
#if RTAA_PERFORMANCE_TIER > 0
        authoredThinCandidate = saturate(
            t_Classification.Load(int3(sourcePixel, 0)).a);
#endif
    }

    s_CurrentCompressedColor[tileIndex] = float4(
        compressedColor, deviceDepth);
#if RTAA_PERFORMANCE_TIER == 0
    s_CurrentLuminance[tileIndex] = Luminance(color);
#endif
    s_NormalViewDepth[tileIndex] = float4(normal, viewDepth);
    s_SurfaceIds[tileIndex] = surface;
#if RTAA_PERFORMANCE_TIER > 0
    s_ThinCandidate[tileIndex] = authoredThinCandidate;
#endif
#if RTAA_PERFORMANCE_TIER >= 2
    s_CombinedThinCandidate[tileIndex] = authoredThinCandidate;
#endif
    return color;
}

void LoadSharedTile(
    uint2 groupId,
    uint2 threadId,
    uint linearThreadIndex,
    out float3 centerColor,
    out float4 centerSpecular)
{
    int2 groupOrigin = int2(groupId * RtaaGroupSize);
    int2 maximumPixel = int2(g_Rtaa.resolution) - 1;

    // Every lane owns its output-center load. Keeping the raw value in a
    // register avoids decoding compressed YCoCg for the final blend without
    // adding a texture fetch; the remaining lanes cooperatively load only halo.
    int2 centerSourcePixel = clamp(groupOrigin + int2(threadId),
        0, maximumPixel);
    uint centerTileIndex = TileIndex(
        int2(threadId) + int(RtaaTileHalo));
    centerColor = LoadSharedTileSample(
        centerSourcePixel, centerTileIndex, centerSpecular);

    const uint TopBottomElementCount = RtaaTileHalo * RtaaTileSize;
    const uint SideElementCount = RtaaGroupSize * 2u * RtaaTileHalo;
    const uint HaloElementCount =
        2u * TopBottomElementCount + SideElementCount;
    for (uint haloIndex = linearThreadIndex;
        haloIndex < HaloElementCount;
        haloIndex += RtaaGroupSize * RtaaGroupSize)
    {
        int2 tilePixel;
        if (haloIndex < TopBottomElementCount)
        {
            tilePixel = int2(haloIndex % RtaaTileSize,
                haloIndex / RtaaTileSize);
        }
        else if (haloIndex < TopBottomElementCount + SideElementCount)
        {
            uint sideIndex = haloIndex - TopBottomElementCount;
            uint row = sideIndex / (2u * RtaaTileHalo);
            uint column = sideIndex % (2u * RtaaTileHalo);
            uint x = column < RtaaTileHalo
                ? column
                : RtaaTileHalo + RtaaGroupSize +
                    (column - RtaaTileHalo);
            tilePixel = int2(x, RtaaTileHalo + row);
        }
        else
        {
            uint bottomIndex = haloIndex -
                TopBottomElementCount - SideElementCount;
            tilePixel = int2(bottomIndex % RtaaTileSize,
                RtaaTileHalo + RtaaGroupSize +
                    bottomIndex / RtaaTileSize);
        }

        int2 sourcePixel = clamp(groupOrigin + tilePixel -
            int(RtaaTileHalo), 0, maximumPixel);
        float4 unusedSpecular;
        LoadSharedTileSample(sourcePixel, TileIndex(tilePixel),
            unusedSpecular);
    }
}

#if RTAA_FUSED_PREPARE
struct FusedPrepareResult
{
    float2 motion;
    float4 classification;
    float4 selectedRawMotion;
};

FusedPrepareResult ComputeFusedPrepare(
    int2 centerPixel,
    int2 tileCenter,
    float centerDepth,
    bool centerDepthValid,
    float4 centerSpecular)
{
    FusedPrepareResult result;
    result.motion = 0.0f;
    result.classification = 0.0f;
    result.selectedRawMotion = 0.0f;

    int2 sourcePixel = centerPixel;
    float sourceDepth = centerDepth;
    bool sourceDepthValid = centerDepthValid;

    // Match Prepare's nearest valid 3x3 owner, but source depth is already in
    // the resolve tile. This removes up to eight texture reads at silhouettes.
    if (!sourceDepthValid && g_Rtaa.enableVelocityDilation != 0u)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0)
                    continue;
                int2 candidatePixel = centerPixel + int2(x, y);
                if (!IsInside(candidatePixel))
                    continue;

                float candidateDepth = s_CurrentCompressedColor[
                    TileIndex(tileCenter + int2(x, y))].a;
                if (!IsValidDeviceDepth(candidateDepth))
                    continue;
                if (!sourceDepthValid ||
                    IsCloserDeviceDepth(candidateDepth, sourceDepth))
                {
                    sourcePixel = candidatePixel;
                    sourceDepth = candidateDepth;
                    sourceDepthValid = true;
                }
            }
        }
    }

    if (sourceDepthValid)
    {
        result.selectedRawMotion = t_RawMotion.Load(
            int3(sourcePixel, 0));
    }
    bool rawMotionValid = sourceDepthValid &&
        IsValidRawMotion(result.selectedRawMotion);
    float2 selectedMotion = rawMotionValid
        ? result.selectedRawMotion.xy : 0.0f;
    bool cameraReconstructed = false;
    if (!rawMotionValid && sourceDepthValid)
    {
        cameraReconstructed = ReconstructCameraMotion(
            sourcePixel, sourceDepth, selectedMotion);
    }
    bool backgroundReconstructed = false;
    if (!sourceDepthValid)
    {
        backgroundReconstructed = ReconstructBackgroundMotion(
            centerPixel, selectedMotion);
    }

    int2 sourceOffset = clamp(sourcePixel - centerPixel, -1, 1);
    bool sourceIsCenter = all(sourceOffset == 0);
    float velocityConfidence = 0.0f;
    if (rawMotionValid)
        velocityConfidence = sourceIsCenter ? 1.0f : 0.85f;
    else if (cameraReconstructed)
        velocityConfidence = sourceIsCenter ? 0.60f : 0.50f;
    else if (backgroundReconstructed)
        velocityConfidence = 0.75f;

    float reactive = g_Rtaa.enableExplicitReactive != 0u
        ? saturate(t_ExplicitReactive.Load(int3(centerPixel, 0)))
        : 0.0f;
    float thinCandidate = 0.0f;
    if (centerDepthValid)
    {
        float4 diffuse = t_CurrentDiffuse.Load(int3(centerPixel, 0));
        thinCandidate = ComputeAuthoredThinCandidate(
            diffuse, centerSpecular);
    }

    uint sourceIndex = uint(sourceOffset.y + 1) * 3u +
        uint(sourceOffset.x + 1);
    result.motion = selectedMotion;
    result.classification = saturate(float4(
        float(sourceIndex) * (1.0f / 8.0f),
        velocityConfidence, reactive, thinCandidate));
    return result;
}
#endif

void ComputeNeighborhoodStatistics(
    int2 tileCenter,
    out float3 mean,
    out float3 variance,
    out float3 lowerSample,
    out float3 upperSample
#if RTAA_ENABLE_DEBUG_OUTPUT
    ,
    out float3 rgbMean)
#else
    )
#endif
{
    // Accumulate moments around the center sample. E[(x-origin)^2] -
    // E[x-origin]^2 avoids the catastrophic cancellation of E[x^2]-E[x]^2
    // when native FP16 statistics see a nearly uniform bright HDR region.
    RtaaLowp3 origin = (RtaaLowp3)
        s_CurrentCompressedColor[TileIndex(tileCenter)].rgb;
    RtaaLowp3 deltaMean = (RtaaLowp3)0.0f;
    RtaaLowp3 deltaSecondMoment = (RtaaLowp3)0.0f;
    RtaaLowp3 localLower = (RtaaLowp3)65504.0f;
    RtaaLowp3 localUpper = (RtaaLowp3)-65504.0f;
#if RTAA_ENABLE_DEBUG_OUTPUT
    rgbMean = 0.0f;
#endif

    // Keep the complete 3x3 distribution in every tier. A cardinal-only cross
    // cannot see a diagonal silhouette or corner, so it can collapse min/max
    // to the center sample and turn temporal accumulation back into jittered
    // sample-following. The shared tile already contains transformed color, so
    // these four extra taps are LDS arithmetic rather than redundant RGB/log
    // transforms.
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            uint tileIndex = TileIndex(tileCenter + int2(x, y));
            RtaaLowp3 value = (RtaaLowp3)
                s_CurrentCompressedColor[tileIndex].rgb;
            RtaaLowp3 delta = value - origin;
            deltaMean += delta;
            deltaSecondMoment += delta * delta;
            localLower = min(localLower, value);
            localUpper = max(localUpper, value);
#if RTAA_ENABLE_DEBUG_OUTPUT
            rgbMean += LoadSharedRgb(tileIndex);
#endif
        }
    }
    const RtaaLowp InvNeighborhoodSampleCount =
        (RtaaLowp)(1.0f / 9.0f);
    deltaMean *= InvNeighborhoodSampleCount;
    deltaSecondMoment *= InvNeighborhoodSampleCount;
    RtaaLowp3 localMean = origin + deltaMean;
    RtaaLowp3 localVariance = max(
        deltaSecondMoment - deltaMean * deltaMean, (RtaaLowp3)0.0f);
    mean = (float3)localMean;
    variance = (float3)localVariance;
    lowerSample = (float3)localLower;
    upperSample = (float3)localUpper;
#if RTAA_ENABLE_DEBUG_OUTPUT
    rgbMean *= (float)InvNeighborhoodSampleCount;
#endif
}

float ComputeCoverageEdgeCandidate(
    int2 tileCenter,
    float centerViewDepth,
    float3 centerNormal,
    uint2 centerSurface)
{
    static const int2 Offsets[4] = {
        int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };
#if RTAA_PERFORMANCE_TIER == 0
    float centerLuma = LoadSharedLuminance(TileIndex(tileCenter));
#else
    float3 centerCompressed =
        s_CurrentCompressedColor[TileIndex(tileCenter)].rgb;
    float centerLuma = 0.0f;
    bool centerLumaLoaded = false;
#endif
    bool centerValid = centerViewDepth > 0.0f;
    RtaaLowp result = (RtaaLowp)0.0f;

    [unroll]
    for (uint neighborIndex = 0u; neighborIndex < 4u; ++neighborIndex)
    {
        uint index = TileIndex(tileCenter + Offsets[neighborIndex]);
        float4 neighborNormalDepth = s_NormalViewDepth[index];
        bool neighborValid = neighborNormalDepth.w > 0.0f;
        float depthEdge = centerValid != neighborValid ? 1.0f : 0.0f;
        if (centerValid && neighborValid)
        {
            float relativeDepth = abs(neighborNormalDepth.w - centerViewDepth) /
                max(centerViewDepth, 0.1f);
            depthEdge = saturate(relativeDepth /
                max(g_Rtaa.thinDepthThreshold, 0.005f));
        }

        RtaaLowp normalEdge = (RtaaLowp)(centerValid && neighborValid
            ? saturate((1.0f - dot(centerNormal,
                neighborNormalDepth.xyz)) * 4.0f) : depthEdge);
        uint2 neighborSurface = s_SurfaceIds[index];
        RtaaLowp identityEdge = (RtaaLowp)(
            (g_Rtaa.enableMaterialValidation != 0u &&
                neighborSurface.x != centerSurface.x) ||
            (g_Rtaa.enableObjectValidation != 0u &&
                neighborSurface.y != centerSurface.y) ? 1.0f : 0.0f);
#if RTAA_PERFORMANCE_TIER == 0
        float neighborLuma = LoadSharedLuminance(index);
        RtaaLowp lumaEdge = (RtaaLowp)saturate(
            abs(neighborLuma - centerLuma) /
            max(1.0f + max(neighborLuma, centerLuma), RtaaEpsilon) /
            max(g_Rtaa.thinContrastThreshold, 0.05f));
#else
        // Uniform interiors are common in close wall views. Equal compressed
        // samples have exactly equal Rec.709 luminance, so avoid up to five
        // exp2 operations without changing the coverage result.
        float3 neighborCompressed =
            s_CurrentCompressedColor[index].rgb;
        RtaaLowp lumaEdge = (RtaaLowp)0.0f;
        if (any(neighborCompressed != centerCompressed))
        {
            if (!centerLumaLoaded)
            {
                centerLuma = CompressedYCoCgLuminance(centerCompressed);
                centerLumaLoaded = true;
            }
            float neighborLuma =
                CompressedYCoCgLuminance(neighborCompressed);
            lumaEdge = (RtaaLowp)saturate(
                abs(neighborLuma - centerLuma) /
                max(1.0f + max(neighborLuma, centerLuma), RtaaEpsilon) /
                max(g_Rtaa.thinContrastThreshold, 0.05f));
        }
#endif
        result = max(result, max(max((RtaaLowp)depthEdge, normalEdge),
            max(identityEdge, lumaEdge)));
    }
    return (float)saturate(result);
}

float ComputeCoverageHistoryNeighborMatch(
    int2 tileCenter,
    bool historyDepthValid,
    float3 historyNormal,
    uint historyMaterialId,
    uint historyObjectToken)
{
    RtaaLowp result = (RtaaLowp)0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
                continue;

            uint index = TileIndex(tileCenter + int2(x, y));
            float4 neighborNormalDepth = s_NormalViewDepth[index];
            bool neighborDepthValid = neighborNormalDepth.w > 0.0f;
            if (!historyDepthValid)
            {
                result = max(result, (RtaaLowp)(
                    neighborDepthValid ? 0.0f : 1.0f));
                continue;
            }
            if (!neighborDepthValid)
                continue;

            uint2 neighborSurface = s_SurfaceIds[index];
            float materialMatch = g_Rtaa.enableMaterialValidation == 0u ||
                neighborSurface.x == historyMaterialId ? 1.0f : 0.0f;
            float objectMatch = g_Rtaa.enableObjectValidation == 0u ||
                HashToWord(neighborSurface.y) == historyObjectToken ? 1.0f : 0.0f;
            float normalMatch = SmoothRange(dot(neighborNormalDepth.xyz,
                historyNormal), g_Rtaa.normalRejectCosine,
                g_Rtaa.normalAcceptCosine);
            result = max(result, (RtaaLowp)(
                materialMatch * objectMatch * normalMatch));
        }
    }
    return (float)saturate(result);
}

float3 ClipLineToBox(
    float3 history,
    float3 center,
    float3 lowerBound,
    float3 upperBound)
{
    // Stable interior history is already the exact clipped result. Avoid three
    // component divisions on the overwhelmingly common planar-wall path while
    // preserving the legacy path for any non-finite input.
    if (all(isfinite(history)) && all(isfinite(lowerBound)) &&
        all(isfinite(upperBound)) && all(history >= lowerBound) &&
        all(history <= upperBound))
    {
        return history;
    }

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
#if RTAA_PERFORMANCE_TIER == 0
    return 0.0f;
#else
    if (g_Rtaa.enableThinGeometry == 0u || centerViewDepth <= 0.0f)
        return 0.0f;

    static const int2 PairOffsets[8] = {
        int2(-1, 0), int2(1, 0),
        int2(0, -1), int2(0, 1),
        int2(-1, -1), int2(1, 1),
        int2(1, -1), int2(-1, 1)
    };

    float centerLuma = LoadSharedLuminance(TileIndex(tileCenter));
    float result = 0.0f;
    [unroll]
    for (uint pairIndex = 0u; pairIndex < 4u; ++pairIndex)
    {
        uint index0 = TileIndex(tileCenter + PairOffsets[pairIndex * 2u]);
        uint index1 = TileIndex(tileCenter + PairOffsets[pairIndex * 2u + 1u]);
        float4 neighbor0 = s_NormalViewDepth[index0];
        float4 neighbor1 = s_NormalViewDepth[index1];
        bool valid0 = neighbor0.w > 0.0f;
        bool valid1 = neighbor1.w > 0.0f;

        // A planar depth slope has near-zero second derivative and must not be
        // mistaken for a thin strip. Curvature or two genuine opposing surface
        // changes are required.
        float curvature = valid0 && valid1
            ? saturate(abs(neighbor0.w + neighbor1.w -
                2.0f * centerViewDepth) / max(centerViewDepth, 0.1f) /
                max(g_Rtaa.thinDepthThreshold, RtaaEpsilon))
            : (!valid0 && !valid1 ? 1.0f : 0.0f);

        uint2 surface0 = s_SurfaceIds[index0];
        uint2 surface1 = s_SurfaceIds[index1];
        RtaaLowp identity0 = (RtaaLowp)(
            (g_Rtaa.enableMaterialValidation != 0u &&
                surface0.x != centerSurface.x) ||
            (g_Rtaa.enableObjectValidation != 0u &&
                surface0.y != centerSurface.y) ? 1.0f : 0.0f);
        RtaaLowp identity1 = (RtaaLowp)(
            (g_Rtaa.enableMaterialValidation != 0u &&
                surface1.x != centerSurface.x) ||
            (g_Rtaa.enableObjectValidation != 0u &&
                surface1.y != centerSurface.y) ? 1.0f : 0.0f);
        RtaaLowp normal0 = (RtaaLowp)(valid0
            ? saturate((1.0f - dot(centerNormal, neighbor0.xyz)) * 4.0f)
            : 1.0f);
        RtaaLowp normal1 = (RtaaLowp)(valid1
            ? saturate((1.0f - dot(centerNormal, neighbor1.xyz)) * 4.0f)
            : 1.0f);
        float luma0 = LoadSharedLuminance(index0);
        float luma1 = LoadSharedLuminance(index1);
        RtaaLowp contrast0 = (RtaaLowp)saturate(abs(luma0 - centerLuma) /
            max(1.0f + max(luma0, centerLuma), RtaaEpsilon) /
            max(g_Rtaa.thinContrastThreshold, RtaaEpsilon));
        RtaaLowp contrast1 = (RtaaLowp)saturate(abs(luma1 - centerLuma) /
            max(1.0f + max(luma1, centerLuma), RtaaEpsilon) /
            max(g_Rtaa.thinContrastThreshold, RtaaEpsilon));
        RtaaLowp authored0 = (RtaaLowp)max(
            s_ThinCandidate[TileIndex(tileCenter)],
            s_ThinCandidate[index0]);
        RtaaLowp authored1 = (RtaaLowp)max(
            s_ThinCandidate[TileIndex(tileCenter)],
            s_ThinCandidate[index1]);
        RtaaLowp edge0 = max(max(identity0, normal0),
            max(contrast0, authored0));
        RtaaLowp edge1 = max(max(identity1, normal1),
            max(contrast1, authored1));
        result = max(result, min(edge0, edge1) * max((RtaaLowp)curvature,
            min(max(identity0, authored0), max(identity1, authored1))));
    }
    return (float)saturate(result);
#endif
}

#if RTAA_PERFORMANCE_TIER >= 2
void BuildSharedThinCandidates(uint2 threadId)
{
    // Diffusion reads one candidate beyond every 8x8 group edge. Populate that
    // complete 10x10 footprint cooperatively; leaving the 36 halo entries as
    // authored-only creates visible group-phase changes in structural thin
    // evidence. The outermost 12x12 load halo supplies every neighbor needed
    // to classify these entries without any additional texture fetch.
    static const uint CandidateExtent = RtaaGroupSize + 2u;
    static const uint CandidateCount = CandidateExtent * CandidateExtent;
    uint linearThreadIndex = threadId.y * RtaaGroupSize + threadId.x;
    for (uint candidateIndex = linearThreadIndex;
        candidateIndex < CandidateCount;
        candidateIndex += RtaaGroupSize * RtaaGroupSize)
    {
        int2 tilePixel = int2(
            candidateIndex % CandidateExtent,
            candidateIndex / CandidateExtent) +
            int(RtaaTileHalo - 1u);
        uint tileIndex = TileIndex(tilePixel);
        float4 normalDepth = s_NormalViewDepth[tileIndex];
        float structural = ComputeStructuralThinCandidate(
            tilePixel, normalDepth.w, normalDepth.xyz,
            s_SurfaceIds[tileIndex]);
        s_CombinedThinCandidate[tileIndex] = max(
            s_ThinCandidate[tileIndex], structural);
    }
}
#endif

float DiffuseThinCandidate(
    int2 tileCenter,
    float centerViewDepth,
    float3 centerNormal,
    uint2 centerSurface,
    float centerCandidate)
{
    RtaaLowp result = (RtaaLowp)centerCandidate;
#if RTAA_PERFORMANCE_TIER < 2
    return g_Rtaa.enableThinGeometry != 0u ? result : 0.0f;
#else
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
            RtaaLowp candidate = (RtaaLowp)s_CombinedThinCandidate[index];
            float4 neighborNormalDepth = s_NormalViewDepth[index];
            if (candidate <= 0.0f || neighborNormalDepth.w <= 0.0f)
                continue;

            float relativeDepthDifference = abs(neighborNormalDepth.w -
                centerViewDepth) / max(centerViewDepth, 0.1f);
            RtaaLowp depthGate = (RtaaLowp)(1.0f - saturate(
                relativeDepthDifference /
                max(g_Rtaa.thinDepthThreshold, RtaaEpsilon)));
            RtaaLowp normalGate = (RtaaLowp)SmoothRange(dot(centerNormal,
                neighborNormalDepth.xyz), g_Rtaa.normalRejectCosine,
                g_Rtaa.normalAcceptCosine);
            uint2 neighborSurface = s_SurfaceIds[index];
            RtaaLowp materialGate = (RtaaLowp)(
                g_Rtaa.enableMaterialValidation == 0u ||
                neighborSurface.x == centerSurface.x ? 1.0f : 0.0f);
            RtaaLowp objectGate = (RtaaLowp)(
                g_Rtaa.enableObjectValidation == 0u ||
                neighborSurface.y == centerSurface.y ? 1.0f : 0.0f);
            RtaaLowp distanceGate = (RtaaLowp)(
                (abs(x) + abs(y)) == 1 ? 0.85f : 0.70f);
            result = max(result, candidate * depthGate * normalGate *
                materialGate * objectGate * distanceGate);
        }
    }
    return (float)saturate(result);
#endif
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

    float centerLogLuma = log2(1.0f +
        LoadSharedLuminance(TileIndex(tileCenter)));
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
        float lumaEdge = abs(log2(1.0f +
            LoadSharedLuminance(index)) - centerLogLuma);
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

#if RTAA_PERFORMANCE_TIER == 0
    static const int2 CrossOffsets[5] = {
        int2(0, 0), int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };
    float3 weightedColor = centerColor;
    float totalWeight = 1.0f;
    [unroll]
    for (uint sampleIndex = 1u; sampleIndex < 5u; ++sampleIndex)
    {
        uint index = TileIndex(tileCenter + CrossOffsets[sampleIndex]);
        float4 neighborNormalDepth = s_NormalViewDepth[index];
        uint2 neighborSurface = s_SurfaceIds[index];
        if (neighborNormalDepth.w <= 0.0f ||
            (g_Rtaa.enableMaterialValidation != 0u &&
                neighborSurface.x != centerSurface.x) ||
            (g_Rtaa.enableObjectValidation != 0u &&
                neighborSurface.y != centerSurface.y))
        {
            continue;
        }
        float relativeDepthDifference = abs(neighborNormalDepth.w -
            centerViewDepth) / max(centerViewDepth, 0.1f);
        float normalDifference = 1.0f - saturate(dot(
            centerNormal, neighborNormalDepth.xyz));
        // Geometry-derived distances and tunable coefficients remain FP32.
        // Casting an extreme depth ratio to half before a zero coefficient can
        // create 0*Inf -> NaN and poison the fallback color.
        float weightDenominator = 2.0f +
            g_Rtaa.spatialDepthWeight * relativeDepthDifference +
            g_Rtaa.spatialNormalWeight * normalDifference;
        RtaaLowp weight = (RtaaLowp)(
            isfinite(weightDenominator) && weightDenominator > RtaaEpsilon
                ? rcp(weightDenominator) : 0.0f);
        weightedColor += LoadSharedRgb(index) * (float)weight;
        totalWeight += (float)weight;
    }
    return SanitizeHdr(weightedColor / totalWeight);
#else
#if RTAA_PERFORMANCE_TIER == 1
    int radius = 1;
#else
    int radius = int(clamp(g_Rtaa.spatialFallbackRadius, 1u, 2u));
#endif
    float centerLogLuma = log2(1.0f + Luminance(centerColor));
    float3 weightedColor = 0.0f;
    float totalWeight = 0.0f;
    float2 edgeDirection;
    float edgeStrength;
#if RTAA_PERFORMANCE_TIER >= 2
    ComputeSharedEdgeDirection(tileCenter, centerViewDepth, centerNormal,
        centerSurface, edgeDirection, edgeStrength);
#else
    edgeDirection = 0.0f;
    edgeStrength = 0.0f;
#endif

    [unroll]
#if RTAA_PERFORMANCE_TIER >= 2
    for (int y = -2; y <= 2; ++y)
#else
    for (int y = -1; y <= 1; ++y)
#endif
    {
        [unroll]
#if RTAA_PERFORMANCE_TIER >= 2
        for (int x = -2; x <= 2; ++x)
#else
        for (int x = -1; x <= 1; ++x)
#endif
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

            float3 neighborColor = LoadSharedRgb(index);
            float relativeDepthDifference = abs(neighborNormalDepth.w -
                centerViewDepth) / max(centerViewDepth, 0.1f);
            float normalDifference = 1.0f - saturate(dot(
                centerNormal, neighborNormalDepth.xyz));
            float lumaDifference = abs(log2(1.0f +
                Luminance(neighborColor)) - centerLogLuma);
            float distanceSquared = float(x * x + y * y);
            RtaaLowp neighborThinCoverage =
                (RtaaLowp)s_ThinCandidate[index];
            RtaaLowp thinActivity = saturate(max(
                (RtaaLowp)centerThinCoverage, neighborThinCoverage) *
                (RtaaLowp)4.0f);
            RtaaLowp thinAgreement = (RtaaLowp)1.0f - saturate(abs(
                neighborThinCoverage - (RtaaLowp)centerThinCoverage) *
                (RtaaLowp)2.0f);
            RtaaLowp thinGate = lerp((RtaaLowp)1.0f,
                thinAgreement, thinActivity);

            float2 offsetDirection = distanceSquared > 0.0f
                ? float2(x, y) * rsqrt(distanceSquared) : 0.0f;
            float crossEdge = abs(dot(offsetDirection, edgeDirection));
            RtaaLowp directionGate = max((RtaaLowp)1.0f -
                (RtaaLowp)0.90f * (RtaaLowp)edgeStrength *
                    (RtaaLowp)crossEdge, (RtaaLowp)0.05f);

            // Reciprocal bilateral weights are deterministic, cheap on low-end
            // GPUs, and avoid per-pixel transcendental evaluation.
            float weightDenominator = 1.0f +
                0.75f * distanceSquared +
                g_Rtaa.spatialDepthWeight * relativeDepthDifference +
                g_Rtaa.spatialNormalWeight * normalDifference +
                g_Rtaa.spatialLumaWeight * lumaDifference;
            RtaaLowp weight = (RtaaLowp)(
                isfinite(weightDenominator) &&
                    weightDenominator > RtaaEpsilon
                    ? rcp(weightDenominator) : 0.0f);
            weight *= thinGate * directionGate;
            weightedColor += neighborColor * (float)weight;
            totalWeight += (float)weight;
        }
    }

    return totalWeight > RtaaEpsilon
        ? SanitizeHdr(weightedColor / totalWeight)
        : centerColor;
#endif
}

float ComputeAutomaticReactive(
    float3 unclippedHistory,
    float3 historyInsideBroadNeighborhood)
{
    if (g_Rtaa.enableAutomaticReactive == 0u)
        return 0.0f;

    // Only unexplained history outside the broad current 3x3 range is reactive.
    // Ordinary jittered edge/texture variation lies inside that neighborhood
    // and must accumulate instead of being mistaken for animated shading.
    RtaaLowp3 residual = (RtaaLowp3)(
        unclippedHistory - historyInsideBroadNeighborhood);
    RtaaLowp lumaDifference = abs(residual.x);
    RtaaLowp chromaDifference = length(residual.yz);
    RtaaLowp lumaThreshold =
        (RtaaLowp)g_Rtaa.automaticReactiveLumaThreshold;
    RtaaLowp chromaThreshold =
        (RtaaLowp)g_Rtaa.automaticReactiveChromaThreshold;
    RtaaLowp lumaReactive = SmoothRangeLowp(lumaDifference,
        lumaThreshold, lumaThreshold * (RtaaLowp)2.0f);
    RtaaLowp chromaReactive = SmoothRangeLowp(chromaDifference,
        chromaThreshold, chromaThreshold * (RtaaLowp)2.0f);
    RtaaLowp strength = max(
        (RtaaLowp)g_Rtaa.automaticReactiveStrength, (RtaaLowp)0.0f);
    return (float)saturate(max(lumaReactive, chromaReactive) * strength);
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

    float2 historyColorCenter;
    float2 historyAuxiliaryCenter;
    float expectedDepth;
    if (!ProjectWorldToHistory(currentWorld, historyView,
            historyColorCenter, historyAuxiliaryCenter, expectedDepth))
    {
        return result;
    }

    bool colorInside = all(historyColorCenter >= 0.5f) &&
        all(historyColorCenter <= g_Rtaa.resolution - 0.5f);
    bool auxiliaryInside = all(historyAuxiliaryCenter >= 0.5f) &&
        all(historyAuxiliaryCenter <= g_Rtaa.resolution - 0.5f);
    if (!colorInside || !auxiliaryInside)
        return result;

    int2 historyColorPixel = clamp(int2(floor(historyColorCenter)), 0,
        int2(g_Rtaa.resolution) - 1);
    int2 historyAuxiliaryPixel = clamp(int2(floor(historyAuxiliaryCenter)), 0,
        int2(g_Rtaa.resolution) - 1);
    float actualDepth = depthTexture.Load(int3(historyAuxiliaryPixel, 0));
    float depthConfidence = ComputeDepthConfidence(
        actualDepth, expectedDepth, historyView,
        ComputeGrazingDepthToleranceScale(
            currentWorld, currentNormal, historyView));
    if (depthConfidence <= 0.0f)
        return result;

    uint2 storedSurface = surfaceTexture.Load(int3(historyAuxiliaryPixel, 0));
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
        historyColorCenter);
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

    float4 metadata = metadataTexture.Load(int3(historyColorPixel, 0));
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

#if RTAA_ENABLE_DEBUG_OUTPUT
float3 VisualizeMotion(float2 motion)
{
    float scale = rcp(max(max(g_Rtaa.motionWeightEndPixels, 8.0f), 1.0f));
    return saturate(float3(0.5f + motion * scale * 0.5f,
        length(motion) * scale));
}
#endif

[numthreads(8, 8, 1)]
void main(
    uint2 groupId : SV_GroupID,
    uint2 threadId : SV_GroupThreadID,
    uint2 pixel : SV_DispatchThreadID)
{
    uint linearThreadIndex = threadId.y * RtaaGroupSize + threadId.x;
    float3 currentColor;
    float4 centerSpecular;
    LoadSharedTile(groupId, threadId, linearThreadIndex,
        currentColor, centerSpecular);
    GroupMemoryBarrierWithGroupSync();
#if RTAA_PERFORMANCE_TIER >= 2
    BuildSharedThinCandidates(threadId);
    GroupMemoryBarrierWithGroupSync();
#endif

    // Every barrier selected by the static tier must execute for every lane.
    // Bounds rejection is therefore deliberately after the cooperative tile
    // and structural-classification phases, which also cover odd native
    // dimensions without out-of-range texture accesses.
    if (any(pixel >= uint2(g_Rtaa.resolution)))
        return;

    int2 outputPixel = int2(pixel);
    int2 tileCenter = int2(threadId) + int(RtaaTileHalo);
    uint centerTileIndex = TileIndex(tileCenter);
    float4 currentColorDepth = s_CurrentCompressedColor[centerTileIndex];
    float3 currentCompressedColor = currentColorDepth.rgb;
    float currentDeviceDepth = currentColorDepth.a;
    bool currentDepthValid = IsValidDeviceDepth(currentDeviceDepth);
    bool uniformPerformanceColorNeighborhood = false;
    float2 prepared;
    float4 classification;
    float4 preparedSelectedRawMotion = 0.0f;
#if RTAA_FUSED_PREPARE
    FusedPrepareResult fusedPrepare = ComputeFusedPrepare(outputPixel,
        tileCenter, currentDeviceDepth, currentDepthValid, centerSpecular);
    prepared = fusedPrepare.motion;
    classification = fusedPrepare.classification;
    preparedSelectedRawMotion = fusedPrepare.selectedRawMotion;
#else
    prepared = t_Prepared.Load(int3(outputPixel, 0));
    classification = saturate(
        t_Classification.Load(int3(outputPixel, 0)));
#endif
    uint sourceIndex = min((uint)round(classification.r * 8.0f), 8u);
    int2 sourceOffset = int2(int(sourceIndex % 3u) - 1,
        int(sourceIndex / 3u) - 1);
    int2 sourcePixel = clamp(outputPixel + sourceOffset, 0,
        int2(g_Rtaa.resolution) - 1);

    // Velocity dilation may borrow reprojection motion from a nearby closest
    // surface, but it must never replace the center pixel's geometry. Using
    // the borrowed depth/normal/IDs here would write foreground identity into
    // background history and let spatial fallback cross a silhouette.
    float currentViewDepth = s_NormalViewDepth[centerTileIndex].w;
    float3 currentNormal = s_NormalViewDepth[centerTileIndex].xyz;
    uint2 currentSurface = s_SurfaceIds[centerTileIndex];
    uint currentMaterialId = currentSurface.x;
    uint currentObjectToken = HashToWord(currentSurface.y);
    float velocityConfidence = classification.g;
    float2 dilatedMotion = prepared.xy;

    // Motion is de-jittered, so resolved color uses current+motion. Raw depth
    // and surface history were copied from the jittered G-buffer and must use a
    // separate coordinate with the previous-current jitter delta applied once.
    float2 previousPixelCenter = float2(outputPixel) + 0.5f + dilatedMotion;
    float2 previousAuxiliaryCenter = previousPixelCenter -
        g_Rtaa.currentJitter + g_Rtaa.previousJitter;

#if RTAA_ENABLE_DEBUG_OUTPUT
    bool backgroundProductionWritten = false;
    float3 backgroundProductionOutput = currentColor;
#endif
    bool pureBackground = !currentDepthValid;
    // Make the geometry fast path structural rather than relying on the
    // compiler to short-circuit a chained boolean expression. Opaque wall
    // lanes now issue none of these eight shared-depth reads.
    if (pureBackground)
    {
        [unroll]
        for (int backgroundY = -1; backgroundY <= 1; ++backgroundY)
        {
            [unroll]
            for (int backgroundX = -1; backgroundX <= 1; ++backgroundX)
            {
                if (backgroundX == 0 && backgroundY == 0)
                    continue;
                pureBackground = pureBackground &&
                    s_NormalViewDepth[TileIndex(tileCenter +
                        int2(backgroundX, backgroundY))].w <= 0.0f;
            }
        }
    }

    // Uniform clear-depth regions still accumulate the reprojected sky, but do
    // not pay geometric validation, neighborhood statistics, thin analysis, or
    // fallback. A historical geometry sample deliberately falls through so a
    // previous thin-coverage lock can resolve a subpixel disappearance.
    if (pureBackground)
    {
        bool backgroundColorInside = all(previousPixelCenter >= 0.5f) &&
            all(previousPixelCenter <= g_Rtaa.resolution - 0.5f);
        bool backgroundAuxiliaryInside = all(previousAuxiliaryCenter >= 0.5f) &&
            all(previousAuxiliaryCenter <= g_Rtaa.resolution - 0.5f);
        int2 backgroundColorPixel = clamp(int2(floor(previousPixelCenter)), 0,
            int2(g_Rtaa.resolution) - 1);
        int2 backgroundAuxiliaryPixel = clamp(
            int2(floor(previousAuxiliaryCenter)), 0,
            int2(g_Rtaa.resolution) - 1);
        float clearDepth = g_Rtaa.reverseZ != 0u ? 0.0f : 1.0f;
        float backgroundHistoryDepth = clearDepth;
        if (g_Rtaa.historyValid != 0u && backgroundAuxiliaryInside)
        {
            backgroundHistoryDepth = t_ImmediateHistoryDepth.Load(
                int3(backgroundAuxiliaryPixel, 0));
        }
        float backgroundResolvedDepth = backgroundHistoryDepth;
        if (g_Rtaa.historyValid != 0u && backgroundColorInside &&
            any(backgroundColorPixel != backgroundAuxiliaryPixel))
        {
            backgroundResolvedDepth = t_ImmediateHistoryDepth.Load(
                int3(backgroundColorPixel, 0));
        }

        bool backgroundColorHistoryAvailable = g_Rtaa.historyValid != 0u &&
            backgroundColorInside;
        bool backgroundHistoryUsable = backgroundColorHistoryAvailable &&
            backgroundAuxiliaryInside && velocityConfidence > 0.0f;
        float4 backgroundHistory = float4(currentColor, 0.0f);
        if (backgroundColorHistoryAvailable)
        {
            backgroundHistory = SampleHistoryColor(
                t_ImmediateHistoryColor, previousPixelCenter);
        }
        float4 backgroundMetadata = 0.0f;
        if (backgroundColorHistoryAvailable)
        {
            backgroundMetadata = saturate(
                t_ImmediateHistoryMetadata.Load(
                    int3(backgroundColorPixel, 0)));
        }

        bool historicalGeometry = g_Rtaa.historyValid != 0u &&
            ((backgroundAuxiliaryInside &&
                IsValidDeviceDepth(backgroundHistoryDepth)) ||
             (backgroundColorInside &&
                IsValidDeviceDepth(backgroundResolvedDepth)));
        // Coverage magnitude and lock lifetime are deliberately independent.
        // Residual alpha keeps the full path active long enough to clip a lock
        // that has expired; only metadata B can grant temporal trust.
        float backgroundLockLifetime = backgroundMetadata.b;
        if (backgroundLockLifetime <= 0.0f &&
            backgroundHistory.a > (1.0f / 255.0f))
        {
            backgroundLockLifetime = LoadInterpolatedStationaryThinLock(
                previousPixelCenter);
        }
        bool historicalCoverage = backgroundColorHistoryAvailable &&
            (backgroundLockLifetime > 0.0f ||
                backgroundHistory.a > (1.0f / 255.0f));
        if (!historicalGeometry && !historicalCoverage)
        {
#if RTAA_ENABLE_MOMENTS
            float2 backgroundMoments = 0.0f;
            if (g_Rtaa.historyValid != 0u && backgroundColorInside)
            {
                backgroundMoments = t_ImmediateHistoryMoments.Load(
                    int3(backgroundColorPixel, 0));
            }
#endif

            float backgroundMotionFactor = SmoothRange(length(dilatedMotion),
                g_Rtaa.motionWeightStartPixels,
                g_Rtaa.motionWeightEndPixels);
            float backgroundReactive = classification.b;
            float backgroundBaseWeight = lerp(g_Rtaa.stableCurrentWeight,
                g_Rtaa.movingCurrentWeight, backgroundMotionFactor);
            backgroundBaseWeight = lerp(backgroundBaseWeight,
                g_Rtaa.reactiveCurrentWeight, backgroundReactive);
            float backgroundMaximumSamples = max(1.0f, round(lerp(
                float(max(g_Rtaa.maxHistorySamples, 1u)),
                float(max(g_Rtaa.maxMovingHistorySamples, 1u)),
                backgroundMotionFactor)));
            float backgroundHistoryCount = backgroundMetadata.r *
                max(float(g_Rtaa.maxHistorySamples), 1.0f);
            float backgroundSampleWeight = rcp(min(backgroundHistoryCount,
                backgroundMaximumSamples) + 1.0f);
            float backgroundCurrentWeight = max(
                saturate(backgroundBaseWeight), backgroundSampleWeight);
            float stableBackground = (1.0f - backgroundMotionFactor) *
                (1.0f - backgroundReactive);
            backgroundCurrentWeight = lerp(backgroundCurrentWeight,
                backgroundSampleWeight, saturate(stableBackground));
            if (!backgroundHistoryUsable)
                backgroundCurrentWeight = 1.0f;

            float3 backgroundOutput = SanitizeHdr(lerp(
                backgroundHistory.rgb, currentColor,
                saturate(backgroundCurrentWeight)));
            bool backgroundAccepted = backgroundHistoryUsable &&
                backgroundCurrentWeight < 0.999f;
            float backgroundNewCount = backgroundAccepted
                ? min(backgroundHistoryCount + 1.0f,
                    backgroundMaximumSamples) : 1.0f;
            backgroundNewCount = lerp(backgroundNewCount, 1.0f,
                saturate(backgroundReactive * 0.5f));
#if RTAA_ENABLE_MOMENTS
            float backgroundLogLuma = log2(1.0f +
                Luminance(currentColor));
            float2 backgroundCurrentMoments = float2(backgroundLogLuma,
                backgroundLogLuma * backgroundLogLuma);
            float2 backgroundOutputMoments = backgroundAccepted
                ? lerp(backgroundMoments, backgroundCurrentMoments,
                    saturate(backgroundCurrentWeight))
                : backgroundCurrentMoments;
            if (!all(isfinite(backgroundOutputMoments)))
                backgroundOutputMoments = backgroundCurrentMoments;
#endif

            u_HistoryColor[pixel] = float4(backgroundOutput, 0.0f);
#if RTAA_ENABLE_MOMENTS
            u_HistoryMoments[pixel] = backgroundOutputMoments;
#endif
            u_HistoryMetadata[pixel] = saturate(float4(
                backgroundNewCount /
                    max(float(g_Rtaa.maxHistorySamples), 1.0f),
                backgroundHistoryUsable ? 1.0f : 0.0f,
                0.0f,
                EncodeReactiveAndMotion(backgroundReactive,
                    backgroundMotionFactor)));
            u_HistoryDepth[pixel] = clearDepth;
            u_HistorySurface[pixel] = uint2(0xffffffffu, 0u);
#if RTAA_ENABLE_DEBUG_OUTPUT
            backgroundProductionWritten = true;
            backgroundProductionOutput = backgroundOutput;
            if (g_Rtaa.writeDebug == 0u)
                return;
#else
            return;
#endif
        }
    }

    float3 currentWorld = 0.0f;
    bool currentWorldValid = false;
    float expectedPreviousDepth = 0.0f;
    bool expectedPreviousDepthValid = false;
    float4 selectedRawMotion = 0.0f;
#if RTAA_FUSED_PREPARE
    if (currentDepthValid)
        selectedRawMotion = preparedSelectedRawMotion;
#else
    if (currentDepthValid)
        selectedRawMotion = t_RawMotion.Load(int3(sourcePixel, 0));
#endif
    if (currentDepthValid && all(sourceOffset == 0) &&
        IsValidRawMotion(selectedRawMotion))
    {
        expectedPreviousDepth = currentDeviceDepth + selectedRawMotion.z;
        expectedPreviousDepthValid = IsValidDeviceDepth(expectedPreviousDepth);
    }
    if (!expectedPreviousDepthValid && currentDepthValid)
    {
        currentWorldValid = ReconstructWorldPosition(
            float2(outputPixel) + 0.5f, currentDeviceDepth,
            g_Rtaa.currentView, currentWorld);
        float2 projectedColorCenter;
        float2 projectedAuxiliaryCenter;
        expectedPreviousDepthValid = currentWorldValid &&
            ProjectWorldToHistory(currentWorld,
                g_Rtaa.immediateHistoryView, projectedColorCenter,
                projectedAuxiliaryCenter, expectedPreviousDepth) &&
                IsValidDeviceDepth(expectedPreviousDepth);
        if (expectedPreviousDepthValid)
        {
            previousPixelCenter = projectedColorCenter;
            previousAuxiliaryCenter = projectedAuxiliaryCenter;
        }
    }

    float2 previousUv = previousPixelCenter * g_Rtaa.invResolution;
    bool previousInside = all(previousPixelCenter >= 0.5f) &&
        all(previousPixelCenter <= g_Rtaa.resolution - 0.5f);
    bool previousAuxiliaryInside = all(previousAuxiliaryCenter >= 0.5f) &&
        all(previousAuxiliaryCenter <= g_Rtaa.resolution - 0.5f);
    int2 previousPixel = clamp(int2(floor(previousPixelCenter)), 0,
        int2(g_Rtaa.resolution) - 1);
    int2 previousAuxiliaryPixel = clamp(
        int2(floor(previousAuxiliaryCenter)), 0,
        int2(g_Rtaa.resolution) - 1);

#if RTAA_ENABLE_MOMENTS
    float2 historyMoments = 0.0f;
#endif
    float4 historyMetadata = 0.0f;
    if (g_Rtaa.historyValid != 0u && previousInside)
    {
#if RTAA_ENABLE_MOMENTS
        historyMoments = t_ImmediateHistoryMoments.Load(
            int3(previousPixel, 0));
#endif
        historyMetadata = saturate(t_ImmediateHistoryMetadata.Load(
            int3(previousPixel, 0)));
    }
    float historyDeviceDepth = g_Rtaa.reverseZ != 0u ? 0.0f : 1.0f;
    uint2 historySurface = uint2(0xffffffffu, 0u);
    if (g_Rtaa.historyValid != 0u && previousAuxiliaryInside)
    {
        historyDeviceDepth = t_ImmediateHistoryDepth.Load(
            int3(previousAuxiliaryPixel, 0));
        historySurface = t_ImmediateHistorySurface.Load(
            int3(previousAuxiliaryPixel, 0));
    }

    bool historyDepthValid = IsValidDeviceDepth(historyDeviceDepth);
    float grazingDepthScale = 1.0f;
    float depthConfidence = currentDepthValid && historyDepthValid &&
        previousAuxiliaryInside && expectedPreviousDepthValid
        ? ComputeDepthConfidence(historyDeviceDepth, expectedPreviousDepth,
            g_Rtaa.immediateHistoryView, grazingDepthScale) : 0.0f;
    if (depthConfidence < 1.0f && currentDepthValid && historyDepthValid &&
        previousAuxiliaryInside && expectedPreviousDepthValid)
    {
        if (!currentWorldValid)
        {
            currentWorldValid = ReconstructWorldPosition(
                float2(outputPixel) + 0.5f, currentDeviceDepth,
                g_Rtaa.currentView, currentWorld);
        }
        if (currentWorldValid)
        {
            grazingDepthScale = ComputeGrazingDepthToleranceScale(
                currentWorld, currentNormal, g_Rtaa.immediateHistoryView);
            depthConfidence = ComputeDepthConfidence(historyDeviceDepth,
                expectedPreviousDepth, g_Rtaa.immediateHistoryView,
                grazingDepthScale);
        }
    }
    float3 previousNormal;
    uint previousMaterialId;
    uint previousObjectToken;
    UnpackHistorySurface(historySurface, previousNormal,
        previousMaterialId, previousObjectToken);
    float normalConfidence = currentDepthValid && historyDepthValid &&
        previousAuxiliaryInside
        ? SmoothRange(dot(currentNormal, previousNormal),
            g_Rtaa.normalRejectCosine, g_Rtaa.normalAcceptCosine) : 0.0f;
    float rawMaterialMatch = currentMaterialId == previousMaterialId
        ? 1.0f : 0.0f;
    float rawObjectMatch = currentObjectToken == previousObjectToken
        ? 1.0f : 0.0f;
    float materialMatch = g_Rtaa.enableMaterialValidation == 0u
        ? 1.0f : rawMaterialMatch;
    float objectMatch = g_Rtaa.enableObjectValidation == 0u
        ? 1.0f : rawObjectMatch;

    float motionMagnitude = length(dilatedMotion);
    float motionFactor = SmoothRange(motionMagnitude,
        g_Rtaa.motionWeightStartPixels, g_Rtaa.motionWeightEndPixels);
    float coverageMotionGate = 1.0f - SmoothRange(motionMagnitude, 0.25f,
        max(g_Rtaa.motionWeightStartPixels, 0.75f));
    float geometricConfidence = saturate(velocityConfidence * depthConfidence *
        normalConfidence * materialMatch * objectMatch);
    float backgroundConfidence = !currentDepthValid && !historyDepthValid &&
        previousAuxiliaryInside
        ? lerp(velocityConfidence, 1.0f, coverageMotionGate) : 0.0f;
    float coverageEdge = ComputeCoverageEdgeCandidate(tileCenter,
        currentViewDepth, currentNormal, currentSurface);
    float previousCoverageLifetime = historyMetadata.b;
    float previousMotionFactor = DecodeMotionFactor(historyMetadata.a);
    if (g_Rtaa.historyValid != 0u && g_Rtaa.enableThinGeometry != 0u &&
        !currentDepthValid && previousInside &&
        previousCoverageLifetime <= 0.0f)
    {
        previousCoverageLifetime = LoadInterpolatedStationaryThinLock(
            previousPixelCenter);
        if (previousCoverageLifetime > 0.0f)
            previousMotionFactor = 0.0f;
    }
    float previousCoverageLock = 0.0f;
    if (g_Rtaa.historyValid != 0u && g_Rtaa.enableThinGeometry != 0u &&
        !currentDepthValid && previousInside &&
        previousCoverageLifetime > 0.0f && previousMotionFactor < 0.10f &&
        coverageMotionGate > 0.0f)
    {
        previousCoverageLock = 1.0f;
    }
    float coverageTransition = currentDepthValid != historyDepthValid ||
        (currentDepthValid && historyDepthValid &&
            (materialMatch == 0.0f || objectMatch == 0.0f)) ||
        previousCoverageLock > (1.0f / 255.0f) ? 1.0f : 0.0f;
    float borrowedCoverage = any(sourceOffset != 0) ? 1.0f : 0.0f;
    float coverageCandidate = max(max(coverageEdge, borrowedCoverage),
        previousCoverageLock);
    float coverageNeighborMatch = 0.0f;
    if (coverageTransition > 0.0f && coverageCandidate > 0.0f &&
        coverageMotionGate > 0.0f)
    {
        coverageNeighborMatch = ComputeCoverageHistoryNeighborMatch(
            tileCenter, historyDepthValid, previousNormal,
            previousMaterialId, previousObjectToken);
    }
    float coverageSupport = max(coverageNeighborMatch, previousCoverageLock);
    float coverageOverrideSignal = coverageTransition * coverageCandidate *
        coverageSupport * coverageMotionGate;
    float coverageConfidence = previousInside
        ? coverageOverrideSignal * velocityConfidence * 0.90f : 0.0f;
    float stableCoverageSignal = max(coverageEdge * geometricConfidence,
        coverageOverrideSignal);
    float combinedHistoryConfidence = g_Rtaa.historyValid != 0u &&
        previousInside ? saturate(max(geometricConfidence,
            max(backgroundConfidence, coverageConfidence))) : 0.0f;

    // Filtered color is issued only after cheap depth/surface/coverage gates.
    // Rejected disocclusions therefore avoid nine Catmull sampler operations.
    float4 historyColorSample = float4(currentColor, 0.0f);
    if (combinedHistoryConfidence > 0.0f)
    {
        historyColorSample = SampleHistoryColor(
            t_ImmediateHistoryColor, previousPixelCenter);
    }

    // An exactly matching recurrent sample is already inside every valid
    // neighborhood box and produces the same temporal RGB for any weight.
    // This common flat-wall/interior case can therefore skip the complete
    // statistics and clipping reduction without weakening rejection. Keep the
    // optimization production-only so diagnostic views still receive their
    // requested distribution, and do not mark the neighborhood uniform: a
    // low-confidence pixel must retain its surface-aware spatial fallback.
    bool exactStableInteriorHistory = false;
#if RTAA_PERFORMANCE_TIER == 0 && !RTAA_ENABLE_DEBUG_OUTPUT
    exactStableInteriorHistory = combinedHistoryConfidence > 0.0f &&
        all(historyColorSample.rgb == currentColor);
#endif

    // A hard rejection cannot consume clipped immediate history: its final
    // temporal weight is exactly zero. Skip all 3x3 statistics and clipping in
    // that case unless persistent resurrection or an active debug view needs
    // the distribution. Defaults describe the current sample exactly.
    bool neighborhoodRequired = combinedHistoryConfidence > 0.0f &&
        !exactStableInteriorHistory;
#if RTAA_RESURRECTION
    neighborhoodRequired = neighborhoodRequired ||
        (g_Rtaa.enableResurrection != 0u &&
            g_Rtaa.persistentValidMask != 0u && currentDepthValid);
#endif
#if RTAA_ENABLE_DEBUG_OUTPUT
    neighborhoodRequired = neighborhoodRequired || g_Rtaa.writeDebug != 0u;
#endif
    float3 neighborhoodMean = currentCompressedColor;
    float3 neighborhoodVariance = 0.0f;
    float3 neighborhoodMinimum = currentCompressedColor;
    float3 neighborhoodMaximum = currentCompressedColor;
#if RTAA_ENABLE_DEBUG_OUTPUT
    float3 neighborhoodRgbMean;
    neighborhoodRgbMean = currentColor;
#endif
    if (neighborhoodRequired)
    {
        ComputeNeighborhoodStatistics(tileCenter, neighborhoodMean,
            neighborhoodVariance, neighborhoodMinimum, neighborhoodMaximum
#if RTAA_ENABLE_DEBUG_OUTPUT
            , neighborhoodRgbMean
#endif
            );
#if RTAA_PERFORMANCE_TIER == 0
        // This reuses the reduction's min/max instead of adding another LDS
        // traversal. With FP32, equality is exact; native FP16 deliberately
        // treats sub-half-ULP variation as stable, matching the tier's already
        // quantized clip bounds and avoiding imperceptible fallback filtering.
        uniformPerformanceColorNeighborhood =
            all(neighborhoodMinimum == neighborhoodMaximum);
#endif
    }

    float3 unclippedHistoryYCoCg = exactStableInteriorHistory
        ? currentCompressedColor
        : combinedHistoryConfidence > 0.0f
        ? RgbToCompressedYCoCg(historyColorSample.rgb)
        : currentCompressedColor;
    float3 broadNeighborhoodHistory = combinedHistoryConfidence > 0.0f
        ? ClipLineToBox(unclippedHistoryYCoCg, neighborhoodMean,
            neighborhoodMinimum, neighborhoodMaximum)
        : currentCompressedColor;
    RtaaLowp explicitReactive = (RtaaLowp)classification.b;
    RtaaLowp automaticReactive = combinedHistoryConfidence > 0.0f &&
        motionFactor > 0.0f
        ? (RtaaLowp)ComputeAutomaticReactive(unclippedHistoryYCoCg,
            broadNeighborhoodHistory) * (RtaaLowp)motionFactor
        : (RtaaLowp)0.0f;
    RtaaLowp finalReactive = saturate(max(
        explicitReactive, automaticReactive));

#if RTAA_PERFORMANCE_TIER == 0
    // Prepare already loaded this center classification for the lane. Avoid a
    // redundant full-tile classification fetch and both shared thin arrays.
    RtaaLowp combinedThinCandidate = (RtaaLowp)classification.a;
#elif RTAA_PERFORMANCE_TIER == 1
    // Balanced does not diffuse candidates, so publishing a second shared
    // array cannot affect any other lane. Keep the structural result local.
    RtaaLowp combinedThinCandidate = (RtaaLowp)max(classification.a,
        ComputeStructuralThinCandidate(tileCenter, currentViewDepth,
            currentNormal, currentSurface));
#else
    RtaaLowp combinedThinCandidate =
        (RtaaLowp)s_CombinedThinCandidate[centerTileIndex];
#endif
    RtaaLowp thinCandidate = (RtaaLowp)DiffuseThinCandidate(tileCenter,
        currentViewDepth, currentNormal, currentSurface,
        (float)combinedThinCandidate);
    // Persist the already-computed silhouette evidence in every tier. This is
    // cheaper and more reliable than probing the filtered color footprint's
    // raw depth, and protects ordinary one-sided opaque edges as well as wires.
    thinCandidate = max(thinCandidate, (RtaaLowp)(
        currentDepthValid ? coverageEdge : 0.0f));
    RtaaLowp previousThinCoverage =
        (RtaaLowp)historyColorSample.a;
    RtaaLowp thinHistoryWeight = saturate(((RtaaLowp)1.0f -
        (RtaaLowp)g_Rtaa.thinCoverageCurrentWeight) *
        (RtaaLowp)combinedHistoryConfidence *
        ((RtaaLowp)1.0f - finalReactive));
    RtaaLowp accumulatedThinCoverage = g_Rtaa.enableThinGeometry != 0u
        ? saturate(lerp(thinCandidate, previousThinCoverage,
            thinHistoryWeight)) : (RtaaLowp)0.0f;
    if (!currentDepthValid && previousCoverageLifetime <= 0.0f)
    {
        // The full path has now clipped expired coverage color to background.
        // Clear residual alpha immediately so the next frame can return to the
        // uniform-background fast path instead of paying an exponential tail.
        accumulatedThinCoverage = (RtaaLowp)0.0f;
    }
    float currentCoverageLock = g_Rtaa.enableThinGeometry != 0u &&
        thinCandidate > (1.0f / 255.0f) ? 1.0f : 0.0f;
    float canInheritCoverageLock = !currentDepthValid ||
        geometricConfidence > 0.5f ? 1.0f : 0.0f;
    RtaaLowp newCoverageLifetime = (RtaaLowp)(
        g_Rtaa.enableThinGeometry != 0u
        ? (currentCoverageLock > 0.0f ? 1.0f :
            max(previousCoverageLifetime -
                g_Rtaa.thinLockDecayPerFrame, 0.0f) *
                canInheritCoverageLock) : 0.0f);
    newCoverageLifetime *= (RtaaLowp)coverageMotionGate *
        ((RtaaLowp)1.0f - finalReactive);
    // A trusted thin surface may receive a tiny, bounded confidence lift, but
    // cannot revive a hard depth/normal/identity rejection. Previous-frame
    // confidence prevents one-frame structural noise from opening the gate.
    RtaaLowp thinConfidenceBoost = (RtaaLowp)geometricConfidence *
        saturate(accumulatedThinCoverage) *
        saturate((RtaaLowp)g_Rtaa.thinMaxRelaxation) *
        (RtaaLowp)historyMetadata.g *
        ((RtaaLowp)1.0f - (RtaaLowp)combinedHistoryConfidence) *
        ((RtaaLowp)1.0f - finalReactive);
    combinedHistoryConfidence = saturate(
        combinedHistoryConfidence + (float)thinConfidenceBoost);

    float3 lowerBound = currentCompressedColor;
    float3 upperBound = currentCompressedColor;
    float3 immediateClippedHistory = currentColor;
    float clipDistance = 0.0f;
    if (neighborhoodRequired)
    {
        if (!uniformPerformanceColorNeighborhood)
        {
            float3 sigma = sqrt(max(neighborhoodVariance, 0.0f)) *
                max(g_Rtaa.varianceSigma, 0.0f);
            sigma.x *= max(g_Rtaa.varianceLumaScale, 0.0f);
            sigma.yz *= max(g_Rtaa.varianceChromaScale, 0.0f);
            lowerBound = max(neighborhoodMinimum, neighborhoodMean - sigma);
            upperBound = min(neighborhoodMaximum, neighborhoodMean + sigma);
            RtaaLowp clipExpansion = saturate(accumulatedThinCoverage *
                (RtaaLowp)g_Rtaa.thinClipExpansion *
                ((RtaaLowp)1.0f - finalReactive));
            lowerBound = lerp(lowerBound, neighborhoodMinimum,
                (float)clipExpansion);
            upperBound = lerp(upperBound, neighborhoodMaximum,
                (float)clipExpansion);
        }

        float3 clippedHistoryYCoCg = uniformPerformanceColorNeighborhood
            ? broadNeighborhoodHistory
            : ClipLineToBox(unclippedHistoryYCoCg,
                neighborhoodMean, lowerBound, upperBound);
        // If a stationary, previously classified thin feature is completely
        // absent from the current footprint, preserve only a bounded fraction
        // of trusted, stationary coverage history.
        float uncoveredCoveragePreservation = !currentDepthValid
            ? previousCoverageLock * coverageMotionGate *
                (1.0f - finalReactive) : 0.0f;
        clippedHistoryYCoCg = lerp(clippedHistoryYCoCg,
            unclippedHistoryYCoCg,
            saturate(uncoveredCoveragePreservation));
        immediateClippedHistory = CompressedYCoCgToRgb(
            clippedHistoryYCoCg);
        clipDistance = length(unclippedHistoryYCoCg -
            clippedHistoryYCoCg) /
            max(length(unclippedHistoryYCoCg - neighborhoodMean),
                RtaaEpsilon);
    }

    float3 historyForAccumulation = immediateClippedHistory;
#if RTAA_ENABLE_MOMENTS
    float2 historyMomentsForAccumulation = historyMoments;
#endif
    float effectiveHistoryCount = historyMetadata.r *
        max(float(g_Rtaa.maxHistorySamples), 1.0f);
    float resurrectionEligibility = 0.0f;
    float resurrectionSource = 0.0f;
    float resurrectionConfidence = 0.0f;
    float3 resurrectionColor = 0.0f;

#if RTAA_RESURRECTION
    // Persistent reprojection has no multi-frame object-motion chain. Restrict
    // it to surfaces whose immediate motion agrees with projecting the current
    // world position through the previous camera; otherwise a moving/skinned
    // object could resurrect stale same-material color from an older location.
    bool resurrectionRequested = g_Rtaa.enableResurrection != 0u &&
        g_Rtaa.persistentValidMask != 0u && currentDepthValid &&
        (combinedHistoryConfidence < g_Rtaa.resurrectionMatchThreshold ||
            clipDistance > 0.5f);
    if (resurrectionRequested && !currentWorldValid)
    {
        currentWorldValid = ReconstructWorldPosition(
            float2(outputPixel) + 0.5f, currentDeviceDepth,
            g_Rtaa.currentView, currentWorld);
    }
    float staticSurfaceForResurrection = 0.0f;
    if (resurrectionRequested && currentWorldValid &&
        expectedPreviousDepthValid)
    {
        float2 cameraOnlyColorCenter;
        float2 cameraOnlyAuxiliaryCenter;
        float cameraOnlyDepth;
        if (ProjectWorldToHistory(currentWorld,
                g_Rtaa.immediateHistoryView, cameraOnlyColorCenter,
                cameraOnlyAuxiliaryCenter, cameraOnlyDepth))
        {
            float cameraMotionError = length(cameraOnlyColorCenter -
                (float2(outputPixel) + 0.5f + dilatedMotion));
            float cameraGrazingDepthScale =
                ComputeGrazingDepthToleranceScale(currentWorld,
                    currentNormal, g_Rtaa.immediateHistoryView);
            float cameraDepthAgreement = ComputeDepthConfidence(
                expectedPreviousDepth, cameraOnlyDepth,
                g_Rtaa.immediateHistoryView, cameraGrazingDepthScale);
            staticSurfaceForResurrection =
                (cameraMotionError <= 0.125f && cameraDepthAgreement > 0.5f)
                ? 1.0f : 0.0f;
        }
    }
    resurrectionEligibility = resurrectionRequested && currentWorldValid &&
        staticSurfaceForResurrection > 0.0f ? 1.0f : 0.0f;
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
#if RTAA_ENABLE_MOMENTS
            float resurrectedLogLuma = log2(1.0f + Luminance(best.color));
            historyMomentsForAccumulation = float2(resurrectedLogLuma,
                resurrectedLogLuma * resurrectedLogLuma);
#endif
            effectiveHistoryCount = best.historyCount;
            resurrectionConfidence = best.confidence;
            resurrectionSource = float(best.sourceIndex);
            resurrectionColor = best.color;
            combinedHistoryConfidence = max(combinedHistoryConfidence,
                best.confidence);
        }
    }
#endif

    // Spatial work is delayed until resurrection has had a chance to restore a
    // valid temporal sample. This avoids paying the fallback only to discard it.
    RtaaLowp fallbackContribution = (RtaaLowp)(
        g_Rtaa.enableSpatialFallback != 0u
        ? 1.0f - SmoothRange(combinedHistoryConfidence, 0.25f, 0.75f)
        : 0.0f);
    float3 spatialFallback = fallbackContribution > 0.0f &&
        !uniformPerformanceColorNeighborhood
        ? ComputeSpatialFallback(tileCenter, currentColor, currentViewDepth,
            currentNormal, currentSurface, (float)accumulatedThinCoverage)
        : currentColor;
    float3 resolvedCurrent = SanitizeHdr(lerp(currentColor,
        spatialFallback, (float)fallbackContribution));

    RtaaLowp baseCurrentWeight = lerp(
        (RtaaLowp)g_Rtaa.stableCurrentWeight,
        (RtaaLowp)g_Rtaa.movingCurrentWeight,
        (RtaaLowp)motionFactor);
    baseCurrentWeight = lerp(baseCurrentWeight,
        (RtaaLowp)g_Rtaa.reactiveCurrentWeight, finalReactive);

    float maximumSamples = max(1.0f, round(lerp(
        float(max(g_Rtaa.maxHistorySamples, 1u)),
        float(max(g_Rtaa.maxMovingHistorySamples, 1u)), motionFactor)));
    RtaaLowp sampleLimitedWeight = rcp((RtaaLowp)(
        min(effectiveHistoryCount, maximumSamples) + 1.0f));
    RtaaLowp currentWeight = max(saturate(baseCurrentWeight),
        sampleLimitedWeight);
    currentWeight = lerp(currentWeight, (RtaaLowp)1.0f,
        (RtaaLowp)1.0f - saturate((RtaaLowp)combinedHistoryConfidence));
    // Once a stationary edge is geometrically validated or conservatively
    // recovered as a true coverage transition, acceptance confidence must not
    // reintroduce asymmetric sample-following. Apply count-driven convergence
    // after the disocclusion safety gate; real motion/reactivity fades it out.
    RtaaLowp stableCoverage = (RtaaLowp)stableCoverageSignal *
        ((RtaaLowp)1.0f - (RtaaLowp)motionFactor) *
        ((RtaaLowp)1.0f - finalReactive);
    currentWeight = lerp(currentWeight, sampleLimitedWeight,
        saturate(stableCoverage));
    if (resurrectionSource > 0.0f)
    {
        // Older history can help a rejected pixel, but stale illumination is
        // never allowed to dominate the immediate current sample.
        currentWeight = max(currentWeight, (RtaaLowp)1.0f -
            saturate((RtaaLowp)g_Rtaa.resurrectionMaxWeight));
    }

    float3 outputColor = SanitizeHdr(lerp(historyForAccumulation,
        resolvedCurrent, (float)saturate(currentWeight)));
    bool historyAccepted = combinedHistoryConfidence > 0.05f &&
        currentWeight < 0.999f;
    float newHistoryCount = historyAccepted
        ? min(effectiveHistoryCount + 1.0f, maximumSamples)
        : 1.0f;
    newHistoryCount = lerp(newHistoryCount, 1.0f,
        saturate(finalReactive * 0.5f));

#if RTAA_ENABLE_MOMENTS
    float currentLogLuma = log2(1.0f + Luminance(resolvedCurrent));
    float2 currentMoments = float2(currentLogLuma,
        currentLogLuma * currentLogLuma);
    float2 outputMoments = historyAccepted
        ? lerp(historyMomentsForAccumulation, currentMoments,
            (float)saturate(currentWeight))
        : currentMoments;
    if (!all(isfinite(outputMoments)))
        outputMoments = currentMoments;
#endif

    float metadataCountDenominator = max(
        float(g_Rtaa.maxHistorySamples), 1.0f);
    float4 outputMetadata = saturate(float4(
        newHistoryCount / metadataCountDenominator,
        combinedHistoryConfidence,
        newCoverageLifetime,
        EncodeReactiveAndMotion(finalReactive, motionFactor)));
    uint2 outputSurface = PackHistorySurface(currentNormal,
        currentMaterialId, currentObjectToken);

    // History writes are unconditional and precede debug selection. Debugging
    // therefore cannot freeze, replace, clamp, or otherwise contaminate the
    // state consumed by a later frame.
#if RTAA_ENABLE_DEBUG_OUTPUT
    if (!backgroundProductionWritten)
    {
#endif
        u_HistoryColor[pixel] = float4(outputColor,
            accumulatedThinCoverage);
#if RTAA_ENABLE_MOMENTS
        u_HistoryMoments[pixel] = outputMoments;
#endif
        u_HistoryMetadata[pixel] = outputMetadata;
        u_HistoryDepth[pixel] = IsValidDeviceDepth(currentDeviceDepth)
            ? currentDeviceDepth : (g_Rtaa.reverseZ != 0u ? 0.0f : 1.0f);
        u_HistorySurface[pixel] = outputSurface;
#if RTAA_ENABLE_DEBUG_OUTPUT
    }
#endif

#if RTAA_ENABLE_DEBUG_OUTPUT
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
            debugColor = float3((float2(sourceOffset) + 1.0f) * 0.5f,
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
            debugColor = backgroundProductionWritten
                ? backgroundProductionOutput : outputColor;
            break;
        }

        u_Debug[pixel] = float4(SanitizeHdr(debugColor), 1.0f);
    }
#endif
}
