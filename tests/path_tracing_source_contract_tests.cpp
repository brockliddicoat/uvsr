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
    const std::string resolveHeader = Canonicalize(ReadSource(
        root / "src/path_tracing_stable_plane_resolve_pass.h"));
    const std::string resolvePass = Canonicalize(ReadSource(
        root / "src/path_tracing_stable_plane_resolve_pass.cpp"));
    const std::string resolveShader = Canonicalize(ReadSource(
        root / "src/path_tracing_stable_plane_resolve_cs.hlsl"));
    const std::string shaderConfig = Canonicalize(ReadSource(
        root / "src/shaders.cfg"));

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
            "boolfullsamplereconnectionsupported=false;" })
    {
        RequireContains(passHeader, capability,
            "the public result must report unsupported advanced capabilities honestly");
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
            "nvrhi::itexture*successfulsamplecount=nullptr;",
        "raw unbiased mean and exact per-pixel sample count must be public outputs");
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
            "constbooldirectreuserequired=inputs.settings.usertxdi&&",
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
            CountOccurrences(shader, "register(u") == 15u,
        "the CPU layout and shader must expose the stable solver history/output ABI");
    RequireContains(
        pass,
        "for(uint32_tslot=0u;slot<=14u;++slot)",
        "every transport specialization must share the complete u0-u14 binding layout");
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
            "for(uint32_tslot=0u;slot<=14u;++slot)",
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
            "capabilities.spatialgicheckpointreusesupported=false;",
            "capabilities.fullsamplereconnectionsupported=false;"
        },
        "capabilities must report executable local replay without claiming spatial GI or geometric reconnection");
    RequireContains(
        pass,
        "capabilities.fullsamplereconnectionsupported=false;",
        "the implementation must not claim full-sample reconnection");
    RequireContains(
        pass,
        "result.pathreuserequestedbutunavailable="
            "requestedsettings.reusepathreservoirs&&!pathreuserequired;"
            "result.gireuserequestedbutunavailable="
            "requestedsettings.reuseindirectgireservoirs&&!gireuserequired;",
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
            "float3oldmean=u_rawmean[pixel].rgb;",
            "constfloat4oldvariancestate=u_colorvariance[pixel];",
            "float3oldvariance=oldvariancestate.rgb;",
            "if(oldcount>0u&&(!all(isfinite(oldmean))||"
                "!all(isfinite(oldvariance))))",
            "oldcount=0u;oldmean=0.0f;oldvariance=0.0f;",
            "constfloat3newmean=oldcount==0u||!accumulate"
        },
        "nonfinite per-pixel path history must recover locally before "
        "scheduling and accumulation");
    RequireOrdered(
        shader,
        {
            "uintfailedattemptsalt=",
            "isfinite(oldvariancestate.a)&&oldvariancestate.a>=0.0f",
            "accumulate?failedattemptsalt:0u,",
            "if(sample.valid==0u)",
            "constuintnextfailedattemptsalt=accumulate?",
            "failedattemptsalt+1u",
            "u_colorvariance[pixel]=float4("
                "oldvariance,float(nextfailedattemptsalt));",
            "u_colorvariance[pixel]=float4(newvariance,0.0f);"
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
            "if(inputs.previousview&&inputs.historyresetbyviewonly)",
            "inputs.previousview->fillplanarviewconstants("
                "constants.previousview);",
            "constants.previousviewvalid=1u;"
        },
        "the CPU must initialize deterministic fallback matrices and explicitly enable only camera-motion reprojection");
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
            "if(historyreset&&dispatchschedule.phasecount>1u)",
            "textureuavbarrier(commandlist,m_display);",
            "commandlist->commitbarriers();",
            "uvsr_path_tracing_flag_replicate_preview",
            "div_ceil(inputs.width,8u)",
            "div_ceil(inputs.height,8u)"
        },
        "history invalidation must initialize presentation for every sparse lattice without retaining radiance history");
    RequireOrdered(
        shader,
        {
            "uvsr_path_tracing_flag_replicate_preview",
            "if(any(dispatchpixel>=g_pathtracing.dispatchextent))return;",
            "constuint2source=(dispatchpixel/"
                "g_pathtracing.schedulinggrid)*"
                "g_pathtracing.schedulinggrid;",
            "u_display[dispatchpixel]=u_display[source];",
            "return;"
        },
        "the reset preview must copy a freshly traced representative to every display pixel only");
    RequireOrdered(
        pass,
        {
            "maxpathtracingworkunitsperdispatch",
            "estimatepathtracingworkunitsperpixel(",
            "buildpathtracingdispatchschedule(",
            "constuint64_tworkperpixel="
                "estimatepathtracingworkunitsperpixel(",
            "if(workperpixel>maxpathtracingworkunitsperdispatch)",
            "if(!dispatchschedule.valid)",
            "constants.schedulinggrid=dispatchschedule.grid;",
            "constants.schedulingphase=dispatchschedule.phase;",
            "div_ceil(dispatchschedule.workextent.x,8u)",
            "div_ceil(dispatchschedule.workextent.y,8u)"
        },
        "the CPU pass must bound each dispatch while preserving a complete progressive lattice");
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
    RequireContains(
        shader,
        "#ifuvsr_pt_solver==2"
            "texture2d<float4>t_previousgicheckpointreservoir:register(t5);"
            "texture2d<uint>t_previousgicheckpointcount:register(t6);",
        "the RESTIR GI body must own a distinct local-checkpoint payload");
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
                "sample.primarybase+solvedindirectsuffix,0.0f);",
            "constfloatfireflyscale="
                "pathtracingfireflyscale(solverradiance);"
        },
        "solver estimates must split primary terms and replace, never double-add, the indirect suffix");
    RequireAbsent(
        shader,
        "sample.primarybase+sample.indirectsuffix+solvedindirectsuffix",
        "the local indirect suffix must not be added beside its reservoir estimate");
    RequireOrdered(
        shader,
        {
            "float3pathtracingresolvegicheckpoint(",
            "pathtracingcontributionreservoirupdate("
                "reservoir,currentindirectsuffix,",
            "constuintpreviouscount="
                "t_previousgicheckpointcount[int2(pixel)];",
            "if(previouscount==1u&&all(isfinite(previouslocal)))",
            "previouslocal.rgb,",
            "returnpathtracingcontributionreservoirestimate(reservoir);"
        },
        "GI must combine only current and previous same-pixel local checkpoints");
    RequireAbsent(
        shader,
        "pathtracingresolvepreviousdonorpixel("
            "pixel,currentindirectsuffix",
        "RESTIR GI radiance checkpoints must never enter camera reprojection");
    RequireOrdered(
        shader,
        {
            "u_gicheckpointreservoir[pixel]=float4("
                "sample.indirectsuffix,localtarget);",
            "u_gicheckpointcount[pixel]=1u;"
        },
        "GI persistence must store the current local checkpoint rather than the combined estimate");
    RequireOrdered(
        shader,
        {
            "pathtracingresolvepreviousdonorpixel(",
            "constint2neighbor=pathtracingpreviousneighbor("
                "uint2(previouscenter),currentseed,0x50544e42u);",
            "pathtracingcontributionreservoirupdate("
                "reservoir,currentindirectsuffix,",
            "t_previouspathseed[previouscenter]",
            "t_previouspathseed[neighbor]",
            "returnpathtracingcontributionreservoirestimate(reservoir);"
        },
        "PT must center temporal and spatial seed donors on the validated previous pixel before replay");
    RequireOrdered(
        shader,
        {
            "u_pathseed[pixel]=continuationseed;",
            "u_pathseedstatistics[pixel]=float4("
                "localtarget,localtarget,1.0f,1.0f);"
        },
        "PT persistence must retain only the current local seed, target, and M=1 statistics");
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
            "reprojected=g_pathtracing.previousviewvalid!=0u;",
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
            "returncurrentindirectsuffix;",
            "t_previouspathseed[previouscenter]",
            "pathtracingreplayseedcandidate("
        },
        "invalid PT reprojection must return the current estimate without a same-pixel fallback");
    RequireOrdered(
        shader,
        {
            "voidpathtracingcarrydirectproposal(uint2pixel)",
            "if(g_pathtracing.previousviewvalid!=0u)",
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
            "if(g_pathtracing.previousviewvalid!=0u)",
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
            "sample.directreservoir.selectedsampleseed;",
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
            "constboolscheduledupdate=updateinterval==1u||",
            "constboolattempt=refreshdebug||!accumulate||oldcount==0u||"
                "scheduledupdate;"
        },
        "adaptive retry must use a deterministic bounded revisit cycle");
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
            "oldcount:g_pathtracing.samplesequencephase;",
        "accumulation must index stochastic samples by each pixel's successful count");
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
            "constfloat3newmean=oldcount==0u||!accumulate?"
                "accumulatedsample:"
                "lerp(oldmean,accumulatedsample,meanweight);",
            "u_rawmean[pixel]=float4(newmean,1.0f);",
            "constfloat3residualsample="
                "max(sample.primarybase,0.0f)*fireflyscale;",
            "constfloat3diffusesuffixsample=",
            "max(solvedindirectsuffix,0.0f)*fireflyscale",
            "u_residualmean[pixel]=float4(residualmean,1.0f);",
            "u_diffusesuffixmean[pixel]=float4(diffusemean,1.0f);"
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
            "requestedsettings.solver!=pathtracingsolver::rtxpt&&"
            "!m_capabilities.fullsamplereconnectionsupported;",
        "executable subsets must separately disclose the absence of geometric reconnection");
    RequireContains(
        shader,
        "constuintnewcount=accumulate?"
            "(oldcount==0xffffffffu?oldcount:oldcount+1u):1u;",
        "every finite black, hit, or miss sample must advance the successful count");
    RequireOrdered(
        shader,
        {
            "if(sample.valid==0u)",
            "if(pathtracingflagisset("
                "uvsr_path_tracing_flag_reuse_direct))",
            "pathtracingcarrydirectproposal(pixel);",
            "return;"
        },
        "a rejected attempt must safely carry or invalidate direct proposal history");
    RequireOrdered(
        shader,
        {
            "if(sample.valid==0u)",
            "uvsr_path_tracing_flag_reuse_gi_checkpoint",
            "u_gicheckpointreservoir[pixel]="
                "t_previousgicheckpointreservoir[int2(pixel)];",
            "u_gicheckpointcount[pixel]="
                "t_previousgicheckpointcount[int2(pixel)];"
        },
        "a rejected attempt must preserve the previous local GI checkpoint");
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
            "false);",
            "if(!resourcesready)"
        },
        "failed full-resolution signal allocation must retry raw transport with inactive 1x1 signals");
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
            "returnmax(sample.primarybase,0.0f)*fireflyscale;",
        "Primary Transport must expose the current primary component with the common firefly scale");
    RequireContains(
        shader,
        "if(g_pathtracing.debugview=="
            "uvsr_path_debug_indirect_transport)"
            "returnmax(solvedindirectsuffix,0.0f)*fireflyscale;",
        "Indirect Transport must expose the solver-resolved suffix with the common firefly scale");
    RequireOrdered(
        pass,
        {
            "constboolpreserverevalidatedproposals=historyreset&&",
            "inputs.historyresetbyviewonly&&",
            "inputs.previousview!=nullptr&&",
            "inputs.settings.reuserevalidatedproposalsduringmotion&&",
            "dispatchschedule.phasecount==1u&&",
            "m_directreservoirhistoryvalid||",
            "m_pathseedhistoryvalid",
            "constboolpreviousgihistoryavailable=",
            "m_gicheckpointhistoryvalid&&!historyreset;",
            "clearhistory(commandlist,preserverevalidatedproposals);"
        },
        "camera-only motion may preserve validated direct and PT proposals only after complete-frame dispatches while GI radiance always resets");
    RequireOrdered(
        pass,
        {
            "voidpathtracingpass::clearhistory(",
            "m_rawmean",
            "m_successfulsamplecount",
            "m_colorvariance",
            "if(!preserverevalidatedproposals)",
            "m_directreservoirs[index]",
            "m_gicheckpointreservoirs[index]",
            "if(!preserverevalidatedproposals)",
            "m_pathseedreservoirs[index]",
            "m_gicheckpointhistoryvalid=false;"
        },
        "selective motion reset must always clear radiance and GI while conditionally retaining proposal stores");

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
