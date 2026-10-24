#include "core.h"
#include <winhttp.h>
#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace uvsr::launcher
{
    namespace
    {
        struct Internet
        {
            std::atomic<HINTERNET> handle{nullptr};
            explicit Internet(HINTERNET value) : handle(value) { WinCheck(value != nullptr, "Open HTTPS connection"); }
            ~Internet() { Close(); }
            void Close() { if (auto value = handle.exchange(nullptr)) WinHttpCloseHandle(value); }
            operator HINTERNET() const { return handle.load(); }
        };
        struct Transient : std::runtime_error { using std::runtime_error::runtime_error; };
        void NetworkCheck(BOOL result)
        {
            if (result) return;
            DWORD error = GetLastError();
            const auto message = "The HTTPS download failed (Windows error " + std::to_string(error) + ").";
            if (error == ERROR_WINHTTP_TIMEOUT || error == ERROR_WINHTTP_CANNOT_CONNECT ||
                error == ERROR_WINHTTP_CONNECTION_ERROR || error == ERROR_WINHTTP_NAME_NOT_RESOLVED ||
                error == ERROR_WINHTTP_RESEND_REQUEST) throw Transient(message);
            throw std::runtime_error(message);
        }
        std::string Header(HINTERNET request, DWORD key)
        {
            DWORD size = 0;
            if (!WinHttpQueryHeaders(request, key, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX))
            {
                if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) return {};
                WinCheck(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Read HTTPS headers");
            }
            Require(size <= 32768, "An HTTPS response header exceeded its safe limit.");
            std::wstring text(size / sizeof(wchar_t), 0);
            NetworkCheck(WinHttpQueryHeaders(request, key, WINHTTP_HEADER_NAME_BY_INDEX, text.data(), &size, WINHTTP_NO_HEADER_INDEX));
            text.resize(size / sizeof(wchar_t));
            while (!text.empty() && text.back() == 0) text.pop_back();
            return Utf8(text);
        }
        uint64_t Unsigned(std::string_view text)
        {
            uint64_t value = 0; const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            Require(!text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size(), "An HTTPS byte count is invalid.");
            return value;
        }
        bool StrongTag(std::string_view value)
        {
            if (value.size() < 2 || value.size() > 4096 || value.front() != '"' || value.back() != '"') return false;
            for (size_t i = 1; i + 1 < value.size(); ++i)
                if (static_cast<unsigned char>(value[i]) < 0x21 || value[i] == '"' || value[i] == 0x7f) return false;
            return true;
        }
        struct Url
        {
            std::wstring host, target;
            INTERNET_PORT port = 0;
            explicit Url(std::string_view value)
            {
                auto wide = Wide(value);
                URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
                parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
                WinCheck(WinHttpCrackUrl(wide.c_str(), DWORD(wide.size()), 0, &parts), "Parse HTTPS URL");
                Require(parts.nScheme == INTERNET_SCHEME_HTTPS && parts.dwHostNameLength > 0 &&
                    !parts.dwUserNameLength && !parts.dwPasswordLength && value.find('#') == value.npos,
                    "Downloads and every redirect must use HTTPS without credentials or fragments.");
                host.assign(parts.lpszHostName, parts.dwHostNameLength);
                target.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
                if (parts.dwExtraInfoLength) target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
                if (target.empty()) target = L"/";
                port = parts.nPort;
            }
        };
        struct Resume { std::string tag; uint64_t length = 0, offset = 0; };
        std::optional<Resume> LoadResume(const fs::path& partial, const fs::path& record,
            std::string_view source, std::string_view hash, uint64_t maximum)
        {
            RejectReparseChain(partial); RejectReparseChain(record);
            try
            {
                if (!fs::exists(record) || !fs::exists(partial)) return {};
                auto state = ReadRecord(record);
                RequireExactObject(state, {"schemaVersion", "source", "expectedSha256", "entityTag", "completeLength"}, "partial download");
                const auto length = Number(state, "completeLength");
                Require(Number(state, "schemaVersion") == 1 && Text(state, "source") == source && Text(state, "expectedSha256") == hash &&
                    StrongTag(Text(state, "entityTag")) && length > 0 && uint64_t(length) <= maximum, "A saved download cannot be resumed.");
                auto size = fs::file_size(partial);
                Require(size <= uint64_t(length), "The saved download exceeds its declared length.");
                return Resume{Text(state, "entityTag"), uint64_t(length), size};
            }
            catch (const std::exception&) { return {}; }
        }
        void Delay(std::stop_token stop, std::chrono::milliseconds duration)
        {
            std::mutex mutex; std::unique_lock lock(mutex); std::condition_variable_any changed;
            changed.wait_for(lock, stop, duration, [] { return false; });
            CheckCancelled(stop);
        }
    }
    void Download(std::string_view source, const fs::path& destination, uint64_t maximum,
        std::optional<std::string_view> expectedHash, std::stop_token stop, const Report& report)
    {
        Require(maximum > 0 && maximum <= MaximumArchiveBytes && (!expectedHash || IsLowerHex(*expectedHash, 64)), "The download request identity is invalid.");
        (void)Url(source);
        CreateDirectories(destination.parent_path()); RejectReparseChain(destination);
        const fs::path partial = destination.wstring() + L".part", record = destination.wstring() + L".part.json";
        std::stop_source lifetime;
        std::stop_callback cancelled(stop, [&] { lifetime.request_stop(); });
        std::jthread deadline([&](std::stop_token timerStop)
        {
            std::mutex mutex; std::unique_lock lock(mutex); std::condition_variable_any changed;
            changed.wait_for(lock, timerStop, std::chrono::minutes(45), [] { return false; });
            if (!timerStop.stop_requested()) lifetime.request_stop();
        });
        const auto token = lifetime.get_token();
        Internet session(WinHttpOpen(Wide(std::string("uvsr-launcher/") + LauncherVersion).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        WinCheck(WinHttpSetTimeouts(session, 45000, 45000, 45000, 90000), "Set HTTPS deadlines");
        auto resume = LoadResume(partial, record, source, expectedHash.value_or(""), maximum);
        std::string lastFailure;
        unsigned segments = 0;
        for (unsigned attempt = 0; attempt < 6; ++attempt)
        {
            CheckCancelled(token);
            if (attempt) Delay(token, std::chrono::seconds(1u << (attempt - 1)));
            try
            {
                auto current = std::string(source);
                bool received = false;
                for (unsigned redirect = 0; redirect <= 5 && !received; ++redirect)
                {
                    CheckCancelled(token);
                    Url url(current);
                    Internet connection(WinHttpConnect(session, url.host.c_str(), url.port, 0));
                    Internet request(WinHttpOpenRequest(connection, L"GET", url.target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
                    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
                    WinCheck(WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)), "Disable automatic redirects");
                    std::wstring headers = L"Accept-Encoding: identity\r\n";
                    if (resume && resume->offset)
                        headers += L"Range: bytes=" + std::to_wstring(resume->offset) + L"-\r\nIf-Range: " + Wide(resume->tag) + L"\r\n";
                    std::stop_callback cancelRequest(token, [&] { request.Close(); });
                    CheckCancelled(token);
                    NetworkCheck(WinHttpSendRequest(request, headers.c_str(), DWORD(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
                    NetworkCheck(WinHttpReceiveResponse(request, nullptr));
                    DWORD status = 0, statusSize = sizeof(status);
                    NetworkCheck(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX));
                    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
                    {
                        Require(redirect < 5, "The download redirected too many times.");
                        auto location = Header(request, WINHTTP_QUERY_LOCATION);
                        if (location.starts_with('/') && !location.starts_with("//"))
                            location = "https://" + Utf8(url.host) + (url.port == 443 ? "" : ":" + std::to_string(url.port)) + location;
                        (void)Url(location); current = std::move(location); continue;
                    }
                    if (status == 408 || status == 425 || status == 429 || status == 500 || status == 502 || status == 503 || status == 504)
                        throw Transient("The download service returned HTTP " + std::to_string(status) + ".");
                    auto encoding = Header(request, WINHTTP_QUERY_CONTENT_ENCODING);
                    Require(encoding.empty() || Lower(encoding) == "identity", "The server changed the download byte representation.");
                    auto lengthText = Header(request, WINHTTP_QUERY_CONTENT_LENGTH);
                    std::optional<uint64_t> segmentLength;
                    if (!lengthText.empty()) segmentLength = Unsigned(lengthText);
                    auto tag = Header(request, WINHTTP_QUERY_ETAG);
                    uint64_t start = 0;
                    std::optional<uint64_t> total;
                    if (status == 416 && resume && resume->offset == resume->length)
                    {
                        Require(Header(request, WINHTTP_QUERY_CONTENT_RANGE) == "bytes */" + std::to_string(resume->length), "The completed download range is invalid.");
                        received = true; break;
                    }
                    Require(status == 200 || status == 206, "The download service returned HTTP " + std::to_string(status) + ".");
                    if (status == 206)
                    {
                        Require(resume && resume->offset > 0, "The server returned an unexpected partial download.");
                        auto range = Header(request, WINHTTP_QUERY_CONTENT_RANGE);
                        Require(range.starts_with("bytes "), "The partial download range is invalid.");
                        auto dash = range.find('-', 6), slash = range.find('/', 6);
                        Require(dash != range.npos && slash > dash && slash != range.npos, "The partial download range is invalid.");
                        auto from = Unsigned(std::string_view(range).substr(6, dash - 6));
                        auto to = Unsigned(std::string_view(range).substr(dash + 1, slash - dash - 1));
                        auto complete = Unsigned(std::string_view(range).substr(slash + 1));
                        Require(from == resume->offset && complete == resume->length && to >= from && to < complete &&
                            (!segmentLength || *segmentLength == to - from + 1), "The partial download range does not match its saved identity.");
                        if (!tag.empty() && tag != resume->tag)
                        { resume.reset(); fs::remove(record); throw Transient("The download changed while resuming. It will restart."); }
                        start = from; total = complete; segmentLength = to - from + 1;
                    }
                    else
                    {
                        total = segmentLength;
                        Require(!total || (*total > 0 && *total <= maximum), "The download size exceeds its limit.");
                        resume.reset();
                        if (StrongTag(tag) && total)
                        {
                            resume = Resume{tag, *total, 0};
                            WriteRecord(record, JObject({{"schemaVersion", JNumber(1)}, {"source", JString(std::string(source))},
                                {"expectedSha256", JString(std::string(expectedHash.value_or("")))}, {"entityTag", JString(tag)}, {"completeLength", JNumber(int64_t(*total))}}));
                        }
                        else { RejectReparseChain(record); fs::remove(record); }
                    }
                    Require(++segments <= 32, "The download required too many segments.");
                    RejectReparseChain(partial);
                    {
                        Handle output(CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, start ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
                        WinCheck(output.value != INVALID_HANDLE_VALUE, "Open partial download");
                        LARGE_INTEGER size{}; WinCheck(GetFileSizeEx(output, &size), "Inspect partial download");
                        Require(uint64_t(size.QuadPart) == start, "The saved partial download changed before it could resume.");
                        LARGE_INTEGER offset{}; offset.QuadPart = start;
                        WinCheck(SetFilePointerEx(output, offset, nullptr, FILE_BEGIN), "Resume partial download");
                        uint64_t count = 0; int lastPercent = -2;
                        auto lastReport = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                        std::array<unsigned char, 128 * 1024> buffer{};
                        for (;;)
                        {
                            CheckCancelled(token);
                            DWORD bytes = 0;
                            NetworkCheck(WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &bytes));
                            if (!bytes) break;
                            Require(count <= maximum - start && bytes <= maximum - start - count &&
                                (!segmentLength || (count <= *segmentLength && bytes <= *segmentLength - count)), "The download exceeded its declared size.");
                            DWORD written = 0;
                            WinCheck(WriteFile(output, buffer.data(), bytes, &written, nullptr) && written == bytes, "Write partial download");
                            count += bytes;
                            if (resume) resume->offset = start + count;
                            int percent = total ? int((start + count) * 100 / *total) : -1;
                            const auto now = std::chrono::steady_clock::now();
                            if (report && (percent != lastPercent || now - lastReport >= std::chrono::milliseconds(500)))
                            {
                                report({"Downloading files", Utf8(destination.filename().wstring()) + " - " + std::to_string((start + count) / 1024) + " KiB",
                                    percent >= 0 ? std::optional<int>(percent) : std::nullopt});
                                lastPercent = percent; lastReport = now;
                            }
                        }
                        WinCheck(FlushFileBuffers(output), "Flush downloaded file");
                        if ((segmentLength && count != *segmentLength) || (total && start + count != *total))
                            throw Transient("The download ended before all declared bytes arrived.");
                    }
                    received = true;
                }
                Require(received, "The HTTPS download did not complete.");
                CheckCancelled(token);
                if (expectedHash) VerifyFile(partial, maximum, *expectedHash);
                else Require(fs::file_size(partial) > 0 && fs::file_size(partial) <= maximum, "The downloaded feed is empty or too large.");
                RejectReparseChain(destination);
                WinCheck(MoveFileExW(partial.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Activate verified download");
                fs::remove(record);
                return;
            }
            catch (const Transient& error) { lastFailure = error.what(); CheckCancelled(token); }
        }
        throw std::runtime_error("The download stopped after six attempts. " + lastFailure);
    }
}
