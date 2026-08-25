using System.Buffers;
using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Runtime.ExceptionServices;
using System.Security.Authentication;
using System.Security.Cryptography;

namespace UvsrInstaller;

internal sealed record DownloadPolicy(
    int MaximumAttempts,
    TimeSpan HeaderTimeout,
    TimeSpan InactivityTimeout,
    TimeSpan OverallTimeout,
    IReadOnlyList<TimeSpan> RetryDelays)
{
    internal static DownloadPolicy Default { get; } = new(
        6,
        TimeSpan.FromSeconds(45),
        TimeSpan.FromSeconds(90),
        TimeSpan.FromMinutes(45),
        new[]
        {
            TimeSpan.FromSeconds(1),
            TimeSpan.FromSeconds(2),
            TimeSpan.FromSeconds(4),
            TimeSpan.FromSeconds(8),
            TimeSpan.FromSeconds(16)
        });
}

internal sealed record PartialDownloadRecord(
    int SchemaVersion,
    string Source,
    string ExpectedSha256,
    string EntityTag,
    long CompleteLength);

internal sealed class DownloadManager : IDisposable
{
    private const int BufferSize = 128 * 1024;
    private const int MaximumSegments = 32;
    private static readonly HashSet<HttpStatusCode> TransientStatusCodes = new()
    {
        HttpStatusCode.RequestTimeout,
        (HttpStatusCode)425,
        (HttpStatusCode)429,
        HttpStatusCode.InternalServerError,
        HttpStatusCode.BadGateway,
        HttpStatusCode.ServiceUnavailable,
        HttpStatusCode.GatewayTimeout
    };

    private readonly HttpClient _client;
    private readonly DownloadPolicy _policy;

    internal DownloadManager(
        HttpMessageHandler? messageHandler = null,
        DownloadPolicy? policy = null)
    {
        HttpMessageHandler handler = messageHandler ?? new HttpClientHandler
        {
            // Redirects are followed manually so every hop, not merely the
            // final address, is proven to remain on HTTPS.
            AllowAutoRedirect = false,
            // Byte offsets must describe the exact representation when a strong
            // ETag permits a resumed Range request.
            AutomaticDecompression = DecompressionMethods.None
        };
        _client = new HttpClient(handler)
        {
            // Header, inactivity, and whole-operation deadlines are enforced
            // independently below. HttpClient.Timeout does not cover streamed
            // bodies when ResponseHeadersRead is used.
            Timeout = Timeout.InfiniteTimeSpan
        };
        _client.DefaultRequestHeaders.UserAgent.ParseAdd("uvsr-launcher/1.1");
        _policy = policy ?? DownloadPolicy.Default;
        if (_policy.MaximumAttempts < 1 || _policy.RetryDelays.Count < _policy.MaximumAttempts - 1)
            throw new ArgumentOutOfRangeException(nameof(policy));
    }

    internal async Task DownloadAndVerifyAsync(
        Uri uri,
        string destination,
        string? expectedSha256,
        long maximumBytes,
        IProgress<InstallerProgress>? progress,
        InstallLog log,
        CancellationToken cancellationToken,
        Action<string>? validateStaged = null,
        string phase = "Downloading files")
    {
        ValidateRequest(uri, destination, expectedSha256, maximumBytes);
        string destinationParent = Path.GetDirectoryName(destination)!;
        SafePaths.RejectReparsePathChain(destinationParent, "download directory");
        Directory.CreateDirectory(destinationParent);
        SafePaths.RejectReparsePathChain(destinationParent, "download directory");

        string partial = destination + ".part";
        string partialRecord = destination + ".part.json";
        PartialDownloadRecord? resume = LoadPartialRecord(
            uri, expectedSha256, maximumBytes, partial, partialRecord, log);
        Stopwatch elapsed = Stopwatch.StartNew();
        Exception? lastTransient = null;
        int segments = 0;
        int attemptsMade = 0;
        bool deadlineExpired = false;

        for (int attempt = 1; attempt <= _policy.MaximumAttempts; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (elapsed.Elapsed >= _policy.OverallTimeout)
            {
                deadlineExpired = true;
                break;
            }
            attemptsMade = attempt;

            long existing = resume is not null && File.Exists(partial)
                ? new FileInfo(partial).Length
                : 0;
            try
            {
                if (resume is not null && existing > 0)
                    progress?.Report(new InstallerProgress(phase,
                        $"Resuming {Path.GetFileName(destination)} - {FormatBytes(existing)} already received."));

                using CancellationTokenSource headerTimeout =
                    CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                headerTimeout.CancelAfter(Remaining(_policy.HeaderTimeout, elapsed));
                HttpResponseMessage response;
                try
                {
                    response = await SendWithSafeRedirectsAsync(uri, resume,
                        existing, headerTimeout.Token);
                }
                catch (OperationCanceledException ex) when (!cancellationToken.IsCancellationRequested)
                {
                    throw new TransientDownloadException(
                        "The download service did not respond in time.", ex, stalled: true);
                }
                catch (HttpRequestException ex) when (IsTransient(ex))
                {
                    throw new TransientDownloadException(
                        "The connection was interrupted before the download began.", ex);
                }

                using (response)
                {
                    Uri finalUri = response.RequestMessage?.RequestUri
                        ?? throw new InstallerException("A download returned no final address.");
                    if (!finalUri.Scheme.Equals(Uri.UriSchemeHttps,
                            StringComparison.OrdinalIgnoreCase))
                        throw new InstallerException("A download redirected away from HTTPS.");

                    if (TransientStatusCodes.Contains(response.StatusCode))
                    {
                        TimeSpan? retryAfter = GetRetryAfter(response.Headers.RetryAfter);
                        throw new TransientDownloadException(
                            $"The download service returned HTTP {(int)response.StatusCode}.",
                            retryAfter: retryAfter);
                    }
                    if (!response.IsSuccessStatusCode && response.StatusCode != HttpStatusCode.RequestedRangeNotSatisfiable)
                        throw new InstallerException(
                            $"The download service returned HTTP {(int)response.StatusCode} ({response.ReasonPhrase}).");

                    DownloadResponsePlan plan = PlanResponse(response, resume, existing,
                        maximumBytes, partial, partialRecord, uri, expectedSha256);
                    resume = plan.Record;
                    if (plan.AlreadyComplete)
                    {
                        await FinalizeDownloadAsync(partial, partialRecord, destination,
                            expectedSha256, validateStaged, uri, log, cancellationToken);
                        return;
                    }

                    segments++;
                    if (segments > MaximumSegments)
                        throw new InstallerException(
                            "The download was interrupted too many times to resume safely.");

                    try
                    {
                        await ReceiveBodyAsync(response, plan, partial, destination,
                            maximumBytes, phase, progress, elapsed, cancellationToken);
                    }
                    catch (OperationCanceledException ex) when (!cancellationToken.IsCancellationRequested)
                    {
                        throw new TransientDownloadException(
                            "The connection stalled while waiting for more data.", ex, stalled: true);
                    }
                    catch (HttpRequestException ex) when (IsTransient(ex))
                    {
                        throw new TransientDownloadException(
                            "The connection was interrupted while downloading.", ex);
                    }
                    catch (IOException ex) when (IsLikelyNetworkRead(ex))
                    {
                        throw new TransientDownloadException(
                            "The connection was interrupted while downloading.", ex);
                    }

                    long received = new FileInfo(partial).Length;
                    if (plan.CompleteLength is long completeLength && received != completeLength)
                    {
                        if (received < completeLength)
                            throw new TransientDownloadException(
                                $"The download ended early at {received} of {completeLength} bytes.");
                        throw new InstallerException("A download exceeded its declared length.");
                    }

                    await FinalizeDownloadAsync(partial, partialRecord, destination,
                        expectedSha256, validateStaged, uri, log, cancellationToken);
                    return;
                }
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch (TransientDownloadException ex)
            {
                lastTransient = ex;
                long offset = File.Exists(partial) ? new FileInfo(partial).Length : 0;
                log.Write($"Transient download failure from {uri.Host}; attempt {attempt}/" +
                          $"{_policy.MaximumAttempts}, offset {offset}, {Describe(ex)}");
                if (resume is null)
                    DeletePartial(partial, partialRecord, log);
                if (attempt == _policy.MaximumAttempts || elapsed.Elapsed >= _policy.OverallTimeout)
                    break;
                TimeSpan delay = RetryDelay(attempt, ex.RetryAfter, elapsed);
                string state = ex.Stalled ? "Connection stalled" : "Connection interrupted";
                progress?.Report(new InstallerProgress(state,
                    $"Retrying automatically in {Math.Max(1, (int)Math.Ceiling(delay.TotalSeconds))} seconds " +
                    $"(attempt {attempt + 1} of {_policy.MaximumAttempts})."));
                await Task.Delay(delay, cancellationToken);
            }
            catch (InstallerException)
            {
                DeletePartial(partial, partialRecord, log);
                throw;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // Local writes, flushes, and promotion failures are not network
                // failures and must not be retried or described as connectivity.
                DeletePartial(partial, partialRecord, log);
                throw new InstallerException(
                    $"Windows could not save {Path.GetFileName(destination)}. " +
                    "Check free space and file access, then try again.", ex);
            }
            catch (HttpRequestException ex)
            {
                DeletePartial(partial, partialRecord, log);
                throw new InstallerException(
                    $"The secure download for {Path.GetFileName(destination)} was rejected. " +
                    "No installed files were changed.", ex);
            }
        }

        string limit = deadlineExpired
            ? $"its {FormatDuration(_policy.OverallTimeout)} safety deadline"
            : $"{attemptsMade} automatic attempt{(attemptsMade == 1 ? string.Empty : "s")}";
        throw new InstallerException(
            $"The download for {Path.GetFileName(destination)} remained unavailable after {limit}. " +
            "Nothing installed was changed. Try again in a moment.",
            lastTransient ?? new TimeoutException());
    }

    private async Task<HttpResponseMessage> SendWithSafeRedirectsAsync(
        Uri initialUri,
        PartialDownloadRecord? resume,
        long existing,
        CancellationToken cancellationToken)
    {
        Uri current = initialUri;
        for (int redirect = 0; redirect <= 8; redirect++)
        {
            using HttpRequestMessage request = new(HttpMethod.Get, current);
            if (resume is not null && existing > 0)
            {
                request.Headers.Range = new RangeHeaderValue(existing, null);
                request.Headers.IfRange = new RangeConditionHeaderValue(
                    EntityTagHeaderValue.Parse(resume.EntityTag));
            }
            HttpResponseMessage response = await _client.SendAsync(request,
                HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            if (!IsRedirect(response.StatusCode))
                return response;
            Uri? location = response.Headers.Location;
            if (location is null)
            {
                response.Dispose();
                throw new InstallerException("A download redirect had no destination.");
            }
            Uri next = location.IsAbsoluteUri ? location : new Uri(current, location);
            response.Dispose();
            if (!next.Scheme.Equals(Uri.UriSchemeHttps, StringComparison.OrdinalIgnoreCase) ||
                !string.IsNullOrEmpty(next.UserInfo))
                throw new InstallerException("A download redirect left secure HTTPS.");
            if (redirect == 8)
                throw new InstallerException("A download exceeded its redirect safety limit.");
            current = next;
        }
        throw new InstallerException("A download exceeded its redirect safety limit.");
    }

    private static bool IsRedirect(HttpStatusCode statusCode) => statusCode is
        HttpStatusCode.MovedPermanently or HttpStatusCode.Redirect or
        HttpStatusCode.SeeOther or HttpStatusCode.TemporaryRedirect or
        HttpStatusCode.PermanentRedirect;

    private DownloadResponsePlan PlanResponse(
        HttpResponseMessage response,
        PartialDownloadRecord? resume,
        long existing,
        long maximumBytes,
        string partial,
        string partialRecord,
        Uri uri,
        string? expectedSha256)
    {
        if (response.StatusCode == HttpStatusCode.RequestedRangeNotSatisfiable)
        {
            long? complete = response.Content.Headers.ContentRange?.Length;
            string? responseTag = StrongEntityTag(response.Headers.ETag);
            bool representationProven = expectedSha256 is not null ||
                string.Equals(responseTag, resume?.EntityTag, StringComparison.Ordinal);
            if (resume is not null && complete == existing &&
                existing == resume.CompleteLength && representationProven)
                return new DownloadResponsePlan(true, true, existing, complete, 0, resume);
            ResetPartialOrThrow(partial, partialRecord);
            throw new TransientDownloadException(
                "The saved partial download no longer matched the server and was restarted.");
        }

        bool append = response.StatusCode == HttpStatusCode.PartialContent;
        if (append)
        {
            if (resume is null || existing <= 0)
                throw new InstallerException("The server returned an unexpected partial download.");
            ContentRangeHeaderValue? range = response.Content.Headers.ContentRange;
            if (range is null || !range.HasRange || !range.HasLength ||
                range.From != existing || range.To is null || range.Length != resume.CompleteLength ||
                range.To.Value < range.From.Value)
                throw new InstallerException("The server returned an unsafe partial-download range.");
            long segmentLength = range.To.Value - range.From.Value + 1;
            if (response.Content.Headers.ContentLength is long declared && declared != segmentLength)
                throw new InstallerException("The partial download length did not match its byte range.");
            string? tag = StrongEntityTag(response.Headers.ETag);
            // If-Range with a strong validator permits a 206 response to omit a
            // repeated ETag. A different explicit validator is never safe to append.
            if (tag is not null && !string.Equals(tag, resume.EntityTag, StringComparison.Ordinal))
            {
                ResetPartialOrThrow(partial, partialRecord);
                throw new TransientDownloadException(
                    "The download changed while it was being resumed and was restarted.");
            }
            return new DownloadResponsePlan(false, true, existing,
                resume.CompleteLength, segmentLength, resume);
        }

        if (response.StatusCode != HttpStatusCode.OK)
            throw new InstallerException("The download service returned an unsupported response.");
        if (existing > 0)
            DeletePartial(partial, partialRecord, log: null);

        long? contentLength = response.Content.Headers.ContentLength;
        if (contentLength is > 0 && contentLength > maximumBytes)
            throw new InstallerException("A download was larger than its safety limit.");
        string? entityTag = StrongEntityTag(response.Headers.ETag);
        PartialDownloadRecord? record = entityTag is not null && contentLength is > 0
            ? new PartialDownloadRecord(1, CanonicalSource(uri), expectedSha256 ?? string.Empty,
                entityTag, contentLength.Value)
            : null;
        if (record is not null)
            JsonStore.WriteAtomic(partialRecord, record);
        else if (File.Exists(partialRecord))
            File.Delete(partialRecord);
        return new DownloadResponsePlan(false, false, 0, contentLength, contentLength, record);
    }

    private async Task ReceiveBodyAsync(
        HttpResponseMessage response,
        DownloadResponsePlan plan,
        string partial,
        string destination,
        long maximumBytes,
        string phase,
        IProgress<InstallerProgress>? progress,
        Stopwatch elapsed,
        CancellationToken cancellationToken)
    {
        await using Stream input = await response.Content.ReadAsStreamAsync(cancellationToken);
        FileMode mode = plan.Append ? FileMode.Open : FileMode.Create;
        FileStream output = OpenPartialForWrite(partial, mode);
        byte[] buffer = ArrayPool<byte>.Shared.Rent(BufferSize);
        long lastReportedBytes = -1;
        long lastReportTicks = 0;
        int? lastReportedPercent = -1;
        long segmentReceived = 0;
        Exception? failure = null;
        try
        {
            try
            {
                if (output.Length != plan.StartingOffset)
                    throw new InstallerException(
                        "The saved partial download changed before it could be resumed safely.");
                output.Position = plan.StartingOffset;
            }
            catch (IOException ex)
            {
                throw new LocalDownloadWriteException(ex);
            }

            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (elapsed.Elapsed >= _policy.OverallTimeout)
                    throw new OperationCanceledException("The download exceeded its overall deadline.");
                int read;
                using (CancellationTokenSource inactivity =
                       CancellationTokenSource.CreateLinkedTokenSource(cancellationToken))
                {
                    inactivity.CancelAfter(Remaining(_policy.InactivityTimeout, elapsed));
                    int requested = buffer.Length;
                    if (plan.ExpectedSegmentLength is long expected)
                    {
                        long remaining = expected - segmentReceived;
                        requested = remaining >= buffer.Length
                            ? buffer.Length
                            : (int)Math.Max(1, remaining + 1);
                    }
                    read = await input.ReadAsync(buffer.AsMemory(0, requested), inactivity.Token);
                }
                if (read == 0)
                {
                    if (plan.ExpectedSegmentLength is long expected && segmentReceived != expected)
                        throw new TransientDownloadException(
                            $"The download segment ended early at {segmentReceived} of {expected} bytes.");
                    break;
                }
                if (plan.ExpectedSegmentLength is long declaredSegment &&
                    segmentReceived + read > declaredSegment)
                    throw new InstallerException(
                        "A download segment exceeded its declared byte range.");
                long total = plan.StartingOffset + segmentReceived + read;
                if (total > maximumBytes)
                    throw new InstallerException("A download exceeded its safety limit.");
                try
                {
                    await output.WriteAsync(buffer.AsMemory(0, read), cancellationToken);
                }
                catch (IOException ex)
                {
                    throw new LocalDownloadWriteException(ex);
                }
                segmentReceived += read;

                int? percent = plan.CompleteLength is > 0
                    ? (int)Math.Clamp(total * 100L / plan.CompleteLength.Value, 0, 100)
                    : null;
                long now = Stopwatch.GetTimestamp();
                bool shouldReport = percent is not null
                    ? percent != lastReportedPercent
                    : lastReportedBytes < 0 || total - lastReportedBytes >= 1024 * 1024 ||
                      Stopwatch.GetElapsedTime(lastReportTicks, now) >= TimeSpan.FromMilliseconds(500);
                if (!shouldReport)
                    continue;
                progress?.Report(new InstallerProgress(phase,
                    $"{Path.GetFileName(destination)} - {FormatBytes(total)}", percent));
                lastReportedBytes = total;
                lastReportedPercent = percent;
                lastReportTicks = now;
            }
            try
            {
                await output.FlushAsync(cancellationToken);
            }
            catch (IOException ex)
            {
                throw new LocalDownloadWriteException(ex);
            }
        }
        catch (Exception ex)
        {
            failure = ex;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(buffer);
            try
            {
                await output.DisposeAsync();
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                failure = new LocalDownloadWriteException(ex);
            }
        }
        if (failure is not null)
            ExceptionDispatchInfo.Capture(failure).Throw();
    }

    private static FileStream OpenPartialForWrite(string path, FileMode mode)
    {
        try
        {
            return new FileStream(path, mode, FileAccess.Write, FileShare.None,
                BufferSize, FileOptions.Asynchronous | FileOptions.SequentialScan);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            throw new LocalDownloadWriteException(ex);
        }
    }

    private static async Task FinalizeDownloadAsync(
        string partial,
        string partialRecord,
        string destination,
        string? expectedSha256,
        Action<string>? validateStaged,
        Uri uri,
        InstallLog log,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        string actual = await ComputeSha256Async(partial, cancellationToken);
        if (expectedSha256 is not null &&
            !CryptographicOperations.FixedTimeEquals(
                Convert.FromHexString(expectedSha256), Convert.FromHexString(actual)))
            throw new InstallerException("A download failed its SHA-256 verification.");
        try
        {
            validateStaged?.Invoke(partial);
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception ex)
        {
            throw new InstallerException(
                "The downloaded file did not pass its Windows trust check.", ex);
        }
        SafePaths.RejectReparsePathChain(destination, "download destination");
        File.Move(partial, destination, overwrite: true);
        if (File.Exists(partialRecord))
            File.Delete(partialRecord);
        log.Write($"Downloaded {uri.Host}/{Path.GetFileName(uri.LocalPath)} " +
                  $"({new FileInfo(destination).Length} bytes, SHA-256 {actual}).");
    }

    private static PartialDownloadRecord? LoadPartialRecord(
        Uri uri,
        string? expectedSha256,
        long maximumBytes,
        string partial,
        string recordPath,
        InstallLog log)
    {
        if (!File.Exists(partial) && !File.Exists(recordPath))
            return null;
        try
        {
            SafePaths.RejectReparsePathChain(partial, "partial download");
            SafePaths.RejectReparsePathChain(recordPath, "partial download record");
            if (!File.Exists(partial) || !File.Exists(recordPath))
                throw new InstallerException("The saved partial download was incomplete.");
            PartialDownloadRecord record = JsonStore.Read<PartialDownloadRecord>(recordPath, 4096);
            FileInfo file = new(partial);
            if (record.SchemaVersion != 1 ||
                !string.Equals(record.Source, CanonicalSource(uri), StringComparison.Ordinal) ||
                !string.Equals(record.ExpectedSha256, expectedSha256 ?? string.Empty,
                    StringComparison.Ordinal) ||
                record.CompleteLength <= 0 || record.CompleteLength > maximumBytes ||
                file.Length < 0 || file.Length > record.CompleteLength ||
                !EntityTagHeaderValue.TryParse(record.EntityTag, out EntityTagHeaderValue? tag) ||
                tag.IsWeak)
                throw new InstallerException("The saved partial download did not match this request.");
            log.Write($"Found a verified partial-download record at {file.Length} of " +
                      $"{record.CompleteLength} bytes.");
            return record;
        }
        catch (InstallerException ex)
        {
            log.Write($"Discarded an unusable partial download: {ex.Message}");
            DeletePartial(partial, recordPath, log);
            return null;
        }
    }

    private static void ValidateRequest(
        Uri uri,
        string destination,
        string? expectedSha256,
        long maximumBytes)
    {
        if (!uri.IsAbsoluteUri || !uri.Scheme.Equals(Uri.UriSchemeHttps,
                StringComparison.OrdinalIgnoreCase) || !string.IsNullOrEmpty(uri.UserInfo))
            throw new InstallerException("A download did not use a safe HTTPS address.");
        if (string.IsNullOrWhiteSpace(destination) || Path.GetDirectoryName(destination) is null)
            throw new InstallerException("A download destination was invalid.");
        if (maximumBytes <= 0)
            throw new InstallerException("A download safety limit was invalid.");
        if (expectedSha256 is not null && !ProductConstants.HashRegex().IsMatch(expectedSha256))
            throw new InstallerException("A download SHA-256 value was invalid.");
    }

    private TimeSpan RetryDelay(int completedAttempt, TimeSpan? retryAfter, Stopwatch elapsed)
    {
        TimeSpan basis = _policy.RetryDelays[completedAttempt - 1];
        double factor = 0.75 + Random.Shared.NextDouble() * 0.5;
        TimeSpan jittered = TimeSpan.FromMilliseconds(basis.TotalMilliseconds * factor);
        TimeSpan requested = retryAfter is not null && retryAfter > jittered
            ? TimeSpan.FromSeconds(Math.Min(120, retryAfter.Value.TotalSeconds))
            : jittered;
        TimeSpan remaining = _policy.OverallTimeout - elapsed.Elapsed;
        return requested < remaining ? requested : TimeSpan.FromMilliseconds(
            Math.Max(1, remaining.TotalMilliseconds));
    }

    private static bool IsTransient(HttpRequestException exception)
    {
        if (exception.StatusCode is HttpStatusCode status)
            return TransientStatusCodes.Contains(status);
        bool authenticationFailure = false;
        for (Exception? current = exception; current is not null; current = current.InnerException)
        {
            if (current is AuthenticationException)
                authenticationFailure = true;
        }
        // Certificate, hostname, protocol, and policy rejection remains
        // permanent. A TLS handshake that was cut off by an EOF/reset is a
        // transport interruption; retrying it never bypasses validation.
        if (authenticationFailure)
        {
            if (ContainsPermanentTlsPolicyFailure(exception))
                return false;
            return ContainsTransientTlsTransport(exception);
        }
        return exception.HttpRequestError switch
        {
            HttpRequestError.NameResolutionError => true,
            HttpRequestError.ConnectionError => true,
            HttpRequestError.HttpProtocolError => true,
            HttpRequestError.ResponseEnded => true,
            HttpRequestError.SecureConnectionError =>
                ContainsTransientTlsTransport(exception),
            HttpRequestError.Unknown => ContainsTransportFailure(exception),
            _ => false
        };
    }

    private static bool ContainsTransientTlsTransport(Exception exception)
    {
        string[] patterns =
        {
            "unexpected eof", "0 bytes from the transport stream",
            "connection reset", "forcibly closed", "connection aborted",
            "transport connection", "transport stream ended",
            "response ended prematurely"
        };
        for (Exception? current = exception; current is not null;
             current = current.InnerException)
        {
            if (current is SocketException)
                return true;
            if (current is IOException && patterns.Any(pattern =>
                    current.Message.Contains(pattern,
                        StringComparison.OrdinalIgnoreCase)))
                return true;
        }
        return false;
    }

    private static bool ContainsPermanentTlsPolicyFailure(Exception exception)
    {
        string[] patterns =
        {
            "certificate", "hostname", "name mismatch", "untrusted",
            "revoked", "revocation", "certificate chain", "protocol version",
            "cipher", "remote party sent a tls alert"
        };
        for (Exception? current = exception; current is not null;
             current = current.InnerException)
        {
            if (patterns.Any(pattern => current.Message.Contains(pattern,
                    StringComparison.OrdinalIgnoreCase)))
                return true;
        }
        return false;
    }

    private static bool ContainsTransportFailure(Exception exception)
    {
        for (Exception? current = exception; current is not null; current = current.InnerException)
        {
            if (current is SocketException ||
                current is IOException and not LocalDownloadWriteException)
                return true;
        }
        return false;
    }

    private static bool IsLikelyNetworkRead(IOException exception) =>
        exception is not LocalDownloadWriteException;

    private static TimeSpan? GetRetryAfter(RetryConditionHeaderValue? value)
    {
        if (value?.Delta is TimeSpan delta && delta > TimeSpan.Zero)
            return delta;
        if (value?.Date is DateTimeOffset date)
        {
            TimeSpan until = date - DateTimeOffset.UtcNow;
            return until > TimeSpan.Zero ? until : null;
        }
        return null;
    }

    private static string? StrongEntityTag(EntityTagHeaderValue? value) =>
        value is not null && !value.IsWeak && !string.IsNullOrWhiteSpace(value.Tag)
            ? value.ToString()
            : null;

    private static string CanonicalSource(Uri uri) => uri.AbsoluteUri;

    private TimeSpan Remaining(TimeSpan requested, Stopwatch elapsed)
    {
        TimeSpan remaining = _policy.OverallTimeout - elapsed.Elapsed;
        TimeSpan result = requested < remaining ? requested : remaining;
        return result > TimeSpan.Zero ? result : TimeSpan.FromMilliseconds(1);
    }

    private static async Task<string> ComputeSha256Async(
        string path,
        CancellationToken cancellationToken)
    {
        await using FileStream stream = new(path, FileMode.Open, FileAccess.Read,
            FileShare.Read, BufferSize, FileOptions.Asynchronous | FileOptions.SequentialScan);
        byte[] hash = await SHA256.HashDataAsync(stream, cancellationToken);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    private static void DeletePartial(string partial, string record, InstallLog? log)
    {
        foreach (string path in new[] { partial, record })
        {
            try
            {
                if (File.Exists(path))
                    File.Delete(path);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                log?.Write($"Could not remove stale partial file {Path.GetFileName(path)}: {ex.Message}");
            }
        }
    }

    private static void ResetPartialOrThrow(string partial, string record)
    {
        DeletePartial(partial, record, log: null);
        if (File.Exists(partial) || File.Exists(record))
            throw new LocalDownloadWriteException(new IOException(
                "Windows could not discard the stale partial download."));
    }

    private static string Describe(Exception exception)
    {
        List<string> parts = new();
        for (Exception? current = exception; current is not null && parts.Count < 5;
             current = current.InnerException)
        {
            string category = current is HttpRequestException http
                ? $"{current.GetType().Name}/{http.HttpRequestError}"
                : current.GetType().Name;
            parts.Add($"{category}, HRESULT 0x{current.HResult:X8}: {current.Message}");
        }
        return string.Join(" <- ", parts);
    }

    private static string FormatDuration(TimeSpan duration) => duration.TotalMinutes >= 1
        ? $"{duration.TotalMinutes:0.#}-minute"
        : $"{Math.Max(1, duration.TotalSeconds):0.#}-second";

    private static string FormatBytes(long bytes) => bytes switch
    {
        >= 1024L * 1024 * 1024 => $"{bytes / (1024d * 1024 * 1024):0.0} GB",
        >= 1024L * 1024 => $"{bytes / (1024d * 1024):0.0} MB",
        _ => $"{bytes / 1024d:0.0} KB"
    };

    public void Dispose() => _client.Dispose();

    private sealed record DownloadResponsePlan(
        bool AlreadyComplete,
        bool Append,
        long StartingOffset,
        long? CompleteLength,
        long? ExpectedSegmentLength,
        PartialDownloadRecord? Record);

    private sealed class TransientDownloadException : Exception
    {
        internal TransientDownloadException(
            string message,
            Exception? inner = null,
            bool stalled = false,
            TimeSpan? retryAfter = null) : base(message, inner)
        {
            Stalled = stalled;
            RetryAfter = retryAfter;
        }

        internal bool Stalled { get; }
        internal TimeSpan? RetryAfter { get; }
    }

    private sealed class LocalDownloadWriteException(Exception inner) : IOException(
        "The local download file could not be written.", inner);
}
