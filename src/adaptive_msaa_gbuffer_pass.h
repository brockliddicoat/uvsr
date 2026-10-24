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
// Adapted from donut/include/donut/render/GBufferFillPass.h at pinned Donut
// commit bc1ea24b0486f1c00d89327fe16c0b4dd11c5937 for UVSR source base
// 578496047c4753077e36f39a68df8c2c323cee58. This sibling leaves Donut's
// ordinary G-buffer pass untouched and adds only the adaptive raster identity
// binding and push state.

#pragma once

#include "adaptive_msaa_raster_cb.h"

#include <donut/render/GBufferFillPass.h>

#include <cstdint>
#include <memory>

namespace uvsr
{
    class AdaptiveMsaaGBufferFillPass final
        : public donut::render::GBufferFillPass
    {
    public:
        static constexpr uint32_t InvalidGeometryLocalIndex = 0xffffffffu;

        class Context final : public donut::render::GBufferFillPass::Context
        {
        public:
            uint32_t geometryLocalIndex = InvalidGeometryLocalIndex;
            uint32_t visibilityClass =
                ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND;
        };

        AdaptiveMsaaGBufferFillPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses,
            nvrhi::IBuffer* geometryInstanceHandleMap,
            nvrhi::ITexture* diagnosticSurfaceKeys,
            bool surfaceHitOnly,
            bool whiteWorld,
            bool compactFallbackGBuffer = false);

        void Init(
            donut::engine::ShaderFactory& shaderFactory,
            const CreateParameters& params) override;

        // Replacing the global map invalidates every per-BufferGroup binding
        // set because t12 is part of each such set. The first map, supplied to
        // the constructor, exists before any binding set can be created.
        void SetGeometryInstanceHandleMap(
            nvrhi::IBuffer* geometryInstanceHandleMap);

        [[nodiscard]] nvrhi::IBuffer* GetGeometryInstanceHandleMap() const;
        [[nodiscard]] bool HasValidGeometryInstanceHandleMap() const;
        [[nodiscard]] uint64_t PreparationEpoch() const;

        bool SetupMaterial(
            donut::render::GeometryPassContext& context,
            const donut::engine::Material* material,
            nvrhi::RasterCullMode cullMode,
            nvrhi::GraphicsState& state) override;

        void SetupInputBuffers(
            donut::render::GeometryPassContext& context,
            const donut::engine::BufferGroup* buffers,
            nvrhi::GraphicsState& state) override;

        void SetGeometryLocalIndex(
            Context& context,
            uint32_t geometryLocalIndex) const;
        void SetVisibilityClass(
            Context& context,
            uint32_t visibilityClass) const;

        void SetPushConstants(
            donut::render::GeometryPassContext& context,
            nvrhi::ICommandList* commandList,
            nvrhi::GraphicsState& state,
            nvrhi::DrawArguments& args) override;

    protected:
        nvrhi::ShaderHandle CreateVertexShader(
            donut::engine::ShaderFactory& shaderFactory,
            const CreateParameters& params) override;

        nvrhi::ShaderHandle CreatePixelShader(
            donut::engine::ShaderFactory& shaderFactory,
            const CreateParameters& params,
            bool alphaTested) override;

        nvrhi::BindingLayoutHandle CreateInputBindingLayout() override;

        nvrhi::BindingSetHandle CreateInputBindingSet(
            const donut::engine::BufferGroup* bufferGroup) override;

        nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(
            PipelineKey key,
            const nvrhi::FramebufferInfo& framebufferInfo) override;

    private:
        void AdvancePreparationEpoch();

        nvrhi::BufferHandle m_GeometryInstanceHandleMap;
        nvrhi::TextureHandle m_DiagnosticSurfaceKeys;
        bool m_SurfaceHitOnly = false;
        bool m_WhiteWorld = false;
        bool m_CompactFallbackGBuffer = false;
        uint64_t m_PreparationEpoch = 1u;
        bool m_Initialized = false;
    };
}
