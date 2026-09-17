#pragma once

#include "array_view.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    struct SettingsSnapshotError;
    using ColorLutValue = std::array<float, 4>;

    // successful parsing publishes one complete table. failed parsing preserves
    // this owner and its views; moves, clearing and destruction invalidate views.
    class ColorLutData
    {
    public:
        ColorLutData() noexcept = default;
        ~ColorLutData() noexcept;
        ColorLutData(const ColorLutData&) = delete;
        ColorLutData& operator=(const ColorLutData&) = delete;
        ColorLutData(ColorLutData&& other) noexcept;
        ColorLutData& operator=(ColorLutData&& other) noexcept;
        [[nodiscard]] uint32_t Size() const noexcept { return m_Size; }
        [[nodiscard]] const std::array<float, 3>& DomainMin() const noexcept { return m_DomainMin; }
        [[nodiscard]] const std::array<float, 3>& DomainMax() const noexcept { return m_DomainMax; }
        [[nodiscard]] ArrayView<const ColorLutValue> Values() const noexcept { return {m_Values, m_Count}; }
        void Clear() noexcept;

    private:
        uint32_t m_Size = 0;
        std::array<float, 3> m_DomainMin{0.f, 0.f, 0.f};
        std::array<float, 3> m_DomainMax{1.f, 1.f, 1.f};
        ColorLutValue* m_Values = nullptr;
        size_t m_Count = 0;
        friend bool ReadColorLut(std::string_view, ColorLutData&, SettingsSnapshotError&) noexcept;
    };

    [[nodiscard]] bool ReadColorLut(std::string_view text, ColorLutData& result,
        SettingsSnapshotError& error) noexcept;

#if defined(UVSR_COLOR_LUT_TEST_HOOKS)
    void FailColorLutAllocationAfter(size_t successfulAllocations) noexcept;
    void ClearColorLutAllocationFailure() noexcept;
#endif
}
