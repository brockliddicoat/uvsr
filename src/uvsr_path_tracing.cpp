#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene_nvrhi.h"
#include "uvsr_renderer_lighting_nvrhi.h"
#include "uvsr_renderer_frame_nvrhi.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>


using namespace donut;
using namespace donut::app;
using namespace uvsr;

auto UvsrSceneViewer::EnsurePathTracingPass(bool requiredForFrame, bool replaceExisting) -> bool {
        if (m_lighting->pathTracingPass && !replaceExisting)
            return true;

        std::unique_ptr<PathTracingPass> candidate(new (std::nothrow) PathTracingPass(
            GetDevice(),
            m_frame->rendererShaderFactory.get(),
            m_scene->bindlessLayout));
        if (!candidate)
            return FailRender("Path tracing allocation failed");
        const PathTracingAvailability availability = candidate->GetAvailability();
        uvsr::log::info(
            "Path tracing first-use availability: ray queries %s, "
            "executable pipeline %s",
            availability.rayQuerySupported ? "supported" : "unsupported",
            availability.executablePipelineAvailable
                ? "available"
                : "unavailable");
        if (requiredForFrame && availability.rayQuerySupported && !availability.executablePipelineAvailable)
            return FailRender("Required renderer pass failed: path tracing transport");
        m_lighting->pathTracingPass = std::move(candidate);
        return true;
    }

auto UvsrSceneViewer::GetPathTracingCapabilities() const -> const PathTracingCapabilities& {
        static const PathTracingCapabilities unavailable;
        return m_lighting->pathTracingPass
            ? m_lighting->pathTracingPass->GetCapabilities()
            : unavailable;
    }

auto UvsrSceneViewer::GetPathTracingCenterPixelAcceptedSampleCount() const noexcept -> uint64_t {
        return m_lighting->pathTracingPass
            ? m_lighting->pathTracingPass->GetCurrentCenterPixelAcceptedSampleCount()
            : 0u;
    }

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::GetPathTracingHistoryGeneration() const noexcept -> uint64_t {
        return m_lighting->pathTracingPass
            ? m_lighting->pathTracingPass->GetHistoryGeneration()
            : 0u;
    }
#endif

auto UvsrSceneViewer::GetSelectedLightingTransportState()
        const noexcept -> SelectedLightingTransportState {
        return m_lighting->selectedLightingTransportState;
    }

auto UvsrSceneViewer::GetPathTracingSceneDomainStatus() const -> PathTracingSceneDomainStatus {
        const auto view = m_scene->canonical.View();
        const auto status = ClassifyPathTracingSceneDomain(view);
        if (status == PathTracingSceneDomainStatus::Unsupported || m_scene->gpuTables.Generation() != view.generation)
            return PathTracingSceneDomainStatus::Unsupported;
        for (size_t i = 0; i < view.meshes.count; ++i)
        {
            nvrhi::IBuffer* indices = nullptr;
            nvrhi::IBuffer* vertices = nullptr;
            if (!m_scene->gpuTables.GetBuffers(view.meshes.data[i].bufferGroupIndex, indices, vertices) || !indices || !vertices)
                return PathTracingSceneDomainStatus::Unsupported;
        }
        return status;
    }
