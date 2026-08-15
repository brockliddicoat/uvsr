#pragma once

#include "ui_settings_command_catalog.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace uvsr
{
    struct SettingsSnapshotSchemaFingerprint
    {
        std::uint64_t high = 0u;
        std::uint64_t low = 0u;

        [[nodiscard]] constexpr bool operator==(
            const SettingsSnapshotSchemaFingerprint& other) const noexcept
        {
            return high == other.high && low == other.low;
        }
    };

    struct SettingsSnapshotSchemaVersionEntry
    {
        std::uint16_t version = 0u;
        SettingsSnapshotSchemaFingerprint fingerprint;
    };

    // This tag changes whenever canonical membership, ordering, escaping,
    // formatting, hashing, or archive syntax changes. Descriptor metadata is
    // mixed separately below, so either kind of schema change produces a new
    // full fingerprint.
    inline constexpr std::string_view SettingsSnapshotSerializationPolicy =
        "uvsr-settings-snapshot-policy-v3\n"
        "membership=non-action-get-except-ui.settings-collapsed-and-material-editor.visible\n"
        "ordering=command-name-ascending\n"
        "line=name=escaped-value-newline\n"
        "float=maximum-round-trip-precision\n"
        "unavailable=<unavailable>\n"
        "payload=fnv64-plus-masked-fnv48\n"
        "archive=bracketed-code-block\n";

    [[nodiscard]] constexpr bool IsSettingsSnapshotValue(
        const UiSettingsCommandDefinition& definition) noexcept
    {
        return definition.kind != UiSettingsCommandKind::Action &&
            definition.Supports(UiSettingsCommandVerb::Get) &&
            definition.name != "ui.settings-collapsed" &&
            definition.name != "material-editor.visible";
    }

    class SettingsSnapshotSchemaFingerprintBuilder
    {
    public:
        constexpr void MixByte(std::uint8_t byte) noexcept
        {
            m_High ^= std::uint64_t(byte);
            m_High *= 1099511628211ull;
            m_Low ^= std::uint64_t(byte ^ 0xa5u);
            m_Low *= 14029467366897019727ull;
        }

        constexpr void MixSize(std::size_t value) noexcept
        {
            for (std::size_t index = 0u; index < sizeof(std::uint64_t); ++index)
            {
                MixByte(static_cast<std::uint8_t>(
                    std::uint64_t(value) >> (index * 8u)));
            }
        }

        constexpr void MixString(std::string_view value) noexcept
        {
            MixSize(value.size());
            for (const unsigned char byte : value)
                MixByte(byte);
        }

        [[nodiscard]] constexpr SettingsSnapshotSchemaFingerprint Finish()
            const noexcept
        {
            return { m_High, m_Low };
        }

    private:
        std::uint64_t m_High = 14695981039346656037ull;
        std::uint64_t m_Low = 7809847782465536322ull;
    };

    template<std::size_t Size>
    [[nodiscard]] constexpr SettingsSnapshotSchemaFingerprint
    BuildSettingsSnapshotSchemaFingerprint(
        const std::array<UiSettingsCommandDefinition, Size>& catalog,
        std::string_view policy = SettingsSnapshotSerializationPolicy) noexcept
    {
        SettingsSnapshotSchemaFingerprintBuilder builder;
        builder.MixString(policy);
        std::size_t representedCount = 0u;
        for (const UiSettingsCommandDefinition& definition : catalog)
        {
            if (!IsSettingsSnapshotValue(definition))
                continue;

            ++representedCount;
            builder.MixByte(0x1eu);
            builder.MixString(definition.name);
            builder.MixByte(static_cast<std::uint8_t>(definition.kind));
            builder.MixByte(static_cast<std::uint8_t>(definition.section));
            builder.MixByte(definition.supportedVerbs);
            builder.MixByte(definition.dynamic ? 1u : 0u);
            builder.MixString(definition.domain);
        }
        builder.MixByte(0x1fu);
        builder.MixSize(representedCount);
        return builder.Finish();
    }

    inline constexpr SettingsSnapshotSchemaFingerprint
        CurrentSettingsSnapshotSchemaFingerprint =
            BuildSettingsSnapshotSchemaFingerprint(
                UiSettingsCommandCatalog);

#define UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION(version, high, low) \
    SettingsSnapshotSchemaVersionEntry{ version, { high, low } },
    inline constexpr auto SettingsSnapshotSchemaVersions = std::array{
#include "settings_snapshot_schema_versions.def"
    };
#undef UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION

    template<std::size_t Size>
    [[nodiscard]] constexpr bool ValidateSettingsSnapshotSchemaRegistry(
        const std::array<SettingsSnapshotSchemaVersionEntry, Size>& entries)
        noexcept
    {
        for (std::size_t left = 0u; left < entries.size(); ++left)
        {
            if (entries[left].version < 2u ||
                (entries[left].fingerprint.high == 0u &&
                    entries[left].fingerprint.low == 0u))
            {
                return false;
            }
            for (std::size_t right = left + 1u;
                right < entries.size();
                ++right)
            {
                if (entries[left].version == entries[right].version ||
                    entries[left].fingerprint == entries[right].fingerprint)
                {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] constexpr std::uint16_t ResolveSettingsSnapshotSchemaVersion(
        SettingsSnapshotSchemaFingerprint fingerprint) noexcept
    {
        for (const SettingsSnapshotSchemaVersionEntry& entry :
            SettingsSnapshotSchemaVersions)
        {
            if (entry.fingerprint == fingerprint)
                return entry.version;
        }
        return 0u;
    }

    template<std::size_t Size>
    [[nodiscard]] constexpr std::uint16_t
    GetNextAvailableSettingsSnapshotSchemaVersion(
        const std::array<SettingsSnapshotSchemaVersionEntry, Size>& entries)
        noexcept
    {
        std::uint32_t maximumVersion = 1u;
        for (const SettingsSnapshotSchemaVersionEntry& entry : entries)
        {
            maximumVersion = std::max(
                maximumVersion,
                std::uint32_t(entry.version));
        }
        return maximumVersion < std::numeric_limits<std::uint16_t>::max()
            ? static_cast<std::uint16_t>(maximumVersion + 1u)
            : 0u;
    }

    [[nodiscard]] constexpr std::uint16_t
    GetNextAvailableSettingsSnapshotSchemaVersion() noexcept
    {
        return GetNextAvailableSettingsSnapshotSchemaVersion(
            SettingsSnapshotSchemaVersions);
    }

    static_assert(
        ValidateSettingsSnapshotSchemaRegistry(
            SettingsSnapshotSchemaVersions),
        "Settings snapshot schema versions and fingerprints must be unique");
}
