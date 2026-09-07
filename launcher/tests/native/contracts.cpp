#include "fixtures.h"
#include "runtime_inventory.h"
#include "progress.h"
#include <array>
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <thread>

using namespace test;
namespace
{
    void FixtureShortcut(const fs::path& path, const fs::path& target, const wchar_t* arguments = L"")
    {
        Microsoft::WRL::ComPtr<IShellLinkW> link;
        Require(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))), "shortcut fixture creation failed");
        Require(SUCCEEDED(link->SetPath(target.c_str())) && SUCCEEDED(link->SetArguments(arguments)), "shortcut fixture target failed");
        Microsoft::WRL::ComPtr<IPersistFile> file;
        Require(SUCCEEDED(link.As(&file)), "shortcut fixture persistence unavailable");
        CreateDirectories(path.parent_path());
        Require(SUCCEEDED(file->Save(path.c_str(), TRUE)), "shortcut fixture save failed");
    }
    void LegacyShortcutMigration()
    {
        for (int interruption = 0; interruption < 3; ++interruption)
        {
            Fixture fixture; const auto owner = EnsureOwnership(fixture.paths);
            const auto state = InstallOldLauncher(fixture.paths, owner, true);
            const auto root = fixture.paths.Launcher(Text(state, "executableSha256"));
            const auto old = root / "UVSR Launcher.exe", canonical = root / LauncherName;
            FixtureShortcut(fixture.paths.StartShortcut(), old);
            FixtureShortcut(fixture.paths.DesktopShortcut(), interruption == 2 ? canonical : old);
            if (interruption) fs::rename(old, canonical);
            auto installer = fixture.Make();
            Require(installer.InspectLauncher(owner, true).valid.has_value(), "old launcher migration failed");
            Require(!fs::exists(old) && fs::exists(canonical), "old executable name was retained");
            uvsr::launcher::Shell shell(fixture.paths, fixture.registry);
            Require(shell.OwnsShortcut(fixture.paths.StartShortcut(), owner) && shell.OwnsShortcut(fixture.paths.DesktopShortcut(), owner),
                "migration did not repair both shortcut targets");
            const auto startHash = HashFile(fixture.paths.StartShortcut()), desktopHash = HashFile(fixture.paths.DesktopShortcut());
            (void)installer.InspectLauncher(owner, true);
            Require(HashFile(fixture.paths.StartShortcut()) == startHash && HashFile(fixture.paths.DesktopShortcut()) == desktopHash,
                "completed shortcut migration was not idempotent");
            for (int foreign = 0; foreign < 3; ++foreign)
            {
                const auto target = foreign == 0 ? fixture.root / "foreign/UVSR Launcher.exe" : foreign == 1 ? old : root / "UVSR Installer.exe";
                FixtureShortcut(fixture.paths.DesktopShortcut(), target, foreign == 1 ? L"--launch" : L"");
                const auto preserved = HashFile(fixture.paths.DesktopShortcut());
                (void)installer.InspectLauncher(owner, true);
                Require(HashFile(fixture.paths.DesktopShortcut()) == preserved, "migration changed an unrelated shortcut or accepted another alias");
                shell.Validate(owner, true);
                Require(HashFile(fixture.paths.DesktopShortcut()) == preserved, "preflight changed a foreign shortcut");
            }
            FixtureShortcut(fixture.paths.DesktopShortcut(), old);
            const auto preserved = HashFile(fixture.paths.DesktopShortcut());
            WriteAtomic(root / "foreign.txt", "preserve");
            Throws([&] { installer.ValidateLauncher(state, owner); });
            fs::remove(root / "foreign.txt");
            WriteAtomic(canonical, "modified package, preserve its shortcut");
            Require(!installer.InspectLauncher(owner, true).valid, "modified package was accepted for shortcut migration");
            Require(HashFile(fixture.paths.DesktopShortcut()) == preserved, "unverified package caused a shortcut rewrite");
        }
    }
    void ProgressOutput()
    {
        HDC dc = CreateCompatibleDC(nullptr);
        Require(dc != nullptr, "progress drawing context unavailable");
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), 80, -8, 1, 32, BI_RGB};
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap) { DeleteDC(dc); throw std::runtime_error("progress bitmap unavailable"); }
        const auto old = SelectObject(dc, bitmap);
        try
        {
            struct Expected { std::optional<int> value; const wchar_t* text; int begin, end; };
            for (const auto& expected : std::array{
                Expected{{}, L"In progress", 30, 50}, Expected{-1, L"0 percent", 0, 0},
                Expected{25, L"25 percent", 0, 20}, Expected{101, L"100 percent", 0, 80}})
            {
                Require(ui::ProgressText(expected.value) == expected.text, "progress accessible text changed");
                ui::DrawProgress(dc, {0, 0, 80, 8}, expected.value, RGB(0,0,0), RGB(255,255,255));
                for (int x = 0; x < 80; ++x)
                    Require((GetPixel(dc, x, 4) == RGB(255,255,255)) == (x >= expected.begin && x < expected.end), "progress pixels differ from the retained contract");
            }
        }
        catch (...) { SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); throw; }
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    }
    void FeedProof(Component component)
    {
        Fixture f; f.Package(); const auto feed = component == Component::Launcher ? f.launcher : f.renderer;
        auto signedFeed = f.signing.Sign(feed);
        Require(VerifyFeed(signedFeed, component, f.signing.publicKey, "test-key") == feed, "signed feed changed identity");
        Throws([&] { VerifyFeed(signedFeed, component, f.signing.publicKey, "wrong-key"); });
        Throws([&] { VerifyFeed(signedFeed, component); });
        if (component == Component::Launcher)
        {
            Throws([&] { VerifyFeed(f.signing.Sign(feed, false), component, f.signing.publicKey, "test-key"); });
            Throws([&] { VerifyFeed(" " + signedFeed, component, f.signing.publicKey, "test-key"); });
        }
    }
    void KnownSignature()
    {
        // RFC 6979 appendix A.2.5, SHA-256, message "sample". no test signing code supplies this answer.
        constexpr auto key = "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEYP7UuiVanTHJYet0xjVtaMBJuJI7Yfps5mliLmDyn7Z5A/4QCLi8maQa6elWKLxk8vGyDC1+n1F3o8KU1EYimQ==";
        auto signature = DecodeBase64("79SLKqy2qP0RQN2c1F6B1p0sh3tWqvmRw00OqE6vNxb3yxyULWV8QdQ2x6G24p9l8+kA27mv9AZNxKsvhDrNqA==");
        std::array<unsigned char, 6> message{'s','a','m','p','l','e'};
        VerifyP256Signature(message, signature, key);
        signature[0] ^= 1;
        Throws([&] { VerifyP256Signature(message, signature, key); });
        signature.resize(63);
        Throws([&] { VerifyP256Signature(message, signature, key); });
    }
    void Tamper()
    {
        Fixture f; f.Package(); auto envelope = ParseJson(f.signing.Sign(f.renderer));
        auto bytes = DecodeBase64(Text(envelope,"payloadBase64")); std::string payload(bytes.begin(),bytes.end());
        auto changed = envelope; auto signature = Text(changed,"signatureBase64"); signature[0] = signature[0] == 'A' ? 'B' : 'A'; Set(changed,"signatureBase64",JString(signature));
        Throws([&] { VerifyFeed(Serialize(changed), Component::Renderer, f.signing.publicKey,"test-key"); });
        for (const auto& bad : {"{\"schemaVersion\":1," + payload.substr(1), "{\"unknown\":true," + payload.substr(1)})
            Throws([&] { VerifyFeed(f.signing.SignPayload(bad, Component::Renderer), Component::Renderer, f.signing.publicKey,"test-key"); });
        auto invalid = f.renderer; invalid.sequence = MaximumSequence + 1;
        Throws([&] { VerifyFeed(f.signing.Sign(invalid), Component::Renderer, f.signing.publicKey,"test-key"); });
        invalid = f.renderer; invalid.hash = std::string(64,'F');
        Throws([&] { VerifyFeed(f.signing.Sign(invalid), Component::Renderer, f.signing.publicKey,"test-key"); });
    }
    void Lifecycle()
    {
        Fixture f; f.Package(); auto installer = f.Make();
        Require(!installer.Inspect().installed, "empty fixture looks installed");
        FixtureShortcut(f.paths.StartShortcut(), f.root / "unrelated.exe", L"--keep-my-arguments");
        WriteAtomic(f.paths.DesktopShortcut(), "unreadable foreign shortcut bytes");
        const auto startHash = HashFile(f.paths.StartShortcut()), desktopHash = HashFile(f.paths.DesktopShortcut());
        std::vector<fs::path> backups;
        std::vector<std::string> messages;
        installer.Execute(Operation::Install, true, {}, [&](const Progress& progress) { messages.push_back(progress.detail); });
        for (const auto& directory : {f.paths.StartShortcut().parent_path(), f.paths.DesktopShortcut().parent_path()})
            for (const auto& entry : fs::directory_iterator(directory))
                if (entry.path().filename().wstring().starts_with(L"UVSR Launcher (preserved ")) backups.push_back(entry.path());
        Require(backups.size() == 2, "conflicting shortcuts were not both preserved");
        for (const auto& backup : backups)
        {
            Require(HashFile(backup) == (backup.parent_path() == f.paths.DesktopShortcut().parent_path() ? desktopHash : startHash),
                "a preserved shortcut changed bytes");
            Require(std::any_of(messages.begin(), messages.end(), [&](const auto& text) { return text.find(Utf8(backup.wstring())) != std::string::npos; }),
                "the preserved shortcut location was not reported");
        }
        auto first = installer.Inspect();
        Require(first.installed && !first.damaged, "fresh install failed");
        Require(fs::exists(f.paths.StartShortcut()) && fs::exists(f.paths.DesktopShortcut()), "shortcuts missing");
        f.Package(17); installer.Execute(Operation::Update, false, {}, {});
        auto current = installer.Inspect(); Require(current.state && Number(*current.state,"releaseSequence") == 17 && !current.damaged, "update failed");
        Require(!fs::exists(f.paths.DesktopShortcut()), "desktop preference ignored");
        WriteAtomic(current.executable, "modified engine"); Require(installer.Inspect().damaged, "modified engine accepted");
        installer.Execute(Operation::Reinstall, true, {}, {}); Require(!installer.Inspect().damaged, "repair failed");
        f.failDownload = true; Throws([&] { installer.Execute(Operation::Update, true, {}, {}); });
        Require(!installer.Inspect().damaged, "download failure changed installed renderer"); f.failDownload = false;
        auto result = installer.Execute(Operation::Uninstall, true, {}, {}); Require(result.cleanupScheduled, "cleanup was not scheduled");
        auto record = ReadRecord(f.paths.operations / "uninstall.json");
        f.services.checkpoint = [](std::string_view phase) { if (phase == "uninstall-file-removed") throw std::runtime_error("injected removal interruption"); };
        auto interrupted = f.Make();
        Require(interrupted.Cleanup(0,0,Text(record,"installationId"),Text(record,"transactionId")) == 1, "uninstall interruption was not retained");
        Require(fs::exists(f.paths.operations / "uninstall.json"), "interrupted uninstall lost its durable record");
        f.services.checkpoint = {}; auto resumed = f.Make();
        Require(resumed.Cleanup(0,0,Text(record,"installationId"),Text(record,"transactionId")) == 0, "uninstall did not resume");
        Require(!fs::exists(f.paths.StartShortcut()) && !fs::exists(f.paths.DesktopShortcut()) && !fs::exists(f.paths.state / "state.json"), "uninstall left active state or shell links");
        Require(fs::exists(current.executable), "uninstall removed modified historical engine");
        for (const auto& backup : backups)
            Require(fs::exists(backup) && HashFile(backup) == (backup.parent_path() == f.paths.DesktopShortcut().parent_path() ? desktopHash : startHash),
                "update, repair, recovery or uninstall removed a preserved shortcut");
    }
    void OldUpgrade()
    {
        Fixture f; f.Package(); auto owner = EnsureOwnership(f.paths); InstallOldLauncher(f.paths, owner);
        auto version = NewVersionId(Commit); auto package = f.paths.Renderer(version); auto oldFeed = MakePackage(package,15,true);
        auto manifest = ValidatePackage(package).manifest;
        auto state = JObject({{"schemaVersion",JNumber(1)}, {"installationId",JString(owner)}, {"activeVersionId",JString(version)}, {"releaseSequence",JNumber(15)},
            {"commit",JString(Commit)}, {"settingsHash",JString(oldFeed.settingsHash)}, {"engineVersion",JString(oldFeed.version)}, {"artifactSha256",JString(std::string(64,'a'))},
            {"executableSha256",Member(manifest,"executableSha256")}, {"desktopShortcut",JBool(true)}, {"installedUtc",JString(UtcNow())}});
        WriteRecord(f.paths.state / "state.json", state); auto installer = f.Make();
        Require(!installer.Inspect().damaged, "old schema package cannot be inspected");
        Throws([&] { ValidatePackage(package, &oldFeed); });
        installer.Ready(true, {}, {}); Require(Number(*installer.InspectLauncher(owner).valid,"releaseSequence") == 17, "old launcher state did not upgrade");
        installer.Execute(Operation::Update,true,{},{}); Require(!installer.Inspect().damaged && Number(*installer.Inspect().state,"releaseSequence") == 16, "old renderer state did not upgrade");
        auto update = installer.UpdateLauncher(f.launcher, true, true, {}, {});
        Require(update.continuation && update.relaunch.filename() == LauncherName && Number(*installer.InspectLauncher(owner).valid,"releaseSequence") == 18,
            "signed launcher self-update did not activate its canonical successor");
        fs::path redirected;
        f.services.start = [&](const fs::path& target, std::span<const std::wstring> arguments, bool)
        { Require(arguments.size() == 1 && arguments[0] == L"--launch", "startup redirection changed arguments"); redirected = target; };
        auto oldProcess = f.Make(); const std::array<std::wstring,1> launch{L"--launch"};
        Require(oldProcess.Redirect(launch) && SamePath(redirected,update.relaunch), "verified successor was not selected at startup");
        f.services.executable = update.relaunch; auto successor = f.Make();
        Require(successor.PendingContinuation(*update.continuation) == update.continuation && !successor.PendingContinuation(Guid()),
            "self-update continuation did not bind the running successor and transaction");
        Throws([&] { successor.CompleteContinuation(Guid()); });
        successor.CompleteContinuation(*update.continuation);
        Require(!fs::exists(f.paths.state / "launcher-update.json"), "completed continuation retained its journal");
    }
    void Recovery()
    {
        for (auto phase : {"renderer-state-activated", "renderer-shell-committed"})
        {
            Fixture f; f.Package(); auto initial = f.Make(); initial.Execute(Operation::Install,true,{},{}); f.Package(17);
            f.services.checkpoint = [phase](std::string_view reached) { if (reached == phase) throw std::runtime_error("injected interruption"); };
            auto interrupted = f.Make(); Throws([&] { interrupted.Execute(Operation::Update,true,{},{}); });
            f.services.checkpoint = {}; auto recovery = f.Make(); recovery.Ready(true,{},{});
            auto state = recovery.Inspect(); Require(state.state && !state.damaged, "renderer recovery lost valid installation");
            Require(Number(*state.state,"releaseSequence") == (std::string_view(phase) == "renderer-shell-committed" ? 17 : 16), "renderer recovery chose wrong state");
        }
        for (auto phase : {"launcher-prepared", "launcher-state-activated", "launcher-shell-committed"})
        {
            Fixture f; auto owner = EnsureOwnership(f.paths); InstallOldLauncher(f.paths,owner);
            f.services.checkpoint = [phase](std::string_view reached) { if (reached == phase) throw std::runtime_error("injected interruption"); };
            auto interrupted = f.Make(); Throws([&] { interrupted.Ready(true,{},{}); });
            f.services.checkpoint = {}; auto recovered = f.Make(); recovered.Ready(true,{},{});
            Require(recovered.InspectLauncher(owner).valid.has_value(), "launcher recovery lost both versions");
        }
    }
    void Health()
    {
        Fixture f; auto executable = f.root / LauncherName; fs::copy_file(UVSR_LAUNCHER_FIXTURE, executable);
        Require(RunHealth(executable,17,"1.3.0",{}) == 0,"health success failed");
        WriteAtomic(f.root / "health-mode.txt","fail"); Require(RunHealth(executable,17,"1.3.0",{}) == 9,"health failure was hidden");
        for (auto mode : {"descendant","cancel"})
        {
            WriteAtomic(f.root / "health-mode.txt",mode); std::stop_source stop;
            std::jthread cancel;
            if (std::string_view(mode) == "cancel") cancel = std::jthread([&] { std::this_thread::sleep_for(std::chrono::milliseconds(500)); stop.request_stop(); });
            if (std::string_view(mode) == "cancel") Throws([&] { RunHealth(executable,17,"1.3.0",stop.get_token()); });
            else Require(RunHealth(executable,17,"1.3.0",{}) == 0,"descendant health failed");
            DWORD pid = 0; std::ifstream(f.root / "descendant.pid") >> pid; Require(pid != 0,"descendant did not start");
            // allow Windows to signal termination before checking for a leaked descendant.
            Handle process(OpenProcess(SYNCHRONIZE,FALSE,pid));
            Require(!process.value || WaitForSingleObject(process,5000) == WAIT_OBJECT_0,"health descendant escaped its operation");
            fs::remove(f.root / "descendant.pid");
        }
    }
    void ProcessHandling()
    {
        Fixture fixture;
        const auto executable = fixture.root / EngineName;
        fs::copy_file(UVSR_ENGINE_FIXTURE, executable);
        for (const auto mode : {L"--window", L"--ignore-close"})
        {
            Handle job(CreateJobObjectW(nullptr, nullptr));
            Require(job.value != nullptr, "process fixture job unavailable");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            WinCheck(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)), "own process fixture");
            std::wstring command = L"\"" + executable.wstring() + L"\" " + mode;
            STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION info{};
            WinCheck(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                nullptr, fixture.root.c_str(), &startup, &info), "start process fixture");
            Handle process(info.hProcess), thread(info.hThread);
            if (!AssignProcessToJobObject(job, process))
            { TerminateProcess(process, 1); WaitForSingleObject(process, 5000); throw std::runtime_error("process fixture assignment failed"); }
            WinCheck(ResumeThread(thread) != DWORD(-1), "resume process fixture");
            struct Search { DWORD pid; bool ready = false; } search{info.dwProcessId};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!search.ready)
            {
                EnumWindows([](HWND window, LPARAM data) -> BOOL
                {
                    auto& search = *reinterpret_cast<Search*>(data); DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
                    if (pid == search.pid && GetWindow(window, GW_OWNER) == nullptr) search.ready = true;
                    return TRUE;
                }, LPARAM(&search));
                Require(std::chrono::steady_clock::now() < deadline, "process fixture window did not initialize");
                if (!search.ready) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const auto found = InspectProcesses(fixture.root, Component::Renderer);
            Require(!found.uncertain && found.found.size() == 1 && found.found[0].id == info.dwProcessId, "running renderer discovery failed");
            auto changed = found.found; ++changed[0].created;
            Throws([&] { CloseProcesses(changed, {}, true); });
            Require(WaitForSingleObject(process, 0) == WAIT_TIMEOUT, "changed process identity was terminated");
            CloseProcesses(found.found, {}, std::wstring_view(mode) == L"--ignore-close");
            Require(WaitForSingleObject(process, 0) == WAIT_OBJECT_0, "selected renderer did not close");
        }
    }
}
int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    try
    {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--verify-launcher-health")
            return RunHealth(argv[2],LauncherSequence,LauncherVersion,{});
        if (argc == 7 && std::wstring_view(argv[1]) == L"--verify-renderer-archive")
        {
            auto sequenceText = Utf8(argv[6]); int64_t sequence = 0;
            auto parsed = std::from_chars(sequenceText.data(),sequenceText.data()+sequenceText.size(),sequence);
            Require(parsed.ec == std::errc{} && parsed.ptr == sequenceText.data()+sequenceText.size(),"invalid archive sequence");
            VerifyExactArchive(argv[2], Feed{Component::Renderer,sequence,Utf8(argv[3]),Utf8(argv[5]),Utf8(argv[4])});
            return 0;
        }
        if (argc == 4 && std::wstring_view(argv[1]) == L"--verify-production-services")
        {
            VerifyProductionServices(argv[2],argv[3]); return 0;
        }
        const bool runtime = argc == 2 && std::wstring_view(argv[1]) == L"--runtime";
        const bool systemServices = argc == 2 && std::wstring_view(argv[1]) == L"--system-services";
        Require(argc == 1 || runtime || systemServices || (argc == 2 && std::wstring_view(argv[1]) == L"--pure"),"unknown test arguments");
        using Case = std::pair<const char*, std::function<void()>>;
        std::vector<Case> cases;
        if (runtime) cases = {
            {"health process exit, cancellation and descendant ownership", Health},
            {"running renderer discovery, graceful close, explicit force and identity preservation", ProcessHandling}};
        else cases = {
            {"engine numeric identity syntax", [] { Require(IsCanonicalDottedVersion("56941.59271.643.22166",4,65535) && !IsCanonicalDottedVersion("056941.59271.643.22166",4,65535) && !IsCanonicalDottedVersion("1.2.3.65536",4,65535),"engine syntax failed"); }},
            {"independent RFC 6979 signature and P1363 rejection", KnownSignature},
            {"static busy and percentage progress output", ProgressOutput},
            {"signed launcher feed and canonical bytes", [] { FeedProof(Component::Launcher); }},
            {"signed renderer feed", [] { FeedProof(Component::Renderer); }},
            {"tampering, duplicates, unknown fields and unsafe numbers", Tamper},
            {"sequence reuse and downgrade rejection", [] { Fixture f; f.Package(); auto i=f.Make(); i.Execute(Operation::Install,true,{},{}); auto state=i.Inspect().state; auto feed=f.renderer; feed.hash=std::string(64,'c'); Throws([&]{Classify(state,true,feed);}); feed=f.renderer; --feed.sequence; Require(Classify(state,true,feed)==UpdateState::Current,"older feed classified as update"); }},
            {"install update repair and current classification", [] { Fixture f; f.Package(); Require(Classify({},true,f.renderer)==UpdateState::NotInstalled,"fresh classification"); auto i=f.Make(); i.Execute(Operation::Install,true,{},{}); auto state=i.Inspect().state; Require(Classify(state,true,f.renderer)==UpdateState::Current && Classify(state,false,f.renderer)==UpdateState::RepairNeeded,"installed classification"); auto feed=f.renderer; ++feed.sequence; Require(Classify(state,true,feed)==UpdateState::UpdateAvailable,"update classification"); }},
            {"manifest and PE bind signed identity", [] { Fixture f; f.Package(); ValidatePackage(f.root / "package-16",&f.renderer); auto feed=f.renderer; ++feed.sequence; Throws([&]{ValidatePackage(f.root / "package-16",&feed);}); }},
            {"stored and deflated archive round trips and unsafe archive paths", [] { Fixture f; f.Package(); for(bool compressed:{false,true}) { auto archive=f.root/(compressed?"deflate.zip":"stored.zip"); Zip(f.root/"package-16",archive,{},compressed); auto output=f.root/(compressed?"deflated":"stored"); ExtractPackage(archive,output,{},{}); ValidatePackage(output); } Zip(f.root/"package-16",f.root/"unsafe.zip","../uvsr-engine.exe"); Throws([&]{ExtractPackage(f.root/"unsafe.zip",f.root/"unsafe",{},{});}); }},
            {"retained media cannot be missing or swapped", [] { Fixture f; f.Package(); auto root=f.root/"package-16"; auto path=Descendant(root,runtime_asset_map[0]); auto content=ReadFile(path, MaximumExpandedBytes); WriteAtomic(path,"swapped"); Throws([&]{ValidatePackage(root);}); WriteAtomic(path,content); fs::remove(path); Throws([&]{ValidatePackage(root);}); }},
            {"real installer lifecycle and modified-file preservation", Lifecycle},
            {"canonical executable names and stage identity", [] { Require(std::string_view(LauncherName)=="uvsr-launcher.exe" && std::string_view(EngineName)=="uvsr-engine.exe","canonical names changed"); Fixture f; EnsureOwnership(f.paths); auto i=f.Make(); i.Ready(true,{},{}); auto owner=InspectOwnership(f.paths); auto state=i.InspectLauncher(*owner).valid; Require(state && fs::exists(f.paths.Launcher(Text(*state,"executableSha256"))/LauncherName),"canonical launcher stage failed"); }},
            {"old name migrates once with exact ownership", LegacyShortcutMigration},
            {"rollback preserves valid or unavailable packages", [] { Require(DecideRecovery("prepared",PackageStatus::Valid,PackageStatus::Valid)==RecoveryAction::RollBack && DecideRecovery("state-activated",PackageStatus::Invalid,PackageStatus::Valid)==RecoveryAction::RollForward && DecideRecovery("state-activated",PackageStatus::Unverifiable,PackageStatus::Invalid)==RecoveryAction::RetryLater,"rollback decision failed"); }},
            {"startup redirects only allowed CLI to verified same or newer", [] { std::array<std::wstring,1> launch{L"--launch"}, unsafe{L"--cleanup"}; Require(ShouldRedirect(17,L"C:/a/uvsr-launcher.exe",18,L"C:/b/uvsr-launcher.exe",launch) && !ShouldRedirect(17,L"C:/a/uvsr-launcher.exe",16,L"C:/b/uvsr-launcher.exe",launch) && !ShouldRedirect(17,L"C:/a/uvsr-launcher.exe",18,L"C:/b/uvsr-launcher.exe",unsafe),"startup routing failed"); }},
            {"unsafe package paths and ownership are rejected", [] { for(auto path:{"../outside","/absolute","C:/escape","bin/con.txt","bin/evil.","bin/evil ","bin/a\\b","bin/a:stream"}) Throws([&]{ValidateRelativePath(path);}); Fixture f; CreateDirectories(f.paths.program); WriteAtomic(f.paths.program/"foreign.txt","keep"); Throws([&]{EnsureOwnership(f.paths);}); Require(fs::exists(f.paths.program/"foreign.txt"),"foreign root was modified"); }},
            {"compatible old C# state and schema 11 upgrade", OldUpgrade},
            {"interrupted renderer and launcher activation recovery", Recovery}
        };
        if (!runtime)
        {
            const std::set<std::string_view> systemCases{
                "sequence reuse and downgrade rejection", "install update repair and current classification",
                "real installer lifecycle and modified-file preservation", "canonical executable names and stage identity",
                "old name migrates once with exact ownership", "compatible old C# state and schema 11 upgrade",
                "interrupted renderer and launcher activation recovery"};
            std::erase_if(cases, [&](const Case& test) { return systemCases.contains(test.first) != systemServices; });
        }
        size_t failures=0;
        for(const auto& [name,body]:cases)
        {
            std::cout << "RUN " << name << '\n';
            try { body(); std::cout << "PASS " << name << '\n'; }
            catch(const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
        }
        std::cout << cases.size()-failures << '/' << cases.size() << " native launcher " << (runtime?"runtime":systemServices?"system services":"pure") << " cases passed\n";
        return failures?1:0;
    }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
