#pragma once

#include "renderer_scene_encoding.h"
#include <nvrhi/nvrhi.h>


namespace uvsr
{
    class RendererSceneResourcesNvrhi;
    class RendererSceneDescriptorsNvrhi;

    // private backend owner. CPU records remain authoritative; these buffers,
    // upload scratch and resource borrows belong to one published generation.
    // imported resources own their slots here and retain GPU handles. reset
    // only after outer GPU retirement, before the descriptor manager dies.
    // one table owns the manager's slots; another candidate uses its own manager.
    class RendererSceneGpuTablesNvrhi final
    {
    public:
        explicit RendererSceneGpuTablesNvrhi(nvrhi::IDevice* device) noexcept;
        ~RendererSceneGpuTablesNvrhi();
        RendererSceneGpuTablesNvrhi(const RendererSceneGpuTablesNvrhi&) = delete;
        RendererSceneGpuTablesNvrhi& operator=(const RendererSceneGpuTablesNvrhi&) = delete;
        // requires empty tables and a Submitted/Complete upload generation.
        // texture slots are created now; raw-buffer slots wait for ray use.
        [[nodiscard]] RendererSceneResult Prepare(const RendererSceneView& scene, const RendererSceneResourcesNvrhi& resources,
            RendererSceneDescriptorsNvrhi* descriptors) noexcept;
        [[nodiscard]] RendererSceneResult PrepareRayGeometry(const RendererSceneView& scene) noexcept;
        void BeginRecording() noexcept;
        // abandon only unsubmitted work. retain scratch for a later recording.
        void AbortRecording() noexcept;
        [[nodiscard]] RendererSceneResult RecordMaterials(nvrhi::ICommandList* commands,
            const RendererSceneView& scene) noexcept;
        [[nodiscard]] RendererSceneResult RecordInstances(nvrhi::ICommandList* commands,
            const RendererSceneView& scene) noexcept;
        [[nodiscard]] bool RecordGeometry(nvrhi::ICommandList* commands) noexcept;
        // submission success commits upload revisions, not physical completion.
        void CommitRecording() noexcept;
        [[nodiscard]] uint64_t Generation() const noexcept;
        [[nodiscard]] uint64_t MaterialRevision() const noexcept;
        [[nodiscard]] bool MaterialsReady(const RendererSceneView& scene) const noexcept;
        [[nodiscard]] bool InstancesReady(const RendererSceneView& scene) const noexcept;
        [[nodiscard]] bool GeometryReady(uint64_t generation) const noexcept;
        // raw allocation views also support memory accounting during preparation.
        // draw/ray consumers must establish MaterialsReady for their scene view.
        [[nodiscard]] nvrhi::IBuffer* MaterialBuffer() const noexcept;
        [[nodiscard]] nvrhi::IBuffer* GeometryBuffer() const noexcept;
        [[nodiscard]] nvrhi::IBuffer* InstanceBuffer() const noexcept;
        [[nodiscard]] nvrhi::IDescriptorTable* DescriptorTable() const noexcept;
        [[nodiscard]] bool GetMaterialBinding(uint32_t index, nvrhi::IBuffer*& buffer, nvrhi::BufferRange& range) const noexcept;
        [[nodiscard]] bool GetTexture(uint32_t index, nvrhi::ITexture*& texture) const noexcept;
        [[nodiscard]] bool GetBuffers(uint32_t index, nvrhi::IBuffer*& indices, nvrhi::IBuffer*& vertices) const noexcept;
        [[nodiscard]] size_t StorageBytes() const noexcept;
        void Reset() noexcept;

    private:
        struct State;
        nvrhi::DeviceHandle m_device;
        State* m_state = nullptr;
    };

#if defined(UVSR_BUILD_TESTING)
    void SetRendererSceneGpuAllocationFailure(uint32_t ordinal) noexcept;
#endif
}
