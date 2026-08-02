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
    const std::string viewer = ReadFile(root / "src/uvsr.cpp");
    const std::string cmaa = ReadFile(root / "src/cmaa2.cpp");
    const std::string deferredLighting = ReadFile(
        root / "src/pbr_deferred_lighting_cs.hlsl");
    const std::string deferredConstants = ReadFile(
        root / "src/pbr_deferred_lighting_cb.h");
    const std::string visibilityConstants = ReadFile(
        root / "src/screen_space_visibility_cb.h");
    const std::string lightingSources = ReadFile(
        root / "src/lighting_contribution_shared.h");
    const std::string imageBasedLighting = ReadFile(
        root / "src/image_based_lighting_environment.cpp");
    const std::string imageBasedLightingHeader = ReadFile(
        root / "src/image_based_lighting_environment.h");
    const std::string visibilityPass = ReadFile(
        root / "src/screen_space_visibility.cpp");
    const std::string visibilityPassHeader = ReadFile(
        root / "src/screen_space_visibility.h");
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
    const std::string donutLoadingOverride = ReadFile(
        root / "overrides/donut-loading.patch");
    const std::string donutLoadingAppOverride = ReadFile(
        root / "overrides/donut-loading-app.patch");
    bool passed = true;

    passed &= ExpectContains(
        viewer,
        "DWORD requested = NORMAL_PRIORITY_CLASS;",
        "normal interactive process priority");
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

    const std::string_view flashlightLightClass = ExtractSection(
        viewer,
        "class FlashlightSpotLight final : public SpotLight",
        "static bool g_RestartRequested");
    passed &= ExpectContains(
        flashlightLightClass,
        "SpotLight::FillLightConstants(lightConstants);",
        "first-party flashlight light-constant base fill");
    passed &= ExpectContains(
        flashlightLightClass,
        "EncodeFlashlightBeamShapeRadius(beamRoundness)",
        "flashlight beam-shape transport");
    passed &= ExpectContains(
        flashlightLightClass,
        "lightConstants.shadowChannel[1]",
        "flashlight right-axis X transport");
    passed &= ExpectContains(
        flashlightLightClass,
        "lightConstants.shadowChannel[2]",
        "flashlight right-axis Y transport");
    passed &= ExpectContains(
        flashlightLightClass,
        "lightConstants.shadowChannel[3]",
        "flashlight right-axis Z transport");
    passed &= ExpectAbsent(
        flashlightLightClass,
        "lightConstants.shadowChannel[0]",
        "existing shadow-channel ownership");

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
        passed &= ExpectAbsent(
            lightingSources,
            retiredEmissiveSourceSurface,
            "retired emissive GI source classification");
    }
    passed &= ExpectContains(
        deferredLighting,
        "finalLinearHdr = max(diffuse + specular + "
            "gbuffer.material.emissive, 0.0f);",
        "visible authored emissive retention");

    const std::string_view factoryExperimentTopology = ExtractSection(
        viewer,
        "static void ApplyFactoryExperimentShaderTopology(UIData& ui)",
        "static std::string FindVisibilityVerificationSettingsMismatch(");
    for (const std::string_view factorySetting : {
            std::string_view("ui.EnablePbr = true;"),
            std::string_view("ui.RenderMode = RendererMode::Deferred;"),
            std::string_view("ui.AntiAliasing = AntiAliasingSettings{};"),
            std::string_view(
                "ui.ScreenSpaceDirectionalShadows = "
                "ScreenSpaceDirectionalShadowSettings{};"),
            std::string_view(
                "ui.SparseVirtualShadowMaps = "
                "SparseVirtualShadowMapSettings{};"),
            std::string_view(
                "ui.ScreenSpaceVisibility = "
                "ScreenSpaceVisibilitySettings{};"),
            std::string_view(
                "ui.WhiteWorld = WhiteWorldMode::Off;") })
    {
        passed &= ExpectContains(
            factoryExperimentTopology,
            factorySetting,
            "factory experiment topology lock");
    }
    passed &= ExpectContains(
        viewer,
        "virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override\n"
        "    {\n"
        "        if (m_SceneGpuUploadPending)",
        "bounded scene upload gate");
    passed &= ExpectOrdered(
        ExtractSection(
            viewer,
            "virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override",
            "std::shared_ptr<ShaderFactory> GetShaderFactory()"),
        "RenderSceneGpuUploadFrame(framebuffer);",
        "ApplyFactoryExperimentShaderTopology(m_ui);",
        "factory experiment per-frame topology lock");
    passed &= ExpectContains(
        viewer,
        "This factory-settings experiment build supports only the ",
        "factory experiment command-line topology rejection");

    const std::string_view refresh = ExtractSection(
        viewer,
        "void RefreshAntiAliasingTargetPasses(bool sampleCountChanged)",
        "void BeginRenderPassPreparation(bool waitForIbl)");
    passed &= ExpectContains(
        refresh,
        "m_PbrDeferredLightingPass->ResetBindingCache();",
        "AA-only target refresh");
    passed &= ExpectContains(
        refresh,
        "m_ScreenSpaceVisibilityPass->ResetBindingCache();",
        "AA-only target refresh");
    passed &= ExpectContains(
        refresh,
        "m_ScreenSpaceVisibilityPass->ResetHistory();",
        "AA-only target refresh history invalidation");
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
        "void EnsureOptionalDirectionalVisibilityPasses()");
    passed &= ExpectContains(
        createPasses,
        "#if UVSR_DEFAULT_SETTINGS_EXPERIMENT_SHADERS",
        "factory experiment pass construction guard");
    passed &= ExpectContains(
        createPasses,
        "m_ScreenSpaceDirectionalShadowPass.reset();",
        "factory experiment screen-space shadow omission");
    passed &= ExpectContains(
        createPasses,
        "m_SparseVirtualShadowMapPass.reset();",
        "factory experiment SVSM omission");
    passed &= ExpectContains(
        createPasses,
        "m_DiagnosticCascadedShadowMapPass.reset();",
        "factory experiment diagnostic CSM omission");
    passed &= ExpectAbsent(
        createPasses,
        "std::make_unique<SparseVirtualShadowMapPass>",
        "disabled SVSM startup omission");
    passed &= ExpectContains(
        createPasses,
        "&m_PreparedVisibilityBlueNoise",
        "worker-prepared visibility sampling data");

    const std::string_view ensureOptionalPasses = ExtractSection(
        viewer,
        "void EnsureOptionalDirectionalVisibilityPasses()",
        "virtual void RenderSplashScreen(");
    passed &= ExpectContains(
        ensureOptionalPasses,
        "m_ui.ScreenSpaceDirectionalShadows.enabled",
        "lazy screen-space shadow construction gate");
    passed &= ExpectContains(
        ensureOptionalPasses,
        "m_ui.SparseVirtualShadowMaps.enabled ||\n"
            "            m_SvsmMotionBenchmarkAutostartPending ||\n"
            "            m_SvsmMotionBenchmarkActive",
        "lazy SVSM construction and benchmark gate");
    passed &= ExpectContains(
        ensureOptionalPasses,
        "m_ui.DiagnosticCascadedShadowMaps.enabled ||\n"
            "            m_DiagnosticCsmBenchmarkRequested ||\n"
            "            m_DiagnosticCsmRecordRequested",
        "lazy diagnostic CSM construction and benchmark gate");
    passed &= ExpectAbsent(
        refresh,
        "std::make_unique<PbrDeferredLightingPass>",
        "AA-only target refresh");

    const std::string_view flashlightShadowResources = ExtractSection(
        viewer,
        "void CreateFlashlightShadowResources()",
        "void RenderFlashlightShadow()");
    passed &= ExpectContains(
        flashlightShadowResources,
        "std::make_shared<PlanarShadowMap>(",
        "flashlight geometry shadow map");
    passed &= ExpectContains(
        flashlightShadowResources,
        "std::make_unique<DepthPass>(",
        "flashlight opaque and alpha-tested depth pass");
    const std::string_view flashlightPresentation = ExtractSection(
        viewer,
        "void ApplyFlashlightPresentation()",
        "void UpdateFlashlightAnimation(float elapsedSeconds)");
    passed &= ExpectContains(
        flashlightPresentation,
        "m_Flashlight->shadowMap = activeShadowMap;",
        "flashlight spill-to-shadow association");
    passed &= ExpectContains(
        flashlightPresentation,
        "m_FlashlightHotspot->shadowMap = activeShadowMap;",
        "flashlight hotspot-to-shadow association");
    passed &= ExpectContains(
        flashlightPresentation,
        "ResolveFlashlightLobeSettings(settings)",
        "flashlight lobe settings resolution");
    passed &= ExpectContains(
        flashlightPresentation,
        "m_FlashlightHotspot->range = settings.rangeMeters;",
        "flashlight shared lobe range");
    passed &= ExpectContains(
        flashlightPresentation,
        "m_Flashlight->beamRoundness =\n"
            "            settings.beamRoundness;",
        "flashlight spill beam footprint");
    passed &= ExpectContains(
        flashlightPresentation,
        "m_FlashlightHotspot->beamRoundness =\n"
            "                settings.beamRoundness;",
        "flashlight hotspot beam footprint");

    const std::string_view flashlightAnimation = ExtractSection(
        viewer,
        "void UpdateFlashlightAnimation(float elapsedSeconds)",
        "static float3 ClampFlashlightAimLag(");
    passed &= ExpectContains(
        flashlightAnimation,
        "!m_ui.EnablePbr",
        "legacy-lighting flashlight animation exclusion");
    passed &= ExpectContains(
        flashlightAnimation,
        "m_FlashlightTransition = 0.f;",
        "legacy-lighting flashlight exact-zero endpoint");

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
            "                settings.cameraLateralOffsetMeters);",
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
        "mount.directionForward +",
        "flashlight converged forward aim");
    passed &= ExpectContains(
        flashlightMotion,
        "mount.directionRight +",
        "flashlight converged horizontal aim");
    passed &= ExpectContains(
        flashlightMotion,
        "mount.directionUp);",
        "flashlight converged vertical aim");
    passed &= ExpectContains(
        flashlightMotion,
        "m_FlashlightResolvedPosition = flashlightPosition;",
        "flashlight shared off-axis position");
    passed &= ExpectContains(
        flashlightMotion,
        "m_FlashlightResolvedDirection = mountedDirection;",
        "simple flashlight converged camera aim");
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
        "m_FlashlightHotspot->SetPosition(",
        "flashlight hotspot position synchronization");
    passed &= ExpectContains(
        flashlightTransform,
        "SetFlashlightDirectionAndRoll(\n"
            "                m_FlashlightHotspot,",
        "flashlight hotspot direction-and-roll synchronization");
    passed &= ExpectContains(
        flashlightTransform,
        "m_Flashlight->beamRight =\n"
            "            m_FlashlightResolvedRight;",
        "flashlight spill shape-basis publication");
    passed &= ExpectContains(
        flashlightTransform,
        "m_FlashlightHotspot->beamRight =\n"
            "                m_FlashlightResolvedRight;",
        "flashlight hotspot shape-basis publication");

    const std::string_view flashlightAttachment = ExtractSection(
        viewer,
        "void AttachFlashlightToScene()",
        "void CreateFlashlightShadowResources()");
    passed &= ExpectContains(
        flashlightAttachment,
        "std::make_shared<FlashlightSpotLight>()",
        "first-party flashlight light construction");
    passed &= ExpectContains(
        flashlightAttachment,
        "m_Flashlight->SetName(FlashlightPublicName);",
        "public flashlight light identifier");
    passed &= ExpectContains(
        flashlightAttachment,
        "m_FlashlightNode->SetName(FlashlightPublicName);",
        "public flashlight node identifier");
    passed &= ExpectContains(
        flashlightAttachment,
        "\"flashlight_lens_hotspot\"",
        "hidden internal lens-lobe identifier");

    const std::string_view flashlightShadowRender = ExtractSection(
        viewer,
        "void RenderFlashlightShadow()",
        "virtual void Animate(float fElapsedTimeSeconds) override");
    passed &= ExpectContains(
        flashlightShadowRender,
        "if (!m_ui.EnablePbr ||",
        "legacy-lighting flashlight shadow exclusion");
    passed &= ExpectContains(
        flashlightShadowRender,
        "!ShouldRenderFlashlightShadow(",
        "settled-off flashlight shadow work gate");
    passed &= ExpectContains(
        flashlightShadowRender,
        "m_ui.Flashlight.castShadows",
        "flashlight shadow setting gate");
    passed &= ExpectContains(
        flashlightShadowRender,
        "FlashlightShadowCollisionNearScale",
        "flashlight camera-collision near-plane margin");
    passed &= ExpectContains(
        flashlightShadowRender,
        "m_CommandList->clearDepthStencilTexture(",
        "flashlight depth-stencil clear");
    passed &= ExpectAbsent(
        flashlightShadowRender,
        "m_FlashlightShadowMap->Clear(",
        "unsafe generic flashlight clear");
    passed &= ExpectContains(
        flashlightShadowRender,
        "\"FlashlightShadow\"",
        "flashlight geometry shadow submission");

    const std::string_view renderScene = ExtractSection(
        viewer,
        "virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override",
        "std::shared_ptr<ShaderFactory> GetShaderFactory()");
    passed &= ExpectOrdered(
        renderScene,
        "UpdateFlashlightTransform();",
        "m_Scene->RefreshSceneGraph(GetFrameIndex());",
        "flashlight transform publication");
    passed &= ExpectOrdered(
        renderScene,
        "lightingLights.push_back(m_Flashlight);",
        "lightingLights.push_back(m_FlashlightHotspot);",
        "realistic flashlight lobe priority");
    passed &= ExpectOrdered(
        renderScene,
        "lightingLights.push_back(m_FlashlightHotspot);",
        "for (const auto& light : sceneLights)",
        "flashlight light-limit priority");
    passed &= ExpectOrdered(
        renderScene,
        "RenderFlashlightShadow();",
        "m_ForwardPass->PrepareLights(",
        "forward flashlight shadow-before-lighting order");
    passed &= ExpectOrdered(
        renderScene,
        "RenderFlashlightShadow();",
        "deferredInputs.lights = submittedLights;",
        "deferred flashlight shadow-before-lighting order");
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
        "if (m_ui.EnablePbr &&\n"
            "            ShouldSubmitFlashlight(",
        "legacy-lighting flashlight submission exclusion");
    passed &= ExpectContains(
        renderScene,
        "m_ui.Flashlight.realisticLens",
        "realistic flashlight hotspot submission gate");

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
        "GenerateVisibilityBlueNoise();",
        "worker visibility-rank preparation");
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
        "m_Scene->BeginLoadingBuffers();",
        "prepared collision installation before bounded upload");
    passed &= ExpectContains(
        sceneLoadedHandoff,
        "StagePreparedRadiance(",
        "prepared HDR activation handoff");

    const std::string_view boundedSceneUpload = ExtractSection(
        viewer,
        "void RenderSceneGpuUploadFrame(",
        "static bool IsSameDiagnosticCsmBenchmarkWork(");
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
        "light != m_Flashlight &&\n"
            "                light != m_FlashlightHotspot",
        "scene-light list flashlight-lobe exclusion");
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
    passed &= ExpectContains(
        msaaSampleCountResolution,
        "bool enablePbr",
        "MSAA allocation-aware PBR format selection");
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
        "        cache.requestedSampleCount == requestedSampleCount &&\n"
        "        cache.enablePbr == enablePbr",
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
        ".rasterSampleCount,\n"
        "                        m_ui.EnablePbr);",
        "MSAA PBR topology routing");
    passed &= ExpectContains(
        viewer,
        "RefreshAntiAliasingTargetPasses(\n"
        "                    antiAliasingSampleCountChanged);",
        "AA-only refresh dispatch");
    passed &= ExpectContains(
        cmaa,
        "void Cmaa2Pass::UpdateSourceColor(",
        "CMAA2 source rebinding");
    passed &= ExpectContains(
        cmaa,
        "RebuildBindingSet(sourceColor);",
        "CMAA2 source rebinding");

    const std::string_view commandLine = ExtractSection(
        viewer,
        "bool ProcessCommandLine(",
        "std::string FormatExperimentLaunchTime(");
    passed &= ExpectContains(
        commandLine,
        "else if (!strcmp(argv[i], \"--aa-rectification\"))",
        "AA rectification benchmark option");
    for (const std::string_view mode : {
            std::string_view("\"pair-rgb\""),
            std::string_view("\"variance-ycocg\"") })
    {
        passed &= ExpectContains(
            commandLine,
            mode,
            "AA rectification benchmark mode");
    }
    passed &= ExpectAbsent(
        commandLine,
        "per-pixel",
        "retired AA per-pixel rectification benchmark modes");
    passed &= ExpectContains(
        commandLine,
        "aaBenchmark.settings.algorithmOverrides.rectification",
        "AA rectification benchmark override routing");
    passed &= ExpectContains(
        commandLine,
        "bool aaQualityExplicit = false;",
        "AA command-line quality-order tracking");
    passed &= ExpectContains(
        commandLine,
        "if (aaQualityExplicit)",
        "AA explicit quality preservation across method options");
    passed &= ExpectContains(
        commandLine,
        "aaQualityExplicit = true;",
        "AA command-line explicit quality recording");

    const std::string_view aaMotionBenchmark = ExtractSection(
        viewer,
        "void UpdateAntiAliasingBenchmark()",
        "virtual void Animate(float fElapsedTimeSeconds) override");
    passed &= ExpectContains(
        aaMotionBenchmark,
        "float(turnStep) /",
        "AA motion benchmark fixed per-frame turn");
    passed &= ExpectContains(
        aaMotionBenchmark,
        "float(AaBenchmarkTurnFrames);",
        "AA motion benchmark fixed per-frame turn");
    passed &= ExpectContains(
        viewer,
        "\"wall_clock_pacing_enabled\\\": false",
        "AA motion benchmark uncapped report");
    passed &= ExpectAbsent(
        viewer,
        "sleep_until",
        "AA motion benchmark wall-clock pacing");
    passed &= ExpectAbsent(
        viewer,
        "AaBenchmarkTargetFramesPerSecond",
        "AA motion benchmark frame-rate target");
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
        "SubmittedTriangleCountingPass geometryPass(\n"
        "                *m_ForwardPass);",
        "forward submitted-triangle accounting");
    passed &= ExpectContains(
        viewer,
        "m_SubmittedMainViewTriangles =\n"
        "                geometryPass.GetSubmittedTriangles();",
        "submitted-triangle publication");

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
