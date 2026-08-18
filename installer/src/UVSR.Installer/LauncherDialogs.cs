using System.Drawing.Drawing2D;

namespace UvsrInstaller;

internal sealed record DialogAction(
    string Id,
    string Text,
    bool Primary = false,
    bool Cancel = false,
    bool Destructive = false);

internal static class LauncherDialog
{
    internal static string Show(
        IWin32Window? owner,
        string title,
        string message,
        params DialogAction[] actions)
    {
        if (actions.Length == 0)
            actions = new[] { new DialogAction("ok", "OK", Primary: true, Cancel: true) };
        using StyledDialog dialog = new(title, message, actions);
        dialog.ShowDialog(owner);
        return dialog.SelectedAction ?? actions.FirstOrDefault(action => action.Cancel)?.Id
            ?? actions[^1].Id;
    }

    private sealed class StyledDialog : Form
    {
        internal StyledDialog(string title, string message, IReadOnlyList<DialogAction> actions)
        {
            Text = title;
            StartPosition = FormStartPosition.CenterParent;
            ShowInTaskbar = false;
            MinimizeBox = false;
            MaximizeBox = false;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            AutoScaleDimensions = new SizeF(96F, 96F);
            AutoScaleMode = AutoScaleMode.Dpi;
            Font = new Font("Segoe UI", 10F);
            BackColor = LauncherPalette.Window;
            ClientSize = new Size(540, 230);
            MinimumSize = new Size(480, 220);

            TableLayoutPanel layout = new()
            {
                Dock = DockStyle.Fill,
                Padding = new Padding(24),
                ColumnCount = 1,
                RowCount = 3
            };
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            Label heading = new()
            {
                AutoSize = true,
                Dock = DockStyle.Top,
                Text = title,
                Font = new Font("Segoe UI Semibold", 16F, FontStyle.Bold),
                ForeColor = LauncherPalette.Text,
                Margin = new Padding(0, 0, 0, 12)
            };
            Label body = new()
            {
                AutoSize = true,
                Dock = DockStyle.Top,
                Text = message,
                ForeColor = LauncherPalette.Muted,
                Margin = new Padding(0, 0, 0, 20)
            };
            Panel bodyViewport = new()
            {
                Dock = DockStyle.Fill,
                AutoScroll = true,
                Margin = Padding.Empty
            };
            bodyViewport.Controls.Add(body);
            FlowLayoutPanel buttons = new()
            {
                AutoSize = true,
                Dock = DockStyle.Fill,
                FlowDirection = FlowDirection.RightToLeft,
                WrapContents = true,
                Margin = Padding.Empty
            };
            foreach (DialogAction action in actions.Reverse())
            {
                Button button = LauncherUi.CreateButton(action.Text,
                    action.Primary, action.Destructive);
                button.Tag = action.Id;
                button.Click += (_, _) =>
                {
                    SelectedAction = (string)button.Tag;
                    DialogResult = action.Cancel ? DialogResult.Cancel : DialogResult.OK;
                    Close();
                };
                buttons.Controls.Add(button);
                if (action.Primary)
                    AcceptButton = button;
                if (action.Cancel)
                    CancelButton = button;
            }
            layout.Controls.Add(heading, 0, 0);
            layout.Controls.Add(bodyViewport, 0, 1);
            layout.Controls.Add(buttons, 0, 2);
            Controls.Add(layout);
            Shown += (_, _) =>
            {
                LauncherUi.SizeToWorkingArea(this, new Size(540, 230),
                    new Size(480, 220));
                Rectangle available = LauncherUi.AvailableWorkingBounds(this);
                int chrome = Math.Max(0, Height - ClientSize.Height);
                int maximumClient = Math.Max(1, available.Height - chrome);
                int textWidth = Math.Max(1,
                    ClientSize.Width - layout.Padding.Horizontal);
                heading.MaximumSize = new Size(textWidth, 0);
                body.MaximumSize = new Size(textWidth, 0);
                PerformLayout();
                int desiredClientHeight = layout.Padding.Vertical +
                    heading.PreferredHeight + heading.Margin.Vertical +
                    body.PreferredHeight + body.Margin.Vertical +
                    buttons.PreferredSize.Height + buttons.Margin.Vertical;
                int minimumClient = Math.Min(
                    LauncherUi.ScaleLogical(this, 230), maximumClient);
                ClientSize = new Size(ClientSize.Width,
                    Math.Clamp(desiredClientHeight, minimumClient, maximumClient));
                LauncherUi.ConstrainToWorkingArea(this, new Size(480, 220));
            };
        }

        internal string? SelectedAction { get; private set; }
    }
}

internal sealed record UpdateSelection(
    bool Accepted,
    bool Retry,
    bool UpdateUvsr,
    bool UpdateLauncher);

internal sealed class UpdateSelectionDialog : Form
{
    private readonly CheckBox _uvsr = new();
    private readonly CheckBox _launcher = new();
    private readonly Button _update;

    internal UpdateSelectionDialog(UpdateCheckResult result)
    {
        Text = "Choose What to Update";
        StartPosition = FormStartPosition.CenterParent;
        ShowInTaskbar = false;
        MinimizeBox = false;
        MaximizeBox = false;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        AutoScaleDimensions = new SizeF(96F, 96F);
        AutoScaleMode = AutoScaleMode.Dpi;
        Font = new Font("Segoe UI", 10F);
        BackColor = LauncherPalette.Window;
        ClientSize = new Size(620, 390);
        MinimumSize = new Size(540, 360);

        TableLayoutPanel layout = new()
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(24),
            ColumnCount = 1,
            RowCount = 4
        };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        Label title = new()
        {
            AutoSize = true,
            Dock = DockStyle.Top,
            Text = "Choose What to Update",
            Font = new Font("Segoe UI Semibold", 17F, FontStyle.Bold),
            ForeColor = LauncherPalette.Text,
            Margin = new Padding(0, 0, 0, 6)
        };
        Label intro = new()
        {
            AutoSize = true,
            Dock = DockStyle.Top,
            Text = "Available updates are selected for you. You can install either one or both.",
            ForeColor = LauncherPalette.Muted,
            Margin = new Padding(0, 0, 0, 16)
        };
        TableLayoutPanel components = new()
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            ColumnCount = 1,
            RowCount = 2,
            Margin = Padding.Empty,
            Padding = Padding.Empty
        };
        components.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        components.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        components.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        components.Controls.Add(CreateComponentCard(_uvsr, "UVSR", result.Uvsr), 0, 0);
        components.Controls.Add(CreateComponentCard(
            _launcher, "UVSR Launcher", result.Launcher), 0, 1);
        Panel componentViewport = new()
        {
            Dock = DockStyle.Fill,
            AutoScroll = true,
            Margin = Padding.Empty
        };
        componentViewport.Controls.Add(components);

        bool hasFailure = result.Uvsr.State == ComponentUpdateState.CheckFailed ||
                          result.Launcher.State == ComponentUpdateState.CheckFailed;
        FlowLayoutPanel buttons = new()
        {
            AutoSize = true,
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.RightToLeft,
            WrapContents = true,
            Margin = Padding.Empty
        };
        Button cancel = LauncherUi.CreateButton("Cancel");
        cancel.Click += (_, _) => { Selection = new(false, false, false, false); Close(); };
        CancelButton = cancel;
        _update = LauncherUi.CreateButton("Update Selected", primary: true);
        _update.Click += (_, _) =>
        {
            Selection = new(true, false, _uvsr.Checked, _launcher.Checked);
            Close();
        };
        AcceptButton = _update;
        buttons.Controls.Add(cancel);
        buttons.Controls.Add(_update);
        if (hasFailure)
        {
            Button retry = LauncherUi.CreateButton("Check Again");
            retry.Click += (_, _) => { Selection = new(false, true, false, false); Close(); };
            buttons.Controls.Add(retry);
        }
        _uvsr.CheckedChanged += (_, _) => UpdateButtonState();
        _launcher.CheckedChanged += (_, _) => UpdateButtonState();
        UpdateButtonState();

        layout.Controls.Add(title, 0, 0);
        layout.Controls.Add(intro, 0, 1);
        layout.Controls.Add(componentViewport, 0, 2);
        layout.Controls.Add(buttons, 0, 3);
        layout.SizeChanged += (_, _) =>
        {
            int width = Math.Max(1,
                layout.ClientSize.Width - layout.Padding.Horizontal);
            title.MaximumSize = new Size(width, 0);
            intro.MaximumSize = new Size(width, 0);
        };
        Controls.Add(layout);
        Shown += (_, _) => LauncherUi.SizeToWorkingArea(this,
            new Size(620, 390), new Size(540, 360));
    }

    internal UpdateSelection Selection { get; private set; } = new(false, false, false, false);

    private static Control CreateComponentCard(
        CheckBox checkBox,
        string name,
        ComponentUpdateStatus status)
    {
        bool selectable = status.State is ComponentUpdateState.UpdateAvailable or
            ComponentUpdateState.RepairNeeded;
        checkBox.Text = name;
        checkBox.Checked = selectable;
        checkBox.Enabled = selectable;
        checkBox.AutoSize = true;
        checkBox.Font = new Font("Segoe UI Semibold", 11F, FontStyle.Bold);
        checkBox.ForeColor = LauncherPalette.Text;
        checkBox.AccessibleName = $"Select {name} update";
        checkBox.AccessibleDescription = status.Detail;

        Label detail = new()
        {
            AutoSize = true,
            Dock = DockStyle.Top,
            Text = status.Detail,
            ForeColor = status.State == ComponentUpdateState.CheckFailed
                ? LauncherPalette.Warning
                : LauncherPalette.Muted,
            Margin = new Padding(26, 2, 0, 0)
        };
        TableLayoutPanel card = new()
        {
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            Dock = DockStyle.Top,
            Padding = new Padding(14, 12, 14, 12),
            Margin = new Padding(0, 0, 0, 10),
            BackColor = LauncherPalette.Card,
            ColumnCount = 1,
            RowCount = 2
        };
        card.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        card.Controls.Add(checkBox, 0, 0);
        card.Controls.Add(detail, 0, 1);
        card.SizeChanged += (_, _) => detail.MaximumSize = new Size(
            Math.Max(1, card.ClientSize.Width - card.Padding.Horizontal - detail.Margin.Left), 0);
        return card;
    }

    private void UpdateButtonState() =>
        _update.Enabled = _uvsr.Checked || _launcher.Checked;
}

internal static class LauncherPalette
{
    internal static readonly Color Window = SystemInformation.HighContrast
        ? SystemColors.Window : Color.FromArgb(245, 247, 250);
    internal static readonly Color Card = SystemInformation.HighContrast
        ? SystemColors.Window : Color.FromArgb(252, 253, 255);
    internal static readonly Color Text = SystemInformation.HighContrast
        ? SystemColors.WindowText : Color.FromArgb(20, 27, 38);
    internal static readonly Color Muted = SystemInformation.HighContrast
        ? SystemColors.GrayText : Color.FromArgb(79, 89, 104);
    internal static readonly Color Accent = SystemInformation.HighContrast
        ? SystemColors.Highlight : Color.FromArgb(38, 99, 235);
    internal static readonly Color AccentHover = SystemInformation.HighContrast
        ? SystemColors.HotTrack : Color.FromArgb(29, 78, 216);
    internal static readonly Color Border = SystemInformation.HighContrast
        ? SystemColors.WindowText : Color.FromArgb(218, 224, 232);
    internal static readonly Color Success = SystemInformation.HighContrast
        ? SystemColors.WindowText : Color.FromArgb(31, 122, 82);
    internal static readonly Color Warning = SystemInformation.HighContrast
        ? SystemColors.WindowText : Color.FromArgb(173, 91, 28);
    internal static readonly Color Danger = SystemInformation.HighContrast
        ? SystemColors.WindowText : Color.FromArgb(185, 48, 48);
}

internal readonly record struct LauncherWindowLayout(
    Rectangle AvailableBounds,
    Size MinimumOuterSize,
    Size OuterSize);

internal static class LauncherUi
{
    private const int LogicalDpi = 96;
    private const int LogicalWorkingAreaMargin = 16;

    internal static int ScaleLogical(Control control, int logicalValue) =>
        ScaleLogical(logicalValue, control.DeviceDpi);

    internal static int ScaleLogical(int logicalValue, int dpi)
    {
        if (logicalValue < 0)
            throw new ArgumentOutOfRangeException(nameof(logicalValue));
        if (dpi <= 0)
            throw new ArgumentOutOfRangeException(nameof(dpi));
        double scaled = logicalValue * (double)dpi / LogicalDpi;
        return scaled >= int.MaxValue
            ? int.MaxValue
            : (int)Math.Round(scaled, MidpointRounding.AwayFromZero);
    }

    internal static Rectangle CalculateAvailableWorkingBounds(
        Rectangle workingArea,
        int dpi)
    {
        if (workingArea.Width <= 0 || workingArea.Height <= 0)
            throw new ArgumentOutOfRangeException(nameof(workingArea));
        int maximumMargin = Math.Max(0,
            (Math.Min(workingArea.Width, workingArea.Height) - 1) / 2);
        int margin = Math.Min(
            ScaleLogical(LogicalWorkingAreaMargin, dpi), maximumMargin);
        return new Rectangle(
            workingArea.Left + margin,
            workingArea.Top + margin,
            Math.Max(1, workingArea.Width - (2 * margin)),
            Math.Max(1, workingArea.Height - (2 * margin)));
    }

    internal static LauncherWindowLayout CalculateWindowLayout(
        Rectangle workingArea,
        int dpi,
        Size requestedOuterSize,
        Size logicalMinimumOuterSize)
    {
        if (requestedOuterSize.Width < 0 || requestedOuterSize.Height < 0)
            throw new ArgumentOutOfRangeException(nameof(requestedOuterSize));
        if (logicalMinimumOuterSize.Width < 0 || logicalMinimumOuterSize.Height < 0)
            throw new ArgumentOutOfRangeException(nameof(logicalMinimumOuterSize));
        Rectangle available = CalculateAvailableWorkingBounds(workingArea, dpi);
        Size minimum = new(
            Math.Min(available.Width,
                Math.Max(1, ScaleLogical(logicalMinimumOuterSize.Width, dpi))),
            Math.Min(available.Height,
                Math.Max(1, ScaleLogical(logicalMinimumOuterSize.Height, dpi))));
        Size bounded = new(
            Math.Clamp(Math.Max(1, requestedOuterSize.Width),
                minimum.Width, available.Width),
            Math.Clamp(Math.Max(1, requestedOuterSize.Height),
                minimum.Height, available.Height));
        return new LauncherWindowLayout(available, minimum, bounded);
    }

    internal static Rectangle AvailableWorkingBounds(Control control) =>
        CalculateAvailableWorkingBounds(
            Screen.FromControl(control).WorkingArea, control.DeviceDpi);

    internal static void SizeToWorkingArea(
        Form form,
        Size logicalPreferredClientSize,
        Size logicalMinimumOuterSize)
    {
        int chromeWidth = Math.Max(0, form.Width - form.ClientSize.Width);
        int chromeHeight = Math.Max(0, form.Height - form.ClientSize.Height);
        Size requested = new(
            ScaleLogical(form, logicalPreferredClientSize.Width) + chromeWidth,
            ScaleLogical(form, logicalPreferredClientSize.Height) + chromeHeight);
        ApplyWindowLayout(form, CalculateWindowLayout(
            Screen.FromControl(form).WorkingArea, form.DeviceDpi,
            requested, logicalMinimumOuterSize));
    }

    internal static void ConstrainToWorkingArea(
        Form form,
        Size logicalMinimumOuterSize) =>
        ApplyWindowLayout(form, CalculateWindowLayout(
            Screen.FromControl(form).WorkingArea, form.DeviceDpi,
            form.Size, logicalMinimumOuterSize));

    private static void ApplyWindowLayout(Form form, LauncherWindowLayout layout)
    {
        Point center = new(form.Left + (form.Width / 2),
            form.Top + (form.Height / 2));
        form.MinimumSize = Size.Empty;
        form.MaximumSize = layout.AvailableBounds.Size;
        form.MinimumSize = layout.MinimumOuterSize;
        form.Size = layout.OuterSize;
        int left = Math.Clamp(center.X - (form.Width / 2),
            layout.AvailableBounds.Left,
            layout.AvailableBounds.Right - form.Width);
        int top = Math.Clamp(center.Y - (form.Height / 2),
            layout.AvailableBounds.Top,
            layout.AvailableBounds.Bottom - form.Height);
        form.Location = new Point(left, top);
    }

    internal static Button CreateButton(
        string text,
        bool primary = false,
        bool destructive = false)
    {
        Button button = new()
        {
            Text = text,
            AutoSize = true,
            MinimumSize = new Size(96, 42),
            Padding = new Padding(14, 7, 14, 7),
            Margin = new Padding(8, 0, 0, 0),
            FlatStyle = FlatStyle.Flat,
            Cursor = Cursors.Hand,
            UseVisualStyleBackColor = false
        };
        if (SystemInformation.HighContrast)
        {
            button.FlatStyle = FlatStyle.Standard;
            button.UseVisualStyleBackColor = true;
            button.ForeColor = destructive ? SystemColors.HotTrack : SystemColors.ControlText;
            return button;
        }
        button.FlatAppearance.BorderSize = 1;
        if (primary)
        {
            button.BackColor = LauncherPalette.Accent;
            button.ForeColor = Color.White;
            button.FlatAppearance.BorderColor = LauncherPalette.Accent;
            button.FlatAppearance.MouseOverBackColor = LauncherPalette.AccentHover;
        }
        else
        {
            button.BackColor = LauncherPalette.Card;
            button.ForeColor = destructive ? LauncherPalette.Danger : LauncherPalette.Text;
            button.FlatAppearance.BorderColor = LauncherPalette.Border;
            button.FlatAppearance.MouseOverBackColor = Color.FromArgb(238, 242, 247);
        }
        return button;
    }
}

internal sealed class LauncherCard : Panel
{
    internal LauncherCard()
    {
        DoubleBuffered = true;
        BackColor = LauncherPalette.Card;
        Padding = new Padding(16);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        using Pen border = new(LauncherPalette.Border);
        Rectangle bounds = ClientRectangle;
        bounds.Width -= 1;
        bounds.Height -= 1;
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        e.Graphics.DrawRectangle(border, bounds);
    }
}
