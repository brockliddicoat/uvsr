using System.Security.Cryptography;
using System.Text.Json;

namespace UvsrInstaller;

internal sealed record SourceResolution(
    string SourceCommit,
    string PublicBaseCommit,
    bool IsAlreadyInstalled,
    bool IsNonFastForward,
    RendererSourceBridge? Bridge)
{
    internal string Commit => SourceCommit;
}

internal sealed class SourceManager
{
    internal const string NotoSansSourceRootRelativePath =
        "assets/fonts/noto-sans";
    internal static readonly IReadOnlyDictionary<string, (long Size, string Sha256)>
        RequiredNotoSansSourceFiles =
            new Dictionary<string, (long Size, string Sha256)>(StringComparer.Ordinal)
            {
                ["NotoSans-Regular.ttf"] =
                    (621572, "478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823"),
                ["NotoSans-SemiBold.ttf"] =
                    (625052, "a4e91fd530ac2b4ef5367240144ff37d7d65d66cf76f2e9a2187b93c676f92d0"),
                ["NotoSans-Bold.ttf"] =
                    (631484, "1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913"),
                ["OFL.txt"] =
                    (4396, "cee9892f9f0cc8fe882c9e9537ee6a89621d86ee7ceaf70b02e2b2b1c25c061a")
            };

    private static readonly TimeSpan[] NetworkRetryDelays =
    {
        TimeSpan.FromSeconds(2),
        TimeSpan.FromSeconds(4),
        TimeSpan.FromSeconds(8)
    };

    private readonly InstallerPaths _paths;
    private readonly ProcessRunner _runner;

    internal SourceManager(InstallerPaths paths, ProcessRunner runner)
    {
        _paths = paths;
        _runner = runner;
    }

    internal async Task<SourceResolution> ResolveMainAsync(
        ToolPaths tools,
        InstallState? installedState,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        progress?.Report(new InstallerProgress("Checking public UVSR main",
            "Contacting GitHub and resolving one exact source revision."));
        SafePaths.RejectReparsePathChain(_paths.CacheDirectory,
            "managed UVSR cache directory");
        Directory.CreateDirectory(_paths.CacheDirectory);
        SafePaths.RejectReparsePathChain(_paths.CacheDirectory,
            "managed UVSR cache directory");
        // A source checkout is executable input. Recreate it for every operation so
        // persistent replace refs, filters, grafts, submodule gitfiles, and local
        // configuration can never survive from an earlier or interrupted run.
        RecreateManagedSourceDirectory();
        await GitRequiredAsync(tools.Git, new[] { "init", _paths.SourceDirectory },
            _paths.CacheDirectory, tools, log, cancellationToken,
            "UVSR Launcher could not initialize its managed UVSR source checkout.");
        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "remote", "add", "origin", ProductConstants.RepositoryUrl },
            _paths.CacheDirectory, tools, log, cancellationToken,
            "UVSR Launcher could not configure the public UVSR source address.");

        WriteSafeRepositoryConfig();

        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "remote", "set-url", "origin", ProductConstants.RepositoryUrl },
            _paths.CacheDirectory, tools, log, cancellationToken,
            "The managed UVSR source remote could not be secured.");
        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "config", "core.longpaths", "true" }, _paths.CacheDirectory, tools,
            log, cancellationToken, "Git could not enable Windows long-path support.");

        ProcessResult fetch = await GitNetworkAsync(tools.Git, new[]
        {
            "-C", _paths.SourceDirectory, "fetch", "--force", "--prune", "--no-tags",
            "--depth=1", "origin",
            $"+{ProductConstants.RepositoryMainRef}:refs/remotes/origin/main"
        }, _paths.CacheDirectory, tools, progress, log, cancellationToken,
            "Downloading UVSR source");
        if (fetch.ExitCode != 0)
            throw new InstallerException(
                "GitHub's public UVSR main branch remained unavailable after several automatic attempts. " +
                "The installed version was preserved; try again in a moment.");

        ProcessResult revision = await GitAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "rev-parse", "refs/remotes/origin/main" }, _paths.CacheDirectory,
            tools, log, cancellationToken);
        string publicCommit = revision.StandardOutput.Trim().ToLowerInvariant();
        if (revision.ExitCode != 0 ||
            !ProductConstants.CommitRegex().IsMatch(publicCommit))
            throw new InstallerException("GitHub returned an invalid UVSR main revision.");

        RendererSourceBridge? bridge =
            RendererSourceBridgeRegistry.FindForPublicBase(publicCommit);
        if (bridge is not null)
        {
            bridge.LoadVerifiedPatch();
            ValidateSupportedRendererBuildContract(bridge.Contract,
                RendererSourceBridge.SourceCommit, log,
                $"embedded bridge '{RendererSourceBridge.BridgeId}'");
        }
        string sourceCommit = bridge is null
            ? publicCommit
            : RendererSourceBridge.SourceCommit;
        bool same = string.Equals(installedState?.Commit, sourceCommit,
            StringComparison.Ordinal);
        bool nonFastForward = false;
        if (installedState is not null && !same)
        {
            string installedPublicCommit =
                RendererSourceBridgeRegistry.MapSourceToPublicBase(installedState.Commit);
            if (!string.Equals(installedPublicCommit, publicCommit,
                    StringComparison.Ordinal))
            {
                ProcessResult deepen = await GitNetworkAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
                    "fetch", "--unshallow", "--no-tags", "origin", ProductConstants.RepositoryMainRef },
                    _paths.CacheDirectory, tools, progress, log, cancellationToken,
                    "Checking update history");
                if (deepen.ExitCode != 0)
                    throw new InstallerException(
                        "The launcher could not verify UVSR's update history after several automatic attempts. " +
                        "The installed version was preserved.");
                ProcessResult ancestry = await GitAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
                    "merge-base", "--is-ancestor", installedPublicCommit,
                    publicCommit }, _paths.CacheDirectory, tools, log,
                    cancellationToken);
                nonFastForward = ClassifyAncestryExitCode(ancestry.ExitCode);
            }
        }

        if (bridge is null)
            log.Write($"Resolved public UVSR main and selected renderer source to {publicCommit}.");
        else
            log.Write($"Resolved public UVSR main to {publicCommit}; selected exact " +
                      $"embedded bridge '{RendererSourceBridge.BridgeId}' " +
                      $"({RendererSourceBridge.PatchSha256}) and synthetic renderer " +
                      $"source {sourceCommit} with tree {RendererSourceBridge.SourceTree}.");
        return new SourceResolution(sourceCommit, publicCommit, same,
            nonFastForward, bridge);
    }

    internal static bool ClassifyAncestryExitCode(int exitCode) => exitCode switch
    {
        0 => false,
        1 => true,
        _ => throw new InstallerException(
            "Git could not verify UVSR's update history. The installed version was preserved; choose Update again to retry.")
    };

    internal async Task PrepareExactSourceAsync(
        ToolPaths tools,
        SourceResolution resolution,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        ValidateResolution(resolution);
        progress?.Report(new InstallerProgress("Preparing UVSR source",
            $"Checking out {resolution.SourceCommit[..7]} and its pinned dependencies."));
        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "checkout", "--detach", "--force", resolution.PublicBaseCommit },
            _paths.CacheDirectory,
            tools, log, cancellationToken, "The exact UVSR source revision could not be checked out.");
        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "clean", "-ffdqx" }, _paths.CacheDirectory, tools, log, cancellationToken,
            "The launcher-owned UVSR source cache could not be cleaned.");
        await ValidatePublicBaseCheckoutAsync(tools, resolution, log,
            cancellationToken);
        if (resolution.Bridge is null)
        {
            ValidateRendererBuildContractFromSource(resolution.PublicBaseCommit, log);
        }
        else
        {
            await ApplyRendererSourceBridgeAsync(tools, resolution, log,
                cancellationToken);
            ValidateRendererBuildContractFromSource(resolution.PublicBaseCommit, log);
        }
        ValidateBundledNotoSourceContractIfRequired(
            resolution, log, cancellationToken);
        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "submodule", "sync", "--recursive" }, _paths.CacheDirectory, tools,
            log, cancellationToken, "UVSR dependency addresses could not be synchronized.");
        ProcessResult submoduleUpdate = await GitNetworkAsync(tools.Git,
            new[] { "-C", _paths.SourceDirectory, "submodule", "update", "--init",
                "--recursive", "--force", "--depth=1" },
            _paths.CacheDirectory, tools, progress, log, cancellationToken,
            "Downloading UVSR dependencies");
        if (submoduleUpdate.ExitCode != 0)
            throw new InstallerException(
                "UVSR's pinned source dependencies remained unavailable after several automatic attempts.");
        SafePaths.RejectReparseTree(_paths.SourceDirectory,
            "managed UVSR source cache");
        ValidateManagedSubmoduleMetadata();
        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "submodule", "foreach", "--recursive", "--quiet", "git", "reset", "--hard" },
            _paths.CacheDirectory, tools, log, cancellationToken,
            "A pinned UVSR dependency could not be reset to its recorded revision.");
        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "submodule", "foreach", "--recursive", "--quiet", "git", "clean", "-ffdqx" },
            _paths.CacheDirectory, tools, log, cancellationToken,
            "Launcher-managed dependency build residue could not be removed safely.");

        ProcessResult submodules = await GitAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "submodule", "status", "--recursive" }, _paths.CacheDirectory, tools,
            log, cancellationToken);
        if (submodules.ExitCode != 0 || submodules.StandardOutput.Split(new[] { '\r', '\n' },
                StringSplitOptions.RemoveEmptyEntries).Any(line =>
                    line.Length == 0 || line[0] is '-' or '+' or 'U'))
        {
            throw new InstallerException("One or more pinned UVSR dependencies did not match their recorded revisions.");
        }
        SafePaths.RejectReparseTree(_paths.SourceDirectory,
            "managed UVSR source cache");
        await ValidatePreparedSourceAsync(tools, resolution, log,
            cancellationToken);
    }

    internal async Task BuildAsync(
        ToolPaths tools,
        SourceResolution resolution,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        ValidateResolution(resolution);
        await ValidatePreparedSourceAsync(tools, resolution, log,
            cancellationToken);
        ValidatePinnedCMakeIsSufficient(resolution.PublicBaseCommit);
        ValidateRendererBuildContractFromSource(resolution.PublicBaseCommit, log);
        ValidateBundledNotoSourceContractIfRequired(
            resolution, log, cancellationToken);
        if (Directory.Exists(_paths.BuildDirectory))
        {
            progress?.Report(new InstallerProgress("Preparing a clean build",
                "Removing the launcher-managed build cache so stale runtime assets cannot be packaged."));
            SafePaths.DeleteOwnedTree(_paths.BuildDirectory, _paths.CacheDirectory);
        }
        SafePaths.RejectReparsePathChain(_paths.BuildDirectory,
            "managed UVSR build directory");
        Directory.CreateDirectory(_paths.BuildDirectory);
        SafePaths.RejectReparsePathChain(_paths.BuildDirectory,
            "managed UVSR build directory");
        IReadOnlyDictionary<string, string?> environment = BuildEnvironment(tools);
        progress?.Report(new InstallerProgress("Configuring UVSR",
            "CMake is preparing the DirectX 12 Release build."));
        string[] configureArguments = BuildConfigureArguments(_paths, tools);
        ProcessResult configure = await _runner.RunAsync(tools.CMake,
            configureArguments, _paths.CacheDirectory, environment, log,
            cancellationToken, clearEnvironment: true);
        if (configure.ExitCode != 0)
            throw new InstallerException(
                "UVSR source configuration failed. Any installed UVSR version was preserved; no candidate was activated.");

        progress?.Report(new InstallerProgress("Building UVSR",
            "Compiling the renderer and production shaders. This is the longest step."));
        ProcessResult build = await _runner.RunAsync(tools.CMake, new[]
        {
            "--build", _paths.BuildDirectory,
            "--config", "Release",
            "--target", "uvsr",
            "--", "/m", "/nodeReuse:false"
        }, _paths.CacheDirectory, environment, log, cancellationToken,
            clearEnvironment: true);
        if (build.ExitCode != 0)
            throw new InstallerException(
                "UVSR did not finish building. Any installed UVSR version was preserved; no candidate was activated.");
        await ValidatePreparedSourceAsync(tools, resolution, log,
            cancellationToken);
    }

    internal async Task ValidatePreparedSourceAsync(
        ToolPaths tools,
        SourceResolution resolution,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        ValidateResolution(resolution);
        ProcessResult head = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "rev-parse", "HEAD" }, _paths.CacheDirectory,
            tools, log, cancellationToken);
        ProcessResult tree = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "rev-parse", "HEAD^{tree}" },
            _paths.CacheDirectory, tools, log, cancellationToken);
        ProcessResult status = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "status", "--porcelain=v1",
            "--untracked-files=all", "--ignore-submodules=none" },
            _paths.CacheDirectory, tools, log, cancellationToken);
        if (head.ExitCode != 0 || tree.ExitCode != 0 || status.ExitCode != 0)
            throw new InstallerException(
                "The managed UVSR source identity could not be verified.");
        string headValue = head.StandardOutput.Trim().ToLowerInvariant();
        string treeValue = tree.StandardOutput.Trim().ToLowerInvariant();
        if (resolution.Bridge is null)
        {
            if (!string.Equals(headValue, resolution.SourceCommit,
                    StringComparison.Ordinal) ||
                !string.Equals(treeValue, await ReadCommitTreeAsync(tools,
                    resolution.SourceCommit, log, cancellationToken),
                    StringComparison.Ordinal) ||
                !string.IsNullOrWhiteSpace(status.StandardOutput))
                throw new InstallerException(
                    "The managed UVSR source did not remain at its selected exact revision.");
            ValidateBundledNotoSourceContractIfRequired(
                resolution, log, cancellationToken);
            return;
        }

        ProcessResult parents = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "rev-list", "--parents", "-n", "1", "HEAD" },
            _paths.CacheDirectory, tools, log, cancellationToken);
        if (parents.ExitCode != 0)
            throw new InstallerException(
                "The renderer source bridge parent identity could not be verified.");
        resolution.Bridge.ValidatePreparedIdentityValues(headValue, treeValue,
            parents.StandardOutput.Trim().ToLowerInvariant(),
            status.StandardOutput);
    }

    private static void ValidateResolution(SourceResolution resolution)
    {
        if (resolution is null ||
            !ProductConstants.CommitRegex().IsMatch(resolution.SourceCommit) ||
            !ProductConstants.CommitRegex().IsMatch(resolution.PublicBaseCommit))
            throw new InstallerException("The selected UVSR source revision is invalid.");
        if (resolution.Bridge is null)
        {
            if (!string.Equals(resolution.SourceCommit, resolution.PublicBaseCommit,
                    StringComparison.Ordinal))
                throw new InstallerException(
                    "The selected UVSR source revision has no verified source bridge.");
            return;
        }
        if (!ReferenceEquals(resolution.Bridge, RendererSourceBridge.Instance) ||
            !string.Equals(resolution.PublicBaseCommit,
                RendererSourceBridge.PublicBaseCommit, StringComparison.Ordinal) ||
            !string.Equals(resolution.SourceCommit,
                RendererSourceBridge.SourceCommit, StringComparison.Ordinal))
            throw new InstallerException(
                "The selected UVSR renderer source bridge identity was invalid.");
    }

    private void ValidateBundledNotoSourceContractIfRequired(
        SourceResolution resolution,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        if (resolution.Bridge is not null)
            return;
        ValidateBundledNotoSourceContract(
            _paths.SourceDirectory,
            resolution.SourceCommit,
            log,
            cancellationToken);
    }

    internal static void ValidateBundledNotoSourceContract(
        string sourceDirectory,
        string commit,
        InstallLog? log = null,
        CancellationToken cancellationToken = default)
    {
        if (!ProductConstants.CommitRegex().IsMatch(commit))
            throw new InstallerException("The selected UVSR source revision is invalid.");
        string sourceRoot = Path.GetFullPath(sourceDirectory);
        string fontRoot = Path.Combine(sourceRoot,
            NotoSansSourceRootRelativePath.Replace('/', Path.DirectorySeparatorChar));
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            SafePaths.RejectReparsePathChain(fontRoot,
                "bundled Noto Sans source directory");
            if (!Directory.Exists(fontRoot))
            {
                throw CreateSourceCompatibilityException(commit,
                    $"required bundled Noto Sans v2.015 source directory " +
                    $"'{NotoSansSourceRootRelativePath}' is missing");
            }
            SafePaths.RejectReparseTree(fontRoot,
                "bundled Noto Sans source directory");

            string[] actualFiles = Directory.EnumerateFiles(fontRoot, "*",
                    SearchOption.AllDirectories)
                .Select(path => Path.GetRelativePath(fontRoot, path)
                    .Replace('\\', '/'))
                .OrderBy(path => path, StringComparer.Ordinal)
                .ToArray();
            string[] expectedFiles = RequiredNotoSansSourceFiles.Keys
                .OrderBy(path => path, StringComparer.Ordinal)
                .ToArray();
            if (!actualFiles.SequenceEqual(expectedFiles, StringComparer.Ordinal))
            {
                string missing = string.Join(", ", expectedFiles.Except(
                    actualFiles, StringComparer.Ordinal));
                string unexpected = string.Join(", ", actualFiles.Except(
                    expectedFiles, StringComparer.Ordinal));
                List<string> differences = new();
                if (missing.Length > 0)
                    differences.Add("missing " + missing);
                if (unexpected.Length > 0)
                    differences.Add("unexpected " + unexpected);
                throw CreateSourceCompatibilityException(commit,
                    $"bundled Noto Sans v2.015 source contract at " +
                    $"'{NotoSansSourceRootRelativePath}' is not exact " +
                    $"({string.Join("; ", differences)})");
            }

            foreach (KeyValuePair<string, (long Size, string Sha256)> expected in
                     RequiredNotoSansSourceFiles)
            {
                cancellationToken.ThrowIfCancellationRequested();
                string path = Path.Combine(fontRoot,
                    expected.Key.Replace('/', Path.DirectorySeparatorChar));
                SafePaths.RejectReparsePoint(path,
                    $"bundled Noto Sans source file '{expected.Key}'");
                FileInfo actual = new(path);
                if (!actual.Exists || actual.Length != expected.Value.Size)
                {
                    throw CreateSourceCompatibilityException(commit,
                        $"bundled Noto Sans v2.015 source asset " +
                        $"'{NotoSansSourceRootRelativePath}/{expected.Key}' has " +
                        $"the wrong size (expected {expected.Value.Size} bytes)");
                }
                using FileStream stream = new(path, FileMode.Open, FileAccess.Read,
                    FileShare.Read);
                string hash = Convert.ToHexString(SHA256.HashData(stream))
                    .ToLowerInvariant();
                if (!string.Equals(hash, expected.Value.Sha256,
                        StringComparison.Ordinal))
                {
                    throw CreateSourceCompatibilityException(commit,
                        $"bundled Noto Sans v2.015 source asset " +
                        $"'{NotoSansSourceRootRelativePath}/{expected.Key}' failed " +
                        "its SHA-256 check");
                }
            }

            log?.Write($"Renderer source font contract validated for {commit}: " +
                       $"exact Noto Sans v2.015 assets at " +
                       $"'{NotoSansSourceRootRelativePath}'.");
        }
        catch (SourceLauncherCompatibilityException)
        {
            throw;
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is InstallerException or IOException or
                                   UnauthorizedAccessException)
        {
            throw CreateSourceCompatibilityException(commit,
                $"bundled Noto Sans v2.015 source contract at " +
                $"'{NotoSansSourceRootRelativePath}' could not be validated " +
                $"({ex.Message})", ex);
        }
    }

    private async Task ValidatePublicBaseCheckoutAsync(
        ToolPaths tools,
        SourceResolution resolution,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        ProcessResult head = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "rev-parse", "HEAD" }, _paths.CacheDirectory,
            tools, log, cancellationToken);
        ProcessResult tree = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "rev-parse", "HEAD^{tree}" },
            _paths.CacheDirectory, tools, log, cancellationToken);
        ProcessResult status = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "status", "--porcelain=v1",
            "--untracked-files=all", "--ignore-submodules=none" },
            _paths.CacheDirectory, tools, log, cancellationToken);
        if (head.ExitCode != 0 || tree.ExitCode != 0 || status.ExitCode != 0 ||
            !string.Equals(head.StandardOutput.Trim(), resolution.PublicBaseCommit,
                StringComparison.OrdinalIgnoreCase) ||
            !string.IsNullOrWhiteSpace(status.StandardOutput))
            throw new InstallerException(
                "The managed UVSR checkout did not match the selected pristine public revision.");
        if (resolution.Bridge is not null &&
            !string.Equals(tree.StandardOutput.Trim(),
                RendererSourceBridge.PublicBaseTree,
                StringComparison.OrdinalIgnoreCase))
            throw new InstallerException(
                "The public UVSR source base tree did not match the audited renderer bridge base.");

        string gitDirectory = Path.Combine(_paths.SourceDirectory, ".git");
        if (File.Exists(Path.Combine(gitDirectory, "info", "grafts")) ||
            File.Exists(Path.Combine(gitDirectory, "info", "attributes")) ||
            Directory.Exists(Path.Combine(gitDirectory, "refs", "replace")))
            throw new InstallerException(
                "The managed UVSR source contains unsupported Git replacement metadata.");
    }

    private async Task ApplyRendererSourceBridgeAsync(
        ToolPaths tools,
        SourceResolution resolution,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        RendererSourceBridge bridge = resolution.Bridge
            ?? throw new InstallerException("The renderer source bridge was not selected.");
        cancellationToken.ThrowIfCancellationRequested();
        byte[] patch = bridge.LoadVerifiedPatch();
        string gitDirectory = Path.Combine(_paths.SourceDirectory, ".git");
        string bridgeDirectory = Path.Combine(gitDirectory,
            "uvsr-launcher-renderer-bridge");
        SafePaths.RejectReparsePathChain(gitDirectory, "managed UVSR Git data");
        SafePaths.RejectReparsePathChain(bridgeDirectory,
            "managed UVSR renderer bridge data");
        Directory.CreateDirectory(bridgeDirectory);
        SafePaths.RejectReparsePathChain(bridgeDirectory,
            "managed UVSR renderer bridge data");
        string patchPath = Path.Combine(bridgeDirectory, "bridge.patch");
        using (FileStream writer = new(patchPath, FileMode.CreateNew,
                   FileAccess.Write, FileShare.None, 64 * 1024,
                   FileOptions.WriteThrough))
        {
            await writer.WriteAsync(patch, cancellationToken);
            writer.Flush(flushToDisk: true);
        }

        using FileStream bridgeLock = new(patchPath, FileMode.Open, FileAccess.Read,
            FileShare.Read, 64 * 1024, FileOptions.SequentialScan);
        byte[] lockedHash = SHA256.HashData(bridgeLock);
        if (!CryptographicOperations.FixedTimeEquals(lockedHash,
                Convert.FromHexString(RendererSourceBridge.PatchSha256)))
            throw new InstallerException(
                "The managed renderer source bridge changed before application.");
        cancellationToken.ThrowIfCancellationRequested();

        ProcessResult check = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "apply", "--check", "--index", "--binary",
            patchPath }, _paths.CacheDirectory, tools, log, cancellationToken,
            strictApply: true);
        if (check.ExitCode != 0)
            throw new InstallerException(
                "The exact embedded renderer source bridge did not apply cleanly to its audited public base.");
        cancellationToken.ThrowIfCancellationRequested();
        ProcessResult apply = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "apply", "--index", "--binary", patchPath },
            _paths.CacheDirectory, tools, log, cancellationToken,
            strictApply: true);
        if (apply.ExitCode != 0)
            throw new InstallerException(
                "The exact embedded renderer source bridge could not be applied.");

        ProcessResult stagedPaths = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "diff", "--cached", "--name-only",
            "--no-renames", resolution.PublicBaseCommit }, _paths.CacheDirectory,
            tools, log, cancellationToken, strictApply: true);
        HashSet<string> actualPaths = stagedPaths.StandardOutput.Split(
                new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
            .ToHashSet(StringComparer.Ordinal);
        if (stagedPaths.ExitCode != 0 || actualPaths.Count != bridge.StagedBlobs.Count ||
            !actualPaths.SetEquals(bridge.StagedBlobs.Keys))
            throw new InstallerException(
                "The renderer source bridge changed outside its exact audited path set.");

        List<string> entryArguments = new()
        {
            "-C", _paths.SourceDirectory, "ls-files", "--stage", "--"
        };
        entryArguments.AddRange(bridge.StagedBlobs.Keys);
        ProcessResult entries = await GitAsync(tools.Git, entryArguments,
            _paths.CacheDirectory, tools, log, cancellationToken,
            strictApply: true);
        if (entries.ExitCode != 0)
            throw new InstallerException(
                "The renderer source bridge staged path identities could not be verified.");
        bridge.ValidateStagedEntries(entries.StandardOutput);

        ProcessResult tree = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "write-tree" }, _paths.CacheDirectory,
            tools, log, cancellationToken, strictApply: true);
        if (tree.ExitCode != 0 || !string.Equals(tree.StandardOutput.Trim(),
                RendererSourceBridge.SourceTree, StringComparison.OrdinalIgnoreCase))
            throw new InstallerException(
                "The renderer source bridge did not produce its exact audited source tree.");

        IReadOnlyDictionary<string, string?> commitEnvironment =
            BuildBridgeCommitEnvironment(tools);
        ProcessResult commit = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "commit-tree", RendererSourceBridge.SourceTree,
            "-p", RendererSourceBridge.PublicBaseCommit, "-m",
            RendererSourceBridge.CommitMessage }, _paths.CacheDirectory, tools,
            log, cancellationToken, strictApply: true,
            environment: commitEnvironment);
        if (commit.ExitCode != 0 || !string.Equals(commit.StandardOutput.Trim(),
                RendererSourceBridge.SourceCommit, StringComparison.OrdinalIgnoreCase))
            throw new InstallerException(
                "The renderer source bridge did not produce its deterministic source commit.");

        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "reset", "--hard", RendererSourceBridge.SourceCommit },
            _paths.CacheDirectory, tools, log, cancellationToken,
            "The deterministic renderer source commit could not be checked out.");
        await ValidatePreparedSourceAsync(tools, resolution, log,
            cancellationToken);
        log.Write($"Applied embedded renderer source bridge " +
                  $"'{RendererSourceBridge.BridgeId}' to public base " +
                  $"{RendererSourceBridge.PublicBaseCommit}; prepared synthetic " +
                  $"source {RendererSourceBridge.SourceCommit}, tree " +
                  $"{RendererSourceBridge.SourceTree}, patch SHA-256 " +
                  $"{RendererSourceBridge.PatchSha256}.");
    }

    private async Task<string> ReadCommitTreeAsync(
        ToolPaths tools,
        string commit,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        ProcessResult result = await GitAsync(tools.Git, new[] { "-C",
            _paths.SourceDirectory, "rev-parse", $"{commit}^{{tree}}" },
            _paths.CacheDirectory, tools, log, cancellationToken);
        string tree = result.StandardOutput.Trim().ToLowerInvariant();
        if (result.ExitCode != 0 || !ProductConstants.CommitRegex().IsMatch(tree))
            throw new InstallerException(
                "The selected UVSR source tree identity could not be verified.");
        return tree;
    }

    private static IReadOnlyDictionary<string, string?> BuildBridgeCommitEnvironment(
        ToolPaths tools)
    {
        Dictionary<string, string?> environment = new(BuildEnvironment(tools,
            strictApply: true), StringComparer.OrdinalIgnoreCase)
        {
            ["GIT_AUTHOR_NAME"] = RendererSourceBridge.CommitIdentityName,
            ["GIT_AUTHOR_EMAIL"] = RendererSourceBridge.CommitIdentityEmail,
            ["GIT_AUTHOR_DATE"] = RendererSourceBridge.CommitIdentityDate,
            ["GIT_COMMITTER_NAME"] = RendererSourceBridge.CommitIdentityName,
            ["GIT_COMMITTER_EMAIL"] = RendererSourceBridge.CommitIdentityEmail,
            ["GIT_COMMITTER_DATE"] = RendererSourceBridge.CommitIdentityDate
        };
        return environment;
    }

    internal static string[] BuildConfigureArguments(InstallerPaths paths, ToolPaths tools) =>
        new[]
        {
            "-S", paths.SourceDirectory,
            "-B", paths.BuildDirectory,
            "-G", "Visual Studio 17 2022",
            "-A", "x64",
            "-T", "host=x64",
            $"-DCMAKE_GENERATOR_INSTANCE={tools.VisualStudioInstance}",
            $"-DGIT_EXECUTABLE={tools.Git}",
            $"-DPython3_EXECUTABLE={tools.Python}",
            $"-DFETCHCONTENT_SOURCE_DIR_D3D_AGILITY_SDK={tools.AgilitySdk}",
            $"-DDONUT_D3D_AGILITY_SDK_FETCH_DIR={tools.AgilitySdk}",
            $"-DFETCHCONTENT_SOURCE_DIR_DIRECTX_HEADERS={tools.DirectXHeaders}",
            $"-DNVRHI_DIRECTX_HEADERS_FETCH_DIR={tools.DirectXHeaders}",
            $"-DFETCHCONTENT_SOURCE_DIR_DXC={tools.Dxc}",
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
            "-DFETCHCONTENT_UPDATES_DISCONNECTED=ON",
            "-DUVSR_BUILD_APPLICATION=ON",
            "-DUVSR_WITH_NRD=OFF"
        };

    private void ValidateRendererBuildContractFromSource(
        string commit,
        InstallLog log)
    {
        string relativePath = ProductConstants.RendererBuildContractRelativePath;
        string path = Path.Combine(_paths.SourceDirectory,
            relativePath.Replace('/', Path.DirectorySeparatorChar));
        try
        {
            SafePaths.RejectReparsePathChain(path, "renderer build contract");
            FileInfo contractFile = new(path);
            if (!contractFile.Exists)
                throw CreateSourceCompatibilityException(commit,
                    $"required contract '{relativePath}' is missing");
            if (contractFile.Length is <= 0 or > ProductConstants.MaximumRendererBuildContractBytes)
                throw new InstallerException("The renderer build contract was empty or too large.");
            RendererBuildContract contract = ParseRendererBuildContract(
                File.ReadAllBytes(path));
            ValidateSupportedRendererBuildContract(contract, commit, log);
        }
        catch (SourceLauncherCompatibilityException)
        {
            throw;
        }
        catch (Exception ex) when (ex is InstallerException or IOException or
                                   UnauthorizedAccessException)
        {
            throw CreateSourceCompatibilityException(commit,
                $"contract '{relativePath}' could not be validated ({ex.Message})", ex);
        }
    }

    internal static RendererBuildContract ParseRendererBuildContract(byte[] data)
    {
        if (data.LongLength is <= 0 or > ProductConstants.MaximumRendererBuildContractBytes)
            throw new InstallerException("The renderer build contract was empty or too large.");
        try
        {
            RejectDuplicateJsonProperties(data);
            RendererBuildContract contract =
                JsonSerializer.Deserialize<RendererBuildContract>(data, JsonStore.Options)
                ?? throw new JsonException("The renderer build contract was empty.");
            if (contract.SchemaVersion != ProductConstants.RendererBuildContractSchemaVersion ||
                !string.Equals(contract.ProductId, ProductConstants.ProductId,
                    StringComparison.Ordinal) ||
                string.IsNullOrWhiteSpace(contract.ContractId) ||
                contract.ContractId.Length > 96 ||
                contract.ContractId.Any(character =>
                    !(char.IsAsciiLetterOrDigit(character) || character == '-')) ||
                contract.MinimumLauncherReleaseSequence is < 1 or
                    > ProductConstants.MaximumReleaseSequence ||
                !IsStableVersion(contract.D3D12AgilitySdkVersion) ||
                !IsStableVersion(contract.DirectXHeadersVersion) ||
                !IsStableVersion(contract.DxcVersion) ||
                string.IsNullOrWhiteSpace(contract.DxcDate) ||
                contract.DxcDate.Length != 10 ||
                contract.DxcDate[4] != '_' || contract.DxcDate[7] != '_' ||
                contract.DxcDate.Where((_, index) => index is not (4 or 7))
                    .Any(character => !char.IsAsciiDigit(character)))
            {
                throw new InstallerException(
                    "The renderer build contract did not match its required format.");
            }
            return contract;
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex) when (ex is JsonException or InvalidOperationException)
        {
            throw new InstallerException("The renderer build contract was invalid.", ex);
        }
    }

    internal static void ValidateSupportedRendererBuildContract(
        RendererBuildContract contract,
        string commit,
        InstallLog? log = null,
        string? sourceAddress = null)
    {
        if (!ProductConstants.CommitRegex().IsMatch(commit))
            throw new InstallerException("The selected UVSR source revision is invalid.");
        List<string> mismatches = new();
        if (!string.Equals(contract.ContractId, ProductConstants.RendererBuildContractId,
                StringComparison.Ordinal))
            mismatches.Add($"contract ID expected '{ProductConstants.RendererBuildContractId}' but found '{contract.ContractId}'");
        if (contract.MinimumLauncherReleaseSequence > ProductConstants.LauncherReleaseSequence)
            mismatches.Add($"minimum launcher sequence is {contract.MinimumLauncherReleaseSequence} but this launcher is sequence {ProductConstants.LauncherReleaseSequence}");
        AddVersionMismatch(mismatches, "Direct3D Agility SDK",
            ProductConstants.AgilitySdk.Version, contract.D3D12AgilitySdkVersion);
        AddVersionMismatch(mismatches, "DirectX Headers",
            ProductConstants.DirectXHeaders.Version, contract.DirectXHeadersVersion);
        AddVersionMismatch(mismatches, "DXC", ProductConstants.Dxc.Version,
            contract.DxcVersion);
        if (!string.Equals(contract.DxcDate, ProductConstants.PinnedDxcDate,
                StringComparison.Ordinal))
            mismatches.Add($"DXC date expected '{ProductConstants.PinnedDxcDate}' but found '{contract.DxcDate}'");
        if (mismatches.Count > 0)
            throw CreateSourceCompatibilityException(commit,
                $"contract '{sourceAddress ?? ProductConstants.RendererBuildContractRelativePath}' " +
                string.Join("; ", mismatches));

        log?.Write($"Renderer source contract validated for {commit}: " +
                   $"{ProductConstants.RendererBuildContractRelativePath}, " +
                   $"contract '{contract.ContractId}', minimum launcher sequence " +
                   $"{contract.MinimumLauncherReleaseSequence}, running launcher " +
                   $"{ProductConstants.LauncherVersion} sequence " +
                   $"{ProductConstants.LauncherReleaseSequence}, Agility SDK " +
                   $"{contract.D3D12AgilitySdkVersion}, DirectX Headers " +
                   $"{contract.DirectXHeadersVersion}, DXC {contract.DxcVersion} " +
                   $"({contract.DxcDate}).");
    }

    internal static Uri BuildRendererBuildContractUri(string commit)
    {
        if (!ProductConstants.CommitRegex().IsMatch(commit))
            throw new InstallerException("The selected UVSR source revision is invalid.");
        return new Uri($"{ProductConstants.RepositoryRawUrl}/{commit}/" +
                       ProductConstants.RendererBuildContractRelativePath);
    }

    internal static SourceLauncherCompatibilityException CreateSourceCompatibilityException(
        string commit,
        string reason,
        Exception? inner = null)
    {
        string shortCommit = ProductConstants.CommitRegex().IsMatch(commit)
            ? commit[..7]
            : "unknown";
        string message =
            $"Public UVSR main {shortCommit} and UVSR Launcher " +
            $"{ProductConstants.LauncherVersion} (sequence " +
            $"{ProductConstants.LauncherReleaseSequence}) are not a compatible " +
            $"release pair: {reason}. No launcher update is needed unless the " +
            "validated launcher feed reports a newer compatible release. Try " +
            "again after matching UVSR source is published. Any installed UVSR " +
            "version was preserved; no candidate was activated.";
        return inner is null
            ? new SourceLauncherCompatibilityException(message)
            : new SourceLauncherCompatibilityException(message, inner);
    }

    private static bool IsStableVersion(string value) =>
        !string.IsNullOrWhiteSpace(value) &&
        ProductConstants.StableVersionRegex().IsMatch(value) &&
        value.Split('.').All(part => int.TryParse(part, out _));

    private static void AddVersionMismatch(
        ICollection<string> mismatches,
        string name,
        string expected,
        string actual)
    {
        if (!string.Equals(expected, actual, StringComparison.Ordinal))
            mismatches.Add($"{name} expected '{expected}' but found '{actual}'");
    }

    private static void RejectDuplicateJsonProperties(ReadOnlySpan<byte> data)
    {
        Utf8JsonReader reader = new(data, new JsonReaderOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 16
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

    private void ValidatePinnedCMakeIsSufficient(string commit)
    {
        Version required = new(0, 0);
        foreach (string buildFile in new[]
                 {
                     Path.Combine(_paths.SourceDirectory, "CMakeLists.txt"),
                     Path.Combine(_paths.SourceDirectory, "donut", "CMakeLists.txt")
                 })
        {
            if (!File.Exists(buildFile))
                throw new InstallerException("The selected UVSR source is missing a required CMake build file.");
            string text = File.ReadAllText(buildFile);
            System.Text.RegularExpressions.Match match =
                System.Text.RegularExpressions.Regex.Match(text,
                    @"cmake_minimum_required\s*\(\s*VERSION\s+(?<version>[0-9]+(?:\.[0-9]+){1,2})",
                    System.Text.RegularExpressions.RegexOptions.IgnoreCase |
                    System.Text.RegularExpressions.RegexOptions.CultureInvariant);
            if (!match.Success ||
                !Version.TryParse(match.Groups["version"].Value, out Version? fileRequired))
                throw new InstallerException("The selected UVSR source has an unreadable CMake requirement.");
            if (fileRequired > required)
                required = fileRequired;
        }
        if (!Version.TryParse(ProductConstants.CMake.Version, out Version? available) ||
            available < required)
            throw CreateSourceCompatibilityException(commit,
                $"public source requires CMake {required} but this launcher provides {available}");
    }

    private async Task<ProcessResult> GitAsync(
        string git,
        IEnumerable<string> arguments,
        string workingDirectory,
        ToolPaths tools,
        InstallLog log,
        CancellationToken cancellationToken,
        bool forceHttp11 = false,
        bool strictApply = false,
        IReadOnlyDictionary<string, string?>? environment = null)
    {
        List<string> secured = new()
        {
            "-c", "http.sslVerify=true",
            "-c", "protocol.allow=never",
            "-c", "protocol.https.allow=always",
            "-c", "core.hooksPath=NUL",
            "-c", "http.lowSpeedLimit=1",
            "-c", "http.lowSpeedTime=90"
        };
        if (forceHttp11)
        {
            secured.Add("-c");
            secured.Add("http.version=HTTP/1.1");
        }
        secured.AddRange(arguments);
        return await _runner.RunAsync(git, secured, workingDirectory,
            environment ?? BuildEnvironment(tools, strictApply), log,
            cancellationToken, clearEnvironment: true);
    }

    private async Task<ProcessResult> GitNetworkAsync(
        string git,
        IEnumerable<string> arguments,
        string workingDirectory,
        ToolPaths tools,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken,
        string phase)
    {
        string[] stableArguments = arguments.ToArray();
        ProcessResult? last = null;
        bool forceHttp11 = false;
        for (int attempt = 1; attempt <= 4; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            last = await GitAsync(git, stableArguments, workingDirectory,
                tools, log, cancellationToken, forceHttp11);
            if (last.ExitCode == 0 || !IsTransientNetworkFailure(last))
                return last;
            if (ContainsHttp2Failure(last))
                forceHttp11 = true;
            log.Write($"Transient GitHub transfer failure; attempt {attempt}/4: " +
                      SummarizeNetworkFailure(last));
            if (attempt == 4)
                return last;
            TimeSpan delay = Jitter(NetworkRetryDelays[attempt - 1]);
            progress?.Report(new InstallerProgress("Connection stalled",
                $"{phase} was interrupted. Retrying automatically in " +
                $"{Math.Max(1, (int)Math.Ceiling(delay.TotalSeconds))} seconds " +
                $"(attempt {attempt + 1} of 4)."));
            await Task.Delay(delay, cancellationToken);
        }
        return last ?? new ProcessResult(-1, string.Empty, "No Git transfer was attempted.");
    }

    internal static bool IsTransientNetworkFailure(ProcessResult result)
    {
        string text = (result.StandardError + "\n" + result.StandardOutput).ToLowerInvariant();
        string[] patterns =
        {
            "could not resolve host", "failed to connect", "connection timed out",
            "operation timed out", "connection reset", "early eof",
            "unexpected disconnect", "http/2 stream", "rpc failed",
            "remote end hung up unexpectedly", "response ended prematurely",
            "network is unreachable", "temporary failure in name resolution",
            "the requested url returned error: 408", "the requested url returned error: 425",
            "the requested url returned error: 429", "the requested url returned error: 500",
            "the requested url returned error: 502", "the requested url returned error: 503",
            "the requested url returned error: 504", "curl 28",
            "operation too slow", "less than 1 bytes/sec transferred",
            "schannel: failed to receive handshake",
            "tls connection was non-properly terminated", "gnutls recv error",
            "ssl_connect: connection was reset"
        };
        return patterns.Any(text.Contains);
    }

    internal static bool ContainsHttp2Failure(ProcessResult result) =>
        (result.StandardError + "\n" + result.StandardOutput).Contains(
            "HTTP/2", StringComparison.OrdinalIgnoreCase);

    private static string SummarizeNetworkFailure(ProcessResult result)
    {
        string source = string.IsNullOrWhiteSpace(result.StandardError)
            ? result.StandardOutput
            : result.StandardError;
        string line = source.Split(new[] { '\r', '\n' },
                StringSplitOptions.RemoveEmptyEntries)
            .LastOrDefault() ?? $"exit code {result.ExitCode}";
        return line.Length <= 400 ? line : line[..400];
    }

    private static TimeSpan Jitter(TimeSpan basis) => TimeSpan.FromMilliseconds(
        basis.TotalMilliseconds * (0.75 + Random.Shared.NextDouble() * 0.5));

    private async Task GitRequiredAsync(
        string git,
        IEnumerable<string> arguments,
        string workingDirectory,
        ToolPaths tools,
        InstallLog log,
        CancellationToken cancellationToken,
        string failureMessage)
    {
        ProcessResult result = await GitAsync(git, arguments, workingDirectory,
            tools, log, cancellationToken);
        if (result.ExitCode != 0)
            throw new InstallerException(failureMessage);
    }

    internal static IReadOnlyDictionary<string, string?> BuildEnvironment(
        ToolPaths tools,
        bool strictApply = false)
    {
        string gitDirectory = Path.GetDirectoryName(tools.Git)!;
        string cmakeDirectory = Path.GetDirectoryName(tools.CMake)!;
        string pythonDirectory = Path.GetDirectoryName(tools.Python)!;
        string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
        Dictionary<string, string?> environment = new(StringComparer.OrdinalIgnoreCase)
        {
            ["PATH"] = string.Join(Path.PathSeparator,
                new[]
                {
                    gitDirectory, cmakeDirectory, pythonDirectory,
                    Path.Combine(windows, "System32"), windows,
                    Path.Combine(windows, "System32", "Wbem")
                }),
            ["GIT_CONFIG_NOSYSTEM"] = "1",
            ["GIT_CONFIG_GLOBAL"] = "NUL",
            ["GIT_NO_REPLACE_OBJECTS"] = "1",
            ["GIT_TERMINAL_PROMPT"] = "0",
            ["GCM_INTERACTIVE"] = "Never",
            ["LC_ALL"] = "C",
            ["LANG"] = "C",
            ["PYTHONDONTWRITEBYTECODE"] = "1"
        };
        if (!strictApply)
        {
            // The build-time dependency overrides have reviewed mixed-EOL
            // context. The embedded renderer source bridge always opts out.
            environment["GIT_CONFIG_COUNT"] = "1";
            environment["GIT_CONFIG_KEY_0"] = "apply.ignoreWhitespace";
            environment["GIT_CONFIG_VALUE_0"] = "change";
        }
        foreach (string key in new[]
                 {
                     "SystemRoot", "WINDIR", "COMSPEC", "TEMP", "TMP", "USERPROFILE",
                     "HOMEDRIVE", "HOMEPATH", "LOCALAPPDATA", "APPDATA", "PROGRAMDATA",
                     "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "SystemDrive",
                     "NUMBER_OF_PROCESSORS", "PROCESSOR_ARCHITECTURE", "PATHEXT",
                     "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY"
                 })
        {
            string? value = Environment.GetEnvironmentVariable(key);
            if (!string.IsNullOrWhiteSpace(value))
                environment[key] = value;
        }
        return environment;
    }

    internal void RecreateManagedSourceDirectory()
    {
        SafePaths.RejectReparsePathChain(_paths.CacheDirectory,
            "managed UVSR cache directory");
        if (Directory.Exists(_paths.SourceDirectory))
        {
            SafePaths.RejectReparseTree(_paths.SourceDirectory,
                "managed UVSR source cache");
            SafePaths.DeleteOwnedTree(_paths.SourceDirectory, _paths.CacheDirectory);
        }
        SafePaths.RejectReparsePathChain(_paths.SourceDirectory,
            "managed UVSR source cache");
        Directory.CreateDirectory(_paths.SourceDirectory);
        SafePaths.RejectReparsePathChain(_paths.SourceDirectory,
            "managed UVSR source cache");
    }

    private void ValidateManagedSubmoduleMetadata()
    {
        string modulesRoot = Path.Combine(_paths.SourceDirectory, ".git", "modules");
        foreach (string gitfile in Directory.EnumerateFiles(_paths.SourceDirectory,
                     ".git", SearchOption.AllDirectories))
        {
            if (string.Equals(gitfile, Path.Combine(_paths.SourceDirectory, ".git"),
                    StringComparison.OrdinalIgnoreCase))
                continue;
            string text = File.ReadAllText(gitfile).Trim();
            const string prefix = "gitdir:";
            if (!text.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                throw new InstallerException("A managed UVSR dependency has invalid Git metadata.");
            string value = text[prefix.Length..].Trim();
            if (string.IsNullOrWhiteSpace(value) || value.Contains('\0'))
                throw new InstallerException("A managed UVSR dependency has invalid Git metadata.");
            string gitDirectory = Path.GetFullPath(Path.Combine(
                Path.GetDirectoryName(gitfile)!, value));
            if (!SafePaths.IsStrictDescendant(gitDirectory, modulesRoot) ||
                !Directory.Exists(gitDirectory))
                throw new InstallerException(
                    "A managed UVSR dependency redirects Git outside launcher ownership.");
            SafePaths.RejectReparsePathChain(gitDirectory,
                "managed UVSR dependency Git data");

            string configPath = Path.Combine(gitDirectory, "config");
            string config = File.ReadAllText(configPath);
            if (config.Contains("include.path", StringComparison.OrdinalIgnoreCase) ||
                config.Contains("[include", StringComparison.OrdinalIgnoreCase) ||
                config.Contains("[filter ", StringComparison.OrdinalIgnoreCase))
                throw new InstallerException(
                    "A managed UVSR dependency contains unsafe Git configuration.");
            string? worktreeValue = config.Split(new[] { '\r', '\n' },
                    StringSplitOptions.RemoveEmptyEntries)
                .Select(line => line.Trim())
                .Where(line => line.StartsWith("worktree", StringComparison.OrdinalIgnoreCase))
                .Select(line => line.Split('=', 2))
                .Where(parts => parts.Length == 2)
                .Select(parts => parts[1].Trim())
                .SingleOrDefault();
            string expectedWorktree = Path.GetFullPath(Path.GetDirectoryName(gitfile)!);
            string actualWorktree = worktreeValue is null ? string.Empty :
                Path.GetFullPath(Path.Combine(gitDirectory, worktreeValue));
            if (!string.Equals(expectedWorktree, actualWorktree,
                    StringComparison.OrdinalIgnoreCase))
                throw new InstallerException(
                    "A managed UVSR dependency redirects its worktree outside launcher ownership.");
            if (File.Exists(Path.Combine(gitDirectory, "info", "grafts")) ||
                File.Exists(Path.Combine(gitDirectory, "info", "attributes")) ||
                Directory.Exists(Path.Combine(gitDirectory, "refs", "replace")))
                throw new InstallerException(
                    "A managed UVSR dependency contains unsupported Git replacement metadata.");
        }
    }

    internal void WriteSafeRepositoryConfig()
    {
        string gitDirectory = Path.Combine(_paths.SourceDirectory, ".git");
        if (!Directory.Exists(gitDirectory))
            throw new InstallerException("The managed UVSR Git directory is missing.");
        SafePaths.RejectReparsePathChain(gitDirectory, "managed UVSR Git data");
        string config = Path.Combine(gitDirectory, "config");
        string contents =
            "[core]\n" +
            "\trepositoryformatversion = 0\n" +
            "\tfilemode = false\n" +
            "\tbare = false\n" +
            "\tlogallrefupdates = true\n" +
            "\tsymlinks = false\n" +
            "\tignorecase = true\n" +
            "\tlongpaths = true\n" +
            "[remote \"origin\"]\n" +
            $"\turl = {ProductConstants.RepositoryUrl}\n" +
            $"\tfetch = +{ProductConstants.RepositoryMainRef}:refs/remotes/origin/main\n";
        string temporary = config + $".{Guid.NewGuid():N}.tmp";
        try
        {
            File.WriteAllText(temporary, contents, new System.Text.UTF8Encoding(false));
            File.Move(temporary, config, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary))
                File.Delete(temporary);
        }
    }
}
