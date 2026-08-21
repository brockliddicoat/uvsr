namespace UvsrInstaller;

internal sealed class LauncherProgressBar : Control
{
    private const int MaximumValue = 100;
    private readonly System.Windows.Forms.Timer _animationTimer = new();
    private ProgressBarStyle _style = ProgressBarStyle.Blocks;
    private int _value;
    private int _marqueeAnimationSpeed;
    private int _animationOffset;

    internal LauncherProgressBar()
    {
        SetStyle(ControlStyles.UserPaint |
                 ControlStyles.AllPaintingInWmPaint |
                 ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw, true);
        AccessibleRole = AccessibleRole.ProgressBar;
        TabStop = false;
        _animationTimer.Tick += (_, _) => AdvanceMarquee();
    }

    [System.ComponentModel.DesignerSerializationVisibilityAttribute(
        System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal ProgressBarStyle Style
    {
        get => _style;
        set
        {
            if (value is not (ProgressBarStyle.Blocks or
                ProgressBarStyle.Continuous or ProgressBarStyle.Marquee))
                throw new ArgumentOutOfRangeException(nameof(value));
            if (_style == value)
                return;
            _style = value;
            _animationOffset = MarqueeSegmentWidth(ClientRectangle);
            UpdateAnimationState();
            NotifyValueChanged();
            Invalidate();
        }
    }

    [System.ComponentModel.DesignerSerializationVisibilityAttribute(
        System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal int Value
    {
        get => _value;
        set
        {
            int bounded = Math.Clamp(value, 0, MaximumValue);
            if (_value == bounded)
                return;
            _value = bounded;
            NotifyValueChanged();
            Invalidate();
        }
    }

    [System.ComponentModel.DesignerSerializationVisibilityAttribute(
        System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal int MarqueeAnimationSpeed
    {
        get => _marqueeAnimationSpeed;
        set
        {
            if (value < 0)
                throw new ArgumentOutOfRangeException(nameof(value));
            if (_marqueeAnimationSpeed == value)
                return;
            _marqueeAnimationSpeed = value;
            UpdateAnimationState();
        }
    }

    internal static Color FillColor => LauncherPalette.Accent;

    internal static Rectangle CalculateDeterminateFill(Rectangle bounds, int value)
    {
        if (bounds.Width <= 0 || bounds.Height <= 0)
            return Rectangle.Empty;
        int bounded = Math.Clamp(value, 0, MaximumValue);
        if (bounded == 0)
            return Rectangle.Empty;
        int width = (int)Math.Round(bounds.Width * (bounded / (double)MaximumValue),
            MidpointRounding.AwayFromZero);
        return new Rectangle(bounds.Left, bounds.Top,
            Math.Clamp(width, 0, bounds.Width), bounds.Height);
    }

    internal static Rectangle CalculateMarqueeFill(Rectangle bounds, int animationOffset)
    {
        if (bounds.Width <= 0 || bounds.Height <= 0)
            return Rectangle.Empty;
        int segmentWidth = MarqueeSegmentWidth(bounds);
        int travel = bounds.Width + segmentWidth;
        int normalized = ((animationOffset % travel) + travel) % travel;
        Rectangle segment = new(
            bounds.Left + normalized - segmentWidth,
            bounds.Top,
            segmentWidth,
            bounds.Height);
        return Rectangle.Intersect(bounds, segment);
    }

    protected override AccessibleObject CreateAccessibilityInstance() =>
        new LauncherProgressAccessibleObject(this);

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        Rectangle bounds = ClientRectangle;
        if (bounds.Width <= 0 || bounds.Height <= 0)
            return;

        Color trackColor = SystemInformation.HighContrast
            ? SystemColors.Control
            : LauncherPalette.Border;
        using SolidBrush track = new(trackColor);
        using SolidBrush fill = new(FillColor);
        e.Graphics.FillRectangle(track, bounds);
        Rectangle filled = _style == ProgressBarStyle.Marquee
            ? CalculateMarqueeFill(bounds, _animationOffset)
            : CalculateDeterminateFill(bounds, _value);
        if (!filled.IsEmpty)
            e.Graphics.FillRectangle(fill, filled);
    }

    protected override void OnVisibleChanged(EventArgs e)
    {
        base.OnVisibleChanged(e);
        UpdateAnimationState();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
            _animationTimer.Dispose();
        base.Dispose(disposing);
    }

    private static int MarqueeSegmentWidth(Rectangle bounds) =>
        bounds.Width <= 0
            ? 1
            : Math.Min(bounds.Width, Math.Max(16, bounds.Width / 4));

    private void AdvanceMarquee()
    {
        int segmentWidth = MarqueeSegmentWidth(ClientRectangle);
        int travel = Math.Max(1, ClientRectangle.Width + segmentWidth);
        int step = Math.Max(1, LauncherUi.ScaleLogical(this, 3));
        _animationOffset = (_animationOffset + step) % travel;
        Invalidate();
    }

    private void UpdateAnimationState()
    {
        bool animate = _style == ProgressBarStyle.Marquee &&
                       _marqueeAnimationSpeed > 0 && Visible;
        if (animate)
            _animationTimer.Interval = Math.Clamp(_marqueeAnimationSpeed, 15, 1000);
        _animationTimer.Enabled = animate;
    }

    private void NotifyValueChanged()
    {
        if (IsHandleCreated)
            AccessibilityNotifyClients(AccessibleEvents.ValueChange, -1);
    }

    private sealed class LauncherProgressAccessibleObject(
        LauncherProgressBar owner) : ControlAccessibleObject(owner)
    {
        public override AccessibleRole Role => AccessibleRole.ProgressBar;

        public override string? Value => owner.Style == ProgressBarStyle.Marquee
            ? "In progress"
            : $"{owner.Value} percent";
    }
}
