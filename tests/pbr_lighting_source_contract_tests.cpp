#include "direct_light_visibility.h"

#include <array>
#include <cctype>
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
    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "PBR lighting source-contract validation failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
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

    size_t CountOccurrences(
        std::string_view source,
        std::string_view token)
    {
        Require(!token.empty(), "an occurrence token cannot be empty");
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

    void RequireContains(
        std::string_view source,
        std::string_view token,
        const std::string& message)
    {
        Require(source.find(token) != std::string_view::npos, message);
    }
}

int main(int argc, char** argv)
{
    Require(argc == 2,
        "expected the UVSR source directory as the only argument");

    const std::filesystem::path sourceDirectory = argv[1];
    const std::string deferredShader = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_deferred_lighting_cs.hlsl"));
    const std::string deferredMsaaShader = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_deferred_lighting_msaa_cs.hlsl"));
    const std::string deferredPass = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_deferred_lighting_pass.cpp"));
    const std::string deferredPassHeader = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_deferred_lighting_pass.h"));
    const std::string deferredConstants = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_deferred_lighting_cb.h"));
    const std::string pbrLighting = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_lighting.hlsli"));
    const std::string pbrCore = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr.hlsli"));
    const std::string flashlightShared = Canonicalize(ReadSource(
        sourceDirectory / "src/flashlight_shared.h"));
    const std::string gbufferShader = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_gbuffer_ps.hlsl"));
    const std::string compositeShader = Canonicalize(ReadSource(
        sourceDirectory / "src/screen_space_indirect_composite_cs.hlsl"));
    const std::string rendererSource = Canonicalize(ReadSource(
        sourceDirectory / "src/uvsr.cpp"));

    Require(
        !std::filesystem::exists(
            sourceDirectory / "src/pbr_forward_ps.hlsl") &&
            !std::filesystem::exists(
                sourceDirectory /
                    "src/screen_space_visibility_fused_apply_cs.hlsl"),
        "dead forward and fused AO-only shaders must remain removed");
    RequireContains(
        rendererSource,
        "m_pbrdeferredlightingpass",
        "the renderer must retain its singular deferred PBR path");
    RequireContains(
        rendererSource,
        "static_assert(static_cast<uint32_t>("
            "pbrlightingdebugview::skyvisibility)==12u);",
        "the CPU Sky Visibility debug ordinal must match both shaders");
    Require(
        rendererSource.find("m_forwardpass") == std::string::npos &&
            rendererSource.find("renderermode::forward") ==
                std::string::npos,
        "the renderer must not restore a selectable forward PBR mode");

    Require(
        CountOccurrences(
            deferredShader,
            "texture2d<float4>t_flashlightvisibility") == 1u &&
            CountOccurrences(
                deferredShader,
                "texture2d<float4>t_sunvisibility") == 1u,
        "the single-sample deferred shader must declare independent "
        "flashlight and sun visibility textures");
    Require(
        CountOccurrences(
            deferredMsaaShader,
            "texture2d<float4>t_flashlightvisibility") == 1u &&
            CountOccurrences(
                deferredMsaaShader,
                "texture2d<float4>t_sunvisibility") == 1u,
        "the MSAA deferred shader must declare independent flashlight and "
        "sun visibility textures");

    for (const std::string* shader :
        { &deferredShader, &deferredMsaaShader })
    {
        RequireContains(
            *shader,
            "texture2d<float4>t_flashlightvisibility:register(t20);",
            "flashlight visibility must bind at t20");
        RequireContains(
            *shader,
            "texture2d<float4>t_sunvisibility:register(t21);",
            "sun visibility must bind at t21");
        RequireContains(
            *shader,
            "texture2d<float>t_skyvisibility:register(t22);",
            "ray-traced sky visibility must bind independently at t22");
        RequireContains(
            *shader,
            "constboolshowskyvisibility="
                "g_pbrdeferred.lightingdebugview==12u;",
            "the sky debug view must request the scalar independently of IBL application");
        RequireContains(
            *shader,
            "elseif(showskyvisibility)",
            "the sky debug branch must remain available without a light probe");
        RequireContains(
            *shader,
            "=skyvisibility.xxx;",
            "the sky debug branch must emit a grayscale visibility field");
        RequireContains(
            *shader,
            "if(encoding==uvsr_direct_visibility_rgb_rgba16f)"
                "{returnsaturate(encoded.rgb);}"
                "returnsaturate(encoded.r).xxx;",
            "direct visibility must decode RGB ratio modulation and "
            "scalar R8 visibility from their explicit encoding");
        RequireContains(
            *shader,
            "if(any(!isfinite(encoded)))return1.0f;",
            "non-finite directional modulation must fail open to white");
        RequireContains(
            *shader,
            "visibility=min(visibility,decodedirectlightvisibility(",
            "exact-light inputs must combine by componentwise minimum");
        RequireContains(
            *shader,
            "float3directmodulation=getdirectlightvisibility("
                "lightindex,pixelposition);",
            "each direct-light loop must use the exact-light "
            "visibility inputs");
        Require(
            shader->find("register(t15)") == std::string::npos &&
                shader->find("register(t16)") == std::string::npos &&
                shader->find("getscreenshadowvisibility") ==
                    std::string::npos &&
                shader->find("screenspacedirectionalvisibility") ==
                    std::string::npos &&
                shader->find("ratioestimatordirectionalvisibility") ==
                    std::string::npos &&
                shader->find("indirectspecular") == std::string::npos,
            "unused inherited indirect-specular and retired directional "
            "screen-shadow inputs "
            "must remain retired");
        RequireContains(
            *shader,
            "evaluateshadowpoisson(",
            "each deferred variant must evaluate attached local-light shadow "
            "maps");
    }
    RequireContains(
        deferredShader,
        "#ifwrite_source_radiance"
            "texture2d<float4>t_sourcesunvisibility:register(t23);"
            "#endif",
        "the alternate sun source must exist only in the write-source shader");
    Require(
        deferredMsaaShader.find("register(t23)") == std::string::npos,
        "the alternate sun source must not enter an MSAA pipeline");
    RequireContains(
        deferredShader,
        "if(int(lightindex)!=g_pbrdeferred."
            "sourcesunvisibilitylightindex)"
            "{returnfinalvisibility;}",
        "unmatched lights must preserve their ordinary final modulation");
    RequireContains(
        deferredShader,
        "if(any(!isfinite(encodedsourcesun)))"
            "returnfinalvisibility;",
        "invalid alternate texels must fall back to ordinary final sun modulation");
    RequireContains(
        deferredShader,
        "constfloat3sourcedirectmodulation="
            "usesourcesunvisibility?getsourcedirectlightvisibility("
            "lightindex,pixelposition,directmodulation):directmodulation;",
        "source-only sun modulation must not replace final direct modulation");
    RequireContains(
        deferredShader,
        "directdiffuse+=direct.diffuse*directmodulation;"
            "directspecular+=direct.specular*directmodulation;",
        "ordinary total modulation must continue to own final direct lighting");
    RequireContains(
        deferredShader,
        "sourcedirectdiffuse+=direct.diffuse*sourcedirectmodulation;",
        "the GI source must apply its alternate only to direct diffuse");
    Require(
        CountOccurrences(
            deferredShader,
            "evaluatedirectlightprevalidated(") == 1u,
        "final and source lighting must share one BRDF evaluation");
    RequireContains(
        deferredShader,
        "constfloat3sourcediffuse=usesourcesunvisibility?"
            "sourcedirectdiffuse:directdiffuse;",
        "omitting the alternate must preserve the ordinary source path");

    RequireContains(
        deferredPass,
        "constboolhasskyvisibilityconsumer="
            "applyskyvisibilitytodiffuseibl||"
            "applyskyvisibilitytospecularibl||"
            "lightingdebugview==12u;",
        "the deferred pass must bind sky visibility for the debug view even when both IBL effects are off");
    RequireContains(
        deferredPass,
        ":(applyskyvisibilitytospecularibl?"
            "uvsr_sky_visibility_apply_specular_ibl:"
            "uvsr_sky_visibility_apply_neither))",
        "a debug-only sky texture must encode Neither when both IBL effects are off");
    RequireContains(
        pbrCore,
        "boolhaspositivefinitepbrsignal(float3signal,floatthroughput)",
        "PBR must retain its exact finite positive signal gate");
    RequireContains(
        pbrCore,
        "boolcanevaluatepbrdirectsurfaceprepared(",
        "PBR must retain its direct-surface hemisphere gate");
    for (const std::string* source :
        { &pbrCore, &deferredShader, &deferredMsaaShader })
    {
        Require(
            source->find("lightingcontribution") == std::string::npos &&
                source->find("lightingsource_") == std::string::npos &&
                source->find("lightingrejection_") == std::string::npos,
            "unused contribution-source and rejection taxonomies must "
            "remain retired");
    }
    RequireContains(
        deferredMsaaShader,
        "float3finallinearhdr=g_pbrdeferred.lightingdebugview==0u?"
            "max(t_resolvedbackground[pixelposition].rgb,0.0f):0.0f;",
        "MSAA information filters must use a black no-surface background");
    RequireContains(
        deferredMsaaShader,
        "constfloatsampledepth=t_gbufferdepth.load("
            "pixelposition,sampleindex);",
        "MSAA lighting must load the sample depth once");
    RequireContains(
        deferredMsaaShader,
        "if(!isfinite(sampledepth)||sampledepth<=0.0f||"
            "!(dot(normalchannels.xyz,normalchannels.xyz)>1e-12f))"
            "returnfalse;",
        "MSAA lighting coverage must require finite positive depth and a surface normal");
    RequireContains(
        rendererSource,
        "constboolscreenspacevisibilityrequested="
            "m_ui.hasactivescreenspacevisibilityconsumer();",
        "PBR information filters must not disable Visibility execution");
    RequireContains(
        rendererSource,
        "constboolscreenspacevisibilityready="
            "screenspacevisibilityrequested&&"
            "m_screenspacevisibilitypass&&"
            "m_screenspacevisibilitypass->arepipelinesready()&&"
            "bool(visibilitynoise);"
            "constboolrunscreenspacevisibility="
            "screenspacevisibilityready;",
        "Visibility execution must fail closed when its pass, pipelines, or "
        "resolved noise asset are unavailable");
    RequireContains(
        compositeShader,
        "if(g_visibility.visibilitydebugview==0u&&"
            "g_visibility.lightingdebugview!=0u)"
            "{u_output[pixel]=t_baselighting[pixel];return;}",
        "PBR information filters must pass through Visibility composition "
        "without adding unrelated lighting");
    RequireContains(
        deferredShader,
        "constfloat3presentedcolor="
            "g_pbrdeferred.lightingdebugview!=0u?"
            "lightingdebugcolor:finallinearhdr;",
        "PBR information filters must change presentation without replacing "
        "the production lighting calculation");
    RequireContains(
        deferredMsaaShader,
        "if(g_pbrdeferred.visibilitydebugview!=0u)"
            "{finallinearhdr=t_visibilitycomposite[pixelposition].rgb;}"
            "elseif(g_pbrdeferred.lightingdebugview==0u)",
        "MSAA must give an explicit Visibility view precedence and suppress "
        "ordinary Visibility composition under a PBR filter");
    RequireContains(
        rendererSource,
        "if(!pathtransportactive&&"
            "m_ui.lightingdebugview==pbrlightingdebugview::none&&"
            "!m_ui.hasactivescreenspacevisibilitydebugconsumer()&&"
            "m_ui.showenvironmentbackground&&",
        "information filters must use a black environment background and only "
        "a successful path-transport dispatch may suppress the raster background");
    RequireContains(
        compositeShader,
        "u_output[pixel]=g_visibility.visibilitydebugview==0u?"
            "t_baselighting[pixel]:float4(0.0f,0.0f,0.0f,1.0f);",
        "Visibility filters must use a neutral no-surface background");
    RequireContains(
        pbrCore,
        "returnisdoublesided?dot(geometricnormal,viewdirection)<0.0f:!isfrontface;",
        "double-sided normal orientation must use the view hemisphere while single-sided surfaces retain raster facing");
    RequireContains(
        gbufferShader,
        "returndirectionorposition.w>0.0f?directionorposition.xyz-surfaceposition:-directionorposition.xyz;",
        "deferred material fill must construct perspective and orthographic view directions with the transport-facing sign");
    RequireContains(
        gbufferShader,
        "if(shouldflippbrsurfacenormals(isdoublesided,i_isfrontface,surface.geometrynormal,viewdirection))",
        "deferred material fill must orient the evaluated geometric normal from the reconstructed view direction");
    Require(
        CountOccurrences(
            gbufferShader,
            "shouldflippbrsurfacenormals(") == 1u,
        "the retained deferred material path must use the shared "
        "normal-orientation contract exactly once");
    Require(
        gbufferShader.find("if(!i_isfrontface)") == std::string::npos,
        "deferred PBR must not orient double-sided normals solely from "
        "raster winding");
    RequireContains(
        gbufferShader,
        "float3trianglenormal=pbrsafenormalize("
            "cross(ddx(i_vtx.pos),ddy(i_vtx.pos)),surface.geometrynormal);",
        "the packed geometric normal must come from the raster triangle plane");
    RequireContains(
        gbufferShader,
        "if(dot(trianglenormal,viewdirection)<0.0f)"
            "trianglenormal=-trianglenormal;",
        "the raster triangle normal must face the visible view hemisphere");
    RequireContains(
        gbufferShader,
        "if(dot(surface.shadingnormal,trianglenormal)<0.0f)"
            "surface.shadingnormal=-surface.shadingnormal;",
        "the material shading normal must stay in the triangle hemisphere");
    RequireContains(
        gbufferShader,
        "pbrdata.shadingnormal=surface.shadingnormal;"
            "pbrdata.geometricnormal=trianglenormal;",
        "the G-buffer must preserve distinct shading and triangle normals");
    Require(
        gbufferShader.find("float3trianglenormal=") <
            gbufferShader.find("clip(surface.opacity-g_material.alphacutoff);"),
        "triangle derivatives must be evaluated before alpha discard");
    RequireContains(
        pbrLighting,
        "light.lighttype==lighttype_spot",
        "PBR lighting must retain spotlight evaluation");
    RequireContains(
        pbrLighting,
        "floatinverserangesquared=light.angularsizeor"
            "invrange*light.angularsizeor"
            "invrange;",
        "spotlights must retain finite-range attenuation");
    RequireContains(
        pbrLighting,
        "if(!(isfinite(light.radius)&&light.radius>0.0f))"
            "returnlight.intensity/distancesquared;",
        "zero-radius analytical lights must preserve the exact point branch");
    RequireContains(
        pbrLighting,
        "constfloathalfangularsize=atan(min("
            "light.radius*inversedistance,1.0f));",
        "positive-radius analytical lights must use projected angular size");
    RequireContains(
        pbrLighting,
        "constfloatradiancetimespi="
            "light.intensity/(light.radius*light.radius);",
        "finite analytical lights must preserve luminous-intensity energy");
    RequireContains(
        pbrLighting,
        "light.color*incidentlightintensity*"
            "(rangeweight*spotweight)",
        "analytical emitter energy must precede range and spot weights");
    RequireContains(
        pbrLighting,
        "spotweight*=spotweight*(3.0f-2.0f*spotweight);",
        "spotlights must retain a smooth cone edge");
    RequireContains(
        pbrLighting,
        "#include\"flashlight_shared.h\"",
        "PBR lighting must consume the shared flashlight transport constants");
    RequireContains(
        flashlightShared,
        "structflashlightbeamprofile",
        "the flashlight must use an explicit first-party beam profile");
    RequireContains(
        flashlightShared,
        "static_assert(sizeof(flashlightbeamprofile)==48u",
        "the flashlight profile must occupy three constant registers");
    RequireContains(
        flashlightShared,
        "evaluateflashlightbeamprofile(",
        "the shared profile must own the two-lobe beam response");
    RequireContains(
        pbrLighting,
        "booluseflashlightprofile,"
            "flashlightbeamprofileflashlightprofile)",
        "the light sampler must receive explicit flashlight identity and "
        "profile data");
    RequireContains(
        pbrLighting,
        "if(validflashlightprofile)"
            "{spotweight=evaluateflashlightbeamprofile(",
        "only the exact valid flashlight profile may replace the ordinary "
        "spot cone response");
    RequireContains(
        pbrLighting,
        "floatcostheta=dot(-sample.directiontolight,lightdirection);",
        "ordinary spotlights must retain their standard cosine cone path");
    for (const std::string* shader :
        { &deferredShader, &deferredMsaaShader })
    {
        RequireContains(
            *shader,
            "samplepbrlight(",
            "every deferred PBR lighting path must share flashlight shape "
            "evaluation");
    }
    RequireContains(
        rendererSource,
        "m_flashlight=std::make_shared<spotlight>();",
        "the flashlight must be one ordinary scene spot light");
    RequireContains(
        rendererSource,
        "resolveflashlightbeamprofile(",
        "the renderer must publish the explicit beam profile separately");
    Require(
        rendererSource.find("flashlightspotlight") == std::string::npos &&
            rendererSource.find("m_flashlighthotspot") == std::string::npos &&
            rendererSource.find("m_flashlightshadowmap") ==
                std::string::npos &&
            rendererSource.find("lightconstants.shadowchannel[") ==
                std::string::npos,
        "the flashlight must not restore a subclass, duplicate lobe, private "
        "shadow map, or shadow-channel transport");
    Require(
        CountOccurrences(
            deferredPass,
            "bindinglayoutitem::texture_srv(20)") == 2u &&
            CountOccurrences(
                deferredPass,
                "bindinglayoutitem::texture_srv(21)") == 2u,
        "each directional visibility slot needs one normal and one MSAA CPU "
        "layout entry");
    Require(
        CountOccurrences(
            deferredPass,
            "bindinglayoutitem::texture_srv(22)") == 2u,
        "sky visibility needs one normal and one MSAA CPU layout entry");
    Require(
        CountOccurrences(
            deferredPass,
            "bindinglayoutitem::texture_srv(23)") == 1u &&
            CountOccurrences(
                deferredPass,
                "bindingsetitem::texture_srv(23,") == 1u,
        "alternate source sun visibility must bind only in the write-source pipeline");
    RequireContains(
        deferredPass,
        "bindingsetitem::texture_srv(20,"
            "activevisibilities.flashlight.texture?"
            "activevisibilities.flashlight.texture:"
            "m_commonpasses->m_whitetexture.get())",
        "flashlight visibility must fail open to white");
    RequireContains(
        deferredPass,
        "bindingsetitem::texture_srv(21,"
            "activevisibilities.sun.texture?"
            "activevisibilities.sun.texture:"
            "m_commonpasses->m_whitetexture.get())",
        "sun visibility must fail open to white");
    RequireContains(
        deferredPass,
        "bindingsetitem::texture_srv(22,"
            "activeskyvisibility?activeskyvisibility:"
            "m_commonpasses->m_whitetexture.get())",
        "sky visibility must bind white when inactive or incompatible");
    Require(
        deferredPass.find("bindinglayoutitem::texture_srv(15)") ==
                std::string::npos &&
            deferredPass.find("bindinglayoutitem::texture_srv(16)") ==
                std::string::npos &&
            deferredPass.find("inputs.indirectspecular") ==
                std::string::npos &&
            deferredPass.find("inputs.shadowchannels") ==
                std::string::npos,
        "the CPU pass must omit unused inherited indirect-specular and "
        "screen-shadow bindings");
    RequireContains(
        deferredPass,
        "constants.directvisibilitylightindices=int2(-1);",
        "unmatched visibility slots must retain neutral light indices");
    RequireContains(
        deferredPass,
        "constants.sourcesunvisibilitylightindex=-1;",
        "an absent or rejected source alternate must select ordinary final sun modulation");
    RequireContains(
        deferredPass,
        "if(candidate.iscomplete()&&"
            "activevisibilities.sun.iscomplete()&&"
            "candidate.light==activevisibilities.sun.light)"
            "{activesourcesunvisibility=candidate;}",
        "the source alternate must match the accepted final sun exactly");
    RequireContains(
        deferredPass,
        "bindingsetitem::texture_srv(23,"
            "activesourcesunvisibility.texture?"
            "activesourcesunvisibility.texture:"
            "m_commonpasses->m_whitetexture.get())",
        "the optional source binding must remain valid when no alternate is accepted");
    RequireContains(
        deferredPass,
        "uvsr::targetsdirectlight("
            "activevisibilities.flashlight,light.get())",
        "the flashlight CPU adapter must use pointer-identical matching");
    RequireContains(
        deferredPass,
        "uvsr::targetsdirectlight("
            "activevisibilities.sun,light.get())",
        "the sun CPU adapter must use pointer-identical matching");
    RequireContains(
        deferredPass,
        "constants.directvisibilitylightindices.x="
            "int(deferredconstants.numlights);",
        "the CPU adapter must publish the flashlight light index");
    RequireContains(
        deferredPass,
        "constants.directvisibilitylightindices.y="
            "int(deferredconstants.numlights);",
        "the CPU adapter must publish the sun light index");
    RequireContains(
        deferredPassHeader,
        "std::array<pipeline,2>m_pipelines;",
        "deferred lighting must retain only no-source and one-source "
        "pipelines");
    RequireContains(
        deferredPassHeader,
        "nvrhi::itexture*visibilitycomposite=nullptr,"
            "constuvsr::directlightvisibility&"
            "sourcesunvisibility={});",
        "the alternate source sun must remain a final optional render argument");
    RequireContains(
        deferredConstants,
        "intflashlightlightindex;"
            "intsourcesunvisibilitylightindex;"
            "uintsourcesunvisibilityencoding;"
            "uintflashlightpadding;",
        "the source sun contract must consume only two former padding words");
    RequireContains(
        deferredPass,
        "sizeof(pbrdeferredlightingconstants)=="
            "sizeof(deferredlightingconstants)+96",
        "the source sun contract must preserve the deferred constant-buffer byte size");
    Require(
        deferredShader.find("write_bounce_metadata") == std::string::npos &&
            deferredShader.find("sourcemetadata") == std::string::npos &&
            deferredPass.find("writebouncemetadata") == std::string::npos &&
            deferredPassHeader.find("writebouncemetadata") ==
                std::string::npos,
        "multi-bounce metadata variants must be absent");
    RequireContains(
        deferredShader,
        "u_sourceradiance[pixelposition]=float4("
            "min(sourceradiance,65504.0f),0.0f);",
        "the retained one-bounce source must leave alpha unclassified");
    Require(
        pbrCore.find("pbrgimetadata_") == std::string::npos,
        "the shared PBR taxonomy must not retain multi-bounce metadata bits");

    for (const std::string* shader :
        { &deferredShader, &deferredMsaaShader })
    {
        RequireContains(
            *shader,
            "texturecubearrayt_diffuseenvironment:register(t1);",
            "each deferred variant must bind diffuse IBL at t1");
        RequireContains(
            *shader,
            "texturecubearrayt_specularenvironment:register(t2);",
            "each deferred variant must bind specular IBL at t2");
        RequireContains(
            *shader,
            "texture2dt_environmentbrdf:register(t3);",
            "each deferred variant must bind the environment BRDF at t3");
        RequireContains(
            *shader,
            "evaluatepbrenvironmentdiffuse(",
            "each deferred variant must evaluate diffuse IBL");
        RequireContains(
            *shader,
            "if(applyskyvisibilitytodiffuseibl)"
                "environmentdiffuse*=skyvisibility;",
            "each deferred variant must conditionally modulate diffuse IBL");
        RequireContains(
            *shader,
            "evaluatepbrenvironmentspecular(",
            "each deferred variant must evaluate specular IBL");
        RequireContains(
            *shader,
            "if(applyskyvisibilitytospecularibl)"
                "environmentspecular*=skyvisibility;",
            "each deferred variant must conditionally modulate specular IBL");
        Require(
            CountOccurrences(*shader, "t_skyvisibility[") == 1u,
            "each deferred variant must share one guarded sky-visibility load");
    }
    RequireContains(
        compositeShader,
        "texturecubearrayt_diffuseenvironment:register(t7);",
        "separate visibility composition must bind diffuse IBL at t7");
    RequireContains(
        compositeShader,
        "texturecubearrayt_specularenvironment:register(t10);",
        "visibility composition must bind specular IBL at t10");
    RequireContains(
        compositeShader,
        "texture2dt_environmentbrdf:register(t11);",
        "visibility composition must bind the environment BRDF at t11");
    RequireContains(
        compositeShader,
        "texture2d<float>t_skyvisibility:register(t12);",
        "visibility composition must bind sky visibility at t12");
    RequireContains(
        compositeShader,
        "evaluatepbrenvironmentdiffuse(",
        "visibility composition must evaluate diffuse IBL");
    RequireContains(
        compositeShader,
        "evaluatepbrenvironmentspecular(",
        "visibility composition must evaluate specular IBL");
    RequireContains(
        compositeShader,
        "if(applyskyvisibilitytodiffuseibl)"
            "environmentdiffuse*=skyvisibility;",
        "visibility composition must modulate recomputed diffuse IBL independently");
    RequireContains(
        compositeShader,
        "if(applyskyvisibilitytospecularibl)"
            "environmentspecular*=skyvisibility;",
        "visibility composition must modulate recomputed specular IBL independently");
    Require(
        CountOccurrences(compositeShader, "t_skyvisibility[") == 1u,
        "visibility composition must share one guarded sky-visibility load");

    RequireContains(
        rendererSource,
        "directlightvisibilitiesdirectlightvisibilities;",
        "the renderer must retain named flashlight and sun producer slots");
    Require(
        CountOccurrences(
            rendererSource,
            "nvrhi::itexture*flashlightvisibility="
                "flashlightshadowresult.visibility;") == 1u &&
            CountOccurrences(
                rendererSource,
                "nvrhi::itexture*sunvisibility="
                    "heitzshadowresult.modulation;") == 1u &&
        CountOccurrences(
            rendererSource,
            "directlightvisibilities.flashlight={"
                "flashlightvisibility,"
                "flashlightshadowresult.light,"
                "directlightvisibilityencoding::scalarr8unorm}") ==
                1u &&
            CountOccurrences(
                rendererSource,
                "directlightvisibilities.sun={"
                    "sunvisibility,"
                    "heitzshadowresult.light,"
                    "sunvisibilitydenoised&&"
                        "shadowdenoisingmethod=="
                            "denoisingmethodchoice::sigma?"
                        "directlightvisibilityencoding::scalarr8unorm:"
                        "directlightvisibilityencoding::rgbrgba16float}") ==
                1u,
        "each raw or denoised direct visibility must remain paired exactly "
        "once with its pointer identical light and matching encoding");
    RequireContains(
        rendererSource,
        "m_raytracedflashlightshadowpass->render(",
        "flashlight shadows must come from the ray traced visibility pass");
    Require(
        rendererSource.find("renderflashlightshadow()") ==
                std::string::npos &&
            rendererSource.find("planarshadowmap") == std::string::npos &&
            rendererSource.find("m_flashlight->shadowmap=") ==
                std::string::npos,
        "the private raster flashlight shadow system must stay removed");

    // Every production lighting variant must retain the same environment
    // contract; imported variants belong in this explicit list.
    constexpr std::array<std::string_view, 5u> LightingSources = {
        "src/pbr_deferred_lighting_cs.hlsl",
        "src/pbr_deferred_lighting_msaa_cs.hlsl",
        "src/pbr_lighting.hlsli",
        "src/pbr_environment.hlsli",
        "src/screen_space_indirect_composite_cs.hlsl"
    };
    for (const std::string_view relativePath : LightingSources)
    {
        const std::string source = Canonicalize(ReadSource(
            sourceDirectory /
                std::filesystem::path(std::string(relativePath))));
        Require(
            source.find("ambientcolortop") == std::string::npos &&
                source.find("ambientcolorbottom") == std::string::npos,
            std::string(relativePath) +
                " must not restore the retired two-color ambient inputs");
        Require(
            source.find("normal.y*0.5+0.5") == std::string::npos &&
                source.find("normal.y*0.5f+0.5f") == std::string::npos,
            std::string(relativePath) +
                " must not restore the visibility-free normal-Y ambient term");
    }

    std::cout << "UVSR PBR lighting source-contract validation passed\n";
    return EXIT_SUCCESS;
}
