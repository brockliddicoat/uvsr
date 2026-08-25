#pragma once

#include "settings_snapshot_transaction.h"
#include "ui_skin.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace uvsr
{
    enum class CommandValueOperation
    {
        Get,
        Set,
        Toggle,
        Reset
    };

    struct UiSettingsCommandBinding
    {
        std::string_view name;
        UiSettingsCommandSection section;
        UiSettingsCommandKind kind;
    };

    inline constexpr auto UiSettingsCommandBindings = []()
    {
        std::array<
            UiSettingsCommandBinding,
            UiSettingsCommandCatalog.size()> bindings{};
        for (std::size_t index = 0u;
            index < UiSettingsCommandCatalog.size();
            ++index)
        {
            bindings[index] = {
                UiSettingsCommandCatalog[index].name,
                UiSettingsCommandCatalog[index].section,
                UiSettingsCommandCatalog[index].kind
            };
        }
        return bindings;
    }();

    static_assert(
        UiSettingsCommandBindings.size() ==
        UiSettingsCommandCatalog.size());

    class SettingsCommandFloatPrecisionScope
    {
    public:
        SettingsCommandFloatPrecisionScope();
        ~SettingsCommandFloatPrecisionScope();

        SettingsCommandFloatPrecisionScope(
            const SettingsCommandFloatPrecisionScope&) = delete;
        SettingsCommandFloatPrecisionScope& operator=(
            const SettingsCommandFloatPrecisionScope&) = delete;

    private:
        bool m_Previous = false;
    };

    [[nodiscard]] std::string NormalizeCommandAscii(
        std::string_view value,
        bool collapseSeparators = false);
    [[nodiscard]] bool StartsWithCommandPrefix(
        std::string_view value,
        std::string_view prefix);
    [[nodiscard]] std::string JoinCommandArguments(
        const std::vector<std::string>& arguments);
    [[nodiscard]] bool TryParseCommandBool(
        std::string_view value,
        bool& parsed);
    [[nodiscard]] bool TryParseCommandFloat(
        std::string_view value,
        float& parsed);
    [[nodiscard]] bool TryParseCommandInteger(
        std::string_view value,
        std::int64_t& parsed);
    [[nodiscard]] std::string FormatCommandFloat(float value);
    [[nodiscard]] bool RejectUnchangedCommandMutation(
        std::string_view path,
        std::string& error);
    [[nodiscard]] std::string FormatCommandUiColorRgb(
        const UiRgbaColor& color);
    [[nodiscard]] std::string FormatCommandUiColorRgba(
        const UiRgbaColor& color);
    [[nodiscard]] bool ApplyCommandUiColorRgb(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        UiRgbaColor& current,
        const UiRgbaColor& defaultValue,
        std::string& value,
        std::string& error);
    [[nodiscard]] bool ApplyCommandUiColorRgba(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        UiRgbaColor& current,
        const UiRgbaColor& defaultValue,
        std::string& value,
        std::string& error);
    [[nodiscard]] const UiSettingsCommandDefinition*
        FindSettingsCommandDefinition(std::string_view rawName);
    [[nodiscard]] UiSettingsCommandVerb GetSettingsCommandVerb(
        CommandValueOperation operation);
    [[nodiscard]] std::string GetSettingsCommandVerbList(
        const UiSettingsCommandDefinition& definition);
    [[nodiscard]] bool ApplyCommandBool(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        bool& current,
        bool defaultValue,
        std::string& value,
        std::string& error);
    [[nodiscard]] bool ApplyCommandInteger(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        int& current,
        int defaultValue,
        int minimum,
        int maximum,
        std::string& value,
        std::string& error);
    [[nodiscard]] bool ApplyCommandUnsigned(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        std::uint32_t& current,
        std::uint32_t defaultValue,
        std::uint32_t minimum,
        std::uint32_t maximum,
        std::string& value,
        std::string& error);
    [[nodiscard]] bool ApplyCommandFloat(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        float& current,
        float defaultValue,
        float minimum,
        float maximum,
        std::string& value,
        std::string& error);

    template <typename Enum, std::size_t Count>
    [[nodiscard]] bool ApplyCommandEnum(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        Enum& current,
        Enum defaultValue,
        const std::array<std::pair<std::string_view, Enum>, Count>& options,
        std::string& value,
        std::string& error,
        bool allowSameValueMutation = false)
    {
        Enum candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            if (arguments.empty())
            {
                error = std::string(path) + " expects one value.";
                return false;
            }
            const std::string requested = NormalizeCommandAscii(
                JoinCommandArguments(arguments), true);
            const auto match = std::find_if(
                options.begin(),
                options.end(),
                [&requested](const auto& option)
                {
                    return NormalizeCommandAscii(
                        option.first, true) == requested;
                });
            if (match == options.end())
            {
                error = std::string(path) + " expects ";
                for (std::size_t index = 0u; index < options.size(); ++index)
                {
                    if (index > 0u)
                    {
                        error += index + 1u == options.size()
                            ? ", or "
                            : ", ";
                    }
                    error += options[index].first;
                }
                error += ".";
                return false;
            }
            candidate = match->second;
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }

        if (operation != CommandValueOperation::Get &&
            candidate == current && !allowSameValueMutation)
        {
            return RejectUnchangedCommandMutation(path, error);
        }
        current = candidate;
        const auto selected = std::find_if(
            options.begin(),
            options.end(),
            [&current](const auto& option)
            {
                return option.second == current;
            });
        value = selected != options.end()
            ? std::string(selected->first)
            : "unknown";
        return true;
    }

    [[nodiscard]] std::vector<std::string>
        BuildSettingsSnapshotCommandArguments(
            const UiSettingsCommandDefinition& definition,
            std::string_view requestedValue);
    [[nodiscard]] std::filesystem::path
        GetSettingsSnapshotCatalogPath();
    [[nodiscard]] std::filesystem::path
        GetSettingsSnapshotRestartHandoffPath();
    [[nodiscard]] bool PersistSettingsSnapshotRestartHandoff(
        const std::filesystem::path& path,
        const SettingsSnapshotRestartHandoff& handoff,
        std::string& error);
    [[nodiscard]] bool LoadSettingsSnapshotRestartHandoff(
        const std::filesystem::path& path,
        SettingsSnapshotRestartHandoff& handoff,
        bool& found,
        std::string& error);
    [[nodiscard]] bool RemoveSettingsSnapshotRestartHandoff(
        const std::filesystem::path& path,
        std::string& error);

    struct SettingsSnapshotAdapterOption
    {
        std::int64_t index = -1;
        std::string name;
    };

    struct SettingsSnapshotSceneOption
    {
        std::string fileName;
        std::string displayName;
        std::string runtimeFileName;
    };

    struct SettingsSnapshotLightOption
    {
        std::size_t index = 0u;
        std::string identity;
    };

    struct SettingsSnapshotMaterialOption
    {
        std::uint32_t id = 0u;
        std::string name;
    };

    [[nodiscard]] std::string FormatSettingsSnapshotAdapterToken(
        std::int64_t index);
    [[nodiscard]] std::string FormatSettingsSnapshotSceneToken(
        std::string_view fileName);
    [[nodiscard]] std::string FormatSettingsSnapshotLightToken(
        std::size_t index,
        std::string_view identity);
    [[nodiscard]] std::string FormatSettingsSnapshotMaterialToken(
        bool none,
        std::uint32_t id = 0u);

    [[nodiscard]] bool ResolveSettingsSnapshotAdapterToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotAdapterOption>& options,
        std::int64_t& index,
        std::string& canonicalToken,
        std::string& error);
    [[nodiscard]] bool ResolveSettingsSnapshotSceneToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotSceneOption>& options,
        std::string& fileName,
        std::string& canonicalToken,
        std::string& error);
    [[nodiscard]] bool ResolveSettingsSnapshotLightToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotLightOption>& options,
        std::size_t& index,
        std::string& canonicalToken,
        std::string& error);
    [[nodiscard]] bool ResolveSettingsSnapshotMaterialToken(
        std::string_view requested,
        const std::vector<SettingsSnapshotMaterialOption>& options,
        bool& none,
        std::uint32_t& id,
        std::string& canonicalToken,
        std::string& error);

    struct SettingsSnapshotRuntimeAccess
    {
        bool sceneReady = false;
        SettingsSnapshotValueValidator validateValue;
        SettingsSnapshotValueReader readValue;
        SettingsSnapshotValueWriter writeValue;
        SettingsSnapshotSelectorDriver driveSelector;
        SettingsSnapshotRestartHandoffWriter persistRestartHandoff;
    };

    class SettingsSnapshotController
    {
    public:
        SettingsSnapshotController();

        [[nodiscard]] const std::string& Code() const noexcept
        {
            return m_Code;
        }

        [[nodiscard]] const std::string& Canonical() const noexcept
        {
            return m_Canonical;
        }

        void Refresh(const SettingsSnapshotValueReader& readValue);
        [[nodiscard]] std::string BuildCatalogSection() const;
        [[nodiscard]] bool Persist(
            const std::filesystem::path& path) const;
        [[nodiscard]] bool PersistToLocalCatalog() const;

        [[nodiscard]] bool ApplyCanonical(
            std::string_view canonical,
            const SettingsSnapshotRuntimeAccess& access,
            std::size_t& changedValueCount,
            std::string& error);
        [[nodiscard]] bool LoadCode(
            std::string_view code,
            const SettingsSnapshotRuntimeAccess& access,
            std::size_t& changedValueCount,
            std::string& error);

        [[nodiscard]] SettingsSnapshotTransactionStep
        BeginApplyCanonicalStaged(
            std::string_view canonical,
            const SettingsSnapshotRuntimeAccess& access);
        [[nodiscard]] SettingsSnapshotTransactionStep
        BeginLoadCodeStaged(
            std::string_view code,
            const SettingsSnapshotRuntimeAccess& access);
        [[nodiscard]] SettingsSnapshotTransactionStep ContinueStagedApply(
            const SettingsSnapshotRuntimeAccess& access);
        [[nodiscard]] SettingsSnapshotTransactionStep ResumeStagedApply(
            const SettingsSnapshotRestartHandoff& handoff,
            const SettingsSnapshotRuntimeAccess& access);
        [[nodiscard]] bool HasStagedApply() const noexcept
        {
            return m_TransactionCoordinator.IsActive();
        }

    private:
        [[nodiscard]] bool ApplyDecodedImmediate(
            const DecodedSettings& decoded,
            const SettingsSnapshotRuntimeAccess& access,
            std::size_t& changedValueCount,
            std::string& error);
        [[nodiscard]] SettingsSnapshotTransactionStep BeginDecodedStaged(
            const DecodedSettings& decoded,
            const SettingsSnapshotRuntimeAccess& access);
        [[nodiscard]] SettingsSnapshotTransactionStep FinalizeStagedStep(
            SettingsSnapshotTransactionStep step,
            const SettingsSnapshotRuntimeAccess& access);

        std::string m_Code;
        std::string m_Canonical;
        SettingsSnapshotTransactionCoordinator m_TransactionCoordinator;
    };
}
