#pragma once
#include "core.h"

namespace uvsr::launcher
{
    struct ProcessIdentity { DWORD id = 0; uint64_t created = 0; fs::path executable; };
    struct Processes
    {
        std::vector<ProcessIdentity> found;
        bool uncertain = false;
        bool Stopped() const { return found.empty() && !uncertain; }
    };
    Processes InspectProcesses(const fs::path& root, Component component, bool excludeCurrent = false);
    bool SamePath(const fs::path& a, const fs::path& b);
    uint64_t ProcessStartTicks(HANDLE process);
    void StartProcess(const fs::path& executable, std::span<const std::wstring> arguments = {}, bool hidden = false);
    void CloseRenderer(const Paths& paths, const Report& report, bool force = false);
    void CloseProcesses(std::span<const ProcessIdentity> processes, const Report& report, bool force = false);
    int RunHealth(const fs::path& executable, int64_t sequence, std::string_view version, std::stop_token stop);

    class Shell
    {
        Paths paths;
        std::wstring keyPath;
    public:
        explicit Shell(Paths paths, std::wstring registryPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\UVSR");
        void Validate(std::string_view installation, bool desktop) const;
        void Apply(std::string_view installation, const std::optional<Json>& renderer, const Json& launcher,
            std::string_view transaction, const Report& report = {}) const;
        void Remove(std::string_view installation) const;
        void RemoveStaged(std::string_view transaction) const;
        bool OwnsShortcut(const fs::path& path, std::string_view installation) const;
        void MigrateLegacyShortcuts(std::string_view installation, const Json& launcher) const;
    };
    enum class PackageStatus { Missing, Valid, Invalid, Unverifiable };
    enum class RecoveryAction { RollBack, RollForward, RetryLater, ClearBrokenJournal };
    RecoveryAction DecideRecovery(std::string_view phase, PackageStatus previous, PackageStatus candidate);
    bool ShouldRedirect(int64_t currentSequence, const fs::path& current, int64_t installedSequence,
        const fs::path& installed, std::span<const std::wstring> arguments);
    struct LauncherInspection
    {
        std::optional<Json> recorded, valid, highest;
        bool malformed = false, unreadable = false;
        std::string problem;
    };
    struct Snapshot
    {
        std::optional<std::string> installation;
        std::optional<Json> state;
        bool installed = false, damaged = false;
        std::string summary = "UVSR is not installed for this Windows user.";
        fs::path executable;
    };
    struct ComponentUpdate
    {
        UpdateState state = UpdateState::CheckFailed;
        std::optional<Feed> feed;
        std::string current, available, detail;
    };
    struct Updates { ComponentUpdate renderer, launcher; };
    enum class Operation { Install = 0, Update = 1, Reinstall = 2, Uninstall = 3 };
    struct Result
    {
        std::string message;
        fs::path relaunch;
        std::optional<std::string> continuation;
        bool cleanupScheduled = false;
    };
    struct Services
    {
        std::function<void()> platform = EnsurePlatform;
        std::function<void(std::string_view, const fs::path&, uint64_t, std::optional<std::string_view>, std::stop_token, const Report&)> download = Download;
        std::function<int(const fs::path&, int64_t, std::string_view, std::stop_token)> health = RunHealth;
        std::function<Processes(const fs::path&, Component, bool)> processes = InspectProcesses;
        std::function<void(const fs::path&, std::span<const std::wstring>, bool)> start = StartProcess;
        std::function<void(std::string_view)> checkpoint;
        fs::path executable = CurrentExecutable();
    };
    class Installer
    {
        Paths paths;
        Shell shell;
        Services services;
        std::string key = PublicKey, keyId = KeyId;
        std::string lockSuffix;
        void Checkpoint(std::string_view phase) const;
        void Log(std::string_view message, const Report& report = {}) const;
        Json EnsureLauncher(std::string_view installation, const std::optional<Json>& renderer, bool desktop,
            std::stop_token stop, const Report& report);
        Json StageLauncher(std::string_view installation, const fs::path& source, int64_t sequence,
            std::string_view version, bool desktop, std::stop_token stop);
        std::string ActivateLauncher(std::string_view installation, const Json& candidate, const std::optional<Json>& renderer,
            bool continuation, const Report& report);
        void RecoverLauncher(std::string_view installation, const std::optional<Json>& renderer, const Report& report);
        void RecoverRenderer(std::string_view installation, const Json& launcher, const Report& report);
        void ValidateRendererState(const Json& state, std::string_view installation) const;
        void Sweep(std::string_view installation, const std::optional<Json>& renderer, const Json& launcher, const Report& report);
        Result ScheduleUninstall(std::string_view installation, const Report& report);
    public:
        explicit Installer(Paths paths, Services services = {}, std::string publicKey = PublicKey,
            std::string keyId = KeyId, std::wstring registryPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\UVSR",
            std::string lockSuffix = {});
        const Paths& GetPaths() const { return paths; }
        Snapshot Inspect() const;
        LauncherInspection InspectLauncher(std::string_view installation, bool migrate = false) const;
        void ValidateLauncher(const Json& state, std::string_view installation) const;
        bool DesktopPreference() const;
        void Ready(bool desktop, std::stop_token stop, const Report& report);
        Updates CheckUpdates(bool desktop, std::stop_token stop, const Report& report);
        Result UpdateLauncher(const Feed& feed, bool desktop, bool continuation, std::stop_token stop, const Report& report);
        Result Execute(Operation operation, bool desktop, std::stop_token stop, const Report& report);
        void Launch() const;
        bool Redirect(std::span<const std::wstring> arguments) const;
        std::optional<std::string> PendingContinuation(std::optional<std::string_view> requested) const;
        void CompleteContinuation(std::string_view transaction);
        int Cleanup(DWORD parentId, uint64_t parentTicks, std::string_view installation, std::string_view transaction, const Report& report = {});
        void RecoverUninstall(const Report& report = {});
        void CleanupHelpers(const Report& report = {});
    };
}
