/*
* Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// UVSR modification and exact provenance:
// Adapted from donut/src/render/GBufferFillPass.cpp at pinned Donut commit
// bc1ea24b0486f1c00d89327fe16c0b4dd11c5937 for UVSR source base
// 578496047c4753077e36f39a68df8c2c323cee58. Donut remains unmodified; this
// first-party sibling retains its buffer-load flow and adds a lossless global
// geometry-instance handle map at t12.

#include "adaptive_msaa_gbuffer_pass.h"
#include "adaptive_msaa.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/MaterialBindingCache.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShaderFactory.h>

#include <limits>
#include <vector>

using namespace donut::math;
#include <donut/shaders/gbuffer_cb.h>
#include "adaptive_msaa_raster_cb.h"

namespace uvsr
{
    using donut::engine::BufferGroup;
    using donut::engine::CommonRenderPasses;
    using donut::engine::ShaderFactory;
    using donut::engine::ShaderMacro;

    static_assert(
        sizeof(AdaptiveMsaaGBufferPushConstants) == 36u,
        "Adaptive MSAA raster push constants must remain nine 32-bit words");
    static_assert(
        ADAPTIVE_MSAA_GEOMETRY_INSTANCE_SLOT_BITS ==
            adaptive_msaa::GeometryInstanceSlotBits &&
        ADAPTIVE_MSAA_GEOMETRY_INSTANCE_SLOT_MASK ==
            adaptive_msaa::GeometryInstanceSlotMask &&
        ADAPTIVE_MSAA_GEOMETRY_INSTANCE_GENERATION_MASK ==
            adaptive_msaa::GeometryInstanceGenerationMask &&
        ADAPTIVE_MSAA_GEOMETRY_INSTANCE_MIN_GENERATION ==
            adaptive_msaa::MinimumGeometryInstanceGeneration &&
        ADAPTIVE_MSAA_GEOMETRY_INSTANCE_MAX_GENERATION ==
            adaptive_msaa::MaximumGeometryInstanceGeneration,
        "Raster handle packing must match the shared SurfaceKey contract");

    namespace
    {
        constexpr uint32_t DiagnosticSurfaceKeysBinding = 13u;

        [[nodiscard]] bool IsValidGeometryInstanceHandleMap(
            const nvrhi::IBuffer* buffer)
        {
            if (!buffer)
                return false;

            const nvrhi::BufferDesc& desc = buffer->getDesc();
            return desc.byteSize >= sizeof(uint32_t) &&
                desc.structStride == sizeof(uint32_t) &&
                desc.byteSize % desc.structStride == 0u;
        }

        [[nodiscard]] bool IsValidDiagnosticSurfaceKeys(
            const nvrhi::ITexture* texture)
        {
            if (!texture)
                return false;

            const nvrhi::TextureDesc& desc = texture->getDesc();
            return desc.format == nvrhi::Format::RG32_UINT &&
                desc.dimension == nvrhi::TextureDimension::Texture2DMS &&
                desc.isShaderResource &&
                adaptive_msaa::IsSupportedSampleCount(desc.sampleCount) &&
                desc.width != 0u && desc.height != 0u &&
                desc.arraySize == 1u && desc.mipLevels == 1u;
        }
    }

    AdaptiveMsaaGBufferFillPass::AdaptiveMsaaGBufferFillPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        nvrhi::IBuffer* geometryInstanceHandleMap,
        nvrhi::ITexture* diagnosticSurfaceKeys,
        bool surfaceHitOnly,
        bool whiteWorld,
        bool compactFallbackGBuffer)
        : GBufferFillPass(device, commonPasses)
        , m_GeometryInstanceHandleMap(geometryInstanceHandleMap)
        , m_DiagnosticSurfaceKeys(diagnosticSurfaceKeys)
        , m_SurfaceHitOnly(surfaceHitOnly)
        , m_WhiteWorld(whiteWorld)
        , m_CompactFallbackGBuffer(compactFallbackGBuffer)
    {
    }

    void AdaptiveMsaaGBufferFillPass::Init(
        ShaderFactory& shaderFactory,
        const CreateParameters& params)
    {
        CreateParameters adaptiveParams = params;

        // The identity contract is defined only for Donut's buffer-load path.
        // UVSR uses planar DX12 views, and the pinned cubemap geometry shader
        // does not forward the additional nointerpolation semantic.
        adaptiveParams.useInputAssembler = false;
        adaptiveParams.enableSinglePassCubemap = false;

        GBufferFillPass::Init(shaderFactory, adaptiveParams);
        m_Initialized = true;
        AdvancePreparationEpoch();
    }

    void AdaptiveMsaaGBufferFillPass::SetGeometryInstanceHandleMap(
        nvrhi::IBuffer* geometryInstanceHandleMap)
    {
        if (m_GeometryInstanceHandleMap.Get() == geometryInstanceHandleMap)
            return;

        m_GeometryInstanceHandleMap = geometryInstanceHandleMap;

        // Before Init there cannot be cached material or input bindings. Every
        // replacement after Init must take the complete Donut reset path so no
        // set can retain the old t12 resource.
        if (m_Initialized)
            ResetBindingCache();
        else
            m_InputBindingSets.clear();
        AdvancePreparationEpoch();
    }

    nvrhi::IBuffer*
        AdaptiveMsaaGBufferFillPass::GetGeometryInstanceHandleMap() const
    {
        return m_GeometryInstanceHandleMap.Get();
    }

    bool AdaptiveMsaaGBufferFillPass::
        HasValidGeometryInstanceHandleMap() const
    {
        return IsValidGeometryInstanceHandleMap(
            m_GeometryInstanceHandleMap.Get());
    }

    uint64_t AdaptiveMsaaGBufferFillPass::PreparationEpoch() const
    {
        return m_PreparationEpoch;
    }

    void AdaptiveMsaaGBufferFillPass::AdvancePreparationEpoch()
    {
        if (m_PreparationEpoch == 0u)
            return;
        m_PreparationEpoch = m_PreparationEpoch ==
                std::numeric_limits<uint64_t>::max()
            ? 0u
            : m_PreparationEpoch + 1u;
    }

    bool AdaptiveMsaaGBufferFillPass::SetupMaterial(
        donut::render::GeometryPassContext& context,
        const donut::engine::Material* material,
        nvrhi::RasterCullMode cullMode,
        nvrhi::GraphicsState& state)
    {
        if (!HasValidGeometryInstanceHandleMap())
            return false;
        if (m_SurfaceHitOnly &&
            !IsValidDiagnosticSurfaceKeys(m_DiagnosticSurfaceKeys))
        {
            return false;
        }

        return GBufferFillPass::SetupMaterial(
            context,
            material,
            cullMode,
            state);
    }

    void AdaptiveMsaaGBufferFillPass::SetupInputBuffers(
        donut::render::GeometryPassContext& abstractContext,
        const donut::engine::BufferGroup* buffers,
        nvrhi::GraphicsState& state)
    {
        GBufferFillPass::SetupInputBuffers(
            abstractContext,
            buffers,
            state);

        // Static Donut BufferGroups do not allocate a PrevPosition stream.
        // Their previous object-space position is the current position; the
        // instance's previous transform still carries object motion. Make
        // that alias explicit instead of relying on Position being offset 0.
        if (buffers && !buffers->hasAttribute(
                donut::engine::VertexAttribute::PrevPosition))
        {
            auto& context = static_cast<Context&>(abstractContext);
            context.prevPositionOffset = context.positionOffset;
        }
    }

    void AdaptiveMsaaGBufferFillPass::SetGeometryLocalIndex(
        Context& context,
        uint32_t geometryLocalIndex) const
    {
        context.geometryLocalIndex = geometryLocalIndex;
    }

    void AdaptiveMsaaGBufferFillPass::SetVisibilityClass(
        Context& context,
        uint32_t visibilityClass) const
    {
        context.visibilityClass = visibilityClass;
    }

    nvrhi::ShaderHandle AdaptiveMsaaGBufferFillPass::CreateVertexShader(
        ShaderFactory& shaderFactory,
        const CreateParameters& params)
    {
        std::vector<ShaderMacro> macros;
        macros.emplace_back(
            "MOTION_VECTORS",
            params.enableMotionVectors ? "1" : "0");
        return shaderFactory.CreateShader(
            "uvsr/adaptive_msaa_gbuffer_vs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Vertex);
    }

    nvrhi::ShaderHandle AdaptiveMsaaGBufferFillPass::CreatePixelShader(
        ShaderFactory& shaderFactory,
        const CreateParameters& params,
        bool alphaTested)
    {
        std::vector<ShaderMacro> macros;
        if (m_SurfaceHitOnly)
        {
            return shaderFactory.CreateShader(
                "uvsr/adaptive_msaa_surface_hit_ps.hlsl",
                "main",
                nullptr,
                nvrhi::ShaderType::Pixel);
        }

        macros.emplace_back(
            "MOTION_VECTORS",
            params.enableMotionVectors ? "1" : "0");
        macros.emplace_back("ALPHA_TESTED", alphaTested ? "1" : "0");
        macros.emplace_back("WHITE_WORLD", m_WhiteWorld ? "1" : "0");
        macros.emplace_back(
            "COMPACT_FALLBACK_GBUFFER",
            m_CompactFallbackGBuffer ? "1" : "0");
        return shaderFactory.CreateShader(
            "uvsr/adaptive_msaa_gbuffer_ps.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Pixel);
    }

    nvrhi::BindingLayoutHandle
        AdaptiveMsaaGBufferFillPass::CreateInputBindingLayout()
    {
        auto bindingLayoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel)
            .setRegisterSpaceAndDescriptorSet(GBUFFER_SPACE_INPUT)
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                GBUFFER_BINDING_INSTANCE_BUFFER))
            .addItem(nvrhi::BindingLayoutItem::RawBuffer_SRV(
                GBUFFER_BINDING_VERTEX_BUFFER))
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                ADAPTIVE_MSAA_BINDING_GEOMETRY_INSTANCE_HANDLE_MAP))
            .addItem(nvrhi::BindingLayoutItem::PushConstants(
                GBUFFER_BINDING_PUSH_CONSTANTS,
                sizeof(AdaptiveMsaaGBufferPushConstants)));

        if (m_SurfaceHitOnly)
        {
            bindingLayoutDesc.addItem(
                nvrhi::BindingLayoutItem::Texture_SRV(
                    DiagnosticSurfaceKeysBinding));
        }

        return m_Device->createBindingLayout(bindingLayoutDesc);
    }

    nvrhi::BindingSetHandle
        AdaptiveMsaaGBufferFillPass::CreateInputBindingSet(
            const BufferGroup* bufferGroup)
    {
        if (!HasValidGeometryInstanceHandleMap())
            return nullptr;

        auto bindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(
                GBUFFER_BINDING_INSTANCE_BUFFER,
                bufferGroup->instanceBuffer))
            .addItem(nvrhi::BindingSetItem::RawBuffer_SRV(
                GBUFFER_BINDING_VERTEX_BUFFER,
                bufferGroup->vertexBuffer))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(
                ADAPTIVE_MSAA_BINDING_GEOMETRY_INSTANCE_HANDLE_MAP,
                m_GeometryInstanceHandleMap))
            .addItem(nvrhi::BindingSetItem::PushConstants(
                GBUFFER_BINDING_PUSH_CONSTANTS,
                sizeof(AdaptiveMsaaGBufferPushConstants)));

        if (m_SurfaceHitOnly)
        {
            if (!IsValidDiagnosticSurfaceKeys(m_DiagnosticSurfaceKeys))
                return nullptr;
            bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(
                DiagnosticSurfaceKeysBinding,
                m_DiagnosticSurfaceKeys));
        }

        return m_Device->createBindingSet(
            bindingSetDesc,
            m_InputBindingLayout);
    }

    nvrhi::GraphicsPipelineHandle
        AdaptiveMsaaGBufferFillPass::CreateGraphicsPipeline(
            PipelineKey key,
            const nvrhi::FramebufferInfo& framebufferInfo)
    {
        if (!m_SurfaceHitOnly)
            return GBufferFillPass::CreateGraphicsPipeline(
                key, framebufferInfo);

        // The diagnostic replay is both key-equal in the pixel shader and
        // depth-equal in fixed function. It never changes depth or color in
        // the accepted G-buffer attachments.
        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.inputLayout = m_InputLayout;
        pipelineDesc.VS = m_VertexShader;
        pipelineDesc.GS = m_GeometryShader;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.renderState.rasterState
            .setFrontCounterClockwise(key.bits.frontCounterClockwise)
            .setCullMode(key.bits.alphaTested
                ? nvrhi::RasterCullMode::None
                : key.bits.cullMode);
        pipelineDesc.renderState.blendState.disableAlphaToCoverage();
        pipelineDesc.renderState.depthStencilState
            .setDepthWriteEnable(false)
            .setDepthFunc(nvrhi::ComparisonFunc::Equal);
        pipelineDesc.bindingLayouts = {
            m_MaterialBindings->GetLayout(),
            m_ViewBindingLayout,
            m_InputBindingLayout
        };

        return m_Device->createGraphicsPipeline(
            pipelineDesc, framebufferInfo);
    }

    void AdaptiveMsaaGBufferFillPass::SetPushConstants(
        donut::render::GeometryPassContext& abstractContext,
        nvrhi::ICommandList* commandList,
        nvrhi::GraphicsState& state,
        nvrhi::DrawArguments& args)
    {
        (void)state;
        auto& context = static_cast<Context&>(abstractContext);

        AdaptiveMsaaGBufferPushConstants constants;
        constants.startInstanceLocation = args.startInstanceLocation;
        constants.startVertexLocation = args.startVertexLocation;
        constants.positionOffset = context.positionOffset;
        constants.prevPositionOffset = context.prevPositionOffset;
        constants.texCoordOffset = context.texCoordOffset;
        constants.normalOffset = context.normalOffset;
        constants.tangentOffset = context.tangentOffset;
        constants.geometryLocalIndex = context.geometryLocalIndex;
        constants.visibilityClass = context.visibilityClass;

        commandList->setPushConstants(&constants, sizeof(constants));

        // Geometry identity is draw-local. Reset immediately after every push
        // so an omitted assignment can only produce the explicit invalid key.
        context.geometryLocalIndex = InvalidGeometryLocalIndex;
        context.visibilityClass =
            ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND;

        args.startInstanceLocation = 0;
        args.startVertexLocation = 0;
    }
}
