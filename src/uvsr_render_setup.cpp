#include "uvsr_scene_viewer.h"
#include "uvsr_renderer_scene_nvrhi.h"
#include "uvsr_renderer_lighting_nvrhi.h"
#include "uvsr_renderer_frame_nvrhi.h"
#include "uvsr_runtime.h"
#include "uvsr_application.h"
#include "renderer_log.h"
#include "renderer_view_nvrhi.h"
#include <donut/app/DeviceManager.h>
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>
#include <nvrhi/utils.h>
#include <directx/d3d12.h>
#include <limits>
#if defined(UVSR_BUILD_TESTING)
#include "retained_runtime_capture_file.h"
#include <stdint.h>
#include <stdio.h>
#endif

using namespace donut;
using namespace donut::app;
using namespace uvsr;

auto UvsrSceneViewer::SetupView(bool& topologyChanged) -> bool {
        const DirectX::XMUINT2 targetSize = m_frame->renderTargets->GetSize();
        const float width = float(targetSize.x), height = float(targetSize.y);
        const float verticalFov = Radians(m_scene->cameraVerticalFov);
        // keep the near plane proportional to scene scale for stable depth.
        const float nearPlane = std::max(0.1f, m_scene->sceneDiagonal * 0.0005f);
        const auto& worldToView = GetActiveCamera().GetWorldToViewMatrix();
        const auto viewToClip = RendererPerspectiveReverseDepth(verticalFov, width / height, nearPlane);
        topologyChanged = !m_frame->view.valid;
        return BuildRendererView({0.f, width, 0.f, height, 0.f, 1.f}, worldToView, viewToClip, {}, m_frame->view);
    }

bool UvsrSceneViewer::FailRender(const char* message)
{
    uvsr::log::error("%s", message);
    GetDeviceManager()->ReportRenderDisposition(RendererRenderDisposition::Failed);
    return false;
}

auto UvsrSceneViewer::FailPreparation(const char* message) -> PreparationResult
{
    FailRender(message);
    return PreparationResult::Failed;
}

auto UvsrSceneViewer::CreateFastApproximateAAPass() -> bool {
        std::unique_ptr<FastApproximateAAPass> candidate(new (std::nothrow) FastApproximateAAPass(
            GetDevice(), m_frame->rendererShaderFactory.get(),
            m_frame->rendererCommonPasses.get(),
            m_frame->renderTargets ? m_frame->renderTargets->LdrColor.Get() : nullptr));
        if (!candidate || !candidate->IsValid())
            return FailRender("Fast Approximate AA initialization failed");
        m_frame->fastApproximateAAPass = std::move(candidate);
        return true;
    }

auto UvsrSceneViewer::CreateGeometryPass(RendererGeometryOutput output) -> std::unique_ptr<RendererGeometryPass> {
        RendererGeometryPassDescription description;
        description.output = output;
        description.whiteWorld =
            output == RendererGeometryOutput::Pbr &&
            m_ui.WhiteWorld != WhiteWorldMode::Off;
        std::unique_ptr<RendererGeometryPass> candidate(new (std::nothrow) RendererGeometryPass(
            GetDevice(), m_frame->rendererShaderFactory.get(),
            m_frame->rendererCommonPasses->BlackTexture(), description));
        if (!candidate || !candidate->IsValid())
        {
            FailRender(output == RendererGeometryOutput::Pbr
                ? "UVSR G-buffer pass failed to initialize"
                : "UVSR material-ID pass failed to initialize");
            return nullptr;
        }
        return candidate;
    }

auto UvsrSceneViewer::RenderGeometry(
        RendererGeometryPass& pass,
        nvrhi::IFramebuffer* framebuffer,
        const RendererView* view,
        const char* marker) -> bool {
        if (!m_frame->commandList || !framebuffer || !view || !view->valid || !m_scene->canonical.IsPublished())
            return false;
        const auto scene = m_scene->canonical.View();
        if (!m_scene->gpuTables.MaterialsReady(scene))
            return false;
        if (!m_scene->draws.Build(scene, view->frustum).Succeeded())
            return false;

        RendererGeometryView geometryView;
        geometryView.constants.view = view->constants;
        geometryView.framebuffer = framebuffer;
        geometryView.viewport = RendererViewportNvrhi(*view);
        geometryView.frontCounterClockwise = view->mirrored;
        geometryView.reverseDepth = view->reverseDepth;

        m_frame->commandList->beginMarker(marker);
        pass.SetMaterialRevision(scene.materialRevision);
        bool succeeded = pass.BeginView(m_frame->commandList, geometryView);
        const auto draws = m_scene->draws.View();
        for (size_t index = 0; succeeded && index < draws.count; ++index)
        {
            const auto& item = draws.data[index];
            nvrhi::IBuffer* indexBuffer = nullptr;
            nvrhi::IBuffer* vertexBuffer = nullptr;
            if (!m_scene->gpuTables.GetBuffers(item.buffers, indexBuffer, vertexBuffer) ||
                !m_scene->gpuTables.InstancesReady(scene))
            {
                succeeded = false;
                break;
            }
            const auto& values = scene.materials.data[item.material].values;
            RendererGeometryMaterial material;
            material.cacheKey = &scene.materials.data[item.material];
            if (!m_scene->gpuTables.GetMaterialBinding(item.material, material.constants, material.constantRange))
            {
                succeeded = false;
                break;
            }
            material.domain = values.domain;
            for (uint32_t slot = 0; slot < RendererGeometryMaterial::TextureCount; ++slot)
            {
                const uint32_t textureIndex = values.textures[slot];
                if (!m_scene->gpuTables.GetTexture(textureIndex, material.textures[slot]))
                {
                    succeeded = false;
                    break;
                }
            }
            if (!succeeded) break;

            RendererGeometryBuffers buffers;
            buffers.cacheKey = &scene.bufferGroups.data[item.buffers];
            buffers.indexBuffer = indexBuffer;
            buffers.vertexBuffer = vertexBuffer;
            buffers.instanceBuffer = m_scene->gpuTables.InstanceBuffer();
            const auto& sourceBuffers = scene.bufferGroups.data[item.buffers];
            const auto copyOffset = [&](RendererSceneVertexAttribute attribute, uint32_t& destination)
            {
                const uint64_t offset = sourceBuffers.attributes[uint32_t(attribute)].offset;
                if (offset > UINT32_MAX) return false;
                destination = uint32_t(offset);
                return true;
            };
            succeeded = copyOffset(RendererSceneVertexAttribute::Position, buffers.positionOffset) &&
                copyOffset(RendererSceneVertexAttribute::TexCoord0, buffers.textureCoordinateOffset) &&
                copyOffset(RendererSceneVertexAttribute::Normal, buffers.normalOffset) &&
                copyOffset(RendererSceneVertexAttribute::Tangent, buffers.tangentOffset);
            if (!succeeded) break;
            const auto& mesh = scene.meshes.data[item.mesh];
            const auto& geometry = scene.geometries.data[item.geometry];
            const uint64_t startIndex = uint64_t(mesh.indexOffset) + geometry.indexOffsetInMesh;
            const uint64_t startVertex = uint64_t(mesh.vertexOffset) + geometry.vertexOffsetInMesh;
            if (startIndex > UINT32_MAX || startVertex > UINT32_MAX)
            {
                succeeded = false;
                break;
            }
            RendererGeometryDraw draw;
            draw.material = &material;
            draw.buffers = &buffers;
            draw.cullMode = values.doubleSided ? nvrhi::RasterCullMode::None : nvrhi::RasterCullMode::Back;
            draw.indexCount = geometry.indexCount;
            draw.startIndexLocation = uint32_t(startIndex);
            draw.startVertexLocation = uint32_t(startVertex);
            draw.startInstanceLocation = item.instance;
            succeeded = pass.Submit(draw);
        }
        const bool ended = pass.EndView();
        m_frame->commandList->endMarker();
#if defined(UVSR_BUILD_TESTING)
        WindowsPathText captureText;
        WindowsPathTextResult captureError;
        if (m_frame->runtimeOutputCaptureRequested &&
            !GetRuntimeCaptureStem(m_frame->runtimeOutputCapturePath, captureText, captureError))
        {
            uvsr::log::error("Runtime capture draw label failed (%u, code %u)",
                unsigned(captureError.error), captureError.nativeCode);
            m_frame->FailRuntimeOutputCapture();
            return false;
        }
        const std::string_view capture(captureText.Data(), captureText.Size());
        if (capture == "case-15-hdr-environment-starry-night-baseline" ||
            capture == "case-15-hdr-environment-starry-night-reference")
            fprintf(stdout, "{\"event\":\"capture-draw-order\",\"capture\":\"%s\",\"pass\":\"%s\","
                "\"valid\":%s,\"materials\":%zu,\"meshes\":%zu,\"geometries\":%zu,\"instances\":%zu,"
                "\"buffers\":%zu,\"draws\":%zu,\"chunks\":%zu,\"maxChunk\":%zu}\n",
                captureText.Data(), marker, succeeded && ended ? "true" : "false", scene.materials.count,
                scene.meshes.count, scene.geometries.count, scene.instances.count, scene.bufferGroups.count,
                draws.count, m_scene->draws.ChunkCount(), m_scene->draws.MaximumChunk());
#endif
        return succeeded && ended;
    }


auto UvsrSceneViewer::BeginRenderPassPreparation(bool waitForIbl) -> void {
        m_frame->fastApproximateAAPass.reset();
        m_frame->renderPassPreparationWaitForIbl = waitForIbl;
        m_frame->materialIdGeometryPass.reset();
        m_frame->pixelReadback.reset();
        const bool raster = m_frame->renderTargets->RasterLightingEnabled;
        if (raster)
            m_lighting->pathTracingPass.reset();
        else
        {
            m_frame->gBufferGeometryPass.reset();
            m_lighting->pbrDeferredLightingPass.reset();
            m_lighting->lightingAccumulationPass.reset();
            m_lighting->directionalRayVisibilityPass.reset();
            m_lighting->rayTracedFlashlightShadowPass.reset();
            m_lighting->rayTracedSkyVisibilityPass.reset();
        }
        m_frame->renderPassPreparationStage = raster
            ? RenderPassPreparationStage::GBuffer : RenderPassPreparationStage::FastApproximateAA;
    }

auto UvsrSceneViewer::ProcessRenderPassPreparationStep() -> PreparationResult {
        switch (m_frame->renderPassPreparationStage)
        {
        case RenderPassPreparationStage::Idle:
        case RenderPassPreparationStage::Complete:
            return PreparationResult::Complete;

        case RenderPassPreparationStage::GBuffer:
        {
            auto candidate = CreateGeometryPass(RendererGeometryOutput::Pbr);
            if (!candidate) return PreparationResult::Failed;
            m_frame->gBufferGeometryPass = std::move(candidate);
            break;
        }

        case RenderPassPreparationStage::DeferredLighting:
        {
            std::unique_ptr<LightingAccumulationPass> accumulation;
            if (!m_lighting->lightingAccumulationPass)
            {
                accumulation.reset(new (std::nothrow) LightingAccumulationPass(
                    GetDevice(), m_frame->rendererShaderFactory.get()));
                if (!accumulation)
                    return FailPreparation("Lighting accumulation allocation failed");
            }
            std::unique_ptr<PbrDeferredLightingPass> candidate(new (std::nothrow) PbrDeferredLightingPass(
                GetDevice(), m_frame->rendererCommonPasses.get()));
            if (!candidate)
                return FailPreparation("Deferred lighting allocation failed");
            candidate->Init(m_frame->rendererShaderFactory.get(), true);
            if (candidate->DidPipelinePreparationFail())
                return FailPreparation("Deferred lighting pass failed to initialize");
            if (accumulation)
                m_lighting->lightingAccumulationPass = std::move(accumulation);
            m_lighting->pbrDeferredLightingPass = std::move(candidate);
            break;
        }

        case RenderPassPreparationStage::DeferredLightingPipelines:
            if (!m_lighting->pbrDeferredLightingPass)
                return FailPreparation("Deferred lighting pass is unavailable during pipeline preparation");
            if (!m_lighting->pbrDeferredLightingPass->PreparePipelinesStep())
                return PreparationResult::Pending;
            if (m_lighting->pbrDeferredLightingPass->DidPipelinePreparationFail() ||
                !m_lighting->pbrDeferredLightingPass->ArePipelinesReady())
                return FailPreparation("Deferred lighting pipeline preparation failed");
            break;

        case RenderPassPreparationStage::FastApproximateAA:
            if (m_ui.UsesFastApproximateAA() && !CreateFastApproximateAAPass())
                return PreparationResult::Failed;
            break;

        case RenderPassPreparationStage::EnvironmentBackground:
        {
            if (m_frame->renderPassPreparationWaitForIbl && m_lighting->imageBasedLightingEnvironment)
            {
                if (m_lighting->imageBasedLightingEnvironment->HasPreparedRadianceFailed())
                    return FailPreparation("Required image-based lighting preparation failed");
                if (!m_lighting->imageBasedLightingEnvironment->IsPreparedRadianceReady())
                    return PreparationResult::Pending;
            }
            std::unique_ptr<ImageBasedLightingBackgroundPass> candidate;
            if (m_frame->renderTargets->RasterLightingEnabled && m_lighting->imageBasedLightingEnvironment)
            {
                candidate.reset(new (std::nothrow) ImageBasedLightingBackgroundPass(
                    GetDevice(), m_frame->rendererShaderFactory.get(),
                    m_frame->rendererCommonPasses.get(), m_frame->renderTargets->HdrFramebuffer,
                    m_frame->view, m_lighting->imageBasedLightingEnvironment->GetRadianceTextureResource()));
                if (!candidate)
                    return FailPreparation("Image-based lighting background allocation failed");
            }
            m_lighting->imageBasedLightingBackgroundPass = std::move(candidate);
            break;
        }

        case RenderPassPreparationStage::ToneMapping:
        {
            std::unique_ptr<AutoExposurePass> exposure(new (std::nothrow) AutoExposurePass(
                GetDevice(), m_frame->rendererShaderFactory.get()));
            if (!exposure)
                return FailPreparation("Auto exposure allocation failed");
            std::unique_ptr<AgxToneMappingPass> toneMapping(new (std::nothrow) AgxToneMappingPass(
                GetDevice(), m_frame->rendererShaderFactory.get(),
                m_frame->rendererCommonPasses.get(), m_frame->renderTargets->LdrFramebuffer));
            if (!toneMapping || !toneMapping->IsValid())
                return FailPreparation("AgX tone mapping failed to initialize");
            m_frame->autoExposurePass = std::move(exposure);
            m_frame->agxToneMappingPass = std::move(toneMapping);
            break;
        }
        }

        m_frame->renderPassPreparationStage = static_cast<RenderPassPreparationStage>(
            static_cast<unsigned int>(m_frame->renderPassPreparationStage) + 1u);
        const bool complete = m_frame->renderPassPreparationStage == RenderPassPreparationStage::Complete;
        if (complete)
            m_frame->renderPassPreparationWaitForIbl = false;
        return complete ? PreparationResult::Complete : PreparationResult::Pending;
    }

bool UvsrSceneViewer::SetToneMappingLut(uvsr::ToneMappingLut lut, uvsr::SettingsSnapshotError& error)
{
    if (lut == m_ui.ToneMapping.lut)
        return true;
    uvsr::ColorLutResource candidate;
    if (lut != uvsr::ToneMappingLut::None &&
        !uvsr::LoadColorLutResource(GetDevice(), m_frame->toneMappingLutDirectory.Data(),
            lut, candidate, error))
        return false;
    m_frame->toneMappingLut = std::move(candidate);
    m_ui.ToneMapping.lut = lut;
    return true;
}
