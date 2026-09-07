#include "ui.h"
#include <commctrl.h>
#include <algorithm>
#include <array>

namespace uvsr::launcher::ui
{
    Palette Palette::Current()
    {
        HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
        if (contrast.dwFlags & HCF_HIGHCONTRASTON)
            return {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_GRAYTEXT),
                GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_BTNFACE)};
        return {RGB(245,247,250), RGB(252,253,255), RGB(20,27,38), RGB(79,89,104), RGB(38,99,235), RGB(218,224,232), RGB(31,122,82), RGB(185,48,48), RGB(232,236,241)};
    }
    namespace
    {
        std::span<const unsigned char> Resource(int id)
        {
            const auto module = GetModuleHandleW(nullptr);
            auto info = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
            Require(info != nullptr, "The launcher resource bundle is incomplete.");
            const auto size = SizeofResource(module, info);
            const auto* bytes = static_cast<const unsigned char*>(LockResource(LoadResource(module, info)));
            Require(bytes && size, "The launcher resource could not be read."); return {bytes, size};
        }
        struct FontResources
        {
            std::array<HANDLE, 2> handles{};
            FontResources()
            {
                try
                {
                    const std::array hashes{
                        "478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823",
                        "1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913"};
                    for (size_t index = 0; index < handles.size(); ++index)
                    {
                        auto bytes = Resource(101 + int(index)); auto hash = HashBytes(bytes); std::string actual;
                        for (auto byte : hash) { actual += "0123456789abcdef"[byte >> 4]; actual += "0123456789abcdef"[byte & 15]; }
                        Require(HashEqual(actual, hashes[index]), "The embedded Noto Sans font failed its integrity check.");
                        DWORD count = 0; handles[index] = AddFontMemResourceEx(const_cast<unsigned char*>(bytes.data()), DWORD(bytes.size()), nullptr, &count);
                        Require(handles[index] && count == 1, "Windows could not load the embedded Noto Sans fonts.");
                    }
                }
                catch (...) { for (auto handle : handles) if (handle) RemoveFontMemResourceEx(handle); throw; }
            }
            ~FontResources() { for (auto handle : handles) if (handle) RemoveFontMemResourceEx(handle); }
        };
    }
    void EnsureFonts() { static FontResources fonts; }
    std::string ResourceText(int id)
    { const auto bytes = Resource(id); return {reinterpret_cast<const char*>(bytes.data()), bytes.size()}; }
    Fonts::~Fonts() { for (const auto& [key, font] : cache) DeleteObject(font); }
    HFONT Fonts::Get(int points, bool bold)
    {
        EnsureFonts(); auto key = std::pair(points, bold);
        if (auto it = cache.find(key); it != cache.end()) return it->second;
        auto font = CreateFontW(-MulDiv(points, int(dpi), 720), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Noto Sans");
        WinCheck(font != nullptr, "Create launcher typeface"); cache.emplace(key, font); return font;
    }
    int Scale(HWND window, int value) { return MulDiv(value, int(GetDpiForWindow(window)), 96); }
    void FitWindow(HWND window, int width, int height, bool center)
    {
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        WinCheck(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor), "Find the launcher work area");
        const int margin = Scale(window, 16); RECT available = monitor.rcWork; InflateRect(&available, -margin, -margin);
        RECT desired{0, 0, Scale(window, width), Scale(window, height)};
        AdjustWindowRectExForDpi(&desired, DWORD(GetWindowLongPtrW(window, GWL_STYLE)), FALSE, DWORD(GetWindowLongPtrW(window, GWL_EXSTYLE)), GetDpiForWindow(window));
        int w = std::min(desired.right - desired.left, available.right - available.left), h = std::min(desired.bottom - desired.top, available.bottom - available.top);
        RECT current{}; GetWindowRect(window, &current);
        int x = center ? available.left + (available.right - available.left - w) / 2 : std::clamp(current.left, available.left, available.right - w);
        int y = center ? available.top + (available.bottom - available.top - h) / 2 : std::clamp(current.top, available.top, available.bottom - h);
        SetWindowPos(window, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    HWND Control(HWND owner, const wchar_t* type, std::wstring_view text, DWORD style, int id, HFONT font)
    {
        const auto label = std::wstring(text);
        auto control = CreateWindowExW(0, type, label.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, owner,
            reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        WinCheck(control != nullptr, "Create launcher control"); SendMessageW(control, WM_SETFONT, WPARAM(font), FALSE); return control;
    }
    void Move(HWND control, int x, int y, int width, int height)
    { MoveWindow(control, x, y, std::max(1, width), std::max(1, height), TRUE); }
    void DrawButton(const DRAWITEMSTRUCT& draw, HFONT font, const Palette& palette, bool primary, bool danger)
    {
        const bool enabled = !(draw.itemState & ODS_DISABLED), pressed = draw.itemState & ODS_SELECTED;
        COLORREF fill = !enabled ? palette.disabled : primary ? palette.accent : palette.card;
        COLORREF text = !enabled ? palette.muted : primary ? GetSysColor(COLOR_HIGHLIGHTTEXT) : danger ? palette.danger : palette.text;
        if (pressed && enabled) fill = primary ? RGB(29,78,216) : palette.disabled;
        HBRUSH brush = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, primary ? fill : palette.border);
        auto oldBrush = SelectObject(draw.hDC, brush), oldPen = SelectObject(draw.hDC, pen), oldFont = SelectObject(draw.hDC, font);
        RoundRect(draw.hDC, draw.rcItem.left, draw.rcItem.top, draw.rcItem.right, draw.rcItem.bottom, Scale(draw.hwndItem, 8), Scale(draw.hwndItem, 8));
        wchar_t label[256]{}; GetWindowTextW(draw.hwndItem, label, 256);
        SetBkMode(draw.hDC, TRANSPARENT); SetTextColor(draw.hDC, text);
        auto rect = draw.rcItem; DrawTextW(draw.hDC, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (draw.itemState & ODS_FOCUS) { InflateRect(&rect, -4, -4); DrawFocusRect(draw.hDC, &rect); }
        SelectObject(draw.hDC, oldFont); SelectObject(draw.hDC, oldPen); SelectObject(draw.hDC, oldBrush); DeleteObject(pen); DeleteObject(brush);
    }
    void CopyText(HWND owner, std::wstring_view text)
    {
        WinCheck(OpenClipboard(owner), "Open the clipboard");
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (!memory) { CloseClipboard(); throw std::runtime_error("Windows could not allocate clipboard text."); }
        auto* destination = static_cast<wchar_t*>(GlobalLock(memory));
        if (!destination) { GlobalFree(memory); CloseClipboard(); throw std::runtime_error("Windows could not write clipboard text."); }
        std::copy(text.begin(), text.end(), destination); destination[text.size()] = 0; GlobalUnlock(memory);
        const BOOL result = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        CloseClipboard(); if (!result) GlobalFree(memory); WinCheck(result, "Copy operation details");
    }
    namespace
    {
        struct Modal
        {
            std::wstring title, text;
            std::vector<Action> actions;
            std::vector<Choice>* choices;
            bool error, scrollable;
            std::unique_ptr<Fonts> fonts;
            Palette palette = Palette::Current();
            HBRUSH brush = CreateSolidBrush(palette.window);
            std::string result;
            HWND heading = nullptr, body = nullptr;
            std::vector<HWND> buttons, checks, details;
            int cancel = 0, primary = 0;
            ~Modal() { DeleteObject(brush); }
            void Layout(HWND window)
            {
                RECT client{}; GetClientRect(window, &client); auto s = [&](int x) { return Scale(window, x); };
                int width = client.right - s(48), y = s(24);
                Move(heading, s(24), y, width, s(38)); y += s(46);
                if (scrollable) Move(body, s(24), y, width, client.bottom - y - s(88));
                else
                {
                    HDC dc = GetDC(window); auto old = SelectObject(dc, fonts->Get());
                    RECT size{0,0,width,0}; DrawTextW(dc, text.c_str(), -1, &size, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                    SelectObject(dc, old); ReleaseDC(window, dc);
                    int height = std::min(int(size.bottom), std::max(s(40), int(client.bottom) - y - s(88) - int(checks.size()) * s(84)));
                    Move(body, s(24), y, width, height); y += height + s(12);
                    for (size_t i = 0; i < checks.size(); ++i)
                    {
                        Move(checks[i], s(24), y, width, s(26)); y += s(28);
                        Move(details[i], s(50), y, width - s(26), s(52)); y += s(56);
                    }
                }
                int x = client.right - s(24);
                for (size_t i = buttons.size(); i-- > 0;)
                { int w = s(actions[i].label.size() > 13 ? 170 : 130); x -= w; Move(buttons[i], x, client.bottom - s(68), w, s(44)); x -= s(8); }
            }
            void Availability()
            {
                if (!choices) return;
                bool selected = false;
                for (size_t i = 0; i < checks.size(); ++i)
                { (*choices)[i].selected = SendMessageW(checks[i], BM_GETCHECK, 0, 0) == BST_CHECKED; selected |= (*choices)[i].selected; }
                for (size_t i = 0; i < actions.size(); ++i) if (actions[i].id == "update") EnableWindow(buttons[i], selected);
            }
            static INT_PTR CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
            {
                auto* self = reinterpret_cast<Modal*>(GetWindowLongPtrW(window, DWLP_USER));
                try
                {
                    if (message == WM_INITDIALOG)
                    {
                        self = reinterpret_cast<Modal*>(lParam); SetWindowLongPtrW(window, DWLP_USER, LONG_PTR(self));
                        self->fonts = std::make_unique<Fonts>(GetDpiForWindow(window)); SetWindowTextW(window, self->title.c_str());
                        self->heading = Control(window, L"STATIC", self->title, 0, 10, self->fonts->Get(160, true));
                        self->body = Control(window, L"EDIT", self->text,
                            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 11, self->fonts->Get());
                        if (self->choices)
                            for (size_t i = 0; i < self->choices->size(); ++i)
                            {
                                auto& choice = (*self->choices)[i];
                                auto check = Control(window, L"BUTTON", choice.label, BS_AUTOCHECKBOX | WS_TABSTOP, 200 + int(i), self->fonts->Get(110, true));
                                SendMessageW(check, BM_SETCHECK, choice.selected ? BST_CHECKED : BST_UNCHECKED, 0); EnableWindow(check, choice.enabled);
                                self->checks.push_back(check);
                                self->details.push_back(Control(window, L"STATIC", choice.detail, SS_NOPREFIX, 300 + int(i), self->fonts->Get()));
                            }
                        for (size_t i = 0; i < self->actions.size(); ++i)
                        {
                            auto& action = self->actions[i]; const int id = 100 + int(i);
                            self->buttons.push_back(Control(window, L"BUTTON", action.label, BS_OWNERDRAW | WS_TABSTOP, id, self->fonts->Get()));
                            if (action.cancel) self->cancel = id;
                            if (action.primary) self->primary = id;
                        }
                        if (!self->cancel) self->cancel = 100 + int(self->actions.size()) - 1;
                        if (!self->primary) self->primary = 100;
                        SendMessageW(window, DM_SETDEFID, self->primary, 0);
                        self->Availability();
                        FitWindow(window, self->scrollable ? 900 : self->choices ? 620 : 540, self->scrollable ? 700 : self->choices ? 420 : 270, true);
                        self->Layout(window); SetFocus(GetDlgItem(window, self->primary)); return FALSE;
                    }
                    if (!self) return FALSE;
                    if (message == WM_COMMAND)
                    {
                        int id = LOWORD(wParam); if (id == IDCANCEL) id = self->cancel; if (id == IDOK) id = self->primary;
                        if (id >= 100 && id < 100 + int(self->actions.size()) && IsWindowEnabled(GetDlgItem(window, id)))
                        { self->Availability(); self->result = self->actions[size_t(id - 100)].id; EndDialog(window, id); return TRUE; }
                        self->Availability();
                    }
                    if (message == WM_CLOSE) { SendMessageW(window, WM_COMMAND, self->cancel, 0); return TRUE; }
                    if (message == WM_SIZE) { self->Layout(window); return TRUE; }
                    if (message == WM_DPICHANGED)
                    {
                        self->fonts = std::make_unique<Fonts>(HIWORD(wParam));
                        EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL { SendMessageW(child, WM_SETFONT, WPARAM(reinterpret_cast<Fonts*>(data)->Get()), TRUE); return TRUE; }, LPARAM(self->fonts.get()));
                        FitWindow(window, self->scrollable ? 900 : self->choices ? 620 : 540, self->scrollable ? 700 : self->choices ? 420 : 270, false); return TRUE;
                    }
                    if (message == WM_SETTINGCHANGE)
                    {
                        self->palette = Palette::Current(); DeleteObject(self->brush); self->brush = CreateSolidBrush(self->palette.window);
                        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN); return TRUE;
                    }
                    if (message == WM_DRAWITEM)
                    { const auto& draw = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam); const auto& action = self->actions.at(size_t(draw.CtlID - 100)); DrawButton(draw, self->fonts->Get(), self->palette, action.primary, action.danger); return TRUE; }
                    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORBTN)
                    {
                        SetBkColor(HDC(wParam), self->palette.window); SetTextColor(HDC(wParam), self->error && HWND(lParam) == self->body ? self->palette.danger : self->palette.text);
                        return INT_PTR(self->brush);
                    }
                }
                catch (const std::exception& error) { if (self) self->result = "error"; MessageBoxW(window, Wide(error.what()).c_str(), L"UVSR Launcher", MB_OK | MB_ICONERROR); EndDialog(window, -1); }
                return FALSE;
            }
        };
    }
    std::string Dialog(HWND owner, std::wstring title, std::wstring text, std::vector<Action> actions, bool error, std::vector<Choice>* choices, bool scrollable)
    {
        if (actions.empty()) actions.push_back({"ok", L"OK", true, true});
        Modal modal{std::move(title), std::move(text), std::move(actions), choices, error, scrollable};
        struct EmptyDialog { DLGTEMPLATE dialog; WORD menu = 0, windowClass = 0, title = 0; } layout{};
        layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
        layout.dialog.dwExtendedStyle = WS_EX_CONTROLPARENT;
        layout.dialog.cx = 360; layout.dialog.cy = 180;
        auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &layout.dialog, owner, Modal::Proc, LPARAM(&modal));
        Require(result != -1, "Windows could not show the launcher dialog."); return modal.result;
    }
}
