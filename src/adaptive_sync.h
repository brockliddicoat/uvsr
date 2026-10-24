#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    enum class AdaptiveSyncMode
    {
        Off,
        GSync,
        FreeSync,
        Count
    };

    inline constexpr uint32_t NvidiaVendorId = 0x10DEu;
    inline constexpr uint32_t AmdVendorId = 0x1002u;

    inline constexpr std::array<AdaptiveSyncMode, 3>
        AdaptiveSyncModeValues = {
            AdaptiveSyncMode::GSync,
            AdaptiveSyncMode::FreeSync,
            AdaptiveSyncMode::Off
        };

    [[nodiscard]] constexpr std::string_view AdaptiveSyncModeLabel(
        AdaptiveSyncMode mode)
    {
        switch (mode)
        {
        case AdaptiveSyncMode::Off:
            return "Off";
        case AdaptiveSyncMode::GSync:
            return "G-Sync";
        case AdaptiveSyncMode::FreeSync:
            return "FreeSync";
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
        case AdaptiveSyncMode::GSync:
            return "g-sync";
        case AdaptiveSyncMode::FreeSync:
            return "free-sync";
        case AdaptiveSyncMode::Count:
            break;
        }
        return {};
    }

    [[nodiscard]] constexpr bool AdaptiveSyncRequestsPresentTearing(
        AdaptiveSyncMode mode)
    {
        return mode == AdaptiveSyncMode::GSync ||
            mode == AdaptiveSyncMode::FreeSync;
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
        if (mode == AdaptiveSyncMode::GSync)
            return adapterVendorId == NvidiaVendorId;
        if (mode == AdaptiveSyncMode::FreeSync)
            return adapterVendorId == AmdVendorId;
        return false;
    }

    [[nodiscard]] constexpr AdaptiveSyncMode DefaultAdaptiveSyncMode(
        uint32_t adapterVendorId,
        bool presentTearingSupported)
    {
        if (!presentTearingSupported)
            return AdaptiveSyncMode::Off;
        if (adapterVendorId == NvidiaVendorId)
            return AdaptiveSyncMode::GSync;
        if (adapterVendorId == AmdVendorId)
            return AdaptiveSyncMode::FreeSync;
        return AdaptiveSyncMode::Off;
    }
}
