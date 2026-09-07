#include "uvsr_settings_commands.h"

#include "renderer_log.h"
#include "settings_snapshot.h"
#include "settings_snapshot_decoder.h"
#include "windows_executable_path.h"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>

namespace uvsr
{
    namespace
    {
        // Retired accents accepted canonical subnormals, including ERANGE conversions.
        bool TryParseLegacyFloat(std::string_view value, float& parsed)
        {
            if (value.empty())
                return false;
            const std::string owned(value);
            char* end = nullptr;
            const float candidate = std::strtof(owned.c_str(), &end);
            if (!end || end != owned.c_str() + owned.size() ||
                !std::isfinite(candidate))
            {
                return false;
            }
            parsed = candidate;
            return true;
        }

        enum class LegacySettingValidation
        {
            Enumeration,
            BoundedFloat3,
            BoundedFloat4,
            SkinPalette,
            HistoryFrames,
            HistoryStrength,
            BoundedNumber,
            BoundedInteger
        };

        struct LegacySettingMigration
        {
            std::uint16_t firstVersion = 0u;
            std::uint16_t lastVersion = 0u;
            std::string_view name;
            LegacySettingValidation validation =
                LegacySettingValidation::Enumeration;
            std::array<std::string_view, 6> tokens{};
            std::size_t tokenCount = 0u;
            float minimum = 0.f;
            float maximum = 0.f;
        };

        inline constexpr std::array<LegacySettingMigration, 83>
            LegacySettingMigrations = {{
                { 0x0007u, 0x0015u, "ui.skin", LegacySettingValidation::Enumeration, { "amp", "ogg" }, 2u },
                { 0x0016u, 0x0017u, "ui.skin", LegacySettingValidation::Enumeration, { "amp", "ogg", "cap" }, 3u },
                { 0x0007u, 0x0017u, "ui.accent.primary", LegacySettingValidation::SkinPalette },
                { 0x0007u, 0x0017u, "ui.accent.font", LegacySettingValidation::SkinPalette },
                { 0x0007u, 0x0017u, "ui.accent.primary-background", LegacySettingValidation::SkinPalette },
                { 0x0007u, 0x0017u, "ui.accent.secondary", LegacySettingValidation::BoundedFloat4 },
                { 0x0007u, 0x0017u, "ui.accent.tertiary", LegacySettingValidation::BoundedFloat4 },
                { 0x0007u, 0x0017u, "ui.font-family", LegacySettingValidation::Enumeration,
                    { "codex", "noto-sans", "proggy-clean" }, 3u },
                { 0x0007u, 0x0014u, "visibility.enabled", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 0x0007u, 0x0014u, "visibility.quality", LegacySettingValidation::Enumeration,
                    { "low", "medium", "high", "ultra", "custom" }, 5u },
                { 0x0007u, 0x0014u, "visibility.estimator", LegacySettingValidation::Enumeration,
                    { "projected-angle", "solid-angle", "cosine-weighted" }, 3u },
                { 0x0007u, 0x0014u, "visibility.resolution", LegacySettingValidation::Enumeration,
                    { "full", "half", "quarter" }, 3u },
                { 0x0007u, 0x0014u, "visibility.samples", LegacySettingValidation::BoundedInteger,
                    {}, 0u, 1.0f, 64.0f },
                { 0x0007u, 0x0014u, "visibility.radius", LegacySettingValidation::BoundedNumber,
                    {}, 0u, 0.1f, 10.0f },
                { 0x0007u, 0x0014u, "visibility.thickness", LegacySettingValidation::BoundedNumber,
                    {}, 0u, 0.01f, 2.0f },
                { 0x0007u, 0x0014u, "visibility.distribution", LegacySettingValidation::BoundedNumber,
                    {}, 0u, 0.25f, 8.0f },
                { 0x0007u, 0x0014u, "visibility.specify-noise", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 0x0007u, 0x0014u, "visibility.noise-pattern", LegacySettingValidation::Enumeration,
                    { "spatial-white", "spatial-blue", "spatiotemporal-blue" }, 3u },
                { 0x0007u, 0x0014u, "visibility.noise-resolution", LegacySettingValidation::Enumeration,
                    { "64x64", "128x128", "256x256", "512x512" }, 4u },
                { 0x0007u, 0x0014u, "visibility.animate-samples", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 0x0007u, 0x0014u, "visibility.ao.enabled", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 0x0007u, 0x0014u, "visibility.ao.strength", LegacySettingValidation::BoundedNumber,
                    {}, 0u, 0.0f, 8.0f },
                { 0x0007u, 0x0014u, "visibility.ao.precision", LegacySettingValidation::Enumeration,
                    { "16-bit", "32-bit" }, 2u },
                { 0x0007u, 0x0014u, "visibility.gi.enabled", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 0x0007u, 0x0014u, "visibility.gi.intensity", LegacySettingValidation::BoundedNumber,
                    {}, 0u, 0.0f, 16.0f },
                { 0x0007u, 0x0014u, "visibility.gi.precision", LegacySettingValidation::Enumeration,
                    { "16-bit", "32-bit" }, 2u },
                { 0x0007u, 0x0014u, "debug.visibility.view", LegacySettingValidation::Enumeration,
                    { "final", "ambient-visibility", "traced-indirect", "applied-indirect" }, 4u },
                { 7u, 15u, "visibility.ao.output-hit-distance", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 7u, 15u, "visibility.gi.output-hit-distance", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 7u, 15u, "denoising.ao.method", LegacySettingValidation::Enumeration,
                    { "raw", "joint-bilateral", "gaussian-bilateral", "reblur" }, 4u },
                { 7u, 15u, "denoising.ao.radius", LegacySettingValidation::BoundedNumber, {}, 0u, 1.0f, 8.0f },
                { 7u, 15u, "denoising.ao.quality", LegacySettingValidation::Enumeration,
                    { "performance", "balanced", "quality", "ultra" }, 4u },
                { 7u, 15u, "denoising.ao.resolution", LegacySettingValidation::Enumeration,
                    { "quarter", "half", "full" }, 3u },
                { 7u, 15u, "denoising.ao.history", LegacySettingValidation::BoundedInteger, {}, 0u, 1, 32 },
                { 7u, 15u, "denoising.ao.disocclusion", LegacySettingValidation::BoundedNumber, {}, 0u, 0.001f, 0.1f },
                { 7u, 15u, "denoising.ao.anti-lag", LegacySettingValidation::BoundedNumber, {}, 0u, 0.0f, 1.0f },
                { 7u, 15u, "denoising.gi.method", LegacySettingValidation::Enumeration,
                    { "raw", "joint-bilateral", "gaussian-bilateral", "reblur", "relax" }, 5u },
                { 7u, 15u, "denoising.gi.radius", LegacySettingValidation::BoundedNumber, {}, 0u, 1.0f, 8.0f },
                { 7u, 15u, "denoising.gi.quality", LegacySettingValidation::Enumeration,
                    { "performance", "balanced", "quality", "ultra" }, 4u },
                { 7u, 15u, "denoising.gi.resolution", LegacySettingValidation::Enumeration,
                    { "quarter", "half", "full" }, 3u },
                { 7u, 15u, "denoising.gi.history", LegacySettingValidation::BoundedInteger, {}, 0u, 1, 32 },
                { 7u, 15u, "denoising.gi.disocclusion", LegacySettingValidation::BoundedNumber, {}, 0u, 0.001f, 0.1f },
                { 7u, 15u, "denoising.gi.anti-lag", LegacySettingValidation::BoundedNumber, {}, 0u, 0.0f, 1.0f },
                { 7u, 15u, "denoising.shadows.method", LegacySettingValidation::Enumeration,
                    { "raw", "joint-bilateral", "gaussian-bilateral", "sigma" }, 4u },
                { 7u, 15u, "denoising.shadows.radius", LegacySettingValidation::BoundedNumber, {}, 0u, 1.0f, 8.0f },
                { 7u, 15u, "denoising.shadows.quality", LegacySettingValidation::Enumeration,
                    { "performance", "balanced", "quality", "ultra" }, 4u },
                { 7u, 15u, "denoising.shadows.resolution", LegacySettingValidation::Enumeration,
                    { "quarter", "half", "full" }, 3u },
                { 7u, 15u, "denoising.shadows.disocclusion", LegacySettingValidation::BoundedNumber, {}, 0u, 0.001f, 0.1f },
                { 7u, 15u, "denoising.sky.method", LegacySettingValidation::Enumeration,
                    { "raw", "joint-bilateral", "gaussian-bilateral", "reblur", "relax" }, 5u },
                { 7u, 15u, "denoising.sky.radius", LegacySettingValidation::BoundedNumber, {}, 0u, 1.0f, 8.0f },
                { 7u, 15u, "denoising.sky.quality", LegacySettingValidation::Enumeration,
                    { "performance", "balanced", "quality", "ultra" }, 4u },
                { 7u, 15u, "denoising.sky.resolution", LegacySettingValidation::Enumeration,
                    { "quarter", "half", "full" }, 3u },
                { 7u, 15u, "denoising.sky.history", LegacySettingValidation::BoundedInteger, {}, 0u, 1, 32 },
                { 7u, 15u, "denoising.sky.disocclusion", LegacySettingValidation::BoundedNumber, {}, 0u, 0.001f, 0.1f },
                { 7u, 15u, "denoising.sky.anti-lag", LegacySettingValidation::BoundedNumber, {}, 0u, 0.0f, 1.0f },
                { 7u, 15u, "sky.visibility.output-hit-distance", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 7u, 15u, "light.selected.flashlight.output-hit-distance", LegacySettingValidation::Enumeration,
                    { "on", "off", "<unavailable>" }, 3u },
                { 7u, 13u, "gpu.adaptive-sync", LegacySettingValidation::Enumeration,
                    { "off", "vendor-agnostic", "nvidia-exclusive" }, 3u },
                { 7u, 11u, "anti-aliasing.taa.enabled", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 7u, 11u, "anti-aliasing.taa.quality", LegacySettingValidation::Enumeration,
                    { "low", "medium", "high", "ultra" }, 4u },
                { 7u, 11u, "anti-aliasing.taa.jitter-sequence", LegacySettingValidation::Enumeration,
                    { "rotated-grid-4", "uniform-helix-4", "halton-8", "halton-16", "halton-32", "sobol-32" }, 6u },
                { 7u, 11u, "anti-aliasing.taa.previous-depth", LegacySettingValidation::Enumeration,
                    { "nearest-texel", "four-texel-footprint" }, 2u },
                { 7u, 11u, "anti-aliasing.taa.temporal-cost", LegacySettingValidation::Enumeration,
                    { "full-quality", "reduced", "minimum" }, 3u },
                { 7u, 11u, "anti-aliasing.taa.history.frames", LegacySettingValidation::HistoryFrames, {}, 0u },
                { 7u, 11u, "anti-aliasing.taa.history.strength", LegacySettingValidation::HistoryStrength, {}, 0u },
                { 7u, 11u, "anti-aliasing.taa.history.storage", LegacySettingValidation::Enumeration,
                    { "temporal-cost", "robust", "compact" }, 3u },
                { 7u, 11u, "anti-aliasing.taa.history.weight", LegacySettingValidation::Enumeration,
                    { "temporal-cost", "confidence-recurrence", "immediate-horizon" }, 3u },
                { 7u, 11u, "anti-aliasing.taa.motion-trust", LegacySettingValidation::Enumeration,
                    { "temporal-cost", "linear-speed", "squared-speed" }, 3u },
                { 7u, 11u, "anti-aliasing.taa.rectification-clip", LegacySettingValidation::Enumeration,
                    { "temporal-cost", "velocity-dilated", "tight-component" }, 3u },
                { 7u, 11u, "anti-aliasing.taa.blend-domain", LegacySettingValidation::Enumeration,
                    { "temporal-cost", "luminance-compressed", "linear-rgb" }, 3u },
                { 7u, 11u, "anti-aliasing.taa.preset-sharpening", LegacySettingValidation::Enumeration,
                    { "auto", "off", "on" }, 3u },
                { 7u, 11u, "anti-aliasing.sharpen.enabled", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 7u, 11u, "anti-aliasing.sharpen.strength", LegacySettingValidation::HistoryStrength, {}, 0u },
                { 7u, 11u, "anti-aliasing.msaa.enabled", LegacySettingValidation::Enumeration,
                    { "on", "off" }, 2u },
                { 7u, 11u, "anti-aliasing.msaa.samples", LegacySettingValidation::Enumeration,
                    { "2x", "4x", "8x", "16x" }, 4u },
                { 7u, 10u, "ui.animations",
                    LegacySettingValidation::Enumeration,
                    { "on", "off", {}, {} }, 2u },
                { 7u, 7u, "anti-aliasing.msaa.quality",
                    LegacySettingValidation::Enumeration,
                    { "low", "medium", "high", "ultra" }, 4u },
                { 7u, 8u, "representation.bvh.build-preference",
                    LegacySettingValidation::Enumeration,
                    { "fast-trace", "balanced", "fast-build", {} }, 3u },
                { 7u, 8u, "representation.blas.update-mode",
                    LegacySettingValidation::Enumeration,
                    { "rebuild", "refit", {}, {} }, 2u },
                { 7u, 8u, "representation.tlas.update-mode",
                    LegacySettingValidation::Enumeration,
                    { "rebuild", "refit", {}, {} }, 2u },
                { 7u, 9u, "ui.accent.main",
                    LegacySettingValidation::BoundedFloat3, {}, 0u },
                { 7u, 9u, "ui.accent.negative",
                    LegacySettingValidation::BoundedFloat3, {}, 0u },
                { 7u, 9u, "ui.accent.positive",
                    LegacySettingValidation::BoundedFloat3, {}, 0u }
            }};

        [[nodiscard]] bool ValidateLegacyColor(
            std::string_view value, std::size_t components)
        {
            std::size_t count = 0u;
            std::size_t begin = 0u;
            for (;;)
            {
                const std::size_t separator = value.find(' ', begin);
                const std::string_view token = value.substr(
                    begin,
                    separator == std::string_view::npos
                        ? std::string_view::npos
                        : separator - begin);
                float parsed = 0.f;
                if (token.empty() || !TryParseLegacyFloat(token, parsed) ||
                    parsed < 0.f || parsed > 1.f ||
                    FormatUiSettingsMetadataFloat(parsed) != token)
                {
                    return false;
                }
                ++count;
                if (separator == std::string_view::npos)
                    break;
                begin = separator + 1u;
            }
            return count == components;
        }

        [[nodiscard]] bool ApplyLegacySettingMigrations(
            std::uint16_t version,
            DecodedSettings& decoded,
            std::string& error)
        {
            const auto skin = decoded.find("ui.skin");
            const bool stockSkin = skin != decoded.end() && skin->second != "amp";
            for (const LegacySettingMigration& migration :
                LegacySettingMigrations)
            {
                if (version < migration.firstVersion ||
                    version > migration.lastVersion)
                {
                    continue;
                }
                const auto setting = decoded.find(migration.name);
                if (setting == decoded.end())
                {
                    error = "schema " + std::to_string(version) +
                        " snapshot is missing retired setting '" +
                        std::string(migration.name) + "'";
                    return false;
                }
                bool valid = false;
                if (migration.validation == LegacySettingValidation::BoundedFloat3 ||
                    migration.validation == LegacySettingValidation::BoundedFloat4 ||
                    migration.validation == LegacySettingValidation::SkinPalette)
                {
                    const bool unavailable = migration.validation == LegacySettingValidation::SkinPalette && stockSkin;
                    valid = unavailable ? setting->second == "<unavailable>" :
                        ValidateLegacyColor(setting->second, migration.validation == LegacySettingValidation::BoundedFloat3 ? 3u : 4u);
                }
                else if (migration.validation == LegacySettingValidation::BoundedNumber)
                {
                    float value = 0.f;
                    valid = ParseCanonicalSettingsFloat(setting->second, value) &&
                        value >= migration.minimum && value <= migration.maximum;
                }
                else if (migration.validation == LegacySettingValidation::BoundedInteger)
                {
                    int value = 0;
                    const auto& token = setting->second;
                    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
                    valid = parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size() &&
                        std::to_string(value) == token && value >= migration.minimum && value <= migration.maximum;
                }
                else if (migration.validation == LegacySettingValidation::HistoryFrames)
                {
                    int value = 0;
                    const auto& token = setting->second;
                    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
                    valid = parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size() &&
                        std::to_string(value) == token && (value == -1 || (value >= 1 && value <= 32));
                }
                else if (migration.validation == LegacySettingValidation::HistoryStrength)
                {
                    float value = 0.f;
                    valid = ParseCanonicalSettingsFloat(setting->second, value) &&
                        (value == -1.f || (value >= 0.f && value <= 2.f));
                    if (migration.name == "anti-aliasing.sharpen.strength")
                        valid = valid && value >= 0.f && value <= 1.f;
                }
                else
                {
                    for (std::size_t index = 0u;
                        index < migration.tokenCount;
                        ++index)
                    {
                        valid = valid ||
                            setting->second == migration.tokens[index];
                    }
                }
                if (!valid)
                {
                    error = "schema " + std::to_string(version) +
                        " snapshot has invalid retired setting '" +
                        std::string(migration.name) + "'";
                    return false;
                }
                decoded.erase(setting);
            }
            if (version <= 0x0012u)
            {
                for (const auto& definition : UiSettingsCommandCatalog)
                {
                    if (definition.name.substr(0u, 11u) != "tonemapper." ||
                        (version == 0x0012u && definition.id != SettingId::TonemapperEnabled))
                        continue;
                    UiSettingsValue value;
                    std::string canonical;
                    if (decoded.count(std::string(definition.name)) ||
                        !GetDeclaredUiSettingsDefaultValue(definition, value) ||
                        !FormatUiSettingsValue(definition, value, canonical, error))
                    {
                        error = "older snapshot contains unexpected tonemapper setting '" +
                            std::string(definition.name) + "'";
                        return false;
                    }
                    decoded.emplace(std::string(definition.name), std::move(canonical));
                }
            }
            if (version <= 0x0013u)
            {
                for (const SettingId id : { SettingId::ShadowsRayTracedHard,
                        SettingId::ShadowsRayTracedSamplesPerPixel })
                {
                    const auto& definition = *FindSettingsCommandDefinition(id);
                    UiSettingsValue value;
                    std::string canonical;
                    if (decoded.count(std::string(definition.name)) ||
                        !GetDeclaredUiSettingsDefaultValue(definition, value) ||
                        !FormatUiSettingsValue(definition, value, canonical, error))
                    {
                        error = "older snapshot contains unexpected shadow setting '" +
                            std::string(definition.name) + "'";
                        return false;
                    }
                    decoded.emplace(std::string(definition.name), std::move(canonical));
                }
            }
            if (version <= 0x0015u)
            {
                if (const auto skin = decoded.find("ui.skin");
                    skin != decoded.end() && skin->second == "cap")
                {
                    error = "older snapshot contains unexpected skin 'cap'";
                    return false;
                }
                for (const auto& definition : UiSettingsCommandCatalog)
                {
                    if (definition.section != UiSettingsCommandSection::Pathing)
                        continue;
                    if (decoded.count(std::string(definition.name)))
                    {
                        error = "older snapshot contains unexpected pathing setting '" +
                            std::string(definition.name) + "'";
                        return false;
                    }
                    // retain the old four hit depths and its roulette floor, with no filtering.
                    decoded.emplace(std::string(definition.name),
                        definition.id == SettingId::PathingMaximumBounces ? "3" :
                        definition.id == SettingId::PathingMinimumBounces ? "1" :
                        definition.id == SettingId::PathingFireflyFilter ? "off" : "5000");
                }
            }
            return true;
        }

        [[nodiscard]] bool TryParseUnsignedToken(
            std::string_view token,
            std::uint64_t& value)
        {
            if (token.empty())
                return false;
            std::uint64_t parsed = 0u;
            const auto result = std::from_chars(
                token.data(), token.data() + token.size(), parsed, 10);
            if (result.ec != std::errc{} ||
                result.ptr != token.data() + token.size())
            {
                return false;
            }
            value = parsed;
            return true;
        }

        [[nodiscard]] bool TryParseSchemaVersion(
            std::string_view token,
            std::uint16_t& value)
        {
            if (token.size() != 4u)
                return false;
            unsigned int parsed = 0u;
            const auto result = std::from_chars(
                token.data(), token.data() + token.size(), parsed, 16);
            if (result.ec != std::errc{} ||
                result.ptr != token.data() + token.size() ||
                parsed > (std::numeric_limits<std::uint16_t>::max)())
            {
                return false;
            }
            value = static_cast<std::uint16_t>(parsed);
            return true;
        }

        [[nodiscard]] bool IsDecimalToken(std::string_view token)
        {
            return !token.empty() && std::all_of(
                token.begin(), token.end(), [](unsigned char character)
                {
                    return character >= static_cast<unsigned char>('0') &&
                        character <= static_cast<unsigned char>('9');
                });
        }

        [[nodiscard]] bool WriteBytesAtomically(
            const std::filesystem::path& path,
            const std::vector<std::uint8_t>& bytes,
            std::string& error)
        {
            error.clear();
            if (path.empty())
            {
                error = "destination path is empty";
                return false;
            }
            std::error_code directoryError;
            std::filesystem::create_directories(
                path.parent_path(), directoryError);
            if (directoryError)
            {
                error = "could not create destination directory: " +
                    directoryError.message();
                return false;
            }

            std::filesystem::path temporary = path;
            temporary += L".tmp";
            DeleteFileW(temporary.c_str());
            HANDLE file = CreateFileW(
                temporary.c_str(),
                GENERIC_WRITE,
                0u,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                error = "could not create same-directory temporary file "
                    "(Win32 error " + std::to_string(GetLastError()) + ")";
                return false;
            }

            bool wrote = true;
            std::size_t offset = 0u;
            while (offset < bytes.size())
            {
                const std::size_t remaining = bytes.size() - offset;
                const DWORD requested = static_cast<DWORD>((std::min)(
                    remaining,
                    static_cast<std::size_t>(
                        (std::numeric_limits<DWORD>::max)())));
                DWORD written = 0u;
                if (!WriteFile(
                        file,
                        bytes.data() + offset,
                        requested,
                        &written,
                        nullptr) ||
                    written != requested)
                {
                    wrote = false;
                    error = "could not write temporary file (Win32 error " +
                        std::to_string(GetLastError()) + ")";
                    break;
                }
                offset += written;
            }
            if (wrote && !FlushFileBuffers(file))
            {
                wrote = false;
                error = "could not flush temporary file (Win32 error " +
                    std::to_string(GetLastError()) + ")";
            }
            if (!CloseHandle(file) && wrote)
            {
                wrote = false;
                error = "could not close temporary file (Win32 error " +
                    std::to_string(GetLastError()) + ")";
            }
            if (!wrote)
            {
                DeleteFileW(temporary.c_str());
                return false;
            }
            if (!MoveFileExW(
                    temporary.c_str(),
                    path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                error = "could not atomically publish file (Win32 error " +
                    std::to_string(GetLastError()) + ")";
                DeleteFileW(temporary.c_str());
                return false;
            }
            return true;
        }

        [[nodiscard]] bool WriteTextAtomically(
            const std::filesystem::path& path,
            std::string_view text,
            std::string& error)
        {
            const std::vector<std::uint8_t> bytes(text.begin(), text.end());
            return WriteBytesAtomically(path, bytes, error);
        }
    }

    std::string NormalizeCommandAscii(
        std::string_view value,
        bool collapseSeparators)
    {
        std::string normalized;
        normalized.reserve(value.size());
        for (const unsigned char character : value)
        {
            if (character >= static_cast<unsigned char>('A') &&
                character <= static_cast<unsigned char>('Z'))
            {
                normalized.push_back(static_cast<char>(
                    character - static_cast<unsigned char>('A') +
                    static_cast<unsigned char>('a')));
            }
            else if (collapseSeparators &&
                (character == static_cast<unsigned char>(' ') ||
                 character == static_cast<unsigned char>('-') ||
                 character == static_cast<unsigned char>('_') ||
                 character == static_cast<unsigned char>('+')))
            {
                continue;
            }
            else
            {
                normalized.push_back(static_cast<char>(character));
            }
        }
        return normalized;
    }

    bool TryParseCommandInteger(
        std::string_view value,
        std::int64_t& parsed)
    {
        if (value.empty())
            return false;
        const std::string owned(value);
        char* end = nullptr;
        errno = 0;
        const long long candidate = std::strtoll(owned.c_str(), &end, 10);
        if (errno == ERANGE || !end ||
            end != owned.c_str() + owned.size())
        {
            return false;
        }
        parsed = static_cast<std::int64_t>(candidate);
        return true;
    }

    bool RejectUnchangedCommandMutation(
        std::string_view path,
        std::string& error)
    {
        error = "No change: " + std::string(path) +
            " already has the requested value.";
        return false;
    }

    std::filesystem::path GetSettingsSnapshotCatalogPath()
    {
#if defined(UVSR_BUILD_TESTING)
        return GetExecutableDirectoryWide() / "state" /
            ("settings-snapshots-v" + std::string(SettingsSnapshotVersionText.data(), 4u) + ".txt");
#else
        PWSTR localAppData = nullptr;
        const HRESULT result = SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &localAppData);
        if (FAILED(result) || !localAppData || localAppData[0] == L'\0')
        {
            CoTaskMemFree(localAppData);
            return {};
        }
        const std::string version(SettingsSnapshotVersionText.data(), 4u);
        const std::wstring wideVersion(version.begin(), version.end());
        const std::filesystem::path path =
            std::filesystem::path(localAppData) / L"UVSR" /
            (L"settings-snapshots-v" + wideVersion + L".txt");
        CoTaskMemFree(localAppData);
        return path;
#endif
    }

    std::string FormatSettingsSnapshotAdapterToken(std::int64_t index)
    {
        return index < 0 ||
            index > static_cast<std::int64_t>(
                (std::numeric_limits<int>::max)())
            ? std::string{}
            : std::to_string(index);
    }

    std::string FormatSettingsSnapshotSceneToken(std::string_view fileName)
    {
        std::string error;
        return ValidateSettingsSnapshotSelectorToken(
                SettingId::SceneCurrent, fileName, error)
            ? std::string(fileName)
            : std::string{};
    }

    std::string FormatSettingsSnapshotLightToken(
        std::size_t index,
        std::string_view identity)
    {
        const std::string token =
            std::to_string(index) + ":" + std::string(identity);
        std::string error;
        return ValidateSettingsSnapshotSelectorToken(
                SettingId::LightSelected, token, error)
            ? token
            : std::string{};
    }

    std::string FormatSettingsSnapshotMaterialToken(
        bool none,
        std::uint32_t id)
    {
        return none ? "none" : std::to_string(id);
    }

    bool ResolveSettingsSnapshotAdapterToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotAdapterOption>& options,
        std::int64_t& index,
        std::string& canonicalToken,
        std::string& error)
    {
        error.clear();
        std::uint64_t numeric = 0u;
        const bool numericRequest = TryParseUnsignedToken(
                requested, numeric) &&
            numeric <= static_cast<std::uint64_t>(
                (std::numeric_limits<int>::max)());
        if (IsDecimalToken(requested) && !numericRequest)
        {
            error = "adapter index is outside the supported range";
            return false;
        }
        const std::string normalized =
            NormalizeCommandAscii(requested, true);
        const SettingsSnapshotAdapterOption* match = nullptr;
        for (const SettingsSnapshotAdapterOption& option : options)
        {
            const bool matches = numericRequest
                ? option.index == static_cast<std::int64_t>(numeric)
                : NormalizeCommandAscii(option.name, true) == normalized;
            if (!matches)
                continue;
            if (match && match->index != option.index)
            {
                error = "adapter name is ambiguous; use its numeric index";
                return false;
            }
            match = &option;
        }
        if (!match)
        {
            error = "unknown adapter selection";
            return false;
        }
        index = match->index;
        canonicalToken = FormatSettingsSnapshotAdapterToken(index);
        return !canonicalToken.empty();
    }

    bool ResolveSettingsSnapshotSceneToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotSceneOption>& options,
        std::string& fileName,
        std::string& canonicalToken,
        std::string& error)
    {
        error.clear();
        const SettingsSnapshotSceneOption* match = nullptr;
        for (const SettingsSnapshotSceneOption& option : options)
        {
            if (option.fileName != requested)
                continue;
            canonicalToken = FormatSettingsSnapshotSceneToken(
                option.fileName);
            if (canonicalToken.empty())
            {
                error = "scene catalog contains a noncanonical filename";
                return false;
            }
            fileName = option.runtimeFileName.empty()
                ? option.fileName
                : option.runtimeFileName;
            return true;
        }

        const std::string normalized =
            NormalizeCommandAscii(requested, true);
        for (const SettingsSnapshotSceneOption& option : options)
        {
            const bool matches =
                NormalizeCommandAscii(option.fileName, true) == normalized ||
                NormalizeCommandAscii(option.displayName, true) == normalized;
            if (!matches)
                continue;
            if (match && match->fileName != option.fileName)
            {
                error = "scene name is ambiguous; use its exact filename";
                return false;
            }
            match = &option;
        }
        if (!match)
        {
            error = "unknown scene selection";
            return false;
        }
        canonicalToken = FormatSettingsSnapshotSceneToken(match->fileName);
        if (canonicalToken.empty())
        {
            error = "scene catalog contains a noncanonical filename";
            return false;
        }
        fileName = match->runtimeFileName.empty()
            ? match->fileName
            : match->runtimeFileName;
        return true;
    }

    bool ResolveSettingsSnapshotLightToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotLightOption>& options,
        std::size_t& index,
        std::string& canonicalToken,
        std::string& error)
    {
        error.clear();
        std::uint64_t numeric = 0u;
        bool exactIdentity = false;
        bool numericRequest = false;
        std::string_view requestedIdentity;
        const std::size_t separator = requested.find(':');
        if (separator != std::string_view::npos)
        {
            exactIdentity = TryParseUnsignedToken(
                    requested.substr(0u, separator), numeric) &&
                numeric <= static_cast<std::uint64_t>(
                    (std::numeric_limits<std::size_t>::max)());
            numericRequest = exactIdentity;
            requestedIdentity = requested.substr(separator + 1u);
            if (!exactIdentity || requestedIdentity.empty())
            {
                error = "invalid light selector token";
                return false;
            }
        }
        else
        {
            numericRequest = TryParseUnsignedToken(requested, numeric) &&
                numeric <= static_cast<std::uint64_t>(
                    (std::numeric_limits<std::size_t>::max)());
            if (IsDecimalToken(requested) && !numericRequest)
            {
                error = "light index is outside the supported range";
                return false;
            }
        }
        const std::string normalized =
            NormalizeCommandAscii(requested, true);
        const SettingsSnapshotLightOption* match = nullptr;
        for (const SettingsSnapshotLightOption& option : options)
        {
            const bool matches = exactIdentity
                ? option.index == static_cast<std::size_t>(numeric) &&
                    option.identity == requestedIdentity
                : numericRequest
                    ? option.index == static_cast<std::size_t>(numeric)
                    : NormalizeCommandAscii(option.identity, true) ==
                        normalized;
            if (!matches)
                continue;
            if (match && match->index != option.index)
            {
                error = "light name is ambiguous; use its index:identity token";
                return false;
            }
            match = &option;
        }
        if (!match)
        {
            error = "unknown light selection";
            return false;
        }
        index = match->index;
        canonicalToken = FormatSettingsSnapshotLightToken(
            match->index, match->identity);
        if (canonicalToken.empty())
        {
            error = "light table contains a noncanonical identity";
            return false;
        }
        return true;
    }

    bool ResolveSettingsSnapshotMaterialToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotMaterialOption>& options,
        bool& none,
        std::uint32_t& id,
        std::string& canonicalToken,
        std::string& error)
    {
        error.clear();
        if (NormalizeCommandAscii(requested, true) == "none")
        {
            none = true;
            id = 0u;
            canonicalToken = "none";
            return true;
        }

        std::uint64_t numeric = 0u;
        const bool numericRequest = TryParseUnsignedToken(
                requested, numeric) &&
            numeric <=
                (std::numeric_limits<std::uint32_t>::max)();
        if (IsDecimalToken(requested) && !numericRequest)
        {
            error = "material id is outside the supported range";
            return false;
        }
        const std::string normalized =
            NormalizeCommandAscii(requested, true);
        const SettingsSnapshotMaterialOption* match = nullptr;
        for (const SettingsSnapshotMaterialOption& option : options)
        {
            const bool matches = numericRequest
                ? option.id == static_cast<std::uint32_t>(numeric)
                : NormalizeCommandAscii(option.name, true) == normalized;
            if (!matches)
                continue;
            if (match && match->id != option.id)
            {
                error = "material name is ambiguous; use its runtime id";
                return false;
            }
            match = &option;
        }
        if (!match)
        {
            error = "unknown material selection";
            return false;
        }
        none = false;
        id = match->id;
        canonicalToken = FormatSettingsSnapshotMaterialToken(false, id);
        return true;
    }

    SettingsSnapshotController::SettingsSnapshotController()
        : m_Code(BuildSettingsSnapshotCode({}))
    {
    }

    void SettingsSnapshotController::Refresh(
        const SettingsSnapshotValueReader& readValue)
    {
        DecodedSettings settings;
        for (const UiSettingsCommandDefinition& definition :
            UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition))
                continue;

            std::string value;
            std::string error;
            if (!readValue || !readValue(definition.id, value, error))
                value = "<unavailable>";
            settings.emplace(std::string(definition.name), std::move(value));
        }
        m_Canonical = FormatCanonicalSettingsSnapshot(settings);
        m_Code = BuildSettingsSnapshotCode(m_Canonical);
    }

    std::string SettingsSnapshotController::BuildCatalogSection() const
    {
        return "[" + m_Code + "]\n" + m_Canonical +
            "[/" + m_Code + "]\n";
    }

    bool SettingsSnapshotController::Persist(
        const std::filesystem::path& path) const
    {
        if (path.empty())
            return false;

        std::string existing;
        {
            std::ifstream input(path, std::ios::binary);
            if (input.is_open())
            {
                existing.assign(
                    std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>());
                if (input.bad())
                {
                    log::warning(
                        "Could not read the settings snapshot catalog at %s",
                        path.generic_string().c_str());
                    return false;
                }
            }
        }

        const std::string section = BuildCatalogSection();
        if (existing.find(section) != std::string::npos)
            return true;
        const std::string opening = "[" + m_Code + "]\n";
        if (existing.find(opening) != std::string::npos)
        {
            log::warning(
                "Settings snapshot catalog contains a conflicting entry "
                "for %s",
                m_Code.c_str());
            return false;
        }

        std::string updated = std::move(existing);
        if (updated.empty())
        {
            updated = "# UVSR Settings Snapshot Catalog v" +
                std::string(SettingsSnapshotVersionText.data(), 4u) + '\n';
        }
        updated += section;
        std::string writeError;
        if (!WriteTextAtomically(path, updated, writeError))
        {
            log::warning(
                "Could not atomically write the settings snapshot catalog "
                "at %s: %s",
                path.generic_string().c_str(),
                writeError.c_str());
            return false;
        }
        return true;
    }

    bool SettingsSnapshotController::PersistToLocalCatalog() const
    {
        return Persist(GetSettingsSnapshotCatalogPath());
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::FinalizeStagedStep(
        SettingsSnapshotTransactionStep step,
        const SettingsSnapshotRuntimeAccess& access)
    {
        if (step.progress == SettingsSnapshotTransactionProgress::Succeeded)
            Refresh(access.readValue);
        return step;
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::BeginDecodedStaged(
        const DecodedSettings& decoded,
        const SettingsSnapshotRuntimeAccess& access)
    {
        SettingsSnapshotTransactionStep rejected;
        rejected.progress = SettingsSnapshotTransactionProgress::Failed;
        if (!access.sceneReady)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                "settings.load requires a fully loaded scene";
            return rejected;
        }

        std::vector<SettingsSnapshotTransactionEntry> transaction;
        std::string error;
        if (!BuildSettingsSnapshotTransaction(
                decoded,
                transaction,
                error))
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                "snapshot membership rejected: " + error;
            return rejected;
        }
        SettingsSnapshotStagedRuntimeAccess stagedAccess{
            access.validateValue,
            access.readValue,
            access.readRawValue,
            access.writeValue,
            access.driveSelector
        };
        return FinalizeStagedStep(
            m_TransactionCoordinator.Begin(transaction, stagedAccess),
            access);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::ContinueStagedApply(
        const SettingsSnapshotRuntimeAccess& access)
    {
        SettingsSnapshotStagedRuntimeAccess stagedAccess{
            access.validateValue,
            access.readValue,
            access.readRawValue,
            access.writeValue,
            access.driveSelector
        };
        return FinalizeStagedStep(
            m_TransactionCoordinator.Advance(stagedAccess),
            access);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::BeginApplyCanonicalStaged(
        std::string_view canonical,
        const SettingsSnapshotRuntimeAccess& access)
    {
        if (HasStagedApply())
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.progress = SettingsSnapshotTransactionProgress::Failed;
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error =
                "another staged settings transaction is active";
            return rejected;
        }
        m_TransactionCoordinator.Reset();
        SettingsSnapshotTransactionStep rejected;
        rejected.progress = SettingsSnapshotTransactionProgress::Failed;
        DecodedSettings decoded;
        try
        {
            decoded = ParseSettingsSnapshot(canonical);
        }
        catch (const std::exception& exception)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error = "snapshot payload decode failed: " +
                std::string(exception.what());
            return rejected;
        }
        if (FormatCanonicalSettingsSnapshot(decoded) != canonical)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                "snapshot payload is not complete canonical text";
            return rejected;
        }
        return BeginDecodedStaged(decoded, access);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::BeginLoadCodeStaged(
        std::string_view code,
        const SettingsSnapshotRuntimeAccess& access)
    {
        if (HasStagedApply())
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.progress = SettingsSnapshotTransactionProgress::Failed;
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error =
                "another staged settings transaction is active";
            return rejected;
        }
        m_TransactionCoordinator.Reset();
        SettingsSnapshotTransactionStep rejected;
        rejected.progress = SettingsSnapshotTransactionProgress::Failed;
        std::string error;
        if (!ValidateSettingsSnapshotLoadCode(code, error))
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error = std::move(error);
            return rejected;
        }

        DecodedSettings decoded;
        try
        {
            decoded = DecodeSettingsSnapshot(
                code,
                GetDefaultSettingsSnapshotCatalogPaths(
                    code.substr(0u, 4u)));
        }
        catch (const std::exception& exception)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error = "snapshot decode failed: " +
                std::string(exception.what());
            return rejected;
        }

        const std::string_view version = code.substr(0u, 4u);
        const std::string canonical =
            FormatCanonicalSettingsSnapshot(decoded);
        if (BuildSettingsSnapshotCode(canonical, version) != std::string(code))
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                "snapshot payload is not in canonical command-name order";
            return rejected;
        }
        std::uint16_t numericVersion = 0u;
        if (!TryParseSchemaVersion(version, numericVersion) ||
            !ApplyLegacySettingMigrations(
                numericVersion,
                decoded,
                error))
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error = error.empty()
                ? "snapshot schema version is invalid"
                : std::move(error);
            return rejected;
        }
        return BeginDecodedStaged(decoded, access);
    }

}
