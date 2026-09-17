#pragma once

#include "ui_settings_command_catalog.h"
#include "settings_snapshot_storage.h"

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

    class SettingsSnapshotText
    {
    public:
        SettingsSnapshotText() noexcept = default;
        ~SettingsSnapshotText() noexcept;
        SettingsSnapshotText(const SettingsSnapshotText&) = delete;
        SettingsSnapshotText& operator=(const SettingsSnapshotText&) = delete;
        SettingsSnapshotText(SettingsSnapshotText&& other) noexcept;
        SettingsSnapshotText& operator=(SettingsSnapshotText&& other) noexcept;

        // successful assignment, movement or destruction invalidates views.
        // failed assignment and cloning preserve output bytes and views.
        [[nodiscard]] std::string_view View() const noexcept
        {
            return {m_LongText ? m_LongText : m_ShortText, m_TextSize};
        }
        [[nodiscard]] bool Assign(std::string_view text, SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool AssignParts(std::initializer_list<std::string_view> parts,
            SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool CloneTo(SettingsSnapshotText& output, SettingsSnapshotError& error) const noexcept;

    private:
        // preserve the previous MSVC string threshold without a path-size limit.
        char m_ShortText[16]{};
        char* m_LongText = nullptr;
        size_t m_TextSize = 0;
        [[nodiscard]] bool StoreParts(std::initializer_list<std::string_view> parts, SettingsSnapshotError& error) noexcept;
    };

    struct UiSettingsValue
    {
        UiSettingsValueKind kind = UiSettingsValueKind::Token;
        bool boolean = false;
        std::int64_t integer = 0;
        float scalar = 0.f;
        std::array<float, 4> vector{};
        std::uint8_t componentCount = 0u;

        UiSettingsValue() noexcept = default;
        ~UiSettingsValue() noexcept = default;
        UiSettingsValue(const UiSettingsValue&) = delete;
        UiSettingsValue& operator=(const UiSettingsValue&) = delete;
        UiSettingsValue(UiSettingsValue&& other) noexcept;
        UiSettingsValue& operator=(UiSettingsValue&& other) noexcept;

        // text is owned. successful assignment, movement or destruction
        // invalidates views; failed assignment and cloning preserve output.
        [[nodiscard]] std::string_view Text() const noexcept
        {
            return m_Text.View();
        }
        [[nodiscard]] bool SetToken(std::string_view text, SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool SetSelector(std::string_view text, SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool SetSelectorParts(std::initializer_list<std::string_view> parts,
            SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool CloneTo(UiSettingsValue& output, SettingsSnapshotError& error) const noexcept;

        [[nodiscard]] bool operator==(const UiSettingsValue& other) const noexcept;

        [[nodiscard]] static UiSettingsValue Boolean(bool value) noexcept
        {
            UiSettingsValue result;
            result.kind = UiSettingsValueKind::Boolean;
            result.boolean = value;
            return result;
        }
        [[nodiscard]] static UiSettingsValue Integer(std::int64_t value) noexcept
        {
            UiSettingsValue result;
            result.kind = UiSettingsValueKind::Integer;
            result.integer = value;
            return result;
        }
        [[nodiscard]] static UiSettingsValue Float(float value) noexcept
        {
            UiSettingsValue result;
            result.kind = UiSettingsValueKind::Float;
            result.scalar = value;
            return result;
        }
        [[nodiscard]] static UiSettingsValue Vector(
            const std::array<float, 4>& value,
            std::uint8_t componentCount) noexcept
        {
            UiSettingsValue result;
            result.kind = UiSettingsValueKind::Vector;
            result.vector = value;
            result.componentCount = componentCount;
            return result;
        }

    private:
        SettingsSnapshotText m_Text;
        [[nodiscard]] bool SetText(UiSettingsValueKind kind, std::string_view text,
            SettingsSnapshotError& error) noexcept;
    };

    [[nodiscard]] bool ParseCanonicalUiSettingsValue(
        const UiSettingsCommandDefinition& definition,
        std::string_view canonical,
        UiSettingsValue& value,
        SettingsSnapshotError& error,
        SettingsSnapshotValidationContext context = {}) noexcept;
    [[nodiscard]] bool FormatUiSettingsValue(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue& value,
        json::EncodedText& canonical,
        SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool FormatUiSettingsValue(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue& value,
        SettingsSnapshotText& canonical,
        SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool ValidateUiSettingsValue(
        const UiSettingsCommandDefinition& definition,
        const UiSettingsValue& value,
        SettingsSnapshotError& error,
        SettingsSnapshotValidationContext context = {}) noexcept;
    [[nodiscard]] bool GetDeclaredUiSettingsDefaultValue(
        const UiSettingsCommandDefinition& definition,
        UiSettingsValue& value, SettingsSnapshotError& error) noexcept;

    [[nodiscard]] bool ValidateSettingsSnapshotSelectorToken(
        SettingId id,
        std::string_view token,
        SettingsSnapshotError& error) noexcept;

    [[nodiscard]] bool ValidateSettingsSnapshotCatalogValue(
        const UiSettingsCommandDefinition& definition,
        std::string_view value,
        SettingsSnapshotError& error,
        SettingsSnapshotValidationContext context = {}) noexcept;

    [[nodiscard]] bool ParseCanonicalSettingsFloat(std::string_view text, float& value,
        SettingsSnapshotError& error) noexcept;

#if defined(UVSR_SETTINGS_VALUE_TEST_HOOKS)
    void FailUiSettingsValueAllocationAfter(size_t successfulAllocations) noexcept;
    void ClearUiSettingsValueAllocationFailure() noexcept;
#endif
}
