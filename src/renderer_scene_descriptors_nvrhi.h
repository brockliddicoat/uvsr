#pragma once

#include <nvrhi/nvrhi.h>

namespace uvsr
{
    enum class RendererDescriptorError : uint8_t { None, Input, Capacity, Allocation, Gpu };

    // one frame thread owns the scene's texture/raw-buffer slots. repeated views
    // borrow the same slot; exactly one caller releases it after GPU retirement.
    // a failed release keeps the slot and resource alive for retry or owner teardown.
    class RendererSceneDescriptorsNvrhi final
    {
    public:
        RendererSceneDescriptorsNvrhi(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout) noexcept;
        ~RendererSceneDescriptorsNvrhi() noexcept;
        RendererSceneDescriptorsNvrhi(const RendererSceneDescriptorsNvrhi&) = delete;
        RendererSceneDescriptorsNvrhi& operator=(const RendererSceneDescriptorsNvrhi&) = delete;
        [[nodiscard]] bool IsValid() const noexcept { return m_table != nullptr; }
        [[nodiscard]] nvrhi::IDescriptorTable* GetDescriptorTable() const noexcept { return m_table; }
        [[nodiscard]] RendererDescriptorError Error() const noexcept { return m_error; }
        [[nodiscard]] uint32_t GetLiveCount() const noexcept { return m_live; }
        [[nodiscard]] uint32_t GetPeakLiveCount() const noexcept { return m_peakLive; }
        [[nodiscard]] uint32_t GetPeakCapacity() const noexcept { return m_peakCapacity; }
        [[nodiscard]] size_t StorageBytes() const noexcept;
        [[nodiscard]] int32_t CreateDescriptor(nvrhi::BindingSetItem item) noexcept;
        [[nodiscard]] nvrhi::BindingSetItem GetDescriptor(int32_t index) const noexcept;
        [[nodiscard]] bool ReleaseDescriptor(int32_t index) noexcept;
    private:
        struct Slot;
        nvrhi::DeviceHandle m_device;
        nvrhi::DescriptorTableHandle m_table;
        Slot* m_slots = nullptr;
        uint32_t m_capacity = 0, m_maxCapacity = 0, m_search = 0;
        uint32_t m_live = 0, m_peakLive = 0, m_peakCapacity = 0;
        RendererDescriptorError m_error = RendererDescriptorError::None;
        [[nodiscard]] bool Grow(uint32_t capacity, bool resize) noexcept;
    };

    enum class RendererDescriptorFailure : uint8_t { None, Allocation, Create, Resize, Write };
    // implemented only in BUILD_TESTING; the production owner has no failure state.
    void SetRendererDescriptorFailure(RendererDescriptorFailure operation, unsigned skip = 0) noexcept;
}
