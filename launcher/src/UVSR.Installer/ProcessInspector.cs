using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace UvsrInstaller;

internal enum TrackedProcessState
{
    NotRunning,
    Running,
    Unverifiable
}

internal sealed record ExactProcessIdentity(
    int ProcessId,
    long CreationTimeUtcFileTime,
    string ExecutablePath);

internal sealed record ExactProcessInspection(
    TrackedProcessState State,
    IReadOnlyList<ExactProcessIdentity> Matches);

internal static class ProcessInspector
{
    private const uint ProcessQueryLimitedInformation = 0x1000;
    private const uint ProcessTerminate = 0x0001;
    private const uint Synchronize = 0x00100000;
    private const int ErrorInvalidParameter = 87;
    private const uint WaitObject0 = 0x00000000;
    private const uint WaitTimeout = 0x00000102;

    internal static ExactProcessInspection InspectManagedLauncherProcesses(
        string programRoot,
        bool excludeCurrentProcess = false)
    {
        List<ExactProcessIdentity> matches = new();
        bool unverifiable = false;
        foreach (string processName in new[]
                 {
                     Path.GetFileNameWithoutExtension(ProductConstants.LauncherExecutableName),
                     Path.GetFileNameWithoutExtension(ProductConstants.LegacyInstalledManagerName)
                 })
        {
            ExactProcessInspection inspection = InspectProcesses(
                processName,
                path => SafePaths.IsStrictDescendant(path, programRoot),
                excludeCurrentProcess ? Environment.ProcessId : null);
            matches.AddRange(inspection.Matches);
            unverifiable |= inspection.State == TrackedProcessState.Unverifiable;
        }
        ExactProcessIdentity[] distinct = matches
            .DistinctBy(process => (process.ProcessId,
                process.CreationTimeUtcFileTime, process.ExecutablePath))
            .ToArray();
        return new ExactProcessInspection(
            distinct.Length > 0
                ? TrackedProcessState.Running
                : unverifiable
                    ? TrackedProcessState.Unverifiable
                    : TrackedProcessState.NotRunning,
            distinct);
    }

    internal static ExactProcessInspection InspectProcessesByExecutable(
        string executable)
    {
        string expected = Path.GetFullPath(executable);
        string processName = Path.GetFileNameWithoutExtension(expected);
        return InspectProcesses(processName, path => string.Equals(
            Path.GetFullPath(path), expected, StringComparison.OrdinalIgnoreCase));
    }

    internal static ExactProcessInspection InspectManagedUvsrProcesses(
        string programRoot)
        => InspectProcesses("uvsr", path =>
            SafePaths.IsStrictDescendant(path, programRoot));

    private static ExactProcessInspection InspectProcesses(
        string processName,
        Func<string, bool> pathMatches,
        int? excludedProcessId = null)
    {
        List<ExactProcessIdentity> matches = new();
        bool unverifiable = false;
        Process[] candidates;
        try
        {
            candidates = Process.GetProcessesByName(processName);
        }
        catch (Exception ex) when (ex is InvalidOperationException or
                                   System.ComponentModel.Win32Exception)
        {
            return new ExactProcessInspection(
                TrackedProcessState.Unverifiable, matches);
        }
        foreach (Process process in candidates)
        {
            using (process)
            {
                if (process.Id == excludedProcessId)
                    continue;
                ProcessQuery query = QueryProcess(process.Id);
                if (query.State == ProcessQueryState.Unverifiable)
                {
                    unverifiable = true;
                    continue;
                }
                if (query.State != ProcessQueryState.Running)
                    continue;
                try
                {
                    string path = Path.GetFullPath(query.ExecutablePath!);
                    if (pathMatches(path))
                        matches.Add(new ExactProcessIdentity(
                            process.Id, query.CreationTimeUtcFileTime, path));
                }
                catch (Exception ex) when (ex is ArgumentException or
                                           NotSupportedException or
                                           InstallerException)
                {
                    unverifiable = true;
                }
            }
        }
        return new ExactProcessInspection(
            matches.Count > 0
                ? TrackedProcessState.Running
                : unverifiable
                    ? TrackedProcessState.Unverifiable
                    : TrackedProcessState.NotRunning,
            matches);
    }

    internal static bool IsConfirmedNotRunning(ExactProcessInspection inspection) =>
        inspection.State == TrackedProcessState.NotRunning;

    internal static TrackedProcessState ClassifyProcessHandleWait(uint waitResult) =>
        waitResult == WaitObject0
            ? TrackedProcessState.NotRunning
            : waitResult == WaitTimeout
                ? TrackedProcessState.Running
                : TrackedProcessState.Unverifiable;

    internal static TrackedProcessState InspectExactProcess(
        ExactProcessIdentity expected)
    {
        ProcessQuery query = QueryProcess(expected.ProcessId);
        if (query.State == ProcessQueryState.Unverifiable)
            return TrackedProcessState.Unverifiable;
        if (query.State != ProcessQueryState.Running)
            return TrackedProcessState.NotRunning;
        return MatchesExactProcess(expected, expected.ProcessId,
            query.ExecutablePath!, query.CreationTimeUtcFileTime)
            ? TrackedProcessState.Running
            : TrackedProcessState.NotRunning;
    }

    internal static ExactProcessIdentity? TryCaptureExactProcess(int processId)
    {
        ProcessQuery query = QueryProcess(processId);
        return query.State == ProcessQueryState.Running
            ? new ExactProcessIdentity(processId, query.CreationTimeUtcFileTime,
                Path.GetFullPath(query.ExecutablePath!))
            : null;
    }

    internal static bool MatchesExactProcess(
        ExactProcessIdentity expected,
        int actualProcessId,
        string actualExecutablePath,
        long actualCreationTimeUtcFileTime) =>
        expected.ProcessId == actualProcessId &&
        expected.CreationTimeUtcFileTime == actualCreationTimeUtcFileTime &&
        string.Equals(Path.GetFullPath(expected.ExecutablePath),
            Path.GetFullPath(actualExecutablePath), StringComparison.OrdinalIgnoreCase);

    internal static bool TryCloseMainWindow(ExactProcessIdentity expected)
    {
        using Process? process = TryOpenExactProcess(expected);
        return process is not null && process.CloseMainWindow();
    }

    internal static bool TryTerminate(ExactProcessIdentity expected)
    {
        using SafeFileHandle process = OpenProcess(
            ProcessQueryLimitedInformation | Synchronize | ProcessTerminate,
            false, expected.ProcessId);
        if (process.IsInvalid)
            return false;

        ProcessQuery query = QueryProcessHandle(process);
        if (query.State != ProcessQueryState.Running ||
            !MatchesExactProcess(expected, expected.ProcessId,
                query.ExecutablePath!, query.CreationTimeUtcFileTime))
            return false;
        // Terminate the same kernel object that was queried above. Reopening by
        // PID here would allow PID reuse to redirect an explicit force-close.
        return TerminateProcess(process, 1);
    }

    private static Process? TryOpenExactProcess(ExactProcessIdentity expected)
    {
        Process? process = null;
        try
        {
            process = Process.GetProcessById(expected.ProcessId);
            if (process.HasExited)
            {
                process.Dispose();
                return null;
            }
            string? path = process.MainModule?.FileName;
            long creationTime = process.StartTime.ToUniversalTime().ToFileTimeUtc();
            if (path is null || !MatchesExactProcess(expected, process.Id,
                    path, creationTime))
            {
                process.Dispose();
                return null;
            }
            return process;
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or
                                   System.ComponentModel.Win32Exception)
        {
            process?.Dispose();
            return null;
        }
    }

    internal static TrackedProcessState InspectTrackedProcess(
        VisualStudioOperationRecord expected)
    {
        string expectedPath;
        try
        {
            expectedPath = Path.GetFullPath(expected.ExecutablePath);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException)
        {
            return TrackedProcessState.Unverifiable;
        }

        if (expected.ProcessId is int processId &&
            expected.CreationTimeUtcFileTime is not null)
        {
            ProcessQuery query = QueryProcess(processId);
            if (query.State == ProcessQueryState.Unverifiable)
                return TrackedProcessState.Unverifiable;
            if (query.State == ProcessQueryState.Running &&
                MatchesTrackedProcess(expected, processId,
                    query.ExecutablePath!, query.CreationTimeUtcFileTime))
                return TrackedProcessState.Running;
        }

        // A crash can occur after ShellExecute starts the elevated program but
        // before its PID reaches the journal. Search only the exact recorded
        // executable path. An inaccessible same-name process is ambiguous and
        // therefore blocks a second elevated operation.
        bool unverifiableCandidate = false;
        string processName = Path.GetFileNameWithoutExtension(expectedPath);
        Process[] candidates;
        try
        {
            candidates = Process.GetProcessesByName(processName);
        }
        catch (Exception ex) when (ex is InvalidOperationException or
                                   System.ComponentModel.Win32Exception)
        {
            return TrackedProcessState.Unverifiable;
        }
        foreach (Process process in candidates)
        {
            using (process)
            {
                ProcessQuery query = QueryProcess(process.Id);
                if (query.State == ProcessQueryState.Unverifiable)
                {
                    unverifiableCandidate = true;
                    continue;
                }
                if (query.State == ProcessQueryState.Running)
                {
                    try
                    {
                        if (string.Equals(Path.GetFullPath(query.ExecutablePath!),
                                expectedPath, StringComparison.OrdinalIgnoreCase))
                            return TrackedProcessState.Running;
                    }
                    catch (Exception ex) when (ex is ArgumentException or
                                               NotSupportedException)
                    {
                        unverifiableCandidate = true;
                    }
                }
            }
        }
        return unverifiableCandidate
            ? TrackedProcessState.Unverifiable
            : TrackedProcessState.NotRunning;
    }

    internal static bool MatchesTrackedProcess(
        VisualStudioOperationRecord expected,
        int actualProcessId,
        string actualExecutablePath,
        long actualCreationTimeUtcFileTime) =>
        expected.ProcessId == actualProcessId &&
        expected.CreationTimeUtcFileTime == actualCreationTimeUtcFileTime &&
        string.Equals(Path.GetFullPath(expected.ExecutablePath),
            Path.GetFullPath(actualExecutablePath), StringComparison.OrdinalIgnoreCase);

    private static ProcessQuery QueryProcess(int processId)
    {
        using SafeFileHandle process = OpenProcess(
            ProcessQueryLimitedInformation | Synchronize, false, processId);
        if (process.IsInvalid)
        {
            int error = Marshal.GetLastWin32Error();
            return error == ErrorInvalidParameter
                ? new ProcessQuery(ProcessQueryState.NotRunning, null, 0)
                : new ProcessQuery(ProcessQueryState.Unverifiable, null, 0);
        }

        return QueryProcessHandle(process);
    }

    private static ProcessQuery QueryProcessHandle(SafeFileHandle process)
    {
        TrackedProcessState waitState = ClassifyProcessHandleWait(
            WaitForSingleObject(process, 0));
        if (waitState == TrackedProcessState.NotRunning)
            return new ProcessQuery(ProcessQueryState.NotRunning, null, 0);
        if (waitState == TrackedProcessState.Unverifiable)
            return new ProcessQuery(ProcessQueryState.Unverifiable, null, 0);

        StringBuilder path = new(32768);
        int pathLength = path.Capacity;
        if (!QueryFullProcessImageName(process, 0, path, ref pathLength) ||
            !GetProcessTimes(process, out FileTime creation, out _, out _, out _))
        {
            return new ProcessQuery(ProcessQueryState.Unverifiable, null, 0);
        }
        long creationTime = unchecked((long)(((ulong)creation.High << 32) | creation.Low));
        return new ProcessQuery(ProcessQueryState.Running, path.ToString(), creationTime);
    }

    private enum ProcessQueryState
    {
        NotRunning,
        Running,
        Unverifiable
    }

    private sealed record ProcessQuery(
        ProcessQueryState State,
        string? ExecutablePath,
        long CreationTimeUtcFileTime);

    [StructLayout(LayoutKind.Sequential)]
    private struct FileTime
    {
        internal uint Low;
        internal uint High;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeFileHandle OpenProcess(
        uint desiredAccess,
        [MarshalAs(UnmanagedType.Bool)] bool inheritHandle,
        int processId);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool QueryFullProcessImageName(
        SafeFileHandle process,
        uint flags,
        StringBuilder executableName,
        ref int size);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetProcessTimes(
        SafeFileHandle process,
        out FileTime creationTime,
        out FileTime exitTime,
        out FileTime kernelTime,
        out FileTime userTime);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(
        SafeFileHandle handle,
        uint milliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool TerminateProcess(
        SafeFileHandle process,
        uint exitCode);
}
