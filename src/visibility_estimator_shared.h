#ifndef UVSR_VISIBILITY_ESTIMATOR_SHARED_H
#define UVSR_VISIBILITY_ESTIMATOR_SHARED_H

// This header is compiled as both C++ and HLSL. Keep the executable estimator
// equations here so the CPU reference and production shader cannot silently
// drift in sign, CDF, sector-mass, or normalization conventions.

#ifdef __cplusplus

using VisibilityEstimatorUint = std::uint32_t;

struct VisibilityEstimatorFloat3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    constexpr VisibilityEstimatorFloat3() noexcept = default;
    constexpr VisibilityEstimatorFloat3(float xValue, float yValue, float zValue) noexcept
        : x(xValue), y(yValue), z(zValue)
    {
    }
};

constexpr VisibilityEstimatorFloat3 operator+(
    VisibilityEstimatorFloat3 left,
    VisibilityEstimatorFloat3 right) noexcept
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

constexpr VisibilityEstimatorFloat3 operator-(
    VisibilityEstimatorFloat3 left,
    VisibilityEstimatorFloat3 right) noexcept
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

constexpr VisibilityEstimatorFloat3 operator-(VisibilityEstimatorFloat3 value) noexcept
{
    return { -value.x, -value.y, -value.z };
}

constexpr VisibilityEstimatorFloat3 operator*(
    VisibilityEstimatorFloat3 value,
    float scale) noexcept
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

constexpr VisibilityEstimatorFloat3 operator*(
    float scale,
    VisibilityEstimatorFloat3 value) noexcept
{
    return value * scale;
}

inline VisibilityEstimatorFloat3& operator*=(
    VisibilityEstimatorFloat3& value,
    float scale) noexcept
{
    value = value * scale;
    return value;
}

inline VisibilityEstimatorFloat3& operator+=(
    VisibilityEstimatorFloat3& left,
    VisibilityEstimatorFloat3 right) noexcept
{
    left = left + right;
    return left;
}

inline VisibilityEstimatorFloat3 operator/(
    VisibilityEstimatorFloat3 value,
    float divisor) noexcept
{
    return value * (1.f / divisor);
}

inline VisibilityEstimatorFloat3 VisibilityEstimatorMakeFloat3(
    float x,
    float y,
    float z) noexcept
{
    return { x, y, z };
}

inline float VisibilityEstimatorDot(
    VisibilityEstimatorFloat3 left,
    VisibilityEstimatorFloat3 right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline VisibilityEstimatorFloat3 VisibilityEstimatorCross(
    VisibilityEstimatorFloat3 left,
    VisibilityEstimatorFloat3 right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x
    };
}

inline bool VisibilityEstimatorIsFinite(float value) noexcept
{
    return std::isfinite(value);
}

inline bool VisibilityEstimatorIsFinite3(VisibilityEstimatorFloat3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline float VisibilityEstimatorSaturate(float value) noexcept
{
    return std::clamp(value, 0.f, 1.f);
}

inline float VisibilityEstimatorMin(float left, float right) noexcept
{
    return std::min(left, right);
}

inline float VisibilityEstimatorMax(float left, float right) noexcept
{
    return std::max(left, right);
}

inline float VisibilityEstimatorAbs(float value) noexcept
{
    return std::abs(value);
}

inline float VisibilityEstimatorAtan2(float y, float x) noexcept
{
    return std::atan2(y, x);
}

inline float VisibilityEstimatorSin(float value) noexcept
{
    return std::sin(value);
}

inline float VisibilityEstimatorRsqrt(float value) noexcept
{
    return 1.f / std::sqrt(value);
}

inline float VisibilityEstimatorSqrt(float value) noexcept
{
    return std::sqrt(value);
}

#else

#define VisibilityEstimatorUint uint
#define VisibilityEstimatorFloat3 float3

float3 VisibilityEstimatorMakeFloat3(float x, float y, float z)
{
    return float3(x, y, z);
}

float VisibilityEstimatorDot(float3 left, float3 right)
{
    return dot(left, right);
}

float3 VisibilityEstimatorCross(float3 left, float3 right)
{
    return cross(left, right);
}

bool VisibilityEstimatorIsFinite(float value)
{
    return isfinite(value);
}

bool VisibilityEstimatorIsFinite3(float3 value)
{
    return all(isfinite(value));
}

float VisibilityEstimatorSaturate(float value)
{
    return saturate(value);
}

float VisibilityEstimatorMin(float left, float right)
{
    return min(left, right);
}

float VisibilityEstimatorMax(float left, float right)
{
    return max(left, right);
}

float VisibilityEstimatorAbs(float value)
{
    return abs(value);
}

float VisibilityEstimatorAtan2(float y, float x)
{
    return atan2(y, x);
}

float VisibilityEstimatorSin(float value)
{
    return sin(value);
}

float VisibilityEstimatorRsqrt(float value)
{
    return rsqrt(value);
}

float VisibilityEstimatorSqrt(float value)
{
    return sqrt(value);
}

#endif

#ifdef __cplusplus
#define UVSR_VISIBILITY_INLINE inline
#else
#define UVSR_VISIBILITY_INLINE
#endif

static const float VisibilityEstimatorPi = 3.14159265358979323846f;
static const float VisibilityEstimatorTwoPi = 6.28318530717958647692f;
static const float VisibilityEstimatorHalfPi = 1.57079632679489661923f;
static const float VisibilityEstimatorEpsilon = 1e-6f;

struct SliceMeasure
{
    // V points from the receiver toward the camera. S is the positive horizon
    // direction in the slice plane. The receiver normal projected into that
    // plane is cosGamma * V - sinGamma * S; the minus sign is intentional and
    // is the convention used by MapDirectionToUniformSliceMass.
    VisibilityEstimatorFloat3 V;
    VisibilityEstimatorFloat3 S;
    float sinGamma;
    float cosGamma;
    // Length of the receiver normal after projection into the slice. The
    // conditional CDF cancels this factor, but the complete joint cosine
    // measure must restore it when resolving AO and GI across slice azimuths.
    float projectedNormalLength;
    float gamma;
    float cosineSliceMass;
    VisibilityEstimatorUint valid;
};

struct GtIntervalBuildResult
{
    VisibilityInterval interval;
    float frontMass;
    float backMass;
    VisibilityEstimatorUint endpointOrderValid;
};

UVSR_VISIBILITY_INLINE float VisibilityEstimatorLengthSquared(
    VisibilityEstimatorFloat3 value)
{
    return VisibilityEstimatorDot(value, value);
}

UVSR_VISIBILITY_INLINE VisibilityEstimatorFloat3 VisibilityEstimatorSafeNormalize(
    VisibilityEstimatorFloat3 value,
    VisibilityEstimatorFloat3 fallback)
{
    float lengthSquared = VisibilityEstimatorLengthSquared(value);
    if (!(lengthSquared > VisibilityEstimatorEpsilon * VisibilityEstimatorEpsilon) ||
        !VisibilityEstimatorIsFinite(lengthSquared))
    {
        return fallback;
    }

    return value * VisibilityEstimatorRsqrt(lengthSquared);
}

UVSR_VISIBILITY_INLINE SliceMeasure BuildSliceMeasure(
    VisibilityEstimatorFloat3 receiverToCamera,
    VisibilityEstimatorFloat3 positiveSliceDirection,
    VisibilityEstimatorFloat3 receiverNormal)
{
    SliceMeasure measure;
    measure.V = VisibilityEstimatorSafeNormalize(
        receiverToCamera,
        VisibilityEstimatorMakeFloat3(0.0f, 0.0f, -1.0f));

    VisibilityEstimatorFloat3 tangent = positiveSliceDirection -
        measure.V * VisibilityEstimatorDot(positiveSliceDirection, measure.V);
    float tangentLengthSquared = VisibilityEstimatorLengthSquared(tangent);
    measure.S = VisibilityEstimatorSafeNormalize(
        tangent,
        VisibilityEstimatorMakeFloat3(1.0f, 0.0f, 0.0f));

    VisibilityEstimatorFloat3 sliceNormal = VisibilityEstimatorSafeNormalize(
        VisibilityEstimatorCross(measure.V, measure.S),
        VisibilityEstimatorMakeFloat3(0.0f, 1.0f, 0.0f));
    float receiverNormalLengthSquared =
        VisibilityEstimatorLengthSquared(receiverNormal);
    VisibilityEstimatorFloat3 unitReceiverNormal =
        VisibilityEstimatorSafeNormalize(receiverNormal, measure.V);
    VisibilityEstimatorFloat3 projectedNormal = unitReceiverNormal - sliceNormal *
        VisibilityEstimatorDot(unitReceiverNormal, sliceNormal);
    float projectedLengthSquared = VisibilityEstimatorLengthSquared(projectedNormal);

    measure.valid = tangentLengthSquared >
            VisibilityEstimatorEpsilon * VisibilityEstimatorEpsilon &&
        projectedLengthSquared >
            VisibilityEstimatorEpsilon * VisibilityEstimatorEpsilon &&
        receiverNormalLengthSquared >
            VisibilityEstimatorEpsilon * VisibilityEstimatorEpsilon &&
        VisibilityEstimatorIsFinite(tangentLengthSquared) &&
        VisibilityEstimatorIsFinite(projectedLengthSquared) &&
        VisibilityEstimatorIsFinite(receiverNormalLengthSquared) &&
        VisibilityEstimatorIsFinite3(receiverNormal)
        ? 1u
        : 0u;

    measure.projectedNormalLength = 0.0f;
    measure.gamma = 0.0f;
    measure.cosineSliceMass = 0.0f;
    if (measure.valid == 0u)
    {
        measure.sinGamma = 0.0f;
        measure.cosGamma = 1.0f;
        return measure;
    }

    measure.projectedNormalLength =
        VisibilityEstimatorSqrt(projectedLengthSquared);
    projectedNormal *= VisibilityEstimatorRsqrt(projectedLengthSquared);
    measure.cosGamma = VisibilityEstimatorMax(-1.0f,
        VisibilityEstimatorMin(1.0f,
            VisibilityEstimatorDot(projectedNormal, measure.V)));
    measure.sinGamma = VisibilityEstimatorMax(-1.0f,
        VisibilityEstimatorMin(1.0f,
            -VisibilityEstimatorDot(projectedNormal, measure.S)));
    // Visible G-buffer surfaces are expected to be face-forward with respect
    // to V. Preserve Uniform Solid Angle's historical behavior for malformed
    // normals,
    // but make their joint-cosine mass zero instead of integrating a wrapped
    // hemisphere with invalid sector semantics.
    if (measure.cosGamma >= 0.0f)
    {
        measure.gamma = VisibilityEstimatorAtan2(
            measure.sinGamma, measure.cosGamma);
        float conditionalMass = measure.cosGamma +
            measure.gamma * measure.sinGamma;
        measure.cosineSliceMass = measure.projectedNormalLength *
            VisibilityEstimatorMax(conditionalMass, 0.0f);
    }
    return measure;
}

UVSR_VISIBILITY_INLINE VisibilityEstimatorFloat3 ComputeBackDelta(
    VisibilityEstimatorFloat3 receiverPositionVS,
    VisibilityEstimatorFloat3 samplePositionVS,
    VisibilityEstimatorFloat3 receiverToCamera,
    float thickness,
    bool orthographic)
{
    VisibilityEstimatorFloat3 awayFromCamera = orthographic
        ? -receiverToCamera
        : VisibilityEstimatorSafeNormalize(samplePositionVS, -receiverToCamera);
    VisibilityEstimatorFloat3 sampleBackPositionVS = samplePositionVS +
        awayFromCamera * VisibilityEstimatorMax(thickness, 0.0f);
    return sampleBackPositionVS - receiverPositionVS;
}

UVSR_VISIBILITY_INLINE float MapDirectionToUniformSliceMass(
    VisibilityEstimatorFloat3 direction,
    SliceMeasure measure)
{
    if (measure.valid == 0u || !VisibilityEstimatorIsFinite3(direction))
        return 0.0f;

    VisibilityEstimatorFloat3 unitDirection = VisibilityEstimatorSafeNormalize(
        direction, measure.V);
    float horizonCosine = VisibilityEstimatorMax(-1.0f,
        VisibilityEstimatorMin(1.0f,
            VisibilityEstimatorDot(unitDirection, measure.V)));
    float side = VisibilityEstimatorDot(unitDirection, measure.S) >= 0.0f
        ? 1.0f
        : -1.0f;

    // This is the closed-form CDF of uniform solid-angle measure over the
    // receiver hemisphere, restricted to the current bidirectional slice:
    // F = 1/2 * (1 + sinGamma + side * (1 - cos(theta))). Because theta is
    // represented by its cosine, no acos is required.
    return VisibilityEstimatorSaturate(0.5f *
        (1.0f + measure.sinGamma + side * (1.0f - horizonCosine)));
}

UVSR_VISIBILITY_INLINE VisibilityEstimatorUint GtUniformEndpointOrderIsConsistent(
    VisibilityEstimatorFloat3 frontDirection,
    VisibilityEstimatorFloat3 backDirection,
    SliceMeasure measure,
    float frontMass,
    float backMass)
{
    float frontSide = VisibilityEstimatorDot(frontDirection, measure.S);
    float backSide = VisibilityEstimatorDot(backDirection, measure.S);
    if (frontSide * backSide < 0.0f ||
        VisibilityEstimatorAbs(frontSide) <= VisibilityEstimatorEpsilon ||
        VisibilityEstimatorAbs(backSide) <= VisibilityEstimatorEpsilon)
    {
        // Crossing the view direction is valid but has no single-side ordering
        // invariant. The sorted interval remains the authoritative behavior.
        return 1u;
    }

    bool ordered = frontSide > 0.0f
        ? frontMass <= backMass + VisibilityEstimatorEpsilon
        : backMass <= frontMass + VisibilityEstimatorEpsilon;
    return ordered ? 1u : 0u;
}

UVSR_VISIBILITY_INLINE GtIntervalBuildResult BuildGtIntervalDetails(
    VisibilityEstimatorFloat3 frontDirection,
    VisibilityEstimatorFloat3 backDirection,
    SliceMeasure measure)
{
    GtIntervalBuildResult result;
    result.frontMass = MapDirectionToUniformSliceMass(frontDirection, measure);
    result.backMass = MapDirectionToUniformSliceMass(backDirection, measure);
    result.interval = MakeVisibilityInterval(
        VisibilityEstimatorMin(result.frontMass, result.backMass),
        VisibilityEstimatorMax(result.frontMass, result.backMass));
    result.endpointOrderValid = GtUniformEndpointOrderIsConsistent(
        frontDirection,
        backDirection,
        measure,
        result.frontMass,
        result.backMass);
    return result;
}

UVSR_VISIBILITY_INLINE VisibilityInterval BuildGtInterval(
    VisibilityEstimatorFloat3 frontDirection,
    VisibilityEstimatorFloat3 backDirection,
    SliceMeasure measure)
{
    return BuildGtIntervalDetails(frontDirection, backDirection, measure).interval;
}

UVSR_VISIBILITY_INLINE VisibilityEstimatorUint BuildGtStochasticEndpointMask(
    VisibilityEstimatorFloat3 frontDirection,
    VisibilityEstimatorFloat3 backDirection,
    SliceMeasure measure,
    float sectorPhase)
{
    return MakeStochasticSectorRangeMask(
        BuildGtInterval(frontDirection, backDirection, measure),
        sectorPhase);
}

UVSR_VISIBILITY_INLINE float ResolveGtUniformAmbientVisibility(
    RadialVisibilityMask mask)
{
    return GetSliceVisibility(mask);
}

UVSR_VISIBILITY_INLINE float CosineSliceAntiderivative(
    float signedAngle,
    float sinGamma,
    float cosGamma)
{
    float sinAngle = VisibilityEstimatorSin(signedAngle);
    float sinDoubleAngle = VisibilityEstimatorSin(2.0f * signedAngle);
    float positiveSide = 0.5f * cosGamma * sinAngle * sinAngle -
        0.5f * sinGamma * signedAngle +
        0.25f * sinGamma * sinDoubleAngle;
    float negativeSide = -0.5f * cosGamma * sinAngle * sinAngle +
        0.5f * sinGamma * signedAngle -
        0.25f * sinGamma * sinDoubleAngle;
    return signedAngle >= 0.0f ? positiveSide : negativeSide;
}

UVSR_VISIBILITY_INLINE float MapDirectionToCosineSliceMass(
    VisibilityEstimatorFloat3 direction,
    SliceMeasure measure)
{
    if (measure.valid == 0u || !(measure.cosGamma >= 0.0f) ||
        !(measure.cosineSliceMass > VisibilityEstimatorEpsilon) ||
        !VisibilityEstimatorIsFinite3(direction))
    {
        return 0.0f;
    }

    VisibilityEstimatorFloat3 unitDirection = VisibilityEstimatorSafeNormalize(
        direction, measure.V);
    float signedAngle = VisibilityEstimatorAtan2(
        VisibilityEstimatorDot(unitDirection, measure.S),
        VisibilityEstimatorDot(unitDirection, measure.V));
    float minimumAngle = -VisibilityEstimatorHalfPi - measure.gamma;
    float maximumAngle = VisibilityEstimatorHalfPi - measure.gamma;
    signedAngle = VisibilityEstimatorMax(minimumAngle,
        VisibilityEstimatorMin(maximumAngle, signedAngle));

    float lowerIntegral = CosineSliceAntiderivative(
        minimumAngle, measure.sinGamma, measure.cosGamma);
    float conditionalMass = measure.cosGamma +
        measure.gamma * measure.sinGamma;
    float accumulatedMass = CosineSliceAntiderivative(
        signedAngle, measure.sinGamma, measure.cosGamma) - lowerIntegral;
    return VisibilityEstimatorSaturate(accumulatedMass /
        VisibilityEstimatorMax(conditionalMass, VisibilityEstimatorEpsilon));
}

UVSR_VISIBILITY_INLINE GtIntervalBuildResult BuildGtCosineIntervalDetails(
    VisibilityEstimatorFloat3 frontDirection,
    VisibilityEstimatorFloat3 backDirection,
    SliceMeasure measure)
{
    GtIntervalBuildResult result;
    result.frontMass = MapDirectionToCosineSliceMass(frontDirection, measure);
    result.backMass = MapDirectionToCosineSliceMass(backDirection, measure);
    result.interval = MakeVisibilityInterval(
        VisibilityEstimatorMin(result.frontMass, result.backMass),
        VisibilityEstimatorMax(result.frontMass, result.backMass));
    result.endpointOrderValid = GtUniformEndpointOrderIsConsistent(
        frontDirection,
        backDirection,
        measure,
        result.frontMass,
        result.backMass);
    return result;
}

UVSR_VISIBILITY_INLINE VisibilityInterval BuildGtCosineInterval(
    VisibilityEstimatorFloat3 frontDirection,
    VisibilityEstimatorFloat3 backDirection,
    SliceMeasure measure)
{
    return BuildGtCosineIntervalDetails(
        frontDirection, backDirection, measure).interval;
}

UVSR_VISIBILITY_INLINE float ResolveGtCosineAmbientVisibility(
    RadialVisibilityMask mask,
    SliceMeasure measure)
{
    // The mask fraction is conditional on the current slice. Multiplying by
    // the projected slice mass restores the joint cosine measure before the
    // uniform azimuth Monte Carlo average.
    return GetSliceVisibility(mask) * measure.cosineSliceMass;
}

UVSR_VISIBILITY_INLINE float ComputeGtUniformGiSampleWeight(
    VisibilityEstimatorUint newlyCoveredSectorCount,
    float receiverCosine,
    float sourceFacingCosine)
{
    float coveredMass = VisibilityEstimatorMin(
        float(newlyCoveredSectorCount),
        float(RadialVisibilitySectorCount)) /
        float(RadialVisibilitySectorCount);
    return coveredMass * VisibilityEstimatorSaturate(receiverCosine) *
        VisibilityEstimatorSaturate(sourceFacingCosine);
}

UVSR_VISIBILITY_INLINE float GetGtUniformIrradianceNormalization()
{
    // Equal-mass Uniform Solid Angle sectors sample p(omega)=1/(2*pi) on the
    // receiver hemisphere. Diffuse irradiance therefore multiplies their mean
    // L_i * cos(theta_receiver) by 2*pi. Cosine-Weighted Solid Angle uses a
    // different CDF and omits this receiver cosine rather than reusing this
    // normalization.
    return VisibilityEstimatorTwoPi;
}

UVSR_VISIBILITY_INLINE float ComputeGtCosineGiSampleWeight(
    VisibilityEstimatorUint newlyCoveredSectorCount,
    float cosineSliceMass,
    float sourceFacingCosine)
{
    float coveredMass = VisibilityEstimatorMin(
        float(newlyCoveredSectorCount),
        float(RadialVisibilitySectorCount)) /
        float(RadialVisibilitySectorCount);
    // Receiver cosine is already in the CDF and slice mass. Applying it again
    // would square the Lambertian term. Source-facing cosine remains part of
    // the finite-screen emitter approximation.
    return coveredMass * VisibilityEstimatorMax(cosineSliceMass, 0.0f) *
        VisibilityEstimatorSaturate(sourceFacingCosine);
}

UVSR_VISIBILITY_INLINE float GetGtCosineIrradianceNormalization()
{
    // Slice azimuth is sampled uniformly over [0, pi). The receiver cosine is
    // already included in each conditional sector, leaving only the outer pi
    // Monte Carlo normalization.
    return VisibilityEstimatorPi;
}

#ifndef __cplusplus
#undef VisibilityEstimatorUint
#undef VisibilityEstimatorFloat3
#endif
#undef UVSR_VISIBILITY_INLINE

#endif // UVSR_VISIBILITY_ESTIMATOR_SHARED_H
