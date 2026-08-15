#pragma once

#include "settings_snapshot_schema.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace uvsr
{
    inline constexpr std::uint16_t SettingsSnapshotVersion =
        ResolveSettingsSnapshotSchemaVersion(
            CurrentSettingsSnapshotSchemaFingerprint);
    inline constexpr std::size_t SettingsSnapshotCodeLength = 32u;

    static_assert(
        SettingsSnapshotVersion != 0u,
        "The represented Settings catalog has no registered snapshot schema; "
        "build and run uvsr_settings_snapshot_schema_probe to reserve one");

    [[nodiscard]] constexpr std::array<char, 5>
    BuildSettingsSnapshotVersionText() noexcept
    {
        constexpr char HexDigits[] = "0123456789abcdef";
        std::array<char, 5> text{};
        for (std::size_t index = 0u; index < 4u; ++index)
        {
            const unsigned int shift =
                static_cast<unsigned int>((3u - index) * 4u);
            text[index] = HexDigits[
                (SettingsSnapshotVersion >> shift) & 0x0fu];
        }
        return text;
    }

    inline constexpr auto SettingsSnapshotVersionText =
        BuildSettingsSnapshotVersionText();

    [[nodiscard]] constexpr std::uint64_t HashSettingsSnapshotBytes(
        std::string_view canonicalSettings,
        std::uint64_t initialValue,
        std::uint64_t prime,
        std::uint8_t byteMask) noexcept
    {
        std::uint64_t hash = initialValue;
        for (const unsigned char byte : canonicalSettings)
        {
            hash ^= std::uint64_t(byte ^ byteMask);
            hash *= prime;
        }
        return hash;
    }

    [[nodiscard]] inline std::string BuildSettingsSnapshotCode(
        std::string_view canonicalSettings)
    {
        constexpr char HexDigits[] = "0123456789abcdef";
        constexpr std::uint64_t FnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t FnvPrime = 1099511628211ull;
        constexpr std::uint64_t SecondaryOffset =
            7809847782465536322ull;
        constexpr std::uint64_t SecondaryPrime =
            14029467366897019727ull;

        const std::uint64_t primary = HashSettingsSnapshotBytes(
            canonicalSettings,
            FnvOffset,
            FnvPrime,
            0u);
        const std::uint64_t secondary = HashSettingsSnapshotBytes(
            canonicalSettings,
            SecondaryOffset,
            SecondaryPrime,
            0xa5u);

        std::array<std::uint8_t, 14> payload{};
        for (std::size_t index = 0u; index < 8u; ++index)
        {
            payload[index] = static_cast<std::uint8_t>(
                primary >> ((7u - index) * 8u));
        }
        for (std::size_t index = 0u; index < 6u; ++index)
        {
            payload[index + 8u] = static_cast<std::uint8_t>(
                secondary >> ((7u - index) * 8u));
        }

        std::string code(SettingsSnapshotCodeLength, '0');
        for (std::size_t index = 0u; index < 4u; ++index)
            code[index] = SettingsSnapshotVersionText[index];
        for (std::size_t index = 0u; index < payload.size(); ++index)
        {
            code[4u + index * 2u] =
                HexDigits[(payload[index] >> 4u) & 0x0fu];
            code[5u + index * 2u] =
                HexDigits[payload[index] & 0x0fu];
        }
        return code;
    }

    [[nodiscard]] constexpr bool IsSettingsSnapshotCode(
        std::string_view code) noexcept
    {
        if (code.size() != SettingsSnapshotCodeLength ||
            code.substr(0u, 4u) != std::string_view(
                SettingsSnapshotVersionText.data(),
                4u))
        {
            return false;
        }
        for (const char character : code)
        {
            if (!((character >= '0' && character <= '9') ||
                    (character >= 'a' && character <= 'f')))
            {
                return false;
            }
        }
        return true;
    }
}
