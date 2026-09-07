#include "fixtures.h"
#include <array>
#include <iostream>
#include <thread>

namespace test
{
    namespace
    {
        Json EngineIdentity(const fs::path& executable)
        {
            SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
            Handle read, write;
            WinCheck(CreatePipe(&read.value, &write.value, &attributes, 0), "create identity output pipe");
            WinCheck(SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0), "restrict identity output inheritance");
            Handle job(CreateJobObjectW(nullptr, nullptr));
            WinCheck(job.value != nullptr, "create identity process job");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            WinCheck(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)), "own identity process lifetime");
            STARTUPINFOW startup{sizeof(startup)};
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = startup.hStdError = write;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            std::wstring command = L"\"" + executable.wstring() + L"\" --identity-json";
            PROCESS_INFORMATION info{};
            WinCheck(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                nullptr, executable.parent_path().c_str(), &startup, &info), "start exact engine identity process");
            Handle process(info.hProcess), thread(info.hThread);
            try
            {
                WinCheck(AssignProcessToJobObject(job, process), "assign identity process job");
                WinCheck(ResumeThread(thread) != DWORD(-1), "resume exact engine identity process");
                CloseHandle(write.value); write.value = nullptr;
                std::string output;
                bool exited = false;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                for (;;)
                {
                    DWORD available = 0;
                    const BOOL pipe = PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr);
                    if (!pipe) Require(GetLastError() == ERROR_BROKEN_PIPE, "identity output pipe failed");
                    if (available)
                    {
                        Require(available <= (1u << 20) - output.size(), "identity output exceeds its limit");
                        std::string bytes(available, '\0'); DWORD received = 0;
                        WinCheck(::ReadFile(read, bytes.data(), available, &received, nullptr), "read exact engine identity");
                        output.append(bytes.data(), received);
                    }
                    else if (exited) break;
                    // observe the pipe again after process exit so its final output is drained.
                    else exited = WaitForSingleObject(process, 10) == WAIT_OBJECT_0;
                    Require(std::chrono::steady_clock::now() < deadline, "engine identity process timed out");
                }
                DWORD exit = 1; WinCheck(GetExitCodeProcess(process, &exit), "read engine identity exit");
                Require(exit == 0, "exact engine identity command failed");
                return ParseJson(output);
            }
            catch (...)
            {
                TerminateProcess(process, 1); TerminateJobObject(job, 1);
                WaitForSingleObject(process, 30000); throw;
            }
        }
    }
    void VerifyProductionServices(const fs::path& engine, const fs::path& launcher)
    {
        Require(engine.filename() == EngineName && launcher.filename() == LauncherName, "production executable names are not canonical");
        const auto package = ValidatePackage(engine.parent_path().parent_path());
        const auto identity = EngineIdentity(engine);
        const auto& manifest = package.manifest;
        Require(Text(identity,"executable") == EngineName && Text(identity,"source_commit") == Text(manifest,"sourceCommit") &&
            Text(identity,"source_identity") == Text(manifest,"sourceCommit") && Text(identity,"settings_hash") == Text(manifest,"settingsHash") &&
            Text(identity,"engine_version") == Text(manifest,"engineVersion") && Text(identity,"configuration") == "Release" &&
            Flag(identity,"source_tree_clean") && Flag(identity,"production"), "running engine identity does not match its exact package");
        Require(PeString(launcher,L"ProductionBuild") == "true", "production service proof requires a clean native launcher");
        Fixture fixture;
        fixture.services.executable = launcher;
        fixture.services.health = RunHealth;
        auto owner = EnsureOwnership(fixture.paths);
        auto installer = fixture.Make(); installer.Ready(true,{},{});
        const auto state = installer.InspectLauncher(owner).valid;
        Require(state.has_value(), "actual native launcher did not stage and pass health");
        Shell shell(fixture.paths, fixture.registry);
        Require(shell.OwnsShortcut(fixture.paths.StartShortcut(),owner) && shell.OwnsShortcut(fixture.paths.DesktopShortcut(),owner), "real COM shortcuts do not target the owned launcher");
        auto executable = fixture.paths.Launcher(Text(*state,"executableSha256")) / LauncherName;
        Require(RunHealth(executable,LauncherSequence,LauncherVersion,{}) == 0, "staged native launcher health failed");
        const auto self = CurrentExecutable();
        const std::array wrongIdentity{ProcessIdentity{GetCurrentProcessId(),ProcessStartTicks(GetCurrentProcess()) + 1,self}};
        Throws([&] { CloseProcesses(wrongIdentity,{},false); });
        std::cout << "PASS exact production engine process, native launcher health, process identity and COM shortcuts\n";
    }
    void VerifyExactArchive(const fs::path& archive, Feed expected)
    {
        Require(archive.filename() == ArchiveName, "exact archive name is not canonical");
        Fixture fixture;
        expected.component = Component::Renderer;
        expected.hash = HashFile(archive); expected.size = fs::file_size(archive);
        fixture.renderer = VerifyFeed(fixture.signing.Sign(expected),Component::Renderer,fixture.signing.publicKey,"test-key");
        fixture.archive = archive;
        auto owner = EnsureOwnership(fixture.paths); InstallOldLauncher(fixture.paths,owner,true);
        auto installer = fixture.Make();
        installer.Execute(Operation::Install,true,{},{});
        const auto installed = installer.Inspect();
        Require(installed.state && !installed.damaged && Number(*installed.state,"releaseSequence") == expected.sequence &&
            Text(*installed.state,"commit") == expected.commit && Text(*installed.state,"settingsHash") == expected.settingsHash &&
            Text(*installed.state,"engineVersion") == expected.version, "exact signed archive did not install coherently");
        installer.Execute(Operation::Update,false,{},{});
        const auto before = installer.Inspect();
        fixture.services.checkpoint = [](std::string_view phase) { if (phase == "renderer-state-activated") throw std::runtime_error("injected shell activation interruption"); };
        auto interrupted = fixture.Make();
        Throws([&] { interrupted.Execute(Operation::Reinstall,false,{},{}); });
        fixture.services.checkpoint = {};
        auto repaired = fixture.Make(); repaired.Ready(false,{},{});
        Require(Serialize(*repaired.Inspect().state) == Serialize(*before.state), "exact archive activation did not roll back");
        WriteAtomic(repaired.Inspect().executable,"modified, preserve");
        repaired.Execute(Operation::Reinstall,false,{},{});
        Require(!repaired.Inspect().damaged, "exact archive repair failed");
        fixture.services.processes = [](const fs::path&, Component, bool) { return Processes{{},true}; };
        auto uncertain = fixture.Make(); Throws([&] { uncertain.Execute(Operation::Uninstall,false,{},{}); });
        fixture.services.processes = [](const fs::path&, Component, bool) { return Processes{}; };
        auto cleanup = fixture.Make(); cleanup.Execute(Operation::Uninstall,false,{},{});
        const auto record = ReadRecord(fixture.paths.operations / "uninstall.json");
        Require(cleanup.Cleanup(0,0,owner,Text(record,"transactionId")) == 0 && !fs::exists(fixture.paths.state / "state.json") &&
            !fs::exists(fixture.paths.StartShortcut()), "exact archive uninstall failed");
        std::cout << "PASS exact signed archive install, update, interruption rollback, repair, old-state migration and uninstall\n";
    }
}
