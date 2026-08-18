using System.Collections.Concurrent;
using System.Diagnostics;
using System.Drawing;

namespace UvsrInstaller;

internal enum UpdateFlowExit
{
    UpToDate,
    SelectionCancelled,
    NoSelection,
    OperationCancelled
}

internal readonly record struct TerminalUiState(
    string Phase,
    string Detail,
    int Progress,
    ProgressBarStyle ProgressStyle = ProgressBarStyle.Blocks);

internal sealed class MainForm : Form
{
    private const int MaximumDisplayedLogCharacters = 200_000;
    private static readonly TimeSpan UvsrLaunchPollTimeout = TimeSpan.FromSeconds(6);
    private static readonly TimeSpan UvsrClosePollTimeout = TimeSpan.FromSeconds(3);
    private static readonly int UvsrPollDelayMs = 150;
    private static readonly Color InstalledLaunchColor = Color.FromArgb(24, 114, 78);
    private static readonly Color InstalledLaunchHover = Color.FromArgb(21, 93, 64);
    private static readonly Color CloseLaunchColor = Color.FromArgb(186, 35, 35);
    private static readonly Color CloseLaunchHover = Color.FromArgb(158, 28, 28);
    private static readonly Color DisabledLaunchColor = Color.FromArgb(232, 236, 241);

    private readonly InstallerEngine _engine;
    private readonly bool _startWithUninstall;
    private readonly Guid? _requestedContinuationId;
    private readonly Label _status = new();
    private readonly Label _phase = new();
    private readonly Label _detail = new();
    private readonly CheckBox _desktopShortcut = new();
    private readonly Button _install;
    private readonly Button _update;
    private readonly Button _uninstall;
    private readonly Button _launch;
    private readonly Button _notices;
    private readonly Button _detailsToggle;
    private readonly Button _copyDetails;
    private readonly ProgressBar _progress = new();
    private readonly RichTextBox _log = new();
    private readonly LauncherCard _detailsCard = new();
    private readonly ConcurrentQueue<string> _pendingLog = new();
    private readonly System.Windows.Forms.Timer _logTimer = new() { Interval = 150 };
    private CancellationTokenSource? _operationCancellation;
    private InstallSnapshot? _snapshot;
    private bool _operationRunning;
    private bool _launchBusy;
    private bool _allowClose;
    private bool _canCancel = true;
    private bool _detailsVisible;
    private bool _launcherWindowObsolete;
    private int? _collapsedClientHeight;

    internal MainForm(
        InstallerEngine engine,
        bool startWithUninstall,
        Guid? requestedContinuationId)
    {
        _engine = engine;
        _startWithUninstall = startWithUninstall;
        _requestedContinuationId = requestedContinuationId;
        _install = LauncherUi.CreateButton("Install", primary: true);
        _update = LauncherUi.CreateButton("Update");
        _uninstall = LauncherUi.CreateButton("Uninstall", destructive: true);
        _launch = LauncherUi.CreateButton("Launch");
        _notices = LauncherUi.CreateButton("Notices");
        _detailsToggle = LauncherUi.CreateButton("Details");
        _copyDetails = LauncherUi.CreateButton("Copy Details");

        Text = "UVSR Launcher";
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new Size(700, 520);
        ClientSize = new Size(840, 570);
        BackColor = LauncherPalette.Window;
        Font = new Font("Segoe UI", 10F);
        AutoScaleDimensions = new SizeF(96F, 96F);
        AutoScaleMode = AutoScaleMode.Dpi;
        BuildInterface();
        Shown += OnShown;
        FormClosing += OnFormClosing;
        DpiChanged += (_, _) =>
        {
            if (!IsDisposed && IsHandleCreated)
                BeginInvoke(new Action(() =>
                    LauncherUi.ConstrainToWorkingArea(this, new Size(700, 520))));
        };
        _logTimer.Tick += (_, _) => FlushPendingLog();
        _logTimer.Start();
    }

    private void BuildInterface()
    {
        TableLayoutPanel layout = new()
        {
            Dock = DockStyle.Fill,
            AutoScroll = true,
            Padding = new Padding(28, 24, 28, 24),
            ColumnCount = 1,
            RowCount = 8
        };
        for (int row = 0; row < 7; row++)
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        Label title = new()
        {
            AutoSize = true,
            Text = "UVSR Launcher",
            Font = new Font("Segoe UI Semibold", 25F, FontStyle.Bold),
            ForeColor = LauncherPalette.Text,
            Margin = new Padding(0, 0, 0, 4),
            AccessibleName = "UVSR Launcher"
        };
        Label introduction = new()
        {
            AutoSize = true,
            Dock = DockStyle.Top,
            Text = "Install, launch, and keep UVSR up to date on Windows 11. " +
                   "The launcher handles downloads and setup for you.",
            ForeColor = LauncherPalette.Muted,
            Margin = new Padding(0, 0, 0, 18)
        };

        LauncherCard statusCard = new()
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            Margin = new Padding(0, 0, 0, 14),
            Padding = new Padding(16, 13, 16, 13)
        };
        _status.AutoSize = true;
        _status.Dock = DockStyle.Top;
        _status.Font = new Font("Segoe UI Semibold", 11.5F, FontStyle.Bold);
        _status.ForeColor = LauncherPalette.Success;
        _status.Text = "Checking this PC...";
        _status.AccessibleName = "Installation status";
        statusCard.Controls.Add(_status);

        _desktopShortcut.AutoSize = true;
        _desktopShortcut.Text = "Create a desktop shortcut for UVSR Launcher";
        _desktopShortcut.Checked = true;
        _desktopShortcut.Margin = new Padding(2, 0, 0, 14);
        _desktopShortcut.AccessibleName = "Create a desktop shortcut for UVSR Launcher";

        FlowLayoutPanel buttons = new()
        {
            AutoSize = true,
            Dock = DockStyle.Top,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = true,
            Margin = new Padding(0, 0, 0, 18)
        };
        _install.Margin = new Padding(0, 0, 8, 8);
        foreach (Button button in new[] { _launch, _update, _uninstall, _notices })
            button.Margin = new Padding(0, 0, 8, 8);
        _install.Click += async (_, _) => await HandleInstallAsync();
        _update.Click += async (_, _) => await RunUpdateFlowAsync();
        _launch.Click += async (_, _) => await LaunchAsync();
        _uninstall.Click += async (_, _) => await RunOperationAsync(
            InstallerOperation.Uninstall, confirm: true);
        _notices.Click += (_, _) => ShowNotices();
        buttons.Controls.AddRange(new Control[]
            { _install, _launch, _update, _uninstall, _notices });

        _phase.AutoSize = true;
        _phase.Dock = DockStyle.Top;
        _phase.Font = new Font("Segoe UI Semibold", 10F, FontStyle.Bold);
        _phase.ForeColor = LauncherPalette.Text;
        _phase.Text = "Ready";
        _phase.Margin = new Padding(0, 0, 0, 4);
        _phase.AccessibleName = "Current step";
        _detail.AutoSize = true;
        _detail.Dock = DockStyle.Top;
        _detail.ForeColor = LauncherPalette.Muted;
        _detail.Text = "Choose an action above.";
        _detail.Margin = new Padding(0, 0, 0, 8);
        _detail.AccessibleName = "Current step details";
        _detail.AccessibleRole = AccessibleRole.StatusBar;
        _detail.LiveSetting = System.Windows.Forms.Automation.AutomationLiveSetting.Polite;
        _progress.Dock = DockStyle.Top;
        _progress.Height = 7;
        _progress.Style = ProgressBarStyle.Blocks;
        _progress.Margin = new Padding(0, 0, 0, 12);
        _progress.AccessibleName = "Operation progress";

        FlowLayoutPanel detailsHeader = new()
        {
            AutoSize = true,
            Dock = DockStyle.Top,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = true,
            Margin = new Padding(0, 0, 0, 8)
        };
        _detailsToggle.MinimumSize = new Size(112, 36);
        _detailsToggle.Padding = new Padding(10, 4, 10, 4);
        _detailsToggle.Margin = Padding.Empty;
        _detailsToggle.Click += (_, _) => SetDetailsVisible(!_detailsVisible);
        _detailsToggle.AccessibleName = "Show operation details";
        _copyDetails.MinimumSize = new Size(108, 36);
        _copyDetails.Padding = new Padding(10, 4, 10, 4);
        _copyDetails.Margin = new Padding(8, 0, 0, 0);
        _copyDetails.Visible = false;
        _copyDetails.Click += (_, _) => CopyDetails();
        detailsHeader.Controls.Add(_detailsToggle);
        detailsHeader.Controls.Add(_copyDetails);

        _detailsCard.Dock = DockStyle.Fill;
        _detailsCard.Margin = Padding.Empty;
        _detailsCard.Padding = new Padding(12);
        _detailsCard.MinimumSize = new Size(0, 180);
        _detailsCard.Visible = false;
        _log.Dock = DockStyle.Fill;
        _log.ReadOnly = true;
        _log.BackColor = LauncherPalette.Card;
        _log.ForeColor = LauncherPalette.Muted;
        _log.BorderStyle = BorderStyle.None;
        _log.Font = new Font("Segoe UI", 9F);
        _log.DetectUrls = false;
        _log.WordWrap = true;
        _log.AccessibleName = "Operation details";
        _detailsCard.Controls.Add(_log);

        layout.Controls.Add(title, 0, 0);
        layout.Controls.Add(introduction, 0, 1);
        layout.Controls.Add(statusCard, 0, 2);
        layout.Controls.Add(_desktopShortcut, 0, 3);
        layout.Controls.Add(buttons, 0, 4);
        TableLayoutPanel progressLayout = new()
        {
            AutoSize = true,
            Dock = DockStyle.Top,
            ColumnCount = 1,
            RowCount = 3,
            Margin = Padding.Empty
        };
        progressLayout.Controls.Add(_phase, 0, 0);
        progressLayout.Controls.Add(_detail, 0, 1);
        progressLayout.Controls.Add(_progress, 0, 2);
        layout.Controls.Add(progressLayout, 0, 5);
        layout.Controls.Add(detailsHeader, 0, 6);
        layout.Controls.Add(_detailsCard, 0, 7);
        layout.SizeChanged += (_, _) =>
        {
            int width = Math.Max(200, layout.ClientSize.Width - layout.Padding.Horizontal);
            introduction.MaximumSize = new Size(width, 0);
            _status.MaximumSize = new Size(width - statusCard.Padding.Horizontal, 0);
            _detail.MaximumSize = new Size(width, 0);
        };
        Controls.Add(layout);
    }

    private async void OnShown(object? sender, EventArgs e)
    {
        LauncherUi.SizeToWorkingArea(this, new Size(840, 570), new Size(700, 520));
        RefreshSnapshot();
        if (_startWithUninstall)
        {
            await RunOperationAsync(InstallerOperation.Uninstall, confirm: true);
            return;
        }

        if (_snapshot?.IsInitialized == true)
        {
            BeginOperation("Preparing UVSR Launcher",
                "Checking the installed launcher and shortcuts.");
            try
            {
                Progress<InstallerProgress> progress = new(UpdateProgress);
                await Task.Run(() => _engine.EnsureLauncherReadyAsync(
                    _desktopShortcut.Checked, progress, AppendLog,
                    _operationCancellation!.Token));
            }
            catch (Exception ex)
            {
                ShowError(ex);
            }
            finally
            {
                EndOperation();
            }
        }

        Guid? pendingContinuation = null;
        try
        {
            pendingContinuation = _engine.FindPendingLauncherContinuation(
                _requestedContinuationId);
        }
        catch (Exception ex)
        {
            ShowError(ex);
        }
        if (pendingContinuation is Guid continuationId)
        {
            InstallerOperation operation = _snapshot?.IsDamaged == true
                ? InstallerOperation.Reinstall
                : InstallerOperation.Update;
            if (await RunOperationAsync(operation, confirm: false))
            {
                try
                {
                    _engine.CompleteLauncherContinuation(continuationId);
                }
                catch (Exception ex)
                {
                    ShowError(ex);
                }
            }
        }
    }

    private void RefreshSnapshot()
    {
        try
        {
            _snapshot = _engine.Inspect();
            _status.Text = _snapshot.Summary;
            _status.ForeColor = _snapshot.IsDamaged
                ? LauncherPalette.Warning
                : _snapshot.IsInstalled
                    ? LauncherPalette.Success
                    : LauncherPalette.Muted;
            _desktopShortcut.Checked = _engine.GetDesktopShortcutPreference(_snapshot);
            SetButtons(_snapshot);
        }
        catch (Exception ex)
        {
            _snapshot = null;
            _status.Text = FriendlyMessage(ex);
            _status.ForeColor = LauncherPalette.Danger;
            SetButtons(null);
        }
    }

    private void SetButtons(InstallSnapshot? snapshot)
    {
        (bool install, bool update, bool uninstall, bool launch, bool options) =
            DetermineActionAvailability(snapshot, _operationRunning);
        bool isUvsrRunning = IsUvsrRunning(snapshot);
        if (_launcherWindowObsolete)
        {
            install = false;
            update = false;
            uninstall = false;
            launch = false;
            options = !_operationRunning;
        }
        _install.Enabled = install;
        _update.Enabled = update;
        _uninstall.Enabled = uninstall;
        _launch.Enabled = launch && !_launchBusy;
        _notices.Enabled = options;
        _desktopShortcut.Enabled = options && !_launcherWindowObsolete;
        ApplyLaunchButtonStyle(isUvsrRunning, launch);
    }

    internal static (bool Install, bool Update, bool Uninstall, bool Launch, bool Options)
        DetermineActionAvailability(InstallSnapshot? snapshot, bool operationRunning)
    {
        bool available = !operationRunning && snapshot is not null;
        return (available, available,
            available && snapshot!.IsInitialized,
            available && snapshot!.IsInstalled && !snapshot.IsDamaged,
            !operationRunning);
    }

    private async Task HandleInstallAsync()
    {
        if (_operationRunning)
            return;
        RefreshSnapshot();
        if (_snapshot is null)
            return;
        if (_snapshot.IsInstalled && !_snapshot.IsDamaged)
        {
            string choice = LauncherDialog.Show(this, "UVSR Is Already Installed",
                "UVSR is ready to use. You can launch it now or rebuild the latest version.",
                new DialogAction("launch", "Launch", Primary: true),
                new DialogAction("reinstall", "Reinstall"),
                new DialogAction("cancel", "Cancel", Cancel: true));
            if (choice == "launch")
                await LaunchAsync();
            else if (choice == "reinstall")
                await RunOperationAsync(InstallerOperation.Reinstall, confirm: false);
            return;
        }
        if (_snapshot.IsDamaged)
        {
            string choice = LauncherDialog.Show(this, "UVSR Needs Repair",
                "Some installed files are missing or changed. Reinstall UVSR to restore it. " +
                "Your settings and history will be kept.",
                new DialogAction("reinstall", "Reinstall", Primary: true),
                new DialogAction("cancel", "Cancel", Cancel: true));
            if (choice == "reinstall")
                await RunOperationAsync(InstallerOperation.Reinstall, confirm: false);
            return;
        }

        string install = LauncherDialog.Show(this, "Install UVSR",
            "UVSR Launcher will download what this PC needs, build the latest UVSR version, " +
            "and keep the installation ready to update. The first install can take several minutes.",
            new DialogAction("install", "Install", Primary: true),
            new DialogAction("cancel", "Cancel", Cancel: true));
        if (install == "install")
            await RunOperationAsync(InstallerOperation.Install, confirm: false);
    }

    private async Task RunUpdateFlowAsync()
    {
        if (_operationRunning)
            return;
        BeginOperation("Checking for updates", "Checking UVSR and UVSR Launcher.");
        try
        {
            UpdateCheckResult result;
            while (true)
            {
                Progress<InstallerProgress> progress = new(UpdateProgress);
                result = await Task.Run(() => _engine.CheckForUpdatesAsync(
                    _desktopShortcut.Checked, progress, AppendLog,
                    _operationCancellation!.Token));
                bool selectable = IsSelectable(result.Uvsr) || IsSelectable(result.Launcher);
                bool failed = result.Uvsr.State == ComponentUpdateState.CheckFailed ||
                              result.Launcher.State == ComponentUpdateState.CheckFailed;
                if (!selectable && !failed)
                {
                    string message = result.Uvsr.State == ComponentUpdateState.NotInstalled
                        ? "UVSR Launcher is up to date. UVSR is not installed yet."
                        : "UVSR and UVSR Launcher are up to date.";
                    ShowTerminalStatus(DetermineUpdateTerminalState(
                        UpdateFlowExit.UpToDate, message));
                    LauncherDialog.Show(this, "Everything Is Up to Date", message,
                        new DialogAction("ok", "OK", Primary: true, Cancel: true));
                    return;
                }
                using UpdateSelectionDialog picker = new(result);
                picker.ShowDialog(this);
                UpdateSelection selection = picker.Selection;
                if (selection.Retry)
                    continue;
                if (!selection.Accepted)
                {
                    ShowTerminalStatus(DetermineUpdateTerminalState(
                        UpdateFlowExit.SelectionCancelled));
                    return;
                }

                if (selection.UpdateLauncher)
                {
                    CancellationToken token = _operationCancellation?.Token
                        ?? throw new InstallerException("The update operation was not active.");
                    LauncherFeed feed = result.Launcher.LauncherFeed
                        ?? throw new InstallerException(
                            "The selected launcher update no longer had a valid release record.");
                    OperationResult updated = await Task.Run(() => _engine.UpdateLauncherAsync(
                        feed, _desktopShortcut.Checked, selection.UpdateUvsr,
                        progress, AppendLog, token));
                    HandleOperationResult(updated);
                    return;
                }
                if (selection.UpdateUvsr)
                {
                    CancellationToken token = _operationCancellation?.Token
                        ?? throw new InstallerException("The update operation was not active.");
                    InstallerOperation operation = result.Uvsr.State == ComponentUpdateState.RepairNeeded
                        ? InstallerOperation.Reinstall
                        : InstallerOperation.Update;
                    OperationResult updated = await Task.Run(() => _engine.ExecuteAsync(
                        operation, _desktopShortcut.Checked, PromptAsync, progress,
                        AppendLog, token));
                    HandleOperationResult(updated);
                    return;
                }
                ShowTerminalStatus(DetermineUpdateTerminalState(
                    UpdateFlowExit.NoSelection));
                return;
            }
        }
        catch (Exception ex) when (WasCancelled(ex))
        {
            ShowTerminalStatus(DetermineUpdateTerminalState(
                UpdateFlowExit.OperationCancelled));
        }
        catch (Exception ex)
        {
            ShowError(ex);
        }
        finally
        {
            EndOperation();
        }
    }

    private async Task<bool> RunOperationAsync(InstallerOperation operation, bool confirm)
    {
        if (_operationRunning)
            return false;
        if (confirm && !ConfirmOperation(operation))
            return false;
        BeginOperation(operation == InstallerOperation.Uninstall ? "Preparing uninstall" : "Getting UVSR ready",
            "This can take a few minutes. You can keep using this window while it works.");
        try
        {
            Progress<InstallerProgress> progress = new(UpdateProgress);
            OperationResult result = await Task.Run(() => _engine.ExecuteAsync(operation,
                _desktopShortcut.Checked, PromptAsync, progress, AppendLog,
                _operationCancellation!.Token));
            HandleOperationResult(result);
            return true;
        }
        catch (Exception ex)
        {
            ShowError(ex);
            return false;
        }
        finally
        {
            EndOperation();
        }
    }

    private void HandleOperationResult(OperationResult result)
    {
        ShowTerminalStatus("Complete", result.Message, 100);
        if (result.CleanupScheduled)
        {
            // The uninstall helper is already waiting for this exact process.
            // Close immediately so no success dialog creates a coordination race.
            _allowClose = true;
            Close();
            return;
        }
        if (result.RelaunchLauncherPath is not null)
        {
            RelaunchUpdatedLauncher(result);
            return;
        }
        LauncherDialog.Show(this, "Complete", result.Message,
            new DialogAction("ok", "OK", Primary: true, Cancel: true));
    }

    private void RelaunchUpdatedLauncher(OperationResult result)
    {
        // Launcher activation is already committed. This process must never
        // resume mutating actions, even when Windows cannot start the new copy.
        _launcherWindowObsolete = true;
        SetButtons(_snapshot);
        while (true)
        {
            ShowTerminalStatus("UVSR Launcher Updated",
                "Opening the updated UVSR Launcher.", 100);
            try
            {
                string launcher = result.RelaunchLauncherPath
                    ?? throw new InstallerException(
                        "The updated UVSR Launcher path was unavailable.");
                ProcessStartInfo start = new()
                {
                    FileName = launcher,
                    WorkingDirectory = Path.GetDirectoryName(launcher)
                        ?? throw new InstallerException(
                            "Windows could not locate the updated UVSR Launcher folder."),
                    UseShellExecute = true
                };
                if (result.ContinueUvsrUpdate &&
                    result.LauncherContinuationId is Guid continuationId)
                {
                    start.ArgumentList.Add("--continue-uvsr-update");
                    start.ArgumentList.Add(continuationId.ToString("D"));
                }
                _ = Process.Start(start) ?? throw new InstallerException(
                    "Windows could not open the updated UVSR Launcher.");
                _allowClose = true;
                Close();
                return;
            }
            catch (Exception ex)
            {
                AppendLog($"The updated launcher could not be opened: {ex.Message}");
                const string guidance =
                    "The update is installed, but Windows could not open it automatically. " +
                    "This window can no longer make changes. Choose Retry, or close this " +
                    "window and open UVSR Launcher from the Start menu.";
                ShowTerminalStatus("Open the Updated Launcher", guidance, 100);
                string choice = LauncherDialog.Show(this, "Open Updated UVSR Launcher",
                    guidance,
                    new DialogAction("retry", "Retry", Primary: true),
                    new DialogAction("close", "Close", Cancel: true));
                if (choice == "retry")
                    continue;
                _allowClose = true;
                Close();
                return;
            }
        }
    }

    private void BeginOperation(string phase, string detail)
    {
        _operationRunning = true;
        _operationCancellation = new CancellationTokenSource();
        _canCancel = true;
        _log.Clear();
        while (_pendingLog.TryDequeue(out _)) { }
        SetProgressStatus(phase, detail);
        _progress.Style = ProgressBarStyle.Marquee;
        _progress.MarqueeAnimationSpeed = 28;
        SetButtons(_snapshot);
    }

    private void EndOperation()
    {
        if (_progress.Style == ProgressBarStyle.Marquee)
        {
            ShowTerminalStatus("Ready",
                "The operation finished without making a change.", 0);
        }
        _operationCancellation?.Dispose();
        _operationCancellation = null;
        _operationRunning = false;
        _canCancel = true;
        if (!IsDisposed)
            RefreshSnapshot();
    }

    private bool ConfirmOperation(InstallerOperation operation)
    {
        if (operation != InstallerOperation.Uninstall)
            return true;
        return LauncherDialog.Show(this, "Uninstall UVSR",
            "Remove UVSR Launcher, installed UVSR program files, shortcuts, and its Windows entry? " +
            "Your renderer settings and history will be kept. Shared Microsoft components will also stay installed.",
            new DialogAction("uninstall", "Uninstall", Destructive: true),
            new DialogAction("cancel", "Cancel", Primary: true, Cancel: true)) == "uninstall";
    }

    private Task<bool> PromptAsync(PromptRequest request)
    {
        TaskCompletionSource<bool> completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
        void ShowPrompt()
        {
            string result = LauncherDialog.Show(this, request.Title, request.Message,
                new DialogAction("yes", "Continue", Primary: true),
                new DialogAction("no", "Cancel", Cancel: true));
            completion.SetResult(result == "yes");
        }
        if (InvokeRequired)
            BeginInvoke(ShowPrompt);
        else
            ShowPrompt();
        return completion.Task;
    }

    private void UpdateProgress(InstallerProgress update)
    {
        if (IsDisposed || !_operationRunning)
            return;
        SetProgressStatus(update.Phase, update.Detail);
        _canCancel = update.CanCancel;
        if (update.Percent.HasValue)
        {
            _progress.Style = ProgressBarStyle.Blocks;
            _progress.MarqueeAnimationSpeed = 0;
            _progress.Value = Math.Clamp(update.Percent.Value, 0, 100);
        }
        else
        {
            _progress.Style = ProgressBarStyle.Marquee;
            _progress.MarqueeAnimationSpeed = 28;
        }
    }

    private void AppendLog(string line)
    {
        if (!IsDisposed)
            _pendingLog.Enqueue(line);
    }

    private void FlushPendingLog()
    {
        if (IsDisposed || _pendingLog.IsEmpty)
            return;
        int lines = 0;
        System.Text.StringBuilder batch = new();
        while (lines < 200 && _pendingLog.TryDequeue(out string? line))
        {
            batch.AppendLine(line);
            lines++;
        }
        if (batch.Length == 0)
            return;
        _log.AppendText(batch.ToString());
        if (_log.TextLength > MaximumDisplayedLogCharacters)
        {
            int remove = _log.TextLength - MaximumDisplayedLogCharacters;
            int newline = _log.Text.IndexOf('\n', remove);
            _log.Select(0, newline >= 0 ? newline + 1 : remove);
            _log.SelectedText = string.Empty;
        }
        _log.SelectionStart = _log.TextLength;
        _log.ScrollToCaret();
    }

    private void SetDetailsVisible(bool visible)
    {
        if (visible == _detailsVisible)
            return;
        if (visible)
            _collapsedClientHeight = ClientSize.Height;
        _detailsVisible = visible;
        _detailsCard.Visible = visible;
        _copyDetails.Visible = visible;
        _detailsToggle.Text = "Details";
        _detailsToggle.AccessibleName = visible
            ? "Hide operation details"
            : "Show operation details";
        Rectangle available = LauncherUi.AvailableWorkingBounds(this);
        int chrome = Math.Max(0, Height - ClientSize.Height);
        int maximumClientHeight = Math.Max(1, available.Height - chrome);
        if (visible)
        {
            int desired = Math.Min(LauncherUi.ScaleLogical(this, 720), maximumClientHeight);
            if (ClientSize.Height < desired)
                ClientSize = new Size(ClientSize.Width, desired);
        }
        else if (_collapsedClientHeight is int collapsed)
        {
            ClientSize = new Size(ClientSize.Width,
                Math.Min(collapsed, maximumClientHeight));
            _collapsedClientHeight = null;
        }
        LauncherUi.ConstrainToWorkingArea(this, new Size(700, 520));
    }

    private void CopyDetails()
    {
        while (!_pendingLog.IsEmpty)
            FlushPendingLog();
        if (string.IsNullOrWhiteSpace(_log.Text))
            return;
        try
        {
            Clipboard.SetText(_log.Text);
        }
        catch (Exception ex) when (ex is System.Runtime.InteropServices.ExternalException or ThreadStateException)
        {
            LauncherDialog.Show(this, "Copy Details",
                "Windows could not access the clipboard right now. Try again in a moment.",
                new DialogAction("ok", "OK", Primary: true, Cancel: true));
        }
    }

    private async Task LaunchAsync()
    {
        if (_operationRunning || _launchBusy)
            return;
        RefreshSnapshot();
        if (_snapshot is null || !_snapshot.IsInstalled || _snapshot.State is null)
            return;
        string? executablePath = _snapshot.ExecutablePath;
        if (string.IsNullOrWhiteSpace(executablePath))
        {
            ShowError(new InstallerException(
                "The installed UVSR executable path is unavailable."));
            return;
        }
        if (IsUvsrRunning(executablePath))
        {
            await CloseUvsrAsync(executablePath);
            return;
        }
        await LaunchUvsrAsync(executablePath);
    }

    private void SetLaunchBusy(bool launching)
    {
        _launchBusy = launching;
        UseWaitCursor = launching;
        Cursor = launching ? Cursors.WaitCursor : Cursors.Default;
        if (!IsDisposed)
            SetButtons(_snapshot);
    }

    private async Task LaunchUvsrAsync(string executablePath)
    {
        SetLaunchBusy(true);
        try
        {
            await Task.Run(_engine.LaunchInstalled);
            if (!await WaitForUvsrStateAsync(executablePath, true))
            {
                ShowError(new InstallerException(
                    "UVSR was launched, but the process did not appear to start."));
            }
        }
        catch (Exception ex)
        {
            ShowError(ex);
        }
        finally
        {
            SetLaunchBusy(false);
            RefreshSnapshot();
        }
    }

    private async Task CloseUvsrAsync(string executablePath)
    {
        SetLaunchBusy(true);
        try
        {
            IReadOnlyList<int> running = ProcessInspector.FindProcessesByExecutable(executablePath);
            foreach (int processId in running)
            {
                try
                {
                    using Process process = Process.GetProcessById(processId);
                    if (!process.HasExited)
                        process.CloseMainWindow();
                }
                catch (ArgumentException) { }
                catch (InvalidOperationException) { }
            }

            if (!await WaitForUvsrStateAsync(executablePath, false))
            {
                foreach (int processId in ProcessInspector.FindProcessesByExecutable(executablePath))
                {
                    try
                    {
                        using Process process = Process.GetProcessById(processId);
                        if (!process.HasExited)
                            process.Kill(entireProcessTree: true);
                    }
                    catch (ArgumentException) { }
                    catch (InvalidOperationException) { }
                    catch (System.ComponentModel.Win32Exception) { }
                }

                if (!await WaitForUvsrStateAsync(executablePath, false))
                {
                    ShowError(new InstallerException(
                        "UVSR was requested to close, but still appears to be running."));
                }
            }
        }
        catch (Exception ex)
        {
            ShowError(ex);
        }
        finally
        {
            SetLaunchBusy(false);
            RefreshSnapshot();
        }
    }

    private async Task<bool> WaitForUvsrStateAsync(string executablePath, bool runningExpected)
    {
        TimeSpan timeout = runningExpected ? UvsrLaunchPollTimeout : UvsrClosePollTimeout;
        DateTimeOffset end = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < end)
        {
            if (IsUvsrRunning(executablePath) == runningExpected)
                return true;
            await Task.Delay(UvsrPollDelayMs);
        }
        return IsUvsrRunning(executablePath) == runningExpected;
    }

    private static bool IsUvsrRunning(string? executablePath) =>
        !string.IsNullOrWhiteSpace(executablePath) &&
        ProcessInspector.FindProcessesByExecutable(executablePath!).Count > 0;

    private bool IsUvsrRunning(InstallSnapshot? snapshot) =>
        IsUvsrRunning(snapshot?.ExecutablePath);

    private void ApplyLaunchButtonStyle(bool isRunning, bool launchAvailable)
    {
        if (SystemInformation.HighContrast)
        {
            _launch.FlatStyle = FlatStyle.Standard;
            _launch.UseVisualStyleBackColor = true;
            _launch.BackColor = DefaultBackColor;
            _launch.ForeColor = DefaultForeColor;
            _launch.FlatAppearance.BorderSize = 1;
            _launch.Text = _launchBusy ? "Launching" : isRunning ? "Close" : "Launch";
            _launch.Enabled = launchAvailable && !_launchBusy;
            return;
        }

        _launch.FlatStyle = FlatStyle.Flat;
        _launch.UseVisualStyleBackColor = false;
        _launch.FlatAppearance.BorderSize = 1;
        if (_launchBusy || !launchAvailable)
        {
            _launch.BackColor = DisabledLaunchColor;
            _launch.ForeColor = LauncherPalette.Muted;
            _launch.FlatAppearance.BorderColor = LauncherPalette.Border;
            _launch.FlatAppearance.MouseOverBackColor = DisabledLaunchColor;
            _launch.Text = _launchBusy ? "Launching" : "Launch";
            return;
        }

        if (isRunning)
        {
            _launch.BackColor = CloseLaunchColor;
            _launch.ForeColor = Color.White;
            _launch.FlatAppearance.BorderColor = CloseLaunchColor;
            _launch.FlatAppearance.MouseOverBackColor = CloseLaunchHover;
            _launch.Text = "Close";
        }
        else
        {
            _launch.BackColor = InstalledLaunchColor;
            _launch.ForeColor = Color.White;
            _launch.FlatAppearance.BorderColor = InstalledLaunchColor;
            _launch.FlatAppearance.MouseOverBackColor = InstalledLaunchHover;
            _launch.Text = "Launch";
        }
    }

    private void ShowNotices()
    {
        try
        {
            string text =
                "UVSR LICENSE\r\n\r\n" + ReadNotice("UVSR.Installer.Notices.UvsrLicense.md") +
                "\r\n\r\n.NET LICENSE\r\n\r\n" + ReadNotice("UVSR.Installer.Notices.DotNetLicense.txt") +
                "\r\n\r\n.NET THIRD-PARTY NOTICES\r\n\r\n" +
                ReadNotice("UVSR.Installer.Notices.DotNetThirdParty.txt");
            using Form noticeWindow = new()
            {
                Text = "UVSR Launcher - Notices",
                StartPosition = FormStartPosition.CenterParent,
                ClientSize = new Size(900, 700),
                MinimumSize = new Size(650, 450),
                Font = Font,
                BackColor = LauncherPalette.Window,
                AutoScaleDimensions = new SizeF(96F, 96F),
                AutoScaleMode = AutoScaleMode.Dpi
            };
            noticeWindow.Shown += (_, _) => LauncherUi.SizeToWorkingArea(
                noticeWindow, new Size(900, 700), new Size(650, 450));
            RichTextBox noticeText = new()
            {
                Dock = DockStyle.Fill,
                Margin = new Padding(20),
                ReadOnly = true,
                BackColor = LauncherPalette.Card,
                ForeColor = LauncherPalette.Text,
                BorderStyle = BorderStyle.None,
                Font = new Font("Segoe UI", 9F),
                Text = text,
                WordWrap = true
            };
            LauncherCard card = new()
            {
                Dock = DockStyle.Fill,
                Margin = new Padding(20),
                Padding = new Padding(16)
            };
            card.Controls.Add(noticeText);
            noticeWindow.Padding = new Padding(20);
            noticeWindow.Controls.Add(card);
            noticeWindow.ShowDialog(this);
        }
        catch (Exception ex)
        {
            ShowError(ex);
        }
    }

    private static string ReadNotice(string resourceName)
    {
        using Stream stream = typeof(MainForm).Assembly.GetManifestResourceStream(resourceName)
            ?? throw new InstallerException("The launcher notice bundle is incomplete.");
        using StreamReader reader = new(stream);
        return reader.ReadToEnd();
    }

    private void ShowError(Exception exception)
    {
        string message = FriendlyMessage(exception);
        ShowTerminalStatus(
            exception is RebootRequiredException ? "Restart Required" : "Stopped Safely",
            message, 0);
        SetDetailsVisible(true);
        LauncherDialog.Show(this,
            exception is RebootRequiredException ? "Restart Required" : "UVSR Launcher Stopped",
            message,
            new DialogAction("ok", "OK", Primary: true, Cancel: true));
    }

    private void ShowTerminalStatus(string phase, string detail, int progress)
        => ShowTerminalStatus(new TerminalUiState(phase, detail, progress));

    private void ShowTerminalStatus(TerminalUiState status)
    {
        SetProgressStatus(status.Phase, status.Detail);
        _progress.Style = status.ProgressStyle;
        _progress.MarqueeAnimationSpeed = status.ProgressStyle == ProgressBarStyle.Marquee
            ? 28
            : 0;
        if (status.ProgressStyle != ProgressBarStyle.Marquee)
            _progress.Value = Math.Clamp(status.Progress, 0, 100);
    }

    internal static TerminalUiState DetermineUpdateTerminalState(
        UpdateFlowExit exit,
        string? upToDateDetail = null) => exit switch
        {
            UpdateFlowExit.UpToDate => new TerminalUiState("Up to Date",
                upToDateDetail ?? "UVSR and UVSR Launcher are up to date.", 100),
            UpdateFlowExit.SelectionCancelled => new TerminalUiState("Cancelled",
                "The update selection was cancelled. Nothing installed was changed.", 0),
            UpdateFlowExit.NoSelection => new TerminalUiState("Ready",
                "No updates were selected. Nothing installed was changed.", 0),
            UpdateFlowExit.OperationCancelled => new TerminalUiState("Cancelled",
                "The update check was cancelled. Nothing installed was changed.", 0),
            _ => throw new ArgumentOutOfRangeException(nameof(exit), exit, null)
        };

    private void SetProgressStatus(string phase, string detail)
    {
        bool changed = !string.Equals(_phase.Text, phase, StringComparison.Ordinal) ||
                       !string.Equals(_detail.Text, detail, StringComparison.Ordinal);
        _phase.Text = phase;
        _detail.Text = detail;
        _phase.AccessibleName = phase;
        _detail.AccessibleName = $"{phase}. {detail}";
        if (changed && _detail.IsHandleCreated && !IsDisposed)
            _detail.AccessibilityObject.RaiseLiveRegionChanged();
    }

    private static string FriendlyMessage(Exception exception) => exception is InstallerException
        ? exception.Message
        : "UVSR Launcher stopped before making a change. " + exception.Message;

    private static bool IsSelectable(ComponentUpdateStatus status) =>
        status.State is ComponentUpdateState.UpdateAvailable or ComponentUpdateState.RepairNeeded;

    private static bool WasCancelled(Exception exception) =>
        exception is OperationCanceledException ||
        exception.GetBaseException() is OperationCanceledException;

    private void OnFormClosing(object? sender, FormClosingEventArgs e)
    {
        if (_launcherWindowObsolete)
            return;
        if (!_operationRunning || _allowClose)
            return;
        if (!_canCancel)
        {
            LauncherDialog.Show(this, "Setup Is Finishing",
                "Microsoft setup is running with administrator approval and must report its result before this window can close.",
                new DialogAction("ok", "OK", Primary: true, Cancel: true));
            e.Cancel = true;
            return;
        }
        string result = LauncherDialog.Show(this, "Stop This Operation?",
            "The active UVSR version will be preserved. Any verified partial download can resume next time.",
            new DialogAction("stop", "Stop", Destructive: true),
            new DialogAction("continue", "Keep Working", Primary: true, Cancel: true));
        if (result == "stop")
            _operationCancellation?.Cancel();
        e.Cancel = true;
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _logTimer.Stop();
            _logTimer.Dispose();
            _operationCancellation?.Dispose();
        }
        base.Dispose(disposing);
    }
}
