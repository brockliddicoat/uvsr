#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr
{
    using DecodedSettings = std::map<std::string, std::string, std::less<>>;

    [[nodiscard]] std::vector<std::filesystem::path>
    GetDefaultSettingsSnapshotCatalogPaths(std::string_view version);

    [[nodiscard]] std::vector<std::string> ReadMatchingSettingsSnapshots(
        const std::filesystem::path& catalogPath,
        std::string_view code);

    [[nodiscard]] std::string UnescapeSettingsSnapshotValue(
        std::string_view value);

    [[nodiscard]] DecodedSettings ParseSettingsSnapshot(
        std::string_view canonicalSettings);

    [[nodiscard]] DecodedSettings DecodeSettingsSnapshot(
        std::string_view code,
        const std::vector<std::filesystem::path>& catalogPaths);

    [[nodiscard]] bool ValidateSettingsSnapshotLoadCode(
        std::string_view code,
        std::string& error);

    [[nodiscard]] std::string FormatCanonicalSettingsSnapshot(
        const DecodedSettings& settings);

    [[nodiscard]] std::string FormatDecodedSettingsJson(
        const DecodedSettings& settings);
}
