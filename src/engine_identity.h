#pragma once

#include "settings_snapshot_schema.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace uvsr
{
    using SettingsNumberHash = SettingsSnapshotSchemaFingerprint;

    extern const SettingsNumberHash CanonicalSettingsNumberHash;

    struct EngineVersion
    {
        std::uint16_t major = 0u;
        std::uint16_t minor = 0u;
        std::uint16_t patch = 0u;
        std::uint16_t build = 0u;

        [[nodiscard]] constexpr bool operator==(
            const EngineVersion& other) const noexcept
        {
            return major == other.major && minor == other.minor &&
                patch == other.patch && build == other.build;
        }
    };

    [[nodiscard]] constexpr EngineVersion DeriveEngineVersion(
        SettingsNumberHash hash) noexcept
    {
        return {
            static_cast<std::uint16_t>(hash.high >> 48u),
            static_cast<std::uint16_t>(hash.high >> 32u),
            static_cast<std::uint16_t>(hash.high >> 16u),
            static_cast<std::uint16_t>(hash.high)
        };
    }

    extern const EngineVersion CurrentEngineVersion;

    [[nodiscard]] inline std::string FormatEngineVersion(EngineVersion version)
    {
        return std::to_string(version.major) + "." + std::to_string(version.minor) +
            "." + std::to_string(version.patch) + "." + std::to_string(version.build);
    }

    [[nodiscard]] constexpr char SettingsIdentityHexDigit(
        std::uint8_t value) noexcept
    {
        return value < 10u
            ? static_cast<char>('0' + value)
            : static_cast<char>('a' + value - 10u);
    }

    [[nodiscard]] constexpr std::array<char, 33>
    BuildSettingsNumberHashText(SettingsNumberHash hash) noexcept
    {
        std::array<char, 33> text{};
        for (std::size_t index = 0u; index < 16u; ++index)
        {
            const std::uint64_t half = index < 8u ? hash.high : hash.low;
            const std::size_t halfIndex = index < 8u ? index : index - 8u;
            const std::uint8_t byte = static_cast<std::uint8_t>(
                half >> ((7u - halfIndex) * 8u));
            text[index * 2u] = SettingsIdentityHexDigit(byte >> 4u);
            text[index * 2u + 1u] = SettingsIdentityHexDigit(byte & 0x0fu);
        }
        return text;
    }

    extern const std::array<char, 33> CanonicalSettingsNumberHashText;

    [[nodiscard]] constexpr std::string_view GetSettingsNumberHashText()
        noexcept
    {
        return {
            CanonicalSettingsNumberHashText.data(),
            CanonicalSettingsNumberHashText.size() - 1u
        };
    }
}
