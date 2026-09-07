#pragma once

#include "settings_snapshot_transaction.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr
{
    [[nodiscard]] std::string NormalizeCommandAscii(
        std::string_view value,
        bool collapseSeparators = false);
    [[nodiscard]] bool TryParseCommandInteger(
        std::string_view value,
        std::int64_t& parsed);
    [[nodiscard]] bool RejectUnchangedCommandMutation(
        std::string_view path,
        std::string& error);
    [[nodiscard]] inline const UiSettingsCommandDefinition*
        FindSettingsCommandDefinition(SettingId id) noexcept
    {
        for (const UiSettingsCommandDefinition& definition :
            UiSettingsCommandCatalog)
        {
            if (definition.id == id)
                return &definition;
        }
        return nullptr;
    }
    [[nodiscard]] std::filesystem::path
        GetSettingsSnapshotCatalogPath();

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
        SettingsSnapshotValueReader readRawValue;
        SettingsSnapshotValueWriter writeValue;
        SettingsSnapshotSelectorDriver driveSelector;
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
        [[nodiscard]] bool HasStagedApply() const noexcept
        {
            return m_TransactionCoordinator.IsActive();
        }

    private:
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
