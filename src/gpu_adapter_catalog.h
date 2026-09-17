#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    struct SettingsSnapshotError;
    struct GpuAdapterChoice
    {
        int adapterIndex = -1;
        std::string_view name;
        std::uint64_t dedicatedVideoMemory = 0;
        std::uint32_t vendorId = 0;
        std::uint32_t deviceId = 0;
        bool usesSharedSystemMemory = false;
        std::uint32_t highestShaderModel = 0;
        std::uint32_t highestFeatureLevel = 0;
        std::uint32_t rootSignatureVersion = 0;
        std::uint32_t resourceBindingTier = 0;
        std::uint32_t rayTracingTier = 0;
        std::uint32_t adapterLuidLowPart = 0;
        std::int32_t adapterLuidHighPart = 0;
        std::uint64_t driverVersion = 0;
    };

    class GpuAdapterCatalog
    {
    public:
        GpuAdapterCatalog() noexcept = default;
        ~GpuAdapterCatalog() noexcept;
        GpuAdapterCatalog(const GpuAdapterCatalog&) = delete;
        GpuAdapterCatalog& operator=(const GpuAdapterCatalog&) = delete;
        GpuAdapterCatalog(GpuAdapterCatalog&& other) noexcept;
        GpuAdapterCatalog& operator=(GpuAdapterCatalog&& other) noexcept;

        [[nodiscard]] size_t Count() const noexcept { return m_Count; }
        [[nodiscard]] const GpuAdapterChoice* Entries() const noexcept { return m_Entries; }
        [[nodiscard]] const GpuAdapterChoice& operator[](size_t index) const noexcept { return m_Entries[index]; }
        [[nodiscard]] const GpuAdapterChoice* begin() const noexcept { return m_Entries; }
        [[nodiscard]] const GpuAdapterChoice* end() const noexcept { return m_Count ? m_Entries + m_Count : m_Entries; }

        // names retain every counted byte and a final NUL, including empty names.
        // append may move records, but preserves existing name addresses. failed
        // append preserves all views. moves transfer their lifetime to the new owner.
        [[nodiscard]] bool Append(const GpuAdapterChoice& choice, SettingsSnapshotError& error) noexcept;
        void Clear() noexcept;

    private:
        GpuAdapterChoice* m_Entries = nullptr;
        size_t m_Count = 0;
        size_t m_Capacity = 0;
    };
}
