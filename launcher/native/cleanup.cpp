#include "installer_internal.h"
#include <array>
#include <thread>

namespace uvsr::launcher
{
    void DeleteValidatedPackage(const Paths& paths, const fs::path& root, std::span<const PackageFile> files, std::string_view manifestName)
    {
        Require(IsDescendant(root, paths.program), "Package cleanup escaped the owned program directory.");
        RejectReparseChain(root);
        std::set<std::string> expected{std::string(manifestName)};
        for (const auto& file : files) { expected.emplace(file.path); VerifyFile(Descendant(root, file.path), file.size, file.hash); }
        for (const auto& entry : fs::recursive_directory_iterator(root))
        {
            RejectReparseChain(entry.path());
            if (entry.is_regular_file()) Require(expected.contains(Utf8(entry.path().lexically_relative(root).generic_wstring())), "A foreign package file was preserved.");
            else Require(entry.is_directory(), "A non-file package member was preserved.");
        }
        const auto marker = Descendant(root, manifestName);
        const auto markerHash = HashFile(marker);
        for (const auto& file : files)
        {
            const auto path = Descendant(root, file.path);
            VerifyFile(path, file.size, file.hash); fs::remove(path); RemoveEmptyParents(path.parent_path(), root);
        }
        Require(HashEqual(HashFile(marker), markerHash), "A changed package marker was preserved.");
        fs::remove(marker); RemoveEmptyParents(marker.parent_path(), paths.program);
    }
    void Installer::Sweep(std::string_view installation, const std::optional<Json>& renderer, const Json& launcher, const Report& report)
    {
        if (fs::exists(paths.state / "transaction.json") || fs::exists(paths.state / "launcher-update.json")) return;
        if (fs::exists(paths.Versions()))
            for (const auto& entry : fs::directory_iterator(paths.Versions()))
            {
                const auto version = Utf8(entry.path().filename().wstring());
                if (!IsVersionId(version) || (renderer && Text(*renderer, "activeVersionId") == version)) continue;
                try
                {
                    if (!services.processes(entry.path(), Component::Renderer, false).Stopped()) continue;
                    auto package = ValidatePackage(entry.path()); DeleteValidatedPackage(paths, entry.path(), package.files, PackageName);
                }
                catch (const std::exception& error) { Log(std::string("Preserved an inactive renderer package: ") + error.what(), report); }
            }
        if (fs::exists(paths.LauncherVersions()))
            for (const auto& entry : fs::directory_iterator(paths.LauncherVersions()))
            {
                const auto hash = Utf8(entry.path().filename().wstring());
                if (!IsLowerHex(hash, 64) || hash == Text(launcher, "executableSha256")) continue;
                try
                {
                    if (!services.processes(entry.path(), Component::Launcher, false).Stopped()) continue;
                    auto marker = ReadRecord(entry.path() / LauncherPackageName); auto state = LauncherStateFromMarker(marker, true);
                    ValidateLauncher(state, installation);
                    const std::array files{PackageFile{LauncherName, hash, uint64_t(Number(marker, "executableSize"))}};
                    DeleteValidatedPackage(paths, entry.path(), files, LauncherPackageName);
                }
                catch (const std::exception& error) { Log(std::string("Preserved an inactive launcher package: ") + error.what(), report); }
            }
    }
    namespace
    {
        void ValidateUninstall(const Json& record, std::string_view owner, std::string_view transaction)
        {
            RequireExactObject(record, {"schemaVersion", "productId", "installationId", "transactionId", "phase", "previousState", "startedUtc"}, "uninstall record");
            Require(Number(record, "schemaVersion") == 1 && Text(record, "productId") == ProductId && Text(record, "installationId") == owner &&
                IsGuid(owner) && IsGuid(transaction) && Text(record, "transactionId") == transaction, "The uninstall record does not prove ownership.");
            const auto& phase = Text(record, "phase");
            Require(phase == "prepared" || phase == "removing-shell" || phase == "shell-removed" || phase == "program-moved" || phase == "roots-moved" || phase == "deleting", "The uninstall phase is invalid.");
            if (auto previous = Optional(Member(record, "previousState"))) ValidateState(*previous, owner, Component::Renderer);
            (void)Text(record, "startedUtc");
        }
        void CheckRootOwner(const fs::path& root, std::string_view owner)
        {
            RejectReparseChain(root); if (!fs::exists(root)) return;
            const auto marker = ReadRecord(root / OwnerName);
            RequireExactObject(marker, {"schemaVersion", "productId", "installationId"}, "cleanup owner marker");
            Require(Number(marker, "schemaVersion") == 1 && Text(marker, "productId") == ProductId && Text(marker, "installationId") == owner, "Cleanup could not prove the root owner. It was preserved.");
        }
    }
    Result Installer::ScheduleUninstall(std::string_view owner, const Report& report)
    {
        Require(fs::exists(paths.program / OwnerName) && fs::exists(paths.state / OwnerName), "Both installation roots must prove ownership before uninstall.");
        CheckRootOwner(paths.program, owner); CheckRootOwner(paths.state, owner);
        Require(services.processes(paths.program, Component::Renderer, false).Stopped(), "UVSR is currently running. Close its window, then choose Uninstall again.");
        Require(services.processes(paths.program, Component::Launcher, true).Stopped(), "Another UVSR Launcher window is open. Close it, then choose Uninstall again.");
        EnsureOwnedRoot(paths.operations, owner);
        Require(!fs::exists(paths.operations / "uninstall.json"), "A UVSR uninstall is already pending. Close the earlier launcher window, then try again.");
        const auto transaction = Guid();
        auto snapshot = Inspect();
        auto record = JObject({{"schemaVersion", JNumber(1)}, {"productId", JString(ProductId)}, {"installationId", JString(std::string(owner))},
            {"transactionId", JString(transaction)}, {"phase", JString("prepared")}, {"previousState", Nullable(snapshot.state)}, {"startedUtc", JString(UtcNow())}});
        auto journal = JObject({{"schemaVersion", JNumber(1)}, {"installationId", JString(std::string(owner))}, {"transactionId", JString(transaction)},
            {"operation", JNumber(int(Operation::Uninstall))}, {"phase", JString("uninstall-pending")}, {"candidateVersionId", Json{}},
            {"previousState", Nullable(snapshot.state)}, {"startedUtc", JString(UtcNow())}});
        WriteRecord(paths.state / "transaction.json", journal); WriteRecord(paths.operations / "uninstall.json", record);
        try
        {
            const auto helperRoot = Descendant(paths.operations / "helpers", CompactGuid(transaction) + "-" + CompactGuid(Guid()));
            CreateDirectories(helperRoot);
            const auto helper = helperRoot / LauncherName;
            WinCheck(CopyFileW(services.executable.c_str(), helper.c_str(), TRUE), "Stage uninstall helper");
            const auto hash = HashFile(services.executable); const auto size = fs::file_size(services.executable);
            VerifyFile(helper, size, hash); ValidateLauncherMetadata(helper, LauncherVersion);
            WriteRecord(helperRoot / ".uvsr-helper.json", JObject({{"schemaVersion", JNumber(1)}, {"installationId", JString(std::string(owner))},
                {"transactionId", JString(transaction)}, {"sha256", JString(hash)}, {"size", JNumber(int64_t(size))}}));
            std::array<std::wstring, 5> arguments{L"--cleanup", std::to_wstring(GetCurrentProcessId()), std::to_wstring(ProcessStartTicks(GetCurrentProcess())), Wide(owner), Wide(transaction)};
            services.start(helper, arguments, true);
        }
        catch (...) { fs::remove(paths.operations / "uninstall.json"); fs::remove(paths.state / "transaction.json"); throw; }
        if (report) report({"Removing UVSR", "Preparing a safe cleanup after this launcher window closes.", {}, false});
        return {"UVSR uninstall is ready. Its shortcuts and owned program files will be removed when this window closes. Renderer settings are preserved.", {}, {}, true};
    }
    int Installer::Cleanup(DWORD parentId, uint64_t parentTicks, std::string_view installation, std::string_view transaction, const Report& report)
    {
        try
        {
            if (parentId)
            {
                Require(parentId != GetCurrentProcessId(), "The cleanup helper cannot wait for itself.");
                Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, parentId));
                if (parent.value && ProcessStartTicks(parent) == parentTicks)
                    while (WaitForSingleObject(parent, 100) != WAIT_OBJECT_0) {}
                else if (!parent.value) Require(GetLastError() == ERROR_INVALID_PARAMETER, "The cleanup parent could not be identified.");
            }
            OperationLock operation(Wide(lockSuffix)); CheckRootOwner(paths.operations, installation);
            const auto recordPath = paths.operations / "uninstall.json";
            auto record = ReadRecord(recordPath); ValidateUninstall(record, installation, transaction);
            const auto id = CompactGuid(std::string(transaction));
            const std::array roots{paths.program, paths.state, paths.local / "Programs" / ("UVSR.uninstall-" + id), paths.local / ("UVSR Installer.uninstall-" + id)};
            for (const auto& root : roots) CheckRootOwner(root, installation);
            for (const auto& root : {roots[0], roots[2]})
            {
                Require(services.processes(root, Component::Renderer, false).Stopped() && services.processes(root, Component::Launcher, false).Stopped(), "UVSR or an installed UVSR Launcher is still running. Cleanup was deferred.");
            }
            if (Text(record, "phase") == "prepared" || Text(record, "phase") == "removing-shell")
            {
                Set(record, "phase", JString("removing-shell")); WriteRecord(recordPath, record);
                shell.Remove(installation);
                Set(record, "phase", JString("shell-removed")); WriteRecord(recordPath, record);
            }
            const auto planPath = paths.operations / ("removal-" + id + ".json");
            if (!fs::exists(planPath))
            {
                Json files; files.kind = Json::Kind::Array;
                const auto add = [&](size_t rootIndex, const fs::path& path)
                {
                    Require(IsDescendant(path, roots[rootIndex]), "Cleanup inventory escaped its root.");
                    RejectReparseChain(path);
                    files.array.push_back(JObject({{"root", JNumber(int64_t(rootIndex))}, {"path", JString(Utf8(path.lexically_relative(roots[rootIndex]).generic_wstring()))},
                        {"size", JNumber(int64_t(fs::file_size(path)))}, {"sha256", JString(HashFile(path))}}));
                };
                for (size_t rootIndex : {size_t(0), size_t(2)})
                {
                    const auto& root = roots[rootIndex]; if (!fs::exists(root)) continue;
                    Paths relocated = paths; relocated.program = root;
                    Installer verifier(relocated, services, key, keyId);
                    const auto rendererStages = root / "staging";
                    if (fs::exists(rendererStages))
                    {
                        RejectReparseChain(rendererStages);
                        for (const auto& stage : fs::directory_iterator(rendererStages))
                            try
                            {
                                Require(IsLowerHex(Utf8(stage.path().filename().wstring()), 32), "Unknown renderer stage name");
                                CheckRootOwner(stage.path(), installation);
                                const auto packageRoot = stage.path() / "package";
                                const auto package = ValidatePackage(packageRoot);
                                for (const auto& file : package.files) add(rootIndex, Descendant(packageRoot, file.path));
                                add(rootIndex, packageRoot / PackageName); add(rootIndex, stage.path() / OwnerName);
                            }
                            catch (const std::exception& error) { Log(std::string("Preserved an incomplete or changed renderer stage: ") + error.what(), report); }
                    }
                    const auto launcherStages = root / "launcher/staging";
                    if (fs::exists(launcherStages))
                    {
                        RejectReparseChain(launcherStages);
                        for (const auto& stage : fs::directory_iterator(launcherStages))
                            try
                            {
                                Require(IsLowerHex(Utf8(stage.path().filename().wstring()), 32), "Unknown launcher stage name");
                                RejectReparseChain(stage.path());
                                const auto marker = ReadRecord(stage.path() / LauncherPackageName);
                                const auto state = LauncherStateFromMarker(marker, false);
                                ValidateState(state, installation, Component::Launcher);
                                const auto size = Number(marker, "executableSize");
                                Require(size > 0 && uint64_t(size) <= MaximumLauncherBytes, "Invalid staged launcher size");
                                VerifyFile(stage.path() / LauncherName, uint64_t(size), Text(state, "executableSha256"));
                                ValidateLauncherMetadata(stage.path() / LauncherName, Text(state, "version"));
                                add(rootIndex, stage.path() / LauncherName); add(rootIndex, stage.path() / LauncherPackageName);
                            }
                            catch (const std::exception& error) { Log(std::string("Preserved an incomplete or changed launcher stage: ") + error.what(), report); }
                    }
                    if (fs::exists(relocated.Versions()))
                        for (const auto& directory : fs::directory_iterator(relocated.Versions()))
                        {
                            if (!IsVersionId(Utf8(directory.path().filename().wstring()))) continue;
                            try
                            {
                                auto package = ValidatePackage(directory.path());
                                for (const auto& file : package.files) add(rootIndex, Descendant(directory.path(), file.path));
                                add(rootIndex, directory.path() / PackageName);
                            }
                            catch (const std::exception& error) { Log(std::string("Preserved an unverified renderer package during uninstall: ") + error.what(), report); }
                        }
                    if (fs::exists(relocated.LauncherVersions()))
                        for (const auto& directory : fs::directory_iterator(relocated.LauncherVersions()))
                        {
                            if (!IsLowerHex(Utf8(directory.path().filename().wstring()), 64)) continue;
                            try
                            {
                                auto marker = ReadRecord(directory.path() / LauncherPackageName);
                                auto state = LauncherStateFromMarker(marker, true); verifier.ValidateLauncher(state, installation);
                                add(rootIndex, directory.path() / LauncherName); add(rootIndex, directory.path() / LauncherPackageName);
                            }
                            catch (const std::exception& error) { Log(std::string("Preserved an unverified launcher package during uninstall: ") + error.what(), report); }
                        }
                }
                for (size_t rootIndex : {size_t(1), size_t(3)})
                {
                    const auto& root = roots[rootIndex]; if (!fs::exists(root)) continue;
                    for (const auto& [name, component] : {std::pair{"state.json", Component::Renderer}, std::pair{"launcher-state.json", Component::Launcher}})
                        if (fs::exists(root / name))
                            try { (void)ReadState(root / name, installation, component); add(rootIndex, root / name); } catch (...) {}
                    for (const auto name : {"transaction.json", "launcher-update.json"})
                        if (fs::exists(root / name))
                            try { auto journal = ReadRecord(root / name); Require(Text(journal, "installationId") == installation && Number(journal, "schemaVersion") == 1, "Unowned journal"); add(rootIndex, root / name); } catch (...) {}
                    const auto downloads = root / "downloads";
                    if (fs::exists(downloads))
                    {
                        RejectReparseChain(downloads);
                        for (const auto& entry : fs::directory_iterator(downloads))
                        {
                            const auto name = Utf8(entry.path().filename().wstring());
                            try
                            {
                                RejectReparseChain(entry.path());
                                if (name.starts_with("renderer-") && name.ends_with(".zip") && name.size() == 77)
                                {
                                    const auto hash = name.substr(9, 64);
                                    Require(IsLowerHex(hash, 64), "Invalid renderer cache name");
                                    VerifyFile(entry.path(), fs::file_size(entry.path()), hash);
                                    add(rootIndex, entry.path());
                                }
                                else if (name == "renderer-feed.json" || name == "launcher-feed.json")
                                {
                                    (void)VerifyFeed(ReadFile(entry.path(), 16384), name == "renderer-feed.json" ? Component::Renderer : Component::Launcher, key, keyId);
                                    add(rootIndex, entry.path());
                                }
                            }
                            catch (const std::exception& error) { Log(std::string("Preserved an unverified download: ") + error.what(), report); }
                        }
                        const auto launchers = downloads / "launcher";
                        if (fs::exists(launchers))
                            for (const auto& entry : fs::directory_iterator(launchers))
                                try
                                {
                                    const auto hash = Utf8(entry.path().filename().wstring());
                                    Require(IsLowerHex(hash, 64), "Invalid launcher cache name");
                                    const auto executable = entry.path() / LauncherName;
                                    VerifyFile(executable, fs::file_size(executable), hash);
                                    ValidatePe(executable); add(rootIndex, executable);
                                }
                                catch (const std::exception& error) { Log(std::string("Preserved an unverified launcher download: ") + error.what(), report); }
                    }
                }
                WriteRecord(planPath, JObject({{"schemaVersion", JNumber(1)}, {"installationId", JString(std::string(installation))}, {"transactionId", JString(std::string(transaction))}, {"files", files}}));
            }
            Set(record, "phase", JString("deleting")); WriteRecord(recordPath, record); Checkpoint("uninstall-deleting");
            const auto plan = ReadRecord(planPath, 32u << 20);
            RequireExactObject(plan, {"schemaVersion", "installationId", "transactionId", "files"}, "uninstall removal inventory");
            Require(Number(plan, "schemaVersion") == 1 && Text(plan, "installationId") == installation && Text(plan, "transactionId") == transaction &&
                Member(plan, "files").kind == Json::Kind::Array && Member(plan, "files").array.size() <= 200000, "The cleanup inventory does not prove ownership.");
            std::set<std::pair<int64_t, std::string>> unique;
            for (const auto& file : Member(plan, "files").array)
            {
                RequireExactObject(file, {"root", "path", "size", "sha256"}, "cleanup file");
                const auto rootIndex = Number(file, "root"), size = Number(file, "size"); const auto relative = Text(file, "path");
                Require(rootIndex >= 0 && rootIndex < int64_t(roots.size()) && size >= 0 && IsLowerHex(Text(file, "sha256"), 64) &&
                    unique.emplace(rootIndex, Lower(relative)).second, "The cleanup file identity is invalid.");
                auto path = Descendant(roots[size_t(rootIndex)], relative);
                if (!fs::exists(path)) continue;
                try
                {
                    VerifyFile(path, uint64_t(size), Text(file, "sha256"));
                    fs::remove(path); RemoveEmptyParents(path.parent_path(), roots[size_t(rootIndex)]); Checkpoint("uninstall-file-removed");
                }
                catch (const std::exception& error) { Log(std::string("Preserved a changed or unavailable file: ") + error.what(), report); }
            }
            for (const auto& root : roots)
            {
                if (!fs::exists(root)) continue;
                CheckRootOwner(root, installation);
                bool onlyMarker = true;
                for (const auto& item : fs::directory_iterator(root)) if (item.path().filename() != OwnerName) onlyMarker = false;
                if (onlyMarker) { fs::remove(root / OwnerName); fs::remove(root); }
            }
            fs::remove(planPath); fs::remove(recordPath);
            if (report) report({"UVSR removed", "Owned program files and shortcuts were removed. Renderer settings and unverified files were preserved.", 100, false});
            return 0;
        }
        catch (const std::exception& error) { try { Log(error.what(), report); } catch (...) {} return 1; }
    }
    void Installer::RecoverUninstall(const Report& report)
    {
        const auto path = paths.operations / "uninstall.json";
        if (!fs::exists(path)) { CleanupHelpers(report); return; }
        const auto record = ReadRecord(path);
        const auto owner = Text(record, "installationId"), transaction = Text(record, "transactionId"); ValidateUninstall(record, owner, transaction);
        Require(!IsDescendant(services.executable, paths.program), "An earlier uninstall is pending. Close this installed launcher and open the original downloaded UVSR Launcher to finish it.");
        Require(Cleanup(0, 0, owner, transaction, report) == 0, "The earlier uninstall still needs attention. Close other UVSR windows, then reopen the downloaded UVSR Launcher.");
        CleanupHelpers(report);
    }
    void Installer::CleanupHelpers(const Report& report)
    {
        if (!fs::exists(paths.operations) || fs::exists(paths.operations / "uninstall.json")) return;
        OperationLock operation(Wide(lockSuffix));
        const auto owner = Text(ReadRecord(paths.operations / OwnerName), "installationId");
        CheckRootOwner(paths.operations, owner);
        const auto helpers = paths.operations / "helpers";
        if (!fs::exists(helpers)) return;
        RejectReparseChain(helpers);
        for (const auto& entry : fs::directory_iterator(helpers))
            try
            {
                RejectReparseChain(entry.path());
                if (SamePath(services.executable.parent_path(), entry.path()) || !services.processes(entry.path(), Component::Launcher, false).Stopped()) continue;
                const auto markerPath = entry.path() / ".uvsr-helper.json";
                const auto marker = ReadRecord(markerPath);
                RequireExactObject(marker, {"schemaVersion", "installationId", "transactionId", "sha256", "size"}, "cleanup helper");
                Require(Number(marker, "schemaVersion") == 1 && Text(marker, "installationId") == owner && IsGuid(Text(marker, "transactionId")) &&
                    IsLowerHex(Text(marker, "sha256"), 64) && Number(marker, "size") > 0 && uint64_t(Number(marker, "size")) <= MaximumLauncherBytes, "Unverified cleanup helper");
                std::set<std::string> names;
                for (const auto& file : fs::directory_iterator(entry.path()))
                { RejectReparseChain(file.path()); Require(file.is_regular_file(), "Foreign helper member"); names.emplace(Utf8(file.path().filename().wstring())); }
                Require(names == std::set<std::string>{LauncherName, ".uvsr-helper.json"}, "Foreign helper files were preserved");
                const auto executable = entry.path() / LauncherName;
                VerifyFile(executable, uint64_t(Number(marker, "size")), Text(marker, "sha256"));
                ValidatePe(executable);
                fs::remove(executable); fs::remove(markerPath); fs::remove(entry.path());
            }
            catch (const std::exception& error) { Log(std::string("Preserved an unavailable cleanup helper: ") + error.what(), report); }
        RemoveEmptyParents(helpers, paths.operations);
    }
}
