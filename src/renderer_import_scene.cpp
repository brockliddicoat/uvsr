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

#include "renderer_import_scene.h"
#include "scene_light_names.h"
#include "renderer_import_path.h"
#include "import/renderer_import_private.h"
#include "import/renderer_import_composition_private.h"

#include <float.h>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

namespace uvsr
{
    namespace
    {
        constexpr uint32_t invalid = InvalidSceneIndex;
        using Attribute = RendererSceneVertexAttribute;
        using Float3 = gpu_contract::Float3;
        using Matrix4 = gpu_contract::Float4x4;

        ImportResult Failure(ImportError error, ImportObject object = ImportObject::Document,
            size_t index = SIZE_MAX) noexcept { return {error, object, index}; }

        ImportResult SceneResult(RendererSceneResult result) noexcept
        {
            if (result.Succeeded()) return {};
            switch (result.error)
            {
            case RendererSceneError::Allocation: return Failure(ImportError::OutOfMemory, ImportObject::Scene);
            case RendererSceneError::Capacity: return Failure(ImportError::Capacity, ImportObject::Scene);
            case RendererSceneError::Workspace: return Failure(ImportError::Workspace, ImportObject::Scene);
            default: return Failure(ImportError::InvalidData, ImportObject::Scene, result.index);
            }
        }

#define UVSR_IMPORT_TRY(expression) do { const ImportResult importResult = (expression); if (!importResult) return importResult; } while (false)
#define UVSR_IMPORT_WRITE(expression) UVSR_IMPORT_TRY(SceneResult(expression))

        bool AddCount(uint32_t& count, size_t value) noexcept
        {
            if (value > UINT32_MAX - count) return false;
            count += uint32_t(value);
            return true;
        }

        struct Budget
        {
            size_t bytes = 0;
            size_t limit = SIZE_MAX;
            ImportError exhausted = ImportError::Workspace;

            ImportResult Add(size_t count, size_t stride) noexcept
            {
                if (stride && count > size_t(PTRDIFF_MAX) / stride) return Failure(ImportError::Overflow);
                const size_t size = count * stride;
                if (size > SIZE_MAX - bytes) return Failure(ImportError::Overflow);
                if (bytes > limit || size > limit - bytes) return Failure(exhausted);
                bytes += size;
                return {};
            }
        };

        // fixed, import-local storage. placement construction establishes typed
        // scalar lifetimes; no accessor writes through reinterpreted byte storage.
        template<class T> struct Fixed
        {
            T* data = nullptr;
            size_t count = 0;
            Fixed() noexcept = default;
            Fixed(const Fixed&) = delete;
            Fixed& operator=(const Fixed&) = delete;
            ~Fixed() noexcept { free(data); }
            ImportResult Allocate(size_t requested, Budget& budget) noexcept
            {
                static_assert(std::is_trivially_destructible_v<T> && std::is_nothrow_default_constructible_v<T>);
                if (data) return Failure(ImportError::InvalidState);
                UVSR_IMPORT_TRY(budget.Add(requested, sizeof(T)));
                if (!requested) return {};
                data = static_cast<T*>(ImportAllocate(requested * sizeof(T)));
                if (!data) return Failure(ImportError::OutOfMemory);
                count = requested;
                for (size_t i = 0; i < count; ++i) new (&data[i]) T{};
                return {};
            }
            T& operator[](size_t index) noexcept { return data[index]; }
            const T& operator[](size_t index) const noexcept { return data[index]; }
        };

        struct GeometryGroup
        {
            uint8_t* indices = nullptr;
            uint8_t* vertices = nullptr;
            uint8_t* morphs = nullptr;
            size_t indexBytes = 0;
            size_t vertexBytes = 0;
            size_t morphBytes = 0;
            uint32_t indexOwner = invalid;
            RendererSceneRange jointMatrices;
            uint32_t skinInstance = invalid;
        };

        struct NodePlan
        {
            uint32_t parent = invalid;
            uint32_t firstChild = invalid;
            uint32_t nextSibling = invalid;
            uint32_t canonical = invalid;
            uint32_t instance = invalid;
            uint32_t mesh = invalid;
            uint32_t jointStamp = invalid;
            uint32_t camera = invalid;
            uint32_t cameraNode = invalid;
            uint32_t cameraNumber = 0;
            uint32_t light = invalid;
            uint32_t lightNode = invalid;
            uint32_t animationStamp[3]{invalid, invalid, invalid};
            uint8_t color = 0;
        };

        struct AnimationPlan
        {
            uint32_t samplerOffset = 0;
            uint32_t canonical = invalid;
            RendererSceneAnimation record;
        };

        struct AnimationSamplerPlan
        {
            size_t input = SIZE_MAX;
            size_t output = SIZE_MAX;
            uint32_t canonical = invalid;
            uint32_t lanes = 0;
            RendererSceneAnimationSampler record;
        };

        enum InputAttribute : uint32_t { Position, Normal, Tangent, Texcoord, Joints, Weights, Radius, InputAttributeCount };
        struct PrimitivePlan
        {
            size_t accessors[InputAttributeCount]{SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
            uint32_t indexMaximum = UINT32_MAX;
            uint32_t vertexOffset = 0;
            uint32_t indexOffset = 0;
            uint32_t material = invalid;
            RendererSceneBounds bounds;
            RendererScenePrimitive type = RendererScenePrimitive::Triangles;
        };

        struct MaterialPlan
        {
            RendererSceneMaterialValues values;
            uint32_t canonical = invalid;
        };

        struct ImagePlan
        {
            RendererSceneString path;
            RendererSceneString mime;
            ArrayView<const uint8_t> bytes;
            uint32_t sourceImage = invalid;
            bool embedded = false;
        };

        struct ImageLookup
        {
            uint32_t raw = invalid;
            uint32_t preferred = invalid;
            uint32_t texture = invalid;
        };

        struct TexturePlan
        {
            uint32_t image = invalid;
            uint32_t canonical = invalid;
            uint32_t firstSwizzle = invalid;
            uint32_t lastSwizzle = invalid;
            uint32_t swizzleCount = 0;
            bool srgb = false;
        };

        struct SwizzlePlan
        {
            ImportTextureSwizzle value;
            uint32_t next = invalid;
            uint32_t texture = invalid;
            uint32_t sourceTexture = invalid;
        };

        struct TexturePayload
        {
            uint32_t image = invalid;
            RendererSceneRange swizzles;
            bool srgb = false;
        };

        struct MeshPlan
        {
            uint32_t firstPrimitive = 0;
            uint32_t canonical = invalid;
            uint32_t vertexOffset = 0;
            uint32_t indexOffset = 0;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
            uint32_t firstMorph = 0;
            uint32_t morphCount = 0;
            bool skinPrototype = false;
        };

        struct MeshOrder
        {
            uint32_t source = invalid;
            uint32_t skinNode = invalid;
            uint32_t group = invalid;
            uint32_t firstGeometry = 0;
        };

        struct GroupPlan
        {
            RendererSceneBufferGroup record;
            uint32_t sourceMesh = invalid;
        };

        void Include(RendererSceneBounds& bounds, Float3 point) noexcept
        {
            if (bounds.empty) { bounds.minimum = bounds.maximum = point; bounds.empty = false; return; }
            if (point.x < bounds.minimum.x) bounds.minimum.x = point.x;
            if (point.y < bounds.minimum.y) bounds.minimum.y = point.y;
            if (point.z < bounds.minimum.z) bounds.minimum.z = point.z;
            if (point.x > bounds.maximum.x) bounds.maximum.x = point.x;
            if (point.y > bounds.maximum.y) bounds.maximum.y = point.y;
            if (point.z > bounds.maximum.z) bounds.maximum.z = point.z;
        }

        Float3 Subtract(Float3 a, Float3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
        Float3 Multiply(Float3 a, float b) noexcept { return {a.x * b, a.y * b, a.z * b}; }
        Float3 Divide(Float3 a, float b) noexcept { return {a.x / b, a.y / b, a.z / b}; }
        Float3 Cross(Float3 a, Float3 b) noexcept
        { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
        float Dot(Float3 a, Float3 b) noexcept { return a.x*b.x + a.y*b.y + a.z*b.z; }
        void Accumulate(Float3& a, Float3 b) noexcept { a.x += b.x; a.y += b.y; a.z += b.z; }

        template<class T> T ReadValue(const uint8_t* bytes) noexcept
        { T result{}; memcpy(&result, bytes, sizeof(T)); return result; }
        template<class T> void WriteValue(uint8_t* bytes, const T& value) noexcept { memcpy(bytes, &value, sizeof(T)); }

        uint32_t PackDirection(Float3 value, float w = 0) noexcept
        {
            const float scale = 127.0f / sqrtf(value.x*value.x + value.y*value.y + value.z*value.z);
            const float lanes[]{value.x * scale, value.y * scale, value.z * scale, w * scale};
            uint32_t packed = 0;
            for (uint32_t lane = 0; lane < 4; ++lane)
            {
                // the retained x64 conversion produces zero low bytes for NaN,
                // infinity and int32 overflow. make that result defined here.
                const float v = lanes[lane];
                const int32_t integer = isfinite(v) && v >= -2147483648.0f && v < 2147483648.0f ? int32_t(v) : 0;
                packed |= (uint32_t(integer) & 255u) << (lane * 8);
            }
            return packed;
        }

        ImportResult Transform(const fastgltf::Node& source, RendererSceneTransform& out) noexcept
        {
            if (const auto* trs = std::get_if<fastgltf::TRS>(&source.transform))
            {
                for (uint32_t i = 0; i < 3; ++i) { out.translation[i] = trs->translation[i]; out.scaling[i] = trs->scale[i]; }
                for (uint32_t i = 0; i < 4; ++i) out.rotation[i] = trs->rotation[i];
            }
            else if (const auto* matrix = std::get_if<fastgltf::math::fmat4x4>(&source.transform))
            {
                // glTF columns become native affine rows before its decomposition.
                double columns[3][3]{};
                for (uint32_t c = 0; c < 3; ++c)
                {
                    for (uint32_t r = 0; r < 3; ++r) columns[c][r] = (*matrix)[r][c];
                    const auto* v = columns[c];
                    out.scaling[c] = sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
                    if (out.scaling[c] > 0)
                        for (uint32_t r = 0; r < 3; ++r) columns[c][r] /= out.scaling[c];
                    out.translation[c] = (*matrix)[3][c];
                }
                if ((*matrix)[0][3] != 0 || (*matrix)[1][3] != 0 || (*matrix)[2][3] != 0 || (*matrix)[3][3] != 1)
                    return Failure(ImportError::InvalidData, ImportObject::Node);
                const auto* c0 = columns[0]; const auto* c1 = columns[1]; const auto* c2 = columns[2];
                const double determinant = (c0[1]*c1[2] - c0[2]*c1[1])*c2[0] +
                    (c0[2]*c1[0] - c0[0]*c1[2])*c2[1] + (c0[0]*c1[1] - c0[1]*c1[0])*c2[2];
                if (determinant < 0)
                {
                    out.scaling[0] = -out.scaling[0];
                    for (uint32_t r = 0; r < 3; ++r) columns[0][r] = -columns[0][r];
                }
                const double magnitudes[]{1 + c0[0] - c1[1] - c2[2], 1 - c0[0] + c1[1] - c2[2],
                    1 - c0[0] - c1[1] + c2[2], 1 + c0[0] + c1[1] + c2[2]};
                for (uint32_t i = 0; i < 4; ++i) out.rotation[i] = sqrt(magnitudes[i] > 0 ? magnitudes[i] : 0) * 0.5;
                out.rotation[0] = copysign(out.rotation[0], c2[1] - c1[2]);
                out.rotation[1] = copysign(out.rotation[1], c0[2] - c2[0]);
                out.rotation[2] = copysign(out.rotation[2], c1[0] - c0[1]);
            }
            else return Failure(ImportError::InvalidData, ImportObject::Node);
            for (uint32_t i = 0; i < 3; ++i)
                if (!isfinite(out.translation[i]) || !isfinite(out.scaling[i])) return Failure(ImportError::NonFiniteValue, ImportObject::Node);
            for (double value : out.rotation)
                if (!isfinite(value)) return Failure(ImportError::NonFiniteValue, ImportObject::Node);
            return {};
        }

        ImportResult AppendAttribute(RendererSceneBufferGroup& group, Attribute attribute, size_t count, size_t stride) noexcept
        {
            if (count > (size_t(PTRDIFF_MAX) - 15) / stride) return Failure(ImportError::Overflow);
            const size_t bytes = (count * stride + 15) & ~size_t(15);
            if (group.vertexBytes > UINT32_MAX || bytes > UINT32_MAX - group.vertexBytes)
                return Failure(ImportError::Capacity, ImportObject::Buffer);
            group.attributes[uint32_t(attribute)] = {group.vertexBytes, bytes};
            group.vertexBytes += bytes;
            return {};
        }
    }

    struct ImportGeometryState
    {
        Fixed<GeometryGroup> groups;
        Fixed<Matrix4> jointMatrices;
        size_t storageBytes = 0;
        size_t scratchBytes = 0;
        bool ownsPayloads = true;
        ~ImportGeometryState() noexcept
        {
            if (!ownsPayloads) return;
            for (size_t i = 0; i < groups.count; ++i)
            {
                auto& group = groups[i];
                if (group.indexOwner == i) free(group.indices);
                free(group.vertices);
                free(group.morphs);
            }
        }
    };

    struct ImportTexturesState
    {
        Fixed<ImportImageView> images;
        Fixed<uint32_t> textureMap;
        Fixed<TexturePayload> requests;
        Fixed<ImportTextureSwizzle> swizzles;
        Fixed<char> strings;
        Fixed<uint8_t> bytes;
        size_t storageBytes = 0;
    };

    namespace
    {
        void DestroyGeometry(ImportGeometryState* state) noexcept
        { if (state) { state->~ImportGeometryState(); free(state); } }

        struct GeometryCandidate
        {
            ImportGeometryState* state = nullptr;
            ~GeometryCandidate() noexcept { DestroyGeometry(state); }
        };

        void DestroyTextures(ImportTexturesState* state) noexcept
        { if (state) { state->~ImportTexturesState(); free(state); } }

        struct TextureCandidate
        {
            ImportTexturesState* state = nullptr;
            ~TextureCandidate() noexcept { DestroyTextures(state); }
        };

        struct Builder
        {
            const ImportDocument& document;
            const ImportState& imported;
            const fastgltf::Asset& asset;
            const ImportSceneOptions& options;
            Budget scratch;
            RendererSceneCounts counts;
            Fixed<NodePlan> nodes;
            Fixed<uint32_t> nodeOrder;
            Fixed<uint32_t> instanceOrder;
            Fixed<AnimationPlan> animations;
            Fixed<AnimationSamplerPlan> animationSamplers;
            Fixed<uint32_t> animationSamplerOrder;
            Fixed<MeshPlan> meshes;
            Fixed<PrimitivePlan> primitives;
            Fixed<MeshOrder> meshOrder;
            Fixed<GroupPlan> groups;
            Fixed<MaterialPlan> materials;
            Fixed<uint32_t> materialOrder;
            Fixed<ImagePlan> images;
            Fixed<ImageLookup> imageLookup;
            Fixed<TexturePlan> textures;
            Fixed<uint32_t> sourceTextures;
            Fixed<uint32_t> textureOrder;
            Fixed<uint32_t> textureHash;
            Fixed<SwizzlePlan> swizzles;
            Fixed<uint32_t> swizzleHash;
            Fixed<char> imageStrings;
            Fixed<char> pathWorkspace;
            Fixed<float> floats;
            Fixed<uint32_t> integers;
            Fixed<Float3> normalScratch;
            Fixed<Float3> tangentScratch;
            Fixed<Float3> bitangentScratch;
            Fixed<uint8_t> sealWorkspace;
            ArrayView<const size_t> roots;
            uint32_t sourceGroup = invalid;
            uint32_t sourceNodeCount = 1;
            uint32_t animationContainer = invalid;
            uint32_t firstAnimationNode = invalid;
            uint32_t firstRuntimeNode = invalid;
            bool appendSun = false;
            ImportRuntimeLightIds runtimeLights;
            uint32_t totalVertices = 0;
            uint32_t totalIndices = 0;
            uint32_t maxVertices = 0;
            uint32_t maxIndices = 0;
            size_t maxFloats = 0;
            uint32_t stringOffset = 0;
            uint32_t skinCount = 0;
            bool hasJoints = false;
            bool hasRadius = false;
            bool generatesTangents = false;
            uint32_t imageCount = 0;
            uint32_t textureCount = 0;
            uint32_t swizzleCount = 0;
            uint32_t imageStringBytes = 0;

            Builder(const ImportDocument& doc, const ImportState& state, const ImportSceneOptions& opts) noexcept
                : document(doc), imported(state), asset(state.asset), options(opts),
                  scratch{0, opts.maxScratchBytes, ImportError::Workspace} {}

            ImportResult PlanNodes() noexcept
            {
                if (asset.nodes.size() >= UINT32_MAX || asset.meshes.size() >= UINT32_MAX || asset.skins.size() >= UINT32_MAX ||
                    asset.cameras.size() >= UINT32_MAX || asset.lights.size() >= UINT32_MAX)
                    return Failure(ImportError::Capacity);
                UVSR_IMPORT_TRY(nodes.Allocate(asset.nodes.size(), scratch));
                UVSR_IMPORT_TRY(nodeOrder.Allocate(asset.nodes.size() + 1, scratch));
                UVSR_IMPORT_TRY(instanceOrder.Allocate(asset.nodes.size(), scratch));
                counts.nodes = 1;
                for (uint32_t i = 0; i < nodes.count; ++i)
                {
                    const auto& source = asset.nodes[i];
                    if (source.meshIndex && *source.meshIndex >= asset.meshes.size()) return Failure(ImportError::InvalidIndex, ImportObject::Mesh, i);
                    if (source.skinIndex && (*source.skinIndex >= asset.skins.size() || !source.meshIndex))
                        return Failure(ImportError::InvalidIndex, ImportObject::Skin, i);
                    if (source.cameraIndex && *source.cameraIndex >= asset.cameras.size()) return Failure(ImportError::InvalidIndex, ImportObject::Camera, i);
                    if ((source.lightIndex && *source.lightIndex >= asset.lights.size()) ||
                        ((imported.metadata.nodeFlags[i] & ImportNodeLight) && !source.lightIndex))
                        return Failure(ImportError::InvalidIndex, ImportObject::Light, i);
                    uint32_t previous = invalid;
                    for (size_t child : source.children)
                    {
                        if (child >= nodes.count) return Failure(ImportError::InvalidIndex, ImportObject::Node, i);
                        if (nodes[child].parent != invalid) return Failure(ImportError::InvalidHierarchy, ImportObject::Node, child);
                        nodes[child].parent = i;
                        if (previous == invalid) nodes[i].firstChild = uint32_t(child);
                        else nodes[previous].nextSibling = uint32_t(child);
                        previous = uint32_t(child);
                    }
                }
                // single-parent links permit a linear color walk without a stack.
                // validate the entire asset, including nodes outside the chosen scene.
                for (uint32_t start = 0; start < nodes.count; ++start)
                {
                    if (nodes[start].color) continue;
                    uint32_t current = start;
                    while (current != invalid && !nodes[current].color)
                    { nodes[current].color = 1; current = nodes[current].parent; }
                    if (current != invalid && nodes[current].color == 1) return Failure(ImportError::Cycle, ImportObject::Node, current);
                    current = start;
                    while (current != invalid && nodes[current].color == 1)
                    { nodes[current].color = 2; current = nodes[current].parent; }
                }
                if (options.sceneIndex != SIZE_MAX || !asset.scenes.empty())
                {
                    const size_t scene = options.sceneIndex != SIZE_MAX ? options.sceneIndex : asset.defaultScene.value_or(0);
                    if (scene >= asset.scenes.size()) return Failure(ImportError::InvalidIndex, ImportObject::Scene, scene);
                    const auto& selected = asset.scenes[scene].nodeIndices;
                    roots = {selected.data(), selected.size()};
                }
                else if (!asset.nodes.empty() || asset.defaultScene)
                    return Failure(ImportError::InvalidIndex, ImportObject::Scene);
                nodeOrder[0] = invalid;
                uint32_t previousRoot = invalid;
                uint32_t unnamedCamera = 1;
                for (size_t rootOrdinal = 0; rootOrdinal < roots.count; ++rootOrdinal)
                {
                    const size_t root = roots.data[rootOrdinal];
                    if (root >= nodes.count) return Failure(ImportError::InvalidIndex, ImportObject::Node, root);
                    if (nodes[root].parent != invalid || nodes[root].canonical != invalid)
                        return Failure(ImportError::InvalidHierarchy, ImportObject::Node, root);
                    if (previousRoot != invalid) nodes[previousRoot].nextSibling = uint32_t(root);
                    previousRoot = uint32_t(root);
                    uint32_t current = uint32_t(root);
                    for (;;)
                    {
                        auto& node = nodes[current];
                        if (node.canonical != invalid || sourceNodeCount >= nodeOrder.count)
                            return Failure(ImportError::InvalidHierarchy, ImportObject::Node, current);
                        node.canonical = counts.nodes;
                        if (!AddCount(counts.nodes, 1)) return Failure(ImportError::Capacity, ImportObject::Node);
                        nodeOrder[sourceNodeCount++] = current;
                        const auto& source = asset.nodes[current];
                        if (!source.instancingAttributes.empty()) return Failure(ImportError::UnsupportedData, ImportObject::Node, current);
                        size_t nodeNameBytes = source.name.size();
                        if (source.cameraIndex)
                        {
                            node.camera = counts.cameras++;
                            node.cameraNode = source.meshIndex ? counts.nodes : node.canonical;
                            if (source.meshIndex && !AddCount(counts.nodes, 1)) return Failure(ImportError::Capacity, ImportObject::Node);
                            const auto& camera = asset.cameras[*source.cameraIndex];
                            size_t cameraNameBytes = camera.name.size();
                            if (!imported.metadata.cameraNames[*source.cameraIndex])
                            {
                                if (!source.meshIndex && !source.name.empty()) cameraNameBytes = source.name.size();
                                else
                                {
                                    node.cameraNumber = unnamedCamera++;
                                    char name[32];
                                    const int length = snprintf(name, sizeof(name), "Camera%u", node.cameraNumber);
                                    if (length < 0 || size_t(length) >= sizeof(name)) return Failure(ImportError::Capacity, ImportObject::Camera);
                                    cameraNameBytes = size_t(length);
                                }
                            }
                            if (node.cameraNode == node.canonical) nodeNameBytes = cameraNameBytes;
                            else if (!AddCount(counts.stringBytes, cameraNameBytes)) return Failure(ImportError::Capacity);
                        }
                        if (source.lightIndex)
                        {
                            node.light = counts.lights++;
                            node.lightNode = source.meshIndex || source.cameraIndex ? counts.nodes : node.canonical;
                            if (node.lightNode != node.canonical && !AddCount(counts.nodes, 1)) return Failure(ImportError::Capacity, ImportObject::Node);
                            if (options.runtimeLights.enabled && node.lightNode == node.canonical)
                                nodeNameBytes = NormalizeSceneLightName({source.name.data(), source.name.size()}).count;
                        }
                        if (!AddCount(counts.stringBytes, nodeNameBytes)) return Failure(ImportError::Capacity);
                        if (node.firstChild != invalid) { current = node.firstChild; continue; }
                        while (current != root && nodes[current].nextSibling == invalid) current = nodes[current].parent;
                        if (current == root) break;
                        current = nodes[current].nextSibling;
                    }
                }
                if (!AddCount(counts.stringBytes, options.modelName.count)) return Failure(ImportError::Capacity);
                // the native importer builds an orphaned tree. attaching its
                // completed root registers every leaf in hierarchy order.
                for (uint32_t i = 1; i < sourceNodeCount; ++i)
                    {
                        const uint32_t sourceIndex = nodeOrder[i];
                        const auto& source = asset.nodes[sourceIndex];
                        if (!source.meshIndex) continue;
                        nodes[sourceIndex].instance = counts.instances;
                        instanceOrder[counts.instances++] = sourceIndex;
                        if (source.skinIndex)
                        {
                            ++skinCount;
                            const auto& skin = asset.skins[*source.skinIndex];
                            if (skin.joints.empty() || skin.joints.size() > UINT32_MAX) return Failure(ImportError::InvalidData, ImportObject::Skin, *source.skinIndex);
                            if (!AddCount(counts.joints, skin.joints.size())) return Failure(ImportError::Capacity);
                            if (skin.inverseBindMatrices)
                            {
                                ImportAccessorInfo info;
                                UVSR_IMPORT_TRY(document.AccessorInfo(*skin.inverseBindMatrices, info));
                                if (info.shape != ImportShape::Mat4 || info.component != ImportComponent::Float32 ||
                                    info.normalized || info.count < skin.joints.size())
                                    return Failure(ImportError::InvalidAccessor, ImportObject::Accessor, *skin.inverseBindMatrices);
                                if (info.scalarCount > maxFloats) maxFloats = info.scalarCount;
                            }
                            if (skin.skeleton && (*skin.skeleton >= nodes.count || nodes[*skin.skeleton].canonical == invalid))
                                return Failure(ImportError::InvalidIndex, ImportObject::Skin, *source.skinIndex);
                            for (size_t joint : skin.joints)
                            {
                                if (joint >= nodes.count || nodes[joint].canonical == invalid)
                                    return Failure(ImportError::InvalidIndex, ImportObject::Skin, *source.skinIndex);
                                if (nodes[joint].jointStamp == sourceIndex) return Failure(ImportError::InvalidHierarchy, ImportObject::Skin, *source.skinIndex);
                                nodes[joint].jointStamp = sourceIndex;
                            }
                        }
                    }
                return {};
            }

            ImportResult DescribeAttribute(size_t accessor, InputAttribute attribute, uint32_t expected,
                ImportAccessorInfo& info) const noexcept
            {
                UVSR_IMPORT_TRY(document.AccessorInfo(accessor, info));
                const ImportShape shapes[]{ImportShape::Vec3, ImportShape::Vec3, ImportShape::Vec4,
                    ImportShape::Vec2, ImportShape::Vec4, ImportShape::Vec4, ImportShape::Scalar};
                if (info.shape != shapes[attribute] || info.count > UINT32_MAX || (expected && info.count != expected))
                    return Failure(ImportError::InvalidAccessor, ImportObject::Accessor, accessor);
                const bool floating = info.component == ImportComponent::Float32 && !info.normalized;
                const bool unsignedSmall = info.component == ImportComponent::Uint8 || info.component == ImportComponent::Uint16;
                const bool signedSmall = info.component == ImportComponent::Int8 || info.component == ImportComponent::Int16;
                bool validType = floating;
                if (attribute == Joints) validType = unsignedSmall && !info.normalized;
                else if (attribute == Weights || attribute == Texcoord) validType |= unsignedSmall && info.normalized;
                else if (attribute == Normal || attribute == Tangent) validType |= signedSmall && info.normalized;
                if (!validType) return Failure(ImportError::InvalidAccessor, ImportObject::Accessor, accessor);
                return {};
            }

            ImportResult PlanGeometry() noexcept;
            ImportResult PlanAnimations() noexcept;
            ImportResult PlanRuntimeLights() noexcept;
            ImportResult PlanMeshes() noexcept;
            ImportResult PlanMaterials() noexcept;
            ImportResult PlanTextures() noexcept;
            ImportResult ImageData(uint32_t source, bool preferDds, uint32_t& output) noexcept;
            ImportResult LoadTexture(uint32_t source, bool srgb, uint32_t& output) noexcept;
            ImportResult AllocateTextures(ImportTexturesState& output) noexcept;
            ImportResult AllocateGeometry(ImportGeometryState& geometry) noexcept;
            ImportResult DecodeGeometry(ImportGeometryState& geometry) noexcept;
            ImportResult WriteScene(RendererScene& scene, ImportGeometryState& geometry) noexcept;
            ImportResult WriteLightsAndCameras(RendererScene& scene) noexcept;
            ImportResult WriteAnimations(RendererScene& scene) noexcept;
            ImportResult WriteRuntimeLights(RendererScene& scene) noexcept;
            ImportResult GenerateTangents(const PrimitivePlan& primitive, GeometryGroup& group) noexcept;

            ArrayView<const char> ImageText(RendererSceneString text) const noexcept
            { return text.length ? ArrayView<const char>{imageStrings.data + text.offset, text.length} : ArrayView<const char>{}; }

            ImportResult ImageString(ArrayView<const char> bytes, RendererSceneString& text) noexcept
            {
                if (bytes.count > imageStrings.count - imageStringBytes) return Failure(ImportError::Capacity, ImportObject::Image);
                text = {imageStringBytes, uint32_t(bytes.count)};
                if (bytes.count) memcpy(imageStrings.data + imageStringBytes, bytes.data, bytes.count);
                imageStringBytes += uint32_t(bytes.count);
                return {};
            }

            ImportResult MaterialTexture(const fastgltf::TextureInfo* info, bool srgb, uint32_t& output) noexcept
            {
                if (!info) { output = invalid; return {}; }
                if (info->textureIndex >= asset.textures.size()) return Failure(ImportError::InvalidIndex, ImportObject::Texture, info->textureIndex);
                return LoadTexture(uint32_t(info->textureIndex), srgb, output);
            }

            ImportResult WriteString(RendererScene& scene, ArrayView<const char> bytes, RendererSceneString& string) noexcept
            {
                if (bytes.count > counts.stringBytes - stringOffset) return Failure(ImportError::Capacity);
                UVSR_IMPORT_WRITE(scene.WriteStrings(stringOffset, bytes));
                string = {stringOffset, uint32_t(bytes.count)};
                stringOffset += uint32_t(bytes.count);
                return {};
            }

            ArrayView<const char> CameraName(uint32_t sourceIndex, char (&generated)[32]) const noexcept
            {
                const auto& plan = nodes[sourceIndex];
                const auto& node = asset.nodes[sourceIndex];
                const auto& camera = asset.cameras[*node.cameraIndex];
                if (plan.cameraNumber)
                {
                    const int length = snprintf(generated, sizeof(generated), "Camera%u", plan.cameraNumber);
                    return {generated, size_t(length)}; // the same bounded format was checked during planning.
                }
                if (!imported.metadata.cameraNames[*node.cameraIndex] && plan.cameraNode == plan.canonical)
                    return {node.name.data(), node.name.size()};
                return {camera.name.data(), camera.name.size()};
            }
        };

        ImportResult Builder::PlanAnimations() noexcept
        {
            if (asset.animations.size() >= UINT32_MAX) return Failure(ImportError::Capacity, ImportObject::Animation);
            uint32_t samplerCount = 0;
            for (const auto& animation : asset.animations)
                if (!AddCount(samplerCount, animation.samplers.size())) return Failure(ImportError::Capacity, ImportObject::Animation);
            UVSR_IMPORT_TRY(animations.Allocate(asset.animations.size(), scratch));
            UVSR_IMPORT_TRY(animationSamplers.Allocate(samplerCount, scratch));
            UVSR_IMPORT_TRY(animationSamplerOrder.Allocate(samplerCount, scratch));
            uint32_t samplerOffset = 0;
            for (uint32_t a = 0; a < animations.count; ++a)
            {
                const auto& source = asset.animations[a];
                auto& plan = animations[a];
                plan.samplerOffset = samplerOffset;
                plan.record.channels.first = counts.channels;
                for (uint32_t s = 0; s < source.samplers.size(); ++s)
                {
                    const auto& input = source.samplers[s];
                    auto& sampler = animationSamplers[samplerOffset + s];
                    sampler.input = input.inputAccessor;
                    sampler.output = input.outputAccessor;
                    // unused samplers have validated references but need no data read.
                    if (sampler.input >= document.AccessorCount() || sampler.output >= document.AccessorCount())
                        return Failure(ImportError::InvalidIndex, ImportObject::Animation, a);
                }
                for (const auto& channel : source.channels)
                {
                    if (channel.samplerIndex >= source.samplers.size() || (channel.nodeIndex && *channel.nodeIndex >= nodes.count))
                        return Failure(ImportError::InvalidIndex, ImportObject::Animation, a);
                    if (!channel.nodeIndex || nodes[*channel.nodeIndex].canonical == invalid || channel.path == fastgltf::AnimationPath::Weights) continue;
                    const uint32_t path = uint32_t(channel.path);
                    if (path < 1 || path > 3) return Failure(ImportError::UnsupportedData, ImportObject::Animation, a);
                    auto& target = nodes[*channel.nodeIndex];
                    if (target.animationStamp[path - 1] == a) return Failure(ImportError::InvalidData, ImportObject::Animation, a);
                    target.animationStamp[path - 1] = a;
                    const auto& sourceSampler = source.samplers[channel.samplerIndex];
                    auto& sampler = animationSamplers[samplerOffset + channel.samplerIndex];
                    const uint32_t lanes = channel.path == fastgltf::AnimationPath::Rotation ? 4 : 3;
                    ImportAccessorInfo times, values;
                    UVSR_IMPORT_TRY(document.AccessorInfo(sampler.input, times));
                    UVSR_IMPORT_TRY(document.AccessorInfo(sampler.output, values));
                    const bool cubic = sourceSampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline;
                    const size_t factor = cubic ? 3 : 1;
                    if (times.shape != ImportShape::Scalar || times.component != ImportComponent::Float32 || times.normalized ||
                        values.shape != (lanes == 4 ? ImportShape::Vec4 : ImportShape::Vec3) ||
                        values.component != ImportComponent::Float32 || values.normalized ||
                        times.count > UINT32_MAX || times.count > SIZE_MAX / factor || values.count != times.count * factor)
                        return Failure(ImportError::InvalidAccessor, ImportObject::Animation, a);
                    if (!times.count) continue;
                    if (sampler.canonical == invalid)
                    {
                        sampler.canonical = counts.samplers++;
                        animationSamplerOrder[sampler.canonical] = samplerOffset + uint32_t(channel.samplerIndex);
                        sampler.lanes = lanes;
                        sampler.record.keyframes = {counts.keyframes, uint32_t(times.count)};
                        if (!AddCount(counts.keyframes, times.count)) return Failure(ImportError::Capacity, ImportObject::Animation);
                        switch (sourceSampler.interpolation)
                        {
                        case fastgltf::AnimationInterpolation::Step: sampler.record.interpolation = RendererSceneInterpolation::Step; break;
                        case fastgltf::AnimationInterpolation::Linear:
                            sampler.record.interpolation = lanes == 4 ? RendererSceneInterpolation::Slerp : RendererSceneInterpolation::Linear; break;
                        case fastgltf::AnimationInterpolation::CubicSpline: sampler.record.interpolation = RendererSceneInterpolation::HermiteSpline; break;
                        default: return Failure(ImportError::UnsupportedData, ImportObject::Animation, a);
                        }
                        if (values.scalarCount > SIZE_MAX - times.count) return Failure(ImportError::Overflow, ImportObject::Animation);
                        const size_t scalars = times.count + values.scalarCount;
                        if (scalars > maxFloats) maxFloats = scalars;
                    }
                    else if (sampler.lanes != lanes) return Failure(ImportError::InvalidAccessor, ImportObject::Animation, a);
                    if (!AddCount(counts.channels, 1) || !AddCount(plan.record.channels.count, 1))
                        return Failure(ImportError::Capacity, ImportObject::Animation);
                }
                samplerOffset += uint32_t(source.samplers.size());
                if (firstAnimationNode == invalid && asset.animations.size() > 1)
                {
                    animationContainer = counts.nodes;
                    if (!AddCount(counts.nodes, 1) || !AddCount(counts.stringBytes, 10)) return Failure(ImportError::Capacity);
                }
                plan.canonical = counts.animations++;
                plan.record.nodeIndex = counts.nodes;
                if (firstAnimationNode == invalid) firstAnimationNode = counts.nodes;
                if (!AddCount(counts.nodes, 1) || !AddCount(counts.stringBytes, source.name.size())) return Failure(ImportError::Capacity);
            }
            return {};
        }

        ImportResult Builder::PlanRuntimeLights() noexcept
        {
            if (!options.runtimeLights.enabled) return {};
            for (uint32_t n = 1; n < sourceNodeCount; ++n)
            {
                const uint32_t source = nodeOrder[n];
                if (nodes[source].light != invalid && asset.lights[*asset.nodes[source].lightIndex].type == fastgltf::LightType::Directional)
                {
                    runtimeLights.sun = {options.generation, nodes[source].light};
                    break;
                }
            }
            firstRuntimeNode = counts.nodes;
            appendSun = !runtimeLights.sun;
            if (appendSun)
            {
                runtimeLights.sun = {options.generation, counts.lights};
                if (!AddCount(counts.nodes, 1) || !AddCount(counts.lights, 1) ||
                    !AddCount(counts.stringBytes, options.runtimeLights.sun.name.count)) return Failure(ImportError::Capacity);
            }
            runtimeLights.flashlight = {options.generation, counts.lights};
            if (!AddCount(counts.nodes, 1) || !AddCount(counts.lights, 1) ||
                !AddCount(counts.stringBytes, options.runtimeLights.flashlight.name.count)) return Failure(ImportError::Capacity);
            return {};
        }

        ImportResult Builder::PlanGeometry() noexcept
        {
            uint32_t primitiveCount = 0;
            for (const auto& mesh : asset.meshes)
                if (!AddCount(primitiveCount, mesh.primitives.size())) return Failure(ImportError::Capacity, ImportObject::Primitive);
            UVSR_IMPORT_TRY(meshes.Allocate(asset.meshes.size(), scratch));
            UVSR_IMPORT_TRY(primitives.Allocate(primitiveCount, scratch));
            uint32_t cursor = 0;
            for (uint32_t m = 0; m < meshes.count; ++m)
            {
                auto& mesh = meshes[m];
                const auto& source = asset.meshes[m];
                mesh.firstPrimitive = cursor;
                mesh.vertexOffset = totalVertices;
                mesh.indexOffset = totalIndices;
                mesh.firstMorph = counts.morphRanges;
                if (source.primitives.empty()) return Failure(ImportError::InvalidData, ImportObject::Mesh, m);
                if (source.primitives[0].targets.size() > UINT32_MAX) return Failure(ImportError::Capacity);
                mesh.morphCount = uint32_t(source.primitives[0].targets.size());
                if (!AddCount(counts.morphRanges, mesh.morphCount)) return Failure(ImportError::Capacity);
                for (const auto& primitive : source.primitives)
                {
                    auto& plan = primitives[cursor];
                    switch (primitive.type)
                    {
                    case fastgltf::PrimitiveType::Triangles: plan.type = RendererScenePrimitive::Triangles; break;
                    case fastgltf::PrimitiveType::Lines: plan.type = RendererScenePrimitive::Lines; break;
                    case fastgltf::PrimitiveType::LineStrip: plan.type = RendererScenePrimitive::LineStrip; break;
                    default: return Failure(ImportError::UnsupportedData, ImportObject::Primitive, cursor);
                    }
                    if (primitive.dracoCompression) return Failure(ImportError::UnsupportedData, ImportObject::Primitive, cursor);
                    if (primitive.materialIndex && *primitive.materialIndex >= asset.materials.size())
                        return Failure(ImportError::InvalidIndex, ImportObject::Material, *primitive.materialIndex);
                    plan.material = primitive.materialIndex ? uint32_t(*primitive.materialIndex) : uint32_t(asset.materials.size());
                    for (const auto& attribute : primitive.attributes)
                    {
                        if (attribute.accessorIndex >= asset.accessors.size()) return Failure(ImportError::InvalidIndex, ImportObject::Accessor, attribute.accessorIndex);
                        uint32_t slot = InputAttributeCount;
                        const auto name = std::string_view(attribute.name.data(), attribute.name.size());
                        if (name == "POSITION") slot = Position;
                        else if (name == "NORMAL") slot = Normal;
                        else if (name == "TANGENT") slot = Tangent;
                        else if (name == "TEXCOORD_0") slot = Texcoord;
                        else if (name.size() >= 7 && name.substr(0, 7) == "JOINTS_") slot = Joints;
                        else if (name.size() >= 8 && name.substr(0, 8) == "WEIGHTS_") slot = Weights;
                        else if (name.size() >= 7 && name.substr(0, 7) == "_RADIUS") slot = Radius;
                        if (slot != InputAttributeCount)
                        {
                            if (plan.accessors[slot] != SIZE_MAX && slot != Joints && slot != Weights && slot != Radius)
                                return Failure(ImportError::InvalidData, ImportObject::Primitive, cursor);
                            plan.accessors[slot] = attribute.accessorIndex;
                        }
                    }
                    if (plan.accessors[Position] == SIZE_MAX) return Failure(ImportError::InvalidAccessor, ImportObject::Primitive, cursor);
                    ImportAccessorInfo info;
                    UVSR_IMPORT_TRY(DescribeAttribute(plan.accessors[Position], Position, 0, info));
                    plan.vertexCount = uint32_t(info.count);
                    plan.indexCount = plan.vertexCount;
                    if (primitive.indicesAccessor)
                    {
                        UVSR_IMPORT_TRY(document.AccessorInfo(*primitive.indicesAccessor, info));
                        if (info.shape != ImportShape::Scalar || info.normalized || info.count > UINT32_MAX ||
                            (info.component != ImportComponent::Uint8 && info.component != ImportComponent::Uint16 && info.component != ImportComponent::Uint32))
                            return Failure(ImportError::InvalidAccessor, ImportObject::Accessor, *primitive.indicesAccessor);
                        plan.indexCount = uint32_t(info.count);
                        plan.indexMaximum = info.component == ImportComponent::Uint8 ? 255u :
                            (info.component == ImportComponent::Uint16 ? 65535u : UINT32_MAX);
                    }
                    if ((plan.type == RendererScenePrimitive::Triangles && plan.indexCount % 3) ||
                        (plan.type == RendererScenePrimitive::Lines && plan.indexCount % 2) ||
                        (plan.type == RendererScenePrimitive::LineStrip && plan.indexCount < 2))
                        return Failure(ImportError::InvalidData, ImportObject::Primitive, cursor);
                    for (uint32_t slot = 1; slot < InputAttributeCount; ++slot)
                        if (plan.accessors[slot] != SIZE_MAX)
                            UVSR_IMPORT_TRY(DescribeAttribute(plan.accessors[slot], InputAttribute(slot), plan.vertexCount, info));
                    if ((plan.accessors[Joints] == SIZE_MAX) != (plan.accessors[Weights] == SIZE_MAX))
                        return Failure(ImportError::InvalidAccessor, ImportObject::Primitive, cursor);
                    mesh.skinPrototype |= plan.accessors[Joints] != SIZE_MAX;
                    hasJoints |= mesh.skinPrototype;
                    hasRadius |= plan.accessors[Radius] != SIZE_MAX;
                    generatesTangents |= plan.accessors[Normal] != SIZE_MAX && plan.accessors[Texcoord] != SIZE_MAX && plan.accessors[Tangent] == SIZE_MAX;
                    if (primitive.targets.size() != mesh.morphCount) return Failure(ImportError::InvalidData, ImportObject::Primitive, cursor);
                    for (const auto& target : primitive.targets)
                    {
                        bool positionFound = false;
                        for (const auto& attribute : target)
                        {
                            if (attribute.accessorIndex >= asset.accessors.size()) return Failure(ImportError::InvalidIndex, ImportObject::Accessor, attribute.accessorIndex);
                            if (attribute.name == "POSITION")
                            {
                                if (positionFound) return Failure(ImportError::InvalidData, ImportObject::Primitive, cursor);
                                positionFound = true;
                                UVSR_IMPORT_TRY(DescribeAttribute(attribute.accessorIndex, Position, plan.vertexCount, info));
                            }
                        }
                    }
                    plan.vertexOffset = totalVertices;
                    plan.indexOffset = totalIndices;
                    if (!AddCount(totalVertices, plan.vertexCount) || !AddCount(totalIndices, plan.indexCount) ||
                        !AddCount(mesh.vertexCount, plan.vertexCount) || !AddCount(mesh.indexCount, plan.indexCount))
                        return Failure(ImportError::Capacity, ImportObject::Primitive, cursor);
                    if (plan.vertexCount > maxVertices) maxVertices = plan.vertexCount;
                    if (plan.indexCount > maxIndices) maxIndices = plan.indexCount;
                    ++cursor;
                }
            }
            return {};
        }

        ImportResult Builder::PlanMeshes() noexcept
        {
            if (skinCount > UINT32_MAX - meshes.count) return Failure(ImportError::Capacity);
            UVSR_IMPORT_TRY(meshOrder.Allocate(meshes.count + skinCount, scratch));
            UVSR_IMPORT_TRY(groups.Allocate(size_t(skinCount) + (counts.instances ? 1 : 0), scratch));
            const auto append = [this](uint32_t source, uint32_t skinNode, uint32_t& index) noexcept -> ImportResult
            {
                if (counts.meshes >= meshOrder.count) return Failure(ImportError::Capacity);
                auto& order = meshOrder[counts.meshes];
                order.source = source;
                order.skinNode = skinNode;
                order.firstGeometry = counts.geometries;
                if (!AddCount(counts.geometries, asset.meshes[source].primitives.size()) ||
                    !AddCount(counts.stringBytes, asset.meshes[source].name.size())) return Failure(ImportError::Capacity);
                if (skinNode != invalid)
                {
                    order.group = counts.bufferGroups++;
                    groups[order.group].sourceMesh = source;
                }
                else
                {
                    if (sourceGroup == invalid) sourceGroup = counts.bufferGroups++;
                    order.group = sourceGroup;
                }
                index = counts.meshes++;
                return {};
            };
            for (uint32_t i = 1; i < sourceNodeCount; ++i)
            {
                const uint32_t sourceNode = nodeOrder[i];
                const auto& node = asset.nodes[sourceNode];
                if (!node.meshIndex) continue;
                const uint32_t sourceMesh = uint32_t(*node.meshIndex);
                auto& mesh = meshes[sourceMesh];
                if (node.skinIndex)
                {
                    for (size_t p = 0; p < asset.meshes[sourceMesh].primitives.size(); ++p)
                        if (primitives[mesh.firstPrimitive + p].accessors[Joints] == SIZE_MAX)
                            return Failure(ImportError::InvalidAccessor, ImportObject::Mesh, sourceMesh);
                    UVSR_IMPORT_TRY(append(sourceMesh, sourceNode, nodes[sourceNode].mesh));
                    if (mesh.canonical == invalid) UVSR_IMPORT_TRY(append(sourceMesh, invalid, mesh.canonical));
                }
                else
                {
                    if (mesh.canonical == invalid) UVSR_IMPORT_TRY(append(sourceMesh, invalid, mesh.canonical));
                    nodes[sourceNode].mesh = mesh.canonical;
                }
            }
            if (sourceGroup == invalid) { counts.morphRanges = 0; return {}; }
            uint32_t consumedMorphs = 0;
            for (uint32_t g = 0; g < counts.bufferGroups; ++g)
            {
                auto& group = groups[g].record;
                group.indexBytes = uint64_t(totalIndices) * 4;
                if (group.indexBytes > UINT32_MAX) return Failure(ImportError::Capacity, ImportObject::Buffer);
                group.morphRanges.first = consumedMorphs;
                const bool derived = g != sourceGroup;
                const size_t vertices = derived ? meshes[groups[g].sourceMesh].vertexCount : totalVertices;
                UVSR_IMPORT_TRY(AppendAttribute(group, Attribute::Position, vertices, 12));
                if (derived) UVSR_IMPORT_TRY(AppendAttribute(group, Attribute::PreviousPosition, vertices, 12));
                UVSR_IMPORT_TRY(AppendAttribute(group, Attribute::Normal, vertices, 4));
                UVSR_IMPORT_TRY(AppendAttribute(group, Attribute::Tangent, vertices, 4));
                UVSR_IMPORT_TRY(AppendAttribute(group, Attribute::TexCoord0, vertices, 8));
                if (!derived)
                {
                    if (hasJoints)
                    {
                        UVSR_IMPORT_TRY(AppendAttribute(group, Attribute::JointWeights, vertices, 16));
                        UVSR_IMPORT_TRY(AppendAttribute(group, Attribute::JointIndices, vertices, 8));
                    }
                    if (hasRadius) UVSR_IMPORT_TRY(AppendAttribute(group, Attribute::CurveRadius, vertices, 4));
                    if (vertices > size_t(PTRDIFF_MAX) / 16 || (vertices && counts.morphRanges > size_t(PTRDIFF_MAX) / (vertices * 16)))
                        return Failure(ImportError::Overflow);
                    group.morphBytes = uint64_t(vertices) * 16 * counts.morphRanges;
                    group.morphRanges.count = counts.morphRanges;
                    consumedMorphs = counts.morphRanges;
                }
            }
            return {};
        }

        ImportResult Builder::PlanMaterials() noexcept
        {
            const size_t capacity = asset.materials.size() + (counts.geometries ? 1 : 0);
            UVSR_IMPORT_TRY(materials.Allocate(capacity, scratch));
            UVSR_IMPORT_TRY(materialOrder.Allocate(capacity, scratch));
            for (uint32_t i = 0; i < asset.materials.size(); ++i)
            {
                const auto& source = asset.materials[i];
                const auto& metadata = imported.metadata.materials[i];
                auto& value = materials[i].values;
                const auto color = [](const auto& vector) noexcept -> Float3 { return {vector[0], vector[1], vector[2]}; };
                if (source.specularGlossiness)
                {
                    const auto& pbr = *source.specularGlossiness;
                    UVSR_IMPORT_TRY(MaterialTexture(pbr.diffuseTexture ? &*pbr.diffuseTexture : nullptr, true, value.textures[0]));
                    UVSR_IMPORT_TRY(MaterialTexture(pbr.specularGlossinessTexture ? &*pbr.specularGlossinessTexture : nullptr, true, value.textures[1]));
                    value.useSpecularGlossModel = true;
                    value.baseOrDiffuseColor = color(pbr.diffuseFactor);
                    value.opacity = pbr.diffuseFactor[3];
                    value.specularColor = color(pbr.specularFactor);
                    value.roughness = 1.f - pbr.glossinessFactor;
                }
                else if (metadata.flags & ImportMaterialPbr)
                {
                    const auto& pbr = source.pbrData;
                    UVSR_IMPORT_TRY(MaterialTexture(pbr.baseColorTexture ? &*pbr.baseColorTexture : nullptr, true, value.textures[0]));
                    UVSR_IMPORT_TRY(MaterialTexture(pbr.metallicRoughnessTexture ? &*pbr.metallicRoughnessTexture : nullptr, false, value.textures[1]));
                    value.baseOrDiffuseColor = color(pbr.baseColorFactor);
                    value.opacity = pbr.baseColorFactor[3];
                    value.metalness = pbr.metallicFactor;
                    value.roughness = pbr.roughnessFactor;
                }
                if (source.transmission)
                {
                    const auto& info = source.transmission->transmissionTexture;
                    UVSR_IMPORT_TRY(MaterialTexture(info ? &*info : nullptr, false, value.textures[5]));
                    value.transmissionFactor = source.transmission->transmissionFactor;
                }
                // request order determines a shared image's first color space.
                UVSR_IMPORT_TRY(MaterialTexture(source.emissiveTexture ? &*source.emissiveTexture : nullptr, true, value.textures[3]));
                UVSR_IMPORT_TRY(MaterialTexture(source.normalTexture ? &*source.normalTexture : nullptr, false, value.textures[2]));
                UVSR_IMPORT_TRY(MaterialTexture(source.occlusionTexture ? &*source.occlusionTexture : nullptr, false, value.textures[4]));
                // cgltf leaves absent texture views zeroed. the empty fallback
                // Material keeps its separate constructor defaults.
                value.normalTextureScale = source.normalTexture ? source.normalTexture->scale : 0.f;
                value.occlusionStrength = source.occlusionTexture ? source.occlusionTexture->strength : 0.f;
                if (source.normalTexture && source.normalTexture->transform)
                {
                    const auto& scale = source.normalTexture->transform->uvScale;
                    value.normalTextureTransformScale = {scale[0], scale[1]};
                }
                value.emissiveColor = color(source.emissiveFactor);
                if (metadata.flags & ImportMaterialEmissiveStrength) value.emissiveIntensity = source.emissiveStrength;
                else
                {
                    value.emissiveIntensity = value.emissiveColor.x;
                    if (value.emissiveIntensity < value.emissiveColor.y) value.emissiveIntensity = value.emissiveColor.y;
                    if (value.emissiveIntensity < value.emissiveColor.z) value.emissiveIntensity = value.emissiveColor.z;
                    if (value.emissiveIntensity > 0)
                    {
                        value.emissiveColor.x /= value.emissiveIntensity;
                        value.emissiveColor.y /= value.emissiveIntensity;
                        value.emissiveColor.z /= value.emissiveIntensity;
                    }
                    else value.emissiveIntensity = 1;
                }
                value.alphaCutoff = source.alphaCutoff;
                value.doubleSided = source.doubleSided;
                const bool transmission = source.transmission != nullptr;
                switch (source.alphaMode)
                {
                case fastgltf::AlphaMode::Opaque: value.domain = transmission ? RendererMaterialDomain::Transmissive : RendererMaterialDomain::Opaque; break;
                case fastgltf::AlphaMode::Mask: value.domain = transmission ? RendererMaterialDomain::TransmissiveAlphaTested : RendererMaterialDomain::AlphaTested; break;
                case fastgltf::AlphaMode::Blend: value.domain = transmission ? RendererMaterialDomain::TransmissiveAlphaBlended : RendererMaterialDomain::AlphaBlended; break;
                default: return Failure(ImportError::InvalidData, ImportObject::Material, i);
                }
                if (metadata.flags & ImportMaterialSubsurface)
                { value.enableSubsurfaceScattering = true; value.subsurface = metadata.subsurface; }
                if (metadata.flags & ImportMaterialHair)
                { value.enableHair = true; value.hair = metadata.hair; }
            }
            for (uint32_t m = 0; m < counts.meshes; ++m)
            {
                const auto& plan = meshes[meshOrder[m].source];
                for (size_t p = 0; p < asset.meshes[meshOrder[m].source].primitives.size(); ++p)
                {
                    const uint32_t source = primitives[plan.firstPrimitive + p].material;
                    if (materials[source].canonical != invalid) continue;
                    if (counts.materials == UINT32_MAX) return Failure(ImportError::Capacity, ImportObject::Material);
                    materials[source].canonical = counts.materials;
                    materialOrder[counts.materials++] = source;
                    const size_t nameBytes = source < asset.materials.size() ? asset.materials[source].name.size() : 7;
                    if (!AddCount(counts.stringBytes, nameBytes) ||
                        (source < asset.materials.size() && !AddCount(counts.stringBytes, options.modelPath.count)))
                        return Failure(ImportError::Capacity, ImportObject::Material, source);
                }
            }
            for (uint32_t m = 0; m < counts.materials; ++m)
            {
                auto& value = materials[materialOrder[m]].values;
                for (uint32_t& slot : value.textures)
                {
                    if (slot == invalid) continue;
                    auto& texture = textures[slot];
                    if (texture.canonical == invalid)
                    {
                        texture.canonical = counts.textures;
                        textureOrder[counts.textures++] = slot;
                        if (!AddCount(swizzleCount, texture.swizzleCount)) return Failure(ImportError::Capacity, ImportObject::Texture);
                        if (texture.image != invalid)
                        {
                            const auto& image = images[texture.image];
                            if (!AddCount(counts.stringBytes, image.path.length) || !AddCount(counts.stringBytes, image.mime.length))
                                return Failure(ImportError::Capacity, ImportObject::Texture);
                        }
                    }
                    slot = texture.canonical;
                }
            }
            return {};
        }

        ImportResult HashCapacity(size_t entries, size_t& capacity) noexcept
        {
            if (!entries) { capacity = 0; return {}; }
            if (entries > (UINT32_MAX - 1) / 2) return Failure(ImportError::Capacity, ImportObject::Texture);
            capacity = 2;
            while (capacity < entries * 2)
            {
                if (capacity > UINT32_MAX / 2) return Failure(ImportError::Capacity, ImportObject::Texture);
                capacity *= 2;
            }
            return {};
        }

        uint64_t TextHash(ArrayView<const char> text) noexcept
        {
            uint64_t hash = 14695981039346656037ull;
            for (size_t i = 0; i < text.count; ++i) { hash ^= uint8_t(text.data[i]); hash *= 1099511628211ull; }
            return hash;
        }

        bool SameText(ArrayView<const char> a, ArrayView<const char> b) noexcept
        { return a.count == b.count && (!a.count || memcmp(a.data, b.data, a.count) == 0); }

        bool Slash(char value) noexcept { return value == '/' || value == '\\'; }
        bool Drive(ArrayView<const char> value) noexcept
        { return value.count >= 2 && ((value.data[0] >= 'A' && value.data[0] <= 'Z') || (value.data[0] >= 'a' && value.data[0] <= 'z')) && value.data[1] == ':'; }

        ImportResult Builder::PlanTextures() noexcept
        {
            if (asset.images.size() > (UINT32_MAX - 1) / 2 || asset.textures.size() > UINT32_MAX - asset.images.size())
                return Failure(ImportError::Capacity, ImportObject::Image);
            const size_t textureCapacity = asset.images.size() + asset.textures.size();
            UVSR_IMPORT_TRY(images.Allocate(asset.images.size() * 2, scratch));
            UVSR_IMPORT_TRY(imageLookup.Allocate(asset.images.size(), scratch));
            UVSR_IMPORT_TRY(textures.Allocate(textureCapacity, scratch));
            UVSR_IMPORT_TRY(sourceTextures.Allocate(asset.textures.size(), scratch));
            for (size_t i = 0; i < sourceTextures.count; ++i) sourceTextures[i] = invalid;
            UVSR_IMPORT_TRY(textureOrder.Allocate(textureCapacity, scratch));
            UVSR_IMPORT_TRY(swizzles.Allocate(imported.metadata.swizzleCount, scratch));
            size_t hashCapacity = 0;
            UVSR_IMPORT_TRY(HashCapacity(asset.images.size(), hashCapacity));
            UVSR_IMPORT_TRY(textureHash.Allocate(hashCapacity, scratch));
            UVSR_IMPORT_TRY(HashCapacity(imported.metadata.swizzleCount, hashCapacity));
            UVSR_IMPORT_TRY(swizzleHash.Allocate(hashCapacity, scratch));
            uint32_t stringCapacity = 0;
            size_t pathCapacity = 0;
            for (const auto& image : asset.images)
            {
                size_t uriBytes = 0;
                if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data)) uriBytes = uri->uri.string().size();
                if (uriBytes > UINT32_MAX - 32 || image.name.size() > UINT32_MAX - 32 || options.modelPath.count > UINT32_MAX - 32)
                    return Failure(ImportError::Capacity, ImportObject::Image);
                const size_t pathBytes = options.modelPath.count + uriBytes + image.name.size() + 32;
                if (!AddCount(stringCapacity, pathBytes) || !AddCount(stringCapacity, pathBytes))
                    return Failure(ImportError::Capacity, ImportObject::Image);
                if (pathCapacity < pathBytes) pathCapacity = pathBytes;
            }
            if (!AddCount(stringCapacity, imported.metadata.imageMimeBytes)) return Failure(ImportError::Capacity, ImportObject::Image);
            UVSR_IMPORT_TRY(imageStrings.Allocate(stringCapacity, scratch));
            UVSR_IMPORT_TRY(pathWorkspace.Allocate(pathCapacity, scratch));
            for (uint32_t i = 0; i < asset.textures.size(); ++i)
            {
                const auto& texture = asset.textures[i];
                if ((texture.imageIndex && *texture.imageIndex >= asset.images.size()) ||
                    (texture.ddsImageIndex && *texture.ddsImageIndex >= asset.images.size()))
                    return Failure(ImportError::InvalidIndex, ImportObject::Image, i);
                if (texture.samplerIndex && *texture.samplerIndex >= asset.samplers.size())
                    return Failure(ImportError::InvalidIndex, ImportObject::Texture, i);
            }
            return {};
        }

        ImportResult Builder::ImageData(uint32_t source, bool preferDds, uint32_t& output) noexcept
        {
            if (source >= imageLookup.count) return Failure(ImportError::InvalidIndex, ImportObject::Image, source);
            auto& lookup = imageLookup[source];
            if (lookup.raw == invalid)
            {
                if (imageCount >= images.count) return Failure(ImportError::Capacity, ImportObject::Image);
                const auto& input = asset.images[source];
                ImagePlan image;
                image.sourceImage = source;
                if (const auto* uri = std::get_if<fastgltf::sources::URI>(&input.data))
                {
                    if (!options.fileExists || !options.modelPath.count) return Failure(ImportError::InvalidInput, ImportObject::Image, source);
                    const auto value = uri->uri.string();
                    const ArrayView<const char> path{value.data(), value.size()};
                    if (!uri->uri.scheme().empty() && !Drive(path)) return Failure(ImportError::UnsupportedData, ImportObject::Image, source);
                    if (uri->fileByteOffset != 0 || !path.count) return Failure(ImportError::InvalidData, ImportObject::Image, source);
                    size_t length = 0;
                    UVSR_IMPORT_TRY(ResolveImportPath(options.modelPath, path, {pathWorkspace.data, pathWorkspace.count}, length));
                    UVSR_IMPORT_TRY(ImageString({pathWorkspace.data, length}, image.path));
                }
                else
                {
                    image.embedded = true;
                    if (const auto* array = std::get_if<fastgltf::sources::Array>(&input.data))
                        image.bytes = {reinterpret_cast<const uint8_t*>(array->bytes.data()), array->bytes.size()};
                    else if (const auto* view = std::get_if<fastgltf::sources::BufferView>(&input.data))
                    {
                        if (view->bufferViewIndex >= asset.bufferViews.size()) return Failure(ImportError::InvalidIndex, ImportObject::BufferView, view->bufferViewIndex);
                        const auto& bufferView = asset.bufferViews[view->bufferViewIndex];
                        if (bufferView.bufferIndex >= asset.buffers.size()) return Failure(ImportError::InvalidIndex, ImportObject::Buffer, bufferView.bufferIndex);
                        const auto bytes = imported.buffers[bufferView.bufferIndex].bytes;
                        if (!bytes.data) return Failure(ImportError::BufferUnavailable, ImportObject::Buffer, bufferView.bufferIndex);
                        if (bufferView.byteOffset > bytes.count || bufferView.byteLength > bytes.count - bufferView.byteOffset)
                            return Failure(ImportError::InvalidRange, ImportObject::Image, source);
                        image.bytes = {bytes.data + bufferView.byteOffset, bufferView.byteLength};
                    }
                    else return Failure(ImportError::UnsupportedData, ImportObject::Image, source);
                    if (!image.bytes.count) return Failure(ImportError::InvalidData, ImportObject::Image, source);
                    const auto& metadata = imported.metadata.images[source];
                    if (metadata.named) UVSR_IMPORT_TRY(ImageString({input.name.data(), input.name.size()}, image.path));
                    else
                    {
                        if (!options.modelPath.count) return Failure(ImportError::InvalidInput, ImportObject::Image, source);
                        size_t first = options.modelPath.count;
                        while (first && !Slash(options.modelPath.data[first - 1])) --first;
                        const size_t nameLength = options.modelPath.count - first;
                        memcpy(pathWorkspace.data, options.modelPath.data + first, nameLength);
                        const int suffix = snprintf(pathWorkspace.data + nameLength, pathWorkspace.count - nameLength, "[%u]", source);
                        if (suffix < 0 || size_t(suffix) >= pathWorkspace.count - nameLength) return Failure(ImportError::Capacity, ImportObject::Image);
                        UVSR_IMPORT_TRY(ImageString({pathWorkspace.data, nameLength + size_t(suffix)}, image.path));
                    }
                    const auto mime = metadata.mimeType;
                    if (mime.length) UVSR_IMPORT_TRY(ImageString({imported.metadata.imageMimeTypes + mime.offset, mime.length}, image.mime));
                }
                lookup.raw = imageCount;
                images[imageCount++] = image;
            }
            if (!preferDds || images[lookup.raw].embedded) { output = lookup.raw; return {}; }
            if (lookup.preferred == invalid)
            {
                const auto original = ImageText(images[lookup.raw].path);
                size_t end = original.count;
                size_t file = original.count;
                while (file && !Slash(original.data[file - 1])) --file;
                const size_t nameSize = original.count - file;
                const bool dotName = (nameSize == 1 && original.data[file] == '.') ||
                    (nameSize == 2 && original.data[file] == '.' && original.data[file + 1] == '.');
                for (size_t i = original.count; !dotName && i > file + 1; --i)
                {
                    if (original.data[i - 1] == '.') { end = i - 1; break; }
                }
                if (end > pathWorkspace.count || pathWorkspace.count - end < 4) return Failure(ImportError::Capacity, ImportObject::Image);
                if (end) memcpy(pathWorkspace.data, original.data, end);
                memcpy(pathWorkspace.data + end, ".dds", 4);
                const ArrayView<const char> preferred{pathWorkspace.data, end + 4};
                lookup.preferred = lookup.raw;
                if (options.fileExists(options.fileContext, preferred) && !SameText(original, preferred))
                {
                    if (imageCount >= images.count) return Failure(ImportError::Capacity, ImportObject::Image);
                    ImagePlan image = images[lookup.raw];
                    UVSR_IMPORT_TRY(ImageString(preferred, image.path));
                    lookup.preferred = imageCount;
                    images[imageCount++] = image;
                }
            }
            output = lookup.preferred;
            return {};
        }

        ImportResult Builder::LoadTexture(uint32_t source, bool srgb, uint32_t& output) noexcept
        {
            if (source >= sourceTextures.count) return Failure(ImportError::InvalidIndex, ImportObject::Texture, source);
            if (sourceTextures[source] != invalid) { output = sourceTextures[source]; return {}; }
            const auto& sourceTexture = asset.textures[source];
            const auto selectedImage = sourceTexture.ddsImageIndex ? sourceTexture.ddsImageIndex : sourceTexture.imageIndex;
            uint32_t texture = invalid;
            if (selectedImage)
            {
                auto& lookup = imageLookup[*selectedImage];
                if (lookup.texture != invalid) texture = lookup.texture;
                else
                {
                    uint32_t image = invalid;
                    UVSR_IMPORT_TRY(ImageData(uint32_t(*selectedImage), !sourceTexture.ddsImageIndex, image));
                    const auto& data = images[image];
                    size_t slot = 0;
                    if (!data.embedded)
                    {
                        slot = size_t(TextHash(ImageText(data.path))) & (textureHash.count - 1);
                        while (textureHash[slot])
                        {
                            const uint32_t candidate = textureHash[slot] - 1;
                            if (SameText(ImageText(images[textures[candidate].image].path), ImageText(data.path)))
                            { texture = candidate; break; }
                            slot = (slot + 1) & (textureHash.count - 1);
                        }
                    }
                    if (texture == invalid)
                    {
                        if (textureCount >= textures.count) return Failure(ImportError::Capacity, ImportObject::Texture);
                        texture = textureCount++;
                        textures[texture].image = image;
                        textures[texture].srgb = srgb;
                        if (!data.embedded) textureHash[slot] = texture + 1;
                    }
                    lookup.texture = texture;
                }
            }
            const auto range = imported.metadata.textureSwizzles[source];
            if (texture == invalid && range.count)
            {
                if (textureCount >= textures.count) return Failure(ImportError::Capacity, ImportObject::Texture);
                texture = textureCount++;
            }
            if (texture == invalid)
            {
                if (imported.metadata.requiredSwizzle) return Failure(ImportError::UnsupportedExtension, ImportObject::Texture, source);
                output = invalid;
                return {};
            }
            if (textures[texture].image == invalid && imported.metadata.requiredSwizzle)
                return Failure(ImportError::UnsupportedExtension, ImportObject::Texture, source);
            for (uint32_t i = 0; i < range.count; ++i)
            {
                const uint32_t inputIndex = range.first + i;
                const auto& input = imported.metadata.swizzles[inputIndex];
                uint32_t image = invalid;
                UVSR_IMPORT_TRY(ImageData(input.image, false, image));
                const auto& imageData = images[image];
                uint64_t hash = imageData.embedded ? uint64_t(imageData.sourceImage) * 1099511628211ull : TextHash(ImageText(imageData.path));
                hash ^= uint64_t(texture) * 0x9e3779b97f4a7c15ull;
                size_t slot = size_t(hash) & (swizzleHash.count - 1);
                bool duplicate = false;
                while (swizzleHash[slot])
                {
                    const auto& previous = swizzles[swizzleHash[slot] - 1];
                    const auto& previousImage = images[previous.value.imageIndex];
                    const bool same = previousImage.embedded == imageData.embedded && (imageData.embedded ?
                        previousImage.sourceImage == imageData.sourceImage : SameText(ImageText(previousImage.path), ImageText(imageData.path)));
                    if (previous.texture == texture && same)
                    {
                        // the native merge compares only options from earlier
                        // glTF texture objects, retaining duplicates in this batch.
                        duplicate = previous.sourceTexture != source;
                        break;
                    }
                    slot = (slot + 1) & (swizzleHash.count - 1);
                }
                if (duplicate) continue;
                auto& converted = swizzles[inputIndex];
                converted.texture = texture;
                converted.sourceTexture = source;
                converted.value.imageIndex = image;
                converted.value.channelCount = input.channelCount;
                memcpy(converted.value.channels, input.channels, sizeof(input.channels));
                if (!swizzleHash[slot]) swizzleHash[slot] = inputIndex + 1;
                auto& plan = textures[texture];
                if (plan.lastSwizzle == invalid) plan.firstSwizzle = inputIndex;
                else swizzles[plan.lastSwizzle].next = inputIndex;
                plan.lastSwizzle = inputIndex;
                ++plan.swizzleCount;
            }
            sourceTextures[source] = texture;
            output = texture;
            return {};
        }

        ImportResult Builder::AllocateTextures(ImportTexturesState& output) noexcept
        {
            Budget storage{sizeof(ImportTexturesState), options.maxImageBytes, ImportError::Capacity};
            UVSR_IMPORT_TRY(output.images.Allocate(imageCount, storage));
            UVSR_IMPORT_TRY(output.textureMap.Allocate(counts.textures, storage));
            UVSR_IMPORT_TRY(output.requests.Allocate(textureCount, storage));
            UVSR_IMPORT_TRY(output.swizzles.Allocate(swizzleCount, storage));
            UVSR_IMPORT_TRY(output.strings.Allocate(imageStringBytes, storage));
            size_t bytes = 0;
            for (uint32_t i = 0; i < imageCount; ++i)
            {
                if (images[i].bytes.count > size_t(PTRDIFF_MAX) - bytes) return Failure(ImportError::Overflow, ImportObject::Image, i);
                bytes += images[i].bytes.count;
            }
            UVSR_IMPORT_TRY(output.bytes.Allocate(bytes, storage));
            if (imageStringBytes) memcpy(output.strings.data, imageStrings.data, imageStringBytes);
            bytes = 0;
            for (uint32_t i = 0; i < imageCount; ++i)
            {
                const auto& source = images[i];
                auto& image = output.images[i];
                if (source.path.length) image.path = {output.strings.data + source.path.offset, source.path.length};
                if (source.mime.length) image.mimeType = {output.strings.data + source.mime.offset, source.mime.length};
                image.embedded = source.embedded;
                if (source.bytes.count)
                {
                    image.bytes = {output.bytes.data + bytes, source.bytes.count};
                    memcpy(output.bytes.data + bytes, source.bytes.data, source.bytes.count);
                    bytes += source.bytes.count;
                }
            }
            uint32_t firstSwizzle = 0;
            for (uint32_t i = 0; i < counts.textures; ++i) output.textureMap[i] = textureOrder[i];
            for (uint32_t i = 0; i < textureCount; ++i)
            {
                const auto& source = textures[i];
                output.requests[i] = {source.image, {firstSwizzle, source.swizzleCount}, source.srgb};
                uint32_t next = source.firstSwizzle;
                for (uint32_t s = 0; s < source.swizzleCount; ++s)
                {
                    if (next >= swizzles.count) return Failure(ImportError::InvalidState, ImportObject::Texture, i);
                    output.swizzles[firstSwizzle++] = swizzles[next].value;
                    next = swizzles[next].next;
                }
                if (next != invalid) return Failure(ImportError::InvalidState, ImportObject::Texture, i);
            }
            output.storageBytes = storage.bytes;
            return {};
        }

        ImportResult Builder::AllocateGeometry(ImportGeometryState& geometry) noexcept
        {
            Budget storage{sizeof(ImportGeometryState), options.maxGeometryBytes, ImportError::Capacity};
            UVSR_IMPORT_TRY(geometry.groups.Allocate(counts.bufferGroups, storage));
            UVSR_IMPORT_TRY(geometry.jointMatrices.Allocate(counts.joints, storage));
            const auto allocateBytes = [&storage](size_t bytes, uint8_t*& output) noexcept -> ImportResult
            {
                UVSR_IMPORT_TRY(storage.Add(bytes, 1));
                if (!bytes) return {};
                output = static_cast<uint8_t*>(ImportAllocate(bytes));
                if (!output) return Failure(ImportError::OutOfMemory);
                memset(output, 0, bytes);
                return {};
            };
            // establish index ownership before any allocation can fail.
            for (uint32_t g = 0; g < counts.bufferGroups; ++g) geometry.groups[g].indexOwner = sourceGroup;
            if (sourceGroup != invalid)
            {
                auto& shared = geometry.groups[sourceGroup];
                shared.indexBytes = size_t(groups[sourceGroup].record.indexBytes);
                UVSR_IMPORT_TRY(allocateBytes(shared.indexBytes, shared.indices));
            }
            for (uint32_t g = 0; g < counts.bufferGroups; ++g)
            {
                auto& group = geometry.groups[g];
                const auto& plan = groups[g].record;
                group.indices = geometry.groups[sourceGroup].indices;
                group.indexBytes = size_t(plan.indexBytes);
                group.vertexBytes = g == sourceGroup ? size_t(plan.vertexBytes) : 0;
                group.morphBytes = size_t(plan.morphBytes);
                UVSR_IMPORT_TRY(allocateBytes(group.vertexBytes, group.vertices));
                UVSR_IMPORT_TRY(allocateBytes(group.morphBytes, group.morphs));
            }
            geometry.storageBytes = storage.bytes;
            if (size_t(maxVertices) * 4 > maxFloats) maxFloats = size_t(maxVertices) * 4;
            UVSR_IMPORT_TRY(floats.Allocate(maxFloats, scratch));
            if (sourceGroup == invalid) return {};
            const size_t integerCount = size_t(maxVertices) * 4 > maxIndices ? size_t(maxVertices) * 4 : maxIndices;
            UVSR_IMPORT_TRY(integers.Allocate(integerCount, scratch));
            if (generatesTangents)
            {
                UVSR_IMPORT_TRY(normalScratch.Allocate(maxVertices, scratch));
                UVSR_IMPORT_TRY(tangentScratch.Allocate(maxVertices, scratch));
                UVSR_IMPORT_TRY(bitangentScratch.Allocate(maxVertices, scratch));
            }
            return {};
        }

        ImportResult Builder::GenerateTangents(const PrimitivePlan& primitive, GeometryGroup& group) noexcept
        {
            const auto& layout = groups[sourceGroup].record;
            const uint8_t* positions = group.vertices + layout.attributes[uint32_t(Attribute::Position)].offset + size_t(primitive.vertexOffset) * 12;
            const uint8_t* uvs = group.vertices + layout.attributes[uint32_t(Attribute::TexCoord0)].offset + size_t(primitive.vertexOffset) * 8;
            const uint8_t* indices = group.indices + size_t(primitive.indexOffset) * 4;
            for (uint32_t v = 0; v < primitive.vertexCount; ++v) { tangentScratch[v] = {}; bitangentScratch[v] = {}; }
            // the retained loader groups all supported modes by threes here.
            for (uint32_t triangle = 0; triangle < primitive.indexCount / 3; ++triangle)
            {
                const uint32_t a = ReadValue<uint32_t>(indices + size_t(triangle) * 12);
                const uint32_t b = ReadValue<uint32_t>(indices + size_t(triangle) * 12 + 4);
                const uint32_t c = ReadValue<uint32_t>(indices + size_t(triangle) * 12 + 8);
                const Float3 p0 = ReadValue<Float3>(positions + size_t(a) * 12);
                const Float3 p1 = ReadValue<Float3>(positions + size_t(b) * 12);
                const Float3 p2 = ReadValue<Float3>(positions + size_t(c) * 12);
                const auto t0 = ReadValue<gpu_contract::Float2>(uvs + size_t(a) * 8);
                const auto t1 = ReadValue<gpu_contract::Float2>(uvs + size_t(b) * 8);
                const auto t2 = ReadValue<gpu_contract::Float2>(uvs + size_t(c) * 8);
                const Float3 dPds = Subtract(p1, p0), dPdt = Subtract(p2, p0);
                const gpu_contract::Float2 dTds{t1.x - t0.x, t1.y - t0.y}, dTdt{t2.x - t0.x, t2.y - t0.y};
                const float r = 1.0f / (dTds.x * dTdt.y - dTds.y * dTdt.x);
                Float3 tangent = Multiply(Subtract(Multiply(dPds, dTdt.y), Multiply(dPdt, dTds.y)), r);
                Float3 bitangent = Multiply(Subtract(Multiply(dPdt, dTds.x), Multiply(dPds, dTdt.x)), r);
                const float tangentLength = sqrtf(Dot(tangent, tangent)), bitangentLength = sqrtf(Dot(bitangent, bitangent));
                if (tangentLength > 0 && bitangentLength > 0)
                {
                    tangent = Divide(tangent, tangentLength);
                    bitangent = Divide(bitangent, bitangentLength);
                    Accumulate(tangentScratch[a], tangent); Accumulate(tangentScratch[b], tangent); Accumulate(tangentScratch[c], tangent);
                    Accumulate(bitangentScratch[a], bitangent); Accumulate(bitangentScratch[b], bitangent); Accumulate(bitangentScratch[c], bitangent);
                }
            }
            uint8_t* output = group.vertices + layout.attributes[uint32_t(Attribute::Tangent)].offset + size_t(primitive.vertexOffset) * 4;
            for (uint32_t v = 0; v < primitive.vertexCount; ++v)
            {
                Float3 tangent = tangentScratch[v], bitangent = bitangentScratch[v];
                const float tangentLength = sqrtf(Dot(tangent, tangent)), bitangentLength = sqrtf(Dot(bitangent, bitangent));
                float sign = 0;
                if (tangentLength > 0 && bitangentLength > 0)
                {
                    tangent = Divide(tangent, tangentLength);
                    bitangent = Divide(bitangent, bitangentLength);
                    sign = Dot(Cross(normalScratch[v], tangent), bitangent) > 0 ? -1.0f : 1.0f;
                }
                WriteValue(output + size_t(v) * 4, PackDirection(tangent, sign));
            }
            return {};
        }

        ImportResult Builder::DecodeGeometry(ImportGeometryState& geometry) noexcept
        {
            if (sourceGroup == invalid) return {};
            auto& group = geometry.groups[sourceGroup];
            const auto& layout = groups[sourceGroup].record;
            for (uint32_t m = 0; m < meshes.count; ++m)
            {
                const auto& mesh = meshes[m];
                const auto& source = asset.meshes[m];
                for (uint32_t p = 0; p < source.primitives.size(); ++p)
                {
                    const auto& primitive = source.primitives[p];
                    auto& plan = primitives[mesh.firstPrimitive + p];
                    const size_t count = plan.vertexCount;
                    const auto destination = [&group, &layout, &plan](Attribute attribute, size_t stride) noexcept
                    { return group.vertices + layout.attributes[uint32_t(attribute)].offset + size_t(plan.vertexOffset) * stride; };
                    UVSR_IMPORT_TRY(document.ReadFloats(plan.accessors[Position], {floats.data, count * 3}));
                    memcpy(destination(Attribute::Position, 12), floats.data, count * 12);
                    for (size_t v = 0; v < count; ++v) Include(plan.bounds, {floats[v*3], floats[v*3+1], floats[v*3+2]});
                    if (primitive.indicesAccessor)
                        UVSR_IMPORT_TRY(document.ReadUnsigned(*primitive.indicesAccessor, {integers.data, plan.indexCount}));
                    else
                        for (uint32_t i = 0; i < plan.indexCount; ++i) integers[i] = i;
                    for (uint32_t i = 0; i < plan.indexCount; ++i)
                        if (integers[i] >= plan.vertexCount || integers[i] == plan.indexMaximum)
                            return Failure(ImportError::InvalidIndex, ImportObject::Primitive, mesh.firstPrimitive + p);
                    memcpy(group.indices + size_t(plan.indexOffset) * 4, integers.data, size_t(plan.indexCount) * 4);
                    const bool generate = plan.accessors[Normal] != SIZE_MAX && plan.accessors[Texcoord] != SIZE_MAX && plan.accessors[Tangent] == SIZE_MAX;
                    if (plan.accessors[Normal] != SIZE_MAX)
                    {
                        UVSR_IMPORT_TRY(document.ReadFloats(plan.accessors[Normal], {floats.data, count * 3}));
                        uint8_t* normals = destination(Attribute::Normal, 4);
                        for (size_t v = 0; v < count; ++v)
                        {
                            const Float3 normal{floats[v*3], floats[v*3+1], floats[v*3+2]};
                            WriteValue(normals + v * 4, PackDirection(normal));
                            if (generate) normalScratch[v] = normal;
                        }
                    }
                    if (plan.accessors[Tangent] != SIZE_MAX)
                    {
                        UVSR_IMPORT_TRY(document.ReadFloats(plan.accessors[Tangent], {floats.data, count * 4}));
                        uint8_t* tangents = destination(Attribute::Tangent, 4);
                        for (size_t v = 0; v < count; ++v)
                            WriteValue(tangents + v * 4, PackDirection({floats[v*4], floats[v*4+1], floats[v*4+2]}, floats[v*4+3]));
                    }
                    if (plan.accessors[Texcoord] != SIZE_MAX)
                    {
                        UVSR_IMPORT_TRY(document.ReadFloats(plan.accessors[Texcoord], {floats.data, count * 2}));
                        memcpy(destination(Attribute::TexCoord0, 8), floats.data, count * 8);
                    }
                    if (generate) UVSR_IMPORT_TRY(GenerateTangents(plan, group));
                    if (plan.accessors[Joints] != SIZE_MAX)
                    {
                        UVSR_IMPORT_TRY(document.ReadUnsigned(plan.accessors[Joints], {integers.data, count * 4}));
                        uint8_t* joints = destination(Attribute::JointIndices, 8);
                        for (size_t lane = 0; lane < count * 4; ++lane) WriteValue(joints + lane * 2, uint16_t(integers[lane]));
                        UVSR_IMPORT_TRY(document.ReadFloats(plan.accessors[Weights], {floats.data, count * 4}));
                        for (size_t lane = 0; lane < count * 4; ++lane)
                            if (floats[lane] < 0 || floats[lane] > 1) return Failure(ImportError::InvalidData, ImportObject::Accessor, plan.accessors[Weights]);
                        memcpy(destination(Attribute::JointWeights, 16), floats.data, count * 16);
                    }
                    if (plan.accessors[Radius] != SIZE_MAX)
                    {
                        UVSR_IMPORT_TRY(document.ReadFloats(plan.accessors[Radius], {floats.data, count}));
                        memcpy(destination(Attribute::CurveRadius, 4), floats.data, count * 4);
                        for (size_t v = 0; v < count; ++v) Include(plan.bounds, {floats[v], floats[v], floats[v]});
                    }
                    for (uint32_t target = 0; target < primitive.targets.size(); ++target)
                        for (const auto& attribute : primitive.targets[target])
                            if (attribute.name == "POSITION")
                            {
                                UVSR_IMPORT_TRY(document.ReadFloats(attribute.accessorIndex, {floats.data, count * 3}));
                                const size_t offset = (size_t(mesh.firstMorph + target) * totalVertices + plan.vertexOffset) * 16;
                                for (size_t v = 0; v < count; ++v)
                                {
                                    const Float3 delta{floats[v*3], floats[v*3+1], floats[v*3+2]};
                                    const gpu_contract::Float4 value{delta.x, delta.y, delta.z, 0};
                                    WriteValue(group.morphs + offset + v * 16, value);
                                    Include(plan.bounds, delta);
                                }
                            }
                }
            }
            return {};
        }

        ImportResult Builder::WriteScene(RendererScene& scene, ImportGeometryState&) noexcept
        {
            UVSR_IMPORT_WRITE(scene.Prepare(counts));
            RendererSceneNode root;
            UVSR_IMPORT_TRY(WriteString(scene, options.modelName, root.name));
            const uint32_t animationRoot = animationContainer != invalid ? animationContainer :
                firstAnimationNode != invalid ? firstAnimationNode : firstRuntimeNode;
            root.firstChildIndex = roots.count ? nodes[roots.data[0]].canonical : animationRoot;
            UVSR_IMPORT_WRITE(scene.Write(0, root));
            for (uint32_t n = 1; n < sourceNodeCount; ++n)
            {
                const uint32_t sourceIndex = nodeOrder[n];
                const auto& source = asset.nodes[sourceIndex];
                const auto& plan = nodes[sourceIndex];
                RendererSceneNode node;
                char generated[32];
                ArrayView<const char> name = plan.cameraNode == plan.canonical ? CameraName(sourceIndex, generated) :
                    ArrayView<const char>{source.name.data(), source.name.size()};
                if (options.runtimeLights.enabled && plan.lightNode == plan.canonical) name = NormalizeSceneLightName(name);
                UVSR_IMPORT_TRY(WriteString(scene, name, node.name));
                node.parentIndex = plan.parent == invalid ? 0 : nodes[plan.parent].canonical;
                node.firstChildIndex = plan.firstChild == invalid ? invalid : nodes[plan.firstChild].canonical;
                node.nextSiblingIndex = plan.nextSibling == invalid ? invalid : nodes[plan.nextSibling].canonical;
                if (plan.parent == invalid && plan.nextSibling == invalid) node.nextSiblingIndex = animationRoot;
                if (plan.instance != invalid) { node.leafKind = RendererSceneLeafKind::Instance; node.leafIndex = plan.instance; }
                else if (plan.camera != invalid) { node.leafKind = RendererSceneLeafKind::Camera; node.leafIndex = plan.camera; }
                else if (plan.light != invalid) { node.leafKind = RendererSceneLeafKind::Light; node.leafIndex = plan.light; }
                // reserve the authored node for a mesh, including a deferred skin.
                // auxiliary leaves precede authored children in camera/light order.
                if (plan.lightNode != invalid && plan.lightNode != plan.canonical)
                {
                    RendererSceneNode child;
                    child.parentIndex = plan.canonical;
                    child.nextSiblingIndex = node.firstChildIndex;
                    child.leafKind = RendererSceneLeafKind::Light;
                    child.leafIndex = plan.light;
                    UVSR_IMPORT_WRITE(scene.Write(plan.lightNode, child));
                    node.firstChildIndex = plan.lightNode;
                }
                if (plan.cameraNode != invalid && plan.cameraNode != plan.canonical)
                {
                    RendererSceneNode child;
                    child.parentIndex = plan.canonical;
                    child.nextSiblingIndex = node.firstChildIndex;
                    child.leafKind = RendererSceneLeafKind::Camera;
                    child.leafIndex = plan.camera;
                    UVSR_IMPORT_TRY(WriteString(scene, CameraName(sourceIndex, generated), child.name));
                    UVSR_IMPORT_WRITE(scene.Write(plan.cameraNode, child));
                    node.firstChildIndex = plan.cameraNode;
                }
                node.hasLocalTransform = (imported.metadata.nodeFlags[sourceIndex] & ImportNodeTransform) != 0;
                if (auto result = Transform(source, node.transform); !result) { result.index = sourceIndex; return result; }
                UVSR_IMPORT_WRITE(scene.Write(plan.canonical, node));
            }
            for (uint32_t m = 0; m < counts.meshes; ++m)
            {
                const auto& order = meshOrder[m];
                const auto& source = asset.meshes[order.source];
                const auto& plan = meshes[order.source];
                RendererSceneMesh mesh;
                UVSR_IMPORT_TRY(WriteString(scene, {source.name.data(), source.name.size()}, mesh.name));
                mesh.bufferGroupIndex = order.group;
                mesh.geometries = {order.firstGeometry, uint32_t(source.primitives.size())};
                mesh.vertexOffset = order.skinNode == invalid ? plan.vertexOffset : 0;
                mesh.indexOffset = plan.indexOffset;
                mesh.vertexCount = plan.vertexCount;
                mesh.indexCount = plan.indexCount;
                mesh.skinPrototypeIndex = order.skinNode == invalid ? invalid : plan.canonical;
                mesh.isSkinPrototype = order.skinNode == invalid && plan.skinPrototype;
                mesh.morphTargetAnimation = order.skinNode == invalid && plan.morphCount != 0;
                // the retained importer unions into a value-initialized mesh box,
                // so its conservative envelope includes the origin.
                Include(mesh.objectBounds, {0, 0, 0});
                mesh.hasDeclaredBounds = true;
                for (uint32_t p = 0; p < source.primitives.size(); ++p)
                {
                    const auto& primitive = primitives[plan.firstPrimitive + p];
                    if (!primitive.bounds.empty)
                    {
                        Include(mesh.objectBounds, primitive.bounds.minimum);
                        Include(mesh.objectBounds, primitive.bounds.maximum);
                    }
                    RendererSceneGeometry record;
                    record.materialIndex = materials[primitive.material].canonical;
                    record.objectBounds = primitive.bounds;
                    record.vertexOffsetInMesh = primitive.vertexOffset - plan.vertexOffset;
                    record.indexOffsetInMesh = primitive.indexOffset - plan.indexOffset;
                    record.vertexCount = primitive.vertexCount;
                    record.indexCount = primitive.indexCount;
                    record.primitive = primitive.type;
                    UVSR_IMPORT_WRITE(scene.Write(order.firstGeometry + p, record));
                }
                UVSR_IMPORT_WRITE(scene.Write(m, mesh));
            }
            uint32_t jointOffset = 0;
            for (uint32_t i = 0; i < counts.instances; ++i)
            {
                const uint32_t sourceNode = instanceOrder[i];
                const auto& source = asset.nodes[sourceNode];
                RendererSceneInstance instance;
                instance.nodeIndex = nodes[sourceNode].canonical;
                instance.meshIndex = nodes[sourceNode].mesh;
                instance.joints.first = jointOffset;
                if (source.skinIndex)
                {
                    const auto& skin = asset.skins[*source.skinIndex];
                    if (skin.inverseBindMatrices)
                    {
                        ImportAccessorInfo info;
                        UVSR_IMPORT_TRY(document.AccessorInfo(*skin.inverseBindMatrices, info));
                        UVSR_IMPORT_TRY(document.ReadFloats(*skin.inverseBindMatrices, {floats.data, info.scalarCount}));
                    }
                    instance.joints.count = uint32_t(skin.joints.size());
                    for (uint32_t j = 0; j < instance.joints.count; ++j)
                    {
                        RendererSceneJoint joint;
                        joint.nodeIndex = nodes[skin.joints[j]].canonical;
                        for (uint32_t lane = 0; lane < 16; ++lane)
                            joint.inverseBind.values[lane] = skin.inverseBindMatrices ? floats[size_t(j) * 16 + lane] : (lane % 5 == 0 ? 1.0f : 0.0f);
                        UVSR_IMPORT_WRITE(scene.Write(jointOffset++, joint));
                    }
                }
                UVSR_IMPORT_WRITE(scene.Write(i, instance));
            }
            for (uint32_t g = 0; g < counts.bufferGroups; ++g) UVSR_IMPORT_WRITE(scene.Write(g, groups[g].record));
            for (uint32_t m = 0; m < counts.morphRanges; ++m)
                UVSR_IMPORT_WRITE(scene.Write(m, RendererSceneByteRange{uint64_t(m) * totalVertices * 16, uint64_t(totalVertices) * 16}));
            for (uint32_t i = 0; i < counts.materials; ++i)
            {
                const uint32_t source = materialOrder[i];
                RendererSceneMaterial material;
                material.selectionId = i;
                if (source < asset.materials.size())
                {
                    const auto& importedMaterial = asset.materials[source];
                    UVSR_IMPORT_TRY(WriteString(scene, {importedMaterial.name.data(), importedMaterial.name.size()}, material.name));
                    UVSR_IMPORT_TRY(WriteString(scene, options.modelPath, material.modelFileName));
                    material.materialIndexInModel = int32_t(source);
                }
                else UVSR_IMPORT_TRY(WriteString(scene, {"(empty)", 7}, material.name));
                material.values = materials[source].values;
                material.originalValues = material.values;
                UVSR_IMPORT_WRITE(scene.Write(i, material));
            }
            for (uint32_t i = 0; i < counts.textures; ++i)
            {
                RendererSceneTexture texture;
                const auto& source = textures[textureOrder[i]];
                if (source.image != invalid)
                {
                    const auto& image = images[source.image];
                    UVSR_IMPORT_TRY(WriteString(scene, ImageText(image.path), texture.path));
                    UVSR_IMPORT_TRY(WriteString(scene, ImageText(image.mime), texture.mimeType));
                }
                // image decoding owns alpha mode and original bit depth.
                UVSR_IMPORT_WRITE(scene.Write(i, texture));
            }
            UVSR_IMPORT_TRY(WriteLightsAndCameras(scene));
            UVSR_IMPORT_TRY(WriteAnimations(scene));
            UVSR_IMPORT_TRY(WriteRuntimeLights(scene));
            if (stringOffset != counts.stringBytes) return Failure(ImportError::InvalidState);
            UVSR_IMPORT_TRY(sealWorkspace.Allocate(scene.SealWorkspaceBytes(), scratch));
            UVSR_IMPORT_WRITE(scene.Seal(0, {sealWorkspace.data, sealWorkspace.count}));
            UVSR_IMPORT_WRITE(scene.Publish(options.generation));
            UVSR_IMPORT_WRITE(scene.AdvancePreviousTransforms());
            return {};
        }

        ImportResult Builder::WriteLightsAndCameras(RendererScene& scene) noexcept
        {
            for (uint32_t n = 1; n < sourceNodeCount; ++n)
            {
                const uint32_t sourceNode = nodeOrder[n];
                const auto& plan = nodes[sourceNode];
                const auto& node = asset.nodes[sourceNode];
                if (plan.camera != invalid)
                {
                    RendererSceneCamera camera;
                    camera.nodeIndex = plan.cameraNode;
                    const auto& source = asset.cameras[*node.cameraIndex].camera;
                    if (const auto* perspective = std::get_if<fastgltf::Camera::Perspective>(&source))
                    {
                        camera.kind = RendererSceneCameraKind::Perspective;
                        camera.nearPlane = perspective->znear;
                        camera.verticalFov = perspective->yfov;
                        camera.hasFarPlane = bool(perspective->zfar);
                        camera.farPlane = perspective->zfar.value_or(1.f);
                        camera.hasAspectRatio = bool(perspective->aspectRatio);
                        camera.aspectRatio = perspective->aspectRatio.value_or(1.f);
                        if (camera.nearPlane <= 0 || camera.verticalFov <= 0 || camera.verticalFov >= 3.141592654f ||
                            (camera.hasFarPlane && camera.farPlane <= camera.nearPlane) ||
                            (camera.hasAspectRatio && camera.aspectRatio <= 0)) return Failure(ImportError::InvalidData, ImportObject::Camera, *node.cameraIndex);
                    }
                    else if (const auto* orthographic = std::get_if<fastgltf::Camera::Orthographic>(&source))
                    {
                        camera.kind = RendererSceneCameraKind::Orthographic;
                        camera.nearPlane = orthographic->znear;
                        camera.farPlane = orthographic->zfar;
                        camera.xMagnitude = orthographic->xmag;
                        camera.yMagnitude = orthographic->ymag;
                        if (camera.nearPlane < 0 || camera.farPlane <= camera.nearPlane || camera.xMagnitude <= 0 || camera.yMagnitude <= 0)
                            return Failure(ImportError::InvalidData, ImportObject::Camera, *node.cameraIndex);
                    }
                    else return Failure(ImportError::UnsupportedData, ImportObject::Camera, *node.cameraIndex);
                    UVSR_IMPORT_WRITE(scene.Write(plan.camera, camera));
                }
                if (plan.light != invalid)
                {
                    const auto& source = asset.lights[*node.lightIndex];
                    RendererSceneLight light;
                    light.nodeIndex = plan.lightNode;
                    light.values.color = {source.color[0], source.color[1], source.color[2]};
                    if (source.intensity < 0 || source.color[0] < 0 || source.color[1] < 0 || source.color[2] < 0 ||
                        (source.range && *source.range <= 0)) return Failure(ImportError::InvalidData, ImportObject::Light, *node.lightIndex);
                    if (source.type == fastgltf::LightType::Directional)
                    {
                        light.kind = RendererSceneLightKind::Directional;
                        light.values.irradiance = source.intensity;
                    }
                    else if (source.type == fastgltf::LightType::Point || source.type == fastgltf::LightType::Spot)
                    {
                        light.kind = source.type == fastgltf::LightType::Point ? RendererSceneLightKind::Point : RendererSceneLightKind::Spot;
                        light.values.intensity = source.intensity;
                        light.values.range = source.range.value_or(0.f);
                        if (light.kind == RendererSceneLightKind::Spot)
                        {
                            const float inner = source.innerConeAngle.value_or(0.f);
                            const float outer = source.outerConeAngle.value_or(3.141592654f / 4.f);
                            if (inner < 0 || outer <= 0 || inner >= outer || outer > 3.141592654f / 2.f)
                                return Failure(ImportError::InvalidData, ImportObject::Light, *node.lightIndex);
                            light.values.innerAngle = inner * (180.f / 3.141592654f);
                            light.values.outerAngle = outer * (180.f / 3.141592654f);
                        }
                    }
                    else return Failure(ImportError::UnsupportedData, ImportObject::Light, *node.lightIndex);
                    if (runtimeLights.sun && plan.light == runtimeLights.sun.index)
                    {
                        light.values.irradiance = options.runtimeLights.sun.values.irradiance;
                        light.values.angularSize = options.runtimeLights.sun.values.angularSize;
                    }
                    UVSR_IMPORT_WRITE(scene.Write(plan.light, light));
                }
            }
            return {};
        }

        ImportResult Builder::WriteRuntimeLights(RendererScene& scene) noexcept
        {
            if (!options.runtimeLights.enabled) return {};
            const ImportRuntimeLightSpec* specs[]{&options.runtimeLights.sun, &options.runtimeLights.flashlight};
            uint32_t nodeIndex = firstRuntimeNode;
            for (uint32_t s = appendSun ? 0u : 1u; s < 2; ++s, ++nodeIndex)
            {
                const auto& spec = *specs[s];
                const auto id = s == 0 ? runtimeLights.sun : runtimeLights.flashlight;
                RendererSceneNode node;
                node.parentIndex = 0;
                node.nextSiblingIndex = s == 0 ? nodeIndex + 1 : invalid;
                node.hasLocalTransform = true;
                node.transform = spec.transform;
                node.leafKind = RendererSceneLeafKind::Light;
                node.leafIndex = id.index;
                UVSR_IMPORT_TRY(WriteString(scene, spec.name, node.name));
                UVSR_IMPORT_WRITE(scene.Write(nodeIndex, node));
                UVSR_IMPORT_WRITE(scene.Write(id.index, RendererSceneLight{nodeIndex, spec.kind, spec.values}));
            }
            return {};
        }

        ImportResult Builder::WriteAnimations(RendererScene& scene) noexcept
        {
            if (animationContainer != invalid)
            {
                RendererSceneNode container;
                container.parentIndex = 0;
                container.firstChildIndex = firstAnimationNode;
                container.nextSiblingIndex = firstRuntimeNode;
                UVSR_IMPORT_TRY(WriteString(scene, {"Animations", 10}, container.name));
                UVSR_IMPORT_WRITE(scene.Write(animationContainer, container));
            }
            for (uint32_t a = 0; a < animations.count; ++a)
            {
                const auto& plan = animations[a];
                const auto& source = asset.animations[a];
                RendererSceneNode node;
                node.parentIndex = animationContainer != invalid ? animationContainer : 0;
                if (plan.canonical + 1 < counts.animations) node.nextSiblingIndex = plan.record.nodeIndex + 1;
                else if (animationContainer == invalid) node.nextSiblingIndex = firstRuntimeNode;
                node.leafKind = RendererSceneLeafKind::Animation;
                node.leafIndex = plan.canonical;
                UVSR_IMPORT_TRY(WriteString(scene, {source.name.data(), source.name.size()}, node.name));
                UVSR_IMPORT_WRITE(scene.Write(plan.record.nodeIndex, node));
                UVSR_IMPORT_WRITE(scene.Write(plan.canonical, plan.record));
                uint32_t channelIndex = plan.record.channels.first;
                for (const auto& sourceChannel : source.channels)
                {
                    if (!sourceChannel.nodeIndex || nodes[*sourceChannel.nodeIndex].canonical == invalid ||
                        sourceChannel.path == fastgltf::AnimationPath::Weights) continue;
                    const auto& sampler = animationSamplers[plan.samplerOffset + sourceChannel.samplerIndex];
                    if (sampler.canonical == invalid) continue;
                    RendererSceneAnimationChannel channel;
                    channel.nodeIndex = nodes[*sourceChannel.nodeIndex].canonical;
                    channel.samplerIndex = sampler.canonical;
                    switch (sourceChannel.path)
                    {
                    case fastgltf::AnimationPath::Translation: channel.attribute = RendererSceneAnimationAttribute::Translation; break;
                    case fastgltf::AnimationPath::Rotation: channel.attribute = RendererSceneAnimationAttribute::Rotation; break;
                    case fastgltf::AnimationPath::Scale: channel.attribute = RendererSceneAnimationAttribute::Scaling; break;
                    default: return Failure(ImportError::UnsupportedData, ImportObject::Animation, a);
                    }
                    UVSR_IMPORT_WRITE(scene.Write(channelIndex++, channel));
                }
                if (channelIndex != plan.record.channels.first + plan.record.channels.count) return Failure(ImportError::InvalidState);
            }
            for (uint32_t s = 0; s < counts.samplers; ++s)
            {
                const auto& plan = animationSamplers[animationSamplerOrder[s]];
                const uint32_t keyCount = plan.record.keyframes.count;
                const size_t factor = plan.record.interpolation == RendererSceneInterpolation::HermiteSpline ? 3 : 1;
                const size_t outputScalars = size_t(keyCount) * factor * plan.lanes;
                UVSR_IMPORT_TRY(document.ReadFloats(plan.input, {floats.data, keyCount}));
                UVSR_IMPORT_TRY(document.ReadFloats(plan.output, {floats.data + keyCount, outputScalars}));
                const float* values = floats.data + keyCount;
                const auto vector = [&plan](const float* input) noexcept -> gpu_contract::Float4
                { return {input[0], input[1], input[2], plan.lanes == 4 ? input[3] : 0.f}; };
                for (uint32_t k = 0; k < keyCount; ++k)
                {
                    RendererSceneKeyframe key;
                    key.time = floats[k];
                    if (key.time < 0 || (k && key.time <= floats[k - 1])) return Failure(ImportError::InvalidData, ImportObject::Accessor, plan.input);
                    const float* input = values + size_t(k) * factor * plan.lanes;
                    if (factor == 3)
                    {
                        key.inTangent = vector(input);
                        key.value = vector(input + plan.lanes);
                        key.outTangent = vector(input + 2 * plan.lanes);
                    }
                    else key.value = vector(input);
                    UVSR_IMPORT_WRITE(scene.Write(plan.record.keyframes.first + k, key));
                }
                UVSR_IMPORT_WRITE(scene.Write(s, plan.record));
            }
            return {};
        }

        bool Inverse(const RendererSceneAffine& source, RendererSceneAffine& result) noexcept
        {
            double a[3][3]{}, b[3][3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            for (uint32_t row = 0; row < 3; ++row)
                for (uint32_t col = 0; col < 3; ++col) a[row][col] = source.linear[row * 3 + col];
            const double epsilon = double(1e-6f);
            for (uint32_t col = 0; col < 3; ++col)
            {
                uint32_t pivot = col;
                for (uint32_t row = col + 1; row < 3; ++row)
                    if (fabs(a[row][col]) > fabs(a[pivot][col])) pivot = row;
                if (fabs(a[pivot][col]) < epsilon) return false;
                if (pivot != col)
                    for (uint32_t i = 0; i < 3; ++i)
                    {
                        const double av = a[col][i], bv = b[col][i];
                        a[col][i] = a[pivot][i]; b[col][i] = b[pivot][i];
                        a[pivot][i] = av; b[pivot][i] = bv;
                    }
                if (a[col][col] != 1)
                {
                    const double scale = a[col][col];
                    for (uint32_t i = 0; i < 3; ++i) { a[col][i] /= scale; b[col][i] /= scale; }
                }
                for (uint32_t row = 0; row < 3; ++row)
                    if (row != col && fabs(a[row][col]) > epsilon)
                    {
                        const double scale = -a[row][col];
                        for (uint32_t i = 0; i < 3; ++i) { a[row][i] += a[col][i] * scale; b[row][i] += b[col][i] * scale; }
                    }
            }
            for (uint32_t row = 0; row < 3; ++row)
                for (uint32_t col = 0; col < 3; ++col)
                {
                    if (!isfinite(b[row][col])) return false;
                    result.linear[row * 3 + col] = b[row][col];
                }
            for (uint32_t col = 0; col < 3; ++col)
            {
                result.translation[col] = -source.translation[0] * b[0][col] - source.translation[1] * b[1][col] - source.translation[2] * b[2][col];
                if (!isfinite(result.translation[col])) return false;
            }
            return true;
        }

        bool JointMatrix(const RendererSceneAffine& joint, const RendererSceneAffine& inverseRoot,
            const Matrix4& inverseBind, Matrix4& result) noexcept
        {
            Matrix4 local{};
            local.values[15] = 1;
            for (uint32_t row = 0; row < 4; ++row)
                for (uint32_t col = 0; col < 3; ++col)
                {
                    double value = 0;
                    for (uint32_t k = 0; k < 3; ++k)
                        value += (row == 3 ? joint.translation[k] : joint.linear[row * 3 + k]) * inverseRoot.linear[k * 3 + col];
                    if (row == 3) value += inverseRoot.translation[col];
                    if (!isfinite(value) || value < -FLT_MAX || value > FLT_MAX) return false;
                    local.values[row * 4 + col] = float(value);
                }
            for (uint32_t row = 0; row < 4; ++row)
                for (uint32_t col = 0; col < 4; ++col)
                {
                    float value = 0;
                    for (uint32_t k = 0; k < 4; ++k) value += inverseBind.values[row * 4 + k] * local.values[k * 4 + col];
                    if (!isfinite(value)) return false;
                    result.values[row * 4 + col] = value;
                }
            return true;
        }

        Float3 SkinVector(Float3 value, const Matrix4& matrix, float w) noexcept
        {
            const auto* m = matrix.values;
            return {value.x*m[0] + value.y*m[4] + value.z*m[8] + w*m[12],
                value.x*m[1] + value.y*m[5] + value.z*m[9] + w*m[13],
                value.x*m[2] + value.y*m[6] + value.z*m[10] + w*m[14]};
        }

        ImportResult PrepareInitialSkins(const RendererSceneView& scene, ImportGeometryState& geometry,
            ArrayView<const uint32_t> sourceOrder = {}) noexcept
        {
            for (uint32_t i = 0; i < scene.instances.count; ++i)
            {
                const auto& instance = scene.instances.data[i];
                if (!instance.joints.count) continue;
                const uint32_t sourceIndex = sourceOrder.count ? sourceOrder.data[i] : i;
                RendererSceneAffine inverseRoot;
                if (!Inverse(scene.nodes.data[instance.nodeIndex].world, inverseRoot))
                    return Failure(ImportError::InvalidData, ImportObject::Skin, sourceIndex);
                for (uint32_t j = 0; j < instance.joints.count; ++j)
                {
                    const auto& joint = scene.joints.data[instance.joints.first + j];
                    if (!JointMatrix(scene.nodes.data[joint.nodeIndex].world, inverseRoot, joint.inverseBind, geometry.jointMatrices[instance.joints.first + j]))
                        return Failure(ImportError::NonFiniteValue, ImportObject::Skin, sourceIndex);
                }
                const auto& mesh = scene.meshes.data[instance.meshIndex];
                const auto& prototype = scene.meshes.data[mesh.skinPrototypeIndex];
                const auto& source = geometry.groups[prototype.bufferGroupIndex];
                auto& output = geometry.groups[mesh.bufferGroupIndex];
                output.skinInstance = i;
                output.jointMatrices = instance.joints;
                const auto& sourceLayout = scene.bufferGroups.data[prototype.bufferGroupIndex];
                const auto input = [&source, &sourceLayout, &prototype](Attribute attribute, size_t stride, size_t vertex) noexcept
                { return source.vertices + sourceLayout.attributes[uint32_t(attribute)].offset + (prototype.vertexOffset + vertex) * stride; };
                // validate indices and finite position arithmetic before handing
                // these bytes to a GPU consumer. GPU skinning owns output rounding.
                for (uint32_t v = 0; v < mesh.vertexCount; ++v)
                {
                    Matrix4 weighted{};
                    const uint8_t* indices = input(Attribute::JointIndices, 8, v);
                    const uint8_t* weights = input(Attribute::JointWeights, 16, v);
                    for (uint32_t lane = 0; lane < 4; ++lane)
                    {
                        const uint16_t joint = ReadValue<uint16_t>(indices + lane * 2);
                        const float weight = ReadValue<float>(weights + lane * 4);
                        if (joint >= instance.joints.count) return Failure(ImportError::InvalidIndex, ImportObject::Skin, sourceIndex);
                        if (weight > 0)
                            for (uint32_t element = 0; element < 16; ++element)
                                weighted.values[element] += geometry.jointMatrices[instance.joints.first + joint].values[element] * weight;
                    }
                    for (float value : weighted.values)
                        if (!isfinite(value)) return Failure(ImportError::NonFiniteValue, ImportObject::Skin, sourceIndex);
                    const Float3 position = SkinVector(ReadValue<Float3>(input(Attribute::Position, 12, v)), weighted, 1);
                    if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z))
                        return Failure(ImportError::NonFiniteValue, ImportObject::Skin, sourceIndex);
                }
            }
            return {};
        }
    }

    ImportGeometry::~ImportGeometry() noexcept { Reset(); }
    ImportGeometry::ImportGeometry(ImportGeometry&& other) noexcept : m_State(other.m_State) { other.m_State = nullptr; }
    ImportGeometry& ImportGeometry::operator=(ImportGeometry&& other) noexcept
    {
        if (this != &other) { Reset(); m_State = other.m_State; other.m_State = nullptr; }
        return *this;
    }
    void ImportGeometry::Reset() noexcept { DestroyGeometry(m_State); m_State = nullptr; }
    size_t ImportGeometry::BufferCount() const noexcept { return m_State ? m_State->groups.count : 0; }
    size_t ImportGeometry::StorageBytes() const noexcept { return m_State ? m_State->storageBytes : 0; }
    size_t ImportGeometry::ConversionScratchBytes() const noexcept { return m_State ? m_State->scratchBytes : 0; }
    ImportGeometryBufferView ImportGeometry::Buffer(size_t index) const noexcept
    {
        if (index >= BufferCount()) return {};
        const auto& group = m_State->groups[index];
        const ArrayView<const Matrix4> matrices = group.jointMatrices.count
            ? ArrayView<const Matrix4>{m_State->jointMatrices.data + group.jointMatrices.first, group.jointMatrices.count} : ArrayView<const Matrix4>{};
        return {{group.indices, group.indexBytes}, {group.vertices, group.vertexBytes}, {group.morphs, group.morphBytes},
            group.indexOwner, matrices, group.skinInstance};
    }

    ImportTextures::~ImportTextures() noexcept { Reset(); }
    ImportTextures::ImportTextures(ImportTextures&& other) noexcept : m_State(other.m_State) { other.m_State = nullptr; }
    ImportTextures& ImportTextures::operator=(ImportTextures&& other) noexcept
    {
        if (this != &other) { Reset(); m_State = other.m_State; other.m_State = nullptr; }
        return *this;
    }
    void ImportTextures::Reset() noexcept { DestroyTextures(m_State); m_State = nullptr; }
    size_t ImportTextures::ImageCount() const noexcept { return m_State ? m_State->images.count : 0; }
    size_t ImportTextures::TextureCount() const noexcept { return m_State ? m_State->textureMap.count : 0; }
    size_t ImportTextures::TextureRequestCount() const noexcept { return m_State ? m_State->requests.count : 0; }
    size_t ImportTextures::StorageBytes() const noexcept { return m_State ? m_State->storageBytes : 0; }
    ImportImageView ImportTextures::Image(size_t index) const noexcept
    { return index < ImageCount() ? m_State->images[index] : ImportImageView{}; }
    ImportTextureView ImportTextures::Texture(size_t index) const noexcept
    {
        if (index >= TextureCount()) return {};
        return TextureRequest(m_State->textureMap[index]);
    }
    ImportTextureView ImportTextures::TextureRequest(size_t index) const noexcept
    {
        if (index >= TextureRequestCount()) return {};
        const auto& texture = m_State->requests[index];
        const auto range = texture.swizzles;
        return {texture.image, texture.srgb, range.count ?
            ArrayView<const ImportTextureSwizzle>{m_State->swizzles.data + range.first, range.count} : ArrayView<const ImportTextureSwizzle>{}, uint32_t(index)};
    }

    ImportResult ConvertImportScene(const ImportDocument& document, const ImportSceneOptions& options,
        RendererScene& scene, ImportGeometry& geometry, ImportTextures* textures, ImportRuntimeLightIds* runtimeLights) noexcept
    {
        if (!document.m_State || !options.generation || scene.StorageBytes() || geometry.m_State || (textures && textures->m_State))
            return Failure(ImportError::InvalidState);
        if (!options.modelName.IsValid() || !options.modelPath.IsValid()) return Failure(ImportError::InvalidInput);
        UVSR_IMPORT_TRY(ValidateImportRuntimeLights(options.runtimeLights));
        Builder builder(document, *document.m_State, options);
        UVSR_IMPORT_TRY(builder.PlanNodes());
        UVSR_IMPORT_TRY(builder.PlanAnimations());
        UVSR_IMPORT_TRY(builder.PlanRuntimeLights());
        UVSR_IMPORT_TRY(builder.PlanGeometry());
        UVSR_IMPORT_TRY(builder.PlanMeshes());
        UVSR_IMPORT_TRY(builder.PlanTextures());
        UVSR_IMPORT_TRY(builder.PlanMaterials());
        if (builder.counts.textures && !textures) return Failure(ImportError::InvalidOutput, ImportObject::Texture);
        GeometryCandidate candidate;
        if (options.maxGeometryBytes < sizeof(ImportGeometryState)) return Failure(ImportError::Capacity);
        auto* storage = ImportAllocate(sizeof(ImportGeometryState));
        if (!storage) return Failure(ImportError::OutOfMemory);
        candidate.state = new (storage) ImportGeometryState{};
        UVSR_IMPORT_TRY(builder.AllocateGeometry(*candidate.state));
        UVSR_IMPORT_TRY(builder.DecodeGeometry(*candidate.state));
        TextureCandidate textureCandidate;
        if (textures && builder.textureCount)
        {
            if (options.maxImageBytes < sizeof(ImportTexturesState)) return Failure(ImportError::Capacity, ImportObject::Image);
            auto* textureStorage = ImportAllocate(sizeof(ImportTexturesState));
            if (!textureStorage) return Failure(ImportError::OutOfMemory, ImportObject::Image);
            textureCandidate.state = new (textureStorage) ImportTexturesState{};
            UVSR_IMPORT_TRY(builder.AllocateTextures(*textureCandidate.state));
        }
        RendererScene prepared;
        UVSR_IMPORT_TRY(builder.WriteScene(prepared, *candidate.state));
        UVSR_IMPORT_TRY(PrepareInitialSkins(prepared.View(), *candidate.state,
            {builder.instanceOrder.data, builder.instanceOrder.count}));
        candidate.state->scratchBytes = builder.scratch.bytes;
        scene = static_cast<RendererScene&&>(prepared);
        geometry.m_State = candidate.state;
        candidate.state = nullptr;
        if (textures) { textures->m_State = textureCandidate.state; textureCandidate.state = nullptr; }
        if (runtimeLights) *runtimeLights = builder.runtimeLights;
        return {};
    }

    bool ImportCompositionAccess::Empty(const ImportGeometry& geometry, const ImportTextures& textures) noexcept
    { return !geometry.m_State && !textures.m_State; }

    ImportResult ImportCompositionAccess::CopyTextures(ArrayView<const ImportImageView> images,
        ArrayView<const ImportTextureView> requests, ArrayView<const uint32_t> textureMap,
        size_t maxBytes, ImportTextures& output) noexcept
    {
        if (output.m_State || !images.IsValid() || !requests.IsValid() || !textureMap.IsValid())
            return Failure(ImportError::InvalidState, ImportObject::Texture);
        if (!images.count && !requests.count && !textureMap.count) return {};
        if (images.count >= invalid || requests.count >= invalid || textureMap.count >= invalid)
            return Failure(ImportError::Capacity, ImportObject::Texture);
        size_t strings = 0, bytes = 0, swizzles = 0;
        Budget measured;
        for (size_t i = 0; i < images.count; ++i)
        {
            const auto& image = images.data[i];
            if (!image.path.IsValid() || !image.mimeType.IsValid() || !image.bytes.IsValid())
                return Failure(ImportError::InvalidInput, ImportObject::Image);
            UVSR_IMPORT_TRY(measured.Add(image.path.count, 1));
            UVSR_IMPORT_TRY(measured.Add(image.mimeType.count, 1));
            strings = measured.bytes;
            if (image.bytes.count > size_t(PTRDIFF_MAX) - bytes) return Failure(ImportError::Overflow, ImportObject::Image);
            bytes += image.bytes.count;
        }
        for (size_t i = 0; i < requests.count; ++i)
        {
            const auto& request = requests.data[i];
            if ((request.imageIndex != invalid && request.imageIndex >= images.count) || !request.swizzles.IsValid())
                return Failure(ImportError::InvalidIndex, ImportObject::Texture);
            if (request.swizzles.count > UINT32_MAX - swizzles) return Failure(ImportError::Capacity, ImportObject::Texture);
            swizzles += request.swizzles.count;
            for (size_t j = 0; j < request.swizzles.count; ++j)
                if (request.swizzles.data[j].imageIndex >= images.count || request.swizzles.data[j].channelCount > 4)
                    return Failure(ImportError::InvalidIndex, ImportObject::Image);
        }
        for (size_t i = 0; i < textureMap.count; ++i)
            if (textureMap.data[i] >= requests.count) return Failure(ImportError::InvalidIndex, ImportObject::Texture);
        if (maxBytes < sizeof(ImportTexturesState)) return Failure(ImportError::Capacity, ImportObject::Texture);
        TextureCandidate candidate;
        auto* memory = ImportAllocate(sizeof(ImportTexturesState));
        if (!memory) return Failure(ImportError::OutOfMemory, ImportObject::Texture);
        candidate.state = new (memory) ImportTexturesState{};
        auto& state = *candidate.state;
        Budget storage{sizeof(ImportTexturesState), maxBytes, ImportError::Capacity};
        UVSR_IMPORT_TRY(state.images.Allocate(images.count, storage));
        UVSR_IMPORT_TRY(state.requests.Allocate(requests.count, storage));
        UVSR_IMPORT_TRY(state.textureMap.Allocate(textureMap.count, storage));
        UVSR_IMPORT_TRY(state.swizzles.Allocate(swizzles, storage));
        UVSR_IMPORT_TRY(state.strings.Allocate(strings, storage));
        UVSR_IMPORT_TRY(state.bytes.Allocate(bytes, storage));
        size_t stringOffset = 0, byteOffset = 0;
        const auto copyString = [&state, &stringOffset](ArrayView<const char> text) noexcept -> ArrayView<const char>
        {
            if (!text.count) return {};
            auto* destination = state.strings.data + stringOffset;
            memcpy(destination, text.data, text.count);
            stringOffset += text.count;
            return {destination, text.count};
        };
        for (size_t i = 0; i < images.count; ++i)
        {
            const auto& source = images.data[i];
            auto& image = state.images[i];
            image.path = copyString(source.path);
            image.mimeType = copyString(source.mimeType);
            image.embedded = source.embedded;
            if (source.bytes.count)
            {
                image.bytes = {state.bytes.data + byteOffset, source.bytes.count};
                memcpy(state.bytes.data + byteOffset, source.bytes.data, source.bytes.count);
                byteOffset += source.bytes.count;
            }
        }
        uint32_t swizzleOffset = 0;
        for (size_t i = 0; i < requests.count; ++i)
        {
            const auto& request = requests.data[i];
            state.requests[i] = {request.imageIndex, {swizzleOffset, uint32_t(request.swizzles.count)}, request.forceSRGB};
            for (size_t j = 0; j < request.swizzles.count; ++j) state.swizzles[swizzleOffset++] = request.swizzles.data[j];
        }
        for (size_t i = 0; i < textureMap.count; ++i) state.textureMap[i] = textureMap.data[i];
        state.storageBytes = storage.bytes;
        output.m_State = candidate.state;
        candidate.state = nullptr;
        return {};
    }

    ImportResult ImportCompositionAccess::TakeGeometry(ArrayView<ImportModel> models,
        ArrayView<const CompositionGeometrySource> sources, const RendererSceneView& scene,
        size_t maxBytes, size_t scratchBytes, ImportGeometry& output) noexcept
    {
        if (output.m_State || !models.IsValid() || !sources.IsValid() || sources.count != scene.bufferGroups.count)
            return Failure(ImportError::InvalidState);
        if (maxBytes < sizeof(ImportGeometryState)) return Failure(ImportError::Capacity);
        GeometryCandidate candidate;
        auto* memory = ImportAllocate(sizeof(ImportGeometryState));
        if (!memory) return Failure(ImportError::OutOfMemory);
        candidate.state = new (memory) ImportGeometryState{};
        auto& state = *candidate.state;
        state.ownsPayloads = false;
        Budget storage{sizeof(ImportGeometryState), maxBytes, ImportError::Capacity};
        UVSR_IMPORT_TRY(state.groups.Allocate(sources.count, storage));
        UVSR_IMPORT_TRY(state.jointMatrices.Allocate(scene.joints.count, storage));
        for (size_t i = 0; i < sources.count; ++i)
        {
            const auto& mapping = sources.data[i];
            if (mapping.model >= models.count || mapping.group >= models.data[mapping.model].geometry.BufferCount() ||
                mapping.indexOwner >= sources.count || (mapping.skinInstance != invalid && mapping.skinInstance >= scene.instances.count))
                return Failure(ImportError::InvalidIndex, ImportObject::Buffer, i);
            const auto& source = models.data[mapping.model].geometry.m_State->groups[mapping.group];
            auto& group = state.groups[i];
            group.indices = source.indices;
            group.indexBytes = source.indexBytes;
            group.indexOwner = mapping.indexOwner;
            const auto& layout = scene.bufferGroups.data[i];
            if (layout.indexBytes != group.indexBytes) return Failure(ImportError::InvalidRange, ImportObject::Buffer, i);
            if (mapping.skinInstance == invalid)
            {
                if (source.skinInstance != invalid || layout.vertexBytes != source.vertexBytes || layout.morphBytes != source.morphBytes)
                    return Failure(ImportError::InvalidRange, ImportObject::Buffer, i);
                for (size_t prior = 0; prior < i; ++prior)
                    if (sources.data[prior].skinInstance == invalid && sources.data[prior].model == mapping.model && sources.data[prior].group == mapping.group)
                        return Failure(ImportError::InvalidState, ImportObject::Buffer, i);
                group.vertices = source.vertices;
                group.vertexBytes = source.vertexBytes;
                group.morphs = source.morphs;
                group.morphBytes = source.morphBytes;
                UVSR_IMPORT_TRY(storage.Add(group.vertexBytes, 1));
                UVSR_IMPORT_TRY(storage.Add(group.morphBytes, 1));
            }
            else if (layout.morphBytes || source.vertexBytes || source.morphBytes)
                return Failure(ImportError::InvalidRange, ImportObject::Buffer, i);
            if (mapping.indexOwner == i)
            {
                if (source.indexOwner != mapping.group || mapping.skinInstance != invalid)
                    return Failure(ImportError::InvalidState, ImportObject::Buffer, i);
                UVSR_IMPORT_TRY(storage.Add(group.indexBytes, 1));
            }
        }
        for (size_t i = 0; i < sources.count; ++i)
        {
            const auto& group = state.groups[i];
            const auto& owner = state.groups[group.indexOwner];
            if (owner.indexOwner != group.indexOwner || owner.indices != group.indices || owner.indexBytes != group.indexBytes)
                return Failure(ImportError::InvalidState, ImportObject::Buffer, i);
        }
        UVSR_IMPORT_TRY(PrepareInitialSkins(scene, state));
        state.storageBytes = storage.bytes;
        state.scratchBytes = scratchBytes;
        // no operation below can fail. input views expire with the consumed owners.
        for (size_t i = 0; i < sources.count; ++i)
        {
            const auto& mapping = sources.data[i];
            if (mapping.skinInstance != invalid) continue;
            auto& source = models.data[mapping.model].geometry.m_State->groups[mapping.group];
            source.vertices = nullptr;
            source.vertexBytes = 0;
            source.morphs = nullptr;
            source.morphBytes = 0;
            if (source.indexOwner == mapping.group) { source.indices = nullptr; source.indexBytes = 0; }
        }
        state.ownsPayloads = true;
        output.m_State = candidate.state;
        candidate.state = nullptr;
        return {};
    }

#undef UVSR_IMPORT_WRITE
#undef UVSR_IMPORT_TRY
}
