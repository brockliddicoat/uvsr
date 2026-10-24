#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace uvsr::launcher
{
    inline constexpr int SchemaVersion = 1;
    inline constexpr int LauncherSchemaVersion = 1;
    inline constexpr int SettingsContractSchemaVersion = 7;
    inline constexpr std::string_view ProductId =
        "0c47a7a8-1ec4-4ffd-b6c4-2f7614181223";
    inline constexpr std::string_view LauncherFeedUrl =
        "https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/launcher-update-feed-v2.json";
    inline constexpr std::string_view RendererFeedUrl =
        "https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/renderer-update-feed-v1.json";
    inline constexpr std::string_view UpdateFeedKeyId =
        "uvsr-launcher-update-p256-2026-01";
    inline constexpr std::string_view UpdateFeedPublicKeySpkiBase64 =
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEATbHkDwYIS0nMut5h9Q6m67qfabhuK+VRo6mDW1UlwZQIfeLI7zc1aKblCclkfgd8DDU0LcblFgTFdvoAWgCYg==";
    inline constexpr std::string_view LauncherVersion = UVSR_LAUNCHER_VERSION;
    inline constexpr std::int64_t LauncherReleaseSequence =
        UVSR_LAUNCHER_RELEASE_SEQUENCE;
    inline constexpr std::wstring_view LauncherExecutableName =
        L"uvsr-launcher.exe";
    inline constexpr std::wstring_view EngineExecutableName =
        L"uvsr-engine.exe";
    inline constexpr std::wstring_view RendererArtifactName =
        L"uvsr-renderer-windows-11-x64.zip";
    inline constexpr std::uint64_t MaximumStateBytes = 64ull * 1024ull;
    inline constexpr std::uint64_t MaximumManifestBytes = 16ull * 1024ull * 1024ull;
    inline constexpr std::uint64_t MaximumFeedBytes = 16ull * 1024ull;
    inline constexpr std::uint64_t MaximumFeedPayloadBytes = 8ull * 1024ull;
    inline constexpr std::uint64_t MaximumLauncherBytes = 256ull * 1024ull * 1024ull;
    inline constexpr std::uint64_t MaximumRendererPackageBytes =
        32ull * 1024ull * 1024ull * 1024ull;
    inline constexpr std::uint64_t MaximumRendererExpandedBytes =
        64ull * 1024ull * 1024ull * 1024ull;
    inline constexpr std::int64_t MaximumReleaseSequence =
        9007199254740991ll;

    class Error : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    class Cancelled final : public Error
    {
    public:
        Cancelled() : Error("The operation was cancelled.") {}
    };

    class Cancellation final
    {
    public:
        void request() noexcept;
        [[nodiscard]] bool requested() const noexcept;
        void check() const;

    private:
        std::atomic_bool m_Requested = false;
    };

    enum class InstallerOperation { Install, Update, Reinstall, Uninstall };
    enum class UpdateComponent { Uvsr, Launcher };
    enum class ComponentUpdateState
    {
        NotInstalled,
        Current,
        UpdateAvailable,
        RepairNeeded,
        CheckFailed
    };
    enum class ProcessState { NotRunning, Running, Unverifiable };
    enum class RendererBusyAction { None, Launching, Closing };
    enum class RendererButtonIntent { None, Launch, Close };
    enum class RendererClickAction { None, Launch, Close };
    enum class UpdateFlowExit
    {
        UpToDate,
        SelectionCancelled,
        NoSelection,
        OperationCancelled
    };

    struct OwnerMarker
    {
        int schemaVersion = SchemaVersion;
        std::string productId{ ProductId };
        GUID installationId{};
    };

    struct InstallState
    {
        int schemaVersion = SchemaVersion;
        GUID installationId{};
        std::string activeVersionId;
        std::int64_t releaseSequence = 0;
        std::string commit;
        std::string settingsHash;
        std::string engineVersion;
        std::string artifactSha256;
        std::string executableSha256;
        bool desktopShortcut = true;
        std::string installedUtc;
    };

    struct PackageFile
    {
        std::string relativePath;
        std::uint64_t size = 0;
        std::string sha256;
    };

    struct PackageManifest
    {
        int schemaVersion = 0;
        std::string productId;
        bool production = false;
        std::string configuration;
        std::int64_t releaseSequence = 0;
        std::string sourceCommit;
        std::string settingsHash;
        std::string engineVersion;
        std::string executableSha256;
        std::vector<PackageFile> files;
    };

    struct LauncherState
    {
        int schemaVersion = LauncherSchemaVersion;
        std::string productId{ ProductId };
        GUID installationId{};
        std::int64_t releaseSequence = 0;
        std::string version;
        std::string executableSha256;
        bool desktopShortcut = true;
        std::string installedUtc;
    };

    struct LauncherFeedArtifact
    {
        std::string name;
        std::uint64_t size = 0;
        std::string sha256;
    };

    struct LauncherFeed
    {
        int schemaVersion = 0;
        std::string productId;
        std::string channel;
        std::int64_t releaseSequence = 0;
        std::string version;
        std::string sourceCommit;
        LauncherFeedArtifact artifact;
    };

    struct RendererFeedArtifact
    {
        std::string name;
        std::uint64_t size = 0;
        std::string sha256;
    };

    struct RendererFeed
    {
        int schemaVersion = 0;
        std::string productId;
        std::string channel;
        std::int64_t releaseSequence = 0;
        std::string sourceCommit;
        std::string settingsHash;
        std::string engineVersion;
        RendererFeedArtifact artifact;
    };

    struct ComponentUpdateStatus
    {
        UpdateComponent component = UpdateComponent::Uvsr;
        ComponentUpdateState state = ComponentUpdateState::CheckFailed;
        std::string currentIdentifier;
        std::string availableIdentifier;
        std::string detail;
        std::optional<LauncherFeed> launcherFeed;
        std::optional<RendererFeed> rendererFeed;
    };

    struct UpdateCheckResult
    {
        ComponentUpdateStatus uvsr;
        ComponentUpdateStatus launcher;
    };

    struct InstallSnapshot
    {
        bool initialized = false;
        bool installed = false;
        std::optional<GUID> installationId;
        std::optional<InstallState> state;
        std::filesystem::path executablePath;
        std::string summary;
        bool damaged = false;
    };

    struct OperationResult
    {
        std::string message;
        std::optional<InstallState> state;
        std::filesystem::path executablePath;
        bool cleanupScheduled = false;
        std::filesystem::path relaunchLauncherPath;
        bool continueUvsrUpdate = false;
        std::optional<GUID> launcherContinuationId;
    };

    struct InstallerProgress
    {
        std::string phase;
        std::string detail;
        std::optional<int> percent;
        bool canCancel = true;
    };

    struct ExactProcessIdentity
    {
        DWORD processId = 0;
        std::uint64_t creationTime = 0;
        std::filesystem::path executablePath;
    };

    struct ExactProcessInspection
    {
        ProcessState state = ProcessState::NotRunning;
        std::vector<ExactProcessIdentity> matches;
    };

    struct TerminalUiState
    {
        std::wstring phase;
        std::wstring detail;
        int progress = 0;
        bool marquee = false;
        bool error = false;
    };

    using ProgressCallback = std::function<void(const InstallerProgress&)>;
    using LogCallback = std::function<void(std::string_view)>;

    struct InstallerPaths
    {
        std::filesystem::path localApplicationData;
        std::filesystem::path programRoot;
        std::filesystem::path stateRoot;
        std::filesystem::path desktopDirectory;
        std::filesystem::path programsDirectory;

        [[nodiscard]] static InstallerPaths current_user();
        [[nodiscard]] static InstallerPaths create(
            std::filesystem::path local,
            std::filesystem::path desktop,
            std::filesystem::path programs);
        [[nodiscard]] std::filesystem::path program_marker() const;
        [[nodiscard]] std::filesystem::path state_marker() const;
        [[nodiscard]] std::filesystem::path state_file() const;
        [[nodiscard]] std::filesystem::path transaction_file() const;
        [[nodiscard]] std::filesystem::path logs_directory() const;
        [[nodiscard]] std::filesystem::path downloads_directory() const;
        [[nodiscard]] std::filesystem::path staging_directory() const;
        [[nodiscard]] std::filesystem::path versions_directory() const;
        [[nodiscard]] std::filesystem::path launcher_versions_directory() const;
        [[nodiscard]] std::filesystem::path launcher_staging_directory() const;
        [[nodiscard]] std::filesystem::path launcher_state_file() const;
        [[nodiscard]] std::filesystem::path launcher_transaction_file() const;
        [[nodiscard]] std::filesystem::path desktop_shortcut() const;
        [[nodiscard]] std::filesystem::path start_menu_directory() const;
        [[nodiscard]] std::filesystem::path start_menu_shortcut() const;
        [[nodiscard]] std::filesystem::path operations_root() const;
        [[nodiscard]] std::filesystem::path operations_marker() const;
        [[nodiscard]] std::filesystem::path uninstall_record_file() const;
        [[nodiscard]] std::filesystem::path helpers_directory() const;
        [[nodiscard]] std::filesystem::path version_root(
            std::string_view versionId) const;
        [[nodiscard]] std::filesystem::path version_executable(
            std::string_view versionId) const;
        [[nodiscard]] std::filesystem::path launcher_version_root(
            std::string_view hash) const;
        [[nodiscard]] std::filesystem::path launcher_executable(
            std::string_view hash) const;
        [[nodiscard]] std::filesystem::path launcher_package_marker(
            std::string_view hash) const;
    };

    class DownloadManager final
    {
    public:
        DownloadManager();
        ~DownloadManager();
        DownloadManager(const DownloadManager&) = delete;
        DownloadManager& operator=(const DownloadManager&) = delete;

        [[nodiscard]] std::vector<std::byte> bytes(
            std::wstring_view url,
            std::uint64_t maximumBytes,
            std::string_view phase,
            const ProgressCallback& progress,
            const LogCallback& log,
            const Cancellation& cancellation);
        void file(
            std::wstring_view url,
            const std::filesystem::path& destination,
            std::uint64_t expectedSize,
            std::string_view expectedSha256,
            std::uint64_t maximumBytes,
            std::string_view phase,
            const ProgressCallback& progress,
            const LogCallback& log,
            const Cancellation& cancellation,
            const std::function<void(const std::filesystem::path&)>& validator = {});
        void cancel() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class InstallerEngine final
    {
    public:
        explicit InstallerEngine(InstallerPaths paths);
        ~InstallerEngine();
        InstallerEngine(const InstallerEngine&) = delete;
        InstallerEngine& operator=(const InstallerEngine&) = delete;

        static void ensure_supported_platform();
        [[nodiscard]] InstallSnapshot inspect();
        [[nodiscard]] ExactProcessInspection inspect_renderer_processes() const;
        void ensure_launcher_ready(
            bool desktopShortcut,
            const ProgressCallback& progress,
            const LogCallback& log,
            const Cancellation& cancellation);
        [[nodiscard]] UpdateCheckResult check_for_updates(
            bool desktopShortcut,
            const ProgressCallback& progress,
            const LogCallback& log,
            const Cancellation& cancellation);
        [[nodiscard]] OperationResult update_launcher(
            const LauncherFeed& feed,
            bool desktopShortcut,
            bool continueUvsrUpdate,
            const ProgressCallback& progress,
            const LogCallback& log,
            const Cancellation& cancellation);
        [[nodiscard]] OperationResult execute(
            InstallerOperation operation,
            bool desktopShortcut,
            const ProgressCallback& progress,
            const LogCallback& log,
            const Cancellation& cancellation);
        [[nodiscard]] bool desktop_shortcut_preference();
        [[nodiscard]] std::optional<GUID> pending_launcher_continuation(
            const std::optional<GUID>& requested);
        void complete_launcher_continuation(const GUID& transactionId);
        void launch_installed();
        void cancel_download() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    [[nodiscard]] std::wstring widen(std::string_view value);
    [[nodiscard]] std::string narrow(std::wstring_view value);
    [[nodiscard]] std::string guid_string(const GUID& value);
    [[nodiscard]] GUID parse_guid(std::string_view value);
    [[nodiscard]] bool guid_equal(const GUID& left, const GUID& right) noexcept;
    [[nodiscard]] std::string sha256(const std::filesystem::path& path);
    [[nodiscard]] std::string sha256(std::span<const std::byte> bytes);
    [[nodiscard]] bool is_lower_hex(std::string_view value, std::size_t length);
    [[nodiscard]] bool is_stable_version(std::string_view value);
    [[nodiscard]] bool is_engine_version(std::string_view value);
    [[nodiscard]] bool is_version_id(std::string_view value);
    [[nodiscard]] bool is_strict_descendant(
        const std::filesystem::path& candidate,
        const std::filesystem::path& parent);
    void reject_reparse_chain(
        const std::filesystem::path& path,
        std::string_view description);
    void delete_owned_tree(
        const std::filesystem::path& path,
        const std::filesystem::path& parent);
    [[nodiscard]] std::string resource_text(int id);
    [[nodiscard]] std::vector<std::byte> resource_bytes(int id);
    void ensure_fonts();
    void verify_launcher_authenticode_policy(const std::filesystem::path& path);
    void validate_pe_x64(const std::filesystem::path& path);
    void validate_launcher_metadata(
        const std::filesystem::path& path,
        std::string_view version,
        std::optional<std::string_view> sourceCommit = std::nullopt);

    [[nodiscard]] LauncherFeed verify_launcher_feed(
        std::span<const std::byte> envelope,
        std::span<const std::byte> publicKey = {},
        std::string_view keyId = {});
    [[nodiscard]] RendererFeed verify_renderer_feed(
        std::span<const std::byte> envelope,
        std::span<const std::byte> publicKey = {},
        std::string_view keyId = {});
    void validate_launcher_feed(const LauncherFeed& feed);
    void validate_renderer_feed(const RendererFeed& feed);
    [[nodiscard]] std::wstring launcher_artifact_url(const LauncherFeed& feed);
    [[nodiscard]] std::wstring renderer_artifact_url(const RendererFeed& feed);
    [[nodiscard]] ComponentUpdateState classify_launcher_update(
        std::int64_t currentSequence,
        std::string_view currentVersion,
        std::string_view currentHash,
        bool recordedStateHealthy,
        const LauncherFeed& available);
    [[nodiscard]] ComponentUpdateState classify_renderer_update(
        const InstallSnapshot& current,
        const RendererFeed& available);

    [[nodiscard]] PackageManifest read_package_manifest(
        const std::filesystem::path& packageRoot);
    void validate_renderer_package(
        const std::filesystem::path& packageRoot,
        const PackageManifest& manifest);
    [[nodiscard]] PackageManifest stage_renderer_archive_for_test(
        const std::filesystem::path& archive,
        const std::filesystem::path& packageRoot,
        const RendererFeed& feed);

    [[nodiscard]] ExactProcessInspection inspect_processes_by_executable(
        const std::filesystem::path& executable);
    [[nodiscard]] ExactProcessInspection inspect_managed_processes(
        std::wstring_view executableName,
        const std::filesystem::path& root,
        bool excludeCurrent = false);
    [[nodiscard]] ProcessState inspect_exact_process(
        const ExactProcessIdentity& identity);
    [[nodiscard]] bool close_exact_process(const ExactProcessIdentity& identity);
    [[nodiscard]] bool terminate_exact_process(const ExactProcessIdentity& identity);

    [[nodiscard]] RendererClickAction reconcile_renderer_click(
        RendererButtonIntent rendered,
        ProcessState current) noexcept;
    [[nodiscard]] TerminalUiState renderer_active_state(RendererBusyAction action);
    [[nodiscard]] TerminalUiState renderer_success_state(RendererBusyAction action);
    [[nodiscard]] TerminalUiState update_terminal_state(
        UpdateFlowExit exit,
        std::wstring upToDateDetail = {});
    [[nodiscard]] std::wstring install_button_text(const InstallSnapshot& snapshot);

    [[nodiscard]] int run_ui(
        HINSTANCE instance,
        InstallerEngine& engine,
        bool startWithUninstall,
        std::optional<GUID> continuationId);
    [[nodiscard]] int run_cleanup_helper(
        DWORD parentProcessId,
        std::uint64_t parentCreationTime,
        const GUID& installationId,
        const GUID& transactionId);
    [[nodiscard]] bool schedule_current_helper_deletion(
        const GUID& installationId);
    void recover_interrupted_uninstall(const InstallerPaths& paths);
    void remove_stale_helpers(const InstallerPaths& paths);
    [[nodiscard]] bool redirect_to_active_launcher(
        const InstallerPaths& paths,
        const OwnerMarker& marker,
        const std::vector<std::wstring>& arguments);
    [[nodiscard]] std::optional<OwnerMarker> inspect_owner(
        const InstallerPaths& paths);

    [[nodiscard]] int verify_renderer_archive_command(
        const std::vector<std::wstring>& arguments);
    [[nodiscard]] int verify_production_services_command(
        const std::vector<std::wstring>& arguments);
}
