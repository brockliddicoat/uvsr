#include "specular_reference.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <limits>

namespace
{
    using namespace uvsr;

    bool NearlyEqual(float left, float right, float tolerance = 1e-5f)
    {
        return std::abs(left - right) <= tolerance;
    }

    void RequireFloat3(
        SpecularFloat3 actual,
        SpecularFloat3 expected,
        float tolerance = 1e-5f)
    {
        assert(NearlyEqual(actual.x, expected.x, tolerance));
        assert(NearlyEqual(actual.y, expected.y, tolerance));
        assert(NearlyEqual(actual.z, expected.z, tolerance));
    }

    void TestDefaultsAndSanitization()
    {
        const SpecularSettings defaults;
        assert(!defaults.sssr.enabled);
        assert(!defaults.hybrid.enabled);
        assert(defaults.sssr.ratioEstimator);
        assert(defaults.sssr.deferredColorLookup);
        assert(defaults.sssr.samplesPerQuad ==
            SpecularSamplesPerQuad::One);
        assert(ResolveSpecularSamplesPerQuad(
            SpecularSamplesPerQuad::One) == 1u);
        assert(ResolveSpecularSamplesPerQuad(
            SpecularSamplesPerQuad::Two) == 2u);
        assert(ResolveSpecularSamplesPerQuad(
            SpecularSamplesPerQuad::Four) == 4u);
        assert(defaults.hybrid.rayTracing);
        assert(defaults.hybrid.compactDispatch);
        assert(ResolveSpecularTechnique(defaults) ==
            SpecularTechnique::None);

        SpecularSettings invalid;
        invalid.sssr.maxRoughness =
            std::numeric_limits<float>::quiet_NaN();
        invalid.sssr.samplesPerQuad =
            static_cast<SpecularSamplesPerQuad>(3u);
        invalid.sssr.depthThickness = -1.f;
        invalid.sssr.maxTraversalIterations = 1u;
        invalid.sssr.minTraversalOccupancy = 1000u;
        invalid.sssr.mostDetailedMip = 1000u;
        invalid.sssr.traceAndFilterBias =
            std::numeric_limits<float>::infinity();
        invalid.sssr.diagnosticView =
            static_cast<SpecularDiagnosticView>(999u);
        invalid.hybrid.screenSpaceConfidence = 2.f;
        invalid.hybrid.maxRoughness = -2.f;
        invalid.hybrid.rayMaxDistance = 0.f;
        invalid.hybrid.rayBias = -1.f;
        invalid.hybrid.backfaceRejection = 2.f;
        invalid.hybrid.depthRejection = -1.f;
        invalid.hybrid.diagnosticView =
            static_cast<SpecularDiagnosticView>(999u);

        const SpecularSettings sanitized =
            SanitizeSpecularSettings(invalid);
        assert(NearlyEqual(
            sanitized.sssr.maxRoughness,
            SpecularDefaultMaxRoughness));
        assert(sanitized.sssr.samplesPerQuad ==
            SpecularSamplesPerQuad::One);
        assert(NearlyEqual(sanitized.sssr.depthThickness, 0.f));
        assert(sanitized.sssr.maxTraversalIterations == 8u);
        assert(sanitized.sssr.minTraversalOccupancy ==
            SpecularMaximumTraversalOccupancy);
        assert(sanitized.sssr.mostDetailedMip ==
            SpecularMaximumMostDetailedMip);
        assert(NearlyEqual(sanitized.sssr.traceAndFilterBias, 0.f));
        assert(sanitized.sssr.diagnosticView ==
            SpecularDiagnosticView::FinalImage);
        assert(NearlyEqual(sanitized.hybrid.screenSpaceConfidence, 1.f));
        assert(NearlyEqual(sanitized.hybrid.maxRoughness, 0.f));
        assert(NearlyEqual(sanitized.hybrid.rayMaxDistance, 0.001f));
        assert(NearlyEqual(sanitized.hybrid.rayBias, 0.f));
        assert(NearlyEqual(sanitized.hybrid.backfaceRejection, 1.f));
        assert(NearlyEqual(sanitized.hybrid.depthRejection, 0.f));
        assert(sanitized.hybrid.diagnosticView ==
            SpecularDiagnosticView::FinalImage);
    }

    void TestHybridPrecedence()
    {
        SpecularSettings settings;
        settings.sssr.enabled = true;
        assert(ResolveSpecularTechnique(settings) ==
            SpecularTechnique::StochasticScreenSpace);

        settings.sssr.maxRoughness = 0.3f;
        settings.sssr.diagnosticView =
            SpecularDiagnosticView::SourcePdf;
        settings.hybrid.enabled = true;
        settings.hybrid.maxRoughness = 0.6f;
        settings.hybrid.diagnosticView =
            SpecularDiagnosticView::RayRoute;
        assert(ResolveSpecularTechnique(settings) ==
            SpecularTechnique::Hybrid);
        assert(NearlyEqual(ResolveSpecularMaxRoughness(settings), 0.6f));
        assert(ResolveSpecularDiagnosticView(settings) ==
            SpecularDiagnosticView::RayRoute);

        settings.sssr.enabled = false;
        assert(ResolveSpecularTechnique(settings) ==
            SpecularTechnique::Hybrid);
    }

    void TestRatioEstimatorUsesDestinationBrdfOverSourcePdf()
    {
        const SpecularReuseSample samples[] = {
            { { 10.f, 0.f, 0.f }, 1.f, 0.1f, true },
            { { 0.f, 0.f, 10.f }, 1.f, 1.f, true }
        };
        const SpecularFloat3 unitDfg = { 1.f, 1.f, 1.f };
        const SpecularFloat3 ordinary = ResolveSpecularSpatialReuse(
            samples, 2u, false, unitDfg);
        const SpecularFloat3 ratio = ResolveSpecularSpatialReuse(
            samples, 2u, true, unitDfg);

        RequireFloat3(ordinary, { 5.f, 0.f, 5.f });
        RequireFloat3(
            ratio,
            { 100.f / 11.f, 0.f, 10.f / 11.f },
            1e-4f);
        assert(ratio.x > ordinary.x);
        assert(ratio.z < ordinary.z);
    }

    void TestRatioEstimatorGuardsInvalidWeightsAndFallsBack()
    {
        const SpecularReuseSample invalidSamples[] = {
            { { 100.f, 0.f, 0.f }, 1.f, 0.f, true },
            { { 0.f, 100.f, 0.f }, -1.f, 1.f, true },
            { { 0.f, 0.f, 100.f }, 1.f,
                std::numeric_limits<float>::quiet_NaN(), true }
        };
        const SpecularFloat3 fallback = { 2.f, 3.f, 4.f };
        const SpecularFloat3 dfg = { 0.5f, 0.25f, 0.125f };
        RequireFloat3(
            ResolveSpecularSpatialReuse(
                invalidSamples,
                3u,
                true,
                dfg,
                fallback),
            { 1.f, 0.75f, 0.5f });
    }

    void TestPreintegratedDfgIsAppliedOnceAfterNormalization()
    {
        const SpecularReuseSample samples[] = {
            { { 2.f, 4.f, 8.f }, 3.f, 0.5f, true }
        };
        const SpecularFloat3 dfg = { 0.5f, 0.25f, 0.125f };
        const SpecularFloat3 resolved = ResolveSpecularSpatialReuse(
            samples, 1u, true, dfg);
        RequireFloat3(resolved, { 1.f, 1.f, 1.f });
        assert(!NearlyEqual(resolved.x, 0.5f));
    }

    void TestPairedSkySpecularContract()
    {
        const SpecularFloat3 resolved = { 10.f, 10.f, 10.f };
        const SpecularFloat3 analyticEnvironment = { 4.f, 4.f, 4.f };

        RequireFloat3(
            ResolveSpecularSkyApplication(
                resolved,
                analyticEnvironment,
                {},
                UVSR_SKY_ESTIMATE_NONE,
                1.f,
                1.f,
                1.f),
            resolved);
        RequireFloat3(
            ResolveSpecularSkyApplication(
                resolved,
                analyticEnvironment,
                { 0.25f, 1.f, 1.f },
                UVSR_SKY_ESTIMATE_SCALAR_VISIBILITY,
                1.f,
                1.f,
                0.5f),
            { 7.f, 7.f, 7.f });
        RequireFloat3(
            ResolveSpecularSkyApplication(
                resolved,
                analyticEnvironment,
                { 0.25f, 0.5f, 1.f },
                UVSR_SKY_ESTIMATE_RATIO_MODULATION,
                1.f,
                1.f,
                1.f),
            { 7.f, 8.f, 10.f });

        // Full stochastic input is already a BRDF-integrated lobe. Only probe
        // scale and specular occlusion apply; no DFG value enters this call.
        RequireFloat3(
            ResolveSpecularSkyApplication(
                analyticEnvironment,
                analyticEnvironment,
                { 2.f, 4.f, 8.f },
                UVSR_SKY_ESTIMATE_FULL_STOCHASTIC_RADIANCE,
                3.f,
                0.5f,
                1.f),
            { 3.f, 6.f, 12.f });

        // A full stochastic lobe replaces only the portion represented by
        // environment routes, while the exact accumulated analytic portion
        // is removed independently of that share.
        RequireFloat3(
            ResolveSpecularSkyApplication(
                resolved,
                analyticEnvironment,
                { 2.f, 4.f, 8.f },
                UVSR_SKY_ESTIMATE_FULL_STOCHASTIC_RADIANCE,
                3.f,
                0.5f,
                0.5f),
            { 7.5f, 9.f, 12.f });

        const float invalid = std::numeric_limits<float>::quiet_NaN();
        RequireFloat3(
            ResolveSpecularSkyApplication(
                analyticEnvironment,
                analyticEnvironment,
                { invalid, 0.5f, 0.5f },
                UVSR_SKY_ESTIMATE_RATIO_MODULATION,
                1.f,
                1.f,
                1.f),
            analyticEnvironment);
        RequireFloat3(
            ResolveSpecularSkyApplication(
                analyticEnvironment,
                analyticEnvironment,
                { invalid, 1.f, 1.f },
                UVSR_SKY_ESTIMATE_FULL_STOCHASTIC_RADIANCE,
                1.f,
                1.f,
                1.f),
            {});
    }

    void TestFootprintMipAndElongation()
    {
        const float grazing = ResolveSpecularElongation(0.25f, 0.1f);
        const float headOn = ResolveSpecularElongation(0.25f, 1.f);
        assert(NearlyEqual(grazing, 0.6f));
        assert(NearlyEqual(headOn, 1.f));

        const SpecularFootprint shortSmooth = ResolveSpecularFootprint(
            0.2f, 5.f, 1.f, 100.f, 0.f, 12u);
        const SpecularFootprint longSmooth = ResolveSpecularFootprint(
            0.2f, 20.f, 1.f, 100.f, 0.f, 12u);
        const SpecularFootprint longRough = ResolveSpecularFootprint(
            0.6f, 20.f, 1.f, 100.f, 0.f, 12u);
        const SpecularFootprint grazingRough = ResolveSpecularFootprint(
            0.6f, 20.f, 0.1f, 100.f, 0.f, 12u);
        assert(longSmooth.mipLevel > shortSmooth.mipLevel);
        assert(longRough.mipLevel > longSmooth.mipLevel);
        assert(grazingRough.traceFootprintPixels >
            longRough.traceFootprintPixels);

        const SpecularFootprint unbiased = ResolveSpecularFootprint(
            0.5f, 10.f, 1.f, 100.f, 0.f, 12u);
        const SpecularFootprint narrowed = ResolveSpecularFootprint(
            0.5f, 10.f, 1.f, 100.f, 1.f, 12u);
        const SpecularFootprint widened = ResolveSpecularFootprint(
            0.5f, 10.f, 1.f, 100.f, -1.f, 12u);
        assert(NearlyEqual(
            narrowed.traceConeTangent,
            unbiased.traceConeTangent * 0.5f));
        assert(NearlyEqual(
            widened.traceConeTangent,
            unbiased.traceConeTangent * 2.f));
        assert(NearlyEqual(narrowed.mipLevel, unbiased.mipLevel));
        assert(NearlyEqual(widened.mipLevel, unbiased.mipLevel));
    }

    void TestReverseDepthAndHybridRouting()
    {
        assert(SpecularRayIsAboveHiZ(0.8f, 0.4f, true));
        assert(!SpecularRayIsAboveHiZ(0.2f, 0.4f, true));
        assert(SpecularRayIsAboveHiZ(0.2f, 0.6f, false));
        assert(!SpecularRayIsAboveHiZ(0.8f, 0.6f, false));

        SpecularSettings settings;
        SpecularRoutingInputs routing;
        assert(ResolveSpecularSampleRoute(settings, routing) ==
            SpecularSampleRoute::InheritedLighting);

        settings.sssr.enabled = true;
        routing.screenConfidence = 0.4f;
        assert(ResolveSpecularSampleRoute(settings, routing) ==
            SpecularSampleRoute::ScreenSpace);
        routing.screenConfidence = 0.f;
        assert(ResolveSpecularSampleRoute(settings, routing) ==
            SpecularSampleRoute::Environment);

        settings.hybrid.enabled = true;
        settings.hybrid.screenSpaceConfidence = 0.8f;
        routing.screenConfidence = 0.9f;
        assert(ResolveSpecularSampleRoute(settings, routing) ==
            SpecularSampleRoute::ScreenSpace);

        routing.screenConfidence = 0.2f;
        routing.rayQuerySupported = true;
        routing.globalRayTraversalAllowed = true;
        routing.coherentTlasReady = true;
        routing.projectableRayHit = true;
        assert(ResolveSpecularSampleRoute(settings, routing) ==
            SpecularSampleRoute::RayTracedScreenRadiance);
        routing.projectableRayHit = false;
        assert(ResolveSpecularSampleRoute(settings, routing) ==
            SpecularSampleRoute::Environment);

        routing.coherentTlasReady = false;
        assert(ResolveSpecularSampleRoute(settings, routing) ==
            SpecularSampleRoute::ScreenSpace);
        settings.hybrid.rayTracing = false;
        routing.coherentTlasReady = true;
        assert(ResolveSpecularSampleRoute(settings, routing) ==
            SpecularSampleRoute::ScreenSpace);
    }

    void TestNearPlaneClippingPreservesTraceableSegment()
    {
        // The long endpoint is behind the camera (W < 0), but a valid hit at
        // t = 0.1 lies before the D3D camera-near plane. The surviving
        // screen-space segment must be traced instead of discarded.
        const float originZ = 0.5f;
        const float originW = 1.f;
        const float endpointZ = 0.5f;
        const float endpointW = -1.f;
        const VisibilityProjectionClipResult clipped =
            ComputeVisibilityProjectionEndpointClip(
                originZ,
                originW,
                endpointZ,
                endpointW,
                true);
        assert(clipped.valid != 0u);
        assert(clipped.clipped != 0u);
        assert(clipped.endpointScale > 0.1f);
        assert(clipped.endpointScale < 1.f);

        const float clippedZ = originZ +
            (endpointZ - originZ) * clipped.endpointScale;
        const float clippedW = originW +
            (endpointW - originW) * clipped.endpointScale;
        assert(clippedW > 0.f);
        assert(clippedW - clippedZ > 0.f);

        const VisibilityProjectionClipResult unchanged =
            ComputeVisibilityProjectionEndpointClip(
                originZ,
                originW,
                0.1f,
                2.f,
                true);
        assert(unchanged.valid != 0u);
        assert(unchanged.clipped == 0u);
        assert(NearlyEqual(unchanged.endpointScale, 1.f));
        assert(ComputeVisibilityProjectionEndpointClip(
            originZ,
            0.f,
            endpointZ,
            endpointW,
            true).valid == 0u);
    }
}

int main()
{
    TestDefaultsAndSanitization();
    TestHybridPrecedence();
    TestRatioEstimatorUsesDestinationBrdfOverSourcePdf();
    TestRatioEstimatorGuardsInvalidWeightsAndFallsBack();
    TestPreintegratedDfgIsAppliedOnceAfterNormalization();
    TestPairedSkySpecularContract();
    TestFootprintMipAndElongation();
    TestReverseDepthAndHybridRouting();
    TestNearPlaneClippingPreservesTraceableSegment();
    return 0;
}
