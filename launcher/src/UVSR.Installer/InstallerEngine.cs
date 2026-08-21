using System.Runtime.InteropServices;
using System.Runtime.ExceptionServices;

namespace UvsrInstaller;

internal sealed class InstallerEngine : IDisposable
{
    private readonly InstallerPaths _paths;
    private readonly OwnershipManager _ownership;
    private readonly ProcessRunner _runner = new();
    private readonly DownloadManager _downloads = new();
    private readonly ToolchainManager _toolchain;
    private readonly SourceManager _source;
    private readonly PayloadPackager _packager;
    private readonly ShellIntegration _shell;
    private readonly LauncherManager _launcher;

    internal InstallerEngine(InstallerPaths paths)
    {
        _paths = paths;
        _ownership = new OwnershipManager(paths);
        _toolchain = new ToolchainManager(paths, _runner, _downloads);
        _source = new SourceManager(paths, _runner);
        _packager = new PayloadPackager(paths);
        _shell = new ShellIntegration(paths);
        _launcher = new LauncherManager(paths, _runner, _downloads);
    }

    internal static void EnsureSupportedPlatform()
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ||
            RuntimeInformation.OSArchitecture != Architecture.X64 ||
            RuntimeInformation.ProcessArchitecture != Architecture.X64)
        {
            throw new InstallerException("UVSR Launcher currently supports only x64 Windows 11 computers.");
        }
        (int build, bool workstation) = NativeMethods.GetWindowsVersion();
        if (!workstation || build < 22000)
            throw new InstallerException("UVSR Launcher currently supports only Windows 11 client editions on x64 computers.");
    }

    internal InstallSnapshot Inspect()
    {
        OwnerMarker? marker = _ownership.Inspect();
        if (marker is null)
            return new InstallSnapshot(false, false, null, null, null,
                "UVSR is not installed for this Windows user.");
        if (!File.Exists(_paths.StateFile))
            return new InstallSnapshot(true, false, marker.InstallationId, null, null,
                "UVSR setup is ready to continue. No active version is installed.");
        try
        {
            InstallState state = JsonStore.Read<InstallState>(_paths.StateFile);
            state.Validate(marker.InstallationId);
            string packageRoot = _paths.VersionRoot(state.ActiveVersionId);
            PackageManifest manifest = JsonStore.Read<PackageManifest>(
                Path.Combine(packageRoot, ".uvsr-package.json"),
                ProductConstants.MaximumPackageManifestBytes);
            if (manifest.InstallationId != marker.InstallationId ||
                manifest.VersionId != state.ActiveVersionId || manifest.Commit != state.Commit ||
                manifest.ExecutableSha256 != state.ExecutableSha256)
                throw new InstallerException("The active UVSR package record does not match the installed state.");
            PayloadPackager.ValidatePackage(packageRoot, manifest);
            string executable = _paths.VersionExecutable(state.ActiveVersionId);
            return new InstallSnapshot(true, true, marker.InstallationId, state, executable,
                "UVSR is installed and ready to launch.");
        }
        catch (InstallerException)
        {
            return new InstallSnapshot(true, true, marker.InstallationId, null, null,
                "UVSR needs to be reinstalled before it can launch.", IsDamaged: true);
        }
    }

    internal ExactProcessInspection InspectRendererProcesses() =>
        ProcessInspector.InspectManagedUvsrProcesses(_paths.VersionsDirectory);

    internal async Task EnsureLauncherReadyAsync(
        bool desktopShortcut,
        IProgress<InstallerProgress>? progress,
        Action<string>? logObserver,
        CancellationToken cancellationToken)
    {
        EnsureSupportedPlatform();
        using OperationLock operationLock = OperationLock.Acquire();
        OwnerMarker? existing = _ownership.Inspect();
        if (existing is null)
            return;
        OwnerMarker marker = _ownership.EnsureRoots();
        InstallLog log = new(_paths.LogsDirectory, logObserver);
        InstallSnapshot snapshot = Inspect();
        InstallState? renderer = snapshot.IsDamaged ? null : snapshot.State;
        _launcher.Recover(marker.InstallationId, renderer, _shell, log);
        EnsureCurrentLauncher(marker, renderer, desktopShortcut, log);
        progress?.Report(new InstallerProgress("Ready",
            "UVSR Launcher is ready.", 100));
        await Task.CompletedTask;
    }

    internal async Task<UpdateCheckResult> CheckForUpdatesAsync(
        bool desktopShortcut,
        IProgress<InstallerProgress>? progress,
        Action<string>? logObserver,
        CancellationToken cancellationToken)
    {
        EnsureSupportedPlatform();
        using OperationLock operationLock = OperationLock.Acquire();
        OwnerMarker marker = _ownership.EnsureRoots();
        InstallLog log = new(_paths.LogsDirectory, logObserver);
        InstallSnapshot snapshot = Inspect();
        InstallState? renderer = snapshot.IsDamaged ? null : snapshot.State;
        _launcher.Recover(marker.InstallationId, renderer, _shell, log);
        try
        {
            EnsureCurrentLauncher(marker, renderer, desktopShortcut, log);
        }
        catch (InstallerException ex)
        {
            // A newer but damaged launcher must still be able to reach the
            // signed update feed and repair itself. Renderer and launcher
            // checks remain independent below.
            log.Write($"Deferred local launcher repair until update selection: {ex.Message}");
        }
        snapshot = Inspect();
        return await _launcher.CheckForUpdatesAsync(marker, snapshot,
            progress, log, cancellationToken);
    }

    internal async Task<OperationResult> UpdateLauncherAsync(
        LauncherFeed feed,
        bool desktopShortcut,
        bool continueUvsrUpdate,
        IProgress<InstallerProgress>? progress,
        Action<string>? logObserver,
        CancellationToken cancellationToken)
    {
        EnsureSupportedPlatform();
        using OperationLock operationLock = OperationLock.Acquire();
        OwnerMarker marker = _ownership.EnsureRoots();
        InstallLog log = new(_paths.LogsDirectory, logObserver);
        InstallSnapshot snapshot = Inspect();
        InstallState? renderer = snapshot.IsDamaged ? null : snapshot.State;
        _launcher.Recover(marker.InstallationId, renderer, _shell, log);
        LauncherState candidate = await _launcher.DownloadAndStageAsync(
            marker.InstallationId, feed, desktopShortcut, progress, log,
            cancellationToken);
        Guid continuationId = _launcher.Activate(marker.InstallationId, candidate,
            renderer, _shell, continueUvsrUpdate, log,
            allowMalformedStateReplacement: true);
        string path = _launcher.ActiveExecutable(candidate);
        progress?.Report(new InstallerProgress("UVSR Launcher updated",
            $"UVSR Launcher {candidate.Version} is ready.", 100));
        return new OperationResult(
            $"UVSR Launcher {candidate.Version} was updated successfully.",
            snapshot.State, snapshot.ExecutablePath,
            RelaunchLauncherPath: path,
            ContinueUvsrUpdate: continueUvsrUpdate,
            LauncherContinuationId: continueUvsrUpdate ? continuationId : null);
    }

    internal bool GetDesktopShortcutPreference(InstallSnapshot? snapshot = null)
    {
        snapshot ??= Inspect();
        if (snapshot.InstallationId is not Guid installationId)
            return snapshot.State?.DesktopShortcut ?? true;
        try
        {
            LauncherActivationInspection launcher = _launcher.InspectActivation(
                installationId, snapshot.State?.DesktopShortcut ?? true);
            return launcher.ValidState?.DesktopShortcut ??
                   launcher.RecordedState?.DesktopShortcut ??
                   snapshot.State?.DesktopShortcut ?? true;
        }
        catch (Exception ex) when (ex is InstallerException or IOException or UnauthorizedAccessException)
        {
            return snapshot.State?.DesktopShortcut ?? true;
        }
    }

    internal Guid? FindPendingLauncherContinuation(Guid? requested)
    {
        using OperationLock operationLock = OperationLock.Acquire();
        OwnerMarker? marker = _ownership.Inspect();
        return marker is null
            ? null
            : _launcher.FindPendingContinuation(marker.InstallationId, requested);
    }

    internal void CompleteLauncherContinuation(Guid transactionId)
    {
        using OperationLock operationLock = OperationLock.Acquire();
        OwnerMarker marker = _ownership.Inspect() ?? throw new InstallerException(
            "The pending UVSR update no longer has an owned launcher installation.");
        InstallLog log = new(_paths.LogsDirectory, observer: null);
        _launcher.CompleteContinuation(marker.InstallationId, transactionId, log);
    }

    internal async Task<OperationResult> ExecuteAsync(
        InstallerOperation operation,
        bool desktopShortcut,
        Func<PromptRequest, Task<bool>> prompt,
        IProgress<InstallerProgress>? progress,
        Action<string>? logObserver,
        CancellationToken cancellationToken)
    {
        EnsureSupportedPlatform();
        using OperationLock operationLock = OperationLock.Acquire();
        OwnerMarker marker = operation == InstallerOperation.Uninstall
            ? _ownership.Inspect() ?? throw new InstallerException("UVSR is not installed for this Windows user.")
            : _ownership.EnsureRoots();
        InstallLog log = new(_paths.LogsDirectory, logObserver);
        log.Write($"Starting {operation}; installation ID {marker.InstallationId:D}.");
        try
        {
            if (operation == InstallerOperation.Uninstall)
            {
                CleanupInterruptedRegistryStagingForUninstall(marker, log);
                return ExecuteUninstall(marker, progress, log);
            }

            InstallSnapshot launcherSnapshot = Inspect();
            InstallState? launcherRenderer = launcherSnapshot.IsDamaged
                ? null
                : launcherSnapshot.State;
            _launcher.Recover(marker.InstallationId, launcherRenderer, _shell, log);
            LauncherState launcherState = EnsureCurrentLauncher(marker,
                launcherRenderer, desktopShortcut, log);
            await RecoverIncompleteTransactionAsync(marker, launcherState, log);
            bool originalStateExisted = File.Exists(_paths.StateFile);
            byte[]? originalStateRecord = ReadStateBytesForRollback();
            InstallState? previousState = operation == InstallerOperation.Reinstall
                ? TryReadValidState(marker.InstallationId)
                : ReadStateIfPresent(marker.InstallationId);
            ValidateOperation(operation, previousState, originalStateExisted);
            EnsureDiskSpace(previousState is null ? 20L : 12L);
            _shell.ValidateCanApply(marker.InstallationId, launcherState,
                desktopShortcut);

            ToolPaths tools = await _toolchain.EnsureAsync(prompt, progress, log, cancellationToken);
            SourceResolution resolution = await _source.ResolveMainAsync(tools, previousState,
                progress, log, cancellationToken);
            if (operation == InstallerOperation.Update && resolution.IsAlreadyInstalled)
            {
                InstallState updated = previousState! with { DesktopShortcut = desktopShortcut };
                Guid shellTransactionId = Guid.NewGuid();
                TransactionRecord shellTransaction = new(ProductConstants.SchemaVersion,
                    marker.InstallationId, shellTransactionId, operation, "shell-update",
                    previousState.ActiveVersionId, previousState, DateTimeOffset.UtcNow);
                JsonStore.WriteAtomic(_paths.TransactionFile, shellTransaction);
                try
                {
                    JsonStore.WriteAtomic(_paths.StateFile, updated);
                    _shell.Apply(marker.InstallationId, updated, launcherState, log,
                        shellTransactionId);
                }
                catch (Exception operationFailure)
                {
                    try
                    {
                        JsonStore.WriteAtomic(_paths.StateFile, previousState);
                        _shell.Apply(marker.InstallationId, previousState,
                            launcherState, log,
                            shellTransactionId);
                    }
                    catch (Exception rollbackFailure)
                    {
                        throw new InstallerException(
                            "The shortcut update failed and Windows could not restore it completely. Reopen UVSR Launcher to finish recovery.",
                            new AggregateException(operationFailure, rollbackFailure));
                    }
                    TryPostCommitCleanup(() => File.Delete(_paths.TransactionFile), log);
                    ExceptionDispatchInfo.Capture(operationFailure).Throw();
                    throw new InvalidOperationException("Unreachable shortcut rollback path.");
                }
                bool shellJournalCleared = TryPostCommitCleanup(
                    () => File.Delete(_paths.TransactionFile), log);
                if (shellJournalCleared)
                    TryPostCommitCleanup(() => SweepOrphanedVersions(updated, log), log);
                return new OperationResult(
                    "UVSR is already up to date with the newest compatible renderer source.", updated,
                    _paths.VersionExecutable(updated.ActiveVersionId));
            }
            if (resolution.IsNonFastForward)
            {
                string installedPublicCommit =
                    RendererSourceBridgeRegistry.MapSourceToPublicBase(
                        previousState!.Commit);
                bool accepted = await prompt(new PromptRequest(
                    "Public Main History Changed",
                    $"The public main branch is no longer a direct continuation of installed public source {installedPublicCommit[..7]}. " +
                    $"Replace it with current public main {resolution.PublicBaseCommit[..7]}? The installed version remains available unless the replacement builds successfully."));
                if (!accepted)
                    throw new InstallerException("The update was cancelled. The installed UVSR version was preserved.");
            }

            Guid transactionId = Guid.NewGuid();
            TransactionRecord transaction = new(ProductConstants.SchemaVersion,
                marker.InstallationId, transactionId, operation, "source", null,
                previousState, DateTimeOffset.UtcNow);
            JsonStore.WriteAtomic(_paths.TransactionFile, transaction);
            PackageManifest? manifest = null;
            bool activated = false;
            try
            {
                await _source.PrepareExactSourceAsync(tools, resolution,
                    progress, log, cancellationToken);
                transaction = transaction with { Phase = "build" };
                JsonStore.WriteAtomic(_paths.TransactionFile, transaction);
                await _source.BuildAsync(tools, resolution, progress, log,
                    cancellationToken);

                string versionId = CreateVersionId(resolution.Commit);
                transaction = transaction with { Phase = "package", CandidateVersionId = versionId };
                JsonStore.WriteAtomic(_paths.TransactionFile, transaction);
                progress?.Report(new InstallerProgress("Packaging UVSR",
                    "Copying only the renderer's required runtime files."));
                await _source.ValidatePreparedSourceAsync(tools, resolution, log,
                    cancellationToken);
                manifest = _packager.Stage(marker.InstallationId, transactionId,
                    versionId, resolution.Commit, log);
                _packager.Activate(transactionId, manifest);

                InstallState newState = new(ProductConstants.SchemaVersion,
                    marker.InstallationId, versionId, resolution.Commit,
                    manifest.ExecutableSha256, desktopShortcut, DateTimeOffset.UtcNow);
                transaction = transaction with { Phase = "activate" };
                JsonStore.WriteAtomic(_paths.TransactionFile, transaction);
                JsonStore.WriteAtomic(_paths.StateFile, newState);
                activated = true;
                transaction = transaction with { Phase = "shell-activation" };
                JsonStore.WriteAtomic(_paths.TransactionFile, transaction);
                _shell.Apply(marker.InstallationId, newState, launcherState,
                    log, transactionId);

                // State plus shell activation is the commit point. Everything after
                // it is best-effort pruning and must never roll back to a package
                // that cleanup may already have started removing.
                bool journalCleared = TryPostCommitCleanup(
                    () => File.Delete(_paths.TransactionFile), log);
                TryPostCommitCleanup(() => CleanupTransactionStaging(transactionId, log), log);
                if (journalCleared)
                {
                    TryPostCommitCleanup(() => CleanupPreviousVersion(previousState, newState, log), log);
                    TryPostCommitCleanup(() => SweepOrphanedVersions(newState, log), log);
                }
                TryPostCommitCleanup(() => progress?.Report(new InstallerProgress("UVSR is ready",
                    "The newest selected UVSR version is installed.", 100)), log);
                string verb = operation == InstallerOperation.Install ? "installed" :
                    operation == InstallerOperation.Reinstall ? "reinstalled" : "updated";
                return new OperationResult(
                    $"UVSR was {verb} successfully.", newState,
                    _paths.VersionExecutable(newState.ActiveVersionId));
            }
            catch (Exception operationFailure)
            {
                TryLog(log, "The operation failed; rolling back any active-state change.");
                List<Exception> rollbackFailures = new();
                bool activationRestored = !activated;
                if (activated)
                {
                    try
                    {
                        if (previousState is null)
                        {
                            if (originalStateRecord is not null)
                                JsonStore.WriteAtomicBytes(_paths.StateFile, originalStateRecord);
                            else if (File.Exists(_paths.StateFile))
                                File.Delete(_paths.StateFile);
                        }
                        else
                        {
                            JsonStore.WriteAtomic(_paths.StateFile, previousState);
                        }
                        // ShellIntegration either rejected before mutation or
                        // restored its own byte-for-byte snapshot. A dedicated
                        // exception is the only signal that shell rollback failed.
                        activationRestored = operationFailure is not ShellRollbackException;
                    }
                    catch (Exception rollbackFailure)
                    {
                        rollbackFailures.Add(rollbackFailure);
                    }
                }
                bool cleanupComplete = activationRestored;
                if (activationRestored && manifest is not null)
                {
                    try
                    {
                        string candidate = _paths.VersionRoot(manifest.VersionId);
                        if (Directory.Exists(candidate) &&
                            ProcessInspector.IsConfirmedNotRunning(
                                ProcessInspector.InspectManagedUvsrProcesses(candidate)))
                            SafePaths.DeleteOwnedTree(candidate, _paths.VersionsDirectory);
                    }
                    catch (Exception cleanupFailure)
                    {
                        cleanupComplete = false;
                        rollbackFailures.Add(cleanupFailure);
                    }
                }
                if (activationRestored)
                {
                    try { CleanupTransactionStaging(transactionId, log); }
                    catch (Exception cleanupFailure)
                    {
                        cleanupComplete = false;
                        rollbackFailures.Add(cleanupFailure);
                    }
                }
                if (cleanupComplete)
                {
                    try
                    {
                        if (File.Exists(_paths.TransactionFile))
                            File.Delete(_paths.TransactionFile);
                    }
                    catch (Exception cleanupFailure)
                    {
                        rollbackFailures.Add(cleanupFailure);
                    }
                }
                if (rollbackFailures.Count > 0)
                {
                    rollbackFailures.Insert(0, operationFailure);
                    throw new InstallerException(
                        "The operation failed and cleanup could not finish completely. The recovery record was preserved; reopen UVSR Launcher to resume safely.",
                        new AggregateException(rollbackFailures));
                }
                ExceptionDispatchInfo.Capture(operationFailure).Throw();
                throw new InvalidOperationException("Unreachable operation rollback path.");
            }
        }
        catch (OperationCanceledException ex)
        {
            log.Write("The operation was cancelled; the previous installed version remains active.");
            throw new InstallerException("The operation was cancelled. The previous installed UVSR version was preserved.", ex);
        }
        catch (Exception ex)
        {
            log.Write($"Failure: {InstallLog.DescribeException(ex)}");
            if (ex is InstallerException)
                throw;
            throw new InstallerException(
                $"UVSR Launcher stopped safely. Details were written to {log.Path}", ex);
        }
    }

    internal void LaunchInstalled()
    {
        using OperationLock operationLock = OperationLock.Acquire();
        InstallSnapshot snapshot = Inspect();
        if (!snapshot.IsInstalled || snapshot.State is null)
            throw new InstallerException("UVSR is not installed for this Windows user.");
        _shell.Launch(snapshot.State);
    }

    private OperationResult ExecuteUninstall(
        OwnerMarker marker,
        IProgress<InstallerProgress>? progress,
        InstallLog log)
    {
        _ownership.ValidateBoth(marker.InstallationId);
        ExactProcessInspection running =
            ProcessInspector.InspectManagedUvsrProcesses(_paths.ProgramRoot);
        if (!ProcessInspector.IsConfirmedNotRunning(running))
            throw new InstallerException("UVSR is currently running. Close its window, then choose Uninstall again.");
        ExactProcessInspection launchers = ProcessInspector.InspectManagedLauncherProcesses(
            _paths.ProgramRoot, excludeCurrentProcess: true);
        if (!ProcessInspector.IsConfirmedNotRunning(launchers))
            throw new InstallerException(
                "Another UVSR Launcher window is open. Close it, then choose Uninstall again.");
        progress?.Report(new InstallerProgress("Removing UVSR",
            "Preparing a safe cleanup after this launcher window closes."));
        Guid transactionId = Guid.NewGuid();
        TransactionRecord transaction = new(ProductConstants.SchemaVersion,
            marker.InstallationId, transactionId, InstallerOperation.Uninstall,
            "uninstall-pending", null, TryReadValidState(marker.InstallationId),
            DateTimeOffset.UtcNow);
        JsonStore.WriteAtomic(_paths.TransactionFile, transaction);
        try
        {
            SelfCleanup.Schedule(_paths, marker.InstallationId, transactionId,
                transaction.PreviousState, log);
        }
        catch
        {
            if (File.Exists(_paths.TransactionFile))
                File.Delete(_paths.TransactionFile);
            throw;
        }
        return new OperationResult(
            "UVSR uninstall is ready. Its shortcuts and owned program files will be removed when this window closes. " +
            "Renderer settings and shared Microsoft prerequisites are preserved.", null, null, true);
    }

    private void CleanupInterruptedRegistryStagingForUninstall(
        OwnerMarker marker,
        InstallLog log)
    {
        if (!File.Exists(_paths.TransactionFile))
            return;
        try
        {
            TransactionRecord interrupted = JsonStore.Read<TransactionRecord>(
                _paths.TransactionFile);
            if (interrupted.SchemaVersion != ProductConstants.SchemaVersion ||
                interrupted.InstallationId != marker.InstallationId ||
                interrupted.TransactionId == Guid.Empty)
                throw new InstallerException(
                    "The interrupted UVSR Launcher transaction does not prove registry ownership.");
            ShellIntegration.RemoveStagedRegistryEntry(interrupted.TransactionId);
            log.Write("Removed the prior transaction's Apps & Features staging entry before uninstall.");
        }
        catch (InstallerException ex)
        {
            // Uninstall remains the recovery path for damaged state. Preserve any
            // registry key that cannot be tied to this installation rather than
            // blocking removal or guessing at ownership.
            log.Write($"Preserved unverified registry staging data during uninstall: {ex.Message}");
        }
    }

    private async Task RecoverIncompleteTransactionAsync(
        OwnerMarker marker,
        LauncherState launcherState,
        InstallLog log)
    {
        await Task.Yield();
        if (!File.Exists(_paths.TransactionFile))
            return;
        TransactionRecord transaction = JsonStore.Read<TransactionRecord>(_paths.TransactionFile);
        if (transaction.SchemaVersion != ProductConstants.SchemaVersion ||
            transaction.InstallationId != marker.InstallationId ||
            transaction.TransactionId == Guid.Empty ||
            string.IsNullOrWhiteSpace(transaction.Phase) ||
            !Enum.IsDefined(transaction.Operation) ||
            transaction.Phase is not ("source" or "build" or "package" or
                "activate" or "shell-activation" or "shell-update" or
                "uninstall-pending") ||
            (transaction.CandidateVersionId is not null &&
             !ProductConstants.VersionIdRegex().IsMatch(transaction.CandidateVersionId)))
            throw new InstallerException("An incomplete UVSR Launcher transaction has an invalid ownership record.");
        if (transaction.Operation == InstallerOperation.Uninstall)
        {
            if (File.Exists(_paths.UninstallRecordFile))
                throw new InstallerException(
                    "A previous uninstall is waiting for its cleanup helper to finish. Close the earlier launcher window, then reopen UVSR Launcher.");
            File.Delete(_paths.TransactionFile);
            return;
        }

        InstallState? current = TryReadValidState(marker.InstallationId);
        bool preActivation = transaction.Phase is "source" or "build" or "package" ||
            (transaction.Phase == "activate" &&
             current?.ActiveVersionId != transaction.CandidateVersionId);
        if (preActivation)
        {
            if (transaction.CandidateVersionId is not null)
            {
                string inactiveCandidate = _paths.VersionRoot(transaction.CandidateVersionId);
                if (Directory.Exists(inactiveCandidate) &&
                    ProcessInspector.IsConfirmedNotRunning(
                        ProcessInspector.InspectManagedUvsrProcesses(inactiveCandidate)))
                    SafePaths.DeleteOwnedTree(inactiveCandidate, _paths.VersionsDirectory);
            }
            CleanupTransactionStaging(transaction.TransactionId, log);
            File.Delete(_paths.TransactionFile);
            log.Write("Discarded an interrupted pre-activation build so repair can retry cleanly.");
            return;
        }
        if (transaction.Phase == "shell-update")
        {
            try
            {
                if (current is null)
                    throw new InstallerException("The interrupted shortcut update has no valid active state.");
                ValidateInstalledPackage(marker.InstallationId, current);
                _shell.Apply(marker.InstallationId, current, launcherState, log,
                    transaction.TransactionId);
            }
            catch
            {
                current = RestorePreviousActivation(marker.InstallationId,
                    transaction.PreviousState, launcherState,
                    transaction.TransactionId, log);
            }
            File.Delete(_paths.TransactionFile);
            if (current is not null)
                TryPostCommitCleanup(() => SweepOrphanedVersions(current, log), log);
            log.Write("Recovered safely from an interrupted shortcut update.");
            return;
        }

        bool candidateRemainsActive = false;
        if (transaction.CandidateVersionId is not null)
        {
            string candidate = _paths.VersionRoot(transaction.CandidateVersionId);
            if (current?.ActiveVersionId == transaction.CandidateVersionId)
            {
                try
                {
                    ValidateInstalledPackage(marker.InstallationId, current!);
                    _shell.Apply(marker.InstallationId, current!, launcherState, log,
                        transaction.TransactionId);
                    candidateRemainsActive = true;
                }
                catch
                {
                    current = RestorePreviousActivation(marker.InstallationId,
                        transaction.PreviousState, launcherState,
                        transaction.TransactionId, log);
                }
            }
            else
            {
                current = RestorePreviousActivation(marker.InstallationId,
                    transaction.PreviousState, launcherState,
                    transaction.TransactionId, log);
            }
            if (!candidateRemainsActive && Directory.Exists(candidate) &&
                ProcessInspector.IsConfirmedNotRunning(
                    ProcessInspector.InspectManagedUvsrProcesses(candidate)))
                SafePaths.DeleteOwnedTree(candidate, _paths.VersionsDirectory);
        }
        else
        {
            current = RestorePreviousActivation(marker.InstallationId,
                transaction.PreviousState, launcherState,
                transaction.TransactionId, log);
        }
        CleanupTransactionStaging(transaction.TransactionId, log);
        File.Delete(_paths.TransactionFile);
        if (current is not null)
            TryPostCommitCleanup(() => SweepOrphanedVersions(current, log), log);
        log.Write("Recovered safely from an incomplete earlier UVSR Launcher transaction.");
    }

    private InstallState? RestorePreviousActivation(
        Guid installationId,
        InstallState? previous,
        LauncherState launcherState,
        Guid transactionId,
        InstallLog log)
    {
        if (previous is null)
        {
            if (File.Exists(_paths.StateFile))
                File.Delete(_paths.StateFile);
            _shell.Apply(installationId, null, launcherState, log, transactionId);
            return null;
        }
        previous.Validate(installationId);
        JsonStore.WriteAtomic(_paths.StateFile, previous);
        try
        {
            ValidateInstalledPackage(installationId, previous);
            _shell.Apply(installationId, previous, launcherState, log,
                transactionId);
        }
        catch (InstallerException ex)
        {
            _shell.RemoveTransactionArtifacts(installationId, transactionId, log);
            log.Write($"The previous UVSR package is damaged; shell entries were cleared so Install can repair it: {ex.Message}");
        }
        return previous;
    }

    private InstallState? ReadStateIfPresent(Guid installationId)
    {
        if (!File.Exists(_paths.StateFile))
            return null;
        InstallState state = JsonStore.Read<InstallState>(_paths.StateFile);
        state.Validate(installationId);
        return state;
    }

    private static void ValidateOperation(
        InstallerOperation operation,
        InstallState? state,
        bool stateRecordExists)
    {
        if (operation == InstallerOperation.Install && stateRecordExists)
            throw new InstallerException("UVSR is already installed. Choose Install again and select Reinstall.");
        if (operation == InstallerOperation.Update && state is null)
            throw new InstallerException("UVSR is not installed yet. Choose Install.");
    }

    private LauncherState EnsureCurrentLauncher(
        OwnerMarker marker,
        InstallState? rendererState,
        bool desktopShortcut,
        InstallLog log)
    {
        LauncherActivationInspection inspection = _launcher.InspectActivation(
            marker.InstallationId, desktopShortcut, log);
        if (inspection.StateRecordUnverifiable)
            throw new InstallerException(
                "The installed launcher record is temporarily unavailable. No launcher files were changed; close other launcher copies or security scans, then try again.");
        LauncherState? existing = inspection.ValidState;
        if (inspection.Problem is not null)
            log.Write($"The installed launcher state needs repair: {inspection.Problem}");
        if (existing is not null &&
            existing.ReleaseSequence >= ProductConstants.LauncherReleaseSequence)
        {
            bool pointerMatches = inspection.RecordedState is not null &&
                inspection.RecordedState.ExecutableSha256 == existing.ExecutableSha256;
            if (existing.DesktopShortcut == desktopShortcut && pointerMatches)
            {
                _launcher.SweepInactive(existing, log);
                return existing;
            }
            LauncherState preference = existing with { DesktopShortcut = desktopShortcut };
            _launcher.Activate(marker.InstallationId, preference, rendererState,
                _shell, continueUvsrUpdate: false, log);
            return preference;
        }

        if (existing is null &&
            inspection.HighestDefensibleSequence > ProductConstants.LauncherReleaseSequence)
            throw new InstallerException(
                "A newer installed UVSR Launcher needs repair. Download the newest UVSR Launcher before changing UVSR.");

        LauncherState candidate = _launcher.StageCurrentLauncher(
            marker.InstallationId, desktopShortcut, log);
        if (existing is not null && existing.ReleaseSequence == candidate.ReleaseSequence &&
            existing.Version == candidate.Version &&
            existing.ExecutableSha256 == candidate.ExecutableSha256 &&
            existing.DesktopShortcut == desktopShortcut &&
            inspection.RecordedState?.ExecutableSha256 == existing.ExecutableSha256)
        {
            _launcher.SweepInactive(existing, log);
            return existing;
        }
        _launcher.Activate(marker.InstallationId, candidate, rendererState,
            _shell, continueUvsrUpdate: false, log,
            allowMalformedStateReplacement: existing is not null);
        return candidate;
    }

    private void CleanupTransactionStaging(Guid transactionId, InstallLog log)
    {
        string path = Path.Combine(_paths.StagingDirectory, transactionId.ToString("N"));
        if (!Directory.Exists(path))
            return;
        SafePaths.DeleteOwnedTree(path, _paths.StagingDirectory);
        log.Write("Removed installer-owned transaction staging files.");
    }

    private void CleanupPreviousVersion(InstallState? previous, InstallState current, InstallLog log)
    {
        if (previous is null || previous.ActiveVersionId == current.ActiveVersionId)
            return;
        string prior = _paths.VersionRoot(previous.ActiveVersionId);
        if (!Directory.Exists(prior))
            return;
        if (!ProcessInspector.IsConfirmedNotRunning(
                ProcessInspector.InspectManagedUvsrProcesses(prior)))
        {
            log.Write("The previous UVSR version is still running; its package will be retained for later cleanup.");
            return;
        }
        SafePaths.DeleteOwnedTree(prior, _paths.VersionsDirectory);
        log.Write($"Removed superseded UVSR package {previous.ActiveVersionId}.");
    }

    private void SweepOrphanedVersions(InstallState active, InstallLog log)
    {
        if (!Directory.Exists(_paths.VersionsDirectory))
            return;
        SafePaths.RejectReparsePathChain(_paths.VersionsDirectory,
            "UVSR managed versions directory");
        foreach (string directory in Directory.EnumerateDirectories(_paths.VersionsDirectory))
        {
            string versionId = Path.GetFileName(directory);
            if (versionId == active.ActiveVersionId ||
                !ProductConstants.VersionIdRegex().IsMatch(versionId) ||
                !ProcessInspector.IsConfirmedNotRunning(
                    ProcessInspector.InspectManagedUvsrProcesses(directory)))
                continue;
            SafePaths.DeleteOwnedTree(directory, _paths.VersionsDirectory);
            log.Write($"Removed superseded UVSR package {versionId}.");
        }
    }

    private InstallState? TryReadValidState(Guid installationId)
    {
        try { return ReadStateIfPresent(installationId); }
        catch (InstallerException) { return null; }
    }

    private byte[]? ReadStateBytesForRollback()
    {
        if (!File.Exists(_paths.StateFile))
            return null;
        FileInfo info = new(_paths.StateFile);
        return info.Length is > 0 and <= ProductConstants.MaximumStateBytes
            ? File.ReadAllBytes(_paths.StateFile)
            : null;
    }

    private void ValidateInstalledPackage(Guid installationId, InstallState state)
    {
        string packageRoot = _paths.VersionRoot(state.ActiveVersionId);
        PackageManifest manifest = JsonStore.Read<PackageManifest>(
            Path.Combine(packageRoot, ".uvsr-package.json"),
            ProductConstants.MaximumPackageManifestBytes);
        if (manifest.InstallationId != installationId ||
            manifest.VersionId != state.ActiveVersionId || manifest.Commit != state.Commit ||
            manifest.ExecutableSha256 != state.ExecutableSha256)
            throw new InstallerException("The UVSR package does not match its UVSR Launcher record.");
        PayloadPackager.ValidatePackage(packageRoot, manifest);
    }

    private static bool TryPostCommitCleanup(Action cleanup, InstallLog log)
    {
        try
        {
            cleanup();
            return true;
        }
        catch (Exception ex)
        {
            TryLog(log, $"Post-commit cleanup was deferred safely: {ex.Message}");
            return false;
        }
    }

    private static void TryLog(InstallLog log, string message)
    {
        try { log.Write(message); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
    }

    private void EnsureDiskSpace(long requiredGigabytes)
    {
        string root = Path.GetPathRoot(_paths.LocalApplicationData)
            ?? throw new InstallerException("Windows could not identify the installation drive.");
        DriveInfo drive = new(root);
        long required = requiredGigabytes * 1024L * 1024 * 1024;
        if (drive.AvailableFreeSpace < required)
            throw new InstallerException(
                $"UVSR needs at least {requiredGigabytes} GB free on {drive.Name} to download source, build safely, and preserve the current version during activation.");
    }

    private static string CreateVersionId(string commit) =>
        $"{commit}-{DateTime.UtcNow:yyyyMMddHHmmss}-{Guid.NewGuid():N}"[..64];

    public void Dispose() => _downloads.Dispose();
}
