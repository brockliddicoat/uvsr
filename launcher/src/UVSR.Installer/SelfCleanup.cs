using System.Diagnostics;
using System.Text;

namespace UvsrInstaller;

internal static class SelfCleanup
{
    private const uint MoveFileDelayUntilReboot = 0x4;
    private const string Prepared = "prepared";
    private const string RemovingShell = "removing-shell";
    private const string ShellRemoved = "shell-removed";
    private const string ProgramMoved = "program-moved";
    private const string RootsMoved = "roots-moved";
    private const string Deleting = "deleting";

    internal static void Schedule(
        InstallerPaths paths,
        Guid installationId,
        Guid transactionId,
        InstallState? previousState,
        InstallLog log)
    {
        EnsureOperationsRoot(paths, installationId);
        if (File.Exists(paths.UninstallRecordFile))
            throw new InstallerException(
                "A UVSR uninstall is already pending. Close the earlier launcher window, then try again.");
        UninstallRecord record = new(ProductConstants.SchemaVersion,
            ProductConstants.ProductId, installationId, transactionId, Prepared,
            previousState, DateTimeOffset.UtcNow);
        JsonStore.WriteAtomic(paths.UninstallRecordFile, record);
        try
        {
            LaunchHelper(paths, record);
        }
        catch
        {
            TryDeleteFile(paths.UninstallRecordFile);
            throw;
        }
        log.Write("Scheduled serialized UVSR cleanup after this launcher window closes.");
    }

    internal static int RunHelper(
        int parentProcessId,
        long parentStartTimeUtcTicks,
        Guid installationId,
        Guid transactionId)
    {
        try
        {
            WaitForExactParent(parentProcessId, parentStartTimeUtcTicks);
            InstallerPaths paths = InstallerPaths.ForCurrentUser();
            using OperationLock operationLock = OperationLock.Acquire();
            OwnerMarker operationsMarker = ReadOperationsMarker(paths);
            UninstallRecord record = ReadUninstallRecord(paths);
            if (operationsMarker.InstallationId != installationId ||
                record.InstallationId != installationId ||
                record.TransactionId != transactionId)
                return 3;

            string programTombstone = ProgramTombstone(paths, transactionId);
            string stateTombstone = StateTombstone(paths, transactionId);
            if (!ProcessInspector.IsConfirmedNotRunning(
                    ProcessInspector.InspectManagedUvsrProcesses(paths.ProgramRoot)) ||
                !ProcessInspector.IsConfirmedNotRunning(
                    ProcessInspector.InspectManagedUvsrProcesses(programTombstone)) ||
                !ProcessInspector.IsConfirmedNotRunning(
                    ProcessInspector.InspectManagedLauncherProcesses(paths.ProgramRoot)) ||
                !ProcessInspector.IsConfirmedNotRunning(
                    ProcessInspector.InspectManagedLauncherProcesses(programTombstone)))
                return 4;

            string cleanupLogRoot = Path.Combine(paths.OperationsRoot, "logs",
                transactionId.ToString("N"));
            InstallLog log = new(cleanupLogRoot);
            ShellIntegration shell = new(paths);

            if (record.Phase == Prepared)
            {
                new OwnershipManager(paths).ValidateBoth(installationId);
                record = WritePhase(paths, record, RemovingShell);
            }
            if (record.Phase == RemovingShell)
            {
                shell.Remove(installationId, log);
                record = WritePhase(paths, record, ShellRemoved);
            }
            if (record.Phase == ShellRemoved)
            {
                MoveOwnedRoot(paths.ProgramRoot, programTombstone,
                    paths.ProgramMarker, installationId, "UVSR program directory");
                record = WritePhase(paths, record, ProgramMoved);
            }
            if (record.Phase == ProgramMoved)
            {
                MoveOwnedRoot(paths.StateRoot, stateTombstone,
                    paths.StateMarker, installationId, "UVSR installer-state directory");
                record = WritePhase(paths, record, RootsMoved);
            }
            if (record.Phase == RootsMoved)
                record = WritePhase(paths, record, Deleting);
            if (record.Phase != Deleting)
                return 5;

            if (Directory.Exists(paths.ProgramRoot) || Directory.Exists(paths.StateRoot))
                return 6;
            DeleteWithRetries(programTombstone,
                Path.Combine(paths.LocalApplicationData, "Programs"));
            DeleteWithRetries(stateTombstone, paths.LocalApplicationData);
            File.Delete(paths.UninstallRecordFile);
            if (File.Exists(paths.UninstallRecordFile))
                return 7;
            TryDeleteTree(cleanupLogRoot, Path.Combine(paths.OperationsRoot, "logs"));
            return 0;
        }
        catch
        {
            return 1;
        }
    }

    internal static bool ScheduleCurrentHelperDeletion(Guid installationId)
    {
        string self = Environment.ProcessPath ?? string.Empty;
        string? watcher = null;
        try
        {
            if (string.IsNullOrWhiteSpace(self))
                return false;
            InstallerPaths paths = InstallerPaths.ForCurrentUser();
            OwnerMarker marker = ReadOperationsMarker(paths);
            if (marker.InstallationId != installationId)
                return false;
            string? helperDirectory = Path.GetDirectoryName(self);
            if (string.IsNullOrWhiteSpace(helperDirectory) ||
                !string.Equals(Path.GetDirectoryName(helperDirectory),
                    paths.HelpersDirectory, StringComparison.OrdinalIgnoreCase) ||
                !string.Equals(Path.GetFileName(self), "UVSR-Launcher-Cleanup.exe",
                    StringComparison.OrdinalIgnoreCase))
                return false;
            SafePaths.RejectReparsePathChain(paths.OperationsRoot,
                "UVSR cleanup-coordination directory");
            SafePaths.RejectReparsePathChain(helperDirectory,
                "UVSR cleanup-helper directory");
            SafePaths.RejectReparsePathChain(self, "UVSR cleanup-helper executable");

            string commandProcessor = Path.Combine(Environment.SystemDirectory, "cmd.exe");
            string ping = Path.Combine(Environment.SystemDirectory, "PING.EXE");
            if (!File.Exists(commandProcessor) || !File.Exists(ping))
                return RegisterDeferredCleanup(self, helperDirectory);

            string watcherName = $"cleanup-watch-{Environment.ProcessId}-{Guid.NewGuid():N}.cmd";
            watcher = SafePaths.CombineDescendant(paths.OperationsRoot, watcherName);
            string script = BuildSelfDeleteScript(self, helperDirectory, ping);
            using (FileStream output = new(watcher, FileMode.CreateNew, FileAccess.Write,
                       FileShare.Read, 4096, FileOptions.WriteThrough))
            {
                using StreamWriter writer = new(output, new UTF8Encoding(false),
                    1024, leaveOpen: true);
                writer.Write(script);
                writer.Flush();
                output.Flush(flushToDisk: true);
            }
            ProcessStartInfo start = new()
            {
                FileName = commandProcessor,
                WorkingDirectory = paths.OperationsRoot,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            start.ArgumentList.Add("/d");
            start.ArgumentList.Add("/q");
            start.ArgumentList.Add("/c");
            start.ArgumentList.Add(watcherName);
            using Process? process = Process.Start(start);
            if (process is null)
                throw new IOException("Windows did not start the cleanup watcher.");
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or
                                   InstallerException or System.ComponentModel.Win32Exception)
        {
            if (!string.IsNullOrWhiteSpace(watcher))
                TryDeleteFile(watcher);
            string? helperDirectory = string.IsNullOrWhiteSpace(self)
                ? null
                : Path.GetDirectoryName(self);
            bool deferred = !string.IsNullOrWhiteSpace(helperDirectory) &&
                RegisterDeferredCleanup(self, helperDirectory);
            LogPendingCleanup(self, ex.Message);
            return deferred;
        }
    }

    internal static string BuildSelfDeleteScript(
        string executable,
        string helperDirectory,
        string? pingExecutable = null,
        int maximumAttempts = 45)
    {
        if (maximumAttempts is < 1 or > 45)
            throw new InstallerException("The cleanup retry count is invalid.");
        executable = ValidateBatchPath(executable, "cleanup-helper executable");
        helperDirectory = ValidateBatchPath(helperDirectory,
            "cleanup-helper directory");
        pingExecutable = ValidateBatchPath(pingExecutable ??
            Path.Combine(Environment.SystemDirectory, "PING.EXE"),
            "Windows wait utility");
        return string.Join("\r\n", new[]
        {
            "@echo off",
            "setlocal DisableDelayedExpansion",
            $"set \"uvsr_cleanup_file={EscapeBatchValue(executable)}\"",
            $"set \"uvsr_cleanup_dir={EscapeBatchValue(helperDirectory)}\"",
            $"set \"uvsr_ping={EscapeBatchValue(pingExecutable)}\"",
            $"for /L %%i in (1,1,{maximumAttempts}) do (",
            "  del /f /q \"%uvsr_cleanup_file%\" >nul 2>&1",
            "  if not exist \"%uvsr_cleanup_file%\" goto :removed",
            "  \"%uvsr_ping%\" -n 2 127.0.0.1 >nul 2>&1",
            ")",
            "if not exist \"logs\" md \"logs\" >nul 2>&1",
            ">> \"logs\\cleanup-pending.log\" echo Cleanup watcher timed out; UVSR Launcher will retry on its next start.",
            "goto :finished",
            ":removed",
            "rd /q \"%uvsr_cleanup_dir%\" >nul 2>&1",
            ":finished",
            "endlocal",
            "(",
            "  del /f /q \"%~f0\" >nul 2>&1",
            "  exit /b 0",
            ")",
            string.Empty
        });
    }

    private static string ValidateBatchPath(string path, string description)
    {
        if (string.IsNullOrWhiteSpace(path) || path.IndexOfAny(new[] { '\r', '\n', '"' }) >= 0)
            throw new InstallerException($"The {description} path cannot be cleaned safely.");
        return Path.GetFullPath(path);
    }

    private static string EscapeBatchValue(string value) => value.Replace("%", "%%",
        StringComparison.Ordinal);

    private static bool RegisterDeferredCleanup(string self, string helperDirectory)
    {
        bool fileScheduled = NativeMethods.MoveFileEx(
            self, null, MoveFileDelayUntilReboot);
        bool directoryScheduled = NativeMethods.MoveFileEx(
            helperDirectory, null, MoveFileDelayUntilReboot);
        if (!fileScheduled || !directoryScheduled)
            LogPendingCleanup(self, "Windows deferred deletion was unavailable.");
        return fileScheduled && directoryScheduled;
    }

    private static void LogPendingCleanup(string self, string reason)
    {
        try
        {
            InstallerPaths paths = InstallerPaths.ForCurrentUser();
            string logs = Path.Combine(paths.OperationsRoot, "logs");
            Directory.CreateDirectory(logs);
            File.AppendAllText(Path.Combine(logs, "cleanup-pending.log"),
                $"{DateTimeOffset.UtcNow:O} Cleanup remains pending for {self}: {reason}{Environment.NewLine}");
        }
        catch { }
    }

    internal static void RecoverInterruptedUninstall(InstallerPaths paths)
    {
        if (!File.Exists(paths.UninstallRecordFile))
            return;
        using OperationLock operationLock = OperationLock.Acquire();
        UninstallRecord record = ReadUninstallRecord(paths);
        OwnerMarker marker = ReadOperationsMarker(paths);
        if (marker.InstallationId != record.InstallationId)
            throw new InstallerException(
                "An interrupted UVSR uninstall has mismatched ownership records. No files were changed.");

        // Once scheduling succeeds, recovery always resumes the user's confirmed
        // uninstall. A second installer instance must never cancel a helper that
        // is merely waiting for the original success dialog to close.
        LaunchHelper(paths, record);
        throw new InstallerException(
            "Windows interrupted an earlier UVSR uninstall. Cleanup will resume after this message closes. Reopen UVSR Launcher when it finishes.");
    }

    internal static void RemoveStaleHelpers(InstallerPaths paths)
    {
        if (!Directory.Exists(paths.OperationsRoot))
            return;
        using OperationLock operationLock = OperationLock.Acquire();
        ReadOperationsMarker(paths);
        if (File.Exists(paths.UninstallRecordFile))
            return;
        bool helpersRemoved = TryDeleteTree(paths.HelpersDirectory,
            paths.OperationsRoot);
        bool logsRemoved = TryDeleteTree(Path.Combine(paths.OperationsRoot, "logs"),
            paths.OperationsRoot);
        if (!helpersRemoved || !logsRemoved || !Directory.Exists(paths.OperationsRoot))
            return;
        try
        {
            string[] remaining = Directory.EnumerateFileSystemEntries(
                paths.OperationsRoot, "*", SearchOption.TopDirectoryOnly).ToArray();
            if (remaining.Any(path => !string.Equals(path, paths.OperationsMarker,
                    StringComparison.OrdinalIgnoreCase)))
                return;
            string retired = Path.Combine(paths.LocalApplicationData,
                $"UVSR Installer Operations.retired-{Guid.NewGuid():N}");
            SafePaths.RejectReparsePathChain(retired,
                "retired UVSR cleanup-coordination directory");
            Directory.Move(paths.OperationsRoot, retired);
            TryDeleteTree(retired, paths.LocalApplicationData);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InstallerException)
        {
            // Preserve the root marker if an active helper or log still owns it.
        }
    }

    private static void LaunchHelper(InstallerPaths paths, UninstallRecord record)
    {
        string current = Environment.ProcessPath
            ?? throw new InstallerException("Windows could not locate the running UVSR Launcher.");
        string helperRoot = Path.Combine(paths.HelpersDirectory,
            $"{record.TransactionId:N}-{Guid.NewGuid():N}");
        SafePaths.RejectReparsePathChain(paths.HelpersDirectory,
            "UVSR cleanup-helper directory");
        Directory.CreateDirectory(helperRoot);
        SafePaths.RejectReparsePathChain(helperRoot,
            "UVSR cleanup-helper directory");
        string helper = Path.Combine(helperRoot, "UVSR-Launcher-Cleanup.exe");
        try
        {
            File.Copy(current, helper, overwrite: false);
            if (!string.Equals(PayloadPackager.ComputeSha256(current),
                    PayloadPackager.ComputeSha256(helper), StringComparison.Ordinal))
                throw new InstallerException("The UVSR cleanup helper did not copy correctly.");
            using Process parent = Process.GetCurrentProcess();
            ProcessStartInfo start = new()
            {
                FileName = helper,
                WorkingDirectory = helperRoot,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            start.ArgumentList.Add("--cleanup");
            start.ArgumentList.Add(Environment.ProcessId.ToString(
                System.Globalization.CultureInfo.InvariantCulture));
            start.ArgumentList.Add(parent.StartTime.ToUniversalTime().Ticks.ToString(
                System.Globalization.CultureInfo.InvariantCulture));
            start.ArgumentList.Add(record.InstallationId.ToString("D"));
            start.ArgumentList.Add(record.TransactionId.ToString("D"));
            if (Process.Start(start) is null)
                throw new InstallerException("Windows could not start the UVSR cleanup helper.");
        }
        catch
        {
            TryDeleteTree(helperRoot, paths.HelpersDirectory);
            throw;
        }
    }

    internal static void EnsureOperationsRoot(InstallerPaths paths, Guid installationId)
    {
        SafePaths.RejectReparsePathChain(paths.OperationsRoot,
            "UVSR cleanup-coordination directory");
        if (Directory.Exists(paths.OperationsRoot))
        {
            if (!File.Exists(paths.OperationsMarker))
            {
                if (Directory.EnumerateFileSystemEntries(paths.OperationsRoot).Any())
                    throw new InstallerException(
                        "The UVSR cleanup-coordination directory has an unknown owner. It was preserved.");
                JsonStore.WriteAtomic(paths.OperationsMarker, new OwnerMarker(
                    ProductConstants.SchemaVersion, ProductConstants.ProductId,
                    installationId));
            }
            OwnerMarker existing = ReadOperationsMarker(paths);
            if (File.Exists(paths.UninstallRecordFile))
                throw new InstallerException(
                    "An earlier UVSR uninstall still needs to finish. Reopen UVSR Launcher to resume it.");
            if (existing.InstallationId != installationId)
                JsonStore.WriteAtomic(paths.OperationsMarker, existing with
                {
                    InstallationId = installationId
                });
        }
        else
        {
            Directory.CreateDirectory(paths.OperationsRoot);
            JsonStore.WriteAtomic(paths.OperationsMarker, new OwnerMarker(
                ProductConstants.SchemaVersion, ProductConstants.ProductId,
                installationId));
        }
        Directory.CreateDirectory(paths.HelpersDirectory);
        SafePaths.RejectReparsePathChain(paths.HelpersDirectory,
            "UVSR cleanup-helper directory");
    }

    private static OwnerMarker ReadOperationsMarker(InstallerPaths paths)
    {
        SafePaths.RejectReparsePathChain(paths.OperationsRoot,
            "UVSR cleanup-coordination directory");
        OwnerMarker marker = JsonStore.Read<OwnerMarker>(paths.OperationsMarker);
        if (marker.SchemaVersion != ProductConstants.SchemaVersion ||
            marker.InstallationId == Guid.Empty ||
            !string.Equals(marker.ProductId, ProductConstants.ProductId,
                StringComparison.OrdinalIgnoreCase))
            throw new InstallerException(
                "The UVSR cleanup-coordination directory has an unknown owner. It was preserved.");
        return marker;
    }

    private static UninstallRecord ReadUninstallRecord(InstallerPaths paths)
    {
        UninstallRecord record = JsonStore.Read<UninstallRecord>(
            paths.UninstallRecordFile);
        if (record.SchemaVersion != ProductConstants.SchemaVersion ||
            record.InstallationId == Guid.Empty || record.TransactionId == Guid.Empty ||
            string.IsNullOrWhiteSpace(record.ProductId) ||
            !string.Equals(record.ProductId, ProductConstants.ProductId,
                StringComparison.OrdinalIgnoreCase) ||
            record.Phase is not (Prepared or RemovingShell or ShellRemoved or
                ProgramMoved or RootsMoved or Deleting))
            throw new InstallerException(
                "The interrupted UVSR uninstall record is invalid. No files were changed.");
        record.PreviousState?.Validate(record.InstallationId);
        return record;
    }

    private static UninstallRecord WritePhase(
        InstallerPaths paths,
        UninstallRecord record,
        string phase)
    {
        record = record with { Phase = phase };
        JsonStore.WriteAtomic(paths.UninstallRecordFile, record);
        return record;
    }

    private static void MoveOwnedRoot(
        string source,
        string tombstone,
        string sourceMarker,
        Guid installationId,
        string description)
    {
        if (Directory.Exists(tombstone))
        {
            if (Directory.Exists(source))
                throw new InstallerException(
                    $"The interrupted {description} has two copies. No files were removed.");
            ValidateMarker(Path.Combine(tombstone, ProductConstants.OwnerMarkerName),
                installationId);
            return;
        }
        if (!Directory.Exists(source))
            throw new InstallerException(
                $"The {description} is missing before cleanup could secure it.");
        ValidateMarker(sourceMarker, installationId);
        SafePaths.RejectReparsePathChain(source, description);
        SafePaths.RejectReparsePathChain(tombstone, $"interrupted {description}");
        Directory.Move(source, tombstone);
    }

    private static void ValidateMarker(string path, Guid installationId)
    {
        OwnerMarker marker = JsonStore.Read<OwnerMarker>(path);
        if (marker.SchemaVersion != ProductConstants.SchemaVersion ||
            marker.InstallationId != installationId ||
            !string.Equals(marker.ProductId, ProductConstants.ProductId,
                StringComparison.OrdinalIgnoreCase))
            throw new InstallerException("UVSR ownership could not be proven. No files were removed.");
    }

    private static string ProgramTombstone(InstallerPaths paths, Guid transactionId) =>
        Path.Combine(paths.LocalApplicationData, "Programs",
            $"UVSR.uninstall-{transactionId:N}");

    private static string StateTombstone(InstallerPaths paths, Guid transactionId) =>
        Path.Combine(paths.LocalApplicationData,
            $"UVSR Installer.uninstall-{transactionId:N}");

    private static void WaitForExactParent(int processId, long expectedStartTimeUtcTicks)
    {
        try
        {
            using Process parent = Process.GetProcessById(processId);
            if (parent.StartTime.ToUniversalTime().Ticks == expectedStartTimeUtcTicks)
                parent.WaitForExit();
        }
        catch (ArgumentException)
        {
            // The exact parent already exited.
        }
    }

    private static void DeleteWithRetries(string path, string parent)
    {
        Exception? last = null;
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            try
            {
                SafePaths.DeleteOwnedTree(path, parent);
                return;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                last = ex;
                Thread.Sleep(1000);
            }
        }
        throw new InstallerException(
            "Windows could not finish removing UVSR files. Reopen UVSR Launcher to resume cleanup.",
            last!);
    }

    private static bool TryDeleteTree(string path, string parent)
    {
        try
        {
            if (Directory.Exists(path))
                SafePaths.DeleteOwnedTree(path, parent);
            return !Directory.Exists(path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InstallerException)
        {
            return false;
        }
    }

    private static void TryDeleteFile(string path)
    {
        try
        {
            if (File.Exists(path))
                File.Delete(path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
        }
    }
}
