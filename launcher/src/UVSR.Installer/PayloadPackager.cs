using System.Diagnostics;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;

namespace UvsrInstaller;

internal sealed class PayloadPackager
{
    private const string ShaderInventoryResource =
        "UVSR.Installer.RuntimeShaderInventory.def";
    private const string MediaInventoryResource =
        "UVSR.Installer.RuntimeMediaInventory.def";
    private static readonly (IReadOnlySet<string> Paths,
        IReadOnlySet<string> Directories) ShaderInventory = LoadShaderInventory();
    private static readonly (IReadOnlySet<string> ProtectedPaths,
        IReadOnlySet<string> MediaPaths,
        IReadOnlySet<string> NoticePaths,
        IReadOnlySet<string> Directories) MediaInventory = LoadMediaInventory();
    internal static IReadOnlySet<string> RuntimeShaderPaths => ShaderInventory.Paths;
    internal static IReadOnlySet<string> RuntimeMediaPaths =>
        MediaInventory.MediaPaths;
    internal static IReadOnlySet<string> RequiredMediaNoticePaths =>
        MediaInventory.NoticePaths;
    internal static IReadOnlySet<string> ProtectedMediaAndNoticePaths =>
        MediaInventory.ProtectedPaths;
    private readonly InstallerPaths _paths;

    internal PayloadPackager(InstallerPaths paths) => _paths = paths;

    internal PackageManifest StageArchive(
        Guid transactionId,
        string versionId,
        string archivePath,
        RendererFeed feed,
        InstallLog log)
    {
        RendererPackageContract.ValidateFeed(feed);
        FileInfo archiveInfo = new(archivePath);
        if (!archiveInfo.Exists || archiveInfo.Length != feed.Artifact.Size ||
            !FixedTimeHashEquals(ComputeSha256(archivePath), feed.Artifact.Sha256))
            throw new InstallerException(
                "The renderer package did not match its signed feed identity.");

        string transactionRoot = Path.Combine(_paths.StagingDirectory,
            transactionId.ToString("N"));
        string packageRoot = Path.Combine(transactionRoot, "package");
        if (Directory.Exists(transactionRoot))
            SafePaths.DeleteOwnedTree(transactionRoot, _paths.StagingDirectory);
        SafePaths.RejectReparsePathChain(_paths.StagingDirectory,
            "UVSR package staging directory");
        Directory.CreateDirectory(transactionRoot);
        try
        {
            long expandedBytes = ValidateArchiveInventory(archivePath);
            string driveRoot = Path.GetPathRoot(packageRoot)
                ?? throw new InstallerException(
                    "The renderer staging drive could not be identified.");
            if (new DriveInfo(driveRoot).AvailableFreeSpace <
                checked(expandedBytes + 1024L * 1024 * 1024))
                throw new InstallerException(
                    "The renderer package does not fit safely in staging.");
            SafePaths.ExtractVerifiedZip(archivePath, packageRoot,
                ProductConstants.MaximumRendererExpandedBytes);
            PackageManifest manifest = ReadManifest(packageRoot);
            ValidateFeedBinding(manifest, feed);
            ValidatePackage(packageRoot, manifest);
            log.Write($"Staged signed renderer package sequence " +
                      $"{feed.ReleaseSequence}; engine {manifest.EngineVersion}; " +
                      $"settings {manifest.SettingsHash}; executable SHA-256 " +
                      $"{manifest.ExecutableSha256}.");
            return manifest;
        }
        catch
        {
            if (Directory.Exists(transactionRoot))
                SafePaths.DeleteOwnedTree(transactionRoot, _paths.StagingDirectory);
            throw;
        }
    }

    internal string Activate(
        Guid transactionId,
        string versionId,
        PackageManifest manifest)
    {
        SafePaths.RejectReparsePathChain(_paths.VersionsDirectory,
            "UVSR managed versions directory");
        Directory.CreateDirectory(_paths.VersionsDirectory);
        string source = Path.Combine(_paths.StagingDirectory,
            transactionId.ToString("N"), "package");
        string destination = _paths.VersionRoot(versionId);
        if (Directory.Exists(destination))
            throw new InstallerException(
                "The candidate UVSR version already exists; no active version was changed.");
        Directory.Move(source, destination);
        ValidatePackage(destination, manifest);
        return destination;
    }

    internal static PackageManifest ReadManifest(string packageRoot)
    {
        string path = Path.Combine(packageRoot, ProductConstants.PackageManifestName);
        SafePaths.RejectReparsePathChain(path, "renderer package manifest");
        FileInfo info = new(path);
        if (!info.Exists || info.Length is <= 0 or >
            ProductConstants.MaximumPackageManifestBytes)
            throw new InstallerException("The renderer package manifest was missing or invalid.");
        try
        {
            byte[] data = File.ReadAllBytes(path);
            LauncherUpdateFeedVerifier.RejectDuplicateJsonProperties(data);
            return JsonSerializer.Deserialize<PackageManifest>(data, JsonStore.Options)
                ?? throw new JsonException("The manifest was empty.");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or
                                   JsonException or InvalidOperationException)
        {
            throw new InstallerException(
                "The renderer package manifest could not be read safely.", ex);
        }
    }

    internal static void ValidatePackage(
        string packageRoot,
        PackageManifest manifest)
    {
        SafePaths.RejectReparseTree(packageRoot, "UVSR runtime package");
        RendererPackageContract.ValidateManifest(manifest);

        Dictionary<string, PackageFile> recorded =
            new(StringComparer.OrdinalIgnoreCase);
        foreach (PackageFile? file in manifest.Files)
        {
            if (file is null ||
                string.IsNullOrWhiteSpace(file.RelativePath) ||
                string.IsNullOrWhiteSpace(file.Sha256) ||
                file.Size < 0 ||
                !ProductConstants.HashRegex().IsMatch(file.Sha256))
                throw new InstallerException(
                    "The renderer package file inventory was invalid.");
            string normalized = NormalizeRelativePath(file.RelativePath);
            if (!string.Equals(normalized, file.RelativePath,
                    StringComparison.Ordinal) ||
                !IsAllowedPackageFile(normalized) ||
                !recorded.TryAdd(normalized, file))
                throw new InstallerException(
                    "The renderer package file inventory was invalid.");
        }

        if (!recorded.ContainsKey($"bin/{ProductConstants.EngineExecutableName}") ||
            !recorded.ContainsKey("bin/D3D12/D3D12Core.dll") ||
            !recorded.ContainsKey(ProductConstants.SettingsContractRelativePath) ||
            recorded.Keys.Count(path => path.StartsWith("bin/shaders/",
                StringComparison.Ordinal)) != ShaderInventory.Paths.Count ||
            ShaderInventory.Paths.Any(path => !recorded.ContainsKey(path)) ||
            MediaInventory.ProtectedPaths.Any(path =>
                !recorded.ContainsKey(path)))
            throw new InstallerException("The renderer runtime package was incomplete.");

        IReadOnlyList<PackageFile> actual = BuildFileManifest(packageRoot);
        if (actual.Count != recorded.Count)
            throw new InstallerException(
                "The renderer package file inventory has changed.");
        foreach (PackageFile file in actual)
        {
            if (!recorded.TryGetValue(file.RelativePath, out PackageFile? expected) ||
                !string.Equals(expected.RelativePath, file.RelativePath,
                    StringComparison.Ordinal) ||
                expected.Size != file.Size ||
                !FixedTimeHashEquals(expected.Sha256, file.Sha256))
                throw new InstallerException(
                    $"The renderer package file '{file.RelativePath}' failed its integrity check.");
        }

        PackageFile executable =
            recorded[$"bin/{ProductConstants.EngineExecutableName}"];
        if (!FixedTimeHashEquals(executable.Sha256, manifest.ExecutableSha256))
            throw new InstallerException(
                "The renderer executable failed its integrity check.");
        ValidateEngineMetadata(
            Path.Combine(packageRoot, "bin", ProductConstants.EngineExecutableName),
            manifest);
        ValidateSettingsContract(packageRoot, manifest);
    }

    internal static void ValidateFeedBinding(
        PackageManifest manifest,
        RendererFeed feed)
    {
        RendererPackageContract.ValidateManifest(manifest);
        RendererPackageContract.ValidateFeed(feed);
        if (manifest.ReleaseSequence != feed.ReleaseSequence ||
            !string.Equals(manifest.SourceCommit, feed.SourceCommit,
                StringComparison.Ordinal) ||
            !string.Equals(manifest.SettingsHash, feed.SettingsHash,
                StringComparison.Ordinal) ||
            !string.Equals(manifest.EngineVersion, feed.EngineVersion,
                StringComparison.Ordinal))
            throw new InstallerException(
                "The renderer package metadata did not match its signed feed.");
    }

    private static long ValidateArchiveInventory(string archivePath)
    {
        try
        {
            using ZipArchive archive = ZipFile.OpenRead(archivePath);
            if (archive.Entries.Count == 0 || archive.Entries.Count > 100_001)
                throw new InstallerException(
                    "The renderer package archive inventory was invalid.");
            HashSet<string> entries = new(StringComparer.OrdinalIgnoreCase);
            long expandedBytes = 0;
            foreach (ZipArchiveEntry entry in archive.Entries)
            {
                string name = entry.FullName;
                if (string.IsNullOrEmpty(name) ||
                    name.Contains(Path.DirectorySeparatorChar) ||
                    name.StartsWith('/') || name.Contains(':'))
                    throw new InstallerException(
                        "The renderer package archive contained an unsafe path.");
                string normalized = name.TrimEnd('/');
                if (normalized.Length == 0 ||
                    normalized.Split('/').Any(part => part is "" or "." or "..") ||
                    !entries.Add(normalized))
                    throw new InstallerException(
                        "The renderer package archive inventory was invalid.");
                bool directory = name.EndsWith('/');
                if (!directory && normalized != ProductConstants.PackageManifestName &&
                    !IsAllowedPackageFile(normalized))
                    throw new InstallerException(
                        $"The renderer package contained an unexpected file '{normalized}'.");
                if (directory && !IsAllowedPackageDirectory(normalized))
                    throw new InstallerException(
                        $"The renderer package contained an unexpected directory '{normalized}'.");
                checked { expandedBytes += entry.Length; }
                if (expandedBytes > ProductConstants.MaximumRendererExpandedBytes)
                    throw new InstallerException(
                        "The renderer package expanded size exceeded its safe limit.");
            }
            if (!entries.Contains(ProductConstants.PackageManifestName) ||
                !entries.Contains($"bin/{ProductConstants.EngineExecutableName}"))
                throw new InstallerException(
                    "The renderer package archive was incomplete.");
            return expandedBytes;
        }
        catch (OverflowException ex)
        {
            throw new InstallerException(
                "The renderer package expanded size was invalid.", ex);
        }
        catch (InvalidDataException ex)
        {
            throw new InstallerException(
                "The renderer package was not a valid ZIP archive.", ex);
        }
    }

    private static bool IsAllowedPackageFile(string path)
    {
        if (IsForbiddenDeveloperFile(path))
            return false;
        return path == $"bin/{ProductConstants.EngineExecutableName}" ||
               path == ProductConstants.SettingsContractRelativePath ||
               path == "bin/D3D12/D3D12Core.dll" ||
               ShaderInventory.Paths.Contains(path) ||
               path.StartsWith("bin/licenses/", StringComparison.Ordinal) ||
               MediaInventory.MediaPaths.Contains(path);
    }

    private static bool IsAllowedPackageDirectory(string path)
    {
        if (path is "bin" or "media" or "bin/D3D12" or "bin/shaders" or
            "bin/licenses" or "bin/settings")
            return true;
        return ShaderInventory.Directories.Contains(path) ||
               path.StartsWith("bin/licenses/", StringComparison.Ordinal) ||
               MediaInventory.Directories.Contains(path);
    }

    private static (IReadOnlySet<string> Paths,
        IReadOnlySet<string> Directories) LoadShaderInventory()
    {
        using Stream stream = typeof(PayloadPackager).Assembly
            .GetManifestResourceStream(ShaderInventoryResource)
            ?? throw new InstallerException(
                "The launcher runtime shader inventory resource was missing.");
        using StreamReader reader = new(stream, System.Text.Encoding.UTF8,
            detectEncodingFromByteOrderMarks: false);
        string[] paths = reader.ReadToEnd().Replace("\r\n", "\n",
                StringComparison.Ordinal)
            .Split('\n', StringSplitOptions.RemoveEmptyEntries);
        string[] sorted = paths.OrderBy(path => path,
            StringComparer.Ordinal).Distinct(StringComparer.Ordinal).ToArray();
        if (paths.Length != 47 || !paths.SequenceEqual(sorted,
                StringComparer.Ordinal) || paths.Any(path =>
                !path.StartsWith("bin/shaders/", StringComparison.Ordinal) ||
                !path.EndsWith(".bin", StringComparison.Ordinal) ||
                path.Split('/').Any(part => part is "" or "." or "..")))
            throw new InstallerException(
                "The launcher runtime shader inventory resource was invalid.");
        HashSet<string> directorySet = new(StringComparer.Ordinal);
        foreach (string path in paths)
        {
            string directory = path;
            while (directory.Contains('/'))
            {
                directory = directory[..directory.LastIndexOf('/')];
                if (!directory.StartsWith("bin/shaders", StringComparison.Ordinal))
                    break;
                directorySet.Add(directory);
            }
        }
        return (paths.ToHashSet(StringComparer.Ordinal), directorySet);
    }

    private static (IReadOnlySet<string> ProtectedPaths,
        IReadOnlySet<string> MediaPaths,
        IReadOnlySet<string> NoticePaths,
        IReadOnlySet<string> Directories) LoadMediaInventory()
    {
        using Stream stream = typeof(PayloadPackager).Assembly
            .GetManifestResourceStream(MediaInventoryResource)
            ?? throw new InstallerException(
                "The launcher retained-media inventory resource was missing.");
        using StreamReader reader = new(stream, System.Text.Encoding.UTF8,
            detectEncodingFromByteOrderMarks: false);
        string text = reader.ReadToEnd();
        if (text.Length == 0 || text.Contains('\r') ||
            !text.EndsWith('\n') || text.EndsWith("\n\n",
                StringComparison.Ordinal))
            throw new InstallerException(
                "The launcher retained-media inventory resource was invalid.");
        string[] paths = text[..^1].Split('\n');
        string[] sorted = paths.OrderBy(path => path,
            StringComparer.Ordinal).Distinct(StringComparer.Ordinal).ToArray();
        if (paths.Length != 310 || !paths.SequenceEqual(sorted,
                StringComparer.Ordinal) || paths.Any(path =>
                path.Length == 0 || path.Contains('\\') ||
                path.Split('/').Any(part => part is "" or "." or "..")))
            throw new InstallerException(
                "The launcher retained-media inventory resource was invalid.");

        HashSet<string> notices = paths.Where(path => path.StartsWith(
                "bin/licenses/", StringComparison.Ordinal))
            .ToHashSet(StringComparer.Ordinal);
        string[] media = paths.Where(path => path.StartsWith("media/",
            StringComparison.Ordinal)).ToArray();
        HashSet<string> mediaSet = media.ToHashSet(StringComparer.Ordinal);
        if (media.Length != 305 || notices.Count != 5 ||
            paths.Any(path => !path.StartsWith("media/",
                StringComparison.Ordinal) && !notices.Contains(path)) ||
            media.Count(IsEnvironmentPath) != 6 ||
            media.Count(IsNoisePath) != 13 ||
            media.Count(path => IsScenePath(path,
                "bistro_interior_retextured")) != 7 ||
            media.Count(path => IsScenePath(path,
                "san_miguel_retextured")) != 276 ||
            media.Count(IsNotoPath) != 3 ||
            media.Count(path => IsNoisePath(path) &&
                path.EndsWith("/manifest.json", StringComparison.Ordinal)) != 1 ||
            media.Count(path => IsScenePath(path,
                "bistro_interior_retextured") &&
                path.EndsWith(".scene.json", StringComparison.Ordinal)) != 1 ||
            media.Count(path => IsScenePath(path,
                "san_miguel_retextured") &&
                path.EndsWith(".scene.json", StringComparison.Ordinal)) != 1)
            throw new InstallerException(
                "The launcher retained-media inventory resource was invalid.");

        HashSet<string> directories = new(StringComparer.Ordinal);
        foreach (string path in media)
        {
            string directory = path;
            while (directory.Contains('/'))
            {
                directory = directory[..directory.LastIndexOf('/')];
                directories.Add(directory);
            }
        }
        return (paths.ToHashSet(StringComparer.Ordinal), mediaSet, notices,
            directories);
    }

    private static bool IsEnvironmentPath(string path) =>
        path.StartsWith("media/environments/", StringComparison.Ordinal) &&
        path.EndsWith(".hdr", StringComparison.Ordinal);

    private static bool IsNoisePath(string path)
    {
        const string prefix = "media/uvsr/noise/";
        if (!path.StartsWith(prefix, StringComparison.Ordinal))
            return false;
        string relative = path[prefix.Length..];
        return !relative.Contains('/') &&
            (relative == "manifest.json" ||
             relative.EndsWith(".bin", StringComparison.Ordinal));
    }

    private static bool IsScenePath(string path, string scene)
    {
        string prefix = $"media/glTF-Sample-Assets/Models/{scene}/";
        return path.StartsWith(prefix, StringComparison.Ordinal) &&
            (path.EndsWith(".scene.json", StringComparison.Ordinal) ||
             path.EndsWith(".gltf", StringComparison.Ordinal) ||
             path.EndsWith(".glb", StringComparison.Ordinal) ||
             path.EndsWith(".bin", StringComparison.Ordinal) ||
             path.EndsWith(".png", StringComparison.Ordinal));
    }

    private static bool IsNotoPath(string path)
    {
        const string prefix = "media/fonts/NotoSans/";
        if (!path.StartsWith(prefix, StringComparison.Ordinal))
            return false;
        string relative = path[prefix.Length..];
        return !relative.Contains('/') &&
            relative.EndsWith(".ttf", StringComparison.Ordinal);
    }

    private static bool IsForbiddenDeveloperFile(string path)
    {
        if (string.Equals(path, "bin/D3D12/D3D12SDKLayers.dll",
                StringComparison.OrdinalIgnoreCase))
            return true;
        string name = Path.GetFileName(path);
        if (name.StartsWith(".git", StringComparison.OrdinalIgnoreCase))
            return true;
        return Path.GetExtension(name).ToLowerInvariant() is
            ".py" or ".pyc" or ".ps1" or ".cmd" or ".bat" or ".cmake" or
            ".cpp" or ".cxx" or ".cc" or ".h" or ".hpp" or ".hlsl" or
            ".hlsli" or ".pdb" or ".ilk" or ".lib" or ".exp" or ".obj" or
            ".sln" or ".vcxproj";
    }

    private static IReadOnlyList<PackageFile> BuildFileManifest(string packageRoot)
    {
        List<PackageFile> files = new();
        foreach (string path in Directory.EnumerateFiles(packageRoot, "*",
                     SearchOption.AllDirectories))
        {
            SafePaths.RejectReparsePoint(path, "renderer package file");
            string relative = NormalizeRelativePath(
                Path.GetRelativePath(packageRoot, path));
            if (relative == ProductConstants.PackageManifestName)
                continue;
            if (!IsAllowedPackageFile(relative))
                throw new InstallerException(
                    $"The renderer package contained an unexpected file '{relative}'.");
            FileInfo info = new(path);
            files.Add(new PackageFile(relative, info.Length, ComputeSha256(path)));
        }
        return files.OrderBy(file => file.RelativePath,
            StringComparer.Ordinal).ToArray();
    }

    private static string NormalizeRelativePath(string path)
    {
        if (string.IsNullOrWhiteSpace(path) || Path.IsPathRooted(path) ||
            path.Contains(':'))
            throw new InstallerException(
                "A renderer package relative path was invalid.");
        string normalized = path.Replace(Path.DirectorySeparatorChar, '/');
        if (normalized.Split('/').Any(part => part is "" or "." or ".."))
            throw new InstallerException(
                "A renderer package relative path was invalid.");
        string contractRoot = Path.Combine(Path.GetTempPath(),
            "uvsr-package-root-contract");
        string full = Path.GetFullPath(Path.Combine(contractRoot,
            normalized.Replace('/', Path.DirectorySeparatorChar)));
        string root = Path.GetFullPath(contractRoot);
        if (!SafePaths.IsStrictDescendant(full, root))
            throw new InstallerException(
                "A renderer package path escaped its root.");
        return normalized;
    }

    private static void ValidateEngineMetadata(
        string path,
        PackageManifest manifest)
    {
        FileVersionInfo info = FileVersionInfo.GetVersionInfo(path);
        if (!string.Equals(info.ProductName, "UVSR Engine",
                StringComparison.Ordinal) ||
            !string.Equals(info.FileVersion, manifest.EngineVersion,
                StringComparison.Ordinal) ||
            !string.Equals(info.ProductVersion,
                $"{manifest.EngineVersion}+{manifest.SettingsHash}",
                StringComparison.Ordinal))
            throw new InstallerException(
                "The renderer executable metadata did not match the package settings identity.");
    }

    private static void ValidateSettingsContract(
        string packageRoot,
        PackageManifest manifest)
    {
        string path = Path.Combine(packageRoot,
            ProductConstants.SettingsContractRelativePath.Replace('/',
                Path.DirectorySeparatorChar));
        SafePaths.RejectReparsePathChain(path, "canonical settings contract");
        FileInfo info = new(path);
        if (!info.Exists || info.Length is <= 0 or > 16L * 1024 * 1024)
            throw new InstallerException(
                "The canonical settings contract was missing or invalid.");
        try
        {
            byte[] data = File.ReadAllBytes(path);
            LauncherUpdateFeedVerifier.RejectDuplicateJsonProperties(data);
            using JsonDocument document = JsonDocument.Parse(data);
            JsonElement root = document.RootElement;
            RequireExactProperties(root,
                "schemaVersion", "settingsHash", "engineVersion",
                "serializationPolicy", "entries");
            if (root.GetProperty("schemaVersion").GetInt32() !=
                    ProductConstants.SettingsContractSchemaVersion ||
                !string.Equals(root.GetProperty("settingsHash").GetString(),
                    manifest.SettingsHash, StringComparison.Ordinal) ||
                !string.Equals(root.GetProperty("engineVersion").GetString(),
                    manifest.EngineVersion, StringComparison.Ordinal))
                throw new JsonException(
                    "Settings identity does not match the package manifest.");
            string? policy = root.GetProperty("serializationPolicy").GetString();
            if (string.IsNullOrWhiteSpace(policy) || policy.Length > 4096)
                throw new JsonException("Settings serialization policy was invalid.");
            JsonElement entries = root.GetProperty("entries");
            if (entries.ValueKind != JsonValueKind.Array ||
                entries.GetArrayLength() != 176)
                throw new JsonException("Settings entries were invalid.");
            string? previous = null;
            int snapshotEntries = 0;
            int sessionEntries = 0;
            foreach (JsonElement entry in entries.EnumerateArray())
            {
                RequireExactProperties(entry, "name", "kind", "persistence",
                    "snapshotMember", "defaultValue", "domain");
                string? name = entry.GetProperty("name").GetString();
                string? kind = entry.GetProperty("kind").GetString();
                string? persistence = entry.GetProperty("persistence").GetString();
                JsonElement snapshotMember = entry.GetProperty("snapshotMember");
                JsonElement defaultValue = entry.GetProperty("defaultValue");
                JsonElement domain = entry.GetProperty("domain");
                if (string.IsNullOrWhiteSpace(name) || name.Length > 256 ||
                    kind is not ("Boolean" or "Integer" or "Float" or
                        "Float3" or "Enum" or "DynamicSelection" or "Float4") ||
                    previous is not null &&
                    string.CompareOrdinal(previous, name) >= 0 ||
                    snapshotMember.ValueKind is not (JsonValueKind.True or
                        JsonValueKind.False) ||
                    defaultValue.ValueKind != JsonValueKind.String ||
                    defaultValue.GetString()!.Length > 4096 ||
                    domain.ValueKind != JsonValueKind.String ||
                    domain.GetString()!.Length > 4096)
                    throw new JsonException("Settings entry was invalid.");
                bool isSnapshotMember = snapshotMember.GetBoolean();
                if (persistence == "SnapshotCatalog" && isSnapshotMember)
                    snapshotEntries++;
                else if (persistence == "SessionOnly" && !isSnapshotMember)
                    sessionEntries++;
                else
                    throw new JsonException(
                        "Settings persistence and snapshot membership disagreed.");
                previous = name;
            }
            if (snapshotEntries != 174 || sessionEntries != 2)
                throw new JsonException(
                    "Settings snapshot membership was invalid.");
        }
        catch (Exception ex) when (ex is IOException or JsonException or
                                   InvalidOperationException or FormatException)
        {
            throw new InstallerException(
                "The canonical settings contract failed validation.", ex);
        }
    }

    private static void RequireExactProperties(
        JsonElement element,
        params string[] expected)
    {
        if (element.ValueKind != JsonValueKind.Object)
            throw new JsonException("A settings contract object was expected.");
        HashSet<string> names = element.EnumerateObject()
            .Select(property => property.Name)
            .ToHashSet(StringComparer.Ordinal);
        if (!names.SetEquals(expected))
            throw new JsonException("Settings contract properties were invalid.");
    }

    internal static string ComputeSha256(string path)
    {
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    private static bool FixedTimeHashEquals(string left, string right)
    {
        if (!ProductConstants.HashRegex().IsMatch(left) ||
            !ProductConstants.HashRegex().IsMatch(right))
            return false;
        return CryptographicOperations.FixedTimeEquals(
            Convert.FromHexString(left), Convert.FromHexString(right));
    }
}

internal static class RendererPackageContract
{
    internal static void ValidateFeed(RendererFeed feed)
    {
        if (feed is null ||
            feed.SchemaVersion != ProductConstants.RendererUpdateFeedSchemaVersion ||
            !string.Equals(feed.ProductId, ProductConstants.ProductId,
                StringComparison.Ordinal) ||
            !string.Equals(feed.Channel, "stable", StringComparison.Ordinal) ||
            feed.ReleaseSequence is < 1 or > ProductConstants.MaximumReleaseSequence ||
            string.IsNullOrWhiteSpace(feed.SourceCommit) ||
            !ProductConstants.CommitRegex().IsMatch(feed.SourceCommit) ||
            string.IsNullOrWhiteSpace(feed.SettingsHash) ||
            !ProductConstants.SettingsHashRegex().IsMatch(feed.SettingsHash) ||
            !ProductConstants.IsCanonicalEngineVersion(feed.EngineVersion) ||
            feed.Artifact is null ||
            !string.Equals(feed.Artifact.Name,
                ProductConstants.RendererArtifactName, StringComparison.Ordinal) ||
            feed.Artifact.Size is <= 0 or >
                ProductConstants.MaximumRendererPackageBytes ||
            string.IsNullOrWhiteSpace(feed.Artifact.Sha256) ||
            !ProductConstants.HashRegex().IsMatch(feed.Artifact.Sha256))
            throw new InstallerException(
                "The renderer update feed did not match the required strict contract.");
    }

    internal static void ValidateManifest(PackageManifest manifest)
    {
        if (manifest is null ||
            manifest.SchemaVersion != ProductConstants.RendererUpdateFeedSchemaVersion ||
            !string.Equals(manifest.ProductId, ProductConstants.ProductId,
                StringComparison.Ordinal) ||
            !manifest.Production ||
            !string.Equals(manifest.Configuration, "Release",
                StringComparison.Ordinal) ||
            manifest.ReleaseSequence is < 1 or >
                ProductConstants.MaximumReleaseSequence ||
            string.IsNullOrWhiteSpace(manifest.SourceCommit) ||
            !ProductConstants.CommitRegex().IsMatch(manifest.SourceCommit) ||
            string.IsNullOrWhiteSpace(manifest.SettingsHash) ||
            !ProductConstants.SettingsHashRegex().IsMatch(manifest.SettingsHash) ||
            !ProductConstants.IsCanonicalEngineVersion(manifest.EngineVersion) ||
            string.IsNullOrWhiteSpace(manifest.ExecutableSha256) ||
            !ProductConstants.HashRegex().IsMatch(manifest.ExecutableSha256) ||
            manifest.Files is null || manifest.Files.Count is 0 or > 100_000)
            throw new InstallerException(
                "The renderer package manifest did not match the required strict contract.");
    }
}
