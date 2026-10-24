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

#include "renderer_scene.h"

#include <math.h>
#include <float.h>
#include <string.h>
// private fallible allocation establishes live C++17 array objects.
#include <new>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error renderer scene owner requires exception-disabled compilation
#endif

namespace uvsr
{
    namespace
    {
#if defined(UVSR_BUILD_TESTING)
        thread_local uint32_t allocationFailure = 0;
        thread_local uint32_t allocationOrdinal = 0;
#endif

        bool CanAllocate() noexcept
        {
#if defined(UVSR_BUILD_TESTING)
            ++allocationOrdinal;
            return allocationFailure == 0 || allocationOrdinal != allocationFailure;
#else
            return true;
#endif
        }

        template<class T>
        T* Allocate(uint32_t count) noexcept
        {
            static_assert(noexcept(T{}), "scene array constructors must not throw");
            if (count == 0 || !CanAllocate())
                return nullptr;
            return new (std::nothrow) T[count]{};
        }

        template<class T>
        bool AddBytes(uint32_t count, size_t& bytes) noexcept
        {
            if (size_t(count) > (SIZE_MAX - bytes) / sizeof(T) ||
                size_t(count) > size_t(PTRDIFF_MAX) / sizeof(T))
                return false;
            bytes += size_t(count) * sizeof(T);
            return true;
        }

        bool IsFinite(gpu_contract::Float3 value) noexcept
        {
            return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
        }

        bool IsFinite(gpu_contract::Float4 value) noexcept
        {
            return isfinite(value.x) && isfinite(value.y) && isfinite(value.z) && isfinite(value.w);
        }

        bool IsFinite(const RendererSceneAffine& value) noexcept
        {
            for (double element : value.linear)
                if (!isfinite(element)) return false;
            for (double element : value.translation)
                if (!isfinite(element)) return false;
            return true;
        }

        bool ValidWorld(const RendererSceneAffine& value) noexcept
        {
            for (double element : value.linear)
                if (!isfinite(element) || element < -double(FLT_MAX) || element > double(FLT_MAX)) return false;
            for (double element : value.translation)
                if (!isfinite(element) || element < -double(FLT_MAX) || element > double(FLT_MAX)) return false;
            return true;
        }

        bool ValidBounds(const RendererSceneBounds& bounds) noexcept
        {
            return bounds.empty || (IsFinite(bounds.minimum) && IsFinite(bounds.maximum) &&
                bounds.minimum.x <= bounds.maximum.x && bounds.minimum.y <= bounds.maximum.y &&
                bounds.minimum.z <= bounds.maximum.z);
        }

        bool ValidTransform(const RendererSceneTransform& transform) noexcept
        {
            for (double value : transform.translation)
                if (!isfinite(value)) return false;
            for (double value : transform.scaling)
                if (!isfinite(value)) return false;
            for (double value : transform.rotation)
                if (!isfinite(value)) return false;
            return true;
        }

        bool ValidMaterial(const RendererSceneMaterialValues& value, uint32_t textureCount) noexcept
        {
            if (value.domain >= RendererMaterialDomain::Count ||
                !IsFinite(value.baseOrDiffuseColor) || !IsFinite(value.specularColor) ||
                !IsFinite(value.emissiveColor) || !IsFinite(value.subsurface.transmissionColor) ||
                !IsFinite(value.subsurface.scatteringColor) || !IsFinite(value.hair.baseColor) ||
                !IsFinite(value.hair.diffuseReflectionTint))
                return false;
            for (uint32_t texture : value.textures)
                if (texture != InvalidSceneIndex && texture >= textureCount) return false;
            const float scalars[]{value.emissiveIntensity, value.metalness, value.roughness,
                value.opacity, value.alphaCutoff, value.transmissionFactor, value.normalTextureScale,
                value.occlusionStrength, value.normalTextureTransformScale.x, value.normalTextureTransformScale.y,
                value.subsurface.scale, value.subsurface.anisotropy, value.hair.melanin, value.hair.melaninRedness,
                value.hair.longitudinalRoughness, value.hair.azimuthalRoughness,
                value.hair.diffuseReflectionWeight, value.hair.ior, value.hair.cuticleAngle};
            for (float scalar : scalars)
                if (!isfinite(scalar)) return false;
            return true;
        }

        bool ValidLight(const RendererSceneLightValues& value) noexcept
        {
            return IsFinite(value.color) && isfinite(value.irradiance) && isfinite(value.angularSize) &&
                isfinite(value.intensity) && isfinite(value.radius) && isfinite(value.range) &&
                isfinite(value.innerAngle) && isfinite(value.outerAngle);
        }

        bool Same(gpu_contract::Float3 left, gpu_contract::Float3 right) noexcept
        {
            return left.x == right.x && left.y == right.y && left.z == right.z;
        }

        bool Same(const RendererSceneAffine& left, const RendererSceneAffine& right) noexcept
        {
            for (uint32_t lane = 0; lane < 9; ++lane)
                if (left.linear[lane] != right.linear[lane]) return false;
            for (uint32_t lane = 0; lane < 3; ++lane)
                if (left.translation[lane] != right.translation[lane]) return false;
            return true;
        }

        bool Same(const RendererSceneMaterialValues& left, const RendererSceneMaterialValues& right) noexcept
        {
            for (uint32_t i = 0; i < uint32_t(RendererSceneMaterialTextureSlot::Count); ++i)
                if (left.textures[i] != right.textures[i]) return false;
#define UVSR_SCENE_SAME(field) if (left.field != right.field) return false;
            UVSR_SCENE_SAME(domain)
            UVSR_SCENE_SAME(emissiveIntensity)
            UVSR_SCENE_SAME(metalness)
            UVSR_SCENE_SAME(roughness)
            UVSR_SCENE_SAME(opacity)
            UVSR_SCENE_SAME(alphaCutoff)
            UVSR_SCENE_SAME(transmissionFactor)
            UVSR_SCENE_SAME(normalTextureScale)
            UVSR_SCENE_SAME(occlusionStrength)
            UVSR_SCENE_SAME(normalTextureTransformScale.x)
            UVSR_SCENE_SAME(normalTextureTransformScale.y)
            UVSR_SCENE_SAME(useSpecularGlossModel)
            UVSR_SCENE_SAME(enableSubsurfaceScattering)
            UVSR_SCENE_SAME(subsurface.scale)
            UVSR_SCENE_SAME(subsurface.anisotropy)
            UVSR_SCENE_SAME(enableHair)
            UVSR_SCENE_SAME(hair.melanin)
            UVSR_SCENE_SAME(hair.melaninRedness)
            UVSR_SCENE_SAME(hair.longitudinalRoughness)
            UVSR_SCENE_SAME(hair.azimuthalRoughness)
            UVSR_SCENE_SAME(hair.diffuseReflectionWeight)
            UVSR_SCENE_SAME(hair.ior)
            UVSR_SCENE_SAME(hair.cuticleAngle)
            UVSR_SCENE_SAME(enableBaseOrDiffuseTexture)
            UVSR_SCENE_SAME(enableMetalRoughOrSpecularTexture)
            UVSR_SCENE_SAME(enableNormalTexture)
            UVSR_SCENE_SAME(enableEmissiveTexture)
            UVSR_SCENE_SAME(enableOcclusionTexture)
            UVSR_SCENE_SAME(enableTransmissionTexture)
            UVSR_SCENE_SAME(enableOpacityTexture)
            UVSR_SCENE_SAME(doubleSided)
            UVSR_SCENE_SAME(metalnessInRedChannel)
#undef UVSR_SCENE_SAME
            return Same(left.baseOrDiffuseColor, right.baseOrDiffuseColor) &&
                Same(left.specularColor, right.specularColor) && Same(left.emissiveColor, right.emissiveColor) &&
                Same(left.subsurface.transmissionColor, right.subsurface.transmissionColor) &&
                Same(left.subsurface.scatteringColor, right.subsurface.scatteringColor) &&
                Same(left.hair.baseColor, right.hair.baseColor) &&
                Same(left.hair.diffuseReflectionTint, right.hair.diffuseReflectionTint);
        }

        bool Same(const RendererSceneLightValues& left, const RendererSceneLightValues& right) noexcept
        {
            return Same(left.color, right.color) && left.irradiance == right.irradiance &&
                left.angularSize == right.angularSize && left.intensity == right.intensity &&
                left.radius == right.radius && left.range == right.range &&
                left.innerAngle == right.innerAngle && left.outerAngle == right.outerAngle;
        }

        bool Same(const RendererSceneTransform& left, const RendererSceneTransform& right) noexcept
        {
            for (uint32_t i = 0; i < 3; ++i)
                if (left.translation[i] != right.translation[i] || left.scaling[i] != right.scaling[i]) return false;
            for (uint32_t i = 0; i < 4; ++i)
                if (left.rotation[i] != right.rotation[i]) return false;
            return true;
        }

        RendererSceneAffine Compose(const RendererSceneAffine& left, const RendererSceneAffine& right) noexcept
        {
            RendererSceneAffine result;
            for (uint32_t row = 0; row < 3; ++row)
                for (uint32_t column = 0; column < 3; ++column)
                    result.linear[row * 3 + column] = left.linear[row * 3] * right.linear[column] +
                        left.linear[row * 3 + 1] * right.linear[3 + column] +
                        left.linear[row * 3 + 2] * right.linear[6 + column];
            for (uint32_t column = 0; column < 3; ++column)
                result.translation[column] = left.translation[0] * right.linear[column] +
                    left.translation[1] * right.linear[3 + column] +
                    left.translation[2] * right.linear[6 + column] + right.translation[column];
            return result;
        }

        RendererSceneAffine LocalTransform(const RendererSceneTransform& transform) noexcept
        {
            const double x = transform.rotation[0], y = transform.rotation[1];
            const double z = transform.rotation[2], w = transform.rotation[3];
            const RendererSceneAffine rotation{{
                1 - 2 * (y*y + z*z), 2 * (x*y + z*w), 2 * (x*z - y*w),
                2 * (x*y - z*w), 1 - 2 * (x*x + z*z), 2 * (y*z + x*w),
                2 * (x*z + y*w), 2 * (y*z - x*w), 1 - 2 * (x*x + y*y)}, {0, 0, 0}};
            RendererSceneAffine scaling;
            scaling.linear[0] = transform.scaling[0];
            scaling.linear[4] = transform.scaling[1];
            scaling.linear[8] = transform.scaling[2];
            RendererSceneAffine translation;
            for (uint32_t i = 0; i < 3; ++i) translation.translation[i] = transform.translation[i];
            return Compose(Compose(scaling, rotation), translation);
        }

        RendererSceneBounds TransformBounds(const RendererSceneBounds& source, const RendererSceneAffine& world) noexcept
        {
            if (source.empty) return {};
            const float minima[]{source.minimum.x, source.minimum.y, source.minimum.z};
            const float maxima[]{source.maximum.x, source.maximum.y, source.maximum.z};
            float outMinimum[3], outMaximum[3];
            for (uint32_t column = 0; column < 3; ++column)
            {
                outMinimum[column] = outMaximum[column] = float(world.translation[column]);
                for (uint32_t row = 0; row < 3; ++row)
                {
                    const float linear = float(world.linear[row * 3 + column]);
                    const float first = minima[row] * linear;
                    const float second = maxima[row] * linear;
                    outMinimum[column] += first < second ? first : second;
                    outMaximum[column] += first > second ? first : second;
                }
            }
            return {{outMinimum[0], outMinimum[1], outMinimum[2]},
                {outMaximum[0], outMaximum[1], outMaximum[2]}, false};
        }

        void UnionBounds(RendererSceneBounds& destination, const RendererSceneBounds& source) noexcept
        {
            if (source.empty) return;
            if (destination.empty) { destination = source; return; }
#define UVSR_SCENE_UNION(component) \
            if (source.minimum.component < destination.minimum.component) destination.minimum.component = source.minimum.component; \
            if (source.maximum.component > destination.maximum.component) destination.maximum.component = source.maximum.component;
            UVSR_SCENE_UNION(x)
            UVSR_SCENE_UNION(y)
            UVSR_SCENE_UNION(z)
#undef UVSR_SCENE_UNION
        }

        bool ValidByteRange(RendererSceneByteRange range, uint64_t capacity) noexcept
        {
            return range.offset <= capacity && range.size <= capacity - range.offset;
        }

        bool ConsumeRange(RendererSceneRange range, uint32_t count, uint32_t& consumed) noexcept
        {
            if (range.first != consumed || consumed > count || range.count > count - consumed) return false;
            consumed += range.count;
            return true;
        }
    }

    RendererSceneBounds TransformRendererSceneBounds(const RendererSceneBounds& source,
        const RendererSceneAffine& world) noexcept
    {
        return TransformBounds(source, world);
    }

#define UVSR_SCENE_TABLES(X) \
    X(RendererSceneNode, nodes, nodes) \
    X(RendererSceneMesh, meshes, meshes) \
    X(RendererSceneGeometry, geometries, geometries) \
    X(RendererSceneInstance, instances, instances) \
    X(RendererSceneMaterial, materials, materials) \
    X(RendererSceneTexture, textures, textures) \
    X(RendererSceneBufferGroup, bufferGroups, bufferGroups) \
    X(RendererSceneByteRange, morphRanges, morphRanges) \
    X(RendererSceneJoint, joints, joints) \
    X(RendererSceneLight, lights, lights) \
    X(RendererSceneCamera, cameras, cameras) \
    X(RendererSceneAnimation, animations, animations) \
    X(RendererSceneAnimationChannel, channels, channels) \
    X(RendererSceneAnimationSampler, samplers, samplers) \
    X(RendererSceneKeyframe, keyframes, keyframes) \
    X(char, strings, stringBytes)

    struct RendererScene::State
    {
        RendererSceneCounts counts;
        uint32_t root = InvalidSceneIndex;
        uint64_t generation = 0;
        uint64_t contentRevision = 1;
        uint64_t materialRevision = 1;
        uint64_t lightRevision = 1;
        uint64_t transformRevision = 1;
        uint64_t previousTransformRevision = 1;
        uint64_t instanceTransformRevision = 1;
        uint64_t previousInstanceTransformRevision = 1;
        size_t storageBytes = 0;
        bool sealed = false;
#define UVSR_SCENE_POINTER(type, name, count) type* name = nullptr;
        UVSR_SCENE_TABLES(UVSR_SCENE_POINTER)
#undef UVSR_SCENE_POINTER
        uint32_t* preorder = nullptr;
        // reusable transaction workspace. proposed worlds never overwrite live data on failure.
        RendererSceneAffine* transformScratch = nullptr;

        ~State()
        {
#define UVSR_SCENE_RELEASE(type, name, count) delete[] name;
            UVSR_SCENE_TABLES(UVSR_SCENE_RELEASE)
#undef UVSR_SCENE_RELEASE
            delete[] preorder;
            delete[] transformScratch;
        }

        uint32_t LeafContent(uint32_t index) const noexcept
        {
            const auto& node = nodes[index];
            if (node.leafKind == RendererSceneLeafKind::Light) return SceneContentLight;
            if (node.leafKind == RendererSceneLeafKind::Camera) return SceneContentCamera;
            if (node.leafKind == RendererSceneLeafKind::Animation) return SceneContentAnimation;
            if (node.leafKind != RendererSceneLeafKind::Instance) return SceneContentNone;
            const auto& mesh = meshes[instances[node.leafIndex].meshIndex];
            uint32_t content = SceneContentNone;
            for (uint32_t i = 0; i < mesh.geometries.count; ++i)
            {
                const auto domain = materials[geometries[mesh.geometries.first + i].materialIndex].values.domain;
                content |= domain == RendererMaterialDomain::Opaque ? SceneContentOpaque :
                    domain == RendererMaterialDomain::AlphaTested ? SceneContentAlphaTested : SceneContentBlended;
            }
            return content;
        }

        RendererSceneBounds LeafBounds(uint32_t index, const RendererSceneAffine& world) const noexcept
        {
            const auto& node = nodes[index];
            if (node.leafKind != RendererSceneLeafKind::Instance) return {};
            return TransformBounds(meshes[instances[node.leafIndex].meshIndex].objectBounds, world);
        }

        void RefreshDerivedNode(uint32_t index) noexcept
        {
            auto& node = nodes[index];
            node.worldBounds = LeafBounds(index, node.world);
            node.leafContent = LeafContent(index);
            node.subtreeContent = node.leafContent;
            for (uint32_t child = node.firstChildIndex; child != InvalidSceneIndex; child = nodes[child].nextSiblingIndex)
            {
                UnionBounds(node.worldBounds, nodes[child].worldBounds);
                node.subtreeContent |= nodes[child].subtreeContent;
            }
        }

        void RefreshDerivedSubtree(uint32_t index) noexcept
        {
            const auto& node = nodes[index];
            for (uint32_t end = node.subtreeEnd; end > node.preorderIndex; --end)
                RefreshDerivedNode(preorder[end - 1]);
            for (uint32_t parent = node.parentIndex; parent != InvalidSceneIndex; parent = nodes[parent].parentIndex)
                RefreshDerivedNode(parent);
        }
    };

    RendererScene::~RendererScene()
    {
        Reset();
    }

    RendererScene::RendererScene(RendererScene&& other) noexcept : m_state(other.m_state)
    {
        other.m_state = nullptr;
    }

    RendererScene& RendererScene::operator=(RendererScene&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_state = other.m_state;
            other.m_state = nullptr;
        }
        return *this;
    }

    void RendererScene::Reset() noexcept
    {
        delete m_state;
        m_state = nullptr;
    }

    RendererSceneResult RendererScene::Prepare(const RendererSceneCounts& counts) noexcept
    {
        if (m_state)
            return {RendererSceneError::InvalidState};
        size_t bytes = sizeof(State);
#define UVSR_SCENE_SIZE(type, name, count) \
        if (!AddBytes<type>(counts.count, bytes)) return {RendererSceneError::Capacity};
        UVSR_SCENE_TABLES(UVSR_SCENE_SIZE)
#undef UVSR_SCENE_SIZE
        if (!AddBytes<uint32_t>(counts.nodes, bytes) || !AddBytes<RendererSceneAffine>(counts.nodes, bytes))
            return {RendererSceneError::Capacity};
        State* candidate = CanAllocate() ? new (std::nothrow) State{} : nullptr;
        if (!candidate)
            return {RendererSceneError::Allocation};
        candidate->counts = counts;
        candidate->storageBytes = bytes;
#define UVSR_SCENE_ALLOCATE(type, name, count) \
        candidate->name = Allocate<type>(counts.count); \
        if (counts.count && !candidate->name) { delete candidate; return {RendererSceneError::Allocation}; }
        UVSR_SCENE_TABLES(UVSR_SCENE_ALLOCATE)
#undef UVSR_SCENE_ALLOCATE
        candidate->preorder = Allocate<uint32_t>(counts.nodes);
        if (counts.nodes && !candidate->preorder)
        {
            delete candidate;
            return {RendererSceneError::Allocation};
        }
        candidate->transformScratch = Allocate<RendererSceneAffine>(counts.nodes);
        if (counts.nodes && !candidate->transformScratch)
        {
            delete candidate;
            return {RendererSceneError::Allocation};
        }
        m_state = candidate;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneNode& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.nodes) return {RendererSceneError::Reference, index};
        m_state->nodes[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneMesh& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.meshes) return {RendererSceneError::Reference, index};
        m_state->meshes[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneGeometry& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.geometries) return {RendererSceneError::Reference, index};
        m_state->geometries[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneInstance& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.instances) return {RendererSceneError::Reference, index};
        m_state->instances[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneMaterial& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.materials) return {RendererSceneError::Reference, index};
        m_state->materials[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneTexture& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.textures) return {RendererSceneError::Reference, index};
        m_state->textures[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneBufferGroup& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.bufferGroups) return {RendererSceneError::Reference, index};
        m_state->bufferGroups[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneByteRange& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.morphRanges) return {RendererSceneError::Reference, index};
        m_state->morphRanges[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneJoint& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.joints) return {RendererSceneError::Reference, index};
        m_state->joints[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneLight& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.lights) return {RendererSceneError::Reference, index};
        m_state->lights[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneCamera& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.cameras) return {RendererSceneError::Reference, index};
        m_state->cameras[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneAnimation& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.animations) return {RendererSceneError::Reference, index};
        m_state->animations[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneAnimationChannel& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.channels) return {RendererSceneError::Reference, index};
        m_state->channels[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneAnimationSampler& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.samplers) return {RendererSceneError::Reference, index};
        m_state->samplers[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::Write(uint32_t index, const RendererSceneKeyframe& value) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (index >= m_state->counts.keyframes) return {RendererSceneError::Reference, index};
        m_state->keyframes[index] = value;
        return {};
    }

    RendererSceneResult RendererScene::WriteStrings(uint32_t offset, ArrayView<const char> bytes) noexcept
    {
        if (!m_state || m_state->sealed) return {RendererSceneError::InvalidState};
        if (!bytes.IsValid() || offset > m_state->counts.stringBytes || bytes.count > m_state->counts.stringBytes - offset)
            return {RendererSceneError::Range};
        if (bytes.count) memcpy(m_state->strings + offset, bytes.data, bytes.count);
        return {};
    }

    RendererSceneView RendererScene::View() const noexcept
    {
        RendererSceneView result;
        if (!IsPublished())
            return result;
#define UVSR_SCENE_READ_VIEW(type, name, count) result.name = {m_state->name, m_state->counts.count};
        UVSR_SCENE_TABLES(UVSR_SCENE_READ_VIEW)
#undef UVSR_SCENE_READ_VIEW
        result.preorder = {m_state->preorder, m_state->counts.nodes};
        result.root = m_state->root;
        result.generation = m_state->generation;
        result.contentRevision = m_state->contentRevision;
        result.materialRevision = m_state->materialRevision;
        result.lightRevision = m_state->lightRevision;
        result.transformRevision = m_state->transformRevision;
        result.previousTransformRevision = m_state->previousTransformRevision;
        result.instanceTransformRevision = m_state->instanceTransformRevision;
        result.previousInstanceTransformRevision = m_state->previousInstanceTransformRevision;
        return result;
    }

    size_t RendererScene::StorageBytes() const noexcept
    {
        return m_state ? m_state->storageBytes : 0;
    }

    size_t RendererScene::SealWorkspaceBytes() const noexcept
    {
        return m_state ? m_state->counts.nodes : 0;
    }

    bool RendererScene::IsSealed() const noexcept
    {
        return m_state && m_state->sealed;
    }

    bool RendererScene::IsPublished() const noexcept
    {
        return m_state && m_state->generation != 0;
    }

    RendererSceneResult RendererScene::Publish(uint64_t generation) noexcept
    {
        if (!m_state || !m_state->sealed || m_state->generation != 0)
            return {RendererSceneError::InvalidState};
        if (generation == 0)
            return {RendererSceneError::Generation};
        m_state->generation = generation;
        return {};
    }

    RendererSceneResult RendererScene::Seal(uint32_t root, ArrayView<uint8_t> workspace) noexcept
    {
        if (!m_state || m_state->sealed)
            return {RendererSceneError::InvalidState};
        State& state = *m_state;
        const uint32_t count = state.counts.nodes;
        if (root >= count)
            return {RendererSceneError::Root, root};
        if (!workspace.IsValid() || workspace.count < count)
            return {RendererSceneError::Workspace};
        const auto validNode = [count](uint32_t index) { return index == InvalidSceneIndex || index < count; };
        const auto validString = [&state](RendererSceneString value)
        {
            return value.offset <= state.counts.stringBytes &&
                value.length <= state.counts.stringBytes - value.offset;
        };
        for (uint32_t index = 0; index < count; ++index)
        {
            const auto& node = state.nodes[index];
            if (!validNode(node.parentIndex) || !validNode(node.firstChildIndex) || !validNode(node.nextSiblingIndex))
                return {RendererSceneError::Reference, index};
            if ((node.parentIndex == InvalidSceneIndex) != (index == root) ||
                (index == root && node.nextSiblingIndex != InvalidSceneIndex))
                return {RendererSceneError::Root, index};
            if (!validString(node.name) || !ValidTransform(node.transform) ||
                !IsFinite(node.previousLocal) || !ValidWorld(node.previousWorld))
                return {RendererSceneError::Value, index};
        }

        memset(workspace.data, 0, count);
        for (uint32_t index = 0; index < count; ++index)
        {
            uint32_t current = index;
            while (current != InvalidSceneIndex && workspace.data[current] == 0)
            {
                workspace.data[current] = 1;
                current = state.nodes[current].parentIndex;
            }
            if (current != InvalidSceneIndex && workspace.data[current] == 1)
                return {RendererSceneError::Cycle, current};
            current = index;
            while (current != InvalidSceneIndex && workspace.data[current] == 1)
            {
                workspace.data[current] = 2;
                current = state.nodes[current].parentIndex;
            }
        }

        memset(workspace.data, 0, count);
        for (uint32_t index = 0; index < count; ++index)
        {
            for (uint32_t child = state.nodes[index].firstChildIndex;
                child != InvalidSceneIndex; child = state.nodes[child].nextSiblingIndex)
            {
                if (workspace.data[child] || state.nodes[child].parentIndex != index)
                    return {RendererSceneError::Hierarchy, child};
                workspace.data[child] = 1;
            }
        }
        for (uint32_t index = 0; index < count; ++index)
            if ((workspace.data[index] != 0) != (index != root))
                return {RendererSceneError::Hierarchy, index};

        uint32_t position = 0;
        uint32_t current = root;
        while (current != InvalidSceneIndex)
        {
            if (position >= count)
                return {RendererSceneError::Hierarchy, current};
            state.preorder[position] = current;
            state.nodes[current].preorderIndex = position++;
            if (state.nodes[current].firstChildIndex != InvalidSceneIndex)
            {
                current = state.nodes[current].firstChildIndex;
                continue;
            }
            while (current != InvalidSceneIndex)
            {
                state.nodes[current].subtreeEnd = position;
                if (state.nodes[current].nextSiblingIndex != InvalidSceneIndex)
                {
                    current = state.nodes[current].nextSiblingIndex;
                    break;
                }
                current = state.nodes[current].parentIndex;
            }
        }
        if (position != count)
            return {RendererSceneError::Hierarchy};

        uint32_t usedGeometries = 0;
        for (uint32_t index = 0; index < state.counts.meshes; ++index)
        {
            const auto& mesh = state.meshes[index];
            if (mesh.bufferGroupIndex >= state.counts.bufferGroups ||
                (mesh.skinPrototypeIndex != InvalidSceneIndex && mesh.skinPrototypeIndex >= state.counts.meshes))
                return {RendererSceneError::Reference, index};
            if (!ConsumeRange(mesh.geometries, state.counts.geometries, usedGeometries))
                return {RendererSceneError::Range, index};
            if (!validString(mesh.name) || mesh.type >= RendererSceneMeshType::Count)
                return {RendererSceneError::Value, index};
            if (mesh.skinPrototypeIndex == index)
                return {RendererSceneError::Cycle, index};
            if (mesh.skinPrototypeIndex != InvalidSceneIndex &&
                (!state.meshes[mesh.skinPrototypeIndex].isSkinPrototype ||
                    state.meshes[mesh.skinPrototypeIndex].skinPrototypeIndex != InvalidSceneIndex))
                return {RendererSceneError::Reference, index};
            const auto& buffers = state.bufferGroups[mesh.bufferGroupIndex];
            if ((uint64_t(mesh.indexOffset) + mesh.indexCount) * 4 > buffers.indexBytes)
                return {RendererSceneError::Range, index};
            constexpr uint32_t strides[]{12, 12, 8, 8, 4, 4, 8, 16, 4};
            static_assert(sizeof(strides) / sizeof(strides[0]) == uint32_t(RendererSceneVertexAttribute::Count));
            for (uint32_t attribute = 0; attribute < uint32_t(RendererSceneVertexAttribute::Count); ++attribute)
            {
                const auto range = buffers.attributes[attribute];
                if ((attribute == uint32_t(RendererSceneVertexAttribute::Position) || range.size != 0) &&
                    (uint64_t(mesh.vertexOffset) + mesh.vertexCount) * strides[attribute] > range.size)
                    return {RendererSceneError::Range, index};
            }
            for (uint32_t local = 0; local < mesh.geometries.count; ++local)
            {
                const auto& geometry = state.geometries[mesh.geometries.first + local];
                if (geometry.indexOffsetInMesh > mesh.indexCount || geometry.indexCount > mesh.indexCount - geometry.indexOffsetInMesh ||
                    geometry.vertexOffsetInMesh > mesh.vertexCount || geometry.vertexCount > mesh.vertexCount - geometry.vertexOffsetInMesh ||
                    uint64_t(mesh.indexOffset) + geometry.indexOffsetInMesh + geometry.indexCount > UINT32_MAX ||
                    uint64_t(mesh.vertexOffset) + geometry.vertexOffsetInMesh + geometry.vertexCount > UINT32_MAX)
                    return {RendererSceneError::Range, mesh.geometries.first + local};
            }
        }
        if (usedGeometries != state.counts.geometries)
            return {RendererSceneError::Range};
        for (uint32_t index = 0; index < state.counts.geometries; ++index)
        {
            const auto& geometry = state.geometries[index];
            if (geometry.materialIndex >= state.counts.materials)
                return {RendererSceneError::Reference, index};
            if (geometry.primitive >= RendererScenePrimitive::Count || !ValidBounds(geometry.objectBounds))
                return {RendererSceneError::Value, index};
        }
        for (uint32_t index = 0; index < state.counts.meshes; ++index)
        {
            auto& mesh = state.meshes[index];
            RendererSceneBounds geometryBounds;
            for (uint32_t local = 0; local < mesh.geometries.count; ++local)
                UnionBounds(geometryBounds, state.geometries[mesh.geometries.first + local].objectBounds);
            if (mesh.hasDeclaredBounds)
            {
                const auto& declared = mesh.objectBounds;
                if (!ValidBounds(declared) || (!geometryBounds.empty &&
                    (declared.empty || declared.minimum.x > geometryBounds.minimum.x ||
                        declared.minimum.y > geometryBounds.minimum.y || declared.minimum.z > geometryBounds.minimum.z ||
                        declared.maximum.x < geometryBounds.maximum.x || declared.maximum.y < geometryBounds.maximum.y ||
                        declared.maximum.z < geometryBounds.maximum.z)))
                    return {RendererSceneError::Value, index};
            }
            else
                mesh.objectBounds = geometryBounds;
        }
        uint32_t usedJoints = 0;
        for (uint32_t index = 0; index < state.counts.instances; ++index)
        {
            const auto& instance = state.instances[index];
            if (instance.nodeIndex >= count || instance.meshIndex >= state.counts.meshes ||
                state.nodes[instance.nodeIndex].leafKind != RendererSceneLeafKind::Instance ||
                state.nodes[instance.nodeIndex].leafIndex != index)
                return {RendererSceneError::Reference, index};
            if (!ConsumeRange(instance.joints, state.counts.joints, usedJoints))
                return {RendererSceneError::Range, index};
            if (instance.joints.count != 0 && state.meshes[instance.meshIndex].skinPrototypeIndex == InvalidSceneIndex)
                return {RendererSceneError::Reference, index};
        }
        if (usedJoints != state.counts.joints)
            return {RendererSceneError::Range};
        for (uint32_t index = 0; index < state.counts.joints; ++index)
        {
            if (state.joints[index].nodeIndex >= count)
                return {RendererSceneError::Reference, index};
            for (float value : state.joints[index].inverseBind.values)
                if (!isfinite(value)) return {RendererSceneError::Value, index};
        }

        uint32_t usedMorphRanges = 0;
        for (uint32_t index = 0; index < state.counts.bufferGroups; ++index)
        {
            const auto& buffers = state.bufferGroups[index];
            if (buffers.indexBytes % 4 || buffers.morphBytes % 16 ||
                !ConsumeRange(buffers.morphRanges, state.counts.morphRanges, usedMorphRanges))
                return {RendererSceneError::Range, index};
            for (const auto range : buffers.attributes)
                if (range.offset % 4 || range.size % 4 || !ValidByteRange(range, buffers.vertexBytes))
                    return {RendererSceneError::Range, index};
            for (uint32_t local = 0; local < buffers.morphRanges.count; ++local)
            {
                const auto range = state.morphRanges[buffers.morphRanges.first + local];
                if (range.offset % 16 || range.size % 16 || !ValidByteRange(range, buffers.morphBytes))
                    return {RendererSceneError::Range, index};
            }
        }
        if (usedMorphRanges != state.counts.morphRanges)
            return {RendererSceneError::Range};

        for (uint32_t index = 0; index < state.counts.materials; ++index)
        {
            const auto& material = state.materials[index];
            if (!validString(material.name) || !validString(material.modelFileName) ||
                !ValidMaterial(material.values, state.counts.textures) ||
                !ValidMaterial(material.originalValues, state.counts.textures))
                return {RendererSceneError::Value, index};
        }
        for (uint32_t index = 0; index < state.counts.textures; ++index)
        {
            const auto& texture = state.textures[index];
            if (!validString(texture.path) || !validString(texture.mimeType) ||
                texture.alpha >= RendererSceneTextureAlpha::Count)
                return {RendererSceneError::Value, index};
        }
        for (uint32_t index = 0; index < state.counts.lights; ++index)
        {
            const auto& light = state.lights[index];
            if (light.nodeIndex >= count || state.nodes[light.nodeIndex].leafKind != RendererSceneLeafKind::Light ||
                state.nodes[light.nodeIndex].leafIndex != index)
                return {RendererSceneError::Reference, index};
            if (light.kind >= RendererSceneLightKind::Count || !ValidLight(light.values))
                return {RendererSceneError::Value, index};
        }
        for (uint32_t index = 0; index < state.counts.cameras; ++index)
        {
            const auto& camera = state.cameras[index];
            if (camera.nodeIndex >= count || state.nodes[camera.nodeIndex].leafKind != RendererSceneLeafKind::Camera ||
                state.nodes[camera.nodeIndex].leafIndex != index)
                return {RendererSceneError::Reference, index};
            if (camera.kind >= RendererSceneCameraKind::Count || !isfinite(camera.nearPlane))
                return {RendererSceneError::Value, index};
            if (camera.kind == RendererSceneCameraKind::Perspective ?
                (!isfinite(camera.verticalFov) || (camera.hasFarPlane && !isfinite(camera.farPlane)) ||
                    (camera.hasAspectRatio && !isfinite(camera.aspectRatio))) :
                (!isfinite(camera.farPlane) || !isfinite(camera.xMagnitude) || !isfinite(camera.yMagnitude)))
                return {RendererSceneError::Value, index};
        }
        uint32_t usedChannels = 0;
        for (uint32_t index = 0; index < state.counts.animations; ++index)
        {
            const auto& animation = state.animations[index];
            if (animation.nodeIndex >= count || state.nodes[animation.nodeIndex].leafKind != RendererSceneLeafKind::Animation ||
                state.nodes[animation.nodeIndex].leafIndex != index)
                return {RendererSceneError::Reference, index};
            if (!ConsumeRange(animation.channels, state.counts.channels, usedChannels))
                return {RendererSceneError::Range, index};
        }
        if (usedChannels != state.counts.channels)
            return {RendererSceneError::Range};
        for (uint32_t index = 0; index < state.counts.channels; ++index)
        {
            const auto& channel = state.channels[index];
            const bool nodeTarget = channel.nodeIndex != InvalidSceneIndex;
            const bool materialTarget = channel.materialIndex != InvalidSceneIndex;
            if (nodeTarget == materialTarget ||
                (nodeTarget && channel.nodeIndex >= count) ||
                (materialTarget && (channel.materialIndex >= state.counts.materials ||
                    channel.attribute != RendererSceneAnimationAttribute::LeafProperty)) ||
                channel.samplerIndex >= state.counts.samplers)
                return {RendererSceneError::Reference, index};
            if (channel.attribute == RendererSceneAnimationAttribute::Undefined ||
                channel.attribute >= RendererSceneAnimationAttribute::Count || !validString(channel.property) ||
                (channel.attribute == RendererSceneAnimationAttribute::LeafProperty && channel.property.length == 0))
                return {RendererSceneError::Value, index};
        }
        uint32_t usedKeyframes = 0;
        for (uint32_t index = 0; index < state.counts.samplers; ++index)
        {
            const auto& sampler = state.samplers[index];
            if (!ConsumeRange(sampler.keyframes, state.counts.keyframes, usedKeyframes))
                return {RendererSceneError::Range, index};
            if (sampler.interpolation >= RendererSceneInterpolation::Count)
                return {RendererSceneError::Value, index};
            float previousTime = 0;
            for (uint32_t local = 0; local < sampler.keyframes.count; ++local)
            {
                const uint32_t keyframeIndex = sampler.keyframes.first + local;
                const auto& keyframe = state.keyframes[keyframeIndex];
                if (!isfinite(keyframe.time) || (local != 0 && keyframe.time < previousTime) ||
                    !IsFinite(keyframe.value) || !IsFinite(keyframe.inTangent) || !IsFinite(keyframe.outTangent))
                    return {RendererSceneError::Value, keyframeIndex};
                previousTime = keyframe.time;
            }
        }
        if (usedKeyframes != state.counts.keyframes)
            return {RendererSceneError::Range};
        for (uint32_t index = 0; index < state.counts.animations; ++index)
        {
            auto& animation = state.animations[index];
            float duration = 0;
            for (uint32_t local = 0; local < animation.channels.count; ++local)
            {
                const auto& channel = state.channels[animation.channels.first + local];
                const auto range = state.samplers[channel.samplerIndex].keyframes;
                if (range.count)
                {
                    const float end = state.keyframes[range.first + range.count - 1].time;
                    if (end > duration) duration = end;
                }
            }
            animation.duration = duration;
        }

        for (uint32_t index = 0; index < count; ++index)
        {
            const auto& node = state.nodes[index];
            switch (node.leafKind)
            {
            case RendererSceneLeafKind::None:
                if (node.leafIndex != InvalidSceneIndex) return {RendererSceneError::Reference, index};
                break;
            case RendererSceneLeafKind::Instance:
                if (node.leafIndex >= state.counts.instances || state.instances[node.leafIndex].nodeIndex != index)
                    return {RendererSceneError::Reference, index};
                break;
            case RendererSceneLeafKind::Light:
                if (node.leafIndex >= state.counts.lights || state.lights[node.leafIndex].nodeIndex != index)
                    return {RendererSceneError::Reference, index};
                break;
            case RendererSceneLeafKind::Camera:
                if (node.leafIndex >= state.counts.cameras || state.cameras[node.leafIndex].nodeIndex != index)
                    return {RendererSceneError::Reference, index};
                break;
            case RendererSceneLeafKind::Animation:
                if (node.leafIndex >= state.counts.animations || state.animations[node.leafIndex].nodeIndex != index)
                    return {RendererSceneError::Reference, index};
                break;
            default:
                return {RendererSceneError::Value, index};
            }
        }
        for (uint32_t positionIndex = 0; positionIndex < count; ++positionIndex)
        {
            const uint32_t index = state.preorder[positionIndex];
            auto& node = state.nodes[index];
            if (!node.hasLocalTransform && !Same(node.transform, RendererSceneTransform{}))
                return {RendererSceneError::Value, index};
            node.local = node.hasLocalTransform ? LocalTransform(node.transform) : RendererSceneAffine{};
            node.world = node.parentIndex == InvalidSceneIndex ? node.local :
                node.hasLocalTransform ? Compose(node.local, state.nodes[node.parentIndex].world) :
                    state.nodes[node.parentIndex].world;
            if (!IsFinite(node.local) || !ValidWorld(node.world) || !ValidBounds(state.LeafBounds(index, node.world)))
                return {RendererSceneError::Value, index};
        }
        for (uint32_t end = count; end > 0; --end)
            state.RefreshDerivedNode(state.preorder[end - 1]);
        state.root = root;
        state.sealed = true;
        return {};
    }

    RendererSceneResult RendererScene::SetMaterial(RendererSceneHandle handle, const RendererSceneMaterialValues& values) noexcept
    {
        if (!IsPublished()) return {RendererSceneError::InvalidState};
        State& state = *m_state;
        if (handle.generation != state.generation) return {RendererSceneError::Generation};
        if (handle.index >= state.counts.materials) return {RendererSceneError::Reference, handle.index};
        if (!ValidMaterial(values, state.counts.textures)) return {RendererSceneError::Value, handle.index};
        auto& current = state.materials[handle.index].values;
        if (Same(current, values)) return {};
        if (state.contentRevision == UINT64_MAX || state.materialRevision == UINT64_MAX)
            return {RendererSceneError::Generation};
        const bool contentChanged = current.domain != values.domain;
        current = values;
        ++state.contentRevision;
        ++state.materialRevision;
        if (contentChanged)
            for (uint32_t end = state.counts.nodes; end > 0; --end)
                state.RefreshDerivedNode(state.preorder[end - 1]);
        return {RendererSceneError::None, handle.index, true};
    }

    RendererSceneResult RendererScene::SetMaterials(
        uint64_t generation, ArrayView<const RendererSceneMaterialValues> values) noexcept
    {
        if (!IsPublished()) return {RendererSceneError::InvalidState};
        State& state = *m_state;
        if (generation != state.generation) return {RendererSceneError::Generation};
        if (values.count != state.counts.materials || !values.IsValid())
            return {RendererSceneError::Range};
        bool changed = false;
        bool contentChanged = false;
        for (uint32_t index = 0; index < state.counts.materials; ++index)
        {
            const auto& value = values.data[index];
            if (!ValidMaterial(value, state.counts.textures)) return {RendererSceneError::Value, index};
            const auto& current = state.materials[index].values;
            changed = changed || !Same(current, value);
            contentChanged = contentChanged || current.domain != value.domain;
        }
        if (!changed) return {};
        if (state.contentRevision == UINT64_MAX || state.materialRevision == UINT64_MAX)
            return {RendererSceneError::Generation};
        for (uint32_t index = 0; index < state.counts.materials; ++index)
            state.materials[index].values = values.data[index];
        ++state.contentRevision;
        ++state.materialRevision;
        if (contentChanged)
            for (uint32_t end = state.counts.nodes; end > 0; --end)
                state.RefreshDerivedNode(state.preorder[end - 1]);
        return {RendererSceneError::None, InvalidSceneIndex, true};
    }

    RendererSceneResult RendererScene::SetTextureMetadata(RendererSceneHandle handle,
        RendererSceneTextureAlpha alpha, uint32_t originalBitsPerPixel) noexcept
    {
        if (!IsPublished()) return {RendererSceneError::InvalidState};
        State& state = *m_state;
        if (handle.generation != state.generation) return {RendererSceneError::Generation};
        if (handle.index >= state.counts.textures) return {RendererSceneError::Reference, handle.index};
        if (alpha >= RendererSceneTextureAlpha::Count) return {RendererSceneError::Value, handle.index};
        auto& texture = state.textures[handle.index];
        if (texture.alpha == alpha && texture.originalBitsPerPixel == originalBitsPerPixel) return {};
        if (state.contentRevision == UINT64_MAX) return {RendererSceneError::Generation};
        texture.alpha = alpha;
        texture.originalBitsPerPixel = originalBitsPerPixel;
        ++state.contentRevision;
        return {RendererSceneError::None, handle.index, true};
    }

    RendererSceneResult RendererScene::SetLight(RendererSceneHandle handle, const RendererSceneLightValues& values) noexcept
    {
        if (!IsPublished()) return {RendererSceneError::InvalidState};
        State& state = *m_state;
        if (handle.generation != state.generation) return {RendererSceneError::Generation};
        if (handle.index >= state.counts.lights) return {RendererSceneError::Reference, handle.index};
        if (!ValidLight(values)) return {RendererSceneError::Value, handle.index};
        auto& current = state.lights[handle.index].values;
        if (Same(current, values)) return {};
        if (state.contentRevision == UINT64_MAX || state.lightRevision == UINT64_MAX)
            return {RendererSceneError::Generation};
        current = values;
        ++state.contentRevision;
        ++state.lightRevision;
        return {RendererSceneError::None, handle.index, true};
    }

    RendererSceneResult RendererScene::SetTransform(RendererSceneHandle handle, const RendererSceneTransform& transform) noexcept
    {
        if (!IsPublished()) return {RendererSceneError::InvalidState};
        State& state = *m_state;
        if (handle.generation != state.generation) return {RendererSceneError::Generation};
        if (handle.index >= state.counts.nodes) return {RendererSceneError::Reference, handle.index};
        if (!ValidTransform(transform)) return {RendererSceneError::Value, handle.index};
        auto& target = state.nodes[handle.index];
        if (target.hasLocalTransform && Same(target.transform, transform)) return {};
        if (state.contentRevision == UINT64_MAX || state.transformRevision == UINT64_MAX)
            return {RendererSceneError::Generation};
        const auto proposedLocal = LocalTransform(transform);
        if (!IsFinite(proposedLocal)) return {RendererSceneError::Value, handle.index};
        bool instancesChanged = false;
        for (uint32_t position = target.preorderIndex; position < target.subtreeEnd; ++position)
        {
            const uint32_t index = state.preorder[position];
            const auto& node = state.nodes[index];
            const auto& local = index == handle.index ? proposedLocal : node.local;
            auto& world = state.transformScratch[index];
            if (node.parentIndex == InvalidSceneIndex)
                world = local;
            else
            {
                const auto& parent = index == handle.index ? state.nodes[node.parentIndex].world :
                    state.transformScratch[node.parentIndex];
                world = index == handle.index || node.hasLocalTransform ? Compose(local, parent) : parent;
            }
            if (!ValidWorld(world) || !ValidBounds(state.LeafBounds(index, world)))
                return {RendererSceneError::Value, index};
            if (node.leafKind == RendererSceneLeafKind::Instance && !Same(node.world, world))
                instancesChanged = true;
        }
        if (instancesChanged && state.instanceTransformRevision == UINT64_MAX)
            return {RendererSceneError::Generation};
        target.transform = transform;
        target.hasLocalTransform = true;
        target.local = proposedLocal;
        for (uint32_t position = target.preorderIndex; position < target.subtreeEnd; ++position)
        {
            const uint32_t index = state.preorder[position];
            state.nodes[index].world = state.transformScratch[index];
        }
        state.RefreshDerivedSubtree(handle.index);
        ++state.contentRevision;
        ++state.transformRevision;
        if (instancesChanged) ++state.instanceTransformRevision;
        return {RendererSceneError::None, handle.index, true};
    }

    RendererSceneResult RendererScene::AdvancePreviousTransforms() noexcept
    {
        if (!IsPublished()) return {RendererSceneError::InvalidState};
        bool changed = false;
        bool instancesChanged = false;
        for (uint32_t index = 0; index < m_state->counts.nodes; ++index)
        {
            const auto& node = m_state->nodes[index];
            const bool worldChanged = !Same(node.previousWorld, node.world);
            changed = changed || worldChanged || !Same(node.previousLocal, node.local);
            if (worldChanged && node.leafKind == RendererSceneLeafKind::Instance)
                instancesChanged = true;
        }
        if (!changed) return {};
        if (m_state->previousTransformRevision == UINT64_MAX ||
            (instancesChanged && m_state->previousInstanceTransformRevision == UINT64_MAX))
            return {RendererSceneError::Generation};
        for (uint32_t index = 0; index < m_state->counts.nodes; ++index)
        {
            auto& node = m_state->nodes[index];
            node.previousLocal = node.local;
            node.previousWorld = node.world;
        }
        ++m_state->previousTransformRevision;
        if (instancesChanged) ++m_state->previousInstanceTransformRevision;
        return {RendererSceneError::None, InvalidSceneIndex, true};
    }

#if defined(UVSR_BUILD_TESTING)
    void SetRendererSceneAllocationFailure(uint32_t ordinal) noexcept
    {
        allocationFailure = ordinal;
        allocationOrdinal = 0;
    }
#endif

#undef UVSR_SCENE_TABLES
}
