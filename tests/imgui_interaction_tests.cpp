#include "imgui.h"
#include "imgui_internal.h"
#include "ui_performance_timing_rows.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }

    bool Near(float a, float b) { return std::abs(a - b) < 0.01f; }

    struct Context
    {
        explicit Context(bool stock)
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(640, 480);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.ConfigInputTrickleEventQueue = false;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ImGui::GetStyle().FrameRounding = 4;
            ImGui::GetStyle().PopupRounding = 4;
            ImGui::SetUvsrUiBehavior(stock);
        }

        ~Context() { ImGui::DestroyContext(); }

        static void Mouse(ImVec2 position, bool down = false)
        {
            ImGui::GetIO().AddMousePosEvent(position.x, position.y);
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        }

        static void Begin(float y = 30)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(30, y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(300, 160), ImGuiCond_Always);
            ImGui::Begin("controls", nullptr, ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        }

        static void End()
        {
            ImGui::End();
            ImGui::Render();
            const auto* data = ImGui::GetDrawData();
            Require(data && data->Valid, "valid draw data");
            for (const auto* list : data->CmdLists)
            {
                for (const auto& vertex : list->VtxBuffer)
                    Require(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y),
                        "finite widget geometry");
                for (const auto& command : list->CmdBuffer)
                    Require(command.ClipRect.z >= command.ClipRect.x &&
                        command.ClipRect.w >= command.ClipRect.y, "ordered clip rectangles");
            }
        }
    };

    struct Combo
    {
        ImGuiID id = 0;
        int selection = 1;
        int presses = 0;
        bool open = false;
        ImRect first;

        void Frame(float y, bool forceOpen = false)
        {
            Context::Begin(y);
            const auto spacing = ImGui::GetStyle().ItemSpacing;
            const auto padding = ImGui::GetStyle().WindowPadding;
            id = ImGui::GetID("mode");
            const auto popup = ImHashStr("##ComboPopup", 0, id);
            if (forceOpen)
                ImGui::OpenPopupEx(popup, ImGuiPopupFlags_None);
            ImGui::SetNextItemWidth(180);
            ImGui::SetNextWindowSizeConstraints(ImVec2(180, 100), ImVec2(180, 100));
            if (ImGui::BeginCombo("mode", "selected"))
            {
                auto* window = ImGui::GetCurrentWindow();
                const char* labels[] = {"one", "two", "three"};
                for (int i = 0; i < 3; ++i)
                {
                    if (ImGui::Selectable(labels[i], i == selection))
                    {
                        selection = i;
                        ++presses;
                    }
                    if (i == 0)
                        first = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                }
                ImGui::EndCombo();
                if (!window->Hidden && !window->Appearing)
                {
                    Require(first.GetWidth() > 0 && first.GetHeight() > 0,
                        "settled popup has a usable option hit area");
                    Require(first.Min.y >= 0 && first.Max.y <= ImGui::GetIO().DisplaySize.y,
                        "popup options remain in the viewport");
                }
            }
            open = ImGui::IsPopupOpen(popup, ImGuiPopupFlags_None);
            Require(Near(spacing.y, ImGui::GetStyle().ItemSpacing.y) &&
                Near(padding.y, ImGui::GetStyle().WindowPadding.y), "combo restores caller style");
            Context::End();
        }
    };

    void CheckCombo(bool stock, float y)
    {
        Context context(stock);
        Combo combo;
        Context::Mouse(ImVec2(-100, -100));
        combo.Frame(y);
        Require(!combo.open, "combo starts closed");
        combo.Frame(y, true);
        Require(combo.open, "combo opens immediately");
        combo.Frame(y);
        Context::Mouse(combo.first.GetCenter());
        combo.Frame(y);
        Context::Mouse(combo.first.GetCenter(), true);
        combo.Frame(y);
        Context::Mouse(combo.first.GetCenter());
        combo.Frame(y);
        Require(combo.selection == 0 && combo.presses == 1, "selection commits once");
        Require(!combo.open, "selection closes the combo immediately");
        combo.Frame(y, true);
        Require(combo.open, "closed combo can reopen");
    }

    void CheckSliderAndToggle(bool stock)
    {
        Context context(stock);
        int value = 3;
        bool enabled = false;
        ImRect slider, toggle;
        auto frame = [&](bool disabled)
        {
            Context::Begin();
            ImGui::Checkbox("enabled", &enabled);
            toggle = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::BeginDisabled(disabled);
            ImGui::SetNextItemWidth(220);
            ImGui::SliderInt("##samples", &value, 0, 6, "%d", ImGuiSliderFlags_AlwaysClamp);
            slider = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::EndDisabled();
            Context::End();
        };
        frame(false);
        Context::Mouse(toggle.GetCenter());
        frame(false);
        Context::Mouse(toggle.GetCenter(), true);
        frame(false);
        Context::Mouse(toggle.GetCenter());
        frame(false);
        Require(enabled, "toggle commits its backing value on release");
        const auto target = ImVec2(slider.Min.x + 8, slider.GetCenter().y);
        Context::Mouse(target, true);
        frame(true);
        Context::Mouse(target);
        frame(true);
        Require(value == 3, "disabled slider preserves its backing value");
        Context::Mouse(target, true);
        frame(false);
        Context::Mouse(target);
        frame(false);
        Require(value < 3 && value >= 0, "enabled slider accepts bounded input");
        Require(Near(slider.GetWidth(), 220), "slider preserves its authored total width");
    }

    void CheckPicker(bool stock, bool alpha)
    {
        Context context(stock);
        float color[] = {0.2f, 0.4f, 0.7f, 0.6f};
        ImGuiID popup = 0;
        auto frame = [&](bool open, bool close)
        {
            Context::Begin();
            if (close)
                ImGui::CloseUvsrColorPickerPopup();
            const ImVec4 surface(0.17f, 0.29f, 0.41f, 1);
            ImGui::PushUvsrColorPickerPopupContentRight(320, 450, surface, surface, surface, surface);
            ImGui::PushID("color");
            popup = ImGui::GetID("picker");
            if (open)
                ImGui::OpenPopupEx(popup, ImGuiPopupFlags_None);
            ImGui::PopID();
            const auto flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB |
                ImGuiColorEditFlags_PickerHueWheel;
            if (alpha)
                ImGui::ColorEdit4("color", color, flags | ImGuiColorEditFlags_AlphaBar);
            else
                ImGui::ColorEdit3("color", color, flags);
            ImGui::PopUvsrColorPickerPopupContentRight();
            Context::End();
        };
        frame(false, false);
        frame(true, false);
        frame(false, false);
        Require(GImGui->OpenPopupStack.Size == 1, "color picker owns one popup");
        const auto* window = GImGui->OpenPopupStack.back().Window;
        Require(window && window->Size.x > 0 && window->Size.y > 0 &&
            window->Pos.x >= 0 && window->Pos.y >= 0 &&
            window->Pos.x + window->Size.x <= 640.5f &&
            window->Pos.y + window->Size.y <= 480.5f, "picker fits the viewport");
        frame(false, true);
        Require(GImGui->OpenPopupStack.empty(), "targeted picker close releases the popup");
        Require(Near(color[3], 0.6f), "opening and closing preserve alpha, including RGB mode");
    }

    void CheckCollapse(bool stock)
    {
        Context context(stock);
        float height = 0;
        auto frame = [&](bool collapsed)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_Always);
            ImGui::SetNextUvsrWindowCollapsedHeight(60);
            ImGui::SetNextWindowCollapsed(collapsed, ImGuiCond_Always);
            ImGui::Begin("settings", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextUnformatted("summary");
            height = ImGui::GetWindowHeight();
            Context::End();
        };
        for (bool collapsed : {false, true, false})
        {
            frame(collapsed);
            Require(Near(height, collapsed ? 60.0f : 200.0f),
                "managed root immediately takes its requested compact or expanded height");
        }
    }

    void CheckTimingRows()
    {
        uvsr::PerformanceTimingRowRetention rows;
        Require(!rows.Resolve(10, 7, 41, false).IsVisible(), "unmeasured timing is hidden");
        const auto measured = rows.Resolve(10, 7, 0.375, true);
        Require(measured.HasMeasurement() && measured.milliseconds == 0.375, "timing reports the measurement");
        const auto unavailable = rows.Resolve(10, 7, 99, false);
        Require(unavailable.IsVisible() && !unavailable.HasMeasurement() && unavailable.milliseconds == 0,
            "timing retains an unavailable placeholder across resets");
        Require(!rows.Resolve(0, 7, 0, false).IsVisible(), "timing retention is local to the view");
    }
}

int main()
{
    for (bool stock : {false, true})
    {
        for (float y : {30.0f, 390.0f})
            CheckCombo(stock, y);
        CheckSliderAndToggle(stock);
        CheckCollapse(stock);
        for (bool alpha : {false, true})
            CheckPicker(stock, alpha);
    }
    CheckTimingRows();
    std::cout << "ImGui interaction and geometry contracts passed\n";
}
