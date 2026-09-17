#include "renderer_scene_ray.h"
#include <new>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene ray selection requires exception-disabled compilation
#endif

namespace uvsr
{
    namespace
    {
        enum class GeometryClass : uint8_t { Omitted, Opaque, AlphaTested };

        struct MeshMapping
        {
            uint32_t selected = InvalidSceneIndex;
            bool visited = false;
        };

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
        bool ValidSpan(ArrayView<const T> values) noexcept
        {
            return values.IsValid() && values.count <= UINT32_MAX &&
                values.count <= size_t(PTRDIFF_MAX) / sizeof(T) &&
                (!values.count || values.data);
        }

        bool EligibleMesh(const RendererSceneView& scene, const RendererSceneMesh& mesh) noexcept
        {
            const auto& buffers = scene.bufferGroups.data[mesh.bufferGroupIndex];
            return mesh.type == RendererSceneMeshType::Triangles && buffers.indexBytes && buffers.vertexBytes &&
                buffers.attributes[uint32_t(RendererSceneVertexAttribute::Position)].size;
        }

        GeometryClass Classify(const RendererSceneView& scene, const RendererSceneGeometry& geometry) noexcept
        {
            if (geometry.primitive != RendererScenePrimitive::Triangles ||
                geometry.indexCount < 3 || geometry.indexCount % 3 != 0 || !geometry.vertexCount)
                return GeometryClass::Omitted;
            switch (scene.materials.data[geometry.materialIndex].values.domain)
            {
            case RendererMaterialDomain::Opaque: return GeometryClass::Opaque;
            case RendererMaterialDomain::AlphaTested: return GeometryClass::AlphaTested;
            default: return GeometryClass::Omitted;
            }
        }

        RendererSceneResult Validate(const RendererSceneView& scene) noexcept
        {
            if (!scene.generation) return {RendererSceneError::Generation};
            if (!ValidSpan(scene.nodes) || !ValidSpan(scene.meshes) || !ValidSpan(scene.instances) ||
                !ValidSpan(scene.geometries) || !ValidSpan(scene.materials) || !ValidSpan(scene.bufferGroups))
                return {RendererSceneError::Capacity};
            size_t consumed = 0;
            for (uint32_t index = 0; index < scene.meshes.count; ++index)
            {
                const auto& mesh = scene.meshes.data[index];
                if (mesh.bufferGroupIndex >= scene.bufferGroups.count)
                    return {RendererSceneError::Reference, index};
                if (mesh.geometries.first != consumed || mesh.geometries.count > scene.geometries.count - consumed)
                    return {RendererSceneError::Range, index};
                consumed += mesh.geometries.count;
            }
            if (consumed != scene.geometries.count) return {RendererSceneError::Range};
            for (uint32_t index = 0; index < scene.geometries.count; ++index)
                if (scene.geometries.data[index].materialIndex >= scene.materials.count)
                    return {RendererSceneError::Reference, index};
            for (uint32_t index = 0; index < scene.instances.count; ++index)
            {
                const auto& instance = scene.instances.data[index];
                if (instance.meshIndex >= scene.meshes.count || instance.nodeIndex >= scene.nodes.count)
                    return {RendererSceneError::Reference, index};
            }
            return {};
        }
    }

    PathTracingSceneDomainStatus ClassifyPathTracingSceneDomain(const RendererSceneView& scene) noexcept
    {
        if (!Validate(scene).Succeeded()) return PathTracingSceneDomainStatus::Unsupported;
        bool blended = false;
        for (size_t i = 0; i < scene.meshes.count; ++i)
            if (!EligibleMesh(scene, scene.meshes.data[i])) return PathTracingSceneDomainStatus::Unsupported;
        for (size_t i = 0; i < scene.geometries.count; ++i)
        {
            const auto& geometry = scene.geometries.data[i];
            if (geometry.primitive != RendererScenePrimitive::Triangles || geometry.indexCount < 3 ||
                geometry.indexCount % 3 != 0 || !geometry.vertexCount)
                return PathTracingSceneDomainStatus::Unsupported;
            const auto& material = scene.materials.data[geometry.materialIndex].values;
            if (material.transmissionFactor > 0 || material.enableSubsurfaceScattering || material.enableHair)
                return PathTracingSceneDomainStatus::Unsupported;
            if (material.domain == RendererMaterialDomain::AlphaBlended) blended = true;
            else if (material.domain != RendererMaterialDomain::Opaque && material.domain != RendererMaterialDomain::AlphaTested)
                return PathTracingSceneDomainStatus::Unsupported;
        }
        return blended ? PathTracingSceneDomainStatus::BlendedGeometryOmitted : PathTracingSceneDomainStatus::Supported;
    }

    struct RendererSceneRaySelection::State
    {
        RendererSceneRayMesh* meshes = nullptr;
        RendererSceneRayGeometry* geometries = nullptr;
        RendererSceneRayInstance* instances = nullptr;
        MeshMapping* mapping = nullptr;
        GeometryClass* classes = nullptr;
        uint32_t meshCount = 0;
        uint32_t geometryCount = 0;
        uint32_t instanceCount = 0;
        uint64_t generation = 0;
        uint64_t materialRevision = 0;
        size_t sourceMeshes = 0;
        size_t sourceGeometries = 0;
        size_t sourceInstances = 0;
        size_t storageBytes = sizeof(State);

        ~State()
        {
            delete[] meshes;
            delete[] geometries;
            delete[] instances;
            delete[] mapping;
            delete[] classes;
        }

        template<class T>
        RendererSceneResult Allocate(T*& output, size_t count) noexcept
        {
            if (!count) return {};
            if (count > (size_t(PTRDIFF_MAX) - storageBytes) / sizeof(T))
                return {RendererSceneError::Capacity};
            // checked nothrow array construction is the only retained standard facility.
            output = CanAllocate() ? new (std::nothrow) T[count]{} : nullptr;
            if (!output) return {RendererSceneError::Allocation};
            storageBytes += count * sizeof(T);
            return {};
        }

        RendererSceneResult Build(const RendererSceneView& scene) noexcept
        {
            sourceMeshes = scene.meshes.count;
            sourceGeometries = scene.geometries.count;
            sourceInstances = scene.instances.count;
            RendererSceneResult result;
            if (!(result = Allocate(meshes, sourceMeshes)).Succeeded() ||
                !(result = Allocate(geometries, sourceGeometries)).Succeeded() ||
                !(result = Allocate(instances, sourceInstances)).Succeeded() ||
                !(result = Allocate(mapping, sourceMeshes)).Succeeded() ||
                !(result = Allocate(classes, sourceGeometries)).Succeeded())
                return result;
            for (uint32_t index = 0; index < scene.instances.count; ++index)
            {
                const uint32_t meshIndex = scene.instances.data[index].meshIndex;
                auto& entry = mapping[meshIndex];
                if (!entry.visited)
                {
                    entry.visited = true;
                    const auto& mesh = scene.meshes.data[meshIndex];
                    if (!EligibleMesh(scene, mesh)) continue;
                    const uint32_t first = geometryCount;
                    for (uint32_t local = 0; local < mesh.geometries.count; ++local)
                    {
                        const uint32_t geometryIndex = mesh.geometries.first + local;
                        const auto classification = Classify(scene, scene.geometries.data[geometryIndex]);
                        classes[geometryIndex] = classification;
                        if (classification == GeometryClass::Omitted) continue;
                        geometries[geometryCount++] = {geometryIndex, classification == GeometryClass::Opaque};
                    }
                    if (first == geometryCount) continue;
                    entry.selected = meshCount;
                    meshes[meshCount++] = {meshIndex, {first, geometryCount - first}};
                }
                if (entry.selected != InvalidSceneIndex)
                    instances[instanceCount++] = {index, entry.selected};
            }
            generation = scene.generation;
            materialRevision = scene.materialRevision;
            return {};
        }
    };

    RendererSceneRaySelection::~RendererSceneRaySelection() { Reset(); }

    RendererSceneResult RendererSceneRaySelection::Prepare(const RendererSceneView& scene) noexcept
    {
        const auto valid = Validate(scene);
        if (!valid.Succeeded()) return valid;
        State* candidate = CanAllocate() ? new (std::nothrow) State{} : nullptr;
        if (!candidate) return {RendererSceneError::Allocation};
        const auto result = candidate->Build(scene);
        if (!result.Succeeded()) { delete candidate; return result; }
        delete m_state;
        m_state = candidate;
        return {RendererSceneError::None, InvalidSceneIndex, true};
    }

    bool RendererSceneRaySelection::Matches(const RendererSceneView& scene) noexcept
    {
        if (!m_state || scene.generation != m_state->generation ||
            scene.meshes.count != m_state->sourceMeshes || scene.geometries.count != m_state->sourceGeometries ||
            scene.instances.count != m_state->sourceInstances)
            return false;
        if (scene.materialRevision == m_state->materialRevision) return true;
        if (!Validate(scene).Succeeded()) return false;
        for (uint32_t index = 0; index < scene.meshes.count; ++index)
        {
            if (!m_state->mapping[index].visited) continue;
            const auto& mesh = scene.meshes.data[index];
            if (!EligibleMesh(scene, mesh)) continue;
            for (uint32_t local = 0; local < mesh.geometries.count; ++local)
            {
                const uint32_t geometry = mesh.geometries.first + local;
                if (Classify(scene, scene.geometries.data[geometry]) != m_state->classes[geometry]) return false;
            }
        }
        m_state->materialRevision = scene.materialRevision;
        return true;
    }

    RendererSceneRayView RendererSceneRaySelection::View() const noexcept
    {
        if (!m_state) return {};
        return {{m_state->meshes, m_state->meshCount}, {m_state->geometries, m_state->geometryCount},
            {m_state->instances, m_state->instanceCount}, m_state->generation};
    }

    size_t RendererSceneRaySelection::StorageBytes() const noexcept { return m_state ? m_state->storageBytes : 0; }
    void RendererSceneRaySelection::Reset() noexcept { delete m_state; m_state = nullptr; }

#if defined(UVSR_BUILD_TESTING)
    void SetRendererSceneRayAllocationFailure(uint32_t ordinal) noexcept
    { allocationFailure = ordinal; allocationOrdinal = 0; }
#endif
}
