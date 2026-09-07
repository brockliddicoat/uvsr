#include "ui.h"
#include "progress.h"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <deque>
#include <mutex>
#include <thread>

namespace uvsr::launcher::ui
{
    namespace
    {
        enum Id { Title = 10, Introduction, Status, Desktop, Install, Launch, Update, Uninstall, Notices,
            Phase, Detail, ProgressBar, Details, Copy, Log };
        constexpr UINT ProgressMessage = WM_APP + 1, CompleteMessage = WM_APP + 2, StartupMessage = WM_APP + 3;
        struct Window
        {
            Installer& installer;
            bool startUninstall;
            std::optional<std::string> requestedContinuation;
            HWND window = nullptr;
            std::map<int, HWND> controls;
            std::unique_ptr<Fonts> fonts;
            Palette palette = Palette::Current();
            HBRUSH background = CreateSolidBrush(palette.window), card = CreateSolidBrush(palette.card);
            Snapshot snapshot;
            bool busy = false, canCancel = true, rendererBusy = false, closingIntent = false, detailsVisible = false, obsolete = false, errorTone = false;
            int scroll = 0, contentHeight = 0;
            std::optional<int> percent = 0;
            std::wstring log;
            std::jthread worker;
            std::mutex mutex;
            std::deque<Progress> updates;
            std::exception_ptr failure;
            std::function<void()> success;
            std::function<bool(std::exception_ptr)> handleFailure;
            std::optional<Updates> checkedUpdates;
            std::optional<Result> operationResult;
            std::vector<ProcessIdentity> closeTargets;
            ~Window() { if (worker.joinable()) { worker.request_stop(); worker.join(); } DeleteObject(background); DeleteObject(card); }
            HWND At(int id) const { return controls.at(id); }
            void Text(int id, std::wstring_view text) { auto copy = std::wstring(text); SetWindowTextW(At(id), copy.c_str()); }
            void Add(int id, const wchar_t* type, std::wstring_view text, DWORD style = 0, int points = 100, bool bold = false)
            { controls[id] = Control(window, type, text, style, id, fonts->Get(points, bold)); }
            void Build()
            {
                fonts = std::make_unique<Fonts>(GetDpiForWindow(window));
                Add(Title, L"STATIC", L"UVSR Launcher", SS_NOPREFIX, 250, true);
                Add(Introduction, L"STATIC", L"Install, launch, and keep UVSR up to date on Windows 11. The launcher handles downloads and setup for you.", SS_NOPREFIX);
                Add(Status, L"STATIC", L"Checking this PC...", SS_NOPREFIX, 115, true);
                Add(Desktop, L"BUTTON", L"Create a desktop shortcut for UVSR Launcher", BS_AUTOCHECKBOX | WS_TABSTOP);
                SendMessageW(At(Desktop), BM_SETCHECK, BST_CHECKED, 0);
                for (const auto& [id, label] : std::initializer_list<std::pair<int, const wchar_t*>>{{Install,L"Install"}, {Launch,L"Launch"}, {Update,L"Update"}, {Uninstall,L"Uninstall"}, {Notices,L"Notices"}, {Details,L"Details"}, {Copy,L"Copy"}})
                    Add(id, L"BUTTON", label, BS_OWNERDRAW | WS_TABSTOP);
                Add(Phase, L"STATIC", L"Ready", SS_NOPREFIX, 100, true);
                Add(Detail, L"STATIC", L"Choose an action above.", SS_NOPREFIX);
                Add(ProgressBar, L"STATIC", L"Operation progress: 0 percent", SS_OWNERDRAW);
                Add(Log, L"EDIT", L"", ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 90);
                SendMessageW(At(Log), EM_SETLIMITTEXT, 200000, 0);
                ShowWindow(At(Copy), SW_HIDE); ShowWindow(At(Log), SW_HIDE);
                SetTimer(window, 1, 750, nullptr);
                FitWindow(window, 840, 720, true); Layout();
            }
            void Layout()
            {
                if (controls.empty()) return;
                RECT client{}; GetClientRect(window, &client);
                const auto s = [&](int x) { return Scale(window, x); };
                const int width = std::max(s(160), int(client.right) - s(56)); int y = s(24);
                const auto move = [&](int id, int x, int yy, int w, int h) { Move(At(id), x, yy - scroll, w, h); };
                move(Title, s(28), y, width, s(46)); y += s(50);
                move(Introduction, s(28), y, width, s(48)); y += s(62);
                move(Status, s(44), y + s(13), width - s(32), s(44)); y += s(78);
                move(Desktop, s(30), y, width, s(28)); y += s(42);
                int x = s(28); const int buttonWidth = s(144), gap = s(8);
                for (int id : {Install, Launch, Update, Uninstall, Notices})
                {
                    if (x != s(28) && x + buttonWidth > client.right - s(28)) { x = s(28); y += s(52); }
                    move(id, x, y, buttonWidth, s(44)); x += buttonWidth + gap;
                }
                y += s(62); move(Phase, s(28), y, width, s(25)); y += s(29);
                move(Detail, s(28), y, width, s(46)); y += s(54);
                move(ProgressBar, s(28), y, width, s(7)); y += s(19);
                move(Details, s(28), y, buttonWidth, s(44)); move(Copy, s(28) + buttonWidth + gap, y, buttonWidth, s(44)); y += s(56);
                if (detailsVisible)
                {
                    const int height = std::max(s(180), int(client.bottom) - y - s(24));
                    move(Log, s(40), y + s(12), width - s(24), height - s(24)); y += height;
                }
                contentHeight = y + s(24);
                SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
                info.nMin = 0; info.nMax = contentHeight - 1; info.nPage = UINT(client.bottom); info.nPos = scroll;
                SetScrollInfo(window, SB_VERT, &info, TRUE);
                const int bounded = std::clamp(scroll, 0, std::max(0, contentHeight - int(client.bottom)));
                if (bounded != scroll) { scroll = bounded; Layout(); return; }
                InvalidateRect(window, nullptr, TRUE);
            }
            void DetailsShown(bool visible)
            {
                detailsVisible = visible; Text(Details, visible ? L"Hide Details" : L"Details");
                ShowWindow(At(Copy), visible ? SW_SHOW : SW_HIDE); ShowWindow(At(Log), visible ? SW_SHOW : SW_HIDE); Layout();
            }
            void RevealFocus()
            {
                const HWND focused = GetFocus();
                if (!focused || !IsChild(window, focused)) return;
                RECT bounds{}, client{};
                GetWindowRect(focused, &bounds); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
                GetClientRect(window, &client);
                const int margin = Scale(window, 8);
                if (bounds.top < margin) scroll += bounds.top - margin;
                else if (bounds.bottom > client.bottom - margin) scroll += bounds.bottom - client.bottom + margin;
                else return;
                scroll = std::clamp(scroll, 0, std::max(0, contentHeight - int(client.bottom)));
                Layout();
            }
            void Append(std::string_view text)
            {
                log += Wide(text); log += L"\r\n";
                if (log.size() > 200000) log.erase(0, log.size() - 200000);
                Text(Log, log); SendMessageW(At(Log), EM_SETSEL, WPARAM(-1), -1); SendMessageW(At(Log), EM_SCROLLCARET, 0, 0);
            }
            void StatusOutput(std::string_view phase, std::string_view detail, std::optional<int> value, bool error = false)
            {
                Text(Phase, Wide(phase)); Text(Detail, Wide(detail)); errorTone = error;
                percent = value ? std::optional<int>(std::clamp(*value, 0, 100)) : std::nullopt;
                Text(ProgressBar, L"Operation progress: " + ProgressText(percent));
                NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, At(ProgressBar), OBJID_CLIENT, CHILDID_SELF);
                InvalidateRect(At(ProgressBar), nullptr, TRUE); InvalidateRect(At(Phase), nullptr, TRUE); InvalidateRect(At(Detail), nullptr, TRUE);
            }
            void Refresh()
            {
                try
                {
                    snapshot = installer.Inspect(); Text(Status, Wide(snapshot.summary));
                    SendMessageW(At(Desktop), BM_SETCHECK, installer.DesktopPreference() ? BST_CHECKED : BST_UNCHECKED, 0);
                }
                catch (const std::exception& error) { snapshot = {}; Text(Status, Wide(error.what())); }
                Buttons();
            }
            void Buttons()
            {
                const bool available = !busy && !obsolete;
                const bool installed = snapshot.installed && !snapshot.damaged;
                auto processes = snapshot.installed ? InspectProcesses(installer.GetPaths().Versions(), Component::Renderer) : Processes{};
                closingIntent = !processes.found.empty();
                Text(Install, installed ? L"Installed" : L"Install"); Text(Launch, closingIntent ? L"Close" : L"Launch");
                for (int id : {Install, Update, Desktop}) EnableWindow(At(id), available);
                EnableWindow(At(Uninstall), available && snapshot.installation.has_value());
                EnableWindow(At(Launch), available && installed && !processes.uncertain);
                EnableWindow(At(Notices), !busy); InvalidateRect(At(Launch), nullptr, TRUE);
            }
            void Error(std::exception_ptr errorState)
            {
                try { std::rethrow_exception(errorState); }
                catch (const std::exception& error)
                {
                    Append(error.what()); StatusOutput("UVSR Launcher Stopped", error.what(), 0, true); DetailsShown(true);
                    Dialog(window, L"UVSR Launcher Stopped", Wide(error.what()), {}, true);
                }
                catch (...) { Dialog(window, L"UVSR Launcher Stopped", L"An unexpected operation failure occurred.", {}, true); }
            }
            void Begin(std::string_view phase, std::string_view detail,
                std::function<void(std::stop_token, const Report&)> task, std::function<void()> completed = {},
                std::function<bool(std::exception_ptr)> failed = {}, bool renderer = false)
            {
                Require(!busy, "Another launcher operation is already active.");
                if (worker.joinable()) worker.join();
                busy = true; rendererBusy = renderer; canCancel = !renderer;
                failure = {}; success = std::move(completed); handleFailure = std::move(failed);
                log.clear(); Text(Log, L""); StatusOutput(phase, detail, {}); Buttons();
                worker = std::jthread([this, task = std::move(task)](std::stop_token stop)
                {
                    const Report report = [this](const Progress& progress)
                    { { std::lock_guard lock(mutex); updates.push_back(progress); } PostMessageW(window, ProgressMessage, 0, 0); };
                    try { task(stop, report); } catch (...) { failure = std::current_exception(); }
                    PostMessageW(window, CompleteMessage, 0, 0);
                });
            }
            void Drain()
            {
                std::deque<Progress> pending;
                { std::lock_guard lock(mutex); pending.swap(updates); }
                for (const auto& progress : pending)
                {
                    Append(progress.detail);
                    if (!progress.phase.empty()) { canCancel = progress.canCancel && !rendererBusy; StatusOutput(progress.phase, progress.detail, progress.percent); }
                }
            }
            void Completed()
            {
                worker.join(); Drain(); const bool cancelled = worker.get_stop_token().stop_requested();
                busy = rendererBusy = false; canCancel = true;
                auto completed = std::move(success); auto failed = std::move(handleFailure); const auto error = failure;
                Refresh();
                if (error)
                {
                    if (cancelled) StatusOutput("Operation Cancelled", "The previous installed UVSR version was preserved.", 0);
                    else if (!failed || !failed(error)) Error(error);
                }
                else if (completed) completed();
            }
            bool DesktopChecked() const { return SendMessageW(At(Desktop), BM_GETCHECK, 0, 0) == BST_CHECKED; }
            void ResultReady()
            {
                if (!operationResult) return;
                const auto result = std::exchange(operationResult, std::nullopt).value();
                StatusOutput(result.cleanupScheduled ? "UVSR Uninstall Ready" : "UVSR is ready", result.message, 100);
                if (result.cleanupScheduled) { obsolete = true; DestroyWindow(window); return; }
                if (!result.relaunch.empty())
                {
                    obsolete = true;
                    for (;;)
                    {
                        try
                        {
                            std::vector<std::wstring> arguments;
                            if (result.continuation) arguments = {L"--continue-uvsr-update", Wide(*result.continuation)};
                            StartProcess(result.relaunch, arguments); DestroyWindow(window); return;
                        }
                        catch (const std::exception& error)
                        {
                            if (Dialog(window, L"Open Updated UVSR Launcher", Wide(error.what()), {{"retry",L"Retry",true}, {"close",L"Close",false,true}}, true) != "retry")
                            { DestroyWindow(window); return; }
                        }
                    }
                }
            }
            void RunOperation(Operation operation, std::optional<std::string> continuation = {})
            {
                const bool desktop = DesktopChecked();
                Begin(operation == Operation::Uninstall ? "Removing UVSR" : "Installing UVSR", "Preparing the selected operation.",
                    [this, operation, desktop](std::stop_token stop, const Report& report) { operationResult = installer.Execute(operation, desktop, stop, report); },
                    [this, continuation] { if (continuation) installer.CompleteContinuation(*continuation); ResultReady(); });
            }
            void InstallClicked()
            {
                Refresh(); std::string choice;
                if (snapshot.installed && !snapshot.damaged)
                {
                    choice = Dialog(window, L"UVSR Is Already Installed", L"UVSR is ready to use. You can launch it now or reinstall its trusted package.",
                        {{"launch",L"Launch",true}, {"reinstall",L"Reinstall"}, {"cancel",L"Cancel",false,true}});
                    if (choice == "launch") LaunchClicked(); else if (choice == "reinstall") RunOperation(Operation::Reinstall);
                }
                else if (snapshot.damaged)
                {
                    choice = Dialog(window, L"UVSR Needs Repair", L"Some installed files are missing or changed. Reinstall UVSR to restore it. Your settings and history will be kept.",
                        {{"reinstall",L"Reinstall",true}, {"cancel",L"Cancel",false,true}});
                    if (choice == "reinstall") RunOperation(Operation::Reinstall);
                }
                else if (Dialog(window, L"Install UVSR", L"UVSR Launcher will authenticate, download, and install the latest renderer package. The installed package remains available during a safe update.",
                    {{"install",L"Install",true}, {"cancel",L"Cancel",false,true}}) == "install") RunOperation(Operation::Install);
            }
            void UninstallClicked()
            {
                if (Dialog(window, L"Uninstall UVSR?", L"UVSR Launcher will remove its owned program files and shortcuts after this window closes. Your renderer settings and history will be kept.",
                    {{"uninstall",L"Uninstall",false,false,true}, {"cancel",L"Cancel",true,true}}) == "uninstall") RunOperation(Operation::Uninstall);
            }
            void CheckUpdates()
            {
                const bool desktop = DesktopChecked();
                Begin("Checking for updates", "Checking UVSR Engine and UVSR Launcher.",
                    [this, desktop](std::stop_token stop, const Report& report) { checkedUpdates = installer.CheckUpdates(desktop, stop, report); }, [this] { SelectUpdates(); });
            }
            void SelectUpdates()
            {
                const auto result = std::exchange(checkedUpdates, std::nullopt).value();
                const auto selectable = [](const ComponentUpdate& update) { return update.state == UpdateState::UpdateAvailable || update.state == UpdateState::RepairNeeded; };
                const bool available = selectable(result.renderer) || selectable(result.launcher);
                const bool failed = result.renderer.state == UpdateState::CheckFailed || result.launcher.state == UpdateState::CheckFailed;
                if (!available && !failed)
                {
                    const auto message = result.renderer.state == UpdateState::NotInstalled ? "UVSR Launcher is up to date. UVSR is not installed yet." : "UVSR Engine and UVSR Launcher are up to date.";
                    StatusOutput("Up to Date", message, 100); Dialog(window, L"Everything Is Up to Date", Wide(message)); return;
                }
                std::vector<Choice> choices{
                    {L"UVSR Engine", Wide(result.renderer.detail), selectable(result.renderer), selectable(result.renderer)},
                    {L"UVSR Launcher", Wide(result.launcher.detail), selectable(result.launcher), selectable(result.launcher)}};
                std::vector<Action> actions;
                if (failed) actions.push_back({"retry",L"Check Again"});
                actions.push_back({"update",L"Update Selected",true}); actions.push_back({"cancel",L"Cancel",false,true});
                const auto text = !failed ? L"Available updates are selected for you. You can install either one or both." : available ?
                    L"Available updates are selected for you. One or more checks failed; review the details or check again." : L"One or more update checks failed. Review the details or check again.";
                const auto selection = Dialog(window, available ? L"Choose What to Update" : L"Update Check Results", text, actions, false, &choices);
                if (selection == "retry") { CheckUpdates(); return; }
                if (selection != "update") { StatusOutput("Update Cancelled", "No updates were selected. Nothing was changed.", 0); return; }
                if (choices[1].selected)
                {
                    Require(result.launcher.feed.has_value(), "The selected launcher update has no trusted release record.");
                    const auto feed = *result.launcher.feed; const bool desktop = DesktopChecked(), continuation = choices[0].selected;
                    Begin("Updating UVSR Launcher", "Downloading the selected launcher update.",
                        [this, feed, desktop, continuation](std::stop_token stop, const Report& report) { operationResult = installer.UpdateLauncher(feed, desktop, continuation, stop, report); }, [this] { ResultReady(); });
                }
                else if (choices[0].selected) RunOperation(snapshot.damaged ? Operation::Reinstall : Operation::Update);
                else StatusOutput("No Updates Selected", "Nothing was changed.", 0);
            }
            void TryClose(bool force)
            {
                const auto targets = closeTargets;
                Begin("Closing UVSR", "Waiting for UVSR to close safely.",
                    [targets, force](std::stop_token, const Report& report) { CloseProcesses(targets, report, force); },
                    [this] { StatusOutput("UVSR Closed", "UVSR closed successfully.", 100); },
                    [this, force](std::exception_ptr error)
                    {
                        if (force) return false;
                        try { std::rethrow_exception(error); } catch (const std::exception& message) { if (std::string_view(message.what()) != "UVSR is still running.") return false; }
                        auto action = Dialog(window, L"UVSR Is Still Running", L"UVSR has not closed yet. You can keep waiting or force it to close. Forcing it may lose unsaved work.",
                            {{"wait",L"Keep Waiting",true}, {"force",L"Force Close",false,false,true}, {"cancel",L"Cancel",false,true}});
                        if (action == "wait" || action == "force") TryClose(action == "force");
                        else StatusOutput("Close Cancelled", "UVSR is still running. Nothing was changed.", 0);
                        return true;
                    }, true);
            }
            void LaunchClicked()
            {
                const auto renderedClose = closingIntent;
                auto processes = InspectProcesses(installer.GetPaths().Versions(), Component::Renderer);
                if (processes.uncertain || renderedClose != !processes.found.empty()) { Buttons(); return; }
                if (renderedClose) { closeTargets = std::move(processes.found); TryClose(false); return; }
                Begin("Launching UVSR", "Waiting for UVSR to start.", [this](std::stop_token, const Report&)
                {
                    installer.Launch();
                    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(6);
                    while (InspectProcesses(installer.GetPaths().Versions(), Component::Renderer).found.empty())
                    { Require(std::chrono::steady_clock::now() < end, "UVSR was launched, but the process did not appear to start."); std::this_thread::sleep_for(std::chrono::milliseconds(150)); }
                }, [this] { StatusOutput("UVSR Started", "UVSR started successfully.", 100); }, {}, true);
            }
            void Startup()
            {
                Refresh();
                if (startUninstall) { UninstallClicked(); return; }
                const auto continuation = [this]
                {
                    auto pending = installer.PendingContinuation(requestedContinuation ? std::optional<std::string_view>(*requestedContinuation) : std::nullopt);
                    if (pending) RunOperation(snapshot.damaged ? Operation::Reinstall : Operation::Update, pending);
                };
                if (!snapshot.installation) { continuation(); return; }
                const bool desktop = DesktopChecked();
                Begin("Preparing UVSR Launcher", "Checking the installed launcher and shortcuts.",
                    [this, desktop](std::stop_token stop, const Report& report) { installer.Ready(desktop, stop, report); }, continuation);
            }
            bool Close()
            {
                if (obsolete || !busy) return true;
                if (rendererBusy) Dialog(window, L"UVSR Launcher Is Busy", L"UVSR Launcher is waiting for UVSR to finish starting or closing. Try closing the launcher again after this action finishes.");
                else if (!canCancel) Dialog(window, L"Setup Is Finishing", L"UVSR Launcher is committing the verified installation. Wait for the result before closing this window.");
                else if (Dialog(window, L"Stop This Operation?", L"The active UVSR version will be preserved. Any verified partial download can resume next time.",
                    {{"stop",L"Stop",false,false,true}, {"continue",L"Keep Working",true,true}}) == "stop") worker.request_stop();
                return false;
            }
            void Paint()
            {
                PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT client{}; GetClientRect(window, &client); FillRect(dc, &client, background);
                RECT status{}; GetWindowRect(At(Status), &status); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&status), 2);
                InflateRect(&status, Scale(window, 16), Scale(window, 13)); FillRect(dc, &status, card);
                if (detailsVisible)
                { RECT details{}; GetWindowRect(At(Log), &details); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&details), 2); InflateRect(&details, Scale(window, 12), Scale(window, 12)); FillRect(dc, &details, card); }
                EndPaint(window, &paint);
            }
            LRESULT Message(UINT message, WPARAM wParam, LPARAM lParam)
            {
                switch (message)
                {
                case WM_CREATE: Build(); PostMessageW(window, StartupMessage, 0, 0); return 0;
                case StartupMessage: Startup(); return 0;
                case ProgressMessage: Drain(); return 0;
                case CompleteMessage: Completed(); return 0;
                case WM_TIMER: if (!busy) Buttons(); return 0;
                case WM_COMMAND:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        switch (LOWORD(wParam))
                        {
                        case Details: DetailsShown(!detailsVisible); break;
                        case Copy: CopyText(window, log); break;
                        case Install: if (!busy) InstallClicked(); break;
                        case Launch: if (!busy) LaunchClicked(); break;
                        case Update: if (!busy) CheckUpdates(); break;
                        case Uninstall: if (!busy) UninstallClicked(); break;
                        case Notices:
                            Dialog(window, L"UVSR Launcher - Notices", Wide("UVSR LICENSE\n\n" + ResourceText(103) + "\n\nNOTO SANS FONT LICENSE\n\n" + ResourceText(104) + "\n\nzlib\n\n" + ResourceText(105)), {}, false, nullptr, true); break;
                        }
                    }
                    return 0;
                case WM_CLOSE: if (Close()) DestroyWindow(window); return 0;
                case WM_DESTROY: KillTimer(window, 1); PostQuitMessage(0); return 0;
                case WM_SIZE: Layout(); return 0;
                case WM_PAINT: Paint(); return 0;
                case WM_ERASEBKGND: return 1;
                case WM_DPICHANGED:
                    fonts = std::make_unique<Fonts>(HIWORD(wParam));
                    for (auto [id, control] : controls) SendMessageW(control, WM_SETFONT, WPARAM(fonts->Get(id == Title ? 250 : id == Status ? 115 : id == Log ? 90 : 100, id == Title || id == Status || id == Phase)), TRUE);
                    FitWindow(window, 840, 720, false); Layout(); return 0;
                case WM_DISPLAYCHANGE: FitWindow(window, 840, 720, false); Layout(); return 0;
                case WM_SETTINGCHANGE:
                    palette = Palette::Current(); DeleteObject(background); DeleteObject(card);
                    background = CreateSolidBrush(palette.window); card = CreateSolidBrush(palette.card);
                    if (wParam == SPI_SETWORKAREA) { FitWindow(window, 840, 720, false); Layout(); }
                    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
                    return 0;
                case WM_VSCROLL:
                {
                    SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(window, SB_VERT, &info);
                    switch (LOWORD(wParam))
                    {
                    case SB_LINEUP: scroll -= Scale(window, 24); break;
                    case SB_LINEDOWN: scroll += Scale(window, 24); break;
                    case SB_PAGEUP: scroll -= int(info.nPage); break;
                    case SB_PAGEDOWN: scroll += int(info.nPage); break;
                    case SB_THUMBTRACK: scroll = info.nTrackPos; break;
                    }
                    scroll = std::clamp(scroll, 0, std::max(0, contentHeight - int(info.nPage))); Layout(); return 0;
                }
                case WM_MOUSEWHEEL:
                    scroll = std::max(0, scroll - GET_WHEEL_DELTA_WPARAM(wParam) * Scale(window, 72) / WHEEL_DELTA); Layout(); return 0;
                case WM_DRAWITEM:
                {
                    const auto& draw = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                    if (draw.CtlID == ProgressBar)
                    {
                        DrawProgress(draw.hDC, draw.rcItem, percent, palette.disabled, errorTone ? palette.danger : palette.accent);
                    }
                    else
                    {
                        auto colors = palette; bool primary = draw.CtlID == Install;
                        if (draw.CtlID == Launch && snapshot.installed && !snapshot.damaged) { colors.accent = closingIntent ? RGB(186,35,35) : RGB(24,114,78); primary = true; }
                        DrawButton(draw, fonts->Get(), colors, primary, draw.CtlID == Uninstall);
                    }
                    return TRUE;
                }
                case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORBTN:
                {
                    HDC dc = HDC(wParam); HWND child = HWND(lParam); const int id = GetDlgCtrlID(child);
                    COLORREF color = palette.text;
                    if (id == Introduction || id == Detail || id == Log) color = palette.muted;
                    if (id == Status) color = snapshot.damaged ? palette.danger : snapshot.installed ? palette.success : palette.muted;
                    if ((id == Phase || id == Detail) && errorTone) color = palette.danger;
                    SetTextColor(dc, color); SetBkColor(dc, id == Status || id == Log ? palette.card : palette.window);
                    return LRESULT(id == Status || id == Log ? card : background);
                }
                }
                return DefWindowProcW(window, message, wParam, lParam);
            }
            static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
            {
                auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
                if (message == WM_NCCREATE)
                { self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); self->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, LONG_PTR(self)); }
                if (!self) return DefWindowProcW(window, message, wParam, lParam);
                try { return self->Message(message, wParam, lParam); }
                catch (const std::exception& error)
                {
                    if (message == WM_CREATE)
                    { MessageBoxW(window, Wide(error.what()).c_str(), L"UVSR Launcher", MB_OK | MB_ICONERROR); return -1; }
                    try { self->Error(std::current_exception()); }
                    catch (...) { MessageBoxW(window, Wide(error.what()).c_str(), L"UVSR Launcher", MB_OK | MB_ICONERROR); }
                    return 0;
                }
            }
        };
    }
    int Run(Installer& installer, bool uninstall, std::optional<std::string> continuation)
    {
        EnsureFonts(); INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
        WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = Window::Proc; cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.lpszClassName = L"UVSR.NativeLauncher";
        WinCheck(RegisterClassExW(&cls) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "Register launcher window");
        Window state{installer, uninstall, std::move(continuation)};
        HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"UVSR Launcher", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT, 840, 720, nullptr, nullptr, cls.hInstance, &state);
        WinCheck(window != nullptr, "Open launcher window"); ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window);
        MSG message{}; BOOL result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
        {
            if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN)
            {
                const auto focus = GetFocus();
                const int id = GetDlgCtrlID(focus);
                const auto target = (id >= Install && id <= Notices) || id == Details || id == Copy
                    ? focus : state.At(IsWindowEnabled(state.At(Launch)) ? Launch : Install);
                if (IsWindowEnabled(target)) SendMessageW(target, BM_CLICK, 0, 0);
            }
            else if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
            state.RevealFocus();
        }
        return result < 0 ? 1 : int(message.wParam);
    }
}
