#include "internal.h"
#include "resource_ids.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <memory>
#include <thread>

namespace fs = std::filesystem;

namespace uvsr::launcher
{
    RendererClickAction reconcile_renderer_click(
        RendererButtonIntent rendered,
        ProcessState current) noexcept
    {
        if (rendered == RendererButtonIntent::Close &&
            current == ProcessState::Running)
            return RendererClickAction::Close;
        if (rendered == RendererButtonIntent::Launch &&
            current == ProcessState::NotRunning)
            return RendererClickAction::Launch;
        return RendererClickAction::None;
    }

    TerminalUiState renderer_active_state(RendererBusyAction action)
    {
        if (action == RendererBusyAction::Launching)
            return { L"Launching UVSR", L"Waiting for UVSR to start.",
                0, true, false };
        if (action == RendererBusyAction::Closing)
            return { L"Closing UVSR", L"Waiting for UVSR to close safely.",
                0, true, false };
        throw Error("A renderer action state was invalid.");
    }

    TerminalUiState renderer_success_state(RendererBusyAction action)
    {
        if (action == RendererBusyAction::Launching)
            return { L"UVSR Started", L"UVSR started successfully.",
                100, false, false };
        if (action == RendererBusyAction::Closing)
            return { L"UVSR Closed", L"UVSR closed successfully.",
                100, false, false };
        throw Error("A renderer action state was invalid.");
    }

    TerminalUiState update_terminal_state(
        UpdateFlowExit exit,
        std::wstring upToDateDetail)
    {
        switch (exit)
        {
        case UpdateFlowExit::UpToDate:
            return { L"Up to Date",
                upToDateDetail.empty()
                    ? L"UVSR Engine and UVSR Launcher are up to date."
                    : std::move(upToDateDetail), 100, false, false };
        case UpdateFlowExit::SelectionCancelled:
            return { L"Cancelled",
                L"The update selection was cancelled. Nothing installed was changed.",
                0, false, false };
        case UpdateFlowExit::NoSelection:
            return { L"Ready",
                L"No updates were selected. Nothing installed was changed.",
                0, false, false };
        case UpdateFlowExit::OperationCancelled:
            return { L"Cancelled",
                L"The update check was cancelled. Nothing installed was changed.",
                0, false, false };
        }
        throw Error("An update result state was invalid.");
    }

    std::wstring install_button_text(const InstallSnapshot& snapshot)
    {
        return snapshot.installed && !snapshot.damaged
            ? L"Installed" : L"Install";
    }

    namespace
    {
        constexpr COLORREF WindowColor = RGB(245, 247, 250);
        constexpr COLORREF CardColor = RGB(252, 253, 255);
        constexpr COLORREF TextColor = RGB(20, 27, 38);
        constexpr COLORREF MutedColor = RGB(79, 89, 104);
        constexpr COLORREF AccentColor = RGB(38, 99, 235);
        constexpr COLORREF BorderColor = RGB(218, 224, 232);
        constexpr COLORREF SuccessColor = RGB(31, 122, 82);
        constexpr COLORREF DangerColor = RGB(185, 48, 48);
        constexpr COLORREF DisabledColor = RGB(232, 236, 241);
        constexpr COLORREF LaunchColor = RGB(24, 114, 78);
        constexpr COLORREF CloseColor = RGB(186, 35, 35);

        constexpr UINT WorkCompleteMessage = WM_APP + 1u;
        constexpr UINT ProgressMessage = WM_APP + 2u;
        constexpr UINT LogMessage = WM_APP + 3u;
        constexpr UINT StartMessage = WM_APP + 4u;

        constexpr int InstallButton = 1001;
        constexpr int LaunchButton = 1002;
        constexpr int UpdateButton = 1003;
        constexpr int UninstallButton = 1004;
        constexpr int NoticesButton = 1005;
        constexpr int DetailsButton = 1006;
        constexpr int CopyButton = 1007;
        constexpr int ShortcutCheck = 1008;

        [[nodiscard]] bool high_contrast()
        {
            HIGHCONTRASTW value{};
            value.cbSize = sizeof(value);
            return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(value),
                       &value, 0) != FALSE &&
                (value.dwFlags & HCF_HIGHCONTRASTON) != 0;
        }

        [[nodiscard]] int scale(int value, UINT dpi)
        {
            return MulDiv(value, static_cast<int>(dpi), 96);
        }

        [[nodiscard]] HFONT make_font(
            UINT dpi,
            int points,
            int weight = FW_NORMAL)
        {
            HFONT font = CreateFontW(-MulDiv(points, static_cast<int>(dpi), 72),
                0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Noto Sans");
            if (!font)
                detail::throw_last_error("Windows could not create the launcher font");
            return font;
        }

        void set_font(HWND control, HFONT font)
        {
            SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(font), TRUE);
        }

        [[nodiscard]] std::wstring window_text(HWND window)
        {
            const int length = GetWindowTextLengthW(window);
            if (length <= 0)
                return {};
            std::wstring value(static_cast<std::size_t>(length) + 1u, L'\0');
            if (length > 0)
                GetWindowTextW(window, value.data(), length + 1);
            value.resize(static_cast<std::size_t>(length));
            return value;
        }

        struct DialogAction
        {
            int id = 0;
            std::wstring text;
        };

        [[nodiscard]] int show_dialog(
            HWND owner,
            std::wstring_view title,
            std::wstring_view message,
            std::initializer_list<DialogAction> actions,
            bool error = false)
        {
            std::vector<TASKDIALOG_BUTTON> buttons;
            buttons.reserve(actions.size());
            for (const auto& action : actions)
                buttons.push_back({ action.id, action.text.c_str() });
            const std::wstring windowTitle(title);
            TASKDIALOGCONFIG config{};
            config.cbSize = sizeof(config);
            config.hwndParent = owner;
            config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW |
                TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
            config.pszWindowTitle = windowTitle.c_str();
            const std::wstring heading(title);
            const std::wstring body(message);
            config.pszMainInstruction = heading.c_str();
            config.pszContent = body.c_str();
            config.pszMainIcon = error ? TD_ERROR_ICON : TD_INFORMATION_ICON;
            config.pButtons = buttons.data();
            config.cButtons = static_cast<UINT>(buttons.size());
            config.nDefaultButton = buttons.empty() ? 0 : buttons.front().nButtonID;
            int selected = 0;
            if (FAILED(TaskDialogIndirect(&config, &selected, nullptr, nullptr)))
            {
                MessageBoxW(owner, body.c_str(), heading.c_str(),
                    MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
                return buttons.empty() ? IDOK : buttons.back().nButtonID;
            }
            return selected;
        }

        struct ButtonStyle
        {
            bool primary = false;
            bool destructive = false;
            bool launch = false;
            bool close = false;
        };

        void draw_button(
            const DRAWITEMSTRUCT& item,
            ButtonStyle style)
        {
            const bool disabled = (item.itemState & ODS_DISABLED) != 0;
            const bool pressed = (item.itemState & ODS_SELECTED) != 0;
            COLORREF background = CardColor;
            COLORREF foreground = TextColor;
            COLORREF border = BorderColor;
            if (high_contrast())
            {
                background = GetSysColor(COLOR_BTNFACE);
                foreground = disabled ? GetSysColor(COLOR_GRAYTEXT)
                    : GetSysColor(COLOR_BTNTEXT);
                border = GetSysColor(COLOR_WINDOWTEXT);
            }
            else if (disabled)
            {
                background = DisabledColor;
                foreground = MutedColor;
            }
            else if (style.launch || style.close)
            {
                background = style.close ? CloseColor : LaunchColor;
                foreground = RGB(255, 255, 255);
                border = background;
            }
            else if (style.primary)
            {
                background = AccentColor;
                foreground = RGB(255, 255, 255);
                border = AccentColor;
            }
            else if (style.destructive)
                foreground = DangerColor;
            if (pressed && !disabled)
            {
                background = RGB(GetRValue(background) * 9 / 10,
                    GetGValue(background) * 9 / 10,
                    GetBValue(background) * 9 / 10);
            }
            HBRUSH fill = CreateSolidBrush(background);
            FillRect(item.hDC, &item.rcItem, fill);
            DeleteObject(fill);
            HPEN pen = CreatePen(PS_SOLID, 1, border);
            const HGDIOBJ oldPen = SelectObject(item.hDC, pen);
            const HGDIOBJ oldBrush = SelectObject(item.hDC,
                GetStockObject(NULL_BRUSH));
            Rectangle(item.hDC, item.rcItem.left, item.rcItem.top,
                item.rcItem.right, item.rcItem.bottom);
            SelectObject(item.hDC, oldBrush);
            SelectObject(item.hDC, oldPen);
            DeleteObject(pen);
            wchar_t text[128]{};
            GetWindowTextW(item.hwndItem, text,
                static_cast<int>(std::size(text)));
            SetBkMode(item.hDC, TRANSPARENT);
            SetTextColor(item.hDC, foreground);
            RECT textRect = item.rcItem;
            if (pressed)
                OffsetRect(&textRect, 1, 1);
            DrawTextW(item.hDC, text, -1, &textRect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if ((item.itemState & ODS_FOCUS) != 0)
            {
                RECT focus = item.rcItem;
                InflateRect(&focus, -3, -3);
                DrawFocusRect(item.hDC, &focus);
            }
        }

        struct UpdateSelection
        {
            bool accepted = false;
            bool retry = false;
            bool updateUvsr = false;
            bool updateLauncher = false;
        };

        struct UpdateDialogState
        {
            const UpdateCheckResult* updates = nullptr;
            UpdateSelection selection;
            HWND window = nullptr;
            HWND uvsr = nullptr;
            HWND launcher = nullptr;
            HWND update = nullptr;
            UINT dpi = 96;
            HFONT regular = nullptr;
            HFONT bold = nullptr;
            HBRUSH background = nullptr;
            std::exception_ptr callbackError;
        };

        constexpr wchar_t UpdateDialogClass[] = L"UVSRLauncherUpdateDialog";

        [[nodiscard]] bool selectable(ComponentUpdateState state) noexcept
        {
            return state == ComponentUpdateState::UpdateAvailable ||
                state == ComponentUpdateState::RepairNeeded;
        }

        LRESULT update_dialog_handle(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam)
        {
            auto* state = reinterpret_cast<UpdateDialogState*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
                state = static_cast<UpdateDialogState*>(create->lpCreateParams);
                state->window = window;
                SetWindowLongPtrW(window, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(state));
            }
            if (!state)
                return DefWindowProcW(window, message, wParam, lParam);
            switch (message)
            {
            case WM_CREATE:
            {
                state->dpi = GetDpiForWindow(window);
                state->regular = make_font(state->dpi, 10);
                state->bold = make_font(state->dpi, 11, FW_BOLD);
                state->background = CreateSolidBrush(WindowColor);
                auto createStatic = [&](std::wstring_view text, DWORD style,
                    int x, int y, int width, int height, HFONT font)
                {
                    HWND control = CreateWindowExW(0, L"STATIC",
                        std::wstring(text).c_str(), WS_CHILD | WS_VISIBLE | style,
                        scale(x, state->dpi), scale(y, state->dpi),
                        scale(width, state->dpi), scale(height, state->dpi),
                        window, nullptr, GetModuleHandleW(nullptr), nullptr);
                    set_font(control, font);
                    return control;
                };
                createStatic(L"Choose What to Update", SS_LEFT, 24, 20,
                    570, 32, state->bold);
                const bool failed =
                    state->updates->uvsr.state ==
                        ComponentUpdateState::CheckFailed ||
                    state->updates->launcher.state ==
                        ComponentUpdateState::CheckFailed;
                createStatic(failed
                        ? L"Available updates are selected. One or more checks failed; review the details or check again."
                        : L"Available updates are selected for you. You can install either one or both.",
                    SS_LEFT, 24, 55, 570, 44, state->regular);
                createStatic(L"", SS_ETCHEDFRAME, 24, 108, 572, 92,
                    state->regular);
                state->uvsr = CreateWindowExW(0, L"BUTTON", L"UVSR Engine",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                    scale(40, state->dpi), scale(120, state->dpi),
                    scale(520, state->dpi), scale(24, state->dpi), window,
                    reinterpret_cast<HMENU>(101), GetModuleHandleW(nullptr),
                    nullptr);
                set_font(state->uvsr, state->bold);
                createStatic(widen(state->updates->uvsr.detail),
                    SS_LEFT | SS_EDITCONTROL, 64, 148, 510, 42,
                    state->regular);
                createStatic(L"", SS_ETCHEDFRAME, 24, 210, 572, 92,
                    state->regular);
                state->launcher = CreateWindowExW(0, L"BUTTON",
                    L"UVSR Launcher",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                    scale(40, state->dpi), scale(222, state->dpi),
                    scale(520, state->dpi), scale(24, state->dpi), window,
                    reinterpret_cast<HMENU>(102), GetModuleHandleW(nullptr),
                    nullptr);
                set_font(state->launcher, state->bold);
                createStatic(widen(state->updates->launcher.detail),
                    SS_LEFT | SS_EDITCONTROL, 64, 250, 510, 42,
                    state->regular);
                const bool chooseUvsr = selectable(state->updates->uvsr.state);
                const bool chooseLauncher = selectable(
                    state->updates->launcher.state);
                Button_SetCheck(state->uvsr,
                    chooseUvsr ? BST_CHECKED : BST_UNCHECKED);
                Button_SetCheck(state->launcher,
                    chooseLauncher ? BST_CHECKED : BST_UNCHECKED);
                EnableWindow(state->uvsr, chooseUvsr);
                EnableWindow(state->launcher, chooseLauncher);
                state->update = CreateWindowExW(0, L"BUTTON",
                    L"Update Selected", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                        BS_OWNERDRAW,
                    scale(430, state->dpi), scale(322, state->dpi),
                    scale(166, state->dpi), scale(44, state->dpi), window,
                    reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr),
                    nullptr);
                HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    scale(278, state->dpi), scale(322, state->dpi),
                    scale(144, state->dpi), scale(44, state->dpi), window,
                    reinterpret_cast<HMENU>(IDCANCEL),
                    GetModuleHandleW(nullptr), nullptr);
                set_font(state->update, state->regular);
                set_font(cancel, state->regular);
                EnableWindow(state->update, chooseUvsr || chooseLauncher);
                if (failed)
                {
                    HWND retry = CreateWindowExW(0, L"BUTTON",
                        L"Check Again", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                            BS_OWNERDRAW,
                        scale(126, state->dpi), scale(322, state->dpi),
                        scale(144, state->dpi), scale(44, state->dpi), window,
                        reinterpret_cast<HMENU>(103),
                        GetModuleHandleW(nullptr), nullptr);
                    set_font(retry, state->regular);
                }
                return 0;
            }
            case WM_COMMAND:
                if (LOWORD(wParam) == 101 || LOWORD(wParam) == 102)
                {
                    EnableWindow(state->update,
                        Button_GetCheck(state->uvsr) == BST_CHECKED ||
                        Button_GetCheck(state->launcher) == BST_CHECKED);
                    return 0;
                }
                if (LOWORD(wParam) == IDOK)
                {
                    state->selection.accepted = true;
                    state->selection.updateUvsr =
                        Button_GetCheck(state->uvsr) == BST_CHECKED;
                    state->selection.updateLauncher =
                        Button_GetCheck(state->launcher) == BST_CHECKED;
                    DestroyWindow(window);
                    return 0;
                }
                if (LOWORD(wParam) == 103)
                {
                    state->selection.retry = true;
                    DestroyWindow(window);
                    return 0;
                }
                if (LOWORD(wParam) == IDCANCEL)
                {
                    DestroyWindow(window);
                    return 0;
                }
                break;
            case WM_DRAWITEM:
            {
                const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                draw_button(item,
                    { item.CtlID == IDOK, item.CtlID == IDCANCEL });
                return TRUE;
            }
            case WM_CTLCOLORSTATIC:
                SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
                SetTextColor(reinterpret_cast<HDC>(wParam), TextColor);
                return reinterpret_cast<LRESULT>(state->background);
            case WM_ERASEBKGND:
            {
                RECT area{};
                GetClientRect(window, &area);
                FillRect(reinterpret_cast<HDC>(wParam), &area,
                    state->background);
                return 1;
            }
            case WM_CLOSE:
                DestroyWindow(window);
                return 0;
            case WM_DESTROY:
                if (state->regular)
                    DeleteObject(state->regular);
                if (state->bold)
                    DeleteObject(state->bold);
                if (state->background)
                    DeleteObject(state->background);
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        LRESULT CALLBACK update_dialog_proc(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam) noexcept
        {
            try
            {
                return update_dialog_handle(window, message, wParam, lParam);
            }
            catch (...)
            {
                auto* state = reinterpret_cast<UpdateDialogState*>(
                    GetWindowLongPtrW(window, GWLP_USERDATA));
                if (state)
                    state->callbackError = std::current_exception();
                if (message == WM_NCCREATE)
                    return FALSE;
                if (message == WM_CREATE)
                    return -1;
                if (message != WM_DESTROY && IsWindow(window))
                    DestroyWindow(window);
                return 0;
            }
        }

        void register_update_dialog(HINSTANCE instance)
        {
            static bool registered = false;
            if (registered)
                return;
            WNDCLASSEXW type{};
            type.cbSize = sizeof(type);
            type.hInstance = instance;
            type.lpfnWndProc = update_dialog_proc;
            type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            type.lpszClassName = UpdateDialogClass;
            if (!RegisterClassExW(&type) &&
                GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                detail::throw_last_error(
                    "Windows could not create the update selection window");
            registered = true;
        }

        [[nodiscard]] UpdateSelection show_update_dialog(
            HWND owner,
            const UpdateCheckResult& updates)
        {
            HINSTANCE instance = GetModuleHandleW(nullptr);
            register_update_dialog(instance);
            UpdateDialogState state{ &updates };
            const UINT dpi = GetDpiForWindow(owner);
            RECT bounds{ 0, 0, scale(620, dpi), scale(390, dpi) };
            AdjustWindowRectExForDpi(&bounds,
                WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
            RECT ownerBounds{};
            GetWindowRect(owner, &ownerBounds);
            const int width = bounds.right - bounds.left;
            const int height = bounds.bottom - bounds.top;
            HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME,
                UpdateDialogClass, L"Choose What to Update",
                WS_CAPTION | WS_SYSMENU,
                ownerBounds.left + (ownerBounds.right - ownerBounds.left - width) / 2,
                ownerBounds.top + (ownerBounds.bottom - ownerBounds.top - height) / 2,
                width, height, owner, nullptr, instance, &state);
            if (!dialog)
            {
                if (state.callbackError)
                    std::rethrow_exception(state.callbackError);
                detail::throw_last_error(
                    "Windows could not open the update selection window");
            }
            EnableWindow(owner, FALSE);
            ShowWindow(dialog, SW_SHOW);
            MSG message{};
            bool quit = false;
            int quitCode = 0;
            bool messageFailed = false;
            while (IsWindow(dialog))
            {
                const BOOL result = GetMessageW(&message, nullptr, 0, 0);
                if (result <= 0)
                {
                    quit = result == 0;
                    quitCode = static_cast<int>(message.wParam);
                    messageFailed = result == -1;
                    break;
                }
                if (!IsDialogMessageW(dialog, &message))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            EnableWindow(owner, TRUE);
            SetForegroundWindow(owner);
            if (quit)
                PostQuitMessage(quitCode);
            if (messageFailed)
                detail::throw_last_error(
                    "Windows could not read an update selection message");
            if (state.callbackError)
                std::rethrow_exception(state.callbackError);
            return state.selection;
        }
    }
}

namespace uvsr::launcher
{
    namespace
    {
        enum class WorkKind
        {
            Ensure,
            Check,
            Execute,
            LauncherUpdate,
            Launch,
            Close
        };

        struct WorkResult
        {
            WorkKind kind = WorkKind::Ensure;
            bool cancelled = false;
            std::string error;
            std::optional<UpdateCheckResult> updates;
            std::optional<OperationResult> operation;
            bool rendererReachedState = false;
        };

        struct ProgressEvent
        {
            InstallerProgress progress;
        };

        void show_notices_window(HWND owner);

        constexpr wchar_t MainWindowClass[] = L"UVSRLauncherMainWindow";

        class MainWindow final
        {
        public:
            MainWindow(
                HINSTANCE instance,
                InstallerEngine& engine,
                bool startWithUninstall,
                std::optional<GUID> continuation)
                : m_Instance(instance),
                  m_Engine(engine),
                  m_StartWithUninstall(startWithUninstall),
                  m_RequestedContinuation(continuation) {}

            ~MainWindow()
            {
                if (m_Worker.joinable())
                {
                    if (m_Cancellation)
                        m_Cancellation->request();
                    m_Engine.cancel_download();
                    m_Worker.join();
                }
                destroy_fonts();
                if (m_WindowBrush)
                    DeleteObject(m_WindowBrush);
                if (m_CardBrush)
                    DeleteObject(m_CardBrush);
            }

            [[nodiscard]] HWND create()
            {
                register_class();
                const UINT dpi = GetDpiForSystem();
                RECT bounds{ 0, 0, scale(840, dpi), scale(720, dpi) };
                const DWORD style = WS_OVERLAPPEDWINDOW;
                AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, dpi);
                const int width = bounds.right - bounds.left;
                const int height = bounds.bottom - bounds.top;
                RECT work{};
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
                const int x = work.left +
                    (std::max)(0, static_cast<int>(
                        work.right - work.left - width) / 2);
                const int y = work.top +
                    (std::max)(0, static_cast<int>(
                        work.bottom - work.top - height) / 2);
                m_Window = CreateWindowExW(0, MainWindowClass,
                    L"UVSR Launcher", style, x, y,
                    (std::min)(width,
                        static_cast<int>(work.right - work.left)),
                    (std::min)(height,
                        static_cast<int>(work.bottom - work.top)),
                    nullptr, nullptr, m_Instance, this);
                if (!m_Window)
                {
                    if (m_CallbackError)
                        std::rethrow_exception(m_CallbackError);
                    detail::throw_last_error(
                        "Windows could not create the UVSR Launcher window");
                }
                return m_Window;
            }

            void rethrow_callback_error() const
            {
                if (m_CallbackError)
                    std::rethrow_exception(m_CallbackError);
            }

        private:
            static LRESULT CALLBACK window_proc(
                HWND window,
                UINT message,
                WPARAM wParam,
                LPARAM lParam) noexcept
            {
                auto* self = reinterpret_cast<MainWindow*>(
                    GetWindowLongPtrW(window, GWLP_USERDATA));
                try
                {
                    if (message == WM_NCCREATE)
                    {
                        const auto* create =
                            reinterpret_cast<CREATESTRUCTW*>(lParam);
                        self = static_cast<MainWindow*>(create->lpCreateParams);
                        self->m_Window = window;
                        SetWindowLongPtrW(window, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(self));
                    }
                    const LRESULT result = self
                        ? self->handle(message, wParam, lParam)
                        : DefWindowProcW(window, message, wParam, lParam);
                    if (message == WM_NCDESTROY && self)
                    {
                        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                        self->m_Window = nullptr;
                    }
                    return result;
                }
                catch (...)
                {
                    if (self)
                        self->m_CallbackError = std::current_exception();
                    if (message == WM_NCCREATE)
                        return FALSE;
                    if (message == WM_CREATE)
                        return -1;
                    if (message != WM_DESTROY && IsWindow(window))
                        DestroyWindow(window);
                    return 0;
                }
            }

            void register_class()
            {
                static bool registered = false;
                if (registered)
                    return;
                WNDCLASSEXW type{};
                type.cbSize = sizeof(type);
                type.hInstance = m_Instance;
                type.lpfnWndProc = window_proc;
                type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
                type.hIconSm = type.hIcon;
                type.hbrBackground = reinterpret_cast<HBRUSH>(
                    COLOR_WINDOW + 1);
                type.lpszClassName = MainWindowClass;
                if (!RegisterClassExW(&type) &&
                    GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                    detail::throw_last_error(
                        "Windows could not register the launcher window");
                registered = true;
            }

            [[nodiscard]] HWND child(
                std::wstring_view type,
                std::wstring_view text,
                DWORD style,
                int id = 0,
                DWORD extended = 0)
            {
                HWND control = CreateWindowExW(extended,
                    std::wstring(type).c_str(), std::wstring(text).c_str(),
                    WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
                    m_Window, id ? reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(id)) : nullptr,
                    m_Instance, nullptr);
                if (!control)
                    detail::throw_last_error(
                        "Windows could not create a launcher control");
                return control;
            }

            void create_controls()
            {
                m_Dpi = GetDpiForWindow(m_Window);
                m_WindowBrush = CreateSolidBrush(WindowColor);
                m_CardBrush = CreateSolidBrush(CardColor);
                create_fonts();
                m_Title = child(L"STATIC", L"UVSR Launcher", SS_LEFT);
                m_Intro = child(L"STATIC",
                    L"Install, launch, and keep UVSR up to date on Windows 11. The launcher handles downloads and setup for you.",
                    SS_LEFT | SS_EDITCONTROL);
                m_Status = child(L"STATIC", L"Checking this PC...",
                    SS_LEFT | SS_EDITCONTROL);
                m_Shortcut = child(L"BUTTON",
                    L"Create a desktop shortcut for UVSR Launcher",
                    BS_AUTOCHECKBOX | WS_TABSTOP, ShortcutCheck);
                Button_SetCheck(m_Shortcut, BST_CHECKED);
                m_Install = child(L"BUTTON", L"Install",
                    BS_OWNERDRAW | WS_TABSTOP, InstallButton);
                m_Launch = child(L"BUTTON", L"Launch",
                    BS_OWNERDRAW | WS_TABSTOP, LaunchButton);
                m_Update = child(L"BUTTON", L"Update",
                    BS_OWNERDRAW | WS_TABSTOP, UpdateButton);
                m_Uninstall = child(L"BUTTON", L"Uninstall",
                    BS_OWNERDRAW | WS_TABSTOP, UninstallButton);
                m_Notices = child(L"BUTTON", L"Notices",
                    BS_OWNERDRAW | WS_TABSTOP, NoticesButton);
                m_Phase = child(L"STATIC", L"Ready", SS_LEFT);
                m_Detail = child(L"STATIC", L"Choose an action above.",
                    SS_LEFT | SS_EDITCONTROL);
                m_Progress = child(PROGRESS_CLASSW, L"",
                    PBS_SMOOTH | PBS_MARQUEE);
                SendMessageW(m_Progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
                SendMessageW(m_Progress, PBM_SETBARCOLOR, 0, AccentColor);
                SendMessageW(m_Progress, PBM_SETBKCOLOR, 0, BorderColor);
                m_Details = child(L"BUTTON", L"Details",
                    BS_OWNERDRAW | WS_TABSTOP, DetailsButton);
                m_Copy = child(L"BUTTON", L"Copy",
                    BS_OWNERDRAW | WS_TABSTOP, CopyButton);
                m_Log = child(L"EDIT", L"",
                    ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY |
                        WS_VSCROLL | WS_TABSTOP,
                    0, WS_EX_CLIENTEDGE);
                ShowWindow(m_Copy, SW_HIDE);
                ShowWindow(m_Log, SW_HIDE);
                apply_fonts();
                layout();
                SetTimer(m_Window, 1, 750, nullptr);
                PostMessageW(m_Window, StartMessage, 0, 0);
            }

            void create_fonts()
            {
                destroy_fonts();
                m_TitleFont = make_font(m_Dpi, 25, FW_BOLD);
                m_StatusFont = make_font(m_Dpi, 12, FW_BOLD);
                m_BoldFont = make_font(m_Dpi, 10, FW_BOLD);
                m_RegularFont = make_font(m_Dpi, 10);
                m_LogFont = make_font(m_Dpi, 9);
            }

            void destroy_fonts() noexcept
            {
                for (HFONT* font : { &m_TitleFont, &m_StatusFont,
                        &m_BoldFont, &m_RegularFont, &m_LogFont })
                {
                    if (*font)
                    {
                        DeleteObject(*font);
                        *font = nullptr;
                    }
                }
            }

            void apply_fonts()
            {
                set_font(m_Title, m_TitleFont);
                set_font(m_Status, m_StatusFont);
                set_font(m_Phase, m_BoldFont);
                for (HWND control : { m_Intro, m_Shortcut, m_Install,
                        m_Launch, m_Update, m_Uninstall, m_Notices,
                        m_Detail, m_Progress, m_Details, m_Copy })
                    set_font(control, m_RegularFont);
                set_font(m_Log, m_LogFont);
            }

            void layout()
            {
                if (!m_Title)
                    return;
                RECT client{};
                GetClientRect(m_Window, &client);
                const int margin = scale(28, m_Dpi);
                const int width = (std::max)(scale(200, m_Dpi),
                    static_cast<int>(client.right) - 2 * margin);
                int y = scale(22, m_Dpi);
                MoveWindow(m_Title, margin, y, width,
                    scale(42, m_Dpi), TRUE);
                y += scale(48, m_Dpi);
                MoveWindow(m_Intro, margin, y, width,
                    scale(42, m_Dpi), TRUE);
                y += scale(52, m_Dpi);
                m_StatusCard = { margin, y, margin + width,
                    y + scale(56, m_Dpi) };
                MoveWindow(m_Status, margin + scale(16, m_Dpi),
                    y + scale(13, m_Dpi), width - scale(32, m_Dpi),
                    scale(30, m_Dpi), TRUE);
                y += scale(70, m_Dpi);
                MoveWindow(m_Shortcut, margin + scale(2, m_Dpi), y,
                    width - scale(4, m_Dpi), scale(28, m_Dpi), TRUE);
                y += scale(36, m_Dpi);
                const int buttonWidth = scale(144, m_Dpi);
                const int buttonHeight = scale(44, m_Dpi);
                const int gap = scale(8, m_Dpi);
                int x = margin;
                int rowTop = y;
                for (HWND button : { m_Install, m_Launch, m_Update,
                        m_Uninstall, m_Notices })
                {
                    if (x != margin && x + buttonWidth > margin + width)
                    {
                        x = margin;
                        rowTop += buttonHeight + gap;
                    }
                    MoveWindow(button, x, rowTop, buttonWidth,
                        buttonHeight, TRUE);
                    x += buttonWidth + gap;
                }
                y = rowTop + buttonHeight + scale(20, m_Dpi);
                MoveWindow(m_Phase, margin, y, width,
                    scale(24, m_Dpi), TRUE);
                y += scale(28, m_Dpi);
                MoveWindow(m_Detail, margin, y, width,
                    scale(42, m_Dpi), TRUE);
                y += scale(48, m_Dpi);
                MoveWindow(m_Progress, margin, y, width,
                    scale(7, m_Dpi), TRUE);
                y += scale(20, m_Dpi);
                MoveWindow(m_Details, margin, y, scale(110, m_Dpi),
                    scale(36, m_Dpi), TRUE);
                MoveWindow(m_Copy, margin + scale(118, m_Dpi), y,
                    scale(90, m_Dpi), scale(36, m_Dpi), TRUE);
                y += scale(44, m_Dpi);
                m_LogCard = { margin, y, margin + width,
                    (std::max)(y + scale(40, m_Dpi),
                        static_cast<int>(client.bottom) - margin) };
                MoveWindow(m_Log, margin + scale(12, m_Dpi),
                    y + scale(12, m_Dpi), width - scale(24, m_Dpi),
                    (std::max)(scale(16, m_Dpi),
                        static_cast<int>(m_LogCard.bottom) - y -
                            scale(24, m_Dpi)), TRUE);
                InvalidateRect(m_Window, nullptr, TRUE);
            }

            void refresh_snapshot()
            {
                try
                {
                    m_Snapshot = m_Engine.inspect();
                    SetWindowTextW(m_Status,
                        widen(m_Snapshot->summary).c_str());
                    m_StatusTone = m_Snapshot->damaged ? DangerColor
                        : m_Snapshot->installed ? SuccessColor : MutedColor;
                    Button_SetCheck(m_Shortcut,
                        m_Engine.desktop_shortcut_preference()
                            ? BST_CHECKED : BST_UNCHECKED);
                }
                catch (const std::exception& error)
                {
                    m_Snapshot.reset();
                    SetWindowTextW(m_Status, widen(error.what()).c_str());
                    m_StatusTone = DangerColor;
                }
                update_buttons();
                InvalidateRect(m_Status, nullptr, TRUE);
            }

            void update_buttons()
            {
                const bool available = !m_Busy && m_Snapshot.has_value() &&
                    m_RendererBusy == RendererBusyAction::None && !m_Obsolete;
                EnableWindow(m_Install, available);
                EnableWindow(m_Update, available);
                EnableWindow(m_Uninstall, available &&
                    m_Snapshot->initialized);
                EnableWindow(m_Notices, !m_Busy);
                EnableWindow(m_Shortcut, available);
                if (m_Snapshot)
                    SetWindowTextW(m_Install,
                        install_button_text(*m_Snapshot).c_str());
                ExactProcessInspection processes;
                if (m_Snapshot && m_Snapshot->installed &&
                    !m_Snapshot->damaged)
                    processes = m_Engine.inspect_renderer_processes();
                else
                    processes.state = ProcessState::NotRunning;
                bool launchEnabled = available && m_Snapshot->installed &&
                    !m_Snapshot->damaged &&
                    processes.state != ProcessState::Unverifiable;
                if (m_RendererBusy != RendererBusyAction::None)
                {
                    SetWindowTextW(m_Launch,
                        m_RendererBusy == RendererBusyAction::Closing
                            ? L"Closing" : L"Launching");
                    m_RenderedIntent = RendererButtonIntent::None;
                    launchEnabled = false;
                }
                else if (processes.state == ProcessState::Unverifiable)
                {
                    SetWindowTextW(m_Launch, L"Checking");
                    m_RenderedIntent = RendererButtonIntent::None;
                }
                else if (processes.state == ProcessState::Running)
                {
                    SetWindowTextW(m_Launch, L"Close");
                    m_RenderedIntent = launchEnabled
                        ? RendererButtonIntent::Close
                        : RendererButtonIntent::None;
                }
                else
                {
                    SetWindowTextW(m_Launch, L"Launch");
                    m_RenderedIntent = launchEnabled
                        ? RendererButtonIntent::Launch
                        : RendererButtonIntent::None;
                }
                EnableWindow(m_Launch, launchEnabled);
                InvalidateRect(m_Launch, nullptr, TRUE);
            }

            void set_terminal(const TerminalUiState& state)
            {
                SetWindowTextW(m_Phase, state.phase.c_str());
                SetWindowTextW(m_Detail, state.detail.c_str());
                m_ErrorTone = state.error;
                SendMessageW(m_Progress, PBM_SETMARQUEE,
                    state.marquee ? TRUE : FALSE, 28);
                if (!state.marquee)
                    SendMessageW(m_Progress, PBM_SETPOS,
                        std::clamp(state.progress, 0, 100), 0);
                InvalidateRect(m_Phase, nullptr, TRUE);
                InvalidateRect(m_Detail, nullptr, TRUE);
            }

            void set_progress(const InstallerProgress& progress)
            {
                m_CanCancel = progress.canCancel;
                set_terminal({ widen(progress.phase), widen(progress.detail),
                    progress.percent.value_or(0), !progress.percent.has_value(),
                    false });
            }

            void append_log(std::string_view line)
            {
                std::wstring text = widen(line);
                text += L"\r\n";
                const LRESULT length = SendMessageW(m_Log,
                    WM_GETTEXTLENGTH, 0, 0);
                if (length > 200000)
                {
                    SendMessageW(m_Log, EM_SETSEL, 0,
                        static_cast<LPARAM>(length - 190000));
                    SendMessageW(m_Log, EM_REPLACESEL, FALSE,
                        reinterpret_cast<LPARAM>(L""));
                }
                SendMessageW(m_Log, EM_SETSEL, static_cast<WPARAM>(-1),
                    static_cast<LPARAM>(-1));
                SendMessageW(m_Log, EM_REPLACESEL, FALSE,
                    reinterpret_cast<LPARAM>(text.c_str()));
                SendMessageW(m_Log, EM_SCROLLCARET, 0, 0);
            }

            void clear_log()
            {
                SetWindowTextW(m_Log, L"");
            }

            void show_details(bool visible)
            {
                m_DetailsVisible = visible;
                ShowWindow(m_Log, visible ? SW_SHOW : SW_HIDE);
                ShowWindow(m_Copy, visible ? SW_SHOW : SW_HIDE);
                SetWindowTextW(m_Details,
                    visible ? L"Hide Details" : L"Details");
                InvalidateRect(m_Window, nullptr, TRUE);
            }

            void copy_details()
            {
                const std::wstring text = window_text(m_Log);
                if (!OpenClipboard(m_Window))
                {
                    static_cast<void>(show_dialog(m_Window, L"Copy",
                        L"Windows could not access the clipboard right now. Try again in a moment.",
                        { { IDOK, L"OK" } }));
                    return;
                }
                EmptyClipboard();
                const SIZE_T bytes = (text.size() + 1u) * sizeof(wchar_t);
                HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (memory)
                {
                    void* data = GlobalLock(memory);
                    if (data)
                    {
                        memcpy(data, text.c_str(), bytes);
                        GlobalUnlock(memory);
                        if (!SetClipboardData(CF_UNICODETEXT, memory))
                            GlobalFree(memory);
                    }
                    else
                        GlobalFree(memory);
                }
                CloseClipboard();
            }

            void begin_work(
                WorkKind kind,
                std::string_view phase,
                std::string_view detailText,
                std::function<void(WorkResult&, const Cancellation&)> work)
            {
                if (m_Busy)
                    return;
                if (m_Worker.joinable())
                    m_Worker.join();
                m_Busy = true;
                m_CanCancel = true;
                m_Cancellation = std::make_shared<Cancellation>();
                clear_log();
                set_terminal({ widen(phase), widen(detailText), 0, true,
                    false });
                update_buttons();
                const HWND window = m_Window;
                const auto cancellation = m_Cancellation;
                m_Worker = std::thread([window, cancellation, kind,
                    work = std::move(work)]() mutable
                {
                    auto result = std::make_unique<WorkResult>();
                    result->kind = kind;
                    try
                    {
                        work(*result, *cancellation);
                    }
                    catch (const Cancelled&)
                    {
                        result->cancelled = true;
                    }
                    catch (const std::exception& error)
                    {
                        result->error = error.what();
                    }
                    WorkResult* raw = result.release();
                    if (!PostMessageW(window, WorkCompleteMessage, 0,
                            reinterpret_cast<LPARAM>(raw)))
                        delete raw;
                });
            }

            [[nodiscard]] ProgressCallback progress_callback() const
            {
                const HWND window = m_Window;
                return [window](const InstallerProgress& progress)
                {
                    auto event = std::make_unique<ProgressEvent>();
                    event->progress = progress;
                    ProgressEvent* raw = event.release();
                    if (!PostMessageW(window, ProgressMessage, 0,
                            reinterpret_cast<LPARAM>(raw)))
                        delete raw;
                };
            }

            [[nodiscard]] LogCallback log_callback() const
            {
                const HWND window = m_Window;
                return [window](std::string_view line)
                {
                    auto text = std::make_unique<std::string>(line);
                    std::string* raw = text.release();
                    if (!PostMessageW(window, LogMessage, 0,
                            reinterpret_cast<LPARAM>(raw)))
                        delete raw;
                };
            }

            [[nodiscard]] bool shortcut_checked() const
            {
                return Button_GetCheck(m_Shortcut) == BST_CHECKED;
            }

            void start_ensure()
            {
                const bool shortcut = shortcut_checked();
                const auto progress = progress_callback();
                const auto log = log_callback();
                begin_work(WorkKind::Ensure, "Preparing UVSR Launcher",
                    "Checking the installed launcher and shortcuts.",
                    [this, shortcut, progress, log](WorkResult&,
                        const Cancellation& cancellation)
                    {
                        m_Engine.ensure_launcher_ready(shortcut, progress,
                            log, cancellation);
                    });
            }

            void start_check()
            {
                const bool shortcut = shortcut_checked();
                const auto progress = progress_callback();
                const auto log = log_callback();
                begin_work(WorkKind::Check, "Checking for updates",
                    "Checking UVSR Engine and UVSR Launcher.",
                    [this, shortcut, progress, log](WorkResult& result,
                        const Cancellation& cancellation)
                    {
                        result.updates = m_Engine.check_for_updates(shortcut,
                            progress, log, cancellation);
                    });
            }

            void start_execute(InstallerOperation operation)
            {
                const bool shortcut = shortcut_checked();
                const auto progress = progress_callback();
                const auto log = log_callback();
                begin_work(WorkKind::Execute,
                    operation == InstallerOperation::Uninstall
                        ? "Preparing uninstall" : "Getting UVSR ready",
                    "This can take a few minutes. You can keep using this window while it works.",
                    [this, shortcut, operation, progress, log](
                        WorkResult& result, const Cancellation& cancellation)
                    {
                        result.operation = m_Engine.execute(operation,
                            shortcut, progress, log, cancellation);
                    });
            }

            void start_launcher_update(
                const LauncherFeed& feed,
                bool continueRenderer)
            {
                const bool shortcut = shortcut_checked();
                const auto progress = progress_callback();
                const auto log = log_callback();
                begin_work(WorkKind::LauncherUpdate,
                    "Updating UVSR Launcher",
                    "Downloading and activating the selected launcher update.",
                    [this, shortcut, feed, continueRenderer, progress, log](
                        WorkResult& result, const Cancellation& cancellation)
                    {
                        result.operation = m_Engine.update_launcher(feed,
                            shortcut, continueRenderer, progress, log,
                            cancellation);
                    });
            }

            void start_launch()
            {
                if (!m_Snapshot || m_Snapshot->executablePath.empty())
                    return;
                const fs::path executable = m_Snapshot->executablePath;
                m_RendererBusy = RendererBusyAction::Launching;
                begin_work(WorkKind::Launch, "Launching UVSR",
                    "Waiting for UVSR to start.",
                    [this, executable](WorkResult& result,
                        const Cancellation& cancellation)
                    {
                        m_Engine.launch_installed();
                        const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(6);
                        do
                        {
                            cancellation.check();
                            if (inspect_processes_by_executable(executable).state ==
                                ProcessState::Running)
                            {
                                result.rendererReachedState = true;
                                return;
                            }
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(150));
                        } while (std::chrono::steady_clock::now() < deadline);
                    });
            }

            void start_close(bool force, bool signalClose)
            {
                const auto targets = m_CloseTargets;
                m_RendererBusy = RendererBusyAction::Closing;
                begin_work(WorkKind::Close, "Closing UVSR",
                    "Waiting for UVSR to close safely.",
                    [targets, force, signalClose](WorkResult& result,
                        const Cancellation&)
                    {
                        for (const auto& target : targets)
                        {
                            if (force)
                                (void)terminate_exact_process(target);
                            else if (signalClose)
                                (void)close_exact_process(target);
                        }
                        const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(3);
                        do
                        {
                            if (std::all_of(targets.begin(), targets.end(),
                                    [](const ExactProcessIdentity& target)
                                    {
                                        return inspect_exact_process(target) ==
                                            ProcessState::NotRunning;
                                    }))
                            {
                                result.rendererReachedState = true;
                                return;
                            }
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(150));
                        } while (std::chrono::steady_clock::now() < deadline);
                    });
            }

            void finish_worker()
            {
                if (m_Worker.joinable())
                    m_Worker.join();
                m_Cancellation.reset();
                m_Busy = false;
                m_CanCancel = true;
            }

            void show_error(std::string_view message)
            {
                set_terminal({ L"Stopped Safely", widen(message),
                    0, false, true });
                show_details(true);
                static_cast<void>(show_dialog(m_Window,
                    L"UVSR Launcher Stopped", widen(message),
                    { { IDOK, L"OK" } }, true));
            }

            void handle_operation_result(const OperationResult& result)
            {
                set_terminal({ L"Complete", widen(result.message),
                    100, false, false });
                if (m_ContinuationToComplete)
                {
                    try
                    {
                        m_Engine.complete_launcher_continuation(
                            *m_ContinuationToComplete);
                        m_ContinuationToComplete.reset();
                    }
                    catch (const std::exception& error)
                    {
                        show_error(error.what());
                        return;
                    }
                }
                if (result.cleanupScheduled)
                {
                    m_AllowClose = true;
                    DestroyWindow(m_Window);
                    return;
                }
                if (!result.relaunchLauncherPath.empty())
                {
                    relaunch(result);
                    return;
                }
                static_cast<void>(show_dialog(m_Window, L"Complete",
                    widen(result.message), { { IDOK, L"OK" } }));
            }

            void relaunch(const OperationResult& result)
            {
                m_Obsolete = true;
                update_buttons();
                for (;;)
                {
                    set_terminal({ L"UVSR Launcher Updated",
                        L"Opening the updated UVSR Launcher.",
                        100, false, false });
                    std::wstring parameters;
                    if (result.continueUvsrUpdate &&
                        result.launcherContinuationId)
                    {
                        parameters = L"--continue-uvsr-update " +
                            widen(guid_string(
                                *result.launcherContinuationId));
                    }
                    SHELLEXECUTEINFOW info{};
                    info.cbSize = sizeof(info);
                    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
                    info.lpFile = result.relaunchLauncherPath.c_str();
                    info.lpParameters = parameters.empty()
                        ? nullptr : parameters.c_str();
                    const std::wstring directory =
                        result.relaunchLauncherPath.parent_path().native();
                    info.lpDirectory = directory.c_str();
                    info.nShow = SW_SHOWNORMAL;
                    if (ShellExecuteExW(&info))
                    {
                        m_AllowClose = true;
                        DestroyWindow(m_Window);
                        return;
                    }
                    const int choice = show_dialog(m_Window,
                        L"Open Updated UVSR Launcher",
                        L"The update is installed, but Windows could not open it automatically. This window can no longer make changes. Choose Retry, or close this window and open UVSR Launcher from the Start menu.",
                        { { IDRETRY, L"Retry" }, { IDCANCEL, L"Close" } },
                        true);
                    if (choice != IDRETRY)
                    {
                        m_AllowClose = true;
                        DestroyWindow(m_Window);
                        return;
                    }
                }
            }

            void continue_startup()
            {
                try
                {
                    const auto pending = m_Engine.pending_launcher_continuation(
                        m_RequestedContinuation);
                    if (!pending)
                        return;
                    m_ContinuationToComplete = pending;
                    refresh_snapshot();
                    start_execute(m_Snapshot && m_Snapshot->damaged
                        ? InstallerOperation::Reinstall
                        : InstallerOperation::Update);
                }
                catch (const std::exception& error)
                {
                    show_error(error.what());
                }
            }

            void handle_update_result(const UpdateCheckResult& updates)
            {
                const bool hasSelectable = selectable(updates.uvsr.state) ||
                    selectable(updates.launcher.state);
                const bool failed =
                    updates.uvsr.state == ComponentUpdateState::CheckFailed ||
                    updates.launcher.state == ComponentUpdateState::CheckFailed;
                if (!hasSelectable && !failed)
                {
                    const std::wstring message =
                        updates.uvsr.state == ComponentUpdateState::NotInstalled
                            ? L"UVSR Launcher is up to date. UVSR is not installed yet."
                            : L"UVSR Engine and UVSR Launcher are up to date.";
                    set_terminal(update_terminal_state(
                        UpdateFlowExit::UpToDate, message));
                    static_cast<void>(show_dialog(m_Window,
                        L"Everything Is Up to Date", message,
                        { { IDOK, L"OK" } }));
                    return;
                }
                const UpdateSelection selection = show_update_dialog(
                    m_Window, updates);
                if (selection.retry)
                {
                    start_check();
                    return;
                }
                if (!selection.accepted)
                {
                    set_terminal(update_terminal_state(
                        UpdateFlowExit::SelectionCancelled));
                    return;
                }
                if (selection.updateLauncher)
                {
                    if (!updates.launcher.launcherFeed)
                    {
                        show_error(
                            "The selected launcher update no longer had a valid release record.");
                        return;
                    }
                    start_launcher_update(*updates.launcher.launcherFeed,
                        selection.updateUvsr);
                    return;
                }
                if (selection.updateUvsr)
                {
                    start_execute(updates.uvsr.state ==
                            ComponentUpdateState::RepairNeeded
                        ? InstallerOperation::Reinstall
                        : InstallerOperation::Update);
                    return;
                }
                set_terminal(update_terminal_state(UpdateFlowExit::NoSelection));
            }

            void handle_work_result(std::unique_ptr<WorkResult> result)
            {
                finish_worker();
                const RendererBusyAction rendererAction = m_RendererBusy;
                m_RendererBusy = RendererBusyAction::None;
                if (result->cancelled)
                {
                    set_terminal(update_terminal_state(
                        UpdateFlowExit::OperationCancelled));
                    refresh_snapshot();
                    return;
                }
                if (!result->error.empty())
                {
                    show_error(result->error);
                    refresh_snapshot();
                    return;
                }
                switch (result->kind)
                {
                case WorkKind::Ensure:
                    refresh_snapshot();
                    set_terminal({ L"Ready", L"UVSR Launcher is ready.",
                        100, false, false });
                    continue_startup();
                    break;
                case WorkKind::Check:
                    refresh_snapshot();
                    if (result->updates)
                        handle_update_result(*result->updates);
                    break;
                case WorkKind::Execute:
                case WorkKind::LauncherUpdate:
                    refresh_snapshot();
                    if (result->operation)
                        handle_operation_result(*result->operation);
                    break;
                case WorkKind::Launch:
                    refresh_snapshot();
                    if (result->rendererReachedState)
                        set_terminal(renderer_success_state(
                            RendererBusyAction::Launching));
                    else
                        show_error(
                            "UVSR was launched, but the process did not appear to start.");
                    break;
                case WorkKind::Close:
                    refresh_snapshot();
                    if (result->rendererReachedState)
                    {
                        set_terminal(renderer_success_state(
                            RendererBusyAction::Closing));
                        break;
                    }
                    if (rendererAction != RendererBusyAction::Closing)
                        break;
                    switch (show_dialog(m_Window, L"UVSR Is Still Running",
                        L"UVSR did not close within three seconds. Keep waiting to preserve its work, or force close only the exact processes that were running when you clicked Close.",
                        { { IDRETRY, L"Keep Waiting" },
                            { IDYES, L"Force Close" },
                            { IDCANCEL, L"Cancel" } }))
                    {
                    case IDRETRY:
                        start_close(false, false);
                        break;
                    case IDYES:
                        start_close(true, false);
                        break;
                    default:
                        set_terminal({ L"Close Cancelled",
                            L"UVSR is still running. Nothing was changed.",
                            0, false, false });
                        break;
                    }
                    break;
                }
                update_buttons();
            }

            void install_clicked()
            {
                if (m_Busy)
                    return;
                refresh_snapshot();
                if (!m_Snapshot)
                    return;
                if (m_Snapshot->installed && !m_Snapshot->damaged)
                {
                    const int choice = show_dialog(m_Window,
                        L"UVSR Is Already Installed",
                        L"UVSR is ready to use. You can launch it now or reinstall its trusted package.",
                        { { IDYES, L"Launch" }, { IDRETRY, L"Reinstall" },
                            { IDCANCEL, L"Cancel" } });
                    if (choice == IDYES)
                        launch_clicked();
                    else if (choice == IDRETRY)
                        start_execute(InstallerOperation::Reinstall);
                    return;
                }
                if (m_Snapshot->damaged)
                {
                    if (show_dialog(m_Window, L"UVSR Needs Repair",
                        L"Some installed files are missing or changed. Reinstall UVSR to restore it. Your settings and history will be kept.",
                        { { IDRETRY, L"Reinstall" },
                            { IDCANCEL, L"Cancel" } }) == IDRETRY)
                    {
                        start_execute(InstallerOperation::Reinstall);
                    }
                    return;
                }
                if (show_dialog(m_Window, L"Install UVSR",
                    L"UVSR Launcher will authenticate, download, and install the latest renderer package. The installed package remains available during a safe update.",
                    { { IDOK, L"Install" }, { IDCANCEL, L"Cancel" } }) == IDOK)
                {
                    start_execute(InstallerOperation::Install);
                }
            }

            void uninstall_clicked()
            {
                if (show_dialog(m_Window, L"Uninstall UVSR",
                    L"Remove UVSR Launcher, installed UVSR program files, shortcuts, and its Windows entry? Your renderer settings and history will be kept. Shared Microsoft components will also stay installed.",
                    { { IDYES, L"Uninstall" },
                        { IDCANCEL, L"Cancel" } }) == IDYES)
                {
                    start_execute(InstallerOperation::Uninstall);
                }
            }

            void launch_clicked()
            {
                if (m_Busy || !m_Snapshot || !m_Snapshot->installed ||
                    m_Snapshot->damaged)
                    return;
                const RendererButtonIntent rendered = m_RenderedIntent;
                const ExactProcessInspection current =
                    m_Engine.inspect_renderer_processes();
                const RendererClickAction action = reconcile_renderer_click(
                    rendered, current.state);
                if (action == RendererClickAction::Launch)
                    start_launch();
                else if (action == RendererClickAction::Close)
                {
                    m_CloseTargets = current.matches;
                    start_close(false, true);
                }
                else
                    update_buttons();
            }

            void command(int id)
            {
                switch (id)
                {
                case InstallButton: install_clicked(); break;
                case LaunchButton: launch_clicked(); break;
                case UpdateButton: start_check(); break;
                case UninstallButton: uninstall_clicked(); break;
                case NoticesButton: show_notices_window(m_Window); break;
                case DetailsButton: show_details(!m_DetailsVisible); break;
                case CopyButton: copy_details(); break;
                }
            }

            LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam)
            {
                switch (message)
                {
                case WM_CREATE:
                    create_controls();
                    return 0;
                case StartMessage:
                    refresh_snapshot();
                    if (m_StartWithUninstall)
                        uninstall_clicked();
                    else if (m_Snapshot && m_Snapshot->initialized)
                        start_ensure();
                    else
                        continue_startup();
                    return 0;
                case WM_COMMAND:
                    if (HIWORD(wParam) == BN_CLICKED)
                        command(LOWORD(wParam));
                    return 0;
                case WM_TIMER:
                    if (!m_Busy && !m_Obsolete)
                        update_buttons();
                    return 0;
                case ProgressMessage:
                {
                    std::unique_ptr<ProgressEvent> event(
                        reinterpret_cast<ProgressEvent*>(lParam));
                    if (m_Busy)
                        set_progress(event->progress);
                    return 0;
                }
                case LogMessage:
                {
                    std::unique_ptr<std::string> text(
                        reinterpret_cast<std::string*>(lParam));
                    append_log(*text);
                    return 0;
                }
                case WorkCompleteMessage:
                    handle_work_result(std::unique_ptr<WorkResult>(
                        reinterpret_cast<WorkResult*>(lParam)));
                    return 0;
                case WM_DRAWITEM:
                {
                    const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                    ButtonStyle style;
                    style.primary = item.CtlID == InstallButton;
                    style.destructive = item.CtlID == UninstallButton;
                    if (item.CtlID == LaunchButton &&
                        IsWindowEnabled(m_Launch))
                    {
                        style.launch = m_RenderedIntent ==
                            RendererButtonIntent::Launch;
                        style.close = m_RenderedIntent ==
                            RendererButtonIntent::Close;
                    }
                    draw_button(item, style);
                    return TRUE;
                }
                case WM_CTLCOLORSTATIC:
                case WM_CTLCOLORBTN:
                {
                    const HWND control = reinterpret_cast<HWND>(lParam);
                    HDC context = reinterpret_cast<HDC>(wParam);
                    SetBkMode(context, TRANSPARENT);
                    COLORREF color = TextColor;
                    if (control == m_Status)
                        color = m_StatusTone;
                    else if (control == m_Intro || control == m_Detail)
                        color = m_ErrorTone ? DangerColor : MutedColor;
                    else if (control == m_Phase && m_ErrorTone)
                        color = DangerColor;
                    SetTextColor(context, high_contrast()
                        ? GetSysColor(COLOR_WINDOWTEXT) : color);
                    return reinterpret_cast<LRESULT>(
                        control == m_Status ? m_CardBrush : m_WindowBrush);
                }
                case WM_PAINT:
                {
                    PAINTSTRUCT paint{};
                    HDC context = BeginPaint(m_Window, &paint);
                    RECT client{};
                    GetClientRect(m_Window, &client);
                    FillRect(context, &client, m_WindowBrush);
                    FillRect(context, &m_StatusCard, m_CardBrush);
                    SetDCBrushColor(context, BorderColor);
                    FrameRect(context, &m_StatusCard,
                        reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                    if (m_DetailsVisible)
                    {
                        FillRect(context, &m_LogCard, m_CardBrush);
                        FrameRect(context, &m_LogCard,
                            reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                    }
                    EndPaint(m_Window, &paint);
                    return 0;
                }
                case WM_SIZE:
                    layout();
                    return 0;
                case WM_DPICHANGED:
                {
                    m_Dpi = HIWORD(wParam);
                    const auto* suggested = reinterpret_cast<RECT*>(lParam);
                    SetWindowPos(m_Window, nullptr, suggested->left,
                        suggested->top, suggested->right - suggested->left,
                        suggested->bottom - suggested->top,
                        SWP_NOACTIVATE | SWP_NOZORDER);
                    create_fonts();
                    apply_fonts();
                    layout();
                    return 0;
                }
                case WM_GETMINMAXINFO:
                {
                    auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
                    limits->ptMinTrackSize.x = scale(700, m_Dpi);
                    limits->ptMinTrackSize.y = scale(520, m_Dpi);
                    return 0;
                }
                case WM_CLOSE:
                    if (m_AllowClose || m_Obsolete)
                    {
                        DestroyWindow(m_Window);
                        return 0;
                    }
                    if (m_RendererBusy != RendererBusyAction::None)
                    {
                        static_cast<void>(show_dialog(m_Window,
                            L"UVSR Launcher Is Busy",
                            m_RendererBusy == RendererBusyAction::Launching
                                ? L"UVSR Launcher is waiting for UVSR to finish starting. Try closing the launcher again after this action finishes."
                                : L"UVSR Launcher is waiting for UVSR to finish closing. Try closing the launcher again after this action finishes.",
                            { { IDOK, L"OK" } }));
                        return 0;
                    }
                    if (m_Busy)
                    {
                        if (!m_CanCancel)
                        {
                            static_cast<void>(show_dialog(m_Window,
                                L"Setup Is Finishing",
                                L"Setup must report its result before this window can close.",
                                { { IDOK, L"OK" } }));
                            return 0;
                        }
                        if (show_dialog(m_Window, L"Stop This Operation?",
                            L"The active UVSR version will be preserved. Any verified partial download can resume next time.",
                            { { IDYES, L"Stop" },
                                { IDCANCEL, L"Keep Working" } }) == IDYES)
                        {
                            if (m_Cancellation)
                                m_Cancellation->request();
                            m_Engine.cancel_download();
                        }
                        return 0;
                    }
                    DestroyWindow(m_Window);
                    return 0;
                case WM_DESTROY:
                    KillTimer(m_Window, 1);
                    PostQuitMessage(0);
                    return 0;
                }
                return DefWindowProcW(m_Window, message, wParam, lParam);
            }

            HINSTANCE m_Instance = nullptr;
            InstallerEngine& m_Engine;
            HWND m_Window = nullptr;
            HWND m_Title = nullptr;
            HWND m_Intro = nullptr;
            HWND m_Status = nullptr;
            HWND m_Shortcut = nullptr;
            HWND m_Install = nullptr;
            HWND m_Launch = nullptr;
            HWND m_Update = nullptr;
            HWND m_Uninstall = nullptr;
            HWND m_Notices = nullptr;
            HWND m_Phase = nullptr;
            HWND m_Detail = nullptr;
            HWND m_Progress = nullptr;
            HWND m_Details = nullptr;
            HWND m_Copy = nullptr;
            HWND m_Log = nullptr;
            HFONT m_TitleFont = nullptr;
            HFONT m_StatusFont = nullptr;
            HFONT m_BoldFont = nullptr;
            HFONT m_RegularFont = nullptr;
            HFONT m_LogFont = nullptr;
            HBRUSH m_WindowBrush = nullptr;
            HBRUSH m_CardBrush = nullptr;
            RECT m_StatusCard{};
            RECT m_LogCard{};
            UINT m_Dpi = 96;
            std::optional<InstallSnapshot> m_Snapshot;
            std::thread m_Worker;
            std::shared_ptr<Cancellation> m_Cancellation;
            std::vector<ExactProcessIdentity> m_CloseTargets;
            std::optional<GUID> m_RequestedContinuation;
            std::optional<GUID> m_ContinuationToComplete;
            std::exception_ptr m_CallbackError;
            RendererBusyAction m_RendererBusy = RendererBusyAction::None;
            RendererButtonIntent m_RenderedIntent = RendererButtonIntent::None;
            COLORREF m_StatusTone = MutedColor;
            bool m_StartWithUninstall = false;
            bool m_Busy = false;
            bool m_CanCancel = true;
            bool m_DetailsVisible = false;
            bool m_ErrorTone = false;
            bool m_Obsolete = false;
            bool m_AllowClose = false;
        };

        constexpr wchar_t NoticesWindowClass[] =
            L"UVSRLauncherNoticesWindow";

        struct NoticesWindowState
        {
            HWND window = nullptr;
            HWND text = nullptr;
            UINT dpi = 96;
            HFONT font = nullptr;
            HBRUSH windowBrush = nullptr;
            HBRUSH cardBrush = nullptr;
            RECT card{};
            std::exception_ptr callbackError;
        };

        [[nodiscard]] std::wstring notice_text()
        {
            std::string text = "UVSR LICENSE\n\n" +
                resource_text(IDR_UVSR_LICENSE) +
                "\n\nNOTO SANS FONT LICENSE\n\n" +
                resource_text(IDR_NOTO_LICENSE) +
                "\n\nMINIZ LICENSE\n\n" +
                resource_text(IDR_MINIZ_LICENSE);
            std::string windowsText;
            windowsText.reserve(text.size() + text.size() / 20u);
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '\n' &&
                    (index == 0 || text[index - 1u] != '\r'))
                    windowsText.push_back('\r');
                windowsText.push_back(text[index]);
            }
            return widen(windowsText);
        }

        void layout_notices(NoticesWindowState& state)
        {
            if (!state.text)
                return;
            RECT client{};
            GetClientRect(state.window, &client);
            const int margin = scale(20, state.dpi);
            const int padding = scale(16, state.dpi);
            state.card = { margin, margin,
                (std::max)(margin + scale(100, state.dpi),
                    static_cast<int>(client.right) - margin),
                (std::max)(margin + scale(100, state.dpi),
                    static_cast<int>(client.bottom) - margin) };
            MoveWindow(state.text, state.card.left + padding,
                state.card.top + padding,
                (std::max)(scale(20, state.dpi),
                    static_cast<int>(state.card.right - state.card.left) -
                        2 * padding),
                (std::max)(scale(20, state.dpi),
                    static_cast<int>(state.card.bottom - state.card.top) -
                        2 * padding), TRUE);
            InvalidateRect(state.window, nullptr, TRUE);
        }

        LRESULT notices_window_handle(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam)
        {
            auto* state = reinterpret_cast<NoticesWindowState*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
                state = static_cast<NoticesWindowState*>(create->lpCreateParams);
                state->window = window;
                SetWindowLongPtrW(window, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(state));
            }
            if (!state)
                return DefWindowProcW(window, message, wParam, lParam);
            switch (message)
            {
            case WM_CREATE:
            {
                state->dpi = GetDpiForWindow(window);
                state->font = make_font(state->dpi, 9);
                state->windowBrush = CreateSolidBrush(WindowColor);
                state->cardBrush = CreateSolidBrush(CardColor);
                state->text = CreateWindowExW(0, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                        ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                    0, 0, 1, 1, window, nullptr, GetModuleHandleW(nullptr),
                    nullptr);
                if (!state->text)
                    return -1;
                set_font(state->text, state->font);
                layout_notices(*state);
                return 0;
            }
            case WM_SIZE:
                layout_notices(*state);
                return 0;
            case WM_DPICHANGED:
            {
                state->dpi = HIWORD(wParam);
                const auto* suggested = reinterpret_cast<RECT*>(lParam);
                SetWindowPos(window, nullptr, suggested->left, suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOACTIVATE | SWP_NOZORDER);
                if (state->font)
                    DeleteObject(state->font);
                state->font = make_font(state->dpi, 9);
                set_font(state->text, state->font);
                layout_notices(*state);
                return 0;
            }
            case WM_GETMINMAXINFO:
            {
                auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
                limits->ptMinTrackSize.x = scale(650, state->dpi);
                limits->ptMinTrackSize.y = scale(450, state->dpi);
                return 0;
            }
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC:
            {
                HDC context = reinterpret_cast<HDC>(wParam);
                SetBkMode(context, TRANSPARENT);
                SetTextColor(context, high_contrast()
                    ? GetSysColor(COLOR_WINDOWTEXT) : TextColor);
                SetBkColor(context, high_contrast()
                    ? GetSysColor(COLOR_WINDOW) : CardColor);
                return reinterpret_cast<LRESULT>(state->cardBrush);
            }
            case WM_PAINT:
            {
                PAINTSTRUCT paint{};
                HDC context = BeginPaint(window, &paint);
                RECT client{};
                GetClientRect(window, &client);
                FillRect(context, &client, state->windowBrush);
                FillRect(context, &state->card, state->cardBrush);
                SetDCBrushColor(context, BorderColor);
                FrameRect(context, &state->card,
                    reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                EndPaint(window, &paint);
                return 0;
            }
            case WM_DESTROY:
                if (state->font)
                    DeleteObject(state->font);
                if (state->windowBrush)
                    DeleteObject(state->windowBrush);
                if (state->cardBrush)
                    DeleteObject(state->cardBrush);
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        LRESULT CALLBACK notices_window_proc(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam) noexcept
        {
            try
            {
                return notices_window_handle(window, message, wParam, lParam);
            }
            catch (...)
            {
                auto* state = reinterpret_cast<NoticesWindowState*>(
                    GetWindowLongPtrW(window, GWLP_USERDATA));
                if (state)
                    state->callbackError = std::current_exception();
                if (message == WM_NCCREATE)
                    return FALSE;
                if (message == WM_CREATE)
                    return -1;
                if (message != WM_DESTROY && IsWindow(window))
                    DestroyWindow(window);
                return 0;
            }
        }

        void register_notices_window(HINSTANCE instance)
        {
            static bool registered = false;
            if (registered)
                return;
            WNDCLASSEXW type{};
            type.cbSize = sizeof(type);
            type.hInstance = instance;
            type.lpfnWndProc = notices_window_proc;
            type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
            type.hIconSm = type.hIcon;
            type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            type.lpszClassName = NoticesWindowClass;
            if (!RegisterClassExW(&type) &&
                GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                detail::throw_last_error(
                    "Windows could not register the notices window");
            registered = true;
        }

        void show_notices_window(HWND owner)
        {
            try
            {
                const std::wstring notices = notice_text();
                HINSTANCE instance = GetModuleHandleW(nullptr);
                register_notices_window(instance);
                NoticesWindowState state;
                const UINT dpi = GetDpiForWindow(owner);
                RECT bounds{ 0, 0, scale(900, dpi), scale(700, dpi) };
                const DWORD style = WS_OVERLAPPEDWINDOW;
                AdjustWindowRectExForDpi(&bounds, style, FALSE,
                    WS_EX_DLGMODALFRAME, dpi);
                RECT ownerBounds{};
                GetWindowRect(owner, &ownerBounds);
                const int width = bounds.right - bounds.left;
                const int height = bounds.bottom - bounds.top;
                HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME,
                    NoticesWindowClass, L"UVSR Launcher - Notices", style,
                    ownerBounds.left + static_cast<int>(
                        ownerBounds.right - ownerBounds.left - width) / 2,
                    ownerBounds.top + static_cast<int>(
                        ownerBounds.bottom - ownerBounds.top - height) / 2,
                    width, height, owner, nullptr, instance, &state);
                if (!window)
                {
                    if (state.callbackError)
                        std::rethrow_exception(state.callbackError);
                    detail::throw_last_error(
                        "Windows could not open the notices window");
                }
                SendMessageW(state.text, EM_SETLIMITTEXT,
                    static_cast<WPARAM>(0x7ffffffe), 0);
                SetWindowTextW(state.text, notices.c_str());
                SendMessageW(state.text, EM_SETSEL, 0, 0);
                EnableWindow(owner, FALSE);
                ShowWindow(window, SW_SHOW);
                MSG message{};
                bool quit = false;
                int quitCode = 0;
                bool messageFailed = false;
                while (IsWindow(window))
                {
                    const BOOL result = GetMessageW(&message, nullptr, 0, 0);
                    if (result <= 0)
                    {
                        quit = result == 0;
                        quitCode = static_cast<int>(message.wParam);
                        messageFailed = result == -1;
                        break;
                    }
                    if (!IsDialogMessageW(window, &message))
                    {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                }
                EnableWindow(owner, TRUE);
                SetForegroundWindow(owner);
                if (quit)
                    PostQuitMessage(quitCode);
                if (messageFailed)
                    detail::throw_last_error(
                        "Windows could not read a notices window message");
                if (state.callbackError)
                    std::rethrow_exception(state.callbackError);
            }
            catch (const std::exception& error)
            {
                static_cast<void>(show_dialog(owner,
                    L"Notices Could Not Open", widen(error.what()),
                    { { IDOK, L"OK" } }, true));
            }
        }
    }

    int run_ui(
        HINSTANCE instance,
        InstallerEngine& engine,
        bool startWithUninstall,
        std::optional<GUID> continuationId)
    {
        ensure_fonts();
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
        if (!InitCommonControlsEx(&controls))
            detail::throw_last_error(
                "Windows could not initialize launcher controls");
        MainWindow window(instance, engine, startWithUninstall,
            continuationId);
        HWND handle = window.create();
        const BOOL dark = FALSE;
        static_cast<void>(DwmSetWindowAttribute(handle, 20, &dark,
            sizeof(dark)));
        ShowWindow(handle, SW_SHOW);
        UpdateWindow(handle);
        MSG message{};
        for (;;)
        {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result == -1)
                detail::throw_last_error(
                    "Windows could not read a launcher window message");
            if (result == 0)
                break;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        window.rethrow_callback_error();
        return static_cast<int>(message.wParam);
    }
}
