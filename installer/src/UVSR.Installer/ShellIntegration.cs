using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace UvsrInstaller;

internal sealed class ShellIntegration
{
    private const string UninstallParentPath = @"Software\Microsoft\Windows\CurrentVersion\Uninstall";
    private const string UninstallKeyName = "UVSR";
    private const string UninstallKeyPath = UninstallParentPath + @"\" + UninstallKeyName;
    private const uint ShortcutRawPath = 0x4;
    private readonly InstallerPaths _paths;

    internal ShellIntegration(InstallerPaths paths) => _paths = paths;

    internal void ValidateCanApply(
        Guid installationId,
        LauncherState? previousLauncher,
        bool desktopShortcut,
        Guid? transactionId = null)
    {
        using RegistryKey? key = Registry.CurrentUser.OpenSubKey(UninstallKeyPath);
        if (key is not null && !RegistryOwnedBy(key, installationId) &&
            (transactionId is null || !RegistryTransactionOwnedBy(key, transactionId.Value)))
            throw new InstallerException("An unrelated Apps & Features entry already uses the UVSR name. It was preserved.");
        _paths.ValidateShellPath(_paths.StartMenuDirectory,
            "UVSR Start menu directory");
        ValidateShortcutCollision(_paths.StartMenuShortcut,
            shortcut => IsOwnedLauncherShortcut(shortcut, installationId),
            "An unrelated Start menu shortcut already uses the UVSR Launcher name.");
        if (desktopShortcut)
        {
            _paths.ValidateShellPath(_paths.DesktopShortcut,
                "UVSR desktop shortcut");
            ValidateShortcutCollision(_paths.DesktopShortcut,
                shortcut => IsOwnedLauncherShortcut(shortcut, installationId),
                "An unrelated desktop shortcut named UVSR Launcher already exists. It was preserved.");
        }
    }

    internal void Apply(
        Guid installationId,
        InstallState? rendererState,
        LauncherState launcherState,
        InstallLog log,
        Guid? transactionId = null)
    {
        rendererState?.Validate(installationId);
        launcherState.Validate(installationId);
        ValidateCanApply(installationId, launcherState,
            launcherState.DesktopShortcut, transactionId);
        ShellSnapshot snapshot = CaptureSnapshot();
        ShellMutation mutation = new();
        try
        {
            ApplyCore(installationId, rendererState, launcherState, log,
                transactionId, mutation);
        }
        catch (Exception operationFailure)
        {
            ThrowAfterShellRollback(snapshot, mutation, installationId,
                transactionId, operationFailure);
        }
    }

    private void ApplyCore(
        Guid installationId,
        InstallState? rendererState,
        LauncherState launcherState,
        InstallLog log,
        Guid? transactionId,
        ShellMutation mutation)
    {
        string launcher = _paths.LauncherExecutable(launcherState.ExecutableSha256);
        string launcherRoot = _paths.LauncherVersionRoot(launcherState.ExecutableSha256);
        if (!File.Exists(launcher))
            throw new InstallerException("The installed UVSR Launcher is incomplete.");
        string? renderer = rendererState is null
            ? null
            : _paths.VersionExecutable(rendererState.ActiveVersionId);
        string? rendererRoot = rendererState is null
            ? null
            : _paths.VersionRoot(rendererState.ActiveVersionId);
        if (renderer is not null && !File.Exists(renderer))
            throw new InstallerException("The installed UVSR program is incomplete.");

        _paths.ValidateShellPath(_paths.StartMenuDirectory,
            "UVSR Start menu directory");
        Directory.CreateDirectory(_paths.StartMenuDirectory);
        _paths.ValidateShellPath(_paths.StartMenuDirectory,
            "UVSR Start menu directory");
        CreateOrReplaceShortcut(_paths.StartMenuShortcut, launcher,
            string.Empty, launcherRoot, launcher,
            "Install, launch, update, or remove UVSR",
            shortcut => IsOwnedLauncherShortcut(shortcut, installationId));
        mutation.StartMenuShortcut = ShellMutationState.OwnedPresent;
        if (DeleteShortcutIfOwned(_paths.LegacyStartMenuShortcut,
                IsOwnedLegacyManagerShortcut, log))
            mutation.LegacyStartMenuShortcut = ShellMutationState.ExpectedAbsent;

        if (launcherState.DesktopShortcut)
        {
            CreateOrReplaceShortcut(_paths.DesktopShortcut, launcher,
                string.Empty, launcherRoot, launcher,
                "Open UVSR Launcher",
                shortcut => IsOwnedLauncherShortcut(shortcut, installationId));
            mutation.DesktopShortcut = ShellMutationState.OwnedPresent;
        }
        else
        {
            if (DeleteShortcutIfOwned(_paths.DesktopShortcut,
                    shortcut => IsOwnedLauncherShortcut(shortcut, installationId), log))
                mutation.DesktopShortcut = ShellMutationState.ExpectedAbsent;
        }
        if (DeleteShortcutIfOwned(_paths.LegacyDesktopShortcut,
                shortcut => IsOwnedLegacyDesktopShortcut(shortcut, installationId), log))
            mutation.LegacyDesktopShortcut = ShellMutationState.ExpectedAbsent;

        using RegistryKey? existing = Registry.CurrentUser.OpenSubKey(
            UninstallKeyPath, writable: true);
        if (existing is not null)
        {
            if (!RegistryOwnedBy(existing, installationId) &&
                (transactionId is null ||
                 !RegistryTransactionOwnedBy(existing, transactionId.Value)))
                throw new InstallerException("An unrelated Apps & Features entry already uses the UVSR name. It was preserved.");
            mutation.Registry = ShellMutationState.OwnedPresent;
            WriteRegistryValues(existing, installationId, rendererState,
                launcherState, launcher, launcherRoot, rendererRoot, transactionId);
        }
        else
        {
            if (transactionId is null)
                throw new InstallerException(
                    "Windows could not create the UVSR Apps & Features entry without an active UVSR Launcher operation.");
            CreateFreshRegistryEntry(installationId, rendererState, launcherState,
                launcher, launcherRoot, rendererRoot, transactionId.Value);
            mutation.Registry = ShellMutationState.OwnedPresent;
        }
        log.Write("Updated the per-user Start menu, desktop-shortcut preference, and Apps & Features entry.");
    }

    private void CreateFreshRegistryEntry(
        Guid installationId,
        InstallState? rendererState,
        LauncherState launcherState,
        string launcher,
        string launcherRoot,
        string? rendererRoot,
        Guid transactionId)
    {
        string stagingName = RegistryStagingKeyName(transactionId);
        string stagingPath = UninstallParentPath + @"\" + stagingName;
        try
        {
            // A transaction-specific staging key can be completed without ever
            // exposing an empty, apparently foreign UVSR key after power loss.
            Registry.CurrentUser.DeleteSubKeyTree(stagingPath, throwOnMissingSubKey: false);
            using (RegistryKey staging = Registry.CurrentUser.CreateSubKey(
                       stagingPath, writable: true)
                   ?? throw new InstallerException(
                       "Windows could not stage the UVSR Apps & Features entry."))
            {
                WriteRegistryValues(staging, installationId, rendererState,
                    launcherState, launcher, launcherRoot, rendererRoot, transactionId);
                staging.Flush();
            }

            using RegistryKey parent = Registry.CurrentUser.OpenSubKey(
                    UninstallParentPath, writable: true)
                ?? throw new InstallerException(
                    "Windows could not open the per-user Apps & Features registry area.");
            using RegistryKey? collision = parent.OpenSubKey(UninstallKeyName);
            if (collision is not null)
                throw new InstallerException(
                    "An unrelated Apps & Features entry appeared while UVSR was being installed. It was preserved.");
            int result = RegRenameKey(parent.Handle, stagingName, UninstallKeyName);
            if (result != 0)
                throw new InstallerException(
                    "Windows could not promote the staged UVSR Apps & Features entry.",
                    new System.ComponentModel.Win32Exception(result));
        }
        finally
        {
            Registry.CurrentUser.DeleteSubKeyTree(stagingPath,
                throwOnMissingSubKey: false);
        }
    }

    private void WriteRegistryValues(
        RegistryKey key,
        Guid installationId,
        InstallState? rendererState,
        LauncherState launcherState,
        string launcher,
        string launcherRoot,
        string? rendererRoot,
        Guid? transactionId)
    {
        if (transactionId is not null)
            key.SetValue("UVSRTransactionId", transactionId.Value.ToString("D"),
                RegistryValueKind.String);
        key.SetValue("UVSRProductId", ProductConstants.ProductId, RegistryValueKind.String);
        key.SetValue("UVSRInstallId", installationId.ToString("D"), RegistryValueKind.String);
        key.SetValue("DisplayName", "UVSR Launcher", RegistryValueKind.String);
        key.SetValue("DisplayVersion", launcherState.Version, RegistryValueKind.String);
        key.SetValue("Publisher", "UVSR", RegistryValueKind.String);
        key.SetValue("InstallLocation", _paths.ProgramRoot, RegistryValueKind.String);
        key.SetValue("DisplayIcon", launcher, RegistryValueKind.String);
        key.SetValue("UninstallString", Quote(launcher) + " --uninstall", RegistryValueKind.String);
        key.SetValue("ModifyPath", Quote(launcher), RegistryValueKind.String);
        key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
        long bytes = DirectorySize(launcherRoot) +
            (rendererRoot is null ? 0 : DirectorySize(rendererRoot));
        key.SetValue("EstimatedSize", (int)Math.Min(int.MaxValue, bytes / 1024), RegistryValueKind.DWord);
        key.DeleteValue("UVSRTransactionId", throwOnMissingValue: false);
    }

    internal void Remove(Guid installationId, InstallLog log)
    {
        RemoveOwnedStagedRegistryEntries(installationId, log);
        ShellSnapshot snapshot = CaptureSnapshot();
        ShellMutation mutation = new();
        try
        {
            RemoveCore(installationId, log, transactionId: null, mutation);
        }
        catch (Exception operationFailure)
        {
            ThrowAfterShellRollback(snapshot, mutation, installationId,
                transactionId: null, operationFailure);
        }
    }

    private void RemoveCore(
        Guid installationId,
        InstallLog log,
        Guid? transactionId,
        ShellMutation mutation)
    {
        using (RegistryKey? key = Registry.CurrentUser.OpenSubKey(UninstallKeyPath))
        {
            if (key is not null)
            {
                if (!RegistryOwnedBy(key, installationId) &&
                    (transactionId is null || !RegistryTransactionOwnedBy(key, transactionId.Value)))
                    throw new InstallerException("The UVSR Apps & Features entry is not owned by this installation. It was preserved.");
                Registry.CurrentUser.DeleteSubKey(UninstallKeyPath, throwOnMissingSubKey: false);
                mutation.Registry = ShellMutationState.ExpectedAbsent;
            }
        }
        if (DeleteShortcutIfOwned(_paths.DesktopShortcut,
                shortcut => IsOwnedLauncherShortcut(shortcut, installationId), log))
            mutation.DesktopShortcut = ShellMutationState.ExpectedAbsent;
        if (DeleteShortcutIfOwned(_paths.StartMenuShortcut,
                shortcut => IsOwnedLauncherShortcut(shortcut, installationId), log))
            mutation.StartMenuShortcut = ShellMutationState.ExpectedAbsent;
        if (DeleteShortcutIfOwned(_paths.LegacyDesktopShortcut,
                shortcut => IsOwnedLegacyDesktopShortcut(shortcut, installationId), log))
            mutation.LegacyDesktopShortcut = ShellMutationState.ExpectedAbsent;
        if (DeleteShortcutIfOwned(_paths.LegacyStartMenuShortcut,
                IsOwnedLegacyManagerShortcut, log))
            mutation.LegacyStartMenuShortcut = ShellMutationState.ExpectedAbsent;
        if (Directory.Exists(_paths.StartMenuDirectory))
        {
            _paths.ValidateShellPath(_paths.StartMenuDirectory,
                "UVSR Start menu directory");
            if (!Directory.EnumerateFileSystemEntries(_paths.StartMenuDirectory).Any())
                Directory.Delete(_paths.StartMenuDirectory, recursive: false);
        }
        log.Write("Removed UVSR's owned per-user shortcuts and Apps & Features entry.");
    }

    internal void RemoveTransactionArtifacts(
        Guid installationId,
        Guid transactionId,
        InstallLog log)
    {
        RemoveStagedRegistryEntry(transactionId);
        ShellSnapshot snapshot = CaptureSnapshot();
        ShellMutation mutation = new();
        try
        {
            RemoveCore(installationId, log, transactionId, mutation);
        }
        catch (Exception operationFailure)
        {
            ThrowAfterShellRollback(snapshot, mutation, installationId,
                transactionId, operationFailure);
        }
    }

    private static string RegistryStagingKeyName(Guid transactionId) =>
        $"UVSR Installer {transactionId:N}.staging";

    internal static void RemoveStagedRegistryEntry(Guid transactionId)
    {
        Registry.CurrentUser.DeleteSubKeyTree(
            UninstallParentPath + @"\" + RegistryStagingKeyName(transactionId),
            throwOnMissingSubKey: false);
    }

    private static void RemoveOwnedStagedRegistryEntries(
        Guid installationId,
        InstallLog log)
    {
        using RegistryKey? parent = Registry.CurrentUser.OpenSubKey(
            UninstallParentPath, writable: true);
        if (parent is null)
            return;
        foreach (string name in parent.GetSubKeyNames())
        {
            const string prefix = "UVSR Installer ";
            const string suffix = ".staging";
            if (!name.StartsWith(prefix, StringComparison.Ordinal) ||
                !name.EndsWith(suffix, StringComparison.Ordinal) ||
                !Guid.TryParseExact(name[prefix.Length..^suffix.Length], "N", out _))
                continue;
            using RegistryKey? candidate = parent.OpenSubKey(name);
            if (candidate is null || !RegistryOwnedBy(candidate, installationId))
                continue;
            candidate.Dispose();
            parent.DeleteSubKeyTree(name, throwOnMissingSubKey: false);
            log.Write($"Removed an interrupted owned Apps & Features staging entry: {name}");
        }
    }

    internal void Launch(InstallState state)
    {
        state.Validate(state.InstallationId);
        string executable = _paths.VersionExecutable(state.ActiveVersionId);
        if (!File.Exists(executable) ||
            !string.Equals(PayloadPackager.ComputeSha256(executable), state.ExecutableSha256,
                StringComparison.Ordinal))
            throw new InstallerException("The installed UVSR executable is missing or has changed. Open UVSR Launcher and choose Install to repair it.");
        System.Diagnostics.ProcessStartInfo start = new()
        {
            FileName = executable,
            WorkingDirectory = _paths.VersionRoot(state.ActiveVersionId),
            UseShellExecute = true
        };
        System.Diagnostics.Process.Start(start);
    }

    private bool IsOwnedLauncherShortcut(ShortcutInfo shortcut, Guid installationId)
    {
        if (!string.IsNullOrWhiteSpace(shortcut.Arguments) || string.IsNullOrWhiteSpace(shortcut.Target))
            return false;
        try
        {
            string target = Path.GetFullPath(shortcut.Target);
            if (!IsLauncherTargetInOwnedLayout(_paths, target))
                return false;
            SafePaths.RejectReparsePathChain(target,
                "installed UVSR Launcher shortcut target");
            OwnerMarker marker = JsonStore.Read<OwnerMarker>(_paths.ProgramMarker);
            return marker.SchemaVersion == ProductConstants.SchemaVersion &&
                   marker.InstallationId == installationId &&
                   string.Equals(marker.ProductId, ProductConstants.ProductId,
                       StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or
                                   PathTooLongException or InstallerException or IOException or
                                   UnauthorizedAccessException)
        {
            return false;
        }
    }

    internal static bool IsLauncherTargetInOwnedLayout(
        InstallerPaths paths,
        string? target)
    {
        if (string.IsNullOrWhiteSpace(target))
            return false;
        try
        {
            string fullTarget = Path.GetFullPath(target);
            if (!string.Equals(Path.GetFileName(fullTarget),
                    ProductConstants.LauncherExecutableName,
                    StringComparison.OrdinalIgnoreCase))
                return false;
            string? versionRoot = Path.GetDirectoryName(fullTarget);
            if (versionRoot is null)
                return false;
            string hash = Path.GetFileName(versionRoot);
            return ProductConstants.HashRegex().IsMatch(hash) &&
                   PathsEqual(Path.GetDirectoryName(versionRoot),
                       paths.LauncherVersionsDirectory) &&
                   PathsEqual(fullTarget, paths.LauncherExecutable(hash));
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or
                                   PathTooLongException or InstallerException)
        {
            return false;
        }
    }

    private bool IsOwnedLegacyManagerShortcut(ShortcutInfo shortcut) =>
        string.IsNullOrWhiteSpace(shortcut.Arguments) &&
        PathsEqual(shortcut.Target, _paths.LegacyManagerPath);

    private bool IsOwnedLegacyDesktopShortcut(ShortcutInfo shortcut, Guid installationId)
    {
        if (!string.IsNullOrWhiteSpace(shortcut.Arguments) ||
            string.IsNullOrWhiteSpace(shortcut.Target))
            return false;
        if (PathsEqual(shortcut.Target, _paths.LegacyManagerPath) ||
            IsOwnedLauncherShortcut(shortcut, installationId))
            return true;
        try
        {
            string target = Path.GetFullPath(shortcut.Target);
            return SafePaths.IsStrictDescendant(target, _paths.VersionsDirectory) &&
                   string.Equals(Path.GetFileName(target), "uvsr.exe",
                       StringComparison.OrdinalIgnoreCase) &&
                   string.Equals(Path.GetFileName(Path.GetDirectoryName(target)), "bin",
                       StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return false;
        }
    }

    private static bool PathsEqual(string? first, string second)
    {
        if (string.IsNullOrWhiteSpace(first))
            return false;
        try
        {
            return string.Equals(Path.GetFullPath(first), Path.GetFullPath(second),
                StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return false;
        }
    }

    private static bool RegistryOwnedBy(RegistryKey key, Guid installationId) =>
        string.Equals(key.GetValue("UVSRProductId") as string, ProductConstants.ProductId,
            StringComparison.OrdinalIgnoreCase) &&
        string.Equals(key.GetValue("UVSRInstallId") as string, installationId.ToString("D"),
            StringComparison.OrdinalIgnoreCase);

    private static bool RegistryTransactionOwnedBy(RegistryKey key, Guid transactionId) =>
        string.Equals(key.GetValue("UVSRTransactionId") as string,
            transactionId.ToString("D"), StringComparison.OrdinalIgnoreCase);

    private void ValidateShortcutCollision(
        string path,
        Func<ShortcutInfo, bool> owned,
        string error)
    {
        if (!File.Exists(path))
            return;
        _paths.ValidateShellPath(path, "shortcut collision path");
        ShortcutInfo shortcut = ReadShortcut(path);
        if (!owned(shortcut))
            throw new InstallerException(error);
    }

    private void CreateOrReplaceShortcut(
        string path,
        string target,
        string arguments,
        string workingDirectory,
        string icon,
        string description,
        Func<ShortcutInfo, bool> owned)
    {
        string parent = Path.GetDirectoryName(path)!;
        _paths.ValidateShellPath(parent, "shortcut directory");
        Directory.CreateDirectory(parent);
        _paths.ValidateShellPath(parent, "shortcut directory");
        if (File.Exists(path))
            _paths.ValidateShellPath(path, "managed shortcut path");
        if (File.Exists(path) && !owned(ReadShortcut(path)))
            throw new InstallerException($"The existing shortcut '{path}' is not owned by UVSR Launcher.");
        string temporary = path + $".{Guid.NewGuid():N}.tmp";
        try
        {
            IShellLinkW link = (IShellLinkW)(object)new ShellLink();
            link.SetPath(target);
            link.SetArguments(arguments);
            link.SetWorkingDirectory(workingDirectory);
            link.SetDescription(description);
            link.SetIconLocation(icon, 0);
            ((IPersistFile)link).Save(temporary, true);
            Marshal.FinalReleaseComObject(link);
            File.Move(temporary, path, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary))
                File.Delete(temporary);
        }
    }

    private static ShortcutInfo ReadShortcut(string path)
    {
        try
        {
            IShellLinkW link = (IShellLinkW)(object)new ShellLink();
            ((IPersistFile)link).Load(path, 0);
            StringBuilder target = new(32768);
            StringBuilder arguments = new(32768);
            link.GetPath(target, target.Capacity, IntPtr.Zero, ShortcutRawPath);
            link.GetArguments(arguments, arguments.Capacity);
            Marshal.FinalReleaseComObject(link);
            return new ShortcutInfo(target.ToString(), arguments.ToString());
        }
        catch (Exception ex) when (ex is COMException or IOException)
        {
            throw new InstallerException($"The shortcut '{path}' could not be inspected safely.", ex);
        }
    }

    private bool DeleteShortcutIfOwned(
        string path,
        Func<ShortcutInfo, bool> owned,
        InstallLog log)
    {
        if (!File.Exists(path))
            return false;
        ShortcutInfo shortcut;
        try
        {
            _paths.ValidateShellPath(path, "managed shortcut path");
            shortcut = ReadShortcut(path);
        }
        catch (Exception ex) when (ex is InstallerException or IOException or
                                   UnauthorizedAccessException)
        {
            log.Write($"Preserved a shortcut that could not be inspected safely: {path} ({ex.Message})");
            return false;
        }
        if (!owned(shortcut))
        {
            log.Write($"Preserved modified or unrelated shortcut: {path}");
            return false;
        }
        File.Delete(path);
        return true;
    }

    private static long DirectorySize(string root)
    {
        long total = 0;
        foreach (string file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
        {
            try { total += new FileInfo(file).Length; }
            catch (IOException) { }
        }
        return total;
    }

    private static string Quote(string value) => $"\"{value.Replace("\"", "\\\"")}\"";

    private ShellSnapshot CaptureSnapshot()
    {
        return new ShellSnapshot(
            CaptureFile(_paths.DesktopShortcut),
            CaptureFile(_paths.StartMenuShortcut),
            CaptureFile(_paths.LegacyDesktopShortcut),
            CaptureFile(_paths.LegacyStartMenuShortcut),
            CaptureRegistry());
    }

    private byte[]? CaptureFile(string path)
    {
        if (!File.Exists(path))
            return null;
        _paths.ValidateShellPath(path, "managed shell snapshot");
        return File.ReadAllBytes(path);
    }

    private void RestoreFile(
        string path,
        byte[]? contents,
        ShellMutationState mutation,
        Func<ShortcutInfo, bool> owned)
    {
        if (mutation == ShellMutationState.None)
            return;
        bool exists = File.Exists(path);
        if (exists)
            _paths.ValidateShellPath(path, "managed shell rollback path");
        if (mutation == ShellMutationState.OwnedPresent)
        {
            // The transaction wrote an owned shortcut. A missing or foreign file
            // is a later external change and must be preserved.
            if (!exists || !owned(ReadShortcut(path)))
                return;
        }
        else if (exists)
        {
            // The transaction deleted its shortcut. A newly created file belongs
            // to someone else, so rollback must not replace it.
            return;
        }

        if (contents is null)
        {
            if (File.Exists(path))
                File.Delete(path);
            return;
        }
        string parent = Path.GetDirectoryName(path)!;
        _paths.ValidateShellPath(parent, "managed shell rollback directory");
        Directory.CreateDirectory(parent);
        _paths.ValidateShellPath(parent, "managed shell rollback directory");
        string temporary = path + $".{Guid.NewGuid():N}.rollback";
        try
        {
            using (FileStream stream = new(temporary, FileMode.CreateNew, FileAccess.Write,
                       FileShare.None, 4096, FileOptions.WriteThrough))
            {
                stream.Write(contents);
                stream.Flush(flushToDisk: true);
            }
            File.Move(temporary, path, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary))
                File.Delete(temporary);
        }
    }

    private static RegistrySnapshot CaptureRegistry()
    {
        using RegistryKey? key = Registry.CurrentUser.OpenSubKey(UninstallKeyPath);
        if (key is null)
            return new RegistrySnapshot(false, new Dictionary<string, RegistryValueSnapshot>());
        Dictionary<string, RegistryValueSnapshot> values = new(StringComparer.OrdinalIgnoreCase);
        foreach (string name in key.GetValueNames())
        {
            object? value = key.GetValue(name, null, RegistryValueOptions.DoNotExpandEnvironmentNames);
            if (value is not null)
                values[name] = new RegistryValueSnapshot(value, key.GetValueKind(name));
        }
        return new RegistrySnapshot(true, values);
    }

    private static void RestoreRegistry(
        RegistrySnapshot snapshot,
        ShellMutationState mutation,
        Guid installationId,
        Guid? transactionId)
    {
        if (mutation == ShellMutationState.None)
            return;
        using (RegistryKey? current = Registry.CurrentUser.OpenSubKey(UninstallKeyPath))
        {
            if (mutation == ShellMutationState.OwnedPresent)
            {
                if (current is null ||
                    (!RegistryOwnedBy(current, installationId) &&
                     (transactionId is null ||
                      !RegistryTransactionOwnedBy(current, transactionId.Value))))
                    return;
            }
            else if (current is not null)
            {
                // A key created after this transaction's deletion is foreign.
                return;
            }
        }

        if (!snapshot.Existed)
        {
            Registry.CurrentUser.DeleteSubKey(UninstallKeyPath, throwOnMissingSubKey: false);
            return;
        }
        using RegistryKey key = Registry.CurrentUser.CreateSubKey(UninstallKeyPath, writable: true)
            ?? throw new InstallerException("Windows could not restore the UVSR Apps & Features entry.");
        foreach (string name in key.GetValueNames())
            key.DeleteValue(name, throwOnMissingValue: false);
        foreach ((string name, RegistryValueSnapshot value) in snapshot.Values)
            key.SetValue(name, value.Value, value.Kind);
    }

    private void ThrowAfterShellRollback(
        ShellSnapshot snapshot,
        ShellMutation mutation,
        Guid installationId,
        Guid? transactionId,
        Exception operationFailure)
    {
        try
        {
            RestoreRegistry(snapshot.Registry, mutation.Registry,
                installationId, transactionId);
            RestoreFile(_paths.StartMenuShortcut, snapshot.StartMenuShortcut,
                mutation.StartMenuShortcut,
                shortcut => IsOwnedLauncherShortcut(shortcut, installationId));
            RestoreFile(_paths.DesktopShortcut, snapshot.DesktopShortcut,
                mutation.DesktopShortcut,
                shortcut => IsOwnedLauncherShortcut(shortcut, installationId));
            RestoreFile(_paths.LegacyStartMenuShortcut,
                snapshot.LegacyStartMenuShortcut,
                mutation.LegacyStartMenuShortcut, IsOwnedLegacyManagerShortcut);
            RestoreFile(_paths.LegacyDesktopShortcut,
                snapshot.LegacyDesktopShortcut,
                mutation.LegacyDesktopShortcut,
                shortcut => IsOwnedLegacyDesktopShortcut(shortcut, installationId));
        }
        catch (Exception rollbackFailure)
        {
            throw new ShellRollbackException(
                "Windows shell integration failed and could not be restored completely. UVSR Launcher will retry recovery the next time it opens.",
                new AggregateException(operationFailure, rollbackFailure));
        }
        System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(operationFailure).Throw();
        throw new InvalidOperationException("Unreachable shell rollback path.");
    }

    private sealed record RegistryValueSnapshot(object Value, RegistryValueKind Kind);
    private sealed record RegistrySnapshot(
        bool Existed,
        IReadOnlyDictionary<string, RegistryValueSnapshot> Values);
    private sealed record ShellSnapshot(
        byte[]? DesktopShortcut,
        byte[]? StartMenuShortcut,
        byte[]? LegacyDesktopShortcut,
        byte[]? LegacyStartMenuShortcut,
        RegistrySnapshot Registry);

    private enum ShellMutationState
    {
        None,
        OwnedPresent,
        ExpectedAbsent
    }

    private sealed class ShellMutation
    {
        internal ShellMutationState DesktopShortcut { get; set; }
        internal ShellMutationState StartMenuShortcut { get; set; }
        internal ShellMutationState LegacyDesktopShortcut { get; set; }
        internal ShellMutationState LegacyStartMenuShortcut { get; set; }
        internal ShellMutationState Registry { get; set; }
    }

    private sealed record ShortcutInfo(string Target, string Arguments);

    [ComImport]
    [Guid("00021401-0000-0000-C000-000000000046")]
    private sealed class ShellLink { }

    [ComImport]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("000214F9-0000-0000-C000-000000000046")]
    private interface IShellLinkW
    {
        void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder path, int maximumPath,
            IntPtr findData, uint flags);
        void GetIdList(out IntPtr idList);
        void SetIdList(IntPtr idList);
        void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder name, int maximumName);
        void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string name);
        void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder directory, int maximumPath);
        void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string directory);
        void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder arguments, int maximumPath);
        void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string arguments);
        void GetHotkey(out short hotkey);
        void SetHotkey(short hotkey);
        void GetShowCmd(out int showCommand);
        void SetShowCmd(int showCommand);
        void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder iconPath,
            int iconPathLength, out int iconIndex);
        void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string iconPath, int iconIndex);
        void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string path, uint reserved);
        void Resolve(IntPtr window, uint flags);
        void SetPath([MarshalAs(UnmanagedType.LPWStr)] string path);
    }

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
    private static extern int RegRenameKey(
        SafeRegistryHandle key,
        string subKeyName,
        string newKeyName);
}
