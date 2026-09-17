#include "uvsr_settings_commands.h"

#include "settings_snapshot.h"
#include "settings_snapshot_decoder.h"
#include "settings_snapshot_legacy.h"
#include "settings_snapshot_persistence_win32.h"


#include <charconv>
#include <limits>
#include <system_error>
#include <utility>

namespace uvsr
{
    namespace
    {
        bool ReportSnapshotReadFailure(std::string_view name, SettingsSnapshotError& reason,
            SettingsSnapshotError& error) noexcept
        {
            if (reason.code != SettingsSnapshotErrorCode::InvalidInput)
            {
                error = std::move(reason);
                return false;
            }
            const std::string_view parts[] = {name, reason.MessageView()};
            json::EncodedText detail([](json::OutputWriter& writer, const void* context) noexcept {
                const auto* parts = static_cast<const std::string_view*>(context);
                return writer.Raw("cannot read setting '") &&
                    writer.Raw({parts[0].data(), parts[0].size()}) && writer.Raw("': ") &&
                    writer.Raw({parts[1].data(), parts[1].size()});
            }, parts);
            error = {};
            if (!detail.IsValid())
            {
                const auto& failure = detail.Failure();
                error.code = failure.code == json::ErrorCode::OutOfMemory
                    ? SettingsSnapshotErrorCode::OutOfMemory
                    : failure.code == json::ErrorCode::Capacity
                    ? SettingsSnapshotErrorCode::Capacity : SettingsSnapshotErrorCode::Format;
                error.message = failure.message;
                return false;
            }
            error.code = SettingsSnapshotErrorCode::InvalidInput;
            error.detail = std::move(detail);
            return false;
        }

        [[nodiscard]] bool TryParseSchemaVersion(
            std::string_view token,
            std::uint16_t& value) noexcept
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

    }

    SettingsSnapshotController::SettingsSnapshotController(SettingsSnapshotCatalogLocation location) noexcept
        : m_CatalogLocation(location), m_Code(BuildSettingsSnapshotCode({}))
    {
    }

    bool SettingsSnapshotController::Refresh(
        const SettingsSnapshotRuntimeAccess& access, SettingsSnapshotError& error) noexcept
    {
        error = {};
        DecodedSettings settings;
        for (const UiSettingsCommandDefinition& definition : UiSettingsCommandCatalog)
        {
            if (!IsSettingsSnapshotValue(definition)) continue;
            SettingsSnapshotText value;
            SettingsSnapshotError readError;
            if (!access.readValue || !access.readValue(access.context, definition.id, value, readError))
            {
                if (readError.code != SettingsSnapshotErrorCode::None)
                    return ReportSnapshotReadFailure(definition.name, readError, error);
                if (!value.Assign("<unavailable>", error)) return false;
            }
            if (!settings.Insert(definition.name, value.View(), error)) return false;
        }
        json::EncodedText canonical;
        if (!FormatCanonicalSettingsSnapshot(settings, canonical, error)) return false;
        const auto code = BuildSettingsSnapshotCode({canonical.Data(), canonical.Size()});
        m_Canonical = static_cast<json::EncodedText&&>(canonical);
        m_Code = code;
        return true;
    }

    bool SettingsSnapshotController::BuildCatalogSection(
        json::EncodedText& output, SettingsSnapshotError& error) const noexcept
    {
        return FormatSettingsSnapshotCatalogSection(Code(), Canonical(), output, error);
    }

    bool SettingsSnapshotController::Persist(const wchar_t* path, SettingsSnapshotError& error) const noexcept
    {
        return PersistSettingsSnapshotCatalog(path, Code(), Canonical(), error);
    }

    bool SettingsSnapshotController::PersistToLocalCatalog(SettingsSnapshotError& error) const noexcept
    {
        WindowsPath path;
        return GetSettingsSnapshotCatalogWritePath(m_CatalogLocation, path, error) && Persist(path.Data(), error);
    }

    namespace
    {
        SettingsSnapshotError PrefixSnapshotFailure(std::string_view prefix,
            const SettingsSnapshotError& cause) noexcept
        {
            return ComposeSettingsSnapshotError({prefix, cause.MessageView()}, cause.code,
                cause.nativeCode, cause.cleanupCode);
        }
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::FinalizeStagedStep(
        SettingsSnapshotTransactionStep step,
        const SettingsSnapshotRuntimeAccess& access) noexcept
    {
        if (step.progress == SettingsSnapshotTransactionProgress::Succeeded)
        {
            SettingsSnapshotError error;
            if (!Refresh(access, error))
            {
                // application finished; this failure does not claim a rollback.
                step.progress = SettingsSnapshotTransactionProgress::Failed;
                step.result.succeeded = false;
                step.result.failureStage = SettingsSnapshotTransactionFailureStage::Readback;
                step.result.error = PrefixSnapshotFailure("settings were applied but snapshot refresh failed: ", error);
            }
        }
        if (step.progress != SettingsSnapshotTransactionProgress::Pending)
            m_StagedAccess = {};
        return step;
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::BeginDecodedStaged(
        const DecodedSettings& decoded,
        const SettingsSnapshotRuntimeAccess& access) noexcept
    {
        SettingsSnapshotTransactionStep rejected;
        rejected.progress = SettingsSnapshotTransactionProgress::Failed;
        if (!access.sceneReady)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "settings.load requires a fully loaded scene", {}};
            return rejected;
        }

        SettingsSnapshotTransactionEntry transaction[AllSettingIds.size()];
        std::size_t count = 0;
        SettingsSnapshotError error;
        if (!BuildSettingsSnapshotTransaction(
                decoded, transaction, count, error))
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                PrefixSnapshotFailure("snapshot membership rejected: ", error);
            return rejected;
        }
        SettingsSnapshotStagedRuntimeAccess stagedAccess{
            access.context,
            access.validateValue,
            access.readValue,
            access.readRawValue,
            access.writeValue,
            access.driveSelector
        };
        m_StagedAccess = access;
        return FinalizeStagedStep(
            m_TransactionCoordinator.Begin({transaction, count}, stagedAccess),
            m_StagedAccess);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::ContinueStagedApply() noexcept
    {
        if (!HasStagedApply())
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.result.failureStage = SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0,
                "no staged settings transaction is active", {}};
            return rejected;
        }
        return FinalizeStagedStep(
            m_TransactionCoordinator.Advance(),
            m_StagedAccess);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::BeginApplyCanonicalStaged(
        std::string_view canonical,
        const SettingsSnapshotRuntimeAccess& access) noexcept
    {
        if (HasStagedApply())
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.progress = SettingsSnapshotTransactionProgress::Failed;
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error =
                {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "another staged settings transaction is active", {}};
            return rejected;
        }
        m_TransactionCoordinator.Reset();
        SettingsSnapshotTransactionStep rejected;
        rejected.progress = SettingsSnapshotTransactionProgress::Failed;
        DecodedSettings decoded;
        SettingsSnapshotError decodeError;
        json::EncodedText formatted;
        if (!ParseSettingsSnapshot(canonical, decoded, decodeError) ||
            !FormatCanonicalSettingsSnapshot(decoded, formatted, decodeError))
        {
            rejected.result.failureStage = SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error = PrefixSnapshotFailure("snapshot payload decode failed: ", decodeError);
            return rejected;
        }
        if (std::string_view(formatted.Data(), formatted.Size()) != canonical)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "snapshot payload is not complete canonical text", {}};
            return rejected;
        }
        return BeginDecodedStaged(decoded, access);
    }

    SettingsSnapshotTransactionStep
    SettingsSnapshotController::BeginLoadCodeStaged(
        std::string_view code,
        const SettingsSnapshotRuntimeAccess& access) noexcept
    {
        if (HasStagedApply())
        {
            SettingsSnapshotTransactionStep rejected;
            rejected.progress = SettingsSnapshotTransactionProgress::Failed;
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Configuration;
            rejected.result.error =
                {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "another staged settings transaction is active", {}};
            return rejected;
        }
        m_TransactionCoordinator.Reset();
        SettingsSnapshotTransactionStep rejected;
        rejected.progress = SettingsSnapshotTransactionProgress::Failed;
        SettingsSnapshotCodeError codeError;
        if (!ValidateSettingsSnapshotLoadCode(code.data(), code.size(), codeError))
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error = ComposeSettingsSnapshotError({codeError.text});
            return rejected;
        }

        DecodedSettings decoded;
        SettingsSnapshotCatalogPaths catalogs;
        SettingsSnapshotError decodeError;
        json::EncodedText formatted;
        if (!GetDefaultSettingsSnapshotCatalogPaths(code.substr(0u, 4u), m_CatalogLocation, catalogs, decodeError) ||
            !DecodeSettingsSnapshot(code, catalogs, decoded, decodeError) ||
            !FormatCanonicalSettingsSnapshot(decoded, formatted, decodeError))
        {
            rejected.result.failureStage = SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error = PrefixSnapshotFailure("snapshot decode failed: ", decodeError);
            return rejected;
        }

        const std::string_view version = code.substr(0u, 4u);
        const std::string_view canonical(formatted.Data(), formatted.Size());
        if (BuildSettingsSnapshotCode(canonical, version).View() != code)
        {
            rejected.result.failureStage =
                SettingsSnapshotTransactionFailureStage::Preflight;
            rejected.result.error =
                {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "snapshot payload is not in canonical command-name order", {}};
            return rejected;
        }
        SettingsSnapshotError error;
        std::uint16_t numericVersion = 0u;
        if (!TryParseSchemaVersion(version, numericVersion) ||
            !ApplyLegacySettingMigrations(numericVersion, decoded, error))
        {
            rejected.result.failureStage = SettingsSnapshotTransactionFailureStage::Preflight;
            if (error.code == SettingsSnapshotErrorCode::None)
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "snapshot schema version is invalid", {}};
            rejected.result.error = std::move(error);
            return rejected;
        }
        return BeginDecodedStaged(decoded, access);
    }

}
