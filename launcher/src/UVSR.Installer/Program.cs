namespace UvsrInstaller;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length == 3 &&
            args[0].Equals("--launcher-health-check", StringComparison.OrdinalIgnoreCase))
        {
            return RunLauncherHealthCheck(args[1], args[2],
                LauncherTypography.EnsureAvailable);
        }

        if (args.Length == 5 && args[0].Equals("--cleanup", StringComparison.OrdinalIgnoreCase) &&
            int.TryParse(args[1], out int parentProcessId) &&
            long.TryParse(args[2], out long parentStartTimeUtcTicks) &&
            Guid.TryParse(args[3], out Guid installationId) &&
            Guid.TryParse(args[4], out Guid transactionId))
        {
            ApplicationConfiguration.Initialize();
            int cleanupResult = SelfCleanup.RunHelper(parentProcessId, parentStartTimeUtcTicks,
                installationId, transactionId);
            if (cleanupResult != 0)
                ShowCleanupFailure(cleanupResult);
            if (!SelfCleanup.ScheduleCurrentHelperDeletion(installationId) &&
                cleanupResult == 0)
            {
                cleanupResult = 8;
                ShowCleanupFailure(cleanupResult);
            }
            return cleanupResult;
        }

        ApplicationConfiguration.Initialize();
        try
        {
            InstallerEngine.EnsureSupportedPlatform();
            if (NativeMethods.IsCurrentProcessElevated())
            {
                ShowLauncherError("UVSR Launcher",
                    "UVSR Launcher must run as your normal Windows user so downloaded source is never built with administrator rights. Close this copy, then open it normally (do not choose Run as administrator).",
                    null);
                return 1;
            }
            if (args.Length == 1 &&
                args[0].Equals("--ui-preview", StringComparison.OrdinalIgnoreCase))
            {
                // This isolated support/QA view never reads or migrates the real
                // installation. The unique paths remain nonexistent unless an
                // operator deliberately starts an action from the preview.
                string previewRoot = Path.Combine(Path.GetTempPath(),
                    "UVSR Launcher UI Preview", Guid.NewGuid().ToString("N"));
                InstallerPaths previewPaths = InstallerPaths.Create(previewRoot,
                    Path.Combine(previewRoot, "Desktop"),
                    Path.Combine(previewRoot, "Programs"));
                using InstallerEngine previewEngine = new(previewPaths);
                LauncherTypography.EnsureAvailable();
                Application.Run(new MainForm(previewEngine, false, null));
                return 0;
            }
            InstallerPaths paths = InstallerPaths.ForCurrentUser();
            SelfCleanup.RecoverInterruptedUninstall(paths);
            SelfCleanup.RemoveStaleHelpers(paths);
            OwnerMarker? marker = new OwnershipManager(paths).Inspect();
            if (marker is not null && LauncherManager.TryRedirectToActive(
                    paths, marker, args, out _))
                return 0;
            using InstallerEngine engine = new(paths);
            if (args.Length == 1 && args[0].Equals("--launch", StringComparison.OrdinalIgnoreCase))
            {
                engine.LaunchInstalled();
                return 0;
            }
            bool uninstall = args.Length == 1 &&
                args[0].Equals("--uninstall", StringComparison.OrdinalIgnoreCase);
            Guid? continuationId = args.Length == 2 &&
                args[0].Equals("--continue-uvsr-update", StringComparison.OrdinalIgnoreCase) &&
                Guid.TryParse(args[1], out Guid parsedContinuation)
                    ? parsedContinuation
                    : null;
            bool continueUvsrUpdate = continuationId is not null;
            if (args.Length > 0 && !uninstall && !continueUvsrUpdate)
                throw new InstallerException("UVSR Launcher received an unsupported command.");
            LauncherTypography.EnsureAvailable();
            Application.Run(new MainForm(engine, uninstall, continuationId));
            return 0;
        }
        catch (InstallerException ex)
        {
            ShowLauncherError("UVSR Launcher", ex.Message, null);
            return 1;
        }
        catch (Exception ex)
        {
            ShowLauncherError("UVSR Launcher",
                "UVSR Launcher stopped before making a change. " + ex.Message, null);
            return 2;
        }
    }

    internal static int RunLauncherHealthCheck(
        string sequenceText,
        string version,
        Action ensureTypography)
    {
        ArgumentNullException.ThrowIfNull(ensureTypography);
        if (!long.TryParse(sequenceText, out long sequence) ||
            sequence != ProductConstants.LauncherReleaseSequence ||
            !string.Equals(version, ProductConstants.LauncherVersion,
                StringComparison.Ordinal))
            return 3;
        try
        {
            ensureTypography();
            return 0;
        }
        catch
        {
            return 4;
        }
    }

    private static void ShowCleanupFailure(int result)
    {
        string reason = result switch
        {
            3 => "UVSR Launcher could not verify the pending uninstall record.",
            4 => "UVSR or UVSR Launcher is still running.",
            5 or 6 => "Windows interrupted the uninstall before all files were secured.",
            7 => "Windows could not finish the pending uninstall record.",
            8 => "UVSR was removed, but Windows could not remove a temporary cleanup copy.",
            _ => "Windows could not finish removing UVSR."
        };
        try
        {
            ShowLauncherError("UVSR Uninstall Needs Attention",
                reason + " Reopen the UVSR Launcher file you originally downloaded; " +
                "it will safely resume the uninstall and keep your renderer settings and history.",
                null);
        }
        catch
        {
            // The helper's nonzero exit code and durable uninstall record still
            // preserve recovery if Windows cannot display this last-resort notice.
        }
    }

    private static void ShowLauncherError(
        string title,
        string message,
        IWin32Window? owner)
    {
        if (LauncherTypography.TryEnsureAvailable(out string fontFailure))
        {
            LauncherDialog.ShowError(owner, title, message,
                new DialogAction("ok", "OK", Primary: true, Cancel: true));
            return;
        }

        MessageBox.Show(owner,
            fontFailure + Environment.NewLine + Environment.NewLine + message,
            title,
            MessageBoxButtons.OK,
            MessageBoxIcon.Error);
    }
}
