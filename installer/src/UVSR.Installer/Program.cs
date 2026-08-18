namespace UvsrInstaller;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length == 3 &&
            args[0].Equals("--launcher-health-check", StringComparison.OrdinalIgnoreCase))
        {
            return long.TryParse(args[1], out long sequence) &&
                   sequence == ProductConstants.LauncherReleaseSequence &&
                   string.Equals(args[2], ProductConstants.LauncherVersion,
                       StringComparison.Ordinal)
                ? 0
                : 3;
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
                LauncherDialog.Show(null, "UVSR Launcher",
                    "UVSR Launcher must run as your normal Windows user so downloaded source is never built with administrator rights. Close this copy, then open it normally (do not choose Run as administrator).",
                    new DialogAction("ok", "OK", Primary: true, Cancel: true));
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
            Application.Run(new MainForm(engine, uninstall, continuationId));
            return 0;
        }
        catch (InstallerException ex)
        {
            LauncherDialog.Show(null, "UVSR Launcher", ex.Message,
                new DialogAction("ok", "OK", Primary: true, Cancel: true));
            return 1;
        }
        catch (Exception ex)
        {
            LauncherDialog.Show(null, "UVSR Launcher",
                "UVSR Launcher stopped before making a change. " + ex.Message,
                new DialogAction("ok", "OK", Primary: true, Cancel: true));
            return 2;
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
            LauncherDialog.Show(null, "UVSR Uninstall Needs Attention",
                reason + " Reopen the UVSR Launcher file you originally downloaded; " +
                "it will safely resume the uninstall and keep your renderer settings and history.",
                new DialogAction("ok", "OK", Primary: true, Cancel: true));
        }
        catch
        {
            // The helper's nonzero exit code and durable uninstall record still
            // preserve recovery if Windows cannot display this last-resort notice.
        }
    }
}
