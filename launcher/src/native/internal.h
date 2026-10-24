#pragma once

#include "launcher.h"

#include "json_document.h"

#include <mutex>
#include <set>
#include <unordered_map>

namespace uvsr::launcher::detail
{
    using Json = uvsr::json::Value;

    [[nodiscard]] std::vector<std::byte> read_file(
        const std::filesystem::path& path,
        std::uint64_t maximumBytes,
        bool allowEmpty = false);
    [[nodiscard]] std::string read_text(
        const std::filesystem::path& path,
        std::uint64_t maximumBytes,
        bool allowEmpty = false);
    void write_atomic(
        const std::filesystem::path& path,
        std::string_view contents);
    void copy_file_verified(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::string_view expectedHash);
    [[nodiscard]] std::filesystem::path current_executable();
    [[nodiscard]] std::string now_iso8601();
    [[nodiscard]] GUID new_guid();
    [[nodiscard]] std::string json_escape(std::string_view value);

    void require_exact_object(
        const Json& value,
        std::initializer_list<std::string_view> names,
        std::string_view description);
    [[nodiscard]] const Json& member(
        const Json& value,
        std::string_view name);
    [[nodiscard]] std::string string_value(
        const Json& value,
        std::string_view description);
    [[nodiscard]] std::int64_t integer_value(
        const Json& value,
        std::string_view description);
    [[nodiscard]] std::uint64_t unsigned_value(
        const Json& value,
        std::string_view description);
    [[nodiscard]] bool boolean_value(
        const Json& value,
        std::string_view description);
    [[nodiscard]] std::optional<std::string> nullable_string(
        const Json& value,
        std::string_view description);
    [[nodiscard]] Json parse_json(std::string_view text);

    void validate_owner(const OwnerMarker& marker);
    void validate_install_state(
        const InstallState& state,
        const GUID& expectedInstallationId);
    void validate_launcher_state(
        const LauncherState& state,
        const GUID& expectedInstallationId);
    [[nodiscard]] OwnerMarker parse_owner(std::string_view text);
    [[nodiscard]] InstallState parse_install_state(const Json& value);
    [[nodiscard]] InstallState parse_install_state(std::string_view text);
    [[nodiscard]] LauncherState parse_launcher_state(const Json& value);
    [[nodiscard]] LauncherState parse_launcher_state(std::string_view text);
    [[nodiscard]] std::string serialize_owner(const OwnerMarker& marker);
    [[nodiscard]] std::string serialize_install_state(const InstallState& state);
    [[nodiscard]] std::string serialize_launcher_state(const LauncherState& state);
    [[nodiscard]] std::string serialize_install_state_value(
        const std::optional<InstallState>& state,
        int indent = 0);
    [[nodiscard]] std::string serialize_launcher_state_value(
        const std::optional<LauncherState>& state,
        int indent = 0);

    class OperationLock final
    {
    public:
        OperationLock();
        ~OperationLock();
        OperationLock(const OperationLock&) = delete;
        OperationLock& operator=(const OperationLock&) = delete;

    private:
        HANDLE m_Handle = nullptr;
    };

    class InstallLog final
    {
    public:
        InstallLog(
            const std::filesystem::path& logsDirectory,
            LogCallback observer = {});
        void write(std::string_view message);
        [[nodiscard]] const std::filesystem::path& path() const noexcept;

    private:
        std::filesystem::path m_Path;
        LogCallback m_Observer;
        std::mutex m_Mutex;
        bool m_Truncated = false;
    };

    class Ownership final
    {
    public:
        explicit Ownership(InstallerPaths paths);
        [[nodiscard]] std::optional<OwnerMarker> inspect() const;
        [[nodiscard]] OwnerMarker ensure_roots() const;
        void validate_both(const GUID& installationId) const;

    private:
        InstallerPaths m_Paths;
    };

    struct ShortcutInfo
    {
        std::filesystem::path target;
        std::wstring arguments;
    };

    class ShellIntegration final
    {
    public:
        explicit ShellIntegration(InstallerPaths paths);
        void validate_can_apply(
            const GUID& installationId,
            bool desktopShortcut,
            const std::optional<GUID>& transactionId = std::nullopt) const;
        void apply(
            const GUID& installationId,
            const std::optional<InstallState>& renderer,
            const LauncherState& launcher,
            InstallLog& log,
            const std::optional<GUID>& transactionId = std::nullopt) const;
        void remove(const GUID& installationId, InstallLog& log) const;
        void launch(const InstallState& state) const;
        [[nodiscard]] bool shortcut_owned(
            const ShortcutInfo& shortcut,
            const GUID& installationId) const;
        [[nodiscard]] static ShortcutInfo read_shortcut(
            const std::filesystem::path& path);
        static void create_shortcut(
            const std::filesystem::path& path,
            const std::filesystem::path& target,
            std::wstring_view arguments,
            const std::filesystem::path& workingDirectory,
            std::wstring_view description);
        static void remove_staged_registry(const GUID& transactionId);

    private:
        InstallerPaths m_Paths;
    };

    [[nodiscard]] std::uint64_t available_bytes(
        const std::filesystem::path& path);
    [[nodiscard]] std::uint64_t directory_bytes(
        const std::filesystem::path& path);
    void validate_shell_path(
        const InstallerPaths& paths,
        const std::filesystem::path& path,
        std::string_view description);
    [[nodiscard]] bool path_equal(
        const std::filesystem::path& left,
        const std::filesystem::path& right);
    [[nodiscard]] std::string format_bytes(std::uint64_t bytes);
    [[nodiscard]] std::string version_string(
        const std::filesystem::path& path,
        std::wstring_view field);
    [[nodiscard]] PackageManifest stage_renderer_archive(
        const InstallerPaths& paths,
        const GUID& transactionId,
        const std::filesystem::path& archive,
        const RendererFeed& feed,
        InstallLog& log);
    [[nodiscard]] std::filesystem::path activate_renderer_package(
        const InstallerPaths& paths,
        const GUID& transactionId,
        std::string_view versionId,
        const PackageManifest& manifest);
    [[nodiscard]] std::string win32_message(DWORD error);
    [[noreturn]] void throw_last_error(std::string_view operation);
}
