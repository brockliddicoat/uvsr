using System.IO.Compression;
using System.Drawing;
using System.Diagnostics;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using UvsrInstaller;

internal static class Program
{
    private static readonly List<(string Name, Action Test)> Tests = new()
    {
        ("installer roots do not overlap renderer user data", PathsPreserveRendererData),
        ("redirected Windows known folders retain a safe shortcut boundary", RedirectedKnownFoldersAreSupported),
        ("strict descendant checks reject traversal", StrictDescendantRejectsTraversal),
        ("state validation rejects arbitrary version paths", StateRejectsArbitraryPaths),
        ("state validation rejects null JSON fields", StateRejectsNullFields),
        ("atomic JSON round trip preserves validated state", AtomicStateRoundTrip),
        ("ZIP extraction rejects traversal", ZipTraversalIsRejected),
        ("ZIP extraction accepts a normal package", NormalZipExtracts),
        ("owned deletion preserves an outside sibling", OwnedDeletionPreservesSibling),
        ("owned deletion rejects an intermediate directory link", OwnedDeletionRejectsLinkedAncestor),
        ("ownership refuses a pre-existing unmarked root", OwnershipRejectsCollision),
        ("ownership recovers an interrupted empty first-run root", OwnershipRecoversEmptyRoot),
        ("ownership markers recover a missing companion root", OwnershipRecoversCompanionRoot),
        ("cleanup ownership recovers an interrupted empty operations root", OperationsOwnershipRecoversEmptyRoot),
        ("runtime package validation requires exact executable hash", PackageHashIsVerified),
        ("runtime package validation requires every recorded file", PackageInventoryIsVerified),
        ("runtime package validation rejects null inventory data", PackageNullInventoryIsRejected),
        ("DirectX runtime contract binds the packaged core", D3D12RuntimeContractIsBound),
        ("pinned prerequisite metadata is HTTPS and hashed", ToolMetadataIsPinned),
        ("pinned build dependencies are offline-configured", BuildDependenciesArePinnedAndOffline),
        ("renderer build contract is strict and launcher-bound", RendererBuildContractIsStrict),
        ("renderer source bridge registry and resource are exact", RendererSourceBridgeIsExact),
        ("renderer source bridge identity rejects mutation", RendererSourceBridgeRejectsMutation),
        ("renderer source bridge applies to its exact Git base", RendererSourceBridgeGitApplicationWorks),
        ("sequence-4 persisted schemas accept bridged renderer identity", SequenceFourLauncherStateIsReadable),
        ("Visual Studio layout acquisition is narrowly resumable", VisualStudioLayoutContractIsBounded),
        ("Visual Studio crash recovery is fail-closed and explicit", VisualStudioRecoveryIsExplicit),
        ("fresh downloads promote the closed verified file", DownloadPromotionWorks),
        ("real HTTPS sockets resume a truncated response", LoopbackHttpsResumeWorks),
        ("downloads retry transient HTTP responses", DownloadRetriesTransientStatus),
        ("downloads resume only with a matching strong ETag", DownloadResumesWithStrongValidator),
        ("stalled downloads report a stall and retry", DownloadStallIsRetried),
        ("permanent HTTP responses are not retried", PermanentDownloadFailureDoesNotRetry),
        ("all retryable HTTP status codes recover", RetryableHttpStatusMatrixRecovers),
        ("transport failures retry while TLS policy failures do not", TransportFailureClassificationIsNarrow),
        ("range ignored by the server restarts safely", IgnoredRangeRestartsSafely),
        ("changed resume validators restart from zero", ChangedValidatorRestartsSafely),
        ("a resumed response may omit its repeated ETag", MissingRepeatedValidatorResumes),
        ("HTTP 416 finalizes an already complete partial", RangeNotSatisfiableFinalizesCompletePartial),
        ("hashless HTTP 416 requires a matching validator", HashlessRangeNotSatisfiableRestarts),
        ("partial responses cannot exceed their declared range", PartialRangeCannotOverflow),
        ("short partial responses remain resumable failures", ShortPartialRangeIsRetriedSafely),
        ("HTTPS redirects are followed within a strict bound", HttpsRedirectsAreBounded),
        ("redirect loops stop at the safety bound", RedirectLoopsStop),
        ("redirects cannot downgrade to HTTP", RedirectDowngradeIsRejected),
        ("cancellation interrupts download backoff", DownloadBackoffIsCancellable),
        ("unknown-length downloads obey their byte cap", UnknownLengthDownloadObeysLimit),
        ("failed downloads preserve an existing destination", FailedDownloadPreservesDestination),
        ("local partial-file failures are not retried as network errors", LocalDownloadFailureIsNotRetried),
        ("pre-cancelled process work never starts", PreCancelledProcessDoesNotStart),
        ("cancelled process jobs contain descendants", CancelledProcessJobContainsDescendants),
        ("direct process completion contains detached descendants", DirectParentExitContainsDescendant),
        ("Git child environment drops redirection variables", GitEnvironmentIsIsolated),
        ("managed Git configuration removes URL rewrites", GitConfigurationIsReplaced),
        ("managed source recreation discards poisoned Git metadata", SourceCachePoisonIsDiscarded),
        ("Git retry classification separates transient failures", GitRetryClassificationIsNarrow),
        ("Git ancestry classification preserves operational failures", GitAncestryClassificationIsExact),
        ("cross-session operation lock rejects a concurrent owner", OperationLockRejectsConcurrency),
        ("launch honors the lifecycle operation lock", LaunchHonorsLifecycleLock),
        ("standard-user cleanup watcher retries and self-deletes", CleanupWatcherRetriesAndSelfDeletes),
        ("cleanup watcher records a retry after timeout", CleanupWatcherRecordsTimeout),
        ("renderer and launcher product paths remain distinct", ProductRootsAreDistinct),
        ("legacy installer operation values remain stable", LegacyOperationValuesRemainStable),
        ("launcher actions remain available for repair states", LauncherActionStatesSupportRepair),
        ("renderer button intent never inverts a stale click", RendererButtonIntentIsStable),
        ("exact renderer process identity observes process exit", ExactRendererIdentityObservesExit),
        ("exact force-close terminates only the verified process handle", ExactTerminationUsesVerifiedHandle),
        ("update exits always stop indeterminate progress", UpdateExitStatesAreTerminal),
        ("update selection copy distinguishes failed checks", UpdateSelectionCopyIsAccurate),
        ("launcher windows stay inside small high-DPI work areas", LauncherLayoutsAreBounded),
        ("launcher feed parsing is strict and deterministic", LauncherFeedParsingIsStrict),
        ("checked-in launcher feed matches the strict production schema", CheckedInLauncherFeedIsValid),
        ("update checks identify live feed and source release skew", UpdateCheckDiagnosticsAreExact),
        ("GitHub main reference parsing is strict", MainReferenceParsingIsStrict),
        ("launcher state rejects rollback and path data", LauncherStateValidationIsStrict),
        ("launcher release identities reject equivocation", LauncherReleaseIdentityIsUnique),
        ("verified installed launchers converge startup automatically", LauncherStartupConverges),
        ("launcher activation recovery selects a valid package", LauncherRecoveryConverges),
        ("pending UVSR continuation keeps its transaction", LauncherContinuationIsPreserved),
        ("launcher package markers bind to their hash directory", LauncherMarkerBindingIsStrict),
        ("launcher shortcut ownership is structural and contained", LauncherShortcutLayoutIsContained),
        ("launcher package validation checks exact files and hash", LauncherPackageIsVerified),
        ("launcher release sequences have a safe upper bound", LauncherSequenceHasUpperBound),
        ("damaged launcher state preserves downgrade knowledge", LauncherInspectionPreventsSilentDowngrade),
        ("retained launcher packages recover a damaged pointer", LauncherInspectionFindsRecoveryPackage),
        ("locked launcher state blocks activation without mutation", LockedLauncherStateBlocksActivation),
        ("launcher cleanup preserves unverified packages", LauncherCleanupPreservesUnverifiedPackages),
        ("launcher publisher configuration is fail-closed or pinned", LauncherPublisherConfigurationIsValid),
        ("launcher copy wording is concise and accessible", LauncherCopyWordingIsConcise),
        ("launcher version metadata agrees across build inputs", LauncherVersionMetadataAgrees)
    };

    private static int Main(string[] args)
    {
        if (args.Length == 3 && args[0] == "--validate-build-output")
        {
            PayloadPackager.ValidateBuildOutput(args[1], args[2]);
            Console.WriteLine("Validated the exact UVSR renderer package inputs.");
            return 0;
        }
        if (args.Length == 2 && args[0] == "--source-build-smoke")
            return RunSourceBuildSmokeAsync(args[1]).GetAwaiter().GetResult();
        if (args.Length == 2 && args[0] == "--source-bridge-prepare-smoke")
            return RunSourceBridgePrepareSmokeAsync(args[1]).GetAwaiter().GetResult();
        if (args.Length == 2 && args[0] == "--job-child")
        {
            Thread.Sleep(1500);
            File.WriteAllText(args[1], "escaped");
            return 0;
        }
        if (args.Length == 3 &&
            (args[0] == "--job-parent" || args[0] == "--job-detached-parent"))
        {
            string appHost = Path.ChangeExtension(typeof(Program).Assembly.Location, ".exe");
            System.Diagnostics.ProcessStartInfo child = new()
            {
                FileName = appHost,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            child.ArgumentList.Add("--job-child");
            child.ArgumentList.Add(args[1]);
            using System.Diagnostics.Process childProcess =
                System.Diagnostics.Process.Start(child)
                ?? throw new InvalidOperationException("Could not start the job child fixture.");
            File.WriteAllText(args[2], "started");
            if (args[0] == "--job-parent")
                Thread.Sleep(30_000);
            return 0;
        }

        int failures = 0;
        foreach ((string name, Action test) in Tests)
        {
            try
            {
                test();
                Console.WriteLine($"PASS {name}");
            }
            catch (Exception ex)
            {
                failures++;
                Console.Error.WriteLine($"FAIL {name}: {ex.Message}");
            }
        }
        Console.WriteLine($"{Tests.Count - failures}/{Tests.Count} launcher contract tests passed.");
        return failures == 0 ? 0 : 1;
    }

    private static async Task<int> RunSourceBuildSmokeAsync(string requestedRoot)
    {
        string root = Path.GetFullPath(requestedRoot);
        if (Directory.Exists(root) && Directory.EnumerateFileSystemEntries(root).Any())
            throw new InvalidOperationException(
                "The source-build smoke root must be new or empty; existing data was preserved.");
        Directory.CreateDirectory(root);
        InstallerPaths paths = InstallerPaths.Create(root,
            Path.Combine(root, "Desktop"), Path.Combine(root, "ProgramsMenu"));
        OwnerMarker owner = new OwnershipManager(paths).EnsureRoots();
        InstallLog log = new(paths.LogsDirectory, Console.WriteLine);
        InlineProgress progress = new(update => Console.WriteLine(
            $"SMOKE_PROGRESS={update.Phase}|{update.Detail}"));
        using DownloadManager downloads = new();
        ProcessRunner runner = new();
        ToolchainManager toolchain = new(paths, runner, downloads);
        ToolPaths tools = await toolchain.EnsureAsync(
            _ => Task.FromResult(false), progress, log, CancellationToken.None);
        SourceManager source = new(paths, runner);
        SourceResolution resolution = await source.ResolveMainAsync(tools, null,
            progress, log, CancellationToken.None);
        await source.PrepareExactSourceAsync(tools, resolution,
            progress, log, CancellationToken.None);
        await source.BuildAsync(tools, resolution, progress, log,
            CancellationToken.None);

        Guid transactionId = Guid.NewGuid();
        string versionId = $"{resolution.Commit}-{DateTime.UtcNow:yyyyMMddHHmmss}-" +
            Guid.NewGuid().ToString("N")[..8];
        PayloadPackager packager = new(paths);
        PackageManifest manifest = packager.Stage(owner.InstallationId,
            transactionId, versionId, resolution.Commit, log);
        string package = packager.Activate(transactionId, manifest);
        InstallState state = new(ProductConstants.SchemaVersion, owner.InstallationId,
            versionId, resolution.Commit, manifest.ExecutableSha256, false,
            DateTimeOffset.UtcNow);
        JsonStore.WriteAtomic(paths.StateFile, state);
        string executable = Path.Combine(package, "bin", "uvsr.exe");
        Console.WriteLine($"SMOKE_COMMIT={resolution.Commit}");
        Console.WriteLine($"SMOKE_UVSR={executable}");
        Console.WriteLine($"SMOKE_SHA256={manifest.ExecutableSha256}");
        return 0;
    }

    private static async Task<int> RunSourceBridgePrepareSmokeAsync(
        string requestedRoot)
    {
        string root = Path.GetFullPath(requestedRoot);
        if (Directory.Exists(root) && Directory.EnumerateFileSystemEntries(root).Any())
            throw new InvalidOperationException(
                "The source-bridge smoke root must be new or empty; existing data was preserved.");
        Directory.CreateDirectory(root);
        InstallerPaths paths = InstallerPaths.Create(root,
            Path.Combine(root, "Desktop"), Path.Combine(root, "ProgramsMenu"));
        Directory.CreateDirectory(paths.CacheDirectory);
        RunGit(RepositoryRoot(), null, "clone", "--no-checkout", "--no-hardlinks",
            "--local", RepositoryRoot(), paths.SourceDirectory);

        string git = GetGitExecutable();
        ToolPaths tools = new(git, git, git, string.Empty, string.Empty,
            string.Empty, string.Empty);
        ProcessRunner runner = new();
        SourceManager source = new(paths, runner);
        source.WriteSafeRepositoryConfig();
        InstallLog log = new(paths.LogsDirectory, Console.WriteLine);
        SourceResolution resolution = new(RendererSourceBridge.SourceCommit,
            RendererSourceBridge.PublicBaseCommit, false, false,
            RendererSourceBridge.Instance);
        InlineProgress progress = new(update => Console.WriteLine(
            $"BRIDGE_PROGRESS={update.Phase}|{update.Detail}"));
        await source.PrepareExactSourceAsync(tools, resolution, progress, log,
            CancellationToken.None);
        await source.ValidatePreparedSourceAsync(tools, resolution, log,
            CancellationToken.None);
        Console.WriteLine($"BRIDGE_BASE={resolution.PublicBaseCommit}");
        Console.WriteLine($"BRIDGE_COMMIT={resolution.SourceCommit}");
        Console.WriteLine($"BRIDGE_TREE={RendererSourceBridge.SourceTree}");
        return 0;
    }

    private static void PathsPreserveRendererData()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        string rendererData = Path.Combine(test.Root, "UVSR");
        Assert(!string.Equals(paths.ProgramRoot, rendererData, StringComparison.OrdinalIgnoreCase));
        Assert(!string.Equals(paths.StateRoot, rendererData, StringComparison.OrdinalIgnoreCase));
        Assert(paths.ProgramRoot.EndsWith(Path.Combine("Programs", "UVSR"), StringComparison.OrdinalIgnoreCase));
        Assert(paths.StateRoot.EndsWith("UVSR Installer", StringComparison.OrdinalIgnoreCase));
    }

    private static void RedirectedKnownFoldersAreSupported()
    {
        using TestDirectory test = new();
        string desktopTarget = Path.Combine(test.Root, "RedirectedDesktopTarget");
        string programsTarget = Path.Combine(test.Root, "RedirectedProgramsTarget");
        string desktopLink = Path.Combine(test.Root, "RedirectedDesktop");
        string programsLink = Path.Combine(test.Root, "RedirectedPrograms");
        string hostileTarget = Path.Combine(test.Root, "HostileShortcutTarget");
        Directory.CreateDirectory(desktopTarget);
        Directory.CreateDirectory(programsTarget);
        Directory.CreateDirectory(hostileTarget);
        CreateJunction(desktopLink, desktopTarget);
        CreateJunction(programsLink, programsTarget);
        try
        {
            InstallerPaths paths = InstallerPaths.Create(
                test.Root, desktopLink, programsLink);
            paths.ValidateShellPath(paths.DesktopShortcut,
                "redirected desktop shortcut fixture");
            paths.ValidateShellPath(paths.StartMenuDirectory,
                "redirected Start menu fixture");

            Directory.CreateDirectory(paths.StartMenuDirectory);
            paths.ValidateShellPath(paths.StartMenuShortcut,
                "redirected Start menu shortcut fixture");
            Directory.Delete(paths.StartMenuDirectory);

            CreateJunction(paths.StartMenuDirectory, hostileTarget);
            Expect<InstallerException>(() => paths.ValidateShellPath(
                paths.StartMenuShortcut, "linked Start menu child fixture"));
            Directory.Delete(paths.StartMenuDirectory);
        }
        finally
        {
            if (Directory.Exists(desktopLink))
                Directory.Delete(desktopLink);
            if (Directory.Exists(programsLink))
                Directory.Delete(programsLink);
        }
    }

    private static void StrictDescendantRejectsTraversal()
    {
        using TestDirectory test = new();
        string parent = Path.Combine(test.Root, "owned");
        Assert(SafePaths.IsStrictDescendant(Path.Combine(parent, "child"), parent));
        Assert(!SafePaths.IsStrictDescendant(parent, parent));
        Assert(!SafePaths.IsStrictDescendant(Path.Combine(parent, "..", "outside"), parent));
        Expect<InstallerException>(() => SafePaths.CombineDescendant(parent, @"..\outside"));
        Expect<InstallerException>(() => SafePaths.CombineDescendant(parent, @"C:\outside"));
        Expect<InstallerException>(() => SafePaths.CombineDescendant(parent, "stream:name"));
    }

    private static void StateRejectsArbitraryPaths()
    {
        Guid id = Guid.NewGuid();
        InstallState valid = new(ProductConstants.SchemaVersion, id,
            "0123456789abcdef0123456789abcdef01234567-20260817123456-89abcdef",
            "0123456789abcdef0123456789abcdef01234567",
            new string('a', 64), true, DateTimeOffset.UtcNow);
        valid.Validate(id);
        Expect<InstallerException>(() => (valid with { ActiveVersionId = @"..\outside" }).Validate(id));
        Expect<InstallerException>(() => (valid with { Commit = "main" }).Validate(id));
        Expect<InstallerException>(() => valid.Validate(Guid.NewGuid()));
    }

    private static void StateRejectsNullFields()
    {
        Guid id = Guid.NewGuid();
        InstallState valid = new(ProductConstants.SchemaVersion, id,
            "0123456789abcdef0123456789abcdef01234567-20260817123456-89abcdef",
            "0123456789abcdef0123456789abcdef01234567",
            new string('a', 64), true, DateTimeOffset.UtcNow);
        Expect<InstallerException>(() => (valid with { ActiveVersionId = null! }).Validate(id));
        Expect<InstallerException>(() => (valid with { Commit = null! }).Validate(id));
        Expect<InstallerException>(() => (valid with { ExecutableSha256 = null! }).Validate(id));

        using TestDirectory test = new();
        string path = Path.Combine(test.Root, "state.json");
        File.WriteAllText(path,
            "{\"schemaVersion\":1,\"installationId\":\"" + id +
            "\",\"activeVersionId\":null,\"commit\":null,\"executableSha256\":null," +
            "\"desktopShortcut\":false,\"installedUtc\":\"2026-08-17T00:00:00Z\"}");
        Expect<InstallerException>(() => JsonStore.Read<InstallState>(path));
    }

    private static void AtomicStateRoundTrip()
    {
        using TestDirectory test = new();
        Guid id = Guid.NewGuid();
        InstallState expected = new(ProductConstants.SchemaVersion, id,
            "0123456789abcdef0123456789abcdef01234567-20260817123456-89abcdef",
            "0123456789abcdef0123456789abcdef01234567",
            new string('b', 64), false, DateTimeOffset.UtcNow);
        string path = Path.Combine(test.Root, "state.json");
        JsonStore.WriteAtomic(path, expected);
        InstallState actual = JsonStore.Read<InstallState>(path);
        Assert(expected == actual);
        Assert(!Directory.EnumerateFiles(test.Root, "*.tmp").Any());
    }

    private static void ZipTraversalIsRejected()
    {
        using TestDirectory test = new();
        string zip = Path.Combine(test.Root, "bad.zip");
        using (ZipArchive archive = ZipFile.Open(zip, ZipArchiveMode.Create))
        {
            ZipArchiveEntry entry = archive.CreateEntry("../outside.txt");
            using StreamWriter writer = new(entry.Open());
            writer.Write("sentinel");
        }
        string destination = Path.Combine(test.Root, "extract");
        Expect<InstallerException>(() => SafePaths.ExtractVerifiedZip(zip, destination));
        Assert(!File.Exists(Path.Combine(test.Root, "outside.txt")));
    }

    private static void NormalZipExtracts()
    {
        using TestDirectory test = new();
        string zip = Path.Combine(test.Root, "good.zip");
        using (ZipArchive archive = ZipFile.Open(zip, ZipArchiveMode.Create))
        {
            ZipArchiveEntry entry = archive.CreateEntry("tool/bin/tool.exe");
            using StreamWriter writer = new(entry.Open());
            writer.Write("test");
        }
        string destination = Path.Combine(test.Root, "extract");
        SafePaths.ExtractVerifiedZip(zip, destination);
        Assert(File.ReadAllText(Path.Combine(destination, "tool", "bin", "tool.exe")) == "test");
    }

    private static void OwnedDeletionPreservesSibling()
    {
        using TestDirectory test = new();
        string parent = Path.Combine(test.Root, "parent");
        string target = Path.Combine(parent, "owned");
        string sibling = Path.Combine(parent, "sentinel.txt");
        Directory.CreateDirectory(Path.Combine(target, "nested"));
        File.WriteAllText(Path.Combine(target, "nested", "file.txt"), "owned");
        File.WriteAllText(sibling, "outside");
        SafePaths.DeleteOwnedTree(target, parent);
        Assert(!Directory.Exists(target));
        Assert(File.ReadAllText(sibling) == "outside");
    }

    private static void OwnedDeletionRejectsLinkedAncestor()
    {
        using TestDirectory test = new();
        string parent = Path.Combine(test.Root, "owned");
        string outside = Path.Combine(test.Root, "outside");
        string link = Path.Combine(parent, "redirected");
        Directory.CreateDirectory(parent);
        Directory.CreateDirectory(Path.Combine(outside, "victim"));
        string sentinel = Path.Combine(outside, "victim", "sentinel.txt");
        File.WriteAllText(sentinel, "preserve");
        CreateJunction(link, outside);
        Expect<InstallerException>(() => SafePaths.DeleteOwnedTree(
            Path.Combine(link, "victim"), parent));
        Assert(File.ReadAllText(sentinel) == "preserve");
        Directory.Delete(link);
    }

    private static void OwnershipRejectsCollision()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        Directory.CreateDirectory(paths.ProgramRoot);
        File.WriteAllText(Path.Combine(paths.ProgramRoot, "unrelated.txt"), "preserve");
        OwnershipManager ownership = new(paths);
        Expect<InstallerException>(() => ownership.EnsureRoots());
        Assert(File.Exists(Path.Combine(paths.ProgramRoot, "unrelated.txt")));
    }

    private static void OwnershipRecoversCompanionRoot()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        OwnershipManager ownership = new(paths);
        OwnerMarker marker = ownership.EnsureRoots();
        SafePaths.DeleteOwnedTree(paths.StateRoot, test.Root);
        OwnerMarker recovered = ownership.EnsureRoots();
        Assert(marker == recovered);
        ownership.ValidateBoth(marker.InstallationId);
    }

    private static void OwnershipRecoversEmptyRoot()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        Directory.CreateDirectory(paths.ProgramRoot);
        OwnershipManager ownership = new(paths);
        Assert(ownership.Inspect() is null);
        OwnerMarker marker = ownership.EnsureRoots();
        ownership.ValidateBoth(marker.InstallationId);
    }

    private static void OperationsOwnershipRecoversEmptyRoot()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        Directory.CreateDirectory(paths.OperationsRoot);
        Guid installationId = Guid.NewGuid();
        SelfCleanup.EnsureOperationsRoot(paths, installationId);
        OwnerMarker marker = JsonStore.Read<OwnerMarker>(paths.OperationsMarker);
        Assert(marker.InstallationId == installationId);
        Assert(Directory.Exists(paths.HelpersDirectory));
    }

    private static void PackageHashIsVerified()
    {
        using TestDirectory test = new();
        string package = Path.Combine(test.Root, "package");
        WritePackageFixture(package);
        string executable = Path.Combine(package, "bin", "uvsr.exe");
        string hash = PayloadPackager.ComputeSha256(executable);
        PackageManifest manifest = new(ProductConstants.SchemaVersion, Guid.NewGuid(),
            "0123456789abcdef0123456789abcdef01234567-20260817123456-89abcdef",
            "0123456789abcdef0123456789abcdef01234567", hash,
            BuildPackageFiles(package), DateTimeOffset.UtcNow);
        PayloadPackager.ValidatePackage(package, manifest);
        File.AppendAllText(executable, "tamper");
        Expect<InstallerException>(() => PayloadPackager.ValidatePackage(package, manifest));
    }

    private static void PackageInventoryIsVerified()
    {
        using TestDirectory test = new();
        string package = Path.Combine(test.Root, "package");
        WritePackageFixture(package);
        string executable = Path.Combine(package, "bin", "uvsr.exe");
        PackageManifest manifest = new(ProductConstants.SchemaVersion, Guid.NewGuid(),
            "0123456789abcdef0123456789abcdef01234567-20260817123456-89abcdef",
            "0123456789abcdef0123456789abcdef01234567",
            PayloadPackager.ComputeSha256(executable), BuildPackageFiles(package),
            DateTimeOffset.UtcNow);
        File.Delete(Path.Combine(package, "bin", "D3D12", "D3D12SDKLayers.dll"));
        Expect<InstallerException>(() => PayloadPackager.ValidatePackage(package, manifest));
    }

    private static void PackageNullInventoryIsRejected()
    {
        using TestDirectory test = new();
        string package = Path.Combine(test.Root, "package");
        WritePackageFixture(package);
        string executable = Path.Combine(package, "bin", "uvsr.exe");
        PackageManifest manifest = new(ProductConstants.SchemaVersion, Guid.NewGuid(),
            "0123456789abcdef0123456789abcdef01234567-20260817123456-89abcdef",
            "0123456789abcdef0123456789abcdef01234567",
            PayloadPackager.ComputeSha256(executable), null!, DateTimeOffset.UtcNow);
        Expect<InstallerException>(() => PayloadPackager.ValidatePackage(package, manifest));
        manifest = manifest with { Files = new PackageFile[] { null! } };
        Expect<InstallerException>(() => PayloadPackager.ValidatePackage(package, manifest));
    }

    private static void D3D12RuntimeContractIsBound()
    {
        using TestDirectory test = new();
        string hashMismatch = Path.Combine(test.Root, "hash-mismatch");
        WritePackageFixture(hashMismatch);
        File.AppendAllText(Path.Combine(hashMismatch, "bin", "D3D12",
            "D3D12Core.dll"), "changed");
        Expect<InstallerException>(() =>
            PayloadPackager.ValidateD3D12RuntimeContract(hashMismatch));

        string versionMismatch = Path.Combine(test.Root, "version-mismatch");
        WritePackageFixture(versionMismatch);
        string contract = Path.Combine(versionMismatch, "bin", "D3D12",
            "uvsr-runtime-contract.txt");
        File.WriteAllText(contract, File.ReadAllText(contract).Replace(
            $"sdkVersion={ProductConstants.D3D12AgilitySdkVersion}",
            "sdkVersion=1", StringComparison.Ordinal));
        Expect<InstallerException>(() =>
            PayloadPackager.ValidateD3D12RuntimeContract(versionMismatch));
    }

    private static void ToolMetadataIsPinned()
    {
        foreach (ToolPackage package in new[]
                 { ProductConstants.Git, ProductConstants.CMake, ProductConstants.Python })
        {
            Assert(package.DownloadUri.Scheme == Uri.UriSchemeHttps);
            Assert(ProductConstants.HashRegex().IsMatch(package.Sha256));
            Assert(package.MaximumBytes > 0);
            Assert(!string.IsNullOrWhiteSpace(package.ExpectedVersionOutput));
            Assert(!Path.IsPathRooted(package.ExecutableRelativePath));
        }
        Assert(ProductConstants.VisualStudioBuildTools.Scheme == Uri.UriSchemeHttps);
        Assert(ProductConstants.Git.ExpectedVersionOutput == "2.55.0.windows.4");
    }

    private static void BuildDependenciesArePinnedAndOffline()
    {
        foreach (PinnedArchivePackage package in ProductConstants.BuildDependencies)
        {
            Assert(package.DownloadUri.Scheme == Uri.UriSchemeHttps);
            Assert(ProductConstants.HashRegex().IsMatch(package.Sha256));
            Assert(package.MaximumBytes > 0);
            Assert(package.RequiredRelativePaths.Count > 0);
            Assert(package.RequiredRelativePaths.All(path => !Path.IsPathRooted(path)));
        }
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "Programs"));
        ToolPaths tools = new("git.exe", "cmake.exe", "python.exe", "vs",
            Path.Combine(test.Root, "agility"), Path.Combine(test.Root, "headers"),
            Path.Combine(test.Root, "dxc"));
        string[] arguments = SourceManager.BuildConfigureArguments(paths, tools);
        Assert(arguments.Contains("-DFETCHCONTENT_FULLY_DISCONNECTED=ON"));
        Assert(arguments.Contains("-DFETCHCONTENT_UPDATES_DISCONNECTED=ON"));
        Assert(arguments.Contains($"-DFETCHCONTENT_SOURCE_DIR_D3D_AGILITY_SDK={tools.AgilitySdk}"));
        Assert(arguments.Contains($"-DDONUT_D3D_AGILITY_SDK_FETCH_DIR={tools.AgilitySdk}"));
        Assert(arguments.Contains($"-DFETCHCONTENT_SOURCE_DIR_DIRECTX_HEADERS={tools.DirectXHeaders}"));
        Assert(arguments.Contains($"-DFETCHCONTENT_SOURCE_DIR_DXC={tools.Dxc}"));
    }

    private static void RendererBuildContractIsStrict()
    {
        string path = Path.Combine(RepositoryRoot(),
            ProductConstants.RendererBuildContractRelativePath.Replace('/',
                Path.DirectorySeparatorChar));
        byte[] valid = File.ReadAllBytes(path);
        RendererBuildContract contract = SourceManager.ParseRendererBuildContract(valid);
        string commit = new('a', 40);
        SourceManager.ValidateSupportedRendererBuildContract(contract, commit);
        Assert(contract.ContractId == ProductConstants.RendererBuildContractId);
        Assert(contract.MinimumLauncherReleaseSequence <=
               ProductConstants.LauncherReleaseSequence);
        Assert(contract.D3D12AgilitySdkVersion == ProductConstants.AgilitySdk.Version);
        Assert(contract.DirectXHeadersVersion == ProductConstants.DirectXHeaders.Version);
        Assert(contract.DxcVersion == ProductConstants.Dxc.Version);
        Assert(contract.DxcDate == ProductConstants.PinnedDxcDate);
        Assert(SourceManager.BuildRendererBuildContractUri(commit).AbsoluteUri ==
               $"{ProductConstants.RepositoryRawUrl}/{commit}/" +
               ProductConstants.RendererBuildContractRelativePath);

        string json = Encoding.UTF8.GetString(valid);
        Expect<InstallerException>(() => SourceManager.ParseRendererBuildContract(
            Encoding.UTF8.GetBytes(json.Replace("\"schemaVersion\": 1",
                "\"schemaVersion\": 1,\n  \"schemaVersion\": 1"))));
        Expect<InstallerException>(() => SourceManager.ParseRendererBuildContract(
            Encoding.UTF8.GetBytes(json.Replace("\"contractId\":",
                "\"unknown\": true,\n  \"contractId\":"))));
        Expect<InstallerException>(() => SourceManager.ParseRendererBuildContract(
            Encoding.UTF8.GetBytes(json.Replace(
                $"\"contractId\": \"{ProductConstants.RendererBuildContractId}\"",
                "\"contractId\": null"))));
        Expect<InstallerException>(() => SourceManager.ParseRendererBuildContract(
            new byte[ProductConstants.MaximumRendererBuildContractBytes + 1]));

        Expect<SourceLauncherCompatibilityException>(() =>
            SourceManager.ValidateSupportedRendererBuildContract(
                contract with { ContractId = "unknown-contract" }, commit));
        Expect<SourceLauncherCompatibilityException>(() =>
            SourceManager.ValidateSupportedRendererBuildContract(
                contract with
                {
                    MinimumLauncherReleaseSequence =
                        ProductConstants.LauncherReleaseSequence + 1
                }, commit));
        Expect<SourceLauncherCompatibilityException>(() =>
            SourceManager.ValidateSupportedRendererBuildContract(
                contract with { DirectXHeadersVersion = "1.0.0" }, commit));
    }

    private static void RendererSourceBridgeIsExact()
    {
        RendererSourceBridge bridge = RendererSourceBridgeRegistry.FindForPublicBase(
            RendererSourceBridge.PublicBaseCommit)
            ?? throw new InvalidOperationException("Expected the exact renderer bridge.");
        Assert(RendererSourceBridgeRegistry.FindForPublicBase(new string('a', 40)) is null);
        Assert(RendererSourceBridgeRegistry.MapSourceToPublicBase(
                   RendererSourceBridge.SourceCommit) ==
               RendererSourceBridge.PublicBaseCommit);
        string ordinary = new string('b', 40);
        Assert(RendererSourceBridgeRegistry.MapSourceToPublicBase(ordinary) == ordinary);
        byte[] patch = bridge.LoadVerifiedPatch();
        Assert(patch.LongLength == RendererSourceBridge.PatchSize);
        Assert(Hash(patch) == RendererSourceBridge.PatchSha256);
        Assert(bridge.StagedBlobs.Count == 10);
        Assert(bridge.Contract.ContractId == ProductConstants.RendererBuildContractId);
        Assert(bridge.Contract.MinimumLauncherReleaseSequence == 4);
        Assert(bridge.Contract.DirectXHeadersVersion ==
               ProductConstants.DirectXHeaders.Version);
    }

    private static void RendererSourceBridgeRejectsMutation()
    {
        RendererSourceBridge bridge = RendererSourceBridge.Instance;
        string validEntries = string.Join("\n", bridge.StagedBlobs.Select(entry =>
            $"100644 {entry.Value} 0\t{entry.Key}")) + "\n";
        bridge.ValidateStagedEntries(validEntries);
        Expect<InstallerException>(() => bridge.ValidateStagedEntries(
            validEntries.Replace(bridge.StagedBlobs.First().Value,
                new string('f', 40), StringComparison.Ordinal)));
        Expect<InstallerException>(() => bridge.ValidateStagedEntries(
            validEntries + $"100644 {new string('a', 40)} 0\tsrc/extra.cpp\n"));

        string validParents = $"{RendererSourceBridge.SourceCommit} " +
                              RendererSourceBridge.PublicBaseCommit;
        bridge.ValidatePreparedIdentityValues(RendererSourceBridge.SourceCommit,
            RendererSourceBridge.SourceTree, validParents, string.Empty);
        Expect<InstallerException>(() => bridge.ValidatePreparedIdentityValues(
            new string('a', 40), RendererSourceBridge.SourceTree, validParents,
            string.Empty));
        Expect<InstallerException>(() => bridge.ValidatePreparedIdentityValues(
            RendererSourceBridge.SourceCommit, new string('a', 40), validParents,
            string.Empty));
        Expect<InstallerException>(() => bridge.ValidatePreparedIdentityValues(
            RendererSourceBridge.SourceCommit, RendererSourceBridge.SourceTree,
            $"{RendererSourceBridge.SourceCommit} {new string('a', 40)}",
            string.Empty));
        Expect<InstallerException>(() => bridge.ValidatePreparedIdentityValues(
            RendererSourceBridge.SourceCommit, RendererSourceBridge.SourceTree,
            validParents, " M src/uvsr.cpp"));
    }

    private static void RendererSourceBridgeGitApplicationWorks()
    {
        using TestDirectory test = new();
        string source = Path.Combine(test.Root, "source");
        RunGit(RepositoryRoot(), null, "clone", "--no-checkout", "--no-hardlinks",
            "--local", RepositoryRoot(), source);
        RunGit(source, null, "checkout", "--detach", "--force",
            RendererSourceBridge.PublicBaseCommit);
        Assert(RunGit(source, null, "rev-parse", "HEAD^{tree}").Trim() ==
               RendererSourceBridge.PublicBaseTree);

        string patchPath = Path.Combine(source, ".git", "bridge.patch");
        File.WriteAllBytes(patchPath,
            RendererSourceBridge.Instance.LoadVerifiedPatch());
        using (FileStream bridgeLock = new(patchPath, FileMode.Open,
                   FileAccess.Read, FileShare.Read))
        {
            RunGit(source, null, "apply", "--check", "--index", "--binary",
                patchPath);
            RunGit(source, null, "apply", "--index", "--binary", patchPath);
        }
        List<string> stagedEntryArguments = new() { "ls-files", "--stage", "--" };
        stagedEntryArguments.AddRange(RendererSourceBridge.Instance.StagedBlobs.Keys);
        RendererSourceBridge.Instance.ValidateStagedEntries(
            RunGit(source, null, stagedEntryArguments.ToArray()));
        Assert(RunGit(source, null, "write-tree").Trim() ==
               RendererSourceBridge.SourceTree);

        Dictionary<string, string> identity = new(StringComparer.Ordinal)
        {
            ["GIT_AUTHOR_NAME"] = RendererSourceBridge.CommitIdentityName,
            ["GIT_AUTHOR_EMAIL"] = RendererSourceBridge.CommitIdentityEmail,
            ["GIT_AUTHOR_DATE"] = RendererSourceBridge.CommitIdentityDate,
            ["GIT_COMMITTER_NAME"] = RendererSourceBridge.CommitIdentityName,
            ["GIT_COMMITTER_EMAIL"] = RendererSourceBridge.CommitIdentityEmail,
            ["GIT_COMMITTER_DATE"] = RendererSourceBridge.CommitIdentityDate
        };
        string commit = RunGit(source, identity, "commit-tree",
            RendererSourceBridge.SourceTree, "-p",
            RendererSourceBridge.PublicBaseCommit, "-m",
            RendererSourceBridge.CommitMessage).Trim();
        Assert(commit == RendererSourceBridge.SourceCommit);
        RunGit(source, null, "reset", "--hard", commit);
        Assert(string.IsNullOrWhiteSpace(RunGit(source, null, "status",
            "--porcelain=v1", "--untracked-files=all")));
        Assert(RunGit(source, null, "rev-list", "--parents", "-n", "1",
                   "HEAD").Trim() ==
               $"{RendererSourceBridge.SourceCommit} " +
               RendererSourceBridge.PublicBaseCommit);
    }

    private static void SequenceFourLauncherStateIsReadable()
    {
        Guid installationId = Guid.NewGuid();
        LauncherState sequenceFour = new(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, installationId, 4, "1.1.3",
            new string('a', 64), true, DateTimeOffset.UtcNow);
        byte[] json = JsonSerializer.SerializeToUtf8Bytes(sequenceFour,
            JsonStore.Options);
        LauncherState roundTrip = JsonSerializer.Deserialize<LauncherState>(json,
            JsonStore.Options) ?? throw new InvalidOperationException(
                "Sequence-4 launcher state did not deserialize.");
        roundTrip.Validate(installationId);
        Assert(roundTrip.ReleaseSequence == 4 && roundTrip.Version == "1.1.3");

        string versionId = $"{RendererSourceBridge.SourceCommit}-20260820000000-12345678";
        InstallState bridgedState = new(ProductConstants.SchemaVersion,
            installationId, versionId, RendererSourceBridge.SourceCommit,
            new string('b', 64), true, DateTimeOffset.UtcNow);
        byte[] stateJson = JsonSerializer.SerializeToUtf8Bytes(bridgedState,
            JsonStore.Options);
        AssertJsonPropertySet(stateJson, "schemaVersion", "installationId",
            "activeVersionId", "commit", "executableSha256", "desktopShortcut",
            "installedUtc");
        InstallState strictState = JsonSerializer.Deserialize<InstallState>(stateJson,
            JsonStore.Options) ?? throw new InvalidOperationException(
                "Bridged renderer state did not deserialize.");
        strictState.Validate(installationId);
        Assert(strictState.Commit == RendererSourceBridge.SourceCommit);

        PackageManifest bridgedManifest = new(ProductConstants.SchemaVersion,
            installationId, versionId, RendererSourceBridge.SourceCommit,
            new string('b', 64), new[]
            {
                new PackageFile("bin/uvsr.exe", 1, new string('b', 64))
            }, DateTimeOffset.UtcNow);
        byte[] manifestJson = JsonSerializer.SerializeToUtf8Bytes(bridgedManifest,
            JsonStore.Options);
        AssertJsonPropertySet(manifestJson, "schemaVersion", "installationId",
            "versionId", "commit", "executableSha256", "files", "builtUtc");
        PackageManifest strictManifest =
            JsonSerializer.Deserialize<PackageManifest>(manifestJson,
                JsonStore.Options) ?? throw new InvalidOperationException(
                    "Bridged renderer manifest did not deserialize.");
        Assert(strictManifest.Commit == RendererSourceBridge.SourceCommit);
    }

    private static void VisualStudioLayoutContractIsBounded()
    {
        string layout = @"C:\UVSR-VS-Layout";
        IReadOnlyList<string> arguments =
            ToolchainManager.BuildVisualStudioLayoutArguments(layout);
        Assert(arguments.Contains("--layout"));
        Assert(arguments.Contains(layout));
        Assert(arguments.Contains("--lang"));
        Assert(arguments.Contains("en-US"));
        Assert(arguments.Contains("Microsoft.VisualStudio.Workload.VCTools"));
        Assert(arguments.Contains("Microsoft.VisualStudio.Component.VC.Tools.x86.x64"));
        Assert(arguments.Contains("Microsoft.VisualStudio.Component.Windows11SDK.26100"));
        Assert(arguments.Contains("Microsoft.VisualStudio.Component.VC.Redist.14.Latest"));
        Assert(!arguments.Contains("--noWeb"));
        Assert(ToolchainManager.IsRetryableVisualStudioLayoutExit(5003));
        Assert(ToolchainManager.IsRetryableVisualStudioLayoutExit(-1073720687));
        foreach (int exit in new[] { 0, 1602, 5004, 5005, 5007, 8005, -1 })
            Assert(!ToolchainManager.IsRetryableVisualStudioLayoutExit(exit));
    }

    private static void VisualStudioRecoveryIsExplicit()
    {
        string layout = Path.GetFullPath(@"C:\UVSR-VS-Layout");
        string bootstrapper = Path.GetFullPath(@"C:\UVSR-Downloads\vs_buildtools.exe");
        string setup = Path.Combine(layout, "vs_setup.exe");
        DateTimeOffset started = DateTimeOffset.UtcNow;
        string bootstrapperHash = new('a', 64);
        string setupHash = new('b', 64);
        VisualStudioOperationRecord layoutOperation = new("layout", bootstrapper,
            bootstrapperHash, 42, 123456, started);
        VisualStudioLayoutRecord layoutRecord = new(ProductConstants.SchemaVersion,
            ProductConstants.ProductId,
            "Microsoft.VisualStudio.Workload.VCTools;" +
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64;" +
            "Microsoft.VisualStudio.Component.Windows11SDK.26100;" +
            "Microsoft.VisualStudio.Component.VC.Redist.14.Latest;en-US",
            bootstrapperHash, "VisualStudio.17.Release", layout, "preparing",
            started, layoutOperation);
        Assert(ToolchainManager.IsValidVisualStudioLayoutRecord(
            layoutRecord, layout, bootstrapper));
        Assert(ToolchainManager.DecideVisualStudioRecovery(layoutRecord,
            TrackedProcessState.Running, false) ==
            VisualStudioRecoveryDecision.WaitForRunningOperation);
        Assert(ToolchainManager.DecideVisualStudioRecovery(layoutRecord,
            TrackedProcessState.Unverifiable, false) ==
            VisualStudioRecoveryDecision.FailClosed);
        Assert(ToolchainManager.DecideVisualStudioRecovery(layoutRecord,
            TrackedProcessState.NotRunning, false) ==
            VisualStudioRecoveryDecision.ResumeIdempotentOperation);
        Assert(ToolchainManager.DecideVisualStudioRecovery(layoutRecord,
            TrackedProcessState.NotRunning, true) ==
            VisualStudioRecoveryDecision.CleanupCompletedOperation);
        VisualStudioLayoutRecord resumed =
            ToolchainManager.NormalizeEndedIdempotentOperation(layoutRecord);
        Assert(resumed.Phase == "preparing" && resumed.ActiveOperation is null);

        VisualStudioOperationRecord installOperation = new("install", setup,
            setupHash, 84, 654321, started);
        VisualStudioLayoutRecord installRecord = layoutRecord with
        {
            Phase = "installing",
            ActiveOperation = installOperation
        };
        Assert(ToolchainManager.IsValidVisualStudioLayoutRecord(
            installRecord, layout, bootstrapper));
        Assert(ToolchainManager.DecideVisualStudioRecovery(installRecord,
            TrackedProcessState.NotRunning, false) ==
            VisualStudioRecoveryDecision.ConfirmInstallRetry);
        VisualStudioLayoutRecord authorized =
            ToolchainManager.AuthorizeInstallRetry(installRecord);
        Assert(authorized.Phase == "verified" && authorized.ActiveOperation is null);
        Assert(ProcessInspector.MatchesTrackedProcess(installOperation,
            84, setup, 654321));
        Assert(!ProcessInspector.MatchesTrackedProcess(installOperation,
            84, setup, 654322));

        Assert(!ToolchainManager.IsValidVisualStudioLayoutRecord(
            layoutRecord with
            {
                ActiveOperation = layoutOperation with { CreationTimeUtcFileTime = null }
            }, layout, bootstrapper));
        Assert(ToolchainManager.DecideVisualStudioRecovery(
            layoutRecord with { ActiveOperation = null },
            TrackedProcessState.NotRunning, false) == VisualStudioRecoveryDecision.None);
    }

    private static void DownloadPromotionWorks()
    {
        using TestDirectory test = new();
        byte[] payload = "verified download"u8.ToArray();
        string hash = Convert.ToHexString(SHA256.HashData(payload)).ToLowerInvariant();
        using DownloadManager downloads = new(new StaticResponseHandler(payload));
        string destination = Path.Combine(test.Root, "tool.zip");
        InstallLog log = new(Path.Combine(test.Root, "logs"));
        downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/tool.zip"),
                destination, hash, 1024, null, log, CancellationToken.None)
            .GetAwaiter().GetResult();
        Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
        Assert(!Directory.EnumerateFiles(test.Root, "*.part").Any());
    }

    private static void LoopbackHttpsResumeWorks()
    {
        using TestDirectory test = new();
        byte[] payload = "real sockets resume this truncated HTTPS body"u8.ToArray();
        const int split = 11;
        using RSA key = RSA.Create(2048);
        CertificateRequest request = new("CN=localhost", key,
            HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        request.CertificateExtensions.Add(new X509BasicConstraintsExtension(
            false, false, 0, false));
        using X509Certificate2 ephemeral = request.CreateSelfSigned(
            DateTimeOffset.UtcNow.AddMinutes(-1), DateTimeOffset.UtcNow.AddMinutes(10));
        const string certificatePassword = "uvsr-loopback-fixture";
        byte[] certificateBytes = ephemeral.Export(X509ContentType.Pfx,
            certificatePassword);
        using X509Certificate2 certificate = X509CertificateLoader.LoadPkcs12(
            certificateBytes, certificatePassword,
            X509KeyStorageFlags.UserKeySet | X509KeyStorageFlags.Exportable);
        TcpListener listener = new(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        Task server = ServeTruncatedHttpsAsync(listener, certificate, payload, split);
        SocketsHttpHandler sockets = new()
        {
            AllowAutoRedirect = false,
            AutomaticDecompression = DecompressionMethods.None,
            UseProxy = false,
            SslOptions = new SslClientAuthenticationOptions
            {
                RemoteCertificateValidationCallback = (_, _, _, _) => true
            }
        };
        string destination = Path.Combine(test.Root, "loopback.bin");
        try
        {
            DownloadPolicy loopbackPolicy = new(3, TimeSpan.FromSeconds(5),
                TimeSpan.FromSeconds(2), TimeSpan.FromSeconds(20),
                new[] { TimeSpan.Zero, TimeSpan.Zero });
            using DownloadManager downloads = new(sockets, loopbackPolicy);
            downloads.DownloadAndVerifyAsync(
                    new Uri($"https://localhost:{port}/payload.bin"), destination,
                    Hash(payload), 1024, null,
                    new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
                .GetAwaiter().GetResult();
            server.GetAwaiter().GetResult();
        }
        finally
        {
            listener.Stop();
        }
        Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
    }

    private static async Task ServeTruncatedHttpsAsync(
        TcpListener listener,
        X509Certificate2 certificate,
        byte[] payload,
        int split)
    {
        for (int attempt = 1; attempt <= 2; attempt++)
        {
            using TcpClient client = await listener.AcceptTcpClientAsync();
            await using SslStream stream = new(client.GetStream(), leaveInnerStreamOpen: false);
            await stream.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
            {
                ServerCertificate = certificate,
                EnabledSslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13
            });
            string headers = await ReadHttpHeadersAsync(stream);
            if (attempt == 1)
            {
                byte[] responseHeaders = Encoding.ASCII.GetBytes(
                    "HTTP/1.1 200 OK\r\n" +
                    $"Content-Length: {payload.Length}\r\n" +
                    "ETag: \"loopback-v1\"\r\nConnection: close\r\n\r\n");
                await stream.WriteAsync(responseHeaders);
                await stream.WriteAsync(payload.AsMemory(0, split));
                await stream.FlushAsync();
                continue;
            }
            Assert(headers.Contains($"Range: bytes={split}-", StringComparison.OrdinalIgnoreCase));
            Assert(headers.Contains("If-Range: \"loopback-v1\"", StringComparison.OrdinalIgnoreCase));
            byte[] partialHeaders = Encoding.ASCII.GetBytes(
                "HTTP/1.1 206 Partial Content\r\n" +
                $"Content-Length: {payload.Length - split}\r\n" +
                $"Content-Range: bytes {split}-{payload.Length - 1}/{payload.Length}\r\n" +
                "ETag: \"loopback-v1\"\r\nConnection: close\r\n\r\n");
            await stream.WriteAsync(partialHeaders);
            await stream.WriteAsync(payload.AsMemory(split));
            await stream.FlushAsync();
        }
    }

    private static async Task<string> ReadHttpHeadersAsync(Stream stream)
    {
        byte[] terminator = "\r\n\r\n"u8.ToArray();
        using MemoryStream bytes = new();
        byte[] one = new byte[1];
        int matched = 0;
        while (bytes.Length < 32 * 1024)
        {
            int read = await stream.ReadAsync(one);
            if (read == 0)
                break;
            bytes.WriteByte(one[0]);
            matched = one[0] == terminator[matched]
                ? matched + 1
                : one[0] == terminator[0] ? 1 : 0;
            if (matched == terminator.Length)
                return Encoding.ASCII.GetString(bytes.ToArray());
        }
        throw new InvalidOperationException("Loopback HTTPS request headers were incomplete.");
    }

    private static void DownloadRetriesTransientStatus()
    {
        using TestDirectory test = new();
        byte[] payload = "retry success"u8.ToArray();
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            if (call == 1)
                return Response(request, HttpStatusCode.ServiceUnavailable,
                    Array.Empty<byte>());
            return Response(request, HttpStatusCode.OK, payload);
        }), FastDownloadPolicy(maximumAttempts: 2));
        string destination = Path.Combine(test.Root, "retry.bin");
        List<InstallerProgress> reports = new();
        downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/retry.bin"),
                destination, Hash(payload), 1024, new InlineProgress(reports.Add),
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult();
        Assert(calls == 2);
        Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
        Assert(reports.Any(report => report.Phase.Contains("Connection", StringComparison.Ordinal)));
    }

    private static void DownloadResumesWithStrongValidator()
    {
        using TestDirectory test = new();
        byte[] payload = "validator-gated-resume"u8.ToArray();
        const int split = 7;
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            if (call == 1)
            {
                HttpResponseMessage first = new(HttpStatusCode.OK)
                {
                    RequestMessage = request,
                    Content = new StreamContent(new ThrowAfterStream(payload, split))
                };
                first.Content.Headers.ContentLength = payload.Length;
                first.Headers.ETag = new System.Net.Http.Headers.EntityTagHeaderValue("\"fixture-v1\"");
                return first;
            }
            Assert(request.Headers.Range?.Ranges.Single().From == split);
            Assert(request.Headers.IfRange?.EntityTag?.Tag == "\"fixture-v1\"");
            byte[] remainder = payload[split..];
            HttpResponseMessage resumed = Response(request, HttpStatusCode.PartialContent,
                remainder);
            resumed.Headers.ETag = new System.Net.Http.Headers.EntityTagHeaderValue("\"fixture-v1\"");
            resumed.Content.Headers.ContentRange =
                new System.Net.Http.Headers.ContentRangeHeaderValue(split,
                    payload.Length - 1, payload.Length);
            return resumed;
        }), FastDownloadPolicy(maximumAttempts: 2));
        string destination = Path.Combine(test.Root, "resume.bin");
        downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/resume.bin"),
                destination, Hash(payload), 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult();
        Assert(calls == 2);
        Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
        Assert(!File.Exists(destination + ".part"));
        Assert(!File.Exists(destination + ".part.json"));
    }

    private static void DownloadStallIsRetried()
    {
        using TestDirectory test = new();
        byte[] payload = "after stall"u8.ToArray();
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            if (call == 1)
            {
                HttpResponseMessage stalled = new(HttpStatusCode.OK)
                {
                    RequestMessage = request,
                    Content = new StreamContent(new StallingStream())
                };
                stalled.Content.Headers.ContentLength = payload.Length;
                stalled.Headers.ETag = new System.Net.Http.Headers.EntityTagHeaderValue("\"stall-v1\"");
                return stalled;
            }
            return Response(request, HttpStatusCode.OK, payload);
        }), new DownloadPolicy(2, TimeSpan.FromSeconds(1),
            TimeSpan.FromMilliseconds(75), TimeSpan.FromSeconds(5),
            new[] { TimeSpan.Zero }));
        List<InstallerProgress> reports = new();
        string destination = Path.Combine(test.Root, "stall.bin");
        downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/stall.bin"),
                destination, Hash(payload), 1024, new InlineProgress(reports.Add),
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult();
        Assert(calls == 2);
        Assert(reports.Any(report => report.Phase == "Connection stalled"));
        Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
    }

    private static void PermanentDownloadFailureDoesNotRetry()
    {
        using TestDirectory test = new();
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            return Response(request, HttpStatusCode.NotFound, Array.Empty<byte>());
        }), FastDownloadPolicy(maximumAttempts: 3));
        string destination = Path.Combine(test.Root, "missing.bin");
        Expect<InstallerException>(() => downloads.DownloadAndVerifyAsync(
                new Uri("https://example.invalid/missing.bin"), destination, null,
                1024, null, new InstallLog(Path.Combine(test.Root, "logs")),
                CancellationToken.None).GetAwaiter().GetResult());
        Assert(calls == 1);
        Assert(!File.Exists(destination));
    }

    private static void RetryableHttpStatusMatrixRecovers()
    {
        HttpStatusCode[] statuses =
        {
            HttpStatusCode.RequestTimeout, (HttpStatusCode)425, (HttpStatusCode)429,
            HttpStatusCode.InternalServerError, HttpStatusCode.BadGateway,
            HttpStatusCode.ServiceUnavailable, HttpStatusCode.GatewayTimeout
        };
        foreach (HttpStatusCode status in statuses)
        {
            using TestDirectory test = new();
            byte[] payload = System.Text.Encoding.UTF8.GetBytes($"recovered-{(int)status}");
            int calls = 0;
            using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
            {
                calls = call;
                return call == 1
                    ? Response(request, status, Array.Empty<byte>())
                    : Response(request, HttpStatusCode.OK, payload);
            }), FastDownloadPolicy(2));
            string destination = Path.Combine(test.Root, $"{(int)status}.bin");
            downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/status.bin"),
                    destination, Hash(payload), 1024, null,
                    new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
                .GetAwaiter().GetResult();
            Assert(calls == 2);
            Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
        }
    }

    private static void TransportFailureClassificationIsNarrow()
    {
        using (TestDirectory test = new())
        {
            byte[] payload = "dns recovered"u8.ToArray();
            int calls = 0;
            using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
            {
                calls = call;
                if (call == 1)
                    throw new HttpRequestException("fixture DNS failure",
                        new SocketException((int)SocketError.HostNotFound));
                return Response(request, HttpStatusCode.OK, payload);
            }), FastDownloadPolicy(2));
            downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/dns.bin"),
                    Path.Combine(test.Root, "dns.bin"), Hash(payload), 1024, null,
                    new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
                .GetAwaiter().GetResult();
            Assert(calls == 2);
        }
        using (TestDirectory test = new())
        {
            int calls = 0;
            using DownloadManager downloads = new(new SequenceResponseHandler((_, call) =>
            {
                calls = call;
                throw new HttpRequestException("fixture TLS failure",
                    new AuthenticationException("untrusted fixture"));
            }), FastDownloadPolicy(3));
            Expect<InstallerException>(() => downloads.DownloadAndVerifyAsync(
                    new Uri("https://example.invalid/tls.bin"),
                    Path.Combine(test.Root, "tls.bin"), null, 1024, null,
                    new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
                .GetAwaiter().GetResult());
            Assert(calls == 1);
        }
        using (TestDirectory test = new())
        {
            byte[] payload = "TLS transport recovered"u8.ToArray();
            int calls = 0;
            using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
            {
                calls = call;
                if (call == 1)
                {
                    throw new HttpRequestException("fixture interrupted TLS handshake",
                        new AuthenticationException("The TLS handshake was interrupted.",
                            new IOException(
                                "Received an unexpected EOF or 0 bytes from the transport stream.")));
                }
                return Response(request, HttpStatusCode.OK, payload);
            }), FastDownloadPolicy(2));
            string destination = Path.Combine(test.Root, "tls-transport.bin");
            downloads.DownloadAndVerifyAsync(
                    new Uri("https://example.invalid/tls-transport.bin"), destination,
                    Hash(payload), 1024, null,
                    new InstallLog(Path.Combine(test.Root, "logs")),
                    CancellationToken.None)
                .GetAwaiter().GetResult();
            Assert(calls == 2);
            Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
        }
    }

    private static void IgnoredRangeRestartsSafely()
    {
        using TestDirectory test = new();
        byte[] payload = "server ignored range"u8.ToArray();
        const int split = 6;
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            if (call == 1)
                return InterruptibleResponse(request, payload, split, "\"range-v1\"");
            Assert(request.Headers.Range?.Ranges.Single().From == split);
            return Response(request, HttpStatusCode.OK, payload);
        }), FastDownloadPolicy(2));
        string destination = Path.Combine(test.Root, "range.bin");
        downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/range.bin"),
                destination, Hash(payload), 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult();
        Assert(calls == 2);
        Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
    }

    private static void ChangedValidatorRestartsSafely()
    {
        using TestDirectory test = new();
        byte[] payload = "changed validator restart"u8.ToArray();
        const int split = 5;
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            if (call == 1)
                return InterruptibleResponse(request, payload, split, "\"entity-v1\"");
            if (call == 2)
            {
                HttpResponseMessage changed = ResponseWithoutLength(request,
                    HttpStatusCode.PartialContent, payload[split..]);
                changed.Headers.ETag = new System.Net.Http.Headers.EntityTagHeaderValue("\"entity-v2\"");
                changed.Content.Headers.ContentRange = new System.Net.Http.Headers.ContentRangeHeaderValue(
                    split, payload.Length - 1, payload.Length);
                return changed;
            }
            Assert(request.Headers.Range is null);
            return Response(request, HttpStatusCode.OK, payload);
        }), FastDownloadPolicy(3));
        string destination = Path.Combine(test.Root, "changed.bin");
        downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/changed.bin"),
                destination, Hash(payload), 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult();
        Assert(calls == 3);
        Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
    }

    private static void MissingRepeatedValidatorResumes()
    {
        using TestDirectory test = new();
        byte[] payload = "missing repeated validator"u8.ToArray();
        const int split = 8;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            if (call == 1)
                return InterruptibleResponse(request, payload, split, "\"entity-v1\"");
            HttpResponseMessage resumed = Response(request, HttpStatusCode.PartialContent,
                payload[split..]);
            resumed.Content.Headers.ContentRange = new System.Net.Http.Headers.ContentRangeHeaderValue(
                split, payload.Length - 1, payload.Length);
            return resumed;
        }), FastDownloadPolicy(2));
        string destination = Path.Combine(test.Root, "missing-etag.bin");
        downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/missing-etag.bin"),
                destination, Hash(payload), 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult();
        Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
    }

    private static void RangeNotSatisfiableFinalizesCompletePartial()
    {
        using TestDirectory test = new();
        byte[] payload = "complete before reset"u8.ToArray();
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            if (call == 1)
                return InterruptibleResponse(request, payload, payload.Length, "\"complete-v1\"");
            HttpResponseMessage complete = Response(request,
                HttpStatusCode.RequestedRangeNotSatisfiable, Array.Empty<byte>());
            complete.Content.Headers.ContentRange =
                new System.Net.Http.Headers.ContentRangeHeaderValue(payload.Length);
            return complete;
        }), FastDownloadPolicy(2));
        string destination = Path.Combine(test.Root, "complete.bin");
        downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/complete.bin"),
                destination, Hash(payload), 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult();
        Assert(File.ReadAllBytes(destination).SequenceEqual(payload));
    }

    private static void HashlessRangeNotSatisfiableRestarts()
    {
        using TestDirectory test = new();
        byte[] stale = "stale same length"u8.ToArray();
        byte[] current = "fresh same data!!"u8.ToArray();
        Assert(stale.Length == current.Length);
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            if (call == 1)
                return InterruptibleResponse(request, stale, stale.Length, "\"stale-v1\"");
            if (call == 2)
            {
                HttpResponseMessage mismatch = Response(request,
                    HttpStatusCode.RequestedRangeNotSatisfiable, Array.Empty<byte>());
                mismatch.Content.Headers.ContentRange =
                    new System.Net.Http.Headers.ContentRangeHeaderValue(stale.Length);
                return mismatch;
            }
            return Response(request, HttpStatusCode.OK, current);
        }), FastDownloadPolicy(3));
        string destination = Path.Combine(test.Root, "hashless-416.bin");
        downloads.DownloadAndVerifyAsync(
                new Uri("https://example.invalid/hashless-416.bin"),
                destination, null, 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult();
        Assert(calls == 3);
        Assert(File.ReadAllBytes(destination).SequenceEqual(current));
    }

    private static void PartialRangeCannotOverflow()
    {
        using TestDirectory test = new();
        byte[] payload = "overflow range body"u8.ToArray();
        const int split = 4;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            if (call == 1)
                return InterruptibleResponse(request, payload, split, "\"overflow-v1\"");
            byte[] extra = payload[split..].Concat(new byte[] { 0x42 }).ToArray();
            HttpResponseMessage resumed = ResponseWithoutLength(request,
                HttpStatusCode.PartialContent, extra);
            resumed.Headers.ETag = new System.Net.Http.Headers.EntityTagHeaderValue("\"overflow-v1\"");
            resumed.Content.Headers.ContentRange = new System.Net.Http.Headers.ContentRangeHeaderValue(
                split, payload.Length - 1, payload.Length);
            return resumed;
        }), FastDownloadPolicy(2));
        Expect<InstallerException>(() => downloads.DownloadAndVerifyAsync(
                new Uri("https://example.invalid/overflow.bin"),
                Path.Combine(test.Root, "overflow.bin"), Hash(payload), 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult());
        Assert(!File.Exists(Path.Combine(test.Root, "overflow.bin")));
    }

    private static void ShortPartialRangeIsRetriedSafely()
    {
        using TestDirectory test = new();
        byte[] payload = "short range body"u8.ToArray();
        const int split = 4;
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            if (call == 1)
                return InterruptibleResponse(request, payload, split, "\"short-v1\"");
            HttpResponseMessage resumed = ResponseWithoutLength(request,
                HttpStatusCode.PartialContent, payload.AsSpan(split, 1).ToArray());
            resumed.Headers.ETag = new System.Net.Http.Headers.EntityTagHeaderValue("\"short-v1\"");
            resumed.Content.Headers.ContentRange = new System.Net.Http.Headers.ContentRangeHeaderValue(
                split, payload.Length - 1, payload.Length);
            return resumed;
        }), FastDownloadPolicy(2));
        Expect<InstallerException>(() => downloads.DownloadAndVerifyAsync(
                new Uri("https://example.invalid/short.bin"),
                Path.Combine(test.Root, "short.bin"), Hash(payload), 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult());
        Assert(calls == 2);
    }

    private static void HttpsRedirectsAreBounded()
    {
        using TestDirectory test = new();
        byte[] payload = "secure redirect"u8.ToArray();
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            if (call == 1)
            {
                HttpResponseMessage redirect = Response(request, HttpStatusCode.Redirect,
                    Array.Empty<byte>());
                redirect.Headers.Location = new Uri("https://cdn.example.invalid/file.bin");
                return redirect;
            }
            Assert(request.RequestUri?.Host == "cdn.example.invalid");
            return Response(request, HttpStatusCode.OK, payload);
        }), FastDownloadPolicy(1));
        string destination = Path.Combine(test.Root, "redirect.bin");
        downloads.DownloadAndVerifyAsync(new Uri("https://example.invalid/file.bin"),
                destination, Hash(payload), 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult();
        Assert(calls == 2);
    }

    private static void RedirectDowngradeIsRejected()
    {
        using TestDirectory test = new();
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            HttpResponseMessage redirect = Response(request, HttpStatusCode.Redirect,
                Array.Empty<byte>());
            redirect.Headers.Location = new Uri("http://example.invalid/insecure.bin");
            return redirect;
        }), FastDownloadPolicy(3));
        Expect<InstallerException>(() => downloads.DownloadAndVerifyAsync(
                new Uri("https://example.invalid/file.bin"),
                Path.Combine(test.Root, "file.bin"), null, 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult());
        Assert(calls == 1);
    }

    private static void RedirectLoopsStop()
    {
        using TestDirectory test = new();
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            HttpResponseMessage redirect = Response(request, HttpStatusCode.Redirect,
                Array.Empty<byte>());
            redirect.Headers.Location = new Uri("https://example.invalid/loop.bin");
            return redirect;
        }), FastDownloadPolicy(1));
        Expect<InstallerException>(() => downloads.DownloadAndVerifyAsync(
                new Uri("https://example.invalid/loop.bin"),
                Path.Combine(test.Root, "loop.bin"), null, 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult());
        Assert(calls == 9);
    }

    private static void DownloadBackoffIsCancellable()
    {
        using TestDirectory test = new();
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            return Response(request, HttpStatusCode.ServiceUnavailable, Array.Empty<byte>());
        }), new DownloadPolicy(3, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(1),
            TimeSpan.FromSeconds(30), new[] { TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(5) }));
        using CancellationTokenSource cancellation = new(TimeSpan.FromMilliseconds(100));
        Expect<OperationCanceledException>(() => downloads.DownloadAndVerifyAsync(
                new Uri("https://example.invalid/backoff.bin"),
                Path.Combine(test.Root, "backoff.bin"), null, 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), cancellation.Token)
            .GetAwaiter().GetResult());
        Assert(calls == 1);
    }

    private static void UnknownLengthDownloadObeysLimit()
    {
        using TestDirectory test = new();
        byte[] payload = Enumerable.Repeat((byte)0x5a, 32).ToArray();
        using DownloadManager downloads = new(new SequenceResponseHandler((request, _) =>
            ResponseWithoutLength(request, HttpStatusCode.OK, payload)),
            FastDownloadPolicy(1));
        Expect<InstallerException>(() => downloads.DownloadAndVerifyAsync(
                new Uri("https://example.invalid/large.bin"),
                Path.Combine(test.Root, "large.bin"), null, 16, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult());
    }

    private static void FailedDownloadPreservesDestination()
    {
        using TestDirectory test = new();
        string destination = Path.Combine(test.Root, "existing.bin");
        byte[] original = "working version"u8.ToArray();
        byte[] replacement = "corrupt replacement"u8.ToArray();
        File.WriteAllBytes(destination, original);
        using DownloadManager downloads = new(new StaticResponseHandler(replacement),
            FastDownloadPolicy(1));
        Expect<InstallerException>(() => downloads.DownloadAndVerifyAsync(
                new Uri("https://example.invalid/existing.bin"), destination,
                Hash("different bytes"u8.ToArray()), 1024, null,
                new InstallLog(Path.Combine(test.Root, "logs")), CancellationToken.None)
            .GetAwaiter().GetResult());
        Assert(File.ReadAllBytes(destination).SequenceEqual(original));
    }

    private static void LocalDownloadFailureIsNotRetried()
    {
        using TestDirectory test = new();
        string destination = Path.Combine(test.Root, "blocked.bin");
        Directory.CreateDirectory(destination + ".part");
        int calls = 0;
        using DownloadManager downloads = new(new SequenceResponseHandler((request, call) =>
        {
            calls = call;
            return Response(request, HttpStatusCode.OK, "payload"u8.ToArray());
        }), FastDownloadPolicy(3));
        Expect<InstallerException>(() => downloads.DownloadAndVerifyAsync(
                new Uri("https://example.invalid/blocked.bin"), destination, null,
                1024, null, new InstallLog(Path.Combine(test.Root, "logs")),
                CancellationToken.None).GetAwaiter().GetResult());
        Assert(calls == 1);
    }

    private static void PreCancelledProcessDoesNotStart()
    {
        using TestDirectory test = new();
        string marker = Path.Combine(test.Root, "started.txt");
        string powershell = Path.Combine(Environment.GetFolderPath(
            Environment.SpecialFolder.Windows), "System32", "WindowsPowerShell",
            "v1.0", "powershell.exe");
        InstallLog log = new(Path.Combine(test.Root, "logs"));
        using CancellationTokenSource cancellation = new();
        cancellation.Cancel();
        Expect<OperationCanceledException>(() => new ProcessRunner().RunAsync(
                powershell,
                new[] { "-NoProfile", "-NonInteractive", "-Command",
                    $"Set-Content -LiteralPath '{marker.Replace("'", "''")}' -Value started" },
                test.Root, null, log, cancellation.Token)
            .GetAwaiter().GetResult());
        Thread.Sleep(200);
        Assert(!File.Exists(marker));
    }

    private static void CancelledProcessJobContainsDescendants()
    {
        using TestDirectory test = new();
        string escaped = Path.Combine(test.Root, "escaped.txt");
        string started = Path.Combine(test.Root, "started.txt");
        string appHost = Path.ChangeExtension(typeof(Program).Assembly.Location, ".exe");
        Assert(File.Exists(appHost));
        InstallLog log = new(Path.Combine(test.Root, "logs"));
        using CancellationTokenSource cancellation = new();
        Task<ProcessResult> running = new ProcessRunner().RunAsync(appHost,
            new[] { "--job-parent", escaped, started }, test.Root, null, log,
            cancellation.Token);
        DateTime deadline = DateTime.UtcNow.AddSeconds(5);
        while (!File.Exists(started) && DateTime.UtcNow < deadline)
            Thread.Sleep(25);
        Assert(File.Exists(started));
        cancellation.Cancel();
        Expect<OperationCanceledException>(() => running.GetAwaiter().GetResult());
        Thread.Sleep(2000);
        Assert(!File.Exists(escaped));
    }

    private static void DirectParentExitContainsDescendant()
    {
        using TestDirectory test = new();
        string escaped = Path.Combine(test.Root, "detached-escaped.txt");
        string started = Path.Combine(test.Root, "detached-started.txt");
        string appHost = Path.ChangeExtension(typeof(Program).Assembly.Location, ".exe");
        Assert(File.Exists(appHost));
        InstallLog log = new(Path.Combine(test.Root, "logs"));
        Task<ProcessResult> running = new ProcessRunner().RunAsync(appHost,
            new[] { "--job-detached-parent", escaped, started }, test.Root,
            null, log, CancellationToken.None);
        DateTime deadline = DateTime.UtcNow.AddSeconds(5);
        while (!File.Exists(started) && DateTime.UtcNow < deadline)
            Thread.Sleep(25);
        Assert(File.Exists(started));
        ProcessResult result = running.GetAwaiter().GetResult();
        Assert(result.ExitCode == 0);
        Thread.Sleep(2000);
        Assert(!File.Exists(escaped));
    }

    private static void GitEnvironmentIsIsolated()
    {
        string? oldGitDirectory = Environment.GetEnvironmentVariable("GIT_DIR");
        string? oldGitWorkTree = Environment.GetEnvironmentVariable("GIT_WORK_TREE");
        string? oldGitConfigCount = Environment.GetEnvironmentVariable("GIT_CONFIG_COUNT");
        try
        {
            Environment.SetEnvironmentVariable("GIT_DIR", @"C:\outside\.git");
            Environment.SetEnvironmentVariable("GIT_WORK_TREE", @"C:\outside");
            Environment.SetEnvironmentVariable("GIT_CONFIG_COUNT", "1");
            ToolPaths tools = new(@"C:\tools\git\cmd\git.exe",
                @"C:\tools\cmake\bin\cmake.exe", @"C:\tools\python\python.exe",
                @"C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
                @"C:\tools\agility", @"C:\tools\directx-headers", @"C:\tools\dxc");
            IReadOnlyDictionary<string, string?> environment = SourceManager.BuildEnvironment(tools);
            Assert(!environment.Keys.Any(key => key.StartsWith("GIT_", StringComparison.OrdinalIgnoreCase) &&
                                                key is not "GIT_CONFIG_NOSYSTEM" and
                                                not "GIT_CONFIG_GLOBAL" and
                                                not "GIT_CONFIG_COUNT" and
                                                not "GIT_CONFIG_KEY_0" and
                                                not "GIT_CONFIG_VALUE_0" and
                                                not "GIT_NO_REPLACE_OBJECTS" and
                                                not "GIT_TERMINAL_PROMPT"));
            Assert(!environment.ContainsKey("GIT_WORK_TREE"));
            Assert(environment["GIT_CONFIG_COUNT"] == "1");
            Assert(environment["GIT_CONFIG_KEY_0"] == "apply.ignoreWhitespace");
            Assert(environment["GIT_CONFIG_VALUE_0"] == "change");
            Assert(environment["LC_ALL"] == "C");
            Assert(environment["LANG"] == "C");
        }
        finally
        {
            Environment.SetEnvironmentVariable("GIT_DIR", oldGitDirectory);
            Environment.SetEnvironmentVariable("GIT_WORK_TREE", oldGitWorkTree);
            Environment.SetEnvironmentVariable("GIT_CONFIG_COUNT", oldGitConfigCount);
        }
    }

    private static void GitConfigurationIsReplaced()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        Directory.CreateDirectory(Path.Combine(paths.SourceDirectory, ".git"));
        string config = Path.Combine(paths.SourceDirectory, ".git", "config");
        File.WriteAllText(config,
            "[url \"https://attacker.invalid/\"]\n\tinsteadOf = https://github.com/\n" +
            "[core]\n\tworktree = C:/outside\n");
        SourceManager source = new(paths, new ProcessRunner());
        source.WriteSafeRepositoryConfig();
        string actual = File.ReadAllText(config);
        Assert(!actual.Contains("insteadOf", StringComparison.OrdinalIgnoreCase));
        Assert(!actual.Contains("worktree", StringComparison.OrdinalIgnoreCase));
        Assert(actual.Contains(ProductConstants.RepositoryUrl, StringComparison.Ordinal));
    }

    private static void SourceCachePoisonIsDiscarded()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        Directory.CreateDirectory(paths.SourceDirectory);
        string outside = Path.Combine(test.Root, "outside");
        Directory.CreateDirectory(outside);
        string sentinel = Path.Combine(outside, "sentinel.txt");
        File.WriteAllText(sentinel, "preserve");
        File.WriteAllText(Path.Combine(paths.SourceDirectory, ".git"),
            "gitdir: " + Path.Combine(outside, ".git"));
        File.WriteAllText(Path.Combine(paths.SourceDirectory, "poison.txt"), "discard");

        SourceManager source = new(paths, new ProcessRunner());
        source.RecreateManagedSourceDirectory();

        Assert(Directory.Exists(paths.SourceDirectory));
        Assert(!Directory.EnumerateFileSystemEntries(paths.SourceDirectory).Any());
        Assert(File.ReadAllText(sentinel) == "preserve");
    }

    private static void GitRetryClassificationIsNarrow()
    {
        Assert(SourceManager.IsTransientNetworkFailure(new ProcessResult(1, string.Empty,
            "RPC failed; HTTP/2 stream 5 was not closed cleanly")));
        Assert(SourceManager.IsTransientNetworkFailure(new ProcessResult(1, string.Empty,
            "fatal: unable to access: Could not resolve host")));
        Assert(SourceManager.IsTransientNetworkFailure(new ProcessResult(1, string.Empty,
            "curl 28 Operation too slow. Less than 1 bytes/sec transferred the last 90 seconds")));
        Assert(SourceManager.IsTransientNetworkFailure(new ProcessResult(1, string.Empty,
            "schannel: failed to receive handshake, SSL/TLS connection failed")));
        Assert(SourceManager.IsTransientNetworkFailure(new ProcessResult(1, string.Empty,
            "GnuTLS recv error (-110): The TLS connection was non-properly terminated")));
        Assert(SourceManager.ContainsHttp2Failure(new ProcessResult(1, string.Empty,
            "RPC failed; HTTP/2 stream 5 was not closed cleanly")));
        Assert(!SourceManager.IsTransientNetworkFailure(new ProcessResult(1, string.Empty,
            "fatal: Authentication failed")));
        Assert(!SourceManager.IsTransientNetworkFailure(new ProcessResult(1, string.Empty,
            "SSL certificate problem: unable to get local issuer certificate")));
        Assert(!SourceManager.IsTransientNetworkFailure(new ProcessResult(1, string.Empty,
            "error: pathspec did not match")));
    }

    private static void GitAncestryClassificationIsExact()
    {
        Assert(!SourceManager.ClassifyAncestryExitCode(0));
        Assert(SourceManager.ClassifyAncestryExitCode(1));
        Expect<InstallerException>(() => SourceManager.ClassifyAncestryExitCode(2));
        Expect<InstallerException>(() => SourceManager.ClassifyAncestryExitCode(128));
    }

    private static void OperationLockRejectsConcurrency()
    {
        using OperationLock first = OperationLock.Acquire();
        Expect<InstallerException>(() =>
        {
            using OperationLock ignored = OperationLock.Acquire();
        });
    }

    private static void LaunchHonorsLifecycleLock()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        using InstallerEngine engine = new(paths);
        using OperationLock held = OperationLock.Acquire();
        Expect<InstallerException>(() => engine.LaunchInstalled());
    }

    private static void CleanupWatcherRetriesAndSelfDeletes()
    {
        using TestDirectory test = new();
        string helperDirectory = Path.Combine(test.Root, "helper & 100%");
        Directory.CreateDirectory(helperDirectory);
        string helper = Path.Combine(helperDirectory, "UVSR-Launcher-Cleanup.exe");
        File.WriteAllText(helper, "fixture");
        string watcherName = "cleanup-watch-fixture.cmd";
        string watcher = Path.Combine(test.Root, watcherName);
        string script = SelfCleanup.BuildSelfDeleteScript(helper, helperDirectory);
        Assert(!script.Contains("rd /s", StringComparison.OrdinalIgnoreCase));
        File.WriteAllText(watcher, script, new UTF8Encoding(false));

        using FileStream locked = new(helper, FileMode.Open, FileAccess.Read, FileShare.None);
        System.Diagnostics.ProcessStartInfo start = new()
        {
            FileName = Path.Combine(Environment.SystemDirectory, "cmd.exe"),
            WorkingDirectory = test.Root,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        start.ArgumentList.Add("/d");
        start.ArgumentList.Add("/q");
        start.ArgumentList.Add("/c");
        start.ArgumentList.Add(watcherName);
        using System.Diagnostics.Process process = System.Diagnostics.Process.Start(start)
            ?? throw new InvalidOperationException("Could not start the cleanup watcher fixture.");
        Thread.Sleep(250);
        if (!File.Exists(helper))
            throw new InvalidOperationException(
                "The watcher did not encounter the locked-file retry fixture.");
        if (process.HasExited)
            throw new InvalidOperationException(
                "The cleanup watcher stopped before the file lock was released.");
        locked.Dispose();
        if (!process.WaitForExit(15_000))
            throw new InvalidOperationException("The cleanup watcher did not finish.");
        // cmd.exe may report 1 after the batch file deletes the source it was
        // reading. The durable result is the exact-file/directory cleanup below.
        if (Directory.Exists(helperDirectory))
            throw new InvalidOperationException("The helper directory was not removed.");
        if (File.Exists(watcher))
            throw new InvalidOperationException("The cleanup watcher did not remove itself.");
    }

    private static void CleanupWatcherRecordsTimeout()
    {
        using TestDirectory test = new();
        string helperDirectory = Path.Combine(test.Root, "timeout-helper");
        Directory.CreateDirectory(helperDirectory);
        string helper = Path.Combine(helperDirectory, "UVSR-Launcher-Cleanup.exe");
        File.WriteAllText(helper, "fixture");
        string watcherName = "cleanup-watch-timeout.cmd";
        string watcher = Path.Combine(test.Root, watcherName);
        File.WriteAllText(watcher, SelfCleanup.BuildSelfDeleteScript(
            helper, helperDirectory, maximumAttempts: 1), new UTF8Encoding(false));

        using FileStream locked = new(helper, FileMode.Open, FileAccess.Read, FileShare.None);
        System.Diagnostics.ProcessStartInfo start = new()
        {
            FileName = Path.Combine(Environment.SystemDirectory, "cmd.exe"),
            WorkingDirectory = test.Root,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        start.ArgumentList.Add("/d");
        start.ArgumentList.Add("/q");
        start.ArgumentList.Add("/c");
        start.ArgumentList.Add(watcherName);
        using System.Diagnostics.Process process = System.Diagnostics.Process.Start(start)
            ?? throw new InvalidOperationException("Could not start the timeout watcher fixture.");
        if (!process.WaitForExit(10_000))
            throw new InvalidOperationException("The timeout watcher did not finish.");
        Assert(File.Exists(helper));
        Assert(!File.Exists(watcher));
        Assert(File.Exists(Path.Combine(test.Root, "logs", "cleanup-pending.log")));
    }

    private static void ProductRootsAreDistinct()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        Assert(!SafePaths.IsStrictDescendant(paths.StateRoot, paths.ProgramRoot));
        Assert(!SafePaths.IsStrictDescendant(paths.ProgramRoot, paths.StateRoot));
        Assert(paths.DesktopShortcut.EndsWith("UVSR Launcher.lnk",
            StringComparison.OrdinalIgnoreCase));
        Assert(paths.LegacyDesktopShortcut.EndsWith("UVSR.lnk",
            StringComparison.OrdinalIgnoreCase));
        Assert(paths.LauncherStateFile.EndsWith("launcher-state.json",
            StringComparison.OrdinalIgnoreCase));
    }

    private static void LegacyOperationValuesRemainStable()
    {
        Assert((int)InstallerOperation.Install == 0);
        Assert((int)InstallerOperation.Update == 1);
        Assert((int)InstallerOperation.Reinstall == 2);
        Assert((int)InstallerOperation.Uninstall == 3);
    }

    private static void LauncherActionStatesSupportRepair()
    {
        InstallSnapshot fresh = new(false, false, null, null, null, "fresh");
        var freshActions = MainForm.DetermineActionAvailability(fresh, false);
        Assert(freshActions.Install && freshActions.Update && !freshActions.Uninstall &&
               !freshActions.Launch && freshActions.Options);

        InstallSnapshot partial = new(true, false, Guid.NewGuid(), null, null, "partial");
        var partialActions = MainForm.DetermineActionAvailability(partial, false);
        Assert(partialActions.Install && partialActions.Update && partialActions.Uninstall &&
               !partialActions.Launch);

        InstallSnapshot damaged = partial with { IsInstalled = true, IsDamaged = true };
        var damagedActions = MainForm.DetermineActionAvailability(damaged, false);
        Assert(damagedActions.Install && damagedActions.Update && damagedActions.Uninstall &&
               !damagedActions.Launch);

        var busy = MainForm.DetermineActionAvailability(damaged, true);
        Assert(!busy.Install && !busy.Update && !busy.Uninstall && !busy.Launch && !busy.Options);
    }

    private static void RendererButtonIntentIsStable()
    {
        Assert(MainForm.ReconcileRendererClick(RendererButtonIntent.Close,
                   TrackedProcessState.Running) == RendererClickAction.Close);
        Assert(MainForm.ReconcileRendererClick(RendererButtonIntent.Launch,
                   TrackedProcessState.NotRunning) == RendererClickAction.Launch);
        Assert(MainForm.ReconcileRendererClick(RendererButtonIntent.Close,
                   TrackedProcessState.NotRunning) == RendererClickAction.None);
        Assert(MainForm.ReconcileRendererClick(RendererButtonIntent.Launch,
                   TrackedProcessState.Running) == RendererClickAction.None);
        Assert(MainForm.ReconcileRendererClick(RendererButtonIntent.Close,
                   TrackedProcessState.Unverifiable) == RendererClickAction.None);
    }

    private static void ExactRendererIdentityObservesExit()
    {
        using TestDirectory test = new();
        string appHost = Path.ChangeExtension(typeof(Program).Assembly.Location, ".exe");
        string marker = Path.Combine(test.Root, "child-finished.txt");
        ExactProcessIdentity? identity = null;
        using (System.Diagnostics.Process process =
               System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
               {
                   FileName = appHost,
                   UseShellExecute = false,
                   CreateNoWindow = true,
                   ArgumentList = { "--job-child", marker }
               }) ?? throw new InvalidOperationException("Could not start the identity fixture."))
        {
            DateTimeOffset captureDeadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(1);
            while (identity is null && DateTimeOffset.UtcNow < captureDeadline)
            {
                identity = ProcessInspector.TryCaptureExactProcess(process.Id);
                if (identity is null)
                    Thread.Sleep(10);
            }
            Assert(identity is not null);
            Assert(ProcessInspector.InspectExactProcess(identity!) ==
                   TrackedProcessState.Running);
            Assert(!ProcessInspector.MatchesExactProcess(identity!, identity!.ProcessId,
                identity.ExecutablePath, identity.CreationTimeUtcFileTime + 1));
            Assert(process.WaitForExit(10_000));
            Assert(File.Exists(marker));
            Assert(ProcessInspector.InspectExactProcess(identity!) ==
                   TrackedProcessState.NotRunning);
        }
        Assert(ProcessInspector.InspectExactProcess(identity!) ==
               TrackedProcessState.NotRunning);
        Assert(ProcessInspector.ClassifyProcessHandleWait(0) ==
               TrackedProcessState.NotRunning);
        Assert(ProcessInspector.ClassifyProcessHandleWait(0x102) ==
               TrackedProcessState.Running);
        Assert(ProcessInspector.ClassifyProcessHandleWait(uint.MaxValue) ==
               TrackedProcessState.Unverifiable);
        Assert(ProcessInspector.IsConfirmedNotRunning(
            new ExactProcessInspection(TrackedProcessState.NotRunning,
                Array.Empty<ExactProcessIdentity>())));
        Assert(!ProcessInspector.IsConfirmedNotRunning(
            new ExactProcessInspection(TrackedProcessState.Unverifiable,
                Array.Empty<ExactProcessIdentity>())));
    }

    private static void ExactTerminationUsesVerifiedHandle()
    {
        using TestDirectory test = new();
        string sentinel = Path.Combine(test.Root, "escaped.txt");
        string appHost = Path.ChangeExtension(typeof(Program).Assembly.Location, ".exe");
        ProcessStartInfo start = new()
        {
            FileName = appHost,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        start.ArgumentList.Add("--job-child");
        start.ArgumentList.Add(sentinel);
        using Process process = Process.Start(start)
            ?? throw new InvalidOperationException("Could not start force-close fixture.");
        try
        {
            ExactProcessIdentity? identity = null;
            for (int attempt = 0; attempt < 20 && identity is null; ++attempt)
            {
                identity = ProcessInspector.TryCaptureExactProcess(process.Id);
                if (identity is null)
                    Thread.Sleep(25);
            }
            Assert(identity is not null);
            Assert(ProcessInspector.TryTerminate(identity!));
            Assert(process.WaitForExit(5000));
            Assert(ProcessInspector.InspectExactProcess(identity!) ==
                   TrackedProcessState.NotRunning);
            Assert(!File.Exists(sentinel));
        }
        finally
        {
            if (!process.HasExited)
                process.Kill(entireProcessTree: true);
        }

        string source = File.ReadAllText(Path.Combine(RepositoryRoot(), "installer",
            "src", "UVSR.Installer", "ProcessInspector.cs"));
        int startIndex = source.IndexOf("internal static bool TryTerminate",
            StringComparison.Ordinal);
        int endIndex = source.IndexOf("private static Process? TryOpenExactProcess",
            startIndex, StringComparison.Ordinal);
        Assert(startIndex >= 0 && endIndex > startIndex);
        string terminationBody = source[startIndex..endIndex];
        Assert(terminationBody.Contains("TerminateProcess(process, 1)",
            StringComparison.Ordinal));
        Assert(!terminationBody.Contains("GetProcessById", StringComparison.Ordinal));
        Assert(!terminationBody.Contains(".Kill(", StringComparison.Ordinal));
    }

    private static void UpdateExitStatesAreTerminal()
    {
        foreach (UpdateFlowExit exit in Enum.GetValues<UpdateFlowExit>())
        {
            TerminalUiState state = MainForm.DetermineUpdateTerminalState(exit);
            Assert(state.ProgressStyle == System.Windows.Forms.ProgressBarStyle.Blocks);
            Assert(!string.IsNullOrWhiteSpace(state.Phase));
            Assert(!string.IsNullOrWhiteSpace(state.Detail));
            Assert(state.Progress is >= 0 and <= 100);
        }
        TerminalUiState current = MainForm.DetermineUpdateTerminalState(
            UpdateFlowExit.UpToDate, "custom current detail");
        Assert(current.Phase == "Up to Date");
        Assert(current.Detail == "custom current detail");
        Assert(current.Progress == 100);
    }

    private static void LauncherLayoutsAreBounded()
    {
        Rectangle working = new(0, 0, 1280, 720);
        foreach (int dpi in new[] { 96, 144, 192, 216 })
        {
            LauncherWindowLayout layout = LauncherUi.CalculateWindowLayout(
                working, dpi, new Size(1700, 1300), new Size(700, 520));
            Assert(layout.OuterSize.Width <= layout.AvailableBounds.Width);
            Assert(layout.OuterSize.Height <= layout.AvailableBounds.Height);
            Assert(layout.MinimumOuterSize.Width <= layout.AvailableBounds.Width);
            Assert(layout.MinimumOuterSize.Height <= layout.AvailableBounds.Height);
            Assert(layout.OuterSize.Width >= layout.MinimumOuterSize.Width);
            Assert(layout.OuterSize.Height >= layout.MinimumOuterSize.Height);
        }
        Rectangle offsetWorking = new(-1280, 40, 1280, 680);
        Rectangle available = LauncherUi.CalculateAvailableWorkingBounds(
            offsetWorking, 216);
        Assert(offsetWorking.Contains(available));
        Assert(available.Width > 0 && available.Height > 0);
    }

    private static void UpdateSelectionCopyIsAccurate()
    {
        ComponentUpdateStatus failed = new(UpdateComponent.Uvsr,
            ComponentUpdateState.CheckFailed, null, null, "fixture failure");
        ComponentUpdateStatus current = new(UpdateComponent.Launcher,
            ComponentUpdateState.Current, "1.1.3", "1.1.1", "fixture current");
        UpdateCheckResult failureOnly = new(failed, current);
        Assert(UpdateSelectionDialog.GetTitle(failureOnly) == "Update Check Results");
        Assert(UpdateSelectionDialog.GetIntro(failureOnly) ==
               "One or more update checks failed. Review the details or check again.");

        ComponentUpdateStatus available = new(UpdateComponent.Launcher,
            ComponentUpdateState.UpdateAvailable, "1.1.3", "1.1.4",
            "fixture update");
        UpdateCheckResult partialFailure = new(failed, available);
        Assert(UpdateSelectionDialog.GetTitle(partialFailure) ==
               "Choose What to Update");
        Assert(UpdateSelectionDialog.GetIntro(partialFailure).Contains(
            "One or more checks failed", StringComparison.Ordinal));
    }

    private static void LauncherFeedParsingIsStrict()
    {
        string valid = ValidLauncherFeedJson();
        LauncherFeed feed = LauncherManager.ParseAndValidateFeed(
            System.Text.Encoding.UTF8.GetBytes(valid));
        Assert(feed.Version == "1.1.0");
        Assert(feed.ReleaseSequence == 1);
        Assert(LauncherManager.BuildArtifactUri(feed).AbsoluteUri ==
               "https://github.com/brockliddicoat/uvsr/releases/download/" +
               "uvsr-launcher-v1.1.0/UVSR-Launcher-Windows-11-x64.exe");

        string legacyPath = Path.Combine(RepositoryRoot(), "installer", "tests",
            "fixtures", "launcher-feed-public-legacy-v1.json");
        string legacy = File.ReadAllText(legacyPath);
        LauncherFeed legacyFeed = LauncherManager.ParseAndValidateFeed(
            Encoding.UTF8.GetBytes(legacy));
        Assert(legacyFeed.Version == "1.1.1");
        Assert(legacyFeed.ReleaseSequence == 2);

        Expect<InstallerException>(() => LauncherManager.ParseAndValidateFeed(
            System.Text.Encoding.UTF8.GetBytes(valid.Replace(
                "\"schemaVersion\":1", "\"schemaVersion\":1,\"schemaVersion\":1"))));
        Expect<InstallerException>(() => LauncherManager.ParseAndValidateFeed(
            System.Text.Encoding.UTF8.GetBytes(valid.Replace(
                "\"channel\":\"stable\"", "\"channel\":\"stable\",\"unknown\":true"))));
        InstallerException identityFailure = Capture<InstallerException>(() =>
            LauncherManager.ParseAndValidateFeed(
                System.Text.Encoding.UTF8.GetBytes(valid.Replace(new string('a', 64),
                    new string('A', 64)))));
        Assert(identityFailure.Message.Contains("artifact SHA-256",
            StringComparison.Ordinal));
        Assert(!identityFailure.Message.Contains("camelCase", StringComparison.Ordinal));
        InstallerException legacyIdentityFailure = Capture<InstallerException>(() =>
            LauncherManager.ParseAndValidateFeed(Encoding.UTF8.GetBytes(
                legacy.Replace("2b5f092bdf80dcdabca46034f1334f6be374c712400e7bf8d6ae1e672f7a5b36",
                    new string('A', 64)))));
        Assert(legacyIdentityFailure.Message.Contains("artifact SHA-256",
            StringComparison.Ordinal));
        Assert(!legacyIdentityFailure.Message.Contains("camelCase",
            StringComparison.Ordinal));
        Expect<InstallerException>(() => LauncherManager.ParseAndValidateFeed(
            System.Text.Encoding.UTF8.GetBytes(valid.Replace("\"version\":\"1.1.0\"",
                "\"version\":\"01.1.0\""))));
        Expect<InstallerException>(() => LauncherManager.ParseAndValidateFeed(
            Encoding.UTF8.GetBytes(legacy.Replace("\"Artifact\"", "\"artifact\""))));
        Expect<InstallerException>(() => LauncherManager.ParseAndValidateFeed(
            Encoding.UTF8.GetBytes(legacy.Replace("\"SchemaVersion\": 1",
                "\"SchemaVersion\": 1,\n  \"schemaVersion\": 1"))));
        Expect<InstallerException>(() => LauncherManager.ParseAndValidateFeed(
            new byte[ProductConstants.MaximumLauncherFeedBytes + 1]));
    }

    private static void CheckedInLauncherFeedIsValid()
    {
        string feedPath = Path.Combine(RepositoryRoot(), "installer",
            "launcher-feed-v1.json");
        LauncherFeed feed = LauncherManager.ParseAndValidateFeed(
            File.ReadAllBytes(feedPath));
        Assert(feed.ProductId == ProductConstants.ProductId);
        Assert(feed.Channel == "stable");
        Assert(feed.ReleaseSequence == 2);
        Assert(feed.Version == "1.1.1");
        Assert(feed.Artifact.Name == ProductConstants.LauncherArtifactName);
        Assert(feed.Artifact.Size == 58_370_076);
        Assert(feed.Artifact.Sha256 ==
               "2b5f092bdf80dcdabca46034f1334f6be374c712400e7bf8d6ae1e672f7a5b36");
        Assert(ProductConstants.LauncherReleaseSequence > feed.ReleaseSequence);
    }

    private static void UpdateCheckDiagnosticsAreExact()
    {
        string legacyPath = Path.Combine(RepositoryRoot(), "installer", "tests",
            "fixtures", "launcher-feed-public-legacy-v1.json");
        byte[] legacyFeed = File.ReadAllBytes(legacyPath);
        byte[] bridgeMainReference = Encoding.UTF8.GetBytes(
            "{\"ref\":\"refs/heads/main\",\"object\":{" +
            "\"type\":\"commit\",\"sha\":\"" +
            RendererSourceBridge.PublicBaseCommit + "\"}}");
        string nonBridgeCommit = new('c', 40);
        byte[] nonBridgeMainReference = Encoding.UTF8.GetBytes(
            "{\"ref\":\"refs/heads/main\",\"object\":{" +
            "\"type\":\"commit\",\"sha\":\"" + nonBridgeCommit + "\"}}");
        string contractSource =
            SourceManager.BuildRendererBuildContractUri(nonBridgeCommit).AbsoluteUri;

        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "Programs"));
        List<string> requestedSources = new();
        using DownloadManager downloads = new(new SequenceResponseHandler(
            (request, _) =>
            {
                string source = request.RequestUri!.AbsoluteUri;
                requestedSources.Add(source);
                return source switch
                {
                    ProductConstants.LauncherFeedUrl =>
                        Response(request, HttpStatusCode.OK, legacyFeed),
                    ProductConstants.RepositoryMainApi =>
                        Response(request, HttpStatusCode.OK, bridgeMainReference),
                    _ => throw new InvalidOperationException(
                        $"Unexpected update-check source '{source}'.")
                };
            }), FastDownloadPolicy(1));
        LauncherManager manager = new(paths, new ProcessRunner(), downloads);
        InstallLog log = new(paths.LogsDirectory);
        UpdateCheckResult result = manager.CheckForUpdatesAsync(
            new OwnerMarker(ProductConstants.SchemaVersion, ProductConstants.ProductId,
                Guid.NewGuid()),
            new InstallSnapshot(false, false, null, null, null, "Not installed"),
            progress: null, log, CancellationToken.None).GetAwaiter().GetResult();

        Assert(result.Launcher.State == ComponentUpdateState.Current);
        Assert(result.Launcher.Detail.Contains(ProductConstants.LauncherFeedUrl,
            StringComparison.Ordinal));
        Assert(result.Launcher.Detail.Contains("No launcher update is needed",
            StringComparison.Ordinal));
        Assert(result.Launcher.Detail.Contains("1.1.1 (sequence 2)",
            StringComparison.Ordinal));
        Assert(result.Uvsr.State == ComponentUpdateState.NotInstalled);
        Assert(result.Uvsr.UvsrCommit == RendererSourceBridge.SourceCommit);
        Assert(result.Uvsr.Detail.Contains(RendererSourceBridge.BridgeId,
            StringComparison.Ordinal));
        Assert(result.Uvsr.Detail.Contains(
            RendererSourceBridge.PublicBaseCommit[..7],
            StringComparison.Ordinal));
        Assert(result.Uvsr.Detail.Contains(RendererSourceBridge.SourceCommit[..7],
            StringComparison.Ordinal));
        Assert(requestedSources.SequenceEqual(new[]
        {
            ProductConstants.LauncherFeedUrl,
            ProductConstants.RepositoryMainApi
        }));

        Guid rendererInstallationId = Guid.NewGuid();
        InstallState bridgedState = new(ProductConstants.SchemaVersion,
            rendererInstallationId,
            $"{RendererSourceBridge.SourceCommit}-20260820000000-12345678",
            RendererSourceBridge.SourceCommit, new string('a', 64), true,
            DateTimeOffset.UtcNow);
        UpdateCheckResult currentBridge = manager.CheckForUpdatesAsync(
            new OwnerMarker(ProductConstants.SchemaVersion,
                ProductConstants.ProductId, rendererInstallationId),
            new InstallSnapshot(true, true, rendererInstallationId, bridgedState,
                @"C:\fixture\uvsr.exe", "Installed"), null, log,
            CancellationToken.None).GetAwaiter().GetResult();
        Assert(currentBridge.Uvsr.State == ComponentUpdateState.Current);

        InstallState unbridgedState = bridgedState with
        {
            ActiveVersionId =
                $"{RendererSourceBridge.PublicBaseCommit}-20260820000000-12345678",
            Commit = RendererSourceBridge.PublicBaseCommit
        };
        UpdateCheckResult bridgeUpdate = manager.CheckForUpdatesAsync(
            new OwnerMarker(ProductConstants.SchemaVersion,
                ProductConstants.ProductId, rendererInstallationId),
            new InstallSnapshot(true, true, rendererInstallationId, unbridgedState,
                @"C:\fixture\uvsr.exe", "Installed"), null, log,
            CancellationToken.None).GetAwaiter().GetResult();
        Assert(bridgeUpdate.Uvsr.State == ComponentUpdateState.UpdateAvailable);
        Assert(bridgeUpdate.Uvsr.UvsrCommit == RendererSourceBridge.SourceCommit);

        string details = File.ReadAllText(log.Path);
        Assert(details.Contains($"Checking UVSR Launcher update source: " +
            ProductConstants.LauncherFeedUrl, StringComparison.Ordinal));
        Assert(details.Contains($"published 1.1.1 sequence 2; result Current",
            StringComparison.Ordinal));
        Assert(details.Contains(ProductConstants.RepositoryMainApi,
            StringComparison.Ordinal));
        Assert(details.Contains("No remote renderer contract request was required",
            StringComparison.Ordinal));
        Assert(details.Contains(RendererSourceBridge.PatchSha256,
            StringComparison.Ordinal));

        byte[] malformedFeed = Encoding.UTF8.GetBytes(
            Encoding.UTF8.GetString(legacyFeed).Replace("\"Artifact\"", "\"artifact\""));
        using TestDirectory failureTest = new();
        InstallerPaths failurePaths = InstallerPaths.Create(failureTest.Root,
            Path.Combine(failureTest.Root, "Desktop"),
            Path.Combine(failureTest.Root, "Programs"));
        using DownloadManager failureDownloads = new(new SequenceResponseHandler(
            (request, _) => request.RequestUri!.AbsoluteUri switch
            {
                ProductConstants.LauncherFeedUrl =>
                    Response(request, HttpStatusCode.OK, malformedFeed),
                ProductConstants.RepositoryMainApi =>
                    Response(request, HttpStatusCode.OK, bridgeMainReference),
                var unexpected => throw new InvalidOperationException(
                    $"Unexpected update-check source '{unexpected}'.")
            }), FastDownloadPolicy(1));
        LauncherManager failureManager = new(failurePaths, new ProcessRunner(),
            failureDownloads);
        InstallLog failureLog = new(failurePaths.LogsDirectory);
        UpdateCheckResult failed = failureManager.CheckForUpdatesAsync(
            new OwnerMarker(ProductConstants.SchemaVersion, ProductConstants.ProductId,
                Guid.NewGuid()),
            new InstallSnapshot(false, false, null, null, null, "Not installed"),
            progress: null, failureLog, CancellationToken.None).GetAwaiter().GetResult();
        Assert(failed.Launcher.State == ComponentUpdateState.CheckFailed);
        Assert(failed.Launcher.Detail.Contains(ProductConstants.LauncherFeedUrl,
            StringComparison.Ordinal));
        Assert(failed.Launcher.Detail.Contains("required canonical camelCase",
            StringComparison.Ordinal));
        Assert(File.ReadAllText(failureLog.Path).Contains(
            $"Launcher update check failed at {ProductConstants.LauncherFeedUrl}",
            StringComparison.Ordinal));

        UpdateCheckResult httpFailure = RunUpdateCheckFixture(legacyFeed,
            nonBridgeMainReference, contractSource,
            request => Response(request, HttpStatusCode.InternalServerError,
                Array.Empty<byte>()), out string httpFailureLog);
        Assert(httpFailure.Launcher.State == ComponentUpdateState.Current);
        Assert(httpFailure.Uvsr.State == ComponentUpdateState.CheckFailed);
        Assert(httpFailure.Uvsr.Detail.StartsWith(
            $"UVSR update check failed at {contractSource}",
            StringComparison.Ordinal));
        Assert(httpFailure.Uvsr.Detail.Contains("HTTP 500", StringComparison.Ordinal));
        Assert(httpFailureLog.Contains($"UVSR update check failed at {contractSource}",
            StringComparison.Ordinal));

        UpdateCheckResult tlsFailure = RunUpdateCheckFixture(legacyFeed,
            nonBridgeMainReference, contractSource,
            _ => throw new HttpRequestException("fixture TLS rejection",
                new AuthenticationException("fixture certificate rejected")),
            out string tlsFailureLog);
        Assert(tlsFailure.Launcher.State == ComponentUpdateState.Current);
        Assert(tlsFailure.Uvsr.State == ComponentUpdateState.CheckFailed);
        Assert(tlsFailure.Uvsr.Detail.StartsWith(
            $"UVSR update check failed at {contractSource}",
            StringComparison.Ordinal));
        Assert(tlsFailure.Uvsr.Detail.Contains("secure download",
            StringComparison.OrdinalIgnoreCase));
        Assert(tlsFailure.Uvsr.Detail.Contains("fixture certificate rejected",
            StringComparison.Ordinal));
        Assert(tlsFailureLog.Contains($"UVSR update check failed at {contractSource}",
            StringComparison.Ordinal));
    }

    private static UpdateCheckResult RunUpdateCheckFixture(
        byte[] launcherFeed,
        byte[] mainReference,
        string contractSource,
        Func<HttpRequestMessage, HttpResponseMessage> contractResponse,
        out string logText)
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "Programs"));
        using DownloadManager downloads = new(new SequenceResponseHandler(
            (request, _) => request.RequestUri!.AbsoluteUri switch
            {
                ProductConstants.LauncherFeedUrl =>
                    Response(request, HttpStatusCode.OK, launcherFeed),
                ProductConstants.RepositoryMainApi =>
                    Response(request, HttpStatusCode.OK, mainReference),
                var source when source == contractSource => contractResponse(request),
                var unexpected => throw new InvalidOperationException(
                    $"Unexpected update-check source '{unexpected}'.")
            }), FastDownloadPolicy(1));
        LauncherManager manager = new(paths, new ProcessRunner(), downloads);
        InstallLog log = new(paths.LogsDirectory);
        UpdateCheckResult result = manager.CheckForUpdatesAsync(
            new OwnerMarker(ProductConstants.SchemaVersion, ProductConstants.ProductId,
                Guid.NewGuid()),
            new InstallSnapshot(false, false, null, null, null, "Not installed"),
            progress: null, log, CancellationToken.None).GetAwaiter().GetResult();
        logText = File.ReadAllText(log.Path);
        return result;
    }

    private static void MainReferenceParsingIsStrict()
    {
        string commit = "0123456789abcdef0123456789abcdef01234567";
        string json = "{\"ref\":\"refs/heads/main\",\"object\":{" +
                      "\"type\":\"commit\",\"sha\":\"" + commit + "\"}}";
        Assert(LauncherManager.ParseMainCommit(System.Text.Encoding.UTF8.GetBytes(json)) == commit);
        Expect<InstallerException>(() => LauncherManager.ParseMainCommit(
            System.Text.Encoding.UTF8.GetBytes(json.Replace(
                "\"ref\":", "\"ref\":\"refs/heads/main\",\"ref\":"))));
        Expect<InstallerException>(() => LauncherManager.ParseMainCommit(
            System.Text.Encoding.UTF8.GetBytes(json.Replace("\"commit\"", "\"tag\""))));
    }

    private static void LauncherStateValidationIsStrict()
    {
        Guid installationId = Guid.NewGuid();
        LauncherState state = new(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, installationId, 1, "1.1.0",
            new string('a', 64), true, DateTimeOffset.UtcNow);
        state.Validate(installationId);
        Expect<InstallerException>(() => (state with { ReleaseSequence = 0 }).Validate(installationId));
        Expect<InstallerException>(() => (state with { Version = "1.1.0-beta" }).Validate(installationId));
        Expect<InstallerException>(() => (state with { ExecutableSha256 = "../outside" }).Validate(installationId));
        Expect<InstallerException>(() => state.Validate(Guid.NewGuid()));
    }

    private static void LauncherReleaseIdentityIsUnique()
    {
        LauncherReleaseIdentity current = new(5, "5.0.0", new string('a', 64));
        LauncherReleaseIdentity exact = current;
        LauncherReleaseIdentity older = new(4, "4.0.0", new string('b', 64));
        LauncherReleaseIdentity newer = new(6, "6.0.0", new string('c', 64));
        LauncherReleaseIdentity conflict = current with
        {
            ExecutableSha256 = new string('d', 64)
        };
        Assert(LauncherManager.ClassifyLauncherUpdate(current, false, exact) ==
               ComponentUpdateState.RepairNeeded);
        Assert(LauncherManager.ClassifyLauncherUpdate(current, true, exact) ==
               ComponentUpdateState.Current);
        Assert(LauncherManager.ClassifyLauncherUpdate(current, true, older) ==
               ComponentUpdateState.Current);
        Assert(LauncherManager.ClassifyLauncherUpdate(current, true, newer) ==
               ComponentUpdateState.UpdateAvailable);
        Expect<InstallerException>(() =>
            LauncherManager.ClassifyLauncherUpdate(current, false, conflict));

        LauncherReleaseIdentity? highest = LauncherManager.ResolveHighestDefensibleIdentity(
            new[] { older, current, exact });
        Assert(highest == current);
        Expect<InstallerException>(() => LauncherManager.ResolveHighestDefensibleIdentity(
            new[] { current, conflict }));
    }

    private static void LauncherStartupConverges()
    {
        string external = Path.Combine(Path.GetTempPath(), "external-launcher.exe");
        string installed = Path.Combine(Path.GetTempPath(), "installed-launcher.exe");
        Assert(LauncherManager.DecideLauncherStartup(3, external, 3, installed,
                   redirectSafeArguments: true) ==
               LauncherStartupAction.RedirectToVerifiedInstalled);
        Assert(LauncherManager.DecideLauncherStartup(3, external, 4, installed,
                   redirectSafeArguments: true) ==
               LauncherStartupAction.RedirectToVerifiedInstalled);
        Assert(LauncherManager.DecideLauncherStartup(3, external, 2, installed,
                   redirectSafeArguments: true) == LauncherStartupAction.ContinueCurrent);
        Assert(LauncherManager.DecideLauncherStartup(3, installed, 3, installed,
                   redirectSafeArguments: true) == LauncherStartupAction.ContinueCurrent);
        Assert(LauncherManager.DecideLauncherStartup(3, external, 3, installed,
                   redirectSafeArguments: false) == LauncherStartupAction.ContinueCurrent);
        Assert(LauncherManager.AreRedirectSafeArguments(Array.Empty<string>()));
        Assert(LauncherManager.AreRedirectSafeArguments(new[] { "--launch" }));
        Assert(LauncherManager.AreRedirectSafeArguments(new[] { "--uninstall" }));
        Assert(!LauncherManager.AreRedirectSafeArguments(
            new[] { "--continue-uvsr-update", Guid.NewGuid().ToString("D") }));
    }

    private static void LauncherRecoveryConverges()
    {
        Assert(LauncherManager.DecideLauncherRecovery("prepared",
                   LauncherRecoveryPackageStatus.Missing,
                   LauncherRecoveryPackageStatus.Valid) ==
               LauncherRecoveryAction.RollBack);
        Assert(LauncherManager.DecideLauncherRecovery("prepared",
                   LauncherRecoveryPackageStatus.Valid,
                   LauncherRecoveryPackageStatus.Invalid) ==
               LauncherRecoveryAction.RollBack);
        Assert(LauncherManager.DecideLauncherRecovery("prepared",
                   LauncherRecoveryPackageStatus.Invalid,
                   LauncherRecoveryPackageStatus.Valid) ==
               LauncherRecoveryAction.RollForward);
        Assert(LauncherManager.DecideLauncherRecovery("state-activated",
                   LauncherRecoveryPackageStatus.Valid,
                   LauncherRecoveryPackageStatus.Invalid) ==
               LauncherRecoveryAction.RollBack);
        Assert(LauncherManager.DecideLauncherRecovery("shell-committed",
                   LauncherRecoveryPackageStatus.Invalid,
                   LauncherRecoveryPackageStatus.Valid) ==
               LauncherRecoveryAction.RollForward);
        Assert(LauncherManager.DecideLauncherRecovery("awaiting-continuation",
                   LauncherRecoveryPackageStatus.Invalid,
                   LauncherRecoveryPackageStatus.Invalid) ==
               LauncherRecoveryAction.ClearBrokenJournal);
        Assert(LauncherManager.DecideLauncherRecovery("state-activated",
                   LauncherRecoveryPackageStatus.Unverifiable,
                   LauncherRecoveryPackageStatus.Invalid) ==
               LauncherRecoveryAction.RetryLater);
        Assert(LauncherManager.DecideLauncherRecovery("prepared",
                   LauncherRecoveryPackageStatus.Invalid,
                   LauncherRecoveryPackageStatus.Unverifiable) ==
               LauncherRecoveryAction.RetryLater);
    }

    private static void LauncherContinuationIsPreserved()
    {
        Guid installationId = Guid.NewGuid();
        Guid transactionId = Guid.NewGuid();
        LauncherState installed = new(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, installationId, 5, "5.0.0",
            new string('a', 64), true, DateTimeOffset.UtcNow);
        LauncherActivationRecord pending = new(
            ProductConstants.LauncherSchemaVersion, ProductConstants.ProductId,
            installationId, transactionId, "awaiting-continuation", null,
            installed, true, DateTimeOffset.UtcNow);
        LauncherActivationRecord merged = LauncherManager.PreserveAwaitingContinuation(
            pending, installed with { DesktopShortcut = false }, installationId);
        Assert(merged.TransactionId == transactionId);
        Assert(merged.Phase == "awaiting-continuation");
        Assert(merged.ContinueUvsrUpdate);
        Assert(!merged.CandidateState.DesktopShortcut);
        Assert(merged.CandidateState.ExecutableSha256 == installed.ExecutableSha256);
        Expect<InstallerException>(() => LauncherManager.PreserveAwaitingContinuation(
            pending, installed with { ExecutableSha256 = new string('b', 64) },
            installationId));
        Expect<InstallerException>(() => LauncherManager.PreserveAwaitingContinuation(
            pending with { Phase = "shell-committed" }, installed, installationId));
    }

    private static void LauncherMarkerBindingIsStrict()
    {
        string hash = new('a', 64);
        LauncherPackageManifest marker = new(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, Guid.NewGuid(), 1, "1.1.0", hash,
            1024, DateTimeOffset.UtcNow);
        LauncherManager.ValidatePackageDirectoryBinding(hash, marker);
        Expect<InstallerException>(() => LauncherManager.ValidatePackageDirectoryBinding(
            new string('b', 64), marker));
        Expect<InstallerException>(() => LauncherManager.ValidatePackageDirectoryBinding(
            "not-a-hash", marker));
    }

    private static void LauncherShortcutLayoutIsContained()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        string hash = new('a', 64);
        string expected = paths.LauncherExecutable(hash);
        Assert(ShellIntegration.IsLauncherTargetInOwnedLayout(paths, expected));
        Assert(!ShellIntegration.IsLauncherTargetInOwnedLayout(paths,
            Path.Combine(paths.LauncherVersionRoot(hash), "other.exe")));
        Assert(!ShellIntegration.IsLauncherTargetInOwnedLayout(paths,
            Path.Combine(test.Root, "outside", hash,
                ProductConstants.LauncherExecutableName)));
        Assert(!ShellIntegration.IsLauncherTargetInOwnedLayout(paths,
            Path.Combine(paths.LauncherVersionsDirectory, "not-a-hash",
                ProductConstants.LauncherExecutableName)));
    }

    private static void LauncherPackageIsVerified()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "ProgramsMenu"));
        OwnerMarker owner = new OwnershipManager(paths).EnsureRoots();
        string source = Path.ChangeExtension(typeof(Program).Assembly.Location, ".exe");
        string hash = PayloadPackager.ComputeSha256(source);
        string package = paths.LauncherVersionRoot(hash);
        Directory.CreateDirectory(package);
        string executable = paths.LauncherExecutable(hash);
        File.Copy(source, executable);
        LauncherState state = new(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, owner.InstallationId, 1, "1.1.0",
            hash, true, DateTimeOffset.UtcNow);
        JsonStore.WriteAtomic(paths.LauncherPackageMarker(hash),
            new LauncherPackageManifest(ProductConstants.LauncherSchemaVersion,
                ProductConstants.ProductId, owner.InstallationId, 1, "1.1.0",
                hash, new FileInfo(executable).Length, DateTimeOffset.UtcNow));
        LauncherManager.ValidatePackage(paths, state);
        File.AppendAllText(executable, "tamper");
        Expect<InstallerException>(() => LauncherManager.ValidatePackage(paths, state));
    }

    private static void LauncherSequenceHasUpperBound()
    {
        Guid installationId = Guid.NewGuid();
        LauncherState state = new(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, installationId,
            ProductConstants.MaximumReleaseSequence + 1, "1.1.0",
            new string('a', 64), true, DateTimeOffset.UtcNow);
        Expect<InstallerException>(() => state.Validate(installationId));
    }

    private static void LauncherInspectionPreventsSilentDowngrade()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "Programs"));
        OwnerMarker owner = new OwnershipManager(paths).EnsureRoots();
        LauncherState retainedV1 = CreateLauncherPackage(paths, owner.InstallationId,
            1, "1.1.0", "retained-v1", true);
        LauncherState damagedV5 = new(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, owner.InstallationId, 5, "5.0.0",
            new string('f', 64), false, DateTimeOffset.UtcNow);
        JsonStore.WriteAtomic(paths.LauncherStateFile, damagedV5);
        using DownloadManager downloads = new(new StaticResponseHandler(Array.Empty<byte>()));
        LauncherManager manager = new(paths, new ProcessRunner(), downloads);
        LauncherActivationInspection inspection = manager.InspectActivation(
            owner.InstallationId, true);
        Assert(inspection.HighestDefensibleSequence == 5);
        Assert(inspection.RecordedState?.ReleaseSequence == 5);
        Assert(inspection.ValidState?.ExecutableSha256 == retainedV1.ExecutableSha256);
    }

    private static void LauncherInspectionFindsRecoveryPackage()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "Programs"));
        OwnerMarker owner = new OwnershipManager(paths).EnsureRoots();
        LauncherState retainedV5 = CreateLauncherPackage(paths, owner.InstallationId,
            5, "5.0.0", "retained-v5", false);
        File.WriteAllText(paths.LauncherStateFile, "{\"schemaVersion\":null}");
        using DownloadManager downloads = new(new StaticResponseHandler(Array.Empty<byte>()));
        LauncherManager manager = new(paths, new ProcessRunner(), downloads);
        LauncherActivationInspection inspection = manager.InspectActivation(
            owner.InstallationId, true);
        Assert(inspection.StateRecordMalformed);
        Assert(inspection.ValidState?.ExecutableSha256 == retainedV5.ExecutableSha256);
        Assert(inspection.ValidState!.DesktopShortcut);
    }

    private static void LockedLauncherStateBlocksActivation()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "Programs"));
        OwnerMarker owner = new OwnershipManager(paths).EnsureRoots();
        LauncherState fallback = CreateLauncherPackage(paths, owner.InstallationId,
            1, "1.1.0", "fallback-v1", true);
        LauncherState newerRecord = new(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, owner.InstallationId, 5, "5.0.0",
            new string('f', 64), false, DateTimeOffset.UtcNow);
        JsonStore.WriteAtomic(paths.LauncherStateFile, newerRecord);
        byte[] originalState = File.ReadAllBytes(paths.LauncherStateFile);
        using DownloadManager downloads = new(new StaticResponseHandler(Array.Empty<byte>()));
        LauncherManager manager = new(paths, new ProcessRunner(), downloads);
        InstallLog log = new(paths.LogsDirectory);

        using (FileStream locked = new(paths.LauncherStateFile, FileMode.Open,
                   FileAccess.ReadWrite, FileShare.None))
        {
            LauncherActivationInspection inspection = manager.InspectActivation(
                owner.InstallationId, true, log);
            Assert(inspection.StateRecordUnverifiable);
            Assert(!inspection.StateRecordMalformed);
            Expect<InstallerException>(() => manager.Activate(owner.InstallationId,
                fallback, null, new ShellIntegration(paths),
                continueUvsrUpdate: false, log));
            Assert(!File.Exists(paths.LauncherTransactionFile));
            Assert(!File.Exists(paths.StartMenuShortcut));
            Assert(!File.Exists(paths.DesktopShortcut));
        }

        Assert(File.ReadAllBytes(paths.LauncherStateFile)
            .SequenceEqual(originalState));
    }

    private static void LauncherCleanupPreservesUnverifiedPackages()
    {
        using TestDirectory test = new();
        InstallerPaths paths = InstallerPaths.Create(test.Root,
            Path.Combine(test.Root, "Desktop"), Path.Combine(test.Root, "Programs"));
        OwnerMarker owner = new OwnershipManager(paths).EnsureRoots();
        LauncherState active = CreateLauncherPackage(paths, owner.InstallationId,
            4, "4.0.0", "active", true);
        LauncherState previous = CreateLauncherPackage(paths, owner.InstallationId,
            3, "3.0.0", "previous", true);
        LauncherState removable = CreateLauncherPackage(paths, owner.InstallationId,
            2, "2.0.0", "removable", true);
        LauncherState unverified = CreateLauncherPackage(paths, owner.InstallationId,
            1, "1.0.0", "unverified", true);
        File.WriteAllText(Path.Combine(paths.LauncherVersionRoot(
            unverified.ExecutableSha256), "sentinel.user"), "preserve");
        string aliasHash = new('e', 64);
        string aliasRoot = paths.LauncherVersionRoot(aliasHash);
        Directory.CreateDirectory(aliasRoot);
        File.Copy(paths.LauncherPackageMarker(removable.ExecutableSha256),
            paths.LauncherPackageMarker(aliasHash));
        File.WriteAllText(Path.Combine(aliasRoot, "sentinel.user"), "preserve-alias");
        using DownloadManager downloads = new(new StaticResponseHandler(Array.Empty<byte>()));
        LauncherManager manager = new(paths, new ProcessRunner(), downloads);
        manager.SweepInactive(active, new InstallLog(paths.LogsDirectory));
        Assert(Directory.Exists(paths.LauncherVersionRoot(active.ExecutableSha256)));
        Assert(Directory.Exists(paths.LauncherVersionRoot(previous.ExecutableSha256)));
        Assert(!Directory.Exists(paths.LauncherVersionRoot(removable.ExecutableSha256)));
        Assert(File.Exists(Path.Combine(paths.LauncherVersionRoot(
            unverified.ExecutableSha256), "sentinel.user")));
        Assert(File.Exists(Path.Combine(aliasRoot, "sentinel.user")));
    }

    private static void LauncherPublisherConfigurationIsValid()
    {
        string pin = ProductConstants.LauncherPublisherSpkiSha256;
        Assert(string.IsNullOrEmpty(pin) || ProductConstants.HashRegex().IsMatch(pin));
        if (string.IsNullOrEmpty(pin))
        {
            Expect<InstallerException>(() =>
                NativeMethods.VerifyLauncherPublisherSignature("does-not-exist.exe"));
        }
    }

    private static void LauncherCopyWordingIsConcise()
    {
        string source = File.ReadAllText(Path.Combine(RepositoryRoot(), "installer",
            "src", "UVSR.Installer", "MainForm.cs"));
        Assert(source.Contains("LauncherUi.CreateButton(\"Copy\")",
            StringComparison.Ordinal));
        Assert(source.Contains("AccessibleName = \"Copy operation details\"",
            StringComparison.Ordinal));
        Assert(!source.Contains("Copy Details", StringComparison.Ordinal));
        Assert(source.Contains("RendererBusyAction.Closing", StringComparison.Ordinal));
    }

    private static void LauncherVersionMetadataAgrees()
    {
        string root = RepositoryRoot();
        string project = File.ReadAllText(Path.Combine(root, "installer", "src",
            "UVSR.Installer", "UVSR.Installer.csproj"));
        string manifest = File.ReadAllText(Path.Combine(root, "installer", "src",
            "UVSR.Installer", "app.manifest"));
        Assert(project.Contains($"<Version>{ProductConstants.LauncherVersion}</Version>",
            StringComparison.Ordinal));
        Assert(project.Contains("<FileVersion>1.1.5.0</FileVersion>",
            StringComparison.Ordinal));
        Assert(manifest.Contains("version=\"1.1.5.0\"", StringComparison.Ordinal));
        Assert(ProductConstants.LauncherVersion == "1.1.5");
        Assert(ProductConstants.LauncherReleaseSequence == 6);
        using JsonDocument inputLock = JsonDocument.Parse(File.ReadAllText(
            Path.Combine(root, "installer", "launcher-input-lock-v1.json")));
        JsonElement lockRoot = inputLock.RootElement;
        Assert(lockRoot.GetProperty("schemaVersion").GetInt32() == 1);
        Assert(lockRoot.GetProperty("version").GetString() ==
               ProductConstants.LauncherVersion);
        Assert(lockRoot.GetProperty("releaseSequence").GetInt64() ==
               ProductConstants.LauncherReleaseSequence);
        Assert(ProductConstants.HashRegex().IsMatch(
            lockRoot.GetProperty("inputsSha256").GetString()!));
        string workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows",
            "windows-launcher.yml"));
        Assert(Regex.Matches(workflow, "- \\\"\\.gitattributes\\\"").Count == 2);
        Assert(Regex.Matches(workflow, "- \\\"tests/\\*\\*\\\"").Count == 2);
    }

    private static string RepositoryRoot()
    {
        DirectoryInfo? directory = new(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "installer", "src",
                    "UVSR.Installer", "UVSR.Installer.csproj")))
                return directory.FullName;
            directory = directory.Parent;
        }
        throw new InvalidOperationException("Could not locate the UVSR repository root.");
    }

    private static LauncherState CreateLauncherPackage(
        InstallerPaths paths,
        Guid installationId,
        long sequence,
        string version,
        string content,
        bool desktopShortcut)
    {
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(content);
        string hash = Hash(bytes);
        string root = paths.LauncherVersionRoot(hash);
        Directory.CreateDirectory(root);
        string executable = paths.LauncherExecutable(hash);
        File.WriteAllBytes(executable, bytes);
        DateTimeOffset installed = DateTimeOffset.UtcNow.AddMinutes(sequence);
        LauncherPackageManifest marker = new(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, installationId, sequence, version, hash,
            bytes.LongLength, installed);
        JsonStore.WriteAtomic(paths.LauncherPackageMarker(hash), marker);
        return new LauncherState(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, installationId, sequence, version, hash,
            desktopShortcut, installed);
    }

    private static void WritePackageFixture(string package)
    {
        string[] directories =
        {
            Path.Combine(package, "bin", "D3D12"),
            Path.Combine(package, "bin", "shaders"),
            Path.Combine(package, "bin", "licenses"),
            Path.Combine(package, "media", "fonts", "System"),
            Path.Combine(package, "media", "glTF-Sample-Assets", "Models"),
            Path.Combine(package, "media", "environments"),
            Path.Combine(package, "media", "uvsr", "noise")
        };
        foreach (string directory in directories)
            Directory.CreateDirectory(directory);
        File.WriteAllText(Path.Combine(package, "bin", "uvsr.exe"), "binary");
        string core = Path.Combine(package, "bin", "D3D12", "D3D12Core.dll");
        File.WriteAllText(core, "dll");
        File.WriteAllText(Path.Combine(package, "bin", "D3D12", "D3D12SDKLayers.dll"), "layers");
        File.WriteAllText(Path.Combine(package, "bin", "D3D12",
                "uvsr-runtime-contract.txt"),
            "schemaVersion=1\n" +
            $"sdkVersion={ProductConstants.D3D12AgilitySdkVersion}\n" +
            "sdkPath=.\\D3D12\\\n" +
            $"coreSha256={PayloadPackager.ComputeSha256(core)}\n");
        File.WriteAllText(Path.Combine(package, "bin", "third-party-notices.md"), "notice");
        File.WriteAllText(Path.Combine(package, "media", "fonts", "System", "CodexUI.ttf"), "font");
        File.WriteAllText(Path.Combine(package, "media", "fonts", "System", "CodexUI-Semibold.ttf"), "font");
        File.WriteAllText(Path.Combine(package, "media", "fonts", "System", "CodexUI-Bold.ttf"), "font");
        File.WriteAllText(Path.Combine(package, "bin", "shaders", "uvsr.bin"), "shader");
        File.WriteAllText(Path.Combine(package, "bin", "licenses", "license.txt"), "license");
        File.WriteAllText(Path.Combine(package, "media", "glTF-Sample-Assets", "Models", "scene.bin"), "scene");
        File.WriteAllText(Path.Combine(package, "media", "environments", "environment.hdr"), "environment");
        File.WriteAllText(Path.Combine(package, "media", "uvsr", "noise", "noise.bin"), "noise");
    }

    private static IReadOnlyList<PackageFile> BuildPackageFiles(string package)
    {
        return Directory.EnumerateFiles(package, "*", SearchOption.AllDirectories)
            .Select(path => new FileInfo(path))
            .Select(info => new PackageFile(
                Path.GetRelativePath(package, info.FullName).Replace('\\', '/'),
                info.Length,
                PayloadPackager.ComputeSha256(info.FullName)))
            .OrderBy(file => file.RelativePath, StringComparer.Ordinal)
            .ToArray();
    }

    private static DownloadPolicy FastDownloadPolicy(int maximumAttempts) => new(
        maximumAttempts,
        TimeSpan.FromSeconds(1),
        TimeSpan.FromSeconds(1),
        TimeSpan.FromSeconds(10),
        Enumerable.Repeat(TimeSpan.Zero, Math.Max(0, maximumAttempts - 1)).ToArray());

    private static string RunGit(
        string workingDirectory,
        IReadOnlyDictionary<string, string>? additionalEnvironment,
        params string[] arguments)
    {
        ProcessStartInfo start = new()
        {
            FileName = (GetGitExecutable()),
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        start.Environment["GIT_CONFIG_NOSYSTEM"] = "1";
        start.Environment["GIT_CONFIG_GLOBAL"] = "NUL";
        start.Environment["GIT_NO_REPLACE_OBJECTS"] = "1";
        start.Environment["LC_ALL"] = "C";
        start.Environment["LANG"] = "C";
        if (additionalEnvironment is not null)
        {
            foreach ((string name, string value) in additionalEnvironment)
                start.Environment[name] = value;
        }
        foreach (string argument in arguments)
            start.ArgumentList.Add(argument);
        using Process process = Process.Start(start)
            ?? throw new InvalidOperationException("Could not start Git fixture.");
        string output = process.StandardOutput.ReadToEnd();
        string error = process.StandardError.ReadToEnd();
        process.WaitForExit();
        if (process.ExitCode != 0)
            throw new InvalidOperationException(
                $"Git fixture failed ({process.ExitCode}): {error}");
        return output;
    }

    private static string GetGitExecutable()
    {
        string installed = Path.Combine(Environment.GetFolderPath(
                Environment.SpecialFolder.LocalApplicationData), "Programs", "UVSR",
            "tools", "git", "cmd", "git.exe");
        if (File.Exists(installed))
            return installed;
        string? path = Environment.GetEnvironmentVariable("PATH");
        string? discovered = path?.Split(Path.PathSeparator,
                StringSplitOptions.RemoveEmptyEntries)
            .Select(directory => Path.Combine(directory.Trim('"'), "git.exe"))
            .FirstOrDefault(File.Exists);
        return discovered ?? "git.exe";
    }

    private static HttpResponseMessage Response(
        HttpRequestMessage request,
        HttpStatusCode status,
        byte[] payload)
    {
        HttpResponseMessage response = new(status)
        {
            RequestMessage = request,
            Content = new ByteArrayContent(payload)
        };
        response.Content.Headers.ContentLength = payload.Length;
        return response;
    }

    private static HttpResponseMessage ResponseWithoutLength(
        HttpRequestMessage request,
        HttpStatusCode status,
        byte[] payload)
    {
        HttpResponseMessage response = new(status)
        {
            RequestMessage = request,
            Content = new StreamContent(new MemoryStream(payload, writable: false))
        };
        response.Content.Headers.ContentLength = null;
        return response;
    }

    private static HttpResponseMessage InterruptibleResponse(
        HttpRequestMessage request,
        byte[] payload,
        int successfulBytes,
        string entityTag)
    {
        HttpResponseMessage response = new(HttpStatusCode.OK)
        {
            RequestMessage = request,
            Content = new StreamContent(new ThrowAfterStream(payload, successfulBytes))
        };
        response.Content.Headers.ContentLength = payload.Length;
        response.Headers.ETag = new System.Net.Http.Headers.EntityTagHeaderValue(entityTag);
        return response;
    }

    private static string Hash(byte[] payload) =>
        Convert.ToHexString(SHA256.HashData(payload)).ToLowerInvariant();

    private static string ValidLauncherFeedJson() =>
        "{\"schemaVersion\":1," +
        "\"productId\":\"" + ProductConstants.ProductId + "\"," +
        "\"channel\":\"stable\",\"releaseSequence\":1," +
        "\"version\":\"1.1.0\",\"artifact\":{" +
        "\"name\":\"UVSR-Launcher-Windows-11-x64.exe\"," +
        "\"size\":1024,\"sha256\":\"" + new string('a', 64) + "\"}}";

    private static void CreateJunction(string link, string target)
    {
        System.Diagnostics.ProcessStartInfo start = new()
        {
            FileName = Path.Combine(Environment.SystemDirectory, "cmd.exe"),
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        start.ArgumentList.Add("/d");
        start.ArgumentList.Add("/c");
        start.ArgumentList.Add("mklink");
        start.ArgumentList.Add("/J");
        start.ArgumentList.Add(link);
        start.ArgumentList.Add(target);
        using System.Diagnostics.Process process = System.Diagnostics.Process.Start(start)
            ?? throw new InvalidOperationException("Could not start junction fixture helper.");
        process.WaitForExit();
        if (process.ExitCode != 0)
            throw new InvalidOperationException(
                "Could not create junction fixture: " + process.StandardError.ReadToEnd());
    }

    private static void Expect<T>(Action action) where T : Exception
    {
        _ = Capture<T>(action);
    }

    private static T Capture<T>(Action action) where T : Exception
    {
        try
        {
            action();
        }
        catch (T exception)
        {
            return exception;
        }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static void AssertJsonPropertySet(byte[] json, params string[] expected)
    {
        using JsonDocument document = JsonDocument.Parse(json);
        HashSet<string> actual = document.RootElement.EnumerateObject()
            .Select(property => property.Name)
            .ToHashSet(StringComparer.Ordinal);
        Assert(actual.Count == expected.Length && actual.SetEquals(expected));
    }

    private static void Assert(bool condition)
    {
        if (!condition)
            throw new InvalidOperationException("Assertion failed.");
    }

    private sealed class TestDirectory : IDisposable
    {
        internal TestDirectory()
        {
            string parent = Path.Combine(Path.GetTempPath(), "UVSR-Installer-Tests");
            Directory.CreateDirectory(parent);
            Root = Path.Combine(parent, Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Root);
        }

        internal string Root { get; }

        public void Dispose()
        {
            string parent = Path.GetDirectoryName(Root)!;
            if (Directory.Exists(Root))
                SafePaths.DeleteOwnedTree(Root, parent);
        }
    }

    private sealed class StaticResponseHandler(byte[] payload) : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(
            HttpRequestMessage request,
            CancellationToken cancellationToken)
        {
            HttpResponseMessage response = new(HttpStatusCode.OK)
            {
                RequestMessage = request,
                Content = new ByteArrayContent(payload)
            };
            response.Content.Headers.ContentLength = payload.Length;
            return Task.FromResult(response);
        }
    }

    private sealed class SequenceResponseHandler(
        Func<HttpRequestMessage, int, HttpResponseMessage> factory) : HttpMessageHandler
    {
        private int _calls;

        protected override Task<HttpResponseMessage> SendAsync(
            HttpRequestMessage request,
            CancellationToken cancellationToken)
        {
            int call = Interlocked.Increment(ref _calls);
            return Task.FromResult(factory(request, call));
        }
    }

    private sealed class ThrowAfterStream(byte[] payload, int successfulBytes) : Stream
    {
        private int _position;

        public override bool CanRead => true;
        public override bool CanSeek => false;
        public override bool CanWrite => false;
        public override long Length => payload.Length;
        public override long Position { get => _position; set => throw new NotSupportedException(); }
        public override void Flush() { }
        public override int Read(byte[] buffer, int offset, int count) =>
            ReadAsync(buffer.AsMemory(offset, count)).AsTask().GetAwaiter().GetResult();
        public override ValueTask<int> ReadAsync(
            Memory<byte> buffer,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (_position >= successfulBytes)
                throw new IOException("fixture connection reset");
            int count = Math.Min(buffer.Length, successfulBytes - _position);
            payload.AsMemory(_position, count).CopyTo(buffer);
            _position += count;
            return ValueTask.FromResult(count);
        }
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
    }

    private sealed class StallingStream : Stream
    {
        public override bool CanRead => true;
        public override bool CanSeek => false;
        public override bool CanWrite => false;
        public override long Length => 0;
        public override long Position { get => 0; set => throw new NotSupportedException(); }
        public override void Flush() { }
        public override int Read(byte[] buffer, int offset, int count) =>
            throw new NotSupportedException();
        public override async ValueTask<int> ReadAsync(
            Memory<byte> buffer,
            CancellationToken cancellationToken = default)
        {
            await Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken);
            return 0;
        }
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
    }

    private sealed class InlineProgress(Action<InstallerProgress> report) :
        IProgress<InstallerProgress>
    {
        public void Report(InstallerProgress value) => report(value);
    }
}
