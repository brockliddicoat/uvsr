#include "settings_snapshot_decoder.h"

#include "settings_snapshot.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

namespace uvsr
{
    namespace
    {
        [[nodiscard]] std::string ReadUtf8Text(
            const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                throw std::runtime_error(
                    "snapshot catalog not found: " + path.u8string());
            return {
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>()
            };
        }

        [[nodiscard]] std::string EscapeJson(std::string_view value)
        {
            constexpr char HexDigits[] = "0123456789abcdef";
            std::string escaped;
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\b': escaped += "\\b"; break;
                case '\f': escaped += "\\f"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default:
                    if (character < 0x20u)
                    {
                        escaped += "\\u00";
                        escaped.push_back(HexDigits[character >> 4u]);
                        escaped.push_back(HexDigits[character & 0x0fu]);
                    }
                    else
                    {
                        escaped.push_back(static_cast<char>(character));
                    }
                }
            }
            return escaped;
        }

        [[nodiscard]] std::string EscapeSnapshotValue(
            std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value)
            {
                switch (character)
                {
                case '\\': escaped += "\\\\"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped.push_back(character); break;
                }
            }
            return escaped;
        }
    }

    std::vector<std::filesystem::path>
    GetDefaultSettingsSnapshotCatalogPaths(std::string_view version)
    {
        if (version.size() != 4u)
            throw std::invalid_argument("snapshot version must have four digits");

        wchar_t* localAppDataBuffer = nullptr;
        std::size_t localAppDataSize = 0u;
        if (_wdupenv_s(
                &localAppDataBuffer,
                &localAppDataSize,
                L"LOCALAPPDATA") != 0)
        {
            throw std::runtime_error("cannot read LOCALAPPDATA");
        }
        const std::unique_ptr<wchar_t, decltype(&std::free)> localAppData(
            localAppDataBuffer,
            &std::free);
        if (!localAppData || localAppDataSize <= 1u)
        {
            throw std::runtime_error(
                "LOCALAPPDATA is unavailable; pass an explicit catalog path");
        }

        const std::string catalogName =
            "settings-snapshots-v" + std::string(version) + ".txt";
        const std::filesystem::path localRoot(localAppData.get());
        std::vector<std::filesystem::path> catalogs = {
            localRoot / "UVSR" / catalogName
        };

        const std::filesystem::path packagesRoot = localRoot / "Packages";
        std::error_code error;
        if (!std::filesystem::is_directory(packagesRoot, error))
            return catalogs;

        std::vector<std::filesystem::path> packageDirectories;
        for (std::filesystem::directory_iterator iterator(packagesRoot, error), end;
             !error && iterator != end;
             iterator.increment(error))
        {
            if (iterator->is_directory(error))
                packageDirectories.push_back(iterator->path());
        }
        if (error)
            throw std::runtime_error("cannot inspect package-local catalogs");
        std::sort(packageDirectories.begin(), packageDirectories.end());
        for (const std::filesystem::path& packageDirectory : packageDirectories)
        {
            const std::filesystem::path candidate = packageDirectory /
                "LocalCache" / "Local" / "UVSR" / catalogName;
            if (std::filesystem::is_regular_file(candidate, error) && !error)
                catalogs.push_back(candidate);
            error.clear();
        }
        return catalogs;
    }

    std::vector<std::string> ReadMatchingSettingsSnapshots(
        const std::filesystem::path& catalogPath,
        std::string_view code)
    {
        const std::string text = ReadUtf8Text(catalogPath);
        const std::string opening = "[" + std::string(code) + "]";
        const std::string closing = "[/" + std::string(code) + "]";
        std::vector<std::string> snapshots;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line != opening)
                continue;

            std::string canonical;
            bool terminated = false;
            while (std::getline(stream, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line == closing)
                {
                    terminated = true;
                    break;
                }
                canonical += line;
                canonical.push_back('\n');
            }
            if (!terminated)
            {
                throw std::runtime_error(
                    "snapshot catalog contains an unterminated " +
                    std::string(code) + " entry");
            }
            snapshots.push_back(std::move(canonical));
        }
        return snapshots;
    }

    std::string UnescapeSettingsSnapshotValue(std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (std::size_t index = 0u; index < value.size(); ++index)
        {
            if (value[index] != '\\')
            {
                result.push_back(value[index]);
                continue;
            }
            if (++index == value.size())
                throw std::runtime_error("snapshot value has a trailing escape");
            switch (value[index])
            {
            case '\\': result.push_back('\\'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default:
                throw std::runtime_error(
                    "snapshot value contains an unknown escape");
            }
        }
        return result;
    }

    DecodedSettings ParseSettingsSnapshot(std::string_view canonicalSettings)
    {
        DecodedSettings settings;
        std::size_t offset = 0u;
        while (offset < canonicalSettings.size())
        {
            const std::size_t newline = canonicalSettings.find('\n', offset);
            const std::size_t end = newline == std::string_view::npos
                ? canonicalSettings.size()
                : newline;
            const std::string_view line = canonicalSettings.substr(
                offset,
                end - offset);
            if (line.empty())
                throw std::runtime_error("snapshot contains an empty setting line");
            const std::size_t separator = line.find('=');
            if (separator == std::string_view::npos || separator == 0u)
                throw std::runtime_error("snapshot contains an invalid setting line");
            const std::string name(line.substr(0u, separator));
            const auto inserted = settings.emplace(
                name,
                UnescapeSettingsSnapshotValue(line.substr(separator + 1u)));
            if (!inserted.second)
                throw std::runtime_error("snapshot contains a duplicate setting");
            if (newline == std::string_view::npos)
                break;
            offset = newline + 1u;
        }
        return settings;
    }

    DecodedSettings DecodeSettingsSnapshot(
        std::string_view code,
        const std::vector<std::filesystem::path>& catalogPaths)
    {
        if (!IsSettingsSnapshotCode(code))
            throw std::invalid_argument("expected a registered 32-digit snapshot code");

        std::set<std::string> uniqueSnapshots;
        bool foundCatalog = false;
        for (const std::filesystem::path& catalogPath : catalogPaths)
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(catalogPath, error))
                continue;
            foundCatalog = true;
            const auto snapshots = ReadMatchingSettingsSnapshots(catalogPath, code);
            uniqueSnapshots.insert(snapshots.begin(), snapshots.end());
        }
        if (!foundCatalog)
            throw std::runtime_error("no settings snapshot catalog was found");
        if (uniqueSnapshots.empty())
            throw std::runtime_error("settings snapshot is absent from the catalogs");
        if (uniqueSnapshots.size() != 1u)
            throw std::runtime_error("settings snapshot fingerprint collision");

        const std::string& canonical = *uniqueSnapshots.begin();
        if (BuildSettingsSnapshotCode(canonical, code.substr(0u, 4u)) != code)
            throw std::runtime_error("settings snapshot fingerprint check failed");
        return ParseSettingsSnapshot(canonical);
    }

    bool ValidateSettingsSnapshotLoadCode(
        std::string_view code,
        std::string& error)
    {
        error.clear();
        if (!IsSettingsSnapshotCode(code))
        {
            error = "expected a registered 32-character lowercase snapshot code";
            return false;
        }
        const std::string_view currentVersion(
            SettingsSnapshotVersionText.data(),
            4u);
        if (code.substr(0u, 4u) != currentVersion)
        {
            error = "snapshot schema " + std::string(code.substr(0u, 4u)) +
                " does not match this engine's schema " +
                std::string(currentVersion);
            return false;
        }
        return true;
    }

    std::string FormatCanonicalSettingsSnapshot(
        const DecodedSettings& settings)
    {
        std::string canonical;
        for (const auto& setting : settings)
        {
            canonical += setting.first;
            canonical.push_back('=');
            canonical += EscapeSnapshotValue(setting.second);
            canonical.push_back('\n');
        }
        return canonical;
    }

    std::string FormatDecodedSettingsJson(const DecodedSettings& settings)
    {
        std::string output = "{\n";
        for (auto iterator = settings.begin(); iterator != settings.end(); ++iterator)
        {
            output += "  \"" + EscapeJson(iterator->first) + "\": \"" +
                EscapeJson(iterator->second) + "\"";
            if (std::next(iterator) != settings.end())
                output.push_back(',');
            output.push_back('\n');
        }
        output += "}\n";
        return output;
    }
}
