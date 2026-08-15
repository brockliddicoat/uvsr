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

    std::string_view ExtractSection(
        std::string_view source,
        std::string_view begin,
        std::string_view end)
    {
        const size_t beginPosition = source.find(begin);
        if (beginPosition == std::string_view::npos)
            return {};
        const size_t endPosition = source.find(
            end, beginPosition + begin.size());
        if (endPosition == std::string_view::npos)
            return {};
        return source.substr(
            beginPosition,
            endPosition - beginPosition);
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
            "for(uintreceiverSampleIndex=0u;",
            "constfloat4normalChannels=HeitzLoadNormals("
                "pixelPosition,receiverSampleIndex);",
            "constfloatdepth=HeitzLoadDepth("
                "pixelPosition,receiverSampleIndex);",
            "if(!isfinite(depth)||depth<=0.0f||",
            "constboolownsClosestSource=!foundClosestReceiver||"
                "depth>closestReceiverDepth;",
            "closestSourceModulation=1.0f;",
            "constfloat4packedMaterial=HeitzLoadMaterial("
                "pixelPosition,receiverSampleIndex);",
            "++validReceiverCount;",
            "constHeitzNormalizedResponsecenterResponse=",
            "float3receiverTotalModulation=1.0f;",
            "float3receiverDiffuseModulation=1.0f;",
            "constfloatvisibility=visible?1.0f:0.0f;",
            "receiverTotalModulation=visibility;",
            "receiverDiffuseModulation=receiverTotalModulation;",
            "for(uintemitterSampleIndex=0u;",
            "constuintsequenceIndex=receiverSampleIndex*"
                "emitterSampleCount+emitterSampleIndex;",
            "constHeitzNormalizedResponsecontribution="
                "HeitzEvaluateNormalizedResponse(",
            "receiverTotalDenominator+=contribution.total;",
            "receiverDiffuseDenominator+=contribution.diffuse;",
            "if(HeitzTraceVisibility(",
            "receiverTotalNumerator+=contribution.total;",
            "receiverDiffuseNumerator+=contribution.diffuse;",
            "receiverTotalModulation=saturate(ResolveCorrelatedRatio(",
            "receiverDiffuseModulation=saturate(ResolveCorrelatedRatio(",
            "resolvedNumerator+=centerResponse.total*"
                "receiverTotalModulation;",
            "resolvedDenominator+=centerResponse.total;",
            "if(ownsClosestSource){",
            "closestTotalModulation=receiverTotalModulation;",
            "closestSourceModulation=receiverDiffuseModulation;",
            "constfloat3modulation=HeitzResolveDeterministicRatio(",
            "resolvedNumerator,",
            "resolvedDenominator);",
            "u_Output[pixelPosition]=float4(modulation,1.0f);",
            "u_ClosestSourceOutput[pixelPosition]=float4("
                "closestSourceModulation,1.0f);"
        },
        "per-receiver matched ratios, analytic MSAA resolution, and closest diffuse source output");
    RequireContains(
        shader,
        "ResolveCorrelatedRatio("
            "receiverTotalNumerator*inverseEmitterSampleCount,"
            "receiverTotalDenominator*inverseEmitterSampleCount,"
            "g_Heitz.denominatorEpsilon)",
        "matched total-response numerator and denominator pair");
    RequireContains(
        shader,
        "ResolveCorrelatedRatio("
            "receiverDiffuseNumerator*inverseEmitterSampleCount,"
            "receiverDiffuseDenominator*inverseEmitterSampleCount,"
            "g_Heitz.denominatorEpsilon)",
        "matched diffuse-response numerator and denominator pair");
    if (CountOccurrences(shader, "ResolveCorrelatedRatio(") != 2u)
    {
        std::cerr << "FAIL: only the two per-receiver stochastic S/U ratios may use the estimator epsilon.\n";
        ++g_Failures;
    }
    RequireContains(
        constants,
        "uinttraceAllMsaaReceivers;uintpadding0;uintpadding1;uintpadding2;",
        "aligned closest-only receiver policy constant");
    RequireContains(
        passHeader,
        "constHeitzRatioEstimatorShadowSettings&settings,"
            "booltraceAllMsaaReceivers,constdonut::engine::IView&view,",
        "explicit receiver-frequency render contract");
    RequireContains(
        pass,
        "constants.traceAllMsaaReceivers=receiverSampleCount<=1u||"
            "traceAllMsaaReceivers?1u:0u;",
        "inert closest-only policy at single-sample raster frequency");
    RequireContains(
        shader,
        "constbooltraceAllMsaaReceivers=HEITZ_RASTER_SAMPLES==1||"
            "g_Heitz.traceAllMsaaReceivers!=0u;",
        "runtime receiver-frequency policy without shader variants");

    const std::string_view closestOnlySelection = ExtractSection(
        shader,
        "#ifHEITZ_RASTER_SAMPLES>1boolselectedClosestReceiver=false;",
        "#endif[loop]for(uintreceiverSampleIndex=0u;");
    RequireOrdered(
        closestOnlySelection,
        {
            "if(!traceAllMsaaReceivers){",
            "for(uintreceiverSampleIndex=0u;",
            "constfloat4normalChannels=HeitzLoadNormals(",
            "constfloatdepth=HeitzLoadDepth(",
            "if(!isfinite(depth)||depth<=0.0f||",
            "if(!selectedClosestReceiver||depth>closestReceiverDepth){",
            "selectedClosestReceiverIndex=receiverSampleIndex;"
        },
        "complete strict reverse-depth owner selection before receiver evaluation");
    RequireAbsent(
        closestOnlySelection,
        "HeitzTraceVisibility(",
        "ray-free closest-owner selection prepass");

    const std::string_view receiverEvaluation = ExtractSection(
        shader,
        "[loop]for(uintreceiverSampleIndex=0u;",
        "if(validReceiverCount==0u)");
    RequireOrdered(
        receiverEvaluation,
        {
            "if(!traceAllMsaaReceivers&&"
                "(!selectedClosestReceiver||"
                "receiverSampleIndex!=selectedClosestReceiverIndex))",
            "constfloat4normalChannels=HeitzLoadNormals(",
            "constfloat4packedMaterial=HeitzLoadMaterial(",
            "HeitzTraceVisibility("
        },
        "non-owner rejection before material and ray work");
    RequireContains(
        receiverEvaluation,
        "constuintsequenceIndex=receiverSampleIndex*emitterSampleCount+"
            "emitterSampleIndex;",
        "original receiver identity in ratio-estimator sampling");
    RequireContains(
        receiverEvaluation,
        "HeitzSample2D(dispatchPosition,receiverSampleIndex,"
            "sampleSequencePhase)",
        "original receiver identity in the one-ray sampling route");

    const std::string_view closestOnlyOutput = ExtractSection(
        shader,
        "#ifHEITZ_RASTER_SAMPLES>1if(!traceAllMsaaReceivers)",
        "//Thisisadeterministicfactorization");
    RequireOrdered(
        closestOnlyOutput,
        {
            "u_Output[pixelPosition]=float4("
                "closestTotalModulation,1.0f);",
            "u_ClosestSourceOutput[pixelPosition]=float4("
                "closestSourceModulation,1.0f);",
            "return;"
        },
        "direct closest-owner total and diffuse output routing");
    RequireAbsent(
        closestOnlyOutput,
        "resolvedNumerator",
        "response reweighting in closest-only mode");
    RequireAbsent(
        closestOnlyOutput,
        "validReceiverCount/",
        "coverage scaling in closest-only mode");
    RequireContains(
        shader,
        "float3HeitzSanitizeNonnegativeResponse(float3response){"
            "returnfloat3("
            "isfinite(response.x)?max(response.x,0.0f):0.0f,"
            "isfinite(response.y)?max(response.y,0.0f):0.0f,"
            "isfinite(response.z)?max(response.z,0.0f):0.0f);}",
        "componentwise finite response sanitization");
    RequireContains(
        shader,
        "#ifHEITZ_RASTER_SAMPLES>1Texture2DMS<float,"
            "HEITZ_RASTER_SAMPLES>t_Depth:register(t1);",
        "compile-time multisampled receiver depth input");
    for (const std::string_view coherentLoad : {
            "t_Depth.Load(pixelPosition,receiverSampleIndex)",
            "t_GBufferDiffuse.Load(pixelPosition,receiverSampleIndex)",
            "t_GBufferMaterial.Load(pixelPosition,receiverSampleIndex)",
            "t_GBufferNormals.Load(pixelPosition,receiverSampleIndex)",
            "t_MaterialAmbientOcclusion.Load(pixelPosition,receiverSampleIndex)",
            "t_GBufferEmissive.Load(pixelPosition,receiverSampleIndex)" })
    {
        RequireContains(
            shader,
            coherentLoad,
            "coherent per-raster-sample G-buffer loads");
    }
    RequireContains(
        shader,
        "if(validReceiverCount==0u){u_Output[pixelPosition]=1.0f;",
        "zero-covered-surface neutral output");
    RequireContains(
        shader,
        "RWTexture2D<float4>u_ClosestSourceOutput:register(u1);",
        "ratio and closest-receiver diffuse modulation output");
    RequireContains(
        shader,
        "#ifOUTPUT_SOURCE_MODULATION&&OUTPUT_HIT_DISTANCE"
            "#errorSourcemodulationandhitdistancearemutuallyexclusiveoutputs."
            "#endif",
        "mutually exclusive source-ratio and physical-distance outputs");
    RequireContains(
        shader,
        "u_ClosestSourceOutput[pixelPosition]=1.0f;",
        "zero-covered closest-source neutral output");
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
            "constbooluseOpacityTexture="
                "(material.flags&MaterialFlags_UseOpacityTexture)!=0&&"
                "material.opacityTextureIndex>=0;",
            "constbooluseBaseAlphaTexture=!useOpacityTexture&&"
                "(material.flags&MaterialFlags_UseBaseOrDiffuseTexture)!=0&&"
                "material.baseOrDiffuseTextureIndex>=0;",
            "floatopacity=material.opacity;",
            "if(useOpacityTexture)",
            "NonUniformResourceIndex(material.opacityTextureIndex)",
            "opacity*=opacityTexture.SampleLevel(",
            ").r;",
            "elseif(useBaseAlphaTexture)",
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
            "if(!useRatioEstimator){",
            "g_Heitz.hardShadows!=0u?HeitzLightCenterDirection():"
                "HeitzSampleDirectionalEmitter(",
            "if(!CanEvaluatePbrDirectSurfacePrepared(",
            "u_Output[pixelPosition]=1.0f;",
            "constfloat3rayOrigin=HeitzPrepareRayOrigin(",
            "constfloatvisible=float(HeitzTraceVisibility(",
            "u_Output[pixelPosition]=float4(visible.xxx,1.0f);",
            "channels[0]=HeitzLoadDiffuse("
                "pixelPosition,receiverSampleIndex);",
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
            "float2HeitzSample2D(uint2dispatchPosition,"
                "uintsampleIndex,uintphase)",
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
            "nvrhi::BindingLayoutItem::Texture_SRV(8),"
            "nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),"
            "nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),"
            "nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12),"
            "nvrhi::BindingLayoutItem::Sampler(0),"
            "nvrhi::BindingLayoutItem::Texture_UAV(0)",
        "material-aware base output binding layout");
    RequireContains(
        shader,
        "if(sampleScheduleEnabled&&attemptToken==0u){return;}",
        "stochastic attempt mask must exit before G-buffer and ray work");
    RequireContains(
        shader,
        "constuintsampleSequencePhase=UvsrResolveSampleSequencePhase("
            "g_Heitz.sampleSequenceMode,attemptToken,"
            "g_Heitz.sampleSequencePhase);",
        "stochastic samples must consume the accepted pixel's successful-sample phase");
    RequireContains(
        pass,
        "constants.sampleSequenceMode=static_cast<uint32_t>("
            "ResolveLightingSampleSequenceMode("
            "sampleSchedule,stochastic,noiseSettings.animate));",
        "every stochastic soft-sun mode must consume the attempt mask");
    RequireContains(
        pass,
        "m_BoundAttemptMask!=attemptMask",
        "attempt mask resource identity binding-cache invalidation");
    RequireContains(
        pass,
        "pipelineDescription.bindingLayouts={"
            "m_BindingLayouts[layoutIndex],m_BindlessLayout};",
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
            "instanceBuffer==other.instanceBuffer&&"
            "descriptorTable==other.descriptorTable;",
        "all material visibility resources participate in cache identity");
    RequireContains(
        passHeader,
        "std::array<nvrhi::BindingSetHandle,7>m_BindingSets;",
        "one frame-local binding set per receiver-count and source or hit permutation");
    RequireContains(
        passHeader,
        "std::array<nvrhi::BindingLayoutHandle,3>m_BindingLayouts;",
        "base, hit-distance, and multisample-owner output layouts");
    RequireContains(
        pass,
        "nvrhi::BindingLayoutItem::Texture_UAV(1)",
        "the optional hit or closest-source output binding");
    RequireContains(
        pass,
        "\"RayTracedSunShadows/ClosestSourceModulation\"",
        "closest-source RGBA16F allocation");
    RequireContains(
        pass,
        "1,m_OutputClosestSourceModulation",
        "ratio or multisample closest-source output binding");
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
        "receiverSampleCount==1u?nvrhi::TextureDimension::Texture2D:"
            "nvrhi::TextureDimension::Texture2DMS;",
        "single-sample and multisampled input dimension contract");
    RequireContains(
        pass,
        "{1u,\"1\",false,false},{1u,\"1\",false,true},"
            "{1u,\"1\",true,false},{2u,\"2\",true,false},"
            "{4u,\"4\",true,false},{8u,\"8\",true,false},"
            "{16u,\"16\",true,false}",
        "complete 1x source-or-hit and 2x-through-16x source pipeline matrix");
    RequireContains(
        pass,
        "if(outputSourceModulation&&outputHitDistance)",
        "source modulation and hit distance resource exclusion");
    RequireContains(
        pass,
        "constboolrequestedSourceModulation=useRatioEstimator||"
            "receiverSampleCount>1u;",
        "1x ratio and MSAA diffuse-source output request");
    RequireContains(
        pass,
        "constboolrequestedHitDistance=settings.outputHitDistance&&"
            "m_HitDistanceSupported&&receiverSampleCount==1u&&"
            "!requestedSourceModulation;",
        "physical hit output only for a matched 1x one-ray signal");
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
        "boolenabled=true;boolhardShadows=false;"
            "booluseRatioEstimator=true;"
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
            "heitz_ratio_estimator_shadows_cs.hlsl-Tcs-EGenerate") != 3u)
    {
        std::cerr << "FAIL: the shader catalog must package single-sample base, single-sample source, and MSAA Heitz Generate axes.\n";
        ++g_Failures;
    }
    RequireContains(
        shaderConfig,
        "heitz_ratio_estimator_shadows_cs.hlsl-Tcs-EGenerate"
            "-DHEITZ_RASTER_SAMPLES=1-DOUTPUT_SOURCE_MODULATION=0"
            "-DOUTPUT_HIT_DISTANCE={0,1}",
        "single-sample base and optional closest-hit permutations");
    RequireContains(
        shaderConfig,
        "heitz_ratio_estimator_shadows_cs.hlsl-Tcs-EGenerate"
            "-DHEITZ_RASTER_SAMPLES=1-DOUTPUT_SOURCE_MODULATION=1"
            "-DOUTPUT_HIT_DISTANCE=0",
        "single-sample ratio diffuse-source permutation");
    RequireContains(
        shaderConfig,
        "heitz_ratio_estimator_shadows_cs.hlsl-Tcs-EGenerate"
            "-DHEITZ_RASTER_SAMPLES={2,4,8,16}"
            "-DOUTPUT_SOURCE_MODULATION=1"
            "-DOUTPUT_HIT_DISTANCE=0",
        "hit-distance-free multisampled receiver permutations");
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
        "boolSupportsHeitzRatioEstimatorShadows()const{"
            "returnHasHeitzRatioEstimatorHardwareSupport();}",
        "MSAA-independent renderer availability gate");
    RequireContains(
        viewer,
        "shadowInputs.depth=m_RenderTargets->Depth;"
            "shadowInputs.diffuse=m_RenderTargets->GBufferDiffuse;"
            "shadowInputs.material=m_RenderTargets->GBufferSpecular;"
            "shadowInputs.normals=m_RenderTargets->GBufferNormals;"
            "shadowInputs.emissive=m_RenderTargets->GBufferEmissive;"
            "shadowInputs.materialAmbientOcclusion="
                "m_RenderTargets->MaterialAmbientOcclusion;",
        "Heitz dispatch consumes the original sample-frequency G-buffer");
    RequireAbsent(
        viewer,
        "shadowInputs.depth=visibilityDepth;shadowInputs.diffuse="
            "closestSurfaceOutputs.diffuse",
        "closest-surface Heitz receiver shortcut");
    RequireContains(
        viewer,
        "heitzShadowResult.receiverSampleCount==1u&&"
            "shadowDenoisingMethod!=DenoisingMethodChoice::None&&"
            "(!shadowThirdPartyDenoising||"
            "(!m_ui.DirectionalShadows.ratioEstimator.hardShadows&&"
            "heitzShadowResult.hitDistance&&"
            "heitzShadowResult.hitDistanceMatchesSignal))",
        "single-sample sun denoising with method-specific third-party inputs");
    RequireAbsent(
        viewer,
        "visibilityDirectLightVisibilities.sun={};",
        "unshadowed MSAA GI source regression");
    const std::string_view auxiliarySourcePreparation = ExtractSection(
        viewer,
        "DirectLightVisibilitiesvisibilityDirectLightVisibilities="
            "directLightVisibilities;",
        "BeginRendererStage("
            "RendererTimingStage::VisibilityLightingPreparation);");
    if (CountOccurrences(
            auxiliarySourcePreparation,
            "visibilityDirectLightVisibilities.sun=") != 1u)
    {
        std::cerr << "FAIL: auxiliary GI-source preparation must assign the closest-owner sun exactly once.\n";
        ++g_Failures;
    }
    RequireAbsent(
        auxiliarySourcePreparation,
        "visibilityDirectLightVisibilities.sun={};",
        "auxiliary GI-source shadow retention");
    RequireAbsent(
        auxiliarySourcePreparation,
        "sourceSunVisibility",
        "single-sample alternate source inside the MSAA-only branch");
    RequireOrdered(
        viewer,
        {
            "if(heitzShadowResult.receiverSampleCount>1u&&"
                "heitzShadowResult.closestSourceModulation){",
            "visibilityDirectLightVisibilities.sun={"
                "heitzShadowResult.closestSourceModulation,"
                "heitzShadowResult.light,"
                "DirectLightVisibilityEncoding::RgbRgba16Float};",
            "m_PbrDeferredLightingPass->Render("
                "m_CommandList,*m_View,visibilityDeferredInputs,"
                "visibilityDirectLightVisibilities,",
            "m_PbrDeferredLightingPass->Render("
                "m_CommandList,*m_View,deferredMsaaInputs,"
                "directLightVisibilities,"
        },
        "closest-owner diffuse GI source and total-response final MSAA lighting");
    const std::string_view singleSampleLighting = ExtractSection(
        viewer,
        "else{DirectLightVisibilitysourceSunVisibility;",
        "EndRendererStage(RendererTimingStage::DirectLighting);");
    RequireOrdered(
        singleSampleLighting,
        {
            "if(heitzShadowResult.receiverSampleCount==1u&&"
                "heitzShadowResult.ratioEstimator&&"
                "heitzShadowResult.closestSourceModulation){",
            "sourceSunVisibility={"
                "heitzShadowResult.closestSourceModulation,"
                "heitzShadowResult.light,"
                "DirectLightVisibilityEncoding::RgbRgba16Float};",
            "m_PbrDeferredLightingPass->Render("
                "m_CommandList,*m_View,deferredInputs,"
                "directLightVisibilities,",
            "sourceSunVisibility);"
        },
        "reachable 1x diffuse-only GI source routing");
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
        "(heitzRatioEstimatorSelected&&"
            "m_HeitzRatioEstimatorShadowPass&&shadowNoise)",
        "Heitz timing expectation must mirror the dispatch noise dependency");
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
        "DrawSliderInt(\"SamplesPerPixel##RatioEstimatorShadows\"",
        "logarithmic integer sample-rate slider");
    RequireOrdered(
        viewer,
        {
            "constboolstochasticSunExtent="
                "std::clamp(angularSize,0.f,90.f)*"
                "0.0087266462599716478846f>1e-6f;",
            "constboolmultipleSamplesEnabled=ratio.useRatioEstimator&&"
                "!ratio.hardShadows&&stochasticSunExtent;",
            "intsampleRateLog2=multipleSamplesEnabled?"
                "ratio.sampleRateLog2:"
                "HeitzRatioEstimatorMinimumSampleRateLog2;",
            "intsampleRate=1<<sampleRateLog2;",
            "constboolsampleSliderManualDisabledAlpha="
                "BeginVisuallyDisabledUiScope("
                "\"##RatioEstimatorSamplesDisabledPresentation\","
                "!multipleSamplesEnabled);",
            "DrawSliderInt("
                "\"SamplesPerPixel##RatioEstimatorShadows\","
                "&sampleRate,",
            "constintcandidateSampleRateLog2=std::clamp(",
            "if(candidateSampleRateLog2!=ratio.sampleRateLog2)",
            "ratio.sampleRateLog2=candidateSampleRateLog2;",
            "m_app->ResetImageBasedLightingHistory();",
            "if(DrawPresetResetIcon("
                "\"RatioEstimatorShadowSamples\","
                "ratio.sampleRateLog2!=ratioDefaults.sampleRateLog2))",
            "EndVisuallyDisabledUiScope("
                "sampleSliderManualDisabledAlpha);"
        },
        "gated sample slider presents the effective one-sample value locally, "
        "uses nested-safe visual dimming, preserves the stored setting, and "
        "only commits interactive changes");
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
            "EndVisuallyDisabledUiScope(sampleSliderManualDisabledAlpha);",
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
