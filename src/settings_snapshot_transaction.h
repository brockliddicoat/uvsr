#pragma once

#include "settings_snapshot_decoder.h"
#include "settings_value.h"
#include "array_view.h"

#include <cstddef>
#include <string_view>

namespace uvsr
{
    using SettingsSnapshotApplicationMode = UiSettingsApplicationRole;

    struct SettingsSnapshotTransactionEntry
    {
        SettingId id = SettingId::Invalid;
        std::string_view requestedValue;
    };
    using SettingsSnapshotValueReader = bool (*)(void* context,
        SettingId id,
        SettingsSnapshotText& value,
        SettingsSnapshotError& error) noexcept;

    using SettingsSnapshotValueWriter = bool (*)(void* context,
        SettingId id,
        std::string_view value,
        SettingsSnapshotError& error) noexcept;

    using SettingsSnapshotValueValidator = bool (*)(void* context,
        SettingId id,
        std::string_view value,
        std::string_view dependencySelector,
        SettingsSnapshotError& error) noexcept;

    enum class SettingsSnapshotSelectorTransition
    {
        Ready,
        Pending,
        Failed
    };

    using SettingsSnapshotSelectorDriver = SettingsSnapshotSelectorTransition (*)(void* context,
            SettingId id,
            std::string_view canonicalToken,
            bool begin,
            bool rollback,
            SettingsSnapshotError& error) noexcept;

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
        SettingsSnapshotError error;
    };

    // copied before Begin returns. context must outlive the active transaction.
    struct SettingsSnapshotStagedRuntimeAccess
    {
        void* context = nullptr;
        SettingsSnapshotValueValidator validateValue = nullptr;
        SettingsSnapshotValueReader readValue = nullptr;
        SettingsSnapshotValueReader readRawValue = nullptr;
        SettingsSnapshotValueWriter writeValue = nullptr;
        SettingsSnapshotSelectorDriver driveSelector = nullptr;
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
        std::string_view waitingFor;
    };

    class SettingsSnapshotTransactionCoordinator
    {
    public:
        SettingsSnapshotTransactionCoordinator() noexcept = default;
        ~SettingsSnapshotTransactionCoordinator() noexcept;
        SettingsSnapshotTransactionCoordinator(const SettingsSnapshotTransactionCoordinator&) = delete;
        SettingsSnapshotTransactionCoordinator& operator=(const SettingsSnapshotTransactionCoordinator&) = delete;

        // request views are copied before any runtime callback. active work is preserved on rejection.
        [[nodiscard]] SettingsSnapshotTransactionStep Begin(
            ArrayView<const SettingsSnapshotTransactionEntry> transaction,
            const SettingsSnapshotStagedRuntimeAccess& access) noexcept;
        [[nodiscard]] SettingsSnapshotTransactionStep Advance() noexcept;

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

        struct Entry;
        struct State;
        static constexpr std::size_t TransactionCapacity = AllSettingIds.size();
        [[nodiscard]] std::size_t FindTransactionEntry(SettingId id) const noexcept;
        [[nodiscard]] bool BuildTransactionValueOrder(
            ArrayView<const std::string_view> desiredValues,
            ArrayView<const std::string_view> liveValues,
            bool reverseDependencies,
            SettingsSnapshotError& error) noexcept;
        [[nodiscard]] bool ValidateRequestedSpotPair(SettingsSnapshotError& error) const noexcept;
        [[nodiscard]] SettingsSnapshotTransactionStep Step(
            SettingsSnapshotTransactionProgress progress, std::string_view waitingFor = {}) const noexcept;
        [[nodiscard]] SettingsSnapshotTransactionStep AdvanceState() noexcept;

        State* m_State = nullptr;
        bool m_InOperation = false;
        Phase m_Phase = Phase::Idle;
        SettingsSnapshotTransactionResult m_Result;
    };

    [[nodiscard]] bool BuildSettingsSnapshotTransaction(
        const DecodedSettings& decoded,
        ArrayView<SettingsSnapshotTransactionEntry> transaction,
        std::size_t& count, SettingsSnapshotError& error) noexcept;

#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
    void FailSettingsSnapshotTransactionAllocationAfter(std::size_t successfulAllocations) noexcept;
    void ClearSettingsSnapshotTransactionAllocationFailure() noexcept;
#endif
}
