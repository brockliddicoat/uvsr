#include "uvsr_internal.h"

auto UvsrSceneViewer::EnsurePathTracingPass() -> void {
        if (m_PathTracingPass)
            return;

        m_PathTracingPass = std::make_unique<PathTracingPass>(
            GetDevice(),
            m_RendererShaderFactory,
            m_BindlessLayout);
        const PathTracingAvailability availability =
            m_PathTracingPass->GetAvailability();
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
        return m_PathTracingPass
            ? m_PathTracingPass->GetCapabilities()
            : unavailable;
    }

auto UvsrSceneViewer::GetPathTracingCenterPixelAcceptedSampleCount() const noexcept -> uint64_t {
        return m_PathTracingPass
            ? m_PathTracingPass->GetCurrentCenterPixelAcceptedSampleCount()
            : 0u;
    }

auto UvsrSceneViewer::GetSelectedLightingTransportState()
        const noexcept -> SelectedLightingTransportState {
        return m_SelectedLightingTransportState;
    }

auto UvsrSceneViewer::GetPathTracingSceneDomainStatus() const -> PathTracingSceneDomainStatus {
        if (!m_Scene)
            return PathTracingSceneDomainStatus::Unsupported;

        const std::shared_ptr<SceneGraph> sceneGraph =
            m_Scene->GetSceneGraph();
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
