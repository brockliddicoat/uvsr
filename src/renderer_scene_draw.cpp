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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "renderer_scene_draw.h"
#include <math.h>
#include <new>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene draw preparation requires exception-disabled compilation
#endif

namespace uvsr
{
    namespace
    {
#if defined(UVSR_BUILD_TESTING)
        thread_local bool failAllocation = false;
#endif
        // retain the current opaque traversal's whole-mesh chunk boundaries.
        // numeric scene indices replace pointer-address sorting within a chunk.
        constexpr size_t ChunkTarget = 128;

        bool Before(const RendererSceneDraw& a, const RendererSceneDraw& b, const RendererSceneView& scene) noexcept
        {
            if (a.material != b.material) return a.material < b.material;
            if (a.buffers != b.buffers) return a.buffers < b.buffers;
            if (a.mesh != b.mesh) return a.mesh < b.mesh;
            if (a.instance != b.instance)
                return scene.nodes.data[scene.instances.data[a.instance].nodeIndex].preorderIndex <
                    scene.nodes.data[scene.instances.data[b.instance].nodeIndex].preorderIndex;
            return a.geometry < b.geometry;
        }

        void Sift(RendererSceneDraw* items, size_t root, size_t count, const RendererSceneView& scene) noexcept
        {
            while (count > 1 && root <= (count - 2) / 2)
            {
                size_t child = root * 2 + 1;
                if (child + 1 < count && Before(items[child], items[child + 1], scene)) ++child;
                if (!Before(items[root], items[child], scene)) return;
                const auto value = items[root]; items[root] = items[child]; items[child] = value;
                root = child;
            }
        }

        void Sort(RendererSceneDraw* items, size_t count, const RendererSceneView& scene) noexcept
        {
            for (size_t root = count / 2; root > 0; --root) Sift(items, root - 1, count, scene);
            for (size_t end = count; end > 1;)
            {
                --end;
                const auto value = items[0]; items[0] = items[end]; items[end] = value;
                Sift(items, 0, end, scene);
            }
        }
    }

    bool IntersectsRendererSceneBounds(const RendererSceneFrustum& frustum,
        const RendererSceneBounds& bounds) noexcept
    {
        if (bounds.empty) return false;
        // preserve the retained frustum's float operation order at the boundary.
        for (const auto& plane : frustum.planes)
        {
            const float x = plane.normal.x > 0 ? bounds.minimum.x : bounds.maximum.x;
            const float y = plane.normal.y > 0 ? bounds.minimum.y : bounds.maximum.y;
            const float z = plane.normal.z > 0 ? bounds.minimum.z : bounds.maximum.z;
            const float distance = plane.normal.x * x + plane.normal.y * y + plane.normal.z * z - plane.distance;
            if (distance > 0) return false;
        }
        return true;
    }

    RendererSceneDrawList::~RendererSceneDrawList() { Reset(); }
    void RendererSceneDrawList::Reset() noexcept
    {
        delete[] m_items;
        m_items = nullptr;
        m_capacity = m_count = m_chunks = m_maxChunk = 0;
        m_generation = 0;
    }

    RendererSceneResult RendererSceneDrawList::Prepare(const RendererSceneView& scene) noexcept
    {
        if (!scene.generation) return {RendererSceneError::Generation};
        if (!scene.instances.IsValid() || !scene.meshes.IsValid()) return {RendererSceneError::Reference};
        size_t capacity = 0;
        constexpr size_t maximum = size_t(PTRDIFF_MAX) / sizeof(RendererSceneDraw);
        for (size_t index = 0; index < scene.instances.count; ++index)
        {
            const auto& instance = scene.instances.data[index];
            if (instance.meshIndex >= scene.meshes.count) return {RendererSceneError::Reference};
            const size_t count = scene.meshes.data[instance.meshIndex].geometries.count;
            if (count > maximum - capacity) return {RendererSceneError::Capacity};
            capacity += count;
        }
        RendererSceneDraw* candidate = nullptr;
        if (capacity)
        {
#if defined(UVSR_BUILD_TESTING)
            if (failAllocation) return {RendererSceneError::Allocation};
#endif
            candidate = new (std::nothrow) RendererSceneDraw[capacity];
            if (!candidate) return {RendererSceneError::Allocation};
        }
        Reset();
        m_items = candidate;
        m_capacity = capacity;
        m_generation = scene.generation;
        return {};
    }

    RendererSceneResult RendererSceneDrawList::Build(const RendererSceneView& scene,
        const RendererSceneFrustum& frustum) noexcept
    {
        m_count = m_chunks = m_maxChunk = 0;
        if (!m_generation || scene.generation != m_generation) return {RendererSceneError::Generation};
        for (const auto& plane : frustum.planes)
            if (!isfinite(plane.normal.x) || !isfinite(plane.normal.y) || !isfinite(plane.normal.z) || !isfinite(plane.distance))
                return {RendererSceneError::Value};
        size_t count = 0, chunkBegin = 0;
        const auto finishChunk = [&]()
        {
            const size_t chunkSize = count - chunkBegin;
            if (!chunkSize) return;
            Sort(m_items + chunkBegin, chunkSize, scene);
            ++m_chunks;
            if (chunkSize > m_maxChunk) m_maxChunk = chunkSize;
            chunkBegin = count;
        };
        for (size_t position = 0; position < scene.preorder.count;)
        {
            const uint32_t index = scene.preorder.data[position];
            if (index >= scene.nodes.count) return {RendererSceneError::Reference, index};
            const auto& node = scene.nodes.data[index];
            if (!(node.subtreeContent & (SceneContentOpaque | SceneContentAlphaTested)) ||
                !IntersectsRendererSceneBounds(frustum, node.worldBounds))
            {
                if (node.subtreeEnd <= position || node.subtreeEnd > scene.preorder.count)
                    return {RendererSceneError::Hierarchy, index};
                position = node.subtreeEnd;
                continue;
            }
            ++position;
            if (node.leafKind != RendererSceneLeafKind::Instance) continue;
            if (node.leafIndex >= scene.instances.count) return {RendererSceneError::Reference, index};
            const auto& instance = scene.instances.data[node.leafIndex];
            if (instance.meshIndex >= scene.meshes.count || instance.nodeIndex != index)
                return {RendererSceneError::Reference, index};
            const auto& mesh = scene.meshes.data[instance.meshIndex];
            for (uint32_t local = 0; local < mesh.geometries.count; ++local)
            {
                const uint32_t geometryIndex = mesh.geometries.first + local;
                if (geometryIndex >= scene.geometries.count) return {RendererSceneError::Reference, geometryIndex};
                const auto& geometry = scene.geometries.data[geometryIndex];
                if (geometry.materialIndex >= scene.materials.count) return {RendererSceneError::Reference, geometryIndex};
                const auto domain = scene.materials.data[geometry.materialIndex].values.domain;
                if (domain != RendererMaterialDomain::Opaque && domain != RendererMaterialDomain::AlphaTested) continue;
                if (mesh.geometries.count > 1 && mesh.skinPrototypeIndex == InvalidSceneIndex &&
                    !IntersectsRendererSceneBounds(frustum, TransformRendererSceneBounds(geometry.objectBounds, node.world)))
                    continue;
                if (count >= m_capacity) return {RendererSceneError::Capacity};
                m_items[count++] = {geometry.materialIndex, mesh.bufferGroupIndex, instance.meshIndex, node.leafIndex, geometryIndex};
            }
            if (count - chunkBegin >= ChunkTarget) finishChunk();
        }
        finishChunk();
        m_count = count;
        return {};
    }

#if defined(UVSR_BUILD_TESTING)
    void SetRendererSceneDrawAllocationFailure(bool fail) noexcept { failAllocation = fail; }
#endif
}
