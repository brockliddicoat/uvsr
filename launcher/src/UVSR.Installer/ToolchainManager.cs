using System.Security.Cryptography;
using System.Text.Json;
using Microsoft.Win32;

namespace UvsrInstaller;

internal enum VisualStudioRecoveryDecision
{
    None,
    ResumeIdempotentOperation,
    ConfirmInstallRetry,
    WaitForRunningOperation,
    FailClosed,
    CleanupCompletedOperation
}

internal sealed class ToolchainManager
{
    private const string LayoutAction = "layout";
    private const string VerifyAction = "verify";
    private const string FixAction = "fix";
    private const string InstallAction = "install";
    private static readonly TimeSpan[] VisualStudioNetworkRetryDelays =
    {
        TimeSpan.FromSeconds(5),
        TimeSpan.FromSeconds(15)
    };
    private const string VisualStudioComponentSet =
        "Microsoft.VisualStudio.Workload.VCTools;" +
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64;" +
        "Microsoft.VisualStudio.Component.Windows11SDK.26100;" +
        "Microsoft.VisualStudio.Component.VC.Redist.14.Latest;en-US";
    private static readonly string[] VisualStudioComponents =
    {
        "Microsoft.VisualStudio.Workload.VCTools",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "Microsoft.VisualStudio.Component.Windows11SDK.26100",
        "Microsoft.VisualStudio.Component.VC.Redist.14.Latest"
    };
    private sealed record VisualStudioInstance(string InstallationPath, string ChannelId);

    private readonly InstallerPaths _paths;
    private readonly ProcessRunner _runner;
    private readonly DownloadManager _downloads;

    internal ToolchainManager(
        InstallerPaths paths,
        ProcessRunner runner,
        DownloadManager downloads)
    {
        _paths = paths;
        _runner = runner;
        _downloads = downloads;
    }

    internal async Task<ToolPaths> EnsureAsync(
        Func<PromptRequest, Task<bool>> prompt,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        progress?.Report(new InstallerProgress("Preparing prerequisites",
            "Checking the launcher-managed build tools."));
        string git = await EnsurePortableToolAsync(ProductConstants.Git, progress, log, cancellationToken);
        string cmake = await EnsurePortableToolAsync(ProductConstants.CMake, progress, log, cancellationToken);
        string python = await EnsurePortableToolAsync(ProductConstants.Python, progress, log, cancellationToken);
        Dictionary<string, string> dependencies = new(StringComparer.Ordinal);
        foreach (PinnedArchivePackage package in ProductConstants.BuildDependencies)
            dependencies[package.DirectoryName] = await EnsurePinnedArchiveAsync(
                package, progress, log, cancellationToken);

        string? visualStudio = await FindCompleteVisualStudioAsync(log, cancellationToken);
        bool buildToolsApproved = false;
        if (File.Exists(_paths.VisualStudioLayoutRecord))
        {
            VisualStudioLayoutRecord recoveryRecord = ReadVisualStudioLayoutRecord();
            TrackedProcessState processState = recoveryRecord.ActiveOperation is null
                ? TrackedProcessState.NotRunning
                : ProcessInspector.InspectTrackedProcess(recoveryRecord.ActiveOperation);
            VisualStudioRecoveryDecision recovery = DecideVisualStudioRecovery(
                recoveryRecord, processState, visualStudio is not null);
            switch (recovery)
            {
                case VisualStudioRecoveryDecision.WaitForRunningOperation:
                    throw new InstallerException(
                        "Microsoft setup is still running with administrator approval. " +
                        "Let it finish, then open UVSR Launcher again.");
                case VisualStudioRecoveryDecision.FailClosed:
                    throw new InstallerException(
                        "UVSR Launcher could not safely identify the previous elevated Microsoft setup process. " +
                        "Let Microsoft setup finish or close it from Visual Studio Installer, then open UVSR Launcher again.");
                case VisualStudioRecoveryDecision.ResumeIdempotentOperation:
                    recoveryRecord = NormalizeEndedIdempotentOperation(recoveryRecord);
                    JsonStore.WriteAtomic(_paths.VisualStudioLayoutRecord, recoveryRecord);
                    log.Write("Recovered an interrupted Microsoft Build Tools layout operation without repeating an installation.");
                    break;
                case VisualStudioRecoveryDecision.ConfirmInstallRetry:
                    buildToolsApproved = await prompt(new PromptRequest(
                        "Previous Microsoft Setup Did Not Finish",
                        "Microsoft's previous Build Tools setup is no longer running, but the required components are incomplete. " +
                        "Only continue after any Visual Studio Installer window has closed. Retry the verified local installation?"));
                    if (!buildToolsApproved)
                        throw new InstallerException(
                            "Microsoft Build Tools recovery was cancelled. The existing UVSR installation was preserved.");
                    recoveryRecord = AuthorizeInstallRetry(recoveryRecord);
                    JsonStore.WriteAtomic(_paths.VisualStudioLayoutRecord, recoveryRecord);
                    log.Write("The user explicitly approved retrying an incomplete Microsoft Build Tools installation.");
                    break;
                case VisualStudioRecoveryDecision.CleanupCompletedOperation:
                    if (recoveryRecord.ActiveOperation is not null)
                    {
                        recoveryRecord = recoveryRecord with { ActiveOperation = null };
                        JsonStore.WriteAtomic(_paths.VisualStudioLayoutRecord, recoveryRecord);
                    }
                    break;
            }
        }
        if (visualStudio is null)
        {
            if (!buildToolsApproved)
                buildToolsApproved = await prompt(new PromptRequest(
                    "Microsoft C++ Build Tools Required",
                    "UVSR needs Microsoft's Visual Studio 2022 C++ Build Tools and Windows 11 SDK to build from source. " +
                    "Microsoft's signed setup will request administrator approval and may download several gigabytes. " +
                    "The tools are shared Windows developer components, are governed by the terms shown by Microsoft setup, " +
                    "and will remain installed if UVSR is removed. Continue?"));
            if (!buildToolsApproved)
                throw new InstallerException("Microsoft build-prerequisite installation was cancelled. The existing UVSR installation was preserved.");
            VisualStudioInstance? existingBuildTools = await FindBuildToolsInstanceAsync(log, cancellationToken);
            await InstallVisualStudioBuildToolsAsync(existingBuildTools,
                progress, log, cancellationToken);
            visualStudio = await FindCompleteVisualStudioAsync(log, cancellationToken);
            if (visualStudio is null)
                throw new InstallerException("Microsoft C++ Build Tools or the Windows SDK could not be detected after setup completed.");
        }
        CleanupVerifiedVisualStudioLayout(log);

        if (!IsVisualCppRuntimeInstalled(visualStudio))
        {
            bool accepted = await prompt(new PromptRequest(
                "Microsoft Visual C++ Runtime Required",
                "UVSR needs Microsoft's signed Visual C++ runtime. Windows will request administrator approval, and " +
                "Microsoft's redistribution terms apply. Continue?"));
            if (!accepted)
                throw new InstallerException("Microsoft Visual C++ runtime installation was cancelled. The existing UVSR installation was preserved.");
            await InstallVisualCppRuntimeAsync(progress, log, cancellationToken);
            if (!IsVisualCppRuntimeInstalled(visualStudio))
                throw new InstallerException("The Microsoft Visual C++ runtime could not be detected after setup completed.");
        }

        log.Write($"Using Visual Studio instance: {visualStudio}");
        log.Write($"Using Windows SDK: {FindWindowsSdk26100()}");
        return new ToolPaths(git, cmake, python, visualStudio,
            dependencies[ProductConstants.AgilitySdk.DirectoryName],
            dependencies[ProductConstants.DirectXHeaders.DirectoryName],
            dependencies[ProductConstants.Dxc.DirectoryName]);
    }

    private async Task<string> EnsurePinnedArchiveAsync(
        PinnedArchivePackage package,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        string dependenciesRoot = Path.Combine(_paths.ToolsDirectory, "build-dependencies");
        string finalRoot = Path.Combine(dependenciesRoot, package.DirectoryName);
        SafePaths.RejectReparsePathChain(dependenciesRoot,
            "launcher-managed build dependencies directory");
        Directory.CreateDirectory(dependenciesRoot);
        SafePaths.RejectReparsePathChain(dependenciesRoot,
            "launcher-managed build dependencies directory");
        if (Directory.Exists(finalRoot))
        {
            SafePaths.DeleteOwnedTree(finalRoot, dependenciesRoot);
            log.Write($"Refreshing {package.DisplayName} from its verified archive.");
        }

        Directory.CreateDirectory(_paths.DownloadsDirectory);
        SafePaths.RejectReparsePathChain(_paths.DownloadsDirectory,
            "launcher-managed downloads directory");
        string extension = Path.GetExtension(package.DownloadUri.AbsolutePath);
        string archive = Path.Combine(_paths.DownloadsDirectory,
            package.DirectoryName + (string.IsNullOrWhiteSpace(extension) ? ".zip" : extension));
        if (!File.Exists(archive) || !HashMatches(archive, package.Sha256))
        {
            if (File.Exists(archive))
                File.Delete(archive);
            progress?.Report(new InstallerProgress("Downloading build components",
                $"Downloading {package.DisplayName} {package.Version}."));
            await _downloads.DownloadAndVerifyAsync(package.DownloadUri, archive,
                package.Sha256, package.MaximumBytes, progress, log, cancellationToken,
                phase: "Downloading build components");
        }
        else
        {
            log.Write($"Reusing verified {package.DisplayName} archive.");
        }

        string stage = Path.Combine(dependenciesRoot,
            $".{package.DirectoryName}-{Guid.NewGuid():N}.staging");
        try
        {
            SafePaths.ExtractVerifiedZip(archive, stage);
            string content = string.IsNullOrEmpty(package.ContentRelativeRoot)
                ? stage
                : SafePaths.CombineDescendant(stage, package.ContentRelativeRoot);
            foreach (string required in package.RequiredRelativePaths)
            {
                string path = SafePaths.CombineDescendant(content,
                    required.Replace('/', Path.DirectorySeparatorChar));
                if (!File.Exists(path))
                    throw new InstallerException(
                        $"The verified {package.DisplayName} archive was missing {required}.");
            }
            Directory.Move(stage, finalRoot);
            return string.IsNullOrEmpty(package.ContentRelativeRoot)
                ? finalRoot
                : SafePaths.CombineDescendant(finalRoot, package.ContentRelativeRoot);
        }
        finally
        {
            if (Directory.Exists(stage))
                SafePaths.DeleteOwnedTree(stage, dependenciesRoot);
        }
    }

    private async Task<string> EnsurePortableToolAsync(
        ToolPackage package,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        string finalRoot = Path.Combine(_paths.ToolsDirectory, package.DirectoryName);
        string executable = Path.Combine(finalRoot, package.ExecutableRelativePath);
        SafePaths.RejectReparsePathChain(_paths.ToolsDirectory,
            "launcher-managed tools directory");
        SafePaths.RejectReparsePathChain(finalRoot,
            $"managed {package.DisplayName} directory");
        if (Directory.Exists(finalRoot))
        {
            SafePaths.RejectReparsePoint(finalRoot, $"managed {package.DisplayName} directory");
            // Re-extract every portable tool from its pinned, fully hashed archive.
            // This keeps corrupt DLLs, Git helpers, CMake modules, or Python library
            // files from surviving merely because the entry executable still runs.
            SafePaths.DeleteOwnedTree(finalRoot, _paths.ToolsDirectory);
            log.Write($"Refreshing {package.DisplayName} from its verified local archive.");
        }

        SafePaths.RejectReparsePathChain(_paths.DownloadsDirectory,
            "launcher-managed downloads directory");
        Directory.CreateDirectory(_paths.DownloadsDirectory);
        Directory.CreateDirectory(_paths.ToolsDirectory);
        SafePaths.RejectReparsePathChain(_paths.DownloadsDirectory,
            "launcher-managed downloads directory");
        SafePaths.RejectReparsePathChain(_paths.ToolsDirectory,
            "launcher-managed tools directory");
        string archive = Path.Combine(_paths.DownloadsDirectory,
            $"{package.DirectoryName}-{package.Version}.zip");
        SafePaths.RejectReparsePathChain(archive,
            $"managed {package.DisplayName} archive");
        if (!File.Exists(archive) || !HashMatches(archive, package.Sha256))
        {
            if (File.Exists(archive))
                File.Delete(archive);
            progress?.Report(new InstallerProgress("Downloading prerequisites",
                $"Downloading {package.DisplayName} {package.Version}."));
            await _downloads.DownloadAndVerifyAsync(package.DownloadUri, archive,
                package.Sha256, package.MaximumBytes, progress, log, cancellationToken);
        }
        else
        {
            log.Write($"Reusing verified {package.DisplayName} archive.");
        }

        string stage = Path.Combine(_paths.ToolsDirectory,
            $".{package.DirectoryName}-{Guid.NewGuid():N}.staging");
        try
        {
            SafePaths.ExtractVerifiedZip(archive, stage);
            string stagedExecutable = Path.Combine(stage, package.ExecutableRelativePath);
            if (!File.Exists(stagedExecutable))
                throw new InstallerException($"The verified {package.DisplayName} archive did not contain its expected program.");
            await ValidateToolAsync(package, stagedExecutable, log, cancellationToken);
            JsonStore.WriteAtomic(Path.Combine(stage, ".uvsr-tool.json"),
                ExpectedMarker(package, ComputeSha256(stagedExecutable)));
            Directory.Move(stage, finalRoot);
            return executable;
        }
        finally
        {
            if (Directory.Exists(stage))
                SafePaths.DeleteOwnedTree(stage, _paths.ToolsDirectory);
        }
    }

    private async Task ValidateToolAsync(
        ToolPackage package,
        string executable,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        ProcessResult result = await _runner.RunAsync(executable, new[] { "--version" },
            Path.GetDirectoryName(executable), null, log, cancellationToken);
        if (result.ExitCode != 0 || !result.StandardOutput.Contains(
                package.ExpectedVersionOutput, StringComparison.OrdinalIgnoreCase))
            throw new InstallerException($"The downloaded {package.DisplayName} did not pass its version check.");
    }

    private async Task<string?> FindCompleteVisualStudioAsync(
        InstallLog log,
        CancellationToken cancellationToken)
    {
        string vswhere = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            "Microsoft Visual Studio", "Installer", "vswhere.exe");
        if (!File.Exists(vswhere))
            return null;
        ProcessResult result = await _runner.RunAsync(vswhere, new[]
        {
            "-latest", "-products", "*", "-version", "[17.0,18.0)",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "Microsoft.VisualStudio.Component.Windows11SDK.26100",
            "-property", "installationPath"
        }, Path.GetDirectoryName(vswhere), null, log, cancellationToken);
        if (result.ExitCode != 0)
            return null;
        string path = result.StandardOutput.Split(new[] { '\r', '\n' },
            StringSplitOptions.RemoveEmptyEntries).LastOrDefault()?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(path) || !Directory.Exists(path))
            return null;
        string msbuild = Path.Combine(path, "MSBuild", "Current", "Bin", "MSBuild.exe");
        string? compiler = FindCompiler(path);
        return File.Exists(msbuild) && compiler is not null && FindWindowsSdk26100() is not null
            ? Path.GetFullPath(path)
            : null;
    }

    private async Task<VisualStudioInstance?> FindBuildToolsInstanceAsync(
        InstallLog log,
        CancellationToken cancellationToken)
    {
        string vswhere = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            "Microsoft Visual Studio", "Installer", "vswhere.exe");
        if (!File.Exists(vswhere))
            return null;
        ProcessResult result = await _runner.RunAsync(vswhere, new[]
        {
            "-latest", "-products", "Microsoft.VisualStudio.Product.BuildTools",
            "-version", "[17.0,18.0)", "-format", "json", "-utf8"
        }, Path.GetDirectoryName(vswhere), null, log, cancellationToken);
        if (result.ExitCode != 0)
            return null;
        try
        {
            using JsonDocument document = JsonDocument.Parse(result.StandardOutput);
            JsonElement first = document.RootElement.EnumerateArray().FirstOrDefault();
            if (first.ValueKind != JsonValueKind.Object ||
                !first.TryGetProperty("installationPath", out JsonElement pathElement) ||
                !first.TryGetProperty("channelId", out JsonElement channelElement))
                return null;
            string path = pathElement.GetString() ?? string.Empty;
            string channel = channelElement.GetString() ?? string.Empty;
            if (!Directory.Exists(path) || channel.Length is < 1 or > 200 ||
                channel.Any(character => !(char.IsAsciiLetterOrDigit(character) ||
                    character is '.' or '_' or '-')))
                return null;
            return new VisualStudioInstance(Path.GetFullPath(path), channel);
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private static string? FindCompiler(string visualStudio)
    {
        string tools = Path.Combine(visualStudio, "VC", "Tools", "MSVC");
        if (!Directory.Exists(tools))
            return null;
        return Directory.EnumerateDirectories(tools)
            .OrderByDescending(Path.GetFileName, StringComparer.OrdinalIgnoreCase)
            .Select(path => Path.Combine(path, "bin", "Hostx64", "x64", "cl.exe"))
            .FirstOrDefault(File.Exists);
    }

    private static string? FindWindowsSdk26100()
    {
        string? kitsRoot = null;
        foreach (RegistryView view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
        {
            using RegistryKey baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
            using RegistryKey? key = baseKey.OpenSubKey(
                @"SOFTWARE\Microsoft\Windows Kits\Installed Roots");
            kitsRoot = key?.GetValue("KitsRoot10") as string;
            if (!string.IsNullOrWhiteSpace(kitsRoot))
                break;
        }
        kitsRoot ??= Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            "Windows Kits", "10");
        string includeRoot = Path.Combine(kitsRoot, "Include");
        if (!Directory.Exists(includeRoot))
            return null;
        return Directory.EnumerateDirectories(includeRoot)
            .Select(path => new { Path = path, Parsed = Version.TryParse(Path.GetFileName(path), out Version? version), Version = version })
            .Where(item => item.Parsed && item.Version is not null &&
                           item.Version >= new Version(10, 0, 26100, 0))
            .Where(item => File.Exists(Path.Combine(item.Path, "um", "Windows.h")) &&
                           File.Exists(Path.Combine(kitsRoot, "Lib",
                                Path.GetFileName(item.Path), "um", "x64", "kernel32.lib")))
            .OrderByDescending(item => item.Version)
            .Select(item => item.Path)
            .FirstOrDefault();
    }

    private async Task InstallVisualStudioBuildToolsAsync(
        VisualStudioInstance? existingBuildTools,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        if (existingBuildTools is not null &&
            !string.Equals(existingBuildTools.ChannelId, "VisualStudio.17.Release",
                StringComparison.OrdinalIgnoreCase))
            throw new InstallerException(
                "The existing Visual Studio Build Tools use a private or long-term servicing channel that this launcher cannot modify safely. Update that instance with Visual Studio Installer, then choose Install again.");

        string bootstrapper = Path.Combine(_paths.DownloadsDirectory, "vs_buildtools.exe");
        progress?.Report(new InstallerProgress("Preparing Microsoft Build Tools",
            "Downloading Microsoft's signed setup program."));
        await _downloads.DownloadAndVerifyAsync(ProductConstants.VisualStudioBuildTools,
            bootstrapper, null, 10L * 1024 * 1024, progress, log, cancellationToken,
            NativeMethods.VerifyMicrosoftSignature);
        string bootstrapperHash = ComputeSha256(bootstrapper);
        string installPath = existingBuildTools?.InstallationPath ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            "Microsoft Visual Studio", "2022", "BuildTools");
        await PrepareVisualStudioLayoutAsync(bootstrapper, bootstrapperHash,
            existingBuildTools?.ChannelId ?? "VisualStudio.17.Release",
            progress, log, cancellationToken);
        string layoutSetup = Path.Combine(_paths.VisualStudioLayoutDirectory,
            "vs_setup.exe");
        if (!File.Exists(layoutSetup))
            throw new InstallerException(
                "Microsoft's verified Build Tools layout did not contain its setup program.");
        progress?.Report(new InstallerProgress("Installing Microsoft Build Tools",
            "Approve the Windows prompt. The verified local files will now be installed without another download.",
            CanCancel: false));
        List<string> arguments = new();
        if (existingBuildTools is not null)
        {
            arguments.Add("modify");
            arguments.Add("--channelId");
            arguments.Add(existingBuildTools.ChannelId);
        }
        arguments.AddRange(new[] { "--quiet", "--wait", "--norestart", "--noWeb",
            "--installPath", installPath });
        AddVisualStudioComponents(arguments);
        ProcessResult result = await RunTrackedVisualStudioOperationAsync(
            layoutSetup, arguments, InstallAction, "installing",
            _paths.VisualStudioLayoutDirectory, log, cancellationToken);
        HandleMicrosoftInstallerExit(result.ExitCode);
    }

    private async Task PrepareVisualStudioLayoutAsync(
        string bootstrapper,
        string bootstrapperHash,
        string channelId,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        string layout = _paths.VisualStudioLayoutDirectory;
        if (layout.Length >= 80)
            throw new InstallerException(
                "This Windows profile path is too long for Microsoft's Build Tools download layout.");
        SafePaths.RejectReparsePathChain(layout,
            "Microsoft Build Tools layout directory");
        VisualStudioLayoutRecord record;
        if (File.Exists(_paths.VisualStudioLayoutRecord))
        {
            record = ReadVisualStudioLayoutRecord();
            if (!string.Equals(record.ChannelId, channelId,
                    StringComparison.OrdinalIgnoreCase))
                throw new InstallerException(
                    "The saved Microsoft Build Tools download belongs to a different Visual Studio channel.");
            if (record.Phase == "installing")
                throw new InstallerException(
                    "Microsoft Build Tools recovery must be confirmed before setup can run again.");

            string recordedSetup = Path.Combine(layout, "vs_setup.exe");
            if (!Directory.Exists(layout) ||
                (record.Phase is "downloaded" or "verified" &&
                 !File.Exists(recordedSetup)))
            {
                // The record is written before directory creation. Re-entering
                // this phase repairs that bounded crash window, and Microsoft's
                // layout command safely resumes any remaining owned packages.
                record = record with
                {
                    BootstrapperSha256 = bootstrapperHash,
                    Phase = "preparing",
                    ActiveOperation = null
                };
                JsonStore.WriteAtomic(_paths.VisualStudioLayoutRecord, record);
            }
            else if (record.Phase == "preparing" &&
                     !string.Equals(record.BootstrapperSha256, bootstrapperHash,
                         StringComparison.Ordinal))
            {
                record = record with { BootstrapperSha256 = bootstrapperHash };
                JsonStore.WriteAtomic(_paths.VisualStudioLayoutRecord, record);
            }
        }
        else
        {
            if (Directory.Exists(layout))
                throw new InstallerException(
                    "An unverified Microsoft Build Tools download directory already exists. No files were changed.");
            record = new VisualStudioLayoutRecord(ProductConstants.SchemaVersion,
                ProductConstants.ProductId, VisualStudioComponentSet,
                bootstrapperHash, channelId, layout, "preparing", DateTimeOffset.UtcNow);
            JsonStore.WriteAtomic(_paths.VisualStudioLayoutRecord, record);
        }

        if (record.Phase == "preparing")
        {
            Directory.CreateDirectory(layout);
            SafePaths.RejectReparsePathChain(layout,
                "Microsoft Build Tools layout directory");
            ProcessResult result = await RunIdempotentVisualStudioOperationWithRetryAsync(
                bootstrapper, BuildVisualStudioLayoutArguments(layout), LayoutAction,
                "preparing", _paths.DownloadsDirectory,
                "Downloading Microsoft Build Tools",
                "Approve the Windows prompt. Microsoft setup is preparing a resumable local download. This can take several minutes.",
                "Approve the Windows prompt again. Microsoft setup is resuming the local download.",
                progress, log, cancellationToken);
            if (result.ExitCode != 0)
                HandleMicrosoftInstallerExit(result.ExitCode);
            WriteVisualStudioPhase("downloaded");
            record = ReadVisualStudioLayoutRecord();
        }

        string layoutSetup = Path.Combine(layout, "vs_setup.exe");
        if (!File.Exists(layoutSetup))
            throw new InstallerException(
                "Microsoft's Build Tools download did not contain its setup program.");
        NativeMethods.VerifyMicrosoftSignature(layoutSetup);
        if (record.Phase is "downloaded" or "verified")
        {
            progress?.Report(new InstallerProgress("Verifying Microsoft Build Tools",
                "Approve the Windows prompt. Microsoft setup is checking every downloaded package.",
                CanCancel: false));
            ProcessResult verify = await RunTrackedVisualStudioOperationAsync(
                layoutSetup, BuildVisualStudioMaintenanceArguments("--verify"),
                VerifyAction, record.Phase, layout, log, cancellationToken);
            if (verify.ExitCode != 0)
            {
                WriteVisualStudioPhase("downloaded");
                ProcessResult fix = await RunIdempotentVisualStudioOperationWithRetryAsync(
                    layoutSetup, BuildVisualStudioMaintenanceArguments("--fix"),
                    FixAction, "downloaded", layout,
                    "Repairing Microsoft Build Tools download",
                    "Approve the Windows prompt. Microsoft setup is repairing the local download, then it will verify it again.",
                    "Approve the Windows prompt again. Microsoft setup is resuming the local repair.",
                    progress, log, cancellationToken);
                if (fix.ExitCode != 0)
                    HandleMicrosoftInstallerExit(fix.ExitCode);
                progress?.Report(new InstallerProgress("Verifying Microsoft Build Tools",
                    "Approve the Windows prompt. Microsoft setup is checking the repaired download.",
                    CanCancel: false));
                verify = await RunTrackedVisualStudioOperationAsync(
                    layoutSetup, BuildVisualStudioMaintenanceArguments("--verify"),
                    VerifyAction, "downloaded", layout, log, cancellationToken);
                if (verify.ExitCode != 0)
                    throw new InstallerException(
                        "Microsoft's Build Tools download could not be verified after repair.");
            }
            WriteVisualStudioPhase("verified");
        }
    }

    private string[] BuildVisualStudioMaintenanceArguments(string action) =>
        new[] { "--layout", _paths.VisualStudioLayoutDirectory, action,
            "--passive", "--wait", "--norestart" };

    private async Task<ProcessResult> RunIdempotentVisualStudioOperationWithRetryAsync(
        string executable,
        IReadOnlyList<string> arguments,
        string action,
        string recordPhase,
        string workingDirectory,
        string progressPhase,
        string firstDetail,
        string retryDetail,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        ProcessResult? result = null;
        for (int attempt = 1; attempt <= 3; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            progress?.Report(new InstallerProgress(progressPhase,
                attempt == 1
                    ? firstDetail
                    : $"{retryDetail} Attempt {attempt} of 3.",
                CanCancel: false));
            result = await RunTrackedVisualStudioOperationAsync(
                executable, arguments, action, recordPhase, workingDirectory,
                log, cancellationToken);
            if (result.ExitCode == 0 ||
                !IsRetryableVisualStudioLayoutExit(result.ExitCode) ||
                attempt == 3)
                return result;

            TimeSpan delay = VisualStudioNetworkRetryDelays[attempt - 1];
            progress?.Report(new InstallerProgress("Connection stalled",
                $"Microsoft's Build Tools transfer was interrupted. Retrying automatically in {(int)delay.TotalSeconds} seconds.",
                CanCancel: true));
            await Task.Delay(delay, cancellationToken);
        }
        return result ?? throw new InstallerException(
            "Microsoft setup did not start its resumable operation.");
    }

    private async Task<ProcessResult> RunTrackedVisualStudioOperationAsync(
        string executable,
        IReadOnlyList<string> arguments,
        string action,
        string recordPhase,
        string workingDirectory,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        string executablePath = Path.GetFullPath(executable);
        SafePaths.RejectReparsePathChain(executablePath,
            "Microsoft setup program");
        NativeMethods.VerifyMicrosoftSignature(executablePath);
        string executableSha256 = ComputeSha256(executablePath);

        VisualStudioLayoutRecord record = ReadVisualStudioLayoutRecord();
        if (record.ActiveOperation is not null)
            throw new InstallerException(
                "A recorded Microsoft setup operation must finish before another one can start.");
        VisualStudioOperationRecord operation = new(
            action,
            executablePath,
            executableSha256,
            null,
            null,
            DateTimeOffset.UtcNow);
        VisualStudioLayoutRecord pending = record with
        {
            Phase = recordPhase,
            ActiveOperation = operation
        };
        if (!IsValidVisualStudioLayoutRecord(pending,
                _paths.VisualStudioLayoutDirectory,
                Path.Combine(_paths.DownloadsDirectory, "vs_buildtools.exe")))
            throw new InstallerException(
                "UVSR Launcher refused an invalid Microsoft setup operation record.");
        JsonStore.WriteAtomic(_paths.VisualStudioLayoutRecord, pending);

        try
        {
            return await _runner.RunAsync(executablePath, arguments,
                workingDirectory, null, log, cancellationToken, elevate: true,
                onElevatedStarted: identity =>
                    RecordVisualStudioOperationStarted(operation, identity));
        }
        finally
        {
            // ProcessRunner returns or throws only before process creation or
            // after the authoritative elevated process has exited. A hard
            // launcher crash skips this finally and deliberately leaves the
            // durable identity for the next launch to inspect.
            ClearVisualStudioOperation(operation);
        }
    }

    private void RecordVisualStudioOperationStarted(
        VisualStudioOperationRecord operation,
        ElevatedProcessIdentity identity)
    {
        if (identity.ProcessId <= 0 || identity.CreationTimeUtcFileTime <= 0 ||
            !string.Equals(Path.GetFullPath(identity.ExecutablePath),
                Path.GetFullPath(operation.ExecutablePath),
                StringComparison.OrdinalIgnoreCase))
            throw new InstallerException(
                "Windows returned an invalid Microsoft setup process identity.");

        VisualStudioLayoutRecord current = ReadVisualStudioLayoutRecord();
        if (!IsSameVisualStudioOperation(current.ActiveOperation, operation))
            throw new InstallerException(
                "The Microsoft setup recovery record changed unexpectedly.");
        VisualStudioLayoutRecord attributed = current with
        {
            ActiveOperation = operation with
            {
                ProcessId = identity.ProcessId,
                CreationTimeUtcFileTime = identity.CreationTimeUtcFileTime
            }
        };
        if (!IsValidVisualStudioLayoutRecord(attributed,
                _paths.VisualStudioLayoutDirectory,
                Path.Combine(_paths.DownloadsDirectory, "vs_buildtools.exe")))
            throw new InstallerException(
                "Windows returned an invalid Microsoft setup process identity.");
        JsonStore.WriteAtomic(_paths.VisualStudioLayoutRecord, attributed);
    }

    private void ClearVisualStudioOperation(VisualStudioOperationRecord operation)
    {
        VisualStudioLayoutRecord current = ReadVisualStudioLayoutRecord();
        if (current.ActiveOperation is null)
            return;
        if (!IsSameVisualStudioOperation(current.ActiveOperation, operation))
            throw new InstallerException(
                "The Microsoft setup recovery record changed unexpectedly. Its owned files were preserved.");
        JsonStore.WriteAtomic(_paths.VisualStudioLayoutRecord,
            current with { ActiveOperation = null });
    }

    private void WriteVisualStudioPhase(string phase)
    {
        VisualStudioLayoutRecord current = ReadVisualStudioLayoutRecord();
        if (current.ActiveOperation is not null)
            throw new InstallerException(
                "Microsoft setup is still recorded as active, so its download state was preserved.");
        VisualStudioLayoutRecord updated = current with { Phase = phase };
        if (!IsValidVisualStudioLayoutRecord(updated,
                _paths.VisualStudioLayoutDirectory,
                Path.Combine(_paths.DownloadsDirectory, "vs_buildtools.exe")))
            throw new InstallerException(
                "UVSR Launcher refused an invalid Microsoft setup state transition.");
        JsonStore.WriteAtomic(_paths.VisualStudioLayoutRecord, updated);
    }

    internal static bool IsSameVisualStudioOperation(
        VisualStudioOperationRecord? actual,
        VisualStudioOperationRecord expected)
    {
        try
        {
            return actual is not null &&
                   string.Equals(actual.Action, expected.Action,
                       StringComparison.Ordinal) &&
                   string.Equals(Path.GetFullPath(actual.ExecutablePath),
                       Path.GetFullPath(expected.ExecutablePath),
                       StringComparison.OrdinalIgnoreCase) &&
                   string.Equals(actual.ExecutableSha256,
                       expected.ExecutableSha256, StringComparison.Ordinal) &&
                   actual.StartedUtc == expected.StartedUtc;
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException)
        {
            return false;
        }
    }

    internal static VisualStudioRecoveryDecision DecideVisualStudioRecovery(
        VisualStudioLayoutRecord record,
        TrackedProcessState processState,
        bool completeInstanceDetected)
    {
        if (record.ActiveOperation is not null)
        {
            if (processState == TrackedProcessState.Running)
                return VisualStudioRecoveryDecision.WaitForRunningOperation;
            if (processState == TrackedProcessState.Unverifiable)
                return VisualStudioRecoveryDecision.FailClosed;
            if (completeInstanceDetected)
                return VisualStudioRecoveryDecision.CleanupCompletedOperation;
            return string.Equals(record.ActiveOperation.Action, InstallAction,
                    StringComparison.Ordinal)
                ? VisualStudioRecoveryDecision.ConfirmInstallRetry
                : VisualStudioRecoveryDecision.ResumeIdempotentOperation;
        }

        if (processState != TrackedProcessState.NotRunning)
            return VisualStudioRecoveryDecision.FailClosed;
        if (completeInstanceDetected)
            return VisualStudioRecoveryDecision.CleanupCompletedOperation;
        return record.Phase == "installing"
            ? VisualStudioRecoveryDecision.ConfirmInstallRetry
            : VisualStudioRecoveryDecision.None;
    }

    internal static VisualStudioLayoutRecord NormalizeEndedIdempotentOperation(
        VisualStudioLayoutRecord record)
    {
        string action = record.ActiveOperation?.Action ?? string.Empty;
        string phase = action switch
        {
            LayoutAction => "preparing",
            VerifyAction or FixAction => "downloaded",
            _ => throw new InstallerException(
                "The previous Microsoft setup operation cannot be resumed automatically.")
        };
        return record with { Phase = phase, ActiveOperation = null };
    }

    internal static VisualStudioLayoutRecord AuthorizeInstallRetry(
        VisualStudioLayoutRecord record)
    {
        if (record.ActiveOperation is not null &&
            !string.Equals(record.ActiveOperation.Action, InstallAction,
                StringComparison.Ordinal) ||
            record.ActiveOperation is null && record.Phase != "installing")
            throw new InstallerException(
                "The saved Microsoft setup state is not an incomplete installation.");
        return record with { Phase = "verified", ActiveOperation = null };
    }

    internal static bool IsValidVisualStudioLayoutRecord(
        VisualStudioLayoutRecord record,
        string expectedLayoutPath,
        string expectedBootstrapperPath)
    {
        try
        {
            if (record.SchemaVersion != ProductConstants.SchemaVersion ||
                !string.Equals(record.ProductId, ProductConstants.ProductId,
                    StringComparison.OrdinalIgnoreCase) ||
                record.ComponentSet != VisualStudioComponentSet ||
                !ProductConstants.HashRegex().IsMatch(record.BootstrapperSha256) ||
                record.ChannelId.Length is < 1 or > 200 ||
                record.ChannelId.Any(character =>
                    !(char.IsAsciiLetterOrDigit(character) ||
                      character is '.' or '_' or '-')) ||
                record.StartedUtc == default ||
                record.Phase is not ("preparing" or "downloaded" or
                    "verified" or "installing") ||
                !string.Equals(Path.GetFullPath(record.LayoutPath),
                    Path.GetFullPath(expectedLayoutPath),
                    StringComparison.OrdinalIgnoreCase))
                return false;

            VisualStudioOperationRecord? operation = record.ActiveOperation;
            if (operation is null)
                return true;
            if (!ProductConstants.HashRegex().IsMatch(operation.ExecutableSha256) ||
                operation.StartedUtc == default ||
                (operation.ProcessId is null) !=
                    (operation.CreationTimeUtcFileTime is null))
                return false;
            // The two nullable identity fields are both absent while the
            // ShellExecute call is pending, or both strictly positive once
            // Windows returns the exact elevated process.
            if (operation.ProcessId is not null &&
                (operation.ProcessId <= 0 ||
                 operation.CreationTimeUtcFileTime <= 0))
                return false;

            string expectedExecutable;
            switch (operation.Action)
            {
                case LayoutAction when record.Phase == "preparing":
                    expectedExecutable = expectedBootstrapperPath;
                    if (!string.Equals(operation.ExecutableSha256,
                            record.BootstrapperSha256, StringComparison.Ordinal))
                        return false;
                    break;
                case VerifyAction or FixAction when
                    record.Phase is "downloaded" or "verified":
                    expectedExecutable = Path.Combine(expectedLayoutPath,
                        "vs_setup.exe");
                    break;
                case InstallAction when record.Phase == "installing":
                    expectedExecutable = Path.Combine(expectedLayoutPath,
                        "vs_setup.exe");
                    break;
                default:
                    return false;
            }
            return string.Equals(Path.GetFullPath(operation.ExecutablePath),
                Path.GetFullPath(expectedExecutable),
                StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is ArgumentException or
                                   NotSupportedException or NullReferenceException)
        {
            return false;
        }
    }

    private VisualStudioLayoutRecord ReadVisualStudioLayoutRecord()
    {
        VisualStudioLayoutRecord record = JsonStore.Read<VisualStudioLayoutRecord>(
            _paths.VisualStudioLayoutRecord);
        if (!IsValidVisualStudioLayoutRecord(record,
                _paths.VisualStudioLayoutDirectory,
                Path.Combine(_paths.DownloadsDirectory, "vs_buildtools.exe")))
            throw new InstallerException(
                "The saved Microsoft Build Tools download record was invalid.");
        return record;
    }

    private static void AddVisualStudioComponents(List<string> arguments)
    {
        foreach (string component in VisualStudioComponents)
        {
            arguments.Add("--add");
            arguments.Add(component);
        }
    }

    internal static List<string> BuildVisualStudioLayoutArguments(string layout)
    {
        List<string> arguments = new()
        {
            "--layout", layout, "--lang", "en-US", "--passive", "--wait", "--norestart"
        };
        AddVisualStudioComponents(arguments);
        return arguments;
    }

    internal static bool IsRetryableVisualStudioLayoutExit(int exitCode) =>
        exitCode is 5003 or -1073720687;

    private void CleanupVerifiedVisualStudioLayout(InstallLog log)
    {
        if (!File.Exists(_paths.VisualStudioLayoutRecord))
            return;
        try
        {
            VisualStudioLayoutRecord record = ReadVisualStudioLayoutRecord();
            if (record.ActiveOperation is not null)
                throw new InstallerException(
                    "Microsoft setup is still recorded as active. Its local files were preserved.");
            if (Directory.Exists(_paths.VisualStudioLayoutDirectory))
                SafePaths.DeleteOwnedTree(_paths.VisualStudioLayoutDirectory,
                    _paths.ProgramRoot);
            File.Delete(_paths.VisualStudioLayoutRecord);
            log.Write("Removed the verified Microsoft Build Tools download layout after successful detection.");
        }
        catch (Exception ex) when (ex is InstallerException or IOException or UnauthorizedAccessException)
        {
            log.Write($"The verified Microsoft Build Tools download layout will be cleaned up later: {ex.Message}");
        }
    }

    private async Task InstallVisualCppRuntimeAsync(
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        string installer = Path.Combine(_paths.DownloadsDirectory, "vc_redist.x64.exe");
        progress?.Report(new InstallerProgress("Preparing Microsoft Visual C++ Runtime",
            "Downloading Microsoft's signed runtime setup program."));
        await _downloads.DownloadAndVerifyAsync(ProductConstants.VisualCppRedistributable,
            installer, null, 50L * 1024 * 1024, progress, log, cancellationToken,
            NativeMethods.VerifyMicrosoftSignature);
        progress?.Report(new InstallerProgress("Installing Microsoft Visual C++ Runtime",
            "Approve the Windows prompt. Microsoft setup must finish before this window can close.",
            CanCancel: false));
        ProcessResult result = await _runner.RunAsync(installer,
            new[] { "/install", "/quiet", "/norestart" }, _paths.DownloadsDirectory,
            null, log, cancellationToken, elevate: true);
        if (result.ExitCode == 1638)
            return;
        HandleMicrosoftInstallerExit(result.ExitCode);
    }

    private static void HandleMicrosoftInstallerExit(int exitCode)
    {
        if (exitCode == 0)
            return;
        if (exitCode is 1641 or 3010)
            throw new RebootRequiredException();
        string message = exitCode switch
        {
            1602 or 5004 => "Microsoft prerequisite setup was cancelled.",
            1001 or 1618 => "Another Windows installer is already running. Let it finish, then try again.",
            1003 or 8006 => "Visual Studio is in use. Close it, then try again.",
            5003 => "Microsoft prerequisite setup could not download its files. Check the internet connection and try again.",
            5005 => "Microsoft prerequisite setup could not read its command. Download a fresh UVSR Launcher and try again.",
            5007 or 8001 or 8002 or 8003 or 8010 => "Microsoft prerequisite setup reported that this computer is not compatible.",
            8004 => "Microsoft prerequisite setup could not use its installation directory.",
            8005 => "Microsoft prerequisite setup could not verify a downloaded file.",
            -1073720687 => "Microsoft prerequisite setup could not reach its download service. Check the internet connection and try again.",
            _ => $"Microsoft prerequisite setup failed with exit code {exitCode}."
        };
        throw new InstallerException(message);
    }

    private static bool IsVisualCppRuntimeInstalled(string visualStudio)
    {
        try
        {
            using RegistryKey baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
            using RegistryKey? key = baseKey.OpenSubKey(@"SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64");
            if (key?.GetValue("Installed") is not int installed || installed != 1 ||
                key.GetValue("Version") is not string installedVersion ||
                !Version.TryParse(installedVersion.TrimStart('v', 'V'), out Version? parsedInstalled))
                return false;

            string compilerRoot = Path.Combine(visualStudio, "VC", "Tools", "MSVC");
            Version? required = Directory.Exists(compilerRoot)
                ? Directory.EnumerateDirectories(compilerRoot)
                    .Select(Path.GetFileName)
                    .Select(value => Version.TryParse(value, out Version? version) ? version : null)
                    .Where(version => version is not null)
                    .OrderByDescending(version => version)
                    .FirstOrDefault()
                : null;
            return required is not null && parsedInstalled >= required;
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or System.Security.SecurityException)
        {
            return false;
        }
    }

    private static ToolInstallMarker ExpectedMarker(
        ToolPackage package,
        string executableSha256) => new(
        ProductConstants.SchemaVersion,
        package.DisplayName,
        package.Version,
        package.Sha256,
        package.ExecutableRelativePath,
        executableSha256);

    private static string ComputeSha256(string path)
    {
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    private static bool HashMatches(string path, string expected)
    {
        using FileStream stream = File.OpenRead(path);
        string actual = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
        return string.Equals(actual, expected, StringComparison.Ordinal);
    }
}
