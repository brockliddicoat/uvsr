#include "uvsr_ui_internal.h"

namespace
{
    double SyncClockSeconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    const GLFWvidmode* SyncDisplayMode(GLFWwindow* window)
    {
        int x, y, width, height, count;
        glfwGetWindowPos(window, &x, &y);
        glfwGetWindowSize(window, &width, &height);
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        const GLFWvidmode* selected = nullptr;
        int largestArea = -1;
        for (int index = 0; index < count; ++index)
        {
            int monitorX, monitorY;
            glfwGetMonitorPos(monitors[index], &monitorX, &monitorY);
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[index]);
            if (!mode)
                continue;
            const int area = std::max(0, std::min(x + width, monitorX + mode->width) - std::max(x, monitorX)) *
                std::max(0, std::min(y + height, monitorY + mode->height) - std::max(y, monitorY));
            if (area > largestArea)
            {
                largestArea = area;
                selected = mode;
            }
        }
        return selected;
    }
}

UIRenderer::~UIRenderer()
{
    if (m_DisplaySyncWaitTimer)
        CloseHandle(m_DisplaySyncWaitTimer);
}

void UIRenderer::SetDisplaySyncTestActive(bool active)
{
    if (active == m_ui.DisplaySyncTestActive || (active && m_app->IsSceneBusy()))
        return;
    if (active)
    {
        m_DisplaySyncSavedVsync = GetDeviceManager()->IsVsyncEnabled();
        if (!m_DisplaySyncWaitTimer)
            m_DisplaySyncWaitTimer = CreateWaitableTimerExW(nullptr, nullptr,
                CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
        m_DisplaySyncStart = SyncClockSeconds();
        m_DisplaySyncLastFrame = 0.0;
        m_DisplaySyncDeadline = 0.0;
        m_DisplaySyncPosition = 0.0;
        m_DisplaySyncIntervalCount = m_DisplaySyncIntervalOffset = 0;
        m_ui.ShowUI = true;
        m_SettingsCollapsed = false;
    }
    else
    {
        GetDeviceManager()->SetVsyncEnabled(m_DisplaySyncSavedVsync);
        GetDeviceManager()->SetPresentAllowTearing(AdaptiveSyncRequestsPresentTearing(m_ui.AdaptiveSync));
    }
    m_ui.DisplaySyncTestActive = active;
    m_app->ResetImageBasedLightingHistory();
    uvsr::log::info("Display sync test %s", active ? "started" : "stopped; presentation policy restored");
}

void UIRenderer::AdvanceDisplaySyncTest()
{
    if (!m_ui.DisplaySyncTestActive)
        return;
    if (m_app->IsSceneBusy())
    {
        SetDisplaySyncTestActive(false);
        return;
    }
    GetDeviceManager()->SetVsyncEnabled(m_DisplaySyncSettings.vsync);
    GetDeviceManager()->SetPresentAllowTearing(!m_DisplaySyncSettings.vsync);
    if (!glfwGetWindowAttrib(GetDeviceManager()->GetWindow(), GLFW_FOCUSED) ||
        glfwGetWindowAttrib(GetDeviceManager()->GetWindow(), GLFW_ICONIFIED))
    {
        m_DisplaySyncLastFrame = m_DisplaySyncDeadline = 0.0;
        return;
    }

    double now = SyncClockSeconds();
    m_DisplaySyncCurrentTarget = DisplaySyncTargetFps(m_DisplaySyncSettings, now - m_DisplaySyncStart);
    const double remaining = m_DisplaySyncDeadline - now;
    if (remaining > 0.0)
    {
        // this wait includes prior Present time in the budget and changes no system timer policy.
        LARGE_INTEGER due;
        due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>(remaining * 10000000.0));
        if (m_DisplaySyncWaitTimer && SetWaitableTimer(m_DisplaySyncWaitTimer, &due, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(m_DisplaySyncWaitTimer, 100);
        else
            Sleep(static_cast<DWORD>(std::ceil(remaining * 1000.0)));
        now = SyncClockSeconds();
    }
    m_DisplaySyncDeadline = NextDisplaySyncDeadline(m_DisplaySyncDeadline, now, 1.0 / m_DisplaySyncCurrentTarget);
    if (m_DisplaySyncLastFrame != 0.0)
    {
        const double elapsed = now - m_DisplaySyncLastFrame;
        m_DisplaySyncIntervals[m_DisplaySyncIntervalOffset] = float(elapsed * 1000.0);
        m_DisplaySyncIntervalOffset = (m_DisplaySyncIntervalOffset + 1) % m_DisplaySyncIntervals.size();
        m_DisplaySyncIntervalCount = std::min(m_DisplaySyncIntervalCount + 1, m_DisplaySyncIntervals.size());
        m_DisplaySyncPosition = AdvanceDisplaySyncPosition(m_DisplaySyncPosition, elapsed,
            m_DisplaySyncSettings.speedPixelsPerSecond, m_DisplaySyncSettings.paused);
    }
    m_DisplaySyncLastFrame = now;
}

void UIRenderer::DrawDisplaySyncTest()
{
    if (!m_ui.DisplaySyncTestActive)
        return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 end(origin.x + viewport->Size.x, origin.y + viewport->Size.y);
    draw->AddRectFilled(origin, end, IM_COL32(22, 22, 22, 255));
    const float offset = float(m_DisplaySyncPosition);
    for (float x = origin.x - 384.f + offset; x < end.x; x += 384.f)
    {
        draw->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + 12.f, end.y), IM_COL32(235, 235, 235, 255));
        draw->AddRectFilled(ImVec2(x + 96.f, origin.y), ImVec2(x + 120.f, end.y), IM_COL32(235, 235, 235, 255));
        draw->AddRectFilled(ImVec2(x + 192.f, origin.y), ImVec2(x + 200.f, end.y), IM_COL32(80, 200, 240, 255));
        draw->AddRectFilled(ImVec2(x + 288.f, origin.y), ImVec2(x + 336.f, end.y), IM_COL32(235, 235, 235, 255));
    }
    draw->AddRectFilled(ImVec2(origin.x, end.y - 34.f), end, IM_COL32(22, 22, 22, 255));
    char label[192];
    snprintf(label, sizeof(label), "Display Sync Test | VSync %s | target %.0f FPS | Esc: menu | F8: stop",
        GetDeviceManager()->IsVsyncEnabled() ? "On" : "Off", m_DisplaySyncCurrentTarget);
    draw->AddText(ImVec2(origin.x + 12.f, end.y - 26.f), IM_COL32(235, 235, 235, 255), label);
}

void UIRenderer::DrawDeveloperDrawer(float controlWidth)
{
    if (!DrawCollapsingHeader("Developer", "Visual tests using the engine's presentation path."))
        return;
    BeginDrawerBody("##DeveloperBody", controlWidth);
    ImGui::TextUnformatted("Display Sync Test");
    ImGui::TextWrapped("Watch for sideways breaks in the moving vertical bars. Straight bars that jump indicate uneven frame delivery.");
    if (ImGui::Button(m_ui.DisplaySyncTestActive ? "Stop Test##DisplaySync" : "Start Test##DisplaySync"))
        SetDisplaySyncTestActive(!m_ui.DisplaySyncTestActive);
    ImGui::SameLine();
    ImGui::TextDisabled("F8");

    const GLFWvidmode* mode = SyncDisplayMode(GetDeviceManager()->GetWindow());
    const float refresh = mode ? float(mode->refreshRate) : 0.f;
    if (refresh > 0.f)
        ImGui::Text("Display mode: %.0f Hz (not live VRR)", refresh);
    else
        ImGui::TextDisabled("Display refresh rate unavailable");

    bool changed = ImGui::Checkbox("VSync##DisplaySync", &m_DisplaySyncSettings.vsync);
    ImGui::SetItemTooltip("On waits for refresh. Off permits the Windows VRR / tearing path. Disable VRR in your driver or display for the tearing reference.");
    ImGui::Checkbox("Pause Motion##DisplaySync", &m_DisplaySyncSettings.paused);
    changed |= ImGui::Checkbox("Sweep FPS##DisplaySync", &m_DisplaySyncSettings.sweep);
    if (m_DisplaySyncSettings.sweep)
    {
        changed |= DrawBoundedSlider("Minimum FPS##DisplaySync", &m_DisplaySyncSettings.minimumFps,
            20.f, 500.f, 20.f, 500.f, "%.0f");
        changed |= DrawBoundedSlider("Maximum FPS##DisplaySync", &m_DisplaySyncSettings.maximumFps,
            m_DisplaySyncSettings.minimumFps, 500.f, m_DisplaySyncSettings.minimumFps, 500.f, "%.0f");
        m_DisplaySyncSettings.maximumFps = std::max(m_DisplaySyncSettings.minimumFps, m_DisplaySyncSettings.maximumFps);
        ImGui::TextWrapped("The target changes gradually over 12 seconds. Keep both limits inside your display's VRR range.");
    }
    else
        changed |= DrawBoundedSlider("Target FPS##DisplaySync", &m_DisplaySyncSettings.targetFps,
            20.f, 500.f, 20.f, 500.f, "%.0f");
    DrawBoundedSlider("Speed##DisplaySync", &m_DisplaySyncSettings.speedPixelsPerSecond,
        300.f, 2400.f, 300.f, 2400.f, "%.0f px/s");
    if (refresh > 0.f)
    {
        if (ImGui::Button("Above Refresh##DisplaySync"))
        {
            m_DisplaySyncSettings.targetFps = std::clamp(refresh + 13.f, 20.f, 500.f);
            m_DisplaySyncSettings.sweep = false;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Below Refresh##DisplaySync"))
        {
            m_DisplaySyncSettings.targetFps = std::clamp(refresh * 0.58f, 20.f, 500.f);
            m_DisplaySyncSettings.sweep = false;
            changed = true;
        }
    }
    if (changed)
    {
        m_DisplaySyncDeadline = 0.0;
        m_DisplaySyncIntervalCount = m_DisplaySyncIntervalOffset = 0;
        m_DisplaySyncStart = SyncClockSeconds();
    }
    if (m_ui.DisplaySyncTestActive)
    {
        double total = 0.0;
        float maximum = 0.f;
        for (size_t index = 0; index < m_DisplaySyncIntervalCount; ++index)
        {
            total += m_DisplaySyncIntervals[index];
            maximum = std::max(maximum, m_DisplaySyncIntervals[index]);
        }
        if (total > 0.0)
            ImGui::Text("Measured: %.1f FPS / peak %.1f ms", 1000.0 * double(m_DisplaySyncIntervalCount) / total, maximum);
        ImGui::PlotLines("##DisplaySyncIntervals", m_DisplaySyncIntervals.data(), int(m_DisplaySyncIntervalCount),
            m_DisplaySyncIntervalCount == m_DisplaySyncIntervals.size() ? int(m_DisplaySyncIntervalOffset) : 0,
            "Frame interval (ms)", 0.f, std::max(25.f, maximum), ImVec2(-1.f, 60.f));
        ImGui::Text("Applied VSync: %s", GetDeviceManager()->IsVsyncEnabled() ? "On" : "Off");
        if (!GetDeviceManager()->IsPresentAllowTearingSupported())
            ImGui::TextWrapped("VRR / tearing presentation is unavailable on this system. The unsynchronized comparison is inconclusive.");
        else if (glfwGetWindowMonitor(GetDeviceManager()->GetWindow()))
            ImGui::TextWrapped("Use a maximized window for the windowed VRR / tearing comparison.");
        else if (!GetDeviceManager()->IsVsyncEnabled())
            ImGui::TextWrapped("VRR eligible, activation unverified. Check the driver indicator or display's live refresh counter.");
        if (!m_DisplaySyncWaitTimer)
            ImGui::TextWrapped("High-resolution waiting is unavailable. Limiter timing may be less precise.");
    }
    ImGui::TextWrapped("For tearing: disable display/driver VRR, choose Above Refresh, and compare VSync off/on. For VRR: leave VSync off, choose Below Refresh, and compare driver/display VRR off/on.");
    ImGui::TextWrapped("Maximize this window. Run without other games. Esc hides the menu; F8 stops the test and restores scene presentation. Low FPS can still look choppy with working VRR.");
    EndDrawerBody();
}
