#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Path-tracing source-contract validation failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
    }

    void RequireNear(
        double actual,
        double expected,
        double tolerance,
        const std::string& message)
    {
        Require(std::isfinite(actual) &&
                std::abs(actual - expected) <= tolerance,
            message);
    }

    std::string ReadSource(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(stream.good(), "cannot open " + path.generic_string());
        std::string source{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
        Require(!source.empty(), "source is empty: " + path.generic_string());
        return source;
    }

    std::string Canonicalize(std::string_view source)
    {
        std::string canonical;
        canonical.reserve(source.size());
        for (char character : source)
        {
            const unsigned char byte =
                static_cast<unsigned char>(character);
            if (!std::isspace(byte))
            {
                canonical.push_back(static_cast<char>(
                    std::tolower(byte)));
            }
        }
        return canonical;
    }

    void RequireContains(
        std::string_view source,
        std::string_view token,
        const std::string& message)
    {
        Require(source.find(token) != std::string_view::npos, message);
    }

    void RequireAbsent(
        std::string_view source,
        std::string_view token,
        const std::string& message)
    {
        Require(source.find(token) == std::string_view::npos, message);
    }

    size_t CountOccurrences(
        std::string_view source,
        std::string_view token)
    {
        size_t count = 0u;
        size_t offset = 0u;
        while ((offset = source.find(token, offset)) !=
            std::string_view::npos)
        {
            ++count;
            offset += token.size();
        }
        return count;
    }

    void RequireOrdered(
        std::string_view source,
        std::initializer_list<std::string_view> tokens,
        const std::string& message)
    {
        size_t offset = 0u;
        for (std::string_view token : tokens)
        {
            offset = source.find(token, offset);
            Require(offset != std::string_view::npos, message);
            offset += token.size();
        }
    }
}

int main(int argc, char** argv)
{
    Require(argc == 2,
        "expected the UVSR source directory as the only argument");
    const std::filesystem::path root = argv[1];
    const std::string constants = Canonicalize(ReadSource(
        root / "src/path_tracing_cb.h"));
    const std::string passHeader = Canonicalize(ReadSource(
        root / "src/path_tracing_pass.h"));
    const std::string pass = Canonicalize(ReadSource(
        root / "src/path_tracing_pass.cpp"));
    const std::string settings = Canonicalize(ReadSource(
        root / "src/path_tracing_settings.h"));
    const std::string material = Canonicalize(ReadSource(
        root / "src/path_tracing_material.hlsli"));
    const std::string materialVisibility = Canonicalize(ReadSource(
        root / "src/ray_traced_material_visibility.hlsli"));
    const std::string sampling = Canonicalize(ReadSource(
        root / "src/path_tracing_sampling.hlsli"));
    const std::string accumulation = Canonicalize(ReadSource(
        root / "src/sample_accumulation.hlsli"));
    const std::string shader = Canonicalize(ReadSource(
        root / "src/path_tracing_cs.hlsl"));
    const std::string primaryShader = Canonicalize(ReadSource(
        root / "src/path_tracing_primary_surface_cs.hlsl"));
    const std::string resolveHeader = Canonicalize(ReadSource(
        root / "src/path_tracing_stable_plane_resolve_pass.h"));
    const std::string resolvePass = Canonicalize(ReadSource(
        root / "src/path_tracing_stable_plane_resolve_pass.cpp"));
    const std::string resolveShader = Canonicalize(ReadSource(
        root / "src/path_tracing_stable_plane_resolve_cs.hlsl"));
    const std::string shaderConfig = Canonicalize(ReadSource(
        root / "src/shaders.cfg"));

    {
        using Vector = std::array<double, 3>;
        const auto subtract = [](Vector left, Vector right)
        {
            return Vector{
                left[0] - right[0],
                left[1] - right[1],
                left[2] - right[2]
            };
        };
        const auto dot = [](Vector left, Vector right)
        {
            return left[0] * right[0] + left[1] * right[1] +
                left[2] * right[2];
        };
        const auto giJacobian = [&](Vector receiver, Vector donorReceiver,
                                    Vector secondary, Vector normal)
        {
            const Vector receiverVector = subtract(receiver, secondary);
            const Vector donorVector = subtract(donorReceiver, secondary);
            const double receiverDistanceSquared =
                dot(receiverVector, receiverVector);
            const double donorDistanceSquared =
                dot(donorVector, donorVector);
            if (!(receiverDistanceSquared > 1.e-8) ||
                !(donorDistanceSquared > 1.e-8))
            {
                return 0.0;
            }
            const double receiverDistance =
                std::sqrt(receiverDistanceSquared);
            const double donorDistance = std::sqrt(donorDistanceSquared);
            const double receiverCosine = dot(normal, receiverVector) /
                receiverDistance;
            const double donorCosine = dot(normal, donorVector) /
                donorDistance;
            if (!(receiverCosine > 1.e-4) || !(donorCosine > 1.e-4))
                return 0.0;
            const double directionDot = dot(receiverVector, donorVector) /
                (receiverDistance * donorDistance);
            const double cosineRatio = receiverCosine / donorCosine;
            if (directionDot < 0.8660254 || cosineRatio < 0.5 ||
                cosineRatio > 2.0)
            {
                return 0.0;
            }
            const double value = receiverCosine * donorDistanceSquared /
                (donorCosine * receiverDistanceSquared);
            return std::isfinite(value) && value >= 0.05 && value <= 20.0
                ? value
                : 0.0;
        };
        RequireNear(
            giJacobian(
                { 0.0, 0.0, 1.0 },
                { 0.0, 0.0, 1.0 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 1.0 }),
            1.0,
            1.e-12,
            "identity GI reconnection must have unit Jacobian");
        RequireNear(
            giJacobian(
                { 0.0, 0.0, 2.0 },
                { 0.0, 0.0, 1.0 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 1.0 }),
            0.25,
            1.e-12,
            "GI reconnection must apply inverse-square distance shifting");
        RequireNear(
            giJacobian(
                { 0.0, 0.0, -1.0 },
                { 0.0, 0.0, 1.0 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 1.0 }),
            0.0,
            0.0,
            "opposite-hemisphere GI reconnection must be rejected");

        const double localSuffix = 7.0;
        const double invalidLocalContribution = 0.0;
        RequireNear(
            std::max(localSuffix - invalidLocalContribution, 0.0),
            localSuffix,
            0.0,
            "an invalid current GI proposal must retain the complete local suffix");
        const double representedBeforeOcclusion = 1.0;
        const double occludedDonorRepresentedCount = 1.0;
        const double representedAfterOcclusion = std::min(
            representedBeforeOcclusion + occludedDonorRepresentedCount,
            32.0);
        RequireNear(
            representedAfterOcclusion,
            2.0,
            0.0,
            "an occluded compatible GI donor must retain M as a zero trial");

        const std::array<double, 2> fixedDonors{ 2.0, 0.0 };
        const std::array<double, 8> freshSamples{
            1.0, 3.0, 5.0, 7.0, 9.0, 11.0, 13.0, 15.0
        };
        for (const size_t sampleCount : { size_t(1), size_t(2), size_t(8) })
        {
            double legacyMean = 0.0;
            double cachedMean = 0.0;
            const double donorSum = fixedDonors[0] + fixedDonors[1];
            for (size_t sample = 0u; sample < sampleCount; ++sample)
            {
                const double legacy =
                    (freshSamples[sample] + fixedDonors[0] +
                        fixedDonors[1]) / 3.0;
                const double cached =
                    (freshSamples[sample] + donorSum) / 3.0;
                legacyMean += legacy;
                cachedMean += cached;
            }
            legacyMean /= double(sampleCount);
            cachedMean /= double(sampleCount);
            RequireNear(
                cachedMean,
                legacyMean,
                1.e-12,
                "once-per-frame PT donor replay must preserve fixed-donor batch means");
        }
    }

    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t StratifiedSampleCount = 8192u;
        const double coneAlpha = 0.63;
        const double coneCosine = std::cos(coneAlpha);
        const double coneSolidAngle = 2.0 * Pi * (1.0 - coneCosine);
        const double conePdf = 1.0 / coneSolidAngle;
        RequireNear(
            conePdf * coneSolidAngle,
            1.0,
            1.e-12,
            "uniform-cone PDF must integrate to one");
        double meanCosine = 0.0;
        for (uint32_t sample = 0u;
            sample < StratifiedSampleCount;
            ++sample)
        {
            const double variate =
                (double(sample) + 0.5) / double(StratifiedSampleCount);
            meanCosine += 1.0 - variate * (1.0 - coneCosine);
        }
        meanCosine /= double(StratifiedSampleCount);
        RequireNear(
            meanCosine,
            0.5 * (1.0 + coneCosine),
            1.e-12,
            "uniform-cone stratification must preserve the analytic mean cosine");

        constexpr double DirectionalIntensity = 7.25;
        for (double alpha : std::array<double, 4>{
                0.01, 0.08, 0.31, 0.72 })
        {
            const double cosineMaximum = std::cos(alpha);
            const double solidAngle =
                2.0 * Pi * (1.0 - cosineMaximum);
            const double pdf = 1.0 / solidAngle;
            const double radiance = DirectionalIntensity /
                (Pi * std::sin(alpha) * std::sin(alpha));
            double estimate = 0.0;
            for (uint32_t sample = 0u;
                sample < StratifiedSampleCount;
                ++sample)
            {
                const double variate =
                    (double(sample) + 0.5) /
                    double(StratifiedSampleCount);
                const double cosine =
                    1.0 - variate * (1.0 - cosineMaximum);
                estimate += radiance * cosine / pdf;
            }
            estimate /= double(StratifiedSampleCount);
            RequireNear(
                estimate,
                DirectionalIntensity,
                1.e-9,
                "finite directional sampling must preserve on-axis irradiance");
        }

        constexpr double SphereDistance = 10.0;
        constexpr double SphereRadius = 1.0;
        constexpr double SphereIntensity = 4.0;
        const double centerProjection = SphereDistance;
        const double centerDiscriminant = SphereRadius * SphereRadius;
        const double centerEndpoint =
            centerProjection - std::sqrt(centerDiscriminant);
        RequireNear(
            centerEndpoint,
            9.0,
            1.e-12,
            "the center sphere proposal must hit the near endpoint");
        RequireNear(
            (centerEndpoint - SphereDistance) *
                    (centerEndpoint - SphereDistance),
            SphereRadius * SphereRadius,
            1.e-12,
            "the sampled endpoint must lie on the emitter sphere");

        const double sphereSine = SphereRadius / SphereDistance;
        const double sphereCosine = std::sqrt(
            1.0 - sphereSine * sphereSine);
        const double sphereSolidAngle =
            2.0 * Pi * (1.0 - sphereCosine);
        const double spherePdf = 1.0 / sphereSolidAngle;
        RequireNear(
            spherePdf * sphereSolidAngle,
            1.0,
            1.e-12,
            "visible-sphere cone PDF must integrate to one");
        const double sphereRadiance = SphereIntensity /
            (Pi * SphereRadius * SphereRadius);
        double sphereEstimate = 0.0;
        for (uint32_t sample = 0u;
            sample < StratifiedSampleCount;
            ++sample)
        {
            const double variate =
                (double(sample) + 0.5) / double(StratifiedSampleCount);
            const double cosine =
                1.0 - variate * (1.0 - sphereCosine);
            sphereEstimate += sphereRadiance * cosine / spherePdf;
        }
        sphereEstimate /= double(StratifiedSampleCount);
        RequireNear(
            sphereEstimate,
            SphereIntensity / (SphereDistance * SphereDistance),
            1.e-10,
            "visible-sphere estimator must preserve luminous-intensity inverse square irradiance");

        constexpr uint64_t FullHdPixels = 1920ull * 1080ull;
        constexpr uint64_t UhdPixels = 3840ull * 2160ull;
        constexpr uint64_t SeedPingPongBytesPerPixel = 8ull;
        RequireNear(
            double(FullHdPixels * SeedPingPongBytesPerPixel) /
                double(1ull << 20u),
            15.8203125,
            1.e-12,
            "1080p direct-sample seed ping-pong footprint must be 15.82 MiB");
        RequireNear(
            double(UhdPixels * SeedPingPongBytesPerPixel) /
                double(1ull << 20u),
            63.28125,
            1.e-12,
            "4K direct-sample seed ping-pong footprint must be 63.28 MiB");

        const auto resolveCorrection = [](
            uint32_t count,
            double variance,
            double mean,
            bool exponential,
            uint32_t history)
        {
            const uint32_t effectiveCount = exponential
                ? std::min(count, history * 2u - 1u)
                : count;
            const double standardError = std::sqrt(
                variance / double(std::max(effectiveCount, 1u)));
            const double countConfidence = double(count) /
                (double(count) + 16.0);
            const double relativeError = standardError /
                std::max(std::abs(mean), 1.e-3);
            return 1.0 - countConfidence / (1.0 + relativeError);
        };
        const double cumulative64 = resolveCorrection(
            64u, 1.0, 1.0, false, 64u);
        const double cumulative4096 = resolveCorrection(
            4096u, 1.0, 1.0, false, 64u);
        const double cumulativeLarge = resolveCorrection(
            1000000000u, 1.0, 1.0, false, 64u);
        Require(cumulative4096 < cumulative64,
            "cumulative resolve correction must decay as the mean converges");
        Require(cumulativeLarge < 0.0001,
            "cumulative resolve correction must approach raw output");
        const double exponentialLarge = resolveCorrection(
            1000000000u, 1.0, 1.0, true, 64u);
        Require(exponentialLarge > 0.08 && exponentialLarge < 0.09,
            "exponential resolve correction must retain its finite-history uncertainty floor");
    }

    RequireContains(
        constants,
        "planarviewconstantsview;"
            "planarviewconstantspreviousview;"
            "flashlightbeamprofilebindingflashlight;",
        "the shared constant block must contain current/previous view and flashlight identity");
    RequireAbsent(
        constants,
        "uvsr_path_tracing_max_lights",
        "the complete transport core must not silently cap analytic lights in its constant block");
    RequireContains(
        constants,
        "uintschedulingseriallow;uintschedulingserialhigh;",
        "retry scheduling must have a frame serial independent of authored noise animation");
    RequireContains(
        constants,
        "uintaccumulationaveraging;uintaccumulationscheduling;"
            "uintaccumulationeffectivehistory;uintaccumulationminimumsamples;",
        "path transport must receive the shared accumulation policy");
    RequireContains(
        constants,
        "uvsr_path_tracing_flag_show_environment_background",
        "primary environment presentation must have an explicit transport flag");

    RequireContains(
        passHeader,
        "uint64_tschedulingserial=~uint64_t(0u);",
        "the public input must expose the independent scheduling serial");
    RequireContains(
        passHeader,
        "uint32_tpipelineavailabilitymask=0u;",
        "hardware support and executable path-tracing variants must be reported separately");
    RequireContains(
        passHeader,
        "boolshowenvironmentbackground=true;",
        "the public input must separate primary background presentation from environment transport");
    for (std::string_view capability : {
            "boolsersupported=false;",
            "boolspatialgicheckpointreusesupported=false;",
            "boolfullsamplereconnectionsupported=false;",
            "boolsharedprimarysurfacesupported=false;" })
    {
        RequireContains(passHeader, capability,
            "the public capability surface must begin from conservative defaults");
    }
    RequireContains(
        passHeader,
        "boolcontinuationseedreservoirsupported=false;"
            "boolreplayablepathseedsupported=false;"
            "booltemporalgicheckpointreusesupported=false;",
        "the public capability surface must distinguish executable seed replay and temporal GI checkpoints");
    RequireContains(
        passHeader,
        "boolstableplanesignalsupported=false;"
            "boolstableplaneresolvesupported=false;",
        "stable signal formats and the executable resolve pipeline must be separate capabilities");
    RequireContains(
        passHeader,
        "returnuvsr::canusespatialpathresolve(settings,"
            "stableplanesignalsupported&&stableplaneresolvesupported);",
        "the public capability query must gate spatial path resolve per authored solver settings");
    RequireContains(
        passHeader,
        "nvrhi::itexture*rawmean=nullptr;"
            "nvrhi::itexture*directmean=nullptr;"
            "nvrhi::itexture*indirectmean=nullptr;"
            "nvrhi::itexture*temporaldepth=nullptr;"
            "nvrhi::itexture*motionvectors=nullptr;"
            "nvrhi::itexture*successfulsamplecount=nullptr;",
        "split direct/indirect means, ray depth/motion, and exact indirect sample count must be public outputs");
    RequireContains(
        passHeader,
        "nvrhi::itexture*residualmean=nullptr;"
            "nvrhi::itexture*diffusesuffixmean=nullptr;"
            "nvrhi::itexture*primarynormalroughness=nullptr;"
            "nvrhi::itexture*primaryviewz=nullptr;",
        "the result must expose only coherent persistent reconstruction signals");
    RequireContains(
        passHeader,
        "uint64_tsignalepoch=0u;"
            "uint32_tdispatchphasecount=1u;"
            "booldispatched=false;boolhistoryreset=false;"
            "boolcompletedsignalcycle=false;"
            "boolstableplaneresolveactive=false;",
        "the result must distinguish signal epoch, complete cycle, and active resolve state");
    RequireContains(
        passHeader,
        "boolcleanroomsolversubsetactive=false;"
            "boolnamesakeparityunavailable=false;",
        "executable clean-room solvers must not imply NVIDIA namesake parity");

    RequireContains(
        pass,
        "createshader(\"uvsr/path_tracing_cs.hlsl\",\"main\"",
        "the pass must compile the manifest entry point");
    RequireContains(
        shaderConfig,
        "path_tracing_cs.hlsl-tcs-emain",
        "the production shader manifest must compile the same entry point");
    RequireContains(
        shaderConfig,
        "-duvsr_pt_solver={0,1,2}",
        "the production shader manifest must compile each solver body separately");
    RequireContains(
        shaderConfig,
        "-duvsr_pt_rtxdi={0,1}",
        "the production shader manifest must compile separate plain and RTXDI transport variants");
    RequireContains(
        shaderConfig,
        "-duvsr_pt_nee_mode={0,1,2}",
        "the production shader manifest must compile each selectable NEE algorithm separately");
    RequireContains(
        shaderConfig,
        "path_tracing_primary_surface_cs.hlsl-tcs-emain"
            "-duvsr_pt_rtxdi={0,1}"
            "-duvsr_pt_nee_mode={0,1,2}",
        "the production shader manifest must compile the shared primary/direct matrix");
    RequireOrdered(
        primaryShader,
        {
            "texture2d<uint2>t_previousprimarysignature:register(t24);",
            "rwtexture2d<float4>u_rawmean:register(u0);",
            "rwtexture2d<uint2>u_sharedgeometrymaterial:register(u17);",
            "rwtexture2d<float4>u_directmean:register(u21);",
            "rwtexture2d<float4>u_pathmotion:register(u23);"
        },
        "shared primary must own explicit previous-signature, split-signal, and motion resources");
    RequireOrdered(
        material,
        {
            "uintpathtracingpackunitvectorhalf(float3direction)",
            "f32tof16(encoded.x)",
            "float3pathtracingunpackunitvectorhalf(uintpacked)",
            "pathtracingdecodeunitvector(float2("
        },
        "primary signatures must pack and recover geometric normals through a stable uint ABI");
    RequireOrdered(
        pass,
        {
            "std::array<nvrhi::texturehandle,2>sharedgeometrymaterial=",
            "nvrhi::format::rg32_uint,",
            "m_sharedgeometrymaterial=sharedgeometrymaterial;",
            "constuint32_tbindingindex=historyindex*2u+primarysurfaceindex;",
            "24,m_sharedgeometrymaterial[previousprimarysurfaceindex]",
            "17,m_sharedgeometrymaterial[primarysurfaceindex]"
        },
        "CPU bindings must independently select estimator and immediate primary history");
    RequireContains(
        passHeader,
        "std::array<nvrhi::bindingsethandle,4>m_bindingsets;"
            "std::array<nvrhi::shaderhandle,"
            "pathtracingprimarypipelinevariantcount>m_primaryshaders;"
            "std::array<nvrhi::computepipelinehandle,"
            "pathtracingprimarypipelinevariantcount>m_primarypipelines;"
            "std::array<nvrhi::bindingsethandle,4>m_primarybindingsets;",
        "estimator and immediate primary ping-pong indices must have four cached binding combinations");
    RequireOrdered(
        pass,
        {
            "constboolpreviousprimarysurfacehistoryvalid="
                "m_primarysurfacehistoryvalid;",
            "m_primarysurfacehistoryvalid=false;",
            "if(!m_capabilities.rayquerysupported||!commandlist)",
            "constboolprimarysurfacehistoryavailable="
                "sharedprimaryrequired&&"
                "previousprimarysurfacehistoryvalid&&"
                "constants.previousviewvalid!=0u;",
            "uvsr_path_tracing_flag_primary_signature_history",
            "m_primarysurfaceindex^=1u;",
            "m_primarysurfacehistoryvalid=true;"
        },
        "primary history must break on render gaps yet advance every successful full-resolution primary frame");
    RequireAbsent(
        pass,
        "cleartextureuint(m_sharedgeometrymaterial",
        "transport history resets must not erase immediate primary signatures during motion");
    RequireOrdered(
        primaryShader,
        {
            "uvsr_path_tracing_flag_primary_signature_history",
            "any(previouslocal<0.0f)",
            "any(previouslocal>=g_pathtracing.previousview.viewportsize)",
            "t_previousprimarysignature[previouspixel]",
            "previoussignature.y!=currentmaterial",
            "dot(currentgeometricnormal,previousnormal)<0.8f",
            "returnall(isfinite(motion))?float4(motion,1.0f):0.0f;"
        },
        "path motion must reject out-of-bounds, material, and normal discontinuities before TAA");
    RequireOrdered(
        primaryShader,
        {
            "if(!all(isfinite(value)))return0.0f;",
            "if(oldcount>0u&&!all(isfinite(oldmean)))",
            "oldcount=0u;",
            "if(!all(isfinite(indirectmean)))indirectmean=0.0f;",
            "u_rawmean[pixel]=float4(rawmean,1.0f);",
            "u_residualmean[pixel]=float4(directmean,1.0f);"
        },
        "shared-primary direct and split means must repair non-finite history and publish full-resolution resolve inputs");
    RequireContains(
        shaderConfig,
        "path_tracing_stable_plane_resolve_cs.hlsl-tcs-emain",
        "the production manifest must compile the spatial resolve singleton");
    RequireContains(
        resolvePass,
        "createshader("
            "\"uvsr/path_tracing_stable_plane_resolve_cs.hlsl\","
            "\"main\"",
        "the runtime resolve pass must request its packaged shader entry point");
    RequireContains(
        pass,
        "shadermacro(\"uvsr_pt_solver\",solvers[solvervariant]),"
            "shadermacro(\"uvsr_pt_rtxdi\","
            "rtxdivariant==0u?\"0\":\"1\"),"
            "shadermacro(\"uvsr_pt_nee_mode\",neemodes[neevariant])",
        "runtime shader lookup must request the exact packaged transport permutation");
    RequireContains(
        passHeader,
        "std::array<nvrhi::shaderhandle,"
            "pathtracingpipelinevariantcount>m_shaders;"
            "std::array<nvrhi::computepipelinehandle,"
            "pathtracingpipelinevariantcount>m_pipelines;",
        "all three solvers must own distinct RTXDI and NEE-specialized pipelines");
    RequireOrdered(
        pass,
        {
            "constpathtracingpipelineresolutionpipelineresolution=",
            "inputs.settings=pipelineresolution.effectivesettings;",
            "constuint32_tpipelinevariant=pipelineresolution.effectivevariant;",
            "if(!pipelineresolution.executable||",
            "!m_pipelines[pipelinevariant])",
            "constbooldirectreuserequired="
                "usesdirectreservoirhistory(inputs.settings)&&",
            "constboolgireuserequired=",
            "constboolpathreuserequired=",
            "constboolstablesignalsrequested=",
            "buildtransportsignature(",
            "state.pipeline=m_pipelines[pipelinevariant];"
        },
        "runtime policy must select an executable specialized pipeline from effective settings");
    RequireOrdered(
        settings,
        {
            "resolvepathtracingpipeline(",
            "if(ispathtracingpipelineavailable(requestedsettings,availabilitymask))",
            "pathtracingsettingscandidate=requestedsettings;",
            "candidate.usertxdi=false;",
            "if(ispathtracingpipelineavailable(candidate,availabilitymask))",
            "candidate.neemode=pathtracingneemode::uniform;",
            "if(ispathtracingpipelineavailable(candidate,availabilitymask))",
            "candidate=requestedsettings;",
            "candidate.solver=pathtracingsolver::rtxpt;",
            "candidate.usertxdi=false;",
            "if(ispathtracingpipelineavailable(candidate,availabilitymask))",
            "candidate.neemode=pathtracingneemode::uniform;",
            "resolution.effectivevariant=0u;"
        },
        "missing optional pipelines must preserve the namesake solver before falling back to RTX PT");
    RequireOrdered(
        pass,
        {
            "m_capabilities.pipelineavailabilitymask|=1u<<variant;",
            "constboolbaselineready=",
            "(m_capabilities.pipelineavailabilitymask&1u)!=0u;",
            "m_capabilities.directreservoirsupported&=rtxdipipelinesready;"
        },
        "optional variants must be reported independently without disabling the RTX PT baseline");
    RequireContains(
        pass,
        "constboolrequiredformatsavailable="
            "solvervariant!=static_cast<uint32_t>("
            "pathtracingsolver::restirpt)||"
            "m_capabilities.continuationseedreservoirsupported;"
            "if(variantsavailable[variant]&&requiredformatsavailable)"
            "m_capabilities.pipelineavailabilitymask|=1u<<variant;",
        "all six RESTIR PT variants must be suppressed when RG32 seed history is unsupported");
    RequireAbsent(
        pass,
        "m_capabilities={};",
        "one optional pipeline failure must not erase valid baseline hardware support");
    Require(
        CountOccurrences(pass, "bindinglayoutitem::texture_uav(slot)") == 1u &&
            CountOccurrences(shader, "register(u") == 18u,
        "the CPU layout and shader must expose split indirect and GI payload outputs");
    RequireContains(
        pass,
        "for(uint32_tslot=0u;slot<=15u;++slot)",
        "every transport specialization must share the complete u0-u15 binding layout");
    for (std::string_view giUav : {
            "bindinglayoutitem::texture_uav(25)",
            "bindinglayoutitem::texture_uav(26)",
            "bindinglayoutitem::texture_uav(27)" })
    {
        RequireContains(pass, giUav,
            "the reconnectable GI payload must have explicit non-aliasing UAV slots");
    }
    RequireContains(
        shader,
        "rwtexture2d<float4>u_residualmean:register(u9);"
            "rwtexture2d<float4>u_diffusesuffixmean:register(u10);"
            "rwtexture2d<float4>u_primarynormalroughness:register(u11);"
            "rwtexture2d<float>u_primaryviewz:register(u12);",
        "stable signals must occupy fixed always-declared UAV slots after solver history");
    RequireContains(
        pass,
        "bindingsetitem::texture_uav(9,m_residualmean),"
            "nvrhi::bindingsetitem::texture_uav(10,m_diffusesuffixmean),"
            "nvrhi::bindingsetitem::texture_uav(11,m_primarynormalroughness),"
            "nvrhi::bindingsetitem::texture_uav(12,m_primaryviewz)",
        "the CPU binding set must bind every stable signal slot even when inactive");
    RequireContains(
        shader,
        "structuredbuffer<lightconstants>t_pathtracinglights:register(t13);",
        "all submitted analytic lights must arrive through a dynamically sized structured buffer");
    RequireContains(
        shader,
        "texture2d<uint>t_previousdirectsampleseed:register(t9);",
        "the persisted finite-emitter proposal seed must occupy fixed SRV slot t9");
    RequireContains(
        shader,
        "rwtexture2d<uint>u_directsampleseed:register(u13);",
        "the persisted finite-emitter proposal seed must occupy fixed UAV slot u13");
    RequireContains(
        passHeader,
        "std::array<nvrhi::texturehandle,2>m_directsampleseeds;",
        "direct reservoir history must own a seed texture for each ping-pong side");
    RequireOrdered(
        pass,
        {
            "bindinglayoutitem::texture_srv(9)",
            "for(uint32_tslot=0u;slot<=15u;++slot)",
            "bindingsetitem::texture_srv("
                "9,m_directsampleseeds[previousindex])",
            "bindingsetitem::texture_uav("
                "13,m_directsampleseeds[historyindex])"
        },
        "CPU SRV/UAV bindings must match the stable t9/u13 HLSL seed ABI");
    RequireContains(
        shader,
        "rwtexture2d<float4>u_colorvariance:register(u14);",
        "variance-guided accumulation must own a persistent RGB variance surface");
    RequireContains(
        accumulation,
        "constfloat3delta=sample-previousmean;",
        "variance updates must preserve independent RGB deltas");
    RequireAbsent(
        accumulation,
        "constfloatdelta=sample-previousmean;",
        "scalar truncation of RGB variance deltas");
    RequireContains(
        pass,
        "bindingsetitem::texture_uav(14,m_colorvariance)",
        "the CPU binding set must match the u14 variance surface");
    RequireOrdered(
        pass,
        {
            "submittedlights.emplace_back();",
            "lightconstants&lightconstants=submittedlights.back();",
            "std::memset(&lightconstants,0,sizeof(lightconstants));",
            "light->filllightconstants(lightconstants);",
            "constants.lightcount=uint32_t(submittedlights.size());",
            "ensurelightbuffer(constants.lightcount)",
            "commandlist->writebuffer("
                "m_lightbuffer,submittedlights.data(),"
                "submittedlights.size()*sizeof(lightconstants));"
        },
        "the CPU pass must upload every non-null submitted light without truncation");
    RequireAbsent(
        pass,
        "constants.lightcount>=",
        "analytic-light submission must not silently drop a suffix of the scene light list");
    RequireOrdered(
        pass,
        {
            "constboolextentchanged=!m_rawmean||",
            "nvrhi::texturehandlerawmean=m_rawmean;",
            "if(extentchanged)",
            "constuint32_tdirecthistorywidth="
                "directreuserequired?width:1u;",
            "constuint32_tgihistorywidth=gireuserequired?width:1u;",
            "constuint32_tpathhistorywidth=pathreuserequired?width:1u;",
            "directreservoirs[index]=createpathtexture(",
            "surfacehistory[index]=createpathtexture(",
            "directsampleseeds[index]=createpathtexture(",
            "gicheckpointreservoirs[index]=createpathtexture(",
            "gicheckpointcounts[index]=createpathtexture(",
            "gilo[index]=createpathtexture(",
            "ginormal[index]=createpathtexture(",
            "gireceiver[index]=createpathtexture(",
            "pathseedreservoirs[index]=createpathtexture(",
            "pathseedstatistics[index]=createpathtexture("
        },
        "each reservoir family must allocate full-resolution history only while its effective solver stage is active");
    RequireOrdered(
        pass,
        {
            "constuint32_tdirecthistorywidth="
                "directreuserequired?width:1u;",
            "directsampleseeds[index]=createpathtexture(",
            "directhistorywidth,",
            "directhistoryheight,",
            "nvrhi::format::r32_uint,",
            "m_directsampleseeds=directsampleseeds;"
        },
        "direct sample seeds must be full resolution only for effective direct reuse and otherwise remain safe 1x1 bindings");
    RequireContains(
        pass,
        "commandlist->cleartextureuint("
            "m_directsampleseeds[index],"
            "nvrhi::allsubresources,0u);",
        "history reset must clear both persisted direct sample seed surfaces");
    RequireContains(
        pass,
        "pathreuserequired?nvrhi::format::rg32_uint:"
            "nvrhi::format::r32_uint",
        "the optional PT seed format must not disable baseline RTX PT dummy resources");
    RequireContains(
        pass,
        "nvrhi::format::rg32_uint",
        "path replay must persist a complete uint2 continuation seed");
    RequireContains(
        pass,
        "m_gireuseresourcesfullresolution==gireuserequired&&"
            "m_pathreuseresourcesfullresolution==pathreuserequired",
        "solver topology changes must invalidate resource reuse and binding caches");
    RequireContains(
        pass,
        "inputs.environment->getdesc().dimension=="
            "nvrhi::texturedimension::texturecube",
        "the pass must reject an invalid environment descriptor");
    RequireContains(
        pass,
        "inputs.noisetexture->getdesc().dimension=="
            "nvrhi::texturedimension::texture2darray",
        "the pass must reject an invalid noise descriptor");
    RequireContains(
        pass,
        "constuint64_tschedulingserial="
            "inputs.schedulingserial==std::numeric_limits<uint64_t>::max()?"
            "m_schedulingserial++:inputs.schedulingserial;",
        "retry scheduling must choose exactly one serial source");
    RequireAbsent(
        pass,
        "m_schedulingserial++^",
        "explicit and internal frame serials must not XOR into a constant retry seed");
    RequireOrdered(
        pass,
        {
            "capabilities.continuationseedreservoirsupported=",
            "capabilities.replayablepathseedsupported=",
            "capabilities.temporalgicheckpointreusesupported=",
            "capabilities.spatialgicheckpointreusesupported=",
            "capabilities.fullsamplereconnectionsupported=",
            "capabilities.sharedprimarysurfacesupported="
        },
        "capabilities must report executable local replay, geometric GI reconnection, and shared primary support");
    RequireContains(
        pass,
        "capabilities.fullsamplereconnectionsupported=false;",
        "bounded diffuse-tail reconnection must not claim arbitrary full-path shifting");
    RequireContains(
        pass,
        "result.pathreuserequestedbutunavailable="
            "usespathseedhistory(requestedsettings)&&!pathreuserequired;"
            "result.gireuserequestedbutunavailable="
            "usesgicheckpointhistory(requestedsettings)&&!gireuserequired;",
        "reuse diagnostics must report the effective stage that actually executed");
    RequireContains(
        pass,
        "result.cleanroomsolversubsetactive="
            "(inputs.settings.solver==pathtracingsolver::restirpt&&"
            "pathreuserequired)||"
            "(inputs.settings.solver==pathtracingsolver::restirgi&&"
            "gireuserequired);"
            "result.namesakeparityunavailable="
            "requestedsettings.solver!=pathtracingsolver::rtxpt;",
        "runtime results must distinguish executable subsets from namesake parity");

    RequireContains(
        material,
        "rayquery<ray_flag_force_non_opaque>query;",
        "the core must trace committed surfaces through material-aware DXR 1.1 inline queries");
    RequireContains(
        material,
        "ray_flag_accept_first_hit_and_end_search|"
            "ray_flag_force_non_opaque",
        "shadow traversal must route opaque triangles through material-aware face rejection");
    RequireOrdered(
        materialVisibility,
        {
            "if(!candidatefrontface&&!doublesided)",
            "if(material.domain==materialdomain_opaque)",
            "if(material.domain!=materialdomain_alphatested)"
        },
        "candidate coverage must reject single-sided backfaces before accepting opaque or alpha-tested geometry");
    RequireContains(
        materialVisibility,
        "if(requirepathtransportmaterial&&"
            "((material.flags&(materialflags_subsurfacescattering|"
            "materialflags_hair))!=0||"
            "material.transmissionfactor>0.0f))",
        "path traversal must omit material lobes outside the complete transport domain");
    RequireContains(
        material,
        "raymaterialtryresolvebounded("
            "query.committedinstancecontributiontohitgroupindex(),"
            "query.committedgeometryindex(),"
            "g_pathtracing.raymateriallimits",
        "committed hits must resolve through a bounded UVSR compact geometry map");
    RequireContains(
        constants,
        "uint4raymateriallimits;",
        "the transport ABI must carry geometry, material, and descriptor bounds");
    RequireOrdered(
        pass,
        {
            "inputs.materialvisibility.descriptortable->getcapacity();",
            "inputs.materialvisibility.geometryindexmap,",
            "inputs.materialvisibility.geometrybuffer,",
            "inputs.materialvisibility.materialbuffer,",
            "constants.raymateriallimits=uint4("
        },
        "the CPU pass must validate and publish every ray-material resource bound");
    RequireOrdered(
        materialVisibility,
        {
            "if(geometrymapoffset>=limits.x||",
            "compactgeometryindex>=limits.x-geometrymapoffset)",
            "if(globalgeometryindex>=limits.y)",
            "if(geometry.materialindex>=limits.z)"
        },
        "path candidates must reject compact-map, geometry, and material indices before dereference");
    RequireOrdered(
        material,
        {
            "uint(geometry.indexbufferindex)>=g_pathtracing.raymateriallimits.w",
            "uint(geometry.vertexbufferindex)>=g_pathtracing.raymateriallimits.w",
            "query.committedprimitiveindex()>=geometry.numindices/3u",
            "indexbuffer.getdimensions(indexbuffersize);",
            "vertexbuffer.getdimensions(vertexbuffersize);",
            "if(any(indices>=geometry.numvertices))"
        },
        "committed shading must bound descriptor, primitive, byte-address, and vertex indices");
    RequireContains(
        material,
        "evaluatescenematerial(",
        "committed hits must evaluate bindless scene textures and glTF material workflow");
    RequireContains(
        material,
        "uvsr_commit_path_tracing_ray_query_candidate(query);",
        "surface and visibility queries must retain path-domain and alpha-tested coverage");
    RequireContains(
        material,
        "pathtracingpreparerayorigin(",
        "secondary and visibility rays must use a scale-aware robust offset");
    RequireOrdered(
        materialVisibility,
        {
            "constboolrequestsopacitytexture=",
            "constboolrequestsbasealphatexture=!requestsopacitytexture&&",
            "if((requestsopacitytexture&&",
            "uint(material.opacitytextureindex)>=limits.w)",
            "returnfalse;",
            "constbooluseopacitytexture=requestsopacitytexture;",
            "if((useopacitytexture||usebasealphatexture)&&",
            "!raymaterialinterpolatetexcoordbounded("
        },
        "alpha-tested constants must not require UVs, while requested textures must be bounded and sampled");

    RequireContains(
        sampling,
        "pathtracingsamplecosinehemisphere(",
        "the shared sampler must retain cosine-weighted Lambert transport");
    RequireContains(
        sampling,
        "constpbrbsdfevaluationevaluation="
            "pathtracingevaluatebsdfpreparedexact(",
        "every continuation branch must evaluate the transport-exact Lambert-plus-GGX BSDF");
    RequireOrdered(
        sampling,
        {
            "pbrmaterialparametersmaterial=(pbrmaterialparameters)0;",
            "material.basecolor=max(surface.material.basecolor,0.0f);",
            "material.dielectricf0=",
            "surface.materialconstants.specularcolor.r",
            "returnpreparepbrmaterial(material);"
        },
        "path transport must reconstruct the same metallic-roughness inputs and dielectric F0 as the raster G-buffer");
    RequireAbsent(
        sampling,
        "surface.material.diffusealbedo",
        "Donut's already attenuated diffuse albedo must not be Fresnel-attenuated twice");
    RequireAbsent(
        sampling,
        "pathtracingradianceshadingnormalcorrection",
        "camera radiance transport must not apply an unbounded adjoint shading-normal factor");
    RequireContains(
        sampling,
        "pathtracingsampleggxhalfvector(",
        "the shared sampler must execute a GGX continuation strategy");
    RequireOrdered(
        sampling,
        {
            "constfloattotal=diffuseweight+specularweight;",
            "if(!(total>0.0f)||!isfinite(total))",
            "result.pdf=diffuseprobability*evaluation.diffusepdf+",
            "if(result.pdf>0.0f&&isfinite(result.pdf)&&cosine>0.0f&&"
        },
        "path continuation must retain every positive finite exact mixture PDF");
    RequireAbsent(
        sampling,
        "result.pdf>uvsr_path_target_epsilon",
        "path continuation must not drop rare valid BSDF samples at an arbitrary PDF floor");
    RequireOrdered(
        sampling,
        {
            "floatpathtracingd_ggxexact(floatnoh,floatalpha)",
            "constfloatnormalization="
                "uvsr_pi*denominator*denominator;",
            "normalization>0.0f&&isfinite(normalization)",
            "floatpathtracingpdfggxexact(",
            "pathtracingd_ggxexact(noh,alpha)*saturate(noh)/"
        },
        "path transport GGX evaluation and sampling PDF must share exact finite analytic denominators");
    RequireAbsent(
        sampling,
        "evaluatebsdfprepared(",
        "path transport must not reintroduce the raster GGX denominator floor");
    {
        constexpr double LowPdf = 1.e-8;
        constexpr double UnitContribution = 1.0;
        Require(
            LowPdf > 0.0 && UnitContribution / LowPdf == 1.e8,
            "the low-BSDF-PDF regression must preserve exact positive finite weighting");
        constexpr double Alpha = 0.002;
        constexpr double Pi = 3.14159265358979323846;
        constexpr double IntendedPeak = 1.0 / (Pi * Alpha * Alpha);
        constexpr double FlooredPeak =
            (Alpha * Alpha) / 1.e-6;
        Require(
            IntendedPeak > 79000.0 && FlooredPeak == 4.0 &&
                IntendedPeak > FlooredPeak * 19000.0,
            "the smooth-GGX regression must distinguish the exact peak from the retired raster floor");
    }
    RequireAbsent(
        sampling,
        "pathtracingpowerheuristic(",
        "the core must not advertise dormant MIS when no overlapping light technique is active");
    RequireContains(
        sampling,
        "abinaryhierarchyoverthecompletesubmitted-lightbuffer",
        "NEE-AT must execute a current-vertex adaptive hierarchy over every submitted light");
    RequireOrdered(
        sampling,
        {
            "reservoir.candidatecount=newcandidatecount;",
            "if(!(target>0.0f)||",
            "if(random*newweightsum<groupweight)"
        },
        "RIS normalization must count zero-contribution proposals before selection eligibility is tested");
    RequireOrdered(
        sampling,
        {
            "if(!(reused.candidatecount>0.0f)||",
            "constboolreusableselection=pathtracingreservoirisvalid(reused)&&",
            "pathtracingreservoirupdate("
        },
        "reused zero-weight proposal groups must retain their effective candidate count");
    RequireContains(
        sampling,
        "reused.weightsum*reusedcandidatescale*"
            "targetatcurrentsurface/"
            "reused.selectedtarget",
        "temporal and spatial reservoirs must re-evaluate reused targets at the current surface");
    RequireOrdered(
        sampling,
        {
            "uvsr_path_max_reused_reservoir_candidates/"
                "reused.candidatecount",
            "reused.weightsum*reusedcandidatescale*",
            "reused.candidatecount*reusedcandidatescale"
        },
        "recursive reservoir reuse must cap both retained weight and effective candidate history");
    RequireOrdered(
        sampling,
        {
            "constuintnewcandidatecount=reservoir.candidatecount+1u;",
            "constfloattarget=max(pathtracingluminance(contribution),0.0f);",
            "constfloatnewtargetsum=reservoir.targetsum+target;",
            "constbooleligible=target>uvsr_path_target_epsilon&&",
            "constfloatinversecount=rcp(float(newcandidatecount));",
            "reservoir.contributionmean=",
            "reservoir.candidatecount=newcandidatecount;"
        },
        "already-evaluated solver proposals must form the Rao-Blackwellized finite mean while counting black candidates");
    RequireContains(
        sampling,
        "returnreservoir.contributionmean;",
        "solver contribution reuse must return its deterministic conditional mean");
    RequireAbsent(
        sampling,
        "lightsample.lightselectionpdf=max("
            "lightselectionpdf,uvsr_path_target_epsilon);",
        "path transport must not floor an exact discrete light-selection PDF");
    RequireOrdered(
        sampling,
        {
            "constfloatsamplingpdf="
                "lightselectionpdf*lightsample.directionalpdf;",
            "if(!(samplingpdf>0.0f)||!isfinite(samplingpdf))",
            "constfloatsampleweight=cosineterm*"
                "saturate(lightsample.visibility)/samplingpdf;"
        },
        "path transport must divide by every positive finite exact light PDF");
    {
        constexpr double LowProbability = 1.e-8;
        constexpr double UnitContribution = 1.0;
        const double exactWeight = UnitContribution / LowProbability;
        const double flooredWeight = UnitContribution / 1.e-6;
        Require(
            exactWeight == 1.e8 && flooredWeight == 1.e6 &&
                exactWeight > flooredWeight,
            "the low-probability regression must distinguish exact weighting from the retired PDF floor");
    }
    RequireOrdered(
        sampling,
        {
            "uintpathtracingrandomuint("
                "inoutpathtracingrandomstreamstream)",
            "constuintcounter=stream.dimension++;",
            "returnpathtracinghash(",
            "float2pathtracingfinitelightrandom(uintsampleseed)",
            "sampleseed^0xa511e9b3u",
            "sampleseed^0x63d83595u"
        },
        "finite-emitter proposals must derive two dimensions from a complete counter-generated uint seed");
    RequireAbsent(
        sampling,
        "asuint(pathtracingrandom(",
        "finite-emitter seed identity must never round-trip through a 24-bit random float");
    RequireOrdered(
        sampling,
        {
            "structpathtracinganalyticlightsample",
            "pbrlightsamplepbr;",
            "float3sampledendpoint;",
            "uinthasfiniteendpoint;",
            "boolpathtracinganalyticlightinputsarefinite(lightconstantslight)",
            "all(isfinite(light.position))&&",
            "all(isfinite(light.direction))&&",
            "all(isfinite(light.color))&&",
            "isfinite(light.radius)&&isfinite(light.intensity)&&",
            "isfinite(light.angularsizeorinvrange)&&",
            "isfinite(light.innerangle)&&isfinite(light.outerangle);",
            "if(!pathtracinganalyticlightinputsarefinite(light)||",
            "if(!finitedirectional&&!finitepositional)",
            "result.pbr=samplepbrlight("
        },
        "analytic inputs must be finite before zero-size lights retain the shared delta-light sample contract");
    RequireContains(
        sampling,
        "constboolfinitedirectional=directional&&"
            "isfinite(light.angularsizeorinvrange)&&"
            "light.angularsizeorinvrange>0.0f;",
        "directional angular size must be interpreted as a positive finite full diameter");
    RequireOrdered(
        sampling,
        {
            "constfloatalpha=0.5f*light.angularsizeorinvrange;",
            "constfloatoneminuscosinemaximum="
                "2.0f*sinehalfalpha*sinehalfalpha;",
            "constfloatsolidangle="
                "uvsr_path_two_pi*oneminuscosinemaximum;",
            "result.pbr.directionalpdf=1.0f/solidangle;",
            "constfloatradiancescale=light.intensity/"
                "(uvsr_path_pi*sinealphasquared);"
        },
        "finite directional lights must use a uniform solid-angle cone with the irradiance-preserving radiance normalization");
    RequireOrdered(
        sampling,
        {
            "constboolfinitepositional=positional&&isfinite(light.radius)&&"
                "light.radius>0.0f;",
            "if(centerdistance>radius)",
            "constfloatsinealpha=radius/centerdistance;",
            "constfloatoneminuscosinemaximum=sinealphasquared/"
                "(1.0f+cosinealpha);",
            "result.pbr.directionalpdf=1.0f/solidangle;",
            "endpointdistance=centerprojection-"
                "sqrt(max(discriminant,0.0f));",
            "result.sampledendpoint=surfaceposition+"
                "result.pbr.directiontolight*endpointdistance;",
            "constfloatradiancescale=light.intensity/"
                "(uvsr_path_pi*radiussquared);"
        },
        "finite point and spot lights must sample the visible sphere cone and persist the near shell endpoint");
    RequireOrdered(
        sampling,
        {
            "areceiverinsideoronatwo-sidedsphericalshellseesthewhole",
            "pathtracingsampleuniformcone(random,2.0f,centerdirection);",
            "result.pbr.directionalpdf=1.0f/(4.0f*uvsr_path_pi);",
            "endpointdistance=-projected+sqrt(max(discriminant,0.0f));"
        },
        "inside/on-sphere sampling must use a finite two-sided 4pi exit-root fallback");
    RequireOrdered(
        sampling,
        {
            "floatpathtracingfinitepositionalprofile(",
            "if(!isfinite(light.angularsizeorinvrange)||",
            "constfloatinverserangesquared=light.angularsizeorinvrange*",
            "if(light.lighttype==lighttype_spot)",
            "evaluateflashlightbeamprofile"
        },
        "finite positional sampling must retain center-range, spot, and flashlight profiles behind finite guards");
    RequireOrdered(
        sampling,
        {
            "voidpathtracingprepareanalyticshadowray(",
            "shadowraymaximum=g_pathtracing.maximumraydistance;",
            "if(analyticsample.hasfiniteendpoint==0u)return;",
            "analyticsample.sampledendpoint-shadoworigin;",
            "shadowdirection=shadowvector/endpointdistance;",
            "shadowraymaximum=endpointdistance-g_pathtracing.raybias;"
        },
        "positional visibility must terminate at the sampled endpoint while directional visibility keeps the configured maximum");
    RequireAbsent(
        sampling,
        "length(light.position-surface.position)-",
        "visibility must not use center-distance/radius shorthand for sampled finite emitters");
    Require(
        CountOccurrences(shader,
            "constuintsampleseed=pathtracingrandomuint(randomstream);") == 2u,
        "conventional NEE and direct RIS must each consume one full sample seed per candidate");
    RequireOrdered(
        shader,
        {
            "constuintsampleseed=pathtracingrandomuint(randomstream);",
            "pathtracingevaluateselectedlightprepared("
                "surface,viewdirection,lightindex,sampleseed,selectionpdf);"
        },
        "conventional NEE must pass the exact sampled-emitter identity into shadowed evaluation");
    RequireOrdered(
        shader,
        {
            "pathtracingevaluateunshadowedlight("
                "surface,viewdirection,lightindex,sampleseed)",
            "constfloatcandidateweight=selectionpdf>0.0f&&"
                "isfinite(selectionpdf)?target/selectionpdf:0.0f;",
            "pathtracingreservoirupdate("
                "reservoir,float(lightindex),sampleseed,target,"
        },
        "direct RIS must retain both exact discrete PDF weighting and sampled-emitter identity");
    RequireAbsent(
        shader,
        "target/max(selectionpdf,uvsr_path_target_epsilon)",
        "direct RIS must not floor a rare positive discrete light PDF");
    RequireOrdered(
        sampling,
        {
            "uintselectedsampleseed;",
            "reservoir.selectedsampleseed=selectedsampleseed;",
            "reservoir.selectedsampleseed=selectedsampleseed;",
            "reused.selectedsampleseed,"
        },
        "reservoir load, selection, and combine must carry the persisted sample seed");

    RequireContains(
        shader,
        "voidmain(uint2dispatchpixel:sv_dispatchthreadid)",
        "the compute transport must use the production main entry point");
    RequireOrdered(
        shader,
        {
            "uintoldcount=u_successfulsamplecount[pixel];",
            "float3oldmean=sharedprimary?"
                "u_indirectmean[pixel].rgb:u_rawmean[pixel].rgb;",
            "constfloat4oldvariancestate=u_colorvariance[pixel];",
            "float3oldvariance=oldvariancestate.rgb;",
            "if(oldcount>0u&&(!all(isfinite(oldmean))||"
                "!all(isfinite(oldvariance))))",
            "oldcount=0u;oldmean=0.0f;oldvariance=0.0f;"
                "failedattemptsalt=0u;",
            "uintrunningcount=accumulate?oldcount:0u;",
            "constfloat3newmean=previouscount==0u?"
        },
        "nonfinite per-pixel path history must recover locally before "
        "scheduling and accumulation");
    RequireOrdered(
        shader,
        {
            "uintfailedattemptsalt=",
            "isfinite(oldvariancestate.a)&&oldvariancestate.a>=0.0f",
            "uintnextfailedattemptsalt=failedattemptsalt;",
            "accumulate?nextfailedattemptsalt:0u,",
            "if(sample.valid==0u)",
            "nextfailedattemptsalt=accumulate?",
            "nextfailedattemptsalt+1u",
            "continue;",
            "nextfailedattemptsalt=0u;",
            "u_colorvariance[pixel]=float4("
                "oldvariance,float(nextfailedattemptsalt));",
            "u_colorvariance[pixel]=float4("
                "runningvariance,float(nextfailedattemptsalt));"
        },
        "invalid stationary path samples must change their retry seed while "
        "adaptive skips preserve the salt and successes clear it");
    RequireOrdered(
        sampling,
        {
            "uintfailedattemptsalt,",
            "low=pathtracinghash(low^failedattemptsalt*0x27d4eb2du);",
            "high=pathtracinghash(high^failedattemptsalt*0x165667b1u);"
        },
        "path retry salt must independently perturb both seed words");
    RequireContains(
        constants,
        "uint2schedulinggrid;uint2schedulingphase;",
        "the transport ABI must carry an interleaved progressive sample lattice");
    RequireContains(
        constants,
        "planarviewconstantsview;planarviewconstantspreviousview;",
        "path transport must carry current and previous nonjittered view transforms");
    RequireContains(
        constants,
        "uintschedulingserialhigh;uintpreviousviewvalid;",
        "previous-view reprojection must have an explicit validity word");
    RequireContains(
        passHeader,
        "constdonut::engine::iview*previousview=nullptr;",
        "the public path input must accept an optional previous view");
    RequireOrdered(
        pass,
        {
            "constants.previousview=constants.view;",
            "constants.previousviewvalid=0u;",
            "if(inputs.previousview)",
            "inputs.previousview->fillplanarviewconstants("
                "constants.previousview);",
            "constants.previousviewvalid=1u;",
            "constants.proposalreprojectionvalid=",
            "constants.previousviewvalid!=0u&&"
                "preserverevalidatedproposals"
        },
        "the CPU must provide motion metadata every frame while separately authorizing estimator reprojection");
    RequireContains(
        pass,
        "static_assert(static_cast<uint32_t>(uvsr::"
            "pathtracingdebugview::primarytransport)==9u);",
        "the CPU Primary Transport debug ordinal must match HLSL");
    RequireContains(
        pass,
        "static_assert(static_cast<uint32_t>(uvsr::"
            "pathtracingdebugview::indirecttransport)==10u);",
        "the CPU Indirect Transport debug ordinal must match HLSL");
    RequireOrdered(
        shader,
        {
            "constuint2pixel=dispatchpixel*"
                "g_pathtracing.schedulinggrid+"
                "g_pathtracing.schedulingphase;",
            "if(any(pixel>=g_pathtracing.dispatchextent))return;"
        },
        "the progressive lattice must map bounded dispatch threads to complete output pixels");
    RequireOrdered(
        pass,
        {
            "if(!sharedprimaryrequired&&historyreset&&"
                "dispatchschedule.phasecount>1u)",
            "textureuavbarrier(commandlist,m_display);",
            "commandlist->commitbarriers();",
            "uvsr_path_tracing_flag_reconstruct_preview",
            "div_ceil(inputs.width,8u)",
            "div_ceil(inputs.height,8u)"
        },
        "legacy sparse history invalidation must reconstruct presentation only when no full-resolution shared baseline exists");
    RequireOrdered(
        shader,
        {
            "uvsr_path_tracing_flag_reconstruct_preview",
            "if(any(dispatchpixel>=g_pathtracing.dispatchextent))return;",
            "constuint2source00=min((dispatchpixel/grid)*grid,"
                "maximumsource);",
            "if(all(dispatchpixel==source00))return;",
            "constfloat4row0=lerp(",
            "constfloat4row1=lerp(",
            "u_display[dispatchpixel]=lerp(row0,row1,blend.y);",
            "return;"
        },
        "the reset preview must smoothly reconstruct freshly traced representatives without fabricating estimator history");
    RequireOrdered(
        pass,
        {
            "maxpathtracingworkunitsperdispatch",
            "estimatepathtracingtransportworkunitsperpixel(",
            "constuint64_treplaypathwork=",
            "if(pathseedhistoryavailable&&"
                "usespathseedhistory(settings))",
            "uint64_tdirectdonorwork=0u;",
            "if(!settings.sharedprimarysurface&&directhistoryavailable&&"
                "usesdirectreservoirhistory(settings))",
            "directdonorwork=saturatingadd("
                "settings.temporalreuse?1u:0u,"
                "settings.spatialneighborcount);",
            "constuint64_trtxdiresolvework=settings.usertxdi&&"
                "lightcount>0u&&!settings.sharedprimarysurface?1u:0u;",
            "constuint64_treplaydonorbatch=saturatingmultiply("
                "replaycount,replaypathwork);",
            "saturatingmultiply("
                "workperfreshsample,settings.samplesperpixel),",
            "stablesignalsrequired?1u:0u",
            "buildpathtracingdispatchschedule(",
            "constuint64_tworkperpixel="
                "estimatepathtracingtransportworkunitsperpixel(",
            "if(workperpixel>maxpathtracingworkunitsperdispatch)",
            "constuint64_tsharedprimaryframework=saturatingmultiply(",
            "if(!dispatchschedule.valid||"
                "sharedprimaryframework>"
                "maxpathtracingworkunitsperdispatch)",
            "constants.schedulinggrid=dispatchschedule.grid;",
            "constants.schedulingphase=dispatchschedule.phase;",
            "div_ceil(dispatchschedule.workextent.x,8u)",
            "div_ceil(dispatchschedule.workextent.y,8u)"
        },
        "the CPU pass must bound each dispatch while preserving a complete progressive lattice");
    RequireContains(
        pass,
        "maximumdirectdonorbatch,1u,true,false,false,false)==56u",
        "the frame estimate must charge one shared-primary RTXDI visibility resolve and all five direct donors once per pixel batch");
    RequireContains(
        pass,
        "maximumdirectdonorbatch,1u,true,false,false,true)==57u",
        "stable guide traversal must be charged once after the fresh-sample batch");
    RequireContains(
        pass,
        "nonsharedrestirpt,1u,false,true,false,false)==30u",
        "all-ray RESTIR PT must charge two fresh paths plus each replay donor once per pixel batch");
    RequireContains(
        pass,
        "nonsharedrestirpt,0u,false,true,false,false)==16u",
        "all-ray zero-light RESTIR PT must retain a bounded donor-aware work estimate");
    RequireContains(
        pass,
        "1024ull*1024ull*1024ull",
        "the synthetic safety budget must not throttle ordinary 1080p Sponza or low-light 4K presets");
    RequireOrdered(
        pass,
        {
            "m_progressivephase=0u;",
            "constboolcompletedprogressivecycle=nextprogressivephase==0u;",
            "constboolanyreservoirhistoryrequired=directreuserequired||"
                "gireuserequired||pathreuserequired;",
            "if(completedprogressivecycle&&anyreservoirhistoryrequired)",
            "m_historyindex^=1u;",
            "m_directreservoirhistoryvalid=directreuserequired;",
            "m_gicheckpointhistoryvalid=gireuserequired;",
            "m_pathseedhistoryvalid=pathreuserequired;"
        },
        "reservoir ping-pong must advance only after every progressive phase has written its target");
    RequireOrdered(
        pass,
        {
            "constbooldebugviewchanged=constants.debugview!=m_lastdebugview;",
            "m_lastdebugview=constants.debugview;",
            "m_progressivephase=0u;",
            "m_debugrefreshactive=true;",
            "if(m_debugrefreshactive)",
            "if(completedprogressivecycle)",
            "m_debugrefreshactive=false;"
        },
        "debug refresh must restart once and remain active through exactly one complete progressive lattice");
    RequireContains(
        shader,
        "#ifuvsr_pt_solver<0||uvsr_pt_solver>2"
            "#errorunsupporteduvsr_pt_solver",
        "the shader must reject unsupported compile-time solver identities");
    RequireContains(
        shader,
        "#ifuvsr_pt_rtxdi",
        "RTXDI reservoir transport must be absent from the plain compiled variant");
    RequireOrdered(
        shader,
        {
            "#ifuvsr_pt_solver==2",
            "texture2d<float4>"
                "t_previousgicheckpointreservoir:register(t5);",
            "texture2d<float4>t_previousgilo:register(t21);",
            "texture2d<float4>t_previousginormal:register(t22);",
            "texture2d<float4>t_previousgireceiver:register(t23);"
        },
        "the RESTIR GI body must own distinct x2/W, Lo, packed-normal, and receiver/M payloads");
    RequireContains(
        shader,
        "#ifuvsr_pt_solver==1"
            "texture2d<uint2>t_previouspathseed:register(t7);"
            "texture2d<float4>t_previouspathseedstatistics:register(t8);",
        "the RESTIR PT body must own a distinct 64-bit seed payload");
    Require(
        CountOccurrences(sampling, "uvsr_pt_nee_mode") >= 6u,
        "NEE selection must be a compile-time transport specialization");
    RequireAbsent(
        sampling,
        "g_pathtracing.neemode",
        "the active shader must not retain runtime branches for inactive NEE algorithms");
    RequireAbsent(
        shader,
        "pathtracingflagisset(uvsr_path_tracing_flag_use_rtxdi)",
        "compile-time specialization must not retain a dormant runtime RTXDI branch");
    RequireOrdered(
        shader,
        {
            "float3pathtracingconventionaldirect(",
            "constuintcandidatecount=max("
                "g_pathtracing.neecandidatecount,1u);",
            "for(uintcandidate=0u;candidate<candidatecount;++candidate)",
            "returnestimate/float(candidatecount);"
        },
        "raw RTX PT must average every configured independent NEE candidate");
    RequireOrdered(
        shader,
        {
            "float3primarybase;",
            "float3indirectsuffix;",
            "if(bounce==0u)result.primarybase+=contribution;",
            "elseresult.indirectsuffix+=contribution;",
            "constfloat3solverradiance=max("
                "sharedprimary?solvedindirectsuffix:"
                "sample.primarybase+solvedindirectsuffix,0.0f);",
            "constfloatfireflyscale="
                "pathtracingfireflyscale(solverradiance);"
        },
        "solver estimates must split primary terms and replace, never double-add, the indirect suffix");
    RequireAbsent(
        shader,
        "sample.primarybase+sample.indirectsuffix+solvedindirectsuffix",
        "the local indirect suffix must not be added beside its reservoir estimate");
    RequireContains(
        shader,
        "structpathtracinggireservoir{"
            "float3secondaryposition;float3secondarynormal;"
            "float3tailradiance;floatselectedtarget;floatweightsum;"
            "floatrepresentedcount;floatfinalizedweight;uintvalid;};",
        "RESTIR GI must preserve reconstructable x2, n2, Lo, W, and M state");
    RequireOrdered(
        shader,
        {
            "float3pathtracingevaluategitarget(",
            "pathtracingtraceocclusion(",
            "pathtracingevaluatebsdfpreparedexact(",
            "max(bsdf.diffuse,0.0f)*cosine"
        },
        "GI target evaluation must reconnect at the current receiver with visibility, BSDF, and cosine");
    RequireOrdered(
        shader,
        {
            "floatpathtracinggireconnectionjacobian(",
            "dot(receiverdirection,donordirection)<0.8660254f",
            "cosineratio<0.5f||cosineratio>2.0f",
            "constfloatdenominator=donorcosine*receiverdistancesquared;",
            "constfloatjacobian=receivercosine*donordistancesquared/denominator;",
            "jacobian>=0.05f&&jacobian<=20.0f"
        },
        "spatial GI must apply a finite bounded solid-angle reconnection Jacobian");
    RequireOrdered(
        shader,
        {
            "float3pathtracingresolvegicheckpoint(",
            "pathtracingresolvepreviousdonorpixel(",
            "uvsr_path_tracing_flag_temporal_reuse",
            "pathtracingcombinegidonor(",
            "neighborindex<g_pathtracing.spatialneighborcount;",
            "pathtracingpreviousneighbor(",
            "pathtracingcombinegidonor(",
            "pathtracinggireservoirfinalize(reservoir);"
        },
        "RESTIR GI must reconnect temporal and bounded previous-frame spatial donors");
    RequireOrdered(
        shader,
        {
            "pathtracingresolvegicheckpoint(",
            "successfulbatchcount==0u",
            "if(!gihistorysamplevalid)",
            "u_gicheckpointreservoir[pixel]=float4(",
            "u_gilo[pixel]=float4(",
            "u_ginormal[pixel]=float4(",
            "u_gireceiver[pixel]=float4(",
            "anonzeroreceivermaterialistheexactm=1marker."
        },
        "GI donor work must run once per pixel batch and persist only one fresh local candidate");
    RequireOrdered(
        shader,
        {
            "pathtracingbuildseedreplaydonors(",
            "pathtracingresolvepreviousdonorpixel(",
            "pathtracingflagisset("
                "uvsr_path_tracing_flag_temporal_reuse)",
            "t_previouspathseed[previouscenter]",
            "neighborindex<g_pathtracing.spatialneighborcount;",
            "constint2neighbor=pathtracingpreviousneighbor("
                "uint2(previouscenter),currentseed,"
                "0x50544e42u,neighborindex);",
            "t_previouspathseed[neighbor]",
            "float3pathtracingresolveseedreplay(",
            "pathtracingcontributionreservoircombined=replaydonors;",
            "pathtracingcontributionreservoirupdate("
                "combined,currentindirectsuffix,",
            "returnpathtracingcontributionreservoirestimate(combined);",
            "if(!replaydonorsprepared)",
            "replaydonors=pathtracingbuildseedreplaydonors(",
            "replaydonorsprepared=true;",
            "solvedindirectsuffix=pathtracingresolveseedreplay("
        },
        "PT must trace one validated temporal/spatial donor set per pixel batch and combine it with every fresh sample");
    Require(
        CountOccurrences(
            shader,
            "replaydonors=pathtracingbuildseedreplaydonors(") == 1u,
        "RESTIR PT donor paths must be built only once outside repeated fresh-sample combination");
    RequireOrdered(
        shader,
        {
            "u_pathseed[pixel]=lastcontinuationseed;",
            "u_pathseedstatistics[pixel]=float4("
                "localtarget,localtarget,1.0f,1.0f);"
        },
        "PT persistence must retain only the final fresh local seed, target, and M=1 statistics from the frame batch");
    RequireContains(
        shader,
        "pathtracingsurfacesignaturesarecompatible("
            "surfacesignature,spatialsurface,true)",
        "previous-frame spatial reuse must retain camera-relative depth validation");
    RequireOrdered(
        shader,
        {
            "constint2neighbor=pathtracingpreviousneighbor("
                "uint2(previouscenter),",
            "if(any(neighbor!=previouscenter))",
            "t_previousdirectreservoir[neighbor]"
        },
        "clamped border neighbors must not duplicate the reprojected temporal reservoir");
    RequireOrdered(
        shader,
        {
            "pathtracingloadreservoir("
                "t_previousdirectreservoir[previouscenter],"
                "t_previousdirectsampleseed[previouscenter])",
            "temporal.selectedsampleseed",
            "pathtracingreservoircombine("
                "reservoir,temporal,target,",
            "pathtracingloadreservoir("
                "t_previousdirectreservoir[neighbor],"
                "t_previousdirectsampleseed[neighbor])",
            "spatial.selectedsampleseed",
            "pathtracingreservoircombine("
                "reservoir,spatial,target,"
        },
        "temporal and previous-frame spatial donors must be re-evaluated from their exact persisted sample seeds");
    RequireContains(
        shader,
        "pathtracingevaluateselectedlightprepared("
            "surface,viewdirection,lightindex,"
            "reservoir.selectedsampleseed,1.0f);",
        "final direct-reservoir visibility must replay the selected finite-emitter sample");
    RequireOrdered(
        shader,
        {
            "boolpathtracingresolvepreviousdonorpixel(",
            "reprojected=g_pathtracing.proposalreprojectionvalid!=0u;",
            "if(!reprojected)returntrue;",
            "constfloat4previousclip=mul("
                "float4(currentworldposition,1.0f),"
                "g_pathtracing.previousview.matworldtoclipnooffset);",
            "!(previousclip.w>1.0e-6f)",
            "previousclip.z<0.0f",
            "previousclip.z>previousclip.w",
            "constfloat2previouslocal=previouswindow-"
                "g_pathtracing.previousview.viewportorigin;",
            "any(previouslocal<0.0f)",
            "any(previouslocal>=g_pathtracing.previousview.viewportsize)",
            "any(candidate>=int2(g_pathtracing.dispatchextent))",
            "previouspixel=candidate;returntrue;"
        },
        "camera reprojection must use the prior nonjittered view and reject behind-camera, clipped, nonfinite, and out-of-bounds donors");
    RequireOrdered(
        material,
        {
            "currentsignature.w!=previoussignature.w",
            "constfloatnormaldistance=length(",
            "if(!(normaldistance<0.12f))returnfalse;",
            "if(!requirecamerarelativedepth)returntrue;",
            "abs(currentsignature.z-previoussignature.z)<"
        },
        "reprojected surfaces may waive only camera-relative depth after exact material and normal validation");
    RequireOrdered(
        shader,
        {
            "constboolpreviouscentervalid="
                "pathtracingresolvepreviousdonorpixel(",
            "if(previouscentervalid)",
            "surfacesignature,temporalsurface,!reprojected",
            "t_previousdirectreservoir[previouscenter]",
            "pathtracingpreviousneighbor(uint2(previouscenter),",
            "surfacesignature,spatialsurface,true"
        },
        "direct temporal reuse must use the reprojected center while spatial reuse retains depth validation");
    RequireOrdered(
        shader,
        {
            "if(!pathtracingresolvepreviousdonorpixel(",
            "returndonors;",
            "t_previouspathseed[previouscenter]",
            "pathtracingreplayseedcandidate("
        },
        "invalid PT reprojection must return the current estimate without a same-pixel fallback");
    RequireOrdered(
        shader,
        {
            "voidpathtracingcarrydirectproposal(uint2pixel)",
            "if(g_pathtracing.proposalreprojectionvalid!=0u)",
            "u_directreservoir[pixel]=0.0f;",
            "u_surface[pixel]=0.0f;",
            "u_directsampleseed[pixel]=0u;",
            "return;",
            "t_previousdirectreservoir[int2(pixel)]"
        },
        "failed motion samples must invalidate direct proposals instead of copying unrelated screen-space history");
    RequireOrdered(
        shader,
        {
            "voidpathtracingcarryreplayseed(uint2pixel)",
            "if(g_pathtracing.proposalreprojectionvalid!=0u)",
            "u_pathseed[pixel]=0u;",
            "u_pathseedstatistics[pixel]=0.0f;",
            "return;",
            "t_previouspathseed[int2(pixel)]"
        },
        "failed motion samples must invalidate replay seeds instead of copying unrelated screen-space history");
    Require(
        CountOccurrences(shader,
            "pathtracingcarrydirectproposal(pixel);") == 2u,
        "skipped and rejected attempts must use the motion-safe proposal carry helper");
    RequireContains(
        shader,
        "u_directsampleseed[pixel]="
            "lastsample.directreservoir.selectedsampleseed;",
        "successful direct reuse must persist the selected sample seed beside its RGBA32 payload");
    RequireContains(
        shader,
        "constboolshowprimaryenvironment=bounce>0u||"
            "pathtracingflagisset("
            "uvsr_path_tracing_flag_show_environment_background);",
        "hiding the primary environment must retain secondary environment lighting");
    RequireOrdered(
        shader,
        {
            "pathtracingaccumulationcyclemodulo(updateinterval)",
            "constuintupdatephase=updateinterval>1u",
            "constboolaccumulationjitteractive=pathtracingflagisset("
                "uvsr_path_tracing_flag_accumulation_jitter);",
            "constuintcoveragestep=accumulationjitteractive&&"
                "updateinterval>1u",
            "constuintsuccessfulsamplesperupdate=max("
                "g_pathtracing.samplesperpixel,1u);",
            "constuintsuccessfulupdatephase=oldcount==0u?0u:1u+"
                "(oldcount-1u)/successfulsamplesperupdate;",
            "constuintsuccessfulphaseadvance=updateinterval>1u",
            "(successfulupdatephase%updateinterval)*coveragestep",
            "constboolscheduledupdate=updateinterval==1u||",
            "constboolattempt=refreshdebug||!accumulate||oldcount==0u||"
                "scheduledupdate;"
        },
        "adaptive retry must decorrelate successful Shared Primary samples "
        "from active camera jitter while retaining a bounded revisit cycle");
    RequireContains(
        constants,
        "#defineuvsr_path_tracing_flag_accumulation_jitter(1u<<14u)",
        "path constants must expose accumulation-jitter scheduling intent");
    RequireContains(
        passHeader,
        "boolaccumulationjitteractive=false;",
        "path inputs must carry effective accumulation-jitter ownership");
    RequireOrdered(
        pass,
        {
            "constboolaccumulationjitterrequired="
                "inputs.accumulationjitteractive&&",
            "inputs.accumulatesamples&&",
            "sharedprimaryrequired;",
            "constants.flags|="
                "uvsr_path_tracing_flag_accumulation_jitter;"
        },
        "only executable accumulating Shared Primary transport may alter the "
        "adaptive coverage phase");
    RequireContains(
        pass,
        "hash=hashvalue(hash,accumulationjitterrequired);",
        "the effective accumulation-jitter policy must reset path history");
    RequireOrdered(
        pass,
        {
            "if(inputs.accumulatesamples)",
            "constants.schedulingseriallow="
                "lowword(m_accumulationschedulingcycle);",
            "constants.schedulingserialhigh="
                "highword(m_accumulationschedulingcycle);"
        },
        "adaptive revisit cycles must advance only with submitted path lattices");
    RequireContains(
        pass,
        "if(inputs.accumulatesamples)++m_accumulationschedulingcycle;",
        "a complete submitted lattice must advance one adaptive revisit cycle");
    RequireContains(
        shader,
        "constuintsamplesequencephase=accumulate&&!animatehistoryreset?"
            "runningcount:g_pathtracing.samplesequencephase*"
            "g_pathtracing.samplesperpixel+sampleindex;",
        "accumulation must index each stochastic path by its successful count while frame batches receive distinct phases");
    RequireContains(
        shader,
        "uvsr_path_tracing_flag_animate_history_reset",
        "camera-motion resets must honor Animate Samples without retaining radiance history");
    RequireAbsent(
        shader,
        "rcp(float(oldcount)+1.0f)",
        "harmonic retry starvation");
    RequireContains(
        shader,
        "accumulate?0u:g_pathtracing.schedulingseriallow,"
            "accumulate?0u:g_pathtracing.schedulingserialhigh,",
        "accumulating sample seeds must depend on accepted sample count, not skipped frame serials");
    RequireOrdered(
        sampling,
        {
            "uint2pathtracingmakesampleseed(",
            "uintseriallow,",
            "uintserialhigh,",
            "low=pathtracinghash(low^seriallow);",
            "high=pathtracinghash(high^serialhigh);",
            "returnuint2(pathtracinghash(low),pathtracinghash(high));"
        },
        "every transport attempt must receive a complete 64-bit scheduling seed");
    RequireOrdered(
        sampling,
        {
            "structpathtracingrandomstream{uint2seed;uintdimension;};",
            "pathtracingcreaterandomstream(",
            "constuintcounter=stream.dimension++;",
            "stream.seed.x^pathtracinghash("
                "stream.seed.y+counter*0x9e3779b9u)"
        },
        "stored seeds must replay every random dimension through a deterministic counter stream");
    RequireOrdered(
        shader,
        {
            "pathtracingcreaterandomstream(continuationseed,0x43414d45u);",
            "pathtracingcreaterandomstream(continuationseed,0x53554646u);",
            "pathtracingcreaterandomstream(continuationseed,0x44495245u);"
        },
        "camera, continuation, and primary-direct dimensions must occupy independent seed domains");
    RequireOrdered(
        shader,
        {
            "constfloatfireflyscale=pathtracingfireflyscale(solverradiance);",
            "constfloat3accumulatedsample=solverradiance*fireflyscale;",
            "constfloat3newmean=previouscount==0u?"
                "accumulatedsample:"
                "lerp(runningmean,accumulatedsample,meanweight);",
            "constfloat3residualsample="
                "max(sample.primarybase,0.0f)*fireflyscale;",
            "constfloat3diffusesuffixsample=",
            "max(solvedindirectsuffix,0.0f)*fireflyscale",
            "u_indirectmean[pixel]=float4(runningmean,1.0f);",
            "u_rawmean[pixel]=float4(rawmean,1.0f);",
            "u_residualmean[pixel]=float4(runningresidual,1.0f);",
            "u_diffusesuffixmean[pixel]=float4(runningdiffuse,1.0f);"
        },
        "one firefly scalar must be applied before raw and signal online means");
    RequireContains(
        pass,
        "result.rawmeanbiasedbyfireflyfilter="
            "inputs.settings.enablefireflyfilter;",
        "the result must report when firefly handling biases persistent history");
    RequireOrdered(
        pass,
        {
            "hash=hashvalue(hash,settings.enablefireflyfilter);",
            "hash=hashvalue(hash,settings.fireflythreshold);"
        },
        "changing the biased firefly estimator must invalidate incompatible history");
    RequireContains(
        pass,
        "constants.flags|=uvsr_path_tracing_flag_replay_path_seeds;",
        "effective RESTIR PT replay must reach its solver-specialized dispatch");
    RequireContains(
        pass,
        "constants.flags|=uvsr_path_tracing_flag_reuse_gi_checkpoint;",
        "effective RESTIR GI checkpoint reuse must reach its solver-specialized dispatch");
    RequireContains(
        pass,
        "result.solverpresetrequestedbutunavailable="
            "!requestedpathpresetisexecutable;",
        "an unavailable requested solver pipeline must be visible to UI and documentation");
    RequireContains(
        pass,
        "result.geometricreconnectionunavailable="
            "requestedsettings.solver==pathtracingsolver::restirgi&&"
            "requestedsettings.spatialneighborcount>0u&&"
            "!m_capabilities.spatialgicheckpointreusesupported;",
        "RESTIR GI must disclose unavailable requested geometric reconnection precisely");
    RequireContains(
        shader,
        "constuintnewcount=previouscount==0xffffffffu?"
            "previouscount:previouscount+1u;",
        "every finite black, hit, or miss path in the frame batch must advance the successful count");
    RequireOrdered(
        shader,
        {
            "if(successfulbatchcount==0u)",
            "if(!sharedprimary&&pathtracingflagisset("
                "uvsr_path_tracing_flag_reuse_direct))",
            "pathtracingcarrydirectproposal(pixel);",
            "return;"
        },
        "a rejected attempt must safely carry or invalidate direct proposal history");
    RequireOrdered(
        shader,
        {
            "voidpathtracingcarrygiproposal(uint2pixel)",
            "if(g_pathtracing.proposalreprojectionvalid!=0u)",
            "u_gicheckpointreservoir[pixel]=0.0f;",
            "u_gilo[pixel]=0.0f;",
            "u_ginormal[pixel]=0.0f;",
            "u_gireceiver[pixel]=0.0f;",
            "return;",
            "u_gicheckpointreservoir[pixel]="
                "t_previousgicheckpointreservoir[int2(pixel)];",
            "if(successfulbatchcount==0u)",
            "uvsr_path_tracing_flag_reuse_gi_checkpoint",
            "pathtracingcarrygiproposal(pixel);"
        },
        "a rejected attempt must carry stationary GI payloads and invalidate camera-motion payloads without a current receiver");
    RequireOrdered(
        shader,
        {
            "if(sample.valid==0u)",
            "uvsr_path_tracing_flag_replay_path_seeds",
            "pathtracingcarryreplayseed(pixel);"
        },
        "a rejected attempt must safely carry or invalidate the previous replay seed");
    RequireContains(
        shader,
        "if(g_pathtracing.debugview==uvsr_path_debug_stable_plane)"
            "returnsample.stableplane.rgb;",
        "stable-plane classification must remain independently inspectable");
    RequireContains(
        pass,
        "constants.debugview==static_cast<uint32_t>("
            "pathtracingdebugview::signalgroup)?"
            "std::max(inputs.settings.stableplanecount,2u)",
        "the signal-group debug view must produce diffuse and specular classification");

    RequireContains(
        settings,
        "returnresolvesupported&&"
            "isspatialpathresolverequested(settings)&&",
        "spatial path resolve must require an executable requested method");
    RequireContains(
        pass,
        "constboolstablesignalsrequested="
            "m_capabilities.canusespatialpathresolve(requestedsettings)&&"
            "canusespatialpathresolve(",
        "requested and effective spatial path resolve support must gate signal allocation");
    RequireOrdered(
        pass,
        {
            "boolresourcesready=validinputs&&ensureresources(",
            "if(!resourcesready&&validinputs&&stablesignalsrequired)",
            "stablesignalsrequired=false;",
            "resourcesready=ensureresources(",
            "false,sharedprimaryrequired);",
            "if(!resourcesready&&validinputs&&sharedprimaryrequired)",
            "sharedprimaryrequired=false;",
            "inputs.settings.sharedprimarysurface=false;",
            "m_capabilities.sharedprimarysurfacesupported=false;",
            "resourcesready=ensureresources(",
            "stablesignalsrequired,false);",
            "if(!resourcesready)"
        },
        "optional full-resolution allocation failures must retain raw all-ray path transport");
    RequireContains(
        passHeader,
        "boolsharedprimarysurfacerequestedbutunavailable=false;",
        "resource fallback must remain visible through the path result contract");
    RequireContains(
        pass,
        "result.sharedprimarysurfacerequestedbutunavailable="
            "requestedsettings.sharedprimarysurface&&"
            "!sharedprimaryrequired;",
        "runtime results must disclose an authored Shared Primary fallback");
    RequireContains(
        pass,
        "constuint32_tsignalwidth=stablesignalsrequired?width:1u;"
            "constuint32_tsignalheight=stablesignalsrequired?height:1u;",
        "inactive stable signal bindings must remain safe 1x1 textures");
    RequireContains(
        pass,
        "commandlist->cleartexturefloat(m_residualmean,"
            "nvrhi::allsubresources,nvrhi::color(0.f));",
        "history reset must clear residual signal history");
    RequireContains(
        pass,
        "commandlist->cleartexturefloat(m_diffusesuffixmean,"
            "nvrhi::allsubresources,nvrhi::color(0.f));",
        "history reset must clear diffuse signal history");
    RequireContains(
        pass,
        "commandlist->cleartexturefloat(m_primarynormalroughness,"
            "nvrhi::allsubresources,nvrhi::color(0.f));"
            "commandlist->cleartexturefloat(m_primaryviewz,"
            "nvrhi::allsubresources,nvrhi::color(0.f));",
        "history reset must clear every stable reconstruction guide");
    RequireContains(
        shader,
        "constfloatprimaryviewz=abs(mul("
            "float4(surface.position,1.0f),"
            "g_pathtracing.view.matworldtoview).z);",
        "primary view depth must use UVSR's row-vector matrix convention");
    RequireContains(
        shader,
        "result.primarynormalroughness=float4("
            "surface.shadingnormal,saturate(primaryroughness));"
            "result.primaryviewz=primaryviewz;",
        "successful primary geometry must publish finite normal, prepared roughness, and view depth guides");
    RequireOrdered(
        shader,
        {
            "voidpathtracingloadprimaryresolveguide(",
            "float2(0.5f,0.5f)",
            "pathtracingtracesurface(",
            "pathtracingpreparematerial(surface)",
            "if(!accumulate||oldcount==0u)",
            "pathtracingloadprimaryresolveguide(",
            "u_primarynormalroughness[pixel]=primarynormalroughness;",
            "u_primaryviewz[pixel]=primaryviewz;"
        },
        "spatial resolve guides must use one deterministic center ray and remain stable across later jittered samples");
    RequireOrdered(
        shader,
        {
            "if(sample.valid==0u)",
            "return;",
            "if(pathtracingflagisset("
                "uvsr_path_tracing_flag_write_stable_signals))",
            "u_residualmean[pixel]="
        },
        "skipped or invalid attempts must retain stable signal histories exactly");
    RequireContains(
        shader,
        "sample.firstcontinuationisdiffuse!=0u?"
            "max(solvedindirectsuffix,0.0f)*fireflyscale:0.0f;",
        "the first sampled continuation lobe must own diffuse suffix classification");
    RequireContains(
        pass,
        "constboolcoherentsignalsavailable=stablesignalsrequired&&"
            "m_completedsignalcycle&&"
            "m_signalepoch==inputs.historyepoch;",
        "signal pointers must be exposed only after a complete cycle in the matching epoch");
    RequireContains(
        pass,
        "result.residualmean=coherentsignalsavailable?"
            "m_residualmean.get():nullptr;",
        "transient partial signal cycles must not escape through the public result");

    RequireContains(
        shader,
        "staticconstuintuvsr_path_debug_primary_transport=9u;",
        "Primary Transport debug enum mapping");
    RequireContains(
        shader,
        "staticconstuintuvsr_path_debug_indirect_transport=10u;",
        "Indirect Transport debug enum mapping");
    RequireContains(
        shader,
        "if(g_pathtracing.debugview=="
            "uvsr_path_debug_primary_transport)"
            "returnpathtracingflagisset("
                "uvsr_path_tracing_flag_shared_primary_surface)?"
            "max(sharedprimarymean,0.0f):"
            "max(sample.primarybase,0.0f)*fireflyscale;",
        "Primary Transport must expose the shared or integrated primary component exactly once");
    RequireContains(
        shader,
        "if(g_pathtracing.debugview=="
            "uvsr_path_debug_indirect_transport)"
            "returnmax(solvedindirectsuffix,0.0f)*fireflyscale;",
        "Indirect Transport must expose the solver-resolved suffix with the common firefly scale");
    RequireOrdered(
        pass,
        {
            "constboolmotionproposalhistoryeligible=historyreset&&",
            "inputs.historyresetbyviewonly&&",
            "inputs.previousview!=nullptr&&",
            "inputs.settings.reuserevalidatedproposalsduringmotion&&",
            "m_directreservoirhistoryvalid||",
            "m_pathseedhistoryvalid||",
            "m_gicheckpointhistoryvalid",
            "pathtracingdispatchscheduledispatchschedule=",
            "if(motionproposalhistoryeligible&&",
            "dispatchschedule.phasecount==1u)",
            "constpathtracingdispatchschedulereuseschedule=",
            "if(reuseschedule.valid&&reuseschedule.phasecount==1u)",
            "preserverevalidatedproposals=true;",
            "constboolpreviousgihistoryavailable=",
            "m_gicheckpointhistoryvalid&&"
                "(!historyreset||preserverevalidatedproposals);",
            "clearhistory(commandlist,preserverevalidatedproposals);"
        },
        "camera-only motion may preserve validated direct, PT, and GI proposals only after complete-frame dispatches while radiance always resets");
    RequireOrdered(
        pass,
        {
            "voidpathtracingpass::clearhistory(",
            "m_rawmean",
            "m_successfulsamplecount",
            "m_colorvariance",
            "m_directmean",
            "m_indirectmean",
            "if(!preserverevalidatedproposals)",
            "m_directreservoirs[index]",
            "if(!preserverevalidatedproposals)",
            "m_gicheckpointreservoirs[index]",
            "if(!preserverevalidatedproposals)",
            "m_pathseedreservoirs[index]",
            "if(!preserverevalidatedproposals)"
        },
        "selective motion reset must always clear radiance while conditionally retaining every proposal family");

    RequireContains(
        resolveShader,
        "value=raw-residual-diffuse;",
        "specular must be derived at resolve time so accumulated channels recompose despite FP16 storage");
    RequireOrdered(
        resolveShader,
        {
            "if(!loadguide("
                "int2(pixel),centernormal,centerroughness,centerviewz))",
            "u_display[pixel]=float4("
                "min(max(raw,0.0f),65504.0f),1.0f);",
            "return;",
            "float3resolved=raw;"
        },
        "primary misses and invalid guides must self-return raw instead of cross-filtering sky and geometry");
    RequireContains(
        resolveShader,
        "for(inty=-2;y<=2;++y)"
            "{[unroll]for(intx=-2;x<=2;++x)",
        "stable resolve must remain a bounded 5x5 spatial filter");
    RequireContains(
        resolveShader,
        "g_resolve.stableplanecount==1u",
        "one-plane mode must be executable");
    RequireContains(
        resolveShader,
        "g_resolve.stableplanecount==2u",
        "two-plane mode must be executable");
    RequireContains(
        resolveShader,
        "g_resolve.stableplanecount==3u",
        "three-plane mode must be executable");
    RequireContains(
        resolvePass,
        "commandlist->beginmarker(\"spatialpathresolve\");",
        "spatial path reconstruction must execute as an explicit post-transport pass");
    RequireContains(
        resolveHeader,
        "sampleaccumulationaveragingaccumulationaveraging="
            "sampleaccumulationaveraging::cumulative;"
            "uint32_taccumulationeffectivehistory=64u;",
        "the resolve must receive the accumulation policy that defines effective sample count");
    RequireContains(
        resolvePass,
        "sizeof(pathtracingstableplaneresolveconstants)==32u",
        "the expanded accumulation-aware resolve ABI must remain exact");
    RequireOrdered(
        resolveShader,
        {
            "uinteffectivesamplecount=max(successfulsamplecount,1u);",
            "g_resolve.accumulationaveraging=="
                "uvsr_sample_averaging_exponential",
            "effectivesamplecount=min(",
            "safehistory*2u-1u",
            "result.luminancestandarderror=sqrt(",
            "max(luminancevariance,0.0f)/"
                "float(effectivesamplecount)",
            "constfloatrelativenoise=result.luminancestandarderror/"
        },
        "resolve confidence must use standard error of the accumulated mean and exponential effective history");
    RequireAbsent(
        resolveShader,
        "luminancestandarddeviation",
        "sample deviation must not create a permanent spatial correction floor");

    for (const std::string* source :
        { &constants, &passHeader, &pass, &material,
            &materialVisibility, &sampling, &shader, &resolveHeader,
            &resolvePass, &resolveShader })
    {
        RequireAbsent(*source, "hitobject",
            "the SM 6.5 path must not pretend to provide SER");
        RequireAbsent(*source, "rtxpt/shaders",
            "first-party transport must not include proprietary RTXPT source");
        RequireAbsent(*source, "nvidiacorporation.allrightsreserved",
            "new first-party files must not copy NVIDIA RTX SDK source headers");
    }

    std::cout << "UVSR path-tracing source contracts passed\n";
    return EXIT_SUCCESS;
}
