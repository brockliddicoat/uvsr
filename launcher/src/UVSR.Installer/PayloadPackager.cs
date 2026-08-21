using System.Security.Cryptography;

namespace UvsrInstaller;

internal sealed class PayloadPackager
{
    private const string NotoSansLicenseFile =
        "bin/licenses/Noto-Sans-OFL-1.1.txt";
    private const string TransitionalGeistLicenseFile =
        "bin/licenses/Geist-OFL-1.1.txt";
    private const string ProggyCleanLicenseFile =
        "bin/licenses/ProggyClean-MIT.txt";
    private const long ProggyCleanLicenseSize = 1082;
    private const string ProggyCleanLicenseSha256 =
        "8b802d79f256d29b45ad253323d212fa14ca952a20dcd227cfbcdb3d140bfe7c";
    private static readonly string[] LegacyUiFontFiles =
    {
        "media/fonts/System/CodexUI.ttf",
        "media/fonts/System/CodexUI-Semibold.ttf",
        "media/fonts/System/CodexUI-Bold.ttf"
    };
    private static readonly string[] NotoSansUiFontFiles =
    {
        "media/fonts/NotoSans/NotoSans-Regular.ttf",
        "media/fonts/NotoSans/NotoSans-SemiBold.ttf",
        "media/fonts/NotoSans/NotoSans-Bold.ttf"
    };
    private static readonly IReadOnlyDictionary<string, string>
        TransitionalUiFontAliases =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["media/fonts/System/CodexUI.ttf"] =
                    "media/fonts/NotoSans/NotoSans-Regular.ttf",
                ["media/fonts/System/CodexUI-Semibold.ttf"] =
                    "media/fonts/NotoSans/NotoSans-SemiBold.ttf",
                ["media/fonts/System/CodexUI-Bold.ttf"] =
                    "media/fonts/NotoSans/NotoSans-Bold.ttf"
            };
    private static readonly IReadOnlyDictionary<string, (long Size, string Sha256)>
        NotoSansPackageFiles =
            new Dictionary<string, (long Size, string Sha256)>(
                StringComparer.OrdinalIgnoreCase)
            {
                ["media/fonts/NotoSans/NotoSans-Regular.ttf"] =
                    (621572, "478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823"),
                ["media/fonts/NotoSans/NotoSans-SemiBold.ttf"] =
                    (625052, "a4e91fd530ac2b4ef5367240144ff37d7d65d66cf76f2e9a2187b93c676f92d0"),
                ["media/fonts/NotoSans/NotoSans-Bold.ttf"] =
                    (631484, "1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913"),
                [NotoSansLicenseFile] =
                    (4396, "cee9892f9f0cc8fe882c9e9537ee6a89621d86ee7ceaf70b02e2b2b1c25c061a")
            };

    private enum UiFontContract
    {
        Legacy,
        NotoSans,
        DualTransition
    }

    private readonly InstallerPaths _paths;

    internal PayloadPackager(InstallerPaths paths) => _paths = paths;

    internal PackageManifest Stage(
        Guid installationId,
        Guid transactionId,
        string versionId,
        string commit,
        InstallLog log)
    {
        ValidateBuildOutput(_paths.SourceDirectory, _paths.BuildDirectory);
        ValidateBuildOutputCanBeStaged(_paths.BuildDirectory,
            ProductConstants.LauncherReleaseSequence);
        string sourceBin = Path.Combine(_paths.BuildDirectory, "bin");
        string sourceMedia = Path.Combine(_paths.BuildDirectory, "media");
        string transactionRoot = Path.Combine(_paths.StagingDirectory, transactionId.ToString("N"));
        string packageRoot = Path.Combine(transactionRoot, "package");
        if (Directory.Exists(transactionRoot))
            SafePaths.DeleteOwnedTree(transactionRoot, _paths.StagingDirectory);
        SafePaths.RejectReparsePathChain(_paths.StagingDirectory,
            "UVSR package staging directory");
        Directory.CreateDirectory(Path.Combine(packageRoot, "bin"));
        SafePaths.RejectReparsePathChain(packageRoot, "UVSR package staging directory");

        CopyFile(sourceBin, Path.Combine(packageRoot, "bin"), "uvsr.exe");
        CopyDirectory(sourceBin, Path.Combine(packageRoot, "bin"), "D3D12");
        CopyDirectory(sourceBin, Path.Combine(packageRoot, "bin"), "shaders");
        CopyDirectory(sourceBin, Path.Combine(packageRoot, "bin"), "licenses");
        CopyFile(sourceBin, Path.Combine(packageRoot, "bin"), "third-party-notices.md");
        SafePaths.CopyDirectory(sourceMedia, Path.Combine(packageRoot, "media"));
        NormalizeStagedUiTransition(packageRoot);

        string executable = Path.Combine(packageRoot, "bin", "uvsr.exe");
        string hash = ComputeSha256(executable);
        IReadOnlyList<PackageFile> files = BuildFileManifest(packageRoot);
        PackageManifest manifest = new(ProductConstants.SchemaVersion, installationId,
            versionId, commit, hash, files, DateTimeOffset.UtcNow);
        JsonStore.WriteAtomic(Path.Combine(packageRoot, ".uvsr-package.json"), manifest);
        ValidatePackage(packageRoot, manifest);
        log.Write($"Staged UVSR package {versionId}; executable SHA-256 {hash}.");
        return manifest;
    }

    internal string Activate(Guid transactionId, PackageManifest manifest)
    {
        SafePaths.RejectReparsePathChain(_paths.VersionsDirectory,
            "UVSR managed versions directory");
        Directory.CreateDirectory(_paths.VersionsDirectory);
        SafePaths.RejectReparsePathChain(_paths.VersionsDirectory,
            "UVSR managed versions directory");
        string source = Path.Combine(_paths.StagingDirectory,
            transactionId.ToString("N"), "package");
        string destination = _paths.VersionRoot(manifest.VersionId);
        if (Directory.Exists(destination))
            throw new InstallerException("The candidate UVSR version already exists; no active version was changed.");
        Directory.Move(source, destination);
        ValidatePackage(destination, manifest);
        return destination;
    }

    internal static void ValidatePackage(string packageRoot, PackageManifest manifest)
    {
        SafePaths.RejectReparsePathChain(packageRoot, "UVSR runtime package");
        if (manifest.SchemaVersion != ProductConstants.SchemaVersion ||
            manifest.InstallationId == Guid.Empty ||
            string.IsNullOrWhiteSpace(manifest.VersionId) ||
            string.IsNullOrWhiteSpace(manifest.Commit) ||
            string.IsNullOrWhiteSpace(manifest.ExecutableSha256) ||
            !ProductConstants.VersionIdRegex().IsMatch(manifest.VersionId) ||
            !ProductConstants.CommitRegex().IsMatch(manifest.Commit) ||
            !ProductConstants.HashRegex().IsMatch(manifest.ExecutableSha256))
            throw new InstallerException("The staged UVSR package manifest is invalid.");
        if (manifest.Files is null || manifest.Files.Count == 0 || manifest.Files.Count > 100_000)
            throw new InstallerException("The staged UVSR package file inventory is invalid.");

        string[] requiredFiles =
        {
            "bin/uvsr.exe",
            "bin/D3D12/D3D12Core.dll",
            "bin/D3D12/D3D12SDKLayers.dll",
            "bin/D3D12/uvsr-runtime-contract.txt",
            "bin/third-party-notices.md"
        };
        Dictionary<string, PackageFile> recorded = new(StringComparer.OrdinalIgnoreCase);
        foreach (PackageFile? file in manifest.Files)
        {
            if (file is null || string.IsNullOrWhiteSpace(file.RelativePath) ||
                string.IsNullOrWhiteSpace(file.Sha256))
                throw new InstallerException("The staged UVSR package file inventory is invalid.");
            string normalized = NormalizeRelativePath(file.RelativePath);
            if (!string.Equals(normalized, file.RelativePath, StringComparison.Ordinal) ||
                file.Size < 0 || !ProductConstants.HashRegex().IsMatch(file.Sha256) ||
                !recorded.TryAdd(normalized, file))
                throw new InstallerException("The staged UVSR package file inventory is invalid.");
        }
        UiFontContract uiFontContract = ClassifyUiFontContract(recorded.Keys);
        ValidateUiLicenseContract(recorded.Keys, uiFontContract,
            "staged UVSR runtime");
        ValidateProggyCleanPackageNotice(recorded,
            required: false, "staged UVSR runtime");
        if (uiFontContract != UiFontContract.Legacy)
            ValidateNotoSansPackageFiles(recorded);
        if (uiFontContract == UiFontContract.DualTransition)
            ValidateTransitionalAliasRecords(recorded);
        if (requiredFiles.Any(path => !recorded.ContainsKey(path)) ||
            !HasFilesUnder(recorded, "bin/shaders/") ||
            !HasFilesUnder(recorded, "bin/licenses/") ||
            !HasFilesUnder(recorded, "media/glTF-Sample-Assets/Models/") ||
            !HasFilesUnder(recorded, "media/environments/") ||
            !HasFilesUnder(recorded, "media/uvsr/noise/"))
            throw new InstallerException("The staged UVSR runtime is incomplete.");

        IReadOnlyList<PackageFile> actual = BuildFileManifest(packageRoot);
        if (actual.Count != recorded.Count)
            throw new InstallerException("The staged UVSR runtime file inventory has changed.");
        foreach (PackageFile file in actual)
        {
            if (!recorded.TryGetValue(file.RelativePath, out PackageFile? expected) ||
                expected.Size != file.Size ||
                !string.Equals(expected.Sha256, file.Sha256, StringComparison.Ordinal))
                throw new InstallerException($"The staged UVSR runtime file '{file.RelativePath}' failed its integrity check.");
        }
        if (!recorded.TryGetValue("bin/uvsr.exe", out PackageFile? executable) ||
            !string.Equals(executable.Sha256, manifest.ExecutableSha256, StringComparison.Ordinal))
            throw new InstallerException("The staged UVSR executable failed its integrity check.");
        ValidateD3D12RuntimeContract(packageRoot);
    }

    internal static string ComputeSha256(string path)
    {
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    internal static void ValidateBuildOutput(
        string sourceDirectory,
        string buildDirectory)
    {
        string sourceRoot = Path.GetFullPath(sourceDirectory);
        string buildRoot = Path.GetFullPath(buildDirectory);
        string bin = Path.Combine(buildRoot, "bin");
        string media = Path.Combine(buildRoot, "media");
        string[] fixedFiles =
        {
            Path.Combine(bin, "uvsr.exe"),
            Path.Combine(bin, "D3D12", "D3D12Core.dll"),
            Path.Combine(bin, "D3D12", "D3D12SDKLayers.dll"),
            Path.Combine(bin, "D3D12", "uvsr-runtime-contract.txt"),
            Path.Combine(bin, "third-party-notices.md")
        };
        string[] legalFiles =
        {
            "UVSR-Polyform-Noncommercial-1.0.0.md",
            "Microsoft-DirectX-Graphics-Samples.txt",
            "Apache-2.0.txt", "BSD-2-Clause.txt", "IOLITE-AgX-MIT.txt",
            "Google-Filament-FXAA-Attribution.md", "NVIDIA-Donut-MIT.txt",
            "Donut-Third-Party-Licenses.txt", "NVIDIA-NVRHI-MIT.txt",
            "NVIDIA-ShaderMake-MIT.txt", "Dear-ImGui-MIT.txt",
            "Intel-PBR-Sponza.txt", "Amazon-Lumberyard-Bistro.txt",
            "San-Miguel-2.1.txt", "Blender-Classroom-CC0-1.0.txt",
            "Poly-Haven-Environments.md", "Microsoft-DirectX-Headers-MIT.txt",
            "Microsoft-D3D12-Agility-SDK-Terms.txt",
            "Microsoft-D3D12-Agility-SDK-Code-MIT.txt"
        };
        if (!Directory.Exists(bin) || !Directory.Exists(media) ||
            fixedFiles.Any(path => !File.Exists(path)) ||
            legalFiles.Any(name => !File.Exists(Path.Combine(bin, "licenses", name))))
        {
            throw new InstallerException("The completed build did not contain a runnable UVSR package.");
        }

        UiFontContract uiFontContract = ValidateUiFontOutput(media);
        ValidateBuildUiLicenseContract(bin, uiFontContract);
        if (uiFontContract != UiFontContract.Legacy)
            ValidateNotoSansBuildFiles(buildRoot);
        if (uiFontContract == UiFontContract.DualTransition)
            ValidateTransitionalBuildAliases(buildRoot);

        ValidateRelativeManifest(
            Path.Combine(buildRoot, "uvsr_runtime_shader_paths.manifest"),
            Path.Combine(bin, "shaders"));
        ValidateSourceManifest(
            Path.Combine(buildRoot, "uvsr_environment_assets.manifest"),
            Path.Combine(sourceRoot, "assets", "environments"),
            Path.Combine(media, "environments"));
        ValidateSourceManifest(
            Path.Combine(buildRoot, "uvsr_noise_assets.manifest"),
            Path.Combine(sourceRoot, "assets", "noise"),
            Path.Combine(media, "uvsr", "noise"));
        ValidateSourceManifest(
            Path.Combine(buildRoot, "scene_runtime_assets.manifest"),
            Path.Combine(sourceRoot, "assets", "scenes"),
            Path.Combine(media, "glTF-Sample-Assets", "Models"));
        ValidateD3D12RuntimeContract(buildRoot);
    }

    internal static void ValidateBuildOutputCanBeStaged(
        string buildDirectory,
        long launcherReleaseSequence)
    {
        if (launcherReleaseSequence < 1 ||
            launcherReleaseSequence > ProductConstants.MaximumReleaseSequence)
            throw new ArgumentOutOfRangeException(nameof(launcherReleaseSequence));
        if (launcherReleaseSequence < 10)
            return;
        UiFontContract contract = ValidateUiFontOutput(
            Path.Combine(Path.GetFullPath(buildDirectory), "media"));
        if (contract == UiFontContract.Legacy)
        {
            throw new InstallerException(
                "UVSR Launcher will not create a new renderer package from the " +
                "legacy CodexUI font inventory. The checked-out UVSR source and " +
                "this launcher are out of sync. Retry after matching public UVSR " +
                "source with the bundled Noto Sans assets is published. Existing " +
                "legacy installations remain available for recovery.");
        }
    }

    internal static void ValidateD3D12RuntimeContract(string packageRoot)
    {
        string runtimeRoot = Path.Combine(packageRoot, "bin", "D3D12");
        string contractPath = Path.Combine(runtimeRoot, "uvsr-runtime-contract.txt");
        string corePath = Path.Combine(runtimeRoot, "D3D12Core.dll");
        SafePaths.RejectReparsePathChain(contractPath,
            "UVSR Direct3D runtime contract");
        FileInfo contractInfo = new(contractPath);
        if (!contractInfo.Exists || contractInfo.Length is <= 0 or > 4096 ||
            !File.Exists(corePath))
            throw new InstallerException(
                "The completed build is missing its DirectX 12 runtime contract.");

        string[] lines = File.ReadAllText(contractPath)
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Split('\n', StringSplitOptions.None);
        if (lines.Length != 5 || lines[4].Length != 0 ||
            lines[0] != "schemaVersion=1" ||
            lines[1] != $"sdkVersion={ProductConstants.D3D12AgilitySdkVersion}" ||
            lines[2] != @"sdkPath=.\D3D12\" ||
            !lines[3].StartsWith("coreSha256=", StringComparison.Ordinal))
            throw new InstallerException(
                "The completed build has an invalid DirectX 12 runtime contract.");

        string recordedHash = lines[3]["coreSha256=".Length..];
        if (!ProductConstants.HashRegex().IsMatch(recordedHash) ||
            !string.Equals(recordedHash, ComputeSha256(corePath),
                StringComparison.Ordinal))
            throw new InstallerException(
                "The packaged DirectX 12 runtime does not match the verified build.");
    }

    private static void ValidateRelativeManifest(string manifestPath, string outputRoot)
    {
        HashSet<string> expected = ReadManifestLines(manifestPath)
            .Select(NormalizeRelativePath)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        ValidateExactOutputSet(expected, outputRoot);
    }

    private static void ValidateSourceManifest(
        string manifestPath,
        string sourceRoot,
        string outputRoot)
    {
        HashSet<string> expected = new(StringComparer.OrdinalIgnoreCase);
        foreach (string source in ReadManifestLines(manifestPath))
        {
            string fullSource = Path.GetFullPath(source);
            if (!SafePaths.IsStrictDescendant(fullSource, sourceRoot))
                throw new InstallerException("A generated UVSR asset manifest escaped its source directory.");
            expected.Add(NormalizeRelativePath(Path.GetRelativePath(sourceRoot, fullSource)));
        }
        ValidateExactOutputSet(expected, outputRoot);
    }

    private static IReadOnlyList<string> ReadManifestLines(string manifestPath)
    {
        if (!File.Exists(manifestPath))
            throw new InstallerException("The completed build is missing a generated runtime manifest.");
        SafePaths.RejectReparsePathChain(manifestPath, "generated UVSR runtime manifest");
        string[] lines = File.ReadAllLines(manifestPath)
            .Select(line => line.Trim())
            .Where(line => line.Length > 0)
            .ToArray();
        if (lines.Length == 0 || lines.Length > 100_000)
            throw new InstallerException("A generated UVSR runtime manifest is invalid.");
        return lines;
    }

    private static void ValidateExactOutputSet(
        IReadOnlySet<string> expected,
        string outputRoot)
    {
        if (!Directory.Exists(outputRoot))
            throw new InstallerException("The completed build is missing a runtime asset directory.");
        HashSet<string> actual = Directory.EnumerateFiles(outputRoot, "*",
                SearchOption.AllDirectories)
            .Select(path => NormalizeRelativePath(Path.GetRelativePath(outputRoot, path)))
            .Where(path => !path.EndsWith("/.uvsr-stage.stamp", StringComparison.OrdinalIgnoreCase) &&
                           !string.Equals(path, ".uvsr-stage.stamp", StringComparison.OrdinalIgnoreCase))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        if (!expected.SetEquals(actual))
            throw new InstallerException(
                "The completed build does not match its generated runtime asset manifest.");
    }

    private static IReadOnlyList<PackageFile> BuildFileManifest(string packageRoot)
    {
        List<PackageFile> files = new();
        EnumerateFiles(packageRoot, packageRoot, files);
        return files.OrderBy(file => file.RelativePath, StringComparer.Ordinal).ToArray();
    }

    private static void EnumerateFiles(string packageRoot, string directory, List<PackageFile> files)
    {
        SafePaths.RejectReparsePathChain(directory, "UVSR runtime package directory");
        foreach (string childDirectory in Directory.EnumerateDirectories(directory,
                     "*", SearchOption.TopDirectoryOnly))
        {
            SafePaths.RejectReparsePoint(childDirectory, "UVSR runtime package directory");
            EnumerateFiles(packageRoot, childDirectory, files);
        }
        foreach (string file in Directory.EnumerateFiles(directory, "*", SearchOption.TopDirectoryOnly))
        {
            SafePaths.RejectReparsePoint(file, "UVSR runtime package file");
            if (string.Equals(Path.GetFileName(file), ".uvsr-package.json",
                    StringComparison.OrdinalIgnoreCase) &&
                string.Equals(Path.GetDirectoryName(file), packageRoot,
                    StringComparison.OrdinalIgnoreCase))
                continue;
            FileInfo info = new(file);
            string relative = NormalizeRelativePath(Path.GetRelativePath(packageRoot, file));
            files.Add(new PackageFile(relative, info.Length, ComputeSha256(file)));
        }
    }

    private static string NormalizeRelativePath(string relative)
    {
        if (Path.IsPathRooted(relative) || relative.Contains(':'))
            throw new InstallerException("A UVSR package contains an invalid relative path.");
        string normalized = relative.Replace('\\', '/');
        string combined = SafePaths.CombineDescendant("C:\\uvsr-package-root", normalized);
        string roundTrip = Path.GetRelativePath("C:\\uvsr-package-root", combined).Replace('\\', '/');
        if (roundTrip.StartsWith("../", StringComparison.Ordinal) || roundTrip == "..")
            throw new InstallerException("A UVSR package path escaped its package root.");
        return roundTrip;
    }

    private static bool HasFilesUnder(
        IReadOnlyDictionary<string, PackageFile> files,
        string prefix) => files.Keys.Any(path => path.StartsWith(prefix,
        StringComparison.OrdinalIgnoreCase));

    internal static void NormalizeStagedUiTransition(string packageRoot)
    {
        string root = Path.GetFullPath(packageRoot);
        SafePaths.RejectReparsePathChain(root,
            "UVSR staged runtime package");
        Dictionary<string, PackageFile> files = BuildStagedUiRecords(root);
        UiFontContract contract = ClassifyUiFontContract(files.Keys);
        ValidateUiLicenseContract(files.Keys, contract,
            "staged UVSR runtime");
        ValidateProggyCleanPackageNotice(files,
            required: contract != UiFontContract.Legacy,
            "staged UVSR runtime");
        if (contract != UiFontContract.Legacy)
            ValidateNotoSansPackageFiles(files);
        if (contract != UiFontContract.DualTransition)
            return;

        ValidateTransitionalAliasRecords(files);
        string fontsRoot = Path.Combine(root, "media", "fonts");
        SafePaths.DeleteOwnedTree(
            Path.Combine(fontsRoot, "System"),
            fontsRoot);
        string licensesRoot = Path.Combine(root, "bin", "licenses");
        SafePaths.DeleteOwnedTree(
            Path.Combine(licensesRoot, "Geist-OFL-1.1.txt"),
            licensesRoot);

        Dictionary<string, PackageFile> normalized = BuildStagedUiRecords(root);
        if (ClassifyUiFontContract(normalized.Keys) != UiFontContract.NotoSans)
            throw new InstallerException(
                "The staged UVSR runtime font transition could not be normalized.");
        ValidateUiLicenseContract(normalized.Keys, UiFontContract.NotoSans,
            "normalized UVSR runtime");
        ValidateProggyCleanPackageNotice(normalized,
            required: true, "normalized UVSR runtime");
        ValidateNotoSansPackageFiles(normalized);
    }

    private static UiFontContract ClassifyUiFontContract(
        IEnumerable<string> files)
    {
        HashSet<string> actual = files
            .Where(path => path.StartsWith("media/fonts/",
                StringComparison.OrdinalIgnoreCase))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        HashSet<string> legacy = LegacyUiFontFiles
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        if (actual.SetEquals(legacy))
            return UiFontContract.Legacy;
        HashSet<string> notoSans = NotoSansUiFontFiles
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        if (actual.SetEquals(notoSans))
            return UiFontContract.NotoSans;
        legacy.UnionWith(notoSans);
        if (actual.SetEquals(legacy))
            return UiFontContract.DualTransition;
        throw new InstallerException(
            "The UVSR runtime has an incomplete, mixed, or unknown UI font contract.");
    }

    private static UiFontContract ValidateUiFontOutput(string mediaRoot)
    {
        string fontsRoot = Path.Combine(mediaRoot, "fonts");
        if (!Directory.Exists(fontsRoot))
        {
            throw new InstallerException(
                "The completed build is missing its UI font directory.");
        }
        IReadOnlyList<string> files = BuildFileManifest(fontsRoot)
            .Select(file => "media/fonts/" + file.RelativePath)
            .ToArray();
        return ClassifyUiFontContract(files);
    }

    private static void ValidateUiLicenseContract(
        IEnumerable<string> files,
        UiFontContract contract,
        string description)
    {
        HashSet<string> inventory = files.ToHashSet(
            StringComparer.OrdinalIgnoreCase);
        bool hasNotoSans = inventory.Contains(NotoSansLicenseFile);
        bool hasGeist = inventory.Contains(TransitionalGeistLicenseFile);
        bool expectsNotoSans = contract != UiFontContract.Legacy;
        bool expectsGeist = contract != UiFontContract.NotoSans;
        if (hasNotoSans != expectsNotoSans || hasGeist != expectsGeist)
        {
            throw new InstallerException(
                $"The {description} has an incomplete or mixed UI font license contract.");
        }
    }

    private static void ValidateBuildUiLicenseContract(
        string binRoot,
        UiFontContract contract)
    {
        string licensesRoot = Path.Combine(binRoot, "licenses");
        List<string> present = new();
        foreach (string relative in new[]
                 {
                     NotoSansLicenseFile,
                     TransitionalGeistLicenseFile
                 })
        {
            string name = relative["bin/licenses/".Length..];
            if (File.Exists(Path.Combine(licensesRoot, name)))
                present.Add(relative);
        }
        ValidateUiLicenseContract(present, contract, "completed build");
        ValidateProggyCleanBuildNotice(
            licensesRoot,
            required: contract != UiFontContract.Legacy);
    }

    private static Dictionary<string, PackageFile> BuildStagedUiRecords(
        string packageRoot)
    {
        string fontsRoot = Path.Combine(packageRoot, "media", "fonts");
        if (!Directory.Exists(fontsRoot))
            throw new InstallerException(
                "The staged UVSR runtime is missing its UI font directory.");
        Dictionary<string, PackageFile> files = BuildFileManifest(fontsRoot)
            .Select(file => new PackageFile(
                "media/fonts/" + file.RelativePath,
                file.Size,
                file.Sha256))
            .ToDictionary(file => file.RelativePath,
                StringComparer.OrdinalIgnoreCase);
        foreach (string relative in new[]
                 {
                     NotoSansLicenseFile,
                     TransitionalGeistLicenseFile,
                     ProggyCleanLicenseFile
                 })
        {
            string path = Path.Combine(packageRoot,
                relative.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(path))
                continue;
            SafePaths.RejectReparsePathChain(path,
                "UVSR staged UI font license");
            FileInfo info = new(path);
            files.Add(relative, new PackageFile(
                relative,
                info.Length,
                ComputeSha256(path)));
        }
        return files;
    }

    private static void ValidateProggyCleanPackageNotice(
        IReadOnlyDictionary<string, PackageFile> files,
        bool required,
        string description)
    {
        if (!files.TryGetValue(ProggyCleanLicenseFile,
                out PackageFile? actual))
        {
            if (required)
            {
                throw new InstallerException(
                    $"The {description} is missing its ProggyClean MIT notice.");
            }
            return;
        }
        if (actual.Size != ProggyCleanLicenseSize ||
            !string.Equals(actual.Sha256, ProggyCleanLicenseSha256,
                StringComparison.Ordinal))
        {
            throw new InstallerException(
                $"The {description} does not contain the exact ProggyClean MIT notice.");
        }
    }

    private static void ValidateProggyCleanBuildNotice(
        string licensesRoot,
        bool required)
    {
        string path = Path.Combine(
            licensesRoot,
            ProggyCleanLicenseFile["bin/licenses/".Length..]);
        FileInfo actual = new(path);
        if (!actual.Exists)
        {
            if (required)
            {
                throw new InstallerException(
                    "The completed build is missing its ProggyClean MIT notice.");
            }
            return;
        }
        SafePaths.RejectReparsePathChain(path,
            "UVSR ProggyClean MIT notice");
        if (actual.Length != ProggyCleanLicenseSize ||
            !string.Equals(ComputeSha256(path), ProggyCleanLicenseSha256,
                StringComparison.Ordinal))
        {
            throw new InstallerException(
                "The completed build does not contain the exact ProggyClean MIT notice.");
        }
    }

    private static void ValidateNotoSansPackageFiles(
        IReadOnlyDictionary<string, PackageFile> files)
    {
        foreach (KeyValuePair<string, (long Size, string Sha256)> expected in
                 NotoSansPackageFiles)
        {
            if (!files.TryGetValue(expected.Key, out PackageFile? actual) ||
                actual.Size != expected.Value.Size ||
                !string.Equals(actual.Sha256, expected.Value.Sha256,
                    StringComparison.Ordinal))
            {
                throw new InstallerException(
                    "The staged UVSR runtime does not contain the exact Noto Sans v2.015 files.");
            }
        }
    }

    private static void ValidateNotoSansBuildFiles(string buildRoot)
    {
        foreach (KeyValuePair<string, (long Size, string Sha256)> expected in
                 NotoSansPackageFiles)
        {
            string path = Path.Combine(buildRoot,
                expected.Key.Replace('/', Path.DirectorySeparatorChar));
            FileInfo actual = new(path);
            if (!actual.Exists || actual.Length != expected.Value.Size ||
                !string.Equals(ComputeSha256(path), expected.Value.Sha256,
                    StringComparison.Ordinal))
            {
                throw new InstallerException(
                    "The completed build does not contain the exact Noto Sans v2.015 files.");
            }
        }
    }

    private static void ValidateTransitionalAliasRecords(
        IReadOnlyDictionary<string, PackageFile> files)
    {
        foreach (KeyValuePair<string, string> alias in
                 TransitionalUiFontAliases)
        {
            if (!files.TryGetValue(alias.Key, out PackageFile? compatibility) ||
                !files.TryGetValue(alias.Value, out PackageFile? canonical) ||
                compatibility.Size != canonical.Size ||
                !string.Equals(compatibility.Sha256, canonical.Sha256,
                    StringComparison.Ordinal))
            {
                throw new InstallerException(
                    "The UVSR runtime has an invalid transitional UI font alias.");
            }
        }
    }

    private static void ValidateTransitionalBuildAliases(string buildRoot)
    {
        foreach (KeyValuePair<string, string> alias in
                 TransitionalUiFontAliases)
        {
            string compatibility = Path.Combine(buildRoot,
                alias.Key.Replace('/', Path.DirectorySeparatorChar));
            string canonical = Path.Combine(buildRoot,
                alias.Value.Replace('/', Path.DirectorySeparatorChar));
            FileInfo compatibilityInfo = new(compatibility);
            FileInfo canonicalInfo = new(canonical);
            if (!compatibilityInfo.Exists || !canonicalInfo.Exists ||
                compatibilityInfo.Length != canonicalInfo.Length ||
                !string.Equals(ComputeSha256(compatibility),
                    ComputeSha256(canonical), StringComparison.Ordinal))
            {
                throw new InstallerException(
                    "The completed build has an invalid transitional UI font alias.");
            }
        }
    }

    private static void CopyDirectory(string sourceParent, string destinationParent, string name)
    {
        string source = Path.Combine(sourceParent, name);
        if (!Directory.Exists(source))
            throw new InstallerException($"The build is missing its required '{name}' directory.");
        SafePaths.CopyDirectory(source, Path.Combine(destinationParent, name));
    }

    private static void CopyFile(string sourceParent, string destinationParent, string name)
    {
        string source = Path.Combine(sourceParent, name);
        if (!File.Exists(source))
            throw new InstallerException($"The build is missing its required '{name}' file.");
        SafePaths.RejectReparsePoint(source, $"build output {name}");
        File.Copy(source, Path.Combine(destinationParent, name), overwrite: false);
    }
}
