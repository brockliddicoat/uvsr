#include "internal.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <charconv>
#include <limits>

namespace
{
    using namespace uvsr::launcher;
    namespace fs = std::filesystem;

    [[nodiscard]] bool same_argument(
        std::wstring_view left,
        std::wstring_view right) noexcept
    {
        return left.size() == right.size() &&
            CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                right.data(), static_cast<int>(right.size()), TRUE) ==
                CSTR_EQUAL;
    }

    template<typename T>
    [[nodiscard]] std::optional<T> parse_unsigned(std::wstring_view text)
    {
        if (text.empty())
            return std::nullopt;
        const std::string value = narrow(text);
        T result{};
        const auto parsed = std::from_chars(value.data(),
            value.data() + value.size(), result);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != value.data() + value.size())
            return std::nullopt;
        return result;
    }

    [[nodiscard]] std::optional<std::int64_t> parse_sequence(
        std::wstring_view text)
    {
        if (text.empty())
            return std::nullopt;
        const std::string value = narrow(text);
        std::int64_t result = 0;
        const auto parsed = std::from_chars(value.data(),
            value.data() + value.size(), result);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != value.data() + value.size())
            return std::nullopt;
        return result;
    }

    [[nodiscard]] std::optional<GUID> parse_argument_guid(
        std::wstring_view text) noexcept
    {
        try
        {
            std::wstring value(text);
            if (value.empty())
                return std::nullopt;
            if (value.front() != L'{')
                value = L"{" + value + L"}";
            GUID result{};
            if (FAILED(CLSIDFromString(value.c_str(), &result)))
                return std::nullopt;
            return result;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    [[nodiscard]] std::vector<std::wstring> command_arguments()
    {
        int count = 0;
        wchar_t** values = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!values || count < 1)
            throw Error("Windows could not read the launcher command line.");
        std::vector<std::wstring> result;
        try
        {
            result.reserve(static_cast<std::size_t>(count - 1));
            for (int index = 1; index < count; ++index)
                result.emplace_back(values[index]);
        }
        catch (...)
        {
            LocalFree(values);
            throw;
        }
        LocalFree(values);
        return result;
    }

    [[nodiscard]] bool current_process_elevated()
    {
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
            detail::throw_last_error(
                "Windows could not inspect launcher permissions");
        struct TokenCloser
        {
            HANDLE value;
            ~TokenCloser() { CloseHandle(value); }
        } token{ rawToken };
        TOKEN_ELEVATION elevation{};
        DWORD bytes = 0;
        if (!GetTokenInformation(token.value, TokenElevation, &elevation,
                sizeof(elevation), &bytes))
            detail::throw_last_error(
                "Windows could not inspect launcher permissions");
        return elevation.TokenIsElevated != 0;
    }

    void show_error(
        std::wstring_view title,
        std::wstring_view message) noexcept
    {
        std::wstring body(message);
        try
        {
            ensure_fonts();
            TASKDIALOGCONFIG dialog{};
            dialog.cbSize = sizeof(dialog);
            dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION |
                TDF_SIZE_TO_CONTENT;
            const std::wstring heading(title);
            dialog.pszWindowTitle = heading.c_str();
            dialog.pszMainInstruction = heading.c_str();
            dialog.pszContent = body.c_str();
            dialog.pszMainIcon = TD_ERROR_ICON;
            dialog.dwCommonButtons = TDCBF_OK_BUTTON;
            if (SUCCEEDED(TaskDialogIndirect(&dialog, nullptr, nullptr,
                    nullptr)))
                return;
        }
        catch (const std::exception& error)
        {
            try
            {
                body = widen(error.what()) + L"\r\n\r\n" + body;
            }
            catch (...)
            {
            }
        }
        MessageBoxW(nullptr, body.c_str(), std::wstring(title).c_str(),
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }

    void show_cleanup_failure(int result) noexcept
    {
        std::wstring reason;
        switch (result)
        {
        case 3:
            reason = L"UVSR Launcher could not verify the pending uninstall record.";
            break;
        case 4:
            reason = L"UVSR or UVSR Launcher is still running.";
            break;
        case 5:
        case 6:
            reason = L"Windows interrupted the uninstall before all files were secured.";
            break;
        case 7:
            reason = L"Windows could not finish the pending uninstall record.";
            break;
        case 8:
            reason = L"UVSR was removed, but Windows could not remove a temporary cleanup copy.";
            break;
        default:
            reason = L"Windows could not finish removing UVSR.";
            break;
        }
        show_error(L"UVSR Uninstall Needs Attention",
            reason +
                L" Reopen the UVSR Launcher file you originally downloaded; it will safely resume the uninstall and keep your renderer settings and history.");
    }

    [[nodiscard]] int run_health_check(
        const std::vector<std::wstring>& arguments)
    {
        const auto sequence = parse_sequence(arguments[1]);
        if (!sequence || *sequence != LauncherReleaseSequence ||
            arguments[2] != widen(LauncherVersion))
            return 3;
        try
        {
            ensure_fonts();
            return 0;
        }
        catch (...)
        {
            return 4;
        }
    }

    [[nodiscard]] std::optional<int> try_cleanup(
        const std::vector<std::wstring>& arguments)
    {
        if (arguments.size() != 5u ||
            !same_argument(arguments[0], L"--cleanup"))
            return std::nullopt;
        const auto process = parse_unsigned<std::uint32_t>(arguments[1]);
        const auto creation = parse_unsigned<std::uint64_t>(arguments[2]);
        const auto installation = parse_argument_guid(arguments[3]);
        const auto transaction = parse_argument_guid(arguments[4]);
        if (!process || *process == 0 || !creation || !installation ||
            !transaction)
            return std::nullopt;
        int result = run_cleanup_helper(static_cast<DWORD>(*process),
            *creation, *installation, *transaction);
        if (result != 0)
            show_cleanup_failure(result);
        if (!schedule_current_helper_deletion(*installation) && result == 0)
        {
            result = 8;
            show_cleanup_failure(result);
        }
        return result;
    }

    class ComApartment final
    {
    public:
        ComApartment()
        {
            const HRESULT result = CoInitializeEx(nullptr,
                COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (FAILED(result))
                throw Error("Windows could not initialize launcher services.");
            m_Initialized = true;
        }

        ~ComApartment()
        {
            if (m_Initialized)
                CoUninitialize();
        }

    private:
        bool m_Initialized = false;
    };
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    using namespace uvsr::launcher;
    static_cast<void>(SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    try
    {
        const std::vector<std::wstring> arguments = command_arguments();
        if (arguments.size() == 3u &&
            same_argument(arguments[0], L"--launcher-health-check"))
            return run_health_check(arguments);
        if (const auto result = try_cleanup(arguments))
            return *result;

        ComApartment apartment;
        InstallerEngine::ensure_supported_platform();
        if (current_process_elevated())
        {
            show_error(L"UVSR Launcher",
                L"UVSR Launcher must run as your normal Windows user so its installation remains per-user. Close this copy, then open it normally (do not choose Run as administrator)."
            );
            return 1;
        }
        if (arguments.size() == 1u &&
            same_argument(arguments[0], L"--ui-preview"))
        {
            wchar_t temporary[MAX_PATH + 1]{};
            const DWORD length = GetTempPathW(
                static_cast<DWORD>(std::size(temporary)), temporary);
            if (length == 0 ||
                static_cast<std::size_t>(length) >= std::size(temporary))
                detail::throw_last_error(
                    "Windows could not locate temporary storage");
            const fs::path previewRoot = fs::path(temporary) /
                L"UVSR Launcher UI Preview" /
                widen(guid_string(detail::new_guid()));
            InstallerEngine preview(InstallerPaths::create(previewRoot,
                previewRoot / L"Desktop", previewRoot / L"Programs"));
            return run_ui(instance, preview, false, std::nullopt);
        }

        const InstallerPaths paths = InstallerPaths::current_user();
        recover_interrupted_uninstall(paths);
        remove_stale_helpers(paths);
        const auto marker = inspect_owner(paths);
        if (marker && redirect_to_active_launcher(paths, *marker, arguments))
            return 0;

        InstallerEngine engine(paths);
        if (arguments.size() == 1u &&
            same_argument(arguments[0], L"--launch"))
        {
            engine.launch_installed();
            return 0;
        }
        const bool uninstall = arguments.size() == 1u &&
            same_argument(arguments[0], L"--uninstall");
        std::optional<GUID> continuation;
        if (arguments.size() == 2u &&
            same_argument(arguments[0], L"--continue-uvsr-update"))
            continuation = parse_argument_guid(arguments[1]);
        if (!arguments.empty() && !uninstall && !continuation)
            throw Error("UVSR Launcher received an unsupported command.");
        return run_ui(instance, engine, uninstall, continuation);
    }
    catch (const Error& error)
    {
        show_error(L"UVSR Launcher", widen(error.what()));
        return 1;
    }
    catch (const std::exception& error)
    {
        show_error(L"UVSR Launcher",
            L"UVSR Launcher stopped before making a change. " +
                widen(error.what()));
        return 2;
    }
    catch (...)
    {
        show_error(L"UVSR Launcher",
            L"UVSR Launcher stopped before making a change.");
        return 2;
    }
}
