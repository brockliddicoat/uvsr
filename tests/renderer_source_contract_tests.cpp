#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

#include "../src/renderer_statistics.h"

namespace
{
    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        std::ostringstream contents;
        contents << stream.rdbuf();
        std::string source = contents.str();
        source.erase(
            std::remove(source.begin(), source.end(), '\r'),
            source.end());
        return source;
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
            beginPosition, endPosition - beginPosition);
    }

    bool ExpectContains(
        std::string_view source,
        std::string_view required,
        const char* contract)
    {
        if (source.find(required) != std::string_view::npos)
            return true;
        std::cerr << "FAIL: " << contract << " must contain '"
                  << required << "'.\n";
        return false;
    }

    bool ExpectAbsent(
        std::string_view source,
        std::string_view forbidden,
        const char* contract)
    {
        if (source.find(forbidden) == std::string_view::npos)
            return true;
        std::cerr << "FAIL: " << contract << " must not contain '"
                  << forbidden << "'.\n";
        return false;
    }

    bool ExpectFileAbsent(
        const std::filesystem::path& path,
        const char* contract)
    {
        if (!std::filesystem::exists(path))
            return true;
        std::cerr << "FAIL: " << contract << " must not exist at '"
                  << path.generic_string() << "'.\n";
        return false;
    }

    bool ExpectOrdered(
        std::string_view source,
        std::string_view first,
        std::string_view second,
        const char* contract)
    {
        const size_t firstPosition = source.find(first);
        const size_t secondPosition = source.find(second);
        if (firstPosition != std::string_view::npos &&
            secondPosition != std::string_view::npos &&
            firstPosition < secondPosition)
        {
            return true;
        }
        std::cerr << "FAIL: " << contract << " must place '"
                  << first << "' before '" << second << "'.\n";
        return false;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: uvsr_renderer_source_contract_tests <root>\n";
        return 2;
    }

    const std::filesystem::path root = argv[1];
    const std::string buildSystem = ReadFile(root / "CMakeLists.txt");
    const std::string attributes = ReadFile(root / ".gitattributes");
    const std::string viewer = ReadFile(root / "src/uvsr.cpp");
    const std::string cmaa = ReadFile(root / "src/cmaa2.cpp");
    const std::string cmaaHeader = ReadFile(root / "src/cmaa2.h");
    const std::string cmaaShader = ReadFile(root / "src/cmaa2.hlsl");
    const std::string cmaaVendoredShader = ReadFile(
        root / "legal/samples/intel-cmaa2/CMAA2.hlsl");
    const std::string fastApproximate = ReadFile(
        root / "src/fast_approximate_aa.cpp");
    const std::string fastApproximateHeader = ReadFile(
        root / "src/fast_approximate_aa.h");
    const std::string fastApproximateShader = ReadFile(
        root / "src/fast_approximate_aa_ps.hlsl");
    const std::string deferredLighting = ReadFile(
        root / "src/pbr_deferred_lighting_cs.hlsl");
    const std::string deferredConstants = ReadFile(
        root / "src/pbr_deferred_lighting_cb.h");
    const std::string directLightVisibility = ReadFile(
        root / "src/direct_light_visibility.h");
    const std::string flashlight = ReadFile(root / "src/flashlight.h");
    const std::string cameraCollision = ReadFile(
        root / "src/camera_collision.cpp");
    const std::string flashlightShared = ReadFile(
        root / "src/flashlight_shared.h");
    const std::string sceneLoading = ReadFile(
        root / "src/scene_loading.h");
    const std::string noiseSettings = ReadFile(
        root / "src/noise_settings.h");
    const std::string noiseTextureLibrary = ReadFile(
        root / "src/noise_texture_library.cpp");
    const std::string noiseTextureLibraryHeader = ReadFile(
        root / "src/noise_texture_library.h");
    const std::string directionalShadowSettings = ReadFile(
        root / "src/directional_shadow_settings.h");
    const std::string skyVisibilitySettings = ReadFile(
        root / "src/ray_traced_sky_visibility_settings.h");
    const std::string representationSettings = ReadFile(
        root / "src/world_space_representation_settings.h");
    const std::string visibilityConstants = ReadFile(
        root / "src/screen_space_visibility_cb.h");
    const std::string imageBasedLighting = ReadFile(
        root / "src/image_based_lighting_environment.cpp");
    const std::string imageBasedLightingHeader = ReadFile(
        root / "src/image_based_lighting_environment.h");
    const std::string visibilityPass = ReadFile(
        root / "src/screen_space_visibility.cpp");
    const std::string visibilityPassHeader = ReadFile(
        root / "src/screen_space_visibility.h");
    const std::string visibilityShader = ReadFile(
        root / "src/screen_space_visibility_cs.hlsl");
    const std::string pbrDeferredLightingPass = ReadFile(
        root / "src/pbr_deferred_lighting_pass.cpp");
    const std::string pbrDeferredLightingPassHeader = ReadFile(
        root / "src/pbr_deferred_lighting_pass.h");
    const std::string msaaVisibilityResolvePass = ReadFile(
        root / "src/msaa_visibility_resolve.cpp");
    const std::string msaaVisibilityResolvePassHeader = ReadFile(
        root / "src/msaa_visibility_resolve.h");
    const std::string temporalAaPass = ReadFile(
        root / "src/temporal_aa.cpp");
    const std::string temporalAaPassHeader = ReadFile(
        root / "src/temporal_aa.h");
    const std::string worldRepresentation = ReadFile(
        root / "src/world_space_representation.cpp");
    const std::string worldRepresentationHeader = ReadFile(
        root / "src/world_space_representation.h");
    const std::string heitzShadows = ReadFile(
        root / "src/heitz_ratio_estimator_shadows.cpp");
    const std::string donutLoadingOverride = ReadFile(
        root / "overrides/donut-loading.patch");
    const std::string donutEngineOverride = ReadFile(
        root / "overrides/donut-engine.patch");
    const std::string donutLoadingAppOverride = ReadFile(
        root / "overrides/donut-loading-app.patch");
    bool passed = true;

    passed &= ExpectContains(
        viewer,
        "\"UVSR Engine \" + std::string(apiName)",
        "runtime window title");
    passed &= ExpectAbsent(
        viewer,
        "\"UVSR Renderer \"",
        "retired runtime window title");
    passed &= ExpectAbsent(
        viewer,
        "\"Heitz ratio estimator shadows require",
        "person-named visible ratio estimator error");
    passed &= ExpectAbsent(
        heitzShadows,
        "\"Heitz ratio estimator shadow",
        "person-named visible ratio estimator logs");

    for (const std::filesystem::path& removedHardwarePath : {
            root / "src/gpu_performance_monitor.cpp",
            root / "src/gpu_performance_monitor.h",
            root / "tests/hardware_statistics_tests.cpp" })
    {
        passed &= ExpectFileAbsent(
            removedHardwarePath,
            "removed Hardware Performance support file");
    }
    for (const std::string_view removedHardwareBinding : {
            std::string_view("HardwareCapabilities"),
            std::string_view("QueryHardwareCapabilities"),
            std::string_view("m_HardwareCapabilities"),
            std::string_view("StatisticsEffect::Hardware"),
            std::string_view("##HardwareStatistics") })
    {
        passed &= ExpectAbsent(
            viewer,
            removedHardwareBinding,
            "removed Hardware Performance runtime binding");
    }
    for (const std::string_view removedHardwareBuildEntry : {
            std::string_view("gpu_performance_monitor.cpp"),
            std::string_view("hardware_statistics_tests.cpp"),
            std::string_view("uvsr_hardware_statistics_tests") })
    {
        passed &= ExpectAbsent(
            buildSystem,
            removedHardwareBuildEntry,
            "removed Hardware Performance build registration");
    }
    passed &= ExpectAbsent(
        viewer,
        "snapshot.gpuMetrics",
        "retired current-utilization header throughput");

    passed &= ExpectContains(
        representationSettings,
        "bool allowRayTraversal = true;",
        "ray traversal factory master gate");
    passed &= ExpectContains(
        viewer,
        "\"Allow Ray Traversal\",\n"
            "                    &representation.allowRayTraversal",
        "Representation ray traversal toggle");
    passed &= ExpectContains(
        visibilityPassHeader,
        "DefaultVisibilitySampleCount = 16u;",
        "default diffuse sample count");
    passed &= ExpectContains(
        visibilityPassHeader,
        "MaximumVisibilityStepDistributionExponent = 8.f;",
        "diffuse distribution maximum");
    passed &= ExpectContains(
        visibilityPassHeader,
        "MaximumVisibilityAmbientOcclusionStrength = 8.f;",
        "ambient occlusion strength maximum");
    passed &= ExpectContains(
        visibilityPassHeader,
        "bool outputHitDistance = false;",
        "optional diffuse hit distance defaults");
    passed &= ExpectContains(
        visibilityPassHeader,
        "ScreenSpaceAmbientOcclusionHitDistanceMatchesSignal = true;",
        "matched ambient occlusion hit distance contract");
    passed &= ExpectContains(
        visibilityPassHeader,
        "ScreenSpaceIndirectDiffuseHitDistanceMatchesSignal = true;",
        "matched diffuse GI hit distance contract");
    passed &= ExpectContains(
        visibilityShader,
        "ambientHitDistanceSectorSum += float(newSectorCount) *",
        "ambient occlusion blocked sector first moment");
    passed &= ExpectContains(
        visibilityShader,
        "float(ambientVisibleSectorCount) * ambientTraceReach",
        "ambient occlusion censored visible sector distance");
    passed &= ExpectContains(
        visibilityShader,
        "contributionWeight * contributionHitDistance",
        "diffuse GI luminance weighted first moment");
    passed &= ExpectContains(
        viewer,
        "denoisingInputs.hitDistanceMatchesSignal =\n"
            "                                hitDistanceMatchesSignal;",
        "diffuse denoising matched hit distance forwarding");
    passed &= ExpectContains(
        skyVisibilitySettings,
        "bool applyToSpecularIbl = true;",
        "specular IBL sky visibility default");
    passed &= ExpectContains(
        skyVisibilitySettings,
        "bool useRatioEstimator = true;\n"
            "        bool outputHitDistance = false;",
        "sky estimator and optional hit distance defaults");
    passed &= ExpectContains(
        directionalShadowSettings,
        "bool useRatioEstimator = true;\n"
            "        bool outputHitDistance = false;",
        "sun estimator and optional hit distance defaults");
    passed &= ExpectContains(
        flashlight,
        "float beamSizeDegrees = 16.f;\n"
            "        float angularSizeDegrees = 2.8641924f;\n"
            "        float beamRoundness = 0.80f;",
        "flashlight beam and analytical emitter defaults");
    passed &= ExpectContains(
        flashlight,
        "float colorLinearRed = 1.f;\n"
            "        float colorLinearGreen = 1.f;\n"
            "        float colorLinearBlue = 1.f;",
        "flashlight pure-white factory beam color");
    passed &= ExpectContains(
        flashlight,
        "ResolveFlashlightEmitterRadiusMeters(\n"
            "        float angularSizeDegrees)",
        "flashlight analytical angular-size conversion");
    passed &= ExpectContains(
        flashlight,
        "FlashlightMinimumAngularSizeDegrees = 0.f;\n"
            "    inline constexpr float FlashlightMaximumAngularSizeDegrees = 20.f;",
        "flashlight Angular Size range");
    passed &= ExpectContains(
        flashlight,
        "FlashlightMinimumCameraHorizontalOffsetMeters =\n"
            "        -0.40f;",
        "flashlight horizontal offset minimum");
    passed &= ExpectContains(
        flashlight,
        "FlashlightMaximumCameraVerticalOffsetMeters =\n"
            "        0.40f;",
        "flashlight vertical offset maximum");
    passed &= ExpectContains(
        viewer,
        "constexpr float DefaultSunIrradiance = 8.f;\n"
            "constexpr float DefaultSunAngularSizeDegrees = 0.2f;",
        "sun lighting defaults");
    passed &= ExpectContains(
        viewer,
        "m_SunLight->irradiance = DefaultSunIrradiance;",
        "scene sun irradiance initialization");
    passed &= ExpectContains(
        viewer,
        "m_SunLight->angularSize = DefaultSunAngularSizeDegrees;",
        "scene sun angular size initialization");
    passed &= ExpectContains(
        viewer,
        "\"%s: %llu/%s\\n\"",
        "loading current and average tick line with colon");
    passed &= ExpectContains(
        sceneLoading,
        "SceneLoadCounterTickMilliseconds = 20u;",
        "20 millisecond loading counter cadence");
    passed &= ExpectContains(
        sceneLoading,
        "return elapsedMilliseconds / SceneLoadCounterTickMilliseconds;",
        "one loading-count increment per 20 milliseconds");
    passed &= ExpectContains(
        viewer,
        "ResolveSceneLoadElapsedTicks(elapsedLoadMilliseconds)",
        "elapsed loading tick counter");
    passed &= ExpectContains(
        viewer,
        "averageLoadTicks = ResolveAverageSceneLoadTicks(\n"
            "                    sceneTiming->second);",
        "per-scene average loading tick denominator");
    passed &= ExpectContains(
        viewer,
        "averageLoadTicks = ResolveAverageSceneLoadTicks(\n"
            "                    m_AllSceneLoadTiming);",
        "global average loading tick fallback");
    passed &= ExpectOrdered(
        viewer,
        "RecordSceneLoadDuration(\n"
            "                    m_AllSceneLoadTiming,",
        "RecordBoundedSceneLoadDuration(\n"
            "                        m_SceneLoadTimingByScene,",
        "completed loading duration history publication");
    passed &= ExpectContains(
        viewer,
        "m_SceneLoadFailed = !m_app->IsSceneLoaded();",
        "failed scene loads excluded from timing history");
    passed &= ExpectOrdered(
        viewer,
        "RecordBoundedSceneLoadDuration(\n"
            "                        m_SceneLoadTimingByScene,",
        "SaveSceneLoadTimingDatabase();",
        "successful scene loading history persistence");
    passed &= ExpectContains(
        sceneLoading,
        "while (historyByScene.size() >=\n"
            "                MaximumSceneLoadTimingEntries)",
        "bounded scene loading history capacity enforcement");
    passed &= ExpectContains(
        viewer,
        "L\"UVSR\" / L\"scene-load-history-v1.txt\"",
        "versioned per-user scene loading history path");
    passed &= ExpectAbsent(
        viewer,
        "%u/100",
        "retired fixed loading denominator");
    passed &= ExpectContains(
        noiseSettings,
        "NoisePattern pattern = NoisePattern::SpatiotemporalBlue;\n"
            "        NoiseResolution resolution = NoiseResolution::Size128;",
        "128x128 Spatiotemporal Blue global noise defaults");
    passed &= ExpectContains(
        noiseSettings,
        "case NoisePattern::SpatiotemporalBlue:\n"
            "            return 64u;",
        "64-layer spatiotemporal noise depth");
    passed &= ExpectContains(
        noiseTextureLibraryHeader,
        "static constexpr std::size_t CacheEntryCount = 12u;",
        "three-pattern by four-resolution lazy noise cache");
    passed &= ExpectContains(
        noiseTextureLibrary,
        "description.format = nvrhi::Format::R8_UNORM;",
        "R8 precomputed noise textures");
    passed &= ExpectContains(
        viewer,
        "m_NoiseTextureLibrary = std::make_unique<NoiseTextureLibrary>(\n"
            "            GetDevice(),\n"
            "            mediaDir / \"uvsr/noise\");",
        "central packaged noise texture library");
    passed &= ExpectContains(
        viewer,
        "const NoiseSettings visibilityNoiseSettings = ResolveNoiseSettings(\n"
            "            m_ui.Noise,\n"
            "            m_ui.ScreenSpaceVisibility.noise);",
        "global-or-custom Visibility noise resolution");
    passed &= ExpectContains(
        viewer,
        "const NoiseSettings shadowNoiseSettings = ResolveNoiseSettings(\n"
            "            m_ui.Noise,\n"
            "            m_ui.DirectionalShadows.ratioEstimator.noise);",
        "global-or-custom shadow noise resolution");
    passed &= ExpectContains(
        viewer,
        "const NoiseSettings skyNoiseSettings = ResolveNoiseSettings(\n"
            "            m_ui.Noise,\n"
            "            m_ui.RayTracedSkyVisibility.noise);",
        "global-or-custom sky noise resolution");
    passed &= ExpectAbsent(
        viewer,
        "m_PreparedVisibilityBlueNoise",
        "retired worker-prepared noise vector");
    passed &= ExpectAbsent(
        viewer,
        "GenerateVisibilityBlueNoise",
        "retired runtime blue-noise generator");
    passed &= ExpectFileAbsent(
        root / "src/screen_space_directional_shadows.cpp",
        "production screen space directional shadow source");
    passed &= ExpectFileAbsent(
        root / "src/screen_space_directional_shadows_cs.hlsl",
        "production screen space directional shadow shader");
    passed &= ExpectAbsent(
        buildSystem,
        "screen_space_directional_shadows",
        "production screen space directional shadows");
    passed &= ExpectContains(
        buildSystem,
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/documentation/third-party-notices.md",
        "packaged third party notice source");
    passed &= ExpectContains(
        buildSystem,
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/third-party-notices.md",
        "packaged third party notice destination");
    passed &= ExpectContains(
        buildSystem,
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md",
        "packaged UVSR public license source");
    passed &= ExpectContains(
        buildSystem,
        "licenses/UVSR-Polyform-Noncommercial-1.0.0.md",
        "packaged UVSR public license");
    passed &= ExpectContains(
        buildSystem,
        "licenses/Donut-Third-Party-Licenses.txt",
        "packaged Donut transitive license inventory");
    passed &= ExpectContains(
        buildSystem,
        "licenses/IOLITE-AgX-MIT.txt",
        "packaged AgX implementation license");
    passed &= ExpectContains(
        buildSystem,
        "legal/samples/*.hlsl",
        "incorporated legal shader dependency tracking");

    const std::string_view representationInvalidation = ExtractSection(
        worldRepresentation,
        "void WorldSpaceRepresentation::Invalidate(",
        "void WorldSpaceRepresentation::Fail(");
    passed &= ExpectContains(
        representationInvalidation,
        "m_NextBlas < m_BlasRecords.size()",
        "TLAS invalidation during staged BLAS construction");
    passed &= ExpectContains(
        representationInvalidation,
        "m_Status.state = WorldSpaceRepresentationState::Unsupported;",
        "unsupported representation status preservation");
    passed &= ExpectContains(
        worldRepresentationHeader,
        "uint32_t lastSynchronizedFrameIndex = 0u;",
        "dynamic BLAS synchronization watermark");
    passed &= ExpectContains(
        worldRepresentation,
        "mesh.skinPrototype ? 1u : 0u",
        "skinning classification topology signature");
    passed &= ExpectContains(
        worldRepresentation,
        "mesh.isMorphTargetAnimationMesh ? 1u : 0u",
        "morph classification topology signature");
    const std::string_view dynamicBlasUpdates = ExtractSection(
        worldRepresentation,
        "bool WorldSpaceRepresentation::UpdateDynamicBlases(",
        "bool WorldSpaceRepresentation::Update(");
    passed &= ExpectContains(
        dynamicBlasUpdates,
        "IsFrameIndexNewer(",
        "inactive-period dynamic BLAS dirtiness");
    passed &= ExpectContains(
        dynamicBlasUpdates,
        "forceAll ||",
        "pre-TLAS dynamic BLAS synchronization");
    passed &= ExpectContains(
        dynamicBlasUpdates,
        "record.mesh->buffers->indexBuffer,\n"
            "                nvrhi::ResourceStates::AccelStructBuildInput",
        "dynamic BLAS index-buffer build-input transition");
    passed &= ExpectContains(
        worldRepresentationHeader,
        "std::vector<SourceInstanceTopology> m_SourceInstanceTopology;",
        "exact source instance topology ownership");
    passed &= ExpectContains(
        worldRepresentation,
        "instance.get() != expected.instance ||",
        "exact scene-instance topology comparison");
    passed &= ExpectContains(
        worldRepresentation,
        "snapshot.instanceId != instanceId",
        "TLAS instance-ID change detection");
    passed &= ExpectContains(
        worldRepresentation,
        "geometry->material->domain == MaterialDomain::Opaque",
        "material-aware opaque geometry classification");
    passed &= ExpectAbsent(
        worldRepresentation,
        "InstanceFlags::ForceOpaque",
        "consumer-neutral TLAS instance flags");
    const std::string_view beginWorldGeneration = ExtractSection(
        worldRepresentation,
        "bool WorldSpaceRepresentation::BeginGeneration(",
        "bool WorldSpaceRepresentation::BuildNextBlas(");
    const std::string_view stagedBlasBuild = ExtractSection(
        worldRepresentation,
        "bool WorldSpaceRepresentation::BuildNextBlas(",
        "bool WorldSpaceRepresentation::BuildOrUpdateTlas(");
    passed &= ExpectAbsent(
        beginWorldGeneration,
        "createAccelStruct(",
        "eager BLAS allocation during generation planning");
    passed &= ExpectContains(
        stagedBlasBuild,
        "m_Device->createAccelStruct(record.description)",
        "one-at-a-time BLAS allocation");
    const std::string_view updateWorldRepresentation = ExtractSection(
        worldRepresentation,
        "bool WorldSpaceRepresentation::Update(",
        "\n}");
    passed &= ExpectOrdered(
        updateWorldRepresentation,
        "if (!TopologyMatches())",
        "WorldSpaceRepresentationState::BuildingBlas",
        "topology revalidation before staged build publication");
    passed &= ExpectOrdered(
        updateWorldRepresentation,
        "commandList, frameIndex, true,",
        "BuildOrUpdateTlas(commandList, false)",
        "dynamic BLAS synchronization before initial TLAS build");
    passed &= ExpectContains(
        worldRepresentationHeader,
        "uint64_t generation = 0u;",
        "world-representation consumer generation serial");
    passed &= ExpectAbsent(
        worldRepresentationHeader,
        "contentRevision",
        "retired shadow-private content revision");
    passed &= ExpectAbsent(
        worldRepresentation,
        "contentRevision",
        "retired TLAS private-history publication revision");
    passed &= ExpectContains(
        viewer,
        "worldRepresentationGenerationBefore",
        "Heitz binding retirement generation observation");
    passed &= ExpectAbsent(
        viewer,
        "worldRepresentationContentRevisionBefore",
        "retired dynamic-occluder private-history observation");
    passed &= ExpectAbsent(
        viewer,
        "shadowInputs.motionVectors",
        "Heitz-private motion-vector input");
    passed &= ExpectAbsent(
        viewer,
        "HeitzRatioEstimatorRequiresPrivateHistory(",
        "Heitz-private temporal policy");
    passed &= ExpectContains(
        viewer,
        "m_SunLight.get(),\n"
            "                        shadowNoiseSettings,\n"
            "                        shadowNoise.texture,\n"
            "                        shadowNoiseSettings.animate\n"
            "                            ? uint32_t(m_HeitzRatioEstimatorPhase)\n"
            "                            : 0u,\n"
            "                        m_SceneDiagonal);",
        "resolved texture-backed Heitz sampling phase input");
    passed &= ExpectContains(
        viewer,
        "if (heitzShadowResult.dispatched &&\n"
            "            heitzShadowResult.stochastic &&\n"
            "            shadowNoiseSettings.animate)",
        "actual stochastic-dispatch phase commit");
    passed &= ExpectAbsent(
        viewer,
        "m_HeitzRatioEstimatorShadowPass->ResetHistory();",
        "private shadow-history reset");
    passed &= ExpectContains(
        viewer,
        "const bool motionVectorsRequired =\n"
            "            m_ui.UsesLongTermTemporalAA() ||\n"
            "            (visibilityResourcesRequired && sampleCount > 1u);",
        "load-time motion topology without Heitz history");
    passed &= ExpectContains(
        viewer,
        "const bool motionVectorsRequired =\n"
            "                temporalAARequired ||\n"
            "                denoisingMotionVectorsRequired ||\n"
            "                (visibilityResourcesRequired && sampleCount > 1u);",
        "runtime motion topology for temporal AA and denoising without private Heitz history");
    const std::string_view renderTargetReplacement = ExtractSection(
        viewer,
        "const bool antiAliasingTopologyChanged =",
        "const bool refreshTemporalPass =");
    passed &= ExpectOrdered(
        renderTargetReplacement,
        "m_HeitzRatioEstimatorShadowPass->ResetBindingCache();",
        "m_RenderTargets->Init(",
        "Heitz input binding retirement before render-target replacement");
    const std::string_view ensureHeitzResources = ExtractSection(
        heitzShadows,
        "bool HeitzRatioEstimatorShadowPass::EnsureResources(",
        "bool HeitzRatioEstimatorShadowPass::EnsureBindingSets(");
    passed &= ExpectContains(
        ensureHeitzResources,
        "const bool modulationSizeMatches = m_OutputModulation &&",
        "independent Heitz modulation output reuse gate");
    passed &= ExpectContains(
        ensureHeitzResources,
        "if (!modulationSizeMatches)",
        "Heitz modulation output replacement gate");
    passed &= ExpectContains(
        ensureHeitzResources,
        "if (outputHitDistance && !hitDistanceSizeMatches)",
        "optional Heitz hit distance output replacement gate");
    passed &= ExpectContains(
        ensureHeitzResources,
        "else if (!outputHitDistance && m_OutputHitDistance)",
        "disabled Heitz hit distance output retirement");
    passed &= ExpectContains(
        ensureHeitzResources,
        "if (!outputModulation)",
        "Heitz output allocation validation");
    passed &= ExpectAbsent(
        ensureHeitzResources,
        "History",
        "Heitz private-history allocation");
    const std::string_view ensureHeitzBindings = ExtractSection(
        heitzShadows,
        "bool HeitzRatioEstimatorShadowPass::EnsureBindingSets(",
        "HeitzRatioEstimatorShadowResult");
    passed &= ExpectContains(
        ensureHeitzBindings,
        "if (m_BindingSets[variant])\n"
            "            return true;",
        "per hit distance variant Heitz binding fast path");
    passed &= ExpectContains(
        ensureHeitzBindings,
        "m_BindingSets[variant] = m_Device->createBindingSet(",
        "per hit distance variant Heitz binding creation");
    passed &= ExpectContains(
        ensureHeitzBindings,
        "ClearBindingSets();",
        "stale Heitz binding retirement before replacement");
    passed &= ExpectAbsent(
        ensureHeitzBindings,
        "ResetHistory();",
        "private history invalidation before binding replacement");

    passed &= ExpectContains(
        viewer,
        "SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)",
        "required High process-priority request");
    passed &= ExpectOrdered(
        viewer,
        "int WINAPI WinMain(",
        "ApplyProcessPriority();",
        "process-priority request before renderer startup");
    passed &= ExpectContains(
        donutLoadingAppOverride,
        "ProcessRenderingThreadCommands(*m_CommonPasses, 4.f)",
        "soft texture-finalization frame budget");
    passed &= ExpectContains(
        donutLoadingAppOverride,
        "SceneRetirementState::ArmQuery",
        "deferred old-scene retirement state");
    passed &= ExpectContains(
        donutLoadingAppOverride,
        "pollEventQuery(m_SceneRetirementQuery)",
        "nonblocking old-scene graphics retirement");
    passed &= ExpectOrdered(
        donutLoadingAppOverride,
        "ProcessPendingSceneRetirement(framebuffer)",
        "ProcessRenderingThreadCommands(*m_CommonPasses, 4.f)",
        "scene retirement before texture finalization");
    passed &= ExpectContains(
        donutLoadingAppOverride,
        "std::atomic_bool m_SceneLoadFinished = false;",
        "async scene completion independent from success");
    const std::string_view applicationBaseDestructor = ExtractSection(
        donutLoadingAppOverride,
        "ApplicationBase::~ApplicationBase()",
        "void ApplicationBase::WaitForSceneLoadingThread()");
    passed &= ExpectContains(
        applicationBaseDestructor,
        "WaitForSceneLoadingThread();",
        "defensive base-destructor scene-worker join");
    const std::string_view waitForSceneLoadingThread = ExtractSection(
        donutLoadingAppOverride,
        "void ApplicationBase::WaitForSceneLoadingThread()",
        "void ApplicationBase::Render(");
    passed &= ExpectOrdered(
        waitForSceneLoadingThread,
        "m_SceneLoadingThread->join();",
        "m_SceneLoadingThread.reset();",
        "scene-worker join before thread-handle release");
    const std::string_view viewerDestructor = ExtractSection(
        viewer,
        "~UvsrSceneViewer() override",
        "std::shared_ptr<vfs::IFileSystem> GetRootFs() const");
    passed &= ExpectContains(
        viewerDestructor,
        "WaitForSceneLoadingThread();",
        "derived scene-worker join before viewer member destruction");
    const std::string_view failedGuiInitialization = ExtractSection(
        viewer,
        "if (!gui->Init(demo->GetShaderFactory()))",
        "deviceManager->AddRenderPassToBack(demo.get());");
    passed &= ExpectOrdered(
        failedGuiInitialization,
        "gui.reset();",
        "demo.reset();",
        "failed GUI initialization releases UI ownership before the viewer");
    passed &= ExpectOrdered(
        failedGuiInitialization,
        "demo.reset();",
        "deviceManager->Shutdown();",
        "failed GUI initialization joins the scene worker before device shutdown");
    passed &= ExpectOrdered(
        failedGuiInitialization,
        "deviceManager->Shutdown();",
        "delete deviceManager;",
        "failed GUI initialization shuts down before deleting the device manager");
    const std::string_view failedAsyncSceneLoad = ExtractSection(
        donutLoadingAppOverride,
        "if (m_SceneLoadingThread && m_SceneLoadFinished && "
            "!m_SceneLoaded)",
        "// Leave most of each loading frame available to presentation and UI.");
    passed &= ExpectOrdered(
        failedAsyncSceneLoad,
        "WaitForSceneLoadingThread();",
        "m_TextureCache->Reset();",
        "failed async scene loads joined before leaving busy state");
    passed &= ExpectOrdered(
        donutLoadingAppOverride,
        "loaded = LoadScene(fs, sceneFileName);",
        "catch (...)",
        "async scene worker catches every exception");
    passed &= ExpectOrdered(
        donutLoadingAppOverride,
        "catch (...)",
        "m_SceneLoaded = loaded;",
        "async scene failure publication after exception handling");
    passed &= ExpectOrdered(
        donutLoadingAppOverride,
        "m_SceneLoaded = loaded;",
        "m_SceneLoadFinished = true;",
        "async scene result before completion publication");
    passed &= ExpectOrdered(
        donutEngineOverride,
        "std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);",
        "m_TexturesToFinalize.swap(discardedFinalizations);",
        "pending texture finalizations cleared under their mutex");
    const std::string_view beginLoadingScene = ExtractSection(
        donutLoadingAppOverride,
        "void ApplicationBase::BeginLoadingScene(",
        "void ApplicationBase::StartPendingSceneLoad()");
    passed &= ExpectAbsent(
        beginLoadingScene,
        "+    GetDevice()->waitForIdle();",
        "blocking wait in ordinary scene switching");
    passed &= ExpectContains(
        donutLoadingOverride,
        "m_completion.wait(lock, [this] { return m_pendingTasks.load() == 0; });",
        "blocking scene task completion wait");
    passed &= ExpectContains(
        donutLoadingOverride,
        "-        std::this_thread::yield();",
        "retired busy scene task completion wait");
    passed &= ExpectContains(
        donutLoadingOverride,
        "bool Scene::ProcessLoadingBuffers(",
        "bounded Donut mesh-upload state machine");
    passed &= ExpectContains(
        donutLoadingOverride,
        "nvrhi::IBuffer* trackedUploadBuffer = nullptr;",
        "per-command-list staged mesh-buffer tracking");
    passed &= ExpectContains(
        donutLoadingOverride,
        "commandList->beginTrackingBufferState(\n+                destination,\n+                nvrhi::ResourceStates::Common);",
        "known mesh-buffer state at the start of each upload frame");
    passed &= ExpectContains(
        donutLoadingOverride,
        "commandList->setBufferState(\n+                trackedUploadBuffer,\n+                nvrhi::ResourceStates::Common);",
        "known mesh-buffer state at the end of each partial upload frame");
    passed &= ExpectContains(
        donutLoadingOverride,
        "return pauseUpload();",
        "partial mesh-upload exits routed through state finalization");
    passed &= ExpectContains(
        donutLoadingOverride,
        "(buffers->indexBuffer &&\n+                 upload.attributeByteOffset == 0)",
        "partially written index buffers must resume instead of being skipped");
    passed &= ExpectAbsent(
        donutLoadingOverride,
        "buffers->indexData.empty() || buffers->indexBuffer",
        "index-buffer existence guard that skips partial staged uploads");
    passed &= ExpectContains(
        donutLoadingOverride,
        "if (buffers->vertexBuffer &&\n+            upload.attributeStage == 1 &&",
        "pre-existing shared vertex buffers must not be uploaded twice");
    passed &= ExpectContains(
        donutLoadingOverride,
        "if (upload.finalizationStage == 0u)",
        "separate mesh-buffer finalization frame");
    passed &= ExpectOrdered(
        donutLoadingOverride,
        "CreateMeshBuffers(commandList);",
        "Refresh(commandList, frameIndex);",
        "separated Donut scene finalization phases");
    passed &= ExpectContains(
        viewer,
        "scene->LoadWithThreadPool(fileName, &threadPool)",
        "explicit capped scene-import pool");
    passed &= ExpectOrdered(
        viewer,
        "if (!m_PendingSceneCpuState || !m_Scene)",
        "Super::SceneLoaded();",
        "prepared scene handoff validation before publication");
    passed &= ExpectContains(
        viewer,
        "ProcessRenderPassPreparationStep()",
        "loading-frame render-pass preparation state machine");
    passed &= ExpectContains(
        viewer,
        "ScenePreparationStage::RenderPasses",
        "scene activation gated on render-pass preparation");
    passed &= ExpectContains(
        viewer,
        "RecordLoadingPresentationFrame();",
        "loading presentation frame-gap telemetry");
    passed &= ExpectContains(
        viewer,
        "maximum presentation gap %.2f ms",
        "reported loading presentation maximum gap");
    passed &= ExpectContains(
        visibilityPassHeader,
        "bool deferPipelineCreation = false",
        "standalone-compatible deferred visibility pipeline creation");
    passed &= ExpectContains(
        visibilityPass,
        "bool ScreenSpaceVisibilityPass::PreparePipelinesStep()",
        "one-step visibility pipeline preparation");
    passed &= ExpectContains(
        visibilityPassHeader,
        "return firstTraceMs + reconstructionMs + compositionMs;",
        "visibility effect cost excludes nested denoiser callbacks");
    passed &= ExpectContains(
        viewer,
        "drawScreenSpaceVisibilityTiming(label);",
        "complete renderer table uses exclusive visibility timing");
    passed &= ExpectContains(
        viewer,
        "{ \"Visibility Lighting Preparation\",\n"
            "                            RendererTimingStage::"
            "VisibilityLightingPreparation }",
        "multisample Visibility preparation has its own timing row");
    for (const std::string_view retiredVisibilitySurface : {
            std::string_view("ResetHistory"),
            std::string_view("Temporal"),
            std::string_view("DepthHierarchy"),
            std::string_view("LaterBounce"),
            std::string_view("FusedResolve"),
            std::string_view("VisibilityPerformanceProfile"),
            std::string_view("VisibilityVerificationProfile") })
    {
        passed &= ExpectAbsent(
            visibilityPass,
            retiredVisibilitySurface,
            "retired visibility history, planner, or multi-bounce runtime");
        passed &= ExpectAbsent(
            visibilityPassHeader,
            retiredVisibilitySurface,
            "retired visibility history, planner, or multi-bounce API");
    }
    passed &= ExpectContains(
        pbrDeferredLightingPassHeader,
        "bool deferPipelineCreation = false",
        "standalone-compatible deferred-lighting pipeline creation");
    passed &= ExpectContains(
        pbrDeferredLightingPass,
        "bool PbrDeferredLightingPass::PreparePipelinesStep()",
        "one-step deferred-lighting pipeline preparation");
    passed &= ExpectContains(
        pbrDeferredLightingPass,
        "while (!PreparePipelinesStep())",
        "default eager deferred-lighting pipeline preparation");
    passed &= ExpectContains(
        msaaVisibilityResolvePassHeader,
        "bool deferPipelineCreation = false",
        "standalone-compatible MSAA visibility-resolve pipeline creation");
    passed &= ExpectContains(
        msaaVisibilityResolvePass,
        "bool MsaaVisibilityResolvePass::PreparePipelinesStep()",
        "one-step MSAA visibility-resolve pipeline preparation");
    passed &= ExpectContains(
        temporalAaPassHeader,
        "bool deferPipelineCreation = false",
        "standalone-compatible temporal-AA pipeline creation");
    passed &= ExpectContains(
        temporalAaPass,
        "bool TemporalAAPass::PreparePipelinesStep()",
        "one-step temporal-AA pipeline preparation");
    passed &= ExpectContains(
        temporalAaPass,
        "while (!PreparePipelinesStep())",
        "default eager temporal-AA pipeline preparation");
    passed &= ExpectContains(
        temporalAaPass,
        "bool TemporalAAPass::PrepareForRender(",
        "exact temporal-AA frame permutation preflight");
    passed &= ExpectContains(
        temporalAaPass,
        "const auto rejectFrame = [&]()",
        "preflight-failure history retirement");
    passed &= ExpectContains(
        temporalAaPassHeader,
        "bool DidRenderThisFrame() const",
        "actual temporal-AA dispatch result");
    passed &= ExpectContains(
        viewer,
        "RenderPassPreparationStage::DeferredLightingPipelines",
        "staged deferred-lighting pipeline preparation");
    passed &= ExpectContains(
        viewer,
        "m_PbrDeferredLightingPass->PreparePipelinesStep()",
        "deferred-lighting loading-frame pipeline step");
    passed &= ExpectContains(
        viewer,
        "RenderPassPreparationStage::MsaaVisibilityResolvePipelines",
        "staged MSAA visibility-resolve pipeline preparation");
    passed &= ExpectContains(
        viewer,
        "m_MsaaVisibilityResolvePass->PreparePipelinesStep()",
        "MSAA visibility-resolve loading-frame pipeline step");
    passed &= ExpectContains(
        viewer,
        "RenderPassPreparationStage::TemporalAAPipelines",
        "staged temporal-AA pipeline preparation");
    passed &= ExpectContains(
        viewer,
        "m_TemporalAAPass->PreparePipelinesStep()",
        "temporal-AA loading-frame pipeline step");
    passed &= ExpectContains(
        viewer,
        "RenderPassPreparationStage::FastApproximateAA",
        "staged Fast Approximate pass preparation");
    passed &= ExpectContains(
        viewer,
        "CreateFastApproximateAAPass();",
        "lazy Fast Approximate pass creation");
    const std::string_view fastApproximateLifetime = ExtractSection(
        viewer,
        "if (fastApproximateAARequired && !m_FastApproximateAAPass)",
        "m_ui.ShaderReloadRequested = false;");
    passed &= ExpectOrdered(
        fastApproximateLifetime,
        "m_Cmaa2Pass->UpdateSourceColor(",
        "m_FastApproximateAAPass.reset();",
        "retained CMAA2 source release before Fast Approximate teardown");

    const std::string_view prepareRadiance = ExtractSection(
        imageBasedLighting,
        "ImageBasedLightingEnvironment::PrepareRadiance(",
        "void ImageBasedLightingEnvironment::StagePreparedRadiance(");
    passed &= ExpectContains(
        prepareRadiance,
        "stbi_loadf(",
        "worker-callable HDR decode");
    passed &= ExpectContains(
        prepareRadiance,
        "imported.radianceFaces.resize(",
        "worker-callable radiance cube resampling");
    const std::string_view rebuildRadiance = ExtractSection(
        imageBasedLighting,
        "bool ImageBasedLightingEnvironment::RebuildRadiance(",
        "bool ImageBasedLightingEnvironment::Update(");
    passed &= ExpectAbsent(
        rebuildRadiance,
        "SampleLatLongBilinear(",
        "render-thread HDR cube resampling");
    passed &= ExpectContains(
        imageBasedLightingHeader,
        "IsPreparedRadianceReady() const",
        "staged IBL readiness gate");
    const std::string_view advancePreparedRadiance = ExtractSection(
        imageBasedLighting,
        "bool ImageBasedLightingEnvironment::AdvancePreparedRadiance(",
        "bool ImageBasedLightingEnvironment::RebuildRadiance(");
    passed &= ExpectContains(
        advancePreparedRadiance,
        "case PreparedRadianceGpuStage::RadianceFaceUpload:",
        "per-face staged IBL upload");
    passed &= ExpectContains(
        advancePreparedRadiance,
        "case PreparedRadianceGpuStage::SpecularMipGeneration:",
        "per-mip staged IBL prefilter");

    passed &= ExpectContains(
        flashlightShared,
        "struct FlashlightBeamProfile",
        "explicit first-party flashlight beam transport");
    passed &= ExpectContains(
        flashlightShared,
        "static_assert(sizeof(FlashlightBeamProfile) == 48u",
        "48-byte flashlight beam profile layout");
    passed &= ExpectContains(
        flashlight,
        "ResolveFlashlightBeamProfile(",
        "flashlight profile construction");
    passed &= ExpectContains(
        deferredConstants,
        "FlashlightBeamProfile flashlightBeamProfile;",
        "deferred flashlight profile binding");
    passed &= ExpectContains(
        directLightVisibility,
        "DirectLightVisibility flashlight;\n"
            "        DirectLightVisibility sun;",
        "independent flashlight and sun visibility slots");
    passed &= ExpectAbsent(
        viewer,
        "class FlashlightSpotLight",
        "retired flashlight light subclass");
    passed &= ExpectAbsent(
        viewer,
        "m_FlashlightHotspot",
        "retired duplicate flashlight lobe");
    passed &= ExpectAbsent(
        viewer,
        "m_FlashlightShadowMap",
        "retired private flashlight shadow map");

    for (const std::string_view retiredEmissiveSourceSurface : {
            std::string_view("includeEmissive"),
            std::string_view("emissiveSourceGain"),
            std::string_view("sourceEmissive"),
            std::string_view("UVSR_LIGHTING_SOURCE_EMISSIVE") })
    {
        passed &= ExpectAbsent(
            viewer,
            retiredEmissiveSourceSurface,
            "retired emissive GI source renderer state");
        passed &= ExpectAbsent(
            deferredLighting,
            retiredEmissiveSourceSurface,
            "retired emissive GI source shader");
        passed &= ExpectAbsent(
            deferredConstants,
            retiredEmissiveSourceSurface,
            "retired emissive GI source constants");
        passed &= ExpectAbsent(
            visibilityConstants,
            retiredEmissiveSourceSurface,
            "retired emissive GI source visibility constants");
    }
    passed &= ExpectContains(
        deferredLighting,
        "finalLinearHdr = max(diffuse + specular + "
            "gbuffer.material.emissive, 0.0f);",
        "visible authored emissive retention");

    passed &= ExpectContains(
        viewer,
        "std::unique_ptr<PbrDeferredLightingPass> m_PbrDeferredLightingPass;",
        "singular deferred PBR pass ownership");
    for (const std::string_view retiredRendererSurface : {
            std::string_view("m_ForwardPass"),
            std::string_view("RendererMode::Forward"),
            std::string_view("SparseVirtualShadowMap"),
            std::string_view("DiagnosticCascadedShadowMap"),
            std::string_view("VisibilityBenchmark"),
            std::string_view("VisibilityPerformanceProfile") })
    {
        passed &= ExpectAbsent(
            viewer,
            retiredRendererSurface,
            "retired renderer mode, shadow engine, or visibility planner");
    }
    passed &= ExpectContains(
        viewer,
        "virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override\n"
        "    {\n"
        "        if (m_SceneGpuUploadPending)",
        "bounded scene upload gate");
    const std::string_view refresh = ExtractSection(
        viewer,
        "void RefreshAntiAliasingTargetPasses()",
        "void BeginRenderPassPreparation(bool waitForIbl)");
    passed &= ExpectContains(
        refresh,
        "m_PbrDeferredLightingPass->ResetBindingCache();",
        "AA-only target refresh");
    passed &= ExpectContains(
        refresh,
        "m_ScreenSpaceVisibilityPass->ResetBindingCache();",
        "AA-only target refresh");
    passed &= ExpectAbsent(
        refresh,
        "m_ScreenSpaceVisibilityPass->ResetHistory();",
        "visibility-owned temporal history invalidation");
    passed &= ExpectContains(
        refresh,
        "m_FastApproximateAAPass->UpdateSourceColor(",
        "Fast Approximate resource retention");
    passed &= ExpectContains(
        refresh,
        "m_Cmaa2Pass->UpdateSourceColor(",
        "CMAA2 resource retention");
    passed &= ExpectAbsent(
        refresh,
        "std::make_unique<ScreenSpaceVisibilityPass>",
        "AA-only target refresh");

    const std::string_view createPasses = ExtractSection(
        viewer,
        "bool ProcessRenderPassPreparationStep()",
        "void CreateRenderPasses()");
    passed &= ExpectContains(
        createPasses,
        "std::make_unique<PbrDeferredLightingPass>",
        "deferred PBR pass construction");

    passed &= ExpectAbsent(
        refresh,
        "std::make_unique<PbrDeferredLightingPass>",
        "AA-only target refresh");

    const std::string_view sunShadowLifetime = ExtractSection(
        viewer,
        "void EnsureHeitzRatioEstimatorShadowPass()",
        "void EnsureRayTracedFlashlightShadowPass()");
    passed &= ExpectContains(
        sunShadowLifetime,
        "!m_ui.Representation.allowRayTraversal",
        "sun shadow ray-traversal master gate");
    const std::string_view flashlightShadowLifetime = ExtractSection(
        viewer,
        "void EnsureRayTracedFlashlightShadowPass()",
        "void EnsureRayTracedSkyVisibilityPass()");
    passed &= ExpectContains(
        flashlightShadowLifetime,
        "!m_ui.Representation.allowRayTraversal",
        "flashlight ray-traversal master gate");
    passed &= ExpectContains(
        flashlightShadowLifetime,
        "std::make_unique<RayTracedFlashlightShadowPass>(",
        "lazy ray traced flashlight shadow construction");
    const std::string_view applyCameraPose = ExtractSection(
        viewer,
        "void ApplyCameraPose(",
        "void ApplyCameraPreset(");
    passed &= ExpectOrdered(
        applyCameraPose,
        "m_StaticCamera.SetExactPose(",
        "ResetFlashlightMotion();",
        "camera teleport flashlight reset after all camera poses");
    passed &= ExpectOrdered(
        applyCameraPose,
        "ResetFlashlightMotion();",
        "m_PreviousView.reset();",
        "camera teleport flashlight reset before view history");
    const std::string_view pointThirdPersonCamera = ExtractSection(
        viewer,
        "void PointThirdPersonCameraAt(",
        "std::shared_ptr<TextureCache> GetTextureCache()");
    passed &= ExpectOrdered(
        pointThirdPersonCamera,
        "m_ThirdPersonCamera.ResetZoomReferenceDistance(distance);",
        "ResetFlashlightMotion();",
        "focus teleport flashlight collision-cache reset");
    const std::string_view skyVisibilityLifetime = ExtractSection(
        viewer,
        "void EnsureRayTracedSkyVisibilityPass()",
        "void UpdateImageBasedLighting(");
    passed &= ExpectContains(
        skyVisibilityLifetime,
        "!m_ui.Representation.allowRayTraversal",
        "sky visibility ray-traversal master gate");
    const std::string_view flashlightPresentation = ExtractSection(
        viewer,
        "void ApplyFlashlightPresentation()",
        "void UpdateFlashlightAnimation(float elapsedSeconds)");
    passed &= ExpectContains(
        flashlightPresentation,
        "ResolveFlashlightLobeSettings(settings)",
        "flashlight lobe settings resolution");
    passed &= ExpectContains(
        flashlightPresentation,
        "m_Flashlight->radius =\n"
            "            ResolveFlashlightEmitterRadiusMeters(\n"
            "                settings.angularSizeDegrees);",
        "selectable analytical flashlight emitter radius");
    passed &= ExpectContains(
        flashlightPresentation,
        "const float3 color(\n"
            "            settings.colorLinearRed,\n"
            "            settings.colorLinearGreen,\n"
            "            settings.colorLinearBlue);\n"
            "        m_Flashlight->color = color;",
        "flashlight sanitized color reaches the analytical light");
    passed &= ExpectAbsent(
        flashlightPresentation,
        "shadowMap",
        "retired raster flashlight shadow association");

    const std::string_view flashlightAnimation = ExtractSection(
        viewer,
        "void UpdateFlashlightAnimation(float elapsedSeconds)",
        "static float3 ClampFlashlightAimLag(");
    passed &= ExpectContains(
        flashlightAnimation,
        "m_FlashlightTransition = AdvanceFlashlightTransition(",
        "bounded flashlight transition helper");
    passed &= ExpectOrdered(
        flashlightAnimation,
        "AdvanceFlashlightTransition(",
        "ApplyFlashlightPresentation();",
        "flashlight transition before presentation");

    const std::string_view resetAllRendererSettings = ExtractSection(
        viewer,
        "void ResetAllRendererSettings()",
        "void SynchronizeCameraInput()");
    passed &= ExpectContains(
        resetAllRendererSettings,
        "m_ui.Flashlight = DefaultFlashlightSettings;",
        "global reset restores the pure-white flashlight defaults");
    passed &= ExpectContains(
        viewer,
        "flashlight.colorLinearRed =\n"
            "                                defaults.colorLinearRed;\n"
            "                            flashlight.colorLinearGreen =\n"
            "                                defaults.colorLinearGreen;\n"
            "                            flashlight.colorLinearBlue =\n"
            "                                defaults.colorLinearBlue;",
        "flashlight Color reset restores every pure-white channel");

    const std::string_view flashlightReset = ExtractSection(
        viewer,
        "void ResetFlashlightMotion()",
        "void ApplyFlashlightPresentation()");
    passed &= ExpectContains(
        flashlightReset,
        "m_FlashlightDesiredPosition = 0.f;",
        "flashlight collision target reset");
    passed &= ExpectContains(
        flashlightReset,
        "m_FlashlightCollisionRadius = 0.f;",
        "flashlight collision radius reset");
    passed &= ExpectContains(
        flashlightReset,
        "m_FlashlightCollisionInitialized = false;",
        "flashlight collision cache reset");
    for (const std::string_view retiredState : {
            std::string_view("Receiver"),
            std::string_view("MountExtension"),
            std::string_view("MountAdjustment"),
            std::string_view("MountRecovery"),
            std::string_view("PendingAction") })
    {
        passed &= ExpectAbsent(
            flashlightReset,
            retiredState,
            "retired flashlight centering state remains absent from reset");
    }

    const std::string_view flashlightMotion = ExtractSection(
        viewer,
        "void UpdateFlashlightMotion(float elapsedSeconds)",
        "void UpdateFlashlightTransform()");
    passed &= ExpectContains(
        flashlightMotion,
        "if (!settings.realisticLens)",
        "simple flashlight camera-lock path");
    passed &= ExpectContains(
        flashlightMotion,
        "ResolveFlashlightMountPose(\n"
            "                settings.cameraHorizontalOffsetMeters,\n"
            "                settings.cameraVerticalOffsetMeters);",
        "flashlight configurable tested mount-pose resolution");
    passed &= ExpectContains(
        flashlightMotion,
        "cameraRight *\n"
            "                mount.positionRightMeters +",
        "flashlight off-axis horizontal mount");
    passed &= ExpectContains(
        flashlightMotion,
        "cameraUp *\n"
            "                mount.positionUpMeters;",
        "flashlight off-axis vertical mount");
    passed &= ExpectContains(
        flashlightMotion,
        "ResolveFlashlightCollisionRadiusMeters(",
        "flashlight emitter-aware collision radius");
    passed &= ExpectContains(
        flashlightMotion,
        "m_CameraCollisionWorld.ResolveSphere(",
        "flashlight stationary overlap repair");
    passed &= ExpectContains(
        flashlightMotion,
        "m_CameraCollisionWorld.ResolveSphere(\n"
            "                cameraPosition,\n"
            "                desiredFlashlightPosition - cameraPosition,",
        "flashlight activation begins from a camera-side safe sphere");
    passed &= ExpectContains(
        flashlightMotion,
        "m_CameraCollisionWorld.MoveSphere(",
        "flashlight continuous collision sweep");
    passed &= ExpectOrdered(
        flashlightMotion,
        "const float3 collisionStart =\n"
            "                m_CameraCollisionWorld.ResolveSphere(",
        "flashlightPosition = m_CameraCollisionWorld.MoveSphere(\n"
            "                collisionStart,\n"
            "                desiredFlashlightPosition,",
        "first activation repairs the camera-side start before sweeping to the mount");
    passed &= ExpectContains(
        flashlightMotion,
        "const float3 mountedDirection = normalize(\n"
            "            cameraDirection * mount.directionForward +",
        "flashlight collision does not rewrite the authored camera aim");
    passed &= ExpectContains(
        flashlightMotion,
        "m_FlashlightDesiredPosition = desiredFlashlightPosition;\n"
            "        m_FlashlightCollisionRadius = collisionRadius;\n"
            "        m_FlashlightCollisionInitialized = true;",
        "flashlight collision cache persists only physical inputs");
    for (const std::string_view retiredFeedback : {
            std::string_view("GetRayTravelFraction"),
            std::string_view("GetSphereTravelFraction"),
            std::string_view("receiver"),
            std::string_view("Receiver"),
            std::string_view("mountExtension"),
            std::string_view("MountExtension"),
            std::string_view("MountAdjustment"),
            std::string_view("MountRecovery"),
            std::string_view("proximity") })
    {
        passed &= ExpectAbsent(
            flashlightMotion,
            retiredFeedback,
            "retired flashlight receiver-driven centering stays absent");
    }
    passed &= ExpectOrdered(
        flashlightMotion,
        "flashlightPosition = m_CameraCollisionWorld.MoveSphere(",
        "m_FlashlightResolvedPosition = flashlightPosition;",
        "flashlight collision resolves before position publication");
    passed &= ExpectContains(
        flashlightMotion,
        "m_FlashlightResolvedDirection = mountedDirection;",
        "simple flashlight authored camera aim");
    passed &= ExpectContains(
        flashlightMotion,
        "m_FlashlightResolvedRight = mountedRight;",
        "simple flashlight projected camera roll");
    passed &= ExpectContains(
        flashlightMotion,
        "GetFlashlightAimCorrectionBlend(",
        "realistic flashlight correction");
    passed &= ExpectContains(
        flashlightMotion,
        "InterpolateFlashlightAim(",
        "realistic flashlight spherical aim interpolation");
    passed &= ExpectContains(
        flashlightMotion,
        "ClampFlashlightAimLag(",
        "realistic flashlight bounded lag");
    passed &= ExpectContains(
        flashlightMotion,
        "ResolveFlashlightSwayOffset(",
        "realistic flashlight bounded sway");
    const std::string_view flashlightSway = ExtractSection(
        flashlightMotion,
        "m_FlashlightSwayTime = AdvanceFlashlightSwayTime(",
        "m_FlashlightPoseValid = true;");
    passed &= ExpectAbsent(
        flashlightSway,
        "m_FlashlightResolvedPosition",
        "flashlight sway cannot change physical mount position");
    passed &= ExpectAbsent(
        flashlightSway,
        "m_CameraCollisionWorld",
        "flashlight sway cannot affect physical collision");
    passed &= ExpectContains(
        flashlightMotion,
        "beamRight -=\n"
            "            m_FlashlightResolvedDirection *",
        "realistic flashlight final-basis reprojection");
    passed &= ExpectContains(
        flashlightMotion,
        "m_FlashlightResolvedRight =",
        "realistic flashlight resolved beam basis");

    const std::string_view flashlightTransform = ExtractSection(
        viewer,
        "void UpdateFlashlightTransform()",
        "void AttachFlashlightToScene()");
    passed &= ExpectContains(
        flashlightTransform,
        "m_FlashlightResolvedPosition",
        "flashlight shared resolved position");
    passed &= ExpectContains(
        flashlightTransform,
        "SetFlashlightDirectionAndRoll(\n"
            "            m_Flashlight,",
        "single flashlight direction and roll publication");

    const std::string_view flashlightAttachment = ExtractSection(
        viewer,
        "void AttachFlashlightToScene()",
        "virtual void Animate(float fElapsedTimeSeconds) override");
    passed &= ExpectContains(
        flashlightAttachment,
        "std::make_shared<SpotLight>()",
        "ordinary flashlight spot-light construction");
    passed &= ExpectContains(
        flashlightAttachment,
        "m_Flashlight->SetName(FlashlightPublicName);",
        "public flashlight light identifier");
    passed &= ExpectContains(
        flashlightAttachment,
        "m_FlashlightNode->SetName(FlashlightPublicName);",
        "public flashlight node identifier");
    passed &= ExpectAbsent(
        viewer,
        "void RenderFlashlightShadow()",
        "retired private flashlight shadow render path");

    const std::string_view renderScene = ExtractSection(
        viewer,
        "virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override",
        "std::shared_ptr<ShaderFactory> GetShaderFactory()");
    const std::string_view setupView = ExtractSection(
        viewer,
        "bool SetupView()",
        "void CaptureCurrentViewForMotionVectors()");
    passed &= ExpectContains(
        setupView,
        "m_ui.GetResolvedAntiAliasingSettings()",
        "resolved TAA jitter selection");
    passed &= ExpectContains(
        setupView,
        "antiAliasing.temporalJitterSequence,\n"
            "                m_AntiAliasingPhase",
        "selected TAA jitter sequence and phase");
    passed &= ExpectContains(
        setupView,
        "planarView->SetPixelOffset(float2(jitter.x, jitter.y));",
        "selected subpixel camera offset");
    passed &= ExpectOrdered(
        renderScene,
        "m_AgxToneMappingPass->Render(",
        "m_FastApproximateAAPass->Render(",
        "Fast Approximate post-tone-map order");
    passed &= ExpectOrdered(
        renderScene,
        "m_FastApproximateAAPass->Render(",
        "m_Cmaa2Pass->Render(",
        "Fast Approximate before morphological AA");
    passed &= ExpectContains(
        renderScene,
        "BeginRendererStage(RendererTimingStage::FastApproximate);",
        "Fast Approximate timing attribution");
    passed &= ExpectContains(
        renderScene,
        "antiAliasing.fastApproximateEnabled ||",
        "temporal presentation sharpening before spatial AA");
    passed &= ExpectContains(
        renderScene,
        "const bool fastApproximateAARequired =",
        "independent Fast Approximate topology gate");

    passed &= ExpectContains(
        fastApproximateHeader,
        "class FastApproximateAAPass",
        "Fast Approximate first-party pass API");
    passed &= ExpectContains(
        fastApproximate,
        "outputDesc.format = nvrhi::Format::RGBA16_FLOAT;",
        "matching Fast Approximate display-linear output");
    passed &= ExpectContains(
        fastApproximate,
        "sourceColor == m_OutputColor.Get()",
        "Fast Approximate SRV/RT feedback rejection");
    passed &= ExpectContains(
        fastApproximate,
        "m_BindingSet = nullptr;\n"
            "            m_BoundSource = nullptr;",
        "incompatible Fast Approximate source release");
    passed &= ExpectContains(
        fastApproximate,
        "compositeView.GetNumChildViews(ViewType::PLANAR) != 1u",
        "Fast Approximate full-view fail-closed gate");
    passed &= ExpectContains(
        fastApproximateShader,
        "sqrt(dot(",
        "Fast Approximate perceptual edge classification");
    passed &= ExpectContains(
        fastApproximateShader,
        "const float lumaNearNegative = PerceptualLuma(",
        "Fast Approximate per-sample near luminance");
    passed &= ExpectContains(
        fastApproximateShader,
        "const float lumaFarPositive = PerceptualLuma(",
        "Fast Approximate per-sample far luminance");
    passed &= ExpectAbsent(
        fastApproximateShader,
        "PerceptualLuma(filtered.rgb)",
        "Fast Approximate nonlinear filtered-luminance reconstruction");
    passed &= ExpectContains(
        fastApproximateShader,
        "float4(filtered.rgb, colorCenter.a)",
        "Fast Approximate presentation alpha preservation");
    passed &= ExpectContains(
        buildSystem,
        "src/fast_approximate_aa.cpp",
        "Fast Approximate application build registration");
    passed &= ExpectContains(
        buildSystem,
        "fast_approximate_aa_ps",
        "Fast Approximate runtime shader registration");
    passed &= ExpectContains(
        buildSystem,
        "licenses/Google-Filament-FXAA-Attribution.md",
        "Fast Approximate runtime attribution packaging");
    passed &= ExpectContains(
        buildSystem,
        "licenses/BSD-2-Clause.txt",
        "Fast Approximate runtime BSD packaging");
    passed &= ExpectContains(
        attributes,
        "legal/licenses/BSD-2-Clause.txt -text -whitespace",
        "stable Fast Approximate BSD license bytes");
    passed &= ExpectOrdered(
        renderScene,
        "UpdateFlashlightTransform();",
        "m_Scene->RefreshSceneGraph(GetFrameIndex());",
        "flashlight transform publication");
    passed &= ExpectOrdered(
        renderScene,
        "lightingLights.push_back(m_Flashlight);",
        "for (const auto& light : sceneLights)",
        "flashlight light-limit priority");
    passed &= ExpectOrdered(
        renderScene,
        "m_RayTracedFlashlightShadowPass->Render(",
        "m_PbrDeferredLightingPass->Render(",
        "ray traced flashlight shadow-before-lighting order");
    passed &= ExpectOrdered(
        renderScene,
        "flashlightNoise = m_NoiseTextureLibrary->Resolve(",
        "m_RayTracedFlashlightShadowPass->Render(",
        "shared Noise resolution before flashlight shadow dispatch");
    passed &= ExpectContains(
        renderScene,
        "m_RayTracedFlashlightShadowPhase",
        "animated finite-emitter flashlight sample phase");
    passed &= ExpectContains(
        renderScene,
        "const auto& sceneLights =\n"
            "            m_SceneLightsWithoutFlashlight;",
        "settled-off flashlight exclusion");
    passed &= ExpectContains(
        renderScene,
        "const std::vector<std::shared_ptr<Light>>* submittedLights =\n"
            "            &sceneLights;",
        "settled-off zero-copy scene-light path");
    passed &= ExpectContains(
        renderScene,
        "submittedLights = &lightingLights;",
        "active flashlight-prioritized light path");
    passed &= ExpectContains(
        renderScene,
        "directLightVisibilities.flashlight = {",
        "flashlight visibility publication");

    const std::string_view loadScene = ExtractSection(
        viewer,
        "virtual bool LoadScene(",
        "virtual void SceneLoaded() override");
    passed &= ExpectContains(
        loadScene,
        "ResolveSceneLoadWorkerCount(",
        "reserved-core scene import policy");
    passed &= ExpectContains(
        loadScene,
        "engine::ThreadPool threadPool(workerCount);",
        "explicit bounded scene import pool");
    passed &= ExpectAbsent(
        loadScene,
        "scene->Load(fileName)",
        "unbounded default scene import pool");
    passed &= ExpectOrdered(
        loadScene,
        "scene->RefreshSceneGraph(0u);",
        "prepared.collisionWorld = BuildCameraCollisionWorld(",
        "worker collision transform preparation");
    passed &= ExpectOrdered(
        loadScene,
        "prepared.collisionWorld = BuildCameraCollisionWorld(",
        "m_PendingSceneCpuState.emplace(std::move(prepared));",
        "worker collision publication order");
    passed &= ExpectOrdered(
        loadScene,
        "m_PendingSceneCpuState.emplace(std::move(prepared));",
        "m_Scene = std::move(scene);",
        "complete CPU handoff publication");
    passed &= ExpectContains(
        loadScene,
        "m_ImageBasedLightingEnvironment->PrepareRadiance(",
        "worker HDR preparation");

    const std::string_view sceneLoadedHandoff = ExtractSection(
        viewer,
        "virtual void SceneLoaded() override",
        "void CompleteSceneActivation()");
    passed &= ExpectAbsent(
        sceneLoadedHandoff,
        "BuildCameraCollisionWorld(",
        "render-thread collision construction");
    passed &= ExpectAbsent(
        sceneLoadedHandoff,
        "FinishedLoading(",
        "monolithic render-thread scene upload");
    passed &= ExpectOrdered(
        sceneLoadedHandoff,
        "m_CameraCollisionWorld = std::move(",
        "ResetFlashlightMotion();",
        "new collision world invalidates flashlight collision cache");
    passed &= ExpectOrdered(
        sceneLoadedHandoff,
        "ResetFlashlightMotion();",
        "m_Scene->BeginLoadingBuffers();",
        "prepared collision installation before bounded upload");
    passed &= ExpectContains(
        sceneLoadedHandoff,
        "StagePreparedRadiance(",
        "prepared HDR activation handoff");

    const std::string_view boundedSceneUpload = ExtractSection(
        viewer,
        "void RenderSceneGpuUploadFrame(",
        "virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override");
    passed &= ExpectContains(
        boundedSceneUpload,
        "c_SceneUploadBytesPerFrame",
        "bounded scene-upload byte budget");
    passed &= ExpectContains(
        boundedSceneUpload,
        "m_Scene->ProcessLoadingBuffers(",
        "incremental scene-upload dispatch");
    passed &= ExpectOrdered(
        boundedSceneUpload,
        "m_ScenePreparationStage =\n"
            "                    ScenePreparationStage::SceneActivation;",
        "case ScenePreparationStage::SceneActivation:",
        "scene activation deferred beyond the final mesh-upload frame");
    passed &= ExpectOrdered(
        boundedSceneUpload,
        "case ScenePreparationStage::SceneActivation:",
        "CompleteSceneActivation();",
        "scene activation stage dispatch");
    passed &= ExpectOrdered(
        boundedSceneUpload,
        "CompleteSceneActivation();",
        "case ScenePreparationStage::MaterialBuffers:",
        "post-activation material-buffer loading phase");
    passed &= ExpectOrdered(
        boundedSceneUpload,
        "case ScenePreparationStage::MaterialBuffers:",
        "m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());",
        "post-activation material-buffer refresh");
    passed &= ExpectOrdered(
        boundedSceneUpload,
        "m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());",
        "ScenePreparationStage::RenderTargets;",
        "material-buffer refresh before renderer resources");

    const std::string_view sceneLoaded = ExtractSection(
        viewer,
        "virtual void SceneLoaded() override",
        "void SetWhiteWorldMode(WhiteWorldMode mode)");
    passed &= ExpectContains(
        sceneLoaded,
        "m_EditableLights.push_back(m_Flashlight);",
        "selectable flashlight editor entry");
    passed &= ExpectContains(
        sceneLoaded,
        "if (light && light != m_Flashlight)",
        "scene-light list flashlight exclusion");
    passed &= ExpectOrdered(
        sceneLoaded,
        "sceneInitialCamera->VerticalFovDegrees;",
        "PointThirdPersonCameraAt(",
        "descriptor camera FOV framing");
    passed &= ExpectOrdered(
        sceneLoaded,
        "PointThirdPersonCameraAt(",
        "ApplySceneInitialCamera(*sceneInitialCamera);",
        "descriptor camera pose application");
    passed &= ExpectContains(
        sceneLoaded,
        "else\n"
            "            m_CameraVerticalFov = 60.f;",
        "descriptor camera FOV reset fallback");
    passed &= ExpectContains(
        viewer,
        "m_ui.EnableAmbientFill &&\n"
            "                m_ui.EnableDiffuseIbl",
        "ambient fill diffuse renderer gate");
    passed &= ExpectContains(
        viewer,
        "m_ui.EnableAmbientFill &&\n"
            "                m_ui.EnableSpecularIbl",
        "ambient fill specular renderer gate");

    passed &= ExpectContains(
        viewer,
        "sameNonAaTopology && antiAliasingTopologyChanged",
        "AA-only topology classification");
    const std::string_view msaaSampleCountResolution = ExtractSection(
        viewer,
        "static uint32_t ResolveSupportedMsaaSampleCount(",
        "class RenderTargets : public GBufferRenderTargets");
    passed &= ExpectAbsent(
        msaaSampleCountResolution,
        "bool enablePbr",
        "retired renderer-mode MSAA parameter");
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "nvrhi::utils::ChooseFormat(",
        "MSAA exact Donut depth-format selection");
    for (const std::string_view format : {
             std::string_view("DXGI_FORMAT_R8G8B8A8_UNORM_SRGB"),
             std::string_view("DXGI_FORMAT_D24_UNORM_S8_UINT"),
             std::string_view("DXGI_FORMAT_D32_FLOAT_S8X24_UINT"),
             std::string_view("DXGI_FORMAT_D32_FLOAT"),
             std::string_view("DXGI_FORMAT_D16_UNORM") })
    {
        passed &= ExpectContains(
            msaaSampleCountResolution,
            format,
            "MSAA exact allocated format");
    }
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "supportsFormat(selectedDepthDxgiFormat, sampleCount)",
        "MSAA selected depth-format validation");
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "struct MsaaSampleCountCache",
        "MSAA per-device query cache");
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "cache.device == nativeDevice &&\n"
        "        cache.requestedSampleCount == requestedSampleCount",
        "MSAA query cache topology key");
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "return cache.resolvedSampleCount;",
        "MSAA cached per-frame resolution");
    passed &= ExpectAbsent(
        msaaSampleCountResolution,
        "std::any_of(",
        "MSAA any-depth false-positive acceptance");
    passed &= ExpectContains(
        viewer,
        ".rasterSampleCount);",
        "MSAA sample-count routing");
    const std::string_view temporalAaPassCreation = ExtractSection(
        viewer,
        "void CreateTemporalAAPass(",
        "void EnsureMsaaVisibilityResolvePass(");
    for (const std::string_view resolvedMsaaInput : {
            std::string_view("m_RenderTargets->DeferredMsaaColor.Get()"),
            std::string_view("m_RenderTargets->VisibilityDepth.Get()"),
            std::string_view("m_RenderTargets->VisibilityMotionVectors.Get()") })
    {
        passed &= ExpectContains(
            temporalAaPassCreation,
            resolvedMsaaInput,
            "single-sample temporal input for composable MSAA");
    }
    passed &= ExpectContains(
        viewer,
        "singleSurfaceVisibilityProducerEnabled ||\n"
            "                    m_ui.UsesLongTermTemporalAA()",
        "temporal MSAA closest-surface resolve gate");
    passed &= ExpectContains(
        viewer,
        "RefreshAntiAliasingTargetPasses();",
        "AA-only refresh dispatch");
    passed &= ExpectContains(
        cmaa,
        "void Cmaa2Pass::UpdateSourceColor(",
        "CMAA2 source rebinding");
    passed &= ExpectContains(
        cmaa,
        "RebuildBindingSet(sourceColor);",
        "CMAA2 source rebinding");
    passed &= ExpectContains(
        cmaa,
        "struct alignas(16) Cmaa2Constants",
        "CMAA2 runtime threshold constants");
    passed &= ExpectContains(
        cmaa,
        "static_assert(sizeof(Cmaa2Constants) == 16u);",
        "CMAA2 constant-buffer alignment");
    passed &= ExpectContains(
        cmaa,
        "constantBufferDesc.isVolatile = true;",
        "CMAA2 volatile threshold constants");
    passed &= ExpectContains(
        cmaa,
        "nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)",
        "CMAA2 threshold binding layout");
    passed &= ExpectContains(
        cmaa,
        "nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer)",
        "CMAA2 threshold binding set");
    passed &= ExpectContains(
        cmaa,
        "ClampCmaa2EdgeThreshold(settings.cmaa2EdgeThreshold)",
        "CMAA2 runtime threshold clamp");
    passed &= ExpectContains(
        cmaa,
        "commandList->writeBuffer(\n"
            "            m_ConstantBuffer, &constants, sizeof(constants));",
        "CMAA2 per-frame threshold upload");
    passed &= ExpectContains(
        cmaaHeader,
        "static_cast<uint32_t>(Cmaa2EdgeDetector::Count)",
        "CMAA2 detector permutation count");
    passed &= ExpectContains(
        cmaa,
        "m_EdgePipelines[detector]",
        "CMAA2 detector pipeline creation");
    passed &= ExpectContains(
        cmaa,
        "m_EdgePipelines[detectorIndex]",
        "CMAA2 detector pipeline selection");
    for (const std::string_view sharedPipeline : {
            std::string_view("m_CandidatePipeline"),
            std::string_view("m_ApplyPipeline"),
            std::string_view("m_DispatchArgumentPipeline") })
    {
        passed &= ExpectContains(
            cmaaHeader,
            sharedPipeline,
            "detector-independent CMAA2 pipeline");
    }
    passed &= ExpectAbsent(
        cmaaHeader,
        "c_QualityCount",
        "retired CMAA2 quality permutation count");
    passed &= ExpectAbsent(
        cmaa,
        "m_CandidatePipelines",
        "retired duplicated CMAA2 stage pipelines");
    passed &= ExpectAbsent(
        cmaa,
        "CMAA2_STATIC_QUALITY_PRESET",
        "retired CMAA2 static threshold axis");
    passed &= ExpectContains(
        cmaaShader,
        "cbuffer UvsrCmaa2Constants : register(b0)",
        "CMAA2 runtime threshold shader constants");
    passed &= ExpectContains(
        cmaaShader,
        "#define g_CMAA2_EdgeThreshold "
            "lpfloat(g_UvsrCmaa2EdgeThreshold)",
        "CMAA2 runtime threshold hook");
    passed &= ExpectContains(
        cmaaVendoredShader,
        "#ifndef g_CMAA2_EdgeThreshold",
        "pinned CMAA2 threshold override boundary");
    passed &= ExpectContains(
        renderScene,
        "displayTexture,\n                antiAliasing);",
        "resolved CMAA2 runtime settings dispatch");

    const std::string_view commandLine = ExtractSection(
        viewer,
        "bool ProcessCommandLine(",
        "bool SelectGraphicsAdapter(");
    for (const std::string_view retiredExperimentOption : {
            std::string_view("--aa-benchmark-output"),
            std::string_view("--aa-rectification"),
            std::string_view("--visibility-benchmark"),
            std::string_view("--visibility-implementation-profile"),
            std::string_view("--diagnostic-csm-"),
            std::string_view("--svsm-motion-"),
            std::string_view("--benchmark-camera") })
    {
        passed &= ExpectAbsent(
            commandLine,
            retiredExperimentOption,
            "retired benchmark or planner command-line surface");
    }
    passed &= ExpectAbsent(
        viewer,
        "UpdateAntiAliasingBenchmark",
        "retired AA motion benchmark implementation");
    passed &= ExpectContains(
        viewer,
        "deviceParams.vsyncEnabled = false;",
        "uncapped renderer presentation");

    const std::string_view mouseButtonUpdate = ExtractSection(
        viewer,
        "virtual bool MouseButtonUpdate(",
        "virtual bool MouseScrollUpdate(");
    passed &= ExpectContains(
        mouseButtonUpdate,
        "button == GLFW_MOUSE_BUTTON_MIDDLE",
        "middle-button material picking");
    passed &= ExpectAbsent(
        mouseButtonUpdate,
        "GLFW_MOUSE_BUTTON_2",
        "right-button material picking");

    passed &= ExpectContains(
        viewer,
        "SubmittedTriangleCountingPass geometryPass(\n"
        "                *m_GBufferPass);",
        "deferred submitted-triangle accounting");
    passed &= ExpectContains(
        viewer,
        "m_SubmittedMainViewTriangles =\n"
        "                geometryPass.GetSubmittedTriangles();",
        "submitted-triangle publication");
    passed &= ExpectContains(
        viewer,
        "RendererTimingStage::MultisampleResolve",
        "multisample closest-surface timing attribution");
    passed &= ExpectOrdered(
        viewer,
        "if (renderDeferredMsaaLighting)\n"
            "            BeginRendererStage(RendererTimingStage::DirectLighting);",
        "m_CommandList->resolveTexture(",
        "multisample color-resolve timing attribution");
    const std::string_view msaaVisibilityPreparation = ExtractSection(
        viewer,
        "if (runScreenSpaceVisibility &&\n"
            "                    closestSurfaceResolved",
        "deferredMsaaVisibilityPending = true;");
    passed &= ExpectOrdered(
        msaaVisibilityPreparation,
        "BeginRendererStage(\n"
            "                        RendererTimingStage::"
            "VisibilityLightingPreparation);",
        "m_PbrDeferredLightingPass->Render(",
        "multisample lighting-preparation timer start");
    passed &= ExpectOrdered(
        msaaVisibilityPreparation,
        "m_PbrDeferredLightingPass->Render(",
        "EndRendererStage(\n"
            "                        RendererTimingStage::"
            "VisibilityLightingPreparation);",
        "multisample lighting-preparation timer end");
    passed &= ExpectOrdered(
        msaaVisibilityPreparation,
        "EndRendererStage(\n"
            "                        RendererTimingStage::"
            "VisibilityLightingPreparation);",
        "BeginRendererStage(\n"
            "                        RendererTimingStage::ScreenSpaceVisibility);",
        "multisample lighting excluded from Visibility timing");
    passed &= ExpectOrdered(
        msaaVisibilityPreparation,
        "BeginRendererStage(\n"
            "                        RendererTimingStage::ScreenSpaceVisibility);",
        "m_ScreenSpaceVisibilityPass->Render(",
        "multisample Visibility timer start");
    passed &= ExpectContains(
        viewer,
        "m_VisibilityLightingPreparationDispatchedThisFrame = true;",
        "multisample Visibility preparation timing availability");

    const auto expectTriangleFormat =
        [&passed](uint64_t count, const char* expected)
        {
            const std::string actual =
                uvsr::FormatTriangleCount(count);
            if (actual == expected)
                return;
            passed = false;
            std::cerr << "FAIL: triangle count " << count
                      << " formatted as '" << actual
                      << "', expected '" << expected << "'.\n";
        };
    expectTriangleFormat(0u, "0 tris");
    expectTriangleFormat(999u, "999 tris");
    expectTriangleFormat(1'000u, "1.0k tris");
    expectTriangleFormat(999'949u, "999.9k tris");
    expectTriangleFormat(999'950u, "1.0m tris");
    expectTriangleFormat(1'200'000u, "1.2m tris");
    expectTriangleFormat(999'949'999u, "999.9m tris");
    expectTriangleFormat(999'950'000u, "1.0b tris");
    expectTriangleFormat(
        999'950'000'000ull,
        "999.9b+ tris");

    const uint64_t maximumSubmittedTriangles =
        uvsr::CountSubmittedTriangleListPrimitives(
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max());
    passed &= maximumSubmittedTriangles ==
        uint64_t(
            std::numeric_limits<uint32_t>::max() / 3u) *
        uint64_t(std::numeric_limits<uint32_t>::max());
    passed &= uvsr::CountSubmittedTriangleListPrimitives(
        8u,
        3u) == 6u;

    if (!passed)
        return 1;
    std::cout << "UVSR renderer source contracts passed.\n";
    return 0;
}
