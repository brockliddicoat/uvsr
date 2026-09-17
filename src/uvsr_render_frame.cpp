#include "uvsr_scene_viewer.h"
#include "renderer_nvrhi_message_callback.h"
#include "renderer_view.h"
#include "uvsr_renderer_scene_nvrhi.h"
#include "uvsr_renderer_lighting_nvrhi.h"
#include "uvsr_renderer_frame_nvrhi.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include "renderer_pixel_readback_nvrhi.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>
#include "renderer_producer_contract.h"
#include "renderer_statistics.h"
#include "renderer_texture_bmp_nvrhi.h"
#include "windows_executable_path.h"
#include "windows_path_text.h"
#include "settings_value.h"
#include <Windows.h>
#include <cstdio>
#if defined(UVSR_BUILD_TESTING)
#include "file_write.h"
#include "retained_runtime_capture_file.h"
#endif

using namespace donut;
using namespace donut::app;
using namespace uvsr;

namespace
{
constexpr float DefaultFlashlightRayBiasMeters = 0.002f;
bool CopyBmpToClipboard(const wchar_t* fileName)
{
    HANDLE file = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, nullptr,
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
    uint64_t errorsBefore = renderer.m_nvrhiMessages.GetErrorCount();
    bool commandOpen = false;
    // a closed list can still be unsubmitted. failure keeps it terminal until teardown.
    bool recording = false;
    bool pathTracingSelected{};
    bool pathTracingSceneDomainSupported{};
#if defined(UVSR_BUILD_TESTING)
    bool runtimePathDispatched = false;
    bool runtimePathHistoryReset = false;
    uint32_t runtimeSkySamplePhase = 0u;
    uint32_t runtimeDirectionalSamplePhase = 0u;
    uint32_t runtimeFlashlightSamplePhase = 0u;
#endif
    DirectX::XMUINT2 presentationSize{};
    DirectX::XMUINT2 renderSize{};
    bool lightingSceneContentChanged{};
    RendererSceneLightRange submittedLights;
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
    RendererSceneHandle submittedFlashlight;
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
    RendererPixelReadback* recordedReadback = nullptr;

    void Open()
    {
        renderer.m_frame->commandList->open();
        renderer.m_scene->gpuTables.BeginRecording();
        commandOpen = true;
        recording = true;
    }
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
        renderer.m_frame->commandList->close();
        commandOpen = false;
        if (renderer.m_nvrhiMessages.GetErrorCount() != errorsBefore)
        {
            return Fail("graphics command recording");
        }
        const uint64_t submission = renderer.GetDevice()->executeCommandList(renderer.m_frame->commandList);
        if (submission == 0)
            return Fail("graphics queue submission");
        recording = false;
        renderer.m_scene->gpuTables.CommitRecording();
        renderer.CompleteRendererTimerFrame();
        if (recordedReadback)
        {
            auto* submittedReadback = recordedReadback;
            recordedReadback = nullptr;
            if (submittedReadback->NotifySubmitted(submission) != RendererReadbackError::None)
                return Fail("material readback submission");
        }
        if (renderer.m_nvrhiMessages.GetErrorCount() != errorsBefore)
            return Fail("graphics queue execution");
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
        if (!recording)
            return;
        if (lightingSampleSchedulePrepared)
            renderer.m_lighting->lightingAccumulationPass->CancelPreparedSchedule(lightingSampleSchedule);
        if (recordedReadback)
            recordedReadback->CancelRecorded();
        auto& frame = *renderer.m_frame;
        const uint32_t slot = frame.rendererTimerFrame % RendererFrameState::TimerLatency;
        for (size_t stage = 0; stage < frame.rendererTimerActive.size(); ++stage)
        {
            if (commandOpen)
                renderer.EndRendererStage(static_cast<RendererTimingStage>(stage));
            // Only this frame's free slot can contain unsubmitted queries.
            if (frame.rendererTimerFrameWritable && frame.rendererTimerPending[stage][slot])
            {
                renderer.GetDevice()->resetTimerQuery(frame.rendererTimerQueries[stage][slot]);
                frame.rendererTimerPending[stage][slot] = false;
                frame.rendererTimings.available[stage] = false;
            }
        }
        if (commandOpen)
            frame.commandList->close();
        renderer.m_scene->gpuTables.AbortRecording();
        renderer.ResetImageBasedLightingHistory();
    }
};

bool UvsrSceneViewer::PrepareFrameTargets(FrameExecution& execution)
{
    execution.pathTracingSelected =
        m_ui.Lighting == LightingSolution::PathTracing;
    const PathTracingSceneDomainStatus pathTracingSceneDomainStatus =
        execution.pathTracingSelected
            ? GetPathTracingSceneDomainStatus()
            : PathTracingSceneDomainStatus::Supported;
    execution.pathTracingSceneDomainSupported =
        pathTracingSceneDomainStatus !=
            PathTracingSceneDomainStatus::Unsupported;
    const bool pathTracingPassRequired = execution.pathTracingSelected &&
        execution.pathTracingSceneDomainSupported && m_ui.Representation.allowRayTraversal && m_scene->bindlessLayout;
    if (execution.pathTracingSelected)
    {
        if (!EnsurePathTracingPass(pathTracingPassRequired)) return false;
        if (m_lighting->pathTracingPass)
            m_lighting->pathTracingPass->PollAcceptedSampleReadback();
    }
    int windowWidth, windowHeight;
    GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
    execution.presentationSize = DirectX::XMUINT2{
        uint32_t(windowWidth),
        uint32_t(windowHeight)
    };

    execution.renderSize = execution.presentationSize;

    if (!UpdateFlashlightTransform())
        return execution.Fail("flashlight pose transaction");
    execution.lightingSceneContentChanged =
        m_scene->canonical.View().contentRevision != m_scene->lastSubmittedContentRevision;
    execution.submittedLights = {m_scene->canonical.View(), m_lighting->flashlight,
        ShouldSubmitFlashlight(m_lighting->flashlightTransition)};

    {
        const bool fastApproximateAARequired = m_ui.UsesFastApproximateAA();
        bool needNewPasses = false;

        if (!m_frame->renderTargets || m_frame->renderTargets->IsUpdateRequired(
            execution.renderSize,
            execution.presentationSize,
            !execution.pathTracingSelected))
        {
            std::unique_ptr<RenderTargets> candidate(new (std::nothrow) RenderTargets());
            if (!candidate || !candidate->Init(
                    GetDevice(), execution.renderSize,
                    execution.presentationSize,
                    true,
                    !execution.pathTracingSelected))
                return execution.Fail("render target initialization");
            if (m_lighting->directionalRayVisibilityPass)
                m_lighting->directionalRayVisibilityPass->ResetBindingCache();
            if (m_lighting->rayTracedFlashlightShadowPass)
                m_lighting->rayTracedFlashlightShadowPass->ResetBindingCache();
            if (m_lighting->rayTracedSkyVisibilityPass)
                m_lighting->rayTracedSkyVisibilityPass->ResetBindingCache();
            m_frame->renderTargets = std::move(candidate);

            needNewPasses = true;
        }

        bool viewChanged = false;
        if (!SetupView(viewChanged))
            return execution.Fail("view preparation");
        needNewPasses = needNewPasses || viewChanged;

        if (m_ui.ShaderReloadRequested)
        {
            WindowsPath directory;
            WindowsPathResult pathResult;
            if (!GetExecutableDirectoryWide(directory, pathResult))
                return execution.Fail("shader reload executable directory");
            WindowsPath parentDirectory, environmentDirectory;
            if (!ExecutableDirectoryFromModulePath(directory.Data(), directory.Size(), parentDirectory, pathResult) ||
                !JoinWindowsRelativePath(parentDirectory.Data(), L"media/environments", environmentDirectory, pathResult))
                return execution.Fail("shader reload environment directory");
            m_frame->rendererShaderFactory->ClearCache();
            // retained environments keep their shader handles across cache clearing.
            std::unique_ptr<ImageBasedLightingEnvironment> environment(new (std::nothrow) ImageBasedLightingEnvironment(
                GetDevice(), m_frame->rendererShaderFactory.get(), m_frame->rendererCommonPasses.get(),
                static_cast<WindowsPath&&>(environmentDirectory)));
            if (!environment || environment->HasPreparedRadianceFailed())
                return execution.Fail("shader reload environment initialization");
            const bool recreatePathTracingPass =
                execution.pathTracingSelected && bool(m_lighting->pathTracingPass);
            if (recreatePathTracingPass && !EnsurePathTracingPass(pathTracingPassRequired, true)) return false;
            m_lighting->directionalRayVisibilityPass.reset();
            m_lighting->rayTracedFlashlightShadowPass.reset();
            m_lighting->rayTracedSkyVisibilityPass.reset();
            if (!recreatePathTracingPass)
                m_lighting->pathTracingPass.reset();
            m_lighting->lightingAccumulationPass.reset();
            m_lighting->imageBasedLightingEnvironment = std::move(environment);
            InvalidateLightingAccumulationHistory();
            needNewPasses = true;
        }

        if (needNewPasses)
        {
            BeginRenderPassPreparation(false);
            PreparationResult result;
            do
            {
                result = ProcessRenderPassPreparationStep();
            } while (result == PreparationResult::Pending);
            if (result == PreparationResult::Failed) return false;
        }
        // Fast Approximate is a presentation-only spatial filter. Its
        // resources follow the tone-mapped presentation target.
        if (fastApproximateAARequired && !m_frame->fastApproximateAAPass)
        {
            if (!CreateFastApproximateAAPass()) return false;
        }
        else if (!fastApproximateAARequired && m_frame->fastApproximateAAPass)
            m_frame->fastApproximateAAPass.reset();

        m_ui.ShaderReloadRequested = false;
    }

    if (!execution.pathTracingSelected)
    {
        if (!EnsureDirectionalRayVisibilityPass() || !EnsureRayTracedFlashlightShadowPass() ||
            !EnsureRayTracedSkyVisibilityPass()) return false;
    }
    return true;
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
    if (!m_scene->gpuTables.RecordMaterials(m_frame->commandList, m_scene->canonical.View()).Succeeded())
        return execution.Fail("canonical material upload");
    if (!m_scene->gpuTables.RecordInstances(m_frame->commandList, m_scene->canonical.View()).Succeeded())
        return execution.Fail("canonical instance upload");
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
            m_scene->canonical.View(),
            m_scene->gpuTables,
            m_ui.Representation,
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
        ? m_scene->worldSpaceRepresentation->GetRaySceneView(m_scene->canonical.View(), m_scene->gpuTables)
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
            ? m_lighting->flashlight
            : RendererSceneHandle{};
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
        execution.submittedLights,
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
        pathInputs.view = &m_frame->view;
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
#if defined(UVSR_BUILD_TESTING)
        if (m_frame->runtimeOutputCaptureRequested && m_frame->runtimeCaptureSettlingFrames == 0u)
        {
            WindowsPathText captureText;
            WindowsPathTextResult captureError;
            if (!GetRuntimeCaptureStem(m_frame->runtimeOutputCapturePath, captureText, captureError))
            {
                uvsr::log::error("Runtime capture input label failed (%u, code %u)",
                    unsigned(captureError.error), captureError.nativeCode);
                m_frame->FailRuntimeOutputCapture();
                return false;
            }
            const std::string_view capture(captureText.Data(), captureText.Size());
            if (capture == "case-25-path-history-global-noise-reset-baseline")
                pathInputs.runtimeInputProbeCase = 25u;
            else if (capture == "case-26-path-history-material-reset-baseline")
                pathInputs.runtimeInputProbeCase = 26u;
            if (pathInputs.runtimeInputProbeCase != 0u)
                pathInputs.runtimeInputProbeDispatch = m_frame->runtimeCaptureSequence.PathDispatchCount() + 1u;
        }
#endif
        BeginRendererStage(RendererTimingStage::PathTransport);
        pathTracingResult = m_lighting->pathTracingPass->Render(
            m_frame->commandList,
            pathInputs);
        EndRendererStage(RendererTimingStage::PathTransport);

        m_lighting->pathTransportDispatchedThisFrame =
            pathTracingResult.dispatched;
#if defined(UVSR_BUILD_TESTING)
        execution.runtimePathDispatched = pathTracingResult.dispatched;
        execution.runtimePathHistoryReset = pathTracingResult.historyReset;
#endif
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

bool UvsrSceneViewer::RenderFrameGeometry(FrameExecution& execution)
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
        &m_frame->view,
        "GBufferFill");
    m_frame->submittedMainViewTriangles =
        m_frame->gBufferGeometryPass->GetSubmittedTriangles();
    EndRendererStage(RendererTimingStage::Geometry);
    if (!geometryRendered)
    {
        return FailRender("UVSR G-buffer rendering failed");
    }

    return true;
}

bool UvsrSceneViewer::RenderRayVisibility(FrameExecution& execution)
{
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
        uint32_t flashlightSamplePhase = execution.directShadowNoiseSettings.animate
            ? uint32_t(m_lighting->rayTracedFlashlightShadowPhase) : 0u;
#if defined(UVSR_BUILD_TESTING)
        if (m_frame->runtimeOutputCaptureRequested)
            flashlightSamplePhase = RuntimeCaptureSequence::SamplePhase();
        execution.runtimeFlashlightSamplePhase = flashlightSamplePhase;
#endif
        execution.flashlightShadowResult =
            m_lighting->rayTracedFlashlightShadowPass->RenderFlashlight(
                m_frame->commandList,
                m_frame->view,
                execution.rasterSurface,
                execution.rayScene,
                execution.submittedLights.scene,
                execution.submittedFlashlight,
                execution.flashlightBeamProfile,
                execution.directShadowNoiseSettings,
                execution.directShadowNoise.texture,
                flashlightSamplePhase,
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
        uint32_t directionalSamplePhase = m_ui.Noise.animate ? uint32_t(GetFrameIndex()) : 0u;
#if defined(UVSR_BUILD_TESTING)
        if (m_frame->runtimeOutputCaptureRequested)
            directionalSamplePhase = RuntimeCaptureSequence::SamplePhase();
        execution.runtimeDirectionalSamplePhase = directionalSamplePhase;
#endif
        execution.directionalVisibilityResult =
            m_lighting->directionalRayVisibilityPass->RenderDirectional(
                m_frame->commandList,
                m_ui.DirectionalShadows,
                m_frame->view,
                execution.rasterSurface,
                execution.rayScene,
                execution.submittedLights.scene,
                m_lighting->sunLight,
                m_scene->sceneDiagonal,
                m_ui.Noise, execution.directShadowNoise.texture,
                directionalSamplePhase,
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
        uint32_t skySamplePhase = execution.skyNoiseSettings.animate
            ? uint32_t(m_lighting->rayTracedSkyVisibilityPhase) : 0u;
#if defined(UVSR_BUILD_TESTING)
        if (m_frame->runtimeOutputCaptureRequested)
            skySamplePhase = RuntimeCaptureSequence::SamplePhase();
        execution.runtimeSkySamplePhase = skySamplePhase;
#endif
        execution.skyVisibilityResult =
            m_lighting->rayTracedSkyVisibilityPass->RenderSky(
                m_frame->commandList,
                m_ui.RayTracedSkyVisibility,
                m_frame->view,
                execution.rasterSurface,
                execution.rayScene,
                execution.skyNoiseSettings,
                execution.skyNoise.texture,
                skySamplePhase,
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
    deferredInputs.view = &m_frame->view;
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
        m_frame->materialPickGeneration != m_scene->canonical.View().generation)
    {
        m_frame->materialPickPurpose = MaterialPickPurpose::None;
        m_frame->materialPickGeneration = 0;
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
                gpu_contract::Uint2{centerPick.x, centerPick.y};
        }
        else
        {
            m_frame->materialPickPurpose = MaterialPickPurpose::None;
            m_frame->materialPickGeneration = 0;
        }
    }
    if (m_frame->materialPickPurpose != MaterialPickPurpose::None)
    {
        if (!m_frame->renderTargets->EnsureMaterialPickingTargets(GetDevice()))
            return execution.Fail("material picking");
        if (!m_frame->materialIdGeometryPass)
        {
            auto candidate = CreateGeometryPass(RendererGeometryOutput::MaterialId);
            if (!candidate) return false;
            m_frame->materialIdGeometryPass = std::move(candidate);
        }
        if (!m_frame->pixelReadback)
        {
            std::unique_ptr<RendererPixelReadback> candidate(new (std::nothrow) RendererPixelReadback());
            if (!candidate) return execution.Fail("material readback allocation");
            const auto shader = m_frame->rendererShaderFactory->CreateShader(
                "uvsr/renderer_pixel_readback_cs.hlsl", "main", {}, nvrhi::ShaderType::Compute);
            const RendererReadbackError initialized = RendererPixelReadbackNvrhi::Initialize(
                *candidate, GetDevice(), m_frame->commandList, shader, m_frame->renderTargets->MaterialIDs);
            if (initialized != RendererReadbackError::None)
                return execution.Fail("material readback initialization");
            m_frame->pixelReadback = std::move(candidate);
        }
        if (!m_frame->pixelReadback->IsValid())
            return execution.Fail("material picking");
        BeginRendererStage(RendererTimingStage::MaterialPicking);
        m_frame->commandList->clearTextureUInt(
            m_frame->renderTargets->MaterialIDs,
            nvrhi::AllSubresources, RendererInvalidPickId);
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
                &m_frame->view,
                "MaterialID"))
        {
            EndRendererStage(
                RendererTimingStage::MaterialPicking);
            return FailRender("UVSR material-ID rendering failed");
        }

        const nvrhi::TextureDesc& materialIdDescription =
            m_frame->renderTargets->MaterialIDs->getDesc();
        const uint32_t pickX = std::min(
            m_frame->pickPosition.x,
            materialIdDescription.width - 1u);
        const uint32_t pickY = std::min(
            m_frame->pickPosition.y,
            materialIdDescription.height - 1u);
        if (m_frame->pixelReadback->Capture({ pickX, pickY }) != RendererReadbackError::None)
        {
            uvsr::log::error("Material readback capture failed");
            m_frame->materialPickPurpose = MaterialPickPurpose::None;
            m_frame->materialPickGeneration = 0;
            m_ui.SelectedMaterial = {};
            m_ui.SelectedNode = {};
        }
        else
        {
            execution.recordedReadback = m_frame->pixelReadback.get();
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
                m_frame->view,
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
    const RendererView* postProcessingView = &m_frame->view;

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
    if (m_frame->runtimeOutputCaptureRequested &&
        m_frame->runtimeCaptureSequence.IsReady() && execution.sceneColor)
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

#if defined(UVSR_BUILD_TESTING)
    if (m_frame->runtimeCaptureDrainBeforeSampling)
    {
        m_frame->runtimeCaptureDrainBeforeSampling = false;
        // match the completion boundary before the first counted sample, not mid-command-list.
        const bool drained = GetDevice()->waitForIdle();
        WindowsPathText captureStem;
        WindowsPathTextResult stemError;
        if (!GetRuntimeCaptureStem(m_frame->runtimeOutputCapturePath, captureStem, stemError))
        {
            uvsr::log::error("Runtime capture drain label failed (%u, code %u)",
                unsigned(stemError.error), stemError.nativeCode);
            m_frame->FailRuntimeOutputCapture();
            return;
        }
        std::fprintf(stdout, "{\"event\":\"capture-ready-drain\",\"capture\":\"%s\",\"valid\":%s}\n",
            captureStem.Data(), drained ? "true" : "false");
        if (!drained)
        {
            uvsr::log::error("Runtime capture completion drain failed");
            m_frame->runtimeOutputEvidence = RuntimeOutputEvidence{};
            m_frame->runtimeOutputCaptureRequested = false;
            return;
        }
    }
#endif

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
        WindowsPath temporaryDirectory, screenshotPath;
        WindowsPathResult pathResult;
        if (!GetTemporaryDirectoryWide(temporaryDirectory, pathResult))
        {
            uvsr::log::error("screenshot directory could not be prepared: error %u, native %u",
                unsigned(pathResult.error), pathResult.nativeCode);
            return;
        }
        wchar_t filename[sizeof(L"uvsr_screenshot_4294967295.bmp") / sizeof(wchar_t)]{};
        const int length = swprintf_s(filename, sizeof(filename) / sizeof(filename[0]),
            L"uvsr_screenshot_%lu.bmp", GetCurrentProcessId());
        if (length <= 0 || size_t(length) >= sizeof(filename) / sizeof(filename[0]))
        {
            uvsr::log::error("screenshot filename could not be formatted");
            return;
        }
        if (!JoinWindowsRelativePath(temporaryDirectory.Data(), filename, screenshotPath, pathResult))
        {
            uvsr::log::error("screenshot path could not be prepared: error %u, native %u",
                unsigned(pathResult.error), pathResult.nativeCode);
            return;
        }
        const bool saved = uvsr::SaveRendererTextureBmp(
            GetDevice(),
            m_frame->rendererCommonPasses.get(),
            execution.framebufferTexture,
            nvrhi::ResourceStates::RenderTarget,
            screenshotPath.Data());
        if (saved && CopyBmpToClipboard(screenshotPath.Data()))
            uvsr::log::info("Capture copied to clipboard.");
        else
            uvsr::log::error("Failed to copy screenshot to clipboard.");
        DeleteFileW(screenshotPath.Data());
        m_ui.CopyScreenshotToClipboard = false;
    }

#if defined(UVSR_BUILD_TESTING)
    if (m_frame->runtimeOutputCaptureRequested && m_frame->runtimeCaptureSequence.IsReady())
    {
        const WindowsPath& capturePath = m_frame->runtimeOutputCapturePath;
        FileWriteResult directoryError;
        const bool directoryReady = PrepareFileParentDirectories(capturePath.Data(), directoryError);
        const bool captured = uvsr::SaveRendererTextureBmp(
            GetDevice(),
            m_frame->rendererCommonPasses.get(),
            execution.framebufferTexture,
            nvrhi::ResourceStates::RenderTarget,
            capturePath.Data());

        // complete the capture before the fixture advances to another scene or action.
        const bool captureCompleted = GetDevice()->waitForIdle();
        WindowsPathText captureStem;
        WindowsPathTextResult stemError;
        if (!GetRuntimeCaptureStem(capturePath, captureStem, stemError))
        {
            uvsr::log::error("Runtime capture drain label failed (%u, code %u)",
                unsigned(stemError.error), stemError.nativeCode);
            m_frame->FailRuntimeOutputCapture();
            return;
        }
        std::fprintf(stdout, "{\"event\":\"capture-complete-drain\",\"capture\":\"%s\",\"valid\":%s}\n",
            captureStem.Data(), captureCompleted ? "true" : "false");
        if (!captureCompleted)
        {
            uvsr::log::error("Runtime capture final completion failed");
            m_frame->runtimeOutputEvidence = RuntimeOutputEvidence{};
            m_frame->runtimeOutputCaptureRequested = false;
            return;
        }

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
        evidence.deterministicCapture = true;
        evidence.capturedPathDispatchCount = m_frame->runtimeCaptureSequence.PathDispatchCount();
        evidence.capturedSkySamplePhaseValid = m_frame->runtimeCaptureSequence.SkySamplePhaseValid();
        evidence.capturedSkySamplePhase = m_frame->runtimeCaptureSequence.SkySamplePhase();
        evidence.capturedRasterProducerMask = m_frame->runtimeCaptureSequence.RasterProducerMask();
        evidence.capturedDirectionalSamplePhase = m_frame->runtimeCaptureSequence.DirectionalSamplePhase();
        evidence.capturedFlashlightSamplePhase = m_frame->runtimeCaptureSequence.FlashlightSamplePhase();
        WindowsPathTextResult artifactError;
        if (!evidence.artifactPath.Assign(capturePath.Data(), capturePath.Size(),
                WindowsPathTextForm::Native, WindowsPathTextEncoding::Filesystem, artifactError))
        {
            uvsr::log::error("Runtime capture artifact label failed (%u, code %u)",
                unsigned(artifactError.error), artifactError.nativeCode);
            m_frame->FailRuntimeOutputCapture();
            return;
        }
        RuntimeCaptureFileResult readError;
        if (!AnalyzeRuntimeCaptureBmp(capturePath.Data(), captured, directoryReady, evidence, readError))
            uvsr::log::error("Runtime capture file analysis failed (%u, code %u, close %u)",
                unsigned(readError.error), readError.code, readError.cleanupCode);
        m_frame->runtimeOutputEvidence = std::move(evidence);
        m_frame->runtimeOutputCaptureRequested = false;
    }
#endif

    if (m_frame->materialPickPurpose != MaterialPickPurpose::None)
    {
        const MaterialPickPurpose completedPurpose =
            m_frame->materialPickPurpose;
        const uint64_t completedGeneration = m_frame->materialPickGeneration;
        m_frame->materialPickPurpose = MaterialPickPurpose::None;
        m_frame->materialPickGeneration = 0;
        RendererReadbackUint4 pixelValue{};
        const bool pixelAvailable =
            m_frame->pixelReadback->ReadUInts(pixelValue) == RendererReadbackError::None;
        m_ui.SelectedMaterial = {};
        m_ui.SelectedNode = {};

        const auto scene = m_scene->canonical.View();
        const bool completedForCurrentScene =
            pixelAvailable && scene.generation && completedGeneration == scene.generation;
        if (!pixelAvailable)
            uvsr::log::error("Material readback result was unavailable");
        if (completedForCurrentScene)
        {
            m_ui.SelectedMaterial = FindRendererSceneMaterialSelection(scene, pixelValue.x);
            if (pixelValue.y < scene.instances.count)
                m_ui.SelectedNode = {scene.generation, scene.instances.data[pixelValue.y].nodeIndex};
        }

        if (completedPurpose ==
            MaterialPickPurpose::RefreshMaterialDrawerSelection)
        {
            if (const auto* material = FindRendererSceneMaterial(scene, m_ui.SelectedMaterial))
            {
                const auto name = RendererSceneText(scene, material->name);
                SettingsSnapshotText nameText;
                SettingsSnapshotError error;
                if (!nameText.Assign({name.count ? name.data : "", name.count}, error))
                {
                    uvsr::log::error("center material name could not be retained: %s", error.Message());
                    return;
                }
                uvsr::log::info(
                    "Center material: %s", nameText.View().data());
            }
        }
        else if (completedForCurrentScene &&
            completedPurpose ==
                MaterialPickPurpose::FocusCameraAtCursor)
        {
            if (m_ui.SelectedNode)
            {
                WindowsPathText path;
                WindowsPathTextResult result;
                if (!GetSceneNodePath(m_ui.SelectedNode, path, result))
                {
                    uvsr::log::error("picked node path could not be formatted: error %u, native %u",
                        unsigned(result.error), result.nativeCode);
                    return;
                }
                uvsr::log::info("Picked node: %s", path.Data());
                PointThirdPersonCameraAt(m_ui.SelectedNode);
            }
            else
            {
                PointThirdPersonCameraAt(
                    RendererSceneHandle{scene.generation, scene.root});
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
    if (!PrepareFrameTargets(execution))
        return;
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
        if (!RenderFrameGeometry(execution) || !RenderRayVisibility(execution) ||
            !RenderDeferredLighting(execution))
            return;
        break;
    }

#if defined(UVSR_BUILD_TESTING)
    if (m_frame->runtimeOutputCaptureRequested)
    {
        bool captureSequenceValid = true;
        if (m_frame->runtimeCaptureSettlingFrames > 0u)
        {
            --m_frame->runtimeCaptureSettlingFrames;
            if (m_frame->runtimeCaptureSettlingFrames == 0u)
            {
                captureSequenceValid = m_frame->runtimeCaptureSequence.Arm(
                    execution.pathTracingSelected ? RuntimeCapturePathDispatchTarget : 0u,
                    !execution.pathTracingSelected && execution.skyVisibilityResult.dispatched,
                    !execution.pathTracingSelected && execution.directionalVisibilityResult.dispatched,
                    !execution.pathTracingSelected && execution.flashlightShadowResult.dispatched);
                if (captureSequenceValid && execution.pathTracingSelected)
                {
                    m_lighting->pathTracingPass->ResetHistory();
                    m_frame->runtimeCaptureDrainBeforeSampling = true;
                }
            }
        }
        else
        {
            captureSequenceValid = execution.pathTracingSelected
                ? m_frame->runtimeCaptureSequence.ObservePath(
                    execution.runtimePathDispatched, execution.runtimePathHistoryReset)
                : m_frame->runtimeCaptureSequence.ObserveRaster(
                    execution.skyVisibilityResult.dispatched, execution.runtimeSkySamplePhase,
                    execution.directionalVisibilityResult.dispatched, execution.runtimeDirectionalSamplePhase,
                    execution.flashlightShadowResult.dispatched, execution.runtimeFlashlightSamplePhase);
        }
        if (!captureSequenceValid)
        {
            uvsr::log::error("Runtime capture changed producer or reset inside its deterministic sequence");
            m_frame->runtimeOutputEvidence = RuntimeOutputEvidence{};
            m_frame->runtimeOutputCaptureRequested = false;
        }
    }
#endif
    if (!RenderMaterialSelection(execution) || !ResolveSceneLighting(execution) ||
        !RenderFramePresentation(execution))
        return;
    if (!execution.Submit())
        return;
    m_scene->lastSubmittedContentRevision = m_scene->canonical.View().contentRevision;
    if (!m_scene->canonical.AdvancePreviousTransforms().Succeeded())
    {
        (void)execution.Fail("previous transform snapshot");
        return;
    }
    CompleteSceneFrame(execution);
}
