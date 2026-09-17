#pragma once

#include "settings_snapshot_transaction.h"
#include "settings_snapshot.h"
#include "settings_snapshot_decoder.h"
#include "settings_snapshot_selectors.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uvsr
{
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
    struct SettingsSnapshotRuntimeAccess
    {
        bool sceneReady = false;
        void* context = nullptr;
        SettingsSnapshotValueValidator validateValue = nullptr;
        SettingsSnapshotValueReader readValue = nullptr;
        SettingsSnapshotValueReader readRawValue = nullptr;
        SettingsSnapshotValueWriter writeValue = nullptr;
        SettingsSnapshotSelectorDriver driveSelector = nullptr;
    };

    class SettingsSnapshotController
    {
    public:
        explicit SettingsSnapshotController(SettingsSnapshotCatalogLocation location) noexcept;

        [[nodiscard]] std::string_view Code() const noexcept
        {
            return m_Code.View();
        }

        [[nodiscard]] std::string_view Canonical() const noexcept
        {
            return {m_Canonical.Data(), m_Canonical.Size()};
        }

        [[nodiscard]] bool Refresh(const SettingsSnapshotRuntimeAccess& access,
            SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool BuildCatalogSection(json::EncodedText& output,
            SettingsSnapshotError& error) const noexcept;
        [[nodiscard]] bool Persist(
            const wchar_t* path, SettingsSnapshotError& error) const noexcept;
        [[nodiscard]] bool PersistToLocalCatalog(SettingsSnapshotError& error) const noexcept;

        [[nodiscard]] SettingsSnapshotTransactionStep
        BeginApplyCanonicalStaged(
            std::string_view canonical,
            const SettingsSnapshotRuntimeAccess& access) noexcept;
        [[nodiscard]] SettingsSnapshotTransactionStep
        BeginLoadCodeStaged(
            std::string_view code,
            const SettingsSnapshotRuntimeAccess& access) noexcept;
        [[nodiscard]] SettingsSnapshotTransactionStep ContinueStagedApply() noexcept;
        [[nodiscard]] bool HasStagedApply() const noexcept
        {
            return m_TransactionCoordinator.IsActive();
        }

    private:
        [[nodiscard]] SettingsSnapshotTransactionStep BeginDecodedStaged(
            const DecodedSettings& decoded,
            const SettingsSnapshotRuntimeAccess& access) noexcept;
        [[nodiscard]] SettingsSnapshotTransactionStep FinalizeStagedStep(
            SettingsSnapshotTransactionStep step,
            const SettingsSnapshotRuntimeAccess& access) noexcept;

        const SettingsSnapshotCatalogLocation m_CatalogLocation;
        SettingsSnapshotCode m_Code;
        json::EncodedText m_Canonical;
        SettingsSnapshotTransactionCoordinator m_TransactionCoordinator;
        SettingsSnapshotRuntimeAccess m_StagedAccess;
    };
}
