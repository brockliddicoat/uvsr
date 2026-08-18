namespace UvsrInstaller;

internal sealed record InstallerPaths(
    string LocalApplicationData,
    string ProgramRoot,
    string StateRoot,
    string DesktopDirectory,
    string ProgramsDirectory)
{
    internal string ProgramMarker => Path.Combine(ProgramRoot, ProductConstants.OwnerMarkerName);
    internal string StateMarker => Path.Combine(StateRoot, ProductConstants.OwnerMarkerName);
    internal string StateFile => Path.Combine(StateRoot, ProductConstants.StateFileName);
    internal string TransactionFile => Path.Combine(StateRoot, ProductConstants.TransactionFileName);
    internal string LogsDirectory => Path.Combine(StateRoot, "logs");
    internal string DownloadsDirectory => Path.Combine(StateRoot, "downloads");
    internal string ToolsDirectory => Path.Combine(ProgramRoot, "tools");
    internal string CacheDirectory => Path.Combine(ProgramRoot, "cache");
    internal string SourceDirectory => Path.Combine(CacheDirectory, "source");
    internal string BuildDirectory => Path.Combine(CacheDirectory, "build");
    internal string StagingDirectory => Path.Combine(ProgramRoot, "staging");
    internal string VersionsDirectory => Path.Combine(ProgramRoot, "versions");
    internal string LauncherRoot => Path.Combine(ProgramRoot, "launcher");
    internal string LauncherVersionsDirectory => Path.Combine(LauncherRoot, "versions");
    internal string LauncherStagingDirectory => Path.Combine(LauncherRoot, "staging");
    internal string LauncherStateFile => Path.Combine(StateRoot, "launcher-state.json");
    internal string LauncherTransactionFile => Path.Combine(StateRoot, "launcher-update.json");
    internal string VisualStudioLayoutDirectory => Path.Combine(ProgramRoot, "vs-buildtools-layout");
    internal string VisualStudioLayoutRecord => Path.Combine(StateRoot, "vs-buildtools-layout.json");
    internal string LegacyManagerPath => Path.Combine(ProgramRoot,
        ProductConstants.LegacyInstalledManagerName);
    internal string DesktopShortcut => Path.Combine(DesktopDirectory, "UVSR Launcher.lnk");
    internal string LegacyDesktopShortcut => Path.Combine(DesktopDirectory, "UVSR.lnk");
    internal string StartMenuDirectory => Path.Combine(ProgramsDirectory, "UVSR");
    internal string StartMenuShortcut => Path.Combine(StartMenuDirectory, "UVSR Launcher.lnk");
    internal string LegacyStartMenuShortcut => Path.Combine(StartMenuDirectory,
        "UVSR Installer.lnk");
    internal string OperationsRoot => Path.Combine(LocalApplicationData, "UVSR Installer Operations");
    internal string OperationsMarker => Path.Combine(OperationsRoot, ProductConstants.OwnerMarkerName);
    internal string UninstallRecordFile => Path.Combine(OperationsRoot, "uninstall.json");
    internal string HelpersDirectory => Path.Combine(OperationsRoot, "helpers");

    internal static InstallerPaths ForCurrentUser()
    {
        string local = RequireKnownFolder(Environment.SpecialFolder.LocalApplicationData);
        string desktop = RequireKnownFolder(Environment.SpecialFolder.DesktopDirectory);
        string programs = RequireKnownFolder(Environment.SpecialFolder.Programs);
        return Create(local, desktop, programs);
    }

    internal static InstallerPaths Create(string local, string desktop, string programs)
    {
        local = Path.GetFullPath(local);
        desktop = Path.GetFullPath(desktop);
        programs = Path.GetFullPath(programs);
        SafePaths.RejectReparsePathChain(local, "local application-data directory");
        SafePaths.RejectReparsePathChain(desktop, "desktop directory");
        SafePaths.RejectReparsePathChain(programs, "Start menu directory");
        string programRoot = Path.GetFullPath(Path.Combine(local, "Programs", "UVSR"));
        string stateRoot = Path.GetFullPath(Path.Combine(local, "UVSR Installer"));
        string operationsRoot = Path.GetFullPath(Path.Combine(local,
            "UVSR Installer Operations"));
        if (!SafePaths.IsStrictDescendant(programRoot, local) ||
            !SafePaths.IsStrictDescendant(stateRoot, local) ||
            !SafePaths.IsStrictDescendant(operationsRoot, local) ||
            string.Equals(programRoot, stateRoot, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(programRoot, operationsRoot, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(stateRoot, operationsRoot, StringComparison.OrdinalIgnoreCase))
        {
            throw new InstallerException("Windows returned an unsafe application-data location.");
        }
        SafePaths.RejectReparsePathChain(programRoot, "UVSR program directory");
        SafePaths.RejectReparsePathChain(stateRoot, "UVSR installer-state directory");
        SafePaths.RejectReparsePathChain(operationsRoot,
            "UVSR cleanup-coordination directory");

        return new InstallerPaths(local, programRoot, stateRoot, desktop, programs);
    }

    internal string VersionRoot(string versionId)
    {
        if (!ProductConstants.VersionIdRegex().IsMatch(versionId))
            throw new InstallerException("The installed UVSR version identifier is invalid.");
        return SafePaths.CombineDescendant(VersionsDirectory, versionId);
    }

    internal string VersionExecutable(string versionId) =>
        Path.Combine(VersionRoot(versionId), "bin", "uvsr.exe");

    internal string LauncherVersionRoot(string executableSha256)
    {
        if (!ProductConstants.HashRegex().IsMatch(executableSha256))
            throw new InstallerException("The launcher package identifier is invalid.");
        return SafePaths.CombineDescendant(LauncherVersionsDirectory, executableSha256);
    }

    internal string LauncherExecutable(string executableSha256) =>
        Path.Combine(LauncherVersionRoot(executableSha256),
            ProductConstants.LauncherExecutableName);

    internal string LauncherPackageMarker(string executableSha256) =>
        Path.Combine(LauncherVersionRoot(executableSha256), ".uvsr-launcher-package.json");

    private static string RequireKnownFolder(Environment.SpecialFolder folder)
    {
        string path = Environment.GetFolderPath(folder, Environment.SpecialFolderOption.Create);
        if (string.IsNullOrWhiteSpace(path))
            throw new InstallerException($"Windows could not locate {folder}.");
        return path;
    }
}
