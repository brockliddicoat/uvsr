#include "uvsr_internal.h"
#include "renderer_producer_contract.h"

auto UvsrSceneViewer::SynchronizeAntiAliasingSettings() -> void {
        const AntiAliasingSettings& applied =
            m_AppliedAntiAliasingSettings;
        const AntiAliasingSettings& requested =
            m_ui.AntiAliasing;
        if (m_HasAppliedAntiAliasingSettings && applied == requested)
        {
            return;
        }

        const bool requiresTemporalReset =
            m_HasAppliedAntiAliasingSettings &&
            AntiAliasingSettingsRequireTemporalReset(
                m_AppliedAntiAliasingSettings,
                m_ui.AntiAliasing);
        if (requiresTemporalReset)
        {
            ResetAntiAliasingState();
        }
        if (requiresTemporalReset)
        {
            // A new temporal sequence must not inherit a previous view whose
            // jitter phase belongs to the old preset or history layout.
            m_PreviousView.reset();
        }
        else if (!m_HasAppliedAntiAliasingSettings)
        {
            m_AntiAliasingPhase = 0u;
        }

        m_AppliedAntiAliasingSettings = m_ui.AntiAliasing;
        m_HasAppliedAntiAliasingSettings = true;
    }

auto UvsrSceneViewer::SetupView() -> bool {
        SynchronizeAntiAliasingSettings();

        const DirectX::XMUINT2 targetSize =
            m_RenderTargets->GetSize();
        const float2 renderTargetSize(
            static_cast<float>(targetSize.x),
            static_cast<float>(targetSize.y));

        std::shared_ptr<PlanarView> planarView = std::dynamic_pointer_cast<PlanarView, IView>(m_View);

        float verticalFov = dm::radians(m_CameraVerticalFov);
        // Keep the near plane proportional to scene scale for stable depth.
        const float sceneScaleNear = std::max(0.1f, m_SceneDiagonal * 0.0005f);
        const dm::affine3 viewMatrix = GetActiveCamera().GetWorldToViewMatrix();

        bool topologyChanged = false;

        if (!planarView)
        {
            m_View = planarView = std::make_shared<PlanarView>();
            topologyChanged = true;
        }

        float4x4 projection = perspProjD3DStyleReverse(verticalFov, renderTargetSize.x / renderTargetSize.y, sceneScaleNear);

        planarView->SetViewport(nvrhi::Viewport(
            renderTargetSize.x,
            renderTargetSize.y));
        TemporalAaJitterSample jitter{ 0.f, 0.f };
        const ResolvedAntiAliasingSettings antiAliasing =
            m_ui.GetResolvedAntiAliasingSettings();
        if (antiAliasing.temporalEnabled)
        {
            jitter = GetTemporalAaJitter(
                antiAliasing.temporalJitterSequence,
                m_AntiAliasingPhase);
        }
        planarView->SetPixelOffset(float2(jitter.x, jitter.y));

        planarView->SetMatrices(viewMatrix, projection);
        planarView->UpdateCache();

        return topologyChanged;
    }

auto UvsrSceneViewer::CaptureCurrentViewForMotionVectors() -> void {
        const auto currentView = std::dynamic_pointer_cast<PlanarView>(m_View);
        if (!currentView)
        {
            m_PreviousView.reset();
            return;
        }

        if (!m_PreviousView)
            m_PreviousView = std::make_shared<PlanarView>();
        m_PreviousView->SetViewport(currentView->GetViewport());
        m_PreviousView->SetPixelOffset(currentView->GetPixelOffset());
        m_PreviousView->SetMatrices(
            currentView->GetViewMatrix(),
            currentView->GetProjectionMatrix(false));
        m_PreviousView->UpdateCache();
    }

auto UvsrSceneViewer::GetPresentationAaInitializationSource() const -> nvrhi::ITexture* {
        if (!m_RenderTargets)
            return nullptr;
        return m_RenderTargets->LdrColor.Get();
    }

auto UvsrSceneViewer::CreateFastApproximateAAPass() -> void {
        m_FastApproximateAAPass =
            std::make_unique<FastApproximateAAPass>(
                GetDevice(),
                m_RendererShaderFactory,
                m_RendererCommonPasses,
                GetPresentationAaInitializationSource());
        if (!m_FastApproximateAAPass->IsValid())
        {
            uvsr::log::error(
                "Fast Approximate AA initialization failed; "
                "the presentation input will be shown unchanged");
        }
    }

auto UvsrSceneViewer::CreateTemporalAAPass(bool deferPipelineCreation ) -> void {
        m_TemporalAAPass.reset();
        if (!m_ui.UsesLongTermTemporalAA() ||
            m_ui.Lighting == LightingSolution::PathTracing)
            return;

        const bool multisampled =
            m_RenderTargets->GetSampleCount() > 1u;

        m_TemporalAAPass =
            std::make_unique<TemporalAAPass>(
                GetDevice(),
                m_RendererShaderFactory,
                m_RendererCommonPasses,
                multisampled
                    ? m_RenderTargets->DeferredMsaaColor.Get()
                    : m_RenderTargets->HdrColor.Get(),
                multisampled
                    ? m_RenderTargets->VisibilityDepth.Get()
                    : m_RenderTargets->Depth.Get(),
                multisampled
                    ? m_RenderTargets->VisibilityMotionVectors.Get()
                    : m_RenderTargets->MotionVectors.Get(),
                deferPipelineCreation);
    }

auto UvsrSceneViewer::CreateGeometryPass(RendererGeometryOutput output) -> std::unique_ptr<RendererGeometryPass> {
        RendererGeometryPassDescription description;
        description.output = output;
        description.enableMotionVectors =
            output == RendererGeometryOutput::Pbr &&
            m_RenderTargets->MotionVectorsEnabled;
        description.whiteWorld =
            output == RendererGeometryOutput::Pbr &&
            m_ui.WhiteWorld != WhiteWorldMode::Off;
        auto pass = std::make_unique<RendererGeometryPass>(
            GetDevice(),
            m_RendererShaderFactory,
            m_RendererCommonPasses->BlackTexture(),
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
        const IView* previousView,
        const char* marker) -> bool {
        if (!m_CommandList || !framebuffer || !view || !previousView ||
            !m_Scene || !m_OpaqueDrawStrategy)
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
        previousView->FillPlanarViewConstants(
            geometryView.constants.viewPrev);
        geometryView.framebuffer = framebuffer;
        geometryView.viewport = view->GetViewportState();
        geometryView.shadingRate =
            view->GetVariableRateShadingState();
        geometryView.frontCounterClockwise = view->IsMirrored();
        geometryView.reverseDepth = view->IsReverseDepth();

        m_CommandList->beginMarker(marker);
        bool succeeded = pass.BeginView(m_CommandList, geometryView);
        if (succeeded)
        {
            m_OpaqueDrawStrategy->PrepareForView(
                m_Scene->GetSceneGraph()->GetRootNode(), *view);
            while (const DrawItem* item =
                m_OpaqueDrawStrategy->GetNextItem())
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
        m_CommandList->endMarker();
        return succeeded && ended;
    }

auto UvsrSceneViewer::EnsureMsaaVisibilityResolvePass(
        bool deferPipelineCreation ) -> void {
        if (!m_RenderTargets->VisibilityResourcesEnabled)
        {
            m_MsaaVisibilityResolvePass.reset();
            return;
        }
        if (m_MsaaVisibilityResolvePass)
            return;

        // All four sample-count PSOs are static. Materialize them while the
        // visibility renderer is first created instead of on the first Method
        // change to Multisample Adaptive.
        m_MsaaVisibilityResolvePass =
            std::make_unique<MsaaVisibilityResolvePass>(GetDevice());
        m_MsaaVisibilityResolvePass->Init(
            m_RendererShaderFactory, deferPipelineCreation);
        if (!deferPipelineCreation &&
            !m_MsaaVisibilityResolvePass->ArePipelinesReady())
        {
            throw std::runtime_error(
                "MSAA visibility resolve pipelines failed to initialize");
        }
    }

auto UvsrSceneViewer::RefreshAntiAliasingTargetPasses() -> void {
        // An AA method can change sample count and motion-vector topology
        // without changing the renderer, visibility consumers, or window.
        // Keep those expensive independent passes alive and refresh only the
        // objects whose shader or binding topology actually names a replaced
        // RenderTargets resource.
        m_TemporalAAPass.reset();

        m_GBufferGeometryPass = CreateGeometryPass(
            RendererGeometryOutput::Pbr);

        m_PixelReadback =
            std::make_unique<uvsr::RendererPixelReadback>(
            GetDevice(),
            m_RendererShaderFactory,
            m_RenderTargets->MaterialIDs);
        if (!m_PixelReadback->IsValid())
        {
            throw std::runtime_error(
                "UVSR material readback failed to initialize");
        }

        if (m_PbrDeferredLightingPass)
            m_PbrDeferredLightingPass->ResetBindingCache();
        if (m_ScreenSpaceVisibilityPass)
            m_ScreenSpaceVisibilityPass->ResetBindingCache();
        EnsureMsaaVisibilityResolvePass();

        CreateTemporalAAPass();
        if (m_FastApproximateAAPass)
        {
            m_FastApproximateAAPass->UpdateSourceColor(
                GetPresentationAaInitializationSource());
        }
        else if (m_ui.UsesFastApproximateAA())
        {
            CreateFastApproximateAAPass();
        }
        m_ImageBasedLightingBackgroundPass =
            m_ImageBasedLightingEnvironment
                ? std::make_unique<ImageBasedLightingBackgroundPass>(
                    GetDevice(),
                    m_RendererShaderFactory,
                    m_RendererCommonPasses,
                    m_RenderTargets->HdrFramebuffer,
                    *m_View,
                    m_ImageBasedLightingEnvironment->
                        GetRadianceTextureResource())
                : nullptr;
        m_AgxToneMappingPass =
            std::make_unique<AgxToneMappingPass>(
                GetDevice(),
                m_RendererShaderFactory,
                m_RendererCommonPasses,
                m_RenderTargets->LdrFramebuffer);
        m_AutoExposurePass = std::make_unique<AutoExposurePass>(
            GetDevice(),
            m_RendererShaderFactory);
    }

auto UvsrSceneViewer::BeginRenderPassPreparation(bool waitForIbl) -> void {
        m_TemporalAAPass.reset();
        m_FastApproximateAAPass.reset();
        m_RenderPassPreparationWaitForIbl = waitForIbl;
        m_RenderPassPreparationStage =
            RenderPassPreparationStage::GBuffer;
    }

auto UvsrSceneViewer::ProcessRenderPassPreparationStep() -> bool {
        switch (m_RenderPassPreparationStage)
        {
        case RenderPassPreparationStage::Idle:
        case RenderPassPreparationStage::Complete:
            return true;

        case RenderPassPreparationStage::GBuffer:
        {
            m_GBufferGeometryPass = CreateGeometryPass(
                RendererGeometryOutput::Pbr);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::MaterialId;
            return false;
        }

        case RenderPassPreparationStage::MaterialId:
        {
            m_MaterialIdGeometryPass = CreateGeometryPass(
                RendererGeometryOutput::MaterialId);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::ReadbackAndFlashlight;
            return false;
        }

        case RenderPassPreparationStage::ReadbackAndFlashlight:
            m_PixelReadback =
                std::make_unique<uvsr::RendererPixelReadback>(
                GetDevice(),
                m_RendererShaderFactory,
                m_RenderTargets->MaterialIDs);
            if (!m_PixelReadback->IsValid())
            {
                throw std::runtime_error(
                    "UVSR material readback failed to initialize");
            }
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::DeferredLighting;
            return false;

        case RenderPassPreparationStage::DeferredLighting:
            m_PbrDeferredLightingPass =
                std::make_unique<PbrDeferredLightingPass>(
                    GetDevice(), m_RendererCommonPasses);
            m_PbrDeferredLightingPass->Init(
                m_RendererShaderFactory, true);
            EnsureMsaaVisibilityResolvePass(true);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::DeferredLightingPipelines;
            return false;

        case RenderPassPreparationStage::DeferredLightingPipelines:
            if (!m_PbrDeferredLightingPass)
            {
                throw std::runtime_error(
                    "Deferred lighting pass is unavailable during pipeline "
                    "preparation");
            }
            if (!m_PbrDeferredLightingPass->PreparePipelinesStep())
            {
                return false;
            }
            if (m_PbrDeferredLightingPass->
                    DidPipelinePreparationFail() ||
                !m_PbrDeferredLightingPass->ArePipelinesReady())
            {
                throw std::runtime_error(
                    "Deferred lighting pipeline preparation failed");
            }
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::MsaaVisibilityResolvePipelines;
            return false;

        case RenderPassPreparationStage::MsaaVisibilityResolvePipelines:
            if (m_MsaaVisibilityResolvePass &&
                !m_MsaaVisibilityResolvePass->PreparePipelinesStep())
            {
                return false;
            }
            if (m_MsaaVisibilityResolvePass &&
                !m_MsaaVisibilityResolvePass->ArePipelinesReady())
            {
                throw std::runtime_error(
                    "MSAA visibility resolve pipeline preparation failed");
            }
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::Visibility;
            return false;

        case RenderPassPreparationStage::Visibility:
            m_ScreenSpaceVisibilityPass =
                std::make_unique<ScreenSpaceVisibilityPass>(
                    GetDevice(),
                    m_RendererShaderFactory,
                    m_RendererCommonPasses,
                    true);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::VisibilityPipelines;
            return false;

        case RenderPassPreparationStage::VisibilityPipelines:
            if (m_ScreenSpaceVisibilityPass &&
                !m_ScreenSpaceVisibilityPass->PreparePipelinesStep())
            {
                if (m_ScreenSpaceVisibilityPass->
                        HasPipelineCreationFailed())
                {
                    throw std::runtime_error(
                        "Screen-space visibility pipeline preparation "
                        "failed");
                }
                return false;
            }
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::Denoising;
            return false;

        case RenderPassPreparationStage::Denoising:
            if (!m_DenoisingPass)
            {
                m_DenoisingPass = std::make_unique<DenoisingPass>(
                    GetDevice(), m_RendererShaderFactory);
            }
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::TemporalAA;
            return false;

        case RenderPassPreparationStage::TemporalAA:
            CreateTemporalAAPass(true);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::TemporalAAPipelines;
            return false;

        case RenderPassPreparationStage::TemporalAAPipelines:
            if (m_TemporalAAPass &&
                !m_TemporalAAPass->PreparePipelinesStep())
            {
                return false;
            }
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::FastApproximateAA;
            return false;

        case RenderPassPreparationStage::FastApproximateAA:
            if (m_ui.UsesFastApproximateAA())
                CreateFastApproximateAAPass();
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::EnvironmentBackground;
            return false;

        case RenderPassPreparationStage::EnvironmentBackground:
            if (m_RenderPassPreparationWaitForIbl &&
                m_ImageBasedLightingEnvironment &&
                !m_ImageBasedLightingEnvironment->
                    IsPreparedRadianceReady())
            {
                return false;
            }
            m_ImageBasedLightingBackgroundPass =
                m_ImageBasedLightingEnvironment
                    ? std::make_unique<ImageBasedLightingBackgroundPass>(
                        GetDevice(),
                        m_RendererShaderFactory,
                        m_RendererCommonPasses,
                        m_RenderTargets->HdrFramebuffer,
                        *m_View,
                        m_ImageBasedLightingEnvironment->
                            GetRadianceTextureResource())
                    : nullptr;
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::ToneMapping;
            return false;

        case RenderPassPreparationStage::ToneMapping:
            m_AutoExposurePass = std::make_unique<AutoExposurePass>(
                GetDevice(),
                m_RendererShaderFactory);
            m_AgxToneMappingPass =
                std::make_unique<AgxToneMappingPass>(
                    GetDevice(),
                    m_RendererShaderFactory,
                    m_RendererCommonPasses,
                    m_RenderTargets->LdrFramebuffer);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::Complete;
            m_RenderPassPreparationWaitForIbl = false;
            return true;
        }

        return false;
    }

auto UvsrSceneViewer::CreateRenderPasses() -> void {
        BeginRenderPassPreparation(false);
        while (!ProcessRenderPassPreparationStep())
        {
        }
    }

auto UvsrSceneViewer::RenderScene(nvrhi::IFramebuffer* framebuffer) -> void {
        if (m_SceneGpuUploadPending)
        {
            RenderSceneGpuUploadFrame(framebuffer);
            return;
        }

        const bool pathTracingSelected =
            m_ui.Lighting == LightingSolution::PathTracing;
        if (pathTracingSelected)
        {
            EnsurePathTracingPass();
            if (m_PathTracingPass)
                m_PathTracingPass->PollAcceptedSampleReadback();
        }
        const PathTracingSceneDomainStatus pathTracingSceneDomainStatus =
            pathTracingSelected
                ? GetPathTracingSceneDomainStatus()
                : PathTracingSceneDomainStatus::Supported;
        const bool pathTracingSceneDomainSupported =
            pathTracingSceneDomainStatus !=
                PathTracingSceneDomainStatus::Unsupported;
        if (!pathTracingSelected)
        {
            EnsureDirectionalRayVisibilityPass();
            EnsureRayTracedFlashlightShadowPass();
            EnsureRayTracedSkyVisibilityPass();
        }

        int windowWidth, windowHeight;
        GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
        const DirectX::XMUINT2 presentationSize{
            uint32_t(windowWidth),
            uint32_t(windowHeight)
        };
        const MsaaRasterTopology msaaTopology = ResolveSupportedMsaaTopology(
            GetDevice(),
            m_ui.GetResolvedAntiAliasingSettings().rasterSampleCount);
        const MsaaRenderExtent renderExtent = ScaleMsaaRenderExtent(
            presentationSize.x,
            presentationSize.y,
            msaaTopology.linearResolutionScale);
        if (!msaaTopology || !renderExtent)
        {
            throw std::runtime_error(
                "UVSR could not create the selected exact MSAA topology");
        }
        const DirectX::XMUINT2 renderSize(
            renderExtent.width,
            renderExtent.height);
        const uint sampleCount = msaaTopology.rasterSampleCount;

        UpdateFlashlightTransform();
        bool lightingSceneContentChanged = false;
        if (const std::shared_ptr<SceneGraph> sceneGraph =
                m_Scene->GetSceneGraph())
        {
            const std::shared_ptr<SceneGraphNode>& root =
                sceneGraph->GetRootNode();
            const bool pendingContentChanges = root &&
                (root->GetDirtyFlags() &
                    SceneGraphNode::DirtyFlags::SubgraphContentUpdate) != 0u;
            lightingSceneContentChanged =
                sceneGraph->HasPendingStructureChanges() ||
                sceneGraph->HasPendingTransformChanges() ||
                pendingContentChanges;
            for (const std::shared_ptr<Material>& material :
                sceneGraph->GetMaterials())
            {
                lightingSceneContentChanged =
                    lightingSceneContentChanged ||
                    (material && material->dirty);
            }
        }
        m_Scene->RefreshSceneGraph(GetFrameIndex());
        const auto& sceneLights =
            m_SceneLightsWithoutFlashlight;
        std::vector<std::shared_ptr<Light>> lightingLights;
        const std::vector<std::shared_ptr<Light>>* submittedLights =
            &sceneLights;
        if (ShouldSubmitFlashlight(m_FlashlightTransition) &&
            m_Flashlight)
        {
            // Local-light limits are deterministic: the user-controlled
            // flashlight is never silently dropped by a light-heavy scene.
            lightingLights.reserve(sceneLights.size() + 1u);
            lightingLights.push_back(m_Flashlight);
            for (const auto& light : sceneLights)
            {
                if (light)
                    lightingLights.push_back(light);
            }
            submittedLights = &lightingLights;
        }

        {
            const bool screenSpaceVisibilityResourcesRequired =
                m_ui.HasActiveScreenSpaceVisibilityConsumer();
            // Ray traced visibility producers consume one coherent closest
            // surface. Keep the resolve targets allocated for every deferred
            // PBR MSAA topology so toggling a producer does not force an
            // unrelated render pass rebuild.
            const bool msaaClosestSurfaceResolveResourcesRequired =
                sampleCount > 1u;
            const bool visibilityResourcesRequired =
                screenSpaceVisibilityResourcesRequired ||
                msaaClosestSurfaceResolveResourcesRequired;
            const bool visibilitySourceRadianceRequired =
                screenSpaceVisibilityResourcesRequired &&
                m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
                (!submittedLights->empty() ||
                    IsAmbientFillLobeActive(
                        m_ui.EnableAmbientFill,
                        m_ui.EnableDiffuseIbl,
                        m_ui.DiffuseIblStrength));
            const bool temporalAARequired =
                m_ui.UsesLongTermTemporalAA();
            const bool rasterTemporalAARequired = temporalAARequired &&
                !pathTracingSelected;
            const bool denoisingMotionVectorsRequired =
                !pathTracingSelected &&
                (IsThirdPartyDenoisingMethod(
                        m_ui.Denoising.ambientOcclusion.method) ||
                    IsThirdPartyDenoisingMethod(
                        m_ui.Denoising.diffuseGi.method) ||
                    IsThirdPartyDenoisingMethod(
                        m_ui.Denoising.shadows.method) ||
                    IsThirdPartyDenoisingMethod(
                        m_ui.Denoising.skyVisibility.method));
            const bool fastApproximateAARequired =
                m_ui.UsesFastApproximateAA();
            const bool motionVectorsRequired =
                rasterTemporalAARequired ||
                denoisingMotionVectorsRequired ||
                (visibilityResourcesRequired && sampleCount > 1u);

            bool needNewPasses = false;
            bool refreshAntiAliasingTargetPasses = false;
            bool antiAliasingSampleCountChanged = false;

            if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(
                renderSize, sampleCount,
                presentationSize,
                msaaTopology.presentationSampleCount,
                visibilityResourcesRequired,
                visibilitySourceRadianceRequired,
                motionVectorsRequired))
            {
                const bool hadRenderTargets = bool(m_RenderTargets);
                const DirectX::XMUINT2 previousSize = hadRenderTargets
                    ? m_RenderTargets->GetSize()
                    : DirectX::XMUINT2{};
                const bool sameNonAaTopology = hadRenderTargets &&
                    previousSize.x == renderSize.x &&
                    previousSize.y == renderSize.y &&
                    m_RenderTargets->VisibilityResourcesEnabled ==
                        visibilityResourcesRequired &&
                    m_RenderTargets->VisibilitySourceRadianceEnabled ==
                        (visibilityResourcesRequired &&
                            visibilitySourceRadianceRequired);
                antiAliasingSampleCountChanged = hadRenderTargets &&
                    m_RenderTargets->GetSampleCount() != sampleCount;
                const bool antiAliasingTopologyChanged =
                    antiAliasingSampleCountChanged ||
                    (hadRenderTargets &&
                        m_RenderTargets->MotionVectorsEnabled !=
                            motionVectorsRequired);

                if (m_DirectionalRayVisibilityPass)
                    m_DirectionalRayVisibilityPass->ResetBindingCache();
                if (m_RayTracedFlashlightShadowPass)
                    m_RayTracedFlashlightShadowPass->ResetBindingCache();
                if (m_RayTracedSkyVisibilityPass)
                    m_RayTracedSkyVisibilityPass->ResetBindingCache();
                m_RenderTargets = nullptr;
                m_BindingCache.Clear();
                m_RenderTargets = std::make_unique<RenderTargets>();
                if (!m_RenderTargets->Init(
                        GetDevice(), renderSize, sampleCount,
                        presentationSize,
                        msaaTopology.presentationSampleCount,
                        motionVectorsRequired, true,
                        visibilityResourcesRequired,
                        visibilitySourceRadianceRequired))
                {
                    throw std::runtime_error(
                        "UVSR render targets failed to initialize");
                }
                m_PreviousView.reset();

                refreshAntiAliasingTargetPasses =
                    sameNonAaTopology && antiAliasingTopologyChanged;
                needNewPasses = !refreshAntiAliasingTargetPasses;
            }

            const bool refreshTemporalPass =
                rasterTemporalAARequired != bool(m_TemporalAAPass);
            if (SetupView())
            {
                needNewPasses = true;
                m_PreviousView.reset();
            }

            if (m_ui.ShaderReloadRequested)
            {
                const bool recreatePathTracingPass =
                    pathTracingSelected && bool(m_PathTracingPass);
                m_DirectionalRayVisibilityPass.reset();
                m_RayTracedFlashlightShadowPass.reset();
                m_RayTracedSkyVisibilityPass.reset();
                m_DenoisingPass.reset();
                // This pass owns shader handles and PSOs independently of the
                // main pass set. Drop it before clearing the factory cache so
                // an explicit reload cannot retain the previous MSAA resolve.
                m_MsaaVisibilityResolvePass.reset();
                m_PathTracingPass.reset();
                m_LightingAccumulationPass.reset();
                m_ShaderFactory->ClearCache();
                m_RendererShaderFactory->ClearCache();
                if (recreatePathTracingPass)
                    EnsurePathTracingPass();
                m_LightingAccumulationPass =
                    std::make_unique<LightingAccumulationPass>(
                        GetDevice(),
                        m_RendererShaderFactory);
                InvalidateLightingAccumulationHistory();
                // Light-probe preprocessing owns shader handles too. Recreate
                // it only for an explicit shader reload, not for resize or TAA
                // pass recreation, so static IBL remains zero-work otherwise.
                m_ImageBasedLightingEnvironment =
                    std::make_unique<ImageBasedLightingEnvironment>(
                        GetDevice(),
                        m_RendererShaderFactory,
                        m_RendererCommonPasses,
                        GetExecutableDirectoryWide().parent_path() /
                            "media/environments");
                needNewPasses = true;
            }

            if(needNewPasses)
            {
                CreateRenderPasses();
            }
            else if (refreshAntiAliasingTargetPasses)
            {
                RefreshAntiAliasingTargetPasses();
            }
            else if (refreshTemporalPass)
            {
                // A method transition does not invalidate scene, G-buffer,
                // lighting, visibility, sky, or output passes while the
                // render-target topology stays unchanged.
                CreateTemporalAAPass();
            }

            // Fast Approximate is a presentation-only spatial filter. Its
            // resources are independent of temporal history and can follow
            // any raster or temporal AA combination without rebuilding them.
            if (fastApproximateAARequired && !m_FastApproximateAAPass)
                CreateFastApproximateAAPass();
            else if (!fastApproximateAARequired && m_FastApproximateAAPass)
                m_FastApproximateAAPass.reset();

            m_ui.ShaderReloadRequested = false;
        }

        if (!pathTracingSelected)
        {
            EnsureDirectionalRayVisibilityPass();
            EnsureRayTracedFlashlightShadowPass();
            EnsureRayTracedSkyVisibilityPass();
        }

        m_CommandList->open();
        AdvanceRendererTimers();
        m_PathTransportDispatchedThisFrame = false;
#if defined(UVSR_BUILD_TESTING)
        m_ScreenSpaceVisibilityDispatchedThisFrame = false;
        m_RuntimeLinearReadbackQueued = false;
#endif
        m_DirectionalRayVisibilityDispatchedThisFrame = false;
        m_RayTracedFlashlightShadowDispatchedThisFrame = false;
#if defined(UVSR_BUILD_TESTING)
        m_FlashlightLightingSubmittedThisFrame = false;
#endif
        m_ShadowDenoisingDispatchedThisFrame = false;
        m_RayTracedSkyVisibilityDispatchedThisFrame = false;
        m_RayTracedSkyVisibilityDenoisedThisFrame = false;
        m_AmbientOcclusionDenoisedThisFrame = false;
        m_DiffuseIlluminationDenoisedThisFrame = false;
        m_AutoExposureDispatchedThisFrame = false;
        m_VisibilityLightingPreparationDispatchedThisFrame = false;
#if defined(UVSR_BUILD_TESTING)
        m_LightingAccumulationCommittedThisFrame = false;
#endif
        BeginRendererStage(RendererTimingStage::CompleteFrame);
        BeginRendererStage(RendererTimingStage::SceneSetup);
        m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());
        const bool directionalRayVisibilitySelected =
            !pathTracingSelected &&
            m_ui.Representation.allowRayTraversal &&
            m_ui.DirectionalShadows.enabled &&
            SupportsDirectionalRayVisibility();
        const bool rayTracedFlashlightShadowSelected =
            !pathTracingSelected &&
            m_ui.Representation.allowRayTraversal &&
            m_ui.Flashlight.castShadows &&
            ShouldSubmitFlashlight(m_FlashlightTransition) &&
            HasRayTracedFlashlightShadowHardwareSupport();
        const bool rayTracedSkyVisibilitySelected =
            !pathTracingSelected &&
            m_ui.Representation.allowRayTraversal &&
            m_ui.RayTracedSkyVisibility.enabled &&
            (HasRayTracedSkyVisibilityConsumer(
                    m_ui.RayTracedSkyVisibility) ||
                m_ui.LightingDebugView ==
                    PbrLightingDebugView::SkyVisibility) &&
            SupportsRayTracedSkyVisibility();
        const bool worldRepresentationSelected =
            directionalRayVisibilitySelected ||
            rayTracedFlashlightShadowSelected ||
            rayTracedSkyVisibilitySelected ||
            (pathTracingSelected &&
                pathTracingSceneDomainSupported &&
                m_ui.Representation.allowRayTraversal &&
                m_BindlessLayout &&
                PathTracingPass::QueryCapabilities(GetDevice())
                    .rayQuerySupported);
        if (pathTracingSelected &&
            (!pathTracingSceneDomainSupported ||
                !m_ui.Representation.allowRayTraversal ||
                !m_PathTracingPass ||
                !m_PathTracingPass->IsSupported()))
        {
            FailOpenRendererFrame("path tracing transport");
            return;
        }
        if (directionalRayVisibilitySelected &&
            (!m_DirectionalRayVisibilityPass ||
                !m_DirectionalRayVisibilityPass->IsSupported()))
        {
            FailOpenRendererFrame("directional ray visibility");
            return;
        }
        if (rayTracedFlashlightShadowSelected &&
            (!m_RayTracedFlashlightShadowPass ||
                !m_RayTracedFlashlightShadowPass->IsSupported()))
        {
            FailOpenRendererFrame("ray-traced flashlight visibility");
            return;
        }
        if (rayTracedSkyVisibilitySelected &&
            (!m_RayTracedSkyVisibilityPass ||
                !m_RayTracedSkyVisibilityPass->IsSupported()))
        {
            FailOpenRendererFrame("ray-traced sky visibility");
            return;
        }
        const uint64_t worldRepresentationGenerationBefore =
            m_WorldSpaceRepresentation
            ? m_WorldSpaceRepresentation->GetStatus().generation
            : 0u;
        const bool worldRepresentationReady =
            m_WorldSpaceRepresentation &&
            m_WorldSpaceRepresentation->Update(
                m_CommandList,
                m_Scene.get(),
                m_ui.Representation,
                uint32_t(GetFrameIndex()),
                worldRepresentationSelected);
        if (worldRepresentationSelected)
        {
            if (!m_WorldSpaceRepresentation)
            {
                FailOpenRendererFrame("world-space representation");
                return;
            }
            const WorldSpaceRepresentationState state =
                m_WorldSpaceRepresentation->GetStatus().state;
            if (state == WorldSpaceRepresentationState::Failed ||
                state == WorldSpaceRepresentationState::Unsupported)
            {
                FailOpenRendererFrame("world-space representation");
                return;
            }
            if (!worldRepresentationReady)
            {
                if (state == WorldSpaceRepresentationState::Ready)
                {
                    FailOpenRendererFrame(
                        "world-space representation dispatch");
                    return;
                }
                SubmitPendingRendererPreparationFrame();
                return;
            }
        }
        RayTracedMaterialVisibilityInputs rayMaterialVisibility;
        if (worldRepresentationReady && m_Scene)
        {
            rayMaterialVisibility.geometryBuffer =
                m_Scene->GetGeometryBuffer();
            rayMaterialVisibility.materialBuffer =
                m_Scene->GetMaterialBuffer();
            rayMaterialVisibility.geometryIndexMap =
                m_WorldSpaceRepresentation->GetGeometryIndexMap();
            rayMaterialVisibility.instanceBuffer =
                m_Scene->GetInstanceBuffer();
            rayMaterialVisibility.descriptorTable =
                m_Scene->GetDescriptorTable();
        }
        if (m_WorldSpaceRepresentation &&
            (m_WorldSpaceRepresentation->GetStatus().generation !=
                    worldRepresentationGenerationBefore ||
                (worldRepresentationSelected &&
                    !worldRepresentationReady)))
        {
            if (m_DirectionalRayVisibilityPass)
                m_DirectionalRayVisibilityPass->ResetBindingCache();
            if (m_RayTracedFlashlightShadowPass)
                m_RayTracedFlashlightShadowPass->ResetBindingCache();
            if (m_RayTracedSkyVisibilityPass)
                m_RayTracedSkyVisibilityPass->ResetBindingCache();
            ResetImageBasedLightingHistory();
        }

        const ResolvedAntiAliasingSettings antiAliasing =
            m_ui.GetResolvedAntiAliasingSettings();
        const bool rayMarchingAccumulationOwnsTemporalHistory =
            m_ui.Lighting == LightingSolution::RayMarching &&
            m_ui.AccumulateSamples;
        const bool temporalSharpenEnabled =
            antiAliasing.sharpeningAllowed &&
            ShouldSharpenTemporalAa(
                m_ui.TemporalAaSharpenEnabled,
                m_ui.TemporalAaSharpness);
        const bool deferTemporalSharpenToPresentation =
            temporalSharpenEnabled &&
            (antiAliasing.fastApproximateEnabled ||
                antiAliasing.historyStorage ==
                    TemporalAaHistoryStorage::Compact);
        const bool temporalAaSelected =
            !pathTracingSelected &&
            !rayMarchingAccumulationOwnsTemporalHistory &&
            m_ui.UsesLongTermTemporalAA();
        bool temporalAaWillRender =
            temporalAaSelected &&
            m_TemporalAAPass &&
            m_TemporalAAPass->PrepareForRender(
                antiAliasing,
                temporalSharpenEnabled &&
                    !deferTemporalSharpenToPresentation,
                deferTemporalSharpenToPresentation,
                m_ui.TemporalAaSharpness);
        if (temporalAaSelected && !temporalAaWillRender)
        {
            FailOpenRendererFrame("temporal anti-aliasing preparation");
            return;
        }
        bool temporalAaRenderedThisFrame = false;
        const bool directionalRayVisibilityExpectedToContribute =
            directionalRayVisibilitySelected &&
            worldRepresentationReady &&
            m_SunLight;

        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));

        const bool diffuseIblSelected =
            !pathTracingSelected &&
            IsAmbientFillLobeActive(
                m_ui.EnableAmbientFill,
                m_ui.EnableDiffuseIbl,
                m_ui.DiffuseIblStrength);
        const bool specularIblSelected =
            !pathTracingSelected &&
            IsAmbientFillLobeActive(
                m_ui.EnableAmbientFill,
                m_ui.EnableSpecularIbl,
                m_ui.SpecularIblStrength);
        const bool environmentBackgroundSelected =
            !pathTracingSelected &&
            m_ui.LightingDebugView == PbrLightingDebugView::None &&
            !m_ui.HasActiveScreenSpaceVisibilityDebugConsumer() &&
            m_ui.ShowEnvironmentBackground;
        const bool imageBasedLightingSelected =
            diffuseIblSelected ||
            specularIblSelected ||
            environmentBackgroundSelected ||
            pathTracingSelected;
        if (imageBasedLightingSelected &&
            !m_ImageBasedLightingEnvironment)
        {
            FailOpenRendererFrame("image-based lighting ownership");
            return;
        }
        UpdateImageBasedLighting(m_CommandList);
        if (m_ImageBasedLightingEnvironment)
        {
            const ImageBasedLightingPreparationStatus environmentStatus =
                m_ImageBasedLightingEnvironment->GetPreparedRadianceStatus();
            if (environmentStatus ==
                ImageBasedLightingPreparationStatus::Failed)
            {
                FailOpenRendererFrame("image-based lighting preparation");
                return;
            }
            if (environmentStatus ==
                    ImageBasedLightingPreparationStatus::Preparing ||
                environmentStatus == ImageBasedLightingPreparationStatus::Idle)
            {
                SubmitPendingRendererPreparationFrame();
                return;
            }
        }
        const ImageBasedLightingProbe* globalEnvironment =
            m_ImageBasedLightingEnvironment
                ? m_ImageBasedLightingEnvironment->GetLightProbe()
                : nullptr;
        if ((diffuseIblSelected &&
                (!globalEnvironment ||
                    !globalEnvironment->diffuseMap ||
                    !(globalEnvironment->diffuseScale > 0.f))) ||
            (specularIblSelected &&
                (!globalEnvironment ||
                    !globalEnvironment->specularMap ||
                    !globalEnvironment->environmentBrdf ||
                    !(globalEnvironment->specularScale > 0.f))) ||
            (environmentBackgroundSelected &&
                (!m_ImageBasedLightingEnvironment ||
                    !m_ImageBasedLightingEnvironment
                        ->GetRadianceTexture())) ||
            (pathTracingSelected &&
                (!m_ImageBasedLightingEnvironment ||
                    !m_ImageBasedLightingEnvironment
                        ->GetRadianceTexture())))
        {
            FailOpenRendererFrame("image-based lighting resources");
            return;
        }
        nvrhi::ITexture* diffuseEnvironment =
            globalEnvironment
                ? globalEnvironment->diffuseMap.Get()
                : nullptr;
        const float diffuseEnvironmentScale =
            globalEnvironment
                ? globalEnvironment->diffuseScale
                : 0.f;
        const bool skyVisibilityDiffuseIblAvailable =
            m_ui.RayTracedSkyVisibility.applyToDiffuseIbl &&
            diffuseEnvironment &&
            diffuseEnvironmentScale > 0.f;
        const bool skyVisibilitySpecularIblAvailable =
            m_ui.RayTracedSkyVisibility.applyToSpecularIbl &&
            globalEnvironment &&
            globalEnvironment->specularMap &&
            globalEnvironment->environmentBrdf &&
            globalEnvironment->specularScale > 0.f;
        const bool rayTracedSkyVisibilityExpectedToContribute =
            rayTracedSkyVisibilitySelected &&
            worldRepresentationReady &&
            (m_ui.LightingDebugView ==
                    PbrLightingDebugView::SkyVisibility ||
                skyVisibilityDiffuseIblAvailable ||
                skyVisibilitySpecularIblAvailable);
        const bool screenSpaceVisibilityRequested =
            m_ui.HasActiveScreenSpaceVisibilityConsumer();
        const NoiseSettings visibilityNoiseSettings = ResolveNoiseSettings(
            m_ui.Noise,
            m_ui.ScreenSpaceVisibility.noise);
        const NoiseSettings skyNoiseSettings = ResolveNoiseSettings(
            m_ui.Noise,
            m_ui.RayTracedSkyVisibility.noise);
        const NoiseSettings flashlightNoiseSettings = m_ui.Noise;
        const bool rayTracedFlashlightShadowExpectedToContribute =
            rayTracedFlashlightShadowSelected &&
            worldRepresentationReady;
        NoiseTextureBinding visibilityNoise;
        NoiseTextureBinding skyNoise;
        NoiseTextureBinding flashlightNoise;
        if (m_NoiseTextureLibrary)
        {
            if (screenSpaceVisibilityRequested)
            {
                visibilityNoise = m_NoiseTextureLibrary->Resolve(
                    m_CommandList,
                    visibilityNoiseSettings);
            }
            if (rayTracedSkyVisibilityExpectedToContribute)
            {
                skyNoise = m_NoiseTextureLibrary->Resolve(
                    m_CommandList,
                    skyNoiseSettings);
            }
            if (rayTracedFlashlightShadowExpectedToContribute)
            {
                flashlightNoise = m_NoiseTextureLibrary->Resolve(
                    m_CommandList,
                    flashlightNoiseSettings);
            }
        }
        const bool screenSpaceVisibilityReady =
            screenSpaceVisibilityRequested &&
            m_ScreenSpaceVisibilityPass &&
            m_ScreenSpaceVisibilityPass->ArePipelinesReady() &&
            bool(visibilityNoise);
        const bool runScreenSpaceVisibility = screenSpaceVisibilityReady;
        const bool writeSourceRadiance = runScreenSpaceVisibility &&
            m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
            (!submittedLights->empty() ||
                (diffuseEnvironment && diffuseEnvironmentScale > 0.f));

        const SpotLight* submittedFlashlight =
            ShouldSubmitFlashlight(m_FlashlightTransition)
                ? m_Flashlight.get()
                : nullptr;
#if defined(UVSR_BUILD_TESTING)
        m_FlashlightLightingSubmittedThisFrame =
            !pathTracingSelected && submittedFlashlight &&
            m_PbrDeferredLightingPass &&
            m_PbrDeferredLightingPass->ArePipelinesReady();
#endif
        const FlashlightBeamProfile flashlightBeamProfile =
            submittedFlashlight
                ? ResolveFlashlightBeamProfile(
                    m_ui.Flashlight,
                    m_FlashlightResolvedRight.x,
                    m_FlashlightResolvedRight.y,
                    m_FlashlightResolvedRight.z)
                : FlashlightBeamProfile{};
        const bool flashlightStochasticRequested =
            rayTracedFlashlightShadowExpectedToContribute &&
            submittedFlashlight &&
            flashlightBeamProfile.emitterRadiusMeters > 0.f;
        const bool skyVisibilityStochasticRequested =
            rayTracedSkyVisibilityExpectedToContribute;
        const bool screenSpaceVisibilityStochasticRequested =
            screenSpaceVisibilityRequested;
        const bool rayMarchingProducerTopologyReady =
            (!screenSpaceVisibilityRequested ||
                screenSpaceVisibilityReady) &&
            (!directionalRayVisibilityExpectedToContribute ||
                m_DirectionalRayVisibilityPass) &&
            (!rayTracedFlashlightShadowExpectedToContribute ||
                (submittedFlashlight && flashlightNoise)) &&
            (!rayTracedSkyVisibilityExpectedToContribute || skyNoise);
        const bool rayMarchingDiagnostic =
            m_ui.LightingDebugView != PbrLightingDebugView::None ||
            m_ui.ScreenSpaceVisibility.debugView !=
                VisibilityDebugView::FinalImage;
        const bool rayMarchingAccumulationSelected =
            !pathTracingSelected &&
            !rayMarchingDiagnostic &&
            m_ui.AccumulateSamples;
        const bool lightingScheduleConsumerSelected =
            screenSpaceVisibilityRequested ||
            rayTracedFlashlightShadowExpectedToContribute ||
            rayTracedSkyVisibilityExpectedToContribute;
        if (lightingScheduleConsumerSelected &&
            (!m_LightingAccumulationPass ||
                !m_LightingAccumulationPass->IsValid()))
        {
            FailOpenRendererFrame("lighting sample scheduling");
            return;
        }
        if (screenSpaceVisibilityRequested &&
            (!m_ScreenSpaceVisibilityPass ||
                !m_ScreenSpaceVisibilityPass->ArePipelinesReady() ||
                !visibilityNoise))
        {
            FailOpenRendererFrame("screen-space visibility preparation");
            return;
        }

        SynchronizeLightingAccumulationHistory(
            renderSize.x,
            renderSize.y,
            *submittedLights,
            worldRepresentationReady,
            lightingSceneContentChanged,
            visibilityNoiseSettings,
            skyNoiseSettings,
            flashlightNoiseSettings,
            screenSpaceVisibilityStochasticRequested,
            screenSpaceVisibilityReady,
            directionalRayVisibilitySelected,
            directionalRayVisibilityExpectedToContribute,
            rayTracedFlashlightShadowSelected,
            flashlightStochasticRequested,
            rayTracedFlashlightShadowExpectedToContribute &&
                submittedFlashlight && bool(flashlightNoise),
            rayTracedSkyVisibilitySelected,
            skyVisibilityStochasticRequested,
            rayTracedSkyVisibilityExpectedToContribute && bool(skyNoise));

        LightingSampleSchedule lightingSampleSchedule;
        bool lightingSampleSchedulePrepared = false;
        if (m_LightingAccumulationPass)
        {
            lightingSampleSchedule =
                m_LightingAccumulationPass->GetDisabledSchedule();
            if (rayMarchingAccumulationSelected)
            {
                if (!rayMarchingProducerTopologyReady ||
                    !m_LightingAccumulationPass->IsValid())
                {
                    FailOpenRendererFrame(
                        "lighting accumulation preparation");
                    return;
                }
                lightingSampleSchedule =
                    m_LightingAccumulationPass->PrepareAttempts(
                        m_CommandList,
                        renderSize.x,
                        renderSize.y,
                        m_LightingHistoryEpoch);
                lightingSampleSchedulePrepared =
                    lightingSampleSchedule.token != 0u;
                if (!lightingSampleSchedulePrepared ||
                    !lightingSampleSchedule.enabled ||
                    !lightingSampleSchedule)
                {
                    FailOpenRendererFrame(
                        "lighting accumulation preparation");
                    return;
                }
            }
        }

        auto lightingScheduleCancellation =
            uvsr::MakePreparedRendererTransactionCancellation(
                lightingSampleSchedulePrepared,
                [&]() noexcept
                {
                    m_LightingAccumulationPass->CancelPreparedSchedule(
                        lightingSampleSchedule);
                });

        m_RenderTargets->Clear(m_CommandList);
        DirectionalRayVisibilityResult directionalVisibilityResult;
        RayTracedFlashlightShadowResult flashlightShadowResult;
        RayTracedSkyVisibilityResult skyVisibilityResult;
        ScreenSpaceVisibilityResult screenSpaceVisibilityResult;
        nvrhi::ITexture* skyVisibility = nullptr;
        nvrhi::ITexture* rawClosestSkyVisibility = nullptr;
        nvrhi::ITexture* visibilitySkyVisibility = nullptr;
        uint32_t skyVisibilityReceiverSampleCount = 1u;
        bool applySkyVisibilityToDiffuseIbl = false;
        bool applySkyVisibilityToSpecularIbl = false;
        DirectLightVisibilities directLightVisibilities;
        MsaaVisibilityResolveOutputs closestSurfaceOutputs;
        bool closestSurfaceResolved = false;

        const auto resolveClosestMsaaSurface = [&]()
        {
            if (closestSurfaceResolved ||
                m_RenderTargets->GetSampleCount() <= 1u ||
                !m_MsaaVisibilityResolvePass)
            {
                return closestSurfaceResolved;
            }

            MsaaVisibilityResolveInputs resolveInputs;
            resolveInputs.depth = m_RenderTargets->Depth;
            resolveInputs.diffuse = m_RenderTargets->GBufferDiffuse;
            resolveInputs.material = m_RenderTargets->GBufferSpecular;
            resolveInputs.normals = m_RenderTargets->GBufferNormals;
            resolveInputs.emissive = m_RenderTargets->GBufferEmissive;
            resolveInputs.materialAmbientOcclusion =
                m_RenderTargets->MaterialAmbientOcclusion;
            resolveInputs.motionVectors =
                m_RenderTargets->MotionVectors;

            MsaaVisibilityResolveOutputs candidateOutputs;
            candidateOutputs.depth =
                m_RenderTargets->VisibilityDepth;
            candidateOutputs.diffuse =
                m_RenderTargets->VisibilityGBufferDiffuse;
            candidateOutputs.material =
                m_RenderTargets->VisibilityGBufferMaterial;
            candidateOutputs.normals =
                m_RenderTargets->VisibilityGBufferNormals;
            candidateOutputs.emissive =
                m_RenderTargets->VisibilityGBufferEmissive;
            candidateOutputs.materialAmbientOcclusion =
                m_RenderTargets->VisibilityMaterialAmbientOcclusion;
            candidateOutputs.motionVectors =
                m_RenderTargets->VisibilityMotionVectors;

            if (!candidateOutputs.depth ||
                !candidateOutputs.diffuse ||
                !candidateOutputs.material ||
                !candidateOutputs.normals ||
                !candidateOutputs.emissive ||
                !candidateOutputs.materialAmbientOcclusion ||
                !candidateOutputs.motionVectors)
            {
                closestSurfaceOutputs = {};
                return false;
            }

            BeginRendererStage(
                RendererTimingStage::MultisampleResolve);
            const bool dispatchSucceeded =
                m_MsaaVisibilityResolvePass->Render(
                m_CommandList,
                resolveInputs,
                candidateOutputs,
                m_RenderTargets->GetSampleCount());
            closestSurfaceResolved = PublishMsaaVisibilityResolveOutputs(
                dispatchSucceeded,
                candidateOutputs,
                closestSurfaceOutputs);
            EndRendererStage(
                RendererTimingStage::MultisampleResolve);
            return closestSurfaceResolved;
        };

        DeferredLightingPass::Inputs deferredMsaaInputs;
        bool deferredMsaaLightingPending = false;
        bool deferredMsaaVisibilityPending = false;
        m_SubmittedMainViewTriangles = 0u;
        EndRendererStage(RendererTimingStage::SceneSetup);

        PathTracingResult pathTracingResult;
        if (pathTracingSelected &&
            pathTracingSceneDomainSupported &&
            worldRepresentationReady &&
            m_PathTracingPass &&
            m_PathTracingPass->IsSupported())
        {
            PathTracingInputs pathInputs;
            pathInputs.view = m_View.get();
            pathInputs.previousView = m_PreviousView.get();
            pathInputs.width = uint32_t(windowWidth);
            pathInputs.height = uint32_t(windowHeight);
            pathInputs.materialVisibility = rayMaterialVisibility;
            pathInputs.worldTlas = m_WorldSpaceRepresentation
                ->GetTopLevelAccelerationStructure();
            pathInputs.environment = m_ImageBasedLightingEnvironment
                ? m_ImageBasedLightingEnvironment->GetRadianceTexture()
                : nullptr;
            pathInputs.environmentScale = m_ImageBasedLightingEnvironment
                ? m_ImageBasedLightingEnvironment->GetRadianceScale()
                : 0.f;
            pathInputs.showEnvironmentBackground =
                m_ui.ShowEnvironmentBackground;
            pathInputs.noiseSettings = m_ui.Noise;
            if (m_NoiseTextureLibrary)
            {
                const NoiseTextureBinding pathNoise =
                    m_NoiseTextureLibrary->Resolve(
                        m_CommandList,
                        pathInputs.noiseSettings);
                pathInputs.noiseTexture = pathNoise.texture;
            }
            pathInputs.lights = *submittedLights;
            pathInputs.flashlight = submittedFlashlight;
            pathInputs.flashlightProfile = flashlightBeamProfile;
            pathInputs.historyEpoch = m_LightingHistoryEpoch;
            BeginRendererStage(RendererTimingStage::PathTransport);
            pathTracingResult = m_PathTracingPass->Render(
                m_CommandList,
                pathInputs);
            EndRendererStage(RendererTimingStage::PathTransport);
            m_PathTransportDispatchedThisFrame =
                pathTracingResult.dispatched;
        }
        const bool pathTransportActive = bool(pathTracingResult);
        const bool pathTransportPermanentlyUnavailable =
            pathTracingSelected &&
            (!pathTracingSceneDomainSupported ||
                !m_ui.Representation.allowRayTraversal ||
                !m_PathTracingPass || !m_PathTracingPass->IsSupported() ||
                (worldRepresentationReady && !pathTransportActive));
        const SelectedLightingTransport selectedTransport =
            ResolveSelectedLightingTransport(
                m_ui.Lighting,
                pathTransportActive,
                pathTransportPermanentlyUnavailable);
        m_SelectedLightingTransportState = selectedTransport.state;
        if (pathTransportPermanentlyUnavailable)
        {
            if (!m_ReportedPathTransportFailure)
            {
                uvsr::log::error(
                    "Path tracing is selected but unavailable because the "
                    "scene domain, ray-query pipeline, or transport dispatch "
                    "failed; no raster transport will be substituted");
                m_ReportedPathTransportFailure = true;
            }
            FailOpenRendererFrame("path tracing transport");
            return;
        }
        else
        {
            m_ReportedPathTransportFailure = false;
        }

        if (selectedTransport.renderRayMarching)
        {
        if (!m_GBufferGeometryPass ||
            !m_PbrDeferredLightingPass ||
            !m_PbrDeferredLightingPass->ArePipelinesReady())
        {
            FailOpenRendererFrame("deferred PBR preparation");
            return;
        }
        {
            BeginRendererStage(RendererTimingStage::Geometry);
            const bool geometryRendered = RenderGeometry(
                *m_GBufferGeometryPass,
                m_RenderTargets->GBufferFramebuffer.Get(),
                m_View.get(),
                m_PreviousView
                    ? m_PreviousView.get()
                    : m_View.get(),
                "GBufferFill");
            m_SubmittedMainViewTriangles =
                m_GBufferGeometryPass->GetSubmittedTriangles();
            EndRendererStage(RendererTimingStage::Geometry);
            if (!geometryRendered)
            {
                throw std::runtime_error(
                    "UVSR G-buffer rendering failed");
            }

            const bool singleSurfaceDenoisingRequested =
                !rayMarchingAccumulationOwnsTemporalHistory &&
                ((directionalRayVisibilitySelected &&
                    m_ui.Denoising.shadows.method !=
                        DenoisingMethodChoice::None) ||
                    (rayTracedFlashlightShadowSelected &&
                    m_ui.Denoising.shadows.method !=
                        DenoisingMethodChoice::None) ||
                    (rayTracedSkyVisibilitySelected &&
                        m_ui.Denoising.skyVisibility.method !=
                            DenoisingMethodChoice::None));
            const bool closestSurfaceResolveRequired =
                m_RenderTargets->GetSampleCount() > 1u &&
                (runScreenSpaceVisibility ||
                    singleSurfaceDenoisingRequested ||
                    m_ui.UsesLongTermTemporalAA());
            if (closestSurfaceResolveRequired &&
                !resolveClosestMsaaSurface())
            {
                FailOpenRendererFrame(
                    "multisample closest-surface resolve");
                return;
            }
            const bool singleSurfaceInputsAvailable =
                m_RenderTargets->GetSampleCount() == 1u ||
                closestSurfaceResolved;
            nvrhi::ITexture* visibilityDepth =
                closestSurfaceResolved
                    ? closestSurfaceOutputs.depth
                    : m_RenderTargets->Depth.Get();
            const bool shadowRayDispatchExpected =
                worldRepresentationReady &&
                ((rayTracedFlashlightShadowSelected &&
                        submittedFlashlight &&
                        flashlightNoise) ||
                    (directionalRayVisibilitySelected &&
                        m_DirectionalRayVisibilityPass &&
                        m_SunLight));
            if (shadowRayDispatchExpected)
            {
                BeginRendererStage(
                    RendererTimingStage::ShadowRayDispatch);
            }
            if (rayTracedFlashlightShadowSelected &&
                worldRepresentationReady &&
                m_RayTracedFlashlightShadowPass &&
                submittedFlashlight &&
                flashlightNoise)
            {
                RayTracedFlashlightShadowInputs shadowInputs;
                shadowInputs.depth = m_RenderTargets->Depth;
                shadowInputs.material =
                    m_RenderTargets->GBufferSpecular;
                shadowInputs.normals =
                    m_RenderTargets->GBufferNormals;
                flashlightShadowResult =
                    m_RayTracedFlashlightShadowPass->Render(
                        m_CommandList,
                        *m_View,
                        shadowInputs,
                        rayMaterialVisibility,
                        m_WorldSpaceRepresentation
                            ->GetTopLevelAccelerationStructure(),
                        submittedFlashlight,
                        flashlightBeamProfile,
                        flashlightNoiseSettings,
                        flashlightNoise.texture,
                        flashlightNoiseSettings.animate
                            ? uint32_t(
                                m_RayTracedFlashlightShadowPhase)
                            : 0u,
                        DefaultFlashlightRayBiasMeters,
                        m_ui.Flashlight.outputHitDistance,
                        lightingSampleSchedule);
            }
            m_RayTracedFlashlightShadowDispatchedThisFrame =
                flashlightShadowResult.dispatched;
            const bool flashlightShadowContributed =
                bool(flashlightShadowResult);
            if (flashlightShadowContributed !=
                m_RayTracedFlashlightShadowContributedLastFrame)
            {
                ResetAntiAliasingState();
                InvalidateRendererStageTiming(
                    RendererTimingStage::ShadowRayDispatch);
                m_RayTracedFlashlightShadowContributedLastFrame =
                    flashlightShadowContributed;
            }
            if (directionalRayVisibilitySelected &&
                m_DirectionalRayVisibilityPass &&
                worldRepresentationReady &&
                m_SunLight)
            {
                DirectionalRayVisibilityInputs shadowInputs;
                shadowInputs.depth = m_RenderTargets->Depth;
                shadowInputs.material = m_RenderTargets->GBufferSpecular;
                shadowInputs.normals = m_RenderTargets->GBufferNormals;
                directionalVisibilityResult =
                    m_DirectionalRayVisibilityPass->Render(
                        m_CommandList,
                        m_ui.DirectionalShadows,
                        *m_View,
                        shadowInputs,
                        rayMaterialVisibility,
                        m_WorldSpaceRepresentation
                            ->GetTopLevelAccelerationStructure(),
                        m_SunLight.get(),
                        m_SceneDiagonal);
            }
            m_DirectionalRayVisibilityDispatchedThisFrame =
                directionalVisibilityResult.dispatched;
            if (shadowRayDispatchExpected)
            {
                EndRendererStage(
                    RendererTimingStage::ShadowRayDispatch);
            }
            if (rayTracedFlashlightShadowExpectedToContribute &&
                !flashlightShadowResult.dispatched)
            {
                FailOpenRendererFrame(
                    "ray-traced flashlight visibility");
                return;
            }
            if (directionalRayVisibilityExpectedToContribute &&
                !directionalVisibilityResult.dispatched)
            {
                FailOpenRendererFrame("directional ray visibility");
                return;
            }
            if (rayTracedSkyVisibilityExpectedToContribute &&
                m_RayTracedSkyVisibilityPass &&
                skyNoise)
            {
                RayTracedSkyVisibilityInputs skyInputs;
                skyInputs.depth = m_RenderTargets->Depth;
                skyInputs.material =
                    m_RenderTargets->GBufferSpecular;
                skyInputs.normals =
                    m_RenderTargets->GBufferNormals;
                BeginRendererStage(
                    RendererTimingStage::SkyVisibilityRayDispatch);
                skyVisibilityResult =
                    m_RayTracedSkyVisibilityPass->Render(
                        m_CommandList,
                        m_ui.RayTracedSkyVisibility,
                        *m_View,
                        skyInputs,
                        rayMaterialVisibility,
                        m_WorldSpaceRepresentation
                            ->GetTopLevelAccelerationStructure(),
                        skyNoiseSettings,
                        skyNoise.texture,
                        skyNoiseSettings.animate
                            ? uint32_t(m_RayTracedSkyVisibilityPhase)
                            : 0u,
                        m_SceneDiagonal,
                        lightingSampleSchedule);
                EndRendererStage(
                    RendererTimingStage::SkyVisibilityRayDispatch);
            }
            const bool rayTracedSkyVisibilityContributed =
                bool(skyVisibilityResult);
            m_RayTracedSkyVisibilityDispatchedThisFrame =
                skyVisibilityResult.dispatched;
            if (rayTracedSkyVisibilityExpectedToContribute &&
                !skyVisibilityResult.dispatched)
            {
                FailOpenRendererFrame("ray-traced sky visibility");
                return;
            }
            if (rayTracedSkyVisibilityContributed !=
                m_RayTracedSkyVisibilityContributedLastFrame)
            {
                ResetAntiAliasingState();
                InvalidateRendererStageTiming(
                    RendererTimingStage::SkyVisibilityRayDispatch);
                m_RayTracedSkyVisibilityContributedLastFrame =
                    rayTracedSkyVisibilityContributed;
            }

            nvrhi::ITexture* visibilityNormalRoughness =
                closestSurfaceResolved
                    ? closestSurfaceOutputs.normals
                    : m_RenderTargets->GBufferNormals.Get();
            nvrhi::ITexture* visibilityMotionVectors =
                closestSurfaceResolved
                    ? closestSurfaceOutputs.motionVectors
                    : m_RenderTargets->MotionVectors.Get();
            const nvrhi::TextureDesc& visibilityDepthDescription =
                visibilityDepth->getDesc();
            const uint2 visibilityFullSize(
                visibilityDepthDescription.width,
                visibilityDepthDescription.height);
            const auto makeDenoisingInputs =
                [&](nvrhi::ITexture* rawSignal,
                    nvrhi::ITexture* hitDistance,
                    uint2 sourceSize = uint2::zero())
            {
                DenoisingInputs inputs;
                inputs.rawSignal = rawSignal;
                inputs.hitDistance = hitDistance;
                inputs.depth = visibilityDepth;
                inputs.normalRoughness = visibilityNormalRoughness;
                inputs.motionVectors = visibilityMotionVectors;
                inputs.currentView = m_View.get();
                inputs.previousView = m_PreviousView.get();
                inputs.signalSize = visibilityFullSize;
                inputs.sourceSize = sourceSize;
                inputs.frameDeltaSeconds = m_FrameDeltaSeconds;
                inputs.frameIndex = GetFrameIndex();
                return inputs;
            };
            // The sample accumulator must be the only temporal estimator in
            // this path. Feeding temporally denoised or repeatedly denoised
            // skipped signals into its mean would correlate samples and could
            // preserve pre-motion history.
            const bool rayMarchingDenoisingAllowed =
                !rayMarchingAccumulationOwnsTemporalHistory;
            const DenoisingMethodChoice ambientOcclusionDenoisingMethod =
                m_ui.Denoising.ambientOcclusion.method;
            const DenoisingMethodChoice diffuseGiDenoisingMethod =
                m_ui.Denoising.diffuseGi.method;
            const bool ambientOcclusionDenoisingSelected =
                rayMarchingDenoisingAllowed &&
                screenSpaceVisibilityRequested &&
                m_ui.ScreenSpaceVisibility.HasActiveAmbientOcclusion() &&
                ambientOcclusionDenoisingMethod !=
                    DenoisingMethodChoice::None;
            const bool diffuseGiDenoisingSelected =
                rayMarchingDenoisingAllowed &&
                screenSpaceVisibilityRequested &&
                m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
                diffuseGiDenoisingMethod != DenoisingMethodChoice::None;
            const bool denoisingSpatialAvailable =
                m_DenoisingPass &&
                m_DenoisingPass->IsSpatialAvailable();
            const bool denoisingThirdPartyAvailable =
                m_DenoisingPass &&
                m_DenoisingPass->IsOperational();
            const bool ambientOcclusionDenoisingReady =
                ambientOcclusionDenoisingSelected &&
                singleSurfaceInputsAvailable &&
                SupportsDenoisingMethod(
                    DenoisingEffect::AmbientOcclusion,
                    ambientOcclusionDenoisingMethod) &&
                IsDenoisingMethodRuntimeAvailable(
                    ambientOcclusionDenoisingMethod,
                    denoisingSpatialAvailable,
                    denoisingThirdPartyAvailable) &&
                (!IsThirdPartyDenoisingMethod(
                        ambientOcclusionDenoisingMethod) ||
                    m_ui.ScreenSpaceVisibility.ambientOcclusion
                        .outputHitDistance);
            const bool diffuseGiDenoisingReady =
                diffuseGiDenoisingSelected &&
                singleSurfaceInputsAvailable &&
                SupportsDenoisingMethod(
                    DenoisingEffect::DiffuseGi,
                    diffuseGiDenoisingMethod) &&
                IsDenoisingMethodRuntimeAvailable(
                    diffuseGiDenoisingMethod,
                    denoisingSpatialAvailable,
                    denoisingThirdPartyAvailable) &&
                (!IsThirdPartyDenoisingMethod(
                        diffuseGiDenoisingMethod) ||
                    m_ui.ScreenSpaceVisibility.indirectDiffuse
                        .outputHitDistance);
            if (ambientOcclusionDenoisingSelected &&
                !ambientOcclusionDenoisingReady)
            {
                FailOpenRendererFrame(
                    "ambient-occlusion denoising preparation");
                return;
            }
            if (diffuseGiDenoisingSelected &&
                !diffuseGiDenoisingReady)
            {
                FailOpenRendererFrame(
                    "diffuse-GI denoising preparation");
                return;
            }
            if (m_DenoisingPass)
            {
                if (!ambientOcclusionDenoisingSelected)
                {
                    m_DenoisingPass->DisableSignal(
                        DenoiserSignalType::AmbientOcclusion);
                }
                if (!diffuseGiDenoisingSelected)
                {
                    m_DenoisingPass->DisableSignal(
                        DenoiserSignalType::DiffuseGi);
                }
            }
            bool ambientOcclusionDenoisingInvoked = false;
            bool ambientOcclusionDenoisingFailed = false;
            bool diffuseGiDenoisingInvoked = false;
            bool diffuseGiDenoisingFailed = false;
            const auto configureDiffuseDenoising =
                [&](ScreenSpaceVisibilityInputs& inputs)
            {
                if (ambientOcclusionDenoisingSelected)
                {
                    inputs.processAmbientOcclusion =
                        [&](nvrhi::ICommandList* commandList,
                            nvrhi::ITexture* rawSignal,
                            nvrhi::ITexture* hitDistance,
                            uint2 sourceSize,
                            bool hitDistanceMatchesSignal)
                        {
                            DenoisingInputs denoisingInputs =
                                makeDenoisingInputs(
                                    rawSignal,
                                    hitDistance,
                                    sourceSize);
                            denoisingInputs.hitDistanceNormalization =
                                std::max(
                                    m_ui.ScreenSpaceVisibility.sampling.radius,
                                    0.001f);
                            denoisingInputs.hitDistanceMatchesSignal =
                                hitDistanceMatchesSignal;
                            BeginRendererStage(
                                RendererTimingStage::
                                    AmbientOcclusionDenoise);
                            const DenoisingResult result =
                                m_DenoisingPass->ProcessAmbientOcclusion(
                                    commandList,
                                    m_ui.Denoising.ambientOcclusion,
                                    denoisingInputs);
                            EndRendererStage(
                                RendererTimingStage::
                                    AmbientOcclusionDenoise);
                            m_AmbientOcclusionDenoisedThisFrame =
                                result.denoised;
                            ambientOcclusionDenoisingInvoked = true;
                            ambientOcclusionDenoisingFailed =
                                ambientOcclusionDenoisingFailed ||
                                !result.denoised || !result.texture;
                            return result.texture
                                ? result.texture
                                : rawSignal;
                        };
                }
                if (diffuseGiDenoisingSelected)
                {
                    inputs.processIndirectDiffuse =
                        [&](nvrhi::ICommandList* commandList,
                            nvrhi::ITexture* rawSignal,
                            nvrhi::ITexture* hitDistance,
                            uint2 sourceSize,
                            bool hitDistanceMatchesSignal)
                        {
                            DenoisingInputs denoisingInputs =
                                makeDenoisingInputs(
                                    rawSignal,
                                    hitDistance,
                                    sourceSize);
                            denoisingInputs.hitDistanceNormalization =
                                std::max(
                                    m_ui.ScreenSpaceVisibility.sampling.radius,
                                    0.001f);
                            denoisingInputs.hitDistanceMatchesSignal =
                                hitDistanceMatchesSignal;
                            BeginRendererStage(
                                RendererTimingStage::
                                    DiffuseIlluminationDenoise);
                            const DenoisingResult result =
                                m_DenoisingPass->ProcessDiffuseGi(
                                    commandList,
                                    m_ui.Denoising.diffuseGi,
                                    denoisingInputs);
                            EndRendererStage(
                                RendererTimingStage::
                                    DiffuseIlluminationDenoise);
                            m_DiffuseIlluminationDenoisedThisFrame =
                                result.denoised;
                            diffuseGiDenoisingInvoked = true;
                            diffuseGiDenoisingFailed =
                                diffuseGiDenoisingFailed ||
                                !result.denoised || !result.texture;
                            return result.texture
                                ? result.texture
                                : rawSignal;
                        };
                }
            };

            nvrhi::ITexture* flashlightVisibility =
                flashlightShadowResult.visibility;
            nvrhi::ITexture* flashlightClosestVisibility =
                flashlightShadowResult.closestVisibility;
            nvrhi::ITexture* directionalVisibility =
                directionalVisibilityResult.visibility;
            nvrhi::ITexture* directionalClosestVisibility =
                directionalVisibilityResult.closestVisibility;
            const DenoisingMethodChoice shadowDenoisingMethod =
                m_ui.Denoising.shadows.method;
            const bool shadowThirdPartyDenoising =
                IsThirdPartyDenoisingMethod(shadowDenoisingMethod);
            const bool shadowDenoisingMethodAvailable =
                SupportsDenoisingMethod(
                    DenoisingEffect::Shadows,
                    shadowDenoisingMethod) &&
                IsDenoisingMethodRuntimeAvailable(
                    shadowDenoisingMethod,
                    denoisingSpatialAvailable,
                    denoisingThirdPartyAvailable);
            const bool flashlightDenoisingSelected =
                rayMarchingDenoisingAllowed &&
                rayTracedFlashlightShadowExpectedToContribute &&
                shadowDenoisingMethod != DenoisingMethodChoice::None;
            const bool sunDenoisingSelected =
                rayMarchingDenoisingAllowed &&
                directionalRayVisibilityExpectedToContribute &&
                shadowDenoisingMethod != DenoisingMethodChoice::None;
            const bool flashlightDenoisingReady =
                flashlightDenoisingSelected &&
                singleSurfaceInputsAvailable &&
                flashlightShadowResult &&
                shadowDenoisingMethodAvailable &&
                (!shadowThirdPartyDenoising ||
                    (m_ui.Flashlight.outputHitDistance &&
                        flashlightShadowResult.hitDistance));
            const float sunTanAngularRadius = m_SunLight
                ? std::tan(radians(
                    m_SunLight->angularSize * 0.5f))
                : 0.f;
            const bool sunDenoisingReady =
                sunDenoisingSelected &&
                singleSurfaceInputsAvailable &&
                directionalVisibilityResult &&
                shadowDenoisingMethodAvailable &&
                (!shadowThirdPartyDenoising ||
                    (directionalVisibilityResult.closestHitDistance &&
                        std::isfinite(sunTanAngularRadius) &&
                        sunTanAngularRadius > 0.f));
            if (flashlightDenoisingSelected &&
                !flashlightDenoisingReady)
            {
                FailOpenRendererFrame(
                    "flashlight shadow denoising preparation");
                return;
            }
            if (sunDenoisingSelected && !sunDenoisingReady)
            {
                FailOpenRendererFrame(
                    "directional shadow denoising preparation");
                return;
            }
            const bool shadowDenoisingSelected =
                flashlightDenoisingSelected || sunDenoisingSelected;
            if (shadowDenoisingSelected)
            {
                BeginRendererStage(
                    RendererTimingStage::ShadowDenoise);
            }
            bool shadowDenoisingFailed = false;
            if (m_DenoisingPass)
            {
                if (flashlightDenoisingSelected)
                {
                    DenoisingInputs inputs = makeDenoisingInputs(
                        flashlightShadowResult.closestVisibility,
                        flashlightShadowResult.hitDistance);
                    inputs.hitDistanceNormalization = std::max(
                        m_ui.Flashlight.rangeMeters, 0.001f);
                    inputs.localLightPosition =
                        float3(submittedFlashlight->GetPosition());
                    inputs.localLightRadius =
                        flashlightBeamProfile.emitterRadiusMeters;
                    const DenoisingResult result =
                        m_DenoisingPass->ProcessFlashlightShadow(
                            m_CommandList,
                            m_ui.Denoising.shadows,
                            inputs);
                    flashlightClosestVisibility = result.texture
                        ? result.texture
                        : flashlightShadowResult.closestVisibility;
                    if (flashlightShadowResult.receiverSampleCount == 1u)
                        flashlightVisibility =
                            flashlightClosestVisibility;
                    m_ShadowDenoisingDispatchedThisFrame =
                        m_ShadowDenoisingDispatchedThisFrame ||
                        result.denoised;
                    shadowDenoisingFailed = shadowDenoisingFailed ||
                        !result.denoised || !result.texture;
                }
                else
                {
                    m_DenoisingPass->DisableSignal(
                        DenoiserSignalType::FlashlightShadow);
                }
                if (sunDenoisingSelected)
                {
                    DenoisingInputs inputs = makeDenoisingInputs(
                        directionalVisibilityResult.closestVisibility,
                        directionalVisibilityResult.closestHitDistance);
                    inputs.hitDistanceNormalization =
                        ResolveRayVisibilityMaxDistance(
                            m_ui.DirectionalShadows.maxDistance,
                            m_SceneDiagonal);
                    inputs.lightDirectionWorld =
                        float3(m_SunLight->GetDirection());
                    inputs.directionalTanAngularRadius =
                        sunTanAngularRadius;
                    const DenoisingResult result =
                        m_DenoisingPass->ProcessSunShadow(
                            m_CommandList,
                            m_ui.Denoising.shadows,
                            inputs);
                    directionalClosestVisibility = result.texture
                        ? result.texture
                        : directionalVisibilityResult.closestVisibility;
                    if (directionalVisibilityResult.receiverSampleCount == 1u)
                        directionalVisibility =
                            directionalClosestVisibility;
                    m_ShadowDenoisingDispatchedThisFrame =
                        m_ShadowDenoisingDispatchedThisFrame ||
                        result.denoised;
                    shadowDenoisingFailed = shadowDenoisingFailed ||
                        !result.denoised || !result.texture;
                }
                else
                {
                    m_DenoisingPass->DisableSignal(
                        DenoiserSignalType::SunShadow);
                }
            }
            if (shadowDenoisingSelected)
            {
                EndRendererStage(
                    RendererTimingStage::ShadowDenoise);
            }
            if (shadowDenoisingFailed)
            {
                FailOpenRendererFrame("selected shadow denoising");
                return;
            }
            if (flashlightShadowResult && flashlightVisibility)
            {
                directLightVisibilities.flashlight = {
                    flashlightVisibility,
                    flashlightShadowResult.light,
                    flashlightShadowResult.receiverSampleCount,
                    flashlightShadowResult.receiverSampleCount > 1u
                        ? flashlightShadowResult.closestVisibility
                        : nullptr,
                    flashlightShadowResult.receiverSampleCount > 1u
                        ? flashlightClosestVisibility
                        : nullptr
                };
            }
            if (directionalVisibilityResult && directionalVisibility)
            {
                directLightVisibilities.sun = {
                    directionalVisibility,
                    directionalVisibilityResult.light,
                    directionalVisibilityResult.receiverSampleCount,
                    directionalVisibilityResult.receiverSampleCount > 1u
                        ? directionalVisibilityResult.closestVisibility
                        : nullptr,
                    directionalVisibilityResult.receiverSampleCount > 1u
                        ? directionalClosestVisibility
                        : nullptr
                };
            }

            skyVisibility = skyVisibilityResult
                ? skyVisibilityResult.visibility
                : nullptr;
            rawClosestSkyVisibility = skyVisibilityResult
                ? skyVisibilityResult.closestVisibility
                : nullptr;
            visibilitySkyVisibility = skyVisibilityResult
                ? skyVisibilityResult.closestVisibility
                : nullptr;
            skyVisibilityReceiverSampleCount = skyVisibilityResult
                ? skyVisibilityResult.receiverSampleCount
                : 1u;
            const DenoisingMethodChoice skyDenoisingMethod =
                m_ui.Denoising.skyVisibility.method;
            const bool skyDenoisingSelected =
                rayMarchingDenoisingAllowed &&
                rayTracedSkyVisibilityExpectedToContribute &&
                skyDenoisingMethod != DenoisingMethodChoice::None;
            const bool skyDenoisingReady =
                skyDenoisingSelected &&
                singleSurfaceInputsAvailable &&
                skyVisibilityResult &&
                SupportsDenoisingMethod(
                    DenoisingEffect::SkyVisibility,
                    skyDenoisingMethod) &&
                IsDenoisingMethodRuntimeAvailable(
                    skyDenoisingMethod,
                    denoisingSpatialAvailable,
                    denoisingThirdPartyAvailable) &&
                (!IsThirdPartyDenoisingMethod(skyDenoisingMethod) ||
                    (m_ui.RayTracedSkyVisibility.outputHitDistance &&
                        skyVisibilityResult.hitDistance));
            if (skyDenoisingSelected && !skyDenoisingReady)
            {
                FailOpenRendererFrame(
                    "sky-visibility denoising preparation");
                return;
            }
            if (m_DenoisingPass)
            {
                if (skyDenoisingSelected)
                {
                    DenoisingInputs inputs = makeDenoisingInputs(
                        skyVisibilityResult.closestVisibility,
                        skyVisibilityResult.hitDistance);
                    inputs.hitDistanceNormalization =
                        ResolveRayVisibilityMaxDistance(
                            m_ui.RayTracedSkyVisibility.maxDistance,
                            m_SceneDiagonal);
                    inputs.hitDistanceMatchesSignal = true;
                    BeginRendererStage(
                        RendererTimingStage::SkyVisibilityDenoise);
                    const DenoisingResult result =
                        m_DenoisingPass->ProcessSkyVisibility(
                            m_CommandList,
                            m_ui.Denoising.skyVisibility,
                            inputs);
                    EndRendererStage(
                        RendererTimingStage::SkyVisibilityDenoise);
                    visibilitySkyVisibility = result.texture
                        ? result.texture
                        : skyVisibilityResult.closestVisibility;
                    if (skyVisibilityResult.receiverSampleCount == 1u)
                        skyVisibility = visibilitySkyVisibility;
                    m_RayTracedSkyVisibilityDenoisedThisFrame =
                        result.denoised;
                    if (!result.denoised || !result.texture)
                    {
                        FailOpenRendererFrame(
                            "sky-visibility denoising");
                        return;
                    }
                }
                else
                {
                    m_DenoisingPass->DisableSignal(
                        DenoiserSignalType::SkyVisibility);
                }
            }
            applySkyVisibilityToDiffuseIbl =
                skyVisibility &&
                m_ui.RayTracedSkyVisibility.applyToDiffuseIbl;
            applySkyVisibilityToSpecularIbl =
                skyVisibility &&
                m_ui.RayTracedSkyVisibility.applyToSpecularIbl;
            DeferredLightingPass::Inputs deferredInputs;
            deferredInputs.depth = m_RenderTargets->Depth.Get();
            deferredInputs.gbufferNormals =
                m_RenderTargets->GBufferNormals.Get();
            deferredInputs.gbufferDiffuse =
                m_RenderTargets->GBufferDiffuse.Get();
            deferredInputs.gbufferSpecular =
                m_RenderTargets->GBufferSpecular.Get();
            deferredInputs.gbufferEmissive =
                m_RenderTargets->GBufferEmissive.Get();
            // Slot 14 carries the authored material ambient-occlusion
            // attachment.
            deferredInputs.indirectDiffuse =
                m_RenderTargets->MaterialAmbientOcclusion;
            deferredInputs.lights = submittedLights;

            deferredInputs.output = runScreenSpaceVisibility
                ? m_RenderTargets->BaseLighting.Get()
                : m_RenderTargets->HdrColor.Get();

            if (m_RenderTargets->GetSampleCount() > 1u)
            {
                // Preserve every G-buffer sample until after material decode
                // and nonlinear lighting. The resolved sky is added later as
                // the exact uncovered-sample contribution.
                deferredMsaaInputs = deferredInputs;
                deferredMsaaInputs.output =
                    m_RenderTargets->DeferredMsaaColor;
                deferredMsaaLightingPending = true;

                if (runScreenSpaceVisibility &&
                    closestSurfaceResolved &&
                    m_ScreenSpaceVisibilityPass)
                {
                    DeferredLightingPass::Inputs
                        visibilityDeferredInputs =
                            deferredInputs;
                    visibilityDeferredInputs.depth =
                        closestSurfaceOutputs.depth;
                    visibilityDeferredInputs.gbufferDiffuse =
                        closestSurfaceOutputs.diffuse;
                    visibilityDeferredInputs.gbufferSpecular =
                        closestSurfaceOutputs.material;
                    visibilityDeferredInputs.gbufferNormals =
                        closestSurfaceOutputs.normals;
                    visibilityDeferredInputs.gbufferEmissive =
                        closestSurfaceOutputs.emissive;
                    visibilityDeferredInputs.indirectDiffuse =
                        closestSurfaceOutputs
                            .materialAmbientOcclusion;
                    visibilityDeferredInputs.output =
                        m_RenderTargets->BaseLighting;
                    DirectLightVisibilities
                        visibilityDirectLightVisibilities =
                            directLightVisibilities;
                    if (flashlightClosestVisibility &&
                        flashlightShadowResult.light)
                    {
                        visibilityDirectLightVisibilities.flashlight = {
                            flashlightClosestVisibility,
                            flashlightShadowResult.light,
                            1u
                        };
                    }
                    if (directionalClosestVisibility &&
                        directionalVisibilityResult.light)
                    {
                        // AO/GI uses the coherent closest receiver selected by
                        // the G-buffer resolve while direct MSAA lighting keeps
                        // the per-raster-sample visibility result.
                        visibilityDirectLightVisibilities.sun = {
                            directionalClosestVisibility,
                            directionalVisibilityResult.light,
                            1u
                        };
                    }
                    BeginRendererStage(
                        RendererTimingStage::VisibilityLightingPreparation);
                    const PbrDeferredLightingRenderResult
                        visibilityLightingResult =
                            m_PbrDeferredLightingPass->Render(
                        m_CommandList,
                        *m_View,
                        visibilityDeferredInputs,
                        visibilityDirectLightVisibilities,
                        submittedFlashlight,
                        flashlightBeamProfile,
                        globalEnvironment,
                        visibilitySkyVisibility,
                        nullptr,
                        nullptr,
                        1u,
                        applySkyVisibilityToDiffuseIbl,
                        applySkyVisibilityToSpecularIbl,
                        m_RenderTargets
                            ->DirectDiffuseRadiance,
                        true,
                        writeSourceRadiance,
                        uint32_t(m_ui.LightingDebugView),
                        uint32_t(m_ui.ScreenSpaceVisibility.debugView),
                        float2(0.f));
                    EndRendererStage(
                        RendererTimingStage::VisibilityLightingPreparation);
                    if (!visibilityLightingResult.Succeeded())
                    {
                        FailOpenRendererFrame(
                            "visibility lighting preparation");
                        return;
                    }
                    m_VisibilityLightingPreparationDispatchedThisFrame = true;

                    ScreenSpaceVisibilityInputs
                        visibilityInputs;
                    visibilityInputs.depth =
                        closestSurfaceOutputs.depth;
                    visibilityInputs.normals =
                        closestSurfaceOutputs.normals;
                    visibilityInputs.sourceRadiance =
                        writeSourceRadiance
                            ? m_RenderTargets->DirectDiffuseRadiance.Get()
                            : nullptr;
                    visibilityInputs.gbufferDiffuse =
                        closestSurfaceOutputs.diffuse;
                    visibilityInputs.gbufferSpecular =
                        closestSurfaceOutputs.material;
                    visibilityInputs.gbufferEmissive =
                        closestSurfaceOutputs.emissive;
                    visibilityInputs.materialAmbientOcclusion =
                        closestSurfaceOutputs
                            .materialAmbientOcclusion;
                    visibilityInputs.diffuseEnvironment =
                        diffuseEnvironment;
                    visibilityInputs.diffuseEnvironmentScale =
                        diffuseEnvironmentScale;
                    visibilityInputs.skyVisibility =
                        visibilitySkyVisibility;
                    visibilityInputs.applySkyVisibilityToDiffuseIbl =
                        applySkyVisibilityToDiffuseIbl;
                    visibilityInputs.applySkyVisibilityToSpecularIbl =
                        applySkyVisibilityToSpecularIbl;
                    if (globalEnvironment)
                    {
                        visibilityInputs.diffuseEnvironmentArrayIndex =
                            globalEnvironment->diffuseArrayIndex;
                        visibilityInputs.specularEnvironment =
                            globalEnvironment->specularMap.Get();
                        visibilityInputs.environmentBrdf =
                            globalEnvironment->environmentBrdf.Get();
                        visibilityInputs.specularEnvironmentScale =
                            globalEnvironment->specularScale;
                        visibilityInputs.specularEnvironmentMipLevels =
                            globalEnvironment->specularMap
                                ? float(globalEnvironment->specularMap->
                                    getDesc().mipLevels)
                                : 0.f;
                        visibilityInputs.specularEnvironmentArrayIndex =
                            globalEnvironment->specularArrayIndex;
                    }
                    visibilityInputs.baseLighting =
                        m_RenderTargets->BaseLighting;
                    visibilityInputs.lightingDebugView =
                        uint32_t(m_ui.LightingDebugView);
                    visibilityInputs.output =
                        m_RenderTargets
                            ->VisibilityComposite;
                    configureDiffuseDenoising(visibilityInputs);
                    // Direct lighting above is a separate producer. Start the
                    // visibility envelope only when the visibility pass does.
                    BeginRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                    screenSpaceVisibilityResult =
                        m_ScreenSpaceVisibilityPass->Render(
                        m_CommandList,
                        m_ui.ScreenSpaceVisibility,
                        *m_View,
                        visibilityInputs,
                        visibilityNoiseSettings,
                        visibilityNoise.texture,
                        visibilityNoiseSettings.animate
                            ? uint32_t(m_ScreenSpaceVisibilityPhase)
                            : 0u,
                        lightingSampleSchedule);
                    EndRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                    if (screenSpaceVisibilityResult.HasFailed())
                    {
                        FailOpenRendererFrame("screen-space visibility");
                        return;
                    }
                    if (ambientOcclusionDenoisingSelected &&
                        (!ambientOcclusionDenoisingInvoked ||
                            ambientOcclusionDenoisingFailed))
                    {
                        FailOpenRendererFrame(
                            "ambient-occlusion denoising");
                        return;
                    }
                    if (diffuseGiDenoisingSelected &&
                        (!diffuseGiDenoisingInvoked ||
                            diffuseGiDenoisingFailed))
                    {
                        FailOpenRendererFrame("diffuse-GI denoising");
                        return;
                    }
                    deferredMsaaVisibilityPending =
                        screenSpaceVisibilityResult.status ==
                        ScreenSpaceVisibilityRenderStatus::Dispatched;
                }
                else if (m_ScreenSpaceVisibilityPass)
                {
                    m_ScreenSpaceVisibilityPass->Deactivate();
                }
            }
            else
            {
                BeginRendererStage(RendererTimingStage::DirectLighting);
                const PbrDeferredLightingRenderResult lightingResult =
                    m_PbrDeferredLightingPass->Render(
                    m_CommandList,
                    *m_View,
                    deferredInputs,
                    directLightVisibilities,
                    submittedFlashlight,
                    flashlightBeamProfile,
                    globalEnvironment,
                    skyVisibility,
                    rawClosestSkyVisibility,
                    visibilitySkyVisibility,
                    skyVisibilityReceiverSampleCount,
                    applySkyVisibilityToDiffuseIbl,
                    applySkyVisibilityToSpecularIbl,
                    m_RenderTargets->DirectDiffuseRadiance,
                    runScreenSpaceVisibility,
                    writeSourceRadiance,
                    uint32_t(m_ui.LightingDebugView),
                    uint32_t(m_ui.ScreenSpaceVisibility.debugView),
                    float2(0.f),
                    nullptr,
                    1u,
                    nullptr,
                    nullptr);
                EndRendererStage(RendererTimingStage::DirectLighting);
                if (!lightingResult.Succeeded())
                {
                    FailOpenRendererFrame("direct lighting");
                    return;
                }

                if (runScreenSpaceVisibility)
                {
                    ScreenSpaceVisibilityInputs visibilityInputs;
                    visibilityInputs.depth = m_RenderTargets->Depth;
                    visibilityInputs.normals = m_RenderTargets->GBufferNormals;
                    visibilityInputs.sourceRadiance = writeSourceRadiance
                        ? m_RenderTargets->DirectDiffuseRadiance.Get()
                        : nullptr;
                    visibilityInputs.gbufferDiffuse = m_RenderTargets->GBufferDiffuse;
                    visibilityInputs.gbufferSpecular =
                        m_RenderTargets->GBufferSpecular;
                    visibilityInputs.gbufferEmissive = m_RenderTargets->GBufferEmissive;
                    visibilityInputs.materialAmbientOcclusion =
                        m_RenderTargets->MaterialAmbientOcclusion;
                    visibilityInputs.diffuseEnvironment =
                        diffuseEnvironment;
                    visibilityInputs.diffuseEnvironmentScale =
                        diffuseEnvironmentScale;
                    visibilityInputs.skyVisibility =
                        visibilitySkyVisibility;
                    visibilityInputs.applySkyVisibilityToDiffuseIbl =
                        applySkyVisibilityToDiffuseIbl;
                    visibilityInputs.applySkyVisibilityToSpecularIbl =
                        applySkyVisibilityToSpecularIbl;
                    if (globalEnvironment)
                    {
                        visibilityInputs.diffuseEnvironmentArrayIndex =
                            globalEnvironment->diffuseArrayIndex;
                        visibilityInputs.specularEnvironment =
                            globalEnvironment->specularMap.Get();
                        visibilityInputs.environmentBrdf =
                            globalEnvironment->environmentBrdf.Get();
                        visibilityInputs.specularEnvironmentScale =
                            globalEnvironment->specularScale;
                        visibilityInputs.specularEnvironmentMipLevels =
                            globalEnvironment->specularMap
                                ? float(globalEnvironment->specularMap->
                                    getDesc().mipLevels)
                                : 0.f;
                        visibilityInputs.specularEnvironmentArrayIndex =
                            globalEnvironment->specularArrayIndex;
                    }
                    visibilityInputs.baseLighting = m_RenderTargets->BaseLighting;
                    visibilityInputs.lightingDebugView =
                        uint32_t(m_ui.LightingDebugView);
                    visibilityInputs.output = m_RenderTargets->HdrColor;
                    configureDiffuseDenoising(visibilityInputs);
                    BeginRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                    screenSpaceVisibilityResult =
                        m_ScreenSpaceVisibilityPass->Render(
                        m_CommandList,
                        m_ui.ScreenSpaceVisibility,
                        *m_View,
                        visibilityInputs,
                        visibilityNoiseSettings,
                        visibilityNoise.texture,
                        visibilityNoiseSettings.animate
                            ? uint32_t(m_ScreenSpaceVisibilityPhase)
                            : 0u,
                        lightingSampleSchedule);
                    EndRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                    if (screenSpaceVisibilityResult.HasFailed())
                    {
                        FailOpenRendererFrame("screen-space visibility");
                        return;
                    }
                    if (ambientOcclusionDenoisingSelected &&
                        (!ambientOcclusionDenoisingInvoked ||
                            ambientOcclusionDenoisingFailed))
                    {
                        FailOpenRendererFrame(
                            "ambient-occlusion denoising");
                        return;
                    }
                    if (diffuseGiDenoisingSelected &&
                        (!diffuseGiDenoisingInvoked ||
                            diffuseGiDenoisingFailed))
                    {
                        FailOpenRendererFrame("diffuse-GI denoising");
                        return;
                    }
                }
                else
                {
                    if (m_ScreenSpaceVisibilityPass)
                        m_ScreenSpaceVisibilityPass->Deactivate();
                }
            }
        }

        }

#if defined(UVSR_BUILD_TESTING)
        m_ScreenSpaceVisibilityDispatchedThisFrame =
            screenSpaceVisibilityResult.dispatched;
#endif
        if ((directionalRayVisibilityExpectedToContribute &&
                !directionalVisibilityResult.dispatched) ||
            (rayTracedFlashlightShadowExpectedToContribute &&
                !flashlightShadowResult.dispatched) ||
            (rayTracedSkyVisibilityExpectedToContribute &&
                !skyVisibilityResult.dispatched))
        {
            FailOpenRendererFrame("expected ray visibility producer");
            return;
        }
        const bool expectedProducersCompleted =
            uvsr::RendererProducerDispatchContract{
                screenSpaceVisibilityRequested,
                screenSpaceVisibilityResult.dispatched,
                directionalRayVisibilityExpectedToContribute,
                directionalVisibilityResult.dispatched,
                rayTracedFlashlightShadowExpectedToContribute,
                flashlightShadowResult.dispatched,
                rayTracedSkyVisibilityExpectedToContribute,
                skyVisibilityResult.dispatched
            }.IsComplete();
        if (lightingSampleSchedulePrepared &&
            lightingSampleSchedule.enabled &&
            !expectedProducersCompleted)
        {
            FailOpenRendererFrame(
                "lighting accumulation producer transaction");
            return;
        }

        if (m_MaterialPickPurpose != MaterialPickPurpose::None &&
            m_MaterialPickScene != m_Scene.get())
        {
            m_MaterialPickPurpose = MaterialPickPurpose::None;
            m_MaterialPickScene = nullptr;
            m_ui.ShowMaterialDrawer = false;
        }
        if (m_MaterialPickPurpose ==
            MaterialPickPurpose::RefreshMaterialDrawerSelection)
        {
            const CenterMaterialPick centerPick =
                ResolveCenterMaterialPick(
                    presentationSize.x,
                    presentationSize.y);
            if (centerPick.valid)
            {
                m_PickPosition =
                    uint2(centerPick.x, centerPick.y);
            }
            else
            {
                m_MaterialPickPurpose = MaterialPickPurpose::None;
                m_MaterialPickScene = nullptr;
            }
        }
        if (m_MaterialPickPurpose != MaterialPickPurpose::None)
        {
            if (!m_MaterialIdGeometryPass ||
                !m_RenderTargets->MaterialIDs ||
                !m_RenderTargets->MaterialIDDepth ||
                !m_RenderTargets->MaterialIDFramebuffer ||
                !m_PixelReadback)
            {
                FailOpenRendererFrame("material picking");
                return;
            }
            BeginRendererStage(RendererTimingStage::MaterialPicking);
            m_CommandList->clearTextureUInt(
                m_RenderTargets->MaterialIDs,
                nvrhi::AllSubresources, 0xffffu);
            if (m_RenderTargets->MaterialIDDepth !=
                m_RenderTargets->Depth)
            {
                const nvrhi::FormatInfo& depthInfo =
                    nvrhi::getFormatInfo(
                        m_RenderTargets->MaterialIDDepth
                            ->getDesc().format);
                m_CommandList->clearDepthStencilTexture(
                    m_RenderTargets->MaterialIDDepth,
                    nvrhi::AllSubresources,
                    true,
                    0.f,
                    depthInfo.hasStencil,
                    0u);
            }

            if (!RenderGeometry(
                    *m_MaterialIdGeometryPass,
                    m_RenderTargets->MaterialIDFramebuffer.Get(),
                    m_View.get(),
                    m_View.get(),
                    "MaterialID"))
            {
                EndRendererStage(
                    RendererTimingStage::MaterialPicking);
                throw std::runtime_error(
                    "UVSR material-ID rendering failed");
            }

            const uint32_t presentationResolutionScale =
                m_RenderTargets->GetPresentationResolutionScale();
            const nvrhi::TextureDesc& materialIdDescription =
                m_RenderTargets->MaterialIDs->getDesc();
            const uint32_t pickX = std::min(
                m_PickPosition.x * presentationResolutionScale +
                    presentationResolutionScale / 2u,
                materialIdDescription.width - 1u);
            const uint32_t pickY = std::min(
                m_PickPosition.y * presentationResolutionScale +
                    presentationResolutionScale / 2u,
                materialIdDescription.height - 1u);
            if (!m_PixelReadback->Capture(
                    m_CommandList,
                    pickX,
                    pickY))
            {
                uvsr::log::error("Material readback capture failed");
                m_MaterialPickPurpose = MaterialPickPurpose::None;
                m_MaterialPickScene = nullptr;
                m_ui.SelectedMaterial = nullptr;
                m_ui.SelectedNode = nullptr;
            }
            EndRendererStage(RendererTimingStage::MaterialPicking);
        }

        if (environmentBackgroundSelected)
        {
            if (!m_ImageBasedLightingBackgroundPass)
            {
                FailOpenRendererFrame("environment background");
                return;
            }
            BeginRendererStage(RendererTimingStage::EnvironmentBackground);
            const ImageBasedLightingBackgroundRenderResult backgroundResult =
                m_ImageBasedLightingBackgroundPass->Render(
                    m_CommandList,
                    *m_View,
                    m_ImageBasedLightingEnvironment->GetRadianceScale());
            EndRendererStage(RendererTimingStage::EnvironmentBackground);
            if (!backgroundResult.Succeeded())
            {
                FailOpenRendererFrame("environment background");
                return;
            }
        }

        // A selected path tracer never silently presents raster lighting.
        // During hierarchy preparation or an explicit failed/unselected frame,
        // HdrColor remains the cleared transport target.
        nvrhi::ITexture* sceneColor = pathTracingResult
            ? pathTracingResult.sceneLinearDisplay
            : m_RenderTargets->HdrColor.Get();
        const bool renderDeferredMsaaLighting =
            !pathTracingSelected &&
            deferredMsaaLightingPending &&
            m_PbrDeferredLightingPass &&
            m_RenderTargets->ResolvedHdrColor &&
            m_RenderTargets->DeferredMsaaColor;
        if (!pathTracingSelected && deferredMsaaLightingPending &&
            !renderDeferredMsaaLighting)
        {
            FailOpenRendererFrame("multisample deferred lighting resources");
            return;
        }
        if (renderDeferredMsaaLighting)
            BeginRendererStage(RendererTimingStage::DirectLighting);
        if (!pathTracingSelected &&
            m_RenderTargets->GetSampleCount() > 1u)
        {
            if (!m_RenderTargets->ResolvedHdrColor ||
                !m_RenderTargets->HdrColor)
            {
                FailOpenRendererFrame("multisample color resolve");
                return;
            }
            m_CommandList->resolveTexture(
                m_RenderTargets->ResolvedHdrColor,
                nvrhi::AllSubresources,
                m_RenderTargets->HdrColor,
                nvrhi::AllSubresources);
            sceneColor = m_RenderTargets->ResolvedHdrColor;
        }
        if (renderDeferredMsaaLighting)
        {
            // Include the required multisample color resolve and final
            // per-sample material/lighting evaluation in the direct-lighting
            // envelope. The earlier closest-surface pass remains separately
            // attributed because other effects can share it.
            const PbrDeferredLightingRenderResult msaaLightingResult =
                m_PbrDeferredLightingPass->Render(
                m_CommandList,
                *m_View,
                deferredMsaaInputs,
                directLightVisibilities,
                submittedFlashlight,
                flashlightBeamProfile,
                globalEnvironment,
                skyVisibility,
                rawClosestSkyVisibility,
                visibilitySkyVisibility,
                skyVisibilityReceiverSampleCount,
                applySkyVisibilityToDiffuseIbl,
                applySkyVisibilityToSpecularIbl,
                nullptr,
                deferredMsaaVisibilityPending,
                false,
                uint32_t(m_ui.LightingDebugView),
                uint32_t(m_ui.ScreenSpaceVisibility.debugView),
                float2(0.f),
                m_RenderTargets->ResolvedHdrColor,
                m_RenderTargets->GetSampleCount(),
                deferredMsaaVisibilityPending
                    ? m_RenderTargets->BaseLighting.Get()
                    : nullptr,
                deferredMsaaVisibilityPending
                    ? m_RenderTargets
                          ->VisibilityComposite.Get()
                    : nullptr);
            EndRendererStage(RendererTimingStage::DirectLighting);
            if (!msaaLightingResult.Succeeded())
            {
                FailOpenRendererFrame("multisample deferred lighting");
                return;
            }
            sceneColor =
                m_RenderTargets->DeferredMsaaColor;
        }

        nvrhi::ITexture* accumulatedSceneColor = sceneColor;
        if (lightingSampleSchedulePrepared &&
            m_LightingAccumulationPass)
        {
            // Accumulation is the only long-term history owner in this mode.
            // It consumes the raw scene-linear frame, never TAA's already
            // temporal/clipped output, so every accepted sample has equal and
            // unbiased ownership in the progressive mean.
            const LightingAccumulationResult accumulationResult =
                m_LightingAccumulationPass->Resolve(
                    m_CommandList,
                    sceneColor,
                    lightingSampleSchedule);
            if (!accumulationResult)
            {
                FailOpenRendererFrame("lighting accumulation commit");
                return;
            }
            accumulatedSceneColor = accumulationResult.sceneLinear;
            lightingSampleSchedulePrepared = false;
#if defined(UVSR_BUILD_TESTING)
            m_LightingAccumulationCommittedThisFrame =
                accumulationResult.committed;
#endif
        }

        nvrhi::ITexture* antiAliasedTexture = accumulatedSceneColor;
        if (temporalAaWillRender)
        {
            TemporalAAPass* temporalPass = m_TemporalAAPass.get();
            antiAliasedTexture = temporalPass->Render(
                m_CommandList,
                *m_View,
                m_PreviousView.get(),
                m_AntiAliasingPhase,
                antiAliasing,
                temporalSharpenEnabled &&
                    !deferTemporalSharpenToPresentation,
                deferTemporalSharpenToPresentation,
                m_ui.TemporalAaSharpness);
            temporalAaRenderedThisFrame =
                temporalPass->DidRenderThisFrame();
            if (!antiAliasedTexture || !temporalAaRenderedThisFrame)
            {
                FailOpenRendererFrame("temporal anti-aliasing");
                return;
            }

            if (temporalAaRenderedThisFrame &&
                deferTemporalSharpenToPresentation)
            {
                // Keep the resolved sharpen separate from compact history,
                // then let display mapping and spatial AA observe its final
                // edges.
                antiAliasedTexture = temporalPass->SharpenPresentation(
                    m_CommandList,
                    antiAliasedTexture);
                if (!antiAliasedTexture)
                {
                    FailOpenRendererFrame(
                        "temporal anti-aliasing presentation sharpen");
                    return;
                }
            }

        }

        PlanarView presentationView;
        const ICompositeView* postProcessingView = m_View.get();
        if (m_RenderTargets->GetPresentationResolutionScale() > 1u)
        {
            if (!antiAliasedTexture || !m_RendererCommonPasses ||
                !m_RenderTargets->PresentationColor ||
                !m_RenderTargets->PresentationFramebuffer)
            {
                FailOpenRendererFrame("composite multisample resolve");
                return;
            }
            m_CommandList->beginMarker("Composite 16x MSAA Resolve");
            const bool resolved = m_RendererCommonPasses->BlitTexture(
                m_CommandList,
                m_RenderTargets->PresentationFramebuffer,
                antiAliasedTexture);
            m_CommandList->endMarker();
            if (!resolved)
            {
                FailOpenRendererFrame("composite multisample resolve");
                return;
            }
            antiAliasedTexture = m_RenderTargets->PresentationColor;
            const DirectX::XMUINT2 resolvedSize =
                m_RenderTargets->GetPresentationSize();
            presentationView.SetViewport(nvrhi::Viewport(
                float(resolvedSize.x),
                float(resolvedSize.y)));
            presentationView.UpdateCache();
            postProcessingView = &presentationView;
        }

        const bool diagnosticExposureView =
            !pathTracingSelected && rayMarchingDiagnostic;
        const bool autoExposureExpected =
            m_ui.AutoExposure.enabled && !diagnosticExposureView;
        if (autoExposureExpected)
            BeginRendererStage(RendererTimingStage::AutoExposure);
        nvrhi::IBuffer* autoExposureBuffer = nullptr;
        if (autoExposureExpected && m_AutoExposurePass &&
            m_AutoExposurePass->IsAvailable())
        {
            autoExposureBuffer = m_AutoExposurePass->Render(
                m_CommandList,
                *postProcessingView,
                antiAliasedTexture,
                m_ui.AutoExposure,
                m_FrameDeltaSeconds,
                false);
        }
        else if (m_AutoExposurePass)
        {
            // Disabled and diagnostic frames select the texture-only AgX
            // permutation. Its math is the exact pre-Auto-Exposure path.
            m_AutoExposurePass->Reset();
        }
        if (autoExposureExpected)
            EndRendererStage(RendererTimingStage::AutoExposure);
        m_AutoExposureDispatchedThisFrame =
            autoExposureExpected && m_AutoExposurePass &&
            m_AutoExposurePass->DidDispatchThisFrame();
        if (autoExposureExpected &&
            (!autoExposureBuffer || !m_AutoExposureDispatchedThisFrame))
        {
            FailOpenRendererFrame("auto exposure");
            return;
        }
#if defined(UVSR_BUILD_TESTING)
        if (m_RuntimeOutputCaptureRequested && antiAliasedTexture)
        {
            const nvrhi::TextureDesc& sourceDesc =
                antiAliasedTexture->getDesc();
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
                bool recreate = !m_RuntimeLinearReadback;
                if (m_RuntimeLinearReadback)
                {
                    const nvrhi::TextureDesc& currentDesc =
                        m_RuntimeLinearReadback->getDesc();
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
                    m_RuntimeLinearReadback = GetDevice()->createStagingTexture(
                        stagingDesc,
                        nvrhi::CpuAccessMode::Read);
                    if (!m_RuntimeLinearReadback)
                    {
                        uvsr::log::error(
                            "Runtime linear readback staging creation failed "
                            "for format %u at %ux%u",
                            static_cast<unsigned int>(sourceDesc.format),
                            sourceDesc.width,
                            sourceDesc.height);
                    }
                }
                if (m_RuntimeLinearReadback)
                {
                    m_CommandList->copyTexture(
                        m_RuntimeLinearReadback,
                        nvrhi::TextureSlice{},
                        antiAliasedTexture,
                        nvrhi::TextureSlice{});
                    m_RuntimeLinearReadbackQueued = true;
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
        nvrhi::ITexture* displayTexture = antiAliasedTexture;
        BeginRendererStage(RendererTimingStage::ToneMapping);
        const bool toneMapped = m_AgxToneMappingPass &&
            m_AgxToneMappingPass->Render(
                m_CommandList,
                *postProcessingView,
                antiAliasedTexture,
                autoExposureBuffer);
        EndRendererStage(RendererTimingStage::ToneMapping);
        if (!toneMapped)
        {
            FailOpenRendererFrame("AgX tone mapping");
            return;
        }
        displayTexture = m_RenderTargets->LdrColor;

        if (antiAliasing.fastApproximateEnabled)
        {
            if (!m_FastApproximateAAPass ||
                !m_FastApproximateAAPass->IsValid())
            {
                FailOpenRendererFrame("fast approximate anti-aliasing");
                return;
            }
            BeginRendererStage(RendererTimingStage::FastApproximate);
            displayTexture = m_FastApproximateAAPass->Render(
                m_CommandList,
                *postProcessingView,
                displayTexture,
                antiAliasing);
            EndRendererStage(RendererTimingStage::FastApproximate);
            if (!displayTexture)
            {
                FailOpenRendererFrame("fast approximate anti-aliasing");
                return;
            }
        }

        BeginRendererStage(RendererTimingStage::OutputBlit);
        const bool outputProduced = m_AgxToneMappingPass &&
            m_AgxToneMappingPass->RenderOutput(
                m_CommandList,
                *postProcessingView,
                framebuffer,
                displayTexture);
        EndRendererStage(RendererTimingStage::OutputBlit);
        if (!outputProduced)
        {
            FailOpenRendererFrame("output blit");
            return;
        }
        EndRendererStage(RendererTimingStage::CompleteFrame);
        CompleteRendererTimerFrame();

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        if (pathTracingSelected && m_PathTracingPass)
            m_PathTracingPass->SubmitAcceptedSampleReadback();
        if (m_RenderTargets->MotionVectorsEnabled || pathTracingSelected)
            CaptureCurrentViewForMotionVectors();
        if (temporalAaRenderedThisFrame)
        {
            ++m_AntiAliasingPhase;
        }
        if (flashlightShadowResult.dispatched &&
            flashlightShadowResult.stochastic &&
            flashlightNoiseSettings.animate)
        {
            ++m_RayTracedFlashlightShadowPhase;
        }
        if (skyVisibilityResult.dispatched &&
            skyNoiseSettings.animate)
        {
            ++m_RayTracedSkyVisibilityPhase;
        }
        if (screenSpaceVisibilityResult.dispatched &&
            visibilityNoiseSettings.animate)
        {
            ++m_ScreenSpaceVisibilityPhase;
        }

        if (m_ui.CopyScreenshotToClipboard)
        {
            const std::filesystem::path screenshotPath = std::filesystem::temp_directory_path()
                / ("uvsr_screenshot_" + std::to_string(GetCurrentProcessId()) + ".bmp");
            const bool saved = uvsr::SaveRendererTextureBmp(
                GetDevice(),
                m_RendererCommonPasses.get(),
                framebufferTexture,
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
        if (m_RuntimeOutputCaptureRequested)
        {
            const std::filesystem::path capturePath =
                m_RuntimeOutputCapturePath;
            std::error_code directoryError;
            std::filesystem::create_directories(
                capturePath.parent_path(), directoryError);
            const bool captured = uvsr::SaveRendererTextureBmp(
                GetDevice(),
                m_RendererCommonPasses.get(),
                framebufferTexture,
                nvrhi::ResourceStates::RenderTarget,
                capturePath);

            RuntimeOutputEvidence evidence;
            if (m_RuntimeLinearReadbackQueued && m_RuntimeLinearReadback)
            {
                size_t rowPitch = 0u;
                const void* pixels = GetDevice()->mapStagingTexture(
                    m_RuntimeLinearReadback,
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
                    m_RuntimeLinearReadback->getDesc();
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
                    GetDevice()->unmapStagingTexture(m_RuntimeLinearReadback);
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
            m_RuntimeOutputEvidence = evidence;
            m_RuntimeOutputCaptureRequested = false;
        }
#endif

        if (m_MaterialPickPurpose != MaterialPickPurpose::None)
        {
            const MaterialPickPurpose completedPurpose =
                m_MaterialPickPurpose;
            const Scene* completedScene = m_MaterialPickScene;
            m_MaterialPickPurpose = MaterialPickPurpose::None;
            m_MaterialPickScene = nullptr;
            const std::optional<uvsr::RendererReadbackUint4> pixelValue =
                m_PixelReadback->ReadUInts();
            m_ui.SelectedMaterial = nullptr;
            m_ui.SelectedNode = nullptr;

            const bool completedForCurrentScene =
                pixelValue && completedScene == m_Scene.get();
            if (!pixelValue)
                uvsr::log::error("Material readback result was unavailable");
            if (completedForCurrentScene)
            {
                for (const auto& material :
                    m_Scene->GetSceneGraph()->GetMaterials())
                {
                    if (material->materialID == int(pixelValue->x))
                    {
                        m_ui.SelectedMaterial = material;
                        break;
                    }
                }

                for (const auto& instance :
                    m_Scene->GetSceneGraph()->GetMeshInstances())
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
                        m_Scene->GetSceneGraph()->GetRootNode());
                }
            }
        }
    }

auto UvsrSceneViewer::GetShaderFactory() -> std::shared_ptr<ShaderFactory> {
        return m_ShaderFactory;
    }

auto UvsrSceneViewer::GetRendererShaderFactory() -> std::shared_ptr<uvsr::RendererShaderFactory> {
        return m_RendererShaderFactory;
    }

auto UvsrSceneViewer::GetRendererCommonPasses() -> std::shared_ptr<uvsr::RendererCommonPasses> {
        return m_RendererCommonPasses;
    }

auto UvsrSceneViewer::GetActiveRasterSampleCount() const -> uint32_t {
        return m_RenderTargets && m_RenderTargets->IsValid()
            ? m_RenderTargets->GetPresentationSampleCount()
            : 1u;
    }

auto UvsrSceneViewer::GetTemporalAATimings() const -> const TemporalAATimings* {
        return m_TemporalAAPass
            ? &m_TemporalAAPass->GetTimings()
            : nullptr;
    }

auto UvsrSceneViewer::GetSubmittedMainViewTriangles() const -> uint64_t {
        return m_SubmittedMainViewTriangles;
    }

auto UvsrSceneViewer::GetRendererTimings() const -> const RendererTimings& {
        return m_RendererTimings;
    }

#if defined(UVSR_BUILD_TESTING)
auto UvsrSceneViewer::DidSubmitFlashlightLightingThisFrame() const -> bool {
        return m_FlashlightLightingSubmittedThisFrame;
    }
#endif

auto UvsrSceneViewer::IsRendererStageActiveThisFrame(
        RendererTimingStage stage) const -> bool {
        switch (stage)
        {
        case RendererTimingStage::PathTransport:
            return m_PathTransportDispatchedThisFrame;
        case RendererTimingStage::Geometry:
        case RendererTimingStage::MultisampleResolve:
        case RendererTimingStage::DirectLighting:
        case RendererTimingStage::ScreenSpaceVisibility:
        case RendererTimingStage::EnvironmentBackground:
            return m_ui.Lighting == LightingSolution::RayMarching;
        case RendererTimingStage::ShadowRayDispatch:
            return m_DirectionalRayVisibilityDispatchedThisFrame ||
                m_RayTracedFlashlightShadowDispatchedThisFrame;
        case RendererTimingStage::ShadowDenoise:
            return m_ShadowDenoisingDispatchedThisFrame;
        case RendererTimingStage::SkyVisibilityRayDispatch:
            return m_RayTracedSkyVisibilityDispatchedThisFrame;
        case RendererTimingStage::SkyVisibilityDenoise:
            return m_RayTracedSkyVisibilityDenoisedThisFrame;
        case RendererTimingStage::AmbientOcclusionDenoise:
            return m_AmbientOcclusionDenoisedThisFrame;
        case RendererTimingStage::DiffuseIlluminationDenoise:
            return m_DiffuseIlluminationDenoisedThisFrame;
        case RendererTimingStage::AutoExposure:
            return m_AutoExposureDispatchedThisFrame;
        case RendererTimingStage::VisibilityLightingPreparation:
            return m_VisibilityLightingPreparationDispatchedThisFrame;
        default:
            return true;
        }
    }

auto UvsrSceneViewer::AdvanceRendererTimers() -> void {
    const uint32_t slot =
        m_RendererTimerFrame % c_RendererTimerLatency;
    m_RendererTimerFrameWritable = true;
    m_RendererTimerActive.fill(false);

    for (size_t stageIndex = 0u;
        stageIndex < static_cast<size_t>(RendererTimingStage::Count);
        ++stageIndex)
    {
        if (!m_RendererTimerPending[stageIndex][slot])
        {
            m_RendererTimings.available[stageIndex] = false;
            continue;
        }

        nvrhi::ITimerQuery* query =
            m_RendererTimerQueries[stageIndex][slot];
        if (!GetDevice()->pollTimerQuery(query))
        {
            m_RendererTimerFrameWritable = false;
            continue;
        }

        const bool currentEpoch =
            m_RendererTimerPendingEpoch[stageIndex][slot] ==
                m_RendererTimerStageEpoch[stageIndex];
        if (currentEpoch)
        {
            m_RendererTimings.milliseconds[stageIndex] =
                GetDevice()->getTimerQueryTime(query) * 1000.f;
        }
        m_RendererTimings.available[stageIndex] = currentEpoch;
        GetDevice()->resetTimerQuery(query);
        m_RendererTimerPending[stageIndex][slot] = false;
    }
}

auto UvsrSceneViewer::BeginRendererStage(RendererTimingStage stage) -> void {
    if (!m_RendererTimerFrameWritable)
        return;

    const size_t stageIndex = static_cast<size_t>(stage);
    const uint32_t slot =
        m_RendererTimerFrame % c_RendererTimerLatency;
    if (m_RendererTimerPending[stageIndex][slot])
        return;

    m_CommandList->beginTimerQuery(
        m_RendererTimerQueries[stageIndex][slot]);
    m_RendererTimerActive[stageIndex] = true;
}

auto UvsrSceneViewer::EndRendererStage(RendererTimingStage stage) -> void {
    const size_t stageIndex = static_cast<size_t>(stage);
    if (!m_RendererTimerActive[stageIndex])
        return;

    const uint32_t slot =
        m_RendererTimerFrame % c_RendererTimerLatency;
    m_CommandList->endTimerQuery(
        m_RendererTimerQueries[stageIndex][slot]);
    m_RendererTimerPending[stageIndex][slot] = true;
    m_RendererTimerPendingEpoch[stageIndex][slot] =
        m_RendererTimerStageEpoch[stageIndex];
    m_RendererTimerActive[stageIndex] = false;
}

auto UvsrSceneViewer::CompleteRendererTimerFrame() -> void {
    if (m_RendererTimerFrameWritable)
        ++m_RendererTimerFrame;
}

auto UvsrSceneViewer::InvalidateRendererStageTiming(
    RendererTimingStage stage) -> void {
    const size_t stageIndex = static_cast<size_t>(stage);
    ++m_RendererTimerStageEpoch[stageIndex];
    m_RendererTimings.available[stageIndex] = false;
}
