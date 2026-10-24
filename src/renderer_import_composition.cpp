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

#include "renderer_import_composition.h"
#include "scene_light_names.h"
#include "import/renderer_import_allocation.h"
#include "import/renderer_import_composition_private.h"
#include "import/renderer_import_description_private.h"

#include <new>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

namespace uvsr
{
    namespace
    {
        constexpr uint32_t invalid = InvalidSceneIndex;
        using LeafKind = RendererSceneLeafKind;
        using Text = ArrayView<const char>;

        ImportResult Failure(ImportError error, ImportObject object = ImportObject::Scene,
            size_t index = SIZE_MAX) noexcept { return {error, object, index}; }

        ImportResult SceneResult(RendererSceneResult result) noexcept
        {
            switch (result.error)
            {
            case RendererSceneError::None: return {};
            case RendererSceneError::Allocation: return Failure(ImportError::OutOfMemory);
            case RendererSceneError::Capacity: return Failure(ImportError::Capacity);
            case RendererSceneError::Workspace: return Failure(ImportError::Workspace);
            default: return Failure(ImportError::InvalidData, ImportObject::Scene, result.index);
            }
        }

#define UVSR_COMPOSE_TRY(expression) do { const ImportResult result = (expression); if (!result) return result; } while (false)
#define UVSR_COMPOSE_WRITE(expression) UVSR_COMPOSE_TRY(SceneResult(expression))

        bool Add(uint32_t& count, size_t extra) noexcept
        {
            if (extra >= invalid - count) return false;
            count += uint32_t(extra);
            return true;
        }

        bool Equal(Text a, Text b) noexcept
        { return a.count == b.count && (!a.count || memcmp(a.data, b.data, a.count) == 0); }
        Text Literal(const char* value) noexcept { return {value, strlen(value)}; }
        bool Separator(char value) noexcept { return value == '/' || value == '\\'; }

        struct Budget
        {
            size_t current = 0, peak = 0, limit = SIZE_MAX;
            ImportResult Acquire(size_t count, size_t stride) noexcept
            {
                if (count > size_t(PTRDIFF_MAX) / stride || count * stride > SIZE_MAX - current)
                    return Failure(ImportError::Overflow);
                const size_t bytes = count * stride;
                if (current > limit || bytes > limit - current) return Failure(ImportError::Workspace);
                current += bytes;
                if (current > peak) peak = current;
                return {};
            }
        };

        // one composition owns these loading-only arrays. validated inputs seed
        // capacities; clones grow explicitly. peak includes old and new storage
        // during growth. no element owns another element or a deferred borrow.
        template<class T> struct List
        {
            T* data = nullptr;
            uint32_t count = 0, capacity = 0;
            Budget* owner = nullptr;
            List() noexcept = default;
            List(const List&) = delete;
            List& operator=(const List&) = delete;
            ~List() noexcept { free(data); if (owner) owner->current -= size_t(capacity) * sizeof(T); }

            ImportResult Reserve(uint32_t requested, Budget& budget) noexcept
            {
                static_assert(std::is_trivially_copyable_v<T> && std::is_nothrow_default_constructible_v<T>);
                if (requested <= capacity) return {};
                if (requested == invalid) return Failure(ImportError::Capacity);
                if (owner && owner != &budget) return Failure(ImportError::InvalidState);
                UVSR_COMPOSE_TRY(budget.Acquire(requested, sizeof(T)));
                auto* replacement = static_cast<T*>(ImportAllocate(size_t(requested) * sizeof(T)));
                if (!replacement)
                {
                    budget.current -= size_t(requested) * sizeof(T);
                    return Failure(ImportError::OutOfMemory);
                }
                for (uint32_t i = 0; i < requested; ++i) new (replacement + i) T{};
                for (uint32_t i = 0; i < count; ++i) replacement[i] = data[i];
                free(data);
                budget.current -= size_t(capacity) * sizeof(T);
                data = replacement;
                capacity = requested;
                owner = &budget;
                return {};
            }
            ImportResult Resize(uint32_t requested, Budget& budget) noexcept
            {
                UVSR_COMPOSE_TRY(Reserve(requested, budget));
                for (uint32_t i = count; i < requested; ++i) data[i] = T{};
                count = requested;
                return {};
            }
            ImportResult Push(T value, Budget& budget, uint32_t& index) noexcept
            {
                if (count >= invalid - 1) return Failure(ImportError::Capacity);
                if (count == capacity)
                {
                    const uint32_t grown = capacity == 0 ? 8 : capacity <= (invalid - 1) / 2 ? capacity * 2 : invalid - 1;
                    UVSR_COMPOSE_TRY(Reserve(grown, budget));
                }
                index = count;
                data[count++] = value;
                return {};
            }
            ImportResult Push(T value, Budget& budget) noexcept
            { uint32_t ignored = 0; return Push(value, budget, ignored); }
            T& operator[](uint32_t index) noexcept { return data[index]; }
            const T& operator[](uint32_t index) const noexcept { return data[index]; }
            ArrayView<const T> View() const noexcept { return {data, count}; }
        };

        struct Mapping { uint32_t value = invalid; };
        struct Source { uint32_t model = invalid, index = invalid; };
        struct ModelPlan
        {
            RendererSceneView scene;
            uint32_t root = invalid;
            uint32_t nodeMap = 0, meshMap = 0, groupMap = 0, materialMap = 0;
            uint32_t imageMap = 0, requestMap = 0, samplerMap = 0;
        };
        struct Node
        {
            Text name;
            RendererSceneTransform transform;
            uint32_t parent = invalid, child = invalid, lastChild = invalid, sibling = invalid;
            uint32_t leaf = invalid, finalLeaf = invalid, cloneStamp = 0, cloneIndex = invalid;
            LeafKind kind = LeafKind::None;
            bool hasTransform = false;
        };
        struct Instance
        {
            Source source;
            uint32_t node = invalid, mesh = invalid, canonical = invalid;
            RendererSceneRange joints;
        };
        struct Mesh
        {
            Source source;
            uint32_t group = invalid, prototype = invalid;
        };
        struct Animation
        {
            uint32_t node = invalid;
            RendererSceneRange channels;
        };
        struct Channel
        {
            RendererSceneAnimationChannel record;
            Text property;
        };
        struct Sampler
        {
            ArrayView<const RendererSceneKeyframe> keys;
            RendererSceneInterpolation interpolation = RendererSceneInterpolation::Step;
            uint32_t canonical = invalid;
        };
        struct Image
        {
            ImportImageView value;
            uint32_t texture = invalid;
        };
        struct Texture
        {
            uint32_t image = invalid, firstSwizzle = invalid, lastSwizzle = invalid, swizzleCount = 0, canonical = invalid;
            bool srgb = false;
        };
        struct Swizzle
        {
            ImportTextureSwizzle value;
            uint32_t next = invalid;
        };

        struct Composer
        {
            const ImportDescriptionState& description;
            ArrayView<ImportModel> models;
            const ImportCompositionOptions& options;
            Budget budget;
            ImportCompositionStats stats;
            RendererSceneCounts counts;
            List<ModelPlan> modelPlans;
            List<Mapping> nodeMap, meshMap, groupMap, materialMap, imageMap, requestMap, samplerMap, imageSlots, descriptionNodes;
            List<Node> nodes;
            List<Instance> instances;
            List<RendererSceneJoint> joints;
            List<RendererSceneLight> lights;
            List<RendererSceneCamera> cameras;
            List<Animation> animations;
            List<Channel> channels;
            List<Sampler> samplers;
            List<Mesh> meshes;
            List<Source> materials, textureSources;
            List<CompositionGeometrySource> groups;
            List<Image> images;
            List<Texture> textures;
            List<Swizzle> swizzles;
            List<uint32_t> work, textureOrder, samplerOrder;
            uint32_t stamp = 0;
            uint32_t sunNode = invalid, flashlightNode = invalid;

            Composer(const ImportDescriptionState& source, ArrayView<ImportModel> inputs,
                const ImportCompositionOptions& limits) noexcept : description(source), models(inputs), options(limits)
            { budget.limit = limits.maxScratchBytes; }

            Text DescriptionText(RendererSceneString value) const noexcept
            { return value.length ? Text{description.strings + value.offset, value.length} : Text{}; }
            bool Live(uint32_t node, LeafKind kind, uint32_t leaf) const noexcept
            { return nodes[node].kind == kind && nodes[node].leaf == leaf; }

            ImportResult PlanInputs() noexcept;
            ImportResult PlanImages() noexcept;
            ImportResult AddNode(Node value, uint32_t parent, uint32_t& output) noexcept;
            ImportResult CopyModel(uint32_t model, uint32_t parent, uint32_t& output) noexcept;
            ImportResult CopyModelLeaf(uint32_t model, uint32_t source, uint32_t destination) noexcept;
            ImportResult Clone(uint32_t source, uint32_t parent, uint32_t& output) noexcept;
            ImportResult CloneLeaf(uint32_t source, uint32_t destination) noexcept;
            uint32_t Remap(uint32_t node) const noexcept;
            ImportResult FindNode(Text path, uint32_t& output) const noexcept;
            ImportResult Graph() noexcept;
            ImportResult ReplaceLeaf(uint32_t entry, uint32_t node) noexcept;
            ImportResult SelectGroup(uint32_t model, uint32_t group, uint32_t& output) noexcept;
            ImportResult SelectStaticMesh(uint32_t model, uint32_t mesh, uint32_t& output) noexcept;
            ImportResult SelectMesh(uint32_t model, uint32_t mesh, uint32_t instance, uint32_t& output) noexcept;
            ImportResult SelectMaterial(uint32_t model, uint32_t material) noexcept;
            ImportResult SelectTexture(uint32_t model, uint32_t texture) noexcept;
            ImportResult Resources() noexcept;
            ImportResult DescriptionAnimations() noexcept;
            ImportResult RuntimeLights() noexcept;
            ImportResult FinalCounts() noexcept;
            ImportResult Write(RendererScene& scene) noexcept;
            ImportResult CopyTextures(ImportTextures& output) noexcept;
        };

        ImportResult Composer::PlanInputs() noexcept
        {
            if (!models.IsValid() || models.count != description.counts.models || models.count >= invalid)
                return Failure(ImportError::InvalidInput);
            const auto availability = options.modelAvailability;
            if (!availability.IsValid() || (availability.count && availability.count != models.count))
                return Failure(ImportError::InvalidInput);
            UVSR_COMPOSE_TRY(modelPlans.Resize(uint32_t(models.count), budget));
            uint32_t nodeTotal = 0, meshTotal = 0, groupTotal = 0, materialTotal = 0, imageTotal = 0, requestTotal = 0, samplerTotal = 0;
            for (uint32_t m = 0; m < models.count; ++m)
            {
                const auto& model = models.data[m];
                auto& plan = modelPlans[m];
                if (availability.count && availability.data[m] != ImportModelAvailability::Available)
                {
                    if (availability.data[m] != ImportModelAvailability::Unavailable || model.scene.StorageBytes() ||
                        model.geometry.StorageBytes() || model.textures.StorageBytes())
                        return Failure(ImportError::InvalidState, ImportObject::Scene, m);
                    continue;
                }
                plan.scene = model.scene.View();
                const auto& scene = plan.scene;
                if (!model.scene.IsPublished() || scene.root >= scene.nodes.count ||
                    model.geometry.BufferCount() != scene.bufferGroups.count || model.textures.TextureCount() != scene.textures.count)
                    return Failure(ImportError::InvalidState, ImportObject::Scene, m);
                for (size_t c = 0; c < scene.channels.count; ++c)
                {
                    const auto& channel = scene.channels.data[c];
                    if (channel.nodeIndex == invalid || channel.property.length || channel.attribute < RendererSceneAnimationAttribute::Scaling ||
                        channel.attribute > RendererSceneAnimationAttribute::Translation)
                        return Failure(ImportError::UnsupportedData, ImportObject::Animation, c);
                }
                plan.nodeMap = nodeTotal; plan.meshMap = meshTotal; plan.groupMap = groupTotal; plan.materialMap = materialTotal;
                plan.imageMap = imageTotal; plan.requestMap = requestTotal; plan.samplerMap = samplerTotal;
                if (!Add(nodeTotal, scene.nodes.count) || !Add(meshTotal, scene.meshes.count) || !Add(groupTotal, scene.bufferGroups.count) ||
                    !Add(materialTotal, scene.materials.count) || !Add(imageTotal, model.textures.ImageCount()) ||
                    !Add(requestTotal, model.textures.TextureRequestCount()) || !Add(samplerTotal, scene.samplers.count))
                    return Failure(ImportError::Capacity, ImportObject::Scene, m);
            }
            UVSR_COMPOSE_TRY(nodeMap.Resize(nodeTotal, budget));
            UVSR_COMPOSE_TRY(meshMap.Resize(meshTotal, budget));
            UVSR_COMPOSE_TRY(groupMap.Resize(groupTotal, budget));
            UVSR_COMPOSE_TRY(materialMap.Resize(materialTotal, budget));
            UVSR_COMPOSE_TRY(imageMap.Resize(imageTotal, budget));
            UVSR_COMPOSE_TRY(requestMap.Resize(requestTotal, budget));
            UVSR_COMPOSE_TRY(samplerMap.Resize(samplerTotal, budget));
            UVSR_COMPOSE_TRY(descriptionNodes.Resize(description.counts.nodes, budget));
            uint32_t initialNodes = nodeTotal;
            if (!Add(initialNodes, description.counts.nodes) || !Add(initialNodes, size_t(description.counts.animations) + 2) ||
                (options.runtimeLights.enabled && !Add(initialNodes, 2)))
                return Failure(ImportError::Capacity, ImportObject::Node);
            UVSR_COMPOSE_TRY(nodes.Reserve(initialNodes, budget));
            UVSR_COMPOSE_TRY(images.Reserve(imageTotal, budget));
            UVSR_COMPOSE_TRY(textures.Reserve(requestTotal, budget));
            if (imageTotal)
            {
                uint32_t slots = 8;
                while (uint64_t(slots) < uint64_t(imageTotal) * 2)
                {
                    if (slots > (invalid - 1) / 2) return Failure(ImportError::Capacity, ImportObject::Image);
                    slots *= 2;
                }
                UVSR_COMPOSE_TRY(imageSlots.Resize(slots, budget));
            }
            uint32_t root = invalid;
            Node node; node.name = Literal("SceneRoot");
            return AddNode(node, invalid, root);
        }

        ImportResult Composer::PlanImages() noexcept
        {
            for (uint32_t m = 0; m < models.count; ++m)
            {
                const auto& owner = models.data[m].textures;
                const auto& plan = modelPlans[m];
                for (uint32_t i = 0; i < owner.ImageCount(); ++i)
                {
                    const auto value = owner.Image(i);
                    uint32_t index = invalid;
                    if (!value.embedded)
                    {
                        uint64_t hash = UINT64_C(14695981039346656037);
                        for (size_t c = 0; c < value.path.count; ++c) { hash ^= uint8_t(value.path.data[c]); hash *= UINT64_C(1099511628211); }
                        uint32_t slot = uint32_t(hash) & (imageSlots.count - 1);
                        while (imageSlots[slot].value != invalid)
                        {
                            const uint32_t candidate = imageSlots[slot].value;
                            if (Equal(images[candidate].value.path, value.path)) { index = candidate; break; }
                            slot = (slot + 1) & (imageSlots.count - 1);
                        }
                        if (index == invalid)
                        {
                            UVSR_COMPOSE_TRY(images.Push({value}, budget, index));
                            imageSlots[slot].value = index;
                        }
                    }
                    else UVSR_COMPOSE_TRY(images.Push({value}, budget, index));
                    imageMap[plan.imageMap + i].value = index;
                }
            }
            for (uint32_t m = 0; m < models.count; ++m)
            {
                const auto& owner = models.data[m].textures;
                const auto& plan = modelPlans[m];
                for (uint32_t r = 0; r < owner.TextureRequestCount(); ++r)
                {
                    const auto request = owner.TextureRequest(r);
                    uint32_t image = invalid, texture = invalid;
                    if (request.imageIndex != invalid)
                    {
                        if (request.imageIndex >= owner.ImageCount()) return Failure(ImportError::InvalidIndex, ImportObject::Image, r);
                        image = imageMap[plan.imageMap + request.imageIndex].value;
                        texture = images[image].texture;
                    }
                    if (texture == invalid)
                    {
                        Texture value; value.image = image; value.srgb = request.forceSRGB;
                        UVSR_COMPOSE_TRY(textures.Push(value, budget, texture));
                        if (image != invalid)
                        {
                            images[image].texture = texture;
                            images[image].value = owner.Image(request.imageIndex);
                        }
                    }
                    requestMap[plan.requestMap + r].value = texture;
                    const uint32_t prefixCount = textures[texture].swizzleCount;
                    for (size_t s = 0; s < request.swizzles.count; ++s)
                    {
                        auto value = request.swizzles.data[s];
                        if (value.imageIndex >= owner.ImageCount()) return Failure(ImportError::InvalidIndex, ImportObject::Image, r);
                        value.imageIndex = imageMap[plan.imageMap + value.imageIndex].value;
                        bool duplicate = false;
                        uint32_t prior = textures[texture].firstSwizzle;
                        for (uint32_t p = 0; p < prefixCount; ++p)
                        {
                            if (swizzles[prior].value.imageIndex == value.imageIndex) { duplicate = true; break; }
                            prior = swizzles[prior].next;
                        }
                        if (duplicate) continue;
                        uint32_t added = invalid;
                        UVSR_COMPOSE_TRY(swizzles.Push({value}, budget, added));
                        auto& target = textures[texture];
                        if (target.lastSwizzle == invalid) target.firstSwizzle = added;
                        else swizzles[target.lastSwizzle].next = added;
                        target.lastSwizzle = added;
                        ++target.swizzleCount;
                    }
                }
            }
            return {};
        }

        ImportResult Composer::AddNode(Node value, uint32_t parent, uint32_t& output) noexcept
        {
            if (parent != invalid && parent >= nodes.count) return Failure(ImportError::InvalidHierarchy, ImportObject::Node, parent);
            value.parent = parent;
            value.child = value.lastChild = value.sibling = value.leaf = value.finalLeaf = value.cloneIndex = invalid;
            value.kind = LeafKind::None;
            value.cloneStamp = 0;
            UVSR_COMPOSE_TRY(nodes.Push(value, budget, output));
            if (parent != invalid)
            {
                if (nodes[parent].lastChild == invalid) nodes[parent].child = output;
                else nodes[nodes[parent].lastChild].sibling = output;
                nodes[parent].lastChild = output;
            }
            return {};
        }

        ImportResult Composer::CopyModel(uint32_t model, uint32_t parent, uint32_t& output) noexcept
        {
            auto& plan = modelPlans[model];
            const auto& source = plan.scene;
            for (uint32_t order = 0; order < source.preorder.count; ++order)
            {
                const uint32_t index = source.preorder.data[order];
                const auto& original = source.nodes.data[index];
                Node value;
                value.name = RendererSceneText(source, original.name);
                value.transform = original.transform;
                value.hasTransform = original.hasLocalTransform;
                const uint32_t destinationParent = original.parentIndex == invalid ? parent : nodeMap[plan.nodeMap + original.parentIndex].value;
                uint32_t destination = invalid;
                UVSR_COMPOSE_TRY(AddNode(value, destinationParent, destination));
                nodeMap[plan.nodeMap + index].value = destination;
            }
            output = nodeMap[plan.nodeMap + source.root].value;
            for (uint32_t order = 0; order < source.preorder.count; ++order)
            {
                const uint32_t index = source.preorder.data[order];
                UVSR_COMPOSE_TRY(CopyModelLeaf(model, index, nodeMap[plan.nodeMap + index].value));
            }
            plan.root = output;
            return {};
        }

        ImportResult Composer::CopyModelLeaf(uint32_t model, uint32_t sourceNode, uint32_t destination) noexcept
        {
            const auto& plan = modelPlans[model];
            const auto& source = plan.scene;
            const auto& node = source.nodes.data[sourceNode];
            uint32_t leaf = invalid;
            switch (node.leafKind)
            {
            case LeafKind::None: break;
            case LeafKind::Instance:
            {
                const auto& original = source.instances.data[node.leafIndex];
                Instance value;
                value.source = {model, node.leafIndex};
                value.node = destination;
                value.joints = {joints.count, original.joints.count};
                for (uint32_t i = 0; i < original.joints.count; ++i)
                {
                    auto joint = source.joints.data[original.joints.first + i];
                    joint.nodeIndex = nodeMap[plan.nodeMap + joint.nodeIndex].value;
                    UVSR_COMPOSE_TRY(joints.Push(joint, budget));
                }
                UVSR_COMPOSE_TRY(instances.Push(value, budget, leaf));
                break;
            }
            case LeafKind::Light:
            {
                auto value = source.lights.data[node.leafIndex];
                value.nodeIndex = destination;
                UVSR_COMPOSE_TRY(lights.Push(value, budget, leaf));
                break;
            }
            case LeafKind::Camera:
            {
                auto value = source.cameras.data[node.leafIndex];
                value.nodeIndex = destination;
                UVSR_COMPOSE_TRY(cameras.Push(value, budget, leaf));
                break;
            }
            case LeafKind::Animation:
            {
                const auto& original = source.animations.data[node.leafIndex];
                Animation value{destination, {channels.count, original.channels.count}};
                for (uint32_t i = 0; i < original.channels.count; ++i)
                {
                    Channel channel;
                    channel.record = source.channels.data[original.channels.first + i];
                    channel.property = RendererSceneText(source, channel.record.property);
                    if (channel.record.nodeIndex != invalid)
                        channel.record.nodeIndex = nodeMap[plan.nodeMap + channel.record.nodeIndex].value;
                    const uint32_t sourceSampler = channel.record.samplerIndex;
                    uint32_t& sampler = samplerMap[plan.samplerMap + sourceSampler].value;
                    if (sampler == invalid)
                    {
                        const auto& record = source.samplers.data[sourceSampler];
                        Sampler input;
                        input.interpolation = record.interpolation;
                        if (record.keyframes.count) input.keys = {source.keyframes.data + record.keyframes.first, record.keyframes.count};
                        UVSR_COMPOSE_TRY(samplers.Push(input, budget, sampler));
                    }
                    channel.record.samplerIndex = sampler;
                    UVSR_COMPOSE_TRY(channels.Push(channel, budget));
                }
                UVSR_COMPOSE_TRY(animations.Push(value, budget, leaf));
                break;
            }
            default: return Failure(ImportError::UnsupportedData, ImportObject::Node, sourceNode);
            }
            nodes[destination].kind = node.leafKind;
            nodes[destination].leaf = leaf;
            return {};
        }

        uint32_t Composer::Remap(uint32_t node) const noexcept
        { return node != invalid && nodes[node].cloneStamp == stamp ? nodes[node].cloneIndex : node; }

        ImportResult Composer::Clone(uint32_t source, uint32_t parent, uint32_t& output) noexcept
        {
            // the native live walk expands its own source when attached below
            // itself. reject that topology before allocating or appending copies.
            for (uint32_t ancestor = parent; ancestor != invalid; ancestor = nodes[ancestor].parent)
                if (ancestor == source) return Failure(ImportError::InvalidHierarchy, ImportObject::Node, source);
            if (stamp == invalid - 1) return Failure(ImportError::Capacity, ImportObject::Node);
            ++stamp;
            work.count = 0;
            uint32_t current = source;
            while (current != invalid)
            {
                UVSR_COMPOSE_TRY(work.Push(current, budget));
                if (work.count > nodes.count) return Failure(ImportError::Cycle, ImportObject::Node, current);
                if (nodes[current].child != invalid) current = nodes[current].child;
                else
                {
                    while (current != source && nodes[current].sibling == invalid) current = nodes[current].parent;
                    current = current == source ? invalid : nodes[current].sibling;
                }
            }
            for (uint32_t i = 0; i < work.count; ++i)
            {
                const uint32_t original = work[i];
                const uint32_t targetParent = original == source ? parent : nodes[nodes[original].parent].cloneIndex;
                uint32_t destination = invalid;
                UVSR_COMPOSE_TRY(AddNode(nodes[original], targetParent, destination));
                nodes[original].cloneStamp = stamp;
                nodes[original].cloneIndex = destination;
                if (original == source) output = destination;
            }
            for (uint32_t i = 0; i < work.count; ++i)
            {
                const uint32_t original = work[i];
                UVSR_COMPOSE_TRY(CloneLeaf(original, nodes[original].cloneIndex));
            }
            return {};
        }

        ImportResult Composer::CloneLeaf(uint32_t source, uint32_t destination) noexcept
        {
            const Node original = nodes[source];
            uint32_t leaf = invalid;
            switch (original.kind)
            {
            case LeafKind::None: break;
            case LeafKind::Instance:
            {
                auto value = instances[original.leaf];
                const auto range = value.joints;
                value.node = destination;
                value.canonical = value.mesh = invalid;
                value.joints.first = joints.count;
                for (uint32_t i = 0; i < range.count; ++i)
                {
                    auto joint = joints[range.first + i];
                    joint.nodeIndex = Remap(joint.nodeIndex);
                    UVSR_COMPOSE_TRY(joints.Push(joint, budget));
                }
                UVSR_COMPOSE_TRY(instances.Push(value, budget, leaf));
                break;
            }
            case LeafKind::Light:
            {
                auto value = lights[original.leaf]; value.nodeIndex = destination;
                UVSR_COMPOSE_TRY(lights.Push(value, budget, leaf));
                break;
            }
            case LeafKind::Camera:
            {
                auto value = cameras[original.leaf]; value.nodeIndex = destination;
                UVSR_COMPOSE_TRY(cameras.Push(value, budget, leaf));
                break;
            }
            case LeafKind::Animation:
            {
                const auto originalAnimation = animations[original.leaf];
                Animation value{destination, {channels.count, originalAnimation.channels.count}};
                for (uint32_t i = 0; i < originalAnimation.channels.count; ++i)
                {
                    auto channel = channels[originalAnimation.channels.first + i];
                    channel.record.nodeIndex = Remap(channel.record.nodeIndex);
                    UVSR_COMPOSE_TRY(channels.Push(channel, budget));
                }
                UVSR_COMPOSE_TRY(animations.Push(value, budget, leaf));
                break;
            }
            default: return Failure(ImportError::UnsupportedData, ImportObject::Node, source);
            }
            nodes[destination].kind = original.kind;
            nodes[destination].leaf = leaf;
            return {};
        }

        ImportResult Composer::FindNode(Text path, uint32_t& output) const noexcept
        {
            output = invalid;
            if (!path.count || !Separator(path.data[0])) return {};
            // a Windows root name (UNC or device) precedes its root directory,
            // so the retained null-context lookup treats it as unresolved.
            if (path.count >= 3 && Separator(path.data[1]) && !Separator(path.data[2])) return {};
            size_t position = 0;
            while (position < path.count && Separator(path.data[position])) ++position;
            uint32_t current = 0;
            if (position == path.count) { output = current; return {}; }
            for (;;)
            {
                const size_t start = position;
                while (position < path.count && !Separator(path.data[position])) ++position;
                const Text component{path.data + start, position - start};
                if (Equal(component, Literal("..")))
                {
                    if (nodes[current].parent == invalid) return Failure(ImportError::InvalidHierarchy, ImportObject::Node, current);
                    current = nodes[current].parent;
                }
                else
                {
                    uint32_t child = nodes[current].child;
                    while (child != invalid && !Equal(nodes[child].name, component)) child = nodes[child].sibling;
                    if (child == invalid) return {};
                    current = child;
                }
                if (position == path.count) { output = current; return {}; }
                while (position < path.count && Separator(path.data[position])) ++position;
                // a final separator produces the native empty path component.
            }
        }

        ImportResult Composer::ReplaceLeaf(uint32_t entry, uint32_t node) noexcept
        {
            const auto& source = description.nodes[entry];
            uint32_t leaf = invalid;
            if (source.leafKind == LeafKind::Light)
            {
                auto value = description.lights[source.leaf]; value.nodeIndex = node;
                UVSR_COMPOSE_TRY(lights.Push(value, budget, leaf));
            }
            else if (source.leafKind == LeafKind::Camera)
            {
                auto value = description.cameras[source.leaf]; value.nodeIndex = node;
                UVSR_COMPOSE_TRY(cameras.Push(value, budget, leaf));
            }
            else return {};
            nodes[node].kind = source.leafKind;
            nodes[node].leaf = leaf;
            return {};
        }

        ImportResult Composer::Graph() noexcept
        {
            uint32_t entry = 0, open = invalid;
            while (entry < description.counts.nodes)
            {
                while (open != invalid && description.nodes[open].subtreeEnd == entry)
                {
                    UVSR_COMPOSE_TRY(ReplaceLeaf(open, descriptionNodes[open].value));
                    open = description.nodes[open].parent;
                }
                const auto& source = description.nodes[entry];
                uint32_t parent = source.parent == invalid ? 0 : descriptionNodes[source.parent].value;
                if (source.hasParent)
                {
                    UVSR_COMPOSE_TRY(FindNode(DescriptionText(source.parentPath), parent));
                    if (parent == invalid)
                    {
                        ++stats.skippedParentSubtrees;
                        entry = source.subtreeEnd;
                        continue;
                    }
                }
                uint32_t node = invalid;
                if (source.model != invalid)
                {
                    if (options.modelAvailability.count &&
                        options.modelAvailability.data[source.model] == ImportModelAvailability::Unavailable)
                    {
                        ++stats.skippedModelSubtrees;
                        entry = source.subtreeEnd;
                        continue;
                    }
                    if (modelPlans[source.model].root == invalid) UVSR_COMPOSE_TRY(CopyModel(source.model, parent, node));
                    else UVSR_COMPOSE_TRY(Clone(modelPlans[source.model].root, parent, node));
                }
                else UVSR_COMPOSE_TRY(AddNode({}, parent, node));
                nodes[node].name = DescriptionText(source.name);
                if (source.transformFlags & DescriptionTranslation) memcpy(nodes[node].transform.translation, source.transform.translation, sizeof(source.transform.translation));
                if (source.transformFlags & DescriptionRotation) memcpy(nodes[node].transform.rotation, source.transform.rotation, sizeof(source.transform.rotation));
                if (source.transformFlags & DescriptionScaling) memcpy(nodes[node].transform.scaling, source.transform.scaling, sizeof(source.transform.scaling));
                if (source.transformFlags) nodes[node].hasTransform = true;
                descriptionNodes[entry].value = node;
                open = entry;
                ++entry;
            }
            while (open != invalid)
            {
                UVSR_COMPOSE_TRY(ReplaceLeaf(open, descriptionNodes[open].value));
                open = description.nodes[open].parent;
            }
            return {};
        }

        ImportResult Composer::SelectGroup(uint32_t model, uint32_t group, uint32_t& output) noexcept
        {
            const auto& plan = modelPlans[model];
            uint32_t& selected = groupMap[plan.groupMap + group].value;
            if (selected != invalid) { output = selected; return {}; }
            const auto value = models.data[model].geometry.Buffer(group);
            if (value.skinInstanceIndex != invalid || value.indexOwner >= plan.scene.bufferGroups.count)
                return Failure(ImportError::InvalidState, ImportObject::Buffer, group);
            UVSR_COMPOSE_TRY(groups.Push({model, group}, budget, selected));
            output = selected;
            if (value.indexOwner == group) groups[selected].indexOwner = selected;
            else
            {
                const auto owner = models.data[model].geometry.Buffer(value.indexOwner);
                if (owner.indexOwner != value.indexOwner || owner.skinInstanceIndex != invalid)
                    return Failure(ImportError::InvalidState, ImportObject::Buffer, group);
                uint32_t& ownerIndex = groupMap[plan.groupMap + value.indexOwner].value;
                if (ownerIndex == invalid)
                {
                    UVSR_COMPOSE_TRY(groups.Push({model, value.indexOwner}, budget, ownerIndex));
                    groups[ownerIndex].indexOwner = ownerIndex;
                }
                groups[selected].indexOwner = ownerIndex;
            }
            return {};
        }

        ImportResult Composer::SelectStaticMesh(uint32_t model, uint32_t mesh, uint32_t& output) noexcept
        {
            const auto& plan = modelPlans[model];
            uint32_t& selected = meshMap[plan.meshMap + mesh].value;
            if (selected != invalid) { output = selected; return {}; }
            const auto& original = plan.scene.meshes.data[mesh];
            if (original.skinPrototypeIndex != invalid) return Failure(ImportError::InvalidState, ImportObject::Mesh, mesh);
            Mesh value; value.source = {model, mesh};
            UVSR_COMPOSE_TRY(SelectGroup(model, original.bufferGroupIndex, value.group));
            UVSR_COMPOSE_TRY(meshes.Push(value, budget, selected));
            output = selected;
            for (uint32_t i = 0; i < original.geometries.count; ++i)
                UVSR_COMPOSE_TRY(SelectMaterial(model, plan.scene.geometries.data[original.geometries.first + i].materialIndex));
            return {};
        }

        ImportResult Composer::SelectMesh(uint32_t model, uint32_t mesh, uint32_t instance, uint32_t& output) noexcept
        {
            const auto& plan = modelPlans[model];
            const auto& original = plan.scene.meshes.data[mesh];
            if (original.skinPrototypeIndex == invalid) return SelectStaticMesh(model, mesh, output);
            Mesh value; value.source = {model, mesh};
            UVSR_COMPOSE_TRY(meshes.Push(value, budget, output));
            uint32_t prototype = invalid;
            UVSR_COMPOSE_TRY(SelectStaticMesh(model, original.skinPrototypeIndex, prototype));
            meshes[output].prototype = prototype;
            const auto buffer = models.data[model].geometry.Buffer(original.bufferGroupIndex);
            if (buffer.skinInstanceIndex == invalid || buffer.indexOwner >= plan.scene.bufferGroups.count)
                return Failure(ImportError::InvalidState, ImportObject::Buffer, original.bufferGroupIndex);
            uint32_t indexOwner = invalid, group = invalid;
            UVSR_COMPOSE_TRY(SelectGroup(model, buffer.indexOwner, indexOwner));
            UVSR_COMPOSE_TRY(groups.Push({model, original.bufferGroupIndex, indexOwner, instance}, budget, group));
            meshes[output].group = group;
            for (uint32_t i = 0; i < original.geometries.count; ++i)
                UVSR_COMPOSE_TRY(SelectMaterial(model, plan.scene.geometries.data[original.geometries.first + i].materialIndex));
            return {};
        }

        ImportResult Composer::SelectTexture(uint32_t model, uint32_t texture) noexcept
        {
            if (texture == invalid) return {};
            const auto& owner = models.data[model].textures;
            const auto source = owner.Texture(texture);
            if (source.requestIndex >= owner.TextureRequestCount()) return Failure(ImportError::InvalidIndex, ImportObject::Texture, texture);
            const uint32_t selected = requestMap[modelPlans[model].requestMap + source.requestIndex].value;
            if (selected >= textures.count) return Failure(ImportError::InvalidState, ImportObject::Texture, texture);
            if (textures[selected].canonical == invalid)
            {
                uint32_t canonical = invalid;
                UVSR_COMPOSE_TRY(textureOrder.Push(selected, budget, canonical));
                textures[selected].canonical = canonical;
                UVSR_COMPOSE_TRY(textureSources.Push({model, texture}, budget));
            }
            return {};
        }

        ImportResult Composer::SelectMaterial(uint32_t model, uint32_t material) noexcept
        {
            const auto& plan = modelPlans[model];
            if (material >= plan.scene.materials.count) return Failure(ImportError::InvalidIndex, ImportObject::Material, material);
            uint32_t& selected = materialMap[plan.materialMap + material].value;
            if (selected != invalid) return {};
            UVSR_COMPOSE_TRY(materials.Push({model, material}, budget, selected));
            const auto& value = plan.scene.materials.data[material];
            for (uint32_t slot = 0; slot < uint32_t(RendererSceneMaterialTextureSlot::Count); ++slot)
            {
                UVSR_COMPOSE_TRY(SelectTexture(model, value.values.textures[slot]));
                UVSR_COMPOSE_TRY(SelectTexture(model, value.originalValues.textures[slot]));
            }
            return {};
        }

        ImportResult Composer::Resources() noexcept
        {
            for (uint32_t i = 0; i < instances.count; ++i)
            {
                auto& instance = instances[i];
                if (!Live(instance.node, LeafKind::Instance, i)) continue;
                instance.canonical = counts.instances;
                if (!Add(counts.instances, 1) || !Add(counts.joints, instance.joints.count)) return Failure(ImportError::Capacity, ImportObject::Skin, i);
                nodes[instance.node].finalLeaf = instance.canonical;
                const auto& source = modelPlans[instance.source.model].scene.instances.data[instance.source.index];
                UVSR_COMPOSE_TRY(SelectMesh(instance.source.model, source.meshIndex, instance.canonical, instance.mesh));
            }
            return {};
        }

        ImportResult Composer::DescriptionAnimations() noexcept
        {
            uint32_t container = invalid;
            for (uint32_t a = 0; a < description.counts.animations; ++a)
            {
                const auto& source = description.animations[a];
                const uint32_t firstChannel = channels.count;
                for (uint32_t c = 0; c < source.channels.count; ++c)
                {
                    const auto& input = description.channels[source.channels.first + c];
                    uint32_t sampler = invalid;
                    for (uint32_t t = 0; t < input.targets.count; ++t)
                    {
                        const Text path = DescriptionText(description.targets[input.targets.first + t]);
                        Channel channel;
                        channel.record.attribute = input.attribute;
                        if (path.count >= 9 && memcmp(path.data, "material:", 9) == 0)
                        {
                            const Text name{path.data + 9, path.count - 9};
                            uint32_t found = invalid, matches = 0;
                            for (uint32_t m = 0; m < materials.count; ++m)
                            {
                                const auto ref = materials[m];
                                const auto& scene = modelPlans[ref.model].scene;
                                if (!Equal(RendererSceneText(scene, scene.materials.data[ref.index].name), name)) continue;
                                if (found == invalid) found = m;
                                ++matches;
                            }
                            if (found == invalid) { ++stats.ignoredAnimationTargets; continue; }
                            if (matches > 1) ++stats.ambiguousMaterialTargets;
                            channel.record.materialIndex = found;
                            channel.record.attribute = RendererSceneAnimationAttribute::LeafProperty;
                        }
                        else
                        {
                            UVSR_COMPOSE_TRY(FindNode(path, channel.record.nodeIndex));
                            if (channel.record.nodeIndex == invalid) { ++stats.ignoredAnimationTargets; continue; }
                        }
                        if (channel.record.attribute == RendererSceneAnimationAttribute::LeafProperty)
                            channel.property = DescriptionText(input.property);
                        if (sampler == invalid)
                        {
                            Sampler value;
                            value.interpolation = input.sampler.interpolation;
                            if (input.sampler.keyframes.count) value.keys = {description.keyframes + input.sampler.keyframes.first, input.sampler.keyframes.count};
                            UVSR_COMPOSE_TRY(samplers.Push(value, budget, sampler));
                        }
                        channel.record.samplerIndex = sampler;
                        UVSR_COMPOSE_TRY(channels.Push(channel, budget));
                    }
                }
                const uint32_t count = channels.count - firstChannel;
                if (!count) continue;
                if (container == invalid)
                {
                    Node node; node.name = Literal("Animations");
                    UVSR_COMPOSE_TRY(AddNode(node, 0, container));
                }
                Node node; node.name = DescriptionText(source.name);
                uint32_t destination = invalid, leaf = invalid;
                UVSR_COMPOSE_TRY(AddNode(node, container, destination));
                UVSR_COMPOSE_TRY(animations.Push({destination, {firstChannel, count}}, budget, leaf));
                nodes[destination].kind = LeafKind::Animation;
                nodes[destination].leaf = leaf;
            }
            return {};
        }

        ImportResult Composer::RuntimeLights() noexcept
        {
            if (!options.runtimeLights.enabled) return {};
            for (uint32_t l = 0; l < lights.count; ++l)
            {
                auto& light = lights[l];
                if (!Live(light.nodeIndex, LeafKind::Light, l)) continue;
                auto& node = nodes[light.nodeIndex];
                node.name = NormalizeSceneLightName(node.name);
                if (sunNode == invalid && light.kind == RendererSceneLightKind::Directional)
                {
                    sunNode = light.nodeIndex;
                    light.values.irradiance = options.runtimeLights.sun.values.irradiance;
                    light.values.angularSize = options.runtimeLights.sun.values.angularSize;
                }
            }
            const auto append = [this](const ImportRuntimeLightSpec& spec, uint32_t& destination) noexcept -> ImportResult
            {
                Node node;
                node.name = spec.name;
                node.transform = spec.transform;
                node.hasTransform = true;
                UVSR_COMPOSE_TRY(AddNode(node, 0, destination));
                uint32_t leaf = invalid;
                UVSR_COMPOSE_TRY(lights.Push({destination, spec.kind, spec.values}, budget, leaf));
                nodes[destination].kind = LeafKind::Light;
                nodes[destination].leaf = leaf;
                return {};
            };
            if (sunNode == invalid) UVSR_COMPOSE_TRY(append(options.runtimeLights.sun, sunNode));
            return append(options.runtimeLights.flashlight, flashlightNode);
        }

        ImportResult Composer::FinalCounts() noexcept
        {
            counts.nodes = nodes.count;
            counts.meshes = meshes.count;
            counts.materials = materials.count;
            counts.textures = textureOrder.count;
            counts.bufferGroups = groups.count;
            const auto text = [this](Text value) noexcept -> ImportResult
            { return Add(counts.stringBytes, value.count) ? ImportResult{} : Failure(ImportError::Capacity); };
            for (uint32_t n = 0; n < nodes.count; ++n) UVSR_COMPOSE_TRY(text(nodes[n].name));
            for (uint32_t m = 0; m < meshes.count; ++m)
            {
                const auto source = meshes[m].source;
                const auto& scene = modelPlans[source.model].scene;
                const auto& mesh = scene.meshes.data[source.index];
                if (!Add(counts.geometries, mesh.geometries.count)) return Failure(ImportError::Capacity, ImportObject::Mesh, m);
                UVSR_COMPOSE_TRY(text(RendererSceneText(scene, mesh.name)));
            }
            for (uint32_t g = 0; g < groups.count; ++g)
            {
                const auto& source = groups[g];
                if (!Add(counts.morphRanges, modelPlans[source.model].scene.bufferGroups.data[source.group].morphRanges.count))
                    return Failure(ImportError::Capacity, ImportObject::Buffer, g);
            }
            for (uint32_t m = 0; m < materials.count; ++m)
            {
                const auto source = materials[m];
                const auto& scene = modelPlans[source.model].scene;
                const auto& material = scene.materials.data[source.index];
                UVSR_COMPOSE_TRY(text(RendererSceneText(scene, material.name)));
                UVSR_COMPOSE_TRY(text(RendererSceneText(scene, material.modelFileName)));
            }
            for (uint32_t t = 0; t < textureOrder.count; ++t)
            {
                const auto image = textures[textureOrder[t]].image;
                if (image == invalid) continue;
                UVSR_COMPOSE_TRY(text(images[image].value.path));
                UVSR_COMPOSE_TRY(text(images[image].value.mimeType));
            }
            for (uint32_t l = 0; l < lights.count; ++l)
                if (Live(lights[l].nodeIndex, LeafKind::Light, l)) nodes[lights[l].nodeIndex].finalLeaf = counts.lights++;
            for (uint32_t c = 0; c < cameras.count; ++c)
                if (Live(cameras[c].nodeIndex, LeafKind::Camera, c)) nodes[cameras[c].nodeIndex].finalLeaf = counts.cameras++;
            for (uint32_t a = 0; a < animations.count; ++a)
            {
                const auto& animation = animations[a];
                if (!Live(animation.node, LeafKind::Animation, a)) continue;
                nodes[animation.node].finalLeaf = counts.animations++;
                if (!Add(counts.channels, animation.channels.count)) return Failure(ImportError::Capacity, ImportObject::Animation, a);
                for (uint32_t c = 0; c < animation.channels.count; ++c)
                {
                    const auto& channel = channels[animation.channels.first + c];
                    UVSR_COMPOSE_TRY(text(channel.property));
                    auto& sampler = samplers[channel.record.samplerIndex];
                    if (sampler.canonical == invalid)
                    {
                        UVSR_COMPOSE_TRY(samplerOrder.Push(channel.record.samplerIndex, budget, sampler.canonical));
                        if (!Add(counts.keyframes, sampler.keys.count)) return Failure(ImportError::Capacity, ImportObject::Animation, a);
                    }
                }
            }
            counts.samplers = samplerOrder.count;
            return {};
        }

        ImportResult Composer::Write(RendererScene& scene) noexcept
        {
            UVSR_COMPOSE_WRITE(scene.Prepare(counts));
            uint32_t stringOffset = 0;
            const auto text = [&scene, &stringOffset](Text value, RendererSceneString& output) noexcept -> ImportResult
            {
                output = {stringOffset, uint32_t(value.count)};
                UVSR_COMPOSE_WRITE(scene.WriteStrings(stringOffset, value));
                stringOffset += uint32_t(value.count);
                return {};
            };
            for (uint32_t n = 0; n < nodes.count; ++n)
            {
                const auto& input = nodes[n];
                RendererSceneNode value;
                UVSR_COMPOSE_TRY(text(input.name, value.name));
                value.parentIndex = input.parent;
                value.firstChildIndex = input.child;
                value.nextSiblingIndex = input.sibling;
                value.leafKind = input.kind;
                value.leafIndex = input.finalLeaf;
                value.transform = input.transform;
                value.hasLocalTransform = input.hasTransform;
                UVSR_COMPOSE_WRITE(scene.Write(n, value));
            }
            uint32_t geometryOffset = 0;
            for (uint32_t m = 0; m < meshes.count; ++m)
            {
                const auto& input = meshes[m];
                const auto& plan = modelPlans[input.source.model];
                auto value = plan.scene.meshes.data[input.source.index];
                const auto sourceRange = value.geometries;
                UVSR_COMPOSE_TRY(text(RendererSceneText(plan.scene, value.name), value.name));
                value.bufferGroupIndex = input.group;
                value.skinPrototypeIndex = input.prototype;
                value.geometries.first = geometryOffset;
                UVSR_COMPOSE_WRITE(scene.Write(m, value));
                for (uint32_t g = 0; g < sourceRange.count; ++g)
                {
                    auto geometry = plan.scene.geometries.data[sourceRange.first + g];
                    geometry.materialIndex = materialMap[plan.materialMap + geometry.materialIndex].value;
                    UVSR_COMPOSE_WRITE(scene.Write(geometryOffset++, geometry));
                }
            }
            uint32_t morphOffset = 0;
            for (uint32_t g = 0; g < groups.count; ++g)
            {
                const auto& input = groups[g];
                const auto& source = modelPlans[input.model].scene;
                auto value = source.bufferGroups.data[input.group];
                const auto sourceRange = value.morphRanges;
                value.morphRanges.first = morphOffset;
                UVSR_COMPOSE_WRITE(scene.Write(g, value));
                for (uint32_t m = 0; m < sourceRange.count; ++m)
                    UVSR_COMPOSE_WRITE(scene.Write(morphOffset++, source.morphRanges.data[sourceRange.first + m]));
            }
            uint32_t jointOffset = 0;
            for (uint32_t i = 0; i < instances.count; ++i)
            {
                const auto& input = instances[i];
                if (!Live(input.node, LeafKind::Instance, i)) continue;
                RendererSceneInstance value{input.node, input.mesh, {jointOffset, input.joints.count}};
                UVSR_COMPOSE_WRITE(scene.Write(input.canonical, value));
                for (uint32_t j = 0; j < input.joints.count; ++j)
                    UVSR_COMPOSE_WRITE(scene.Write(jointOffset++, joints[input.joints.first + j]));
            }
            const auto textureIndex = [this](uint32_t model, uint32_t source) noexcept -> uint32_t
            {
                if (source == invalid) return invalid;
                const auto request = models.data[model].textures.Texture(source).requestIndex;
                return textures[requestMap[modelPlans[model].requestMap + request].value].canonical;
            };
            for (uint32_t m = 0; m < materials.count; ++m)
            {
                const auto input = materials[m];
                const auto& source = modelPlans[input.model].scene;
                auto value = source.materials.data[input.index];
                value.selectionId = m;
                UVSR_COMPOSE_TRY(text(RendererSceneText(source, value.name), value.name));
                UVSR_COMPOSE_TRY(text(RendererSceneText(source, value.modelFileName), value.modelFileName));
                for (uint32_t slot = 0; slot < uint32_t(RendererSceneMaterialTextureSlot::Count); ++slot)
                {
                    value.values.textures[slot] = textureIndex(input.model, value.values.textures[slot]);
                    value.originalValues.textures[slot] = textureIndex(input.model, value.originalValues.textures[slot]);
                }
                UVSR_COMPOSE_WRITE(scene.Write(m, value));
            }
            for (uint32_t t = 0; t < textureOrder.count; ++t)
            {
                const auto source = textureSources[t];
                const auto& original = modelPlans[source.model].scene.textures.data[source.index];
                RendererSceneTexture value;
                value.alpha = original.alpha;
                value.originalBitsPerPixel = original.originalBitsPerPixel;
                const uint32_t image = textures[textureOrder[t]].image;
                if (image != invalid)
                {
                    UVSR_COMPOSE_TRY(text(images[image].value.path, value.path));
                    UVSR_COMPOSE_TRY(text(images[image].value.mimeType, value.mimeType));
                }
                UVSR_COMPOSE_WRITE(scene.Write(t, value));
            }
            for (uint32_t l = 0; l < lights.count; ++l)
                if (Live(lights[l].nodeIndex, LeafKind::Light, l)) UVSR_COMPOSE_WRITE(scene.Write(nodes[lights[l].nodeIndex].finalLeaf, lights[l]));
            for (uint32_t c = 0; c < cameras.count; ++c)
                if (Live(cameras[c].nodeIndex, LeafKind::Camera, c)) UVSR_COMPOSE_WRITE(scene.Write(nodes[cameras[c].nodeIndex].finalLeaf, cameras[c]));
            uint32_t channelOffset = 0;
            for (uint32_t a = 0; a < animations.count; ++a)
            {
                const auto& input = animations[a];
                if (!Live(input.node, LeafKind::Animation, a)) continue;
                RendererSceneAnimation value{input.node, {channelOffset, input.channels.count}};
                UVSR_COMPOSE_WRITE(scene.Write(nodes[input.node].finalLeaf, value));
                for (uint32_t c = 0; c < input.channels.count; ++c)
                {
                    const auto& source = channels[input.channels.first + c];
                    auto channel = source.record;
                    channel.samplerIndex = samplers[channel.samplerIndex].canonical;
                    UVSR_COMPOSE_TRY(text(source.property, channel.property));
                    UVSR_COMPOSE_WRITE(scene.Write(channelOffset++, channel));
                }
            }
            uint32_t keyframeOffset = 0;
            for (uint32_t s = 0; s < samplerOrder.count; ++s)
            {
                const auto& source = samplers[samplerOrder[s]];
                RendererSceneAnimationSampler value{{keyframeOffset, uint32_t(source.keys.count)}, source.interpolation};
                UVSR_COMPOSE_WRITE(scene.Write(s, value));
                for (size_t k = 0; k < source.keys.count; ++k)
                    UVSR_COMPOSE_WRITE(scene.Write(keyframeOffset++, source.keys.data[k]));
            }
            if (stringOffset != counts.stringBytes || geometryOffset != counts.geometries || morphOffset != counts.morphRanges ||
                jointOffset != counts.joints || channelOffset != counts.channels || keyframeOffset != counts.keyframes)
                return Failure(ImportError::InvalidState);
            const size_t required = scene.SealWorkspaceBytes();
            if (required >= invalid) return Failure(ImportError::Workspace);
            List<uint8_t> workspace;
            UVSR_COMPOSE_TRY(workspace.Resize(uint32_t(required), budget));
            UVSR_COMPOSE_WRITE(scene.Seal(0, {workspace.data, workspace.count}));
            UVSR_COMPOSE_WRITE(scene.Publish(options.generation));
            UVSR_COMPOSE_WRITE(scene.AdvancePreviousTransforms());
            return {};
        }

        ImportResult Composer::CopyTextures(ImportTextures& output) noexcept
        {
            List<ImportImageView> ownerImages;
            List<ImportTextureView> ownerRequests;
            List<ImportTextureSwizzle> ownerSwizzles;
            UVSR_COMPOSE_TRY(ownerImages.Resize(images.count, budget));
            UVSR_COMPOSE_TRY(ownerRequests.Resize(textures.count, budget));
            UVSR_COMPOSE_TRY(ownerSwizzles.Resize(swizzles.count, budget));
            for (uint32_t i = 0; i < images.count; ++i) ownerImages[i] = images[i].value;
            uint32_t swizzleOffset = 0;
            for (uint32_t t = 0; t < textures.count; ++t)
            {
                const auto& source = textures[t];
                auto& request = ownerRequests[t];
                request.imageIndex = source.image;
                request.forceSRGB = source.srgb;
                request.requestIndex = t;
                if (source.swizzleCount) request.swizzles = {ownerSwizzles.data + swizzleOffset, source.swizzleCount};
                uint32_t next = source.firstSwizzle;
                for (uint32_t s = 0; s < source.swizzleCount; ++s)
                {
                    ownerSwizzles[swizzleOffset++] = swizzles[next].value;
                    next = swizzles[next].next;
                }
                if (next != invalid) return Failure(ImportError::InvalidState, ImportObject::Texture, t);
            }
            if (swizzleOffset != swizzles.count) return Failure(ImportError::InvalidState, ImportObject::Texture);
            return ImportCompositionAccess::CopyTextures(ownerImages.View(), ownerRequests.View(), textureOrder.View(), options.maxImageBytes, output);
        }
    }

    ImportResult ComposeImportScene(const ImportSceneDescription& description, ArrayView<ImportModel> models,
        const ImportCompositionOptions& options, RendererScene& scene, ImportGeometry& geometry,
        ImportTextures& textures, ImportCompositionStats* stats, ImportRuntimeLightIds* runtimeLights) noexcept
    {
        const auto* source = ImportDescriptionAccess::State(description);
        if (!source || !options.generation || scene.StorageBytes() || !ImportCompositionAccess::Empty(geometry, textures) || !models.IsValid())
            return Failure(ImportError::InvalidState);
        UVSR_COMPOSE_TRY(ValidateImportRuntimeLights(options.runtimeLights));
        for (size_t m = 0; m < models.count; ++m)
            if (&models.data[m].scene == &scene || &models.data[m].geometry == &geometry || &models.data[m].textures == &textures)
                return Failure(ImportError::InvalidOutput);
        Composer composer(*source, models, options);
        UVSR_COMPOSE_TRY(composer.PlanInputs());
        UVSR_COMPOSE_TRY(composer.PlanImages());
        UVSR_COMPOSE_TRY(composer.Graph());
        UVSR_COMPOSE_TRY(composer.Resources());
        UVSR_COMPOSE_TRY(composer.DescriptionAnimations());
        UVSR_COMPOSE_TRY(composer.RuntimeLights());
        UVSR_COMPOSE_TRY(composer.FinalCounts());
        RendererScene prepared;
        ImportGeometry preparedGeometry;
        ImportTextures preparedTextures;
        UVSR_COMPOSE_TRY(composer.Write(prepared));
        UVSR_COMPOSE_TRY(composer.CopyTextures(preparedTextures));
        UVSR_COMPOSE_TRY(ImportCompositionAccess::TakeGeometry(models, composer.groups.View(), prepared.View(),
            options.maxGeometryBytes, composer.budget.peak, preparedGeometry));
        scene = static_cast<RendererScene&&>(prepared);
        geometry = static_cast<ImportGeometry&&>(preparedGeometry);
        textures = static_cast<ImportTextures&&>(preparedTextures);
        for (size_t m = 0; m < models.count; ++m)
        {
            models.data[m].scene.Reset();
            models.data[m].geometry.Reset();
            models.data[m].textures.Reset();
        }
        if (stats) { composer.stats.peakScratchBytes = composer.budget.peak; *stats = composer.stats; }
        if (runtimeLights)
        {
            *runtimeLights = options.runtimeLights.enabled ? ImportRuntimeLightIds{
                {options.generation, composer.nodes[composer.sunNode].finalLeaf},
                {options.generation, composer.nodes[composer.flashlightNode].finalLeaf}} : ImportRuntimeLightIds{};
        }
        return {};
    }

#undef UVSR_COMPOSE_WRITE
#undef UVSR_COMPOSE_TRY
}
