#include "ray_material_visibility_contract.h"
#include "ray_origin_contract.h"
#include "ray_visibility_trace_contract.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Ray visibility semantic validation failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool Near(float actual, float expected, float tolerance = 1e-6f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    void TestRayOriginContract()
    {
        const RayOriginFloat3 oriented =
            RayOriginOrientGeometricNormal(
                { 0.f, 0.f, -2.f },
                { 0.f, 0.f, 1.f });
        Require(
            Near(oriented.x, 0.f) && Near(oriented.y, 0.f) &&
                Near(oriented.z, 1.f),
            "geometric normal was not oriented to the view hemisphere");

        const float reverseFloat = RayOriginStepDepthTowardCamera(
            0.5f, true, true, 0.f);
        const float forwardFloat = RayOriginStepDepthTowardCamera(
            0.5f, true, false, 0.f);
        Require(
            reverseFloat == std::nextafter(0.5f, 1.f) &&
                forwardFloat == std::nextafter(0.5f, 0.f),
            "float depth did not move one representable step toward camera");
        Require(
            RayOriginStepDepthTowardCamera(1.f, true, true, 0.f) == 1.f &&
                RayOriginStepDepthTowardCamera(0.f, true, false, 0.f) == 0.f,
            "float depth endpoint was not clamped");
        Require(
            Near(RayOriginStepDepthTowardCamera(
                    0.5f, false, true, 1.f / 1024.f),
                0.5f + 1.f / 1024.f) &&
                Near(RayOriginStepDepthTowardCamera(
                        0.5f, false, false, 1.f / 1024.f),
                    0.5f - 1.f / 1024.f),
            "integer depth did not use the configured forward/reverse step");

        Require(
            RayOriginOffsetFloatComponent(1.f, 1.f) > 1.f &&
                RayOriginOffsetFloatComponent(1.f, -1.f) < 1.f &&
                RayOriginOffsetFloatComponent(-1.f, 1.f) > -1.f &&
                RayOriginOffsetFloatComponent(-1.f, -1.f) < -1.f,
            "signed representable-position offset moved the wrong way");
        Require(
            Near(RayOriginOffsetFloatComponent(0.f, 1.f),
                1.f / 65536.f),
            "near-origin offset did not use the fixed float-scale step");

        Require(
            Near(ResolveRayOriginClearance(0.01f, 0.02f), 0.02f) &&
                Near(ResolveRayOriginClearance(0.03f, 0.02f), 0.03f) &&
                Near(ResolveRayOriginClearance(
                        -1.f,
                        std::numeric_limits<float>::quiet_NaN()),
                    0.f),
            "ray clearance is not max(user bias, depth-step distance)");
        const RayOriginFloat3 resolved = ResolveRayOriginPosition(
            { 1.f, -1.f, 0.f },
            { 0.f, 0.f, 1.f },
            0.01f);
        Require(
            resolved.x == 1.f && resolved.y == -1.f &&
                resolved.z > 0.01f,
            "resolved origin did not combine clearance and representable offset");
    }

    void TestMaterialVisibilityContract()
    {
        const RayMaterialCoveragePlan opaque =
            ResolveRayMaterialCoveragePlan(
                true, false, false, false,
                true, false, true, true);
        Require(
            opaque.mode == UVSR_RAY_MATERIAL_COVERAGE_OPAQUE &&
                ResolveRayMaterialCandidateCoverage(
                    opaque, 0.f, 0.f, 1.f, false),
            "opaque front-face candidate was rejected");
        Require(
            ResolveRayMaterialCoveragePlan(
                false, false, false, false,
                true, false, false, false).mode ==
                UVSR_RAY_MATERIAL_COVERAGE_REJECT &&
                ResolveRayMaterialCoveragePlan(
                    false, true, false, false,
                    true, false, false, false).mode ==
                UVSR_RAY_MATERIAL_COVERAGE_OPAQUE,
            "backface/double-sided candidate policy changed");
        Require(
            ResolveRayMaterialCoveragePlan(
                true, false, true, true,
                true, false, false, false).mode ==
                UVSR_RAY_MATERIAL_COVERAGE_REJECT,
            "unsupported transport material blocked path-only rejection");
        Require(
            ResolveRayMaterialCoveragePlan(
                true, false, false, false,
                false, false, false, false).mode ==
                UVSR_RAY_MATERIAL_COVERAGE_REJECT,
            "transparent non-alpha-tested domain became a blocker");

        const RayMaterialCoveragePlan opacityPreferred =
            ResolveRayMaterialCoveragePlan(
                true, false, false, false,
                false, true, true, true);
        const RayMaterialCoveragePlan baseFallback =
            ResolveRayMaterialCoveragePlan(
                true, false, false, false,
                false, true, false, true);
        const RayMaterialCoveragePlan scalarAlpha =
            ResolveRayMaterialCoveragePlan(
                true, false, false, false,
                false, true, false, false);
        Require(
            opacityPreferred.alphaSource ==
                UVSR_RAY_MATERIAL_ALPHA_OPACITY_TEXTURE &&
                baseFallback.alphaSource ==
                UVSR_RAY_MATERIAL_ALPHA_BASE_TEXTURE &&
                scalarAlpha.alphaSource ==
                UVSR_RAY_MATERIAL_ALPHA_NONE,
            "opacity texture no longer takes precedence over base alpha");
        Require(
            !ResolveRayMaterialCandidateCoverage(
                opacityPreferred, 1.f, 0.2f, 0.5f, true) &&
                ResolveRayMaterialCandidateCoverage(
                    baseFallback, 1.f, 0.9f, 0.5f, true) &&
                !ResolveRayMaterialCandidateCoverage(
                    opacityPreferred, 1.f, 1.f, 0.5f, false),
            "selected texture channel or bounded-descriptor rejection changed");
        Require(
            ResolveRayMaterialCandidateCoverage(
                scalarAlpha, 0.5f, 0.f, 0.5f, true) &&
                !ResolveRayMaterialCandidateCoverage(
                    scalarAlpha, 0.499f, 1.f, 0.5f, true) &&
                ResolveRayMaterialCandidateCoverage(
                    scalarAlpha, 2.f, 1.f, 1.f, true) &&
                !ResolveRayMaterialCandidateCoverage(
                    scalarAlpha,
                    std::numeric_limits<float>::quiet_NaN(),
                    1.f,
                    0.5f,
                    true),
            "alpha saturation, exact cutoff, or nonfinite rejection changed");
    }

    void TestTraceReductionContract()
    {
        constexpr float Miss = 65504.f;
        constexpr float Maximum = 65472.f;
        const RayVisibilityTraceSample miss =
            ResolveRayVisibilityTraceSample(
                false, 0.f, true, Maximum, Miss);
        const RayVisibilityTraceSample farHit =
            ResolveRayVisibilityTraceSample(
                true, 5.f, true, Maximum, Miss);
        const RayVisibilityTraceSample nearHit =
            ResolveRayVisibilityTraceSample(
                true, 2.f, true, Maximum, Miss);
        const RayVisibilityTraceSample binaryHit =
            ResolveRayVisibilityTraceSample(
                true, 7.f, false, Maximum, Miss);
        const RayVisibilityTraceSample malformedHit =
            ResolveRayVisibilityTraceSample(
                true,
                std::numeric_limits<float>::quiet_NaN(),
                true,
                Maximum,
                Miss);
        Require(
            miss.queryCount == 1u && miss.occluded == 0u &&
                Near(miss.visibility, 1.f) && Near(miss.hitDistance, Miss) &&
                binaryHit.queryCount == 1u && binaryHit.occluded == 1u &&
                Near(binaryHit.visibility, 0.f) &&
                Near(binaryHit.hitDistance, 0.f) &&
                Near(malformedHit.hitDistance, Maximum),
            "one-query binary hit/miss encoding changed");

        RayVisibilityTraceAggregate aggregate =
            BeginRayVisibilityTraceAggregate(Miss);
        aggregate = AccumulateRayVisibilityTraceSample(aggregate, miss);
        aggregate = AccumulateRayVisibilityTraceSample(aggregate, farHit);
        aggregate = AccumulateRayVisibilityTraceSample(aggregate, nearHit);
        Require(
            aggregate.queryCount == 3u && aggregate.sampleCount == 3u &&
                aggregate.visibleSampleCount == 1u &&
                Near(aggregate.closestHitDistance, 2.f) &&
                Near(ResolveRayVisibilityTraceAverage(aggregate), 1.f / 3.f) &&
                RayVisibilityTraceAggregateIsComplete(aggregate, 3u) &&
                !RayVisibilityTraceAggregateIsComplete(aggregate, 4u),
            "closest blocker or exactly-one-query/sample reduction changed");
    }
}

int main()
{
    TestRayOriginContract();
    TestMaterialVisibilityContract();
    TestTraceReductionContract();
    return EXIT_SUCCESS;
}
