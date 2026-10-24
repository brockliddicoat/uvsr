/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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
// Adapted from donut/include/donut/render/GeometryPasses.h at pinned Donut
// commit bc1ea24b0486f1c00d89327fe16c0b4dd11c5937 for UVSR source base
// 578496047c4753077e36f39a68df8c2c323cee58. These declarations are used only
// by adaptive modes; Reference continues to call Donut's original functions.

#pragma once

#include "adaptive_msaa_gbuffer_pass.h"

#include <donut/render/GeometryPasses.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace uvsr
{
    enum class AdaptiveMsaaRasterSupportClass : uint8_t
    {
        SurfaceKey,
        InvalidKeyFallback
    };

    // Diagnostic replay proof for one exact visible draw sequence. Pointer
    // identity is intentionally process-local: the two traces are compared
    // only within one render invocation, before their scene/view owners can
    // change. Equal aggregate triangle counts alone are not sufficient.
    struct AdaptiveMsaaRasterSubmissionRecord
    {
        const donut::engine::IView* view = nullptr;
        const void* mesh = nullptr;
        const void* geometry = nullptr;
        const void* material = nullptr;
        const void* buffers = nullptr;
        nvrhi::RasterCullMode cullMode = nvrhi::RasterCullMode::Back;
        AdaptiveMsaaRasterSupportClass supportClass =
            AdaptiveMsaaRasterSupportClass::InvalidKeyFallback;
        uint32_t geometryLocalIndex = 0xffffffffu;
        uint32_t vertexCount = 0u;
        uint32_t instanceCount = 0u;
        uint32_t startIndexLocation = 0u;
        uint32_t startVertexLocation = 0u;
        uint32_t startInstanceLocation = 0u;

        [[nodiscard]] bool Matches(
            const AdaptiveMsaaRasterSubmissionRecord& other) const
        {
            return view == other.view &&
                mesh == other.mesh && geometry == other.geometry &&
                material == other.material && buffers == other.buffers &&
                cullMode == other.cullMode &&
                supportClass == other.supportClass &&
                geometryLocalIndex == other.geometryLocalIndex &&
                vertexCount == other.vertexCount &&
                instanceCount == other.instanceCount &&
                startIndexLocation == other.startIndexLocation &&
                startVertexLocation == other.startVertexLocation &&
                startInstanceLocation == other.startInstanceLocation;
        }
    };

    struct AdaptiveMsaaRasterSubmissionTrace
    {
        uint64_t submittedTriangles = 0u;
        uint64_t submittedDraws = 0u;
        uint64_t sequenceHash = 14695981039346656037ull;
        std::vector<const donut::engine::IView*> views;
        std::vector<AdaptiveMsaaRasterSubmissionRecord> records;

        [[nodiscard]] bool Matches(
            const AdaptiveMsaaRasterSubmissionTrace& other) const
        {
            if (submittedTriangles != other.submittedTriangles ||
                submittedDraws != other.submittedDraws ||
                sequenceHash != other.sequenceHash ||
                views != other.views ||
                records.size() != other.records.size())
            {
                return false;
            }
            for (size_t index = 0u; index < records.size(); ++index)
            {
                if (!records[index].Matches(other.records[index]))
                    return false;
            }
            return true;
        }
    };

    [[nodiscard]] uint64_t RenderAdaptiveMsaaView(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView* view,
        const donut::engine::IView* viewPrev,
        nvrhi::IFramebuffer* framebuffer,
        donut::render::IDrawStrategy& drawStrategy,
        AdaptiveMsaaGBufferFillPass& pass,
        AdaptiveMsaaGBufferFillPass::Context& passContext,
        bool materialEvents = false,
        AdaptiveMsaaRasterSubmissionTrace* submissionTrace = nullptr);

    [[nodiscard]] uint64_t RenderAdaptiveMsaaCompositeView(
        nvrhi::ICommandList* commandList,
        const donut::engine::ICompositeView* compositeView,
        const donut::engine::ICompositeView* compositeViewPrev,
        donut::engine::FramebufferFactory& framebufferFactory,
        const std::shared_ptr<donut::engine::SceneGraphNode>& rootNode,
        donut::render::IDrawStrategy& drawStrategy,
        AdaptiveMsaaGBufferFillPass& pass,
        AdaptiveMsaaGBufferFillPass::Context& passContext,
        const char* passEvent = nullptr,
        bool materialEvents = false,
        AdaptiveMsaaRasterSubmissionTrace* submissionTrace = nullptr);
}
