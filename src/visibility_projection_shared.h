#ifndef UVSR_VISIBILITY_PROJECTION_SHARED_H
#define UVSR_VISIBILITY_PROJECTION_SHARED_H

// Compiled as both C++ and HLSL. The inputs are homogeneous D3D clip-space
// z/w values for a visible receiver and a desired radial endpoint. The result
// clips their segment once against the active near plane and positive-w domain.

#ifdef __cplusplus

using VisibilityProjectionUint = std::uint32_t;

inline bool VisibilityProjectionIsFinite(float value) noexcept
{
    return std::isfinite(value);
}

inline float VisibilityProjectionMin(float left, float right) noexcept
{
    return std::min(left, right);
}

inline float VisibilityProjectionMax(float left, float right) noexcept
{
    return std::max(left, right);
}

#define UVSR_VISIBILITY_PROJECTION_INLINE inline

#else

#define VisibilityProjectionUint uint

bool VisibilityProjectionIsFinite(float value)
{
    return isfinite(value);
}

float VisibilityProjectionMin(float left, float right)
{
    return min(left, right);
}

float VisibilityProjectionMax(float left, float right)
{
    return max(left, right);
}

#define UVSR_VISIBILITY_PROJECTION_INLINE

#endif

static const float VisibilityProjectionEpsilon = 1e-6f;
static const float VisibilityProjectionClipInset = 1e-5f;

struct VisibilityProjectionClipResult
{
    float endpointScale;
    VisibilityProjectionUint valid;
    VisibilityProjectionUint clipped;
};

UVSR_VISIBILITY_PROJECTION_INLINE float VisibilityProjectionNearDistance(
    float clipZ,
    float clipW,
    bool reverseDepth)
{
    // D3D NDC depth is [0, 1]. Forward depth reaches the near plane at z=0;
    // reverse depth reaches it at z=w.
    return reverseDepth ? clipW - clipZ : clipZ;
}

UVSR_VISIBILITY_PROJECTION_INLINE VisibilityProjectionClipResult
ComputeVisibilityProjectionEndpointClip(
    float receiverClipZ,
    float receiverClipW,
    float endpointClipZ,
    float endpointClipW,
    bool reverseDepth)
{
    VisibilityProjectionClipResult result;
    result.endpointScale = 0.0f;
    result.valid = 0u;
    result.clipped = 0u;

    if (!VisibilityProjectionIsFinite(receiverClipZ) ||
        !VisibilityProjectionIsFinite(receiverClipW) ||
        !VisibilityProjectionIsFinite(endpointClipZ) ||
        !VisibilityProjectionIsFinite(endpointClipW) ||
        !(receiverClipW > VisibilityProjectionEpsilon))
    {
        return result;
    }

    float receiverNearDistance = VisibilityProjectionNearDistance(
        receiverClipZ, receiverClipW, reverseDepth);
    float endpointNearDistance = VisibilityProjectionNearDistance(
        endpointClipZ, endpointClipW, reverseDepth);
    if (!VisibilityProjectionIsFinite(receiverNearDistance) ||
        !VisibilityProjectionIsFinite(endpointNearDistance) ||
        receiverNearDistance < -VisibilityProjectionEpsilon)
    {
        return result;
    }
    receiverNearDistance = VisibilityProjectionMax(receiverNearDistance, 0.0f);

    float endpointScale = 1.0f;
    if (endpointClipW <= VisibilityProjectionEpsilon)
    {
        float denominator = receiverClipW - endpointClipW;
        if (!(denominator > VisibilityProjectionEpsilon) ||
            !VisibilityProjectionIsFinite(denominator))
        {
            return result;
        }

        // Clip to a strictly positive w so the final perspective divide cannot
        // land exactly on the projection singularity.
        float positiveWScale =
            (receiverClipW - 2.0f * VisibilityProjectionEpsilon) /
            denominator;
        endpointScale = VisibilityProjectionMin(
            endpointScale, positiveWScale);
        result.clipped = 1u;
    }

    if (endpointNearDistance < 0.0f)
    {
        float denominator = receiverNearDistance - endpointNearDistance;
        if (!(denominator > VisibilityProjectionEpsilon) ||
            !VisibilityProjectionIsFinite(denominator))
        {
            return result;
        }

        float nearPlaneScale = receiverNearDistance / denominator;
        endpointScale = VisibilityProjectionMin(endpointScale, nearPlaneScale);
        result.clipped = 1u;
    }

    endpointScale = VisibilityProjectionMax(
        VisibilityProjectionMin(endpointScale, 1.0f), 0.0f);
    if (result.clipped != 0u)
    {
        // Move a tiny amount toward the already-valid receiver. This keeps the
        // reconstructed endpoint inside both half-spaces after float rounding.
        endpointScale *= 1.0f - VisibilityProjectionClipInset;
    }

    float clippedW = receiverClipW +
        (endpointClipW - receiverClipW) * endpointScale;
    float clippedZ = receiverClipZ +
        (endpointClipZ - receiverClipZ) * endpointScale;
    float clippedNearDistance = VisibilityProjectionNearDistance(
        clippedZ, clippedW, reverseDepth);
    if (!VisibilityProjectionIsFinite(clippedW) ||
        !VisibilityProjectionIsFinite(clippedZ) ||
        !VisibilityProjectionIsFinite(clippedNearDistance) ||
        !(clippedW > VisibilityProjectionEpsilon) ||
        clippedNearDistance < -VisibilityProjectionEpsilon)
    {
        return result;
    }

    result.endpointScale = endpointScale;
    result.valid = 1u;
    return result;
}

#ifndef __cplusplus
#undef VisibilityProjectionUint
#endif
#undef UVSR_VISIBILITY_PROJECTION_INLINE

#endif // UVSR_VISIBILITY_PROJECTION_SHARED_H
