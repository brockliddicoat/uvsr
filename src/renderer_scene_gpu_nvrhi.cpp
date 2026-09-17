#include "renderer_scene_gpu_nvrhi.h"
#include "renderer_scene_resources_nvrhi.h"
#include "renderer_scene_descriptors_nvrhi.h"
#include <new>
#include <string.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene GPU table ownership requires exception-disabled compilation
#endif

static_assert(sizeof(RendererMaterialTableEntry) == nvrhi::c_ConstantBufferOffsetSizeAlignment);

namespace uvsr
{

    namespace
    {
#if defined(UVSR_BUILD_TESTING)
        uint32_t allocationFailure = 0;
        uint32_t allocationOrdinal = 0;
#endif
        bool CanAllocate() noexcept
        {
#if defined(UVSR_BUILD_TESTING)
            return ++allocationOrdinal != allocationFailure;
#else
            return true;
#endif
        }

        template<class T>
        RendererSceneResult Allocate(T*& output, size_t count) noexcept
        {
            if (!count) return {};
            if (count > size_t(PTRDIFF_MAX) / sizeof(T)) return {RendererSceneError::Capacity};
            output = CanAllocate() ? new (std::nothrow) T[count]{} : nullptr;
            return output ? RendererSceneResult{} : RendererSceneResult{RendererSceneError::Allocation};
        }

        bool MatchesBuffer(nvrhi::IBuffer* buffer, uint64_t bytes) noexcept
        { return bytes ? buffer && buffer->getDesc().byteSize == bytes : buffer == nullptr; }

        bool CanAddDescriptors(RendererSceneDescriptorsNvrhi* owner, uint64_t count) noexcept
        {
            const auto* table = owner && owner->IsValid() ? owner->GetDescriptorTable() : nullptr;
            const auto* layout = table ? table->getLayout() : nullptr;
            const auto* desc = layout ? layout->getBindlessDesc() : nullptr;
            return desc && owner->GetLiveCount() <= desc->maxCapacity && count <= desc->maxCapacity - owner->GetLiveCount();
        }

        struct SceneBuffer
        {
            nvrhi::BufferHandle indices, vertices;
            RendererSceneBufferDescriptors slots;
            uint32_t indexOwner = InvalidSceneIndex;
            bool ownsIndexSlot = false, ownsVertexSlot = false;
        };
    }

    struct RendererSceneGpuTablesNvrhi::State
    {
        uint64_t generation = 0;
        uint64_t materialRevision = 0;
        uint64_t pendingMaterialRevision = 0;
        uint64_t instanceTransformRevision = 0;
        uint64_t previousInstanceTransformRevision = 0;
        uint64_t pendingInstanceTransformRevision = 0;
        uint64_t pendingPreviousInstanceTransformRevision = 0;
        uint32_t materialCount = 0;
        uint32_t textureCount = 0;
        uint32_t geometryCount = 0;
        uint32_t instanceCount = 0;
        uint32_t bufferCount = 0;
        RendererMaterialTableEntry* materialScratch = nullptr;
        GeometryData* geometryScratch = nullptr;
        InstanceData* instanceScratch = nullptr;
        int32_t* textureDescriptors = nullptr;
        nvrhi::TextureHandle* textures = nullptr;
        SceneBuffer* buffers = nullptr;
        nvrhi::BufferHandle materials;
        nvrhi::BufferHandle geometries;
        nvrhi::BufferHandle instances;
        nvrhi::DescriptorTableHandle descriptorTable;
        // the manager outlives this state and its outer GPU retirement.
        RendererSceneDescriptorsNvrhi* descriptorOwner = nullptr;
        bool ownsDescriptors = false;
        bool geometryPrepared = false;
        bool geometryUploaded = false;
        bool pendingGeometry = false;

        bool DescriptorsValid() const noexcept
        {
            return descriptorOwner && descriptorOwner->IsValid() && descriptorOwner->GetDescriptorTable() == descriptorTable.Get();
        }

        bool MatchesDescriptor(int32_t index, nvrhi::ResourceType kind, nvrhi::IResource* resource) const noexcept
        {
            if (!DescriptorsValid() || index < 0 || uint32_t(index) >= descriptorTable->getCapacity()) return false;
            nvrhi::BindingSetItem item;
                item = descriptorOwner->GetDescriptor(index);
            return item.type == kind && item.resourceHandle == resource;
        }

        ~State()
        {
            ReleaseBufferSlots();
            if (ownsDescriptors && textureDescriptors)
            {
                for (uint32_t index = 0; index < textureCount; ++index)
                {
                    const int32_t slot = textureDescriptors[index];
                    if (slot >= 0) (void)descriptorOwner->ReleaseDescriptor(slot);
                }
            }
            delete[] materialScratch;
            delete[] geometryScratch;
            delete[] instanceScratch;
            delete[] textureDescriptors;
            delete[] textures;
            delete[] buffers;
        }

        void ReleaseBufferSlots() noexcept
        {
            if (!buffers) return;
            for (uint32_t index = 0; index < bufferCount; ++index)
            {
                auto& buffer = buffers[index];
                if (buffer.ownsIndexSlot) (void)descriptorOwner->ReleaseDescriptor(buffer.slots.index);
                if (buffer.ownsVertexSlot) (void)descriptorOwner->ReleaseDescriptor(buffer.slots.vertex);
                if (ownsDescriptors) buffer.slots = {};
                buffer.ownsIndexSlot = buffer.ownsVertexSlot = false;
            }
        }

        RendererSceneResult PrepareStorage(const RendererSceneView& scene,
            nvrhi::IDescriptorTable* descriptors) noexcept
        {
            if (scene.materials.count > UINT32_MAX || scene.textures.count > UINT32_MAX || scene.geometries.count > UINT32_MAX ||
                scene.instances.count > UINT32_MAX || scene.bufferGroups.count > UINT32_MAX || !scene.instances.IsValid() ||
                !scene.materials.IsValid() || !scene.textures.IsValid() || !scene.bufferGroups.IsValid())
                return {RendererSceneError::Capacity};
            materialCount = uint32_t(scene.materials.count);
            textureCount = uint32_t(scene.textures.count);
            geometryCount = uint32_t(scene.geometries.count);
            instanceCount = uint32_t(scene.instances.count);
            bufferCount = uint32_t(scene.bufferGroups.count);
            descriptorTable = descriptors;
            if (descriptors && !DescriptorsValid()) return {RendererSceneError::InvalidState};
            RendererSceneResult result;
            if (!(result = Allocate(materialScratch, materialCount)).Succeeded() ||
                !(result = Allocate(textureDescriptors, textureCount)).Succeeded())
                return result;
            // initialize before any later allocation can fail and run cleanup.
            for (uint32_t index = 0; index < textureCount; ++index) textureDescriptors[index] = -1;
            if (!(result = Allocate(textures, textureCount)).Succeeded() ||
                !(result = Allocate(instanceScratch, instanceCount)).Succeeded() ||
                !(result = Allocate(buffers, bufferCount)).Succeeded())
                return result;
            return {};
        }


        RendererSceneResult CaptureImported(const RendererSceneView& scene, const RendererSceneResourcesNvrhi& resources) noexcept
        {
            if (resources.BufferCount() != bufferCount || resources.TextureCount() != textureCount)
                return {RendererSceneError::Reference};
            for (uint32_t index = 0; index < bufferCount; ++index)
            {
                const auto source = resources.Buffer(index);
                const auto& group = scene.bufferGroups.data[index];
                if (!MatchesBuffer(source.indices, group.indexBytes) || !MatchesBuffer(source.vertices, group.vertexBytes))
                    return {RendererSceneError::Reference, index};
                buffers[index].indices = source.indices;
                buffers[index].vertices = source.vertices;
                buffers[index].indexOwner = source.indexOwner;
            }
            for (uint32_t index = 0; index < bufferCount; ++index)
            {
                const auto& buffer = buffers[index];
                if (buffer.indices && (buffer.indexOwner >= bufferCount ||
                    buffers[buffer.indexOwner].indexOwner != buffer.indexOwner ||
                    buffers[buffer.indexOwner].indices != buffer.indices)) return {RendererSceneError::Reference, index};
            }
            if (descriptorOwner && !CanAddDescriptors(descriptorOwner, textureCount)) return {RendererSceneError::Capacity};
            for (uint32_t index = 0; index < textureCount; ++index)
            {
                textures[index] = resources.Texture(index);
                if (!textures[index]) return {RendererSceneError::Reference, index};
                if (!descriptorOwner) continue;
                // the upload owner creates one distinct texture per canonical
                // image. sharing was resolved before that fixed allocation.
                textureDescriptors[index] = descriptorOwner->CreateDescriptor(nvrhi::BindingSetItem::Texture_SRV(0, textures[index]));
                if (!MatchesDescriptor(textureDescriptors[index], nvrhi::ResourceType::Texture_SRV, textures[index]))
                    return {RendererSceneError::Reference, index};
            }
            return {};
        }

        RendererSceneResult PrepareBufferSlots() noexcept
        {
            if (!ownsDescriptors) return {};
            uint64_t requested = 0;
            for (uint32_t index = 0; index < bufferCount; ++index)
                requested += uint64_t(buffers[index].indices && buffers[index].indexOwner == index) + uint64_t(bool(buffers[index].vertices));
            if (!CanAddDescriptors(descriptorOwner, requested)) return {RendererSceneError::Capacity};
            for (uint32_t index = 0; index < bufferCount; ++index)
            {
                auto& buffer = buffers[index];
                const auto create = [&](nvrhi::IBuffer* resource, int32_t& slot, bool& owns) noexcept
                {
                    if (!resource) return true;
                    slot = descriptorOwner->CreateDescriptor(nvrhi::BindingSetItem::RawBuffer_SRV(0, resource));
                    owns = slot >= 0;
                    return MatchesDescriptor(slot, nvrhi::ResourceType::RawBuffer_SRV, resource);
                };
                if ((buffer.indexOwner == index && !create(buffer.indices, buffer.slots.index, buffer.ownsIndexSlot)) ||
                    !create(buffer.vertices, buffer.slots.vertex, buffer.ownsVertexSlot))
                { ReleaseBufferSlots(); return {RendererSceneError::Reference, index}; }
            }
            for (uint32_t index = 0; index < bufferCount; ++index)
            {
                auto& buffer = buffers[index];
                if (buffer.indices && buffer.indexOwner != index) buffer.slots.index = buffers[buffer.indexOwner].slots.index;
            }
            return {};
        }

        RendererSceneResult CreateTables(nvrhi::IDevice* device, const RendererSceneView& scene) noexcept
        {
            if (materialCount)
            {
                nvrhi::BufferDesc desc;
                desc.byteSize = uint64_t(materialCount) * sizeof(RendererMaterialTableEntry);
                desc.structStride = sizeof(RendererMaterialTableEntry);
                desc.isConstantBuffer = true;
                desc.initialState = nvrhi::ResourceStates::ConstantBuffer | nvrhi::ResourceStates::ShaderResource;
                desc.keepInitialState = true;
                desc.debugName = "scene materials";
                materials = device->createBuffer(desc);
                if (!materials) return {RendererSceneError::Allocation};
            }
            if (instanceCount)
            {
                nvrhi::BufferDesc desc;
                desc.byteSize = uint64_t(instanceCount) * sizeof(InstanceData);
                desc.structStride = sizeof(InstanceData);
                desc.isVertexBuffer = true;
                desc.initialState = nvrhi::ResourceStates::ShaderResource;
                desc.keepInitialState = true;
                desc.debugName = "scene instances";
                instances = device->createBuffer(desc);
                if (!instances) return {RendererSceneError::Allocation};
            }
            generation = scene.generation;
            return {};
        }
    };

    RendererSceneGpuTablesNvrhi::RendererSceneGpuTablesNvrhi(nvrhi::IDevice* device) noexcept : m_device(device) {}
    RendererSceneGpuTablesNvrhi::~RendererSceneGpuTablesNvrhi() { Reset(); }


    RendererSceneResult RendererSceneGpuTablesNvrhi::Prepare(const RendererSceneView& scene,
        const RendererSceneResourcesNvrhi& resources, RendererSceneDescriptorsNvrhi* descriptors) noexcept
    {
        if (m_state || !m_device || (descriptors && !descriptors->IsValid()) ||
            !m_device->queryFeatureSupport(nvrhi::Feature::ConstantBufferRanges))
            return {RendererSceneError::InvalidState};
        if (!scene.generation || scene.generation != resources.Generation()) return {RendererSceneError::Generation};
        const auto phase = resources.Progress().phase;
        if (phase != RendererUploadPhase::Submitted && phase != RendererUploadPhase::Complete)
            return {RendererSceneError::InvalidState};
        State* candidate = CanAllocate() ? new (std::nothrow) State{} : nullptr;
        if (!candidate) return {RendererSceneError::Allocation};
        candidate->ownsDescriptors = true;
        candidate->descriptorOwner = descriptors;
        RendererSceneResult result;
        if (!(result = candidate->PrepareStorage(scene, descriptors ? descriptors->GetDescriptorTable() : nullptr)).Succeeded() ||
            !(result = candidate->CaptureImported(scene, resources)).Succeeded() ||
            !(result = candidate->CreateTables(m_device, scene)).Succeeded())
        { delete candidate; return result; }
        m_state = candidate;
        return {};
    }

    RendererSceneResult RendererSceneGpuTablesNvrhi::PrepareRayGeometry(const RendererSceneView& scene) noexcept
    {
        if (!m_state || !scene.generation || scene.generation != m_state->generation)
            return {RendererSceneError::Generation};
        if (m_state->geometryPrepared) return {};
        if (!m_state->descriptorTable || scene.geometries.count != m_state->geometryCount ||
            !m_state->DescriptorsValid() ||
            scene.meshes.count > UINT32_MAX || !scene.geometries.IsValid() ||
            !scene.meshes.IsValid() || !scene.bufferGroups.IsValid() || scene.bufferGroups.count != m_state->bufferCount)
            return {RendererSceneError::Reference};
        GeometryData* candidate = nullptr;
        auto result = Allocate(candidate, m_state->geometryCount);
        if (!result.Succeeded()) return result;
        result = m_state->PrepareBufferSlots();
        if (!result.Succeeded()) { delete[] candidate; return result; }
        const uint32_t capacity = m_state->descriptorTable->getCapacity();
        for (uint32_t meshIndex = 0; result.Succeeded() && meshIndex < scene.meshes.count; ++meshIndex)
        {
            const auto& mesh = scene.meshes.data[meshIndex];
            if (mesh.bufferGroupIndex >= scene.bufferGroups.count)
            { result = {RendererSceneError::Reference, meshIndex}; break; }
            const auto& buffers = m_state->buffers[mesh.bufferGroupIndex];
            const auto& source = scene.bufferGroups.data[mesh.bufferGroupIndex];
            const auto slots = buffers.slots;
            if ((source.indexBytes && (!MatchesBuffer(buffers.indices, source.indexBytes) ||
                    !m_state->MatchesDescriptor(slots.index, nvrhi::ResourceType::RawBuffer_SRV, buffers.indices))) ||
                (source.vertexBytes && (!MatchesBuffer(buffers.vertices, source.vertexBytes) ||
                    !m_state->MatchesDescriptor(slots.vertex, nvrhi::ResourceType::RawBuffer_SRV, buffers.vertices))))
            { result = {RendererSceneError::Reference, meshIndex}; break; }
            for (uint32_t local = 0; result.Succeeded() && local < mesh.geometries.count; ++local)
            {
                const uint32_t geometry = mesh.geometries.first + local;
                if (geometry >= m_state->geometryCount) { result = {RendererSceneError::Range, meshIndex}; break; }
                result = EncodeRendererSceneGeometry(scene, meshIndex, geometry, slots, capacity, candidate[geometry]);
                if (!result.Succeeded()) result.index = geometry;
            }
        }
        if (!result.Succeeded()) { delete[] candidate; m_state->ReleaseBufferSlots(); return result; }
        nvrhi::BufferHandle buffer;
        if (m_state->geometryCount)
        {
            nvrhi::BufferDesc desc;
            desc.byteSize = uint64_t(m_state->geometryCount) * sizeof(GeometryData);
            desc.structStride = sizeof(GeometryData);
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            desc.debugName = "scene geometry";
            buffer = m_device->createBuffer(desc);
            if (!buffer) { delete[] candidate; m_state->ReleaseBufferSlots(); return {RendererSceneError::Allocation}; }
        }
        m_state->geometryScratch = candidate;
        m_state->geometries = buffer;
        m_state->geometryPrepared = true;
        return {};
    }

    void RendererSceneGpuTablesNvrhi::BeginRecording() noexcept
    {
        AbortRecording();
    }

    void RendererSceneGpuTablesNvrhi::AbortRecording() noexcept
    {
        if (!m_state) return;
        m_state->pendingMaterialRevision = 0;
        m_state->pendingInstanceTransformRevision = 0;
        m_state->pendingPreviousInstanceTransformRevision = 0;
        m_state->pendingGeometry = false;
    }

    RendererSceneResult RendererSceneGpuTablesNvrhi::RecordMaterials(nvrhi::ICommandList* commands,
        const RendererSceneView& scene) noexcept
    {
        if (!m_state || !scene.generation || scene.generation != m_state->generation)
            return {RendererSceneError::Generation};
        if (!commands || !scene.materialRevision) return {RendererSceneError::InvalidState};
        if (!scene.materials.IsValid() || scene.materials.count != m_state->materialCount)
            return {RendererSceneError::Reference};
        if (m_state->materialRevision == scene.materialRevision) return {};
        const uint32_t capacity = m_state->descriptorTable ? m_state->descriptorTable->getCapacity() : 0;
        for (uint32_t index = 0; index < m_state->materialCount; ++index)
        {
            int32_t pickingId;
            const uint32_t selectionId = scene.materials.data[index].selectionId;
            memcpy(&pickingId, &selectionId, sizeof(pickingId));
            const auto result = EncodeRendererSceneMaterial(scene.materials.data[index].values, pickingId,
                {m_state->textureDescriptors, m_state->textureCount}, capacity, m_state->materialScratch[index].material);
            if (!result.Succeeded()) return {result.error, index};
        }
        if (m_state->materialCount)
            commands->writeBuffer(m_state->materials, m_state->materialScratch,
                size_t(m_state->materialCount) * sizeof(RendererMaterialTableEntry));
        m_state->pendingMaterialRevision = scene.materialRevision;
        return {};
    }

    RendererSceneResult RendererSceneGpuTablesNvrhi::RecordInstances(nvrhi::ICommandList* commands,
        const RendererSceneView& scene) noexcept
    {
        if (!m_state || !scene.generation || scene.generation != m_state->generation)
            return {RendererSceneError::Generation};
        if (!commands || !scene.instanceTransformRevision || !scene.previousInstanceTransformRevision)
            return {RendererSceneError::InvalidState};
        if (!scene.instances.IsValid() || scene.instances.count != m_state->instanceCount)
            return {RendererSceneError::Reference};
        if (m_state->instanceTransformRevision == scene.instanceTransformRevision &&
            m_state->previousInstanceTransformRevision == scene.previousInstanceTransformRevision) return {};
        uint32_t geometryPrefix = 0;
        for (uint32_t index = 0; index < m_state->instanceCount; ++index)
        {
            auto& encoded = m_state->instanceScratch[index];
            const auto result = EncodeRendererSceneInstance(scene, index, geometryPrefix, encoded);
            if (!result.Succeeded()) return {result.error, index};
            geometryPrefix += encoded.numGeometries;
        }
        if (m_state->instanceCount)
            commands->writeBuffer(m_state->instances, m_state->instanceScratch, size_t(m_state->instanceCount) * sizeof(InstanceData));
        m_state->pendingInstanceTransformRevision = scene.instanceTransformRevision;
        m_state->pendingPreviousInstanceTransformRevision = scene.previousInstanceTransformRevision;
        return {};
    }

    bool RendererSceneGpuTablesNvrhi::RecordGeometry(nvrhi::ICommandList* commands) noexcept
    {
        if (!m_state || !commands || !m_state->geometryPrepared) return false;
        if (m_state->geometryUploaded) return true;
        if (m_state->geometryCount)
            commands->writeBuffer(m_state->geometries, m_state->geometryScratch, size_t(m_state->geometryCount) * sizeof(GeometryData));
        m_state->pendingGeometry = true;
        return true;
    }

    void RendererSceneGpuTablesNvrhi::CommitRecording() noexcept
    {
        if (!m_state) return;
        if (m_state->pendingMaterialRevision) m_state->materialRevision = m_state->pendingMaterialRevision;
        if (m_state->pendingInstanceTransformRevision)
        {
            m_state->instanceTransformRevision = m_state->pendingInstanceTransformRevision;
            m_state->previousInstanceTransformRevision = m_state->pendingPreviousInstanceTransformRevision;
        }
        if (m_state->pendingGeometry)
        {
            m_state->geometryUploaded = true;
            delete[] m_state->geometryScratch;
            m_state->geometryScratch = nullptr;
        }
        BeginRecording();
    }

    uint64_t RendererSceneGpuTablesNvrhi::Generation() const noexcept { return m_state ? m_state->generation : 0; }
    uint64_t RendererSceneGpuTablesNvrhi::MaterialRevision() const noexcept { return m_state ? m_state->materialRevision : 0; }
    bool RendererSceneGpuTablesNvrhi::MaterialsReady(const RendererSceneView& scene) const noexcept
    {
        return m_state && scene.generation == m_state->generation && scene.materialRevision &&
            scene.materials.IsValid() && scene.materials.count == m_state->materialCount &&
            (scene.materialRevision == m_state->materialRevision || scene.materialRevision == m_state->pendingMaterialRevision);
    }
    bool RendererSceneGpuTablesNvrhi::InstancesReady(const RendererSceneView& scene) const noexcept
    {
        return m_state && scene.generation == m_state->generation && scene.instanceTransformRevision &&
            scene.previousInstanceTransformRevision && scene.instances.IsValid() && scene.instances.count == m_state->instanceCount &&
            ((scene.instanceTransformRevision == m_state->instanceTransformRevision &&
                scene.previousInstanceTransformRevision == m_state->previousInstanceTransformRevision) ||
             (scene.instanceTransformRevision == m_state->pendingInstanceTransformRevision &&
                scene.previousInstanceTransformRevision == m_state->pendingPreviousInstanceTransformRevision));
    }
    bool RendererSceneGpuTablesNvrhi::GeometryReady(uint64_t generation) const noexcept
    {
        return m_state && generation == m_state->generation && (m_state->geometryUploaded || m_state->pendingGeometry);
    }
    nvrhi::IBuffer* RendererSceneGpuTablesNvrhi::MaterialBuffer() const noexcept { return m_state ? m_state->materials.Get() : nullptr; }
    nvrhi::IBuffer* RendererSceneGpuTablesNvrhi::GeometryBuffer() const noexcept { return m_state ? m_state->geometries.Get() : nullptr; }
    nvrhi::IBuffer* RendererSceneGpuTablesNvrhi::InstanceBuffer() const noexcept { return m_state ? m_state->instances.Get() : nullptr; }
    nvrhi::IDescriptorTable* RendererSceneGpuTablesNvrhi::DescriptorTable() const noexcept { return m_state ? m_state->descriptorTable.Get() : nullptr; }
    bool RendererSceneGpuTablesNvrhi::GetMaterialBinding(uint32_t index, nvrhi::IBuffer*& buffer, nvrhi::BufferRange& range) const noexcept
    {
        if (!m_state || index >= m_state->materialCount || !m_state->materials) return false;
        buffer = m_state->materials;
        range = {uint64_t(index) * sizeof(RendererMaterialTableEntry), sizeof(RendererMaterialTableEntry)};
        return true;
    }
    bool RendererSceneGpuTablesNvrhi::GetTexture(uint32_t index, nvrhi::ITexture*& texture) const noexcept
    {
        if (!m_state || (index != InvalidSceneIndex && index >= m_state->textureCount)) return false;
        texture = index == InvalidSceneIndex ? nullptr : m_state->textures[index].Get();
        return true;
    }
    bool RendererSceneGpuTablesNvrhi::GetBuffers(uint32_t index, nvrhi::IBuffer*& indices, nvrhi::IBuffer*& vertices) const noexcept
    {
        if (!m_state || index >= m_state->bufferCount) return false;
        indices = m_state->buffers[index].indices;
        vertices = m_state->buffers[index].vertices;
        return true;
    }
    size_t RendererSceneGpuTablesNvrhi::StorageBytes() const noexcept
    {
        return m_state ? sizeof(State) + size_t(m_state->materialCount) * sizeof(RendererMaterialTableEntry) +
            size_t(m_state->instanceCount) * sizeof(InstanceData) +
            size_t(m_state->bufferCount) * sizeof(SceneBuffer) +
            size_t(m_state->textureCount) * (sizeof(int32_t) + sizeof(nvrhi::TextureHandle)) +
            (m_state->geometryScratch ? size_t(m_state->geometryCount) * sizeof(GeometryData) : 0) : 0;
    }
    void RendererSceneGpuTablesNvrhi::Reset() noexcept { delete m_state; m_state = nullptr; }

#if defined(UVSR_BUILD_TESTING)
    void SetRendererSceneGpuAllocationFailure(uint32_t ordinal) noexcept
    { allocationFailure = ordinal; allocationOrdinal = 0; }
#endif
}
