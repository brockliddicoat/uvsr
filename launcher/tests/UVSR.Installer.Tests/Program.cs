using System.Security.Cryptography;
using System.Text.Json;
using System.IO.Compression;
using System.Net;
using UvsrInstaller;

if (args.Length != 0 && args[0] == "--verify-renderer-archive")
    return VerifyActualRendererArchive(args);
if (args.Length != 0 && args[0] == "--verify-production-services")
    return VerifyProductionServices(args);

List<(string Name, Action Body)> tests =
[
    ("engine version syntax accepts the emitted numeric identity", EngineVersion),
    ("signed launcher feed verifies", SignedLauncherFeed),
    ("signed renderer feed verifies", SignedRendererFeed),
    ("renderer feed rejects tampering", RendererFeedTamper),
    ("renderer feed rejects sequence identity reuse", RendererSequenceConflict),
    ("renderer update classifies install update repair current", RendererClassification),
    ("renderer manifest binds feed identity", ManifestBinding),
    ("signed renderer archive stages with exact inventory", RendererArchive),
    ("retained media inventory rejects missing and swapped members",
        RendererMediaInventory),
    ("real installer transaction lifecycle is isolated and reversible", InstallerLifecycle),
    ("canonical executable names are exact", CanonicalNames),
    ("launcher update temp path keeps the canonical filename", LauncherTempPath),
    ("known old launcher filename migrates once", KnownOldNameMigration),
    ("launcher rollback decisions preserve valid package", RollbackDecision),
    ("launcher startup redirects only to verified newer identity", StartupDecision),
    ("unsafe package paths are rejected", UnsafePackagePath)
];

int failures = 0;
foreach ((string name, Action body) in tests)
{
    try
    {
        body();
        Console.WriteLine($"PASS {name}");
    }
    catch (Exception ex)
    {
        failures++;
        Console.Error.WriteLine($"FAIL {name}: {ex}");
    }
}
Console.WriteLine($"{tests.Count - failures}/{tests.Count} launcher contract tests passed.");
return failures == 0 ? 0 : 1;

static int VerifyProductionServices(string[] arguments)
{
    try
    {
        if (arguments.Length != 3)
            throw new Exception(
                "Production-service verification requires engine and launcher paths.");
        string engine = Path.GetFullPath(arguments[1]);
        string launcher = Path.GetFullPath(arguments[2]);
        Assert(Path.GetFileName(engine) == ProductConstants.EngineExecutableName &&
            File.Exists(engine));
        Assert(Path.GetFileName(launcher) == ProductConstants.LauncherExecutableName &&
            File.Exists(launcher));

        string packageRoot = Directory.GetParent(
            Path.GetDirectoryName(engine)!)?.FullName ??
            throw new Exception("The exact engine package root is unavailable.");
        string manifestPath = Path.Combine(packageRoot,
            ProductConstants.PackageManifestName);
        using JsonDocument packageManifest = JsonDocument.Parse(
            File.ReadAllText(manifestPath));
        JsonElement manifestRoot = packageManifest.RootElement;
        string expectedSourceCommit =
            manifestRoot.GetProperty("sourceCommit").GetString() ??
            throw new Exception("The package manifest source identity is absent.");
        string expectedSettingsHash =
            manifestRoot.GetProperty("settingsHash").GetString() ??
            throw new Exception("The package manifest settings hash is absent.");
        string expectedEngineVersion =
            manifestRoot.GetProperty("engineVersion").GetString() ??
            throw new Exception("The package manifest engine version is absent.");
        Assert(manifestRoot.GetProperty("productId").GetString() ==
            ProductConstants.ProductId);
        Assert(manifestRoot.GetProperty("production").GetBoolean());
        Assert(manifestRoot.GetProperty("configuration").GetString() ==
            "Release");
        Assert(manifestRoot.GetProperty("executableSha256").GetString() ==
            PayloadPackager.ComputeSha256(engine));

        string currentExecutable = Environment.ProcessPath ??
            throw new Exception("The test process path is unavailable.");
        ExactProcessInspection current =
            ProcessInspector.InspectProcessesByExecutable(currentExecutable);
        Assert(current.State == TrackedProcessState.Running &&
            current.Matches.Any(match => match.ProcessId == Environment.ProcessId));
        ExactProcessIdentity captured =
            ProcessInspector.TryCaptureExactProcess(Environment.ProcessId) ??
            throw new Exception("The real test process could not be captured.");
        Assert(ProcessInspector.InspectExactProcess(captured) ==
            TrackedProcessState.Running);
        Assert(ProcessInspector.InspectExactProcess(captured with
        {
            CreationTimeUtcFileTime = captured.CreationTimeUtcFileTime + 1
        }) == TrackedProcessState.NotRunning);

        System.Diagnostics.ProcessStartInfo engineStart = new()
        {
            FileName = engine,
            WorkingDirectory = Path.GetDirectoryName(engine)!,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        engineStart.ArgumentList.Add("--identity-json");
        using (System.Diagnostics.Process engineProcess = new()
        {
            StartInfo = engineStart
        })
        {
            Assert(engineProcess.Start());
            string identityText = engineProcess.StandardOutput.ReadToEnd();
            string identityError = engineProcess.StandardError.ReadToEnd();
            if (!engineProcess.WaitForExit(30_000) || engineProcess.ExitCode != 0)
                throw new Exception(
                    $"The exact engine identity process failed: {identityError}");
            using JsonDocument identity = JsonDocument.Parse(identityText);
            JsonElement root = identity.RootElement;
            Assert(root.GetProperty("executable").GetString() ==
                ProductConstants.EngineExecutableName);
            Assert(root.GetProperty("settings_hash").GetString() ==
                expectedSettingsHash);
            Assert(root.GetProperty("engine_version").GetString() ==
                expectedEngineVersion);
            Assert(root.GetProperty("source_commit").GetString() ==
                expectedSourceCommit);
            Assert(root.GetProperty("source_identity").GetString() ==
                expectedSourceCommit);
            Assert(root.GetProperty("source_tree_clean").GetBoolean());
            Assert(root.GetProperty("production").GetBoolean());
            Assert(root.GetProperty("configuration").GetString() ==
                "Release");
        }

        using TemporaryDirectory temporary = new();
        InstallerPaths paths = InstallerPaths.Create(
            Path.Combine(temporary.Path, "local"),
            Path.Combine(temporary.Path, "desktop"),
            Path.Combine(temporary.Path, "programs"));
        Directory.CreateDirectory(paths.ProgramRoot);
        Directory.CreateDirectory(paths.DesktopDirectory);
        Directory.CreateDirectory(paths.ProgramsDirectory);
        Guid installationId = Guid.NewGuid();
        JsonStore.WriteAtomic(paths.ProgramMarker, new OwnerMarker(
            ProductConstants.SchemaVersion,
            ProductConstants.ProductId,
            installationId));
        string launcherHash = PayloadPackager.ComputeSha256(launcher);
        string installedLauncher = paths.LauncherExecutable(launcherHash);
        Directory.CreateDirectory(Path.GetDirectoryName(installedLauncher)!);
        File.Copy(launcher, installedLauncher);

        ShellIntegration shell = new(paths);
        Func<ShellIntegration.ShortcutInfo, bool> owned = shortcut =>
            shell.IsOwnedLauncherShortcut(shortcut, installationId);
        shell.CreateOrReplaceShortcut(
            paths.StartMenuShortcut,
            installedLauncher,
            string.Empty,
            Path.GetDirectoryName(installedLauncher)!,
            installedLauncher,
            "UVSR production shortcut integration test",
            owned);
        ShellIntegration.ShortcutInfo shortcut =
            ShellIntegration.ReadShortcut(paths.StartMenuShortcut);
        Assert(string.Equals(Path.GetFullPath(shortcut.Target),
            Path.GetFullPath(installedLauncher),
            StringComparison.OrdinalIgnoreCase));
        Assert(shortcut.Arguments.Length == 0 && owned(shortcut));
        shell.CreateOrReplaceShortcut(
            paths.StartMenuShortcut,
            installedLauncher,
            string.Empty,
            Path.GetDirectoryName(installedLauncher)!,
            installedLauncher,
            "UVSR production shortcut integration test",
            owned);
        Assert(owned(ShellIntegration.ReadShortcut(paths.StartMenuShortcut)));

        Console.WriteLine(
            "PASS production engine process, inspection, and COM shortcut services");
        return 0;
    }
    catch (Exception ex)
    {
        Console.Error.WriteLine(
            $"FAIL production process/shortcut services: {ex}");
        return 1;
    }
}

static void EngineVersion()
{
    Assert(ProductConstants.IsCanonicalEngineVersion(
        "40016.45297.20830.35288"));
    Assert(!ProductConstants.IsCanonicalEngineVersion(
        "52087.1234.34644.65536"));
    Assert(!ProductConstants.IsCanonicalEngineVersion(
        "040016.45297.20830.35288"));
}

static void SignedLauncherFeed()
{
    using ECDsa key = ECDsa.Create(ECCurve.NamedCurves.nistP256);
    LauncherFeed expected = LauncherFeedValue();
    byte[] signed = Sign(expected,
        ProductConstants.LauncherUpdateFeedSchemaVersion, key, "test-key");
    LauncherFeed actual = LauncherManager.ParseAndValidateFeed(signed,
        key.ExportSubjectPublicKeyInfo(), "test-key");
    Equal(expected, actual);
    Throws<InstallerException>(() =>
        LauncherManager.ParseAndValidateFeed(signed,
            key.ExportSubjectPublicKeyInfo(), "wrong-key"));
}

static void SignedRendererFeed()
{
    using ECDsa key = ECDsa.Create(ECCurve.NamedCurves.nistP256);
    RendererFeed expected = RendererFeedValue();
    byte[] signed = Sign(expected,
        ProductConstants.RendererUpdateFeedSchemaVersion, key, "test-key");
    RendererFeed actual = LauncherManager.ParseAndValidateRendererFeed(signed,
        key.ExportSubjectPublicKeyInfo(), "test-key");
    Equal(expected, actual);
    Equal(ProductConstants.RendererArtifactName,
        LauncherManager.BuildRendererArtifactUri(actual).Segments[^1]);
}

static void RendererFeedTamper()
{
    using ECDsa key = ECDsa.Create(ECCurve.NamedCurves.nistP256);
    byte[] signed = Sign(RendererFeedValue(),
        ProductConstants.RendererUpdateFeedSchemaVersion, key, "test-key");
    using JsonDocument document = JsonDocument.Parse(signed);
    SignedFeedEnvelope envelope =
        JsonSerializer.Deserialize<SignedFeedEnvelope>(signed, JsonStore.Options)!;
    byte[] payload = Convert.FromBase64String(envelope.PayloadBase64);
    payload[^2] ^= 1;
    byte[] tampered = JsonSerializer.SerializeToUtf8Bytes(envelope with
    {
        PayloadBase64 = Convert.ToBase64String(payload)
    }, JsonStore.Options);
    Throws<InstallerException>(() =>
        LauncherManager.ParseAndValidateRendererFeed(tampered,
            key.ExportSubjectPublicKeyInfo(), "test-key"));

    string duplicate =
        """{"schemaVersion":1,"schemaVersion":1,"keyId":"x","payloadBase64":"eA==","signatureBase64":"eA=="}""";
    Throws<InstallerException>(() =>
        LauncherUpdateFeedVerifier.VerifyRendererAndParse(
            System.Text.Encoding.UTF8.GetBytes(duplicate),
            key.ExportSubjectPublicKeyInfo(), "x"));
}

static void RendererSequenceConflict()
{
    RendererFeed feed = RendererFeedValue();
    InstallState state = InstalledState(feed) with
    {
        ArtifactSha256 = new string('b', 64)
    };
    InstallSnapshot snapshot = Snapshot(state);
    Throws<InstallerException>(() =>
        LauncherManager.ClassifyRendererUpdate(snapshot, feed));
}

static void RendererClassification()
{
    RendererFeed feed = RendererFeedValue();
    InstallSnapshot absent = new(false, false, null, null, null, "none");
    Equal(ComponentUpdateState.NotInstalled,
        LauncherManager.ClassifyRendererUpdate(absent, feed));
    InstallState current = InstalledState(feed);
    Equal(ComponentUpdateState.Current,
        LauncherManager.ClassifyRendererUpdate(Snapshot(current), feed));
    Equal(ComponentUpdateState.UpdateAvailable,
        LauncherManager.ClassifyRendererUpdate(Snapshot(current with
        {
            ReleaseSequence = feed.ReleaseSequence - 1
        }), feed));
    Equal(ComponentUpdateState.Current,
        LauncherManager.ClassifyRendererUpdate(Snapshot(current with
        {
            ReleaseSequence = feed.ReleaseSequence + 1
        }), feed));
    Equal(ComponentUpdateState.RepairNeeded,
        LauncherManager.ClassifyRendererUpdate(
            Snapshot(current) with { IsDamaged = true }, feed));
}

static void ManifestBinding()
{
    RendererFeed feed = RendererFeedValue();
    PackageManifest manifest = new(
        ProductConstants.RendererUpdateFeedSchemaVersion,
        ProductConstants.ProductId,
        true,
        "Release",
        feed.ReleaseSequence,
        feed.SourceCommit,
        feed.SettingsHash,
        feed.EngineVersion,
        new string('c', 64),
        [new PackageFile($"bin/{ProductConstants.EngineExecutableName}",
            1, new string('c', 64))]);
    PayloadPackager.ValidateFeedBinding(manifest, feed);
    Throws<InstallerException>(() =>
        PayloadPackager.ValidateFeedBinding(manifest with
        {
            Production = false
        }, feed));
    Throws<InstallerException>(() =>
        PayloadPackager.ValidateFeedBinding(manifest with
        {
            Configuration = "Debug"
        }, feed));
    Throws<InstallerException>(() =>
        PayloadPackager.ValidateFeedBinding(manifest with
        {
            SettingsHash = "100102030405060708090a0b0c0d0e0f"
        }, feed));
}

static void RendererArchive()
{
    using TemporaryDirectory temporary = new();
    RendererFixture fixture = CreateRendererFixture(
        Path.Combine(temporary.Path, "fixture"), 16, 'a', 2);
    string package = fixture.PackageRoot;
    string local = Path.Combine(temporary.Path, "local");
    string desktop = Path.Combine(temporary.Path, "desktop");
    string programs = Path.Combine(temporary.Path, "programs");
    foreach (string directory in new[] { local, desktop, programs })
        Directory.CreateDirectory(directory);
    InstallerPaths paths = InstallerPaths.Create(local, desktop, programs);
    PayloadPackager packager = new(paths);
    PackageManifest staged = packager.StageArchive(Guid.NewGuid(),
        $"{fixture.Feed.SourceCommit}-20260823000000-12345678",
        fixture.Archive, fixture.Feed,
        new InstallLog(paths.LogsDirectory, null));
    Equal(fixture.Manifest.EngineVersion, staged.EngineVersion);
    Equal(fixture.Manifest.ExecutableSha256, staged.ExecutableSha256);

    foreach (string extension in new[] { ".txt", ".json", ".dll", ".exe" })
    {
        string stale = Path.Combine(package, "bin", "shaders",
            "stale" + extension);
        File.WriteAllText(stale, "stale");
        File.Delete(fixture.Archive);
        ZipFile.CreateFromDirectory(package, fixture.Archive,
            CompressionLevel.Fastest,
            includeBaseDirectory: false);
        RendererFeed staleFeed = fixture.Feed with
        {
            Artifact = new RendererFeedArtifact(ProductConstants.RendererArtifactName,
                new FileInfo(fixture.Archive).Length,
                PayloadPackager.ComputeSha256(fixture.Archive))
        };
        Throws<InstallerException>(() => packager.StageArchive(Guid.NewGuid(),
            $"{fixture.Feed.SourceCommit}-20260823000000-{Guid.NewGuid():N}"[..64],
            fixture.Archive, staleFeed,
            new InstallLog(paths.LogsDirectory, null)));
        File.Delete(stale);
    }
}

static void RendererMediaInventory()
{
    Assert(PayloadPackager.RuntimeMediaPaths.Count == 305);
    Assert(PayloadPackager.RequiredMediaNoticePaths.Count == 5);
    Assert(PayloadPackager.ProtectedMediaAndNoticePaths.Count == 310);
    Assert(PayloadPackager.RuntimeMediaPaths.Count(path =>
        path.StartsWith("media/environments/", StringComparison.Ordinal)) == 6);
    Assert(PayloadPackager.RuntimeMediaPaths.Count(path =>
        path.StartsWith("media/uvsr/noise/", StringComparison.Ordinal)) == 13);
    Assert(PayloadPackager.RuntimeMediaPaths.Count(path => path.StartsWith(
        "media/glTF-Sample-Assets/Models/bistro_interior_retextured/",
        StringComparison.Ordinal)) == 7);
    Assert(PayloadPackager.RuntimeMediaPaths.Count(path => path.StartsWith(
        "media/glTF-Sample-Assets/Models/san_miguel_retextured/",
        StringComparison.Ordinal)) == 276);
    Assert(PayloadPackager.RuntimeMediaPaths.Count(path =>
        path.StartsWith("media/fonts/NotoSans/",
            StringComparison.Ordinal)) == 3);

    string[] requiredClassMembers =
    [
        PayloadPackager.RuntimeMediaPaths.OrderBy(path => path,
            StringComparer.Ordinal).First(path => path.StartsWith(
                "media/environments/", StringComparison.Ordinal)),
        PayloadPackager.RuntimeMediaPaths.First(path => path.StartsWith(
            "media/uvsr/noise/", StringComparison.Ordinal) &&
            path.EndsWith("/manifest.json", StringComparison.Ordinal)),
        PayloadPackager.RuntimeMediaPaths.First(path => path.StartsWith(
            "media/glTF-Sample-Assets/Models/bistro_interior_retextured/",
            StringComparison.Ordinal) && path.EndsWith(".scene.json",
            StringComparison.Ordinal)),
        PayloadPackager.RuntimeMediaPaths.First(path => path.StartsWith(
            "media/glTF-Sample-Assets/Models/san_miguel_retextured/",
            StringComparison.Ordinal) && path.EndsWith(".scene.json",
            StringComparison.Ordinal)),
        PayloadPackager.RuntimeMediaPaths.OrderBy(path => path,
            StringComparer.Ordinal).First(path => path.StartsWith(
            "media/fonts/NotoSans/", StringComparison.Ordinal))
    ];
    foreach (string missing in requiredClassMembers.Concat(
                 PayloadPackager.RequiredMediaNoticePaths.OrderBy(path => path,
                     StringComparer.Ordinal)))
    {
        using TemporaryDirectory temporary = new();
        RendererFixture fixture = CreateRendererFixture(
            Path.Combine(temporary.Path, "missing"), 16, 'a', 2);
        File.Delete(PackagePath(fixture.PackageRoot, missing));
        AssertRendererFixtureRejected(RewriteRendererFixture(fixture),
            Path.Combine(temporary.Path, "runtime"));
    }

    string[] unlistedClassMembers =
    [
        "media/environments/unlisted/unlisted.hdr",
        "media/uvsr/noise/unlisted.bin",
        "media/glTF-Sample-Assets/Models/bistro_interior_retextured/" +
            "components/textures/unlisted.png",
        "media/glTF-Sample-Assets/Models/san_miguel_retextured/" +
            "components/textures/unlisted.png",
        "media/fonts/NotoSans/NotoSans-Unlisted.ttf"
    ];
    foreach (string extra in unlistedClassMembers)
    {
        Assert(!PayloadPackager.RuntimeMediaPaths.Contains(extra));
        using TemporaryDirectory temporary = new();
        RendererFixture fixture = CreateRendererFixture(
            Path.Combine(temporary.Path, "extra"), 16, 'a', 2);
        string extraPath = PackagePath(fixture.PackageRoot, extra);
        Directory.CreateDirectory(Path.GetDirectoryName(extraPath)!);
        File.WriteAllBytes(extraPath, [2]);
        AssertRendererFixtureRejected(RewriteRendererFixture(fixture),
            Path.Combine(temporary.Path, "runtime"));
    }

    using (TemporaryDirectory temporary = new())
    {
        RendererFixture fixture = CreateRendererFixture(
            Path.Combine(temporary.Path, "swapped"), 16, 'a', 2);
        string original = requiredClassMembers[2];
        string swapped = original.Replace(
            "/bistro_interior_retextured/",
            "/san_miguel_retextured/", StringComparison.Ordinal);
        Assert(!PayloadPackager.RuntimeMediaPaths.Contains(swapped));
        string destination = PackagePath(fixture.PackageRoot, swapped);
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        File.Move(PackagePath(fixture.PackageRoot, original), destination);
        AssertRendererFixtureRejected(RewriteRendererFixture(fixture),
            Path.Combine(temporary.Path, "runtime"));
    }
}

static RendererFixture CreateRendererFixture(
    string root,
    long releaseSequence,
    char commitDigit,
    byte contentByte)
{
    string package = Path.Combine(root, "package");
    foreach (string directory in new[]
    {
        Path.Combine(package, "bin", "D3D12"),
        Path.Combine(package, "bin", "shaders"),
        Path.Combine(package, "bin", "licenses"),
        Path.Combine(package, "bin", "settings")
    })
        Directory.CreateDirectory(directory);

    string engine = Path.Combine(package, "bin",
        ProductConstants.EngineExecutableName);
    File.Copy(Environment.ProcessPath!, engine);
    File.WriteAllBytes(Path.Combine(package, "bin", "D3D12",
        "D3D12Core.dll"), [1]);
    foreach (string relativePath in PayloadPackager.RuntimeShaderPaths)
    {
        string shader = Path.Combine(package,
            relativePath.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(shader)!);
        File.WriteAllBytes(shader, [contentByte]);
    }
    foreach (string relativePath in
             PayloadPackager.ProtectedMediaAndNoticePaths)
    {
        string asset = PackagePath(package, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(asset)!);
        File.WriteAllBytes(asset, [contentByte]);
    }

    RendererFeed identity = RendererFeedValue() with
    {
        ReleaseSequence = releaseSequence,
        SourceCommit = new string(commitDigit, 40)
    };
    File.WriteAllText(Path.Combine(package,
        ProductConstants.SettingsContractRelativePath.Replace('/',
            Path.DirectorySeparatorChar)), JsonSerializer.Serialize(new
        {
            schemaVersion = ProductConstants.SettingsContractSchemaVersion,
            settingsHash = identity.SettingsHash,
            engineVersion = identity.EngineVersion,
            serializationPolicy = "command-name-sorted-v1",
            entries = Enumerable.Range(0, 176).Select(index => new
            {
                name = $"example.setting.{index:D3}",
                kind = "Boolean",
                persistence = index < 174 ? "SnapshotCatalog" : "SessionOnly",
                snapshotMember = index < 174,
                defaultValue = "false",
                domain = "false|true"
            })
        }, JsonStore.Options));
    PackageFile[] files = Directory.EnumerateFiles(package, "*",
            SearchOption.AllDirectories)
        .Select(path => new PackageFile(
            Path.GetRelativePath(package, path).Replace('\\', '/'),
            new FileInfo(path).Length,
            PayloadPackager.ComputeSha256(path)))
        .OrderBy(file => file.RelativePath, StringComparer.Ordinal)
        .ToArray();
    string engineHash = files.Single(file =>
        file.RelativePath == $"bin/{ProductConstants.EngineExecutableName}").Sha256;
    PackageManifest manifest = new(1, ProductConstants.ProductId, true, "Release",
        identity.ReleaseSequence, identity.SourceCommit, identity.SettingsHash,
        identity.EngineVersion, engineHash, files);
    JsonStore.WriteAtomic(Path.Combine(package,
        ProductConstants.PackageManifestName), manifest);
    string archive = Path.Combine(root, ProductConstants.RendererArtifactName);
    ZipFile.CreateFromDirectory(package, archive, CompressionLevel.Fastest,
        includeBaseDirectory: false);
    RendererFeed feed = identity with
    {
        Artifact = new RendererFeedArtifact(ProductConstants.RendererArtifactName,
            new FileInfo(archive).Length, PayloadPackager.ComputeSha256(archive))
    };
    return new RendererFixture(package, archive, feed, manifest);
}

static RendererFixture RewriteRendererFixture(RendererFixture fixture)
{
    PackageFile[] files = Directory.EnumerateFiles(fixture.PackageRoot, "*",
            SearchOption.AllDirectories)
        .Select(path => new PackageFile(
            Path.GetRelativePath(fixture.PackageRoot, path).Replace('\\', '/'),
            new FileInfo(path).Length,
            PayloadPackager.ComputeSha256(path)))
        .Where(file => file.RelativePath !=
            ProductConstants.PackageManifestName)
        .OrderBy(file => file.RelativePath, StringComparer.Ordinal)
        .ToArray();
    string engineHash = files.Single(file => file.RelativePath ==
        $"bin/{ProductConstants.EngineExecutableName}").Sha256;
    PackageManifest manifest = fixture.Manifest with
    {
        ExecutableSha256 = engineHash,
        Files = files
    };
    JsonStore.WriteAtomic(Path.Combine(fixture.PackageRoot,
        ProductConstants.PackageManifestName), manifest);
    if (File.Exists(fixture.Archive))
        File.Delete(fixture.Archive);
    ZipFile.CreateFromDirectory(fixture.PackageRoot, fixture.Archive,
        CompressionLevel.Fastest, includeBaseDirectory: false);
    RendererFeed feed = fixture.Feed with
    {
        Artifact = new RendererFeedArtifact(ProductConstants.RendererArtifactName,
            new FileInfo(fixture.Archive).Length,
            PayloadPackager.ComputeSha256(fixture.Archive))
    };
    return fixture with { Feed = feed, Manifest = manifest };
}

static void AssertRendererFixtureRejected(
    RendererFixture fixture,
    string root)
{
    string local = Path.Combine(root, "local");
    string desktop = Path.Combine(root, "desktop");
    string programs = Path.Combine(root, "programs");
    foreach (string directory in new[] { local, desktop, programs })
        Directory.CreateDirectory(directory);
    InstallerPaths paths = InstallerPaths.Create(local, desktop, programs);
    PayloadPackager packager = new(paths);
    Throws<InstallerException>(() => packager.StageArchive(Guid.NewGuid(),
        $"{fixture.Feed.SourceCommit}-20260823000000-12345678",
        fixture.Archive, fixture.Feed,
        new InstallLog(paths.LogsDirectory, null)));
}

static string PackagePath(string root, string relativePath) => Path.Combine(
    root, relativePath.Replace('/', Path.DirectorySeparatorChar));

static void InstallerLifecycle()
{
    using TemporaryDirectory temporary = new();
    string local = Path.Combine(temporary.Path, "local");
    string desktop = Path.Combine(temporary.Path, "desktop");
    string programs = Path.Combine(temporary.Path, "programs");
    Directory.CreateDirectory(local);
    Directory.CreateDirectory(desktop);
    Directory.CreateDirectory(programs);
    InstallerPaths paths = InstallerPaths.Create(local, desktop, programs);
    OwnerMarker owner = PrepareInstalledLauncher(paths, migrateOldName: true);
    TestInstallerShell shell = new(paths);
    TestInstallerEnvironment environment = new(paths);
    using ECDsa key = ECDsa.Create(ECCurve.NamedCurves.nistP256);
    TestRendererHandler handler = new();
    DownloadManager downloads = new(handler, new DownloadPolicy(
        1, TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(5),
        TimeSpan.FromSeconds(30), Array.Empty<TimeSpan>()));
    using InstallerEngine engine = new(paths, shell, downloads,
        key.ExportSubjectPublicKeyInfo(), "test-key", environment.Services);

    RendererFixture initial = CreateRendererFixture(
        Path.Combine(temporary.Path, "renderer-16"), 16, 'a', 2);
    handler.Set(initial.Feed,
        Sign(initial.Feed, ProductConstants.RendererUpdateFeedSchemaVersion,
            key, "test-key"),
        File.ReadAllBytes(initial.Archive));
    OperationResult installed = engine.ExecuteAsync(
        InstallerOperation.Install, desktopShortcut: true,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult();
    Assert(installed.State is not null &&
        installed.State.ReleaseSequence == 16 &&
        File.Exists(installed.ExecutablePath));
    Assert(File.Exists(paths.LauncherExecutable(
        JsonStore.Read<LauncherState>(paths.LauncherStateFile).
            ExecutableSha256)));
    Assert(!File.Exists(paths.MigrationLauncherExecutable(
        JsonStore.Read<LauncherState>(paths.LauncherStateFile).
            ExecutableSha256)));
    Assert(File.Exists(paths.DesktopShortcut) &&
        File.Exists(paths.StartMenuShortcut));
    Assert(!File.Exists(paths.TransactionFile));
    Equal(16L, engine.Inspect().State!.ReleaseSequence);

    RendererFixture update = CreateRendererFixture(
        Path.Combine(temporary.Path, "renderer-17"), 17, 'b', 3);
    handler.Set(update.Feed,
        Sign(update.Feed, ProductConstants.RendererUpdateFeedSchemaVersion,
            key, "test-key"),
        File.ReadAllBytes(update.Archive));
    string previousVersion = installed.State!.ActiveVersionId;
    OperationResult updated = engine.ExecuteAsync(
        InstallerOperation.Update, desktopShortcut: true,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult();
    Assert(updated.State is not null && updated.State.ReleaseSequence == 17);
    Assert(!Directory.Exists(paths.VersionRoot(previousVersion)));

    OperationResult shortcutUpdate = engine.ExecuteAsync(
        InstallerOperation.Update, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult();
    Assert(shortcutUpdate.State is not null &&
        !shortcutUpdate.State.DesktopShortcut &&
        !File.Exists(paths.DesktopShortcut) &&
        File.Exists(paths.StartMenuShortcut));

    InstallState shortcutState = shortcutUpdate.State ??
        throw new Exception("Shortcut update omitted installed state.");
    string damagedVersion = shortcutState.ActiveVersionId;
    File.WriteAllText(paths.VersionExecutable(damagedVersion), "damaged");
    Assert(engine.Inspect().IsDamaged);
    OperationResult repaired = engine.ExecuteAsync(
        InstallerOperation.Reinstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult();
    Assert(repaired.State is not null && repaired.State.ReleaseSequence == 17 &&
        repaired.State.ActiveVersionId != damagedVersion &&
        !engine.Inspect().IsDamaged);
    Assert(!Directory.Exists(paths.VersionRoot(damagedVersion)));

    RendererFixture rejected = CreateRendererFixture(
        Path.Combine(temporary.Path, "renderer-18"), 18, 'c', 4);
    handler.Set(rejected.Feed,
        Sign(rejected.Feed, ProductConstants.RendererUpdateFeedSchemaVersion,
            key, "test-key"),
        File.ReadAllBytes(rejected.Archive));
    InstallState rollbackState = repaired.State ??
        throw new Exception("Repair omitted installed state.");
    shell.FailNextApply = true;
    ExpectInstallerFailure(() => engine.ExecuteAsync(
        InstallerOperation.Update, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult());
    InstallSnapshot afterRollback = engine.Inspect();
    Equal(rollbackState, afterRollback.State!);
    Assert(!File.Exists(paths.TransactionFile));
    Assert(Directory.EnumerateDirectories(paths.VersionsDirectory).Count() == 1);
    engine.LaunchInstalled();
    Equal(1, shell.LaunchCount);

    byte[] programMarker = File.ReadAllBytes(paths.ProgramMarker);
    JsonStore.WriteAtomic(paths.ProgramMarker, owner with
    {
        InstallationId = Guid.NewGuid()
    });
    ExpectInstallerFailure(() => engine.ExecuteAsync(
        InstallerOperation.Uninstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult());
    JsonStore.WriteAtomicBytes(paths.ProgramMarker, programMarker);

    environment.RendererProcessState = TrackedProcessState.Running;
    ExpectInstallerFailure(() => engine.ExecuteAsync(
        InstallerOperation.Uninstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult());
    environment.RendererProcessState = TrackedProcessState.NotRunning;
    environment.LauncherProcessState = TrackedProcessState.Running;
    ExpectInstallerFailure(() => engine.ExecuteAsync(
        InstallerOperation.Uninstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult());
    environment.LauncherProcessState = TrackedProcessState.NotRunning;
    OperationResult uninstall = engine.ExecuteAsync(
        InstallerOperation.Uninstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult();
    Assert(uninstall.CleanupScheduled && environment.UninstallScheduled &&
        File.Exists(paths.TransactionFile) &&
        File.Exists(paths.UninstallRecordFile));
}

static OwnerMarker PrepareInstalledLauncher(
    InstallerPaths paths,
    bool migrateOldName)
{
    OwnerMarker owner = new OwnershipManager(paths).EnsureRoots();
    string source = Path.Combine(AppContext.BaseDirectory,
        ProductConstants.LauncherExecutableName);
    if (!File.Exists(source))
        throw new Exception("The test launcher apphost is missing.");
    string hash = PayloadPackager.ComputeSha256(source);
    string root = paths.LauncherVersionRoot(hash);
    Directory.CreateDirectory(root);
    string destination = migrateOldName
        ? paths.MigrationLauncherExecutable(hash)
        : paths.LauncherExecutable(hash);
    File.Copy(source, destination);
    DateTimeOffset installedUtc = DateTimeOffset.UnixEpoch;
    JsonStore.WriteAtomic(paths.LauncherPackageMarker(hash),
        new LauncherPackageManifest(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, owner.InstallationId,
            ProductConstants.LauncherReleaseSequence,
            ProductConstants.LauncherVersion, hash,
            new FileInfo(source).Length, installedUtc));
    JsonStore.WriteAtomic(paths.LauncherStateFile,
        new LauncherState(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, owner.InstallationId,
            ProductConstants.LauncherReleaseSequence,
            ProductConstants.LauncherVersion, hash,
            DesktopShortcut: true, installedUtc));
    return owner;
}

static void ExpectInstallerFailure(Action action)
{
    try { action(); }
    catch (InstallerException) { return; }
    throw new Exception("Expected InstallerException.");
}

static void CanonicalNames()
{
    Equal("uvsr-launcher.exe", ProductConstants.LauncherExecutableName);
    Equal("uvsr-engine.exe", ProductConstants.EngineExecutableName);
    Equal("uvsr-launcher.exe", ProductConstants.LauncherArtifactName);
    Assert(!ProductConstants.LauncherExecutableName.Any(char.IsUpper));
    Assert(!ProductConstants.EngineExecutableName.Any(char.IsUpper));
}

static void LauncherTempPath()
{
    using TemporaryDirectory temporary = new();
    string local = Path.Combine(temporary.Path, "local");
    string desktop = Path.Combine(temporary.Path, "desktop");
    string programs = Path.Combine(temporary.Path, "programs");
    Directory.CreateDirectory(local);
    Directory.CreateDirectory(desktop);
    Directory.CreateDirectory(programs);
    InstallerPaths paths = InstallerPaths.Create(local, desktop, programs);
    string path = LauncherManager.GetLauncherDownloadPath(paths,
        LauncherFeedValue());
    Equal(ProductConstants.LauncherExecutableName, Path.GetFileName(path));
    Assert(SafePaths.IsStrictDescendant(path, paths.DownloadsDirectory));
    Assert(Path.GetFileName(Path.GetDirectoryName(path)!).StartsWith(
        "launcher-update-", StringComparison.Ordinal));
}

static void KnownOldNameMigration()
{
    using TemporaryDirectory temporary = new();
    string local = Path.Combine(temporary.Path, "local");
    string desktop = Path.Combine(temporary.Path, "desktop");
    string programs = Path.Combine(temporary.Path, "programs");
    Directory.CreateDirectory(local);
    Directory.CreateDirectory(desktop);
    Directory.CreateDirectory(programs);
    InstallerPaths paths = InstallerPaths.Create(local, desktop, programs);
    Guid installationId = Guid.NewGuid();
    byte[] contents = [1, 2, 3, 4];
    string hash = Convert.ToHexString(SHA256.HashData(contents)).ToLowerInvariant();
    string root = paths.LauncherVersionRoot(hash);
    Directory.CreateDirectory(root);
    File.WriteAllBytes(paths.MigrationLauncherExecutable(hash), contents);
    JsonStore.WriteAtomic(paths.LauncherPackageMarker(hash),
        new LauncherPackageManifest(ProductConstants.LauncherSchemaVersion,
            ProductConstants.ProductId, installationId, 1, "1.0.0",
            hash, contents.Length, DateTimeOffset.UnixEpoch));
    using DownloadManager downloads = new();
    LauncherManager manager = new(paths, new ProcessRunner(), downloads);
    _ = manager.InspectActivation(installationId, true);
    Assert(File.Exists(paths.LauncherExecutable(hash)));
    Assert(!File.Exists(paths.MigrationLauncherExecutable(hash)));
}

static void RollbackDecision()
{
    Equal(LauncherRecoveryAction.RollBack,
        LauncherManager.DecideLauncherRecovery("prepared",
            LauncherRecoveryPackageStatus.Valid,
            LauncherRecoveryPackageStatus.Valid));
    Equal(LauncherRecoveryAction.RollForward,
        LauncherManager.DecideLauncherRecovery("state-activated",
            LauncherRecoveryPackageStatus.Valid,
            LauncherRecoveryPackageStatus.Valid));
    Equal(LauncherRecoveryAction.RetryLater,
        LauncherManager.DecideLauncherRecovery("state-activated",
            LauncherRecoveryPackageStatus.Unverifiable,
            LauncherRecoveryPackageStatus.Invalid));
}

static void StartupDecision()
{
    Equal(LauncherStartupAction.RedirectToVerifiedInstalled,
        LauncherManager.DecideLauncherStartup(16, @"C:\a\uvsr-launcher.exe",
            17, @"C:\b\uvsr-launcher.exe", true));
    Equal(LauncherStartupAction.ContinueCurrent,
        LauncherManager.DecideLauncherStartup(16, @"C:\a\uvsr-launcher.exe",
            17, @"C:\b\uvsr-launcher.exe", false));
}

static void UnsafePackagePath()
{
    Throws<InstallerException>(() => SafePaths.CombineDescendant(
        @"C:\safe", @"..\escape"));
}

static RendererFeed RendererFeedValue()
{
    const string settings = "9c50b0f1515e89d856c8ebb627b86984";
    return new RendererFeed(
        ProductConstants.RendererUpdateFeedSchemaVersion,
        ProductConstants.ProductId,
        "stable",
        16,
        new string('a', 40),
        settings,
        "40016.45297.20830.35288",
        new RendererFeedArtifact(ProductConstants.RendererArtifactName,
            1234, new string('a', 64)));
}

static int VerifyActualRendererArchive(string[] arguments)
{
    if (arguments.Length != 6 ||
        arguments[0] != "--verify-renderer-archive" ||
        !long.TryParse(arguments[5], out long releaseSequence))
    {
        Console.Error.WriteLine("usage: --verify-renderer-archive " +
            "<uvsr-renderer-windows-11-x64.zip> <source-commit> " +
            "<settings-hash> <engine-version> <release-sequence>");
        return 2;
    }
    try
    {
        string archive = Path.GetFullPath(arguments[1]);
        if (!File.Exists(archive) ||
            Path.GetFileName(archive) != ProductConstants.RendererArtifactName)
            throw new Exception("The exact canonical renderer archive was not supplied.");
        RendererFeed unsignedFeed = new(
            ProductConstants.RendererUpdateFeedSchemaVersion,
            ProductConstants.ProductId,
            "stable",
            releaseSequence,
            arguments[2],
            arguments[3],
            arguments[4],
            new RendererFeedArtifact(
                ProductConstants.RendererArtifactName,
                new FileInfo(archive).Length,
                PayloadPackager.ComputeSha256(archive)));
        using ECDsa key = ECDsa.Create(ECCurve.NamedCurves.nistP256);
        byte[] envelope = Sign(unsignedFeed,
            ProductConstants.RendererUpdateFeedSchemaVersion, key, "test-key");
        RendererFeed verifiedFeed = LauncherManager.ParseAndValidateRendererFeed(
            envelope, key.ExportSubjectPublicKeyInfo(), "test-key");

        using TemporaryDirectory temporary = new();
        string local = Path.Combine(temporary.Path, "local");
        string desktop = Path.Combine(temporary.Path, "desktop");
        string programs = Path.Combine(temporary.Path, "programs");
        Directory.CreateDirectory(local);
        Directory.CreateDirectory(desktop);
        Directory.CreateDirectory(programs);
        InstallerPaths paths = InstallerPaths.Create(local, desktop, programs);
        PackageManifest manifest = ExerciseExactArchiveLifecycle(
            paths, archive, verifiedFeed, envelope, key);
        Equal(verifiedFeed.SourceCommit, manifest.SourceCommit);
        Equal(verifiedFeed.SettingsHash, manifest.SettingsHash);
        Equal(verifiedFeed.EngineVersion, manifest.EngineVersion);
        Equal(verifiedFeed.ReleaseSequence, manifest.ReleaseSequence);
        Console.WriteLine("PASS exact CMake renderer archive completed the real " +
            "signed install/repair/rollback/uninstall lifecycle at sequence " +
            releaseSequence);
        return 0;
    }
    catch (Exception ex)
    {
        Console.Error.WriteLine("FAIL exact renderer archive round-trip: " + ex);
        return 1;
    }
}

static PackageManifest ExerciseExactArchiveLifecycle(
    InstallerPaths paths,
    string archive,
    RendererFeed feed,
    byte[] envelope,
    ECDsa key)
{
    OwnerMarker owner = PrepareInstalledLauncher(paths, migrateOldName: true);
    TestInstallerShell shell = new(paths);
    TestInstallerEnvironment environment = new(paths);
    TestRendererHandler handler = new();
    handler.Set(feed, envelope, File.ReadAllBytes(archive));
    DownloadManager downloads = new(handler, new DownloadPolicy(
        1, TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(5),
        TimeSpan.FromSeconds(30), Array.Empty<TimeSpan>()));
    using InstallerEngine engine = new(paths, shell, downloads,
        key.ExportSubjectPublicKeyInfo(), "test-key", environment.Services);

    OperationResult installed = engine.ExecuteAsync(
        InstallerOperation.Install, desktopShortcut: true,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult();
    InstallState installedState = installed.State ??
        throw new Exception("Exact package install omitted state.");
    string installedRoot = paths.VersionRoot(installedState.ActiveVersionId);
    PackageManifest manifest = PayloadPackager.ReadManifest(installedRoot);
    Assert(PayloadPackager.ProtectedMediaAndNoticePaths.All(path =>
        File.Exists(PackagePath(installedRoot, path))));
    Assert(File.Exists(paths.DesktopShortcut) &&
        File.Exists(paths.StartMenuShortcut));

    OperationResult shortcutUpdate = engine.ExecuteAsync(
        InstallerOperation.Update, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult();
    InstallState shortcutState = shortcutUpdate.State ??
        throw new Exception("Exact package shortcut update omitted state.");
    Assert(!shortcutState.DesktopShortcut &&
        !File.Exists(paths.DesktopShortcut));

    string damagedVersion = shortcutState.ActiveVersionId;
    File.WriteAllText(paths.VersionExecutable(damagedVersion), "damaged");
    OperationResult repaired = engine.ExecuteAsync(
        InstallerOperation.Reinstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult();
    InstallState repairedState = repaired.State ??
        throw new Exception("Exact package repair omitted state.");
    Assert(repairedState.ActiveVersionId != damagedVersion &&
        !engine.Inspect().IsDamaged);

    shell.FailNextApply = true;
    ExpectInstallerFailure(() => engine.ExecuteAsync(
        InstallerOperation.Reinstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult());
    Equal(repairedState, engine.Inspect().State!);
    Assert(!File.Exists(paths.TransactionFile) &&
        Directory.EnumerateDirectories(paths.VersionsDirectory).Count() == 1);
    engine.LaunchInstalled();

    byte[] marker = File.ReadAllBytes(paths.ProgramMarker);
    JsonStore.WriteAtomic(paths.ProgramMarker, owner with
    {
        InstallationId = Guid.NewGuid()
    });
    ExpectInstallerFailure(() => engine.ExecuteAsync(
        InstallerOperation.Uninstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult());
    JsonStore.WriteAtomicBytes(paths.ProgramMarker, marker);
    environment.RendererProcessState = TrackedProcessState.Running;
    ExpectInstallerFailure(() => engine.ExecuteAsync(
        InstallerOperation.Uninstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult());
    environment.RendererProcessState = TrackedProcessState.NotRunning;
    environment.LauncherProcessState = TrackedProcessState.Running;
    ExpectInstallerFailure(() => engine.ExecuteAsync(
        InstallerOperation.Uninstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult());
    environment.LauncherProcessState = TrackedProcessState.NotRunning;
    OperationResult uninstall = engine.ExecuteAsync(
        InstallerOperation.Uninstall, desktopShortcut: false,
        progress: null, logObserver: null, CancellationToken.None)
        .GetAwaiter().GetResult();
    Assert(uninstall.CleanupScheduled && environment.UninstallScheduled);
    return manifest;
}

static LauncherFeed LauncherFeedValue() => new(
    ProductConstants.LauncherUpdateFeedSchemaVersion,
    ProductConstants.ProductId,
    "stable",
    ProductConstants.LauncherReleaseSequence,
    ProductConstants.LauncherVersion,
    new string('b', 40),
    new LauncherFeedArtifact(ProductConstants.LauncherArtifactName,
        1234, new string('b', 64)));

static InstallState InstalledState(RendererFeed feed) => new(
    ProductConstants.SchemaVersion,
    Guid.Parse("11111111-1111-1111-1111-111111111111"),
    $"{feed.SourceCommit}-20260823000000-12345678",
    feed.ReleaseSequence,
    feed.SourceCommit,
    feed.SettingsHash,
    feed.EngineVersion,
    feed.Artifact.Sha256,
    new string('c', 64),
    true,
    DateTimeOffset.UnixEpoch);

static InstallSnapshot Snapshot(InstallState state) =>
    new(true, true, state.InstallationId, state,
        @"C:\uvsr\bin\uvsr-engine.exe", "ready");

static byte[] Sign<T>(T payload, int schemaVersion, ECDsa key, string keyId)
{
    byte[] payloadBytes = JsonSerializer.SerializeToUtf8Bytes(payload,
        JsonStore.Options);
    byte[] signature = key.SignData(payloadBytes, HashAlgorithmName.SHA256,
        DSASignatureFormat.IeeeP1363FixedFieldConcatenation);
    return JsonSerializer.SerializeToUtf8Bytes(new SignedFeedEnvelope(
        schemaVersion, keyId, Convert.ToBase64String(payloadBytes),
        Convert.ToBase64String(signature)), JsonStore.Options);
}

static void Equal<T>(T expected, T actual)
{
    if (!EqualityComparer<T>.Default.Equals(expected, actual))
        throw new Exception($"Expected '{expected}', got '{actual}'.");
}

static void Assert(bool condition)
{
    if (!condition)
        throw new Exception("Assertion failed.");
}

static void Throws<T>(Action action) where T : Exception
{
    try
    {
        action();
    }
    catch (T)
    {
        return;
    }
    throw new Exception($"Expected {typeof(T).Name}.");
}

sealed record RendererFixture(
    string PackageRoot,
    string Archive,
    RendererFeed Feed,
    PackageManifest Manifest);

sealed class TestRendererHandler : HttpMessageHandler
{
    private readonly Dictionary<string, byte[]> _responses =
        new(StringComparer.Ordinal);

    internal void Set(RendererFeed feed, byte[] envelope, byte[] archive)
    {
        _responses.Clear();
        _responses.Add(ProductConstants.RendererFeedUrl, envelope);
        _responses.Add(LauncherManager.BuildRendererArtifactUri(feed).AbsoluteUri,
            archive);
    }

    protected override Task<HttpResponseMessage> SendAsync(
        HttpRequestMessage request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        string uri = request.RequestUri?.AbsoluteUri ?? string.Empty;
        if (request.Method != HttpMethod.Get ||
            !_responses.TryGetValue(uri, out byte[]? bytes))
            throw new HttpRequestException("Unexpected lifecycle test request.");
        HttpResponseMessage response = new(HttpStatusCode.OK)
        {
            RequestMessage = new HttpRequestMessage(HttpMethod.Get, uri),
            Content = new ByteArrayContent(bytes)
        };
        return Task.FromResult(response);
    }
}

sealed class TestInstallerShell : IInstallerShell
{
    private readonly InstallerPaths _paths;

    internal bool FailNextApply { get; set; }
    internal int LaunchCount { get; private set; }

    internal TestInstallerShell(InstallerPaths paths) => _paths = paths;

    public void ValidateCanApply(
        Guid installationId,
        LauncherState? previousLauncher,
        bool desktopShortcut,
        Guid? transactionId = null)
    {
        foreach (string shortcut in new[]
        {
            _paths.StartMenuShortcut,
            _paths.DesktopShortcut
        })
        {
            if (File.Exists(shortcut) &&
                !File.ReadAllText(shortcut).StartsWith(
                    installationId.ToString("D") + "|",
                    StringComparison.Ordinal))
                throw new InstallerException("A foreign shortcut was preserved.");
        }
    }

    public void Apply(
        Guid installationId,
        InstallState? rendererState,
        LauncherState launcherState,
        InstallLog log,
        Guid? transactionId = null)
    {
        ValidateCanApply(installationId, launcherState,
            launcherState.DesktopShortcut, transactionId);
        launcherState.Validate(installationId);
        rendererState?.Validate(installationId);
        if (FailNextApply)
        {
            FailNextApply = false;
            throw new InstallerException("Injected isolated shell failure.");
        }
        string launcher = _paths.LauncherExecutable(
            launcherState.ExecutableSha256);
        if (!File.Exists(launcher) ||
            (rendererState is not null &&
             !File.Exists(_paths.VersionExecutable(
                 rendererState.ActiveVersionId))))
            throw new InstallerException("The isolated shell target is missing.");
        string contents = installationId.ToString("D") + "|" + launcher;
        Directory.CreateDirectory(_paths.StartMenuDirectory);
        File.WriteAllText(_paths.StartMenuShortcut, contents);
        if (launcherState.DesktopShortcut)
        {
            Directory.CreateDirectory(_paths.DesktopDirectory);
            File.WriteAllText(_paths.DesktopShortcut, contents);
        }
        else if (File.Exists(_paths.DesktopShortcut))
        {
            File.Delete(_paths.DesktopShortcut);
        }
        log.Write("Applied isolated owned shortcuts.");
    }

    public void Remove(Guid installationId, InstallLog log)
    {
        foreach (string shortcut in new[]
        {
            _paths.DesktopShortcut,
            _paths.StartMenuShortcut
        })
        {
            if (File.Exists(shortcut) && File.ReadAllText(shortcut).StartsWith(
                    installationId.ToString("D") + "|",
                    StringComparison.Ordinal))
                File.Delete(shortcut);
        }
        log.Write("Removed isolated owned shortcuts.");
    }

    public void RemoveTransactionArtifacts(
        Guid installationId,
        Guid transactionId,
        InstallLog log) => Remove(installationId, log);

    public void Launch(InstallState state)
    {
        state.Validate(state.InstallationId);
        string executable = _paths.VersionExecutable(state.ActiveVersionId);
        if (!File.Exists(executable) ||
            PayloadPackager.ComputeSha256(executable) != state.ExecutableSha256)
            throw new InstallerException("The isolated launch target changed.");
        LaunchCount++;
    }
}

sealed class TestInstallerEnvironment
{
    private readonly InstallerPaths _paths;

    internal TrackedProcessState RendererProcessState { get; set; } =
        TrackedProcessState.NotRunning;
    internal TrackedProcessState LauncherProcessState { get; set; } =
        TrackedProcessState.NotRunning;
    internal bool UninstallScheduled { get; private set; }
    internal InstallerEngineServices Services { get; }

    internal TestInstallerEnvironment(InstallerPaths paths)
    {
        _paths = paths;
        Services = new InstallerEngineServices(
            PlatformCheck: () => { },
            DiskSpaceCheck: (_, _) => { },
            InspectRendererProcesses: root => Inspection(
                RendererProcessState, Path.Combine(root,
                    ProductConstants.EngineExecutableName)),
            InspectLauncherProcesses: (root, _) => Inspection(
                LauncherProcessState, Path.Combine(root,
                    ProductConstants.LauncherExecutableName)),
            ScheduleUninstall: Schedule,
            RemoveStagedRegistryEntry: _ => { });
    }

    private void Schedule(
        InstallerPaths paths,
        Guid installationId,
        Guid transactionId,
        InstallState? previousState,
        InstallLog log)
    {
        if (_paths != paths)
            throw new Exception("Uninstall escaped its isolated test paths.");
        Directory.CreateDirectory(paths.OperationsRoot);
        JsonStore.WriteAtomic(paths.OperationsMarker,
            new OwnerMarker(ProductConstants.SchemaVersion,
                ProductConstants.ProductId, installationId));
        JsonStore.WriteAtomic(paths.UninstallRecordFile,
            new UninstallRecord(ProductConstants.SchemaVersion,
                ProductConstants.ProductId, installationId, transactionId,
                "prepared", previousState, DateTimeOffset.UtcNow));
        UninstallScheduled = true;
        log.Write("Scheduled isolated uninstall cleanup.");
    }

    private static ExactProcessInspection Inspection(
        TrackedProcessState state,
        string executable) => new(
            state,
            state == TrackedProcessState.Running
                ? new[] { new ExactProcessIdentity(42, 1, executable) }
                : Array.Empty<ExactProcessIdentity>());
}

sealed class TemporaryDirectory : IDisposable
{
    internal string Path { get; } = System.IO.Path.Combine(
        System.IO.Path.GetTempPath(), "uvsr-launcher-tests",
        Guid.NewGuid().ToString("N"));

    internal TemporaryDirectory() => Directory.CreateDirectory(Path);

    public void Dispose()
    {
        if (Directory.Exists(Path))
            Directory.Delete(Path, true);
    }
}
