#include "renderer_geometry_passes_nvrhi.h"

#include "renderer_log.h"
#include "renderer_shader_factory_nvrhi.h"
#include "renderer_statistics.h"

#include <new>
#include <type_traits>
#include <utility>

namespace uvsr
{
    std::size_t RendererGeometryPass::BindingCache::FindSlot(
        const Entry* entries, std::size_t capacity, const void* key) noexcept
    {
        static_assert(sizeof(uintptr_t) <= sizeof(uint64_t));
        uint64_t hash = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(key));
        hash ^= hash >> 30;
        hash *= 0xbf58476d1ce4e5b9ull;
        hash ^= hash >> 27;
        hash *= 0x94d049bb133111ebull;
        hash ^= hash >> 31;
        std::size_t slot = std::size_t(hash) & (capacity - 1);
        while (entries[slot].key && entries[slot].key != key)
            slot = (slot + 1) & (capacity - 1);
        return slot;
    }

    RendererGeometryPass::BindingCache::~BindingCache() noexcept
    {
        delete[] m_Entries;
    }

    nvrhi::BindingSetHandle RendererGeometryPass::BindingCache::Find(
        const void* key) const noexcept
    {
        if (!key || !m_Capacity) return nullptr;
        const auto& entry = m_Entries[FindSlot(m_Entries, m_Capacity, key)];
        return entry.key ? entry.binding : nullptr;
    }

    bool RendererGeometryPass::BindingCache::Insert(
        const void* key, nvrhi::BindingSetHandle binding) noexcept
    {
        static_assert(std::is_nothrow_default_constructible_v<Entry>);
        static_assert(std::is_nothrow_move_assignable_v<Entry>);
        static_assert(std::is_nothrow_destructible_v<Entry>);
        if (!key || !binding) return false;
        if (m_Capacity && m_Entries[FindSlot(m_Entries, m_Capacity, key)].key)
            return true;
        if (!m_Capacity || m_Count == m_Capacity / 2)
        {
            constexpr std::size_t maximum = std::size_t(PTRDIFF_MAX) / sizeof(Entry);
            if (m_Capacity > maximum / 2) return false;
            const std::size_t capacity = m_Capacity ? m_Capacity * 2 : 8;
            if (capacity > maximum) return false;
            auto* candidate = new (std::nothrow) Entry[capacity];
            if (!candidate) return false;
            for (std::size_t index = 0; index < m_Capacity; ++index)
            {
                if (!m_Entries[index].key) continue;
                const std::size_t slot = FindSlot(candidate, capacity, m_Entries[index].key);
                candidate[slot] = std::move(m_Entries[index]);
            }
            delete[] m_Entries;
            m_Entries = candidate;
            m_Capacity = capacity;
        }
        m_Entries[FindSlot(m_Entries, m_Capacity, key)] = {key, std::move(binding)};
        ++m_Count;
        return true;
    }

    void RendererGeometryPass::BindingCache::Clear() noexcept
    {
        for (std::size_t index = 0; index < m_Capacity; ++index)
            m_Entries[index] = {};
        m_Count = 0;
    }

    RendererGeometryPass::RendererGeometryPass(
        nvrhi::IDevice* device,
        RendererShaderFactory* shaderFactory,
        nvrhi::ITexture* fallbackTexture,
        RendererGeometryPassDescription description)
        : m_Device(device)
        , m_FallbackTexture(fallbackTexture)
    {
        m_Initialization.device = device != nullptr;
        m_Initialization.shaderFactory = shaderFactory != nullptr;
        m_Initialization.fallbackTexture = fallbackTexture != nullptr;
        if (!device || !shaderFactory || !fallbackTexture)
            return;

        m_VertexShader = shaderFactory->CreateShader(
            "uvsr/renderer_gbuffer_vs.hlsl",
            "buffer_loads",
            {},
            nvrhi::ShaderType::Vertex);
        m_Initialization.vertexShader = bool(m_VertexShader);

        if (description.output == RendererGeometryOutput::Pbr)
        {
            const auto createPixelShader = [&](bool alphaTested)
            {
                const shader_blob::Constant macros[] = {
                    { "ALPHA_TESTED", alphaTested ? "1" : "0" },
                    { "WHITE_WORLD", description.whiteWorld ? "1" : "0" }
                };
                return shaderFactory->CreateShader(
                    "uvsr/pbr_gbuffer_ps.hlsl",
                    "main",
                    macros,
                    nvrhi::ShaderType::Pixel);
            };
            m_PixelShader = createPixelShader(false);
            m_AlphaTestedPixelShader = createPixelShader(true);
        }
        else
        {
            const auto createPixelShader = [&](bool alphaTested)
            {
                const shader_blob::Constant macros[] = {
                    { "ALPHA_TESTED", alphaTested ? "1" : "0" }
                };
                return shaderFactory->CreateShader(
                    "uvsr/material_id_ps.hlsl",
                    "main",
                    macros,
                    nvrhi::ShaderType::Pixel);
            };
            m_PixelShader = createPixelShader(false);
            m_AlphaTestedPixelShader = createPixelShader(true);
        }
        m_Initialization.pixelShader = bool(m_PixelShader);
        m_Initialization.alphaTestedPixelShader =
            bool(m_AlphaTestedPixelShader);

        nvrhi::BindingLayoutDesc materialLayoutDescription;
        materialLayoutDescription.visibility = nvrhi::ShaderType::Pixel;
        materialLayoutDescription.registerSpace =
            UVSR_GBUFFER_SPACE_MATERIAL;
        materialLayoutDescription.registerSpaceIsDescriptorSet = true;
        materialLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::ConstantBuffer(
                UVSR_GBUFFER_BINDING_MATERIAL_CONSTANTS),
            nvrhi::BindingLayoutItem::Texture_SRV(
                UVSR_GBUFFER_BINDING_MATERIAL_DIFFUSE_TEXTURE),
            nvrhi::BindingLayoutItem::Texture_SRV(
                UVSR_GBUFFER_BINDING_MATERIAL_SPECULAR_TEXTURE),
            nvrhi::BindingLayoutItem::Texture_SRV(
                UVSR_GBUFFER_BINDING_MATERIAL_NORMAL_TEXTURE),
            nvrhi::BindingLayoutItem::Texture_SRV(
                UVSR_GBUFFER_BINDING_MATERIAL_EMISSIVE_TEXTURE),
            nvrhi::BindingLayoutItem::Texture_SRV(
                UVSR_GBUFFER_BINDING_MATERIAL_OCCLUSION_TEXTURE),
            nvrhi::BindingLayoutItem::Texture_SRV(
                UVSR_GBUFFER_BINDING_MATERIAL_TRANSMISSION_TEXTURE),
            nvrhi::BindingLayoutItem::Texture_SRV(
                UVSR_GBUFFER_BINDING_MATERIAL_OPACITY_TEXTURE)
        };
        m_MaterialLayout =
            device->createBindingLayout(materialLayoutDescription);
        m_Initialization.materialLayout = bool(m_MaterialLayout);

        nvrhi::BindingLayoutDesc viewLayoutDescription;
        viewLayoutDescription.visibility =
            nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
        viewLayoutDescription.registerSpace = UVSR_GBUFFER_SPACE_VIEW;
        viewLayoutDescription.registerSpaceIsDescriptorSet = true;
        viewLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                UVSR_GBUFFER_BINDING_VIEW_CONSTANTS),
            nvrhi::BindingLayoutItem::Sampler(
                UVSR_GBUFFER_BINDING_MATERIAL_SAMPLER)
        };
        m_ViewLayout = device->createBindingLayout(viewLayoutDescription);
        m_Initialization.viewLayout = bool(m_ViewLayout);

        nvrhi::BindingLayoutDesc inputLayoutDescription;
        inputLayoutDescription.visibility =
            nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
        inputLayoutDescription.registerSpace = UVSR_GBUFFER_SPACE_INPUT;
        inputLayoutDescription.registerSpaceIsDescriptorSet = true;
        inputLayoutDescription.bindings = {
            nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                UVSR_GBUFFER_BINDING_INSTANCE_BUFFER),
            nvrhi::BindingLayoutItem::RawBuffer_SRV(
                UVSR_GBUFFER_BINDING_VERTEX_BUFFER),
            nvrhi::BindingLayoutItem::PushConstants(
                UVSR_GBUFFER_BINDING_PUSH_CONSTANTS,
                sizeof(GBufferPushConstants))
        };
        m_InputLayout = device->createBindingLayout(inputLayoutDescription);
        m_Initialization.inputLayout = bool(m_InputLayout);

        nvrhi::BufferDesc viewConstantBufferDescription;
        viewConstantBufferDescription.byteSize = sizeof(GBufferFillConstants);
        viewConstantBufferDescription.debugName =
            "RendererGeometryViewConstants";
        viewConstantBufferDescription.isConstantBuffer = true;
        viewConstantBufferDescription.isVolatile = true;
        viewConstantBufferDescription.maxVersions = 16u;
        m_ViewConstantBuffer =
            device->createBuffer(viewConstantBufferDescription);
        m_Initialization.viewConstantBuffer = bool(m_ViewConstantBuffer);

        m_MaterialSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
                .setMaxAnisotropy(16.f));
        m_Initialization.materialSampler = bool(m_MaterialSampler);

        if (m_ViewLayout && m_ViewConstantBuffer && m_MaterialSampler)
        {
            nvrhi::BindingSetDesc viewBindingDescription;
            viewBindingDescription.trackLiveness = true;
            viewBindingDescription.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    UVSR_GBUFFER_BINDING_VIEW_CONSTANTS,
                    m_ViewConstantBuffer),
                nvrhi::BindingSetItem::Sampler(
                    UVSR_GBUFFER_BINDING_MATERIAL_SAMPLER,
                    m_MaterialSampler)
            };
            m_ViewBindingSet = device->createBindingSet(
                viewBindingDescription, m_ViewLayout);
        }
        m_Initialization.viewBindingSet = bool(m_ViewBindingSet);

        if (!IsValid())
            log::error("Renderer geometry pass initialization failed");
    }

    RendererGeometryPass::~RendererGeometryPass() noexcept
    {
        delete[] m_Pipelines;
    }

    bool RendererGeometryPass::BeginView(
        nvrhi::ICommandList* commandList,
        const RendererGeometryView& view)
    {
        if (!IsValid() || !commandList || !view.framebuffer || m_CommandList)
            return false;

        m_CommandList = commandList;
        m_GraphicsState = {};
        m_GraphicsState.framebuffer = view.framebuffer;
        m_GraphicsState.viewport = view.viewport;
        m_GraphicsState.shadingRateState = view.shadingRate;
        m_PendingDraw = {};
        m_PendingDraw.instanceCount = 0u;
        m_CurrentMaterial = nullptr;
        m_CurrentBuffers = nullptr;
        m_CurrentCullMode = nvrhi::RasterCullMode::Back;
        m_FrontCounterClockwise = view.frontCounterClockwise;
        m_ReverseDepth = view.reverseDepth;
        m_StateValid = false;
        m_CurrentDrawEnabled = false;
        m_SubmittedTriangles = 0u;
        commandList->writeBuffer(
            m_ViewConstantBuffer,
            &view.constants,
            sizeof(view.constants));
        return true;
    }

    bool RendererGeometryPass::Submit(const RendererGeometryDraw& draw)
    {
        if (!m_CommandList || !draw.material || !draw.buffers ||
            draw.indexCount == 0u || draw.instanceCount == 0u)
        {
            return false;
        }

        const void* materialKey = draw.material->cacheKey;
        const void* bufferKey = draw.buffers->cacheKey;
        if (!materialKey || !bufferKey)
            return false;

        const bool newBuffers = bufferKey != m_CurrentBuffers;
        const bool newMaterial = materialKey != m_CurrentMaterial ||
            draw.cullMode != m_CurrentCullMode;
        if ((newBuffers || newMaterial) && !Flush())
            return false;

        if (newBuffers)
        {
            if (!ApplyBuffers(*draw.buffers))
            {
                m_CurrentDrawEnabled = false;
                return false;
            }
            m_CurrentBuffers = bufferKey;
            m_StateValid = false;
        }

        if (newMaterial)
        {
            const MaterialResult result = ApplyMaterial(*draw.material, draw.cullMode);
            m_CurrentDrawEnabled = result == MaterialResult::Applied;
            if (result == MaterialResult::Failed)
                return false;
            m_CurrentMaterial = materialKey;
            m_CurrentCullMode = draw.cullMode;
            m_StateValid = false;
        }

        if (!m_CurrentDrawEnabled)
            return true;

        if (!m_StateValid)
        {
            m_CommandList->setGraphicsState(m_GraphicsState);
            m_StateValid = true;
        }

        if (CanMergeRendererGeometryDraws(m_PendingDraw, draw))
        {
            m_PendingDraw.instanceCount += draw.instanceCount;
        }
        else
        {
            if (!Flush())
                return false;
            m_PendingDraw.vertexCount = draw.indexCount;
            m_PendingDraw.instanceCount = draw.instanceCount;
            m_PendingDraw.startIndexLocation = draw.startIndexLocation;
            m_PendingDraw.startVertexLocation = draw.startVertexLocation;
            m_PendingDraw.startInstanceLocation = draw.startInstanceLocation;
        }
        m_SubmittedTriangles += CountSubmittedTriangleListPrimitives(
            draw.indexCount, draw.instanceCount);
        return true;
    }

    bool RendererGeometryPass::EndView()
    {
        if (!m_CommandList)
            return false;
        const bool flushed = Flush();
        m_CommandList = nullptr;
        m_CurrentMaterial = nullptr;
        m_CurrentBuffers = nullptr;
        m_CurrentCullMode = nvrhi::RasterCullMode::Back;
        m_StateValid = false;
        m_CurrentDrawEnabled = false;
        return flushed;
    }

    void RendererGeometryPass::ResetBindingCache()
    {
        m_MaterialBindings.Clear();
        m_InputBindings.Clear();
        m_MaterialRevision = 0;
    }

    void RendererGeometryPass::SetMaterialRevision(uint64_t revision)
    {
        if (m_MaterialRevision == revision) return;
        m_MaterialBindings.Clear();
        m_MaterialRevision = revision;
    }

    nvrhi::BindingSetHandle RendererGeometryPass::GetMaterialBindingSet(
        const RendererGeometryMaterial& material)
    {
        if (!material.cacheKey || !material.constants)
            return nullptr;
        if (const auto binding = m_MaterialBindings.Find(material.cacheKey))
            return binding;

        nvrhi::BindingSetDesc description;
        description.trackLiveness = true;
        description.bindings.push_back(
            nvrhi::BindingSetItem::ConstantBuffer(
                UVSR_GBUFFER_BINDING_MATERIAL_CONSTANTS,
                material.constants, material.constantRange));
        for (std::uint32_t index = 0u;
            index < RendererGeometryMaterial::TextureCount;
            ++index)
        {
            description.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    index,
                    material.textures[index]
                        ? material.textures[index]
                        : m_FallbackTexture.Get()));
        }
        nvrhi::BindingSetHandle bindingSet =
            m_Device->createBindingSet(description, m_MaterialLayout);
        if (bindingSet && !m_MaterialBindings.Insert(material.cacheKey, bindingSet))
            return nullptr;
        return bindingSet;
    }

    nvrhi::BindingSetHandle RendererGeometryPass::GetInputBindingSet(
        const RendererGeometryBuffers& buffers)
    {
        if (!buffers.cacheKey || !buffers.vertexBuffer ||
            !buffers.instanceBuffer)
        {
            return nullptr;
        }
        if (const auto binding = m_InputBindings.Find(buffers.cacheKey))
            return binding;

        nvrhi::BindingSetDesc description;
        description.trackLiveness = true;
        description.bindings = {
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                UVSR_GBUFFER_BINDING_INSTANCE_BUFFER,
                buffers.instanceBuffer),
            nvrhi::BindingSetItem::RawBuffer_SRV(
                UVSR_GBUFFER_BINDING_VERTEX_BUFFER,
                buffers.vertexBuffer),
            nvrhi::BindingSetItem::PushConstants(
                UVSR_GBUFFER_BINDING_PUSH_CONSTANTS,
                sizeof(GBufferPushConstants))
        };
        nvrhi::BindingSetHandle bindingSet =
            m_Device->createBindingSet(description, m_InputLayout);
        if (bindingSet && !m_InputBindings.Insert(buffers.cacheKey, bindingSet))
            return nullptr;
        return bindingSet;
    }

    nvrhi::GraphicsPipelineHandle RendererGeometryPass::GetPipeline(
        PipelineKey key,
        const nvrhi::FramebufferInfo& framebufferInfo)
    {
        for (std::size_t index = 0; index < m_PipelineCount; ++index)
        {
            const auto& entry = m_Pipelines[index];
            if (entry.key == key && entry.framebufferInfo == framebufferInfo)
                return entry.pipeline;
        }

        nvrhi::GraphicsPipelineDesc description;
        description.VS = m_VertexShader;
        description.PS = key.alphaTested
            ? m_AlphaTestedPixelShader.Get()
            : m_PixelShader.Get();
        description.bindingLayouts = {
            m_MaterialLayout,
            m_ViewLayout,
            m_InputLayout
        };
        description.renderState.rasterState
            .setFrontCounterClockwise(key.frontCounterClockwise)
            .setCullMode(key.alphaTested
                ? nvrhi::RasterCullMode::None
                : key.cullMode);
        description.renderState.blendState.disableAlphaToCoverage();
        description.renderState.depthStencilState
            .setDepthWriteEnable(true)
            .setDepthFunc(key.reverseDepth
                ? nvrhi::ComparisonFunc::GreaterOrEqual
                : nvrhi::ComparisonFunc::LessOrEqual);
        nvrhi::GraphicsPipelineHandle pipeline =
            m_Device->createGraphicsPipeline(description, framebufferInfo);
        if (pipeline)
        {
            // pinned FramebufferInfo construction only initializes inline storage.
            // its static_vector constructor has no noexcept annotation.
            static_assert(std::is_nothrow_copy_constructible_v<nvrhi::FramebufferInfo>);
            static_assert(std::is_nothrow_move_assignable_v<PipelineEntry>);
            static_assert(std::is_nothrow_destructible_v<PipelineEntry>);
            constexpr std::size_t maximum = std::size_t(PTRDIFF_MAX) / sizeof(PipelineEntry);
            if (m_PipelineCount >= maximum) return nullptr;
            PipelineEntry incoming{key, framebufferInfo, pipeline};
            if (m_PipelineCount == m_PipelineCapacity)
            {
                const std::size_t capacity = !m_PipelineCapacity ? 1 :
                    m_PipelineCapacity <= maximum / 2 ? m_PipelineCapacity * 2 : maximum;
                auto* candidate = new (std::nothrow) PipelineEntry[capacity];
                if (!candidate) return nullptr;
                for (std::size_t index = 0; index < m_PipelineCount; ++index)
                    candidate[index] = std::move(m_Pipelines[index]);
                delete[] m_Pipelines;
                m_Pipelines = candidate;
                m_PipelineCapacity = capacity;
            }
            m_Pipelines[m_PipelineCount++] = std::move(incoming);
        }
        return pipeline;
    }

    bool RendererGeometryPass::ApplyBuffers(
        const RendererGeometryBuffers& buffers)
    {
        if (!buffers.indexBuffer)
            return false;
        nvrhi::BindingSetHandle inputBindingSet =
            GetInputBindingSet(buffers);
        if (!inputBindingSet)
            return false;

        m_GraphicsState.indexBuffer = {
            buffers.indexBuffer,
            nvrhi::Format::R32_UINT,
            0u
        };
        m_PushConstants.positionOffset = buffers.positionOffset;
        m_PushConstants.texCoordOffset = buffers.textureCoordinateOffset;
        m_PushConstants.normalOffset = buffers.normalOffset;
        m_PushConstants.tangentOffset = buffers.tangentOffset;

        if (m_GraphicsState.bindings.size() == 3u)
            m_GraphicsState.bindings[2] = inputBindingSet;
        else
            m_GraphicsState.bindings = { nullptr, m_ViewBindingSet, inputBindingSet };
        return true;
    }

    RendererGeometryPass::MaterialResult RendererGeometryPass::ApplyMaterial(
        const RendererGeometryMaterial& material,
        nvrhi::RasterCullMode cullMode)
    {
        const RendererMaterialRasterClass rasterClass =
            ClassifyRendererMaterialDomain(material.domain);
        if (rasterClass == RendererMaterialRasterClass::Rejected)
            return MaterialResult::Skipped;

        nvrhi::BindingSetHandle materialBindingSet =
            GetMaterialBindingSet(material);
        if (!materialBindingSet || !m_GraphicsState.framebuffer)
            return MaterialResult::Failed;

        const PipelineKey key = {
            cullMode,
            rasterClass == RendererMaterialRasterClass::AlphaTested,
            m_FrontCounterClockwise,
            m_ReverseDepth
        };
        nvrhi::GraphicsPipelineHandle pipeline = GetPipeline(
            key, m_GraphicsState.framebuffer->getFramebufferInfo());
        if (!pipeline)
            return MaterialResult::Failed;

        m_GraphicsState.pipeline = pipeline;
        if (m_GraphicsState.bindings.size() == 3u)
            m_GraphicsState.bindings[0] = materialBindingSet;
        else
            m_GraphicsState.bindings = {
                materialBindingSet, m_ViewBindingSet, nullptr };
        return m_GraphicsState.bindings[2] ? MaterialResult::Applied : MaterialResult::Failed;
    }

    bool RendererGeometryPass::Flush()
    {
        if (m_PendingDraw.instanceCount == 0u)
            return true;
        if (!m_CommandList || !m_CurrentDrawEnabled)
            return false;

        nvrhi::DrawArguments arguments = m_PendingDraw;
        m_PushConstants.startInstanceLocation =
            arguments.startInstanceLocation;
        m_PushConstants.startVertexLocation =
            arguments.startVertexLocation;
        m_CommandList->setPushConstants(
            &m_PushConstants, sizeof(m_PushConstants));
        arguments.startInstanceLocation = 0u;
        arguments.startVertexLocation = 0u;
        m_CommandList->drawIndexed(arguments);
        m_PendingDraw = {};
        m_PendingDraw.instanceCount = 0u;
        return true;
    }
}
