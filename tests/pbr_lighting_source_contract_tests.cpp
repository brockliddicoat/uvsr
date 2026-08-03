#include "directional_light_visibility.h"

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
    Require(
        rendererSource.find("m_forwardpass") == std::string::npos &&
            rendererSource.find("renderermode::forward") ==
                std::string::npos,
        "the renderer must not restore a selectable forward PBR mode");

    Require(
        CountOccurrences(
            deferredShader,
            "texture2d<float>t_directionalvisibility") == 1u,
        "the single-sample deferred shader must declare exactly one "
        "directional visibility texture");
    Require(
        CountOccurrences(
            deferredMsaaShader,
            "texture2d<float>t_directionalvisibility") == 1u,
        "the MSAA deferred shader must declare exactly one directional "
        "visibility texture");

    for (const std::string* shader :
        { &deferredShader, &deferredMsaaShader })
    {
        RequireContains(
            *shader,
            "texture2d<float>t_directionalvisibility:register(t20);",
            "directional visibility must retain its single t20 binding");
        RequireContains(
            *shader,
            "if(int(lightindex)==g_pbrdeferred."
                "directionalvisibilitylightindex)"
                "{returnsaturate(t_directionalvisibility[pixelposition]);}"
                "return1.0f;",
            "directional visibility must clamp only its exact indexed light "
            "and fail open to white");
        Require(
            shader->find("register(t21)") == std::string::npos &&
                shader->find("register(t22)") == std::string::npos,
            "removed directional visibility slots must not retain bindings");
        RequireContains(
            *shader,
            "floatvisibility=getdirectionallightvisibility("
                "lightindex,pixelposition);",
            "each direct-light loop must use the singular exact-light "
            "visibility input");
        Require(
            shader->find("register(t15)") == std::string::npos &&
                shader->find("register(t16)") == std::string::npos &&
                shader->find("getscreenshadowvisibility") ==
                    std::string::npos &&
                shader->find("indirectspecular") == std::string::npos,
            "unused inherited indirect-specular and screen-shadow inputs "
            "must remain retired");
        RequireContains(
            *shader,
            "evaluateshadowpoisson(",
            "each deferred variant must evaluate attached local-light shadow "
            "maps");
    }
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
        rendererSource,
        "constboolrunscreenspacevisibility="
            "m_ui.hasactivescreenspacevisibilityconsumer();",
        "PBR information filters must not disable Visibility execution");
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
        "if(m_ui.lightingdebugview==pbrlightingdebugview::none&&"
            "!m_ui.hasactivescreenspacevisibilitydebugconsumer()&&"
            "m_ui.showenvironmentbackground&&",
        "information filters must use a black environment background");
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
        "spotweight*=spotweight*(3.0f-2.0f*spotweight);",
        "spotlights must retain a smooth cone edge");
    RequireContains(
        pbrLighting,
        "#include\"flashlight_shared.h\"",
        "PBR lighting must consume the shared flashlight transport constants");
    RequireContains(
        flashlightShared,
        "#defineuvsr_flashlight_shape_radius_tag1024.0f",
        "the flashlight shape transport must retain its remote radius tag");
    RequireContains(
        flashlightShared,
        "#defineuvsr_flashlight_min_shape_exponent2.0f",
        "the flashlight circle endpoint must remain exponent two");
    RequireContains(
        flashlightShared,
        "#defineuvsr_flashlight_max_shape_exponent16.0f",
        "the flashlight rounded-square endpoint must remain bounded");
    RequireContains(
        pbrLighting,
        "floatcostheta=dot(-sample.directiontolight,"
            "lightdirection);",
        "ordinary spotlights and the exact-circle endpoint must retain the "
        "original cosine path");
    RequireContains(
        pbrLighting,
        "shapeexponent>uvsr_flashlight_min_shape_exponent+1e-4f",
        "only noncircular flashlight beams may enter shaped evaluation");
    RequireContains(
        pbrLighting,
        "float3(light.shadowchannel.yzw)/"
            "uvsr_flashlight_axis_quantization",
        "the shaped flashlight must decode its packed camera-right axis");
    RequireContains(
        pbrLighting,
        "beamright-=lightdirection*dot(beamright,lightdirection);",
        "the shaped flashlight must orthogonalize its transported basis");
    RequireContains(
        pbrLighting,
        "float2poweredslope=pow(abs(beamslope),"
            "float2(shapeexponent,shapeexponent));",
        "the shaped flashlight must evaluate its bounded superellipse");
    RequireContains(
        pbrLighting,
        "costheta=rsqrt(1.0f+shapedslope*shapedslope);",
        "the superellipse distance must feed the existing cone falloff");
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
        "lightconstants.shadowchannel[1]=",
        "flashlight shape transport must start after shadowChannel.x");
    RequireContains(
        rendererSource,
        "lightconstants.shadowchannel[3]=",
        "flashlight shape transport must fill only the unused axis lanes");
    Require(
        rendererSource.find("lightconstants.shadowchannel[0]=") ==
            std::string::npos,
        "flashlight shape transport must not overwrite shadowChannel.x");
    Require(
        CountOccurrences(
            deferredPass,
            "bindinglayoutitem::texture_srv(20)") == 2u,
        "directional visibility needs one normal and one MSAA CPU layout "
        "entry");
    Require(
        deferredPass.find("bindinglayoutitem::texture_srv(21)") ==
                std::string::npos &&
            deferredPass.find("bindinglayoutitem::texture_srv(22)") ==
                std::string::npos,
        "removed visibility slots must not retain CPU layout entries");
    RequireContains(
        deferredPass,
        "bindingsetitem::texture_srv(20,activevisibility.texture?"
            "activevisibility.texture:"
            "m_commonpasses->m_whitetexture.get())",
        "directional visibility must fail open to white");
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
        "constants.directionalvisibilitylightindex=-1;",
        "unmatched visibility must retain the neutral light index");
    RequireContains(
        deferredPass,
        "uvsr::targetsdirectionallight("
            "activevisibility,light.get())",
        "the CPU adapter must use pointer-identical light matching");
    RequireContains(
        deferredPass,
        "constants.directionalvisibilitylightindex="
            "int(deferredconstants.numlights);",
        "the CPU adapter must publish the matching deferred-light index");
    RequireContains(
        deferredPassHeader,
        "std::array<pipeline,2>m_pipelines;",
        "deferred lighting must retain only no-source and one-source "
        "pipelines");
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
            "evaluatepbrenvironmentspecular(",
            "each deferred variant must evaluate specular IBL");
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
        "evaluatepbrenvironmentdiffuse(",
        "visibility composition must evaluate diffuse IBL");
    RequireContains(
        compositeShader,
        "evaluatepbrenvironmentspecular(",
        "visibility composition must evaluate specular IBL");

    Require(
        rendererSource.find("directionallightvisibilityset") ==
            std::string::npos,
        "the renderer must not retain the removed multi-producer set");
    Require(
        CountOccurrences(
            rendererSource,
            "{screenspaceshadowresult.nearvisibility,"
                "screenspaceshadowresult.light}") == 1u,
        "the retained visibility texture must remain paired exactly once "
        "with its pointer-identical light");
    RequireContains(
        rendererSource,
        "conststd::shared_ptr<ishadowmap>activeshadowmap="
            "settings.castshadows?m_flashlightshadowmap:nullptr;",
        "flashlight shadow association must honor the shadow setting");
    RequireContains(
        rendererSource,
        "m_flashlight->shadowmap=activeshadowmap;",
        "flashlight spill must submit its exact gated shadow map");
    RequireContains(
        rendererSource,
        "m_flashlighthotspot->shadowmap=activeshadowmap;",
        "flashlight hotspot must share the exact gated shadow map");

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
