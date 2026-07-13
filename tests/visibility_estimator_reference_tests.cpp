#include "radial_visibility_mask.h"
#include "visibility_estimator_shared.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using Float3 = VisibilityEstimatorFloat3;

    constexpr float HalfPi = VisibilityEstimatorPi * 0.5f;

    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Visibility estimator validation failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    bool Near(float actual, float expected, float tolerance = 1e-5f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    bool Near(Float3 actual, Float3 expected, float tolerance = 1e-5f)
    {
        return Near(actual.x, expected.x, tolerance) &&
            Near(actual.y, expected.y, tolerance) &&
            Near(actual.z, expected.z, tolerance);
    }

    Float3 Normalize(Float3 value, Float3 fallback = { 0.f, 0.f, 1.f })
    {
        return VisibilityEstimatorSafeNormalize(value, fallback);
    }

    float Dot(Float3 left, Float3 right)
    {
        return VisibilityEstimatorDot(left, right);
    }

    Float3 Cross(Float3 left, Float3 right)
    {
        return VisibilityEstimatorCross(left, right);
    }

    Float3 Hadamard(Float3 left, Float3 right)
    {
        return { left.x * right.x, left.y * right.y, left.z * right.z };
    }

    float Luminance(Float3 value)
    {
        return value.x * 0.2126f + value.y * 0.7152f + value.z * 0.0722f;
    }

    float ChromaError(Float3 actual, Float3 expected)
    {
        const float actualSum = actual.x + actual.y + actual.z;
        const float expectedSum = expected.x + expected.y + expected.z;
        if (!(actualSum > 1e-8f) && !(expectedSum > 1e-8f))
            return 0.f;

        Float3 actualChroma = actualSum > 1e-8f
            ? actual / actualSum
            : Float3{};
        Float3 expectedChroma = expectedSum > 1e-8f
            ? expected / expectedSum
            : Float3{};
        Float3 difference = actualChroma - expectedChroma;
        return std::sqrt(Dot(difference, difference));
    }

    uint32_t CountBits(uint32_t bits)
    {
        uint32_t count = 0u;
        while (bits != 0u)
        {
            bits &= bits - 1u;
            ++count;
        }
        return count;
    }

    Float3 DirectionInSlice(const SliceMeasure& measure, float signedAngle)
    {
        return Normalize(measure.V * std::cos(signedAngle) +
            measure.S * std::sin(signedAngle), measure.V);
    }

    Float3 MakePositiveSliceDirection(Float3 viewDirection)
    {
        Float3 candidate{ 1.f, 0.f, 0.f };
        candidate = candidate - viewDirection * Dot(candidate, viewDirection);
        if (Dot(candidate, candidate) <= 1e-6f)
        {
            candidate = Float3{ 0.f, 1.f, 0.f } -
                viewDirection * viewDirection.y;
        }
        return Normalize(candidate, { 1.f, 0.f, 0.f });
    }

    float NumericalUniformSliceCdf(
        Float3 receiverNormal,
        const SliceMeasure& measure,
        float targetSignedAngle)
    {
        constexpr uint32_t IntegrationSteps = 131072u;
        const double minimumAngle = -static_cast<double>(VisibilityEstimatorPi);
        const double maximumAngle = static_cast<double>(VisibilityEstimatorPi);
        const double step = (maximumAngle - minimumAngle) /
            static_cast<double>(IntegrationSteps);
        double accumulated = 0.0;
        double total = 0.0;

        for (uint32_t index = 0u; index < IntegrationSteps; ++index)
        {
            const double angle = minimumAngle +
                (static_cast<double>(index) + 0.5) * step;
            Float3 direction = DirectionInSlice(measure, static_cast<float>(angle));
            if (!(Dot(receiverNormal, direction) > 0.f))
                continue;

            const double mass = 0.5 * std::abs(std::sin(angle)) * step;
            total += mass;
            if (angle <= static_cast<double>(targetSignedAngle))
                accumulated += mass;
        }

        Require(std::abs(total - 1.0) < 2e-4,
            "numerical uniform-slice mass integrates to one");
        return static_cast<float>(accumulated / total);
    }

    void TestSliceMeasureAndUniformCdf()
    {
        const Float3 viewDirection{ 0.f, 0.f, 1.f };
        const Float3 sliceDirection{ 1.f, 0.f, 0.f };
        constexpr std::array<float, 7> NormalTilts = {
            -1.25f, -0.8f, -0.25f, 0.f, 0.35f, 0.9f, 1.25f
        };
        constexpr std::array<float, 11> DirectionAngles = {
            -2.8f, -2.1f, -1.4f, -0.75f, -0.15f, 0.f,
            0.2f, 0.65f, 1.3f, 2.0f, 2.75f
        };

        for (float normalTilt : NormalTilts)
        {
            Float3 receiverNormal = Normalize(
                viewDirection * std::cos(normalTilt) +
                sliceDirection * std::sin(normalTilt));
            SliceMeasure measure = BuildSliceMeasure(
                viewDirection, sliceDirection, receiverNormal);
            Require(measure.valid != 0u, "non-degenerate slice measure is valid");
            Require(Near(Dot(measure.V, measure.S), 0.f, 1e-6f),
                "slice basis is orthogonal");
            Require(Near(Dot(measure.V, measure.V), 1.f, 1e-6f) &&
                Near(Dot(measure.S, measure.S), 1.f, 1e-6f),
                "slice basis is normalized");
            Require(Near(measure.cosGamma, std::cos(normalTilt), 2e-5f),
                "cosGamma follows the projected receiver normal");
            Require(Near(measure.sinGamma, -std::sin(normalTilt), 2e-5f),
                "sinGamma follows the documented signed basis convention");

            for (float directionAngle : DirectionAngles)
            {
                Float3 direction = DirectionInSlice(measure, directionAngle);
                float actual = MapDirectionToUniformSliceMass(direction, measure);
                float expected = NumericalUniformSliceCdf(
                    receiverNormal, measure, directionAngle);
                std::ostringstream context;
                context << "no-acos CDF matches numerical uniform measure at normal tilt "
                    << normalTilt << " and direction " << directionAngle;
                Require(Near(actual, expected, 3e-4f), context.str());
            }
        }

        SliceMeasure degenerate = BuildSliceMeasure(
            viewDirection, viewDirection, viewDirection);
        Require(degenerate.valid == 0u,
            "slice direction parallel to V is rejected deterministically");
        Require(Near(MapDirectionToUniformSliceMass(viewDirection, degenerate), 0.f),
            "invalid slice measure fails open with zero interval mass");
    }

    struct ProjectedPoint
    {
        float x;
        float y;
        float w;
    };

    ProjectedPoint ProjectPerspective(Float3 point)
    {
        // Row-vector, row-major D3D-style perspective convention with view
        // space in front of the camera at negative z. Forward/reversed depth
        // alter clip z only, so xy/w is intentionally isolated here.
        return { point.x, point.y, -point.z };
    }

    void TestSampleRayThickness()
    {
        const Float3 receiverPosition{ 2.5f, -0.75f, -4.f };
        const Float3 receiverToCamera = Normalize(-receiverPosition);
        const Float3 samplePosition{ 3.5f, 0.4f, -2.f };
        constexpr float Thickness = 0.65f;

        Float3 perspectiveBackDelta = ComputeBackDelta(
            receiverPosition,
            samplePosition,
            receiverToCamera,
            Thickness,
            false);
        Float3 perspectiveBackPosition = receiverPosition + perspectiveBackDelta;
        Float3 expectedBackPosition = samplePosition + Normalize(samplePosition) * Thickness;
        Require(Near(perspectiveBackPosition, expectedBackPosition, 1e-6f),
            "perspective thickness follows the sampled point's camera ray");

        ProjectedPoint frontProjection = ProjectPerspective(samplePosition);
        ProjectedPoint backProjection = ProjectPerspective(perspectiveBackPosition);
        Require(frontProjection.w > 0.f && backProjection.w > frontProjection.w,
            "perspective thickness extends away from the camera and remains in front");
        Require(Near(frontProjection.x / frontProjection.w,
                backProjection.x / backProjection.w, 1e-6f) &&
            Near(frontProjection.y / frontProjection.w,
                backProjection.y / backProjection.w, 1e-6f),
            "sample-ray thickness preserves the projected sample coordinate");

        Float3 oldReceiverRayBack = samplePosition - receiverToCamera * Thickness;
        ProjectedPoint oldProjection = ProjectPerspective(oldReceiverRayBack);
        Require(std::abs(frontProjection.x / frontProjection.w -
                oldProjection.x / oldProjection.w) > 1e-3f,
            "wide-FOV off-axis fixture detects receiver-ray thickness bias");

        const Float3 nearPlaneSample{ 0.04f, -0.02f, -0.05f };
        Float3 nearBack = receiverPosition + ComputeBackDelta(
            receiverPosition,
            nearPlaneSample,
            receiverToCamera,
            0.1f,
            false);
        Require(nearBack.z < nearPlaneSample.z,
            "near-plane sample thickness moves away from the camera");

        const Float3 orthographicReceiverToCamera{ 0.f, 0.f, 1.f };
        Float3 orthographicBack = receiverPosition + ComputeBackDelta(
            receiverPosition,
            samplePosition,
            orthographicReceiverToCamera,
            Thickness,
            true);
        Require(Near(orthographicBack,
            samplePosition - orthographicReceiverToCamera * Thickness, 1e-6f),
            "orthographic thickness follows the constant camera direction");
    }

    void TestStochasticEqualMassQuantization()
    {
        constexpr VisibilityInterval Interval{ 0.1375f, 0.73125f };
        constexpr uint32_t PhaseCount = 16384u;
        double averagePopulation = 0.0;
        for (uint32_t phaseIndex = 0u; phaseIndex < PhaseCount; ++phaseIndex)
        {
            float phase = (static_cast<float>(phaseIndex) + 0.5f) /
                static_cast<float>(PhaseCount);
            averagePopulation += static_cast<double>(CountBits(
                MakeStochasticSectorRangeMask(Interval, phase)));
        }
        averagePopulation /= static_cast<double>(PhaseCount);
        const double expectedPopulation =
            static_cast<double>(Interval.maximumAngle01 - Interval.minimumAngle01) *
            static_cast<double>(RadialVisibilitySectorCount);
        Require(std::abs(averagePopulation - expectedPopulation) < 2e-3,
            "coherent stochastic point lattice is unbiased for equal-mass sectors");
    }

    struct FixtureSample
    {
        float signedAngle = 0.f;
        float distance = 1.f;
        float thickness = 0.1f;
        Float3 sourceRadiance{ 1.f, 1.f, 1.f };
        float sourceNormalOffset = 0.f;
        bool doubleSided = false;
        bool inScreen = true;
    };

    struct Fixture
    {
        const char* name = nullptr;
        Float3 receiverPosition{ 0.f, 0.f, -4.f };
        bool orthographic = false;
        float receiverNormalTilt = 0.f;
        float receiverNormalOutOfPlane = 0.f;
        float receiverDiffuseScale = 1.f;
        std::vector<FixtureSample> samples;
    };

    struct PreparedSample
    {
        Float3 frontDirection;
        Float3 backDirection;
        Float3 sourceNormal;
        Float3 sourceRadiance;
        float minimumSignedAngle = 0.f;
        float maximumSignedAngle = 0.f;
        VisibilityInterval gtInterval;
        VisibilityInterval paperInterval;
        bool doubleSided = false;
    };

    struct PreparedFixture
    {
        const char* name = nullptr;
        Float3 receiverNormal;
        SliceMeasure measure;
        float receiverDiffuseScale = 1.f;
        std::vector<PreparedSample> samples;
    };

    float FastAcos(float value)
    {
        const float x = std::abs(std::clamp(value, -1.f, 1.f));
        const float result = (-0.156583f * x + HalfPi) * std::sqrt(1.f - x);
        return value >= 0.f ? result : VisibilityEstimatorPi - result;
    }

    VisibilityInterval BuildPaperInterval(
        Float3 frontDirection,
        Float3 backDirection,
        const SliceMeasure& measure)
    {
        const float samplingSide = Dot(frontDirection, measure.S) >= 0.f ? 1.f : -1.f;
        const float projectedNormalAngle = std::atan2(
            measure.sinGamma, measure.cosGamma);
        const float frontAngle = FastAcos(Dot(frontDirection, measure.V));
        const float backAngle = FastAcos(Dot(backDirection, measure.V));
        const float frontMass = std::clamp(
            (samplingSide * -frontAngle - projectedNormalAngle + HalfPi) /
                VisibilityEstimatorPi,
            0.f,
            1.f);
        const float backMass = std::clamp(
            (samplingSide * -backAngle - projectedNormalAngle + HalfPi) /
                VisibilityEstimatorPi,
            0.f,
            1.f);
        return MakeVisibilityInterval(
            std::min(frontMass, backMass),
            std::max(frontMass, backMass));
    }

    std::pair<float, float> BuildGeometricAngularInterval(
        Float3 frontDirection,
        Float3 backDirection,
        const SliceMeasure& measure)
    {
        const float frontAngle = std::atan2(
            Dot(frontDirection, measure.S),
            Dot(frontDirection, measure.V));
        const float backAngle = std::atan2(
            Dot(backDirection, measure.S),
            Dot(backDirection, measure.V));
        const float shortestDelta = std::remainder(
            backAngle - frontAngle,
            VisibilityEstimatorTwoPi);
        const float unwrappedBackAngle = frontAngle + shortestDelta;
        return {
            std::min(frontAngle, unwrappedBackAngle),
            std::max(frontAngle, unwrappedBackAngle)
        };
    }

    bool ContainsSignedAngle(
        const PreparedSample& sample,
        float signedAngle)
    {
        const float midpoint = 0.5f *
            (sample.minimumSignedAngle + sample.maximumSignedAngle);
        const float unwrappedAngle = signedAngle +
            std::round((midpoint - signedAngle) / VisibilityEstimatorTwoPi) *
                VisibilityEstimatorTwoPi;
        return unwrappedAngle >= sample.minimumSignedAngle &&
            unwrappedAngle < sample.maximumSignedAngle;
    }

    PreparedFixture PrepareFixture(const Fixture& fixture)
    {
        Float3 receiverToCamera = fixture.orthographic
            ? Float3{ 0.f, 0.f, 1.f }
            : Normalize(-fixture.receiverPosition);
        Float3 positiveSliceDirection = MakePositiveSliceDirection(receiverToCamera);
        Float3 sliceNormal = Normalize(Cross(receiverToCamera, positiveSliceDirection),
            { 0.f, 1.f, 0.f });
        Float3 receiverNormal = Normalize(
            receiverToCamera * std::cos(fixture.receiverNormalTilt) +
            positiveSliceDirection * std::sin(fixture.receiverNormalTilt) +
            sliceNormal * fixture.receiverNormalOutOfPlane,
            receiverToCamera);
        SliceMeasure measure = BuildSliceMeasure(
            receiverToCamera, positiveSliceDirection, receiverNormal);
        Require(measure.valid != 0u,
            std::string(fixture.name) + ": fixture slice measure is valid");

        PreparedFixture prepared;
        prepared.name = fixture.name;
        prepared.receiverNormal = receiverNormal;
        prepared.measure = measure;
        prepared.receiverDiffuseScale = fixture.receiverDiffuseScale;

        for (const FixtureSample& sample : fixture.samples)
        {
            if (!sample.inScreen)
                continue;

            Float3 frontDirection = DirectionInSlice(measure, sample.signedAngle);
            Float3 samplePosition = fixture.receiverPosition +
                frontDirection * sample.distance;
            Float3 backDelta = ComputeBackDelta(
                fixture.receiverPosition,
                samplePosition,
                receiverToCamera,
                sample.thickness,
                fixture.orthographic);
            Float3 backDirection = Normalize(backDelta, frontDirection);
            GtIntervalBuildResult gtBuild = BuildGtIntervalDebug(
                frontDirection, backDirection, measure);
            Require(gtBuild.endpointOrderValid != 0u,
                std::string(fixture.name) +
                    ": GT endpoints follow the documented side convention");

            Float3 sourceTangent = Normalize(
                measure.V * -std::sin(sample.signedAngle) +
                measure.S * std::cos(sample.signedAngle),
                measure.S);
            Float3 sourceNormal = Normalize(
                -frontDirection * std::cos(sample.sourceNormalOffset) +
                sourceTangent * std::sin(sample.sourceNormalOffset),
                -frontDirection);
            const auto geometricInterval = BuildGeometricAngularInterval(
                frontDirection, backDirection, measure);

            prepared.samples.push_back({
                frontDirection,
                backDirection,
                sourceNormal,
                sample.sourceRadiance,
                geometricInterval.first,
                geometricInterval.second,
                gtBuild.interval,
                BuildPaperInterval(frontDirection, backDirection, measure),
                sample.doubleSided
            });
        }

        return prepared;
    }

    std::vector<Fixture> MakeFixtures()
    {
        const Float3 neutral{ 0.8f, 0.8f, 0.8f };
        return {
            { "infinite floor and wall", { 0.f, 0.f, -4.f }, false, 0.1f, 0.f, 1.f,
                { { -0.95f, 1.2f, 0.8f, neutral }, { 0.72f, 1.8f, 0.65f, { 0.6f, 0.7f, 0.9f } } } },
            { "thin vertical card", { 0.f, 0.f, -4.f }, false, 0.f, 0.f, 1.f,
                { { 0.45f, 1.1f, 0.025f, { 1.f, 0.7f, 0.3f } } } },
            { "two overlapping thin cards", { 0.f, 0.f, -4.f }, false, -0.15f, 0.f, 1.f,
                { { -0.38f, 0.9f, 0.035f, { 1.f, 0.15f, 0.1f } },
                  { -0.36f, 1.7f, 0.08f, { 0.1f, 1.f, 0.2f } } } },
            { "fence or evenly spaced bars", { 0.f, 0.f, -5.f }, false, 0.05f, 0.f, 1.f,
                { { -1.15f, 1.8f, 0.035f, neutral }, { -0.72f, 1.6f, 0.035f, neutral },
                  { -0.25f, 1.4f, 0.035f, neutral }, { 0.24f, 1.4f, 0.035f, neutral },
                  { 0.70f, 1.6f, 0.035f, neutral }, { 1.12f, 1.8f, 0.035f, neutral } } },
            { "wide-FOV off-axis receiver", { 3.2f, -0.4f, -2.f }, false, 0.42f, 0.f, 1.f,
                { { -0.9f, 1.1f, 0.5f, { 0.2f, 0.5f, 1.f } },
                  { 0.62f, 1.4f, 0.4f, { 1.f, 0.45f, 0.2f } } } },
            { "near-plane-crossing geometry", { 0.06f, 0.f, -0.22f }, false, -0.2f, 0.f, 1.f,
                { { -0.55f, 0.12f, 0.16f, { 0.7f, 0.8f, 1.f } },
                  { 0.5f, 0.16f, 0.12f, neutral } } },
            { "orthographic camera reference", { 1.5f, -0.5f, -4.f }, true, 0.3f, 0.f, 1.f,
                { { -0.82f, 1.3f, 0.35f, neutral }, { 0.48f, 1.1f, 0.22f, neutral } } },
            { "small bright emissive source", { 0.f, 0.f, -4.f }, false, 0.f, 0.f, 1.f,
                { { 0.3f, 1.8f, 0.045f, { 40.f, 8.f, 1.f } } } },
            { "double-sided emissive card", { 0.f, 0.f, -4.f }, false, 0.18f, 0.f, 1.f,
                { { -0.5f, 1.2f, 0.12f, { 2.f, 5.f, 9.f }, VisibilityEstimatorPi, true } } },
            { "flat geometry with a high-frequency normal map", { 0.f, 0.f, -4.f }, false, 0.55f, 0.15f, 1.f,
                { { -0.8f, 1.1f, 0.18f, { 1.f, 0.2f, 0.8f }, 0.7f },
                  { -0.15f, 1.3f, 0.2f, { 0.2f, 1.f, 0.4f }, -0.65f },
                  { 0.65f, 1.5f, 0.24f, { 0.2f, 0.5f, 1.f }, 0.55f } } },
            { "fully metallic receiver", { 0.f, 0.f, -4.f }, false, 0.f, 0.f, 0.f,
                { { -0.4f, 1.2f, 0.3f, { 4.f, 3.f, 2.f } } } },
            { "black diffuse receiver", { 0.f, 0.f, -4.f }, false, -0.1f, 0.f, 0.f,
                { { 0.42f, 1.2f, 0.3f, { 3.f, 2.f, 1.f } } } },
            { "diffuse furnace and multibounce energy", { 0.f, 0.f, -4.f }, false, 0.f, 0.f, 0.8f,
                { { -1.1f, 1.f, 0.9f, { 1.f, 1.f, 1.f }, 0.f, true },
                  { 0.f, 1.f, 0.9f, { 1.f, 1.f, 1.f }, 0.f, true },
                  { 1.1f, 1.f, 0.9f, { 1.f, 1.f, 1.f }, 0.f, true } } },
            { "screen-edge emitter entering and leaving the viewport", { 0.f, 0.f, -4.f }, false, 0.12f, 0.f, 1.f,
                { { -1.25f, 1.8f, 0.09f, { 8.f, 1.f, 0.2f }, 0.f, false, false },
                  { 1.22f, 1.7f, 0.09f, { 0.2f, 1.f, 8.f }, 0.f, false, true } } },
            { "foreground/background depth-layer ambiguity", { 0.f, 0.f, -4.f }, false, -0.3f, 0.f, 1.f,
                { { 0.2f, 0.7f, 0.35f, { 1.f, 0.1f, 0.1f } },
                  { 0.21f, 2.2f, 0.75f, { 0.1f, 0.2f, 1.f } },
                  { -0.35f, 1.6f, 0.2f, { 0.2f, 1.f, 0.2f } } } }
        };
    }

    struct Evaluation
    {
        float ambientVisibility = 1.f;
        Float3 irradiance{};
        Float3 composite{};
    };

    Evaluation EvaluateDenseReference(const PreparedFixture& fixture)
    {
        constexpr uint32_t DirectionCount = 131072u;
        const double angleStep = static_cast<double>(VisibilityEstimatorTwoPi) /
            static_cast<double>(DirectionCount);
        double visibleMass = 0.0;
        double totalMass = 0.0;
        Float3 irradiance{};

        for (uint32_t directionIndex = 0u;
            directionIndex < DirectionCount;
            ++directionIndex)
        {
            const double signedAngle = -static_cast<double>(VisibilityEstimatorPi) +
                (static_cast<double>(directionIndex) + 0.5) * angleStep;
            Float3 direction = DirectionInSlice(
                fixture.measure, static_cast<float>(signedAngle));
            if (!(Dot(fixture.receiverNormal, direction) > 0.f))
                continue;

            const double mass = 0.5 * std::abs(std::sin(signedAngle)) * angleStep;
            totalMass += mass;

            const PreparedSample* owner = nullptr;
            for (const PreparedSample& sample : fixture.samples)
            {
                if (ContainsSignedAngle(sample, static_cast<float>(signedAngle)))
                {
                    owner = &sample;
                    break;
                }
            }

            if (!owner)
            {
                visibleMass += mass;
                continue;
            }

            // UVSR has one radiance/normal sample at the front surface. Hold
            // that source kernel constant over the virtual thickness interval
            // while the explicit directions independently integrate its
            // solid-angle support. This matches the renderer's sampled-source
            // contract without treating the synthetic back point as a second
            // emitting surface.
            const float receiverCosine = std::max(Dot(
                fixture.receiverNormal, owner->frontDirection), 0.f);
            const float signedSourceCosine = Dot(
                owner->sourceNormal, -owner->frontDirection);
            const float sourceCosine = owner->doubleSided
                ? std::abs(signedSourceCosine)
                : std::max(signedSourceCosine, 0.f);
            irradiance += owner->sourceRadiance * static_cast<float>(
                mass * static_cast<double>(VisibilityEstimatorTwoPi) *
                static_cast<double>(receiverCosine * sourceCosine));
        }

        Require(std::abs(totalMass - 1.0) < 2e-4,
            std::string(fixture.name) + ": dense reference integrates unit mass");
        Evaluation result;
        result.ambientVisibility = static_cast<float>(visibleMass / totalMass);
        result.irradiance = irradiance;
        result.composite = irradiance * fixture.receiverDiffuseScale;
        return result;
    }

    enum class PacketEstimator
    {
        PaperAngular,
        GTUniform
    };

    Evaluation EvaluatePacket(
        const PreparedFixture& fixture,
        PacketEstimator estimator,
        float sectorPhase)
    {
        RadialVisibilityMask mask;
        Float3 irradiance{};
        for (const PreparedSample& sample : fixture.samples)
        {
            VisibilityInterval interval = estimator == PacketEstimator::GTUniform
                ? sample.gtInterval
                : sample.paperInterval;
            const uint32_t candidateBits = MakeStochasticSectorRangeMask(
                interval, sectorPhase);
            const uint32_t newlyCoveredBits = AccumulateOccluder(mask, candidateBits);
            const uint32_t newSectorCount = CountBits(newlyCoveredBits);
            if (newSectorCount == 0u)
                continue;

            const float receiverCosine = std::max(
                Dot(fixture.receiverNormal, sample.frontDirection), 0.f);
            const float signedSourceCosine = Dot(
                sample.sourceNormal, -sample.frontDirection);
            const float sourceCosine = sample.doubleSided
                ? std::abs(signedSourceCosine)
                : std::max(signedSourceCosine, 0.f);
            const float coverage = static_cast<float>(newSectorCount) /
                static_cast<float>(RadialVisibilitySectorCount);
            const float normalization = estimator == PacketEstimator::GTUniform
                ? GetGtUniformIrradianceNormalization()
                : VisibilityEstimatorPi;
            const float sampleWeight = estimator == PacketEstimator::GTUniform
                ? ComputeGtUniformGiSampleWeight(
                    newSectorCount, receiverCosine, sourceCosine)
                : coverage * receiverCosine * sourceCosine;
            irradiance += sample.sourceRadiance * (normalization * sampleWeight);
        }

        Evaluation result;
        result.ambientVisibility = estimator == PacketEstimator::GTUniform
            ? ResolveGtUniformAmbientVisibility(mask)
            : GetSliceVisibility(mask);
        result.irradiance = irradiance;
        result.composite = irradiance * fixture.receiverDiffuseScale;
        return result;
    }

    struct PhaseStatistics
    {
        Evaluation mean;
        float ambientVariance = 0.f;
        float luminanceVariance = 0.f;
    };

    PhaseStatistics EvaluateAcrossPhases(
        const PreparedFixture& fixture,
        PacketEstimator estimator)
    {
        constexpr uint32_t PhaseCount = 2048u;
        double ambientSum = 0.0;
        double ambientSquaredSum = 0.0;
        double luminanceSum = 0.0;
        double luminanceSquaredSum = 0.0;
        Float3 irradianceSum{};

        for (uint32_t phaseIndex = 0u; phaseIndex < PhaseCount; ++phaseIndex)
        {
            const float phase = (static_cast<float>(phaseIndex) + 0.5f) /
                static_cast<float>(PhaseCount);
            Evaluation evaluation = EvaluatePacket(fixture, estimator, phase);
            const double ambient = evaluation.ambientVisibility;
            const double luminance = Luminance(evaluation.irradiance);
            ambientSum += ambient;
            ambientSquaredSum += ambient * ambient;
            luminanceSum += luminance;
            luminanceSquaredSum += luminance * luminance;
            irradianceSum += evaluation.irradiance;
        }

        const double inverseCount = 1.0 / static_cast<double>(PhaseCount);
        PhaseStatistics statistics;
        statistics.mean.ambientVisibility = static_cast<float>(ambientSum * inverseCount);
        statistics.mean.irradiance = irradianceSum / static_cast<float>(PhaseCount);
        statistics.mean.composite = statistics.mean.irradiance *
            fixture.receiverDiffuseScale;
        statistics.ambientVariance = static_cast<float>(std::max(
            ambientSquaredSum * inverseCount -
                (ambientSum * inverseCount) * (ambientSum * inverseCount),
            0.0));
        statistics.luminanceVariance = static_cast<float>(std::max(
            luminanceSquaredSum * inverseCount -
                (luminanceSum * inverseCount) * (luminanceSum * inverseCount),
            0.0));
        return statistics;
    }

    float RootMeanSquare(const std::vector<float>& values)
    {
        double sum = 0.0;
        for (float value : values)
            sum += static_cast<double>(value) * static_cast<double>(value);
        return values.empty()
            ? 0.f
            : static_cast<float>(std::sqrt(sum / static_cast<double>(values.size())));
    }

    float Percentile(std::vector<float> values, float percentile)
    {
        if (values.empty())
            return 0.f;
        std::sort(values.begin(), values.end());
        const float scaledIndex = percentile * static_cast<float>(values.size() - 1u);
        const size_t lower = static_cast<size_t>(std::floor(scaledIndex));
        const size_t upper = static_cast<size_t>(std::ceil(scaledIndex));
        const float blend = scaledIndex - static_cast<float>(lower);
        return values[lower] * (1.f - blend) + values[upper] * blend;
    }

    struct MetricSet
    {
        std::vector<float> signedAmbientBias;
        std::vector<float> absoluteAmbientError;
        std::vector<float> giLuminanceError;
        std::vector<float> giChromaError;
        std::vector<float> ambientVariance;
        std::vector<float> luminanceVariance;
    };

    void AppendMetrics(
        MetricSet& metrics,
        const Evaluation& reference,
        const PhaseStatistics& packet)
    {
        const float ambientBias = packet.mean.ambientVisibility -
            reference.ambientVisibility;
        metrics.signedAmbientBias.push_back(ambientBias);
        metrics.absoluteAmbientError.push_back(std::abs(ambientBias));
        metrics.giLuminanceError.push_back(std::abs(
            Luminance(packet.mean.irradiance) - Luminance(reference.irradiance)));
        metrics.giChromaError.push_back(ChromaError(
            packet.mean.irradiance, reference.irradiance));
        metrics.ambientVariance.push_back(packet.ambientVariance);
        metrics.luminanceVariance.push_back(packet.luminanceVariance);
    }

    float Mean(const std::vector<float>& values)
    {
        return values.empty()
            ? 0.f
            : std::accumulate(values.begin(), values.end(), 0.f) /
                static_cast<float>(values.size());
    }

    void PrintSummary(const char* estimatorName, const MetricSet& metrics)
    {
        std::cout << estimatorName
            << " signed_mean_ao_bias=" << Mean(metrics.signedAmbientBias)
            << " ao_rmse=" << RootMeanSquare(metrics.signedAmbientBias)
            << " gi_luminance_rmse=" << RootMeanSquare(metrics.giLuminanceError)
            << " gi_chroma_mean=" << Mean(metrics.giChromaError)
            << " p95_ao_error=" << Percentile(metrics.absoluteAmbientError, 0.95f)
            << " p99_ao_error=" << Percentile(metrics.absoluteAmbientError, 0.99f)
            << " phase_ao_variance=" << Mean(metrics.ambientVariance)
            << " phase_gi_luminance_variance=" << Mean(metrics.luminanceVariance)
            << '\n';
    }

    void TestDeterministicReferenceSuite()
    {
        MetricSet paperMetrics;
        MetricSet uniformMetrics;
        std::cout << std::fixed << std::setprecision(7);

        for (const Fixture& fixture : MakeFixtures())
        {
            PreparedFixture prepared = PrepareFixture(fixture);
            Evaluation reference = EvaluateDenseReference(prepared);
            PhaseStatistics paper = EvaluateAcrossPhases(
                prepared, PacketEstimator::PaperAngular);
            PhaseStatistics uniform = EvaluateAcrossPhases(
                prepared, PacketEstimator::GTUniform);
            AppendMetrics(paperMetrics, reference, paper);
            AppendMetrics(uniformMetrics, reference, uniform);

            std::cout << "scene=\"" << fixture.name << "\""
                << " reference_ao=" << reference.ambientVisibility
                << " paper_ao=" << paper.mean.ambientVisibility
                << " gt_uniform_ao=" << uniform.mean.ambientVisibility
                << " reference_gi_luma=" << Luminance(reference.irradiance)
                << " paper_gi_luma=" << Luminance(paper.mean.irradiance)
                << " gt_uniform_gi_luma=" << Luminance(uniform.mean.irradiance)
                << '\n';

            Require(std::isfinite(uniform.mean.ambientVisibility) &&
                VisibilityEstimatorIsFinite3(uniform.mean.irradiance),
                std::string(fixture.name) + ": GTUniform result remains finite");
            if (fixture.receiverDiffuseScale == 0.f)
            {
                Require(Near(uniform.mean.composite, Float3{}, 1e-7f),
                    std::string(fixture.name) +
                        ": non-diffuse receiver composite rejects GI exactly");
            }
        }

        PrintSummary("PaperAngular", paperMetrics);
        PrintSummary("GTUniform", uniformMetrics);

        const float uniformAoRmse = RootMeanSquare(uniformMetrics.signedAmbientBias);
        const float paperAoRmse = RootMeanSquare(paperMetrics.signedAmbientBias);
        const float uniformGiLuminanceRmse = RootMeanSquare(
            uniformMetrics.giLuminanceError);
        const float paperGiLuminanceRmse = RootMeanSquare(
            paperMetrics.giLuminanceError);
        Require(uniformAoRmse < 0.0125f,
            "GTUniform averaged AO agrees with the explicit-direction reference");
        Require(uniformAoRmse <= paperAoRmse + 1e-5f,
            "GTUniform does not increase aggregate AO bias against reference");
        Require(Percentile(uniformMetrics.absoluteAmbientError, 0.99f) < 0.025f,
            "GTUniform 99th-percentile AO error remains within one sector");
        Require(uniformGiLuminanceRmse < 0.001f,
            "GTUniform GI agrees with the sampled-source reference");
        Require(uniformGiLuminanceRmse <= paperGiLuminanceRmse + 1e-5f,
            "GTUniform does not increase aggregate GI luminance error");
        Require(Mean(uniformMetrics.giChromaError) < 0.001f,
            "GTUniform preserves sampled-source chroma");
        Require(Near(
                GetGtUniformIrradianceNormalization(),
                VisibilityEstimatorTwoPi,
                1e-7f),
            "GTUniform irradiance normalization is two pi");
    }

    void TestFiniteMultibounceEnergy()
    {
        constexpr float DiffuseReflectance = 0.8f;
        constexpr uint32_t BounceCount = 4u;
        float frontier = 1.f;
        float cumulative = 0.f;
        for (uint32_t bounce = 0u; bounce < BounceCount; ++bounce)
        {
            frontier *= DiffuseReflectance;
            cumulative += frontier;
            Require(std::isfinite(frontier) && std::isfinite(cumulative),
                "finite current-frame bounce chain remains finite");
        }
        const float geometricBound = DiffuseReflectance /
            (1.f - DiffuseReflectance);
        Require(cumulative < geometricBound,
            "four-bounce furnace energy stays below the infinite geometric series");
    }
}

int main()
{
    TestSliceMeasureAndUniformCdf();
    TestSampleRayThickness();
    TestStochasticEqualMassQuantization();
    TestDeterministicReferenceSuite();
    TestFiniteMultibounceEnergy();

    std::cout << "UVSR visibility estimator reference validation passed\n";
    return EXIT_SUCCESS;
}
