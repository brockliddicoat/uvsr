#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
    int g_Failures = 0;

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }

    std::string Compact(std::string_view source)
    {
        std::string result;
        result.reserve(source.size());
        for (const char character : source)
        {
            if (character != ' ' && character != '\t' &&
                character != '\r' && character != '\n')
            {
                result.push_back(character);
            }
        }
        return result;
    }

    void RequireContains(
        std::string_view source,
        std::string_view required,
        std::string_view contract)
    {
        if (source.find(required) != std::string_view::npos)
            return;
        std::cerr << "FAIL: " << contract << " must contain '"
                  << required << "'.\n";
        ++g_Failures;
    }

    void RequireAbsent(
        std::string_view source,
        std::string_view forbidden,
        std::string_view contract)
    {
        if (source.find(forbidden) == std::string_view::npos)
            return;
        std::cerr << "FAIL: " << contract << " must not contain '"
                  << forbidden << "'.\n";
        ++g_Failures;
    }

    void RequireOrdered(
        std::string_view source,
        std::initializer_list<std::string_view> values,
        std::string_view contract)
    {
        size_t cursor = 0u;
        for (const std::string_view value : values)
        {
            const size_t position = source.find(value, cursor);
            if (position == std::string_view::npos)
            {
                std::cerr << "FAIL: " << contract
                          << " is missing ordered value '" << value
                          << "'.\n";
                ++g_Failures;
                return;
            }
            cursor = position + value.size();
        }
    }

    size_t CountOccurrences(
        std::string_view source,
        std::string_view value)
    {
        size_t count = 0u;
        size_t cursor = 0u;
        while ((cursor = source.find(value, cursor)) !=
            std::string_view::npos)
        {
            ++count;
            cursor += value.size();
        }
        return count;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: heitz source contract <source-root>\n";
        return 2;
    }

    const std::filesystem::path root = argv[1];
    const std::string shader = Compact(ReadFile(
        root / "src/heitz_ratio_estimator_shadows_cs.hlsl"));
    const std::string pass = Compact(ReadFile(
        root / "src/heitz_ratio_estimator_shadows.cpp"));
    const std::string ratioHelper = Compact(ReadFile(
        root / "src/ratio_estimator_shared.h"));
    const std::string viewer = Compact(ReadFile(root / "src/uvsr.cpp"));
    const std::string passHeader = Compact(ReadFile(
        root / "src/heitz_ratio_estimator_shadows.h"));
    const std::string constants = Compact(ReadFile(
        root / "src/heitz_ratio_estimator_shadows_cb.h"));
    const std::string settings = Compact(ReadFile(
        root / "src/directional_shadow_settings.h"));
    const std::string pbrGBuffer = Compact(ReadFile(
        root / "src/pbr_gbuffer.hlsli"));
    const std::string pbrGBufferPixel = Compact(ReadFile(
        root / "src/pbr_gbuffer_ps.hlsl"));
    const std::string shaderConfig = Compact(ReadFile(
        root / "src/shaders.cfg"));
    const std::string materialVisibility = Compact(ReadFile(
        root / "src/ray_traced_material_visibility.hlsli"));
    const std::string materialVisibilityHeader = Compact(ReadFile(
        root / "src/ray_traced_material_visibility.h"));

    RequireOrdered(
        shader,
        {
            "constfloat3directionToLight=HeitzSampleDirectionalEmitter(",
            "constfloat3contribution=HeitzEvaluateNormalizedResponse(",
            "currentDenominator+=contribution;",
            "if(HeitzTraceVisibility(",
            "currentNumerator+=contribution;",
            "constfloatinverseSampleCount=rcp(float(sampleCount));",
            "constfloat3numeratorMean=currentNumerator*inverseSampleCount;",
            "constfloat3denominatorMean=currentDenominator*inverseSampleCount;",
            "constfloat3modulation=saturate(ResolveCorrelatedRatio(",
            "numeratorMean,",
            "denominatorMean,",
            "u_Output[pixelPosition]=float4(modulation,1.0f);"
        },
        "matched current-frame accumulation before guarded division");
    RequireAbsent(
        shader,
        "RAY_FLAG_FORCE_OPAQUE",
        "alpha-tested triangle traversal");
    RequireOrdered(
        shader,
        {
            "#ifOUTPUT_HIT_DISTANCERayQuery<"
                "RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>query;",
            "#elseRayQuery<"
                "RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|"
                "RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>query;",
            "while(query.Proceed()){"
                "UVSR_COMMIT_COVERED_RAY_QUERY_CANDIDATE(query)}",
            "query.CommittedRayT()"
        },
        "the hit permutation must retain the closest covered blocker while the raw path keeps ACCEPT_FIRST");
    RequireContains(
        shader,
        "#include\"ray_traced_material_visibility.hlsli\"",
        "shared alpha-tested material traversal helper");
    RequireOrdered(
        materialVisibility,
        {
            "constuintglobalGeometryIndex=t_RayGeometryIndexMap["
                "geometryMapOffset+compactGeometryIndex];",
            "constGeometryDatageometry="
                "t_RayMaterialGeometries[globalGeometryIndex];",
            "constMaterialConstantsmaterial="
                "t_RayMaterials[geometry.materialIndex];",
            "if(material.domain!=MaterialDomain_AlphaTested)returnfalse;",
            "floatopacity=material.opacity;",
            "MaterialFlags_UseOpacityTexture",
            "NonUniformResourceIndex(material.opacityTextureIndex)",
            "opacity*=opacityTexture.SampleLevel(",
            ").r;",
            "elseif((material.flags&"
                "MaterialFlags_UseBaseOrDiffuseTexture)!=0",
            "NonUniformResourceIndex(material.baseOrDiffuseTextureIndex)",
            "opacity*=baseTexture.SampleLevel(",
            ").a;",
            "returnsaturate(opacity)>=material.alphaCutoff;"
        },
        "explicit opacity texture precedence before base alpha cutoff");
    for (const std::string_view candidateContract : {
            "CandidateInstanceContributionToHitGroupIndex()",
            "CandidateGeometryIndex()",
            "CandidatePrimitiveIndex()",
            "CandidateTriangleBarycentrics()",
            "CommitNonOpaqueTriangleHit();" })
    {
        RequireContains(
            materialVisibility,
            candidateContract,
            "complete nonopaque triangle candidate evaluation");
    }
    RequireContains(
        shader,
        "u_Output[pixelPosition]=1.0f;",
        "background and invalid surfaces fail open to neutral white");

    RequireOrdered(
        shader,
        {
            "float3safeNormal=PbrSafeNormalize(geometricNormal,viewDirection);",
            "if(dot(safeNormal,viewDirection)<0.0f)",
            "constfloatsafeDepth=HeitzStepDepthTowardCamera(depth);",
            "float3depthStepPosition=ReconstructWorldPosition(",
            "constfloatdepthStepDistance=all(isfinite(depthStepPosition))",
            "constfloatclearance=max(max(g_Heitz.rayBias,0.0f),depthStepDistance);",
            "surfacePosition+safeNormal*clearance,",
            "safeNormal);"
        },
        "triangle-normal world-space ray-origin clearance");
    RequireOrdered(
        shader,
        {
            "boolHeitzTraceVisibility(",
            "ray.Origin=rayOrigin;",
            "ray.TMin=0.0f;",
            "ray.TMax=g_Heitz.rayDistance;"
        },
        "single origin bias with unshortened far reach");
    RequireContains(
        shader,
        "depth+direction*g_Heitz.depthQuantizationStep",
        "one internal D16 or D24 depth-quantization step");
    RequireContains(
        shader,
        "bits=min(bits+1u,asuint(1.0f));",
        "one internal reverse floating-point depth step");
    RequireContains(
        shader,
        "asint(position)+(position<0.0f?-integerOffset:integerOffset)",
        "signed representable-position offset");
    if (CountOccurrences(shader, "g_Heitz.rayBias") != 1u)
    {
        std::cerr << "FAIL: Ray Bias must affect only origin clearance.\n";
        ++g_Failures;
    }

    RequireOrdered(
        pbrGBufferPixel,
        {
            "MaterialSamplesurface=EvaluateSceneMaterial(",
            "float3triangleNormal=PbrSafeNormalize(",
            "cross(ddx(i_vtx.pos),ddy(i_vtx.pos)),",
            "if(dot(triangleNormal,viewDirection)<0.0f)",
            "if(dot(surface.shadingNormal,triangleNormal)<0.0f)",
            "clip(surface.opacity-g_Material.alphaCutoff);",
            "pbrData.shadingNormal=surface.shadingNormal;",
            "pbrData.geometricNormal=triangleNormal;"
        },
        "true raster triangle normal and distinct shading normal contract");
    RequireOrdered(
        pbrGBuffer,
        {
            "PbrGBufferSurfaceNormalsDecodePbrGBufferSurfaceNormals(",
            "result.geometricNormal=DecodeOctahedralNormal(",
            "result.shadingNormal=PbrSafeNormalize(",
            "if(dot(result.shadingNormal,result.geometricNormal)<0.0f)",
            "returnresult;"
        },
        "shared minimal G-buffer surface decoder");

    RequireOrdered(
        shader,
        {
            "if(g_Heitz.hardShadows!=0u||"
                "g_Heitz.useRatioEstimator==0u){",
            "g_Heitz.hardShadows!=0u?HeitzLightCenterDirection():"
                "HeitzSampleDirectionalEmitter(",
            "if(!CanEvaluatePbrDirectSurfacePrepared(",
            "u_Output[pixelPosition]=1.0f;",
            "constfloat3rayOrigin=HeitzPrepareRayOrigin(",
            "constfloatvisible=float(HeitzTraceVisibility(",
            "u_Output[pixelPosition]=float4(visible.xxx,1.0f);",
            "channels[0]=t_GBufferDiffuse[pixelPosition];",
            "constPbrPreparedMaterialpreparedMaterial="
        },
        "receiver gated hard or one ray scalar branch before ratio material work");
    RequireContains(
        shader,
        "constPbrGBufferSurfaceNormalssurfaceNormals="
            "DecodePbrGBufferSurfaceNormals(normalChannels,packedMaterial);",
        "shared minimal hard-shadow G-buffer surface decoder");
    RequireAbsent(
        shader,
        "DecodeOctahedralNormal(",
        "hard-shadow-local G-buffer normal decoding");

    RequireOrdered(
        shader,
        {
            "constuintphase=g_Heitz.sampleSequencePhase;",
            "constuintfirstDimension=sampleIndex*2u;",
            "constuintsequenceIndex=sampleIndex+1u;",
            "constfloat2noiseShift=float2(",
            "UVSRSamplePrecomputedNoise(t_Noise,g_Heitz.noisePattern,"
                "dispatchPosition,dispatchExtent,phase,"
                "0x200u+firstDimension)",
            "UVSRSamplePrecomputedNoise(t_Noise,g_Heitz.noisePattern,"
                "dispatchPosition,dispatchExtent,phase,"
                "0x200u+firstDimension+1u)",
            "HeitzRadicalInverse(sequenceIndex,2u),",
            "HeitzRadicalInverse(sequenceIndex,3u))",
            "+noiseShift)"
        },
        "progressive low-discrepancy sequence shifted by shared precomputed noise");
    RequireContains(
        shader,
        "floatcosTheta=lerp(1.0f,cosMaximum,sample.x);",
        "uniform spherical-cap radial CDF mapping");
    for (const std::string_view removed : {
            "HashedWhiteNoise",
            "HeitzHashedWhiteNoise",
            "samplePeriod",
            "dutyPhase",
            "HeitzDutyPixelSelected",
            "HeitzTemporalHistory",
            "HistoryNumerator",
            "HistoryDenominator",
            "temporalAccumulation",
            "historyValid",
            "currentToPreviousJitter",
            "t_MotionVectors",
            "temporal_aa_common.hlsli" })
    {
        RequireAbsent(shader, removed, "retired fractional/private-history shader state");
        RequireAbsent(pass, removed, "retired fractional/private-history pass state");
        RequireAbsent(passHeader, removed, "retired fractional/private-history header state");
        RequireAbsent(constants, removed, "retired fractional/private-history constants");
    }
    RequireContains(
        pass,
        "nvrhi::BindingLayoutItem::Texture_SRV(7),"
            "nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),"
            "nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),"
            "nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),"
            "nvrhi::BindingLayoutItem::Sampler(0),"
            "nvrhi::BindingLayoutItem::Texture_UAV(0)",
        "material-aware base output binding layout");
    RequireContains(
        pass,
        "pipelineDescription.bindingLayouts={"
            "m_BindingLayouts[variant],m_BindlessLayout};",
        "bindless material texture pipeline layout");
    RequireContains(
        pass,
        "m_BoundMaterialVisibility!=materialVisibility",
        "material resource identity binding-cache invalidation");
    RequireContains(
        pass,
        "state.bindings={m_BindingSets[variant],"
            "materialVisibility.descriptorTable};",
        "live descriptor table dispatch binding");
    RequireContains(
        materialVisibilityHeader,
        "geometryBuffer==other.geometryBuffer&&"
            "materialBuffer==other.materialBuffer&&"
            "geometryIndexMap==other.geometryIndexMap&&"
            "descriptorTable==other.descriptorTable;",
        "all material visibility resources participate in cache identity");
    RequireContains(
        passHeader,
        "std::array<nvrhi::BindingSetHandle,2>m_BindingSets;",
        "independent hit and no hit frame local binding sets");
    RequireContains(
        pass,
        "nvrhi::BindingLayoutItem::Texture_UAV(1)",
        "the hit permutation must bind its optional R16 output");
    RequireAbsent(
        passHeader,
        "ResetHistory",
        "private shadow-history API");

    RequireContains(
        pass,
        "\"uvsr/heitz_ratio_estimator_shadows_cs.hlsl\",\"Generate\"",
        "single Generate pipeline");
    if (CountOccurrences(pass, "commandList->dispatch(") != 1u)
    {
        std::cerr << "FAIL: the Heitz pass must issue exactly one dispatch.\n";
        ++g_Failures;
    }
    RequireContains(
        pass,
        "texture->getDesc().sampleCount!=1u",
        "single-surface Heitz input contract");
    RequireContains(
        pass,
        "HasFormatSupport(device,nvrhi::Format::RGBA16_FLOAT,true)&&"
            "HasFormatSupport(device,nvrhi::Format::R8_UNORM,false)",
        "minimal output and shared R8 noise format support gate");
    for (const std::string_view removed : {
            "RawUnshadowed",
            "RawShadowed",
            "NoiseEstimate",
            "FilteredNoise",
            "HorizontalUnshadowed",
            "HorizontalShadowed",
            "EstimateNoise",
            "DenoiseNoise",
            "FilterHorizontal",
            "FilterVerticalResolve" })
    {
        RequireAbsent(pass, removed, "removed spatial-denoiser pass state");
        RequireAbsent(shader, removed, "removed spatial-denoiser shader state");
        RequireAbsent(passHeader, removed, "removed spatial-denoiser header state");
    }

    RequireContains(
        settings,
        "boolhardShadows=false;booluseRatioEstimator=true;"
            "booloutputHitDistance=false;int32_tsampleRateLog2=1;"
            "floatrayBias=0.002f;",
        "soft ratio, disabled hit output, integer rate, and low geometric bias defaults");
    RequireContains(
        settings,
        "returnstochastic&&settings.useRatioEstimator?"
            "ResolveHeitzRatioEstimatorSampleCount("
            "settings.sampleRateLog2):1u;",
        "disabling the ratio estimator must select one stochastic sun ray");
    RequireContains(
        constants,
        "uintuseRatioEstimator;",
        "the shader constants must select ratio or matched scalar output");
    RequireContains(
        pass,
        "nvrhi::Format::R16_FLOAT,"
            "\"RayTracedSunShadows/HitDistance\"",
        "requested hit output must allocate R16_FLOAT storage");
    RequireContains(
        shader,
        "RWTexture2D<float>u_HitDistance:register(u1);",
        "the hit permutation must expose a scalar distance UAV");
    RequireContains(
        shader,
        "u_HitDistance[pixelPosition]=tracedRayCount!=0u?"
            "nearestHitDistance:0.0f;",
        "ratio output must distinguish invalid from miss and nearest blocker");
    RequireContains(
        settings,
        "RayVisibilityMaxDistancemaxDistance="
            "RayVisibilityMaxDistance::Maximum;",
        "ratio-estimator shadows must default to the established Max reach");
    RequireContains(
        settings,
        "IsRayVisibilityMaxDistanceSupported(settings.maxDistance)",
        "ratio-estimator settings must reject unsupported max-distance modes");
    RequireContains(
        pass,
        "constfloatrayDistance=ResolveRayVisibilityMaxDistance("
            "settings.maxDistance,sceneDiagonal);",
        "the Heitz pass must resolve the selected Max or finite TMax");
    RequireContains(
        pass,
        "if(std::isnan(rayDistance))",
        "an invalid scene extent must fail open before DXR receives a NaN TMax");
    RequireContains(
        pass,
        "constants.rayDistance=rayDistance;",
        "the validated distance must reach the Heitz constant buffer");
    RequireContains(
        settings,
        "\"1\",\"2\",\"4\",\"8\",\"16\",\"32\",\"64\"",
        "complete integer one-through-64 sample-rate domain");
    RequireAbsent(settings, "\"1/", "fractional sample-rate labels");
    RequireAbsent(settings, "HashedWhiteNoise", "retired RT hashed-noise option");
    RequireAbsent(settings, "RequiresPrivateHistory", "private history policy");
    RequireAbsent(settings, "ResolveHeitzRatioEstimatorSamplePeriod", "fractional period policy");
    if (CountOccurrences(
            shaderConfig,
            "heitz_ratio_estimator_shadows_cs.hlsl-Tcs-EGenerate") != 1u)
    {
        std::cerr << "FAIL: the shader catalog must package one complete Heitz Generate permutation axis.\n";
        ++g_Failures;
    }
    RequireContains(
        shaderConfig,
        "heitz_ratio_estimator_shadows_cs.hlsl-Tcs-EGenerate"
            "-DOUTPUT_HIT_DISTANCE={0,1}",
        "base and optional closest hit Heitz shader permutations");
    RequireAbsent(
        ratioHelper,
        "saturate(",
        "generic correlated-ratio helper clamping");
    RequireContains(
        ratioHelper,
        "RatioEstimatorIsFinite(ratio)&&ratio>=0.0f?ratio:1.0f;",
        "finite nonnegative generic ratio resolution");

    RequireContains(
        viewer,
        "returnHasHeitzRatioEstimatorHardwareSupport()&&"
            "m_ui.GetResolvedAntiAliasingSettings().rasterSampleCount==1u;",
        "single-sample renderer availability gate");
    RequireContains(
        viewer,
        "\"Representation\",\"Configuretheworldspacehierarchysharedbyraytraced\""
            "\"techniques.\"",
        "visible Representation drawer contract");
    RequireContains(
        viewer,
        "\"AllowRayTraversal\",&representation.allowRayTraversal",
        "ray traversal master control");
    RequireAbsent(
        viewer,
        "ScreenSpaceDirectionalShadows",
        "quarantined screen space directional shadows");
    RequireContains(
        viewer,
        "\"RayTracedShadows##Shadows\"",
        "public ray traced shadow group");
    RequireContains(
        viewer,
        "\"Enabled##RatioEstimatorShadows\"",
        "independent ray traced shadow enable control");
    RequireOrdered(
        viewer,
        {
            "if(shadowRayDispatchExpected){BeginRendererStage("
                "RendererTimingStage::ShadowRayDispatch);}",
            "m_HeitzRatioEstimatorShadowPass->Render(",
            "if(shadowRayDispatchExpected){EndRendererStage("
                "RendererTimingStage::ShadowRayDispatch);}"
        },
        "combined shadow-ray dispatch statistics timing envelope");
    RequireContains(
        viewer,
        "\"ShadowRayDispatch\","
            "RendererTimingStage::ShadowRayDispatch",
        "shadow ray dispatch cost in Statistics");
    RequireContains(
        viewer,
        "if(heitzShadowResult.dispatched&&"
            "heitzShadowResult.stochastic&&shadowNoiseSettings.animate)",
        "TAA-independent stochastic phase commit");
    RequireContains(
        viewer,
        "constexprfloatDefaultSunIrradiance=8.f;"
            "constexprfloatDefaultSunAngularSizeDegrees=0.2f;",
        "sun irradiance and angular-size defaults");
    RequireContains(
        viewer,
        "m_SunLight->angularSize=DefaultSunAngularSizeDegrees;",
        "loaded primary sun angular-size default");
    RequireContains(
        viewer,
        "m_SunLight->irradiance=DefaultSunIrradiance;",
        "loaded primary sun irradiance default");
    RequireContains(
        viewer,
        "ImGui::SliderInt(\"SamplesPerPixel##RatioEstimatorShadows\"",
        "logarithmic integer sample-rate slider");
    RequireContains(
        viewer,
        "\"SpecifyNoise##RatioEstimatorShadows\"",
        "effect-local shared-noise override control");
    RequireContains(
        viewer,
        "\"Usecustomnoisesamplingforthiseffectonly.This\""
            "\"doesnotchangethenoisesamplingusedbyanyother\""
            "\"effect.\"",
        "effect-local noise override tooltip");
    RequireOrdered(
        viewer,
        {
            "if(!multipleSamplesEnabled)ImGui::EndDisabled();",
            "\"MaxDistance##RatioEstimatorShadows\"",
            "\"RayBias##RatioEstimatorShadows\""
        },
        "max distance must remain available in hard-shadow mode");
    for (const std::string_view removed : {
            "HeitzRatioEstimatorRequiresPrivateHistory",
            "shadowInputs.motionVectors",
            "m_HeitzRatioEstimatorShadowPass->ResetHistory()",
            "hashed-white-noise",
            "\"HashedWhiteNoise\"",
            "\"1/16\"",
            "\"1/8\"",
            "\"1/4\"",
            "\"1/2\"" })
    {
        RequireAbsent(viewer, removed, "retired renderer/UI command state");
    }

    if (g_Failures != 0)
        return 1;
    std::cout << "Heitz ratio-estimator source contracts passed.\n";
    return 0;
}
