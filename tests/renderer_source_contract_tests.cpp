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
    const std::string buildSystem = ReadFile(root / "CMakeLists.txt");
    const std::string attributes = ReadFile(root / ".gitattributes");
    const std::string viewer = ReadFile(root / "src/uvsr.cpp");
    const std::string cmaa = ReadFile(root / "src/cmaa2.cpp");
    const std::string cmaaHeader = ReadFile(root / "src/cmaa2.h");
    const std::string cmaaShader = ReadFile(root / "src/cmaa2.hlsl");
    const std::string cmaaVendoredShader = ReadFile(
        root / "src/third_party/intel_cmaa2/CMAA2.hlsl");
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
    const std::string gpuPerformanceMonitor = ReadFile(
        root / "src/gpu_performance_monitor.cpp");
    const std::string gpuPerformanceMonitorHeader = ReadFile(
        root / "src/gpu_performance_monitor.h");
    const std::string worldRepresentation = ReadFile(
        root / "src/world_space_representation.cpp");
    const std::string worldRepresentationHeader = ReadFile(
        root / "src/world_space_representation.h");
    const std::string heitzShadows = ReadFile(
        root / "src/heitz_ratio_estimator_shadows.cpp");
    const std::string donutLoadingOverride = ReadFile(
        root / "overrides/donut-loading.patch");
    const std::string donutLoadingAppOverride = ReadFile(
        root / "overrides/donut-loading-app.patch");
    bool passed = true;

    passed &= ExpectContains(
        gpuPerformanceMonitorHeader,
        "bool utilizationValid = false;",
        "graphics utilization validity transport");
    passed &= ExpectContains(
        gpuPerformanceMonitor,
        "m_Metrics.utilizationValid = utilizationValid;",
        "NVIDIA utilization validity publication");
    passed &= ExpectContains(
        gpuPerformanceMonitor,
        "m_Metrics.utilizationValid = false;",
        "Intel unavailable utilization publication");
    passed &= ExpectContains(
        viewer,
        "if (snapshot.gpuMetrics.utilizationValid)",
        "utilization-scaled throughput validity gate");

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
            "                        uint32_t(m_HeitzRatioEstimatorPhase),\n"
            "                        m_SceneDiagonal);",
        "TAA-independent Heitz sampling phase input");
    passed &= ExpectContains(
        viewer,
        "if (heitzShadowResult.dispatched &&\n"
            "            heitzShadowResult.stochastic)",
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
            "                (visibilityResourcesRequired && sampleCount > 1u);",
        "runtime motion topology without Heitz history");
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
        "if (outputSizeMatches)",
        "frame-local Heitz output resource fast path");
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
        "if (m_BindingSet &&",
        "complete frame-local Heitz binding fast path");
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
    passed &= ExpectContains(
        createPasses,
        "&m_PreparedVisibilityBlueNoise",
        "worker-prepared visibility sampling data");

    const std::string_view ensureScreenSpaceShadows = ExtractSection(
        viewer,
        "void EnsureScreenSpaceDirectionalShadowPass()",
        "void UpdateImageBasedLighting(");
    passed &= ExpectContains(
        ensureScreenSpaceShadows,
        "m_ui.ScreenSpaceDirectionalShadows.enabled",
        "lazy screen-space shadow construction gate");
    passed &= ExpectContains(
        ensureScreenSpaceShadows,
        "std::make_unique<ScreenSpaceDirectionalShadowPass>",
        "retained screen-space shadow construction");
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
        "m_FlashlightTransition = AdvanceFlashlightTransition(",
        "bounded flashlight transition helper");
    passed &= ExpectOrdered(
        flashlightAnimation,
        "AdvanceFlashlightTransition(",
        "ApplyFlashlightPresentation();",
        "flashlight transition before presentation");

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
        "third_party/licenses/BSD-2-Clause.txt -text -whitespace",
        "stable Fast Approximate BSD license bytes");
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
    passed &= ExpectOrdered(
        viewer,
        "BeginRendererStage(\n"
            "                        RendererTimingStage::ScreenSpaceVisibility);\n"
            "                    m_PbrDeferredLightingPass->Render(",
        "m_ScreenSpaceVisibilityPass->Render(",
        "multisample Visibility preparation timing attribution");

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
