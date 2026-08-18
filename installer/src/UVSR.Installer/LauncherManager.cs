using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;

namespace UvsrInstaller;

internal sealed class LauncherManager
{
    private readonly InstallerPaths _paths;
    private readonly ProcessRunner _runner;
    private readonly DownloadManager _downloads;

    internal LauncherManager(
        InstallerPaths paths,
        ProcessRunner runner,
        DownloadManager downloads)
    {
        _paths = paths;
        _runner = runner;
        _downloads = downloads;
    }

    internal LauncherState? TryReadState(Guid installationId)
    {
        if (!File.Exists(_paths.LauncherStateFile))
            return null;
        LauncherState state = JsonStore.Read<LauncherState>(_paths.LauncherStateFile);
        state.Validate(installationId);
        ValidatePackage(state);
        return state;
    }

    internal LauncherState ReadState(Guid installationId) =>
        TryReadState(installationId) ?? throw new InstallerException(
            "UVSR Launcher is not installed for this Windows user.");

    internal LauncherActivationInspection InspectActivation(
        Guid installationId,
        bool defaultDesktopShortcut,
        InstallLog? log = null)
    {
        bool stateFileExists = File.Exists(_paths.LauncherStateFile);
        bool malformed = false;
        LauncherState? recorded = null;
        LauncherState? valid = null;
        List<LauncherReleaseIdentity> defensibleIdentities = new();
        string? problem = null;
        if (stateFileExists)
        {
            try
            {
                recorded = JsonStore.Read<LauncherState>(_paths.LauncherStateFile);
                recorded.Validate(installationId);
                defensibleIdentities.Add(LauncherReleaseIdentity.From(recorded));
                try
                {
                    ValidatePackage(recorded);
                    valid = recorded;
                }
                catch (Exception ex) when (ex is InstallerException or IOException or
                                           UnauthorizedAccessException)
                {
                    problem = ex.Message;
                }
            }
            catch (Exception ex) when (ex is InstallerException or IOException or
                                       UnauthorizedAccessException)
            {
                malformed = true;
                problem = ex.Message;
            }
        }

        try
        {
            if (Directory.Exists(_paths.LauncherVersionsDirectory))
            {
                foreach (string directory in Directory.EnumerateDirectories(
                             _paths.LauncherVersionsDirectory))
                {
                    string hash = Path.GetFileName(directory);
                    if (!ProductConstants.HashRegex().IsMatch(hash))
                        continue;
                    try
                    {
                        LauncherPackageManifest marker =
                            JsonStore.Read<LauncherPackageManifest>(
                                _paths.LauncherPackageMarker(hash));
                        ValidatePackageDirectoryBinding(hash, marker);
                        LauncherState candidate = new(marker.SchemaVersion,
                            marker.ProductId, marker.InstallationId,
                            marker.ReleaseSequence, marker.Version,
                            marker.ExecutableSha256,
                            recorded?.DesktopShortcut ?? defaultDesktopShortcut,
                            marker.InstalledUtc);
                        candidate.Validate(installationId);
                        ValidatePackage(candidate);
                        defensibleIdentities.Add(LauncherReleaseIdentity.From(candidate));
                        if (valid is null ||
                            candidate.ReleaseSequence > valid.ReleaseSequence ||
                            (candidate.ReleaseSequence == valid.ReleaseSequence &&
                             candidate.InstalledUtc > valid.InstalledUtc))
                            valid = candidate;
                    }
                    catch (Exception ex) when (ex is InstallerException or IOException or
                                               UnauthorizedAccessException)
                    {
                        log?.Write($"Preserved an unverified launcher recovery package: {ex.Message}");
                    }
                }
            }
        }
        catch (Exception ex) when (ex is InstallerException or IOException or UnauthorizedAccessException)
        {
            problem ??= ex.Message;
            log?.Write($"Launcher recovery-package scan was deferred: {ex.Message}");
        }

        LauncherReleaseIdentity? highestIdentity =
            ResolveHighestDefensibleIdentity(defensibleIdentities);
        return new LauncherActivationInspection(stateFileExists, malformed,
            recorded, valid, highestIdentity?.ReleaseSequence ?? 0, problem);
    }

    internal async Task<UpdateCheckResult> CheckForUpdatesAsync(
        OwnerMarker marker,
        InstallSnapshot snapshot,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        ComponentUpdateStatus launcher;
        progress?.Report(new InstallerProgress("Checking UVSR Launcher",
            "Looking for a newer launcher release."));
        try
        {
            LauncherFeed feed = await DownloadLauncherFeedAsync(progress, log,
                cancellationToken);
            LauncherActivationInspection inspection = InspectActivation(
                marker.InstallationId, defaultDesktopShortcut: true, log);
            LauncherReleaseIdentity? defensibleIdentity =
                HighestDefensibleIdentity(inspection);
            long currentSequence = Math.Max(
                inspection.HighestDefensibleSequence,
                ProductConstants.LauncherReleaseSequence);
            LauncherReleaseIdentity currentIdentity = defensibleIdentity is not null &&
                                                       defensibleIdentity.Value.ReleaseSequence == currentSequence
                ? defensibleIdentity.Value
                : CurrentProcessIdentity();
            LauncherReleaseIdentity feedIdentity = LauncherReleaseIdentity.From(feed);
            bool recordedStateHealthy =
                IsRecordedStateHealthyAtIdentity(inspection.RecordedState,
                    currentIdentity);
            ComponentUpdateState launcherState = ClassifyLauncherUpdate(
                currentIdentity, recordedStateHealthy, feedIdentity);
            if (launcherState == ComponentUpdateState.Current)
            {
                launcher = new ComponentUpdateStatus(UpdateComponent.Launcher,
                    ComponentUpdateState.Current,
                    currentIdentity.Version,
                    feed.Version,
                    feed.ReleaseSequence < currentSequence
                        ? "This UVSR Launcher is newer than the published update feed."
                        : "UVSR Launcher is up to date.", feed);
            }
            else if (launcherState == ComponentUpdateState.RepairNeeded)
            {
                launcher = new ComponentUpdateStatus(UpdateComponent.Launcher,
                    ComponentUpdateState.RepairNeeded,
                    currentIdentity.Version,
                    feed.Version,
                    "UVSR Launcher needs to restore its installed files.", feed);
            }
            else
            {
                launcher = new ComponentUpdateStatus(UpdateComponent.Launcher,
                    ComponentUpdateState.UpdateAvailable,
                    currentIdentity.Version,
                    feed.Version,
                    $"UVSR Launcher {feed.Version} is available.", feed);
            }
        }
        catch (Exception ex) when (ex is InstallerException or IOException or UnauthorizedAccessException)
        {
            log.Write($"Launcher update check failed independently: {ex.Message}");
            launcher = new ComponentUpdateStatus(UpdateComponent.Launcher,
                ComponentUpdateState.CheckFailed,
                TryCurrentLauncherVersion(marker.InstallationId), null,
                "UVSR Launcher could not be checked right now.");
        }

        ComponentUpdateStatus uvsr;
        progress?.Report(new InstallerProgress("Checking UVSR",
            "Looking for a newer renderer version."));
        try
        {
            string commit = await DownloadMainCommitAsync(progress, log,
                cancellationToken);
            ComponentUpdateState state = snapshot.IsDamaged
                ? ComponentUpdateState.RepairNeeded
                : !snapshot.IsInstalled
                    ? ComponentUpdateState.NotInstalled
                    : string.Equals(snapshot.State?.Commit, commit, StringComparison.Ordinal)
                        ? ComponentUpdateState.Current
                        : ComponentUpdateState.UpdateAvailable;
            string detail = state switch
            {
                ComponentUpdateState.RepairNeeded =>
                    "UVSR needs repair; reinstalling will restore its files.",
                ComponentUpdateState.NotInstalled => "UVSR is not installed yet.",
                ComponentUpdateState.Current => "UVSR is up to date.",
                _ => "A newer UVSR version is available."
            };
            uvsr = new ComponentUpdateStatus(UpdateComponent.Uvsr, state,
                snapshot.State?.Commit is string current ? current[..7] : null,
                commit[..7], detail, UvsrCommit: commit);
        }
        catch (Exception ex) when (ex is InstallerException or IOException or UnauthorizedAccessException)
        {
            log.Write($"UVSR update check failed independently: {ex.Message}");
            uvsr = new ComponentUpdateStatus(UpdateComponent.Uvsr,
                ComponentUpdateState.CheckFailed,
                snapshot.State?.Commit is string current ? current[..7] : null,
                null, "UVSR could not be checked right now.");
        }
        return new UpdateCheckResult(uvsr, launcher);
    }

    internal LauncherState StageCurrentLauncher(
        Guid installationId,
        bool desktopShortcut,
        InstallLog log)
    {
        string current = Environment.ProcessPath
            ?? throw new InstallerException("Windows could not locate the running UVSR Launcher.");
        return StageExecutable(current, installationId,
            ProductConstants.LauncherReleaseSequence,
            ProductConstants.LauncherVersion,
            expectedSize: null,
            expectedHash: null,
            desktopShortcut,
            log);
    }

    internal async Task<LauncherState> DownloadAndStageAsync(
        Guid installationId,
        LauncherFeed feed,
        bool desktopShortcut,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        ValidateFeed(feed);
        string downloaded = Path.Combine(_paths.DownloadsDirectory,
            $"launcher-{feed.ReleaseSequence}-{feed.Artifact.Sha256}.exe");
        Uri artifact = BuildArtifactUri(feed);
        progress?.Report(new InstallerProgress("Downloading UVSR Launcher",
            $"Downloading UVSR Launcher {feed.Version}."));
        try
        {
            await _downloads.DownloadAndVerifyAsync(artifact, downloaded,
                feed.Artifact.Sha256, ProductConstants.MaximumLauncherBytes,
                progress, log, cancellationToken,
                NativeMethods.VerifyLauncherPublisherSignature,
                "Downloading UVSR Launcher");
        }
        catch (InstallerException ex) when (IsUnavailableArtifactError(ex))
        {
            log.Write("Versioned UVSR Launcher release tag is unavailable; falling back to " +
                      "uvsr-launcher-latest.");
            Uri fallback = BuildLatestArtifactUri(feed);
            await _downloads.DownloadAndVerifyAsync(fallback, downloaded,
                feed.Artifact.Sha256, ProductConstants.MaximumLauncherBytes,
                progress, log, cancellationToken,
                NativeMethods.VerifyLauncherPublisherSignature,
                "Downloading UVSR Launcher");
        }
        if (new FileInfo(downloaded).Length != feed.Artifact.Size)
            throw new InstallerException("The launcher download did not match its published size.");
        ValidatePeX64(downloaded);
        ValidateFileMetadata(downloaded, feed.Version);
        using CancellationTokenSource healthTimeout =
            CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        healthTimeout.CancelAfter(TimeSpan.FromSeconds(15));
        ProcessResult health;
        try
        {
            health = await _runner.RunAsync(downloaded,
                new[] { "--launcher-health-check", feed.ReleaseSequence.ToString(), feed.Version },
                Path.GetDirectoryName(downloaded), null, log, healthTimeout.Token);
        }
        catch (OperationCanceledException ex) when (!cancellationToken.IsCancellationRequested)
        {
            throw new InstallerException(
                "The downloaded UVSR Launcher did not respond to its safety check.", ex);
        }
        if (health.ExitCode != 0)
            throw new InstallerException("The downloaded UVSR Launcher did not pass its health check.");
        return StageExecutable(downloaded, installationId, feed.ReleaseSequence,
            feed.Version, feed.Artifact.Size, feed.Artifact.Sha256,
            desktopShortcut, log);
    }

    internal Guid Activate(
        Guid installationId,
        LauncherState candidate,
        InstallState? rendererState,
        ShellIntegration shell,
        bool continueUvsrUpdate,
        InstallLog log,
        bool allowMalformedStateReplacement = false)
    {
        candidate.Validate(installationId);
        ValidatePackage(candidate);
        LauncherActivationInspection inspection = InspectActivation(
            installationId, candidate.DesktopShortcut, log);
        LauncherState? previous = inspection.ValidState;
        if (inspection.StateRecordMalformed && previous is null &&
            !allowMalformedStateReplacement)
            throw new InstallerException(
                "The installed launcher record is damaged and cannot be replaced safely by this older copy. Choose Update from the newest UVSR Launcher.");
        if (candidate.ReleaseSequence < inspection.HighestDefensibleSequence)
            throw new InstallerException("UVSR Launcher refused to activate an older release.");
        LauncherReleaseIdentity? highestIdentity = HighestDefensibleIdentity(inspection);
        if (highestIdentity is not null &&
            candidate.ReleaseSequence == highestIdentity.Value.ReleaseSequence)
            RequireSameReleaseIdentity(highestIdentity.Value,
                LauncherReleaseIdentity.From(candidate),
                "UVSR Launcher detected conflicting files for the same release sequence.");
        if (previous is not null && candidate.ReleaseSequence < previous.ReleaseSequence)
            throw new InstallerException("UVSR Launcher refused to activate an older release.");

        if (File.Exists(_paths.LauncherTransactionFile))
        {
            LauncherActivationRecord pending = JsonStore.Read<LauncherActivationRecord>(
                _paths.LauncherTransactionFile);
            pending = PreserveAwaitingContinuation(pending, candidate, installationId);
            candidate = pending.CandidateState;
            JsonStore.WriteAtomic(_paths.LauncherTransactionFile, pending);
            JsonStore.WriteAtomic(_paths.LauncherStateFile, candidate);
            shell.Apply(installationId, rendererState, candidate, log,
                pending.TransactionId);
            log.Write("Preserved the pending UVSR update while updating launcher preferences.");
            return pending.TransactionId;
        }

        Guid transactionId = Guid.NewGuid();
        LauncherActivationRecord transaction = new(
            ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId,
            installationId,
            transactionId,
            "prepared",
            previous,
            candidate,
            continueUvsrUpdate,
            DateTimeOffset.UtcNow);
        JsonStore.WriteAtomic(_paths.LauncherTransactionFile, transaction);
        try
        {
            JsonStore.WriteAtomic(_paths.LauncherStateFile, candidate);
            transaction = transaction with { Phase = "state-activated" };
            JsonStore.WriteAtomic(_paths.LauncherTransactionFile, transaction);
            shell.Apply(installationId, rendererState, candidate, log, transactionId);
            transaction = transaction with { Phase = "shell-committed" };
            JsonStore.WriteAtomic(_paths.LauncherTransactionFile, transaction);
            if (continueUvsrUpdate)
            {
                transaction = transaction with { Phase = "awaiting-continuation" };
                JsonStore.WriteAtomic(_paths.LauncherTransactionFile, transaction);
            }
            else
            {
                if (TryDeleteActivationJournal(log))
                    TrySweepInactive(candidate, log);
            }
        }
        catch
        {
            if (transaction.Phase == "prepared")
            {
                if (File.Exists(_paths.LauncherTransactionFile))
                    File.Delete(_paths.LauncherTransactionFile);
            }
            throw;
        }
        return transactionId;
    }

    internal void Recover(
        Guid installationId,
        InstallState? rendererState,
        ShellIntegration shell,
        InstallLog log)
    {
        if (!File.Exists(_paths.LauncherTransactionFile))
            return;
        LauncherActivationRecord transaction = JsonStore.Read<LauncherActivationRecord>(
            _paths.LauncherTransactionFile);
        ValidateActivationRecord(transaction, installationId);
        if (transaction.Phase == "continuation-complete")
        {
            if (TryDeleteActivationJournal(log))
                TrySweepInactive(transaction.CandidateState, log);
            return;
        }
        if (transaction.Phase == "prepared")
        {
            if (transaction.PreviousState is null)
            {
                if (File.Exists(_paths.LauncherStateFile))
                    File.Delete(_paths.LauncherStateFile);
            }
            else
            {
                transaction.PreviousState.Validate(installationId);
                ValidatePackage(transaction.PreviousState);
                JsonStore.WriteAtomic(_paths.LauncherStateFile, transaction.PreviousState);
            }
            File.Delete(_paths.LauncherTransactionFile);
            log.Write("Discarded an interrupted pre-activation launcher update.");
            return;
        }

        transaction.CandidateState.Validate(installationId);
        ValidatePackage(transaction.CandidateState);
        JsonStore.WriteAtomic(_paths.LauncherStateFile, transaction.CandidateState);
        shell.Apply(installationId, rendererState, transaction.CandidateState,
            log, transaction.TransactionId);
        if (transaction.ContinueUvsrUpdate)
        {
            transaction = transaction with { Phase = "awaiting-continuation" };
            JsonStore.WriteAtomic(_paths.LauncherTransactionFile, transaction);
        }
        else
        {
            if (TryDeleteActivationJournal(log))
                TrySweepInactive(transaction.CandidateState, log);
        }
        log.Write("Finished an interrupted UVSR Launcher activation safely.");
    }

    internal Guid? FindPendingContinuation(Guid installationId, Guid? requested)
    {
        if (!File.Exists(_paths.LauncherTransactionFile))
            return null;
        LauncherActivationRecord transaction =
            JsonStore.Read<LauncherActivationRecord>(_paths.LauncherTransactionFile);
        ValidateActivationRecord(transaction, installationId);
        if (transaction.Phase != "awaiting-continuation" ||
            !transaction.ContinueUvsrUpdate ||
            (requested is not null && requested != transaction.TransactionId))
            return null;
        transaction.CandidateState.Validate(installationId);
        ValidatePackage(transaction.CandidateState);
        string running = Environment.ProcessPath ?? string.Empty;
        string expected = _paths.LauncherExecutable(
            transaction.CandidateState.ExecutableSha256);
        if (!string.Equals(Path.GetFullPath(running), Path.GetFullPath(expected),
                StringComparison.OrdinalIgnoreCase))
            return null;
        return transaction.TransactionId;
    }

    internal void CompleteContinuation(
        Guid installationId,
        Guid transactionId,
        InstallLog log)
    {
        if (!File.Exists(_paths.LauncherTransactionFile))
            return;
        LauncherActivationRecord transaction =
            JsonStore.Read<LauncherActivationRecord>(_paths.LauncherTransactionFile);
        ValidateActivationRecord(transaction, installationId);
        if (transaction.TransactionId != transactionId ||
            transaction.Phase != "awaiting-continuation" ||
            !transaction.ContinueUvsrUpdate)
            throw new InstallerException(
                "The pending UVSR update continuation did not match this launcher.");
        transaction = transaction with { Phase = "continuation-complete" };
        JsonStore.WriteAtomic(_paths.LauncherTransactionFile, transaction);
        if (TryDeleteActivationJournal(log))
            TrySweepInactive(transaction.CandidateState, log);
        log.Write("Completed the renderer update selected before launcher activation.");
    }

    private bool TryDeleteActivationJournal(InstallLog log)
    {
        try
        {
            File.Delete(_paths.LauncherTransactionFile);
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            log.Write($"Launcher activation cleanup will finish next time: {ex.Message}");
            return false;
        }
    }

    internal static bool TryRedirectToActive(
        InstallerPaths paths,
        OwnerMarker marker,
        IReadOnlyList<string> arguments,
        out string? activePath)
    {
        activePath = null;
        if (!File.Exists(paths.LauncherStateFile))
            return false;
        LauncherState state;
        try
        {
            state = JsonStore.Read<LauncherState>(paths.LauncherStateFile);
            state.Validate(marker.InstallationId);
            ValidatePackage(paths, state);
        }
        catch (InstallerException)
        {
            // Let the directly invoked launcher open its normal repair UI. It
            // must never follow an unverified active pointer.
            return false;
        }
        string running = Environment.ProcessPath
            ?? throw new InstallerException("Windows could not locate the running UVSR Launcher.");
        string active = paths.LauncherExecutable(state.ExecutableSha256);
        if (string.Equals(Path.GetFullPath(running), Path.GetFullPath(active),
                StringComparison.OrdinalIgnoreCase))
            return false;
        if (state.ReleaseSequence < ProductConstants.LauncherReleaseSequence)
            return false;
        if (state.ReleaseSequence == ProductConstants.LauncherReleaseSequence)
        {
            string runningHash = PayloadPackager.ComputeSha256(running);
            if (!string.Equals(runningHash, state.ExecutableSha256, StringComparison.Ordinal))
                throw new InstallerException(
                    "This launcher has different files from the installed release. " +
                    "Open the installed UVSR Launcher shortcut instead.");
            return false;
        }
        if (arguments.Count > 1 ||
            (arguments.Count == 1 &&
             !arguments[0].Equals("--uninstall", StringComparison.OrdinalIgnoreCase)))
            return false;
        activePath = active;
        ProcessStartInfo start = new()
        {
            FileName = active,
            WorkingDirectory = paths.LauncherVersionRoot(state.ExecutableSha256),
            UseShellExecute = true
        };
        foreach (string argument in arguments)
            start.ArgumentList.Add(argument);
        _ = Process.Start(start) ?? throw new InstallerException(
            "Windows could not open the active UVSR Launcher.");
        return true;
    }

    internal string ActiveExecutable(LauncherState state)
    {
        ValidatePackage(state);
        return _paths.LauncherExecutable(state.ExecutableSha256);
    }

    internal static LauncherFeed ParseAndValidateFeed(byte[] data)
    {
        if (data.LongLength is <= 0 or > ProductConstants.MaximumLauncherFeedBytes)
            throw new InstallerException("The launcher update feed was empty or too large.");
        try
        {
            RejectDuplicateJsonProperties(data);
            LauncherFeed feed = JsonSerializer.Deserialize<LauncherFeed>(data,
                JsonStore.Options) ?? throw new JsonException("The feed was empty.");
            ValidateFeed(feed);
            return feed;
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex) when (ex is JsonException or InvalidOperationException)
        {
            throw new InstallerException("The launcher update feed was invalid.", ex);
        }
    }

    internal static string ParseMainCommit(byte[] data)
    {
        if (data.LongLength is <= 0 or > ProductConstants.MaximumLauncherFeedBytes)
            throw new InstallerException("GitHub returned an invalid UVSR update record.");
        try
        {
            RejectDuplicateJsonProperties(data);
            using JsonDocument document = JsonDocument.Parse(data);
            JsonElement root = document.RootElement;
            string reference = root.GetProperty("ref").GetString() ?? string.Empty;
            JsonElement gitObject = root.GetProperty("object");
            string type = gitObject.GetProperty("type").GetString() ?? string.Empty;
            string commit = gitObject.GetProperty("sha").GetString() ?? string.Empty;
            if (reference != ProductConstants.RepositoryMainRef || type != "commit" ||
                !ProductConstants.CommitRegex().IsMatch(commit))
                throw new InstallerException("GitHub returned an invalid UVSR main revision.");
            return commit;
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex) when (ex is JsonException or InvalidOperationException or KeyNotFoundException)
        {
            throw new InstallerException("GitHub returned an invalid UVSR update record.", ex);
        }
    }

    internal static Uri BuildArtifactUri(LauncherFeed feed)
    {
        ValidateFeed(feed);
        return BuildReleaseArtifactUri(feed, $"uvsr-launcher-v{feed.Version}");
    }

    internal static Uri BuildLatestArtifactUri(LauncherFeed feed)
    {
        ValidateFeed(feed);
        return BuildReleaseArtifactUri(feed, "uvsr-launcher-latest");
    }

    private static Uri BuildReleaseArtifactUri(LauncherFeed feed, string releaseTag)
    {
        ValidateFeed(feed);
        return new Uri("https://github.com/brockliddicoat/uvsr/releases/download/" +
                       $"{releaseTag}/{ProductConstants.LauncherArtifactName}");
    }

    private static bool IsUnavailableArtifactError(InstallerException ex)
    {
        return ex.Message.Contains("HTTP 404", StringComparison.OrdinalIgnoreCase) &&
               ex.Message.Contains("download service returned", StringComparison.OrdinalIgnoreCase);
    }

    private async Task<LauncherFeed> DownloadLauncherFeedAsync(
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        string path = Path.Combine(_paths.DownloadsDirectory, "launcher-feed-v1.json");
        await _downloads.DownloadAndVerifyAsync(new Uri(ProductConstants.LauncherFeedUrl),
            path, null, ProductConstants.MaximumLauncherFeedBytes, progress, log,
            cancellationToken, phase: "Checking UVSR Launcher");
        return ParseAndValidateFeed(await File.ReadAllBytesAsync(path, cancellationToken));
    }

    private async Task<string> DownloadMainCommitAsync(
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        string path = Path.Combine(_paths.DownloadsDirectory, "uvsr-main-ref.json");
        await _downloads.DownloadAndVerifyAsync(new Uri(ProductConstants.RepositoryMainApi),
            path, null, ProductConstants.MaximumLauncherFeedBytes, progress, log,
            cancellationToken, phase: "Checking UVSR");
        return ParseMainCommit(await File.ReadAllBytesAsync(path, cancellationToken));
    }

    private LauncherState StageExecutable(
        string source,
        Guid installationId,
        long releaseSequence,
        string version,
        long? expectedSize,
        string? expectedHash,
        bool desktopShortcut,
        InstallLog log)
    {
        ValidatePeX64(source);
        ValidateFileMetadata(source, version);
        FileInfo sourceInfo = new(source);
        if (expectedSize is not null && sourceInfo.Length != expectedSize)
            throw new InstallerException("The UVSR Launcher did not match its published size.");
        string hash = PayloadPackager.ComputeSha256(source);
        if (expectedHash is not null &&
            !CryptographicOperations.FixedTimeEquals(Convert.FromHexString(expectedHash),
                Convert.FromHexString(hash)))
            throw new InstallerException("The UVSR Launcher did not match its published SHA-256.");

        DateTimeOffset installedUtc = DateTimeOffset.UtcNow;
        LauncherState state = new(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, installationId, releaseSequence, version,
            hash, desktopShortcut, installedUtc);
        state.Validate(installationId);
        string finalRoot = _paths.LauncherVersionRoot(hash);
        if (Directory.Exists(finalRoot))
        {
            try
            {
                ValidatePackage(state);
                LauncherPackageManifest existing =
                    JsonStore.Read<LauncherPackageManifest>(
                        _paths.LauncherPackageMarker(hash));
                return state with { InstalledUtc = existing.InstalledUtc };
            }
            catch (InstallerException ex)
            {
                log.Write("The existing launcher package needs repair and will be preserved " +
                          $"before replacement: {ex.Message}");
            }
        }

        SafePaths.RejectReparsePathChain(_paths.LauncherStagingDirectory,
            "launcher staging directory");
        Directory.CreateDirectory(_paths.LauncherStagingDirectory);
        Directory.CreateDirectory(_paths.LauncherVersionsDirectory);
        string stage = Path.Combine(_paths.LauncherStagingDirectory,
            Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(stage);
        try
        {
            string stagedExecutable = Path.Combine(stage,
                ProductConstants.LauncherExecutableName);
            using (FileStream input = new(source, FileMode.Open, FileAccess.Read,
                       FileShare.Read | FileShare.Delete))
            using (FileStream output = new(stagedExecutable, FileMode.CreateNew,
                       FileAccess.Write, FileShare.None, 1024 * 1024, FileOptions.WriteThrough))
            {
                input.CopyTo(output);
                output.Flush(flushToDisk: true);
            }
            if (!string.Equals(PayloadPackager.ComputeSha256(stagedExecutable), hash,
                    StringComparison.Ordinal))
                throw new InstallerException("The UVSR Launcher did not copy correctly.");
            LauncherPackageManifest manifest = new(
                ProductConstants.LauncherSchemaVersion,
                ProductConstants.ProductId,
                installationId,
                releaseSequence,
                version,
                hash,
                sourceInfo.Length,
                installedUtc);
            JsonStore.WriteAtomic(Path.Combine(stage, ".uvsr-launcher-package.json"),
                manifest);
            SafePaths.RejectReparsePathChain(finalRoot,
                "launcher version directory");
            string? preservedRoot = null;
            if (Directory.Exists(finalRoot))
            {
                preservedRoot = Path.Combine(_paths.LauncherVersionsDirectory,
                    $"{hash}.preserved-{Guid.NewGuid():N}");
                SafePaths.RejectReparsePathChain(preservedRoot,
                    "preserved launcher package directory");
                Directory.Move(finalRoot, preservedRoot);
                log.Write($"Preserved the damaged launcher package at {preservedRoot}.");
            }
            try
            {
                Directory.Move(stage, finalRoot);
            }
            catch (Exception promotionFailure)
            {
                if (preservedRoot is not null && Directory.Exists(preservedRoot) &&
                    !Directory.Exists(finalRoot))
                {
                    try
                    {
                        Directory.Move(preservedRoot, finalRoot);
                    }
                    catch (Exception rollbackFailure)
                    {
                        throw new InstallerException(
                            "UVSR Launcher preserved the damaged package but could not restore its original path after replacement stopped.",
                            new AggregateException(promotionFailure, rollbackFailure));
                    }
                }
                throw;
            }
            ValidatePackage(state);
            log.Write($"Staged UVSR Launcher {version} ({hash}).");
            return state;
        }
        finally
        {
            if (Directory.Exists(stage))
                SafePaths.DeleteOwnedTree(stage, _paths.LauncherStagingDirectory);
        }
    }

    private void ValidatePackage(LauncherState state) => ValidatePackage(_paths, state);

    internal static LauncherReleaseIdentity? ResolveHighestDefensibleIdentity(
        IEnumerable<LauncherReleaseIdentity> identities)
    {
        Dictionary<long, LauncherReleaseIdentity> bySequence = new();
        foreach (LauncherReleaseIdentity identity in identities)
        {
            identity.Validate();
            if (bySequence.TryGetValue(identity.ReleaseSequence,
                    out LauncherReleaseIdentity existing))
            {
                RequireSameReleaseIdentity(existing, identity,
                    "UVSR Launcher found conflicting files for the same release sequence.");
            }
            else
            {
                bySequence.Add(identity.ReleaseSequence, identity);
            }
        }

        return bySequence.Count == 0
            ? null
            : bySequence.Values.MaxBy(identity => identity.ReleaseSequence);
    }

    internal static LauncherReleaseIdentity? HighestDefensibleIdentity(
        LauncherActivationInspection inspection)
    {
        List<LauncherReleaseIdentity> identities = new();
        if (inspection.RecordedState is not null)
            identities.Add(LauncherReleaseIdentity.From(inspection.RecordedState));
        if (inspection.ValidState is not null)
            identities.Add(LauncherReleaseIdentity.From(inspection.ValidState));
        LauncherReleaseIdentity? identity = ResolveHighestDefensibleIdentity(identities);
        if ((identity?.ReleaseSequence ?? 0) != inspection.HighestDefensibleSequence)
            throw new InstallerException(
                "UVSR Launcher could not establish a consistent installed release identity.");
        return identity;
    }

    internal static void ValidatePackageDirectoryBinding(
        string directoryHash,
        LauncherPackageManifest marker)
    {
        if (!ProductConstants.HashRegex().IsMatch(directoryHash) ||
            !string.Equals(marker.ExecutableSha256, directoryHash,
                StringComparison.Ordinal))
            throw new InstallerException(
                "The UVSR Launcher package record did not match its version directory.");
    }

    internal static LauncherActivationRecord PreserveAwaitingContinuation(
        LauncherActivationRecord pending,
        LauncherState candidate,
        Guid installationId)
    {
        ValidateActivationRecord(pending, installationId);
        pending.CandidateState.Validate(installationId);
        candidate.Validate(installationId);
        if (pending.Phase != "awaiting-continuation" ||
            !pending.ContinueUvsrUpdate)
            throw new InstallerException(
                "An earlier UVSR Launcher change must finish recovery before another launcher change can begin.");
        RequireSameReleaseIdentity(LauncherReleaseIdentity.From(pending.CandidateState),
            LauncherReleaseIdentity.From(candidate),
            "Finish the pending UVSR update before installing a different UVSR Launcher release.");
        LauncherState merged = pending.CandidateState with
        {
            DesktopShortcut = candidate.DesktopShortcut
        };
        return pending with { CandidateState = merged };
    }

    internal static void RequireSameReleaseIdentity(
        LauncherReleaseIdentity expected,
        LauncherReleaseIdentity actual,
        string conflictMessage)
    {
        if (expected.ReleaseSequence != actual.ReleaseSequence ||
            !string.Equals(expected.Version, actual.Version, StringComparison.Ordinal) ||
            !string.Equals(expected.ExecutableSha256, actual.ExecutableSha256,
                StringComparison.Ordinal))
            throw new InstallerException(conflictMessage);
    }

    internal static ComponentUpdateState ClassifyLauncherUpdate(
        LauncherReleaseIdentity current,
        bool recordedStateHealthy,
        LauncherReleaseIdentity available)
    {
        current.Validate();
        available.Validate();
        if (available.ReleaseSequence < current.ReleaseSequence)
            return ComponentUpdateState.Current;
        if (available.ReleaseSequence > current.ReleaseSequence)
            return ComponentUpdateState.UpdateAvailable;
        RequireSameReleaseIdentity(current, available,
            "The UVSR Launcher update feed reused a release sequence with different files.");
        return recordedStateHealthy
            ? ComponentUpdateState.Current
            : ComponentUpdateState.RepairNeeded;
    }

    private LauncherReleaseIdentity CurrentProcessIdentity()
    {
        string current = Environment.ProcessPath
            ?? throw new InstallerException(
                "Windows could not locate the running UVSR Launcher.");
        return new LauncherReleaseIdentity(ProductConstants.LauncherReleaseSequence,
            ProductConstants.LauncherVersion, PayloadPackager.ComputeSha256(current));
    }

    private bool IsRecordedStateHealthyAtIdentity(
        LauncherState? recorded,
        LauncherReleaseIdentity identity)
    {
        if (recorded is null)
            return false;
        try
        {
            RequireSameReleaseIdentity(LauncherReleaseIdentity.From(recorded), identity,
                "The installed UVSR Launcher identity changed unexpectedly.");
            ValidatePackage(recorded);
            return true;
        }
        catch (Exception ex) when (ex is InstallerException or IOException or
                                   UnauthorizedAccessException)
        {
            return false;
        }
    }

    internal static void ValidatePackage(InstallerPaths paths, LauncherState state)
    {
        state.Validate(state.InstallationId);
        string root = paths.LauncherVersionRoot(state.ExecutableSha256);
        SafePaths.RejectReparsePathChain(root, "installed launcher package");
        string executable = paths.LauncherExecutable(state.ExecutableSha256);
        string markerPath = paths.LauncherPackageMarker(state.ExecutableSha256);
        LauncherPackageManifest marker = JsonStore.Read<LauncherPackageManifest>(markerPath);
        if (marker.SchemaVersion != ProductConstants.LauncherSchemaVersion ||
            !string.Equals(marker.ProductId, ProductConstants.ProductId,
                StringComparison.OrdinalIgnoreCase) ||
            marker.InstallationId != state.InstallationId ||
            marker.ReleaseSequence != state.ReleaseSequence ||
            marker.Version != state.Version || marker.ExecutableSha256 != state.ExecutableSha256 ||
            marker.ExecutableSize <= 0 || marker.ExecutableSize > ProductConstants.MaximumLauncherBytes ||
            !File.Exists(executable) || new FileInfo(executable).Length != marker.ExecutableSize ||
            !string.Equals(PayloadPackager.ComputeSha256(executable),
                state.ExecutableSha256, StringComparison.Ordinal))
            throw new InstallerException("The installed UVSR Launcher package is incomplete or changed.");
        string[] entries = Directory.EnumerateFileSystemEntries(root)
            .Select(Path.GetFileName)
            .OrderBy(name => name, StringComparer.OrdinalIgnoreCase)
            .ToArray()!;
        string[] expected = { ".uvsr-launcher-package.json", ProductConstants.LauncherExecutableName };
        Array.Sort(expected, StringComparer.OrdinalIgnoreCase);
        if (!entries.SequenceEqual(expected, StringComparer.OrdinalIgnoreCase))
            throw new InstallerException("The installed UVSR Launcher package contains unexpected files.");
    }

    internal void SweepInactive(LauncherState active, InstallLog log)
    {
        TrySweepInactive(active, log);
    }

    private void TrySweepInactive(LauncherState active, InstallLog log)
    {
        try
        {
            if (!Directory.Exists(_paths.LauncherVersionsDirectory))
                return;
            List<(string Directory, string Executable, LauncherPackageManifest Marker)> packages = new();
            foreach (string directory in Directory.EnumerateDirectories(_paths.LauncherVersionsDirectory))
            {
                string hash = Path.GetFileName(directory);
                if (hash == active.ExecutableSha256 || !ProductConstants.HashRegex().IsMatch(hash))
                    continue;
                try
                {
                    LauncherPackageManifest marker = JsonStore.Read<LauncherPackageManifest>(
                        _paths.LauncherPackageMarker(hash));
                    ValidatePackageDirectoryBinding(hash, marker);
                    LauncherState candidate = new(marker.SchemaVersion, marker.ProductId,
                        marker.InstallationId, marker.ReleaseSequence, marker.Version,
                        marker.ExecutableSha256, active.DesktopShortcut, marker.InstalledUtc);
                    candidate.Validate(active.InstallationId);
                    ValidatePackage(candidate);
                    packages.Add((directory, _paths.LauncherExecutable(hash), marker));
                }
                catch (Exception ex) when (ex is InstallerException or IOException or UnauthorizedAccessException)
                {
                    log.Write($"Preserved an unverified launcher package during cleanup: {ex.Message}");
                }
            }
            bool retainedPrevious = false;
            foreach ((string directory, string executable, LauncherPackageManifest marker) in
                     packages.OrderByDescending(package => package.Marker.ReleaseSequence)
                         .ThenByDescending(package => package.Marker.InstalledUtc))
            {
                try
                {
                    if (ProcessInspector.FindProcessesByExecutable(executable).Count > 0)
                        continue;
                    if (!retainedPrevious)
                    {
                        retainedPrevious = true;
                        log.Write($"Retained previous UVSR Launcher {marker.Version} as a recovery package.");
                        continue;
                    }
                    SafePaths.DeleteOwnedTree(directory, _paths.LauncherVersionsDirectory);
                    log.Write($"Removed inactive UVSR Launcher package {marker.ExecutableSha256}.");
                }
                catch (Exception ex) when (ex is InstallerException or IOException or UnauthorizedAccessException)
                {
                    log.Write($"Deferred inactive launcher cleanup safely: {ex.Message}");
                }
            }

            SweepStaging(log);
            if (File.Exists(_paths.LegacyManagerPath) &&
                ProcessInspector.FindProcessesByExecutable(_paths.LegacyManagerPath).Count == 0)
            {
                File.Delete(_paths.LegacyManagerPath);
                log.Write("Removed the retired UVSR Installer compatibility executable.");
            }
        }
        catch (Exception ex) when (ex is InstallerException or IOException or UnauthorizedAccessException)
        {
            log.Write($"Post-update launcher cleanup was deferred safely: {ex.Message}");
        }
    }

    private void SweepStaging(InstallLog log)
    {
        if (!Directory.Exists(_paths.LauncherStagingDirectory))
            return;
        foreach (string stage in Directory.EnumerateDirectories(
                     _paths.LauncherStagingDirectory))
        {
            if (!Guid.TryParseExact(Path.GetFileName(stage), "N", out _))
                continue;
            try
            {
                SafePaths.DeleteOwnedTree(stage, _paths.LauncherStagingDirectory);
                log.Write("Removed interrupted launcher staging files.");
            }
            catch (Exception ex) when (ex is InstallerException or IOException or UnauthorizedAccessException)
            {
                log.Write($"Preserved busy launcher staging files: {ex.Message}");
            }
        }
    }

    private string TryCurrentLauncherVersion(Guid installationId)
    {
        try { return TryReadState(installationId)?.Version ?? ProductConstants.LauncherVersion; }
        catch (InstallerException) { return ProductConstants.LauncherVersion; }
    }

    private static void ValidateFeed(LauncherFeed feed)
    {
        if (feed is null || feed.Artifact is null ||
            feed.SchemaVersion != ProductConstants.LauncherSchemaVersion ||
            !string.Equals(feed.ProductId, ProductConstants.ProductId,
                StringComparison.OrdinalIgnoreCase) ||
            !string.IsNullOrWhiteSpace(feed.Channel) &&
            !string.Equals(feed.Channel, "stable", StringComparison.OrdinalIgnoreCase) ||
            feed.ReleaseSequence is < 1 or > ProductConstants.MaximumReleaseSequence ||
            string.IsNullOrWhiteSpace(feed.Version) ||
            !ProductConstants.StableVersionRegex().IsMatch(feed.Version) ||
            !feed.Version.Split('.').All(part => int.TryParse(part, out _)) ||
            feed.Artifact.Name != ProductConstants.LauncherArtifactName ||
            feed.Artifact.Size is <= 0 or > ProductConstants.MaximumLauncherBytes ||
            string.IsNullOrWhiteSpace(feed.Artifact.Sha256) ||
            !ProductConstants.HashRegex().IsMatch(feed.Artifact.Sha256))
            throw new InstallerException("The launcher update feed did not match its required format.");
    }

    private static void ValidateActivationRecord(
        LauncherActivationRecord record,
        Guid installationId)
    {
        if (record.SchemaVersion != ProductConstants.LauncherSchemaVersion ||
            record.ProductId != ProductConstants.ProductId ||
            record.InstallationId != installationId || record.TransactionId == Guid.Empty ||
            record.Phase is not ("prepared" or "state-activated" or "shell-committed" or
                "awaiting-continuation" or "continuation-complete") ||
            record.CandidateState is null)
            throw new InstallerException("The interrupted launcher update record was invalid.");
    }

    private static void ValidatePeX64(string path)
    {
        try
        {
            using FileStream stream = new(path, FileMode.Open, FileAccess.Read,
                FileShare.Read | FileShare.Delete);
            using BinaryReader reader = new(stream);
            if (stream.Length < 0x100 || reader.ReadUInt16() != 0x5A4D)
                throw new InstallerException("The launcher file was not a Windows program.");
            stream.Position = 0x3C;
            int peOffset = reader.ReadInt32();
            if (peOffset < 0x40 || peOffset > stream.Length - 6)
                throw new InstallerException("The launcher file had an invalid Windows header.");
            stream.Position = peOffset;
            if (reader.ReadUInt32() != 0x00004550 || reader.ReadUInt16() != 0x8664)
                throw new InstallerException("The launcher file was not an x64 Windows program.");
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or EndOfStreamException)
        {
            throw new InstallerException("The launcher file could not be inspected safely.", ex);
        }
    }

    private static void ValidateFileMetadata(string path, string expectedVersion)
    {
        FileVersionInfo info = FileVersionInfo.GetVersionInfo(path);
        if (!string.Equals(info.ProductName, "UVSR Launcher", StringComparison.Ordinal) ||
            string.IsNullOrWhiteSpace(info.ProductVersion) ||
            !info.ProductVersion.StartsWith(expectedVersion, StringComparison.Ordinal))
            throw new InstallerException("The launcher file had unexpected product metadata.");
    }

    private static void RejectDuplicateJsonProperties(ReadOnlySpan<byte> data)
    {
        Utf8JsonReader reader = new(data, new JsonReaderOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 32
        });
        Stack<HashSet<string>?> scopes = new();
        while (reader.Read())
        {
            switch (reader.TokenType)
            {
                case JsonTokenType.StartObject:
                    scopes.Push(new HashSet<string>(StringComparer.Ordinal));
                    break;
                case JsonTokenType.StartArray:
                    scopes.Push(null);
                    break;
                case JsonTokenType.EndObject:
                case JsonTokenType.EndArray:
                    if (scopes.Count == 0)
                        throw new JsonException("Unbalanced JSON scope.");
                    scopes.Pop();
                    break;
                case JsonTokenType.PropertyName:
                    if (scopes.Count == 0 || scopes.Peek() is not HashSet<string> names ||
                        !names.Add(reader.GetString() ?? string.Empty))
                        throw new JsonException("Duplicate JSON property.");
                    break;
            }
        }
        if (scopes.Count != 0)
            throw new JsonException("Incomplete JSON document.");
    }
}

internal readonly record struct LauncherReleaseIdentity(
    long ReleaseSequence,
    string Version,
    string ExecutableSha256)
{
    internal static LauncherReleaseIdentity From(LauncherState state) =>
        new(state.ReleaseSequence, state.Version, state.ExecutableSha256);

    internal static LauncherReleaseIdentity From(LauncherFeed feed) =>
        new(feed.ReleaseSequence, feed.Version, feed.Artifact.Sha256);

    internal void Validate()
    {
        if (ReleaseSequence is < 1 or > ProductConstants.MaximumReleaseSequence ||
            string.IsNullOrWhiteSpace(Version) ||
            !ProductConstants.StableVersionRegex().IsMatch(Version) ||
            !Version.Split('.').All(part => int.TryParse(part, out _)) ||
            string.IsNullOrWhiteSpace(ExecutableSha256) ||
            !ProductConstants.HashRegex().IsMatch(ExecutableSha256))
            throw new InstallerException(
                "A UVSR Launcher release identity was invalid.");
    }
}
