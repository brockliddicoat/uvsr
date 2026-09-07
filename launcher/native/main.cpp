#include "ui.h"
#include <shellapi.h>
#include <objbase.h>
#include <array>

namespace
{
    using namespace uvsr::launcher;
    int Run(std::span<const std::wstring> arguments)
    {
        if (arguments.size() == 3 && Lower(Utf8(arguments[0])) == "--launcher-health-check")
        {
            if (Utf8(arguments[1]) != std::to_string(LauncherSequence) || Utf8(arguments[2]) != LauncherVersion) return 3;
            try { ui::EnsureFonts(); return 0; } catch (...) { return 4; }
        }
        if (arguments.size() == 5 && Lower(Utf8(arguments[0])) == "--cleanup")
        {
            const auto integer = [](const std::wstring& text)
            {
                const auto value = Utf8(text); uint64_t number = 0;
                auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
                Require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(), "The cleanup process identity is invalid."); return number;
            };
            auto parent = integer(arguments[1]), ticks = integer(arguments[2]); Require(parent <= UINT32_MAX, "The cleanup process identity is invalid.");
            Installer installer(Paths::CurrentUser());
            const int result = installer.Cleanup(DWORD(parent), ticks, Utf8(arguments[3]), Utf8(arguments[4]));
            if (result) ui::Dialog(nullptr, L"UVSR Uninstall Needs Attention", L"Windows could not finish removing UVSR. Reopen the UVSR Launcher file you originally downloaded; it will safely resume the uninstall and keep your renderer settings and history.", {}, true);
            return result;
        }
        EnsurePlatform();
        if (arguments.size() == 1 && Lower(Utf8(arguments[0])) == "--ui-preview")
        {
            const auto root = fs::temp_directory_path() / "UVSR Launcher UI Preview" / Guid();
            Installer installer(Paths::Create(root, root / "Desktop", root / "Programs"));
            return ui::Run(installer, false, {});
        }
        Installer installer(Paths::CurrentUser()); installer.RecoverUninstall();
        if (installer.Redirect(arguments)) return 0;
        if (arguments.size() == 1 && Lower(Utf8(arguments[0])) == "--launch") { installer.Launch(); return 0; }
        const bool uninstall = arguments.size() == 1 && Lower(Utf8(arguments[0])) == "--uninstall";
        std::optional<std::string> continuation;
        if (arguments.size() == 2 && Lower(Utf8(arguments[0])) == "--continue-uvsr-update" && IsGuid(Utf8(arguments[1]))) continuation = Utf8(arguments[1]);
        Require(arguments.empty() || uninstall || continuation, "UVSR Launcher received an unsupported command.");
        return ui::Run(installer, uninstall, continuation);
    }
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 1;
    try
    {
        int count = 0; LPWSTR* words = CommandLineToArgvW(GetCommandLineW(), &count);
        uvsr::launcher::WinCheck(words != nullptr, "Read launcher arguments");
        std::vector<std::wstring> arguments;
        for (int i = 1; i < count; ++i) arguments.emplace_back(words[i]); LocalFree(words);
        result = Run(arguments);
    }
    catch (const std::exception& error)
    {
        try { uvsr::launcher::ui::Dialog(nullptr, L"UVSR Launcher", uvsr::launcher::Wide(error.what()), {}, true); }
        catch (...) { MessageBoxW(nullptr, uvsr::launcher::Wide(error.what()).c_str(), L"UVSR Launcher", MB_OK | MB_ICONERROR); }
    }
    if (SUCCEEDED(com)) CoUninitialize(); return result;
}
