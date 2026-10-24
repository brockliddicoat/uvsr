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
// Adapted from donut/src/render/GeometryPasses.cpp at pinned Donut commit
// bc1ea24b0486f1c00d89327fe16c0b4dd11c5937 for UVSR source base
// 578496047c4753077e36f39a68df8c2c323cee58. This owned copy preserves the
// traversal and material-marker flow while carrying exact DrawItem geometry
// identity and using a stricter, overflow-safe instancing predicate.

#include "adaptive_msaa_geometry_pass.h"

#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>
#include <donut/render/DrawStrategy.h>

#include <limits>
#include <unordered_map>

using namespace donut::math;
#include <donut/shaders/bindless.h>

namespace
{
    using donut::engine::BufferGroup;
    using donut::engine::Material;
    using donut::engine::MeshGeometry;
    using donut::engine::MeshInfo;
    using donut::render::DrawItem;
    using uvsr::AdaptiveMsaaRasterSupportClass;

    struct AdaptiveDrawIdentity
    {
        const MeshInfo* mesh = nullptr;
        const MeshGeometry* geometry = nullptr;
        const Material* material = nullptr;
        const BufferGroup* buffers = nullptr;
        nvrhi::RasterCullMode cullMode = nvrhi::RasterCullMode::Back;
        AdaptiveMsaaRasterSupportClass supportClass =
            AdaptiveMsaaRasterSupportClass::InvalidKeyFallback;
        uint32_t geometryLocalIndex =
            uvsr::AdaptiveMsaaGBufferFillPass::InvalidGeometryLocalIndex;
        uint32_t vertexCount = 0u;
        uint32_t geometryVertexCount = 0u;
        uint32_t geometryIndexCount = 0u;
        uint32_t startIndexLocation = 0u;
        uint32_t startVertexLocation = 0u;
        bool valid = false;
    };

    class GeometryOrdinalCache
    {
    private:
        using OrdinalMap = std::unordered_map<
            const MeshGeometry*,
            uint32_t>;
        std::unordered_map<const MeshInfo*, OrdinalMap> m_MeshOrdinals;

    public:
        [[nodiscard]] uint32_t Resolve(
            const MeshInfo* mesh,
            const MeshGeometry* geometry)
        {
            if (!mesh || !geometry)
            {
                return uvsr::AdaptiveMsaaGBufferFillPass::
                    InvalidGeometryLocalIndex;
            }

            auto [meshIt, inserted] = m_MeshOrdinals.try_emplace(mesh);
            if (inserted)
            {
                OrdinalMap& ordinals = meshIt->second;
                for (size_t index = 0;
                    index < mesh->geometries.size();
                    ++index)
                {
                    const MeshGeometry* candidate =
                        mesh->geometries[index].get();
                    if (!candidate ||
                        index > std::numeric_limits<uint32_t>::max())
                    {
                        continue;
                    }

                    const uint32_t ordinal =
                        static_cast<uint32_t>(index);
                    auto [ordinalIt, unique] =
                        ordinals.try_emplace(candidate, ordinal);
                    if (!unique)
                    {
                        // A shared geometry pointer in two mesh slots has no
                        // unique logical geometry identity. Keep the draw but
                        // force its key to the explicit invalid sentinel.
                        ordinalIt->second =
                            uvsr::AdaptiveMsaaGBufferFillPass::
                                InvalidGeometryLocalIndex;
                    }
                }
            }

            const auto ordinalIt = meshIt->second.find(geometry);
            return ordinalIt != meshIt->second.end()
                ? ordinalIt->second
                : uvsr::AdaptiveMsaaGBufferFillPass::
                    InvalidGeometryLocalIndex;
        }
    };

    [[nodiscard]] bool TryAddUint32(
        uint32_t left,
        uint32_t right,
        uint32_t& result)
    {
        if (left > std::numeric_limits<uint32_t>::max() - right)
            return false;

        result = left + right;
        return true;
    }

    [[nodiscard]] bool ContainsVertexElements(
        const BufferGroup& buffers,
        donut::engine::VertexAttribute attribute,
        uint32_t endVertex,
        uint32_t elementSize)
    {
        const nvrhi::BufferRange& range =
            buffers.getVertexBufferRange(attribute);
        const uint64_t requiredBytes =
            static_cast<uint64_t>(endVertex) * elementSize;
        const uint64_t maximumRawByteEnd =
            uint64_t(std::numeric_limits<uint32_t>::max()) + 1u;
        const uint64_t physicalByteSize =
            buffers.vertexBuffer->getDesc().byteSize;
        return range.byteSize >= requiredBytes &&
            range.byteOffset <= physicalByteSize &&
            requiredBytes <= physicalByteSize - range.byteOffset &&
            range.byteOffset <= maximumRawByteEnd &&
            requiredBytes <= maximumRawByteEnd - range.byteOffset;
    }

    [[nodiscard]] bool BuildDrawArguments(
        const DrawItem& item,
        nvrhi::DrawArguments& args)
    {
        if (!item.instance || !item.mesh || !item.geometry ||
            !item.material || !item.buffers ||
            !item.buffers->indexBuffer ||
            !item.buffers->vertexBuffer ||
            !item.buffers->instanceBuffer ||
            item.geometry->numIndices == 0u ||
            item.geometry->numVertices == 0u)
        {
            return false;
        }

        const int signedInstanceIndex =
            item.instance->GetInstanceIndex();
        if (signedInstanceIndex < 0)
            return false;

        const uint32_t instanceIndex =
            static_cast<uint32_t>(signedInstanceIndex);
        const nvrhi::BufferDesc& instanceDesc =
            item.buffers->instanceBuffer->getDesc();
        if (instanceDesc.structStride != sizeof(InstanceData) ||
            instanceDesc.byteSize < instanceDesc.structStride ||
            instanceDesc.byteSize % instanceDesc.structStride != 0u ||
            instanceIndex >=
                instanceDesc.byteSize / instanceDesc.structStride)
        {
            return false;
        }

        uint32_t startVertex = 0u;
        uint32_t startIndex = 0u;
        uint32_t endVertex = 0u;
        uint32_t endIndex = 0u;
        if (!TryAddUint32(
                item.mesh->vertexOffset,
                item.geometry->vertexOffsetInMesh,
                startVertex) ||
            !TryAddUint32(
                item.mesh->indexOffset,
                item.geometry->indexOffsetInMesh,
                startIndex) ||
            !TryAddUint32(
                startVertex,
                item.geometry->numVertices,
                endVertex) ||
            !TryAddUint32(
                startIndex,
                item.geometry->numIndices,
                endIndex))
        {
            return false;
        }

        const nvrhi::BufferDesc& indexDesc =
            item.buffers->indexBuffer->getDesc();
        const bool hasPreviousPositions = item.buffers->hasAttribute(
            donut::engine::VertexAttribute::PrevPosition);
        if (static_cast<uint64_t>(endIndex) * sizeof(uint32_t) >
                indexDesc.byteSize ||
            !ContainsVertexElements(
                *item.buffers,
                donut::engine::VertexAttribute::Position,
                endVertex,
                12u) ||
            (hasPreviousPositions &&
                !ContainsVertexElements(
                    *item.buffers,
                    donut::engine::VertexAttribute::PrevPosition,
                    endVertex,
                    12u)) ||
            !ContainsVertexElements(
                *item.buffers,
                donut::engine::VertexAttribute::TexCoord1,
                endVertex,
                8u) ||
            !ContainsVertexElements(
                *item.buffers,
                donut::engine::VertexAttribute::Normal,
                endVertex,
                4u) ||
            !ContainsVertexElements(
                *item.buffers,
                donut::engine::VertexAttribute::Tangent,
                endVertex,
                4u))
        {
            return false;
        }

        args.vertexCount = item.geometry->numIndices;
        args.instanceCount = 1u;
        args.startVertexLocation = startVertex;
        args.startIndexLocation = startIndex;
        args.startInstanceLocation = instanceIndex;
        return true;
    }

    [[nodiscard]] AdaptiveMsaaRasterSupportClass ClassifyRasterSupport(
        const DrawItem& item,
        uint32_t geometryLocalIndex)
    {
        if (geometryLocalIndex ==
                uvsr::AdaptiveMsaaGBufferFillPass::InvalidGeometryLocalIndex ||
            !item.mesh ||
            !item.geometry ||
            item.mesh->type != donut::engine::MeshType::Triangles ||
            item.geometry->type !=
                donut::engine::MeshGeometryPrimitiveType::Triangles)
        {
            return AdaptiveMsaaRasterSupportClass::InvalidKeyFallback;
        }

        return AdaptiveMsaaRasterSupportClass::SurfaceKey;
    }

    [[nodiscard]] AdaptiveDrawIdentity MakeDrawIdentity(
        const DrawItem& item,
        const nvrhi::DrawArguments& args,
        uint32_t geometryLocalIndex,
        AdaptiveMsaaRasterSupportClass supportClass)
    {
        AdaptiveDrawIdentity identity;
        identity.mesh = item.mesh;
        identity.geometry = item.geometry;
        identity.material = item.material;
        identity.buffers = item.buffers;
        identity.cullMode = item.cullMode;
        identity.supportClass = supportClass;
        identity.geometryLocalIndex = geometryLocalIndex;
        identity.vertexCount = args.vertexCount;
        identity.geometryVertexCount = item.geometry->numVertices;
        identity.geometryIndexCount = item.geometry->numIndices;
        identity.startIndexLocation = args.startIndexLocation;
        identity.startVertexLocation = args.startVertexLocation;
        identity.valid = true;
        return identity;
    }

    [[nodiscard]] bool CanAppendInstance(
        const AdaptiveDrawIdentity& identity,
        const DrawItem& item,
        const nvrhi::DrawArguments& args,
        uint32_t geometryLocalIndex,
        AdaptiveMsaaRasterSupportClass supportClass,
        const nvrhi::DrawArguments& currentDraw)
    {
        if (!identity.valid ||
            identity.mesh != item.mesh ||
            identity.geometry != item.geometry ||
            identity.material != item.material ||
            identity.buffers != item.buffers ||
            identity.cullMode != item.cullMode ||
            identity.supportClass != supportClass ||
            identity.geometryLocalIndex != geometryLocalIndex ||
            identity.vertexCount != args.vertexCount ||
            identity.geometryVertexCount != item.geometry->numVertices ||
            identity.geometryIndexCount != item.geometry->numIndices ||
            identity.startIndexLocation != args.startIndexLocation ||
            identity.startVertexLocation != args.startVertexLocation ||
            currentDraw.instanceCount == 0u ||
            currentDraw.instanceCount == std::numeric_limits<uint32_t>::max())
        {
            return false;
        }

        if (currentDraw.startInstanceLocation >
            std::numeric_limits<uint32_t>::max() -
                currentDraw.instanceCount)
        {
            return false;
        }

        const uint32_t expectedInstance =
            currentDraw.startInstanceLocation + currentDraw.instanceCount;
        return expectedInstance == args.startInstanceLocation;
    }

    [[nodiscard]] uint64_t CountSubmittedTriangles(
        const nvrhi::DrawArguments& args)
    {
        return static_cast<uint64_t>(args.vertexCount / 3u) *
            static_cast<uint64_t>(args.instanceCount);
    }

    void HashSubmissionValue(
        uvsr::AdaptiveMsaaRasterSubmissionTrace& trace,
        uint64_t value)
    {
        constexpr uint64_t FnvPrime = 1099511628211ull;
        for (uint32_t byteIndex = 0u; byteIndex < 8u; ++byteIndex)
        {
            trace.sequenceHash ^=
                (value >> (byteIndex * 8u)) & 0xffull;
            trace.sequenceHash *= FnvPrime;
        }
    }

    template <typename Pointer>
    void HashSubmissionPointer(
        uvsr::AdaptiveMsaaRasterSubmissionTrace& trace,
        Pointer* pointer)
    {
        HashSubmissionValue(
            trace,
            static_cast<uint64_t>(
                reinterpret_cast<std::uintptr_t>(pointer)));
    }

    void AppendSubmissionTrace(
        uvsr::AdaptiveMsaaRasterSubmissionTrace& trace,
        const donut::engine::IView* view,
        const AdaptiveDrawIdentity& identity,
        const nvrhi::DrawArguments& arguments)
    {
        HashSubmissionPointer(trace, identity.mesh);
        HashSubmissionPointer(trace, identity.geometry);
        HashSubmissionPointer(trace, identity.material);
        HashSubmissionPointer(trace, identity.buffers);
        HashSubmissionValue(
            trace, static_cast<uint64_t>(identity.cullMode));
        HashSubmissionValue(
            trace, static_cast<uint64_t>(identity.supportClass));
        HashSubmissionValue(trace, identity.geometryLocalIndex);
        HashSubmissionValue(trace, arguments.vertexCount);
        HashSubmissionValue(trace, arguments.instanceCount);
        HashSubmissionValue(trace, arguments.startIndexLocation);
        HashSubmissionValue(trace, arguments.startVertexLocation);
        HashSubmissionValue(trace, arguments.startInstanceLocation);
        uvsr::AdaptiveMsaaRasterSubmissionRecord record;
        record.view = view;
        record.mesh = identity.mesh;
        record.geometry = identity.geometry;
        record.material = identity.material;
        record.buffers = identity.buffers;
        record.cullMode = identity.cullMode;
        record.supportClass = identity.supportClass;
        record.geometryLocalIndex = identity.geometryLocalIndex;
        record.vertexCount = arguments.vertexCount;
        record.instanceCount = arguments.instanceCount;
        record.startIndexLocation = arguments.startIndexLocation;
        record.startVertexLocation = arguments.startVertexLocation;
        record.startInstanceLocation = arguments.startInstanceLocation;
        trace.records.push_back(record);
        ++trace.submittedDraws;
        trace.submittedTriangles += CountSubmittedTriangles(arguments);
    }
}

namespace uvsr
{
    uint64_t RenderAdaptiveMsaaView(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView* view,
        const donut::engine::IView* viewPrev,
        nvrhi::IFramebuffer* framebuffer,
        donut::render::IDrawStrategy& drawStrategy,
        AdaptiveMsaaGBufferFillPass& pass,
        AdaptiveMsaaGBufferFillPass::Context& passContext,
        bool materialEvents,
        AdaptiveMsaaRasterSubmissionTrace* submissionTrace)
    {
        using namespace donut::engine;

        if (!commandList || !view || !framebuffer ||
            !pass.HasValidGeometryInstanceHandleMap())
        {
            return 0u;
        }
        if (!viewPrev)
            viewPrev = view;

        if (submissionTrace)
        {
            HashSubmissionValue(*submissionTrace, 0x5653574956455755ull);
            HashSubmissionPointer(*submissionTrace, view);
            submissionTrace->views.push_back(view);
        }

        pass.SetupView(passContext, commandList, view, viewPrev);

        const Material* lastMaterial = nullptr;
        const BufferGroup* lastBuffers = nullptr;
        nvrhi::RasterCullMode lastCullMode = nvrhi::RasterCullMode::Back;

        bool drawMaterial = true;
        bool stateValid = false;
        const Material* eventMaterial = nullptr;

        nvrhi::GraphicsState graphicsState;
        graphicsState.framebuffer = framebuffer;
        graphicsState.viewport = view->GetViewportState();
        graphicsState.shadingRateState =
            view->GetVariableRateShadingState();

        nvrhi::DrawArguments currentDraw;
        currentDraw.instanceCount = 0u;
        AdaptiveDrawIdentity currentIdentity;
        GeometryOrdinalCache geometryOrdinals;
        uint64_t submittedTriangles = 0u;

        auto flushDraw = [&]()
        {
            if (currentDraw.instanceCount == 0u)
                return;

            const Material* material = currentIdentity.material;
            if (materialEvents && material != eventMaterial)
            {
                if (eventMaterial)
                    commandList->endMarker();

                if (!material || material->name.empty())
                {
                    eventMaterial = nullptr;
                }
                else
                {
                    commandList->beginMarker(material->name.c_str());
                    eventMaterial = material;
                }
            }

            pass.SetGeometryLocalIndex(
                passContext,
                currentIdentity.geometryLocalIndex);
            submittedTriangles += CountSubmittedTriangles(currentDraw);
            if (submissionTrace)
            {
                AppendSubmissionTrace(
                    *submissionTrace,
                    view,
                    currentIdentity,
                    currentDraw);
            }
            pass.SetPushConstants(
                passContext,
                commandList,
                graphicsState,
                currentDraw);
            commandList->drawIndexed(currentDraw);

            currentDraw.instanceCount = 0u;
            currentIdentity = {};
        };

        while (const donut::render::DrawItem* item =
            drawStrategy.GetNextItem())
        {
            nvrhi::DrawArguments args;
            if (!BuildDrawArguments(*item, args))
            {
                // Never batch across a malformed item: it may represent an
                // ordering or state boundary even though no safe GPU draw can
                // be submitted for it.
                flushDraw();
                continue;
            }

            const bool newBuffers = item->buffers != lastBuffers;
            const bool newMaterial =
                item->material != lastMaterial ||
                item->cullMode != lastCullMode;

            if (newBuffers || newMaterial)
                flushDraw();

            if (newBuffers)
            {
                pass.SetupInputBuffers(
                    passContext,
                    item->buffers,
                    graphicsState);
                lastBuffers = item->buffers;
                stateValid = false;
            }

            // SetupMaterial installs the current input binding set as well as
            // the material and view sets. Re-run it when only BufferGroup
            // changes so t10/t11/t12 can never remain bound to the prior draw.
            if (newMaterial || newBuffers)
            {
                drawMaterial = pass.SetupMaterial(
                    passContext,
                    item->material,
                    item->cullMode,
                    graphicsState);
                lastMaterial = item->material;
                lastCullMode = item->cullMode;
                stateValid = false;
            }

            if (!drawMaterial)
                continue;

            uint32_t geometryLocalIndex =
                geometryOrdinals.Resolve(item->mesh, item->geometry);
            const AdaptiveMsaaRasterSupportClass supportClass =
                ClassifyRasterSupport(*item, geometryLocalIndex);
            if (supportClass ==
                AdaptiveMsaaRasterSupportClass::InvalidKeyFallback)
            {
                geometryLocalIndex =
                    AdaptiveMsaaGBufferFillPass::InvalidGeometryLocalIndex;
            }

            if (CanAppendInstance(
                currentIdentity,
                *item,
                args,
                geometryLocalIndex,
                supportClass,
                currentDraw))
            {
                ++currentDraw.instanceCount;
                continue;
            }

            // Flush before replacing the stored identity. SetPushConstants
            // consumes and resets the geometry-local state of that exact batch.
            flushDraw();

            if (!stateValid)
            {
                commandList->setGraphicsState(graphicsState);
                stateValid = true;
            }

            currentDraw = args;
            currentIdentity = MakeDrawIdentity(
                *item,
                args,
                geometryLocalIndex,
                supportClass);
        }

        flushDraw();

        if (materialEvents && eventMaterial)
            commandList->endMarker();

        return submittedTriangles;
    }

    uint64_t RenderAdaptiveMsaaCompositeView(
        nvrhi::ICommandList* commandList,
        const donut::engine::ICompositeView* compositeView,
        const donut::engine::ICompositeView* compositeViewPrev,
        donut::engine::FramebufferFactory& framebufferFactory,
        const std::shared_ptr<donut::engine::SceneGraphNode>& rootNode,
        donut::render::IDrawStrategy& drawStrategy,
        AdaptiveMsaaGBufferFillPass& pass,
        AdaptiveMsaaGBufferFillPass::Context& passContext,
        const char* passEvent,
        bool materialEvents,
        AdaptiveMsaaRasterSubmissionTrace* submissionTrace)
    {
        using namespace donut::engine;

        if (!commandList || !compositeView ||
            !pass.HasValidGeometryInstanceHandleMap())
        {
            return 0u;
        }

        if (passEvent)
            commandList->beginMarker(passEvent);

        if (submissionTrace)
            *submissionTrace = {};

        const ViewType::Enum supportedViewTypes =
            pass.GetSupportedViewTypes();

        if (compositeViewPrev &&
            compositeView->GetNumChildViews(supportedViewTypes) !=
                compositeViewPrev->GetNumChildViews(supportedViewTypes))
        {
            // Motion from mismatched view topologies is undefined. Use the
            // current child as its own previous view, producing zero motion
            // without dropping otherwise valid geometry.
            compositeViewPrev = nullptr;
        }

        uint64_t submittedTriangles = 0u;
        const uint32_t viewCount =
            compositeView->GetNumChildViews(supportedViewTypes);
        for (uint32_t viewIndex = 0u; viewIndex < viewCount; ++viewIndex)
        {
            const IView* view = compositeView->GetChildView(
                supportedViewTypes,
                viewIndex);
            const IView* viewPrev = compositeViewPrev
                ? compositeViewPrev->GetChildView(
                    supportedViewTypes,
                    viewIndex)
                : view;
            if (!view)
                continue;
            if (!viewPrev)
                viewPrev = view;

            drawStrategy.PrepareForView(rootNode, *view);
            nvrhi::IFramebuffer* framebuffer =
                framebufferFactory.GetFramebuffer(*view);
            if (!framebuffer)
                continue;

            submittedTriangles += RenderAdaptiveMsaaView(
                commandList,
                view,
                viewPrev,
                framebuffer,
                drawStrategy,
                pass,
                passContext,
                materialEvents,
                submissionTrace);
        }

        if (passEvent)
            commandList->endMarker();

        return submittedTriangles;
    }
}
