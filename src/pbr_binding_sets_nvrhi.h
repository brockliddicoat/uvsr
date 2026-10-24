#pragma once

#include <nvrhi/nvrhi.h>

namespace uvsr
{
    // the PBR pass owns a small set of resource combinations on the frame thread.
    // misses grow by one checked node; layout/item/liveness equality owns identity.
    // Clear drops CPU references; submitted commands retain the GPU uses.
    class PbrBindingSetsNvrhi final
    {
    public:
        explicit PbrBindingSetsNvrhi(nvrhi::IDevice* device) noexcept : m_device(device) {}
        ~PbrBindingSetsNvrhi() noexcept { Clear(); }
        PbrBindingSetsNvrhi(const PbrBindingSetsNvrhi&) = delete;
        PbrBindingSetsNvrhi& operator=(const PbrBindingSetsNvrhi&) = delete;
        [[nodiscard]] nvrhi::BindingSetHandle GetOrCreateBindingSet(const nvrhi::BindingSetDesc& desc,
            nvrhi::IBindingLayout* layout) noexcept;
        void Clear() noexcept;
        [[nodiscard]] size_t GetPeakSize() const noexcept { return m_peak; }
        [[nodiscard]] size_t GetSize() const noexcept { return m_count; }
        [[nodiscard]] size_t StorageBytes() const noexcept;
    private:
        struct Entry;
        nvrhi::DeviceHandle m_device;
        Entry* m_entries = nullptr;
        size_t m_count = 0, m_peak = 0;
    };

    enum class PbrBindingFailure : uint8_t { None, Allocation, Create };
    void SetPbrBindingFailure(PbrBindingFailure operation) noexcept;
}
