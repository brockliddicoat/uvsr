#pragma once

#include "settings_snapshot_decoder.h"
#include "settings_value.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr
{
    using SettingsSnapshotApplicationMode = UiSettingsApplicationRole;

    struct SettingsSnapshotTransactionEntry
    {
        SettingId id = SettingId::Invalid;
        std::string requestedValue;
    };
    using SettingsSnapshotValueReader = std::function<bool(
        SettingId id,
        std::string& value,
        std::string& error)>;

    using SettingsSnapshotValueWriter = std::function<bool(
        SettingId id,
        std::string_view value,
        std::string& error)>;

    using SettingsSnapshotValueValidator = std::function<bool(
        SettingId id,
        std::string_view value,
        std::string_view dependencySelector,
        std::string& error)>;

    enum class SettingsSnapshotSelectorTransition
    {
        Ready,
        Pending,
        Failed
    };

    using SettingsSnapshotSelectorDriver = std::function<
        SettingsSnapshotSelectorTransition(
            SettingId id,
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

    struct SettingsSnapshotStagedRuntimeAccess
    {
        SettingsSnapshotValueValidator validateValue;
        SettingsSnapshotValueReader readValue;
        SettingsSnapshotValueReader readRawValue;
        SettingsSnapshotValueWriter writeValue;
        SettingsSnapshotSelectorDriver driveSelector;
    };

    enum class SettingsSnapshotTransactionProgress
    {
        Pending,
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

    class SettingsSnapshotTransactionCoordinator
    {
    public:
        [[nodiscard]] SettingsSnapshotTransactionStep Begin(
            const std::vector<SettingsSnapshotTransactionEntry>& transaction,
            const SettingsSnapshotStagedRuntimeAccess& access);
        [[nodiscard]] SettingsSnapshotTransactionStep Advance(
            const SettingsSnapshotStagedRuntimeAccess& access);

        [[nodiscard]] bool IsActive() const noexcept;
        void Reset() noexcept;

    private:
        enum class Phase
        {
            Idle,
            ApplyScene,
            ApplyLight,
            ApplyMaterial,
            CaptureTarget,
            ApplyPrerequisites,
            ValidateTarget,
            ApplyValues,
            Verify,
            RollbackTargetValues,
            RollbackScene,
            RollbackLight,
            RollbackMaterial,
            RollbackSourceValues,
            RollbackVerify,
            Succeeded,
            Failed
        };

        struct Entry : SettingsSnapshotTransactionEntry
        {
            const UiSettingsCommandDefinition* definition = nullptr;
        };
        [[nodiscard]] std::size_t FindTransactionEntry(SettingId id) const;
        [[nodiscard]] bool BuildTransactionValueOrder(
            const std::vector<std::string>& desiredValues,
            const std::vector<std::string>& liveValues,
            bool reverseDependencies,
            std::vector<std::size_t>& order,
            std::string& error) const;
        [[nodiscard]] bool ValidateRequestedSpotPair(std::string& error) const;

        std::vector<Entry> m_Transaction;
        std::vector<std::string> m_SourceVisibleValues;
        std::vector<std::string> m_SourceRawValues;
        std::vector<std::string> m_TargetRawValues;
        std::vector<std::size_t> m_ValueOrder;
        std::vector<bool> m_PrerequisiteApplied;
        std::vector<bool> m_Mutated;
        Phase m_Phase = Phase::Idle;
        std::size_t m_Cursor = 0u;
        bool m_SelectorRequestIssued = false;
        bool m_MutationStarted = false;
        SettingsSnapshotTransactionResult m_Result;
    };

    [[nodiscard]] bool BuildSettingsSnapshotTransaction(
        const DecodedSettings& decoded,
        std::vector<SettingsSnapshotTransactionEntry>& transaction,
        std::string& error);

}
