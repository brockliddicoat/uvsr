#include "installer_internal.h"
#include <algorithm>
#include <map>

namespace uvsr::launcher
{
    void Installer::ValidateLauncher(const Json& state, std::string_view installation) const
    {
        ValidateState(state, installation, Component::Launcher);
        const auto hash = Text(state, "executableSha256");
        const auto root = paths.Launcher(hash);
        const auto marker = ReadRecord(root / LauncherPackageName);
        const auto markerState = LauncherStateFromMarker(marker, Flag(state, "desktopShortcut"));
        ValidateState(markerState, installation, Component::Launcher); SameLauncherIdentity(markerState, state);
        Require(Number(marker, "executableSize") > 0 && uint64_t(Number(marker, "executableSize")) <= MaximumLauncherBytes, "The launcher package size is invalid.");
        std::set<std::string> names;
        for (const auto& entry : fs::directory_iterator(root))
        {
            RejectReparseChain(entry.path());
            auto name = Utf8(entry.path().filename().wstring());
            Require(entry.is_regular_file() && (name == LauncherName || name == LauncherPackageName) && names.emplace(name).second,
                "The launcher package contains unexpected or modified files. It was preserved.");
        }
        Require(names.size() == 2, "The launcher package is incomplete.");
        VerifyFile(root / LauncherName, uint64_t(Number(marker, "executableSize")), hash);
        ValidateLauncherMetadata(root / LauncherName, Text(state, "version"));
    }
    LauncherInspection Installer::InspectLauncher(std::string_view installation, bool migrate) const
    {
        LauncherInspection inspection; std::map<int64_t, Json> identities;
        const auto remember = [&](const Json& state)
        {
            auto [found, inserted] = identities.emplace(Number(state, "releaseSequence"), state);
            if (!inserted) SameLauncherIdentity(found->second, state);
        };
        try { inspection.recorded = ReadState(paths.state / "launcher-state.json", installation, Component::Launcher); }
        catch (const std::exception& error)
        { inspection.unreadable = Unverifiable(error); inspection.malformed = !inspection.unreadable; inspection.problem = error.what(); }
        if (inspection.recorded)
        {
            remember(*inspection.recorded);
            try { ValidateLauncher(*inspection.recorded, installation); inspection.valid = inspection.recorded; }
            catch (const std::exception& error) { inspection.problem = error.what(); }
        }
        RejectReparseChain(paths.LauncherVersions());
        if (fs::exists(paths.LauncherVersions()))
            for (const auto& directory : fs::directory_iterator(paths.LauncherVersions()))
            {
                const auto hash = Utf8(directory.path().filename().wstring());
                if (!IsLowerHex(hash, 64)) continue;
                std::optional<Json> candidate;
                try
                {
                    RejectReparseChain(directory.path());
                    auto marker = ReadRecord(directory.path() / LauncherPackageName);
                    candidate = LauncherStateFromMarker(marker, inspection.recorded ? Flag(*inspection.recorded, "desktopShortcut") : true);
                    ValidateState(*candidate, installation, Component::Launcher);
                    Require(Text(*candidate, "executableSha256") == hash, "The launcher package does not match its directory hash.");
                    if (migrate && !fs::exists(directory.path() / LauncherName))
                    {
                        const fs::path old = directory.path() / "UVSR Launcher.exe";
                        if (fs::exists(old))
                        {
                            std::set<std::string> names;
                            for (const auto& file : fs::directory_iterator(directory.path()))
                            { RejectReparseChain(file.path()); Require(file.is_regular_file(), "The old launcher package contains an unexpected member."); names.emplace(Utf8(file.path().filename().wstring())); }
                            Require(names == std::set<std::string>{"UVSR Launcher.exe", LauncherPackageName}, "The old launcher package is not the exact supported migration.");
                            VerifyFile(old, uint64_t(Number(marker, "executableSize")), hash);
                            ValidateLauncherMetadata(old, Text(*candidate, "version"));
                            WinCheck(MoveFileExW(old.c_str(), (directory.path() / LauncherName).c_str(), MOVEFILE_WRITE_THROUGH), "Migrate the installed launcher filename");
                        }
                    }
                    ValidateLauncher(*candidate, installation);
                }
                catch (const std::exception& error) { inspection.problem = error.what(); continue; }
                // repair references even when an earlier run already renamed the executable.
                if (migrate) shell.MigrateLegacyShortcuts(installation, *candidate);
                remember(*candidate);
                if (!inspection.valid || Number(*candidate, "releaseSequence") > Number(*inspection.valid, "releaseSequence")) inspection.valid = candidate;
            }
        if (!identities.empty()) inspection.highest = identities.rbegin()->second;
        return inspection;
    }
    Json Installer::StageLauncher(std::string_view installation, const fs::path& source, int64_t sequence,
        std::string_view version, bool desktop, std::stop_token stop)
    {
        CheckCancelled(stop); ValidateLauncherMetadata(source, version);
        const auto hash = HashFile(source), time = UtcNow();
        const auto size = fs::file_size(source);
        Require(size > 0 && size <= MaximumLauncherBytes, "The launcher executable size is invalid.");
        Json state = JObject({{"schemaVersion", JNumber(1)}, {"productId", JString(ProductId)}, {"installationId", JString(std::string(installation))},
            {"releaseSequence", JNumber(sequence)}, {"version", JString(std::string(version))}, {"executableSha256", JString(hash)},
            {"desktopShortcut", JBool(desktop)}, {"installedUtc", JString(time)}});
        ValidateState(state, installation, Component::Launcher);
        const auto destination = paths.Launcher(hash);
        if (fs::exists(destination))
            try { ValidateLauncher(state, installation); return state; }
            catch (const std::exception& error) { if (Unverifiable(error)) throw; }
        CreateDirectories(paths.program / "launcher/staging"); CreateDirectories(paths.LauncherVersions());
        const auto stage = Descendant(paths.program / "launcher/staging", CompactGuid(Guid())); CreateDirectories(stage);
        const auto executable = stage / LauncherName;
        WinCheck(CopyFileW(source.c_str(), executable.c_str(), TRUE), "Stage the native launcher");
        VerifyFile(executable, size, hash); ValidateLauncherMetadata(executable, version);
        auto marker = JObject({{"schemaVersion", JNumber(1)}, {"productId", JString(ProductId)}, {"installationId", JString(std::string(installation))},
            {"releaseSequence", JNumber(sequence)}, {"version", JString(std::string(version))}, {"executableSha256", JString(hash)},
            {"executableSize", JNumber(int64_t(size))}, {"installedUtc", JString(time)}});
        WriteRecord(stage / LauncherPackageName, marker);
        CheckCancelled(stop);
        Require(services.health(executable, sequence, version, stop) == 0, "The downloaded launcher did not pass its health check.");
        CheckCancelled(stop);
        std::optional<fs::path> preserved;
        if (fs::exists(destination))
        {
            Require(services.processes(destination, Component::Launcher, false).Stopped(), "The damaged launcher package is still running or cannot be verified. It was preserved.");
            preserved = paths.LauncherVersions() / (hash + ".preserved-" + CompactGuid(Guid()));
            RejectReparseChain(*preserved); fs::rename(destination, *preserved);
        }
        try { fs::rename(stage, destination); ValidateLauncher(state, installation); }
        catch (...)
        { if (preserved && !fs::exists(destination)) fs::rename(*preserved, destination); throw; }
        return state;
    }
    std::string Installer::ActivateLauncher(std::string_view installation, const Json& requested,
        const std::optional<Json>& renderer, bool continuation, const Report& report)
    {
        Json candidate = requested; ValidateLauncher(candidate, installation);
        auto existing = InspectLauncher(installation);
        Require(!existing.unreadable, "The installed launcher state is temporarily unavailable. No activation was changed.");
        if (existing.highest)
        {
            Require(Number(candidate, "releaseSequence") >= Number(*existing.highest, "releaseSequence"), "UVSR Launcher refused to activate an older release.");
            if (Number(candidate, "releaseSequence") == Number(*existing.highest, "releaseSequence")) SameLauncherIdentity(candidate, *existing.highest);
        }
        const auto journalPath = paths.state / "launcher-update.json";
        if (fs::exists(journalPath))
        {
            auto pending = ReadRecord(journalPath); ValidateLauncherJournal(pending, installation);
            Require(Text(pending, "phase") == "awaiting-continuation" && Flag(pending, "continueUvsrUpdate"), "An earlier launcher update must finish recovery first.");
            SameLauncherIdentity(Member(pending, "candidateState"), candidate);
            Set(pending, "candidateState", candidate); WriteRecord(journalPath, pending);
            WriteRecord(paths.state / "launcher-state.json", candidate);
            shell.Apply(installation, renderer, candidate, Text(pending, "transactionId"), [&](const Progress& progress) { Log(progress.detail, report); });
            return Text(pending, "transactionId");
        }
        const auto transaction = Guid();
        Json journal = JObject({{"schemaVersion", JNumber(1)}, {"productId", JString(ProductId)}, {"installationId", JString(std::string(installation))},
            {"transactionId", JString(transaction)}, {"phase", JString("prepared")}, {"previousState", Nullable(existing.valid)},
            {"candidateState", candidate}, {"continueUvsrUpdate", JBool(continuation)}, {"startedUtc", JString(UtcNow())}});
        WriteRecord(journalPath, journal); Checkpoint("launcher-prepared");
        WriteRecord(paths.state / "launcher-state.json", candidate);
        Set(journal, "phase", JString("state-activated")); WriteRecord(journalPath, journal); Checkpoint("launcher-state-activated");
        shell.Apply(installation, renderer, candidate, transaction, [&](const Progress& progress) { Log(progress.detail, report); });
        Set(journal, "phase", JString("shell-committed")); WriteRecord(journalPath, journal); Checkpoint("launcher-shell-committed");
        if (continuation) { Set(journal, "phase", JString("awaiting-continuation")); WriteRecord(journalPath, journal); }
        else fs::remove(journalPath);
        Log("Activated UVSR Launcher " + Text(candidate, "version") + ".", report);
        return transaction;
    }
    void Installer::RecoverLauncher(std::string_view installation, const std::optional<Json>& renderer, const Report& report)
    {
        const auto path = paths.state / "launcher-update.json";
        if (!fs::exists(path)) return;
        auto journal = ReadRecord(path); ValidateLauncherJournal(journal, installation);
        auto previous = Optional(Member(journal, "previousState"));
        const Json& candidate = Member(journal, "candidateState");
        const auto status = [&](const std::optional<Json>& state)
        {
            if (!state) return PackageStatus::Missing;
            try { ValidateLauncher(*state, installation); return PackageStatus::Valid; }
            catch (const std::exception& error) { return Unverifiable(error) ? PackageStatus::Unverifiable : PackageStatus::Invalid; }
        };
        auto action = DecideRecovery(Text(journal, "phase"), status(previous), status(candidate));
        Require(action != RecoveryAction::RetryLater, "The interrupted launcher packages are temporarily unavailable. The recovery record was preserved.");
        if (action == RecoveryAction::ClearBrokenJournal)
        {
            fs::remove(path);
            throw std::runtime_error("Both packages from an interrupted launcher update are damaged. The newest UVSR Launcher can now repair the installation.");
        }
        if (action == RecoveryAction::RollBack)
        {
            RestoreState(paths.state / "launcher-state.json", previous);
            if (previous) shell.Apply(installation, renderer, *previous, Text(journal, "transactionId"), [&](const Progress& progress) { Log(progress.detail, report); });
            else shell.Remove(installation);
            fs::remove(path); Log("Rolled back the interrupted launcher activation.", report); return;
        }
        WriteRecord(paths.state / "launcher-state.json", candidate);
        shell.Apply(installation, renderer, candidate, Text(journal, "transactionId"), [&](const Progress& progress) { Log(progress.detail, report); });
        if (Flag(journal, "continueUvsrUpdate") && Text(journal, "phase") != "continuation-complete")
        { Set(journal, "phase", JString("awaiting-continuation")); WriteRecord(path, journal); }
        else fs::remove(path);
        Log("Finished the interrupted launcher activation.", report);
    }
    Json Installer::EnsureLauncher(std::string_view installation, const std::optional<Json>& renderer, bool desktop, std::stop_token stop, const Report& report)
    {
        auto existing = InspectLauncher(installation, true);
        Require(!existing.unreadable, "The installed launcher record is temporarily unavailable. No launcher files were changed.");
        if (existing.valid && Number(*existing.valid, "releaseSequence") >= LauncherSequence)
        {
            auto candidate = *existing.valid; Set(candidate, "desktopShortcut", JBool(desktop));
            if (!existing.recorded || Serialize(candidate) != Serialize(*existing.recorded)) ActivateLauncher(installation, candidate, renderer, false, report);
            return candidate;
        }
        Require(!existing.highest || Number(*existing.highest, "releaseSequence") <= LauncherSequence,
            "A newer installed UVSR Launcher needs repair. Download the newest UVSR Launcher before changing UVSR.");
        auto candidate = StageLauncher(installation, services.executable, LauncherSequence, LauncherVersion, desktop, stop);
        ActivateLauncher(installation, candidate, renderer, false, report); return candidate;
    }
    bool Installer::Redirect(std::span<const std::wstring> arguments) const
    {
        OperationLock operation(Wide(lockSuffix));
        auto owner = InspectOwnership(paths); if (!owner) return false;
        std::optional<Json> state;
        try { state = ReadState(paths.state / "launcher-state.json", *owner, Component::Launcher); if (!state) return false; ValidateLauncher(*state, *owner); }
        catch (...) { return false; }
        auto executable = paths.Launcher(Text(*state, "executableSha256")) / LauncherName;
        if (!ShouldRedirect(LauncherSequence, services.executable, Number(*state, "releaseSequence"), executable, arguments)) return false;
        services.start(executable, arguments, false); return true;
    }
    std::optional<std::string> Installer::PendingContinuation(std::optional<std::string_view> requested) const
    {
        OperationLock operation(Wide(lockSuffix));
        const auto owner = InspectOwnership(paths); const auto path = paths.state / "launcher-update.json";
        if (!owner || !fs::exists(path)) return {};
        auto journal = ReadRecord(path); ValidateLauncherJournal(journal, *owner);
        if (Text(journal, "phase") != "awaiting-continuation" || !Flag(journal, "continueUvsrUpdate") ||
            (requested && *requested != Text(journal, "transactionId"))) return {};
        const auto& candidate = Member(journal, "candidateState"); ValidateLauncher(candidate, *owner);
        if (!SamePath(services.executable, paths.Launcher(Text(candidate, "executableSha256")) / LauncherName)) return {};
        return Text(journal, "transactionId");
    }
    void Installer::CompleteContinuation(std::string_view transaction)
    {
        OperationLock operation(Wide(lockSuffix));
        const auto owner = InspectOwnership(paths); Require(owner.has_value(), "The continuation no longer has an owned installation.");
        const auto path = paths.state / "launcher-update.json";
        if (!fs::exists(path)) return;
        auto journal = ReadRecord(path); ValidateLauncherJournal(journal, *owner);
        Require(Text(journal, "transactionId") == transaction && Text(journal, "phase") == "awaiting-continuation" && Flag(journal, "continueUvsrUpdate"), "The pending renderer update does not match this launcher.");
        Set(journal, "phase", JString("continuation-complete")); WriteRecord(path, journal); fs::remove(path);
    }
}
