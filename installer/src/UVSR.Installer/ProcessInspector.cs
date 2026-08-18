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

internal static class ProcessInspector
{
    private const uint ProcessQueryLimitedInformation = 0x1000;
    private const uint Synchronize = 0x00100000;
    private const int ErrorInvalidParameter = 87;

    internal static IReadOnlyList<int> FindManagedLauncherProcesses(
        string programRoot,
        bool excludeCurrentProcess = false)
    {
        List<int> result = new();
        foreach (string processName in new[]
                 {
                     Path.GetFileNameWithoutExtension(ProductConstants.LauncherExecutableName),
                     Path.GetFileNameWithoutExtension(ProductConstants.LegacyInstalledManagerName)
                 })
        {
            foreach (Process process in Process.GetProcessesByName(processName))
            {
                using (process)
                {
                    if (excludeCurrentProcess && process.Id == Environment.ProcessId)
                        continue;
                    try
                    {
                        string? path = process.MainModule?.FileName;
                        if (path is not null && SafePaths.IsStrictDescendant(path, programRoot))
                            result.Add(process.Id);
                    }
                    catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
                    {
                        // A process that cannot be attributed is never treated as installer-owned.
                    }
                }
            }
        }
        return result.Distinct().ToArray();
    }

    internal static IReadOnlyList<int> FindProcessesByExecutable(string executable)
    {
        string expected = Path.GetFullPath(executable);
        string processName = Path.GetFileNameWithoutExtension(expected);
        List<int> result = new();
        foreach (Process process in Process.GetProcessesByName(processName))
        {
            using (process)
            {
                try
                {
                    string? path = process.MainModule?.FileName;
                    if (path is not null && string.Equals(Path.GetFullPath(path), expected,
                            StringComparison.OrdinalIgnoreCase))
                        result.Add(process.Id);
                }
                catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
                {
                    // A process that cannot be attributed is never treated as installer-owned.
                }
            }
        }
        return result;
    }

    internal static IReadOnlyList<int> FindManagedUvsrProcesses(string programRoot)
    {
        List<int> result = new();
        foreach (Process process in Process.GetProcessesByName("uvsr"))
        {
            using (process)
            {
                try
                {
                    string? path = process.MainModule?.FileName;
                    if (path is not null && SafePaths.IsStrictDescendant(path, programRoot))
                        result.Add(process.Id);
                }
                catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
                {
                    // A process that cannot be attributed is never treated as installer-owned.
                }
            }
        }
        return result;
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
}
