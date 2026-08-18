using System.Text.RegularExpressions;

namespace UvsrInstaller;

internal sealed record SourceResolution(string Commit, bool IsAlreadyInstalled, bool IsNonFastForward);

internal sealed class SourceManager
{
    private const string PinnedDxcDate = "2026_02_20";
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
        string commit = revision.StandardOutput.Trim().ToLowerInvariant();
        if (revision.ExitCode != 0 || !ProductConstants.CommitRegex().IsMatch(commit))
            throw new InstallerException("GitHub returned an invalid UVSR main revision.");

        bool same = string.Equals(installedState?.Commit, commit, StringComparison.Ordinal);
        bool nonFastForward = false;
        if (installedState is not null && !same)
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
                "merge-base", "--is-ancestor", installedState.Commit, commit },
                _paths.CacheDirectory, tools, log, cancellationToken);
            nonFastForward = ancestry.ExitCode != 0;
        }

        log.Write($"Resolved public UVSR main to {commit}.");
        return new SourceResolution(commit, same, nonFastForward);
    }

    internal async Task PrepareExactSourceAsync(
        ToolPaths tools,
        string commit,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        if (!ProductConstants.CommitRegex().IsMatch(commit))
            throw new InstallerException("The selected UVSR source revision is invalid.");
        progress?.Report(new InstallerProgress("Preparing UVSR source",
            $"Checking out {commit[..7]} and its pinned dependencies."));
        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "checkout", "--detach", "--force", commit }, _paths.CacheDirectory,
            tools, log, cancellationToken, "The exact UVSR source revision could not be checked out.");
        await GitRequiredAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "clean", "-ffdqx" }, _paths.CacheDirectory, tools, log, cancellationToken,
            "The launcher-owned UVSR source cache could not be cleaned.");
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

        ProcessResult head = await GitAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "rev-parse", "HEAD" }, _paths.CacheDirectory, tools, log, cancellationToken);
        if (head.ExitCode != 0 || !string.Equals(head.StandardOutput.Trim(), commit,
                StringComparison.OrdinalIgnoreCase))
            throw new InstallerException("The managed UVSR checkout did not match the selected public revision.");
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
        ProcessResult dirty = await GitAsync(tools.Git, new[] { "-C", _paths.SourceDirectory,
            "status", "--porcelain=v1", "--untracked-files=all", "--ignore-submodules=none" },
            _paths.CacheDirectory, tools, log, cancellationToken);
        if (dirty.ExitCode != 0 || !string.IsNullOrWhiteSpace(dirty.StandardOutput))
            throw new InstallerException("The managed UVSR source did not become clean after exact checkout.");
    }

    internal async Task BuildAsync(
        ToolPaths tools,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        ValidatePinnedCMakeIsSufficient();
        ValidatePinnedBuildDependencies();
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
            throw new InstallerException("UVSR source configuration failed. The previous installed version was preserved.");

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
            throw new InstallerException("UVSR did not finish building. The previous installed version was preserved.");
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

    private void ValidatePinnedBuildDependencies()
    {
        string root = File.ReadAllText(Path.Combine(_paths.SourceDirectory, "CMakeLists.txt"));
        string nvrhi = File.ReadAllText(Path.Combine(_paths.SourceDirectory,
            "donut", "nvrhi", "CMakeLists.txt"));
        string shaderMake = File.ReadAllText(Path.Combine(_paths.SourceDirectory,
            "donut", "ShaderMake", "CMakeLists.txt"));
        string agilityUrl = ParseExactCMakeSetAssignment(root,
            "DONUT_D3D_AGILITY_SDK_URL");
        string directXHeadersTag = ParseExactCMakeSetAssignment(nvrhi,
            "NVRHI_DIRECTX_HEADERS_GIT_TAG");
        string dxcVersion = ParseExactCMakeSetAssignment(shaderMake,
            "SHADERMAKE_DXC_VERSION");
        string dxcDate = ParseExactCMakeSetAssignment(shaderMake,
            "SHADERMAKE_DXC_DATE");
        string expectedAgilityUrl =
            "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/" +
            ProductConstants.AgilitySdk.Version;
        if (!string.Equals(agilityUrl, expectedAgilityUrl, StringComparison.Ordinal) ||
            !string.Equals(directXHeadersTag,
                "v" + ProductConstants.DirectXHeaders.Version, StringComparison.Ordinal) ||
            !string.Equals(dxcVersion, "v" + ProductConstants.Dxc.Version,
                StringComparison.Ordinal) ||
            !string.Equals(dxcDate, PinnedDxcDate, StringComparison.Ordinal))
            throw new InstallerException(
                "Public UVSR main now requires newer build components. Update UVSR Launcher first.");
    }

    internal static string ParseExactCMakeSetAssignment(
        string contents,
        string variable)
    {
        if (string.IsNullOrWhiteSpace(contents) || string.IsNullOrWhiteSpace(variable) ||
            variable.Any(character => !(char.IsAsciiLetterOrDigit(character) ||
                character == '_')))
            throw new InstallerException("A required UVSR build-component assignment was invalid.");

        string activeContents = MaskCMakeComments(contents);
        string pattern =
            "^[\\t ]*(?i:set)[\\t ]*\\([\\t ]*" + Regex.Escape(variable) +
            "[\\t ]+(?:\"(?<quoted>[^\"\\r\\n]*)\"|(?<bare>[^\\s\\)]+))" +
            "(?:[\\t ]+CACHE[\\t ]+(?:BOOL|FILEPATH|PATH|STRING|INTERNAL)[\\t ]+" +
            "(?:\"[^\"\\r\\n]*\"|[^\\s\\)]+)(?:[\\t ]+FORCE)?)?" +
            "[\\t ]*\\)[\\t ]*(?:#.*)?$";
        MatchCollection matches = Regex.Matches(activeContents, pattern,
            RegexOptions.Multiline | RegexOptions.CultureInvariant);
        if (matches.Count != 1)
            throw new InstallerException(
                $"Public UVSR main has an unsupported {variable} build assignment. " +
                "Update UVSR Launcher first.");
        Match match = matches[0];
        return match.Groups["quoted"].Success
            ? match.Groups["quoted"].Value
            : match.Groups["bare"].Value;
    }

    private static string MaskCMakeComments(string contents)
    {
        char[] masked = contents.ToCharArray();
        bool quoted = false;
        for (int index = 0; index < contents.Length; index++)
        {
            char character = contents[index];
            if (quoted)
            {
                if (character == '\\' && index + 1 < contents.Length)
                {
                    index++;
                    continue;
                }
                if (character == '"')
                    quoted = false;
                continue;
            }
            if (character == '"')
            {
                quoted = true;
                continue;
            }
            if (character != '#')
                continue;

            int end;
            if (TryReadCMakeBracketComment(contents, index + 1,
                    out string closing, out int openerLength))
            {
                int contentStart = index + 1 + openerLength;
                int closingStart = contents.IndexOf(closing, contentStart,
                    StringComparison.Ordinal);
                end = closingStart < 0
                    ? contents.Length
                    : closingStart + closing.Length;
            }
            else
            {
                end = index;
                while (end < contents.Length && contents[end] is not '\r' and not '\n')
                    end++;
            }
            for (int commentIndex = index; commentIndex < end; commentIndex++)
            {
                if (masked[commentIndex] is not '\r' and not '\n')
                    masked[commentIndex] = ' ';
            }
            index = Math.Max(index, end - 1);
        }
        return new string(masked);
    }

    private static bool TryReadCMakeBracketComment(
        string contents,
        int openerStart,
        out string closing,
        out int openerLength)
    {
        closing = string.Empty;
        openerLength = 0;
        if (openerStart >= contents.Length || contents[openerStart] != '[')
            return false;
        int cursor = openerStart + 1;
        while (cursor < contents.Length && contents[cursor] == '=')
            cursor++;
        if (cursor >= contents.Length || contents[cursor] != '[')
            return false;
        int equalsCount = cursor - openerStart - 1;
        closing = "]" + new string('=', equalsCount) + "]";
        openerLength = cursor - openerStart + 1;
        return true;
    }

    private void ValidatePinnedCMakeIsSufficient()
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
            throw new InstallerException(
                $"Public UVSR main now requires CMake {required} or newer. Update UVSR Launcher before building.");
    }

    private async Task<ProcessResult> GitAsync(
        string git,
        IEnumerable<string> arguments,
        string workingDirectory,
        ToolPaths tools,
        InstallLog log,
        CancellationToken cancellationToken,
        bool forceHttp11 = false)
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
            BuildEnvironment(tools), log, cancellationToken, clearEnvironment: true);
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

    internal static IReadOnlyDictionary<string, string?> BuildEnvironment(ToolPaths tools)
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
            // Public main currently contains reviewed dependency patches with
            // mixed Windows/LF context endings. This fixed Git setting affects
            // context matching only and keeps the checked-out commit pristine.
            ["GIT_CONFIG_COUNT"] = "1",
            ["GIT_CONFIG_KEY_0"] = "apply.ignoreWhitespace",
            ["GIT_CONFIG_VALUE_0"] = "change",
            ["GIT_NO_REPLACE_OBJECTS"] = "1",
            ["GIT_TERMINAL_PROMPT"] = "0",
            ["GCM_INTERACTIVE"] = "Never",
            ["LC_ALL"] = "C",
            ["LANG"] = "C",
            ["PYTHONDONTWRITEBYTECODE"] = "1"
        };
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
