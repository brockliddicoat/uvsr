#pragma once

#include "settings_snapshot_decoder.h"
#include "ui_settings_command_catalog.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr
{
    enum class SettingsSnapshotApplicationMode
    {
        Mutable,
        LiveDependent,
        Selector
    };

    struct SettingsSnapshotCatalogEntry
    {
        std::string name;
        SettingsSnapshotApplicationMode mode =
            SettingsSnapshotApplicationMode::Mutable;
    };

    struct SettingsSnapshotTransactionEntry
    {
        std::string name;
        std::string requestedValue;
        SettingsSnapshotApplicationMode mode =
            SettingsSnapshotApplicationMode::Mutable;
    };

    using SettingsSnapshotValueReader = std::function<bool(
        std::string_view name,
        std::string& value,
        std::string& error)>;

    using SettingsSnapshotValueWriter = std::function<bool(
        std::string_view name,
        std::string_view value,
        std::string& error)>;

    using SettingsSnapshotValueValidator = std::function<bool(
        std::string_view name,
        std::string_view value,
        std::string& error)>;

    enum class SettingsSnapshotSelectorTransition
    {
        Ready,
        Pending,
        RestartRequired,
        Failed
    };

    using SettingsSnapshotSelectorDriver = std::function<
        SettingsSnapshotSelectorTransition(
            std::string_view name,
            std::string_view canonicalToken,
            bool begin,
            bool rollback,
            std::string& error)>;

    enum class SettingsSnapshotTransactionFailureStage
    {
        None,
        Configuration,
        Preflight,
        Capture,
        Selector,
        Apply,
        Readback,
        Rollback
    };

    struct SettingsSnapshotTransactionResult
    {
        bool succeeded = false;
        bool rollbackAttempted = false;
        bool rollbackSucceeded = false;
        std::size_t changedValueCount = 0u;
        SettingsSnapshotTransactionFailureStage failureStage =
            SettingsSnapshotTransactionFailureStage::None;
        std::string error;
    };

    struct SettingsSnapshotRestartHandoff
    {
        std::vector<SettingsSnapshotTransactionEntry> transaction;
        std::vector<std::string> sourceValues;
        bool rollingBack = false;
        std::size_t changedValueCount = 0u;
        SettingsSnapshotTransactionFailureStage failureStage =
            SettingsSnapshotTransactionFailureStage::None;
        std::string failure;
    };

    using SettingsSnapshotRestartHandoffWriter = std::function<bool(
        const SettingsSnapshotRestartHandoff& handoff,
        std::string& error)>;

    struct SettingsSnapshotStagedRuntimeAccess
    {
        SettingsSnapshotValueValidator validateValue;
        SettingsSnapshotValueReader readValue;
        SettingsSnapshotValueWriter writeValue;
        SettingsSnapshotSelectorDriver driveSelector;
        SettingsSnapshotRestartHandoffWriter persistRestartHandoff;
    };

    enum class SettingsSnapshotTransactionProgress
    {
        Pending,
        RestartRequired,
        Succeeded,
        Failed
    };

    struct SettingsSnapshotTransactionStep
    {
        SettingsSnapshotTransactionProgress progress =
            SettingsSnapshotTransactionProgress::Failed;
        SettingsSnapshotTransactionResult result;
        std::string waitingFor;
    };

    [[nodiscard]] bool ValidateSettingsSnapshotSelectorToken(
        std::string_view name,
        std::string_view token,
        std::string& error);

    class SettingsSnapshotTransactionCoordinator
    {
    public:
        [[nodiscard]] SettingsSnapshotTransactionStep Begin(
            const std::vector<SettingsSnapshotTransactionEntry>& transaction,
            const SettingsSnapshotStagedRuntimeAccess& access);
        [[nodiscard]] SettingsSnapshotTransactionStep Resume(
            const SettingsSnapshotRestartHandoff& handoff,
            const SettingsSnapshotStagedRuntimeAccess& access);
        [[nodiscard]] SettingsSnapshotTransactionStep Advance(
            const SettingsSnapshotStagedRuntimeAccess& access);

        [[nodiscard]] bool IsActive() const noexcept;
        void Reset() noexcept;

    private:
        enum class Phase
        {
            Idle,
            ApplyAdapter,
            ApplyScene,
            ApplyLight,
            CaptureLight,
            ApplyMaterial,
            CaptureMaterial,
            ApplyDependents,
            ApplyRemaining,
            Verify,
            RollbackTargetDependents,
            RollbackAdapter,
            RollbackScene,
            RollbackLight,
            RollbackOriginalLightDependents,
            RollbackMaterial,
            RollbackOriginalMaterialDependents,
            RollbackRemaining,
            RollbackVerify,
            Succeeded,
            Failed
        };

        std::vector<SettingsSnapshotTransactionEntry> m_Transaction;
        std::vector<std::string> m_SourceValues;
        std::vector<std::string> m_TargetValues;
        std::vector<bool> m_TargetCaptured;
        std::vector<bool> m_Mutated;
        Phase m_Phase = Phase::Idle;
        std::size_t m_Cursor = 0u;
        bool m_SelectorRequestIssued = false;
        bool m_MutationStarted = false;
        SettingsSnapshotTransactionResult m_Result;
    };

    [[nodiscard]] SettingsSnapshotApplicationMode
    ResolveSettingsSnapshotApplicationMode(
        const UiSettingsCommandDefinition& definition) noexcept;

    [[nodiscard]] bool ValidateSettingsSnapshotCatalogValue(
        const UiSettingsCommandDefinition& definition,
        std::string_view value,
        std::string& error);

    [[nodiscard]] bool BuildSettingsSnapshotTransaction(
        const DecodedSettings& decoded,
        const std::vector<SettingsSnapshotCatalogEntry>& catalog,
        std::vector<SettingsSnapshotTransactionEntry>& transaction,
        std::string& error);

    [[nodiscard]] SettingsSnapshotTransactionResult
    ApplySettingsSnapshotTransaction(
        const std::vector<SettingsSnapshotTransactionEntry>& transaction,
        const SettingsSnapshotValueValidator& validateValue,
        const SettingsSnapshotValueReader& readValue,
        const SettingsSnapshotValueWriter& writeValue);
}
