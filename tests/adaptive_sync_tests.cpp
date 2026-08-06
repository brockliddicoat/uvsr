#include "adaptive_sync.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace
{
    bool Require(bool condition, const char* message)
    {
        if (condition)
            return true;
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
}

int main()
{
    using namespace uvsr;

    bool passed = true;
    passed &= Require(
        AdaptiveSyncModeValues ==
            std::array{
                AdaptiveSyncMode::Off,
                AdaptiveSyncMode::VendorAgnostic,
                AdaptiveSyncMode::NvidiaExclusive },
        "Adaptive Sync values must retain their visible order");
    passed &= Require(
        AdaptiveSyncModeLabel(AdaptiveSyncMode::Off) == "Off" &&
            AdaptiveSyncModeLabel(AdaptiveSyncMode::VendorAgnostic) ==
                "Vendor Agnostic" &&
            AdaptiveSyncModeLabel(AdaptiveSyncMode::NvidiaExclusive) ==
                "Nvidia Exclusive",
        "Adaptive Sync labels must remain exact");
    passed &= Require(
        AdaptiveSyncModeToken(AdaptiveSyncMode::Off) == "off" &&
            AdaptiveSyncModeToken(AdaptiveSyncMode::VendorAgnostic) ==
                "vendor-agnostic" &&
            AdaptiveSyncModeToken(AdaptiveSyncMode::NvidiaExclusive) ==
                "nvidia-exclusive",
        "Adaptive Sync command tokens must remain exact");
    passed &= Require(
        !AdaptiveSyncRequestsPresentTearing(AdaptiveSyncMode::Off) &&
            AdaptiveSyncRequestsPresentTearing(
                AdaptiveSyncMode::VendorAgnostic) &&
            AdaptiveSyncRequestsPresentTearing(
                AdaptiveSyncMode::NvidiaExclusive),
        "only enabled Adaptive Sync modes may request tearing-compatible Present");

    constexpr uint32_t AmdVendorId = 0x1002u;
    constexpr uint32_t IntelVendorId = 0x8086u;
    for (const uint32_t vendorId :
        { NvidiaVendorId, AmdVendorId, IntelVendorId })
    {
        passed &= Require(
            IsAdaptiveSyncModeAvailable(
                AdaptiveSyncMode::Off, vendorId, false) &&
                !IsAdaptiveSyncModeAvailable(
                    AdaptiveSyncMode::VendorAgnostic, vendorId, false) &&
                !IsAdaptiveSyncModeAvailable(
                    AdaptiveSyncMode::NvidiaExclusive, vendorId, false),
            "unsupported systems must expose Off only");
        passed &= Require(
            IsAdaptiveSyncModeAvailable(
                AdaptiveSyncMode::VendorAgnostic, vendorId, true),
            "Vendor Agnostic must support every tearing-compatible adapter");
    }
    passed &= Require(
        IsAdaptiveSyncModeAvailable(
            AdaptiveSyncMode::NvidiaExclusive, NvidiaVendorId, true) &&
            !IsAdaptiveSyncModeAvailable(
                AdaptiveSyncMode::NvidiaExclusive, AmdVendorId, true) &&
            !IsAdaptiveSyncModeAvailable(
                AdaptiveSyncMode::NvidiaExclusive, IntelVendorId, true),
        "Nvidia Exclusive must remain NVIDIA-only");

    passed &= Require(
        DefaultAdaptiveSyncMode(NvidiaVendorId, true) ==
                AdaptiveSyncMode::NvidiaExclusive &&
            DefaultAdaptiveSyncMode(AmdVendorId, true) ==
                AdaptiveSyncMode::VendorAgnostic &&
            DefaultAdaptiveSyncMode(IntelVendorId, true) ==
                AdaptiveSyncMode::VendorAgnostic &&
            DefaultAdaptiveSyncMode(NvidiaVendorId, false) ==
                AdaptiveSyncMode::Off,
        "Adaptive Sync defaults must follow capability and vendor policy");

    if (!passed)
        return EXIT_FAILURE;
    std::cout << "Adaptive Sync validation passed\n";
    return EXIT_SUCCESS;
}
