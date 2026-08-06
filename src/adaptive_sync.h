#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    enum class AdaptiveSyncMode
    {
        Off,
        VendorAgnostic,
        NvidiaExclusive,
        Count
    };

    inline constexpr uint32_t NvidiaVendorId = 0x10DEu;

    inline constexpr std::array<AdaptiveSyncMode, 3>
        AdaptiveSyncModeValues = {
            AdaptiveSyncMode::Off,
            AdaptiveSyncMode::VendorAgnostic,
            AdaptiveSyncMode::NvidiaExclusive
        };

    [[nodiscard]] constexpr std::string_view AdaptiveSyncModeLabel(
        AdaptiveSyncMode mode)
    {
        switch (mode)
        {
        case AdaptiveSyncMode::Off:
            return "Off";
        case AdaptiveSyncMode::VendorAgnostic:
            return "Vendor Agnostic";
        case AdaptiveSyncMode::NvidiaExclusive:
            return "Nvidia Exclusive";
        case AdaptiveSyncMode::Count:
            break;
        }
        return {};
    }

    [[nodiscard]] constexpr std::string_view AdaptiveSyncModeToken(
        AdaptiveSyncMode mode)
    {
        switch (mode)
        {
        case AdaptiveSyncMode::Off:
            return "off";
        case AdaptiveSyncMode::VendorAgnostic:
            return "vendor-agnostic";
        case AdaptiveSyncMode::NvidiaExclusive:
            return "nvidia-exclusive";
        case AdaptiveSyncMode::Count:
            break;
        }
        return {};
    }

    [[nodiscard]] constexpr bool AdaptiveSyncRequestsPresentTearing(
        AdaptiveSyncMode mode)
    {
        return mode == AdaptiveSyncMode::VendorAgnostic ||
            mode == AdaptiveSyncMode::NvidiaExclusive;
    }

    [[nodiscard]] constexpr bool IsAdaptiveSyncModeAvailable(
        AdaptiveSyncMode mode,
        uint32_t adapterVendorId,
        bool presentTearingSupported)
    {
        if (mode == AdaptiveSyncMode::Off)
            return true;
        if (!presentTearingSupported)
            return false;
        if (mode == AdaptiveSyncMode::VendorAgnostic)
            return true;
        if (mode == AdaptiveSyncMode::NvidiaExclusive)
            return adapterVendorId == NvidiaVendorId;
        return false;
    }

    [[nodiscard]] constexpr AdaptiveSyncMode DefaultAdaptiveSyncMode(
        uint32_t adapterVendorId,
        bool presentTearingSupported)
    {
        if (!presentTearingSupported)
            return AdaptiveSyncMode::Off;
        if (adapterVendorId == NvidiaVendorId)
            return AdaptiveSyncMode::NvidiaExclusive;
        return AdaptiveSyncMode::VendorAgnostic;
    }
}
