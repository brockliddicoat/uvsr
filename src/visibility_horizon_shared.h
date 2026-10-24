#ifndef UVSR_VISIBILITY_HORIZON_SHARED_H
#define UVSR_VISIBILITY_HORIZON_SHARED_H

// Shared scalar math for the Horizon Approximation estimator. Keeping this
// header independent of radial_visibility_mask.hlsli is part of the estimator's
// compile-time contract: a Horizon permutation has no visibility intervals,
// sectors, masks, or population counts.

#ifdef __cplusplus

using VisibilityHorizonUint = std::uint32_t;

inline float VisibilityHorizonMin(float left, float right) noexcept
{
    return std::min(left, right);
}

inline float VisibilityHorizonMax(float left, float right) noexcept
{
    return std::max(left, right);
}

inline float VisibilityHorizonAbs(float value) noexcept
{
    return std::abs(value);
}

inline float VisibilityHorizonSqrt(float value) noexcept
{
    return std::sqrt(value);
}

inline bool VisibilityHorizonIsFinite(float value) noexcept
{
    return std::isfinite(value);
}

#define UVSR_VISIBILITY_HORIZON_INLINE inline

#else

#define VisibilityHorizonUint uint

float VisibilityHorizonMin(float left, float right)
{
    return min(left, right);
}

float VisibilityHorizonMax(float left, float right)
{
    return max(left, right);
}

float VisibilityHorizonAbs(float value)
{
    return abs(value);
}

float VisibilityHorizonSqrt(float value)
{
    return sqrt(value);
}

bool VisibilityHorizonIsFinite(float value)
{
    return isfinite(value);
}

#define UVSR_VISIBILITY_HORIZON_INLINE

#endif

static const float VisibilityHorizonPi = 3.14159265358979323846f;
static const float VisibilityHorizonHalfPi = 1.57079632679489661923f;
static const float VisibilityHorizonEpsilon = 1e-6f;

struct VisibilityHorizonSideState
{
    float receiverViewComponent;
    float receiverSideComponent;
    float horizonCosine;
    float horizonPrimitive;
    float fullMass;
    VisibilityHorizonUint valid;
};

struct VisibilityHorizonCandidate
{
    float cosine;
    float sine;
    VisibilityHorizonUint valid;
};

struct VisibilityHorizonRaiseResult
{
    float horizonCosine;
    float horizonPrimitive;
    float wedgeMass;
    VisibilityHorizonUint raised;
};

UVSR_VISIBILITY_HORIZON_INLINE float VisibilityHorizonClamp(
    float value,
    float minimum,
    float maximum)
{
    return VisibilityHorizonMin(VisibilityHorizonMax(value, minimum), maximum);
}

UVSR_VISIBILITY_HORIZON_INLINE float VisibilityHorizonSaturate(float value)
{
    return VisibilityHorizonClamp(value, 0.0f, 1.0f);
}

UVSR_VISIBILITY_HORIZON_INLINE float VisibilityHorizonFastAcos(float value)
{
    // The existing UVSR signed approximation is retained here so the Horizon
    // primitive pays one square root and a short polynomial only on skyline
    // raises. Mirroring preserves the full [-1, 1] tangent-horizon domain.
    float clamped = VisibilityHorizonClamp(value, -1.0f, 1.0f);
    float x = VisibilityHorizonAbs(clamped);
    float result = (-0.156583f * x + VisibilityHorizonHalfPi) *
        VisibilityHorizonSqrt(VisibilityHorizonMax(1.0f - x, 0.0f));
    return clamped >= 0.0f ? result : VisibilityHorizonPi - result;
}

UVSR_VISIBILITY_HORIZON_INLINE float VisibilityHorizonPrimitive(
    float receiverViewComponent,
    float receiverSideComponent,
    float cosine,
    float sine)
{
    // Mayaux's receiver-cosine solid-angle integral, expressed as a cumulative
    // primitive F(c). F(new) - F(old) is the complete wedge newly claimed by a
    // horizon raise; the projected receiver-normal length is intentionally not
    // normalized away.
    float thetaMinusSinCos = VisibilityHorizonFastAcos(cosine) - sine * cosine;
    return 0.5f * (
        -receiverSideComponent * thetaMinusSinCos +
        receiverViewComponent * cosine * cosine);
}

UVSR_VISIBILITY_HORIZON_INLINE VisibilityHorizonSideState
InitializeVisibilityHorizonSide(
    float receiverViewComponent,
    float receiverSideComponent)
{
    VisibilityHorizonSideState state;
    state.receiverViewComponent = receiverViewComponent;
    state.receiverSideComponent = receiverSideComponent;
    state.horizonCosine = 1.0f;
    state.horizonPrimitive = 0.0f;
    state.fullMass = 0.0f;
    state.valid = 0u;

    float projectedLengthSquared =
        receiverViewComponent * receiverViewComponent +
        receiverSideComponent * receiverSideComponent;
    if (!(receiverViewComponent >= 0.0f) ||
        !(projectedLengthSquared >
            VisibilityHorizonEpsilon * VisibilityHorizonEpsilon) ||
        !VisibilityHorizonIsFinite(projectedLengthSquared))
    {
        return state;
    }

    float inverseProjectedLength = 1.0f /
        VisibilityHorizonSqrt(projectedLengthSquared);
    float tangentCosine = VisibilityHorizonClamp(
        -receiverSideComponent * inverseProjectedLength, -1.0f, 1.0f);
    float tangentSine = VisibilityHorizonSaturate(
        receiverViewComponent * inverseProjectedLength);
    float tangentPrimitive = VisibilityHorizonPrimitive(
        receiverViewComponent,
        receiverSideComponent,
        tangentCosine,
        tangentSine);
    float viewPrimitive = 0.5f * receiverViewComponent;

    state.horizonCosine = tangentCosine;
    state.horizonPrimitive = tangentPrimitive;
    state.fullMass = VisibilityHorizonMax(
        viewPrimitive - tangentPrimitive, 0.0f);
    state.valid = VisibilityHorizonIsFinite(tangentPrimitive) &&
            VisibilityHorizonIsFinite(state.fullMass)
        ? 1u
        : 0u;
    return state;
}

UVSR_VISIBILITY_HORIZON_INLINE VisibilityHorizonCandidate
BuildVisibilityHorizonCandidate(
    float viewComponent,
    float sideComponent,
    float inverseDistance,
    float thicknessWorld)
{
    VisibilityHorizonCandidate candidate;
    candidate.cosine = -1.0f;
    candidate.sine = 0.0f;
    candidate.valid = 0u;

    float projectedLengthSquared =
        viewComponent * viewComponent + sideComponent * sideComponent;
    if (!(sideComponent >= 0.0f) ||
        !(projectedLengthSquared >
            VisibilityHorizonEpsilon * VisibilityHorizonEpsilon) ||
        !VisibilityHorizonIsFinite(projectedLengthSquared))
    {
        return candidate;
    }

    float inverseProjectedLength = 1.0f /
        VisibilityHorizonSqrt(projectedLengthSquared);
    candidate.cosine = VisibilityHorizonClamp(
        viewComponent * inverseProjectedLength, -1.0f, 1.0f);
    candidate.sine = VisibilityHorizonSaturate(
        sideComponent * inverseProjectedLength);

    if (thicknessWorld > 0.0f)
    {
        // A heightfield has no exact thickness. This optional first-order
        // angular dilation is monotonic, uses the already available inverse
        // distance, and never constructs a back endpoint or interval.
        float angularDilation = VisibilityHorizonSaturate(
            thicknessWorld * inverseDistance);
        candidate.cosine = VisibilityHorizonMin(
            candidate.cosine + angularDilation * candidate.sine, 1.0f);
        candidate.sine = VisibilityHorizonSqrt(
            VisibilityHorizonMax(
                1.0f - candidate.cosine * candidate.cosine,
                0.0f));
    }

    candidate.valid = VisibilityHorizonIsFinite(candidate.cosine) &&
            VisibilityHorizonIsFinite(candidate.sine)
        ? 1u
        : 0u;
    return candidate;
}

UVSR_VISIBILITY_HORIZON_INLINE VisibilityHorizonRaiseResult
EvaluateVisibilityHorizonRaise(
    VisibilityHorizonSideState state,
    VisibilityHorizonCandidate candidate)
{
    VisibilityHorizonRaiseResult result;
    result.horizonCosine = state.horizonCosine;
    result.horizonPrimitive = state.horizonPrimitive;
    result.wedgeMass = 0.0f;
    result.raised = 0u;

    if (state.valid == 0u || candidate.valid == 0u ||
        !(candidate.cosine > state.horizonCosine))
    {
        return result;
    }

    float candidatePrimitive = VisibilityHorizonPrimitive(
        state.receiverViewComponent,
        state.receiverSideComponent,
        candidate.cosine,
        candidate.sine);
    if (!VisibilityHorizonIsFinite(candidatePrimitive))
        return result;

    result.horizonCosine = candidate.cosine;
    result.horizonPrimitive = VisibilityHorizonMax(
        candidatePrimitive, state.horizonPrimitive);
    result.wedgeMass = result.horizonPrimitive - state.horizonPrimitive;
    result.raised = 1u;
    return result;
}

UVSR_VISIBILITY_HORIZON_INLINE float VisibilityHorizonRadiusFalloff(
    float distanceSquared,
    float radiusSquared)
{
    if (!(radiusSquared > VisibilityHorizonEpsilon * VisibilityHorizonEpsilon))
        return 0.0f;
    return VisibilityHorizonSaturate(1.0f - distanceSquared / radiusSquared);
}

#undef UVSR_VISIBILITY_HORIZON_INLINE

#endif
