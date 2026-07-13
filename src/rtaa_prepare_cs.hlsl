#pragma pack_matrix(row_major)

#include <donut/shaders/binding_helpers.hlsli>
#include "reconstructive_temporal_aa_cb.h"

cbuffer c_ReconstructiveTemporalAA : register(b0)
{
    ReconstructiveTemporalAAConstants g_Rtaa;
};

Texture2D<float>  t_Depth : register(t0);
Texture2D<float4> t_Motion : register(t1);
Texture2D<float4> t_Diffuse : register(t2);
Texture2D<float4> t_Specular : register(t3);
Texture2D<float>  t_ExplicitReactive : register(t4);

// rg: de-jittered current-to-previous motion in pixels. Source ownership and
// confidence are packed into Classification, reducing this transient to 4 B/px.
VK_IMAGE_FORMAT("rg16f") RWTexture2D<float2> u_Prepared : register(u0);

// r: selected 3x3 source index [0,8] encoded to UNORM (center = 4)
// g: velocity confidence (center > dilated neighbor > camera reconstruction)
// b: explicit application-authored reactive classification
// a: current-frame thin-geometry candidate
VK_IMAGE_FORMAT("rgba8") RWTexture2D<float4> u_Classification : register(u1);

static const float RtaaEpsilon = 1e-6f;
static const uint RtaaFeatureTranslucency = 1u << 2;
static const uint RtaaFeatureRefraction = 1u << 3;
static const uint RtaaFeatureScattering = 1u << 4;
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

bool ReconstructBackgroundMotion(int2 pixel, out float2 motion)
{
    motion = 0.0f;
    if (g_Rtaa.historyValid == 0u)
        return false;

    // Reconstruct a ray at a finite reference depth, then project a distant
    // point. Camera translation becomes negligible while rotation remains
    // exact enough for procedural sky/background reprojection.
    float3 rayPoint;
    if (!ReconstructWorldPosition(float2(pixel) + 0.5f, 0.5f,
            g_Rtaa.currentView, rayPoint))
    {
        return false;
    }

    float3 cameraOrigin = g_Rtaa.currentView.cameraDirectionOrPosition.xyz;
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
        !ProjectWorldNoOffset(distantWorld, g_Rtaa.immediateHistoryView,
            previousNoOffset))
    {
        return false;
    }

    motion = previousNoOffset - currentNoOffset;
    return all(isfinite(motion));
}

float ComputeReactiveClassification(int2 pixel)
{
    // Material feature presence is not temporal change. The explicit input is
    // reserved for a producer that knows a pixel has animated shading,
    // composition without reliable depth/motion, or another concrete contract
    // violation. Final-color neighborhood residuals are handled in Resolve.
    return g_Rtaa.enableExplicitReactive != 0u
        ? saturate(t_ExplicitReactive.Load(int3(pixel, 0)))
        : 0.0f;
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
    bool centerDepthValid = IsValidDeviceDepth(centerDepth);
    float sourceDepth = centerDepth;
    bool sourceDepthValid = centerDepthValid;

    // A valid center surface owns its own motion. Searching for a merely closer
    // neighbor on every sloped wall both wastes eight depth reads and borrows the
    // wrong velocity over broad regions. Dilation is reserved for uncovered
    // background/silhouette samples where no center surface exists.
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

    float4 rawMotion = 0.0f;
    if (sourceDepthValid)
        rawMotion = t_Motion.Load(int3(sourcePixel, 0));
    bool rawMotionValid = sourceDepthValid && IsValidMotion(rawMotion);
    float2 selectedMotion = rawMotionValid ? rawMotion.xy : 0.0f;
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

    float reactive = 0.0f;
    float thinCandidate = 0.0f;
    if (centerDepthValid)
    {
        float4 diffuse = t_Diffuse.Load(int3(centerPixel, 0));
        float4 specular = t_Specular.Load(int3(centerPixel, 0));
        reactive = ComputeReactiveClassification(centerPixel);
        thinCandidate = ComputeAuthoredThinCandidate(diffuse, specular);
    }

    uint sourceIndex = uint(sourceOffset.y + 1) * 3u +
        uint(sourceOffset.x + 1);
    float encodedSourceIndex = float(sourceIndex) * (1.0f / 8.0f);
    u_Prepared[pixel] = selectedMotion;
    u_Classification[pixel] = saturate(float4(
        encodedSourceIndex, velocityConfidence, reactive, thinCandidate));
}
