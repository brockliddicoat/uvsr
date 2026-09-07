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
    GetDeviceManager()->m_callbacks.beforePresent = nullptr;
    if (m_PresentationWaitTimer)
        CloseHandle(m_PresentationWaitTimer);
}

void UIRenderer::SetDisplaySyncTestActive(bool active)
{
    if (active == m_ui.DisplaySyncTestActive || (active && m_app->IsSceneBusy()))
        return;
    if (active)
    {
        m_ui.DisplaySyncPosition = 0.0;
        m_ui.ShowUI = true;
        m_SettingsCollapsed = false;
    }
    ResetPresentationTiming();
    m_ui.DisplaySyncTestActive = active;
    uvsr::log::info("Display sync test %s; shared presentation settings retained", active ? "started" : "stopped");
}

void UIRenderer::ResetPresentationTiming()
{
    m_DisplaySyncStart = SyncClockSeconds();
    m_PresentationLastFrame = m_PresentationDeadline = 0.0;
    m_PresentationIntervalCount = m_PresentationIntervalOffset = 0;
}

void UIRenderer::AdvanceDisplayPresentation(float elapsedTimeSeconds)
{
    if (m_ui.DisplaySyncTestActive && m_app->IsSceneBusy())
        SetDisplaySyncTestActive(false);
    const GLFWvidmode* displayMode = SyncDisplayMode(GetDeviceManager()->GetWindow());
    const double refresh = displayMode ? displayMode->refreshRate : 0.0;
    ReconcileDisplayPresentation(m_ui.Presentation, m_PreviousPresentationSettings,
        refresh, m_PresentationRefreshRate);
    m_PresentationRefreshRate = refresh;
    if (!(m_PreviousPresentationSettings == m_ui.Presentation))
    {
        m_PreviousPresentationSettings = m_ui.Presentation;
        ResetPresentationTiming();
    }
    GetDeviceManager()->SetVsyncEnabled(m_ui.Presentation.verticalSynchronization);
    if (glfwGetWindowAttrib(GetDeviceManager()->GetWindow(), GLFW_ICONIFIED))
    {
        m_PresentationLastFrame = m_PresentationDeadline = 0.0;
        return;
    }

    double now = SyncClockSeconds();
    double testTarget = 0.0;
    if (m_ui.DisplaySyncTestActive)
    {
        testTarget = DisplaySyncTargetFps(
            m_DisplaySyncSettings, now - m_DisplaySyncStart, m_PresentationRefreshRate);
    }
    m_PresentationTargetFps = DisplayPresentationTargetFps(
        m_ui.Presentation, m_ui.DisplaySyncTestActive, testTarget);
    if (m_ui.DisplaySyncTestActive)
        m_ui.DisplaySyncPosition = AdvanceDisplaySyncPosition(m_ui.DisplaySyncPosition,
            elapsedTimeSeconds, m_DisplaySyncSettings.speedPixelsPerSecond, m_DisplaySyncSettings.paused);
}

void UIRenderer::PacePresentation()
{
    double now = SyncClockSeconds();
    if (m_PresentationTargetFps <= 0.0)
        m_PresentationDeadline = 0.0;
    while (now < m_PresentationDeadline)
    {
        const double remaining = m_PresentationDeadline - now;
        // timer wake-up latency must not extend every frame. sleep early, then
        // finish the last half millisecond without another scheduler round trip.
        constexpr double precisionWindow = 0.0005;
        if (remaining > precisionWindow)
        {
            const double sleepTime = remaining - precisionWindow;
            LARGE_INTEGER due;
            due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>(sleepTime * 10000000.0));
            if (m_PresentationWaitTimer && SetWaitableTimer(m_PresentationWaitTimer, &due, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject(m_PresentationWaitTimer, static_cast<DWORD>(std::ceil(sleepTime * 1000.0)) + 1);
            else if (sleepTime >= 0.001)
                Sleep(static_cast<DWORD>(sleepTime * 1000.0));
            else
                YieldProcessor();
        }
        else
            YieldProcessor();
        now = SyncClockSeconds();
    }
    m_PresentationDeadline = NextPresentationDeadline(now, m_PresentationTargetFps);
    if (m_PresentationLastFrame != 0.0)
    {
        const double elapsed = now - m_PresentationLastFrame;
        m_PresentationIntervals[m_PresentationIntervalOffset] = float(elapsed * 1000.0);
        m_PresentationIntervalOffset = (m_PresentationIntervalOffset + 1) % m_PresentationIntervals.size();
        m_PresentationIntervalCount = std::min(m_PresentationIntervalCount + 1, m_PresentationIntervals.size());
    }
    m_PresentationLastFrame = now;
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
    const float offset = float(m_ui.DisplaySyncPosition);
    for (float x = origin.x - 384.f + offset; x < end.x; x += 384.f)
    {
        draw->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + 12.f, end.y), IM_COL32(235, 235, 235, 255));
        draw->AddRectFilled(ImVec2(x + 96.f, origin.y), ImVec2(x + 120.f, end.y), IM_COL32(235, 235, 235, 255));
        draw->AddRectFilled(ImVec2(x + 192.f, origin.y), ImVec2(x + 200.f, end.y), IM_COL32(80, 200, 240, 255));
        draw->AddRectFilled(ImVec2(x + 288.f, origin.y), ImVec2(x + 336.f, end.y), IM_COL32(235, 235, 235, 255));
    }
}

void UIRenderer::DrawDeveloperDrawer(float controlWidth)
{
    if (!DrawCollapsingHeader("Developer", "Visual tests using the engine's presentation path."))
    {
        ImGui::Spacing();
        return;
    }
    BeginDrawerBody("##DeveloperBody", controlWidth);
    bool active = m_ui.DisplaySyncTestActive;
    if (ImGui::Checkbox("Display Sync Test", &active))
        SetDisplaySyncTestActive(active);
    ImGui::SetItemTooltip("Show moving bars over the fully rendered scene, retaining its GPU workload and presentation settings. F8 also toggles the test; Esc hides the menu.");

    const float refresh = float(m_PresentationRefreshRate);
    if (refresh > 0.f)
        ImGui::Text("Display mode: %.0f Hz", refresh);
    else
        ImGui::TextDisabled("Display refresh rate unavailable");

    DrawUiBoolean("Vertical Sync", SettingId::PresentationVerticalSynchronization,
        "Enable the frame limiter at the display refresh rate and present complete frames at refresh boundaries. You can then choose a lower frame limit. Turning Vertical Sync off also turns the limiter off.");
    DrawUiBoolean("Frame Rate Limit", SettingId::PresentationFrameRateLimitEnabled,
        "Enable the shared scene and test frame limit. Off retains the chosen rate. Vertical Sync can still limit presentation.");
    if (BeginToggleRegion("##FrameRateLimitControls", m_ui.Presentation.frameRateLimitEnabled))
    {
        const int maximum = PresentationFrameRateMaximum(m_ui.Presentation.verticalSynchronization, refresh);
        int limit = std::min(m_ui.Presentation.frameRateLimit, maximum);
        if (DrawBoundedSlider("Maximum FPS", &limit, 1, maximum, 1, maximum, "%d FPS"))
            ApplyUiSetting(SettingId::PresentationFrameRateLimit, UiSettingsValue::Integer(limit));
        ImGui::SetItemTooltip("Limit scene and test presentation. With Vertical Sync on, the maximum follows the active panel refresh rate.");
        DrawUiReset(SettingId::PresentationFrameRateLimit);
        EndControlRegion();
    }
    const double effectiveCeiling = EffectivePresentationCeiling(
        m_PresentationTargetFps, m_ui.Presentation.verticalSynchronization, refresh);
    if (effectiveCeiling > 0.0)
        ImGui::Text("Effective ceiling: %.1f FPS", effectiveCeiling);
    if (m_ui.Presentation.verticalSynchronization && refresh > 0.f &&
        (m_PresentationTargetFps <= 0.0 || m_PresentationTargetFps > refresh))
        ImGui::TextDisabled("Vertical Sync is limited by the %.0f Hz display.", refresh);
    bool changed = false;
    if (BeginToggleRegion("##DisplaySyncControls", m_ui.DisplaySyncTestActive))
    {
        ImGui::Checkbox("Pause Motion##DisplaySync", &m_DisplaySyncSettings.paused);

        ImGui::TextUnformatted("Test Frame Rate");
        ImGui::SetNextItemWidth(-FLT_MIN);
        constexpr const char* rateLabels[] = { "Frame Rate Limit", "Below Refresh", "Above Refresh", "Fixed FPS", "Sweep FPS" };
        if (ImGui::BeginCombo("##DisplaySyncRate", rateLabels[int(m_DisplaySyncSettings.rateMode)]))
        {
            for (int index = 0; index < 5; ++index)
            {
                const bool selected = index == int(m_DisplaySyncSettings.rateMode);
                if (refresh <= 0.f && (index == 1 || index == 2))
                    continue;
                DrawDropdownOption(rateLabels[index], selected, [&]()
                {
                    m_DisplaySyncSettings.rateMode = static_cast<DisplaySyncRateMode>(index);
                    changed = true;
                });
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (m_DisplaySyncSettings.rateMode == DisplaySyncRateMode::Sweep)
        {
            changed |= DrawBoundedSlider("Minimum FPS##DisplaySync", &m_DisplaySyncSettings.minimumFps,
                20.f, float(MaximumFrameRateLimit), 20.f, float(MaximumFrameRateLimit), "%.0f");
            changed |= DrawBoundedSlider("Maximum FPS##DisplaySync", &m_DisplaySyncSettings.maximumFps,
                m_DisplaySyncSettings.minimumFps, float(MaximumFrameRateLimit), m_DisplaySyncSettings.minimumFps, float(MaximumFrameRateLimit), "%.0f");
            m_DisplaySyncSettings.maximumFps = std::max(m_DisplaySyncSettings.minimumFps, m_DisplaySyncSettings.maximumFps);
            ImGui::SetItemTooltip("The target sweeps over 12 seconds. The shared frame limit and Vertical Sync still apply.");
        }
        else if (m_DisplaySyncSettings.rateMode == DisplaySyncRateMode::Fixed ||
            (refresh <= 0.f && m_DisplaySyncSettings.rateMode != DisplaySyncRateMode::FrameRateLimit))
            changed |= DrawBoundedSlider("Target FPS##DisplaySync", &m_DisplaySyncSettings.targetFps,
                20.f, float(MaximumFrameRateLimit), 20.f, float(MaximumFrameRateLimit), "%.0f");
        DrawBoundedSlider("Speed##DisplaySync", &m_DisplaySyncSettings.speedPixelsPerSecond,
            300.f, 2400.f, 300.f, 2400.f, "%.0f px/s");
        EndControlRegion();
    }
    if (changed)
        ResetPresentationTiming();
    if (m_PresentationIntervalCount > 0)
    {
        double total = 0.0;
        float maximum = 0.f;
        for (size_t index = 0; index < m_PresentationIntervalCount; ++index)
        {
            total += m_PresentationIntervals[index];
            maximum = std::max(maximum, m_PresentationIntervals[index]);
        }
        if (total > 0.0)
            ImGui::Text("Measured: %.1f FPS / peak %.1f ms", 1000.0 * double(m_PresentationIntervalCount) / total, maximum);
        ImGui::PlotLines("##DisplaySyncIntervals", m_PresentationIntervals.data(), int(m_PresentationIntervalCount),
            m_PresentationIntervalCount == m_PresentationIntervals.size() ? int(m_PresentationIntervalOffset) : 0,
            nullptr, 0.f, std::max(25.f, maximum), ImVec2(-1.f, 60.f));
    }
    ImGui::Spacing();
    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (BeginSettingsTree("Advanced##Developer", ImGuiTreeNodeFlags_None,
            "Numeric entry and shared ray traversal controls."))
    {
        DrawUiBoolean("Override Visual Maxes", SettingId::UiOverrideVisualMaxes,
            "Allow numeric entry beyond a slider's visible track, up to the setting's safe supported limits.");
        const WorldSpaceRepresentationSettings& representation =
            m_ui.Representation;
        const WorldSpaceRepresentationStatus& representationStatus =
            m_app->GetWorldSpaceRepresentationStatus();
        DrawUiBoolean(
            "Allow Ray Traversal",
            SettingId::RepresentationAllowRayTraversal,
            "Allow ray traced effects to traverse the shared scene "
            "representation. Their settings stay stored while traversal "
            "is off.");

        const char* representationState = "Inactive";
        switch (representationStatus.state)
        {
        case WorldSpaceRepresentationState::Unsupported:
            representationState = "Unsupported";
            break;
        case WorldSpaceRepresentationState::BuildingBlas:
            representationState = "Building BLAS";
            break;
        case WorldSpaceRepresentationState::BuildingTlas:
            representationState = "Building TLAS";
            break;
        case WorldSpaceRepresentationState::Ready:
            representationState = "Ready";
            break;
        case WorldSpaceRepresentationState::Failed:
            representationState = "Failed";
            break;
        case WorldSpaceRepresentationState::Idle:
        default:
            break;
        }
        if (!representation.allowRayTraversal)
        {
            ImGui::TextDisabled("Status: Ray traversal disabled");
        }
        else
            ImGui::TextDisabled("Status: %s", representationState);
        if (!representationStatus.accelerationStructuresSupported ||
            !representationStatus.rayQueriesSupported)
        {
            ImGui::TextDisabled(
                "Requires DirectX Raytracing 1.1 inline ray queries.");
        }
        EndSettingsTree();
    }

    ImGui::Spacing();
    EndDrawerBody();
    ImGui::Spacing();
}
