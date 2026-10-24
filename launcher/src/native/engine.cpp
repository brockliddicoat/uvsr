#include "internal.h"

#include <shellapi.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <thread>

namespace fs = std::filesystem;

namespace uvsr::launcher
{
    namespace
    {
        constexpr std::wstring_view OldLauncherName = L"UVSR Launcher.exe";

        class Handle final
        {
        public:
            Handle() = default;
            explicit Handle(HANDLE value) : m_Value(value) {}
            ~Handle() { reset(); }
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            Handle(Handle&& other) noexcept : m_Value(other.release()) {}
            Handle& operator=(Handle&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    m_Value = other.release();
                }
                return *this;
            }
            [[nodiscard]] explicit operator bool() const noexcept
            {
                return m_Value != nullptr && m_Value != INVALID_HANDLE_VALUE;
            }
            [[nodiscard]] HANDLE get() const noexcept { return m_Value; }
            [[nodiscard]] HANDLE release() noexcept
            {
                const HANDLE value = m_Value;
                m_Value = nullptr;
                return value;
            }
            void reset(HANDLE value = nullptr) noexcept
            {
                if (*this)
                    CloseHandle(m_Value);
                m_Value = value;
            }

        private:
            HANDLE m_Value = nullptr;
        };

        struct LauncherPackageMarker
        {
            int schemaVersion = LauncherSchemaVersion;
            std::string productId{ ProductId };
            GUID installationId{};
            std::int64_t releaseSequence = 0;
            std::string version;
            std::string executableSha256;
            std::uint64_t executableSize = 0;
            std::string installedUtc;
        };

        struct LauncherTransaction
        {
            int schemaVersion = LauncherSchemaVersion;
            std::string productId{ ProductId };
            GUID installationId{};
            GUID transactionId{};
            std::string phase;
            std::optional<LauncherState> previousState;
            LauncherState candidateState;
            bool continueUvsrUpdate = false;
            std::string startedUtc;
        };

        struct RendererTransaction
        {
            int schemaVersion = SchemaVersion;
            GUID installationId{};
            GUID transactionId{};
            InstallerOperation operation = InstallerOperation::Install;
            std::string phase;
            std::optional<std::string> candidateVersionId;
            std::optional<InstallState> previousState;
            std::string startedUtc;
        };

        struct UninstallRecord
        {
            int schemaVersion = SchemaVersion;
            std::string productId{ ProductId };
            GUID installationId{};
            GUID transactionId{};
            std::string phase;
            std::optional<InstallState> previousState;
            std::string startedUtc;
        };

        struct ReleaseIdentity
        {
            std::int64_t sequence = 0;
            std::string version;
            std::string hash;
        };

        struct LauncherInspection
        {
            bool stateFileExists = false;
            bool stateMalformed = false;
            std::optional<LauncherState> recorded;
            std::optional<LauncherState> valid;
            std::optional<ReleaseIdentity> highest;
            std::string problem;
        };

        [[nodiscard]] std::string json_quote(std::string_view value)
        {
            return "\"" + detail::json_escape(value) + "\"";
        }

        [[nodiscard]] std::optional<InstallState> optional_install_state(
            const detail::Json& value)
        {
            if (value.kind == detail::Json::Kind::Null)
                return std::nullopt;
            return detail::parse_install_state(value);
        }

        [[nodiscard]] std::optional<LauncherState> optional_launcher_state(
            const detail::Json& value)
        {
            if (value.kind == detail::Json::Kind::Null)
                return std::nullopt;
            return detail::parse_launcher_state(value);
        }

        [[nodiscard]] LauncherPackageMarker parse_launcher_marker(
            std::string_view text)
        {
            const auto root = detail::parse_json(text);
            detail::require_exact_object(root, {
                "schemaVersion", "productId", "installationId",
                "releaseSequence", "version", "executableSha256",
                "executableSize", "installedUtc" },
                "The launcher package marker");
            LauncherPackageMarker marker;
            marker.schemaVersion = static_cast<int>(detail::integer_value(
                detail::member(root, "schemaVersion"), "schemaVersion"));
            marker.productId = detail::string_value(
                detail::member(root, "productId"), "productId");
            marker.installationId = parse_guid(detail::string_value(
                detail::member(root, "installationId"), "installationId"));
            marker.releaseSequence = detail::integer_value(
                detail::member(root, "releaseSequence"), "releaseSequence");
            marker.version = detail::string_value(
                detail::member(root, "version"), "version");
            marker.executableSha256 = detail::string_value(
                detail::member(root, "executableSha256"), "executableSha256");
            marker.executableSize = detail::unsigned_value(
                detail::member(root, "executableSize"), "executableSize");
            marker.installedUtc = detail::string_value(
                detail::member(root, "installedUtc"), "installedUtc");
            if (marker.schemaVersion != LauncherSchemaVersion ||
                marker.productId != ProductId ||
                guid_equal(marker.installationId, GUID{}) ||
                marker.releaseSequence < 1 ||
                marker.releaseSequence > MaximumReleaseSequence ||
                !is_stable_version(marker.version) ||
                !is_lower_hex(marker.executableSha256, 64u) ||
                marker.executableSize == 0 ||
                marker.executableSize > MaximumLauncherBytes ||
                marker.installedUtc.empty())
            {
                throw Error("The launcher package marker was invalid.");
            }
            return marker;
        }

        [[nodiscard]] std::string serialize_launcher_marker(
            const LauncherPackageMarker& marker)
        {
            return "{\n  \"schemaVersion\": " +
                std::to_string(marker.schemaVersion) +
                ",\n  \"productId\": " + json_quote(marker.productId) +
                ",\n  \"installationId\": " +
                json_quote(guid_string(marker.installationId)) +
                ",\n  \"releaseSequence\": " +
                std::to_string(marker.releaseSequence) +
                ",\n  \"version\": " + json_quote(marker.version) +
                ",\n  \"executableSha256\": " +
                json_quote(marker.executableSha256) +
                ",\n  \"executableSize\": " +
                std::to_string(marker.executableSize) +
                ",\n  \"installedUtc\": " + json_quote(marker.installedUtc) + "\n}\n";
        }

        void validate_launcher_transaction(
            const LauncherTransaction& transaction,
            const GUID& installationId)
        {
            if (transaction.schemaVersion != LauncherSchemaVersion ||
                transaction.productId != ProductId ||
                !guid_equal(transaction.installationId, installationId) ||
                guid_equal(transaction.transactionId, GUID{}) ||
                (transaction.phase != "prepared" &&
                    transaction.phase != "state-activated" &&
                    transaction.phase != "shell-committed" &&
                    transaction.phase != "awaiting-continuation" &&
                    transaction.phase != "continuation-complete") ||
                transaction.startedUtc.empty())
            {
                throw Error("The interrupted launcher update record was invalid.");
            }
            detail::validate_launcher_state(
                transaction.candidateState, installationId);
            if (transaction.previousState)
                detail::validate_launcher_state(*transaction.previousState,
                    installationId);
        }

        [[nodiscard]] LauncherTransaction parse_launcher_transaction(
            std::string_view text,
            const GUID& installationId)
        {
            const auto root = detail::parse_json(text);
            detail::require_exact_object(root, {
                "schemaVersion", "productId", "installationId",
                "transactionId", "phase", "previousState", "candidateState",
                "continueUvsrUpdate", "startedUtc" },
                "The launcher activation record");
            LauncherTransaction result;
            result.schemaVersion = static_cast<int>(detail::integer_value(
                detail::member(root, "schemaVersion"), "schemaVersion"));
            result.productId = detail::string_value(
                detail::member(root, "productId"), "productId");
            result.installationId = parse_guid(detail::string_value(
                detail::member(root, "installationId"), "installationId"));
            result.transactionId = parse_guid(detail::string_value(
                detail::member(root, "transactionId"), "transactionId"));
            result.phase = detail::string_value(
                detail::member(root, "phase"), "phase");
            result.previousState = optional_launcher_state(
                detail::member(root, "previousState"));
            result.candidateState = detail::parse_launcher_state(
                detail::member(root, "candidateState"));
            result.continueUvsrUpdate = detail::boolean_value(
                detail::member(root, "continueUvsrUpdate"),
                "continueUvsrUpdate");
            result.startedUtc = detail::string_value(
                detail::member(root, "startedUtc"), "startedUtc");
            validate_launcher_transaction(result, installationId);
            return result;
        }

        [[nodiscard]] std::string serialize_launcher_transaction(
            const LauncherTransaction& transaction)
        {
            validate_launcher_transaction(transaction, transaction.installationId);
            return "{\n  \"schemaVersion\": " +
                std::to_string(transaction.schemaVersion) +
                ",\n  \"productId\": " + json_quote(transaction.productId) +
                ",\n  \"installationId\": " +
                json_quote(guid_string(transaction.installationId)) +
                ",\n  \"transactionId\": " +
                json_quote(guid_string(transaction.transactionId)) +
                ",\n  \"phase\": " + json_quote(transaction.phase) +
                ",\n  \"previousState\": " +
                detail::serialize_launcher_state_value(
                    transaction.previousState, 2) +
                ",\n  \"candidateState\": " +
                detail::serialize_launcher_state_value(
                    transaction.candidateState, 2) +
                ",\n  \"continueUvsrUpdate\": " +
                (transaction.continueUvsrUpdate ? "true" : "false") +
                ",\n  \"startedUtc\": " + json_quote(transaction.startedUtc) + "\n}\n";
        }

        [[nodiscard]] RendererTransaction parse_renderer_transaction(
            std::string_view text,
            const GUID& installationId)
        {
            const auto root = detail::parse_json(text);
            detail::require_exact_object(root, {
                "schemaVersion", "installationId", "transactionId",
                "operation", "phase", "candidateVersionId",
                "previousState", "startedUtc" },
                "The renderer transaction record");
            RendererTransaction result;
            result.schemaVersion = static_cast<int>(detail::integer_value(
                detail::member(root, "schemaVersion"), "schemaVersion"));
            result.installationId = parse_guid(detail::string_value(
                detail::member(root, "installationId"), "installationId"));
            result.transactionId = parse_guid(detail::string_value(
                detail::member(root, "transactionId"), "transactionId"));
            const auto operation = detail::integer_value(
                detail::member(root, "operation"), "operation");
            if (operation < 0 || operation > 3)
                throw Error("The renderer transaction operation was invalid.");
            result.operation = static_cast<InstallerOperation>(operation);
            result.phase = detail::string_value(
                detail::member(root, "phase"), "phase");
            result.candidateVersionId = detail::nullable_string(
                detail::member(root, "candidateVersionId"),
                "candidateVersionId");
            result.previousState = optional_install_state(
                detail::member(root, "previousState"));
            result.startedUtc = detail::string_value(
                detail::member(root, "startedUtc"), "startedUtc");
            if (result.schemaVersion != SchemaVersion ||
                !guid_equal(result.installationId, installationId) ||
                guid_equal(result.transactionId, GUID{}) ||
                (result.phase != "download" && result.phase != "package" &&
                    result.phase != "activate" &&
                    result.phase != "shell-activation" &&
                    result.phase != "shell-update" &&
                    result.phase != "uninstall-pending") ||
                (result.candidateVersionId &&
                    !is_version_id(*result.candidateVersionId)) ||
                result.startedUtc.empty())
            {
                throw Error(
                    "An incomplete UVSR Launcher transaction had an invalid ownership record.");
            }
            if (result.previousState)
                detail::validate_install_state(*result.previousState,
                    installationId);
            return result;
        }

        [[nodiscard]] std::string serialize_renderer_transaction(
            const RendererTransaction& transaction)
        {
            return "{\n  \"schemaVersion\": " +
                std::to_string(transaction.schemaVersion) +
                ",\n  \"installationId\": " +
                json_quote(guid_string(transaction.installationId)) +
                ",\n  \"transactionId\": " +
                json_quote(guid_string(transaction.transactionId)) +
                ",\n  \"operation\": " +
                std::to_string(static_cast<int>(transaction.operation)) +
                ",\n  \"phase\": " + json_quote(transaction.phase) +
                ",\n  \"candidateVersionId\": " +
                (transaction.candidateVersionId
                    ? json_quote(*transaction.candidateVersionId) : "null") +
                ",\n  \"previousState\": " +
                detail::serialize_install_state_value(
                    transaction.previousState, 2) +
                ",\n  \"startedUtc\": " + json_quote(transaction.startedUtc) + "\n}\n";
        }

        [[nodiscard]] UninstallRecord parse_uninstall_record(
            std::string_view text)
        {
            const auto root = detail::parse_json(text);
            detail::require_exact_object(root, {
                "schemaVersion", "productId", "installationId",
                "transactionId", "phase", "previousState", "startedUtc" },
                "The uninstall record");
            UninstallRecord result;
            result.schemaVersion = static_cast<int>(detail::integer_value(
                detail::member(root, "schemaVersion"), "schemaVersion"));
            result.productId = detail::string_value(
                detail::member(root, "productId"), "productId");
            result.installationId = parse_guid(detail::string_value(
                detail::member(root, "installationId"), "installationId"));
            result.transactionId = parse_guid(detail::string_value(
                detail::member(root, "transactionId"), "transactionId"));
            result.phase = detail::string_value(
                detail::member(root, "phase"), "phase");
            result.previousState = optional_install_state(
                detail::member(root, "previousState"));
            result.startedUtc = detail::string_value(
                detail::member(root, "startedUtc"), "startedUtc");
            if (result.schemaVersion != SchemaVersion ||
                result.productId != ProductId ||
                guid_equal(result.installationId, GUID{}) ||
                guid_equal(result.transactionId, GUID{}) ||
                (result.phase != "prepared" &&
                    result.phase != "removing-shell" &&
                    result.phase != "shell-removed" &&
                    result.phase != "program-moved" &&
                    result.phase != "roots-moved" &&
                    result.phase != "deleting") ||
                result.startedUtc.empty())
            {
                throw Error(
                    "The interrupted UVSR uninstall record was invalid. No files were changed.");
            }
            if (result.previousState)
                detail::validate_install_state(
                    *result.previousState, result.installationId);
            return result;
        }

        [[nodiscard]] std::string serialize_uninstall_record(
            const UninstallRecord& record)
        {
            return "{\n  \"schemaVersion\": " +
                std::to_string(record.schemaVersion) +
                ",\n  \"productId\": " + json_quote(record.productId) +
                ",\n  \"installationId\": " +
                json_quote(guid_string(record.installationId)) +
                ",\n  \"transactionId\": " +
                json_quote(guid_string(record.transactionId)) +
                ",\n  \"phase\": " + json_quote(record.phase) +
                ",\n  \"previousState\": " +
                detail::serialize_install_state_value(record.previousState, 2) +
                ",\n  \"startedUtc\": " + json_quote(record.startedUtc) + "\n}\n";
        }

        [[nodiscard]] std::uint64_t checked_file_size(const fs::path& path)
        {
            std::error_code error;
            const auto value = fs::file_size(path, error);
            if (error)
                throw Error("Windows could not inspect a required UVSR file.");
            return value;
        }

        [[nodiscard]] bool same_identity(
            const ReleaseIdentity& left,
            const ReleaseIdentity& right) noexcept
        {
            return left.sequence == right.sequence &&
                left.version == right.version && left.hash == right.hash;
        }

        [[nodiscard]] ReleaseIdentity identity(const LauncherState& state)
        {
            return { state.releaseSequence, state.version,
                state.executableSha256 };
        }

        void validate_launcher_package(
            const InstallerPaths& paths,
            const LauncherState& state)
        {
            detail::validate_launcher_state(state, state.installationId);
            const fs::path root = paths.launcher_version_root(
                state.executableSha256);
            reject_reparse_chain(root, "installed launcher package");
            const LauncherPackageMarker marker = parse_launcher_marker(
                detail::read_text(paths.launcher_package_marker(
                    state.executableSha256), MaximumStateBytes));
            const fs::path executable = paths.launcher_executable(
                state.executableSha256);
            if (!guid_equal(marker.installationId, state.installationId) ||
                marker.releaseSequence != state.releaseSequence ||
                marker.version != state.version ||
                marker.executableSha256 != state.executableSha256 ||
                checked_file_size(executable) != marker.executableSize ||
                sha256(executable) != state.executableSha256)
            {
                throw Error(
                    "The installed UVSR Launcher package was incomplete or changed.");
            }
            std::vector<std::wstring> entries;
            for (const auto& entry : fs::directory_iterator(root))
                entries.push_back(entry.path().filename().native());
            std::sort(entries.begin(), entries.end(), [](const auto& left,
                const auto& right)
                { return _wcsicmp(left.c_str(), right.c_str()) < 0; });
            if (entries.size() != 2u ||
                _wcsicmp(entries[0].c_str(),
                    L".uvsr-launcher-package.json") != 0 ||
                _wcsicmp(entries[1].c_str(), LauncherExecutableName.data()) != 0)
            {
                throw Error(
                    "The installed UVSR Launcher package contained unexpected files.");
            }
        }

        void migrate_old_launcher_package(
            const InstallerPaths& paths,
            const GUID& installationId,
            const fs::path& directory,
            std::string_view hash,
            detail::InstallLog* log)
        {
            const fs::path canonical = directory / LauncherExecutableName;
            const fs::path old = directory / OldLauncherName;
            if (fs::exists(canonical) || !fs::exists(old))
                return;
            const LauncherPackageMarker marker = parse_launcher_marker(
                detail::read_text(paths.launcher_package_marker(hash),
                    MaximumStateBytes));
            if (!guid_equal(marker.installationId, installationId) ||
                marker.executableSha256 != hash ||
                marker.executableSize != checked_file_size(old) ||
                sha256(old) != hash)
            {
                throw Error(
                    "The known old launcher filename did not match its owned package.");
            }
            std::vector<fs::path> entries;
            for (const auto& entry : fs::directory_iterator(directory))
                entries.push_back(entry.path().filename());
            if (entries.size() != 2u)
                throw Error(
                    "The known old launcher package contained unexpected files.");
            fs::rename(old, canonical);
            if (log)
                log->write("Migrated the exact known old launcher filename to uvsr-launcher.exe.");
        }

        [[nodiscard]] LauncherInspection inspect_launcher(
            const InstallerPaths& paths,
            const GUID& installationId,
            bool defaultDesktopShortcut,
            detail::InstallLog* log = nullptr)
        {
            LauncherInspection result;
            std::map<std::int64_t, ReleaseIdentity> identities;
            auto addIdentity = [&](const ReleaseIdentity& value)
            {
                const auto [iterator, inserted] = identities.emplace(
                    value.sequence, value);
                if (!inserted && !same_identity(iterator->second, value))
                    throw Error(
                        "UVSR Launcher found conflicting files for the same release sequence.");
            };
            result.stateFileExists = fs::exists(paths.launcher_state_file());
            if (result.stateFileExists)
            {
                try
                {
                    result.recorded = detail::parse_launcher_state(
                        detail::read_text(paths.launcher_state_file(),
                            MaximumStateBytes));
                    detail::validate_launcher_state(
                        *result.recorded, installationId);
                    addIdentity(identity(*result.recorded));
                    validate_launcher_package(paths, *result.recorded);
                    result.valid = result.recorded;
                }
                catch (const std::exception& error)
                {
                    result.stateMalformed = true;
                    result.problem = error.what();
                }
            }
            if (fs::exists(paths.launcher_versions_directory()))
            {
                for (const auto& entry :
                    fs::directory_iterator(paths.launcher_versions_directory()))
                {
                    if (!entry.is_directory())
                        continue;
                    const std::string hash = narrow(entry.path().filename().native());
                    if (!is_lower_hex(hash, 64u))
                        continue;
                    try
                    {
                        migrate_old_launcher_package(paths, installationId,
                            entry.path(), hash, log);
                        const LauncherPackageMarker marker = parse_launcher_marker(
                            detail::read_text(paths.launcher_package_marker(hash),
                                MaximumStateBytes));
                        LauncherState candidate;
                        candidate.installationId = marker.installationId;
                        candidate.releaseSequence = marker.releaseSequence;
                        candidate.version = marker.version;
                        candidate.executableSha256 = marker.executableSha256;
                        candidate.desktopShortcut = result.recorded
                            ? result.recorded->desktopShortcut
                            : defaultDesktopShortcut;
                        candidate.installedUtc = marker.installedUtc;
                        detail::validate_launcher_state(candidate, installationId);
                        validate_launcher_package(paths, candidate);
                        addIdentity(identity(candidate));
                        if (!result.valid ||
                            candidate.releaseSequence >
                                result.valid->releaseSequence ||
                            (candidate.releaseSequence ==
                                    result.valid->releaseSequence &&
                                candidate.installedUtc >
                                    result.valid->installedUtc))
                        {
                            result.valid = candidate;
                        }
                    }
                    catch (const std::exception& error)
                    {
                        if (log)
                            log->write(std::string(
                                "Preserved an unverified launcher recovery package: ") +
                                error.what());
                    }
                }
            }
            if (!identities.empty())
                result.highest = identities.rbegin()->second;
            return result;
        }

        [[nodiscard]] LauncherState stage_launcher(
            const InstallerPaths& paths,
            const fs::path& source,
            const GUID& installationId,
            std::int64_t sequence,
            std::string_view version,
            bool desktopShortcut,
            detail::InstallLog& log,
            std::optional<std::uint64_t> expectedSize = std::nullopt,
            std::optional<std::string_view> expectedHash = std::nullopt,
            std::optional<std::string_view> sourceCommit = std::nullopt)
        {
            validate_pe_x64(source);
            validate_launcher_metadata(source, version, sourceCommit);
            const std::uint64_t size = checked_file_size(source);
            if (size == 0 || size > MaximumLauncherBytes ||
                (expectedSize && size != *expectedSize))
                throw Error("The UVSR Launcher did not match its published size.");
            const std::string hash = sha256(source);
            if (expectedHash && hash != *expectedHash)
                throw Error("The UVSR Launcher did not match its published SHA-256.");
            LauncherState state;
            state.installationId = installationId;
            state.releaseSequence = sequence;
            state.version = std::string(version);
            state.executableSha256 = hash;
            state.desktopShortcut = desktopShortcut;
            state.installedUtc = detail::now_iso8601();
            detail::validate_launcher_state(state, installationId);

            const fs::path finalRoot = paths.launcher_version_root(hash);
            if (fs::exists(finalRoot))
            {
                try
                {
                    validate_launcher_package(paths, state);
                    const auto marker = parse_launcher_marker(detail::read_text(
                        paths.launcher_package_marker(hash), MaximumStateBytes));
                    state.installedUtc = marker.installedUtc;
                    return state;
                }
                catch (const std::exception& error)
                {
                    log.write(std::string(
                        "The existing launcher package needs repair and will be preserved: ") +
                        error.what());
                }
            }

            fs::create_directories(paths.launcher_staging_directory());
            fs::create_directories(paths.launcher_versions_directory());
            reject_reparse_chain(paths.launcher_staging_directory(),
                "launcher staging directory");
            const GUID stageId = detail::new_guid();
            const fs::path stage = paths.launcher_staging_directory() /
                widen(guid_string(stageId));
            fs::create_directories(stage);
            try
            {
                detail::copy_file_verified(source,
                    stage / LauncherExecutableName, hash);
                LauncherPackageMarker marker;
                marker.installationId = installationId;
                marker.releaseSequence = sequence;
                marker.version = std::string(version);
                marker.executableSha256 = hash;
                marker.executableSize = size;
                marker.installedUtc = state.installedUtc;
                detail::write_atomic(stage / L".uvsr-launcher-package.json",
                    serialize_launcher_marker(marker));
                std::optional<fs::path> preserved;
                if (fs::exists(finalRoot))
                {
                    preserved = paths.launcher_versions_directory() /
                        widen(hash + ".preserved-" +
                            guid_string(detail::new_guid()));
                    fs::rename(finalRoot, *preserved);
                    log.write("Preserved a damaged launcher package at " +
                        narrow(preserved->native()) + ".");
                }
                try
                {
                    fs::rename(stage, finalRoot);
                }
                catch (...)
                {
                    if (preserved && fs::exists(*preserved) &&
                        !fs::exists(finalRoot))
                        fs::rename(*preserved, finalRoot);
                    throw;
                }
                validate_launcher_package(paths, state);
                log.write("Staged UVSR Launcher " + state.version + " (" +
                    state.executableSha256 + ").");
                return state;
            }
            catch (...)
            {
                if (fs::exists(stage))
                    delete_owned_tree(stage, paths.launcher_staging_directory());
                throw;
            }
        }

        void write_launcher_state(
            const InstallerPaths& paths,
            const LauncherState& state)
        {
            detail::write_atomic(paths.launcher_state_file(),
                detail::serialize_launcher_state(state));
        }

        [[nodiscard]] GUID activate_launcher(
            const InstallerPaths& paths,
            const GUID& installationId,
            LauncherState candidate,
            const std::optional<InstallState>& renderer,
            bool continueUvsrUpdate,
            detail::ShellIntegration& shell,
            detail::InstallLog& log)
        {
            detail::validate_launcher_state(candidate, installationId);
            validate_launcher_package(paths, candidate);
            const auto inspection = inspect_launcher(paths, installationId,
                candidate.desktopShortcut, &log);
            if (inspection.highest &&
                candidate.releaseSequence < inspection.highest->sequence)
                throw Error("UVSR Launcher refused to activate an older release.");
            if (inspection.highest &&
                candidate.releaseSequence == inspection.highest->sequence &&
                !same_identity(identity(candidate), *inspection.highest))
            {
                throw Error(
                    "UVSR Launcher detected conflicting files for the same release sequence.");
            }
            if (fs::exists(paths.launcher_transaction_file()))
            {
                LauncherTransaction pending = parse_launcher_transaction(
                    detail::read_text(paths.launcher_transaction_file(),
                        MaximumStateBytes), installationId);
                if (pending.phase != "awaiting-continuation" ||
                    !pending.continueUvsrUpdate ||
                    !same_identity(identity(pending.candidateState),
                        identity(candidate)))
                {
                    throw Error(
                        "An earlier UVSR Launcher change must finish recovery before another launcher change can begin.");
                }
                pending.candidateState.desktopShortcut =
                    candidate.desktopShortcut;
                detail::write_atomic(paths.launcher_transaction_file(),
                    serialize_launcher_transaction(pending));
                write_launcher_state(paths, pending.candidateState);
                shell.apply(installationId, renderer,
                    pending.candidateState, log, pending.transactionId);
                return pending.transactionId;
            }

            LauncherTransaction transaction;
            transaction.installationId = installationId;
            transaction.transactionId = detail::new_guid();
            transaction.phase = "prepared";
            transaction.previousState = inspection.valid;
            transaction.candidateState = candidate;
            transaction.continueUvsrUpdate = continueUvsrUpdate;
            transaction.startedUtc = detail::now_iso8601();
            detail::write_atomic(paths.launcher_transaction_file(),
                serialize_launcher_transaction(transaction));
            try
            {
                write_launcher_state(paths, candidate);
                transaction.phase = "state-activated";
                detail::write_atomic(paths.launcher_transaction_file(),
                    serialize_launcher_transaction(transaction));
                shell.apply(installationId, renderer, candidate, log,
                    transaction.transactionId);
                transaction.phase = "shell-committed";
                detail::write_atomic(paths.launcher_transaction_file(),
                    serialize_launcher_transaction(transaction));
                if (continueUvsrUpdate)
                {
                    transaction.phase = "awaiting-continuation";
                    detail::write_atomic(paths.launcher_transaction_file(),
                        serialize_launcher_transaction(transaction));
                }
                else
                    fs::remove(paths.launcher_transaction_file());
            }
            catch (...)
            {
                if (transaction.phase == "prepared")
                    fs::remove(paths.launcher_transaction_file());
                throw;
            }
            return transaction.transactionId;
        }

        void recover_launcher(
            const InstallerPaths& paths,
            const GUID& installationId,
            const std::optional<InstallState>& renderer,
            detail::ShellIntegration& shell,
            detail::InstallLog& log)
        {
            if (!fs::exists(paths.launcher_transaction_file()))
                return;
            LauncherTransaction transaction = parse_launcher_transaction(
                detail::read_text(paths.launcher_transaction_file(),
                    MaximumStateBytes), installationId);
            bool candidateValid = false;
            bool previousValid = !transaction.previousState;
            try
            {
                validate_launcher_package(paths, transaction.candidateState);
                candidateValid = true;
            }
            catch (const std::exception& error)
            {
                log.write(std::string(
                    "The candidate launcher recovery package is invalid: ") +
                    error.what());
            }
            if (transaction.previousState)
            {
                try
                {
                    validate_launcher_package(paths, *transaction.previousState);
                    previousValid = true;
                }
                catch (const std::exception& error)
                {
                    log.write(std::string(
                        "The previous launcher recovery package is invalid: ") +
                        error.what());
                }
            }
            const bool rollBack = transaction.phase == "prepared" && previousValid;
            if (rollBack || (!candidateValid && previousValid))
            {
                if (transaction.previousState)
                {
                    write_launcher_state(paths, *transaction.previousState);
                    if (transaction.phase != "prepared")
                        shell.apply(installationId, renderer,
                            *transaction.previousState, log,
                            transaction.transactionId);
                }
                else
                    fs::remove(paths.launcher_state_file());
                fs::remove(paths.launcher_transaction_file());
                log.write("Rolled back an interrupted UVSR Launcher activation safely.");
                return;
            }
            if (!candidateValid)
                throw Error(
                    "Both launcher packages from an interrupted update were damaged. No package was trusted.");
            write_launcher_state(paths, transaction.candidateState);
            shell.apply(installationId, renderer, transaction.candidateState,
                log, transaction.transactionId);
            if (transaction.continueUvsrUpdate &&
                transaction.phase != "continuation-complete")
            {
                transaction.phase = "awaiting-continuation";
                detail::write_atomic(paths.launcher_transaction_file(),
                    serialize_launcher_transaction(transaction));
            }
            else
                fs::remove(paths.launcher_transaction_file());
            log.write("Finished an interrupted UVSR Launcher activation safely.");
        }

        [[nodiscard]] int run_health_process(
            const fs::path& executable,
            std::int64_t sequence,
            std::string_view version,
            const Cancellation& cancellation)
        {
            std::wstring command = L"\"" + executable.native() +
                L"\" --launcher-health-check " +
                std::to_wstring(sequence) + L" " + widen(version);
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(executable.c_str(), command.data(), nullptr,
                    nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                    executable.parent_path().c_str(), &startup, &process))
            {
                detail::throw_last_error(
                    "Windows could not start the launcher safety check");
            }
            Handle processHandle(process.hProcess);
            Handle threadHandle(process.hThread);
            const ULONGLONG deadline = GetTickCount64() + 15000u;
            for (;;)
            {
                if (cancellation.requested())
                {
                    TerminateProcess(processHandle.get(), ERROR_CANCELLED);
                    throw Cancelled();
                }
                const DWORD wait = WaitForSingleObject(processHandle.get(), 100u);
                if (wait == WAIT_OBJECT_0)
                    break;
                if (wait == WAIT_FAILED)
                    detail::throw_last_error(
                        "Windows could not wait for the launcher safety check");
                if (GetTickCount64() >= deadline)
                {
                    TerminateProcess(processHandle.get(), ERROR_TIMEOUT);
                    throw Error(
                        "The downloaded UVSR Launcher did not respond to its safety check.");
                }
            }
            DWORD exitCode = 0;
            if (!GetExitCodeProcess(processHandle.get(), &exitCode))
                detail::throw_last_error(
                    "Windows could not read the launcher safety check result");
            return static_cast<int>(exitCode);
        }

        void launch_executable(
            const fs::path& executable,
            std::wstring_view arguments = {})
        {
            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
            info.lpFile = executable.c_str();
            const std::wstring parameters(arguments);
            info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
            const std::wstring directory = executable.parent_path().native();
            info.lpDirectory = directory.c_str();
            info.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&info))
                detail::throw_last_error("Windows could not open UVSR Launcher");
        }

        [[nodiscard]] std::string create_version_id(std::string_view commit)
        {
            SYSTEMTIME time{};
            GetSystemTime(&time);
            char timestamp[20]{};
            sprintf_s(timestamp, "%04u%02u%02u%02u%02u%02u",
                time.wYear, time.wMonth, time.wDay, time.wHour,
                time.wMinute, time.wSecond);
            std::string id = std::string(commit) + "-" + timestamp + "-" +
                guid_string(detail::new_guid());
            id.resize(64u);
            if (!is_version_id(id))
                throw Error("Windows could not create a renderer version identity.");
            return id;
        }
    }

    struct InstallerEngine::Impl
    {
        explicit Impl(InstallerPaths value)
            : paths(std::move(value)), ownership(paths), shell(paths) {}

        InstallerPaths paths;
        detail::Ownership ownership;
        detail::ShellIntegration shell;
        DownloadManager downloads;
    };

    InstallerEngine::InstallerEngine(InstallerPaths paths)
        : m_Impl(std::make_unique<Impl>(std::move(paths))) {}

    InstallerEngine::~InstallerEngine() = default;

    namespace
    {
        void write_install_state(
            const InstallerPaths& paths,
            const InstallState& state)
        {
            detail::write_atomic(paths.state_file(),
                detail::serialize_install_state(state));
        }

        void validate_installed_package(
            const InstallerPaths& paths,
            const GUID& installationId,
            const InstallState& state)
        {
            detail::validate_install_state(state, installationId);
            const fs::path root = paths.version_root(state.activeVersionId);
            const PackageManifest manifest = read_package_manifest(root);
            if (manifest.releaseSequence != state.releaseSequence ||
                manifest.sourceCommit != state.commit ||
                manifest.settingsHash != state.settingsHash ||
                manifest.engineVersion != state.engineVersion ||
                manifest.executableSha256 != state.executableSha256)
            {
                throw Error(
                    "The active UVSR package record did not match the installed state.");
            }
            validate_renderer_package(root, manifest);
        }

        [[nodiscard]] std::optional<InstallState> read_state_if_present(
            const InstallerPaths& paths,
            const GUID& installationId)
        {
            if (!fs::exists(paths.state_file()))
                return std::nullopt;
            InstallState state = detail::parse_install_state(detail::read_text(
                paths.state_file(), MaximumStateBytes));
            detail::validate_install_state(state, installationId);
            return state;
        }

        [[nodiscard]] std::optional<InstallState> try_valid_state(
            const InstallerPaths& paths,
            const GUID& installationId)
        {
            try
            {
                auto state = read_state_if_present(paths, installationId);
                if (state)
                    validate_installed_package(paths, installationId, *state);
                return state;
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        }

        [[nodiscard]] bool confirmed_stopped(
            const ExactProcessInspection& inspection) noexcept
        {
            return inspection.state == ProcessState::NotRunning;
        }

        void cleanup_transaction_stage(
            const InstallerPaths& paths,
            const GUID& transactionId,
            detail::InstallLog& log)
        {
            const fs::path path = paths.staging_directory() /
                widen(guid_string(transactionId));
            if (!fs::exists(path))
                return;
            delete_owned_tree(path, paths.staging_directory());
            log.write("Removed installer owned transaction staging files.");
        }

        void remove_inactive_renderer(
            const InstallerPaths& paths,
            const fs::path& directory,
            detail::InstallLog& log)
        {
            if (!fs::exists(directory))
                return;
            const auto processes = inspect_managed_processes(
                EngineExecutableName, directory);
            if (!confirmed_stopped(processes))
            {
                log.write(
                    "A previous UVSR version is still running or could not be verified. Its package was retained.");
                return;
            }
            delete_owned_tree(directory, paths.versions_directory());
        }

        void sweep_renderer_versions(
            const InstallerPaths& paths,
            const InstallState& active,
            detail::InstallLog& log)
        {
            if (!fs::exists(paths.versions_directory()))
                return;
            reject_reparse_chain(paths.versions_directory(),
                "UVSR managed versions directory");
            for (const auto& entry : fs::directory_iterator(
                    paths.versions_directory()))
            {
                if (!entry.is_directory())
                    continue;
                const std::string id = narrow(entry.path().filename().native());
                if (id == active.activeVersionId || !is_version_id(id))
                    continue;
                remove_inactive_renderer(paths, entry.path(), log);
            }
        }

        void sweep_launcher_packages(
            const InstallerPaths& paths,
            const LauncherState& active,
            detail::InstallLog& log)
        {
            if (!fs::exists(paths.launcher_versions_directory()))
                return;
            struct Package
            {
                fs::path directory;
                fs::path executable;
                LauncherPackageMarker marker;
            };
            std::vector<Package> packages;
            for (const auto& entry : fs::directory_iterator(
                    paths.launcher_versions_directory()))
            {
                if (!entry.is_directory())
                    continue;
                const std::string hash = narrow(entry.path().filename().native());
                if (hash == active.executableSha256 ||
                    !is_lower_hex(hash, 64u))
                    continue;
                try
                {
                    const auto marker = parse_launcher_marker(detail::read_text(
                        paths.launcher_package_marker(hash), MaximumStateBytes));
                    LauncherState state;
                    state.installationId = marker.installationId;
                    state.releaseSequence = marker.releaseSequence;
                    state.version = marker.version;
                    state.executableSha256 = marker.executableSha256;
                    state.desktopShortcut = active.desktopShortcut;
                    state.installedUtc = marker.installedUtc;
                    detail::validate_launcher_state(state, active.installationId);
                    validate_launcher_package(paths, state);
                    packages.push_back({ entry.path(),
                        paths.launcher_executable(hash), marker });
                }
                catch (const std::exception& error)
                {
                    log.write(std::string(
                        "Preserved an unverified launcher package during cleanup: ") +
                        error.what());
                }
            }
            std::sort(packages.begin(), packages.end(),
                [](const Package& left, const Package& right)
                {
                    if (left.marker.releaseSequence !=
                            right.marker.releaseSequence)
                    {
                        return left.marker.releaseSequence >
                            right.marker.releaseSequence;
                    }
                    return left.marker.installedUtc > right.marker.installedUtc;
                });
            bool retainedPrevious = false;
            for (const auto& package : packages)
            {
                const auto processes = inspect_processes_by_executable(
                    package.executable);
                if (!confirmed_stopped(processes))
                    continue;
                if (!retainedPrevious)
                {
                    retainedPrevious = true;
                    log.write("Retained previous UVSR Launcher " +
                        package.marker.version + " as a recovery package.");
                    continue;
                }
                delete_owned_tree(package.directory,
                    paths.launcher_versions_directory());
                log.write("Removed an inactive UVSR Launcher package.");
            }
            if (!fs::exists(paths.launcher_staging_directory()))
                return;
            for (const auto& entry : fs::directory_iterator(
                    paths.launcher_staging_directory()))
            {
                if (!entry.is_directory())
                    continue;
                try
                {
                    (void)parse_guid(narrow(entry.path().filename().native()));
                    delete_owned_tree(entry.path(),
                        paths.launcher_staging_directory());
                }
                catch (const std::exception&)
                {
                }
            }
        }

        [[nodiscard]] LauncherState ensure_current_launcher(
            const InstallerPaths& paths,
            const OwnerMarker& owner,
            const std::optional<InstallState>& renderer,
            bool desktopShortcut,
            detail::ShellIntegration& shell,
            detail::InstallLog& log)
        {
            LauncherInspection inspection = inspect_launcher(paths,
                owner.installationId, desktopShortcut, &log);
            if (!inspection.problem.empty())
                log.write("The installed launcher state needs repair: " +
                    inspection.problem);
            if (inspection.valid &&
                inspection.valid->releaseSequence >= LauncherReleaseSequence)
            {
                LauncherState state = *inspection.valid;
                const bool pointerMatches = inspection.recorded &&
                    inspection.recorded->executableSha256 ==
                        state.executableSha256;
                if (!pointerMatches ||
                    state.desktopShortcut != desktopShortcut)
                {
                    state.desktopShortcut = desktopShortcut;
                    (void)activate_launcher(paths, owner.installationId,
                        state, renderer, false, shell, log);
                }
                sweep_launcher_packages(paths, state, log);
                return state;
            }
            if (!inspection.valid && inspection.highest &&
                inspection.highest->sequence > LauncherReleaseSequence)
            {
                throw Error(
                    "A newer installed UVSR Launcher needs repair. Download the newest UVSR Launcher before changing UVSR.");
            }
            LauncherState candidate = stage_launcher(paths,
                detail::current_executable(), owner.installationId,
                LauncherReleaseSequence, LauncherVersion, desktopShortcut, log);
            (void)activate_launcher(paths, owner.installationId, candidate,
                renderer, false, shell, log);
            sweep_launcher_packages(paths, candidate, log);
            return candidate;
        }

        void restore_previous_renderer(
            const InstallerPaths& paths,
            const GUID& installationId,
            const std::optional<InstallState>& previous,
            const LauncherState& launcher,
            const GUID& transactionId,
            detail::ShellIntegration& shell,
            detail::InstallLog& log)
        {
            if (!previous)
            {
                fs::remove(paths.state_file());
                shell.apply(installationId, std::nullopt, launcher, log,
                    transactionId);
                return;
            }
            detail::validate_install_state(*previous, installationId);
            write_install_state(paths, *previous);
            try
            {
                validate_installed_package(paths, installationId, *previous);
                shell.apply(installationId, previous, launcher, log,
                    transactionId);
            }
            catch (const std::exception& error)
            {
                shell.apply(installationId, std::nullopt, launcher, log,
                    transactionId);
                log.write(std::string(
                    "The previous UVSR package was damaged. Shell entries were cleared: ") +
                    error.what());
            }
        }

        void recover_renderer_transaction(
            const InstallerPaths& paths,
            const OwnerMarker& owner,
            const LauncherState& launcher,
            detail::ShellIntegration& shell,
            detail::InstallLog& log)
        {
            if (!fs::exists(paths.transaction_file()))
                return;
            RendererTransaction transaction = parse_renderer_transaction(
                detail::read_text(paths.transaction_file(), MaximumStateBytes),
                owner.installationId);
            if (transaction.operation == InstallerOperation::Uninstall)
            {
                if (fs::exists(paths.uninstall_record_file()))
                    throw Error(
                        "A previous uninstall is waiting for its cleanup helper to finish.");
                fs::remove(paths.transaction_file());
                return;
            }
            auto current = try_valid_state(paths, owner.installationId);
            const bool beforeActivation = transaction.phase == "download" ||
                transaction.phase == "package" ||
                (transaction.phase == "activate" &&
                    (!current || !transaction.candidateVersionId ||
                        current->activeVersionId !=
                            *transaction.candidateVersionId));
            if (beforeActivation)
            {
                if (transaction.candidateVersionId)
                    remove_inactive_renderer(paths,
                        paths.version_root(*transaction.candidateVersionId), log);
                cleanup_transaction_stage(paths, transaction.transactionId, log);
                fs::remove(paths.transaction_file());
                log.write(
                    "Discarded an interrupted package stage so repair can retry cleanly.");
                return;
            }
            if (transaction.phase == "shell-update")
            {
                try
                {
                    if (!current)
                        throw Error(
                            "The interrupted shortcut update had no valid active state.");
                    shell.apply(owner.installationId, current, launcher, log,
                        transaction.transactionId);
                }
                catch (const std::exception&)
                {
                    restore_previous_renderer(paths, owner.installationId,
                        transaction.previousState, launcher,
                        transaction.transactionId, shell, log);
                    current = transaction.previousState;
                }
                fs::remove(paths.transaction_file());
                if (current)
                    sweep_renderer_versions(paths, *current, log);
                return;
            }
            bool candidateActive = false;
            if (transaction.candidateVersionId && current &&
                current->activeVersionId == *transaction.candidateVersionId)
            {
                try
                {
                    validate_installed_package(paths, owner.installationId,
                        *current);
                    shell.apply(owner.installationId, current, launcher, log,
                        transaction.transactionId);
                    candidateActive = true;
                }
                catch (const std::exception&)
                {
                    restore_previous_renderer(paths, owner.installationId,
                        transaction.previousState, launcher,
                        transaction.transactionId, shell, log);
                    current = transaction.previousState;
                }
            }
            else
            {
                restore_previous_renderer(paths, owner.installationId,
                    transaction.previousState, launcher,
                    transaction.transactionId, shell, log);
                current = transaction.previousState;
            }
            if (!candidateActive && transaction.candidateVersionId)
                remove_inactive_renderer(paths,
                    paths.version_root(*transaction.candidateVersionId), log);
            cleanup_transaction_stage(paths, transaction.transactionId, log);
            fs::remove(paths.transaction_file());
            if (current)
                sweep_renderer_versions(paths, *current, log);
            log.write(
                "Recovered safely from an incomplete earlier UVSR Launcher transaction.");
        }

        [[nodiscard]] RendererFeed download_renderer_feed(
            DownloadManager& downloads,
            const ProgressCallback& progress,
            const LogCallback& log,
            const Cancellation& cancellation)
        {
            if (log)
                log(std::string("Checking authenticated renderer package source: ") +
                    std::string(RendererFeedUrl) + ".");
            const auto data = downloads.bytes(widen(RendererFeedUrl),
                MaximumFeedBytes, "Checking UVSR", progress, log,
                cancellation);
            return verify_renderer_feed(data);
        }

        [[nodiscard]] LauncherFeed download_launcher_feed(
            DownloadManager& downloads,
            const ProgressCallback& progress,
            const LogCallback& log,
            const Cancellation& cancellation)
        {
            if (log)
                log(std::string("Checking signed UVSR Launcher update source: ") +
                    std::string(LauncherFeedUrl) + ".");
            const auto data = downloads.bytes(widen(LauncherFeedUrl),
                MaximumFeedBytes, "Checking UVSR Launcher", progress, log,
                cancellation);
            return verify_launcher_feed(data);
        }

        void ensure_disk_space(
            const InstallerPaths& paths,
            std::uint64_t archiveBytes,
            bool preservingInstalled)
        {
            const std::uint64_t copies = preservingInstalled ? 2u : 1u;
            constexpr std::uint64_t gib = 1024ull * 1024ull * 1024ull;
            if (archiveBytes >
                (std::numeric_limits<std::uint64_t>::max() - copies * gib) / 2u)
                throw Error("The renderer package size was invalid.");
            const std::uint64_t required = archiveBytes * 2u + copies * gib;
            if (detail::available_bytes(paths.localApplicationData) < required)
            {
                throw Error("UVSR needs at least " +
                    std::to_string(required / gib + 1u) +
                    " GB free to download, unpack, and activate safely.");
            }
        }
    }

    void InstallerEngine::ensure_supported_platform()
    {
#if !defined(_M_X64)
        throw Error(
            "UVSR Launcher currently supports only x64 Windows 11 computers.");
#else
        using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        const HMODULE module = GetModuleHandleW(L"ntdll.dll");
        const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFunction>(
            module ? GetProcAddress(module, "RtlGetVersion") : nullptr);
        OSVERSIONINFOEXW information{};
        information.dwOSVersionInfoSize = sizeof(information);
        if (!rtlGetVersion || rtlGetVersion(
                reinterpret_cast<PRTL_OSVERSIONINFOW>(&information)) != 0)
            throw Error("Windows version detection failed.");
        USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        if (!IsWow64Process2(GetCurrentProcess(), &processMachine,
                &nativeMachine) ||
            nativeMachine != IMAGE_FILE_MACHINE_AMD64 ||
            information.dwBuildNumber < 22000 ||
            information.wProductType != VER_NT_WORKSTATION)
        {
            throw Error(
                "UVSR Launcher currently supports only Windows 11 client editions on x64 computers.");
        }
#endif
    }

    InstallSnapshot InstallerEngine::inspect()
    {
        const auto owner = m_Impl->ownership.inspect();
        if (!owner)
        {
            return { false, false, std::nullopt, std::nullopt, {},
                "UVSR is not installed for this Windows user.", false };
        }
        if (!fs::exists(m_Impl->paths.state_file()))
        {
            return { true, false, owner->installationId, std::nullopt, {},
                "UVSR setup is ready to continue. No active version is installed.",
                false };
        }
        try
        {
            InstallState state = detail::parse_install_state(detail::read_text(
                m_Impl->paths.state_file(), MaximumStateBytes));
            validate_installed_package(m_Impl->paths,
                owner->installationId, state);
            const fs::path executable = m_Impl->paths.version_executable(
                state.activeVersionId);
            return { true, true, owner->installationId, state, executable,
                "UVSR Engine " + state.engineVersion +
                    " is ready; settings " + state.settingsHash + ".",
                false };
        }
        catch (const std::exception&)
        {
            return { true, true, owner->installationId, std::nullopt, {},
                "UVSR needs to be reinstalled before it can launch.", true };
        }
    }

    ExactProcessInspection InstallerEngine::inspect_renderer_processes() const
    {
        return inspect_managed_processes(EngineExecutableName,
            m_Impl->paths.versions_directory());
    }

    void InstallerEngine::ensure_launcher_ready(
        bool desktopShortcut,
        const ProgressCallback& progress,
        const LogCallback& logCallback,
        const Cancellation& cancellation)
    {
        ensure_supported_platform();
        cancellation.check();
        detail::OperationLock lock;
        const auto existing = m_Impl->ownership.inspect();
        if (!existing)
            return;
        const OwnerMarker owner = m_Impl->ownership.ensure_roots();
        detail::InstallLog log(m_Impl->paths.logs_directory(), logCallback);
        const InstallSnapshot snapshot = inspect();
        const auto renderer = snapshot.damaged
            ? std::optional<InstallState>{} : snapshot.state;
        recover_launcher(m_Impl->paths, owner.installationId, renderer,
            m_Impl->shell, log);
        (void)ensure_current_launcher(m_Impl->paths, owner, renderer,
            desktopShortcut, m_Impl->shell, log);
        if (progress)
            progress({ "Ready", "UVSR Launcher is ready.", 100, true });
    }

    UpdateCheckResult InstallerEngine::check_for_updates(
        bool desktopShortcut,
        const ProgressCallback& progress,
        const LogCallback& logCallback,
        const Cancellation& cancellation)
    {
        ensure_supported_platform();
        detail::OperationLock lock;
        const OwnerMarker owner = m_Impl->ownership.ensure_roots();
        detail::InstallLog log(m_Impl->paths.logs_directory(), logCallback);
        InstallSnapshot snapshot = inspect();
        auto renderer = snapshot.damaged
            ? std::optional<InstallState>{} : snapshot.state;
        recover_launcher(m_Impl->paths, owner.installationId, renderer,
            m_Impl->shell, log);
        try
        {
            (void)ensure_current_launcher(m_Impl->paths, owner, renderer,
                desktopShortcut, m_Impl->shell, log);
        }
        catch (const std::exception& error)
        {
            log.write(std::string(
                "Deferred local launcher repair until update selection: ") +
                error.what());
        }
        snapshot = inspect();
        renderer = snapshot.damaged
            ? std::optional<InstallState>{} : snapshot.state;
        UpdateCheckResult result;
        try
        {
            if (progress)
                progress({ "Checking UVSR Launcher",
                    "Authenticating the signed launcher update feed.",
                    std::nullopt, true });
            const LauncherFeed feed = download_launcher_feed(m_Impl->downloads,
                progress, [&](std::string_view message) { log.write(message); },
                cancellation);
            const LauncherInspection inspection = inspect_launcher(m_Impl->paths,
                owner.installationId, desktopShortcut, &log);
            ReleaseIdentity current{ LauncherReleaseSequence,
                std::string(LauncherVersion),
                sha256(detail::current_executable()) };
            if (inspection.highest &&
                inspection.highest->sequence > current.sequence)
                current = *inspection.highest;
            const bool healthy = inspection.recorded && inspection.valid &&
                same_identity(identity(*inspection.recorded), current) &&
                same_identity(identity(*inspection.valid), current);
            const ComponentUpdateState state = classify_launcher_update(
                current.sequence, current.version, current.hash, healthy, feed);
            result.launcher = { UpdateComponent::Launcher, state,
                current.version, feed.version,
                state == ComponentUpdateState::UpdateAvailable
                    ? "UVSR Launcher " + feed.version + " is available from the authenticated signed feed."
                    : state == ComponentUpdateState::RepairNeeded
                        ? "UVSR Launcher needs to restore its installed files from the authenticated signed feed."
                        : "UVSR Launcher " + current.version + " is up to date.",
                feed, std::nullopt };
            log.write("Authenticated launcher feed sequence " +
                std::to_string(feed.releaseSequence) + "; result " +
                std::to_string(static_cast<int>(state)) + ".");
        }
        catch (const Cancelled&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            log.write(std::string("Signed launcher update check failed: ") +
                error.what());
            result.launcher = { UpdateComponent::Launcher,
                ComponentUpdateState::CheckFailed,
                std::string(LauncherVersion), {},
                std::string("UVSR Launcher signed update check failed. ") +
                    error.what(), std::nullopt, std::nullopt };
        }

        try
        {
            if (progress)
                progress({ "Checking UVSR",
                    "Authenticating the renderer package feed.",
                    std::nullopt, true });
            const RendererFeed feed = download_renderer_feed(m_Impl->downloads,
                progress, [&](std::string_view message) { log.write(message); },
                cancellation);
            const ComponentUpdateState state = classify_renderer_update(
                snapshot, feed);
            std::string detail;
            switch (state)
            {
            case ComponentUpdateState::RepairNeeded:
                detail = "UVSR Engine needs repair. Reinstalling restores its signed package.";
                break;
            case ComponentUpdateState::NotInstalled:
                detail = "UVSR Engine " + feed.engineVersion +
                    " is ready to install.";
                break;
            case ComponentUpdateState::Current:
                detail = "UVSR Engine " +
                    (snapshot.state ? snapshot.state->engineVersion
                        : feed.engineVersion) + " is up to date.";
                break;
            default:
                detail = "UVSR Engine " + feed.engineVersion +
                    " is available.";
                break;
            }
            detail += " Settings " + feed.settingsHash +
                "; authenticated signed feed.";
            result.uvsr = { UpdateComponent::Uvsr, state,
                snapshot.state ? snapshot.state->engineVersion : std::string{},
                feed.engineVersion, detail, std::nullopt, feed };
            log.write("Authenticated renderer feed sequence " +
                std::to_string(feed.releaseSequence) + "; engine " +
                feed.engineVersion + ".");
        }
        catch (const Cancelled&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            log.write(std::string("Renderer update check failed: ") +
                error.what());
            result.uvsr = { UpdateComponent::Uvsr,
                ComponentUpdateState::CheckFailed,
                snapshot.state ? snapshot.state->engineVersion : std::string{},
                {}, std::string("UVSR renderer update check failed. ") +
                    error.what(), std::nullopt, std::nullopt };
        }
        return result;
    }

    OperationResult InstallerEngine::update_launcher(
        const LauncherFeed& feed,
        bool desktopShortcut,
        bool continueUvsrUpdate,
        const ProgressCallback& progress,
        const LogCallback& logCallback,
        const Cancellation& cancellation)
    {
        ensure_supported_platform();
        validate_launcher_feed(feed);
        detail::OperationLock lock;
        const OwnerMarker owner = m_Impl->ownership.ensure_roots();
        detail::InstallLog log(m_Impl->paths.logs_directory(), logCallback);
        const InstallSnapshot snapshot = inspect();
        const auto renderer = snapshot.damaged
            ? std::optional<InstallState>{} : snapshot.state;
        recover_launcher(m_Impl->paths, owner.installationId, renderer,
            m_Impl->shell, log);
        const fs::path directory = m_Impl->paths.downloads_directory() /
            widen("launcher-update-" + std::to_string(feed.releaseSequence) +
                "-" + feed.artifact.sha256);
        const fs::path downloaded = directory / LauncherExecutableName;
        if (progress)
            progress({ "Downloading UVSR Launcher",
                "Downloading authenticated UVSR Launcher " + feed.version + ".",
                std::nullopt, true });
        m_Impl->downloads.file(launcher_artifact_url(feed), downloaded,
            feed.artifact.size, feed.artifact.sha256, MaximumLauncherBytes,
            "Downloading UVSR Launcher", progress,
            [&](std::string_view message) { log.write(message); }, cancellation,
            [](const fs::path& path)
            {
                verify_launcher_authenticode_policy(path);
            });
        validate_pe_x64(downloaded);
        validate_launcher_metadata(downloaded, feed.version,
            feed.sourceCommit);
        if (run_health_process(downloaded, feed.releaseSequence,
                feed.version, cancellation) != 0)
            throw Error(
                "The downloaded UVSR Launcher did not pass its safety check.");
        LauncherState candidate = stage_launcher(m_Impl->paths, downloaded,
            owner.installationId, feed.releaseSequence, feed.version,
            desktopShortcut, log, feed.artifact.size, feed.artifact.sha256,
            feed.sourceCommit);
        const GUID continuation = activate_launcher(m_Impl->paths,
            owner.installationId, candidate, renderer, continueUvsrUpdate,
            m_Impl->shell, log);
        OperationResult result;
        result.message = "UVSR Launcher " + candidate.version +
            " was updated successfully.";
        result.state = snapshot.state;
        result.executablePath = snapshot.executablePath;
        result.relaunchLauncherPath = m_Impl->paths.launcher_executable(
            candidate.executableSha256);
        result.continueUvsrUpdate = continueUvsrUpdate;
        if (continueUvsrUpdate)
            result.launcherContinuationId = continuation;
        if (progress)
            progress({ "UVSR Launcher updated",
                "UVSR Launcher " + candidate.version + " is ready.",
                100, true });
        return result;
    }

    bool InstallerEngine::desktop_shortcut_preference()
    {
        const InstallSnapshot snapshot = inspect();
        if (!snapshot.installationId)
            return snapshot.state ? snapshot.state->desktopShortcut : true;
        try
        {
            const auto launcher = inspect_launcher(m_Impl->paths,
                *snapshot.installationId,
                snapshot.state ? snapshot.state->desktopShortcut : true);
            if (launcher.valid)
                return launcher.valid->desktopShortcut;
            if (launcher.recorded)
                return launcher.recorded->desktopShortcut;
        }
        catch (const std::exception&)
        {
        }
        return snapshot.state ? snapshot.state->desktopShortcut : true;
    }

    std::optional<GUID> InstallerEngine::pending_launcher_continuation(
        const std::optional<GUID>& requested)
    {
        detail::OperationLock lock;
        const auto owner = m_Impl->ownership.inspect();
        if (!owner || !fs::exists(m_Impl->paths.launcher_transaction_file()))
            return std::nullopt;
        const LauncherTransaction transaction = parse_launcher_transaction(
            detail::read_text(m_Impl->paths.launcher_transaction_file(),
                MaximumStateBytes), owner->installationId);
        if (transaction.phase != "awaiting-continuation" ||
            !transaction.continueUvsrUpdate ||
            (requested && !guid_equal(*requested, transaction.transactionId)))
            return std::nullopt;
        validate_launcher_package(m_Impl->paths, transaction.candidateState);
        if (!detail::path_equal(detail::current_executable(),
                m_Impl->paths.launcher_executable(
                    transaction.candidateState.executableSha256)))
            return std::nullopt;
        return transaction.transactionId;
    }

    void InstallerEngine::complete_launcher_continuation(
        const GUID& transactionId)
    {
        detail::OperationLock lock;
        const OwnerMarker owner = m_Impl->ownership.inspect().value_or(
            OwnerMarker{});
        detail::validate_owner(owner);
        if (!fs::exists(m_Impl->paths.launcher_transaction_file()))
            return;
        LauncherTransaction transaction = parse_launcher_transaction(
            detail::read_text(m_Impl->paths.launcher_transaction_file(),
                MaximumStateBytes), owner.installationId);
        if (!guid_equal(transaction.transactionId, transactionId) ||
            transaction.phase != "awaiting-continuation" ||
            !transaction.continueUvsrUpdate)
        {
            throw Error(
                "The pending UVSR update continuation did not match this launcher.");
        }
        transaction.phase = "continuation-complete";
        detail::write_atomic(m_Impl->paths.launcher_transaction_file(),
            serialize_launcher_transaction(transaction));
        fs::remove(m_Impl->paths.launcher_transaction_file());
    }

    void InstallerEngine::launch_installed()
    {
        detail::OperationLock lock;
        const InstallSnapshot snapshot = inspect();
        if (!snapshot.installed || !snapshot.state)
            throw Error("UVSR is not installed for this Windows user.");
        m_Impl->shell.launch(*snapshot.state);
    }

    void InstallerEngine::cancel_download() noexcept
    {
        m_Impl->downloads.cancel();
    }

    bool redirect_to_active_launcher(
        const InstallerPaths& paths,
        const OwnerMarker& marker,
        const std::vector<std::wstring>& arguments)
    {
        const bool safe = arguments.empty() ||
            (arguments.size() == 1u &&
                (_wcsicmp(arguments[0].c_str(), L"--launch") == 0 ||
                    _wcsicmp(arguments[0].c_str(), L"--uninstall") == 0));
        if (!safe || !fs::exists(paths.launcher_state_file()))
            return false;
        try
        {
            const LauncherState state = detail::parse_launcher_state(
                detail::read_text(paths.launcher_state_file(),
                    MaximumStateBytes));
            detail::validate_launcher_state(state, marker.installationId);
            validate_launcher_package(paths, state);
            const fs::path active = paths.launcher_executable(
                state.executableSha256);
            if (state.releaseSequence < LauncherReleaseSequence ||
                detail::path_equal(active, detail::current_executable()))
                return false;
            launch_executable(active,
                arguments.empty() ? std::wstring_view{} : arguments.front());
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    namespace
    {
        void schedule_uninstall(
            const InstallerPaths& paths,
            const GUID& installationId,
            const GUID& transactionId,
            const std::optional<InstallState>& previous,
            detail::InstallLog& log);
    }

    OperationResult InstallerEngine::execute(
        InstallerOperation operation,
        bool desktopShortcut,
        const ProgressCallback& progress,
        const LogCallback& logCallback,
        const Cancellation& cancellation)
    {
        ensure_supported_platform();
        detail::OperationLock lock;
        const auto existingOwner = m_Impl->ownership.inspect();
        if (operation == InstallerOperation::Uninstall && !existingOwner)
            throw Error("UVSR is not installed for this Windows user.");
        const OwnerMarker owner = operation == InstallerOperation::Uninstall
            ? *existingOwner : m_Impl->ownership.ensure_roots();
        detail::InstallLog log(m_Impl->paths.logs_directory(), logCallback);
        log.write("Starting installer operation " +
            std::to_string(static_cast<int>(operation)) +
            "; installation ID " + guid_string(owner.installationId) + ".");

        if (operation == InstallerOperation::Uninstall)
        {
            m_Impl->ownership.validate_both(owner.installationId);
            if (fs::exists(m_Impl->paths.transaction_file()))
            {
                try
                {
                    const RendererTransaction interrupted =
                        parse_renderer_transaction(detail::read_text(
                            m_Impl->paths.transaction_file(), MaximumStateBytes),
                            owner.installationId);
                    detail::ShellIntegration::remove_staged_registry(
                        interrupted.transactionId);
                }
                catch (const std::exception& error)
                {
                    log.write(std::string(
                        "Preserved unverified registry staging data during uninstall: ") +
                        error.what());
                }
            }
            if (!confirmed_stopped(inspect_managed_processes(
                    EngineExecutableName, m_Impl->paths.programRoot)))
                throw Error(
                    "UVSR is currently running. Close its window, then choose Uninstall again.");
            if (!confirmed_stopped(inspect_managed_processes(
                    LauncherExecutableName, m_Impl->paths.programRoot, true)))
                throw Error(
                    "Another UVSR Launcher window is open. Close it, then choose Uninstall again.");
            if (progress)
                progress({ "Removing UVSR",
                    "Preparing safe cleanup after this launcher window closes.",
                    std::nullopt, false });
            RendererTransaction transaction;
            transaction.installationId = owner.installationId;
            transaction.transactionId = detail::new_guid();
            transaction.operation = operation;
            transaction.phase = "uninstall-pending";
            transaction.previousState = try_valid_state(m_Impl->paths,
                owner.installationId);
            transaction.startedUtc = detail::now_iso8601();
            detail::write_atomic(m_Impl->paths.transaction_file(),
                serialize_renderer_transaction(transaction));
            try
            {
                schedule_uninstall(m_Impl->paths, owner.installationId,
                    transaction.transactionId, transaction.previousState, log);
            }
            catch (...)
            {
                fs::remove(m_Impl->paths.transaction_file());
                throw;
            }
            OperationResult result;
            result.message =
                "UVSR uninstall is ready. Shortcuts and owned program files will be removed when this window closes. Renderer settings are preserved.";
            result.cleanupScheduled = true;
            return result;
        }

        InstallSnapshot initial = inspect();
        auto renderer = initial.damaged
            ? std::optional<InstallState>{} : initial.state;
        recover_launcher(m_Impl->paths, owner.installationId, renderer,
            m_Impl->shell, log);
        LauncherState launcher = ensure_current_launcher(m_Impl->paths, owner,
            renderer, desktopShortcut, m_Impl->shell, log);
        recover_renderer_transaction(m_Impl->paths, owner, launcher,
            m_Impl->shell, log);

        const bool originalStateExists = fs::exists(m_Impl->paths.state_file());
        std::optional<std::vector<std::byte>> originalState;
        if (originalStateExists)
        {
            try
            {
                originalState = detail::read_file(m_Impl->paths.state_file(),
                    MaximumStateBytes);
            }
            catch (const std::exception&)
            {
            }
        }
        std::optional<InstallState> previous;
        if (operation == InstallerOperation::Reinstall)
            previous = try_valid_state(m_Impl->paths, owner.installationId);
        else
            previous = read_state_if_present(m_Impl->paths,
                owner.installationId);
        if (operation == InstallerOperation::Install && originalStateExists)
            throw Error(
                "UVSR is already installed. Choose Install again and select Reinstall.");
        if (operation == InstallerOperation::Update && !previous)
            throw Error("UVSR is not installed yet. Choose Install.");
        m_Impl->shell.validate_can_apply(owner.installationId,
            desktopShortcut);

        const RendererFeed feed = download_renderer_feed(m_Impl->downloads,
            progress, [&](std::string_view message) { log.write(message); },
            cancellation);
        ensure_disk_space(m_Impl->paths, feed.artifact.size,
            previous.has_value());
        const ComponentUpdateState feedState = classify_renderer_update(
            inspect(), feed);
        if (operation == InstallerOperation::Reinstall && previous &&
            feed.releaseSequence < previous->releaseSequence)
        {
            throw Error(
                "UVSR Launcher refused to replace a newer renderer with an older feed sequence.");
        }
        if (operation == InstallerOperation::Update &&
            feedState == ComponentUpdateState::Current)
        {
            InstallState updated = *previous;
            updated.desktopShortcut = desktopShortcut;
            RendererTransaction transaction;
            transaction.installationId = owner.installationId;
            transaction.transactionId = detail::new_guid();
            transaction.operation = operation;
            transaction.phase = "shell-update";
            transaction.candidateVersionId = previous->activeVersionId;
            transaction.previousState = previous;
            transaction.startedUtc = detail::now_iso8601();
            detail::write_atomic(m_Impl->paths.transaction_file(),
                serialize_renderer_transaction(transaction));
            try
            {
                write_install_state(m_Impl->paths, updated);
                m_Impl->shell.apply(owner.installationId, updated, launcher,
                    log, transaction.transactionId);
            }
            catch (...)
            {
                write_install_state(m_Impl->paths, *previous);
                m_Impl->shell.apply(owner.installationId, previous, launcher,
                    log, transaction.transactionId);
                fs::remove(m_Impl->paths.transaction_file());
                throw;
            }
            fs::remove(m_Impl->paths.transaction_file());
            sweep_renderer_versions(m_Impl->paths, updated, log);
            OperationResult result;
            result.message =
                "UVSR Engine is already at the newest trusted package.";
            result.state = updated;
            result.executablePath = m_Impl->paths.version_executable(
                updated.activeVersionId);
            return result;
        }

        RendererTransaction transaction;
        transaction.installationId = owner.installationId;
        transaction.transactionId = detail::new_guid();
        transaction.operation = operation;
        transaction.phase = "download";
        transaction.previousState = previous;
        transaction.startedUtc = detail::now_iso8601();
        detail::write_atomic(m_Impl->paths.transaction_file(),
            serialize_renderer_transaction(transaction));
        std::optional<PackageManifest> manifest;
        try
        {
            const fs::path archive = m_Impl->paths.downloads_directory() /
                widen("renderer-" + std::to_string(feed.releaseSequence) +
                    "-" + feed.artifact.sha256 + ".zip");
            if (progress)
                progress({ "Downloading UVSR Engine",
                    "Downloading the authenticated renderer package.",
                    std::nullopt, true });
            m_Impl->downloads.file(renderer_artifact_url(feed), archive,
                feed.artifact.size, feed.artifact.sha256,
                MaximumRendererPackageBytes, "Downloading UVSR Engine",
                progress,
                [&](std::string_view message) { log.write(message); },
                cancellation);
            transaction.phase = "package";
            transaction.candidateVersionId = create_version_id(
                feed.sourceCommit);
            detail::write_atomic(m_Impl->paths.transaction_file(),
                serialize_renderer_transaction(transaction));
            if (progress)
                progress({ "Installing UVSR Engine",
                    "Verifying and unpacking the authenticated renderer package.",
                    std::nullopt, true });
            manifest = detail::stage_renderer_archive(m_Impl->paths,
                transaction.transactionId, archive, feed, log);
            (void)detail::activate_renderer_package(m_Impl->paths,
                transaction.transactionId, *transaction.candidateVersionId,
                *manifest);
            InstallState state;
            state.installationId = owner.installationId;
            state.activeVersionId = *transaction.candidateVersionId;
            state.releaseSequence = feed.releaseSequence;
            state.commit = feed.sourceCommit;
            state.settingsHash = feed.settingsHash;
            state.engineVersion = feed.engineVersion;
            state.artifactSha256 = feed.artifact.sha256;
            state.executableSha256 = manifest->executableSha256;
            state.desktopShortcut = desktopShortcut;
            state.installedUtc = detail::now_iso8601();
            detail::validate_install_state(state, owner.installationId);
            transaction.phase = "activate";
            detail::write_atomic(m_Impl->paths.transaction_file(),
                serialize_renderer_transaction(transaction));
            write_install_state(m_Impl->paths, state);
            transaction.phase = "shell-activation";
            detail::write_atomic(m_Impl->paths.transaction_file(),
                serialize_renderer_transaction(transaction));
            m_Impl->shell.apply(owner.installationId, state, launcher, log,
                transaction.transactionId);
            fs::remove(m_Impl->paths.transaction_file());
            cleanup_transaction_stage(m_Impl->paths,
                transaction.transactionId, log);
            if (previous &&
                previous->activeVersionId != state.activeVersionId)
            {
                remove_inactive_renderer(m_Impl->paths,
                    m_Impl->paths.version_root(previous->activeVersionId), log);
            }
            sweep_renderer_versions(m_Impl->paths, state, log);
            if (progress)
                progress({ "UVSR is ready",
                    "The newest selected UVSR version is installed.",
                    100, true });
            OperationResult result;
            const char* verb = operation == InstallerOperation::Install
                ? "installed" : operation == InstallerOperation::Reinstall
                    ? "reinstalled" : "updated";
            result.message = std::string("UVSR was ") + verb +
                " successfully.";
            result.state = state;
            result.executablePath = m_Impl->paths.version_executable(
                state.activeVersionId);
            return result;
        }
        catch (...)
        {
            log.write(
                "The operation failed. Restoring the previous active state.");
            try
            {
                if (previous)
                {
                    restore_previous_renderer(m_Impl->paths,
                        owner.installationId, previous, launcher,
                        transaction.transactionId, m_Impl->shell, log);
                }
                else if (originalState)
                {
                    detail::write_atomic(m_Impl->paths.state_file(),
                        std::string_view(
                            reinterpret_cast<const char*>(originalState->data()),
                             originalState->size()));
                    m_Impl->shell.apply(owner.installationId, std::nullopt,
                        launcher, log, transaction.transactionId);
                }
                else
                {
                    fs::remove(m_Impl->paths.state_file());
                    m_Impl->shell.apply(owner.installationId, std::nullopt,
                        launcher, log, transaction.transactionId);
                }
                if (transaction.candidateVersionId)
                    remove_inactive_renderer(m_Impl->paths,
                        m_Impl->paths.version_root(
                            *transaction.candidateVersionId), log);
                cleanup_transaction_stage(m_Impl->paths,
                    transaction.transactionId, log);
                fs::remove(m_Impl->paths.transaction_file());
            }
            catch (const std::exception& rollbackError)
            {
                log.write(std::string(
                    "Rollback could not finish. The recovery record was preserved: ") +
                    rollbackError.what());
                throw Error(
                    "The operation failed and cleanup could not finish. Reopen UVSR Launcher to resume safely.");
            }
            throw;
        }
    }

    namespace
    {
        [[nodiscard]] std::string compact_guid(const GUID& value)
        {
            std::string result = guid_string(value);
            std::erase(result, '-');
            return result;
        }

        [[nodiscard]] std::uint64_t process_creation_time(HANDLE process)
        {
            FILETIME created{}, exited{}, kernel{}, user{};
            if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
                detail::throw_last_error(
                    "Windows could not identify the cleanup parent");
            ULARGE_INTEGER value{};
            value.LowPart = created.dwLowDateTime;
            value.HighPart = created.dwHighDateTime;
            return value.QuadPart;
        }

        [[nodiscard]] OwnerMarker read_operations_owner(
            const InstallerPaths& paths)
        {
            reject_reparse_chain(paths.operations_root(),
                "UVSR cleanup coordination directory");
            return detail::parse_owner(detail::read_text(
                paths.operations_marker(), MaximumStateBytes));
        }

        void ensure_operations_root(
            const InstallerPaths& paths,
            const GUID& installationId)
        {
            reject_reparse_chain(paths.operations_root(),
                "UVSR cleanup coordination directory");
            if (fs::exists(paths.operations_root()))
            {
                if (!fs::exists(paths.operations_marker()))
                {
                    if (!fs::is_empty(paths.operations_root()))
                        throw Error(
                            "The UVSR cleanup coordination directory had an unknown owner. It was preserved.");
                    OwnerMarker marker;
                    marker.installationId = installationId;
                    detail::write_atomic(paths.operations_marker(),
                        detail::serialize_owner(marker));
                }
                OwnerMarker marker = read_operations_owner(paths);
                if (fs::exists(paths.uninstall_record_file()))
                    throw Error(
                        "An earlier UVSR uninstall still needs to finish.");
                if (!guid_equal(marker.installationId, installationId))
                {
                    marker.installationId = installationId;
                    detail::write_atomic(paths.operations_marker(),
                        detail::serialize_owner(marker));
                }
            }
            else
            {
                fs::create_directories(paths.operations_root());
                OwnerMarker marker;
                marker.installationId = installationId;
                detail::write_atomic(paths.operations_marker(),
                    detail::serialize_owner(marker));
            }
            fs::create_directories(paths.helpers_directory());
            reject_reparse_chain(paths.helpers_directory(),
                "UVSR cleanup helper directory");
        }

        void launch_cleanup_helper(
            const InstallerPaths& paths,
            const UninstallRecord& record)
        {
            const fs::path current = detail::current_executable();
            const fs::path helperRoot = paths.helpers_directory() /
                widen(compact_guid(record.transactionId) + "-" +
                    compact_guid(detail::new_guid()));
            fs::create_directories(helperRoot);
            reject_reparse_chain(helperRoot, "UVSR cleanup helper directory");
            const fs::path helper = helperRoot / LauncherExecutableName;
            try
            {
                const std::string currentHash = sha256(current);
                detail::copy_file_verified(current, helper, currentHash);
                const std::uint64_t creation = process_creation_time(
                    GetCurrentProcess());
                std::wstring command = L"\"" + helper.native() +
                    L"\" --cleanup " +
                    std::to_wstring(GetCurrentProcessId()) + L" " +
                    std::to_wstring(creation) + L" " +
                    widen(guid_string(record.installationId)) + L" " +
                    widen(guid_string(record.transactionId));
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                PROCESS_INFORMATION process{};
                if (!CreateProcessW(helper.c_str(), command.data(), nullptr,
                        nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        helperRoot.c_str(), &startup, &process))
                {
                    detail::throw_last_error(
                        "Windows could not start the UVSR cleanup helper");
                }
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
            catch (...)
            {
                if (fs::exists(helperRoot))
                    delete_owned_tree(helperRoot, paths.helpers_directory());
                throw;
            }
        }

        void schedule_uninstall(
            const InstallerPaths& paths,
            const GUID& installationId,
            const GUID& transactionId,
            const std::optional<InstallState>& previous,
            detail::InstallLog& log)
        {
            ensure_operations_root(paths, installationId);
            UninstallRecord record;
            record.installationId = installationId;
            record.transactionId = transactionId;
            record.phase = "prepared";
            record.previousState = previous;
            record.startedUtc = detail::now_iso8601();
            detail::write_atomic(paths.uninstall_record_file(),
                serialize_uninstall_record(record));
            try
            {
                launch_cleanup_helper(paths, record);
            }
            catch (...)
            {
                fs::remove(paths.uninstall_record_file());
                throw;
            }
            log.write(
                "Scheduled serialized UVSR cleanup after this launcher window closes.");
        }

        void wait_for_parent(
            DWORD processId,
            std::uint64_t expectedCreationTime)
        {
            Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                SYNCHRONIZE, FALSE, processId));
            if (!process)
            {
                if (GetLastError() == ERROR_INVALID_PARAMETER)
                    return;
                return;
            }
            if (process_creation_time(process.get()) == expectedCreationTime)
                (void)WaitForSingleObject(process.get(), INFINITE);
        }

        [[nodiscard]] fs::path program_tombstone(
            const InstallerPaths& paths,
            const GUID& transactionId)
        {
            return paths.localApplicationData / L"Programs" /
                widen("UVSR.uninstall-" + compact_guid(transactionId));
        }

        [[nodiscard]] fs::path state_tombstone(
            const InstallerPaths& paths,
            const GUID& transactionId)
        {
            return paths.localApplicationData /
                widen("UVSR Installer.uninstall-" +
                    compact_guid(transactionId));
        }

        void validate_root_owner(
            const fs::path& markerPath,
            const GUID& installationId)
        {
            const OwnerMarker marker = detail::parse_owner(detail::read_text(
                markerPath, MaximumStateBytes));
            if (!guid_equal(marker.installationId, installationId))
                throw Error(
                    "UVSR ownership could not be proven. No files were removed.");
        }

        void move_owned_root(
            const fs::path& source,
            const fs::path& tombstone,
            const fs::path& marker,
            const GUID& installationId)
        {
            if (fs::exists(tombstone))
            {
                if (fs::exists(source))
                    throw Error(
                        "An interrupted uninstall found two owned copies. No files were removed.");
                validate_root_owner(tombstone /
                    L".uvsr-installer-owner.json", installationId);
                return;
            }
            if (!fs::exists(source))
                throw Error(
                    "An owned UVSR root was missing before cleanup could secure it.");
            validate_root_owner(marker, installationId);
            reject_reparse_chain(source, "UVSR uninstall source");
            reject_reparse_chain(tombstone, "UVSR uninstall tombstone");
            fs::rename(source, tombstone);
        }

        void delete_with_retries(
            const fs::path& path,
            const fs::path& parent)
        {
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                try
                {
                    if (fs::exists(path))
                        delete_owned_tree(path, parent);
                    return;
                }
                catch (const std::exception&)
                {
                    if (attempt == 9)
                        throw;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        }
    }

    int run_cleanup_helper(
        DWORD parentProcessId,
        std::uint64_t parentCreationTime,
        const GUID& installationId,
        const GUID& transactionId)
    {
        try
        {
            wait_for_parent(parentProcessId, parentCreationTime);
            const InstallerPaths paths = InstallerPaths::current_user();
            detail::OperationLock lock;
            const OwnerMarker operationsOwner = read_operations_owner(paths);
            UninstallRecord record = parse_uninstall_record(detail::read_text(
                paths.uninstall_record_file(), MaximumStateBytes));
            if (!guid_equal(operationsOwner.installationId, installationId) ||
                !guid_equal(record.installationId, installationId) ||
                !guid_equal(record.transactionId, transactionId))
                return 3;
            const fs::path programTrash = program_tombstone(paths, transactionId);
            const fs::path stateTrash = state_tombstone(paths, transactionId);
            const auto rendererLive = inspect_managed_processes(
                EngineExecutableName, paths.programRoot);
            const auto rendererTrash = inspect_managed_processes(
                EngineExecutableName, programTrash);
            const auto launcherLive = inspect_managed_processes(
                LauncherExecutableName, paths.programRoot);
            const auto launcherTrash = inspect_managed_processes(
                LauncherExecutableName, programTrash);
            if (!confirmed_stopped(rendererLive) ||
                !confirmed_stopped(rendererTrash) ||
                !confirmed_stopped(launcherLive) ||
                !confirmed_stopped(launcherTrash))
                return 4;
            const fs::path cleanupLogs = paths.operations_root() / L"logs" /
                widen(compact_guid(transactionId));
            detail::InstallLog log(cleanupLogs);
            detail::ShellIntegration shell(paths);
            auto writePhase = [&](std::string_view phase)
            {
                record.phase = std::string(phase);
                detail::write_atomic(paths.uninstall_record_file(),
                    serialize_uninstall_record(record));
            };
            if (record.phase == "prepared")
            {
                detail::Ownership(paths).validate_both(installationId);
                writePhase("removing-shell");
            }
            if (record.phase == "removing-shell")
            {
                shell.remove(installationId, log);
                writePhase("shell-removed");
            }
            if (record.phase == "shell-removed")
            {
                move_owned_root(paths.programRoot, programTrash,
                    paths.program_marker(), installationId);
                writePhase("program-moved");
            }
            if (record.phase == "program-moved")
            {
                move_owned_root(paths.stateRoot, stateTrash,
                    paths.state_marker(), installationId);
                writePhase("roots-moved");
            }
            if (record.phase == "roots-moved")
                writePhase("deleting");
            if (record.phase != "deleting")
                return 5;
            if (fs::exists(paths.programRoot) || fs::exists(paths.stateRoot))
                return 6;
            delete_with_retries(programTrash,
                paths.localApplicationData / L"Programs");
            delete_with_retries(stateTrash, paths.localApplicationData);
            fs::remove(paths.uninstall_record_file());
            return fs::exists(paths.uninstall_record_file()) ? 7 : 0;
        }
        catch (const std::exception&)
        {
            return 1;
        }
    }

    bool schedule_current_helper_deletion(const GUID& installationId)
    {
        try
        {
            const InstallerPaths paths = InstallerPaths::current_user();
            const OwnerMarker owner = read_operations_owner(paths);
            if (!guid_equal(owner.installationId, installationId))
                return false;
            const fs::path current = detail::current_executable();
            const fs::path directory = current.parent_path();
            if (!detail::path_equal(directory.parent_path(),
                    paths.helpers_directory()) ||
                _wcsicmp(current.filename().c_str(),
                    LauncherExecutableName.data()) != 0)
                return false;
            reject_reparse_chain(current, "UVSR cleanup helper executable");
            Handle file(CreateFileW(current.c_str(), DELETE | SYNCHRONIZE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (file)
            {
                FILE_DISPOSITION_INFO_EX disposition{};
                disposition.Flags = FILE_DISPOSITION_FLAG_DELETE |
                    FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                    FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE;
                if (SetFileInformationByHandle(file.get(),
                        FileDispositionInfoEx, &disposition,
                        sizeof(disposition)))
                {
                    file.reset();
                    RemoveDirectoryW(directory.c_str());
                    return true;
                }
            }
            const bool fileScheduled = MoveFileExW(current.c_str(), nullptr,
                MOVEFILE_DELAY_UNTIL_REBOOT) != FALSE;
            const bool directoryScheduled = MoveFileExW(directory.c_str(),
                nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != FALSE;
            return fileScheduled && directoryScheduled;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    void recover_interrupted_uninstall(const InstallerPaths& paths)
    {
        if (!fs::exists(paths.uninstall_record_file()))
            return;
        detail::OperationLock lock;
        const UninstallRecord record = parse_uninstall_record(detail::read_text(
            paths.uninstall_record_file(), MaximumStateBytes));
        const OwnerMarker owner = read_operations_owner(paths);
        if (!guid_equal(owner.installationId, record.installationId))
            throw Error(
                "An interrupted UVSR uninstall had mismatched ownership records.");
        launch_cleanup_helper(paths, record);
        throw Error(
            "Windows interrupted an earlier UVSR uninstall. Cleanup will resume after this message closes.");
    }

    void remove_stale_helpers(const InstallerPaths& paths)
    {
        if (!fs::exists(paths.operations_root()))
            return;
        detail::OperationLock lock;
        (void)read_operations_owner(paths);
        if (fs::exists(paths.uninstall_record_file()))
            return;
        if (fs::exists(paths.helpers_directory()) && confirmed_stopped(
                inspect_managed_processes(LauncherExecutableName,
                    paths.helpers_directory())))
        {
            delete_owned_tree(paths.helpers_directory(),
                paths.operations_root());
        }
        const fs::path logs = paths.operations_root() / L"logs";
        if (fs::exists(logs))
            delete_owned_tree(logs, paths.operations_root());
        std::vector<fs::path> remaining;
        for (const auto& entry : fs::directory_iterator(paths.operations_root()))
            remaining.push_back(entry.path());
        if (remaining.size() == 1u && detail::path_equal(
                remaining.front(), paths.operations_marker()))
        {
            const fs::path retired = paths.localApplicationData /
                widen("UVSR Installer Operations.retired-" +
                    compact_guid(detail::new_guid()));
            fs::rename(paths.operations_root(), retired);
            delete_owned_tree(retired, paths.localApplicationData);
        }
    }
}
