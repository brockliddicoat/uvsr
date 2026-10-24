#include "internal.h"

#include <bcrypt.h>
#include <shlwapi.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>
#include <type_traits>

namespace fs = std::filesystem;

namespace uvsr::launcher
{
    namespace
    {
        void close_internet(HINTERNET value)
        {
            (void)WinHttpCloseHandle(value);
        }

        template<typename T, auto Close>
        class Scoped final
        {
        public:
            Scoped() = default;
            explicit Scoped(T value) : m_Value(value) {}
            ~Scoped() { reset(); }
            Scoped(const Scoped&) = delete;
            Scoped& operator=(const Scoped&) = delete;
            Scoped(Scoped&& other) noexcept : m_Value(other.release()) {}
            Scoped& operator=(Scoped&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    m_Value = other.release();
                }
                return *this;
            }
            [[nodiscard]] T get() const noexcept { return m_Value; }
            [[nodiscard]] explicit operator bool() const noexcept
            {
                if constexpr (std::is_pointer_v<T>)
                    return m_Value != nullptr && m_Value != INVALID_HANDLE_VALUE;
                else
                    return m_Value != T{};
            }
            [[nodiscard]] T release() noexcept
            {
                const T result = m_Value;
                m_Value = T{};
                return result;
            }
            void reset(T value = T{}) noexcept
            {
                if (*this)
                    Close(m_Value);
                m_Value = value;
            }

        private:
            T m_Value{};
        };

        using Internet = Scoped<HINTERNET, close_internet>;

        class Transient final : public std::runtime_error
        {
        public:
            explicit Transient(
                std::string message,
                std::chrono::seconds retryAfter = {})
                : std::runtime_error(std::move(message)),
                  retryAfter(retryAfter) {}

            std::chrono::seconds retryAfter;
        };

        [[nodiscard]] bool transient_status(DWORD status) noexcept
        {
            return status == 408u || status == 425u || status == 429u ||
                status == 500u || status == 502u || status == 503u ||
                status == 504u;
        }

        [[nodiscard]] bool transient_error(DWORD error) noexcept
        {
            switch (error)
            {
            case ERROR_WINHTTP_TIMEOUT:
            case ERROR_WINHTTP_CANNOT_CONNECT:
            case ERROR_WINHTTP_CONNECTION_ERROR:
            case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            case ERROR_WINHTTP_RESEND_REQUEST:
            case ERROR_WINHTTP_RESPONSE_DRAIN_OVERFLOW:
            case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
            case ERROR_WINHTTP_OPERATION_CANCELLED:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] std::uint64_t parse_unsigned(
            std::wstring_view value,
            std::string_view description)
        {
            if (value.empty())
                throw Error(std::string(description) + " was empty.");
            const std::string text = narrow(value);
            std::uint64_t result = 0;
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), result);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != text.data() + text.size())
            {
                throw Error(std::string(description) + " was invalid.");
            }
            return result;
        }

        [[nodiscard]] std::wstring header(
            HINTERNET request,
            DWORD query,
            const wchar_t* custom = WINHTTP_HEADER_NAME_BY_INDEX)
        {
            DWORD bytes = 0;
            WinHttpQueryHeaders(request, query, custom,
                WINHTTP_NO_OUTPUT_BUFFER, &bytes, WINHTTP_NO_HEADER_INDEX);
            const DWORD error = GetLastError();
            if (error == ERROR_WINHTTP_HEADER_NOT_FOUND)
                return {};
            if (error != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t))
                throw Error("The download response headers were invalid.");
            std::wstring value(bytes / sizeof(wchar_t), L'\0');
            if (!WinHttpQueryHeaders(request, query, custom,
                    value.data(), &bytes, WINHTTP_NO_HEADER_INDEX))
            {
                throw Error("The download response headers could not be read.");
            }
            value.resize(wcsnlen_s(value.c_str(), value.size()));
            return value;
        }

        [[nodiscard]] DWORD status_code(HINTERNET request)
        {
            DWORD status = 0;
            DWORD bytes = sizeof(status);
            if (!WinHttpQueryHeaders(request,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes,
                    WINHTTP_NO_HEADER_INDEX))
            {
                throw Error("The download response had no HTTP status.");
            }
            return status;
        }

        [[nodiscard]] std::optional<std::uint64_t> content_length(
            HINTERNET request)
        {
            const std::wstring value = header(request, WINHTTP_QUERY_CONTENT_LENGTH);
            return value.empty()
                ? std::nullopt
                : std::optional(parse_unsigned(value, "The Content-Length header"));
        }

        [[nodiscard]] std::string strong_etag(HINTERNET request)
        {
            const std::wstring value = header(request, WINHTTP_QUERY_ETAG);
            if (value.size() < 2u || value.starts_with(L"W/") ||
                value.front() != L'"' || value.back() != L'"')
            {
                return {};
            }
            return narrow(value);
        }

        [[nodiscard]] bool is_strong_etag(std::string_view value) noexcept
        {
            if (value.size() < 2u || value.front() != '"' ||
                value.back() != '"')
                return false;
            return std::all_of(value.begin() + 1, value.end() - 1,
                [](const unsigned char character)
                {
                    return character == 0x21u ||
                        (character >= 0x23u && character <= 0x7eu);
                });
        }

        struct ContentRange
        {
            std::uint64_t first = 0;
            std::uint64_t last = 0;
            std::uint64_t total = 0;
        };

        [[nodiscard]] ContentRange content_range(HINTERNET request)
        {
            const std::wstring text = header(request, WINHTTP_QUERY_CUSTOM,
                L"Content-Range");
            constexpr std::wstring_view prefix = L"bytes ";
            if (!text.starts_with(prefix))
                throw Error("The server returned an unsafe partial download range.");
            const std::size_t dash = text.find(L'-', prefix.size());
            const std::size_t slash = text.find(L'/', dash + 1u);
            if (dash == std::wstring::npos || slash == std::wstring::npos)
                throw Error("The server returned an unsafe partial download range.");
            ContentRange result;
            result.first = parse_unsigned(text.substr(prefix.size(),
                dash - prefix.size()), "The range start");
            result.last = parse_unsigned(text.substr(dash + 1u,
                slash - dash - 1u), "The range end");
            result.total = parse_unsigned(text.substr(slash + 1u),
                "The range length");
            if (result.last < result.first || result.total <= result.last)
                throw Error("The server returned an unsafe partial download range.");
            return result;
        }

        [[nodiscard]] std::chrono::seconds retry_after(HINTERNET request)
        {
            const std::wstring value = header(request, WINHTTP_QUERY_RETRY_AFTER);
            if (value.empty())
                return {};
            try
            {
                return std::chrono::seconds(std::min<std::uint64_t>(
                    120u, parse_unsigned(value, "The Retry-After header")));
            }
            catch (const Error&)
            {
                return {};
            }
        }

        [[nodiscard]] std::wstring combine_url(
            std::wstring_view base,
            std::wstring_view relative)
        {
            DWORD characters = 32768u;
            std::wstring result(characters, L'\0');
            HRESULT status = UrlCombineW(std::wstring(base).c_str(),
                std::wstring(relative).c_str(), result.data(), &characters, 0);
            if (status == E_POINTER && characters > result.size())
            {
                result.resize(characters);
                status = UrlCombineW(std::wstring(base).c_str(),
                    std::wstring(relative).c_str(), result.data(), &characters, 0);
            }
            if (FAILED(status) || characters == 0)
                throw Error("A download redirect had an invalid destination.");
            result.resize(characters);
            return result;
        }

        [[nodiscard]] std::chrono::milliseconds retry_delay(int attempt)
        {
            const int exponent = std::min(attempt - 1, 5);
            const int basis = std::min(30, 1 << exponent);
            unsigned random = GetTickCount();
            (void)BCryptGenRandom(nullptr,
                reinterpret_cast<PUCHAR>(&random), sizeof(random),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            const double jitter = 0.75 + (random % 501u) / 1000.0;
            return std::chrono::milliseconds(
                static_cast<int>(basis * 1000.0 * jitter));
        }

        void wait_retry(
            std::chrono::milliseconds delay,
            const Cancellation& cancellation)
        {
            const auto end = std::chrono::steady_clock::now() + delay;
            while (std::chrono::steady_clock::now() < end)
            {
                cancellation.check();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        [[nodiscard]] std::string source_string(std::wstring_view url)
        {
            return narrow(url);
        }

        struct PartialRecord
        {
            std::string source;
            std::string hash;
            std::uint64_t size = 0;
            std::string etag;
        };

        [[nodiscard]] std::string serialize_partial(const PartialRecord& record)
        {
            return "{\n  \"schemaVersion\": 1,\n  \"source\": \"" +
                detail::json_escape(record.source) +
                "\",\n  \"expectedSha256\": \"" + record.hash +
                "\",\n  \"expectedSize\": " + std::to_string(record.size) +
                ",\n  \"entityTag\": \"" + detail::json_escape(record.etag) +
                "\"\n}\n";
        }

        [[nodiscard]] PartialRecord parse_partial(std::string_view text)
        {
            const detail::Json root = detail::parse_json(text);
            detail::require_exact_object(root,
                { "schemaVersion", "source", "expectedSha256",
                    "expectedSize", "entityTag" },
                "The partial download record");
            if (detail::integer_value(detail::member(root, "schemaVersion"),
                    "Partial schema version") != 1)
            {
                throw Error("The partial download record had an invalid schema.");
            }
            PartialRecord result;
            result.source = detail::string_value(
                detail::member(root, "source"), "Partial source");
            result.hash = detail::string_value(
                detail::member(root, "expectedSha256"), "Partial hash");
            result.size = detail::unsigned_value(
                detail::member(root, "expectedSize"), "Partial size");
            result.etag = detail::string_value(
                detail::member(root, "entityTag"), "Partial ETag");
            if (!result.etag.empty() && !is_strong_etag(result.etag))
                throw Error("The partial download record had an invalid ETag.");
            return result;
        }

        void remove_partial(const fs::path& partial, const fs::path& record)
        {
            std::error_code ignored;
            fs::remove(partial, ignored);
            fs::remove(record, ignored);
        }
    }

    struct DownloadManager::Impl
    {
        Internet session;
        std::mutex requestMutex;
        HINTERNET activeRequest = nullptr;

        Impl()
        {
            const std::wstring agent = L"uvsr-launcher/" +
                widen(LauncherVersion);
            session.reset(WinHttpOpen(agent.c_str(),
                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session)
                detail::throw_last_error("Windows could not start secure downloads");
        }

        ~Impl()
        {
            cancel();
        }

        void activate(HINTERNET request)
        {
            std::lock_guard lock(requestMutex);
            if (activeRequest != nullptr)
                throw Error("UVSR Launcher attempted overlapping downloads.");
            activeRequest = request;
        }

        void close_request(HINTERNET request) noexcept
        {
            bool close = false;
            {
                std::lock_guard lock(requestMutex);
                if (activeRequest == request)
                {
                    activeRequest = nullptr;
                    close = true;
                }
            }
            if (close)
                WinHttpCloseHandle(request);
        }

        void cancel() noexcept
        {
            HINTERNET request = nullptr;
            {
                std::lock_guard lock(requestMutex);
                request = activeRequest;
                activeRequest = nullptr;
            }
            if (request != nullptr)
                WinHttpCloseHandle(request);
        }

        struct Response
        {
            Impl* owner = nullptr;
            Internet connection;
            HINTERNET request = nullptr;
            DWORD status = 0;
            std::wstring finalUrl;

            Response() = default;
            ~Response()
            {
                if (owner != nullptr && request != nullptr)
                    owner->close_request(request);
            }
            Response(const Response&) = delete;
            Response& operator=(const Response&) = delete;
            Response(Response&& other) noexcept
                : owner(other.owner),
                  connection(std::move(other.connection)),
                  request(other.request),
                  status(other.status),
                  finalUrl(std::move(other.finalUrl))
            {
                other.owner = nullptr;
                other.request = nullptr;
            }
            Response& operator=(Response&&) = delete;
        };

        [[noreturn]] static void request_failure(
            std::string_view action,
            const Cancellation& cancellation)
        {
            const DWORD error = GetLastError();
            cancellation.check();
            if (transient_error(error))
                throw Transient(std::string(action) + ": " +
                    detail::win32_message(error));
            throw Error(std::string(action) + ": " +
                detail::win32_message(error));
        }

        [[nodiscard]] Response open(
            std::wstring initialUrl,
            std::uint64_t offset,
            std::string_view etag,
            const Cancellation& cancellation)
        {
            std::wstring current = std::move(initialUrl);
            for (int redirect = 0; redirect <= 8; ++redirect)
            {
                cancellation.check();
                URL_COMPONENTS components{};
                components.dwStructSize = sizeof(components);
                components.dwSchemeLength = DWORD(-1);
                components.dwHostNameLength = DWORD(-1);
                components.dwUrlPathLength = DWORD(-1);
                components.dwExtraInfoLength = DWORD(-1);
                components.dwUserNameLength = DWORD(-1);
                components.dwPasswordLength = DWORD(-1);
                if (!WinHttpCrackUrl(current.c_str(),
                        static_cast<DWORD>(current.size()), 0, &components) ||
                    components.nScheme != INTERNET_SCHEME_HTTPS ||
                    components.dwHostNameLength == 0 ||
                    components.dwUserNameLength != 0 ||
                    components.dwPasswordLength != 0)
                {
                    throw Error("A download did not use a safe HTTPS address.");
                }
                const std::wstring host(components.lpszHostName,
                    components.dwHostNameLength);
                std::wstring path = components.dwUrlPathLength == 0
                    ? L"/"
                    : std::wstring(components.lpszUrlPath,
                        components.dwUrlPathLength);
                if (components.dwExtraInfoLength != 0)
                    path.append(components.lpszExtraInfo,
                        components.dwExtraInfoLength);
                Internet connection(WinHttpConnect(session.get(), host.c_str(),
                    components.nPort, 0));
                if (!connection)
                    request_failure("The secure download could not connect", cancellation);
                static const wchar_t* Accept[] = { L"*/*", nullptr };
                HINTERNET request = WinHttpOpenRequest(connection.get(), L"GET",
                    path.c_str(), nullptr, WINHTTP_NO_REFERER, Accept,
                    WINHTTP_FLAG_SECURE);
                if (request == nullptr)
                    request_failure("The secure download request could not be opened", cancellation);
                activate(request);
                Response response;
                response.owner = this;
                response.connection = std::move(connection);
                response.request = request;
                response.finalUrl = current;
                DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
                if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                        &redirectPolicy, sizeof(redirectPolicy)))
                {
                    request_failure(
                        "The secure redirect policy could not be set",
                        cancellation);
                }
                if (!WinHttpSetTimeouts(request, 30000, 30000, 45000, 90000))
                    request_failure("The secure download timeouts could not be set", cancellation);
                std::wstring headers = L"Accept-Encoding: identity\r\n";
                if (offset != 0)
                {
                    headers += L"Range: bytes=" + std::to_wstring(offset) + L"-\r\n";
                    if (!etag.empty())
                        headers += L"If-Range: " + widen(etag) + L"\r\n";
                }
                if (!WinHttpSendRequest(request, headers.c_str(),
                        static_cast<DWORD>(headers.size()),
                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
                {
                    request_failure("The secure download request could not be sent", cancellation);
                }
                if (!WinHttpReceiveResponse(request, nullptr))
                    request_failure("The secure download service did not respond", cancellation);
                response.status = status_code(request);
                if (response.status != 301u && response.status != 302u &&
                    response.status != 303u && response.status != 307u &&
                    response.status != 308u)
                {
                    return response;
                }
                const std::wstring location = header(request, WINHTTP_QUERY_LOCATION);
                if (location.empty())
                    throw Error("A download redirect had no destination.");
                if (redirect == 8)
                    throw Error("A download exceeded its redirect safety limit.");
                current = combine_url(current, location);
            }
            throw Error("A download exceeded its redirect safety limit.");
        }

        [[nodiscard]] std::vector<std::byte> read_memory(
            HINTERNET request,
            std::uint64_t maximumBytes,
            std::string_view phase,
            const ProgressCallback& progress,
            const Cancellation& cancellation)
        {
            const auto declared = content_length(request);
            if (declared && *declared > maximumBytes)
                throw Error("A download was larger than its safety limit.");
            std::vector<std::byte> output;
            if (declared)
                output.reserve(static_cast<std::size_t>(*declared));
            std::array<std::byte, 128u * 1024u> buffer{};
            for (;;)
            {
                cancellation.check();
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer.data(),
                        static_cast<DWORD>(buffer.size()), &read))
                {
                    request_failure("The secure download was interrupted", cancellation);
                }
                if (read == 0)
                    break;
                if (output.size() + read > maximumBytes)
                    throw Error("A download exceeded its safety limit.");
                output.insert(output.end(), buffer.begin(), buffer.begin() + read);
                if (progress)
                {
                    progress({ std::string(phase),
                        "Received " + detail::format_bytes(output.size()) + ".",
                        declared && *declared != 0
                            ? std::optional(static_cast<int>(
                                output.size() * 100ull / *declared))
                            : std::nullopt,
                        true });
                }
            }
            if (declared && output.size() != *declared)
                throw Transient("The download ended before its declared length.");
            return output;
        }

        [[nodiscard]] std::uint64_t read_file_body(
            HINTERNET request,
            HANDLE output,
            std::uint64_t offset,
            std::uint64_t expectedSize,
            std::string_view phase,
            const ProgressCallback& progress,
            const Cancellation& cancellation)
        {
            LARGE_INTEGER position{};
            position.QuadPart = static_cast<LONGLONG>(offset);
            if (!SetFilePointerEx(output, position, nullptr, FILE_BEGIN))
                detail::throw_last_error("Windows could not resume the partial download");
            std::array<std::byte, 128u * 1024u> buffer{};
            std::uint64_t total = offset;
            int lastPercent = -1;
            for (;;)
            {
                cancellation.check();
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer.data(),
                        static_cast<DWORD>(buffer.size()), &read))
                {
                    request_failure("The secure download was interrupted", cancellation);
                }
                if (read == 0)
                    break;
                if (read > expectedSize - total)
                    throw Error("A download exceeded its signed size.");
                DWORD written = 0;
                if (!WriteFile(output, buffer.data(), read, &written, nullptr) ||
                    written != read)
                {
                    detail::throw_last_error("Windows could not save the partial download");
                }
                total += read;
                const int percent = static_cast<int>(total * 100ull / expectedSize);
                if (progress && percent != lastPercent)
                {
                    progress({ std::string(phase),
                        "Received " + detail::format_bytes(total) + ".",
                        percent, true });
                    lastPercent = percent;
                }
            }
            if (!FlushFileBuffers(output))
                detail::throw_last_error("Windows could not flush the partial download");
            return total;
        }
    };

    DownloadManager::DownloadManager() : m_Impl(std::make_unique<Impl>()) {}
    DownloadManager::~DownloadManager() = default;

    std::vector<std::byte> DownloadManager::bytes(
        std::wstring_view url,
        std::uint64_t maximumBytes,
        std::string_view phase,
        const ProgressCallback& progress,
        const LogCallback& log,
        const Cancellation& cancellation)
    {
        if (url.empty() || maximumBytes == 0)
            throw Error("A download request was invalid.");
        const auto started = std::chrono::steady_clock::now();
        std::string last;
        for (int attempt = 1; attempt <= 8; ++attempt)
        {
            cancellation.check();
            try
            {
                auto response = m_Impl->open(std::wstring(url), 0, {}, cancellation);
                if (transient_status(response.status))
                    throw Transient("The download service returned HTTP " +
                        std::to_string(response.status) + ".",
                        retry_after(response.request));
                if (response.status != 200u)
                    throw Error("The download service returned HTTP " +
                        std::to_string(response.status) + ".");
                auto result = m_Impl->read_memory(response.request,
                    maximumBytes, phase, progress, cancellation);
                if (result.empty())
                    throw Error("The download was empty.");
                if (log)
                    log("Downloaded and bounded " + source_string(response.finalUrl) + ".");
                return result;
            }
            catch (const Transient& error)
            {
                last = error.what();
                if (log)
                    log("Transient download failure, attempt " +
                        std::to_string(attempt) + "/8: " + last);
                if (attempt == 8 ||
                    std::chrono::steady_clock::now() - started >=
                        std::chrono::minutes(10))
                {
                    break;
                }
                const auto delay = error.retryAfter.count() != 0
                    ? std::chrono::duration_cast<std::chrono::milliseconds>(
                        error.retryAfter)
                    : retry_delay(attempt);
                if (progress)
                    progress({ "Connection interrupted",
                        "Retrying the update check automatically.",
                        std::nullopt, true });
                wait_retry(delay, cancellation);
            }
        }
        throw Error("The update service remained unavailable. Nothing installed was changed. " + last);
    }

    void DownloadManager::file(
        std::wstring_view url,
        const fs::path& destination,
        std::uint64_t expectedSize,
        std::string_view expectedSha256,
        std::uint64_t maximumBytes,
        std::string_view phase,
        const ProgressCallback& progress,
        const LogCallback& log,
        const Cancellation& cancellation,
        const std::function<void(const fs::path&)>& validator)
    {
        if (url.empty() || destination.parent_path().empty() ||
            expectedSize == 0 || expectedSize > maximumBytes ||
            !is_lower_hex(expectedSha256, 64u))
        {
            throw Error("An artifact download request was invalid.");
        }
        reject_reparse_chain(destination.parent_path(), "download directory");
        fs::create_directories(destination.parent_path());
        reject_reparse_chain(destination.parent_path(), "download directory");
        reject_reparse_chain(destination, "download cache entry");

        const fs::path partial = destination.native() + L".part";
        const fs::path recordPath = destination.native() + L".part.json";
        reject_reparse_chain(partial, "partial download");
        reject_reparse_chain(recordPath, "partial download record");

        if (fs::is_regular_file(destination))
        {
            std::error_code sizeError;
            const auto size = fs::file_size(destination, sizeError);
            if (!sizeError && size == expectedSize &&
                sha256(destination) == expectedSha256)
            {
                if (validator)
                    validator(destination);
                if (log)
                    log("Reused the verified download cache entry " +
                        narrow(destination.filename().native()) + ".");
                if (progress)
                    progress({ std::string(phase),
                        "The verified download was already available.", 100, true });
                return;
            }
            fs::remove(destination);
        }

        PartialRecord record{ source_string(url), std::string(expectedSha256),
            expectedSize, {} };
        std::uint64_t offset = 0;
        if (fs::exists(partial) || fs::exists(recordPath))
        {
            try
            {
                if (!fs::is_regular_file(partial) ||
                    !fs::is_regular_file(recordPath))
                {
                    throw Error("The saved partial download was incomplete.");
                }
                const PartialRecord saved = parse_partial(detail::read_text(
                    recordPath, 4096u));
                const std::uint64_t savedBytes = fs::file_size(partial);
                if (saved.source != record.source || saved.hash != record.hash ||
                    saved.size != record.size || savedBytes > expectedSize)
                {
                    throw Error("The saved partial download did not match this request.");
                }
                record = saved;
                offset = savedBytes;
                if (log)
                    log("Found a hash bound partial download at " +
                        detail::format_bytes(offset) + " of " +
                        detail::format_bytes(expectedSize) + ".");
            }
            catch (const std::exception& error)
            {
                if (log)
                    log("Discarded an unusable partial download: " +
                        std::string(error.what()));
                remove_partial(partial, recordPath);
                record.etag.clear();
                offset = 0;
            }
        }

        const auto started = std::chrono::steady_clock::now();
        std::string last;
        bool hashRestarted = false;
        for (int attempt = 1; attempt <= 12; ++attempt)
        {
            cancellation.check();
            try
            {
                if (progress && offset != 0)
                    progress({ std::string(phase),
                        "Resuming with " + detail::format_bytes(offset) +
                            " already received.",
                        static_cast<int>(offset * 100ull / expectedSize), true });
                auto response = m_Impl->open(std::wstring(url), offset,
                    record.etag, cancellation);
                if (transient_status(response.status))
                    throw Transient("The download service returned HTTP " +
                        std::to_string(response.status) + ".",
                        retry_after(response.request));
                if (response.status == 416u && offset == expectedSize)
                {
                    // The complete local file still passes the signed size and
                    // hash below. No server validator is needed at this point.
                }
                else if (response.status == 416u && offset != 0)
                {
                    remove_partial(partial, recordPath);
                    offset = 0;
                    record.etag.clear();
                    throw Transient(
                        "The server rejected the saved range, so the download restarted.");
                }
                else if (response.status == 206u)
                {
                    if (offset == 0)
                        throw Error("The server returned an unexpected partial download.");
                    const ContentRange range = content_range(response.request);
                    if (range.first != offset || range.total != expectedSize)
                        throw Error("The server returned an unsafe partial download range.");
                    const auto length = content_length(response.request);
                    const std::uint64_t segment = range.last - range.first + 1u;
                    if (length && *length != segment)
                        throw Error("The partial response length did not match its range.");
                    const std::string tag = strong_etag(response.request);
                    if (!record.etag.empty() && !tag.empty() && tag != record.etag)
                    {
                        remove_partial(partial, recordPath);
                        offset = 0;
                        record.etag.clear();
                        throw Transient("The remote artifact changed while resuming, so the download restarted.");
                    }
                }
                else if (response.status == 200u)
                {
                    offset = 0;
                    record.etag = strong_etag(response.request);
                    const auto length = content_length(response.request);
                    if (length && *length != expectedSize)
                        throw Error("The download did not match its signed size.");
                    remove_partial(partial, recordPath);
                }
                else
                {
                    throw Error("The download service returned HTTP " +
                        std::to_string(response.status) + ".");
                }

                if (response.status != 416u)
                {
                    if (record.etag.empty())
                        record.etag = strong_etag(response.request);
                    detail::write_atomic(recordPath, serialize_partial(record));
                    const DWORD disposition = offset == 0
                        ? CREATE_ALWAYS
                        : OPEN_ALWAYS;
                    Scoped<HANDLE, CloseHandle> output(CreateFileW(partial.c_str(),
                        GENERIC_WRITE, 0, nullptr, disposition,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
                    if (!output)
                        detail::throw_last_error("Windows could not save the partial download");
                    offset = m_Impl->read_file_body(response.request, output.get(),
                        offset, expectedSize, phase, progress, cancellation);
                }
                if (offset != expectedSize || fs::file_size(partial) != expectedSize)
                    throw Transient("The download ended before its signed size.");
                const std::string actual = sha256(partial);
                if (actual != expectedSha256)
                {
                    remove_partial(partial, recordPath);
                    offset = 0;
                    record.etag.clear();
                    if (hashRestarted)
                        throw Error("The download failed its signed SHA-256 twice.");
                    hashRestarted = true;
                    throw Transient("The completed partial did not match its signed SHA-256 and was restarted.");
                }
                if (validator)
                    validator(partial);
                if (!MoveFileExW(partial.c_str(), destination.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    detail::throw_last_error("Windows could not activate the verified download");
                }
                fs::remove(recordPath);
                if (log)
                    log("Downloaded " + source_string(response.finalUrl) + " (" +
                        std::to_string(expectedSize) + " bytes, SHA-256 " +
                        actual + ").");
                if (progress)
                    progress({ std::string(phase),
                        "The download passed its signed integrity check.", 100, true });
                return;
            }
            catch (const Cancelled&)
            {
                throw;
            }
            catch (const Transient& error)
            {
                last = error.what();
                if (fs::exists(partial))
                    offset = fs::file_size(partial);
                if (log)
                    log("Transient download failure, attempt " +
                        std::to_string(attempt) + "/12 at " +
                        std::to_string(offset) + " bytes: " + last);
                if (attempt == 12 ||
                    std::chrono::steady_clock::now() - started >=
                        std::chrono::minutes(45))
                {
                    break;
                }
                const auto delay = error.retryAfter.count() != 0
                    ? std::chrono::duration_cast<std::chrono::milliseconds>(
                        error.retryAfter)
                    : retry_delay(attempt);
                if (progress)
                    progress({ "Connection interrupted",
                        "Retrying automatically. Verified partial data was preserved.",
                        std::nullopt, true });
                wait_retry(delay, cancellation);
            }
        }
        throw Error("The artifact download remained unavailable. Verified partial data was preserved for the next attempt. " + last);
    }

    void DownloadManager::cancel() noexcept
    {
        m_Impl->cancel();
    }
}
