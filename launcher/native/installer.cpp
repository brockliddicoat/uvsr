#include "installer_internal.h"
#include <fstream>

namespace uvsr::launcher
{
    Installer::Installer(Paths p, Services s, std::string publicKey, std::string trustId, std::wstring registryPath, std::string suffix)
        : paths(std::move(p)), shell(paths, std::move(registryPath)), services(std::move(s)),
          key(std::move(publicKey)), keyId(std::move(trustId)), lockSuffix(std::move(suffix)) {}
    void Installer::Checkpoint(std::string_view phase) const { if (services.checkpoint) services.checkpoint(phase); }
    void Installer::Log(std::string_view message, const Report& report) const
    {
        if (fs::exists(paths.state / OwnerName))
        {
            const auto file = paths.state / "logs/operations.log"; CreateDirectories(file.parent_path()); RejectReparseChain(file);
            std::ofstream output(file, std::ios::binary | std::ios::app);
            if (output) output << UtcNow() << ' ' << message << '\n';
        }
        if (report) report({"", std::string(message), {}, true});
    }
    void Installer::ValidateRendererState(const Json& state, std::string_view installation) const
    {
        ValidateState(state, installation, Component::Renderer);
        const auto package = ValidatePackage(paths.Renderer(Text(state, "activeVersionId")));
        const auto& manifest = package.manifest;
        Require(Number(manifest, "releaseSequence") == Number(state, "releaseSequence") && Text(manifest, "sourceCommit") == Text(state, "commit") &&
            Text(manifest, "settingsHash") == Text(state, "settingsHash") && Text(manifest, "engineVersion") == Text(state, "engineVersion") &&
            HashEqual(Text(manifest, "executableSha256"), Text(state, "executableSha256")), "The active renderer package does not match its installed state.");
    }
    Snapshot Installer::Inspect() const
    {
        Snapshot snapshot; snapshot.installation = InspectOwnership(paths);
        if (!snapshot.installation) return snapshot;
        snapshot.summary = "UVSR setup is ready to continue. No active version is installed.";
        const auto statePath = paths.state / "state.json";
        if (!fs::exists(statePath)) return snapshot;
        snapshot.installed = true;
        try
        {
            snapshot.state = ReadState(statePath, *snapshot.installation, Component::Renderer);
            Require(snapshot.state.has_value(), "The active renderer record is missing.");
            ValidateRendererState(*snapshot.state, *snapshot.installation);
            snapshot.executable = paths.Renderer(Text(*snapshot.state, "activeVersionId")) / "bin" / EngineName;
            snapshot.summary = "UVSR Engine " + Text(*snapshot.state, "engineVersion") + " is ready; settings " + Text(*snapshot.state, "settingsHash") + ".";
        }
        catch (const std::exception&)
        { snapshot.damaged = true; snapshot.summary = "UVSR needs to be reinstalled before it can launch."; }
        return snapshot;
    }
    bool Installer::DesktopPreference() const
    {
        auto snapshot = Inspect();
        if (!snapshot.installation) return true;
        try
        {
            auto launcher = InspectLauncher(*snapshot.installation);
            if (launcher.valid) return Flag(*launcher.valid, "desktopShortcut");
            if (launcher.recorded) return Flag(*launcher.recorded, "desktopShortcut");
        }
        catch (...) {}
        return snapshot.state ? Flag(*snapshot.state, "desktopShortcut") : true;
    }
    void Installer::Ready(bool desktop, std::stop_token stop, const Report& report)
    {
        services.platform(); OperationLock operation(Wide(lockSuffix));
        auto owner = InspectOwnership(paths); if (!owner) return;
        EnsureOwnership(paths);
        auto snapshot = Inspect(); auto renderer = snapshot.damaged ? std::nullopt : snapshot.state;
        RecoverLauncher(*owner, renderer, report);
        auto launcher = EnsureLauncher(*owner, renderer, desktop, stop, report);
        RecoverRenderer(*owner, launcher, report);
        if (report) report({"Ready", "UVSR Launcher is ready.", 100});
    }
    Updates Installer::CheckUpdates(bool desktop, std::stop_token stop, const Report& report)
    {
        services.platform(); OperationLock operation(Wide(lockSuffix));
        const auto owner = EnsureOwnership(paths);
        auto snapshot = Inspect();
        RecoverLauncher(owner, snapshot.damaged ? std::nullopt : snapshot.state, report);
        try { EnsureLauncher(owner, snapshot.damaged ? std::nullopt : snapshot.state, desktop, stop, report); }
        catch (const std::exception& error) { CheckCancelled(stop); Log(std::string("Local launcher repair deferred until update selection: ") + error.what(), report); }
        snapshot = Inspect();
        Updates updates;
        for (const auto component : {Component::Renderer, Component::Launcher})
        {
            auto& result = component == Component::Renderer ? updates.renderer : updates.launcher;
            try
            {
                CheckCancelled(stop);
                const auto feedPath = paths.state / "downloads" / (component == Component::Renderer ? "renderer-feed.json" : "launcher-feed.json");
                services.download(component == Component::Renderer ? RendererFeedUrl : LauncherFeedUrl, feedPath, 16384, {}, stop, report);
                result.feed = VerifyFeed(ReadFile(feedPath, 16384), component, key, keyId);
                const auto& feed = *result.feed;
                result.available = feed.version;
                if (component == Component::Renderer)
                {
                    result.state = Classify(snapshot.state, !snapshot.damaged, feed);
                    if (snapshot.state) result.current = Text(*snapshot.state, "engineVersion");
                }
                else
                {
                    const auto inspection = InspectLauncher(owner);
                    Require(!inspection.unreadable, "The installed launcher identity is temporarily unavailable.");
                    auto current = inspection.highest;
                    if (!current || Number(*current, "releaseSequence") < LauncherSequence)
                        current = JObject({{"releaseSequence", JNumber(LauncherSequence)}, {"version", JString(LauncherVersion)}, {"executableSha256", JString(HashFile(services.executable))}});
                    result.current = Text(*current, "version");
                    bool healthy = inspection.valid && Number(*inspection.valid, "releaseSequence") == Number(*current, "releaseSequence") && HashEqual(Text(*inspection.valid, "executableSha256"), Text(*current, "executableSha256"));
                    result.state = Classify(current, healthy, feed);
                }
                switch (result.state)
                {
                case UpdateState::NotInstalled: result.detail = "Not installed"; break;
                case UpdateState::Current: result.detail = "Up to date"; break;
                case UpdateState::UpdateAvailable: result.detail = "Update available"; break;
                case UpdateState::RepairNeeded: result.detail = "Repair needed"; break;
                default: break;
                }
            }
            catch (const std::exception& error)
            { CheckCancelled(stop); result.state = UpdateState::CheckFailed; result.detail = error.what(); result.feed.reset(); Log(error.what(), report); }
        }
        return updates;
    }
    Result Installer::UpdateLauncher(const Feed& feed, bool desktop, bool continuation, std::stop_token stop, const Report& report)
    {
        services.platform(); OperationLock operation(Wide(lockSuffix));
        Require(feed.component == Component::Launcher, "The selected update is not a launcher feed.");
        auto owner = EnsureOwnership(paths); auto snapshot = Inspect(); auto renderer = snapshot.damaged ? std::nullopt : snapshot.state;
        RecoverLauncher(owner, renderer, report);
        auto inspection = InspectLauncher(owner);
        Require(!inspection.unreadable, "The installed launcher identity is unavailable.");
        if (inspection.highest)
        {
            Require(feed.sequence >= Number(*inspection.highest, "releaseSequence"), "UVSR Launcher refused to download an older release.");
            (void)Classify(inspection.highest, inspection.valid.has_value(), feed);
        }
        Require(feed.sequence >= LauncherSequence, "UVSR Launcher refused to replace the running version with an older release.");
        auto download = Descendant(paths.state / "downloads/launcher", feed.hash) / LauncherName;
        services.download(ArtifactUrl(feed), download, feed.size, feed.hash, stop, report);
        VerifyFile(download, feed.size, feed.hash); ValidateLauncherMetadata(download, feed.version, feed.commit);
        auto candidate = StageLauncher(owner, download, feed.sequence, feed.version, desktop, stop);
        Require(HashEqual(Text(candidate, "executableSha256"), feed.hash), "The staged launcher no longer matches its signed feed.");
        CheckCancelled(stop); if (report) report({"Updating UVSR Launcher", "Activating the verified launcher.", {}, false});
        auto transaction = ActivateLauncher(owner, candidate, renderer, continuation, report);
        if (report) report({"UVSR Launcher updated", "UVSR Launcher " + feed.version + " is ready.", 100, false});
        return {"UVSR Launcher " + feed.version + " was updated successfully.", paths.Launcher(feed.hash) / LauncherName,
            continuation ? std::optional<std::string>(transaction) : std::nullopt};
    }
    namespace
    {
        void ValidateRendererJournal(const Json& journal, std::string_view owner)
        {
            RequireExactObject(journal, {"schemaVersion", "installationId", "transactionId", "operation", "phase", "candidateVersionId", "previousState", "startedUtc"}, "renderer transaction");
            const auto phase = Text(journal, "phase"); auto op = Number(journal, "operation");
            Require(Number(journal, "schemaVersion") == 1 && Text(journal, "installationId") == owner && IsGuid(Text(journal, "transactionId")) && op >= 0 && op <= 3 &&
                (phase == "download" || phase == "package" || phase == "activate" || phase == "shell-activation" || phase == "shell-update" || phase == "uninstall-pending"), "The interrupted renderer transaction has an invalid ownership record.");
            if (auto previous = Optional(Member(journal, "previousState"))) ValidateState(*previous, owner, Component::Renderer);
            const auto& candidate = Member(journal, "candidateVersionId");
            Require(candidate.kind == Json::Kind::Null || (candidate.kind == Json::Kind::String && IsVersionId(candidate.string)), "The interrupted renderer candidate is invalid.");
            (void)Text(journal, "startedUtc");
        }
    }
    void Installer::RecoverRenderer(std::string_view owner, const Json& launcher, const Report& report)
    {
        const auto path = paths.state / "transaction.json";
        if (!fs::exists(path)) return;
        const auto journal = ReadRecord(path); ValidateRendererJournal(journal, owner);
        if (Number(journal, "operation") == int(Operation::Uninstall))
        {
            Require(!fs::exists(paths.operations / "uninstall.json"), "An earlier uninstall is waiting for its cleanup helper. Close the earlier launcher window, then reopen UVSR Launcher.");
            fs::remove(path); return;
        }
        const auto phase = Text(journal, "phase");
        if (phase == "download" || phase == "package")
        {
            shell.RemoveStaged(Text(journal, "transactionId")); fs::remove(path);
            Log("The interrupted package stage remained inactive. Repair can retry with a new transaction.", report); return;
        }
        std::optional<Json> current;
        try { current = ReadState(paths.state / "state.json", owner, Component::Renderer); }
        catch (const std::exception& error) { if (Unverifiable(error)) throw; }
        bool keep = false;
        const auto& candidate = Member(journal, "candidateVersionId");
        if (current && (phase == "shell-update" || (candidate.kind == Json::Kind::String && Text(*current, "activeVersionId") == candidate.string)))
            try { ValidateRendererState(*current, owner); shell.Apply(owner, current, launcher, Text(journal, "transactionId"), [&](const Progress& progress) { Log(progress.detail, report); }); keep = true; }
            catch (const std::exception& error) { if (Unverifiable(error)) throw; }
        if (!keep)
        {
            auto previous = Optional(Member(journal, "previousState")); RestoreState(paths.state / "state.json", previous);
            bool healthy = false;
            if (previous) try { ValidateRendererState(*previous, owner); healthy = true; }
                catch (const std::exception& error) { if (Unverifiable(error)) throw; }
            shell.Apply(owner, healthy ? previous : std::nullopt, launcher, Text(journal, "transactionId"), [&](const Progress& progress) { Log(progress.detail, report); });
        }
        fs::remove(path); Log("Recovered the interrupted renderer activation.", report);
    }
    Result Installer::Execute(Operation requested, bool desktop, std::stop_token stop, const Report& report)
    {
        services.platform(); OperationLock operation(Wide(lockSuffix)); CheckCancelled(stop);
        if (requested == Operation::Uninstall)
        { auto owner = InspectOwnership(paths); Require(owner.has_value(), "UVSR is not installed for this Windows user."); return ScheduleUninstall(*owner, report); }
        const auto owner = EnsureOwnership(paths);
        Require(!fs::exists(paths.operations / "uninstall.json"), "An uninstall is pending. Close earlier launcher windows and reopen UVSR Launcher.");
        auto snapshot = Inspect(); RecoverLauncher(owner, snapshot.damaged ? std::nullopt : snapshot.state, report);
        auto launcher = EnsureLauncher(owner, snapshot.damaged ? std::nullopt : snapshot.state, desktop, stop, report);
        RecoverRenderer(owner, launcher, report);
        const auto statePath = paths.state / "state.json", journalPath = paths.state / "transaction.json";
        const bool existed = fs::exists(statePath);
        const auto oldBytes = existed ? std::optional<std::string>(ReadFile(statePath, 65536)) : std::nullopt;
        std::optional<Json> previous;
        try { previous = ReadState(statePath, owner, Component::Renderer); }
        catch (const std::exception& error) { if (requested != Operation::Reinstall || Unverifiable(error)) throw; }
        Require(requested != Operation::Install || !existed, "UVSR is already installed. Choose Install again and select Reinstall.");
        Require(requested != Operation::Update || previous.has_value(), "UVSR is not installed yet. Choose Install.");
        shell.Validate(owner, desktop);
        const auto feedPath = paths.state / "downloads/renderer-feed.json";
        services.download(RendererFeedUrl, feedPath, 16384, {}, stop, report);
        const auto feed = VerifyFeed(ReadFile(feedPath, 16384), Component::Renderer, key, keyId);
        if (previous)
        {
            Require(feed.sequence >= Number(*previous, "releaseSequence"), "UVSR Launcher refused to replace a newer renderer with an older feed sequence.");
            (void)Classify(previous, true, feed);
        }
        ULARGE_INTEGER free{}; WinCheck(GetDiskFreeSpaceExW(paths.local.c_str(), &free, nullptr, nullptr), "Check installation space");
        Require(free.QuadPart >= feed.size * 2 + ((previous ? 2ull : 1ull) << 30), "UVSR needs more free space to download, unpack, and activate the renderer safely.");
        snapshot = Inspect();
        const bool shellOnly = requested == Operation::Update && !snapshot.damaged && previous && Classify(previous, true, feed) == UpdateState::Current;
        const auto transaction = Guid();
        Json journal = JObject({{"schemaVersion", JNumber(1)}, {"installationId", JString(owner)}, {"transactionId", JString(transaction)},
            {"operation", JNumber(int(requested))}, {"phase", JString(shellOnly ? "shell-update" : "download")},
            {"candidateVersionId", shellOnly ? Member(*previous, "activeVersionId") : Json{}}, {"previousState", Nullable(previous)}, {"startedUtc", JString(UtcNow())}});
        WriteRecord(journalPath, journal); Checkpoint("renderer-download");
        bool activated = false, committed = false;
        Json candidate;
        try
        {
            if (shellOnly) { candidate = *previous; Set(candidate, "desktopShortcut", JBool(desktop)); }
            else
            {
                auto archive = paths.state / "downloads" / ("renderer-" + feed.hash + ".zip");
                services.download(ArtifactUrl(feed), archive, feed.size, feed.hash, stop, report);
                VerifyFile(archive, feed.size, feed.hash);
                const auto version = NewVersionId(feed.commit);
                Set(journal, "phase", JString("package")); Set(journal, "candidateVersionId", JString(version)); WriteRecord(journalPath, journal); Checkpoint("renderer-package");
                const auto stage = Descendant(paths.program / "staging", CompactGuid(transaction)); EnsureOwnedRoot(stage, owner);
                const auto root = stage / "package";
                ExtractPackage(archive, root, stop, report, &feed);
                auto package = ValidatePackage(root, &feed);
                CheckCancelled(stop); CreateDirectories(paths.Versions());
                const auto destination = paths.Renderer(version); Require(!fs::exists(destination), "The candidate renderer version already exists.");
                fs::rename(root, destination); (void)ValidatePackage(destination, &feed);
                fs::remove(stage / OwnerName); RemoveEmptyParents(stage, paths.program);
                candidate = JObject({{"schemaVersion", JNumber(1)}, {"installationId", JString(owner)}, {"activeVersionId", JString(version)},
                    {"releaseSequence", JNumber(feed.sequence)}, {"commit", JString(feed.commit)}, {"settingsHash", JString(feed.settingsHash)},
                    {"engineVersion", JString(feed.version)}, {"artifactSha256", JString(feed.hash)}, {"executableSha256", Member(package.manifest, "executableSha256")},
                    {"desktopShortcut", JBool(desktop)}, {"installedUtc", JString(UtcNow())}});
                Set(journal, "phase", JString("activate")); WriteRecord(journalPath, journal);
            }
            CheckCancelled(stop); if (report) report({"Installing UVSR Engine", "Activating the verified renderer and Windows shortcuts.", {}, false});
            WriteRecord(statePath, candidate); activated = true; Checkpoint("renderer-state-activated");
            Set(journal, "phase", JString(shellOnly ? "shell-update" : "shell-activation")); WriteRecord(journalPath, journal);
            shell.Apply(owner, candidate, launcher, transaction, [&](const Progress& progress) { Log(progress.detail, report); }); committed = true; Checkpoint("renderer-shell-committed");
        }
        catch (...)
        {
            auto failure = std::current_exception();
            if (activated && !committed)
            {
                if (oldBytes) WriteAtomic(statePath, *oldBytes); else { RejectReparseChain(statePath); fs::remove(statePath); }
                bool healthy = false;
                if (previous) try { ValidateRendererState(*previous, owner); healthy = true; } catch (...) {}
                shell.Apply(owner, healthy ? previous : std::nullopt, launcher, transaction, [&](const Progress& progress) { Log(progress.detail, report); });
            }
            if (!committed) { shell.RemoveStaged(transaction); fs::remove(journalPath); }
            std::rethrow_exception(failure);
        }
        try { fs::remove(journalPath); Sweep(owner, candidate, launcher, report); }
        catch (const std::exception& error) { Log(std::string("Cleanup was deferred: ") + error.what(), report); }
        const auto verb = requested == Operation::Install ? "installed" : requested == Operation::Reinstall ? "reinstalled" : "updated";
        const auto message = shellOnly ? "UVSR Engine is already at the newest trusted package." : "UVSR was " + std::string(verb) + " successfully.";
        if (report) report({"UVSR is ready", "The newest selected UVSR version is installed.", 100, false});
        Log(message, report); return {message};
    }
    void Installer::Launch() const
    {
        OperationLock operation(Wide(lockSuffix)); auto snapshot = Inspect();
        Require(snapshot.state && !snapshot.damaged && !snapshot.executable.empty(), "UVSR is not installed or needs repair. Choose Install to repair it.");
        Require(services.processes(paths.Versions(), Component::Renderer, false).Stopped(), "UVSR is already running or its process identity cannot be verified.");
        services.start(snapshot.executable, {}, false);
    }
}
