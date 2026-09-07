#include "installer.h"
#include <TlHelp32.h>
#include <array>
#include <thread>

namespace uvsr::launcher
{
    bool SamePath(const fs::path& a, const fs::path& b)
    {
        const auto left = fs::absolute(a).lexically_normal().wstring(), right = fs::absolute(b).lexically_normal().wstring();
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
    }
    uint64_t ProcessStartTicks(HANDLE process)
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        WinCheck(GetProcessTimes(process, &created, &exited, &kernel, &user), "Read process identity");
        return (uint64_t(created.dwHighDateTime) << 32 | created.dwLowDateTime) + 504911232000000000ull;
    }
    namespace
    {
        fs::path ProcessPath(HANDLE process)
        {
            std::wstring path(32768, 0); DWORD size = DWORD(path.size());
            WinCheck(QueryFullProcessImageNameW(process, 0, path.data(), &size), "Read process executable"); path.resize(size); return path;
        }
        std::wstring Quote(std::wstring_view argument)
        {
            std::wstring result = L"\""; size_t slashes = 0;
            for (wchar_t c : argument)
            {
                if (c == L'\\') { ++slashes; continue; }
                if (c == L'"') result.append(slashes * 2 + 1, L'\\'); else result.append(slashes, L'\\');
                slashes = 0; result += c;
            }
            result.append(slashes * 2, L'\\'); return result + L'"';
        }
        std::wstring Command(const fs::path& executable, std::span<const std::wstring> arguments)
        {
            auto command = Quote(executable.wstring());
            for (const auto& argument : arguments) command += L" " + Quote(argument);
            return command;
        }
        struct Job
        {
            Handle handle{CreateJobObjectW(nullptr, nullptr)};
            Job()
            {
                WinCheck(handle.value != nullptr, "Create health process job");
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{}; info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                WinCheck(SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &info, sizeof(info)), "Configure health process job");
            }
            DWORD Count() const
            {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
                WinCheck(QueryInformationJobObject(handle, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr), "Inspect health process descendants");
                return info.ActiveProcesses;
            }
            void Drain()
            {
                bool terminated = true;
                if (Count()) terminated = TerminateJobObject(handle, 1) != FALSE;
                while (Count()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                Require(terminated, "Windows could not stop the health process. UVSR Launcher waited for its descendants to exit before continuing.");
            }
        };
    }
    Processes InspectProcesses(const fs::path& root, Component component, bool excludeCurrent)
    {
        Processes result;
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (snapshot.value == INVALID_HANDLE_VALUE) { result.uncertain = true; return result; }
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        if (!Process32FirstW(snapshot, &entry)) { result.uncertain = true; return result; }
        const auto name = Wide(component == Component::Launcher ? LauncherName : EngineName);
        do
        {
            if (excludeCurrent && entry.th32ProcessID == GetCurrentProcessId()) continue;
            if (CompareStringOrdinal(entry.szExeFile, -1, name.c_str(), -1, TRUE) != CSTR_EQUAL) continue;
            Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID));
            if (!process.value)
            { if (GetLastError() != ERROR_INVALID_PARAMETER) result.uncertain = true; continue; }
            const auto wait = WaitForSingleObject(process, 0);
            if (wait == WAIT_OBJECT_0) continue;
            if (wait != WAIT_TIMEOUT) { result.uncertain = true; continue; }
            try
            {
                auto path = ProcessPath(process); auto created = ProcessStartTicks(process);
                if (IsDescendant(path, root)) result.found.push_back({entry.th32ProcessID, created, path});
            }
            catch (...) { if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) result.uncertain = true; }
        } while (Process32NextW(snapshot, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) result.uncertain = true;
        return result;
    }
    void StartProcess(const fs::path& executable, std::span<const std::wstring> arguments, bool hidden)
    {
        RejectReparseChain(executable);
        auto command = Command(executable, arguments);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        if (hidden) { startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE; }
        PROCESS_INFORMATION info{};
        WinCheck(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            hidden ? CREATE_NO_WINDOW : 0, nullptr, executable.parent_path().c_str(), &startup, &info), "Start UVSR");
        Handle process(info.hProcess), thread(info.hThread);
    }
    void CloseRenderer(const Paths& paths, const Report& report, bool force)
    {
        const auto processes = InspectProcesses(paths.Versions(), Component::Renderer);
        Require(!processes.uncertain, "UVSR's process identity cannot be verified. No process was closed.");
        CloseProcesses(processes.found, report, force);
    }
    void CloseProcesses(std::span<const ProcessIdentity> processes, const Report& report, bool force)
    {
        if (report) report({"Closing UVSR", "Waiting for UVSR to close.", {}, false});
        std::vector<Handle> waiting;
        for (const auto& identity : processes)
        {
            Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | (force ? PROCESS_TERMINATE : 0), FALSE, identity.id));
            if (!process.value && GetLastError() == ERROR_INVALID_PARAMETER) continue;
            WinCheck(process.value != nullptr, "Open the selected UVSR process");
            if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) continue;
            Require(ProcessStartTicks(process) == identity.created && SamePath(ProcessPath(process), identity.executable), "The UVSR process identity changed. It was preserved.");
            if (force) WinCheck(TerminateProcess(process, 1), "Force close the selected UVSR process");
            else
            {
                struct Windows { DWORD pid; } context{identity.id};
                EnumWindows([](HWND window, LPARAM data) -> BOOL
                {
                    const auto& context = *reinterpret_cast<Windows*>(data); DWORD pid = 0;
                    GetWindowThreadProcessId(window, &pid);
                    if (pid == context.pid && GetWindow(window, GW_OWNER) == nullptr) PostMessageW(window, WM_CLOSE, 0, 0);
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&context));
            }
            waiting.push_back(std::move(process));
        }
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        for (const auto& process : waiting)
            while (WaitForSingleObject(process, 50) != WAIT_OBJECT_0)
                Require(std::chrono::steady_clock::now() < end, "UVSR is still running.");
    }
    int RunHealth(const fs::path& executable, int64_t sequence, std::string_view version, std::stop_token stop)
    {
        CheckCancelled(stop); ValidateLauncherMetadata(executable, version);
        Job job;
        std::array<std::wstring, 3> arguments{L"--launcher-health-check", std::to_wstring(sequence), Wide(version)};
        auto command = Command(executable, arguments);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        WinCheck(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(), &startup, &info), "Start launcher health check");
        Handle process(info.hProcess), thread(info.hThread);
        try
        {
            WinCheck(AssignProcessToJobObject(job.handle, process), "Own launcher health descendants");
            WinCheck(ResumeThread(thread) != DWORD(-1), "Resume launcher health process");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (WaitForSingleObject(process, 50) != WAIT_OBJECT_0)
            {
                CheckCancelled(stop);
                Require(std::chrono::steady_clock::now() < deadline, "The launcher health process exceeded its deadline.");
            }
            DWORD result = 1; WinCheck(GetExitCodeProcess(process, &result), "Read launcher health result");
            job.Drain(); return int(result);
        }
        catch (...)
        {
            // assignment failure still owns a suspended process that must not escape.
            if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) TerminateProcess(process, 1);
            job.Drain();
            while (WaitForSingleObject(process, 50) != WAIT_OBJECT_0) {}
            throw;
        }
    }
    RecoveryAction DecideRecovery(std::string_view phase, PackageStatus previous, PackageStatus candidate)
    {
        if (phase == "prepared" && (previous == PackageStatus::Missing || previous == PackageStatus::Valid)) return RecoveryAction::RollBack;
        if (candidate == PackageStatus::Valid) return RecoveryAction::RollForward;
        if (previous == PackageStatus::Valid) return RecoveryAction::RollBack;
        if (previous == PackageStatus::Unverifiable || candidate == PackageStatus::Unverifiable) return RecoveryAction::RetryLater;
        return RecoveryAction::ClearBrokenJournal;
    }
    bool ShouldRedirect(int64_t currentSequence, const fs::path& current, int64_t installedSequence,
        const fs::path& installed, std::span<const std::wstring> arguments)
    {
        const bool safe = arguments.empty() || (arguments.size() == 1 &&
            (Lower(Utf8(arguments[0])) == "--launch" || Lower(Utf8(arguments[0])) == "--uninstall"));
        return safe && !SamePath(current, installed) && installedSequence >= currentSequence;
    }
}
