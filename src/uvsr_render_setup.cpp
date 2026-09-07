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
#include <donut/engine/CommonRenderPasses.h>
#include <donut/render/GeometryPasses.h>
#include <nvrhi/utils.h>
#include <directx/d3d12.h>
#include <limits>

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;








auto UvsrSceneViewer::SetupView() -> bool {

        const DirectX::XMUINT2 targetSize =
            m_frame->renderTargets->GetSize();
        const float2 renderTargetSize(
            static_cast<float>(targetSize.x),
            static_cast<float>(targetSize.y));

        std::shared_ptr<PlanarView> planarView = std::dynamic_pointer_cast<PlanarView, IView>(m_frame->view);

        float verticalFov = dm::radians(m_scene->cameraVerticalFov);
        // Keep the near plane proportional to scene scale for stable depth.
        const float sceneScaleNear = std::max(0.1f, m_scene->sceneDiagonal * 0.0005f);
        const dm::affine3 viewMatrix = GetActiveCamera().GetWorldToViewMatrix();

        bool topologyChanged = false;

        if (!planarView)
        {
            m_frame->view = planarView = std::make_shared<PlanarView>();
            topologyChanged = true;
        }

        float4x4 projection = perspProjD3DStyleReverse(verticalFov, renderTargetSize.x / renderTargetSize.y, sceneScaleNear);

        planarView->SetViewport(nvrhi::Viewport(
            renderTargetSize.x,
            renderTargetSize.y));
        planarView->SetMatrices(viewMatrix, projection);
        planarView->UpdateCache();

        return topologyChanged;
    }

auto UvsrSceneViewer::CreateFastApproximateAAPass() -> void {
        m_frame->fastApproximateAAPass =
            std::make_unique<FastApproximateAAPass>(
                GetDevice(),
                m_frame->rendererShaderFactory,
                m_frame->rendererCommonPasses,
                m_frame->renderTargets ? m_frame->renderTargets->LdrColor.Get() : nullptr);
        if (!m_frame->fastApproximateAAPass->IsValid())
        {
            uvsr::log::error(
                "Fast Approximate AA initialization failed; "
                "the presentation input will be shown unchanged");
        }
    }



auto UvsrSceneViewer::CreateGeometryPass(RendererGeometryOutput output) -> std::unique_ptr<RendererGeometryPass> {
        RendererGeometryPassDescription description;
        description.output = output;
        description.whiteWorld =
            output == RendererGeometryOutput::Pbr &&
            m_ui.WhiteWorld != WhiteWorldMode::Off;
        auto pass = std::make_unique<RendererGeometryPass>(
            GetDevice(),
            m_frame->rendererShaderFactory,
            m_frame->rendererCommonPasses->BlackTexture(),
            description);
        if (!pass->IsValid())
        {
            throw std::runtime_error(
                output == RendererGeometryOutput::Pbr
                    ? "UVSR G-buffer pass failed to initialize"
                    : "UVSR material-ID pass failed to initialize");
        }
        return pass;
    }

auto UvsrSceneViewer::RenderGeometry(
        RendererGeometryPass& pass,
        nvrhi::IFramebuffer* framebuffer,
        const IView* view,
        const char* marker) -> bool {
        if (!m_frame->commandList || !framebuffer || !view ||
            !m_scene->world || !m_frame->opaqueDrawStrategy)
        {
            return false;
        }

        static_assert(
            static_cast<std::uint8_t>(MaterialDomain::Opaque) ==
                static_cast<std::uint8_t>(
                    RendererMaterialDomain::Opaque) &&
            static_cast<std::uint8_t>(MaterialDomain::AlphaTested) ==
                static_cast<std::uint8_t>(
                    RendererMaterialDomain::AlphaTested) &&
            static_cast<std::uint8_t>(MaterialDomain::AlphaBlended) ==
                static_cast<std::uint8_t>(
                    RendererMaterialDomain::AlphaBlended) &&
            static_cast<std::uint8_t>(MaterialDomain::Transmissive) ==
                static_cast<std::uint8_t>(
                    RendererMaterialDomain::Transmissive) &&
            static_cast<std::uint8_t>(
                MaterialDomain::TransmissiveAlphaTested) ==
                static_cast<std::uint8_t>(
                    RendererMaterialDomain::TransmissiveAlphaTested) &&
            static_cast<std::uint8_t>(
                MaterialDomain::TransmissiveAlphaBlended) ==
                static_cast<std::uint8_t>(
                    RendererMaterialDomain::TransmissiveAlphaBlended) &&
            static_cast<std::uint8_t>(MaterialDomain::Count) ==
                static_cast<std::uint8_t>(
                    RendererMaterialDomain::Count),
            "Renderer material-domain ordinals must match the scene ABI");

        RendererGeometryView geometryView;
        view->FillPlanarViewConstants(geometryView.constants.view);
        geometryView.framebuffer = framebuffer;
        geometryView.viewport = view->GetViewportState();
        geometryView.shadingRate =
            view->GetVariableRateShadingState();
        geometryView.frontCounterClockwise = view->IsMirrored();
        geometryView.reverseDepth = view->IsReverseDepth();

        m_frame->commandList->beginMarker(marker);
        bool succeeded = pass.BeginView(m_frame->commandList, geometryView);
        if (succeeded)
        {
            m_frame->opaqueDrawStrategy->PrepareForView(
                m_scene->world->GetSceneGraph()->GetRootNode(), *view);
            while (const DrawItem* item =
                m_frame->opaqueDrawStrategy->GetNextItem())
            {
                if (!item->instance || !item->mesh || !item->geometry ||
                    !item->material || !item->buffers ||
                    item->instance->GetInstanceIndex() < 0)
                {
                    succeeded = false;
                    break;
                }

                const auto texture = [](const auto& loaded)
                    -> nvrhi::ITexture*
                {
                    return loaded && loaded->texture
                        ? loaded->texture.Get()
                        : nullptr;
                };
                RendererGeometryMaterial material;
                material.cacheKey = item->material;
                material.constants =
                    item->material->materialConstants.Get();
                material.textures = {
                    texture(item->material->baseOrDiffuseTexture),
                    texture(item->material->metalRoughOrSpecularTexture),
                    texture(item->material->normalTexture),
                    texture(item->material->emissiveTexture),
                    texture(item->material->occlusionTexture),
                    texture(item->material->transmissionTexture),
                    texture(item->material->opacityTexture)
                };
                material.domain = static_cast<RendererMaterialDomain>(
                    item->material->domain);

                RendererGeometryBuffers buffers;
                buffers.cacheKey = item->buffers;
                buffers.indexBuffer = item->buffers->indexBuffer.Get();
                buffers.vertexBuffer = item->buffers->vertexBuffer.Get();
                buffers.instanceBuffer =
                    item->buffers->instanceBuffer.Get();
                const auto copyOffset = [](
                    const nvrhi::BufferRange& range,
                    std::uint32_t& destination)
                {
                    if (range.byteOffset >
                        std::numeric_limits<std::uint32_t>::max())
                    {
                        return false;
                    }
                    destination = static_cast<std::uint32_t>(
                        range.byteOffset);
                    return true;
                };
                succeeded =
                    copyOffset(item->buffers->getVertexBufferRange(
                        VertexAttribute::Position),
                        buffers.positionOffset) &&
                    copyOffset(item->buffers->getVertexBufferRange(
                        VertexAttribute::PrevPosition),
                        buffers.previousPositionOffset) &&
                    copyOffset(item->buffers->getVertexBufferRange(
                        VertexAttribute::TexCoord1),
                        buffers.textureCoordinateOffset) &&
                    copyOffset(item->buffers->getVertexBufferRange(
                        VertexAttribute::Normal),
                        buffers.normalOffset) &&
                    copyOffset(item->buffers->getVertexBufferRange(
                        VertexAttribute::Tangent),
                        buffers.tangentOffset);
                const std::uint64_t startIndex =
                    std::uint64_t(item->mesh->indexOffset) +
                    item->geometry->indexOffsetInMesh;
                const std::uint64_t startVertex =
                    std::uint64_t(item->mesh->vertexOffset) +
                    item->geometry->vertexOffsetInMesh;
                if (!succeeded ||
                    startIndex >
                        std::numeric_limits<std::uint32_t>::max() ||
                    startVertex >
                        std::numeric_limits<std::uint32_t>::max())
                {
                    succeeded = false;
                    break;
                }

                RendererGeometryDraw draw;
                draw.material = &material;
                draw.buffers = &buffers;
                draw.cullMode = item->cullMode;
                draw.indexCount = item->geometry->numIndices;
                draw.startIndexLocation =
                    static_cast<std::uint32_t>(startIndex);
                draw.startVertexLocation =
                    static_cast<std::uint32_t>(startVertex);
                draw.startInstanceLocation =
                    static_cast<std::uint32_t>(
                        item->instance->GetInstanceIndex());
                if (!pass.Submit(draw))
                {
                    succeeded = false;
                    break;
                }
            }
        }
        const bool ended = pass.EndView();
        m_frame->commandList->endMarker();
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

auto UvsrSceneViewer::ProcessRenderPassPreparationStep() -> bool {
        switch (m_frame->renderPassPreparationStage)
        {
        case RenderPassPreparationStage::Idle:
        case RenderPassPreparationStage::Complete:
            return true;

        case RenderPassPreparationStage::GBuffer:
            m_frame->gBufferGeometryPass = CreateGeometryPass(RendererGeometryOutput::Pbr);
            break;

        case RenderPassPreparationStage::DeferredLighting:
            if (!m_lighting->lightingAccumulationPass)
                m_lighting->lightingAccumulationPass = std::make_unique<LightingAccumulationPass>(
                    GetDevice(), m_frame->rendererShaderFactory);
            m_lighting->pbrDeferredLightingPass =
                std::make_unique<PbrDeferredLightingPass>(
                    GetDevice(), m_frame->rendererCommonPasses);
            m_lighting->pbrDeferredLightingPass->Init(
                m_frame->rendererShaderFactory, true);
            break;

        case RenderPassPreparationStage::DeferredLightingPipelines:
            if (!m_lighting->pbrDeferredLightingPass)
            {
                throw std::runtime_error(
                    "Deferred lighting pass is unavailable during pipeline "
                    "preparation");
            }
            if (!m_lighting->pbrDeferredLightingPass->PreparePipelinesStep())
            {
                return false;
            }
            if (m_lighting->pbrDeferredLightingPass->
                    DidPipelinePreparationFail() ||
                !m_lighting->pbrDeferredLightingPass->ArePipelinesReady())
            {
                throw std::runtime_error(
                    "Deferred lighting pipeline preparation failed");
            }
            break;

        case RenderPassPreparationStage::FastApproximateAA:
            if (m_ui.UsesFastApproximateAA())
                CreateFastApproximateAAPass();
            break;

        case RenderPassPreparationStage::EnvironmentBackground:
            if (m_frame->renderPassPreparationWaitForIbl &&
                m_lighting->imageBasedLightingEnvironment &&
                !m_lighting->imageBasedLightingEnvironment->
                    IsPreparedRadianceReady())
            {
                return false;
            }
            m_lighting->imageBasedLightingBackgroundPass =
                m_frame->renderTargets->RasterLightingEnabled && m_lighting->imageBasedLightingEnvironment
                    ? std::make_unique<ImageBasedLightingBackgroundPass>(
                        GetDevice(),
                        m_frame->rendererShaderFactory,
                        m_frame->rendererCommonPasses,
                        m_frame->renderTargets->HdrFramebuffer,
                        *m_frame->view,
                        m_lighting->imageBasedLightingEnvironment->
                            GetRadianceTextureResource())
                    : nullptr;
            break;

        case RenderPassPreparationStage::ToneMapping:
            m_frame->autoExposurePass = std::make_unique<AutoExposurePass>(
                GetDevice(),
                m_frame->rendererShaderFactory);
            m_frame->agxToneMappingPass =
                std::make_unique<AgxToneMappingPass>(
                    GetDevice(),
                    m_frame->rendererShaderFactory,
                    m_frame->rendererCommonPasses,
                    m_frame->renderTargets->LdrFramebuffer);
            break;
        }

        m_frame->renderPassPreparationStage = static_cast<RenderPassPreparationStage>(
            static_cast<unsigned int>(m_frame->renderPassPreparationStage) + 1u);
        const bool complete = m_frame->renderPassPreparationStage == RenderPassPreparationStage::Complete;
        if (complete)
            m_frame->renderPassPreparationWaitForIbl = false;
        return complete;
    }

bool UvsrSceneViewer::SetToneMappingLut(uvsr::ToneMappingLut lut, std::string& error)
{
    if (lut == m_ui.ToneMapping.lut)
        return true;
    uvsr::ColorLutResource candidate;
    if (lut != uvsr::ToneMappingLut::None &&
        !uvsr::LoadColorLutResource(GetDevice(),
            GetSceneDir().parent_path().parent_path() / "luts" / "kodak" /
                uvsr::ToneMappingLutFilename(lut), candidate, error))
        return false;
    m_frame->toneMappingLut = std::move(candidate);
    m_ui.ToneMapping.lut = lut;
    return true;
}
