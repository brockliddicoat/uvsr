#include "imgui.h"
#include "imgui_internal.h"
#include "ui_performance_timing_rows.h"
#include "ui_layout.h"
#include <limits>
#include <cstring>

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
        Context()
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

    void CheckCombo(float y)
    {
        Context context;
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

    void CheckSliderAndToggle()
    {
        Context context;
        int value = 3;
        bool enabled = false;
        ImRect slider, toggle;
        auto frame = [&](bool disabled)
        {
            Context::Begin();
            ImGui::GetStyle().Colors[ImGuiCol_FrameBg] = ImVec4(.13f, .19f, .29f, 1.f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.f);
            Require((ImGui::GetColorU32(ImGuiCol_FrameBg) & IM_COL32_A_MASK) == 0,
                "hidden blue controls remain invisible");
            ImGui::PopStyleVar();
            ImGui::Checkbox("enabled", &enabled);
            toggle = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::BeginDisabled(disabled);
            Require((ImGui::GetColorU32(ImGuiCol_FrameBg) & IM_COL32_A_MASK) == IM_COL32_A_MASK,
                "visible disabled blue controls stay opaque");
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

    void CheckPicker(bool alpha)
    {
        Context context;
        float color[] = {0.2f, 0.4f, 0.7f, 0.6f};
        ImGuiID popup = 0;
        auto frame = [&](bool open, bool close)
        {
            Context::Begin();
            if (close)
                ImGui::CloseUvsrColorPickerPopup();
            ImGui::SetUvsrColorPickerBounds(320, 450);
            ImGui::PushID("color");
            popup = ImGui::GetID("picker");
            if (open)
                ImGui::OpenPopupEx(popup, ImGuiPopupFlags_None);
            ImGui::PopID();
            const auto flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB |
                ImGuiColorEditFlags_PickerHueBar;
            if (alpha)
                ImGui::ColorEdit4("color", color, flags | ImGuiColorEditFlags_AlphaBar);
            else
                ImGui::ColorEdit3("color", color, flags);
            ImGui::SetUvsrColorPickerBounds(0, 0);
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

    void CheckCollapse()
    {
        Context context;
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

    void CheckNumbers()
    {
        char text[64];
        for (double value : { 0.0, -0.0, 0.001, 0.0001, 0.0999, 0.99999, 1.0, 2.0, 9.99999,
                10.0, 99.9999, 135.0, 999.9999, 9999.9, 10000.0, 1.e6, -1.e12, 1.e100,
                std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max() })
        {
            const double before = value;
            ImGui::FormatUvsrNumber(text, sizeof(text), ImGuiDataType_Double, &value, "%.17g");
            int digits = 0;
            for (const char* p = text; *p; ++p) digits += *p >= '0' && *p <= '9';
            if (digits != 4) std::cerr << value << " -> " << text << '\n';
            Require(digits == 4 && value == before, "display has exactly four digits without changing the value");
        }
        float degrees = -135.f;
        ImGui::FormatUvsrNumber(text, sizeof(text), ImGuiDataType_Float, &degrees, "%.1f\xC2\xB0");
        Require(std::strcmp(text, "-135.0\xC2\xB0") == 0, "angle keeps its sign and degree symbol");
        int integer = 3;
        ImGui::FormatUvsrNumber(text, sizeof(text), ImGuiDataType_S32, &integer, "%d");
        Require(std::strcmp(text, "0003") == 0, "integer display has four digits");
        float negativeZero = -0.f;
        ImGui::FormatUvsrNumber(text, sizeof(text), ImGuiDataType_Float, &negativeZero, "%+.2f");
        Require(std::strcmp(text, "-0.000") == 0, "negative zero has only one sign");
    }

    void CheckNumberEditing()
    {
        Context context;
        float value = 0.123456789f;
        const float before = value;
        ImRect input;
        auto frame = [&]()
        {
            Context::Begin();
            ImGui::SetNextItemWidth(200);
            ImGui::InputFloat("number", &value);
            input = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            Context::End();
        };
        Context::Mouse(ImVec2(-100, -100));
        frame(); frame();
        Context::Mouse(ImVec2(input.Min.x + 20, input.GetCenter().y)); frame();
        Context::Mouse(ImVec2(input.Min.x + 20, input.GetCenter().y), true); frame();
        Context::Mouse(ImVec2(input.Min.x + 20, input.GetCenter().y)); frame();
        Require(value == before && std::strtof(GImGui->InputTextState.TextA.Data, nullptr) == before,
            "focus preserves full editing precision");
        ImGui::GetIO().AddInputCharactersUTF8("0.987654321"); frame();
        Require(value == 0.987654321f, "number input accepts full precision");
        Context::Mouse(ImVec2(350, 250), true); frame();
        Context::Mouse(ImVec2(350, 250)); frame();
        Require(value == 0.987654321f, "deactivation preserves the edited value");
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
    for (float y : {30.0f, 390.0f})
        CheckCombo(y);
    CheckSliderAndToggle();
    CheckCollapse();
    for (bool alpha : {false, true})
        CheckPicker(alpha);
    CheckNumbers();
    CheckNumberEditing();
    const auto minimum = uvsr::ResolveUiSpacingTokens(.5f), normal = uvsr::ResolveUiSpacingTokens(1);
    const auto maximum = uvsr::ResolveUiSpacingTokens(4);
    Require(normal.tight == 2 * minimum.tight && maximum.tight == 4 * normal.tight &&
        uvsr::ResolveUiSpacingTokens(0).section == minimum.section && uvsr::ResolveUiSpacingTokens(8).section == maximum.section,
        "spacing respects display scale bounds");
    Require(Near(uvsr::ResolvePerformanceMaximumWindowHeight(900, 20, 300, 8), 572) &&
        uvsr::ResolvePerformanceMaximumWindowHeight(0, 100, -1, -1) >= 1, "performance panel fits available height");
    CheckTimingRows();
    std::cout << "ImGui interaction and geometry contracts passed\n";
}
