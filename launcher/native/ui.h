#pragma once
#include "installer.h"
#include <map>

namespace uvsr::launcher::ui
{
    struct Palette
    {
        COLORREF window, card, text, muted, accent, border, success, danger, disabled;
        static Palette Current();
    };
    class Fonts
    {
        std::map<std::pair<int, bool>, HFONT> cache;
        UINT dpi;
    public:
        explicit Fonts(UINT dpi) : dpi(dpi) {}
        ~Fonts();
        HFONT Get(int tenthsOfPoint = 100, bool bold = false);
    };
    void EnsureFonts();
    std::string ResourceText(int id);
    int Scale(HWND window, int value);
    void FitWindow(HWND window, int width, int height, bool center);
    HWND Control(HWND owner, const wchar_t* type, std::wstring_view text, DWORD style, int id, HFONT font);
    void Move(HWND control, int x, int y, int width, int height);
    void DrawButton(const DRAWITEMSTRUCT& draw, HFONT font, const Palette& colors, bool primary = false, bool danger = false);
    void CopyText(HWND owner, std::wstring_view text);
    struct Action { std::string id; std::wstring label; bool primary = false, cancel = false, danger = false; };
    struct Choice { std::wstring label, detail; bool enabled = false, selected = false; };
    std::string Dialog(HWND owner, std::wstring title, std::wstring text, std::vector<Action> actions = {},
        bool error = false, std::vector<Choice>* choices = nullptr, bool scrollableText = false);
    int Run(Installer& installer, bool uninstall, std::optional<std::string> continuation);
}
