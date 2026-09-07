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

auto UvsrSceneViewer::EnsurePathTracingPass() -> void {
        if (m_lighting->pathTracingPass)
            return;

        m_lighting->pathTracingPass = std::make_unique<PathTracingPass>(
            GetDevice(),
            m_frame->rendererShaderFactory,
            m_scene->bindlessLayout);
        const PathTracingAvailability availability =
            m_lighting->pathTracingPass->GetAvailability();
        uvsr::log::info(
            "Path tracing first-use availability: ray queries %s, "
            "executable pipeline %s",
            availability.rayQuerySupported ? "supported" : "unsupported",
            availability.executablePipelineAvailable
                ? "available"
                : "unavailable");
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

auto UvsrSceneViewer::GetSelectedLightingTransportState()
        const noexcept -> SelectedLightingTransportState {
        return m_lighting->selectedLightingTransportState;
    }

auto UvsrSceneViewer::GetPathTracingSceneDomainStatus() const -> PathTracingSceneDomainStatus {
        if (!m_scene->world)
            return PathTracingSceneDomainStatus::Unsupported;

        const std::shared_ptr<SceneGraph> sceneGraph =
            m_scene->world->GetSceneGraph();
        if (!sceneGraph)
            return PathTracingSceneDomainStatus::Unsupported;

        bool blendedGeometryOmitted = false;
        for (const std::shared_ptr<MeshInfo>& mesh : sceneGraph->GetMeshes())
        {
            if (!mesh || mesh->type != MeshType::Triangles ||
                !mesh->buffers || !mesh->buffers->indexBuffer ||
                !mesh->buffers->vertexBuffer ||
                !mesh->buffers->hasAttribute(VertexAttribute::Position))
            {
                return PathTracingSceneDomainStatus::Unsupported;
            }

            for (const std::shared_ptr<MeshGeometry>& geometry :
                mesh->geometries)
            {
                if (!geometry ||
                    geometry->type != MeshGeometryPrimitiveType::Triangles ||
                    geometry->numIndices < 3u ||
                    geometry->numIndices % 3u != 0u ||
                    geometry->numVertices == 0u ||
                    !geometry->material)
                {
                    return PathTracingSceneDomainStatus::Unsupported;
                }

                const Material& material = *geometry->material;
                if (material.transmissionFactor > 0.f ||
                    material.enableSubsurfaceScattering || material.enableHair)
                {
                    return PathTracingSceneDomainStatus::Unsupported;
                }
                if (material.domain == MaterialDomain::AlphaBlended)
                {
                    blendedGeometryOmitted = true;
                    continue;
                }
                if (material.domain != MaterialDomain::Opaque &&
                    material.domain != MaterialDomain::AlphaTested)
                {
                    return PathTracingSceneDomainStatus::Unsupported;
                }
            }
        }

        return blendedGeometryOmitted
            ? PathTracingSceneDomainStatus::BlendedGeometryOmitted
            : PathTracingSceneDomainStatus::Supported;
    }
