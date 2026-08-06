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
    RequireContains(
        shader,
        "RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|"
            "RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES",
        "conservative first-hit inline shadow query");
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
            "if(g_Heitz.hardShadows!=0u){",
            "if(!CanEvaluatePbrDirectSurfacePrepared(",
            "u_Output[pixelPosition]=1.0f;",
            "constfloat3rayOrigin=HeitzPrepareRayOrigin(",
            "constfloatvisible=float(HeitzTraceVisibility(",
            "u_Output[pixelPosition]=float4(visible.xxx,1.0f);",
            "channels[0]=t_GBufferDiffuse[pixelPosition];",
            "constPbrPreparedMaterialpreparedMaterial="
        },
        "receiver-gated hard center-ray branch before soft material work");
    RequireContains(
        shader,
        "constPbrGBufferSurfaceNormalssurfaceNormals="
            "DecodePbrGBufferSurfaceNormals(normalChannels,packedMaterial);",
        "shared minimal hard-shadow G-buffer surface decoder");
    RequireAbsent(
        shader,
        "DecodeOctahedralNormal(",
        "hard-shadow-local G-buffer normal decoding");

    RequireContains(
        shader,
        "frameIndex*0x9e3779b9u",
        "integer golden-Weyl temporal rotation");
    RequireOrdered(
        shader,
        {
            "constuintphase=g_Heitz.sampleSequencePhase;",
            "constuintsequenceIndex=sampleIndex+1u;",
            "HeitzRadicalInverse(sequenceIndex,2u)+",
            "HeitzBlueNoise(pixelPosition,0u)+",
            "HeitzRadicalInverse(phase+1u,5u),",
            "HeitzRadicalInverse(sequenceIndex,3u)+",
            "HeitzBlueNoise(pixelPosition,1u)+",
            "HeitzGoldenWeylPhase(phase)"
        },
        "progressive independently shifted blue-noise emitter sequence");
    RequireContains(
        shader,
        "floatcosTheta=lerp(1.0f,cosMaximum,sample.x);",
        "uniform spherical-cap radial CDF mapping");
    RequireContains(
        shader,
        "HeitzPermutatedWhiteNoise(",
        "permutated white-noise emitter sampling");

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
            "nvrhi::BindingLayoutItem::Texture_UAV(0)",
        "compact one-output binding layout");
    RequireContains(
        passHeader,
        "nvrhi::BindingSetHandlem_BindingSet;",
        "single frame-local binding set");
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
            "HasFormatSupport(device,nvrhi::Format::R16_UNORM,false)",
        "minimal output and blue-noise format support gate");
    for (const std::string_view removed : {
            "R16_FLOAT",
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
        "boolhardShadows=false;int32_tsampleRateLog2=1;floatrayBias=0.002f;",
        "hard-shadow, integer-rate, and low geometric-bias defaults");
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
        std::cerr << "FAIL: the shader catalog must package only Heitz Generate.\n";
        ++g_Failures;
    }
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
        "\"Representation\",\"Configuretheworld-spacehierarchysharedbyray-traced\""
            "\"techniques.\"",
        "visible Representation drawer contract");
    RequireContains(
        viewer,
        "\"Enabled##ScreenSpaceShadows\"",
        "independent screen-space enable control");
    RequireContains(
        viewer,
        "\"Enabled##RatioEstimatorShadows\"",
        "independent ratio-estimator enable control");
    RequireOrdered(
        viewer,
        {
            "BeginRendererStage(RendererTimingStage::RatioEstimatorShadows);",
            "m_HeitzRatioEstimatorShadowPass->Render(",
            "EndRendererStage(RendererTimingStage::RatioEstimatorShadows);"
        },
        "ray-dispatch-only statistics timing envelope");
    RequireContains(
        viewer,
        "\"Ratio-EstimatorRayDispatch\","
            "RendererTimingStage::RatioEstimatorShadows",
        "ratio-estimator cost in Statistics");
    RequireContains(
        viewer,
        "if(heitzShadowResult.dispatched&&heitzShadowResult.stochastic)",
        "TAA-independent stochastic phase commit");
    RequireContains(
        viewer,
        "m_SunLight->angularSize=0.53f;",
        "loaded primary sun angular-size default");
    RequireContains(
        viewer,
        "ImGui::SliderInt(\"SamplesPerPixel##RatioEstimatorShadows\"",
        "logarithmic integer sample-rate slider");
    RequireContains(
        viewer,
        "\"NoisePattern##RatioEstimatorShadows\"",
        "ratio-estimator noise-pattern control");
    RequireContains(
        viewer,
        "\"AnimateSamples##RatioEstimatorShadows\"",
        "ratio-estimator sample-animation control");
    RequireOrdered(
        viewer,
        {
            "if(!softSamplingControlsEnabled)ImGui::EndDisabled();",
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
