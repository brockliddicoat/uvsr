#include "engine_diagnostic_log.h"
#include "renderer_log.h"

#include <Windows.h>

#include <chrono>
#include <errno.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

namespace uvsr
{
    namespace
    {
        SRWLOCK g_LogMutex = SRWLOCK_INIT;
        FILE* g_Log = nullptr;
        char g_FileBuffer[8192];
        char g_LastMessage[log::MessageCapacity]{};
        log::Callback g_Downstream;
        EngineDiagnosticLogClock g_LogClock;
        int64_t g_LastFlush = 0;
        int64_t g_LastMessageWrite = 0;
        log::Severity g_LastSeverity = log::Severity::None;
        uint64_t g_SuppressedRepeatCount = 0;
        bool g_LogCallbackInstalled = false;
        bool g_WriteFailed = false;
        bool g_ShutdownRegistered = false;

#if defined(UVSR_ENGINE_LOG_TEST_HOOKS)
        thread_local EngineDiagnosticLogFailure g_NextFailure = EngineDiagnosticLogFailure::None;
        bool Fail(EngineDiagnosticLogFailure failure) noexcept
        {
            if (g_NextFailure != failure) return false;
            g_NextFailure = EngineDiagnosticLogFailure::None;
            errno = EIO;
            return true;
        }
#define UVSR_LOG_FAIL(name) Fail(EngineDiagnosticLogFailure::name)
#else
#define UVSR_LOG_FAIL(name) false
#endif

        struct LogLock
        {
            LogLock() noexcept { AcquireSRWLockExclusive(&g_LogMutex); }
            ~LogLock() noexcept { ReleaseSRWLockExclusive(&g_LogMutex); }
        };

        struct PreparedPath
        {
            void* storage = nullptr;
            wchar_t* directory = nullptr;
            char* display = nullptr;
            ~PreparedPath() noexcept { free(storage); }
        };

        bool Separator(wchar_t value) noexcept { return value == L'\\' || value == L'/'; }

        bool PreparePath(const wchar_t* path, PreparedPath& prepared, DWORD& error) noexcept
        {
            const size_t length = wcslen(path);
            const int displayBytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                path, -1, nullptr, 0, nullptr, nullptr);
            if (!displayBytes)
            {
                error = GetLastError();
                return false;
            }
            if (length >= size_t(PTRDIFF_MAX) / sizeof(wchar_t) - 1)
            {
                error = ERROR_FILENAME_EXCED_RANGE;
                return false;
            }
            const size_t nativeBytes = (length + 1) * sizeof(wchar_t);
            if (size_t(displayBytes) > size_t(PTRDIFF_MAX) - nativeBytes)
            {
                error = ERROR_FILENAME_EXCED_RANGE;
                return false;
            }
            prepared.storage = UVSR_LOG_FAIL(Allocation) ? nullptr : malloc(nativeBytes + size_t(displayBytes));
            if (!prepared.storage)
            {
                error = ERROR_NOT_ENOUGH_MEMORY;
                return false;
            }
            prepared.directory = static_cast<wchar_t*>(prepared.storage);
            prepared.display = static_cast<char*>(prepared.storage) + nativeBytes;
            memcpy(prepared.directory, path, nativeBytes);
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
                prepared.display, displayBytes, nullptr, nullptr) != displayBytes)
            {
                error = GetLastError();
                return false;
            }
            return true;
        }

        bool CreateParentDirectories(const wchar_t* path, wchar_t* directory, DWORD& error) noexcept
        {
            size_t parent = 0;
            for (size_t i = 0; path[i]; ++i)
                if (Separator(path[i])) parent = i;
            if (!parent) return true;
            directory[parent] = 0;
            size_t existing = parent;
            while (existing)
            {
                DWORD attributes = GetFileAttributesW(directory);
                DWORD failure = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0;
                if (attributes == INVALID_FILE_ATTRIBUTES && Separator(path[existing]))
                {
                    // extended drive and volume roots require their final slash.
                    const wchar_t saved = directory[existing + 1];
                    directory[existing] = path[existing];
                    directory[existing + 1] = 0;
                    attributes = GetFileAttributesW(directory);
                    if (attributes != INVALID_FILE_ATTRIBUTES)
                        ++existing;
                    else
                    {
                        directory[existing] = 0;
                        directory[existing + 1] = saved;
                    }
                }
                if (attributes != INVALID_FILE_ATTRIBUTES)
                {
                    if (attributes & FILE_ATTRIBUTE_DIRECTORY) break;
                    error = ERROR_DIRECTORY;
                    return false;
                }
                if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND)
                {
                    error = failure;
                    return false;
                }
                size_t previous = existing;
                while (previous && !Separator(path[previous - 1])) --previous;
                existing = previous ? previous - 1 : 0;
                directory[existing] = 0;
            }
            for (size_t i = existing; i <= parent; ++i)
            {
                if (i == parent || Separator(path[i]))
                {
                    directory[i] = 0;
                    if (i > existing)
                    {
                        if (!CreateDirectoryW(directory, nullptr))
                        {
                            const DWORD failure = GetLastError();
                            const DWORD attributes = GetFileAttributesW(directory);
                            if (failure != ERROR_ALREADY_EXISTS || attributes == INVALID_FILE_ATTRIBUTES ||
                                !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                            {
                                error = failure;
                                return false;
                            }
                        }
                    }
                }
                if (i < parent) directory[i] = path[i];
            }
            return true;
        }

        const char* SeverityName(log::Severity severity) noexcept
        {
            switch (severity)
            {
            case log::Severity::Debug: return "debug";
            case log::Severity::Info: return "info";
            case log::Severity::Warning: return "warning";
            case log::Severity::Error: return "error";
            case log::Severity::Fatal: return "fatal";
            default: return "none";
            }
        }

        int64_t MonotonicTime() noexcept
        {
            return g_LogClock.nanoseconds ? g_LogClock.nanoseconds(g_LogClock.context) :
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        bool Write(const char* data, size_t size) noexcept
        {
            return !UVSR_LOG_FAIL(Write) && fwrite(data, 1, size, g_Log) == size;
        }

        bool WriteLine(log::Severity severity, const char* message) noexcept
        {
            const time_t now = time(nullptr);
            tm local{};
            char timestamp[64]{};
            char header[96]{};
            if (localtime_s(&local, &now) != 0 ||
                !strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local)) return false;
            const int length = snprintf(header, sizeof(header), "%s [%s] ", timestamp, SeverityName(severity));
            return length > 0 && size_t(length) < sizeof(header) &&
                Write(header, size_t(length)) && Write(message, strlen(message)) && Write("\n", 1);
        }

        bool WriteSuppressedSummary() noexcept
        {
            if (!g_SuppressedRepeatCount) return true;
            char message[96];
            const int length = snprintf(message, sizeof(message),
                "Previous warning repeated %llu additional times",
                static_cast<unsigned long long>(g_SuppressedRepeatCount));
            g_SuppressedRepeatCount = 0;
            return length > 0 && size_t(length) < sizeof(message) && WriteLine(log::Severity::Warning, message);
        }

        bool Flush() noexcept
        {
            const int result = fflush(g_Log);
            return !UVSR_LOG_FAIL(Flush) && result == 0;
        }

        void ReportFileFailure(log::Callback downstream) noexcept
        {
            if (downstream.function)
                downstream.function(downstream.context, log::Severity::Warning,
                    "UVSR engine diagnostic log file I/O failed; messages continue through the previous log callback");
        }

        void DiagnosticCallback(void*, log::Severity severity, const char* message)
        {
            log::Callback downstream;
            bool reportFailure = false;
            {
                LogLock lock;
                if (!g_LogCallbackInstalled) return;
                if (!g_WriteFailed)
                {
                    const int64_t now = MonotonicTime();
                    const char* safeMessage = message ? message : "";
                    const size_t length = strlen(safeMessage);
                    const bool repeat = severity == log::Severity::Warning &&
                        g_LastSeverity == severity && strcmp(g_LastMessage, safeMessage) == 0;
                    const bool suppress = repeat && now - g_LastMessageWrite < 5'000'000'000;
                    bool wroteLine = false;
                    bool success = true;
                    if (suppress)
                        ++g_SuppressedRepeatCount;
                    else
                    {
                        success = WriteSuppressedSummary() && WriteLine(severity, safeMessage);
                        // ordinary dispatch is bounded. an oversized direct callback
                        // keeps its full text without coalescing a truncated key.
                        g_LastSeverity = length < sizeof(g_LastMessage) ? severity : log::Severity::None;
                        if (length < sizeof(g_LastMessage)) memcpy(g_LastMessage, safeMessage, length + 1);
                        else g_LastMessage[0] = 0;
                        g_LastMessageWrite = now;
                        wroteLine = true;
                    }
                    const bool urgent = severity == log::Severity::Error || severity == log::Severity::Fatal;
                    if (success && (urgent || (wroteLine && now - g_LastFlush >= 1'000'000'000)))
                    {
                        success = Flush();
                        g_LastFlush = now;
                    }
                    if (!success) g_WriteFailed = reportFailure = true;
                }
                downstream = g_Downstream;
            }
            if (downstream.function) downstream.function(downstream.context, severity, message);
            if (reportFailure) ReportFileFailure(downstream);
        }
    }

    bool InitializeEngineDiagnosticLog(const wchar_t* path, EngineDiagnosticLogClock clock) noexcept
    {
        ShutdownEngineDiagnosticLog();
        if (!path || !*path) return false;
        PreparedPath prepared;
        DWORD error = 0;
        if (!PreparePath(path, prepared, error))
        {
            log::warning("UVSR could not prepare its engine diagnostic log path (Win32 error %lu)", error);
            return false;
        }
        if (!CreateParentDirectories(path, prepared.directory, error))
        {
            log::warning("UVSR could not create its engine diagnostic log directory (Win32 error %lu)", error);
            return false;
        }
        FILE* file = UVSR_LOG_FAIL(Open) ? nullptr : _wfsopen(path, L"w", _SH_DENYNO);
        if (!file)
        {
            log::warning("UVSR could not open its engine diagnostic log: %s", prepared.display);
            return false;
        }
        if (UVSR_LOG_FAIL(Buffer) || setvbuf(file, g_FileBuffer, _IOFBF, sizeof(g_FileBuffer)) != 0)
        {
            fclose(file);
            log::warning("UVSR could not prepare its engine diagnostic log buffer");
            return false;
        }
        if (!g_ShutdownRegistered)
        {
            if (atexit(ShutdownEngineDiagnosticLog) != 0)
            {
                fclose(file);
                log::warning("UVSR could not register its engine diagnostic log shutdown");
                return false;
            }
            g_ShutdownRegistered = true;
        }
        {
            LogLock lock;
            g_Log = file;
            g_LogClock = clock;
            g_LastFlush = g_LastMessageWrite = MonotonicTime();
            g_LastSeverity = log::Severity::None;
            g_LastMessage[0] = 0;
            g_SuppressedRepeatCount = 0;
            g_WriteFailed = false;
            g_Downstream = log::GetCallback();
            g_LogCallbackInstalled = true;
        }
        log::SetCallback({DiagnosticCallback, nullptr});
        log::info("Engine diagnostic log: %s", prepared.display);
        bool success;
        {
            LogLock lock;
            success = !g_WriteFailed;
        }
        if (!success) ShutdownEngineDiagnosticLog();
        return success;
    }

    void ShutdownEngineDiagnosticLog() noexcept
    {
        log::Callback downstream;
        bool reportFailure = false;
        {
            LogLock lock;
            if (!g_LogCallbackInstalled) return;
            bool success = g_WriteFailed || WriteSuppressedSummary();
            if (!Flush()) success = false;
            const int closed = fclose(g_Log);
            if (UVSR_LOG_FAIL(Close) || closed != 0) success = false;
            reportFailure = !g_WriteFailed && !success;
            g_Log = nullptr;
            downstream = g_Downstream;
            g_Downstream = {};
            g_LogClock = {};
            g_LastSeverity = log::Severity::None;
            g_LastMessage[0] = 0;
            g_LastMessageWrite = g_LastFlush = 0;
            g_SuppressedRepeatCount = 0;
            g_WriteFailed = false;
            g_LogCallbackInstalled = false;
        }
        log::SetCallback(downstream);
        if (reportFailure) ReportFileFailure(downstream);
    }

#if defined(UVSR_ENGINE_LOG_TEST_HOOKS)
    void FailEngineDiagnosticLogOnce(EngineDiagnosticLogFailure failure) noexcept { g_NextFailure = failure; }
#endif
#undef UVSR_LOG_FAIL
}
