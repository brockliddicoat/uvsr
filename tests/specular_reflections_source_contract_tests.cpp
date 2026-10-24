#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
    [[noreturn]] void Fail(const std::string& message)
    {
        std::cerr << "Specular reflection source-contract validation failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            Fail(message);
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
        for (const char character : source)
        {
            const unsigned char byte =
                static_cast<unsigned char>(character);
            if (!std::isspace(byte))
            {
                canonical.push_back(
                    static_cast<char>(std::tolower(byte)));
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

    void RequireOrdered(
        std::string_view source,
        std::initializer_list<std::string_view> tokens,
        const std::string& message)
    {
        size_t offset = 0u;
        for (const std::string_view token : tokens)
        {
            const size_t position = source.find(token, offset);
            Require(position != std::string_view::npos, message);
            offset = position + token.size();
        }
    }

    size_t CountOccurrences(
        std::string_view source,
        std::string_view token)
    {
        Require(!token.empty(), "occurrence token cannot be empty");
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
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: specular source contract <source-root>\n";
        return 2;
    }

    const std::filesystem::path root = argv[1];
    const std::string settings = Canonicalize(ReadSource(
        root / "src/specular_settings.h"));
    const std::string passHeader = Canonicalize(ReadSource(
        root / "src/specular_reflections.h"));
    const std::string pass = Canonicalize(ReadSource(
        root / "src/specular_reflections.cpp"));
    const std::string shared = Canonicalize(ReadSource(
        root / "src/specular_reflections_shared.hlsli"));
    const std::string trace = Canonicalize(ReadSource(
        root / "src/specular_reflections_trace_cs.hlsl"));
    const std::string rayQuery = Canonicalize(ReadSource(
        root / "src/specular_reflections_ray_query_cs.hlsl"));
    const std::string resolve = Canonicalize(ReadSource(
        root / "src/specular_reflections_resolve_cs.hlsl"));

    RequireContains(
        settings,
        "boolenabled=false;boolratioestimator=true;"
            "booldeferredcolorlookup=true;",
        "standalone SSSR must default off with both Frostbite controls on");
    RequireContains(
        settings,
        "structhybridreflectionsettings{boolenabled=false;"
            "boolraytracing=true;",
        "Hybrid must default off while retaining its gated RT fallback");
    RequireOrdered(
        settings,
        {
            "if(settings.hybrid.enabled)return"
                "speculartechnique::hybrid;",
            "returnsettings.sssr.enabled?"
                "speculartechnique::stochasticscreenspace:"
                "speculartechnique::none;"
        },
        "Hybrid must own the single final result whenever both techniques are enabled");

    RequireContains(
        passHeader,
        "nvrhi::itexture*indirectspecular=nullptr;",
        "the pass must expose one indirect-specular contribution texture");
    RequireContains(
        passHeader,
        "nvrhi::itexture*skyspecularestimate=nullptr;"
            "floatspecularenvironmentscale=1.f;",
        "the pass must consume one paired-contract sky specular texture");
    RequireContains(
        passHeader,
        "uint32_tskyestimatekind=uvsr_sky_estimate_none;",
        "the sky texture must carry an explicit NONE/SCALAR/RATIO/FULL kind");
    RequireContains(
        passHeader,
        "externaldebugviewactive,",
        "external PBR and visibility debug ownership must be reportable");
    RequireContains(
        passHeader,
        "voiddeactivateforexternaldebugview("
            "constspecularsettings&settings);",
        "external debug ownership must explicitly deactivate the pass");
    RequireAbsent(
        passHeader,
        "skyvisibilityspecularchannel",
        "the repaired paired sky contract must replace legacy channel selection");
    RequireContains(
        passHeader,
        "boolglobalraytraversalallowed=false;"
            "boolcoherenttlasready=false;",
        "RT fallback must consume both global traversal gates");
    RequireContains(
        pass,
        "inputs.globalraytraversalallowed&&"
            "inputs.coherenttlasready&&inputs.worldtlas;",
        "RT dispatch must fail open unless both global gates and a TLAS are ready");
    RequireContains(
        pass,
        "m_readiness.status=m_readiness.raytracingavailable?"
            "specularreflectionstatus::readyhybrid:"
            "specularreflectionstatus::readyscreenspace;",
        "missing RT support must preserve the screen-space candidate");
    RequireContains(
        pass,
        "constants.backfacerejection=readiness.technique=="
            "speculartechnique::hybrid?"
            "settings.hybrid.backfacerejection:"
            "speculardefaultbackfacerejection;",
        "Hybrid-only hit-rejection settings must not leak into standalone SSSR");
    RequireContains(
        pass,
        "returnkind==uvsr_sky_estimate_scalar_visibility?"
            "description.format==nvrhi::format::r8_unorm:"
            "description.format==nvrhi::format::rgba16_float;",
        "scalar sky input must be R8 while paired ratio and full inputs must be RGBA16F");
    RequireOrdered(
        pass,
        {
            "voidspecularreflectionpass::"
                "deactivateforexternaldebugview(",
            "constspecularsettingssettings="
                "sanitizespecularsettings(",
            "releaseresources();",
            "m_readiness={};",
            "m_readiness.status=specularreflectionstatus::"
                "externaldebugviewactive;",
            "m_readiness.technique="
                "resolvespeculartechnique(settings);"
        },
        "external debug ownership must release transient activity and preserve the sanitized requested technique");

    RequireOrdered(
        pass,
        {
            "m_depthmipgenerator->dispatch(commandlist);",
            "m_scenecolormipgenerator->dispatch(commandlist);",
            "state.pipeline=m_tracepipeline.pipeline;",
            "if(readiness.raytracingavailable){",
            "state.pipeline=m_resolvepipeline.pipeline;",
            "result.indirectspecular=m_resolvedspecular;"
        },
        "execution must build pyramids, trace screen space, optionally route RT, then resolve once");
    RequireContains(
        pass,
        "reversedepth?donut::render::mipmapgenpass::mode_max:"
            "donut::render::mipmapgenpass::mode_min",
        "HiZ reduction must follow the active depth convention");
    RequireContains(
        pass,
        "while(largest>1u){largest>>=1u;++mipcount;}",
        "mip allocation must use the API's floor-halved non-power-of-two geometry");
    RequireContains(
        pass,
        "destination->bindinglayout="
            "m_rayquerycompactpipeline.bindinglayout;",
        "compact and dense RayQuery entry points must share binding compatibility");

    RequireOrdered(
        shared,
        {
            "constfloatunbiasedalpha=perceptualroughnesstoalpha(",
            "constfloatproposalalpha=max(unbiasedalpha*"
                "exp2(-g_specular.traceandfilterbias),",
            "sourcepdf=specularvisibleggxdirectionpdf(",
            "*specularrayselectionprobability();"
        },
        "each stochastic ray must retain the PDF of its actual narrowed source proposal");
    RequireContains(
        shared,
        "returnmax(lerp(saturate(normaldotview*2.0f),1.0f,"
            "sqrt(saturate(roughness))),1e-3f);",
        "Frostbite specular elongation must survive at grazing angles");
    RequireOrdered(
        shared,
        {
            "constfloattraceconetangent=unbiasedconetangent*"
                "exp2(-g_specular.traceandfilterbias);",
            "log2(max(tracefootprintpixels,1.0f))+"
                "g_specular.traceandfilterbias"
        },
        "ray narrowing and mip filtering must use one compensating signed bias");
    RequireContains(
        shared,
        "returnimagebasedlightingreceivermip("
            "saturate(roughness),"
            "g_specular.specularenvironmentmiplevels);",
        "environment fallback must retain UVSR's native prefilter roughness mapping");
    RequireOrdered(
        shared,
        {
            "float3specularprojectclippedrayendpoint(",
            "computevisibilityprojectionendpointclip(",
            "g_specular.reversedepth!=0u);",
            "if(clip.valid==0u)",
            "endpointclip=lerp(originclip,endpointclip,clip.endpointscale);",
            "returnspecularprojectclipposition(endpointclip,valid);"
        },
        "screen rays must clip a surviving segment to the active D3D camera-near plane before projection");

    RequireOrdered(
        trace,
        {
            "u_hituvpdfdistance[pixel]=float4(hit.xy,sourcepdf,"
                "hitdistance);",
            "u_hitdirectionfootprint[pixel]=float4(raydirection,"
                "footprint);",
            "u_hitmetadata[pixel]=uint4(",
            "u_hitconfidence[pixel]=confidence;"
        },
        "screen records must retain hit, PDF, distance, direction, footprint, identity, and confidence");
    RequireOrdered(
        trace,
        {
            "constfloat3endpoint=specularprojectclippedrayendpoint(",
            "worldposition,",
            "worldposition+raydirection*g_specular.raymaxdistance,",
            "if(endpointvalid&&dot(screendirection,screendirection)>1e-12f)",
            "hit=specularhierarchicaltrace("
        },
        "a distant endpoint behind the eye must not discard its traceable pre-near-plane segment");
    RequireContains(
        trace,
        "waveactivecountbits(compactray)",
        "low-confidence Hybrid rays must be wave-compacted");
    RequireContains(
        trace,
        "returnfloat2(max(baseresolution>>mip,1u));",
        "HiZ traversal must address the same floor-halved mip geometry that was allocated");
    RequireContains(
        trace,
        "confidence<g_specular.screenspaceconfidence",
        "the confidence control must gate fallback rays");
    RequireContains(
        trace,
        "specularclamphalfradiance(radiance),roughness);",
        "stored source radiance must retain source roughness for exact environment separation");

    RequireContains(
        rayQuery,
        "rayquery<ray_flag_cull_back_facing_triangles|"
            "ray_flag_force_opaque|"
            "ray_flag_skip_procedural_primitives>query;",
        "the RT fallback must be a compact inline RayQuery");
    RequireOrdered(
        rayQuery,
        {
            "constboolcommitted=query.committedstatus()=="
                "committed_triangle_hit;",
            "boolprojectable=committed&&projectionvalid&&",
            "constboolshadeable=dot("
                "visiblenormalchannels.xyz,"
                "visiblenormalchannels.xyz)>1e-12f;",
            "projectable=shadeable&&all(isfinite(visibleworld))&&",
            "constuintpath=projectable?"
                "specularhitpathraytracedscreen:"
                "specularhitpathenvironment;",
            "query.committedinstanceid()",
            "query.committedgeometryindex()",
            "query.committedprimitiveindex()"
        },
        "RT hits must reuse screen radiance only when projectable and shadeable, otherwise route to the environment with identity retained");
    RequireAbsent(
        rayQuery,
        "evaluatescenematerial(",
        "off-screen RT hits must not invoke a second material shading system");
    RequireContains(
        rayQuery,
        "specularclamphalfradiance(radiance),roughness);",
        "RayQuery source radiance must preserve the same roughness contract");

    RequireOrdered(
        resolve,
        {
            "constfloat3fallbackradiance=specularresolveenvironment(",
            "if(roughness>g_specular.maxroughness)continue;",
            "float3normalizedradiance=fallbackradiance;",
            "constfloat3specularresponsefactor="
                "evaluatepbrenvironmentspecular("
        },
        "surfaces outside the stochastic roughness range must retain the deferred native IBL response");
    RequireOrdered(
        resolve,
        {
            "constfloat3worldposition=reconstructworldposition("
                "g_specular.view,pixelcenter,t_depth[pixel]);",
            "if(g_specular.deferredcolorlookup!=0u){",
            "radiance=specularloaddeferredradiance(",
            "constfloatdestinationggx=speculardestinationggxweight(",
            "weight=destinationggx/max(sourcepdf,"
                "specularminimumpdf);",
            "numerator+=radiance*weight;",
            "environmentnumerator+=environmentcontribution*weight;",
            "denominator+=weight;",
            "environmentweight+=weight*environmentfraction;",
            "normalizedradiance=numerator/denominator;",
            "normalizedenvironmentradiance="
                "environmentnumerator/denominator;",
            "constfloat3specularresponsefactor="
                "evaluatepbrenvironmentspecular(",
            "float3resolved=max(normalizedradiance,0.0f)*"
                "specularresponsefactor;"
        },
        "Frostbite reuse must evaluate destination BRDF over source PDF, normalize, then apply the preintegrated DFG once");
    Require(
        CountOccurrences(resolve, "evaluatepbrenvironmentspecular(") == 1u,
        "the preintegrated DFG helper must be called exactly once");
    RequireContains(
        resolve,
        "texture2d<float4>t_skyspecularestimate:register(t13);",
        "one sky specular binding must serve all paired estimator kinds");
    RequireOrdered(
        resolve,
        {
            "uvsr_sky_estimate_scalar_visibility)",
            "saturate(t_skyspecularestimate[pixel].r)",
            "adjustedenvironmentlobe*=scalarvisibility.xxx;",
            "uvsr_sky_estimate_ratio_modulation)",
            "adjustedenvironmentlobe*=all(isfinite(ratiomodulation))?"
                "saturate(ratiomodulation):1.0f;",
            "uvsr_sky_estimate_full_stochastic_radiance)",
            "adjustedenvironmentlobe=all(isfinite(fullstochastic))?"
                "max(fullstochastic,0.0f)*"
                "g_specular.specularenvironmentscale*"
                "pbrenvironmentspecularocclusion("
        },
        "scalar and ratio sky estimates must modulate the accumulated analytic environment while FULL replaces it without DFG");
    RequireContains(
        resolve,
        "resolved+=adjustedenvironmentlobe-"
            "analyticenvironmentlobe;",
        "scalar and ratio estimates must replace the exact accumulated analytic environment contribution");
    RequireContains(
        resolve,
        "resolved+=adjustedenvironmentlobe*environmentshare-"
            "analyticenvironmentlobe;",
        "FULL must remove the exact analytic contribution and scale its integrated replacement only by environment share");
    RequireContains(
        resolve,
        "u_resolvedspecular[pixel]=float4("
            "specularclamphalfradiance(resolved),0.0f);",
        "the result must be finite reflection-only RGB with zero alpha for additive compositing");
    RequireAbsent(
        resolve,
        "rwtexture2d<float4>u_scenecolor",
        "the resolver must not write back into the sampled scene-color pyramid");

    for (const std::string_view forbidden : {
            "motionvector",
            "historytexture",
            "temporalaccumulation",
            "ffx_denoiser",
            "reprojecthistory" })
    {
        RequireAbsent(pass, forbidden, "the pass must not carry temporal or denoiser state");
        RequireAbsent(trace, forbidden, "screen tracing must remain current-frame only");
        RequireAbsent(rayQuery, forbidden, "RT fallback must remain current-frame only");
        RequireAbsent(resolve, forbidden, "resolve must remain current-frame only");
    }

    std::cout << "Specular reflection source contracts passed.\n";
    return 0;
}
