#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc == 2 && (std::wstring_view(argv[1]) == L"--window" || std::wstring_view(argv[1]) == L"--ignore-close"))
    {
        const bool ignore = std::wstring_view(argv[1]) == L"--ignore-close";
        WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"UVSR.NativeProcessFixture";
        cls.lpfnWndProc = [](HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT
        {
            if (message == WM_CLOSE)
            { if (!GetWindowLongPtrW(window, GWLP_USERDATA)) DestroyWindow(window); return 0; }
            if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
            return DefWindowProcW(window, message, wParam, lParam);
        };
        if (!RegisterClassW(&cls)) return 21;
        HWND window = CreateWindowW(cls.lpszClassName, L"UVSR process fixture", WS_OVERLAPPEDWINDOW,
            0, 0, 100, 100, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) return 22;
        SetWindowLongPtrW(window, GWLP_USERDATA, ignore);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
        return 0;
    }
    if (argc == 2 && std::wstring_view(argv[1]) == L"--descendant") { Sleep(120000); return 0; }
    wchar_t name[32768]{}; GetModuleFileNameW(nullptr, name, 32768);
    const auto root = std::filesystem::path(name).parent_path();
    std::string mode; std::ifstream(root / "health-mode.txt") >> mode;
    if (mode == "descendant" || mode == "cancel")
    {
        std::wstring command = L"\"" + std::wstring(name) + L"\" --descendant";
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        if (!CreateProcessW(name, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process)) return 20;
        std::ofstream(root / "descendant.pid") << process.dwProcessId;
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        if (mode == "cancel") Sleep(120000);
    }
    if (mode == "fail") return 9;
    return argc == 4 && std::wstring_view(argv[1]) == L"--launcher-health-check" ? 0 : 11;
}
