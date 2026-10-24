#pragma once

#include "settings_value.h"

namespace uvsr
{
    struct SettingsSnapshotAdapterOption
    {
        int64_t index = -1;
        std::string_view name;
    };
    struct SettingsSnapshotSceneOption
    {
        std::string_view fileName;
        std::string_view displayName;
    };
    struct SettingsSnapshotLightOption
    {
        size_t index = 0;
        std::string_view identity;
    };
    struct SettingsSnapshotMaterialOption
    {
        uint32_t id = 0;
        std::string_view name;
        // renderer tables can contain records without selection ownership.
        bool selectable = true;
    };

    // reads and borrowed views remain stable throughout one synchronous resolve.
    // read may be null only for an empty source. context belongs to the caller.
    template<class Option> struct SettingsSnapshotOptionSource
    {
        const void* context = nullptr;
        size_t count = 0;
        bool (*read)(const void*, size_t, Option&, SettingsSnapshotError&) noexcept = nullptr;
    };

    [[nodiscard]] bool EqualNormalizedCommandAscii(std::string_view left,
        std::string_view right, bool collapseSeparators = false) noexcept;
    [[nodiscard]] bool ContainsNormalizedCommandAscii(std::string_view text,
        std::string_view part, bool collapseSeparators = false) noexcept;
    [[nodiscard]] bool TryParseCommandInteger(std::string_view value, int64_t& parsed) noexcept;

    // allocation failure preserves output. invalid source text produces an empty selector.
    [[nodiscard]] bool FormatSettingsSnapshotAdapterToken(int64_t index,
        UiSettingsValue& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool FormatSettingsSnapshotSceneToken(std::string_view fileName,
        UiSettingsValue& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool FormatSettingsSnapshotLightToken(size_t index, std::string_view identity,
        UiSettingsValue& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool FormatSettingsSnapshotMaterialToken(bool none, uint32_t id,
        UiSettingsValue& output, SettingsSnapshotError& error) noexcept;

    // storage and callback failures preserve output values. malformed selected rows
    // retain the old empty-token behavior; scene ordinals refer to this exact source.
    [[nodiscard]] bool ResolveSettingsSnapshotAdapterToken(std::string_view requested,
        const SettingsSnapshotOptionSource<SettingsSnapshotAdapterOption>& options,
        int64_t& index, UiSettingsValue& canonicalToken, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool ResolveSettingsSnapshotSceneToken(std::string_view requested,
        const SettingsSnapshotOptionSource<SettingsSnapshotSceneOption>& options,
        size_t& ordinal, UiSettingsValue& canonicalToken, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool ResolveSettingsSnapshotLightToken(std::string_view requested,
        const SettingsSnapshotOptionSource<SettingsSnapshotLightOption>& options,
        size_t& index, UiSettingsValue& canonicalToken, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool ResolveSettingsSnapshotMaterialToken(std::string_view requested,
        const SettingsSnapshotOptionSource<SettingsSnapshotMaterialOption>& options,
        bool& none, uint32_t& id, UiSettingsValue& canonicalToken, SettingsSnapshotError& error) noexcept;
}
