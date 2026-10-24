#ifndef UVSR_VISIBILITY_HORIZON_SHARED_H
#define UVSR_VISIBILITY_HORIZON_SHARED_H

// Scalar and first moment math for the mask free Horizon estimator.

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
    float fullViewMoment;
    float fullSideMoment;
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
    float wedgeViewMoment;
    float wedgeSideMoment;
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
    float thetaMinusSinCos = VisibilityHorizonFastAcos(cosine) - sine * cosine;
    return 0.5f * (
        -receiverSideComponent * thetaMinusSinCos +
        receiverViewComponent * cosine * cosine);
}

UVSR_VISIBILITY_HORIZON_INLINE float VisibilityHorizonViewMomentPrimitive(
    float receiverViewComponent,
    float receiverSideComponent,
    float cosine,
    float sine)
{
    return (receiverViewComponent * (1.0f - cosine * cosine * cosine) +
        receiverSideComponent * sine * sine * sine) * (1.0f / 3.0f);
}

UVSR_VISIBILITY_HORIZON_INLINE float VisibilityHorizonSideMomentPrimitive(
    float receiverViewComponent,
    float receiverSideComponent,
    float cosine,
    float sine)
{
    return (receiverViewComponent * sine * sine * sine +
        receiverSideComponent *
            (2.0f - 3.0f * cosine + cosine * cosine * cosine)) *
        (1.0f / 3.0f);
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
    state.fullViewMoment = 0.0f;
    state.fullSideMoment = 0.0f;
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
    state.fullMass = VisibilityHorizonMax(viewPrimitive - tangentPrimitive, 0.0f);
    state.fullViewMoment = VisibilityHorizonViewMomentPrimitive(
        receiverViewComponent,
        receiverSideComponent,
        tangentCosine,
        tangentSine);
    state.fullSideMoment = VisibilityHorizonSideMomentPrimitive(
        receiverViewComponent,
        receiverSideComponent,
        tangentCosine,
        tangentSine);
    state.valid = VisibilityHorizonIsFinite(tangentPrimitive) &&
            VisibilityHorizonIsFinite(state.fullMass) &&
            VisibilityHorizonIsFinite(state.fullViewMoment) &&
            VisibilityHorizonIsFinite(state.fullSideMoment)
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
    result.wedgeViewMoment = 0.0f;
    result.wedgeSideMoment = 0.0f;
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
    float previousSine = VisibilityHorizonSqrt(VisibilityHorizonMax(
        1.0f - state.horizonCosine * state.horizonCosine, 0.0f));
    float previousViewMoment = VisibilityHorizonViewMomentPrimitive(
        state.receiverViewComponent,
        state.receiverSideComponent,
        state.horizonCosine,
        previousSine);
    float candidateViewMoment = VisibilityHorizonViewMomentPrimitive(
        state.receiverViewComponent,
        state.receiverSideComponent,
        candidate.cosine,
        candidate.sine);
    float previousSideMoment = VisibilityHorizonSideMomentPrimitive(
        state.receiverViewComponent,
        state.receiverSideComponent,
        state.horizonCosine,
        previousSine);
    float candidateSideMoment = VisibilityHorizonSideMomentPrimitive(
        state.receiverViewComponent,
        state.receiverSideComponent,
        candidate.cosine,
        candidate.sine);
    if (!VisibilityHorizonIsFinite(candidatePrimitive) ||
        !VisibilityHorizonIsFinite(previousViewMoment) ||
        !VisibilityHorizonIsFinite(candidateViewMoment) ||
        !VisibilityHorizonIsFinite(previousSideMoment) ||
        !VisibilityHorizonIsFinite(candidateSideMoment))
    {
        return result;
    }

    result.horizonCosine = candidate.cosine;
    result.horizonPrimitive = VisibilityHorizonMax(
        candidatePrimitive, state.horizonPrimitive);
    result.wedgeMass = result.horizonPrimitive - state.horizonPrimitive;
    result.wedgeViewMoment = previousViewMoment - candidateViewMoment;
    result.wedgeSideMoment = previousSideMoment - candidateSideMoment;
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
