#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "reconstructive_temporal_aa_cb.h"

cbuffer c_ReconstructiveTemporalAA : register(b0)
{
    ReconstructiveTemporalAAConstants g_Rtaa;
};

Texture2D<float>  t_Depth : register(t0);
Texture2D<float4> t_Motion : register(t1);
Texture2D<float4> t_HdrColor : register(t2);
Texture2D<float4> t_Normals : register(t3);
Texture2D<float4> t_Diffuse : register(t4);
Texture2D<float4> t_Specular : register(t5);
Texture2D<float4> t_Emissive : register(t6);
Texture2D<uint2>  t_SurfaceIds : register(t7);
Texture2D<float>  t_ExplicitReactive : register(t8);

// xy: de-jittered current-to-previous motion in pixels
// z:  device depth belonging to the selected velocity source
// w:  velocity confidence (center > dilated neighbor > camera reconstruction)
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> u_Prepared : register(u0);

// rg: selected source offset encoded from [-1, 1] to [0, 1]
// b:  explicit/material reactive classification
// a:  current-frame thin-geometry candidate
VK_IMAGE_FORMAT("rgba8") RWTexture2D<float4> u_Classification : register(u1);

static const float RtaaEpsilon = 1e-6f;
static const uint RtaaFeatureTranslucency = 1u << 2;
static const uint RtaaFeatureRefraction = 1u << 3;
static const uint RtaaFeatureScattering = 1u << 4;
static const uint RtaaFeatureThinFilm = 1u << 5;
static const uint RtaaFeatureAbsorption = 1u << 6;
static const uint RtaaFeatureDoubleSided = 1u << 7;

bool IsInside(int2 pixel)
{
    return all(pixel >= 0) && all(pixel < int2(g_Rtaa.resolution));
}

bool IsValidDeviceDepth(float depth)
{
    if (!isfinite(depth))
        return false;

    // The clear plane is excluded in both conventions. This makes background
    // velocity invalid instead of allowing a zero motion vector to masquerade
    // as a stable surface.
    return g_Rtaa.reverseZ != 0u
        ? depth > 0.0f && depth <= 1.0f
        : depth >= 0.0f && depth < 1.0f;
}

bool IsCloser(float candidate, float reference)
{
    return g_Rtaa.reverseZ != 0u
        ? candidate > reference
        : candidate < reference;
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

bool IsValidMotion(float4 motion)
{
    // A zero vector is a valid static-surface velocity. Validity comes from
    // the producer's alpha bit, finite values, and a valid owning depth, never
    // from motion magnitude. Behind-camera vertices explicitly write alpha 0.
    float2 generousLimit = max(g_Rtaa.resolution * 2.0f, 1.0f);
    return motion.w > 0.5f && all(isfinite(motion.xyz)) &&
        all(abs(motion.xy) <= generousLimit);
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

    // Match donut/shaders/motion_vectors.hlsli exactly: motion is de-jittered
    // current-to-previous displacement in pixel units. Resolve consequently
    // uses previous = current + motion and must not add a jitter delta again.
    motion = previousNoOffset - currentNoOffset;
    return all(isfinite(motion));
}

float ComputeReactiveClassification(
    int2 pixel,
    float3 hdrColor,
    float4 diffuse,
    float4 specular,
    float4 emissive)
{
    float explicitReactive = g_Rtaa.enableExplicitReactive != 0u
        ? saturate(t_ExplicitReactive.Load(int3(pixel, 0)))
        : 0.0f;

    uint featureMask = (uint)round(saturate(specular.a) * 255.0f);
    uint reactiveFeatures = RtaaFeatureTranslucency | RtaaFeatureRefraction |
        RtaaFeatureScattering | RtaaFeatureThinFilm | RtaaFeatureAbsorption;
    float materialReactive = (featureMask & reactiveFeatures) != 0u
        ? 0.75f : 0.0f;

    // Alpha and emissive classifications are intentionally bounded. They raise
    // current-frame contribution but do not by themselves invalidate geometry;
    // Resolve keeps geometric confidence and shading reactivity independent.
    float alphaReactive = saturate((1.0f - saturate(diffuse.a)) * 2.0f);
    float emissiveLuma = Luminance(SanitizeHdr(emissive.rgb));
    float sceneLuma = Luminance(hdrColor);
    float emissiveReactive = saturate(emissiveLuma /
        max(1.0f + sceneLuma, RtaaEpsilon));

    return max(max(explicitReactive, materialReactive),
        max(alphaReactive, emissiveReactive));
}

float ComputeAuthoredThinCandidate(
    float4 centerDiffuse,
    float4 centerSpecular)
{
    if (g_Rtaa.enableThinGeometry == 0u)
        return 0.0f;

    uint centerFeatureMask = (uint)round(saturate(centerSpecular.a) * 255.0f);
    uint thinFeatures = RtaaFeatureTranslucency | RtaaFeatureRefraction |
        RtaaFeatureDoubleSided;
    float authoredThin = (centerFeatureMask & thinFeatures) != 0u
        ? 1.0f : 0.0f;
    float alphaThin = saturate((1.0f - saturate(centerDiffuse.a)) * 2.0f);

    // Structural/opposing-edge evidence is derived in Resolve from its one
    // cooperatively loaded shared tile. Keeping Prepare center-only removes a
    // second full 3x3 color/depth/normal/material neighborhood from bandwidth.
    return saturate(max(authoredThin, alphaThin));
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(g_Rtaa.resolution)))
        return;

    int2 centerPixel = int2(pixel);
    int2 sourcePixel = centerPixel;
    float centerDepth = t_Depth.Load(int3(centerPixel, 0));
    float sourceDepth = centerDepth;
    bool sourceDepthValid = IsValidDeviceDepth(sourceDepth);

    // Velocity dilation selects the nearest geometric owner, never the largest
    // vector. Largest-velocity dilation pulls foreground motion across unrelated
    // background and is a primary source of silhouette ghosting.
    if (g_Rtaa.enableVelocityDilation != 0u)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                int2 candidatePixel = centerPixel + int2(x, y);
                if (!IsInside(candidatePixel))
                    continue;

                float candidateDepth = t_Depth.Load(int3(candidatePixel, 0));
                if (!IsValidDeviceDepth(candidateDepth))
                    continue;

                if (!sourceDepthValid || IsCloser(candidateDepth, sourceDepth))
                {
                    sourcePixel = candidatePixel;
                    sourceDepth = candidateDepth;
                    sourceDepthValid = true;
                }
            }
        }
    }

    float4 rawMotion = t_Motion.Load(int3(sourcePixel, 0));
    bool rawMotionValid = sourceDepthValid && IsValidMotion(rawMotion);
    float2 selectedMotion = rawMotionValid ? rawMotion.xy : 0.0f;
    bool cameraReconstructed = false;
    if (!rawMotionValid && sourceDepthValid)
    {
        cameraReconstructed = ReconstructCameraMotion(
            sourcePixel, sourceDepth, selectedMotion);
    }

    int2 sourceOffset = clamp(sourcePixel - centerPixel, -1, 1);
    bool sourceIsCenter = all(sourceOffset == 0);
    float velocityConfidence = 0.0f;
    if (rawMotionValid)
        velocityConfidence = sourceIsCenter ? 1.0f : 0.85f;
    else if (cameraReconstructed)
        velocityConfidence = sourceIsCenter ? 0.60f : 0.50f;

    float3 hdrColor = SanitizeHdr(t_HdrColor.Load(int3(centerPixel, 0)).rgb);
    float4 diffuse = t_Diffuse.Load(int3(centerPixel, 0));
    float4 specular = t_Specular.Load(int3(centerPixel, 0));
    float4 emissive = t_Emissive.Load(int3(centerPixel, 0));
    float3 normal = SafeNormal(t_Normals.Load(int3(centerPixel, 0)).xyz);
    uint2 surface = t_SurfaceIds.Load(int3(centerPixel, 0));

    float reactive = ComputeReactiveClassification(
        centerPixel, hdrColor, diffuse, specular, emissive);
    float thinCandidate = ComputeAuthoredThinCandidate(diffuse, specular);

    float2 encodedOffset = (float2(sourceOffset) + 1.0f) * 0.5f;
    float storedDepth = sourceDepthValid ? sourceDepth : 0.0f;
    u_Prepared[pixel] = float4(
        selectedMotion, storedDepth, saturate(velocityConfidence));
    u_Classification[pixel] = saturate(float4(
        encodedOffset, reactive, thinCandidate));
}
