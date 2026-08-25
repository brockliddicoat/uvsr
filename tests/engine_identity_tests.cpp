#include "engine_identity.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::cerr << "Engine identity validation failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const char* message)
    {
        if (!condition)
            Fail(message);
    }
}

int main()
{
    using namespace uvsr;
    constexpr SettingsNumberHash KnownHash{
        0x0123456789abcdefull,
        0xfedcba9876543210ull
    };
    constexpr EngineVersion KnownVersion = DeriveEngineVersion(KnownHash);
    constexpr auto KnownText = BuildSettingsNumberHashText(KnownHash);
    static_assert(KnownVersion == EngineVersion{
        0x0123u, 0x4567u, 0x89abu, 0xcdefu });
    static_assert(std::string_view(KnownText.data(), 32u) ==
        "0123456789abcdeffedcba9876543210");
    static_assert(GetSettingsNumberHashText().size() == 32u);
    static_assert(CanonicalSettingsNumberHash == SettingsNumberHash{
        0x9c50b0f1515e89d8ull,
        0x56c8ebb627b86984ull });
    static_assert(GetSettingsNumberHashText() ==
        "9c50b0f1515e89d856c8ebb627b86984");
    static_assert(CurrentEngineVersion == EngineVersion{
        0x9c50u, 0xb0f1u, 0x515eu, 0x89d8u });
    static_assert(CurrentEngineVersion ==
        DeriveEngineVersion(CanonicalSettingsNumberHash));

    Require(
        GetSettingsNumberHashText() == std::string(
            CanonicalSettingsNumberHashText.data(), 32u),
        "full settings hash text changed between compile-time and runtime");
    std::cout << "settings-hash=" << GetSettingsNumberHashText() << '\n';
    return EXIT_SUCCESS;
}
