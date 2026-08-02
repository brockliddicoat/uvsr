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

    static_assert(uvsr::DirectionalLightVisibilityCount == 3u,
        "the experimental deferred-lighting seam has three producer slots");

    const std::filesystem::path sourceDirectory = argv[1];
    const std::string deferredShader = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_deferred_lighting_cs.hlsl"));
    const std::string deferredMsaaShader = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_deferred_lighting_msaa_cs.hlsl"));
    const std::string deferredPass = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_deferred_lighting_pass.cpp"));
    const std::string pbrLighting = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_lighting.hlsli"));
    const std::string pbrCore = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr.hlsli"));
    const std::string flashlightShared = Canonicalize(ReadSource(
        sourceDirectory / "src/flashlight_shared.h"));
    const std::string forwardShader = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_forward_ps.hlsl"));
    const std::string gbufferShader = Canonicalize(ReadSource(
        sourceDirectory / "src/pbr_gbuffer_ps.hlsl"));
    const std::string compositeShader = Canonicalize(ReadSource(
        sourceDirectory / "src/screen_space_indirect_composite_cs.hlsl"));
    const std::string fusedShader = Canonicalize(ReadSource(
        sourceDirectory /
            "src/screen_space_visibility_fused_apply_cs.hlsl"));
    const std::string rendererSource = Canonicalize(ReadSource(
        sourceDirectory / "src/uvsr.cpp"));

    Require(
        CountOccurrences(
            deferredShader,
            "texture2d<float>t_directionalvisibility") == 3u,
            "the current single-sample deferred shader must declare exactly three "
            "directional visibility textures");
    Require(
        CountOccurrences(
            deferredMsaaShader,
            "texture2d<float>t_directionalvisibility") == 3u,
        "the MSAA deferred shader must declare exactly three directional "
        "visibility textures");

    constexpr std::array<std::string_view, 3u> Components = {
        "x", "y", "z"
    };
    for (uint32_t slot = 0u; slot < Components.size(); ++slot)
    {
        const std::string slotText = std::to_string(slot);
        const std::string singleSampleRegister =
            std::to_string(20u + slot);
        const std::string msaaRegister =
            std::to_string(20u + slot);
        RequireContains(
            deferredShader,
            "texture2d<float>t_directionalvisibility" + slotText +
                ":register(t" + singleSampleRegister + ");",
            "directional visibility slot " + slotText +
                " must retain its unique single-sample shader register");
        RequireContains(
            deferredMsaaShader,
            "texture2d<float>t_directionalvisibility" + slotText +
                ":register(t" + msaaRegister + ");",
            "directional visibility slot " + slotText +
                " must retain its unique MSAA shader register");

        for (const std::string* shader :
            { &deferredShader, &deferredMsaaShader })
        {
            RequireContains(
                *shader,
                "if(int(lightindex)==g_pbrdeferred."
                    "directionalvisibilitylightindices." +
                    std::string(Components[slot]) +
                    "){visibility*=saturate(t_directionalvisibility" +
                    slotText + "[pixelposition]);}",
                "directional visibility slot " + slotText +
                    " must multiply only its exact indexed light");
        }
    }

    for (const std::string* shader :
        { &deferredShader, &deferredMsaaShader })
    {
        RequireContains(
            *shader,
            "floatvisibility=1.0f;",
            "directional visibility accumulation must start at neutral white");
        Require(
            CountOccurrences(
                *shader,
                "visibility*=saturate(t_directionalvisibility") == 3u,
            "all three visibility factors must clamp and multiply");
        RequireContains(
            *shader,
            "floatvisibility=getscreenshadowvisibility("
                "light,pixelposition)*getdirectionallightvisibility("
                "lightindex,pixelposition);",
            "each direct-light loop must multiply the legacy screen-shadow "
            "factor by the exact-light visibility product");
    }
    for (const std::string* shader :
        { &deferredShader, &deferredMsaaShader })
    {
        RequireContains(
            *shader,
            "texture2dt_shadowbuffer:register(t16);",
            "each deferred variant must preserve shadowChannels at t16");
        RequireContains(
            *shader,
            "floatgetscreenshadowvisibility(",
            "each deferred variant must preserve the inherited screen-shadow "
            "adapter");
        RequireContains(
            *shader,
            "evaluateshadowpoisson(",
            "each deferred variant must evaluate attached local-light shadow "
            "maps");
    }
    RequireContains(
        forwardShader,
        "evaluateshadowgather16(",
        "forward PBR must evaluate attached local-light shadow maps");
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
    RequireContains(
        forwardShader,
        "float3viewdirection=-viewincident;",
        "forward material fill must convert its incident vector into the surface-to-view direction");
    RequireContains(
        forwardShader,
        "if(shouldflippbrsurfacenormals(isdoublesided,i_isfrontface,sampledmaterial.geometrynormal,viewdirection))",
        "forward material fill must orient the evaluated geometric normal from its view direction");
    for (const std::string* shader : { &gbufferShader, &forwardShader })
    {
        Require(
            CountOccurrences(
                *shader,
                "shouldflippbrsurfacenormals(") == 1u,
            "each PBR material path must use the shared normal-orientation contract exactly once");
        Require(
            shader->find("if(!i_isfrontface)") == std::string::npos,
            "PBR material paths must not orient double-sided normals solely from raster winding");
    }
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
        { &forwardShader, &deferredShader, &deferredMsaaShader })
    {
        RequireContains(
            *shader,
            "samplepbrlight(",
            "every production PBR lighting path must share flashlight shape "
            "evaluation");
    }
    for (const std::string* shader :
        { &deferredShader, &deferredMsaaShader })
    {
        RequireContains(
            *shader,
            "light.shadowchannel.x",
            "deferred local-shadow selection must retain shadowChannel.x");
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
    for (uint32_t slot = 0u; slot < 3u; ++slot)
    {
        Require(
            CountOccurrences(
                deferredPass,
                "bindinglayoutitem::texture_srv(" +
                    std::to_string(20u + slot) + ")") == 2u,
            "each directional visibility slot needs one normal and one MSAA "
            "CPU layout entry");
        RequireContains(
            deferredPass,
            "bindingsetitem::texture_srv(20u+slot,"
                "activevisibility[slot].texture?"
                "activevisibility[slot].texture:"
                "m_commonpasses->m_whitetexture.get())",
            "directional visibility must fail open to white");
    }
    RequireContains(
        deferredPass,
        "bindingsetitem::texture_srv(16,inputs.shadowchannels?"
            "inputs.shadowchannels:m_commonpasses->m_blacktexture.get())",
        "the CPU binding set must preserve shadowChannels at t16");
    RequireContains(
        deferredPass,
        "constants.directionalvisibilitylightindices=int4(-1);",
        "unmatched visibility slots must retain the neutral light index");
    RequireContains(
        deferredPass,
        "uvsr::targetsdirectionallight("
            "activevisibility[slot],light.get())",
        "the CPU adapter must use pointer-identical light matching");
    RequireContains(
        deferredPass,
        "constants.directionalvisibilitylightindices[slot]="
            "int(deferredconstants.numlights);",
        "the CPU adapter must publish the matching deferred-light index");

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
        fusedShader,
        "texturecubearrayt_diffuseenvironment:register(t9);",
        "fused visibility application must bind diffuse IBL at t9");
    for (const std::string* shader :
        { &compositeShader, &fusedShader })
    {
        RequireContains(
            *shader,
            "texturecubearrayt_specularenvironment:register(t10);",
            "each visibility application variant must bind specular IBL at "
            "t10");
        RequireContains(
            *shader,
            "texture2dt_environmentbrdf:register(t11);",
            "each visibility application variant must bind the environment "
            "BRDF at t11");
        RequireContains(
            *shader,
            "evaluatepbrenvironmentdiffuse(",
            "each visibility application variant must evaluate diffuse IBL");
        RequireContains(
            *shader,
            "evaluatepbrenvironmentspecular(",
            "each visibility application variant must evaluate specular IBL");
    }

    Require(
        CountOccurrences(
            rendererSource,
            "directionallightvisibilitysetdirectionalvisibility{};") == 1u &&
            CountOccurrences(
                rendererSource,
                "directionalvisibility={{") == 1u,
        "the renderer must retain exactly one shared directional-visibility "
        "adapter initializer");
    constexpr std::array<std::string_view, 3u> ProducerPairs = {
        "{screenspaceshadowresult.nearvisibility,"
            "screenspaceshadowresult.light}",
        "{sparsevirtualshadowmapresult.visibility,"
            "sparsevirtualshadowmapresult.light}",
        "{diagnosticcsmresult.visibility,diagnosticcsmresult.light}"
    };
    for (const std::string_view producerPair : ProducerPairs)
    {
        Require(
            CountOccurrences(rendererSource, producerPair) == 1u,
            "each directional-visibility texture must remain paired exactly "
            "once with the pointer-identical light that produced it");
    }
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
    constexpr std::array<std::string_view, 7u> LightingSources = {
        "src/pbr_deferred_lighting_cs.hlsl",
        "src/pbr_deferred_lighting_msaa_cs.hlsl",
        "src/pbr_forward_ps.hlsl",
        "src/pbr_lighting.hlsli",
        "src/pbr_environment.hlsli",
        "src/screen_space_indirect_composite_cs.hlsl",
        "src/screen_space_visibility_fused_apply_cs.hlsl"
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
