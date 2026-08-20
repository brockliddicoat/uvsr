namespace UvsrInstaller;

internal sealed class InstallLog
{
    private const long MaximumLogBytes = 8L * 1024 * 1024;
    private const int MaximumRetainedLogs = 5;
    private readonly object _gate = new();
    private readonly string _path;
    private readonly Action<string>? _observer;
    private bool _truncated;

    internal InstallLog(string logsDirectory, Action<string>? observer = null)
    {
        SafePaths.RejectReparsePathChain(logsDirectory, "installer log directory");
        Directory.CreateDirectory(logsDirectory);
        SafePaths.RejectReparsePathChain(logsDirectory, "installer log directory");
        foreach (FileInfo stale in new DirectoryInfo(logsDirectory)
                     .EnumerateFiles("uvsr-*.log")
                     .OrderByDescending(file => file.LastWriteTimeUtc)
                     .Skip(MaximumRetainedLogs - 1))
        {
            try { stale.Delete(); }
            catch (IOException) { }
        }
        string file = $"uvsr-launcher-{DateTime.UtcNow:yyyyMMdd-HHmmss}-{Guid.NewGuid():N}.log";
        _path = System.IO.Path.Combine(logsDirectory, file);
        _observer = observer;
    }

    internal string Path => _path;

    internal void Write(string message)
    {
        string clean = Sanitize(message);
        string line = $"{DateTimeOffset.Now:O} {clean}";
        lock (_gate)
        {
            SafePaths.RejectReparsePathChain(_path, "installer log file");
            if (_truncated)
                return;
            if (File.Exists(_path) && new FileInfo(_path).Length >= MaximumLogBytes)
            {
                clean = "UVSR Launcher details reached the 8 MB safety limit; additional child output was omitted.";
                line = $"{DateTimeOffset.Now:O} {clean}";
                _truncated = true;
            }
            File.AppendAllText(_path, line + Environment.NewLine);
            _observer?.Invoke(clean);
        }
    }

    internal void WriteProcessLine(string label, string line)
    {
        if (!string.IsNullOrWhiteSpace(line))
            Write($"[{label}] {line}");
    }

    internal static string DescribeException(Exception exception)
    {
        ArgumentNullException.ThrowIfNull(exception);
        Queue<Exception> pending = new();
        pending.Enqueue(exception);
        List<string> messages = new();
        HashSet<string> seen = new(StringComparer.Ordinal);
        while (pending.Count > 0 && messages.Count < 6)
        {
            Exception current = pending.Dequeue();
            if (current is AggregateException aggregate)
            {
                foreach (Exception inner in aggregate.InnerExceptions)
                    pending.Enqueue(inner);
                continue;
            }

            string message = current.Message.Replace('\r', ' ').Replace('\n', ' ').Trim();
            if (!string.IsNullOrWhiteSpace(message) && seen.Add(message))
                messages.Add(message);
            if (current.InnerException is not null)
                pending.Enqueue(current.InnerException);
        }
        string detail = string.Join(" Cause: ", messages);
        if (detail.Length > 1800)
            detail = detail[..1800] + "…";
        return string.IsNullOrWhiteSpace(detail)
            ? exception.GetType().Name
            : detail;
    }

    private static string Sanitize(string value)
    {
        string singleLine = value.Replace('\r', ' ').Replace('\n', ' ');
        return singleLine.Length <= 4000 ? singleLine : singleLine[..4000] + "…";
    }
}
