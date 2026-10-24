#include "settings_snapshot_legacy.h"
#include "settings_value.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace uvsr
{
    namespace
    {
        // retired accents accepted canonical subnormals, including ERANGE conversions.
        bool TryParseLegacyFloat(std::string_view value, float& parsed,
            SettingsSnapshotError& error) noexcept
        {
            if (value.empty()) return false;
            SettingsSnapshotText owned;
            if (!owned.Assign(value, error)) return false;
            const auto input = owned.View();
            char* end = nullptr;
            const float candidate = std::strtof(input.data(), &end);
            if (end != input.data() + input.size() || !std::isfinite(candidate))
                return false;
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

        bool ValidateLegacyColor(std::string_view value, size_t components,
            SettingsSnapshotError& error) noexcept
        {
            size_t count = 0;
            size_t begin = 0;
            for (;;)
            {
                const size_t separator = value.find(' ', begin);
                const auto token = value.substr(begin, separator == std::string_view::npos
                    ? std::string_view::npos : separator - begin);
                float parsed = 0.f;
                if (token.empty() || !TryParseLegacyFloat(token, parsed, error) ||
                    parsed < 0.f || parsed > 1.f ||
                    FormatUiSettingsMetadataFloat(parsed).View() != token)
                    return false;
                ++count;
                if (separator == std::string_view::npos) break;
                begin = separator + 1;
            }
            return count == components;
        }

        bool ParseLegacyInteger(std::string_view token, int& value) noexcept
        {
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) return false;
            char canonical[32];
            const auto formatted = std::to_chars(canonical, canonical + sizeof(canonical), value);
            return formatted.ec == std::errc{} &&
                std::string_view(canonical, static_cast<size_t>(formatted.ptr - canonical)) == token;
        }

        bool LegacyFailure(uint16_t version, std::string_view name, bool missing,
            SettingsSnapshotError& error) noexcept
        {
            char number[8];
            const auto formatted = std::to_chars(number, number + sizeof(number), version);
            error = ComposeSettingsSnapshotError({"schema ",
                {number, static_cast<size_t>(formatted.ptr - number)},
                missing ? " snapshot is missing retired setting '" : " snapshot has invalid retired setting '",
                name, "'"});
            return false;
        }

        bool UnexpectedDefault(std::string_view group, std::string_view name,
            SettingsSnapshotError& error) noexcept
        {
            error = ComposeSettingsSnapshotError({"older snapshot contains unexpected ",
                group, " setting '", name, "'"});
            return false;
        }

        bool InsertDeclaredDefault(DecodedSettings& decoded,
            const UiSettingsCommandDefinition& definition, std::string_view group,
            SettingsSnapshotError& error) noexcept
        {
            if (decoded.Find(definition.name)) return UnexpectedDefault(group, definition.name, error);
            UiSettingsValue value;
            SettingsSnapshotText canonical;
            if (!GetDeclaredUiSettingsDefaultValue(definition, value, error) ||
                !FormatUiSettingsValue(definition, value, canonical, error))
            {
                if (error.code == SettingsSnapshotErrorCode::None || error.code == SettingsSnapshotErrorCode::InvalidInput)
                    return UnexpectedDefault(group, definition.name, error);
                return false;
            }
            return decoded.Insert(definition.name, canonical.View(), error);
        }
    }

    bool ApplyLegacySettingMigrations(uint16_t version,
        DecodedSettings& decoded, SettingsSnapshotError& error) noexcept
    {
        error = {};
        const auto* skin = decoded.Find("ui.skin");
        const bool stockSkin = skin && skin->value != "amp";
        for (const auto& migration : LegacySettingMigrations)
        {
            if (version < migration.firstVersion || version > migration.lastVersion) continue;
            const auto* setting = decoded.Find(migration.name);
            if (!setting) return LegacyFailure(version, migration.name, true, error);
            bool valid = false;
            SettingsSnapshotError validationError;
            if (migration.validation == LegacySettingValidation::BoundedFloat3 ||
                migration.validation == LegacySettingValidation::BoundedFloat4 ||
                migration.validation == LegacySettingValidation::SkinPalette)
            {
                const bool unavailable = migration.validation == LegacySettingValidation::SkinPalette && stockSkin;
                valid = unavailable ? setting->value == "<unavailable>" :
                    ValidateLegacyColor(setting->value,
                        migration.validation == LegacySettingValidation::BoundedFloat3 ? 3u : 4u, validationError);
            }
            else if (migration.validation == LegacySettingValidation::BoundedNumber)
            {
                float value = 0.f;
                valid = ParseCanonicalSettingsFloat(setting->value, value, validationError) &&
                    value >= migration.minimum && value <= migration.maximum;
            }
            else if (migration.validation == LegacySettingValidation::BoundedInteger)
            {
                int value = 0;
                valid = ParseLegacyInteger(setting->value, value) && value >= migration.minimum && value <= migration.maximum;
            }
            else if (migration.validation == LegacySettingValidation::HistoryFrames)
            {
                int value = 0;
                valid = ParseLegacyInteger(setting->value, value) && (value == -1 || (value >= 1 && value <= 32));
            }
            else if (migration.validation == LegacySettingValidation::HistoryStrength)
            {
                float value = 0.f;
                valid = ParseCanonicalSettingsFloat(setting->value, value, validationError) &&
                    (value == -1.f || (value >= 0.f && value <= 2.f));
                if (migration.name == "anti-aliasing.sharpen.strength") valid = valid && value >= 0.f && value <= 1.f;
            }
            else
            {
                for (size_t index = 0; index < migration.tokenCount; ++index)
                    valid = valid || setting->value == migration.tokens[index];
            }
            if (!valid)
            {
                if (validationError.code != SettingsSnapshotErrorCode::None &&
                    validationError.code != SettingsSnapshotErrorCode::InvalidInput)
                {
                    error = std::move(validationError);
                    return false;
                }
                return LegacyFailure(version, migration.name, false, error);
            }
            (void)decoded.Erase(migration.name);
        }
        if (version <= 0x0012u)
        {
            for (const auto& definition : UiSettingsCommandCatalog)
            {
                if (definition.name.substr(0, 11) != "tonemapper." ||
                    (version == 0x0012u && definition.id != SettingId::TonemapperEnabled)) continue;
                if (!InsertDeclaredDefault(decoded, definition, "tonemapper", error)) return false;
            }
        }
        if (version <= 0x0013u)
        {
            for (const SettingId id : {SettingId::ShadowsRayTracedHard, SettingId::ShadowsRayTracedSamplesPerPixel})
            {
                const UiSettingsCommandDefinition* found = nullptr;
                for (const auto& definition : UiSettingsCommandCatalog)
                    if (definition.id == id) { found = &definition; break; }
                if (!found)
                {
                    error = {SettingsSnapshotErrorCode::Catalog, 0, 0, "legacy shadow setting is absent from the catalog", {}};
                    return false;
                }
                if (!InsertDeclaredDefault(decoded, *found, "shadow", error)) return false;
            }
        }
        if (version <= 0x0015u)
        {
            if (const auto* remainingSkin = decoded.Find("ui.skin"); remainingSkin && remainingSkin->value == "cap")
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "older snapshot contains unexpected skin 'cap'", {}};
                return false;
            }
            for (const auto& definition : UiSettingsCommandCatalog)
            {
                if (definition.section != UiSettingsCommandSection::Pathing) continue;
                if (decoded.Find(definition.name)) return UnexpectedDefault("pathing", definition.name, error);
                // retain the old four hit depths and its roulette floor, with no filtering.
                if (!decoded.Insert(definition.name,
                    definition.id == SettingId::PathingMaximumBounces ? "3" :
                    definition.id == SettingId::PathingMinimumBounces ? "1" :
                    definition.id == SettingId::PathingFireflyFilter ? "off" : "5000", error)) return false;
            }
        }
        return true;
    }
}
