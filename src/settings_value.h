#pragma once

#include "ui_settings_command_catalog.h"

#include <utility>

namespace uvsr
{
    struct SettingsSnapshotValidationContext
    {
        bool hasMaterialBaseTexture = false;
        bool materialHasBaseTexture = false;
    };

    enum class UiSettingsValueKind : std::uint8_t
    {
        Boolean,
        Integer,
        Float,
        Vector,
        Token,
        Selector
    };

    struct UiSettingsValue
    {
        UiSettingsValueKind kind = UiSettingsValueKind::Token;
        bool boolean = false;
        std::int64_t integer = 0;
        float scalar = 0.f;
        std::array<float, 4> vector{};
        std::uint8_t componentCount = 0u;
        std::string text;

        [[nodiscard]] bool operator==(const UiSettingsValue& other) const noexcept;

        [[nodiscard]] static UiSettingsValue Boolean(bool value)
        {
            UiSettingsValue result;
            result.kind = UiSettingsValueKind::Boolean;
            result.boolean = value;
            return result;
        }
        [[nodiscard]] static UiSettingsValue Integer(std::int64_t value)
        {
            UiSettingsValue result;
            result.kind = UiSettingsValueKind::Integer;
            result.integer = value;
            return result;
        }
        [[nodiscard]] static UiSettingsValue Float(float value)
        {
            UiSettingsValue result;
            result.kind = UiSettingsValueKind::Float;
            result.scalar = value;
            return result;
        }
        [[nodiscard]] static UiSettingsValue Vector(
            const std::array<float, 4>& value,
            std::uint8_t componentCount)
        {
            UiSettingsValue result;
            result.kind = UiSettingsValueKind::Vector;
            result.vector = value;
            result.componentCount = componentCount;
            return result;
        }
        [[nodiscard]] static UiSettingsValue Token(std::string value)
        {
            UiSettingsValue result;
            result.kind = UiSettingsValueKind::Token;
            result.text = std::move(value);
            return result;
        }
        [[nodiscard]] static UiSettingsValue Selector(std::string value)
        {
            UiSettingsValue result;
            result.kind = UiSettingsValueKind::Selector;
            result.text = std::move(value);
            return result;
        }
    };

    [[nodiscard]] bool ParseCanonicalUiSettingsValue(
        const UiSettingsCommandDefinition& definition,
        std::string_view canonical,
        UiSettingsValue& value,
        std::string& error,
        SettingsSnapshotValidationContext context = {});
    [[nodiscard]] bool FormatUiSettingsValue(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue& value,
        std::string& canonical,
        std::string& error);
    [[nodiscard]] bool ValidateUiSettingsValue(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue& value,
        std::string& error,
        SettingsSnapshotValidationContext context = {});
    [[nodiscard]] bool GetDeclaredUiSettingsDefaultValue(
        const UiSettingsCommandDefinition& definition,
        UiSettingsValue& value) noexcept;

    [[nodiscard]] bool ValidateSettingsSnapshotSelectorToken(
        SettingId id,
        std::string_view token,
        std::string& error);

    [[nodiscard]] bool ValidateSettingsSnapshotCatalogValue(
        const UiSettingsCommandDefinition& definition,
        std::string_view value,
        std::string& error,
        SettingsSnapshotValidationContext context = {});

    [[nodiscard]] bool ParseCanonicalSettingsFloat(std::string_view text, float& value);
}