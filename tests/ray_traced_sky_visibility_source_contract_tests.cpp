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
        std::cerr << "Ray-traced sky-visibility source-contract validation "
            "failed: " << message << '\n';
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

    std::string_view ExtractSection(
        std::string_view source,
        std::string_view beginToken,
        std::string_view endToken,
        const std::string& message)
    {
        const size_t begin = source.find(beginToken);
        Require(begin != std::string_view::npos, message + " start");
        const size_t end = source.find(endToken, begin + beginToken.size());
        Require(end != std::string_view::npos, message + " end");
        return source.substr(begin, end + endToken.size() - begin);
    }

    void ValidateSettingsAndProducer(
        const std::string& settings,
        const std::string& constants,
        const std::string& passHeader,
        const std::string& pass,
        const std::string& shader,
        const std::string& shaderConfig)
    {
        RequireContains(
            settings,
            "boolenabled=false;boolapplytodiffuseibl=true;"
                "boolapplytospecularibl=false;int32_tsampleratelog2=0;"
                "floatraybias=0.002f;",
            "sky visibility must default disabled, diffuse-only, at one sample and 0.002 bias");
        RequireContains(
            settings,
            "rayvisibilitymaxdistancemaxdistance="
                "rayvisibilitymaxdistance::maximum;",
            "sky visibility must default to the established Max reach");
        RequireContains(
            settings,
            "returnsettings.applytodiffuseibl||"
                "settings.applytospecularibl;",
            "either selected IBL lobe must activate the producer");
        RequireContains(
            settings,
            "raytracedskyvisibilitynoisepattern::voidclusterbluenoise;"
                "boolanimatesamples=true;",
            "sky visibility must default to animated void-cluster blue noise");
        RequireContains(
            settings,
            "raytracedskyvisibilityminimumsampleratelog2=0;",
            "the sample-rate domain must begin at one sample");
        RequireContains(
            settings,
            "raytracedskyvisibilitymaximumsampleratelog2=6;",
            "the sample-rate domain must end at 64 samples");
        RequireContains(
            settings,
            "\"1\",\"2\",\"4\",\"8\",\"16\",\"32\",\"64\"",
            "the sample-rate labels must expose the exact integer domain");
        RequireContains(
            settings,
            "raytracedskyvisibilitymaximumraybias=0.1f;",
            "the ray-bias domain must remain bounded at 0.1 world units");
        RequireContains(
            settings,
            "israyvisibilitymaxdistancesupported(settings.maxdistance)&&",
            "configuration validation must reject unsupported max-distance modes");
        RequireContains(
            settings,
            "settings.raybias>=0.f&&settings.raybias"
                "<=raytracedskyvisibilitymaximumraybias;",
            "configuration validation must enforce the ray-bias bounds");

        RequireOrdered(
            constants,
            {
                "uintsamplesequencephase;",
                "uintsamplecount;",
                "uintnoisepattern;",
                "floatraydistance;",
                "floatdepthquantizationstep;",
                "floatraybias;",
                "uintreversedepth;",
                "uintfloatdepth;"
            },
            "the producer constant block must carry only current-frame query state");

        const std::string_view inputs = ExtractSection(
            passHeader,
            "structraytracedskyvisibilityinputs{",
            "};structraytracedskyvisibilityresult",
            "producer input structure");
        Require(
            CountOccurrences(inputs, "nvrhi::itexture*") == 3u,
            "the producer must accept exactly depth, material, and normals");
        for (const std::string_view input : {
                "nvrhi::itexture*depth=nullptr;",
                "nvrhi::itexture*material=nullptr;",
                "nvrhi::itexture*normals=nullptr;" })
        {
            RequireContains(inputs, input, "the producer input set is incomplete");
        }
        RequireContains(
            passHeader,
            "returnvisibility!=nullptr;",
            "an empty producer result must be the fail-open availability signal");

        RequireContains(
            pass,
            "nvrhi::feature::raytracingaccelstruct",
            "the pass must require acceleration-structure support");
        RequireContains(
            pass,
            "nvrhi::feature::rayquery",
            "the pass must require inline ray-query support");
        RequireContains(
            pass,
            "hasformatsupport(device,nvrhi::format::r8_unorm,true)&&"
                "hasformatsupport(device,nvrhi::format::r16_unorm,false)",
            "the pass must gate its R8 UAV and R16 blue-noise formats");
        RequireContains(
            pass,
            "description.format=nvrhi::format::r8_unorm;",
            "the visibility output must use scalar R8_UNORM storage");
        RequireContains(
            pass,
            "description.dimension=nvrhi::texturedimension::texture2d;",
            "the visibility output must be a two-dimensional texture");
        RequireContains(
            pass,
            "description.isuav=true;",
            "the visibility output must support UAV writes");
        RequireContains(
            pass,
            "description.enableautomaticstatetracking("
                "nvrhi::resourcestates::shaderresource);",
            "the visibility output must be published as an SRV");

        const std::string_view resources = ExtractSection(
            pass,
            "boolraytracedskyvisibilitypass::ensureresources(",
            "boolraytracedskyvisibilitypass::ensurebindingset(",
            "producer resource validation");
        RequireOrdered(
            resources,
            {
                "constnvrhi::itexture*textures[]={inputs.depth,"
                    "inputs.material,inputs.normals};",
                "texture->getdesc().samplecount!=1u",
                "texture->getdesc().dimension!="
                    "nvrhi::texturedimension::texture2d",
                "texture->getdesc().width!=depthdescription.width",
                "texture->getdesc().height!=depthdescription.height",
                "m_outputvisibility->getdesc().width=="
                    "depthdescription.width",
                "m_outputvisibility->getdesc().height=="
                    "depthdescription.height",
                "createoutputtexture(m_device,depthdescription.width,"
                    "depthdescription.height)",
                "clearbindingset();",
                "m_outputvisibility=outputvisibility;"
            },
            "the producer must validate exact full-resolution single-surface inputs before replacing its output");
        RequireContains(
            pass,
            "returnleft.depth==right.depth&&left.material==right.material&&"
                "left.normals==right.normals;",
            "the binding cache must track every producer input identity");
        RequireContains(
            pass,
            "m_boundtlas==worldtlas&&sameinputs(m_boundinputs,inputs)",
            "the binding cache must include TLAS identity");
        RequireContains(
            pass,
            "nvrhi::bindinglayoutitem::raytracingaccelstruct(0)",
            "the producer must bind the shared TLAS");
        RequireContains(
            pass,
            "nvrhi::bindinglayoutitem::texture_srv(1),"
                "nvrhi::bindinglayoutitem::texture_srv(2),"
                "nvrhi::bindinglayoutitem::texture_srv(3),"
                "nvrhi::bindinglayoutitem::texture_srv(4),"
                "nvrhi::bindinglayoutitem::texture_uav(0)",
            "the producer must retain one compact current-frame binding layout");
        Require(
            CountOccurrences(pass, "commandlist->dispatch(") == 1u,
            "the producer must issue exactly one compute dispatch");
        RequireContains(
            pass,
            "div_ceil(viewextent.width(),8),"
                "div_ceil(viewextent.height(),8)",
            "the pass must dispatch its 8x8 shader over the full view");
        RequireContains(
            pass,
            "constants.samplesequencephase=settings.animatesamples?"
                "samplingphase:0u;",
            "disabled sample animation must hold producer phase zero");
        RequireContains(
            pass,
            "constfloatraydistance=resolverayvisibilitymaxdistance("
                "settings.maxdistance,scenediagonal);",
            "ray distance must resolve the shared Max or finite reach");
        RequireContains(
            pass,
            "if(std::isnan(raydistance))",
            "an invalid scene extent must fail open before DXR receives a NaN TMax");
        RequireContains(
            pass,
            "constants.raydistance=raydistance;",
            "the validated distance must reach the sky-visibility constant buffer");
        RequireOrdered(
            pass,
            {
                "if(!m_supported||!commandlist||!worldtlas||",
                "return{};",
                "if(std::isnan(raydistance))",
                "return{};",
                "if(!ensureresources(inputs))",
                "return{};",
                "if(!ensurebindingset(inputs,worldtlas))",
                "return{};",
                "return{m_outputvisibility,true};"
            },
            "all unavailable or invalid producer states must return empty before publishing an output");

        RequireContains(
            shader,
            "[numthreads(8,8,1)]",
            "the producer shader must use an 8x8 thread group");
        RequireContains(
            shader,
            "raytracingaccelerationstructuret_worldbvh:register(t0);",
            "the producer shader must consume the TLAS directly");
        RequireContains(
            shader,
            "texture2d<float>t_depth:register(t1);",
            "the producer shader must consume full-resolution depth");
        RequireContains(
            shader,
            "texture2d<float4>t_gbuffermaterial:register(t2);",
            "the producer shader must consume packed material normals");
        RequireContains(
            shader,
            "texture2d<float4>t_gbuffernormals:register(t3);",
            "the producer shader must consume geometric-normal channels");
        RequireContains(
            shader,
            "rwtexture2d<float>u_visibility:register(u0);",
            "the producer shader must write only scalar visibility");
        RequireContains(
            shader,
            "u_visibility[pixelposition]=1.0f;return;",
            "invalid/background pixels must fail open to white");
        RequireContains(
            shader,
            "constpbrgbuffersurfacenormalssurfacenormals="
                "decodepbrgbuffersurfacenormals(normalchannels,"
                "packedmaterial);",
            "the producer must use the shared geometric-normal decoder");
        Require(
            CountOccurrences(shader, "surfacenormals.geometricnormal") == 2u &&
                CountOccurrences(shader, "surfacenormals.shadingnormal") == 0u,
            "ray orientation and bias must use only the geometric normal");
        RequireOrdered(
            shader,
            {
                "constfloatradialsquared=saturate(sample.x);",
                "constfloatradial=sqrt(radialsquared);",
                "constfloatphi=skyvisibilitytwopi*sample.y;",
                "constfloatnormaldistance=sqrt(max(1.0f-radialsquared,0.0f));",
                "tangent*(cos(phi)*radial)+",
                "bitangent*(sin(phi)*radial)+",
                "geometricnormal*normaldistance"
            },
            "hemisphere directions must use the cosine-weighted radial CDF");
        RequireOrdered(
            shader,
            {
                "float3safenormal=pbrsafenormalize(geometricnormal,"
                    "viewdirection);",
                "if(dot(safenormal,viewdirection)<0.0f)",
                "constfloatsafedepth=skyvisibilitystepdepthtowardcamera(depth);",
                "float3depthstepposition=reconstructworldposition(",
                "constfloatdepthstepdistance=all(isfinite(depthstepposition))",
                "constfloatclearance=max(max(g_skyvisibility.raybias,0.0f),"
                    "depthstepdistance);",
                "surfaceposition+safenormal*clearance,",
                "safenormal);"
            },
            "ray origins must retain reverse-Z-aware geometric-normal clearance");
        RequireContains(
            shader,
            "depth+direction*g_skyvisibility.depthquantizationstep",
            "integer depth formats must advance one quantization step toward the camera");
        RequireContains(
            shader,
            "bits=min(bits+1u,asuint(1.0f));",
            "reverse floating depth must advance one representable value");
        RequireContains(
            shader,
            "asint(position)+(position<0.0f?-integeroffset:integeroffset)",
            "ray origins must retain the signed representable-position offset");
        RequireOrdered(
            shader,
            {
                "boolskyvisibilitytrace(",
                "ray.origin=rayorigin;",
                "ray.direction=direction;",
                "ray.tmin=0.0f;",
                "ray.tmax=g_skyvisibility.raydistance;",
                "ray_flag_accept_first_hit_and_end_search|"
                    "ray_flag_force_opaque|"
                    "ray_flag_skip_procedural_primitives",
                "query.tracerayinline(t_worldbvh,ray_flag_none,0xff,ray);",
                "returnquery.committedstatus()!=committed_triangle_hit;"
            },
            "the producer must retain the proven fail-open opaque first-hit ray query");
        RequireOrdered(
            shader,
            {
                "constuintsamplecount=max(g_skyvisibility.samplecount,1u);",
                "uintvisiblesamplecount=0u;",
                "for(uintsampleindex=0u;sampleindex<samplecount;"
                    "++sampleindex)",
                "skyvisibilitysamplecosinehemisphere(",
                "skyvisibilitysample2d(pixelposition,sampleindex)",
                "if(skyvisibilitytrace(rayorigin,direction))",
                "++visiblesamplecount;",
                "u_visibility[pixelposition]="
                    "float(visiblesamplecount)/float(samplecount);"
            },
            "one binary sky query per sample must be averaged exactly once");
        Require(
            CountOccurrences(
                shader,
                "if(skyvisibilitytrace(rayorigin,direction))") == 1u,
            "the sampling loop must contain exactly one ray query");
        RequireContains(
            shader,
            "skyvisibilitypermutatedwhitenoise(",
            "the producer must retain permutated white noise");
        RequireContains(
            shader,
            "skyvisibilitybluenoise(pixelposition,0u)",
            "the producer must retain prepared void-cluster blue noise");
        Require(
            CountOccurrences(shader, "g_skyvisibility.raybias") == 1u,
            "ray bias must affect origin clearance only");

        std::string producer = settings;
        producer += constants;
        producer += passHeader;
        producer += pass;
        producer += shader;
        for (const std::string_view forbidden : {
                "temporalaccumulation",
                "historytexture",
                "historybuffer",
                "historyvalid",
                "motionvectors",
                "t_motionvectors",
                "denoise",
                "reprojection",
                "reproject",
                "upsample",
                "downsample",
                "halfresolution",
                "quarterresolution",
                "visibilitybitmask",
                "bitmaskvisibility",
                "bentnormal",
                "sphericalharmonic",
                "adaptivesampling",
                "shadowedpixel",
                "receivershadow",
                "directionallight",
                "directionalvisibility",
                "screenspacedirectional",
                "shadowvisibility",
                "reconstructskyvisibility",
                "visibilitycache" })
        {
            RequireAbsent(
                producer,
                forbidden,
                "the producer must remain a standalone current-frame sky query without forbidden history, reconstruction, scheduling, or shadow coupling");
        }

        Require(
            CountOccurrences(
                shaderConfig,
                "ray_traced_sky_visibility_cs.hlsl-tcs-egenerate") == 1u,
            "the shader catalog must package exactly one sky-visibility Generate entry");
    }

    void ValidateRendererIntegration(const std::string& viewer)
    {
        RequireOrdered(
            viewer,
            {
                "voidresetantialiasingstate()",
                "m_antialiasingphase=0u;",
                "m_heitzratioestimatorphase=0u;",
                "m_raytracedskyvisibilityphase=0u;"
            },
            "renderer history invalidation must reset the independent sky phase");
        const std::string_view representationSelection = ExtractSection(
            viewer,
            "constboolheitzratioestimatorselected=",
            "constuint64_tworldrepresentationgenerationbefore=",
            "world-representation consumer selection");
        RequireContains(
            representationSelection,
            "constboolraytracedskyvisibilityselected="
                "m_ui.raytracedskyvisibility.enabled&&"
                "hasraytracedskyvisibilityconsumer("
                    "m_ui.raytracedskyvisibility)&&"
                "supportsraytracedskyvisibility();",
            "sky visibility must select world representation independently");
        RequireContains(
            representationSelection,
            "constboolworldrepresentationselected="
                "heitzratioestimatorselected||"
                "raytracedskyvisibilityselected;",
            "the shared TLAS consumer gate must be Heitz or sky visibility");
        RequireAbsent(
            representationSelection,
            "m_sunlight",
            "sky world-representation selection must not depend on the sun");

        const std::string_view expectedContribution = ExtractSection(
            viewer,
            "constboolraytracedskyvisibilityexpectedtocontribute=",
            "constboolrunscreenspacevisibility=",
            "sky contribution availability gate");
        RequireContains(
            expectedContribution,
            "raytracedskyvisibilityselected&&"
                "m_raytracedskyvisibilitypass&&"
                "m_raytracedskyvisibilitypass->issupported()&&"
                "worldrepresentationready&&"
                "(skyvisibilitydiffuseiblavailable||"
                    "skyvisibilityspeculariblavailable);",
            "sky queries must run only for an available selected IBL consumer");
        RequireContains(
            viewer,
            "constboolskyvisibilitydiffuseiblavailable="
                "m_ui.raytracedskyvisibility.applytodiffuseibl&&"
                "diffuseenvironment&&diffuseenvironmentscale>0.f;",
            "diffuse-only tracing must require a live diffuse environment");
        RequireContains(
            viewer,
            "constboolskyvisibilityspeculariblavailable="
                "m_ui.raytracedskyvisibility.applytospecularibl&&"
                "globalenvironment&&globalenvironment->specularmap&&"
                "globalenvironment->environmentbrdf&&"
                "globalenvironment->specularscale>0.f;",
            "specular-only tracing must require a complete specular environment");
        RequireAbsent(
            expectedContribution,
            "m_sunlight",
            "sky contribution must not depend on directional-light state");
        Require(
            CountOccurrences(
                viewer,
                "hasraytracedskyvisibilityconsumer(") >= 3u,
            "neither mode must suppress pass creation, loading TLAS work, and frame dispatch");

        const std::string_view multisampleGate = ExtractSection(
            viewer,
            "constboolsinglesurfacevisibilityproducerenabled=",
            "constboolsinglesurfaceinputsavailable=",
            "MSAA closest-surface resolve gate");
        RequireContains(
            multisampleGate,
            "m_ui.screenspacedirectionalshadows.enabled||"
                "heitzratioestimatorselected||"
                "raytracedskyvisibilityselected;",
            "MSAA must resolve a single full-resolution surface for sky visibility");
        RequireContains(
            multisampleGate,
            "resolveclosestmsaasurface();",
            "the enabled sky producer must execute the closest-surface resolve");

        const std::string_view dispatch = ExtractSection(
            viewer,
            "if(raytracedskyvisibilityexpectedtocontribute&&",
            "deferredlightingpass::inputsdeferredinputs;",
            "sky dispatch and publication");
        RequireOrdered(
            dispatch,
            {
                "raytracedskyvisibilityinputsskyinputs;",
                "skyinputs.depth=visibilitydepth;",
                "skyinputs.material=closestsurfaceresolved",
                "skyinputs.normals=closestsurfaceresolved",
                "beginrendererstage(renderertimingstage::skyvisibility);",
                "skyvisibilityresult=m_raytracedskyvisibilitypass->render(",
                "m_worldspacerepresentation"
                    "->gettoplevelaccelerationstructure(),",
                "uint32_t(m_raytracedskyvisibilityphase),",
                "m_scenediagonal);",
                "endrendererstage(renderertimingstage::skyvisibility);",
                "skyvisibility=skyvisibilityresult?"
                    "skyvisibilityresult.visibility:nullptr;"
            },
            "the current-frame producer must run after G-buffer resolve and publish only a valid result");
        RequireAbsent(
            dispatch,
            "m_sunlight",
            "the sky dispatch must not bind a directional light");
        RequireAbsent(
            dispatch,
            "directionalvisibilities",
            "the sky dispatch must not bind directional shadow visibility");
        RequireOrdered(
            viewer,
            {
                "skyvisibilityresult=m_raytracedskyvisibilitypass->render(",
                "deferredlightingpass::inputsdeferredinputs;",
                "m_pbrdeferredlightingpass->render("
            },
            "sky visibility must be produced before deferred lighting consumes it");

        const std::string_view phaseCommit = ExtractSection(
            viewer,
            "if(skyvisibilityresult.dispatched&&",
            "if(m_ui.copyscreenshottoclipboard)",
            "sky sampling-phase commit");
        RequireContains(
            phaseCommit,
            "if(skyvisibilityresult.dispatched&&"
                "m_ui.raytracedskyvisibility.animatesamples)"
                "{++m_raytracedskyvisibilityphase;}",
            "phase must advance only after an animated successful dispatch");
        RequireAbsent(
            phaseCommit,
            "temporalaa",
            "sky sample animation must remain independent of TAA");
        Require(
            CountOccurrences(viewer, "++m_raytracedskyvisibilityphase;") == 1u,
            "the renderer must have exactly one sky-phase commit site");
    }

    void ValidateIblConsumers(
        const std::string& deferredShader,
        const std::string& deferredMsaaShader,
        const std::string& deferredPass,
        const std::string& deferredPassHeader,
        const std::string& deferredConstants,
        const std::string& compositeShader,
        const std::string& visibilityPass,
        const std::string& visibilityPassHeader,
        const std::string& visibilityConstants)
    {
        for (const std::string* shader :
            { &deferredShader, &deferredMsaaShader })
        {
            RequireContains(
                *shader,
                "texture2d<float>t_skyvisibility:register(t22);",
                "each deferred shader must bind sky visibility at t22");
            RequireOrdered(
                *shader,
                {
                    "constboolapplyskyvisibilitytodiffuseibl=",
                    "constboolapplyskyvisibilitytospecularibl=",
                    "if((needdiffuseenvironment&&"
                        "applyskyvisibilitytodiffuseibl)||",
                    "constfloatsampledskyvisibility="
                        "t_skyvisibility[pixelposition];",
                    "skyvisibility=isfinite(sampledskyvisibility)?"
                        "saturate(sampledskyvisibility):1.0f;",
                    "environmentdiffuse=evaluatepbrenvironmentdiffuse(",
                    "if(applyskyvisibilitytodiffuseibl)",
                    "environmentdiffuse*=skyvisibility;",
                    "environmentspecular=evaluatepbrenvironmentspecular(",
                    "if(applyskyvisibilitytospecularibl)",
                    "environmentspecular*=skyvisibility;"
                },
                "each deferred shader must load once and independently modulate both IBL lobes");
            Require(
                CountOccurrences(*shader, "t_skyvisibility[") == 1u &&
                    CountOccurrences(*shader, "environmentdiffuse*=") == 1u &&
                    CountOccurrences(*shader, "environmentspecular*=") == 1u,
                "each deferred shader must contain one guarded sky load and one modulation site per IBL lobe");
        }

        RequireOrdered(
            deferredShader,
            {
                "environmentdiffuse*=skyvisibility;",
                "float3sourceradiance=max("
                    "directdiffuse+environmentdiffuse,0.0f);",
                "float3diffuse=directdiffuse+",
                "float3finallinearhdr=max(diffuse+specular+"
                    "gbuffer.material.emissive,0.0f);"
            },
            "sky-modulated diffuse IBL must feed source radiance and the final single-sample image");
        RequireOrdered(
            deferredMsaaShader,
            {
                "environmentdiffuse*=skyvisibility;",
                "float3diffuse=directdiffuse+",
                "finallinearhdr=max(diffuse+specular+"
                    "gbuffer.material.emissive,0.0f);"
            },
            "sky-modulated diffuse IBL must feed the final per-sample MSAA image");

        const std::string_view sourceRadianceSection = ExtractSection(
            deferredShader,
            "float3sourceradiance=max(",
            "#endif",
            "diffuse GI source-radiance section");
        RequireContains(
            sourceRadianceSection,
            "directdiffuse+environmentdiffuse",
            "diffuse sky visibility must reach GI source radiance");
        RequireAbsent(
            sourceRadianceSection,
            "environmentspecular",
            "specular sky visibility must never enter GI source radiance");
        const std::string_view directSection = ExtractSection(
            deferredShader,
            "float3directdiffuse=0.0f;",
            "#ifwrite_source_radiance",
            "single-sample direct-light section");
        RequireAbsent(
            directSection,
            "skyvisibility",
            "sky visibility must not affect direct lighting or direct shadows");
        const std::string_view directMsaaSection = ExtractSection(
            deferredMsaaShader,
            "float3directdiffuse=0.0f;",
            "float3diffuse=directdiffuse+",
            "MSAA direct-light section");
        RequireAbsent(
            directMsaaSection,
            "skyvisibility",
            "sky visibility must not affect MSAA direct lighting or direct shadows");

        RequireContains(
            compositeShader,
            "texture2d<float>t_skyvisibility:register(t12);",
            "the Visibility composite must bind sky visibility at t12");
        RequireOrdered(
            compositeShader,
            {
                "constboolapplyskyvisibilitytodiffuseibl=",
                "constboolapplyskyvisibilitytospecularibl=",
                "if(applyskyvisibilitytodiffuseibl||"
                    "applyskyvisibilitytospecularibl)",
                "constfloatsampledskyvisibility=t_skyvisibility[pixel];",
                "float3environmentdiffuse="
                    "evaluatepbrenvironmentdiffuse(",
                "if(applyskyvisibilitytodiffuseibl)",
                "environmentdiffuse*=skyvisibility;",
                "float3environmentspecular=0.0f;",
                "environmentspecular=evaluatepbrenvironmentspecular(",
                "if(applyskyvisibilitytospecularibl)",
                "environmentspecular*=skyvisibility;",
                "float3screenspaceindirect=",
                "composescreenspaceindirectlighting("
            },
            "the Visibility composite must modulate recomputed diffuse IBL before composition");
        Require(
            CountOccurrences(compositeShader, "t_skyvisibility[") == 1u &&
                CountOccurrences(compositeShader, "environmentdiffuse*=") == 1u &&
                CountOccurrences(compositeShader, "environmentspecular*=") == 1u,
            "the Visibility composite must contain one guarded sky load and one modulation site per IBL lobe");
        const std::string_view indirectSection = ExtractSection(
            compositeShader,
            "float3screenspaceindirect=",
            "u_output[pixel]=",
            "traced-indirect composition section");
        RequireAbsent(
            indirectSection,
            "skyvisibility",
            "sky visibility must not modulate traced indirect lighting");

        const size_t modulationCount =
            CountOccurrences(deferredShader, "environmentdiffuse*=") +
            CountOccurrences(deferredMsaaShader, "environmentdiffuse*=") +
            CountOccurrences(compositeShader, "environmentdiffuse*=") +
            CountOccurrences(deferredShader, "environmentspecular*=") +
            CountOccurrences(deferredMsaaShader, "environmentspecular*=") +
            CountOccurrences(compositeShader, "environmentspecular*=");
        Require(
            modulationCount == 6u,
            "the feature must have exactly one modulation site per IBL lobe and shader topology");

        Require(
            CountOccurrences(
                deferredPass,
                "bindinglayoutitem::texture_srv(22)") == 2u,
            "normal and MSAA deferred layouts must each bind t22");
        RequireContains(
            deferredPassHeader,
            "constdonut::engine::lightprobe*environment,"
                "nvrhi::itexture*skyvisibility,"
                "boolapplyskyvisibilitytodiffuseibl,"
                "boolapplyskyvisibilitytospecularibl,"
                "nvrhi::itexture*sourceradianceoutput,",
            "deferred lighting must accept sky visibility independently of directional visibility");
        RequireContains(
            deferredPass,
            "constboolhasskyvisibilityconsumer="
                "applyskyvisibilitytodiffuseibl||"
                "applyskyvisibilitytospecularibl;",
            "deferred lighting must accept either or both IBL consumers");
        RequireContains(
            deferredPass,
            "nvrhi::itexture*activeskyvisibility="
                "hasskyvisibilityconsumer&&"
                "isskyvisibilitytexturecompatible(skyvisibility,inputs)?"
                "skyvisibility:nullptr;",
            "deferred lighting must reject incompatible or stale sky textures");
        for (const std::string_view compatibility : {
                "texturedesc.width==outputdesc.width",
                "texturedesc.height==outputdesc.height",
                "texturedesc.samplecount==1u",
                "texturedesc.format==nvrhi::format::r8_unorm",
                "texturedesc.dimension==nvrhi::texturedimension::texture2d",
                "texturedesc.isshaderresource" })
        {
            RequireContains(
                deferredPass,
                compatibility,
                "deferred sky texture compatibility must be full-resolution single-sample R8 SRV");
        }
        RequireContains(
            deferredPass,
            "constants.skyvisibilityapplication=activeskyvisibility?",
            "deferred application mode must be neutral before binding fallback white");
        for (const std::string_view application : {
                "uvsr_sky_visibility_apply_neither",
                "uvsr_sky_visibility_apply_diffuse_ibl",
                "uvsr_sky_visibility_apply_specular_ibl",
                "uvsr_sky_visibility_apply_both_ibl" })
        {
            RequireContains(
                deferredPass,
                application,
                "deferred lighting must encode all four sky-visibility application modes");
        }
        RequireContains(
            deferredPass,
            "nvrhi::bindingsetitem::texture_srv(22,"
                "activeskyvisibility?activeskyvisibility:"
                "m_commonpasses->m_whitetexture.get())",
            "deferred lighting must bind white when sky visibility is unavailable");
        RequireContains(
            deferredConstants,
            "uintskyvisibilityapplication;",
            "the deferred constant block must carry the four-state application mode");

        RequireContains(
            visibilityPassHeader,
            "nvrhi::itexture*skyvisibility=nullptr;"
                "boolapplyskyvisibilitytodiffuseibl=false;"
                "boolapplyskyvisibilitytospecularibl=false;",
            "the Visibility composite input set must carry sky visibility separately");
        RequireContains(
            visibilityPass,
            "constboolhasskyvisibilityconsumer="
                "inputs.applyskyvisibilitytodiffuseibl||"
                "inputs.applyskyvisibilitytospecularibl;",
            "the Visibility composite must accept either or both IBL consumers");
        RequireContains(
            visibilityPass,
            "nvrhi::itexture*activeskyvisibility="
                "hasskyvisibilityconsumer&&"
                "isskyvisibilitytexturecompatible(inputs.skyvisibility,"
                "fullsize)?inputs.skyvisibility:nullptr;",
            "the Visibility composite must reject incompatible or stale sky textures");
        for (const std::string_view compatibility : {
                "description.width==fullsize.x",
                "description.height==fullsize.y",
                "description.samplecount==1u",
                "description.format==nvrhi::format::r8_unorm",
                "description.dimension==nvrhi::texturedimension::texture2d",
                "description.isshaderresource" })
        {
            RequireContains(
                visibilityPass,
                compatibility,
                "Visibility sky texture compatibility must be full-resolution single-sample R8 SRV");
        }
        Require(
            CountOccurrences(
                visibilityPass,
                "bindinglayoutitem::texture_srv(12)") == 1u,
            "the Visibility composite layout must bind t12 exactly once");
        RequireContains(
            visibilityPass,
            "constants.skyvisibilityapplication=activeskyvisibility?",
            "Visibility application mode must be neutral before binding fallback white");
        RequireContains(
            visibilityPass,
            "nvrhi::bindingsetitem::texture_srv(12,"
                "activeskyvisibility?activeskyvisibility:"
                "m_commonpasses->m_whitetexture.get())",
            "the Visibility composite must bind white when sky visibility is unavailable");
        RequireContains(
            visibilityConstants,
            "uintskyvisibilityapplication;",
            "the Visibility constant block must carry the four-state application mode");
    }

    void ValidateSkyUiAndCommands(
        const std::string& viewer,
        const std::string& catalog)
    {
        const std::string_view skyDrawer = ExtractSection(
            viewer,
            "constboolskyopen=drawcollapsingheader(",
            "constboollightsopen=drawcollapsingheader(",
            "Sky drawer");
        RequireOrdered(
            skyDrawer,
            {
                "\"showenvironmentbackground\"",
                "imgui::separatortext(\"ray-tracedskyvisibility\");",
                "imgui::checkbox(\"enable##raytracedskyvisibility\"",
                "beginanimatedtoggleregion("
                    "\"##raytracedskyvisibilitycontrols\","
                    "skyvisibility.enabled&&skyvisibilityavailable)",
                "\"diffuseibl##raytracedskyvisibility\"",
                "\"specularibl##raytracedskyvisibility\"",
                "\"samplesperpixel##raytracedskyvisibility\"",
                "\"noisepattern##raytracedskyvisibility\"",
                "\"animatesamples##raytracedskyvisibility\"",
                "\"maxdistance##raytracedskyvisibility\"",
                "\"raybias##raytracedskyvisibility\"",
                "endanimatedtoggleregion();",
                "enddrawerbody();"
            },
            "the collapsed sky-visibility controls must be the final Sky drawer group in shadow-like order");

        const std::string_view featureGroup = ExtractSection(
            skyDrawer,
            "imgui::separatortext(\"ray-tracedskyvisibility\");",
            "enddrawerbody();",
            "sky-visibility UI group");
        Require(
            CountOccurrences(featureGroup, "beginanimatedtoggleregion(") == 1u &&
                CountOccurrences(featureGroup, "endanimatedtoggleregion();") == 1u,
            "the dependent sky controls must use one balanced collapsed region");
        const std::string_view enableOnly = ExtractSection(
            featureGroup,
            "imgui::separatortext(\"ray-tracedskyvisibility\");",
            "beginanimatedtoggleregion(",
            "always-visible sky enable row");
        RequireContains(
            enableOnly,
            "imgui::checkbox(\"enable##raytracedskyvisibility\"",
            "Enable must remain visible while the dependent region is collapsed");
        for (const std::string_view hiddenControl : {
                "diffuseibl##raytracedskyvisibility",
                "specularibl##raytracedskyvisibility",
                "samplesperpixel##raytracedskyvisibility",
                "noisepattern##raytracedskyvisibility",
                "animatesamples##raytracedskyvisibility",
                "maxdistance##raytracedskyvisibility",
                "raybias##raytracedskyvisibility" })
        {
            RequireAbsent(
                enableOnly,
                hiddenControl,
                "only Enable may appear outside the collapsed dependent region");
        }
        RequireContains(
            featureGroup,
            "constbooldisableskyvisibilityenable="
                "!skyvisibilityavailable&&!skyvisibility.enabled;",
            "unsupported hardware must visibly disable initial activation");

        for (const std::string_view command : {
                "sky.visibility.enabled",
                "sky.visibility.diffuse-ibl",
                "sky.visibility.specular-ibl",
                "sky.visibility.samples-per-pixel",
                "sky.visibility.noise-pattern",
                "sky.visibility.animate-samples",
                "sky.visibility.max-distance",
                "sky.visibility.ray-bias" })
        {
            Require(
                CountOccurrences(catalog, command) == 1u,
                "each sky-visibility command must appear exactly once in the catalog");
        }
        RequireOrdered(
            catalog,
            {
                "value(\"sky.visibility.enabled\",kind::boolean,"
                    "section::sky,\"on|off\")",
                "value(\"sky.visibility.diffuse-ibl\",kind::boolean,"
                    "section::sky,\"on|off\")",
                "value(\"sky.visibility.specular-ibl\",kind::boolean,"
                    "section::sky,\"on|off\")",
                "value(\"sky.visibility.samples-per-pixel\",kind::enum,"
                    "section::sky,\"1|2|4|8|16|32|64\")",
                "value(\"sky.visibility.noise-pattern\",kind::enum,"
                    "section::sky,\"permutated-white-noise|"
                    "void-cluster-blue-noise\")",
                "value(\"sky.visibility.animate-samples\",kind::boolean,"
                    "section::sky,\"on|off\")",
                "value(\"sky.visibility.max-distance\",kind::enum,"
                    "section::sky,\"max|32m|16m|8m|4m|2m\")",
                "value(\"sky.visibility.ray-bias\",kind::float,"
                    "section::sky,\"worldunits0..0.1\")"
            },
            "the Sky command catalog must expose all eight settings in UI order");

        const std::string_view dispatcher = ExtractSection(
            viewer,
            "if(path==\"sky.visibility.enabled\"||",
            "boolresetvisibilityhistory=false;",
            "sky-visibility command dispatcher");
        RequireOrdered(
            dispatcher,
            {
                "path==\"sky.visibility.enabled\"",
                "path==\"sky.visibility.diffuse-ibl\"",
                "candidate.applytodiffuseibl,",
                "path==\"sky.visibility.specular-ibl\"",
                "candidate.applytospecularibl,",
                "path==\"sky.visibility.samples-per-pixel\"",
                "{\"1\",0},{\"2\",1},{\"4\",2},{\"8\",3},"
                    "{\"16\",4},{\"32\",5},{\"64\",6}",
                "path==\"sky.visibility.noise-pattern\"",
                "{\"permutated-white-noise\","
                    "raytracedskyvisibilitynoisepattern::"
                    "permutatedwhitenoise}",
                "{\"void-cluster-blue-noise\","
                    "raytracedskyvisibilitynoisepattern::"
                    "voidclusterbluenoise}",
                "path==\"sky.visibility.animate-samples\"",
                "path==\"sky.visibility.max-distance\"",
                "{\"max\",rayvisibilitymaxdistance::maximum}",
                "{\"32m\",rayvisibilitymaxdistance::meters32}",
                "{\"2m\",rayvisibilitymaxdistance::meters2}",
                "candidate.raybias,",
                "raytracedskyvisibilitymaximumraybias,",
                "israytracedskyvisibilityconfigurationsupported(candidate)",
                "candidate.enabled&&!m_app->supportsraytracedskyvisibility()",
                "m_ui.raytracedskyvisibility=candidate;",
                "m_app->resetimagebasedlightinghistory();"
            },
            "Sky commands must validate, round-trip, and reset the independent sample phase");
    }
}

int main(int argc, char** argv)
{
    Require(argc == 2,
        "expected the UVSR source directory as the only argument");
    const std::filesystem::path root = argv[1];

    const std::string settings = Canonicalize(ReadSource(
        root / "src/ray_traced_sky_visibility_settings.h"));
    const std::string constants = Canonicalize(ReadSource(
        root / "src/ray_traced_sky_visibility_cb.h"));
    const std::string passHeader = Canonicalize(ReadSource(
        root / "src/ray_traced_sky_visibility.h"));
    const std::string pass = Canonicalize(ReadSource(
        root / "src/ray_traced_sky_visibility.cpp"));
    const std::string shader = Canonicalize(ReadSource(
        root / "src/ray_traced_sky_visibility_cs.hlsl"));
    const std::string shaderConfig = Canonicalize(ReadSource(
        root / "src/shaders.cfg"));
    const std::string viewer = Canonicalize(ReadSource(
        root / "src/uvsr.cpp"));
    const std::string deferredShader = Canonicalize(ReadSource(
        root / "src/pbr_deferred_lighting_cs.hlsl"));
    const std::string deferredMsaaShader = Canonicalize(ReadSource(
        root / "src/pbr_deferred_lighting_msaa_cs.hlsl"));
    const std::string deferredPass = Canonicalize(ReadSource(
        root / "src/pbr_deferred_lighting_pass.cpp"));
    const std::string deferredPassHeader = Canonicalize(ReadSource(
        root / "src/pbr_deferred_lighting_pass.h"));
    const std::string deferredConstants = Canonicalize(ReadSource(
        root / "src/pbr_deferred_lighting_cb.h"));
    const std::string compositeShader = Canonicalize(ReadSource(
        root / "src/screen_space_indirect_composite_cs.hlsl"));
    const std::string visibilityPass = Canonicalize(ReadSource(
        root / "src/screen_space_visibility.cpp"));
    const std::string visibilityPassHeader = Canonicalize(ReadSource(
        root / "src/screen_space_visibility.h"));
    const std::string visibilityConstants = Canonicalize(ReadSource(
        root / "src/screen_space_visibility_cb.h"));
    const std::string catalog = Canonicalize(ReadSource(
        root / "src/ui_settings_command_catalog.h"));

    ValidateSettingsAndProducer(
        settings,
        constants,
        passHeader,
        pass,
        shader,
        shaderConfig);
    ValidateRendererIntegration(viewer);
    ValidateIblConsumers(
        deferredShader,
        deferredMsaaShader,
        deferredPass,
        deferredPassHeader,
        deferredConstants,
        compositeShader,
        visibilityPass,
        visibilityPassHeader,
        visibilityConstants);
    ValidateSkyUiAndCommands(viewer, catalog);

    std::cout << "Ray-traced sky-visibility source contracts passed.\n";
    return 0;
}
