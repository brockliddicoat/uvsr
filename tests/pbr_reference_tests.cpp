#include "direct_light_visibility.h"
#include "pbr_lighting_debug_contract.h"
#include "pbr_material.h"
#include "pbr_surface_light_contract.h"
#include "ray_material_visibility_contract.h"
#include "ray_origin_contract.h"
#include "ray_visibility_trace_contract.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "image_based_lighting_shared.h"
#include "directional_shadow_settings.h"

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
    bool Near(float a, float b, float tolerance = 1e-5f) { return std::abs(a - b) <= tolerance; }

    void CheckDirectVisibility()
    {
        using namespace uvsr;
        int textureToken = 0, lightToken = 0, unrelatedToken = 0;
        auto* texture = reinterpret_cast<nvrhi::ITexture*>(&textureToken);
        auto* light = reinterpret_cast<const donut::engine::Light*>(&lightToken);
        auto* unrelated = reinterpret_cast<const donut::engine::Light*>(&unrelatedToken);
        const DirectLightVisibility factor{ texture, light };
        Require(TargetsDirectLight(factor, light) && !TargetsDirectLight(factor, unrelated) &&
            !TargetsDirectLight({ texture, nullptr }, light) && !DirectLightVisibility{}.IsComplete(),
            "direct visibility escaped its exact light identity");
        const DirectLightVisibilityTextureProperties properties{
            1920, 1080, 1, 1, 1, 1, true, true, true };
        Require(IsDirectLightVisibilityTextureCompatible(properties, 1920, 1080) &&
            !IsDirectLightVisibilityTextureCompatible(properties, 1919, 1080),
            "direct visibility lost its receiver extent");
        Require(ComposeDirectLightVisibility(.6f, .4f, true) == .4f &&
            ComposeDirectLightVisibility(.6f, .8f, true) == .6f &&
            ComposeDirectLightVisibility(.25f, 0, false) == .25f &&
            ComposeDirectLightVisibility(4, -1, true) == 0,
            "direct visibility did not compose as a bounded matching minimum");
    }

    void CheckLighting()
    {
        using namespace uvsr;
        const auto scales = ResolveImageBasedLightingScales(2, 2, true, .5f, true, 2);
        const auto disabled = ResolveImageBasedLightingScales(2, 2, false, .5f, false, 2);
        Require(scales.radiance == 8 && scales.diffuse == 4 && scales.specular == 16 &&
            disabled.radiance == 8 && disabled.diffuse == 0 && disabled.specular == 0,
            "IBL exposure and lobe controls became coupled");
        for (float roughness : { 0.f, .25f, .5f, 1.f })
        {
            const float mip = ImageBasedLightingReceiverMip(roughness, 9);
            Require(Near(ImageBasedLightingGenerationRoughness(mip / 8), roughness),
                "IBL receiver mip disagrees with prefilter roughness");
            Require(ImageBasedLightingSpecularOcclusion(.35f, 0, roughness) == 0 &&
                ImageBasedLightingSpecularOcclusion(.35f, 1, roughness) == 1,
                "IBL occlusion lost blocked or open endpoints");
        }
        Require(ResolveAnalyticalPositionalLightIntensity(12, 0, .5f, 4) == 3 &&
            Near(ResolveAnalyticalPositionalLightIntensity(12, .1f, .01f, 10000), .0012f, 1e-7f),
            "analytical emitter lost point or far-field energy");
        const float near = ResolveAnalyticalPositionalLightIntensity(12, .1f, 100, .0001f);
        Require(std::isfinite(near) && near > 0 && near < 120000 &&
            ResolvePbrAnalyticalRangeWeight(4, 0) == 1 &&
            Near(ResolvePbrAnalyticalRangeWeight(4, .25f), .5625f) &&
            ResolvePbrAnalyticalRangeWeight(16, .25f) == 0,
            "analytical emitter lost bounded near-field or finite range");
        const float inner = std::cos(.25f), outer = std::cos(.5f);
        Require(ResolvePbrOrdinarySpotWeight(inner, .5f, 1) == 1 &&
            ResolvePbrOrdinarySpotWeight(outer, .5f, 1) == 0 &&
            Near(ResolvePbrOrdinarySpotWeight((inner + outer) * .5f, .5f, 1), .5f),
            "spotlight inner, outer or midpoint weight changed");
    }

    void CheckShadowEmitters()
    {
        uvsr::DirectionalShadowSettings settings;
        settings.samplesPerPixel = 64;
        settings.hardShadows = true;
        Require(uvsr::ResolveRayShadowSampleCount(settings) == 1 &&
                uvsr::ResolveShadowEmitterSize(.53f, true) == 0 && settings.samplesPerPixel == 64,
            "hard shadows must override effective samples and emitter size without erasing them");
        settings.hardShadows = false;
        Require(uvsr::ResolveRayShadowSampleCount(settings) == 64 &&
                uvsr::ResolveShadowEmitterSize(.53f, false) == .53f,
            "disabling hard shadows must recover the stored sampling and emitter size");
        for (const ShaderFloat3 center : { ShaderFloat3{ 0, 0, 1 }, ShaderFloat3{ 0, 1, 0 } })
        {
            const auto hard = SamplePbrDirectionalEmitter(center, 0, { .1f, .9f });
            Require(hard.x == center.x && hard.y == center.y && hard.z == center.z,
                "a zero-angle emitter must use its exact center direction");
            for (float diameter : { .00925f, .5f, 1.5707963f })
            {
                double meanCosine = 0;
                for (int index = 0; index < 4096; ++index)
                {
                    const auto direction = SamplePbrDirectionalEmitter(center, diameter,
                        { (index + .5f) / 4096.f, float(index % 64) / 64.f });
                    const float cosine = ShaderDot(direction, center);
                    Require(Near(ShaderDot(direction, direction), 1.f, 2e-6f) &&
                            cosine >= std::cos(diameter * .5f) - 1e-6f,
                        "sampled shadow direction must be unit length and inside the emitter cone");
                    meanCosine += cosine;
                }
                Require(std::abs(meanCosine / 4096 - (1 + std::cos(diameter * .5)) * .5) < 2e-7,
                    "shadow samples must cover uniform solid angle with the correct mean cosine");
            }
        }
    }

    void CheckMaterialsAndDebug()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        PbrMaterialParameters material;
        material.baseColor.x = nan; material.metalness = 2; material.perceptualRoughness = -1;
        material.ior = 0; material.emissive.x = -2; material.opacity = nan;
        ValidatePbrMaterialParameters(material);
        Require(material.baseColor.x == 1 && material.metalness == 1 && material.perceptualRoughness == 0 &&
            material.ior == 1 && material.emissive.x == 0 && material.opacity == 1 &&
            PbrIorToF0(1) == 0 && Near(PbrIorToF0(1.5f), .04f),
            "material import validation changed");
        const ShaderFloat3 view{ 0, 0, 1 };
        Require(!ShouldFlipPbrSurfaceNormals(true, false, view, view) &&
            ShouldFlipPbrSurfaceNormals(true, true, { 0, 0, -1 }, view) &&
            ShouldFlipPbrSurfaceNormals(false, false, view, view),
            "surface orientation lost winding or two-sided behavior");
        const auto oriented = ResolvePbrTriangleSurfaceNormals(
            { 0, 3, 0 }, { 2, 0, 0 }, { 0, 1, 0 }, { .6f, 0, -.8f }, view);
        Require(Near(oriented.geometricNormal.z, 1) && Near(oriented.shadingNormal.x, -.6f) &&
            Near(oriented.shadingNormal.z, .8f), "triangle shading and geometry disagree on hemisphere");
        const auto fallback = ResolvePbrTrianglePlaneNormal({}, {}, { 0, 1, 0 });
        Require(fallback.y == 1, "degenerate triangle lost its material normal");

        const auto debug = ResolvePbrSkyVisibilityDebugColor(.25f);
        const auto invalid = ResolvePbrSkyVisibilityDebugColor(nan);
        Require(debug.x == .25f && debug.y == .25f && debug.z == .25f &&
            invalid.x == 1 && invalid.y == 1 && invalid.z == 1,
            "sky visibility debug lost grayscale or invalid-sample behavior");
        Require(PbrNeedsSkyVisibilitySample(UVSR_PBR_LIGHTING_DEBUG_SKY_VISIBILITY, false, false) &&
            !PbrNeedsSkyVisibilitySample(UVSR_PBR_LIGHTING_DEBUG_NONE, false, false),
            "sky visibility debug routing changed");
    }

    void CheckRayVisibility()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto normal = RayOriginOrientGeometricNormal({ 0, 0, -2 }, { 0, 0, 1 });
        Require(normal.x == 0 && normal.y == 0 && normal.z == 1, "ray origin normal lost its view hemisphere");
        Require(RayOriginStepDepthTowardCamera(.5f, true, true, 0) == std::nextafter(.5f, 1.f) &&
            RayOriginStepDepthTowardCamera(.5f, true, false, 0) == std::nextafter(.5f, 0.f) &&
            RayOriginStepDepthTowardCamera(1, true, true, 0) == 1 &&
            RayOriginStepDepthTowardCamera(0, true, false, 0) == 0, "float depth lost its camera-directed ULP step");
        Require(Near(RayOriginStepDepthTowardCamera(.5f, false, true, 1.f / 1024), .5f + 1.f / 1024, 1e-6f) &&
            Near(RayOriginStepDepthTowardCamera(.5f, false, false, 1.f / 1024), .5f - 1.f / 1024, 1e-6f),
            "integer depth ignored its configured forward/reverse step");
        Require(RayOriginOffsetFloatComponent(1, 1) > 1 && RayOriginOffsetFloatComponent(1, -1) < 1 &&
            RayOriginOffsetFloatComponent(-1, 1) > -1 && RayOriginOffsetFloatComponent(-1, -1) < -1 &&
            Near(RayOriginOffsetFloatComponent(0, 1), 1.f / 65536, 1e-6f), "signed ray origin offset changed");
        Require(Near(ResolveRayOriginClearance(.01f, .02f), .02f, 1e-6f) &&
            Near(ResolveRayOriginClearance(.03f, .02f), .03f, 1e-6f) &&
            ResolveRayOriginClearance(-1, nan) == 0, "ray clearance lost its user/depth bias boundary");
        const auto position = ResolveRayOriginPosition({ 1, -1, 0 }, { 0, 0, 1 }, .01f);
        Require(position.x == 1 && position.y == -1 && position.z > .01f,
            "ray origin failed to combine clearance with representable offset");

        const auto opaque = ResolveRayMaterialCoveragePlan(true, false, false, false, true, false, true, true);
        Require(opaque.mode == UVSR_RAY_MATERIAL_COVERAGE_OPAQUE &&
            ResolveRayMaterialCandidateCoverage(opaque, 0, 0, 1, false) &&
            ResolveRayMaterialCoveragePlan(false, false, false, false, true, false, false, false).mode ==
                UVSR_RAY_MATERIAL_COVERAGE_REJECT &&
            ResolveRayMaterialCoveragePlan(false, true, false, false, true, false, false, false).mode ==
                UVSR_RAY_MATERIAL_COVERAGE_OPAQUE, "opaque/backface/double-sided ray acceptance changed");
        Require(ResolveRayMaterialCoveragePlan(true, false, true, true, true, false, false, false).mode ==
                UVSR_RAY_MATERIAL_COVERAGE_REJECT &&
            ResolveRayMaterialCoveragePlan(true, false, false, false, false, false, false, false).mode ==
                UVSR_RAY_MATERIAL_COVERAGE_REJECT, "unsupported or transparent material became a blocker");
        const auto opacity = ResolveRayMaterialCoveragePlan(true, false, false, false, false, true, true, true);
        const auto base = ResolveRayMaterialCoveragePlan(true, false, false, false, false, true, false, true);
        const auto scalar = ResolveRayMaterialCoveragePlan(true, false, false, false, false, true, false, false);
        Require(opacity.alphaSource == UVSR_RAY_MATERIAL_ALPHA_OPACITY_TEXTURE &&
            base.alphaSource == UVSR_RAY_MATERIAL_ALPHA_BASE_TEXTURE && scalar.alphaSource == UVSR_RAY_MATERIAL_ALPHA_NONE &&
            !ResolveRayMaterialCandidateCoverage(opacity, 1, .2f, .5f, true) &&
            ResolveRayMaterialCandidateCoverage(base, 1, .9f, .5f, true) &&
            !ResolveRayMaterialCandidateCoverage(opacity, 1, 1, .5f, false),
            "ray alpha source precedence or descriptor rejection changed");
        Require(ResolveRayMaterialCandidateCoverage(scalar, .5f, 0, .5f, true) &&
            !ResolveRayMaterialCandidateCoverage(scalar, .499f, 1, .5f, true) &&
            ResolveRayMaterialCandidateCoverage(scalar, 2, 1, 1, true) &&
            !ResolveRayMaterialCandidateCoverage(scalar, nan, 1, .5f, true),
            "ray alpha lost exact cutoff, saturation or finite rejection");

        const auto miss = ResolveRayVisibilityTraceSample(false);
        const auto hit = ResolveRayVisibilityTraceSample(true);
        Require(miss.queryCount == 1 && !miss.occluded && miss.visibility == 1 &&
            hit.queryCount == 1 && hit.occluded && hit.visibility == 0,
            "ray hit/miss encoding lost its binary one-query contract");
        for (const auto samples : { std::array{ miss, hit, hit }, std::array{ hit, miss, hit } })
        {
            auto aggregate = BeginRayVisibilityTraceAggregate();
            for (const auto sample : samples)
                aggregate = AccumulateRayVisibilityTraceSample(aggregate, sample);
            Require(aggregate.queryCount == 3 && aggregate.sampleCount == 3 && aggregate.visibleSampleCount == 1 &&
                Near(ResolveRayVisibilityTraceAverage(aggregate), 1.f / 3, 1e-6f) &&
                RayVisibilityTraceAggregateIsComplete(aggregate, 3) && !RayVisibilityTraceAggregateIsComplete(aggregate, 4),
                "ray reduction lost order independence or one query per sample");
        }
    }

}

int main()
{
    CheckDirectVisibility();
    CheckLighting();
    CheckShadowEmitters();
    CheckMaterialsAndDebug();
    CheckRayVisibility();
    return EXIT_SUCCESS;
}
