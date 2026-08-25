using System.Diagnostics;
using System.Globalization;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace UvsrInstaller;

internal sealed record ProcessResult(int ExitCode, string StandardOutput, string StandardError);

internal sealed class ProcessRunner
{
    private const int MaximumCapturedCharacters = 1024 * 1024;

    internal async Task<ProcessResult> RunAsync(
        string executable,
        IEnumerable<string> arguments,
        string? workingDirectory,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        executable = Path.GetFullPath(executable);
        if (!File.Exists(executable))
            throw new InstallerException($"A required program was not found: {executable}");

        ProcessStartInfo start = new()
        {
            FileName = executable,
            WorkingDirectory = workingDirectory ?? Path.GetDirectoryName(executable)!,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8
        };
        foreach (string argument in arguments)
            start.ArgumentList.Add(argument);

        log.Write($"Starting {Path.GetFileName(executable)}.");
        cancellationToken.ThrowIfCancellationRequested();
        using ProcessJob job = new();
        using ContainedProcess child = ContainedProcess.Start(start, job);
        Process containedProcess = child.Process;
        Task<string> stdoutTask = ReadBoundedAsync(child.StandardOutput,
            Path.GetFileName(executable), log);
        Task<string> stderrTask = ReadBoundedAsync(child.StandardError,
            Path.GetFileName(executable), log);
        try
        {
            await WaitAndKillOnCancellationAsync(containedProcess, job, cancellationToken);
        }
        catch
        {
            // WaitAndKillOnCancellationAsync does not return until the exact child
            // has exited. Drain both redirected pipes before releasing callers'
            // operation lock so no child process can outlive cancellation.
            await Task.WhenAll(stdoutTask, stderrTask);
            throw;
        }
        string stdout = await stdoutTask;
        string stderr = await stderrTask;
        log.Write($"{Path.GetFileName(executable)} exited with code {containedProcess.ExitCode}.");
        return new ProcessResult(containedProcess.ExitCode, stdout.Trim(), stderr.Trim());
    }

    private static async Task WaitAndKillOnCancellationAsync(
        Process process,
        ProcessJob job,
        CancellationToken token)
    {
        try
        {
            await process.WaitForExitAsync(token);
            // A launcher health command has no contract permitting a background
            // child to survive its authoritative process.
            job.Terminate();
            await job.WaitForEmptyAsync(CancellationToken.None);
        }
        catch (OperationCanceledException)
        {
            Exception? terminationFailure = null;
            try
            {
                job.Terminate();
            }
            catch (Exception ex)
            {
                if (!process.HasExited)
                    terminationFailure = ex;
            }
            // Cancellation is not complete until the exact task-owned process tree
            // is gone. If Windows denied termination, wait for its natural exit.
            await process.WaitForExitAsync(CancellationToken.None);
            await job.WaitForEmptyAsync(CancellationToken.None);
            if (terminationFailure is not null)
                throw new InstallerException(
                    "Cancellation was requested, but Windows would not stop the launcher health process. UVSR Launcher waited for it to exit before continuing.",
                    terminationFailure);
            throw;
        }
    }

    private static async Task<string> ReadBoundedAsync(
        StreamReader reader,
        string label,
        InstallLog log)
    {
        StringBuilder captured = new();
        while (await reader.ReadLineAsync() is { } line)
        {
            log.WriteProcessLine(label, line);
            if (captured.Length >= MaximumCapturedCharacters)
                continue;
            int remaining = MaximumCapturedCharacters - captured.Length;
            string bounded = line.Length <= remaining ? line : line[..remaining];
            captured.AppendLine(bounded);
        }
        return captured.ToString();
    }
}

internal sealed class ContainedProcess : IDisposable
{
    private const uint CreateNoWindow = 0x08000000;
    private const uint CreateSuspended = 0x00000004;
    private const uint CreateUnicodeEnvironment = 0x00000400;
    private const uint ExtendedStartupInfoPresent = 0x00080000;
    private const uint StartfUseStdHandles = 0x00000100;
    private const uint Infinite = 0xffffffff;
    private const uint ProcThreadAttributeHandleList = 0x00020002;
    private const uint ResumeThreadFailed = 0xffffffff;
    private const uint GenericRead = 0x80000000;
    private const uint FileShareRead = 0x00000001;
    private const uint FileShareWrite = 0x00000002;
    private const uint OpenExisting = 3;
    private const uint FileAttributeNormal = 0x00000080;

    private readonly AnonymousPipeServerStream _standardOutputPipe;
    private readonly AnonymousPipeServerStream _standardErrorPipe;

    private ContainedProcess(
        Process process,
        AnonymousPipeServerStream standardOutputPipe,
        AnonymousPipeServerStream standardErrorPipe)
    {
        Process = process;
        _standardOutputPipe = standardOutputPipe;
        _standardErrorPipe = standardErrorPipe;
        StandardOutput = new StreamReader(_standardOutputPipe, Encoding.UTF8, true, 4096, false);
        StandardError = new StreamReader(_standardErrorPipe, Encoding.UTF8, true, 4096, false);
    }

    internal Process Process { get; }
    internal StreamReader StandardOutput { get; }
    internal StreamReader StandardError { get; }

    internal static ContainedProcess Start(ProcessStartInfo start, ProcessJob job)
    {
        AnonymousPipeServerStream outputPipe = new(
            PipeDirection.In, HandleInheritability.Inheritable);
        AnonymousPipeServerStream errorPipe = new(
            PipeDirection.In, HandleInheritability.Inheritable);
        bool ownershipTransferred = false;
        try
        {
            IntPtr outputHandle = ParsePipeHandle(outputPipe.GetClientHandleAsString());
            IntPtr errorHandle = ParsePipeHandle(errorPipe.GetClientHandleAsString());
            SecurityAttributes inheritable = new()
            {
                Length = Marshal.SizeOf<SecurityAttributes>(),
                InheritHandle = true
            };
            using SafeFileHandle inputHandle = CreateFile(
                "NUL", GenericRead, FileShareRead | FileShareWrite,
                ref inheritable, OpenExisting, FileAttributeNormal, IntPtr.Zero);
            if (inputHandle.IsInvalid)
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());

            IntPtr attributeList = IntPtr.Zero;
            IntPtr inheritedHandles = IntPtr.Zero;
            IntPtr environmentBlock = IntPtr.Zero;
            ProcessInformation information = default;
            bool processCreated = false;
            try
            {
                nuint attributeBytes = 0;
                _ = InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref attributeBytes);
                if (attributeBytes == 0)
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                attributeList = Marshal.AllocHGlobal(checked((nint)attributeBytes));
                if (!InitializeProcThreadAttributeList(attributeList, 1, 0, ref attributeBytes))
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());

                inheritedHandles = Marshal.AllocHGlobal(IntPtr.Size * 3);
                Marshal.WriteIntPtr(inheritedHandles, 0, inputHandle.DangerousGetHandle());
                Marshal.WriteIntPtr(inheritedHandles, IntPtr.Size, outputHandle);
                Marshal.WriteIntPtr(inheritedHandles, IntPtr.Size * 2, errorHandle);
                if (!UpdateProcThreadAttribute(attributeList, 0,
                        (IntPtr)ProcThreadAttributeHandleList, inheritedHandles,
                        (nuint)(IntPtr.Size * 3), IntPtr.Zero, IntPtr.Zero))
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());

                string environment = BuildEnvironmentBlock(start);
                environmentBlock = Marshal.StringToHGlobalUni(environment);
                StartupInfoEx startup = new()
                {
                    StartupInfo = new StartupInfo
                    {
                        Size = (uint)Marshal.SizeOf<StartupInfoEx>(),
                        Flags = StartfUseStdHandles,
                        StandardInput = inputHandle.DangerousGetHandle(),
                        StandardOutput = outputHandle,
                        StandardError = errorHandle
                    },
                    AttributeList = attributeList
                };
                StringBuilder commandLine = new(BuildCommandLine(start));
                uint creationFlags = CreateSuspended | CreateUnicodeEnvironment |
                                     ExtendedStartupInfoPresent;
                if (start.CreateNoWindow)
                    creationFlags |= CreateNoWindow;
                if (!CreateProcess(start.FileName, commandLine, IntPtr.Zero, IntPtr.Zero,
                        true, creationFlags, environmentBlock, start.WorkingDirectory,
                        ref startup, out information))
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                processCreated = true;

                // The primary thread cannot spawn descendants until the whole process
                // is already inside the kill-on-close job.
                job.Assign(information.Process);
                Process process = Process.GetProcessById(checked((int)information.ProcessId));
                _ = process.Handle;
                if (ResumeThread(information.Thread) == ResumeThreadFailed)
                {
                    process.Dispose();
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                }

                outputPipe.DisposeLocalCopyOfClientHandle();
                errorPipe.DisposeLocalCopyOfClientHandle();
                ownershipTransferred = true;
                return new ContainedProcess(process, outputPipe, errorPipe);
            }
            catch
            {
                if (processCreated)
                {
                    _ = TerminateProcess(information.Process, 1);
                    _ = WaitForSingleObject(information.Process, Infinite);
                }
                throw;
            }
            finally
            {
                if (!ownershipTransferred)
                {
                    outputPipe.DisposeLocalCopyOfClientHandle();
                    errorPipe.DisposeLocalCopyOfClientHandle();
                }
                if (information.Thread != IntPtr.Zero)
                    _ = CloseHandle(information.Thread);
                if (information.Process != IntPtr.Zero)
                    _ = CloseHandle(information.Process);
                if (attributeList != IntPtr.Zero)
                    DeleteProcThreadAttributeList(attributeList);
                if (attributeList != IntPtr.Zero)
                    Marshal.FreeHGlobal(attributeList);
                if (inheritedHandles != IntPtr.Zero)
                    Marshal.FreeHGlobal(inheritedHandles);
                if (environmentBlock != IntPtr.Zero)
                    Marshal.FreeHGlobal(environmentBlock);
            }
        }
        catch
        {
            outputPipe.Dispose();
            errorPipe.Dispose();
            throw;
        }
    }

    private static IntPtr ParsePipeHandle(string handle) =>
        new(long.Parse(handle, NumberStyles.Integer, CultureInfo.InvariantCulture));

    private static string BuildEnvironmentBlock(ProcessStartInfo start)
    {
        StringBuilder block = new();
        foreach ((string key, string? value) in start.Environment
                     .OrderBy(pair => pair.Key, StringComparer.OrdinalIgnoreCase))
        {
            if (value is null)
                continue;
            block.Append(key).Append('=').Append(value).Append('\0');
        }
        block.Append('\0');
        return block.ToString();
    }

    private static string BuildCommandLine(ProcessStartInfo start)
    {
        StringBuilder command = new(QuoteArgument(start.FileName));
        foreach (string argument in start.ArgumentList)
            command.Append(' ').Append(QuoteArgument(argument));
        return command.ToString();
    }

    private static string QuoteArgument(string argument)
    {
        if (argument.Length == 0)
            return "\"\"";
        if (!argument.Any(character => char.IsWhiteSpace(character) || character == '"'))
            return argument;

        StringBuilder quoted = new StringBuilder(argument.Length + 2).Append('"');
        int slashes = 0;
        foreach (char character in argument)
        {
            if (character == '\\')
            {
                slashes++;
                continue;
            }
            if (character == '"')
            {
                quoted.Append('\\', slashes * 2 + 1).Append('"');
                slashes = 0;
                continue;
            }
            quoted.Append('\\', slashes).Append(character);
            slashes = 0;
        }
        quoted.Append('\\', slashes * 2).Append('"');
        return quoted.ToString();
    }

    public void Dispose()
    {
        StandardOutput.Dispose();
        StandardError.Dispose();
        Process.Dispose();
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct SecurityAttributes
    {
        internal int Length;
        internal IntPtr SecurityDescriptor;
        [MarshalAs(UnmanagedType.Bool)] internal bool InheritHandle;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct StartupInfo
    {
        internal uint Size;
        internal string? Reserved;
        internal string? Desktop;
        internal string? Title;
        internal uint X;
        internal uint Y;
        internal uint XSize;
        internal uint YSize;
        internal uint XCountChars;
        internal uint YCountChars;
        internal uint FillAttribute;
        internal uint Flags;
        internal ushort ShowWindow;
        internal ushort Reserved2Size;
        internal IntPtr Reserved2;
        internal IntPtr StandardInput;
        internal IntPtr StandardOutput;
        internal IntPtr StandardError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct StartupInfoEx
    {
        internal StartupInfo StartupInfo;
        internal IntPtr AttributeList;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInformation
    {
        internal IntPtr Process;
        internal IntPtr Thread;
        internal uint ProcessId;
        internal uint ThreadId;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFile(
        string fileName,
        uint desiredAccess,
        uint shareMode,
        ref SecurityAttributes securityAttributes,
        uint creationDisposition,
        uint flagsAndAttributes,
        IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool InitializeProcThreadAttributeList(
        IntPtr attributeList,
        int attributeCount,
        uint flags,
        ref nuint size);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool UpdateProcThreadAttribute(
        IntPtr attributeList,
        uint flags,
        IntPtr attribute,
        IntPtr value,
        nuint size,
        IntPtr previousValue,
        IntPtr returnSize);

    [DllImport("kernel32.dll")]
    private static extern void DeleteProcThreadAttributeList(IntPtr attributeList);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcess(
        string applicationName,
        StringBuilder commandLine,
        IntPtr processAttributes,
        IntPtr threadAttributes,
        [MarshalAs(UnmanagedType.Bool)] bool inheritHandles,
        uint creationFlags,
        IntPtr environment,
        string currentDirectory,
        ref StartupInfoEx startupInfo,
        out ProcessInformation processInformation);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint ResumeThread(IntPtr thread);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool TerminateProcess(IntPtr process, uint exitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);
}

internal sealed class ProcessJob : IDisposable
{
    private const uint JobObjectLimitKillOnJobClose = 0x00002000;
    private readonly SafeFileHandle _handle;

    internal ProcessJob()
    {
        _handle = CreateJobObject(IntPtr.Zero, null);
        if (_handle.IsInvalid)
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        JobObjectExtendedLimitInformation limits = new();
        limits.BasicLimitInformation.LimitFlags = JobObjectLimitKillOnJobClose;
        int length = Marshal.SizeOf<JobObjectExtendedLimitInformation>();
        IntPtr pointer = Marshal.AllocHGlobal(length);
        try
        {
            Marshal.StructureToPtr(limits, pointer, false);
            if (!SetInformationJobObject(_handle, 9, pointer, (uint)length))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }
        catch
        {
            _handle.Dispose();
            throw;
        }
        finally
        {
            Marshal.FreeHGlobal(pointer);
        }
    }

    internal void Assign(IntPtr process)
    {
        if (AssignProcessToJobObject(_handle, process))
            return;
        int error = Marshal.GetLastWin32Error();
        throw new System.ComponentModel.Win32Exception(error);
    }

    internal void Terminate()
    {
        if (ActiveProcessCount() == 0)
            return;
        if (!TerminateJobObject(_handle, 1))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    }

    internal async Task WaitForEmptyAsync(CancellationToken cancellationToken)
    {
        while (ActiveProcessCount() != 0)
            await Task.Delay(25, cancellationToken);
    }

    private uint ActiveProcessCount()
    {
        if (!QueryInformationJobObject(_handle, 1,
                out JobObjectBasicAccountingInformation accounting,
                (uint)Marshal.SizeOf<JobObjectBasicAccountingInformation>(), out _))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        return accounting.ActiveProcesses;
    }

    public void Dispose() => _handle.Dispose();

    [StructLayout(LayoutKind.Sequential)]
    private struct JobObjectBasicAccountingInformation
    {
        internal long TotalUserTime;
        internal long TotalKernelTime;
        internal long ThisPeriodTotalUserTime;
        internal long ThisPeriodTotalKernelTime;
        internal uint TotalPageFaultCount;
        internal uint TotalProcesses;
        internal uint ActiveProcesses;
        internal uint TotalTerminatedProcesses;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct JobObjectBasicLimitInformation
    {
        internal long PerProcessUserTimeLimit;
        internal long PerJobUserTimeLimit;
        internal uint LimitFlags;
        internal UIntPtr MinimumWorkingSetSize;
        internal UIntPtr MaximumWorkingSetSize;
        internal uint ActiveProcessLimit;
        internal UIntPtr Affinity;
        internal uint PriorityClass;
        internal uint SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct IoCounters
    {
        internal ulong ReadOperationCount;
        internal ulong WriteOperationCount;
        internal ulong OtherOperationCount;
        internal ulong ReadTransferCount;
        internal ulong WriteTransferCount;
        internal ulong OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct JobObjectExtendedLimitInformation
    {
        internal JobObjectBasicLimitInformation BasicLimitInformation;
        internal IoCounters IoInfo;
        internal UIntPtr ProcessMemoryLimit;
        internal UIntPtr JobMemoryLimit;
        internal UIntPtr PeakProcessMemoryUsed;
        internal UIntPtr PeakJobMemoryUsed;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateJobObject(IntPtr securityAttributes, string? name);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetInformationJobObject(
        SafeFileHandle job,
        int informationClass,
        IntPtr information,
        uint informationLength);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AssignProcessToJobObject(SafeFileHandle job, IntPtr process);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool TerminateJobObject(SafeFileHandle job, uint exitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool QueryInformationJobObject(
        SafeFileHandle job,
        int informationClass,
        out JobObjectBasicAccountingInformation information,
        uint informationLength,
        out uint returnLength);
}
