#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene.h"
#include "uvsr_renderer_lighting.h"
#include "uvsr_renderer_frame.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include "renderer_producer_contract.h"
#include "renderer_statistics.h"
#include "renderer_texture_bmp.h"
#include "windows_executable_path.h"
#include <Windows.h>
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;

namespace
{
constexpr float DefaultFlashlightRayBiasMeters = 0.002f;
bool CopyBmpToClipboard(const std::filesystem::path& fileName)
{
    HANDLE file = CreateFileW(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    const DWORD fileSize = GetFileSize(file, nullptr);
    if (fileSize <= sizeof(BITMAPFILEHEADER))
    {
        CloseHandle(file);
        return false;
    }

    BITMAPFILEHEADER header{};
    DWORD bytesRead = 0;
    const bool validHeader = ReadFile(file, &header, sizeof(header), &bytesRead, nullptr)
        && bytesRead == sizeof(header) && header.bfType == 0x4D42;
    if (!validHeader)
    {
        CloseHandle(file);
        return false;
    }

    const SIZE_T dibSize = fileSize - sizeof(BITMAPFILEHEADER);
    HGLOBAL dibMemory = GlobalAlloc(GMEM_MOVEABLE, dibSize);
    void* dibData = dibMemory ? GlobalLock(dibMemory) : nullptr;
    const bool readSucceeded = dibData
        && ReadFile(file, dibData, DWORD(dibSize), &bytesRead, nullptr)
        && bytesRead == dibSize;
    if (dibData)
        GlobalUnlock(dibMemory);
    CloseHandle(file);

    if (!readSucceeded)
    {
        if (dibMemory)
            GlobalFree(dibMemory);
        return false;
    }

    bool clipboardOpened = false;
    for (int attempt = 0; attempt < 5 && !clipboardOpened; ++attempt)
    {
        clipboardOpened = OpenClipboard(nullptr) != FALSE;
        if (!clipboardOpened)
            Sleep(10);
    }

    if (!clipboardOpened)
    {
        GlobalFree(dibMemory);
        return false;
    }

    EmptyClipboard();
    const bool copied = SetClipboardData(CF_DIB, dibMemory) != nullptr;
    CloseClipboard();
    if (!copied)
        GlobalFree(dibMemory);
    return copied;
}

}

// Transient borrowed inputs and results shared by this frame's fixed stages.
struct UvsrSceneViewer::FrameExecution
{
    UvsrSceneViewer& renderer;
    nvrhi::IFramebuffer* framebuffer;
    bool commandOpen = false;
    bool pathTracingSelected{};
    bool pathTracingSceneDomainSupported{};
    DirectX::XMUINT2 presentationSize{};
    DirectX::XMUINT2 renderSize{};
    bool lightingSceneContentChanged{};
    const std::vector<std::shared_ptr<Light>>* submittedLights{};
    bool directionalRayVisibilitySelected{};
    bool rayTracedFlashlightShadowSelected{};
    bool rayTracedSkyVisibilitySelected{};
    RaySceneView rayScene{};
    bool worldRepresentationReady{};
    ResolvedAntiAliasingSettings antiAliasing{};
    bool directionalRayVisibilityExpectedToContribute{};
    nvrhi::ITexture* framebufferTexture{};
    bool environmentBackgroundSelected{};
    const ImageBasedLightingProbe* globalEnvironment{};
    bool rayTracedSkyVisibilityExpectedToContribute{};
    NoiseSettings skyNoiseSettings{};
    NoiseSettings directShadowNoiseSettings{};
    bool rayTracedFlashlightShadowExpectedToContribute{};
    NoiseTextureBinding skyNoise{};
    NoiseTextureBinding directShadowNoise{};
    const SpotLight* submittedFlashlight{};
    FlashlightBeamProfile flashlightBeamProfile{};
    bool rayMarchingProducerTopologyReady{};
    bool rayMarchingDiagnostic{};
    bool rayMarchingAccumulationSelected{};
    LightingSampleSchedule lightingSampleSchedule{};
    bool lightingSampleSchedulePrepared{};
    DirectionalRayVisibilityResult directionalVisibilityResult{};
    RayTracedFlashlightShadowResult flashlightShadowResult{};
    RayTracedSkyVisibilityResult skyVisibilityResult{};
    LightingSurfaceView rasterSurface{};
    nvrhi::ITexture* sceneColor{};

    void Open() { renderer.m_frame->commandList->open(); commandOpen = true; }
    bool Fail(const char* pass)
    {
        uvsr::log::error("Required renderer pass failed: %s", pass);
        renderer.GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
        return false;
    }
    bool Submit(bool pending = false)
    {
        renderer.EndRendererStage(RendererTimingStage::SceneSetup);
        renderer.EndRendererStage(RendererTimingStage::CompleteFrame);
        renderer.CompleteRendererTimerFrame();
        renderer.m_frame->commandList->close();
        renderer.GetDevice()->executeCommandList(renderer.m_frame->commandList);
        commandOpen = false;
        if (pending)
            renderer.GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Pending);
        return !pending;
    }
    bool SuspendPathTracing()
    {
        if (renderer.m_lighting->selectedLightingTransportState !=
            SelectedLightingTransportState::PathTracingUnavailable)
            renderer.ResetImageBasedLightingHistory();
        renderer.m_lighting->selectedLightingTransportState =
            SelectedLightingTransportState::PathTracingUnavailable;
        // Present the UI so a missing prerequisite can be restored. Pending
        // skips the UI and presentation, and Failed terminates the shell.
        renderer.m_frame->commandList->clearTextureFloat(
            framebuffer->getDesc().colorAttachments[0].texture,
            nvrhi::AllSubresources, nvrhi::Color(0.f));
        Submit();
        return false;
    }
    ~FrameExecution()
    {
        if (lightingSampleSchedulePrepared)
            renderer.m_lighting->lightingAccumulationPass->CancelPreparedSchedule(lightingSampleSchedule);
        if (!commandOpen)
            return;
        auto& frame = *renderer.m_frame;
        const uint32_t slot = frame.rendererTimerFrame % RendererFrameState::TimerLatency;
        for (size_t stage = 0; stage < frame.rendererTimerActive.size(); ++stage)
        {
            renderer.EndRendererStage(static_cast<RendererTimingStage>(stage));
            // Only this frame's free slot can contain unsubmitted queries.
            if (frame.rendererTimerFrameWritable && frame.rendererTimerPending[stage][slot])
            {
                renderer.GetDevice()->resetTimerQuery(frame.rendererTimerQueries[stage][slot]);
                frame.rendererTimerPending[stage][slot] = false;
                frame.rendererTimings.available[stage] = false;
            }
        }
        frame.commandList->close();
        renderer.ResetImageBasedLightingHistory();
    }
};

void UvsrSceneViewer::PrepareFrameTargets(FrameExecution& execution)
{
    execution.pathTracingSelected =
        m_ui.Lighting == LightingSolution::PathTracing;
    if (execution.pathTracingSelected)
    {
        EnsurePathTracingPass();
        if (m_lighting->pathTracingPass)
            m_lighting->pathTracingPass->PollAcceptedSampleReadback();
    }
    const PathTracingSceneDomainStatus pathTracingSceneDomainStatus =
        execution.pathTracingSelected
            ? GetPathTracingSceneDomainStatus()
            : PathTracingSceneDomainStatus::Supported;
    execution.pathTracingSceneDomainSupported =
        pathTracingSceneDomainStatus !=
            PathTracingSceneDomainStatus::Unsupported;
    int windowWidth, windowHeight;
    GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
    execution.presentationSize = DirectX::XMUINT2{
        uint32_t(windowWidth),
        uint32_t(windowHeight)
    };

    execution.renderSize = execution.presentationSize;

    UpdateFlashlightTransform();
    execution.lightingSceneContentChanged = false;
    if (const std::shared_ptr<SceneGraph> sceneGraph =
            m_scene->world->GetSceneGraph())
    {
        const std::shared_ptr<SceneGraphNode>& root =
            sceneGraph->GetRootNode();
        const bool pendingContentChanges = root &&
            (root->GetDirtyFlags() &
                SceneGraphNode::DirtyFlags::SubgraphContentUpdate) != 0u;
        execution.lightingSceneContentChanged =
            sceneGraph->HasPendingStructureChanges() ||
            sceneGraph->HasPendingTransformChanges() ||
            pendingContentChanges;
        for (const std::shared_ptr<Material>& material :
            sceneGraph->GetMaterials())
        {
            execution.lightingSceneContentChanged =
                execution.lightingSceneContentChanged ||
                (material && material->dirty);
        }
    }
    m_scene->world->RefreshSceneGraph(GetFrameIndex());
    // Scene activation keeps both borrowed lists in deterministic light order.
    execution.submittedLights =
        ShouldSubmitFlashlight(m_lighting->flashlightTransition) && m_lighting->flashlight
            ? &m_lighting->editableLights
            : &m_lighting->sceneLightsWithoutFlashlight;

    {
        const bool fastApproximateAARequired = m_ui.UsesFastApproximateAA();
        bool needNewPasses = false;

        if (!m_frame->renderTargets || m_frame->renderTargets->IsUpdateRequired(
            execution.renderSize,
            execution.presentationSize,
            !execution.pathTracingSelected))
        {
            if (m_lighting->directionalRayVisibilityPass)
                m_lighting->directionalRayVisibilityPass->ResetBindingCache();
            if (m_lighting->rayTracedFlashlightShadowPass)
                m_lighting->rayTracedFlashlightShadowPass->ResetBindingCache();
            if (m_lighting->rayTracedSkyVisibilityPass)
                m_lighting->rayTracedSkyVisibilityPass->ResetBindingCache();
            m_frame->renderTargets = nullptr;
            m_frame->bindingCache.Clear();
            m_frame->renderTargets = std::make_unique<RenderTargets>();
            if (!m_frame->renderTargets->Init(
                    GetDevice(), execution.renderSize,
                    execution.presentationSize,
                    true,
                    !execution.pathTracingSelected))
            {
                throw std::runtime_error(
                    "UVSR render targets failed to initialize");
            }

            needNewPasses = true;
        }

        if (SetupView())
        {
            needNewPasses = true;
        }

        if (m_ui.ShaderReloadRequested)
        {
            const bool recreatePathTracingPass =
                execution.pathTracingSelected && bool(m_lighting->pathTracingPass);
            m_lighting->directionalRayVisibilityPass.reset();
            m_lighting->rayTracedFlashlightShadowPass.reset();
            m_lighting->rayTracedSkyVisibilityPass.reset();
            m_lighting->pathTracingPass.reset();
            m_lighting->lightingAccumulationPass.reset();
            m_frame->shaderFactory->ClearCache();
            m_frame->rendererShaderFactory->ClearCache();
            if (recreatePathTracingPass)
                EnsurePathTracingPass();
            InvalidateLightingAccumulationHistory();
            // Light-probe preprocessing owns shader handles too. Recreate
            // it only for an explicit shader reload; static IBL otherwise needs no work.
            m_lighting->imageBasedLightingEnvironment =
                std::make_unique<ImageBasedLightingEnvironment>(
                    GetDevice(),
                    m_frame->rendererShaderFactory,
                    m_frame->rendererCommonPasses,
                    GetExecutableDirectoryWide().parent_path() /
                        "media/environments");
            needNewPasses = true;
        }

        if (needNewPasses)
        {
            BeginRenderPassPreparation(false);
            while (!ProcessRenderPassPreparationStep())
            {
            }
        }
        // Fast Approximate is a presentation-only spatial filter. Its
        // resources follow the tone-mapped presentation target.
        if (fastApproximateAARequired && !m_frame->fastApproximateAAPass)
            CreateFastApproximateAAPass();
        else if (!fastApproximateAARequired && m_frame->fastApproximateAAPass)
            m_frame->fastApproximateAAPass.reset();

        m_ui.ShaderReloadRequested = false;
    }

    if (!execution.pathTracingSelected)
    {
        EnsureDirectionalRayVisibilityPass();
        EnsureRayTracedFlashlightShadowPass();
        EnsureRayTracedSkyVisibilityPass();
    }

}

bool UvsrSceneViewer::PrepareWorldRepresentation(FrameExecution& execution)
{
    execution.Open();
    AdvanceRendererTimers();
    m_lighting->pathTransportDispatchedThisFrame = false;
#if defined(UVSR_BUILD_TESTING)
    m_frame->runtimeLinearReadbackQueued = false;
#endif
    m_lighting->directionalRayVisibilityDispatchedThisFrame = false;
    m_lighting->rayTracedFlashlightShadowDispatchedThisFrame = false;
#if defined(UVSR_BUILD_TESTING)
    m_lighting->flashlightLightingSubmittedThisFrame = false;
#endif
    m_lighting->rayTracedSkyVisibilityDispatchedThisFrame = false;
    m_frame->autoExposureDispatchedThisFrame = false;
#if defined(UVSR_BUILD_TESTING)
    m_lighting->lightingAccumulationCommittedThisFrame = false;
#endif
    BeginRendererStage(RendererTimingStage::CompleteFrame);
    BeginRendererStage(RendererTimingStage::SceneSetup);
    m_scene->world->RefreshBuffers(m_frame->commandList, GetFrameIndex());
    execution.directionalRayVisibilitySelected =
        !execution.pathTracingSelected &&
        m_ui.Representation.allowRayTraversal &&
        m_ui.DirectionalShadows.enabled &&
        SupportsDirectionalRayVisibility();
    execution.rayTracedFlashlightShadowSelected =
        !execution.pathTracingSelected &&
        m_ui.Representation.allowRayTraversal &&
        m_ui.Flashlight.castShadows &&
        ShouldSubmitFlashlight(m_lighting->flashlightTransition) &&
        HasRayTracedFlashlightShadowHardwareSupport();
    execution.rayTracedSkyVisibilitySelected =
        !execution.pathTracingSelected &&
        m_ui.Representation.allowRayTraversal &&
        m_ui.RayTracedSkyVisibility.enabled &&
        (HasRayTracedSkyVisibilityConsumer(
                m_ui.RayTracedSkyVisibility) ||
            m_ui.LightingDebugView ==
                PbrLightingDebugView::SkyVisibility) &&
        SupportsRayTracedSkyVisibility();
    const bool worldRepresentationSelected =
        execution.directionalRayVisibilitySelected ||
        execution.rayTracedFlashlightShadowSelected ||
        execution.rayTracedSkyVisibilitySelected ||
        (execution.pathTracingSelected &&
            execution.pathTracingSceneDomainSupported &&
            m_ui.Representation.allowRayTraversal &&
            m_scene->bindlessLayout &&
            PathTracingPass::QueryCapabilities(GetDevice())
                .rayQuerySupported);
    if (execution.pathTracingSelected &&
        (!execution.pathTracingSceneDomainSupported ||
            !m_ui.Representation.allowRayTraversal ||
            !m_scene->bindlessLayout ||
            !PathTracingPass::QueryCapabilities(GetDevice()).rayQuerySupported))
        return execution.SuspendPathTracing();
    if (execution.pathTracingSelected &&
        (!m_lighting->pathTracingPass || !m_lighting->pathTracingPass->IsSupported()))
    {
        return execution.Fail("path tracing transport");
    }
    if (execution.directionalRayVisibilitySelected &&
        (!m_lighting->directionalRayVisibilityPass ||
            !m_lighting->directionalRayVisibilityPass->IsSupported()))
    {
        return execution.Fail("directional ray visibility");
    }
    if (execution.rayTracedFlashlightShadowSelected &&
        (!m_lighting->rayTracedFlashlightShadowPass ||
            !m_lighting->rayTracedFlashlightShadowPass->IsSupported()))
    {
        return execution.Fail("ray-traced flashlight visibility");
    }
    if (execution.rayTracedSkyVisibilitySelected &&
        (!m_lighting->rayTracedSkyVisibilityPass ||
            !m_lighting->rayTracedSkyVisibilityPass->IsSupported()))
    {
        return execution.Fail("ray-traced sky visibility");
    }
    const uint64_t worldRepresentationGenerationBefore =
        m_scene->worldSpaceRepresentation
        ? m_scene->worldSpaceRepresentation->GetStatus().generation
        : 0u;
    const bool worldRepresentationUpdated =
        m_scene->worldSpaceRepresentation &&
        m_scene->worldSpaceRepresentation->Update(
            m_frame->commandList,
            m_scene->world.get(),
            m_ui.Representation,
            uint32_t(GetFrameIndex()),
            worldRepresentationSelected);
    if (worldRepresentationSelected)
    {
        if (!m_scene->worldSpaceRepresentation)
        {
            return execution.Fail("world-space representation");
        }
        const WorldSpaceRepresentationState state =
            m_scene->worldSpaceRepresentation->GetStatus().state;
        if (state == WorldSpaceRepresentationState::Failed ||
            state == WorldSpaceRepresentationState::Unsupported)
        {
            return execution.Fail("world-space representation");
        }
        if (!worldRepresentationUpdated)
        {
            if (state == WorldSpaceRepresentationState::Ready)
            {
                return execution.Fail(
                    "world-space representation dispatch");
            }
            return execution.Submit(true);
        }
    }
    execution.rayScene = worldRepresentationUpdated
        ? m_scene->worldSpaceRepresentation->GetRaySceneView(m_scene->world.get())
        : RaySceneView{};
    execution.worldRepresentationReady = bool(execution.rayScene);
    if (worldRepresentationSelected && !execution.worldRepresentationReady)
    {
        return execution.Fail("world-space representation view");
    }
    if (m_scene->worldSpaceRepresentation &&
        (m_scene->worldSpaceRepresentation->GetStatus().generation !=
                worldRepresentationGenerationBefore ||
            (worldRepresentationSelected &&
                !execution.worldRepresentationReady)))
    {
        ResetImageBasedLightingHistory();
    }

    return true;
}

bool UvsrSceneViewer::PrepareLightingInputs(FrameExecution& execution)
{
    execution.antiAliasing =
        m_ui.GetResolvedAntiAliasingSettings();
    execution.directionalRayVisibilityExpectedToContribute =
        execution.directionalRayVisibilitySelected &&
        execution.worldRepresentationReady &&
        m_lighting->sunLight;

    execution.framebufferTexture = execution.framebuffer->getDesc().colorAttachments[0].texture;
    m_frame->commandList->clearTextureFloat(execution.framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));

    const bool diffuseIblSelected =
        !execution.pathTracingSelected &&
        IsAmbientFillLobeActive(
            m_ui.EnableAmbientFill,
            m_ui.EnableDiffuseIbl,
            m_ui.DiffuseIblStrength);
    const bool specularIblSelected =
        !execution.pathTracingSelected &&
        IsAmbientFillLobeActive(
            m_ui.EnableAmbientFill,
            m_ui.EnableSpecularIbl,
            m_ui.SpecularIblStrength);
    execution.environmentBackgroundSelected =
        !execution.pathTracingSelected &&
        m_ui.LightingDebugView == PbrLightingDebugView::None &&
        m_ui.ShowEnvironmentBackground;
    const bool imageBasedLightingSelected =
        diffuseIblSelected ||
        specularIblSelected ||
        execution.environmentBackgroundSelected ||
        execution.pathTracingSelected;
    if (imageBasedLightingSelected &&
        !m_lighting->imageBasedLightingEnvironment)
    {
        return execution.Fail("image-based lighting ownership");
    }
    UpdateImageBasedLighting(m_frame->commandList);
    if (m_lighting->imageBasedLightingEnvironment)
    {
        const ImageBasedLightingPreparationStatus environmentStatus =
            m_lighting->imageBasedLightingEnvironment->GetPreparedRadianceStatus();
        if (environmentStatus ==
            ImageBasedLightingPreparationStatus::Failed)
        {
            return execution.Fail("image-based lighting preparation");
        }
        if (environmentStatus ==
                ImageBasedLightingPreparationStatus::Preparing ||
            environmentStatus == ImageBasedLightingPreparationStatus::Idle)
        {
            return execution.Submit(true);
        }
    }
    execution.globalEnvironment =
        m_lighting->imageBasedLightingEnvironment
            ? m_lighting->imageBasedLightingEnvironment->GetLightProbe()
            : nullptr;
    if ((diffuseIblSelected &&
            (!execution.globalEnvironment ||
                !execution.globalEnvironment->diffuseMap ||
                !(execution.globalEnvironment->diffuseScale > 0.f))) ||
        (specularIblSelected &&
            (!execution.globalEnvironment ||
                !execution.globalEnvironment->specularMap ||
                !execution.globalEnvironment->environmentBrdf ||
                !(execution.globalEnvironment->specularScale > 0.f))) ||
        (execution.environmentBackgroundSelected &&
            (!m_lighting->imageBasedLightingEnvironment ||
                !m_lighting->imageBasedLightingEnvironment
                    ->GetRadianceTexture())) ||
        (execution.pathTracingSelected &&
            (!m_lighting->imageBasedLightingEnvironment ||
                !m_lighting->imageBasedLightingEnvironment
                    ->GetRadianceTexture())))
    {
        return execution.Fail("image-based lighting resources");
    }
    const bool skyVisibilityDiffuseIblAvailable =
        m_ui.RayTracedSkyVisibility.applyToDiffuseIbl &&
        execution.globalEnvironment &&
        execution.globalEnvironment->diffuseMap &&
        execution.globalEnvironment->diffuseScale > 0.f;
    const bool skyVisibilitySpecularIblAvailable =
        m_ui.RayTracedSkyVisibility.applyToSpecularIbl &&
        execution.globalEnvironment &&
        execution.globalEnvironment->specularMap &&
        execution.globalEnvironment->environmentBrdf &&
        execution.globalEnvironment->specularScale > 0.f;
    execution.rayTracedSkyVisibilityExpectedToContribute =
        execution.rayTracedSkyVisibilitySelected &&
        execution.worldRepresentationReady &&
        (m_ui.LightingDebugView ==
                PbrLightingDebugView::SkyVisibility ||
            skyVisibilityDiffuseIblAvailable ||
            skyVisibilitySpecularIblAvailable);
    execution.skyNoiseSettings = ResolveNoiseSettings(
        m_ui.Noise,
        m_ui.RayTracedSkyVisibility.noise);
    execution.directShadowNoiseSettings = m_ui.Noise;
    execution.rayTracedFlashlightShadowExpectedToContribute =
        execution.rayTracedFlashlightShadowSelected &&
        execution.worldRepresentationReady;

    if (m_lighting->noiseTextureLibrary)
    {
        if (execution.rayTracedSkyVisibilityExpectedToContribute)
        {
            execution.skyNoise = m_lighting->noiseTextureLibrary->Resolve(
                m_frame->commandList,
                execution.skyNoiseSettings);
        }
        if (execution.rayTracedFlashlightShadowExpectedToContribute ||
            execution.directionalRayVisibilityExpectedToContribute)
        {
            execution.directShadowNoise = m_lighting->noiseTextureLibrary->Resolve(
                m_frame->commandList,
                execution.directShadowNoiseSettings);
        }
    }
    execution.submittedFlashlight =
        ShouldSubmitFlashlight(m_lighting->flashlightTransition)
            ? m_lighting->flashlight.get()
            : nullptr;
#if defined(UVSR_BUILD_TESTING)
    m_lighting->flashlightLightingSubmittedThisFrame =
        !execution.pathTracingSelected && execution.submittedFlashlight &&
        m_lighting->pbrDeferredLightingPass &&
        m_lighting->pbrDeferredLightingPass->ArePipelinesReady();
#endif
    execution.flashlightBeamProfile =
        execution.submittedFlashlight
            ? ResolveFlashlightBeamProfile(
                m_ui.Flashlight,
                m_lighting->flashlightResolvedRight.x,
                m_lighting->flashlightResolvedRight.y,
                m_lighting->flashlightResolvedRight.z)
            : FlashlightBeamProfile{};
    execution.flashlightBeamProfile.emitterRadiusMeters = ResolveShadowEmitterSize(
        execution.flashlightBeamProfile.emitterRadiusMeters, m_ui.DirectionalShadows.hardShadows);
    const bool flashlightStochasticRequested =
        execution.rayTracedFlashlightShadowExpectedToContribute &&
        execution.submittedFlashlight &&
        execution.flashlightBeamProfile.emitterRadiusMeters > 0.f;
    const bool skyVisibilityStochasticRequested =
        execution.rayTracedSkyVisibilityExpectedToContribute;
    execution.rayMarchingProducerTopologyReady =
        (!execution.directionalRayVisibilityExpectedToContribute ||
            (m_lighting->directionalRayVisibilityPass && execution.directShadowNoise)) &&
        (!execution.rayTracedFlashlightShadowExpectedToContribute ||
            (execution.submittedFlashlight && execution.directShadowNoise)) &&
        (!execution.rayTracedSkyVisibilityExpectedToContribute || execution.skyNoise);
    execution.rayMarchingDiagnostic =
        m_ui.LightingDebugView != PbrLightingDebugView::None;
    execution.rayMarchingAccumulationSelected =
        !execution.pathTracingSelected &&
        !execution.rayMarchingDiagnostic &&
        m_ui.AccumulateSamples;
    const bool lightingScheduleConsumerSelected =
        execution.directionalRayVisibilityExpectedToContribute ||
        execution.rayTracedFlashlightShadowExpectedToContribute ||
        execution.rayTracedSkyVisibilityExpectedToContribute;
    if (lightingScheduleConsumerSelected &&
        (!m_lighting->lightingAccumulationPass ||
            !m_lighting->lightingAccumulationPass->IsValid()))
    {
        return execution.Fail("lighting sample scheduling");
    }
    SynchronizeLightingAccumulationHistory(
        execution.renderSize.x,
        execution.renderSize.y,
        *execution.submittedLights,
        execution.rayScene,
        execution.lightingSceneContentChanged,
        execution.skyNoiseSettings,
        execution.directShadowNoiseSettings,
        execution.directionalRayVisibilitySelected,
        execution.directionalRayVisibilityExpectedToContribute,
        execution.rayTracedFlashlightShadowSelected,
        flashlightStochasticRequested,
        execution.rayTracedFlashlightShadowExpectedToContribute &&
            execution.submittedFlashlight && bool(execution.directShadowNoise),
        execution.rayTracedSkyVisibilitySelected,
        skyVisibilityStochasticRequested,
        execution.rayTracedSkyVisibilityExpectedToContribute && bool(execution.skyNoise));

    return true;
}

bool UvsrSceneViewer::PrepareLightingSchedule(FrameExecution& execution)
{

    execution.lightingSampleSchedulePrepared = false;
    if (m_lighting->lightingAccumulationPass)
    {
        execution.lightingSampleSchedule =
            m_lighting->lightingAccumulationPass->GetDisabledSchedule();
        if (execution.rayMarchingAccumulationSelected)
        {
            if (!execution.rayMarchingProducerTopologyReady ||
                !m_lighting->lightingAccumulationPass->IsValid())
            {
                return execution.Fail(
                    "lighting accumulation preparation");
            }
            execution.lightingSampleSchedule =
                m_lighting->lightingAccumulationPass->PrepareAttempts(
                    m_frame->commandList,
                    execution.renderSize.x,
                    execution.renderSize.y,
                    m_lighting->lightingHistoryEpoch);
            execution.lightingSampleSchedulePrepared =
                execution.lightingSampleSchedule.token != 0u;
            if (!execution.lightingSampleSchedulePrepared ||
                !execution.lightingSampleSchedule.enabled ||
                !execution.lightingSampleSchedule)
            {
                return execution.Fail(
                    "lighting accumulation preparation");
            }
        }
    }

    m_frame->submittedMainViewTriangles = 0u;
    EndRendererStage(RendererTimingStage::SceneSetup);
    execution.sceneColor = m_frame->renderTargets->HdrColor.Get();
    return true;
}

bool UvsrSceneViewer::RenderPathTracingFrame(FrameExecution& execution)
{
    PathTracingResult pathTracingResult;
    {
        PathTracingInputs pathInputs;
        pathInputs.view = m_frame->view.get();
        pathInputs.width = execution.presentationSize.x;
        pathInputs.height = execution.presentationSize.y;
        pathInputs.rayScene = execution.rayScene;
        pathInputs.environment = m_lighting->imageBasedLightingEnvironment
            ? m_lighting->imageBasedLightingEnvironment->GetRadianceTexture()
            : nullptr;
        pathInputs.environmentScale = m_lighting->imageBasedLightingEnvironment
            ? m_lighting->imageBasedLightingEnvironment->GetRadianceScale()
            : 0.f;
        pathInputs.showEnvironmentBackground =
            m_ui.ShowEnvironmentBackground;
        pathInputs.noiseSettings = m_ui.Noise;
        pathInputs.settings = m_ui.PathTracing;
        if (m_lighting->noiseTextureLibrary)
        {
            const NoiseTextureBinding pathNoise =
                m_lighting->noiseTextureLibrary->Resolve(
                    m_frame->commandList,
                    pathInputs.noiseSettings);
            pathInputs.noiseTexture = pathNoise.texture;
        }
        pathInputs.lights = execution.submittedLights;
        pathInputs.hardShadows = m_ui.DirectionalShadows.hardShadows;
        pathInputs.flashlight = execution.submittedFlashlight;
        pathInputs.flashlightProfile = execution.flashlightBeamProfile;
        pathInputs.historyEpoch = m_lighting->lightingHistoryEpoch;
        BeginRendererStage(RendererTimingStage::PathTransport);
        pathTracingResult = m_lighting->pathTracingPass->Render(
            m_frame->commandList,
            pathInputs);
        EndRendererStage(RendererTimingStage::PathTransport);
        m_lighting->pathTransportDispatchedThisFrame =
            pathTracingResult.dispatched;
    }
    m_lighting->selectedLightingTransportState = pathTracingResult
        ? SelectedLightingTransportState::PathTracingActive
        : SelectedLightingTransportState::PathTracingUnavailable;
    if (!pathTracingResult)
    {
        m_lighting->reportedPathTransportFailure = true;
        return execution.Fail("path tracing transport");
    }
    m_lighting->reportedPathTransportFailure = false;
    execution.sceneColor = pathTracingResult.sceneLinearDisplay;
    return true;
}

bool UvsrSceneViewer::RenderRayVisibility(FrameExecution& execution)
{
    m_frame->renderTargets->Clear(m_frame->commandList);

    execution.rasterSurface =
        m_frame->renderTargets->GetRasterSurface();
    if (!m_frame->gBufferGeometryPass ||
        !m_lighting->pbrDeferredLightingPass ||
        !m_lighting->pbrDeferredLightingPass->ArePipelinesReady())
    {
        return execution.Fail("deferred PBR preparation");
    }
    BeginRendererStage(RendererTimingStage::Geometry);
    const bool geometryRendered = RenderGeometry(
        *m_frame->gBufferGeometryPass,
        m_frame->renderTargets->GBufferFramebuffer.Get(),
        m_frame->view.get(),
        "GBufferFill");
    m_frame->submittedMainViewTriangles =
        m_frame->gBufferGeometryPass->GetSubmittedTriangles();
    EndRendererStage(RendererTimingStage::Geometry);
    if (!geometryRendered)
    {
        throw std::runtime_error(
            "UVSR G-buffer rendering failed");
    }

    const bool shadowRayDispatchExpected =
        execution.worldRepresentationReady &&
        ((execution.rayTracedFlashlightShadowSelected &&
                execution.submittedFlashlight &&
                execution.directShadowNoise) ||
            (execution.directionalRayVisibilitySelected &&
                m_lighting->directionalRayVisibilityPass &&
                m_lighting->sunLight));
    if (shadowRayDispatchExpected)
    {
        BeginRendererStage(
            RendererTimingStage::ShadowRayDispatch);
    }
    if (execution.rayTracedFlashlightShadowSelected &&
        execution.worldRepresentationReady &&
        m_lighting->rayTracedFlashlightShadowPass &&
        execution.submittedFlashlight &&
        execution.directShadowNoise)
    {
        execution.flashlightShadowResult =
            m_lighting->rayTracedFlashlightShadowPass->RenderFlashlight(
                m_frame->commandList,
                *m_frame->view,
                execution.rasterSurface,
                execution.rayScene,
                execution.submittedFlashlight,
                execution.flashlightBeamProfile,
                execution.directShadowNoiseSettings,
                execution.directShadowNoise.texture,
                execution.directShadowNoiseSettings.animate
                    ? uint32_t(
                        m_lighting->rayTracedFlashlightShadowPhase)
                    : 0u,
                DefaultFlashlightRayBiasMeters,
                execution.lightingSampleSchedule,
                ResolveRayShadowSampleCount(m_ui.DirectionalShadows));
    }
    m_lighting->rayTracedFlashlightShadowDispatchedThisFrame =
        execution.flashlightShadowResult.dispatched;
    const bool flashlightShadowContributed =
        bool(execution.flashlightShadowResult);
    if (flashlightShadowContributed !=
        m_lighting->rayTracedFlashlightShadowContributedLastFrame)
    {
        InvalidateRendererStageTiming(
            RendererTimingStage::ShadowRayDispatch);
        m_lighting->rayTracedFlashlightShadowContributedLastFrame =
            flashlightShadowContributed;
    }
    if (execution.directionalRayVisibilitySelected &&
        m_lighting->directionalRayVisibilityPass &&
        execution.worldRepresentationReady &&
        m_lighting->sunLight)
    {
        execution.directionalVisibilityResult =
            m_lighting->directionalRayVisibilityPass->RenderDirectional(
                m_frame->commandList,
                m_ui.DirectionalShadows,
                *m_frame->view,
                execution.rasterSurface,
                execution.rayScene,
                m_lighting->sunLight.get(),
                m_scene->sceneDiagonal,
                m_ui.Noise, execution.directShadowNoise.texture,
                m_ui.Noise.animate ? uint32_t(GetFrameIndex()) : 0u,
                execution.lightingSampleSchedule);
    }
    m_lighting->directionalRayVisibilityDispatchedThisFrame =
        execution.directionalVisibilityResult.dispatched;
    if (shadowRayDispatchExpected)
    {
        EndRendererStage(
            RendererTimingStage::ShadowRayDispatch);
    }
    if (execution.rayTracedFlashlightShadowExpectedToContribute &&
        !execution.flashlightShadowResult.dispatched)
    {
        return execution.Fail(
            "ray-traced flashlight visibility");
    }
    if (execution.directionalRayVisibilityExpectedToContribute &&
        !execution.directionalVisibilityResult.dispatched)
    {
        return execution.Fail("directional ray visibility");
    }
    if (execution.rayTracedSkyVisibilityExpectedToContribute &&
        m_lighting->rayTracedSkyVisibilityPass &&
        execution.skyNoise)
    {
        BeginRendererStage(
            RendererTimingStage::SkyVisibilityRayDispatch);
        execution.skyVisibilityResult =
            m_lighting->rayTracedSkyVisibilityPass->RenderSky(
                m_frame->commandList,
                m_ui.RayTracedSkyVisibility,
                *m_frame->view,
                execution.rasterSurface,
                execution.rayScene,
                execution.skyNoiseSettings,
                execution.skyNoise.texture,
                execution.skyNoiseSettings.animate
                    ? uint32_t(m_lighting->rayTracedSkyVisibilityPhase)
                    : 0u,
                m_scene->sceneDiagonal,
                execution.lightingSampleSchedule);
        EndRendererStage(
            RendererTimingStage::SkyVisibilityRayDispatch);
    }
    const bool rayTracedSkyVisibilityContributed =
        bool(execution.skyVisibilityResult);
    m_lighting->rayTracedSkyVisibilityDispatchedThisFrame =
        execution.skyVisibilityResult.dispatched;
    if (execution.rayTracedSkyVisibilityExpectedToContribute &&
        !execution.skyVisibilityResult.dispatched)
    {
        return execution.Fail("ray-traced sky visibility");
    }
    if (rayTracedSkyVisibilityContributed !=
        m_lighting->rayTracedSkyVisibilityContributedLastFrame)
    {
        InvalidateRendererStageTiming(
            RendererTimingStage::SkyVisibilityRayDispatch);
        m_lighting->rayTracedSkyVisibilityContributedLastFrame =
            rayTracedSkyVisibilityContributed;
    }

    return true;
}

bool UvsrSceneViewer::RenderDeferredLighting(FrameExecution& execution)
{
    DirectLightVisibilities directLightVisibilities;
    if (execution.flashlightShadowResult)
        directLightVisibilities.flashlight = { execution.flashlightShadowResult.visibility, execution.flashlightShadowResult.light };
    if (execution.directionalVisibilityResult)
        directLightVisibilities.sun = { execution.directionalVisibilityResult.visibility, execution.directionalVisibilityResult.light };
    nvrhi::ITexture* skyVisibility = execution.skyVisibilityResult ? execution.skyVisibilityResult.visibility : nullptr;
    const bool applySkyVisibilityToDiffuseIbl = skyVisibility && m_ui.RayTracedSkyVisibility.applyToDiffuseIbl;
    const bool applySkyVisibilityToSpecularIbl = skyVisibility && m_ui.RayTracedSkyVisibility.applyToSpecularIbl;
    PbrDeferredLightingInputs deferredInputs;
    deferredInputs.view = m_frame->view.get();
    deferredInputs.surface = execution.rasterSurface;
    deferredInputs.lights = execution.submittedLights;
    deferredInputs.hardShadows = m_ui.DirectionalShadows.hardShadows;
    deferredInputs.directLightVisibilities =
        directLightVisibilities;
    deferredInputs.flashlight = execution.submittedFlashlight;
    deferredInputs.flashlightBeamProfile =
        execution.flashlightBeamProfile;
    deferredInputs.environment = execution.globalEnvironment;
    deferredInputs.skyVisibility = skyVisibility;
    deferredInputs.applySkyVisibilityToDiffuseIbl =
        applySkyVisibilityToDiffuseIbl;
    deferredInputs.applySkyVisibilityToSpecularIbl =
        applySkyVisibilityToSpecularIbl;
    deferredInputs.lightingDebugView =
        uint32_t(m_ui.LightingDebugView);
    deferredInputs.output = m_frame->renderTargets->HdrColor;

    BeginRendererStage(RendererTimingStage::DirectLighting);
        const PbrDeferredLightingRenderResult lightingResult =
            m_lighting->pbrDeferredLightingPass->Render(
            m_frame->commandList,
            deferredInputs);
        EndRendererStage(RendererTimingStage::DirectLighting);
        if (!lightingResult.Succeeded())
        {
            return execution.Fail("direct lighting");
        }

    const bool expectedProducersCompleted =
        uvsr::RendererProducerDispatchContract{
            execution.directionalRayVisibilityExpectedToContribute,
            execution.directionalVisibilityResult.dispatched,
            execution.rayTracedFlashlightShadowExpectedToContribute,
            execution.flashlightShadowResult.dispatched,
            execution.rayTracedSkyVisibilityExpectedToContribute,
            execution.skyVisibilityResult.dispatched
        }.IsComplete();
    if (execution.lightingSampleSchedulePrepared &&
        execution.lightingSampleSchedule.enabled &&
        !expectedProducersCompleted)
    {
        return execution.Fail(
            "lighting accumulation producer transaction");
    }

    return true;
}

bool UvsrSceneViewer::RenderMaterialSelection(FrameExecution& execution)
{
    if (m_frame->materialPickPurpose != MaterialPickPurpose::None &&
        m_frame->materialPickScene != m_scene->world.get())
    {
        m_frame->materialPickPurpose = MaterialPickPurpose::None;
        m_frame->materialPickScene = nullptr;
        m_ui.ShowMaterialDrawer = false;
    }
    if (m_frame->materialPickPurpose ==
        MaterialPickPurpose::RefreshMaterialDrawerSelection)
    {
        const CenterMaterialPick centerPick =
            ResolveCenterMaterialPick(
                execution.presentationSize.x,
                execution.presentationSize.y);
        if (centerPick.valid)
        {
            m_frame->pickPosition =
                uint2(centerPick.x, centerPick.y);
        }
        else
        {
            m_frame->materialPickPurpose = MaterialPickPurpose::None;
            m_frame->materialPickScene = nullptr;
        }
    }
    if (m_frame->materialPickPurpose != MaterialPickPurpose::None)
    {
        if (!m_frame->renderTargets->EnsureMaterialPickingTargets(GetDevice()))
            return execution.Fail("material picking");
        if (!m_frame->materialIdGeometryPass)
            m_frame->materialIdGeometryPass = CreateGeometryPass(RendererGeometryOutput::MaterialId);
        if (!m_frame->pixelReadback)
            m_frame->pixelReadback = std::make_unique<RendererPixelReadback>(
                GetDevice(), m_frame->rendererShaderFactory, m_frame->renderTargets->MaterialIDs);
        if (!m_frame->pixelReadback->IsValid())
            return execution.Fail("material picking");
        BeginRendererStage(RendererTimingStage::MaterialPicking);
        m_frame->commandList->clearTextureUInt(
            m_frame->renderTargets->MaterialIDs,
            nvrhi::AllSubresources, 0xffffu);
        if (m_frame->renderTargets->MaterialIDDepth !=
            m_frame->renderTargets->Depth)
        {
            const nvrhi::FormatInfo& depthInfo =
                nvrhi::getFormatInfo(
                    m_frame->renderTargets->MaterialIDDepth
                        ->getDesc().format);
            m_frame->commandList->clearDepthStencilTexture(
                m_frame->renderTargets->MaterialIDDepth,
                nvrhi::AllSubresources,
                true,
                0.f,
                depthInfo.hasStencil,
                0u);
        }

        if (!RenderGeometry(
                *m_frame->materialIdGeometryPass,
                m_frame->renderTargets->MaterialIDFramebuffer.Get(),
                m_frame->view.get(),
                "MaterialID"))
        {
            EndRendererStage(
                RendererTimingStage::MaterialPicking);
            throw std::runtime_error(
                "UVSR material-ID rendering failed");
        }

        const nvrhi::TextureDesc& materialIdDescription =
            m_frame->renderTargets->MaterialIDs->getDesc();
        const uint32_t pickX = std::min(
            m_frame->pickPosition.x,
            materialIdDescription.width - 1u);
        const uint32_t pickY = std::min(
            m_frame->pickPosition.y,
            materialIdDescription.height - 1u);
        if (!m_frame->pixelReadback->Capture(
                m_frame->commandList,
                pickX,
                pickY))
        {
            uvsr::log::error("Material readback capture failed");
            m_frame->materialPickPurpose = MaterialPickPurpose::None;
            m_frame->materialPickScene = nullptr;
            m_ui.SelectedMaterial = nullptr;
            m_ui.SelectedNode = nullptr;
        }
        EndRendererStage(RendererTimingStage::MaterialPicking);
    }

    return true;
}

bool UvsrSceneViewer::ResolveSceneLighting(FrameExecution& execution)
{
    if (execution.environmentBackgroundSelected)
    {
        if (!m_lighting->imageBasedLightingBackgroundPass)
        {
            return execution.Fail("environment background");
        }
        BeginRendererStage(RendererTimingStage::EnvironmentBackground);
        const ImageBasedLightingBackgroundRenderResult backgroundResult =
            m_lighting->imageBasedLightingBackgroundPass->Render(
                m_frame->commandList,
                *m_frame->view,
                m_lighting->imageBasedLightingEnvironment->GetRadianceScale());
        EndRendererStage(RendererTimingStage::EnvironmentBackground);
        if (!backgroundResult.Succeeded())
        {
            return execution.Fail("environment background");
        }
    }

    if (execution.lightingSampleSchedulePrepared &&
        m_lighting->lightingAccumulationPass)
    {
        // Accumulation is the only long-term history owner in this mode.
        // Every accepted scene-linear sample has equal ownership in the progressive mean.
        const LightingAccumulationResult accumulationResult =
            m_lighting->lightingAccumulationPass->Resolve(
                m_frame->commandList,
                execution.sceneColor,
                execution.lightingSampleSchedule);
        if (!accumulationResult)
        {
            return execution.Fail("lighting accumulation commit");
        }
        execution.sceneColor = accumulationResult.sceneLinear;
        execution.lightingSampleSchedulePrepared = false;
#if defined(UVSR_BUILD_TESTING)
        m_lighting->lightingAccumulationCommittedThisFrame =
            accumulationResult.committed;
#endif
    }

    return true;
}



bool UvsrSceneViewer::RenderFramePresentation(FrameExecution& execution)
{
    const ICompositeView* postProcessingView = m_frame->view.get();

    const bool diagnosticExposureView =
        !execution.pathTracingSelected && execution.rayMarchingDiagnostic;
    const bool autoExposureExpected =
        m_ui.AutoExposure.enabled && !diagnosticExposureView;
    if (autoExposureExpected)
        BeginRendererStage(RendererTimingStage::AutoExposure);
    nvrhi::IBuffer* autoExposureBuffer = nullptr;
    if (autoExposureExpected && m_frame->autoExposurePass &&
        m_frame->autoExposurePass->IsAvailable())
    {
        autoExposureBuffer = m_frame->autoExposurePass->Render(
            m_frame->commandList,
            *postProcessingView,
            execution.sceneColor,
            m_ui.AutoExposure,
            m_frame->frameDeltaSeconds,
            false);
    }
    else if (m_frame->autoExposurePass)
    {
        // Disabled and diagnostic frames select the texture-only AgX
        // permutation. Its math is the exact pre-Auto-Exposure path.
        m_frame->autoExposurePass->Reset();
    }
    if (autoExposureExpected)
        EndRendererStage(RendererTimingStage::AutoExposure);
    m_frame->autoExposureDispatchedThisFrame =
        autoExposureExpected && m_frame->autoExposurePass &&
        m_frame->autoExposurePass->DidDispatchThisFrame();
    if (autoExposureExpected &&
        (!autoExposureBuffer || !m_frame->autoExposureDispatchedThisFrame))
    {
        return execution.Fail("auto exposure");
    }
#if defined(UVSR_BUILD_TESTING)
    if (m_frame->runtimeOutputCaptureRequested && execution.sceneColor)
    {
        const nvrhi::TextureDesc& sourceDesc =
            execution.sceneColor->getDesc();
        if ((sourceDesc.format == nvrhi::Format::RGBA16_FLOAT ||
                sourceDesc.format == nvrhi::Format::RGBA32_FLOAT) &&
            sourceDesc.sampleCount == 1u)
        {
            const RuntimeLinearReadbackLayout requestedLayout{
                sourceDesc.width,
                sourceDesc.height,
                static_cast<std::uint32_t>(sourceDesc.format),
                sourceDesc.sampleCount
            };
            bool recreate = !m_frame->runtimeLinearReadback;
            if (m_frame->runtimeLinearReadback)
            {
                const nvrhi::TextureDesc& currentDesc =
                    m_frame->runtimeLinearReadback->getDesc();
                recreate = !RuntimeLinearReadbackLayoutsMatch(
                    {
                        currentDesc.width,
                        currentDesc.height,
                        static_cast<std::uint32_t>(currentDesc.format),
                        currentDesc.sampleCount
                    },
                    requestedLayout);
            }
            if (recreate)
            {
                nvrhi::TextureDesc stagingDesc = sourceDesc;
                stagingDesc.isRenderTarget = false;
                stagingDesc.isUAV = false;
                stagingDesc.useClearValue = false;
                stagingDesc.debugName = "RuntimeLinearReadback";
                m_frame->runtimeLinearReadback = GetDevice()->createStagingTexture(
                    stagingDesc,
                    nvrhi::CpuAccessMode::Read);
                if (!m_frame->runtimeLinearReadback)
                {
                    uvsr::log::error(
                        "Runtime linear readback staging creation failed "
                        "for format %u at %ux%u",
                        static_cast<unsigned int>(sourceDesc.format),
                        sourceDesc.width,
                        sourceDesc.height);
                }
            }
            if (m_frame->runtimeLinearReadback)
            {
                m_frame->commandList->copyTexture(
                    m_frame->runtimeLinearReadback,
                    nvrhi::TextureSlice{},
                    execution.sceneColor,
                    nvrhi::TextureSlice{});
                m_frame->runtimeLinearReadbackQueued = true;
            }
        }
        else
        {
            uvsr::log::error(
                "Runtime linear readback source was incompatible: "
                "format %u, samples %u, extent %ux%u",
                static_cast<unsigned int>(sourceDesc.format),
                sourceDesc.sampleCount,
                sourceDesc.width,
                sourceDesc.height);
        }
    }
#endif
    nvrhi::ITexture* displayTexture = execution.sceneColor;
    BeginRendererStage(RendererTimingStage::ToneMapping);
    const bool toneMapped = m_frame->agxToneMappingPass &&
        m_frame->agxToneMappingPass->Render(
            m_frame->commandList,
            *postProcessingView,
            execution.sceneColor,
            autoExposureBuffer, m_ui.ToneMapping, m_frame->toneMappingLut);
    EndRendererStage(RendererTimingStage::ToneMapping);
    if (!toneMapped)
    {
        return execution.Fail("AgX tone mapping");
    }
    displayTexture = m_frame->renderTargets->LdrColor;

    if (execution.antiAliasing.fastApproximateEnabled)
    {
        if (!m_frame->fastApproximateAAPass ||
            !m_frame->fastApproximateAAPass->IsValid())
        {
            return execution.Fail("fast approximate anti-aliasing");
        }
        BeginRendererStage(RendererTimingStage::FastApproximate);
        displayTexture = m_frame->fastApproximateAAPass->Render(
            m_frame->commandList,
            *postProcessingView,
            displayTexture,
            execution.antiAliasing);
        EndRendererStage(RendererTimingStage::FastApproximate);
        if (!displayTexture)
        {
            return execution.Fail("fast approximate anti-aliasing");
        }
    }

    BeginRendererStage(RendererTimingStage::OutputBlit);
    const bool outputProduced = m_frame->agxToneMappingPass &&
        m_frame->agxToneMappingPass->RenderOutput(
            m_frame->commandList,
            *postProcessingView,
            execution.framebuffer,
            displayTexture);
    EndRendererStage(RendererTimingStage::OutputBlit);
    if (!outputProduced)
    {
        return execution.Fail("output blit");
    }
    return true;
}

void UvsrSceneViewer::CompleteSceneFrame(FrameExecution& execution)
{
    if (execution.pathTracingSelected && m_lighting->pathTracingPass)
        m_lighting->pathTracingPass->SubmitAcceptedSampleReadback();

    if (execution.flashlightShadowResult.dispatched &&
        execution.flashlightShadowResult.stochastic &&
        execution.directShadowNoiseSettings.animate)
    {
        ++m_lighting->rayTracedFlashlightShadowPhase;
    }
    if (execution.skyVisibilityResult.dispatched &&
        execution.skyNoiseSettings.animate)
    {
        ++m_lighting->rayTracedSkyVisibilityPhase;
    }
    if (m_ui.CopyScreenshotToClipboard)
    {
        const std::filesystem::path screenshotPath = std::filesystem::temp_directory_path()
            / ("uvsr_screenshot_" + std::to_string(GetCurrentProcessId()) + ".bmp");
        const bool saved = uvsr::SaveRendererTextureBmp(
            GetDevice(),
            m_frame->rendererCommonPasses.get(),
            execution.framebufferTexture,
            nvrhi::ResourceStates::RenderTarget,
            screenshotPath);
        if (saved && CopyBmpToClipboard(screenshotPath))
            uvsr::log::info("Capture copied to clipboard.");
        else
            uvsr::log::error("Failed to copy screenshot to clipboard.");
        DeleteFileW(screenshotPath.c_str());
        m_ui.CopyScreenshotToClipboard = false;
    }

#if defined(UVSR_BUILD_TESTING)
    if (m_frame->runtimeOutputCaptureRequested)
    {
        const std::filesystem::path capturePath =
            m_frame->runtimeOutputCapturePath;
        std::error_code directoryError;
        std::filesystem::create_directories(
            capturePath.parent_path(), directoryError);
        const bool captured = uvsr::SaveRendererTextureBmp(
            GetDevice(),
            m_frame->rendererCommonPasses.get(),
            execution.framebufferTexture,
            nvrhi::ResourceStates::RenderTarget,
            capturePath);

        RuntimeOutputEvidence evidence;
        if (m_frame->runtimeLinearReadbackQueued && m_frame->runtimeLinearReadback)
        {
            size_t rowPitch = 0u;
            const void* pixels = GetDevice()->mapStagingTexture(
                m_frame->runtimeLinearReadback,
                nvrhi::TextureSlice{},
                nvrhi::CpuAccessMode::Read,
                &rowPitch);
            if (!pixels)
            {
                uvsr::log::error(
                    "Runtime linear readback mapping failed with row "
                    "pitch %zu",
                    rowPitch);
            }
            const nvrhi::TextureDesc& linearDesc =
                m_frame->runtimeLinearReadback->getDesc();
            if (linearDesc.format == nvrhi::Format::RGBA16_FLOAT)
            {
                evidence = AnalyzeRuntimeLinearRgba16(
                    pixels,
                    linearDesc.width,
                    linearDesc.height,
                    rowPitch);
            }
            else if (linearDesc.format == nvrhi::Format::RGBA32_FLOAT)
            {
                evidence = AnalyzeRuntimeLinearRgba32(
                    pixels,
                    linearDesc.width,
                    linearDesc.height,
                    rowPitch);
            }
            if (pixels)
                GetDevice()->unmapStagingTexture(m_frame->runtimeLinearReadback);
        }
        evidence.artifactPath = capturePath.string();
        std::ifstream input;
        if (captured)
            input.open(capturePath, std::ios::binary);
        const std::vector<unsigned char> encoded{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        input.close();

        const auto readU32 = [&encoded](size_t offset)
        {
            if (offset + 4u > encoded.size())
                return 0u;
            return uint32_t(encoded[offset]) |
                (uint32_t(encoded[offset + 1u]) << 8u) |
                (uint32_t(encoded[offset + 2u]) << 16u) |
                (uint32_t(encoded[offset + 3u]) << 24u);
        };
        const uint32_t pixelOffset = readU32(10u);
        evidence.width = readU32(18u);
        evidence.height = readU32(22u);
        evidence.encodedBytes = encoded.size();
        if (encoded.size() >= 54u && encoded[0] == 'B' &&
            encoded[1] == 'M' && pixelOffset < encoded.size() &&
            evidence.width > 0u && evidence.height > 0u &&
            !directoryError)
        {
            evidence.pixelBytes = encoded.size() - pixelOffset;
            evidence.minimumByte = 0xffu;
            for (size_t index = pixelOffset; index < encoded.size(); ++index)
            {
                const unsigned char byte = encoded[index];
                evidence.minimumByte = std::min(
                    evidence.minimumByte, byte);
                evidence.maximumByte = std::max(
                    evidence.maximumByte, byte);
                evidence.pixelHash ^= uint64_t(byte);
                evidence.pixelHash *= 1099511628211ull;
            }
            evidence.valid = evidence.pixelBytes > 0u;
        }
        m_frame->runtimeOutputEvidence = evidence;
        m_frame->runtimeOutputCaptureRequested = false;
    }
#endif

    if (m_frame->materialPickPurpose != MaterialPickPurpose::None)
    {
        const MaterialPickPurpose completedPurpose =
            m_frame->materialPickPurpose;
        const Scene* completedScene = m_frame->materialPickScene;
        m_frame->materialPickPurpose = MaterialPickPurpose::None;
        m_frame->materialPickScene = nullptr;
        const std::optional<uvsr::RendererReadbackUint4> pixelValue =
            m_frame->pixelReadback->ReadUInts();
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;

        const bool completedForCurrentScene =
            pixelValue && completedScene == m_scene->world.get();
        if (!pixelValue)
            uvsr::log::error("Material readback result was unavailable");
        if (completedForCurrentScene)
        {
            for (const auto& material :
                m_scene->world->GetSceneGraph()->GetMaterials())
            {
                if (material->materialID == int(pixelValue->x))
                {
                    m_ui.SelectedMaterial = material;
                    break;
                }
            }

            for (const auto& instance :
                m_scene->world->GetSceneGraph()->GetMeshInstances())
            {
                if (instance->GetInstanceIndex() == int(pixelValue->y))
                {
                    m_ui.SelectedNode =
                        instance->GetNodeSharedPtr();
                    break;
                }
            }
        }

        if (completedPurpose ==
            MaterialPickPurpose::RefreshMaterialDrawerSelection)
        {
            if (m_ui.SelectedMaterial)
            {
                uvsr::log::info(
                    "Center material: %s",
                    m_ui.SelectedMaterial->name.c_str());
            }
        }
        else if (completedForCurrentScene &&
            completedPurpose ==
                MaterialPickPurpose::FocusCameraAtCursor)
        {
            if (m_ui.SelectedNode)
            {
                uvsr::log::info(
                    "Picked node: %s",
                    m_ui.SelectedNode->GetPath()
                        .generic_string().c_str());
                PointThirdPersonCameraAt(m_ui.SelectedNode);
            }
            else
            {
                PointThirdPersonCameraAt(
                    m_scene->world->GetSceneGraph()->GetRootNode());
            }
        }
    }
}

void UvsrSceneViewer::RenderScene(nvrhi::IFramebuffer* framebuffer)
{
    if (m_scene->sceneGpuUploadPending)
    {
        RenderSceneGpuUploadFrame(framebuffer);
        return;
    }
    FrameExecution execution{ *this, framebuffer };
    PrepareFrameTargets(execution);
    if (!PrepareWorldRepresentation(execution) || !PrepareLightingInputs(execution) ||
        !PrepareLightingSchedule(execution))
        return;

    switch (m_ui.Lighting)
    {
    case LightingSolution::PathTracing:
        if (!RenderPathTracingFrame(execution))
            return;
        break;
    case LightingSolution::RayMarching:
        m_lighting->selectedLightingTransportState = SelectedLightingTransportState::RayMarching;
        m_lighting->reportedPathTransportFailure = false;
        if (!RenderRayVisibility(execution) || !RenderDeferredLighting(execution))
            return;
        break;
    }

    if (!RenderMaterialSelection(execution) || !ResolveSceneLighting(execution) ||
        !RenderFramePresentation(execution))
        return;
    execution.Submit();
    CompleteSceneFrame(execution);
}
