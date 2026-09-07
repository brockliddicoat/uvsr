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


using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;

auto UvsrSceneViewer::GetShaderFactory() -> std::shared_ptr<ShaderFactory> {
        return m_frame->shaderFactory;
    }

auto UvsrSceneViewer::GetRendererShaderFactory() -> std::shared_ptr<uvsr::RendererShaderFactory> {
        return m_frame->rendererShaderFactory;
    }

auto UvsrSceneViewer::GetRendererCommonPasses() -> std::shared_ptr<uvsr::RendererCommonPasses> {
        return m_frame->rendererCommonPasses;
    }





auto UvsrSceneViewer::GetSubmittedMainViewTriangles() const -> uint64_t {
        return m_frame->submittedMainViewTriangles;
    }

auto UvsrSceneViewer::GetRendererTimings() const -> const RendererTimings& {
        return m_frame->rendererTimings;
    }

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::DidSubmitFlashlightLightingThisFrame() const -> bool {
        return m_lighting->flashlightLightingSubmittedThisFrame;
    }
#endif

auto UvsrSceneViewer::IsRendererStageActiveThisFrame(
        RendererTimingStage stage) const -> bool {
        switch (stage)
        {
        case RendererTimingStage::PathTransport:
            return m_lighting->pathTransportDispatchedThisFrame;
        case RendererTimingStage::Geometry:
        case RendererTimingStage::DirectLighting:
        case RendererTimingStage::EnvironmentBackground:
            return m_ui.Lighting == LightingSolution::RayMarching;
        case RendererTimingStage::ShadowRayDispatch:
            return m_lighting->directionalRayVisibilityDispatchedThisFrame ||
                m_lighting->rayTracedFlashlightShadowDispatchedThisFrame;
        case RendererTimingStage::SkyVisibilityRayDispatch:
            return m_lighting->rayTracedSkyVisibilityDispatchedThisFrame;
        case RendererTimingStage::AutoExposure:
            return m_frame->autoExposureDispatchedThisFrame;
        default:
            return true;
        }
    }

auto UvsrSceneViewer::AdvanceRendererTimers() -> void {
    const uint32_t slot =
        m_frame->rendererTimerFrame % RendererFrameState::TimerLatency;
    m_frame->rendererTimerFrameWritable = true;
    m_frame->rendererTimerActive.fill(false);

    for (size_t stageIndex = 0u;
        stageIndex < static_cast<size_t>(RendererTimingStage::Count);
        ++stageIndex)
    {
        if (!m_frame->rendererTimerPending[stageIndex][slot])
        {
            m_frame->rendererTimings.available[stageIndex] = false;
            continue;
        }

        nvrhi::ITimerQuery* query =
            m_frame->rendererTimerQueries[stageIndex][slot];
        if (!GetDevice()->pollTimerQuery(query))
        {
            m_frame->rendererTimerFrameWritable = false;
            continue;
        }

        const bool currentEpoch =
            m_frame->rendererTimerPendingEpoch[stageIndex][slot] ==
                m_frame->rendererTimerStageEpoch[stageIndex];
        if (currentEpoch)
        {
            m_frame->rendererTimings.milliseconds[stageIndex] =
                GetDevice()->getTimerQueryTime(query) * 1000.f;
        }
        m_frame->rendererTimings.available[stageIndex] = currentEpoch;
        GetDevice()->resetTimerQuery(query);
        m_frame->rendererTimerPending[stageIndex][slot] = false;
    }
}

auto UvsrSceneViewer::BeginRendererStage(RendererTimingStage stage) -> void {
    if (!m_frame->rendererTimerFrameWritable)
        return;

    const size_t stageIndex = static_cast<size_t>(stage);
    const uint32_t slot =
        m_frame->rendererTimerFrame % RendererFrameState::TimerLatency;
    if (m_frame->rendererTimerPending[stageIndex][slot])
        return;

    m_frame->commandList->beginTimerQuery(
        m_frame->rendererTimerQueries[stageIndex][slot]);
    m_frame->rendererTimerActive[stageIndex] = true;
}

auto UvsrSceneViewer::EndRendererStage(RendererTimingStage stage) -> void {
    const size_t stageIndex = static_cast<size_t>(stage);
    if (!m_frame->rendererTimerActive[stageIndex])
        return;

    const uint32_t slot =
        m_frame->rendererTimerFrame % RendererFrameState::TimerLatency;
    m_frame->commandList->endTimerQuery(
        m_frame->rendererTimerQueries[stageIndex][slot]);
    m_frame->rendererTimerPending[stageIndex][slot] = true;
    m_frame->rendererTimerPendingEpoch[stageIndex][slot] =
        m_frame->rendererTimerStageEpoch[stageIndex];
    m_frame->rendererTimerActive[stageIndex] = false;
}

auto UvsrSceneViewer::CompleteRendererTimerFrame() -> void {
    if (m_frame->rendererTimerFrameWritable)
        ++m_frame->rendererTimerFrame;
}

auto UvsrSceneViewer::InvalidateRendererStageTiming(
    RendererTimingStage stage) -> void {
    const size_t stageIndex = static_cast<size_t>(stage);
    ++m_frame->rendererTimerStageEpoch[stageIndex];
    m_frame->rendererTimings.available[stageIndex] = false;
}

#if defined(UVSR_BUILD_TESTING)
uint64_t UvsrSceneViewer::GetLightingHistoryEpochForRuntimeDiagnostic() const noexcept
{
        return m_lighting->lightingHistoryEpoch;
}

void UvsrSceneViewer::SeedNoiseSamplingPhasesForRuntimeDiagnostic() noexcept
{
        m_lighting->rayTracedFlashlightShadowPhase = 13u;
        m_lighting->rayTracedSkyVisibilityPhase = 17u;
}

std::array<uint64_t, 2> UvsrSceneViewer::GetNoiseSamplingPhasesForRuntimeDiagnostic() const noexcept
{
        return {
            m_lighting->rayTracedFlashlightShadowPhase,
            m_lighting->rayTracedSkyVisibilityPhase
        };
}

void UvsrSceneViewer::ClearShaderReloadRequestForRuntimeDiagnostic() noexcept
{
        m_ui.ShaderReloadRequested = false;
}

bool UvsrSceneViewer::IsShaderReloadRequestedForRuntimeDiagnostic() const noexcept
{
        return m_ui.ShaderReloadRequested;
}
#endif
